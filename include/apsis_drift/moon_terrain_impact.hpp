#pragma once

#include "apsis_drift/moon_surface_contact.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kMoonTerrainImpactVersion{1};
inline constexpr std::uint32_t kMoonTerrainImpactColliderVersion{1};
inline constexpr std::uint32_t kMoonTerrainImpactMaximumCells{128};
inline constexpr double kMoonTerrainImpactClearMarginMetres{0.05};
inline constexpr double kMoonTerrainImpactResponseRetreatMetres{0.002};

enum class MoonCraftImpactRegion : std::uint8_t {
  forebody = 1,
  landing_structure = 2
};
enum class MoonTerrainImpactClass : std::uint8_t {
  clear = 1,
  unknown = 2,
  impact = 3,
  near = 4
};
struct MoonTerrainImpactWitness {
  std::uint32_t version{kMoonTerrainImpactVersion};
  std::uint32_t terrain_version{kMoonTerrainRecipeVersion};
  std::uint32_t collider_version{kMoonTerrainImpactColliderVersion};
  CraftFrameRecipe craft;
  SystemId system;
  BodyId body;
  SimulationTick tick{};
  std::uint64_t source_checksum{}, candidate_checksum{};
  double fraction{};
  MoonContactTriangleId triangle;
  MoonCraftImpactRegion region{};
  std::uint8_t support_index{};
  // Authoring origin, not COM; normal/velocity are in genuine moon-fixed6.
  RigidVector3 contact_point_craft_metres;
  RigidVector3 outward_normal_moon_fixed;
  RigidVector3 pre_response_velocity_moon_fixed;
  double closing_speed_metres_per_second{};
  friend auto operator==(const MoonTerrainImpactWitness&,
                         const MoonTerrainImpactWitness&) -> bool = default;
};
struct MoonTerrainImpactSweep {
  std::uint32_t version{kMoonTerrainImpactVersion};
  MoonTerrainImpactClass classification{MoonTerrainImpactClass::unknown};
  double minimum_clearance_metres{};
  std::optional<MoonTerrainImpactWitness> witness;
  friend auto operator==(const MoonTerrainImpactSweep&,
                         const MoonTerrainImpactSweep&) -> bool = default;
};
// Same bounded registered hull/deployed-stroke proxy and fixed-start attitude
// as the planet path. Own-tick endpoints translate linearly in actual fixed6.
// The full BodyId/descriptor is authenticated; all signed moon relief is dry.
// Unresolved coverage, burial, rotation or arithmetic remains unknown, never
// clear. This is finite top-facet collision, not a visual-hull/solid theorem.
[[nodiscard]] auto assess_moon_terrain_impact(
    const PhysicalMoonDescriptor&, const RigidBodyState& before,
    const RigidBodyState& candidate, bool gear_deployed,
    std::uint32_t terrain_version = kMoonTerrainRecipeVersion)
    -> std::expected<MoonTerrainImpactSweep, std::string>;
// Revalidate the exact witnessed finite contact, stop inward motion at its
// fraction, retreat2mm, preserve tangent velocity and stop relative spin.
[[nodiscard]] auto respond_moon_terrain_impact(const PhysicalMoonDescriptor&,
                                               const RigidBodyState& before,
                                               const RigidBodyState& candidate,
                                               const MoonTerrainImpactWitness&)
    -> std::expected<RigidBodyState, std::string>;
} // namespace apsis_drift
