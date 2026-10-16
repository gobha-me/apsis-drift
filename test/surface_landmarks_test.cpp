#include "apsis_drift/surface_landmarks.hpp"

#include "../src/godot/contact_geometry_internal.hpp"
#include "apsis_drift/godot/saved_contact_geometry.hpp"
#include "apsis_drift/terrain_sea_surface.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace apsis_drift;
using Point = PlanetFixedPositionMetres;
using Error = SurfaceLandmarkError;
int checks{}, failures{};
auto check(bool good, std::string_view message) -> void {
  ++checks;
  if (!good) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Surface landmark fixture refused");
  return std::move(*value);
}
template <class T>
auto error_is(const std::expected<T, Error>& value, Error error) -> bool {
  return !value && value.error() == error;
}
auto distance(Point a, Point b) -> double {
  return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}
auto cell(Point p) -> SurfaceLandmarkCell {
  return {static_cast<std::int32_t>(std::floor(p.x / 512)),
          static_cast<std::int32_t>(std::floor(p.y / 512)),
          static_cast<std::int32_t>(std::floor(p.z / 512))};
}
auto reference(const PlanetDescriptor& planet, std::uint32_t tile_y) -> Point {
  return need(planet_fixed_from_terrain_address(
      planet, {{planet.id, CubeFace::positive_x, 13, 4000, tile_y}, .4, .4}));
}
auto cells_around(Point point) -> std::vector<SurfaceLandmarkCell> {
  const auto center = cell(point);
  std::vector<SurfaceLandmarkCell> result;
  for (std::int32_t x = -2; x <= 2; ++x)
    for (std::int32_t y = -2; y <= 2; ++y)
      for (std::int32_t z = -2; z <= 2; ++z)
        result.push_back({center.x + x, center.y + y, center.z + z});
  return result;
}
auto invalid_inputs() -> void {
  const auto owner = need(generate_physical_origin_system(Seed{42}, 2));
  const auto& planet = owner.catalog.planets.front().descriptor;
  const auto body = body_id(planet.id);
  check(error_is(SurfaceLandmarkField::create(owner, body, 3, 2),
                 Error::unsupported_version) &&
            error_is(identify_surface_landmark(planet, {}, 0),
                     Error::unsupported_version),
        "Unsupported field and identity versions refuse");
  for (auto policy : {0U, 4U, std::numeric_limits<std::uint32_t>::max()})
    check(error_is(SurfaceLandmarkField::create(owner, body, policy),
                   Error::invalid_policy),
          "Only existing selected terrain policies are accepted");
  auto bad_owner = owner;
  ++bad_owner.stellar_mass_millisolar;
  check(error_is(SurfaceLandmarkField::create(bad_owner, body, 3),
                 Error::invalid_owner),
        "Owner authentication rejects altered generated context");
  for (const auto bad_body : {BodyId{2, BodyKind::planet, planet.id.value},
                              BodyId{1, BodyKind::moon, planet.id.value},
                              BodyId{1, BodyKind::planet, 0}})
    check(error_is(SurfaceLandmarkField::create(owner, bad_body, 3),
                   Error::invalid_body),
          "Foreign, unsupported and nonplanet body identities refuse");
  auto field = need(SurfaceLandmarkField::create(owner, body, 3));
  check(field.cache_size() == 0 && field.terrain_cache_size() == 0,
        "Factory authenticates ownership without sampling terrain");
  for (const auto bad_cell :
       {SurfaceLandmarkCell{std::numeric_limits<std::int32_t>::min(), 0, 0},
        {0, 32768, 0},
        {0, 0, -32769}})
    check(error_is(field.site(bad_cell), Error::invalid_cell) &&
              error_is(identify_surface_landmark(planet, bad_cell),
                       Error::invalid_cell),
          "Unencodable signed cell coordinates refuse before lookup");
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto infinity = std::numeric_limits<double>::infinity();
  for (const auto bad_query : {Point{},
                               {nan, 0, 1},
                               {0, infinity, 1},
                               {1, 0, -infinity},
                               {1e300, 1, 1},
                               {1e12 + 1, 0, 0}})
    check(error_is(field.nearby(bad_query), Error::invalid_query),
          "Nonfinite, centre and unsafe query components refuse");
  check(field.cache_size() == 0 && field.terrain_cache_size() == 0,
        "Invalid cells and coordinates cannot populate either cache");
  check(need(field.nearby({1e10, 0, 0})).empty() && field.cache_size() == 0 &&
            field.terrain_cache_size() == 0,
        "A distant space view is empty without contact work");
  check(!need(field.site({0, 0, 0})) && field.cache_size() == 1 &&
            !need(field.site({0, 0, 0})) && field.cache_size() == 1 &&
            field.terrain_cache_size() == 0,
        "Rejected shell cells are memoized as empty without terrain samples");
  const PlanetDescriptor invalid_radius{planet.seed,
                                        planet.id,
                                        planet.display_name,
                                        {0},
                                        planet.surface_gravity,
                                        planet.atmosphere_class,
                                        planet.atmosphere_pressure,
                                        planet.terrain_character,
                                        planet.water_coverage,
                                        planet.palette};
  check(error_is(identify_surface_landmark(invalid_radius, {}),
                 Error::invalid_body),
        "Lightweight identity rejects an invalid reference sphere");
}

