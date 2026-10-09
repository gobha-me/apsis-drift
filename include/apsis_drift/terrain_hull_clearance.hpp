#pragma once

#include "apsis_drift/planet_rotation.hpp"
#include "apsis_drift/terrain_tiles.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kTerrainHullClearanceVersion{1};
inline constexpr std::uint32_t kMaximumHullClearanceTriangles{128};

enum class TerrainHullError : std::uint8_t {
  invalid_source,
  invalid_cache_capacity,
  outside_query_bounds,
  face_boundary,
  cell_budget_exceeded,
  surface_query_failed,
  unsafe_arithmetic
};
struct TerrainHullClearance {
  std::uint32_t version{kTerrainHullClearanceVersion};
  std::uint64_t source_checksum{};
  SimulationTick tick{};
  CubeFace face{};
  std::array<std::uint32_t, 2> minimum_cell{}, maximum_cell{};
  std::uint32_t triangle_count{};
  // A LOWER bound using the highest terrain vertex over a conservatively
  // enclosing region. A nonpositive value does not prove an actual collision.
  double lower_clearance_metres{};
  bool certified{};
  friend auto operator==(const TerrainHullClearance&,
                         const TerrainHullClearance&) -> bool = default;
};

// Read-only registered hull-box clearance for the same finest top mesh as
// terrain support policy1. Validates/reframes the real owned C++ source, bounds
// all eight hull corners in one cube face, then samples at most128 triangles.
// Cube coordinates are linear-fractional with a positive denominator; extrema
// over the convex box occur at corners. One-cell halo encloses rounding.
// Every triangle is affine: its maximum body-Y is bounded by its vertices.
// A positive lower bound certifies all this terrain below the whole hull box.
// Crossing/uncertain faces, excessive coverage and malformed input refuse.
// Conservative failure is unknown clearance, not an impact or pose adjustment.
// No pad/bearing, swept-path, gear, landed state or flight mutation is implied.
[[nodiscard]] auto assess_terrain_hull_clearance(
    const PhysicalLocalSystem&, const PhysicalPlanetRotationRecipe&,
    const RigidBodyState&,
    std::size_t cache_capacity = kDefaultTerrainTileCacheCapacity)
    -> std::expected<TerrainHullClearance, TerrainHullError>;
} // namespace apsis_drift
