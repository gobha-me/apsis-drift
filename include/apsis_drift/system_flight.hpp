#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>

#include "apsis_drift/coordinates.hpp"
#include "apsis_drift/intersystem_contract.hpp"
#include "apsis_drift/local_system.hpp"
#include "apsis_drift/planetary_flight.hpp"
#include "apsis_drift/simulation.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kSystemFlightVersion{1};
inline constexpr double kSystemFlightMaximumRelativeSpeed{1'000'000.0};
inline constexpr double kSystemFlightForwardAcceleration{50'000.0};
inline constexpr double kSystemFlightManeuverAcceleration{25'000.0};
inline constexpr double kSystemFlightTurnRateRadiansPerSecond{0.75};
inline constexpr double kSystemApproachRadiusRadii{6.0};
inline constexpr double kSystemOrbitInsertionRadiusRadii{3.0};
inline constexpr double kSystemOrbitInsertionMaximumSpeed{4'000.0};
inline constexpr double kSystemOrbitInsertionMaximumRadialSpeed{250.0};
inline constexpr SimulationTick kSystemPlanetFrameRotationTicks{
    24ULL * 60ULL * 60ULL * kSimulationHz};

enum class SystemTimeScale : std::uint8_t { one = 1, four = 4, sixteen = 16 };

enum class SystemFlightCue : std::uint8_t {
  hold,
  closing,
  opening,
  brake,
  orbit_ready,
};

struct SystemFlightState {
  SimulationTick tick{};
  SystemId system;
  PlanetId target;
  SystemPositionMetres position;
  SystemVelocityMetresPerSecond velocity;
  SystemDirection forward{0.0, 1.0, 0.0};
  SystemDirection up{0.0, 0.0, 1.0};
  FlightMode mode{FlightMode::autopilot};
  FlightControls controls;
  SystemTimeScale time_scale{SystemTimeScale::one};

  friend auto operator==(const SystemFlightState&, const SystemFlightState&)
      -> bool = default;
};

struct SystemFlightGuidance {
  PlanetId target;
  double distance_metres{};
  double closing_speed_metres_per_second{};
  double relative_speed_metres_per_second{};
  std::optional<double> arrival_estimate_seconds;
  double stopping_distance_metres{};
  SystemFlightCue cue{SystemFlightCue::hold};
  bool inside_approach_boundary{};
  bool orbit_insertion_ready{};

  friend auto operator==(const SystemFlightGuidance&,
                         const SystemFlightGuidance&) -> bool = default;
};

enum class SystemFlightError : std::uint8_t {
  invalid_system,
  invalid_state,
  invalid_arrival,
  invalid_step,
  invalid_command,
  wrong_command_tick,
  unknown_target,
  ephemeris_failure,
  tick_overflow,
  coordinate_failure,
  orbit_insertion_refused,
  planet_departure_refused,
};

[[nodiscard]] auto initial_system_flight_state(
    const LocalSystemDescriptor& system, PlanetId target,
    const IntersystemArrivalSolution& arrival)
    -> std::expected<SystemFlightState, SystemFlightError>;

[[nodiscard]] auto validate_system_flight_state(
    const LocalSystemDescriptor& system,
    const SystemFlightState& state) noexcept
    -> std::expected<void, SystemFlightError>;

[[nodiscard]] auto resolve_system_flight_guidance(
    const LocalSystemDescriptor& system, const SystemFlightState& state)
    -> std::expected<SystemFlightGuidance, SystemFlightError>;

// Advances one host simulation step. Time compression is implemented as
// bounded authoritative 120 Hz substeps; no large integration step is used.
[[nodiscard]] auto advance_system_flight(
    const LocalSystemDescriptor& system, SystemFlightState& state,
    std::span<const FlightCommand> commands)
    -> std::expected<void, SystemFlightError>;

[[nodiscard]] auto insert_system_flight_orbit(
    const LocalSystemDescriptor& system, const SystemFlightState& state)
    -> std::expected<PlanetaryFlightState, SystemFlightError>;

// Converts an orbital planet-fixed craft state back into system-inertial
// flight without changing the authoritative tick or mission target.
[[nodiscard]] auto depart_planetary_orbit(
    const LocalSystemDescriptor& system, const PlanetaryFlightState& state)
    -> std::expected<SystemFlightState, SystemFlightError>;

[[nodiscard]] auto system_flight_state_checksum(
    const SystemFlightState& state) noexcept -> std::uint64_t;

[[nodiscard]] constexpr auto system_time_scale_value(
    SystemTimeScale scale) noexcept -> SimulationTick {
  return static_cast<SimulationTick>(scale);
}

}  // namespace apsis_drift
