#include "apsis_drift/landscape.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace apsis_drift {
namespace {

class XorShift32 {
 public:
  explicit XorShift32(std::uint32_t seed)
      : m_state(seed == 0 ? 0x9E3779B9U : seed) {}

  [[nodiscard]] auto next() noexcept -> std::uint32_t {
    auto value = m_state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    m_state = value;
    return value;
  }

  [[nodiscard]] auto signed_unit() noexcept -> float {
    constexpr float denominator =
        static_cast<float>(std::numeric_limits<std::uint32_t>::max());
    return static_cast<float>(next()) / denominator * 2.0F - 1.0F;
  }

 private:
  std::uint32_t m_state;
};

[[nodiscard]] auto clamp_channel(float value) noexcept -> std::uint8_t {
  return static_cast<std::uint8_t>(
      std::clamp(static_cast<int>(std::lround(value)), 0, 255));
}

[[nodiscard]] auto shade(termforge::Pixel color, float factor)
    -> termforge::Pixel {
  color.r = clamp_channel(static_cast<float>(color.r) * factor);
  color.g = clamp_channel(static_cast<float>(color.g) * factor);
  color.b = clamp_channel(static_cast<float>(color.b) * factor);
  color.a = 255;
  return color;
}

[[nodiscard]] auto mix(termforge::Pixel from, termforge::Pixel to, float amount)
    -> termforge::Pixel {
  amount = std::clamp(amount, 0.0F, 1.0F);
  const float keep = 1.0F - amount;
  return {
      clamp_channel(static_cast<float>(from.r) * keep +
                    static_cast<float>(to.r) * amount),
      clamp_channel(static_cast<float>(from.g) * keep +
                    static_cast<float>(to.g) * amount),
      clamp_channel(static_cast<float>(from.b) * keep +
                    static_cast<float>(to.b) * amount),
      255,
  };
}

[[nodiscard]] auto terrain_color(std::uint8_t elevation) -> termforge::Pixel {
  if (elevation < kWaterLevel - 20) return {16, 48, 92, 255};
  if (elevation < kWaterLevel) return {24, 78, 118, 255};
  if (elevation < kWaterLevel + 8) return {181, 164, 103, 255};
  if (elevation < 128) return {55, 112, 62, 255};
  if (elevation < 178) return {83, 105, 69, 255};
  if (elevation < 220) return {117, 111, 102, 255};
  return {224, 229, 226, 255};
}

}  // namespace

Terrain::Terrain(int size)
    : m_size(size),
      m_heights(static_cast<std::size_t>(size) * size),
      m_colors(static_cast<std::size_t>(size) * size) {}

auto Terrain::generate(int size, std::uint32_t seed)
    -> std::expected<Terrain, TerrainError> {
  if (size < 32) return std::unexpected{TerrainError::size_too_small};
  if (!std::has_single_bit(static_cast<unsigned>(size))) {
    return std::unexpected{TerrainError::size_not_power_of_two};
  }
  if (size > 4096) return std::unexpected{TerrainError::size_too_large};

  Terrain terrain{size};
  std::vector<float> field(static_cast<std::size_t>(size) * size, 0.0F);
  const auto wrapped_index = [size](int x, int y) {
    const int wx = (x % size + size) % size;
    const int wy = (y % size + size) % size;
    return static_cast<std::size_t>(wy) * size + wx;
  };

  XorShift32 random{seed};
  field[0] = 128.0F + random.signed_unit() * 18.0F;
  float displacement = 118.0F;

  for (int step = size; step > 1; step /= 2) {
    const int half = step / 2;

    for (int y = half; y < size; y += step) {
      for (int x = half; x < size; x += step) {
        const float average =
            (field[wrapped_index(x - half, y - half)] +
             field[wrapped_index(x + half, y - half)] +
             field[wrapped_index(x - half, y + half)] +
             field[wrapped_index(x + half, y + half)]) *
            0.25F;
        field[wrapped_index(x, y)] =
            average + random.signed_unit() * displacement;
      }
    }

    for (int y = 0; y < size; y += half) {
      const int start_x = (y + half) % step;
      for (int x = start_x; x < size; x += step) {
        const float average =
            (field[wrapped_index(x - half, y)] +
             field[wrapped_index(x + half, y)] +
             field[wrapped_index(x, y - half)] +
             field[wrapped_index(x, y + half)]) *
            0.25F;
        field[wrapped_index(x, y)] =
            average + random.signed_unit() * displacement;
      }
    }

    displacement *= 0.54F;
  }

  const auto [low, high] = std::minmax_element(field.begin(), field.end());
  const float span = std::max(1.0F, *high - *low);
  for (std::size_t i = 0; i < field.size(); ++i) {
    const float normalized = (field[i] - *low) / span;
    // Bias some low ground into water and retain a long mountainous tail.
    const float shaped = std::pow(normalized, 1.08F) * 255.0F;
    terrain.m_heights[i] = clamp_channel(shaped);
  }
  terrain.build_colors();
  return terrain;
}

