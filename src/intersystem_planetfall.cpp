#include "apsis_drift/intersystem_planetfall.hpp"

#include <algorithm>
#include <utility>

#include "apsis_drift/coordinates.hpp"

namespace apsis_drift {
namespace {

[[nodiscard]] auto target_in_catalog(const SurfaceSignalCatalog& catalog,
                                     SurfaceSignalId target) noexcept -> bool {
  return std::ranges::find(catalog.signals, target, &SurfaceSignal::id) !=
         catalog.signals.end();
}

[[nodiscard]] auto valid_mission_journal(const WorldDeltaJournal& journal,
                                         SurfaceSignalId target,
                                         SimulationTick tick) noexcept -> bool {
  if (journal.entries().empty()) return true;
  if (journal.entries().size() != 1U) return false;
  const auto& delta = journal.entries().front();
  const auto parsed = parse_surface_signal_object_key(delta.object_key);
  return parsed && *parsed == target &&
         delta.kind == SaveWorldDeltaKind::collected && delta.tick <= tick;
}

[[nodiscard]] auto valid_collection(const SignalCollectionState& collection,
                                    SurfaceSignalId target,
                                    SimulationTick flight_tick,
                                    bool completed) noexcept -> bool {
  if (collection.last_tick && *collection.last_tick > flight_tick) return false;
  if (completed) {
    return collection.status == SignalCollectionStatus::complete &&
           collection.target == target &&
           collection.consecutive_in_range_ticks ==
               kSignalCollectionTotalInRangeTicks &&
           collection.completion_tick &&
           *collection.completion_tick <= flight_tick;
  }
  if (collection.completion_tick ||
      (collection.target && collection.target != target)) return false;
  switch (collection.status) {
    case SignalCollectionStatus::approach:
    case SignalCollectionStatus::aborted:
      return collection.consecutive_in_range_ticks == 0;
    case SignalCollectionStatus::in_range:
      return collection.target &&
             collection.consecutive_in_range_ticks > 0 &&
             collection.consecutive_in_range_ticks <=
                 kSignalCollectionAcquireTicks;
    case SignalCollectionStatus::scanning:
      return collection.target &&
             collection.consecutive_in_range_ticks >
                 kSignalCollectionAcquireTicks &&
             collection.consecutive_in_range_ticks <
                 kSignalCollectionTotalInRangeTicks;
    case SignalCollectionStatus::complete:
      return false;
  }
  return false;
}

[[nodiscard]] auto surface_environment(const PlanetDescriptor& planet,
                                       const PlanetaryFlightState& flight,
                                       TerrainTileCache& cache)
    -> std::expected<PlanetaryFlightEnvironment, IntersystemPlanetfallError> {
  const auto fixed = planet_fixed_from_geodetic(
      planet, {flight.pose.position.latitude_radians,
               flight.pose.position.longitude_radians, 0.0});
  const auto sample =
      fixed ? sample_planet_surface(planet, *fixed, kSurfaceSignalPlacementLod,
                                    cache)
            : std::expected<TerrainSurfaceSample, TerrainTileError>{
                  std::unexpected{TerrainTileError::coordinate_failure}};
  if (!sample) {
    return std::unexpected{IntersystemPlanetfallError::terrain_failure};
  }
  return PlanetaryFlightEnvironment{sample->elevation_metres};
}

}  // namespace

auto initialize_intersystem_planetfall(
    const PlanetDescriptor& planet, SurfaceSignalId target,
    const PlanetaryFlightState& flight,
    std::span<const SaveWorldDelta> world_deltas, TerrainTileCache& cache)
    -> std::expected<IntersystemPlanetfallState, IntersystemPlanetfallError> {
  if (flight.planet != planet.id ||
      !validate_planetary_flight_state(planet, flight)) {
    return std::unexpected{IntersystemPlanetfallError::invalid_planet};
  }
  auto catalog = generate_surface_signals(planet, cache);
  if (!catalog) {
    return std::unexpected{IntersystemPlanetfallError::terrain_failure};
  }
  if (!target_in_catalog(*catalog, target)) {
    return std::unexpected{IntersystemPlanetfallError::invalid_target};
  }
  auto journal = WorldDeltaJournal::create(world_deltas);
  if (!journal || !valid_mission_journal(*journal, target, flight.tick) ||
      !apply_world_delta_journal(*catalog, *journal)) {
    return std::unexpected{IntersystemPlanetfallError::journal_failure};
  }
  SignalScannerState scanner{.selected = target};
  auto navigation =
      resolve_signal_navigation(planet, *catalog, flight, scanner);
  if (!navigation) {
    return std::unexpected{IntersystemPlanetfallError::navigation_failure};
  }
  SignalCollectionState collection;
  if (!world_deltas.empty()) {
    if (!advance_signal_collection(*catalog, *navigation, flight.tick, *journal,
                                   collection)) {
      return std::unexpected{IntersystemPlanetfallError::collection_failure};
    }
  }
  IntersystemPlanetfallState result{
      .planet = std::make_shared<const PlanetDescriptor>(planet),
      .catalog = std::move(*catalog),
      .flight = flight,
      .scanner = scanner,
      .navigation = *navigation,
      .journal = std::move(*journal),
      .collection = collection,
  };
  if (!validate_intersystem_planetfall_state(result, target)) {
    return std::unexpected{IntersystemPlanetfallError::invalid_state};
  }
  return result;
}

auto validate_intersystem_planetfall_state(
    const IntersystemPlanetfallState& state, SurfaceSignalId target) noexcept
    -> std::expected<void, IntersystemPlanetfallError> {
  if (!state.planet || state.catalog.planet != state.planet->id ||
      state.flight.planet != state.planet->id ||
      state.scanner.selected != target ||
      !target_in_catalog(state.catalog, target) ||
      !valid_mission_journal(state.journal, target, state.flight.tick) ||
      !valid_collection(state.collection, target, state.flight.tick,
                        !state.journal.entries().empty()) ||
      !validate_planetary_flight_state(*state.planet, state.flight)) {
    return std::unexpected{IntersystemPlanetfallError::invalid_state};
  }
  const auto navigation = resolve_signal_navigation(
      *state.planet, state.catalog, state.flight, state.scanner);
  if (!navigation || *navigation != state.navigation) {
    return std::unexpected{IntersystemPlanetfallError::navigation_failure};
  }
  return {};
}

auto advance_intersystem_planetfall(IntersystemPlanetfallState& state,
                                    TerrainTileCache& cache,
                                    std::span<const FlightCommand> commands,
                                    IntersystemRuleProfile profile)
    -> std::expected<IntersystemPlanetfallUpdate, IntersystemPlanetfallError> {
  if ((profile != IntersystemRuleProfile::assisted &&
       profile != IntersystemRuleProfile::pilot) ||
      (profile == IntersystemRuleProfile::assisted &&
       state.flight.thermal.abort_latched)) {
    return std::unexpected{IntersystemPlanetfallError::invalid_state};
  }
  if (!state.scanner.selected ||
      !validate_intersystem_planetfall_state(state, *state.scanner.selected)) {
    return std::unexpected{IntersystemPlanetfallError::invalid_state};
  }
  auto next = state;
  const auto environment =
      surface_environment(*next.planet, next.flight, cache);
  if (!environment) return std::unexpected{environment.error()};
  if (!advance_planetary_flight(
          *next.planet, *environment, next.flight, commands, kSimulationStep,
          {.enforce_thermal_abort =
               profile == IntersystemRuleProfile::pilot})) {
    return std::unexpected{IntersystemPlanetfallError::flight_failure};
  }
  const auto navigation = resolve_signal_navigation(*next.planet, next.catalog,
                                                    next.flight, next.scanner);
  if (!navigation) {
    return std::unexpected{IntersystemPlanetfallError::navigation_failure};
  }
  next.navigation = *navigation;
  const auto collection =
      advance_signal_collection(next.catalog, next.navigation, next.flight.tick,
                                next.journal, next.collection);
  if (!collection) {
    return std::unexpected{IntersystemPlanetfallError::collection_failure};
  }
  if (!validate_intersystem_planetfall_state(next, *next.scanner.selected)) {
    return std::unexpected{IntersystemPlanetfallError::invalid_state};
  }
  state = std::move(next);
  return IntersystemPlanetfallUpdate{.objective_completed =
                                         collection->delta_emitted};
}

}  // namespace apsis_drift
