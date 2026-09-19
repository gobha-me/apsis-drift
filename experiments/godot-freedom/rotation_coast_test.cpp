#include "thrust_flight.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
using namespace apsis_drift;
namespace lab = apsis_drift::flight_lab;
using lab::V;
int failures{};
auto check(bool value, std::string_view message) -> void {
  if (value) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}
auto near(double a, double b, double tolerance, std::string_view message)
    -> void {
  check(std::isfinite(a) && std::abs(a - b) <= tolerance, message);
}
auto same_bits(const lab::State& a, const lab::State& b) -> bool {
  if (a.tick != b.tick || a.assist != b.assist ||
      a.floor_guard != b.floor_guard || a.angular_model != b.angular_model ||
      a.torque_saturated != b.torque_saturated ||
      a.orientation.has_value() != b.orientation.has_value())
    return false;
  const auto vectors = [](const lab::State& s) {
    return std::array{s.position, s.velocity, s.right,       s.up,
                      s.back,     s.angular,  s.thrust_body, s.torque_body};
  };
  const auto av = vectors(a), bv = vectors(b);
  for (std::size_t i = 0; i < av.size(); ++i) {
    const std::array aa{av[i].x, av[i].y, av[i].z};
    const std::array bb{bv[i].x, bv[i].y, bv[i].z};
    for (std::size_t j = 0; j < aa.size(); ++j)
      if (std::bit_cast<std::uint64_t>(aa[j]) !=
          std::bit_cast<std::uint64_t>(bb[j]))
        return false;
  }
  const std::array aa{a.main,      a.retro,       a.density, a.dynamic_pressure,
                      a.clearance, a.acceleration};
  const std::array bb{b.main,      b.retro,       b.density, b.dynamic_pressure,
                      b.clearance, b.acceleration};
  for (std::size_t i = 0; i < aa.size(); ++i)
    if (std::bit_cast<std::uint64_t>(aa[i]) !=
        std::bit_cast<std::uint64_t>(bb[i]))
      return false;
  if (a.orientation) {
    const std::array aq{a.orientation->w, a.orientation->x, a.orientation->y,
                        a.orientation->z};
    const std::array bq{b.orientation->w, b.orientation->x, b.orientation->y,
                        b.orientation->z};
    for (std::size_t i = 0; i < aq.size(); ++i)
      if (std::bit_cast<std::uint64_t>(aq[i]) !=
          std::bit_cast<std::uint64_t>(bq[i]))
        return false;
  }
  return true;
}
template <class Operation>
auto rejects(Operation operation, std::string_view message) -> void {
  try {
    operation();
  } catch (const std::invalid_argument&) {
    return;
  }
  check(false, message);
}
auto evolve(const PlanetDescriptor& p, lab::State s, lab::Demand d, int ticks)
    -> lab::State {
  for (int i = 0; i < ticks; ++i)
    lab::advance(p, 0, s, d);
  return s;
}
auto free_demand() -> lab::Demand {
  lab::Demand result;
  result.assist = false;
  return result;
}
auto spin_momentum(const lab::State& s) -> V {
  const auto& inertia =
      starter_shuttle_frame().properties.principal_inertia_kg_m2;
  return s.right * (s.angular.x * static_cast<double>(inertia[0])) +
         s.up * (s.angular.y * static_cast<double>(inertia[1])) +
         s.back * (s.angular.z * static_cast<double>(inertia[2]));
}
auto spin_energy(const lab::State& s) -> double {
  const auto& inertia =
      starter_shuttle_frame().properties.principal_inertia_kg_m2;
  return .5 * ((static_cast<double>(inertia[0]) * s.angular.x * s.angular.x +
                static_cast<double>(inertia[1]) * s.angular.y * s.angular.y) +
               static_cast<double>(inertia[2]) * s.angular.z * s.angular.z);
}
auto independent_rotate(RigidOrientation q, V v) -> V {
  const V u{q.x, q.y, q.z};
  return u * (2 * lab::dot(u, v)) + v * (q.w * q.w - lab::dot(u, u)) +
         lab::cross(u, v) * (2 * q.w);
}
auto check_basis(const lab::State& s) -> void {
  check(s.orientation.has_value(),
        "model two carries authoritative quaternion");
  if (!s.orientation) return;
  for (const auto& pair :
       {std::pair{s.right, V{1, 0, 0}}, std::pair{s.up, V{0, 1, 0}},
        std::pair{s.back, V{0, 0, 1}}})
    near(lab::length(pair.first -
                     independent_rotate(*s.orientation, pair.second)),
         0, 1e-12, "presented basis is derived from authoritative quaternion");
  near(lab::dot(lab::cross(s.right, s.up), s.back), 1, 1e-12,
       "derived presentation basis stays right handed and orthonormal");
}

