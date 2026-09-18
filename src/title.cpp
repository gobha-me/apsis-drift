#include "apsis_drift/title.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace apsis_drift {
namespace {

using termforge::Pixel;

struct Glyph {
  char letter;
  std::array<std::uint8_t, 7> rows;
};

inline constexpr std::array kGlyphs{
    Glyph{'A', {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},
    Glyph{'D', {0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110}},
    Glyph{'F', {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}},
    Glyph{'I', {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b11111}},
    Glyph{'P', {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}},
    Glyph{'R', {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}},
    Glyph{'S', {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}},
    Glyph{'T', {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}},
    Glyph{' ', {0, 0, 0, 0, 0, 0, 0}},
};

inline constexpr int kGlyphWidth{5};
inline constexpr int kGlyphHeight{7};
inline constexpr int kGlyphAdvance{6};
inline constexpr int kLineGap{2};
inline constexpr int kLogoWidth{5 * kGlyphWidth + 4};
inline constexpr int kLogoHeight{2 * kGlyphHeight + kLineGap};
inline constexpr std::string_view kTop{"APSIS"};
inline constexpr std::string_view kBottom{"DRIFT"};

[[nodiscard]] constexpr auto glyph_for(char letter) noexcept -> const Glyph* {
  for (const auto& glyph : kGlyphs) {
    if (glyph.letter == letter) return &glyph;
  }
  return nullptr;
}

auto fill_rect(std::span<Pixel> pixels, int width, int height, int x, int y,
               int rect_width, int rect_height, Pixel color) noexcept -> void {
  const int left = std::clamp(x, 0, width);
  const int top = std::clamp(y, 0, height);
  const int right = std::clamp(x + rect_width, 0, width);
  const int bottom = std::clamp(y + rect_height, 0, height);
  for (int row = top; row < bottom; ++row) {
    for (int column = left; column < right; ++column) {
      pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(column)] = color;
    }
  }
}

auto draw_word(std::span<Pixel> pixels, int width, int height,
               std::string_view word, int x, int y, int scale,
               Pixel color) noexcept -> void {
  for (std::size_t index = 0; index < word.size(); ++index) {
    const Glyph* glyph = glyph_for(word[index]);
    if (glyph == nullptr) continue;
    for (int row = 0; row < kGlyphHeight; ++row) {
      for (int column = 0; column < kGlyphWidth; ++column) {
        const auto bit =
            static_cast<std::uint8_t>(1U << (kGlyphWidth - column - 1));
        if ((glyph->rows[static_cast<std::size_t>(row)] & bit) == 0) continue;
        fill_rect(pixels, width, height,
                  x + static_cast<int>(index) * kGlyphAdvance * scale +
                      column * scale,
                  y + row * scale, scale, scale, color);
      }
    }
  }
}

} // namespace

auto render_title(ViewportSize size,
                  std::span<termforge::Pixel> destination) noexcept
    -> std::expected<TitleRenderResult, TitleRenderError> {
  if (!validate_viewport(size)) {
    return std::unexpected{TitleRenderError::invalid_dimensions};
  }
  const auto width = static_cast<std::size_t>(size.width);
  const auto height = static_cast<std::size_t>(size.height);
  if (width > std::numeric_limits<std::size_t>::max() / height ||
      destination.size() != width * height) {
    return std::unexpected{TitleRenderError::buffer_size_mismatch};
  }

  const int scale =
      std::min((size.width - 8) / kLogoWidth, (size.height - 8) / kLogoHeight);
  if (scale < 1) return std::unexpected{TitleRenderError::too_small};

  constexpr Pixel deep_space{5, 12, 22, 255};
  constexpr Pixel upper_space{8, 24, 38, 255};
  constexpr Pixel star{184, 220, 218, 255};
  constexpr Pixel shadow{28, 80, 88, 255};
  constexpr Pixel title{126, 214, 210, 255};
  constexpr Pixel accent{238, 184, 104, 255};

  for (int y = 0; y < size.height; ++y) {
    const int blend = (y * 255) / std::max(1, size.height - 1);
    const auto channel = [blend](std::uint8_t top,
                                 std::uint8_t bottom) -> std::uint8_t {
      return static_cast<std::uint8_t>((static_cast<int>(top) * (255 - blend) +
                                        static_cast<int>(bottom) * blend) /
                                       255);
    };
    const Pixel background{channel(upper_space.r, deep_space.r),
                           channel(upper_space.g, deep_space.g),
                           channel(upper_space.b, deep_space.b), 255};
    for (int x = 0; x < size.width; ++x) {
      auto& pixel = destination[static_cast<std::size_t>(y) * width +
                                static_cast<std::size_t>(x)];
      const std::uint32_t hash = static_cast<std::uint32_t>(x) * 73856093U ^
                                 static_cast<std::uint32_t>(y) * 19349663U;
      pixel = hash % 997U == 0U ? star : background;
    }
  }

  const int logo_width = kLogoWidth * scale;
  const int logo_height = kLogoHeight * scale;
  const int logo_x = (size.width - logo_width) / 2;
  const int logo_y = (size.height - logo_height) / 2;
  const int shadow_offset = std::max(1, scale / 5);
  const int second_line_y = logo_y + (kGlyphHeight + kLineGap) * scale;

  draw_word(destination, size.width, size.height, kTop, logo_x + shadow_offset,
            logo_y + shadow_offset, scale, shadow);
  draw_word(destination, size.width, size.height, kBottom,
            logo_x + shadow_offset, second_line_y + shadow_offset, scale,
            shadow);
  draw_word(destination, size.width, size.height, kTop, logo_x, logo_y, scale,
            title);
  draw_word(destination, size.width, size.height, kBottom, logo_x,
            second_line_y, scale, title);

  const int accent_y =
      std::min(size.height - 2, logo_y + logo_height + std::max(1, scale / 2));
  fill_rect(destination, size.width, size.height, logo_x, accent_y, logo_width,
            std::max(1, scale / 5), accent);

  return TitleRenderResult{
      .scale = scale,
      .logo_bounds = {logo_x, logo_y, logo_width, logo_height},
  };
}

} // namespace apsis_drift
