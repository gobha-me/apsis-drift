#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "termforge/core/app.hpp"
#include "termforge/drivers/ansi_rgb_driver.hpp"
#include "termforge/drivers/fallback_driver.hpp"
#include "termforge/drivers/kitty_driver.hpp"
#include "termforge/widgets/pixel_surface.hpp"
#include "apsis_drift/benchmark.hpp"
#include "apsis_drift/landscape.hpp"
#include "apsis_drift/render_profile.hpp"
#include "simulation.hpp"

namespace {

using Clock = std::chrono::steady_clock;
using namespace termforge;
using namespace apsis_drift;
namespace simulation = apsis_drift::detail;

struct FrameSample {
  std::uint64_t bytes{};
  double render_ms{};
  double work_ms{};
};

enum class DriverChoice { automatic, kitty, ansi, fallback };

[[nodiscard]] auto elapsed_ms(Clock::time_point from,
                              Clock::time_point to) noexcept -> double {
  return std::chrono::duration<double, std::milli>(to - from).count();
}

class MeasuringSink final : public ByteSink {
 public:
  auto set_fd(int fd) noexcept -> void { m_fd = fd; }

  auto begin_frame(Clock::time_point started, double render_ms) noexcept
      -> void {
    m_started = started;
    m_render_ms = render_ms;
    m_pending = true;
  }

  [[nodiscard]] auto write(std::span<const char> bytes)
      -> std::expected<void, ErrorEvent> override {
    if (m_fd >= 0) {
      std::size_t offset = 0;
      while (offset < bytes.size()) {
        const auto count =
            ::write(m_fd, bytes.data() + offset, bytes.size() - offset);
        if (count > 0) {
          offset += static_cast<std::size_t>(count);
          continue;
        }
        if (count < 0 && errno == EINTR) continue;
        return std::unexpected{ErrorEvent{
            Severity::Warning, "landscape",
            count == 0 ? "frame sink wrote zero bytes"
                       : std::format("frame sink write failed: {}",
                                     std::strerror(errno))}};
      }
    }

    const auto finished = Clock::now();
    if (m_pending) {
      if (m_samples.empty()) m_first_frame = m_started;
      m_samples.push_back(FrameSample{
          .bytes = bytes.size(),
          .render_ms = m_render_ms,
          .work_ms = elapsed_ms(m_started, finished),
      });
      m_last_frame = finished;
      m_pending = false;
    }
    return {};
  }

  [[nodiscard]] auto summary(std::uint64_t checksum) const
      -> BenchmarkSummary {
    BenchmarkSummary result;
    result.frames = m_samples.size();
    result.checksum = checksum;
    if (m_samples.empty()) return result;

    std::vector<double> renders;
    std::vector<double> work;
    renders.reserve(m_samples.size());
    work.reserve(m_samples.size());
    for (const auto& sample : m_samples) {
      result.total_bytes += sample.bytes;
      result.render_avg_ms += sample.render_ms;
      result.work_avg_ms += sample.work_ms;
      renders.push_back(sample.render_ms);
      work.push_back(sample.work_ms);
    }
    const double count = static_cast<double>(m_samples.size());
    result.render_avg_ms /= count;
    result.work_avg_ms /= count;
    result.bytes_per_frame = static_cast<double>(result.total_bytes) / count;
    result.elapsed_seconds =
        std::chrono::duration<double>(m_last_frame - m_first_frame).count();
    if (result.elapsed_seconds > 0.0) {
      result.achieved_fps = count / result.elapsed_seconds;
      result.mebibytes_per_second =
          static_cast<double>(result.total_bytes) / result.elapsed_seconds /
          (1024.0 * 1024.0);
    }

    std::sort(renders.begin(), renders.end());
    std::sort(work.begin(), work.end());
    const std::size_t p95 = std::min(
        renders.size() - 1,
        static_cast<std::size_t>(std::ceil(renders.size() * 0.95)) - 1);
    result.render_p95_ms = renders[p95];
    result.work_p95_ms = work[p95];
    return result;
  }

