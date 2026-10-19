#pragma once

#include "apsis_drift/landed_craft.hpp"
#include "apsis_drift/moon_surface_contact.hpp"
#include "apsis_drift/physical_moon_rotation.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kMoonLandedCraftAnchorVersion{1};
struct MoonLandedCraftAnchor {
  std::uint32_t version{kMoonLandedCraftAnchorVersion};
  std::uint32_t terrain_version{kMoonTerrainRecipeVersion};
  std::uint32_t rotation_version{kPhysicalMoonRotationVersion};
  RigidBodyState fixed;
  friend auto operator==(const MoonLandedCraftAnchor&,
                         const MoonLandedCraftAnchor&) -> bool = default;
};
[[nodiscard]] auto prepare_moon_landed_craft(
    const PhysicalMoonDescriptor&, const RigidBodyState&, bool gear_deployed,
    std::uint64_t expected_source_checksum,
    std::uint32_t terrain_version = kMoonTerrainRecipeVersion)
    -> std::expected<MoonLandedCraftAnchor, LandedCraftError>;
[[nodiscard]] auto resolve_moon_landed_craft(const PhysicalMoonDescriptor&,
                                             const MoonLandedCraftAnchor&,
                                             SimulationTick)
    -> std::expected<RigidBodyState, LandedCraftError>;
[[nodiscard]] auto validate_moon_landed_craft(const PhysicalMoonDescriptor&,
                                              const MoonLandedCraftAnchor&,
                                              SimulationTick)
    -> std::expected<void, LandedCraftError>;
[[nodiscard]] auto release_moon_landed_craft(const PhysicalMoonDescriptor&,
                                             const MoonLandedCraftAnchor&,
                                             SimulationTick)
    -> std::expected<RigidBodyState, LandedCraftError>;
} // namespace apsis_drift
