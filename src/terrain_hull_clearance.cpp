#include "apsis_drift/terrain_hull_clearance.hpp"

#include "godot/contact_geometry_internal.hpp"
#include "godot/saved_contact_geometry_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace apsis_drift {
namespace {
using V = RigidVector3;
auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto subtract(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(V a, double s) -> V {
  return {a.x * s, a.y * s, a.z * s};
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto rotate(RigidOrientation q, V v) -> V {
  const V imaginary{q.x, q.y, q.z};
  const auto norm2 = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  return add(v, scale(add(scale(cross(imaginary, v), q.w),
                          cross(imaginary, cross(imaginary, v))),
                      2 / norm2));
}
auto finite(V a) -> bool {
  return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z) &&
         std::max({std::abs(a.x), std::abs(a.y), std::abs(a.z)}) <= 1e8;
}
auto strictly_in_face(V p, CubeFace face) -> bool {
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
} // namespace

auto assess_terrain_hull_clearance(const PhysicalLocalSystem& owner,
                                   const PhysicalPlanetRotationRecipe& rotation,
                                   const RigidBodyState& source,
                                   std::size_t cache_capacity)
    -> std::expected<TerrainHullClearance, TerrainHullError> {
  using namespace godot_spike;
  const auto prepared = godot_spike::detail::prepare_saved_contact(
      owner, rotation, source, kExperimentalSavedContactSurface);
  if (!prepared) return std::unexpected{TerrainHullError::invalid_source};
  const auto& state = prepared->fixed_query_state;
  const auto craft = resolve_craft_frame(state.craft);
  if (!craft) return std::unexpected{TerrainHullError::invalid_source};
  const auto& properties = craft->properties;
  const auto radius = static_cast<double>(prepared->planet.radius.value) * 1000;
  if (std::abs(std::hypot(state.position_metres.x, state.position_metres.y,
                          state.position_metres.z) -
               radius) > kContactPatchMaximumDatumAltitudeMetres)
    return std::unexpected{TerrainHullError::outside_query_bounds};

  TerrainHullClearance result;
  result.source_checksum = prepared->original_state_checksum;
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
    const auto id = godot_spike::detail::locate_selected_contact_triangle(
        prepared->planet, {p.x, p.y, p.z});
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
  auto cache = TerrainTileCache::create(cache_capacity);
  if (!cache) return std::unexpected{TerrainHullError::invalid_cache_capacity};
  const RigidOrientation inverse{state.orientation.w, -state.orientation.x,
                                 -state.orientation.y, -state.orientation.z};
  double highest_y = -std::numeric_limits<double>::infinity();
  for (auto x = result.minimum_cell[0]; x <= result.maximum_cell[0]; ++x)
    for (auto y = result.minimum_cell[1]; y <= result.maximum_cell[1]; ++y)
      for (unsigned half = 0; half < 2; ++half) {
        const auto triangle =
            godot_spike::detail::build_selected_contact_triangle(
                prepared->planet, kExperimentalSavedContactSurface,
                {prepared->planet.id, result.face, x / 32, y / 32, x % 32,
                 y % 32, half},
                *cache);
        if (!triangle)
          return std::unexpected{TerrainHullError::surface_query_failed};
        for (const auto p : triangle->vertices) {
          const auto relative =
              subtract({p.x, p.y, p.z}, state.position_metres);
          const auto body = rotate(inverse, relative);
          if (!finite(body))
            return std::unexpected{TerrainHullError::unsafe_arithmetic};
          highest_y = std::max(highest_y, body.y);
        }
      }
  const auto minimum_y =
      static_cast<double>(std::int64_t{properties.hull_min_mm[1]} -
                          properties.center_of_mass_mm[1]) /
      1000;
  result.lower_clearance_metres =
      minimum_y - highest_y - kContactPatchAllowanceMetres;
  if (!std::isfinite(result.lower_clearance_metres))
    return std::unexpected{TerrainHullError::unsafe_arithmetic};
  result.certified = result.lower_clearance_metres > 0;
  return result;
}
} // namespace apsis_drift
