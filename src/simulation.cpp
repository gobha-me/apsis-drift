#include "simulation.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace apsis_drift::detail {
namespace {

[[nodiscard]] auto finite_camera(const Camera& camera) noexcept -> bool {
  constexpr float coordinate_limit =
      static_cast<float>(std::numeric_limits<int>::max() / 2);
  return std::isfinite(camera.x) && std::isfinite(camera.y) &&
         std::isfinite(camera.height) && std::isfinite(camera.yaw) &&
         std::isfinite(camera.horizon) && std::isfinite(camera.clearance) &&
         std::abs(camera.x) <= coordinate_limit &&
         std::abs(camera.y) <= coordinate_limit;
}

[[nodiscard]] auto finite_input(const FlightInput& input) noexcept -> bool {
  return std::isfinite(input.forward) && std::isfinite(input.turn) &&
         std::isfinite(input.strafe) && std::isfinite(input.vertical);
}

auto hash_word(std::uint64_t& hash, std::uint64_t value) noexcept -> void {
  constexpr std::uint64_t prime{1099511628211ULL};
  for (int byte = 0; byte < 8; ++byte) {
    hash ^= (value >> (byte * 8)) & 0xFFU;
    hash *= prime;
  }
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

auto advance_flight(const Terrain& terrain, FlightRuntime& state,
                    FlightInput input, SimulationSeconds step) noexcept
    -> bool {
  if (!finite_camera(state.camera) ||
      !std::isfinite(state.elapsed_seconds) || !finite_input(input) ||
      !std::isfinite(step.count()) || step <= SimulationSeconds::zero()) {
    return false;
  }

  FlightRuntime next = state;
  next.elapsed_seconds += step.count();
  if (input.autopilot) {
    input.forward = 0.72F;
    input.turn = 0.055F;
  }

  const float dt = static_cast<float>(step.count());
  next.camera.yaw += input.turn * 1.15F * dt;
  const float forward_x = std::cos(next.camera.yaw);
  const float forward_y = std::sin(next.camera.yaw);
  const float right_x = -forward_y;
  const float right_y = forward_x;
  constexpr float speed{52.0F};
  next.camera.x +=
      (forward_x * input.forward + right_x * input.strafe) * speed * dt;
  next.camera.y +=
      (forward_y * input.forward + right_y * input.strafe) * speed * dt;
  next.camera.clearance = std::clamp(
      next.camera.clearance + input.vertical * 45.0F * dt, 16.0F, 160.0F);

  if (!finite_camera(next.camera) || !std::isfinite(next.elapsed_seconds)) {
    return false;
  }

  const float world = static_cast<float>(terrain.size());
  next.camera.x = std::fmod(next.camera.x + world, world);
  next.camera.y = std::fmod(next.camera.y + world, world);
  if (!finite_camera(next.camera)) return false;
  const float floor = std::max<float>(
      terrain.height_at(static_cast<int>(next.camera.x),
                        static_cast<int>(next.camera.y)),
      kWaterLevel);
  const float target_height = floor + next.camera.clearance;
  next.camera.height +=
      (target_height - next.camera.height) * std::min(1.0F, dt * 3.0F);

  if (!finite_camera(next.camera) || !std::isfinite(next.elapsed_seconds)) {
    return false;
  }
  state = next;
  return true;
}

auto flight_state_checksum(const FlightRuntime& state) noexcept
    -> std::uint64_t {
  constexpr std::uint64_t offset{1469598103934665603ULL};
  std::uint64_t hash = offset;
  hash_word(hash, std::bit_cast<std::uint32_t>(state.camera.x));
  hash_word(hash, std::bit_cast<std::uint32_t>(state.camera.y));
  hash_word(hash, std::bit_cast<std::uint32_t>(state.camera.height));
  hash_word(hash, std::bit_cast<std::uint32_t>(state.camera.yaw));
  hash_word(hash, std::bit_cast<std::uint32_t>(state.camera.clearance));
  hash_word(hash, std::bit_cast<std::uint64_t>(state.elapsed_seconds));
  return hash;
}

}  // namespace apsis_drift::detail
