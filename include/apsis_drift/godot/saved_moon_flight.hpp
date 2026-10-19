#pragma once

#include "apsis_drift/moon_surface_contact.hpp"
#include "apsis_drift/moon_terrain.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/physical_moon_rotation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>

namespace apsis_drift::godot_spike {
inline constexpr std::uint32_t kSavedMoonFlightProjectionVersion{1};
struct MoonGearClearanceReading {
  std::uint32_t version{1}, terrain_version{kMoonTerrainRecipeVersion};
  RigidCoordinateFrame frame;
  BodyId body;
  CraftFrameRecipe craft;
  SimulationTick tick{};
  std::uint64_t source_checksum{};
  double minimum_gap_metres{}, maximum_gap_metres{};
  friend auto operator==(const MoonGearClearanceReading&,
                         const MoonGearClearanceReading&) -> bool = default;
};
// One current observation of every deployed pad's certified finite top plane.
// Signed gaps enclose WHOLE footprints along their respective facet normals;
// negative is stroke overlap, never a hull/landing/readiness permission. The
// caller gates its actual seated/idle mode. No contact query belongs in the
// ordinary transform projection or a per-tile presentation operation.
inline auto project_moon_gear_clearance(
    const PhysicalMoonDescriptor& moon, const RigidBodyState& body,
    bool gear_deployed,
    std::uint32_t terrain_version = kMoonTerrainRecipeVersion)
    -> std::optional<MoonGearClearanceReading> {
  if (!gear_deployed) return std::nullopt;
  const auto snapshot =
      MoonContactSnapshot::create(moon, body, true, terrain_version);
  if (!snapshot) return std::nullopt;
  const auto query = snapshot->query();
  if (query.support_count != 3) return std::nullopt;
  double minimum = std::numeric_limits<double>::infinity();
  double maximum = -minimum;
  for (unsigned i = 0; i < query.support_count; ++i) {
    const auto& pad = query.supports[i];
    if (!pad.geometry || !pad.geometry->has_value() || !pad.support ||
        !pad.support->has_value())
      return std::nullopt;
    const auto& geometry = **pad.geometry;
    const auto& support = **pad.support;
    if (support.surface != TouchdownSurface::solid ||
        !support.footprint_supported ||
        !std::isfinite(geometry.minimum_gap_metres) ||
        !std::isfinite(geometry.maximum_gap_metres) ||
        geometry.minimum_gap_metres > geometry.maximum_gap_metres)
      return std::nullopt;
    minimum = std::min(minimum, geometry.minimum_gap_metres);
    maximum = std::max(maximum, geometry.maximum_gap_metres);
  }
  return MoonGearClearanceReading{1,
                                  terrain_version,
                                  body.frame,
                                  moon.body,
                                  body.craft,
                                  body.tick,
                                  query.provenance.original_state_checksum,
                                  minimum,
                                  maximum};
}
struct MoonSphericalPose {
  double latitude_radians{}, longitude_radians{}, altitude_metres{};
  friend auto operator==(const MoonSphericalPose&, const MoonSphericalPose&)
      -> bool = default;
};
struct MoonLocalTangentFrame {
  MoonFixedPositionMetres origin;
  RigidVector3 east, north, up;
  friend auto operator==(const MoonLocalTangentFrame&,
                         const MoonLocalTangentFrame&) -> bool = default;
};
struct SavedMoonFlightProjection {
  std::uint32_t version{kSavedMoonFlightProjectionVersion};
  PhysicalMoonDescriptor moon;
  RigidBodyState body;
  RigidCoordinateFrame fixed_frame;
  PhysicalMoonRotationGeometry rotation;
  MoonSphericalPose pose;
  MoonLocalTangentFrame tangent;
  // Identity-free east/north/up vectors, matching existing flight projection.
  std::array<LocalPositionMetres, 3> body_axes, system_axes;
  LocalPositionMetres star_direction;
  bool surface_observer{};
};
namespace saved_moon_detail {
inline auto finite(RigidVector3 v) -> bool {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline auto inverse(RigidOrientation q) -> RigidOrientation {
  return {q.w, -q.x, -q.y, -q.z};
}
inline auto rotate(RigidOrientation q, RigidVector3 v) -> RigidVector3 {
  const double n = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
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
inline auto local(const MoonLocalTangentFrame& frame, RigidVector3 v)
    -> LocalPositionMetres {
  const auto dot = [](RigidVector3 a, RigidVector3 b) {
    return (a.x * b.x + a.y * b.y) + a.z * b.z;
  };
  const LocalPositionMetres result{dot(v, frame.east), dot(v, frame.north),
                                   dot(v, frame.up)};
  if (!finite(v) || !std::isfinite(result.east) ||
      !std::isfinite(result.north) || !std::isfinite(result.up))
    throw std::invalid_argument("Nonfinite moon projection vector");
  return result;
}
} // namespace saved_moon_detail

// These vector-only helpers do not translate points or qualify a world. Output
// is Godot's right/up/back convention: east,up,-north. Observer subtraction,
// when needed, belongs to the caller's typed point operation.
inline auto moon_fixed_vector_to_local(const SavedMoonFlightProjection& view,
                                       RigidVector3 v) -> RigidVector3 {
  const auto local = saved_moon_detail::local(view.tangent, v);
  return {local.east, local.up, -local.north};
}
inline auto system_vector_to_moon_local(const SavedMoonFlightProjection& view,
                                        RigidVector3 v) -> RigidVector3 {
  return moon_fixed_vector_to_local(
      view, saved_moon_detail::rotate(
                saved_moon_detail::inverse(view.rotation.fixed_to_system), v));
}
// Pure display projection of an authenticated descriptor/state. A supplied
// scalar actor must name that exact moon-fixed frame. This checks projection
// inputs only, never grants terrain/landing/walking authority; the session
// overload supplies its already admitted actual actor and immutable owner.
inline auto project_saved_moon_flight(
    const PhysicalMoonDescriptor& moon, const RigidBodyState& body,
    const std::optional<PlanetSurfaceWalkerState>& actor = std::nullopt)
    -> SavedMoonFlightProjection {
  using namespace saved_moon_detail;
  if (!validate_physical_moon_descriptor(moon))
    throw std::invalid_argument("Invalid saved moon projection owner");
  const RigidBodyWorldContext context{moon};
  if (!validate_rigid_body_state(context, body) ||
      body.frame.kind != RigidFrameKind::body_relative_inertial ||
      body.frame.body != moon.body)
    throw std::invalid_argument(
        "Saved moon projection requires owned canonical frame5");
  const RigidCoordinateFrame fixed_frame{
      RigidFrameKind::body_fixed, context.system.id, {}, {}, moon.body};
  auto rotation = resolve_physical_moon_rotation(moon, body.tick);
  if (!rotation) throw std::invalid_argument("Saved moon rotation refused");
  const auto inverse_rotation = inverse(rotation->fixed_to_system);
  auto observer = rotate(inverse_rotation, body.position_metres);
  if (actor) {
    if (actor->version != kPlanetSurfaceWalkerVersion || actor->actor_id != 1 ||
        actor->frame != fixed_frame || !finite(actor->foot_position_metres) ||
        !finite(actor->velocity_metres_per_second) ||
        std::hypot(actor->velocity_metres_per_second.x,
                   actor->velocity_metres_per_second.y,
                   actor->velocity_metres_per_second.z) > 2.45 ||
        !std::isfinite(actor->heading_radians) ||
        std::abs(actor->heading_radians) > std::numbers::pi)
      throw std::invalid_argument(
          "Saved moon observer actor has a different owner or invalid state");
    observer = actor->foot_position_metres;
  }
  const double horizontal = std::hypot(observer.x, observer.y);
  const double radial = std::hypot(observer.x, observer.y, observer.z);
  if (!finite(observer) || !std::isfinite(radial) || radial <= 1)
    throw std::invalid_argument(
        "Saved moon observer is nonfinite or at its center");
  const MoonSphericalPose pose{
      std::atan2(observer.z, horizontal),
      horizontal == 0 ? 0 : std::atan2(observer.y, observer.x),
      radial - moon.radius_metres};
  MoonLocalTangentFrame tangent;
  tangent.origin = {observer.x, observer.y, observer.z};
  tangent.up = {observer.x / radial, observer.y / radial, observer.z / radial};
  if (horizontal == 0) {
    tangent.east = {0, 1, 0};
    tangent.north = {observer.z > 0 ? -1. : 1., 0, 0};
  } else {
    tangent.east = {-observer.y / horizontal, observer.x / horizontal, 0};
    tangent.north = {-(observer.z / radial) * (observer.x / horizontal),
                     -(observer.z / radial) * (observer.y / horizontal),
                     horizontal / radial};
  }
  rotation = resolve_physical_moon_rotation(moon, body.tick, observer);
  if (!rotation)
    throw std::invalid_argument("Saved moon observer/star geometry refused");
  const auto local_axis = [&](RigidVector3 system_axis) {
    return local(tangent, rotate(inverse_rotation, system_axis));
  };
  return {kSavedMoonFlightProjectionVersion,
          moon,
          body,
          fixed_frame,
          *rotation,
          pose,
          tangent,
          {local_axis(rotate(body.orientation, {1, 0, 0})),
           local_axis(rotate(body.orientation, {0, 1, 0})),
           local_axis(rotate(body.orientation, {0, 0, 1}))},
          {local_axis({1, 0, 0}), local_axis({0, 1, 0}), local_axis({0, 0, 1})},
          local(tangent, rotation->observer_to_star_fixed),
          actor.has_value()};
}
inline auto project_saved_moon_flight(const NativeFreedomFlightSession& session)
    -> SavedMoonFlightProjection {
  if (!session.moon())
    throw std::invalid_argument("Flight session has no actual moon owner");
  const auto body = session.relative_state();
  if (!body) throw std::invalid_argument("Saved moon relative state refused");
  return project_saved_moon_flight(*session.moon(), *body,
                                   session.surface_walker());
}
} // namespace apsis_drift::godot_spike
