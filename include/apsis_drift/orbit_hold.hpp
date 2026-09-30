#pragma once

#include "apsis_drift/orbital_telemetry.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kOrbitHoldVersion{1};
inline constexpr double kOrbitHoldPositionGainPerSecondSquared{0.0004};
inline constexpr double kOrbitHoldVelocityGainPerSecond{0.04};
inline constexpr double kOrbitHoldMinimumPlaneProjectionFraction{1.0e-6};

struct OrbitHoldTarget {
  PlanetId planet;
  double radius_metres{};
  // Unit normal in the named body's nonrotating frame. Its sign selects the
  // orbit direction; no inferred plane, quaternion alias or camera input.
  RigidVector3 plane_normal{0, 0, 1};
  friend auto operator==(const OrbitHoldTarget&, const OrbitHoldTarget&)
      -> bool = default;
};
struct OrbitHoldRequest {
  std::uint32_t version{kOrbitHoldVersion};
  // Empty means disabled. A future session retains target, recipe/policy and
  // runtime assistance explicitly; no hidden controller history exists here.
  std::optional<OrbitHoldTarget> target;
  friend auto operator==(const OrbitHoldRequest&, const OrbitHoldRequest&)
      -> bool = default;
};
enum class OrbitHoldStatus : std::uint8_t {
  disabled,
  paused_advanced,
  paused_manual_translation,
  unavailable_environment,
  unavailable_geometry,
  active,
  saturated,
};
enum class OrbitHoldErrorCode : std::uint8_t {
  unsupported_version,
  invalid_target,
  telemetry_failure,
  dynamics_failure,
  unsafe_correction,
};
struct OrbitHoldError {
  OrbitHoldErrorCode code;
  std::optional<OrbitalTelemetryError> telemetry;
  std::optional<CentralBodyError> dynamics;
};
struct OrbitHoldResult {
  OrbitHoldStatus status{OrbitHoldStatus::disabled};
  RigidVector3 requested_correction_metres_per_second_squared;
  RigidVector3 requested_force_body_newtons, applied_force_body_newtons;
  // Actual gross channels/impulses include the hold correction once. The
  // central-body report still separates propulsion and environmental gravity.
  CentralBodyActuation actuation;
  OrbitalTelemetry observation_after;
  bool orbit_established{};
};

struct OrbitHoldCorrection {
  OrbitHoldStatus status{OrbitHoldStatus::disabled};
  VacuumIntent commands;
  RigidVector3 requested_correction_metres_per_second_squared;
  RigidVector3 requested_force_body_newtons;
};
// Pure allocation plan for composition into the shared atmospheric kernel.
// It neither advances state nor chooses a different integrator at an air edge.
[[nodiscard]] auto evaluate_orbit_hold_correction(
    const RigidBodyWorldContext&, const RigidBodyState&, const VacuumIntent&,
    const PhysicalPlanetRotationRecipe&, OrbitalTelemetryRecipe,
    OrbitHoldRequest, CentralBodyDynamicsRecipe = {})
    -> std::expected<OrbitHoldCorrection, OrbitHoldError>;

// Pure qualification for persisted targets; does not advance a candidate just
// to validate its selection or invent a hold target from the flight state.
[[nodiscard]] auto validate_orbit_hold_request(
    const RigidBodyWorldContext&, const RigidBodyState&,
    const PhysicalPlanetRotationRecipe&, OrbitalTelemetryRecipe,
    OrbitHoldRequest, CentralBodyDynamicsRecipe = {})
    -> std::expected<void, OrbitHoldError>;

// Explicit optional hold through actual capped directional actuators and the
// existing central-body RK4 kernel. Advanced/manual translation pause hold;
// valid unavailable situations still advance under gravity. All refused steps
// leave source state untouched, including failed post-step observation.
[[nodiscard]] auto advance_orbit_hold_dynamics(
    const RigidBodyWorldContext& context, RigidBodyState& state,
    const VacuumIntent& intent, const PhysicalPlanetRotationRecipe& rotation,
    OrbitalTelemetryRecipe policy, OrbitHoldRequest request = {},
    CentralBodyDynamicsRecipe dynamics = {},
    SimulationSeconds step = kSimulationStep)
    -> std::expected<OrbitHoldResult, OrbitHoldError>;
} // namespace apsis_drift
