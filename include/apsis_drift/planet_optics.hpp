#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>

#include "apsis_drift/planet_ambient.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kPlanetOpticsVersion{1};
inline constexpr std::size_t kPlanetOpticsWidth{64};
inline constexpr std::size_t kPlanetOpticsHeight{32};

struct PlanetOpticsRecipe {
  std::uint32_t version{kPlanetOpticsVersion};
  PlanetAmbientRecipe ambient;
  friend auto operator==(const PlanetOpticsRecipe&, const PlanetOpticsRecipe&)
      -> bool = default;
};

// Static body-fixed optical appearance, not current weather or exposure.
// Atlas nodes: longitude -pi+2*pi*x/64, latitude -pi/2+pi*y/31.
// Longitude repeats; both pole rows are constant. No seed/maxima projection.
struct PlanetOpticsField {
  PlanetOpticsRecipe recipe;
  AtmosphericVisibilityKind kind{};
  std::array<std::uint8_t, kPlanetOpticsWidth * kPlanetOpticsHeight> density{};
  double lower_height_metres{}, upper_height_metres{};
  std::uint16_t opacity_cap_basis_points{};
  Rgb8 tint{};
  friend auto operator==(const PlanetOpticsField&, const PlanetOpticsField&)
      -> bool = default;
};

enum class PlanetOpticsError : std::uint8_t {
  unsupported_version,
  invalid_owner,
  invalid_field,
  invalid_sample,
};

[[nodiscard]] auto generate_planet_optics_recipe(
    const PhysicalLocalSystem&, PlanetId,
    std::uint32_t version = kPlanetOpticsVersion)
    -> std::expected<PlanetOpticsRecipe, PlanetOpticsError>;
[[nodiscard]] auto resolve_planet_optics(const PhysicalLocalSystem&,
                                         const PlanetOpticsRecipe&)
    -> std::expected<PlanetOpticsField, PlanetOpticsError>;
// Bounds/coherence for authored references only; does not authenticate
// generated ownership or certify that edited pixels belong to a real seeded
// body.
[[nodiscard]] auto validate_planet_optics_field(const PlanetOpticsField&)
    -> std::expected<void, PlanetOpticsError>;
// Pure bilinear atlas sampling times 4*t*(1-t) within the height band.
// Angles must be finite latitude[-pi/2,pi/2], longitude[-pi,pi]. Height is
// reference-sphere metres. Out-of-band finite heights return zero density.
[[nodiscard]] auto sample_planet_optics_density(const PlanetOpticsField&,
                                                double latitude_radians,
                                                double longitude_radians,
                                                double height_metres)
    -> std::expected<double, PlanetOpticsError>;
} // namespace apsis_drift
