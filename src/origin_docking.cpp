#include "apsis_drift/origin_docking.hpp"

#include <algorithm>
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
auto scale(V a, double s) -> V {
  return {a.x * s, a.y * s, a.z * s};
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
auto norm2(RigidOrientation q) -> double {
  return ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
}
auto rotate(RigidOrientation q, V v) -> V {
  // Project using the accepted unit-quaternion matrix, including its norm;
  // never normalize the stored state or substitute a heading-only attitude.
  const auto n = norm2(q);
  return {(1 - 2 * (q.y * q.y + q.z * q.z) / n) * v.x +
              2 * (q.x * q.y - q.w * q.z) / n * v.y +
              2 * (q.x * q.z + q.w * q.y) / n * v.z,
          2 * (q.x * q.y + q.w * q.z) / n * v.x +
              (1 - 2 * (q.x * q.x + q.z * q.z) / n) * v.y +
              2 * (q.y * q.z - q.w * q.x) / n * v.z,
          2 * (q.x * q.z - q.w * q.y) / n * v.x +
              2 * (q.y * q.z + q.w * q.x) / n * v.y +
              (1 - 2 * (q.x * q.x + q.y * q.y) / n) * v.z};
}
auto inside(V p, const StationBounds& box) -> bool {
  return p.x >= box.minimum_metres.x && p.x <= box.maximum_metres.x &&
         p.y >= box.minimum_metres.y && p.y <= box.maximum_metres.y &&
         p.z >= box.minimum_metres.z && p.z <= box.maximum_metres.z;
}
auto port_valid(const OriginStationDescriptor& station,
                const OriginStationGeometry& geometry, OriginPortId port)
    -> std::expected<void, OriginDockError> {
  if (!validate_origin_station_geometry(station, geometry))
    return std::unexpected{OriginDockError::invalid_geometry};
  if (port.station != station.id || port.ordinal < 1 ||
      port.ordinal > geometry.ports.size())
    return std::unexpected{OriginDockError::unknown_port};
  return {};
}
auto source_reference(const PhysicalLocalSystem& system,
                      const OriginStationDescriptor& station,
                      const OriginPortPose& pose,
                      const RigidCoordinateFrame& frame)
    -> std::expected<RigidBodyState, OriginDockError> {
  if (frame.kind == RigidFrameKind::station_relative_inertial)
    return pose.station_relative;
  if (frame.kind == RigidFrameKind::system_inertial)
    return pose.system_inertial;
  if (frame.kind != RigidFrameKind::planet_relative_inertial)
    return std::unexpected{OriginDockError::unsupported_frame};
  if (frame == pose.planet_relative.frame) return pose.planet_relative;
  const RigidBodyWorldContext context{system, &station};
  const auto reference = reframe_rigid_body(context, pose.system_inertial,
                                            {frame, pose.system_inertial.tick});
  if (!reference) return std::unexpected{OriginDockError::handoff_failure};
  return *reference;
}
auto hull_inside(V local_origin, RigidOrientation orientation,
                 const CraftFrameProperties& p, const StationBounds& box)
    -> bool {
  for (unsigned mask = 0; mask < 8; ++mask) {
    const V corner{
        ((mask & 1U) != 0 ? p.hull_max_mm[0] : p.hull_min_mm[0]) / 1000.0,
        ((mask & 2U) != 0 ? p.hull_max_mm[1] : p.hull_min_mm[1]) / 1000.0,
        ((mask & 4U) != 0 ? p.hull_max_mm[2] : p.hull_min_mm[2]) / 1000.0};
    if (!inside(add(local_origin, rotate(orientation, corner)), box))
      return false;
  }
  return true;
}
} // namespace