auto legacy_contract(const PlanetDescriptor& p) -> void {
  const auto base = lab::initial(p, {.25, .4, 100000}, .3);
  check(base.angular_model == 1 && !base.orientation,
        "standalone initial defaults to unchanged model one");
  // Captured with GCC -O3 before the model-two header edits; same source seed,
  // origin and demand trace as legacy fixtures. These are compatibility locks.
  check(lab::checksum(base) == 12702440880597842818ULL,
        "pre-change model-one initial checksum");
  check(lab::checksum(evolve(p, base, {.7, .1, .3, -.2, .4, .15, -.1, false},
                             240)) == 6380759757687234280ULL,
        "pre-change model-one mixed OFF trace checksum");
  check(lab::checksum(evolve(p, base, {.7, .1, .3, -.2, .4, .15, -.1, true},
                             240)) == 16915801839928502580ULL,
        "pre-change model-one mixed ON trace checksum");
  auto moving = base;
  moving.angular = {.3, -.4, .2};
  const auto damped = evolve(p, moving, free_demand(), 120);
  check(lab::checksum(damped) == 16959986697016779776ULL &&
            damped.angular == V{},
        "legacy model retains historical attitude damping with translation "
        "assist off");
}

auto boundaries(const PlanetDescriptor& p) -> void {
  const auto base = lab::initial(p, {.25, .4, 1000000}, .3, 2);
  const auto reject_state = [&](lab::State state) {
    const auto before = state;
    rejects([&] { lab::advance(p, 0, state, free_demand()); },
            "invalid model-two state accepted");
    check(same_bits(state, before),
          "refused model-two state leaves every input bit unchanged");
  };
  auto state = base;
  state.angular_model = 3;
  reject_state(state);
  state = base;
  state.orientation.reset();
  reject_state(state);
  state = base;
  state.orientation = RigidOrientation{0, 0, 0, 0};
  reject_state(state);
  state = base;
  state.orientation = RigidOrientation{-1, 0, 0, 0};
  reject_state(state);
  state = base;
  state.orientation->x = NAN;
  reject_state(state);
  state = base;
  state.torque_body.y = INFINITY;
  reject_state(state);
  state = base;
  state.angular.x = NAN;
  reject_state(state);
  state = base;
  state.angular = {11, 0, 0};
  reject_state(state);
  state = base;
  state.right = state.up;
  reject_state(state);
  state = base;
  state.right = state.right * -1;
  state.back = state.back * -1;
  reject_state(state);
  state = base;
  state.tick = std::numeric_limits<SimulationTick>::max();
  reject_state(state);
  for (double bad : {0.0, -.1, .1, std::numeric_limits<double>::quiet_NaN()}) {
    state = base;
    rejects(
        [&] {
          lab::advance(p, 0, state, free_demand(), SimulationSeconds{bad});
        },
        "invalid model-two step accepted");
    check(same_bits(state, base), "bad model-two step is transactional");
  }
  for (int axis = 0; axis < 3; ++axis)
    for (double bad : {1.01, -1.01, std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity()}) {
      state = base;
      auto demand = free_demand();
      const std::array angular{&demand.pitch, &demand.yaw, &demand.roll};
      *angular[static_cast<std::size_t>(axis)] = bad;
      rejects([&] { lab::advance(p, 0, state, demand); },
              "invalid rotation input accepted");
      check(same_bits(state, base), "bad angular input is transactional");
    }
  state = base;
  rejects([&] { lab::enable_rigid_attitude(state); },
          "duplicate promotion accepted");
  check(same_bits(state, base),
        "duplicate promotion cannot reset quaternion/spin");
  state = lab::initial(p, {.25, .4, 1000000}, .3);
  lab::advance(p, 0, state, free_demand());
  const auto advanced = state;
  rejects([&] { lab::enable_rigid_attitude(state); },
          "late promotion silently migrated old model");
  check(same_bits(state, advanced),
        "late promotion refusal preserves legacy state");
  for (unsigned bad : {0U, 3U, std::numeric_limits<unsigned>::max()})
    rejects(
        [&] {
          static_cast<void>(lab::initial(p, {.25, .4, 1000000}, .3, bad));
        },
        "unknown initial angular model accepted");
}

auto coast_contract(const PlanetDescriptor& p) -> void {
  const auto base = lab::initial(p, {.25, .4, 1000000}, .3, 2);
  check_basis(base);
  for (std::size_t axis = 0; axis < 3; ++axis) {
    auto state = base;
    const std::array omega{&state.angular.x, &state.angular.y,
                           &state.angular.z};
    *omega[axis] = .4;
    const auto spin = state.angular;
    state = evolve(p, state, free_demand(), 2400);
    near(lab::length(state.angular - spin), 0, 1e-9,
         "OFF principal-axis spin persists for twenty seconds");
    check(state.torque_body == V{} && !state.assist,
          "OFF neutral coast has no hidden stabilizing torque");
    check_basis(state);
  }
  auto state = base;
  state.angular = {.31, -.47, .29};
  const auto initial_l = spin_momentum(state);
  const double initial_energy = spin_energy(state);
  for (int tick = 0; tick < 12000; ++tick) {
    lab::advance(p, 0, state, free_demand());
    if (tick % 120 == 119) {
      near(lab::length(spin_momentum(state) - initial_l), 0, 1e-5,
           "native model-two tumble conserves frame angular momentum");
      near(spin_energy(state) / initial_energy, 1, 1e-7,
           "native mixed-axis coast retains bounded rotational energy");
      check_basis(state);
    }
  }
  check(lab::length(state.angular - V{.31, -.47, .29}) > .01,
        "asymmetric model-two free tumble does not freeze body omega");
  state = base;
  state.angular = {.4, 0, 0};
  auto counter = free_demand();
  counter.pitch = -1;
  lab::advance(p, 0, state, counter);
  check(state.angular.x > .3 && state.angular.x < .4 && state.torque_body.x < 0,
        "countersteer applies bounded opposing torque without snap reversal");
  const auto released = state.angular;
  lab::advance(p, 0, state, free_demand());
  near(lab::length(state.angular - released), 0, 1e-10,
       "releasing countersteer immediately returns to torque-free coast");
  state = base;
  state.angular = {.4, -.3, .2};
  lab::advance(p, 0, state, lab::Demand{});
  check(lab::length(state.angular) > .45 && lab::length(state.torque_body) > 0,
        "ON recovery starts with physical bounded torque rather than reset");
  state = evolve(p, state, lab::Demand{}, 240);
  check(lab::length(state.angular) < .001,
        "ON stabilization recovers modest mixed-axis spin");
}

