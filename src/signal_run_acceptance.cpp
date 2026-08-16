#include "apsis_drift/signal_run_acceptance.hpp"

#include <array>
#include <cmath>
#include <format>
#include <span>
#include <system_error>
#include <utility>

#include "apsis_drift/planetary_presentation.hpp"
#include "apsis_drift/save_file.hpp"

namespace apsis_drift {
namespace {

struct Guidance {
  bool forward{};
  bool backward{};
  bool left{};
  bool right{};
  bool rise{};
  bool fall{};
};

class CheckpointCleanup {
 public:
  explicit CheckpointCleanup(std::filesystem::path path)
      : m_path{std::move(path)} {}
  CheckpointCleanup(const CheckpointCleanup&) = delete;
  auto operator=(const CheckpointCleanup&) -> CheckpointCleanup& = delete;
  ~CheckpointCleanup() {
    std::error_code ignored;
    std::filesystem::remove(m_path, ignored);
  }

 private:
  std::filesystem::path m_path;
};

auto command_if_changed(std::vector<FlightCommand>& commands,
                        SimulationTick tick, bool current, bool desired,
                        FlightCommandKind press,
                        FlightCommandKind release) -> void {
  if (current == desired) return;
  commands.push_back({tick, desired ? press : release});
}

[[nodiscard]] auto guidance_commands(const PlanetaryFlightState& flight,
                                     const Guidance& guidance)
    -> std::vector<FlightCommand> {
  std::vector<FlightCommand> commands;
  commands.reserve(6);
  command_if_changed(commands, flight.tick, flight.controls.forward,
                     guidance.forward, FlightCommandKind::press_forward,
                     FlightCommandKind::release_forward);
  command_if_changed(commands, flight.tick, flight.controls.backward,
                     guidance.backward, FlightCommandKind::press_backward,
                     FlightCommandKind::release_backward);
  command_if_changed(commands, flight.tick, flight.controls.turn_left,
                     guidance.left, FlightCommandKind::press_turn_left,
                     FlightCommandKind::release_turn_left);
  command_if_changed(commands, flight.tick, flight.controls.turn_right,
                     guidance.right, FlightCommandKind::press_turn_right,
                     FlightCommandKind::release_turn_right);
  command_if_changed(commands, flight.tick, flight.controls.rise,
                     guidance.rise, FlightCommandKind::press_rise,
                     FlightCommandKind::release_rise);
  command_if_changed(commands, flight.tick, flight.controls.fall,
                     guidance.fall, FlightCommandKind::press_fall,
                     FlightCommandKind::release_fall);
  return commands;
}

[[nodiscard]] auto descent_guidance(const SignalRunState& run) -> Guidance {
  const auto& flight = *run.flight;
  const auto& navigation = run.signal_navigation;
  const double relative = navigation.relative_bearing_radians;
  const bool aligned = std::abs(relative) <= 0.20;
  const bool reached = navigation.status == SignalScannerStatus::reached;
  const auto target = std::ranges::find(run.catalog.signals,
                                       *run.scanner.selected,
                                       &SurfaceSignal::id);
  const double target_altitude =
      target == run.catalog.signals.end()
          ? flight.pose.position.altitude_metres
          : static_cast<double>(target->surface_elevation_metres +
                                target->approach_altitude_metres);
  return Guidance{
      .forward = aligned && !reached && navigation.distance_metres > 700.0,
      .backward = false,
      .left = relative < -0.025,
      .right = relative > 0.025,
      .rise = false,
      .fall = flight.pose.position.altitude_metres > target_altitude + 250.0,
  };
}

[[nodiscard]] auto return_guidance(const SignalRunState& run) -> Guidance {
  const auto& flight = *run.flight;
  const auto& navigation = *run.origin_navigation;
  const double relative = navigation.relative_bearing_radians;
  const bool aligned = std::abs(relative) <= 0.20;
  const double horizontal_speed = std::hypot(
      flight.velocity.east_metres_per_second,
      flight.velocity.north_metres_per_second);
  const double altitude_error =
      run.rendezvous->position.altitude_metres -
      flight.pose.position.altitude_metres;
  const bool braking = navigation.distance_metres < 12'000.0 &&
                       horizontal_speed > 80.0;
  return Guidance{
      .forward = aligned && navigation.distance_metres >= 12'000.0,
      .backward = aligned && braking,
      .left = relative < -0.025,
      .right = relative > 0.025,
      .rise = altitude_error > 2'000.0,
      .fall = altitude_error < -2'000.0,
  };
}

}  // namespace

auto run_signal_run_acceptance(
    RenderConfiguration configuration,
    const std::filesystem::path& checkpoint_path,
    std::string_view presentation)
    -> std::expected<SignalRunAcceptanceResult, SignalRunAcceptanceError> {
  std::error_code checkpoint_error;
  const bool checkpoint_exists =
      std::filesystem::exists(checkpoint_path, checkpoint_error);
  if (!validate_viewport(configuration.viewport) || checkpoint_path.empty() ||
      checkpoint_error || checkpoint_exists) {
    return std::unexpected{
        SignalRunAcceptanceError::invalid_configuration};
  }
  const CheckpointCleanup cleanup{checkpoint_path};
  auto cache = TerrainTileCache::create();
  if (!cache) {
    return std::unexpected{
        SignalRunAcceptanceError::initialization_failure};
  }
  const auto fresh = make_new_game_document(Seed{kSignalRunAcceptanceSeed});
  auto run = hydrate_signal_run(fresh, *cache);
  if (!run || !accept_signal_run(*run) ||
      !launch_signal_run(*run, *cache) || !run->flight ||
      !run->scanner.selected) {
    return std::unexpected{
        SignalRunAcceptanceError::initialization_failure};
  }

  SignalRunAcceptanceReport report{
      .render_configuration = configuration,
      .presentation = std::string{presentation},
      .station_id = run->onboarding.origin_station,
      .target_id = *run->scanner.selected,
      .launch_tick = run->flight->tick,
  };
  while (run->onboarding.first_objective !=
         FirstObjectiveStatus::completed) {
    if (run->flight->tick >= kSignalRunAcceptanceMaximumTicks) {
      return std::unexpected{SignalRunAcceptanceError::incomplete_path};
    }
    const auto prior_regime = run->flight->regime;
    const auto prior_status = run->signal_navigation.status;
    const auto commands =
        guidance_commands(*run->flight, descent_guidance(*run));
    if (!advance_signal_run(*run, *cache, commands)) {
      return std::unexpected{SignalRunAcceptanceError::simulation_failure};
    }
    if (prior_regime == FlightRegime::orbital &&
        run->flight->regime == FlightRegime::atmospheric) {
      report.atmospheric_tick = run->flight->tick;
    }
    if (prior_regime != FlightRegime::terrain_flight &&
        run->flight->regime == FlightRegime::terrain_flight) {
      report.terrain_tick = run->flight->tick;
    }
    if (prior_status != SignalScannerStatus::reached &&
        run->signal_navigation.status == SignalScannerStatus::reached) {
      report.reached_tick = run->flight->tick;
    }
  }
  report.completion_tick = *run->collection.completion_tick;

  while (run->flight->regime != FlightRegime::orbital ||
         !run->origin_navigation || !run->origin_navigation->arrived) {
    if (run->flight->tick >= kSignalRunAcceptanceMaximumTicks) {
      return std::unexpected{SignalRunAcceptanceError::incomplete_path};
    }
    const auto commands =
        guidance_commands(*run->flight, return_guidance(*run));
    if (!advance_signal_run(*run, *cache, commands)) {
      return std::unexpected{SignalRunAcceptanceError::simulation_failure};
    }
  }
  report.orbital_return_tick = run->flight->tick;
  report.checkpoint_flight_checksum =
      planetary_flight_state_checksum(*run->flight);
  const auto checkpoint = project_signal_run_save(*run);
  if (!checkpoint || !write_save_file_atomically(checkpoint_path, *checkpoint)) {
    return std::unexpected{
        SignalRunAcceptanceError::checkpoint_write_failure};
  }
  const auto loaded = load_save_file(checkpoint_path);
  if (!loaded) {
    return std::unexpected{
        SignalRunAcceptanceError::checkpoint_load_failure};
  }
  auto resumed_cache = TerrainTileCache::create();
  auto resumed = resumed_cache
                     ? hydrate_signal_run(*loaded, *resumed_cache)
                     : std::expected<SignalRunState, SignalRunError>{
                           std::unexpected{SignalRunError::terrain_failure}};
  if (!resumed || !resumed->flight || *loaded != *checkpoint) {
    return std::unexpected{SignalRunAcceptanceError::resume_mismatch};
  }
  report.resumed_flight_checksum =
      planetary_flight_state_checksum(*resumed->flight);
  if (report.resumed_flight_checksum != report.checkpoint_flight_checksum ||
      !resumed->origin_navigation || !resumed->origin_navigation->arrived) {
    return std::unexpected{SignalRunAcceptanceError::resume_mismatch};
  }

  PlanetaryPresentationSettings settings{
      .width = configuration.viewport.width,
      .height = configuration.viewport.height,
  };
  PlanetaryPresentationRenderer renderer{settings};
  std::vector<termforge::Pixel> frame(
      static_cast<std::size_t>(configuration.viewport.width) *
      static_cast<std::size_t>(configuration.viewport.height));
  if (!renderer.render(*resumed->planet, *resumed->flight,
                       {.pitch_radians = -0.18}, frame)) {
    return std::unexpected{
        SignalRunAcceptanceError::presentation_failure};
  }
  report.framebuffer_checksum = pixel_checksum(frame);
  report.discovery_count = resumed->discoveries.size();
  report.world_delta_count = resumed->journal.entries().size();
  if (!return_signal_run_to_origin(*resumed)) {
    return std::unexpected{SignalRunAcceptanceError::simulation_failure};
  }
  auto returned = project_signal_run_save(*resumed);
  if (!returned || returned->state.location !=
                       OriginLocation::docked_at_origin ||
      returned->state.flight || returned->state.first_objective !=
                                    FirstObjectiveStatus::completed) {
    return std::unexpected{SignalRunAcceptanceError::incomplete_path};
  }
  return SignalRunAcceptanceResult{
      .report = report,
      .returned_save = std::move(*returned),
      .final_frame = std::move(frame),
  };
}

auto signal_run_acceptance_json(const SignalRunAcceptanceReport& report)
    -> std::string {
  return std::format(
      "{{\n"
      "  \"schema_version\": 1,\n"
      "  \"scenario\": \"{}\",\n"
      "  \"seed\": {},\n"
      "  \"station_id\": \"{}\",\n"
      "  \"target_id\": \"{}\",\n"
      "  \"launch_tick\": {},\n"
      "  \"atmospheric_tick\": {},\n"
      "  \"terrain_tick\": {},\n"
      "  \"reached_tick\": {},\n"
      "  \"completion_tick\": {},\n"
      "  \"orbital_return_tick\": {},\n"
      "  \"checkpoint_flight_checksum\": \"{}\",\n"
      "  \"resumed_flight_checksum\": \"{}\",\n"
      "  \"framebuffer_checksum\": \"{}\",\n"
      "  \"discovery_count\": {},\n"
      "  \"world_delta_count\": {},\n"
      "  \"final_location\": \"docked_at_origin\",\n"
      "  \"final_objective\": \"completed\",\n"
      "  \"presentation\": \"{}\",\n"
      "  \"render_profile\": \"{}\",\n"
      "  \"viewport_width\": {},\n"
      "  \"viewport_height\": {}\n"
      "}}\n",
      kSignalRunAcceptanceScenario, kSignalRunAcceptanceSeed,
      origin_station_id_string(report.station_id),
      surface_signal_id_string(report.target_id), report.launch_tick,
      report.atmospheric_tick, report.terrain_tick, report.reached_tick,
      report.completion_tick, report.orbital_return_tick,
      report.checkpoint_flight_checksum, report.resumed_flight_checksum,
      report.framebuffer_checksum, report.discovery_count,
      report.world_delta_count, report.presentation,
      profile_name(report.render_configuration),
      report.render_configuration.viewport.width,
      report.render_configuration.viewport.height);
}

}  // namespace apsis_drift
