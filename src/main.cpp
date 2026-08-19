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
#include "termforge/drivers/fallback_driver.hpp"
#include "termforge/drivers/kitty_driver.hpp"
#include "termforge/widgets/frame.hpp"
#include "termforge/widgets/pixel_surface.hpp"
#include "apsis_drift/benchmark.hpp"
#include "apsis_drift/cockpit.hpp"
#include "apsis_drift/flight_deck_acceptance.hpp"
#include "apsis_drift/intersystem_contract.hpp"
#include "apsis_drift/intersystem_contract_acceptance.hpp"
#include "apsis_drift/intersystem_jump.hpp"
#include "apsis_drift/intersystem_jump_acceptance.hpp"
#include "apsis_drift/intersystem_planetfall.hpp"
#include "apsis_drift/intersystem_planetfall_acceptance.hpp"
#include "apsis_drift/intersystem_return_acceptance.hpp"
#include "apsis_drift/landscape.hpp"
#include "apsis_drift/local_system.hpp"
#include "apsis_drift/menu.hpp"
#include "apsis_drift/mission_board.hpp"
#include "apsis_drift/orbital.hpp"
#include "apsis_drift/origin_return.hpp"
#include "apsis_drift/planet.hpp"
#include "apsis_drift/planetfall_acceptance.hpp"
#include "apsis_drift/planetary_presentation.hpp"
#include "apsis_drift/render_profile.hpp"
#include "apsis_drift/save_file.hpp"
#include "apsis_drift/signal_navigation_acceptance.hpp"
#include "apsis_drift/signal_run.hpp"
#include "apsis_drift/signal_run_acceptance.hpp"
#include "apsis_drift/signal_scanner.hpp"
#include "apsis_drift/simulation.hpp"
#include "apsis_drift/system_flight.hpp"
#include "apsis_drift/system_flight_acceptance.hpp"
#include "apsis_drift/system_rendering.hpp"
#include "apsis_drift/title.hpp"
#include "apsis_drift/version.hpp"
#include "capability_floor.hpp"
#include "flight_input.hpp"
#include "signal_input.hpp"

