#pragma once

#include "apsis_drift/freedom_save.hpp"
#include "apsis_drift/moon_body_dynamics.hpp"
#include "apsis_drift/moon_terrain.hpp"
#include "apsis_drift/physical_moon_rotation.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomMoonFlightSaveFormatVersion{42};
inline constexpr std::uint32_t kFreedomNetworkMoonFlightSaveFormatVersion{54};
inline constexpr std::uint32_t kFreedomMoonWorldVersion{1};

// Explicit moon selection, mutually exclusive with the old planet selection.
// These recipes never reinterpret the historical planet model fields.
struct FreedomMoonWorldSelection {
  std::uint32_t version{kFreedomMoonWorldVersion};
  BodyTarget target;
  std::uint32_t physical_recipe{kPhysicalMoonRecipeVersion};
  std::uint32_t rotation_recipe{kPhysicalMoonRotationVersion};
  std::uint32_t dynamics_recipe{kMoonBodyDynamicsVersion};
  std::uint32_t terrain_recipe{kMoonTerrainRecipeVersion};
  friend auto operator==(const FreedomMoonWorldSelection&,
                         const FreedomMoonWorldSelection&) -> bool = default;
};
struct FreedomMoonFlightHydration {
  PhysicalMoonDescriptor moon;
  PhysicalMoonRotationGeometry rotation;
  MoonBodyGravity gravity;
};
struct FreedomFlightSaveDocument;
[[nodiscard]] auto hydrate_freedom_moon_flight_document(
    const FreedomFlightSaveDocument&)
    -> std::expected<FreedomMoonFlightHydration, SaveSchemaError>;
} // namespace apsis_drift
