#include "../moon_contact_fixture.hpp"
#include "apsis_drift/godot/saved_flight.hpp"
#include "apsis_drift/moon_surface_walker.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>

namespace {
using namespace apsis_drift;
using namespace apsis_drift::godot_spike;
using V = RigidVector3;
int checks{}, failures{};
auto check(bool ok, std::string_view text) -> void {
  ++checks;
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << text << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> v) -> T {
  if (!v) throw std::runtime_error("Saved moon projection fixture refused");
  return std::move(*v);
}
template <class F> auto rejects(F operation, std::string_view text) -> void {
  bool refused{};
  try {
    operation();
  } catch (const std::invalid_argument&) {
    refused = true;
  }
  check(refused, text);
}
auto near(double a, double b, double tolerance, std::string_view text) -> void {
  check(std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tolerance,
        text);
}
auto dot(V a, V b) -> long double {
  return static_cast<long double>(a.x) * b.x +
         static_cast<long double>(a.y) * b.y +
         static_cast<long double>(a.z) * b.z;
}
auto norm(V a) -> double {
  return std::hypot(a.x, a.y, a.z);
}
auto vector_near(V a, V b, double tolerance, std::string_view text) -> void {
  near(norm({a.x - b.x, a.y - b.y, a.z - b.z}), 0, tolerance, text);
}
// Independent axis-angle source rotation. Recipe1 pole is system+Z, not +Y;
// no production saved-vector helper is used to establish this oracle.
auto fixed_from_system(double phase, V v) -> V {
  const long double c = std::cos(static_cast<long double>(phase)),
                    s = std::sin(static_cast<long double>(phase));
  return {static_cast<double>(c * v.x + s * v.y),
          static_cast<double>(-s * v.x + c * v.y), v.z};
}
auto body_axis(RigidOrientation q, std::size_t i) -> V {
  const long double w = q.w, x = q.x, y = q.y, z = q.z,
                    n = w * w + x * x + y * y + z * z;
  const std::array<std::array<long double, 3>, 3> columns{
      {{{1 - 2 * (y * y + z * z) / n, 2 * (x * y + w * z) / n,
         2 * (x * z - w * y) / n}},
       {{2 * (x * y - w * z) / n, 1 - 2 * (x * x + z * z) / n,
         2 * (y * z + w * x) / n}},
       {{2 * (x * z + w * y) / n, 2 * (y * z - w * x) / n,
         1 - 2 * (x * x + y * y) / n}}}};
  return {static_cast<double>(columns[i][0]),
          static_cast<double>(columns[i][1]),
          static_cast<double>(columns[i][2])};
}
auto expected_local(const SavedMoonFlightProjection& view, V fixed)
    -> LocalPositionMetres {
  const long double lat = view.pose.latitude_radians,
                    lon = view.pose.longitude_radians;
  const auto slat = std::sin(lat), clat = std::cos(lat), slon = std::sin(lon),
             clon = std::cos(lon);
  return {static_cast<double>(-slon * fixed.x + clon * fixed.y),
          static_cast<double>(-slat * clon * fixed.x - slat * slon * fixed.y +
                              clat * fixed.z),
          static_cast<double>(clat * clon * fixed.x + clat * slon * fixed.y +
                              slat * fixed.z)};
}
auto near_local(LocalPositionMetres a, LocalPositionMetres b,
                std::string_view text) -> void {
  near(a.east, b.east, 2e-14, text);
  near(a.north, b.north, 2e-14, text);
  near(a.up, b.up, 2e-14, text);
}
auto verify(const SavedMoonFlightProjection& view,
            const PhysicalMoonDescriptor& moon, const RigidBodyState& source)
    -> void {
  check(view.version == 1 && view.moon == moon && view.body == source &&
            view.body.frame.kind == RigidFrameKind::body_relative_inertial,
        "Projection owns exact recipe/moon and unchanged canonical frame5");
  check(view.fixed_frame == RigidCoordinateFrame{RigidFrameKind::body_fixed,
                                                 source.frame.system,
                                                 {},
                                                 {},
                                                 moon.body},
        "Projection fixed frame retains full typed BodyId without Planet "
        "carrier");
  check(view.rotation == need(resolve_physical_moon_rotation(
                             moon, source.tick,
                             {view.tangent.origin.x, view.tangent.origin.y,
                              view.tangent.origin.z})),
        "Rotation/star geometry resolved at actual observer and authoritative "
        "tick");
  const V observer{view.tangent.origin.x, view.tangent.origin.y,
                   view.tangent.origin.z};
  const auto radius = norm(observer), h = std::hypot(observer.x, observer.y);
  near(view.pose.latitude_radians, std::atan2(observer.z, h), 1e-15,
       "Actual spherical latitude follows system+Z pole");
  near(view.pose.longitude_radians,
       h == 0 ? 0 : std::atan2(observer.y, observer.x), 1e-15,
       "Actual spherical longitude comes from fixed axes");
  near(view.pose.altitude_metres, radius - moon.radius_metres, 1e-8,
       "Altitude uses actual moon radius, no planet floor");
  const auto& t = view.tangent;
  for (const auto& axis : std::array<V, 3>{t.east, t.north, t.up})
    near(norm(axis), 1, 5e-15, "Tangent axes unit");
  near(static_cast<double>(dot(t.east, t.north)), 0, 5e-15,
       "East and north perpendicular");
  near(static_cast<double>(dot(t.east, t.up)), 0, 5e-15,
       "East and up perpendicular");
  near(static_cast<double>(dot(t.north, t.up)), 0, 5e-15,
       "North and up perpendicular");
  vector_near(t.up,
              {observer.x / radius, observer.y / radius, observer.z / radius},
              3e-15, "Tangent up is actual observer radial");
  const std::array<V, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  for (std::size_t i = 0; i < 3; ++i) {
    near_local(
        view.system_axes[i],
        expected_local(view,
                       fixed_from_system(view.rotation.phase_radians, axes[i])),
        "System axis uses independent +Z inverse spin and ENU projection");
    const auto actual_body = body_axis(source.orientation, i);
    near_local(
        view.body_axes[i],
        expected_local(
            view, fixed_from_system(view.rotation.phase_radians, actual_body)),
        "Full actual quaternion body axes survive projection");
    const auto mapped = system_vector_to_moon_local(view, axes[i]);
    vector_near(mapped,
                {view.system_axes[i].east, view.system_axes[i].up,
                 -view.system_axes[i].north},
                3e-15, "Bridge system vector map is east/up/minusnorth");
  }
  vector_near(moon_fixed_vector_to_local(view, t.east), {1, 0, 0}, 5e-15,
              "Fixed east maps Godot+X");
  vector_near(moon_fixed_vector_to_local(view, t.up), {0, 1, 0}, 5e-15,
              "Fixed up maps Godot+Y");
  vector_near(moon_fixed_vector_to_local(view, t.north), {0, 0, -1}, 5e-15,
              "Fixed north maps Godot-Z");
  const auto& r = view.rotation;
  // Finite point-star parallax is evaluated at observer, not moon centre.
  const V toward{r.star_position_fixed.x - observer.x,
                 r.star_position_fixed.y - observer.y,
                 r.star_position_fixed.z - observer.z};
  const auto distance = norm(toward);
  near(r.observer_star_distance_metres, distance, 1e-3,
       "Finite point-star distance binds actual observer");
  const V unit{toward.x / distance, toward.y / distance, toward.z / distance};
  near_local(view.star_direction, expected_local(view, unit),
             "Local illumination uses actual finite point-star direction");
  near(std::hypot(view.star_direction.east, view.star_direction.north,
                  view.star_direction.up),
       1, 5e-15, "Projected star direction remains unit");
}
auto gear_clearance(const test_fixture::MoonContactFixture& fixture) -> void {
  const auto& moon = fixture.moon;
  const RigidBodyWorldContext context{moon};
  const auto original =
      need(encode_rigid_body_state_json(context, fixture.source));
  const auto owner = moon;
  const auto inspect = [&](const RigidBodyState& source) {
    const auto sample = project_moon_gear_clearance(moon, source, true);
    check(sample.has_value(), "All three genuine certified pad planes produce "
                              "a complete gear reading");
    if (!sample)
      throw std::runtime_error("Expected actual Moon gear gap reading");
    check(sample->version == 1 && sample->terrain_version == 1 &&
              sample->frame == source.frame && sample->body == moon.body &&
              sample->craft == source.craft && sample->tick == source.tick &&
              sample->source_checksum ==
                  need(rigid_body_state_checksum(context, source)),
          "Gear reading retains exact typed owner/frame/recipe/current source "
          "clock and checksum");
    const auto query =
        need(MoonContactSnapshot::create(moon, source, true)).query();
    double minimum = std::numeric_limits<double>::infinity(),
           maximum = -minimum;
    for (unsigned i = 0; i < 3; ++i) {
      check(query.supports[i].geometry &&
                query.supports[i].geometry->has_value() &&
                query.supports[i].support &&
                query.supports[i].support->has_value(),
            "Reading uses complete geometry and genuine first-surface support "
            "for every pad");
      const auto& patch = **query.supports[i].geometry;
      minimum = std::min(minimum, patch.minimum_gap_metres);
      maximum = std::max(maximum, patch.maximum_gap_metres);
      // Independent long-double plane distances of the real stored rectangle
      // corners corroborate enclosures; do not repeat the production extent
      // formula or reinterpret a centre sample as whole-footprint clearance.
      const auto& a = patch.triangle.vertices[0];
      const auto n = patch.triangle.outward_normal;
      for (long double sx : {-1.L, 1.L})
        for (long double sz : {-1.L, 1.L}) {
          const long double x = static_cast<long double>(patch.pad_center.x) -
                                a.x + sx * patch.half_width.x +
                                sz * patch.half_length.x;
          const long double y = static_cast<long double>(patch.pad_center.y) -
                                a.y + sx * patch.half_width.y +
                                sz * patch.half_length.y;
          const long double z = static_cast<long double>(patch.pad_center.z) -
                                a.z + sx * patch.half_width.z +
                                sz * patch.half_length.z;
          const long double gap = x * n.x + y * n.y + z * n.z;
          check(static_cast<long double>(sample->minimum_gap_metres) <= gap &&
                    gap <= static_cast<long double>(sample->maximum_gap_metres),
                "Aggregate signed gap encloses every independently evaluated "
                "actual pad corner");
        }
    }
    check(sample->minimum_gap_metres == minimum &&
              sample->maximum_gap_metres == maximum,
          "Reading preserves min/max across all three pad certificates without "
          "averaging");
    check(project_moon_gear_clearance(moon, source, true) == sample,
          "Same source yields identical reading without saved/transient state");
    return *sample;
  };
  const auto compressed = inspect(fixture.source);
  check(compressed.minimum_gap_metres < compressed.maximum_gap_metres &&
            compressed.maximum_gap_metres < 0,
        "Actual compressed pads retain signed negative gaps rather than "
        "zero-clamping");
  const auto to_source = [&](RigidBodyState fixed) {
    fixed = need(canonicalize_rigid_body_state(context, fixed));
    return need(reframe_rigid_body(context, fixed,
                                   {fixture.source.frame, fixed.tick}, moon));
  };
  const auto n = fixture.triangle.outward_normal;
  auto raised = fixture.fixed;
  raised.position_metres = {raised.position_metres.x + .25 * n.x,
                            raised.position_metres.y + .25 * n.y,
                            raised.position_metres.z + .25 * n.z};
  const auto separated = inspect(to_source(raised));
  check(separated.minimum_gap_metres > 0 &&
            separated.maximum_gap_metres > separated.minimum_gap_metres,
        "Actual airborne separated pads show positive plane gaps without "
        "requiring landing readiness");
  auto fast = fixture.fixed;
  fast.linear_velocity_metres_per_second = {-3 * n.x, -3 * n.y, -3 * n.z};
  const auto fast_source = to_source(fast);
  const auto unsafe =
      need(MoonContactSnapshot::create(moon, fast_source, true)).query();
  check(unsafe.assessment && unsafe.assessment->failed_margins != 0 &&
            project_moon_gear_clearance(moon, fast_source, true).has_value(),
        "Certified plane measurement remains distinct from an unsafe touchdown "
        "envelope");
  const auto baseline =
      need(MoonContactSnapshot::create(moon, fixture.source, true)).query();
  const auto& front = **baseline.supports[0].geometry;
  const auto& corner = front.triangle.vertices[0];
  auto edge = baseline.provenance.fixed_query_state;
  edge.position_metres = {
      edge.position_metres.x + corner.x - front.pad_center.x - .05 * n.x,
      edge.position_metres.y + corner.y - front.pad_center.y - .05 * n.y,
      edge.position_metres.z + corner.z - front.pad_center.z - .05 * n.z};
  const auto edge_source = to_source(edge);
  const auto unresolved =
      need(MoonContactSnapshot::create(moon, edge_source, true)).query();
  bool missing{};
  for (unsigned i = 0; i < 3; ++i)
    missing |= !unresolved.supports[i].geometry ||
               !unresolved.supports[i].geometry->has_value() ||
               !unresolved.supports[i].support ||
               !unresolved.supports[i].support->has_value();
  check(missing && !project_moon_gear_clearance(moon, edge_source, true),
        "Footprint on a genuine facet edge returns unavailable, never "
        "partial/zero aggregate");
  auto far = fixture.fixed;
  const auto radius = norm(far.position_metres),
             scale = (moon.radius_metres + 200000.) / radius;
  far.position_metres = {far.position_metres.x * scale,
                         far.position_metres.y * scale,
                         far.position_metres.z * scale};
  check(!project_moon_gear_clearance(moon, to_source(far), true),
        "Valid far flight outside the existing100km pad query range remains "
        "unavailable");
  check(!project_moon_gear_clearance(moon, fixture.source, false),
        "Stowed gear gives no observation");
  check(!project_moon_gear_clearance(moon, fixture.source, true, 99),
        "Unknown terrain recipe gives no observation");
  auto forged = moon;
  ++forged.spin_period_ticks;
  check(!project_moon_gear_clearance(forged, fixture.source, true),
        "Forged source owner refuses before any pad observation");
  auto bad = fixture.source;
  ++bad.frame.body->value;
  check(!project_moon_gear_clearance(moon, bad, true),
        "Foreign body cannot reuse a finite reading");
  bad = fixture.source;
  bad.frame.body->kind = BodyKind::planet;
  check(!project_moon_gear_clearance(moon, bad, true),
        "Planet alias cannot enter Moon readout");
  bad = fixture.source;
  bad.position_metres.x = std::numeric_limits<double>::quiet_NaN();
  check(!project_moon_gear_clearance(moon, bad, true),
        "Nonfinite source gives unavailable");
  check(!project_moon_gear_clearance(moon, fixture.fixed, true),
        "Raw fixed6 cannot masquerade as saved frame5");
  check(need(encode_rigid_body_state_json(context, fixture.source)) ==
                original &&
            moon == owner,
        "All observations and refusals preserve complete source state and "
        "physical owner");
}
auto rise_readout(const test_fixture::MoonContactFixture& fixture) -> void {
  const auto& moon = fixture.moon;
  const RigidBodyWorldContext context{moon};
  const auto properties = need(resolve_craft_frame(fixture.source.craft));
  const long double radius = moon.radius_metres;
  // Independent recipe1 sphere mass/G oracle, rather than the gravity query
  // or the presentation's fixed-frame tangent/body-axis calculation.
  const long double mu =
      moon.gravitational_constant_si * moon.density_kilograms_per_cubic_metre *
      (4.L / 3.L) * std::numbers::pi_v<long double> * radius * radius * radius;
  std::array<double, 2> upright{};
  std::size_t altitude_index{};
  for (const double altitude : {1000., 100000.}) {
    for (const double angle :
         {0., std::numbers::pi / 3, std::numbers::pi / 2, std::numbers::pi}) {
      // Explicit saved-space instrument fixture, never a flown approach or
      // supported landing claim. Preserve the genuine descriptor/frame/tick.
      auto body = fixture.source;
      body.position_metres = {0, moon.radius_metres + altitude, 0};
      body.orientation = {std::cos(angle / 2), std::sin(angle / 2), 0, 0};
      body = need(canonicalize_rigid_body_state(context, body));
      FreedomFlightSaveDocument document{
          make_freedom_new_game_document(Seed{42}), body, {}};
      document.origin.state.tick = body.tick;
      document.model.physical_catalog = 2;
      document.model.physical_ephemeris = 2;
      document.moon =
          FreedomMoonWorldSelection{1, {context.system.id, moon.body}};
      auto session = need(NativeFreedomFlightSession::open(
          need(native_select_save_document(NativeSaveDocument{document}))));
      const auto saved =
          need(encode_native_save_document_json(need(session.save_document())));
      const auto view = project_saved_moon_flight(session);
      const auto margin = rated_rise_margin(session, view);
      const auto& actual = session.document().flight;
      const auto thrust_up = body_axis(actual.orientation, 1);
      const auto p = actual.position_metres;
      const long double distance_squared = dot(p, p);
      const long double distance = std::sqrt(distance_squared);
      const long double radial_up = dot(thrust_up, p) / distance;
      const long double thrust =
          static_cast<long double>(
              properties.properties.positive_force_newtons[1]) /
          properties.properties.dry_mass_kg;
      const double expected =
          static_cast<double>(thrust * radial_up - mu / distance_squared);
      check(margin.has_value(),
            "Admitted historical Moon flight has a rated RISE estimate");
      if (!margin) throw std::runtime_error("Actual Moon RISE unavailable");
      near(*margin, expected, 1e-11,
           "Moon RISE uses independent quaternion/radial and physical gravity "
           "oracle");
      near(static_cast<double>(radial_up), std::cos(angle), 2e-15,
           "Upright tilted sideways and inverted thrust retain radial signs");
      if (angle == 0) upright[altitude_index] = *margin;
      if (angle == std::numbers::pi / 2 || angle == std::numbers::pi)
        check(*margin < 0,
              "Sideways/inverted full RISE cannot become upward lift");
      for (const double bad :
           {std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(), -1.01, 1.01}) {
        auto malformed = view;
        malformed.body_axes[1].up = bad;
        check(!rated_rise_margin(session, malformed),
              "Nonfinite or nonunit Moon radial component is unavailable");
      }
      {
        auto malformed = view;
        ++malformed.version;
        check(!rated_rise_margin(session, malformed),
              "Unsupported Moon projection version is unavailable");
      }
      {
        auto malformed = view;
        ++malformed.moon.radius_metres;
        check(!rated_rise_margin(session, malformed),
              "Altered physical Moon descriptor cannot supply thrust reading");
      }
      {
        auto malformed = view;
        ++malformed.moon.body.value;
        check(!rated_rise_margin(session, malformed),
              "Foreign Moon owner cannot supply thrust reading");
      }
      {
        auto malformed = view;
        ++malformed.body.tick;
        check(!rated_rise_margin(session, malformed),
              "Stale Moon projection clock is unavailable");
      }
      {
        auto malformed = view;
        malformed.body.position_metres.x += 1;
        check(!rated_rise_margin(session, malformed),
              "Changed source pose cannot reuse a Moon thrust reading");
      }
      {
        auto malformed = view;
        malformed.body.position_metres.x =
            std::numeric_limits<double>::quiet_NaN();
        check(!rated_rise_margin(session, malformed),
              "Nonfinite source projection is unavailable without throwing");
      }
      {
        auto malformed = view;
        malformed.surface_observer = true;
        check(!rated_rise_margin(session, malformed),
              "Foot-observer projection cannot describe craft radial attitude");
      }
      {
        auto malformed = view;
        malformed.body.frame = view.fixed_frame;
        check(!rated_rise_margin(session, malformed),
              "A raw Moon-fixed state cannot substitute for relative flight");
      }
      for (unsigned repeat = 0; repeat < 3; ++repeat)
        check(rated_rise_margin(session, view) == margin,
              "Repeated Moon thrust reads are exactly stable");
      check(need(encode_native_save_document_json(
                need(session.save_document()))) == saved,
            "All valid/refused Moon thrust reads preserve complete Save and "
            "clock");
    }
    ++altitude_index;
  }
  check(upright[1] > upright[0],
        "Greater actual Moon altitude reduces gravity in the RISE estimate");
  const auto legacy =
      need(NativeFreedomFlightSession::open(need(native_new_game(Seed{42}))));
  const auto view = project_saved_moon_flight(moon, fixture.source);
  const auto saved =
      need(encode_native_save_document_json(need(legacy.save_document())));
  check(
      !rated_rise_margin(legacy, view),
      "Planetary session cannot acquire Moon capability through a projection");
  check(need(encode_native_save_document_json(need(legacy.save_document()))) ==
            saved,
        "Unsupported planetary read preserves its complete Save");
}
auto run() -> void {
  const auto fixture = test_fixture::make_moon_contact_fixture();
  gear_clearance(fixture);
  rise_readout(fixture);
  const auto& moon = fixture.moon;
  const RigidBodyWorldContext context{moon};
  const auto original =
      need(encode_rigid_body_state_json(context, fixture.source));
  const auto source_copy = moon;
  const auto view = project_saved_moon_flight(moon, fixture.source);
  verify(view, moon, fixture.source);
  check(!view.surface_observer, "Seated/free flight observes actual craft COM");
  const auto expected = fixed_from_system(view.rotation.phase_radians,
                                          fixture.source.position_metres);
  vector_near(
      {view.tangent.origin.x, view.tangent.origin.y, view.tangent.origin.z},
      expected, 2e-8,
      "Observer fixed position agrees with independent actual source spin");
  check(need(encode_rigid_body_state_json(context, fixture.source)) ==
                original &&
            moon == source_copy,
        "Projection does not mutate source or owned source catalog");
  auto changed = fixture.source;
  changed.tick += moon.spin_period_ticks / 4;
  changed = need(canonicalize_rigid_body_state(context, changed));
  const auto spun = project_saved_moon_flight(moon, changed);
  verify(spun, moon, changed);
  check(spun.rotation.phase_radians != view.rotation.phase_radians &&
            spun.system_axes != view.system_axes,
        "Actual spin tick changes world basis, not authoritative inertial "
        "attitude");
  for (const double sign : {-1., 1.}) {
    auto fixed = fixture.fixed;
    fixed.position_metres = {0, 0, sign * (moon.radius_metres + 100)};
    const auto state =
        need(reframe_rigid_body(context, fixed,
                                {{RigidFrameKind::body_relative_inertial,
                                  context.system.id,
                                  {},
                                  {},
                                  moon.body},
                                 fixed.tick},
                                moon));
    const auto pole = project_saved_moon_flight(moon, state);
    verify(pole, moon, state);
    vector_near(pole.tangent.east, {0, 1, 0}, 0,
                "Exact polar east convention is deterministic");
    vector_near(pole.tangent.north, {-sign, 0, 0}, 0,
                "Exact polar north convention preserves handedness");
  }
  const auto hash = need(rigid_body_state_checksum(context, fixture.source));
  const auto anchor =
      need(prepare_moon_landed_craft(moon, fixture.source, true, hash));
  auto terrain =
      need(MoonSurfaceWalkTerrain::create(moon, anchor, fixture.source.tick));
  const auto actor = need(terrain.entry());
  const auto outside = project_saved_moon_flight(moon, fixture.source, actor);
  verify(outside, moon, fixture.source);
  check(outside.surface_observer &&
            outside.tangent.origin ==
                MoonFixedPositionMetres{actor.foot_position_metres.x,
                                        actor.foot_position_metres.y,
                                        actor.foot_position_metres.z},
        "Actual supported walker foot replaces observer once without moving "
        "craft");
  check(outside.body == view.body &&
            outside.tangent.origin != view.tangent.origin,
        "Outside eye origin does not replace source craft pose/attitude");
  auto owner = moon;
  ++owner.radius_metres;
  rejects([&] { (void)project_saved_moon_flight(owner, fixture.source); },
          "Tampered owner rejects before projection math");
  auto bad = fixture.source;
  bad.frame.body->kind = BodyKind::planet;
  rejects([&] { (void)project_saved_moon_flight(moon, bad); },
          "Moon cannot be relabelled as Planet BodyId");
  bad = fixture.source;
  ++bad.frame.body->value;
  rejects([&] { (void)project_saved_moon_flight(moon, bad); },
          "Foreign moon refuses");
  bad = fixture.fixed;
  rejects([&] { (void)project_saved_moon_flight(moon, bad); },
          "Raw fixed6 cannot pretend canonical saved relative5");
  bad = fixture.source;
  bad.position_metres.y = std::numeric_limits<double>::quiet_NaN();
  rejects([&] { (void)project_saved_moon_flight(moon, bad); },
          "Nonfinite source refuses");
  auto bad_actor = actor;
  ++bad_actor.frame.body->value;
  rejects(
      [&] { (void)project_saved_moon_flight(moon, fixture.source, bad_actor); },
      "Foreign observer actor cannot cross body owners");
  bad_actor = actor;
  bad_actor.foot_position_metres = {};
  rejects(
      [&] { (void)project_saved_moon_flight(moon, fixture.source, bad_actor); },
      "Observer at moon centre refuses");
  bad_actor = actor;
  bad_actor.foot_position_metres.x = std::numeric_limits<double>::infinity();
  rejects(
      [&] { (void)project_saved_moon_flight(moon, fixture.source, bad_actor); },
      "Nonfinite observer refuses");
  rejects(
      [&] {
        (void)system_vector_to_moon_local(
            view, {0, std::numeric_limits<double>::infinity(), 0});
      },
      "Nonfinite bridge vector refuses");
  // Actual bare format42 session exercises the public session projection path.
  FreedomFlightSaveDocument document{
      make_freedom_new_game_document(Seed{42}), fixture.source, {}};
  document.origin.state.tick = fixture.source.tick;
  document.model.physical_catalog = 2;
  document.model.physical_ephemeris = 2;
  document.moon = FreedomMoonWorldSelection{1, {context.system.id, moon.body}};
  auto session = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(NativeSaveDocument{document}))));
  const auto saved =
      need(encode_native_save_document_json(need(session.save_document())));
  const auto actual = project_saved_moon_flight(session);
  check(actual.body == view.body && actual.moon == view.moon &&
            actual.rotation == view.rotation && actual.tangent == view.tangent,
        "Session overload consumes same actual canonical state and owner");
  check(need(encode_native_save_document_json(need(session.save_document()))) ==
            saved,
        "Presentation leaves complete saved session unchanged");
  const auto planet = need(native_new_game(Seed{42}));
  const auto legacy = need(NativeFreedomFlightSession::open(planet));
  rejects([&] { (void)project_saved_moon_flight(legacy); },
          "Existing planet session cannot be carried by Moon projection");
}
} // namespace
int main() {
  try {
    run();
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << e.what() << '\n';
  }
  std::cout << checks << " saved moon projection checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
