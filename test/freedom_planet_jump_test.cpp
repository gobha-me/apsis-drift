#include "apsis_drift/freedom_planet_jump.hpp"

#include "apsis_drift/intersystem_jump.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"

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
int checks{}, failures{};
auto check(bool value, std::string_view message) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) {
    if constexpr (std::is_enum_v<E>)
      std::cerr << "Refusal enum " << static_cast<unsigned>(value.error())
                << '\n';
    else if constexpr (requires { value.error().detail; })
      std::cerr << value.error().detail << '\n';
    throw std::runtime_error("Planet jump fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto length(RigidVector3 v) -> double {
  return std::hypot(v.x, v.y, v.z);
}
auto near(double value, long double expected, long double scale = 1) -> bool {
  const auto bound = 64 * std::numeric_limits<double>::epsilon() *
                     std::max({1.0L, std::abs(expected), scale});
  return std::isfinite(value) &&
         std::abs(static_cast<long double>(value) - expected) <= bound;
}
auto fixture(bool network = false, std::size_t ordinal = 0)
    -> FreedomPlanetJumpRequest {
  const Seed seed{42};
  const auto topology = need(generate_freedom_system_network(seed));
  const auto owner = network ? need(resolve_freedom_network_system(
                                   topology, topology.nodes[ordinal].system))
                             : need(generate_physical_origin_system(seed, 2));
  const auto& planet = owner.catalog.planets.front().descriptor;
  FreedomFlightSaveDocument source;
  source.origin = network ? need(make_freedom_network_new_game_document(seed))
                          : make_freedom_new_game_document(seed);
  source.origin.state.tick = 1200;
  source.model.physical_catalog = 2;
  source.model.physical_ephemeris = 2;
  source.model.terrain_policy = 3;
  source.model.frame_policy = 2;
  source.model.optics = 1;
  source.model.companions = 1;
  source.world = FreedomActiveWorldSelection{network ? 2U : 1U,
                                             owner.catalog.id, planet.id};
  source.flight.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  source.flight.frame = {RigidFrameKind::planet_relative_inertial,
                         owner.catalog.id,
                         planet.id,
                         {}};
  source.flight.tick = source.origin.state.tick;
  source.flight.position_metres = {
      0, 0, static_cast<double>(planet.radius.value) * 10000};
  source.flight.linear_velocity_metres_per_second = {3, -4, 2};
  source.flight.orientation =
      need(normalize_rigid_orientation({.8, .1, .2, .3}));
  source.flight.angular_velocity_radians_per_second = {.01, -.02, .03};
  source.flight = need(canonicalize_rigid_body_state(
      RigidBodyWorldContext{owner}, source.flight));
  return {1,
          source,
          {owner.catalog.id, body_id(owner.catalog.planets[1].descriptor.id)},
          1};
}
auto invalid_contract() -> void {
  const auto valid = fixture();
  for (unsigned fault = 0; fault < 21; ++fault) {
    auto r = valid;
    switch (fault) {
      case 0: r.version = 0; break;
      case 1: r.version = 2; break;
      case 2: r.attempt = 0; break;
      case 3: r.attempt = UINT64_MAX; break;
      case 4: r.destination.system = {}; break;
      case 5:
        r.destination.body = body_id(*r.source.flight.frame.planet);
        break;
      case 6: r.destination.body.version = 2; break;
      case 7: r.destination.body.kind = BodyKind::moon; break;
      case 8: r.destination.body.kind = BodyKind::minor; break;
      case 9: r.destination.body.value = 0; break;
      case 10:
        r.source.flight.position_metres.x =
            std::numeric_limits<double>::quiet_NaN();
        break;
      case 11:
        r.source.flight.linear_velocity_metres_per_second.y =
            std::numeric_limits<double>::infinity();
        break;
      case 12:
        r.source.flight.orientation.w =
            std::numeric_limits<double>::quiet_NaN();
        break;
      case 13:
        r.source.flight.angular_velocity_radians_per_second.z =
            std::numeric_limits<double>::infinity();
        break;
      case 14: r.source.model.physical_catalog = 1; break;
      case 15: r.source.model.atmosphere.version = 2; break;
      case 16: r.source.world->system = {}; break;
      case 17: ++r.source.origin.state.tick; break;
      case 18:
        r.source.flight.tick = UINT64_MAX - kJumpTransitTicks;
        r.source.origin.state.tick = r.source.flight.tick;
        break;
      case 19: r.source.flight.craft.version = 999; break;
      case 20:
        r.source.flight.position_metres.x = kRigidBodyMaximumPositionMetres * 2;
        break;
      default: break;
    }
    check(!freeze_freedom_planet_jump_arrival(r),
          "Invalid policy/owner/body/command-time/nonfinite geometry refuses "
          "before arrival");
  }
  auto blocked = valid;
  const auto owner =
      need(hydrate_freedom_flight_document(blocked.source)).system;
  const auto& planet = owner.catalog.planets.front().descriptor;
  const auto profile = need(resolve_atmospheric_flight_profile(planet));
  auto at_star = valid;
  at_star.source.flight.frame = {
      RigidFrameKind::system_inertial, owner.catalog.id, {}, {}};
  at_star.source.flight.position_metres = {};
  check(!freeze_freedom_planet_jump_arrival(at_star),
        "A valid selected-planet frame cannot hide actual system-star "
        "intersection");
  for (double height :
       {100.0, 17000.0, profile.space_boundary_altitude_metres * .5}) {
    blocked.source.flight.position_metres = {
        0, 0, static_cast<double>(planet.radius.value) * 1000 + height};
    const auto refused = freeze_freedom_planet_jump_arrival(blocked);
    check(!refused &&
              refused.error() == FreedomPlanetJumpError::unclear_departure,
          "Sphere-clear but terrain/atmosphere-envelope departure cannot be "
          "mistaken for clear space");
  }
  // Actual secondary ephemerides, not fabricated Moon/planet carriers.
  const auto catalog = need(generate_secondary_body_catalog(owner));
  const auto hierarchy = need(make_body_hierarchy(catalog));
  const auto bodies =
      need(resolve_body_ephemerides(hierarchy, {valid.source.flight.tick, 0}));
  for (const auto& secondary : catalog.bodies) {
    const auto pose =
        std::ranges::find(bodies, secondary.node.id, &BodyEphemeris::body);
    if (pose == bodies.end())
      throw std::runtime_error("Secondary source unavailable");
    auto system_source = valid;
    auto& body = system_source.source.flight;
    body.frame = {RigidFrameKind::system_inertial, owner.catalog.id, {}, {}};
    const double relief = secondary.node.id.kind == BodyKind::moon
                              ? kMoonTerrainMaximumReliefMetres
                              : 1000;
    body.position_metres = {pose->position.x + secondary.radius_metres +
                                relief * .5,
                            pose->position.y, pose->position.z};
    body.linear_velocity_metres_per_second = {
        pose->velocity.x, pose->velocity.y, pose->velocity.z};
    body =
        need(canonicalize_rigid_body_state(RigidBodyWorldContext{owner}, body));
    const auto refused = freeze_freedom_planet_jump_arrival(system_source);
    check(!refused &&
              refused.error() == FreedomPlanetJumpError::unclear_departure,
          "Actual secondary relief/stand-off envelopes cannot be omitted from "
          "endpoint clearance");
  }
}
auto valid_contract(bool selected_network, std::size_t ordinal) -> void {
  const auto request = fixture(selected_network, ordinal);
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Immutability
  const auto snapshot = request;
  const auto source_owner =
      need(hydrate_freedom_flight_document(request.source));
  const auto& system = source_owner.system;
  const auto relative =
      need(relative_freedom_flight_state(request.source, system));
  const auto frozen = need(freeze_freedom_planet_jump_arrival(request));
  check(
      request == snapshot && frozen.request == request && frozen.version == 1 &&
          frozen.arrival_tick == request.source.flight.tick + kJumpTransitTicks,
      "Freeze owns the complete exact commit source without advancing or "
      "mutating it");
  check(frozen.source_checksum ==
                need(rigid_body_state_checksum(RigidBodyWorldContext{system},
                                               request.source.flight)) &&
            frozen.departure_volume.clear() && frozen.arrival_volume.clear() &&
            validate_frozen_freedom_planet_jump_arrival(frozen).has_value(),
        "Frozen owner checksum and both complete endpoint volumes qualify");
  const auto destination = PlanetId{request.destination.body.value};
  const auto target =
      std::ranges::find_if(system.catalog.planets, [&](const auto& p) {
        return p.descriptor.id == destination;
      });
  if (target == system.catalog.planets.end())
    throw std::runtime_error("Target catalog unavailable");
  const auto arrival_owner =
      need(hydrate_freedom_flight_document(frozen.arrival));
  const auto rotation = need(resolve_planet_rotation(
      system, arrival_owner.rotation, frozen.arrival_tick));
  const auto ephemeris = need(
      resolve_planet_ephemeris(system, destination, {frozen.arrival_tick, 0}));
  const auto frame =
      need(resolve_craft_frame(request.source.flight.craft)).properties;
  long double hull_squared{};
  for (unsigned corner = 0; corner < 8; ++corner) {
    long double squared{};
    for (unsigned axis = 0; axis < 3; ++axis) {
      const long double component =
          static_cast<long double>((corner & (1U << axis))
                                       ? frame.hull_max_mm[axis]
                                       : frame.hull_min_mm[axis]) /
          1000;
      squared += component * component;
    }
    hull_squared = std::max(hull_squared, squared);
  }
  const auto target_profile = need(resolve_atmospheric_flight_profile(
      target->descriptor, request.source.model.atmosphere));
  const long double surface_radius =
      static_cast<long double>(target->descriptor.radius.value) * 1000;
  const long double envelope = std::max(
      17070.0L,
      static_cast<long double>(target_profile.space_boundary_altitude_metres));
  const long double radius =
      surface_radius + envelope + std::sqrt(hull_squared) + 100000.0L;
  check(kFreedomPlanetJumpArrivalClearanceMetres == 100000.0,
        "Unpublished policy1 exposes the fixed 100km arrival clearance");
  // Independent long-double rotation-matrix column for the fixed +Y ray.
  const auto q = rotation.geometry.fixed_to_system;
  const long double w = q.w, x = q.x, y = q.y, z = q.z;
  const auto n = w * w + x * x + y * y + z * z;
  const std::array<long double, 3> expected{2 * (x * y - w * z) / n * radius,
                                            (w * w - x * x + y * y - z * z) /
                                                n * radius,
                                            2 * (y * z + w * x) / n * radius};
  const auto& p = frozen.arrival.flight.position_metres;
  check(near(p.x, expected[0], radius) && near(p.y, expected[1], radius) &&
            near(p.z, expected[2], radius) && near(length(p), radius),
        "Envelope-clear arrival follows the actual future physical rotation, "
        "not a static or borrowed frame");
  check(near(frozen.arrival_system_position.x,
             static_cast<long double>(ephemeris.position.x) + p.x) &&
            near(frozen.arrival_system_position.y,
                 static_cast<long double>(ephemeris.position.y) + p.y) &&
            near(frozen.arrival_system_position.z,
                 static_cast<long double>(ephemeris.position.z) + p.z),
        "Future target ephemeris enters the system position exactly once");
  const auto system_arrival = need(reframe_rigid_body(
      RigidBodyWorldContext{system}, frozen.arrival.flight,
      {{RigidFrameKind::system_inertial, system.catalog.id, {}, {}},
       frozen.arrival_tick}));
  check(frozen.arrival.flight.linear_velocity_metres_per_second ==
                relative.linear_velocity_metres_per_second &&
            frozen.arrival.flight.orientation ==
                request.source.flight.orientation &&
            frozen.arrival.flight.angular_velocity_radians_per_second ==
                request.source.flight.angular_velocity_radians_per_second &&
            system_arrival.linear_velocity_metres_per_second ==
                RigidVector3{ephemeris.velocity.x +
                                 relative.linear_velocity_metres_per_second.x,
                             ephemeris.velocity.y +
                                 relative.linear_velocity_metres_per_second.y,
                             ephemeris.velocity.z +
                                 relative.linear_velocity_metres_per_second.z},
        "Velocity is target ephemeris plus retained source-relative velocity; "
        "attitude/angular state survive exactly");
  auto expected_origin = request.source.origin;
  expected_origin.state.tick = frozen.arrival_tick;
  auto expected_model = request.source.model;
  expected_model.hold = {};
  check(frozen.arrival.origin == expected_origin &&
            frozen.arrival.model == expected_model &&
            frozen.arrival.world ==
                FreedomActiveWorldSelection{selected_network ? 2U : 1U,
                                            system.catalog.id, destination} &&
            !frozen.arrival.moon && !frozen.arrival.minor &&
            arrival_owner.system == system,
        "Only actual tick/world/pose and source-bound hold change; "
        "origin/history/model selection remains authentic");
  const long double p_squared = static_cast<long double>(p.x) * p.x +
                                static_cast<long double>(p.y) * p.y +
                                static_cast<long double>(p.z) * p.z;
  const long double rounding =
      64 * std::numeric_limits<double>::epsilon() * radius;
  check(std::sqrt(p_squared) - std::sqrt(hull_squared) - surface_radius -
                envelope >=
            100000.0L - rounding,
        "Independent all-corner hull sphere retains 100km beyond both complete "
        "target envelopes");
  // Rotate each original hull corner independently, then measure its genuine
  // distance from the target centre; no producer hull-radius helper is used.
  const auto attitude = frozen.arrival.flight.orientation;
  const long double aw = attitude.w, ax = attitude.x, ay = attitude.y,
                    az = attitude.z;
  const long double an = aw * aw + ax * ax + ay * ay + az * az;
  long double closest = std::numeric_limits<long double>::infinity();
  for (unsigned corner = 0; corner < 8; ++corner) {
    std::array<long double, 3> body{};
    for (unsigned axis = 0; axis < 3; ++axis)
      body[axis] = static_cast<long double>((corner & (1U << axis))
                                                ? frame.hull_max_mm[axis]
                                                : frame.hull_min_mm[axis]) /
                   1000;
    const long double cx =
        p.x + ((aw * aw + ax * ax - ay * ay - az * az) * body[0] +
               2 * (ax * ay - aw * az) * body[1] +
               2 * (ax * az + aw * ay) * body[2]) /
                  an;
    const long double cy =
        p.y + (2 * (ax * ay + aw * az) * body[0] +
               (aw * aw - ax * ax + ay * ay - az * az) * body[1] +
               2 * (ay * az - aw * ax) * body[2]) /
                  an;
    const long double cz =
        p.z +
        (2 * (ax * az - aw * ay) * body[0] + 2 * (ay * az + aw * ax) * body[1] +
         (aw * aw - ax * ax - ay * ay + az * az) * body[2]) /
            an;
    closest = std::min(closest, std::sqrt(cx * cx + cy * cy + cz * cz));
  }
  check(closest - surface_radius - envelope >= 100000.0L - rounding,
        "Actual closest transformed hull corner remains at least 100km above "
        "atmosphere and terrain within rounding");
  for (const auto& other : system.catalog.planets) {
    const auto pose = need(resolve_planet_ephemeris(system, other.descriptor.id,
                                                    {frozen.arrival_tick, 0}));
    const auto air = need(resolve_atmospheric_flight_profile(
        other.descriptor, request.source.model.atmosphere));
    const auto dx = static_cast<long double>(frozen.arrival_system_position.x) -
                    pose.position.x;
    const auto dy = static_cast<long double>(frozen.arrival_system_position.y) -
                    pose.position.y;
    const auto dz = static_cast<long double>(frozen.arrival_system_position.z) -
                    pose.position.z;
    const auto bound =
        static_cast<long double>(other.descriptor.radius.value) * 1000 +
        std::max(17070.0L,
                 static_cast<long double>(air.space_boundary_altitude_metres)) +
        std::sqrt(hull_squared);
    check(dx * dx + dy * dy + dz * dz > bound * bound,
          "Independent squared-distance oracle corroborates every "
          "ordinary-planet endpoint envelope");
  }
  check(need(decode_freedom_flight_document_json(
            need(encode_freedom_flight_document_json(frozen.arrival)))) ==
            frozen.arrival,
        "Arrival uses the existing owned planetary flight codec without a new "
        "physical frame");
  auto future = request;
  future.source.flight.tick += 12000;
  future.source.origin.state.tick = future.source.flight.tick;
  const auto later = need(freeze_freedom_planet_jump_arrival(future));
  check(later.arrival.flight.position_metres !=
                frozen.arrival.flight.position_metres &&
            later.arrival_system_position != frozen.arrival_system_position,
        "Later genuine commits recompute rotating/moving targets rather than "
        "freezing catalog epoch zero");
  auto held = request;
  held.source.model.hold.target = OrbitHoldTarget{
      *relative.frame.planet, length(relative.position_metres), {0, 1, 0}};
  check(
      !need(freeze_freedom_planet_jump_arrival(held)).arrival.model.hold.target,
      "A valid source orbit hold never targets the previous planet after "
      "handoff");
  for (unsigned fault = 0; fault < 11; ++fault) {
    auto changed = frozen;
    switch (fault) {
      case 0: changed.version = 2; break;
      case 1: ++changed.source_checksum; break;
      case 2: ++changed.arrival_tick; break;
      case 3: ++changed.request.attempt; break;
      case 4:
        changed.arrival.flight.linear_velocity_metres_per_second.x += 1;
        break;
      case 5: changed.arrival_system_position.z += 1; break;
      case 6: changed.source_system_position.x += 1; break;
      case 7: changed.departure_volume.intersects_minor = true; break;
      case 8: changed.arrival_volume.intersects_moon = true; break;
      case 9: changed.arrival.world->planet = *relative.frame.planet; break;
      case 10:
        changed.arrival.model.assistance = !changed.arrival.model.assistance;
        break;
      default: break;
    }
    // Attempt is retained in the frozen request but deliberately does not
    // randomize this deterministic recipe. Outer history authenticates it.
    if (fault == 3)
      check(validate_frozen_freedom_planet_jump_arrival(changed).has_value(),
            "Changing a valid attempt cannot reroll a deterministic planet "
            "arrival");
    else
      check(!validate_frozen_freedom_planet_jump_arrival(changed),
            "Frozen receipt corruption refuses exact rederivation");
  }
  // Canonical system-frame source must be retained verbatim. Translation to
  // the selected force-body view happens only inside the calculation.
  auto system_source = request;
  system_source.source.flight = need(reframe_rigid_body(
      RigidBodyWorldContext{system}, request.source.flight,
      {{RigidFrameKind::system_inertial, system.catalog.id, {}, {}},
       request.source.flight.tick}));
  const auto system_relative =
      need(relative_freedom_flight_state(system_source.source, system));
  const auto system_frozen =
      need(freeze_freedom_planet_jump_arrival(system_source));
  check(system_frozen.request.source == system_source.source &&
            system_frozen.arrival.flight.orientation ==
                request.source.flight.orientation &&
            system_frozen.arrival.flight.linear_velocity_metres_per_second ==
                system_relative.linear_velocity_metres_per_second,
        "Actual system-frame source remains canonical while its selected-body "
        "velocity is derived once");
  const FreedomPlanetJumpRequest back{
      1,
      frozen.arrival,
      {system.catalog.id, body_id(*relative.frame.planet)},
      request.attempt + 1};
  const auto returned = need(freeze_freedom_planet_jump_arrival(back));
  check(returned.arrival.world->planet == *relative.frame.planet &&
            returned.arrival.origin.network == request.source.origin.network &&
            returned.arrival.flight.tick ==
                request.source.flight.tick + 2 * kJumpTransitTicks,
        "Same genuine catalog supports a retained-owner return without a fake "
        "Origin port or resource refill");
  std::cout << "planet provider network=" << selected_network
            << " ordinal=" << ordinal << " arrival_tick=" << frozen.arrival_tick
            << " radius=" << length(p) << '\n';
}
} // namespace

auto main() -> int {
  try {
    invalid_contract();
    valid_contract(false, 0);
    for (std::size_t ordinal = 0; ordinal < 4; ++ordinal)
      valid_contract(true, ordinal);
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << e.what() << '\n';
  }
  std::cout << checks << " planet jump checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
