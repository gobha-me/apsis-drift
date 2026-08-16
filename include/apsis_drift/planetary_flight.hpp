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

[[nodiscard]] auto flight_regime_bands(
    const PlanetDescriptor& planet) noexcept
    -> std::expected<FlightRegimeBands, PlanetaryFlightError>;

[[nodiscard]] auto initial_planetary_flight_state(
    const PlanetDescriptor& planet, GeodeticPosition position,
    PlanetaryFlightEnvironment environment, double heading_radians = 0.0,
    FlightMode mode = FlightMode::autopilot) noexcept
    -> std::expected<PlanetaryFlightState, PlanetaryFlightError>;

// Applies every command in recorded order and advances one application-owned
// fixed step. The caller supplies the deterministic surface elevation for the
// craft's current subpoint; rendering cadence and terrain cache state are not
// inputs. Rejected steps leave state untouched.
[[nodiscard]] auto advance_planetary_flight(
    const PlanetDescriptor& planet, PlanetaryFlightEnvironment environment,
    PlanetaryFlightState& state,
    std::span<const FlightCommand> commands, SimulationSeconds step) noexcept
    -> std::expected<void, PlanetaryFlightError>;

[[nodiscard]] auto planetary_flight_state_checksum(
    const PlanetaryFlightState& state) noexcept -> std::uint64_t;

}  // namespace apsis_drift
