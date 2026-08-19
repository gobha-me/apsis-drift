#include "apsis_drift/intersystem_planetfall_acceptance.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <utility>

#include "apsis_drift/intersystem_jump.hpp"
#include "apsis_drift/save_file.hpp"
#include "apsis_drift/save_schema.hpp"

namespace apsis_drift {
namespace {

struct Fixture {
  FirstIntersystemIdentities identities;
  PlanetDescriptor planet;
  SurfaceSignalCatalog catalog;
};

struct Replay {
  std::array<IntersystemPlanetfallEntryMeasurement, 3> entries;
  IntersystemPlanetfallState completed;
  SimulationTick abort_orbit_tick{};
  std::uint64_t abort_orbit_checksum{};
  PilotThermalAcceptanceMeasurement thermal;
  std::vector<termforge::Pixel> frame;
  std::uint64_t framebuffer_checksum{};
};

[[nodiscard]] auto pixel_checksum(
    std::span<const termforge::Pixel> pixels) noexcept -> std::uint64_t {
  std::uint64_t hash{1469598103934665603ULL};
  for (const auto pixel : pixels) {
    for (const std::uint8_t value : {pixel.r, pixel.g, pixel.b, pixel.a}) {
      hash ^= value;
      hash *= 1099511628211ULL;
    }
  }
  return hash;
}

[[nodiscard]] auto canonical_longitude(double value) noexcept -> double {
  return std::remainder(value, 2.0 * std::numbers::pi_v<double>);
}

[[nodiscard]] auto make_fixture(TerrainTileCache& cache)
    -> std::expected<Fixture, IntersystemPlanetfallAcceptanceError> {
  auto identities = generate_first_intersystem_identities(
      Seed{kIntersystemPlanetfallAcceptanceSeed});
  auto system = generate_local_system(identities.target_system_seed);
  const auto body = find_local_system_planet(system, identities.target_planet);
  if (!body) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::initialization_failure};
  }
  auto catalog = generate_surface_signals((*body)->descriptor, cache);
  if (!catalog || catalog->signals.front().id != identities.target_objective) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::initialization_failure};
  }
  return Fixture{identities, (*body)->descriptor, std::move(*catalog)};
}

[[nodiscard]] auto environment_at(const PlanetDescriptor& planet,
                                  GeodeticPosition position,
                                  TerrainTileCache& cache)
    -> std::expected<PlanetaryFlightEnvironment,
                     IntersystemPlanetfallAcceptanceError> {
  const auto fixed = planet_fixed_from_geodetic(
      planet, {position.latitude_radians, position.longitude_radians, 0.0});
  const auto sampled =
      fixed ? sample_planet_surface(planet, *fixed, kSurfaceSignalPlacementLod,
                                    cache)
            : std::expected<TerrainSurfaceSample, TerrainTileError>{
                  std::unexpected{TerrainTileError::coordinate_failure}};
  if (!sampled) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::initialization_failure};
  }
  return PlanetaryFlightEnvironment{sampled->elevation_metres};
}

[[nodiscard]] auto target_position(const Fixture& fixture)
    -> std::expected<GeodeticPosition, IntersystemPlanetfallAcceptanceError> {
  const auto& target = fixture.catalog.signals.front();
  const auto fixed = planet_fixed_from_terrain_address(
      fixture.planet, target.anchor,
      static_cast<double>(target.approach_altitude_metres));
  const auto position =
      fixed ? geodetic_from_planet_fixed(fixture.planet, *fixed)
            : std::expected<GeodeticPosition, CoordinateError>{
                  std::unexpected{CoordinateError::non_finite_input}};
  if (!position) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::initialization_failure};
  }
  return *position;
}

