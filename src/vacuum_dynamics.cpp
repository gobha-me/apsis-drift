#include "apsis_drift/vacuum_dynamics.hpp"

#include "apsis_drift/atmospheric_flight.hpp"
#include "apsis_drift/central_body_dynamics.hpp"

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

struct TorquePlan {
  V requested;
  Allocation allocation;
};
auto torque_plan(const CraftFrameProperties& properties, V angular,
                 V positive_rotation, V negative_rotation, bool assistance,
                 double dt) -> std::expected<TorquePlan, VacuumDynamicsError> {
  const auto torque_limit = vector(properties.torque_newton_metres);
  const auto inertia = vector(properties.principal_inertia_kg_m2);
  const auto positive = multiply(positive_rotation, torque_limit);
  const auto negative = multiply(negative_rotation, torque_limit);
  const auto requested = subtract(positive, negative);
  auto desired = requested;
  if (assistance) {
    // Rate ratings bound command targets, not existing angular momentum.
    // Gyroscopic compensation is actual torque allocated and reported below.
    const auto target = multiply(
        subtract(positive_rotation, negative_rotation),
        scale(vector(properties.max_angular_rate_milliradians_per_second),
              .001));
    desired = add(scale(multiply(inertia, subtract(target, angular)), 1.0 / dt),
                  cross(angular, multiply(inertia, angular)));
  }
  if (!finite(desired))
    return std::unexpected{VacuumDynamicsError::unsafe_arithmetic};
  return TorquePlan{requested, allocate(positive, negative, desired,
                                        torque_limit, torque_limit)};
}

auto valid_attitude(Q q, V omega) -> bool {
  if (!finite(q) || !finite(omega)) return false;
  bool first = true;
  for (const double value : {q.w, q.x, q.y, q.z}) {
    if (std::abs(value) > 2 || (value == 0 && std::signbit(value)))
      return false;
    if (value != 0 && first) {
      if (value < 0) return false;
      first = false;
    }
  }
  if (std::abs(norm_squared(q) - 1) > kRigidBodyOrientationSquaredNormTolerance)
    return false;
  for (const double value : {omega.x, omega.y, omega.z})
    if (std::abs(value) > kRigidBodyMaximumAngularVelocityRadiansPerSecond ||
        (value == 0 && std::signbit(value)))
      return false;
  return true;
}

// This is the bounded central-body composition, not a general force registry.
struct GravityParameters {
  PlanetId planet;
  double radius, surface_gravity, mu;
};
auto gravity_acceleration(const GravityParameters& gravity, V position)
    -> std::expected<V, CentralBodyError> {
  if (!finite(position) ||
      std::abs(position.x) > kRigidBodyMaximumPositionMetres ||
      std::abs(position.y) > kRigidBodyMaximumPositionMetres ||
      std::abs(position.z) > kRigidBodyMaximumPositionMetres)
    return std::unexpected{
        CentralBodyError{CentralBodyErrorCode::unsafe_arithmetic, {}}};
  const double r2 = (position.x * position.x + position.y * position.y) +
                    position.z * position.z;
  if (!std::isfinite(r2) ||
      r2 < kCentralBodyMinimumRadiusMetres * kCentralBodyMinimumRadiusMetres)
    return std::unexpected{
        CentralBodyError{CentralBodyErrorCode::invalid_radius, {}}};
  const auto acceleration = scale(position, -gravity.mu / (r2 * std::sqrt(r2)));
  if (!finite(acceleration))
    return std::unexpected{
        CentralBodyError{CentralBodyErrorCode::unsafe_arithmetic, {}}};
  return acceleration;
}
auto gravity_parameters(const RigidBodyWorldContext& context,
                        const RigidBodyState& state,
                        CentralBodyDynamicsRecipe recipe)
    -> std::expected<GravityParameters, CentralBodyError> {
  using Code = CentralBodyErrorCode;
  if (recipe.version != kCentralBodyDynamicsVersion)
    return std::unexpected{CentralBodyError{Code::unsupported_version, {}}};
  const auto* physical = context.physical_owner();
  if (physical == nullptr || !validate_local_system(*physical))
    return std::unexpected{CentralBodyError{Code::invalid_owner, {}}};
  if (state.frame.kind != RigidFrameKind::planet_relative_inertial)
    return std::unexpected{
        CentralBodyError{Code::unsupported_coordinate_frame, {}}};
  if (!state.frame.planet)
    return std::unexpected{CentralBodyError{Code::invalid_body, {}}};
  const auto planet = find_local_system_planet(*physical, *state.frame.planet);
  if (!planet) return std::unexpected{CentralBodyError{Code::invalid_body, {}}};
  if (!validate_rigid_body_state(context, state))
    return std::unexpected{CentralBodyError{Code::invalid_state, {}}};
  const auto& descriptor = (*planet)->descriptor;
  const double radius = static_cast<double>(descriptor.radius.value) * 1000.0;
  const double surface =
      static_cast<double>(descriptor.surface_gravity.value) * 9.80665 / 1000.0;
  const double mu = (surface * radius) * radius;
  if (!std::isfinite(mu) || mu <= 0)
    return std::unexpected{CentralBodyError{Code::unsafe_arithmetic, {}}};
  return GravityParameters{descriptor.id, radius, surface, mu};
}

