#include "saved_contact_geometry.hpp"

#include "apsis_drift/rigid_frame_handoff.hpp"
#include "contact_geometry_internal.hpp"
#include "saved_contact_geometry_internal.hpp"

namespace apsis_drift::godot_spike {
namespace detail {
auto prepare_saved_contact(const PhysicalLocalSystem& owner,
                           const PhysicalPlanetRotationRecipe& rotation,
                           const RigidBodyState& source,
                           ContactSurfaceRecipe recipe)
    -> std::expected<SavedContactProvenance, SavedContactError> {
  if (recipe != kExperimentalSavedContactSurface)
    return std::unexpected{SavedContactError::unsupported_recipe};
  if (!validate_local_system(owner))
    return std::unexpected{SavedContactError::invalid_physical_owner};
  const RigidBodyWorldContext context{owner};
  if (!validate_rigid_body_state(context, source))
    return std::unexpected{SavedContactError::invalid_state};
  if (source.frame.kind != RigidFrameKind::planet_relative_inertial)
    return std::unexpected{SavedContactError::unsupported_frame};
  const auto planet = find_local_system_planet(owner, *source.frame.planet);
  if (!planet)
    return std::unexpected{SavedContactError::invalid_physical_owner};
  const auto craft = resolve_craft_frame(source.craft);
  if (!craft ||
      !supports_operation(craft->properties, CraftOperation::terrain_contact) ||
      craft->properties.support_count < 1 ||
      craft->properties.support_count > 4)
    return std::unexpected{SavedContactError::invalid_craft};
  if (!resolve_planet_rotation(owner, rotation, source.tick))
    return std::unexpected{SavedContactError::invalid_rotation};
  const RigidFrameHandoffRequest request{
      {RigidFrameKind::planet_fixed, owner.catalog.id, source.frame.planet, {}},
      source.tick};
  const auto fixed = reframe_rigid_body(context, source, request, rotation);
  if (!fixed) return std::unexpected{SavedContactError::reframe_failed};
  const auto checksum = rigid_body_state_checksum(context, source);
  if (!checksum) return std::unexpected{SavedContactError::invalid_state};
  return SavedContactProvenance{
      owner,    (*planet)->descriptor, rotation, recipe, source, *fixed,
      *checksum};
}
auto query_saved_contact(const SavedContactProvenance& provenance,
                         TerrainTileCache& cache) -> SavedContactGeometryBatch {
  SavedContactGeometryBatch result{provenance, 0, {}};
  // Only factory-prepared snapshots enter this private seam.
  const auto craft = resolve_craft_frame(provenance.fixed_query_state.craft);
  result.support_count = craft->properties.support_count;
  std::array<std::expected<NominalContactRectangle, ContactPatchError>, 4> pads;
  for (unsigned i = 0; i < result.support_count; ++i)
    pads[i] = nominal_contact_rectangle(
        provenance.planet, provenance.fixed_query_state, craft->properties, i);
  // Derive/check every rectangle before any source-cache access.
  for (unsigned i = 0; i < result.support_count; ++i) {
    auto& outcome = result.supports[i];
    outcome.support_index = static_cast<std::uint8_t>(i);
    if (!pads[i]) {
      outcome.geometry = std::unexpected{pads[i].error()};
      continue;
    }
    const auto& center = pads[i]->center;
    const auto id = locate_selected_contact_triangle(
        provenance.planet, {center.x, center.y, center.z});
    if (!id) {
      outcome.geometry =
          std::unexpected{ContactPatchError::surface_query_failed};
      continue;
    }
    const auto triangle = build_selected_contact_triangle(
        provenance.planet, provenance.recipe, *id, cache);
    if (!triangle) {
      outcome.geometry =
          std::unexpected{ContactPatchError::surface_query_failed};
      continue;
    }
    outcome.triangle = *triangle;
    outcome.geometry = certify_contact_rectangle(provenance.fixed_query_state,
                                                 i, *pads[i], *triangle);
  }
  return result;
}
} // namespace detail

auto SavedContactGeometry::create(const NativeFreedomFlightSession& session,
                                  ContactSurfaceRecipe recipe,
                                  std::size_t cache_capacity)
    -> std::expected<SavedContactGeometry, SavedContactError> {
  auto prepared = detail::prepare_saved_contact(
      session.system(), session.rotation(), session.document().flight, recipe);
  if (!prepared) return std::unexpected{prepared.error()};
  auto cache = TerrainTileCache::create(cache_capacity);
  if (!cache) return std::unexpected{SavedContactError::invalid_cache_capacity};
  return SavedContactGeometry{std::move(*prepared), std::move(*cache)};
}
auto SavedContactGeometry::query() -> SavedContactGeometryBatch {
  return detail::query_saved_contact(provenance_, cache_);
}
} // namespace apsis_drift::godot_spike
