#include "apsis_drift/planet_surface_walker.hpp"
#include "apsis_drift/terrain_sea_surface.hpp"

#include "apsis_drift/terrain_touchdown.hpp"
#include "godot/contact_geometry_internal.hpp"
#include "surface_walk_internal.hpp"

namespace apsis_drift {
namespace {
using V = RigidVector3;
using surface_walk_detail::dot;
using surface_walk_detail::finite;
using surface_walk_detail::length;
using surface_walk_detail::scale;
using surface_walk_detail::unit;
} // namespace

auto PlanetSurfaceWalkTerrain::create(
    const PhysicalLocalSystem& owner,
    const PhysicalPlanetRotationRecipe& rotation,
    const LandedCraftAnchor& anchor, SimulationTick tick)
    -> std::expected<PlanetSurfaceWalkTerrain, std::string> {
  if (!validate_landed_craft(owner, rotation, anchor, tick))
    return std::unexpected{
        "Surface walking requires a valid supported landing"};
  if (anchor.fixed.craft !=
      CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion})
    return std::unexpected{"Surface walking requires the registered Wayfarer"};
  const auto p = find_local_system_planet(owner, *anchor.fixed.frame.planet);
  if (!p) return std::unexpected{"Surface walking body owner is unavailable"};
  auto cache = TerrainTileCache::create(4);
  if (!cache) return std::unexpected{"Surface walking cache unavailable"};
  return PlanetSurfaceWalkTerrain{owner, (*p)->descriptor, anchor,
                                  std::move(*cache)};
}

auto PlanetSurfaceWalkTerrain::ground(V direction)
    -> std::expected<V, PlanetSurfaceWalkStatus> {
  using namespace godot_spike;
  if (!finite(direction) || length(direction) <= 1)
    return std::unexpected{PlanetSurfaceWalkStatus::terrain_unresolved};
  const auto id = detail::locate_selected_contact_triangle(
      planet_, {direction.x, direction.y, direction.z});
  if (!id) return std::unexpected{PlanetSurfaceWalkStatus::terrain_unresolved};
  const auto recipe = saved_contact_recipe(anchor_.terrain_policy);
  if (!recipe)
    return std::unexpected{PlanetSurfaceWalkStatus::terrain_unresolved};
  const auto triangle = detail::build_selected_contact_triangle(
      owner_, planet_, *recipe, *id, cache_);
  if (!triangle)
    return std::unexpected{PlanetSurfaceWalkStatus::terrain_unresolved};
  if (*recipe == kSeaDatumSavedContactSurface) {
    const auto datum = classify_terrain_datum_mask(triangle->datum_mask);
    if (!datum)
      return std::unexpected{PlanetSurfaceWalkStatus::terrain_unresolved};
    if (*datum != TerrainDatumClass::dry)
      return std::unexpected{PlanetSurfaceWalkStatus::water};
  }
  const V n{triangle->outward_normal.x, triangle->outward_normal.y,
            triangle->outward_normal.z};
  const auto& first = triangle->vertices[0];
  const V a{first.x, first.y, first.z};
  const auto up = unit(direction);
  const auto alignment = dot(n, up);
  // Fictional supported-walking envelope, independent of ship touchdown slope.
  if (!std::isfinite(alignment) || alignment < .8191520442889918)
    return std::unexpected{PlanetSurfaceWalkStatus::slope};
  const auto radius = static_cast<double>(planet_.radius.value) * 1000;
  const auto radial = unit(a);
  for (auto vertex : triangle->vertices)
    if (dot(radial, {vertex.x, vertex.y, vertex.z}) <= radius + .0001)
      return std::unexpected{PlanetSurfaceWalkStatus::water};
  const auto distance = dot(n, a) / alignment;
  if (!std::isfinite(distance) || distance <= radius || distance > 1e8)
    return std::unexpected{PlanetSurfaceWalkStatus::terrain_unresolved};
  return scale(up, distance);
}

auto PlanetSurfaceWalkTerrain::blocked(V from, V to) const -> bool {
  return surface_walk_detail::blocked(anchor_.fixed, from, to);
}
auto PlanetSurfaceWalkTerrain::entry()
    -> std::expected<PlanetSurfaceWalkerState, std::string> {
  return surface_walk_detail::entry(anchor_.fixed,
                                    [this](V v) { return ground(v); });
}
auto PlanetSurfaceWalkTerrain::validate(const PlanetSurfaceWalkerState& s)
    -> std::expected<void, std::string> {
  return surface_walk_detail::validate(
      anchor_.fixed, s, [this](V v) { return ground(v); },
      "Invalid planet-fixed walker or body owner");
}
auto PlanetSurfaceWalkTerrain::advance(const PlanetSurfaceWalkerState& s,
                                       const OriginWalkControls& controls,
                                       SimulationSeconds step)
    -> std::expected<PlanetSurfaceWalkStep, std::string> {
  return surface_walk_detail::advance(
      anchor_.fixed, s, controls, step, [this](V v) { return ground(v); },
      "Invalid planet-fixed walker or body owner");
}
auto PlanetSurfaceWalkTerrain::near_entry(const PlanetSurfaceWalkerState& s)
    -> std::expected<bool, std::string> {
  const auto access = return_access(s);
  if (!access) return std::unexpected{access.error()};
  return access->within_reach;
}
auto PlanetSurfaceWalkTerrain::return_access(const PlanetSurfaceWalkerState& s)
    -> std::expected<PlanetSurfaceReturnAccess, std::string> {
  return surface_walk_detail::return_access(
      anchor_.fixed, s, [this](V v) { return ground(v); },
      "Invalid planet-fixed walker or body owner");
}
} // namespace apsis_drift