auto gravity_sample(const GravityParameters& parameters, V position)
    -> std::expected<CentralBodyGravity, CentralBodyError> {
  const auto acceleration = gravity_acceleration(parameters, position);
  if (!acceleration) return std::unexpected{acceleration.error()};
  const double distance =
      std::sqrt((position.x * position.x + position.y * position.y) +
                position.z * position.z);
  return CentralBodyGravity{
      parameters.planet, parameters.radius, parameters.surface_gravity,
      parameters.mu,     distance,          *acceleration};
}

// Fixed bounded atmosphere, not a pluggable force registry. All constants and
// class scale heights belong to atmospheric-flight version one.
struct AtmosphereParameters {
  PlanetId planet;
  AtmosphereClass classification;
  double radius, surface_gravity, sea_pressure, sea_density, scale_height, edge;
  V spin;
  CraftFrameProperties craft;
};
auto atmosphere_parameters(const RigidBodyWorldContext& context,
                           const RigidBodyState& state,
                           const PhysicalPlanetRotationRecipe& rotation,
                           AtmosphericFlightRecipe recipe,
                           CentralBodyDynamicsRecipe dynamics)
    -> std::expected<AtmosphereParameters, AtmosphericFlightError> {
  using Code = AtmosphericFlightErrorCode;
  if (recipe.version != kAtmosphericFlightVersion)
    return std::unexpected{
        AtmosphericFlightError{Code::unsupported_version, {}, {}, {}}};
  const auto gravity = evaluate_central_body_gravity(context, state, dynamics);
  if (!gravity)
    return std::unexpected{
        AtmosphericFlightError{Code::gravity_failure, gravity.error(), {}, {}}};
  if (rotation.rotation.planet != gravity->planet)
    return std::unexpected{AtmosphericFlightError{
        Code::rotation_failure, {}, PlanetRotationError::owner_mismatch, {}}};
  const auto geometry =
      resolve_planet_rotation(*context.physical_owner(), rotation, state.tick);
  if (!geometry)
    return std::unexpected{AtmosphericFlightError{
        Code::rotation_failure, {}, geometry.error(), {}}};
  const auto planet =
      find_local_system_planet(*context.physical_owner(), gravity->planet);
  if (!planet)
    return std::unexpected{
        AtmosphericFlightError{Code::invalid_atmosphere, {}, {}, {}}};
  const auto& descriptor = (*planet)->descriptor;
  const auto craft = resolve_craft_frame(state.craft);
  if (!craft ||
      !supports_operation(craft->properties, CraftOperation::atmosphere))
    return std::unexpected{
        AtmosphericFlightError{Code::unsupported_craft, {}, {}, {}}};
  const auto profile = resolve_atmospheric_flight_profile(descriptor, recipe);
  if (!profile) return std::unexpected{profile.error()};
  const double pressure = profile->sea_level_pressure_millibars;
  const double rho = profile->sea_level_density_kg_per_cubic_metre;
  const double height = profile->scale_height_metres;
  const double edge = profile->space_boundary_altitude_metres;
  const auto& omega = geometry->geometry.angular_velocity_radians_per_second;
  return AtmosphereParameters{
      gravity->planet,
      descriptor.atmosphere_class,
      gravity->reference_radius_metres,
      gravity->surface_gravity_metres_per_second_squared,
      pressure,
      rho,
      height,
      edge,
      {omega.x, omega.y, omega.z},
      craft->properties};
}
auto dot(V a, V b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto magnitude(V a) -> double {
  return std::sqrt(dot(a, a));
}
auto atmosphere_sample(const AtmosphereParameters& p, V position, V velocity,
                       Q orientation, V angular, const VacuumIntent& intent)
    -> std::expected<AtmosphericFlightSample, AtmosphericFlightError> {
  using Code = AtmosphericFlightErrorCode;
  if (!finite(position) || !finite(velocity) || !finite(orientation) ||
      !finite(angular) || !valid_fraction(intent.positive_translation) ||
      !valid_fraction(intent.negative_translation) ||
      !valid_fraction(intent.positive_rotation) ||
      !valid_fraction(intent.negative_rotation))
    return std::unexpected{
        AtmosphericFlightError{Code::unsafe_arithmetic, {}, {}, {}}};
  const double distance = magnitude(position), norm = norm_squared(orientation);
  if (!std::isfinite(distance) || distance < kCentralBodyMinimumRadiusMetres ||
      !std::isfinite(norm) || norm < .125 || norm > 8)
    return std::unexpected{
        AtmosphericFlightError{Code::unsafe_arithmetic, {}, {}, {}}};
  AtmosphericFlightSample result;
  result.planet = p.planet;
  result.atmosphere_class = p.classification;
  result.altitude_metres = distance - p.radius;
  result.space_boundary_altitude_metres = p.edge;
  if (p.sea_density > 0 && result.altitude_metres < p.edge) {
    // No interior/compression model: below-surface samples use surface density.
    const double altitude = std::max(0.0, result.altitude_metres);
    const double taper =
        std::clamp((p.edge - altitude) / p.scale_height, 0.0, 1.0);
    const double profile =
        std::exp(-altitude / p.scale_height) * taper * taper * (3 - 2 * taper);
    result.density_kg_per_cubic_metre = p.sea_density * profile;
    result.pressure_millibars = p.sea_pressure * profile;
  }
  const V air = subtract(velocity, cross(p.spin, position));
  result.air_relative_velocity_metres_per_second = air;
  result.air_speed_metres_per_second = magnitude(air);
  result.radial_rate_metres_per_second =
      dot(velocity, scale(position, 1 / distance));
  const V body = rotate(conjugate(orientation), air);
  const double speed = result.air_speed_metres_per_second;
  const double rho = result.density_kg_per_cubic_metre;
  result.dynamic_pressure_pascals = .5 * rho * speed * speed;
  result.angle_of_attack_radians =
      body.z < 0 ? std::atan2(-body.y, -body.z) : 0;
  result.within_rated_envelope =
      result.altitude_metres >= 0 &&
      p.surface_gravity * 1000 <= p.craft.max_surface_gravity_mm_per_second2 &&
      result.pressure_millibars <= p.craft.max_pressure_millibars &&
      speed <= 350 && std::abs(result.angle_of_attack_radians) <= .35;
  const auto rate_limits = p.craft.max_angular_rate_milliradians_per_second;
  const std::array rates{angular.x, angular.y, angular.z};
  for (std::size_t i = 0; i < rates.size(); ++i)
    result.within_rated_envelope = result.within_rated_envelope &&
                                   std::abs(rates[i]) * 1000 <= rate_limits[i];
  result.within_rated_envelope =
      result.within_rated_envelope && (speed < 1 || body.z < 0);
  if (rho > 0 && speed > 0) {
    const V area = scale(vector(p.craft.drag_area_square_mm), 1e-6);
    result.drag_force_body_newtons =
        scale(multiply(area, body), -.5 * rho * speed);
    // Authored bounded lifting-body reference area 16 m^2 and CL slope 2/rad.
    // Project body-up perpendicular to relative air: lift does zero air work.
    if (body.z < 0) {
      const V direction = scale(body, 1 / speed);
      const V up = subtract(V{0, 1, 0}, scale(direction, direction.y));
      const double up_length = magnitude(up);
      if (up_length > 1e-12)
        result.lift_force_body_newtons = scale(
            up, result.dynamic_pressure_pascals * 16 *
                    std::clamp(2 * result.angle_of_attack_radians, -1.0, 1.0) /
                    up_length);
      const V weathercock = cross(V{0, 0, -1}, direction);
      const V limits = scale(vector(p.craft.torque_newton_metres), .25);
      const double stiffness = result.dynamic_pressure_pascals * 16 * 2;
      result.passive_torque_body_newton_metres = {
          std::clamp(weathercock.x * stiffness, -limits.x, limits.x),
          std::clamp(weathercock.y * stiffness, -limits.y, limits.y), 0};
    }
    const double weight = result.dynamic_pressure_pascals /
                          (result.dynamic_pressure_pascals + 250);
    const V relative_rate =
        subtract(angular, rotate(conjugate(orientation), p.spin));
    result.passive_torque_body_newton_metres = subtract(
        result.passive_torque_body_newton_metres,
        scale(multiply(vector(p.craft.principal_inertia_kg_m2), relative_rate),
              .3 * weight));
    const auto limits = vector(p.craft.torque_newton_metres);
    const double authority = result.dynamic_pressure_pascals * 16 * 2 * .1;
    result.control_authority_fraction = {std::min(authority / limits.x, .35),
                                         std::min(authority / limits.y, .35),
                                         std::min(authority / limits.z, .35)};
    result.control_torque_body_newton_metres = multiply(
        multiply(subtract(intent.positive_rotation, intent.negative_rotation),
                 limits),
        result.control_authority_fraction);
  }
  for (double x :
       {result.altitude_metres, result.density_kg_per_cubic_metre,
        result.pressure_millibars, speed, result.dynamic_pressure_pascals,
        result.radial_rate_metres_per_second})
    if (!std::isfinite(x))
      return std::unexpected{
          AtmosphericFlightError{Code::unsafe_arithmetic, {}, {}, {}}};
  if (!finite(result.drag_force_body_newtons) ||
      !finite(result.lift_force_body_newtons) ||
      !finite(result.passive_torque_body_newton_metres) ||
      !finite(result.control_torque_body_newton_metres))
    return std::unexpected{
        AtmosphericFlightError{Code::unsafe_arithmetic, {}, {}, {}}};
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

struct IntegratedResult {
  V position, velocity;
  Q orientation;
  V angular, linear_impulse, angular_impulse;
  V gravity_impulse, total_linear_impulse;
  V aerodynamic_impulse, aerodynamic_angular_impulse;
};
auto integrate(const Integrated& initial, V inertia, double mass, V force,
               V torque, double dt, const GravityParameters* gravity = nullptr,
               const AtmosphereParameters* atmosphere = nullptr,
               const VacuumIntent* intent = nullptr)
    -> std::expected<IntegratedResult, VacuumDynamicsError> {
  // Shared coupled RK4: full flight must retain these same attitude stages
  // when rotating thrust/torque into its owning frame. The attitude-only
  // caller supplies zero translation and force, not a different integrator.
  const auto evaluate =
      [&](const Integrated& value, V& external, V& aero_force,
          V& aero_torque) -> std::expected<Integrated, VacuumDynamicsError> {
    auto result = derivative(value, inertia, mass, force, torque);
    if (gravity != nullptr) {
      const auto acceleration = gravity_acceleration(*gravity, value.position);
      if (!acceleration)
        return std::unexpected{VacuumDynamicsError::unsafe_arithmetic};
      external = *acceleration;
      result.velocity = add(result.velocity, external);
    }
    if (atmosphere != nullptr) {
      const V angular =
          divide(rotate(conjugate(value.orientation), value.angular_momentum),
                 inertia);
      const auto sample =
          atmosphere_sample(*atmosphere, value.position, value.velocity,
                            value.orientation, angular, *intent);
      if (!sample)
        return std::unexpected{VacuumDynamicsError::unsafe_arithmetic};
      if (sample->density_kg_per_cubic_metre > 0) {
        aero_force =
            rotate(value.orientation, add(sample->drag_force_body_newtons,
                                          sample->lift_force_body_newtons));
        aero_torque = rotate(value.orientation,
                             add(sample->passive_torque_body_newton_metres,
                                 sample->control_torque_body_newton_metres));
        result.velocity = add(result.velocity, scale(aero_force, 1.0 / mass));
        result.angular_momentum = add(result.angular_momentum, aero_torque);
      }
    }
    return result;
  };
  V ga{}, gb{}, gc{}, gd{};
  V fa{}, fb{}, fc{}, fd{}, ta{}, tb{}, tc{}, td{};
  const auto first_derivative = evaluate(initial, ga, fa, ta);
  if (!first_derivative) return std::unexpected{first_derivative.error()};
  const auto& a = *first_derivative;
  const auto second = sum(initial, a, dt * .5);
  if (!valid_stage(second))
    return std::unexpected{VacuumDynamicsError::unsafe_arithmetic};
  const auto second_derivative = evaluate(second, gb, fb, tb);
  if (!second_derivative) return std::unexpected{second_derivative.error()};
  const auto& b = *second_derivative;
  const auto third = sum(initial, b, dt * .5);
  if (!valid_stage(third))
    return std::unexpected{VacuumDynamicsError::unsafe_arithmetic};
  const auto third_derivative = evaluate(third, gc, fc, tc);
  if (!third_derivative) return std::unexpected{third_derivative.error()};
  const auto& c = *third_derivative;
  const auto fourth = sum(initial, c, dt);
  if (!valid_stage(fourth))
    return std::unexpected{VacuumDynamicsError::unsafe_arithmetic};
  const auto fourth_derivative = evaluate(fourth, gd, fd, td);
  if (!fourth_derivative) return std::unexpected{fourth_derivative.error()};
  const auto& d = *fourth_derivative;
  const double weight = dt / 6.0;
  const auto impulse = scale(
      weighted(a.velocity, b.velocity, c.velocity, d.velocity), mass * weight);
  auto propulsion_impulse = impulse;
  V gravity_impulse{};
  if (gravity != nullptr) {
    gravity_impulse = scale(weighted(ga, gb, gc, gd), mass * weight);
    // Compute actual thruster impulse directly; subtracting gravity from total
    // would erase small actuator contributions through cancellation.
    propulsion_impulse = scale(
        weighted(derivative(initial, inertia, mass, force, torque).velocity,
                 derivative(second, inertia, mass, force, torque).velocity,
                 derivative(third, inertia, mass, force, torque).velocity,
                 derivative(fourth, inertia, mass, force, torque).velocity),
        mass * weight);
  }
  const auto total_angular_impulse =
      scale(weighted(a.angular_momentum, b.angular_momentum, c.angular_momentum,
                     d.angular_momentum),
            weight);
  const auto aerodynamic_impulse = scale(weighted(fa, fb, fc, fd), weight);
  const auto aerodynamic_angular_impulse =
      scale(weighted(ta, tb, tc, td), weight);
  auto angular_impulse = total_angular_impulse;
  if (atmosphere != nullptr) {
    angular_impulse = scale(weighted(rotate(initial.orientation, torque),
                                     rotate(second.orientation, torque),
                                     rotate(third.orientation, torque),
                                     rotate(fourth.orientation, torque)),
                            weight);
  }
  const auto next_orientation = normalize_rigid_orientation(
      add(initial.orientation, scale(weighted(a.orientation, b.orientation,
                                              c.orientation, d.orientation),
                                     weight)));
  if (!next_orientation || !finite(impulse) || !finite(propulsion_impulse) ||
      !finite(gravity_impulse) || !finite(angular_impulse) ||
      !finite(aerodynamic_impulse) || !finite(aerodynamic_angular_impulse))
    return std::unexpected{VacuumDynamicsError::unsafe_arithmetic};
  return IntegratedResult{
      add(initial.position,
          scale(weighted(a.position, b.position, c.position, d.position),
                weight)),
      add(initial.velocity,
          scale(weighted(a.velocity, b.velocity, c.velocity, d.velocity),
                weight)),
      *next_orientation,
      divide(rotate(conjugate(*next_orientation),
                    add(initial.angular_momentum, total_angular_impulse)),
             inertia),
      propulsion_impulse,
      angular_impulse,
      gravity_impulse,
      impulse,
      aerodynamic_impulse,
      aerodynamic_angular_impulse};
}
} // namespace

auto resolve_atmospheric_flight_profile(const PlanetDescriptor& descriptor,
                                        AtmosphericFlightRecipe recipe)
    -> std::expected<AtmosphericFlightProfile, AtmosphericFlightError> {
  using Code = AtmosphericFlightErrorCode;
  if (recipe.version != kAtmosphericFlightVersion)
    return std::unexpected{
        AtmosphericFlightError{Code::unsupported_version, {}, {}, {}}};
  double height{};
  const double pressure = descriptor.atmosphere_pressure.value;
  switch (descriptor.atmosphere_class) {
    case AtmosphereClass::airless:
      if (pressure != 0)
        return std::unexpected{
            AtmosphericFlightError{Code::invalid_atmosphere, {}, {}, {}}};
      break;
    case AtmosphereClass::tenuous: height = 6000; break;
    case AtmosphereClass::temperate: height = 8500; break;
    case AtmosphereClass::dense: height = 11000; break;
    default:
      return std::unexpected{
          AtmosphericFlightError{Code::invalid_atmosphere, {}, {}, {}}};
  }
  if (height != 0 &&
      (pressure <= 0 || pressure > AtmospherePressureMillibars::max))
    return std::unexpected{
        AtmosphericFlightError{Code::invalid_atmosphere, {}, {}, {}}};
  const double rho = 1.225 * (pressure / 1013.25);
  const double edge = height == 0 ? 0 : height * std::log(rho / 1e-6);
  return AtmosphericFlightProfile{pressure, rho, height, edge};
}

auto advance_vacuum_attitude(CraftFrameRecipe craft,
                             RigidOrientation orientation,
                             RigidVector3 angular_velocity_radians_per_second,
                             RigidVector3 positive_rotation,
                             RigidVector3 negative_rotation, bool assistance,
                             SimulationSeconds step)
    -> std::expected<VacuumAttitudeResult, VacuumDynamicsError> {
  if (!std::isfinite(step.count()) || step != kSimulationStep)
    return std::unexpected{VacuumDynamicsError::invalid_step};
  const auto frame = resolve_craft_frame(craft);
  if (!frame || !validate_craft_frame_properties(frame->properties) ||
      !supports_operation(frame->properties, CraftOperation::vacuum))
    return std::unexpected{VacuumDynamicsError::invalid_craft_frame};
  if (!valid_attitude(orientation, angular_velocity_radians_per_second))
    return std::unexpected{VacuumDynamicsError::invalid_state};
  if (!valid_fraction(positive_rotation) || !valid_fraction(negative_rotation))
    return std::unexpected{VacuumDynamicsError::invalid_intent};
  const auto& properties = frame->properties;
  const auto plan = torque_plan(properties, angular_velocity_radians_per_second,
                                positive_rotation, negative_rotation,
                                assistance, step.count());
  if (!plan) return std::unexpected{plan.error()};
  const auto inertia = vector(properties.principal_inertia_kg_m2);
  const Integrated initial{
      {},
      {},
      orientation,
      rotate(orientation,
             multiply(inertia, angular_velocity_radians_per_second))};
  const auto integrated = integrate(initial, inertia, properties.dry_mass_kg,
                                    {}, plan->allocation.net, step.count());
  if (!integrated) return std::unexpected{integrated.error()};
  auto angular = integrated->angular;
  angular.x = angular.x == 0 ? 0.0 : angular.x;
  angular.y = angular.y == 0 ? 0.0 : angular.y;
  angular.z = angular.z == 0 ? 0.0 : angular.z;
  if (!valid_attitude(integrated->orientation, angular))
    return std::unexpected{VacuumDynamicsError::unsafe_arithmetic};
  const auto& allocation = plan->allocation;
  return VacuumAttitudeResult{
      integrated->orientation,
      angular,
      {plan->requested, subtract(allocation.net, plan->requested),
       allocation.net, allocation.positive, allocation.negative,
       integrated->angular_impulse, assistance, allocation.saturated}};
}

namespace {
auto advance_vacuum_dynamics_impl(
    const RigidBodyWorldContext& context, RigidBodyState& state,
    const VacuumIntent& intent, bool lateral_damping, SimulationSeconds step,
    const GravityParameters* gravity = nullptr,
    CentralBodyActuation* central_report = nullptr,
    const AtmosphereParameters* atmosphere = nullptr,
    AtmosphericFlightActuation* atmospheric_report = nullptr)
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
  if (state.frame.kind != (gravity == nullptr
                               ? RigidFrameKind::system_inertial
                               : RigidFrameKind::planet_relative_inertial))
    return std::unexpected{VacuumDynamicsError::unsupported_coordinate_frame};
  if (!valid_fraction(intent.positive_translation) ||
      !valid_fraction(intent.negative_translation) ||
      !valid_fraction(intent.positive_rotation) ||
      !valid_fraction(intent.negative_rotation))
    return std::unexpected{VacuumDynamicsError::invalid_intent};

  const auto& properties = frame->properties;
  const auto positive_limit = vector(properties.positive_force_newtons);
  const auto negative_limit = vector(properties.negative_force_newtons);
  const auto inertia = vector(properties.principal_inertia_kg_m2);
  const double mass = properties.dry_mass_kg;
  const double dt = step.count();
  const auto positive_force =
      multiply(intent.positive_translation, positive_limit);
  const auto negative_force =
      multiply(intent.negative_translation, negative_limit);
  VacuumActuation report;
  report.assistance = intent.assistance;
  report.requested_force_newtons = subtract(positive_force, negative_force);
  auto desired_force = report.requested_force_newtons;
  const auto angular = state.angular_velocity_radians_per_second;
  if (intent.assistance && lateral_damping) {
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
  }
  if (!finite(desired_force))
    return std::unexpected{VacuumDynamicsError::unsafe_arithmetic};
  const auto plan =
      torque_plan(properties, angular, intent.positive_rotation,
                  intent.negative_rotation, intent.assistance, dt);
  if (!plan) return std::unexpected{plan.error()};
  const auto force = allocate(positive_force, negative_force, desired_force,
                              positive_limit, negative_limit);
  const auto& torque = plan->allocation;
  report.requested_torque_newton_metres = plan->requested;
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
  const auto integrated =
      integrate(initial, inertia, mass, force.net, torque.net, dt, gravity,
                atmosphere, &intent);
  if (!integrated) return std::unexpected{integrated.error()};
  auto candidate = state;
  candidate.position_metres = integrated->position;
  candidate.linear_velocity_metres_per_second = integrated->velocity;
  candidate.orientation = integrated->orientation;
  candidate.angular_velocity_radians_per_second = integrated->angular;
  ++candidate.tick;
  if (gravity != nullptr &&
      !gravity_acceleration(*gravity, candidate.position_metres))
    return std::unexpected{VacuumDynamicsError::unsafe_arithmetic};
  const auto canonical = canonicalize_rigid_body_state(context, candidate);
  if (!canonical)
    return std::unexpected{VacuumDynamicsError::unsafe_arithmetic};
  report.world_linear_impulse_newton_seconds = integrated->linear_impulse;
  report.world_angular_impulse_newton_metre_seconds =
      integrated->angular_impulse;
  if (central_report != nullptr) {
    central_report->gravity_impulse_newton_seconds =
        integrated->gravity_impulse;
    central_report->total_linear_impulse_newton_seconds =
        integrated->total_linear_impulse;
  }
  if (atmospheric_report != nullptr) {
    atmospheric_report->aerodynamic_linear_impulse_newton_seconds =
        integrated->aerodynamic_impulse;
    atmospheric_report->aerodynamic_angular_impulse_newton_metre_seconds =
        integrated->aerodynamic_angular_impulse;
  }
  state = *canonical;
  return report;
}
} // namespace

