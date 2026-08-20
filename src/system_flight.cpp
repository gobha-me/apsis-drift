#include "apsis_drift/system_flight.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>

namespace apsis_drift {
namespace {

struct Vec3 {
  double x{};
  double y{};
  double z{};
};

[[nodiscard]] auto vec(SystemPositionMetres value) noexcept -> Vec3 {
  return {value.x, value.y, value.z};
}
[[nodiscard]] auto vec(SystemVelocityMetresPerSecond value) noexcept -> Vec3 {
  return {value.x, value.y, value.z};
}
[[nodiscard]] auto vec(SystemDirection value) noexcept -> Vec3 {
  return {value.x, value.y, value.z};
}
[[nodiscard]] auto add(Vec3 a, Vec3 b) noexcept -> Vec3 {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
[[nodiscard]] auto subtract(Vec3 a, Vec3 b) noexcept -> Vec3 {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
[[nodiscard]] auto multiply(Vec3 value, double scale) noexcept -> Vec3 {
  return {value.x * scale, value.y * scale, value.z * scale};
}
[[nodiscard]] auto dot(Vec3 a, Vec3 b) noexcept -> double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
[[nodiscard]] auto cross(Vec3 a, Vec3 b) noexcept -> Vec3 {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}
[[nodiscard]] auto length(Vec3 value) noexcept -> double {
  return std::hypot(value.x, value.y, value.z);
}
[[nodiscard]] auto finite(Vec3 value) noexcept -> bool {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}
[[nodiscard]] auto bounded(Vec3 value, double maximum) noexcept -> bool {
  return finite(value) && std::abs(value.x) <= maximum &&
         std::abs(value.y) <= maximum && std::abs(value.z) <= maximum;
}
[[nodiscard]] auto normalized(Vec3 value) noexcept -> std::optional<Vec3> {
  const double magnitude = length(value);
  if (!finite(value) || !std::isfinite(magnitude) || magnitude <= 1.0e-12) {
    return std::nullopt;
  }
  return multiply(value, 1.0 / magnitude);
}

[[nodiscard]] auto valid_scale(SystemTimeScale value) noexcept -> bool {
  return value == SystemTimeScale::one || value == SystemTimeScale::four ||
         value == SystemTimeScale::sixteen;
}

[[nodiscard]] auto valid_mode(FlightMode value) noexcept -> bool {
  return value == FlightMode::manual || value == FlightMode::autopilot;
}

[[nodiscard]] auto valid_command(FlightCommandKind kind) noexcept -> bool {
  return static_cast<unsigned>(kind) <=
         static_cast<unsigned>(FlightCommandKind::increase_time_scale);
}

auto apply_command(SystemFlightState& state, FlightCommandKind kind) noexcept
    -> void {
  const auto manual = [&state](bool& control, bool value) {
    control = value;
    if (value) state.mode = FlightMode::manual;
  };
  switch (kind) {
    case FlightCommandKind::press_forward: manual(state.controls.forward, true); break;
    case FlightCommandKind::release_forward: state.controls.forward = false; break;
    case FlightCommandKind::press_backward: manual(state.controls.backward, true); break;
    case FlightCommandKind::release_backward: state.controls.backward = false; break;
    case FlightCommandKind::press_turn_left: manual(state.controls.turn_left, true); break;
    case FlightCommandKind::release_turn_left: state.controls.turn_left = false; break;
    case FlightCommandKind::press_turn_right: manual(state.controls.turn_right, true); break;
    case FlightCommandKind::release_turn_right: state.controls.turn_right = false; break;
    case FlightCommandKind::press_strafe_left: manual(state.controls.strafe_left, true); break;
    case FlightCommandKind::release_strafe_left: state.controls.strafe_left = false; break;
    case FlightCommandKind::press_strafe_right: manual(state.controls.strafe_right, true); break;
    case FlightCommandKind::release_strafe_right: state.controls.strafe_right = false; break;
    case FlightCommandKind::press_rise: manual(state.controls.rise, true); break;
    case FlightCommandKind::release_rise: state.controls.rise = false; break;
    case FlightCommandKind::press_fall: manual(state.controls.fall, true); break;
    case FlightCommandKind::release_fall: state.controls.fall = false; break;
    case FlightCommandKind::toggle_autopilot:
      state.mode = state.mode == FlightMode::autopilot ? FlightMode::manual
                                                       : FlightMode::autopilot;
      state.controls = {};
      break;
    case FlightCommandKind::decrease_time_scale:
      state.time_scale = state.time_scale == SystemTimeScale::sixteen
                             ? SystemTimeScale::four
                             : SystemTimeScale::one;
      break;
    case FlightCommandKind::increase_time_scale:
      state.time_scale = state.time_scale == SystemTimeScale::one
                             ? SystemTimeScale::four
                             : SystemTimeScale::sixteen;
      break;
  }
}

[[nodiscard]] auto planet_radius(const LocalSystemDescriptor& system,
                                 PlanetId target)
    -> std::expected<double, SystemFlightError> {
  const auto found = find_local_system_planet(system, target);
  if (!found) return std::unexpected{SystemFlightError::unknown_target};
  return static_cast<double>((*found)->descriptor.radius.value) * 1'000.0;
}

[[nodiscard]] auto guidance(const LocalSystemDescriptor& system,
                            const SystemFlightState& state)
    -> std::expected<SystemFlightGuidance, SystemFlightError> {
  const auto radius = planet_radius(system, state.target);
  if (!radius) return std::unexpected{radius.error()};
  const auto target = resolve_planet_ephemeris(
      system, state.target, {.tick = state.tick, .sub_tick_fraction = 0.0});
  if (!target) return std::unexpected{SystemFlightError::ephemeris_failure};
  const Vec3 relative_position =
      subtract(vec(target->position), vec(state.position));
  const double distance = length(relative_position);
  const auto direction = normalized(relative_position);
  if (!direction || !std::isfinite(distance)) {
    return std::unexpected{SystemFlightError::invalid_state};
  }
  const Vec3 craft_relative_velocity =
      subtract(vec(state.velocity), vec(target->velocity));
  const double relative_speed = length(craft_relative_velocity);
  const double closing = dot(craft_relative_velocity, *direction);
  const double radial_speed = std::abs(closing);
  const double stopping = closing > 0.0
                              ? closing * closing /
                                    (2.0 * kSystemFlightForwardAcceleration)
                              : 0.0;
  const double insertion_radius =
      *radius * kSystemOrbitInsertionRadiusRadii;
  const bool approach = distance <= *radius * kSystemApproachRadiusRadii;
  const bool ready =
      distance >= *radius + kMinimumFlightClearanceMetres &&
      distance <= insertion_radius &&
      relative_speed <= kSystemOrbitInsertionMaximumSpeed &&
      radial_speed <= kSystemOrbitInsertionMaximumRadialSpeed;
  const double remaining = std::max(0.0, distance - insertion_radius);
  SystemFlightCue cue = SystemFlightCue::hold;
  if (ready) {
    cue = SystemFlightCue::orbit_ready;
  } else if (closing < -0.5) {
    cue = SystemFlightCue::opening;
  } else if (closing > 0.5 && stopping >= remaining) {
    cue = SystemFlightCue::brake;
  } else if (closing > 0.5) {
    cue = SystemFlightCue::closing;
  }
  return SystemFlightGuidance{
      .target = state.target,
      .target_radius_metres = *radius,
      .distance_metres = distance,
      .closing_speed_metres_per_second = closing,
      .relative_speed_metres_per_second = relative_speed,
      .arrival_estimate_seconds = closing > 0.5
                                      ? std::optional<double>{remaining / closing}
                                      : std::nullopt,
      .stopping_distance_metres = stopping,
      .cue = cue,
      .inside_approach_boundary = approach,
      .orbit_insertion_ready = ready,
  };
}

auto advance_one_tick(const LocalSystemDescriptor& system,
                      SystemFlightState& state)
    -> std::expected<void, SystemFlightError> {
  if (state.tick == std::numeric_limits<SimulationTick>::max()) {
    return std::unexpected{SystemFlightError::tick_overflow};
  }
  const auto target = resolve_planet_ephemeris(
      system, state.target, {.tick = state.tick, .sub_tick_fraction = 0.0});
  const auto current_guidance = guidance(system, state);
  if (!target || !current_guidance) {
    return std::unexpected{target ? current_guidance.error()
                                  : SystemFlightError::ephemeris_failure};
  }
  const auto target_direction = normalized(
      subtract(vec(target->position), vec(state.position)));
  auto forward = normalized(vec(state.forward));
  auto up = normalized(vec(state.up));
  if (!target_direction || !forward || !up) {
    return std::unexpected{SystemFlightError::invalid_state};
  }
  constexpr double dt{1.0 / static_cast<double>(kSimulationHz)};
  if (state.mode == FlightMode::autopilot) {
    forward = target_direction;
    state.forward = {forward->x, forward->y, forward->z};
  } else {
    const int turn = static_cast<int>(state.controls.turn_left) -
                     static_cast<int>(state.controls.turn_right);
    if (turn != 0) {
      const double angle = static_cast<double>(turn) *
                           kSystemFlightTurnRateRadiansPerSecond * dt;
      const Vec3 rotated = add(multiply(*forward, std::cos(angle)),
                               multiply(cross(*up, *forward), std::sin(angle)));
      forward = normalized(rotated);
      if (!forward) return std::unexpected{SystemFlightError::invalid_state};
      state.forward = {forward->x, forward->y, forward->z};
    }
  }
  Vec3 acceleration{};
  const Vec3 relative_velocity =
      subtract(vec(state.velocity), vec(target->velocity));
  if (state.mode == FlightMode::autopilot) {
    const double radius = *planet_radius(system, state.target);
    const double remaining = std::max(
        0.0, current_guidance->distance_metres -
                 radius * kSystemOrbitInsertionRadiusRadii);
    const bool braking = remaining <= current_guidance->stopping_distance_metres +
                                          current_guidance->relative_speed_metres_per_second * dt;
    if (braking && current_guidance->relative_speed_metres_per_second > 1.0) {
      const auto opposite = normalized(multiply(relative_velocity, -1.0));
      if (opposite) {
        acceleration = multiply(*opposite, kSystemFlightForwardAcceleration);
      }
    } else if (remaining > 0.0) {
      acceleration = multiply(*target_direction,
                              kSystemFlightForwardAcceleration);
    }
  } else {
    const int thrust = static_cast<int>(state.controls.forward) -
                       static_cast<int>(state.controls.backward);
    acceleration = multiply(*forward,
                            static_cast<double>(thrust) *
                                kSystemFlightForwardAcceleration);
    auto right = normalized(cross(*forward, *up));
    if (!right) return std::unexpected{SystemFlightError::invalid_state};
    const int strafe = static_cast<int>(state.controls.strafe_right) -
                       static_cast<int>(state.controls.strafe_left);
    const int rise = static_cast<int>(state.controls.rise) -
                     static_cast<int>(state.controls.fall);
    acceleration = add(
        acceleration,
        add(multiply(*right, static_cast<double>(strafe) *
                                 kSystemFlightManeuverAcceleration),
            multiply(*up, static_cast<double>(rise) *
                              kSystemFlightManeuverAcceleration)));
  }
  Vec3 velocity = add(vec(state.velocity), multiply(acceleration, dt));
  Vec3 new_relative = subtract(velocity, vec(target->velocity));
  const double relative_speed = length(new_relative);
  if (!finite(velocity) || !std::isfinite(relative_speed)) {
    return std::unexpected{SystemFlightError::invalid_state};
  }
  const double maximum_relative_speed =
      state.mode == FlightMode::autopilot
          ? kSystemFlightAutopilotMaximumRelativeSpeed
          : kSystemFlightMaximumRelativeSpeed;
  if (relative_speed > maximum_relative_speed) {
    new_relative =
        multiply(new_relative, maximum_relative_speed / relative_speed);
    velocity = add(vec(target->velocity), new_relative);
  }
  const Vec3 position = add(vec(state.position), multiply(velocity, dt));
  if (!finite(position)) return std::unexpected{SystemFlightError::invalid_state};
  state.velocity = {velocity.x, velocity.y, velocity.z};
  state.position = {position.x, position.y, position.z};
  ++state.tick;
  return {};
}

auto hash_word(std::uint64_t& hash, std::uint64_t word) noexcept -> void {
  hash ^= word;
  hash *= 1099511628211ULL;
}

auto hash_bool(std::uint64_t& hash, bool value) noexcept -> void {
  hash_word(hash, static_cast<std::uint64_t>(value));
}

[[nodiscard]] auto inverse_spin(Vec3 system_vector, PlanetId planet,
                                SimulationTick tick) noexcept -> Vec3 {
  const SimulationTick offset = planet.value % kSystemPlanetFrameRotationTicks;
  const SimulationTick cycle =
      (offset + tick % kSystemPlanetFrameRotationTicks) %
      kSystemPlanetFrameRotationTicks;
  const double phase = 2.0 * std::numbers::pi_v<double> *
                       static_cast<double>(cycle) /
                       static_cast<double>(kSystemPlanetFrameRotationTicks);
  const double cosine = std::cos(phase);
  const double sine = std::sin(phase);
  return {cosine * system_vector.x + sine * system_vector.y,
          -sine * system_vector.x + cosine * system_vector.y,
          system_vector.z};
}

[[nodiscard]] auto forward_spin(Vec3 fixed_vector, PlanetId planet,
                                SimulationTick tick) noexcept -> Vec3 {
  const SimulationTick offset = planet.value % kSystemPlanetFrameRotationTicks;
  const SimulationTick cycle =
      (offset + tick % kSystemPlanetFrameRotationTicks) %
      kSystemPlanetFrameRotationTicks;
  const double phase = 2.0 * std::numbers::pi_v<double> *
                       static_cast<double>(cycle) /
                       static_cast<double>(kSystemPlanetFrameRotationTicks);
  const double cosine = std::cos(phase);
  const double sine = std::sin(phase);
  return {cosine * fixed_vector.x - sine * fixed_vector.y,
          sine * fixed_vector.x + cosine * fixed_vector.y,
          fixed_vector.z};
}

}  // namespace

auto initial_system_flight_state(const LocalSystemDescriptor& system,
                                 PlanetId target,
                                 const IntersystemArrivalSolution& arrival)
    -> std::expected<SystemFlightState, SystemFlightError> {
  if (!validate_local_system(system) || arrival.destination != system.id ||
      arrival.reference_planet != target || arrival.arrival_tick == 0) {
    return std::unexpected{SystemFlightError::invalid_arrival};
  }
  const auto ephemeris = resolve_planet_ephemeris(
      system, target, {.tick = arrival.arrival_tick, .sub_tick_fraction = 0.0});
  if (!ephemeris) return std::unexpected{SystemFlightError::unknown_target};
  const auto forward = normalized(subtract(vec(ephemeris->position),
                                           vec(arrival.position)));
  if (!forward) return std::unexpected{SystemFlightError::invalid_arrival};
  SystemFlightState result{
      .tick = arrival.arrival_tick,
      .system = system.id,
      .target = target,
      .position = arrival.position,
      .velocity = arrival.velocity,
      .forward = {forward->x, forward->y, forward->z},
      .up = {0.0, 0.0, 1.0},
      .mode = FlightMode::autopilot,
      .controls = {},
      .time_scale = SystemTimeScale::one,
  };
  if (!validate_system_flight_state(system, result)) {
    return std::unexpected{SystemFlightError::invalid_arrival};
  }
  return result;
}

auto validate_system_flight_state(const LocalSystemDescriptor& system,
                                  const SystemFlightState& state) noexcept
    -> std::expected<void, SystemFlightError> {
  if (!validate_local_system(system) || state.system != system.id) {
    return std::unexpected{SystemFlightError::invalid_system};
  }
  if (!find_local_system_planet(system, state.target)) {
    return std::unexpected{SystemFlightError::unknown_target};
  }
  const auto forward = normalized(vec(state.forward));
  const auto up = normalized(vec(state.up));
  if (!bounded(vec(state.position), 1.0e16) ||
      !bounded(vec(state.velocity), 1.0e9) ||
      !forward || !up || std::abs(dot(*forward, *up)) > 0.999 ||
      !valid_mode(state.mode) || !valid_scale(state.time_scale)) {
    return std::unexpected{SystemFlightError::invalid_state};
  }
  return {};
}

auto resolve_system_flight_guidance(const LocalSystemDescriptor& system,
                                    const SystemFlightState& state)
    -> std::expected<SystemFlightGuidance, SystemFlightError> {
  if (auto valid = validate_system_flight_state(system, state); !valid) {
    return std::unexpected{valid.error()};
  }
  return guidance(system, state);
}

auto advance_system_flight(const LocalSystemDescriptor& system,
                           SystemFlightState& state,
                           std::span<const FlightCommand> commands)
    -> std::expected<void, SystemFlightError> {
  if (auto valid = validate_system_flight_state(system, state); !valid) {
    return std::unexpected{valid.error()};
  }
  auto next = state;
  for (const auto& command : commands) {
    if (!valid_command(command.kind)) {
      return std::unexpected{SystemFlightError::invalid_command};
    }
    if (command.tick != state.tick) {
      return std::unexpected{SystemFlightError::wrong_command_tick};
    }
    apply_command(next, command.kind);
  }
  const auto initial_guidance = guidance(system, next);
  if (!initial_guidance) return std::unexpected{initial_guidance.error()};
  const SimulationTick steps = initial_guidance->inside_approach_boundary
                                   ? 1
                                   : system_time_scale_value(next.time_scale);
  if (initial_guidance->inside_approach_boundary) {
    next.time_scale = SystemTimeScale::one;
  }
  if (steps == 0 ||
      steps > system_time_scale_value(SystemTimeScale::sixteen)) {
    return std::unexpected{SystemFlightError::invalid_step};
  }
  if (next.tick > std::numeric_limits<SimulationTick>::max() - steps) {
    return std::unexpected{SystemFlightError::tick_overflow};
  }
  for (SimulationTick step = 0; step < steps; ++step) {
    if (auto advanced = advance_one_tick(system, next); !advanced) {
      return std::unexpected{advanced.error()};
    }
    const auto updated_guidance = guidance(system, next);
    if (!updated_guidance) {
      return std::unexpected{updated_guidance.error()};
    }
    if (updated_guidance->inside_approach_boundary) {
      next.time_scale = SystemTimeScale::one;
      break;
    }
  }
  state = std::move(next);
  return {};
}

auto insert_system_flight_orbit(const LocalSystemDescriptor& system,
                                const SystemFlightState& state)
    -> std::expected<PlanetaryFlightState, SystemFlightError> {
  const auto valid = validate_system_flight_state(system, state);
  if (!valid) return std::unexpected{valid.error()};
  const auto insertion = guidance(system, state);
  if (!insertion) return std::unexpected{insertion.error()};
  if (!insertion->orbit_insertion_ready) {
    return std::unexpected{SystemFlightError::orbit_insertion_refused};
  }
  const auto body = find_local_system_planet(system, state.target);
  const auto ephemeris = resolve_planet_ephemeris(
      system, state.target, {.tick = state.tick, .sub_tick_fraction = 0.0});
  if (!body || !ephemeris) {
    return std::unexpected{SystemFlightError::ephemeris_failure};
  }
  const Vec3 relative_position =
      subtract(vec(state.position), vec(ephemeris->position));
  const Vec3 relative_velocity =
      subtract(vec(state.velocity), vec(ephemeris->velocity));
  const Vec3 fixed_position = inverse_spin(relative_position, state.target,
                                           state.tick);
  Vec3 fixed_velocity = inverse_spin(relative_velocity, state.target,
                                     state.tick);
  constexpr double omega = 2.0 * std::numbers::pi_v<double> /
                           (static_cast<double>(kSystemPlanetFrameRotationTicks) /
                            static_cast<double>(kSimulationHz));
  fixed_velocity = subtract(fixed_velocity,
                            cross({0.0, 0.0, omega}, fixed_position));
  const PlanetFixedPositionMetres fixed{fixed_position.x, fixed_position.y,
                                        fixed_position.z};
  const auto geodetic =
      geodetic_from_planet_fixed((*body)->descriptor, fixed);
  if (!geodetic) return std::unexpected{SystemFlightError::coordinate_failure};
  const auto frame = make_local_tangent_frame((*body)->descriptor, *geodetic);
  if (!frame) return std::unexpected{SystemFlightError::coordinate_failure};
  const auto component = [&](PlanetFixedDirection axis) {
    return fixed_velocity.x * axis.x + fixed_velocity.y * axis.y +
           fixed_velocity.z * axis.z;
  };
  const double east = component(frame->east);
  const double north = component(frame->north);
  const double up_velocity = component(frame->up);
  const double heading = std::hypot(east, north) > 1.0e-9
                             ? std::atan2(north, east)
                             : 0.0;
  PlanetaryFlightState result{
      .tick = state.tick,
      .planet = state.target,
      .pose = {.position = *geodetic, .heading_radians = heading},
      .velocity = {east, north, up_velocity},
      .clearance_metres = geodetic->altitude_metres,
      .mode = state.mode,
      .controls = {},
      .regime = FlightRegime::orbital,
      .last_transition = std::nullopt,
      .thermal = {},
  };
  if (!validate_planetary_flight_state((*body)->descriptor, result)) {
    return std::unexpected{SystemFlightError::coordinate_failure};
  }
  return result;
}

auto depart_planetary_orbit(const LocalSystemDescriptor& system,
                            const PlanetaryFlightState& state)
    -> std::expected<SystemFlightState, SystemFlightError> {
  if (!validate_local_system(system) || state.regime != FlightRegime::orbital) {
    return std::unexpected{SystemFlightError::planet_departure_refused};
  }
  const auto body = find_local_system_planet(system, state.planet);
  if (!body ||
      !validate_planetary_flight_state((*body)->descriptor, state)) {
    return std::unexpected{SystemFlightError::invalid_state};
  }
  const auto ephemeris = resolve_planet_ephemeris(
      system, state.planet, {.tick = state.tick, .sub_tick_fraction = 0.0});
  const auto fixed_position =
      planet_fixed_from_geodetic((*body)->descriptor, state.pose.position);
  const auto frame = make_local_tangent_frame((*body)->descriptor,
                                               state.pose.position);
  if (!ephemeris || !fixed_position || !frame) {
    return std::unexpected{SystemFlightError::coordinate_failure};
  }
  const auto combine = [](PlanetFixedDirection east,
                          PlanetFixedDirection north,
                          PlanetFixedDirection up, double east_value,
                          double north_value, double up_value) noexcept {
    return Vec3{east.x * east_value + north.x * north_value + up.x * up_value,
                east.y * east_value + north.y * north_value + up.y * up_value,
                east.z * east_value + north.z * north_value + up.z * up_value};
  };
  const Vec3 fixed{fixed_position->x, fixed_position->y, fixed_position->z};
  Vec3 fixed_velocity = combine(
      frame->east, frame->north, frame->up,
      state.velocity.east_metres_per_second,
      state.velocity.north_metres_per_second,
      state.velocity.up_metres_per_second);
  constexpr double omega = 2.0 * std::numbers::pi_v<double> /
                           (static_cast<double>(kSystemPlanetFrameRotationTicks) /
                            static_cast<double>(kSimulationHz));
  fixed_velocity = add(fixed_velocity,
                       cross({0.0, 0.0, omega}, fixed));
  const Vec3 relative_position = forward_spin(fixed, state.planet, state.tick);
  const Vec3 relative_velocity =
      forward_spin(fixed_velocity, state.planet, state.tick);
  const Vec3 fixed_forward = combine(
      frame->east, frame->north, frame->up,
      std::cos(state.pose.heading_radians),
      std::sin(state.pose.heading_radians), 0.0);
  const Vec3 system_forward =
      forward_spin(fixed_forward, state.planet, state.tick);
  const Vec3 system_up = forward_spin(
      {frame->up.x, frame->up.y, frame->up.z}, state.planet, state.tick);
  SystemFlightState result{
      .tick = state.tick,
      .system = system.id,
      .target = state.planet,
      .position = {ephemeris->position.x + relative_position.x,
                   ephemeris->position.y + relative_position.y,
                   ephemeris->position.z + relative_position.z},
      .velocity = {ephemeris->velocity.x + relative_velocity.x,
                   ephemeris->velocity.y + relative_velocity.y,
                   ephemeris->velocity.z + relative_velocity.z},
      .forward = {system_forward.x, system_forward.y, system_forward.z},
      .up = {system_up.x, system_up.y, system_up.z},
      .mode = state.mode,
      .controls = {},
      .time_scale = SystemTimeScale::one,
  };
  if (!validate_system_flight_state(system, result)) {
    return std::unexpected{SystemFlightError::coordinate_failure};
  }
  return result;
}

auto system_flight_state_checksum(const SystemFlightState& state) noexcept
    -> std::uint64_t {
  std::uint64_t hash{1469598103934665603ULL};
  hash_word(hash, state.tick);
  hash_word(hash, state.system.value);
  hash_word(hash, state.target.value);
  for (const double value : {state.position.x, state.position.y, state.position.z,
                             state.velocity.x, state.velocity.y, state.velocity.z,
                             state.forward.x, state.forward.y, state.forward.z,
                             state.up.x, state.up.y, state.up.z}) {
    hash_word(hash, std::bit_cast<std::uint64_t>(value));
  }
  hash_word(hash, static_cast<std::uint64_t>(state.mode));
  hash_bool(hash, state.controls.forward);
  hash_bool(hash, state.controls.backward);
  hash_bool(hash, state.controls.turn_left);
  hash_bool(hash, state.controls.turn_right);
  hash_bool(hash, state.controls.strafe_left);
  hash_bool(hash, state.controls.strafe_right);
  hash_bool(hash, state.controls.rise);
  hash_bool(hash, state.controls.fall);
  hash_word(hash, static_cast<std::uint64_t>(state.time_scale));
  return hash;
}

}  // namespace apsis_drift
