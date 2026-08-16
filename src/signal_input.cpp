#include "signal_input.hpp"

namespace apsis_drift::detail {

auto signal_selection_command(const termforge::KeyEvent& key) noexcept
    -> std::optional<SignalSelectionCommand> {
  if (key.key != termforge::Key::Tab ||
      key.action != termforge::KeyAction::Press) {
    return std::nullopt;
  }
  return key.shift ? SignalSelectionCommand::previous
                   : SignalSelectionCommand::next;
}

}  // namespace apsis_drift::detail
