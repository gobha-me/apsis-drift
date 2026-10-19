#pragma once

#include "apsis_drift/godot/contact_patch.hpp"
#include "apsis_drift/moon_terrain.hpp"
#include "apsis_drift/terrain_hull_clearance.hpp"
#include "apsis_drift/terrain_support.hpp"
#include "apsis_drift/touchdown.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kMoonSurfaceContactVersion{1};
struct MoonContactTriangleId {
  BodyId body;
  CubeFace face{};
  std::uint32_t tile_x{}, tile_y{}, cell_x{}, cell_y{}, half{};
  friend auto operator==(const MoonContactTriangleId&,
                         const MoonContactTriangleId&) -> bool = default;
};
struct MoonContactTriangle {
  std::uint32_t version{kMoonSurfaceContactVersion};
  MoonContactTriangleId id;
  std::array<MoonFixedPositionMetres, 3> vertices;
  MoonFixedDirection outward_normal;
  friend auto operator==(const MoonContactTriangle&, const MoonContactTriangle&)
      -> bool = default;
};
struct MoonContactSurfacePoint {
  MoonContactTriangle triangle;
  MoonFixedPositionMetres position;
  double radial_distance_metres{};
  std::array<double, 3> barycentric{};
};
enum class MoonContactError : std::uint8_t {
  unsupported_version,
  invalid_owner,
  invalid_direction,
  invalid_triangle,
  sampling_failure,
  unsafe_geometry,
  invalid_state,
  unsupported_frame,
  invalid_craft,
  reframe_failed
};
// Fixed actual top topology: LOD13,32 intervals, halves(00,10,01)/(10,11,01).
// The same sampler supplies mesh vertices, pads, hull and walking. No skirts,
// sea clamp, independent bilinear-height collision or PlanetId carrier.
[[nodiscard]] auto locate_moon_contact_triangle(const MoonTerrainSampler&,
                                                MoonFixedDirection)
    -> std::expected<MoonContactTriangleId, MoonContactError>;
[[nodiscard]] auto build_moon_contact_triangle(const MoonTerrainSampler&,
                                               MoonContactTriangleId)
    -> std::expected<MoonContactTriangle, MoonContactError>;
[[nodiscard]] auto query_moon_contact_surface(const MoonTerrainSampler&,
                                              MoonFixedDirection)
    -> std::expected<MoonContactSurfacePoint, MoonContactError>;
struct MoonContactPatch {
  std::uint32_t version{kMoonSurfaceContactVersion};
  CraftFrameRecipe craft;
  RigidCoordinateFrame frame;
  SimulationTick tick{};
  std::uint8_t support_index{};
  MoonContactTriangle triangle;
  MoonFixedPositionMetres pad_center;
  MoonFixedDirection half_width, half_length;
  double minimum_gap_metres{}, maximum_gap_metres{};
  std::array<double, 3> minimum_edge_distances_metres{};
};
struct MoonContactPadOutcome {
  std::uint8_t support_index{};
  std::optional<MoonContactTriangle> triangle;
  std::optional<std::expected<MoonContactPatch, godot_spike::ContactPatchError>>
      geometry;
  std::optional<std::expected<TouchdownPadObservation, TerrainSupportError>>
      support;
};
struct MoonContactProvenance {
  PhysicalMoonDescriptor owner;
  std::uint32_t terrain_version{kMoonTerrainRecipeVersion};
  RigidBodyState original_state, fixed_query_state;
  std::uint64_t original_state_checksum{};
};
struct MoonContactResult {
  MoonContactProvenance provenance;
  std::uint8_t support_count{};
  std::array<MoonContactPadOutcome, 4> supports;
  std::optional<TouchdownAssessment> assessment;
  std::optional<TouchdownError> envelope_error;
};
// Authenticates and reframes once, without terrain access. Bounds/unknowns and
// the existing strict pad/envelope policies remain; dry negative relief is
// genuine rock because this authenticated moon recipe has no water.
class MoonContactSnapshot {
 public:
  [[nodiscard]] static auto create(
      const PhysicalMoonDescriptor&, const RigidBodyState&, bool gear_deployed,
      std::uint32_t terrain_version = kMoonTerrainRecipeVersion)
      -> std::expected<MoonContactSnapshot, MoonContactError>;
  [[nodiscard]] auto query() const -> MoonContactResult;
  [[nodiscard]] auto hull_clearance() const
      -> std::expected<TerrainHullClearance, TerrainHullError>;
  [[nodiscard]] auto provenance() const noexcept
      -> const MoonContactProvenance& {
    return provenance_;
  }

 private:
  MoonContactSnapshot(MoonContactProvenance provenance,
                      MoonTerrainSampler sampler, bool gear)
      : provenance_(std::move(provenance)), sampler_(std::move(sampler)),
        gear_(gear) {}
  MoonContactProvenance provenance_;
  MoonTerrainSampler sampler_;
  bool gear_{};
};
} // namespace apsis_drift
