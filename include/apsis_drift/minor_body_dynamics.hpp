#pragma once

#include "apsis_drift/central_body_dynamics.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kMinorBodyDynamicsVersion{1};

struct MinorBodyDynamicsRecipe {
  std::uint32_t version{kMinorBodyDynamicsVersion};
  friend auto operator==(const MinorBodyDynamicsRecipe&,
                         const MinorBodyDynamicsRecipe&) -> bool = default;
};

// Share the existing bounded central-body refusal vocabulary, not a force
// registry. Minor reports retain BodyId and never manufacture a PlanetId.
using MinorBodyError = CentralBodyError;
struct MinorBodyGravity {
  BodyId body;
  double reference_radius_metres{}, surface_gravity_metres_per_second_squared{};
  double gravitational_parameter_metres_cubed_per_second_squared{};
  double distance_metres{};
  RigidVector3 acceleration_metres_per_second_squared;
  friend auto operator==(const MinorBodyGravity&, const MinorBodyGravity&)
      -> bool = default;
};
struct MinorBodyActuation {
  VacuumActuation propulsion;
  MinorBodyGravity initial_gravity;
  RigidVector3 gravity_impulse_newton_seconds;
  RigidVector3 total_linear_impulse_newton_seconds;
};

// Requires the exact authenticated PhysicalMinorDescriptor in context and its
// body_relative_inertial frame. No atmosphere, parent/tidal/N-body force,
// surface support, automatic hold or gameplay save adoption is implied.
[[nodiscard]] auto evaluate_minor_body_gravity(
    const RigidBodyWorldContext&, const RigidBodyState&,
    MinorBodyDynamicsRecipe recipe = {})
    -> std::expected<MinorBodyGravity, MinorBodyError>;

// One atomic120Hz tick through the SAME existing coupled RK4/torque kernel.
// Gravity is reevaluated at every stage. Assistance stabilizes rotation only;
// gravity never occupies gross propulsion channels or fuel impulses.
[[nodiscard]] auto advance_minor_body_dynamics(
    const RigidBodyWorldContext&, RigidBodyState&, const VacuumIntent&,
    MinorBodyDynamicsRecipe recipe = {},
    SimulationSeconds step = kSimulationStep)
    -> std::expected<MinorBodyActuation, MinorBodyError>;

inline constexpr double kMinorInspectionStandoffMetres{1000.0};
enum class MinorExclusionClass : std::uint8_t { clear, intersects, unknown };
struct MinorExclusionSweep {
  MinorExclusionClass classification{MinorExclusionClass::unknown};
  BodyId body;
  SimulationTick source_tick{}, candidate_tick{};
  double hull_radius_metres{}, exclusion_radius_metres{};
  double minimum_distance_metres{};
  // Conservative arithmetic uncertainty, not additional physical clearance.
  double arithmetic_uncertainty_metres{};
};
[[nodiscard]] auto minor_exclusion_radius_metres(const PhysicalMinorDescriptor&,
                                                 CraftFrameRecipe)
    -> std::expected<double, MinorBodyError>;
// A conservative origin-centred sphere enclosing the complete craft hull is
// swept along the candidate tick's COM segment. Source must be strictly clear.
// This prototype inspection guard provides no landing, damage or solid mesh.
[[nodiscard]] auto assess_minor_exclusion_sweep(const PhysicalMinorDescriptor&,
                                                const RigidBodyState& before,
                                                const RigidBodyState& candidate)
    -> std::expected<MinorExclusionSweep, MinorBodyError>;
// Reassess the genuine pair. Only a known intersection is eligible: preserve
// candidate tick/attitude/angular velocity, retain last clear COM position and
// remove inward radial candidate velocity. Clear/unknown pairs refuse here.
[[nodiscard]] auto respond_minor_exclusion_sweep(
    const PhysicalMinorDescriptor&, const RigidBodyState& before,
    const RigidBodyState& candidate)
    -> std::expected<RigidBodyState, MinorBodyError>;

} // namespace apsis_drift
