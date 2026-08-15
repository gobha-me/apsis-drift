#include "flight_input.hpp"

#include <algorithm>
#include <optional>

namespace apsis_drift::detail {
namespace {

using termforge::Key;
using termforge::KeyAction;
using termforge::KeyEvent;

enum class Control : std::uint8_t {
  forward,
  backward,
  turn_left,
  turn_right,
  strafe_left,
  strafe_right,
  rise,
  fall,
};

[[nodiscard]] auto control_for(const KeyEvent& key) noexcept
    -> std::optional<Control> {
  if (key.key == Key::Up) return Control::forward;
  if (key.key == Key::Down) return Control::backward;
  if (key.key == Key::Left) return Control::turn_left;
  if (key.key == Key::Right) return Control::turn_right;
  if (key.key != Key::Char) return std::nullopt;

  char32_t ch = key.ch;
  if (ch >= U'A' && ch <= U'Z') ch += U'a' - U'A';
  if (ch == U'w') return Control::forward;
  if (ch == U's') return Control::backward;
  if (ch == U'a') return Control::turn_left;
  if (ch == U'd') return Control::turn_right;
  if (ch == U'q') return Control::strafe_left;
  if (ch == U'e') return Control::strafe_right;
  if (ch == U'r') return Control::rise;
  if (ch == U'f') return Control::fall;
  return std::nullopt;
}

[[nodiscard]] auto command_for(Control control, bool pressed) noexcept
    -> FlightCommandKind {
  switch (control) {
    case Control::forward:
      return pressed ? FlightCommandKind::press_forward
                     : FlightCommandKind::release_forward;
    case Control::backward:
      return pressed ? FlightCommandKind::press_backward
                     : FlightCommandKind::release_backward;
    case Control::turn_left:
      return pressed ? FlightCommandKind::press_turn_left
                     : FlightCommandKind::release_turn_left;
    case Control::turn_right:
      return pressed ? FlightCommandKind::press_turn_right
                     : FlightCommandKind::release_turn_right;
    case Control::strafe_left:
      return pressed ? FlightCommandKind::press_strafe_left
                     : FlightCommandKind::release_strafe_left;
    case Control::strafe_right:
      return pressed ? FlightCommandKind::press_strafe_right
                     : FlightCommandKind::release_strafe_right;
    case Control::rise:
      return pressed ? FlightCommandKind::press_rise
                     : FlightCommandKind::release_rise;
    case Control::fall:
      return pressed ? FlightCommandKind::press_fall
                     : FlightCommandKind::release_fall;
  }
  return FlightCommandKind::press_forward;
}

}  // namespace

auto FlightInputMapper::insert(FlightCommand command) -> void {
  const auto position = std::upper_bound(
      m_pending.begin(), m_pending.end(), command.tick,
      [](SimulationTick tick, const FlightCommand& queued) {
        return tick < queued.tick;
      });
  m_pending.insert(position, command);
}

auto FlightInputMapper::enqueue(const KeyEvent& key,
                                SimulationTick current_tick) -> void {
  if (key.key == Key::Char && key.ch == U' ') {
    if (key.action == KeyAction::Press) {
      insert({current_tick, FlightCommandKind::toggle_autopilot});
    }
    return;
  }

  const auto control = control_for(key);
  if (!control) return;

  insert({current_tick,
          command_for(*control, key.action != KeyAction::Release)});
}

auto FlightInputMapper::take_commands(SimulationTick tick)
    -> std::vector<FlightCommand> {
  const auto first = std::lower_bound(
      m_pending.begin(), m_pending.end(), tick,
      [](const FlightCommand& command, SimulationTick value) {
        return command.tick < value;
      });
  const auto last = std::upper_bound(
      first, m_pending.end(), tick,
      [](SimulationTick value, const FlightCommand& command) {
        return value < command.tick;
      });
  std::vector<FlightCommand> result(first, last);
  m_pending.erase(first, last);
  return result;
}

}  // namespace apsis_drift::detail
