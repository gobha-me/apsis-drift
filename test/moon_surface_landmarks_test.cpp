#include "apsis_drift/moon_surface_landmarks.hpp"

#include "apsis_drift/moon_surface_contact.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace {
using namespace apsis_drift;
using Point = MoonFixedPositionMetres;
using Cell = SurfaceLandmarkCell;
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
  if (!value) throw std::runtime_error("Moon landmark fixture refused");
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
template <class T>
auto error_is(const std::expected<T, Error>& value, Error e) -> bool {
  return !value && value.error() == e;
}
auto owner() -> PhysicalMoonDescriptor {
  const auto catalog = need(generate_secondary_body_catalog(
      need(generate_physical_origin_system(Seed{42}, 2))));
  return need(
      generate_physical_moon_descriptor(catalog, catalog.bodies[0].node.id));
}
auto cell(Point p) -> Cell {
  return {static_cast<std::int32_t>(std::floor(p.x / 512)),
          static_cast<std::int32_t>(std::floor(p.y / 512)),
          static_cast<std::int32_t>(std::floor(p.z / 512))};
}
auto cells_around(Cell c) -> std::array<Cell, 27> {
  std::array<Cell, 27> result{};
  std::size_t next{};
  for (std::int32_t x = -1; x <= 1; ++x)
    for (std::int32_t y = -1; y <= 1; ++y)
      for (std::int32_t z = -1; z <= 1; ++z)
        result[next++] = {c.x + x, c.y + y, c.z + z};
  return result;
}
auto distance(Point a, Point b) -> double {
  return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}
auto reference(const PhysicalMoonDescriptor& moon, MoonFixedDirection d)
    -> Point {
  const double scale = moon.radius_metres / std::hypot(d.x, d.y, d.z);
  return {d.x * scale, d.y * scale, d.z * scale};
}
constexpr std::array<MoonFixedDirection, 8> directions{
    MoonFixedDirection{1, 0, 0},
    {-1, 0, 0},
    {0, 1, 0},
    {0, -1, 0},
    {0, 0, 1},
    {0, 0, -1},
    {1, 1, 1},
    {-1, -1, -1}};

auto invalid_inputs() -> void {
  const auto moon = owner();
  check(error_is(MoonSurfaceLandmarkField::create(moon, 1, 2),
                 Error::unsupported_version) &&
            error_is(identify_moon_surface_landmark(moon, {}, 0),
                     Error::unsupported_version),
        "Identity and field reject unsupported exposure recipe");
  for (const auto policy : {0U, 2U, std::numeric_limits<std::uint32_t>::max()})
    check(error_is(MoonSurfaceLandmarkField::create(moon, policy),
                   Error::invalid_policy),
          "Only actual Moon terrain recipe1 is admitted");
  {
    auto bad = moon;
    ++bad.radius_metres;
    check(
        error_is(MoonSurfaceLandmarkField::create(bad), Error::invalid_owner) &&
            error_is(identify_moon_surface_landmark(bad, {}),
                     Error::invalid_owner),
        "Tampered source radius cannot authorize a field or lightweight "
        "identity");
  }
  {
    auto bad = moon;
    bad.body.kind = BodyKind::minor;
    check(
        error_is(MoonSurfaceLandmarkField::create(bad), Error::invalid_body) &&
            error_is(identify_moon_surface_landmark(bad, {}),
                     Error::invalid_body),
        "A minor identity never masquerades as a Moon");
  }
  {
    auto bad = moon;
    bad.body.version = 2;
    check(error_is(MoonSurfaceLandmarkField::create(bad), Error::invalid_body),
          "Unknown body identity version refuses");
  }
  {
    auto bad = moon;
    bad.body.value ^= 1;
    check(error_is(MoonSurfaceLandmarkField::create(bad), Error::invalid_owner),
          "Foreign Moon identity cannot bind the retained source catalog");
  }
  {
    auto bad = moon;
    bad.terrain_seed.value ^= 1;
    check(error_is(MoonSurfaceLandmarkField::create(bad), Error::invalid_owner),
          "Unbound terrain seed cannot become a new exposure owner");
  }
  {
    auto bad = moon;
    bad.water_coverage_basis_points = 1;
    check(error_is(MoonSurfaceLandmarkField::create(bad), Error::invalid_owner),
          "Dry negative-relief rule cannot be applied to a forged wet owner");
  }
  auto field = need(MoonSurfaceLandmarkField::create(moon));
  check(field.owner() == moon && field.cache_size() == 0,
        "Factory owns exact immutable source without site work");
  for (const auto c :
       std::array<Cell, 4>{Cell{32768, 0, 0},
                           {0, -32769, 0},
                           {0, 0, std::numeric_limits<std::int32_t>::max()},
                           {std::numeric_limits<std::int32_t>::min(), 0, 0}})
    check(error_is(field.site(c), Error::invalid_cell) &&
              error_is(identify_moon_surface_landmark(moon, c),
                       Error::invalid_cell),
          "Signed16 cell bounds refuse before seed ordinal arithmetic");
  for (const auto c :
       std::array<Cell, 2>{Cell{-32768, -32768, -32768}, {32767, 32767, 32767}})
    check(
        !need(identify_moon_surface_landmark(moon, c)),
        "Representable extreme cells are safe empty identities, not overflow");
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto inf = std::numeric_limits<double>::infinity();
  for (const auto p : std::array<Point, 6>{Point{},
                                           {nan, 0, 1},
                                           {0, inf, 1},
                                           {1, 1, -inf},
                                           {1e12 + 1, 0, 0},
                                           {1e300, 1, 1}})
    check(error_is(field.nearby(p), Error::invalid_query),
          "Invalid/nonfinite/excessive query refuses before memo work");
  check(field.cache_size() == 0, "All refused queries leave memo untouched");
  check(need(field.nearby({1e10, 0, 0})).empty() && field.cache_size() == 0,
        "Distant orbital query returns empty without visiting cells");
  check(!need(field.site({0, 0, 0})) && field.cache_size() == 1 &&
            !need(field.site({0, 0, 0})) && field.cache_size() == 1,
        "Empty reference-shell result is memoized");
  for (std::int32_t i = 1; i <= 80; ++i)
    static_cast<void>(need(field.site({i, 0, 0})));
  check(field.cache_size() == 64 && !need(field.site({0, 0, 0})) &&
            field.cache_size() == 64,
        "Evicted empty cells rebuild with bounded64-entry FIFO residency");
}

