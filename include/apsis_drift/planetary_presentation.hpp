#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "apsis_drift/celestial.hpp"
#include "apsis_drift/orbital.hpp"
#include "apsis_drift/planetary_flight.hpp"
#include "apsis_drift/terrain_tiles.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift {

enum class PlanetaryPresentationMode : std::uint8_t {
  orbital,
  atmospheric,
  terrain_blend,
  local_terrain,
};

[[nodiscard]] auto planetary_presentation_mode_name(
    PlanetaryPresentationMode mode) noexcept -> std::string_view;

struct PlanetaryPresentationMix {
  double atmosphere{};
  double local_terrain{};

  friend auto operator==(const PlanetaryPresentationMix&,
                         const PlanetaryPresentationMix&) -> bool = default;
};

struct PlanetaryPresentationCamera {
  // Positive pitch looks above the local geometric horizon.
  double pitch_radians{};
};

struct PlanetaryPresentationSettings {
  int width{kDefaultViewportWidth};
  int height{kDefaultViewportHeight};
  double field_of_view_degrees{60.0};
  // Preserve the established near-field density and fog response, then extend
  // only as far as the camera geometry requires for a continuous handoff.
  double local_near_distance_metres{900.0};
  double local_max_distance_metres{32'000.0};
  double local_fog_start_metres{420.0};
  std::uint8_t orbital_terrain_lod{2};
  std::size_t terrain_cache_capacity{kDefaultTerrainTileCacheCapacity};
};

struct PlanetaryRenderStats {
  PlanetaryPresentationMode mode{};
  PlanetaryPresentationMix mix;
  LocalSunGeometry sun;
  double local_solar_elevation{};
  std::uint8_t local_terrain_lod{};
  TerrainTileAddress surface_anchor;
  std::size_t orbital_tiles_touched{};
  std::size_t local_tiles_touched{};
  std::size_t local_terrain_pixels{};
  double local_distance_metres{};
  bool orbital_surface_fallback{};
  std::size_t sun_pixels{};
  double orbital_render_ms{};
  double local_render_ms{};
  double composite_ms{};
  double total_ms{};
};

enum class PlanetaryPresentationError : std::uint8_t {
  invalid_settings,
  invalid_framebuffer,
  invalid_state,
  invalid_camera,
  coordinate_failure,
  terrain_failure,
  orbital_failure,
  celestial_failure,
};

[[nodiscard]] auto planetary_presentation_mix(
    const PlanetDescriptor& planet,
    const PlanetaryFlightState& state) noexcept
    -> std::expected<PlanetaryPresentationMix, PlanetaryPresentationError>;

class PlanetaryPresentationRenderer {
 public:
  explicit PlanetaryPresentationRenderer(
      PlanetaryPresentationSettings settings = {});

  [[nodiscard]] auto settings() const noexcept
      -> const PlanetaryPresentationSettings& {
    return m_settings;
  }

  // A rejected frame leaves destination unchanged. Timing and cache state are
  // presentation diagnostics and never enter deterministic simulation state.
  [[nodiscard]] auto render(
      const PlanetDescriptor& planet, const PlanetaryFlightState& state,
      PlanetaryPresentationCamera camera,
      std::span<termforge::Pixel> destination)
      -> std::expected<PlanetaryRenderStats, PlanetaryPresentationError>;

 private:
  PlanetaryPresentationSettings m_settings;
  std::optional<TerrainTileCache> m_cache;
  OrbitalRenderer m_orbital_renderer;
  std::vector<termforge::Pixel> m_orbital_frame;
  std::vector<termforge::Pixel> m_local_frame;
  std::vector<std::uint8_t> m_local_coverage;
};

}  // namespace apsis_drift
