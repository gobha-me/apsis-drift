#include "apsis_drift/signal_run.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "apsis_drift/coordinates.hpp"
#include "apsis_drift/seed.hpp"

namespace apsis_drift {
namespace {

[[nodiscard]] auto regenerated_planet(const SaveRecipe& recipe)
    -> PlanetDescriptor {
  const auto system = derive_seed(recipe.universe_seed, SeedDomain::system,
                                  recipe.origin_system_ordinal);
  const auto planet = derive_seed(system, SeedDomain::planet,
                                  recipe.active_planet_ordinal);
  return generate_planet_descriptor(planet);
}

[[nodiscard]] auto signal_for(const SurfaceSignalCatalog& catalog,
                              SurfaceSignalId id) noexcept
    -> const SurfaceSignal* {
  const auto found = std::ranges::find(catalog.signals, id,
                                       &SurfaceSignal::id);
  return found == catalog.signals.end() ? nullptr : &*found;
}

[[nodiscard]] auto surface_environment(
    const PlanetDescriptor& planet, const PlanetaryFlightState& flight,
    TerrainTileCache& cache)
    -> std::expected<PlanetaryFlightEnvironment, SignalRunError> {
  const auto fixed = planet_fixed_from_geodetic(
      planet, {flight.pose.position.latitude_radians,
               flight.pose.position.longitude_radians, 0.0});
  if (!fixed) return std::unexpected{SignalRunError::terrain_failure};
  const auto sample = sample_planet_surface(
      planet, *fixed, kSurfaceSignalPlacementLod, cache);
  if (!sample) return std::unexpected{SignalRunError::terrain_failure};
  return PlanetaryFlightEnvironment{sample->elevation_metres};
}

[[nodiscard]] auto derive_rendezvous(const SignalRunState& state)
    -> std::expected<OriginRendezvous, SignalRunError> {
  if (!state.scanner.selected) {
    return std::unexpected{SignalRunError::invalid_target};
  }
  const auto* target = signal_for(state.catalog, *state.scanner.selected);
  if (target == nullptr) {
    return std::unexpected{SignalRunError::invalid_target};
  }
  const auto target_fixed = planet_fixed_from_terrain_address(
      *state.planet, target->anchor, 0.0);
  const auto target_position =
      target_fixed ? geodetic_from_planet_fixed(*state.planet, *target_fixed)
                   : std::expected<GeodeticPosition, CoordinateError>{
                         std::unexpected{CoordinateError::non_finite_input}};
  const auto bands = flight_regime_bands(*state.planet);
  if (!target_position || !bands) {
    return std::unexpected{SignalRunError::navigation_failure};
  }
  const double angle =
      static_cast<double>(state.recipe.origin_station.value >> 11U) *
      (2.0 * std::numbers::pi_v<double> / 9'007'199'254'740'992.0);
  GeodeticPosition orbital = *target_position;
  orbital.altitude_metres = bands->orbit_enter_altitude_metres +
                            kOriginRendezvousAboveOrbitMetres;
  const auto frame = make_local_tangent_frame(*state.planet, orbital);
  const auto fixed = frame ? planet_fixed_from_local(
                                 *frame,
                                 {std::cos(angle) *
                                      kOriginRendezvousOffsetMetres,
                                  std::sin(angle) *
                                      kOriginRendezvousOffsetMetres,
                                  0.0})
                           : std::expected<PlanetFixedPositionMetres,
                                           CoordinateError>{std::unexpected{
                                 CoordinateError::non_finite_input}};
  const auto position =
      fixed ? geodetic_from_planet_fixed(*state.planet, *fixed)
            : std::expected<GeodeticPosition, CoordinateError>{
                  std::unexpected{CoordinateError::non_finite_input}};
  if (!position) {
    return std::unexpected{SignalRunError::navigation_failure};
  }
  return OriginRendezvous{*position,
                          kOriginRendezvousArrivalRadiusMetres};
}

[[nodiscard]] auto local_navigation(const PlanetDescriptor& planet,
                                    const PlanetaryFlightState& flight,
                                    GeodeticPosition target,
                                    double arrival_radius)
    -> std::expected<OriginNavigationSolution, SignalRunError> {
  const auto frame = make_local_tangent_frame(planet, flight.pose.position);
  const auto fixed = planet_fixed_from_geodetic(planet, target);
  const auto local = frame && fixed
                         ? local_from_planet_fixed(*frame, *fixed)
                         : std::expected<LocalPositionMetres, CoordinateError>{
                               std::unexpected{
                                   CoordinateError::non_finite_input}};
  if (!local) {
    return std::unexpected{SignalRunError::navigation_failure};
  }
  const double bearing = std::atan2(local->north, local->east);
  const double distance = std::hypot(local->east, local->north, local->up);
  double relative = bearing - flight.pose.heading_radians;
  relative = std::remainder(relative, 2.0 * std::numbers::pi_v<double>);
  if (!std::isfinite(distance) || !std::isfinite(relative)) {
    return std::unexpected{SignalRunError::navigation_failure};
  }
  const auto motion = resolve_target_relative_motion(
      planet, flight, *local, arrival_radius);
  if (!motion) {
    return std::unexpected{SignalRunError::navigation_failure};
  }
  return OriginNavigationSolution{
      .absolute_bearing_radians = bearing,
      .relative_bearing_radians = relative,
      .distance_metres = distance,
      .motion = *motion,
      .arrived = distance <= arrival_radius,
  };
}

[[nodiscard]] auto consistent_loaded_state(const SignalRunState& state)
    -> bool {
  const auto status = state.onboarding.first_objective;
  const bool has_target = state.scanner.selected.has_value();
  if (status == FirstObjectiveStatus::offered) {
    return !has_target && state.onboarding.location ==
                              OriginLocation::docked_at_origin &&
           !state.flight && state.discoveries.empty() &&
           state.journal.entries().empty();
  }
  if (!has_target || signal_for(state.catalog, *state.scanner.selected) ==
                         nullptr) {
    return false;
  }
  const bool target_discovered = std::ranges::any_of(
      state.discoveries, [&](const SaveDiscovery& discovery) {
        return discovery.signal == *state.scanner.selected;
      });
  if (!target_discovered) return false;
  const auto target_key = surface_signal_object_key(*state.scanner.selected);
  const auto* delta = state.journal.state(target_key);
  const bool collected = delta != nullptr &&
                         delta->kind == SaveWorldDeltaKind::collected;
  if (status == FirstObjectiveStatus::completed && !collected) return false;
  if (status == FirstObjectiveStatus::active && collected) return false;
  if (state.onboarding.location == OriginLocation::in_flight) {
    return state.flight.has_value();
  }
  return !state.flight;
}

}  // namespace

auto signal_run_error_name(SignalRunError error) noexcept -> std::string_view {
  switch (error) {
    case SignalRunError::invalid_document: return "invalid_document";
    case SignalRunError::terrain_failure: return "terrain_failure";
    case SignalRunError::invalid_target: return "invalid_target";
    case SignalRunError::inconsistent_state: return "inconsistent_state";
    case SignalRunError::invalid_transition: return "invalid_transition";
    case SignalRunError::flight_failure: return "flight_failure";
    case SignalRunError::navigation_failure: return "navigation_failure";
    case SignalRunError::collection_failure: return "collection_failure";
    case SignalRunError::journal_failure: return "journal_failure";
  }
  return "unknown";
}

auto hydrate_signal_run(const SaveDocument& document, TerrainTileCache& cache)
    -> std::expected<SignalRunState, SignalRunError> {
  if (!validate_save_document(document)) {
    return std::unexpected{SignalRunError::invalid_document};
  }
  auto journal = WorldDeltaJournal::create(document.state.world_deltas);
  if (!journal) return std::unexpected{SignalRunError::journal_failure};
  auto planet = std::make_shared<const PlanetDescriptor>(
      regenerated_planet(document.recipe));
  auto catalog = generate_surface_signals(*planet, cache);
  if (!catalog) return std::unexpected{SignalRunError::terrain_failure};
  if (!apply_world_delta_journal(*catalog, *journal)) {
    return std::unexpected{SignalRunError::journal_failure};
  }
  for (const auto& discovery : document.state.discoveries) {
    if (signal_for(*catalog, discovery.signal) == nullptr) {
      return std::unexpected{SignalRunError::invalid_target};
    }
  }

  SignalRunState state{
      .recipe = document.recipe,
      .onboarding = {document.recipe.origin_station,
                     document.state.location,
                     document.state.first_objective},
      .planet = std::move(planet),
      .catalog = std::move(*catalog),
      .rendezvous = std::nullopt,
      .flight = document.state.flight,
      .scanner = {},
      .signal_navigation = {},
      .origin_navigation = std::nullopt,
      .journal = std::move(*journal),
      .collection = {},
      .discoveries = document.state.discoveries,
  };
  if (document.state.first_objective != FirstObjectiveStatus::offered) {
    state.scanner.selected = document.state.first_objective_target;
    auto rendezvous = derive_rendezvous(state);
    if (!rendezvous) return std::unexpected{rendezvous.error()};
    state.rendezvous = *rendezvous;
  } else if (document.state.first_objective_target.value != 0) {
    return std::unexpected{SignalRunError::inconsistent_state};
  }
  if (!consistent_loaded_state(state)) {
    return std::unexpected{SignalRunError::inconsistent_state};
  }
  if (state.flight) {
    auto navigation = resolve_signal_navigation(
        *state.planet, state.catalog, *state.flight, state.scanner);
    if (!navigation) {
      return std::unexpected{SignalRunError::navigation_failure};
    }
    state.signal_navigation = *navigation;
    if (state.onboarding.first_objective ==
        FirstObjectiveStatus::completed) {
      state.collection = SignalCollectionState{
          .status = SignalCollectionStatus::complete,
          .target = state.scanner.selected,
          .consecutive_in_range_ticks =
              kSignalCollectionTotalInRangeTicks,
          .last_tick = state.flight->tick,
          .completion_tick = state.journal
                                 .state(surface_signal_object_key(
                                     *state.scanner.selected))
                                 ->tick,
      };
      auto origin = local_navigation(*state.planet, *state.flight,
                                     state.rendezvous->position,
                                     state.rendezvous->arrival_radius_metres);
      if (!origin) return std::unexpected{origin.error()};
      state.origin_navigation = *origin;
    }
  }
  return state;
}

auto accept_signal_run(SignalRunState& state)
    -> std::expected<void, SignalRunError> {
  auto next = state;
  if (!advance_origin_onboarding(
          next.onboarding,
          OriginOnboardingCommand::accept_first_objective)) {
    return std::unexpected{SignalRunError::invalid_transition};
  }
  next.scanner.selected = next.catalog.signals.front().id;
  next.discoveries.push_back({*next.scanner.selected, 0});
  auto rendezvous = derive_rendezvous(next);
  if (!rendezvous) {
    return std::unexpected{rendezvous.error()};
  }
  next.rendezvous = *rendezvous;
  state = std::move(next);
  return {};
}

auto launch_signal_run(SignalRunState& state, TerrainTileCache& cache)
    -> std::expected<void, SignalRunError> {
  if (!state.rendezvous) {
    return std::unexpected{SignalRunError::invalid_transition};
  }
  const auto fixed = planet_fixed_from_geodetic(
      *state.planet,
      {state.rendezvous->position.latitude_radians,
       state.rendezvous->position.longitude_radians, 0.0});
  const auto sample = fixed ? sample_planet_surface(
                                  *state.planet, *fixed,
                                  kSurfaceSignalPlacementLod, cache)
                            : std::expected<TerrainSurfaceSample,
                                            TerrainTileError>{
                                  std::unexpected{
                                      TerrainTileError::coordinate_failure}};
  if (!sample) return std::unexpected{SignalRunError::terrain_failure};
  auto flight = initial_planetary_flight_state(
      *state.planet, state.rendezvous->position,
      {sample->elevation_metres}, 0.0, FlightMode::manual);
  if (!flight) return std::unexpected{SignalRunError::flight_failure};
  auto onboarding = state.onboarding;
  if (!advance_origin_onboarding(onboarding, OriginOnboardingCommand::launch)) {
    return std::unexpected{SignalRunError::invalid_transition};
  }
  auto navigation = resolve_signal_navigation(
      *state.planet, state.catalog, *flight, state.scanner);
  if (!navigation) {
    return std::unexpected{SignalRunError::navigation_failure};
  }
  state.onboarding = onboarding;
  state.flight = *flight;
  state.signal_navigation = *navigation;
  state.collection = {};
  return {};
}

auto select_signal_run_target(SignalRunState& state,
                              SignalSelectionCommand command)
    -> std::expected<void, SignalRunError> {
  if (!state.flight ||
      state.onboarding.first_objective != FirstObjectiveStatus::active) {
    return std::unexpected{SignalRunError::invalid_transition};
  }
  auto scanner = state.scanner;
  if (!advance_signal_selection(state.catalog, scanner, command)) {
    return std::unexpected{SignalRunError::navigation_failure};
  }
  // The first bounded objective cannot be silently retargeted.
  if (scanner.selected != state.scanner.selected) {
    return std::unexpected{SignalRunError::invalid_target};
  }
  return {};
}

auto advance_signal_run(SignalRunState& state, TerrainTileCache& cache,
                        std::span<const FlightCommand> commands)
    -> std::expected<void, SignalRunError> {
  if (!state.flight || state.onboarding.location != OriginLocation::in_flight) {
    return std::unexpected{SignalRunError::invalid_transition};
  }
  auto next = state;
  const auto environment =
      surface_environment(*next.planet, *next.flight, cache);
  if (!environment) {
    return std::unexpected{environment.error()};
  }
  if (!advance_planetary_flight(*next.planet, *environment, *next.flight,
                                commands, kSimulationStep)) {
    return std::unexpected{SignalRunError::flight_failure};
  }
  auto navigation = resolve_signal_navigation(
      *next.planet, next.catalog, *next.flight, next.scanner);
  if (!navigation) {
    return std::unexpected{SignalRunError::navigation_failure};
  }
  next.signal_navigation = *navigation;

  if (next.onboarding.first_objective == FirstObjectiveStatus::active) {
    if (!advance_signal_collection(next.catalog, next.signal_navigation,
                                   next.flight->tick, next.journal,
                                   next.collection)) {
      return std::unexpected{SignalRunError::collection_failure};
    }
    if (next.collection.status == SignalCollectionStatus::complete) {
      if (!advance_origin_onboarding(
              next.onboarding,
              OriginOnboardingCommand::complete_first_objective)) {
        return std::unexpected{SignalRunError::invalid_transition};
      }
    }
  }
  if (next.onboarding.first_objective == FirstObjectiveStatus::completed) {
    auto origin = local_navigation(*next.planet, *next.flight,
                                   next.rendezvous->position,
                                   next.rendezvous->arrival_radius_metres);
    if (!origin) {
      return std::unexpected{origin.error()};
    }
    next.origin_navigation = *origin;
  }
  state = std::move(next);
  return {};
}

auto return_signal_run_to_origin(SignalRunState& state)
    -> std::expected<void, SignalRunError> {
  if (!state.flight || !state.origin_navigation ||
      state.flight->regime != FlightRegime::orbital ||
      !state.origin_navigation->arrived) {
    return std::unexpected{SignalRunError::invalid_transition};
  }
  auto onboarding = state.onboarding;
  if (!advance_origin_onboarding(onboarding,
                                 OriginOnboardingCommand::return_to_origin)) {
    return std::unexpected{SignalRunError::invalid_transition};
  }
  state.onboarding = onboarding;
  state.flight.reset();
  state.origin_navigation.reset();
  return {};
}

auto project_signal_run_save(const SignalRunState& state)
    -> std::expected<SaveDocument, SignalRunError> {
  SaveDocument document{
      .recipe = state.recipe,
      .state =
          SaveMutableState{
              .location = state.onboarding.location,
              .first_objective = state.onboarding.first_objective,
              .first_objective_target =
                  state.scanner.selected.value_or(SurfaceSignalId{}),
              .flight = state.flight,
              .discoveries = state.discoveries,
              .world_deltas = {state.journal.entries().begin(),
                               state.journal.entries().end()},
              .intersystem_contract = std::nullopt,
          },
  };
  if (!consistent_loaded_state(state) || !validate_save_document(document)) {
    return std::unexpected{SignalRunError::inconsistent_state};
  }
  return document;
}

}  // namespace apsis_drift
