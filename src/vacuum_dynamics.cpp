#include "apsis_drift/vacuum_dynamics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace apsis_drift {
namespace {
using V = RigidVector3;
using Q = RigidOrientation;

auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto subtract(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(V v, double factor) -> V {
  return {v.x * factor, v.y * factor, v.z * factor};
}
auto multiply(V a, V b) -> V {
  return {a.x * b.x, a.y * b.y, a.z * b.z};
}
auto divide(V a, V b) -> V {
  return {a.x / b.x, a.y / b.y, a.z / b.z};
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto finite(V v) -> bool {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
auto finite(Q q) -> bool {
  return std::isfinite(q.w) && finite(V{q.x, q.y, q.z});
}
auto norm_squared(Q q) -> double {
  return ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
}
auto conjugate(Q q) -> Q {
  return {q.w, -q.x, -q.y, -q.z};
}
auto add(Q a, Q b) -> Q {
  return {a.w + b.w, a.x + b.x, a.y + b.y, a.z + b.z};
}
auto scale(Q q, double factor) -> Q {
  return {q.w * factor, q.x * factor, q.y * factor, q.z * factor};
}
auto multiply(Q a, Q b) -> Q {
  return {((a.w * b.w - a.x * b.x) - a.y * b.y) - a.z * b.z,
          ((a.w * b.x + a.x * b.w) + a.y * b.z) - a.z * b.y,
          ((a.w * b.y - a.x * b.z) + a.y * b.w) + a.z * b.x,
          ((a.w * b.z + a.x * b.y) - a.y * b.x) + a.z * b.w};
}
auto rotate(Q q, V v) -> V {
  // RK stage quaternions need not be unit: use q*v*q^-1, NOT conjugate
  // alone. This evaluates a rotation without normalizing stored/stage q.
  const auto product = multiply(multiply(q, Q{0, v.x, v.y, v.z}), conjugate(q));
  return scale(V{product.x, product.y, product.z}, 1.0 / norm_squared(q));
}
auto vector(const std::array<std::uint32_t, 3>& values) -> V {
  return {static_cast<double>(values[0]), static_cast<double>(values[1]),
          static_cast<double>(values[2])};
}
auto vector(const std::array<std::uint64_t, 3>& values) -> V {
  return {static_cast<double>(values[0]), static_cast<double>(values[1]),
          static_cast<double>(values[2])};
}
auto valid_fraction(V v) -> bool {
  return finite(v) && v.x >= 0 && v.x <= 1 && v.y >= 0 && v.y <= 1 &&
         v.z >= 0 && v.z <= 1;
}

struct Allocation {
  V positive, negative, net;
  bool saturated{};
};
auto allocate(V positive_request, V negative_request, V desired_net,
              V positive_limit, V negative_limit) -> Allocation {
  Allocation result;
  const std::array positive{positive_request.x, positive_request.y,
                            positive_request.z};
  const std::array negative{negative_request.x, negative_request.y,
                            negative_request.z};
  const std::array desired{desired_net.x, desired_net.y, desired_net.z};
  const std::array upper{positive_limit.x, positive_limit.y, positive_limit.z};
  const std::array lower{negative_limit.x, negative_limit.y, negative_limit.z};
  const std::array net{&result.net.x, &result.net.y, &result.net.z};
  const std::array applied_positive{&result.positive.x, &result.positive.y,
                                    &result.positive.z};
  const std::array applied_negative{&result.negative.x, &result.negative.y,
                                    &result.negative.z};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (desired[axis] == positive[axis] - negative[axis]) {
      *applied_positive[axis] = positive[axis];
      *applied_negative[axis] = negative[axis];
      *net[axis] = desired[axis];
      continue;
    }
    // Preserve explicitly requested opposing firings. Only the remaining
    // capacity can realize the stabilized net demand; no invisible actuator.
    const double opposing = std::min(positive[axis], negative[axis]);
    const double applied = std::clamp(desired[axis], opposing - lower[axis],
                                      upper[axis] - opposing);
    *applied_positive[axis] = opposing + std::max(applied, 0.0);
    *applied_negative[axis] = opposing + std::max(-applied, 0.0);
    *net[axis] = *applied_positive[axis] - *applied_negative[axis];
    result.saturated = result.saturated || applied != desired[axis];
  }
  return result;
}

struct Integrated {
  V position, velocity;
  Q orientation;
  V angular_momentum;
};
auto sum(const Integrated& a, const Integrated& b, double factor)
    -> Integrated {
  return {add(a.position, scale(b.position, factor)),
          add(a.velocity, scale(b.velocity, factor)),
          add(a.orientation, scale(b.orientation, factor)),
          add(a.angular_momentum, scale(b.angular_momentum, factor))};
}
auto valid_stage(const Integrated& value) -> bool {
  const double norm = norm_squared(value.orientation);
  return finite(value.position) && finite(value.velocity) &&
         finite(value.orientation) && finite(value.angular_momentum) &&
         std::isfinite(norm) && norm >= .125 && norm <= 8;
}
auto derivative(const Integrated& value, V inertia, double mass, V force,
                V torque) -> Integrated {
  const auto omega = divide(
      rotate(conjugate(value.orientation), value.angular_momentum), inertia);
  return {
      value.velocity, scale(rotate(value.orientation, force), 1.0 / mass),
      scale(multiply(value.orientation, Q{0, omega.x, omega.y, omega.z}), .5),
      rotate(value.orientation, torque)};
}
auto weighted(V a, V b, V c, V d) -> V {
  return add(add(add(a, scale(b, 2)), scale(c, 2)), d);
}
auto weighted(Q a, Q b, Q c, Q d) -> Q {
  return add(add(add(a, scale(b, 2)), scale(c, 2)), d);
}
} // namespace

auto advance_vacuum_dynamics(const RigidBodyWorldContext& context,
                             RigidBodyState& state, const VacuumIntent& intent,
                             SimulationSeconds step)
    -> std::expected<VacuumActuation, VacuumDynamicsError> {
  if (!std::isfinite(step.count()) || step != kSimulationStep)
    return std::unexpected{VacuumDynamicsError::invalid_step};
  if (state.tick >= std::numeric_limits<SimulationTick>::max() - 1)
    return std::unexpected{VacuumDynamicsError::tick_overflow};
  const auto frame = resolve_craft_frame(state.craft);
  if (!frame || !validate_craft_frame_properties(frame->properties) ||
      !supports_operation(frame->properties, CraftOperation::vacuum))
    return std::unexpected{VacuumDynamicsError::invalid_craft_frame};
  if (!validate_rigid_body_state(context, state))
    return std::unexpected{VacuumDynamicsError::invalid_state};
  if (state.frame.kind != RigidFrameKind::system_inertial)
    return std::unexpected{VacuumDynamicsError::unsupported_coordinate_frame};
  if (!valid_fraction(intent.positive_translation) ||
      !valid_fraction(intent.negative_translation) ||
      !valid_fraction(intent.positive_rotation) ||
      !valid_fraction(intent.negative_rotation))
    return std::unexpected{VacuumDynamicsError::invalid_intent};

  const auto& properties = frame->properties;
  const auto positive_limit = vector(properties.positive_force_newtons);
  const auto negative_limit = vector(properties.negative_force_newtons);
  const auto torque_limit = vector(properties.torque_newton_metres);
  const auto inertia = vector(properties.principal_inertia_kg_m2);
  const double mass = properties.dry_mass_kg;
  const double dt = step.count();
  const auto positive_force =
      multiply(intent.positive_translation, positive_limit);
  const auto negative_force =
      multiply(intent.negative_translation, negative_limit);
  const auto positive_torque = multiply(intent.positive_rotation, torque_limit);
  const auto negative_torque = multiply(intent.negative_rotation, torque_limit);
  VacuumActuation report;
  report.assistance = intent.assistance;
  report.requested_force_newtons = subtract(positive_force, negative_force);
  report.requested_torque_newton_metres =
      subtract(positive_torque, negative_torque);
  auto desired_force = report.requested_force_newtons;
  auto desired_torque = report.requested_torque_newton_metres;
  const auto angular = state.angular_velocity_radians_per_second;
  if (intent.assistance) {
    const auto body_velocity = rotate(conjugate(state.orientation),
                                      state.linear_velocity_metres_per_second);
    if (intent.positive_translation.x == 0 &&
        intent.negative_translation.x == 0)
      desired_force.x =
          -mass * kVacuumLateralDampingPerSecond * body_velocity.x;
    if (intent.positive_translation.y == 0 &&
        intent.negative_translation.y == 0)
      desired_force.y =
          -mass * kVacuumLateralDampingPerSecond * body_velocity.y;
    // No forward velocity hold. Target angular rates are command ratings,
    // not a post-integration speed clamp. Gyroscopic compensation is real,
    // bounded, reported torque and belongs in future fuel accounting.
    const auto target = multiply(
        subtract(intent.positive_rotation, intent.negative_rotation),
        scale(vector(properties.max_angular_rate_milliradians_per_second),
              .001));
    desired_torque =
        add(scale(multiply(inertia, subtract(target, angular)), 1.0 / dt),
            cross(angular, multiply(inertia, angular)));
  }
  if (!finite(desired_force) || !finite(desired_torque))
    return std::unexpected{VacuumDynamicsError::unsafe_arithmetic};
  const auto force = allocate(positive_force, negative_force, desired_force,
                              positive_limit, negative_limit);
  const auto torque = allocate(positive_torque, negative_torque, desired_torque,
                               torque_limit, torque_limit);
  report.positive_force_newtons = force.positive;
  report.negative_force_newtons = force.negative;
  report.applied_force_newtons = force.net;
  report.assist_force_newtons =
      subtract(force.net, report.requested_force_newtons);
  report.positive_torque_newton_metres = torque.positive;
  report.negative_torque_newton_metres = torque.negative;
  report.applied_torque_newton_metres = torque.net;
  report.assist_torque_newton_metres =
      subtract(torque.net, report.requested_torque_newton_metres);
  report.force_saturated = force.saturated;
  report.torque_saturated = torque.saturated;

  const Integrated initial{
      state.position_metres, state.linear_velocity_metres_per_second,
      state.orientation, rotate(state.orientation, multiply(inertia, angular))};
  const auto a = derivative(initial, inertia, mass, force.net, torque.net);
  const auto second = sum(initial, a, dt * .5);
  if (!valid_stage(second))
    return std::unexpected{VacuumDynamicsError::unsafe_arithmetic};
  const auto b = derivative(second, inertia, mass, force.net, torque.net);
  const auto third = sum(initial, b, dt * .5);
  if (!valid_stage(third))
    return std::unexpected{VacuumDynamicsError::unsafe_arithmetic};
  const auto c = derivative(third, inertia, mass, force.net, torque.net);
  const auto fourth = sum(initial, c, dt);
  if (!valid_stage(fourth))
    return std::unexpected{VacuumDynamicsError::unsafe_arithmetic};
  const auto d = derivative(fourth, inertia, mass, force.net, torque.net);
  const double weight = dt / 6.0;
  const auto impulse = scale(
      weighted(a.velocity, b.velocity, c.velocity, d.velocity), mass * weight);
  const auto angular_impulse =
      scale(weighted(a.angular_momentum, b.angular_momentum, c.angular_momentum,
                     d.angular_momentum),
            weight);
  const auto next_orientation = normalize_rigid_orientation(
      add(initial.orientation, scale(weighted(a.orientation, b.orientation,
                                              c.orientation, d.orientation),
                                     weight)));
  if (!next_orientation || !finite(impulse) || !finite(angular_impulse))
    return std::unexpected{VacuumDynamicsError::unsafe_arithmetic};
  auto candidate = state;
  candidate.position_metres = add(
      initial.position,
      scale(weighted(a.position, b.position, c.position, d.position), weight));
  candidate.linear_velocity_metres_per_second = add(
      initial.velocity,
      scale(weighted(a.velocity, b.velocity, c.velocity, d.velocity), weight));
  candidate.orientation = *next_orientation;
  candidate.angular_velocity_radians_per_second =
      divide(rotate(conjugate(candidate.orientation),
                    add(initial.angular_momentum, angular_impulse)),
             inertia);
  ++candidate.tick;
  const auto canonical = canonicalize_rigid_body_state(context, candidate);
  if (!canonical)
    return std::unexpected{VacuumDynamicsError::unsafe_arithmetic};
  report.world_linear_impulse_newton_seconds = impulse;
  report.world_angular_impulse_newton_metre_seconds = angular_impulse;
  state = *canonical;
  return report;
}
} // namespace apsis_drift