 private:
  int m_fd{-1};
  bool m_pending{false};
  double m_render_ms{};
  Clock::time_point m_started{};
  Clock::time_point m_first_frame{};
  Clock::time_point m_last_frame{};
  std::vector<FrameSample> m_samples;
};

[[nodiscard]] auto required_terrain(int size, std::uint32_t seed) -> Terrain {
  auto generated = Terrain::generate(size, seed);
  if (!generated) throw std::runtime_error{"terrain generation failed"};
  return std::move(*generated);
}

[[nodiscard]] auto aspect_fit(Rect available, Extent per_cell,
                              ViewportSize viewport) -> Rect {
  if (available.empty()) return {};
  const int cell_w = std::max(1, per_cell.w);
  const int cell_h = std::max(1, per_cell.h);
  int width = available.w;
  int height = static_cast<int>(
      (static_cast<std::int64_t>(width) * viewport.height * cell_w) /
      (static_cast<std::int64_t>(viewport.width) * cell_h));
  if (height <= 0) height = 1;
  if (height > available.h) {
    height = available.h;
    width = static_cast<int>(
        (static_cast<std::int64_t>(height) * viewport.width * cell_h) /
        (static_cast<std::int64_t>(viewport.height) * cell_w));
    width = std::clamp(width, 1, available.w);
  }
  return {available.x + (available.w - width) / 2,
          available.y + (available.h - height) / 2, width, height};
}

[[nodiscard]] auto render_settings_for(ViewportSize viewport)
    -> RenderSettings {
  RenderSettings settings;
  settings.width = viewport.width;
  settings.height = viewport.height;
  settings.vertical_scale *= static_cast<float>(viewport.height) /
                             static_cast<float>(kFrameHeight);
  return settings;
}

class LandscapeApp final : public App {
 public:
  explicit LandscapeApp(RenderConfiguration render_configuration,
                        std::uint32_t seed, double capture_seconds = 0.0)
      : m_render_configuration(render_configuration),
        m_terrain(required_terrain(1024, seed)),
        m_renderer(render_settings_for(render_configuration.viewport)),
        m_surface({render_configuration.viewport.width,
                   render_configuration.viewport.height},
                  {0, 0, 0, 255}),
        m_capture_seconds(capture_seconds),
        m_seed(seed) {
    set_frame_ms(33);
    // TermForge supplies elapsed host time. Apsis Drift owns the fixed-step
    // accumulator and its bounded catch-up policy.
    set_tick_hz(0);
    set_max_tick_dt(std::chrono::duration<double>::zero());
    set_mouse_mode(MouseMode::None);
    set_keyboard_mode(KeyboardMode::Enhanced);
    m_flight.camera.height =
        std::max<float>(m_terrain.height_at(
                            static_cast<int>(m_flight.camera.x),
                            static_cast<int>(m_flight.camera.y)),
                        kWaterLevel) +
        m_flight.camera.clearance;
    render_landscape();
  }

  auto on_start() -> void override {
    m_sink.set_fd(terminal().io().out);
    driver().set_output(&m_sink);
    m_output_bound = true;
    const auto& caps = capabilities();
    const std::string_view tier = caps.kitty_graphics
                                      ? "kitty"
                                      : (caps.truecolor ? "ansi" : "fallback");
    m_display_tier = tier;
    m_display_path = std::format(
        "{} (kitty_graphics={}, truecolor={}, kitty_keyboard={}, sync={})",
        tier, caps.kitty_graphics, caps.truecolor, caps.kitty_keyboard,
        caps.sync_updates);
    if (m_capture_seconds > 0.0 && !capabilities().kitty_graphics) {
      m_error = "capture mode requires negotiated Kitty graphics";
      quit();
    }
  }

  auto on_event(const Event& event) -> void override {
    if (const auto* error = std::get_if<ErrorEvent>(&event)) {
      m_error = error->message;
    } else if (const auto* key = std::get_if<KeyEvent>(&event)) {
      handle_key(*key);
    }
    App::on_event(event);
  }

  auto on_tick(std::chrono::duration<double> dt) -> void override {
    advance_simulation(dt);
  }

