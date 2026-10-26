#include "apsis_drift/freedom_jump_targeting.hpp"

#include "apsis_drift/atmospheric_flight.hpp"
#include "apsis_drift/freedom_system_network.hpp"
#include "apsis_drift/intersystem_jump.hpp"
#include "apsis_drift/terrain_impact.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace apsis_drift;
unsigned checks{};
auto check(bool ok, const char* label) -> void {
  ++checks;
  if (!ok) throw std::runtime_error(label);
}
template <class T, class E> auto need(std::expected<T, E> result) -> T {
  if (!result) throw std::runtime_error("Charted arrival fixture refused");
  return std::move(*result);
}
auto length(RigidVector3 p) -> double {
  return std::sqrt((p.x * p.x + p.y * p.y) + p.z * p.z);
}
auto offset(SystemPositionMetres a, SystemPositionMetres b) -> RigidVector3 {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto fixture(Seed seed, std::uint32_t network_version = 1)
    -> FreedomJumpRequest {
  // Explicit numerical targeting fixture. No claimed flown departure/history.
  const auto network =
      need(generate_freedom_system_network(seed, network_version));
  const auto owner =
      need(resolve_freedom_network_system(network, network.nodes[0].system));
  FreedomJumpRequest r;
  r.version = kFreedomChartedJumpTargetingVersion;
  r.universe_seed = seed;
  r.craft = make_freedom_new_game_document(seed).state.craft;
  r.network = FreedomNetworkSelection{network_version};
  r.charted_arrival.emplace();
  r.source.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  r.source.frame = {RigidFrameKind::planet_relative_inertial,
                    owner.catalog.id,
                    owner.catalog.planets[0].descriptor.id,
                    {}};
  r.source.tick = 1200;
  r.source.position_metres = {
      0, 0, owner.catalog.planets[0].descriptor.radius.value * 12000.0};
  r.source.orientation = need(normalize_rigid_orientation({.8, .1, .2, .3}));
  r.source.linear_velocity_metres_per_second = {3, -4, 2};
  r.source.angular_velocity_radians_per_second = {.01, -.02, .03};
  r.source = need(
      canonicalize_rigid_body_state(RigidBodyWorldContext{owner}, r.source));
  r.destination = network.nodes[1].system;
  r.attempt = 2;
  return r;
}
auto invalid_contract() -> void {
  const auto good = fixture({42});
  for (unsigned index = 0; index < 19; ++index) {
    auto bad = good;
    switch (index) {
      case 0: bad.version = 0; break;
      case 1: bad.version = 5; break;
      case 2: bad.charted_arrival.reset(); break;
      case 3: bad.charted_arrival->version = 0; break;
      case 4: bad.charted_arrival->version = 2; break;
      case 5: bad.network.reset(); break;
      case 6: bad.network->version = 4; break;
      case 7: bad.craft.value ^= 1; break;
      case 8: bad.source.frame.planet.reset(); break;
      case 9: bad.source.craft.version = 2; break;
      case 10:
        bad.source.tick = std::numeric_limits<SimulationTick>::max();
        break;
      case 11:
        bad.source.position_metres.x = std::numeric_limits<double>::quiet_NaN();
        break;
      case 12:
        bad.source.linear_velocity_metres_per_second.z =
            std::numeric_limits<double>::infinity();
        break;
      case 13: bad.source.orientation.w = 0; break;
      case 14: bad.source.angular_velocity_radians_per_second.y = 101; break;
      case 15: bad.attempt = 0; break;
      case 16: bad.profile = static_cast<IntersystemRuleProfile>(255); break;
      case 17:
        bad.source.position_metres.z = kRigidBodyMaximumPositionMetres;
        break;
      case 18: bad.destination = {}; break;
    }
    check(!preview_freedom_jump(bad) && !freeze_freedom_jump_arrival(bad),
          "Invalid typed owner, policy, clock or physical state refuses");
  }
  for (std::uint32_t version : {1U, 2U, 3U}) {
    auto bad = good;
    bad.version = version;
    if (version == 1) bad.network.reset();
    if (version == 3) bad.recovery.emplace();
    check(!preview_freedom_jump(bad),
          "Historical requests reject charted selection");
  }
  auto home = good;
  const auto network =
      need(generate_freedom_system_network(good.universe_seed));
  const auto owner =
      need(resolve_freedom_network_system(network, good.destination));
  home.source.frame.system = owner.catalog.id;
  home.source.frame.planet = owner.catalog.planets[0].descriptor.id;
  home.source.position_metres = {
      0, 0, owner.catalog.planets[0].descriptor.radius.value * 12000.0};
  home.destination = network.nodes[0].system;
  check(!preview_freedom_jump(home),
        "Origin request4 refuses rather than replacing station approach");
  home.version = 2;
  home.charted_arrival.reset();
  check(preview_freedom_jump(home).has_value(),
        "The same Origin route retains its old wide policy");
}
auto route_contract(Seed seed, std::uint32_t network_version) -> void {
  auto r = fixture(seed, network_version);
  const auto original = r;
  const auto network =
      need(generate_freedom_system_network(seed, network_version));
  const auto to = need(resolve_freedom_network_system(network, r.destination));
  const auto& planet = to.catalog.planets[0].descriptor;
  const auto p = need(preview_freedom_jump(r));
  const auto ephemeris =
      need(resolve_planet_ephemeris(to, planet.id, {p.arrival_tick, 0}));
  const auto atmosphere = need(resolve_atmospheric_flight_profile(planet));
  const auto& craft = wayfarer_frame().properties;
  // Independent eight-corner enclosure around the actual COM.
  double hull_radius{};
  for (unsigned corner = 0; corner < 8; ++corner) {
    std::array<double, 3> point{};
    for (unsigned axis = 0; axis < 3; ++axis)
      point[axis] = ((corner & (1U << axis) ? craft.hull_max_mm[axis]
                                            : craft.hull_min_mm[axis]) -
                     static_cast<double>(craft.center_of_mass_mm[axis])) /
                    1000.0;
    hull_radius = std::max(hull_radius, length({point[0], point[1], point[2]}));
  }
  const double radius = planet.radius.value * 1000.0;
  const double boundary = std::max(atmosphere.space_boundary_altitude_metres,
                                   kTerrainMaximumElevationMetres);
  const double nominal =
      std::max(1.5 * radius, radius + boundary + hull_radius + 302000.0);
  check(p.nominal_arrival ==
            SystemPositionMetres{ephemeris.position.x, ephemeris.position.y,
                                 ephemeris.position.z + nominal},
        "Nominal shell follows atmosphere1, full terrain ceiling, registered "
        "hull and conservative scatter bound");
  check(p.request == original && r == original &&
            p.arrival_tick == r.source.tick + kJumpTransitTicks &&
            p.reference_planet == planet.id && p.volume && p.volume->clear(),
        "Pure selection preserves source pose, owner and transit while "
        "certifying existing volume");
  check(p.arrival_velocity ==
            SystemVelocityMetresPerSecond{ephemeris.velocity.x + 3,
                                          ephemeris.velocity.y - 4,
                                          ephemeris.velocity.z + 2},
        "Closer placement preserves source-relative velocity without orbit "
        "insertion");
  const auto exact = need(assess_freedom_jump_distance(
      p.distance_metres, IntersystemArrivalQuality::aligned));
  check(p.distance == exact,
        "Exact alignment and distance are reassessed at the selected endpoint");
  const auto baseline = need(freeze_freedom_jump_arrival(r));
  check(baseline == need(freeze_freedom_jump_arrival(r)) &&
            validate_frozen_freedom_jump_arrival(baseline).has_value(),
        "Charted sample freezes deterministically and authenticates complete "
        "inputs");
  check(length(offset(baseline.point, ephemeris.position)) - hull_radius -
                radius >=
            boundary + 100000.0,
        "Actual hidden point retains atmosphere, terrain, hull and100km shell "
        "clearance");
  auto old = r;
  old.version = kFreedomNetworkJumpTargetingVersion;
  old.charted_arrival.reset();
  const auto wide = need(preview_freedom_jump(old));
  check(wide.nominal_arrival.z ==
                ephemeris.position.z +
                    radius * kAssistedTargetArrivalStandoffRadii &&
            wide.arrival_velocity == p.arrival_velocity &&
            wide.arrival_tick == p.arrival_tick,
        "Absent selection retains exact old10R geometry, velocity and timing");
  for (auto profile :
       {IntersystemRuleProfile::assisted, IntersystemRuleProfile::pilot}) {
    r.profile = profile;
    for (std::uint32_t drive : {1U, 2U}) {
      r.attempt = 1;
      const auto ready_plain = need(freeze_freedom_jump_arrival(r));
      r.recovery = FreedomDriveRecoveryState{drive, {}, {}};
      const auto ready = need(freeze_freedom_jump_arrival(r));
      check(ready.point == ready_plain.point &&
                ready.sample_seed == ready_plain.sample_seed &&
                ready.recovery && ready.recovery->extra_radius_millimetres == 0,
            "First-use Standard/Recovery/strain geometry differs only by "
            "explicit recovery receipt");
      r.recovery.reset();
      r.attempt = drive == 1 ? 2 : 3;
      const auto plain = need(freeze_freedom_jump_arrival(r));
      r.recovery = FreedomDriveRecoveryState{
          drive,
          {},
          need(make_freedom_jump_recovery_stamp(FreedomJumpRecoveryKind::system,
                                                r.attempt - 1, 17, 100))};
      if (drive == 2) r.recovery->last_use->recovery_ticks = 10800;
      const auto frozen = need(freeze_freedom_jump_arrival(r));
      const auto& uncertainty = *frozen.recovery;
      const auto delta = uncertainty.offset_metres;
      check(frozen.sample_seed == plain.sample_seed &&
                frozen.point == SystemPositionMetres{plain.point.x + delta.x,
                                                     plain.point.y + delta.y,
                                                     plain.point.z + delta.z} &&
                frozen.preview.request == r &&
                frozen.preview.arrival_velocity ==
                    plain.preview.arrival_velocity &&
                frozen.preview.arrival_tick == plain.preview.arrival_tick,
            "Recovery and strain compose only independent displacement, "
            "preserving stream14 and physical source");
      check(*frozen.preview.distance.envelope_radius_metres ==
                    *plain.preview.distance.envelope_radius_metres +
                        (uncertainty.extra_radius_millimetres + 999) / 1000 &&
                *frozen.preview.distance.envelope_radius_metres <= 202000 &&
                frozen.preview.volume->clear() &&
                frozen.point_assessment.clear() &&
                validate_frozen_freedom_jump_arrival(frozen).has_value(),
            "Rated grade plus recovery remains within proven202km bound and "
            "certified existing volume");
      check(
          length(offset(frozen.point, ephemeris.position)) - hull_radius -
                  radius >=
              boundary + 100000.0,
          "Independent recovery displacement preserves the conservative shell");
      auto changed = frozen;
      ++changed.preview.request.charted_arrival->version;
      check(!validate_frozen_freedom_jump_arrival(changed),
            "Changed selection cannot authenticate a frozen receipt");
      changed = frozen;
      changed.point.z += 1;
      check(!validate_frozen_freedom_jump_arrival(changed),
            "Hidden point edits cannot authenticate");
      r.recovery.reset();
    }
  }
}
} // namespace
auto main() -> int {
  try {
    invalid_contract();
    route_contract({42}, 1);
    route_contract({0}, 2);
    std::cout << checks << " charted arrival targeting checks\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "After " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
