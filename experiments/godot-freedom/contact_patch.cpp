#include "contact_patch.hpp"

#include <algorithm>
#include <cmath>

namespace apsis_drift::godot_spike {
namespace {
using V = PlanetFixedPositionMetres;
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
auto length(V v) -> double {
  return std::hypot(v.x, v.y, v.z);
}
auto finite(V v) -> bool {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
auto direction(V v) -> PlanetFixedDirection {
  return {v.x, v.y, v.z};
}
auto rotate(RigidOrientation q, V v) -> V {
  const V imaginary{q.x, q.y, q.z};
  const double norm2 = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  return add(v, scale(add(scale(cross(imaginary, v), q.w),
                          cross(imaginary, cross(imaginary, v))),
                      2.0 / norm2));
}
auto certify_patch_impl(const RigidBodyWorldContext& context,
                        const RigidBodyState& state, unsigned support_index,
                        ContactSurfaceRecipe recipe, TerrainTileCache& cache,
                        bool context_owned, ExperimentalContactOwner& owner)
    -> std::expected<ContactPatch, ContactPatchError> {
  if (recipe != kExperimentalContactSurface)
    return std::unexpected{ContactPatchError::unsupported_recipe};
  if (!validate_rigid_body_state(context, state))
    return std::unexpected{ContactPatchError::invalid_state};
  if (state.frame.kind != RigidFrameKind::planet_fixed)
    return std::unexpected{ContactPatchError::unsupported_frame};
  const auto craft = resolve_craft_frame(state.craft);
  if (!craft ||
      !supports_operation(craft->properties, CraftOperation::terrain_contact) ||
      support_index >= craft->properties.support_count)
    return std::unexpected{ContactPatchError::invalid_support};
  const auto body =
      find_local_system_planet(context.system, *state.frame.planet);
  if (!body) return std::unexpected{ContactPatchError::invalid_state};
  const auto& planet = (*body)->descriptor;
  const auto& support = craft->properties.supports[support_index];
  V offset{};
  const auto relative_mm = [&](unsigned axis) {
    // Promote BEFORE subtraction, including future signed COM offsets.
    return static_cast<double>(std::int64_t{support.contact_mm[axis]} -
                               craft->properties.center_of_mass_mm[axis]) /
           1000.0;
  };
  offset = {relative_mm(0), relative_mm(1), relative_mm(2)};
  const V center = add({state.position_metres.x, state.position_metres.y,
                        state.position_metres.z},
                       rotate(state.orientation, offset));
  const V width =
      rotate(state.orientation,
             {static_cast<double>(support.half_width_mm) / 1000.0, 0, 0});
  const V depth =
      rotate(state.orientation,
             {0, 0, static_cast<double>(support.half_length_mm) / 1000.0});
  const V up = rotate(state.orientation, {0, 1, 0});
  const auto radial = length(center);
  const auto radius = static_cast<double>(planet.radius.value) * 1000.0;
  if (!finite(center) || !finite(width) || !finite(depth) || !finite(up) ||
      !std::isfinite(radial) || radial <= 0 ||
      std::abs(radial - radius) > kContactPatchMaximumDatumAltitudeMetres)
    return std::unexpected{ContactPatchError::outside_query_bounds};

  // Input validation and query bounds above touch no terrain cache. This one
  // candidate is chosen by radial center, not claimed to be a first normal hit.
  const auto triangle =
      [&]() -> std::expected<ContactTriangle, ContactSurfaceError> {
    if (context_owned) {
      const auto id = locate_owned_contact_triangle(context.system, planet.id,
                                                    recipe, direction(center));
      if (!id) return std::unexpected{id.error()};
      const auto result =
          build_owned_contact_triangle(context.system, *id, cache);
      if (!result) return std::unexpected{result.error()};
      owner =
          result->owner; // Private temporary; never published on later refusal.
      return result->triangle;
    }
    const auto id = locate_contact_triangle(planet, recipe, direction(center));
    if (!id) return std::unexpected{id.error()};
    return build_contact_triangle(planet, recipe, *id, cache);
  }();
  if (!triangle)
    return std::unexpected{ContactPatchError::surface_query_failed};
  const auto& vertices = triangle->vertices;
  const auto normal = triangle->outward_normal;
  const V n{normal.x, normal.y, normal.z};
  const V relative = subtract(center, vertices[0]);
  if (!finite(relative) || length(relative) > 200000.0)
    return std::unexpected{ContactPatchError::ill_conditioned_geometry};
  std::array<V, 3> edges{};
  std::array<double, 3> edge_lengths{};
  for (unsigned i = 0; i < 3; ++i) {
    edges[i] = subtract(vertices[(i + 1) % 3], vertices[i]);
    edge_lengths[i] = length(edges[i]);
    if (!std::isfinite(edge_lengths[i]) || edge_lengths[i] < 1.0 ||
        edge_lengths[i] > 10000.0)
      return std::unexpected{ContactPatchError::ill_conditioned_geometry};
  }
  const auto sine = length(cross(edges[0], scale(edges[2], -1))) /
                    (edge_lengths[0] * edge_lengths[2]);
  if (!std::isfinite(sine) || sine < .001)
    return std::unexpected{ContactPatchError::ill_conditioned_geometry};
  if (std::abs(dot(up, n)) <= 1e-6)
    return std::unexpected{ContactPatchError::degenerate_projection};

  ContactPatch result;
  result.craft = state.craft;
  result.frame = state.frame;
  result.tick = state.tick;
  result.support_index = static_cast<std::uint8_t>(support_index);
  result.triangle = *triangle;
  result.pad_center = center;
  result.half_width = direction(width);
  result.half_length = direction(depth);
  for (unsigned i = 0; i < 3; ++i) {
    V inward = cross(n, edges[i]);
    const auto magnitude = length(inward);
    if (!std::isfinite(magnitude) || magnitude < .5 * edge_lengths[i])
      return std::unexpected{ContactPatchError::ill_conditioned_geometry};
    inward = {inward.x / magnitude, inward.y / magnitude, inward.z / magnitude};
    // Grid winding varies with face; orient against the opposite vertex.
    if (dot(inward, subtract(vertices[(i + 2) % 3], vertices[i])) < 0)
      inward = scale(inward, -1);
    const auto minimum = dot(inward, subtract(center, vertices[i])) -
                         std::abs(dot(inward, width)) -
                         std::abs(dot(inward, depth));
    if (!std::isfinite(minimum))
      return std::unexpected{ContactPatchError::ill_conditioned_geometry};
    if (minimum <= kContactPatchAllowanceMetres)
      return std::unexpected{ContactPatchError::boundary_not_certified};
    result.minimum_edge_distances_metres[i] = minimum;
  }
  const auto gap = dot(n, relative);
  const auto extent = std::abs(dot(n, width)) + std::abs(dot(n, depth));
  result.minimum_gap_metres = gap - extent - kContactPatchAllowanceMetres;
  result.maximum_gap_metres = gap + extent + kContactPatchAllowanceMetres;
  if (!std::isfinite(result.minimum_gap_metres) ||
      !std::isfinite(result.maximum_gap_metres))
    return std::unexpected{ContactPatchError::ill_conditioned_geometry};
  return result;
}
} // namespace

auto certify_contact_patch(const RigidBodyWorldContext& context,
                           const RigidBodyState& state, unsigned support_index,
                           ContactSurfaceRecipe recipe, TerrainTileCache& cache)
    -> std::expected<ContactPatch, ContactPatchError> {
  ExperimentalContactOwner unused;
  return certify_patch_impl(context, state, support_index, recipe, cache, false,
                            unused);
}

auto certify_owned_contact_patch(const RigidBodyWorldContext& context,
                                 const RigidBodyState& state,
                                 unsigned support_index,
                                 ContactSurfaceRecipe recipe,
                                 TerrainTileCache& cache)
    -> std::expected<ExperimentalOwnedContactPatch, ContactPatchError> {
  ExperimentalContactOwner owner;
  const auto patch = certify_patch_impl(context, state, support_index, recipe,
                                        cache, true, owner);
  if (!patch) return std::unexpected{patch.error()};
  return ExperimentalOwnedContactPatch{owner, *patch};
}

} // namespace apsis_drift::godot_spike
