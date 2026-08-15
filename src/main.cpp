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
#include "termforge/drivers/kitty_driver.hpp"
#include "termforge/widgets/frame.hpp"
#include "termforge/widgets/pixel_surface.hpp"
#include "apsis_drift/benchmark.hpp"
#include "apsis_drift/cockpit.hpp"
#include "apsis_drift/landscape.hpp"
#include "apsis_drift/menu.hpp"
#include "apsis_drift/render_profile.hpp"
#include "apsis_drift/simulation.hpp"
#include "apsis_drift/title.hpp"
#include "capability_floor.hpp"
#include "flight_input.hpp"

namespace {

using Clock = std::chrono::steady_clock;
using namespace termforge;
using namespace apsis_drift;

struct FrameSample {
  std::uint64_t bytes{};
  double render_ms{};
  double work_ms{};
};

using apsis_drift::detail::DriverChoice;
using apsis_drift::detail::KeyboardChoice;

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
        static_cast<std::size_t>(
            std::ceil(static_cast<double>(renders.size()) * 0.95)) -
            1);
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

[[nodiscard]] auto required_initial_flight(const Terrain& terrain)
    -> FlightState {
  auto state = initial_flight_state(terrain);
  if (!state) throw std::runtime_error{"invalid initial flight state"};
  return *state;
}

[[nodiscard]] auto render_settings_for(ViewportSize viewport)
    -> RenderSettings {
  RenderSettings settings;
  settings.width = viewport.width;
  settings.height = viewport.height;
  return settings;
}

class LandscapeApp final : public App {
 public:
  explicit LandscapeApp(RenderConfiguration render_configuration,
                        std::uint32_t seed, double capture_seconds = 0.0,
                        bool interactive_controls = false)
      : m_render_configuration(render_configuration),
        m_terrain(required_terrain(1024, seed)),
        m_renderer(render_settings_for(render_configuration.viewport)),
        m_surface({render_configuration.viewport.width,
                   render_configuration.viewport.height},
                  {0, 0, 0, 255}),
        m_flight(required_initial_flight(m_terrain)),
        m_session(!interactive_controls),
        m_capture_seconds(capture_seconds),
        m_seed(seed),
        m_interactive_controls(interactive_controls) {
    set_frame_ms(33);
    // TermForge supplies elapsed host time. Apsis Drift owns the fixed-step
    // accumulator and its bounded catch-up policy.
    set_tick_hz(0);
    set_max_tick_dt(std::chrono::duration<double>::zero());
    set_mouse_mode(interactive_controls ? MouseMode::Click : MouseMode::None);
    set_keyboard_mode(KeyboardMode::Enhanced);
    require(apsis_drift::detail::flight_deck_requirements());
    render_landscape();
  }

  auto on_start() -> void override {
    m_sink.set_fd(terminal().io().out);
    driver().set_output(&m_sink);
    m_output_bound = true;
    const auto& caps = capabilities();
    const std::string_view tier = caps.kitty_graphics ? "kitty" : "ansi";
    const auto input = input_capabilities();
    m_display_tier = tier;
    m_display_path = std::format(
        "{} (kitty_graphics={}, truecolor={}, input_press={}, "
        "input_repeat={}, input_release={}, sync={})",
        tier, caps.kitty_graphics, caps.truecolor, input.key_press,
        input.key_repeat, input.key_release, caps.sync_updates);
    m_input_tier = m_interactive_controls ? "KEY + MOUSE" : "HELD INPUT";
    if (m_capture_seconds > 0.0 && !capabilities().kitty_graphics) {
      m_error = "capture mode requires negotiated Kitty graphics";
      quit();
    }
  }