auto pure_identity() -> void {
  for (const auto seed : {Seed{0}, Seed{42}, Seed{UINT64_MAX}}) {
    const auto owner = need(generate_physical_origin_system(seed, 2));
    const auto& planet = owner.catalog.planets.front().descriptor;
    const auto cells = cells_around(reference(planet, 4000));
    std::vector<SurfaceLandmarkIdentity> identities;
    for (const auto c : cells) {
      const auto first = need(identify_surface_landmark(planet, c));
      check(first == need(identify_surface_landmark(planet, c)),
            "Pure identity is deterministic for every shell and empty cell");
      if (!first) continue;
      identities.push_back(*first);
      const auto& point = first->reference_position_metres;
      const double radius = static_cast<double>(planet.radius.value) * 1000;
      check(first->cell == c && cell(point) == c &&
                std::abs(std::hypot(point.x, point.y, point.z) - radius) <
                    1e-7 &&
                first->radius_metres >= 28 && first->radius_metres <= 52 &&
                static_cast<unsigned>(first->kind) <= 2,
            "Accepted identities own their original cell, reference sphere "
            "and fixed visual ranges");
      // Independently spell the published signed16 ordinal, including signs.
      const auto packed = static_cast<std::uint64_t>(c.x + 32768) |
                          (static_cast<std::uint64_t>(c.y + 32768) << 16U) |
                          (static_cast<std::uint64_t>(c.z + 32768) << 32U);
      const auto root =
          derive_seed(planet.seed, SeedDomain::surface_landmark, 1);
      check(first->id.value ==
                derive_seed(root, SeedDomain::surface_landmark, packed).value,
            "Serialized cell identity retains the exact versioned ordinal");
    }
    check(!identities.empty(),
          "Bounded sphere neighborhood has real identities");
    std::sort(identities.begin(), identities.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    check(std::adjacent_find(identities.begin(), identities.end(),
                             [](const auto& a, const auto& b) {
                               return a.id == b.id;
                             }) == identities.end(),
          "Distinct accepted cells have distinct IDs in the bounded fixture");
    std::reverse(identities.begin(), identities.end());
    for (const auto& identity : identities)
      check(need(identify_surface_landmark(planet, identity.cell)) == identity,
            "Reverse access order cannot consume another cell's random stream");
  }
  check(derive_seed(Seed{0}, SeedDomain::universe).value ==
                1291384648262051579ULL &&
            derive_seed(Seed{UINT64_MAX}, SeedDomain::encounter, UINT64_MAX)
                    .value == 11366853328773030509ULL,
        "Appending the landmark domain preserves existing seed goldens");
}

auto ground_and_cache() -> void {
  using namespace godot_spike;
  const auto owner = need(generate_physical_origin_system(Seed{42}, 2));
  const auto& planet = owner.catalog.planets.front().descriptor;
  const auto cells = cells_around(reference(planet, 4000));
  auto field = need(SurfaceLandmarkField::create(owner, body_id(planet.id), 3));
  auto reversed =
      need(SurfaceLandmarkField::create(owner, body_id(planet.id), 3));
  auto cache = need(TerrainTileCache::create(4));
  const auto source_key =
      TerrainTileKey{planet.id, CubeFace::positive_x, 8, 125, 125};
  const auto source_before =
      need(generate_terrain_tile(planet, source_key)).checksum();
  std::vector<SurfaceLandmarkSite> accepted;
  for (const auto c : cells)
    if (const auto site = need(field.site(c))) accepted.push_back(*site);
  check(!accepted.empty(),
        "Known dry contact neighborhood produces visitable sites");
  for (auto it = cells.rbegin(); it != cells.rend(); ++it)
    check(need(reversed.site(*it)) == need(field.site(*it)),
          "Site identity and support are independent of access/eviction order");
  for (const auto& site : accepted) {
    const auto& reference_point = site.identity.reference_position_metres;
    const auto hit = need(godot_spike::detail::query_selected_contact_surface(
        owner, planet, kSeaDatumSavedContactSurface,
        {reference_point.x, reference_point.y, reference_point.z}, cache));
    const auto& n = hit.triangle.outward_normal;
    const auto& a = hit.triangle.vertices.front();
    const double plane_error = n.x * (site.position_metres.x - a.x) +
                               n.y * (site.position_metres.y - a.y) +
                               n.z * (site.position_metres.z - a.z);
    check(site.position_metres == hit.position &&
              std::abs(plane_error) < 1e-7 && hit.triangle.datum_mask == 0 &&
              hit.radial_distance_metres > planet.radius.value * 1000.0,
          "Every actual site centre is on the selected dry TOP facet");
    const auto nearby = need(field.nearby(site.position_metres));
    check(!nearby.empty() && nearby.front() == site &&
              nearby.size() <= kMaximumNearbySurfaceLandmarks,
          "A visited site is first at zero range and output stays bounded");
    for (std::size_t i = 0; i < nearby.size(); ++i) {
      const auto current =
          distance(nearby[i].position_metres, site.position_metres);
      check(current <= kSurfaceLandmarkNearbyMetres,
            "Nearby sites use actual ground-to-query range");
      if (!i) continue;
      const auto previous =
          distance(nearby[i - 1].position_metres, site.position_metres);
      check(previous < current ||
                (previous == current &&
                 nearby[i - 1].identity.id < nearby[i].identity.id),
            "Nearby ordering is stable by actual distance then ID");
    }
    check(need(reversed.nearby(site.position_metres)) == nearby &&
              need(field.nearby(site.position_metres)) == nearby,
          "Repeated and independently cached queries return identical sites");
  }
  check(field.cache_size() <= kSurfaceLandmarkMemoCells &&
            reversed.cache_size() <= kSurfaceLandmarkMemoCells &&
            field.terrain_cache_size() <= 4 &&
            reversed.terrain_cache_size() <= 4 &&
            need(generate_terrain_tile(planet, source_key)).checksum() ==
                source_before,
        "Bounded memo/tile residency never changes the existing terrain "
        "generator");
}

auto wet_rejection() -> void {
  using namespace godot_spike;
  // Existing seed4/positive-X4000,2000 submerged basin contact fixture.
  const auto owner = need(generate_physical_origin_system(Seed{4}, 2));
  const auto& planet = owner.catalog.planets.front().descriptor;
  auto field = need(SurfaceLandmarkField::create(owner, body_id(planet.id), 3));
  auto cache = need(TerrainTileCache::create(4));
  unsigned wet{};
  for (const auto c : cells_around(reference(planet, 2000))) {
    const auto identity = need(identify_surface_landmark(planet, c));
    if (!identity) continue;
    const auto& p = identity->reference_position_metres;
    const auto hit = need(godot_spike::detail::query_selected_contact_surface(
        owner, planet, kSeaDatumSavedContactSurface, {p.x, p.y, p.z}, cache));
    if (hit.triangle.datum_mask == 0) continue;
    ++wet;
    check(
        !need(field.site(c)),
        "Wet and mixed raw datum facets cannot become exposed landmark sites");
  }
  check(wet != 0,
        "Existing submerged basin exercises actual wet candidate refusal");
}
} // namespace

auto main() -> int {
  try {
    invalid_inputs();
    pure_identity();
    ground_and_cache();
    wet_rejection();
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << e.what() << '\n';
  }
  std::cout << checks << " surface landmark checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
