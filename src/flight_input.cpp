#include "flight_input.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
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

inline constexpr std::size_t kFirstLeftBank{
    static_cast<std::size_t>(Control::forward)};
inline constexpr std::size_t kLastLeftBank{
    static_cast<std::size_t>(Control::turn_right)};
inline constexpr std::size_t kFirstRightBank{
    static_cast<std::size_t>(Control::strafe_left)};
inline constexpr std::size_t kLastRightBank{
    static_cast<std::size_t>(Control::fall)};

[[nodiscard]] constexpr auto control_index(Control control) noexcept
    -> std::size_t {
  return static_cast<std::size_t>(control);
}

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

[[nodiscard]] auto axis_zone(int position, int start, int length) noexcept
    -> int {
  if (length <= 0) return 0;
  using i64 = std::int64_t;
  const i64 relative = i64{position} - start;
  const i64 scaled = relative * 3;
  if (scaled < length) return -1;
  if (scaled >= i64{length} * 2) return 1;
  return 0;
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

auto FlightInputMapper::insert_before_tick(FlightCommand command) -> void {
  const auto position = std::lower_bound(
      m_pending.begin(), m_pending.end(), command.tick,
      [](const FlightCommand& queued, SimulationTick tick) {
        return queued.tick < tick;
      });
  m_pending.insert(position, command);
}

auto FlightInputMapper::enqueue(const KeyEvent& key,
                                SimulationTick current_tick,
                                bool enable_time_scale) -> void {
  if (enable_time_scale && key.key == Key::Char &&
      key.action == KeyAction::Press &&
      (key.ch == U'[' || key.ch == U']')) {
    insert({current_tick, key.ch == U'['
                              ? FlightCommandKind::decrease_time_scale
                              : FlightCommandKind::increase_time_scale});
    return;
  }
  if (key.key == Key::Char && key.ch == U' ') {
    if (key.action == KeyAction::Press) {
      insert({current_tick, FlightCommandKind::toggle_autopilot});
      // The simulation toggle clears authoritative controls. Mirror that
      // boundary here so held sources must repeat or move before taking over.
      m_keyboard_controls = {};
      clear_mouse_bank(0, kControlCount - 1, current_tick);
    }
    return;
  }

  const auto control = control_for(key);
  if (!control) return;

  const auto index = control_index(*control);
  const bool pressed = key.action != KeyAction::Release;
  m_keyboard_controls[index] = pressed;
  if (pressed || !m_mouse_controls[index]) {
    insert({current_tick, command_for(*control, pressed)});
  }
}

auto FlightInputMapper::mouse_intent(SimulationTick current_tick)
    -> MouseTickIntent& {
  const auto position = std::lower_bound(
      m_mouse_intents.begin(), m_mouse_intents.end(), current_tick,
      [](const MouseTickIntent& intent, SimulationTick tick) {
        return intent.tick < tick;
      });
  if (position != m_mouse_intents.end() && position->tick == current_tick) {
    return *position;
  }
  return *m_mouse_intents.insert(
      position, MouseTickIntent{.tick = current_tick,
                                .initial = m_mouse_controls,
                                .final = m_mouse_controls});
}

auto FlightInputMapper::set_mouse_control(std::size_t control, bool pressed,
                                          SimulationTick current_tick)
    -> void {
  if (control >= kControlCount || m_mouse_controls[control] == pressed) return;
  auto& intent = mouse_intent(current_tick);
  m_mouse_controls[control] = pressed;
  intent.final[control] = pressed;
  intent.pressed[control] = intent.pressed[control] || pressed;
  intent.released[control] = intent.released[control] || !pressed;
}

auto FlightInputMapper::clear_mouse_bank(std::size_t first, std::size_t last,
                                         SimulationTick current_tick) -> void {
  for (std::size_t control = first;
       control <= last && control < kControlCount; ++control) {
    set_mouse_control(control, false, current_tick);
  }
}

auto FlightInputMapper::neutralize_mouse(SimulationTick current_tick) -> void {
  clear_mouse_bank(0, kControlCount - 1, current_tick);
  m_mouse_buttons = {};
}

auto FlightInputMapper::suspend(const FlightControls& applied_controls,
                                SimulationTick current_tick) -> void {
  m_pending.clear();
  m_mouse_intents.clear();
  m_keyboard_controls = {};
  m_mouse_controls = {};
  m_mouse_buttons = {};

  const std::array active{
      applied_controls.forward,      applied_controls.backward,
      applied_controls.turn_left,    applied_controls.turn_right,
      applied_controls.strafe_left,  applied_controls.strafe_right,
      applied_controls.rise,         applied_controls.fall,
  };
  for (std::size_t index = 0; index < active.size(); ++index) {
    if (!active[index]) continue;
    insert({current_tick,
            command_for(static_cast<Control>(index), false)});
  }
}

auto FlightInputMapper::enqueue(const termforge::MouseEvent& mouse,
                                termforge::Rect active_region,
                                SimulationTick current_tick) -> void {
  if (mouse.scroll_up || mouse.scroll_down || mouse.button < 0 ||
      mouse.button > 2) {
    return;
  }

  const auto button = static_cast<std::size_t>(mouse.button);
  if (!mouse.pressed) {
    m_mouse_buttons[button] = false;
    if (mouse.button == 0) {
      clear_mouse_bank(kFirstLeftBank, kLastLeftBank, current_tick);
    } else if (mouse.button == 2) {
      clear_mouse_bank(kFirstRightBank, kLastRightBank, current_tick);
    }
    return;
  }

  const bool was_down = m_mouse_buttons[button];
  m_mouse_buttons[button] = true;
  if (active_region.empty() ||
      !active_region.contains(mouse.x, mouse.y)) {
    clear_mouse_bank(0, kControlCount - 1, current_tick);
    return;
  }

  if (mouse.button == 1) {
    if (!was_down) {
      auto& intent = mouse_intent(current_tick);
      intent.toggle_autopilot = !intent.toggle_autopilot;
      m_keyboard_controls = {};
      clear_mouse_bank(0, kControlCount - 1, current_tick);
    }
    return;
  }

  const int horizontal =
      axis_zone(mouse.x, active_region.x, active_region.w);
  const int vertical = axis_zone(mouse.y, active_region.y, active_region.h);
  if (mouse.button == 0) {
    set_mouse_control(control_index(Control::forward), vertical < 0,
                      current_tick);
    set_mouse_control(control_index(Control::backward), vertical > 0,
                      current_tick);
    set_mouse_control(control_index(Control::turn_left), horizontal < 0,
                      current_tick);
    set_mouse_control(control_index(Control::turn_right), horizontal > 0,
                      current_tick);
  } else {
    set_mouse_control(control_index(Control::strafe_left), horizontal < 0,
                      current_tick);
    set_mouse_control(control_index(Control::strafe_right), horizontal > 0,
                      current_tick);
    set_mouse_control(control_index(Control::rise), vertical < 0,
                      current_tick);
    set_mouse_control(control_index(Control::fall), vertical > 0,
                      current_tick);
  }
}

auto FlightInputMapper::flush_mouse_intent(SimulationTick tick) -> void {
  const auto position = std::lower_bound(
      m_mouse_intents.begin(), m_mouse_intents.end(), tick,
      [](const MouseTickIntent& intent, SimulationTick value) {
        return intent.tick < value;
      });
  if (position == m_mouse_intents.end() || position->tick != tick) return;

  const MouseTickIntent intent = *position;
  m_mouse_intents.erase(position);
  if (intent.toggle_autopilot) {
    // A pointer toggle precedes every manual command at the same tick, so a
    // simultaneous keyboard or held pointer direction wins back manual flight.
    insert_before_tick({tick, FlightCommandKind::toggle_autopilot});
  }

  for (std::size_t index = 0; index < kControlCount; ++index) {
    if (m_keyboard_controls[index]) continue;
    const auto control = static_cast<Control>(index);
    if (intent.initial[index] == intent.final[index]) {
      if (!intent.pressed[index] || !intent.released[index]) continue;
      insert({tick, command_for(control, !intent.final[index])});
      insert({tick, command_for(control, intent.final[index])});
      continue;
    }
    insert({tick, command_for(control, intent.final[index])});
  }
}

auto FlightInputMapper::take_commands(SimulationTick tick)
    -> std::vector<FlightCommand> {
  flush_mouse_intent(tick);
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
