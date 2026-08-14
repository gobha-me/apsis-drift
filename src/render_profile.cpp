#include "apsis_drift/render_profile.hpp"

#include <charconv>
#include <cstdint>
#include <system_error>

namespace apsis_drift {
namespace {

[[nodiscard]] auto parse_component(std::string_view text) noexcept
    -> std::expected<std::int64_t, ViewportError> {
  if (text.empty()) return std::unexpected{ViewportError::malformed};

  std::int64_t value{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error == std::errc::result_out_of_range) {
    return std::unexpected{ViewportError::numeric_overflow};
  }
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::unexpected{ViewportError::malformed};
  }
  return value;
}

}  // namespace

auto profile_viewport(RenderProfile profile) noexcept -> ViewportSize {
  switch (profile) {
    case RenderProfile::remote: return {320, 240};
    case RenderProfile::balanced: return {512, 320};
    case RenderProfile::local: return {640, 480};
    case RenderProfile::cinematic: return {1024, 768};
  }
  return {kDefaultViewportWidth, kDefaultViewportHeight};
}

auto profile_name(RenderProfile profile) noexcept -> std::string_view {
  switch (profile) {
    case RenderProfile::remote: return "remote";
    case RenderProfile::balanced: return "balanced";
    case RenderProfile::local: return "local";
    case RenderProfile::cinematic: return "cinematic";
  }
  return "local";
}

auto profile_name(const RenderConfiguration& configuration) noexcept
    -> std::string_view {
  return configuration.named_profile
             ? profile_name(*configuration.named_profile)
             : std::string_view{"custom"};
}

auto parse_render_profile(std::string_view text) noexcept
    -> std::optional<RenderProfile> {
  if (text == "remote") return RenderProfile::remote;
  if (text == "balanced") return RenderProfile::balanced;
  if (text == "local") return RenderProfile::local;
  if (text == "cinematic") return RenderProfile::cinematic;
  return std::nullopt;
}

auto validate_viewport(ViewportSize viewport) noexcept
    -> std::expected<ViewportSize, ViewportError> {
  if (viewport.width <= 0 || viewport.height <= 0) {
    return std::unexpected{ViewportError::non_positive};
  }
  if (viewport.width > kMaxViewportAxis ||
      viewport.height > kMaxViewportAxis) {
    return std::unexpected{ViewportError::dimension_too_large};
  }
  const auto pixels = static_cast<std::size_t>(viewport.width) *
                      static_cast<std::size_t>(viewport.height);
  if (pixels > kMaxViewportPixels) {
    return std::unexpected{ViewportError::pixel_budget_exceeded};
  }
  return viewport;
}

auto parse_viewport(std::string_view text) noexcept
    -> std::expected<ViewportSize, ViewportError> {
  const auto separator = text.find('x');
  if (separator == std::string_view::npos || separator == 0 ||
      separator + 1 >= text.size() ||
      text.find('x', separator + 1) != std::string_view::npos) {
    return std::unexpected{ViewportError::malformed};
  }

  const auto width = parse_component(text.substr(0, separator));
  if (!width) return std::unexpected{width.error()};
  const auto height = parse_component(text.substr(separator + 1));
  if (!height) return std::unexpected{height.error()};
  if (*width <= 0 || *height <= 0) {
    return std::unexpected{ViewportError::non_positive};
  }
  if (*width > kMaxViewportAxis || *height > kMaxViewportAxis) {
    return std::unexpected{ViewportError::dimension_too_large};
  }
  return validate_viewport(
      {static_cast<int>(*width), static_cast<int>(*height)});
}

auto viewport_error_message(ViewportError error) noexcept -> std::string_view {
  switch (error) {
    case ViewportError::malformed:
      return "viewport must use WIDTHxHEIGHT with decimal integers";
    case ViewportError::non_positive:
      return "viewport dimensions must be positive";
    case ViewportError::numeric_overflow:
      return "viewport dimension is outside the supported integer range";
    case ViewportError::dimension_too_large:
      return "viewport width and height must not exceed 4096";
    case ViewportError::pixel_budget_exceeded:
      return "viewport must not exceed 4194304 pixels";
  }
  return "invalid viewport";
}

auto resolve_render_configuration(RenderProfile profile,
                                  std::optional<ViewportSize> override) noexcept
    -> RenderConfiguration {
  if (override) return {*override, std::nullopt};
  return {profile_viewport(profile), profile};
}

auto default_render_configuration() noexcept -> RenderConfiguration {
  return resolve_render_configuration(RenderProfile::local);
}

}  // namespace apsis_drift
