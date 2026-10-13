#include "apsis_drift/godot/saved_contact_geometry.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "apsis_drift/terrain_impact.hpp"
#include "contact_geometry_internal.hpp"

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
auto check(bool value, std::string_view label) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T, class E>
auto required(std::expected<T, E> value,
              std::source_location location = std::source_location::current())
    -> T {
  if (!value) {
    std::string reason = "unknown error";
    if constexpr (std::is_same_v<E, std::string>)
      reason = value.error();
    else if constexpr (requires { value.error().detail; })
      reason = value.error().detail;
    else if constexpr (std::is_enum_v<E>)
      reason = std::to_string(static_cast<unsigned>(value.error()));
    throw std::runtime_error("impact fixture line" +
                             std::to_string(location.line()) + ": " + reason);
  }
  return std::move(*value);
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
auto rotate(RigidOrientation q, V a) -> V {
  const double s = 2 / (((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z);
  return {
      (1 - s * (q.y * q.y + q.z * q.z)) * a.x +
          s * (q.x * q.y - q.w * q.z) * a.y + s * (q.x * q.z + q.w * q.y) * a.z,
      s * (q.x * q.y + q.w * q.z) * a.x +
          (1 - s * (q.x * q.x + q.z * q.z)) * a.y +
          s * (q.y * q.z - q.w * q.x) * a.z,
      s * (q.x * q.z - q.w * q.y) * a.x + s * (q.y * q.z + q.w * q.x) * a.y +
          (1 - s * (q.x * q.x + q.y * q.y)) * a.z};
}
struct Fixture {
  PhysicalLocalSystem owner =
      required(generate_physical_origin_system(Seed{42}, 2));
  PlanetDescriptor planet =
      owner.catalog.planets[kOriginHomePlanetOrdinal].descriptor;
  PhysicalPlanetRotationRecipe rotation =
      required(generate_planet_rotation_recipe(owner, planet.id));
  RigidBodyWorldContext context{owner};
  godot_spike::ContactTriangle triangle;
  V center, n;
  RigidOrientation orientation;
  std::uint32_t policy;
  explicit Fixture(std::uint32_t selected) : policy(selected) {
    auto cache = required(TerrainTileCache::create(1));
    triangle = required(godot_spike::detail::build_selected_contact_triangle(
        owner, planet, *godot_spike::saved_contact_recipe(policy),
        {planet.id, CubeFace::positive_x, 4000, 4000, 12, 13, 0}, cache));
    const auto a = triangle.vertices[0], b = triangle.vertices[1],
               c = triangle.vertices[2];
    center = {a.x + .3 * ((b.x - a.x) + (c.x - a.x)),
              a.y + .3 * ((b.y - a.y) + (c.y - a.y)),
              a.z + .3 * ((b.z - a.z) + (c.z - a.z))};
    n = {triangle.outward_normal.x, triangle.outward_normal.y,
         triangle.outward_normal.z};
    const double w = 1 + n.y, magnitude = std::hypot(w, n.z, n.x);
    orientation = {w / magnitude, n.z / magnitude, 0, -n.x / magnitude};
  }
  auto fixed(double gap, bool pad = false) const -> RigidBodyState {
    RigidBodyState state;
    state.tick = 1200;
    state.craft = {kWayfarerFrameId, 1};
    state.frame = {
        RigidFrameKind::planet_fixed, owner.catalog.id, planet.id, {}};
    state.orientation = orientation;
    const auto craft = required(resolve_craft_frame(state.craft)).properties;
    V bottom;
    if (pad) {
      const auto p = craft.supports[0].contact_mm;
      bottom = {p[0] / 1000.0, p[1] / 1000.0, p[2] / 1000.0};
    } else {
      bottom = {(craft.hull_min_mm[0] + craft.hull_max_mm[0]) / 2000.0,
                craft.hull_min_mm[1] / 1000.0,
                (craft.hull_min_mm[2] + craft.hull_max_mm[2]) / 2000.0};
    }
    const auto com = craft.center_of_mass_mm;
    bottom = sub(bottom, {com[0] / 1000.0, com[1] / 1000.0, com[2] / 1000.0});
    state.position_metres =
        sub(add(center, mul(n, gap)), rotate(orientation, bottom));
    return state;
  }
  auto document(RigidBodyState fixed) const -> FreedomFlightSaveDocument {
    fixed = required(canonicalize_rigid_body_state(context, fixed));
    const auto state =
        required(reframe_rigid_body(context, fixed,
                                    {{RigidFrameKind::planet_relative_inertial,
                                      owner.catalog.id,
                                      planet.id,
                                      {}},
                                     fixed.tick},
                                    rotation));
    FreedomFlightSaveDocument result{
        make_freedom_new_game_document(Seed{42}), state, {}};
    result.origin.state.tick = state.tick;
    result.model.physical_catalog = 2;
    result.model.physical_ephemeris = 2;
    result.model.terrain_policy = policy;
    return result;
  }
  auto pair(double gap, double travel, double speed, bool pad = false) const
      -> std::pair<FreedomFlightSaveDocument, FreedomFlightSaveDocument> {
    auto before = fixed(gap, pad);
    before.linear_velocity_metres_per_second = mul(n, -speed);
    auto after = before;
    ++after.tick;
    after.position_metres = add(after.position_metres, mul(n, travel));
    return {document(before), document(after)};
  }
  auto assess(const auto& pair, bool gear = false) const -> TerrainImpactSweep {
    return required(assess_terrain_impact(owner, rotation, pair.first,
                                          pair.second, gear, policy));
  }
};
auto invalid(const Fixture& f) -> void {
  const auto pair = f.pair(100, 0, 0);
  auto broken = pair.second;
  broken.flight.position_metres.x = std::numeric_limits<double>::quiet_NaN();
  check(!assess_terrain_impact(f.owner, f.rotation, pair.first, broken, false,
                               f.policy),
        "nonfinite state rejected before source");
  broken = pair.second;
  ++broken.flight.tick;
  broken.origin.state.tick = broken.flight.tick;
  check(!assess_terrain_impact(f.owner, f.rotation, pair.first, broken, false,
                               f.policy),
        "nonconsecutive clocks rejected");
  check(!assess_terrain_impact(f.owner, f.rotation, pair.first, pair.second,
                               false, 0),
        "unknown terrain selection rejected");
  broken = pair.second;
  broken.model.terrain_policy = f.policy == 1 ? 2 : 1;
  check(!assess_terrain_impact(f.owner, f.rotation, pair.first, broken, false,
                               f.policy),
        "candidate terrain owner mismatch rejected");
  auto owner = f.owner;
  owner.generator_version = 999;
  check(!assess_terrain_impact(owner, f.rotation, pair.first, pair.second,
                               false, f.policy),
        "forged physical owner rejected");
}
auto actual_contacts(const Fixture& f) -> void {
  const auto pair = f.pair(10, -20, 2400);
  const auto encoded =
      required(encode_freedom_flight_document_json(pair.first));
  const auto sweep = f.assess(pair);
  check(sweep.classification == TerrainImpactClass::impact &&
            sweep.witness.has_value(),
        "actual hull crosses finite selected terrain triangle");
  if (!sweep.witness) return;
  const auto& event = *sweep.witness;
  check(event.fraction > 0 && event.fraction < 1 &&
            event.region == CraftImpactRegion::forebody,
        "earliest translated hull witness occurs within tick");
  check(event.terrain_policy == f.policy &&
            event.tick == pair.second.flight.tick &&
            event.craft == pair.first.origin.state.craft,
        "witness binds selected terrain tick and instance");
  check(event.closing_speed_metres_per_second > 2300 &&
            event.closing_speed_metres_per_second < 2500,
        "closing speed measured on geometric terrain normal");
  auto cache = required(TerrainTileCache::create(1));
  const auto triangle =
      required(godot_spike::detail::build_selected_contact_triangle(
          f.owner, f.planet, *godot_spike::saved_contact_recipe(f.policy),
          event.triangle, cache));
  check(event.outward_normal_planet_fixed == V{triangle.outward_normal.x,
                                               triangle.outward_normal.y,
                                               triangle.outward_normal.z},
        "normal is authoritative top facet geometry");
  check(sweep == f.assess(pair),
        "repeated deterministic query returns exact witness");
  // Start20mm before the certified earliest contact along the same complete
  // sweep path, rather than assuming one arbitrary facet is the highest under
  // the hull. Original geometry is clear; expanded geometry is still near.
  auto near = f.fixed(10);
  near.position_metres =
      add(near.position_metres, mul(f.n, -20 * event.fraction + .02));
  auto next_near = near;
  ++next_near.tick;
  const auto near_result =
      f.assess(std::pair{f.document(near), f.document(next_near)});
  check(near_result.classification == TerrainImpactClass::near &&
            !near_result.witness && near_result.minimum_clearance_metres == 0,
        "complete finite clear proxy near actual contact permits ordinary "
        "physics");
  check(required(encode_freedom_flight_document_json(pair.first)) == encoded,
        "query leaves source save unchanged");
  const auto response = required(respond_terrain_impact(
      f.owner, f.rotation, pair.first, pair.second, event));
  const auto fixed = required(reframe_rigid_body(
      f.context, response,
      {{RigidFrameKind::planet_fixed, f.owner.catalog.id, f.planet.id, {}},
       response.tick},
      f.rotation));
  check(response.tick == pair.second.flight.tick &&
            response.frame == pair.second.flight.frame,
        "response owns candidate clock and native frame");
  check(std::abs(dot(fixed.linear_velocity_metres_per_second,
                     event.outward_normal_planet_fixed)) < 1e-7 &&
            length(fixed.angular_velocity_radians_per_second) < 1e-12,
        "response removes inward velocity and ground relative spin");
  const auto start = f.fixed(10);
  const auto expected =
      add(add(start.position_metres, mul(f.n, -20 * event.fraction)),
          mul(event.outward_normal_planet_fixed,
              kTerrainImpactResponseRetreatMetres));
  check(length(sub(fixed.position_metres, expected)) < 1e-6,
        "response stops at contact fraction with 2mm outward retreat");
  auto forged = event;
  ++forged.source_checksum;
  check(!respond_terrain_impact(f.owner, f.rotation, pair.first, pair.second,
                                forged),
        "stale witness rejected atomically");
  forged = event;
  forged.contact_point_craft_metres.x += .1;
  check(!respond_terrain_impact(f.owner, f.rotation, pair.first, pair.second,
                                forged),
        "noncontact witness location rejected");
  const auto pads = f.pair(10, -20, 2400, true);
  const auto pad = f.assess(pads, true);
  check(pad.witness &&
            pad.witness->region == CraftImpactRegion::landing_structure,
        "deployed support strikes before hull");
  const auto separating = f.pair(-.2, .4, -48, true);
  const auto separation = f.assess(separating, true);
  check(separation.witness && separation.witness->fraction == 0 &&
            separation.witness->closing_speed_metres_per_second == 0,
        "initial separating pad overlap remains nondamaging contact");
  if (separation.witness)
    check(!respond_terrain_impact(f.owner, f.rotation, separating.first,
                                  separating.second, *separation.witness),
          "separating contact cannot force response or trap liftoff");
}
auto coverage_limits(const Fixture& f) -> void {
  const auto clear = f.assess(f.pair(100, 0, 0));
  check(clear.classification == TerrainImpactClass::clear &&
            clear.minimum_clearance_metres >= .05 && !clear.witness,
        "fully covered expanded hull certifies clear margin");
  const auto far = f.assess(f.pair(100000, -100, 12000));
  check(far.classification == TerrainImpactClass::clear &&
            far.minimum_clearance_metres > 50000,
        "derived whole-field bound safely clears orbit without local samples");
  const auto properties =
      required(resolve_craft_frame({kWayfarerFrameId, 1})).properties;
  const auto low = properties.hull_min_mm, high = properties.hull_max_mm,
             com = properties.center_of_mass_mm;
  const V proxy_center{(low[0] + high[0]) / 2000.0 - com[0] / 1000.0,
                       (low[1] + high[1]) / 2000.0 - com[1] / 1000.0,
                       (low[2] + high[2]) / 2000.0 - com[2] / 1000.0};
  const V half{(high[0] - low[0]) / 2000.0, (high[1] - low[1]) / 2000.0,
               (high[2] - low[2]) / 2000.0};
  auto passing = f.fixed(10);
  // Nominal closest radius leaves only51mm beyond the published field/proxy
  // bound; 2e12m translation makes cancellation uncertainty larger than1mm.
  passing.position_metres = {-1e12,
                             f.planet.radius.value * 1000.0 + 17070 +
                                 length(proxy_center) + length(half) + .051,
                             0};
  passing.orientation = {1, 0, 0, 0};
  auto passed = passing;
  ++passed.tick;
  passed.position_metres.x = 1e12;
  const auto uncertain =
      f.assess(std::pair{f.document(passing), f.document(passed)});
  check(uncertain.classification == TerrainImpactClass::unknown &&
            !uncertain.witness,
        "extreme passing sweep cannot certify5cm margin through cancellation");
  const auto long_move = f.assess(f.pair(10, -100, 12000));
  check(long_move.classification == TerrainImpactClass::unknown &&
            !long_move.witness,
        "unsupported near-ground long sweep cannot invent impact");
  auto seam = f.fixed(10);
  const double r = f.planet.radius.value * 1000.0 + 100;
  seam.position_metres = {r / std::sqrt(2.0), r / std::sqrt(2.0), 0};
  auto end = seam;
  ++end.tick;
  const auto result = f.assess(std::pair{f.document(seam), f.document(end)});
  check(result.classification == TerrainImpactClass::unknown && !result.witness,
        "cross-face proxy coverage is explicitly unknown");
  const auto buried = f.assess(f.pair(-100, 0, 0));
  check(buried.classification == TerrainImpactClass::unknown && !buried.witness,
        "fully buried proxy is not a certified clear or invented surface hit");
}
} // namespace
int main() {
  try {
    for (std::uint32_t policy : {1U, 2U}) {
      const Fixture fixture{policy};
      invalid(fixture);
      actual_contacts(fixture);
      coverage_limits(fixture);
    }
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << error.what() << '\n';
  }
  std::cout << checks << " terrain impact checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
