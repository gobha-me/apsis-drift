#include "apsis_drift/body_ephemeris.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <source_location>
#include <type_traits>
#include <utility>

namespace {
using namespace apsis_drift;
using Error = BodyEphemerisError;
int failures{};
auto check(bool value, std::string_view message,
           std::source_location where = std::source_location::current())
    -> void {
  if (value) return;
  ++failures;
  std::cerr << "FAIL line " << where.line() << ": " << message << '\n';
}
template <typename T, typename E>
auto need(const std::expected<T, E>& value,
          std::source_location where = std::source_location::current()) -> T {
  if (value) return *value;
  std::cerr << "Fixture refused at line " << where.line() << '\n';
  std::exit(1);
}
template <typename T>
auto error(const std::expected<T, Error>& value, Error wanted) -> bool {
  return !value && value.error() == wanted;
}
auto bits(double value) -> std::uint64_t {
  return std::bit_cast<std::uint64_t>(value);
}
template <typename Catalog> auto compatibility(const Catalog& catalog) -> void {
  const auto hierarchy = need(make_body_hierarchy(catalog));
  check(hierarchy.catalog.has_value(), "Compatibility owner missing");
  const auto& source = [&]() -> const LocalSystemDescriptor& {
    if constexpr (std::is_same_v<Catalog, LocalSystemDescriptor>)
      return catalog;
    else
      return catalog.catalog;
  }();
  for (const auto tick :
       {SimulationTick{0}, SimulationTick{25}, SimulationTick{720'000},
        std::numeric_limits<SimulationTick>::max() - 1,
        std::numeric_limits<SimulationTick>::max()}) {
    for (const double fraction : {0.0, 0.5}) {
      const EphemerisQueryTime time{tick, fraction};
      const auto resolved = resolve_body_ephemerides(hierarchy, time);
      if constexpr (std::is_same_v<Catalog, PhysicalLocalSystem>) {
        if (tick == std::numeric_limits<SimulationTick>::max()) {
          check(error(resolved, Error::invalid_time) &&
                    !resolve_planet_ephemeris(
                        catalog, source.planets.front().descriptor.id, time),
                "Physical reserved tick validation changed");
          continue;
        }
      }
      const auto actual = need(resolved);
      check(actual.size() == source.planets.size() + 1,
            "Catalog projection fabricated bodies");
      check(actual.front().body == body_id(source.star.id) &&
                actual.front().position == SystemPositionMetres{},
            "Source star moved or changed identity");
      for (const auto& planet : source.planets) {
        const auto expected =
            need(resolve_planet_ephemeris(catalog, planet.descriptor.id, time));
        const auto row = std::ranges::find(
            actual, body_id(planet.descriptor.id), &BodyEphemeris::body);
        check(row != actual.end(), "Compatible planet missing");
        if (row == actual.end()) continue;
        check(row->parent == body_id(source.star.id) &&
                  need(planet_id(row->body)) == planet.descriptor.id &&
                  row->cycle_tick == expected.cycle_tick &&
                  bits(row->phase_radians) == bits(expected.phase_radians),
              "Planet identity/phase changed");
        for (const auto& pair :
             {std::pair{row->position.x, expected.position.x},
              std::pair{row->position.y, expected.position.y},
              std::pair{row->position.z, expected.position.z},
              std::pair{row->velocity.x, expected.velocity.x},
              std::pair{row->velocity.y, expected.velocity.y},
              std::pair{row->velocity.z, expected.velocity.z}})
          check(bits(pair.first) == bits(pair.second),
                "Compatibility arithmetic/sign bits changed");
        const BodyTarget target{source.id, row->body};
        check(need(decode_body_target(need(encode_body_target(target)))) ==
                      target &&
                  validate_body_target(hierarchy, target).has_value(),
              "Actual catalog target did not round trip");
      }
      auto reversed = hierarchy;
      std::ranges::reverse(reversed.bodies);
      check(need(resolve_body_ephemerides(reversed, time)) == actual,
            "Compatible enumeration changed output ordering");
    }
  }
  auto forged = hierarchy;
  ++forged.bodies.back().orbit->radius_kilometres;
  check(error(validate_body_hierarchy(forged), Error::invalid_catalog),
        "Altered compatible orbit bypassed source ownership");
  forged.bodies = hierarchy.bodies;
  forged.system = hierarchy.system;
  ++forged.system.value;
  check(error(validate_body_hierarchy(forged), Error::invalid_catalog),
        "Wrong source system admitted");
}
auto declared_fixture() -> BodyHierarchy {
  const auto star = body_id(StarId{1});
  const auto planet = body_id(PlanetId{1});
  const BodyId moon{kBodyIdentityVersion, BodyKind::moon, 1};
  const BodyId minor{kBodyIdentityVersion, BodyKind::minor, 0};
  const CircularBodyOrbit orbit{kBodyHierarchyVersion,
                                kContinuousAnalyticEphemerisVersion,
                                10,
                                120,
                                0,
                                0,
                                0};
  return {kBodyHierarchyVersion,
          SystemId{7},
          {{moon, planet, orbit},
           {star, {}, {}},
           {minor, star, orbit},
           {planet, star, orbit}},
          {}};
}
auto hierarchy_checks() -> void {
  const auto fixture = declared_fixture();
  check(!fixture.catalog, "Declared geometry claims a generated owner");
  const auto initial = need(resolve_body_ephemerides(fixture, {0, 0}));
  check(initial[0].body.kind == BodyKind::star &&
            initial[1].body.kind == BodyKind::planet &&
            initial[2].body.kind == BodyKind::moon &&
            initial[3].body.kind == BodyKind::minor,
        "Canonical kind/word ordering lost");
  check(initial[1].position.x == 10'000 &&
            initial[2].relative_position.x == 10'000 &&
            initial[2].position.x == 20'000 && initial[3].position.x == 10'000,
        "Parent-relative translation or root/minor resolution wrong");
  check(initial[2].velocity.y == initial[1].velocity.y * 2 &&
            initial[2].relative_velocity == initial[1].relative_velocity,
        "Parent-relative velocity omitted");
  auto shuffled = fixture;
  std::ranges::sort(shuffled.bodies, {}, &HierarchyBody::id);
  std::size_t permutations{};
  do {
    check(need(resolve_body_ephemerides(shuffled, {31, .25})) ==
              need(resolve_body_ephemerides(fixture, {31, .25})),
          "Declaration order changed composed results");
    ++permutations;
  } while (
      std::ranges::next_permutation(shuffled.bodies, {}, &HierarchyBody::id)
          .found);
  check(permutations == 24, "Did not cover all declaration orders");
  auto bad = fixture;
  bad.bodies.push_back(bad.bodies.front());
  check(error(validate_body_hierarchy(bad), Error::duplicate_identity),
        "Duplicate body admitted");
  bad.bodies = fixture.bodies;
  bad.system = fixture.system;
  bad.version = fixture.version;
  bad.bodies[0].parent->value = 99;
  check(error(validate_body_hierarchy(bad), Error::missing_parent),
        "Missing parent admitted");
  bad.bodies = fixture.bodies;
  bad.system = fixture.system;
  bad.version = fixture.version;
  bad.bodies[0].parent = bad.bodies[0].id;
  check(error(validate_body_hierarchy(bad), Error::cycle),
        "Self-parent admitted");
  bad.bodies = fixture.bodies;
  bad.system = fixture.system;
  bad.version = fixture.version;
  bad.bodies[3].parent = bad.bodies[0].id;
  check(error(validate_body_hierarchy(bad), Error::cycle),
        "Parent cycle admitted");
  bad.bodies = fixture.bodies;
  bad.system = fixture.system;
  bad.version = fixture.version;
  bad.bodies[0].id.version = 2;
  check(!validate_body_hierarchy(bad), "Future identity version admitted");
  bad.bodies = fixture.bodies;
  bad.system = fixture.system;
  bad.version = fixture.version;
  bad.version = 2;
  check(error(validate_body_hierarchy(bad), Error::unsupported_version),
        "Future hierarchy admitted");
  for (const auto invalid : {0U, 3U}) {
    bad.bodies = fixture.bodies;
    bad.system = fixture.system;
    bad.version = fixture.version;
    bad.bodies[0].orbit->ephemeris_version = invalid;
    check(error(validate_body_hierarchy(bad), Error::unsupported_version),
          "Unknown ephemeris admitted");
  }
  bad.bodies = fixture.bodies;
  bad.system = fixture.system;
  bad.version = fixture.version;
  bad.bodies[0].orbit->period_ticks = 0;
  check(error(validate_body_hierarchy(bad), Error::invalid_orbit),
        "Zero period admitted");
  bad.bodies = fixture.bodies;
  bad.system = fixture.system;
  bad.version = fixture.version;
  bad.bodies[0].orbit->radius_kilometres = 0;
  check(error(validate_body_hierarchy(bad), Error::invalid_orbit),
        "Zero radius admitted");
  bad.bodies = fixture.bodies;
  bad.system = fixture.system;
  bad.version = fixture.version;
  bad.bodies[0].orbit->inclination_microdegrees = 180'000'001;
  check(error(validate_body_hierarchy(bad), Error::invalid_orbit),
        "Unsupported inclination admitted");
  for (const double fraction :
       {-1.0, 1.0, std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()})
    check(error(resolve_body_ephemerides(fixture, {0, fraction}),
                Error::invalid_time),
          "Invalid query fraction admitted");
  check(resolve_body_ephemerides(
            fixture, {std::numeric_limits<SimulationTick>::max(), 0})
            .has_value(),
        "Pure query unnecessarily rejected a full-width tick");
  bad.bodies = fixture.bodies;
  bad.system = fixture.system;
  bad.version = fixture.version;
  auto parent = bad.bodies.front().id;
  for (std::size_t i = 0; i < kMaximumBodyHierarchyDepth; ++i) {
    const BodyId id{kBodyIdentityVersion, BodyKind::moon, i + 2};
    bad.bodies.push_back({id, parent, fixture.bodies.front().orbit});
    parent = id;
  }
  check(error(validate_body_hierarchy(bad), Error::excessive_depth),
        "Excessive depth admitted");
  bad.bodies = fixture.bodies;
  bad.system = fixture.system;
  bad.version = fixture.version;
  while (bad.bodies.size() <= kMaximumHierarchyBodies)
    bad.bodies.push_back(fixture.bodies[0]);
  check(error(validate_body_hierarchy(bad), Error::oversized_hierarchy),
        "Excessive body count admitted");
  auto bounded = declared_fixture();
  bounded.bodies = {{fixture.bodies[1].id, {}, {}}};
  auto bounded_parent = bounded.bodies.front().id;
  for (std::size_t i = 1; i < kMaximumBodyHierarchyDepth; ++i) {
    const BodyId id{kBodyIdentityVersion, BodyKind::moon, i};
    bounded.bodies.push_back({id, bounded_parent, fixture.bodies[0].orbit});
    bounded_parent = id;
  }
  check(resolve_body_ephemerides(bounded, {31, .5}).has_value(),
        "Maximum supported depth refused");
  while (bounded.bodies.size() < kMaximumHierarchyBodies) {
    const BodyId id{kBodyIdentityVersion, BodyKind::minor,
                    bounded.bodies.size()};
    bounded.bodies.push_back(
        {id, bounded.bodies.front().id, fixture.bodies[0].orbit});
  }
  check(need(resolve_body_ephemerides(bounded, {31, .5})).size() ==
            kMaximumHierarchyBodies,
        "Maximum supported body count refused");
  auto extreme = declared_fixture();
  for (auto& node : extreme.bodies)
    if (node.orbit) {
      node.orbit->radius_kilometres = std::numeric_limits<std::uint64_t>::max();
      node.orbit->period_ticks = 1;
    }
  const auto largest = need(resolve_body_ephemerides(extreme, {0, .25}));
  for (const auto& row : largest)
    check(std::isfinite(row.position.x) && std::isfinite(row.position.y) &&
              std::isfinite(row.position.z) && std::isfinite(row.velocity.x) &&
              std::isfinite(row.velocity.y) && std::isfinite(row.velocity.z),
          "Full-width radius overflowed before finite validation");
  for (const auto invalid_period :
       {std::uint64_t{1} << 53, std::numeric_limits<std::uint64_t>::max()}) {
    extreme.bodies[0].orbit->period_ticks = invalid_period;
    check(error(validate_body_hierarchy(extreme), Error::invalid_orbit),
          "Period beyond exact phase domain admitted");
  }
  auto root_only = declared_fixture();
  root_only.bodies = {{fixture.bodies[1].id, {}, {}}};
  check(need(resolve_body_ephemerides(root_only, {0, 0})).size() == 1,
        "Valid single star refused");
  root_only.bodies.clear();
  check(error(validate_body_hierarchy(root_only), Error::oversized_hierarchy),
        "Empty hierarchy admitted");
  auto invalid_root = declared_fixture();
  invalid_root.bodies = {{fixture.bodies[1].id, {}, {}}};
  invalid_root.bodies[0].id.kind = BodyKind::minor;
  check(error(validate_body_hierarchy(invalid_root), Error::invalid_root),
        "Non-star root admitted");
  auto invalid_kind = declared_fixture();
  invalid_kind.bodies[0].id.kind = static_cast<BodyKind>(255);
  check(error(validate_body_hierarchy(invalid_kind), Error::invalid_identity),
        "Unsupported body kind admitted");
  auto invalid_orbit_version = declared_fixture();
  invalid_orbit_version.bodies[0].orbit->version = 2;
  check(error(validate_body_hierarchy(invalid_orbit_version),
              Error::unsupported_version),
        "Unsupported orbit version admitted");
  const BodyTarget wrong{SystemId{8}, fixture.bodies[0].id};
  check(error(validate_body_target(fixture, wrong), Error::invalid_target),
        "Cross-system body target admitted");
  check(!planet_id(fixture.bodies[0].id),
        "Moon silently converted to PlanetId");
}
auto encoding_checks() -> void {
  for (const auto kind :
       {BodyKind::star, BodyKind::planet, BodyKind::moon, BodyKind::minor})
    for (const auto word :
         {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{1} << 63,
          std::numeric_limits<std::uint64_t>::max()}) {
      const BodyTarget target{SystemId{word},
                              {kBodyIdentityVersion, kind, word}};
      check(need(decode_body_target(need(encode_body_target(target)))) ==
                target,
            "Full-width tagged identity narrowed");
    }
  for (
      const std::string_view invalid :
      {"", "null", "{}", "[]",
       R"({"version":1,"system":"7","kind":"moon","identity":"01"})",
       R"({"version":1,"system":"7","kind":"moon","identity":"-1"})",
       R"({"version":1,"system":"7","kind":"moon","identity":1})",
       R"({"version":2,"system":"7","kind":"moon","identity":"1"})",
       R"({"version":1.0,"system":"7","kind":"moon","identity":"1"})",
       R"({"version":1,"system":"7","kind":"unknown","identity":"1"})",
       R"({"version":1,"system":"7","kind":"moon","identity":"18446744073709551616"})",
       R"({"version":1,"system":"7","kind":"moon","identity":"1","identity":"2"})",
       R"({"version":1,"system":"7","kind":"moon","identity":"1","extra":true})",
       R"({"version":1,"system":"7","kind":"moon","identity":{"nested":"1"}})",
       R"({"version":1,"version":1,"system":"7","kind":"moon","identity":"1"})"})
    check(!decode_body_target(invalid), "Malformed target admitted");
  check(!decode_body_target(std::string(257, ' ')),
        "Excessive target bytes admitted");
}
} // namespace

int main() {
  for (const auto seed : {Seed{0}, Seed{42}, Seed{43}, Seed{99}}) {
    compatibility(generate_local_system(seed));
    compatibility(generate_origin_system(seed));
    for (const std::uint32_t version : {1U, 2U}) {
      compatibility(need(generate_physical_local_system(seed, version)));
      compatibility(need(generate_physical_origin_system(seed, version)));
    }
  }
  hierarchy_checks();
  encoding_checks();
  std::cout << "Body ephemerides: " << failures << " failures\n";
  return failures ? 1 : 0;
}
