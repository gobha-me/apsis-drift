#pragma once

#include <expected>
#include <string>

#include "apsis_drift/intersystem_contract.hpp"

namespace apsis_drift {

enum class MissionBoardError : std::uint8_t {
  invalid_contract,
};

// Presentation-independent values for the single bounded first contract.
// Kitty and ANSI presenters consume the same snapshot and input remains an
// application-owned mission command rather than a terminal protocol detail.
struct MissionBoardSnapshot {
  std::string station;
  std::string mission;
  std::string destination_system;
  std::string destination_planet;
  std::string objective;
  std::string return_destination;
  std::string status;
  std::string rule_profile;
  std::string rule_profile_description;
  std::string primary_action;
  bool primary_action_enabled{};
  bool launch_authorized{};
  bool rule_profile_selection_enabled{};

  friend auto operator==(const MissionBoardSnapshot&,
                         const MissionBoardSnapshot&) -> bool = default;
};

[[nodiscard]] auto mission_board_snapshot(
    const IntersystemContractState& state)
    -> std::expected<MissionBoardSnapshot, MissionBoardError>;

}  // namespace apsis_drift
