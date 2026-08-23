#include "apsis_drift/origin_return.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

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
[[nodiscard]] auto normalized(Vec3 value) noexcept -> std::optional<Vec3> {
  const double magnitude = length(value);
  if (!finite(value) || !std::isfinite(magnitude) || magnitude <= 1.0e-12) {
    return std::nullopt;
  }
  return multiply(value, 1.0 / magnitude);
}
[[nodiscard]] auto valid_mode(FlightMode mode) noexcept -> bool {
  return mode == FlightMode::manual || mode == FlightMode::autopilot;
}
[[nodiscard]] auto valid_command(FlightCommandKind kind) noexcept -> bool {
  return static_cast<unsigned>(kind) <=
         static_cast<unsigned>(FlightCommandKind::increase_time_scale);
}

[[nodiscard]] auto station_flight_tick_matches(
    const IntersystemContractState& contract,
    const OriginStationFlightState& state) noexcept -> bool {
  if (contract.travel_phase == IntersystemTravelPhase::origin_system_flight ||
      contract.travel_phase == IntersystemTravelPhase::origin_system_return) {
    return state.tick == contract.universe_tick;
  }
  return contract.travel_phase ==
             IntersystemTravelPhase::outbound_jump_spooling &&
         contract.phase_started_tick &&
         state.tick == *contract.phase_started_tick;
}

[[nodiscard]] auto station_flight_phase_supported(
    IntersystemTravelPhase phase) noexcept -> bool {
  return phase == IntersystemTravelPhase::origin_system_flight ||
         phase == IntersystemTravelPhase::outbound_jump_spooling ||
         phase == IntersystemTravelPhase::origin_system_return;
}

[[nodiscard]] auto direction_to_station(
    const OriginStationFlightState& state) noexcept -> std::optional<Vec3> {
  const Vec3 offset = multiply(vec(state.relative_position), -1.0);
  if (const auto direction = normalized(offset))
    return direction;
  if (!finite(offset) || length(offset) > 1.0e-12)
    return std::nullopt;
  if (const auto braking =
          normalized(multiply(vec(state.relative_velocity), -1.0))) {
    return braking;
  }
  return normalized(vec(state.forward));
}

auto apply_command(OriginStationFlightState& state,
                   FlightCommandKind kind) noexcept -> void {
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
    case FlightCommandKind::increase_time_scale:
      break;
  }
}

[[nodiscard]] auto guidance(const OriginStationFlightState& state,
                            double acceleration_limit) noexcept
    -> std::expected<OriginStationFlightGuidance, OriginStationFlightError> {
  const Vec3 offset = multiply(vec(state.relative_position), -1.0);
  const double distance = length(offset);
  const auto direction = direction_to_station(state);
  const Vec3 relative_velocity = vec(state.relative_velocity);
  const double speed = length(relative_velocity);
  if (!direction || !std::isfinite(distance) || !std::isfinite(speed)) {
    return std::unexpected{OriginStationFlightError::invalid_state};
  }
  const double closing = dot(relative_velocity, *direction);
  const double stopping =
      closing > 0.0 ? closing * closing / (2.0 * acceleration_limit) : 0.0;
  const auto forward = normalized(vec(state.forward));
  const auto supplied_up = normalized(vec(state.up));
  const auto right = forward && supplied_up
                         ? normalized(cross(*forward, *supplied_up))
                         : std::nullopt;
  const auto camera_up = right && forward
                             ? normalized(cross(*right, *forward))
                             : std::nullopt;
  if (!forward || !right || !camera_up) {
    return std::unexpected{OriginStationFlightError::invalid_state};
  }
  const double forward_component = dot(*direction, *forward);
  const double bearing = std::atan2(dot(*direction, *right),
                                    forward_component);
  const double elevation =
      std::asin(std::clamp(dot(*direction, *camera_up), -1.0, 1.0));
  if (!std::isfinite(bearing) || !std::isfinite(elevation)) {
    return std::unexpected{OriginStationFlightError::invalid_state};
  }
  const bool within_rendezvous = distance <= kOriginStationArrivalRadiusMetres;
  const bool arrived =
      within_rendezvous && speed <= kOriginStationDockingSpeedMetresPerSecond;
  OriginStationFlightCue cue = OriginStationFlightCue::hold;
  if (arrived) {
    cue = OriginStationFlightCue::arrived;
  } else if (within_rendezvous) {
    cue = OriginStationFlightCue::brake;
  } else if (closing < -0.5) {
    cue = OriginStationFlightCue::opening;
  } else if (closing > 0.5 &&
             stopping >= distance - kOriginStationArrivalRadiusMetres) {
    cue = OriginStationFlightCue::brake;
  } else if (closing > 0.5) {
    cue = OriginStationFlightCue::closing;
  }
  return OriginStationFlightGuidance{
      .distance_metres = distance,
      .closing_speed_metres_per_second = closing,
      .relative_speed_metres_per_second = speed,
      .stopping_distance_metres = stopping,
      .bearing_radians = bearing,
      .elevation_radians = elevation,
      .cue = cue,
      .in_front = forward_component > 0.0,
      .within_rendezvous = within_rendezvous,
      .arrived = arrived,
  };
}

