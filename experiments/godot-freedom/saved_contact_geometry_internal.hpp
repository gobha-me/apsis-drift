#pragma once

#include "saved_contact_geometry.hpp"

namespace apsis_drift::godot_spike::detail {
// Factory preparation seam for physical ownership/invalid-input tests. It
// neither samples terrain nor constructs a legacy-owned contact result.
[[nodiscard]] auto prepare_saved_contact(const PhysicalLocalSystem&,
                                         const PhysicalPlanetRotationRecipe&,
                                         const RigidBodyState&,
                                         ContactSurfaceRecipe)
    -> std::expected<SavedContactProvenance, SavedContactError>;
[[nodiscard]] auto query_saved_contact(const SavedContactProvenance&,
                                       TerrainTileCache&)
    -> SavedContactGeometryBatch;
} // namespace apsis_drift::godot_spike::detail
