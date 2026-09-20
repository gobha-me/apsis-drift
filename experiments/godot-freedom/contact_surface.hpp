#pragma once

#include "apsis_drift/terrain_tiles.hpp"

#include <array>
#include <cstdint>
#include <expected>

namespace apsis_drift::godot_spike {

// An opt-in geometric experiment, NOT a production terrain/save recipe. This
// freezes the native finest TOP triangulation, excluding skirts and shading.
struct ContactSurfaceRecipe {
  std::uint32_t version{1}, terrain_generator{1}, source_lod{8}, relief{1},
      mesh_lod{13}, intervals{32}, diagonal{1};
  friend auto operator==(const ContactSurfaceRecipe&,
                         const ContactSurfaceRecipe&) -> bool = default;
};
inline constexpr ContactSurfaceRecipe kExperimentalContactSurface{};

struct ContactTriangleId {
  PlanetId planet;
  CubeFace face{};
  std::uint32_t tile_x{}, tile_y{}, cell_x{}, cell_y{}, half{};
  friend auto operator==(const ContactTriangleId&, const ContactTriangleId&)
      -> bool = default;
};

struct ContactTriangle {
  ContactSurfaceRecipe recipe;
  ContactTriangleId id;
  // Canonical grid order: half 0 = (00,10,01), half 1 = (10,11,01).
  // Winding is NOT Godot front-face winding. normal is explicitly outward.
  std::array<PlanetFixedPositionMetres, 3> vertices;
  PlanetFixedDirection outward_normal;
  friend auto operator==(const ContactTriangle&, const ContactTriangle&)
      -> bool = default;
};

struct ContactSurfacePoint {
  ContactTriangle triangle;
  PlanetFixedPositionMetres position;
  double radial_distance_metres{};
  // True Cartesian triangle coordinates, not bilinear height interpolation.
  // Roundoff at edges can place a weight at most 1e-7 outside [0,1]. No
  // clamping/moving the ray hit is performed to conceal a failed query.
  std::array<double, 3> barycentric{};
  friend auto operator==(const ContactSurfacePoint&, const ContactSurfacePoint&)
      -> bool = default;
};

enum class ContactSurfaceError : std::uint8_t {
  unsupported_recipe,
  invalid_planet,
  invalid_direction,
  wrong_planet,
  invalid_triangle,
  coordinate_failure,
  sampling_failure,
  unsafe_geometry,
};

// Requires an unchanged generated descriptor. Direction need not be unit, but
// every component must be finite, largest magnitude in [1e-12,1e9]. Exact cube
// ties use existing X-before-Y-before-Z ownership; internal grid edges belong
// to the positive-side cell/tile; the face upper edge belongs to its last cell.
// Within a cell, u+v<=1 selects half 0; half 1 otherwise. These are binary64
// input rules, not an epsilon band or a promise that rounded aliases agree.
[[nodiscard]] auto locate_contact_triangle(const PlanetDescriptor&,
                                           ContactSurfaceRecipe,
                                           PlanetFixedDirection)
    -> std::expected<ContactTriangleId, ContactSurfaceError>;

// Three vertex samples, fixed work, no unbounded neighborhood search. The
// caller-owned source cache may be populated/evicted but cannot affect output.
// Identity is the pair (recipe,id), including planet and all topology fields.
[[nodiscard]] auto build_contact_triangle(const PlanetDescriptor&,
                                          ContactSurfaceRecipe,
                                          ContactTriangleId, TerrainTileCache&)
    -> std::expected<ContactTriangle, ContactSurfaceError>;

// Radial ray from the planet center, not an arbitrary ray or swept collision.
// No material/bearing/whole-pad support, hull clearance, contact event, landed
// transition, velocity response, damage, or live floor-guard changes.
[[nodiscard]] auto query_contact_surface(const PlanetDescriptor&,
                                         ContactSurfaceRecipe,
                                         PlanetFixedDirection,
                                         TerrainTileCache&)
    -> std::expected<ContactSurfacePoint, ContactSurfaceError>;

} // namespace apsis_drift::godot_spike