[[nodiscard]] auto thermal_reentry_measurement()
    -> std::expected<PilotThermalAcceptanceMeasurement,
                     IntersystemPlanetfallAcceptanceError> {
  const Seed universe_seed{kPilotThermalAcceptanceSeed};
  const auto identities = generate_first_intersystem_identities(universe_seed);
  const auto system = generate_local_system(identities.target_system_seed);
  const auto body = find_local_system_planet(system, identities.target_planet);
  if (!body || (*body)->descriptor.atmosphere_class != AtmosphereClass::dense ||
      (*body)->descriptor.atmosphere_pressure.value < 2'200U) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::initialization_failure};
  }
  const PlanetDescriptor& dense = (*body)->descriptor;
  auto contract = initial_intersystem_contract_state(universe_seed);
  const auto command = [&](IntersystemContractCommand value) {
    return advance_intersystem_contract(contract, contract.universe_tick,
                                        value);
  };
  if (!command(IntersystemContractCommand::select_pilot_profile) ||
      !command(IntersystemContractCommand::accept_mission) ||
      !command(IntersystemContractCommand::launch) ||
      !begin_intersystem_jump(contract)) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::initialization_failure};
  }
  for (SimulationTick tick = 0;
       tick < kJumpSpoolTicks + kJumpTransitTicks; ++tick) {
    if (!advance_intersystem_jump_tick(contract, system)) {
      return std::unexpected{
          IntersystemPlanetfallAcceptanceError::initialization_failure};
    }
  }
  if (!command(IntersystemContractCommand::enter_target_planet)) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::initialization_failure};
  }
  const PlanetaryFlightEnvironment environment{};
  const auto initial = [&](double altitude, FlightMode mode) {
    return initial_planetary_flight_state(
        dense, {0.125, -0.25, altitude}, environment, 0.0, mode);
  };
  const auto advance = [&](PlanetaryFlightState& state,
                           std::span<const FlightCommand> commands,
                           bool pilot) {
    return advance_planetary_flight(
        dense, environment, state, commands, kSimulationStep,
        {.enforce_thermal_abort = pilot});
  };

  auto nominal = initial(40'000.0, FlightMode::manual);
  auto shallow = initial(20'000.0, FlightMode::autopilot);
  auto manual_correction = initial(40'000.0, FlightMode::manual);
  auto assisted = initial(40'000.0, FlightMode::manual);
  auto forced = initial(40'000.0, FlightMode::manual);
  if (!nominal || !shallow || !manual_correction || !assisted || !forced) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::initialization_failure};
  }
  nominal->tick = contract.universe_tick;
  shallow->tick = contract.universe_tick;
  manual_correction->tick = contract.universe_tick;
  assisted->tick = contract.universe_tick;
  forced->tick = contract.universe_tick;

  nominal->velocity = {250.0, 0.0, -150.0};
  std::uint32_t nominal_peak{};
  for (int tick = 0; tick < 1'200; ++tick) {
    if (!advance(*nominal, {}, true)) {
      return std::unexpected{
          IntersystemPlanetfallAcceptanceError::simulation_failure};
    }
    nominal_peak = std::max(nominal_peak, nominal->thermal.load_units);
  }

  shallow->velocity = {360.0, 0.0, -25.0};
  std::uint32_t shallow_peak{};
  for (int tick = 0; tick < 3'600; ++tick) {
    if (!advance(*shallow, {}, true)) {
      return std::unexpected{
          IntersystemPlanetfallAcceptanceError::simulation_failure};
    }
    shallow_peak = std::max(shallow_peak, shallow->thermal.load_units);
  }

  manual_correction->velocity = {400.0, 0.0, -700.0};
  manual_correction->controls.fall = true;
  std::uint32_t manual_correction_peak{};
  for (int tick = 0; tick < 2'400; ++tick) {
    std::array<FlightCommand, 1> storage{};
    std::span<const FlightCommand> commands;
    if (tick == 15) {
      storage = {{{manual_correction->tick, FlightCommandKind::release_fall}}};
      commands = storage;
    }
    if (!advance(*manual_correction, commands, true)) {
      return std::unexpected{
          IntersystemPlanetfallAcceptanceError::simulation_failure};
    }
    manual_correction_peak = std::max(
        manual_correction_peak, manual_correction->thermal.load_units);
    if (manual_correction->thermal.abort_latched) {
      return std::unexpected{
          IntersystemPlanetfallAcceptanceError::incomplete_path};
    }
  }

  assisted->velocity = {400.0, 0.0, -1'500.0};
  assisted->controls.fall = true;
  std::uint32_t assisted_peak{};
  for (int tick = 0; tick < 240; ++tick) {
    if (!advance(*assisted, {}, false)) {
      return std::unexpected{
          IntersystemPlanetfallAcceptanceError::simulation_failure};
    }
    assisted_peak = std::max(assisted_peak, assisted->thermal.load_units);
  }
  if (assisted_peak != kMaximumThermalLoadUnits ||
      assisted->thermal.abort_latched) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::incomplete_path};
  }

  forced->velocity = {400.0, 0.0, -1'500.0};
  forced->controls.fall = true;
  SimulationTick forced_abort_tick{};
  for (int tick = 0; tick < 2'000 && forced_abort_tick == 0; ++tick) {
    if (!advance(*forced, {}, true)) {
      return std::unexpected{
          IntersystemPlanetfallAcceptanceError::simulation_failure};
    }
    if (forced->thermal.abort_latched) forced_abort_tick = forced->tick;
  }
  if (forced_abort_tick == 0) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::incomplete_path};
  }
  contract.universe_tick = forced->tick;
  auto document = make_new_game_document(universe_seed);
  document.state.intersystem_contract = contract;
  document.state.flight = *forced;
  const auto encoded = encode_save_document_json(document);
  const auto decoded = encoded
                           ? decode_save_document_json(*encoded)
                           : std::expected<SaveDocument, SaveSchemaError>{
                                 std::unexpected{SaveSchemaError{}}};
  if (!decoded || !decoded->state.flight ||
      decoded->state.flight->thermal != forced->thermal) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::save_failure};
  }
  auto resumed = *decoded->state.flight;
  SimulationTick recovery_tick{};
  for (int tick = 0; tick < 30'000 && recovery_tick == 0; ++tick) {
    if (!advance(*forced, {}, true) || !advance(resumed, {}, true)) {
      return std::unexpected{
          IntersystemPlanetfallAcceptanceError::simulation_failure};
    }
    if (*forced != resumed) {
      return std::unexpected{
          IntersystemPlanetfallAcceptanceError::cadence_mismatch};
    }
    if (forced->regime == FlightRegime::orbital &&
        !forced->thermal.abort_latched) {
      recovery_tick = forced->tick;
    }
  }
  if (recovery_tick == 0 || forced->controls.fall) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::incomplete_path};
  }
  const std::uint64_t recovery_checksum =
      planetary_flight_state_checksum(resumed);

  const std::array reentry_command{
      FlightCommand{forced->tick, FlightCommandKind::press_fall}};
  if (!advance(*forced, reentry_command, true)) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::simulation_failure};
  }
  SimulationTick reentry_tick{};
  for (int tick = 0; tick < 20'000 && reentry_tick == 0; ++tick) {
    if (!advance(*forced, {}, true)) {
      return std::unexpected{
          IntersystemPlanetfallAcceptanceError::simulation_failure};
    }
    if (forced->regime == FlightRegime::atmospheric) {
      reentry_tick = forced->tick;
    }
  }
  if (reentry_tick == 0) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::incomplete_path};
  }

  return PilotThermalAcceptanceMeasurement{
      .universe_seed = universe_seed,
      .planet = dense.id,
      .nominal_peak_load_units = nominal_peak,
      .shallow_peak_load_units = shallow_peak,
      .manual_correction_peak_load_units = manual_correction_peak,
      .assisted_peak_load_units = assisted_peak,
      .forced_abort_tick = forced_abort_tick,
      .recovery_orbit_tick = recovery_tick,
      .deliberate_reentry_tick = reentry_tick,
      .resumed_recovery_checksum = recovery_checksum,
  };
}