  auto on_event(const Event& event) -> void override {
    if (const auto* error = std::get_if<ErrorEvent>(&event)) {
      if (error->severity != Severity::Info) m_error = error->message;
      if (error->severity != Severity::Info) {
        m_input_mapper.neutralize_mouse(m_flight.tick);
      }
      // TermForge dispatches any synthesized held-key releases before this
      // requirements transition. Stop instead of simulating with an input
      // route that can no longer guarantee release events.
      if (error->source == "requirements" && !requirements_met()) {
        m_requirements_failed = true;
        quit();
      }
    } else if (const auto* key = std::get_if<KeyEvent>(&event)) {
      if (key->key == Key::Escape && m_interactive_controls) {
        if (key->action == KeyAction::Press) {
          apply_session_command(MenuCommand::escape);
        }
        return;
      }
      if (m_session.menu_visible()) {
        handle_menu_key(*key);
      } else if (m_session.screen() == SessionScreen::flight) {
        handle_key(*key);
      }
    } else if (const auto* mouse = std::get_if<MouseEvent>(&event)) {
      if (m_session.menu_visible()) {
        handle_menu_mouse(*mouse);
      } else if (m_interactive_controls &&
                 m_session.screen() == SessionScreen::flight) {
        m_input_mapper.enqueue(*mouse, m_active_mouse_region, m_flight.tick);
      }
    } else if (const auto* resize = std::get_if<ResizeEvent>(&event)) {
      m_input_mapper.neutralize_mouse(m_flight.tick);
      m_active_mouse_region = {};
      m_menu_layout = compute_menu_layout(resize->cols, resize->rows);
    }
    App::on_event(event);
  }

  auto on_tick(std::chrono::duration<double> dt) -> void override {
    if (m_session.screen() == SessionScreen::flight) {
      advance_simulation(dt);
    }
  }

