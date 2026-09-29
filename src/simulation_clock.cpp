#include "apsis_drift/simulation.hpp"

#include <algorithm>
#include <cmath>

namespace apsis_drift {

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
      static_cast<int>(std::floor((m_accumulator.count() + boundary_epsilon) /
                                  kSimulationStep.count())));
  m_accumulator -= kSimulationStep * result.steps;
  if (m_accumulator.count() < 0.0 &&
      m_accumulator.count() >= -boundary_epsilon) {
    m_accumulator = SimulationSeconds::zero();
  }
  result.interpolation_alpha =
      std::clamp(m_accumulator.count() / kSimulationStep.count(), 0.0, 1.0);
  return result;
}

} // namespace apsis_drift
