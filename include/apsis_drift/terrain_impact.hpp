#pragma once

#include "apsis_drift/freedom_flight_save.hpp"
#include "apsis_drift/godot/contact_surface.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kTerrainImpactVersion{1};
inline constexpr std::uint32_t kTerrainImpactColliderVersion{1};
inline constexpr std::uint32_t kTerrainImpactMaximumCells{128};
inline constexpr double kTerrainImpactClearMarginMetres{0.05};
inline constexpr double kTerrainImpactResponseRetreatMetres{0.002};

enum class CraftImpactRegion : std::uint8_t {
  forebody = 1,
  landing_structure = 2
};
enum class TerrainImpactClass : std::uint8_t {
  clear = 1,
  unknown = 2,
  impact = 3,
  near = 4
};
struct TerrainImpactWitness {
  std::uint32_t version{kTerrainImpactVersion}, terrain_policy{},
      collider_policy{kTerrainImpactColliderVersion};
  StarterCraftId craft;
  CraftFrameRecipe craft_frame;
  SystemId system;
  PlanetId planet;
  SimulationTick tick{};
  std::uint64_t source_checksum{}, candidate_checksum{};
  double fraction{};
  godot_spike::ContactTriangleId triangle;
  CraftImpactRegion region{};
  std::uint8_t support_index{};
  // Craft authoring origin, not COM. Body axes are right/up/back.
  RigidVector3 contact_point_craft_metres;
  RigidVector3 outward_normal_planet_fixed;
  RigidVector3 pre_response_velocity_planet_fixed;
  double closing_speed_metres_per_second{};
  friend auto operator==(const TerrainImpactWitness&,
                         const TerrainImpactWitness&) -> bool = default;
};
struct TerrainImpactSweep {
  std::uint32_t version{kTerrainImpactVersion};
  TerrainImpactClass classification{TerrainImpactClass::unknown};
  // Clear queries certify this fixed margin through a separately expanded
  // collider sweep. Unknown and contact results carry canonical zero here.
  double minimum_clearance_metres{};
  std::optional<TerrainImpactWitness> witness;
  friend auto operator==(const TerrainImpactSweep&, const TerrainImpactSweep&)
      -> bool = default;
};

// Explicit prototype: registered hull box plus deployed support stroke boxes;
// freeze the start planet-fixed orientation and translate linearly between
// actual own-tick COM endpoints. This is not exact visual-hull geometry.
// Inputs/ownership are validated before source access. Unknown coverage,
// excessive motion/rotation, seams or unsupported material do not confer an
// impact or loss. Actual initial overlap can report contact with closing zero
// while separating; it never traps a lifting craft through a forced response.
[[nodiscard]] auto assess_terrain_impact(
    const PhysicalLocalSystem&, const PhysicalPlanetRotationRecipe&,
    const FreedomFlightSaveDocument& before,
    const FreedomFlightSaveDocument& candidate, bool gear_deployed,
    std::uint32_t terrain_policy)
    -> std::expected<TerrainImpactSweep, std::string>;

// Caller applies only an inward witnessed contact. Retain the fixed start
// orientation at the witnessed COM fraction, retreat 2mm along the geometric
// terrain normal, remove inward relative linear velocity, preserve tangent,
// stop relative spin, and reframe at the candidate's authoritative tick.
[[nodiscard]] auto respond_terrain_impact(
    const PhysicalLocalSystem&, const PhysicalPlanetRotationRecipe&,
    const FreedomFlightSaveDocument& before,
    const FreedomFlightSaveDocument& candidate, const TerrainImpactWitness&)
    -> std::expected<RigidBodyState, std::string>;
} // namespace apsis_drift
