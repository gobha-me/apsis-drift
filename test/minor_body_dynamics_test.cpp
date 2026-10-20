#include "apsis_drift/minor_body_dynamics.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>

namespace {
using namespace apsis_drift;
int checks{}, failures{};
auto check(bool v, std::string_view text) -> void {
  ++checks;
  if (!v) {
    ++failures;
    std::cerr << "FAIL: " << text << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Minor dynamics fixture refused");
  return std::move(*value);
}
auto near(double a, long double b, long double tolerance) -> bool {
  return std::isfinite(a) && std::isfinite(b) &&
         std::abs(static_cast<long double>(a) - b) <= tolerance;
}
auto run() -> void {
  const auto catalog = need(generate_secondary_body_catalog(
      need(generate_physical_origin_system(Seed{42}, 2))));
  const auto owner = need(
      generate_physical_minor_descriptor(catalog, catalog.bodies[1].node.id));
  const RigidBodyWorldContext context{owner};
  const auto properties =
      need(resolve_craft_frame({kWayfarerFrameId, kWayfarerFrameVersion}))
          .properties;
  RigidBodyState source;
  source.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  source.frame = {RigidFrameKind::body_relative_inertial,
                  context.system.id,
                  {},
                  {},
                  owner.body};
  source.tick = 25;
  source.position_metres = {owner.radius_metres + 20000.0, 0, 0};
  const auto original = source;
  const long double r = owner.radius_metres;
  const long double mu = 6.67430e-11L * 3000.0L * (4.0L / 3.0L) *
                         std::numbers::pi_v<long double> * r * r * r;
  const auto gravity = need(evaluate_minor_body_gravity(context, source));
  const long double distance = source.position_metres.x;
  check(gravity.body == owner.body &&
            near(gravity.acceleration_metres_per_second_squared.x,
                 -mu / (distance * distance), 1e-14L) &&
            gravity.acceleration_metres_per_second_squared.y == 0 &&
            gravity.acceleration_metres_per_second_squared.z == 0,
        "Actual typed gravity agrees with independent mass/radius formula");
  {
    auto twice = source;
    twice.position_metres.x *= 2;
    const auto g = need(evaluate_minor_body_gravity(context, twice));
    check(near(g.acceleration_metres_per_second_squared.x,
               gravity.acceleration_metres_per_second_squared.x / 4, 1e-15L),
          "Inverse-square radius relation");
  }
  VacuumIntent passive{{}, {}, {}, {}, false};
  auto coast = source;
  long double x = distance, v = 0;
  constexpr long double dt = 1.0L / 120;
  for (int i = 0; i < 120; ++i) {
    const auto report =
        need(advance_minor_body_dynamics(context, coast, passive));
    const auto accel = [&](long double pos) { return -mu / (pos * pos); };
    const long double k1x = v, k1v = accel(x);
    const long double k2x = v + dt * k1v / 2, k2v = accel(x + dt * k1x / 2);
    const long double k3x = v + dt * k2v / 2, k3v = accel(x + dt * k2x / 2);
    const long double k4x = v + dt * k3v, k4v = accel(x + dt * k3x);
    x += dt * (k1x + 2 * k2x + 2 * k3x + k4x) / 6;
    v += dt * (k1v + 2 * k2v + 2 * k3v + k4v) / 6;
    check(report.propulsion.positive_force_newtons == RigidVector3{} &&
              report.propulsion.negative_force_newtons == RigidVector3{} &&
              report.propulsion.world_linear_impulse_newton_seconds ==
                  RigidVector3{} &&
              report.gravity_impulse_newton_seconds.x < 0,
          "Gravity coasting consumes no gross propulsion channels");
  }
  check(coast.tick == source.tick + 120 &&
            near(coast.position_metres.x, x, 1e-7L) &&
            near(coast.linear_velocity_metres_per_second.x, v, 1e-12L),
        "120Hz radial coast agrees with independent long-double integration");
  {
    auto body = source;
    const auto force = need(advance_minor_body_dynamics(
        context, body, {{.25, 0, 0}, {}, {}, {}, false}));
    check(force.propulsion.positive_force_newtons.x ==
                  properties.positive_force_newtons[0] * .25 &&
              force.propulsion.negative_force_newtons == RigidVector3{} &&
              near(force.propulsion.world_linear_impulse_newton_seconds.x,
                   properties.positive_force_newtons[0] * .25L / 120, 1e-10L),
          "Actual rated thrust remains separate from gravity and fuel");
    check(near(force.total_linear_impulse_newton_seconds.x,
               force.propulsion.world_linear_impulse_newton_seconds.x +
                   force.gravity_impulse_newton_seconds.x,
               1e-9L),
          "Total impulse composes actual propulsion and gravity");
  }
  {
    auto body = source;
    const auto torque = need(advance_minor_body_dynamics(
        context, body, {{}, {}, {0, 0, .5}, {}, false}));
    check(torque.propulsion.applied_torque_newton_metres.z > 0 &&
              near(body.angular_velocity_radians_per_second.z,
                   torque.propulsion.applied_torque_newton_metres.z /
                       (120.0L * properties.principal_inertia_kg_m2[2]),
                   1e-12L),
          "Same principal-axis torque/RK4 allocator is active");
  }
  {
    auto body = source;
    body.angular_velocity_radians_per_second = {0, 0, .01};
    const auto report = need(
        advance_minor_body_dynamics(context, body, {{}, {}, {}, {}, true}));
    check(body.angular_velocity_radians_per_second.z < .01 &&
              report.propulsion.negative_torque_newton_metres.z > 0 &&
              report.propulsion.assist_force_newtons == RigidVector3{},
          "Assistance stabilizes rotation only, not gravity compensation");
  }
  for (const auto& command : std::array<VacuumIntent, 3>{
           VacuumIntent{{1.01, 0, 0}, {}, {}, {}, false},
           VacuumIntent{{},
                        {},
                        {std::numeric_limits<double>::quiet_NaN(), 0, 0},
                        {},
                        false},
           VacuumIntent{{}, {-1, 0, 0}, {}, {}, true}}) {
    auto body = source;
    check(!advance_minor_body_dynamics(context, body, command) &&
              body == source,
          "Invalid controls refuse atomically");
  }
  {
    auto body = source;
    check(!advance_minor_body_dynamics(context, body, passive, {},
                                       SimulationSeconds{.01}) &&
              body == source,
          "Wrong time step refuses atomically");
  }
  {
    auto body = source;
    check(!advance_minor_body_dynamics(context, body, passive, {2}) &&
              body == source,
          "Unknown dynamics recipe refuses");
  }
  {
    auto body = source;
    body.position_metres.x = std::numeric_limits<double>::infinity();
    const auto previous = body;
    check(!advance_minor_body_dynamics(context, body, passive) &&
              body == previous,
          "Nonfinite source is not partly advanced");
  }
  {
    auto body = source;
    body.tick = std::numeric_limits<SimulationTick>::max() - 1;
    const auto previous = body;
    check(!advance_minor_body_dynamics(context, body, passive) &&
              body == previous,
          "Tick overflow refuses atomically");
  }
  const auto moon = need(
      generate_physical_moon_descriptor(catalog, catalog.bodies[0].node.id));
  check(!evaluate_minor_body_gravity(RigidBodyWorldContext{moon}, source),
        "Moon cannot authenticate a minor dynamics source");
  const double radius =
      need(minor_exclusion_radius_metres(owner, source.craft));
  check(radius > owner.radius_metres + 1000 &&
            !minor_exclusion_radius_metres(owner, {CraftFrameId{999999}, 1}),
        "Readout uses the actual registered hull and refuses unknown craft");
  auto before = source;
  before.position_metres = {radius + 20, 0, 0};
  auto candidate = before;
  ++candidate.tick;
  candidate.position_metres = {radius - 20, 0, 0};
  candidate.linear_velocity_metres_per_second = {-30, 2, 3};
  candidate.angular_velocity_radians_per_second = {.01, .02, .03};
  candidate.orientation = need(normalize_rigid_orientation({.9, .1, .2, .3}));
  const auto sweep =
      need(assess_minor_exclusion_sweep(owner, before, candidate));
  check(
      sweep.classification == MinorExclusionClass::intersects &&
          sweep.body == owner.body && sweep.source_tick == before.tick &&
          sweep.candidate_tick == candidate.tick &&
          sweep.exclusion_radius_metres == radius &&
          sweep.hull_radius_metres > 0 &&
          sweep.minimum_distance_metres < radius,
      "Genuine intersecting tick and pure readout share one exclusion radius");
  const auto response =
      need(respond_minor_exclusion_sweep(owner, before, candidate));
  check(response.position_metres == before.position_metres &&
            response.tick == candidate.tick &&
            response.orientation == candidate.orientation &&
            response.angular_velocity_radians_per_second ==
                candidate.angular_velocity_radians_per_second &&
            response.linear_velocity_metres_per_second == RigidVector3{0, 2, 3},
        "Guard response preserves time/attitude/spin/tangential velocity and "
        "removes inward component");
  {
    auto clear = candidate;
    clear.position_metres = {radius + 21, 0, 0};
    check(need(assess_minor_exclusion_sweep(owner, before, clear))
                      .classification == MinorExclusionClass::clear &&
              !respond_minor_exclusion_sweep(owner, before, clear),
          "Clear ticks never receive an artificial stand-off response");
  }
  {
    auto crossing = candidate;
    crossing.position_metres = {-radius - 20, 0, 0};
    check(need(assess_minor_exclusion_sweep(owner, before, crossing))
                  .classification == MinorExclusionClass::intersects,
          "Exterior endpoints cannot tunnel through the exclusion sphere");
  }
  {
    auto uncertain = candidate;
    uncertain.position_metres = {radius, 0, 0};
    check(need(assess_minor_exclusion_sweep(owner, before, uncertain))
                      .classification == MinorExclusionClass::unknown &&
              !respond_minor_exclusion_sweep(owner, before, uncertain),
          "Near-boundary uncertainty is never silently clear or responded");
  }
  {
    auto bad = before;
    bad.position_metres = {radius - 1, 0, 0};
    check(!assess_minor_exclusion_sweep(owner, bad, candidate),
          "Response requires a genuinely last-clear source");
  }
  {
    auto bad = candidate;
    ++bad.tick;
    check(!assess_minor_exclusion_sweep(owner, before, bad),
          "Sweep cannot misrepresent a multi-tick segment");
  }
  {
    auto bad = candidate;
    bad.frame.body = moon.body;
    check(!assess_minor_exclusion_sweep(owner, before, bad),
          "Sweep owner/frame substitution refuses");
  }
  check(source == original,
        "Queries and separate candidate integration preserve original source");
}
} // namespace
int main() {
  try {
    run();
  } catch (const std::exception& e) {
    check(false, e.what());
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
