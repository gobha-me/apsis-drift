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

auto apply_command(OriginReturnState& state, FlightCommandKind kind) noexcept
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
    case FlightCommandKind::increase_time_scale:
      break;
  }
}

[[nodiscard]] auto guidance(const OriginStationWaypoint& waypoint,
                            const OriginReturnState& state) noexcept
    -> std::expected<OriginReturnGuidance, OriginReturnError> {
  const Vec3 offset = subtract(vec(waypoint.position), vec(state.position));
  const double distance = length(offset);
  const auto direction = normalized(offset);
  const Vec3 relative_velocity =
      subtract(vec(state.velocity), vec(waypoint.velocity));
  const double speed = length(relative_velocity);
  if (!direction || !std::isfinite(distance) || !std::isfinite(speed)) {
    return std::unexpected{OriginReturnError::invalid_state};
  }
  const double closing = dot(relative_velocity, *direction);
  const double stopping = closing > 0.0
                              ? closing * closing /
                                    (2.0 * kOriginReturnAcceleration)
                              : 0.0;
  const bool arrived = distance <= kOriginStationArrivalRadiusMetres;
  OriginReturnCue cue = OriginReturnCue::hold;
  if (arrived) {
    cue = OriginReturnCue::arrived;
  } else if (closing < -0.5) {
    cue = OriginReturnCue::opening;
  } else if (closing > 0.5 &&
             stopping >= distance - kOriginStationArrivalRadiusMetres) {
    cue = OriginReturnCue::brake;
  } else if (closing > 0.5) {
    cue = OriginReturnCue::closing;
  }
  return OriginReturnGuidance{distance, closing, speed, stopping, cue, arrived};
}

auto hash_word(std::uint64_t& hash, std::uint64_t value) noexcept -> void {
  hash ^= value;
  hash *= 1099511628211ULL;
}

}  // namespace

auto generate_origin_station_waypoint(
    const FirstIntersystemIdentities& identities) noexcept
    -> OriginStationWaypoint {
  return {.system = identities.origin_system,
          .station = identities.origin_station,
          .position = {kOriginStationApproachOffsetMetres,
                       kOriginStationSystemYMetres, 0.0},
          .velocity = {}};
}

auto initialize_origin_return(const IntersystemContractState& contract)
    -> std::expected<OriginReturnState, OriginReturnError> {
  if (!validate_intersystem_contract_state(contract) ||
      contract.travel_phase != IntersystemTravelPhase::origin_system_return ||
      !contract.arrival_solution ||
      contract.arrival_solution->destination != contract.identities.origin_system ||
      contract.arrival_solution->reference_planet) {
    return std::unexpected{OriginReturnError::invalid_contract};
  }
  const auto waypoint = generate_origin_station_waypoint(contract.identities);
  const Vec3 direction = subtract(vec(waypoint.position),
                                  vec(contract.arrival_solution->position));
  const auto forward = normalized(direction);
  if (!forward) return std::unexpected{OriginReturnError::invalid_arrival};
  OriginReturnState result{
      .tick = contract.universe_tick,
      .system = contract.identities.origin_system,
      .station = contract.identities.origin_station,
      .position = contract.arrival_solution->position,
      .velocity = contract.arrival_solution->velocity,
      .forward = {forward->x, forward->y, forward->z},
      .up = {0.0, 0.0, 1.0},
      .mode = FlightMode::autopilot,
      .controls = {},
  };
  if (!validate_origin_return_state(contract, result)) {
    return std::unexpected{OriginReturnError::invalid_arrival};
  }
  return result;
}

auto validate_origin_return_state(const IntersystemContractState& contract,
                                  const OriginReturnState& state) noexcept
    -> std::expected<void, OriginReturnError> {
  if (!validate_intersystem_contract_state(contract) ||
      contract.travel_phase != IntersystemTravelPhase::origin_system_return ||
      state.tick != contract.universe_tick ||
      state.system != contract.identities.origin_system ||
      state.station != contract.identities.origin_station ||
      !finite(vec(state.position)) || !finite(vec(state.velocity)) ||
      !normalized(vec(state.forward)) || !normalized(vec(state.up)) ||
      std::abs(dot(*normalized(vec(state.forward)),
                   *normalized(vec(state.up)))) > 0.999 ||
      !valid_mode(state.mode) ||
      std::abs(state.position.x) > 1.0e15 ||
      std::abs(state.position.y) > 1.0e15 ||
      std::abs(state.position.z) > 1.0e15 ||
      std::abs(state.velocity.x) > 1.0e9 ||
      std::abs(state.velocity.y) > 1.0e9 ||
      std::abs(state.velocity.z) > 1.0e9) {
    return std::unexpected{OriginReturnError::invalid_state};
  }
  return {};
}

auto resolve_origin_return_guidance(const IntersystemContractState& contract,
                                    const OriginReturnState& state) noexcept
    -> std::expected<OriginReturnGuidance, OriginReturnError> {
  if (!validate_origin_return_state(contract, state)) {
    return std::unexpected{OriginReturnError::invalid_state};
  }
  return guidance(generate_origin_station_waypoint(contract.identities), state);
}

