#pragma once

#include <cstddef>
#include <expected>
#include <span>

#include "apsis_drift/coordinates.hpp"
#include "apsis_drift/render_profile.hpp"
#include "apsis_drift/terrain_tiles.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift {

// Orbital presentation uses the planet-fixed frame from the coordinate
// contract. Camera directions do not need to be normalized, but forward and
// up must be non-zero and non-collinear.
struct OrbitalCamera {
  PlanetFixedPositionMetres position{0.0, -30'000'000.0, 0.0};
  PlanetFixedDirection forward{0.0, 1.0, 0.0};
  PlanetFixedDirection up{0.0, 0.0, 1.0};
};

struct OrbitalRenderSettings {
  int width{kDefaultViewportWidth};
  int height{kDefaultViewportHeight};
  double field_of_view_degrees{55.0};
  // Adjacent output columns may share one centered ray sample. The planetary
  // handoff uses two-column sampling at local-profile widths to bound the
  // tile-backed pass without changing generated terrain identity.
  int horizontal_sample_stride{1};
  // Unit direction is from the planet toward the light source.
  PlanetFixedDirection light_direction{-0.45, -0.55, 0.70};
};

struct OrbitalRenderStats {
  std::size_t surface_pixels{};
  std::size_t atmosphere_pixels{};
  std::size_t terrain_tiles_touched{};

  friend auto operator==(const OrbitalRenderStats&,
                         const OrbitalRenderStats&) -> bool = default;
};

enum class OrbitalRenderError {
  invalid_viewport,
  invalid_framebuffer,
  invalid_planet,
  non_finite_camera,
  camera_inside_planet,
  invalid_camera_basis,
  invalid_field_of_view,
  invalid_sample_stride,
  invalid_light_direction,
  invalid_terrain_lod,
  terrain_failure,
};

class OrbitalRenderer {
 public:
  explicit OrbitalRenderer(OrbitalRenderSettings settings = {});

  [[nodiscard]] auto settings() const noexcept
      -> const OrbitalRenderSettings& {
    return m_settings;
  }

  // All inputs are validated before rendering begins. A failure leaves the
  // destination unchanged.
  [[nodiscard]] auto render(
      const PlanetDescriptor& planet, const OrbitalCamera& camera,
      std::span<termforge::Pixel> destination) const
      -> std::expected<OrbitalRenderStats, OrbitalRenderError>;

  // The tile-backed path preserves generated surface identity through the
  // orbital-to-local presentation handoff. Cache residency affects cost only.
  [[nodiscard]] auto render_tile_backed(
      const PlanetDescriptor& planet, const OrbitalCamera& camera,
      std::uint8_t terrain_lod, TerrainTileCache& cache,
      std::span<termforge::Pixel> destination) const
      -> std::expected<OrbitalRenderStats, OrbitalRenderError>;

 private:
  [[nodiscard]] auto render_impl(
      const PlanetDescriptor& planet, const OrbitalCamera& camera,
      TerrainTileCache* cache, std::uint8_t terrain_lod,
      std::span<termforge::Pixel> destination) const
      -> std::expected<OrbitalRenderStats, OrbitalRenderError>;

  OrbitalRenderSettings m_settings;
};

}  // namespace apsis_drift
