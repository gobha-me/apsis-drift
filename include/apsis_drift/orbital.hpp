#pragma once

#include <cstddef>
#include <expected>
#include <span>

#include "apsis_drift/coordinates.hpp"
#include "apsis_drift/render_profile.hpp"
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
  // Unit direction is from the planet toward the light source.
  PlanetFixedDirection light_direction{-0.45, -0.55, 0.70};
};

struct OrbitalRenderStats {
  std::size_t surface_pixels{};
  std::size_t atmosphere_pixels{};

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
  invalid_light_direction,
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

 private:
  OrbitalRenderSettings m_settings;
};

}  // namespace apsis_drift
