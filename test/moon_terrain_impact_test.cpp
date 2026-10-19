#include "apsis_drift/moon_terrain_impact.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "moon_contact_fixture.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace {
using namespace apsis_drift;
using V = RigidVector3;
int checks{}, failures{};
auto check(bool ok, std::string_view label) -> void {
  ++checks;
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T, class E>
auto need(std::expected<T, E> result,
          std::source_location at = std::source_location::current()) -> T {
  if (!result) {
    std::string reason;
    if constexpr (std::is_same_v<E, std::string>) reason = result.error();
    throw std::runtime_error("Moon impact fixture line" +
                             std::to_string(at.line()) + ": " + reason);
  }
  return std::move(*result);
}
auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto mul(V a, double s) -> V {
  return {a.x * s, a.y * s, a.z * s};
}
auto dot(V a, V b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto length(V a) -> double {
  return std::hypot(a.x, a.y, a.z);
}
// Independent matrix expansion; no production SAT/shape helper is used.
auto rotate(RigidOrientation q, V v) -> V {
  const double s = 2 / (((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z);
  return {
      (1 - s * (q.y * q.y + q.z * q.z)) * v.x +
          s * (q.x * q.y - q.w * q.z) * v.y + s * (q.x * q.z + q.w * q.y) * v.z,
      s * (q.x * q.y + q.w * q.z) * v.x +
          (1 - s * (q.x * q.x + q.z * q.z)) * v.y +
          s * (q.y * q.z - q.w * q.x) * v.z,
      s * (q.x * q.z - q.w * q.y) * v.x + s * (q.y * q.z + q.w * q.x) * v.y +
          (1 - s * (q.x * q.x + q.y * q.y)) * v.z};
}
struct Fixture {
  test_fixture::MoonContactFixture source =
      test_fixture::make_moon_contact_fixture();
  RigidBodyWorldContext context{source.moon};
  V center, n;
  Fixture() {
    const auto& vertices = source.triangle.vertices;
    center = {vertices[0].x + .3 * ((vertices[1].x - vertices[0].x) +
                                    (vertices[2].x - vertices[0].x)),
              vertices[0].y + .3 * ((vertices[1].y - vertices[0].y) +
                                    (vertices[2].y - vertices[0].y)),
              vertices[0].z + .3 * ((vertices[1].z - vertices[0].z) +
                                    (vertices[2].z - vertices[0].z))};
    n = {source.triangle.outward_normal.x, source.triangle.outward_normal.y,
         source.triangle.outward_normal.z};
  }
  auto fixed(double gap, bool pad = false) const -> RigidBodyState {
    auto state = source.fixed;
    state.linear_velocity_metres_per_second = {};
    state.angular_velocity_radians_per_second = {};
    const auto properties = need(resolve_craft_frame(state.craft)).properties;
    V bottom;
    if (pad) {
      const auto p = properties.supports[0].contact_mm;
      bottom = {p[0] / 1000., p[1] / 1000., p[2] / 1000.};
    } else
      bottom = {(properties.hull_min_mm[0] + properties.hull_max_mm[0]) / 2000.,
                properties.hull_min_mm[1] / 1000.,
                (properties.hull_min_mm[2] + properties.hull_max_mm[2]) /
                    2000.};
    const auto com = properties.center_of_mass_mm;
    bottom = sub(bottom, {com[0] / 1000., com[1] / 1000., com[2] / 1000.});
    state.position_metres =
        sub(add(center, mul(n, gap)), rotate(state.orientation, bottom));
    return need(canonicalize_rigid_body_state(context, state));
  }
  auto native(RigidBodyState fixed_state) const -> RigidBodyState {
    fixed_state = need(canonicalize_rigid_body_state(context, fixed_state));
    return need(reframe_rigid_body(context, fixed_state,
                                   {{RigidFrameKind::body_relative_inertial,
                                     context.system.id,
                                     {},
                                     {},
                                     source.moon.body},
                                    fixed_state.tick},
                                   source.moon));
  }
  auto pair(double gap, double travel, double speed, bool pad = false) const
      -> std::pair<RigidBodyState, RigidBodyState> {
    auto before = fixed(gap, pad);
    before.linear_velocity_metres_per_second = mul(n, -speed);
    auto after = before;
    ++after.tick;
    after.position_metres = add(after.position_metres, mul(n, travel));
    return {native(before), native(after)};
  }
  auto assess(const std::pair<RigidBodyState, RigidBodyState>& states,
              bool gear = false) const -> MoonTerrainImpactSweep {
    return need(assess_moon_terrain_impact(source.moon, states.first,
                                           states.second, gear));
  }
};
auto invalid(const Fixture& f) -> void {
  const auto states = f.pair(100, 0, 0);
  const auto source_hash =
      need(rigid_body_state_checksum(f.context, states.first));
  auto bad = states.second;
  bad.position_metres.x = std::numeric_limits<double>::quiet_NaN();
  check(!assess_moon_terrain_impact(f.source.moon, states.first, bad, false),
        "NaN refuses before terrain");
  bad = states.second;
  bad.linear_velocity_metres_per_second.z =
      std::numeric_limits<double>::infinity();
  check(!assess_moon_terrain_impact(f.source.moon, states.first, bad, false),
        "Infinite velocity refuses before terrain");
  bad = states.second;
  ++bad.tick;
  check(!assess_moon_terrain_impact(f.source.moon, states.first, bad, false),
        "Nonconsecutive candidate tick refuses");
  bad = states.second;
  ++bad.frame.body->value;
  check(!assess_moon_terrain_impact(f.source.moon, states.first, bad, false),
        "Foreign typed body refuses");
  bad = states.second;
  bad.frame.body->kind = BodyKind::planet;
  check(!assess_moon_terrain_impact(f.source.moon, states.first, bad, false),
        "Numeric Planet alias refuses");
  bad = states.second;
  bad.frame.kind = RigidFrameKind::planet_relative_inertial;
  check(!assess_moon_terrain_impact(f.source.moon, states.first, bad, false),
        "Planet frame cannot carry moon collision");
  auto owner = f.source.moon;
  ++owner.radius_metres;
  check(!assess_moon_terrain_impact(owner, states.first, states.second, false),
        "Forged moon descriptor refuses");
  check(!assess_moon_terrain_impact(f.source.moon, states.first, states.second,
                                    false, 99),
        "Unknown terrain recipe refuses");
  check(need(rigid_body_state_checksum(f.context, states.first)) == source_hash,
        "All failures preserve source state");
}
auto contacts(const Fixture& f) -> void {
  const auto states = f.pair(10, -20, 2400);
  const auto before =
      need(encode_rigid_body_state_json(f.context, states.first));
  const auto result = f.assess(states);
  std::cout << "Actual moon hull sweep class="
            << static_cast<unsigned>(result.classification)
            << " witness=" << bool(result.witness) << '\n';
  check(
      result.classification == MoonTerrainImpactClass::impact &&
          result.witness.has_value(),
      "Actual continuous finite hull sweep catches crossing between endpoints");
  if (!result.witness) return;
  const auto& event = *result.witness;
  check(event.fraction > 0 && event.fraction < 1 &&
            event.region == MoonCraftImpactRegion::forebody,
        "Earliest hull contact has interior sweep fraction");
  check(event.body == f.source.moon.body && event.triangle.body == event.body &&
            event.system == f.context.system.id &&
            event.tick == states.second.tick,
        "Witness has genuine typed moon/source/tick identity");
  check(event.source_checksum ==
                need(rigid_body_state_checksum(f.context, states.first)) &&
            event.candidate_checksum ==
                need(rigid_body_state_checksum(f.context, states.second)),
        "Witness binds both complete actual states");
  const auto sampler = need(MoonTerrainSampler::create(f.source.moon));
  const auto triangle =
      need(build_moon_contact_triangle(sampler, event.triangle));
  check(event.outward_normal_moon_fixed == V{triangle.outward_normal.x,
                                             triangle.outward_normal.y,
                                             triangle.outward_normal.z},
        "Witness normal is actual MoonContact facet");
  check(event.closing_speed_metres_per_second > 2300 &&
            event.closing_speed_metres_per_second < 2500,
        "Closing speed projects actual own-frame velocity");
  // Independently reconstruct the contact in fixed coordinates and verify
  // the finite facet plane/barycentric domain, not an infinite plane hit.
  const auto properties =
      need(resolve_craft_frame(states.first.craft)).properties;
  const auto com = properties.center_of_mass_mm;
  const auto local = sub(event.contact_point_craft_metres,
                         {com[0] / 1000., com[1] / 1000., com[2] / 1000.});
  const auto start = f.fixed(10);
  const auto contact =
      add(add(start.position_metres, mul(f.n, -20 * event.fraction)),
          rotate(start.orientation, local));
  const V a{triangle.vertices[0].x, triangle.vertices[0].y,
            triangle.vertices[0].z};
  const V b{triangle.vertices[1].x, triangle.vertices[1].y,
            triangle.vertices[1].z};
  const V c{triangle.vertices[2].x, triangle.vertices[2].y,
            triangle.vertices[2].z};
  check(std::abs(dot(sub(contact, a), event.outward_normal_moon_fixed)) < 1e-6,
        "Witness reconstructs onto actual triangle plane");
  const auto u = sub(b, a), v = sub(c, a), w = sub(contact, a);
  const auto uu = dot(u, u), uv = dot(u, v), vv = dot(v, v), wu = dot(w, u),
             wv = dot(w, v);
  const auto det = uu * vv - uv * uv;
  const auto beta = (vv * wu - uv * wv) / det,
             gamma = (uu * wv - uv * wu) / det;
  check(beta >= -1e-6 && gamma >= -1e-6 && beta + gamma <= 1 + 1e-6,
        "Witness lies in finite facet rather than plane extension");
  check(result == f.assess(states),
        "Repeated own-state query returns exact deterministic event");
  const auto response = need(respond_moon_terrain_impact(
      f.source.moon, states.first, states.second, event));
  const auto fixed = need(
      reframe_rigid_body(f.context, response,
                         {f.source.fixed.frame, response.tick}, f.source.moon));
  const auto expected =
      add(add(start.position_metres, mul(f.n, -20 * event.fraction)),
          mul(event.outward_normal_moon_fixed,
              kMoonTerrainImpactResponseRetreatMetres));
  check(response.frame == states.second.frame &&
            response.tick == states.second.tick,
        "Response retains native frame5 and candidate clock");
  check(length(sub(fixed.position_metres, expected)) < 1e-6,
        "Response stops at continuous contact plus2mm retreat");
  check(std::abs(dot(fixed.linear_velocity_metres_per_second,
                     event.outward_normal_moon_fixed)) < 1e-7 &&
            length(fixed.angular_velocity_radians_per_second) < 1e-12,
        "Response removes inward velocity and spin");
  const auto tangent = sub(fixed.linear_velocity_metres_per_second,
                           mul(event.outward_normal_moon_fixed,
                               dot(fixed.linear_velocity_metres_per_second,
                                   event.outward_normal_moon_fixed)));
  const auto candidate_fixed = need(reframe_rigid_body(
      f.context, states.second, {f.source.fixed.frame, states.second.tick},
      f.source.moon));
  const auto initial_tangent =
      sub(candidate_fixed.linear_velocity_metres_per_second,
          mul(event.outward_normal_moon_fixed,
              dot(candidate_fixed.linear_velocity_metres_per_second,
                  event.outward_normal_moon_fixed)));
  check(length(sub(tangent, initial_tangent)) < 1e-7,
        "Impact response preserves tangent velocity");
  for (unsigned field = 0; field < 5; ++field) {
    auto forged = event;
    if (field == 0) ++forged.source_checksum;
    if (field == 1) ++forged.body.value;
    if (field == 2) forged.contact_point_craft_metres.x += .1;
    if (field == 3) forged.fraction += .001;
    if (field == 4) forged.closing_speed_metres_per_second += 1;
    check(!respond_moon_terrain_impact(f.source.moon, states.first,
                                       states.second, forged),
          "Stale/foreign/substituted witness refuses atomically");
  }
  check(need(encode_rigid_body_state_json(f.context, states.first)) == before,
        "Queries and response never mutate source state");
  auto near = f.fixed(10);
  near.position_metres =
      add(near.position_metres, mul(f.n, -20 * event.fraction + .02));
  auto after = near;
  ++after.tick;
  const auto close = f.assess({f.native(near), f.native(after)});
  check(close.classification == MoonTerrainImpactClass::near &&
            !close.witness && close.minimum_clearance_metres == 0,
        "20mm before actual first contact stays near, not false clear or hit");
  const auto pads = f.assess(f.pair(10, -20, 2400, true), true);
  check(pads.witness &&
            pads.witness->region == MoonCraftImpactRegion::landing_structure,
        "Actual deployed stroke proxy catches gear before hull");
  const auto separation_states = f.pair(-.2, .4, -48, true);
  const auto separation = f.assess(separation_states, true);
  check(separation.witness && separation.witness->fraction == 0 &&
            separation.witness->closing_speed_metres_per_second == 0,
        "Initial separating gear overlap stays nondamaging");
  if (separation.witness)
    check(!respond_moon_terrain_impact(f.source.moon, separation_states.first,
                                       separation_states.second,
                                       *separation.witness),
          "Separating event cannot trap liftoff through forced response");
}
auto bounds(const Fixture& f) -> void {
  const auto clear = f.assess(f.pair(100, 0, 0));
  check(clear.classification == MoonTerrainImpactClass::clear &&
            clear.minimum_clearance_metres >= .05 && !clear.witness,
        "Complete near-field expanded sweep certifies actual clear margin");
  const auto far = f.assess(f.pair(100000, -100, 12000));
  check(far.classification == MoonTerrainImpactClass::clear &&
            far.minimum_clearance_metres > 50000,
        "Whole-recipe sphere certifies distant sweep despite local motion cap");
  const auto large = f.assess(f.pair(10, -100, 12000));
  check(large.classification == MoonTerrainImpactClass::unknown &&
            !large.witness && large.minimum_clearance_metres == 0,
        "Near-ground over64m sweep stays unknown");
  auto before = f.fixed(10), after = before;
  ++after.tick;
  after.orientation = need(normalize_rigid_orientation({.995, 0, .1, 0}));
  check(f.assess({f.native(before), f.native(after)}).classification ==
            MoonTerrainImpactClass::unknown,
        "Unsupported rotation remains unknown");
  before = f.fixed(10);
  before.angular_velocity_radians_per_second = {0, 3, 0};
  after = before;
  ++after.tick;
  check(f.assess({f.native(before), f.native(after)}).classification ==
            MoonTerrainImpactClass::unknown,
        "Excessive relative spin remains unknown");
  before = f.fixed(10);
  const auto r = f.source.moon.radius_metres + 100;
  before.position_metres = {r / std::sqrt(2.), r / std::sqrt(2.), 0};
  after = before;
  ++after.tick;
  check(f.assess({f.native(before), f.native(after)}).classification ==
            MoonTerrainImpactClass::unknown,
        "Cross-face coverage cannot fabricate clear/hit");
  const auto buried = f.assess(f.pair(-100, 0, 0));
  check(buried.classification == MoonTerrainImpactClass::unknown &&
            !buried.witness,
        "Wholly buried proxy has unknown initial history, not invented surface "
        "contact");
  // The preselected same-owner eight cardinal/diagonal directions are bounded;
  // negative relief remains genuine dry collision geometry, never sea.
  const auto sampler = need(MoonTerrainSampler::create(f.source.moon));
  bool negative{};
  for (const auto& direction :
       std::array<MoonFixedDirection, 8>{{{1, 0, 0},
                                          {-1, 0, 0},
                                          {0, 1, 0},
                                          {0, -1, 0},
                                          {0, 0, 1},
                                          {0, 0, -1},
                                          {1, 1, 1},
                                          {-1, -1, -1}}}) {
    const auto hit = need(query_moon_contact_surface(sampler, direction));
    if (hit.radial_distance_metres >= f.source.moon.radius_metres) continue;
    // Independently place the registered hull across this actual negative
    // facet. A planetary datum/water filter here would lose this real hit.
    const auto& triangle = hit.triangle;
    const auto& vertices = triangle.vertices;
    const V center{vertices[0].x + ((vertices[1].x - vertices[0].x) +
                                    (vertices[2].x - vertices[0].x)) /
                                       3,
                   vertices[0].y + ((vertices[1].y - vertices[0].y) +
                                    (vertices[2].y - vertices[0].y)) /
                                       3,
                   vertices[0].z + ((vertices[1].z - vertices[0].z) +
                                    (vertices[2].z - vertices[0].z)) /
                                       3};
    const V n{triangle.outward_normal.x, triangle.outward_normal.y,
              triangle.outward_normal.z};
    const double qw = 1 + n.y, magnitude = std::hypot(qw, n.z, n.x);
    auto state = f.fixed(10);
    state.orientation = {qw / magnitude, n.z / magnitude, 0, -n.x / magnitude};
    const auto properties = need(resolve_craft_frame(state.craft)).properties;
    const V bottom{
        (properties.hull_min_mm[0] + properties.hull_max_mm[0]) / 2000. -
            properties.center_of_mass_mm[0] / 1000.,
        properties.hull_min_mm[1] / 1000. -
            properties.center_of_mass_mm[1] / 1000.,
        (properties.hull_min_mm[2] + properties.hull_max_mm[2]) / 2000. -
            properties.center_of_mass_mm[2] / 1000.};
    state.position_metres =
        sub(add(center, mul(n, 10)), rotate(state.orientation, bottom));
    state.linear_velocity_metres_per_second = mul(n, -2400);
    auto next = state;
    ++next.tick;
    next.position_metres = add(next.position_metres, mul(n, -20));
    const auto basin = f.assess({f.native(state), f.native(next)});
    check(basin.classification == MoonTerrainImpactClass::impact &&
              basin.witness.has_value(),
          "Actual below-datum dry facet catches a swept hull rather than "
          "becoming water/unknown");
    negative = true;
    break;
  }
  check(negative, "Fixed same-owner direction corpus contains actual signed "
                  "negative relief");
}
} // namespace
int main() {
  try {
    const Fixture f;
    invalid(f);
    contacts(f);
    bounds(f);
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << e.what() << '\n';
  }
  std::cout << checks << " moon terrain impact checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
