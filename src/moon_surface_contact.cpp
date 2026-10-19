#include "apsis_drift/moon_surface_contact.hpp"

#include "apsis_drift/rigid_frame_handoff.hpp"
#include "godot/contact_geometry_internal.hpp"
#include "terrain_hull_clearance_internal.hpp"
#include "terrain_touchdown_internal.hpp"
#include "touchdown_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace apsis_drift {
namespace {
using V = MoonFixedPositionMetres;
using Error = MoonContactError;
auto subtract(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto dot(V a, V b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto finite(V a) -> bool {
  return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z) &&
         std::max({std::abs(a.x), std::abs(a.y), std::abs(a.z)}) <= 1e8;
}
auto direction_valid(MoonFixedDirection d) -> bool {
  const auto magnitude =
      std::max({std::abs(d.x), std::abs(d.y), std::abs(d.z)});
  return std::isfinite(d.x) && std::isfinite(d.y) && std::isfinite(d.z) &&
         magnitude >= 1e-12 && magnitude <= 1e9;
}
auto numeric_vertices(const MoonContactTriangle& triangle)
    -> std::array<PlanetFixedPositionMetres, 3> {
  std::array<PlanetFixedPositionMetres, 3> result;
  for (unsigned i = 0; i < 3; ++i)
    result[i] = {triangle.vertices[i].x, triangle.vertices[i].y,
                 triangle.vertices[i].z};
  return result;
}
} // namespace

auto locate_moon_contact_triangle(const MoonTerrainSampler& sampler,
                                  MoonFixedDirection direction)
    -> std::expected<MoonContactTriangleId, Error> {
  if (!direction_valid(direction))
    return std::unexpected{Error::invalid_direction};
  const auto address = cube_surface_address_from_direction(
      {direction.x, direction.y, direction.z}, 13);
  if (!address) return std::unexpected{Error::invalid_direction};
  const auto u = address->u * 32, v = address->v * 32;
  const auto x = std::min(static_cast<unsigned>(u), 31U);
  const auto y = std::min(static_cast<unsigned>(v), 31U);
  const auto half = (u - x) + (v - y) <= 1.0 ? 0U : 1U;
  return MoonContactTriangleId{
      sampler.owner().body, address->face, address->x, address->y, x, y, half};
}
auto build_moon_contact_triangle(const MoonTerrainSampler& sampler,
                                 MoonContactTriangleId id)
    -> std::expected<MoonContactTriangle, Error> {
  if (id.body != sampler.owner().body || static_cast<unsigned>(id.face) > 5 ||
      id.tile_x >= 8192 || id.tile_y >= 8192 || id.cell_x >= 32 ||
      id.cell_y >= 32 || id.half >= 2)
    return std::unexpected{Error::invalid_triangle};
  MoonContactTriangle triangle{kMoonSurfaceContactVersion, id, {}, {}};
  constexpr std::array<std::array<unsigned, 2>, 4> corners{
      {{0, 0}, {1, 0}, {0, 1}, {1, 1}}};
  constexpr std::array<std::array<unsigned, 3>, 2> indices{
      {{0, 1, 2}, {1, 3, 2}}};
  for (unsigned i = 0; i < 3; ++i) {
    const auto offset = corners[indices[id.half][i]];
    const auto direction = direction_from_cube_surface_address(
        {id.face, 13, id.tile_x, id.tile_y,
         static_cast<double>(id.cell_x + offset[0]) / 32,
         static_cast<double>(id.cell_y + offset[1]) / 32});
    if (!direction) return std::unexpected{Error::invalid_triangle};
    const auto position =
        sampler.surface_position({direction->x, direction->y, direction->z});
    if (!position) return std::unexpected{Error::sampling_failure};
    triangle.vertices[i] = *position;
  }
  const auto normal =
      cross(subtract(triangle.vertices[1], triangle.vertices[0]),
            subtract(triangle.vertices[2], triangle.vertices[0]));
  const auto area = std::hypot(normal.x, normal.y, normal.z);
  const auto facing = dot(normal, triangle.vertices[0]);
  if (!std::isfinite(area) || area <= 1e-8 || !std::isfinite(facing) ||
      facing == 0)
    return std::unexpected{Error::unsafe_geometry};
  const auto sign = facing < 0 ? -1.0 : 1.0;
  triangle.outward_normal = {sign * (normal.x / area), sign * (normal.y / area),
                             sign * (normal.z / area)};
  return triangle;
}
auto query_moon_contact_surface(const MoonTerrainSampler& sampler,
                                MoonFixedDirection direction)
    -> std::expected<MoonContactSurfacePoint, Error> {
  const auto id = locate_moon_contact_triangle(sampler, direction);
  if (!id) return std::unexpected{id.error()};
  const auto triangle = build_moon_contact_triangle(sampler, *id);
  if (!triangle) return std::unexpected{triangle.error()};
  // Same radial top-facet intersection and barycentric conditioning as the
  // existing contact surface; no continuous-height shortcut or moved hit.
  const auto magnitude = std::hypot(direction.x, direction.y, direction.z);
  const V ray{direction.x / magnitude, direction.y / magnitude,
              direction.z / magnitude};
  const auto n = triangle->outward_normal;
  const V normal{n.x, n.y, n.z};
  const auto denominator = dot(normal, ray);
  if (!std::isfinite(denominator) || denominator <= 1e-8)
    return std::unexpected{Error::unsafe_geometry};
  const auto distance = dot(normal, triangle->vertices[0]) / denominator;
  const V hit{ray.x * distance, ray.y * distance, ray.z * distance};
  if (!std::isfinite(distance) || distance <= 0 || distance > 1e8 ||
      !finite(hit))
    return std::unexpected{Error::unsafe_geometry};
  const auto a = subtract(triangle->vertices[1], triangle->vertices[0]);
  const auto b = subtract(triangle->vertices[2], triangle->vertices[0]);
  const auto c = subtract(hit, triangle->vertices[0]);
  const auto aa = dot(a, a), ab = dot(a, b), bb = dot(b, b), ca = dot(c, a),
             cb = dot(c, b);
  const auto determinant = aa * bb - ab * ab;
  if (!std::isfinite(determinant) || determinant <= 1e-8)
    return std::unexpected{Error::unsafe_geometry};
  const auto y = (bb * ca - ab * cb) / determinant;
  const auto z = (aa * cb - ab * ca) / determinant;
  const std::array<double, 3> barycentric{1.0 - y - z, y, z};
  for (const auto weight : barycentric)
    if (!std::isfinite(weight) || weight < -1e-7 || weight > 1.0 + 1e-7)
      return std::unexpected{Error::unsafe_geometry};
  return MoonContactSurfacePoint{*triangle, hit, distance, barycentric};
}
auto MoonContactSnapshot::create(const PhysicalMoonDescriptor& moon,
                                 const RigidBodyState& source, bool gear,
                                 std::uint32_t version)
    -> std::expected<MoonContactSnapshot, Error> {
  if (version != kMoonTerrainRecipeVersion)
    return std::unexpected{Error::unsupported_version};
  auto sampler = MoonTerrainSampler::create(moon, version);
  if (!sampler) return std::unexpected{Error::invalid_owner};
  const RigidBodyWorldContext context{moon};
  if (!validate_rigid_body_state(context, source))
    return std::unexpected{Error::invalid_state};
  if (source.frame.kind != RigidFrameKind::body_relative_inertial)
    return std::unexpected{Error::unsupported_frame};
  const auto craft = resolve_craft_frame(source.craft);
  if (!craft ||
      !supports_operation(craft->properties, CraftOperation::terrain_contact) ||
      craft->properties.support_count < 1 ||
      craft->properties.support_count > 4)
    return std::unexpected{Error::invalid_craft};
  const RigidFrameHandoffRequest request{
      {RigidFrameKind::body_fixed, context.system.id, {}, {}, moon.body},
      source.tick};
  const auto fixed = reframe_rigid_body(context, source, request, moon);
  if (!fixed) return std::unexpected{Error::reframe_failed};
  const auto checksum = rigid_body_state_checksum(context, source);
  if (!checksum) return std::unexpected{Error::invalid_state};
  return MoonContactSnapshot{
      MoonContactProvenance{moon, version, source, *fixed, *checksum},
      std::move(*sampler), gear};
}
auto MoonContactSnapshot::query() const -> MoonContactResult {
  using namespace godot_spike;
  MoonContactResult result{provenance_, 0, {}, {}, {}};
  const auto& state = provenance_.fixed_query_state;
  if (!validate_physical_moon_descriptor(provenance_.owner)) {
    result.envelope_error = TouchdownError::invalid_state;
    return result;
  }
  const auto craft = resolve_craft_frame(state.craft);
  result.support_count = craft->properties.support_count;
  std::array<std::expected<godot_spike::detail::NominalContactRectangle,
                           ContactPatchError>,
             4>
      rectangles;
  for (unsigned i = 0; i < result.support_count; ++i)
    rectangles[i] = godot_spike::detail::nominal_contact_rectangle(
        provenance_.owner.radius_metres, state, craft->properties, i);
  std::array<TouchdownPadObservation, 4> observations{};
  bool complete = true;
  for (unsigned i = 0; i < result.support_count; ++i) {
    auto& outcome = result.supports[i];
    outcome.support_index = static_cast<std::uint8_t>(i);
    const auto& rectangle = rectangles[i];
    if (!rectangle) {
      outcome.geometry = std::unexpected{rectangle.error()};
      outcome.support =
          std::unexpected{TerrainSupportError::unresolved_geometry};
      complete = false;
      continue;
    }
    const auto id = locate_moon_contact_triangle(
        sampler_,
        {rectangle->center.x, rectangle->center.y, rectangle->center.z});
    const auto triangle = id ? build_moon_contact_triangle(sampler_, *id)
                             : std::expected<MoonContactTriangle, Error>{
                                   std::unexpected{id.error()}};
    if (!triangle) {
      outcome.geometry =
          std::unexpected{ContactPatchError::surface_query_failed};
      outcome.support =
          std::unexpected{TerrainSupportError::unresolved_geometry};
      complete = false;
      continue;
    }
    outcome.triangle = *triangle;
    const auto vertices = numeric_vertices(*triangle);
    const PlanetFixedDirection normal{triangle->outward_normal.x,
                                      triangle->outward_normal.y,
                                      triangle->outward_normal.z};
    const auto certificate =
        godot_spike::detail::certify_contact_rectangle_geometry(
            *rectangle, vertices, normal);
    if (!certificate) {
      outcome.geometry = std::unexpected{certificate.error()};
      outcome.support =
          std::unexpected{TerrainSupportError::unresolved_geometry};
      complete = false;
      continue;
    }
    outcome.geometry = MoonContactPatch{
        kMoonSurfaceContactVersion,
        state.craft,
        state.frame,
        state.tick,
        static_cast<std::uint8_t>(i),
        *triangle,
        {rectangle->center.x, rectangle->center.y, rectangle->center.z},
        {rectangle->width.x, rectangle->width.y, rectangle->width.z},
        {rectangle->depth.x, rectangle->depth.y, rectangle->depth.z},
        certificate->minimum_gap_metres,
        certificate->maximum_gap_metres,
        certificate->minimum_edge_distances_metres};
    const auto first = apsis_drift::detail::certify_contact_first_surface(
        vertices, *rectangle, normal);
    if (!first) {
      outcome.support = std::unexpected{first.error()};
      complete = false;
      continue;
    }
    // Authenticated recipe1 is dry bedrock at ALL signed elevations. Datum
    // radius is not a sea level on this water-free body.
    observations[i] = {
        static_cast<std::uint8_t>(i),    certificate->minimum_gap_metres,
        certificate->maximum_gap_metres, {normal.x, normal.y, normal.z},
        TouchdownSurface::solid,         true};
    outcome.support = observations[i];
  }
  if (complete) {
    const auto physical = resolve_physical_moon_properties(provenance_.owner);
    if (!physical) {
      result.envelope_error = TouchdownError::unsafe_arithmetic;
      return result;
    }
    const auto assessment = apsis_drift::detail::assess_touchdown_geometry(
        state, result.support_count, observations, gear_,
        physical->surface_gravity_metres_per_second_squared, 0);
    if (assessment)
      result.assessment = *assessment;
    else
      result.envelope_error = assessment.error();
  }
  return result;
}
auto MoonContactSnapshot::hull_clearance() const
    -> std::expected<TerrainHullClearance, TerrainHullError> {
  using namespace detail::hull_numeric;
  if (!validate_physical_moon_descriptor(provenance_.owner))
    return std::unexpected{TerrainHullError::invalid_source};
  const auto& state = provenance_.fixed_query_state;
  const auto craft = resolve_craft_frame(state.craft);
  const auto coverage = bound_cells(
      state, craft->properties, provenance_.owner.radius_metres,
      provenance_.original_state_checksum, provenance_.terrain_version,
      [&](RigidVector3 p) {
        return locate_moon_contact_triangle(sampler_, {p.x, p.y, p.z});
      });
  if (!coverage) return std::unexpected{coverage.error()};
  auto result = *coverage;
  const RigidOrientation inverse{state.orientation.w, -state.orientation.x,
                                 -state.orientation.y, -state.orientation.z};
  double highest_y = -std::numeric_limits<double>::infinity();
  for (auto x = result.minimum_cell[0]; x <= result.maximum_cell[0]; ++x)
    for (auto y = result.minimum_cell[1]; y <= result.maximum_cell[1]; ++y)
      for (unsigned half = 0; half < 2; ++half) {
        const auto triangle = build_moon_contact_triangle(
            sampler_, {provenance_.owner.body, result.face, x / 32, y / 32,
                       x % 32, y % 32, half});
        if (!triangle)
          return std::unexpected{TerrainHullError::surface_query_failed};
        for (const auto& p : triangle->vertices) {
          const auto relative = detail::hull_numeric::subtract(
              {p.x, p.y, p.z}, state.position_metres);
          const auto body = rotate(inverse, relative);
          if (!detail::hull_numeric::finite(body))
            return std::unexpected{TerrainHullError::unsafe_arithmetic};
          highest_y = std::max(highest_y, body.y);
        }
      }
  const auto minimum_y =
      static_cast<double>(std::int64_t{craft->properties.hull_min_mm[1]} -
                          craft->properties.center_of_mass_mm[1]) /
      1000;
  result.lower_clearance_metres =
      minimum_y - highest_y - godot_spike::kContactPatchAllowanceMetres;
  if (!std::isfinite(result.lower_clearance_metres))
    return std::unexpected{TerrainHullError::unsafe_arithmetic};
  result.certified = result.lower_clearance_metres > 0;
  return result;
}
} // namespace apsis_drift