  auto on_render(Screen& screen) -> void override {
    if (!m_output_bound) {
      driver().set_output(&m_sink);
      m_output_bound = true;
    }
    const auto frame_started = Clock::now();
    const auto render_started = Clock::now();
    render_landscape();
    const double render_time = elapsed_ms(render_started, Clock::now());
    m_sink.begin_frame(frame_started, render_time);

    screen.clear();
    const auto totals = driver().total_bytes();
    screen.write_text(
        0, 0,
        std::format(
            " fractal landscape {}x{} | {} | {} | seed {} | frame {} | "
            "{:.2f} ms | {:.1f} MiB ",
            m_render_configuration.viewport.width,
            m_render_configuration.viewport.height,
            profile_name(m_render_configuration), m_display_tier, m_seed,
            m_frame, render_time,
            static_cast<double>(totals.total()) / (1024.0 * 1024.0)),
        {238, 243, 247}, {20, 43, 66});

    if (screen.rows() > 1) {
      const std::string status =
          m_error.empty()
              ? std::format(
                    " {} | arrows/WASD move | Q/E strafe | R/F altitude | "
                    "Space autopilot | ESC quit ",
                    m_autopilot ? "AUTOPILOT" : "MANUAL")
              : " ERROR: " + m_error + " | ESC quit ";
      screen.write_text(0, screen.rows() - 1, status, {190, 208, 214},
                        {13, 25, 35});
    }

    const Rect available{0, 1, screen.cols(),
                         std::max(0, screen.rows() - 2)};
    const Extent one_cell = driver().preferred_pixel_extent({0, 0, 1, 1});
    m_surface.set_geometry(
        aspect_fit(available, one_cell, m_render_configuration.viewport));
    m_surface.draw(screen);
    render_pixel_regions(m_surface);
    ++m_frame;

    if (m_run_started == Clock::time_point{}) m_run_started = frame_started;
    if (m_capture_seconds > 0.0 &&
        std::chrono::duration<double>(Clock::now() - m_run_started).count() >=
            m_capture_seconds) {
      quit();
    }
  }

  auto benchmark(int frames) -> void {
    // TermForge issue #256: test_run_frames() skips setup(), so its real stdin
    // remains a blocking cooked TTY when a developer launches the benchmark
    // interactively. Give this headless run an EOF input until the framework
    // seam owns that invariant itself.
    const int null_input = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (null_input < 0) {
      throw std::runtime_error{
          std::format("cannot open /dev/null: {}", std::strerror(errno))};
    }
    const auto injected = terminal().set_io({null_input, -1});
    if (!injected) {
      ::close(null_input);
      throw std::runtime_error{injected.error().message};
    }
    // The borrowed clock makes the ordinary frame/tick path advance by a
    // deterministic 33 ms per rendered frame without sleeping. The readiness
    // override below prevents /dev/null's persistent EOF from shortening that
    // synthetic wait. Renderer and wire measurements intentionally continue
    // to use the real clock.
    m_synthetic_headless = true;
    set_clock(&m_headless_clock);
    set_frame_ms(33);
    auto selected = std::make_unique<KittyDriver>();
    selected->set_cell_pixel_size({8, 16});
    try {
      test_run_frames(frames, 100, 40, nullptr, std::move(selected));
    } catch (...) {
      ::close(null_input);
      set_clock(nullptr);
      m_synthetic_headless = false;
      throw;
    }
    ::close(null_input);
    set_clock(nullptr);
    m_synthetic_headless = false;
    m_display_tier = "kitty-headless";
    m_display_path = "kitty (headless, /dev/null input workaround for #256)";
  }

  [[nodiscard]] auto force_driver(DriverChoice choice)
      -> std::expected<void, ErrorEvent> {
    if (choice == DriverChoice::automatic) return {};
    Capabilities caps;
    if (choice == DriverChoice::kitty) {
      caps.kitty_graphics = true;
      caps.truecolor = true;
      caps.color_levels = 24;
      caps.kitty_keyboard = true;
    } else if (choice == DriverChoice::ansi) {
      caps.truecolor = true;
      caps.color_levels = 24;
    }
    return terminal().set_capabilities(caps);
  }

  [[nodiscard]] auto summary() const -> BenchmarkSummary {
    return m_sink.summary(pixel_checksum(m_surface.pixels()));
  }

  [[nodiscard]] auto error() const -> const std::string& { return m_error; }
  [[nodiscard]] auto display_path() const -> const std::string& {
    return m_display_path;
  }
  [[nodiscard]] auto render_configuration() const noexcept
      -> const RenderConfiguration& {
    return m_render_configuration;
  }

  [[nodiscard]] auto write_snapshot(const std::filesystem::path& path) const
      -> bool {
    std::ofstream output{path, std::ios::binary};
    if (!output) return false;
    output << "P6\n" << m_render_configuration.viewport.width << ' '
           << m_render_configuration.viewport.height << "\n255\n";
    for (const auto pixel : m_surface.pixels()) {
      const char rgb[] = {static_cast<char>(pixel.r),
                          static_cast<char>(pixel.g),
                          static_cast<char>(pixel.b)};
      output.write(rgb, sizeof(rgb));
    }
    return output.good();
  }

