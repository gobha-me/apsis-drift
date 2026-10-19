#pragma once

#include "apsis_drift/moon_landed_craft.hpp"
#include "apsis_drift/planet_surface_walker.hpp"

namespace apsis_drift {
// Actual supported moon terrain, not a PlanetDescriptor carrier. The historical
// PlanetSurfaceWalkerState/Step/ReturnAccess names describe shared scalar
// kinematics; moon actors retain their genuine body_fixed6/BodyId frame.
// The existing codec remains planet-only; this class adds no save admission.
class MoonSurfaceWalkTerrain {
 public:
  [[nodiscard]] static auto create(const PhysicalMoonDescriptor&,
                                   const MoonLandedCraftAnchor&, SimulationTick)
      -> std::expected<MoonSurfaceWalkTerrain, std::string>;
  [[nodiscard]] auto entry()
      -> std::expected<PlanetSurfaceWalkerState, std::string>;
  [[nodiscard]] auto validate(const PlanetSurfaceWalkerState&)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto advance(const PlanetSurfaceWalkerState&,
                             const OriginWalkControls&,
                             SimulationSeconds = kSimulationStep)
      -> std::expected<PlanetSurfaceWalkStep, std::string>;
  [[nodiscard]] auto return_access(const PlanetSurfaceWalkerState&)
      -> std::expected<PlanetSurfaceReturnAccess, std::string>;
  [[nodiscard]] auto near_entry(const PlanetSurfaceWalkerState&)
      -> std::expected<bool, std::string>;
  [[nodiscard]] auto anchor() const -> const MoonLandedCraftAnchor& {
    return anchor_;
  }

 private:
  MoonSurfaceWalkTerrain(MoonTerrainSampler terrain,
                         MoonLandedCraftAnchor anchor)
      : terrain_(std::move(terrain)), anchor_(anchor) {}
  [[nodiscard]] auto ground(RigidVector3)
      -> std::expected<RigidVector3, PlanetSurfaceWalkStatus>;
  MoonTerrainSampler terrain_;
  MoonLandedCraftAnchor anchor_;
};
} // namespace apsis_drift
