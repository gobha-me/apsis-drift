#include "thrust_flight.hpp"
#include <iostream>
#include <numbers>

using namespace apsis_drift;
using namespace apsis_drift::flight_lab;
auto check(bool ok, const char *message) -> void {
  if (!ok)
    throw std::runtime_error(message);
}
template <class F> auto rejects(F f) -> void {
  try {
    f();
  } catch (const std::invalid_argument &) {
    return;
  }
  throw std::runtime_error("invalid input accepted");
}
auto evolve(const PlanetDescriptor &p, State s, Demand d, int ticks) -> State {
  for (int i = 0; i < ticks; ++i)
    advance(p, 0, s, d);
  return s;
}
auto main() -> int {
  try {
    const auto planet = generate_planet_descriptor(Seed{42});
    Seed vacuum_seed{0};
    while (generate_planet_descriptor(vacuum_seed).atmosphere_class !=
           AtmosphereClass::airless)
      ++vacuum_seed.value;
    const auto vacuum = generate_planet_descriptor(vacuum_seed);
    const auto base = initial(planet, {.25, .4, 100000}, .3);
    for (double bad : {std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity(), 1.01, -1.01}) {
      for (int axis = 0; axis < 7; ++axis) {
        std::array<double, 7> axes{};
        axes[axis] = bad;
        auto s = base;
        rejects([&] {
          advance(
              planet, 0, s,
              {axes[0], axes[1], axes[2], axes[3], axes[4], axes[5], axes[6]});
        });
        check(s == base, "rejected demand mutated state");
      }
    }
    for (auto step : {SimulationSeconds{0}, SimulationSeconds{.1},
                      SimulationSeconds{NAN}}) {
      auto s = base;
      rejects([&] { advance(planet, 0, s, {}, step); });
      check(s == base, "bad step mutated state");
    }
    for (double surface : std::array<double, 3>{NAN, INFINITY, 1e11}) {
      auto s = base;
      rejects([&] { advance(planet, surface, s, {}); });
      check(s == base, "bad terrain mutated state");
    }
    auto invalid = base;
    invalid.right.x = NAN;
    rejects([&] { advance(planet, 0, invalid, {}); });
    invalid = base;
    invalid.up = invalid.right;
    rejects([&] { advance(planet, 0, invalid, {}); });
    invalid = base;
    invalid.tick = std::numeric_limits<SimulationTick>::max();
    rejects([&] { advance(planet, 0, invalid, {}); });
    invalid = base;
    rejects([&] { advance(planet, 0, invalid, {-.1}); });
    const auto full = evolve(planet, base, {1}, 240);
    const auto half = evolve(planet, base, {.5}, 240);
    const auto retro = evolve(planet, base, {0, 1}, 240);
    check(length(full.velocity) > 70 &&
              length(half.velocity) < length(full.velocity) * .60,
          "main throttle is not proportional");
    check(length(full.velocity) > length(retro.velocity) * 4,
          "retro not weaker than main");
    check(dot(retro.velocity, base.back) > 0, "retro does not thrust backward");
    auto moving = initial(vacuum, {.25, .4, 100000}, 0);
    moving.velocity = moving.back * (-100);
    const V before = moving.velocity;
    const double r = vacuum.radius.value * 1000.0;
    const double distance = length(moving.position);
    const V gravity =
        unit(moving.position) * (-9.80665 * vacuum.surface_gravity.value /
                                 1000.0 * r * r / (distance * distance));
    advance(vacuum, 0, moving, {0, 0, 0, 0, 0, 0, 0, false});
    check(length(moving.velocity -
                 (before + gravity * kSimulationStep.count())) < 1e-9,
          "vacuum coast changed inertia beyond gravity");
    auto fast = initial(planet, {.25, .4, 2000}, 0);
    fast.velocity = fast.back * (-350);
    const auto drag = evolve(planet, fast, {}, 1);
    check(length(drag.velocity) < 350 && drag.dynamic_pressure > 1000,
          "atmospheric drag missing");
    check(density(planet, 8500) < density(planet, 0) && density(vacuum, 0) == 0,
          "density profile incorrect");
    const auto cruise =
        evolve(planet, initial(planet, {.25, .4, 2000}, 0), {1}, 2400);
    check(length(cruise.velocity) > 300,
          "flight lab retains old 120 m/s speed ceiling");
    auto braking = base;
    braking.velocity = base.back * (-200);
    const auto retro_stop = evolve(planet, braking, {0, 1}, 360);
    auto flipped = braking;
    flipped.right = flipped.right * (-1);
    flipped.back = flipped.back * (-1);
    const auto main_stop = evolve(planet, flipped, {1}, 360);
    check(length(main_stop.velocity) < length(retro_stop.velocity) - 70,
          "flip and main burn not stronger than retro");
    for (int axis = 0; axis < 3; ++axis) {
      Demand d;
      if (axis == 0)
        d.pitch = 1;
      if (axis == 1)
        d.yaw = 1;
      if (axis == 2)
        d.roll = 1;
      const auto rotated = evolve(planet, base, d, 180);
      check(length(rotated.up - base.up) + length(rotated.back - base.back) >
                .5,
            "attitude axis has no effect");
    }
    const auto lateral = evolve(planet, base, {0, 0, 0, 0, 0, 1, 0}, 120);
    const auto vertical = evolve(planet, base, {0, 0, 0, 0, 0, 0, 1}, 120);
    check(dot(lateral.velocity, base.right) > 10 &&
              dot(vertical.velocity, base.up) > 5,
          "translation axes missing");
    const Demand trace{.7, .1, .3, -.2, .4, .15, -.1, true};
    const auto reference = evolve(planet, base, trace, 240);
    for (int fps : {30, 60, 144}) {
      auto replay = base;
      FixedStepClock clock;
      for (int frame = 0; frame < fps * 2; ++frame) {
        const auto scheduled = clock.advance(SimulationSeconds{1.0 / fps});
        check(scheduled.has_value(), "clock");
        for (int tick = 0; tick < scheduled->steps; ++tick)
          advance(planet, 0, replay, trace);
      }
      check(replay == reference && checksum(replay) == checksum(reference),
            "render cadence changed thrust trace");
    }
    auto tumble = base;
    for (int tick = 0; tick < 10000; ++tick)
      advance(
          planet, 0, tumble,
          {0, 0, std::sin(tick * .01), std::cos(tick * .007), .7, 0, 0, false});
    validate(tumble);
    auto low = initial(planet, {.25, .4, 10}, 0);
    advance(planet, 0, low, {});
    check(low.floor_guard && std::abs(low.clearance - 16) < 1e-6,
          "explicit test-floor guard missing");
    std::cout << "Thrust flight lab: validation, seven actuator demands/six "
                 "DOF, inertia, drag, retro/flip braking, "
                 "cadence, 10000-tick attitude and floor-guard checks passed.\n"
              << "Dense-air 20 s full-thrust speed: " << length(cruise.velocity)
              << " m/s; 2 s main/half/retro: " << length(full.velocity) << " / "
              << length(half.velocity) << " / " << length(retro.velocity)
              << " m/s\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