  auto on_render(Screen& screen) -> void override {
    if (!m_output_bound) {
      driver().set_output(&m_sink);
      m_output_bound = true;
    }
    const auto frame_started = Clock::now();
    const auto render_started = Clock::now();
    if (m_session.screen() == SessionScreen::flight) {
      render_landscape();
    } else if (m_session.screen() == SessionScreen::title &&
               !m_title_rendered) {
      m_title_available =
          render_title(m_render_configuration.viewport, m_surface.pixels())
              .has_value();
      m_title_rendered = true;
    }
    const double render_time = elapsed_ms(render_started, Clock::now());
    m_sink.begin_frame(frame_started, render_time);

    Cell background;
    background.bg = {7, 15, 24};
    screen.clear(background);
    const Extent one_cell = driver().preferred_pixel_extent({0, 0, 1, 1});
    const auto layout = compute_cockpit_layout(
        screen.cols(), screen.rows(), one_cell,
        m_render_configuration.viewport);
    if (m_session.screen() == SessionScreen::title) {
      draw_title_screen(screen);
    } else if (m_session.screen() == SessionScreen::paused) {
      draw_cockpit(screen, layout, render_time, false);
      draw_menu(screen, "FLIGHT PAUSED", "RESUME FLIGHT");
    } else if (m_session.screen() == SessionScreen::flight) {
      draw_cockpit(screen, layout, render_time, true);
    }
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

  [[nodiscard]] auto force_capabilities(DriverChoice driver_choice,
                                        KeyboardChoice keyboard_choice)
      -> std::expected<void, ErrorEvent> {
    const auto caps = apsis_drift::detail::forced_capabilities(
        driver_choice, keyboard_choice);
    if (!caps) return {};
    return terminal().set_capabilities(*caps);
  }

  [[nodiscard]] auto summary() const -> BenchmarkSummary {
    return m_sink.summary(pixel_checksum(m_surface.pixels()));
  }

  [[nodiscard]] auto error() const -> const std::string& { return m_error; }
  [[nodiscard]] auto requirements_failed() const noexcept -> bool {
    return m_requirements_failed;
  }
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
  [[nodiscard]] auto menu_command_for(const KeyEvent& key) const noexcept
      -> std::optional<MenuCommand> {
    if (key.action == KeyAction::Release) return std::nullopt;
    if (key.key == Key::Up) return MenuCommand::previous;
    if (key.key == Key::Down) return MenuCommand::next;
    if (key.key == Key::Tab) {
      return key.shift ? MenuCommand::previous : MenuCommand::next;
    }
    if (key.action != KeyAction::Press) return std::nullopt;
    if (key.key == Key::Enter ||
        (key.key == Key::Char && key.ch == U' ')) {
      return MenuCommand::activate;
    }
    return std::nullopt;
  }

  auto apply_session_command(MenuCommand command) -> void {
    const auto transition = m_session.dispatch(command);
    if (!transition.changed()) return;

    if (transition.from == SessionScreen::flight &&
        transition.to == SessionScreen::paused) {
      m_input_mapper.suspend(m_flight.controls, m_flight.tick);
      m_simulation_clock.reset();
      m_active_mouse_region = {};
    }
    if (transition.to == SessionScreen::flight) {
      m_simulation_clock.reset();
      m_active_mouse_region = {};
    }
    if (transition.to == SessionScreen::exit_requested) quit();
  }

  auto handle_menu_key(const KeyEvent& key) -> void {
    if (const auto command = menu_command_for(key)) {
      apply_session_command(*command);
    }
  }

  auto handle_menu_mouse(const MouseEvent& mouse) -> void {
    if (!mouse.pressed || mouse.button != 0 || mouse.scroll_up ||
        mouse.scroll_down) {
      return;
    }
    const auto selected = menu_item_at(m_menu_layout, mouse.x, mouse.y);
    if (!selected) return;
    m_session.select(*selected);
    apply_session_command(MenuCommand::activate);
  }

  auto draw_size_requirement(Screen& screen) -> void {
    m_active_mouse_region = {};
    m_menu_layout = {};
    m_surface.set_geometry({});
    m_surface.draw(screen);
    render_pixel_regions(m_surface);

    const std::string title{"APSIS DRIFT // FLIGHT DECK"};
    const std::string dimensions = std::format(
        "terminal too small: need at least {}x{}, current {}x{}",
        kMinimumCockpitCols, kMinimumCockpitRows, screen.cols(),
        screen.rows());
    const int center_y = std::max(0, screen.rows() / 2 - 1);
    screen.write_text(
        std::max(0,
                 (screen.cols() - static_cast<int>(title.size())) / 2),
        center_y, title, {126, 214, 210}, {7, 15, 24});
    screen.write_text(
        std::max(0,
                 (screen.cols() - static_cast<int>(dimensions.size())) / 2),
        center_y + 2, dimensions, {238, 184, 104}, {7, 15, 24});
  }

  auto draw_menu(Screen& screen, std::string_view heading,
                 std::string_view primary_label) -> void {
    if (screen.cols() < kMinimumCockpitCols ||
        screen.rows() < kMinimumCockpitRows) {
      draw_size_requirement(screen);
      return;
    }
    m_menu_layout = compute_menu_layout(screen.cols(), screen.rows());
    if (!m_menu_layout.supported()) {
      draw_size_requirement(screen);
      return;
    }

    constexpr Rgb text{205, 222, 224};
    constexpr Rgb muted{109, 143, 151};
    constexpr Rgb accent{126, 214, 210};
    constexpr Rgb panel_bg{11, 28, 40};
    constexpr Rgb selected_bg{20, 61, 70};

    m_menu_frame.set_style(BorderStyle::Rounded);
    m_menu_frame.set_geometry(m_menu_layout.panel);
    screen.fill_rect(m_menu_layout.panel.x + 1, m_menu_layout.panel.y + 1,
                     m_menu_layout.panel.w - 2,
                     m_menu_layout.panel.h - 2, text, panel_bg);

    const auto centered = [&](Rect row, std::string_view value, Rgb fg,
                              Rgb bg) {
      const int x = row.x + std::max(
                                0, (row.w - static_cast<int>(value.size())) /
                                       2);
      screen.write_text(x, row.y, value, fg, bg);
    };
    centered(m_menu_layout.heading, heading, accent, panel_bg);

    const auto action = [&](Rect row, MenuItem item, std::string_view label) {
      const bool selected = m_session.selected() == item;
      const Rgb background = selected ? selected_bg : panel_bg;
      screen.fill_rect(row.x, row.y, row.w, row.h, text, background);
      const std::string display =
          selected ? std::format("> {} <", label) : std::string(label);
      centered(row, display, text, background);
    };
    action(m_menu_layout.primary_action, MenuItem::primary, primary_label);
    action(m_menu_layout.exit_action, MenuItem::exit, "EXIT TO TERMINAL");
    centered(m_menu_layout.hint, "ARROWS/TAB  ENTER  MOUSE", muted,
             panel_bg);
    m_menu_frame.draw(screen);
  }

  auto draw_title_screen(Screen& screen) -> void {
    if (screen.cols() < kMinimumCockpitCols ||
        screen.rows() < kMinimumCockpitRows) {
      draw_size_requirement(screen);
      return;
    }
    m_menu_layout = compute_menu_layout(screen.cols(), screen.rows());
    if (!m_menu_layout.supported()) {
      draw_size_requirement(screen);
      return;
    }

    if (m_title_available && !m_menu_layout.art.empty()) {
      m_surface.set_geometry(m_menu_layout.art);
      m_surface.draw(screen);
      render_pixel_regions(m_surface);
    } else {
      m_surface.set_geometry({});
      m_surface.draw(screen);
      render_pixel_regions(m_surface);
      constexpr std::string_view fallback{"APSIS DRIFT"};
      screen.write_text(
          std::max(0, (screen.cols() - static_cast<int>(fallback.size())) /
                          2),
          std::max(1, m_menu_layout.art.y + m_menu_layout.art.h / 2),
          fallback, {126, 214, 210}, {7, 15, 24});
    }
    draw_menu(screen, "FLIGHT DECK // v0.2", "START FLIGHT");
  }

  auto draw_cockpit(Screen& screen, const CockpitLayout& layout,
                    double render_time, bool enhanced_pixels) -> void {
    if (!layout.supported()) {
      if (!m_active_mouse_region.empty()) {
        m_input_mapper.neutralize_mouse(m_flight.tick);
      }
      draw_size_requirement(screen);
      return;
    }
    m_active_mouse_region = enhanced_pixels ? layout.viewport : Rect{};

    m_left_frame.set_style(BorderStyle::Rounded);
    m_viewport_frame.set_style(BorderStyle::Rounded);
    m_right_frame.set_style(BorderStyle::Rounded);
    m_message_frame.set_style(BorderStyle::Rounded);
    m_left_frame.set_geometry(layout.left_instruments);
    m_viewport_frame.set_geometry(layout.viewport_frame);
    m_right_frame.set_geometry(layout.right_instruments);
    m_message_frame.set_geometry(layout.messages);

    constexpr Rgb text{205, 222, 224};
    constexpr Rgb muted{109, 143, 151};
    constexpr Rgb accent{126, 214, 210};
    constexpr Rgb warning{238, 184, 104};
    constexpr Rgb danger{238, 104, 104};
    constexpr Rgb chrome_bg{11, 28, 40};
    constexpr Rgb status_bg{20, 43, 66};
    const auto instruments = format_flight_instruments(m_flight);

    screen.fill_rect(layout.header.x, layout.header.y, layout.header.w,
                     layout.header.h, text, status_bg);
    screen.write_text(
        layout.header.x, layout.header.y,
        std::format(" APSIS DRIFT // FLIGHT DECK | {} {}x{} | {} | seed {} ",
                    profile_name(m_render_configuration),
                    m_render_configuration.viewport.width,
                    m_render_configuration.viewport.height, m_display_tier,
                    m_seed),
        text, status_bg);

    const auto fill_panel = [&](Rect panel) {
      screen.fill_rect(panel.x + 1, panel.y + 1,
                       std::max(0, panel.w - 2),
                       std::max(0, panel.h - 2), muted, chrome_bg);
    };
    fill_panel(layout.left_instruments);
    fill_panel(layout.right_instruments);
    fill_panel(layout.messages);

    screen.write_text(layout.left_instruments.x + 2,
                      layout.left_instruments.y + 2, "CONTROL", accent,
                      chrome_bg);
    screen.write_text(layout.left_instruments.x + 2,
                      layout.left_instruments.y + 4, instruments.mode, text,
                      chrome_bg);
    const Rgb alert_color =
        instruments.alert_state == CockpitAlert::invalid_telemetry
            ? danger
            : (instruments.alert_state == CockpitAlert::low_clearance
                   ? warning
                   : muted);
    screen.write_text(layout.left_instruments.x + 2,
                      layout.left_instruments.y + 6, instruments.alert,
                      alert_color, chrome_bg);
    screen.write_text(layout.right_instruments.x + 2,
                      layout.right_instruments.y + 2, "NAV DATA", accent,
                      chrome_bg);
    screen.write_text(layout.right_instruments.x + 2,
                      layout.right_instruments.y + 4, instruments.heading,
                      text, chrome_bg);
    screen.write_text(layout.right_instruments.x + 2,
                      layout.right_instruments.y + 6, instruments.altitude,
                      text, chrome_bg);
    screen.write_text(layout.right_instruments.x + 2,
                      layout.right_instruments.y + 8, instruments.clearance,
                      alert_color, chrome_bg);
    screen.write_text(layout.right_instruments.x + 2,
                      layout.right_instruments.y + 10, instruments.speed,
                      text, chrome_bg);

    std::string message;
    if (!m_error.empty()) {
      message = " ERROR: " + m_error + " | ESC menu ";
    } else if (instruments.alert_state == CockpitAlert::invalid_telemetry) {
      message = " WARNING: TELEM ERR | flight instruments unavailable | "
                "ESC menu ";
    } else if (instruments.alert_state == CockpitAlert::low_clearance) {
      message = " WARNING: LOW CLEARANCE | R to climb | ESC menu ";
    } else {
      message = layout.mode == CockpitLayoutMode::wide
                    ? " L-hold fly | R-hold strafe/alt | M-click auto | "
                      "keys WASD Q/E R/F Space | ESC menu "
                    : " L-hold fly | R-hold strafe/alt | M auto | "
                      "keys WASD/QE/RF/Space | ESC menu ";
    }
    screen.write_text(layout.messages.x + 2, layout.messages.y + 1, message,
                      text, chrome_bg);

    m_left_frame.draw(screen);
    m_viewport_frame.draw(screen);
    m_right_frame.draw(screen);
    m_message_frame.draw(screen);

    const auto totals = driver().total_bytes();
    screen.fill_rect(layout.status.x, layout.status.y, layout.status.w,
                     layout.status.h, text, status_bg);
    screen.write_text(
        layout.status.x, layout.status.y,
        std::format(" {} | {} | frame {} | {:.2f} ms | {:.1f} MiB ",
                    m_flight.mode == FlightMode::autopilot ? "AUTOPILOT"
                                                           : "MANUAL",
                    m_input_tier, m_frame, render_time,
                    static_cast<double>(totals.total()) / (1024.0 * 1024.0)),
        text, status_bg);

    m_surface.set_geometry(layout.viewport);
    m_surface.draw(screen);
    if (enhanced_pixels) render_pixel_regions(m_surface);
  }

  auto handle_key(const KeyEvent& key) -> void {
    m_input_mapper.enqueue(key, m_flight.tick);
  }

  auto advance_simulation(std::chrono::duration<double> elapsed) -> void {
    const auto advance = m_simulation_clock.advance(elapsed);
    if (!advance) {
      throw std::runtime_error{"simulation clock rejected elapsed time"};
    }
    for (int step = 0; step < advance->steps; ++step) {
      const auto commands = m_input_mapper.take_commands(m_flight.tick);
      if (!advance_flight(m_terrain, m_flight, commands, kSimulationStep)) {
        throw std::runtime_error{"simulation rejected flight state"};
      }
    }
  }

  auto render_landscape() -> void {
    auto derived = derive_camera(m_flight);
    if (!derived) {
      throw std::runtime_error{"cannot derive camera from flight state"};
    }
    Camera camera = *derived;
    const double elapsed_seconds =
        static_cast<double>(m_flight.tick) * kSimulationStep.count();
    constexpr float degrees_to_radians{3.14159265358979323846F / 180.0F};
    camera.pitch = std::sin(static_cast<float>(elapsed_seconds) * 0.17F) *
                   degrees_to_radians;
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
  Frame m_left_frame{"FLIGHT"};
  Frame m_viewport_frame{"EXTERIOR"};
  Frame m_right_frame{"NAV"};
  Frame m_message_frame{"COMMS"};
  Frame m_menu_frame{"SYSTEM"};
  FlightState m_flight;
  SessionController m_session;
  FixedStepClock m_simulation_clock;
  apsis_drift::detail::FlightInputMapper m_input_mapper;
  SyntheticClock m_headless_clock;
  MeasuringSink m_sink;
  Clock::time_point m_run_started{};
  double m_capture_seconds{};
  std::uint32_t m_seed{};
  int m_frame{};
  bool m_output_bound{false};
  bool m_synthetic_headless{false};
  bool m_requirements_failed{false};
  std::string m_error;
  std::string m_display_tier{"probing"};
  std::string m_display_path{"not started"};
  std::string m_input_tier{"INPUT PROBING"};
  Rect m_active_mouse_region{};
  MenuLayout m_menu_layout{};
  bool m_interactive_controls{};
  bool m_title_rendered{};
  bool m_title_available{};
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
      "                   [--keyboard enhanced|press-only]\n"
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
      "Space toggles autopilot; left-hold flies, right-hold strafes/climbs,\n"
      "middle-click toggles autopilot, and Escape opens the pause menu.");
}

}  // namespace

