#include "apsis_drift/secondary_bodies.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {
using namespace apsis_drift;
int checks{}, failures{};
std::uint64_t fingerprint{14695981039346656037ULL};
auto check(bool value, std::string_view message) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Secondary body fixture refused");
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto hash(std::uint64_t value) -> void {
  for (unsigned byte = 0; byte < 8; ++byte) {
    fingerprint ^= (value >> (byte * 8U)) & 255U;
    fingerprint *= 1099511628211ULL;
  }
}
auto bits(double value) -> std::uint64_t {
  return std::bit_cast<std::uint64_t>(value);
}
auto same_position_bits(const BodyEphemeris& a, const BodyEphemeris& b)
    -> bool {
  return a == b && bits(a.position.x) == bits(b.position.x) &&
         bits(a.position.y) == bits(b.position.y) &&
         bits(a.position.z) == bits(b.position.z) &&
         bits(a.velocity.x) == bits(b.velocity.x) &&
         bits(a.velocity.y) == bits(b.velocity.y) &&
         bits(a.velocity.z) == bits(b.velocity.z) &&
         bits(a.phase_radians) == bits(b.phase_radians);
}
auto fixture(const PhysicalLocalSystem& owner) -> void {
  const auto old = need(make_body_hierarchy(owner));
  const auto catalog = need(generate_secondary_body_catalog(owner));
  const auto hierarchy = need(make_body_hierarchy(catalog));
  check(catalog.owner == owner &&
            catalog == need(generate_secondary_body_catalog(owner)) &&
            validate_secondary_body_catalog(catalog).has_value() &&
            hierarchy.catalog &&
            std::holds_alternative<SecondaryBodyCatalog>(*hierarchy.catalog),
        "Explicit population reproduces its full authenticated owner");
  check(hierarchy.bodies.size() == old.bodies.size() + 2,
        "Population adds exactly one moon and one minor body");
  const auto& moon = catalog.bodies[0];
  const auto& minor = catalog.bodies[1];
  const auto& parent = owner.catalog.planets.front();
  check(moon.node.id.kind == BodyKind::moon &&
            moon.node.parent == body_id(parent.descriptor.id) &&
            moon.radius_metres >= 600'000 && moon.radius_metres <= 1'800'000 &&
            moon.radius_metres <= parent.descriptor.radius.value * 1000 / 3 &&
            moon.node.orbit &&
            moon.node.orbit->radius_kilometres >
                parent.descriptor.radius.value + moon.radius_metres / 1000,
        "Moon keeps its typed parent, bounded radius and separated shell");
  check(minor.node.id.kind == BodyKind::minor &&
            minor.node.parent == body_id(owner.catalog.star.id) &&
            minor.radius_metres >= 50'000 && minor.radius_metres <= 250'000 &&
            minor.node.orbit &&
            minor.node.orbit->radius_kilometres >=
                owner.catalog.planets.back().orbit.radius_kilometres +
                    2'000'000,
        "Minor body keeps its typed star parent outside planetary shells");
  for (const auto& body : catalog.bodies) {
    check(
        body.node.orbit->period_ticks > 0 && body.spin_period_ticks >= 86'400 &&
            body.spin_period_ticks <= 8'640'000 &&
            body.node.orbit->ephemeris_version == owner.ephemeris_version &&
            !planet_id(body.node.id),
        "Bounded spin/orbit inputs never reinterpret a companion as PlanetId");
    const auto seconds = static_cast<double>(body.node.orbit->period_ticks) /
                         static_cast<double>(kSimulationHz);
    const auto angular_rate = 2 * std::numbers::pi / seconds;
    const auto a = static_cast<double>(body.node.orbit->radius_kilometres);
    const auto inferred_gm = (angular_rate * angular_rate) * ((a * a) * a);
    const auto parent_radius =
        static_cast<double>(parent.descriptor.radius.value);
    const auto expected_gm =
        body.node.id.kind == BodyKind::moon
            ? 9.80665 * parent.descriptor.surface_gravity.value / 1'000'000.0 *
                  (parent_radius * parent_radius)
            : static_cast<double>(owner.stellar_gm_km3_per_second2);
    check(std::abs(inferred_gm - expected_gm) / expected_gm < .00001,
          "Circular periods use the actual parent GM within tick rounding");
    const BodyTarget target{owner.catalog.id, body.node.id};
    check(need(decode_body_target(need(encode_body_target(target)))) ==
                  target &&
              validate_body_target(hierarchy, target).has_value() &&
              !validate_body_target(old, target),
          "Companion target roundtrips only within its admitted owner");
    for (const auto value :
         {body.node.id.value, body.node.parent->value,
          body.node.orbit->radius_kilometres, body.node.orbit->period_ticks,
          static_cast<std::uint64_t>(body.radius_metres),
          static_cast<std::uint64_t>(body.spin_period_ticks),
          static_cast<std::uint64_t>(body.epoch_spin_phase_turns),
          static_cast<std::uint64_t>(body.node.orbit->epoch_phase_turns),
          static_cast<std::uint64_t>(body.node.orbit->ascending_node_turns),
          static_cast<std::uint64_t>(body.node.orbit->inclination_microdegrees),
          static_cast<std::uint64_t>(body.tint.red),
          static_cast<std::uint64_t>(body.tint.green),
          static_cast<std::uint64_t>(body.tint.blue)})
      hash(value);
  }
  for (const auto tick :
       {SimulationTick{0}, SimulationTick{25}, SimulationTick{720'000},
        std::numeric_limits<SimulationTick>::max() - 1}) {
    const EphemerisQueryTime time{tick, .5};
    const auto base = need(resolve_body_ephemerides(old, time));
    const auto extended = need(resolve_body_ephemerides(hierarchy, time));
    for (const auto& prior : base) {
      const auto found =
          std::ranges::find(extended, prior.body, &BodyEphemeris::body);
      check(found != extended.end() && same_position_bits(*found, prior),
            "Adding companions preserves existing star/planet ephemeris bits");
    }
    for (const auto& body : catalog.bodies) {
      const auto& row =
          *std::ranges::find(extended, body.node.id, &BodyEphemeris::body);
      const auto& host =
          *std::ranges::find(extended, *body.node.parent, &BodyEphemeris::body);
      const double distance =
          std::hypot(row.relative_position.x, row.relative_position.y,
                     row.relative_position.z);
      // Historical v1 rounds each coordinate to metres: radial error is at
      // most sqrt(3)/2m. Continuous v2 keeps its tighter floating-point bound.
      check(std::isfinite(distance) &&
                std::abs(distance - static_cast<double>(
                                        body.node.orbit->radius_kilometres) *
                                        1000) <
                    (owner.ephemeris_version == kAnalyticEphemerisVersion
                         ? .87
                         : .0001) &&
                (body.node.id.kind != BodyKind::moon ||
                 (row.position ==
                  SystemPositionMetres{
                      host.position.x + row.relative_position.x,
                      host.position.y + row.relative_position.y,
                      host.position.z + row.relative_position.z})),
            "Same-clock moon uses the parent's translated circular geometry");
      hash(bits(row.position.x));
      hash(bits(row.position.y));
      hash(bits(row.position.z));
      hash(bits(row.velocity.x));
      hash(bits(row.velocity.y));
      hash(bits(row.velocity.z));
    }
    auto reordered = hierarchy;
    std::ranges::reverse(reordered.bodies);
    check(need(resolve_body_ephemerides(reordered, time)) == extended,
          "Admission is independent of hierarchy declaration order");
  }
  check(!resolve_body_ephemerides(
            hierarchy, {std::numeric_limits<SimulationTick>::max(), 0}) &&
            !resolve_body_ephemerides(hierarchy, {0, -0.1}) &&
            !resolve_body_ephemerides(hierarchy, {0, 1.0}) &&
            !resolve_body_ephemerides(
                hierarchy, {0, std::numeric_limits<double>::quiet_NaN()}),
        "Physical companion hierarchy preserves invalid/reserved time bounds");
  auto forged = old;
  forged.bodies.push_back(moon.node);
  check(!validate_body_hierarchy(forged),
        "Old physical owner cannot silently admit a companion append");
  auto forged_descriptor = hierarchy;
  std::get<SecondaryBodyCatalog>(forged_descriptor.catalog.value())
      .bodies[0]
      .radius_metres += 1;
  check(!validate_body_hierarchy(forged_descriptor),
        "Hierarchy admission authenticates descriptor fields as well as nodes");
}
auto invalid() -> void {
  const auto owner = need(generate_physical_origin_system({42}, 2));
  const auto catalog = need(generate_secondary_body_catalog(owner));
  check(!generate_secondary_body_catalog(owner, 0) &&
            !generate_secondary_body_catalog(owner, 2),
        "Unsupported population versions refuse before generation");
  auto wrong_owner = owner;
  wrong_owner.catalog.planets.clear();
  check(!generate_secondary_body_catalog(wrong_owner),
        "Malformed empty owner refuses before selecting a parent");
  for (unsigned fault = 0; fault < 14; ++fault) {
    auto bad = catalog;
    auto& body = bad.bodies[0];
    switch (fault) {
      case 0: bad.version = 2; break;
      case 1: ++bad.owner.stellar_mass_millisolar; break;
      case 2: body.node.parent = body_id(PlanetId{}); break;
      case 3: body.node.id.kind = BodyKind::planet; break;
      case 4: body.node.id.version = 2; break;
      case 5: body.node.orbit.reset(); break;
      case 6: body.node.orbit->radius_kilometres = 0; break;
      case 7: body.node.orbit->period_ticks = 0; break;
      case 8: body.node.orbit->ephemeris_version = 0; break;
      case 9: body.radius_metres = 0; break;
      case 10: body.radius_metres = UINT32_MAX; break;
      case 11: body.spin_period_ticks = 0; break;
      case 12: body.epoch_spin_phase_turns ^= 1; break;
      case 13: body.tint.red ^= 1; break;
      default: break;
    }
    check(!validate_secondary_body_catalog(bad) && !make_body_hierarchy(bad),
          "Forged population owner, identity, orbit, radius or spin refuses");
  }
  auto legacy = need(make_body_hierarchy(generate_origin_system({42})));
  legacy.bodies.push_back(catalog.bodies[0].node);
  check(!validate_body_hierarchy(legacy),
        "Legacy catalog ownership still refuses extra companion geometry");
}
} // namespace

auto main() -> int {
  try {
    invalid();
    for (const auto seed : {Seed{0}, Seed{42}, Seed{UINT64_MAX}})
      for (const std::uint32_t version : {1U, 2U}) {
        fixture(need(generate_physical_origin_system(seed, version)));
        fixture(need(generate_physical_local_system(seed, version)));
      }
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << e.what() << '\n';
  }
  check(
      fingerprint == 17328796469459053665ULL,
      "Version1 descriptor and same-clock ephemeris transcript remains exact");
  std::cout << checks << " secondary body checks, " << failures
            << " failures; fingerprint " << fingerprint << '\n';
  return failures ? 1 : 0;
}