 protected:
  auto wait_readable(int timeout_ms) -> bool override {
    if (m_synthetic_headless) return false;
    return App::wait_readable(timeout_ms);
  }

 private:
  enum class Control { forward, backward, left, right, strafe_left,
                       strafe_right, rise, fall };

  auto handle_key(const KeyEvent& key) -> void {
    if (key.action == KeyAction::Press && key.key == Key::Char &&
        key.ch == U' ') {
      m_autopilot = !m_autopilot;
      clear_controls();
      return;
    }

    std::optional<Control> control;
    if (key.key == Key::Up) control = Control::forward;
    if (key.key == Key::Down) control = Control::backward;
    if (key.key == Key::Left) control = Control::left;
    if (key.key == Key::Right) control = Control::right;
    if (key.key == Key::Char) {
      char32_t ch = key.ch;
      if (ch >= U'A' && ch <= U'Z') ch += U'a' - U'A';
      if (ch == U'w') control = Control::forward;
      if (ch == U's') control = Control::backward;
      if (ch == U'a') control = Control::left;
      if (ch == U'd') control = Control::right;
      if (ch == U'q') control = Control::strafe_left;
      if (ch == U'e') control = Control::strafe_right;
      if (ch == U'r') control = Control::rise;
      if (ch == U'f') control = Control::fall;
    }
    if (!control) return;

    m_autopilot = false;
    if (capabilities().kitty_keyboard) {
      set_control(*control, key.action != KeyAction::Release);
    } else if (key.action != KeyAction::Release) {
      nudge(*control);
    }
  }

  auto set_control(Control control, bool active) -> void {
    switch (control) {
      case Control::forward: m_forward = active; break;
      case Control::backward: m_backward = active; break;
      case Control::left: m_left = active; break;
      case Control::right: m_right = active; break;
      case Control::strafe_left: m_strafe_left = active; break;
      case Control::strafe_right: m_strafe_right = active; break;
      case Control::rise: m_rise = active; break;
      case Control::fall: m_fall = active; break;
    }
  }

  auto nudge(Control control) -> void {
    constexpr double step{0.08};
    clear_controls();
    set_control(control, true);
    advance_simulation(std::chrono::duration<double>{step});
    set_control(control, false);
  }

  auto clear_controls() noexcept -> void {
    m_forward = m_backward = m_left = m_right = false;
    m_strafe_left = m_strafe_right = m_rise = m_fall = false;
  }

  [[nodiscard]] auto flight_input() const noexcept
      -> simulation::FlightInput {
    return {
        .forward = static_cast<float>(m_forward) -
                   static_cast<float>(m_backward),
        .turn = static_cast<float>(m_right) - static_cast<float>(m_left),
        .strafe = static_cast<float>(m_strafe_right) -
                  static_cast<float>(m_strafe_left),
        .vertical = static_cast<float>(m_rise) - static_cast<float>(m_fall),
        .autopilot = m_autopilot,
    };
  }

  auto advance_simulation(std::chrono::duration<double> elapsed) -> void {
    const auto advance = m_simulation_clock.advance(elapsed);
    if (!advance) {
      throw std::runtime_error{"simulation clock rejected elapsed time"};
    }
    const auto input = flight_input();
    for (int step = 0; step < advance->steps; ++step) {
      if (!simulation::advance_flight(m_terrain, m_flight, input,
                                      simulation::kSimulationStep)) {
        throw std::runtime_error{"simulation rejected flight state"};
      }
    }
  }

  [[nodiscard]] auto scaled_vertical(float baseline) const noexcept -> float {
    return baseline *
           static_cast<float>(m_render_configuration.viewport.height) /
           static_cast<float>(kFrameHeight);
  }

  auto render_landscape() -> void {
    Camera camera = m_flight.camera;
    camera.horizon =
        scaled_vertical(205.0F) +
        std::sin(static_cast<float>(m_flight.elapsed_seconds) * 0.17F) *
            scaled_vertical(5.0F);
    if (!m_renderer.render(m_terrain, camera, m_surface.pixels())) {
      throw std::runtime_error{std::format(
          "renderer rejected the {}x{} surface",
          m_render_configuration.viewport.width,
          m_render_configuration.viewport.height)};
    }
  }

