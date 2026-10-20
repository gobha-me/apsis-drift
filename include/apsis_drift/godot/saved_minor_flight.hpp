#pragma once

#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/physical_minor_rotation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

namespace apsis_drift::godot_spike {
inline constexpr std::uint32_t kSavedMinorFlightProjectionVersion{1};
struct MinorSphericalPose {
  double latitude_radians{}, longitude_radians{}, altitude_metres{};
  friend auto operator==(const MinorSphericalPose&, const MinorSphericalPose&)
      -> bool = default;
};
struct MinorLocalTangentFrame {
  RigidVector3 origin, east, north, up;
  friend auto operator==(const MinorLocalTangentFrame&,
                         const MinorLocalTangentFrame&) -> bool = default;
};
struct SavedMinorFlightProjection {
  std::uint32_t version{kSavedMinorFlightProjectionVersion};
  PhysicalMinorDescriptor minor;
  RigidBodyState body;
  RigidCoordinateFrame fixed_frame;
  PhysicalMinorRotationGeometry rotation;
  MinorSphericalPose pose;
  MinorLocalTangentFrame tangent;
  // Identity-free east/north/up components, as in the existing flight views.
  std::array<LocalPositionMetres, 3> body_axes, system_axes;
  LocalPositionMetres star_direction;
  // Godot east/up/-north translation and columns for one body-fixed mesh.
  RigidVector3 minor_center;
  std::array<RigidVector3, 3> minor_fixed_basis;
  friend auto operator==(const SavedMinorFlightProjection&,
                         const SavedMinorFlightProjection&) -> bool = default;
};
namespace saved_minor_detail {
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
inline auto local(const MinorLocalTangentFrame& frame, RigidVector3 v)
    -> LocalPositionMetres {
  const auto dot = [](RigidVector3 a, RigidVector3 b) {
    return (a.x * b.x + a.y * b.y) + a.z * b.z;
  };
  const LocalPositionMetres result{dot(v, frame.east), dot(v, frame.north),
                                   dot(v, frame.up)};
  if (!finite(v) || !std::isfinite(result.east) ||
      !std::isfinite(result.north) || !std::isfinite(result.up))
    throw std::invalid_argument("Nonfinite minor projection vector");
  return result;
}
} // namespace saved_minor_detail

// Vector operations do not translate points. Godot axes are east/up/-north.
inline auto minor_fixed_vector_to_local(const SavedMinorFlightProjection& view,
                                        RigidVector3 v) -> RigidVector3 {
  const auto local = saved_minor_detail::local(view.tangent, v);
  return {local.east, local.up, -local.north};
}
inline auto system_vector_to_minor_local(const SavedMinorFlightProjection& view,
                                         RigidVector3 v) -> RigidVector3 {
  return minor_fixed_vector_to_local(
      view, saved_minor_detail::rotate(
                saved_minor_detail::inverse(view.rotation.fixed_to_system), v));
}
inline auto minor_fixed_point_to_local(const SavedMinorFlightProjection& view,
                                       RigidVector3 point) -> RigidVector3 {
  return minor_fixed_vector_to_local(view, {point.x - view.tangent.origin.x,
                                            point.y - view.tangent.origin.y,
                                            point.z - view.tangent.origin.z});
}

// Pure projection only: no stand-off/landing decision, clock or terrain query.
inline auto project_saved_minor_flight(const PhysicalMinorDescriptor& minor,
                                       const RigidBodyState& body)
    -> SavedMinorFlightProjection {
  using namespace saved_minor_detail;
  if (!validate_physical_minor_descriptor(minor))
    throw std::invalid_argument("Invalid saved minor projection owner");
  const RigidBodyWorldContext context{minor};
  if (!validate_rigid_body_state(context, body) ||
      body.frame.kind != RigidFrameKind::body_relative_inertial ||
      body.frame.body != minor.body)
    throw std::invalid_argument(
        "Saved minor projection requires owned canonical frame5");
  auto rotation = resolve_physical_minor_rotation(minor, body.tick);
  if (!rotation) throw std::invalid_argument("Saved minor rotation refused");
  const auto inverse_rotation = inverse(rotation->fixed_to_system);
  const auto observer = rotate(inverse_rotation, body.position_metres);
  const double horizontal = std::hypot(observer.x, observer.y);
  const double radial = std::hypot(observer.x, observer.y, observer.z);
  if (!finite(observer) || !std::isfinite(radial) || radial <= 0)
    throw std::invalid_argument(
        "Saved minor observer is nonfinite or centered");
  const MinorSphericalPose pose{
      std::atan2(observer.z, horizontal),
      horizontal == 0 ? 0 : std::atan2(observer.y, observer.x),
      radial - minor.radius_metres};
  MinorLocalTangentFrame tangent;
  tangent.origin = observer;
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
  rotation = resolve_physical_minor_rotation(minor, body.tick, observer);
  if (!rotation)
    throw std::invalid_argument("Saved minor observer/star geometry refused");
  const auto local_axis = [&](RigidVector3 axis) {
    return local(tangent, rotate(inverse_rotation, axis));
  };
  const auto godot_axis = [&](RigidVector3 fixed) -> RigidVector3 {
    const auto v = local(tangent, fixed);
    return {v.east, v.up, -v.north};
  };
  return {
      kSavedMinorFlightProjectionVersion,
      minor,
      body,
      {RigidFrameKind::body_fixed, context.system.id, {}, {}, minor.body},
      *rotation,
      pose,
      tangent,
      {local_axis(rotate(body.orientation, {1, 0, 0})),
       local_axis(rotate(body.orientation, {0, 1, 0})),
       local_axis(rotate(body.orientation, {0, 0, 1}))},
      {local_axis({1, 0, 0}), local_axis({0, 1, 0}), local_axis({0, 0, 1})},
      local(tangent, rotation->observer_to_star_fixed),
      godot_axis({-observer.x, -observer.y, -observer.z}),
      {godot_axis({1, 0, 0}), godot_axis({0, 1, 0}), godot_axis({0, 0, 1})}};
}
inline auto project_saved_minor_flight(
    const NativeFreedomFlightSession& session) -> SavedMinorFlightProjection {
  if (!session.minor())
    throw std::invalid_argument("Flight session has no actual minor owner");
  const auto body = session.relative_state();
  if (!body) throw std::invalid_argument("Saved minor relative state refused");
  return project_saved_minor_flight(*session.minor(), *body);
}

inline constexpr std::uint32_t kMinorProxyMeshVersion{1};
inline constexpr std::uint32_t kMinorProxyMaximumLatitudeIntervals{32};
inline constexpr std::uint32_t kMinorProxyMaximumLongitudeIntervals{64};
struct MinorProxyMeshDimensions {
  std::uint32_t latitude_intervals{32}, longitude_intervals{64};
};
struct MinorProxyMeshCounts {
  std::size_t vertices{}, indices{};
  friend auto operator==(const MinorProxyMeshCounts&,
                         const MinorProxyMeshCounts&) -> bool = default;
};
struct MinorProxyMeshVertex {
  RigidVector3 position_metres, normal;
  friend auto operator==(const MinorProxyMeshVertex&,
                         const MinorProxyMeshVertex&) -> bool = default;
};
enum class MinorProxyMeshError : std::uint8_t {
  invalid_dimensions,
  short_buffer,
  invalid_owner,
  unsafe_arithmetic,
};
inline auto minor_proxy_mesh_counts(MinorProxyMeshDimensions dimensions)
    -> std::expected<MinorProxyMeshCounts, MinorProxyMeshError> {
  const auto lat = dimensions.latitude_intervals;
  const auto lon = dimensions.longitude_intervals;
  if (lat < 2 || lat > kMinorProxyMaximumLatitudeIntervals || lon < 3 ||
      lon > kMinorProxyMaximumLongitudeIntervals)
    return std::unexpected(MinorProxyMeshError::invalid_dimensions);
  return MinorProxyMeshCounts{static_cast<std::size_t>(lat + 1) * (lon + 1),
                              static_cast<std::size_t>(6) * lon * (lat - 1)};
}

// Bounded inscribed spherical display proxy. Every vertex lies on the source
// radius, normals are source radial, and exterior faces wind Godot-clockwise
// (as in the existing Moon provider). Facets do not
// introduce a second collision/terrain surface or permit landing. Positions
// are genuine body-fixed XYZ metres. The snapshot's minor_center and
// minor_fixed_basis transform this unchanged mesh without per-tick uploads.
// All refusals preserve both complete caller buffers, including their tails.
inline auto fill_minor_proxy_mesh(const PhysicalMinorDescriptor& minor,
                                  MinorProxyMeshDimensions dimensions,
                                  std::span<MinorProxyMeshVertex> vertices,
                                  std::span<std::uint32_t> indices)
    -> std::expected<MinorProxyMeshCounts, MinorProxyMeshError> {
  const auto counts = minor_proxy_mesh_counts(dimensions);
  if (!counts) return std::unexpected(counts.error());
  if (vertices.size() < counts->vertices || indices.size() < counts->indices)
    return std::unexpected(MinorProxyMeshError::short_buffer);
  if (!validate_physical_minor_descriptor(minor))
    return std::unexpected(MinorProxyMeshError::invalid_owner);
  std::vector<MinorProxyMeshVertex> staged_vertices;
  std::vector<std::uint32_t> staged_indices;
  staged_vertices.reserve(counts->vertices);
  staged_indices.reserve(counts->indices);
  const auto lat = dimensions.latitude_intervals;
  const auto lon = dimensions.longitude_intervals;
  const double radius = minor.radius_metres;
  for (std::uint32_t row = 0; row <= lat; ++row) {
    const double latitude =
        std::numbers::pi * (static_cast<double>(row) / lat - .5);
    const double horizontal = row == 0 || row == lat ? 0 : std::cos(latitude);
    const double z = row == 0 ? -1 : row == lat ? 1 : std::sin(latitude);
    for (std::uint32_t column = 0; column <= lon; ++column) {
      const double longitude = 2 * std::numbers::pi *
                               static_cast<double>(column == lon ? 0 : column) /
                               lon;
      const RigidVector3 radial{horizontal * std::cos(longitude),
                                horizontal * std::sin(longitude), z};
      const RigidVector3 position{radius * radial.x, radius * radial.y,
                                  radius * radial.z};
      const auto normal = radial;
      if (!saved_minor_detail::finite(position) ||
          !saved_minor_detail::finite(normal))
        return std::unexpected(MinorProxyMeshError::unsafe_arithmetic);
      staged_vertices.push_back({position, normal});
    }
  }
  for (std::uint32_t row = 0; row < lat; ++row) {
    for (std::uint32_t column = 0; column < lon; ++column) {
      const auto a = row * (lon + 1) + column, b = a + 1, c = a + lon + 1,
                 d = c + 1;
      if (row != 0) staged_indices.insert(staged_indices.end(), {a, c, b});
      if (row + 1 != lat)
        staged_indices.insert(staged_indices.end(), {b, c, d});
    }
  }
  std::ranges::copy(staged_vertices, vertices.begin());
  std::ranges::copy(staged_indices, indices.begin());
  return *counts;
}
} // namespace apsis_drift::godot_spike
