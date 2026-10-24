#pragma once

#include "apsis_drift/intersystem_contract.hpp"
#include "apsis_drift/simulation.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace apsis_drift {
struct NativeReturnChargeLeg {
  SystemId source, destination;
  std::string source_name, destination_name;
  friend auto operator==(const NativeReturnChargeLeg&,
                         const NativeReturnChargeLeg&) -> bool = default;
};
// A read-only budget for named jumps, not future reach, arrival or maneuver
// fuel.
struct NativeReturnChargeView {
  std::uint32_t version{1};
  bool available{};
  SimulationTick tick{};
  std::uint32_t jump_charges{}, required_charges{}, charge_shortfall{},
      leg_count{};
  double flight_fraction{};
  bool reserve{}, operationally_empty{};
  std::array<NativeReturnChargeLeg, 2> legs{};
  std::string note;
  friend auto operator==(const NativeReturnChargeView&,
                         const NativeReturnChargeView&) -> bool = default;
};
} // namespace apsis_drift
