#include "apsis_drift/touchdown.hpp"

#include <algorithm>
#include <cmath>

namespace apsis_drift {
namespace {
using V = RigidVector3;
using Q = RigidOrientation;

// Version 1 supports the registered 12-degree envelope only. A new authored
// slope rating needs an explicit threshold policy, not a host acos conversion.
constexpr double kMinimumSlopeCosine{0.9781476007338057};
constexpr double kStandardGravityMetresPerSecondSquared{9.80665};

auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto subtract(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(V v, double factor) -> V {
  return {v.x * factor, v.y * factor, v.z * factor};
}
auto dot(V a, V b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto finite(V v) -> bool {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
auto rotate(Q q, V v) -> V {
  // q is validated, but its norm need not be exactly 1 in binary64. This
  // unit-equivalent rotation never renormalizes or mutates authoritative q.
  const V imaginary{q.x, q.y, q.z};
  const double squared_norm = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  return add(v, scale(add(scale(cross(imaginary, v), q.w),
                          cross(imaginary, cross(imaginary, v))),
                      2.0 / squared_norm));
}
auto mark(std::uint32_t& bits, TouchdownMargin margin, bool failed) -> void {
  if (failed) {
    bits |= static_cast<std::uint32_t>(margin);
  }
}
auto valid_gap(double value) -> bool {
  return std::isfinite(value) &&
         std::abs(value) <= kRigidBodyMaximumPositionMetres;
}
auto valid_surface(TouchdownSurface surface) -> bool {
  return surface == TouchdownSurface::solid ||
         surface == TouchdownSurface::water ||
         surface == TouchdownSurface::unsupported;
}
auto positive_zero(double value) -> double {
  return value == 0.0 ? 0.0 : value;
}
} // namespace

auto assess_touchdown_envelope(const RigidBodyWorldContext& context,
                               const RigidBodyState& state,
                               const TouchdownObservations& observations)
    -> std::expected<TouchdownAssessment, TouchdownError> {
  if (!validate_rigid_body_state(context, state)) {
    return std::unexpected(TouchdownError::invalid_state);
  }
  if (state.frame.kind != RigidFrameKind::planet_fixed) {
    return std::unexpected(TouchdownError::unsupported_frame);
  }
  if (observations.planet != *state.frame.planet ||
      observations.tick != state.tick) {
    return std::unexpected(TouchdownError::observation_identity_mismatch);
  }
  const auto frame = resolve_craft_frame(state.craft);
  if (!frame ||
      !supports_operation(frame->properties, CraftOperation::terrain_contact) ||
      frame->properties.max_slope_millidegrees != 12000) {
    return std::unexpected(TouchdownError::invalid_craft_frame);
  }
  const auto& craft = frame->properties;
  if (observations.support_count != craft.support_count ||
      observations.support_count < 3 || observations.support_count > 4) {
    return std::unexpected(TouchdownError::invalid_support_count);
  }
  // Validate the complete bounded observation before assessing any margin.
  std::array<V, 4> normals{};
  for (std::size_t index = 0; index < observations.supports.size(); ++index) {
    const auto& pad = observations.supports[index];
    if (index >= observations.support_count) {
      if (pad != TouchdownPadObservation{}) {
        return std::unexpected(TouchdownError::invalid_observation);
      }
      continue;
    }
    if (pad.support_index != index) {
      return std::unexpected(TouchdownError::invalid_support_order);
    }
    if (!valid_gap(pad.minimum_gap_metres) ||
        !valid_gap(pad.maximum_gap_metres) ||
        pad.minimum_gap_metres > pad.maximum_gap_metres ||
        !valid_surface(pad.surface)) {
      return std::unexpected(TouchdownError::invalid_observation);
    }
    const double norm_squared = dot(pad.outward_normal, pad.outward_normal);
    if (!finite(pad.outward_normal) || !std::isfinite(norm_squared) ||
        std::abs(norm_squared - 1.0) >
            kRigidBodyOrientationSquaredNormTolerance) {
      return std::unexpected(TouchdownError::invalid_normal);
    }
    const double length = std::sqrt(norm_squared);
    normals[index] = {pad.outward_normal.x / length,
                      pad.outward_normal.y / length,
                      pad.outward_normal.z / length};
  }
  const double radius_squared =
      dot(state.position_metres, state.position_metres);
  if (!std::isfinite(radius_squared) || radius_squared <= 0.0) {
    return std::unexpected(TouchdownError::unsafe_arithmetic);
  }
  const double radius = std::sqrt(radius_squared);
  // Divide components directly: reciprocal multiplication can turn an exact
  // axis unit vector into 1-ULP and falsely exceed the inclusive slope bound.
  const V radial_up{state.position_metres.x / radius,
                    state.position_metres.y / radius,
                    state.position_metres.z / radius};
  const V body_up = rotate(state.orientation, V{0.0, 1.0, 0.0});
  if (!finite(radial_up) || !finite(body_up)) {
    return std::unexpected(TouchdownError::unsafe_arithmetic);
  }
  for (std::size_t index = 0; index < observations.support_count; ++index) {
    if (dot(normals[index], radial_up) <= 0.0) {
      return std::unexpected(TouchdownError::invalid_normal);
    }
  }
  const auto planet =
      find_local_system_planet(context.system, observations.planet);
  if (!planet) {
    return std::unexpected(TouchdownError::invalid_state);
  }
  const auto& descriptor = (*planet)->descriptor;
  const double gravity = static_cast<double>(descriptor.surface_gravity.value) *
                         kStandardGravityMetresPerSecondSquared / 1000.0;
  const double weight = static_cast<double>(craft.dry_mass_kg) * gravity;
  const double angular_limit =
      static_cast<double>(craft.max_touchdown_angular_milliradians_per_second) /
      1000.0;
  const double descent_limit =
      static_cast<double>(craft.max_touchdown_vertical_mm_per_second) / 1000.0;
  const double tangential_limit =
      static_cast<double>(craft.max_touchdown_horizontal_mm_per_second) /
      1000.0;
  TouchdownAssessment result;
  result.support_count = observations.support_count;
  mark(result.failed_margins, TouchdownMargin::gear_not_deployed,
       !observations.gear_deployed);
  mark(result.failed_margins, TouchdownMargin::not_upright,
       dot(body_up, radial_up) <= 0.0);
  mark(result.failed_margins, TouchdownMargin::angular_speed_exceeded,
       dot(state.angular_velocity_radians_per_second,
           state.angular_velocity_radians_per_second) >
           angular_limit * angular_limit);
  mark(result.failed_margins, TouchdownMargin::gravity_rating_exceeded,
       gravity > static_cast<double>(craft.max_surface_gravity_mm_per_second2) /
                     1000.0);
  mark(result.failed_margins, TouchdownMargin::pressure_rating_exceeded,
       descriptor.atmosphere_pressure.value > craft.max_pressure_millibars);
  bool any_contact = false;
  for (std::size_t index = 0; index < observations.support_count; ++index) {
    const auto& observed = observations.supports[index];
    const auto& support = craft.supports[index];
    const V normal = normals[index];
    auto& assessed = result.supports[index];
    const V offset{static_cast<double>(support.contact_mm[0] -
                                       craft.center_of_mass_mm[0]) /
                       1000.0,
                   static_cast<double>(support.contact_mm[1] -
                                       craft.center_of_mass_mm[1]) /
                       1000.0,
                   static_cast<double>(support.contact_mm[2] -
                                       craft.center_of_mass_mm[2]) /
                       1000.0};
    const V velocity =
        add(state.linear_velocity_metres_per_second,
            rotate(state.orientation,
                   cross(state.angular_velocity_radians_per_second, offset)));
    const double normal_velocity = dot(velocity, normal);
    const V tangent = subtract(velocity, scale(normal, normal_velocity));
    const double tangent_squared = dot(tangent, tangent);
    const double stroke_projection = dot(body_up, normal);
    const double capacity = static_cast<double>(support.stroke_mm) / 1000.0 *
                            std::max(stroke_projection, 0.0);
    if (!finite(velocity) || !std::isfinite(normal_velocity) ||
        !std::isfinite(tangent_squared) || !std::isfinite(capacity)) {
      return std::unexpected(TouchdownError::unsafe_arithmetic);
    }
    assessed.normal_velocity_metres_per_second = positive_zero(normal_velocity);
    assessed.tangential_speed_metres_per_second = std::sqrt(tangent_squared);
    assessed.compression_capacity_metres = positive_zero(capacity);
    mark(assessed.failed_margins, TouchdownMargin::not_upright,
         stroke_projection <= 0.0);
    mark(assessed.failed_margins, TouchdownMargin::unsupported_material,
         observed.surface != TouchdownSurface::solid);
    mark(assessed.failed_margins, TouchdownMargin::unsupported_footprint,
         !observed.footprint_supported);
    mark(assessed.failed_margins, TouchdownMargin::separated_support,
         observed.maximum_gap_metres > 0.0);
    mark(assessed.failed_margins, TouchdownMargin::compression_exceeded,
         observed.minimum_gap_metres < -capacity);
    mark(assessed.failed_margins, TouchdownMargin::slope_exceeded,
         dot(normal, radial_up) < kMinimumSlopeCosine);
    mark(assessed.failed_margins, TouchdownMargin::separating_velocity,
         normal_velocity > 0.0);
    mark(assessed.failed_margins, TouchdownMargin::descent_speed_exceeded,
         normal_velocity < -descent_limit);
    mark(assessed.failed_margins, TouchdownMargin::tangential_speed_exceeded,
         tangent_squared > tangential_limit * tangential_limit);
    // Conservative bearing gate: any one pad must tolerate the full static
    // dry weight. This is not an equal-share assumption or a reaction solver.
    mark(assessed.failed_margins, TouchdownMargin::static_support_load_exceeded,
         weight > static_cast<double>(support.rated_load_newtons));
    result.failed_margins |= assessed.failed_margins;
    any_contact = any_contact || observed.minimum_gap_metres <= 0.0;
  }
  if (any_contact) {
    result.classification = result.failed_margins == 0
                                ? TouchdownClass::pad_contact_ready
                                : TouchdownClass::pad_contact_unsafe;
  }
  return result;
}

} // namespace apsis_drift
