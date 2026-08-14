#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <string_view>

namespace apsis_drift {

inline constexpr int kDefaultViewportWidth{640};
inline constexpr int kDefaultViewportHeight{480};
inline constexpr int kMaxViewportAxis{4096};
inline constexpr std::size_t kMaxViewportPixels{4U * 1024U * 1024U};

struct ViewportSize {
  int width{};
  int height{};

  auto operator==(const ViewportSize&) const -> bool = default;
};

enum class RenderProfile { remote, balanced, local, cinematic };

enum class ViewportError {
  malformed,
  non_positive,
  numeric_overflow,
  dimension_too_large,
  pixel_budget_exceeded,
};

struct RenderConfiguration {
  ViewportSize viewport{};
  std::optional<RenderProfile> named_profile{};

  auto operator==(const RenderConfiguration&) const -> bool = default;
};

[[nodiscard]] auto profile_viewport(RenderProfile profile) noexcept
    -> ViewportSize;
[[nodiscard]] auto profile_name(RenderProfile profile) noexcept
    -> std::string_view;
[[nodiscard]] auto profile_name(const RenderConfiguration& configuration)
    noexcept -> std::string_view;
[[nodiscard]] auto parse_render_profile(std::string_view text) noexcept
    -> std::optional<RenderProfile>;

[[nodiscard]] auto validate_viewport(ViewportSize viewport) noexcept
    -> std::expected<ViewportSize, ViewportError>;
[[nodiscard]] auto parse_viewport(std::string_view text) noexcept
    -> std::expected<ViewportSize, ViewportError>;
[[nodiscard]] auto viewport_error_message(ViewportError error) noexcept
    -> std::string_view;

[[nodiscard]] auto resolve_render_configuration(
    RenderProfile profile, std::optional<ViewportSize> override = std::nullopt)
    noexcept -> RenderConfiguration;
[[nodiscard]] auto default_render_configuration() noexcept
    -> RenderConfiguration;

}  // namespace apsis_drift
