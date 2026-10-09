#pragma once

#include "apsis_drift/planet_rotation.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kLandedCraftAnchorVersion{1};
struct LandedCraftAnchor {
  std::uint32_t version{kLandedCraftAnchorVersion};
  std::uint32_t terrain_policy{1}, rotation_generator{1}, rotation_owner{1};
  // Exact supported local position/orientation at landing tick. Relative
  // linear/angular velocity is canonical positive zero. This anchor never
  // changes during idle; current flight pose is derived at the current clock.
  RigidBodyState fixed;
  friend auto operator==(const LandedCraftAnchor&, const LandedCraftAnchor&)
      -> bool = default;
};
enum class LandedCraftError : std::uint8_t {
  invalid_source,
  stale_source,
  gear_not_deployed,
  contact_unknown,
  contact_not_ready,
  hull_not_certified,
  invalid_anchor,
  unsupported_version,
  stale_tick,
  frame_resolution_failed,
  insufficient_liftoff_authority
};

// Commit preparation, not flight clamping: from actual ready pads and a
// certified hull, retain position/orientation and dissipate the bounded safe
// contact velocities in the fixed surface frame. A stale source checksum
// refuses before sampling. Caller commits the anchor and derived flight
// together transactionally; no resource/history awards are implied.
[[nodiscard]] auto prepare_landed_craft(const PhysicalLocalSystem&,
                                        const PhysicalPlanetRotationRecipe&,
                                        const RigidBodyState&,
                                        bool gear_deployed,
                                        std::uint64_t expected_source_checksum)
    -> std::expected<LandedCraftAnchor, LandedCraftError>;

// Pure pose/velocity resolution at an authoritative tick. No integration of
// idle drift, renderer time or accumulated thrust. Validates the owned fixed
// anchor and applies existing rotation/frame conversion exactly once.
[[nodiscard]] auto resolve_landed_craft(const PhysicalLocalSystem&,
                                        const PhysicalPlanetRotationRecipe&,
                                        const LandedCraftAnchor&,
                                        SimulationTick)
    -> std::expected<RigidBodyState, LandedCraftError>;

// Full support/hull revalidation for save hydration and liftoff. Nominal
// structural gravity/pressure ratings alone do not promise available thrust.
[[nodiscard]] auto validate_landed_craft(const PhysicalLocalSystem&,
                                         const PhysicalPlanetRotationRecipe&,
                                         const LandedCraftAnchor&,
                                         SimulationTick)
    -> std::expected<void, LandedCraftError>;
[[nodiscard]] auto release_landed_craft(const PhysicalLocalSystem&,
                                        const PhysicalPlanetRotationRecipe&,
                                        const LandedCraftAnchor&,
                                        SimulationTick)
    -> std::expected<RigidBodyState, LandedCraftError>;
} // namespace apsis_drift
