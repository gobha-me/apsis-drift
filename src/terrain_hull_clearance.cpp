#include "apsis_drift/terrain_hull_clearance.hpp"
#include "terrain_hull_clearance_internal.hpp"

#include "godot/contact_geometry_internal.hpp"
#include "godot/saved_contact_geometry_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace apsis_drift {
using namespace detail::hull_numeric;

auto assess_terrain_hull_clearance(const PhysicalLocalSystem& owner,
                                   const PhysicalPlanetRotationRecipe& rotation,
                                   const RigidBodyState& source,
                                   std::size_t cache_capacity,
                                   std::uint32_t terrain_policy)
    -> std::expected<TerrainHullClearance, TerrainHullError> {
  using namespace godot_spike;
  const auto recipe = saved_contact_recipe(terrain_policy);
  if (!recipe) return std::unexpected{TerrainHullError::invalid_source};
  const auto prepared = godot_spike::detail::prepare_saved_contact(
      owner, rotation, source, *recipe);
  if (!prepared) return std::unexpected{TerrainHullError::invalid_source};
  const auto& state = prepared->fixed_query_state;
  const auto craft = resolve_craft_frame(state.craft);
  if (!craft) return std::unexpected{TerrainHullError::invalid_source};
  const auto& properties = craft->properties;
  const auto radius = static_cast<double>(prepared->planet.radius.value) * 1000;
  const auto coverage = apsis_drift::detail::hull_numeric::bound_cells(
      state, properties, radius, prepared->original_state_checksum,
      terrain_policy, [&](RigidVector3 p) {
        return godot_spike::detail::locate_selected_contact_triangle(
            prepared->planet, {p.x, p.y, p.z});
      });
  if (!coverage) return std::unexpected{coverage.error()};
  auto result = *coverage;
  auto cache = TerrainTileCache::create(cache_capacity);
  if (!cache) return std::unexpected{TerrainHullError::invalid_cache_capacity};
  const RigidOrientation inverse{state.orientation.w, -state.orientation.x,
                                 -state.orientation.y, -state.orientation.z};
  double highest_y = -std::numeric_limits<double>::infinity();
  for (auto x = result.minimum_cell[0]; x <= result.maximum_cell[0]; ++x)
    for (auto y = result.minimum_cell[1]; y <= result.maximum_cell[1]; ++y)
      for (unsigned half = 0; half < 2; ++half) {
        const auto triangle =
            godot_spike::detail::build_selected_contact_triangle(
                owner, prepared->planet, *recipe,
                {prepared->planet.id, result.face, x / 32, y / 32, x % 32,
                 y % 32, half},
                *cache);
        if (!triangle)
          return std::unexpected{TerrainHullError::surface_query_failed};
        for (const auto p : triangle->vertices) {
          const auto relative =
              subtract({p.x, p.y, p.z}, state.position_metres);
          const auto body = rotate(inverse, relative);
          if (!finite(body))
            return std::unexpected{TerrainHullError::unsafe_arithmetic};
          highest_y = std::max(highest_y, body.y);
        }
      }
  const auto minimum_y =
      static_cast<double>(std::int64_t{properties.hull_min_mm[1]} -
                          properties.center_of_mass_mm[1]) /
      1000;
  result.lower_clearance_metres =
      minimum_y - highest_y - kContactPatchAllowanceMetres;
  if (!std::isfinite(result.lower_clearance_metres))
    return std::unexpected{TerrainHullError::unsafe_arithmetic};
  result.certified = result.lower_clearance_metres > 0;
  return result;
}
} // namespace apsis_drift
