#pragma once

#include "apsis_drift/central_body_dynamics.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kMoonBodyDynamicsVersion{1};

struct MoonBodyDynamicsRecipe {
  std::uint32_t version{kMoonBodyDynamicsVersion};
  friend auto operator==(const MoonBodyDynamicsRecipe&,
                         const MoonBodyDynamicsRecipe&) -> bool = default;
};

// Share the existing bounded central-body refusal vocabulary, not a force
// registry. Moon reports retain BodyId and never manufacture a PlanetId.
using MoonBodyError = CentralBodyError;
struct MoonBodyGravity {
  BodyId body;
  double reference_radius_metres{}, surface_gravity_metres_per_second_squared{};
  double gravitational_parameter_metres_cubed_per_second_squared{};
  double distance_metres{};
  RigidVector3 acceleration_metres_per_second_squared;
  friend auto operator==(const MoonBodyGravity&, const MoonBodyGravity&)
      -> bool = default;
};
struct MoonBodyActuation {
  VacuumActuation propulsion;
  MoonBodyGravity initial_gravity;
  RigidVector3 gravity_impulse_newton_seconds;
  RigidVector3 total_linear_impulse_newton_seconds;
};

// Requires the exact authenticated PhysicalMoonDescriptor in context and its
// body_relative_inertial frame. No atmosphere, parent/tidal/N-body force,
// surface support, automatic hold or gameplay save adoption is implied.
[[nodiscard]] auto evaluate_moon_body_gravity(
    const RigidBodyWorldContext&, const RigidBodyState&,
    MoonBodyDynamicsRecipe recipe = {})
    -> std::expected<MoonBodyGravity, MoonBodyError>;

// One atomic120Hz tick through the SAME existing coupled RK4/torque kernel.
// Gravity is reevaluated at every stage. Assistance stabilizes rotation only;
// gravity never occupies gross propulsion channels or fuel impulses.
[[nodiscard]] auto advance_moon_body_dynamics(
    const RigidBodyWorldContext&, RigidBodyState&, const VacuumIntent&,
    MoonBodyDynamicsRecipe recipe = {},
    SimulationSeconds step = kSimulationStep)
    -> std::expected<MoonBodyActuation, MoonBodyError>;

} // namespace apsis_drift
