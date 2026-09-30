#include "apsis_drift/central_body_dynamics.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace apsis_drift;
using V = RigidVector3;
using Code = CentralBodyErrorCode;
int failures{};
auto check(bool condition, std::string_view message) -> void {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}
template <typename T, typename E>
auto required(std::expected<T, E> result) -> T {
  if (!result) throw std::runtime_error("required central-body fixture failed");
  return *result;
}
auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(V a, double f) -> V {
  return {a.x * f, a.y * f, a.z * f};
}
auto dot(V a, V b) -> double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
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
  return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) &&
         near(a.z, b.z, tolerance);
}
auto scalars(RigidBodyState& s) -> std::array<double*, 13> {
  return {&s.position_metres.x,
          &s.position_metres.y,
          &s.position_metres.z,
          &s.orientation.w,
          &s.orientation.x,
          &s.orientation.y,
          &s.orientation.z,
          &s.linear_velocity_metres_per_second.x,
          &s.linear_velocity_metres_per_second.y,
          &s.linear_velocity_metres_per_second.z,
          &s.angular_velocity_radians_per_second.x,
          &s.angular_velocity_radians_per_second.y,
          &s.angular_velocity_radians_per_second.z};
}
auto same_bits(RigidBodyState a, RigidBodyState b) -> bool {
  const auto av = scalars(a), bv = scalars(b);
  for (std::size_t i = 0; i < av.size(); ++i)
    if (std::bit_cast<std::uint64_t>(*av[i]) !=
        std::bit_cast<std::uint64_t>(*bv[i]))
      return false;
  return a.craft == b.craft && a.frame == b.frame && a.tick == b.tick;
}
auto same_report(const CentralBodyActuation& a, const CentralBodyActuation& b)
    -> bool {
  const auto& x = a.propulsion;
  const auto& y = b.propulsion;
  return x.requested_force_newtons == y.requested_force_newtons &&
         x.requested_torque_newton_metres == y.requested_torque_newton_metres &&
         x.assist_force_newtons == y.assist_force_newtons &&
         x.assist_torque_newton_metres == y.assist_torque_newton_metres &&
         x.applied_force_newtons == y.applied_force_newtons &&
         x.applied_torque_newton_metres == y.applied_torque_newton_metres &&
         x.positive_force_newtons == y.positive_force_newtons &&
         x.negative_force_newtons == y.negative_force_newtons &&
         x.positive_torque_newton_metres == y.positive_torque_newton_metres &&
         x.negative_torque_newton_metres == y.negative_torque_newton_metres &&
         x.world_linear_impulse_newton_seconds ==
             y.world_linear_impulse_newton_seconds &&
         x.world_angular_impulse_newton_metre_seconds ==
             y.world_angular_impulse_newton_metre_seconds &&
         x.assistance == y.assistance &&
         x.force_saturated == y.force_saturated &&
         x.torque_saturated == y.torque_saturated &&
         a.initial_gravity.planet == b.initial_gravity.planet &&
         a.initial_gravity.reference_radius_metres ==
             b.initial_gravity.reference_radius_metres &&
         a.initial_gravity.surface_gravity_metres_per_second_squared ==
             b.initial_gravity.surface_gravity_metres_per_second_squared &&
         a.initial_gravity
                 .gravitational_parameter_metres_cubed_per_second_squared ==
             b.initial_gravity
                 .gravitational_parameter_metres_cubed_per_second_squared &&
         a.initial_gravity.distance_metres ==
             b.initial_gravity.distance_metres &&
         a.initial_gravity.acceleration_metres_per_second_squared ==
             b.initial_gravity.acceleration_metres_per_second_squared &&
         a.gravity_impulse_newton_seconds == b.gravity_impulse_newton_seconds &&
         a.total_linear_impulse_newton_seconds ==
             b.total_linear_impulse_newton_seconds;
}
struct Fixture {
  Fixture(Seed seed = Seed{42}, bool origin = true)
      : system(required(origin ? generate_physical_origin_system(seed)
                               : generate_physical_local_system(seed))),
        context(system), planet(system.catalog.planets.front().descriptor) {}
  PhysicalLocalSystem system;
  RigidBodyWorldContext context;
  const PlanetDescriptor& planet;
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
    s.tick = 25;
    s.position_metres = {radius() + 500000, 0, 0};
    return s;
  }
};
// Independent translation RK4 oracle. For spinning thrust the analytic
// principal-Y rotation determines force at each stage, independently of the
// provider's quaternion/attitude kernel.
struct Motion {
  V position, velocity;
};
auto oracle(Motion source, double mu, V force, double mass, double omega,
            double time) -> Motion {
  const auto derivative = [&](Motion m, double t) -> Motion {
    const double r = length(m.position);
    const double angle = omega * t;
    const V thrust{std::cos(angle) * force.x + std::sin(angle) * force.z,
                   force.y,
                   -std::sin(angle) * force.x + std::cos(angle) * force.z};
    return {m.velocity, add(scale(m.position, -mu / (r * r * r)),
                            scale(thrust, 1.0 / mass))};
  };
  const auto offset = [](Motion a, Motion b, double f) -> Motion {
    return {add(a.position, scale(b.position, f)),
            add(a.velocity, scale(b.velocity, f))};
  };
  const double dt = kSimulationStep.count();
  const auto a = derivative(source, time);
  const auto b = derivative(offset(source, a, dt / 2), time + dt / 2);
  const auto c = derivative(offset(source, b, dt / 2), time + dt / 2);
  const auto d = derivative(offset(source, c, dt), time + dt);
  const auto weighted = [](V a, V b, V c, V d) -> V {
    return add(add(a, scale(add(b, c), 2)), d);
  };
  return {add(source.position,
              scale(weighted(a.position, b.position, c.position, d.position),
                    dt / 6)),
          add(source.velocity,
              scale(weighted(a.velocity, b.velocity, c.velocity, d.velocity),
                    dt / 6))};
}
auto law(const Fixture& f) -> void {
  for (const auto& body : f.system.catalog.planets) {
    auto s = f.state();
    s.frame.planet = body.descriptor.id;
    const double radius = body.descriptor.radius.value * 1000.0;
    const double surface =
        9.80665 * body.descriptor.surface_gravity.value / 1000;
    double previous = std::numeric_limits<double>::infinity();
    for (const double factor : {1.0, 1.25, 2.0, 10.0, 1000.0}) {
      for (const V direction :
           {V{1, 0, 0}, V{0, -1, 0}, V{0, 0, 1}, V{.6, 0, .8}}) {
        s.position_metres = scale(direction, radius * factor);
        const auto before = s;
        const auto sample =
            required(evaluate_central_body_gravity(f.context, s));
        const double expected = surface / (factor * factor);
        check(
            sample.planet == body.descriptor.id &&
                sample.reference_radius_metres == radius &&
                near(
                    sample
                        .gravitational_parameter_metres_cubed_per_second_squared,
                    surface * radius * radius, .5) &&
                close(sample.acceleration_metres_per_second_squared,
                      scale(direction, -expected), 4e-14) &&
                near(length(sample.acceleration_metres_per_second_squared),
                     expected, 4e-14),
            "generated truth and independent inverse-square "
            "direction/magnitude");
        check(same_bits(s, before), "gravity query preserves source bits");
      }
      const auto sample = required(evaluate_central_body_gravity(f.context, s));
      check(length(sample.acceleration_metres_per_second_squared) < previous,
            "gravity declines monotonically with bounded altitude");
      previous = length(sample.acceleration_metres_per_second_squared);
    }
  }
}
auto orbits(const Fixture& f) -> void {
  const double r = f.state().position_metres.x;
  const double circular_speed = std::sqrt(f.mu() / r);
  for (const double sign : {-1.0, 1.0}) {
    auto s = f.state();
    s.linear_velocity_metres_per_second = {0, 0, sign * circular_speed};
    const auto initial = s;
    const double energy = dot(s.linear_velocity_metres_per_second,
                              s.linear_velocity_metres_per_second) /
                              2 -
                          f.mu() / r;
    const V momentum =
        cross(s.position_metres, s.linear_velocity_metres_per_second);
    VacuumIntent intent;
    intent.assistance = sign > 0;
    constexpr unsigned ticks = 108000; // 900 simulated seconds, no pose resets
    for (unsigned i = 0; i < ticks; ++i) {
      const auto report =
          required(advance_central_body_dynamics(f.context, s, intent));
      check(
          report.propulsion.applied_force_newtons == V{} &&
              report.propulsion.assist_force_newtons == V{} &&
              report.propulsion.world_linear_impulse_newton_seconds == V{},
          "circular motion uses no propulsion or neutral translational assist");
    }
    const double elapsed = ticks * kSimulationStep.count();
    const double angle = circular_speed / r * elapsed;
    check(close(s.position_metres,
                {r * std::cos(angle), 0, sign * r * std::sin(angle)}, .01) &&
              close(s.linear_velocity_metres_per_second,
                    {-circular_speed * std::sin(angle), 0,
                     sign * circular_speed * std::cos(angle)},
                    1e-5),
          "900-second circular prograde/retrograde agrees with analytic phase");
    check(
        near(length(s.position_metres), r, .01) &&
            near(dot(s.linear_velocity_metres_per_second,
                     s.linear_velocity_metres_per_second) /
                         2 -
                     f.mu() / length(s.position_metres),
                 energy, 1e-4) &&
            close(cross(s.position_metres, s.linear_velocity_metres_per_second),
                  momentum, 2),
        "unpowered circular radius, energy and angular momentum remain "
        "bounded");
    check(s.tick == initial.tick + ticks, "integration owns one tick per step");
  }
  // Radial fall, eccentric, suborbital and escape energy classes are physics
  // fixtures; this provider does not publish orbital classification telemetry.
  for (const double speed : {0.0, .5, .8, 1.6}) {
    auto s = f.state();
    s.linear_velocity_metres_per_second = {0, 0, speed * circular_speed};
    auto other = s;
    Motion reference{s.position_metres, s.linear_velocity_metres_per_second};
    const auto initial = s;
    VacuumIntent off;
    off.assistance = false;
    for (unsigned i = 0; i < 14400; ++i) {
      const auto report =
          required(advance_central_body_dynamics(f.context, s, {}));
      required(advance_central_body_dynamics(f.context, other, off));
      reference = oracle(reference, f.mu(), {}, 1, 0, 0);
      check(
          s.position_metres == other.position_metres &&
              s.linear_velocity_metres_per_second ==
                  other.linear_velocity_metres_per_second,
          "assistance leaves neutral translation subject to the same gravity");
      check(report.propulsion.positive_force_newtons == V{} &&
                report.propulsion.negative_force_newtons == V{},
            "environmental impulse is never a firing channel");
    }
    check(close(s.position_metres, reference.position, .001) &&
              close(s.linear_velocity_metres_per_second, reference.velocity,
                    1e-6),
          "120-second radial/eccentric/suborbital/escape agrees with "
          "independent RK4");
    if (speed == 0)
      check(s.position_metres.x < initial.position_metres.x &&
                s.linear_velocity_metres_per_second.x < 0,
            "zero inertial velocity falls instead of hovering");
    const double initial_energy =
        dot(initial.linear_velocity_metres_per_second,
            initial.linear_velocity_metres_per_second) /
            2 -
        f.mu() / r;
    const double final_energy = dot(s.linear_velocity_metres_per_second,
                                    s.linear_velocity_metres_per_second) /
                                    2 -
                                f.mu() / length(s.position_metres);
    check(near(initial_energy, final_energy, 1e-4) &&
              ((speed == 1.6) == (final_energy > 0)),
          "bound and escape fixtures conserve independently defined energy");
  }
}
auto thrust_and_resume(const Fixture& f) -> void {
  const auto craft = required(resolve_craft_frame(f.state().craft)).properties;
  auto s = f.state();
  s.angular_velocity_radians_per_second = {0, .25, 0};
  s.linear_velocity_metres_per_second = {13, -7, 19};
  Motion reference{s.position_metres, s.linear_velocity_metres_per_second};
  VacuumIntent intent;
  intent.assistance = false;
  intent.negative_translation.z = .5;
  const V force{0, 0, -.5 * craft.negative_force_newtons[2]};
  for (unsigned i = 0; i < 1200; ++i) {
    const auto before = s;
    const auto attitude = required(advance_vacuum_attitude(
        s.craft, s.orientation, s.angular_velocity_radians_per_second, {}, {},
        false));
    const auto report =
        required(advance_central_body_dynamics(f.context, s, intent));
    reference = oracle(reference, f.mu(), force, craft.dry_mass_kg, .25,
                       i * kSimulationStep.count());
    check(s.orientation == attitude.orientation &&
              s.angular_velocity_radians_per_second ==
                  attitude.angular_velocity_radians_per_second &&
              report.propulsion.world_angular_impulse_newton_metre_seconds ==
                  attitude.actuation.frame_angular_impulse_newton_metre_seconds,
          "central flight uses the identical existing attitude/torque stages");
    check(close(report.total_linear_impulse_newton_seconds,
                scale(sub(s.linear_velocity_metres_per_second,
                          before.linear_velocity_metres_per_second),
                      craft.dry_mass_kg),
                1e-8) &&
              close(add(report.propulsion.world_linear_impulse_newton_seconds,
                        report.gravity_impulse_newton_seconds),
                    report.total_linear_impulse_newton_seconds, 1e-9),
          "stage-composed gravity and actual propulsion account for velocity "
          "impulse");
  }
  check(
      close(s.position_metres, reference.position, .001) &&
          close(s.linear_velocity_metres_per_second, reference.velocity, 1e-6),
      "rotating thrust follows analytic spin and independent stage gravity");
  constexpr CentralBodyDynamicsRecipe selected{kCentralBodyDynamicsVersion};
  for (const double speed : {0.0, .5, 1.0, 1.6}) {
    auto uninterrupted = f.state();
    uninterrupted.linear_velocity_metres_per_second.z =
        speed * std::sqrt(f.mu() / uninterrupted.position_metres.x);
    auto resumed = uninterrupted;
    for (unsigned i = 0; i < 1200; ++i) {
      VacuumIntent command;
      command.assistance = (i / 73) % 2 == 0;
      if (i > 700) {
        command.positive_translation.x = .125;
        command.negative_translation.z = .25;
        command.positive_rotation.y = .25;
      }
      if (i == 37 || i == 600 || i == 915) {
        resumed = required(decode_rigid_body_state_json(
            f.context,
            required(encode_rigid_body_state_json(f.context, resumed))));
        check(same_bits(uninterrupted, resumed),
              "v3 hydration retains flight bits");
      }
      const auto a = required(advance_central_body_dynamics(
          f.context, uninterrupted, command, selected));
      const auto b = required(
          advance_central_body_dynamics(f.context, resumed, command, selected));
      check(same_bits(uninterrupted, resumed) && same_report(a, b) &&
                required(rigid_body_state_checksum(f.context, uninterrupted)) ==
                    required(rigid_body_state_checksum(f.context, resumed)),
            "explicit recipe/intent continuation exactly preserves state, "
            "impulses and checksum");
    }
    if (speed == 1.0)
      // New explicit central-body v1 trace, agreed by GCC and Clang; legacy
      // vacuum/attitude and physical projection goldens remain untouched.
      check(required(rigid_body_state_checksum(f.context, uninterrupted)) ==
                2603969511048881734ULL,
            "central-body v1 mixed-input continuation checksum golden");
  }
}
auto channels(const Fixture& f) -> void {
  const auto craft = required(resolve_craft_frame(f.state().craft)).properties;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    for (const bool positive : {false, true}) {
      auto state = f.state();
      VacuumIntent intent;
      auto& requested =
          positive ? intent.positive_translation : intent.negative_translation;
      const std::array<double*, 3> values{&requested.x, &requested.y,
                                          &requested.z};
      *values[axis] = .25;
      const auto before = state;
      const auto report =
          required(advance_central_body_dynamics(f.context, state, intent));
      V expected;
      const std::array<double*, 3> force{&expected.x, &expected.y, &expected.z};
      *force[axis] = (positive ? .25 : -.25) *
                     (positive ? craft.positive_force_newtons[axis]
                               : craft.negative_force_newtons[axis]);
      const auto reference = oracle(
          {before.position_metres, before.linear_velocity_metres_per_second},
          f.mu(), expected, craft.dry_mass_kg, 0, 0);
      check(report.propulsion.applied_force_newtons == expected &&
                report.propulsion.assist_force_newtons == V{} &&
                close(report.propulsion.world_linear_impulse_newton_seconds,
                      scale(expected, kSimulationStep.count()), 1e-10) &&
                close(state.position_metres, reference.position, 1e-8) &&
                close(state.linear_velocity_metres_per_second,
                      reference.velocity, 1e-11),
            "six directional channels compose real thrust with stage gravity");
    }
  }
  auto s = f.state();
  VacuumIntent opposed;
  opposed.positive_translation.x = .5;
  opposed.negative_translation.x = .5;
  const auto report =
      required(advance_central_body_dynamics(f.context, s, opposed));
  check(report.propulsion.positive_force_newtons.x > 0 &&
            report.propulsion.positive_force_newtons.x ==
                report.propulsion.negative_force_newtons.x &&
            report.propulsion.applied_force_newtons == V{} &&
            report.propulsion.world_linear_impulse_newton_seconds == V{} &&
            length(report.gravity_impulse_newton_seconds) > 0,
        "opposing firings retain gross channels while gravity remains "
        "environmental");
  s = f.state();
  VacuumIntent tiny;
  tiny.positive_translation.x = 1e-20;
  const auto small =
      required(advance_central_body_dynamics(f.context, s, tiny));
  check(small.propulsion.world_linear_impulse_newton_seconds.x > 0 &&
            near(small.propulsion.world_linear_impulse_newton_seconds.x,
                 1e-20 * craft.positive_force_newtons[0] *
                     kSimulationStep.count(),
                 1e-30),
        "tiny propulsion is computed directly rather than cancelled out of "
        "total minus gravity");
  for (const bool assistance : {false, true}) {
    s = f.state();
    s.orientation =
        required(normalize_rigid_orientation({.125, .375, .125, .875}));
    s.angular_velocity_radians_per_second = {.125, -.25, .375};
    auto neutral = s;
    neutral.orientation = {};
    neutral.angular_velocity_radians_per_second = {};
    for (unsigned i = 0; i < 240; ++i) {
      VacuumIntent intent;
      intent.assistance = assistance;
      const auto attitude = required(advance_vacuum_attitude(
          s.craft, s.orientation, s.angular_velocity_radians_per_second, {}, {},
          assistance));
      const auto mixed =
          required(advance_central_body_dynamics(f.context, s, intent));
      required(advance_central_body_dynamics(f.context, neutral, intent));
      check(
          s.orientation == attitude.orientation &&
              s.angular_velocity_radians_per_second ==
                  attitude.angular_velocity_radians_per_second &&
              mixed.propulsion.applied_torque_newton_metres ==
                  attitude.actuation.applied_torque_newton_metres &&
              s.position_metres == neutral.position_metres &&
              s.linear_velocity_metres_per_second ==
                  neutral.linear_velocity_metres_per_second,
          "mixed spin and rotational assistance never brake or cancel gravity");
    }
  }
}
auto invalid(const Fixture& f) -> void {
  const auto refuses = [&](RigidBodyState s, VacuumIntent intent = {},
                           CentralBodyDynamicsRecipe recipe = {},
                           SimulationSeconds step = kSimulationStep) {
    const auto before = s;
    check(!advance_central_body_dynamics(f.context, s, intent, recipe, step) &&
              same_bits(s, before),
          "invalid central-body step refuses without replacing any state bits");
  };
  refuses(f.state(), {}, {0});
  refuses(f.state(), {}, {2});
  for (const auto value : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(), -0.0}) {
    for (std::size_t i = 0; i < 13; ++i) {
      auto s = f.state();
      *scalars(s)[i] = value;
      refuses(s);
    }
  }
  for (const double step :
       {0.0, -1.0, .01, std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()})
    refuses(f.state(), {}, {}, SimulationSeconds{step});
  auto s = f.state();
  s.tick = std::numeric_limits<SimulationTick>::max() - 1;
  refuses(s);
  s = f.state();
  s.craft.version = 0;
  refuses(s);
  s = f.state();
  s.frame.system.value ^= 1;
  refuses(s);
  s = f.state();
  s.frame.planet = PlanetId{0};
  refuses(s);
  s = f.state();
  s.frame.planet.reset();
  refuses(s);
  s = f.state();
  s.frame.station = OriginStationId{1};
  refuses(s);
  for (const auto kind :
       {RigidFrameKind::system_inertial, RigidFrameKind::planet_fixed,
        RigidFrameKind::station_relative_inertial}) {
    s = f.state();
    s.frame.kind = kind;
    refuses(s);
  }
  for (const double radius :
       {0.0, .5, std::nextafter(kCentralBodyMinimumRadiusMetres, 0.0)}) {
    s = f.state();
    s.position_metres = {radius, 0, 0};
    const auto result = evaluate_central_body_gravity(f.context, s);
    check(!result && result.error().code == Code::invalid_radius,
          "singularity bound refuses before division");
    refuses(s);
  }
  s = f.state();
  s.position_metres = {1, 0, 0};
  check(evaluate_central_body_gravity(f.context, s).has_value(),
        "exact declared minimum radius is queryable");
  refuses(s); // finite gravity, but unsafe integrated result
  s = f.state();
  s.position_metres = {1000, 0, 0};
  s.linear_velocity_metres_per_second = {-240000, 0, 0};
  refuses(s); // an RK stage reaches the singularity
  s = f.state();
  s.position_metres = {kRigidBodyMaximumPositionMetres, 0, 0};
  s.linear_velocity_metres_per_second = {
      kRigidBodyMaximumVelocityMetresPerSecond, 0, 0};
  refuses(s);
  s = f.state();
  s.position_metres.x = std::nextafter(kRigidBodyMaximumPositionMetres,
                                       std::numeric_limits<double>::infinity());
  refuses(s);
  for (std::size_t i = 0; i < 12; ++i) {
    for (const double value :
         {-1.0, 1.01, std::numeric_limits<double>::quiet_NaN(),
          std::numeric_limits<double>::infinity()}) {
      VacuumIntent intent;
      std::array<V*, 4> channels{
          &intent.positive_translation, &intent.negative_translation,
          &intent.positive_rotation, &intent.negative_rotation};
      auto& v = *channels[i / 3];
      const std::array<double*, 3> axes{&v.x, &v.y, &v.z};
      *axes[i % 3] = value;
      refuses(f.state(), intent);
    }
  }
  const RigidBodyWorldContext unwrapped{f.system.catalog};
  s = f.state();
  const auto before = s;
  const auto wrong_owner = advance_central_body_dynamics(unwrapped, s, {});
  check(!wrong_owner && wrong_owner.error().code == Code::invalid_owner &&
            same_bits(s, before),
        "unwrapped matching catalog cannot impersonate physical owner");
  for (const bool radius : {false, true}) {
    auto forged = f.system;
    forged.catalog.planets.clear();
    for (const auto& body : f.system.catalog.planets) {
      const auto& d = body.descriptor;
      const PlanetDescriptor invalid{d.seed,
                                     d.id,
                                     d.display_name,
                                     radius ? PlanetRadiusKm{0} : d.radius,
                                     radius ? d.surface_gravity
                                            : SurfaceGravityMilliG{0},
                                     d.atmosphere_class,
                                     d.atmosphere_pressure,
                                     d.terrain_character,
                                     d.water_coverage,
                                     d.palette};
      forged.catalog.planets.push_back({invalid, body.orbit});
    }
    const RigidBodyWorldContext bad{forged};
    check(!evaluate_central_body_gravity(bad, s),
          "invalid generated radius/gravity refuses");
    check(!advance_central_body_dynamics(bad, s, {}) && same_bits(s, before),
          "forged physical body never replaces state");
  }
  check(!advance_vacuum_dynamics(f.context, s, {}, VacuumDynamicsRecipe{}),
        "ordinary vacuum remains unable to adopt the moving body frame");
}
} // namespace
auto main() -> int {
  try {
    const Fixture f;
    invalid(f); // numerical and ownership boundaries before trajectory checks
    for (const auto seed :
         {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}})
      for (const bool origin : {false, true})
        law(Fixture{seed, origin});
    orbits(f);
    channels(f);
    thrust_and_resume(f);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    ++failures;
  }
  std::cout << "central body dynamics: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