[[nodiscard]] auto initial_contract()
    -> std::expected<IntersystemContractState,
                     IntersystemPlanetfallAcceptanceError> {
  auto contract = initial_intersystem_contract_state(
      Seed{kIntersystemPlanetfallAcceptanceSeed});
  if (!advance_intersystem_contract(
          contract, contract.universe_tick,
          IntersystemContractCommand::accept_mission) ||
      !advance_intersystem_contract(contract, contract.universe_tick,
                                    IntersystemContractCommand::launch) ||
      !begin_intersystem_jump(contract)) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::initialization_failure};
  }
  const auto system =
      generate_local_system(contract.identities.target_system_seed);
  for (SimulationTick tick = 0;
       tick < kJumpSpoolTicks + kJumpTransitTicks; ++tick) {
    if (!advance_intersystem_jump_tick(contract, system)) {
      return std::unexpected{
          IntersystemPlanetfallAcceptanceError::initialization_failure};
    }
  }
  if (!advance_intersystem_contract(
          contract, contract.universe_tick,
          IntersystemContractCommand::enter_target_planet)) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::initialization_failure};
  }
  return contract;
}

[[nodiscard]] auto initial_planetfall(const Fixture& fixture,
                                      GeodeticPosition position,
                                      TerrainTileCache& cache)
    -> std::expected<IntersystemPlanetfallState,
                     IntersystemPlanetfallAcceptanceError> {
  const auto environment = environment_at(fixture.planet, position, cache);
  auto flight =
      environment
          ? initial_planetary_flight_state(
                fixture.planet, position, *environment, 0.0, FlightMode::manual)
          : std::expected<PlanetaryFlightState, PlanetaryFlightError>{
                std::unexpected{PlanetaryFlightError::invalid_environment}};
  if (!flight) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::initialization_failure};
  }
  flight->tick = kJumpSpoolTicks + kJumpTransitTicks;
  auto result = initialize_intersystem_planetfall(
      fixture.planet, fixture.identities.target_objective, *flight, {}, cache);
  if (!result) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::initialization_failure};
  }
  return std::move(*result);
}

