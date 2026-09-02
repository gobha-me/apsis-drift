#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <span>

#include "apsis_drift/landscape.hpp"

namespace apsis_drift {

using SimulationSeconds = std::chrono::duration<double>;
using SimulationTick = std::uint64_t;

inline constexpr int kSimulationHz{120};
inline constexpr int kMaxCatchUpSteps{15};
inline constexpr SimulationSeconds kSimulationStep{1.0 / kSimulationHz};
inline constexpr SimulationSeconds kMaxCatchUp{kSimulationStep *
                                               kMaxCatchUpSteps};
inline constexpr double kLowClearanceWarningMetres{24.0};

enum class SimulationTimeError : std::uint8_t {
  negative_elapsed,
  non_finite_elapsed,
};

struct SimulationAdvance {
  int steps{};
  SimulationSeconds dropped{};
  double interpolation_alpha{};
};

class FixedStepClock {
 public:
  [[nodiscard]] auto advance(SimulationSeconds elapsed) noexcept
      -> std::expected<SimulationAdvance, SimulationTimeError>;
  auto reset() noexcept -> void { m_accumulator = SimulationSeconds::zero(); }

  [[nodiscard]] auto accumulator() const noexcept -> SimulationSeconds {
    return m_accumulator;
  }

 private:
  SimulationSeconds m_accumulator{};
};

enum class FlightMode : std::uint8_t { manual, autopilot };

enum class FlightCommandKind : std::uint8_t {
  press_forward,
  release_forward,
  press_backward,
  release_backward,
  press_turn_left,
  release_turn_left,
  press_turn_right,
  release_turn_right,
  press_strafe_left,
  release_strafe_left,
  press_strafe_right,
  release_strafe_right,
  press_rise,
  release_rise,
  press_fall,
  release_fall,
  toggle_autopilot,
  decrease_time_scale,
  increase_time_scale,
};

struct FlightCommand {
  SimulationTick tick{};
  FlightCommandKind kind{};

  friend auto operator==(const FlightCommand&, const FlightCommand&)
      -> bool = default;
};

struct CraftPose {
  float x{180.0F};
  float y{240.0F};
  float altitude{135.0F};
  float yaw{0.35F};
};

struct CraftVelocity {
  float x{};
  float y{};
  float vertical{};
};

struct FlightControls {
  bool forward{};
  bool backward{};
  bool turn_left{};
  bool turn_right{};
  bool strafe_left{};
  bool strafe_right{};
  bool rise{};
  bool fall{};

  friend auto operator==(const FlightControls&, const FlightControls&)
      -> bool = default;
};

struct FlightState {
  SimulationTick tick{};
  CraftPose pose;
  CraftVelocity velocity;
  float clearance{48.0F};
  FlightMode mode{FlightMode::autopilot};
  FlightControls controls;
};

enum class FlightError : std::uint8_t {
  invalid_state,
  invalid_step,
  invalid_command,
  wrong_command_tick,
  tick_overflow,
};

[[nodiscard]] auto initial_flight_state(const Terrain& terrain) noexcept
    -> std::expected<FlightState, FlightError>;

// Applies all commands in their recorded order, advances exactly one fixed
// step, and commits the result atomically. Every command must target the
// state's current tick, and step must equal kSimulationStep.
[[nodiscard]] auto advance_flight(const Terrain& terrain, FlightState& state,
                                  std::span<const FlightCommand> commands,
                                  SimulationSeconds step) noexcept
    -> std::expected<void, FlightError>;

// Camera state is presentation-only and is derived from authoritative flight
// state. Callers remain responsible for presentation choices such as pitch.
[[nodiscard]] auto derive_camera(const FlightState& state) noexcept
    -> std::expected<Camera, FlightError>;

[[nodiscard]] auto flight_state_checksum(const FlightState& state) noexcept
    -> std::uint64_t;

} // namespace apsis_drift
