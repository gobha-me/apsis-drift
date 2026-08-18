#include "apsis_drift/intersystem_jump_acceptance.hpp"

#include <format>
#include <limits>
#include <utility>

#include "apsis_drift/benchmark.hpp"
#include "apsis_drift/intersystem_jump.hpp"
#include "apsis_drift/save_file.hpp"
#include "apsis_drift/save_schema.hpp"

namespace apsis_drift {

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
      !begin_assisted_jump(state)) {
    return std::unexpected{
        IntersystemJumpAcceptanceError::transition_failure};
  }
  SimulationTick committed_tick{};
  for (SimulationTick tick = 0; tick < kJumpSpoolTicks; ++tick) {
    const auto advanced = advance_assisted_jump_tick(state, target);
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
    if (!advance_assisted_jump_tick(resumed, target)) {
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
    if (!advance_assisted_jump_tick(resumed, target)) {
      return std::unexpected{
          IntersystemJumpAcceptanceError::transition_failure};
    }
  }
  if (resumed.travel_phase !=
          IntersystemTravelPhase::target_system_flight ||
      !resumed.arrival_solution) {
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
      "  \"schema_version\": 1,\n"
      "  \"scenario\": \"v0.4.8-assisted-intersystem-jump\",\n"
      "  \"presentation\": \"{}\",\n"
      "  \"destination_system_id\": \"{}\",\n"
      "  \"reference_planet_id\": \"planet-{:016x}\",\n"
      "  \"committed_tick\": \"{}\",\n"
      "  \"arrival_tick\": \"{}\",\n"
      "  \"arrival_checksum\": \"{}\",\n"
      "  \"viewport\": {{\"width\": {}, \"height\": {}}},\n"
      "  \"framebuffer_checksum\": \"{}\"\n"
      "}}\n",
      presentation, system_id_string(report.destination),
      report.reference_planet.value, report.committed_tick,
      report.arrival_tick, report.arrival_checksum, report.width,
      report.height, report.framebuffer_checksum);
}

}  // namespace apsis_drift
