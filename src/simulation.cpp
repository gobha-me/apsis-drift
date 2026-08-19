#include "apsis_drift/simulation.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace apsis_drift {
namespace {

[[nodiscard]] auto valid_mode(FlightMode mode) noexcept -> bool {
  switch (mode) {
    case FlightMode::manual:
    case FlightMode::autopilot: return true;
  }
  return false;
}

[[nodiscard]] auto finite_state(const FlightState& state) noexcept -> bool {
  constexpr float coordinate_limit =
      static_cast<float>(std::numeric_limits<int>::max() / 2);
  return std::isfinite(state.pose.x) && std::isfinite(state.pose.y) &&
         std::isfinite(state.pose.altitude) &&
         std::isfinite(state.pose.yaw) &&
         std::isfinite(state.velocity.x) &&
         std::isfinite(state.velocity.y) &&
         std::isfinite(state.velocity.vertical) &&
         std::isfinite(state.clearance) &&
         std::abs(state.pose.x) <= coordinate_limit &&
         std::abs(state.pose.y) <= coordinate_limit &&
         valid_mode(state.mode);
}

[[nodiscard]] auto valid_command(FlightCommandKind kind) noexcept -> bool {
  switch (kind) {
    case FlightCommandKind::press_forward:
    case FlightCommandKind::release_forward:
    case FlightCommandKind::press_backward:
    case FlightCommandKind::release_backward:
    case FlightCommandKind::press_turn_left:
    case FlightCommandKind::release_turn_left:
    case FlightCommandKind::press_turn_right:
    case FlightCommandKind::release_turn_right:
    case FlightCommandKind::press_strafe_left:
    case FlightCommandKind::release_strafe_left:
    case FlightCommandKind::press_strafe_right:
    case FlightCommandKind::release_strafe_right:
    case FlightCommandKind::press_rise:
    case FlightCommandKind::release_rise:
    case FlightCommandKind::press_fall:
    case FlightCommandKind::release_fall:
    case FlightCommandKind::toggle_autopilot: return true;
    case FlightCommandKind::decrease_time_scale:
    case FlightCommandKind::increase_time_scale: return false;
  }
  return false;
}

auto clear_controls(FlightControls& controls) noexcept -> void {
  controls = {};
}

auto apply_command(FlightState& state, FlightCommandKind kind) noexcept
    -> void {
  switch (kind) {
    case FlightCommandKind::press_forward:
      state.controls.forward = true;
      state.mode = FlightMode::manual;
      break;
    case FlightCommandKind::release_forward:
      state.controls.forward = false;
      break;
    case FlightCommandKind::press_backward:
      state.controls.backward = true;
      state.mode = FlightMode::manual;
      break;
    case FlightCommandKind::release_backward:
      state.controls.backward = false;
      break;
    case FlightCommandKind::press_turn_left:
      state.controls.turn_left = true;
      state.mode = FlightMode::manual;
      break;
    case FlightCommandKind::release_turn_left:
      state.controls.turn_left = false;
      break;
    case FlightCommandKind::press_turn_right:
      state.controls.turn_right = true;
      state.mode = FlightMode::manual;
      break;
    case FlightCommandKind::release_turn_right:
      state.controls.turn_right = false;
      break;
    case FlightCommandKind::press_strafe_left:
      state.controls.strafe_left = true;
      state.mode = FlightMode::manual;
      break;
    case FlightCommandKind::release_strafe_left:
      state.controls.strafe_left = false;
      break;
    case FlightCommandKind::press_strafe_right:
      state.controls.strafe_right = true;
      state.mode = FlightMode::manual;
      break;
    case FlightCommandKind::release_strafe_right:
      state.controls.strafe_right = false;
      break;
    case FlightCommandKind::press_rise:
      state.controls.rise = true;
      state.mode = FlightMode::manual;
      break;
    case FlightCommandKind::release_rise:
      state.controls.rise = false;
      break;
    case FlightCommandKind::press_fall:
      state.controls.fall = true;
      state.mode = FlightMode::manual;
      break;
    case FlightCommandKind::release_fall:
      state.controls.fall = false;
      break;
    case FlightCommandKind::toggle_autopilot:
      state.mode = state.mode == FlightMode::autopilot
                       ? FlightMode::manual
                       : FlightMode::autopilot;
      clear_controls(state.controls);
      break;
    case FlightCommandKind::decrease_time_scale:
    case FlightCommandKind::increase_time_scale: break;
  }
}

auto hash_word(std::uint64_t& hash, std::uint64_t value) noexcept -> void {
  constexpr std::uint64_t prime{1099511628211ULL};
  for (int byte = 0; byte < 8; ++byte) {
    hash ^= (value >> (byte * 8)) & 0xFFU;
    hash *= prime;
  }
}

auto hash_bool(std::uint64_t& hash, bool value) noexcept -> void {
  hash_word(hash, value ? 1U : 0U);
}

}  // namespace

