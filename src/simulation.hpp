#pragma once

#include <chrono>
#include <cstdint>
#include <expected>

#include "apsis_drift/landscape.hpp"

namespace apsis_drift::detail {

using SimulationSeconds = std::chrono::duration<double>;

inline constexpr int kSimulationHz{120};
inline constexpr int kMaxCatchUpSteps{15};
inline constexpr SimulationSeconds kSimulationStep{1.0 / kSimulationHz};
inline constexpr SimulationSeconds kMaxCatchUp{
    kSimulationStep * kMaxCatchUpSteps};

enum class SimulationTimeError {
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

struct FlightInput {
  float forward{};
  float turn{};
  float strafe{};
  float vertical{};
  bool autopilot{true};
};

// This is the current flyover's private runtime state, not the command and
// craft-state contract tracked by issue #5.
struct FlightRuntime {
  Camera camera;
  double elapsed_seconds{};
};

// Advances exactly one simulation step. Invalid input or state returns false
// without changing the runtime.
[[nodiscard]] auto advance_flight(const Terrain& terrain, FlightRuntime& state,
                                  FlightInput input,
                                  SimulationSeconds step) noexcept -> bool;

[[nodiscard]] auto flight_state_checksum(const FlightRuntime& state) noexcept
    -> std::uint64_t;

}  // namespace apsis_drift::detail
