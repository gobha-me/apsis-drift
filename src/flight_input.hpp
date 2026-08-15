#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "apsis_drift/simulation.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift::detail {

class FlightInputMapper {
 public:
  auto enqueue(const termforge::KeyEvent& key,
               SimulationTick current_tick) -> void;
  auto enqueue(const termforge::MouseEvent& mouse,
               termforge::Rect active_region,
               SimulationTick current_tick) -> void;

  // Release only pointer-owned controls. Keyboard holds remain authoritative.
  auto neutralize_mouse(SimulationTick current_tick) -> void;

  [[nodiscard]] auto take_commands(SimulationTick tick)
      -> std::vector<FlightCommand>;

 private:
  static constexpr std::size_t kControlCount{8};

  struct MouseTickIntent {
    SimulationTick tick{};
    std::array<bool, kControlCount> initial{};
    std::array<bool, kControlCount> final{};
    std::array<bool, kControlCount> pressed{};
    std::array<bool, kControlCount> released{};
    bool toggle_autopilot{};
  };

  auto insert(FlightCommand command) -> void;
  auto insert_before_tick(FlightCommand command) -> void;
  auto set_mouse_control(std::size_t control, bool pressed,
                         SimulationTick current_tick) -> void;
  auto clear_mouse_bank(std::size_t first, std::size_t last,
                        SimulationTick current_tick) -> void;
  [[nodiscard]] auto mouse_intent(SimulationTick current_tick)
      -> MouseTickIntent&;
  auto flush_mouse_intent(SimulationTick tick) -> void;

  std::vector<FlightCommand> m_pending;
  std::vector<MouseTickIntent> m_mouse_intents;
  std::array<bool, kControlCount> m_keyboard_controls{};
  std::array<bool, kControlCount> m_mouse_controls{};
  std::array<bool, 3> m_mouse_buttons{};
};

}  // namespace apsis_drift::detail
