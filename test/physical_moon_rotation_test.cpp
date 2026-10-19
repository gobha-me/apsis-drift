#include "apsis_drift/physical_moon_rotation.hpp"
#include "apsis_drift/secondary_observations.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <string_view>

namespace {
using namespace apsis_drift;
int failures{};
auto check(bool condition, std::string_view message) -> void {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <typename T, typename E>
auto required(const std::expected<T, E>& value) -> T {
  if (!value) {
    std::cerr << "FAIL: physical moon rotation fixture refused\n";
    std::exit(1);
  }
  return *value;
}
auto close(double a, double b, double tolerance = 1e-12) -> bool {
  return std::abs(a - b) <= tolerance;
}
auto length(RigidVector3 v) -> double {
  return std::hypot(v.x, v.y, v.z);
}
auto planar(RigidVector3 v, double angle) -> RigidVector3 {
  return {std::cos(angle) * v.x - std::sin(angle) * v.y,
          std::sin(angle) * v.x + std::cos(angle) * v.y, v.z};
}
auto contract(std::uint32_t version) -> void {
  const auto system =
      required(generate_physical_origin_system(Seed{42}, version));
  const auto catalog = required(generate_secondary_body_catalog(system));
  const auto moon = required(
      generate_physical_moon_descriptor(catalog, catalog.bodies[0].node.id));
  const auto before = moon;
  const auto hierarchy = required(make_body_hierarchy(catalog));
  const RigidVector3 observer{static_cast<double>(moon.radius_metres), 25.0,
                              -9.0};
  for (const auto tick :
       {SimulationTick{0}, SimulationTick{1}, SimulationTick{1234567},
        std::numeric_limits<SimulationTick>::max() - 1}) {
    const auto geometry =
        required(resolve_physical_moon_rotation(moon, tick, observer));
    const auto ephemerides =
        required(resolve_body_ephemerides(hierarchy, {tick, 0.0}));
    const auto actual =
        std::ranges::find(ephemerides, moon.body, &BodyEphemeris::body);
    check(actual != ephemerides.end(),
          "Actual moon appears in authenticated hierarchy");
    if (actual == ephemerides.end()) return;
    check(geometry.body_position == actual->position &&
              geometry.body_velocity == actual->velocity &&
              geometry.star == system.catalog.star.id,
          "Moon rotation uses complete parent-inclusive same-tick centre and "
          "real system star");
    const auto optical =
        required(observe_secondary_bodies(catalog, {}, {tick, 0.0}));
    check(geometry.phase_radians == optical[0].spin_radians &&
              geometry.cycle_tick == tick % moon.spin_period_ticks &&
              geometry.tick == tick,
          "Physical and optical moon retain original full-turn epoch word and "
          "modulo clock");
    const auto& q = geometry.fixed_to_system;
    check(close(((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z, 1.0) &&
              q.x == 0 && q.y == 0 && !std::signbit(q.x) && !std::signbit(q.y),
          "Moon orientation is canonical unit system-Z rotation");
    const auto east = planar({1, 0, 0}, geometry.phase_radians);
    check(close(q.w * q.w - q.z * q.z, east.x) && close(2 * q.w * q.z, east.y),
          "Independent planar orientation agrees with quaternion without "
          "orbit-plane tilt");
    check(geometry.angular_velocity_radians_per_second.x == 0 &&
              geometry.angular_velocity_radians_per_second.y == 0 &&
              close(geometry.angular_velocity_radians_per_second.z *
                        moon.spin_period_ticks / kSimulationHz,
                    2 * std::numbers::pi),
          "Fixed moon spin is sidereal and independent of orbital parent "
          "velocity");
    const auto star =
        planar({-actual->position.x, -actual->position.y, -actual->position.z},
               -geometry.phase_radians);
    const double scale =
        std::max({1.0, std::abs(star.x), std::abs(star.y), std::abs(star.z)});
    check(close(star.x, geometry.star_position_fixed.x, scale * 2e-15) &&
              close(star.y, geometry.star_position_fixed.y, scale * 2e-15) &&
              star.z == geometry.star_position_fixed.z,
          "Independent rotation of finite star offset matches moon-fixed "
          "geometry");
    const RigidVector3 difference{geometry.star_position_fixed.x - observer.x,
                                  geometry.star_position_fixed.y - observer.y,
                                  geometry.star_position_fixed.z - observer.z};
    const double distance = length(difference);
    check(
        geometry.observer_position_fixed == observer &&
            close(geometry.observer_star_distance_metres, distance,
                  distance * 2e-15) &&
            close(geometry.observer_to_star_fixed.x, difference.x / distance) &&
            close(geometry.observer_to_star_fixed.y, difference.y / distance) &&
            close(geometry.observer_to_star_fixed.z, difference.z / distance),
        "Illumination uses finite-distance observer offset rather than "
        "body-centre direction");
    check(close(length(geometry.observer_to_star_fixed), 1.0) &&
              close(length({geometry.observer_to_star_system.x,
                            geometry.observer_to_star_system.y,
                            geometry.observer_to_star_system.z}),
                    1.0),
          "Both light directions are finite units");
    const auto repeated = resolve_physical_moon_rotation(moon, tick, observer);
    check(repeated && *repeated == geometry && moon == before,
          "Repeated moon rotation is deterministic and leaves source "
          "descriptor untouched");
  }
  const auto first = required(resolve_physical_moon_rotation(moon, 0));
  const auto period =
      required(resolve_physical_moon_rotation(moon, moon.spin_period_ticks));
  check(first.fixed_to_system == period.fixed_to_system &&
            first.phase_radians == period.phase_radians &&
            first.angular_velocity_radians_per_second ==
                period.angular_velocity_radians_per_second,
        "One original spin period repeats attitude exactly without freezing "
        "orbital centre");
  check(first.body_position != period.body_position,
        "Repeated spin does not reset the orbital clock");
  const auto refuse = [&](const PhysicalMoonDescriptor& owner,
                          SimulationTick tick, RigidVector3 position,
                          PhysicalMoonRotationError error) {
    const auto result = resolve_physical_moon_rotation(owner, tick, position);
    check(!result && result.error() == error,
          "Invalid moon query refuses with the owning provider's error");
  };
  refuse(moon, std::numeric_limits<SimulationTick>::max(), {},
         PhysicalMoonRotationError::invalid_tick);
  for (const auto bad :
       {std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(), 1e12 + 1, -1e12 - 1})
    for (const auto v : {RigidVector3{bad, 0, 0}, RigidVector3{0, bad, 0},
                         RigidVector3{0, 0, bad}})
      refuse(moon, 0, v, PhysicalMoonRotationError::invalid_observer);
  check(
      resolve_physical_moon_rotation(moon, 0, {1e12, -1e12, 1e12}).has_value(),
      "Inclusive finite observer boundary admitted");
  const auto signed_zero =
      required(resolve_physical_moon_rotation(moon, 0, {-0.0, -0.0, -0.0}));
  check(!std::signbit(signed_zero.observer_position_fixed.x) &&
            !std::signbit(signed_zero.observer_position_fixed.y) &&
            !std::signbit(signed_zero.observer_position_fixed.z),
        "Observer zeros canonicalized in derived geometry only");
  refuse(moon, 0, first.star_position_fixed,
         PhysicalMoonRotationError::observer_at_star);
  auto forged = moon;
  ++forged.version;
  refuse(forged, 0, {}, PhysicalMoonRotationError::unsupported_version);
  forged.version = moon.version;
  ++forged.spin_period_ticks;
  refuse(forged, 0, {}, PhysicalMoonRotationError::invalid_owner);
  forged.spin_period_ticks = moon.spin_period_ticks;
  forged.spin_pole_system.x = .1;
  refuse(forged, 0, {}, PhysicalMoonRotationError::invalid_owner);
}
} // namespace

auto main() -> int {
  contract(1);
  contract(2);
  std::cout << "Physical moon rotation: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
