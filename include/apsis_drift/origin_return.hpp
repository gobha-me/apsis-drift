#pragma once

#include <cstdint>
#include <expected>
#include <span>

#include "termforge/core/types.hpp"
#include "apsis_drift/intersystem_contract.hpp"
#include "apsis_drift/planetary_flight.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kOriginReturnVersion{1};
inline constexpr double kOriginStationSystemYMetres{-80'000'000'000.0};
inline constexpr double kOriginStationApproachOffsetMetres{40'000.0};
inline constexpr double kOriginStationArrivalRadiusMetres{5'000.0};
inline constexpr double kOriginReturnAcceleration{250.0};
inline constexpr double kOriginReturnMaximumSpeed{1'000.0};
inline constexpr double kOriginReturnTurnRateRadiansPerSecond{0.75};

struct OriginStationWaypoint {
  SystemId system;
  OriginStationId station;
  SystemPositionMetres position;
  SystemVelocityMetresPerSecond velocity;

  friend auto operator==(const OriginStationWaypoint&,
                         const OriginStationWaypoint&) -> bool = default;
};

enum class OriginReturnCue : std::uint8_t {
  hold,
  closing,
  opening,
  brake,
  arrived,
};

struct OriginReturnState {
  SimulationTick tick{};
  SystemId system;
  OriginStationId station;
  SystemPositionMetres position;
  SystemVelocityMetresPerSecond velocity;
  SystemDirection forward{1.0, 0.0, 0.0};
  SystemDirection up{0.0, 0.0, 1.0};
  FlightMode mode{FlightMode::autopilot};
  FlightControls controls;

  friend auto operator==(const OriginReturnState&, const OriginReturnState&)
      -> bool = default;
};

struct OriginReturnGuidance {
  double distance_metres{};
  double closing_speed_metres_per_second{};
  double relative_speed_metres_per_second{};
  double stopping_distance_metres{};
  OriginReturnCue cue{OriginReturnCue::hold};
  bool arrived{};

  friend auto operator==(const OriginReturnGuidance&,
                         const OriginReturnGuidance&) -> bool = default;
};

enum class OriginReturnError : std::uint8_t {
  invalid_contract,
  invalid_state,
  invalid_waypoint,
  invalid_arrival,
  invalid_command,
  wrong_command_tick,
  tick_overflow,
  invalid_framebuffer,
};

[[nodiscard]] auto generate_origin_station_waypoint(
    const FirstIntersystemIdentities& identities) noexcept
    -> OriginStationWaypoint;

[[nodiscard]] auto initialize_origin_return(
    const IntersystemContractState& contract)
    -> std::expected<OriginReturnState, OriginReturnError>;

[[nodiscard]] auto validate_origin_return_state(
    const IntersystemContractState& contract,
    const OriginReturnState& state) noexcept
    -> std::expected<void, OriginReturnError>;

[[nodiscard]] auto resolve_origin_return_guidance(
    const IntersystemContractState& contract,
    const OriginReturnState& state) noexcept
    -> std::expected<OriginReturnGuidance, OriginReturnError>;

[[nodiscard]] auto advance_origin_return(
    const IntersystemContractState& contract, OriginReturnState& state,
    std::span<const FlightCommand> commands) noexcept
    -> std::expected<void, OriginReturnError>;

[[nodiscard]] auto origin_return_state_checksum(
    const OriginReturnState& state) noexcept -> std::uint64_t;

[[nodiscard]] auto render_origin_station_marker(
    int width, int height, std::span<termforge::Pixel> destination) noexcept
    -> std::expected<void, OriginReturnError>;

}  // namespace apsis_drift
