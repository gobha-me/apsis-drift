#include "apsis_drift/landed_craft.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "apsis_drift/terrain_hull_clearance.hpp"
#include "apsis_drift/terrain_touchdown.hpp"
#include "contact_geometry_internal.hpp"
#include "flight_endurance_meter.hpp"
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
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
  Fixture() {
    auto cache = required(TerrainTileCache::create(1));
    const auto triangle =
        required(godot_spike::detail::build_selected_contact_triangle(
            planet, godot_spike::kExperimentalSavedContactSurface,
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
  const auto hull =
      required(assess_terrain_hull_clearance(f.owner, f.rotation, source, 1));
  check(hull.certified && hull.lower_clearance_metres > .7 &&
            hull.triangle_count <= 128,
        "actual whole hull clears generated terrain within bounded work");
  check(hull == required(assess_terrain_hull_clearance(f.owner, f.rotation,
                                                       source, 64)),
        "hull certificate is cache-independent");
  const auto checksum = required(rigid_body_state_checksum(f.context, source));
  const auto anchor = required(
      prepare_landed_craft(f.owner, f.rotation, source, true, checksum));
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
  auto false_checksum =
      prepare_landed_craft(f.owner, f.rotation, source, true, checksum ^ 1);
  check(!false_checksum &&
            false_checksum.error() == LandedCraftError::stale_source,
        "changed assessment checksum refuses before terrain commit");
  auto stowed =
      prepare_landed_craft(f.owner, f.rotation, source, false, checksum);
  check(!stowed && stowed.error() == LandedCraftError::gear_not_deployed,
        "actual declaration is required; prospective pads do not deploy gear");
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
                    f.planet, godot_spike::kExperimentalSavedContactSurface,
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
  const auto uncertain =
      assess_terrain_hull_clearance(f.owner, f.rotation, f.source(seam));
  check(!uncertain && uncertain.error() == TerrainHullError::face_boundary,
        "a box crossing cube-face seams refuses before terrain access");
  auto buried = f.fixed;
  buried.position_metres =
      sub(buried.position_metres, rotate(buried.orientation, {0, 1, 0}));
  const auto blocked = required(
      assess_terrain_hull_clearance(f.owner, f.rotation, f.source(buried)));
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
} // namespace
auto main(int argc, char** argv) -> int {
  try {
    const Fixture f;
    if (argc == 3 && std::string_view{argv[1]} == "--fixtures") {
      const auto directory = std::filesystem::absolute(argv[2]);
      std::filesystem::create_directories(directory);
      native_fixtures(f, directory);
    } else if (argc == 2 && std::string_view{argv[1]} == "--resources") {
      research::FlightEnduranceMeter meter;
      assisted_commands(f, &meter, true);
      check(
          meter.effort_quanta == 266'483'611'299'623ULL,
          "Resource-enabled surface trace retains the selected research total");
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
