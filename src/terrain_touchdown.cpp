#include "apsis_drift/terrain_touchdown.hpp"
#include "terrain_touchdown_internal.hpp"

#include <algorithm>
#include <cmath>

namespace apsis_drift {
namespace {
using V = RigidVector3;
template <class T> auto vector(T p) -> V {
  return {p.x, p.y, p.z};
}
auto subtract(V a, V b) -> V {
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
  return std::hypot(a.x, a.y, a.z);
}
auto finite(V a) -> bool {
  return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z) &&
         std::max({std::abs(a.x), std::abs(a.y), std::abs(a.z)}) <= 1e8;
}
auto rectangle_minimum(V inward, V center, V width, V depth) -> double {
  return dot(inward, center) - std::abs(dot(inward, width)) -
         std::abs(dot(inward, depth));
}
} // namespace

namespace detail {
auto terrain_pad_observation(const PlanetDescriptor& planet,
                             const godot_spike::ContactPatch& patch)
    -> std::expected<TouchdownPadObservation, TerrainSupportError> {
  using namespace godot_spike;
  const auto& triangle = patch.triangle;
  const auto n = vector(triangle.outward_normal);
  const auto center = vector(patch.pad_center);
  const auto width = vector(patch.half_width);
  const auto depth = vector(patch.half_length);
  const auto& id = triangle.id;
  if (patch.version != kExperimentalContactPatchVersion ||
      triangle.recipe != kExperimentalSavedContactSurface ||
      id.planet != planet.id || static_cast<unsigned>(id.face) > 5 ||
      id.tile_x >= 8192 || id.tile_y >= 8192 || id.cell_x >= 32 ||
      id.cell_y >= 32 || id.half >= 2 || patch.support_index >= 4 ||
      !finite(n) || !finite(center) || !finite(width) || !finite(depth) ||
      std::abs(dot(n, n) - 1) > kRigidBodyOrientationSquaredNormTolerance ||
      length(width) <= 0 || length(depth) <= 0 ||
      !std::isfinite(patch.minimum_gap_metres) ||
      !std::isfinite(patch.maximum_gap_metres) ||
      patch.minimum_gap_metres > patch.maximum_gap_metres)
    return std::unexpected{TerrainSupportError::invalid_geometry};
  for (const auto p : triangle.vertices)
    if (!finite(vector(p)))
      return std::unexpected{TerrainSupportError::invalid_geometry};
  const auto anchor = vector(triangle.vertices[0]);
  const auto plane_distance = dot(n, anchor);
  if (!std::isfinite(plane_distance) || plane_distance <= 0)
    return std::unexpected{TerrainSupportError::invalid_geometry};

  const auto gap = dot(n, subtract(center, anchor));
  const auto projected_center = subtract(center, scale(n, gap));
  const auto projected_width = subtract(width, scale(n, dot(n, width)));
  const auto projected_depth = subtract(depth, scale(n, dot(n, depth)));
  for (unsigned edge = 0; edge < 3; ++edge) {
    const auto a = vector(triangle.vertices[edge]);
    const auto b = vector(triangle.vertices[(edge + 1) % 3]);
    const auto opposite = vector(triangle.vertices[(edge + 2) % 3]);
    const auto delta = subtract(b, a);
    const auto edge_length = length(delta);
    if (!std::isfinite(edge_length) || edge_length < 1 || edge_length > 10000 ||
        std::abs(dot(n, delta)) > kContactPatchAllowanceMetres)
      return std::unexpected{TerrainSupportError::invalid_geometry};
    // Edge difference avoids subtracting two huge, nearly equal cross
    // products. Cone planes pass through the planet origin, not the facet.
    auto inward = cross(a, delta);
    const auto magnitude = length(inward);
    if (!std::isfinite(magnitude) || magnitude <= 0)
      return std::unexpected{TerrainSupportError::invalid_geometry};
    inward = scale(inward, 1 / magnitude);
    const auto orientation = dot(inward, subtract(opposite, a));
    if (!std::isfinite(orientation) ||
        std::abs(orientation) <= kContactPatchAllowanceMetres)
      return std::unexpected{TerrainSupportError::invalid_geometry};
    if (orientation < 0) inward = scale(inward, -1);
    const auto pad_minimum = rectangle_minimum(inward, center, width, depth);
    const auto surface_minimum = rectangle_minimum(
        inward, projected_center, projected_width, projected_depth);
    if (!std::isfinite(pad_minimum) || !std::isfinite(surface_minimum))
      return std::unexpected{TerrainSupportError::invalid_geometry};
    if (pad_minimum <= kContactPatchAllowanceMetres ||
        surface_minimum <= kContactPatchAllowanceMetres)
      return std::unexpected{TerrainSupportError::first_surface_not_certified};
  }

  // Entire facet above sea datum: projection onto the anchor radial unit
  // vector lower-bounds norm throughout the facet. Below datum: convexity
  // bounds norm by its vertices. This remains local on steep dry terrain.
  // Shoreline/uncertainty is unsupported; palette is never physical evidence.
  const auto radius = static_cast<double>(planet.radius.value) * 1000;
  TouchdownSurface surface = TouchdownSurface::unsupported;
  const auto radial = scale(anchor, 1 / length(anchor));
  if (std::all_of(triangle.vertices.begin(), triangle.vertices.end(),
                  [&](auto p) {
                    return dot(radial, vector(p)) >
                           radius + kContactPatchAllowanceMetres;
                  })) {
    surface = TouchdownSurface::solid;
  } else if (std::all_of(triangle.vertices.begin(), triangle.vertices.end(),
                         [&](auto p) {
                           return length(vector(p)) <
                                  radius - kContactPatchAllowanceMetres;
                         })) {
    surface = TouchdownSurface::water;
  }
  return TouchdownPadObservation{patch.support_index,
                                 patch.minimum_gap_metres,
                                 patch.maximum_gap_metres,
                                 n,
                                 surface,
                                 surface == TouchdownSurface::solid};
}
} // namespace detail