auto hash_word(std::uint64_t& hash, std::uint64_t value) noexcept -> void {
  hash ^= value;
  hash *= 1099511628211ULL;
}

}  // namespace

auto resolve_origin_station_waypoint(
    const FirstIntersystemIdentities& identities,
    const LocalSystemDescriptor& origin_system, EphemerisQueryTime time)
    -> std::expected<OriginStationWaypoint, OriginStationFlightError> {
  const auto station = generate_origin_station(identities.universe_seed);
  if (station.id != identities.origin_station ||
      station.home_system_seed != identities.origin_system_seed ||
      origin_system.id != identities.origin_system) {
    return std::unexpected{OriginStationFlightError::invalid_waypoint};
  }
  return resolve_origin_station_waypoint(identities.universe_seed,
                                         origin_system, time);
}

auto resolve_origin_station_waypoint(Seed universe_seed,
                                     const LocalSystemDescriptor& origin_system,
                                     EphemerisQueryTime time)
    -> std::expected<OriginStationWaypoint, OriginStationFlightError> {
  const auto station = generate_origin_station(universe_seed);
  if (origin_system.id.value != station.home_system_seed.value ||
      origin_system.kind != LocalSystemKind::origin_home) {
    return std::unexpected{OriginStationFlightError::invalid_waypoint};
  }
  const auto ephemeris =
      resolve_origin_station_ephemeris(origin_system, station, time);
  if (!ephemeris) {
    return std::unexpected{OriginStationFlightError::invalid_waypoint};
  }
  return OriginStationWaypoint{
      .system = origin_system.id,
      .station = station.id,
      .position = ephemeris->position,
      .velocity = ephemeris->velocity,
  };
}

auto initialize_origin_station_launch(
    Seed universe_seed, SimulationTick tick,
    const LocalSystemDescriptor& origin_system)
    -> std::expected<OriginStationFlightState, OriginStationFlightError> {
  const auto waypoint = resolve_origin_station_waypoint(
      universe_seed, origin_system, {.tick = tick, .sub_tick_fraction = 0.0});
  if (!waypoint) return std::unexpected{waypoint.error()};
  OriginStationFlightState result{
      .tick = tick,
      .system = waypoint->system,
      .station = waypoint->station,
      .relative_position = {kOriginStationLaunchStandoffMetres, 0.0, 0.0},
      .relative_velocity = {},
      .forward = {1.0, 0.0, 0.0},
      .up = {0.0, 0.0, 1.0},
      .mode = FlightMode::manual,
      .controls = {},
  };
  if (!validate_origin_station_flight_state(universe_seed, tick, origin_system,
                                            result)) {
    return std::unexpected{OriginStationFlightError::invalid_state};
  }
  return result;
}

auto initialize_origin_station_launch(
    const IntersystemContractState& contract,
    const LocalSystemDescriptor& origin_system)
    -> std::expected<OriginStationFlightState, OriginStationFlightError> {
  if (!validate_intersystem_contract_state(contract) ||
      contract.travel_phase != IntersystemTravelPhase::origin_system_flight ||
      contract.mission_phase != IntersystemMissionPhase::active) {
    return std::unexpected{OriginStationFlightError::invalid_contract};
  }
  return initialize_origin_station_launch(
      contract.identities.universe_seed, contract.universe_tick, origin_system);
}

