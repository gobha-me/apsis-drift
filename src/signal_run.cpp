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
  if (recipe.active_planet_ordinal == kOriginHomePlanetOrdinal) {
    return generate_origin_home_planet(system);
  }
  const auto planet =
      derive_seed(system, SeedDomain::planet, recipe.active_planet_ordinal);
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
  const bool guided_contract_one =
      state.career &&
      state.career_onboarding.state == OnboardingState::guided &&
      state.career_onboarding.chapter == OnboardingChapter::contract_one;
  const bool has_target = state.scanner.selected.has_value();
  if (status == FirstObjectiveStatus::offered) {
    return !has_target &&
           state.onboarding.location == OriginLocation::docked_at_origin &&
           !state.flight && !state.station_flight &&
           state.discoveries.empty() && state.journal.entries().empty();
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
  const bool collected =
      delta != nullptr && delta->kind == SaveWorldDeltaKind::collected;
  const bool complete = status == FirstObjectiveStatus::completed ||
                        status == FirstObjectiveStatus::returned ||
                        status == FirstObjectiveStatus::turned_in;
  if (complete && !collected) return false;
  if (status == FirstObjectiveStatus::active && collected) return false;
  if (state.onboarding.location == OriginLocation::in_flight) {
    return state.flight.has_value() != state.station_flight.has_value();
  }
  if (state.flight || state.station_flight) return false;
  if (guided_contract_one) {
    return status != FirstObjectiveStatus::turned_in;
  }
  return true;
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
      .career_onboarding = document.state.onboarding,
      .onboarding = {document.recipe.origin_station,
                     document.state.first_objective_contract,
                     document.state.first_objective_target,
                     document.state.location, document.state.first_objective},
      .career = document.state.intersystem_contract,
      .origin_system_contract = document.state.origin_system_contract,
      .origin_system = generate_origin_system(document.recipe.universe_seed),
      .planet = std::move(planet),
      .catalog = std::move(*catalog),
      .rendezvous = std::nullopt,
      .flight = document.state.flight,
      .station_flight = document.state.origin_station_flight,
      .scanner = {},
      .signal_navigation = {},
      .origin_navigation = std::nullopt,
      .journal = std::move(*journal),
      .collection = {},
      .discoveries = document.state.discoveries,
      .origin_system_discoveries = document.state.origin_system_discoveries,
      .origin_system_world_deltas = document.state.origin_system_world_deltas,
  };
  const auto binding =
      generate_home_signal_contract(document.recipe.universe_seed);
  if (state.onboarding.first_contract != binding.contract ||
      state.onboarding.first_target != binding.target ||
      state.onboarding.origin_station != binding.station ||
      state.planet->id != binding.home_planet) {
    return std::unexpected{SignalRunError::invalid_target};
  }
  if (document.state.first_objective != FirstObjectiveStatus::offered) {
    state.scanner.selected = document.state.first_objective_target;
    if (!state.career) {
      auto rendezvous = derive_rendezvous(state);
      if (!rendezvous) return std::unexpected{rendezvous.error()};
      state.rendezvous = *rendezvous;
    }
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
      if (state.rendezvous) {
        auto origin = local_navigation(*state.planet, *state.flight,
                                       state.rendezvous->position,
                                       state.rendezvous->arrival_radius_metres);
        if (!origin) return std::unexpected{origin.error()};
        state.origin_navigation = *origin;
      }
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
  if (signal_for(next.catalog, next.onboarding.first_target) == nullptr) {
    return std::unexpected{SignalRunError::invalid_target};
  }
  next.scanner.selected = next.onboarding.first_target;
  const SimulationTick tick = next.career ? next.career->universe_tick : 0;
  next.discoveries.push_back({*next.scanner.selected, tick});
  if (!next.career) {
    auto rendezvous = derive_rendezvous(next);
    if (!rendezvous) {
      return std::unexpected{rendezvous.error()};
    }
    next.rendezvous = *rendezvous;
  }
  state = std::move(next);
  return {};
}

