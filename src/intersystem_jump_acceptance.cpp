#include "apsis_drift/intersystem_jump_acceptance.hpp"

#include <cmath>
#include <format>
#include <limits>
#include <utility>

#include "apsis_drift/benchmark.hpp"
#include "apsis_drift/intersystem_jump.hpp"
#include "apsis_drift/save_file.hpp"
#include "apsis_drift/save_schema.hpp"

namespace apsis_drift {
namespace {

struct PilotArrival {
  IntersystemContractState contract;
  std::int32_t initial_heading_error_millidegrees{};
  std::int32_t initial_velocity_error_basis_points{};
};

[[nodiscard]] auto run_pilot_arrival(
    IntersystemJumpAlignmentState alignment, bool persist_at_commit)
    -> std::expected<PilotArrival, IntersystemJumpAcceptanceError> {
  auto document = make_new_game_document(Seed{kIntersystemJumpAcceptanceSeed});
  if (!document.state.intersystem_contract) {
    return std::unexpected{
        IntersystemJumpAcceptanceError::transition_failure};
  }
  auto& contract = *document.state.intersystem_contract;
  const auto target =
      generate_local_system(contract.identities.target_system_seed);
  if (!advance_intersystem_contract(
          contract, contract.universe_tick,
          IntersystemContractCommand::select_pilot_profile) ||
      !advance_intersystem_contract(
          contract, contract.universe_tick,
          IntersystemContractCommand::accept_mission) ||
      !advance_intersystem_contract(
          contract, contract.universe_tick,
          IntersystemContractCommand::launch) ||
      !begin_intersystem_jump(contract) || !contract.jump_alignment) {
    return std::unexpected{
        IntersystemJumpAcceptanceError::transition_failure};
  }
  PilotArrival result{
      .contract = {},
      .initial_heading_error_millidegrees =
          contract.jump_alignment->heading_error_millidegrees,
      .initial_velocity_error_basis_points =
          contract.jump_alignment->velocity_error_basis_points,
  };
  contract.jump_alignment = alignment;
  for (SimulationTick tick = 0; tick < kJumpSpoolTicks; ++tick) {
    if (!advance_intersystem_jump_tick(contract, target)) {
      return std::unexpected{
          IntersystemJumpAcceptanceError::transition_failure};
    }
  }
  if (persist_at_commit) {
    const auto encoded = encode_save_document_json(document);
    const auto restored =
        encoded ? decode_save_document_json(*encoded)
                : std::expected<SaveDocument, SaveSchemaError>{
                      std::unexpected{SaveSchemaError{}}};
    if (!restored || !restored->state.intersystem_contract) {
      return std::unexpected{
          IntersystemJumpAcceptanceError::persistence_failure};
    }
    document = *restored;
  }
  auto& resumed = *document.state.intersystem_contract;
  for (SimulationTick tick = 0; tick < kJumpTransitTicks; ++tick) {
    if (!advance_intersystem_jump_tick(resumed, target)) {
      return std::unexpected{
          IntersystemJumpAcceptanceError::transition_failure};
    }
  }
  if (resumed.travel_phase !=
          IntersystemTravelPhase::target_system_flight ||
      !resumed.arrival_solution || !resumed.arrival_solution->assessment) {
    return std::unexpected{
        IntersystemJumpAcceptanceError::transition_failure};
  }
  result.contract = resumed;
  return result;
}

[[nodiscard]] auto distance_from_target(
    const LocalSystemDescriptor& target,
    const IntersystemArrivalSolution& arrival)
    -> std::expected<double, IntersystemJumpAcceptanceError> {
  if (!arrival.reference_planet) {
    return std::unexpected{
        IntersystemJumpAcceptanceError::transition_failure};
  }
  const auto ephemeris = resolve_planet_ephemeris(
      target, *arrival.reference_planet,
      {.tick = arrival.arrival_tick, .sub_tick_fraction = 0.0});
  if (!ephemeris) {
    return std::unexpected{
        IntersystemJumpAcceptanceError::transition_failure};
  }
  return std::hypot(arrival.position.x - ephemeris->position.x,
                    arrival.position.y - ephemeris->position.y,
                    arrival.position.z - ephemeris->position.z);
}

}  // namespace

auto run_intersystem_jump_acceptance(int width, int height)
    -> std::expected<IntersystemJumpAcceptanceResult,
                     IntersystemJumpAcceptanceError> {
  if (width <= 0 || height <= 0 ||
      static_cast<std::size_t>(width) >
          std::numeric_limits<std::size_t>::max() /
              static_cast<std::size_t>(height) ||
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) >
          kMaximumIntersystemJumpAcceptancePixels) {
    return std::unexpected{
        IntersystemJumpAcceptanceError::invalid_configuration};
  }
  auto document = make_new_game_document(Seed{kIntersystemJumpAcceptanceSeed});
  if (!document.state.intersystem_contract) {
    return std::unexpected{
        IntersystemJumpAcceptanceError::transition_failure};
  }
  auto& state = *document.state.intersystem_contract;
  const auto target = generate_local_system(state.identities.target_system_seed);
  if (!advance_intersystem_contract(
          state, state.universe_tick,
          IntersystemContractCommand::accept_mission) ||
      !advance_intersystem_contract(
          state, state.universe_tick, IntersystemContractCommand::launch) ||
      !begin_intersystem_jump(state)) {
    return std::unexpected{
        IntersystemJumpAcceptanceError::transition_failure};
  }
  SimulationTick committed_tick{};
  for (SimulationTick tick = 0; tick < kJumpSpoolTicks; ++tick) {
    const auto advanced = advance_intersystem_jump_tick(state, target);
    if (!advanced) {
      return std::unexpected{
          IntersystemJumpAcceptanceError::transition_failure};
    }
    if (advanced->committed) committed_tick = state.universe_tick;
  }
  if (!state.arrival_solution || committed_tick == 0) {
    return std::unexpected{
        IntersystemJumpAcceptanceError::transition_failure};
  }