auto initialize_origin_station_approach(
    Seed universe_seed, const LocalSystemDescriptor& origin_system,
    const SystemFlightState& departure)
    -> std::expected<OriginStationFlightState, OriginStationFlightError> {
  const auto binding = generate_home_signal_contract(universe_seed);
  if (departure.tick == std::numeric_limits<SimulationTick>::max() ||
      departure.system != origin_system.id ||
      departure.target != binding.home_planet ||
      !validate_system_flight_state(origin_system, departure)) {
    return std::unexpected{OriginStationFlightError::invalid_arrival};
  }
  const auto waypoint = resolve_origin_station_waypoint(
      universe_seed, origin_system,
      {.tick = departure.tick, .sub_tick_fraction = 0.0});
  if (!waypoint) return std::unexpected{waypoint.error()};
  const Vec3 relative_position =
      subtract(vec(departure.position), vec(waypoint->position));
  const Vec3 relative_velocity =
      subtract(vec(departure.velocity), vec(waypoint->velocity));
  const auto forward = normalized(multiply(relative_position, -1.0));
  if (!forward) {
    return std::unexpected{OriginStationFlightError::invalid_arrival};
  }
  Vec3 up = vec(departure.up);
  if (const auto normalized_up = normalized(up);
      !normalized_up || std::abs(dot(*forward, *normalized_up)) > 0.95) {
    up = std::abs(forward->z) < 0.95 ? Vec3{0.0, 0.0, 1.0}
                                      : Vec3{0.0, 1.0, 0.0};
  }
  OriginStationFlightState result{
      .tick = departure.tick,
      .system = origin_system.id,
      .station = binding.station,
      .relative_position = {relative_position.x, relative_position.y,
                            relative_position.z},
      .relative_velocity = {relative_velocity.x, relative_velocity.y,
                            relative_velocity.z},
      .forward = {forward->x, forward->y, forward->z},
      .up = {up.x, up.y, up.z},
      .mode = FlightMode::autopilot,
      .controls = {},
  };
  if (!validate_origin_station_flight_state(universe_seed, departure.tick,
                                            origin_system, result)) {
    return std::unexpected{OriginStationFlightError::invalid_arrival};
  }
  return result;
}

auto initialize_origin_return(const IntersystemContractState& contract,
                              const LocalSystemDescriptor& origin_system)
    -> std::expected<OriginStationFlightState, OriginStationFlightError> {
  if (!validate_intersystem_contract_state(contract) ||
      contract.travel_phase != IntersystemTravelPhase::origin_system_return ||
      !contract.arrival_solution ||
      contract.arrival_solution->destination != contract.identities.origin_system ||
      contract.arrival_solution->reference_planet) {
    return std::unexpected{OriginStationFlightError::invalid_contract};
  }
  const auto waypoint = resolve_origin_station_waypoint(
      contract.identities, origin_system,
      {.tick = contract.universe_tick, .sub_tick_fraction = 0.0});
  if (!waypoint)
    return std::unexpected{waypoint.error()};
  const Vec3 relative_position = subtract(
      vec(contract.arrival_solution->position), vec(waypoint->position));
  const Vec3 relative_velocity = subtract(
      vec(contract.arrival_solution->velocity), vec(waypoint->velocity));
  const Vec3 direction = multiply(relative_position, -1.0);
  const auto forward = normalized(direction);
  if (!forward)
    return std::unexpected{OriginStationFlightError::invalid_arrival};
  OriginStationFlightState result{
      .tick = contract.universe_tick,
      .system = contract.identities.origin_system,
      .station = contract.identities.origin_station,
      .relative_position = {relative_position.x, relative_position.y,
                            relative_position.z},
      .relative_velocity = {relative_velocity.x, relative_velocity.y,
                            relative_velocity.z},
      .forward = {forward->x, forward->y, forward->z},
      .up = {0.0, 0.0, 1.0},
      .mode = FlightMode::autopilot,
      .controls = {},
  };
  if (!validate_origin_station_flight_state(contract, origin_system, result)) {
    return std::unexpected{OriginStationFlightError::invalid_arrival};
  }
  return result;
}

