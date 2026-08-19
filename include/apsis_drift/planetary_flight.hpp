#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

#include "apsis_drift/coordinates.hpp"
#include "apsis_drift/planet.hpp"
#include "apsis_drift/simulation.hpp"

namespace apsis_drift {

enum class FlightRegime : std::uint8_t {
  orbital,
  atmospheric,
  terrain_flight,
};

inline constexpr double kTerrainFlightEnterClearanceMetres{2'000.0};
inline constexpr double kTerrainFlightExitClearanceMetres{2'500.0};
inline constexpr double kMinimumFlightClearanceMetres{16.0};
inline constexpr std::uint32_t kMaximumThermalLoadUnits{1'000'000U};

struct PlanetaryThermalState {
  // Fixed-point fraction of the thermal limit. One million is 100%.
  std::uint32_t load_units{};
  bool abort_latched{};

  friend auto operator==(const PlanetaryThermalState&,
                         const PlanetaryThermalState&) -> bool = default;
};

struct PlanetaryFlightRules {
  bool enforce_thermal_abort{};

  friend auto operator==(const PlanetaryFlightRules&,
                         const PlanetaryFlightRules&) -> bool = default;
};

enum class ThermalTrend : std::uint8_t {
  cooling,
  steady,
  heating,
};

enum class ThermalCue : std::uint8_t {
  nominal,
  slow_and_rise,
  cooling,
  abort_climb,
};

struct ThermalAssessment {
  unsigned load_percent{};
  double flight_path_angle_degrees{};
  double load_change_per_second{};
  ThermalTrend trend{ThermalTrend::steady};
  ThermalCue cue{ThermalCue::nominal};
  bool at_limit{};

  friend auto operator==(const ThermalAssessment&,
                         const ThermalAssessment&) -> bool = default;
};

struct FlightRegimeBands {
  double terrain_enter_clearance_metres{};
  double terrain_exit_clearance_metres{};
  double atmosphere_enter_altitude_metres{};
  double orbit_enter_altitude_metres{};

  friend auto operator==(const FlightRegimeBands&,
                         const FlightRegimeBands&) -> bool = default;
};

struct PlanetaryFlightPose {
  GeodeticPosition position;
  // Heading is measured in the local east/north tangent plane. Zero points
  // east and positive angles turn toward north.
  double heading_radians{};

  friend auto operator==(const PlanetaryFlightPose&,
                         const PlanetaryFlightPose&) -> bool = default;
};

struct PlanetaryFlightVelocity {
  double east_metres_per_second{};
  double north_metres_per_second{};
  double up_metres_per_second{};

  friend auto operator==(const PlanetaryFlightVelocity&,
                         const PlanetaryFlightVelocity&) -> bool = default;
};

struct FlightPerformance {
  double maximum_horizontal_speed{};
  double maximum_vertical_speed{};
  double horizontal_acceleration{};
  double vertical_acceleration{};
  double turn_rate_radians_per_second{};

  friend auto operator==(const FlightPerformance&,
                         const FlightPerformance&) -> bool = default;
};

enum class FlightDriveState : std::uint8_t {
  idle,
  coast,
  forward,
  reverse,
  maneuvering,
  braking,
};

enum class TargetMotionCue : std::uint8_t {
  holding,
  closing,
  opening,
  brake,
};

struct TargetRelativeMotion {
  // Positive values close the range; negative values open it.
  double closing_speed_metres_per_second{};
  std::optional<double> arrival_estimate_seconds;
  double stopping_distance_metres{};
  TargetMotionCue cue{TargetMotionCue::holding};

  friend auto operator==(const TargetRelativeMotion&,
                         const TargetRelativeMotion&) -> bool = default;
};

struct FlightRegimeTransition {
  FlightRegime from{};
  FlightRegime to{};
  // The transition is visible on the state produced at this tick.
  SimulationTick tick{};

  friend auto operator==(const FlightRegimeTransition&,
                         const FlightRegimeTransition&) -> bool = default;
};

struct PlanetaryFlightEnvironment {
  // Generated terrain supplies the reference-surface-relative elevation at
  // the craft's current subpoint. The simulation does not own tile caching.
  double surface_elevation_metres{};
};

struct PlanetaryFlightState {
  SimulationTick tick{};
  PlanetId planet;
  PlanetaryFlightPose pose;
  PlanetaryFlightVelocity velocity;
  double clearance_metres{};
  FlightMode mode{FlightMode::autopilot};
  FlightControls controls;
  FlightRegime regime{FlightRegime::orbital};
  std::optional<FlightRegimeTransition> last_transition;
  PlanetaryThermalState thermal;

  friend auto operator==(const PlanetaryFlightState&,
                         const PlanetaryFlightState&) -> bool = default;
};

enum class PlanetaryFlightError : std::uint8_t {
  invalid_planet,
  invalid_state,
  invalid_environment,
  invalid_step,
  invalid_command,
  wrong_command_tick,
  tick_overflow,
  coordinate_failure,
};

[[nodiscard]] auto flight_regime_name(FlightRegime regime) noexcept
    -> std::string_view;
[[nodiscard]] auto planetary_flight_error_name(
    PlanetaryFlightError error) noexcept -> std::string_view;
[[nodiscard]] auto thermal_trend_name(ThermalTrend trend) noexcept
    -> std::string_view;
[[nodiscard]] auto thermal_cue_name(ThermalCue cue) noexcept
    -> std::string_view;

[[nodiscard]] auto flight_regime_bands(
    const PlanetDescriptor& planet) noexcept
    -> std::expected<FlightRegimeBands, PlanetaryFlightError>;

[[nodiscard]] auto flight_performance(const PlanetDescriptor& planet,
                                      FlightRegime regime) noexcept
    -> std::expected<FlightPerformance, PlanetaryFlightError>;

// Drive state and target motion are derived cockpit/navigation semantics.
// They never enter authoritative saves and cannot affect simulation state.
[[nodiscard]] auto flight_drive_state(
    const PlanetaryFlightState& state) noexcept
    -> std::expected<FlightDriveState, PlanetaryFlightError>;
[[nodiscard]] auto resolve_target_relative_motion(
    const PlanetDescriptor& planet, const PlanetaryFlightState& state,
    LocalPositionMetres target, double arrival_radius_metres) noexcept
    -> std::expected<TargetRelativeMotion, PlanetaryFlightError>;
[[nodiscard]] auto resolve_thermal_assessment(
    const PlanetDescriptor& planet,
    const PlanetaryFlightState& state) noexcept
    -> std::expected<ThermalAssessment, PlanetaryFlightError>;

[[nodiscard]] auto initial_planetary_flight_state(
    const PlanetDescriptor& planet, GeodeticPosition position,
    PlanetaryFlightEnvironment environment, double heading_radians = 0.0,
    FlightMode mode = FlightMode::autopilot) noexcept
    -> std::expected<PlanetaryFlightState, PlanetaryFlightError>;

// Validates the complete authoritative state independently of terrain cache
// and presentation state. Persistence uses this before committing a load.
[[nodiscard]] auto validate_planetary_flight_state(
    const PlanetDescriptor& planet, const PlanetaryFlightState& state) noexcept
    -> std::expected<void, PlanetaryFlightError>;

// Applies every command in recorded order and advances one application-owned
// fixed step. The caller supplies kSimulationStep and the deterministic surface
// elevation for the craft's current subpoint; rendering cadence and terrain
// cache state are not inputs. Rejected steps leave state untouched.
[[nodiscard]] auto advance_planetary_flight(
    const PlanetDescriptor& planet, PlanetaryFlightEnvironment environment,
    PlanetaryFlightState& state,
    std::span<const FlightCommand> commands, SimulationSeconds step,
    PlanetaryFlightRules rules = {}) noexcept
    -> std::expected<void, PlanetaryFlightError>;

[[nodiscard]] auto planetary_flight_state_checksum(
    const PlanetaryFlightState& state) noexcept -> std::uint64_t;

}  // namespace apsis_drift
