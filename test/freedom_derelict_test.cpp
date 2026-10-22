#include "apsis_drift/freedom_derelict.hpp"

#include "apsis_drift/atmospheric_flight.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "apsis_drift/secondary_bodies.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
using namespace apsis_drift;
int checks{}, failures{};
auto check(bool value, std::string_view message) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <typename T, typename E> auto need(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Derelict fixture refused");
  return std::move(*value);
}
auto length(RigidVector3 p) -> double {
  return std::hypot(p.x, p.y, p.z);
}
auto difference(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto generated(Seed seed) -> void {
  const auto old_network = need(generate_freedom_system_network(seed));
  const auto network = need(generate_freedom_system_network(seed, 2));
  const auto d = need(generate_freedom_derelict(seed));
  // Independent snapshot detects any change to the canonical descriptor.
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Snapshot.
  const auto frozen = d;
  const auto stream = derive_seed(seed, SeedDomain::freedom_derelict, 1);
  const auto owner =
      need(resolve_freedom_network_system(network, network.nodes[4].system));
  check(d == need(generate_freedom_derelict(seed)) &&
            validate_freedom_derelict_descriptor(d).has_value() &&
            d.version == 1 && d.universe_seed == seed && d.id != 0 &&
            d.id ==
                derive_seed(stream, SeedDomain::freedom_derelict, 0).value &&
            d.system == network.nodes[4].system && d.owner == owner &&
            d.planet == owner.catalog.planets.front().descriptor.id,
        "One deterministic domain23 hulk owns actual uncharted4/first-planet "
        "identity");
  const auto& parent = owner.catalog.planets.front().descriptor;
  const auto atmosphere = need(resolve_atmospheric_flight_profile(parent));
  const long double radius =
      static_cast<long double>(d.radius_kilometres) * 1000;
  const long double parent_radius =
      static_cast<long double>(parent.radius.value) * 1000;
  const long double gravity =
      static_cast<long double>(parent.surface_gravity.value) * 9.80665L / 1000;
  const long double mu = gravity * parent_radius * parent_radius;
  const long double period = 2 * std::numbers::pi_v<long double> *
                             std::sqrt(radius * radius * radius / mu) *
                             kSimulationHz;
  check(std::abs(static_cast<long double>(d.period_ticks) - period) < 0.500001L,
        "Independent long-double physical-mu oracle certifies rounded circular "
        "period");
  const auto frame =
      need(resolve_craft_frame({kWayfarerFrameId, kWayfarerFrameVersion}));
  const auto& p = frame.properties;
  double hull{};
  for (const auto x : {p.hull_min_mm[0], p.hull_max_mm[0]})
    for (const auto y : {p.hull_min_mm[1], p.hull_max_mm[1]})
      for (const auto z : {p.hull_min_mm[2], p.hull_max_mm[2]})
        hull = std::max(
            hull,
            std::hypot((static_cast<double>(x) - p.center_of_mass_mm[0]) / 1000,
                       (static_cast<double>(y) - p.center_of_mass_mm[1]) / 1000,
                       (static_cast<double>(z) - p.center_of_mass_mm[2]) /
                           1000));
  check(radius - hull > parent_radius + 17070 &&
            radius - hull >
                parent_radius + atmosphere.space_boundary_altitude_metres,
        "Whole enclosing hulk radius remains beyond full terrain and actual "
        "atmosphere");
  const auto secondary = need(generate_secondary_body_catalog(owner));
  for (const auto& body : secondary.bodies) {
    check(d.id != body.node.id.value,
          "Hulk identity never aliases generated optical bodies");
    if (body.node.parent == body_id(parent.id) &&
        body.node.id.kind == BodyKind::moon)
      check(radius + hull < body.node.orbit->radius_kilometres * 1000.0 -
                                body.radius_metres - 4545,
            "Whole hulk orbit stays inside the Moon's innermost complete "
            "radial envelope");
  }
  const auto at_zero = need(resolve_freedom_derelict(d, 0));
  const auto cycled = need(resolve_freedom_derelict(d, d.period_ticks));
  check(at_zero.position_metres == cycled.position_metres &&
            at_zero.linear_velocity_metres_per_second ==
                cycled.linear_velocity_metres_per_second,
        "Modulo-before-conversion gives exact full-period relative pose "
        "recurrence");
  for (const auto tick :
       {SimulationTick{0}, SimulationTick{1}, d.period_ticks - 1,
        d.period_ticks, std::numeric_limits<SimulationTick>::max() - 1}) {
    const auto pose = need(resolve_freedom_derelict(d, tick));
    const double r = length(pose.position_metres);
    const double v = length(pose.linear_velocity_metres_per_second);
    const auto& a = pose.position_metres;
    const auto& b = pose.linear_velocity_metres_per_second;
    check(validate_rigid_body_state(RigidBodyWorldContext{owner}, pose)
                  .has_value() &&
              pose.frame ==
                  RigidCoordinateFrame{RigidFrameKind::planet_relative_inertial,
                                       d.system,
                                       d.planet,
                                       {}} &&
              pose.craft ==
                  CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion} &&
              pose.tick == tick && pose.orientation == RigidOrientation{} &&
              pose.angular_velocity_radians_per_second == RigidVector3{} &&
              std::abs(r - static_cast<double>(radius)) < 1e-6 &&
              std::abs(a.x * b.x + a.y * b.y + a.z * b.z) < r * v * 1e-12 &&
              std::abs(v - 2 * std::numbers::pi * static_cast<double>(radius) *
                               kSimulationHz /
                               static_cast<double>(d.period_ticks)) < v * 1e-12,
          "Resolved canonical Wayfarer pose has correct owner, constant radius "
          "and tangent speed");
    const auto system = need(reframe_rigid_body(
        RigidBodyWorldContext{owner}, pose,
        {{RigidFrameKind::system_inertial, d.system, {}, {}}, tick}));
    const auto parent_pose =
        need(resolve_planet_ephemeris(owner, d.planet, {tick, 0}));
    check(length(difference(system.position_metres,
                            {parent_pose.position.x + a.x,
                             parent_pose.position.y + a.y,
                             parent_pose.position.z + a.z})) < 1e-5,
          "Parent-relative hulk uses the existing same-tick physical ephemeris "
          "handoff");
  }
  const auto next = need(resolve_freedom_derelict(d, 1));
  const auto delta = difference(next.position_metres, at_zero.position_metres);
  check(length(difference(
            delta, {at_zero.linear_velocity_metres_per_second.x / kSimulationHz,
                    at_zero.linear_velocity_metres_per_second.y / kSimulationHz,
                    at_zero.linear_velocity_metres_per_second.z /
                        kSimulationHz})) < .01,
        "One120Hz analytic step is locally consistent with the reported "
        "velocity");
  check(d == frozen &&
            old_network == need(generate_freedom_system_network(seed)) &&
            network == need(generate_freedom_system_network(seed, 2)),
        "Hulk generation and pose reads preserve all preexisting world/seed "
        "recipes");
}
auto refusals() -> void {
  const auto d = need(generate_freedom_derelict({42}));
  for (unsigned damage = 0; damage < 12; ++damage) {
    auto bad = d;
    switch (damage) {
      case 0: bad.version = 2; break;
      case 1: bad.universe_seed = Seed{43}; break;
      case 2: bad.id = 0; break;
      case 3: bad.system = SystemId{0}; break;
      case 4: bad.planet = PlanetId{0}; break;
      case 5: ++bad.owner.stellar_mass_millisolar; break;
      case 6:
        bad.radius_kilometres = std::numeric_limits<std::uint64_t>::max();
        break;
      case 7: bad.period_ticks = 0; break;
      case 8: ++bad.period_ticks; break;
      case 9: ++bad.epoch_phase_turns; break;
      case 10: ++bad.inclination_microdegrees; break;
      case 11: ++bad.ascending_node_turns; break;
    }
    const auto frozen = bad;
    check(!validate_freedom_derelict_descriptor(bad) &&
              !resolve_freedom_derelict(bad, 17) && bad == frozen,
          "Forged owner/ID/circular recipe refuses before caller-controlled "
          "orbit arithmetic");
  }
  check(
      !resolve_freedom_derelict(d, std::numeric_limits<SimulationTick>::max()),
      "Reserved maximum clock refuses before circular resolution");
}
auto records() -> void {
  const Seed seed{42};
  const auto d = need(generate_freedom_derelict(seed));
  const auto craft = need(derive_freedom_craft_identity(seed, 0));
  const auto record = [&](SimulationTick tick, double gap, double speed) {
    const auto pose = need(resolve_freedom_derelict(d, tick));
    return FreedomDerelictObservation{
        craft,
        tick,
        {pose.position_metres.x, pose.position_metres.y,
         pose.position_metres.z + gap},
        {pose.linear_velocity_metres_per_second.x + speed,
         pose.linear_velocity_metres_per_second.y,
         pose.linear_velocity_metres_per_second.z}};
  };
  // Explicit observer-boundary inputs, not claimed flown approach histories.
  check(validate_freedom_derelict_state(seed, {}, 0).has_value(),
        "Unobserved selected state is empty");
  FreedomDerelictState s{
      1, record(17, kFreedomDerelictContactRangeMetres, 0), {}};
  check(validate_freedom_derelict_state(seed, s, 17).has_value(),
        "Exact100000km contact boundary is inclusive");
  s.contact = record(17, kFreedomDerelictContactRangeMetres + 1, 0);
  check(!validate_freedom_derelict_state(seed, s, 17),
        "Contact beyond100000km refuses");
  s.contact = record(17, 300, 0);
  for (const auto gap : {100.0, 500.0}) {
    s.inspection = record(18, gap, 20);
    check(validate_freedom_derelict_state(seed, s, 18).has_value(),
          "Inspection endpoints100/500m and20m/s are inclusive");
  }
  for (const auto gap : {99.99, 500.01}) {
    s.inspection = record(18, gap, 0);
    check(!validate_freedom_derelict_state(seed, s, 18),
          "Too close or distant inspection refuses without collision policy");
  }
  s.inspection = record(18, 300, 20.01);
  check(!validate_freedom_derelict_state(seed, s, 18),
        "Inspection excess relative speed refuses");
  s.inspection = record(18, 300, 0);
  const auto valid = s;
  check(validate_freedom_derelict_state(seed, s, 18).has_value() &&
            validate_freedom_derelict_state(seed, s, 1000).has_value() &&
            s == valid,
        "Historical notes authenticate actual hulk at their own ticks and "
        "reads are idempotent");
  auto boundary = valid;
  boundary.inspection.reset();
  boundary.contact->position_metres = {};
  check(!validate_freedom_derelict_state(seed, boundary, 18),
        "A finite planet-centre contact cannot authenticate a space scan");
  const auto parent = need(find_local_system_planet(d.owner, d.planet));
  const auto atmosphere =
      need(resolve_atmospheric_flight_profile(parent->descriptor));
  const double surface_radius = parent->descriptor.radius.value * 1000.0;
  const auto contact_pose = need(resolve_freedom_derelict(d, 17));
  for (const double offset : {-1.0, 0.0, 1.0}) {
    boundary.contact->position_metres = {
        surface_radius + atmosphere.space_boundary_altitude_metres + offset, 0,
        0};
    check(length(difference(boundary.contact->position_metres,
                            contact_pose.position_metres)) <=
              kFreedomDerelictContactRangeMetres,
          "Atmosphere-boundary fixture remains inside contact range");
    check(validate_freedom_derelict_state(seed, boundary, 18).has_value() ==
              (offset > 0.0),
          "Contact acquisition requires strictly above the actual atmosphere "
          "boundary");
  }
  for (unsigned damage = 0; damage < 12; ++damage) {
    auto bad = valid;
    switch (damage) {
      case 0: bad.version = 2; break;
      case 1: bad.contact.reset(); break;
      case 2: bad.contact->tick = 19; break;
      case 3: bad.inspection->tick = 16; break;
      case 4: bad.contact->craft = StarterCraftId{0}; break;
      case 5:
        bad.inspection->position_metres.x =
            std::numeric_limits<double>::quiet_NaN();
        break;
      case 6:
        bad.contact->velocity_metres_per_second.y =
            std::numeric_limits<double>::infinity();
        break;
      case 7:
        bad.inspection->position_metres.z = kRigidBodyMaximumPositionMetres * 2;
        break;
      case 8:
        bad.inspection->velocity_metres_per_second.x =
            kRigidBodyMaximumVelocityMetresPerSecond * 2;
        break;
      case 9:
        bad.contact->tick = std::numeric_limits<SimulationTick>::max();
        break;
      case 10: bad.inspection->position_metres = {}; break;
      case 11: bad.inspection->velocity_metres_per_second = {}; break;
    }
    check(!validate_freedom_derelict_state(seed, bad, 18),
          "Malformed/nonfinite/out-of-bounds/incorrect-time or relative-frame "
          "observation refuses");
  }
  check(!validate_freedom_derelict_state(
            seed, valid, std::numeric_limits<SimulationTick>::max()),
        "State validation rejects reserved future-clock boundary");
}
} // namespace
int main() {
  try {
    refusals();
    records();
    for (const auto value : std::array<std::uint64_t, 8>{
             0, 1, 4, 7, 42, 123, 2048,
             std::numeric_limits<std::uint64_t>::max()})
      generated(Seed{value});
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << error.what() << '\n';
  }
  std::cout << checks << " derelict checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
