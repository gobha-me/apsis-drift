#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "apsis_drift/render_profile.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift {

inline constexpr int kFrameWidth{kDefaultViewportWidth};
inline constexpr int kFrameHeight{kDefaultViewportHeight};
inline constexpr std::uint8_t kWaterLevel{70};

enum class TerrainError {
  size_too_small,
  size_not_power_of_two,
  size_too_large,
};

class Terrain {
 public:
  [[nodiscard]] static auto generate(int size, std::uint32_t seed)
      -> std::expected<Terrain, TerrainError>;

  [[nodiscard]] auto size() const noexcept -> int { return m_size; }
  [[nodiscard]] auto height_at(int x, int y) const noexcept -> std::uint8_t;
  [[nodiscard]] auto color_at(int x, int y) const noexcept
      -> termforge::Pixel;
  [[nodiscard]] auto checksum() const noexcept -> std::uint64_t;

 private:
  explicit Terrain(int size);

  [[nodiscard]] auto index(int x, int y) const noexcept -> std::size_t;
  auto build_colors() -> void;

  int m_size{};
  std::vector<std::uint8_t> m_heights;
  std::vector<termforge::Pixel> m_colors;
};

struct Camera {
  float x{180.0F};
  float y{240.0F};
  float height{135.0F};
  float yaw{0.35F};
  float horizon{205.0F};
};

struct RenderSettings {
  int width{kFrameWidth};
  int height{kFrameHeight};
  float field_of_view_degrees{72.0F};
  float max_distance{900.0F};
  float vertical_scale{255.0F};
  float fog_start{420.0F};
};

class VoxelRenderer {
 public:
  explicit VoxelRenderer(RenderSettings settings = {});

  [[nodiscard]] auto settings() const noexcept -> const RenderSettings& {
    return m_settings;
  }

  // Returns false without touching the destination when its length does not
  // exactly match the configured framebuffer.
  [[nodiscard]] auto render(const Terrain& terrain, const Camera& camera,
                            std::span<termforge::Pixel> destination) -> bool;

 private:
  RenderSettings m_settings;
  std::vector<int> m_occlusion;
};

[[nodiscard]] auto pixel_checksum(std::span<const termforge::Pixel> pixels)
    noexcept -> std::uint64_t;

}  // namespace apsis_drift
