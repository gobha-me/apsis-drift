#pragma once

#include <optional>

#include "termforge/core/requirements.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift::detail {

enum class DriverChoice { automatic, kitty, ansi, fallback };
enum class KeyboardChoice { enhanced, press_only };

[[nodiscard]] inline auto flight_deck_requirements()
    -> termforge::AppRequirements {
  return {
      .truecolor = true,
      .key_repeat = true,
      .key_release = true,
  };
}

[[nodiscard]] inline auto forced_capabilities(DriverChoice driver,
                                              KeyboardChoice keyboard)
    -> std::optional<termforge::Capabilities> {
  if (driver == DriverChoice::automatic) return std::nullopt;

  termforge::Capabilities caps;
  caps.kitty_keyboard = keyboard == KeyboardChoice::enhanced;
  if (driver == DriverChoice::kitty) {
    caps.kitty_graphics = true;
    caps.truecolor = true;
    caps.color_levels = 24;
  } else if (driver == DriverChoice::ansi) {
    caps.truecolor = true;
    caps.color_levels = 24;
  }
  return caps;
}

}  // namespace apsis_drift::detail
