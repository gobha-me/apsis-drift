#include "apsis_drift/freedom_jump_targeting.hpp"

#include "apsis_drift/intersystem_jump.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace apsis_drift;
int checks{}, failures{};
std::uint64_t fingerprint{14695981039346656037ULL};
auto check(bool ok, std::string_view label) -> void {
  ++checks;
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> r) -> T {
  if (!r) throw std::runtime_error("Jump targeting fixture refused");
  return std::move(*r);
}
auto hash(std::uint64_t word) -> void {
  for (unsigned byte = 0; byte < 8; ++byte) {
    fingerprint ^= (word >> (8 * byte)) & 255U;
    fingerprint *= 1099511628211ULL;
  }
}
auto norm(RigidVector3 p) -> double {
  return std::sqrt((p.x * p.x + p.y * p.y) + p.z * p.z);
}
auto fixture(Seed seed, bool returning = false) -> FreedomJumpRequest {
  const auto route = generate_first_universe_route(seed);
  const auto identities = generate_first_intersystem_identities(seed);
  const auto system = need(
      returning
          ? generate_physical_local_system(identities.target_system_seed, 2)
          : generate_physical_origin_system(seed, 2));
  FreedomJumpRequest request;
  request.universe_seed = seed;
  request.craft = make_freedom_new_game_document(seed).state.craft;
  request.source.craft = {kWayfarerFrameId, 1};
  request.source.frame = {RigidFrameKind::planet_relative_inertial,
                          system.catalog.id,
                          system.catalog.planets[0].descriptor.id,
                          {}};
  request.source.tick = 120;
  request.source.position_metres = {
      0, 0, system.catalog.planets[0].descriptor.radius.value * 12000.0};
  request.destination = returning ? route.origin : route.destination;
  return request;
}
auto distance_contract() -> void {
  const auto rated = static_cast<double>(kFreedomJumpRatedMetres);
  std::uint32_t previous{};
  for (unsigned i = 1; i <= 100; ++i) {
    const auto distance = rated * (static_cast<double>(i) / 100);
    const auto aligned = need(assess_freedom_jump_distance(
        distance, IntersystemArrivalQuality::aligned));
    const auto offset = need(assess_freedom_jump_distance(
        distance, IntersystemArrivalQuality::offset));
    const auto opposed = need(assess_freedom_jump_distance(
        distance, IntersystemArrivalQuality::opposed));
    check(aligned.reach == FreedomJumpReach::rated &&
              *aligned.envelope_radius_metres >= previous &&
              *offset.envelope_radius_metres ==
                  10 * (*aligned.envelope_radius_metres) &&
              *opposed.envelope_radius_metres ==
                  100 * (*aligned.envelope_radius_metres),
          "Distance envelope is monotonic and grades have fixed multipliers");
    previous = *aligned.envelope_radius_metres;
  }
  check(previous == 2000 && need(assess_freedom_jump_distance(
                                     1, IntersystemArrivalQuality::aligned))
                                    .envelope_radius_metres == 1001,
        "Rated boundary and smallest positive distance have disclosed radii");
  const auto outside = need(assess_freedom_jump_distance(
      std::nextafter(rated, std::numeric_limits<double>::infinity()),
      IntersystemArrivalQuality::aligned));
  check(outside.reach == FreedomJumpReach::beyond_rated &&
            !outside.envelope_radius_metres && !outside.distance_millionths,
        "Beyond rated is unqualified risk, never a fabricated volume or safe "
        "clamp");
  for (double bad : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                     std::numeric_limits<double>::quiet_NaN()})
    check(
        !assess_freedom_jump_distance(bad, IntersystemArrivalQuality::aligned),
        "Invalid distance refuses before arithmetic");
  check(!assess_freedom_jump_distance(
            1, static_cast<IntersystemArrivalQuality>(255)),
        "Invalid grade refuses");
}
auto route_contract(Seed seed, bool returning) -> void {
  auto r = fixture(seed, returning);
  const auto original = r;
  const auto p = need(preview_freedom_jump(r));
  const auto route = generate_first_universe_route(seed);
  const auto ids = generate_first_intersystem_identities(seed);
  const auto from =
      need(returning ? generate_physical_local_system(ids.target_system_seed, 2)
                     : generate_physical_origin_system(seed, 2));
  const auto to = need(
      returning ? generate_physical_origin_system(seed, 2)
                : generate_physical_local_system(ids.target_system_seed, 2));
  const auto source = need(resolve_planet_ephemeris(
      from, *r.source.frame.planet, {r.source.tick, 0}));
  const auto from_anchor =
      returning ? route.destination_position : route.origin_position;
  const auto to_anchor =
      returning ? route.origin_position : route.destination_position;
  const RigidVector3 leg{static_cast<double>(to_anchor.x - from_anchor.x) +
                             (p.nominal_arrival.x - p.source_position.x),
                         static_cast<double>(to_anchor.y - from_anchor.y) +
                             (p.nominal_arrival.y - p.source_position.y),
                         static_cast<double>(to_anchor.z - from_anchor.z) +
                             (p.nominal_arrival.z - p.source_position.z)};
  check(p.distance_metres == norm(leg) &&
            p.source_position.x ==
                source.position.x + r.source.position_metres.x &&
            p.source_position.z ==
                source.position.z + r.source.position_metres.z,
        "Actual endpoint offsets and body-relative source enter reach");
  check(p.arrival_tick == r.source.tick + kJumpTransitTicks && p.volume &&
            p.distance.reach == FreedomJumpReach::rated,
        "Both real generated route directions fit selected policy");
  if (returning) {
    const auto station = generate_origin_station(seed);
    const auto ephem = need(
        resolve_origin_station_ephemeris(to, station, {p.arrival_tick, 0}));
    check(std::abs(norm({p.nominal_arrival.x - ephem.position.x,
                         p.nominal_arrival.y - ephem.position.y,
                         p.nominal_arrival.z - ephem.position.z}) -
                   40000) < 0.001,
          "Return uses actual station position at arrival tick");
  } else {
    const auto body = need(
        resolve_planet_ephemeris(to, p.reference_planet, {p.arrival_tick, 0}));
    check(p.nominal_arrival.x == body.position.x &&
              p.nominal_arrival.y == body.position.y &&
              p.nominal_arrival.z ==
                  body.position.z +
                      to.catalog.planets[0].descriptor.radius.value * 10000,
          "Outbound uses generated reference body at arrival tick");
  }
  const auto frozen = need(freeze_freedom_jump_arrival(r));
  check(frozen == need(freeze_freedom_jump_arrival(r)) &&
            validate_frozen_freedom_jump_arrival(frozen).has_value() &&
            r == original,
        "Pure repeat resolution freezes same complete payload without changing "
        "source");
  const auto radius = *p.distance.envelope_radius_metres;
  check(norm({frozen.point.x - p.nominal_arrival.x,
              frozen.point.y - p.nominal_arrival.y,
              frozen.point.z - p.nominal_arrival.z}) <= radius,
        "Hidden deterministic point stays inside assessed volume");
  check(frozen.point_assessment == need(assess_freedom_arrival_volume(
                                       to, p.arrival_tick, frozen.point, 0)),
        "Point hazards use actual generated bodies without safe reroll");
  auto tampered = frozen;
  tampered.point.x += 1;
  check(!validate_frozen_freedom_jump_arrival(tampered),
        "Mutating bound point refuses");
  tampered = frozen;
  ++tampered.preview.arrival_tick;
  check(!validate_frozen_freedom_jump_arrival(tampered),
        "Mutating bound clock refuses");
  tampered = frozen;
  tampered.point_assessment.intersects_star =
      !tampered.point_assessment.intersects_star;
  check(!validate_frozen_freedom_jump_arrival(tampered),
        "Forging consequence inputs refuses");
  tampered = frozen;
  ++tampered.preview.request.attempt;
  check(!validate_frozen_freedom_jump_arrival(tampered),
        "Changing frozen attempt refuses old payload");
  ++r.attempt;
  check(need(freeze_freedom_jump_arrival(r)).sample_seed != frozen.sample_seed,
        "New attempt has independent deterministic sample");
  for (auto word : {seed.value, std::uint64_t{returning},
                    frozen.sample_seed.value, p.reference_planet.value,
                    std::bit_cast<std::uint64_t>(p.distance_metres),
                    std::bit_cast<std::uint64_t>(frozen.point.x),
                    std::bit_cast<std::uint64_t>(frozen.point.y),
                    std::bit_cast<std::uint64_t>(frozen.point.z)})
    hash(word);
  for (double offset : {-static_cast<double>(kLocalSystemBoundaryMetres),
                        static_cast<double>(kLocalSystemBoundaryMetres)}) {
    r = original;
    r.source.position_metres = {offset, 0, 0};
    check(preview_freedom_jump(r).has_value(),
          "Qualified local coordinate edge is supported in both directions");
    r.source.position_metres.x = std::nextafter(offset, offset * 2);
    check(!preview_freedom_jump(r),
          "One step beyond local qualified domain refuses without clamping");
  }
}
auto alignment_contract() -> void {
  auto r = fixture(Seed{42});
  r.profile = IntersystemRuleProfile::pilot;
  // Seed 42 lies on -Z. Small ephemeris/local offsets remain inside 3 degrees.
  const auto aligned = need(preview_freedom_jump(r));
  check(aligned.alignment.quality == IntersystemArrivalQuality::aligned,
        "Actual forward axis aligns on seed 42 outbound");
  r.source.orientation = need(normalize_rigid_orientation(
      {std::cos(0.17453292519943295), 0, std::sin(0.17453292519943295), 0}));
  const auto offset = need(preview_freedom_jump(r));
  check(offset.alignment.quality == IntersystemArrivalQuality::offset &&
            *offset.distance.envelope_radius_metres ==
                10 * (*aligned.distance.envelope_radius_metres),
        "Actual yaw widens Pilot uncertainty");
  r.source.orientation = {0, 0, 1, 0};
  const auto opposed = need(preview_freedom_jump(r));
  check(opposed.alignment.quality == IntersystemArrivalQuality::opposed &&
            *opposed.distance.envelope_radius_metres ==
                100 * (*aligned.distance.envelope_radius_metres),
        "Opposed actual yaw widens Pilot uncertainty again");
  r.profile = IntersystemRuleProfile::assisted;
  check(need(preview_freedom_jump(r)).distance == aligned.distance,
        "Assisted compensates grade but keeps actual pose assessment");
  r = fixture(Seed{42});
  r.profile = IntersystemRuleProfile::pilot;
  r.source.linear_velocity_metres_per_second = {1200, 0, 0};
  check(need(preview_freedom_jump(r)).alignment.quality ==
            IntersystemArrivalQuality::aligned,
        "Inclusive aligned drift threshold");
  r.source.linear_velocity_metres_per_second.x = 1206;
  check(need(preview_freedom_jump(r)).alignment.quality ==
            IntersystemArrivalQuality::offset,
        "Drift above rounded aligned threshold widens volume");
  r.source.linear_velocity_metres_per_second.x = 12006;
  check(need(preview_freedom_jump(r)).alignment.quality ==
            IntersystemArrivalQuality::opposed,
        "Drift above offset threshold widens volume again");
}
auto volume_contract() -> void {
  const auto system = need(generate_physical_origin_system(Seed{42}, 2));
  const auto star_radius = system.catalog.star.radius_kilometres * 1000.0;
  const auto star = need(assess_freedom_arrival_volume(
      system, 120, {star_radius + 1000, 0, 0}, 1000));
  check(star.intersects_star,
        "Arrival sphere includes exact stellar surface tangency");
  check(!need(assess_freedom_arrival_volume(
                  system, 120,
                  {std::nextafter(star_radius + 1000,
                                  std::numeric_limits<double>::infinity()),
                   0, 0},
                  1000))
             .intersects_star,
        "One step outside stellar tangency excludes star");
  for (const auto& body : system.catalog.planets) {
    const auto ephem =
        need(resolve_planet_ephemeris(system, body.descriptor.id, {120, 0}));
    const auto point =
        need(assess_freedom_arrival_volume(system, 120, ephem.position, 0));
    check(std::ranges::find(point.intersecting_planets, body.descriptor.id) !=
              point.intersecting_planets.end(),
          "Point inside each actual generated planet is flagged");
    auto centre = ephem.position;
    centre.x += body.descriptor.radius.value * 1000.0 + 1000;
    const auto tangent =
        need(assess_freedom_arrival_volume(system, 120, centre, 1000));
    check(std::ranges::find(tangent.intersecting_planets, body.descriptor.id) !=
              tangent.intersecting_planets.end(),
          "Planet tangency is included in full arrival volume");
    centre.x =
        std::nextafter(centre.x, std::numeric_limits<double>::infinity());
    const auto outside =
        need(assess_freedom_arrival_volume(system, 120, centre, 1000));
    check(
        std::ranges::find(outside.intersecting_planets, body.descriptor.id) ==
            outside.intersecting_planets.end(),
        "One representable step outside planet tangency excludes that planet");
  }
  check(system == need(generate_physical_origin_system(Seed{42}, 2)),
        "Hazard assessment never consumes generated-world random state");
  for (double radius :
       {-1.0, 1000000001.0, std::numeric_limits<double>::quiet_NaN()})
    check(!assess_freedom_arrival_volume(system, 120, {0, 0, 0}, radius),
          "Invalid volume radius refuses");
  check(!assess_freedom_arrival_volume(
            system, 120, {std::numeric_limits<double>::infinity(), 0, 0}, 0),
        "Nonfinite centre refuses");
  check(!assess_freedom_arrival_volume(
            system, std::numeric_limits<SimulationTick>::max(), {0, 0, 0}, 0),
        "Overflow clock refuses");
  auto forged = system;
  ++forged.generator_version;
  check(!assess_freedom_arrival_volume(forged, 120, {0, 0, 0}, 0),
        "Unsupported physical owner refuses");
}
auto invalid_contract() -> void {
  const auto valid = fixture(Seed{42});
  const auto rejects = [&](FreedomJumpRequest r,
                           FreedomJumpTargetingError error) {
    const auto before = r;
    const auto result = preview_freedom_jump(r);
    check(!result && result.error() == error && r == before,
          "Invalid owner/state/version refuses without mutation");
  };
  auto r = valid;
  ++r.version;
  rejects(r, FreedomJumpTargetingError::unsupported_policy);
  r = valid;
  r.attempt = 0;
  rejects(r, FreedomJumpTargetingError::invalid_attempt);
  r = valid;
  r.profile = static_cast<IntersystemRuleProfile>(255);
  rejects(r, FreedomJumpTargetingError::invalid_profile);
  r = valid;
  ++r.craft.value;
  rejects(r, FreedomJumpTargetingError::invalid_owner);
  r = valid;
  r.destination = r.source.frame.system;
  rejects(r, FreedomJumpTargetingError::invalid_destination);
  r = valid;
  r.destination = {0};
  rejects(r, FreedomJumpTargetingError::invalid_destination);
  r = valid;
  r.source.tick = std::numeric_limits<SimulationTick>::max();
  rejects(r, FreedomJumpTargetingError::tick_overflow);
  r = valid;
  r.source.tick =
      std::numeric_limits<SimulationTick>::max() - kJumpTransitTicks;
  rejects(r, FreedomJumpTargetingError::tick_overflow);
  r.source.tick -= 1;
  check(
      preview_freedom_jump(r).has_value(),
      "Largest qualified commitment tick does not overflow arrival ephemeris");
  r = valid;
  r.source.craft = {kStarterShuttleFrameId, 1};
  rejects(r, FreedomJumpTargetingError::invalid_source);
  r = valid;
  r.source.frame.planet = {PlanetId{0}};
  rejects(r, FreedomJumpTargetingError::invalid_source);
  r = valid;
  r.source.orientation = {2, 0, 0, 0};
  rejects(r, FreedomJumpTargetingError::invalid_source);
  r = valid;
  r.source.position_metres.x = -0.0;
  rejects(r, FreedomJumpTargetingError::invalid_source);
  r = valid;
  r.source.linear_velocity_metres_per_second.x =
      std::numeric_limits<double>::infinity();
  rejects(r, FreedomJumpTargetingError::invalid_source);
  r = valid;
  r.source.position_metres.x = std::numeric_limits<double>::quiet_NaN();
  check(!preview_freedom_jump(r),
        "NaN source refuses before vector arithmetic");
}
} // namespace
auto main() -> int {
  try {
    distance_contract();
    alignment_contract();
    volume_contract();
    invalid_contract();
    for (auto seed :
         {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}})
      for (bool returning : {false, true})
        route_contract(seed, returning);
    for (std::uint64_t seed = 1; seed <= 64; ++seed)
      if (seed != 42)
        for (bool returning : {false, true})
          route_contract(Seed{seed}, returning);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
  check(fingerprint == 4987509392207033452ULL,
        "Policy-1 route/distance/arrival bit patterns retain their published "
        "golden");
  std::cout << checks << " jump targeting checks; fingerprint " << fingerprint
            << '\n';
  return failures ? 1 : 0;
}
