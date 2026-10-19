#include "apsis_drift/moon_surface_walker.hpp"

#include "surface_walk_internal.hpp"

namespace apsis_drift {
namespace {
using V = RigidVector3;
constexpr const char* owner_error{"Invalid moon-fixed walker or body owner"};
} // namespace
auto MoonSurfaceWalkTerrain::create(const PhysicalMoonDescriptor& owner,
                                    const MoonLandedCraftAnchor& anchor,
                                    SimulationTick tick)
    -> std::expected<MoonSurfaceWalkTerrain, std::string> {
  if (!validate_moon_landed_craft(owner, anchor, tick))
    return std::unexpected{"Moon walking requires a valid supported landing"};
  if (anchor.fixed.craft !=
      CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion})
    return std::unexpected{"Surface walking requires the registered Wayfarer"};
  const auto terrain =
      MoonTerrainSampler::create(owner, anchor.terrain_version);
  if (!terrain)
    return std::unexpected{"Moon walking terrain owner unavailable"};
  return MoonSurfaceWalkTerrain{*terrain, anchor};
}
auto MoonSurfaceWalkTerrain::ground(V direction)
    -> std::expected<V, PlanetSurfaceWalkStatus> {
  using namespace surface_walk_detail;
  if (!finite(direction) || length(direction) <= 1)
    return std::unexpected{PlanetSurfaceWalkStatus::terrain_unresolved};
  const auto hit = query_moon_contact_surface(
      terrain_, {direction.x, direction.y, direction.z});
  if (!hit) return std::unexpected{PlanetSurfaceWalkStatus::terrain_unresolved};
  const auto& n = hit->triangle.outward_normal;
  const auto alignment = dot({n.x, n.y, n.z}, unit(direction));
  if (!std::isfinite(alignment) || alignment < .8191520442889918)
    return std::unexpected{PlanetSurfaceWalkStatus::slope};
  const auto& p = hit->position;
  if (!finite({p.x, p.y, p.z}) || !std::isfinite(hit->radial_distance_metres) ||
      hit->radial_distance_metres <= 1 || hit->radial_distance_metres > 1e8)
    return std::unexpected{PlanetSurfaceWalkStatus::terrain_unresolved};
  // The authenticated moon recipe is dry. Negative relief remains solid rock;
  // no planet sea-datum classifier or scalar-height substitute is consulted.
  return V{p.x, p.y, p.z};
}
auto MoonSurfaceWalkTerrain::entry()
    -> std::expected<PlanetSurfaceWalkerState, std::string> {
  return surface_walk_detail::entry(anchor_.fixed,
                                    [this](V v) { return ground(v); });
}
auto MoonSurfaceWalkTerrain::validate(const PlanetSurfaceWalkerState& actor)
    -> std::expected<void, std::string> {
  return surface_walk_detail::validate(
      anchor_.fixed, actor, [this](V v) { return ground(v); }, owner_error);
}
auto MoonSurfaceWalkTerrain::advance(const PlanetSurfaceWalkerState& actor,
                                     const OriginWalkControls& controls,
                                     SimulationSeconds step)
    -> std::expected<PlanetSurfaceWalkStep, std::string> {
  return surface_walk_detail::advance(
      anchor_.fixed, actor, controls, step, [this](V v) { return ground(v); },
      owner_error);
}
auto MoonSurfaceWalkTerrain::return_access(
    const PlanetSurfaceWalkerState& actor)
    -> std::expected<PlanetSurfaceReturnAccess, std::string> {
  return surface_walk_detail::return_access(
      anchor_.fixed, actor, [this](V v) { return ground(v); }, owner_error);
}
auto MoonSurfaceWalkTerrain::near_entry(const PlanetSurfaceWalkerState& actor)
    -> std::expected<bool, std::string> {
  const auto access = return_access(actor);
  if (!access) return std::unexpected{access.error()};
  return access->within_reach;
}
} // namespace apsis_drift
