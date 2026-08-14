#pragma once

#include <expected>
#include <vector>

#include "apsis_drift/simulation.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift::detail {

inline constexpr SimulationTick kFallbackControlPulseTicks{10};

enum class FlightInputError : std::uint8_t { tick_overflow };

class FlightInputMapper {
 public:
  auto set_enhanced_keyboard(bool enabled) noexcept -> void {
    m_enhanced_keyboard = enabled;
  }

  [[nodiscard]] auto enqueue(const termforge::KeyEvent& key,
                             SimulationTick current_tick)
      -> std::expected<void, FlightInputError>;

  [[nodiscard]] auto take_commands(SimulationTick tick)
      -> std::vector<FlightCommand>;

 private:
  auto insert(FlightCommand command) -> void;

  bool m_enhanced_keyboard{true};
  std::vector<FlightCommand> m_pending;
};

}  // namespace apsis_drift::detail