auto main(int argc, char** argv) -> int {
  std::optional<int> benchmark_frames;
  std::optional<int> sweep_frames;
  int capture_seconds = 0;
  std::uint32_t seed = 0xC0FFEEU;
  DriverChoice driver_choice{DriverChoice::automatic};
  KeyboardChoice keyboard_choice{KeyboardChoice::enhanced};
  RenderProfile selected_profile{RenderProfile::local};
  std::optional<ViewportSize> viewport_override;
  auto sweep_viewports = default_sweep_viewports();
  auto sweep_fps = default_sweep_fps();
  std::filesystem::path report_path;
  std::filesystem::path snapshot_path;
  bool profile_specified{};
  bool viewport_specified{};
  bool driver_specified{};
  bool keyboard_specified{};
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
    if (argument == "--keyboard" && i + 1 < argc) {
      const std::string_view value{argv[++i]};
      if (value == "enhanced") keyboard_choice = KeyboardChoice::enhanced;
      else if (value == "press-only") {
        keyboard_choice = KeyboardChoice::press_only;
      } else {
        std::fprintf(stderr, "unknown keyboard tier '%.*s'\n",
                     static_cast<int>(value.size()), value.data());
        return 2;
      }
      keyboard_specified = true;
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
  if (keyboard_specified && driver_choice == DriverChoice::automatic) {
    std::fprintf(stderr,
                 "--keyboard requires --driver kitty, ansi, or fallback\n");
    return 2;
  }
  if (sweep_frames && (profile_specified || viewport_specified ||
                       driver_specified || keyboard_specified ||
                       !snapshot_path.empty())) {
    std::fprintf(stderr,
                 "sweep mode does not accept profile, viewport, driver, "
                 "keyboard, or snapshot options\n");
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
    const bool interactive_controls =
        !benchmark_frames && capture_seconds == 0;
    LandscapeApp app{render_configuration, seed,
                     static_cast<double>(capture_seconds),
                     interactive_controls};
    if (auto forced =
            app.force_capabilities(driver_choice, keyboard_choice);
        !forced) {
      std::fprintf(stderr, "cannot force capabilities: %s\n",
                   forced.error().message.c_str());
      return 2;
    }
    int result{};
    if (benchmark_frames) {
      app.benchmark(*benchmark_frames);
    } else {
      result = app.run();
      if (app.requirements_failed()) result = 1;
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
