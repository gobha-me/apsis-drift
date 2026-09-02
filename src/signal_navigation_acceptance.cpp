#include "apsis_drift/signal_navigation_acceptance.hpp"

#include <array>
#include <format>
#include <span>
#include <utility>

#include "apsis_drift/coordinates.hpp"
#include "apsis_drift/landscape.hpp"
#include "apsis_drift/seed.hpp"

namespace apsis_drift {
namespace {

[[nodiscard]] auto surface_environment(const PlanetDescriptor& planet,
                                       const PlanetaryFlightState& state,
                                       TerrainTileCache& cache)
    -> std::expected<PlanetaryFlightEnvironment,
                     SignalNavigationAcceptanceError> {
  const auto fixed = planet_fixed_from_geodetic(
      planet, {state.pose.position.latitude_radians,
               state.pose.position.longitude_radians, 0.0});
  if (!fixed) {
    return std::unexpected{SignalNavigationAcceptanceError::terrain_failure};
  }
  const auto surface =
      sample_planet_surface(planet, *fixed, kSurfaceSignalPlacementLod, cache);
  if (!surface) {
    return std::unexpected{SignalNavigationAcceptanceError::terrain_failure};
  }
  return PlanetaryFlightEnvironment{surface->elevation_metres};
}

} // namespace

auto initial_signal_navigation_acceptance(const PlanetDescriptor& planet,
                                          TerrainTileCache& cache)
    -> std::expected<SignalNavigationAcceptanceState,
                     SignalNavigationAcceptanceError> {
  auto catalog = generate_surface_signals(planet, cache);
  if (!catalog) {
    return std::unexpected{SignalNavigationAcceptanceError::terrain_failure};
  }
  const auto& target =
      catalog->signals[kSignalNavigationAcceptanceTargetOrdinal];
  const auto target_fixed = planet_fixed_from_terrain_address(
      planet, target.anchor,
      static_cast<double>(target.approach_altitude_metres));
  const auto target_position =
      target_fixed ? geodetic_from_planet_fixed(planet, *target_fixed)
                   : std::expected<GeodeticPosition, CoordinateError>{
                         std::unexpected{CoordinateError::non_finite_input}};
  const auto frame =
      target_position ? make_local_tangent_frame(planet, *target_position)
                      : std::expected<LocalTangentFrame, CoordinateError>{
                            std::unexpected{CoordinateError::non_finite_input}};
  const auto start_fixed =
      frame ? planet_fixed_from_local(
                  *frame,
                  {-kSignalNavigationAcceptanceStartOffsetMetres, 0.0, 0.0})
            : std::expected<PlanetFixedPositionMetres, CoordinateError>{
                  std::unexpected{CoordinateError::non_finite_input}};
  const auto start_position =
      start_fixed ? geodetic_from_planet_fixed(planet, *start_fixed)
                  : std::expected<GeodeticPosition, CoordinateError>{
                        std::unexpected{CoordinateError::non_finite_input}};
  if (!start_position) {
    return std::unexpected{SignalNavigationAcceptanceError::terrain_failure};
  }

  PlanetaryFlightState probe{
      .tick = 0,
      .planet = planet.id,
      .pose = {*start_position, 0.0},
      .velocity = {},
      .clearance_metres = 0.0,
      .mode = FlightMode::manual,
      .controls = {},
      .regime = FlightRegime::terrain_flight,
      .last_transition = std::nullopt,
      .thermal = {},
  };
  const auto environment = surface_environment(planet, probe, cache);
  if (!environment) return std::unexpected{environment.error()};
  const auto flight = initial_planetary_flight_state(
      planet, *start_position, *environment, 0.0, FlightMode::manual);
  if (!flight) {
    return std::unexpected{SignalNavigationAcceptanceError::flight_failure};
  }
  auto journal = WorldDeltaJournal::create();
  if (!journal) {
    return std::unexpected{SignalNavigationAcceptanceError::journal_failure};
  }

  SignalNavigationAcceptanceState state{
      .catalog = std::move(*catalog),
      .flight = *flight,
      .scanner = {},
      .navigation = {},
      .journal = std::move(*journal),
      .collection = {},
      .reached_tick = std::nullopt,
      .command_count = 1,
  };
  if (!advance_signal_selection(state.catalog, state.scanner,
                                SignalSelectionCommand::next)) {
    return std::unexpected{SignalNavigationAcceptanceError::scanner_failure};
  }
  const auto navigation = resolve_signal_navigation(
      planet, state.catalog, state.flight, state.scanner);
  if (!navigation || navigation->status != SignalScannerStatus::tracking) {
    return std::unexpected{SignalNavigationAcceptanceError::scanner_failure};
  }
  state.navigation = *navigation;
  const auto collection = advance_signal_collection(
      state.catalog, state.navigation, state.flight.tick, state.journal,
      state.collection);
  if (!collection) {
    return std::unexpected{SignalNavigationAcceptanceError::collection_failure};
  }
  return state;
}

auto advance_signal_navigation_acceptance(
    const PlanetDescriptor& planet, TerrainTileCache& cache,
    SignalNavigationAcceptanceState& state)
    -> std::expected<bool, SignalNavigationAcceptanceError> {
  if (state.collection.status == SignalCollectionStatus::complete) return true;
  if (state.flight.tick >= kSignalNavigationAcceptanceMaximumTicks) {
    return std::unexpected{SignalNavigationAcceptanceError::incomplete_path};
  }
  const auto environment = surface_environment(planet, state.flight, cache);
  if (!environment) return std::unexpected{environment.error()};
  const bool release_for_scan =
      state.navigation.status == SignalScannerStatus::reached &&
      state.flight.controls.forward;
  const std::array command{FlightCommand{
      state.flight.tick, release_for_scan ? FlightCommandKind::release_forward
                                          : FlightCommandKind::press_forward}};
  const std::span<const FlightCommand> commands =
      state.flight.tick == 0 || release_for_scan
          ? std::span<const FlightCommand>{command}
          : std::span<const FlightCommand>{};
  if (!advance_planetary_flight(planet, *environment, state.flight, commands,
                                kSimulationStep)) {
    return std::unexpected{SignalNavigationAcceptanceError::flight_failure};
  }
  if (release_for_scan) ++state.command_count;
  const auto navigation = resolve_signal_navigation(
      planet, state.catalog, state.flight, state.scanner);
  if (!navigation) {
    return std::unexpected{SignalNavigationAcceptanceError::scanner_failure};
  }
  state.navigation = *navigation;
  if (state.navigation.status == SignalScannerStatus::reached &&
      !state.reached_tick) {
    state.reached_tick = state.flight.tick;
  }
  const auto collection = advance_signal_collection(
      state.catalog, state.navigation, state.flight.tick, state.journal,
      state.collection);
  if (!collection) {
    return std::unexpected{SignalNavigationAcceptanceError::collection_failure};
  }
  return state.collection.status == SignalCollectionStatus::complete;
}

auto replay_signal_navigation_acceptance()
    -> std::expected<SignalNavigationAcceptanceState,
                     SignalNavigationAcceptanceError> {
  const auto planet =
      generate_planet_descriptor(Seed{kSignalNavigationAcceptanceSeed});
  auto cache = TerrainTileCache::create();
  if (!cache) {
    return std::unexpected{SignalNavigationAcceptanceError::terrain_failure};
  }
  auto state = initial_signal_navigation_acceptance(planet, *cache);
  if (!state) return std::unexpected{state.error()};
  while (state->collection.status != SignalCollectionStatus::complete) {
    const auto complete =
        advance_signal_navigation_acceptance(planet, *cache, *state);
    if (!complete) return std::unexpected{complete.error()};
  }
  return state;
}

auto signal_navigation_acceptance_json(
    const SignalNavigationAcceptanceReport& report) -> std::string {
  return std::format(
      "{{\n"
      "  \"schema_version\": 2,\n"
      "  \"scenario\": \"{}\",\n"
      "  \"seed\": {},\n"
      "  \"simulation_hz\": {},\n"
      "  \"target_ordinal\": {},\n"
      "  \"target_id\": \"{}\",\n"
      "  \"selection_tick\": 0,\n"
      "  \"reached_tick\": {},\n"
      "  \"completion_tick\": {},\n"
      "  \"command_count\": {},\n"
      "  \"final_status\": \"complete\",\n"
      "  \"world_delta_count\": {},\n"
      "  \"world_delta_kind\": \"collected\",\n"
      "  \"final_distance_metres\": {:.6f},\n"
      "  \"flight_checksum\": \"{}\",\n"
      "  \"framebuffer_checksum\": \"{}\",\n"
      "  \"render_profile\": \"{}\",\n"
      "  \"viewport_width\": {},\n"
      "  \"viewport_height\": {},\n"
      "  \"presentation\": \"{}\"\n"
      "}}\n",
      kSignalNavigationAcceptanceScenario, kSignalNavigationAcceptanceSeed,
      kSimulationHz, kSignalNavigationAcceptanceTargetOrdinal,
      surface_signal_id_string(report.target_id), report.reached_tick,
      report.completion_tick, report.command_count, report.world_delta_count,
      report.final_distance_metres, report.flight_checksum,
      report.framebuffer_checksum, profile_name(report.render_configuration),
      report.render_configuration.viewport.width,
      report.render_configuration.viewport.height, report.presentation);
}

} // namespace apsis_drift
