#include "apsis_drift/cockpit.hpp"

#include <algorithm>
#include <cstdint>

namespace apsis_drift {
namespace {

inline constexpr int kWideCockpitCols{120};
inline constexpr int kWideCockpitRows{32};
inline constexpr int kCompactRailCols{12};
inline constexpr int kWideRailCols{18};
inline constexpr int kHeaderRows{1};
inline constexpr int kMessageRows{3};
inline constexpr int kStatusRows{1};
inline constexpr int kRailGutterCols{1};
inline constexpr int kMaximumTerminalAxis{65535};

[[nodiscard]] auto aspect_fit(termforge::Rect available,
                              termforge::Extent cell_pixels,
                              ViewportSize viewport) noexcept
    -> termforge::Rect {
  if (available.empty() || cell_pixels.empty() || viewport.width <= 0 ||
      viewport.height <= 0) {
    return {};
  }

  using i64 = std::int64_t;
  int width = available.w;
  int height = static_cast<int>(
      (i64{width} * viewport.height * cell_pixels.w) /
      (i64{viewport.width} * cell_pixels.h));
  height = std::max(1, height);
  if (height > available.h) {
    height = available.h;
    width = static_cast<int>(
        (i64{height} * viewport.width * cell_pixels.h) /
        (i64{viewport.height} * cell_pixels.w));
    width = std::clamp(width, 1, available.w);
  }

  return {available.x + (available.w - width) / 2,
          available.y + (available.h - height) / 2, width, height};
}

}  // namespace

auto compute_cockpit_layout(int cols, int rows,
                            termforge::Extent cell_pixels,
                            ViewportSize viewport) noexcept
    -> CockpitLayout {
  CockpitLayout layout;
  if (cols <= 0 || rows <= 0) return layout;
  layout.screen = {0, 0, cols, rows};

  if (cols > kMaximumTerminalAxis || rows > kMaximumTerminalAxis ||
      cols < kMinimumCockpitCols || rows < kMinimumCockpitRows ||
      cell_pixels.empty() || cell_pixels.w > kMaximumTerminalAxis ||
      cell_pixels.h > kMaximumTerminalAxis || !validate_viewport(viewport)) {
    return layout;
  }

  const bool wide = cols >= kWideCockpitCols && rows >= kWideCockpitRows;
  layout.mode = wide ? CockpitLayoutMode::wide
                     : CockpitLayoutMode::compact;
  const int rail_cols = wide ? kWideRailCols : kCompactRailCols;

  layout.header = {0, 0, cols, kHeaderRows};
  layout.messages = {0, rows - kStatusRows - kMessageRows, cols,
                     kMessageRows};
  layout.status = {0, rows - kStatusRows, cols, kStatusRows};

  const int deck_y = kHeaderRows;
  const int deck_rows = rows - kHeaderRows - kMessageRows - kStatusRows;
  layout.left_instruments = {0, deck_y, rail_cols, deck_rows};
  layout.right_instruments = {cols - rail_cols, deck_y, rail_cols,
                              deck_rows};

  const termforge::Rect center{
      rail_cols + kRailGutterCols,
      deck_y,
      cols - 2 * (rail_cols + kRailGutterCols),
      deck_rows,
  };
  const termforge::Rect viewport_available{
      center.x + 1,
      center.y + 1,
      std::max(0, center.w - 2),
      std::max(0, center.h - 2),
  };
  layout.viewport = aspect_fit(viewport_available, cell_pixels, viewport);
  if (layout.viewport.empty()) {
    layout.mode = CockpitLayoutMode::too_small;
    return layout;
  }
  layout.viewport_frame = {
      layout.viewport.x - 1,
      layout.viewport.y - 1,
      layout.viewport.w + 2,
      layout.viewport.h + 2,
  };
  return layout;
}

}  // namespace apsis_drift
