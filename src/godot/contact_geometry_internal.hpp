#pragma once

#include "apsis_drift/godot/contact_patch.hpp"
#include "apsis_drift/physical_local_system.hpp"

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

// New local-detail selection must retain and authenticate complete ownership.
// The legacy overload cannot construct local-detail triangles.
[[nodiscard]] auto build_selected_contact_triangle(const PhysicalLocalSystem&,
                                                   const PlanetDescriptor&,
                                                   ContactSurfaceRecipe,
                                                   ContactTriangleId,
                                                   TerrainTileCache&)
    -> std::expected<ContactTriangle, ContactSurfaceError>;

// One owned radial first-surface query against the actual selected TOP facet.
// Includes sea-datum chord sag; not a scalar-height or support certificate.
[[nodiscard]] auto query_selected_contact_surface(const PhysicalLocalSystem&,
                                                  const PlanetDescriptor&,
                                                  ContactSurfaceRecipe,
                                                  PlanetFixedDirection,
                                                  TerrainTileCache&)
    -> std::expected<ContactSurfacePoint, ContactSurfaceError>;

struct NominalContactRectangle {
  PlanetFixedPositionMetres center, width, depth, up;
};
struct ContactRectangleCertificate {
  double minimum_gap_metres{}, maximum_gap_metres{};
  std::array<double, 3> minimum_edge_distances_metres{};
};
// Numeric-only seams; owning entry points authenticate body and frame first.
[[nodiscard]] auto nominal_contact_rectangle(double reference_radius_metres,
                                             const RigidBodyState&,
                                             const CraftFrameProperties&,
                                             unsigned support_index)
    -> std::expected<NominalContactRectangle, ContactPatchError>;
[[nodiscard]] auto certify_contact_rectangle_geometry(
    const NominalContactRectangle&,
    const std::array<PlanetFixedPositionMetres, 3>&,
    PlanetFixedDirection outward_normal)
    -> std::expected<ContactRectangleCertificate, ContactPatchError>;
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