auto advance_origin_return(const IntersystemContractState& contract,
                           OriginReturnState& state,
                           std::span<const FlightCommand> commands) noexcept
    -> std::expected<void, OriginReturnError> {
  if (!validate_origin_return_state(contract, state)) {
    return std::unexpected{OriginReturnError::invalid_state};
  }
  if (state.tick == std::numeric_limits<SimulationTick>::max()) {
    return std::unexpected{OriginReturnError::tick_overflow};
  }
  auto next = state;
  for (const auto& command : commands) {
    if (!valid_command(command.kind)) {
      return std::unexpected{OriginReturnError::invalid_command};
    }
    if (command.tick != state.tick) {
      return std::unexpected{OriginReturnError::wrong_command_tick};
    }
    apply_command(next, command.kind);
  }
  const auto waypoint = generate_origin_station_waypoint(contract.identities);
  const auto current = guidance(waypoint, next);
  if (!current) return std::unexpected{current.error()};
  const auto target_direction = normalized(
      subtract(vec(waypoint.position), vec(next.position)));
  auto forward = normalized(vec(next.forward));
  auto up = normalized(vec(next.up));
  if (!target_direction || !forward || !up) {
    return std::unexpected{OriginReturnError::invalid_state};
  }
  constexpr double dt{1.0 / static_cast<double>(kSimulationHz)};
  if (next.mode == FlightMode::autopilot) {
    forward = target_direction;
    next.forward = {forward->x, forward->y, forward->z};
  } else {
    const int turn = static_cast<int>(next.controls.turn_left) -
                     static_cast<int>(next.controls.turn_right);
    if (turn != 0) {
      const double angle = static_cast<double>(turn) *
                           kOriginReturnTurnRateRadiansPerSecond * dt;
      const auto rotated = normalized(add(
          multiply(*forward, std::cos(angle)),
          multiply(cross(*up, *forward), std::sin(angle))));
      if (!rotated) return std::unexpected{OriginReturnError::invalid_state};
      forward = rotated;
      next.forward = {forward->x, forward->y, forward->z};
    }
  }
  Vec3 acceleration{};
  const Vec3 relative_velocity =
      subtract(vec(next.velocity), vec(waypoint.velocity));
  if (next.mode == FlightMode::autopilot) {
    const double remaining =
        std::max(0.0, current->distance_metres -
                          kOriginStationArrivalRadiusMetres);
    const bool braking =
        remaining <= current->stopping_distance_metres +
                         current->relative_speed_metres_per_second * dt;
    if (braking && current->relative_speed_metres_per_second > 0.1) {
      const auto opposite = normalized(multiply(relative_velocity, -1.0));
      if (opposite) acceleration = multiply(*opposite, kOriginReturnAcceleration);
    } else if (!current->arrived) {
      acceleration = multiply(*target_direction, kOriginReturnAcceleration);
    }
  } else {
    const int thrust = static_cast<int>(next.controls.forward) -
                       static_cast<int>(next.controls.backward);
    acceleration = multiply(*forward,
                            static_cast<double>(thrust) *
                                kOriginReturnAcceleration);
    const auto right = normalized(cross(*forward, *up));
    if (!right) return std::unexpected{OriginReturnError::invalid_state};
    const int strafe = static_cast<int>(next.controls.strafe_right) -
                       static_cast<int>(next.controls.strafe_left);
    const int rise = static_cast<int>(next.controls.rise) -
                     static_cast<int>(next.controls.fall);
    acceleration = add(
        acceleration,
        add(multiply(*right, static_cast<double>(strafe) *
                                 kOriginReturnAcceleration),
            multiply(*up, static_cast<double>(rise) *
                              kOriginReturnAcceleration)));
  }
  Vec3 velocity = add(vec(next.velocity), multiply(acceleration, dt));
  Vec3 new_relative = subtract(velocity, vec(waypoint.velocity));
  const double speed = length(new_relative);
  if (!finite(velocity) || !std::isfinite(speed)) {
    return std::unexpected{OriginReturnError::invalid_state};
  }
  if (speed > kOriginReturnMaximumSpeed) {
    new_relative = multiply(new_relative, kOriginReturnMaximumSpeed / speed);
    velocity = add(vec(waypoint.velocity), new_relative);
  }
  const Vec3 position = add(vec(next.position), multiply(velocity, dt));
  if (!finite(position)) return std::unexpected{OriginReturnError::invalid_state};
  next.position = {position.x, position.y, position.z};
  next.velocity = {velocity.x, velocity.y, velocity.z};
  ++next.tick;
  auto next_contract = contract;
  if (!advance_intersystem_time(next_contract, 1) ||
      !validate_origin_return_state(next_contract, next)) {
    return std::unexpected{OriginReturnError::invalid_state};
  }
  state = std::move(next);
  return {};
}

auto origin_return_state_checksum(const OriginReturnState& state) noexcept
    -> std::uint64_t {
  std::uint64_t hash{1469598103934665603ULL};
  hash_word(hash, state.tick);
  hash_word(hash, state.system.value);
  hash_word(hash, state.station.value);
  for (double value : {state.position.x, state.position.y, state.position.z,
                       state.velocity.x, state.velocity.y, state.velocity.z,
                       state.forward.x, state.forward.y, state.forward.z,
                       state.up.x, state.up.y, state.up.z}) {
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

auto render_origin_station_marker(
    int width, int height, std::span<termforge::Pixel> destination) noexcept
    -> std::expected<void, OriginReturnError> {
  if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
      static_cast<std::size_t>(width) >
          std::numeric_limits<std::size_t>::max() /
              static_cast<std::size_t>(height) ||
      destination.size() != static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height)) {
    return std::unexpected{OriginReturnError::invalid_framebuffer};
  }
  const int cx = width / 2;
  const int cy = height / 2;
  constexpr termforge::Pixel color{126, 214, 210, 255};
  const auto set = [&](int x, int y) {
    if (x >= 0 && x < width && y >= 0 && y < height) {
      destination[static_cast<std::size_t>(y) *
                      static_cast<std::size_t>(width) +
                  static_cast<std::size_t>(x)] = color;
    }
  };
  for (int offset = -8; offset <= 8; ++offset) {
    if (std::abs(offset) >= 4) {
      set(cx + offset, cy);
      set(cx, cy + offset);
    }
  }
  return {};
}

}  // namespace apsis_drift