auto validate_origin_station_flight_state(
    Seed universe_seed, SimulationTick authoritative_tick,
    const LocalSystemDescriptor& origin_system,
    const OriginStationFlightState& state)
    -> std::expected<void, OriginStationFlightError> {
  const auto binding = generate_home_signal_contract(universe_seed);
  const auto waypoint = resolve_origin_station_waypoint(
      universe_seed, origin_system,
      {.tick = state.tick, .sub_tick_fraction = 0.0});
  if (state.tick != authoritative_tick || state.system != origin_system.id ||
      state.station != binding.station || !waypoint ||
      !finite(vec(state.relative_position)) ||
      !finite(vec(state.relative_velocity)) ||
      !normalized(vec(state.forward)) || !normalized(vec(state.up)) ||
      std::abs(dot(*normalized(vec(state.forward)),
                   *normalized(vec(state.up)))) > 0.999 ||
      !valid_mode(state.mode) || std::abs(state.relative_position.x) > 1.0e15 ||
      std::abs(state.relative_position.y) > 1.0e15 ||
      std::abs(state.relative_position.z) > 1.0e15 ||
      std::abs(state.relative_velocity.x) > 1.0e9 ||
      std::abs(state.relative_velocity.y) > 1.0e9 ||
      std::abs(state.relative_velocity.z) > 1.0e9) {
    return std::unexpected{OriginStationFlightError::invalid_state};
  }
  return {};
}

auto validate_origin_station_flight_state(
    const IntersystemContractState& contract,
    const LocalSystemDescriptor& origin_system,
    const OriginStationFlightState& state)
    -> std::expected<void, OriginStationFlightError> {
  if (!validate_intersystem_contract_state(contract) ||
      !station_flight_phase_supported(contract.travel_phase) ||
      !station_flight_tick_matches(contract, state) ||
      !validate_origin_station_flight_state(contract.identities.universe_seed,
                                            state.tick, origin_system, state)) {
    return std::unexpected{OriginStationFlightError::invalid_state};
  }
  return {};
}

auto resolve_origin_station_flight_guidance(
    Seed universe_seed, SimulationTick authoritative_tick,
    const LocalSystemDescriptor& origin_system,
    const OriginStationFlightState& state)
    -> std::expected<OriginStationFlightGuidance, OriginStationFlightError> {
  if (!validate_origin_station_flight_state(universe_seed, authoritative_tick,
                                            origin_system, state)) {
    return std::unexpected{OriginStationFlightError::invalid_state};
  }
  return guidance(state, kHomeSignalStationFlightAcceleration);
}

auto resolve_origin_station_flight_guidance(
    const IntersystemContractState& contract,
    const LocalSystemDescriptor& origin_system,
    const OriginStationFlightState& state)
    -> std::expected<OriginStationFlightGuidance, OriginStationFlightError> {
  if (!validate_origin_station_flight_state(contract, origin_system, state)) {
    return std::unexpected{OriginStationFlightError::invalid_state};
  }
  return guidance(state, kOriginStationFlightAcceleration);
}

auto resolve_origin_station_flight_pose(
    Seed universe_seed, SimulationTick authoritative_tick,
    const LocalSystemDescriptor& origin_system,
    const OriginStationFlightState& state)
    -> std::expected<OriginStationFlightPose, OriginStationFlightError> {
  if (!validate_origin_station_flight_state(universe_seed, authoritative_tick,
                                            origin_system, state)) {
    return std::unexpected{OriginStationFlightError::invalid_state};
  }
  const auto waypoint = resolve_origin_station_waypoint(
      universe_seed, origin_system,
      {.tick = state.tick, .sub_tick_fraction = 0.0});
  if (!waypoint) return std::unexpected{waypoint.error()};
  return OriginStationFlightPose{
      .position = {waypoint->position.x + state.relative_position.x,
                   waypoint->position.y + state.relative_position.y,
                   waypoint->position.z + state.relative_position.z},
      .velocity = {waypoint->velocity.x + state.relative_velocity.x,
                   waypoint->velocity.y + state.relative_velocity.y,
                   waypoint->velocity.z + state.relative_velocity.z},
  };
}

auto resolve_origin_station_flight_pose(
    const IntersystemContractState& contract,
    const LocalSystemDescriptor& origin_system,
    const OriginStationFlightState& state)
    -> std::expected<OriginStationFlightPose, OriginStationFlightError> {
  if (!validate_origin_station_flight_state(contract, origin_system, state)) {
    return std::unexpected{OriginStationFlightError::invalid_state};
  }
  return resolve_origin_station_flight_pose(contract.identities.universe_seed,
                                            state.tick, origin_system, state);
}

