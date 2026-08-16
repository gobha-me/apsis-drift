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
#include <limits>
#include <numbers>
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
#include "apsis_drift/flight_deck_acceptance.hpp"
#include "apsis_drift/landscape.hpp"
#include "apsis_drift/menu.hpp"
#include "apsis_drift/orbital.hpp"
#include "apsis_drift/planet.hpp"
#include "apsis_drift/planetfall_acceptance.hpp"
#include "apsis_drift/planetary_presentation.hpp"
#include "apsis_drift/render_profile.hpp"
#include "apsis_drift/save_file.hpp"
#include "apsis_drift/signal_navigation_acceptance.hpp"
#include "apsis_drift/signal_scanner.hpp"
#include "apsis_drift/simulation.hpp"
#include "apsis_drift/title.hpp"
#include "capability_floor.hpp"
#include "flight_input.hpp"
#include "signal_input.hpp"

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

[[nodiscard]] auto write_snapshot(
    const std::filesystem::path& path, ViewportSize viewport,
    std::span<const termforge::Pixel> pixels) -> bool {
  const auto expected = static_cast<std::size_t>(viewport.width) *
                        static_cast<std::size_t>(viewport.height);
  if (!validate_viewport(viewport) || pixels.size() != expected) return false;
  std::ofstream output{path, std::ios::binary};
  if (!output) return false;
  output << "P6\n" << viewport.width << ' ' << viewport.height << "\n255\n";
  for (const auto pixel : pixels) {
    const char rgb[] = {static_cast<char>(pixel.r),
                        static_cast<char>(pixel.g),
                        static_cast<char>(pixel.b)};
    output.write(rgb, sizeof(rgb));
  }
  return output.good();
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

[[nodiscard]] auto orbital_settings_for(ViewportSize viewport)
    -> OrbitalRenderSettings {
  OrbitalRenderSettings settings;
  settings.width = viewport.width;
  settings.height = viewport.height;
  settings.field_of_view_degrees = 60.0;
  settings.light_direction = {0.55, 0.15, 0.82};
  return settings;
}

[[nodiscard]] auto planetary_settings_for(ViewportSize viewport)
    -> PlanetaryPresentationSettings {
  PlanetaryPresentationSettings settings;
  settings.width = viewport.width;
  settings.height = viewport.height;
  return settings;
}

struct PlanetarySurfaceFixture {
  double latitude_radians{};
  double longitude_radians{};
  double elevation_metres{};
};

[[nodiscard]] auto required_planetary_surface(
    const PlanetDescriptor& planet) -> PlanetarySurfaceFixture {
  auto cache = TerrainTileCache::create();
  if (!cache) throw std::runtime_error{"cannot create terrain tile cache"};
  PlanetarySurfaceFixture best{0.0, 0.0,
                               -std::numeric_limits<double>::infinity()};
  constexpr int latitude_samples{9};
  constexpr int longitude_samples{16};
  for (int latitude_index = 0; latitude_index < latitude_samples;
       ++latitude_index) {
    const double latitude = -1.0 +
                            2.0 * latitude_index /
                                static_cast<double>(latitude_samples - 1);
    for (int longitude_index = 0; longitude_index < longitude_samples;
         ++longitude_index) {
      const double longitude =
          -std::numbers::pi +
          2.0 * std::numbers::pi * longitude_index /
              static_cast<double>(longitude_samples);
      const auto fixed = planet_fixed_from_geodetic(
          planet, {latitude, longitude, 0.0});
      if (!fixed) {
        throw std::runtime_error{"cannot resolve planetary surface"};
      }
      const auto sample = sample_planet_surface(planet, *fixed, 8, *cache);
      if (!sample) {
        throw std::runtime_error{"cannot sample planetary surface"};
      }
      constexpr PlanetFixedDirection light{0.55, 0.15, 0.82};
      const double position_length =
          std::hypot(fixed->x, fixed->y, fixed->z);
      const double light_length = std::hypot(light.x, light.y, light.z);
      const double illumination =
          (fixed->x * light.x + fixed->y * light.y + fixed->z * light.z) /
          (position_length * light_length);
      if (illumination < 0.35) continue;
      if (sample->elevation_metres > best.elevation_metres) {
        best = {latitude, longitude, sample->elevation_metres};
      }
    }
  }
  if (!std::isfinite(best.elevation_metres)) {
    throw std::runtime_error{"cannot find an illuminated planetary surface"};
  }
  best.elevation_metres = std::max(0.0, best.elevation_metres);
  return best;
}

class LandscapeApp final : public App {
 public:
  explicit LandscapeApp(RenderConfiguration render_configuration,
                        std::uint32_t seed,
                        BenchmarkWorkload workload =
                            BenchmarkWorkload::landscape,
                        double capture_seconds = 0.0,
                        bool interactive_controls = false,
                        bool flight_deck_acceptance = false,
                        bool signal_navigation_acceptance = false)
      : m_render_configuration(render_configuration),
        m_terrain(required_terrain(1024, seed)),
        m_planet(generate_planet_descriptor(Seed{seed})),
        m_renderer(render_settings_for(render_configuration.viewport)),
        m_orbital_renderer(
            orbital_settings_for(render_configuration.viewport)),
        m_planetary_renderer(
            (workload == BenchmarkWorkload::planetary ||
             signal_navigation_acceptance)
                ? std::optional<PlanetaryPresentationRenderer>{
                      std::in_place,
                      planetary_settings_for(render_configuration.viewport)}
                : std::nullopt),
        m_surface({render_configuration.viewport.width,
                   render_configuration.viewport.height},
                  {0, 0, 0, 255}),
        m_flight(required_initial_flight(m_terrain)),
        m_planetary_surface(
            workload == BenchmarkWorkload::planetary
                ? required_planetary_surface(m_planet)
                : PlanetarySurfaceFixture{}),
        m_session(!interactive_controls),
        m_capture_seconds(capture_seconds),
        m_seed(seed),
        m_workload(workload),
        m_interactive_controls(interactive_controls),
        m_flight_deck_acceptance(flight_deck_acceptance),
        m_signal_navigation_acceptance(signal_navigation_acceptance) {
    if (m_signal_navigation_acceptance) {
      auto cache = TerrainTileCache::create();
      if (!cache) {
        throw std::runtime_error{"cannot create signal navigation terrain cache"};
      }
      m_signal_cache.emplace(std::move(*cache));
      auto scenario =
          initial_signal_navigation_acceptance(m_planet, *m_signal_cache);
      if (!scenario) {
        throw std::runtime_error{"cannot initialize signal navigation acceptance"};
      }
      m_signal_scenario.emplace(std::move(*scenario));
    }
    set_frame_ms(33);
    // TermForge supplies elapsed host time. Apsis Drift owns the fixed-step
    // accumulator and its bounded catch-up policy.
    set_tick_hz(0);
    set_max_tick_dt(std::chrono::duration<double>::zero());
    set_mouse_mode(interactive_controls ? MouseMode::Click : MouseMode::None);
    set_keyboard_mode(KeyboardMode::Enhanced);
    require(apsis_drift::detail::flight_deck_requirements());
    render_viewport();
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
      render_viewport();
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

    if (m_flight_deck_acceptance &&
        m_flight.tick == kFlightDeckAcceptanceTicks) {
      // Keep the canonical final state visible long enough for a human to
      // inspect or capture the supported terminal presentation. Simulation is
      // already stopped, so this presentation dwell cannot affect its checksum.
      constexpr int acceptance_final_frames{300};
      ++m_acceptance_final_frames;
      if (m_acceptance_final_frames >= acceptance_final_frames) {
        m_acceptance_final_frame_rendered = true;
        quit();
      }
    }
    if (m_signal_navigation_acceptance && m_signal_scenario &&
        m_signal_scenario->collection.status ==
            SignalCollectionStatus::complete) {
      // Preserve the collected cockpit for a short visual capture dwell without
      // advancing authoritative simulation state.
      constexpr int signal_acceptance_final_frames{90};
      ++m_acceptance_final_frames;
      if (m_acceptance_final_frames >= signal_acceptance_final_frames) {
        m_acceptance_final_frame_rendered = true;
        quit();
      }
    }

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
    m_planetary_samples.clear();
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
    auto result = m_sink.summary(pixel_checksum(m_surface.pixels()));
    if (m_planetary_samples.empty()) return result;
    PlanetaryPresentationBenchmarkSummary presentation;
    std::vector<double> totals;
    totals.reserve(m_planetary_samples.size());
    for (const auto& sample : m_planetary_samples) {
      switch (sample.mode) {
        case PlanetaryPresentationMode::orbital:
          ++presentation.orbital_frames;
          break;
        case PlanetaryPresentationMode::atmospheric:
          ++presentation.atmospheric_frames;
          break;
        case PlanetaryPresentationMode::terrain_blend:
          ++presentation.terrain_blend_frames;
          break;
        case PlanetaryPresentationMode::local_terrain:
          ++presentation.local_terrain_frames;
          break;
      }
      presentation.orbital_render_avg_ms += sample.orbital_render_ms;
      presentation.local_render_avg_ms += sample.local_render_ms;
      presentation.composite_avg_ms += sample.composite_ms;
      presentation.total_avg_ms += sample.total_ms;
      presentation.maximum_tiles_touched = std::max(
          presentation.maximum_tiles_touched,
          sample.orbital_tiles_touched + sample.local_tiles_touched);
      totals.push_back(sample.total_ms);
    }
    const double count = static_cast<double>(m_planetary_samples.size());
    presentation.orbital_render_avg_ms /= count;
    presentation.local_render_avg_ms /= count;
    presentation.composite_avg_ms /= count;
    presentation.total_avg_ms /= count;
    std::ranges::sort(totals);
    const std::size_t p95 = std::min(
        totals.size() - 1,
        static_cast<std::size_t>(
            std::ceil(static_cast<double>(totals.size()) * 0.95)) -
            1);
    presentation.total_p95_ms = totals[p95];
    result.planetary_presentation = presentation;
    return result;
  }

  [[nodiscard]] auto error() const -> const std::string& { return m_error; }
  [[nodiscard]] auto requirements_failed() const noexcept -> bool {
    return m_requirements_failed;
  }
  [[nodiscard]] auto display_path() const -> const std::string& {
    return m_display_path;
  }
  [[nodiscard]] auto display_tier() const -> const std::string& {
    return m_display_tier;
  }
  [[nodiscard]] auto flight_checksum() const noexcept -> std::uint64_t {
    if (m_signal_scenario) {
      return planetary_flight_state_checksum(m_signal_scenario->flight);
    }
    return flight_state_checksum(m_flight);
  }
  [[nodiscard]] auto acceptance_complete() const noexcept -> bool {
    if (m_signal_navigation_acceptance) {
      return m_acceptance_final_frame_rendered && m_signal_scenario &&
             m_signal_scenario->collection.status ==
                 SignalCollectionStatus::complete;
    }
    return m_acceptance_final_frame_rendered &&
           m_flight.tick == kFlightDeckAcceptanceTicks;
  }
  [[nodiscard]] auto signal_acceptance_state() const noexcept
      -> const SignalNavigationAcceptanceState* {
    return m_signal_scenario ? &*m_signal_scenario : nullptr;
  }
  [[nodiscard]] auto render_configuration() const noexcept
      -> const RenderConfiguration& {
    return m_render_configuration;
  }
  [[nodiscard]] auto workload() const noexcept -> BenchmarkWorkload {
    return m_workload;
  }

  [[nodiscard]] auto write_snapshot(const std::filesystem::path& path) const
      -> bool {
    return ::write_snapshot(path, m_render_configuration.viewport,
                            m_surface.pixels());
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
    const auto instruments =
        m_signal_scenario
            ? format_flight_instruments(m_signal_scenario->flight)
            : m_planetary_flight
                  ? format_flight_instruments(*m_planetary_flight)
                  : format_flight_instruments(m_flight);

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
    if (m_signal_scenario || m_planetary_flight) {
      const auto regime = format_flight_regime(
          m_signal_scenario ? m_signal_scenario->flight
                            : *m_planetary_flight);
      screen.write_text(layout.left_instruments.x + 2,
                        layout.left_instruments.y + 8, regime.regime,
                        regime.valid ? text : danger, chrome_bg);
      screen.write_text(layout.left_instruments.x + 2,
                        layout.left_instruments.y + 10,
                        regime.transition, muted, chrome_bg);
    }
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
    if (m_signal_scenario) {
      const auto scanner =
          format_signal_scanner(m_signal_scenario->navigation);
      const auto collection =
          format_signal_collection(m_signal_scenario->collection);
      screen.write_text(layout.left_instruments.x + 2,
                        layout.left_instruments.y + 12, "SIGNAL", accent,
                        chrome_bg);
      screen.write_text(layout.left_instruments.x + 2,
                        layout.left_instruments.y + 14, scanner.distance,
                        text, chrome_bg);
      screen.write_text(layout.left_instruments.x + 2,
                        layout.left_instruments.y + 16, scanner.strength,
                        text, chrome_bg);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 12, scanner.target,
                        text, chrome_bg);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 14, scanner.bearing,
                        text, chrome_bg);
      screen.write_text(
          layout.right_instruments.x + 2,
          layout.right_instruments.y + 16,
          m_signal_scenario->collection.status ==
                  SignalCollectionStatus::approach
              ? scanner.cue
              : collection.cue,
          m_signal_scenario->collection.status ==
                  SignalCollectionStatus::complete
              ? accent
              : (m_signal_scenario->collection.status ==
                         SignalCollectionStatus::aborted
                     ? warning
                     : text),
          chrome_bg);
    }

    std::string message;
    if (!m_error.empty()) {
      message = " ERROR: " + m_error + " | ESC menu ";
    } else if (instruments.alert_state == CockpitAlert::invalid_telemetry) {
      message = " WARNING: TELEM ERR | flight instruments unavailable | "
                "ESC menu ";
    } else if (instruments.alert_state == CockpitAlert::low_clearance) {
      message = " WARNING: LOW CLEARANCE | R to climb | ESC menu ";
    } else if (m_signal_scenario) {
      message =
          format_signal_collection(m_signal_scenario->collection).message;
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
                    (m_signal_scenario
                         ? m_signal_scenario->flight.mode
                         : (m_planetary_flight ? m_planetary_flight->mode
                                               : m_flight.mode)) ==
                            FlightMode::autopilot
                        ? "AUTOPILOT"
                        : "MANUAL",
                    m_input_tier, m_frame, render_time,
                    static_cast<double>(totals.total()) / (1024.0 * 1024.0)),
        text, status_bg);

    m_surface.set_geometry(layout.viewport);
    m_surface.draw(screen);
    if (enhanced_pixels) render_pixel_regions(m_surface);
  }

  auto handle_key(const KeyEvent& key) -> void {
    if (m_signal_scenario) {
      const auto command =
          apsis_drift::detail::signal_selection_command(key);
      if (!command) {
        m_input_mapper.enqueue(key, m_flight.tick);
        return;
      }
      if (!advance_signal_selection(m_signal_scenario->catalog,
                                    m_signal_scenario->scanner, *command)) {
        m_error = "signal selection rejected";
        return;
      }
      const auto navigation = resolve_signal_navigation(
          m_planet, m_signal_scenario->catalog, m_signal_scenario->flight,
          m_signal_scenario->scanner);
      if (!navigation) {
        m_error = "signal navigation rejected";
        return;
      }
      m_signal_scenario->navigation = *navigation;
      return;
    }
    m_input_mapper.enqueue(key, m_flight.tick);
  }

  auto advance_simulation(std::chrono::duration<double> elapsed) -> void {
    const auto advance = m_simulation_clock.advance(elapsed);
    if (!advance) {
      throw std::runtime_error{"simulation clock rejected elapsed time"};
    }
    for (int step = 0; step < advance->steps; ++step) {
      if (m_signal_scenario) {
        if (!m_signal_cache) {
          throw std::runtime_error{"signal navigation cache is unavailable"};
        }
        const auto reached = advance_signal_navigation_acceptance(
            m_planet, *m_signal_cache, *m_signal_scenario);
        if (!reached) {
          throw std::runtime_error{"signal collection acceptance failed"};
        }
        continue;
      }
      if (m_flight_deck_acceptance &&
          m_flight.tick == kFlightDeckAcceptanceTicks) {
        break;
      }
      std::vector<FlightCommand> input_commands;
      std::span<const FlightCommand> commands;
      if (m_flight_deck_acceptance) {
        const auto acceptance_commands = flight_deck_acceptance_commands();
        const auto first = m_acceptance_next_command;
        while (m_acceptance_next_command < acceptance_commands.size() &&
               acceptance_commands[m_acceptance_next_command].tick ==
                   m_flight.tick) {
          ++m_acceptance_next_command;
        }
        commands = acceptance_commands.subspan(
            first, m_acceptance_next_command - first);
      } else {
        input_commands = m_input_mapper.take_commands(m_flight.tick);
        commands = input_commands;
      }
      if (!advance_flight(m_terrain, m_flight, commands, kSimulationStep)) {
        throw std::runtime_error{"simulation rejected flight state"};
      }
    }
  }

  auto render_viewport() -> void {
    if (m_signal_scenario) {
      if (!m_planetary_renderer) {
        throw std::runtime_error{"signal presentation is unavailable"};
      }
      const auto rendered = m_planetary_renderer->render(
          m_planet, m_signal_scenario->flight, {.pitch_radians = -0.18},
          m_surface.pixels());
      if (!rendered) {
        throw std::runtime_error{"signal presentation rejected frame"};
      }
      m_planetary_samples.push_back(*rendered);
      return;
    }
    if (m_workload == BenchmarkWorkload::planetary) {
      const auto bands = flight_regime_bands(m_planet);
      if (!bands) throw std::runtime_error{"invalid planetary flight bands"};
      const int phase = m_frame % 4;
      double altitude{};
      double clearance{};
      FlightRegime regime{};
      std::optional<FlightRegimeTransition> transition;
      if (phase == 0) {
        altitude = bands->orbit_enter_altitude_metres + 50'000.0;
        clearance = altitude - m_planetary_surface.elevation_metres;
        regime = FlightRegime::orbital;
      } else if (phase == 1) {
        altitude = (bands->orbit_enter_altitude_metres +
                    bands->atmosphere_enter_altitude_metres) * 0.5;
        clearance = altitude - m_planetary_surface.elevation_metres;
        regime = FlightRegime::atmospheric;
        transition = FlightRegimeTransition{
            FlightRegime::orbital, FlightRegime::atmospheric,
            static_cast<SimulationTick>(m_frame)};
      } else if (phase == 2) {
        clearance = (bands->terrain_enter_clearance_metres +
                     bands->terrain_exit_clearance_metres) * 0.5;
        altitude = m_planetary_surface.elevation_metres + clearance;
        regime = FlightRegime::atmospheric;
        transition = FlightRegimeTransition{
            FlightRegime::orbital, FlightRegime::atmospheric,
            static_cast<SimulationTick>(m_frame)};
      } else {
        clearance = 100.0;
        altitude = m_planetary_surface.elevation_metres + clearance;
        regime = FlightRegime::terrain_flight;
        transition = FlightRegimeTransition{
            FlightRegime::atmospheric, FlightRegime::terrain_flight,
            static_cast<SimulationTick>(m_frame)};
      }
      m_planetary_flight = PlanetaryFlightState{
          .tick = static_cast<SimulationTick>(m_frame),
          .planet = m_planet.id,
          .pose = {{m_planetary_surface.latitude_radians,
                    m_planetary_surface.longitude_radians, altitude},
                   0.35},
          .velocity = {85.0, 24.0, phase < 3 ? -30.0 : 0.0},
          .clearance_metres = clearance,
          .mode = FlightMode::autopilot,
          .controls = {},
          .regime = regime,
          .last_transition = transition,
      };
      if (!m_planetary_renderer) {
        throw std::runtime_error{"planetary presentation is unavailable"};
      }
      const auto rendered = m_planetary_renderer->render(
          m_planet, *m_planetary_flight,
          {.pitch_radians = phase == 2 ? -1.25
                                       : (phase == 3 ? 0.0 : -0.08)},
          m_surface.pixels());
      if (!rendered) {
        throw std::runtime_error{"planetary presentation rejected frame"};
      }
      m_planetary_samples.push_back(*rendered);
      return;
    }
    m_planetary_flight.reset();
    if (m_workload == BenchmarkWorkload::orbital) {
      const double radius = static_cast<double>(m_planet.radius.value) * 1'000.0;
      const double elapsed_seconds =
          static_cast<double>(m_flight.tick) * kSimulationStep.count();
      const double angle = 0.35 + elapsed_seconds * 0.025;
      OrbitalCamera camera;
      camera.position = {std::cos(angle) * radius * 3.5,
                         std::sin(angle) * radius * 3.5, radius * 0.25};
      camera.forward = {-camera.position.x, -camera.position.y,
                        -camera.position.z};
      camera.up = {0.0, 0.0, 1.0};
      const auto rendered =
          m_orbital_renderer.render(m_planet, camera, m_surface.pixels());
      if (!rendered) {
        throw std::runtime_error{std::format(
            "orbital renderer rejected the {}x{} surface",
            m_render_configuration.viewport.width,
            m_render_configuration.viewport.height)};
      }
      return;
    }

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
  PlanetDescriptor m_planet;
  VoxelRenderer m_renderer;
  OrbitalRenderer m_orbital_renderer;
  std::optional<PlanetaryPresentationRenderer> m_planetary_renderer;
  PixelSurface m_surface;
  Frame m_left_frame{"FLIGHT"};
  Frame m_viewport_frame{"EXTERIOR"};
  Frame m_right_frame{"NAV"};
  Frame m_message_frame{"COMMS"};
  Frame m_menu_frame{"SYSTEM"};
  FlightState m_flight;
  PlanetarySurfaceFixture m_planetary_surface;
  std::optional<PlanetaryFlightState> m_planetary_flight;
  std::optional<TerrainTileCache> m_signal_cache;
  std::optional<SignalNavigationAcceptanceState> m_signal_scenario;
  std::vector<PlanetaryRenderStats> m_planetary_samples;
  SessionController m_session;
  FixedStepClock m_simulation_clock;
  apsis_drift::detail::FlightInputMapper m_input_mapper;
  SyntheticClock m_headless_clock;
  MeasuringSink m_sink;
  Clock::time_point m_run_started{};
  double m_capture_seconds{};
  std::uint32_t m_seed{};
  BenchmarkWorkload m_workload{BenchmarkWorkload::landscape};
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
  bool m_flight_deck_acceptance{};
  bool m_signal_navigation_acceptance{};
  bool m_acceptance_final_frame_rendered{};
  int m_acceptance_final_frames{};
  std::size_t m_acceptance_next_command{};
};