  const auto encoded = encode_save_document_json(document);
  if (!encoded) {
    return std::unexpected{
        IntersystemJumpAcceptanceError::persistence_failure};
  }
  const auto restored = decode_save_document_json(*encoded);
  if (!restored || !restored->state.intersystem_contract) {
    return std::unexpected{
        IntersystemJumpAcceptanceError::persistence_failure};
  }
  document = *restored;
  auto& resumed = *document.state.intersystem_contract;
  for (SimulationTick tick = 0; tick < kJumpTransitTicks / 2; ++tick) {
    if (!advance_intersystem_jump_tick(resumed, target)) {
      return std::unexpected{
          IntersystemJumpAcceptanceError::transition_failure};
    }
  }
  const auto snapshot = intersystem_jump_snapshot(resumed);
  std::vector<termforge::Pixel> frame(static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(height));
  if (!snapshot || !render_intersystem_jump(*snapshot, width, height, frame)) {
    return std::unexpected{IntersystemJumpAcceptanceError::render_failure};
  }
  for (SimulationTick tick = kJumpTransitTicks / 2;
       tick < kJumpTransitTicks; ++tick) {
    if (!advance_intersystem_jump_tick(resumed, target)) {
      return std::unexpected{
          IntersystemJumpAcceptanceError::transition_failure};
    }
  }
  if (resumed.travel_phase !=
          IntersystemTravelPhase::target_system_flight ||
      !resumed.arrival_solution || !resumed.arrival_solution->assessment) {
    return std::unexpected{
        IntersystemJumpAcceptanceError::transition_failure};
  }
  const auto pilot_aligned = run_pilot_arrival(
      IntersystemJumpAlignmentState{}, false);
  const auto pilot_offset = run_pilot_arrival(
      IntersystemJumpAlignmentState{
          .heading_error_millidegrees = 30'000,
          .velocity_error_basis_points = 1'000,
          .controls = {}},
      true);
  const auto pilot_opposed = run_pilot_arrival(
      IntersystemJumpAlignmentState{
          .heading_error_millidegrees = 90'000,
          .velocity_error_basis_points = 3'000,
          .controls = {}},
      false);
  if (!pilot_aligned || !pilot_offset || !pilot_opposed ||
      !pilot_aligned->contract.arrival_solution ||
      !pilot_offset->contract.arrival_solution ||
      !pilot_opposed->contract.arrival_solution) {
    return std::unexpected{
        IntersystemJumpAcceptanceError::transition_failure};
  }
  const auto offset_distance =
      distance_from_target(target, *pilot_offset->contract.arrival_solution);
  const auto opposed_distance =
      distance_from_target(target, *pilot_opposed->contract.arrival_solution);
  if (!offset_distance || !opposed_distance) {
    return std::unexpected{
        IntersystemJumpAcceptanceError::transition_failure};
  }
  return IntersystemJumpAcceptanceResult{
      .report =
          IntersystemJumpAcceptanceReport{
              .destination = resumed.current_system,
              .reference_planet = *resumed.arrival_solution->reference_planet,
              .committed_tick = committed_tick,
              .arrival_tick = resumed.arrival_solution->arrival_tick,
              .arrival_checksum = intersystem_arrival_checksum(resumed),
              .assisted_quality =
                  resumed.arrival_solution->assessment->quality,
              .pilot_initial_heading_error_millidegrees =
                  pilot_aligned->initial_heading_error_millidegrees,
              .pilot_initial_velocity_error_basis_points =
                  pilot_aligned->initial_velocity_error_basis_points,
              .pilot_aligned_checksum =
                  intersystem_arrival_checksum(pilot_aligned->contract),
              .pilot_offset_checksum =
                  intersystem_arrival_checksum(pilot_offset->contract),
              .pilot_opposed_checksum =
                  intersystem_arrival_checksum(pilot_opposed->contract),
              .pilot_offset_distance_metres = *offset_distance,
              .pilot_opposed_distance_metres = *opposed_distance,
              .framebuffer_checksum = pixel_checksum(frame),
              .width = width,
              .height = height,
          },
      .transit_frame = std::move(frame),
  };
}