[[nodiscard]] auto descend_to_terrain(const Fixture& fixture,
                                      IntersystemPlanetfallState state,
                                      TerrainTileCache& cache,
                                      bool checkpoint_resume)
    -> std::expected<IntersystemPlanetfallState,
                     IntersystemPlanetfallAcceptanceError> {
  bool pressed{};
  bool resumed{};
  constexpr SimulationTick maximum_ticks{80'000};
  for (SimulationTick step = 0; step < maximum_ticks; ++step) {
    if (state.flight.regime == FlightRegime::terrain_flight) return state;
    std::array<FlightCommand, 1> storage{};
    std::span<const FlightCommand> commands;
    if (!pressed) {
      storage.front() = {state.flight.tick, FlightCommandKind::press_fall};
      commands = storage;
      pressed = true;
    }
    if (!advance_intersystem_planetfall(
            state, cache, commands, IntersystemRuleProfile::assisted)) {
      return std::unexpected{
          IntersystemPlanetfallAcceptanceError::simulation_failure};
    }
    if (checkpoint_resume && !resumed &&
        state.flight.regime == FlightRegime::atmospheric) {
      auto contract = initial_contract();
      if (!contract) return std::unexpected{contract.error()};
      contract->universe_tick = state.flight.tick;
      auto document =
          make_new_game_document(Seed{kIntersystemPlanetfallAcceptanceSeed});
      document.state.intersystem_contract = *contract;
      document.state.flight = state.flight;
      const auto encoded = encode_save_document_json(document);
      const auto decoded = encoded
                               ? decode_save_document_json(*encoded)
                               : std::expected<SaveDocument, SaveSchemaError>{
                                     std::unexpected{SaveSchemaError{}}};
      auto restored_cache = TerrainTileCache::create();
      auto restored =
          decoded && restored_cache && decoded->state.flight
              ? initialize_intersystem_planetfall(
                    fixture.planet, fixture.identities.target_objective,
                    *decoded->state.flight, decoded->state.world_deltas,
                    *restored_cache)
              : std::expected<IntersystemPlanetfallState,
                              IntersystemPlanetfallError>{
                    std::unexpected{IntersystemPlanetfallError::invalid_state}};
      if (!restored || restored->flight != state.flight ||
          restored->scanner.selected != state.scanner.selected ||
          restored->navigation != state.navigation) {
        return std::unexpected{
            IntersystemPlanetfallAcceptanceError::save_failure};
      }
      cache = std::move(*restored_cache);
      state = std::move(*restored);
      resumed = true;
    }
  }
  return std::unexpected{IntersystemPlanetfallAcceptanceError::incomplete_path};
}

[[nodiscard]] auto terrain_measurement(std::string name,
                                       GeodeticPosition initial,
                                       const IntersystemPlanetfallState& state,
                                       TerrainTileCache& cache)
    -> std::expected<IntersystemPlanetfallEntryMeasurement,
                     IntersystemPlanetfallAcceptanceError> {
  const auto fixed = planet_fixed_from_geodetic(
      *state.planet, {state.flight.pose.position.latitude_radians,
                      state.flight.pose.position.longitude_radians, 0.0});
  const auto address =
      fixed ? terrain_address_from_planet_fixed(*state.planet, *fixed,
                                                kSurfaceSignalPlacementLod)
            : std::expected<TerrainTileAddress, CoordinateError>{
                  std::unexpected{CoordinateError::non_finite_input}};
  if (!address || !cache.get(*state.planet, address->tile)) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::simulation_failure};
  }
  return IntersystemPlanetfallEntryMeasurement{
      .name = std::move(name),
      .initial_position = initial,
      .terrain_position = state.flight.pose.position,
      .terrain_tick = state.flight.tick,
      .flight_checksum = planetary_flight_state_checksum(state.flight),
      .terrain_anchor = *address,
  };
}

