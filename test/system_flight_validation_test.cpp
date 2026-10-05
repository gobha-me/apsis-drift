#include "apsis_drift/system_flight.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {
using namespace apsis_drift;
int checks{};
int failures{};
auto check(bool condition, std::string_view message) -> void {
  ++checks;
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}
template <typename T, typename E>
auto required(std::expected<T, E> result) -> T {
  if (!result) throw std::runtime_error("valid public fixture failed");
  if constexpr (!std::is_void_v<T>) return *result;
}
auto scalars(SystemFlightState& state) -> std::array<double*, 12> {
  return {&state.position.x, &state.position.y, &state.position.z,
          &state.velocity.x, &state.velocity.y, &state.velocity.z,
          &state.forward.x,  &state.forward.y,  &state.forward.z,
          &state.up.x,       &state.up.y,       &state.up.z};
}
auto same_bits(SystemFlightState left, SystemFlightState right) -> bool {
  const auto a = scalars(left), b = scalars(right);
  for (std::size_t i = 0; i < a.size(); ++i)
    if (std::bit_cast<std::uint64_t>(*a[i]) !=
        std::bit_cast<std::uint64_t>(*b[i]))
      return false;
  return left.tick == right.tick && left.system == right.system &&
         left.target == right.target && left.mode == right.mode &&
         left.controls == right.controls && left.time_scale == right.time_scale;
}
auto state_at(const LocalSystemDescriptor& system, SimulationTick tick = 1000)
    -> SystemFlightState {
  const auto& target = system.planets[1].descriptor;
  const auto body =
      required(resolve_planet_ephemeris(system, target.id, {tick, 0}));
  const double radius = static_cast<double>(target.radius.value) * 1000;
  return {.tick = tick,
          .system = system.id,
          .target = target.id,
          .position = {body.position.x + 30 * radius, body.position.y,
                       body.position.z},
          .velocity = body.velocity,
          .forward = {-1, 0, 0},
          .up = {0, 0, 1},
          .mode = FlightMode::autopilot,
          .controls = {},
          .time_scale = SystemTimeScale::one};
}
auto refused(const LocalSystemDescriptor& system, SystemFlightState state,
             SystemFlightError error,
             std::span<const FlightCommand> commands = {}) -> void {
  const auto before = state;
  check(advance_system_flight(system, state, commands) ==
            std::unexpected{error},
        "public advance preserves the refusal category");
  check(same_bits(state, before),
        "refusal preserves every state field and bit");
}
auto invalid_state(const LocalSystemDescriptor& system, SystemFlightState state,
                   SystemFlightError error = SystemFlightError::invalid_state)
    -> void {
  check(validate_system_flight_state(system, state) == std::unexpected{error} &&
            resolve_system_flight_guidance(system, state) ==
                std::unexpected{error} &&
            insert_system_flight_orbit(system, state) == std::unexpected{error},
        "validation, guidance and insertion retain invalid-state precedence");
  refused(system, state, error);
}
auto catalogs(const LocalSystemDescriptor& system) -> void {
  const auto state = state_at(system);
  auto bad = system;
  bad.planets[0].orbit.period_ticks = 0;
  invalid_state(bad, state, SystemFlightError::invalid_system);
  auto wrong_star = system;
  wrong_star.star.id.value ^= 1U;
  invalid_state(wrong_star, state, SystemFlightError::invalid_system);
  // Planet descriptors are immutable: rebuild the catalog with a wrong
  // nontarget descriptor, retaining its original orbit and the valid target.
  auto replaced = system;
  replaced.planets.clear();
  const auto foreign = generate_planet_descriptor(
      Seed{system.planets[0].descriptor.seed.value ^ 1U});
  for (std::size_t i = 0; i < system.planets.size(); ++i)
    replaced.planets.push_back({i == 0 ? foreign : system.planets[i].descriptor,
                                system.planets[i].orbit});
  invalid_state(replaced, state, SystemFlightError::invalid_system);
  auto short_catalog = system;
  while (short_catalog.planets.size() >= kMinimumLocalSystemPlanets)
    short_catalog.planets.pop_back();
  invalid_state(short_catalog, state, SystemFlightError::invalid_system);
  auto excessive = system;
  while (excessive.planets.size() <= kMaximumLocalSystemPlanets)
    excessive.planets.push_back(system.planets[0]);
  invalid_state(excessive, state, SystemFlightError::invalid_system);
  auto missing = state;
  missing.target = PlanetId{0};
  invalid_state(system, missing, SystemFlightError::unknown_target);
  missing.position.x = std::numeric_limits<double>::quiet_NaN();
  invalid_state(system, missing, SystemFlightError::unknown_target);
  invalid_state(replaced, missing, SystemFlightError::invalid_system);
  auto wrong_system = missing;
  wrong_system.system.value ^= 1U;
  invalid_state(system, wrong_system, SystemFlightError::invalid_system);
}
auto rejected_inputs(const LocalSystemDescriptor& system) -> void {
  const auto source = state_at(system);
  for (std::size_t i = 0; i < 12; ++i) {
    for (const double value : {std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity(),
                               -std::numeric_limits<double>::infinity()}) {
      auto state = source;
      *scalars(state)[i] = value;
      invalid_state(system, state);
    }
  }
  for (const bool velocity : {false, true}) {
    const double bound = velocity ? 1.0e9 : 1.0e16;
    for (const double sign : {-1.0, 1.0}) {
      auto state = source;
      auto& coordinate = velocity ? state.velocity.x : state.position.x;
      coordinate = sign * bound;
      check(validate_system_flight_state(system, state).has_value(),
            "finite coordinate bounds are inclusive");
      coordinate =
          sign * std::nextafter(bound, std::numeric_limits<double>::infinity());
      invalid_state(system, state);
    }
  }
  for (const bool up : {false, true}) {
    auto state = source;
    if (up)
      state.up = {0, 0, 0};
    else
      state.forward = {0, 0, 0};
    invalid_state(system, state);
  }
  auto parallel = source;
  parallel.up = parallel.forward;
  invalid_state(system, parallel);
  auto invalid_mode = source;
  invalid_mode.mode = static_cast<FlightMode>(255);
  invalid_state(system, invalid_mode);
  auto invalid_scale = source;
  invalid_scale.time_scale = static_cast<SystemTimeScale>(32);
  invalid_state(system, invalid_scale);
  const std::array late{
      FlightCommand{source.tick + 1, FlightCommandKind::press_forward}};
  refused(system, source, SystemFlightError::wrong_command_tick, late);
  const std::array malformed{
      FlightCommand{source.tick + 1, static_cast<FlightCommandKind>(255)}};
  refused(system, source, SystemFlightError::invalid_command, malformed);
  const std::array partial{
      FlightCommand{source.tick, FlightCommandKind::press_forward},
      FlightCommand{source.tick + 1, FlightCommandKind::press_rise}};
  refused(system, source, SystemFlightError::wrong_command_tick, partial);
  invalid_scale.target = PlanetId{0};
  refused(system, invalid_scale, SystemFlightError::unknown_target, malformed);
  for (const auto scale : {SystemTimeScale::one, SystemTimeScale::sixteen}) {
    auto overflow =
        state_at(system, std::numeric_limits<SimulationTick>::max() -
                             (scale == SystemTimeScale::one ? 0U : 8U));
    overflow.time_scale = scale;
    refused(system, overflow, SystemFlightError::tick_overflow);
  }
}
auto compare_steps(const LocalSystemDescriptor& system,
                   SystemFlightState source,
                   std::span<const FlightCommand> commands,
                   SimulationTick steps) -> void {
  auto compressed = source;
  auto reference = source;
  reference.time_scale = SystemTimeScale::one;
  std::vector<FlightCommand> control_commands;
  for (const auto command : commands)
    if (command.kind != FlightCommandKind::increase_time_scale &&
        command.kind != FlightCommandKind::decrease_time_scale)
      control_commands.push_back(command);
  required(advance_system_flight(system, compressed, commands));
  for (SimulationTick i = 0; i < steps; ++i)
    required(advance_system_flight(
        system, reference,
        i == 0 ? std::span<const FlightCommand>{control_commands}
               : std::span<const FlightCommand>{}));
  check(compressed.tick == source.tick + steps &&
            compressed.tick == reference.tick,
        "compression advances the same bounded authoritative ticks");
  reference.time_scale = compressed.time_scale;
  check(
      same_bits(compressed, reference),
      "compressed motion exactly matches independent one-tick public advances");
  check(resolve_system_flight_guidance(system, compressed) ==
            resolve_system_flight_guidance(system, reference),
        "guidance matches at the same final authoritative tick");
}
auto valid_motion(const LocalSystemDescriptor& system) -> void {
  for (const auto mode : {FlightMode::manual, FlightMode::autopilot}) {
    for (const auto scale : {SystemTimeScale::one, SystemTimeScale::four,
                             SystemTimeScale::sixteen}) {
      auto source = state_at(system);
      source.mode = mode;
      source.time_scale = scale;
      compare_steps(system, source, {}, system_time_scale_value(scale));
      const std::array thrust{
          FlightCommand{source.tick, FlightCommandKind::press_forward},
          FlightCommand{source.tick, FlightCommandKind::press_turn_left},
          FlightCommand{source.tick, FlightCommandKind::press_strafe_right},
          FlightCommand{source.tick, FlightCommandKind::press_rise}};
      compare_steps(system, source, thrust, system_time_scale_value(scale));
      const std::array toggle{
          FlightCommand{source.tick, FlightCommandKind::toggle_autopilot}};
      compare_steps(system, source, toggle, system_time_scale_value(scale));
      source.controls = {.forward = true,
                         .backward = false,
                         .turn_left = true,
                         .turn_right = false,
                         .strafe_left = false,
                         .strafe_right = true,
                         .rise = true,
                         .fall = false};
      const std::array release{
          FlightCommand{source.tick, FlightCommandKind::release_forward},
          FlightCommand{source.tick, FlightCommandKind::release_turn_left},
          FlightCommand{source.tick, FlightCommandKind::release_strafe_right},
          FlightCommand{source.tick, FlightCommandKind::release_rise}};
      compare_steps(system, source, release, system_time_scale_value(scale));
    }
  }
  auto source = state_at(system);
  const std::array faster{
      FlightCommand{source.tick, FlightCommandKind::increase_time_scale},
      FlightCommand{source.tick, FlightCommandKind::increase_time_scale}};
  compare_steps(system, source, std::span<const FlightCommand>{faster}.first(1),
                4);
  compare_steps(system, source, faster, 16);
  source.time_scale = SystemTimeScale::sixteen;
  const std::array slower{
      FlightCommand{source.tick, FlightCommandKind::decrease_time_scale},
      FlightCommand{source.tick, FlightCommandKind::decrease_time_scale}};
  compare_steps(system, source, std::span<const FlightCommand>{slower}.first(1),
                4);
  compare_steps(system, source, slower, 1);
  const auto body = required(
      resolve_planet_ephemeris(system, source.target, {source.tick, 0}));
  const double radius =
      static_cast<double>(system.planets[1].descriptor.radius.value) * 1000;
  source.position = {body.position.x + 6 * radius + 1000, body.position.y,
                     body.position.z};
  source.velocity.x = body.velocity.x - kSystemFlightMaximumRelativeSpeed;
  source.mode = FlightMode::manual;
  compare_steps(system, source, {}, 1);
}
auto orbit_insertion(const LocalSystemDescriptor& system) -> void {
  auto state = state_at(system);
  const auto& target = system.planets[1].descriptor;
  const auto body =
      required(resolve_planet_ephemeris(system, target.id, {state.tick, 0}));
  const double radius = static_cast<double>(target.radius.value) * 1000;
  check(insert_system_flight_orbit(system, state) ==
            std::unexpected{SystemFlightError::orbit_insertion_refused},
        "valid distant flight cannot insert into target orbit");
  state.position.x = body.position.x + 2.5 * radius;
  const auto before = state;
  const auto inserted = required(insert_system_flight_orbit(system, state));
  check(inserted.tick == state.tick && inserted.planet == target.id &&
            inserted.regime == FlightRegime::orbital &&
            validate_planetary_flight_state(target, inserted).has_value() &&
            same_bits(state, before),
        "authentic matched-velocity insertion produces valid orbital state");
  for (const double distance :
       {0.5 * radius, radius + 0.5 * kMinimumFlightClearanceMetres}) {
    state.position.x = body.position.x + distance;
    check(validate_system_flight_state(system, state).has_value() &&
              insert_system_flight_orbit(system, state) ==
                  std::unexpected{SystemFlightError::orbit_insertion_refused},
          "below-surface and insufficient-clearance flight refuse insertion");
  }
}
auto mutation_between_calls(LocalSystemDescriptor system) -> void {
  auto state = state_at(system);
  required(resolve_system_flight_guidance(system, state));
  required(advance_system_flight(system, state, {}));
  const auto saved_period = system.planets[0].orbit.period_ticks;
  system.planets[0].orbit.period_ticks = 0;
  invalid_state(system, state, SystemFlightError::invalid_system);
  system.planets[0].orbit.period_ticks = saved_period;
  required(resolve_system_flight_guidance(system, state));
  required(advance_system_flight(system, state, {}));
  auto wrong_target = state;
  wrong_target.target = PlanetId{0};
  invalid_state(system, wrong_target, SystemFlightError::unknown_target);
  // A valid new target also must be resolved after a successful previous call.
  const auto& other = system.planets[0].descriptor;
  const auto body =
      required(resolve_planet_ephemeris(system, other.id, {state.tick, 0}));
  state.target = other.id;
  state.position = {body.position.x +
                        30 * static_cast<double>(other.radius.value) * 1000,
                    body.position.y, body.position.z};
  state.velocity = body.velocity;
  check(required(resolve_system_flight_guidance(system, state)).target ==
            other.id,
        "a later public call resolves its changed target");
  compare_steps(system, state, {}, 1);
}
} // namespace

auto main() -> int {
  try {
    for (const auto& system :
         {generate_origin_system(Seed{42}), generate_local_system(Seed{7})}) {
      catalogs(system);
      rejected_inputs(system);
      valid_motion(system);
      orbit_insertion(system);
      mutation_between_calls(system);
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    ++failures;
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
