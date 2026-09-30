#include "apsis_drift/orbital_telemetry.hpp"

#include <cmath>
#include <limits>

namespace apsis_drift {
namespace {
using V = RigidVector3;
auto dot(V a, V b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto scale(V a, double f) -> V {
  return {a.x * f, a.y * f, a.z * f};
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto length(V a) -> double {
  return std::sqrt(dot(a, a));
}
auto finite(V a) -> bool {
  return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
} // namespace

auto evaluate_orbital_telemetry(const RigidBodyWorldContext& context,
                                const RigidBodyState& state,
                                const PhysicalPlanetRotationRecipe& rotation,
                                OrbitalTelemetryRecipe policy,
                                CentralBodyDynamicsRecipe dynamics)
    -> std::expected<OrbitalTelemetry, OrbitalTelemetryError> {
  using Code = OrbitalTelemetryErrorCode;
  if (policy.version != kOrbitalTelemetryVersion)
    return std::unexpected{
        OrbitalTelemetryError{Code::unsupported_version, {}, {}}};
  const double edge = policy.space_boundary_altitude_metres;
  if (!std::isfinite(edge) || edge < 0 ||
      edge > kRigidBodyMaximumPositionMetres ||
      (edge == 0 && std::signbit(edge)))
    return std::unexpected{OrbitalTelemetryError{Code::invalid_policy, {}, {}}};
  const auto gravity = evaluate_central_body_gravity(context, state, dynamics);
  if (!gravity)
    return std::unexpected{
        OrbitalTelemetryError{Code::gravity_failure, gravity.error(), {}}};
  // Successful gravity qualification proves physical owner and named planet.
  if (rotation.rotation.planet != gravity->planet)
    return std::unexpected{OrbitalTelemetryError{
        Code::rotation_failure, {}, PlanetRotationError::owner_mismatch}};
  const auto geometry =
      resolve_planet_rotation(*context.physical_owner(), rotation, state.tick);
  if (!geometry)
    return std::unexpected{
        OrbitalTelemetryError{Code::rotation_failure, {}, geometry.error()}};
  const auto& angular = geometry->geometry.angular_velocity_radians_per_second;
  const V omega{angular.x, angular.y, angular.z};
  const V r = state.position_metres,
          v = state.linear_velocity_metres_per_second;
  const double distance = gravity->distance_metres;
  const double mu =
      gravity->gravitational_parameter_metres_cubed_per_second_squared;
  const V radial = scale(r, 1.0 / distance);
  const V momentum = cross(r, v);
  const V e_vector = sub(scale(cross(v, momentum), 1.0 / mu), radial);
  const double v2 = dot(v, v), potential = mu / distance;
  OrbitalTelemetry result;
  result.planet = gravity->planet;
  result.tick = state.tick;
  result.inertial_speed_metres_per_second = std::sqrt(v2);
  result.surface_relative_velocity_metres_per_second = sub(v, cross(omega, r));
  result.surface_relative_speed_metres_per_second =
      length(result.surface_relative_velocity_metres_per_second);
  result.radial_rate_metres_per_second = dot(v, radial);
  result.specific_energy_metres_squared_per_second_squared =
      v2 * .5 - potential;
  result.specific_angular_momentum_metres_squared_per_second = momentum;
  result.eccentricity = length(e_vector);
  const double semilatus = dot(momentum, momentum) / mu;
  result.periapsis_radius_metres = semilatus / (1.0 + result.eccentricity);
  const double energy =
      result.specific_energy_metres_squared_per_second_squared;
  result.energy_tolerance_metres_squared_per_second_squared =
      64 * std::numeric_limits<double>::epsilon() * (v2 * .5 + potential);
  result.near_parabolic =
      std::abs(energy) <=
      result.energy_tolerance_metres_squared_per_second_squared;
  result.bound =
      energy < -result.energy_tolerance_metres_squared_per_second_squared;
  if (result.bound) {
    // Use the same geometric invariant for both apsides. Energy-derived
    // 2a-peri can round below peri on a circular orbit and falsely lose apo.
    // Radial bound trajectories have e==1 and zero semilatus; energy gives
    // their finite outer turning point without a 0/0 geometric division.
    const double apoapsis =
        result.eccentricity < 1 && semilatus > 0
            ? semilatus / (1.0 - result.eccentricity)
            : (-mu / energy) - result.periapsis_radius_metres;
    if (std::isfinite(apoapsis) && apoapsis >= result.periapsis_radius_metres &&
        apoapsis <= kMaximumOrbitalElementRadiusMetres)
      result.apoapsis_radius_metres = apoapsis;
  }
  const double surface = gravity->reference_radius_metres;
  // On an escaping outbound branch a below-surface periapsis is in the past.
  // Bound trajectories revisit it; inbound unbound trajectories have it ahead.
  const bool intersects =
      distance <= surface ||
      (result.periapsis_radius_metres <= surface &&
       (result.bound || result.radial_rate_metres_per_second < 0));
  if (intersects)
    result.classification = OrbitClassification::impact;
  else if (!result.bound)
    result.classification = OrbitClassification::escape;
  else if (result.periapsis_radius_metres <= surface + edge)
    result.classification = OrbitClassification::decaying;
  else
    result.classification = OrbitClassification::stable;
  const double spin = length(omega);
  result.synchronous_radius_metres = std::cbrt(mu / (spin * spin));
  const double circular_speed = std::sqrt(potential);
  const double tolerance = kSynchronousOrbitRelativeTolerance;
  result.synchronous_orbit =
      result.bound && result.classification == OrbitClassification::stable &&
      result.eccentricity <= tolerance &&
      std::abs(distance - result.synchronous_radius_metres) <=
          result.synchronous_radius_metres * tolerance &&
      std::abs(dot(r, scale(omega, 1.0 / spin))) <= distance * tolerance &&
      dot(momentum, omega) > 0 &&
      std::abs(result.radial_rate_metres_per_second) <=
          circular_speed * tolerance &&
      result.surface_relative_speed_metres_per_second <=
          circular_speed * tolerance;
  for (const double value :
       {result.inertial_speed_metres_per_second,
        result.surface_relative_speed_metres_per_second,
        result.radial_rate_metres_per_second, energy, result.eccentricity,
        result.periapsis_radius_metres, result.synchronous_radius_metres,
        result.energy_tolerance_metres_squared_per_second_squared})
    if (!std::isfinite(value))
      return std::unexpected{
          OrbitalTelemetryError{Code::unsafe_arithmetic, {}, {}}};
  if (!finite(momentum) ||
      !finite(result.surface_relative_velocity_metres_per_second) ||
      result.periapsis_radius_metres > kMaximumOrbitalElementRadiusMetres ||
      result.synchronous_radius_metres > kMaximumOrbitalElementRadiusMetres)
    return std::unexpected{
        OrbitalTelemetryError{Code::unsafe_arithmetic, {}, {}}};
  return result;
}
} // namespace apsis_drift