[[nodiscard]] auto replay(int width, int height)
    -> std::expected<Replay, IntersystemPlanetfallAcceptanceError> {
  auto cache = TerrainTileCache::create();
  if (!cache) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::initialization_failure};
  }
  auto fixture = make_fixture(*cache);
  const auto target =
      fixture
          ? target_position(*fixture)
          : std::expected<GeodeticPosition,
                          IntersystemPlanetfallAcceptanceError>{std::unexpected{
                IntersystemPlanetfallAcceptanceError::initialization_failure}};
  if (!fixture || !target) {
    return std::unexpected{fixture ? target.error() : fixture.error()};
  }
  const auto bands = flight_regime_bands(fixture->planet);
  if (!bands) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::initialization_failure};
  }
  const double entry_altitude =
      bands->atmosphere_enter_altitude_metres + 20'000.0;
  std::array<GeodeticPosition, 3> positions{
      GeodeticPosition{target->latitude_radians, target->longitude_radians,
                       entry_altitude},
      GeodeticPosition{target->latitude_radians,
                       canonical_longitude(target->longitude_radians +
                                           std::numbers::pi_v<double> / 2.0),
                       entry_altitude},
      GeodeticPosition{-target->latitude_radians,
                       canonical_longitude(target->longitude_radians +
                                           std::numbers::pi_v<double>),
                       entry_altitude},
  };
  constexpr std::array<std::string_view, 3> names{"correct-side", "early",
                                                  "opposite-side"};
  std::array<IntersystemPlanetfallEntryMeasurement, 3> entries;
  for (std::size_t index = 0; index < positions.size(); ++index) {
    auto state = initial_planetfall(*fixture, positions[index], *cache);
    auto descended =
        state ? descend_to_terrain(*fixture, std::move(*state), *cache,
                                   index == 1U)
              : std::expected<IntersystemPlanetfallState,
                              IntersystemPlanetfallAcceptanceError>{
                    std::unexpected{IntersystemPlanetfallAcceptanceError::
                                        initialization_failure}};
    const auto measured =
        descended
            ? terrain_measurement(std::string{names[index]}, positions[index],
                                  *descended, *cache)
            : std::expected<
                  IntersystemPlanetfallEntryMeasurement,
                  IntersystemPlanetfallAcceptanceError>{std::unexpected{
                  IntersystemPlanetfallAcceptanceError::simulation_failure}};
    if (!measured ||
        descended->scanner.selected != fixture->identities.target_objective) {
      return std::unexpected{
          IntersystemPlanetfallAcceptanceError::incomplete_path};
    }
    entries[index] = *measured;
  }

  auto abort_state = initial_planetfall(*fixture, positions[1], *cache);
  if (!abort_state) return std::unexpected{abort_state.error()};
  const FlightCommand descend{abort_state->flight.tick,
                              FlightCommandKind::press_fall};
  if (!advance_intersystem_planetfall(
          *abort_state, *cache, std::span<const FlightCommand>{&descend, 1},
          IntersystemRuleProfile::assisted)) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::simulation_failure};
  }
  constexpr SimulationTick maximum_abort_ticks{100'000};
  bool climbing{};
  for (SimulationTick tick = 0; tick < maximum_abort_ticks; ++tick) {
    if (climbing && abort_state->flight.regime == FlightRegime::orbital) break;
    std::array<FlightCommand, 2> storage{};
    std::span<const FlightCommand> commands;
    if (!climbing && abort_state->flight.regime == FlightRegime::atmospheric) {
      storage = {{{abort_state->flight.tick, FlightCommandKind::release_fall},
                  {abort_state->flight.tick, FlightCommandKind::press_rise}}};
      commands = storage;
      climbing = true;
    }
    if (!advance_intersystem_planetfall(
            *abort_state, *cache, commands, IntersystemRuleProfile::assisted)) {
      return std::unexpected{
          IntersystemPlanetfallAcceptanceError::simulation_failure};
    }
  }
  if (!climbing || abort_state->flight.regime != FlightRegime::orbital ||
      abort_state->scanner.selected != fixture->identities.target_objective) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::incomplete_path};
  }

  const auto thermal = thermal_reentry_measurement();
  if (!thermal) return std::unexpected{thermal.error()};

  auto completed = initial_planetfall(*fixture, *target, *cache);
  if (!completed) return std::unexpected{completed.error()};
  constexpr SimulationTick collection_limit{2 * kSimulationHz * 10};
  for (SimulationTick tick = 0;
       tick < collection_limit &&
       completed->collection.status != SignalCollectionStatus::complete;
       ++tick) {
    if (!advance_intersystem_planetfall(
            *completed, *cache, {}, IntersystemRuleProfile::assisted)) {
      return std::unexpected{
          IntersystemPlanetfallAcceptanceError::simulation_failure};
    }
  }
  if (completed->collection.status != SignalCollectionStatus::complete ||
      completed->journal.entries().size() != 1U) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::incomplete_path};
  }
  PlanetaryPresentationRenderer renderer{{.width = width, .height = height}};
  std::vector<termforge::Pixel> frame(static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(height));
  const auto rendered = renderer.render(*completed->planet, completed->flight,
                                        {.pitch_radians = -0.18}, frame);
  if (!rendered) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::presentation_failure};
  }
  const auto checksum = pixel_checksum(frame);
  return Replay{
      .entries = std::move(entries),
      .completed = std::move(*completed),
      .abort_orbit_tick = abort_state->flight.tick,
      .abort_orbit_checksum =
          planetary_flight_state_checksum(abort_state->flight),
      .thermal = *thermal,
      .frame = std::move(frame),
      .framebuffer_checksum = checksum,
  };
}

}  // namespace

