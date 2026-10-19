#pragma once

#include <span>

#include "apsis_drift/physical_moon.hpp"
#include "apsis_drift/terrain_tiles.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kMoonTerrainRecipeVersion{1};
// Recipe1's six bounded smooth lattice octaves have this absolute sum.
inline constexpr double kMoonTerrainMaximumReliefMetres{4545.0};

struct MoonFixedDirection {
  double x{}, y{}, z{};
  friend auto operator==(const MoonFixedDirection&, const MoonFixedDirection&)
      -> bool = default;
};
struct MoonFixedPositionMetres {
  double x{}, y{}, z{};
  friend auto operator==(const MoonFixedPositionMetres&,
                         const MoonFixedPositionMetres&) -> bool = default;
};
struct MoonTerrainTileKey {
  BodyId body;
  CubeFace face{};
  std::uint8_t lod{};
  std::uint32_t x{}, y{};
  friend auto operator==(const MoonTerrainTileKey&, const MoonTerrainTileKey&)
      -> bool = default;
};
struct MoonTerrainSample {
  double elevation_metres{};
  Rgb8 color;
  friend auto operator==(const MoonTerrainSample&, const MoonTerrainSample&)
      -> bool = default;
};
enum class MoonTerrainError : std::uint8_t {
  unsupported_version,
  invalid_owner,
  invalid_direction,
  invalid_position,
  invalid_tile,
  wrong_body,
  invalid_dimensions,
  short_buffer,
  unsafe_arithmetic,
};

// Owns the authenticated source. Rendering, clearance and walking consume this
// same continuous field; output LOD changes mesh locations, never its heights.
class MoonTerrainSampler {
 public:
  [[nodiscard]] static auto create(
      const PhysicalMoonDescriptor&,
      std::uint32_t version = kMoonTerrainRecipeVersion)
      -> std::expected<MoonTerrainSampler, MoonTerrainError>;
  [[nodiscard]] auto owner() const noexcept -> const PhysicalMoonDescriptor& {
    return m_owner;
  }
  [[nodiscard]] auto sample(MoonFixedDirection) const
      -> std::expected<MoonTerrainSample, MoonTerrainError>;
  [[nodiscard]] auto surface_position(MoonFixedDirection) const
      -> std::expected<MoonFixedPositionMetres, MoonTerrainError>;
  [[nodiscard]] auto clearance(MoonFixedPositionMetres) const
      -> std::expected<double, MoonTerrainError>;
  // Caller output is untouched on every refusal; trailing elements are kept.
  [[nodiscard]] auto fill_tile(MoonTerrainTileKey, std::size_t samples_per_axis,
                               std::span<MoonTerrainSample>) const
      -> std::expected<void, MoonTerrainError>;

 private:
  explicit MoonTerrainSampler(PhysicalMoonDescriptor owner)
      : m_owner{std::move(owner)} {}
  [[nodiscard]] auto sample_unit(MoonFixedDirection) const
      -> std::expected<MoonTerrainSample, MoonTerrainError>;
  PhysicalMoonDescriptor m_owner;
};

} // namespace apsis_drift
