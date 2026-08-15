#pragma once

#include "termforge/core/types.hpp"
#include "apsis_drift/render_profile.hpp"

namespace apsis_drift {

inline constexpr int kMinimumCockpitCols{80};
inline constexpr int kMinimumCockpitRows{24};

enum class CockpitLayoutMode { too_small, compact, wide };

struct CockpitLayout {
  CockpitLayoutMode mode{CockpitLayoutMode::too_small};
  termforge::Rect screen{};
  termforge::Rect header{};
  termforge::Rect left_instruments{};
  termforge::Rect viewport_frame{};
  termforge::Rect viewport{};
  termforge::Rect right_instruments{};
  termforge::Rect messages{};
  termforge::Rect status{};

  [[nodiscard]] constexpr auto supported() const noexcept -> bool {
    return mode != CockpitLayoutMode::too_small;
  }

  constexpr auto operator==(const CockpitLayout&) const noexcept
      -> bool = default;
};

// Compute cockpit regions in terminal cells. The driver-provided `cell_pixels`
// keeps the logical pixel viewport's aspect ratio correct on Kitty, ANSI
// half-block, and cell fallback paths without moving terminal policy into the
// game.
[[nodiscard]] auto compute_cockpit_layout(
    int cols, int rows, termforge::Extent cell_pixels,
    ViewportSize viewport) noexcept -> CockpitLayout;

}  // namespace apsis_drift
