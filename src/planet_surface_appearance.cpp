#include "apsis_drift/planet_surface_appearance.hpp"

namespace apsis_drift {
namespace {
using Error = PlanetSurfaceAppearanceError;

auto valid_atmosphere(const PlanetDescriptor& planet) noexcept -> bool {
  const auto pressure = planet.atmosphere_pressure.value;
  switch (planet.atmosphere_class) {
    case AtmosphereClass::airless: return pressure == 0;
    case AtmosphereClass::tenuous: return pressure >= 1 && pressure <= 249;
    case AtmosphereClass::temperate:
      return pressure >= 250 && pressure <= 1'499;
    case AtmosphereClass::dense:
      return pressure >= 1'500 && pressure <= AtmospherePressureMillibars::max;
  }
  return false;
}
auto valid_terrain(TerrainCharacter terrain) noexcept -> bool {
  switch (terrain) {
    case TerrainCharacter::oceanic:
    case TerrainCharacter::plains:
    case TerrainCharacter::rugged:
    case TerrainCharacter::alpine:
    case TerrainCharacter::volcanic: return true;
  }
  return false;
}
auto valid_descriptor(const PlanetDescriptor& planet) noexcept -> bool {
  return planet.radius.value >= PlanetRadiusKm::min &&
         planet.radius.value <= PlanetRadiusKm::max &&
         planet.surface_gravity.value >= SurfaceGravityMilliG::min &&
         planet.surface_gravity.value <= SurfaceGravityMilliG::max &&
         planet.water_coverage.value <= WaterCoverageBasisPoints::max &&
         valid_atmosphere(planet) && valid_terrain(planet.terrain_character);
}
} // namespace

auto resolve_planet_surface_appearance(const PlanetDescriptor& planet,
                                       std::uint32_t version)
    -> std::expected<PlanetSurfaceAppearance, Error> {
  if (version != kPlanetSurfaceAppearanceVersion)
    return std::unexpected{Error::unsupported_version};
  if (!valid_descriptor(planet)) return std::unexpected{Error::invalid_owner};
  switch (planet.palette.family) {
    case PaletteFamily::verdant:
      return PlanetSurfaceAppearance{version, 4'500, 8'500, 8'800, 3'500};
    case PaletteFamily::arid:
      return PlanetSurfaceAppearance{version, 5'500, 6'500, 9'800, 1'800};
    case PaletteFamily::glacial:
      return PlanetSurfaceAppearance{version, 7'200, 3'000, 3'800, 6'500};
    case PaletteFamily::volcanic:
      return PlanetSurfaceAppearance{version, 4'300, 12'500, 8'500, 4'000};
    case PaletteFamily::alien:
      return PlanetSurfaceAppearance{version, 5'000, 9'000, 8'200, 4'500};
  }
  return std::unexpected{Error::invalid_owner};
}

auto validate_planet_surface_appearance(const PlanetSurfaceAppearance& profile)
    -> std::expected<void, Error> {
  if (profile.version != kPlanetSurfaceAppearanceVersion)
    return std::unexpected{Error::unsupported_version};
  if (profile.albedo_scale_basis_points > 10'000 ||
      profile.detail_contrast_basis_points > 20'000 ||
      profile.roughness_basis_points > 10'000 ||
      profile.specular_basis_points > 10'000)
    return std::unexpected{Error::invalid_profile};
  return {};
}
} // namespace apsis_drift
