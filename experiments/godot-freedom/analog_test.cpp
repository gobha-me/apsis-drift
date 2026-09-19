#include "apsis_drift/planetary_flight.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace apsis_drift;

auto check(bool condition, const char* message) -> void {
  if (!condition) throw std::runtime_error(message);
}

auto main() -> int {
  try {
    const auto planet = generate_planet_descriptor(Seed{42});
    const auto initial = initial_planetary_flight_state(
        planet, {0.25, 0.4, 1000.0}, {0.0}, 0.3, FlightMode::manual);
    check(initial.has_value(), "initial state");
    for (double invalid :
         {-1.00001, 1.00001, std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::quiet_NaN()}) {
      for (int axis = 0; axis < 4; ++axis) {
        std::array<double, 4> values{};
        values[axis] = invalid;
        auto state = *initial;
        check(!advance_planetary_flight(
                  planet, {0}, state, {}, kSimulationStep, {},
                  PlanetaryAnalogInput{values[0], values[1], values[2],
                                       values[3]}),
              "invalid axis accepted");
        check(state == *initial, "invalid axis mutated state");
      }
    }
    const std::array press{
        FlightCommandKind::press_forward, FlightCommandKind::press_turn_right,
        FlightCommandKind::press_strafe_right, FlightCommandKind::press_rise};
    const std::array negative{
        FlightCommandKind::press_backward, FlightCommandKind::press_turn_left,
        FlightCommandKind::press_strafe_left, FlightCommandKind::press_fall};
    // Exhaust all 3^4 digital endpoint combinations. Old command semantics
    // and checksums must agree exactly with the new per-tick input path.
    for (int code = 0; code < 81; ++code) {
      int digits = code;
      std::array<double, 4> axes{};
      std::array<FlightCommand, 4> commands{};
      std::size_t count = 0;
      for (int i = 0; i < 4; ++i) {
        axes[i] = digits % 3 - 1;
        digits /= 3;
        if (axes[i] != 0)
          commands[count++] = {0, axes[i] > 0 ? press[i] : negative[i]};
      }
      auto digital = *initial;
      auto analog = *initial;
      for (int tick = 0; tick < 120; ++tick) {
        check(advance_planetary_flight(planet, {0}, digital,
                                       {commands.data(), tick == 0 ? count : 0},
                                       kSimulationStep)
                  .has_value(),
              "digital step rejected");
        check(advance_planetary_flight(
                  planet, {0}, analog, {}, kSimulationStep, {},
                  PlanetaryAnalogInput{axes[0], axes[1], axes[2], axes[3]})
                  .has_value(),
              "analog endpoint rejected");
        check(analog == digital, "analog endpoint changed digital state");
        check(planetary_flight_state_checksum(analog) ==
                  planetary_flight_state_checksum(digital),
              "endpoint checksum");
      }
    }
    auto partial = *initial;
    auto duplicate = *initial;
    auto full = *initial;
    for (int i = 0; i < 120; ++i) {
      for (auto* state : {&partial, &duplicate})
        check(advance_planetary_flight(planet, {0}, *state, {}, kSimulationStep,
                                       {},
                                       PlanetaryAnalogInput{0.0001, 0.25, 0, 0})
                  .has_value(),
              "fractional step");
      check(advance_planetary_flight(planet, {0}, full, {}, kSimulationStep, {},
                                     PlanetaryAnalogInput{1, 1, 0, 0})
                .has_value(),
            "full step");
    }
    check(partial == duplicate, "fractional input not deterministic");
    check(partial.pose.heading_radians != full.pose.heading_radians &&
              partial.velocity.east_metres_per_second !=
                  full.velocity.east_metres_per_second,
          "fractional controls collapsed to digital");
    auto mixed = *initial;
    const FlightCommand command{0, FlightCommandKind::press_forward};
    check(!advance_planetary_flight(planet, {0}, mixed, {&command, 1},
                                    kSimulationStep, {},
                                    PlanetaryAnalogInput{}),
          "ambiguous mixed input accepted");
    check(mixed == *initial, "mixed input mutated state");
    std::cout
        << "Analog flight contract: invalid axes, 81 endpoint combinations, "
           "fractional repeatability and mixed-input rejection passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
