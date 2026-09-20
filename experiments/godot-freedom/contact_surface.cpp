#include "contact_surface.hpp"

#include "relief.hpp"

#include <algorithm>
#include <cmath>

namespace apsis_drift::godot_spike {
namespace {
using Point = PlanetFixedPositionMetres;
auto difference(Point a, Point b) -> Point {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto product(Point a, Point b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto vector_product(Point a, Point b) -> Point {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto valid_point(Point p) -> bool {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
         std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)}) <= 1e8;
}
auto validate(const PlanetDescriptor& planet, ContactSurfaceRecipe recipe)
    -> std::expected<void, ContactSurfaceError> {
  static_assert(kTerrainTileGeneratorVersion == 1);
  static_assert(kExperimentalReliefVersion == 1);
  static_assert(kPlanetGeneratorVersion == 1);
  static_assert(kSeedDerivationVersion == 1);
  if (recipe != kExperimentalContactSurface)
    return std::unexpected{ContactSurfaceError::unsupported_recipe};
  if (planet != generate_planet_descriptor(planet.seed))
    return std::unexpected{ContactSurfaceError::invalid_planet};
  return {};
}
auto valid_direction(PlanetFixedDirection direction) -> bool {
  const auto maximum = std::max(
      {std::abs(direction.x), std::abs(direction.y), std::abs(direction.z)});
  return std::isfinite(direction.x) && std::isfinite(direction.y) &&
         std::isfinite(direction.z) && maximum >= 1e-12 && maximum <= 1e9;
}
auto valid_id(ContactTriangleId id) -> bool {
  return static_cast<unsigned>(id.face) <= 5 && id.tile_x < 8192 &&
         id.tile_y < 8192 && id.cell_x < 32 && id.cell_y < 32 && id.half < 2;
}
auto build_validated(const PlanetDescriptor& planet,
                     ContactSurfaceRecipe recipe, ContactTriangleId id,
                     TerrainTileCache& cache)
    -> std::expected<ContactTriangle, ContactSurfaceError> {
  ContactTriangle triangle{recipe, id, {}, {}};
  constexpr std::array<std::array<unsigned, 2>, 4> corners{
      {{0, 0}, {1, 0}, {0, 1}, {1, 1}}};
  constexpr std::array<std::array<unsigned, 3>, 2> indices{
      {{0, 1, 2}, {1, 3, 2}}};
  for (unsigned i = 0; i < 3; ++i) {
    const auto offset = corners[indices[id.half][i]];
    // Preserve build_stream_tile's actual address/reference/sample arithmetic.
    const TerrainTileAddress address{
        {planet.id, id.face, 13, id.tile_x, id.tile_y},
        static_cast<double>(id.cell_x + offset[0]) / 32,
        static_cast<double>(id.cell_y + offset[1]) / 32};
    const auto reference = planet_fixed_from_terrain_address(planet, address);
    if (!reference)
      return std::unexpected{ContactSurfaceError::coordinate_failure};
    auto sample = sample_planet_surface(planet, *reference, 8, cache);
    if (!sample) return std::unexpected{ContactSurfaceError::sampling_failure};
    // Validated generated radii and address bound relief's coordinate/cell
    // arithmetic. No arbitrary heights, relief versions or vector magnitudes.
    *sample = with_relief(*sample, planet, *reference, 1);
    const auto vertex = planet_fixed_from_terrain_address(
        planet, address, sample->elevation_metres);
    if (!vertex || !valid_point(*vertex))
      return std::unexpected{ContactSurfaceError::unsafe_geometry};
    triangle.vertices[i] = *vertex;
  }
  auto normal =
      vector_product(difference(triangle.vertices[1], triangle.vertices[0]),
                     difference(triangle.vertices[2], triangle.vertices[0]));
  const auto area = std::hypot(normal.x, normal.y, normal.z);
  const auto facing = product(normal, triangle.vertices[0]);
  if (!std::isfinite(area) || area <= 1e-8 || !std::isfinite(facing) ||
      facing == 0)
    return std::unexpected{ContactSurfaceError::unsafe_geometry};
  const auto sign = facing < 0 ? -1.0 : 1.0;
  triangle.outward_normal = {sign * (normal.x / area), sign * (normal.y / area),
                             sign * (normal.z / area)};
  return triangle;
}
} // namespace

auto locate_contact_triangle(const PlanetDescriptor& planet,
                             ContactSurfaceRecipe recipe,
                             PlanetFixedDirection direction)
    -> std::expected<ContactTriangleId, ContactSurfaceError> {
  const auto valid = validate(planet, recipe);
  if (!valid) return std::unexpected{valid.error()};
  if (!valid_direction(direction))
    return std::unexpected{ContactSurfaceError::invalid_direction};
  const auto address =
      terrain_address_from_planet_direction(planet, direction, 13);
  if (!address) return std::unexpected{ContactSurfaceError::coordinate_failure};
  const auto u = address->u * 32, v = address->v * 32;
  const auto x = std::min(static_cast<unsigned>(u), 31U);
  const auto y = std::min(static_cast<unsigned>(v), 31U);
  const auto half = (u - x) + (v - y) <= 1.0 ? 0U : 1U;
  return ContactTriangleId{
      planet.id, address->tile.face, address->tile.x, address->tile.y, x, y,
      half};
}

auto build_contact_triangle(const PlanetDescriptor& planet,
                            ContactSurfaceRecipe recipe, ContactTriangleId id,
                            TerrainTileCache& cache)
    -> std::expected<ContactTriangle, ContactSurfaceError> {
  const auto valid = validate(planet, recipe);
  if (!valid) return std::unexpected{valid.error()};
  if (id.planet != planet.id)
    return std::unexpected{ContactSurfaceError::wrong_planet};
  if (!valid_id(id))
    return std::unexpected{ContactSurfaceError::invalid_triangle};
  return build_validated(planet, recipe, id, cache);
}

auto query_contact_surface(const PlanetDescriptor& planet,
                           ContactSurfaceRecipe recipe,
                           PlanetFixedDirection direction,
                           TerrainTileCache& cache)
    -> std::expected<ContactSurfacePoint, ContactSurfaceError> {
  const auto id = locate_contact_triangle(planet, recipe, direction);
  if (!id) return std::unexpected{id.error()};
  const auto triangle = build_validated(planet, recipe, *id, cache);
  if (!triangle) return std::unexpected{triangle.error()};
  const auto magnitude = std::hypot(direction.x, direction.y, direction.z);
  const Point ray{direction.x / magnitude, direction.y / magnitude,
                  direction.z / magnitude};
  const auto n = triangle->outward_normal;
  const Point normal{n.x, n.y, n.z};
  const auto denominator = product(normal, ray);
  if (!std::isfinite(denominator) || denominator <= 1e-8)
    return std::unexpected{ContactSurfaceError::unsafe_geometry};
  const auto distance = product(normal, triangle->vertices[0]) / denominator;
  const Point hit{ray.x * distance, ray.y * distance, ray.z * distance};
  if (!std::isfinite(distance) || distance <= 0 || distance > 1e8 ||
      !valid_point(hit))
    return std::unexpected{ContactSurfaceError::unsafe_geometry};
  const auto a = difference(triangle->vertices[1], triangle->vertices[0]);
  const auto b = difference(triangle->vertices[2], triangle->vertices[0]);
  const auto c = difference(hit, triangle->vertices[0]);
  const auto aa = product(a, a), ab = product(a, b), bb = product(b, b);
  const auto ca = product(c, a), cb = product(c, b);
  const auto determinant = aa * bb - ab * ab;
  if (!std::isfinite(determinant) || determinant <= 1e-8)
    return std::unexpected{ContactSurfaceError::unsafe_geometry};
  const auto y = (bb * ca - ab * cb) / determinant;
  const auto z = (aa * cb - ab * ca) / determinant;
  const std::array<double, 3> barycentric{1.0 - y - z, y, z};
  for (const auto weight : barycentric)
    if (!std::isfinite(weight) || weight < -1e-7 || weight > 1.0 + 1e-7)
      return std::unexpected{ContactSurfaceError::unsafe_geometry};
  return ContactSurfacePoint{*triangle, hit, distance, barycentric};
}

} // namespace apsis_drift::godot_spike
