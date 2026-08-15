#pragma once

#include <vector>

#include "apsis_drift/simulation.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift::detail {

class FlightInputMapper {
 public:
  auto enqueue(const termforge::KeyEvent& key,
               SimulationTick current_tick) -> void;

  [[nodiscard]] auto take_commands(SimulationTick tick)
      -> std::vector<FlightCommand>;

 private:
  auto insert(FlightCommand command) -> void;

  std::vector<FlightCommand> m_pending;
};

}  // namespace apsis_drift::detail
