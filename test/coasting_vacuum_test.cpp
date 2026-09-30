#include "apsis_drift/vacuum_dynamics.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace apsis_drift;
int failures{};
constexpr VacuumDynamicsRecipe coasting{kCoastingVacuumDynamicsVersion};

auto check(bool condition, std::string_view message) -> void {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}
template <typename T, typename E>
auto required(std::expected<T, E> result) -> T {
  if (!result) throw std::runtime_error("required provider failed");
  return *result;
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
auto axes(RigidVector3& v) -> std::array<double*, 3> {
  return {&v.x, &v.y, &v.z};
}
auto near(double a, double b, double tolerance) -> bool {
  return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tolerance;
}
auto magnitude_squared(RigidVector3 v) -> double {
  return (v.x * v.x + v.y * v.y) + v.z * v.z;
}
auto rotate(RigidOrientation q, RigidVector3 v) -> RigidVector3 {
  // Independent quaternion-vector expansion, not RK4 or provider helpers.
  const RigidVector3 u{q.x, q.y, q.z};
  const double dot = (u.x * v.x + u.y * v.y) + u.z * v.z;
  const double n = (q.w * q.w + u.x * u.x) + (u.y * u.y + u.z * u.z);
  const double diagonal = q.w * q.w - ((u.x * u.x + u.y * u.y) + u.z * u.z);
  return {
      (2 * dot * u.x + diagonal * v.x + 2 * q.w * (u.y * v.z - u.z * v.y)) / n,
      (2 * dot * u.y + diagonal * v.y + 2 * q.w * (u.z * v.x - u.x * v.z)) / n,
      (2 * dot * u.z + diagonal * v.z + 2 * q.w * (u.x * v.y - u.y * v.x)) / n};
}
struct Fixture {
  PhysicalLocalSystem system{
      required(generate_physical_origin_system(Seed{42}))};
  OriginStationDescriptor station{generate_origin_station(Seed{42})};
  RigidBodyWorldContext context{system, &station};
  auto state() const -> RigidBodyState {
    RigidBodyState s;
    s.frame.system = system.catalog.id;
    s.tick = 25;
    s.position_metres = {1024, -2048, 4096};
    s.linear_velocity_metres_per_second = {1024, -2048, 4096};
    return s;
  }
};
auto neutral_coasting(const Fixture& f) -> void {
  const auto awkward =
      required(normalize_rigid_orientation({.125, .375, .125, .875}));
  for (const auto attitude :
       {RigidOrientation{}, RigidOrientation{.5, .5, -.5, .5}, awkward}) {
    for (const bool assistance : {false, true}) {
      for (const bool spinning : {false, true}) {
        auto state = f.state();
        state.orientation = attitude;
        if (spinning)
          state.angular_velocity_radians_per_second = {.125, -.25, .375};
        const auto source = state;
        VacuumIntent intent;
        intent.assistance = assistance;
        for (unsigned i = 0; i < 3600; ++i) {
          const auto report = required(
              advance_vacuum_dynamics(f.context, state, intent, coasting));
          check(report.requested_force_newtons == RigidVector3{} &&
                    report.assist_force_newtons == RigidVector3{} &&
                    report.applied_force_newtons == RigidVector3{} &&
                    report.positive_force_newtons == RigidVector3{} &&
                    report.negative_force_newtons == RigidVector3{} &&
                    report.world_linear_impulse_newton_seconds ==
                        RigidVector3{},
                "neutral v2 uses no translational actuators at any "
                "attitude/spin");
          check(state.linear_velocity_metres_per_second ==
                    source.linear_velocity_metres_per_second,
                "neutral translation preserves all velocity bits");
          if (!assistance)
            check(report.assist_torque_newton_metres == RigidVector3{} &&
                      report.applied_torque_newton_metres == RigidVector3{},
                  "assistance off retains torque-free coasting");
        }
        const double elapsed = 3600 * kSimulationStep.count();
        check(near(state.position_metres.x,
                   source.position_metres.x +
                       source.linear_velocity_metres_per_second.x * elapsed,
                   2e-7) &&
                  near(state.position_metres.y,
                       source.position_metres.y +
                           source.linear_velocity_metres_per_second.y * elapsed,
                       4e-7) &&
                  near(state.position_metres.z,
                       source.position_metres.z +
                           source.linear_velocity_metres_per_second.z * elapsed,
                       8e-7),
              "unpowered displacement follows independent constant velocity "
              "solution");
        check(state.tick == source.tick + 3600 && state.craft == source.craft &&
                  state.frame == source.frame,
              "coasting advances only physical tick/pose");
        if (assistance && spinning)
          check(magnitude_squared(state.angular_velocity_radians_per_second) <
                    1e-18,
                "rotation assistance stabilizes while translation coasts");
        if (!assistance && spinning)
          check(magnitude_squared(state.angular_velocity_radians_per_second) >
                    .01,
                "assistance off retains angular motion");
      }
    }
  }
}
auto manual_authority(const Fixture& f) -> void {
  const auto& p = starter_shuttle_frame().properties;
  const double dt = kSimulationStep.count();
  for (const auto q : {RigidOrientation{}, RigidOrientation{.5, .5, .5, .5}}) {
    for (const bool assistance : {false, true}) {
      for (std::size_t axis = 0; axis < 3; ++axis) {
        for (const bool positive : {false, true}) {
          auto state = f.state();
          state.orientation = q;
          const auto before = state;
          VacuumIntent intent;
          intent.assistance = assistance;
          *axes(positive ? intent.positive_translation
                         : intent.negative_translation)[axis] = 1;
          const auto report = required(
              advance_vacuum_dynamics(f.context, state, intent, coasting));
          RigidVector3 body_force;
          *axes(body_force)[axis] =
              (positive ? 1 : -1) *
              static_cast<double>(positive ? p.positive_force_newtons[axis]
                                           : p.negative_force_newtons[axis]);
          const auto force = rotate(q, body_force);
          const std::array expected{force.x, force.y, force.z};
          auto initial = before;
          for (std::size_t observed = 0; observed < 3; ++observed) {
            const double acceleration = expected[observed] / p.dry_mass_kg;
            check(
                near(
                    *axes(state.linear_velocity_metres_per_second)[observed],
                    *axes(initial.linear_velocity_metres_per_second)[observed] +
                        acceleration * dt,
                    2e-12),
                "manual thrust independently changes world velocity through "
                "rotated F/m");
            check(
                near(*axes(state.position_metres)[observed],
                     *axes(initial.position_metres)[observed] +
                         *axes(
                             initial
                                 .linear_velocity_metres_per_second)[observed] *
                             dt +
                         .5 * acceleration * dt * dt,
                     2e-12),
                "manual thrust displacement includes original momentum");
          }
          check(report.applied_force_newtons == body_force &&
                    report.assist_force_newtons == RigidVector3{} &&
                    !report.force_saturated,
                "v2 assistance adds no translation correction to commanded "
                "thrust");
        }
      }
    }
  }
  auto state = f.state();
  VacuumIntent opposing;
  opposing.positive_translation = {1, 0, 1};
  opposing.negative_translation = {1, 0, .2};
  const auto report =
      required(advance_vacuum_dynamics(f.context, state, opposing, coasting));
  check(report.applied_force_newtons == RigidVector3{} &&
            report.world_linear_impulse_newton_seconds == RigidVector3{},
        "equal opposing physical firings have zero net impulse");
  check(report.positive_force_newtons.x == p.positive_force_newtons[0] &&
            report.negative_force_newtons.x == p.negative_force_newtons[0] &&
            report.positive_force_newtons.z == p.positive_force_newtons[2] &&
            report.negative_force_newtons.z == p.positive_force_newtons[2],
        "gross opposing channels remain observable for later fuel accounting");
  state = f.state();
  opposing.negative_translation.z = 1;
  const auto unequal =
      required(advance_vacuum_dynamics(f.context, state, opposing, coasting));
  check(unequal.applied_force_newtons.z < 0,
        "equal main/retro fractions retain unequal physical authority");
}
auto version_and_rotation(const Fixture& f) -> void {
  auto legacy = f.state();
  legacy.angular_velocity_radians_per_second = {.125, -.25, .375};
  auto explicit_v1 = legacy, coast = legacy;
  VacuumIntent intent;
  const auto old = required(advance_vacuum_dynamics(f.context, legacy, intent));
  const auto selected = required(
      advance_vacuum_dynamics(f.context, explicit_v1, intent,
                              VacuumDynamicsRecipe{kVacuumDynamicsVersion}));
  const auto current =
      required(advance_vacuum_dynamics(f.context, coast, intent, coasting));
  check(same_bits(legacy, explicit_v1),
        "explicit v1 reproduces original default state exactly");
  check(old.applied_force_newtons == selected.applied_force_newtons &&
            old.assist_force_newtons == selected.assist_force_newtons &&
            old.world_linear_impulse_newton_seconds ==
                selected.world_linear_impulse_newton_seconds,
        "explicit v1 preserves historical force ledger");
  check(old.assist_force_newtons != RigidVector3{} &&
            current.assist_force_newtons == RigidVector3{},
        "v1 historical damping stays distinct from v2 coasting");
  check(legacy.orientation == coast.orientation &&
            legacy.angular_velocity_radians_per_second ==
                coast.angular_velocity_radians_per_second &&
            old.applied_torque_newton_metres ==
                current.applied_torque_newton_metres &&
            old.assist_torque_newton_metres ==
                current.assist_torque_newton_metres &&
            old.positive_torque_newton_metres ==
                current.positive_torque_newton_metres &&
            old.negative_torque_newton_metres ==
                current.negative_torque_newton_metres &&
            old.world_angular_impulse_newton_metre_seconds ==
                current.world_angular_impulse_newton_metre_seconds,
        "v2 uses exactly the existing coupled attitude and rotation ledger");
  intent.assistance = false;
  legacy = f.state();
  explicit_v1 = legacy;
  coast = legacy;
  intent.negative_translation = {.1, .2, .3};
  intent.positive_rotation = {.3, .2, .1};
  required(advance_vacuum_dynamics(f.context, legacy, intent));
  required(advance_vacuum_dynamics(f.context, explicit_v1, intent,
                                   VacuumDynamicsRecipe{1}));
  required(advance_vacuum_dynamics(f.context, coast, intent, coasting));
  check(same_bits(legacy, coast) && same_bits(legacy, explicit_v1),
        "unassisted v1/v2 use identical coupled integration");
}
auto continuation(const Fixture& f) -> void {
  auto uninterrupted = f.state(), resumed = uninterrupted;
  uninterrupted.orientation =
      required(normalize_rigid_orientation({.125, .375, .125, .875}));
  resumed = uninterrupted;
  for (unsigned i = 0; i < 1200; ++i) {
    VacuumIntent intent;
    intent.assistance = (i / 75) % 2 == 0;
    intent.negative_translation = {.125, .25, .375};
    intent.positive_rotation = {.1, .2, .3};
    const auto before = resumed;
    const auto a = required(
        advance_vacuum_dynamics(f.context, uninterrupted, intent, coasting));
    if (i % 31 == 0)
      resumed = required(decode_rigid_body_state_json(
          f.context,
          required(encode_rigid_body_state_json(f.context, resumed))));
    const auto b =
        required(advance_vacuum_dynamics(f.context, resumed, intent, coasting));
    check(same_bits(uninterrupted, resumed) &&
              a.applied_force_newtons == b.applied_force_newtons &&
              a.applied_torque_newton_metres ==
                  b.applied_torque_newton_metres &&
              a.world_linear_impulse_newton_seconds ==
                  b.world_linear_impulse_newton_seconds &&
              a.world_angular_impulse_newton_metre_seconds ==
                  b.world_angular_impulse_newton_metre_seconds,
          "selected v2 state and impulses resume exactly with unchanged intent "
          "trace");
    check(resumed.tick == before.tick + 1,
          "resumption has no hidden step accumulator");
  }
  // New v2 trace pinned only after independent GCC/Clang agreement.
  check(required(rigid_body_state_checksum(f.context, uninterrupted)) ==
            4255661836357321364ULL,
        "v2 coasting maneuver trace has an exact physical-state checkpoint");
}
auto refusals(const Fixture& f) -> void {
  const auto source = f.state();
  const VacuumIntent intent;
  const auto reject = [&](RigidBodyState s, const VacuumIntent& command,
                          VacuumDynamicsRecipe recipe,
                          SimulationSeconds step = kSimulationStep) {
    const auto original = s;
    check(!advance_vacuum_dynamics(f.context, s, command, recipe, step),
          "invalid provider/state/intent/frame/step refuses");
    check(same_bits(s, original),
          "refused tick never replaces any source bits");
  };
  for (const auto version :
       {0U, 3U, std::numeric_limits<std::uint32_t>::max()}) {
    auto s = source;
    const auto result = advance_vacuum_dynamics(f.context, s, intent,
                                                VacuumDynamicsRecipe{version});
    check(!result &&
              result.error() == VacuumDynamicsError::unsupported_version &&
              same_bits(source, s),
          "unknown version refuses transactionally before dispatch");
  }
  for (std::size_t i = 0; i < 13; ++i)
    for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(), -0.0}) {
      auto s = source;
      *scalars(s)[i] = bad;
      reject(s, intent, coasting);
    }
  for (const double invalid :
       {-1.0, 1.01, std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()}) {
    for (unsigned channel = 0; channel < 4; ++channel) {
      auto bad = intent;
      for (std::size_t axis = 0; axis < 3; ++axis) {
        auto altered = bad;
        auto& selected = channel == 0   ? altered.positive_translation
                         : channel == 1 ? altered.negative_translation
                         : channel == 2 ? altered.positive_rotation
                                        : altered.negative_rotation;
        *axes(selected)[axis] = invalid;
        reject(source, altered, coasting);
      }
    }
  }
  for (const double dt : {0.0, -1.0, kSimulationStep.count() * 2,
                          std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::quiet_NaN()})
    reject(source, intent, coasting, SimulationSeconds{dt});
  auto s = source;
  s.tick = std::numeric_limits<SimulationTick>::max() - 1;
  reject(s, intent, coasting);
  s.tick = std::numeric_limits<SimulationTick>::max();
  reject(s, intent, coasting);
  s = source;
  s.frame = {RigidFrameKind::station_relative_inertial,
             f.system.catalog.id,
             {},
             f.station.id};
  reject(s, intent, coasting);
  s = source;
  s.frame = {RigidFrameKind::planet_fixed,
             f.system.catalog.id,
             f.station.orbit.host_planet,
             {}};
  reject(s, intent, coasting);
  s = source;
  s.orientation = {2, 0, 0, 0};
  reject(s, intent, coasting);
  s = source;
  s.position_metres.x = kRigidBodyMaximumPositionMetres;
  reject(s, intent, coasting);
  s = source;
  s.linear_velocity_metres_per_second.x =
      kRigidBodyMaximumVelocityMetresPerSecond;
  auto thrust = intent;
  thrust.positive_translation.x = 1;
  reject(s, thrust, coasting);
  s = source;
  s.frame.system = SystemId{0};
  reject(s, intent, coasting);
  const RigidBodyWorldContext unwrapped{f.system.catalog};
  s = source;
  check(!advance_vacuum_dynamics(unwrapped, s, intent, coasting) &&
            same_bits(s, source),
        "unwrapped physical catalog remains invalid");
}
} // namespace

auto main() -> int {
  try {
    const Fixture f;
    neutral_coasting(f);
    manual_authority(f);
    version_and_rotation(f);
    continuation(f);
    refusals(f);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "coasting vacuum: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
