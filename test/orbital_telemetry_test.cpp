#include "apsis_drift/orbital_telemetry.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string_view>

namespace {
using namespace apsis_drift;
using V = RigidVector3;
using Classification = OrbitClassification;
int failures{};
constexpr OrbitalTelemetryRecipe policy{
    1, 100000}; // fixture classification boundary
auto check(bool condition, std::string_view message) -> void {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}
template <typename T, typename E>
auto required(std::expected<T, E> result,
              std::source_location here = std::source_location::current())
    -> T {
  if (!result) {
    std::cerr << "required fixture at line " << here.line() << '\n';
    throw std::runtime_error("required orbital fixture failed");
  }
  return *result;
}
auto dot(V a, V b) -> double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
auto scale(V a, double b) -> V {
  return {a.x * b, a.y * b, a.z * b};
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto length(V a) -> double {
  return std::sqrt(dot(a, a));
}
auto near(double a, double b, double tolerance) -> bool {
  return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tolerance;
}
auto close(V a, V b, double tolerance) -> bool {
  return length(sub(a, b)) <= tolerance;
}
auto rotate(RigidOrientation q, V v) -> V {
  // Independent matrix expansion of body-fixed -> nonrotating orientation.
  const double norm = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
  return {(1 - 2 * (q.y * q.y + q.z * q.z) / norm) * v.x +
              2 * (q.x * q.y - q.w * q.z) / norm * v.y +
              2 * (q.x * q.z + q.w * q.y) / norm * v.z,
          2 * (q.x * q.y + q.w * q.z) / norm * v.x +
              (1 - 2 * (q.x * q.x + q.z * q.z) / norm) * v.y +
              2 * (q.y * q.z - q.w * q.x) / norm * v.z,
          2 * (q.x * q.z - q.w * q.y) / norm * v.x +
              2 * (q.y * q.z + q.w * q.x) / norm * v.y +
              (1 - 2 * (q.x * q.x + q.y * q.y) / norm) * v.z};
}
struct Fixture {
  Fixture(Seed seed = Seed{42}, bool origin = true)
      : system(required(origin ? generate_physical_origin_system(seed)
                               : generate_physical_local_system(seed))),
        context(system), planet(system.catalog.planets.front().descriptor),
        rotation(required(generate_planet_rotation_recipe(system, planet.id))) {
  }
  PhysicalLocalSystem system;
  RigidBodyWorldContext context;
  const PlanetDescriptor& planet;
  PhysicalPlanetRotationRecipe rotation;
  auto radius() const -> double { return planet.radius.value * 1000.0; }
  auto mu() const -> double {
    return 9.80665 * planet.surface_gravity.value / 1000.0 * radius() *
           radius();
  }
  auto state() const -> RigidBodyState {
    RigidBodyState s;
    s.frame = {RigidFrameKind::planet_relative_inertial,
               system.catalog.id,
               planet.id,
               {}};
    s.position_metres = {radius() + 500000, 0, 0};
    s.tick = 1234567;
    return s;
  }
  auto query(const RigidBodyState& s,
             OrbitalTelemetryRecipe recipe = policy) const -> OrbitalTelemetry {
    const auto result =
        evaluate_orbital_telemetry(context, s, rotation, recipe);
    if (!result) {
      std::cerr << "query error: " << static_cast<unsigned>(result.error().code)
                << " radius " << length(s.position_metres) << " tick " << s.tick
                << '\n';
      if (result.error().gravity)
        std::cerr << "gravity error: "
                  << static_cast<unsigned>(result.error().gravity->code)
                  << '\n';
      if (result.error().rotation)
        std::cerr << "rotation error: "
                  << static_cast<unsigned>(*result.error().rotation) << '\n';
    }
    return required(result);
  }
};
auto check_hydration(const Fixture& f, const RigidBodyState& source,
                     const OrbitalTelemetry& observation) -> void {
  const auto json = required(encode_rigid_body_state_json(f.context, source));
  const auto restored = required(decode_rigid_body_state_json(f.context, json));
  check(f.query(restored) == observation &&
            required(encode_rigid_body_state_json(f.context, source)) == json,
        "all query fields reproduce with the selected recipes after exact v3 "
        "hydration");
}
auto elements(const Fixture& f) -> void {
  auto s = f.state();
  const double r = s.position_metres.x, speed = std::sqrt(f.mu() / r);
  for (const double sign : {-1.0, 1.0}) {
    s.linear_velocity_metres_per_second = {0, 0, sign * speed};
    const auto o = f.query(s);
    check_hydration(f, s, o);
    check(o.bound && !o.near_parabolic &&
              o.classification == Classification::stable &&
              near(o.eccentricity, 0, 1e-14) &&
              near(o.periapsis_radius_metres, r, 1e-7) &&
              o.apoapsis_radius_metres &&
              near(*o.apoapsis_radius_metres, r, 1e-7) &&
              near(o.inertial_speed_metres_per_second, speed, 1e-10) &&
              o.radial_rate_metres_per_second == 0 &&
              close(o.specific_angular_momentum_metres_squared_per_second,
                    {0, -sign * r * speed, 0}, .001) &&
              near(o.specific_energy_metres_squared_per_second_squared,
                   -f.mu() / (2 * r), 1e-7),
          "circular prograde/retrograde matches independent orbital elements");
    check(!o.synchronous_orbit,
          "generic circular motion is not automatically synchronous");
  }
  for (const double peri :
       {f.radius() - 1000, f.radius() + 50000, f.radius() + 250000}) {
    const double apo = f.radius() + 1000000;
    const double a = (peri + apo) / 2;
    s.position_metres = {apo, 0, 0};
    s.linear_velocity_metres_per_second = {
        0, 0, std::sqrt(f.mu() * (2 / apo - 1 / a))};
    const auto o = f.query(s);
    check_hydration(f, s, o);
    const auto expected =
        peri <= f.radius() ? Classification::impact
        : peri <= f.radius() + policy.space_boundary_altitude_metres
            ? Classification::decaying
            : Classification::stable;
    check(o.bound && o.classification == expected &&
              near(o.eccentricity, (apo - peri) / (apo + peri), 1e-14) &&
              near(o.periapsis_radius_metres, peri, 1e-6) &&
              o.apoapsis_radius_metres &&
              near(*o.apoapsis_radius_metres, apo, 1e-6),
          "eccentric periapsis independently distinguishes surface impact, "
          "boundary intersection and clear orbit");
    const auto vacuum = f.query(s, {1, 0});
    check(vacuum.eccentricity == o.eccentricity &&
              vacuum.periapsis_radius_metres == o.periapsis_radius_metres &&
              vacuum.specific_energy_metres_squared_per_second_squared ==
                  o.specific_energy_metres_squared_per_second_squared,
          "classification policy never changes orbital physics");
  }
  s = f.state();
  auto o = f.query(s);
  check_hydration(f, s, o);
  check(o.bound && o.classification == Classification::impact &&
            o.inertial_speed_metres_per_second == 0 &&
            o.radial_rate_metres_per_second == 0 && !o.synchronous_orbit &&
            o.periapsis_radius_metres == 0 && o.apoapsis_radius_metres &&
            near(*o.apoapsis_radius_metres, r, 1e-7),
        "stationary zero radial rate means fall, not orbit or hover");
  s.linear_velocity_metres_per_second = {-100, 0, 0};
  o = f.query(s);
  check_hydration(f, s, o);
  check(near(o.radial_rate_metres_per_second, -100, 1e-12) &&
            o.classification == Classification::impact,
        "radial descent retains its signed rate and impact semantics");
  s = f.state();
  s.linear_velocity_metres_per_second = {0, 0, std::sqrt(2 * f.mu() / r)};
  o = f.query(s);
  check(o.near_parabolic && !o.bound && !o.apoapsis_radius_metres &&
            o.classification == Classification::escape,
        "near-parabolic energy has explicit unbounded semantics without "
        "infinity");
  for (const double direction : {-1.0, 1.0}) {
    s = f.state();
    s.linear_velocity_metres_per_second = {
        direction * std::sqrt(3 * f.mu() / r), 0, 0};
    o = f.query(s);
    check(!o.bound && !o.apoapsis_radius_metres &&
              o.classification == (direction < 0 ? Classification::impact
                                                 : Classification::escape) &&
              o.periapsis_radius_metres == 0,
          "inbound future collision differs from an outbound past hyperbolic "
          "periapsis");
  }
  // Still bound outside the documented near-parabolic band, but with an
  // apoapsis beyond the radius-reporting bound. Preserve bound semantics.
  s = f.state();
  const double energy = -f.mu() / (8 * kMaximumOrbitalElementRadiusMetres);
  s.linear_velocity_metres_per_second = {0, 0,
                                         std::sqrt(2 * (f.mu() / r + energy))};
  o = f.query(s);
  check(
      o.bound && !o.near_parabolic && !o.apoapsis_radius_metres &&
          o.classification == Classification::stable,
      "excessive bound apoapsis is absent instead of clamped or made unbound");
}
auto surface_and_sync(const Fixture& f) -> void {
  for (const auto tick :
       {SimulationTick{0}, SimulationTick{1234567},
        f.rotation.rotation.period_ticks - 1, f.rotation.rotation.period_ticks,
        std::numeric_limits<SimulationTick>::max() - 1}) {
    auto s = f.state();
    s.tick = tick;
    const auto geometry =
        required(resolve_planet_rotation(f.system, f.rotation, tick)).geometry;
    const auto& w = geometry.angular_velocity_radians_per_second;
    const V omega{w.x, w.y, w.z};
    const double synchronous = std::cbrt(f.mu() / dot(omega, omega));
    s.position_metres = rotate(geometry.fixed_to_system, {synchronous, 0, 0});
    s.linear_velocity_metres_per_second = cross(omega, s.position_metres);
    const auto o = f.query(s);
    check(o.synchronous_orbit && o.classification == Classification::stable &&
              o.bound && near(o.synchronous_radius_metres, synchronous, 1e-7) &&
              o.surface_relative_speed_metres_per_second == 0 &&
              near(o.inertial_speed_metres_per_second,
                   std::sqrt(f.mu() / synchronous), 1e-8) &&
              std::abs(o.radial_rate_metres_per_second) < 1e-9,
          "equatorial prograde circular velocity at generated synchronous "
          "radius qualifies");
    auto fixed = s;
    fixed.frame.kind = RigidFrameKind::planet_fixed;
    fixed.position_metres = {synchronous, 0, 0};
    fixed.orientation = {};
    fixed.linear_velocity_metres_per_second = {};
    fixed.angular_velocity_radians_per_second = {};
    const auto inertial = required(
        reframe_rigid_body(f.context, fixed, {s.frame, s.tick}, f.rotation));
    const auto transformed = f.query(inertial);
    check(transformed.synchronous_orbit &&
              transformed.surface_relative_speed_metres_per_second < 1e-8 &&
              close(inertial.linear_velocity_metres_per_second,
                    cross(omega, inertial.position_metres), 1e-8),
          "fixed/nonrotating handoff and independent Omega-cross-r surface "
          "velocity agree");
    auto wrong = s;
    wrong.linear_velocity_metres_per_second = {};
    check(!f.query(wrong).synchronous_orbit &&
              f.query(wrong).classification == Classification::impact,
          "zero inertial velocity at synchronous radius still falls");
    wrong = s;
    wrong.linear_velocity_metres_per_second =
        scale(s.linear_velocity_metres_per_second, -1);
    check(!f.query(wrong).synchronous_orbit &&
              f.query(wrong).surface_relative_speed_metres_per_second >
                  o.inertial_speed_metres_per_second,
          "retrograde circular orbit is not surface-stationary synchronous");
    wrong = s;
    wrong.position_metres = scale(s.position_metres, .5);
    wrong.linear_velocity_metres_per_second =
        cross(omega, wrong.position_metres);
    check(f.query(wrong).surface_relative_speed_metres_per_second == 0 &&
              !f.query(wrong).synchronous_orbit,
          "surface-stationary hovering at wrong radius cannot masquerade as "
          "synchronous orbit");
    wrong = s;
    wrong.linear_velocity_metres_per_second =
        scale(s.linear_velocity_metres_per_second, .9);
    check(!f.query(wrong).synchronous_orbit,
          "eccentric tangential speed is not synchronous");
    wrong = s;
    wrong.position_metres =
        rotate(geometry.fixed_to_system, {0, 0, synchronous});
    wrong.linear_velocity_metres_per_second = rotate(
        geometry.fixed_to_system, {0, std::sqrt(f.mu() / synchronous), 0});
    check(!f.query(wrong).synchronous_orbit,
          "inclined circular period match is not the equatorial "
          "surface-stationary contract");
    // Exact same-query reproduction at a wrap/extreme tick, independent of any
    // presentation time/fraction or observer/camera. Projection does not store
    // telemetry or policy; the selected recipes are passed again explicitly.
    const auto json = required(encode_rigid_body_state_json(f.context, s));
    const auto restored =
        required(decode_rigid_body_state_json(f.context, json));
    const auto again = f.query(restored);
    check(again.inertial_speed_metres_per_second ==
                  o.inertial_speed_metres_per_second &&
              again.surface_relative_velocity_metres_per_second ==
                  o.surface_relative_velocity_metres_per_second &&
              again.radial_rate_metres_per_second ==
                  o.radial_rate_metres_per_second &&
              again.specific_energy_metres_squared_per_second_squared ==
                  o.specific_energy_metres_squared_per_second_squared &&
              again.eccentricity == o.eccentricity &&
              again.periapsis_radius_metres == o.periapsis_radius_metres &&
              again.apoapsis_radius_metres == o.apoapsis_radius_metres &&
              again.synchronous_radius_metres == o.synchronous_radius_metres &&
              again.classification == o.classification &&
              again.synchronous_orbit == o.synchronous_orbit &&
              required(encode_rigid_body_state_json(f.context, s)) == json,
          "same recipes and hydrated state reproduce query exactly without "
          "replacing source");
  }
}
auto invalid(const Fixture& f) -> void {
  auto s = f.state();
  for (const double edge : {-1.0, -0.0, std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN(),
                            kRigidBodyMaximumPositionMetres * 2}) {
    const auto result =
        evaluate_orbital_telemetry(f.context, s, f.rotation, {1, edge});
    check(!result &&
              result.error().code == OrbitalTelemetryErrorCode::invalid_policy,
          "non-finite, signed-zero and excessive policy boundary refuses");
  }
  check(!evaluate_orbital_telemetry(f.context, s, f.rotation, {0, 0}) &&
            !evaluate_orbital_telemetry(f.context, s, f.rotation, {2, 0}) &&
            !evaluate_orbital_telemetry(f.context, s, f.rotation, policy, {0}),
        "telemetry and selected dynamics versions refuse independently");
  auto rotation = f.rotation;
  rotation.rotation.planet.value ^= 1;
  check(!evaluate_orbital_telemetry(f.context, s, rotation, policy),
        "wrong rotation planet refuses");
  rotation = f.rotation;
  rotation.owner_version = 0;
  check(!evaluate_orbital_telemetry(f.context, s, rotation, policy),
        "wrong rotation owner version refuses");
  rotation = f.rotation;
  rotation.rotation.epoch_phase_tick ^= 1;
  check(!evaluate_orbital_telemetry(f.context, s, rotation, policy),
        "forged spin recipe refuses");
  rotation = f.rotation;
  rotation.origin_universe_seed = Seed{999};
  check(!evaluate_orbital_telemetry(f.context, s, rotation, policy),
        "numeric IDs cannot hide a mismatched origin owner");
  const RigidBodyWorldContext unwrapped{f.system.catalog};
  check(!evaluate_orbital_telemetry(unwrapped, s, f.rotation, policy),
        "legacy namesake cannot own physical orbital telemetry");
  for (const auto kind :
       {RigidFrameKind::system_inertial, RigidFrameKind::planet_fixed,
        RigidFrameKind::station_relative_inertial}) {
    auto bad = s;
    bad.frame.kind = kind;
    check(!evaluate_orbital_telemetry(f.context, bad, f.rotation, policy),
          "wrong coordinate frame refuses");
  }
  for (const double value : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(), -0.0}) {
    for (std::size_t i = 0; i < 13; ++i) {
      auto bad = s;
      const std::array<double*, 13> components{
          &bad.position_metres.x,
          &bad.position_metres.y,
          &bad.position_metres.z,
          &bad.orientation.w,
          &bad.orientation.x,
          &bad.orientation.y,
          &bad.orientation.z,
          &bad.linear_velocity_metres_per_second.x,
          &bad.linear_velocity_metres_per_second.y,
          &bad.linear_velocity_metres_per_second.z,
          &bad.angular_velocity_radians_per_second.x,
          &bad.angular_velocity_radians_per_second.y,
          &bad.angular_velocity_radians_per_second.z};
      *components[i] = value;
      check(!evaluate_orbital_telemetry(f.context, bad, f.rotation, policy),
            "every malformed state scalar refuses");
    }
  }
  auto bad = s;
  bad.tick = std::numeric_limits<SimulationTick>::max();
  check(!evaluate_orbital_telemetry(f.context, bad, f.rotation, policy),
        "invalid saved clock refuses");
  bad = s;
  bad.position_metres = {};
  check(!evaluate_orbital_telemetry(f.context, bad, f.rotation, policy),
        "singular position refuses before element arithmetic");
  bad = s;
  bad.frame.planet = PlanetId{0};
  check(!evaluate_orbital_telemetry(f.context, bad, f.rotation, policy),
        "unknown named body refuses");
  bad = s;
  bad.position_metres = {kRigidBodyMaximumPositionMetres,
                         kRigidBodyMaximumPositionMetres,
                         kRigidBodyMaximumPositionMetres};
  bad.linear_velocity_metres_per_second = {
      kRigidBodyMaximumVelocityMetresPerSecond,
      -kRigidBodyMaximumVelocityMetresPerSecond, 0};
  const auto excessive = f.query(bad);
  check(std::isfinite(excessive.eccentricity) &&
            std::isfinite(
                excessive.specific_energy_metres_squared_per_second_squared) &&
            !excessive.bound && !excessive.apoapsis_radius_metres,
        "maximum supported finite state yields bounded semantic telemetry");
}
} // namespace
auto main() -> int {
  try {
    const Fixture f;
    invalid(f);
    for (const auto seed :
         {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}}) {
      for (const bool origin : {false, true}) {
        const Fixture fixture{seed, origin};
        elements(fixture);
        surface_and_sync(fixture);
      }
    }
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    ++failures;
  }
  std::cout << "orbital telemetry: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
