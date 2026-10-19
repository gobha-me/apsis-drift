#include "../src/surface_walk_internal.hpp"
#include "apsis_drift/moon_surface_walker.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "moon_contact_fixture.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
using namespace apsis_drift;
using V = RigidVector3;
int failures{};
auto check(bool pass, std::string_view message) -> void {
  if (!pass) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <typename T, typename E>
auto need(const std::expected<T, E>& result) -> T {
  if (!result) {
    std::cerr << "FAIL: moon walker fixture refused\n";
    std::exit(1);
  }
  return *result;
}
auto norm(V v) -> double {
  return std::hypot(v.x, v.y, v.z);
}
auto difference(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

// Pure shared-kinematic boundary checks. This declared plane is not represented
// as a generated world, a certified landing, or proof of moon terrain support.
auto movement_boundaries(const PhysicalMoonDescriptor& moon) -> void {
  RigidBodyState craft;
  craft.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  craft.frame = {RigidFrameKind::body_fixed,
                 moon.catalog.owner.catalog.id,
                 {},
                 {},
                 moon.body};
  craft.position_metres = {0, 1000, 0};
  PlanetSurfaceWalkerState actor{1, 1, craft.frame, {20, 1000, 20}, {}, 0};
  const auto plane = [](V point) -> std::expected<V, PlanetSurfaceWalkStatus> {
    return V{point.x, 1000, point.z};
  };
  const auto step = [&](const OriginWalkControls& input, auto ground) {
    return surface_walk_detail::advance(craft, actor, input, kSimulationStep,
                                        ground, "Invalid test walker");
  };
  const auto diagonal = need(step({1, 1, 0}, plane));
  check(diagonal.status == PlanetSurfaceWalkStatus::supported &&
            norm(difference(diagonal.actor.foot_position_metres,
                            actor.foot_position_metres)) <=
                2.0 * kSimulationStep.count() + 1e-10,
        "Shared kernel normalizes diagonal walking to existing two-metre "
        "tangent demand");
  const auto refused_ground =
      [&](V point) -> std::expected<V, PlanetSurfaceWalkStatus> {
    if (point == actor.foot_position_metres) return point;
    return std::unexpected{PlanetSurfaceWalkStatus::slope};
  };
  const auto slope = need(step({1, 0, .25}, refused_ground));
  check(slope.status == PlanetSurfaceWalkStatus::slope &&
            slope.actor.foot_position_metres == actor.foot_position_metres &&
            slope.actor.heading_radians == .25 &&
            slope.actor.velocity_metres_per_second == V{},
        "Unsupported slope preserves feet while accepting look heading and "
        "clearing movement velocity");
  const auto cliff = [&](V point) -> std::expected<V, PlanetSurfaceWalkStatus> {
    if (point == actor.foot_position_metres) return point;
    point.y += .301;
    return point;
  };
  const auto too_high = need(step({1, 0, 0}, cliff));
  check(too_high.status == PlanetSurfaceWalkStatus::step &&
            too_high.actor.foot_position_metres == actor.foot_position_metres,
        "Shared kernel refuses an over-thirty-centimetre projected step");
  const auto steep = [&](V point) -> std::expected<V, PlanetSurfaceWalkStatus> {
    if (point == actor.foot_position_metres) return point;
    point.y += .020;
    return point;
  };
  const auto too_fast = need(step({1, 0, 0}, steep));
  check(
      too_fast.status == PlanetSurfaceWalkStatus::slope &&
          too_fast.actor.foot_position_metres == actor.foot_position_metres,
      "Shared kernel keeps the existing 2.45m/s three-dimensional climb bound");
  const auto properties = need(resolve_craft_frame(craft.craft)).properties;
  const double floor =
      craft.position_metres.y + properties.hull_min_mm[1] / 1000.0;
  check(surface_walk_detail::blocked(craft, {0, floor, 0}, {0, floor, 0}) &&
            !surface_walk_detail::blocked(craft, {100, 1000, 100},
                                          {101, 1000, 100}),
        "Shared swept standing capsule blocks registered hull and permits a "
        "clear path");
}

// Actual supported terrain/anchor checks are appended below the contact
// provider's fixed seed fixture. No planet descriptor or ID is fabricated.
auto actual_walking(const PhysicalMoonDescriptor& moon,
                    const MoonLandedCraftAnchor& anchor) -> void {
  auto terrain =
      need(MoonSurfaceWalkTerrain::create(moon, anchor, anchor.fixed.tick));
  const auto original_anchor = terrain.anchor();
  const auto sampler = need(MoonTerrainSampler::create(moon));
  const auto start = need(terrain.entry());
  const auto initial_access = need(terrain.return_access(start));
  check(start.frame.kind == RigidFrameKind::body_fixed &&
            start.frame.body == moon.body && !start.frame.planet &&
            need(terrain.near_entry(start)) &&
            initial_access.foot_position_metres == start.foot_position_metres &&
            initial_access.distance_metres == 0,
        "Actual moon entry and return retain genuine typed fixed body and "
        "supported feet");
  const auto foot = start.foot_position_metres;
  const auto hit =
      need(query_moon_contact_surface(sampler, {foot.x, foot.y, foot.z}));
  check(norm(difference(
            foot, {hit.position.x, hit.position.y, hit.position.z})) < 1e-8,
        "Walker feet consume the same actual top facet used by contact");
  auto actor = start;
  bool crossed_reach{};
  for (int i = 0; i < 1200; ++i) {
    const auto previous = need(terrain.return_access(actor));
    const auto next = need(terrain.advance(actor, {-1, 0, 0}));
    check(next.status == PlanetSurfaceWalkStatus::supported,
          "Actual supported moon walk moves away from the aft hatch");
    actor = next.actor;
    const auto access = need(terrain.return_access(actor));
    if (!crossed_reach && !access.within_reach) {
      check(previous.within_reach && previous.distance_metres <= 3.5 &&
                access.distance_metres > 3.5 &&
                access.distance_metres - previous.distance_metres <=
                    2.45 * kSimulationStep.count() + 1e-8,
            "Actual moon footsteps bracket the unchanged 3.5m return boundary");
      crossed_reach = true;
    }
  }
  check(crossed_reach &&
            norm(difference(actor.foot_position_metres,
                            start.foot_position_metres)) > 19 &&
            !need(terrain.near_entry(actor)),
        "Moon walk travels beyond interaction reach without an invented area "
        "wall");
  for (int i = 0; i < 1200; ++i)
    actor = need(terrain.advance(actor, {1, 0, 0})).actor;
  check(norm(difference(actor.foot_position_metres,
                        start.foot_position_metres)) < .001 &&
            need(terrain.near_entry(actor)),
        "Actual moon walk returns to the unchanged supported hatch access");
  bool obstructed{};
  for (int i = 0; i < 120; ++i) {
    const auto next = need(terrain.advance(actor, {1, 0, 0}));
    obstructed |= next.status == PlanetSurfaceWalkStatus::craft_obstruction;
    actor = next.actor;
  }
  check(obstructed && terrain.validate(actor).has_value(),
        "Actual moon walker stops at the registered hull capsule");
  bool basin{};
  // Fixed small direction corpus on this exact owner, no seed/location search.
  for (const auto direction :
       {MoonFixedDirection{1, 0, 0}, MoonFixedDirection{-1, 0, 0},
        MoonFixedDirection{0, 1, 0}, MoonFixedDirection{0, -1, 0},
        MoonFixedDirection{0, 0, 1}, MoonFixedDirection{0, 0, -1},
        MoonFixedDirection{1, 1, 1}, MoonFixedDirection{-1, -1, -1}}) {
    const auto point = need(query_moon_contact_surface(sampler, direction));
    if (point.radial_distance_metres >= moon.radius_metres) continue;
    auto in_basin = start;
    in_basin.foot_position_metres = {point.position.x, point.position.y,
                                     point.position.z};
    check(terrain.validate(in_basin).has_value() &&
              need(terrain.advance(in_basin, {})).status ==
                  PlanetSurfaceWalkStatus::supported,
          "Actual dry negative-relief top facets remain walkable, not "
          "planetary water");
    basin = true;
  }
  check(basin,
        "Fixed actual moon corpus includes a dry below-reference-radius basin");
  for (const auto value : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(), 2.0}) {
    check(!terrain.advance(start, {value, 0, 0}) &&
              !terrain.advance(start, {0, value, 0}) &&
              !terrain.advance(start, {0, 0, value == 2.0 ? 4.0 : value}),
          "Invalid moon walking axes refuse before movement");
  }
  auto wrong = start;
  wrong.frame.body->kind = BodyKind::planet;
  check(!terrain.validate(wrong) && !terrain.advance(wrong, {}) &&
            !terrain.return_access(wrong),
        "Moon walker refuses a numeric planet alias");
  wrong = start;
  ++wrong.frame.body->value;
  check(!terrain.validate(wrong), "Moon walker refuses a foreign body");
  wrong = start;
  wrong.foot_position_metres.x = std::numeric_limits<double>::quiet_NaN();
  check(!terrain.validate(wrong) && !terrain.advance(wrong, {}),
        "Moon walker refuses nonfinite feet");
  check(!terrain.advance(start, {}, SimulationSeconds{0}) &&
            !terrain.advance(start, {}, SimulationSeconds{1}),
        "Moon walking accepts only the existing fixed simulation tick");
  auto forged = moon;
  ++forged.radius_metres;
  check(!MoonSurfaceWalkTerrain::create(forged, anchor, anchor.fixed.tick),
        "Moon walking cannot adopt a forged physical owner");
  check(terrain.anchor() == original_anchor,
        "Walks and refusals never move or replace the retained craft anchor");
}

} // namespace

auto main() -> int {
  try {
    const auto fixture = apsis_drift::test_fixture::make_moon_contact_fixture();
    movement_boundaries(fixture.moon);
    const auto checksum = need(rigid_body_state_checksum(
        RigidBodyWorldContext{fixture.moon}, fixture.source));
    const auto anchor =
        prepare_moon_landed_craft(fixture.moon, fixture.source, true, checksum);
    if (!anchor) {
      std::cerr << "Actual fixed moon landing refused: "
                << static_cast<unsigned>(anchor.error()) << '\n';
      check(false, "Fixed genuine moon fixture must certify before walking");
    } else {
      actual_walking(fixture.moon, *anchor);
    }
  } catch (const std::exception& error) {
    check(false, error.what());
  }
  std::cout << "Moon supported surface walking: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
