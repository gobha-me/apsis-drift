#include "apsis_drift/moon_body_dynamics.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
using namespace apsis_drift;
using V = RigidVector3;
using Code = CentralBodyErrorCode;
int checks{}, failures{};

auto check(bool value, std::string_view message) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Moon dynamics fixture refused");
  return std::move(*value);
}
auto components(V value) -> std::array<double, 3> {
  return {value.x, value.y, value.z};
}
auto state_values(const RigidBodyState& value) -> std::array<double, 13> {
  return {value.position_metres.x,
          value.position_metres.y,
          value.position_metres.z,
          value.orientation.w,
          value.orientation.x,
          value.orientation.y,
          value.orientation.z,
          value.linear_velocity_metres_per_second.x,
          value.linear_velocity_metres_per_second.y,
          value.linear_velocity_metres_per_second.z,
          value.angular_velocity_radians_per_second.x,
          value.angular_velocity_radians_per_second.y,
          value.angular_velocity_radians_per_second.z};
}
auto same_bits(const RigidBodyState& a, const RigidBodyState& b) -> bool {
  const auto av = state_values(a);
  const auto bv = state_values(b);
  if (a.frame != b.frame || a.craft != b.craft || a.tick != b.tick)
    return false;
  for (std::size_t i = 0; i < av.size(); ++i)
    if (std::bit_cast<std::uint64_t>(av[i]) !=
        std::bit_cast<std::uint64_t>(bv[i]))
      return false;
  return true;
}
auto near(double actual, long double expected, long double tolerance) -> bool {
  return std::isfinite(actual) && std::isfinite(expected) &&
         std::abs(static_cast<long double>(actual) - expected) <= tolerance;
}
auto close(V actual, V expected, double tolerance) -> bool {
  return near(actual.x, expected.x, tolerance) &&
         near(actual.y, expected.y, tolerance) &&
         near(actual.z, expected.z, tolerance);
}
auto moon_for(std::uint32_t version) -> PhysicalMoonDescriptor {
  const auto owner = need(generate_physical_origin_system(Seed{42}, version));
  const auto catalog = need(generate_secondary_body_catalog(owner));
  return need(
      generate_physical_moon_descriptor(catalog, catalog.bodies[0].node.id));
}
struct Fixture {
  explicit Fixture(std::uint32_t version = 2)
      : moon(moon_for(version)), context(moon),
        properties(
            need(resolve_craft_frame({kWayfarerFrameId, kWayfarerFrameVersion}))
                .properties) {}
  PhysicalMoonDescriptor moon;
  RigidBodyWorldContext context;
  CraftFrameProperties properties;
  auto state() const -> RigidBodyState {
    RigidBodyState result;
    result.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
    result.frame = {RigidFrameKind::body_relative_inertial,
                    moon.catalog.owner.catalog.id,
                    {},
                    {},
                    moon.body};
    result.tick = 25;
    result.position_metres = {moon.radius_metres + 10000.0, 0, 0};
    return result;
  }
  auto mu() const -> long double {
    const long double r = moon.radius_metres;
    return 6.67430e-11L * 3000.0L * (4.0L / 3.0L) *
           std::numbers::pi_v<long double> * r * r * r;
  }
};
auto gravity(const Fixture& fixture) -> void {
  auto state = fixture.state();
  const auto before = state;
  const auto r = static_cast<double>(fixture.moon.radius_metres);
  for (const auto& position : std::array<V, 7>{{{r, 0, 0},
                                                {-r, 0, 0},
                                                {0, r, 0},
                                                {0, -r, 0},
                                                {0, 0, r},
                                                {0, 0, -r},
                                                {r, 2 * r, 2 * r}}}) {
    state.position_metres = position;
    const auto result =
        need(evaluate_moon_body_gravity(fixture.context, state));
    const long double x = position.x, y = position.y, z = position.z;
    const long double distance = std::sqrt(x * x + y * y + z * z);
    const long double scale = -fixture.mu() / (distance * distance * distance);
    const auto acceleration =
        components(result.acceleration_metres_per_second_squared);
    const auto coordinates = components(position);
    for (std::size_t i = 0; i < 3; ++i)
      check(near(acceleration[i], coordinates[i] * scale, 2e-14L),
            "Typed moon gravity agrees with independent SI inverse-square "
            "vector");
    check(
        result.body == fixture.moon.body &&
            result.reference_radius_metres == r &&
            near(result.distance_metres, distance, 1e-8L) &&
            near(result.gravitational_parameter_metres_cubed_per_second_squared,
                 fixture.mu(), fixture.mu() * 2e-15L),
        "Gravity binds exact moon radius, GM and complete BodyId");
  }
  state = before;
  const auto source = state;
  const auto first = need(evaluate_moon_body_gravity(fixture.context, state));
  state.position_metres.x *= 2;
  const auto second = need(evaluate_moon_body_gravity(fixture.context, state));
  check(near(first.acceleration_metres_per_second_squared.x,
             4.0L * second.acceleration_metres_per_second_squared.x, 1e-15L),
        "Doubling distance quarters gravity without changing body selection");
  check(same_bits(source, before),
        "Gravity query leaves authoritative input unchanged");
}
auto coast(const Fixture& fixture) -> void {
  auto state = fixture.state();
  const long double radius = state.position_metres.x;
  const auto resolved = need(resolve_physical_moon_properties(fixture.moon));
  const long double mu =
      resolved.gravitational_parameter_metres_cubed_per_second_squared;
  state.linear_velocity_metres_per_second.y =
      static_cast<double>(std::sqrt(mu / radius));
  const auto before = state;
  const long double rate = std::sqrt(mu / (radius * radius * radius));
  VacuumIntent neutral;
  neutral.assistance = true;
  constexpr unsigned ticks = 120;
  for (unsigned i = 0; i < ticks; ++i) {
    const auto previous = state;
    const auto report =
        need(advance_moon_body_dynamics(fixture.context, state, neutral));
    check(report.propulsion.applied_force_newtons == V{} &&
              report.propulsion.assist_force_newtons == V{} &&
              report.propulsion.positive_force_newtons == V{} &&
              report.propulsion.negative_force_newtons == V{} &&
              report.propulsion.world_linear_impulse_newton_seconds == V{},
          "Gravity/coasting assistance consumes no gross translation channels");
    const V delta{state.linear_velocity_metres_per_second.x -
                      previous.linear_velocity_metres_per_second.x,
                  state.linear_velocity_metres_per_second.y -
                      previous.linear_velocity_metres_per_second.y,
                  state.linear_velocity_metres_per_second.z -
                      previous.linear_velocity_metres_per_second.z};
    check(close(report.total_linear_impulse_newton_seconds,
                {fixture.properties.dry_mass_kg * delta.x,
                 fixture.properties.dry_mass_kg * delta.y,
                 fixture.properties.dry_mass_kg * delta.z},
                2e-9) &&
              report.total_linear_impulse_newton_seconds ==
                  report.gravity_impulse_newton_seconds,
          "Neutral momentum change is reported as gravity, separate from "
          "propulsion");
  }
  const long double angle = rate * ticks / 120.0L;
  const long double position_tolerance =
      64 * std::numeric_limits<double>::epsilon() * radius * ticks;
  const long double speed = std::sqrt(mu / radius);
  check(near(state.position_metres.x, radius * std::cos(angle),
             position_tolerance) &&
            near(state.position_metres.y, radius * std::sin(angle),
                 position_tolerance) &&
            near(state.linear_velocity_metres_per_second.x,
                 -speed * std::sin(angle), 2e-8L) &&
            near(state.linear_velocity_metres_per_second.y,
                 speed * std::cos(angle), 2e-8L) &&
            state.position_metres.z == 0 && state.tick == before.tick + ticks &&
            state.frame == before.frame && state.craft == before.craft,
        "One-second moon coast corroborates analytic circular orbit with no "
        "hidden damping");
}
auto actuation(const Fixture& fixture) -> void {
  auto state = fixture.state();
  VacuumIntent thrust;
  thrust.assistance = false;
  thrust.negative_translation.z = 0.5;
  const auto before = state;
  const auto report =
      need(advance_moon_body_dynamics(fixture.context, state, thrust));
  const double force = fixture.properties.negative_force_newtons[2] * 0.5;
  const double dt = kSimulationStep.count();
  check(report.propulsion.negative_force_newtons.z == force &&
            report.propulsion.positive_force_newtons == V{} &&
            report.propulsion.applied_force_newtons == V{0, 0, -force} &&
            near(report.propulsion.world_linear_impulse_newton_seconds.z,
                 -force * dt, 1e-10L),
        "Main thrust reports actual gross force/impulse without gravity fuel");
  const auto total = components(report.total_linear_impulse_newton_seconds);
  const auto gravity_impulse =
      components(report.gravity_impulse_newton_seconds);
  const auto propulsion =
      components(report.propulsion.world_linear_impulse_newton_seconds);
  for (std::size_t i = 0; i < 3; ++i)
    check(near(total[i], gravity_impulse[i] + propulsion[i], 1e-10L),
          "Total integrated momentum combines propulsion and gravity once");
  check(state.linear_velocity_metres_per_second.x <
                before.linear_velocity_metres_per_second.x &&
            state.linear_velocity_metres_per_second.z <
                before.linear_velocity_metres_per_second.z,
        "Moon gravity and main thrust both change the actual candidate state");
  auto opposing_state = fixture.state();
  auto coast_state = opposing_state;
  VacuumIntent opposing;
  opposing.assistance = false;
  opposing.positive_translation.x = 1;
  opposing.negative_translation.x = 1;
  const auto opposed = need(
      advance_moon_body_dynamics(fixture.context, opposing_state, opposing));
  VacuumIntent neutral;
  neutral.assistance = false;
  need(advance_moon_body_dynamics(fixture.context, coast_state, neutral));
  check(fixture.properties.positive_force_newtons[0] ==
                fixture.properties.negative_force_newtons[0] &&
            opposed.propulsion.positive_force_newtons.x > 0 &&
            opposed.propulsion.negative_force_newtons.x > 0 &&
            opposed.propulsion.applied_force_newtons == V{} &&
            same_bits(opposing_state, coast_state),
        "Opposing gross thrusters retain fuel channels even when net coast is "
        "identical");

  auto torque_state = fixture.state();
  VacuumIntent torque;
  torque.assistance = false;
  torque.positive_rotation.y = 0.25;
  const auto spun =
      need(advance_moon_body_dynamics(fixture.context, torque_state, torque));
  const long double moment = fixture.properties.torque_newton_metres[1] * 0.25L;
  const long double alpha =
      moment / fixture.properties.principal_inertia_kg_m2[1];
  const long double half_angle = alpha * dt * dt / 4;
  check(near(torque_state.angular_velocity_radians_per_second.y, alpha * dt,
             1e-13L) &&
            near(torque_state.orientation.w, std::cos(half_angle), 1e-13L) &&
            near(torque_state.orientation.y, std::sin(half_angle), 1e-13L) &&
            near(spun.propulsion.world_angular_impulse_newton_metre_seconds.y,
                 moment * dt, 1e-10L),
        "Unassisted single-axis moon torque matches independent "
        "constant-angular-acceleration oracle");
  auto assisted = fixture.state();
  assisted.angular_velocity_radians_per_second = {0.1, -0.2, 0.3};
  const auto attitude = need(advance_vacuum_attitude(
      assisted.craft, assisted.orientation,
      assisted.angular_velocity_radians_per_second, {}, {}, true));
  const auto stabilized =
      need(advance_moon_body_dynamics(fixture.context, assisted, {}));
  check(assisted.orientation == attitude.orientation &&
            assisted.angular_velocity_radians_per_second ==
                attitude.angular_velocity_radians_per_second &&
            stabilized.propulsion.assist_force_newtons == V{} &&
            stabilized.propulsion.applied_torque_newton_metres ==
                attitude.actuation.applied_torque_newton_metres &&
            stabilized.propulsion.world_angular_impulse_newton_metre_seconds ==
                attitude.actuation.frame_angular_impulse_newton_metre_seconds,
        "Gravity uses the same torque/RK4 path and rotation-only assistance as "
        "the public attitude kernel");
}
auto refusals(const Fixture& fixture) -> void {
  const auto source = fixture.state();
  const auto refuse = [&](const RigidBodyWorldContext& context,
                          RigidBodyState state, const VacuumIntent& intent,
                          MoonBodyDynamicsRecipe recipe = {},
                          SimulationSeconds step = kSimulationStep) {
    const auto before = state;
    const auto result =
        advance_moon_body_dynamics(context, state, intent, recipe, step);
    check(!result && same_bits(state, before),
          "Refused moon tick preserves every original state bit");
  };
  auto state = source;
  const auto unknown =
      advance_moon_body_dynamics(fixture.context, state, {}, {0});
  check(!unknown && unknown.error().code == Code::unsupported_version &&
            same_bits(state, source),
        "Unsupported moon dynamics recipe refuses atomically");
  refuse(fixture.context, source, {}, {2});
  const RigidBodyWorldContext planet{fixture.moon.catalog.owner};
  refuse(planet, source, {});
  check(!evaluate_central_body_gravity(fixture.context, source),
        "Moon cannot enter the historical PlanetId gravity API");
  auto forged = fixture.moon;
  ++forged.radius_metres;
  refuse(RigidBodyWorldContext{forged}, source, {});
  auto wrong = source;
  wrong.frame.kind = RigidFrameKind::system_inertial;
  wrong.frame.body.reset();
  refuse(fixture.context, wrong, {});
  wrong = source;
  wrong.frame.body->kind = BodyKind::planet;
  refuse(fixture.context, wrong, {});
  wrong = source;
  ++wrong.frame.body->value;
  refuse(fixture.context, wrong, {});
  wrong = source;
  wrong.frame.planet =
      fixture.moon.catalog.owner.catalog.planets.front().descriptor.id;
  refuse(fixture.context, wrong, {});
  wrong = source;
  wrong.position_metres = {};
  const auto center = evaluate_moon_body_gravity(fixture.context, wrong);
  check(!center && center.error().code == Code::invalid_radius,
        "Moon center has no inverse-square acceleration");
  refuse(fixture.context, wrong, {});
  wrong.position_metres = {std::nextafter(1.0, 0.0), 0, 0};
  check(!evaluate_moon_body_gravity(fixture.context, wrong),
        "Subminimum radial domain refuses");
  wrong.position_metres.x = 1;
  check(evaluate_moon_body_gravity(fixture.context, wrong).has_value(),
        "Exact existing1m arithmetic radius boundary remains supported");
  for (const auto value :
       {std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -0.1, 1.1}) {
    for (unsigned channel = 0; channel < 4; ++channel) {
      VacuumIntent bad;
      if (channel == 0)
        bad.positive_translation.x = value;
      else if (channel == 1)
        bad.negative_translation.y = value;
      else if (channel == 2)
        bad.positive_rotation.z = value;
      else
        bad.negative_rotation.x = value;
      refuse(fixture.context, source, bad);
    }
  }
  for (const auto value : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity()}) {
    wrong = source;
    wrong.position_metres.x = value;
    refuse(fixture.context, wrong, {});
    wrong = source;
    wrong.linear_velocity_metres_per_second.y = value;
    refuse(fixture.context, wrong, {});
    wrong = source;
    wrong.angular_velocity_radians_per_second.z = value;
    refuse(fixture.context, wrong, {});
  }
  wrong = source;
  wrong.orientation = {0, 0, 0, 0};
  refuse(fixture.context, wrong, {});
  wrong = source;
  wrong.tick = std::numeric_limits<SimulationTick>::max() - 1;
  refuse(fixture.context, wrong, {});
  ++wrong.tick;
  refuse(fixture.context, wrong, {});
  wrong = source;
  wrong.position_metres.x = kRigidBodyMaximumPositionMetres;
  wrong.linear_velocity_metres_per_second.x =
      kRigidBodyMaximumVelocityMetresPerSecond;
  refuse(fixture.context, wrong, {});
  for (const auto step :
       {SimulationSeconds{0}, SimulationSeconds{-1},
        SimulationSeconds{std::numeric_limits<double>::quiet_NaN()},
        SimulationSeconds{std::nextafter(kSimulationStep.count(), 1.0)}})
    refuse(fixture.context, source, {}, {}, step);
}
} // namespace

int main() {
  try {
    const Fixture fixture;
    gravity(fixture);
    coast(fixture);
    actuation(fixture);
    refusals(fixture);
    const Fixture legacy{1};
    gravity(legacy);
    check(
        need(evaluate_moon_body_gravity(legacy.context, legacy.state())).body ==
            fixture.moon.body,
        "Both explicit source ephemeris families retain typed moon identity");
  } catch (const std::exception& error) {
    check(false, error.what());
  }
  std::cout << checks << " moon dynamics checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
