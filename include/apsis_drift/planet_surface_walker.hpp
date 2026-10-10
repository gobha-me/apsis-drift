#pragma once

#include "apsis_drift/landed_craft.hpp"
#include "apsis_drift/origin_walker.hpp"
#include "apsis_drift/terrain_tiles.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kPlanetSurfaceWalkerVersion{1};
// Ordinary supported surface kinematics; neither space EVA nor a suit timer.
// The voyage owns time. Positions and velocities are relative to planet-fixed
// terrain, never the station frame or a renderer floating origin.
struct PlanetSurfaceWalkerState {
  std::uint32_t version{kPlanetSurfaceWalkerVersion};
  std::uint64_t actor_id{1};
  RigidCoordinateFrame frame;
  RigidVector3 foot_position_metres, velocity_metres_per_second;
  double heading_radians{};
  friend auto operator==(const PlanetSurfaceWalkerState&,
                         const PlanetSurfaceWalkerState&) -> bool = default;
};
enum class PlanetSurfaceWalkStatus : std::uint8_t {
  supported,
  terrain_unresolved,
  water,
  slope,
  step,
  craft_obstruction
};
struct PlanetSurfaceWalkStep {
  PlanetSurfaceWalkerState actor;
  PlanetSurfaceWalkStatus status{};
};

// Owner-qualified terrain for one unchanged landing. Four source tiles are
// cached on demand; output is independent of cache residency. Creation fully
// validates actual support/hull/rotation, with no renderer or location search.
class PlanetSurfaceWalkTerrain {
 public:
  [[nodiscard]] static auto create(const PhysicalLocalSystem&,
                                   const PhysicalPlanetRotationRecipe&,
                                   const LandedCraftAnchor&, SimulationTick)
      -> std::expected<PlanetSurfaceWalkTerrain, std::string>;
  [[nodiscard]] auto entry()
      -> std::expected<PlanetSurfaceWalkerState, std::string>;
  [[nodiscard]] auto validate(const PlanetSurfaceWalkerState&)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto advance(const PlanetSurfaceWalkerState&,
                             const OriginWalkControls&,
                             SimulationSeconds = kSimulationStep)
      -> std::expected<PlanetSurfaceWalkStep, std::string>;
  [[nodiscard]] auto near_entry(const PlanetSurfaceWalkerState&)
      -> std::expected<bool, std::string>;
  [[nodiscard]] auto cache_size() const -> std::size_t { return cache_.size(); }
  [[nodiscard]] auto anchor() const -> const LandedCraftAnchor& {
    return anchor_;
  }

 private:
  PlanetSurfaceWalkTerrain(PlanetDescriptor planet, LandedCraftAnchor anchor,
                           TerrainTileCache cache)
      : planet_(std::move(planet)), anchor_(anchor), cache_(std::move(cache)) {}
  [[nodiscard]] auto ground(RigidVector3 direction)
      -> std::expected<RigidVector3, PlanetSurfaceWalkStatus>;
  [[nodiscard]] auto blocked(RigidVector3 from, RigidVector3 to) const -> bool;
  PlanetDescriptor planet_;
  LandedCraftAnchor anchor_;
  TerrainTileCache cache_;
};
} // namespace apsis_drift
