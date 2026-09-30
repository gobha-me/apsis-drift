#pragma once

#include "apsis_drift/vacuum_dynamics.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kCentralBodyDynamicsVersion{1};
inline constexpr double kCentralBodyMinimumRadiusMetres{1.0};

// Persist this explicit selection and the runtime assistance choice with any
// future flight session. A standalone rigid-state projection stores neither.
struct CentralBodyDynamicsRecipe {
  std::uint32_t version{kCentralBodyDynamicsVersion};
  friend auto operator==(const CentralBodyDynamicsRecipe&,
                         const CentralBodyDynamicsRecipe&) -> bool = default;
};

enum class CentralBodyErrorCode : std::uint8_t {
  unsupported_version,
  invalid_owner,
  invalid_body,
  invalid_state,
  unsupported_coordinate_frame,
  invalid_radius,
  unsafe_arithmetic,
  dynamics_failure,
};
struct CentralBodyError {
  CentralBodyErrorCode code;
  std::optional<VacuumDynamicsError> dynamics;
};

struct CentralBodyGravity {
  PlanetId planet;
  double reference_radius_metres{}, surface_gravity_metres_per_second_squared{};
  double gravitational_parameter_metres_cubed_per_second_squared{};
  double distance_metres{};
  RigidVector3 acceleration_metres_per_second_squared;
};

struct CentralBodyActuation {
  // These are propulsion channels/impulses ONLY, expressed in the named body's
  // nonrotating owning frame. Gravity never occupies an actuator channel.
  VacuumActuation propulsion;
  CentralBodyGravity initial_gravity;
  RigidVector3 gravity_impulse_newton_seconds;
  RigidVector3 total_linear_impulse_newton_seconds;
};

// Physical catalog, named planet-relative nonrotating state only. Derives
// mu = generated surface gravity * generated radius squared. No caller-supplied
// mass/gravity or silently substituted body. Pure query, never mutates state.
[[nodiscard]] auto evaluate_central_body_gravity(
    const RigidBodyWorldContext& context, const RigidBodyState& state,
    CentralBodyDynamicsRecipe recipe = {})
    -> std::expected<CentralBodyGravity, CentralBodyError>;

// One atomic 120 Hz tick through the existing coupled thrust/torque RK4 kernel.
// Gravity is reevaluated at every stage, with rotation-only assistance. The
// bounded local model neglects external/tidal acceleration; it does not claim
// that the moving body origin is an exact force-free system-inertial frame.
// No automatic orbital hold, atmosphere, contact or gameplay save adoption.
[[nodiscard]] auto advance_central_body_dynamics(
    const RigidBodyWorldContext& context, RigidBodyState& state,
    const VacuumIntent& intent, CentralBodyDynamicsRecipe recipe = {},
    SimulationSeconds step = kSimulationStep)
    -> std::expected<CentralBodyActuation, CentralBodyError>;

} // namespace apsis_drift
