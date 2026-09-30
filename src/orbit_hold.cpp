#include "apsis_drift/orbit_hold.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace apsis_drift {
namespace {
using V = RigidVector3;
auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(V a, double b) -> V {
  return {a.x * b, a.y * b, a.z * b};
}
auto dot(V a, V b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto length(V a) -> double {
  return std::sqrt(dot(a, a));
}
auto finite(V a) -> bool {
  return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
auto body_force(RigidOrientation q, V force) -> V {
  // Transpose of the active body-to-owning-frame matrix. This projects a
  // controller demand, never modifies the authoritative attitude.
  const double n = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  return {(1 - 2 * (q.y * q.y + q.z * q.z) / n) * force.x +
              2 * (q.x * q.y + q.w * q.z) / n * force.y +
              2 * (q.x * q.z - q.w * q.y) / n * force.z,
          2 * (q.x * q.y - q.w * q.z) / n * force.x +
              (1 - 2 * (q.x * q.x + q.z * q.z) / n) * force.y +
              2 * (q.y * q.z + q.w * q.x) / n * force.z,
          2 * (q.x * q.z + q.w * q.y) / n * force.x +
              2 * (q.y * q.z - q.w * q.x) / n * force.y +
              (1 - 2 * (q.x * q.x + q.y * q.y) / n) * force.z};
}
auto valid_target(const OrbitHoldTarget& target,
                  const CentralBodyGravity& gravity,
                  OrbitalTelemetryRecipe policy) -> bool {
  const auto n = target.plane_normal;
  if (target.planet != gravity.planet || !std::isfinite(target.radius_metres) ||
      target.radius_metres <= gravity.reference_radius_metres +
                                  policy.space_boundary_altitude_metres ||
      target.radius_metres > kRigidBodyMaximumPositionMetres || !finite(n) ||
      std::abs(dot(n, n) - 1) > kRigidBodyOrientationSquaredNormTolerance)
    return false;
  for (const double x : {n.x, n.y, n.z})
    if (std::abs(x) > 1 || (x == 0 && std::signbit(x))) return false;
  return true;
}
} // namespace
auto validate_orbit_hold_request(const RigidBodyWorldContext& context,
                                 const RigidBodyState& state,
                                 const PhysicalPlanetRotationRecipe& rotation,
                                 OrbitalTelemetryRecipe policy,
                                 OrbitHoldRequest request,
                                 CentralBodyDynamicsRecipe dynamics)
    -> std::expected<void, OrbitHoldError> {
  using Code = OrbitHoldErrorCode;
  if (request.version != kOrbitHoldVersion)
    return std::unexpected{OrbitHoldError{Code::unsupported_version, {}, {}}};
  const auto observed =
      evaluate_orbital_telemetry(context, state, rotation, policy, dynamics);
  if (!observed)
    return std::unexpected{
        OrbitHoldError{Code::telemetry_failure, observed.error(), {}}};
  const auto gravity = evaluate_central_body_gravity(context, state, dynamics);
  if (!gravity)
    return std::unexpected{
        OrbitHoldError{Code::dynamics_failure, {}, gravity.error()}};
  if (request.target && !valid_target(*request.target, *gravity, policy))
    return std::unexpected{OrbitHoldError{Code::invalid_target, {}, {}}};
  return {};
}
auto advance_orbit_hold_dynamics(
    const RigidBodyWorldContext& context, RigidBodyState& state,
    const VacuumIntent& intent, const PhysicalPlanetRotationRecipe& rotation,
    OrbitalTelemetryRecipe policy, OrbitHoldRequest request,
    CentralBodyDynamicsRecipe dynamics, SimulationSeconds step)
    -> std::expected<OrbitHoldResult, OrbitHoldError> {
  using Code = OrbitHoldErrorCode;
  const auto valid = validate_orbit_hold_request(context, state, rotation,
                                                 policy, request, dynamics);
  if (!valid) return std::unexpected{valid.error()};
  const auto gravity = evaluate_central_body_gravity(context, state, dynamics);
  if (!gravity)
    return std::unexpected{
        OrbitHoldError{Code::dynamics_failure, {}, gravity.error()}};
  auto commands = intent;
  OrbitHoldResult result;
  if (request.target) {
    if (!intent.assistance)
      result.status = OrbitHoldStatus::paused_advanced;
    else if (intent.positive_translation != V{} ||
             intent.negative_translation != V{})
      result.status = OrbitHoldStatus::paused_manual_translation;
    else if (gravity->distance_metres <=
             gravity->reference_radius_metres +
                 policy.space_boundary_altitude_metres)
      result.status = OrbitHoldStatus::unavailable_environment;
    else {
      const auto& target = *request.target;
      const V projected =
          sub(state.position_metres,
              scale(target.plane_normal,
                    dot(state.position_metres, target.plane_normal)));
      const double projected_length = length(projected);
      if (projected_length <=
          gravity->distance_metres * kOrbitHoldMinimumPlaneProjectionFraction)
        result.status = OrbitHoldStatus::unavailable_geometry;
      else {
        const auto craft = resolve_craft_frame(state.craft);
        if (!craft)
          return std::unexpected{OrbitHoldError{
              Code::dynamics_failure,
              {},
              CentralBodyError{CentralBodyErrorCode::dynamics_failure,
                               VacuumDynamicsError::invalid_craft_frame}}};
        const auto& properties = craft->properties;
        const double mu =
            gravity->gravitational_parameter_metres_cubed_per_second_squared;
        const V radial = scale(projected, 1.0 / projected_length);
        const V target_position = scale(radial, target.radius_metres);
        const V target_velocity = scale(cross(target.plane_normal, radial),
                                        std::sqrt(mu / target.radius_metres));
        const V feedforward =
            scale(radial, -mu / (target.radius_metres * target.radius_metres));
        const V desired = add(
            feedforward, add(scale(sub(target_position, state.position_metres),
                                   kOrbitHoldPositionGainPerSecondSquared),
                             scale(sub(target_velocity,
                                       state.linear_velocity_metres_per_second),
                                   kOrbitHoldVelocityGainPerSecond)));
        result.requested_correction_metres_per_second_squared =
            sub(desired, gravity->acceleration_metres_per_second_squared);
        result.requested_force_body_newtons = body_force(
            state.orientation,
            scale(result.requested_correction_metres_per_second_squared,
                  properties.dry_mass_kg));
        if (!finite(result.requested_correction_metres_per_second_squared) ||
            !finite(result.requested_force_body_newtons))
          return std::unexpected{
              OrbitHoldError{Code::unsafe_correction, {}, {}}};
        const auto wanted = result.requested_force_body_newtons;
        const std::array<double, 3> values{wanted.x, wanted.y, wanted.z};
        const std::array<double*, 3> positive{&commands.positive_translation.x,
                                              &commands.positive_translation.y,
                                              &commands.positive_translation.z};
        const std::array<double*, 3> negative{&commands.negative_translation.x,
                                              &commands.negative_translation.y,
                                              &commands.negative_translation.z};
        bool saturated = false;
        for (std::size_t i = 0; i < values.size(); ++i) {
          const double upper = properties.positive_force_newtons[i],
                       lower = properties.negative_force_newtons[i];
          saturated = saturated || values[i] > upper || values[i] < -lower;
          *positive[i] =
              upper > 0 ? std::clamp(values[i] / upper, 0.0, 1.0) : 0.0;
          *negative[i] =
              lower > 0 ? std::clamp(-values[i] / lower, 0.0, 1.0) : 0.0;
        }
        result.status =
            saturated ? OrbitHoldStatus::saturated : OrbitHoldStatus::active;
      }
    }
  }
  auto candidate = state;
  const auto actuation = advance_central_body_dynamics(
      context, candidate, commands, dynamics, step);
  if (!actuation)
    return std::unexpected{
        OrbitHoldError{Code::dynamics_failure, {}, actuation.error()}};
  result.actuation = *actuation;
  if (result.status == OrbitHoldStatus::active ||
      result.status == OrbitHoldStatus::saturated)
    result.applied_force_body_newtons =
        actuation->propulsion.applied_force_newtons;
  const auto after = evaluate_orbital_telemetry(context, candidate, rotation,
                                                policy, dynamics);
  if (!after)
    return std::unexpected{
        OrbitHoldError{Code::telemetry_failure, after.error(), {}}};
  result.observation_after = *after;
  result.orbit_established =
      after->classification == OrbitClassification::stable;
  state = candidate;
  return result;
}
} // namespace apsis_drift
