#include "apsis_drift/intersystem_return_acceptance.hpp"

#include <format>
#include <limits>
#include <numbers>

#include "apsis_drift/benchmark.hpp"
#include "apsis_drift/intersystem_jump.hpp"
#include "apsis_drift/origin_return.hpp"
#include "apsis_drift/save_file.hpp"
#include "apsis_drift/save_schema.hpp"
#include "apsis_drift/system_flight.hpp"
#include "apsis_drift/system_rendering.hpp"
#include "apsis_drift/world_delta_journal.hpp"

namespace apsis_drift {
namespace {

[[nodiscard]] auto checkpoint(SaveDocument& document)
    -> std::expected<void, IntersystemReturnAcceptanceError> {
  const auto encoded = encode_save_document_json(document);
  const auto decoded =
      encoded ? decode_save_document_json(*encoded)
              : std::expected<SaveDocument, SaveSchemaError>{
                    std::unexpected{SaveSchemaError{}}};
  if (!decoded) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::persistence_failure};
  }
  document = std::move(*decoded);
  return {};
}

[[nodiscard]] auto reach_completed_orbit(IntersystemContractState& contract)
    -> std::expected<void, IntersystemReturnAcceptanceError> {
  const auto command = [&](IntersystemContractCommand value) {
    return advance_intersystem_contract(contract, contract.universe_tick,
                                        value);
  };
  if (!command(IntersystemContractCommand::accept_mission) ||
      !command(IntersystemContractCommand::launch) ||
      !command(IntersystemContractCommand::begin_outbound_jump) ||
      !advance_intersystem_time(contract, kJumpSpoolTicks) ||
      !command(IntersystemContractCommand::commit_outbound_jump) ||
      !advance_intersystem_time(contract, kJumpTransitTicks) ||
      !command(IntersystemContractCommand::arrive_target_system) ||
      !command(IntersystemContractCommand::enter_target_planet) ||
      !command(IntersystemContractCommand::complete_objective)) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::transition_failure};
  }
  return {};
}

}  // namespace

