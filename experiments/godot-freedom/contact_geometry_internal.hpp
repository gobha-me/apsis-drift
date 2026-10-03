#pragma once

#include "contact_patch.hpp"

namespace apsis_drift::godot_spike::detail {
// Selected descriptors/states are validated by each owning entry point. These
// shared arithmetic seams do not validate a catalog or confer support
// authority.
[[nodiscard]] auto locate_selected_contact_triangle(const PlanetDescriptor&,
                                                    PlanetFixedDirection)
    -> std::expected<ContactTriangleId, ContactSurfaceError>;
[[nodiscard]] auto build_selected_contact_triangle(const PlanetDescriptor&,
                                                   ContactSurfaceRecipe,
                                                   ContactTriangleId,
                                                   TerrainTileCache&)
    -> std::expected<ContactTriangle, ContactSurfaceError>;

struct NominalContactRectangle {
  PlanetFixedPositionMetres center, width, depth, up;
};
[[nodiscard]] auto nominal_contact_rectangle(const PlanetDescriptor&,
                                             const RigidBodyState&,
                                             const CraftFrameProperties&,
                                             unsigned support_index)
    -> std::expected<NominalContactRectangle, ContactPatchError>;
[[nodiscard]] auto certify_contact_rectangle(const RigidBodyState&,
                                             unsigned support_index,
                                             const NominalContactRectangle&,
                                             const ContactTriangle&)
    -> std::expected<ContactPatch, ContactPatchError>;
} // namespace apsis_drift::godot_spike::detail
