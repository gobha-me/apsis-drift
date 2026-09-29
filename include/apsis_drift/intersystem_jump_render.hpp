#pragma once

#include <expected>
#include <span>

#include "apsis_drift/intersystem_jump.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift {

[[nodiscard]] auto render_intersystem_jump(
    const IntersystemJumpSnapshot& snapshot, int width, int height,
    std::span<termforge::Pixel> destination)
    -> std::expected<void, IntersystemJumpError>;

} // namespace apsis_drift
