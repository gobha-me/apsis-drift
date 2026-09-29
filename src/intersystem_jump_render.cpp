#include "apsis_drift/intersystem_jump_render.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace apsis_drift {

auto render_intersystem_jump(const IntersystemJumpSnapshot& snapshot, int width,
                             int height,
                             std::span<termforge::Pixel> destination)
    -> std::expected<void, IntersystemJumpError> {
  if (width <= 0 || height <= 0 ||
      static_cast<std::size_t>(width) >
          std::numeric_limits<std::size_t>::max() /
              static_cast<std::size_t>(height) ||
      destination.size() !=
          static_cast<std::size_t>(width) * static_cast<std::size_t>(height) ||
      !std::isfinite(snapshot.progress) || snapshot.progress < 0.0 ||
      snapshot.progress > 1.0 || snapshot.duration_ticks == 0 ||
      snapshot.elapsed_ticks > snapshot.duration_ticks ||
      (snapshot.alignment &&
       (snapshot.alignment->heading_error_millidegrees < -180'000 ||
        snapshot.alignment->heading_error_millidegrees > 180'000 ||
        snapshot.alignment->velocity_error_basis_points < -10'000 ||
        snapshot.alignment->velocity_error_basis_points > 10'000))) {
    return std::unexpected{IntersystemJumpError::invalid_framebuffer};
  }
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index =
          static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(x);
      const std::uint64_t noise =
          (static_cast<std::uint64_t>(x + 1) * 0x9e3779b185ebca87ULL) ^
          (static_cast<std::uint64_t>(y + 1) * 0xc2b2ae3d27d4eb4fULL) ^
          (snapshot.elapsed_ticks * 0x165667b19e3779f9ULL);
      const double dx =
          static_cast<double>(x) - static_cast<double>(width - 1) * 0.5;
      const double dy =
          static_cast<double>(y) - static_cast<double>(height - 1) * 0.5;
      const double radius =
          std::hypot(dx, dy) / static_cast<double>(std::max(width, height));
      const bool star = (noise & 0x7ffU) < (snapshot.committed ? 18U : 5U);
      const double flare =
          snapshot.committed ? std::max(0.0, 1.0 - radius * 5.0) : 0.0;
      destination[index] = {
          static_cast<std::uint8_t>(std::clamp(
              8.0 + flare * 150.0 + (star ? 120.0 : 0.0), 0.0, 255.0)),
          static_cast<std::uint8_t>(std::clamp(
              15.0 + flare * 190.0 + (star ? 150.0 : 0.0), 0.0, 255.0)),
          static_cast<std::uint8_t>(std::clamp(28.0 + snapshot.progress * 55.0 +
                                                   flare * 210.0 +
                                                   (star ? 210.0 : 0.0),
                                               0.0, 255.0)),
          255};
    }
  }
  if (snapshot.alignment) {
    const double horizontal = std::clamp(
        static_cast<double>(snapshot.alignment->heading_error_millidegrees) /
            static_cast<double>(kOffsetHeadingErrorMillidegrees),
        -1.0, 1.0);
    const double vertical = std::clamp(
        static_cast<double>(snapshot.alignment->velocity_error_basis_points) /
            static_cast<double>(kOffsetVelocityErrorBasisPoints),
        -1.0, 1.0);
    const int center_x = width / 2;
    const int center_y = height / 2;
    const int marker_x = std::clamp(
        center_x + static_cast<int>(std::round(horizontal * width * 0.35)), 0,
        width - 1);
    const int marker_y = std::clamp(
        center_y + static_cast<int>(std::round(vertical * height * 0.35)), 0,
        height - 1);
    const auto paint = [&](int x, int y, termforge::Pixel pixel) {
      if (x < 0 || y < 0 || x >= width || y >= height) return;
      destination[static_cast<std::size_t>(y) *
                      static_cast<std::size_t>(width) +
                  static_cast<std::size_t>(x)] = pixel;
    };
    const termforge::Pixel guide{90, 210, 220, 255};
    const termforge::Pixel marker{245, 180, 80, 255};
    for (int delta = -5; delta <= 5; ++delta) {
      paint(center_x + delta, center_y, guide);
      paint(center_x, center_y + delta, guide);
    }
    for (int delta = -3; delta <= 3; ++delta) {
      paint(marker_x + delta, marker_y, marker);
      paint(marker_x, marker_y + delta, marker);
    }
  }
  return {};
}

} // namespace apsis_drift
