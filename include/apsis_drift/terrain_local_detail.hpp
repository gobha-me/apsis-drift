#pragma once

#include <optional>
#include <span>

#include "apsis_drift/body_ephemeris.hpp"
#include "apsis_drift/terrain_tiles.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kLegacyPhysicalTerrainRecipe{1};
inline constexpr std::uint32_t kLocalDetailTerrainRecipe{2};
inline constexpr std::uint8_t kLocalDetailMacroSourceLod{8};
inline constexpr double kLocalDetailSlopeStepMetres{32.0};

// Explicit selection; neither recipe changes saved generator defaults.
struct TerrainLocalDetailRecipe {
  std::uint32_t version{};
  friend auto operator==(const TerrainLocalDetailRecipe&,
                         const TerrainLocalDetailRecipe&) -> bool = default;
};

struct TerrainLocalDetailSample {
  double macro_elevation_metres{};
  double local_offset_metres{};
  double elevation_metres{};
  Rgb8 color;
  // Recipe2: centered 32m Cartesian stencil, projected onto the reference
  // sphere's tangent. A finite-scale slope input, never a contact normal.
  // Recipe1 preserves legacy height/color and supplies no slope estimate.
  std::optional<PlanetFixedDirection> tangent_slope_estimate;
  friend auto operator==(const TerrainLocalDetailSample&,
                         const TerrainLocalDetailSample&) -> bool = default;
};

enum class TerrainLocalDetailError : std::uint8_t {
  unsupported_version,
  invalid_owner,
  invalid_body,
  invalid_direction,
  invalid_tile,
  invalid_dimensions,
  short_buffer,
  invalid_macro_source,
  unsafe_arithmetic,
};

// Immutable selected catalog/body ownership. The caller's bounded legacy
// cache stores only generator1 macro tiles; detail outputs never enter it.
class TerrainLocalDetailSampler {
 public:
  [[nodiscard]] static auto create(const PhysicalLocalSystem& owner,
                                   BodyId body, TerrainLocalDetailRecipe recipe,
                                   TerrainTileCache& macro_cache)
      -> std::expected<TerrainLocalDetailSampler, TerrainLocalDetailError>;

  [[nodiscard]] auto owner() const noexcept -> const PhysicalLocalSystem& {
    return m_owner;
  }
  [[nodiscard]] auto body() const noexcept -> BodyId { return m_body; }
  [[nodiscard]] auto recipe() const noexcept -> TerrainLocalDetailRecipe {
    return m_recipe;
  }
  [[nodiscard]] auto planet() const noexcept -> const PlanetDescriptor& {
    return m_owner.catalog.planets[m_planet_index].descriptor;
  }

  // Output LOD selects locations/legacy sampling, never recipe2's field.
  [[nodiscard]] auto sample(PlanetFixedDirection direction,
                            std::uint8_t output_lod) const
      -> std::expected<TerrainLocalDetailSample, TerrainLocalDetailError>;

  // Fixed 65x65 layout. Validate all inputs before staging; any refusal leaves
  // the entire caller buffer unchanged, including an unused trailing region.
  [[nodiscard]] auto fill_tile(TerrainTileKey key, std::size_t samples_per_axis,
                               std::span<TerrainLocalDetailSample> output) const
      -> std::expected<void, TerrainLocalDetailError>;

 private:
  TerrainLocalDetailSampler(PhysicalLocalSystem owner, BodyId body,
                            TerrainLocalDetailRecipe recipe,
                            std::size_t planet_index,
                            TerrainTileCache& macro_cache);
  [[nodiscard]] auto height(PlanetFixedDirection direction,
                            std::uint8_t output_lod) const
      -> std::expected<TerrainLocalDetailSample, TerrainLocalDetailError>;

  PhysicalLocalSystem m_owner;
  BodyId m_body;
  TerrainLocalDetailRecipe m_recipe;
  std::size_t m_planet_index{};
  TerrainTileCache* m_macro_cache{};
  Seed m_relief_seed;
};

} // namespace apsis_drift
