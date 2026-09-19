#include "apsis_drift/vacuum_dynamics.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
using namespace apsis_drift;
int failures{};

auto check(bool condition, std::string_view message) -> void {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}
auto near(double actual, double expected, double tolerance,
          std::string_view message) -> void {
  check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
        message);
}
struct Fixture {
  LocalSystemDescriptor system{generate_origin_system(Seed{42})};
  OriginStationDescriptor station{generate_origin_station(Seed{42})};
  RigidBodyWorldContext context{system, &station};
  auto state() const -> RigidBodyState {
    RigidBodyState result;
    result.frame.system = system.id;
    return result;
  }
};
auto axes(RigidVector3& value) -> std::array<double*, 3> {
  return {&value.x, &value.y, &value.z};
}
auto values(RigidVector3 value) -> std::array<double, 3> {
  return {value.x, value.y, value.z};
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
  const auto aa = scalars(a), bb = scalars(b);
  for (std::size_t i = 0; i < aa.size(); ++i)
    if (std::bit_cast<std::uint64_t>(*aa[i]) !=
        std::bit_cast<std::uint64_t>(*bb[i]))
      return false;
  return a.craft == b.craft && a.frame == b.frame && a.tick == b.tick;
}
auto dot(RigidVector3 a, RigidVector3 b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto cross(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto rotate(RigidOrientation q, RigidVector3 v) -> RigidVector3 {
  // Independent Hamilton-vector expansion, not the integrator's helper.
  const RigidVector3 u{q.x, q.y, q.z};
  const auto c = cross(u, v);
  const double uv = dot(u, v), uu = dot(u, u);
  return {2 * uv * u.x + (q.w * q.w - uu) * v.x + 2 * q.w * c.x,
          2 * uv * u.y + (q.w * q.w - uu) * v.y + 2 * q.w * c.y,
          2 * uv * u.z + (q.w * q.w - uu) * v.z + 2 * q.w * c.z};
}
auto momentum(const RigidBodyState& state) -> RigidVector3 {
  const auto& inertia =
      starter_shuttle_frame().properties.principal_inertia_kg_m2;
  const auto w = state.angular_velocity_radians_per_second;
  return rotate(state.orientation, {w.x * static_cast<double>(inertia[0]),
                                    w.y * static_cast<double>(inertia[1]),
                                    w.z * static_cast<double>(inertia[2])});
}
auto energy(const RigidBodyState& state) -> double {
  const auto& inertia =
      starter_shuttle_frame().properties.principal_inertia_kg_m2;
  const auto w = values(state.angular_velocity_radians_per_second);
  return .5 * ((static_cast<double>(inertia[0]) * w[0] * w[0] +
                static_cast<double>(inertia[1]) * w[1] * w[1]) +
               static_cast<double>(inertia[2]) * w[2] * w[2]);
}
auto coast_intent() -> VacuumIntent {
  VacuumIntent intent;
  intent.assistance = false;
  return intent;
}

auto axis_contract(const Fixture& fixture) -> void {
  const auto& p = starter_shuttle_frame().properties;
  const double dt = kSimulationStep.count();
  for (std::size_t axis = 0; axis < 3; ++axis) {
    for (const bool positive : {false, true}) {
      auto state = fixture.state();
      auto intent = coast_intent();
      *axes(positive ? intent.positive_translation
                     : intent.negative_translation)[axis] = 1;
      const auto result =
          advance_vacuum_dynamics(fixture.context, state, intent);
      check(result.has_value(), "every signed translation axis advances");
      if (!result) continue;
      const double force =
          static_cast<double>(positive ? p.positive_force_newtons[axis]
                                       : p.negative_force_newtons[axis]) *
          (positive ? 1 : -1);
      for (std::size_t observed = 0; observed < 3; ++observed) {
        const double acceleration =
            observed == axis ? force / p.dry_mass_kg : 0;
        near(values(state.linear_velocity_metres_per_second)[observed],
             acceleration * dt, 1e-13,
             "signed force/mass determines correct velocity axis");
        near(values(state.position_metres)[observed],
             .5 * acceleration * dt * dt, 1e-13,
             "RK4 constant acceleration has analytic displacement");
        near(values(result->applied_force_newtons)[observed],
             observed == axis ? force : 0, 1e-10,
             "translation telemetry records physical net force");
      }
      check(state.tick == 1 && state.orientation == RigidOrientation{},
            "translation advances one tick without rotating");
      state = fixture.state();
      intent = coast_intent();
      *axes(positive ? intent.positive_rotation
                     : intent.negative_rotation)[axis] = 1;
      const auto rotational =
          advance_vacuum_dynamics(fixture.context, state, intent);
      check(rotational.has_value(), "every signed rotation axis advances");
      if (!rotational) continue;
      const double torque = static_cast<double>(p.torque_newton_metres[axis]) *
                            (positive ? 1 : -1);
      const double expected =
          torque / static_cast<double>(p.principal_inertia_kg_m2[axis]) * dt;
      near(values(state.angular_velocity_radians_per_second)[axis], expected,
           1e-10,
           "principal-axis angular acceleration uses matching principal "
           "inertia");
      near(values(rotational->applied_torque_newton_metres)[axis], torque, 1e-8,
           "signed torque telemetry preserves body axis convention");
      check(state.linear_velocity_metres_per_second == RigidVector3{} &&
                state.position_metres == RigidVector3{},
            "rotation does not invent translational forces");
    }
  }
  auto state = fixture.state();
  state.orientation = {.5, .5, .5, .5}; // right->up->back->right
  auto intent = coast_intent();
  intent.positive_translation.x = 1;
  const auto tilted = advance_vacuum_dynamics(fixture.context, state, intent);
  check(tilted.has_value(), "tilted body thrust advances");
  near(state.linear_velocity_metres_per_second.x, 0, 1e-13,
       "tilted thrust leaves original axis");
  near(state.linear_velocity_metres_per_second.y, 14 * dt, 1e-13,
       "body X thrust rotates into world Y");
  near(state.linear_velocity_metres_per_second.z, 0, 1e-13,
       "tilted thrust has no spurious Z");
}

auto opposed_contract(const Fixture& fixture) -> void {
  auto state = fixture.state();
  auto intent = coast_intent();
  intent.positive_translation = {1, 0, 1};
  intent.negative_translation = {1, 0, .2}; // equal physical retro/main force
  intent.positive_rotation = {1, 1, 1};
  intent.negative_rotation = {1, 1, 1};
  const auto result = advance_vacuum_dynamics(fixture.context, state, intent);
  check(result.has_value(), "equal opposed actuator forces are legal");
  if (!result) return;
  check(state.position_metres == RigidVector3{} &&
            state.linear_velocity_metres_per_second == RigidVector3{} &&
            state.angular_velocity_radians_per_second == RigidVector3{} &&
            state.orientation == RigidOrientation{},
        "equal opposing physical forces and torques cancel exactly");
  check(
      result->positive_force_newtons.x == 112000 &&
          result->negative_force_newtons.x == 112000 &&
          result->positive_force_newtons.z == 72000 &&
          result->negative_force_newtons.z == 72000 &&
          result->positive_torque_newton_metres.x > 0 &&
          result->negative_torque_newton_metres.x > 0,
      "gross actuator activity remains visible despite zero net acceleration");
  state = fixture.state();
  intent = coast_intent();
  intent.positive_translation.z = 1;
  intent.negative_translation.z = 1;
  const auto unequal = advance_vacuum_dynamics(fixture.context, state, intent);
  check(unequal.has_value(), "equal fractions on asymmetric engines advance");
  near(state.linear_velocity_metres_per_second.z, -36 * kSimulationStep.count(),
       1e-13,
       "equal main/retro fractions retain documented stronger main thrust");
}

auto analytic_golden_contract(const Fixture& fixture) -> void {
  auto state = fixture.state();
  auto intent = coast_intent();
  intent.positive_translation.x = 1;
  const auto step = advance_vacuum_dynamics(fixture.context, state, intent);
  check(step.has_value(), "analytic one-tick constant-force golden advances");
  // Independently evaluated binary64 RK4 constant acceleration and documented
  // little-endian rigid-state FNV recipe; not recorded from this integrator.
  auto expected = fixture.state();
  expected.tick = 1;
  expected.position_metres.x = 0x1.fdb97530eca86p-12;
  expected.linear_velocity_metres_per_second.x = 0x1.ddddddddddddep-4;
  check(same_bits(state, expected),
        "constant force has exact independent binary64 state golden");
  const auto checksum = rigid_body_state_checksum(fixture.context, state);
  check(checksum && *checksum == 9785537037076653852ULL,
        "one-tick constant-force state has independent exact checksum golden");
}

auto assist_contract(const Fixture& fixture) -> void {
  auto state = fixture.state();
  state.linear_velocity_metres_per_second = {1000, -1000, 123};
  state.angular_velocity_radians_per_second = {.3, -.4, .2};
  const auto before = state;
  const auto result =
      advance_vacuum_dynamics(fixture.context, state, VacuumIntent{});
  check(result.has_value(), "assisted large lateral motion advances");
  if (!result) return;
  check(result->assistance && result->force_saturated,
        "assist reports limited available thrust rather than free damping");
  check(result->assist_force_newtons.x < 0 &&
            result->assist_force_newtons.y > 0,
        "assist opposes lateral and vertical motion");
  check(result->assist_force_newtons.z == 0 &&
            result->applied_force_newtons.z == 0,
        "assist has no body-forward speed hold");
  check(std::abs(state.linear_velocity_metres_per_second.x) > 999 &&
            std::abs(state.linear_velocity_metres_per_second.y) > 999 &&
            energy(state) < energy(before),
        "assist changes momentum gradually and damps modest spin");
  const auto& p = starter_shuttle_frame().properties;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    check(values(result->positive_force_newtons)[axis] <=
                  p.positive_force_newtons[axis] &&
              values(result->negative_force_newtons)[axis] <=
                  p.negative_force_newtons[axis] &&
              values(result->positive_torque_newton_metres)[axis] <=
                  p.torque_newton_metres[axis] &&
              values(result->negative_torque_newton_metres)[axis] <=
                  p.torque_newton_metres[axis],
          "every gross assist actuator remains within immutable authority");
  }
  state = fixture.state();
  state.linear_velocity_metres_per_second = {0, 0, -500};
  for (int i = 0; i < 120; ++i)
    check(advance_vacuum_dynamics(fixture.context, state, VacuumIntent{})
              .has_value(),
          "assisted longitudinal coast advances");
  check(state.linear_velocity_metres_per_second.z == -500,
        "assistance preserves neutral longitudinal velocity over time");
  state = fixture.state();
  state.linear_velocity_metres_per_second.x = 5;
  auto commanded = VacuumIntent{};
  commanded.positive_translation.x = .25;
  const auto driven =
      advance_vacuum_dynamics(fixture.context, state, commanded);
  check(driven && driven->assist_force_newtons.x == 0,
        "explicit lateral demand suppresses neutral-axis translational hold");
  state = fixture.state();
  commanded = VacuumIntent{};
  commanded.positive_rotation.x = .25;
  for (int tick = 0; tick < 120; ++tick)
    check(
        advance_vacuum_dynamics(fixture.context, state, commanded).has_value(),
        "assisted angular-rate command advances");
  near(state.angular_velocity_radians_per_second.x, .25 * 1.15, 1e-10,
       "assisted rotation converges to fraction of frame-rated angular target");
  state = fixture.state();
  state.angular_velocity_radians_per_second.x = 2;
  commanded.positive_rotation.x = 1;
  commanded.negative_rotation.x = .5;
  const auto reserved =
      advance_vacuum_dynamics(fixture.context, state, commanded);
  check(reserved && reserved->torque_saturated &&
            reserved->positive_torque_newton_metres.x == 364000 &&
            reserved->negative_torque_newton_metres.x == 728000 &&
            reserved->applied_torque_newton_metres.x == -364000,
        "assist reserves opposed gross firing and saturates only remaining "
        "torque capacity");
  state = fixture.state();
  state.angular_velocity_radians_per_second.x = 2;
  commanded.negative_rotation.x = 0;
  const auto reversal =
      advance_vacuum_dynamics(fixture.context, state, commanded);
  check(reversal && reversal->requested_torque_newton_metres.x == 728000 &&
            reversal->applied_torque_newton_metres.x == -728000 &&
            reversal->assist_torque_newton_metres.x == -1456000 &&
            reversal->positive_torque_newton_metres.x == 0 &&
            reversal->negative_torque_newton_metres.x == 728000,
        "assist delta is not gross fuel activity when stabilizer reverses "
        "requested torque");
}

auto conservation_contract(const Fixture& fixture) -> void {
  auto state = fixture.state();
  state.linear_velocity_metres_per_second = {12.5, -3.25, 80};
  const auto coast = coast_intent();
  for (int i = 0; i < 12000; ++i) {
    const auto result = advance_vacuum_dynamics(fixture.context, state, coast);
    if (!result) {
      check(false, "100-second inertial linear trace advances");
      return;
    }
    check(result->applied_force_newtons == RigidVector3{} &&
              result->applied_torque_newton_metres == RigidVector3{},
          "neutral unassisted coast uses no hidden actuators");
  }
  check(state.linear_velocity_metres_per_second ==
            RigidVector3{12.5, -3.25, 80},
        "unassisted coast preserves every linear velocity bit");
  near(state.position_metres.x, 1250, 1e-8, "long linear coast displacement X");
  near(state.position_metres.y, -325, 1e-8, "long linear coast displacement Y");
  near(state.position_metres.z, 8000, 1e-8, "long linear coast displacement Z");
  for (std::size_t axis = 0; axis < 3; ++axis) {
    state = fixture.state();
    *axes(state.angular_velocity_radians_per_second)[axis] = .4;
    for (int i = 0; i < 2400; ++i) {
      if (!advance_vacuum_dynamics(fixture.context, state, coast)) {
        check(false, "principal spin advances without attitude stabilization");
        return;
      }
    }
    near(values(state.angular_velocity_radians_per_second)[axis], .4, 1e-10,
         "unassisted principal-axis spin is not secretly damped");
  }
  state = fixture.state();
  state.orientation = {.5, .5, .5, .5};
  state.angular_velocity_radians_per_second = {.31, -.47, .29};
  const auto initial = state;
  const auto initial_momentum = momentum(initial);
  const double initial_energy = energy(initial);
  for (int i = 0; i < 12000; ++i) {
    if (!advance_vacuum_dynamics(fixture.context, state, coast)) {
      check(false, "100-second asymmetric free tumble advances");
      return;
    }
    if (i % 120 == 119) {
      const auto current = momentum(state);
      for (std::size_t axis = 0; axis < 3; ++axis)
        near(values(current)[axis], values(initial_momentum)[axis], 1e-5,
             "free tumble conserves WORLD angular momentum, not body omega");
      near(energy(state) / initial_energy, 1, 1e-7,
           "free tumble rotational energy remains within accumulated-error "
           "bound");
    }
  }
  check(state.angular_velocity_radians_per_second !=
            initial.angular_velocity_radians_per_second,
        "asymmetric mixed-axis tumble does not incorrectly freeze body angular "
        "velocity");
}

auto qualified_tumble_contract(const Fixture& fixture) -> void {
  const auto coast = coast_intent();
  const std::array<RigidVector3, 2> starts{{{1.15, .9, 1.7}, {3, 3, 3}}};
  for (std::size_t scenario = 0; scenario < starts.size(); ++scenario) {
    auto state = fixture.state();
    state.orientation = {.5, .5, .5, .5};
    state.angular_velocity_radians_per_second = starts[scenario];
    const auto original_l = momentum(state);
    const double original_l_norm = std::sqrt(dot(original_l, original_l));
    const double original_energy = energy(state);
    double maximum_energy_error{}, maximum_momentum_error{};
    for (int tick = 0; tick < 72000; ++tick) {
      if (!advance_vacuum_dynamics(fixture.context, state, coast)) {
        check(false, "qualified 600-second free tumble advances");
        return;
      }
      const auto current_l = momentum(state);
      const RigidVector3 difference{current_l.x - original_l.x,
                                    current_l.y - original_l.y,
                                    current_l.z - original_l.z};
      maximum_energy_error = std::max(
          maximum_energy_error, std::abs(energy(state) / original_energy - 1));
      maximum_momentum_error =
          std::max(maximum_momentum_error,
                   std::sqrt(dot(difference, difference)) / original_l_norm);
    }
    std::cout << "Vacuum free tumble initial omega " << starts[scenario].x
              << ',' << starts[scenario].y << ',' << starts[scenario].z
              << " rad/s duration 600 s max relative energy error "
              << maximum_energy_error << " max relative world-L error "
              << maximum_momentum_error << '\n';
    check(maximum_energy_error < (scenario == 0 ? 1e-7 : 2e-6),
          "qualified initial-rate tumble energy stays within its measured "
          "envelope");
    check(maximum_momentum_error < 1e-8,
          "qualified initial-rate tumble world momentum stays within measured "
          "envelope");
  }
  // Existing spin beyond command rating is not an actuator request and must
  // not be clamped. This short safety check is not a high-spin accuracy claim.
  auto state = fixture.state();
  state.angular_velocity_radians_per_second.x = 10;
  check(advance_vacuum_dynamics(fixture.context, state, coast).has_value(),
        "above-rated existing principal spin advances safely without damping");
  near(state.angular_velocity_radians_per_second.x, 10, 1e-10,
       "unassisted high spin is not clipped to rated controller speed");
}

auto rejection_contract(const Fixture& fixture) -> void {
  const auto coast = coast_intent();
  const auto reject = [&](RigidBodyState state, VacuumIntent intent,
                          SimulationSeconds step) {
    const auto before = state;
    check(!advance_vacuum_dynamics(fixture.context, state, intent, step),
          "invalid dynamics input rejects");
    check(same_bits(state, before),
          "refused tick transaction leaves all state bits unchanged");
  };
  for (double bad :
       {-1.0, 0.0, 1.0 / 60, std::nextafter(kSimulationStep.count(), 1.0),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()})
    reject(fixture.state(), coast, SimulationSeconds{bad});
  for (std::size_t field = 0; field < 12; ++field) {
    for (double bad : {-.001, 1.001, std::nextafter(1.0, 2.0),
                       std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
      auto intent = coast;
      const std::array<RigidVector3*, 4> vectors{
          &intent.positive_translation, &intent.negative_translation,
          &intent.positive_rotation, &intent.negative_rotation};
      *axes(*vectors[field / 3])[field % 3] = bad;
      reject(fixture.state(), intent, kSimulationStep);
    }
  }
  for (std::size_t field = 0; field < 13; ++field)
    for (double bad : {std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
      auto state = fixture.state();
      *scalars(state)[field] = bad;
      reject(state, coast, kSimulationStep);
    }
  for (const auto craft :
       {CraftFrameRecipe{{0}, 1}, CraftFrameRecipe{{1}, 2}}) {
    auto state = fixture.state();
    state.craft = craft;
    reject(state, coast, kSimulationStep);
  }
  for (const auto tick : {std::numeric_limits<SimulationTick>::max(),
                          std::numeric_limits<SimulationTick>::max() - 1}) {
    auto state = fixture.state();
    state.tick = tick;
    reject(state, coast, kSimulationStep);
  }
  auto state = fixture.state();
  state.frame.kind = RigidFrameKind::planet_fixed;
  state.frame.planet = fixture.system.planets.front().descriptor.id;
  check(validate_rigid_body_state(fixture.context, state).has_value(),
        "unsupported dynamics owner fixture is valid generic rigid state");
  reject(state, coast, kSimulationStep);
  state = fixture.state();
  state.frame.kind = RigidFrameKind::station_relative_inertial;
  state.frame.station = fixture.station.id;
  reject(state, coast, kSimulationStep);
  state = fixture.state();
  state.frame.system.value ^= 1;
  reject(state, coast, kSimulationStep);
  state = fixture.state();
  state.orientation = {0, 0, 0, 0};
  reject(state, coast, kSimulationStep);
  state = fixture.state();
  state.orientation = {-1, 0, 0, 0};
  reject(state, coast, kSimulationStep);
  state = fixture.state();
  state.position_metres.x = -0.0;
  reject(state, coast, kSimulationStep);
  state = fixture.state();
  state.linear_velocity_metres_per_second.x =
      kRigidBodyMaximumVelocityMetresPerSecond;
  auto intent = coast;
  intent.positive_translation.x = 1;
  reject(state, intent, kSimulationStep);
  state = fixture.state();
  state.position_metres.x = kRigidBodyMaximumPositionMetres;
  state.linear_velocity_metres_per_second.x = 120;
  reject(state, coast, kSimulationStep);
  state = fixture.state();
  state.angular_velocity_radians_per_second.x = 101;
  reject(state, coast, kSimulationStep);
  auto broken = fixture.system;
  broken.planets.clear();
  state = fixture.state();
  const auto before = state;
  check(!advance_vacuum_dynamics({broken, nullptr}, state, coast) &&
            same_bits(state, before),
        "invalid authoritative world context refuses atomically");
}

auto trace_intent(int tick, int mode) -> VacuumIntent {
  auto intent = coast_intent();
  if (mode == 0 || mode == 2) {
    intent.negative_translation.z = tick % 180 < 90 ? .7 : 0;
    intent.positive_translation.x = tick % 60 < 30 ? .2 : 0;
  }
  if (mode == 1 || mode == 2) {
    intent.positive_rotation.x = tick % 120 < 60 ? .03 : 0;
    intent.negative_rotation.y = tick % 100 < 50 ? .02 : 0;
    intent.positive_rotation.z = tick % 80 < 40 ? .015 : 0;
  }
  intent.assistance = mode == 3;
  return intent;
}
auto resume_and_cadence_contract(const Fixture& fixture) -> void {
  static_assert(kVacuumDynamicsVersion == 1);
  // Version-one trace goldens were observed independently in GCC and Clang
  // after analytic axis/conservation checks, not derived as closed-form truth.
  constexpr std::array<std::uint64_t, 4> golden{
      14093167661298966439ULL, 5388409882239744150ULL, 15624328770361882707ULL,
      1281804748820979367ULL};
  for (int mode = 0; mode < 4; ++mode) {
    auto direct = fixture.state();
    if (mode == 3) {
      direct.linear_velocity_metres_per_second = {12, -8, -70};
      direct.angular_velocity_radians_per_second = {.2, -.15, .1};
    }
    auto resumed = direct;
    auto batched = direct;
    for (int tick = 0; tick < 720; ++tick) {
      const auto intent = trace_intent(tick, mode);
      check(
          advance_vacuum_dynamics(fixture.context, direct, intent).has_value(),
          "uninterrupted qualification trace advances");
      check(
          advance_vacuum_dynamics(fixture.context, resumed, intent).has_value(),
          "resumed qualification trace advances");
      if (tick % 37 == 0) {
        const auto json =
            encode_rigid_body_state_json(fixture.context, resumed);
        check(json.has_value(), "mid-maneuver state encodes");
        if (!json) return;
        const auto loaded =
            decode_rigid_body_state_json(fixture.context, *json);
        check(loaded && same_bits(*loaded, resumed),
              "save/resume preserves exact moving state");
        if (!loaded) return;
        resumed = *loaded;
      }
    }
    constexpr std::array<int, 7> batches{1, 4, 2, 17, 3, 8, 29};
    int tick{};
    std::size_t batch{};
    while (tick < 720) {
      const int count = batches[batch++ % batches.size()];
      for (int i = 0; i < count && tick < 720; ++i, ++tick)
        check(advance_vacuum_dynamics(fixture.context, batched,
                                      trace_intent(tick, mode))
                  .has_value(),
              "irregular presentation batches consume identical simulation "
              "intent trace");
    }
    check(same_bits(direct, resumed) && same_bits(direct, batched),
          "translation/rotation/combined/damping traces are bitwise resume and "
          "cadence independent");
    const auto checksum = rigid_body_state_checksum(fixture.context, direct);
    check(checksum && *checksum == golden[static_cast<std::size_t>(mode)],
          "versioned maneuver trace matches GCC/Clang exact checkpoint golden");
    check(checksum == rigid_body_state_checksum(fixture.context, resumed) &&
              checksum == rigid_body_state_checksum(fixture.context, batched),
          "all trace checksum checkpoints agree");
    if (checksum)
      std::cout << "Vacuum trace " << mode << " checksum " << *checksum << '\n';
  }
}
} // namespace

auto main() -> int {
  const Fixture fixture;
  rejection_contract(fixture);
  axis_contract(fixture);
  opposed_contract(fixture);
  analytic_golden_contract(fixture);
  assist_contract(fixture);
  conservation_contract(fixture);
  qualified_tumble_contract(fixture);
  resume_and_cadence_contract(fixture);
  std::cout << "Vacuum dynamics contracts: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
