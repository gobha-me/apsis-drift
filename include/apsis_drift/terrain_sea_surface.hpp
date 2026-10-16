#pragma once

#include <cmath>
#include <cstdint>
#include <expected>

namespace apsis_drift {
inline constexpr std::uint32_t kSeaDatumTerrainPolicy{3};

enum class TerrainSeaSurfaceError : std::uint8_t {
  unsupported_policy,
  non_finite_height,
  invalid_datum_mask,
};
enum class TerrainDatumClass : std::uint8_t { dry, water, shoreline };

struct TerrainSeaSurfaceSample {
  double elevation_metres{};
  bool wet_datum{};
};

[[nodiscard]] inline auto valid_terrain_surface_policy(
    std::uint32_t policy) noexcept -> bool {
  return policy == 1 || policy == 2 || policy == kSeaDatumTerrainPolicy;
}

// Policy3 changes the selected top, not the underlying recipe2 terrain field.
[[nodiscard]] inline auto terrain_surface_raw_recipe(std::uint32_t policy)
    -> std::expected<std::uint32_t, TerrainSeaSurfaceError> {
  if (!valid_terrain_surface_policy(policy))
    return std::unexpected{TerrainSeaSurfaceError::unsupported_policy};
  return policy == kSeaDatumTerrainPolicy ? 2U : policy;
}

// The same vertex operation serves streamed geometry and finest contact.
// Wetness is the raw datum sign, never palette, phase or support evidence.
// Legacy policies retain their exact height, including a signed zero.
[[nodiscard]] inline auto select_terrain_sea_surface(std::uint32_t policy,
                                                     double raw_height)
    -> std::expected<TerrainSeaSurfaceSample, TerrainSeaSurfaceError> {
  if (!valid_terrain_surface_policy(policy))
    return std::unexpected{TerrainSeaSurfaceError::unsupported_policy};
  if (!std::isfinite(raw_height))
    return std::unexpected{TerrainSeaSurfaceError::non_finite_height};
  if (policy != kSeaDatumTerrainPolicy)
    return TerrainSeaSurfaceSample{raw_height, false};
  return TerrainSeaSurfaceSample{raw_height > 0 ? raw_height : 0.0,
                                 raw_height <= 0};
}

// Three bits describe the raw height sign of the canonical triangle vertices.
// Clamped vertices lie on a sphere, but their shared physical facet is a chord.
[[nodiscard]] inline auto classify_terrain_datum_mask(std::uint8_t mask)
    -> std::expected<TerrainDatumClass, TerrainSeaSurfaceError> {
  if (mask > 7)
    return std::unexpected{TerrainSeaSurfaceError::invalid_datum_mask};
  if (mask == 0) return TerrainDatumClass::dry;
  if (mask == 7) return TerrainDatumClass::water;
  return TerrainDatumClass::shoreline;
}
} // namespace apsis_drift