auto assess_origin_dock(const PhysicalLocalSystem& system,
                        const OriginStationDescriptor& station,
                        const OriginStationGeometry& geometry,
                        OriginPortId port, const RigidBodyState& source)
    -> std::expected<OriginDockAssessment, OriginDockError> {
  if (const auto valid = port_valid(station, geometry, port); !valid)
    return std::unexpected{valid.error()};
  const RigidBodyWorldContext context{system, &station};
  if (!validate_rigid_body_state(context, source))
    return std::unexpected{OriginDockError::invalid_state};
  const auto pose =
      resolve_origin_port_pose(system, station, geometry, port, source.tick);
  if (!pose) return std::unexpected{OriginDockError::invalid_result};
  const auto reference = source_reference(system, station, *pose, source.frame);
  if (!reference) return std::unexpected{reference.error()};
  // Compare with the same-tick port reference in the source's validated
  // system-aligned frame. This retains exact coincidence/co-motion and avoids
  // subtracting large global origins twice. These are query error vectors,
  // never a new authoritative planet/station handoff or simulation state.
  const auto displacement =
      sub(source.position_metres, reference->position_metres);
  const auto relative_velocity =
      sub(source.linear_velocity_metres_per_second,
          reference->linear_velocity_metres_per_second);
  const auto local_origin =
      add(pose->station_relative.position_metres, displacement);
  const auto& p = geometry.ports[port.ordinal - 1];
  const auto frame = resolve_craft_frame(source.craft);
  if (!frame) return std::unexpected{OriginDockError::invalid_state};
  OriginDockAssessment result;
  result.port = port;
  result.tick = source.tick;
  const auto offset = rotate(source.orientation, p.craft_collar_metres);
  const auto nominal_offset =
      rotate(p.docked_craft_orientation, p.craft_collar_metres);
  const auto delta = add(displacement, sub(offset, nominal_offset));
  result.collar_station_metres = add(p.collar_position_metres, delta);
  result.collar_velocity_station_metres_per_second =
      add(relative_velocity,
          cross(rotate(source.orientation,
                       source.angular_velocity_radians_per_second),
                offset));

  result.separation_metres = length(delta);
  result.outward_separation_metres = dot(delta, p.outward_normal);
  result.lateral_separation_metres = length(
      sub(delta, scale(p.outward_normal, result.outward_separation_metres)));
  const auto& q = source.orientation;
  const auto& r = p.docked_craft_orientation;
  const double orientation_dot =
      ((q.w * r.w + q.x * r.x) + q.y * r.y) + q.z * r.z;
  const double cosine = std::clamp(
      std::abs(orientation_dot) / std::sqrt(norm2(q) * norm2(r)), 0.0, 1.0);
  result.alignment_radians = 2 * std::acos(cosine);
  const double outward_speed =
      dot(result.collar_velocity_station_metres_per_second, p.outward_normal);
  result.inward_speed_metres_per_second = -outward_speed;
  result.lateral_speed_metres_per_second =
      length(sub(result.collar_velocity_station_metres_per_second,
                 scale(p.outward_normal, outward_speed)));
  result.angular_speed_radians_per_second =
      length(source.angular_velocity_radians_per_second);
  result.hull_inside_reservation =
      hull_inside(local_origin, source.orientation, frame->properties,
                  p.approach_reservation);
  using D = OriginDockDecision;
  if (source.craft != wayfarer_frame().recipe)
    result.decision = D::incompatible_craft;
  else if (result.outward_separation_metres < 0)
    result.decision = D::wrong_side;
  else if (cosine < std::cos(p.capture.alignment_radians * .5))
    result.decision = D::wrong_attitude;
  else if (!result.hull_inside_reservation)
    result.decision = D::outside_reservation;
  else if (result.separation_metres > p.capture.separation_metres)
    result.decision = D::too_far;
  else if (result.inward_speed_metres_per_second < 0)
    result.decision = D::retreating;
  else if (result.inward_speed_metres_per_second >
           p.capture.inward_speed_metres_per_second)
    result.decision = D::excessive_closure;
  else if (result.lateral_speed_metres_per_second >
           p.capture.lateral_speed_metres_per_second)
    result.decision = D::excessive_lateral_motion;
  else if (result.angular_speed_radians_per_second >
           p.capture.angular_speed_radians_per_second)
    result.decision = D::excessive_angular_motion;
  return result;
}

auto capture_origin_port(const PhysicalLocalSystem& system,
                         const OriginStationDescriptor& station,
                         const OriginStationGeometry& geometry,
                         OriginPortId port, const RigidBodyState& state)
    -> std::expected<OriginDockConstraint, OriginDockError> {
  const auto result =
      assess_origin_dock(system, station, geometry, port, state);
  if (!result) return std::unexpected{result.error()};
  if (result->decision != OriginDockDecision::capture_ready)
    return std::unexpected{OriginDockError::capture_refused};
  return OriginDockConstraint{geometry.version, port, state.craft, state.tick};
}

auto resolve_origin_docked_pose(const PhysicalLocalSystem& system,
                                const OriginStationDescriptor& station,
                                const OriginStationGeometry& geometry,
                                const OriginDockConstraint& constraint)
    -> std::expected<OriginPortPose, OriginDockError> {
  if (const auto valid = port_valid(station, geometry, constraint.port); !valid)
    return std::unexpected{valid.error()};
  if (constraint.geometry_version != geometry.version ||
      constraint.craft != wayfarer_frame().recipe)
    return std::unexpected{OriginDockError::invalid_constraint};
  auto pose = resolve_origin_port_pose(system, station, geometry,
                                       constraint.port, constraint.tick);
  if (!pose) return std::unexpected{OriginDockError::invalid_result};
  for (auto* state : {&pose->station_relative, &pose->system_inertial,
                      &pose->planet_relative})
    state->craft = constraint.craft;
  return *pose;
}

auto release_origin_port(const PhysicalLocalSystem& system,
                         const OriginStationDescriptor& station,
                         const OriginStationGeometry& geometry,
                         const OriginDockConstraint& constraint)
    -> std::expected<RigidBodyState, OriginDockError> {
  const auto pose =
      resolve_origin_docked_pose(system, station, geometry, constraint);
  if (!pose) return std::unexpected{pose.error()};
  return pose->planet_relative;
}
} // namespace apsis_drift
