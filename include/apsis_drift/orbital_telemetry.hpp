#pragma once

#include "apsis_drift/central_body_dynamics.hpp"
#include "apsis_drift/planet_rotation.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kOrbitalTelemetryVersion{1};
inline constexpr double kMaximumOrbitalElementRadiusMetres{2.0e15};
inline constexpr double kSynchronousOrbitRelativeTolerance{1.0e-6};

// Explicit classification policy, not an atmosphere/density generator. A later
// atmosphere owner supplies the selected space boundary; persist that recipe.
struct OrbitalTelemetryRecipe {
  std::uint32_t version{kOrbitalTelemetryVersion};
  double space_boundary_altitude_metres{};
};
enum class OrbitClassification : std::uint8_t {
  stable,
  decaying,
  impact,
  escape,
};
struct OrbitalTelemetry {
  PlanetId planet;
  SimulationTick tick{};
  double inertial_speed_metres_per_second{};
  RigidVector3 surface_relative_velocity_metres_per_second;
  double surface_relative_speed_metres_per_second{},
      radial_rate_metres_per_second{};
  double specific_energy_metres_squared_per_second_squared{};
  RigidVector3 specific_angular_momentum_metres_squared_per_second;
  double eccentricity{}, periapsis_radius_metres{};
  // Missing for unbound/marginal trajectories, or when a bound apoapsis exceeds
  // the declared element radius bound. Never a fabricated clamp or infinity.
  std::optional<double> apoapsis_radius_metres;
  double energy_tolerance_metres_squared_per_second_squared{};
  bool bound{}, near_parabolic{};
  OrbitClassification classification{OrbitClassification::impact};
  double synchronous_radius_metres{};
  // Strict surface-stationary equatorial/prograde/circular qualification,
  // not a zero-radial-rate/zero-surface-speed shortcut or orbit-hold state.
  bool synchronous_orbit{};
  friend auto operator==(const OrbitalTelemetry&, const OrbitalTelemetry&)
      -> bool = default;
};
enum class OrbitalTelemetryErrorCode : std::uint8_t {
  unsupported_version,
  invalid_policy,
  gravity_failure,
  rotation_failure,
  unsafe_arithmetic,
};
struct OrbitalTelemetryError {
  OrbitalTelemetryErrorCode code;
  std::optional<CentralBodyError> gravity;
  std::optional<PlanetRotationError> rotation;
};

// Pure same-tick observation. The selected gravity provider supplies GM; the
// explicitly owned physical rotation supplies Omega. State is never replaced,
// normalized or corrected. No atmosphere force or automatic orbit hold.
[[nodiscard]] auto evaluate_orbital_telemetry(
    const RigidBodyWorldContext& context, const RigidBodyState& state,
    const PhysicalPlanetRotationRecipe& rotation, OrbitalTelemetryRecipe policy,
    CentralBodyDynamicsRecipe dynamics = {})
    -> std::expected<OrbitalTelemetry, OrbitalTelemetryError>;
} // namespace apsis_drift