auto identity_and_sites() -> void {
  const auto moon = owner();
  const auto saved = moon;
  const auto sampler = need(MoonTerrainSampler::create(moon));
  auto field = need(MoonSurfaceLandmarkField::create(moon));
  auto reverse = need(MoonSurfaceLandmarkField::create(moon));
  std::vector<Cell> all_cells;
  std::vector<MoonSurfaceLandmarkSite> accepted;
  std::size_t identities{}, below_datum{};
  std::array<MoonTerrainSample, 8> terrain_before{};
  for (std::size_t i = 0; i < directions.size(); ++i)
    terrain_before[i] = need(sampler.sample(directions[i]));
  // Fixed source42 direction corpus from the existing dry-basin contract.
  // This corroborates provider semantics, not a reachable player route.
  for (const auto direction : directions) {
    const auto cells = cells_around(cell(reference(moon, direction)));
    all_cells.insert(all_cells.end(), cells.begin(), cells.end());
    for (const auto c : cells) {
      const auto id = need(identify_moon_surface_landmark(moon, c));
      check(id == need(identify_moon_surface_landmark(moon, c)),
            "Lightweight identity is deterministic");
      const auto site = need(field.site(c));
      if (!id) {
        check(!site, "Empty identity cannot acquire a terrain-backed site");
        continue;
      }
      ++identities;
      const auto packed = static_cast<std::uint64_t>(c.x + 32768) |
                          (static_cast<std::uint64_t>(c.y + 32768) << 16U) |
                          (static_cast<std::uint64_t>(c.z + 32768) << 32U);
      const auto recipe =
          derive_seed(moon.terrain_seed, SeedDomain::moon_surface_landmark, 1);
      const auto seed =
          derive_seed(recipe, SeedDomain::moon_surface_landmark, packed);
      const auto child = [&](std::uint64_t n) {
        return derive_seed(seed, SeedDomain::moon_surface_landmark, n).value;
      };
      check(id->id.value == seed.value && id->cell == c &&
                cell(id->reference_position_metres) == c &&
                id->radius_metres == 28 + static_cast<double>(child(4) % 25) &&
                static_cast<unsigned>(id->kind) == child(3) % 3,
            "Independent exact ordinal/stream/kind/radius derivation");
      const long double px =
          static_cast<long double>(c.x) * 512 + 128 + child(0) % 257;
      const long double py =
          static_cast<long double>(c.y) * 512 + 128 + child(1) % 257;
      const long double pz =
          static_cast<long double>(c.z) * 512 + 128 + child(2) % 257;
      const long double length = std::sqrt(px * px + py * py + pz * pz);
      const auto& r = id->reference_position_metres;
      const long double tolerance =
          16 * std::numeric_limits<double>::epsilon() * moon.radius_metres;
      check(std::abs(r.x - px * moon.radius_metres / length) <= tolerance &&
                std::abs(r.y - py * moon.radius_metres / length) <= tolerance &&
                std::abs(r.z - pz * moon.radius_metres / length) <= tolerance,
            "Independent long-double reference sphere and middle-half jitter");
      constexpr std::array<Rgb8, 3> palette{
          Rgb8{230, 222, 183}, {195, 106, 38}, {38, 45, 54}};
      check(id->tint == palette[child(3) % 3] && id->radius_metres >= 28 &&
                id->radius_metres <= 52,
            "Bounded existing pale/ochre/dark appearance");
      const auto hit =
          need(query_moon_contact_surface(sampler, {r.x, r.y, r.z}));
      const auto& n = hit.triangle.outward_normal;
      const long double radial_length = std::hypot(
          static_cast<long double>(r.x), static_cast<long double>(r.y),
          static_cast<long double>(r.z));
      const long double alignment = (n.x * static_cast<long double>(r.x) +
                                     n.y * static_cast<long double>(r.y) +
                                     n.z * static_cast<long double>(r.z)) /
                                    radial_length;
      if (!site) {
        check(alignment < .8191520442889918L + 1e-14L,
              "Only unsuitable actual centre slope is omitted");
        continue;
      }
      accepted.push_back(*site);
      check(site->identity == *id && site->position_metres == hit.position &&
                hit.triangle.id.body == moon.body &&
                alignment >= .8191520442889918L - 1e-14L,
            "Actual selected Moon facet, genuine BodyId and unchanged35degree "
            "centre policy");
      const auto& a = hit.triangle.vertices[0];
      const auto& b = hit.triangle.vertices[1];
      const auto& d = hit.triangle.vertices[2];
      const auto& p = site->position_metres;
      const long double ux = static_cast<long double>(b.x) - a.x,
                        uy = static_cast<long double>(b.y) - a.y,
                        uz = static_cast<long double>(b.z) - a.z;
      const long double vx = static_cast<long double>(d.x) - a.x,
                        vy = static_cast<long double>(d.y) - a.y,
                        vz = static_cast<long double>(d.z) - a.z;
      const long double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz,
                        nz = ux * vy - uy * vx;
      const long double norm = std::sqrt(nx * nx + ny * ny + nz * nz);
      const long double plane = (nx * (static_cast<long double>(p.x) - a.x) +
                                 ny * (static_cast<long double>(p.y) - a.y) +
                                 nz * (static_cast<long double>(p.z) - a.z)) /
                                norm;
      check(std::abs(plane) < 1e-7L,
            "Independent triangle-plane corroboration, not continuous-height "
            "replacement");
      if (hit.radial_distance_metres < moon.radius_metres) {
        ++below_datum;
        check(need(field.site(c)).has_value(),
              "Genuine negative lunar relief remains dry and eligible");
      }
      auto fresh = need(MoonSurfaceLandmarkField::create(moon));
      const auto nearby = need(fresh.nearby(p));
      check(fresh.cache_size() == 27 && !nearby.empty() &&
                nearby.front() == *site && nearby.size() <= 8,
            "Near query evaluates exactly27 candidate cells and returns at "
            "most8 real sites");
      for (std::size_t i = 0; i < nearby.size(); ++i) {
        const double range = distance(nearby[i].position_metres, p);
        check(range <= 384, "Range uses actual selected top positions");
        if (i) {
          const double previous = distance(nearby[i - 1].position_metres, p);
          check(previous < range ||
                    (previous == range &&
                     nearby[i - 1].identity.id < nearby[i].identity.id),
                "Nearby ordering is deterministic distance then ID");
        }
      }
      check(nearby == need(fresh.nearby(p)),
            "Repeated near query has exact same output");
    }
  }
  check(identities > 0 && !accepted.empty(),
        "Fixed genuine Moon corpus has identities and actual sites");
  check(below_datum > 0,
        "Fixed genuine Moon corpus exercises eligible below-datum sites");
  for (auto it = all_cells.rbegin(); it != all_cells.rend(); ++it)
    check(need(reverse.site(*it)) == need(field.site(*it)),
          "Reverse access and eviction preserve site identity/geometry");
  check(field.cache_size() <= 64 && reverse.cache_size() <= 64 &&
            field.owner() == saved && moon == saved,
        "Both bounded fields retain source ownership unchanged");
  for (std::size_t i = 0; i < directions.size(); ++i)
    check(need(sampler.sample(directions[i])) == terrain_before[i],
          "Landmark generation leaves selected terrain samples unchanged");
  check(derive_seed(Seed{0}, SeedDomain::universe).value ==
                1291384648262051579ULL &&
            derive_seed(Seed{UINT64_MAX}, SeedDomain::encounter, UINT64_MAX)
                    .value == 11366853328773030509ULL &&
            static_cast<std::uint64_t>(SeedDomain::moon_surface) == 17 &&
            static_cast<std::uint64_t>(SeedDomain::surface_landmark) == 15 &&
            static_cast<std::uint64_t>(SeedDomain::moon_surface_landmark) == 18,
        "Appending Moon exposure domain preserves previous seed words and "
        "goldens");
  std::cout << "Fixed corpus identities " << identities << ", sites "
            << accepted.size() << ", below datum " << below_datum << '\n';
}
} // namespace
int main() {
  try {
    invalid_inputs();
    identity_and_sites();
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << error.what() << '\n';
  }
  std::cout << checks << " Moon landmark checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