static auto advance_origin_station_flight_with_limits(
    Seed universe_seed, SimulationTick authoritative_tick,
    const LocalSystemDescriptor& origin_system, OriginStationFlightState& state,
    std::span<const FlightCommand> commands, double acceleration_limit,
    double maximum_speed) -> std::expected<void, OriginStationFlightError> {
  if (!validate_origin_station_flight_state(universe_seed, authoritative_tick,
                                            origin_system, state)) {
    return std::unexpected{OriginStationFlightError::invalid_state};
  }
  if (state.tick == std::numeric_limits<SimulationTick>::max()) {
    return std::unexpected{OriginStationFlightError::tick_overflow};
  }
  auto next = state;
  for (const auto& command : commands) {
    if (!valid_command(command.kind)) {
      return std::unexpected{OriginStationFlightError::invalid_command};
    }
    if (command.tick != state.tick) {
      return std::unexpected{OriginStationFlightError::wrong_command_tick};
    }
    apply_command(next, command.kind);
  }
  const auto current = guidance(next, acceleration_limit);
  if (!current) return std::unexpected{current.error()};
  const auto target_direction = direction_to_station(next);
  auto forward = normalized(vec(next.forward));
  auto up = normalized(vec(next.up));
  if (!target_direction || !forward || !up) {
    return std::unexpected{OriginStationFlightError::invalid_state};
  }
  constexpr double dt{1.0 / static_cast<double>(kSimulationHz)};
  if (next.mode == FlightMode::autopilot) {
    forward = target_direction;
    next.forward = {forward->x, forward->y, forward->z};
    if (std::abs(dot(*forward, *up)) > 0.95) {
      const Vec3 reference = std::abs(forward->z) < 0.95 ? Vec3{0.0, 0.0, 1.0}
                                                         : Vec3{0.0, 1.0, 0.0};
      const auto right = normalized(cross(*forward, reference));
      const auto corrected_up =
          right ? normalized(cross(*right, *forward)) : std::nullopt;
      if (!corrected_up) {
        return std::unexpected{OriginStationFlightError::invalid_state};
      }
      up = corrected_up;
      next.up = {up->x, up->y, up->z};
    }
  } else {
    const int turn = static_cast<int>(next.controls.turn_left) -
                     static_cast<int>(next.controls.turn_right);
    if (turn != 0) {
      const double angle = static_cast<double>(turn) *
                           kOriginStationFlightTurnRateRadiansPerSecond * dt;
      const auto rotated =
          normalized(add(multiply(*forward, std::cos(angle)),
                         multiply(cross(*up, *forward), std::sin(angle))));
      if (!rotated)
        return std::unexpected{OriginStationFlightError::invalid_state};
      forward = rotated;
      next.forward = {forward->x, forward->y, forward->z};
    }
  }
  Vec3 acceleration{};
  const Vec3 relative_velocity = vec(next.relative_velocity);
  if (next.mode == FlightMode::autopilot) {
    const double remaining = std::max(
        0.0, current->distance_metres - kOriginStationArrivalRadiusMetres);
    const double desired_speed = std::min(
        maximum_speed, std::sqrt(2.0 * acceleration_limit * remaining));
    const Vec3 desired_velocity = multiply(*target_direction, desired_speed);
    const Vec3 velocity_error = subtract(desired_velocity, relative_velocity);
    const double error_speed = length(velocity_error);
    if (!std::isfinite(error_speed)) {
      return std::unexpected{OriginStationFlightError::invalid_state};
    }
    if (error_speed > 0.0) {
      const double requested_acceleration = error_speed / dt;
      acceleration = multiply(
          velocity_error,
          std::min(acceleration_limit, requested_acceleration) / error_speed);
    }
  } else {
    const int thrust = static_cast<int>(next.controls.forward) -
                       static_cast<int>(next.controls.backward);
    acceleration =
        multiply(*forward, static_cast<double>(thrust) * acceleration_limit);
    const auto right = normalized(cross(*forward, *up));
    if (!right) return std::unexpected{OriginStationFlightError::invalid_state};
    const int strafe = static_cast<int>(next.controls.strafe_right) -
                       static_cast<int>(next.controls.strafe_left);
    const int rise = static_cast<int>(next.controls.rise) -
                     static_cast<int>(next.controls.fall);
    acceleration = add(
        acceleration,
        add(multiply(*right, static_cast<double>(strafe) * acceleration_limit),
            multiply(*up, static_cast<double>(rise) * acceleration_limit)));
  }
  Vec3 velocity = add(vec(next.relative_velocity), multiply(acceleration, dt));
  const double speed = length(velocity);
  if (!finite(velocity) || !std::isfinite(speed)) {
    return std::unexpected{OriginStationFlightError::invalid_state};
  }
  if (speed > maximum_speed) {
    velocity = multiply(velocity, maximum_speed / speed);
  }
  const Vec3 position =
      add(vec(next.relative_position), multiply(velocity, dt));
  if (!finite(position))
    return std::unexpected{OriginStationFlightError::invalid_state};
  next.relative_position = {position.x, position.y, position.z};
  next.relative_velocity = {velocity.x, velocity.y, velocity.z};
  ++next.tick;
  if (!validate_origin_station_flight_state(
          universe_seed, authoritative_tick + 1, origin_system, next)) {
    return std::unexpected{OriginStationFlightError::invalid_state};
  }
  state = std::move(next);
  return {};
}

