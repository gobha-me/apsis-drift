#include "apsis_drift/mission_board.hpp"

#include <string_view>

#include "apsis_drift/local_system.hpp"

namespace apsis_drift {
namespace {

[[nodiscard]] auto mission_status(IntersystemMissionPhase phase)
    -> std::string_view {
  switch (phase) {
    case IntersystemMissionPhase::offered: return "OFFERED";
    case IntersystemMissionPhase::accepted: return "ACCEPTED";
    case IntersystemMissionPhase::active: return "ACTIVE";
    case IntersystemMissionPhase::objective_complete:
      return "OBJECTIVE COMPLETE";
    case IntersystemMissionPhase::returned: return "RETURNED";
    case IntersystemMissionPhase::turned_in: return "TURNED IN";
  }
  return "INVALID";
}

}  // namespace

auto mission_board_snapshot(const IntersystemContractState& state)
    -> std::expected<MissionBoardSnapshot, MissionBoardError> {
  if (!validate_intersystem_contract_state(state)) {
    return std::unexpected{MissionBoardError::invalid_contract};
  }
  const auto destination = generate_local_system(
      state.identities.target_system_seed);
  if (!validate_local_system(destination) || destination.planets.empty() ||
      destination.id != state.identities.target_system ||
      destination.planets.front().descriptor.id !=
          state.identities.target_planet) {
    return std::unexpected{MissionBoardError::invalid_contract};
  }

  MissionBoardSnapshot snapshot{
      .station = origin_station_id_string(state.identities.origin_station),
      .mission = mission_id_string(state.identities.mission),
      .destination_system = destination.star.display_name + " // " +
                            system_id_string(destination.id),
      .destination_planet = destination.planets.front().descriptor.display_name,
      .objective =
          "SIGNAL SURVEY // " +
          surface_signal_id_string(state.identities.target_objective),
      .return_destination =
          origin_station_id_string(state.identities.origin_station),
      .status = std::string{mission_status(state.mission_phase)},
      .primary_action = "CONTRACT IN FLIGHT",
      .primary_action_enabled = false,
      .launch_authorized =
          state.mission_phase == IntersystemMissionPhase::accepted,
  };
  if (state.mission_phase == IntersystemMissionPhase::offered) {
    snapshot.primary_action = "ACCEPT CONTRACT";
    snapshot.primary_action_enabled = true;
  } else if (state.mission_phase == IntersystemMissionPhase::accepted) {
    snapshot.primary_action = "MISSION ACCEPTED";
  } else if (state.mission_phase == IntersystemMissionPhase::returned) {
    snapshot.primary_action = "TURN IN CONTRACT";
    snapshot.primary_action_enabled = true;
  } else if (state.mission_phase == IntersystemMissionPhase::turned_in) {
    snapshot.primary_action = "CONTRACT COMPLETE";
  }
  return snapshot;
}

}  // namespace apsis_drift
