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

struct PacingProbe {
  SimulationTick first_motion_tick{};
  SimulationTick acceleration_ticks{};
  SimulationTick braking_ticks{};
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

[[nodiscard]] auto pacing_probe(const SignalRunState& initial)
    -> std::expected<PacingProbe, SignalRunAcceptanceError> {
  auto cache = TerrainTileCache::create();
  if (!cache || !initial.flight) {
    return std::unexpected{
        SignalRunAcceptanceError::initialization_failure};
  }
  auto run = initial;
  const auto performance = flight_performance(FlightRegime::orbital);
  if (!performance) {
    return std::unexpected{
        SignalRunAcceptanceError::initialization_failure};
  }
  constexpr SimulationTick maximum_probe_ticks{2'000};
  PacingProbe result;
  while (run.flight->tick < maximum_probe_ticks) {
    std::array<FlightCommand, 1> pressed{
        FlightCommand{run.flight->tick, FlightCommandKind::press_forward}};
    const std::span<const FlightCommand> commands =
        run.flight->tick == 0 ? std::span{pressed}
                              : std::span<const FlightCommand>{};
    if (!advance_signal_run(run, *cache, commands)) {
      return std::unexpected{SignalRunAcceptanceError::simulation_failure};
    }
    const double speed = std::hypot(
        run.flight->velocity.east_metres_per_second,
        run.flight->velocity.north_metres_per_second);
    if (result.first_motion_tick == 0 && speed > 0.5) {
      result.first_motion_tick = run.flight->tick;
    }
    if (speed >= performance->maximum_horizontal_speed * 0.9) break;
  }
  result.acceleration_ticks = run.flight->tick;
  if (result.first_motion_tick == 0 ||
      result.acceleration_ticks >= maximum_probe_ticks) {
    return std::unexpected{SignalRunAcceptanceError::incomplete_path};
  }

  const SimulationTick braking_started = run.flight->tick;
  std::array braking_commands{
      FlightCommand{braking_started, FlightCommandKind::release_forward},
      FlightCommand{braking_started, FlightCommandKind::press_backward},
  };
  bool first_step{true};
  while (run.flight->tick - braking_started < maximum_probe_ticks) {
    const std::span<const FlightCommand> commands =
        first_step ? std::span{braking_commands}
                   : std::span<const FlightCommand>{};
    first_step = false;
    if (!advance_signal_run(run, *cache, commands)) {
      return std::unexpected{SignalRunAcceptanceError::simulation_failure};
    }
    const double heading_speed =
        std::cos(run.flight->pose.heading_radians) *
            run.flight->velocity.east_metres_per_second +
        std::sin(run.flight->pose.heading_radians) *
            run.flight->velocity.north_metres_per_second;
    if (heading_speed <= 0.0) break;
  }
  result.braking_ticks = run.flight->tick - braking_started;
  if (result.braking_ticks >= maximum_probe_ticks) {
    return std::unexpected{SignalRunAcceptanceError::incomplete_path};
  }
  return result;
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
      .initial_distance_metres = run->signal_navigation.distance_metres,
  };
  const auto probe = pacing_probe(*run);
  if (!probe) return std::unexpected{probe.error()};
  report.first_motion_tick = probe->first_motion_tick;
  report.orbital_acceleration_ticks = probe->acceleration_ticks;
  report.orbital_braking_ticks = probe->braking_ticks;
  bool resumed{};
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
    if (run->flight->regime == FlightRegime::orbital) {
      report.peak_orbital_speed_metres_per_second = std::max(
          report.peak_orbital_speed_metres_per_second,
          std::hypot(run->flight->velocity.east_metres_per_second,
                     run->flight->velocity.north_metres_per_second,
                     run->flight->velocity.up_metres_per_second));
    }
    if (!resumed && run->flight->tick == kSignalRunAcceptanceResumeTick) {
      const auto checkpoint = project_signal_run_save(*run);
      if (!checkpoint ||
          !write_save_file_atomically(checkpoint_path, *checkpoint)) {
        return std::unexpected{
            SignalRunAcceptanceError::checkpoint_write_failure};
      }
      report.resume_tick = run->flight->tick;
      report.checkpoint_flight_checksum =
          planetary_flight_state_checksum(*run->flight);
      const auto loaded = load_save_file(checkpoint_path);
      auto resumed_cache = TerrainTileCache::create();
      auto resumed_run = loaded && resumed_cache
                             ? hydrate_signal_run(*loaded, *resumed_cache)
                             : std::expected<SignalRunState, SignalRunError>{
                                   std::unexpected{
                                       SignalRunError::terrain_failure}};
      if (!loaded || !resumed_cache || !resumed_run ||
          !resumed_run->flight || *loaded != *checkpoint) {
        return std::unexpected{
            SignalRunAcceptanceError::checkpoint_load_failure};
      }
      report.resumed_flight_checksum =
          planetary_flight_state_checksum(*resumed_run->flight);
      if (report.resumed_flight_checksum !=
          report.checkpoint_flight_checksum) {
        return std::unexpected{SignalRunAcceptanceError::resume_mismatch};
      }
      *cache = std::move(*resumed_cache);
      *run = std::move(*resumed_run);
      resumed = true;
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
  if (!resumed || report.first_motion_tick != 1 ||
      report.orbital_acceleration_ticks > 600 ||
      report.orbital_braking_ticks > 600 ||
      report.atmospheric_tick > kSignalRunAtmosphericPacingTargetTicks ||
      report.reached_tick > kSignalRunReachedPacingTargetTicks) {
    return std::unexpected{SignalRunAcceptanceError::incomplete_path};
  }

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
  report.return_flight_checksum =
      planetary_flight_state_checksum(*run->flight);

  PlanetaryPresentationSettings settings{
      .width = configuration.viewport.width,
      .height = configuration.viewport.height,
  };
  PlanetaryPresentationRenderer renderer{settings};
  std::vector<termforge::Pixel> frame(
      static_cast<std::size_t>(configuration.viewport.width) *
      static_cast<std::size_t>(configuration.viewport.height));
  if (!renderer.render(*run->planet, *run->flight,
                       {.pitch_radians = -0.18}, frame)) {
    return std::unexpected{
        SignalRunAcceptanceError::presentation_failure};
  }
  report.framebuffer_checksum = pixel_checksum(frame);
  report.discovery_count = run->discoveries.size();
  report.world_delta_count = run->journal.entries().size();
  if (!return_signal_run_to_origin(*run)) {
    return std::unexpected{SignalRunAcceptanceError::simulation_failure};
  }
  auto returned = project_signal_run_save(*run);
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
      "  \"schema_version\": 2,\n"
      "  \"scenario\": \"{}\",\n"
      "  \"seed\": {},\n"
      "  \"station_id\": \"{}\",\n"
      "  \"target_id\": \"{}\",\n"
      "  \"launch_tick\": {},\n"
      "  \"initial_distance_metres\": {:.3f},\n"
      "  \"first_motion_tick\": {},\n"
      "  \"orbital_acceleration_ticks\": {},\n"
      "  \"orbital_braking_ticks\": {},\n"
      "  \"peak_orbital_speed_metres_per_second\": {:.3f},\n"
      "  \"atmospheric_tick\": {},\n"
      "  \"terrain_tick\": {},\n"
      "  \"reached_tick\": {},\n"
      "  \"completion_tick\": {},\n"
      "  \"orbital_return_tick\": {},\n"
      "  \"resume_tick\": {},\n"
      "  \"checkpoint_flight_checksum\": \"{}\",\n"
      "  \"resumed_flight_checksum\": \"{}\",\n"
      "  \"return_flight_checksum\": \"{}\",\n"
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
      report.initial_distance_metres, report.first_motion_tick,
      report.orbital_acceleration_ticks, report.orbital_braking_ticks,
      report.peak_orbital_speed_metres_per_second,
      report.atmospheric_tick, report.terrain_tick, report.reached_tick,
      report.completion_tick, report.orbital_return_tick,
      report.resume_tick,
      report.checkpoint_flight_checksum, report.resumed_flight_checksum,
      report.return_flight_checksum, report.framebuffer_checksum,
      report.discovery_count,
      report.world_delta_count, report.presentation,
      profile_name(report.render_configuration),
      report.render_configuration.viewport.width,
      report.render_configuration.viewport.height);
}

}  // namespace apsis_drift