auto advance_vacuum_dynamics(const RigidBodyWorldContext& context,
                             RigidBodyState& state, const VacuumIntent& intent,
                             SimulationSeconds step)
    -> std::expected<VacuumActuation, VacuumDynamicsError> {
  return advance_vacuum_dynamics_impl(context, state, intent, true, step);
}

auto advance_vacuum_dynamics(const RigidBodyWorldContext& context,
                             RigidBodyState& state, const VacuumIntent& intent,
                             VacuumDynamicsRecipe recipe,
                             SimulationSeconds step)
    -> std::expected<VacuumActuation, VacuumDynamicsError> {
  if (recipe.version != kVacuumDynamicsVersion &&
      recipe.version != kCoastingVacuumDynamicsVersion)
    return std::unexpected{VacuumDynamicsError::unsupported_version};
  return advance_vacuum_dynamics_impl(
      context, state, intent, recipe.version == kVacuumDynamicsVersion, step);
}

auto evaluate_central_body_gravity(const RigidBodyWorldContext& context,
                                   const RigidBodyState& state,
                                   CentralBodyDynamicsRecipe recipe)
    -> std::expected<CentralBodyGravity, CentralBodyError> {
  const auto parameters = gravity_parameters(context, state, recipe);
  if (!parameters) return std::unexpected{parameters.error()};
  return gravity_sample(*parameters, state.position_metres);
}