auto print_summary(const BenchmarkSummary& summary,
                   const RenderConfiguration& configuration,
                   BenchmarkWorkload workload) -> void {
  std::printf(
      "%.*s: profile=%.*s viewport=%dx%d frames=%zu elapsed=%.3fs "
      "achieved=%.2f fps\n"
      "timing: renderer avg/p95 %.3f/%.3f ms, full frame work %.3f/%.3f ms\n"
      "wire: %.1f KiB/frame, %.2f MiB/s, total %.2f MiB\n"
      "checksum: %llu\n",
      static_cast<int>(workload_name(workload).size()),
      workload_name(workload).data(),
      static_cast<int>(profile_name(configuration).size()),
      profile_name(configuration).data(), configuration.viewport.width,
      configuration.viewport.height, summary.frames, summary.elapsed_seconds,
      summary.achieved_fps,
      summary.render_avg_ms, summary.render_p95_ms, summary.work_avg_ms,
      summary.work_p95_ms, summary.bytes_per_frame / 1024.0,
      summary.mebibytes_per_second,
      static_cast<double>(summary.total_bytes) / (1024.0 * 1024.0),
      static_cast<unsigned long long>(summary.checksum));
  if (summary.planetary_presentation) {
    const auto& presentation = *summary.planetary_presentation;
    std::printf(
        "planetary stages: orbital/atmosphere/blend/local=%zu/%zu/%zu/%zu "
        "passes avg orbital/local/composite %.3f/%.3f/%.3f ms, "
        "total avg/p95 %.3f/%.3f ms, max tiles=%zu\n",
        presentation.orbital_frames, presentation.atmospheric_frames,
        presentation.terrain_blend_frames,
        presentation.local_terrain_frames,
        presentation.orbital_render_avg_ms,
        presentation.local_render_avg_ms, presentation.composite_avg_ms,
        presentation.total_avg_ms, presentation.total_p95_ms,
        presentation.maximum_tiles_touched);
  }
}

