#pragma once

#include <cstdint>
#include <expected>

#include "apsis_drift/planet.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kPlanetSurfaceAppearanceVersion{1};

// Artist-authored material response, not composition, phase or support truth.
// Coefficients are basis points: albedo/roughness/specular 0..10,000;
// detail contrast 0..20,000. No generated stream or simulation state is used.
struct PlanetSurfaceAppearance {
  std::uint32_t version{kPlanetSurfaceAppearanceVersion};
  std::uint16_t albedo_scale_basis_points{};
  std::uint16_t detail_contrast_basis_points{};
  std::uint16_t roughness_basis_points{};
  std::uint16_t specular_basis_points{};
  friend auto operator==(const PlanetSurfaceAppearance&,
                         const PlanetSurfaceAppearance&) -> bool = default;
};

enum class PlanetSurfaceAppearanceError : std::uint8_t {
  unsupported_version,
  invalid_owner,
  invalid_profile,
};

// Resolves cosmetic response from the existing palette after descriptor bounds
// validation. This does not authenticate membership in a physical catalog.
[[nodiscard]] auto resolve_planet_surface_appearance(
    const PlanetDescriptor&,
    std::uint32_t version = kPlanetSurfaceAppearanceVersion)
    -> std::expected<PlanetSurfaceAppearance, PlanetSurfaceAppearanceError>;
[[nodiscard]] auto validate_planet_surface_appearance(
    const PlanetSurfaceAppearance&)
    -> std::expected<void, PlanetSurfaceAppearanceError>;
} // namespace apsis_drift