auto Terrain::index(int x, int y) const noexcept -> std::size_t {
  const unsigned mask = static_cast<unsigned>(m_size - 1);
  const auto wrapped_x = static_cast<unsigned>(x) & mask;
  const auto wrapped_y = static_cast<unsigned>(y) & mask;
  return static_cast<std::size_t>(wrapped_y) * static_cast<unsigned>(m_size) +
         wrapped_x;
}

auto Terrain::height_at(int x, int y) const noexcept -> std::uint8_t {
  return m_heights[index(x, y)];
}

auto Terrain::color_at(int x, int y) const noexcept -> termforge::Pixel {
  return m_colors[index(x, y)];
}

auto Terrain::build_colors() -> void {
  constexpr float light_x{-0.55F};
  constexpr float light_y{-0.35F};
  for (int y = 0; y < m_size; ++y) {
    for (int x = 0; x < m_size; ++x) {
      const auto elevation = height_at(x, y);
      termforge::Pixel color = terrain_color(elevation);
      if (elevation < kWaterLevel) {
        const float glint = ((x + y) & 7) == 0 ? 1.08F : 0.96F;
        m_colors[index(x, y)] = shade(color, glint);
        continue;
      }

      const float dx = static_cast<float>(height_at(x + 1, y)) -
                       static_cast<float>(height_at(x - 1, y));
      const float dy = static_cast<float>(height_at(x, y + 1)) -
                       static_cast<float>(height_at(x, y - 1));
      const float lighting = std::clamp(0.82F + dx * light_x * 0.018F +
                                           dy * light_y * 0.018F,
                                       0.48F, 1.22F);
      m_colors[index(x, y)] = shade(color, lighting);
    }
  }
}

auto Terrain::checksum() const noexcept -> std::uint64_t {
  constexpr std::uint64_t offset{1469598103934665603ULL};
  constexpr std::uint64_t prime{1099511628211ULL};
  std::uint64_t hash = offset;
  for (const auto value : m_heights) {
    hash ^= value;
    hash *= prime;
  }
  return hash;
}

VoxelRenderer::VoxelRenderer(RenderSettings settings)
    : m_settings(settings),
      m_occlusion(validate_viewport({settings.width, settings.height})
                      ? static_cast<std::size_t>(settings.width)
                      : 0U) {}