[[nodiscard]] auto summary_json(const BenchmarkSummary& summary,
                                const RenderConfiguration& configuration,
                                BenchmarkWorkload workload)
    -> std::string {
  const std::string_view workload_prefix =
      workload == BenchmarkWorkload::orbital
          ? "orbital-planet"
          : (workload == BenchmarkWorkload::planetary
                 ? "planetary-presentation"
                 : "voxel-landscape");
  std::string presentation;
  if (summary.planetary_presentation) {
    const auto& value = *summary.planetary_presentation;
    presentation = std::format(
        ",\n"
        "  \"planetary_presentation\": {{\n"
        "    \"mode_frames\": {{\"orbital\": {}, "
        "\"atmospheric\": {}, \"terrain_blend\": {}, "
        "\"local_terrain\": {}}},\n"
        "    \"orbital_render_avg_ms\": {:.6f},\n"
        "    \"local_render_avg_ms\": {:.6f},\n"
        "    \"composite_avg_ms\": {:.6f},\n"
        "    \"total_avg_ms\": {:.6f},\n"
        "    \"total_p95_ms\": {:.6f},\n"
        "    \"maximum_tiles_touched\": {}\n"
        "  }}",
        value.orbital_frames, value.atmospheric_frames,
        value.terrain_blend_frames, value.local_terrain_frames,
        value.orbital_render_avg_ms,
        value.local_render_avg_ms, value.composite_avg_ms,
        value.total_avg_ms, value.total_p95_ms,
        value.maximum_tiles_touched);
  }
  return std::format(
      "{{\n"
      "  \"workload\": \"{}-{}x{}-rgba\",\n"
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
      "  \"checksum\": {}{}\n"
      "}}\n",
      workload_prefix, configuration.viewport.width,
      configuration.viewport.height,
      profile_name(configuration), configuration.viewport.width,
      configuration.viewport.height,
      summary.frames, summary.elapsed_seconds, summary.achieved_fps,
      summary.render_avg_ms, summary.render_p95_ms, summary.work_avg_ms,
      summary.work_p95_ms, summary.bytes_per_frame,
      summary.mebibytes_per_second, summary.total_bytes, summary.checksum,
      presentation);
}