auto advance_central_body_dynamics(const RigidBodyWorldContext& context,
                                   RigidBodyState& state,
                                   const VacuumIntent& intent,
                                   CentralBodyDynamicsRecipe recipe,
                                   SimulationSeconds step)
    -> std::expected<CentralBodyActuation, CentralBodyError> {
  const auto parameters = gravity_parameters(context, state, recipe);
  if (!parameters) return std::unexpected{parameters.error()};
  const auto initial_gravity =
      gravity_sample(*parameters, state.position_metres);
  if (!initial_gravity) return std::unexpected{initial_gravity.error()};
  CentralBodyActuation result;
  result.initial_gravity = *initial_gravity;
  const auto propulsion = advance_vacuum_dynamics_impl(
      context, state, intent, false, step, &*parameters, &result);
  if (!propulsion)
    return std::unexpected{CentralBodyError{
        CentralBodyErrorCode::dynamics_failure, propulsion.error()}};
  result.propulsion = *propulsion;
  return result;
}

auto evaluate_atmospheric_flight(const RigidBodyWorldContext& context,
                                 const RigidBodyState& state,
                                 const VacuumIntent& intent,
                                 const PhysicalPlanetRotationRecipe& rotation,
                                 AtmosphericFlightRecipe recipe,
                                 CentralBodyDynamicsRecipe dynamics)
    -> std::expected<AtmosphericFlightSample, AtmosphericFlightError> {
  const auto parameters =
      atmosphere_parameters(context, state, rotation, recipe, dynamics);
  if (!parameters) return std::unexpected{parameters.error()};
  return atmosphere_sample(*parameters, state.position_metres,
                           state.linear_velocity_metres_per_second,
                           state.orientation,
                           state.angular_velocity_radians_per_second, intent);
}

