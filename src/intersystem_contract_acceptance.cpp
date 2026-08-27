#include "apsis_drift/intersystem_contract_acceptance.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <format>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <ranges>
#include <span>
#include <utility>

#include "apsis_drift/intersystem_jump.hpp"
#include "apsis_drift/intersystem_planetfall.hpp"
#include "apsis_drift/intersystem_planetfall_acceptance.hpp"
#include "apsis_drift/landscape.hpp"
#include "apsis_drift/local_system.hpp"
#include "apsis_drift/origin_return.hpp"
#include "apsis_drift/save_file.hpp"
#include "apsis_drift/save_schema.hpp"
#include "apsis_drift/surface_signals.hpp"
#include "apsis_drift/system_flight.hpp"
#include "apsis_drift/system_rendering.hpp"
#include "apsis_drift/terrain_tiles.hpp"
#include "apsis_drift/universe_navigation.hpp"

namespace apsis_drift {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::array<std::string_view, 9> kResumeStages{
    "docked",           "origin-flight",    "outbound-spool",
    "canceled-spool",   "outbound-transit", "target-system",
    "planet-side",      "origin-return",    "returned-docked"};

struct Replay {
  SaveDocument final_document;
  std::vector<IntersystemContractAcceptanceCheckpoint> checkpoints;
  std::vector<termforge::Pixel> final_frame;
  std::uint64_t final_checksum{};
  std::size_t target_system_planet_count{};
  std::uint64_t target_system_initial_framebuffer_checksum{};
  std::uint64_t target_system_moved_framebuffer_checksum{};
  std::uint64_t framebuffer_checksum{};
  double simulation_ms{};
  double application_render_ms{};
  SystemId outbound_selected_system;
  SystemId return_selected_system;
  std::size_t universe_navigation_rows{};
};

[[nodiscard]] auto milliseconds(Clock::time_point start,
                                Clock::time_point finish) noexcept -> double {
  return std::chrono::duration<double, std::milli>(finish - start).count();
}

[[nodiscard]] auto hash_bytes(std::string_view bytes) noexcept
    -> std::uint64_t {
  std::uint64_t hash{1469598103934665603ULL};
  for (const unsigned char byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

[[nodiscard]] auto document_checksum(const SaveDocument& document)
    -> std::expected<std::uint64_t, IntersystemContractAcceptanceError> {
  const auto encoded = encode_save_document_json(document);
  if (!encoded) {
    return std::unexpected{
        IntersystemContractAcceptanceError::persistence_failure};
  }
  try {
    const auto root = nlohmann::ordered_json::parse(*encoded);
    const nlohmann::ordered_json authoritative{
        {"recipe", root.at("recipe")}, {"state", root.at("state")}};
    return hash_bytes(authoritative.dump());
  } catch (const nlohmann::json::exception&) {
    return std::unexpected{
        IntersystemContractAcceptanceError::persistence_failure};
  }
}

[[nodiscard]] auto guided_contract_three_document(Seed seed)
    -> std::expected<SaveDocument, IntersystemContractAcceptanceError> {
  auto document = make_new_game_document(
      seed, NewGameOnboardingChoice::guided);
  auto contract_two = initial_origin_system_contract(seed);
  if (!document.state.intersystem_contract || !contract_two) {
    return std::unexpected{
        IntersystemContractAcceptanceError::initialization_failure};
  }
  contract_two->phase = OriginSystemContractPhase::turned_in;
  document.state.onboarding = {
      .state = OnboardingState::guided,
      .chapter = OnboardingChapter::contract_three};
  document.state.first_objective = FirstObjectiveStatus::turned_in;
  document.state.origin_system_contract = *contract_two;
  document.state.origin_system_discoveries = {
      {document.state.first_objective_target, 0},
      {contract_two->binding.target_objective, 0}};
  document.state.origin_system_world_deltas = {
      {surface_signal_object_key(document.state.first_objective_target),
       SaveWorldDeltaKind::collected, 0},
      {surface_signal_object_key(contract_two->binding.target_objective),
       SaveWorldDeltaKind::collected, 0}};
  if (!validate_save_document(document)) {
    return std::unexpected{
        IntersystemContractAcceptanceError::initialization_failure};
  }
  return document;
}

[[nodiscard]] auto record_checkpoint(
    SaveDocument& document, std::string_view name,
    std::optional<std::string_view> resume_stage,
    std::vector<IntersystemContractAcceptanceCheckpoint>& checkpoints)
    -> std::expected<void, IntersystemContractAcceptanceError> {
  const auto encoded = encode_save_document_json(document);
  if (!encoded || !document.state.intersystem_contract) {
    if (!encoded) {
      std::fprintf(stderr, "checkpoint %.*s save rejected at %s: %s\n",
                   static_cast<int>(name.size()), name.data(),
                   encoded.error().path.c_str(),
                   encoded.error().detail.c_str());
    }
    return std::unexpected{
        IntersystemContractAcceptanceError::persistence_failure};
  }
  const auto checksum = document_checksum(document);
  if (!checksum) return std::unexpected{checksum.error()};
  checkpoints.push_back(
      {.name = std::string{name},
       .tick = document.state.intersystem_contract->universe_tick,
       .authoritative_checksum = *checksum});
  if (resume_stage != name)
    return {};
  const auto decoded = decode_save_document_json(*encoded);
  if (!decoded || *decoded != document) {
    return std::unexpected{
        IntersystemContractAcceptanceError::persistence_failure};
  }
  document = std::move(*decoded);
  return {};
}

[[nodiscard]] auto target_entry_position(const PlanetDescriptor& planet,
                                         SurfaceSignalId target,
                                         TerrainTileCache& cache)
    -> std::expected<std::pair<GeodeticPosition, PlanetaryFlightEnvironment>,
                     IntersystemContractAcceptanceError> {
  const auto catalog = generate_surface_signals(planet, cache);
  if (!catalog) {
    return std::unexpected{
        IntersystemContractAcceptanceError::initialization_failure};
  }
  const auto found =
      std::ranges::find(catalog->signals, target, &SurfaceSignal::id);
  if (found == catalog->signals.end()) {
    return std::unexpected{
        IntersystemContractAcceptanceError::initialization_failure};
  }
  const auto fixed = planet_fixed_from_terrain_address(
      planet, found->anchor,
      static_cast<double>(found->approach_altitude_metres));
  const auto position =
      fixed ? geodetic_from_planet_fixed(planet, *fixed)
            : std::expected<GeodeticPosition, CoordinateError>{
                  std::unexpected{CoordinateError::non_finite_input}};
  if (!position) {
    return std::unexpected{
        IntersystemContractAcceptanceError::initialization_failure};
  }
  return std::pair{*position, PlanetaryFlightEnvironment{static_cast<double>(
                                  found->surface_elevation_metres)}};
}

[[nodiscard]] auto render_system_frames(const LocalSystemDescriptor& system,
                                        const SystemFlightState& flight,
                                        int width, int height,
                                        double& render_ms)
    -> std::expected<std::pair<std::uint64_t, std::uint64_t>,
                     IntersystemContractAcceptanceError> {
  LocalSystemRenderer renderer{{.width = width, .height = height}};
  std::vector<termforge::Pixel> initial(static_cast<std::size_t>(width) *
                                        static_cast<std::size_t>(height));
  std::vector<termforge::Pixel> moved(initial.size());
  const SystemPositionMetres fixed_camera{0.0, -92'000'000'000.0,
                                           26'000'000'000.0};
  const SystemPositionMetres camera_offset{0.0, -92'000'000'000.0,
                                            26'000'000'000.0};
  const auto view = [&](SimulationTick tick)
      -> std::expected<LocalSystemView, LocalSystemError> {
    const auto target = resolve_planet_ephemeris(
        system, flight.target, {.tick = tick, .sub_tick_fraction = 0.0});
    if (!target) return std::unexpected{target.error()};
    return LocalSystemView{
        .time = {tick, 0.0},
        .position = {target->position.x + camera_offset.x,
                     target->position.y + camera_offset.y,
                     target->position.z + camera_offset.z},
        .velocity = target->velocity,
        .forward = {-camera_offset.x, -camera_offset.y, -camera_offset.z},
        .up = {0.0, 0.0, 1.0},
        .selected_planet = flight.target};
  };
  const auto start = Clock::now();
  auto first = renderer.render(
      system,
      {.time = {flight.tick, 0.0},
       .position = fixed_camera,
       .velocity = {},
       .forward = {-fixed_camera.x, -fixed_camera.y, -fixed_camera.z},
       .up = {0.0, 0.0, 1.0},
       .selected_planet = flight.target},
      initial);
  auto second = renderer.render(
      system,
      {.time = {flight.tick + 10U * kSimulationHz, 0.0},
       .position = fixed_camera,
       .velocity = {},
       .forward = {-fixed_camera.x, -fixed_camera.y, -fixed_camera.z},
       .up = {0.0, 0.0, 1.0},
       .selected_planet = flight.target},
      moved);
  const auto complete = [](const auto& rendered) {
    return rendered && rendered->selected_visible &&
           rendered->visible_planets > 0U && rendered->star_pixels > 0U;
  };
  if (!complete(first) || !complete(second) ||
      pixel_checksum(initial) == pixel_checksum(moved)) {
    const auto target_planet = find_local_system_planet(system, flight.target);
    if (!target_planet || (*target_planet)->orbit.period_ticks == 0U) {
      std::fprintf(stderr,
                   "contract-three target presentation orbit failed\n");
      return std::unexpected{
          IntersystemContractAcceptanceError::presentation_failure};
    }
    const SimulationTick probe_offset =
        std::max<SimulationTick>(1U,
                                (*target_planet)->orbit.period_ticks / 512U);
    if (flight.tick >
        std::numeric_limits<SimulationTick>::max() - probe_offset) {
      std::fprintf(stderr,
                   "contract-three target presentation tick overflowed\n");
      return std::unexpected{
          IntersystemContractAcceptanceError::presentation_failure};
    }
    const auto initial_view = view(flight.tick);
    if (!initial_view) {
      std::fprintf(stderr,
                   "contract-three target presentation ephemeris failed\n");
      return std::unexpected{
          IntersystemContractAcceptanceError::presentation_failure};
    }
    auto moved_view = *initial_view;
    moved_view.time = {flight.tick + probe_offset, 0.0};
    first = renderer.render(system, *initial_view, initial);
    second = renderer.render(system, moved_view, moved);
  }
  render_ms += milliseconds(start, Clock::now());
  if (!complete(first) || !complete(second)) {
    std::fprintf(stderr,
                 "contract-three target presentation was incomplete\n");
    return std::unexpected{
        IntersystemContractAcceptanceError::presentation_failure};
  }
  const auto initial_checksum = pixel_checksum(initial);
  const auto moved_checksum = pixel_checksum(moved);
  if (initial_checksum == moved_checksum) {
    std::fprintf(stderr,
                 "contract-three target presentation did not move\n");
    return std::unexpected{
        IntersystemContractAcceptanceError::presentation_failure};
  }
  return std::pair{initial_checksum, moved_checksum};
}

[[nodiscard]] auto replay(const SaveDocument& starting_document, int width,
                          int height,
                          std::optional<std::string_view> resume_stage)
    -> std::expected<Replay, IntersystemContractAcceptanceError> {
  const auto replay_start = Clock::now();
  double render_ms{};
  auto document = starting_document;
  if (!validate_save_document(document) ||
      document.state.onboarding.state != OnboardingState::guided ||
      document.state.onboarding.chapter != OnboardingChapter::contract_three ||
      document.state.first_objective != FirstObjectiveStatus::turned_in ||
      !document.state.intersystem_contract ||
      !document.state.origin_system_contract ||
      document.state.origin_system_contract->phase !=
          OriginSystemContractPhase::turned_in ||
      document.state.location != OriginLocation::docked_at_origin ||
      document.state.flight || document.state.system_flight ||
      document.state.origin_station_flight) {
    return std::unexpected{
        IntersystemContractAcceptanceError::initialization_failure};
  }
  std::vector<IntersystemContractAcceptanceCheckpoint> checkpoints;
  if (!record_checkpoint(document, "docked", resume_stage, checkpoints)) {
    return std::unexpected{
        IntersystemContractAcceptanceError::persistence_failure};
  }

  auto& initial_contract = *document.state.intersystem_contract;
  if (!advance_intersystem_contract(
          initial_contract, initial_contract.universe_tick,
          IntersystemContractCommand::accept_mission) ||
      !advance_intersystem_contract(initial_contract,
                                    initial_contract.universe_tick,
                                    IntersystemContractCommand::launch)) {
    return std::unexpected{
        IntersystemContractAcceptanceError::transition_failure};
  }
  const auto target_system =
      generate_local_system(initial_contract.identities.target_system_seed);
  const auto origin_system =
      generate_origin_system(initial_contract.identities.universe_seed);
  auto launched =
      initialize_origin_station_launch(initial_contract, origin_system);
  if (!launched) {
    return std::unexpected{
        IntersystemContractAcceptanceError::initialization_failure};
  }
  document.state.origin_station_flight = *launched;
  const auto route = generate_first_universe_route(
      document.recipe.universe_seed);
  const auto outbound_view = resolve_onboarding_navigation_view(
      route, document.state.onboarding, route.origin);
  UniverseNavigationSelectionState outbound_selection;
  if (!outbound_view ||
      !advance_universe_navigation_selection(
          *outbound_view, outbound_selection,
          UniverseNavigationSelectionCommand::next) ||
      !advance_universe_navigation_selection(
          *outbound_view, outbound_selection,
          UniverseNavigationSelectionCommand::select) ||
      outbound_selection.pending_destination != route.destination) {
    return std::unexpected{
        IntersystemContractAcceptanceError::transition_failure};
  }

  const auto advance_origin_tick =
      [&](std::span<const FlightCommand> commands) -> bool {
    auto next_contract = *document.state.intersystem_contract;
    auto next_flight = *document.state.origin_station_flight;
    if (!advance_origin_station_flight(next_contract, origin_system,
                                       next_flight, commands) ||
        !advance_intersystem_time(next_contract, 1U)) {
      return false;
    }
    document.state.intersystem_contract = std::move(next_contract);
    document.state.origin_station_flight = std::move(next_flight);
    return true;
  };
  {
    const std::array press{
        FlightCommand{document.state.origin_station_flight->tick,
                      FlightCommandKind::press_forward}};
    if (!advance_origin_tick(press)) {
      return std::unexpected{
          IntersystemContractAcceptanceError::simulation_failure};
    }
  }
  for (SimulationTick tick = 1; tick < kSimulationHz; ++tick) {
    if (!advance_origin_tick({})) {
      return std::unexpected{
          IntersystemContractAcceptanceError::simulation_failure};
    }
  }
  {
    const std::array release{
        FlightCommand{document.state.origin_station_flight->tick,
                      FlightCommandKind::release_forward}};
    if (!advance_origin_tick(release) ||
        !record_checkpoint(document, "origin-flight", resume_stage,
                           checkpoints) ||
        !document.state.origin_station_flight) {
      return std::unexpected{
          IntersystemContractAcceptanceError::persistence_failure};
    }
  }
  {
    const std::array assist{
        FlightCommand{document.state.origin_station_flight->tick,
                      FlightCommandKind::toggle_autopilot}};
    if (!advance_origin_tick(assist)) {
      return std::unexpected{
          IntersystemContractAcceptanceError::simulation_failure};
    }
  }
  constexpr SimulationTick redock_limit{30U * kSimulationHz};
  SimulationTick redock_ticks{};
  for (; redock_ticks < redock_limit; ++redock_ticks) {
    const auto guidance = resolve_origin_station_flight_guidance(
        *document.state.intersystem_contract, origin_system,
        *document.state.origin_station_flight);
    if (!guidance) {
      return std::unexpected{
          IntersystemContractAcceptanceError::simulation_failure};
    }
    if (guidance->arrived) break;
    if (!advance_origin_tick({})) {
      return std::unexpected{
          IntersystemContractAcceptanceError::simulation_failure};
    }
  }
  if (redock_ticks == redock_limit ||
      !attempt_origin_docking(*document.state.intersystem_contract,
                              origin_system,
                              *document.state.origin_station_flight)) {
    return std::unexpected{
        IntersystemContractAcceptanceError::transition_failure};
  }
  document.state.origin_station_flight.reset();
  if (!advance_intersystem_contract(
          *document.state.intersystem_contract,
          document.state.intersystem_contract->universe_tick,
          IntersystemContractCommand::launch)) {
    return std::unexpected{
        IntersystemContractAcceptanceError::transition_failure};
  }
  launched = initialize_origin_station_launch(
      *document.state.intersystem_contract, origin_system);
  if (!launched) {
    return std::unexpected{
        IntersystemContractAcceptanceError::initialization_failure};
  }
  document.state.origin_station_flight = *launched;
  const auto frozen_flight = *document.state.origin_station_flight;
  if (!begin_intersystem_jump(
          *document.state.intersystem_contract,
          *document.state.origin_station_flight,
          *outbound_selection.pending_destination)) {
    return std::unexpected{
        IntersystemContractAcceptanceError::transition_failure};
  }
  constexpr SimulationTick canceled_spool_ticks{kSimulationHz / 2U};
  for (SimulationTick tick = 0; tick < canceled_spool_ticks; ++tick) {
    if (!advance_intersystem_jump_tick(*document.state.intersystem_contract,
                                       target_system)) {
      return std::unexpected{
          IntersystemContractAcceptanceError::simulation_failure};
    }
  }
  if (!record_checkpoint(document, "outbound-spool", resume_stage,
                         checkpoints) ||
      !document.state.origin_station_flight ||
      !cancel_intersystem_jump(*document.state.intersystem_contract)) {
    return std::unexpected{
        IntersystemContractAcceptanceError::persistence_failure};
  }
  document.state.origin_station_flight->tick =
      document.state.intersystem_contract->universe_tick;
  document.state.origin_station_flight->controls = {};
  auto retimed_frozen = frozen_flight;
  retimed_frozen.tick = document.state.intersystem_contract->universe_tick;
  const auto canceled_guidance = resolve_origin_station_flight_guidance(
      *document.state.intersystem_contract, origin_system,
      *document.state.origin_station_flight);
  if (*document.state.origin_station_flight != retimed_frozen ||
      !canceled_guidance ||
      !record_checkpoint(document, "canceled-spool", resume_stage,
                         checkpoints) ||
      !document.state.origin_station_flight ||
      resolve_origin_station_flight_guidance(
          *document.state.intersystem_contract, origin_system,
          *document.state.origin_station_flight) != canceled_guidance ||
      !begin_intersystem_jump(
          *document.state.intersystem_contract,
          *document.state.origin_station_flight,
          *outbound_selection.pending_destination)) {
    return std::unexpected{IntersystemContractAcceptanceError::resume_mismatch};
  }
  for (SimulationTick tick = 0; tick < kJumpSpoolTicks; ++tick) {
    if (!advance_intersystem_jump_tick(*document.state.intersystem_contract,
                                    target_system)) {
      return std::unexpected{
          IntersystemContractAcceptanceError::simulation_failure};
    }
  }
  if (document.state.intersystem_contract->travel_phase !=
      IntersystemTravelPhase::outbound_jump_committed) {
    return std::unexpected{
        IntersystemContractAcceptanceError::transition_failure};
  }
  document.state.origin_station_flight.reset();
  if (!record_checkpoint(document, "outbound-transit", resume_stage,
                         checkpoints)) {
    return std::unexpected{
        IntersystemContractAcceptanceError::persistence_failure};
  }
  for (SimulationTick tick = 0; tick < kJumpTransitTicks; ++tick) {
    if (!advance_intersystem_jump_tick(*document.state.intersystem_contract,
                                    target_system)) {
      return std::unexpected{
          IntersystemContractAcceptanceError::simulation_failure};
    }
  }
  auto& arrived_contract = *document.state.intersystem_contract;
  if (arrived_contract.travel_phase !=
          IntersystemTravelPhase::target_system_flight ||
      !arrived_contract.arrival_solution) {
    return std::unexpected{
        IntersystemContractAcceptanceError::transition_failure};
  }
  auto system_flight = initial_system_flight_state(
      target_system, arrived_contract.identities.target_planet,
      *arrived_contract.arrival_solution);
  if (!system_flight) {
    return std::unexpected{
        IntersystemContractAcceptanceError::initialization_failure};
  }
  document.state.system_flight = *system_flight;
  if (!record_checkpoint(document, "target-system", resume_stage,
                         checkpoints) ||
      !document.state.system_flight) {
    return std::unexpected{
        IntersystemContractAcceptanceError::persistence_failure};
  }
  system_flight = *document.state.system_flight;
  const auto system_frames = render_system_frames(target_system, *system_flight,
                                                  width, height, render_ms);
  if (!system_frames)
    return std::unexpected{system_frames.error()};

  const std::array speed_commands{
      FlightCommand{system_flight->tick,
                    FlightCommandKind::increase_time_scale},
      FlightCommand{system_flight->tick,
                    FlightCommandKind::increase_time_scale}};
  auto before_tick = system_flight->tick;
  if (!advance_system_flight(target_system, *system_flight, speed_commands) ||
      !advance_intersystem_time(*document.state.intersystem_contract,
                                system_flight->tick - before_tick)) {
    return std::unexpected{
        IntersystemContractAcceptanceError::simulation_failure};
  }
  constexpr SimulationTick maximum_system_steps{30'000};
  SimulationTick system_steps{};
  for (; system_steps < maximum_system_steps; ++system_steps) {
    const auto guidance =
        resolve_system_flight_guidance(target_system, *system_flight);
    if (!guidance) {
      return std::unexpected{
          IntersystemContractAcceptanceError::simulation_failure};
    }
    if (guidance->orbit_insertion_ready)
      break;
    before_tick = system_flight->tick;
    if (!advance_system_flight(target_system, *system_flight, {}) ||
        !advance_intersystem_time(*document.state.intersystem_contract,
                                  system_flight->tick - before_tick)) {
      return std::unexpected{
          IntersystemContractAcceptanceError::simulation_failure};
    }
  }
  const auto orbital =
      system_steps < maximum_system_steps
          ? insert_system_flight_orbit(target_system, *system_flight)
          : std::expected<PlanetaryFlightState, SystemFlightError>{
                std::unexpected{SystemFlightError::orbit_insertion_refused}};
  if (!orbital || !advance_intersystem_contract(
                      *document.state.intersystem_contract,
                      document.state.intersystem_contract->universe_tick,
                      IntersystemContractCommand::enter_target_planet)) {
    return std::unexpected{
        IntersystemContractAcceptanceError::transition_failure};
  }
  document.state.system_flight.reset();

  const auto target_body = find_local_system_planet(
      target_system,
      document.state.intersystem_contract->identities.target_planet);
  auto terrain_cache = TerrainTileCache::create();
  if (!target_body || !terrain_cache) {
    return std::unexpected{
        IntersystemContractAcceptanceError::initialization_failure};
  }
  const auto target_entry = target_entry_position(
      (*target_body)->descriptor,
      document.state.intersystem_contract->identities.target_objective,
      *terrain_cache);
  auto target_flight =
      target_entry ? initial_planetary_flight_state(
                         (*target_body)->descriptor, target_entry->first,
                         target_entry->second, 0.0, FlightMode::autopilot)
                   : std::expected<PlanetaryFlightState, PlanetaryFlightError>{
                         std::unexpected{PlanetaryFlightError::invalid_state}};
  if (!target_entry || !target_flight) {
    return std::unexpected{
        IntersystemContractAcceptanceError::initialization_failure};
  }
  target_flight->tick = document.state.intersystem_contract->universe_tick;
  auto planetfall = initialize_intersystem_planetfall(
      (*target_body)->descriptor,
      document.state.intersystem_contract->identities.target_objective,
      *target_flight, document.state.world_deltas, *terrain_cache);
  if (!planetfall) {
    return std::unexpected{
        IntersystemContractAcceptanceError::initialization_failure};
  }
  constexpr SimulationTick collection_limit{20U * kSimulationHz};
  bool objective_completed{};
  for (SimulationTick tick = 0; tick < collection_limit && !objective_completed;
       ++tick) {
    const auto previous = planetfall->flight.tick;
    const auto advanced =
        advance_intersystem_planetfall(
            *planetfall, *terrain_cache, {},
            document.state.intersystem_contract->rule_profile);
    if (!advanced ||
        !advance_intersystem_time(*document.state.intersystem_contract,
                                  planetfall->flight.tick - previous)) {
      return std::unexpected{
          IntersystemContractAcceptanceError::simulation_failure};
    }
    if (advanced->objective_completed) {
      if (!advance_intersystem_contract(
              *document.state.intersystem_contract,
              document.state.intersystem_contract->universe_tick,
              IntersystemContractCommand::complete_objective)) {
        return std::unexpected{
            IntersystemContractAcceptanceError::transition_failure};
      }
      objective_completed = true;
    }
  }
  if (!objective_completed || planetfall->journal.entries().size() != 1U) {
    return std::unexpected{IntersystemContractAcceptanceError::incomplete_path};
  }
  document.state.flight = planetfall->flight;
  document.state.world_deltas.assign(planetfall->journal.entries().begin(),
                                     planetfall->journal.entries().end());
  document.state.discoveries = {
      {document.state.intersystem_contract->identities.target_objective,
       document.state.intersystem_contract->universe_tick}};
  if (!record_checkpoint(document, "planet-side", resume_stage, checkpoints) ||
      !document.state.flight) {
    return std::unexpected{
        IntersystemContractAcceptanceError::persistence_failure};
  }
  if (resume_stage == "planet-side") {
    terrain_cache = TerrainTileCache::create();
    if (!terrain_cache) {
      return std::unexpected{
          IntersystemContractAcceptanceError::initialization_failure};
    }
    planetfall = initialize_intersystem_planetfall(
        (*target_body)->descriptor,
        document.state.intersystem_contract->identities.target_objective,
        *document.state.flight, document.state.world_deltas, *terrain_cache);
    if (!planetfall) {
      return std::unexpected{
          IntersystemContractAcceptanceError::persistence_failure};
    }
  }

  constexpr SimulationTick ascent_limit{200'000};
  bool ascent_commanded{};
  SimulationTick ascent_ticks{};
  for (; ascent_ticks < ascent_limit; ++ascent_ticks) {
    if (planetfall->flight.regime == FlightRegime::orbital)
      break;
    std::array<FlightCommand, 1> command_storage{};
    std::span<const FlightCommand> commands;
    if (!ascent_commanded) {
      command_storage.front() = {planetfall->flight.tick,
                                 FlightCommandKind::press_rise};
      commands = command_storage;
      ascent_commanded = true;
    }
    const auto previous = planetfall->flight.tick;
    if (!advance_intersystem_planetfall(*planetfall, *terrain_cache, commands,
                                        document.state.intersystem_contract
                                            ->rule_profile) ||
        !advance_intersystem_time(*document.state.intersystem_contract,
                                  planetfall->flight.tick - previous)) {
      return std::unexpected{
          IntersystemContractAcceptanceError::simulation_failure};
    }
  }
  auto departing =
      ascent_ticks < ascent_limit
          ? depart_planetary_orbit(target_system, planetfall->flight)
          : std::expected<SystemFlightState, SystemFlightError>{
                std::unexpected{SystemFlightError::planet_departure_refused}};
  if (!departing || !advance_intersystem_contract(
                        *document.state.intersystem_contract,
                        document.state.intersystem_contract->universe_tick,
                        IntersystemContractCommand::leave_target_planet)) {
    return std::unexpected{
        IntersystemContractAcceptanceError::transition_failure};
  }
  document.state.flight.reset();
  document.state.system_flight = *departing;
  const auto return_view = resolve_onboarding_navigation_view(
      route, document.state.onboarding, route.destination);
  UniverseNavigationSelectionState return_selection;
  if (!return_view ||
      !advance_universe_navigation_selection(
          *return_view, return_selection,
          UniverseNavigationSelectionCommand::select) ||
      return_selection.pending_destination != route.origin ||
      !begin_intersystem_jump(
          *document.state.intersystem_contract,
          *return_selection.pending_destination)) {
    return std::unexpected{
        IntersystemContractAcceptanceError::transition_failure};
  }
  for (SimulationTick tick = 0; tick < kJumpSpoolTicks + kJumpTransitTicks;
       ++tick) {
    const auto advanced = advance_intersystem_jump_tick(
        *document.state.intersystem_contract, origin_system);
    if (!advanced) {
      return std::unexpected{
          IntersystemContractAcceptanceError::simulation_failure};
    }
    if (advanced->committed)
      document.state.system_flight.reset();
  }
  auto origin_return = initialize_origin_return(
      *document.state.intersystem_contract, origin_system);
  if (!origin_return) {
    return std::unexpected{
        IntersystemContractAcceptanceError::initialization_failure};
  }
  document.state.origin_station_flight = *origin_return;

  constexpr SimulationTick approach_limit{90U * kSimulationHz};
  bool origin_checkpointed{};
  SimulationTick approach_ticks{};
  for (; approach_ticks < approach_limit; ++approach_ticks) {
    const auto guidance = resolve_origin_station_flight_guidance(
        *document.state.intersystem_contract, origin_system, *origin_return);
    if (!guidance) {
      return std::unexpected{
          IntersystemContractAcceptanceError::simulation_failure};
    }
    if (guidance->arrived)
      break;
    if (!origin_checkpointed && approach_ticks == approach_limit / 3U) {
      document.state.origin_station_flight = *origin_return;
      if (!record_checkpoint(document, "origin-return", resume_stage,
                             checkpoints) ||
          !document.state.origin_station_flight) {
        return std::unexpected{
            IntersystemContractAcceptanceError::persistence_failure};
      }
      origin_return = *document.state.origin_station_flight;
      origin_checkpointed = true;
    }
    if (!advance_origin_station_flight(*document.state.intersystem_contract,
                                       origin_system, *origin_return, {}) ||
        !advance_intersystem_time(*document.state.intersystem_contract, 1U)) {
      return std::unexpected{
          IntersystemContractAcceptanceError::simulation_failure};
    }
  }
  const auto final_guidance = resolve_origin_station_flight_guidance(
      *document.state.intersystem_contract, origin_system, *origin_return);
  if (!origin_checkpointed || !final_guidance || !final_guidance->arrived) {
    return std::unexpected{IntersystemContractAcceptanceError::incomplete_path};
  }

  std::vector<termforge::Pixel> final_frame(static_cast<std::size_t>(width) *
                                            static_cast<std::size_t>(height));
  LocalSystemRenderer origin_renderer{{.width = width, .height = height}};
  const auto selected = origin_system.planets.front().descriptor.id;
  const auto origin_pose = resolve_origin_station_flight_pose(
      *document.state.intersystem_contract, origin_system, *origin_return);
  if (!origin_pose) {
    std::fprintf(stderr,
                 "contract-three origin presentation pose failed\n");
    return std::unexpected{
        IntersystemContractAcceptanceError::presentation_failure};
  }
  const auto render_start = Clock::now();
  const LocalSystemView origin_view{
      .time = {document.state.intersystem_contract->universe_tick, 0.0},
      .position = origin_pose->position,
      .velocity = origin_pose->velocity,
      .forward = origin_return->forward,
      .up = origin_return->up,
      .selected_planet = selected};
  const auto rendered =
      origin_renderer.render(origin_system, origin_view, final_frame);
  const auto station_ephemeris = resolve_origin_station_ephemeris(
      origin_system,
      generate_origin_station(
          document.state.intersystem_contract->identities.universe_seed),
      origin_view.time);
  const auto marked =
      station_ephemeris
          ? origin_renderer.render_origin_station(
                origin_view, *station_ephemeris, final_frame)
          : std::expected<void, LocalSystemRenderError>{
                std::unexpected{LocalSystemRenderError::ephemeris_failure}};
  render_ms += milliseconds(render_start, Clock::now());
  if (!rendered || !marked) {
    std::fprintf(stderr,
                 "contract-three origin presentation render failed\n");
    return std::unexpected{
        IntersystemContractAcceptanceError::presentation_failure};
  }
  const auto framebuffer_checksum = pixel_checksum(final_frame);

  if (!attempt_origin_docking(*document.state.intersystem_contract,
                              origin_system, *origin_return)) {
    return std::unexpected{
        IntersystemContractAcceptanceError::transition_failure};
  }
  document.state.origin_station_flight.reset();
  if (!record_checkpoint(document, "returned-docked", resume_stage,
                         checkpoints) ||
      !advance_intersystem_contract(
          *document.state.intersystem_contract,
          document.state.intersystem_contract->universe_tick,
          IntersystemContractCommand::turn_in) ||
      !advance_onboarding(document.state.onboarding,
                          OnboardingCommand::complete_contract_three) ||
      !validate_save_document(document)) {
    return std::unexpected{
        IntersystemContractAcceptanceError::persistence_failure};
  }
  const auto checksum = document_checksum(document);
  if (!checksum ||
      document.state.intersystem_contract->mission_phase !=
          IntersystemMissionPhase::turned_in ||
      document.state.intersystem_contract->travel_phase !=
          IntersystemTravelPhase::docked_at_origin ||
      document.state.onboarding.state != OnboardingState::completed ||
      document.state.onboarding.chapter ||
      document.state.discoveries.size() != 1U ||
      document.state.world_deltas.size() != 1U) {
    return std::unexpected{IntersystemContractAcceptanceError::incomplete_path};
  }
  const double total_ms = milliseconds(replay_start, Clock::now());
  return Replay{
      .final_document = std::move(document),
      .checkpoints = std::move(checkpoints),
      .final_frame = std::move(final_frame),
      .final_checksum = *checksum,
      .target_system_planet_count = target_system.planets.size(),
      .target_system_initial_framebuffer_checksum = system_frames->first,
      .target_system_moved_framebuffer_checksum = system_frames->second,
      .framebuffer_checksum = framebuffer_checksum,
      .simulation_ms = std::max(0.0, total_ms - render_ms),
      .application_render_ms = render_ms,
      .outbound_selected_system = *outbound_selection.pending_destination,
      .return_selected_system = *return_selection.pending_destination,
      .universe_navigation_rows = outbound_view->destinations.size(),
  };
}

}  // namespace

auto run_intersystem_contract_acceptance(int width, int height)
    -> std::expected<IntersystemContractAcceptanceResult,
                     IntersystemContractAcceptanceError> {
  auto prepared = guided_contract_three_document(
      Seed{kIntersystemContractAcceptanceSeed});
  if (!prepared) return std::unexpected{prepared.error()};
  return run_intersystem_contract_acceptance(*prepared, width, height, true);
}

auto run_intersystem_contract_acceptance(
    const SaveDocument& starting_document, int width, int height,
    bool verify_pilot_recovery)
    -> std::expected<IntersystemContractAcceptanceResult,
                     IntersystemContractAcceptanceError> {
  if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
      static_cast<std::size_t>(width) >
          std::numeric_limits<std::size_t>::max() /
              static_cast<std::size_t>(height) ||
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) >
          4'194'304U) {
    return std::unexpected{
        IntersystemContractAcceptanceError::invalid_configuration};
  }
  auto baseline = replay(starting_document, width, height, std::nullopt);
  if (!baseline || !baseline->final_document.state.intersystem_contract) {
    return std::unexpected{
        baseline ? IntersystemContractAcceptanceError::incomplete_path
                 : baseline.error()};
  }
  if (baseline->checkpoints.size() != kResumeStages.size()) {
    return std::unexpected{IntersystemContractAcceptanceError::incomplete_path};
  }
  for (std::size_t index = 0; index < kResumeStages.size(); ++index) {
    auto resumed = replay(starting_document, width, height,
                          kResumeStages[index]);
    if (!resumed || resumed->final_document != baseline->final_document ||
        resumed->final_checksum != baseline->final_checksum ||
        resumed->final_frame != baseline->final_frame) {
      return std::unexpected{
          IntersystemContractAcceptanceError::resume_mismatch};
    }
    baseline->checkpoints[index].resumed_final_checksum =
        resumed->final_checksum;
  }
  const auto& contract = *baseline->final_document.state.intersystem_contract;
  const auto onboarding_access =
      resolve_onboarding_access(baseline->final_document.state.onboarding);
  std::uint64_t recovery_checksum{};
  if (verify_pilot_recovery) {
    const auto recovery = run_intersystem_planetfall_acceptance(width, height);
    if (!recovery ||
        recovery->report.planet != contract.identities.target_planet ||
        recovery->report.target != contract.identities.target_objective ||
        recovery->report.abort_orbit_checksum == 0U) {
      return std::unexpected{
          IntersystemContractAcceptanceError::recovery_failure};
    }
    recovery_checksum = recovery->report.abort_orbit_checksum;
  }
  if (!onboarding_access) {
    return std::unexpected{
        IntersystemContractAcceptanceError::recovery_failure};
  }
  return IntersystemContractAcceptanceResult{
      .report = {.mission = contract.identities.mission,
                 .target_system = contract.identities.target_system,
                 .target_planet = contract.identities.target_planet,
                 .target_objective = contract.identities.target_objective,
                 .origin_station = contract.identities.origin_station,
                 .outbound_selected_system =
                     baseline->outbound_selected_system,
                 .return_selected_system =
                     baseline->return_selected_system,
                 .universe_navigation_rows =
                     baseline->universe_navigation_rows,
                 .open_exploration_available =
                     onboarding_access->open_exploration_available,
                 .checkpoints = std::move(baseline->checkpoints),
                 .final_tick = contract.universe_tick,
                 .final_authoritative_checksum = baseline->final_checksum,
                 .wrong_side_recovery_checksum = recovery_checksum,
                 .target_system_planet_count =
                     baseline->target_system_planet_count,
                 .target_system_initial_framebuffer_checksum =
                     baseline->target_system_initial_framebuffer_checksum,
                 .target_system_moved_framebuffer_checksum =
                     baseline->target_system_moved_framebuffer_checksum,
                 .discovery_count =
                     baseline->final_document.state.discoveries.size(),
                 .world_delta_count =
                     baseline->final_document.state.world_deltas.size(),
                 .width = width,
                 .height = height,
                 .framebuffer_checksum = baseline->framebuffer_checksum,
                 .simulation_ms = baseline->simulation_ms,
                 .application_render_ms = baseline->application_render_ms},
      .returned_save = std::move(baseline->final_document),
      .final_frame = std::move(baseline->final_frame)};
}

auto intersystem_contract_acceptance_json(
    const IntersystemContractAcceptanceReport& report) -> std::string {
  std::string checkpoints;
  for (std::size_t index = 0; index < report.checkpoints.size(); ++index) {
    const auto& checkpoint = report.checkpoints[index];
    checkpoints += std::format(
        "    {{\"name\": \"{}\", \"tick\": \"{}\", "
        "\"authoritative_checksum\": \"{}\", "
        "\"resumed_final_checksum\": \"{}\"}}{}\n",
        checkpoint.name, checkpoint.tick, checkpoint.authoritative_checksum,
        checkpoint.resumed_final_checksum,
        index + 1U == report.checkpoints.size() ? "" : ",");
  }
  return std::format(
      "{{\n"
      "  \"schema_version\": 3,\n"
      "  \"scenario\": \"{}\",\n"
      "  \"evidence_scope\": \"application_framebuffer\",\n"
      "  \"seed\": \"{}\",\n"
      "  \"mission_id\": \"{}\",\n"
      "  \"target_system_id\": \"{}\",\n"
      "  \"target_planet_id\": \"planet-{:016x}\",\n"
      "  \"target_objective_id\": \"{}\",\n"
      "  \"origin_station_id\": \"{}\",\n"
      "  \"outbound_selected_system_id\": \"{}\",\n"
      "  \"return_selected_system_id\": \"{}\",\n"
      "  \"universe_navigation_rows\": {},\n"
      "  \"checkpoints\": [\n{}  ],\n"
      "  \"final_tick\": \"{}\",\n"
      "  \"final_mission_phase\": \"turned_in\",\n"
      "  \"final_onboarding_state\": \"completed\",\n"
      "  \"open_exploration_available\": {},\n"
      "  \"final_authoritative_checksum\": \"{}\",\n"
      "  \"wrong_side_recovery_checksum\": \"{}\",\n"
      "  \"target_system_planet_count\": {},\n"
      "  \"target_system_initial_framebuffer_checksum\": \"{}\",\n"
      "  \"target_system_moved_framebuffer_checksum\": \"{}\",\n"
      "  \"discovery_count\": {},\n"
      "  \"world_delta_count\": {},\n"
      "  \"viewport\": {{\"width\": {}, \"height\": {}}},\n"
      "  \"framebuffer_checksum\": \"{}\",\n"
      "  \"timings\": {{\"simulation_ms\": {:.3f}, "
      "\"application_render_ms\": {:.3f}, "
      "\"terminal_proxy\": \"external-live-capture\"}}\n"
      "}}\n",
      kIntersystemContractAcceptanceScenario,
      kIntersystemContractAcceptanceSeed, mission_id_string(report.mission),
      system_id_string(report.target_system), report.target_planet.value,
      surface_signal_id_string(report.target_objective),
      origin_station_id_string(report.origin_station),
      system_id_string(report.outbound_selected_system),
      system_id_string(report.return_selected_system),
      report.universe_navigation_rows, checkpoints, report.final_tick,
      report.open_exploration_available ? "true" : "false",
      report.final_authoritative_checksum,
      report.wrong_side_recovery_checksum, report.target_system_planet_count,
      report.target_system_initial_framebuffer_checksum,
      report.target_system_moved_framebuffer_checksum, report.discovery_count,
      report.world_delta_count, report.width, report.height,
      report.framebuffer_checksum, report.simulation_ms,
      report.application_render_ms);
}

}  // namespace apsis_drift
