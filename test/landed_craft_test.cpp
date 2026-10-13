#include "apsis_drift/godot/saved_flight.hpp"
#include "apsis_drift/landed_craft.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/planet_surface_walker.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "apsis_drift/terrain_hull_clearance.hpp"
#include "apsis_drift/terrain_touchdown.hpp"
#include "contact_geometry_internal.hpp"
#include "flight_endurance_meter.hpp"
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace {
using namespace apsis_drift;
int failures{}, checks{};
auto check(bool value, std::string_view name) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << name << '\n';
  }
}
template <class T, class E> auto required(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("required landed fixture");
  return std::move(*value);
}
template <class E> auto required(std::expected<void, E> value) -> void {
  if (!value) {
    if constexpr (std::is_same_v<E, std::string>)
      std::cerr << value.error() << '\n';
    throw std::runtime_error("required landed action");
  }
}
using V = RigidVector3;
auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto mul(V a, double s) -> V {
  return {a.x * s, a.y * s, a.z * s};
}
auto norm(V a) -> double {
  return std::hypot(a.x, a.y, a.z);
}
auto rotate(RigidOrientation q, V a) -> V {
  // Independent rotation matrix, unlike the production cross-product formula.
  const auto s = 2 / (((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z);
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
  RigidBodyState fixed;
  std::uint32_t terrain_policy;
  explicit Fixture(std::uint32_t policy = kFreedomLegacyTerrainPolicy)
      : terrain_policy(policy) {
    auto cache = required(TerrainTileCache::create(1));
    const auto triangle =
        required(godot_spike::detail::build_selected_contact_triangle(
            owner, planet, *godot_spike::saved_contact_recipe(terrain_policy),
            {planet.id, CubeFace::positive_x, 4000, 4000, 12, 13, 0}, cache));
    const auto a = triangle.vertices[0], b = triangle.vertices[1],
               c = triangle.vertices[2];
    const V center{a.x + .3 * ((b.x - a.x) + (c.x - a.x)),
                   a.y + .3 * ((b.y - a.y) + (c.y - a.y)),
                   a.z + .3 * ((b.z - a.z) + (c.z - a.z))};
    const auto n = triangle.outward_normal;
    const auto w = 1 + n.y, magnitude = std::hypot(w, n.z, n.x);
    fixed.craft = {kWayfarerFrameId, 1};
    fixed.tick = 1200;
    fixed.frame = {
        RigidFrameKind::planet_fixed, owner.catalog.id, planet.id, {}};
    fixed.orientation = {w / magnitude, n.z / magnitude, 0, -n.x / magnitude};
    fixed.position_metres = sub(add(center, mul({n.x, n.y, n.z}, -.05)),
                                rotate(fixed.orientation, {0, -2.08, -.7}));
  }
  auto source(RigidBodyState state) const -> RigidBodyState {
    return required(
        reframe_rigid_body(context, state,
                           {{RigidFrameKind::planet_relative_inertial,
                             owner.catalog.id,
                             planet.id,
                             {}},
                            state.tick},
                           rotation));
  }
};
auto ray_hit(V start, V direction, const godot_spike::ContactTriangle& triangle)
    -> std::optional<long double> {
  using L = std::array<long double, 3>;
  const auto convert = [](V p) -> L { return {p.x, p.y, p.z}; };
  const auto subtract = [](L a, L b) -> L {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
  };
  const auto product = [](L a, L b) -> long double {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  };
  const auto vector_product = [](L a, L b) -> L {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
  };
  const auto a = convert(
      {triangle.vertices[0].x, triangle.vertices[0].y, triangle.vertices[0].z});
  const auto e1 =
      subtract(convert({triangle.vertices[1].x, triangle.vertices[1].y,
                        triangle.vertices[1].z}),
               a);
  const auto e2 =
      subtract(convert({triangle.vertices[2].x, triangle.vertices[2].y,
                        triangle.vertices[2].z}),
               a);
  const auto d = convert(direction);
  const auto p = vector_product(d, e2);
  const auto det = product(e1, p);
  if (std::abs(det) < 1e-12L) return {};
  const auto s = subtract(convert(start), a);
  const auto u = product(s, p) / det;
  const auto q = vector_product(s, e1);
  const auto w = product(d, q) / det;
  if (u < 0 || w < 0 || u + w > 1) return {};
  return product(e2, q) / det;
}

auto hull_and_idle(const Fixture& f) -> void {
  const auto source = f.source(f.fixed);
  const auto before = required(encode_rigid_body_state_json(f.context, source));
  const auto hull = required(assess_terrain_hull_clearance(
      f.owner, f.rotation, source, 1, f.terrain_policy));
  check(hull.certified && hull.lower_clearance_metres > .7 &&
            hull.triangle_count <= 128,
        "actual whole hull clears generated terrain within bounded work");
  check(hull == required(assess_terrain_hull_clearance(
                    f.owner, f.rotation, source, 64, f.terrain_policy)),
        "hull certificate is cache-independent");
  const auto checksum = required(rigid_body_state_checksum(f.context, source));
  const auto anchor = required(prepare_landed_craft(
      f.owner, f.rotation, source, true, checksum, f.terrain_policy));
  check(anchor.terrain_policy == f.terrain_policy &&
            hull.terrain_policy == f.terrain_policy,
        "support and hull certificate retain the selected physical terrain");
  check(anchor.fixed.linear_velocity_metres_per_second == V{} &&
            anchor.fixed.angular_velocity_radians_per_second == V{},
        "safe landing stores zero ground-relative velocities");
  for (SimulationTick delta : {0ULL, 1ULL, 1200ULL, 10000000ULL}) {
    const auto resolved = required(
        resolve_landed_craft(f.owner, f.rotation, anchor, source.tick + delta));
    const auto fixed = required(reframe_rigid_body(
        f.context, resolved, {anchor.fixed.frame, resolved.tick}, f.rotation));
    check(norm(sub(fixed.position_metres, anchor.fixed.position_metres)) <
                  1e-7 &&
              norm(fixed.linear_velocity_metres_per_second) < 1e-9 &&
              norm(fixed.angular_velocity_radians_per_second) < 1e-15,
          "arbitrary authoritative idle time preserves surface anchor without "
          "accumulated motion");
    check(static_cast<bool>(validate_landed_craft(f.owner, f.rotation, anchor,
                                                  resolved.tick)),
          "supported idle remains valid across planet rotation");
    const auto released = required(
        release_landed_craft(f.owner, f.rotation, anchor, resolved.tick));
    check(released == resolved,
          "liftoff preserves pose and applies rotation velocity exactly once");
  }
  auto false_checksum = prepare_landed_craft(f.owner, f.rotation, source, true,
                                             checksum ^ 1, f.terrain_policy);
  check(!false_checksum &&
            false_checksum.error() == LandedCraftError::stale_source,
        "changed assessment checksum refuses before terrain commit");
  auto stowed = prepare_landed_craft(f.owner, f.rotation, source, false,
                                     checksum, f.terrain_policy);
  check(!stowed && stowed.error() == LandedCraftError::gear_not_deployed,
        "actual declaration is required; prospective pads do not deploy gear");
  check(!prepare_landed_craft(f.owner, f.rotation, source, true, checksum, 99),
        "unknown physical terrain selection refuses before support sampling");
  check(!assess_terrain_hull_clearance(f.owner, f.rotation, source, 64, 99),
        "unknown physical terrain selection cannot certify hull clearance");
  auto bad = anchor;
  bad.fixed.linear_velocity_metres_per_second.x = 1;
  check(!resolve_landed_craft(f.owner, f.rotation, bad, source.tick),
        "hidden landed velocity refuses");
  bad = anchor;
  bad.terrain_policy = 0;
  check(!resolve_landed_craft(f.owner, f.rotation, bad, source.tick),
        "unknown terrain version refuses");
  bad = anchor;
  bad.fixed.position_metres.x = std::numeric_limits<double>::quiet_NaN();
  check(!resolve_landed_craft(f.owner, f.rotation, bad, source.tick),
        "nonfinite anchor refuses");
  check(!resolve_landed_craft(f.owner, f.rotation, anchor, source.tick - 1),
        "pre-landing time refuses");
  check(!resolve_landed_craft(f.owner, f.rotation, anchor,
                              std::numeric_limits<SimulationTick>::max()),
        "landed clock overflow refuses");
  check(
      before == required(encode_rigid_body_state_json(f.context, source)),
      "assessment and anchor preparation leave actual source bytes untouched");
  // Independent rays sample the complete box bottom, including edges and
  // interiors, and find the nearest actual facet in the certified region.
  auto terrain_cache = required(TerrainTileCache::create(1));
  const auto downward = rotate(f.fixed.orientation, {0, -1, 0});
  const auto properties =
      required(resolve_craft_frame(source.craft)).properties;
  for (unsigned x = 0; x <= 8; ++x)
    for (unsigned z = 0; z <= 8; ++z) {
      const auto blend = [](std::int32_t low, std::int32_t high,
                            unsigned step) {
        return (low + (static_cast<double>(high) - low) * step / 8) / 1000;
      };
      const auto point = add(
          f.fixed.position_metres,
          rotate(f.fixed.orientation, {blend(properties.hull_min_mm[0],
                                             properties.hull_max_mm[0], x),
                                       properties.hull_min_mm[1] / 1000.0,
                                       blend(properties.hull_min_mm[2],
                                             properties.hull_max_mm[2], z)}));
      std::optional<long double> nearest;
      for (auto cx = hull.minimum_cell[0]; cx <= hull.maximum_cell[0]; ++cx)
        for (auto cz = hull.minimum_cell[1]; cz <= hull.maximum_cell[1]; ++cz)
          for (unsigned half = 0; half < 2; ++half) {
            const auto triangle =
                required(godot_spike::detail::build_selected_contact_triangle(
                    f.owner, f.planet,
                    *godot_spike::saved_contact_recipe(f.terrain_policy),
                    {f.planet.id, hull.face, cx / 32, cz / 32, cx % 32, cz % 32,
                     half},
                    terrain_cache));
            const auto hit = ray_hit(point, downward, triangle);
            if (hit && (!nearest || *hit < *nearest)) nearest = hit;
          }
      check(nearest && *nearest >= hull.lower_clearance_metres && *nearest < 1,
            "whole-box clearance agrees with independent long-double interior "
            "terrain rays");
    }
  auto seam = f.fixed;
  const auto r = static_cast<double>(f.planet.radius.value) * 1000 + 10;
  seam.position_metres = {r / std::sqrt(2.0), r / std::sqrt(2.0), 0};
  const auto uncertain = assess_terrain_hull_clearance(
      f.owner, f.rotation, f.source(seam), kDefaultTerrainTileCacheCapacity,
      f.terrain_policy);
  check(!uncertain && uncertain.error() == TerrainHullError::face_boundary,
        "a box crossing cube-face seams refuses before terrain access");
  auto buried = f.fixed;
  buried.position_metres =
      sub(buried.position_metres, rotate(buried.orientation, {0, 1, 0}));
  const auto blocked = required(assess_terrain_hull_clearance(
      f.owner, f.rotation, f.source(buried), kDefaultTerrainTileCacheCapacity,
      f.terrain_policy));
  check(!blocked.certified && blocked.lower_clearance_metres < 0,
        "terrain reaching the registered hull cannot receive clearance");
  auto invalid = source;
  invalid.position_metres.x = std::numeric_limits<double>::infinity();
  check(!assess_terrain_hull_clearance(f.owner, f.rotation, invalid),
        "nonfinite hull source refuses");
  check(!assess_terrain_hull_clearance(f.owner, f.rotation, source, 0),
        "invalid hull cache refuses");
  std::cout << std::setprecision(17) << "hull triangles " << hull.triangle_count
            << " lower clearance " << hull.lower_clearance_metres << '\n';
}
auto ascent_and_orbit(NativeFreedomFlightSession session, const Fixture& f,
                      research::FlightEnduranceMeter* meter = nullptr) -> void {
  required(session.set_landing_gear(false));
  const auto initial_resources = session.resources();
  const auto initial_effort = meter ? meter->effort_quanta : 0;
  const auto initial_history = session.document().origin.state;
  const auto initial_tick = session.document().flight.tick;
  const auto initial_body = session.document().flight;
  const auto inverse = [&](V a) {
    const auto q = initial_body.orientation;
    return rotate({q.w, -q.x, -q.y, -q.z}, a);
  };
  const auto nominal_forward = rotate(initial_body.orientation, {0, 0, -1});
  const auto gravity =
      required(evaluate_central_body_gravity(f.context, initial_body));
  const auto edge =
      required(session.observe()).atmosphere.space_boundary_altitude_metres;
  const auto target_radius =
      gravity.reference_radius_metres + std::max(200000.0, edge + 100000.0);
  const auto properties =
      required(resolve_craft_frame(initial_body.craft)).properties;
  bool space = false, orbit = false;
  unsigned ticks = 0;
  for (; ticks < 120 * 2400; ++ticks) {
    const auto& body = session.document().flight;
    const auto radius = norm(body.position_metres);
    const auto radial = mul(body.position_metres, 1 / radius);
    const auto velocity = body.linear_velocity_metres_per_second;
    const auto product = [](V a, V b) {
      return (a.x * b.x + a.y * b.y) + a.z * b.z;
    };
    const auto radial_velocity = product(radial, velocity);
    const auto tangent = sub(velocity, mul(radial, radial_velocity));
    auto forward =
        sub(nominal_forward, mul(radial, product(nominal_forward, radial)));
    forward = mul(forward, 1 / norm(forward));
    const auto goal_speed =
        space
            ? std::sqrt(
                  gravity
                      .gravitational_parameter_metres_cubed_per_second_squared /
                  radius)
            : 0;
    const auto goal_radial =
        std::clamp(.02 * (target_radius - radius), -300.0, 300.0);
    const auto actual_gravity =
        required(evaluate_central_body_gravity(f.context, body));
    const auto acceleration =
        sub(add(mul(radial, .12 * (goal_radial - radial_velocity) -
                                product(tangent, tangent) / radius),
                mul(sub(mul(forward, goal_speed), tangent), .04)),
            actual_gravity.acceleration_metres_per_second_squared);
    const auto demand = inverse(acceleration);
    const auto fraction =
        [mass = properties.dry_mass_kg](double a, std::uint32_t rating) {
          return std::clamp(a * mass / rating, 0.0, 1.0);
        };
    NativeFlightControls controls;
    controls.positive_translation = {
        fraction(demand.x, properties.positive_force_newtons[0]),
        fraction(demand.y, properties.positive_force_newtons[1]),
        fraction(demand.z, properties.positive_force_newtons[2])};
    controls.negative_translation = {
        fraction(-demand.x, properties.negative_force_newtons[0]),
        fraction(-demand.y, properties.negative_force_newtons[1]),
        fraction(-demand.z, properties.negative_force_newtons[2])};
    const auto step = required(session.advance(controls));
    if (meter) meter->sample(step.actuation.central.propulsion);
    if (ticks % 120 == 0) {
      const auto observed = required(session.observe());
      if (!space && observed.atmosphere.altitude_metres >= edge) {
        space = true;
        std::cout << "space tick " << session.document().flight.tick
                  << " checksum "
                  << required(rigid_body_state_checksum(
                         f.context, session.document().flight))
                  << '\n';
      }
      if (space &&
          observed.orbit.classification == OrbitClassification::stable &&
          observed.orbit.periapsis_radius_metres >
              gravity.reference_radius_metres + edge &&
          std::abs(norm(session.document().flight.position_metres) -
                   target_radius) < 2000) {
        orbit = true;
        break;
      }
    }
  }
  const auto final = required(session.observe());
  std::cout << "ascent ticks " << ticks << " altitude "
            << final.atmosphere.altitude_metres << " peri "
            << final.orbit.periapsis_radius_metres -
                   gravity.reference_radius_metres
            << " checksum "
            << required(rigid_body_state_checksum(f.context,
                                                  session.document().flight))
            << '\n';
  check(space && orbit, "real rated thrust takes the same landed vehicle "
                        "through atmosphere into a stable orbit");
  check(session.document().flight.tick > initial_tick &&
            session.document().flight.craft == initial_body.craft &&
            session.document().origin.state.discoveries ==
                initial_history.discoveries &&
            session.document().origin.state.world_deltas ==
                initial_history.world_deltas,
        "complete ascent preserves vehicle identity and authoritative history");
  if (initial_resources && meter) {
    check(
        session.resources()->flight_quanta ==
                initial_resources->flight_quanta -
                    (meter->effort_quanta - initial_effort) &&
            session.resources()->jump_charges == 3,
        "Reserve-funded ascent debits exact observed effort without jump fuel");
    const auto resource_text = required(encode_freedom_resource_document_json(
        {session.surface_document(), *session.resources()}));
    const auto resource_restored = required(NativeFreedomFlightSession::open(
        {NativeStartup::Mode::freedom,
         required(decode_freedom_resource_document_json(resource_text)),
         f.planet,
         {}}));
    check(resource_restored.resources() == session.resources() &&
              resource_restored.document() == session.document() &&
              resource_restored.surface() == session.surface(),
          "Fuel-funded landing/ascent checkpoint preserves exact quantity and "
          "orbit");
    std::cout << "reserve-funded final quantity "
              << session.resources()->flight_quanta << '\n';
  }
  const auto encoded = required(
      encode_freedom_surface_document_json(session.surface_document()));
  const auto continued = required(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom,
       required(decode_freedom_surface_document_json(encoded)),
       f.planet,
       {}}));
  check(continued.document() == session.document() &&
            continued.surface() == session.surface(),
        "the same save layer survives return to orbital flight exactly");
  const auto system_pose = required(reframe_rigid_body(
      f.context, session.document().flight,
      {{RigidFrameKind::system_inertial, f.owner.catalog.id, {}, {}},
       session.document().flight.tick}));
  check(static_cast<bool>(validate_rigid_body_state(f.context, system_pose)),
        "orbital state continues through the existing physical system frame");
}
auto assisted_commands(const Fixture& f,
                       research::FlightEnduranceMeter* meter = nullptr,
                       bool resources = false) -> void {
  auto fixed = f.fixed;
  fixed.position_metres =
      add(fixed.position_metres, rotate(fixed.orientation, {0, .7, 0}));
  FreedomFlightSaveDocument flight{
      make_freedom_new_game_document(Seed{42}), f.source(fixed), {}};
  flight.origin.state.tick = flight.flight.tick;
  flight.model.physical_catalog = 2;
  flight.model.physical_ephemeris = 2;
  flight.model.terrain_policy = f.terrain_policy;
  const auto open = [&]() {
    NativeSaveDocument selected = flight;
    if (resources)
      selected = FreedomResourceSaveDocument{
          {flight, {}},
          {1, flight.origin.state.craft, flight.flight.craft,
           flight.flight.tick, kFreedomFlightReserveQuanta, 3}};
    return required(NativeFreedomFlightSession::open(
        {NativeStartup::Mode::freedom, std::move(selected), f.planet, {}}));
  };
  auto session = open();
  required(session.set_assistance(true));
  check((!session.surface() ||
         (!session.surface()->gear_deployed && !session.surface()->landed)) &&
            session.surface_maneuver().kind == NativeSurfaceManeuverKind::off,
        "enabling assist alone does not deploy gear or start landing");
  required(session.request_landing());
  check(
      session.surface()->gear_deployed &&
          session.surface_maneuver().kind == NativeSurfaceManeuverKind::landing,
      "single assisted action explicitly deploys and starts bounded maneuver");
  NativeFlightControls invalid;
  invalid.positive_translation.x = std::numeric_limits<double>::quiet_NaN();
  const auto maneuver = session.surface_maneuver();
  const auto before = required(
      encode_freedom_surface_document_json(session.surface_document()));
  check(!session.advance(invalid) && session.surface_maneuver() == maneuver &&
            before == required(encode_freedom_surface_document_json(
                          session.surface_document())),
        "invalid input cannot cancel or advance assisted landing");
  auto intervention = session;
  NativeFlightControls manual;
  manual.positive_translation.y = .5;
  required(intervention.advance(manual));
  check(intervention.surface_maneuver().kind ==
                NativeSurfaceManeuverKind::off &&
            intervention.surface()->gear_deployed,
        "pilot intervention cancels maneuver without surprise gear retraction");
  auto resumed = required(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom,
       required(decode_freedom_surface_document_json(before)),
       f.planet,
       {}}));
  check(
      resumed.surface()->gear_deployed &&
          resumed.surface_maneuver().kind == NativeSurfaceManeuverKind::off,
      "saved gear survives while pending assisted maneuver is not serialized");
  unsigned ticks = 0;
  while (!session.surface()->landed && ticks < 1200) {
    const auto step = required(session.advance({}));
    if (meter) meter->sample(step.actuation.central.propulsion);
    ++ticks;
  }
  std::cout << "assisted landing ticks " << ticks << " note "
            << session.surface_maneuver().note << '\n';
  check(session.surface()->landed.has_value(),
        "bounded real thrust lands from above actual generated bedrock");
  if (session.surface()->landed) {
    const auto pose = session.document().flight;
    required(session.release_surface());
    check(session.document().flight == pose,
          "liftoff command never teleports or injects an impulse");
    unsigned ascent = 0;
    bool propulsion = false;
    while (session.surface_maneuver().kind ==
               NativeSurfaceManeuverKind::liftoff &&
           ascent < 1200) {
      const auto step = required(session.advance({}));
      if (meter) meter->sample(step.actuation.central.propulsion);
      propulsion =
          propulsion ||
          step.actuation.central.propulsion.positive_force_newtons.y > 0;
      ++ascent;
    }
    check(propulsion && !session.surface()->landed &&
              session.surface_maneuver().kind == NativeSurfaceManeuverKind::off,
          "rated vertical thrust lifts pads clear and returns control");
    std::cout << "initial liftoff ticks " << ascent << " note "
              << session.surface_maneuver().note << '\n';
    ascent_and_orbit(session, f, meter);
  }
  auto pilot = open();
  required(pilot.set_assistance(false));
  required(pilot.request_landing());
  check(pilot.surface()->gear_deployed &&
            pilot.surface_maneuver().kind == NativeSurfaceManeuverKind::off &&
            pilot.document().flight == flight.flight,
        "manual landing action deploys gear and retains piloted flight");
  auto far = flight;
  far.flight.position_metres = add(far.flight.position_metres,
                                   rotate(far.flight.orientation, {0, 100, 0}));
  auto refused = required(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom, far, f.planet, {}}));
  check(!refused.request_landing() && !refused.surface(),
        "assisted out-of-range refusal is transactional");
}
auto persistence_and_session(const Fixture& f) -> void {
  FreedomFlightSaveDocument flight{
      make_freedom_new_game_document(Seed{42}), f.source(f.fixed), {}};
  flight.origin.state.tick = flight.flight.tick;
  flight.origin.state.discoveries.push_back({{77}, 8});
  flight.origin.state.world_deltas.push_back(
      {"signal:77", SaveWorldDeltaKind::discovered, 9});
  flight.model.physical_catalog = 2;
  flight.model.physical_ephemeris = 2;
  flight.model.terrain_policy = f.terrain_policy;
  const auto historical = required(encode_freedom_flight_document_json(flight));
  auto session = required(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom, flight, f.planet, {}}));
  check(!session.surface(),
        "historical flight starts without silently adding a new save layer");
  const auto source_checksum =
      required(rigid_body_state_checksum(f.context, flight.flight));
  check(!session.commit_touchdown(source_checksum),
        "prospective ready geometry does not deploy actual gear");
  check(static_cast<bool>(session.set_landing_gear(true)),
        "explicit gear action deploys actual gear");
  const auto gear_bytes = required(
      encode_freedom_surface_document_json(session.surface_document()));
  check(!session.commit_touchdown(source_checksum ^ 1) &&
            gear_bytes == required(encode_freedom_surface_document_json(
                              session.surface_document())),
        "stale assessment refuses whole session transactionally");
  check(static_cast<bool>(session.commit_touchdown(source_checksum)),
        "native session commits physical supported landing");
  const auto anchor = *session.surface()->landed;
  check(!session.set_landing_gear(false) && !session.capture_port() &&
            !session.commit_touchdown(source_checksum),
        "landed gear, capture and repeated landing refuse");
  for (unsigned tick = 0; tick < 1200; ++tick)
    required(session.advance({}));
  check(session.document().flight ==
                required(resolve_landed_craft(f.owner, f.rotation, anchor,
                                              flight.flight.tick + 1200)) &&
            session.document().origin.state.tick == flight.flight.tick + 1200 &&
            session.surface()->landed == anchor,
        "native idle advances one shared clock and retains immutable fixed "
        "anchor");
  NativeFlightControls invalid;
  invalid.positive_translation.x = std::numeric_limits<double>::quiet_NaN();
  const auto idle_bytes = required(
      encode_freedom_surface_document_json(session.surface_document()));
  NativeFlightControls thrust;
  thrust.positive_translation.y = 1;
  check(
      !session.advance(invalid) && !session.advance(thrust) &&
          !session.advance({}, SimulationSeconds{.1}) &&
          idle_bytes == required(encode_freedom_surface_document_json(
                            session.surface_document())),
      "invalid or landed thrust input cannot hide motion or advance the clock");
  const auto decoded =
      required(decode_freedom_surface_document_json(idle_bytes));
  check(decoded == session.surface_document() &&
            idle_bytes ==
                required(encode_freedom_surface_document_json(decoded)),
        "version23 encodes and roundtrips the complete constrained state "
        "exactly");
  auto restored = required(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom, decoded, f.planet, {}}));
  required(session.advance({}));
  required(restored.advance({}));
  check(session.document() == restored.document() &&
            session.surface() == restored.surface(),
        "Continue resumes the next landed tick exactly");
  const auto released = restored.document().flight;
  check(static_cast<bool>(session.release_surface()) &&
            static_cast<bool>(restored.release_surface()) &&
            restored.document().flight == released &&
            session.document() == restored.document() &&
            !restored.surface()->landed && restored.surface()->gear_deployed,
        "liftoff preparation preserves exact momentum and deployed gear across "
        "save/load");
  check(!restored.release_surface(), "repeat liftoff refuses while airborne");
  required(session.advance(thrust));
  required(restored.advance(thrust));
  check(session.document() == restored.document() &&
            session.document().origin.state.discoveries ==
                flight.origin.state.discoveries &&
            session.document().origin.state.world_deltas ==
                flight.origin.state.world_deltas,
        "real ascent thrust advances the same vehicle without awards or "
        "history mutation");
  using Json = nlohmann::ordered_json;
  const auto root = Json::parse(idle_bytes);
  for (const auto name :
       {"version", "terrain_policy", "rotation_generator", "rotation_owner"}) {
    auto malformed = root;
    malformed["surface"]["landed"][name] = 99;
    check(!decode_freedom_surface_document_json(malformed.dump()),
          "unsupported anchor version refuses");
  }
  auto malformed = root;
  malformed["surface"]["gear_deployed"] = false;
  check(!decode_freedom_surface_document_json(malformed.dump()),
        "landed save with stowed gear refuses");
  malformed = root;
  malformed["surface"]["unexpected"] = 1;
  check(!decode_freedom_surface_document_json(malformed.dump()),
        "unknown surface fields refuse");
  auto mismatched = decoded;
  mismatched.surface.landed->terrain_policy = f.terrain_policy == 1 ? 2 : 1;
  check(!encode_freedom_surface_document_json(mismatched),
        "landed anchor cannot silently switch the saved physical field");
  auto contradictory = decoded;
  std::get<FreedomFlightSaveDocument>(contradictory.base)
      .flight.position_metres.x += 1;
  check(!encode_freedom_surface_document_json(contradictory),
        "save cannot detach its flight pose from fixed constraint");
  check(!decode_freedom_surface_document_json(
            "{\"format_version\":23,\"format_version\":23}") &&
            !decode_freedom_surface_document_json(std::string(65, '[') +
                                                  std::string(65, ']')) &&
            !decode_freedom_surface_document_json(
                std::string(kMaximumSaveDocumentBytes + 1, ' ')),
        "duplicate keys, excessive depth and document boundary refuse");
  const auto temporary =
      std::filesystem::temp_directory_path() /
      ("apsis-drift-landed-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".json");
  required(session.save_as(temporary));
  const auto continued = required(native_continue(temporary));
  auto disk = required(NativeFreedomFlightSession::open(continued));
  check(disk.document() == session.document() &&
            disk.surface() == session.surface(),
        "atomic Save As and actual native Continue load version23");
  std::filesystem::remove(temporary);
  check(historical == required(encode_freedom_flight_document_json(flight)),
        "original historical format18 bytes remain unchanged");
}
auto observed_lifecycle(const Fixture& f) -> void {
  FreedomFlightSaveDocument flight{
      make_freedom_new_game_document(Seed{42}), f.source(f.fixed), {}};
  flight.origin.state.tick = flight.flight.tick;
  flight.model.physical_catalog = 2;
  flight.model.physical_ephemeris = 2;
  flight.model.terrain_policy = f.terrain_policy;
  FreedomKnowledgeSaveDocument selected;
  selected.voyage.voyage.base = flight;
  selected.voyage.voyage.surface.gear_deployed = true;
  selected.voyage.resources = {1, flight.origin.state.craft,
                               flight.flight.craft, flight.flight.tick};
  selected.knowledge = required(make_freedom_starting_knowledge(Seed{42}, 2));
  auto session = required(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom, selected, f.planet, {}}));
  const auto checksum =
      required(rigid_body_state_checksum(f.context, flight.flight));
  const auto initial = session.knowledge();
  check(!session.commit_touchdown(checksum ^ 1) &&
            session.knowledge() == initial,
        "Rejected actual touchdown cannot award a visit");
  required(session.commit_touchdown(checksum));
  const KnowledgeSubject subject{KnowledgeSubjectKind::planet,
                                 f.owner.catalog.id, f.planet.id.value};
  const auto presence = required(query_freedom_knowledge(
      *session.knowledge(), subject, KnowledgeFact::presence,
      session.document().flight.tick));
  check(presence &&
            presence->provenance.level == NavigationKnowledgeLevel::visited &&
            presence->provenance.tick == flight.flight.tick,
        "Qualified actual touchdown records physical presence once");
  const auto landed_knowledge = session.knowledge();
  for (unsigned tick = 0; tick < 120; ++tick)
    required(session.advance({}));
  check(
      session.knowledge() == landed_knowledge,
      "Actual landed clock cannot duplicate or erase survey/visit provenance");
  required(session.release_surface());
  for (unsigned tick = 0; tick < 120; ++tick)
    required(session.advance({}));
  check(session.knowledge() == landed_knowledge &&
            session.resources()->flight_quanta < kFreedomFlightCapacityQuanta,
        "Real thrust liftoff retains visits and still bills finite propulsion");
  const auto path =
      std::filesystem::temp_directory_path() /
      ("apsis-observed-surface-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".json");
  required(session.save_as(path));
  const auto resumed = required(
      NativeFreedomFlightSession::open(required(native_continue(path))));
  check(resumed.knowledge() == session.knowledge() &&
            resumed.document() == session.document() &&
            resumed.resources() == session.resources(),
        "Actual landed/ascent Save As and Continue retain all selected owners");
  std::filesystem::remove(path);
}