  RenderConfiguration m_render_configuration;
  Terrain m_terrain;
  VoxelRenderer m_renderer;
  PixelSurface m_surface;
  simulation::FlightRuntime m_flight;
  simulation::FixedStepClock m_simulation_clock;
  SyntheticClock m_headless_clock;
  MeasuringSink m_sink;
  Clock::time_point m_run_started{};
  double m_capture_seconds{};
  std::uint32_t m_seed{};
  int m_frame{};
  bool m_output_bound{false};
  bool m_synthetic_headless{false};
  bool m_autopilot{true};
  bool m_forward{}, m_backward{}, m_left{}, m_right{};
  bool m_strafe_left{}, m_strafe_right{}, m_rise{}, m_fall{};
  std::string m_error;
  std::string m_display_tier{"probing"};
  std::string m_display_path{"not started"};
};

auto print_summary(const BenchmarkSummary& summary,
                   const RenderConfiguration& configuration) -> void {
  std::printf(
      "landscape: profile=%.*s viewport=%dx%d frames=%zu elapsed=%.3fs "
      "achieved=%.2f fps\n"
      "timing: renderer avg/p95 %.3f/%.3f ms, full frame work %.3f/%.3f ms\n"
      "wire: %.1f KiB/frame, %.2f MiB/s, total %.2f MiB\n"
      "checksum: %llu\n",
      static_cast<int>(profile_name(configuration).size()),
      profile_name(configuration).data(), configuration.viewport.width,
      configuration.viewport.height, summary.frames, summary.elapsed_seconds,
      summary.achieved_fps,
      summary.render_avg_ms, summary.render_p95_ms, summary.work_avg_ms,
      summary.work_p95_ms, summary.bytes_per_frame / 1024.0,
      summary.mebibytes_per_second,
      static_cast<double>(summary.total_bytes) / (1024.0 * 1024.0),
      static_cast<unsigned long long>(summary.checksum));
}

[[nodiscard]] auto summary_json(const BenchmarkSummary& summary,
                                const RenderConfiguration& configuration)
    -> std::string {
  return std::format(
      "{{\n"
      "  \"workload\": \"voxel-landscape-{}x{}-rgba\",\n"
      "  \"render_profile\": \"{}\",\n"
      "  \"viewport_width\": {},\n"
      "  \"viewport_height\": {},\n"
      "  \"frames\": {},\n"
      "  \"elapsed_seconds\": {:.6f},\n"
      "  \"achieved_fps\": {:.6f},\n"
      "  \"renderer_avg_ms\": {:.6f},\n"
      "  \"renderer_p95_ms\": {:.6f},\n"
      "  \"frame_work_avg_ms\": {:.6f},\n"
      "  \"frame_work_p95_ms\": {:.6f},\n"
      "  \"bytes_per_frame\": {:.6f},\n"
      "  \"mebibytes_per_second\": {:.6f},\n"
      "  \"total_bytes\": {},\n"
      "  \"checksum\": {}\n"
      "}}\n",
      configuration.viewport.width, configuration.viewport.height,
      profile_name(configuration), configuration.viewport.width,
      configuration.viewport.height,
      summary.frames, summary.elapsed_seconds, summary.achieved_fps,
      summary.render_avg_ms, summary.render_p95_ms, summary.work_avg_ms,
      summary.work_p95_ms, summary.bytes_per_frame,
      summary.mebibytes_per_second, summary.total_bytes, summary.checksum);
}

template <typename T>
[[nodiscard]] auto parse_positive(std::string_view text, T& value) -> bool {
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size() && value > 0;
}

auto usage() -> void {
  std::puts(
      "Usage: apsis-drift [--seed N] [--profile NAME] "
      "[--viewport WIDTHxHEIGHT]\n"
      "       apsis-drift [--driver automatic|kitty|ansi|fallback]\n"
      "       apsis-drift --benchmark [FRAMES] [--seed N] [--report PATH]\n"
      "       apsis-drift --sweep [FRAMES] --report PATH\n"
      "                   [--sweep-viewports LIST] [--sweep-fps LIST]\n"
      "       apsis-drift --capture-seconds N [--seed N] --report PATH\n\n"
      "Profiles: remote (320x240), balanced (512x320), local (640x480, "
      "default),\n"
      "and cinematic (1024x768). An explicit viewport overrides the "
      "profile.\n"
      "Sweep defaults: remote,balanced,local at 30,60 FPS. Viewport list\n"
      "entries may be profile names or validated WIDTHxHEIGHT values.\n"
      "Add --snapshot PATH to save the final framebuffer as a binary PPM.\n"
      "Interactive controls: arrows/WASD move, Q/E strafe, R/F altitude,\n"
      "Space toggles autopilot, and Escape quits.");
}

}  // namespace