auto advance_atmospheric_flight(const RigidBodyWorldContext& context,
                                RigidBodyState& state,
                                const VacuumIntent& intent,
                                const PhysicalPlanetRotationRecipe& rotation,
                                AtmosphericFlightRecipe recipe,
                                CentralBodyDynamicsRecipe dynamics,
                                SimulationSeconds step)
    -> std::expected<AtmosphericFlightActuation, AtmosphericFlightError> {
  using Code = AtmosphericFlightErrorCode;
  const auto atmosphere =
      atmosphere_parameters(context, state, rotation, recipe, dynamics);
  if (!atmosphere) return std::unexpected{atmosphere.error()};
  const auto gravity = gravity_parameters(context, state, dynamics);
  if (!gravity)
    return std::unexpected{
        AtmosphericFlightError{Code::gravity_failure, gravity.error(), {}, {}}};
  const auto sample = atmosphere_sample(
      *atmosphere, state.position_metres,
      state.linear_velocity_metres_per_second, state.orientation,
      state.angular_velocity_radians_per_second, intent);
  if (!sample) return std::unexpected{sample.error()};
  AtmosphericFlightActuation report;
  report.initial = *sample;
  const auto initial_gravity = gravity_sample(*gravity, state.position_metres);
  if (!initial_gravity)
    return std::unexpected{AtmosphericFlightError{
        Code::gravity_failure, initial_gravity.error(), {}, {}}};
  report.central.initial_gravity = *initial_gravity;
  auto candidate = state;
  const auto propulsion = advance_vacuum_dynamics_impl(
      context, candidate, intent, false, step, &*gravity, &report.central,
      &*atmosphere, &report);
  if (!propulsion)
    return std::unexpected{AtmosphericFlightError{
        Code::dynamics_failure, {}, {}, propulsion.error()}};
  report.central.propulsion = *propulsion;
  const auto after = atmosphere_sample(
      *atmosphere, candidate.position_metres,
      candidate.linear_velocity_metres_per_second, candidate.orientation,
      candidate.angular_velocity_radians_per_second, intent);
  if (!after) return std::unexpected{after.error()};
  report.after = *after;
  const auto observed = evaluate_orbital_telemetry(
      context, candidate, rotation, {1, atmosphere->edge}, dynamics);
  if (!observed)
    return std::unexpected{
        AtmosphericFlightError{Code::observation_failure, {}, {}, {}}};
  report.observation_after = *observed;
  state = candidate;
  return report;
}

} // namespace apsis_drift
