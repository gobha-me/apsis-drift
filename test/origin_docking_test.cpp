#include "apsis_drift/origin_docking.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>

namespace {
using namespace apsis_drift;
int failures{};
auto check(bool condition, std::string_view message) -> void {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
template <class T, class E> auto require(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("docking fixture refused");
  return *value;
}
struct Fixture {
  explicit Fixture(Seed seed = Seed{42})
      : system(require(generate_physical_origin_system(seed))),
        station(generate_origin_station(seed)),
        geometry(require(origin_station_geometry(station))),
        context(system, &station) {}
  PhysicalLocalSystem system;
  OriginStationDescriptor station;
  OriginStationGeometry geometry;
  RigidBodyWorldContext context;
  auto state(std::uint32_t ordinal = 1, double separation = .1,
             SimulationTick tick = 25) const -> RigidBodyState {
    auto s = require(resolve_origin_port_pose(system, station, geometry,
                                              {station.id, ordinal}, tick,
                                              separation))
                 .station_relative;
    s.craft = wayfarer_frame().recipe;
    return s;
  }
  auto assess(const RigidBodyState& state, std::uint32_t ordinal = 1) const
      -> OriginDockAssessment {
    return require(assess_origin_dock(system, station, geometry,
                                      {station.id, ordinal}, state));
  }
  auto capture(const RigidBodyState& state, std::uint32_t ordinal = 1) const {
    return capture_origin_port(system, station, geometry, {station.id, ordinal},
                               state);
  }
};
auto invalid() -> void {
  const Fixture f;
  const auto state = f.state();
  for (const auto port : {OriginPortId{f.station.id, 0},
                          OriginPortId{f.station.id, 3}, OriginPortId{{0}, 1}})
    check(
        !assess_origin_dock(f.system, f.station, f.geometry, port, state) &&
            !capture_origin_port(f.system, f.station, f.geometry, port, state),
        "unknown/foreign port refuses without capture");
  auto geometry = f.geometry;
  geometry.ports[0].capture.alignment_radians =
      std::numeric_limits<double>::quiet_NaN();
  check(!assess_origin_dock(f.system, f.station, geometry, {f.station.id, 1},
                            state),
        "nonfinite caller-edited tolerances refuse");
  geometry = f.geometry;
  geometry.ports[0].approach_reservation.maximum_metres.x += 1;
  check(!assess_origin_dock(f.system, f.station, geometry, {f.station.id, 1},
                            state),
        "edited clearance cannot authorize capture");
  for (const double value : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()}) {
    auto bad = state;
    bad.position_metres.x = value;
    check(!f.capture(bad), "nonfinite position refuses atomically");
    bad = state;
    bad.linear_velocity_metres_per_second.y = value;
    check(!f.capture(bad), "nonfinite velocity refuses atomically");
    bad = state;
    bad.angular_velocity_radians_per_second.x = value;
    check(!f.capture(bad), "nonfinite angular state refuses atomically");
  }
  auto bad = state;
  bad.orientation = {0, 0, 0, 0};
  check(!f.capture(bad), "invalid full attitude refuses");
  bad = state;
  bad.position_metres.x = 1e16;
  check(!f.capture(bad), "excessive coordinates refuse");
  bad = state;
  bad.tick = std::numeric_limits<SimulationTick>::max();
  check(!f.capture(bad), "overflow clock refuses");
  bad = state;
  bad.frame.system.value ^= 1;
  check(!f.capture(bad), "foreign frame owner refuses");
  bad = state;
  bad.frame = {RigidFrameKind::planet_fixed,
               f.system.catalog.id,
               f.station.orbit.host_planet,
               {}};
  check(!f.capture(bad), "unsupported rotating frame refuses explicitly");
  const auto lock = require(f.capture(state));
  auto changed = lock;
  changed.geometry_version = 2;
  check(!release_origin_port(f.system, f.station, f.geometry, changed),
        "unknown constraint geometry refuses release");
  changed = lock;
  changed.craft = starter_shuttle_frame().recipe;
  check(!release_origin_port(f.system, f.station, f.geometry, changed),
        "incompatible constrained craft refuses release");
  changed = lock;
  changed.tick = std::numeric_limits<SimulationTick>::max();
  check(!release_origin_port(f.system, f.station, f.geometry, changed),
        "constraint clock overflow refuses release");
  check(state == f.state() && lock == require(f.capture(state)),
        "failed operations never change source, owner or selected constraint");
}
auto rate_and_position_edges() -> void {
  const Fixture f;
  using D = OriginDockDecision;
  auto s = f.state(1, .15);
  // The nominal .15 m pose rounds just outside the limit at this datum.
  // Test the adjacent representable poses across the strict metric boundary.
  s.position_metres.y = std::nextafter(s.position_metres.y,
                                       std::numeric_limits<double>::infinity());
  check(f.assess(s).decision == D::capture_ready && f.capture(s).has_value(),
        "closest representable separation within the limit captures");
  s.position_metres.y = std::nextafter(
      s.position_metres.y, -std::numeric_limits<double>::infinity());
  check(f.assess(s).decision == D::too_far && !f.capture(s),
        "next representable outward pose beyond distance boundary refuses");
  s = f.state();
  s.position_metres.y += .2;
  check(f.assess(s).decision == D::wrong_side && !f.capture(s),
        "equal-distance rear approach refuses");
  s = f.state();
  s.position_metres.x += 20;
  check(f.assess(s).decision == D::outside_reservation && !f.capture(s),
        "whole-hull reservation failure refuses");
  s = f.state();
  s.craft = starter_shuttle_frame().recipe;
  check(f.assess(s).decision == D::incompatible_craft && !f.capture(s),
        "known legacy frame does not inherit the Wayfarer interface");
  s = f.state();
  s.linear_velocity_metres_per_second.y = .3;
  check(f.assess(s).decision == D::capture_ready,
        "inclusive inward closure boundary captures");
  s.linear_velocity_metres_per_second.y = std::nextafter(.3, 1.0);
  check(f.assess(s).decision == D::excessive_closure && !f.capture(s),
        "next closure rate outside boundary refuses");
  s = f.state();
  s.linear_velocity_metres_per_second.y = -.001;
  check(f.assess(s).decision == D::retreating,
        "retreating craft cannot capture");
  s = f.state();
  s.linear_velocity_metres_per_second.x = .2;
  check(f.assess(s).decision == D::capture_ready,
        "inclusive lateral speed boundary captures");
  s.linear_velocity_metres_per_second.x = std::nextafter(.2, 1.0);
  check(f.assess(s).decision == D::excessive_lateral_motion && !f.capture(s),
        "next lateral speed outside boundary refuses");
  s = f.state();
  s.angular_velocity_radians_per_second = {0, .02, 0};
  check(f.assess(s).decision == D::capture_ready,
        "inclusive angular-rate boundary captures with qualified collar "
        "velocity");
  s.angular_velocity_radians_per_second.y = std::nextafter(.02, 1.0);
  check(f.assess(s).decision == D::excessive_angular_motion && !f.capture(s),
        "next angular rate outside boundary refuses");
  s = f.state();
  s.angular_velocity_radians_per_second.z = -.02;
  s.linear_velocity_metres_per_second.y = .25;
  const auto moving = f.assess(s);
  // D1 yaw maps body omega -Z to station -X. omega x (5.2,2.907,0)
  // changes collar velocity in Z; an independent oracle checks that offset.
  check(std::abs(moving.collar_velocity_station_metres_per_second.z + .05814) <
            1e-12,
        "collar velocity includes angular point motion");
  s = f.state();
  s.angular_velocity_radians_per_second.x = -.02;
  s.linear_velocity_metres_per_second.y = .25;
  check(f.assess(s).decision == D::excessive_closure && !f.capture(s),
        "angular collar motion can exceed closure while COM rate remains safe");
}
auto attitude_edges() -> void {
  const Fixture f;
  using D = OriginDockDecision;
  for (const double angle : {3.0, 3.001, 180.0}) {
    auto s = f.state();
    const double yaw = std::numbers::pi / 2 + angle * std::numbers::pi / 180;
    s.orientation = require(normalize_rigid_orientation(
        {std::cos(yaw / 2), 0, std::sin(yaw / 2), 0}));
    // Keep the actual collar at the same .1 m separation while yaw changes.
    const auto& port = f.geometry.ports[0];
    s.position_metres = {port.collar_position_metres.x - std::sin(yaw) * 5.2,
                         port.collar_position_metres.y - .1 - 2.907,
                         port.collar_position_metres.z - std::cos(yaw) * 5.2};
    const auto decision = f.assess(s).decision;
    check(angle == 3.0 ? decision == D::capture_ready
                       : decision == D::wrong_attitude,
          "full yaw alignment edge is independent of equal collar distance");
  }
  auto s = f.state();
  // Preserve the normal's overall yaw while adding body roll: yaw alone cannot
  // authorize this attitude. Quaternion product Ry(90) * Rz(10).
  const double half = 5 * std::numbers::pi / 180;
  const double base = std::sqrt(.5);
  s.orientation = require(normalize_rigid_orientation(
      {base * std::cos(half), base * std::sin(half), base * std::cos(half),
       base * std::sin(half)}));
  check(f.assess(s).decision == D::wrong_attitude && !f.capture(s),
        "same legacy yaw with wrong roll cannot capture");
}
auto frame_and_constraint_agreement() -> void {
  for (const auto seed :
       {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}}) {
    const Fixture f{seed};
    for (const auto ordinal : {1U, 2U}) {
      for (const SimulationTick tick : {0ULL, 25ULL, 900000ULL}) {
        const OriginDockConstraint lock{
            1, {f.station.id, ordinal}, wayfarer_frame().recipe, tick};
        const auto pose = require(
            resolve_origin_docked_pose(f.system, f.station, f.geometry, lock));
        for (const auto& state : {pose.station_relative, pose.system_inertial,
                                  pose.planet_relative}) {
          const auto before = state;
          const auto assessed = f.assess(state, ordinal);
          check(assessed.decision == OriginDockDecision::capture_ready &&
                    assessed.separation_metres == 0 &&
                    assessed.collar_velocity_station_metres_per_second ==
                        RigidVector3{},
                "same-tick exact port reference/co-motion agrees in every "
                "supported frame");
          check(require(f.capture(state, ordinal)) == lock && state == before,
                "capturing retains one qualified port, recipe/tick and "
                "immutable source");
          const auto encoded =
              require(encode_rigid_body_state_json(f.context, state));
          check(f.assess(
                    require(decode_rigid_body_state_json(f.context, encoded)),
                    ordinal) == assessed,
                "pose projection/hydration retains exact capture outcome");
        }
        const auto released =
            require(release_origin_port(f.system, f.station, f.geometry, lock));
        check(released == pose.planet_relative && released.tick == tick &&
                  released.craft == wayfarer_frame().recipe,
              "release retains exact constrained pose and station co-motion "
              "without a tick step");
      }
    }
  }
}
} // namespace
auto main() -> int {
  invalid();
  rate_and_position_edges();
  attitude_edges();
  frame_and_constraint_agreement();
  std::cout << "Origin docking: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