auto run_intersystem_planetfall_acceptance(int width, int height)
    -> std::expected<IntersystemPlanetfallAcceptanceResult,
                     IntersystemPlanetfallAcceptanceError> {
  if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) >
          4'194'304ULL) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::invalid_configuration};
  }
  auto first = replay(width, height);
  auto second = replay(width, height);
  if (!first || !second) {
    return std::unexpected{first ? second.error() : first.error()};
  }
  if (first->entries != second->entries ||
      first->completed.flight != second->completed.flight ||
      first->completed.navigation != second->completed.navigation ||
      !std::ranges::equal(first->completed.journal.entries(),
                          second->completed.journal.entries()) ||
      first->abort_orbit_tick != second->abort_orbit_tick ||
      first->abort_orbit_checksum != second->abort_orbit_checksum ||
      first->thermal != second->thermal ||
      first->frame != second->frame) {
    return std::unexpected{
        IntersystemPlanetfallAcceptanceError::cadence_mismatch};
  }
  return IntersystemPlanetfallAcceptanceResult{
      .report = {.planet = first->completed.planet->id,
                 .target = *first->completed.scanner.selected,
                 .entries = first->entries,
                 .abort_orbit_tick = first->abort_orbit_tick,
                 .abort_orbit_checksum = first->abort_orbit_checksum,
                 .completion_tick =
                     first->completed.collection.completion_tick.value_or(0),
                 .completed_flight_checksum =
                     planetary_flight_state_checksum(first->completed.flight),
                 .world_delta_count = first->completed.journal.entries().size(),
                 .thermal = first->thermal,
                 .framebuffer_checksum = first->framebuffer_checksum},
      .final_frame = std::move(first->frame),
  };
}