auto TerrainTouchdownSnapshot::create(const NativeFreedomFlightSession& session,
                                      bool gear_deployed,
                                      std::uint32_t policy_version,
                                      std::size_t cache_capacity)
    -> std::expected<TerrainTouchdownSnapshot, godot_spike::SavedContactError> {
  if (policy_version != kTerrainTouchdownPolicyVersion)
    return std::unexpected{godot_spike::SavedContactError::unsupported_recipe};
  auto geometry = godot_spike::SavedContactGeometry::create(
      session, godot_spike::kExperimentalSavedContactSurface, cache_capacity);
  if (!geometry) return std::unexpected{geometry.error()};
  return TerrainTouchdownSnapshot{std::move(*geometry), gear_deployed};
}
auto TerrainTouchdownSnapshot::query() -> TerrainTouchdownResult {
  TerrainTouchdownResult result{
      kTerrainTouchdownPolicyVersion, geometry_.query(), {}, {}, {}};
  const auto& provenance = result.geometry.provenance;
  TouchdownObservations observations{provenance.planet.id,
                                     provenance.fixed_query_state.tick,
                                     result.geometry.support_count,
                                     {},
                                     gear_deployed_};
  bool complete = true;
  for (unsigned i = 0; i < result.geometry.support_count; ++i) {
    const auto& geometry = result.geometry.supports[i].geometry;
    auto& support = result.supports[i];
    support =
        geometry && *geometry
            ? detail::terrain_pad_observation(provenance.planet, **geometry)
            : std::unexpected{TerrainSupportError::unresolved_geometry};
    if (*support)
      observations.supports[i] = **support;
    else
      complete = false;
  }
  if (complete) {
    const auto assessment = assess_touchdown_envelope(
        {provenance.owner}, provenance.fixed_query_state, observations);
    if (assessment)
      result.assessment = *assessment;
    else
      result.envelope_error = assessment.error();
  }
  return result;
}
} // namespace apsis_drift
