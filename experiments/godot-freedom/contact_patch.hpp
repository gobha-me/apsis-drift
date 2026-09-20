#pragma once

#include "apsis_drift/rigid_body.hpp"
#include "contact_surface.hpp"

#include <array>
#include <cstdint>
#include <expected>

namespace apsis_drift::godot_spike {

inline constexpr std::uint32_t kExperimentalContactPatchVersion{1};
// Strict inset for containment, also an outward allowance on plane-gap bounds.
// This is a numerical policy, NOT a touchdown capture/compression tolerance.
inline constexpr double kContactPatchAllowanceMetres{0.0001};
inline constexpr double kContactPatchMaximumDatumAltitudeMetres{100000.0};

struct ContactPatch {
  std::uint32_t version{kExperimentalContactPatchVersion};
  CraftFrameRecipe craft;
  RigidCoordinateFrame frame;
  SimulationTick tick{};
  std::uint8_t support_index{};
  ContactTriangle triangle;
  PlanetFixedPositionMetres pad_center;
  // Registered body +X and +Z half-extent vectors, rotated into planet frame.
  PlanetFixedDirection half_width, half_length;
  // Lower/upper enclosures on the WHOLE rectangle's signed plane gaps.
  // Expanded outwards by kContactPatchAllowanceMetres, not point samples.
  double minimum_gap_metres{}, maximum_gap_metres{};
  // Raw minimum distance of the projected rectangle to each inward edge plane.
  // All three must be strictly greater than kContactPatchAllowanceMetres.
  std::array<double, 3> minimum_edge_distances_metres{};
  friend auto operator==(const ContactPatch&, const ContactPatch&)
      -> bool = default;
};

enum class ContactPatchError : std::uint8_t {
  unsupported_recipe,
  invalid_state,
  unsupported_frame,
  invalid_support,
  outside_query_bounds,
  surface_query_failed,
  ill_conditioned_geometry,
  degenerate_projection,
  boundary_not_certified,
};

// Nominal uncompressed pad geometry ONLY, independent of current gear state.
// Requires registered craft/support and canonical planet_fixed state/owner.
// The pad center must lie within 100km of the reference sphere. Three terrain
// vertices bound work; no adjacent-triangle search or renderer input.
//
// Certifies that normal projection of the entire rectangular pad lies strictly
// inside ONE triangle. Each triangle edge is an affine half-plane; its minimum
// over a rectangle is analytic. This is not a center/corner sampling heuristic.
// Boundary crossings, near-edge uncertainty and collapsed projection refuse.
// Inverted but noncollapsed rectangles may pass GEOMETRY, not touchdown.
//
// NOT material/bearing support, nearest/first surface along normal rays,
// obstruction/hull/swept clearance, slope suitability, a contact/landed state,
// or permission to set TouchdownPadObservation::footprint_supported=true.
// Malformed input is rejected before terrain-cache access. Valid but uncovered
// rectangles return boundary_not_certified, not a verdict of unsafe terrain.
[[nodiscard]] auto certify_contact_patch(const RigidBodyWorldContext&,
                                         const RigidBodyState&,
                                         unsigned support_index,
                                         ContactSurfaceRecipe,
                                         TerrainTileCache&)
    -> std::expected<ContactPatch, ContactPatchError>;

} // namespace apsis_drift::godot_spike
