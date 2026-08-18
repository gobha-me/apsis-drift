#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#include "apsis_drift/origin_station.hpp"
#include "apsis_drift/planet.hpp"
#include "apsis_drift/simulation.hpp"
#include "apsis_drift/surface_signals.hpp"

namespace apsis_drift {

// This high-level contract is generated-world and save compatibility data.
// It deliberately proves only identities, authoritative phases, and legal
// transitions; later systems own ephemerides, flight, and presentation.
inline constexpr std::uint32_t kIntersystemContractVersion{1};
inline constexpr std::uint64_t kFirstTargetSystemOrdinal{1};
inline constexpr std::uint64_t kSystemStarOrdinal{0};
inline constexpr std::uint64_t kFirstMissionOrdinal{0};
inline constexpr std::uint64_t kFirstMissionTargetPlanetOrdinal{0};
inline constexpr std::uint64_t kFirstMissionObjectiveOrdinal{0};
inline constexpr SimulationTick kJumpSpoolTicks{3 * kSimulationHz};
inline constexpr SimulationTick kJumpTransitTicks{2 * kSimulationHz};

struct SystemId {
  std::uint64_t value{};

  friend auto operator==(const SystemId&, const SystemId&) -> bool = default;
};

struct StarId {
  std::uint64_t value{};

  friend auto operator==(const StarId&, const StarId&) -> bool = default;
};

struct MissionId {
  std::uint64_t value{};

  friend auto operator==(const MissionId&, const MissionId&) -> bool = default;
};

struct FirstIntersystemIdentities {
  Seed universe_seed;
  Seed origin_system_seed;
  SystemId origin_system;
  OriginStationId origin_station;
  Seed target_system_seed;
  SystemId target_system;
  Seed target_star_seed;
  StarId target_star;
  Seed target_planet_seed;
  PlanetId target_planet;
  Seed target_orbit_seed;
  Seed target_objective_seed;
  SurfaceSignalId target_objective;
  Seed mission_seed;
  MissionId mission;

  friend auto operator==(const FirstIntersystemIdentities&,
                         const FirstIntersystemIdentities&) -> bool = default;
};

[[nodiscard]] auto generate_first_intersystem_identities(
    Seed universe_seed) noexcept -> FirstIntersystemIdentities;

[[nodiscard]] auto system_id_string(SystemId id) -> std::string;
[[nodiscard]] auto star_id_string(StarId id) -> std::string;
[[nodiscard]] auto mission_id_string(MissionId id) -> std::string;

enum class IntersystemMissionPhase : std::uint8_t {
  offered,
  accepted,
  active,
  objective_complete,
  returned,
  turned_in,
};

enum class IntersystemTravelPhase : std::uint8_t {
  docked_at_origin,
  origin_system_flight,
  outbound_jump_spooling,
  outbound_jump_committed,
  target_system_flight,
  target_planet_flight,
  return_jump_spooling,
  return_jump_committed,
  origin_system_return,
};

struct IntersystemContractState {
  FirstIntersystemIdentities identities;
  SimulationTick universe_tick{};
  IntersystemMissionPhase mission_phase{IntersystemMissionPhase::offered};
  IntersystemTravelPhase travel_phase{IntersystemTravelPhase::docked_at_origin};
  SystemId current_system;
  std::optional<PlanetId> current_planet;
  std::optional<SystemId> committed_jump_destination;
  std::optional<SimulationTick> phase_started_tick;

  friend auto operator==(const IntersystemContractState&,
                         const IntersystemContractState&) -> bool = default;
};

enum class IntersystemContractCommand : std::uint8_t {
  accept_mission,
  launch,
  begin_outbound_jump,
  cancel_jump,
  commit_outbound_jump,
  arrive_target_system,
  enter_target_planet,
  complete_objective,
  leave_target_planet,
  begin_return_jump,
  commit_return_jump,
  arrive_origin_system,
  dock_at_origin,
  turn_in,
};

enum class IntersystemContractError : std::uint8_t {
  invalid_state,
  invalid_transition,
  wrong_command_tick,
  invalid_time_advance,
  tick_overflow,
};

[[nodiscard]] auto initial_intersystem_contract_state(
    Seed universe_seed) noexcept -> IntersystemContractState;

[[nodiscard]] auto validate_intersystem_contract_state(
    const IntersystemContractState& state) noexcept
    -> std::expected<void, IntersystemContractError>;

// Time advances only through application-owned fixed simulation steps. A
// failure leaves the state unchanged.
[[nodiscard]] auto advance_intersystem_time(IntersystemContractState& state,
                                            SimulationTick ticks) noexcept
    -> std::expected<void, IntersystemContractError>;

// Commands are addressed to the authoritative universe tick and commit
// atomically. Spooling may be canceled; a committed jump cannot be canceled.
[[nodiscard]] auto advance_intersystem_contract(
    IntersystemContractState& state, SimulationTick command_tick,
    IntersystemContractCommand command) noexcept
    -> std::expected<void, IntersystemContractError>;

}  // namespace apsis_drift
