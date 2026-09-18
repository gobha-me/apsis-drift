#pragma once

#include <expected>
#include <span>

#include "apsis_drift/render_profile.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift {

enum class TitleRenderError {
  invalid_dimensions,
  buffer_size_mismatch,
  too_small,
};

struct TitleRenderResult {
  int scale{};
  termforge::Rect logo_bounds{};
};

// Renders the original, code-authored APSIS DRIFT title alphabet. The glyph
// coverage is exactly A, D, F, I, P, R, S, T, and space. Scaling is integral,
// and every validation failure leaves the destination untouched.
[[nodiscard]] auto render_title(
    ViewportSize size, std::span<termforge::Pixel> destination) noexcept
    -> std::expected<TitleRenderResult, TitleRenderError>;

} // namespace apsis_drift