auto advance_origin_station_flight(Seed universe_seed,
                                   SimulationTick authoritative_tick,
                                   const LocalSystemDescriptor& origin_system,
                                   OriginStationFlightState& state,
                                   std::span<const FlightCommand> commands)
    -> std::expected<void, OriginStationFlightError> {
  return advance_origin_station_flight_with_limits(
      universe_seed, authoritative_tick, origin_system, state, commands,
      kHomeSignalStationFlightAcceleration,
      kHomeSignalStationFlightMaximumSpeed);
}

auto advance_origin_station_flight(const IntersystemContractState& contract,
                                   const LocalSystemDescriptor& origin_system,
                                   OriginStationFlightState& state,
                                   std::span<const FlightCommand> commands)
    -> std::expected<void, OriginStationFlightError> {
  if (contract.travel_phase != IntersystemTravelPhase::origin_system_flight &&
      contract.travel_phase != IntersystemTravelPhase::origin_system_return) {
    return std::unexpected{OriginStationFlightError::invalid_state};
  }
  return advance_origin_station_flight_with_limits(
      contract.identities.universe_seed, contract.universe_tick, origin_system,
      state, commands, kOriginStationFlightAcceleration,
      kOriginStationFlightMaximumSpeed);
}

auto attempt_origin_docking(IntersystemContractState& contract,
                            const LocalSystemDescriptor& origin_system,
                            const OriginStationFlightState& state)
    -> std::expected<void, OriginStationFlightError> {
  const auto guidance =
      resolve_origin_station_flight_guidance(contract, origin_system, state);
  if (!guidance || !guidance->arrived) {
    return std::unexpected{OriginStationFlightError::invalid_arrival};
  }
  auto next = contract;
  if (!advance_intersystem_contract(
          next, next.universe_tick,
          IntersystemContractCommand::dock_at_origin)) {
    return std::unexpected{OriginStationFlightError::invalid_contract};
  }
  contract = std::move(next);
  return {};
}

auto origin_station_flight_state_checksum(
    const OriginStationFlightState& state) noexcept -> std::uint64_t {
  std::uint64_t hash{1469598103934665603ULL};
  hash_word(hash, state.tick);
  hash_word(hash, state.system.value);
  hash_word(hash, state.station.value);
  for (double value :
       {state.relative_position.x, state.relative_position.y,
        state.relative_position.z, state.relative_velocity.x,
        state.relative_velocity.y, state.relative_velocity.z, state.forward.x,
        state.forward.y, state.forward.z, state.up.x, state.up.y, state.up.z}) {
    hash_word(hash, std::bit_cast<std::uint64_t>(value));
  }
  hash_word(hash, static_cast<std::uint64_t>(state.mode));
  hash_word(hash, static_cast<std::uint64_t>(state.controls.forward));
  hash_word(hash, static_cast<std::uint64_t>(state.controls.backward));
  hash_word(hash, static_cast<std::uint64_t>(state.controls.turn_left));
  hash_word(hash, static_cast<std::uint64_t>(state.controls.turn_right));
  hash_word(hash, static_cast<std::uint64_t>(state.controls.strafe_left));
  hash_word(hash, static_cast<std::uint64_t>(state.controls.strafe_right));
  hash_word(hash, static_cast<std::uint64_t>(state.controls.rise));
  hash_word(hash, static_cast<std::uint64_t>(state.controls.fall));
  return hash;
}

}  // namespace apsis_drift