namespace {

using Clock = std::chrono::steady_clock;
using namespace termforge;
using namespace apsis_drift;

template <typename ScreenType>
auto clear_screen(ScreenType& screen, const Cell& fill) -> void {
  if constexpr (requires { screen.clear(fill); }) {
    screen.clear(fill);
  } else {
    screen.clear(fill.fg, fill.bg, fill.attrs);
  }
}

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

[[nodiscard]] auto make_headless_driver(DriverChoice choice)
    -> std::unique_ptr<TerminalDriver> {
  switch (choice) {
    case DriverChoice::automatic:
    case DriverChoice::kitty: {
      auto driver = std::make_unique<KittyDriver>();
      driver->set_cell_pixel_size({8, 16});
      return driver;
    }
    case DriverChoice::ansi: return std::make_unique<AnsiRgbDriver>();
    case DriverChoice::fallback: return std::make_unique<FallbackDriver>();
  }
  throw std::runtime_error{"unknown headless driver choice"};
}

[[nodiscard]] auto headless_presentation_name(
    const TerminalDriver& driver) -> std::string_view {
  if (driver.name() == "kitty") return "kitty";
  if (driver.name() == "ansi-rgb") return "ansi";
  if (driver.name() == "fallback") return "fallback";
  throw std::runtime_error{
      std::format("unsupported headless driver '{}'", driver.name())};
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
                        bool signal_navigation_acceptance = false,
                        const SaveDocument* profile = nullptr)
      : m_render_configuration(render_configuration),
        m_terrain(required_terrain(1024, seed)),
        m_planet(generate_planet_descriptor(Seed{seed})),
        m_origin_system(generate_local_system(
            generate_first_intersystem_identities(
                profile ? profile->recipe.universe_seed : Seed{seed})
                .origin_system_seed)),
        m_local_system(generate_local_system(
            generate_first_intersystem_identities(
                profile ? profile->recipe.universe_seed : Seed{seed})
                .target_system_seed)),
        m_renderer(render_settings_for(render_configuration.viewport)),
        m_orbital_renderer(
            orbital_settings_for(render_configuration.viewport)),
        m_system_renderer(
            (workload == BenchmarkWorkload::system || profile != nullptr)
                ? std::optional<LocalSystemRenderer>{
                      std::in_place,
                      LocalSystemRenderSettings{
                          .width = render_configuration.viewport.width,
                          .height = render_configuration.viewport.height,
                          .field_of_view_degrees = 60.0}}
                : std::nullopt),
        m_planetary_renderer(
            (workload == BenchmarkWorkload::planetary ||
             signal_navigation_acceptance || profile != nullptr)
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
        m_session(!interactive_controls,
                  profile != nullptr &&
                      (profile->state.intersystem_contract
                           ? profile->state.intersystem_contract->travel_phase ==
                                 IntersystemTravelPhase::docked_at_origin
                           : profile->state.location ==
                                 OriginLocation::docked_at_origin)),
        m_capture_seconds(capture_seconds),
        m_seed(seed),
        m_workload(workload),
        m_interactive_controls(interactive_controls),
        m_flight_deck_acceptance(flight_deck_acceptance),
        m_signal_navigation_acceptance(signal_navigation_acceptance) {
    if (profile != nullptr) {
      m_save_profile = *profile;
      if (profile->state.intersystem_contract) {
        m_intersystem_contract = *profile->state.intersystem_contract;
        m_system_flight = profile->state.system_flight;
        m_origin_return = profile->state.origin_return;
        m_intersystem_world_deltas = profile->state.world_deltas;
        if (profile->state.flight) {
          auto cache = TerrainTileCache::create();
          const auto body = find_local_system_planet(
              m_local_system,
              m_intersystem_contract->identities.target_planet);
          if (!cache || !body) {
            throw std::runtime_error{
                "cannot create target Planetfall terrain state"};
          }
          auto planetfall = initialize_intersystem_planetfall(
              (*body)->descriptor,
              m_intersystem_contract->identities.target_objective,
              *profile->state.flight, profile->state.world_deltas, *cache);
          if (!planetfall) {
            throw std::runtime_error{
                "cannot hydrate target Planetfall state"};
          }
          m_intersystem_planetfall_cache.emplace(std::move(*cache));
          m_intersystem_planetfall.emplace(std::move(*planetfall));
        }
      } else {
        auto cache = TerrainTileCache::create();
        if (!cache) {
          throw std::runtime_error{"cannot create Signal Run terrain cache"};
        }
        m_signal_run_cache.emplace(std::move(*cache));
        auto run = hydrate_signal_run(*profile, *m_signal_run_cache);
        if (!run) {
          throw std::runtime_error{"cannot hydrate Signal Run profile"};
        }
        m_signal_run.emplace(std::move(*run));
      }
    }
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
        m_input_mapper.neutralize_mouse(current_flight_tick());
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
        m_input_mapper.enqueue(*mouse, m_active_mouse_region,
                               current_flight_tick());
      }
    } else if (const auto* resize = std::get_if<ResizeEvent>(&event)) {
      m_input_mapper.neutralize_mouse(current_flight_tick());
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
    clear_screen(screen, background);
    const Extent one_cell = driver().preferred_pixel_extent({0, 0, 1, 1});
    const auto layout = compute_cockpit_layout(
        screen.cols(), screen.rows(), one_cell,
        m_render_configuration.viewport);
    if (m_session.screen() == SessionScreen::title) {
      draw_title_screen(screen);
    } else if (m_session.screen() == SessionScreen::station) {
      draw_station_screen(screen);
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

  auto benchmark(int frames,
                 DriverChoice driver_choice = DriverChoice::automatic)
      -> void {
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
    auto selected = make_headless_driver(driver_choice);
    const std::string presentation{headless_presentation_name(*selected)};
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
    m_display_tier = presentation;
    m_display_path = std::format(
        "{} (headless, /dev/null input workaround for #256)", presentation);
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
    if (m_origin_return) {
      return origin_return_state_checksum(*m_origin_return);
    }
    if (m_system_flight) {
      return system_flight_state_checksum(*m_system_flight);
    }
    if (m_intersystem_planetfall) {
      return planetary_flight_state_checksum(
          m_intersystem_planetfall->flight);
    }
    if (m_signal_run && m_signal_run->flight) {
      return planetary_flight_state_checksum(*m_signal_run->flight);
    }
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
  [[nodiscard]] auto system_render_stats() const noexcept
      -> const LocalSystemRenderStats* {
    return m_system_render ? &*m_system_render : nullptr;
  }
  [[nodiscard]] auto local_system() const noexcept
      -> const LocalSystemDescriptor& {
    return m_local_system;
  }

  [[nodiscard]] auto signal_run_save() const
      -> std::expected<SaveDocument, SignalRunError> {
    if (m_intersystem_contract && m_save_profile) {
      auto document = *m_save_profile;
      document.state.intersystem_contract = *m_intersystem_contract;
      document.state.system_flight = m_system_flight;
      document.state.origin_return = m_origin_return;
      document.state.flight =
          m_intersystem_planetfall
              ? std::optional<PlanetaryFlightState>{
                    m_intersystem_planetfall->flight}
              : std::nullopt;
      if (m_intersystem_planetfall) {
        document.state.world_deltas = std::vector<SaveWorldDelta>{
            m_intersystem_planetfall->journal.entries().begin(),
            m_intersystem_planetfall->journal.entries().end()};
      } else {
        document.state.world_deltas = m_intersystem_world_deltas;
      }
      return document;
    }
    if (!m_signal_run) {
      return std::unexpected{SignalRunError::inconsistent_state};
    }
    return project_signal_run_save(*m_signal_run);
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
  [[nodiscard]] auto current_flight_tick() const noexcept -> SimulationTick {
    if (m_intersystem_contract) {
      return m_intersystem_contract->universe_tick;
    }
    return m_signal_run && m_signal_run->flight
               ? m_signal_run->flight->tick
               : m_flight.tick;
  }

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
    if (m_session.screen() == SessionScreen::station &&
        command == MenuCommand::activate &&
        m_session.selected() == MenuItem::primary) {
      if (m_intersystem_contract) {
        const auto snapshot =
            mission_board_snapshot(*m_intersystem_contract);
        if (!snapshot || !snapshot->primary_action_enabled) return;
        const auto board_command =
            m_intersystem_contract->mission_phase ==
                    IntersystemMissionPhase::offered
                ? IntersystemContractCommand::accept_mission
                : m_intersystem_contract->mission_phase ==
                          IntersystemMissionPhase::accepted
                      ? IntersystemContractCommand::launch
                      : IntersystemContractCommand::turn_in;
        const auto advanced = advance_intersystem_contract(
            *m_intersystem_contract,
            m_intersystem_contract->universe_tick, board_command);
        if (!advanced) {
          m_error = "mission board action was rejected";
        } else if (board_command == IntersystemContractCommand::launch) {
          (void)m_session.start_flight();
          m_simulation_clock.reset();
          m_active_mouse_region = {};
        }
        return;
      }
      if (!m_signal_run || !m_signal_run_cache) {
        m_error = "Signal Run station state is unavailable";
        return;
      }
      if (m_signal_run->onboarding.first_objective ==
          FirstObjectiveStatus::offered) {
        if (!accept_signal_run(*m_signal_run)) {
          m_error = "first objective acceptance was rejected";
        }
        return;
      }
      if (m_signal_run->onboarding.first_objective ==
          FirstObjectiveStatus::active) {
        if (!launch_signal_run(*m_signal_run, *m_signal_run_cache)) {
          m_error = "Signal Run launch was rejected";
          return;
        }
        (void)m_session.start_flight();
        m_simulation_clock.reset();
        m_active_mouse_region = {};
        return;
      }
      return;
    }
    const auto transition = m_session.dispatch(command);
    if (!transition.changed()) return;

    if (transition.from == SessionScreen::flight &&
        transition.to == SessionScreen::paused) {
      if (m_signal_run && m_signal_run->flight) {
        m_input_mapper.suspend(m_signal_run->flight->controls,
                               m_signal_run->flight->tick);
      } else if (m_system_flight) {
        m_input_mapper.suspend(m_system_flight->controls,
                               m_system_flight->tick);
      } else if (m_origin_return) {
        m_input_mapper.suspend(m_origin_return->controls,
                               m_origin_return->tick);
      } else if (m_intersystem_planetfall) {
        m_input_mapper.suspend(m_intersystem_planetfall->flight.controls,
                               m_intersystem_planetfall->flight.tick);
      } else {
        m_input_mapper.suspend(m_flight.controls, m_flight.tick);
      }
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
    if (m_session.screen() == SessionScreen::station &&
        m_intersystem_contract && key.action == KeyAction::Press &&
        (key.key == Key::Left || key.key == Key::Right)) {
      const auto board = mission_board_snapshot(*m_intersystem_contract);
      if (!board || !board->rule_profile_selection_enabled) return;
      const auto command =
          m_intersystem_contract->rule_profile ==
                  IntersystemRuleProfile::assisted
              ? IntersystemContractCommand::select_pilot_profile
              : IntersystemContractCommand::select_assisted_profile;
      if (!advance_intersystem_contract(*m_intersystem_contract,
                                        m_intersystem_contract->universe_tick,
                                        command)) {
        m_error = "rule profile selection was rejected";
      }
      return;
    }
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
                 std::string_view primary_label,
                 bool primary_enabled = true) -> void {
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
      centered(row, display,
               item == MenuItem::primary && !primary_enabled ? muted : text,
               background);
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
    draw_menu(screen,
              std::format("FIRST CONTRACT // v{}", kApplicationVersion),
              "CONTINUE");
  }

  auto draw_station_screen(Screen& screen) -> void {
    m_surface.set_geometry({});
    m_surface.draw(screen);
    render_pixel_regions(m_surface);
    if (m_intersystem_contract) {
      const auto board = mission_board_snapshot(*m_intersystem_contract);
      if (!board) {
        draw_menu(screen, "ORIGIN STATION", "CONTRACT INVALID", false);
        return;
      }
      draw_menu(screen, "ORIGIN STATION // MISSION BOARD",
                board->primary_action, board->primary_action_enabled);
      if (!m_menu_layout.supported()) return;
      constexpr Rgb text{205, 222, 224};
      constexpr Rgb muted{109, 143, 151};
      constexpr Rgb accent{126, 214, 210};
      constexpr Rgb background{7, 15, 24};
      const auto centered = [&](int row, std::string_view value, Rgb color) {
        screen.write_text(
            std::max(0, (screen.cols() - static_cast<int>(value.size())) / 2),
            std::max(1, m_menu_layout.art.y + row), value, color, background);
      };
      centered(1, board->station, muted);
      centered(2, std::format("FIRST INTERSYSTEM CONTRACT // {} // {}",
                              board->mission,
                              board->status),
               accent);
      centered(4, std::format("DESTINATION: {}", board->destination_system),
               text);
      centered(6, std::format("PLANET: {}", board->destination_planet), text);
      centered(8, std::format("OBJECTIVE: {}", board->objective), text);
      centered(10,
               std::format("RETURN REQUIRED: {}", board->return_destination),
               muted);
      centered(12,
               std::format("RULE PROFILE: {} // {} [{}]",
                           board->rule_profile,
                           board->rule_profile_description,
                           board->rule_profile_selection_enabled
                               ? "LEFT/RIGHT"
                               : "LOCKED"),
               board->rule_profile_selection_enabled ? accent : muted);
      return;
    }
    if (!m_signal_run) {
      draw_menu(screen, "ORIGIN STATION", "UNAVAILABLE");
      return;
    }
    std::string_view action{"RETURN COMPLETE"};
    std::string_view status{"COMPLETED"};
    if (m_signal_run->onboarding.first_objective ==
        FirstObjectiveStatus::offered) {
      action = "ACCEPT BRIEFING";
      status = "OFFERED";
    } else if (m_signal_run->onboarding.first_objective ==
               FirstObjectiveStatus::active) {
      action = "LAUNCH";
      status = "ACTIVE";
    }
    draw_menu(screen, "ORIGIN STATION", action);
    if (!m_menu_layout.supported()) return;
    constexpr Rgb text{205, 222, 224};
    constexpr Rgb muted{109, 143, 151};
    constexpr Rgb accent{126, 214, 210};
    constexpr Rgb background{7, 15, 24};
    const auto station = origin_station_id_string(
        m_signal_run->onboarding.origin_station);
    const auto objective = std::format("FIRST SIGNAL RUN // {}", status);
    screen.write_text(std::max(0, (screen.cols() -
                                   static_cast<int>(station.size())) /
                                      2),
                      std::max(1, m_menu_layout.art.y + 2), station, muted,
                      background);
    screen.write_text(std::max(0, (screen.cols() -
                                   static_cast<int>(objective.size())) /
                                      2),
                      std::max(2, m_menu_layout.art.y + 4), objective, text,
                      background);
    screen.write_text(
        std::max(0, (screen.cols() - 31) / 2),
        std::max(3, m_menu_layout.art.y + 6),
        "RETURN DESTINATION: ORIGIN STATION", muted, background);
    if (m_signal_run->onboarding.first_objective ==
        FirstObjectiveStatus::active) {
      constexpr std::string_view launch_help{
          "MATCH BRG | W/F THRUST | S/R BRAKE"};
      screen.write_text(
          std::max(0, (screen.cols() -
                       static_cast<int>(launch_help.size())) /
                          2),
          std::max(4, m_menu_layout.art.y + 8), launch_help, accent,
          background);
    }
  }

  auto draw_cockpit(Screen& screen, const CockpitLayout& layout,
                    double render_time, bool enhanced_pixels) -> void {
    if (!layout.supported()) {
      if (!m_active_mouse_region.empty()) {
        m_input_mapper.neutralize_mouse(current_flight_tick());
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
        m_signal_run && m_signal_run->flight
            ? format_flight_instruments(*m_signal_run->flight)
            : m_signal_scenario
            ? format_flight_instruments(m_signal_scenario->flight)
            : m_system_flight
            ? format_flight_instruments(*m_system_flight)
            : m_origin_return
            ? format_flight_instruments(*m_origin_return)
            : m_intersystem_planetfall
                  ? format_flight_instruments(
                        m_intersystem_planetfall->flight)
            : m_planetary_flight
                  ? format_flight_instruments(*m_planetary_flight)
                  : format_flight_instruments(m_flight);
    const std::optional<ThermalInstrumentReadout> thermal =
        m_intersystem_planetfall
            ? std::optional<ThermalInstrumentReadout>{
                  format_thermal_instruments(
                      *m_intersystem_planetfall->planet,
                      m_intersystem_planetfall->flight)}
            : std::nullopt;

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
                      layout.left_instruments.y + 2, "FLIGHT", accent,
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
                      layout.left_instruments.y + 6, instruments.drive,
                      instruments.drive == "BRAKING  " ? warning : text,
                      chrome_bg);
    if ((m_signal_run && m_signal_run->flight) || m_signal_scenario ||
        m_intersystem_planetfall || m_planetary_flight) {
      const auto regime = format_flight_regime(
          m_signal_run && m_signal_run->flight
              ? *m_signal_run->flight
              : (m_signal_scenario ? m_signal_scenario->flight
                 : (m_intersystem_planetfall
                        ? m_intersystem_planetfall->flight
                        : *m_planetary_flight)));
      screen.write_text(layout.left_instruments.x + 2,
                        layout.left_instruments.y + 8,
                        thermal ? thermal->load : regime.regime,
                        thermal ? (thermal->cue_state == ThermalCue::abort_climb
                                       ? danger
                                       : (thermal->trend_state ==
                                                  ThermalTrend::heating
                                              ? warning
                                              : text))
                                : (regime.valid ? text : danger),
                        chrome_bg);
    }
    screen.write_text(layout.left_instruments.x + 2,
                      layout.left_instruments.y + 10, instruments.speed,
                      text, chrome_bg);
    screen.write_text(layout.left_instruments.x + 2,
                      layout.left_instruments.y + 12, instruments.altitude,
                      text, chrome_bg);
    screen.write_text(layout.left_instruments.x + 2,
                      layout.left_instruments.y + 14,
                      instruments.clearance, alert_color, chrome_bg);
    screen.write_text(layout.left_instruments.x + 2,
                      layout.left_instruments.y + 16,
                      instruments.alert_state == CockpitAlert::none && thermal
                          ? thermal->cue
                          : instruments.alert,
                      instruments.alert_state == CockpitAlert::none && thermal
                          ? (thermal->cue_state == ThermalCue::abort_climb
                                 ? danger
                                 : (thermal->cue_state ==
                                            ThermalCue::slow_and_rise
                                        ? warning
                                        : text))
                          : alert_color,
                      chrome_bg);
    screen.write_text(layout.right_instruments.x + 2,
                      layout.right_instruments.y + 2, "TARGET", accent,
                      chrome_bg);
    if (m_origin_return && m_intersystem_contract) {
      const auto guidance = resolve_origin_return_guidance(
          *m_intersystem_contract, *m_origin_return);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 4, "ORIGIN STN", text,
                        chrome_bg);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 6, "BRG CENTER", text,
                        chrome_bg);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 8, "ELEV 000 ", text,
                        chrome_bg);
      screen.write_text(
          layout.right_instruments.x + 2, layout.right_instruments.y + 10,
          guidance ? std::format("DST {:>5.0f}", guidance->distance_metres)
                   : "DST ---- ",
          text, chrome_bg);
      screen.write_text(
          layout.right_instruments.x + 2, layout.right_instruments.y + 12,
          guidance ? std::format(
                         "CLS {:+5.0f}",
                         guidance->closing_speed_metres_per_second)
                   : "CLS ---- ",
          guidance && (guidance->cue == OriginReturnCue::opening ||
                       guidance->cue == OriginReturnCue::brake)
              ? warning
              : text,
          chrome_bg);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 14,
                        guidance && guidance->arrived ? "ETA ARRVD" : "ETA --:--",
                        text, chrome_bg);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 16,
                        guidance && guidance->arrived ? "ENTER DOCK" : "APPROACH ",
                        accent, chrome_bg);
    } else if (m_system_render) {
      const auto guidance = m_system_flight
                                ? resolve_system_flight_guidance(
                                      m_local_system, *m_system_flight)
                                : std::expected<SystemFlightGuidance,
                                                SystemFlightError>{
                                      std::unexpected{
                                          SystemFlightError::invalid_state}};
      const auto navigation = guidance
                                  ? format_system_navigation(
                                        m_system_render->navigation, *guidance)
                                  : format_system_navigation(
                                        m_system_render->navigation);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 4, navigation.target,
                        text, chrome_bg);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 6, navigation.bearing,
                        text, chrome_bg);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 8, navigation.elevation,
                        text, chrome_bg);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 10, navigation.distance,
                        text, chrome_bg);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 12, navigation.motion,
                        (guidance &&
                         (guidance->cue == SystemFlightCue::opening ||
                          guidance->cue == SystemFlightCue::brake))
                            ? warning
                            : text,
                        chrome_bg);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 14, navigation.arrival,
                        text, chrome_bg);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 16, navigation.cue,
                        text, chrome_bg);
    } else if ((m_signal_run && m_signal_run->flight) || m_signal_scenario ||
               m_intersystem_planetfall) {
      const auto& navigation =
          m_signal_run && m_signal_run->flight
              ? m_signal_run->signal_navigation
              : (m_signal_scenario
                     ? m_signal_scenario->navigation
                     : m_intersystem_planetfall->navigation);
      const auto& collection =
          m_signal_run && m_signal_run->flight
              ? m_signal_run->collection
              : (m_signal_scenario
                     ? m_signal_scenario->collection
                     : m_intersystem_planetfall->collection);
      const auto scanner =
          format_signal_scanner(navigation);
      const auto collection_readout =
          format_signal_collection(collection);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 4, scanner.target,
                        text, chrome_bg);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 6, scanner.bearing,
                        text, chrome_bg);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 8, scanner.distance,
                        text, chrome_bg);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 10, scanner.motion,
                        scanner.status == SignalScannerStatus::tracking &&
                                navigation.motion.cue ==
                                    TargetMotionCue::opening
                            ? warning
                            : text,
                        chrome_bg);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 12, scanner.arrival,
                        text, chrome_bg);
      screen.write_text(layout.right_instruments.x + 2,
                        layout.right_instruments.y + 14, scanner.strength,
                        muted, chrome_bg);
      screen.write_text(
          layout.right_instruments.x + 2,
          layout.right_instruments.y + 16,
          collection.status ==
                  SignalCollectionStatus::approach
              ? scanner.cue
              : collection_readout.cue,
          collection.status ==
                  SignalCollectionStatus::complete
              ? accent
              : (collection.status ==
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
    } else if (thermal) {
      message = std::format(
          " {} | {} | {} | {} | {} | R rise / S brake ", thermal->load,
          thermal->trend, thermal->limit, thermal->flight_path_angle,
          thermal->cue);
    } else if (m_signal_run && m_signal_run->flight) {
      if (m_signal_run->onboarding.first_objective ==
              FirstObjectiveStatus::completed &&
          m_signal_run->origin_navigation) {
        const auto& origin = *m_signal_run->origin_navigation;
        const std::string_view cue =
            origin.motion.cue == TargetMotionCue::brake
                ? "BRAKE NOW"
                : (origin.motion.cue == TargetMotionCue::opening
                       ? "OPENING"
                       : (origin.arrived ? "RENDEZVOUS" : "ASCEND + NAV"));
        message = std::format(
            " ORIGIN {:.0f} m | CLS {:+.0f} m/s | {} | ENTER when arrived ",
            origin.distance_metres,
            origin.motion.closing_speed_metres_per_second, cue);
      } else {
        const auto scanner =
            format_signal_scanner(m_signal_run->signal_navigation);
        if (m_signal_run->collection.status ==
            SignalCollectionStatus::approach) {
          message = std::format(
              " MATCH {} | W/F thrust | S/R brake | {} ", scanner.bearing,
              scanner.cue);
        } else {
          message =
              format_signal_collection(m_signal_run->collection).message;
        }
      }
    } else if (m_signal_scenario) {
      message =
          format_signal_collection(m_signal_scenario->collection).message;
    } else if (m_intersystem_planetfall) {
      const auto scanner =
          format_signal_scanner(m_intersystem_planetfall->navigation);
      const auto& flight = m_intersystem_planetfall->flight;
      if (m_intersystem_planetfall->collection.status ==
          SignalCollectionStatus::complete) {
        message = flight.regime == FlightRegime::orbital
                      ? " OBJECTIVE COMPLETE | ENTER DEPART PLANET "
                      : " OBJECTIVE COMPLETE | ASCEND TO ORBIT WITH R ";
      } else if (flight.regime == FlightRegime::orbital) {
        message = std::format(
            " {} | ALIGNMENT GUIDANCE ONLY | ENTRY ANYWHERE | F descend ",
            scanner.cue);
      } else {
        message = std::format(
            " MATCH {} | W/F thrust | S/R brake | {} ", scanner.bearing,
            scanner.cue);
      }
    } else if (m_intersystem_contract &&
               intersystem_jump_snapshot(*m_intersystem_contract)) {
      const auto jump =
          *intersystem_jump_snapshot(*m_intersystem_contract);
      if (jump.alignment) {
        message = std::format(
            " PILOT ALIGN {:+.1f} deg | VEL {:+.1f}% | {} | {} | J cancel ",
            static_cast<double>(
                jump.alignment->heading_error_millidegrees) /
                1'000.0,
            static_cast<double>(
                jump.alignment->velocity_error_basis_points) /
                100.0,
            intersystem_arrival_quality_name(
                jump.alignment->projected_quality),
            jump.alignment->correction);
      } else {
        const auto quality = jump.bound_quality
                                 ? intersystem_arrival_quality_name(
                                       *jump.bound_quality)
                                 : std::string_view{"AUTO ALIGN"};
        message = std::format(" {} | {} | {:3.0f}% | {} | {} ", jump.phase,
                              jump.destination, jump.progress * 100.0,
                              quality,
                              jump.cancelable ? "J CANCEL"
                                              : "ARRIVAL BOUND");
      }
    } else if (m_intersystem_contract &&
               m_intersystem_contract->travel_phase ==
                   IntersystemTravelPhase::origin_system_flight) {
      message = " FTL READY | J BEGIN 3-SECOND SPOOL | J AGAIN TO CANCEL ";
    } else if (m_origin_return && m_intersystem_contract) {
      const auto guidance = resolve_origin_return_guidance(
          *m_intersystem_contract, *m_origin_return);
      if (guidance) {
        std::string_view cue{"HOLD"};
        switch (guidance->cue) {
          case OriginReturnCue::closing: cue = "CLOSING"; break;
          case OriginReturnCue::opening: cue = "OPENING"; break;
          case OriginReturnCue::brake: cue = "BRAKE NOW"; break;
          case OriginReturnCue::arrived: cue = "ENTER DOCK"; break;
          case OriginReturnCue::hold: break;
        }
        message = std::format(
            " ORIGIN STATION {:.0f} m | CLS {:+.0f} m/s | {} ",
            guidance->distance_metres,
            guidance->closing_speed_metres_per_second, cue);
      } else {
        message = " ORIGIN STATION GUIDANCE INVALID ";
      }
    } else if (m_system_render) {
      const auto guidance = m_system_flight
                                ? resolve_system_flight_guidance(
                                      m_local_system, *m_system_flight)
                                : std::expected<SystemFlightGuidance,
                                                SystemFlightError>{
                                      std::unexpected{
                                          SystemFlightError::invalid_state}};
      message = guidance && m_system_flight
                    ? format_system_flight_status(*m_system_flight, *guidance)
                          .message
                    : " SYSTEM GUIDANCE INVALID ";
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
                    (m_signal_run && m_signal_run->flight
                         ? m_signal_run->flight->mode
                         : m_signal_scenario
                         ? m_signal_scenario->flight.mode
                         : m_system_flight
                         ? m_system_flight->mode
                         : m_origin_return
                         ? m_origin_return->mode
                         : (m_intersystem_planetfall
                                ? m_intersystem_planetfall->flight.mode
                            : (m_planetary_flight
                                   ? m_planetary_flight->mode
                                   : m_flight.mode))) ==
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
    if (m_intersystem_contract) {
      if (m_system_flight) {
        if (key.key == Key::Enter && key.action == KeyAction::Press) {
          const auto guidance = resolve_system_flight_guidance(
              m_local_system, *m_system_flight);
          auto orbital =
              insert_system_flight_orbit(m_local_system, *m_system_flight);
          if (!orbital) {
            m_error = guidance
                          ? format_system_flight_status(*m_system_flight,
                                                        *guidance)
                                .insertion_refusal
                          : " INSERT BLOCKED | GUIDANCE INVALID ";
            return;
          }
          auto next_contract = *m_intersystem_contract;
          if (!advance_intersystem_contract(
                  next_contract, next_contract.universe_tick,
                  IntersystemContractCommand::enter_target_planet)) {
            m_error =
                "orbit insertion is unavailable in the current mission phase";
            return;
          }
          const auto body = find_local_system_planet(
              m_local_system, next_contract.identities.target_planet);
          auto cache = TerrainTileCache::create();
          if (!body || !cache) {
            m_error = "target Planetfall terrain is unavailable";
            return;
          }
          auto planetfall = initialize_intersystem_planetfall(
              (*body)->descriptor, next_contract.identities.target_objective,
              *orbital, m_intersystem_world_deltas, *cache);
          if (!planetfall) {
            m_error = "target Planetfall initialization was rejected";
            return;
          }
          m_intersystem_contract = std::move(next_contract);
          m_intersystem_planetfall_cache.emplace(std::move(*cache));
          m_intersystem_planetfall.emplace(std::move(*planetfall));
          m_system_flight.reset();
          m_error.clear();
          m_input_mapper.suspend({}, m_intersystem_contract->universe_tick);
          return;
        }
        if (key.action == KeyAction::Press && key.key == Key::Char &&
            (key.ch == U'j' || key.ch == U'J')) {
          if (m_intersystem_contract->travel_phase ==
              IntersystemTravelPhase::return_jump_spooling) {
            if (!cancel_intersystem_jump(*m_intersystem_contract)) {
              m_error = "return jump cancellation was rejected";
              return;
            }
            m_system_flight->tick = m_intersystem_contract->universe_tick;
            m_system_flight->controls = {};
            m_error.clear();
          } else if (m_intersystem_contract->mission_phase ==
                         IntersystemMissionPhase::objective_complete &&
                     begin_intersystem_jump(*m_intersystem_contract)) {
            m_system_flight->controls = {};
            m_error.clear();
            m_input_mapper.suspend({},
                                   m_intersystem_contract->universe_tick);
          } else {
            m_error = "complete the planet objective before the return jump";
          }
          return;
        }
        if (key.action != KeyAction::Release) m_error.clear();
        m_input_mapper.enqueue(key, m_system_flight->tick, true);
        return;
      }
      if (m_intersystem_planetfall) {
        if (key.key == Key::Enter && key.action == KeyAction::Press &&
            m_intersystem_planetfall->flight.regime == FlightRegime::orbital) {
          auto departed = depart_planetary_orbit(
              m_local_system, m_intersystem_planetfall->flight);
          auto next_contract = *m_intersystem_contract;
          if (!departed ||
              !advance_intersystem_contract(
                  next_contract, next_contract.universe_tick,
                  IntersystemContractCommand::leave_target_planet)) {
            m_error = "planet departure requires a valid orbital state";
            return;
          }
          m_intersystem_world_deltas.assign(
              m_intersystem_planetfall->journal.entries().begin(),
              m_intersystem_planetfall->journal.entries().end());
          m_intersystem_contract = std::move(next_contract);
          m_system_flight = std::move(*departed);
          m_intersystem_planetfall.reset();
          m_intersystem_planetfall_cache.reset();
          m_input_mapper.suspend({}, m_system_flight->tick);
          return;
        }
        // The briefing binds one immutable objective; target-selection keys
        // cannot retarget the intersystem mission.
        if (apsis_drift::detail::signal_selection_command(key)) return;
        m_input_mapper.enqueue(key,
                               m_intersystem_planetfall->flight.tick);
        return;
      }
      if (m_origin_return) {
        if (key.key == Key::Enter && key.action == KeyAction::Press) {
          const auto guidance = resolve_origin_return_guidance(
              *m_intersystem_contract, *m_origin_return);
          auto next_contract = *m_intersystem_contract;
          if (!guidance || !guidance->arrived ||
              !advance_intersystem_contract(
                  next_contract, next_contract.universe_tick,
                  IntersystemContractCommand::dock_at_origin)) {
            m_error = "reach the Origin Station rendezvous before docking";
            return;
          }
          m_intersystem_contract = std::move(next_contract);
          m_origin_return.reset();
          (void)m_session.dock_at_station();
          m_simulation_clock.reset();
          m_active_mouse_region = {};
          return;
        }
        m_input_mapper.enqueue(key, m_origin_return->tick, true);
        return;
      }
      const bool jump_key = key.action == KeyAction::Press &&
                            key.key == Key::Char &&
                            (key.ch == U'j' || key.ch == U'J');
      if (!jump_key) {
        if (m_intersystem_contract->travel_phase ==
                IntersystemTravelPhase::outbound_jump_spooling &&
            m_intersystem_contract->rule_profile ==
                IntersystemRuleProfile::pilot) {
          m_input_mapper.enqueue(key,
                                 m_intersystem_contract->universe_tick);
        }
        return;
      }
      const bool spooling =
          m_intersystem_contract->travel_phase ==
              IntersystemTravelPhase::outbound_jump_spooling ||
          m_intersystem_contract->travel_phase ==
              IntersystemTravelPhase::return_jump_spooling;
      const auto changed = spooling
                               ? cancel_intersystem_jump(*m_intersystem_contract)
                               : begin_intersystem_jump(*m_intersystem_contract);
      if (!changed) {
        m_error = "jump command refused in the current state";
      } else {
        m_error.clear();
        m_input_mapper.suspend({}, m_intersystem_contract->universe_tick);
      }
      return;
    }
    if (m_signal_run && m_signal_run->flight) {
      if (key.key == Key::Enter && key.action == KeyAction::Press &&
          m_signal_run->onboarding.first_objective ==
              FirstObjectiveStatus::completed) {
        if (return_signal_run_to_origin(*m_signal_run)) {
          (void)m_session.dock_at_station();
          m_simulation_clock.reset();
          m_active_mouse_region = {};
        } else {
          m_error = "reach the Origin Station rendezvous in orbit first";
        }
        return;
      }
      if (apsis_drift::detail::signal_selection_command(key)) {
        // The first bounded objective is selected by the briefing and cannot
        // be silently retargeted in flight.
        return;
      }
      m_input_mapper.enqueue(key, m_signal_run->flight->tick);
      return;
    }
    if (m_signal_scenario) {
      const auto command =
          apsis_drift::detail::signal_selection_command(key);
      if (!command) {
        m_input_mapper.enqueue(key, m_signal_scenario->flight.tick);
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
      if (m_intersystem_contract) {
        const auto phase = m_intersystem_contract->travel_phase;
        const bool jumping =
            phase == IntersystemTravelPhase::outbound_jump_spooling ||
            phase == IntersystemTravelPhase::outbound_jump_committed ||
            phase == IntersystemTravelPhase::return_jump_spooling ||
            phase == IntersystemTravelPhase::return_jump_committed;
        if (jumping) {
          const bool outbound =
              phase == IntersystemTravelPhase::outbound_jump_spooling ||
              phase == IntersystemTravelPhase::outbound_jump_committed;
          const auto commands =
              outbound && phase ==
                              IntersystemTravelPhase::outbound_jump_spooling &&
                      m_intersystem_contract->rule_profile ==
                          IntersystemRuleProfile::pilot
                  ? m_input_mapper.take_commands(
                        m_intersystem_contract->universe_tick)
                  : std::vector<FlightCommand>{};
          const auto advanced = advance_intersystem_jump_tick(
              *m_intersystem_contract,
              outbound ? m_local_system : m_origin_system, commands);
          if (!advanced) {
            throw std::runtime_error{"intersystem jump simulation failed"};
          }
          if (advanced->arrived && outbound) {
            if (!m_intersystem_contract->arrival_solution) {
              throw std::runtime_error{"intersystem arrival state is unavailable"};
            }
            auto flight = initial_system_flight_state(
                m_local_system,
                m_intersystem_contract->identities.target_planet,
                *m_intersystem_contract->arrival_solution);
            if (!flight) {
              throw std::runtime_error{"cannot initialize target-system flight"};
            }
            m_system_flight = std::move(*flight);
            m_input_mapper.suspend({}, m_system_flight->tick);
          } else if (advanced->arrived && !outbound) {
            auto returning = initialize_origin_return(*m_intersystem_contract);
            if (!returning) {
              throw std::runtime_error{
                  "cannot initialize Origin Station return flight"};
            }
            m_origin_return = std::move(*returning);
          }
          if (advanced->committed && !outbound) {
            m_system_flight.reset();
          }
          if (advanced->committed) {
            m_input_mapper.suspend({},
                                   m_intersystem_contract->universe_tick);
          }
        } else if (m_system_flight) {
          auto next_flight = *m_system_flight;
          auto next_contract = *m_intersystem_contract;
          auto commands = m_input_mapper.take_commands(next_flight.tick);
          const auto previous_tick = next_flight.tick;
          if (!advance_system_flight(m_local_system, next_flight, commands)) {
            throw std::runtime_error{"system-flight simulation failed"};
          }
          if (!advance_intersystem_time(next_contract,
                                        next_flight.tick - previous_tick)) {
            throw std::runtime_error{"system-flight clock failed"};
          }
          m_system_flight = std::move(next_flight);
          m_intersystem_contract = std::move(next_contract);
        } else if (m_intersystem_planetfall) {
          if (!m_intersystem_planetfall_cache) {
            throw std::runtime_error{
                "target Planetfall terrain cache is unavailable"};
          }
          auto next_planetfall = *m_intersystem_planetfall;
          auto commands = m_input_mapper.take_commands(
              next_planetfall.flight.tick);
          const auto advanced = advance_intersystem_planetfall(
              next_planetfall, *m_intersystem_planetfall_cache, commands,
              m_intersystem_contract->rule_profile);
          if (!advanced) {
            throw std::runtime_error{"target Planetfall simulation failed"};
          }
          auto next_contract = *m_intersystem_contract;
          if (!advance_intersystem_time(next_contract, 1)) {
            throw std::runtime_error{"target Planetfall clock failed"};
          }
          if (advanced->objective_completed &&
              !advance_intersystem_contract(
                  next_contract, next_contract.universe_tick,
                  IntersystemContractCommand::complete_objective)) {
            throw std::runtime_error{
                "target objective completion was rejected"};
          }
          if (advanced->objective_completed) {
            m_intersystem_world_deltas.assign(
                next_planetfall.journal.entries().begin(),
                next_planetfall.journal.entries().end());
          }
          m_intersystem_planetfall = std::move(next_planetfall);
          m_intersystem_contract = std::move(next_contract);
        } else if (m_origin_return) {
          auto next_return = *m_origin_return;
          auto next_contract = *m_intersystem_contract;
          auto commands = m_input_mapper.take_commands(next_return.tick);
          if (!advance_origin_return(next_contract, next_return, commands) ||
              !advance_intersystem_time(next_contract, 1)) {
            throw std::runtime_error{"Origin Station return flight failed"};
          }
          m_origin_return = std::move(next_return);
          m_intersystem_contract = std::move(next_contract);
        } else if (!advance_intersystem_time(*m_intersystem_contract, 1)) {
          throw std::runtime_error{"universe clock failed"};
        }
        continue;
      }
      if (m_signal_run && m_signal_run->flight) {
        if (!m_signal_run_cache) {
          throw std::runtime_error{"Signal Run cache is unavailable"};
        }
        auto commands =
            m_input_mapper.take_commands(m_signal_run->flight->tick);
        const auto advanced = advance_signal_run(
            *m_signal_run, *m_signal_run_cache, commands);
        if (!advanced) {
          const auto& flight = *m_signal_run->flight;
          throw std::runtime_error{std::format(
              "Signal Run simulation failed: {} at tick {} "
              "(regime={}, latitude={:.12f}, longitude={:.12f}, "
              "altitude={:.3f} m, clearance={:.3f} m, "
              "velocity=({:.3f}, {:.3f}, {:.3f}) m/s)",
              signal_run_error_name(advanced.error()), flight.tick,
              flight_regime_name(flight.regime),
              flight.pose.position.latitude_radians,
              flight.pose.position.longitude_radians,
              flight.pose.position.altitude_metres, flight.clearance_metres,
              flight.velocity.east_metres_per_second,
              flight.velocity.north_metres_per_second,
              flight.velocity.up_metres_per_second)};
        }
        continue;
      }
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
    m_system_render.reset();
    if (m_intersystem_contract) {
      if (const auto jump =
              intersystem_jump_snapshot(*m_intersystem_contract)) {
        if (!render_intersystem_jump(
                *jump, m_render_configuration.viewport.width,
                m_render_configuration.viewport.height, m_surface.pixels())) {
          throw std::runtime_error{"jump presentation rejected frame"};
        }
        return;
      }
      if (m_intersystem_planetfall) {
        m_system_render.reset();
        if (!m_planetary_renderer) {
          throw std::runtime_error{"target planetary presentation is unavailable"};
        }
        const auto rendered = m_planetary_renderer->render(
            *m_intersystem_planetfall->planet,
            m_intersystem_planetfall->flight,
            {.pitch_radians = -0.18}, m_surface.pixels());
        if (!rendered) {
          throw std::runtime_error{"target orbital presentation rejected frame"};
        }
        m_planetary_samples.push_back(*rendered);
        return;
      }
      const bool at_target =
          m_intersystem_contract->current_system ==
          m_intersystem_contract->identities.target_system;
      const auto& system = at_target ? m_local_system : m_origin_system;
      SystemPositionMetres camera_position{0.0, -92'000'000'000.0,
                                            26'000'000'000.0};
      SystemVelocityMetresPerSecond camera_velocity{};
      if (m_intersystem_contract->arrival_solution &&
          m_intersystem_contract->arrival_solution->destination == system.id) {
        camera_position =
            m_intersystem_contract->arrival_solution->position;
        camera_velocity =
            m_intersystem_contract->arrival_solution->velocity;
      }
      SystemDirection forward{};
      SystemDirection up{0.0, 0.0, 1.0};
      if (m_system_flight && m_system_flight->system == system.id) {
        camera_position = m_system_flight->position;
        camera_velocity = m_system_flight->velocity;
        forward = m_system_flight->forward;
        up = m_system_flight->up;
      } else if (m_origin_return && m_origin_return->system == system.id) {
        camera_position = m_origin_return->position;
        camera_velocity = m_origin_return->velocity;
        forward = m_origin_return->forward;
        up = m_origin_return->up;
      }
      const PlanetId selected =
          at_target ? m_intersystem_contract->identities.target_planet
                    : system.planets.front().descriptor.id;
      const auto selected_ephemeris = resolve_planet_ephemeris(
          system, selected,
          {.tick = m_intersystem_contract->universe_tick,
           .sub_tick_fraction = 0.0});
      if (!selected_ephemeris) {
        throw std::runtime_error{"cannot resolve intersystem target"};
      }
      const LocalSystemView view{
          .time = {m_intersystem_contract->universe_tick, 0.0},
          .position = camera_position,
          .velocity = camera_velocity,
          .forward = (m_system_flight || m_origin_return)
                         ? forward
                         : SystemDirection{
                               selected_ephemeris->position.x - camera_position.x,
                               selected_ephemeris->position.y - camera_position.y,
                               selected_ephemeris->position.z - camera_position.z},
          .up = up,
          .selected_planet = selected,
      };
      if (!m_system_renderer) {
        throw std::runtime_error{"intersystem presentation is unavailable"};
      }
      const auto rendered =
          m_system_renderer->render(system, view, m_surface.pixels());
      if (!rendered) {
        throw std::runtime_error{"intersystem presentation rejected frame"};
      }
      m_system_render = *rendered;
      if (m_origin_return &&
          !render_origin_station_marker(
              m_render_configuration.viewport.width,
              m_render_configuration.viewport.height, m_surface.pixels())) {
        throw std::runtime_error{"origin station marker rejected frame"};
      }
      return;
    }
    if (m_signal_run && m_signal_run->flight) {
      if (!m_planetary_renderer) {
        throw std::runtime_error{"Signal Run presentation is unavailable"};
      }
      const auto rendered = m_planetary_renderer->render(
          *m_signal_run->planet, *m_signal_run->flight,
          {.pitch_radians = -0.18}, m_surface.pixels());
      if (!rendered) {
        throw std::runtime_error{"Signal Run presentation rejected frame"};
      }
      m_planetary_samples.push_back(*rendered);
      return;
    }
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
          .thermal = {},
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
    if (m_workload == BenchmarkWorkload::system) {
      const SystemPositionMetres camera_position{0.0, -92'000'000'000.0,
                                                  26'000'000'000.0};
      const LocalSystemView view{
          .time = {m_flight.tick, 0.0},
          .position = camera_position,
          .velocity = {},
          .forward = {-camera_position.x, -camera_position.y,
                      -camera_position.z},
          .up = {0.0, 0.0, 1.0},
          .selected_planet = m_local_system.planets.front().descriptor.id,
      };
      if (!m_system_renderer) {
        throw std::runtime_error{"local-system renderer is unavailable"};
      }
      const auto rendered = m_system_renderer->render(
          m_local_system, view, m_surface.pixels());
      if (!rendered) {
        throw std::runtime_error{std::format(
            "local-system renderer rejected the {}x{} surface ({})",
            m_render_configuration.viewport.width,
            m_render_configuration.viewport.height,
            static_cast<unsigned>(rendered.error()))};
      }
      m_system_render = *rendered;
      return;
    }
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
      const auto sun = resolve_local_sun(m_planet, m_flight.tick);
      const auto rendered = sun
                                ? m_orbital_renderer.render(
                                      m_planet, camera, sun->planet_to_sun,
                                      m_surface.pixels())
                                : std::expected<OrbitalRenderStats,
                                                OrbitalRenderError>{
                                      std::unexpected{
                                          OrbitalRenderError::invalid_light_direction}};
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
  LocalSystemDescriptor m_origin_system;
  LocalSystemDescriptor m_local_system;
  VoxelRenderer m_renderer;
  OrbitalRenderer m_orbital_renderer;
  std::optional<LocalSystemRenderer> m_system_renderer;
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
  std::optional<SystemFlightState> m_system_flight;
  std::optional<OriginReturnState> m_origin_return;
  std::optional<TerrainTileCache> m_intersystem_planetfall_cache;
  std::optional<IntersystemPlanetfallState> m_intersystem_planetfall;
  std::optional<LocalSystemRenderStats> m_system_render;
  std::optional<TerrainTileCache> m_signal_cache;
  std::optional<SignalNavigationAcceptanceState> m_signal_scenario;
  std::optional<TerrainTileCache> m_signal_run_cache;
  std::optional<SignalRunState> m_signal_run;
  std::optional<SaveDocument> m_save_profile;
  std::optional<IntersystemContractState> m_intersystem_contract;
  std::vector<SaveWorldDelta> m_intersystem_world_deltas;
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
                                BenchmarkWorkload workload,
                                std::string_view presentation)
    -> std::string {
  const std::string_view workload_prefix =
      workload == BenchmarkWorkload::orbital
          ? "orbital-planet"
          : (workload == BenchmarkWorkload::planetary
                 ? "planetary-presentation"
                 : (workload == BenchmarkWorkload::system
                        ? "local-system"
                        : "voxel-landscape"));
  std::string planetary_presentation_json;
  if (summary.planetary_presentation) {
    const auto& value = *summary.planetary_presentation;
    planetary_presentation_json = std::format(
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
      "  \"presentation\": \"{}\",\n"
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
      configuration.viewport.height, presentation,
      profile_name(configuration), configuration.viewport.width,
      configuration.viewport.height,
      summary.frames, summary.elapsed_seconds, summary.achieved_fps,
      summary.render_avg_ms, summary.render_p95_ms, summary.work_avg_ms,
      summary.work_p95_ms, summary.bytes_per_frame,
      summary.mebibytes_per_second, summary.total_bytes, summary.checksum,
      planetary_presentation_json);
}

inline constexpr std::uint32_t kSystemNavigationAcceptanceSeed{42};

[[nodiscard]] auto system_navigation_acceptance_json(
    const BenchmarkSummary& summary,
    const RenderConfiguration& configuration,
    const LocalSystemDescriptor& system,
    const LocalSystemRenderStats& render,
    std::string_view presentation) -> std::string {
  return std::format(
      "{{\n"
      "  \"schema_version\": 1,\n"
      "  \"scenario\": \"v0.4.6-local-system-navigation\",\n"
      "  \"presentation\": \"{}\",\n"
      "  \"system_id\": \"{}\",\n"
      "  \"star_id\": \"{}\",\n"
      "  \"target_planet_id\": \"{}\",\n"
      "  \"target_name\": \"{}\",\n"
      "  \"distance_metres\": {:.3f},\n"
      "  \"closing_speed_metres_per_second\": {:.3f},\n"
      "  \"visible_planets\": {},\n"
      "  \"selected_visible\": {},\n"
      "  \"benchmark\": {}"
      "}}\n",
      presentation, system.id.value, system.star.id.value,
      render.navigation.target.value, render.navigation.display_name,
      render.navigation.distance_metres,
      render.navigation.closing_speed_metres_per_second,
      render.visible_planets, render.selected_visible,
      summary_json(summary, configuration, BenchmarkWorkload::system,
                   presentation));
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
      "Usage: apsis-drift --version\n"
      "       apsis-drift [--seed N] [--profile NAME] "
      "[--viewport WIDTHxHEIGHT]\n"
      "                   [--load PATH | --new-game-seed N] [--save PATH]\n"
      "       apsis-drift [--driver automatic|kitty|ansi|fallback]\n"
      "                   [--keyboard enhanced|press-only]\n"
      "       apsis-drift --benchmark [FRAMES] [--seed N] [--report PATH]\n"
      "                   [--driver automatic|kitty|ansi|fallback]\n"
      "       apsis-drift --sweep [FRAMES] --report PATH\n"
      "                   [--sweep-viewports LIST] [--sweep-fps LIST]\n"
      "                   [--workload landscape|orbital|planetary|system]\n"
      "       apsis-drift --capture-seconds N [--seed N] --report PATH\n\n"
      "       apsis-drift --flight-deck-acceptance --report PATH\n"
      "                   [--driver kitty|ansi] [--profile NAME]\n\n"
      "       apsis-drift --planetfall-acceptance --report PATH\n"
      "                   [--profile NAME] [--snapshot PATH]\n\n"
      "       apsis-drift --signal-navigation-acceptance --report PATH\n"
      "                   [--driver kitty|ansi] [--profile NAME]\n\n"
      "       apsis-drift --signal-run-acceptance --report PATH\n"
      "                   --driver kitty|ansi [--profile NAME]\n\n"
      "       apsis-drift --system-navigation-acceptance --report PATH\n"
      "                   --driver kitty|ansi [--profile NAME]\n\n"
      "       apsis-drift --intersystem-jump-acceptance --report PATH\n"
      "                   --driver kitty|ansi [--profile NAME]\n\n"
      "       apsis-drift --system-flight-acceptance --report PATH\n"
      "                   --driver kitty|ansi [--profile NAME]\n\n"
      "       apsis-drift --intersystem-planetfall-acceptance --report PATH\n"
      "                   --driver kitty|ansi [--profile NAME]\n\n"
      "       apsis-drift --intersystem-return-acceptance --report PATH\n"
      "                   --driver kitty|ansi [--profile NAME] [--snapshot PATH]\n\n"
      "       apsis-drift --intersystem-contract-acceptance --report PATH\n"
      "                   --driver kitty|ansi [--profile NAME] [--snapshot PATH]\n\n"
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
  if (argc == 2 && std::string_view{argv[1]} == "--version") {
    std::printf("apsis-drift %.*s\n",
                static_cast<int>(kApplicationVersion.size()),
                kApplicationVersion.data());
    return 0;
  }

  std::optional<int> benchmark_frames;
  std::optional<int> sweep_frames;
  int capture_seconds = 0;
  bool flight_deck_acceptance{};
  bool planetfall_acceptance{};
  bool signal_navigation_acceptance{};
  bool signal_run_acceptance{};
  bool system_navigation_acceptance{};
  bool intersystem_jump_acceptance{};
  bool system_flight_acceptance{};
  bool intersystem_planetfall_acceptance{};
  bool intersystem_return_acceptance{};
  bool intersystem_contract_acceptance{};
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
    if (argument == "--signal-run-acceptance") {
      signal_run_acceptance = true;
      continue;
    }
    if (argument == "--system-navigation-acceptance") {
      system_navigation_acceptance = true;
      continue;
    }
    if (argument == "--intersystem-jump-acceptance") {
      intersystem_jump_acceptance = true;
      continue;
    }
    if (argument == "--system-flight-acceptance") {
      system_flight_acceptance = true;
      continue;
    }
    if (argument == "--intersystem-planetfall-acceptance") {
      intersystem_planetfall_acceptance = true;
      continue;
    }
    if (argument == "--intersystem-return-acceptance") {
      intersystem_return_acceptance = true;
      continue;
    }
    if (argument == "--intersystem-contract-acceptance") {
      intersystem_contract_acceptance = true;
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

  const bool profile_options = !load_path.empty() || !save_path.empty() ||
                               new_game_seed.has_value();
  if (intersystem_contract_acceptance &&
      (benchmark_frames || sweep_frames || capture_seconds > 0 ||
       flight_deck_acceptance || planetfall_acceptance ||
       signal_navigation_acceptance || signal_run_acceptance ||
       system_navigation_acceptance || intersystem_jump_acceptance ||
       system_flight_acceptance || intersystem_planetfall_acceptance ||
       intersystem_return_acceptance || profile_options || seed_specified ||
       workload_specified || keyboard_specified)) {
    std::fprintf(stderr,
                 "Intersystem contract acceptance is mutually exclusive with "
                 "other run, save, seed, workload, and keyboard options\n");
    return 2;
  }
  if (intersystem_contract_acceptance &&
      (!driver_specified ||
       (driver_choice != DriverChoice::kitty &&
        driver_choice != DriverChoice::ansi))) {
    std::fprintf(stderr,
                 "Intersystem contract acceptance requires --driver kitty "
                 "or --driver ansi\n");
    return 2;
  }
  if (intersystem_contract_acceptance && report_path.empty()) {
    std::fprintf(stderr,
                 "Intersystem contract acceptance requires --report PATH\n");
    return 2;
  }
  if (intersystem_return_acceptance &&
      (benchmark_frames || sweep_frames || capture_seconds > 0 ||
       flight_deck_acceptance || planetfall_acceptance ||
       signal_navigation_acceptance || signal_run_acceptance ||
       system_navigation_acceptance || intersystem_jump_acceptance ||
       system_flight_acceptance || intersystem_planetfall_acceptance ||
       profile_options || seed_specified || workload_specified ||
       keyboard_specified)) {
    std::fprintf(stderr,
                 "Intersystem return acceptance is mutually exclusive with "
                 "other run, save, seed, workload, and keyboard options\n");
    return 2;
  }
  if (intersystem_return_acceptance &&
      (!driver_specified ||
       (driver_choice != DriverChoice::kitty &&
        driver_choice != DriverChoice::ansi))) {
    std::fprintf(stderr,
                 "Intersystem return acceptance requires --driver kitty "
                 "or --driver ansi\n");
    return 2;
  }
  if (intersystem_return_acceptance && report_path.empty()) {
    std::fprintf(stderr,
                 "Intersystem return acceptance requires --report PATH\n");
    return 2;
  }
  if (intersystem_planetfall_acceptance &&
      (benchmark_frames || sweep_frames || capture_seconds > 0 ||
       flight_deck_acceptance || planetfall_acceptance ||
       signal_navigation_acceptance || signal_run_acceptance ||
       system_navigation_acceptance || intersystem_jump_acceptance ||
       system_flight_acceptance || profile_options || seed_specified ||
       workload_specified || keyboard_specified)) {
    std::fprintf(stderr,
                 "Intersystem Planetfall acceptance is mutually exclusive "
                 "with other run, save, seed, workload, and keyboard options\n");
    return 2;
  }
  if (intersystem_planetfall_acceptance &&
      (!driver_specified ||
       (driver_choice != DriverChoice::kitty &&
        driver_choice != DriverChoice::ansi))) {
    std::fprintf(stderr,
                 "Intersystem Planetfall acceptance requires --driver kitty "
                 "or --driver ansi\n");
    return 2;
  }
  if (intersystem_planetfall_acceptance && report_path.empty()) {
    std::fprintf(stderr,
                 "Intersystem Planetfall acceptance requires --report PATH\n");
    return 2;
  }
  if (system_flight_acceptance &&
      (benchmark_frames || sweep_frames || capture_seconds > 0 ||
       flight_deck_acceptance || planetfall_acceptance ||
       signal_navigation_acceptance || signal_run_acceptance ||
       system_navigation_acceptance || intersystem_jump_acceptance ||
       profile_options || seed_specified || workload_specified ||
       keyboard_specified)) {
    std::fprintf(stderr,
                 "System flight acceptance is mutually exclusive with "
                 "other run, save, seed, workload, and keyboard options\n");
    return 2;
  }
  if (system_flight_acceptance &&
      (!driver_specified ||
       (driver_choice != DriverChoice::kitty &&
        driver_choice != DriverChoice::ansi))) {
    std::fprintf(stderr,
                 "System flight acceptance requires --driver kitty or "
                 "--driver ansi\n");
    return 2;
  }
  if (system_flight_acceptance && report_path.empty()) {
    std::fprintf(stderr,
                 "System flight acceptance requires --report PATH\n");
    return 2;
  }
  if (intersystem_jump_acceptance &&
      (benchmark_frames || sweep_frames || capture_seconds > 0 ||
       flight_deck_acceptance || planetfall_acceptance ||
       signal_navigation_acceptance || signal_run_acceptance ||
       system_navigation_acceptance || profile_options || seed_specified ||
       workload_specified || keyboard_specified)) {
    std::fprintf(stderr,
                 "Intersystem jump acceptance is mutually exclusive with "
                 "other run, save, seed, workload, and keyboard options\n");
    return 2;
  }
  if (intersystem_jump_acceptance &&
      (!driver_specified ||
       (driver_choice != DriverChoice::kitty &&
        driver_choice != DriverChoice::ansi))) {
    std::fprintf(stderr,
                 "Intersystem jump acceptance requires --driver kitty or "
                 "--driver ansi\n");
    return 2;
  }
  if (intersystem_jump_acceptance && report_path.empty()) {
    std::fprintf(stderr,
                 "Intersystem jump acceptance requires --report PATH\n");
    return 2;
  }
  if (benchmark_frames && capture_seconds > 0) {
    std::fprintf(stderr, "benchmark and capture modes are mutually exclusive\n");
    return 2;
  }
  if (system_navigation_acceptance &&
      (benchmark_frames || sweep_frames || capture_seconds > 0 ||
       flight_deck_acceptance || planetfall_acceptance ||
       signal_navigation_acceptance || signal_run_acceptance ||
       profile_options || seed_specified || workload_specified ||
       keyboard_specified)) {
    std::fprintf(stderr,
                 "System navigation acceptance is mutually exclusive with "
                 "other run, save, seed, workload, and keyboard options\n");
    return 2;
  }
  if (system_navigation_acceptance &&
      (!driver_specified ||
       (driver_choice != DriverChoice::kitty &&
        driver_choice != DriverChoice::ansi))) {
    std::fprintf(stderr,
                 "System navigation acceptance requires --driver kitty or "
                 "--driver ansi\n");
    return 2;
  }
  if (system_navigation_acceptance && report_path.empty()) {
    std::fprintf(stderr,
                 "System navigation acceptance requires --report PATH\n");
    return 2;
  }
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
       signal_navigation_acceptance || signal_run_acceptance)) {
    std::fprintf(stderr,
                 "save-profile options are available only for interactive "
                 "runs\n");
    return 2;
  }
  if (flight_deck_acceptance &&
      (benchmark_frames || sweep_frames || capture_seconds > 0 ||
       signal_navigation_acceptance || signal_run_acceptance)) {
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
  if (signal_run_acceptance && seed_specified) {
    std::fprintf(stderr,
                 "Signal Run acceptance uses the fixed seed %u\n",
                 kSignalRunAcceptanceSeed);
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
  if (signal_run_acceptance &&
      (benchmark_frames || sweep_frames || capture_seconds > 0 ||
       flight_deck_acceptance || planetfall_acceptance ||
       signal_navigation_acceptance)) {
    std::fprintf(stderr,
                 "Signal Run acceptance is mutually exclusive with other "
                 "run modes\n");
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
  if (signal_run_acceptance && report_path.empty()) {
    std::fprintf(stderr,
                 "Signal Run acceptance mode requires --report PATH\n");
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
  if (signal_run_acceptance &&
      (!driver_specified ||
       (driver_choice != DriverChoice::kitty &&
        driver_choice != DriverChoice::ansi) ||
       keyboard_specified || workload_specified)) {
    std::fprintf(stderr,
                 "Signal Run acceptance requires --driver kitty or ansi and "
                 "does not accept keyboard or workload options\n");
    return 2;
  }

  try {
    if (intersystem_contract_acceptance) {
      const RenderConfiguration configuration =
          resolve_render_configuration(selected_profile, viewport_override);
      const auto acceptance = run_intersystem_contract_acceptance(
          configuration.viewport.width, configuration.viewport.height);
      if (!acceptance) {
        std::fprintf(stderr,
                     "Intersystem contract acceptance failed (%u)\n",
                     static_cast<unsigned>(acceptance.error()));
        return 1;
      }
      const std::string_view presentation =
          driver_choice == DriverChoice::kitty ? "kitty" : "ansi";
      std::ofstream report{report_path};
      if (!report) {
        std::fprintf(stderr, "cannot open report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
      report << intersystem_contract_acceptance_json(acceptance->report,
                                                      presentation);
      if (!report.good()) {
        std::fprintf(stderr, "cannot write report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
      if (!snapshot_path.empty() &&
          !::write_snapshot(snapshot_path, configuration.viewport,
                            acceptance->final_frame)) {
        std::fprintf(stderr, "cannot write snapshot '%s'\n",
                     snapshot_path.string().c_str());
        return 1;
      }
      std::printf(
          "intersystem-contract: presentation=%.*s final=%llu checksum=%llu\n",
          static_cast<int>(presentation.size()), presentation.data(),
          static_cast<unsigned long long>(acceptance->report.final_tick),
          static_cast<unsigned long long>(
              acceptance->report.final_authoritative_checksum));
      return 0;
    }
    if (intersystem_return_acceptance) {
      const RenderConfiguration configuration =
          resolve_render_configuration(selected_profile, viewport_override);
      const auto acceptance = run_intersystem_return_acceptance(
          configuration.viewport.width, configuration.viewport.height);
      if (!acceptance) {
        std::fprintf(stderr, "Intersystem return acceptance failed (%u)\n",
                     static_cast<unsigned>(acceptance.error()));
        return 1;
      }
      const std::string_view presentation =
          driver_choice == DriverChoice::kitty ? "kitty" : "ansi";
      std::ofstream report{report_path};
      if (!report) {
        std::fprintf(stderr, "cannot open report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
      report << intersystem_return_acceptance_json(acceptance->report,
                                                    presentation);
      if (!report.good()) {
        std::fprintf(stderr, "cannot write report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
      if (!snapshot_path.empty() &&
          !::write_snapshot(snapshot_path, configuration.viewport,
                            acceptance->final_frame)) {
        std::fprintf(stderr, "cannot write snapshot '%s'\n",
                     snapshot_path.string().c_str());
        return 1;
      }
      std::printf(
          "intersystem-return: presentation=%.*s docking=%llu checksum=%llu\n",
          static_cast<int>(presentation.size()), presentation.data(),
          static_cast<unsigned long long>(acceptance->report.docking_tick),
          static_cast<unsigned long long>(
              acceptance->report.docked_return_checksum));
      return 0;
    }
    if (intersystem_planetfall_acceptance) {
      const RenderConfiguration configuration =
          resolve_render_configuration(selected_profile, viewport_override);
      const auto acceptance = run_intersystem_planetfall_acceptance(
          configuration.viewport.width, configuration.viewport.height);
      if (!acceptance) {
        std::fprintf(stderr,
                     "Intersystem Planetfall acceptance failed (%u)\n",
                     static_cast<unsigned>(acceptance.error()));
        return 1;
      }
      const std::string_view presentation =
          driver_choice == DriverChoice::kitty ? "kitty" : "ansi";
      std::ofstream report{report_path};
      if (!report) {
        std::fprintf(stderr, "cannot open report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
      report << intersystem_planetfall_acceptance_json(acceptance->report,
                                                       presentation);
      if (!report.good()) {
        std::fprintf(stderr, "cannot write report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
      if (!snapshot_path.empty() &&
          !::write_snapshot(snapshot_path, configuration.viewport,
                            acceptance->final_frame)) {
        std::fprintf(stderr, "cannot write snapshot '%s'\n",
                     snapshot_path.string().c_str());
        return 1;
      }
      std::printf(
          "intersystem-planetfall: presentation=%.*s completion=%llu "
          "checksum=%llu\n",
          static_cast<int>(presentation.size()), presentation.data(),
          static_cast<unsigned long long>(acceptance->report.completion_tick),
          static_cast<unsigned long long>(
              acceptance->report.completed_flight_checksum));
      return 0;
    }
    if (system_flight_acceptance) {
      const RenderConfiguration configuration =
          resolve_render_configuration(selected_profile, viewport_override);
      const auto acceptance = run_system_flight_acceptance(
          configuration.viewport.width, configuration.viewport.height);
      if (!acceptance) {
        std::fprintf(stderr, "System flight acceptance failed (%u)\n",
                     static_cast<unsigned>(acceptance.error()));
        return 1;
      }
      const std::string_view presentation =
          driver_choice == DriverChoice::kitty ? "kitty" : "ansi";
      std::ofstream report{report_path};
      if (!report) {
        std::fprintf(stderr, "cannot open report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
      report << system_flight_acceptance_json(acceptance->report,
                                              presentation);
      if (!report.good()) {
        std::fprintf(stderr, "cannot write report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
      if (!snapshot_path.empty() &&
          !::write_snapshot(snapshot_path, configuration.viewport,
                            acceptance->final_frame)) {
        std::fprintf(stderr, "cannot write snapshot '%s'\n",
                     snapshot_path.string().c_str());
        return 1;
      }
      std::printf(
          "system-flight: presentation=%.*s insertion=%llu checksum=%llu\n",
          static_cast<int>(presentation.size()), presentation.data(),
          static_cast<unsigned long long>(acceptance->report.insertion_tick),
          static_cast<unsigned long long>(
              acceptance->report.system_flight_checksum));
      return 0;
    }
    if (intersystem_jump_acceptance) {
      const RenderConfiguration configuration =
          resolve_render_configuration(selected_profile, viewport_override);
      const auto acceptance = run_intersystem_jump_acceptance(
          configuration.viewport.width, configuration.viewport.height);
      if (!acceptance) {
        std::fprintf(stderr, "Intersystem jump acceptance failed (%u)\n",
                     static_cast<unsigned>(acceptance.error()));
        return 1;
      }
      const std::string_view presentation =
          driver_choice == DriverChoice::kitty ? "kitty" : "ansi";
      std::ofstream report{report_path};
      if (!report) {
        std::fprintf(stderr, "cannot open report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
      report << intersystem_jump_acceptance_json(acceptance->report,
                                                 presentation);
      if (!report.good()) {
        std::fprintf(stderr, "cannot write report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
      if (!snapshot_path.empty() &&
          !::write_snapshot(snapshot_path, configuration.viewport,
                            acceptance->transit_frame)) {
        std::fprintf(stderr, "cannot write snapshot '%s'\n",
                     snapshot_path.string().c_str());
        return 1;
      }
      std::printf(
          "intersystem-jump: presentation=%.*s destination=%llu "
          "commit=%llu arrival=%llu checksum=%llu\n",
          static_cast<int>(presentation.size()), presentation.data(),
          static_cast<unsigned long long>(acceptance->report.destination.value),
          static_cast<unsigned long long>(acceptance->report.committed_tick),
          static_cast<unsigned long long>(acceptance->report.arrival_tick),
          static_cast<unsigned long long>(
              acceptance->report.arrival_checksum));
      return 0;
    }
    if (system_navigation_acceptance) {
      const RenderConfiguration configuration =
          resolve_render_configuration(selected_profile, viewport_override);
      LandscapeApp app{configuration, kSystemNavigationAcceptanceSeed,
                       BenchmarkWorkload::system};
      if (auto forced =
              app.force_capabilities(driver_choice, KeyboardChoice::enhanced);
          !forced) {
        std::fprintf(stderr, "cannot force capabilities: %s\n",
                     forced.error().message.c_str());
        return 2;
      }
      constexpr int acceptance_frames{6};
      app.benchmark(acceptance_frames, driver_choice);
      const auto summary = app.summary();
      const auto* render = app.system_render_stats();
      if (summary.frames != acceptance_frames || render == nullptr ||
          !render->selected_visible) {
        std::fprintf(stderr,
                     "System navigation acceptance ended before its final "
                     "selected-target frame\n");
        return 1;
      }
      const std::string_view requested_presentation =
          driver_choice == DriverChoice::kitty ? "kitty" : "ansi";
      if (app.display_tier() != requested_presentation) {
        std::fprintf(
            stderr,
            "System navigation acceptance requested %.*s but exercised %s\n",
            static_cast<int>(requested_presentation.size()),
            requested_presentation.data(), app.display_tier().c_str());
        return 1;
      }
      const std::string_view presentation = app.display_tier();
      std::ofstream report{report_path};
      if (!report) {
        std::fprintf(stderr, "cannot open report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
      report << system_navigation_acceptance_json(
          summary, configuration, app.local_system(), *render,
          presentation);
      if (!report.good()) {
        std::fprintf(stderr, "cannot write report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
      if (!snapshot_path.empty() && !app.write_snapshot(snapshot_path)) {
        std::fprintf(stderr, "cannot write snapshot '%s'\n",
                     snapshot_path.string().c_str());
        return 1;
      }
      std::printf(
          "system-navigation: seed=%u presentation=%.*s system=%llu "
          "target=%llu frames=%zu checksum=%llu\n",
          kSystemNavigationAcceptanceSeed,
          static_cast<int>(presentation.size()), presentation.data(),
          static_cast<unsigned long long>(app.local_system().id.value),
          static_cast<unsigned long long>(render->navigation.target.value),
          summary.frames,
          static_cast<unsigned long long>(summary.checksum));
      return 0;
    }
    if (signal_run_acceptance) {
      const RenderConfiguration configuration =
          resolve_render_configuration(selected_profile, viewport_override);
      const auto checkpoint_path =
          std::filesystem::temp_directory_path() /
          std::format("apsis-signal-run-{}-{}.json",
                      static_cast<long long>(::getpid()),
                      Clock::now().time_since_epoch().count());
      const std::string_view presentation =
          driver_choice == DriverChoice::kitty ? "kitty" : "ansi";
      const auto acceptance = run_signal_run_acceptance(
          configuration, checkpoint_path, presentation);
      if (!acceptance) {
        std::fprintf(stderr, "Signal Run acceptance failed (%u)\n",
                     static_cast<unsigned>(acceptance.error()));
        return 1;
      }
      std::ofstream report{report_path};
      if (!report) {
        std::fprintf(stderr, "cannot open report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
      report << signal_run_acceptance_json(acceptance->report);
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
          "signal-run: seed=%u presentation=%.*s atmosphere=%llu "
          "reached=%llu completion=%llu return=%llu checksum=%llu\n",
          kSignalRunAcceptanceSeed,
          static_cast<int>(presentation.size()), presentation.data(),
          static_cast<unsigned long long>(
              acceptance->report.atmospheric_tick),
          static_cast<unsigned long long>(acceptance->report.reached_tick),
          static_cast<unsigned long long>(
              acceptance->report.completion_tick),
          static_cast<unsigned long long>(
              acceptance->report.orbital_return_tick),
          static_cast<unsigned long long>(
              acceptance->report.return_flight_checksum));
      return 0;
    }
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
                     signal_navigation_acceptance,
                     save_profile ? &*save_profile : nullptr};
    if (auto forced =
            app.force_capabilities(driver_choice, keyboard_choice);
        !forced) {
      std::fprintf(stderr, "cannot force capabilities: %s\n",
                   forced.error().message.c_str());
      return 2;
    }
    int result{};
    if (benchmark_frames) {
      app.benchmark(*benchmark_frames, driver_choice);
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
                               app.workload(), app.display_tier());
      }
      if (!report.good()) {
        std::fprintf(stderr, "cannot write report '%s'\n",
                     report_path.string().c_str());
        return 1;
      }
    }
    if (!save_path.empty() && result == 0) {
      auto projected = app.signal_run_save();
      if (!projected) {
        std::fprintf(stderr,
                     "cannot project the live Signal Run into a save\n");
        return 1;
      }
      if (auto saved = write_save_file_atomically(save_path, *projected);
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