auto launch_signal_run(SignalRunState& state, TerrainTileCache& cache)
    -> std::expected<void, SignalRunError> {
  if (state.career) {
    auto next = state;
    auto onboarding = next.onboarding;
    if (!advance_origin_onboarding(onboarding,
                                   OriginOnboardingCommand::launch)) {
      return std::unexpected{SignalRunError::invalid_transition};
    }
    auto station_flight = initialize_origin_station_launch(
        next.recipe.universe_seed, next.career->universe_tick,
        next.origin_system);
    if (!station_flight) {
      return std::unexpected{SignalRunError::flight_failure};
    }
    next.onboarding = onboarding;
    next.station_flight = std::move(*station_flight);
    next.flight.reset();
    state = std::move(next);
    return {};
  }
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

auto begin_signal_run_planetfall(SignalRunState& state, TerrainTileCache& cache)
    -> std::expected<void, SignalRunError> {
  (void)cache;
  if (!state.career || !state.station_flight || state.flight ||
      state.onboarding.first_objective != FirstObjectiveStatus::active ||
      state.onboarding.location != OriginLocation::in_flight) {
    return std::unexpected{SignalRunError::invalid_transition};
  }
  const auto station_guidance = resolve_origin_station_flight_guidance(
      state.recipe.universe_seed, state.career->universe_tick,
      state.origin_system, *state.station_flight);
  const auto pose = resolve_origin_station_flight_pose(
      state.recipe.universe_seed, state.career->universe_tick,
      state.origin_system, *state.station_flight);
  const auto home = resolve_planet_ephemeris(
      state.origin_system, state.recipe.home_planet,
      {.tick = state.career->universe_tick, .sub_tick_fraction = 0.0});
  if (!station_guidance || station_guidance->within_rendezvous || !pose ||
      !home) {
    return std::unexpected{SignalRunError::invalid_transition};
  }
  const double dx = home->position.x - pose->position.x;
  const double dy = home->position.y - pose->position.y;
  const double dz = home->position.z - pose->position.z;
  const double magnitude = std::hypot(dx, dy, dz);
  if (!std::isfinite(magnitude) || magnitude <= 1.0e-9) {
    return std::unexpected{SignalRunError::navigation_failure};
  }
  SystemDirection forward{dx / magnitude, dy / magnitude, dz / magnitude};
  auto up = state.station_flight->up;
  const double alignment =
      forward.x * up.x + forward.y * up.y + forward.z * up.z;
  if (std::abs(alignment) > 0.95) up = {0.0, 1.0, 0.0};
  SystemFlightState approach{
      .tick = state.career->universe_tick,
      .system = state.origin_system.id,
      .target = state.recipe.home_planet,
      .position = pose->position,
      // Preserve the physical station-flight position while matching the
      // host's inertial velocity. The generated station orbit is
      // presentation-scale rather than a physically integrated launch
      // velocity, so it does not enter the bounded starter-entry corridor.
      .velocity = home->velocity,
      .forward = forward,
      .up = up,
      .mode = FlightMode::manual,
      .controls = {},
      .time_scale = SystemTimeScale::one,
  };
  auto flight = insert_system_flight_orbit(state.origin_system, approach);
  if (!flight) return std::unexpected{SignalRunError::flight_failure};
  auto navigation = resolve_signal_navigation(*state.planet, state.catalog,
                                              *flight, state.scanner);
  if (!navigation) {
    return std::unexpected{SignalRunError::navigation_failure};
  }
  auto next = state;
  next.guidance.targeting_observed = true;
  next.flight = std::move(*flight);
  next.station_flight.reset();
  next.signal_navigation = *navigation;
  next.collection = {};
  state = std::move(next);
  return {};
}

auto interact_signal_run_station(SignalRunState& state,
                                 TerrainTileCache& cache)
    -> std::expected<SignalRunStationInteraction,
                     SignalRunStationInteractionError> {
  if (!state.career || !state.station_flight || state.flight ||
      state.onboarding.location != OriginLocation::in_flight) {
    return std::unexpected{SignalRunStationInteractionError::invalid_state};
  }
  const auto guidance = resolve_origin_station_flight_guidance(
      state.recipe.universe_seed, state.career->universe_tick,
      state.origin_system, *state.station_flight);
  if (!guidance) {
    return std::unexpected{
        SignalRunStationInteractionError::guidance_unavailable};
  }

  if (state.onboarding.first_objective == FirstObjectiveStatus::active) {
    if (guidance->arrived) {
      if (!return_signal_run_to_origin(state)) {
        return std::unexpected{
            SignalRunStationInteractionError::transition_rejected};
      }
      return SignalRunStationInteraction::redocked;
    }
    if (guidance->within_rendezvous) {
      return std::unexpected{
          SignalRunStationInteractionError::reduce_speed_or_depart};
    }
    if (!begin_signal_run_planetfall(state, cache)) {
      return std::unexpected{
          SignalRunStationInteractionError::transition_rejected};
    }
    return SignalRunStationInteraction::planetfall_started;
  }

  if (state.onboarding.first_objective == FirstObjectiveStatus::completed) {
    if (!guidance->arrived) {
      return std::unexpected{
          guidance->within_rendezvous
              ? SignalRunStationInteractionError::reduce_speed_or_depart
              : SignalRunStationInteractionError::approach_station};
    }
    if (!return_signal_run_to_origin(state)) {
      return std::unexpected{
          SignalRunStationInteractionError::transition_rejected};
    }
    return SignalRunStationInteraction::objective_returned;
  }

  return std::unexpected{SignalRunStationInteractionError::invalid_state};
}

auto advance_signal_run_station_flight(SignalRunState& state,
                                       std::span<const FlightCommand> commands)
    -> std::expected<void, SignalRunError> {
  if (!state.career || !state.station_flight || state.flight ||
      state.onboarding.location != OriginLocation::in_flight) {
    return std::unexpected{SignalRunError::invalid_transition};
  }
  auto next = state;
  if (!advance_origin_station_flight(
          next.recipe.universe_seed, next.career->universe_tick,
          next.origin_system, *next.station_flight, commands) ||
      !advance_intersystem_time(*next.career, 1)) {
    return std::unexpected{SignalRunError::flight_failure};
  }
  for (const auto& command : commands) {
    switch (command.kind) {
      case FlightCommandKind::press_turn_left:
      case FlightCommandKind::press_turn_right:
        next.guidance.attitude_observed = true;
        break;
      case FlightCommandKind::press_strafe_left:
      case FlightCommandKind::press_strafe_right:
      case FlightCommandKind::press_rise:
      case FlightCommandKind::press_fall:
        next.guidance.translation_observed = true;
        break;
      case FlightCommandKind::press_forward:
        next.guidance.thrust_observed = true;
        break;
      case FlightCommandKind::press_backward:
        next.guidance.braking_observed = true;
        break;
      case FlightCommandKind::release_forward:
      case FlightCommandKind::release_backward:
        if (next.guidance.thrust_observed || next.guidance.braking_observed) {
          next.guidance.coast_observed = true;
        }
        break;
      case FlightCommandKind::toggle_autopilot:
      case FlightCommandKind::decrease_time_scale:
      case FlightCommandKind::increase_time_scale:
      case FlightCommandKind::release_turn_left:
      case FlightCommandKind::release_turn_right:
      case FlightCommandKind::release_strafe_left:
      case FlightCommandKind::release_strafe_right:
      case FlightCommandKind::release_rise:
      case FlightCommandKind::release_fall: break;
    }
  }
  state = std::move(next);
  return {};
}

auto depart_signal_run_home_planet(SignalRunState& state)
    -> std::expected<void, SignalRunError> {
  if (!state.career || !state.flight || state.station_flight ||
      state.onboarding.first_objective != FirstObjectiveStatus::completed ||
      state.flight->regime != FlightRegime::orbital) {
    return std::unexpected{SignalRunError::invalid_transition};
  }
  const auto departure =
      depart_planetary_orbit(state.origin_system, *state.flight);
  auto station_flight =
      departure
          ? initialize_origin_station_approach(state.recipe.universe_seed,
                                               state.origin_system, *departure)
          : std::expected<OriginStationFlightState, OriginStationFlightError>{
                std::unexpected{OriginStationFlightError::invalid_arrival}};
  if (!departure || !station_flight) {
    return std::unexpected{SignalRunError::flight_failure};
  }
  auto next = state;
  next.flight.reset();
  next.station_flight = std::move(*station_flight);
  next.origin_navigation.reset();
  state = std::move(next);
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
  const PlanetaryFlightRules rules{.enforce_thermal_abort =
                                       next.career &&
                                       next.career->rule_profile ==
                                           IntersystemRuleProfile::pilot};
  if (!advance_planetary_flight(*next.planet, *environment, *next.flight,
                                commands, kSimulationStep, rules) ||
      (next.career && !advance_intersystem_time(*next.career, 1))) {
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
    if (next.rendezvous) {
      auto origin = local_navigation(*next.planet, *next.flight,
                                     next.rendezvous->position,
                                     next.rendezvous->arrival_radius_metres);
      if (!origin) {
        return std::unexpected{origin.error()};
      }
      next.origin_navigation = *origin;
    }
  }
  state = std::move(next);
  return {};
}

auto return_signal_run_to_origin(SignalRunState& state)
    -> std::expected<void, SignalRunError> {
  if (state.career) {
    if (!state.station_flight || state.flight) {
      return std::unexpected{SignalRunError::invalid_transition};
    }
    const auto guidance = resolve_origin_station_flight_guidance(
        state.recipe.universe_seed, state.career->universe_tick,
        state.origin_system, *state.station_flight);
    if (!guidance || !guidance->arrived) {
      return std::unexpected{SignalRunError::invalid_transition};
    }
    auto next = state;
    const auto command =
        next.onboarding.first_objective == FirstObjectiveStatus::active
            ? OriginOnboardingCommand::redock
            : OriginOnboardingCommand::dock_at_origin;
    if (!advance_origin_onboarding(next.onboarding, command)) {
      return std::unexpected{SignalRunError::invalid_transition};
    }
    next.guidance.redocking_observed = true;
    next.station_flight.reset();
    state = std::move(next);
    return {};
  }
  if (!state.flight || !state.origin_navigation ||
      state.flight->regime != FlightRegime::orbital ||
      !state.origin_navigation->arrived) {
    return std::unexpected{SignalRunError::invalid_transition};
  }
  auto onboarding = state.onboarding;
  if (!advance_origin_onboarding(onboarding,
                                 OriginOnboardingCommand::dock_at_origin)) {
    return std::unexpected{SignalRunError::invalid_transition};
  }
  state.onboarding = onboarding;
  state.flight.reset();
  state.origin_navigation.reset();
  return {};
}

auto turn_in_signal_run(SignalRunState& state)
    -> std::expected<void, SignalRunError> {
  auto next = state;
  if (!advance_origin_onboarding(next.onboarding,
                                 OriginOnboardingCommand::turn_in)) {
    return std::unexpected{SignalRunError::invalid_transition};
  }
  if (next.career) {
    if (next.career_onboarding.state != OnboardingState::guided ||
        next.career_onboarding.chapter != OnboardingChapter::contract_one ||
        !advance_onboarding(next.career_onboarding,
                            OnboardingCommand::complete_contract_one)) {
      return std::unexpected{SignalRunError::invalid_transition};
    }
    auto contract = initial_origin_system_contract(next.recipe.universe_seed);
    if (!contract) {
      return std::unexpected{SignalRunError::invalid_transition};
    }
    next.origin_system_contract = std::move(*contract);
  }
  state = std::move(next);
  return {};
}

auto project_signal_run_save(const SignalRunState& state)
    -> std::expected<SaveDocument, SignalRunError> {
  SaveDocument document{
      .recipe = state.recipe,
      .state =
          SaveMutableState{
              .onboarding = state.career_onboarding,
              .location = state.onboarding.location,
              .first_objective = state.onboarding.first_objective,
              .first_objective_contract = state.onboarding.first_contract,
              .first_objective_target = state.onboarding.first_target,
              .flight = state.flight,
              .system_flight = std::nullopt,
              .origin_station_flight = state.station_flight,
              .discoveries = state.discoveries,
              .world_deltas = {state.journal.entries().begin(),
                               state.journal.entries().end()},
              .origin_system_contract = state.origin_system_contract,
              .origin_system_discoveries = state.origin_system_discoveries,
              .origin_system_world_deltas = state.origin_system_world_deltas,
              .intersystem_contract = state.career,
          },
  };
  if (!consistent_loaded_state(state) || !validate_save_document(document)) {
    return std::unexpected{SignalRunError::inconsistent_state};
  }
  return document;
}

}  // namespace apsis_drift
