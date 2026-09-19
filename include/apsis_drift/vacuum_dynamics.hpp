#pragma once

#include "apsis_drift/rigid_body.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kVacuumDynamicsVersion{1};
inline constexpr double kVacuumLateralDampingPerSecond{0.65};

// Independent physical actuator fractions, each component in [0,1]. Axes are
// body right/up/back. Main thrust is negative Z; weaker retro is positive Z.
// Equal opposing physical forces cancel; equal main/retro fractions do not.
struct VacuumIntent {
  RigidVector3 positive_translation, negative_translation;
  RigidVector3 positive_rotation, negative_rotation;
  bool assistance{true}; // runtime stabilization, NOT career penalty profile
};

struct VacuumActuation {
  RigidVector3 requested_force_newtons, requested_torque_newton_metres;
  // Actual correction after authority allocation, not an unlimited wish.
  RigidVector3 assist_force_newtons, assist_torque_newton_metres;
  RigidVector3 applied_force_newtons, applied_torque_newton_metres;
  // Gross channels retain opposing firings even when their net is zero.
  RigidVector3 positive_force_newtons, negative_force_newtons;
  RigidVector3 positive_torque_newton_metres, negative_torque_newton_metres;
  RigidVector3 world_linear_impulse_newton_seconds;
  RigidVector3 world_angular_impulse_newton_metre_seconds;
  bool assistance{}, force_saturated{}, torque_saturated{};
};

enum class VacuumDynamicsError : std::uint8_t {
  invalid_state,
  unsupported_coordinate_frame,
  invalid_craft_frame,
  invalid_intent,
  invalid_step,
  tick_overflow,
  unsafe_arithmetic
};

// One fixed120Hz step, all-or-nothing. No external forces, spool/filter state,
// renderer data or implicit coordinate transform. Only system_inertial is
// supported until the frame-composition provider supplies noninertial terms.
// Commanded angular-rate ratings bound assisted targets, never existing spin.
[[nodiscard]] auto advance_vacuum_dynamics(
    const RigidBodyWorldContext& context, RigidBodyState& state,
    const VacuumIntent& intent, SimulationSeconds step = kSimulationStep)
    -> std::expected<VacuumActuation, VacuumDynamicsError>;

} // namespace apsis_drift
