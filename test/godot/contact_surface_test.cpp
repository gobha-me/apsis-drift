#include "../../src/godot/contact_geometry_internal.hpp"
#include "apsis_drift/godot/contact_surface.hpp"
#include "apsis_drift/godot/streaming.hpp"
#include "apsis_drift/terrain_sea_surface.hpp"

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
auto owned_hash(const ExperimentalOwnedSurfacePoint& hit) -> std::uint64_t {
  auto result = hash(hit.point);
  const auto& owner = hit.owner;
  const std::array<std::uint64_t, 9> fields{
      owner.format,
      owner.system.value,
      static_cast<unsigned>(owner.catalog_kind),
      owner.planet.value,
      owner.seed_derivation,
      owner.system_generator,
      owner.planet_generator,
      static_cast<unsigned>(owner.descriptor_variant),
      owner.origin_home_generator};
  for (auto field : fields)
    for (unsigned i = 0; i < 8; ++i) {
      result ^= (field >> (8 * i)) & 255U;
      result *= 1099511628211ULL;
    }
  return result;
}
auto owned_contract() -> void {
  const auto home = generate_origin_system(Seed{42});
  const auto procedural = generate_local_system(home.seed);
  const auto& home_planet = home.planets.front().descriptor;
  const auto& plain_planet = procedural.planets.front().descriptor;
  const auto recipe = kExperimentalContactSurface;
  constexpr PlanetFixedDirection d{1, .31415, -.27182};
  check(home_planet.id == plain_planet.id && home_planet != plain_planet,
        "origin variant fixture must share ID but not descriptor");
  auto home_cache = require(TerrainTileCache::create(1));
  auto plain_cache = require(TerrainTileCache::create(1));
  const auto home_id =
      require(locate_owned_contact_triangle(home, home_planet.id, recipe, d));
  const auto plain_id = require(
      locate_owned_contact_triangle(procedural, plain_planet.id, recipe, d));
  check(home_id.triangle == plain_id.triangle && home_id != plain_id,
        "same-ID descriptor variants lost qualified identity");
  check(home_id.owner.system == home.id &&
            home_id.owner.planet == home_planet.id &&
            home_id.owner.catalog_kind == LocalSystemKind::origin_home &&
            home_id.owner.descriptor_variant ==
                ContactDescriptorVariant::origin_home &&
            home_id.owner.origin_home_generator ==
                kOriginHomePlanetGeneratorVersion &&
            plain_id.owner.origin_home_generator == 0 &&
            plain_id.owner.descriptor_variant ==
                ContactDescriptorVariant::procedural,
        "resolved context owner has incorrect generator/variant provenance");
  const auto home_hit = require(
      query_owned_contact_surface(home, home_planet.id, recipe, d, home_cache));
  const auto plain_hit = require(query_owned_contact_surface(
      procedural, plain_planet.id, recipe, d, plain_cache));
  check(home_hit.point.position != plain_hit.point.position,
        "authored terrain silently substituted procedural geometry");
  const auto standalone =
      require(query_contact_surface(plain_planet, recipe, d, plain_cache));
  check(plain_hit.point == standalone,
        "context wrapper changed procedural geometry bits");
  error(query_contact_surface(home_planet, recipe, d, home_cache),
        ContactSurfaceError::invalid_planet,
        "standalone origin refusal contract changed");
  check(require(build_owned_contact_triangle(home, home_id, home_cache))
                .triangle == home_hit.point.triangle,
        "owned extraction and radial query disagree");
  geometric_contract(home_hit.point, d);

  auto reject_id = [&](ExperimentalOwnedTriangleId id,
                       ContactSurfaceError wanted) {
    auto untouched = require(TerrainTileCache::create(1));
    error(build_owned_contact_triangle(home, id, untouched), wanted,
          "forged owner/triangle accepted");
    check(untouched.size() == 0, "invalid owner accessed terrain cache");
  };
  for (auto member : {&ExperimentalContactOwner::format,
                      &ExperimentalContactOwner::seed_derivation,
                      &ExperimentalContactOwner::system_generator,
                      &ExperimentalContactOwner::planet_generator,
                      &ExperimentalContactOwner::origin_home_generator}) {
    auto bad = home_id;
    ++(bad.owner.*member);
    reject_id(bad, ContactSurfaceError::owner_mismatch);
  }
  auto bad = home_id;
  bad.owner.system.value ^= 1;
  reject_id(bad, ContactSurfaceError::owner_mismatch);
  bad = home_id;
  bad.owner.catalog_kind = LocalSystemKind::procedural;
  reject_id(bad, ContactSurfaceError::owner_mismatch);
  bad = home_id;
  bad.owner.descriptor_variant = ContactDescriptorVariant::procedural;
  reject_id(bad, ContactSurfaceError::owner_mismatch);
  bad = home_id;
  bad.owner.descriptor_variant = static_cast<ContactDescriptorVariant>(255);
  reject_id(bad, ContactSurfaceError::owner_mismatch);
  bad = home_id;
  bad.owner.planet.value ^= 1;
  reject_id(bad, ContactSurfaceError::unknown_context_planet);
  bad = home_id;
  bad.triangle.planet.value ^= 1;
  reject_id(bad, ContactSurfaceError::wrong_planet);
  bad = home_id;
  bad.triangle.cell_x = 32;
  reject_id(bad, ContactSurfaceError::invalid_triangle);
  bad = home_id;
  bad.recipe.version = 2;
  reject_id(bad, ContactSurfaceError::unsupported_recipe);
  auto untouched = require(TerrainTileCache::create(1));
  error(build_owned_contact_triangle(procedural, home_id, untouched),
        ContactSurfaceError::owner_mismatch,
        "qualified home triangle accepted in same-seed procedural catalog");
  auto wrong_catalog = home;
  wrong_catalog.kind = LocalSystemKind::procedural;
  error(query_owned_contact_surface(wrong_catalog, home_planet.id, recipe, d,
                                    untouched),
        ContactSurfaceError::invalid_context,
        "authored override under wrong catalog kind accepted");
  auto wrong_owner = home;
  wrong_owner.id.value ^= 1;
  error(query_owned_contact_surface(wrong_owner, home_planet.id, recipe, d,
                                    untouched),
        ContactSurfaceError::invalid_context,
        "catalog owner/seed mismatch accepted");
  auto wrong_ordinal = home;
  wrong_ordinal.planets.front().orbit.ordinal = 1;
  error(query_owned_contact_surface(wrong_ordinal, home_planet.id, recipe, d,
                                    untouched),
        ContactSurfaceError::invalid_context,
        "wrong authored ordinal accepted");
  const PlanetDescriptor plausible_override{
      home_planet.seed,
      home_planet.id,
      home_planet.display_name,
      PlanetRadiusKm{home_planet.radius.value ==
                             kOriginHomeMaximumRadiusKilometres
                         ? home_planet.radius.value - 1
                         : home_planet.radius.value + 1},
      home_planet.surface_gravity,
      home_planet.atmosphere_class,
      home_planet.atmosphere_pressure,
      home_planet.terrain_character,
      home_planet.water_coverage,
      home_planet.palette};
  check(is_tutorial_safe_home_planet(plausible_override),
        "forged descriptor fixture must remain within tutorial-safe ranges");
  auto forged_catalog = home;
  std::vector<LocalSystemPlanet> forged_planets;
  forged_planets.push_back({plausible_override, home.planets.front().orbit});
  for (std::size_t i = 1; i < home.planets.size(); ++i)
    forged_planets.push_back(home.planets[i]);
  forged_catalog.planets = std::move(forged_planets);
  error(query_owned_contact_surface(forged_catalog, home_planet.id, recipe, d,
                                    untouched),
        ContactSurfaceError::invalid_context,
        "tutorial-safe fields substituted for exact authored descriptor");
  error(query_owned_contact_surface(home, PlanetId{0}, recipe, d, untouched),
        ContactSurfaceError::unknown_context_planet,
        "unknown planet resolved by replacement");
  error(query_owned_contact_surface(
            home, home_planet.id, recipe,
            {std::numeric_limits<double>::quiet_NaN(), 0, 0}, untouched),
        ContactSurfaceError::invalid_direction,
        "owned path accepted nonfinite ray");
  check(untouched.size() == 0, "invalid catalog/direction sampled terrain");

  // Retain the cache's deliberate descriptor-collision refusal. No implicit
  // replacing/flushing a still-valid procedural cache to accommodate origin.
  error(
      query_owned_contact_surface(home, home_planet.id, recipe, d, plain_cache),
      ContactSurfaceError::sampling_failure,
      "same-key different-descriptor cache silently replaced");
  check(require(query_owned_contact_surface(procedural, plain_planet.id, recipe,
                                            d, plain_cache)) == plain_hit,
        "cache conflict corrupted previous context's geometry");
  (void)require(query_owned_contact_surface(home, home_planet.id, recipe,
                                            {-1, .2, .1}, home_cache));
  check(require(query_owned_contact_surface(home, home_planet.id, recipe, d,
                                            home_cache)) == home_hit,
        "owned cache eviction changed geometry/provenance");
  // Other planets in an origin catalog remain procedural, not authored-home.
  const auto& neighbor = home.planets[1].descriptor;
  auto neighbor_cache = require(TerrainTileCache::create(1));
  const auto neighbor_hit = require(query_owned_contact_surface(
      home, neighbor.id, recipe, d, neighbor_cache));
  check(neighbor_hit.owner.descriptor_variant ==
                ContactDescriptorVariant::procedural &&
            neighbor_hit.owner.origin_home_generator == 0 &&
            neighbor_hit.owner.catalog_kind == LocalSystemKind::origin_home &&
            neighbor_hit.point == require(query_contact_surface(
                                      neighbor, recipe, d, neighbor_cache)),
        "origin catalog incorrectly overrides non-home planet");

  auto mesh_cache = require(TerrainTileCache::create(8));
  for (unsigned face = 0; face < 6; ++face) {
    const StreamKey key{face, 13, 4123, 3671};
    const auto mesh = build_stream_tile(home_planet, key, 8, 1, mesh_cache);
    for (unsigned cell : {0U, 31U, 511U, 1023U})
      for (unsigned half : {0U, 1U}) {
        auto id = home_id;
        id.triangle = {home_planet.id,
                       static_cast<CubeFace>(face),
                       key.x,
                       key.y,
                       cell % 32,
                       cell / 32,
                       half};
        const auto triangle =
            require(build_owned_contact_triangle(home, id, mesh_cache));
        const auto first = (cell * 2 + half) * 3;
        for (auto vertex : triangle.triangle.vertices) {
          bool found = false;
          for (unsigned i = 0; i < 3; ++i)
            found |= vertex == add(mesh->anchor,
                                   mesh->vertices[mesh->indices[first + i]]);
          check(found, "context home vertex differs from unchanged native "
                       "stream builder");
        }
      }
  }
  for (double x : {-1.0, 0.0, 1.0})
    for (double y : {-1.0, 0.0, 1.0})
      for (double z : {-1.0, 0.0, 1.0}) {
        if (x == 0 && y == 0 && z == 0) continue;
        const PlanetFixedDirection ray{x, y, z};
        const auto hit = require(query_owned_contact_surface(
            home, home_planet.id, recipe, ray, home_cache));
        geometric_contract(hit.point, ray);
      }
  std::cout << "owned origin golden " << owned_hash(home_hit) << '\n';
  std::cout << "owned procedural golden " << owned_hash(plain_hit) << '\n';
  // Owner-inclusive new fixtures observed identically in GCC/Clang runs.
  check(owned_hash(home_hit) == 17524982012417750703ULL,
        "experimental owned origin golden changed");
  check(owned_hash(plain_hit) == 13132756775747483476ULL,
        "experimental owned procedural golden changed");
}
auto selected_physical_contract() -> void {
  check(!saved_contact_recipe(0) && !saved_contact_recipe(4) &&
            !saved_contact_recipe(std::numeric_limits<std::uint32_t>::max()) &&
            saved_contact_recipe(1) == kExperimentalSavedContactSurface &&
            saved_contact_recipe(2) == kLocalDetailSavedContactSurface &&
            saved_contact_recipe(3) == kSeaDatumSavedContactSurface,
        "terrain policy mapping admitted an unsupported selection");
  const auto owner = require(generate_physical_origin_system(Seed{42}, 2));
  const auto& planet =
      owner.catalog.planets[kOriginHomePlanetOrdinal].descriptor;
  auto cache = require(TerrainTileCache::create(4));
  ContactTriangleId id{planet.id, CubeFace::positive_x, 4095, 4096, 17, 21, 0};
  error(apsis_drift::godot_spike::detail::build_selected_contact_triangle(
            planet, kLocalDetailSavedContactSurface, id, cache),
        ContactSurfaceError::unsupported_recipe,
        "descriptor-only path admitted selected local detail");
  auto forged = owner;
  ++forged.stellar_gm_km3_per_second2;
  error(apsis_drift::godot_spike::detail::build_selected_contact_triangle(
            forged, planet, kLocalDetailSavedContactSurface, id, cache),
        ContactSurfaceError::invalid_context, "forged physical owner accepted");
  auto invalid_recipe = kLocalDetailSavedContactSurface;
  invalid_recipe.diagonal = 99;
  error(apsis_drift::godot_spike::detail::build_selected_contact_triangle(
            owner, planet, invalid_recipe, id, cache),
        ContactSurfaceError::unsupported_recipe,
        "unknown selected topology accepted");
  auto invalid_id = id;
  invalid_id.half = 2;
  error(apsis_drift::godot_spike::detail::build_selected_contact_triangle(
            owner, planet, kLocalDetailSavedContactSurface, invalid_id, cache),
        ContactSurfaceError::invalid_triangle, "bad selected half accepted");
  const auto namesake = generate_planet_descriptor(planet.seed);
  check(namesake != planet,
        "authored owner fixture collapsed into its namesake");
  error(apsis_drift::godot_spike::detail::build_selected_contact_triangle(
            owner, namesake, kLocalDetailSavedContactSurface, id, cache),
        ContactSurfaceError::owner_mismatch,
        "procedural namesake replaced authored home");
  check(cache.size() == 0, "refused selected geometry accessed source cache");

  constexpr unsigned n = kStreamIntervals + 1;
  constexpr std::array<std::array<unsigned, 3>, 2> offsets{
      {{0, 1, n}, {1, n + 1, n}}};
  for (unsigned face = 0; face < 6; ++face) {
    const StreamKey key{face, 13, id.tile_x, id.tile_y};
    const auto tile =
        build_stream_tile(owner, body_id(planet.id), key, 2, cache);
    const auto old = build_stream_tile(planet, key, 8, 0, cache);
    const auto owned_old =
        build_stream_tile(owner, body_id(planet.id), key, 1, cache);
    check(old->vertices == owned_old->vertices &&
              old->normals == owned_old->normals &&
              old->indices == owned_old->indices &&
              old->elevations == owned_old->elevations,
          "owned legacy policy changed existing stream geometry");
    for (unsigned half = 0; half < 2; ++half) {
      id.face = static_cast<CubeFace>(face);
      id.half = half;
      const auto triangle = require(
          apsis_drift::godot_spike::detail::build_selected_contact_triangle(
              owner, planet, kLocalDetailSavedContactSurface, id, cache));
      const unsigned start = id.cell_y * n + id.cell_x;
      for (unsigned corner = 0; corner < 3; ++corner) {
        const auto streamed =
            add(tile->vertices[start + offsets[half][corner]], tile->anchor);
        check(
            length(sub(streamed, triangle.vertices[corner])) <= 2e-9,
            "finest selected stream vertex differs from physical contact top");
      }
      const unsigned index_offset =
          (id.cell_y * kStreamIntervals + id.cell_x) * 6 + half * 3;
      const auto a =
          tile->vertices[static_cast<std::size_t>(tile->indices[index_offset])];
      const auto b = tile->vertices[static_cast<std::size_t>(
          tile->indices[index_offset + 1])];
      const auto c = tile->vertices[static_cast<std::size_t>(
          tile->indices[index_offset + 2])];
      check(dot(cross(sub(b, a), sub(c, a)), point(triangle.outward_normal)) <
                0,
            "selected stream top winding is not Godot-clockwise relative to "
            "contact");
      const auto legacy_triangle = require(
          apsis_drift::godot_spike::detail::build_selected_contact_triangle(
              owner, planet, kExperimentalSavedContactSurface, id, cache));
      check(triangle.vertices != legacy_triangle.vertices,
            "new policy failed to select physical local detail");
    }
  }
}
auto sea_datum_helper_contract() -> void {
  for (const auto policy : {1U, 2U, 3U}) {
    check(valid_terrain_surface_policy(policy) &&
              require(terrain_surface_raw_recipe(policy)) ==
                  (policy == 3 ? 2U : policy),
          "sea-top policy did not retain its selected raw terrain recipe");
    for (const auto height : {-123.0, -0.0, 0.0, 123.0}) {
      const auto selected =
          require(select_terrain_sea_surface(policy, height, true));
      if (policy < 3)
        check(std::bit_cast<std::uint64_t>(selected.elevation_metres) ==
                      std::bit_cast<std::uint64_t>(height) &&
                  !selected.wet_datum,
              "legacy height or signed-zero arithmetic changed");
      else
        check(selected.elevation_metres == (height > 0 ? height : 0.0) &&
                  !std::signbit(selected.elevation_metres) &&
                  selected.wet_datum == (height <= 0),
              "sea-top height clamp or raw datum flag is incorrect");
    }
    for (const auto height : {-123.0, -0.0, 0.0, 123.0}) {
      const auto dry =
          require(select_terrain_sea_surface(policy, height, false));
      check(std::bit_cast<std::uint64_t>(dry.elevation_metres) ==
                    std::bit_cast<std::uint64_t>(height) &&
                !dry.wet_datum,
            "zero-water descriptor acquired sea geometry or material from "
            "relief");
    }
    for (const auto height : {std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity()}) {
      const auto result = select_terrain_sea_surface(policy, height, true);
      check(!result &&
                result.error() == TerrainSeaSurfaceError::non_finite_height,
            "nonfinite raw height entered selected geometry");
    }
  }
  for (const auto policy :
       {0U, 4U, std::numeric_limits<std::uint32_t>::max()}) {
    const auto raw = terrain_surface_raw_recipe(policy);
    const auto selected = select_terrain_sea_surface(policy, 12, true);
    check(!valid_terrain_surface_policy(policy) && !raw && !selected &&
              raw.error() == TerrainSeaSurfaceError::unsupported_policy &&
              selected.error() == TerrainSeaSurfaceError::unsupported_policy,
          "invalid policy entered raw or selected geometry");
  }
  for (unsigned mask = 0; mask < 8; ++mask) {
    const auto kind =
        require(classify_terrain_datum_mask(static_cast<std::uint8_t>(mask)));
    check(kind == (mask == 0   ? TerrainDatumClass::dry
                   : mask == 7 ? TerrainDatumClass::water
                               : TerrainDatumClass::shoreline),
          "all-wet or mixed raw datum mask was misclassified");
  }
  for (const auto mask : {8U, 128U, 255U}) {
    const auto kind =
        classify_terrain_datum_mask(static_cast<std::uint8_t>(mask));
    check(!kind && kind.error() == TerrainSeaSurfaceError::invalid_datum_mask,
          "malformed raw datum mask accepted");
  }
}
auto sea_datum_geometry_contract() -> void {
  using apsis_drift::godot_spike::detail::build_selected_contact_triangle;
  using apsis_drift::godot_spike::detail::query_selected_contact_surface;
  const auto dry_owner = require(generate_physical_origin_system(Seed{42}, 2));
  const auto& dry_planet =
      dry_owner.catalog.planets[kOriginHomePlanetOrdinal].descriptor;
  auto dry_cache = require(TerrainTileCache::create(4));
  // Existing landed-craft fixture: no site or seed search for this increment.
  const ContactTriangleId dry_id{
      dry_planet.id, CubeFace::positive_x, 4000, 4000, 12, 13, 0};
  const auto before = require(build_selected_contact_triangle(
      dry_owner, dry_planet, kLocalDetailSavedContactSurface, dry_id,
      dry_cache));
  const auto after = require(build_selected_contact_triangle(
      dry_owner, dry_planet, kSeaDatumSavedContactSurface, dry_id, dry_cache));
  check(before.datum_mask == 0 && after.datum_mask == 0 &&
            before.vertices == after.vertices &&
            before.outward_normal == after.outward_normal,
        "fully dry finest top changed when sea-datum policy was selected");
  error(build_selected_contact_triangle(
            dry_planet, kSeaDatumSavedContactSurface, dry_id, dry_cache),
        ContactSurfaceError::unsupported_recipe,
        "descriptor-only path admitted owned sea-datum geometry");

  const auto wet_owner = require(generate_physical_origin_system(Seed{4}));
  const auto& wet_planet =
      wet_owner.catalog.planets[kOriginHomePlanetOrdinal].descriptor;
  auto wet_cache = require(TerrainTileCache::create(4));
  // Exact existing terrain-touchdown water fixture (trial10), not a new search.
  const ContactTriangleId wet_id{
      wet_planet.id, CubeFace::positive_x, 4000, 2000, 12, 13, 0};
  const auto basin = require(build_selected_contact_triangle(
      wet_owner, wet_planet, kLocalDetailSavedContactSurface, wet_id,
      wet_cache));
  const auto sea = require(build_selected_contact_triangle(
      wet_owner, wet_planet, kSeaDatumSavedContactSurface, wet_id, wet_cache));
  const double radius = wet_planet.radius.value * 1000.0;
  check(sea.datum_mask == 7 && require(classify_terrain_datum_mask(
                                   sea.datum_mask)) == TerrainDatumClass::water,
        "known submerged basin did not retain all three raw wet flags");
  for (unsigned i = 0; i < 3; ++i)
    check(length(basin.vertices[i]) < radius &&
              std::abs(length(sea.vertices[i]) - radius) < 1e-8,
          "wet selected vertex was not raised from the basin to sea datum");
  const auto ray = cell_direction(wet_id, .3, .3);
  const auto hit = require(query_selected_contact_surface(
      wet_owner, wet_planet, kSeaDatumSavedContactSurface, ray, wet_cache));
  check(hit.triangle == sea && hit.radial_distance_metres < radius &&
            radius - hit.radial_distance_metres < .01,
        "radial first surface ignored the actual sea facet's chord sag");
  geometric_contract(hit, ray);
  check(require(query_selected_contact_surface(wet_owner, wet_planet,
                                               kSeaDatumSavedContactSurface,
                                               ray, wet_cache)) == hit,
        "owned sea-top query changed on repeat");
  auto untouched = require(TerrainTileCache::create(1));
  error(query_selected_contact_surface(
            wet_owner, wet_planet, kSeaDatumSavedContactSurface,
            {std::numeric_limits<double>::quiet_NaN(), 0, 0}, untouched),
        ContactSurfaceError::invalid_direction,
        "owned sea-top radial query accepted a nonfinite direction");
  check(untouched.size() == 0,
        "invalid sea-top radial query touched the terrain cache");
}
} // namespace

auto main() -> int {
  try {
    const auto planet = generate_planet_descriptor(Seed{42});
    sea_datum_helper_contract();
    sea_datum_geometry_contract();
    selected_physical_contract();
    invalid_contract(planet);
    ownership_contract(planet);
    mesh_contract(planet);
    queries_contract(planet);
    varied_planets_contract();
    owned_contract();
    std::cout << "experimental contact surface: PASS (not footprint/collision "
                 "proof)\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "experimental contact surface: " << exception.what() << '\n';
    return 1;
  }
}