auto native_fixtures(const Fixture& f, const std::filesystem::path& directory)
    -> void {
  using Json = nlohmann::ordered_json;
  auto station = required(
      NativeFreedomFlightSession::open(required(native_new_game(Seed{42}))));
  for (unsigned tick = 0; tick < 2960; ++tick)
    required(station.advance_walk({0, -1, 0}));
  required(station.begin_boarding());
  for (unsigned tick = 0; tick < kGameplayBoardingTicks; ++tick)
    required(station.advance_walk({}));
  // This is an explicit near-ground test configuration. It does not claim a
  // playable station-to-site voyage or replace the ordinary New Game spawn.
  auto base =
      std::get<FreedomBoardingSaveDocument>(station.surface_document().base);
  auto fixed = f.fixed;
  fixed.tick = base.voyage.flight.flight.tick;
  fixed.position_metres =
      add(fixed.position_metres, rotate(fixed.orientation, {0, .7, 0}));
  base.voyage.flight.flight = f.source(fixed);
  base.voyage.docking.attached = false;
  base.voyage.flight.model.terrain_policy = f.terrain_policy;
  base.voyage.flight.model.assistance = true;
  required(
      write_freedom_boarding_file_atomically(directory / "source.json", base));
  auto session = required(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom, base, f.planet, {}}));
  Json phases = Json::array();
  const auto checkpoint = [&](std::string_view name, unsigned ticks) {
    const auto file = std::string{name} + ".json";
    required(session.save_as(directory / file));
    phases.push_back(
        {{"name", name},
         {"ticks", ticks},
         {"file", file},
         {"checksum", std::to_string(required(rigid_body_state_checksum(
                          f.context, session.document().flight)))}});
  };
  required(session.request_landing());
  unsigned ticks = 0;
  while (!session.surface()->landed && ticks < 1200) {
    required(session.advance({}));
    ++ticks;
  }
  if (!session.surface()->landed)
    throw std::runtime_error("native landing fixture failed");
  checkpoint("landed", ticks);
  for (unsigned tick = 0; tick < 1200; ++tick)
    required(session.advance({}));
  checkpoint("idle", 1200);
  required(session.release_surface());
  checkpoint("released", 0);
  ticks = 0;
  while (session.surface_maneuver().kind ==
             NativeSurfaceManeuverKind::liftoff &&
         ticks < 1200) {
    required(session.advance({}));
    ++ticks;
  }
  checkpoint("airborne", ticks);
  required(session.set_landing_gear(false));
  NativeFlightControls thrust;
  thrust.positive_translation.y = 1;
  for (unsigned tick = 0; tick < 1200; ++tick)
    required(session.advance(thrust));
  checkpoint("ascent", 1200);
  const Json manifest = {
      {"schema_version", 1},
      {"scope",
       "near-ground physical lifecycle fixture; not station-to-site gameplay"},
      {"source", "source.json"},
      {"phases", phases}};
  std::ofstream output(directory / "manifest.json");
  output << manifest.dump(2) << '\n';
  if (!output) throw std::runtime_error("native manifest write failed");
}
auto surface_walk_session(const Fixture& f,
                          const std::filesystem::path& directory = {}) -> void {
  auto station = required(
      NativeFreedomFlightSession::open(required(native_new_game(Seed{42}))));
  for (unsigned tick = 0; tick < 2960; ++tick)
    required(station.advance_walk({0, -1, 0}));
  required(station.begin_boarding());
  for (unsigned tick = 0; tick < kGameplayBoardingTicks; ++tick)
    required(station.advance_walk({}));
  auto base =
      std::get<FreedomBoardingSaveDocument>(station.surface_document().base);
  auto fixed = f.fixed;
  fixed.tick = base.voyage.flight.flight.tick;
  base.voyage.flight.flight = f.source(fixed);
  base.voyage.docking.attached = false;
  base.voyage.flight.model.terrain_policy = f.terrain_policy;
  // Explicit near-ground fixture, never an uninterrupted station-to-site claim.
  FreedomKnowledgeSaveDocument selected{};
  selected.voyage.voyage.base = base;
  selected.voyage.voyage.surface.gear_deployed = true;
  selected.voyage.resources = *station.resources();
  selected.knowledge = *station.knowledge();
  auto session = required(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom, selected, f.planet, {}}));
  required(session.commit_touchdown(required(
      rigid_body_state_checksum(f.context, session.document().flight))));
  const auto anchor = session.surface()->landed;
  const auto resources = session.resources();
  const auto initial_tick = session.document().flight.tick;
  const auto checkpoint = [&](std::string_view name) {
    if (!directory.empty())
      required(session.save_as(directory / (std::string{name} + ".json")));
  };
  checkpoint("source");
  required(session.begin_surface_walk());
  checkpoint("exited");
  check(session.surface_walker() && !session.walker() &&
            session.document().flight.tick == initial_tick,
        "Explicit surface exit transfers actor ownership without changing the "
        "craft or clock");
  const auto snapshot = [&] {
    return required(encode_freedom_surface_walk_document_json(
        required(session.surface_walk_document())));
  };
  const auto before = snapshot();
  const auto temporary =
      std::filesystem::temp_directory_path() /
      ("apsis-surface-walk-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".json");
  required(session.save_as(temporary));
  auto disk = required(
      NativeFreedomFlightSession::open(required(native_continue(temporary))));
  check(required(encode_freedom_surface_walk_document_json(
            required(disk.surface_walk_document()))) == before,
        "Actual atomic Save As and native Continue retain the complete outside "
        "actor and voyage");
  std::filesystem::remove(temporary);
  auto loss = disk;
  const auto loss_tick = loss.document().flight.tick;
  required(loss.record_loss(
      FreedomLossCause::recoverable_destruction,
      loss.document().origin.state.craft, loss_tick,
      required(rigid_body_state_checksum(f.context, loss.document().flight))));
  const auto pending = required(encode_freedom_surface_walk_document_json(
      required(loss.surface_walk_document())));
  if (!directory.empty())
    required(loss.save_as(directory / "pending-loss.json"));
  check(!loss.advance_surface_walk({}) && !loss.return_from_surface_walk() &&
            pending == required(encode_freedom_surface_walk_document_json(
                           required(loss.surface_walk_document()))),
        "Declared loss stops outside commands without inventing an automatic "
        "surface fatality");
  auto lost_resume = required(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom,
       required(decode_freedom_surface_walk_document_json(pending)),
       f.planet,
       {}}));
  required(loss.complete_recovery());
  required(lost_resume.complete_recovery());
  if (!directory.empty()) required(loss.save_as(directory / "recovered.json"));
  check(loss.surface_walk_selected() && !loss.surface_walker() &&
            loss.walker() &&
            required(encode_freedom_surface_walk_document_json(
                required(loss.surface_walk_document()))) ==
                required(encode_freedom_surface_walk_document_json(
                    required(lost_resume.surface_walk_document()))),
        "Outside recovery restores one station actor and preserves suit "
        "selection, retired craft history and exact resumed replacement");
  check(!session.begin_surface_walk() && !session.advance({}) &&
            !session.advance_walk({}) && !session.release_surface() &&
            !session.set_assistance(false) && !session.begin_disembarking() &&
            snapshot() == before,
        "Outside actor cannot control or launch the craft; refused commands "
        "preserve the whole voyage");
  for (unsigned tick = 0; tick < 600; ++tick)
    required(session.advance_surface_walk({-1, 0, 0}));
  check(!session.return_from_surface_walk(),
        "Distant actor cannot reboard by clicking a button");
  checkpoint("halfway");
  const auto halfway = snapshot();
  auto resumed = required(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom,
       required(decode_freedom_surface_walk_document_json(halfway)),
       f.planet,
       {}}));
  for (unsigned tick = 0; tick < 600; ++tick) {
    required(session.advance_surface_walk({-1, 0, 0}));
    required(resumed.advance_surface_walk({-1, 0, 0}));
  }
  check(
      required(encode_freedom_surface_walk_document_json(
          required(resumed.surface_walk_document()))) == snapshot(),
      "Continued surface walking reproduces the complete uninterrupted voyage");
  check(session.document().flight.tick == initial_tick + 1200 &&
            session.surface()->landed == anchor &&
            session.resources()->flight_quanta == resources->flight_quanta &&
            session.resources()->jump_charges == resources->jump_charges,
        "Ground walking shares the ship clock without fuel or anchor edits");
  checkpoint("far");
  auto malformed = nlohmann::ordered_json::parse(snapshot());
  malformed["actor"]["planet"] = 0U;
  check(!decode_freedom_surface_walk_document_json(malformed.dump()),
        "Wrong actor body refuses Continue");
  malformed = nlohmann::ordered_json::parse(snapshot());
  malformed["actor"]["foot_position_metres"] =
      nlohmann::ordered_json::array({1, 2});
  check(!decode_freedom_surface_walk_document_json(malformed.dump()),
        "Short surface position refuses Continue");
  check(!decode_freedom_surface_walk_document_json(
            "{\"format_version\":30,\"format_version\":30}") &&
            !decode_freedom_surface_walk_document_json(std::string(65, '[') +
                                                       std::string(65, ']')),
        "Duplicate keys and excessive nesting refuse surface saves");
  for (unsigned tick = 0; tick < 1200; ++tick)
    required(session.advance_surface_walk({1, 0, 0}));
  checkpoint("returned");
  required(session.return_from_surface_walk());
  checkpoint("seated");
  check(!session.surface_walker() && session.surface()->landed == anchor,
        "Nearby return restores the retained seated craft without moving it");
  const auto inside = snapshot();
  auto inside_resume = required(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom,
       required(decode_freedom_surface_walk_document_json(inside)),
       f.planet,
       {}}));
  required(session.release_surface());
  required(inside_resume.release_surface());
  for (unsigned tick = 0; tick < 120; ++tick) {
    required(session.advance({}));
    required(inside_resume.advance({}));
  }
  checkpoint("lifted");
  check(required(encode_freedom_surface_walk_document_json(
            required(inside_resume.surface_walk_document()))) == snapshot(),
        "Reboarded Continue retains exact real-thrust liftoff");
  std::cout << "Surface voyage checksum: "
            << required(rigid_body_state_checksum(f.context,
                                                  session.document().flight))
            << '\n';
}
auto surface_walking(const Fixture& f) -> void {
  const auto source = f.source(f.fixed);
  const auto checksum = required(rigid_body_state_checksum(f.context, source));
  const auto anchor = required(prepare_landed_craft(
      f.owner, f.rotation, source, true, checksum, f.terrain_policy));
  auto terrain = required(PlanetSurfaceWalkTerrain::create(
      f.owner, f.rotation, anchor, source.tick));
  const auto start = required(terrain.entry());
  check(terrain.validate(start).has_value() &&
            required(terrain.near_entry(start)),
        "Actual landed Wayfarer has supported nearby hatch ground access");
  auto independent_cache = required(TerrainTileCache::create(1));
  const auto foot = start.foot_position_metres;
  const auto id =
      required(godot_spike::detail::locate_selected_contact_triangle(
          f.planet, {foot.x, foot.y, foot.z}));
  const auto triangle =
      required(godot_spike::detail::build_selected_contact_triangle(
          f.owner, f.planet,
          *godot_spike::saved_contact_recipe(f.terrain_policy), id,
          independent_cache));
  const auto radial = mul(foot, 1 / norm(foot));
  const auto hit =
      ray_hit(add(foot, mul(radial, 2)), mul(radial, -1), triangle);
  check(hit && std::abs(*hit - 2.0L) < 1e-8L,
        "Independent long-double ray agrees with the actual rendered top "
        "triangle at the foot");
  auto actor = start;
  for (int i = 0; i < 1200; ++i) {
    const auto step = required(terrain.advance(actor, {-1, 0, 0}));
    check(step.status == PlanetSurfaceWalkStatus::supported,
          "Walking away remains supported without an invented area wall");
    actor = step.actor;
  }
  check(norm(sub(actor.foot_position_metres, start.foot_position_metres)) >
                19 &&
            !required(terrain.near_entry(actor)) && terrain.cache_size() <= 4,
        "Ground travel leaves interaction range with bounded terrain cache");
  for (int i = 0; i < 1200; ++i)
    actor = required(terrain.advance(actor, {1, 0, 0})).actor;
  check(norm(sub(actor.foot_position_metres, start.foot_position_metres)) <
                .001 &&
            required(terrain.near_entry(actor)),
        "Walking back reacquires the unchanged physical craft");
  bool obstruction = false;
  for (int i = 0; i < 120; ++i) {
    const auto step = required(terrain.advance(actor, {1, 0, 0}));
    obstruction |= step.status == PlanetSurfaceWalkStatus::craft_obstruction;
    actor = step.actor;
  }
  check(obstruction && terrain.validate(actor).has_value(),
        "Ordinary ground walking stops at the craft rather than entering its "
        "hull");
  const auto cached = terrain.cache_size();
  for (double value : {std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity(), 2.0}) {
    for (int axis = 0; axis < 3; ++axis) {
      auto controls = OriginWalkControls{};
      if (axis == 0) controls.forward = value;
      if (axis == 1) controls.right = value;
      if (axis == 2) controls.heading_radians = value == 2.0 ? 4.0 : value;
      check(!terrain.advance(actor, controls), "Invalid walking axes refuse");
    }
  }
  auto invalid = actor;
  invalid.foot_position_metres.x = std::numeric_limits<double>::quiet_NaN();
  check(!terrain.advance(invalid, {}) && terrain.cache_size() == cached,
        "Malformed position refuses before terrain cache access");
  invalid = actor;
  invalid.version = 2;
  check(!terrain.advance(invalid, {}), "Unsupported walker policy refuses");
  invalid = actor;
  invalid.frame.planet = PlanetId{0};
  check(!terrain.advance(invalid, {}) && terrain.cache_size() == cached,
        "Wrong actor body refuses before querying terrain");
  check(!terrain.advance(actor, {}, SimulationSeconds{0}) &&
            !terrain.advance(actor, {}, SimulationSeconds{1}),
        "Only the existing simulation tick advances ground kinematics");
  auto stale = anchor;
  stale.fixed.frame.planet = PlanetId{0};
  check(!PlanetSurfaceWalkTerrain::create(f.owner, f.rotation, stale,
                                          source.tick),
        "Wrong landed body cannot populate a walking terrain owner");
  stale = anchor;
  stale.fixed.position_metres.x += 100;
  check(!PlanetSurfaceWalkTerrain::create(f.owner, f.rotation, stale,
                                          source.tick),
        "Unsupported craft anchor cannot start surface walking");
  check(terrain.anchor() == anchor,
        "Walking and refusals never move the retained craft anchor");
  std::cout << std::setprecision(17)
            << "Surface walker checkpoint: " << actor.foot_position_metres.x
            << ',' << actor.foot_position_metres.y << ','
            << actor.foot_position_metres.z << '\n';
}
// Explicitly initialized, real catalog2 snapshots; only the adjacent setting
// limb samples are an uninterrupted simulation trace. No accelerated clock.
using DaynightMatrix = std::array<std::array<double, 3>, 3>;
auto daynight_dot(V a, V b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto daynight_apply(const DaynightMatrix& m, V v) -> V {
  return {daynight_dot({m[0][0], m[0][1], m[0][2]}, v),
          daynight_dot({m[1][0], m[1][1], m[1][2]}, v),
          daynight_dot({m[2][0], m[2][1], m[2][2]}, v)};
}
auto daynight_transpose(const DaynightMatrix& m) -> DaynightMatrix {
  return {{{m[0][0], m[1][0], m[2][0]},
           {m[0][1], m[1][1], m[2][1]},
           {m[0][2], m[1][2], m[2][2]}}};
}
auto daynight_product(const DaynightMatrix& a, const DaynightMatrix& b)
    -> DaynightMatrix {
  DaynightMatrix result{};
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j)
      for (std::size_t k = 0; k < 3; ++k)
        result[i][j] += a[i][k] * b[k][j];
  return result;
}
auto daynight_rotation(const Fixture& f, SimulationTick tick)
    -> DaynightMatrix {
  // Independent trigonometric matrix composition: no production quaternion
  // multiplication, rotation resolver, frame handoff or native tangent helper.
  const auto rx = [](double angle) -> DaynightMatrix {
    const auto c = std::cos(angle), s = std::sin(angle);
    return {{{1, 0, 0}, {0, c, -s}, {0, s, c}}};
  };
  const auto ry = [](double angle) -> DaynightMatrix {
    const auto c = std::cos(angle), s = std::sin(angle);
    return {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}};
  };
  const auto rz = [](double angle) -> DaynightMatrix {
    const auto c = std::cos(angle), s = std::sin(angle);
    return {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
  };
  const auto& orbit = f.owner.catalog.planets[kOriginHomePlanetOrdinal].orbit;
  const auto& recipe = f.rotation.rotation;
  const auto cycle = (tick % recipe.period_ticks + recipe.epoch_phase_tick) %
                     recipe.period_ticks;
  const auto pi = std::numbers::pi_v<double>;
  return daynight_product(
      daynight_product(
          daynight_product(
              daynight_product(
                  rz(2 * pi * orbit.ascending_node_turns / 4294967296.0),
                  rx(orbit.inclination_microdegrees * pi / 180000000.0)),
              rz(2 * pi * recipe.pole_azimuth_turns / 4294967296.0)),
          ry(recipe.tilt_microdegrees * pi / 180000000.0)),
      rz(2 * pi * static_cast<double>(cycle) /
         static_cast<double>(recipe.period_ticks)));
}
struct DaynightOracle {
  V system, fixed, native;
  double distance, elevation, horizon, angular_radius;
};
auto daynight_oracle(const Fixture& f, const LandedCraftAnchor& anchor,
                     SimulationTick tick) -> DaynightOracle {
  const auto matrix = daynight_rotation(f, tick);
  const auto ephemeris =
      required(resolve_planet_ephemeris(f.owner, f.planet.id, {tick, 0}));
  const auto position = anchor.fixed.position_metres;
  const auto observer = daynight_apply(matrix, position);
  const auto toward = sub(
      mul({ephemeris.position.x, ephemeris.position.y, ephemeris.position.z},
          -1),
      observer);
  const auto distance = norm(toward);
  const auto system = mul(toward, 1 / distance);
  const auto fixed = daynight_apply(daynight_transpose(matrix), system);
  const auto radial = norm(position);
  const auto up = mul(position, 1 / radial);
  const auto latitude = std::asin(up.z);
  const auto longitude = std::atan2(up.y, up.x);
  const V east{-std::sin(longitude), std::cos(longitude), 0};
  const V north{-std::sin(latitude) * std::cos(longitude),
                -std::sin(latitude) * std::sin(longitude), std::cos(latitude)};
  const V native{daynight_dot(east, fixed), daynight_dot(up, fixed),
                 -daynight_dot(north, fixed)};
  const auto radius = f.planet.radius.value * 1000.0;
  return {
      system,
      fixed,
      native,
      distance,
      std::asin(std::clamp(native.y, -1.0, 1.0)),
      -std::acos(std::clamp(radius / std::max(radius, radial), 0.0, 1.0)),
      std::asin(f.owner.catalog.star.radius_kilometres * 1000.0 / distance)};
}
auto daynight_snapshots(const Fixture& f,
                        const std::filesystem::path& directory = {}) -> void {
  check(f.owner.generator_version == 2 && f.owner.ephemeris_version == 2 &&
            f.terrain_policy == kFreedomLocalDetailTerrainPolicy,
        "day/night fixtures explicitly select physical catalog2 and terrain2");
  FreedomFlightSaveDocument flight{
      make_freedom_new_game_document(Seed{42}), f.source(f.fixed), {}};
  flight.origin.state.tick = flight.flight.tick;
  flight.origin.state.discoveries.push_back({{77}, 8});
  flight.origin.state.world_deltas.push_back(
      {"signal:77", SaveWorldDeltaKind::discovered, 9});
  flight.model.physical_catalog = 2;
  flight.model.physical_ephemeris = 2;
  flight.model.terrain_policy = f.terrain_policy;
  flight.world =
      FreedomActiveWorldSelection{1, f.owner.catalog.id, f.planet.id};
  auto landed = required(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom, flight, f.planet, {}}));
  required(landed.set_landing_gear(true));
  required(landed.commit_touchdown(required(
      rigid_body_state_checksum(f.context, landed.document().flight))));
  const auto anchor = *landed.surface()->landed;
  auto malformed = anchor;
  malformed.fixed.position_metres.x = std::numeric_limits<double>::quiet_NaN();
  check(
      !resolve_landed_craft(f.owner, f.rotation, malformed, flight.flight.tick),
      "nonfinite day/night anchor refuses before projection");
  check(
      !resolve_landed_craft(f.owner, f.rotation, anchor, anchor.fixed.tick - 1),
      "day/night fixture cannot resolve before its landing tick");
  const auto start = anchor.fixed.tick;
  const auto period = f.rotation.rotation.period_ticks;
  SimulationTick day = start, low = start, high = start, night = start;
  double brightest = -2;
  bool crossed = false;
  for (SimulationTick i = 0; i <= 256; ++i) {
    const auto tick = start + (2 * period * i) / 256;
    const auto oracle = daynight_oracle(f, anchor, tick);
    const auto limb = oracle.elevation - oracle.horizon;
    if (!crossed) {
      if (oracle.elevation > brightest) {
        brightest = oracle.elevation;
        day = tick;
      }
      if (i != 0 && brightest > .1 && limb <= 0) {
        high = tick;
        crossed = true;
      } else {
        low = tick;
      }
    } else if (limb < -std::max(.1, oracle.angular_radius)) {
      night = tick;
      break;
    }
  }
  if (!crossed || night <= high || day >= low)
    throw std::runtime_error("bounded catalog2 day/night sequence not found");
  while (high - low > 1) {
    const auto middle = low + (high - low) / 2;
    const auto oracle = daynight_oracle(f, anchor, middle);
    if (oracle.elevation - oracle.horizon > 0)
      low = middle;
    else
      high = middle;
  }
  const std::array<SimulationTick, 5> ticks{day, high - 1, high, high + 1,
                                            night};
  const std::array<std::string_view, 5> labels{
      "day", "terminator-before", "terminator", "terminator-after", "night"};
  std::array<std::string, 5> encoded;
  nlohmann::ordered_json phases = nlohmann::ordered_json::array();
  std::optional<NativeFreedomFlightSession> crossing;
  for (std::size_t i = 0; i < ticks.size(); ++i) {
    auto snapshot = landed.surface_document();
    auto& base = std::get<FreedomFlightSaveDocument>(snapshot.base);
    base.flight =
        required(resolve_landed_craft(f.owner, f.rotation, anchor, ticks[i]));
    base.origin.state.tick = ticks[i];
    const auto text = required(encode_freedom_surface_document_json(snapshot));
    auto session = required(NativeFreedomFlightSession::open(
        {NativeStartup::Mode::freedom,
         required(decode_freedom_surface_document_json(text)),
         f.planet,
         {}}));
    encoded[i] = required(
        encode_freedom_surface_document_json(session.surface_document()));
    check(encoded[i] == text && session.surface()->landed == anchor &&
              session.document().origin.state.discoveries ==
                  flight.origin.state.discoveries &&
              session.document().origin.state.world_deltas ==
                  flight.origin.state.world_deltas,
          "each phase Continue preserves complete owner, history and landed "
          "anchor");
    const auto view = godot_spike::project_saved_flight(session);
    const auto oracle = daynight_oracle(f, anchor, ticks[i]);
    const auto& geometry = view.rotation.geometry;
    check(norm(sub({geometry.observer_to_star_fixed.x,
                    geometry.observer_to_star_fixed.y,
                    geometry.observer_to_star_fixed.z},
                   oracle.fixed)) < 2e-14 &&
              norm(sub({geometry.observer_to_star_system.x,
                        geometry.observer_to_star_system.y,
                        geometry.observer_to_star_system.z},
                       oracle.system)) < 2e-14,
          "actual system/body star vectors agree with independent matrix "
          "geometry");
    const auto direction = geometry.observer_to_star_fixed;
    const auto& tangent = view.tangent;
    const V native{direction.x * tangent.east.x + direction.y * tangent.east.y +
                       direction.z * tangent.east.z,
                   direction.x * tangent.up.x + direction.y * tangent.up.y +
                       direction.z * tangent.up.z,
                   -(direction.x * tangent.north.x +
                     direction.y * tangent.north.y +
                     direction.z * tangent.north.z)};
    check(norm(sub(native, oracle.native)) < 2e-14 &&
              std::abs(geometry.observer_star_distance_metres -
                       oracle.distance) < 2e-14 * oracle.distance,
          "native tangent star and finite-distance limb inputs match "
          "independent geometry");
    const auto fixed = required(
        reframe_rigid_body(f.context, session.document().flight,
                           {anchor.fixed.frame, ticks[i]}, f.rotation));
    check(norm(sub(fixed.position_metres, anchor.fixed.position_metres)) <
                  1e-7 &&
              norm(fixed.linear_velocity_metres_per_second) < 1e-9,
          "all declared times retain the same physical ground anchor");
    const auto before = encoded[i];
    const std::array<double, 12> neutral{};
    godot_spike::SavedFlightWorld paused{session, {}, {}, 0};
    paused.advance(60, neutral, true);
    check(required(encode_freedom_surface_document_json(
              paused.session.surface_document())) == before,
          "paused full landed save stays exact at every phase");
    if (i == 1) crossing.emplace(session);
    if (i == 2 || i == 3) {
      required(crossing->advance({}));
      check(required(encode_freedom_surface_document_json(
                crossing->surface_document())) == encoded[i],
            "ordinary landed 120Hz step exactly reaches the adjacent "
            "terminator save");
    }
    const auto limb = oracle.elevation - oracle.horizon;
    if (i == 0)
      check(limb > oracle.angular_radius, "day disk is fully above real limb");
    if (i == 4)
      check(limb < -oracle.angular_radius,
            "night disk is fully behind real limb");
    if (i == 1)
      check(limb > 0 && limb < 1e-5,
            "last illuminated-center tick is adjacent to limb");
    if (i == 2)
      check(limb <= 0 && limb > -1e-5,
            "first hidden-center tick is adjacent to limb");
    phases.push_back({{"name", labels[i]},
                      {"file", std::string{labels[i]} + ".json"},
                      {"tick", std::to_string(ticks[i])},
                      {"star_direction_native",
                       {oracle.native.x, oracle.native.y, oracle.native.z}},
                      {"solar_elevation_radians", oracle.elevation},
                      {"horizon_radians", oracle.horizon},
                      {"star_angular_radius_radians", oracle.angular_radius},
                      {"observer_star_distance_metres", oracle.distance},
                      {"altitude_metres", norm(anchor.fixed.position_metres) -
                                              f.planet.radius.value * 1000.0}});
    if (!directory.empty()) {
      std::ofstream out{directory / (std::string{labels[i]} + ".json"),
                        std::ios::binary};
      out << encoded[i];
      if (!out) throw std::runtime_error("day/night save write failed");
    }
  }
  if (!directory.empty()) {
    const nlohmann::ordered_json manifest{
        {"schema_version", 1},
        {"catalog_generator", 2},
        {"ephemeris_generator", 2},
        {"terrain_policy", 2},
        {"system_id", std::to_string(f.owner.catalog.id.value)},
        {"planet_id", std::to_string(f.planet.id.value)},
        {"anchor_tick", std::to_string(anchor.fixed.tick)},
        {"rotation_period_ticks", std::to_string(period)},
        {"scope", "Explicit initialized real catalog2 times at one qualified "
                  "landed anchor; only the two adjacent terminator transitions "
                  "are continuously stepped. No accelerated clock or "
                  "uninterrupted day-long flight."},
        {"terminator_convention",
         "Setting stellar disk center crosses the observer altitude-dependent "
         "spherical limb; solar elevation and terrain normal illumination are "
         "separate quantities."},
        {"oracle",
         "Independent Rx/Ry/Rz rotation matrix and ENU basis, actual catalog2 "
         "same-tick ephemeris and finite-distance star geometry."},
        {"phases", phases}};
    std::ofstream out{directory / "manifest.json"};
    out << manifest.dump(2) << '\n';
    if (!out) throw std::runtime_error("day/night manifest write failed");
  }
}

} // namespace
auto main(int argc, char** argv) -> int {
  try {
    const bool local_detail =
        argc >= 2 &&
        (std::string_view{argv[1]} == "--local-detail" ||
         std::string_view{argv[1]} == "--local-detail-fixtures" ||
         std::string_view{argv[1]} == "--local-detail-walk-fixtures" ||
         std::string_view{argv[1]} == "--daynight" ||
         std::string_view{argv[1]} == "--daynight-fixtures");
    const Fixture f{local_detail ? kFreedomLocalDetailTerrainPolicy
                                 : kFreedomLegacyTerrainPolicy};
    if (argc == 2 && std::string_view{argv[1]} == "--daynight") {
      daynight_snapshots(f);
    } else if (argc == 3 &&
               std::string_view{argv[1]} == "--daynight-fixtures") {
      const auto directory = std::filesystem::absolute(argv[2]);
      std::filesystem::create_directories(directory);
      daynight_snapshots(f, directory);
    } else if (argc == 2 && std::string_view{argv[1]} == "--local-detail") {
      hull_and_idle(f);
      persistence_and_session(f);
      surface_walking(f);
      surface_walk_session(f);
      assisted_commands(f);
    } else if (argc == 3 &&
               (std::string_view{argv[1]} == "--fixtures" ||
                std::string_view{argv[1]} == "--local-detail-fixtures")) {
      const auto directory = std::filesystem::absolute(argv[2]);
      std::filesystem::create_directories(directory);
      native_fixtures(f, directory);
    } else if (argc == 3 &&
               (std::string_view{argv[1]} == "--walk-fixtures" ||
                std::string_view{argv[1]} == "--local-detail-walk-fixtures")) {
      const auto directory = std::filesystem::absolute(argv[2]);
      std::filesystem::create_directories(directory);
      surface_walk_session(f, directory);
    } else if (argc == 2 && std::string_view{argv[1]} == "--resources") {
      research::FlightEnduranceMeter meter;
      assisted_commands(f, &meter, true);
      check(
          meter.effort_quanta == 266'483'611'299'623ULL,
          "Resource-enabled surface trace retains the selected research total");
    } else if (argc == 2 && std::string_view{argv[1]} == "--observations") {
      observed_lifecycle(f);
    } else if (argc == 2 && std::string_view{argv[1]} == "--walk") {
      surface_walking(f);
      surface_walk_session(f);
    } else if (argc == 1) {
      hull_and_idle(f);
      persistence_and_session(f);
      assisted_commands(f);
    } else if (argc == 3 && std::string_view{argv[1]} == "--endurance") {
      const std::filesystem::path path{argv[2]};
      if (!path.is_absolute() || path.native().size() > 4000 ||
          !std::filesystem::is_directory(path.parent_path()) ||
          std::filesystem::exists(path))
        throw std::runtime_error(
            "expected bounded fresh absolute endurance file");
      research::FlightEnduranceMeter meter;
      hull_and_idle(f);
      persistence_and_session(f);
      assisted_commands(f, &meter);
      const nlohmann::ordered_json report{
          {"schema_version", 1},
          {"accounting_proposal_version", 1},
          {"scope", "Explicit near-ground landing/liftoff/ascent test pilot; "
                    "no fuel state"},
          {"quanta_per_equivalent_newton_second",
           research::kEffortQuantaPerNewtonSecond},
          {"flight_ticks", meter.ticks},
          {"firing_ticks", meter.firing_ticks},
          {"effort_quanta", meter.effort_quanta},
          {"channel_milliunit_ticks", meter.channel_milliunit_ticks}};
      std::ofstream out{path};
      out << report.dump(2) << '\n';
      if (!out) throw std::runtime_error("endurance write failed");
    } else
      throw std::runtime_error("usage: landed-craft-tests [--fixtures "
                               "DIRECTORY | --endurance FILE]");
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << e.what() << '\n';
  }
  std::cout << "Landed craft core: " << checks << " checks, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