auto run_intersystem_return_acceptance(int width, int height)
    -> std::expected<IntersystemReturnAcceptanceResult,
                     IntersystemReturnAcceptanceError> {
  if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
      static_cast<std::size_t>(width) >
          std::numeric_limits<std::size_t>::max() /
              static_cast<std::size_t>(height) ||
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) >
          4'194'304U) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::invalid_configuration};
  }
  auto document =
      make_new_game_document(Seed{kIntersystemReturnAcceptanceSeed});
  if (!document.state.intersystem_contract) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::initialization_failure};
  }
  auto& contract = *document.state.intersystem_contract;
  if (!reach_completed_orbit(contract)) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::transition_failure};
  }
  const auto target_system =
      generate_local_system(contract.identities.target_system_seed);
  const auto target_body = find_local_system_planet(
      target_system, contract.identities.target_planet);
  if (!target_body) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::initialization_failure};
  }
  const double radius =
      static_cast<double>((*target_body)->descriptor.radius.value) * 1'000.0;
  auto orbital = initial_planetary_flight_state(
      (*target_body)->descriptor,
      {.latitude_radians = 0.35,
       .longitude_radians = std::numbers::pi_v<double>,
       .altitude_metres = radius * 2.0},
      {.surface_elevation_metres = 0.0}, 1.1, FlightMode::autopilot);
  if (!orbital) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::initialization_failure};
  }
  orbital->tick = contract.universe_tick;
  orbital->regime = FlightRegime::orbital;
  auto system_flight = depart_planetary_orbit(target_system, *orbital);
  if (!system_flight ||
      !advance_intersystem_contract(
          contract, contract.universe_tick,
          IntersystemContractCommand::leave_target_planet)) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::transition_failure};
  }
  const auto departure_tick = contract.universe_tick;
  const auto departure_checksum =
      system_flight_state_checksum(*system_flight);
  document.state.flight.reset();
  document.state.system_flight = *system_flight;
  document.state.discoveries = {
      {contract.identities.target_objective, contract.universe_tick}};
  document.state.world_deltas = {
      {surface_signal_object_key(contract.identities.target_objective),
       SaveWorldDeltaKind::collected, contract.universe_tick}};
  if (!checkpoint(document)) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::persistence_failure};
  }

  auto resumed_contract = *document.state.intersystem_contract;
  const auto persist = [&]()
      -> std::expected<void, IntersystemReturnAcceptanceError> {
    document.state.intersystem_contract = resumed_contract;
    const auto saved = checkpoint(document);
    if (saved && document.state.intersystem_contract) {
      resumed_contract = *document.state.intersystem_contract;
    }
    return saved;
  };
  if (!document.state.system_flight ||
      !begin_intersystem_jump(resumed_contract)) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::transition_failure};
  }
  const auto frozen_tick = document.state.system_flight->tick;
  for (SimulationTick tick = 0; tick < kSimulationHz / 2; ++tick) {
    if (!advance_intersystem_jump_tick(resumed_contract,
                                   generate_local_system(
                                       resumed_contract.identities
                                           .origin_system_seed))) {
      return std::unexpected{
          IntersystemReturnAcceptanceError::simulation_failure};
    }
  }
  if (!cancel_intersystem_jump(resumed_contract)) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::transition_failure};
  }
  document.state.system_flight->tick = resumed_contract.universe_tick;
  document.state.system_flight->controls = {};
  if (frozen_tick >= document.state.system_flight->tick ||
      !persist() || !begin_intersystem_jump(resumed_contract)) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::persistence_failure};
  }

  const auto origin_system =
      generate_local_system(resumed_contract.identities.origin_system_seed);
  SimulationTick commit_tick{};
  for (SimulationTick tick = 0; tick < kJumpSpoolTicks; ++tick) {
    const auto advanced =
        advance_intersystem_jump_tick(resumed_contract, origin_system);
    if (!advanced) {
      return std::unexpected{
          IntersystemReturnAcceptanceError::simulation_failure};
    }
    if (advanced->committed) {
      commit_tick = resumed_contract.universe_tick;
      document.state.system_flight.reset();
    }
  }
  if (commit_tick == 0 || !persist()) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::persistence_failure};
  }
  for (SimulationTick tick = 0; tick < kJumpTransitTicks; ++tick) {
    if (!advance_intersystem_jump_tick(resumed_contract, origin_system)) {
      return std::unexpected{
          IntersystemReturnAcceptanceError::simulation_failure};
    }
  }
  const auto arrival_tick = resumed_contract.universe_tick;
  auto origin_return = initialize_origin_return(resumed_contract);
  if (!origin_return) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::initialization_failure};
  }
  const auto arrival_checksum = origin_return_state_checksum(*origin_return);
  document.state.origin_return = *origin_return;
  if (!persist() || !document.state.origin_return) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::persistence_failure};
  }

  std::vector<termforge::Pixel> frame(static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(height));
  LocalSystemRenderer renderer{{.width = width,
                                .height = height,
                                .field_of_view_degrees = 60.0}};
  const auto selected = origin_system.planets.front().descriptor.id;
  const LocalSystemView view{
      .time = {resumed_contract.universe_tick, 0.0},
      .position = document.state.origin_return->position,
      .velocity = document.state.origin_return->velocity,
      .forward = document.state.origin_return->forward,
      .up = document.state.origin_return->up,
      .selected_planet = selected,
  };
  if (!renderer.render(origin_system, view, frame) ||
      !render_origin_station_marker(width, height, frame)) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::presentation_failure};
  }
  const auto framebuffer_checksum = pixel_checksum(frame);

  constexpr SimulationTick approach_limit{90 * kSimulationHz};
  bool resumed_midway{};
  for (SimulationTick tick = 0; tick < approach_limit; ++tick) {
    const auto guidance = resolve_origin_return_guidance(
        resumed_contract, *document.state.origin_return);
    if (!guidance) {
      return std::unexpected{
          IntersystemReturnAcceptanceError::simulation_failure};
    }
    if (guidance->arrived) break;
    auto next_return = *document.state.origin_return;
    if (!advance_origin_return(resumed_contract, next_return, {}) ||
        !advance_intersystem_time(resumed_contract, 1)) {
      return std::unexpected{
          IntersystemReturnAcceptanceError::simulation_failure};
    }
    document.state.origin_return = std::move(next_return);
    if (!resumed_midway && tick == approach_limit / 3) {
      if (!persist()) {
        return std::unexpected{
            IntersystemReturnAcceptanceError::persistence_failure};
      }
      resumed_midway = true;
    }
  }
  const auto final_guidance = resolve_origin_return_guidance(
      resumed_contract, *document.state.origin_return);
  if (!final_guidance || !final_guidance->arrived || !resumed_midway) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::incomplete_path};
  }
  const auto docked_return_checksum =
      origin_return_state_checksum(*document.state.origin_return);
  if (!advance_intersystem_contract(
          resumed_contract, resumed_contract.universe_tick,
          IntersystemContractCommand::dock_at_origin)) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::transition_failure};
  }
  const auto docking_tick = resumed_contract.universe_tick;
  document.state.origin_return.reset();
  if (!persist() ||
      !advance_intersystem_contract(
          resumed_contract, resumed_contract.universe_tick,
          IntersystemContractCommand::turn_in) ||
      advance_intersystem_contract(
          resumed_contract, resumed_contract.universe_tick,
          IntersystemContractCommand::turn_in) ||
      !persist() ||
      resumed_contract.mission_phase != IntersystemMissionPhase::turned_in ||
      document.state.discoveries.size() != 1U ||
      document.state.discoveries.front().signal !=
          resumed_contract.identities.target_objective ||
      document.state.world_deltas.size() != 1U) {
    return std::unexpected{
        IntersystemReturnAcceptanceError::incomplete_path};
  }
  return IntersystemReturnAcceptanceResult{
      .report = {.station = resumed_contract.identities.origin_station,
                 .departure_tick = departure_tick,
                 .return_commit_tick = commit_tick,
                 .origin_arrival_tick = arrival_tick,
                 .docking_tick = docking_tick,
                 .departure_checksum = departure_checksum,
                 .origin_arrival_checksum = arrival_checksum,
                 .docked_return_checksum = docked_return_checksum,
                 .discovery_count = document.state.discoveries.size(),
                 .world_delta_count = document.state.world_deltas.size(),
                 .width = width,
                 .height = height,
                 .framebuffer_checksum = framebuffer_checksum},
      .final_frame = std::move(frame),
  };
}

