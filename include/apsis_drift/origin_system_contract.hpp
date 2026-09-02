#pragma once

#include <cstdint>
#include <expected>

#include "apsis_drift/intersystem_contract.hpp"
#include "apsis_drift/local_system.hpp"
#include "apsis_drift/origin_return.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kOriginSystemContractVersion{1};
inline constexpr std::uint32_t kOriginSystemContractTargetOrdinal{1};
inline constexpr std::uint32_t kOriginSystemContractObjectiveOrdinal{0};
inline constexpr std::uint64_t kOriginSystemContractMissionOrdinal{1};

struct OriginSystemContractBinding {
  Seed mission_seed;
  MissionId contract;
  SystemId system;
  OriginStationId station;
  PlanetId home_planet;
  PlanetId target_planet;
  SurfaceSignalId target_objective;
  std::uint32_t target_ordinal{};

  friend auto operator==(const OriginSystemContractBinding&,
                         const OriginSystemContractBinding&) -> bool = default;
};

enum class OriginSystemContractPhase : std::uint8_t {
  offered,
  accepted,
  station_departure,
  outbound_transfer,
  target_planet,
  objective_complete,
  return_transfer,
  station_rendezvous,
  returned,
  turned_in,
};

struct OriginSystemContractState {
  OriginSystemContractBinding binding;
  OriginSystemContractPhase phase{OriginSystemContractPhase::offered};

  friend auto operator==(const OriginSystemContractState&,
                         const OriginSystemContractState&) -> bool = default;
};

enum class OriginSystemContractCommand : std::uint8_t {
  accept,
  launch,
  begin_outbound_transfer,
  enter_target_planet,
  complete_objective,
  leave_target_planet,
  begin_station_rendezvous,
  dock,
  turn_in,
};

enum class OriginSystemContractError : std::uint8_t {
  invalid_system,
  invalid_binding,
  invalid_state,
  invalid_transition,
  wrong_command_tick,
  invalid_flight,
  target_unreachable,
};

[[nodiscard]] auto generate_origin_system_contract(Seed universe_seed)
    -> std::expected<OriginSystemContractBinding, OriginSystemContractError>;

[[nodiscard]] auto initial_origin_system_contract(Seed universe_seed)
    -> std::expected<OriginSystemContractState, OriginSystemContractError>;

[[nodiscard]] auto validate_origin_system_contract(
    Seed universe_seed, const OriginSystemContractState& state)
    -> std::expected<void, OriginSystemContractError>;

[[nodiscard]] auto advance_origin_system_contract(
    OriginSystemContractState& state, SimulationTick authoritative_tick,
    SimulationTick command_tick, OriginSystemContractCommand command)
    -> std::expected<void, OriginSystemContractError>;

// Converts the physical station-relative departure pose into origin-system
// inertial flight toward the one bound destination.
[[nodiscard]] auto initialize_origin_system_outbound_transfer(
    Seed universe_seed, SimulationTick authoritative_tick,
    const LocalSystemDescriptor& system,
    const OriginSystemContractState& contract,
    const OriginStationFlightState& departure)
    -> std::expected<SystemFlightState, OriginSystemContractError>;

// Preserves the physical planetary departure pose while selecting the fixed
// home-planet return target.
[[nodiscard]] auto initialize_origin_system_return_transfer(
    Seed universe_seed, const LocalSystemDescriptor& system,
    const OriginSystemContractState& contract,
    const SystemFlightState& departure)
    -> std::expected<SystemFlightState, OriginSystemContractError>;

// Requires a physical home-planet intercept before converting to the moving
// station-relative rendezvous frame.
[[nodiscard]] auto initialize_origin_system_station_rendezvous(
    Seed universe_seed, const LocalSystemDescriptor& system,
    const OriginSystemContractState& contract,
    const SystemFlightState& home_approach)
    -> std::expected<OriginStationFlightState, OriginSystemContractError>;

} // namespace apsis_drift
