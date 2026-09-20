#include "contact_surface.hpp"
#include "streaming.hpp"

#include <bit>
#include <iostream>
#include <limits>

using namespace apsis_drift;
using namespace apsis_drift::godot_spike;

namespace {
auto check(bool passed, const char* message) -> void {
  if (!passed) throw std::runtime_error(message);
}
template <class T>
auto error(const std::expected<T, ContactSurfaceError>& result,
           ContactSurfaceError expected, const char* message) -> void {
  check(!result && result.error() == expected, message);
}
auto direction(unsigned face, double u, double v) -> PlanetFixedDirection {
  // Independent cube-plane basis, without provider addressing helpers.
  switch (face) {
    case 0: return {1, u, v};
    case 1: return {-1, -u, v};
    case 2: return {-u, 1, v};
    case 3: return {u, -1, v};
    case 4: return {-v, u, 1};
    default: return {v, u, -1};
  }
}
auto cell_direction(ContactTriangleId id, double u, double v)
    -> PlanetFixedDirection {
  const auto x = static_cast<double>(id.tile_x * 32 + id.cell_x) + u;
  const auto y = static_cast<double>(id.tile_y * 32 + id.cell_y) + v;
  return direction(static_cast<unsigned>(id.face), x / 131072.0 - 1,
                   y / 131072.0 - 1);
}
auto point(PlanetFixedDirection d) -> StreamPoint {
  return {d.x, d.y, d.z};
}
auto hash(const ContactSurfacePoint& result) -> std::uint64_t {
  std::uint64_t value = 1469598103934665603ULL;
  auto add_value = [&](std::uint64_t item) {
    for (unsigned i = 0; i < 8; ++i) {
      value ^= (item >> (i * 8)) & 255U;
      value *= 1099511628211ULL;
    }
  };
  const auto& recipe = result.triangle.recipe;
  for (auto item :
       {recipe.version, recipe.terrain_generator, recipe.source_lod,
        recipe.relief, recipe.mesh_lod, recipe.intervals, recipe.diagonal})
    add_value(item);
  const auto& id = result.triangle.id;
  add_value(id.planet.value);
  add_value(static_cast<unsigned>(id.face));
  for (auto item : {id.tile_x, id.tile_y, id.cell_x, id.cell_y, id.half})
    add_value(item);
  auto add_point = [&](StreamPoint p) {
    for (auto component : {p.x, p.y, p.z})
      add_value(std::bit_cast<std::uint64_t>(component));
  };
  for (auto vertex : result.triangle.vertices)
    add_point(vertex);
  add_point(point(result.triangle.outward_normal));
  add_point(result.position);
  add_value(std::bit_cast<std::uint64_t>(result.radial_distance_metres));
  for (auto component : result.barycentric)
    add_value(std::bit_cast<std::uint64_t>(component));
  return value;
}
auto invalid_contract(const PlanetDescriptor& planet) -> void {
  auto cache = require(TerrainTileCache::create(1));
  const auto recipe = kExperimentalContactSurface;
  constexpr PlanetFixedDirection valid{1, .25, -.25};
  constexpr auto nan = std::numeric_limits<double>::quiet_NaN();
  constexpr auto inf = std::numeric_limits<double>::infinity();
  for (const auto bad : {PlanetFixedDirection{0, 0, 0},
                         {nan, 1, 0},
                         {1, nan, 0},
                         {1, 0, nan},
                         {inf, 0, 0},
                         {0, -inf, 0},
                         {1e10, 0, 0},
                         {1e-13, 0, 0},
                         {1e300, 1e300, 1e300}})
    error(query_contact_surface(planet, recipe, bad, cache),
          ContactSurfaceError::invalid_direction, "invalid direction accepted");
  for (unsigned field = 0; field < 7; ++field) {
    auto bad = recipe;
    const std::array members{&ContactSurfaceRecipe::version,
                             &ContactSurfaceRecipe::terrain_generator,
                             &ContactSurfaceRecipe::source_lod,
                             &ContactSurfaceRecipe::relief,
                             &ContactSurfaceRecipe::mesh_lod,
                             &ContactSurfaceRecipe::intervals,
                             &ContactSurfaceRecipe::diagonal};
    bad.*members[field] = std::numeric_limits<std::uint32_t>::max();
    error(query_contact_surface(planet, bad, valid, cache),
          ContactSurfaceError::unsupported_recipe,
          "unsupported recipe accepted");
  }
  const auto id = require(locate_contact_triangle(planet, recipe, valid));
  auto wrong = id;
  wrong.planet.value ^= 1;
  error(build_contact_triangle(planet, recipe, wrong, cache),
        ContactSurfaceError::wrong_planet, "wrong triangle planet accepted");
  for (unsigned field = 0; field < 6; ++field) {
    auto bad = id;
    switch (field) {
      case 0: bad.face = static_cast<CubeFace>(255); break;
      case 1: bad.tile_x = 8192; break;
      case 2: bad.tile_y = std::numeric_limits<std::uint32_t>::max(); break;
      case 3: bad.cell_x = 32; break;
      case 4: bad.cell_y = std::numeric_limits<std::uint32_t>::max(); break;
      default: bad.half = 2; break;
    }
    error(build_contact_triangle(planet, recipe, bad, cache),
          ContactSurfaceError::invalid_triangle,
          "invalid triangle index accepted");
  }
  const PlanetDescriptor forged{planet.seed,
                                planet.id,
                                planet.display_name,
                                PlanetRadiusKm{0},
                                planet.surface_gravity,
                                planet.atmosphere_class,
                                planet.atmosphere_pressure,
                                planet.terrain_character,
                                planet.water_coverage,
                                planet.palette};
  error(query_contact_surface(forged, recipe, valid, cache),
        ContactSurfaceError::invalid_planet,
        "degenerate forged planet accepted");
  const PlanetDescriptor forged_valid_range{
      planet.seed,
      planet.id,
      planet.display_name,
      PlanetRadiusKm{planet.radius.value == 9000 ? 8999U
                                                 : planet.radius.value + 1},
      planet.surface_gravity,
      planet.atmosphere_class,
      planet.atmosphere_pressure,
      planet.terrain_character,
      planet.water_coverage,
      planet.palette};
  error(query_contact_surface(forged_valid_range, recipe, valid, cache),
        ContactSurfaceError::invalid_planet,
        "plausible but noncanonical planet accepted");
  check(cache.size() == 0, "malformed request sampled/mutated terrain cache");
}
auto ownership_contract(const PlanetDescriptor& planet) -> void {
  const auto recipe = kExperimentalContactSurface;
  for (unsigned face = 0; face < 6; ++face) {
    ContactTriangleId id{
        planet.id, static_cast<CubeFace>(face), 5000, 3000, 11, 19, 0};
    for (const auto uv : {std::array{.125, .125}, std::array{.25, .75},
                          std::array{.875, .875}}) {
      const auto found = require(locate_contact_triangle(
          planet, recipe, cell_direction(id, uv[0], uv[1])));
      id.half = uv[0] + uv[1] <= 1 ? 0 : 1;
      check(found == id,
            "face/cell/diagonal ownership differs from authored topology");
    }
    id.half = 0;
    const auto edge = require(
        locate_contact_triangle(planet, recipe, cell_direction(id, 1, 0)));
    check(edge.cell_x == 12 && edge.cell_y == 19 && edge.half == 0,
          "internal cell edge must belong to positive-side cell");
    id.cell_x = 31;
    const auto tile_edge = require(
        locate_contact_triangle(planet, recipe, cell_direction(id, 1, 0)));
    check(tile_edge.tile_x == 5001 && tile_edge.cell_x == 0,
          "internal tile edge must belong to positive-side tile");
  }
  for (double x : {-1.0, 1.0})
    for (double y : {-1.0, 1.0})
      for (double z : {-1.0, 1.0}) {
        const auto id =
            require(locate_contact_triangle(planet, recipe, {x, y, z}));
        check(id.face == (x > 0 ? CubeFace::positive_x : CubeFace::negative_x),
              "cube corner must belong to X face");
      }
  for (double y : {-1.0, 1.0})
    for (double z : {-1.0, 1.0}) {
      const auto id =
          require(locate_contact_triangle(planet, recipe, {0, y, z}));
      check(id.face == (y > 0 ? CubeFace::positive_y : CubeFace::negative_y),
            "Y/Z cube edge must belong to Y face");
    }
  auto last = require(locate_contact_triangle(planet, recipe, {1, 1, 1}));
  check(last.tile_x == 8191 && last.tile_y == 8191 && last.cell_x == 31 &&
            last.cell_y == 31 && last.half == 1,
        "upper face endpoint wrapped or overflowed");
}
auto geometric_contract(const ContactSurfacePoint& hit,
                        PlanetFixedDirection input) -> void {
  const auto& vertices = hit.triangle.vertices;
  const auto n = point(hit.triangle.outward_normal);
  check(std::abs(length(n) - 1) < 1e-12 && dot(n, vertices[0]) > 0,
        "geometric normal not unit/outward");
  check(std::abs(dot(sub(hit.position, vertices[0]), n)) < 2e-8,
        "radial hit is not on triangle plane");
  check(length(cross(unit(point(input)), unit(hit.position))) < 1e-14,
        "hit moved off input radial ray");
  StreamPoint reconstructed{};
  for (unsigned i = 0; i < 3; ++i)
    reconstructed = add(reconstructed, mul(vertices[i], hit.barycentric[i]));
  check(length(sub(reconstructed, hit.position)) < 2e-8,
        "true triangle barycentrics do not reconstruct ray hit");
  // Independent Moller-Trumbore ray/triangle intersection; does not use the
  // provider's geometric-normal plane equation or Gram-matrix barycentrics.
  const auto ray = unit(point(input));
  const auto e1 = sub(vertices[1], vertices[0]),
             e2 = sub(vertices[2], vertices[0]);
  const auto p = cross(ray, e2);
  const auto determinant = dot(e1, p);
  const auto t = mul(vertices[0], -1);
  const auto q = cross(t, e1);
  const auto distance = dot(e2, q) / determinant;
  const auto y = dot(t, p) / determinant, z = dot(ray, q) / determinant;
  check(std::abs(distance - hit.radial_distance_metres) < 2e-7 &&
            std::abs(y - hit.barycentric[1]) < 2e-8 &&
            std::abs(z - hit.barycentric[2]) < 2e-8,
        "independent ray intersection disagrees");
}
auto mesh_contract(const PlanetDescriptor& planet) -> void {
  auto cache = require(TerrainTileCache::create(16));
  for (unsigned face = 0; face < 6; ++face) {
    const StreamKey key{face, 13, 4123, 3671};
    const auto mesh = build_stream_tile(planet, key, 8, 1, cache);
    // All 2048 triangles of one complete native top tile, and representative
    // triangles on every other face. Skirts begin after these top indices.
    for (unsigned cell = 0; cell < 1024; cell += face == 0 ? 1 : 127)
      for (unsigned half = 0; half < 2; ++half) {
        const ContactTriangleId id{planet.id, static_cast<CubeFace>(face),
                                   key.x,     key.y,
                                   cell % 32, cell / 32,
                                   half};
        const auto triangle = require(build_contact_triangle(
            planet, kExperimentalContactSurface, id, cache));
        const auto first = (cell * 2 + half) * 3;
        for (auto vertex : triangle.vertices) {
          bool found = false;
          for (unsigned corner = 0; corner < 3; ++corner) {
            const auto index =
                static_cast<unsigned>(mesh->indices[first + corner]);
            check(index < 33 * 33,
                  "top triangle unexpectedly references skirt");
            found |= vertex == add(mesh->anchor, mesh->vertices[index]);
          }
          check(
              found,
              "contact triangle differs from unmodified native mesh geometry");
        }
        const auto a = mesh->vertices[mesh->indices[first]];
        const auto b = mesh->vertices[mesh->indices[first + 1]];
        const auto c = mesh->vertices[mesh->indices[first + 2]];
        const auto expected = mul(unit(cross(sub(b, a), sub(c, a))), -1);
        check(
            dot(expected, point(triangle.outward_normal)) > 1 - 1e-12,
            "contact normal differs from actual triangle (not shading normal)");
      }
  }
}
auto queries_contract(const PlanetDescriptor& planet) -> void {
  auto cache = require(TerrainTileCache::create(1));
  auto other = require(TerrainTileCache::create(8));
  std::vector<PlanetFixedDirection> directions{{1, 0, 0},
                                               {-1, 0, 0},
                                               {0, 1, 0},
                                               {0, -1, 0},
                                               {0, 0, 1},
                                               {0, 0, -1},
                                               {1, .31415, -.27182},
                                               {-1, .123, .456},
                                               {1e-12, 0, 0},
                                               {1e9, 0, 0}};
  // All face edges/corners, both sides of each tie, and adjacent representable
  // directions. Normals can legitimately change at edges; positions cannot.
  for (double x : {-1.0, 0.0, 1.0})
    for (double y : {-1.0, 0.0, 1.0})
      for (double z : {-1.0, 0.0, 1.0})
        if (x != 0 || y != 0 || z != 0) directions.push_back({x, y, z});
  // Off-midpoint seams catch face-basis/sign errors hidden by symmetric cube
  // corners. Include XY, XZ and YZ edges, not only X-dominant perturbations.
  for (double first : {-1.0, 1.0})
    for (double second : {-1.0, 1.0})
      for (double along : {-.75, -.31415, .125, .819}) {
        directions.push_back({first, second, along});
        directions.push_back({first, along, second});
        directions.push_back({along, first, second});
      }
  for (const auto d : directions) {
    const auto hit = require(
        query_contact_surface(planet, kExperimentalContactSurface, d, cache));
    geometric_contract(hit, d);
    for (const auto member :
         {&PlanetFixedDirection::x, &PlanetFixedDirection::y,
          &PlanetFixedDirection::z})
      for (const auto toward : {-2.0, 2.0}) {
        auto adjacent = d;
        adjacent.*member = std::nextafter(d.*member, toward);
        const auto maximum = std::max(
            {std::abs(adjacent.x), std::abs(adjacent.y), std::abs(adjacent.z)});
        if (maximum < 1e-12 || maximum > 1e9) continue;
        const auto near = require(query_contact_surface(
            planet, kExperimentalContactSurface, adjacent, cache));
        check(length(sub(hit.position, near.position)) < 1e-5,
              "adjacent representable directions crack at seam");
      }
    // Force unrelated source tiles into capacity-one cache before retry.
    (void)require(query_contact_surface(planet, kExperimentalContactSurface,
                                        {-d.z, d.x, d.y}, cache));
    const auto again = require(
        query_contact_surface(planet, kExperimentalContactSurface, d, cache));
    const auto independent = require(
        query_contact_surface(planet, kExperimentalContactSurface, d, other));
    check(hit == again && hit == independent,
          "cache order/capacity changed query bits");
    check(cache.size() <= 1 && other.size() <= 8,
          "query exceeded cache budget");
  }
  ContactTriangleId id{planet.id, CubeFace::positive_x, 5000, 3000, 31, 19, 0};
  for (const auto uv :
       {std::array{.25, .75}, std::array{1.0, .25}, std::array{1.0, 1.0}}) {
    const auto d = cell_direction(id, uv[0], uv[1]);
    const auto center = require(
        query_contact_surface(planet, kExperimentalContactSurface, d, cache));
    geometric_contract(center, d);
    for (double delta : {-0x1p-36, 0x1p-36}) {
      auto nearby = d;
      nearby.y += delta;
      const auto hit = require(query_contact_surface(
          planet, kExperimentalContactSurface, nearby, cache));
      geometric_contract(hit, nearby);
      check(length(sub(center.position, hit.position)) < .01,
            "cell/tile/diagonal edge is discontinuous");
    }
  }
  // Experiment-one bit goldens observed identically in independent GCC and
  // Clang runs. Analytic/mesh tests above independently qualify the geometry.
  constexpr std::array<std::uint64_t, 3> golden{
      8647641455011721912ULL, 13081107393735833342ULL, 9542093610058446988ULL};
  unsigned fixture = 0;
  for (const auto d :
       {PlanetFixedDirection{1, .31415, -.27182}, {-1, 1, 1}, {0, 0, -1}}) {
    const auto hit = require(
        query_contact_surface(planet, kExperimentalContactSurface, d, cache));
    const auto observed = hash(hit);
    std::cout << "contact golden " << observed << '\n';
    check(observed == golden[fixture++], "experimental contact golden changed");
  }
}
auto varied_planets_contract() -> void {
  auto cache = require(TerrainTileCache::create(1));
  for (auto seed : {std::uint64_t{0}, std::uint64_t{7}, std::uint64_t{1337},
                    std::numeric_limits<std::uint64_t>::max()}) {
    const auto planet = generate_planet_descriptor(Seed{seed});
    for (unsigned face = 0; face < 6; ++face) {
      const auto d = direction(face, -.4375, .6875);
      const auto hit = require(
          query_contact_surface(planet, kExperimentalContactSurface, d, cache));
      check(hit.triangle.id.planet == planet.id,
            "cross-planet cache reused wrong identity");
      geometric_contract(hit, d);
    }
  }
}
} // namespace

auto main() -> int {
  try {
    const auto planet = generate_planet_descriptor(Seed{42});
    invalid_contract(planet);
    ownership_contract(planet);
    mesh_contract(planet);
    queries_contract(planet);
    varied_planets_contract();
    std::cout << "experimental contact surface: PASS (not footprint/collision "
                 "proof)\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "experimental contact surface: " << exception.what() << '\n';
    return 1;
  }
}
