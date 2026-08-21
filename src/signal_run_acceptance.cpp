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

[[nodiscard]] auto sun_visibility_name(
    SunCheckpointVisibility visibility) noexcept -> std::string_view {
  switch (visibility) {
    case SunCheckpointVisibility::visible: return "visible";
    case SunCheckpointVisibility::planet_occluded: return "planet_occluded";
    case SunCheckpointVisibility::reemerged: return "reemerged";
  }
  return "unknown";
}

[[nodiscard]] auto sun_cycle_probe(
    const PlanetDescriptor& planet, RenderConfiguration configuration)
    -> std::expected<std::vector<SunCycleCheckpointMeasurement>,
                     SignalRunAcceptanceError> {
  constexpr SimulationTick limb_offset{5'200};
  constexpr std::array checkpoints{
      std::pair{SunCheckpointVisibility::visible,
                kLocalDayTicks - limb_offset},
      std::pair{SunCheckpointVisibility::planet_occluded, kLocalDayTicks},
      std::pair{SunCheckpointVisibility::reemerged,
                kLocalDayTicks + limb_offset},
  };
  const auto aligned = resolve_local_sun(planet, kLocalDayTicks);
  if (!aligned) {
    return std::unexpected{
        SignalRunAcceptanceError::initialization_failure};
  }
  const double radius = static_cast<double>(planet.radius.value) * 1'000.0;
  const OrbitalCamera camera{
      .position = {-aligned->planet_to_sun.x * radius * 3.5,
                   -aligned->planet_to_sun.y * radius * 3.5,
                   -aligned->planet_to_sun.z * radius * 3.5},
      .forward = aligned->planet_to_sun,
      .up = {0.0, 0.0, 1.0},
  };
  const OrbitalRenderer renderer{{
      .width = configuration.viewport.width,
      .height = configuration.viewport.height,
      .field_of_view_degrees = 60.0,
      .horizontal_sample_stride =
          configuration.viewport.width >= kDefaultViewportWidth ? 2 : 1,
  }};
  std::vector<termforge::Pixel> frame(
      static_cast<std::size_t>(configuration.viewport.width) *
      static_cast<std::size_t>(configuration.viewport.height));
  std::vector<SunCycleCheckpointMeasurement> result;
  result.reserve(checkpoints.size());
  for (const auto& [visibility, tick] : checkpoints) {
    const auto sun = resolve_local_sun(planet, tick);
    const auto rendered =
        sun ? renderer.render(planet, camera, sun->planet_to_sun, frame)
            : std::expected<OrbitalRenderStats, OrbitalRenderError>{
                  std::unexpected{OrbitalRenderError::invalid_light_direction}};
    const bool expected_visibility =
        visibility != SunCheckpointVisibility::planet_occluded;
    if (!sun || !rendered ||
        (rendered->sun_pixels > 0) != expected_visibility) {
      return std::unexpected{
          SignalRunAcceptanceError::presentation_failure};
    }
    result.push_back({.visibility = visibility,
                      .tick = tick,
                      .direction = sun->planet_to_sun,
                      .sun_pixels = rendered->sun_pixels,
                      .framebuffer_checksum = pixel_checksum(frame)});
  }
  return result;
}

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

[[nodiscard]] auto save_bytes_checksum(std::string_view bytes) noexcept
    -> std::uint64_t {
  std::uint64_t hash{1469598103934665603ULL};
  for (const unsigned char byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

[[nodiscard]] auto verify_save_checkpoint(
    const SignalRunState& run, std::string_view name,
    std::vector<SignalRunSaveCheckpointMeasurement>* measurements)
    -> std::expected<void, SignalRunAcceptanceError> {
  if (measurements == nullptr) return {};
  const auto document = project_signal_run_save(run);
  const auto encoded = document ? encode_save_document_json(*document)
                                : std::expected<std::string, SaveSchemaError>{
                                      std::unexpected{SaveSchemaError{}}};
  const auto decoded = encoded ? decode_save_document_json(*encoded)
                               : std::expected<SaveDocument, SaveSchemaError>{
                                     std::unexpected{SaveSchemaError{}}};
  auto resumed_cache = TerrainTileCache::create();
  const auto resumed =
      decoded && resumed_cache
          ? hydrate_signal_run(*decoded, *resumed_cache)
          : std::expected<SignalRunState, SignalRunError>{
                std::unexpected{SignalRunError::terrain_failure}};
  const auto resumed_document =
      resumed ? project_signal_run_save(*resumed)
              : std::expected<SaveDocument, SignalRunError>{
                    std::unexpected{SignalRunError::inconsistent_state}};
  if (!document || !encoded || !decoded || !resumed || !resumed_document ||
      *decoded != *document || *resumed_document != *document) {
    return std::unexpected{SignalRunAcceptanceError::checkpoint_load_failure};
  }
  const SimulationTick tick = run.career ? run.career->universe_tick
                                         : (run.flight ? run.flight->tick : 0U);
  measurements->push_back(
      {std::string{name}, tick, save_bytes_checksum(*encoded)});
  return {};
}

[[nodiscard]] auto launch_home_planetfall(
    SignalRunState& run, TerrainTileCache& cache,
    std::vector<SignalRunSaveCheckpointMeasurement>* checkpoints)
    -> std::expected<void, SignalRunAcceptanceError> {
  if (!accept_signal_run(run) ||
      !verify_save_checkpoint(run, "docked", checkpoints) ||
      !launch_signal_run(run, cache) || !run.station_flight ||
      !verify_save_checkpoint(run, "station-flight", checkpoints)) {
    return std::unexpected{SignalRunAcceptanceError::initialization_failure};
  }
  const std::array commands{FlightCommand{run.station_flight->tick,
                                          FlightCommandKind::press_forward}};
  if (!advance_signal_run_station_flight(run, commands) ||
      !begin_signal_run_planetfall(run, cache) || !run.flight ||
      !verify_save_checkpoint(run, "orbital", checkpoints)) {
    return std::unexpected{SignalRunAcceptanceError::initialization_failure};
  }
  return {};
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
  const bool pilot =
      run.career && run.career->rule_profile == IntersystemRuleProfile::pilot;
  const bool thermal_recovery =
      pilot &&
      (flight.thermal.abort_latched || flight.thermal.load_units >= 600'000U);
  return Guidance{
      .forward = aligned && !reached && navigation.distance_metres > 700.0 &&
                 !thermal_recovery,
      .backward = thermal_recovery,
      .left = relative < -0.025,
      .right = relative > 0.025,
      .rise = thermal_recovery,
      .fall = navigation.distance_metres < 750'000.0 &&
              flight.pose.position.altitude_metres > target_altitude + 250.0 &&
              !thermal_recovery,
  };
}

[[nodiscard]] auto home_ascent_guidance(const SignalRunState& run) -> Guidance {
  const auto& flight = *run.flight;
  const double horizontal_speed =
      std::hypot(flight.velocity.east_metres_per_second,
                 flight.velocity.north_metres_per_second);
  return Guidance{
      .forward = false,
      .backward = horizontal_speed > 25.0,
      .left = false,
      .right = false,
      .rise = true,
      .fall = false,
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
  const auto performance =
      flight_performance(*run.planet, FlightRegime::orbital);
  if (!performance) {
    return std::unexpected{
        SignalRunAcceptanceError::initialization_failure};
  }
  constexpr SimulationTick maximum_probe_ticks{2'000};
  PacingProbe result;
  const SimulationTick probe_started = run.flight->tick;
  while (run.flight->tick - probe_started < maximum_probe_ticks) {
    std::array<FlightCommand, 1> pressed{
        FlightCommand{run.flight->tick, FlightCommandKind::press_forward}};
    const std::span<const FlightCommand> commands =
        run.flight->tick == probe_started ? std::span{pressed}
                                          : std::span<const FlightCommand>{};
    if (!advance_signal_run(run, *cache, commands)) {
      return std::unexpected{SignalRunAcceptanceError::simulation_failure};
    }
    const double speed = std::hypot(
        run.flight->velocity.east_metres_per_second,
        run.flight->velocity.north_metres_per_second);
    if (result.first_motion_tick == 0 && speed > 0.5) {
      result.first_motion_tick = run.flight->tick - probe_started;
    }
    if (speed >= performance->maximum_horizontal_speed * 0.9) break;
  }
  result.acceleration_ticks = run.flight->tick - probe_started;
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

struct CompletedScenario {
  SignalRunScenarioMeasurement measurement;
  SaveDocument returned_save;
  std::vector<termforge::Pixel> final_frame;
};

struct TerrainSafetyProbe {
  SimulationTick ticks{};
  double minimum_clearance_metres{};
  std::uint64_t flight_checksum{};
};

[[nodiscard]] auto terrain_safety_probe()
    -> std::expected<TerrainSafetyProbe, SignalRunAcceptanceError> {
  auto cache = TerrainTileCache::create();
  const auto fresh =
      make_legacy_signal_run_document(Seed{kSignalRunDefaultSeed});
  auto run = cache ? hydrate_signal_run(fresh, *cache)
                   : std::expected<SignalRunState, SignalRunError>{
                         std::unexpected{SignalRunError::terrain_failure}};
  if (!cache || !run || !accept_signal_run(*run) ||
      !launch_signal_run(*run, *cache) || !run->flight) {
    return std::unexpected{
        SignalRunAcceptanceError::initialization_failure};
  }
  TerrainSafetyProbe result{
      .minimum_clearance_metres = run->flight->clearance_metres,
  };
  while (run->flight->tick < kTerrainSafetyProbeTicks) {
    constexpr std::array initial_commands{
        FlightCommand{0, FlightCommandKind::press_forward},
        FlightCommand{0, FlightCommandKind::press_fall},
    };
    const std::span<const FlightCommand> commands =
        run->flight->tick == 0
            ? std::span<const FlightCommand>{initial_commands}
            : std::span<const FlightCommand>{};
    if (!advance_signal_run(*run, *cache, commands)) {
      return std::unexpected{SignalRunAcceptanceError::simulation_failure};
    }
    result.minimum_clearance_metres =
        std::min(result.minimum_clearance_metres,
                 run->flight->clearance_metres);
  }
  result.ticks = run->flight->tick;
  result.flight_checksum = planetary_flight_state_checksum(*run->flight);
  if (result.ticks != kTerrainSafetyProbeTicks ||
      result.minimum_clearance_metres < kMinimumFlightClearanceMetres) {
    return std::unexpected{SignalRunAcceptanceError::incomplete_path};
  }
  return result;
}

[[nodiscard]] auto run_complete_scenario(
    std::uint32_t seed, RenderConfiguration configuration,
    const std::filesystem::path* checkpoint_path,
    SignalRunAcceptanceReport* canonical_report,
    IntersystemRuleProfile rule_profile)
    -> std::expected<CompletedScenario, SignalRunAcceptanceError> {
  auto cache = TerrainTileCache::create();
  if (!cache) {
    return std::unexpected{
        SignalRunAcceptanceError::initialization_failure};
  }
  auto fresh = make_new_game_document(NewGameOptions{
      .universe_seed = Seed{seed},
      .penalty_mode = rule_profile,
      .onboarding = NewGameOnboardingChoice::guided,
  });
  auto run = hydrate_signal_run(fresh, *cache);
  auto* checkpoints = canonical_report != nullptr
                          ? &canonical_report->save_checkpoints
                          : nullptr;
  if (!run || !launch_home_planetfall(*run, *cache, checkpoints) ||
      !run->flight || !run->scanner.selected) {
    return std::unexpected{SignalRunAcceptanceError::initialization_failure};
  }

  SignalRunScenarioMeasurement measurement{
      .seed = seed,
      .rule_profile = rule_profile,
      .atmosphere_class = run->planet->atmosphere_class,
      .minimum_clearance_metres = run->flight->clearance_metres,
  };
  if (canonical_report != nullptr) {
    canonical_report->station_id = run->onboarding.origin_station;
    canonical_report->contract_id = run->onboarding.first_contract;
    canonical_report->target_id = *run->scanner.selected;
    canonical_report->launch_tick = run->flight->tick;
    canonical_report->initial_distance_metres =
        run->signal_navigation.distance_metres;
    const auto probe = pacing_probe(*run);
    if (!probe) return std::unexpected{probe.error()};
    canonical_report->first_motion_tick = probe->first_motion_tick;
    canonical_report->orbital_acceleration_ticks = probe->acceleration_ticks;
    canonical_report->orbital_braking_ticks = probe->braking_ticks;
  }

  std::optional<PlanetaryFlightState> atmospheric_state;
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
    measurement.minimum_clearance_metres = std::min(
        measurement.minimum_clearance_metres, run->flight->clearance_metres);
    measurement.peak_thermal_load_units = std::max(
        measurement.peak_thermal_load_units, run->flight->thermal.load_units);
    measurement.thermal_abort_observed = measurement.thermal_abort_observed ||
                                         run->flight->thermal.abort_latched;
    if (canonical_report != nullptr &&
        run->flight->regime == FlightRegime::orbital) {
      canonical_report->peak_orbital_speed_metres_per_second = std::max(
          canonical_report->peak_orbital_speed_metres_per_second,
          std::hypot(run->flight->velocity.east_metres_per_second,
                     run->flight->velocity.north_metres_per_second,
                     run->flight->velocity.up_metres_per_second));
    }
    if (canonical_report != nullptr && !resumed && checkpoint_path != nullptr &&
        run->flight->tick == kSignalRunAcceptanceResumeTick) {
      const auto checkpoint = project_signal_run_save(*run);
      PlanetaryPresentationSettings checkpoint_settings{
          .width = configuration.viewport.width,
          .height = configuration.viewport.height,
      };
      PlanetaryPresentationRenderer checkpoint_renderer{checkpoint_settings};
      std::vector<termforge::Pixel> checkpoint_frame(
          static_cast<std::size_t>(configuration.viewport.width) *
          static_cast<std::size_t>(configuration.viewport.height));
      const auto checkpoint_render = checkpoint_renderer.render(
          *run->planet, *run->flight, {.pitch_radians = -0.18},
          checkpoint_frame);
      if (!checkpoint ||
          !checkpoint_render ||
          !write_save_file_atomically(*checkpoint_path, *checkpoint)) {
        return std::unexpected{
            SignalRunAcceptanceError::checkpoint_write_failure};
      }
      canonical_report->resume_tick = run->flight->tick;
      canonical_report->checkpoint_flight_checksum =
          planetary_flight_state_checksum(*run->flight);
      canonical_report->checkpoint_sun = checkpoint_render->sun;
      canonical_report->checkpoint_framebuffer_checksum =
          pixel_checksum(checkpoint_frame);
      const auto loaded = load_save_file(*checkpoint_path);
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
      canonical_report->resumed_flight_checksum =
          planetary_flight_state_checksum(*resumed_run->flight);
      PlanetaryPresentationRenderer resumed_renderer{checkpoint_settings};
      std::vector<termforge::Pixel> resumed_frame(checkpoint_frame.size());
      const auto resumed_render = resumed_renderer.render(
          *resumed_run->planet, *resumed_run->flight,
          {.pitch_radians = -0.18}, resumed_frame);
      canonical_report->resumed_framebuffer_checksum =
          resumed_render ? pixel_checksum(resumed_frame) : 0;
      if (canonical_report->resumed_flight_checksum !=
              canonical_report->checkpoint_flight_checksum ||
          !resumed_render ||
          resumed_render->sun != canonical_report->checkpoint_sun ||
          canonical_report->resumed_framebuffer_checksum !=
              canonical_report->checkpoint_framebuffer_checksum) {
        return std::unexpected{SignalRunAcceptanceError::resume_mismatch};
      }
      *cache = std::move(*resumed_cache);
      *run = std::move(*resumed_run);
      resumed = true;
    }
    if (prior_regime == FlightRegime::orbital &&
        run->flight->regime == FlightRegime::atmospheric) {
      measurement.atmospheric_tick = run->flight->tick;
      atmospheric_state = *run->flight;
      if (!verify_save_checkpoint(*run, "atmospheric", checkpoints)) {
        return std::unexpected{
            SignalRunAcceptanceError::checkpoint_load_failure};
      }
    }
    if (prior_regime != FlightRegime::terrain_flight &&
        run->flight->regime == FlightRegime::terrain_flight) {
      measurement.terrain_tick = run->flight->tick;
      if (!verify_save_checkpoint(*run, "terrain", checkpoints)) {
        return std::unexpected{
            SignalRunAcceptanceError::checkpoint_load_failure};
      }
    }
    if (prior_status != SignalScannerStatus::reached &&
        run->signal_navigation.status == SignalScannerStatus::reached) {
      measurement.reached_tick = run->flight->tick;
    }
  }
  measurement.completion_tick = *run->collection.completion_tick;
  if (!verify_save_checkpoint(*run, "objective-complete", checkpoints)) {
    return std::unexpected{SignalRunAcceptanceError::checkpoint_load_failure};
  }
  if (!atmospheric_state || measurement.atmospheric_tick == 0 ||
      measurement.terrain_tick <= measurement.atmospheric_tick ||
      measurement.terrain_tick - measurement.atmospheric_tick >
          kAtmosphericLegPacingTargetTicks ||
      measurement.minimum_clearance_metres <
          kMinimumFlightClearanceMetres) {
    return std::unexpected{SignalRunAcceptanceError::incomplete_path};
  }
  if (canonical_report != nullptr) {
    canonical_report->atmospheric_tick = measurement.atmospheric_tick;
    canonical_report->terrain_tick = measurement.terrain_tick;
    canonical_report->reached_tick = measurement.reached_tick;
    canonical_report->completion_tick = measurement.completion_tick;
    if (!resumed || canonical_report->first_motion_tick != 1 ||
        canonical_report->orbital_acceleration_ticks > 600 ||
        canonical_report->orbital_braking_ticks > 600) {
      return std::unexpected{SignalRunAcceptanceError::incomplete_path};
    }
  }

  while (run->flight->regime != FlightRegime::orbital) {
    if (run->flight->tick >= kSignalRunAcceptanceMaximumTicks) {
      return std::unexpected{SignalRunAcceptanceError::incomplete_path};
    }
    const auto commands =
        guidance_commands(*run->flight, home_ascent_guidance(*run));
    if (!advance_signal_run(*run, *cache, commands)) {
      return std::unexpected{SignalRunAcceptanceError::simulation_failure};
    }
    measurement.minimum_clearance_metres = std::min(
        measurement.minimum_clearance_metres, run->flight->clearance_metres);
    measurement.peak_thermal_load_units = std::max(
        measurement.peak_thermal_load_units, run->flight->thermal.load_units);
    measurement.thermal_abort_observed = measurement.thermal_abort_observed ||
                                         run->flight->thermal.abort_latched;
  }
  measurement.orbital_return_tick = run->flight->tick;
  measurement.return_flight_checksum =
      planetary_flight_state_checksum(*run->flight);
  if (!verify_save_checkpoint(*run, "ascent", checkpoints)) {
    return std::unexpected{SignalRunAcceptanceError::checkpoint_load_failure};
  }
  if (canonical_report != nullptr) {
    canonical_report->orbital_return_tick = measurement.orbital_return_tick;
    canonical_report->return_flight_checksum =
        measurement.return_flight_checksum;
  }

  PlanetaryPresentationSettings settings{
      .width = configuration.viewport.width,
      .height = configuration.viewport.height,
  };
  PlanetaryPresentationRenderer renderer{settings};
  std::vector<termforge::Pixel> atmospheric_frame(
      static_cast<std::size_t>(configuration.viewport.width) *
      static_cast<std::size_t>(configuration.viewport.height));
  const auto atmospheric_render = renderer.render(
      *run->planet, *atmospheric_state, {.pitch_radians = -0.08},
      atmospheric_frame);
  if (!atmospheric_render ||
      (run->planet->atmosphere_class != AtmosphereClass::airless &&
       atmospheric_render->mode != PlanetaryPresentationMode::atmospheric)) {
    return std::unexpected{
        SignalRunAcceptanceError::presentation_failure};
  }
  measurement.atmospheric_framebuffer_checksum =
      pixel_checksum(atmospheric_frame);

  std::vector<termforge::Pixel> final_frame(atmospheric_frame.size());
  if (!renderer.render(*run->planet, *run->flight,
                       {.pitch_radians = -0.18}, final_frame)) {
    return std::unexpected{
        SignalRunAcceptanceError::presentation_failure};
  }
  if (canonical_report != nullptr) {
    canonical_report->framebuffer_checksum = pixel_checksum(final_frame);
    canonical_report->discovery_count = run->discoveries.size();
    canonical_report->world_delta_count = run->journal.entries().size();
  }
  if (!depart_signal_run_home_planet(*run) || !run->station_flight) {
    return std::unexpected{SignalRunAcceptanceError::simulation_failure};
  }
  const SimulationTick station_return_started = run->career->universe_tick;
  while (true) {
    const auto station_guidance = resolve_origin_station_flight_guidance(
        run->recipe.universe_seed, run->career->universe_tick,
        run->origin_system, *run->station_flight);
    if (!station_guidance) {
      return std::unexpected{SignalRunAcceptanceError::simulation_failure};
    }
    if (station_guidance->arrived) break;
    if (run->career->universe_tick - station_return_started >=
            kSignalRunAcceptanceMaximumTicks ||
        !advance_signal_run_station_flight(*run, {})) {
      return std::unexpected{SignalRunAcceptanceError::incomplete_path};
    }
  }
  if (!verify_save_checkpoint(*run, "rendezvous", checkpoints)) {
    return std::unexpected{SignalRunAcceptanceError::checkpoint_load_failure};
  }
  if (!return_signal_run_to_origin(*run) || !turn_in_signal_run(*run)) {
    return std::unexpected{SignalRunAcceptanceError::simulation_failure};
  }
  auto returned = project_signal_run_save(*run);
  if (!returned ||
      returned->state.location != OriginLocation::docked_at_origin ||
      returned->state.flight ||
      returned->state.first_objective != FirstObjectiveStatus::turned_in ||
      returned->state.onboarding.chapter != OnboardingChapter::contract_two) {
    return std::unexpected{SignalRunAcceptanceError::incomplete_path};
  }
  return CompletedScenario{measurement, std::move(*returned),
                           std::move(final_frame)};
}

}  // namespace

auto run_signal_run_acceptance(
    RenderConfiguration configuration,
    const std::filesystem::path& checkpoint_path)
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
  SignalRunAcceptanceReport report{};
  report.render_configuration = configuration;
  constexpr std::array seeds{kSignalRunAcceptanceSeed,
                             kSignalRunDefaultSeed,
                             kSignalRunDenseSeed};
  std::optional<CompletedScenario> canonical;
  report.scenarios.reserve(seeds.size() + 1U);
  for (const auto seed : seeds) {
    const bool is_canonical = seed == kSignalRunAcceptanceSeed;
    auto completed = run_complete_scenario(
        seed, configuration, is_canonical ? &checkpoint_path : nullptr,
        is_canonical ? &report : nullptr, IntersystemRuleProfile::assisted);
    if (!completed) return std::unexpected{completed.error()};
    report.scenarios.push_back(completed->measurement);
    if (is_canonical) canonical = std::move(*completed);
  }
  if (!canonical) {
    return std::unexpected{SignalRunAcceptanceError::incomplete_path};
  }
  auto pilot =
      run_complete_scenario(kSignalRunAcceptanceSeed, configuration, nullptr,
                            nullptr, IntersystemRuleProfile::pilot);
  if (!pilot) return std::unexpected{pilot.error()};
  report.scenarios.push_back(pilot->measurement);
  const auto canonical_system_seed = derive_seed(
      canonical->returned_save.recipe.universe_seed, SeedDomain::system,
      canonical->returned_save.recipe.origin_system_ordinal);
  const auto canonical_planet =
      generate_origin_home_planet(canonical_system_seed);
  const auto sun_cycle = sun_cycle_probe(canonical_planet, configuration);
  if (!sun_cycle) return std::unexpected{sun_cycle.error()};
  report.sun_cycle = *sun_cycle;
  const auto safety = terrain_safety_probe();
  if (!safety) return std::unexpected{safety.error()};
  report.terrain_safety_probe_ticks = safety->ticks;
  report.terrain_safety_minimum_clearance_metres =
      safety->minimum_clearance_metres;
  report.terrain_safety_flight_checksum = safety->flight_checksum;
  return SignalRunAcceptanceResult{
      .report = report,
      .returned_save = std::move(canonical->returned_save),
      .final_frame = std::move(canonical->final_frame),
  };
}

auto signal_run_acceptance_json(const SignalRunAcceptanceReport& report)
    -> std::string {
  std::string save_checkpoints;
  for (std::size_t index = 0; index < report.save_checkpoints.size(); ++index) {
    const auto& checkpoint = report.save_checkpoints[index];
    save_checkpoints +=
        std::format("    {{\"name\": \"{}\", \"tick\": {}, "
                    "\"save_checksum\": \"{}\"}}{}\n",
                    checkpoint.name, checkpoint.tick, checkpoint.save_checksum,
                    index + 1 == report.save_checkpoints.size() ? "" : ",");
  }
  std::string scenarios;
  for (std::size_t index = 0; index < report.scenarios.size(); ++index) {
    const auto& scenario = report.scenarios[index];
    scenarios += std::format(
        "    {{\"seed\": {}, \"rule_profile\": \"{}\", "
        "\"atmosphere_class\": \"{}\", "
        "\"atmospheric_tick\": {}, \"terrain_tick\": {}, "
        "\"reached_tick\": {}, \"completion_tick\": {}, "
        "\"orbital_return_tick\": {}, "
        "\"minimum_clearance_metres\": {:.6f}, "
        "\"atmospheric_framebuffer_checksum\": \"{}\", "
        "\"return_flight_checksum\": \"{}\", "
        "\"peak_thermal_load_units\": \"{}\", "
        "\"thermal_abort_observed\": {}}}{}\n",
        scenario.seed,
        scenario.rule_profile == IntersystemRuleProfile::pilot ? "pilot"
                                                               : "assisted",
        atmosphere_class_name(scenario.atmosphere_class),
        scenario.atmospheric_tick, scenario.terrain_tick, scenario.reached_tick,
        scenario.completion_tick, scenario.orbital_return_tick,
        scenario.minimum_clearance_metres,
        scenario.atmospheric_framebuffer_checksum,
        scenario.return_flight_checksum, scenario.peak_thermal_load_units,
        scenario.thermal_abort_observed ? "true" : "false",
        index + 1 == report.scenarios.size() ? "" : ",");
  }
  std::string sun_cycle;
  for (std::size_t index = 0; index < report.sun_cycle.size(); ++index) {
    const auto& checkpoint = report.sun_cycle[index];
    sun_cycle += std::format(
        "    {{\"visibility\": \"{}\", \"tick\": {}, "
        "\"direction\": {{\"x\": {:.9f}, \"y\": {:.9f}, "
        "\"z\": {:.9f}}}, \"sun_pixels\": {}, "
        "\"framebuffer_checksum\": \"{}\"}}{}\n",
        sun_visibility_name(checkpoint.visibility), checkpoint.tick,
        checkpoint.direction.x, checkpoint.direction.y,
        checkpoint.direction.z, checkpoint.sun_pixels,
        checkpoint.framebuffer_checksum,
        index + 1 == report.sun_cycle.size() ? "" : ",");
  }
  return std::format(
      "{{\n"
      "  \"schema_version\": 6,\n"
      "  \"scenario\": \"{}\",\n"
      "  \"evidence_scope\": \"application_framebuffer\",\n"
      "  \"seed\": {},\n"
      "  \"station_id\": \"{}\",\n"
      "  \"contract_id\": \"{}\",\n"
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
      "  \"sun_generator_version\": {},\n"
      "  \"checkpoint_sun\": {{\"cycle_tick\": {}, "
      "\"direction\": {{\"x\": {:.9f}, \"y\": {:.9f}, "
      "\"z\": {:.9f}}}}},\n"
      "  \"checkpoint_framebuffer_checksum\": \"{}\",\n"
      "  \"resumed_framebuffer_checksum\": \"{}\",\n"
      "  \"return_flight_checksum\": \"{}\",\n"
      "  \"framebuffer_checksum\": \"{}\",\n"
      "  \"terrain_safety_probe_ticks\": {},\n"
      "  \"terrain_safety_minimum_clearance_metres\": {:.6f},\n"
      "  \"terrain_safety_flight_checksum\": \"{}\",\n"
      "  \"discovery_count\": {},\n"
      "  \"world_delta_count\": {},\n"
      "  \"final_location\": \"docked_at_origin\",\n"
      "  \"final_objective\": \"turned_in\",\n"
      "  \"final_onboarding_chapter\": \"contract_two\",\n"
      "  \"save_checkpoints\": [\n"
      "{}"
      "  ],\n"
      "  \"sun_cycle\": [\n"
      "{}"
      "  ],\n"
      "  \"scenarios\": [\n"
      "{}"
      "  ],\n"
      "  \"render_profile\": \"{}\",\n"
      "  \"viewport_width\": {},\n"
      "  \"viewport_height\": {}\n"
      "}}\n",
      kSignalRunAcceptanceScenario, kSignalRunAcceptanceSeed,
      origin_station_id_string(report.station_id),
      home_signal_contract_id_string(report.contract_id),
      surface_signal_id_string(report.target_id), report.launch_tick,
      report.initial_distance_metres, report.first_motion_tick,
      report.orbital_acceleration_ticks, report.orbital_braking_ticks,
      report.peak_orbital_speed_metres_per_second,
      report.atmospheric_tick, report.terrain_tick, report.reached_tick,
      report.completion_tick, report.orbital_return_tick,
      report.resume_tick,
      report.checkpoint_flight_checksum, report.resumed_flight_checksum,
      kLocalSunGeneratorVersion, report.checkpoint_sun.cycle_tick,
      report.checkpoint_sun.planet_to_sun.x,
      report.checkpoint_sun.planet_to_sun.y,
      report.checkpoint_sun.planet_to_sun.z,
      report.checkpoint_framebuffer_checksum,
      report.resumed_framebuffer_checksum,
      report.return_flight_checksum, report.framebuffer_checksum,
      report.terrain_safety_probe_ticks,
      report.terrain_safety_minimum_clearance_metres,
      report.terrain_safety_flight_checksum, report.discovery_count,
      report.world_delta_count, save_checkpoints, sun_cycle, scenarios,
      profile_name(report.render_configuration),
      report.render_configuration.viewport.width,
      report.render_configuration.viewport.height);
}

}  // namespace apsis_drift
