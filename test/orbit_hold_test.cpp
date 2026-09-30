#include "apsis_drift/orbit_hold.hpp"

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
constexpr OrbitalTelemetryRecipe policy{1, 100000};
auto check(bool ok, std::string_view message) -> void {
  if (ok) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}
template <typename T, typename E>
auto required(std::expected<T, E> result) -> T {
  if (!result) throw std::runtime_error("required orbit-hold fixture failed");
  return *result;
}
auto dot(V a, V b) -> double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
auto length(V a) -> double {
  return std::sqrt(dot(a, a));
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
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
  const auto x = scalars(a), y = scalars(b);
  for (std::size_t i = 0; i < x.size(); ++i)
    if (std::bit_cast<std::uint64_t>(*x[i]) !=
        std::bit_cast<std::uint64_t>(*y[i]))
      return false;
  return a.craft == b.craft && a.frame == b.frame && a.tick == b.tick;
}
struct Fixture {
  PhysicalLocalSystem system{
      required(generate_physical_origin_system(Seed{42}))};
  RigidBodyWorldContext context{system};
  const PlanetDescriptor& planet{system.catalog.planets.front().descriptor};
  PhysicalPlanetRotationRecipe rotation{
      required(generate_planet_rotation_recipe(system, planet.id))};
  double surface{planet.radius.value * 1000.0};
  double radius{surface + 500000};
  double mu{9.80665 * planet.surface_gravity.value / 1000.0 * surface *
            surface};
  auto state() const -> RigidBodyState {
    RigidBodyState s;
    s.frame = {RigidFrameKind::planet_relative_inertial,
               system.catalog.id,
               planet.id,
               {}};
    s.position_metres = {radius, 0, 0};
    s.linear_velocity_metres_per_second = {0, std::sqrt(mu / radius), 0};
    s.tick = 25;
    return s;
  }
  auto request() const -> OrbitHoldRequest {
    return {1, OrbitHoldTarget{planet.id, radius, {0, 0, 1}}};
  }
  auto advance(RigidBodyState& s, const VacuumIntent& intent = {},
               OrbitHoldRequest request = {}) const -> OrbitHoldResult {
    return required(advance_orbit_hold_dynamics(context, s, intent, rotation,
                                                policy, request));
  }
};
auto ordinary_modes(const Fixture& f) -> void {
  for (const bool enabled : {false, true}) {
    for (const bool advanced : {false, true}) {
      for (const bool manual : {false, true}) {
        if (enabled && !advanced && !manual) continue;
        auto s = f.state(), expected = s;
        s.angular_velocity_radians_per_second = {.125, -.25, .375};
        expected = s;
        VacuumIntent intent;
        intent.assistance = !advanced;
        if (manual) intent.negative_translation.z = .25;
        const auto normal = required(
            advance_central_body_dynamics(f.context, expected, intent));
        const auto report =
            f.advance(s, intent, enabled ? f.request() : OrbitHoldRequest{});
        const auto status = !enabled ? OrbitHoldStatus::disabled
                            : advanced
                                ? OrbitHoldStatus::paused_advanced
                                : OrbitHoldStatus::paused_manual_translation;
        check(report.status == status && same_bits(s, expected) &&
                  report.requested_force_body_newtons == V{} &&
                  report.applied_force_body_newtons == V{} &&
                  report.actuation.propulsion.positive_force_newtons ==
                      normal.propulsion.positive_force_newtons &&
                  report.actuation.propulsion.negative_force_newtons ==
                      normal.propulsion.negative_force_newtons &&
                  report.actuation.propulsion
                          .world_linear_impulse_newton_seconds ==
                      normal.propulsion.world_linear_impulse_newton_seconds &&
                  report.actuation.gravity_impulse_newton_seconds ==
                      normal.gravity_impulse_newton_seconds,
              "disabled, Advanced and manual-override modes preserve exact "
              "ordinary dynamics");
        const auto query = required(
            evaluate_orbital_telemetry(f.context, s, f.rotation, policy));
        check(report.observation_after == query &&
                  report.orbit_established ==
                      (query.classification == OrbitClassification::stable),
              "orbit-established observation is shared and after the committed "
              "step");
      }
    }
  }
  for (const bool geometry : {false, true}) {
    auto s = f.state();
    if (geometry) {
      s.position_metres = {0, 0, f.radius};
      s.linear_velocity_metres_per_second = {};
    } else
      s.position_metres.x = f.surface + policy.space_boundary_altitude_metres;
    auto expected = s;
    required(advance_central_body_dynamics(f.context, expected, {}));
    const auto report = f.advance(s, {}, f.request());
    check(report.status == (geometry
                                ? OrbitHoldStatus::unavailable_geometry
                                : OrbitHoldStatus::unavailable_environment) &&
              same_bits(s, expected) &&
              report.applied_force_body_newtons == V{},
          "valid unavailable state pauses hold without freezing gravitational "
          "flight");
  }
}
auto authority(const Fixture& f) -> void {
  const auto craft = required(resolve_craft_frame(f.state().craft)).properties;
  for (const auto attitude :
       {RigidOrientation{}, RigidOrientation{.5, .5, -.5, .5}}) {
    auto s = f.state();
    s.orientation = attitude;
    s.linear_velocity_metres_per_second = {};
    const auto initial = s;
    const auto report = f.advance(s, {}, f.request());
    const double demand = std::sqrt(f.mu / f.radius) *
                          kOrbitHoldVelocityGainPerSecond * craft.dry_mass_kg;
    // q=(.5,.5,-.5,.5) cycles the owning +Y demand onto body -Z.
    const V wanted =
        attitude == RigidOrientation{} ? V{0, demand, 0} : V{0, 0, -demand};
    check(length(sub(report.requested_force_body_newtons, wanted)) < 1e-5,
          "controller demand projects an independent tangential acceleration "
          "into body axes");
    const auto& p = report.actuation.propulsion;
    const std::array<double, 3> positive{p.positive_force_newtons.x,
                                         p.positive_force_newtons.y,
                                         p.positive_force_newtons.z};
    const std::array<double, 3> negative{p.negative_force_newtons.x,
                                         p.negative_force_newtons.y,
                                         p.negative_force_newtons.z};
    for (std::size_t i = 0; i < 3; ++i)
      check(positive[i] >= 0 &&
                positive[i] <= craft.positive_force_newtons[i] &&
                negative[i] >= 0 &&
                negative[i] <= craft.negative_force_newtons[i],
            "hold is allocated to actual finite directional channel capacity");
    check(report.status == OrbitHoldStatus::saturated &&
              !report.orbit_established &&
              report.applied_force_body_newtons == p.applied_force_newtons &&
              length(p.applied_force_newtons) <
                  length(report.requested_force_body_newtons) &&
              s.tick == initial.tick + 1 &&
              length(sub(s.linear_velocity_metres_per_second,
                         initial.linear_velocity_metres_per_second)) < 10 &&
              length(s.position_metres) < f.radius,
          "insufficient authority reports saturation, actual correction and "
          "continuing descent without capture");
  }
  auto s = f.state();
  s.angular_velocity_radians_per_second = {.125, -.25, .375};
  const auto attitude = required(advance_vacuum_attitude(
      s.craft, s.orientation, s.angular_velocity_radians_per_second, {}, {},
      true));
  const auto result = f.advance(s, {}, f.request());
  check(s.orientation == attitude.orientation &&
            s.angular_velocity_radians_per_second ==
                attitude.angular_velocity_radians_per_second &&
            result.actuation.propulsion.applied_torque_newton_metres ==
                attitude.actuation.applied_torque_newton_metres,
        "orbit correction retains the existing bounded rotation stabilization "
        "and attitude kernel");
}
auto recovery(const Fixture& f) -> void {
  auto s = f.state();
  s.position_metres.x += 2000;
  s.position_metres.z = 200;
  s.linear_velocity_metres_per_second.x = 20;
  s.linear_velocity_metres_per_second.y *= .99;
  s.linear_velocity_metres_per_second.z = 5;
  const auto original = s;
  double propulsion_impulse{};
  unsigned saturated{};
  for (unsigned i = 0; i < 108000; ++i) {
    const auto result = f.advance(s, {}, f.request());
    if (result.status == OrbitHoldStatus::saturated) ++saturated;
    check(result.status == OrbitHoldStatus::active ||
              result.status == OrbitHoldStatus::saturated,
          "bounded perturbed orbit stays in the explicit hold domain");
    propulsion_impulse +=
        length(result.actuation.propulsion.world_linear_impulse_newton_seconds);
    check(result.applied_force_body_newtons ==
              result.actuation.propulsion.applied_force_newtons,
          "every correction remains an actual propulsion channel, not a hidden "
          "velocity reset");
  }
  const auto o =
      required(evaluate_orbital_telemetry(f.context, s, f.rotation, policy));
  std::cout << "hold recovery: radius error "
            << length(s.position_metres) - f.radius << " radial "
            << o.radial_rate_metres_per_second << " plane "
            << s.position_metres.z << " eccentricity " << o.eccentricity
            << " impulse " << propulsion_impulse << " saturated ticks "
            << saturated << '\n';
  check(std::abs(length(s.position_metres) - f.radius) < 1 &&
            std::abs(o.radial_rate_metres_per_second) < .01 &&
            std::abs(s.position_metres.z) < .1 && o.eccentricity < 1e-5 &&
            o.classification == OrbitClassification::stable &&
            propulsion_impulse > 0 && s.tick == original.tick + 108000,
        "900-second measured recovery uses bounded physical correction toward "
        "the selected orbit");
}
auto equilibrium(const Fixture& f) -> void {
  for (const double sign : {-1.0, 1.0}) {
    auto state = f.state();
    state.linear_velocity_metres_per_second.y *= sign;
    auto request = f.request();
    request.target->plane_normal.z = sign;
    double peak_force{};
    constexpr unsigned ticks = 36000;
    for (unsigned i = 0; i < ticks; ++i) {
      const auto result = f.advance(state, {}, request);
      check(result.status == OrbitHoldStatus::active &&
                result.orbit_established,
            "explicit prograde/retrograde circular equilibrium remains active "
            "and clear");
      peak_force =
          std::max(peak_force, length(result.applied_force_body_newtons));
    }
    const double angle = std::sqrt(f.mu / (f.radius * f.radius * f.radius)) *
                         ticks * kSimulationStep.count();
    check(length(sub(state.position_metres,
                     {f.radius * std::cos(angle),
                      sign * f.radius * std::sin(angle), 0})) < .01 &&
              std::abs(length(state.position_metres) - f.radius) < .01 &&
              peak_force < .01,
          "300-second ideal circular hold follows analytic phase with only "
          "bounded numerical correction");
  }
}
auto replay(const Fixture& f) -> void {
  auto a = f.state(), b = a;
  const auto selected = f.request();
  a.position_metres.x += 2000;
  b = a;
  for (unsigned i = 0; i < 1200; ++i) {
    VacuumIntent intent;
    intent.assistance = (i / 97) % 2 == 0;
    if (i > 700 && i < 800) intent.negative_translation.z = .25;
    if (i > 950) intent.positive_rotation.y = .125;
    if (i == 37 || i == 600 || i == 915)
      b = required(decode_rigid_body_state_json(
          f.context, required(encode_rigid_body_state_json(f.context, b))));
    const auto x = f.advance(a, intent, selected),
               y = f.advance(b, intent, selected);
    check(same_bits(a, b) && x.status == y.status &&
              x.requested_correction_metres_per_second_squared ==
                  y.requested_correction_metres_per_second_squared &&
              x.requested_force_body_newtons ==
                  y.requested_force_body_newtons &&
              x.applied_force_body_newtons == y.applied_force_body_newtons &&
              x.actuation.propulsion.world_linear_impulse_newton_seconds ==
                  y.actuation.propulsion.world_linear_impulse_newton_seconds &&
              x.actuation.gravity_impulse_newton_seconds ==
                  y.actuation.gravity_impulse_newton_seconds &&
              x.observation_after == y.observation_after &&
              x.orbit_established == y.orbit_established,
          "retained target/recipes/subsequent inputs and exact v3 hydration "
          "reproduce hold continuation");
  }
  // New explicitly selected hold v1 trace, agreed by GCC and Clang. All
  // earlier vacuum, central-body and rigid projection goldens stay untouched.
  check(required(rigid_body_state_checksum(f.context, a)) ==
            11305337061246386878ULL,
        "hold v1 mixed-mode continuation checksum golden");
}
auto invalid(const Fixture& f) -> void {
  const auto refuses = [&](OrbitHoldRequest request, VacuumIntent intent = {},
                           SimulationSeconds step = kSimulationStep) {
    auto s = f.state();
    const auto before = s;
    check(!advance_orbit_hold_dynamics(f.context, s, intent, f.rotation, policy,
                                       request, {}, step) &&
              same_bits(s, before),
          "invalid request/input/step refuses without replacing state");
  };
  refuses({0, {}});
  refuses({2, f.request().target});
  for (const double radius :
       {0.0, -1.0, f.surface, f.surface + policy.space_boundary_altitude_metres,
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        kRigidBodyMaximumPositionMetres * 2}) {
    auto r = f.request();
    r.target->radius_metres = radius;
    refuses(r);
  }
  auto r = f.request();
  r.target->planet.value ^= 1;
  refuses(r);
  for (const V normal : {V{}, V{1, 1, 1}, V{-0.0, 0, 1},
                         V{std::numeric_limits<double>::quiet_NaN(), 0, 1},
                         V{0, std::numeric_limits<double>::infinity(), 0}}) {
    r = f.request();
    r.target->plane_normal = normal;
    refuses(r);
  }
  VacuumIntent malformed;
  malformed.positive_translation.x = 2;
  refuses(f.request(), malformed);
  malformed = {};
  malformed.negative_translation.z = std::numeric_limits<double>::quiet_NaN();
  refuses(f.request(), malformed);
  malformed = {};
  malformed.positive_rotation.y = -1;
  refuses(f.request(), malformed);
  for (const double step :
       {0.0, -1.0, .01, std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity()})
    refuses(f.request(), {}, SimulationSeconds{step});
  for (const bool clock : {false, true}) {
    auto s = f.state();
    if (clock)
      s.tick = std::numeric_limits<SimulationTick>::max() - 1;
    else
      s.position_metres.x = std::numeric_limits<double>::infinity();
    const auto before = s;
    check(!advance_orbit_hold_dynamics(f.context, s, {}, f.rotation, policy,
                                       f.request()) &&
              same_bits(s, before),
          "malformed state or exhausted clock never partially advances hold");
  }
  auto s = f.state();
  const auto before = s;
  const RigidBodyWorldContext unwrapped{f.system.catalog};
  check(!advance_orbit_hold_dynamics(unwrapped, s, {}, f.rotation, policy,
                                     f.request()) &&
            same_bits(s, before),
        "unwrapped matching IDs cannot own physical hold");
  auto wrong = f.rotation;
  wrong.owner_version = 0;
  check(!advance_orbit_hold_dynamics(f.context, s, {}, wrong, policy,
                                     f.request()) &&
            same_bits(s, before),
        "wrong physical rotation owner refuses hold atomically");
  for (const double value : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(), -0.0}) {
    for (std::size_t i = 0; i < 13; ++i) {
      auto bad = f.state();
      *scalars(bad)[i] = value;
      const auto original = bad;
      check(!advance_orbit_hold_dynamics(f.context, bad, {}, f.rotation, policy,
                                         f.request()) &&
                same_bits(bad, original),
            "every malformed state scalar refuses hold without rewriting "
            "source bits");
    }
  }
  check(!advance_orbit_hold_dynamics(f.context, s, {}, f.rotation, {0, 100000},
                                     f.request()) &&
            same_bits(s, before) &&
            !advance_orbit_hold_dynamics(f.context, s, {}, f.rotation, policy,
                                         f.request(), {0}) &&
            same_bits(s, before),
        "policy and selected gravity versions are independently required");
}
} // namespace
auto main() -> int {
  try {
    const Fixture f;
    invalid(f);
    ordinary_modes(f);
    authority(f);
    equilibrium(f);
    recovery(f);
    replay(f);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    ++failures;
  }
  std::cout << "orbit hold: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