auto FixedStepClock::advance(SimulationSeconds elapsed) noexcept
    -> std::expected<SimulationAdvance, SimulationTimeError> {
  if (!std::isfinite(elapsed.count())) {
    return std::unexpected{SimulationTimeError::non_finite_elapsed};
  }
  if (elapsed < SimulationSeconds::zero()) {
    return std::unexpected{SimulationTimeError::negative_elapsed};
  }

  SimulationAdvance result;
  if (elapsed > kMaxCatchUp) {
    result.dropped = elapsed - kMaxCatchUp;
    elapsed = kMaxCatchUp;
  }

  m_accumulator += elapsed;
  constexpr double boundary_epsilon = kSimulationStep.count() * 1.0e-9;
  result.steps = std::min(
      kMaxCatchUpSteps,
      static_cast<int>(std::floor(
          (m_accumulator.count() + boundary_epsilon) /
          kSimulationStep.count())));
  m_accumulator -= kSimulationStep * result.steps;
  if (m_accumulator.count() < 0.0 &&
      m_accumulator.count() >= -boundary_epsilon) {
    m_accumulator = SimulationSeconds::zero();
  }
  result.interpolation_alpha = std::clamp(
      m_accumulator.count() / kSimulationStep.count(), 0.0, 1.0);
  return result;
}

auto initial_flight_state(const Terrain& terrain) noexcept
    -> std::expected<FlightState, FlightError> {
  FlightState state;
  state.pose.altitude =
      std::max<float>(terrain.height_at(static_cast<int>(state.pose.x),
                                        static_cast<int>(state.pose.y)),
                      kWaterLevel) +
      state.clearance;
  if (!finite_state(state)) {
    return std::unexpected{FlightError::invalid_state};
  }
  return state;
}

