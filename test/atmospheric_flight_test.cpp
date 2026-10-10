#include "apsis_drift/atmospheric_flight.hpp"

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
int failures{};
auto check(bool ok, std::string_view message) -> void {
  if (!ok) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
template <class T, class E> auto required(std::expected<T, E> x) -> T {
  if (!x) throw std::runtime_error("required atmospheric fixture failed");
  return *x;
}
auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(V a, double b) -> V {
  return {a.x * b, a.y * b, a.z * b};
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
auto near(double a, double b, double t) -> bool {
  return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= t;
}
auto close(V a, V b, double t) -> bool {
  return length(sub(a, b)) <= t;
}
// Independent matrix, not the implementation's Hamilton products.
auto rotate(RigidOrientation q, V v) -> V {
  return {
      (1 - 2 * (q.y * q.y + q.z * q.z)) * v.x +
          2 * (q.x * q.y - q.w * q.z) * v.y + 2 * (q.x * q.z + q.w * q.y) * v.z,
      2 * (q.x * q.y + q.w * q.z) * v.x +
          (1 - 2 * (q.x * q.x + q.z * q.z)) * v.y +
          2 * (q.y * q.z - q.w * q.x) * v.z,
      2 * (q.x * q.z - q.w * q.y) * v.x + 2 * (q.y * q.z + q.w * q.x) * v.y +
          (1 - 2 * (q.x * q.x + q.y * q.y)) * v.z};
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
  auto x = scalars(a), y = scalars(b);
  for (std::size_t i = 0; i < x.size(); ++i)
    if (std::bit_cast<std::uint64_t>(*x[i]) !=
        std::bit_cast<std::uint64_t>(*y[i]))
      return false;
  return a.tick == b.tick && a.craft == b.craft && a.frame == b.frame;
}
struct Fixture {
  explicit Fixture(Seed seed, bool origin = false)
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
    return (planet.surface_gravity.value * 9.80665 / 1000.0 * radius()) *
           radius();
  }
  auto spin() const -> V {
    auto x = required(resolve_planet_rotation(system, rotation, 25))
                 .geometry.angular_velocity_radians_per_second;
    return {x.x, x.y, x.z};
  }
  auto state(double altitude = 1000) const -> RigidBodyState {
    RigidBodyState s;
    s.tick = 25;
    s.frame = {RigidFrameKind::planet_relative_inertial,
               system.catalog.id,
               planet.id,
               {}};
    s.position_metres = {radius() + altitude, 0, 0};
    s.linear_velocity_metres_per_second =
        add(cross(spin(), s.position_metres), V{0, 0, -100});
    return required(canonicalize_rigid_body_state(context, s));
  }
  auto sample(const RigidBodyState& s, const VacuumIntent& i = {}) const
      -> AtmosphericFlightSample {
    return required(evaluate_atmospheric_flight(context, s, i, rotation));
  }
  auto advance(RigidBodyState& s, const VacuumIntent& i = {}) const
      -> AtmosphericFlightActuation {
    return required(advance_atmospheric_flight(context, s, i, rotation));
  }
};
auto profile_contract(const Fixture& f) -> void {
  const auto profile = required(resolve_atmospheric_flight_profile(f.planet));
  const auto sea = f.sample(f.state(0));
  check(profile.sea_level_density_kg_per_cubic_metre ==
                sea.density_kg_per_cubic_metre &&
            profile.sea_level_pressure_millibars == sea.pressure_millibars &&
            profile.space_boundary_altitude_metres ==
                sea.space_boundary_altitude_metres,
        "read-only profile matches actual atmospheric force sampling");
  for (const auto version :
       {0U, 2U, std::numeric_limits<std::uint32_t>::max()}) {
    const auto invalid =
        resolve_atmospheric_flight_profile(f.planet, {version});
    check(!invalid && invalid.error().code ==
                          AtmosphericFlightErrorCode::unsupported_version,
          "profile refuses unsupported recipes");
  }
}
auto invalid(const Fixture& f) -> void {
  const auto refuses = [&](RigidBodyState s, VacuumIntent intent = {},
                           PhysicalPlanetRotationRecipe rotation = {},
                           AtmosphericFlightRecipe recipe = {},
                           CentralBodyDynamicsRecipe gravity = {},
                           SimulationSeconds step = kSimulationStep) {
    if (rotation.rotation.planet == PlanetId{}) rotation = f.rotation;
    const auto before = s;
    check(!advance_atmospheric_flight(f.context, s, intent, rotation, recipe,
                                      gravity, step) &&
              same_bits(s, before),
          "invalid atmospheric step refuses atomically");
  };
  for (std::size_t i = 0; i < 13; ++i)
    for (double bad : {std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()}) {
      auto s = f.state();
      *scalars(s)[i] = bad;
      refuses(s);
    }
  auto s = f.state();
  s.position_metres = {0, 0, 0};
  refuses(s);
  s = f.state();
  s.frame.kind = RigidFrameKind::planet_fixed;
  refuses(s);
  s = f.state();
  s.craft.version = 99;
  refuses(s);
  s = f.state();
  s.tick = std::numeric_limits<SimulationTick>::max() - 1;
  refuses(s);
  for (double dt : {0., -.01, .1, std::numeric_limits<double>::quiet_NaN()})
    refuses(f.state(), {}, {}, {}, {}, SimulationSeconds{dt});
  refuses(f.state(), {}, {}, {99});
  refuses(f.state(), {}, {}, {}, {99});
  auto rotation = f.rotation;
  rotation.owner_version = 99;
  refuses(f.state(), {}, rotation);
  rotation = f.rotation;
  ++rotation.rotation.period_ticks;
  refuses(f.state(), {}, rotation);
  rotation = f.rotation;
  rotation.rotation.planet.value ^= 1;
  refuses(f.state(), {}, rotation);
  for (std::size_t i = 0; i < 12; ++i)
    for (double bad : {-.1, 1.1, std::numeric_limits<double>::quiet_NaN()}) {
      VacuumIntent intent;
      std::array<double*, 12> xs{
          &intent.positive_translation.x, &intent.positive_translation.y,
          &intent.positive_translation.z, &intent.negative_translation.x,
          &intent.negative_translation.y, &intent.negative_translation.z,
          &intent.positive_rotation.x,    &intent.positive_rotation.y,
          &intent.positive_rotation.z,    &intent.negative_rotation.x,
          &intent.negative_rotation.y,    &intent.negative_rotation.z};
      *xs[i] = bad;
      refuses(f.state(), intent);
    }
  auto legacy = RigidBodyWorldContext{f.system.catalog};
  s = f.state();
  auto before = s;
  check(!advance_atmospheric_flight(legacy, s, {}, f.rotation) &&
            same_bits(s, before),
        "embedded legacy catalog cannot authorize atmospheric flight");
  s = f.state();
  s.orientation = {1, 0, -0., 0};
  refuses(s);
  s = f.state();
  s.angular_velocity_radians_per_second.x = 2e4;
  refuses(s);
}
auto environment(const Fixture& f) -> void {
  auto s = f.state();
  const auto sample = f.sample(s);
  const bool airless = f.planet.atmosphere_class == AtmosphereClass::airless;
  check(close(sample.air_relative_velocity_metres_per_second, {0, 0, -100},
              1e-12),
        "air velocity subtracts authoritative co-rotation");
  check(near(sample.radial_rate_metres_per_second,
             s.linear_velocity_metres_per_second.x, 1e-12),
        "radial velocity remains owning-frame telemetry");
  if (airless) {
    check(sample.density_kg_per_cubic_metre == 0 &&
              sample.drag_force_body_newtons == V{} &&
              sample.lift_force_body_newtons == V{} &&
              sample.passive_torque_body_newton_metres == V{} &&
              sample.space_boundary_altitude_metres == 0,
          "airless has exactly zero aerodynamics");
    return;
  }
  const double height =
      f.planet.atmosphere_class == AtmosphereClass::tenuous     ? 6000
      : f.planet.atmosphere_class == AtmosphereClass::temperate ? 8500
                                                                : 11000;
  const double rho0 = 1.225 * f.planet.atmosphere_pressure.value / 1013.25;
  check(near(sample.density_kg_per_cubic_metre, rho0 * std::exp(-1000 / height),
             1e-14) &&
            near(sample.space_boundary_altitude_metres,
                 height * std::log(rho0 / 1e-6), 1e-9),
        "class/pressure isothermal profile has independently derived finite "
        "edge");
  double previous = std::numeric_limits<double>::infinity();
  for (double h :
       {0., 1000., 10000., sample.space_boundary_altitude_metres - height,
        sample.space_boundary_altitude_metres - .1,
        sample.space_boundary_altitude_metres,
        sample.space_boundary_altitude_metres + 1}) {
    auto t = f.state(h);
    auto x = f.sample(t);
    check(x.density_kg_per_cubic_metre >= 0 &&
              x.density_kg_per_cubic_metre <= previous,
          "density decreases through C1 boundary to exact zero");
    previous = x.density_kg_per_cubic_metre;
  }
  auto q = f.state(sample.space_boundary_altitude_metres + .1);
  check(f.sample(q).density_kg_per_cubic_metre == 0,
        "above finite edge is exact vacuum");
  double last_drag = 0, last_authority = 0;
  for (double speed : {0., 10., 100., 300.}) {
    auto t = f.state();
    t.linear_velocity_metres_per_second =
        add(cross(f.spin(), t.position_metres), V{0, 0, -speed});
    VacuumIntent intent;
    intent.positive_rotation.x = .5;
    auto x = f.sample(t, intent);
    const double drag = length(x.drag_force_body_newtons);
    check(drag >= last_drag && x.control_authority_fraction.x >= last_authority,
          "drag and control authority increase with air speed");
    last_drag = drag;
    last_authority = x.control_authority_fraction.x;
    check(near(drag, .5 * x.density_kg_per_cubic_metre * 3.84 * speed * speed,
               1e-7) &&
              x.control_authority_fraction.x <= .35,
          "immutable forward Cd*A and bounded surface authority");
  }
  auto tilted = f.state();
  tilted.orientation = {.5, .5, -.5, .5};
  for (V air : {V{40, -20, -100}, V{-40, 20, 100}, V{0, 100, 0}}) {
    tilted.linear_velocity_metres_per_second =
        add(cross(f.spin(), tilted.position_metres),
            rotate(tilted.orientation, air));
    auto x = f.sample(tilted);
    check(dot(x.drag_force_body_newtons, air) < 0 &&
              std::abs(dot(x.lift_force_body_newtons, air)) < 1e-6,
          "drag dissipates and lift is perpendicular in independently rotated "
          "axes");
  }
  auto lift = f.state();
  lift.linear_velocity_metres_per_second =
      add(cross(f.spin(), lift.position_metres), V{0, -10, -100});
  auto x = f.sample(lift);
  check(x.lift_force_body_newtons.y > 0 &&
            x.passive_torque_body_newton_metres.x < 0,
        "positive angle produces up lift and restoring nose-down torque");
  auto high = f.state();
  high.linear_velocity_metres_per_second =
      add(cross(f.spin(), high.position_metres), V{0, 0, -1000});
  check(!f.sample(high).within_rated_envelope,
        "outside speed envelope remains explicit");
  high = f.state();
  high.angular_velocity_radians_per_second.y = 2;
  check(!f.sample(high).within_rated_envelope,
        "outside angular-rate envelope remains explicit");
}
auto vacuum_parity(const Fixture& f) -> void {
  const double edge = f.sample(f.state()).space_boundary_altitude_metres;
  for (bool assist : {false, true}) {
    auto a = f.state(edge + 10000), b = a;
    a.angular_velocity_radians_per_second = {.125, -.25, .375};
    b = a;
    VacuumIntent intent;
    intent.assistance = assist;
    intent.negative_translation.z = .125;
    intent.positive_rotation.y = .1;
    for (unsigned n = 0; n < 240; ++n) {
      auto x = f.advance(a, intent);
      auto y = required(advance_central_body_dynamics(f.context, b, intent));
      check(
          same_bits(a, b) &&
              x.aerodynamic_linear_impulse_newton_seconds == V{} &&
              x.aerodynamic_angular_impulse_newton_metre_seconds == V{} &&
              x.central.gravity_impulse_newton_seconds ==
                  y.gravity_impulse_newton_seconds &&
              x.central.propulsion.world_linear_impulse_newton_seconds ==
                  y.propulsion.world_linear_impulse_newton_seconds &&
              x.central.propulsion.world_angular_impulse_newton_metre_seconds ==
                  y.propulsion.world_angular_impulse_newton_metre_seconds,
          "vacuum atmospheric composition retains exact central-body "
          "state/impulse parity");
    }
  }
}
auto forces_and_control(const Fixture& f) -> void {
  auto a = f.state(), b = a;
  VacuumIntent i;
  i.assistance = false;
  auto x = f.advance(a, i);
  const auto delta = scale(sub(a.linear_velocity_metres_per_second,
                               b.linear_velocity_metres_per_second),
                           8000);
  check(close(delta, x.central.total_linear_impulse_newton_seconds, 1e-7) &&
            close(delta,
                  add(add(x.central.gravity_impulse_newton_seconds,
                          x.aerodynamic_linear_impulse_newton_seconds),
                      x.central.propulsion.world_linear_impulse_newton_seconds),
                  1e-7),
        "gravity/aero/actual propulsion impulses compose once");
  check(x.central.propulsion.positive_force_newtons == V{} &&
            x.central.propulsion.negative_force_newtons == V{} &&
            x.central.propulsion.world_linear_impulse_newton_seconds == V{},
        "neutral aero/gravity never consume propulsion channels");
  a = f.state();
  b = a;
  a.angular_velocity_radians_per_second = {0, 0, .3};
  b = a;
  auto initial = f.sample(a);
  check(initial.passive_torque_body_newton_metres.z < 0,
        "passive roll damping opposes relative spin");
  VacuumIntent advanced;
  advanced.assistance = false;
  VacuumIntent assisted;
  for (unsigned n = 0; n < 120; ++n) {
    f.advance(a, advanced);
    f.advance(b, assisted);
  }
  check(std::abs(a.angular_velocity_radians_per_second.z) < .3 &&
            std::abs(b.angular_velocity_radians_per_second.z) <
                std::abs(a.angular_velocity_radians_per_second.z),
        "Advanced keeps passive damping, Assisted adds bounded physical "
        "stabilization");
  for (unsigned axis = 0; axis < 3; ++axis) {
    a = f.state();
    b = a;
    VacuumIntent command;
    command.assistance = false;
    std::array<double*, 3> channels{&command.positive_rotation.x,
                                    &command.positive_rotation.y,
                                    &command.positive_rotation.z};
    *channels[axis] = .2;
    const auto report = f.advance(a, command);
    const std::array rates{a.angular_velocity_radians_per_second.x,
                           a.angular_velocity_radians_per_second.y,
                           a.angular_velocity_radians_per_second.z};
    check(rates[axis] > 0 && report.initial.control_authority_fraction.x >= 0,
          "same signed pitch/yaw/roll commands retain bounded authority");
  }
  // Gross opposing physical channels stay billable even with zero net force.
  a = f.state();
  VacuumIntent opposed;
  opposed.positive_translation.x = .2;
  opposed.negative_translation.x = .2;
  x = f.advance(a, opposed);
  check(x.central.propulsion.positive_force_newtons.x == 22400 &&
            x.central.propulsion.negative_force_newtons.x == 22400 &&
            x.central.propulsion.applied_force_newtons.x == 0,
        "opposing atmospheric propulsion stays gross and separate");
}
auto rated_home_and_limits() -> void {
  for (Seed seed :
       {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}}) {
    Fixture f{seed, true};
    auto state = f.state();
    check(
        f.sample(state).within_rated_envelope,
        "generated physical home forward approach is inside declared envelope");
    for (double sign : {-1., 1.}) {
      auto s = state;
      VacuumIntent command;
      command.positive_rotation.x = sign > 0 ? .05 : 0;
      command.negative_rotation.x = sign < 0 ? .05 : 0;
      for (unsigned n = 0; n < 60; ++n)
        f.advance(s, command);
      check(sign * s.angular_velocity_radians_per_second.x > 0 &&
                f.sample(s).within_rated_envelope,
            "bounded assisted pitch remains controllable inside home envelope");
    }
    state.tick = f.rotation.rotation.period_ticks - 1;
    auto hydrated = required(decode_rigid_body_state_json(
        f.context, required(encode_rigid_body_state_json(f.context, state))));
    auto a = f.advance(state), b = f.advance(hydrated);
    check(same_bits(state, hydrated) && a.after == b.after,
          "phase wrap preserves physical owner and atmospheric continuation");
    state = f.state();
    state.tick = std::numeric_limits<SimulationTick>::max() - 2;
    check(advance_atmospheric_flight(f.context, state, {}, f.rotation)
              .has_value(),
          "last supported atmospheric tick is finite");
    state = f.state();
    state.position_metres = {kRigidBodyMaximumPositionMetres, 0, 0};
    state.linear_velocity_metres_per_second = {0, 0, 0};
    const auto sample = f.sample(state);
    check(sample.density_kg_per_cubic_metre == 0 &&
              sample.drag_force_body_newtons == V{},
          "maximum finite position observes exact vacuum without overflow");
  }
}
// Independent scalar radial RK4 oracle at the rotating pole, reverse flight:
// no lift/weathercock; axial Cd*A=3.84. Principal-axis rotation leaves the
// radial drag direction invariant. The oracle takes eight shorter substeps.
struct Radial {
  double r, v;
};
auto oracle(const Fixture& f, Radial s, double dt) -> Radial {
  const double rho0 = 1.225 * f.planet.atmosphere_pressure.value / 1013.25;
  const double height =
      f.planet.atmosphere_class == AtmosphereClass::tenuous     ? 6000
      : f.planet.atmosphere_class == AtmosphereClass::temperate ? 8500
                                                                : 11000;
  const double edge = height * std::log(rho0 / 1e-6);
  auto derivative = [&](Radial x) -> Radial {
    const double altitude = std::max(0., x.r - f.radius());
    double rho = 0;
    if (altitude < edge) {
      double t = std::clamp((edge - altitude) / height, 0., 1.);
      rho = rho0 * std::exp(-altitude / height) * t * t * (3 - 2 * t);
    }
    return {x.v, -f.mu() / (x.r * x.r) -
                     .5 * rho * 3.84 * x.v * std::abs(x.v) / 8000};
  };
  auto offset = [](Radial a, Radial b, double h) -> Radial {
    return {a.r + h * b.r, a.v + h * b.v};
  };
  for (unsigned n = 0; n < 8; ++n) {
    double h = dt / 8;
    auto a = derivative(s), b = derivative(offset(s, a, h / 2)),
         c = derivative(offset(s, b, h / 2)), d = derivative(offset(s, c, h));
    s = {s.r + h / 6 * (a.r + 2 * b.r + 2 * c.r + d.r),
         s.v + h / 6 * (a.v + 2 * b.v + 2 * c.v + d.v)};
  }
  return s;
}
auto radial_oracle(const Fixture& f) -> void {
  auto s = f.state(1000);
  const auto geometry =
      required(resolve_planet_rotation(f.system, f.rotation, s.tick)).geometry;
  s.orientation = geometry.fixed_to_system;
  const V pole = scale(f.spin(), 1 / length(f.spin()));
  s.position_metres = scale(pole, f.radius() + 1000);
  s.linear_velocity_metres_per_second = scale(pole, 200);
  s.angular_velocity_radians_per_second = {0, 0, length(f.spin())};
  s = required(canonicalize_rigid_body_state(f.context, s));
  Radial expected{f.radius() + 1000, 200};
  VacuumIntent intent;
  intent.assistance = false;
  for (unsigned n = 0; n < 1200; ++n) {
    auto report = f.advance(s, intent);
    expected = oracle(f, expected, kSimulationStep.count());
    check(report.initial.lift_force_body_newtons == V{},
          "radial reverse oracle has no invented lift");
  }
  check(near(length(s.position_metres), expected.r, .001) &&
            near(dot(s.linear_velocity_metres_per_second, pole), expected.v,
                 1e-6),
        "full stage composition agrees with independent eight-substep radial "
        "oracle");
}
auto replay_and_boundary(const Fixture& f) -> void {
  const double edge = f.sample(f.state()).space_boundary_altitude_metres;
  for (double direction : {-1., 1.}) {
    auto a = f.state(edge - direction * .5), b = a;
    a.linear_velocity_metres_per_second =
        add(cross(f.spin(), a.position_metres), V{direction * 1000, 0, -50});
    b = a;
    VacuumIntent intent;
    intent.assistance = false;
    bool crossed = false;
    for (unsigned n = 0; n < 120; ++n) {
      if (n == 17 || n == 70)
        b = required(decode_rigid_body_state_json(
            f.context, required(encode_rigid_body_state_json(f.context, b))));
      auto before = a;
      auto x = f.advance(a, intent), y = f.advance(b, intent);
      check(same_bits(a, b) && x.initial == y.initial && x.after == y.after &&
                x.observation_after == y.observation_after &&
                x.aerodynamic_linear_impulse_newton_seconds ==
                    y.aerodynamic_linear_impulse_newton_seconds,
            "boundary v3 hydration retains exact observations and "
            "environmental impulses");
      const double altitude = length(a.position_metres) - f.radius();
      crossed = crossed || direction * (altitude - edge) > 0;
      check(length(sub(a.position_metres, before.position_metres)) < 20 &&
                length(sub(a.linear_velocity_metres_per_second,
                           before.linear_velocity_metres_per_second)) < 1,
            "entry/exit have bounded integrated pose and velocity, no "
            "clamp/reset");
      const auto observed = required(
          evaluate_orbital_telemetry(f.context, a, f.rotation, {1, edge}));
      check(x.observation_after == observed,
            "classification uses the same derived atmosphere boundary");
    }
    check(crossed, "explicit inbound/outbound trace crosses finite boundary");
  }
  auto a = f.state(), b = a;
  for (unsigned n = 0; n < 600; ++n) {
    VacuumIntent i;
    i.assistance = (n / 97) % 2 == 0;
    if (n > 150 && n < 300) i.negative_translation.z = .2;
    if (n > 330) i.positive_rotation.y = .1;
    if (n == 35 || n == 305 || n == 450)
      b = required(decode_rigid_body_state_json(
          f.context, required(encode_rigid_body_state_json(f.context, b))));
    auto x = f.advance(a, i), y = f.advance(b, i);
    check(same_bits(a, b) && x.after == y.after &&
              x.observation_after == y.observation_after &&
              x.aerodynamic_angular_impulse_newton_metre_seconds ==
                  y.aerodynamic_angular_impulse_newton_metre_seconds,
          "high-angle/approach/ascent continuation keeps explicit recipes and "
          "input, no hidden history");
  }
  constexpr std::array<std::uint64_t, 4> goldens{
      11156515336560805162ULL, 8356589538494115423ULL, 1777814090012443372ULL,
      1369238175830983921ULL};
  check(required(rigid_body_state_checksum(f.context, a)) ==
            goldens[static_cast<std::size_t>(f.planet.atmosphere_class)],
        "new atmospheric-flight v1 fixed-class exact replay golden");
}
} // namespace
int main() {
  try {
    rated_home_and_limits();
    constexpr std::array seeds{Seed{12}, Seed{4}, Seed{0}, Seed{3}};
    for (const auto seed : seeds) {
      Fixture f{seed};
      profile_contract(f);
      invalid(f);
      environment(f);
      vacuum_parity(f);
      if (f.planet.atmosphere_class != AtmosphereClass::airless) {
        forces_and_control(f);
        radial_oracle(f);
      }
      replay_and_boundary(f);
    }
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 2;
  }
  return failures ? 1 : 0;
}