template <typename T>
[[nodiscard]] auto parse_positive(std::string_view text, T& value) -> bool {
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size() && value > 0;
}

template <typename T>
[[nodiscard]] auto parse_unsigned(std::string_view text, T& value) -> bool {
  if (text.empty() || text.front() == '-' || text.front() == '+') return false;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size();
}

[[nodiscard]] auto presentation_seed(Seed universe_seed) noexcept
    -> std::uint32_t {
  return static_cast<std::uint32_t>(universe_seed.value ^
                                    (universe_seed.value >> 32U));
}

auto usage() -> void {
  std::puts(
      "Usage: apsis-drift [--seed N] [--profile NAME] "
      "[--viewport WIDTHxHEIGHT]\n"
      "                   [--load PATH | --new-game-seed N] [--save PATH]\n"
      "       apsis-drift [--driver automatic|kitty|ansi|fallback]\n"
      "                   [--keyboard enhanced|press-only]\n"
      "       apsis-drift --benchmark [FRAMES] [--seed N] [--report PATH]\n"
      "       apsis-drift --sweep [FRAMES] --report PATH\n"
      "                   [--sweep-viewports LIST] [--sweep-fps LIST]\n"
      "                   [--workload landscape|orbital|planetary]\n"
      "       apsis-drift --capture-seconds N [--seed N] --report PATH\n\n"
      "       apsis-drift --flight-deck-acceptance --report PATH\n"
      "                   [--driver kitty|ansi] [--profile NAME]\n\n"
      "       apsis-drift --planetfall-acceptance --report PATH\n"
      "                   [--profile NAME] [--snapshot PATH]\n\n"
      "       apsis-drift --signal-navigation-acceptance --report PATH\n"
      "                   [--driver kitty|ansi] [--profile NAME]\n\n"
      "Profiles: remote (320x240), balanced (512x320), local (640x480, "
      "default),\n"
      "and cinematic (1024x768). An explicit viewport overrides the "
      "profile.\n"
      "Sweep defaults: remote,balanced,local at 30,60 FPS. Viewport list\n"
      "entries may be profile names or validated WIDTHxHEIGHT values.\n"
      "Workload selection is available only for benchmark and sweep modes.\n"
      "Profile options are interactive-only. --load restores a validated save;\n"
      "--save writes on clean exit; --new-game-seed accepts any 64-bit unsigned\n"
      "integer. Loading and saving different paths performs an explicit save-as.\n"
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
  bool flight_deck_acceptance{};
  bool planetfall_acceptance{};
  bool signal_navigation_acceptance{};
  std::uint32_t seed = 0xC0FFEEU;
  DriverChoice driver_choice{DriverChoice::automatic};
  KeyboardChoice keyboard_choice{KeyboardChoice::enhanced};
  RenderProfile selected_profile{RenderProfile::local};
  BenchmarkWorkload selected_workload{BenchmarkWorkload::landscape};
  std::optional<ViewportSize> viewport_override;
  auto sweep_viewports = default_sweep_viewports();
  auto sweep_fps = default_sweep_fps();
  std::filesystem::path report_path;
  std::filesystem::path snapshot_path;
  std::filesystem::path load_path;
  std::filesystem::path save_path;
  std::optional<std::uint64_t> new_game_seed;
  bool profile_specified{};
  bool viewport_specified{};
  bool driver_specified{};
  bool keyboard_specified{};
  bool seed_specified{};
  bool sweep_viewports_specified{};
  bool sweep_fps_specified{};
  bool workload_specified{};

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
    if (argument == "--flight-deck-acceptance") {
      flight_deck_acceptance = true;
      continue;
    }
    if (argument == "--planetfall-acceptance") {
      planetfall_acceptance = true;
      continue;
    }
    if (argument == "--signal-navigation-acceptance") {
      signal_navigation_acceptance = true;
      continue;
    }
    if (argument == "--seed" && i + 1 < argc) {
      if (!parse_positive(std::string_view{argv[++i]}, seed)) {
        std::fprintf(stderr, "seed must be a positive 32-bit integer\n");
        return 2;
      }
      seed_specified = true;
      continue;
    }
    if (argument == "--new-game-seed" && i + 1 < argc) {
      std::uint64_t value{};
      if (!parse_unsigned(std::string_view{argv[++i]}, value)) {
        std::fprintf(stderr,
                     "new-game seed must be an unsigned 64-bit integer\n");
        return 2;
      }
      new_game_seed = value;
      continue;
    }
    if (argument == "--load" && i + 1 < argc) {
      const std::string_view value{argv[++i]};
      if (value.empty()) {
        std::fprintf(stderr, "load path must not be empty\n");
        return 2;
      }
      load_path = value;
      continue;
    }
    if (argument == "--save" && i + 1 < argc) {
      const std::string_view value{argv[++i]};
      if (value.empty()) {
        std::fprintf(stderr, "save path must not be empty\n");
        return 2;
      }
      save_path = value;
      continue;
    }
    if (argument == "--workload" && i + 1 < argc) {
      const std::string_view value{argv[++i]};
      const auto parsed = parse_benchmark_workload(value);
      if (!parsed) {
        std::fprintf(stderr, "unknown benchmark workload '%.*s'\n",
                     static_cast<int>(value.size()), value.data());
        return 2;
      }
      selected_workload = *parsed;
      workload_specified = true;
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
  const bool profile_options = !load_path.empty() || !save_path.empty() ||
                               new_game_seed.has_value();
  if (!load_path.empty() && new_game_seed) {
    std::fprintf(stderr,
                 "--load and --new-game-seed are mutually exclusive\n");
    return 2;
  }
  if (profile_options && seed_specified) {
    std::fprintf(stderr,
                 "--seed cannot be combined with save-profile options; use "
                 "--new-game-seed\n");
    return 2;
  }
  if (profile_options &&
      (benchmark_frames || sweep_frames || capture_seconds > 0 ||
       flight_deck_acceptance || planetfall_acceptance ||
       signal_navigation_acceptance)) {
    std::fprintf(stderr,
                 "save-profile options are available only for interactive "
                 "runs\n");
    return 2;
  }
  if (flight_deck_acceptance &&
      (benchmark_frames || sweep_frames || capture_seconds > 0 ||
       signal_navigation_acceptance)) {
    std::fprintf(
        stderr,
        "Flight Deck acceptance, sweep, benchmark, and capture modes are "
        "mutually exclusive\n");
    return 2;
  }
  if (planetfall_acceptance &&
      (benchmark_frames || sweep_frames || capture_seconds > 0 ||
       flight_deck_acceptance || signal_navigation_acceptance)) {
    std::fprintf(
        stderr,
        "Planetfall acceptance, Flight Deck acceptance, sweep, benchmark, "
        "and capture modes are mutually exclusive\n");
    return 2;
  }
  if (flight_deck_acceptance && seed_specified) {
    std::fprintf(stderr,
                 "Flight Deck acceptance uses the fixed seed %u\n",
                 kFlightDeckAcceptanceSeed);
    return 2;
  }
  if (planetfall_acceptance && seed_specified) {
    std::fprintf(stderr,
                 "Planetfall acceptance uses the fixed seed %u\n",
                 kPlanetfallAcceptanceSeed);
    return 2;
  }
  if (signal_navigation_acceptance && seed_specified) {
    std::fprintf(stderr,
                 "Signal navigation acceptance uses the fixed seed %u\n",
                 kSignalNavigationAcceptanceSeed);
    return 2;
  }
  if (signal_navigation_acceptance &&
      (benchmark_frames || sweep_frames || capture_seconds > 0)) {
    std::fprintf(
        stderr,
        "Signal navigation acceptance, sweep, benchmark, and capture modes "
        "are mutually exclusive\n");
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
  if (workload_specified && !benchmark_frames && !sweep_frames) {
    std::fprintf(stderr,
                 "--workload requires --benchmark or --sweep\n");
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
  if (flight_deck_acceptance && report_path.empty()) {
    std::fprintf(stderr,
                 "Flight Deck acceptance mode requires --report PATH\n");
    return 2;
  }
  if (planetfall_acceptance && report_path.empty()) {
    std::fprintf(stderr,
                 "Planetfall acceptance mode requires --report PATH\n");
    return 2;
  }
  if (signal_navigation_acceptance && report_path.empty()) {
    std::fprintf(stderr,
                 "Signal navigation acceptance mode requires --report PATH\n");
    return 2;
  }
  if (planetfall_acceptance &&
      (driver_specified || keyboard_specified || workload_specified)) {
    std::fprintf(stderr,
                 "Planetfall acceptance does not accept driver, keyboard, "
                 "or workload options\n");
    return 2;
  }
  if (signal_navigation_acceptance && workload_specified) {
    std::fprintf(stderr,
                 "Signal navigation acceptance does not accept workload "
                 "options\n");
    return 2;
  }

  try {
    if (planetfall_acceptance) {
      const RenderConfiguration configuration =
          resolve_render_configuration(selected_profile, viewport_override);
      const auto acceptance = run_planetfall_acceptance(configuration);
      if (!acceptance) {
        std::fprintf(stderr, "Planetfall acceptance failed (%u)\n",
                     static_cast<unsigned>(acceptance.error()));
        return 1;
      }
      std::ofstream report{report_path};
      if (!report) {
        std::fprintf(stderr, "cannot open report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
      report << planetfall_acceptance_json(acceptance->report);
      if (!report.good()) {
        std::fprintf(stderr, "cannot write report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
      if (!snapshot_path.empty() &&
          !write_snapshot(snapshot_path, configuration.viewport,
                          acceptance->final_frame)) {
        std::fprintf(stderr, "cannot write snapshot '%s'\n",
                     snapshot_path.string().c_str());
        return 1;
      }
      std::printf(
          "planetfall: seed=%u profile=%.*s viewport=%dx%d final-tick=%llu "
          "checksum=%llu\n",
          kPlanetfallAcceptanceSeed,
          static_cast<int>(profile_name(configuration).size()),
          profile_name(configuration).data(), configuration.viewport.width,
          configuration.viewport.height,
          static_cast<unsigned long long>(acceptance->report.final_state.tick),
          static_cast<unsigned long long>(planetary_flight_state_checksum(
              acceptance->report.final_state)));
      for (const auto& stage : acceptance->report.stages) {
        std::printf("  %.*s tick=%llu avg/p95=%.3f/%.3f ms frame=%llu\n",
                    static_cast<int>(planetary_presentation_mode_name(
                                         stage.presentation_mode)
                                         .size()),
                    planetary_presentation_mode_name(stage.presentation_mode)
                        .data(),
                    static_cast<unsigned long long>(stage.tick),
                    stage.total_avg_ms, stage.total_p95_ms,
                    static_cast<unsigned long long>(
                        stage.framebuffer_checksum));
      }
      return 0;
    }

    if (sweep_frames) {
      std::vector<BenchmarkMeasurement> measurements;
      measurements.reserve(sweep_viewports.size());
      for (const auto& configuration : sweep_viewports) {
        LandscapeApp app{configuration, seed, selected_workload};
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
                           static_cast<std::size_t>(*sweep_frames),
                           selected_workload);
      if (!report.good()) {
        std::fprintf(stderr, "cannot write report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }

      std::printf("sweep: workload=%.*s seed=%u frames-per-viewport=%d\n",
                  static_cast<int>(workload_name(selected_workload).size()),
                  workload_name(selected_workload).data(), seed,
                  *sweep_frames);
      const auto table = sweep_table(measurements, sweep_fps);
      std::fputs(table.c_str(), stdout);
      return 0;
    }

    const RenderConfiguration render_configuration =
        resolve_render_configuration(selected_profile, viewport_override);
    std::uint32_t run_seed =
        flight_deck_acceptance
            ? kFlightDeckAcceptanceSeed
            : (signal_navigation_acceptance
                   ? kSignalNavigationAcceptanceSeed
                   : seed);
    const bool interactive_controls =
        !benchmark_frames && capture_seconds == 0 &&
        !flight_deck_acceptance && !signal_navigation_acceptance;
    std::optional<SaveDocument> save_profile;
    if (interactive_controls) {
      if (!load_path.empty()) {
        auto loaded = load_save_file(load_path);
        if (!loaded) {
          const auto message = save_file_error_message(loaded.error());
          std::fprintf(stderr, "cannot load save: %s\n", message.c_str());
          return 1;
        }
        save_profile = std::move(*loaded);
        run_seed = presentation_seed(save_profile->recipe.universe_seed);
      } else {
        const Seed universe_seed{new_game_seed.value_or(seed)};
        save_profile = make_new_game_document(universe_seed);
        if (new_game_seed) run_seed = presentation_seed(universe_seed);
      }
    }
    LandscapeApp app{render_configuration, run_seed, selected_workload,
                     static_cast<double>(capture_seconds),
                     interactive_controls, flight_deck_acceptance,
                     signal_navigation_acceptance};
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

    if ((flight_deck_acceptance || signal_navigation_acceptance) &&
        result == 0 &&
        !app.acceptance_complete()) {
      std::fprintf(stderr, "%s acceptance ended before the final frame\n",
                   signal_navigation_acceptance ? "Signal collection"
                                                : "Flight Deck");
      result = 1;
    }

    if (!app.error().empty()) {
      std::fprintf(stderr, "last TermForge event: %s\n", app.error().c_str());
    }
    const BenchmarkSummary summary = app.summary();
    std::printf("display: %s\n", app.display_path().c_str());
    print_summary(summary, app.render_configuration(), app.workload());
    if (!snapshot_path.empty() && !app.write_snapshot(snapshot_path)) {
      std::fprintf(stderr, "cannot write snapshot '%s'\n",
                   snapshot_path.string().c_str());
      return 1;
    }
    if (!report_path.empty() && result == 0) {
      std::ofstream report{report_path};
      if (!report) {
        std::fprintf(stderr, "cannot open report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
      if (signal_navigation_acceptance) {
        const auto* state = app.signal_acceptance_state();
        if (state == nullptr || !state->scanner.selected) {
          std::fprintf(stderr,
                       "signal collection acceptance report is unavailable\n");
          return 1;
        }
        if (!state->reached_tick || !state->collection.completion_tick) {
          std::fprintf(stderr,
                       "signal collection checkpoints are unavailable\n");
          return 1;
        }
        report << signal_navigation_acceptance_json({
            .target_id = *state->scanner.selected,
            .reached_tick = *state->reached_tick,
            .completion_tick = *state->collection.completion_tick,
            .command_count = state->command_count,
            .world_delta_count = state->journal.entries().size(),
            .final_distance_metres = state->navigation.distance_metres,
            .flight_checksum = planetary_flight_state_checksum(state->flight),
            .render_configuration = app.render_configuration(),
            .presentation = app.display_tier(),
            .framebuffer_checksum = summary.checksum,
        });
      } else if (flight_deck_acceptance) {
        report << flight_deck_acceptance_json({
            .flight_checksum = app.flight_checksum(),
            .framebuffer_checksum = summary.checksum,
            .render_configuration = app.render_configuration(),
            .presentation = app.display_tier(),
        });
      } else {
        report << summary_json(summary, app.render_configuration(),
                               app.workload());
      }
      if (!report.good()) {
        std::fprintf(stderr, "cannot write report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
    }
    if (!save_path.empty() && result == 0) {
      if (!save_profile) {
        std::fprintf(stderr, "cannot save without an interactive profile\n");
        return 1;
      }
      if (auto saved = write_save_file_atomically(save_path, *save_profile);
          !saved) {
        const auto message = save_file_error_message(saved.error());
        std::fprintf(stderr, "cannot save profile: %s\n", message.c_str());
        return 1;
      }
      std::printf("save: %s\n", save_path.string().c_str());
    }
    return result;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "apsis-drift: %s\n", error.what());
    return 1;
  }
}
