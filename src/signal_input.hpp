#pragma once

#include <optional>

#include "apsis_drift/signal_scanner.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift::detail {

// Target selection consumes only semantic Tab press events. Repeat and release
// events cannot make selection cadence dependent on terminal key-repeat timing.
[[nodiscard]] auto signal_selection_command(
    const termforge::KeyEvent& key) noexcept
    -> std::optional<SignalSelectionCommand>;

} // namespace apsis_drift::detail