auto advance_flight(const Terrain& terrain, FlightState& state,
                    std::span<const FlightCommand> commands,
                    SimulationSeconds step) noexcept
    -> std::expected<void, FlightError> {
  if (!finite_state(state)) {
    return std::unexpected{FlightError::invalid_state};
  }
  if (!std::isfinite(step.count()) || step != kSimulationStep) {
    return std::unexpected{FlightError::invalid_step};
  }
  if (state.tick == std::numeric_limits<SimulationTick>::max()) {
    return std::unexpected{FlightError::tick_overflow};
  }
  for (const auto& command : commands) {
    if (!valid_command(command.kind)) {
      return std::unexpected{FlightError::invalid_command};
    }
    if (command.tick != state.tick) {
      return std::unexpected{FlightError::wrong_command_tick};
    }
  }

  FlightState next = state;
  for (const auto& command : commands) apply_command(next, command.kind);

  float forward = static_cast<float>(next.controls.forward) -
                  static_cast<float>(next.controls.backward);
  float turn = static_cast<float>(next.controls.turn_right) -
               static_cast<float>(next.controls.turn_left);
  float strafe = static_cast<float>(next.controls.strafe_right) -
                 static_cast<float>(next.controls.strafe_left);
  float vertical = static_cast<float>(next.controls.rise) -
                   static_cast<float>(next.controls.fall);
  if (next.mode == FlightMode::autopilot) {
    forward = 0.72F;
    turn = 0.055F;
    strafe = 0.0F;
    vertical = 0.0F;
  }

  const float dt = static_cast<float>(kSimulationStep.count());
  next.pose.yaw += turn * 1.15F * dt;
  const float forward_x = std::cos(next.pose.yaw);
  const float forward_y = std::sin(next.pose.yaw);
  const float right_x = -forward_y;
  const float right_y = forward_x;
  constexpr float speed{52.0F};
  next.velocity.x = (forward_x * forward + right_x * strafe) * speed;
  next.velocity.y = (forward_y * forward + right_y * strafe) * speed;
  next.pose.x += next.velocity.x * dt;
  next.pose.y += next.velocity.y * dt;
  next.clearance = std::clamp(next.clearance + vertical * 45.0F * dt,
                              16.0F, 160.0F);

  if (!finite_state(next)) {
    return std::unexpected{FlightError::invalid_state};
  }

  const float world = static_cast<float>(terrain.size());
  next.pose.x = std::fmod(std::fmod(next.pose.x, world) + world, world);
  next.pose.y = std::fmod(std::fmod(next.pose.y, world) + world, world);
  const float floor = std::max<float>(
      terrain.height_at(static_cast<int>(next.pose.x),
                        static_cast<int>(next.pose.y)),
      kWaterLevel);
  const float target_altitude = floor + next.clearance;
  const float previous_altitude = next.pose.altitude;
  next.pose.altitude += (target_altitude - next.pose.altitude) *
                        std::min(1.0F, dt * 3.0F);
  next.velocity.vertical =
      (next.pose.altitude - previous_altitude) / dt;
  ++next.tick;

  if (!finite_state(next)) {
    return std::unexpected{FlightError::invalid_state};
  }
  state = next;
  return {};
}

auto derive_camera(const FlightState& state) noexcept
    -> std::expected<Camera, FlightError> {
  if (!finite_state(state)) {
    return std::unexpected{FlightError::invalid_state};
  }
  Camera camera;
  camera.x = state.pose.x;
  camera.y = state.pose.y;
  camera.height = state.pose.altitude;
  camera.yaw = state.pose.yaw;
  return camera;
}

auto flight_state_checksum(const FlightState& state) noexcept
    -> std::uint64_t {
  constexpr std::uint64_t offset{1469598103934665603ULL};
  std::uint64_t hash = offset;
  hash_word(hash, state.tick);
  hash_word(hash, std::bit_cast<std::uint32_t>(state.pose.x));
  hash_word(hash, std::bit_cast<std::uint32_t>(state.pose.y));
  hash_word(hash, std::bit_cast<std::uint32_t>(state.pose.altitude));
  hash_word(hash, std::bit_cast<std::uint32_t>(state.pose.yaw));
  hash_word(hash, std::bit_cast<std::uint32_t>(state.velocity.x));
  hash_word(hash, std::bit_cast<std::uint32_t>(state.velocity.y));
  hash_word(hash, std::bit_cast<std::uint32_t>(state.velocity.vertical));
  hash_word(hash, std::bit_cast<std::uint32_t>(state.clearance));
  hash_word(hash, static_cast<std::uint8_t>(state.mode));
  hash_bool(hash, state.controls.forward);
  hash_bool(hash, state.controls.backward);
  hash_bool(hash, state.controls.turn_left);
  hash_bool(hash, state.controls.turn_right);
  hash_bool(hash, state.controls.strafe_left);
  hash_bool(hash, state.controls.strafe_right);
  hash_bool(hash, state.controls.rise);
  hash_bool(hash, state.controls.fall);
  return hash;
}

}  // namespace apsis_drift