auto intersystem_planetfall_acceptance_json(
    const IntersystemPlanetfallAcceptanceReport& report) -> std::string {
  std::string entries;
  for (std::size_t index = 0; index < report.entries.size(); ++index) {
    const auto& entry = report.entries[index];
    entries += std::format(
        "    {{\"name\": \"{}\", \"terrain_tick\": \"{}\", "
        "\"flight_checksum\": \"{}\", \"latitude_radians\": {:.17g}, "
        "\"longitude_radians\": {:.17g}}}{}\n",
        entry.name, entry.terrain_tick, entry.flight_checksum,
        entry.terrain_position.latitude_radians,
        entry.terrain_position.longitude_radians,
        index + 1U == report.entries.size() ? "" : ",");
  }
  return std::format(
      "{{\n"
      "  \"schema_version\": 3,\n"
      "  \"scenario\": \"{}\",\n"
      "  \"evidence_scope\": \"application_framebuffer\",\n"
      "  \"planet_id\": \"planet-{:016x}\",\n"
      "  \"target_id\": \"{}\",\n"
      "  \"entries\": [\n{}  ],\n"
      "  \"abort_orbit_tick\": \"{}\",\n"
      "  \"abort_orbit_checksum\": \"{}\",\n"
      "  \"completion_tick\": \"{}\",\n"
      "  \"completed_flight_checksum\": \"{}\",\n"
      "  \"world_delta_count\": {},\n"
      "  \"thermal\": {{\"universe_seed\": \"{}\", "
      "\"planet_id\": \"planet-{:016x}\", "
      "\"nominal_peak_load_units\": {}, "
      "\"shallow_peak_load_units\": {}, "
      "\"manual_correction_peak_load_units\": {}, "
      "\"assisted_peak_load_units\": {}, "
      "\"forced_abort_tick\": \"{}\", "
      "\"recovery_orbit_tick\": \"{}\", "
      "\"deliberate_reentry_tick\": \"{}\", "
      "\"resumed_recovery_checksum\": \"{}\"}},\n"
      "  \"framebuffer_checksum\": \"{}\"\n"
      "}}\n",
      kIntersystemPlanetfallAcceptanceScenario,
      report.planet.value, surface_signal_id_string(report.target), entries,
      report.abort_orbit_tick, report.abort_orbit_checksum,
      report.completion_tick, report.completed_flight_checksum,
      report.world_delta_count, report.thermal.universe_seed.value,
      report.thermal.planet.value,
      report.thermal.nominal_peak_load_units,
      report.thermal.shallow_peak_load_units,
      report.thermal.manual_correction_peak_load_units,
      report.thermal.assisted_peak_load_units,
      report.thermal.forced_abort_tick,
      report.thermal.recovery_orbit_tick,
      report.thermal.deliberate_reentry_tick,
      report.thermal.resumed_recovery_checksum,
      report.framebuffer_checksum);
}

}  // namespace apsis_drift