auto intersystem_jump_acceptance_json(
    const IntersystemJumpAcceptanceReport& report,
    std::string_view presentation) -> std::string {
  return std::format(
      "{{\n"
      "  \"schema_version\": 2,\n"
      "  \"scenario\": \"v0.4.15-pilot-ftl-alignment\",\n"
      "  \"presentation\": \"{}\",\n"
      "  \"destination_system_id\": \"{}\",\n"
      "  \"reference_planet_id\": \"planet-{:016x}\",\n"
      "  \"committed_tick\": \"{}\",\n"
      "  \"arrival_tick\": \"{}\",\n"
      "  \"arrival_checksum\": \"{}\",\n"
      "  \"assisted_quality\": \"{}\",\n"
      "  \"pilot_initial_heading_error_millidegrees\": {},\n"
      "  \"pilot_initial_velocity_error_basis_points\": {},\n"
      "  \"pilot_aligned_checksum\": \"{}\",\n"
      "  \"pilot_offset_checksum\": \"{}\",\n"
      "  \"pilot_opposed_checksum\": \"{}\",\n"
      "  \"pilot_offset_distance_metres\": \"{:.3f}\",\n"
      "  \"pilot_opposed_distance_metres\": \"{:.3f}\",\n"
      "  \"viewport\": {{\"width\": {}, \"height\": {}}},\n"
      "  \"framebuffer_checksum\": \"{}\"\n"
      "}}\n",
      presentation, system_id_string(report.destination),
      report.reference_planet.value, report.committed_tick,
      report.arrival_tick, report.arrival_checksum,
      intersystem_arrival_quality_name(report.assisted_quality),
      report.pilot_initial_heading_error_millidegrees,
      report.pilot_initial_velocity_error_basis_points,
      report.pilot_aligned_checksum, report.pilot_offset_checksum,
      report.pilot_opposed_checksum, report.pilot_offset_distance_metres,
      report.pilot_opposed_distance_metres, report.width, report.height,
      report.framebuffer_checksum);
}

}  // namespace apsis_drift
