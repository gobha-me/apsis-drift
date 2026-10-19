#pragma once
#include "apsis_drift/godot/contact_patch.hpp"
#include "apsis_drift/terrain_hull_clearance.hpp"
#include <algorithm>
#include <cmath>
namespace apsis_drift::detail::hull_numeric {
using V = RigidVector3;
inline auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline auto subtract(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline auto scale(V a, double s) -> V {
  return {a.x * s, a.y * s, a.z * s};
}
inline auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline auto rotate(RigidOrientation q, V v) -> V {
  const V imaginary{q.x, q.y, q.z};
  const auto norm2 = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  return add(v, scale(add(scale(cross(imaginary, v), q.w),
                          cross(imaginary, cross(imaginary, v))),
                      2 / norm2));
}
inline auto finite(V a) -> bool {
  return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z) &&
         std::max({std::abs(a.x), std::abs(a.y), std::abs(a.z)}) <= 1e8;
}
inline auto strictly_in_face(V p, CubeFace face) -> bool {
  const std::array<double, 3> axes{p.x, p.y, p.z};
  const auto index = static_cast<unsigned>(face) / 2;
  const auto sign = static_cast<unsigned>(face) % 2 == 0 ? 1.0 : -1.0;
  const auto primary = sign * axes[index];
  for (unsigned other = 0; other < 3; ++other)
    if (other != index && primary - std::abs(axes[other]) <=
                              godot_spike::kContactPatchAllowanceMetres)
      return false;
  return primary > 0;
}

// Exactly the existing eight-corner, positive-face, one-cell-halo bound.
// Caller authenticates state/radius and supplies its typed actual locator.
template <class Locate>
auto bound_cells(const RigidBodyState& state,
                 const CraftFrameProperties& properties, double radius,
                 std::uint64_t source_checksum, std::uint32_t terrain_policy,
                 Locate&& locate)
    -> std::expected<TerrainHullClearance, TerrainHullError> {
  if (std::abs(std::hypot(state.position_metres.x, state.position_metres.y,
                          state.position_metres.z) -
               radius) > godot_spike::kContactPatchMaximumDatumAltitudeMetres)
    return std::unexpected{TerrainHullError::outside_query_bounds};

  TerrainHullClearance result;
  result.terrain_policy = terrain_policy;
  result.source_checksum = source_checksum;
  result.tick = state.tick;
  result.minimum_cell = {262143, 262143};
  std::array<V, 8> corners;
  for (unsigned corner = 0; corner < 8; ++corner) {
    V offset;
    std::array<double, 3> components;
    for (unsigned axis = 0; axis < 3; ++axis) {
      const auto point = (corner & (1U << axis)) != 0
                             ? properties.hull_max_mm[axis]
                             : properties.hull_min_mm[axis];
      components[axis] =
          static_cast<double>(std::int64_t{point} -
                              properties.center_of_mass_mm[axis]) /
          1000;
    }
    offset = {components[0], components[1], components[2]};
    corners[corner] =
        add(state.position_metres, rotate(state.orientation, offset));
    const auto& p = corners[corner];
    if (!finite(p)) return std::unexpected{TerrainHullError::unsafe_arithmetic};
    const auto id = locate(p);
    if (!id) return std::unexpected{TerrainHullError::surface_query_failed};
    if (corner == 0) result.face = id->face;
    if (id->face != result.face || !strictly_in_face(p, result.face))
      return std::unexpected{TerrainHullError::face_boundary};
    const std::array<std::uint32_t, 2> cell{id->tile_x * 32 + id->cell_x,
                                            id->tile_y * 32 + id->cell_y};
    for (unsigned axis = 0; axis < 2; ++axis) {
      result.minimum_cell[axis] =
          std::min(result.minimum_cell[axis], cell[axis]);
      result.maximum_cell[axis] =
          std::max(result.maximum_cell[axis], cell[axis]);
    }
  }
  // All input geometry is checked before any terrain-cache access. A clipped
  // halo is valid at a face edge only because the whole box is strictly in it.
  for (unsigned axis = 0; axis < 2; ++axis) {
    if (result.minimum_cell[axis] != 0) --result.minimum_cell[axis];
    result.maximum_cell[axis] =
        std::min(result.maximum_cell[axis] + 1, 262143U);
  }
  const std::uint64_t count =
      std::uint64_t{result.maximum_cell[0] - result.minimum_cell[0] + 1} *
      (result.maximum_cell[1] - result.minimum_cell[1] + 1) * 2;
  if (count > kMaximumHullClearanceTriangles)
    return std::unexpected{TerrainHullError::cell_budget_exceeded};
  result.triangle_count = static_cast<std::uint32_t>(count);
  return result;
}
} // namespace apsis_drift::detail::hull_numeric