auto intersystem_return_acceptance_json(
    const IntersystemReturnAcceptanceReport& report,
    std::string_view presentation) -> std::string {
  return std::format(
      "{{\n"
      "  \"schema_version\": 1,\n"
      "  \"scenario\": \"{}\",\n"
      "  \"presentation\": \"{}\",\n"
      "  \"origin_station_id\": \"{}\",\n"
      "  \"departure_tick\": \"{}\",\n"
      "  \"return_commit_tick\": \"{}\",\n"
      "  \"origin_arrival_tick\": \"{}\",\n"
      "  \"docking_tick\": \"{}\",\n"
      "  \"departure_checksum\": \"{}\",\n"
      "  \"origin_arrival_checksum\": \"{}\",\n"
      "  \"docked_return_checksum\": \"{}\",\n"
      "  \"discovery_count\": {},\n"
      "  \"world_delta_count\": {},\n"
      "  \"viewport\": {{\"width\": {}, \"height\": {}}},\n"
      "  \"framebuffer_checksum\": \"{}\"\n"
      "}}\n",
      kIntersystemReturnAcceptanceScenario, presentation,
      origin_station_id_string(report.station), report.departure_tick,
      report.return_commit_tick, report.origin_arrival_tick,
      report.docking_tick, report.departure_checksum,
      report.origin_arrival_checksum, report.docked_return_checksum,
      report.discovery_count, report.world_delta_count, report.width,
      report.height,
      report.framebuffer_checksum);
}

}  // namespace apsis_drift