auto VoxelRenderer::render(const Terrain& terrain, const Camera& camera,
                           std::span<termforge::Pixel> destination) -> bool {
  if (!validate_viewport({m_settings.width, m_settings.height}) ||
      m_settings.max_distance <= 1.0F || m_settings.vertical_scale <= 0.0F ||
      m_settings.field_of_view_degrees <= 1.0F ||
      m_settings.field_of_view_degrees >= 179.0F ||
      !std::isfinite(m_settings.max_distance) ||
      !std::isfinite(m_settings.vertical_scale) ||
      !std::isfinite(m_settings.field_of_view_degrees) ||
      !std::isfinite(m_settings.fog_start) || !std::isfinite(camera.x) ||
      !std::isfinite(camera.y) || !std::isfinite(camera.height) ||
      !std::isfinite(camera.yaw) || !std::isfinite(camera.horizon)) {
    return false;
  }
  const auto expected = static_cast<std::size_t>(m_settings.width) *
                        static_cast<std::size_t>(m_settings.height);
  if (destination.size() != expected) return false;

  const int width = m_settings.width;
  const int height = m_settings.height;
  const float horizon = std::clamp(camera.horizon, 0.0F,
                                   static_cast<float>(height - 1));

  for (int y = 0; y < height; ++y) {
    const float t = height > 1
                        ? static_cast<float>(y) /
                              static_cast<float>(height - 1)
                        : 0.0F;
    const float haze = std::clamp(t / std::max(0.01F, horizon / height),
                                  0.0F, 1.0F);
    const termforge::Pixel sky = mix({28, 68, 116, 255},
                                     {178, 199, 207, 255}, haze);
    std::fill_n(destination.begin() + static_cast<std::ptrdiff_t>(y) * width,
                width, sky);
  }

  // A small fixed sun gives the otherwise procedural scene a stable visual
  // landmark without adding an asset or another rendering subsystem.
  const int sun_x = width * 4 / 5;
  const int sun_y = static_cast<int>(horizon * 0.42F);
  const int sun_radius = std::max(4, height / 42);
  for (int y = std::max(0, sun_y - sun_radius);
       y < std::min(height, sun_y + sun_radius); ++y) {
    for (int x = std::max(0, sun_x - sun_radius);
         x < std::min(width, sun_x + sun_radius); ++x) {
      const int dx = x - sun_x;
      const int dy = y - sun_y;
      if (dx * dx + dy * dy <= sun_radius * sun_radius) {
        destination[static_cast<std::size_t>(y) * width + x] =
            {247, 220, 151, 255};
      }
    }
  }

  std::fill(m_occlusion.begin(), m_occlusion.end(), height);

  constexpr float pi{3.14159265358979323846F};
  const float half_fov =
      m_settings.field_of_view_degrees * pi / 360.0F;
  const float lateral = std::tan(half_fov);
  const float forward_x = std::cos(camera.yaw);
  const float forward_y = std::sin(camera.yaw);
  const float right_x = -forward_y;
  const float right_y = forward_x;
  const termforge::Pixel fog_color{174, 190, 196, 255};

  float distance = 1.0F;
  while (distance < m_settings.max_distance) {
    const float half_width = distance * lateral;
    float sample_x = camera.x + forward_x * distance - right_x * half_width;
    float sample_y = camera.y + forward_y * distance - right_y * half_width;
    const float step_x = right_x * (2.0F * half_width / width);
    const float step_y = right_y * (2.0F * half_width / width);
    const float fog = std::clamp(
        (distance - m_settings.fog_start) /
            std::max(1.0F, m_settings.max_distance - m_settings.fog_start),
        0.0F, 1.0F);

    for (int screen_x = 0; screen_x < width; ++screen_x) {
      const int map_x = static_cast<int>(std::floor(sample_x));
      const int map_y = static_cast<int>(std::floor(sample_y));
      const auto raw_height = terrain.height_at(map_x, map_y);
      const float ground = static_cast<float>(
          std::max(raw_height, kWaterLevel));
      const float projected =
          horizon + (camera.height - ground) * m_settings.vertical_scale /
                        distance;
      const int top = std::clamp(static_cast<int>(projected), 0, height);
      const int bottom = m_occlusion[static_cast<std::size_t>(screen_x)];

      if (top < bottom) {
        auto color = terrain.color_at(map_x, map_y);
        color = mix(color, fog_color, fog * fog);
        for (int screen_y = top; screen_y < bottom; ++screen_y) {
          destination[static_cast<std::size_t>(screen_y) * width + screen_x] =
              color;
        }
        m_occlusion[static_cast<std::size_t>(screen_x)] = top;
      }

      sample_x += step_x;
      sample_y += step_y;
    }

    distance += std::max(0.75F, distance * 0.0125F);
  }
  return true;
}

auto pixel_checksum(std::span<const termforge::Pixel> pixels) noexcept
    -> std::uint64_t {
  constexpr std::uint64_t offset{1469598103934665603ULL};
  constexpr std::uint64_t prime{1099511628211ULL};
  std::uint64_t hash = offset;
  for (const auto pixel : pixels) {
    hash = (hash ^ pixel.r) * prime;
    hash = (hash ^ pixel.g) * prime;
    hash = (hash ^ pixel.b) * prime;
    hash = (hash ^ pixel.a) * prime;
  }
  return hash;
}

}  // namespace apsis_drift