auto main(int argc, char** argv) -> int {
  std::optional<int> benchmark_frames;
  std::optional<int> sweep_frames;
  int capture_seconds = 0;
  std::uint32_t seed = 0xC0FFEEU;
  DriverChoice driver_choice{DriverChoice::automatic};
  RenderProfile selected_profile{RenderProfile::local};
  std::optional<ViewportSize> viewport_override;
  auto sweep_viewports = default_sweep_viewports();
  auto sweep_fps = default_sweep_fps();
  std::filesystem::path report_path;
  std::filesystem::path snapshot_path;
  bool profile_specified{};
  bool viewport_specified{};
  bool driver_specified{};
  bool sweep_viewports_specified{};
  bool sweep_fps_specified{};

  for (int i = 1; i < argc; ++i) {
    const std::string_view argument{argv[i]};
    if (argument == "--help" || argument == "-h") {
      usage();
      return 0;
    }
    if (argument == "--benchmark") {
      benchmark_frames = 180;
      if (i + 1 < argc) {
        int value{};
        if (parse_positive(std::string_view{argv[i + 1]}, value)) {
          benchmark_frames = value;
          ++i;
        }
      }
      continue;
    }
    if (argument == "--sweep") {
      sweep_frames = 180;
      const std::string_view next =
          i + 1 < argc ? std::string_view{argv[i + 1]} : std::string_view{};
      if (!next.empty() && next.front() != '-') {
        int value{};
        if (!parse_positive(next, value)) {
          std::fprintf(stderr, "sweep frames must be positive\n");
          return 2;
        }
        sweep_frames = value;
        ++i;
      }
      continue;
    }
    if (argument == "--capture-seconds" && i + 1 < argc) {
      if (!parse_positive(std::string_view{argv[++i]}, capture_seconds)) {
        std::fprintf(stderr, "capture seconds must be positive\n");
        return 2;
      }
      continue;
    }
    if (argument == "--seed" && i + 1 < argc) {
      if (!parse_positive(std::string_view{argv[++i]}, seed)) {
        std::fprintf(stderr, "seed must be a positive 32-bit integer\n");
        return 2;
      }
      continue;
    }
    if (argument == "--profile" && i + 1 < argc) {
      const std::string_view value{argv[++i]};
      const auto parsed = parse_render_profile(value);
      if (!parsed) {
        std::fprintf(stderr, "unknown render profile '%.*s'\n",
                     static_cast<int>(value.size()), value.data());
        return 2;
      }
      selected_profile = *parsed;
      profile_specified = true;
      continue;
    }
    if (argument == "--viewport" && i + 1 < argc) {
      const std::string_view value{argv[++i]};
      const auto parsed = parse_viewport(value);
      if (!parsed) {
        const auto message = viewport_error_message(parsed.error());
        std::fprintf(stderr, "invalid viewport '%.*s': %.*s\n",
                     static_cast<int>(value.size()), value.data(),
                     static_cast<int>(message.size()), message.data());
        return 2;
      }
      viewport_override = *parsed;
      viewport_specified = true;
      continue;
    }
    if (argument == "--sweep-viewports" && i + 1 < argc) {
      const auto parsed =
          parse_sweep_viewports(std::string_view{argv[++i]});
      if (!parsed) {
        std::fprintf(stderr, "%s\n", parsed.error().c_str());
        return 2;
      }
      sweep_viewports = *parsed;
      sweep_viewports_specified = true;
      continue;
    }
    if (argument == "--sweep-fps" && i + 1 < argc) {
      const auto parsed = parse_sweep_fps(std::string_view{argv[++i]});
      if (!parsed) {
        std::fprintf(stderr, "%s\n", parsed.error().c_str());
        return 2;
      }
      sweep_fps = *parsed;
      sweep_fps_specified = true;
      continue;
    }
    if (argument == "--driver" && i + 1 < argc) {
      const std::string_view value{argv[++i]};
      if (value == "automatic") driver_choice = DriverChoice::automatic;
      else if (value == "kitty") driver_choice = DriverChoice::kitty;
      else if (value == "ansi") driver_choice = DriverChoice::ansi;
      else if (value == "fallback") driver_choice = DriverChoice::fallback;
      else {
        std::fprintf(stderr, "unknown driver '%.*s'\n",
                     static_cast<int>(value.size()), value.data());
        return 2;
      }
      driver_specified = true;
      continue;
    }
    if (argument == "--report" && i + 1 < argc) {
      report_path = argv[++i];
      continue;
    }
    if (argument == "--snapshot" && i + 1 < argc) {
      snapshot_path = argv[++i];
      continue;
    }
    std::fprintf(stderr, "unknown or incomplete option '%.*s'\n",
                 static_cast<int>(argument.size()), argument.data());
    return 2;
  }

  if (benchmark_frames && capture_seconds > 0) {
    std::fprintf(stderr, "benchmark and capture modes are mutually exclusive\n");
    return 2;
  }
  if (sweep_frames && (benchmark_frames || capture_seconds > 0)) {
    std::fprintf(stderr,
                 "sweep, benchmark, and capture modes are mutually exclusive\n");
    return 2;
  }
  if (!sweep_frames && (sweep_viewports_specified || sweep_fps_specified)) {
    std::fprintf(stderr,
                 "sweep selection options require --sweep\n");
    return 2;
  }
  if (sweep_frames && (profile_specified || viewport_specified ||
                       driver_specified || !snapshot_path.empty())) {
    std::fprintf(stderr,
                 "sweep mode does not accept profile, viewport, driver, or "
                 "snapshot options\n");
    return 2;
  }
  if (sweep_frames && report_path.empty()) {
    std::fprintf(stderr, "sweep mode requires --report PATH\n");
    return 2;
  }
  if (capture_seconds > 0 && report_path.empty()) {
    std::fprintf(stderr, "capture mode requires --report PATH\n");
    return 2;
  }

  try {
    if (sweep_frames) {
      std::vector<BenchmarkMeasurement> measurements;
      measurements.reserve(sweep_viewports.size());
      for (const auto& configuration : sweep_viewports) {
        LandscapeApp app{configuration, seed};
        app.benchmark(*sweep_frames);
        auto summary = app.summary();
        if (summary.frames != static_cast<std::size_t>(*sweep_frames)) {
          throw std::runtime_error{std::format(
              "sweep expected {} frames for {}x{} but measured {}",
              *sweep_frames, configuration.viewport.width,
              configuration.viewport.height, summary.frames)};
        }
        measurements.push_back({configuration, summary});
      }

      std::ofstream report{report_path};
      if (!report) {
        std::fprintf(stderr, "cannot open report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
      report << sweep_json(measurements, sweep_fps, seed,
                           static_cast<std::size_t>(*sweep_frames));
      if (!report.good()) {
        std::fprintf(stderr, "cannot write report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }

      std::printf("sweep: seed=%u frames-per-viewport=%d\n", seed,
                  *sweep_frames);
      const auto table = sweep_table(measurements, sweep_fps);
      std::fputs(table.c_str(), stdout);
      return 0;
    }

    const RenderConfiguration render_configuration =
        resolve_render_configuration(selected_profile, viewport_override);
    LandscapeApp app{render_configuration, seed,
                     static_cast<double>(capture_seconds)};
    if (auto forced = app.force_driver(driver_choice); !forced) {
      std::fprintf(stderr, "cannot force driver: %s\n",
                   forced.error().message.c_str());
      return 2;
    }
    int result{};
    if (benchmark_frames) {
      app.benchmark(*benchmark_frames);
    } else {
      result = app.run();
    }

    if (!app.error().empty()) {
      std::fprintf(stderr, "last TermForge event: %s\n", app.error().c_str());
    }
    const BenchmarkSummary summary = app.summary();
    std::printf("display: %s\n", app.display_path().c_str());
    print_summary(summary, app.render_configuration());
    if (!snapshot_path.empty() && !app.write_snapshot(snapshot_path)) {
      std::fprintf(stderr, "cannot write snapshot '%s'\n",
                   snapshot_path.string().c_str());
      return 1;
    }
    if (!report_path.empty()) {
      std::ofstream report{report_path};
      if (!report) {
        std::fprintf(stderr, "cannot open report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
      report << summary_json(summary, app.render_configuration());
    }
    return result;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "apsis-drift: %s\n", error.what());
    return 1;
  }
}
