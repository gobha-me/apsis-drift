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
#include "apsis_drift/landscape.hpp"

namespace {

using Clock = std::chrono::steady_clock;
using namespace termforge;
using namespace apsis_drift;

struct FrameSample {
  std::uint64_t bytes{};
  double render_ms{};
  double work_ms{};
};

struct Summary {
  std::size_t frames{};
  double elapsed_seconds{};
  double achieved_fps{};
  double render_avg_ms{};
  double render_p95_ms{};
  double work_avg_ms{};
  double work_p95_ms{};
  double bytes_per_frame{};
  double mebibytes_per_second{};
  std::uint64_t total_bytes{};
  std::uint64_t checksum{};
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

  [[nodiscard]] auto summary(std::uint64_t checksum) const -> Summary {
    Summary result;
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

[[nodiscard]] auto aspect_fit(Rect available, Extent per_cell) -> Rect {
  if (available.empty()) return {};
  const int cell_w = std::max(1, per_cell.w);
  const int cell_h = std::max(1, per_cell.h);
  int width = available.w;
  int height = static_cast<int>(
      (static_cast<std::int64_t>(width) * kFrameHeight * cell_w) /
      (static_cast<std::int64_t>(kFrameWidth) * cell_h));
  if (height <= 0) height = 1;
  if (height > available.h) {
    height = available.h;
    width = static_cast<int>(
        (static_cast<std::int64_t>(height) * kFrameWidth * cell_h) /
        (static_cast<std::int64_t>(kFrameHeight) * cell_w));
    width = std::clamp(width, 1, available.w);
  }
  return {available.x + (available.w - width) / 2,
          available.y + (available.h - height) / 2, width, height};
}

class LandscapeApp final : public App {
 public:
  explicit LandscapeApp(std::uint32_t seed, double capture_seconds = 0.0)
      : m_terrain(required_terrain(1024, seed)),
        m_capture_seconds(capture_seconds),
        m_seed(seed) {
    set_frame_ms(33);
    set_tick_hz(120);
    set_max_tick_dt(std::chrono::duration<double>{0.125});
    set_mouse_mode(MouseMode::None);
    set_keyboard_mode(KeyboardMode::Enhanced);
    m_camera.height =
        std::max<float>(m_terrain.height_at(static_cast<int>(m_camera.x),
                                            static_cast<int>(m_camera.y)),
                        kWaterLevel) +
        m_camera.clearance;
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
    if (!m_headless) advance(dt.count());
  }

  auto on_render(Screen& screen) -> void override {
    if (!m_output_bound) {
      driver().set_output(&m_sink);
      m_output_bound = true;
    }
    if (m_headless) advance(1.0 / 30.0);

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
            " fractal landscape 640x480 | {} | seed {} | frame {} | {:.2f} "
            "ms | {:.1f} MiB ",
            m_display_tier, m_seed, m_frame, render_time,
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
    m_surface.set_geometry(aspect_fit(available, one_cell));
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
    m_headless = true;
    set_frame_ms(0);
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
    auto selected = std::make_unique<KittyDriver>();
    selected->set_cell_pixel_size({8, 16});
    try {
      test_run_frames(frames, 100, 40, nullptr, std::move(selected));
    } catch (...) {
      ::close(null_input);
      throw;
    }
    ::close(null_input);
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

  [[nodiscard]] auto summary() const -> Summary {
    return m_sink.summary(pixel_checksum(m_surface.pixels()));
  }

  [[nodiscard]] auto error() const -> const std::string& { return m_error; }
  [[nodiscard]] auto display_path() const -> const std::string& {
    return m_display_path;
  }

  [[nodiscard]] auto write_snapshot(const std::filesystem::path& path) const
      -> bool {
    std::ofstream output{path, std::ios::binary};
    if (!output) return false;
    output << "P6\n" << kFrameWidth << ' ' << kFrameHeight << "\n255\n";
    for (const auto pixel : m_surface.pixels()) {
      const char rgb[] = {static_cast<char>(pixel.r),
                          static_cast<char>(pixel.g),
                          static_cast<char>(pixel.b)};
      output.write(rgb, sizeof(rgb));
    }
    return output.good();
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
    advance(step);
    set_control(control, false);
  }

  auto clear_controls() noexcept -> void {
    m_forward = m_backward = m_left = m_right = false;
    m_strafe_left = m_strafe_right = m_rise = m_fall = false;
  }

  auto advance(double seconds) -> void {
    m_elapsed += seconds;
    float forward = static_cast<float>(m_forward) -
                    static_cast<float>(m_backward);
    float turn = static_cast<float>(m_right) - static_cast<float>(m_left);
    float strafe = static_cast<float>(m_strafe_right) -
                   static_cast<float>(m_strafe_left);
    float vertical = static_cast<float>(m_rise) - static_cast<float>(m_fall);
    if (m_autopilot) {
      forward = 0.72F;
      turn = 0.055F;
    }

    const float dt = static_cast<float>(seconds);
    m_camera.yaw += turn * 1.15F * dt;
    const float forward_x = std::cos(m_camera.yaw);
    const float forward_y = std::sin(m_camera.yaw);
    const float right_x = -forward_y;
    const float right_y = forward_x;
    constexpr float speed{52.0F};
    m_camera.x += (forward_x * forward + right_x * strafe) * speed * dt;
    m_camera.y += (forward_y * forward + right_y * strafe) * speed * dt;
    m_camera.clearance =
        std::clamp(m_camera.clearance + vertical * 45.0F * dt, 16.0F, 160.0F);

    const float world = static_cast<float>(m_terrain.size());
    m_camera.x = std::fmod(m_camera.x + world, world);
    m_camera.y = std::fmod(m_camera.y + world, world);
    const float floor = std::max<float>(
        m_terrain.height_at(static_cast<int>(m_camera.x),
                            static_cast<int>(m_camera.y)),
        kWaterLevel);
    const float target_height = floor + m_camera.clearance;
    m_camera.height +=
        (target_height - m_camera.height) * std::min(1.0F, dt * 3.0F);
    m_camera.horizon = 205.0F + std::sin(static_cast<float>(m_elapsed) * 0.17F) *
                                     5.0F;
  }

  auto render_landscape() -> void {
    if (!m_renderer.render(m_terrain, m_camera, m_surface.pixels())) {
      throw std::runtime_error{"renderer rejected the 640x480 surface"};
    }
  }

  Terrain m_terrain;
  VoxelRenderer m_renderer;
  PixelSurface m_surface{{kFrameWidth, kFrameHeight}, {0, 0, 0, 255}};
  Camera m_camera;
  MeasuringSink m_sink;
  Clock::time_point m_run_started{};
  double m_capture_seconds{};
  double m_elapsed{};
  std::uint32_t m_seed{};
  int m_frame{};
  bool m_headless{false};
  bool m_output_bound{false};
  bool m_autopilot{true};
  bool m_forward{}, m_backward{}, m_left{}, m_right{};
  bool m_strafe_left{}, m_strafe_right{}, m_rise{}, m_fall{};
  std::string m_error;
  std::string m_display_tier{"probing"};
  std::string m_display_path{"not started"};
};

auto print_summary(const Summary& summary) -> void {
  std::printf(
      "landscape: frames=%zu elapsed=%.3fs achieved=%.2f fps\n"
      "timing: renderer avg/p95 %.3f/%.3f ms, full frame work %.3f/%.3f ms\n"
      "wire: %.1f KiB/frame, %.2f MiB/s, total %.2f MiB\n"
      "checksum: %llu\n",
      summary.frames, summary.elapsed_seconds, summary.achieved_fps,
      summary.render_avg_ms, summary.render_p95_ms, summary.work_avg_ms,
      summary.work_p95_ms, summary.bytes_per_frame / 1024.0,
      summary.mebibytes_per_second,
      static_cast<double>(summary.total_bytes) / (1024.0 * 1024.0),
      static_cast<unsigned long long>(summary.checksum));
}

[[nodiscard]] auto summary_json(const Summary& summary) -> std::string {
  return std::format(
      "{{\n"
      "  \"workload\": \"voxel-landscape-640x480-rgba\",\n"
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
      "Usage: apsis-drift [--seed N]\n"
      "       apsis-drift [--driver automatic|kitty|ansi|fallback]\n"
      "       apsis-drift --benchmark [FRAMES] [--seed N] [--report PATH]\n"
      "       apsis-drift --capture-seconds N [--seed N] --report PATH\n\n"
      "Add --snapshot PATH to save the final framebuffer as a binary PPM.\n"
      "Interactive controls: arrows/WASD move, Q/E strafe, R/F altitude,\n"
      "Space toggles autopilot, and Escape quits.");
}

}  // namespace

auto main(int argc, char** argv) -> int {
  std::optional<int> benchmark_frames;
  int capture_seconds = 0;
  std::uint32_t seed = 0xC0FFEEU;
  DriverChoice driver_choice{DriverChoice::automatic};
  std::filesystem::path report_path;
  std::filesystem::path snapshot_path;

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
  if (capture_seconds > 0 && report_path.empty()) {
    std::fprintf(stderr, "capture mode requires --report PATH\n");
    return 2;
  }

  try {
    LandscapeApp app{seed, static_cast<double>(capture_seconds)};
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
    const Summary summary = app.summary();
    std::printf("display: %s\n", app.display_path().c_str());
    print_summary(summary);
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
      report << summary_json(summary);
    }
    return result;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "apsis-drift: %s\n", error.what());
    return 1;
  }
}