auto held_command_contract(const PlanetDescriptor& p) -> void {
  for (int scenario = 0; scenario < 9; ++scenario) {
    auto state = lab::initial(p, {.25, .4, 1000000}, .3, 2);
    if (scenario >= 5) state.angular = {.3, -.4, .2};
    const bool partial_axes = scenario != 3 && scenario != 4;
    const int ticks = partial_axes ? 72000 : 7200;
    double peak_rate = lab::length(state.angular);
    for (int tick = 0; tick < ticks; ++tick) {
      auto d = free_demand();
      if (scenario == 0 || scenario == 5 || scenario == 8) d.pitch = 1;
      if (scenario == 1 || scenario == 6 || scenario == 8) d.yaw = 1;
      if (scenario == 2 || scenario == 7) d.roll = 1;
      if (scenario == 3 || scenario == 4) {
        const double direction = scenario == 3 || tick % 240 < 120 ? 1 : -1;
        d.pitch = direction;
        d.yaw = -direction;
        d.roll = direction;
      }
      lab::advance(p, 0, state, d);
      peak_rate = std::max(peak_rate, lab::length(state.angular));
      check((d.pitch != 0 || state.torque_body.x == 0) &&
                (d.yaw != 0 || state.torque_body.y == 0) &&
                (d.roll != 0 || state.torque_body.z == 0),
            "OFF released axes apply exactly zero torque during other-axis "
            "commands");
      // Uncommanded axes may exchange momentum; they are not rate-clamped.
    }
    check(peak_rate < (partial_axes ? 10.0 : 3.5),
          "sustained/alternating full sticks stay inside the numerical safety "
          "envelope");
    std::cout << "Rotation held-command scenario " << scenario << " duration "
              << ticks / 120 << " s peak angular rate " << peak_rate
              << " rad/s\n";
    check_basis(state);
    const auto initial_l = spin_momentum(state);
    state = evolve(p, state, free_demand(), 240);
    near(lab::length(spin_momentum(state) - initial_l), 0, 1e-5,
         "release after full-stick maneuver preserves angular momentum");
  }
  auto dense = lab::initial(p, {.25, .4, 2000}, .3, 2);
  dense.velocity = dense.back * -8000;
  auto d = free_demand();
  d.pitch = 1;
  d.roll = -.5;
  dense = evolve(p, dense, d, 240);
  check(std::isfinite(dense.dynamic_pressure) &&
            lab::length(dense.angular) < 3.5,
        "high dynamic-pressure rate authority stays finite and bounded");
  check_basis(dense);
}

auto cadence_contract(const PlanetDescriptor& p) -> void {
  const auto base = lab::initial(p, {.25, .4, 1000000}, .3, 2);
  const lab::Demand demand{.7, .1, .3, -.2, .4, .15, -.1, false};
  const auto reference = evolve(p, base, demand, 240);
  for (int fps : {30, 60, 144}) {
    auto state = base;
    FixedStepClock clock;
    for (int frame = 0; frame < fps * 2; ++frame) {
      const auto scheduled = clock.advance(SimulationSeconds{1.0 / fps});
      check(scheduled.has_value(),
            "valid render interval schedules fixed ticks");
      if (!scheduled) return;
      for (int step = 0; step < scheduled->steps; ++step)
        lab::advance(p, 0, state, demand);
    }
    check(same_bits(state, reference) &&
              lab::checksum(state) == lab::checksum(reference),
          "model-two quaternion trace is bitwise independent of renderer "
          "cadence");
  }
  std::cout << "Rotation model-two cadence checksum "
            << lab::checksum(reference) << '\n';
}
} // namespace

auto main() -> int {
  try {
    const auto planet = generate_planet_descriptor(Seed{42});
    boundaries(planet);
    legacy_contract(planet);
    coast_contract(planet);
    held_command_contract(planet);
    cadence_contract(planet);
  } catch (const std::exception& error) {
    check(false, error.what());
  }
  std::cout << "Native rotation-coast contracts: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
