#pragma once

#include <cstdint>
#include <expected>
#include <span>

#include "apsis_drift/intersystem_contract.hpp"
#include "apsis_drift/local_system.hpp"
#include "apsis_drift/planetary_flight.hpp"
#include "apsis_drift/system_flight.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kOriginStationFlightVersion{4};
inline constexpr double kOriginStationArrivalRadiusMetres{5'000.0};
inline constexpr double kOriginStationDockingSpeedMetresPerSecond{25.0};
inline constexpr double kOriginStationLaunchStandoffMetres{5'000.0};
inline constexpr double kOriginStationFlightAcceleration{250.0};
inline constexpr double kOriginStationFlightMaximumSpeed{1'000.0};
inline constexpr double kHomeSignalStationFlightAcceleration{5'000.0};
inline constexpr double kHomeSignalStationFlightMaximumSpeed{100'000.0};
inline constexpr double kOriginStationFlightTurnRateRadiansPerSecond{0.75};

struct OriginStationWaypoint {
  SystemId system;
  OriginStationId station;
  SystemPositionMetres position;
  SystemVelocityMetresPerSecond velocity;

  friend auto operator==(const OriginStationWaypoint&,
                         const OriginStationWaypoint&) -> bool = default;
};

enum class OriginStationFlightCue : std::uint8_t {
  hold,
  closing,
  opening,
  brake,
  arrived,
};

struct OriginStationFlightState {
  SimulationTick tick{};
  SystemId system;
  OriginStationId station;
  SystemPositionMetres relative_position;
  SystemVelocityMetresPerSecond relative_velocity;
  SystemDirection forward{1.0, 0.0, 0.0};
  SystemDirection up{0.0, 0.0, 1.0};
  FlightMode mode{FlightMode::autopilot};
  FlightControls controls;

  friend auto operator==(const OriginStationFlightState&,
                         const OriginStationFlightState&) -> bool = default;
};

struct OriginStationFlightGuidance {
  double distance_metres{};
  double closing_speed_metres_per_second{};
  double relative_speed_metres_per_second{};
  double stopping_distance_metres{};
  OriginStationFlightCue cue{OriginStationFlightCue::hold};
  bool within_rendezvous{};
  bool arrived{};

  friend auto operator==(const OriginStationFlightGuidance&,
                         const OriginStationFlightGuidance&) -> bool = default;
};

struct OriginStationFlightPose {
  SystemPositionMetres position;
  SystemVelocityMetresPerSecond velocity;

  friend auto operator==(const OriginStationFlightPose&,
                         const OriginStationFlightPose&) -> bool = default;
};

enum class OriginStationFlightError : std::uint8_t {
  invalid_contract,
  invalid_state,
  invalid_waypoint,
  invalid_arrival,
  invalid_command,
  wrong_command_tick,
  tick_overflow,
};

[[nodiscard]] auto resolve_origin_station_waypoint(
    const FirstIntersystemIdentities& identities,
    const LocalSystemDescriptor& origin_system, EphemerisQueryTime time)
    -> std::expected<OriginStationWaypoint, OriginStationFlightError>;

[[nodiscard]] auto resolve_origin_station_waypoint(
    Seed universe_seed, const LocalSystemDescriptor& origin_system,
    EphemerisQueryTime time)
    -> std::expected<OriginStationWaypoint, OriginStationFlightError>;

[[nodiscard]] auto initialize_origin_station_launch(
    Seed universe_seed, SimulationTick tick,
    const LocalSystemDescriptor& origin_system)
    -> std::expected<OriginStationFlightState, OriginStationFlightError>;

[[nodiscard]] auto initialize_origin_station_approach(
    Seed universe_seed, const LocalSystemDescriptor& origin_system,
    const SystemFlightState& departure)
    -> std::expected<OriginStationFlightState, OriginStationFlightError>;

[[nodiscard]] auto validate_origin_station_flight_state(
    Seed universe_seed, SimulationTick authoritative_tick,
    const LocalSystemDescriptor& origin_system,
    const OriginStationFlightState& state)
    -> std::expected<void, OriginStationFlightError>;

[[nodiscard]] auto resolve_origin_station_flight_guidance(
    Seed universe_seed, SimulationTick authoritative_tick,
    const LocalSystemDescriptor& origin_system,
    const OriginStationFlightState& state)
    -> std::expected<OriginStationFlightGuidance, OriginStationFlightError>;

[[nodiscard]] auto resolve_origin_station_flight_pose(
    Seed universe_seed, SimulationTick authoritative_tick,
    const LocalSystemDescriptor& origin_system,
    const OriginStationFlightState& state)
    -> std::expected<OriginStationFlightPose, OriginStationFlightError>;

[[nodiscard]] auto advance_origin_station_flight(
    Seed universe_seed, SimulationTick authoritative_tick,
    const LocalSystemDescriptor& origin_system, OriginStationFlightState& state,
    std::span<const FlightCommand> commands)
    -> std::expected<void, OriginStationFlightError>;

[[nodiscard]] auto initialize_origin_station_launch(
    const IntersystemContractState& contract,
    const LocalSystemDescriptor& origin_system)
    -> std::expected<OriginStationFlightState, OriginStationFlightError>;

[[nodiscard]] auto initialize_origin_return(
    const IntersystemContractState& contract,
    const LocalSystemDescriptor& origin_system)
    -> std::expected<OriginStationFlightState, OriginStationFlightError>;

[[nodiscard]] auto validate_origin_station_flight_state(
    const IntersystemContractState& contract,
    const LocalSystemDescriptor& origin_system,
    const OriginStationFlightState& state)
    -> std::expected<void, OriginStationFlightError>;

[[nodiscard]] auto resolve_origin_station_flight_guidance(
    const IntersystemContractState& contract,
    const LocalSystemDescriptor& origin_system,
    const OriginStationFlightState& state)
    -> std::expected<OriginStationFlightGuidance, OriginStationFlightError>;

[[nodiscard]] auto resolve_origin_station_flight_pose(
    const IntersystemContractState& contract,
    const LocalSystemDescriptor& origin_system,
    const OriginStationFlightState& state)
    -> std::expected<OriginStationFlightPose, OriginStationFlightError>;

[[nodiscard]] auto advance_origin_station_flight(
    const IntersystemContractState& contract,
    const LocalSystemDescriptor& origin_system, OriginStationFlightState& state,
    std::span<const FlightCommand> commands)
    -> std::expected<void, OriginStationFlightError>;

[[nodiscard]] auto attempt_origin_docking(
    IntersystemContractState& contract,
    const LocalSystemDescriptor& origin_system,
    const OriginStationFlightState& state)
    -> std::expected<void, OriginStationFlightError>;

[[nodiscard]] auto origin_station_flight_state_checksum(
    const OriginStationFlightState& state) noexcept -> std::uint64_t;

}  // namespace apsis_drift
