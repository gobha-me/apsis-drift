#pragma once

#include "apsis_drift/origin_boarding_initial_material.hpp"
#include "origin_boarding_source_endpoint_surface_checkpoint_internal.hpp"
#include <array>
#include <span>

namespace apsis_drift::detail {
struct BoardingInitialMaterialLimits {
  std::size_t source_bytes{kBoardingInitialMaterialMaximumSourceBytes};
  std::uint64_t ring_inclusions{1680}, support_plane_inclusions{2304},
      envelope_pairs{26265}, enclosure_pairs{240}, triangle_visits{349087},
      boundary_pairs{5236305}, axes{83780880};
  std::size_t pair_axes{16};
};
struct MaterialQuantizedPoint {
  std::array<std::int64_t, 3> value{};
};
struct MaterialTriangle {
  std::array<std::uint32_t, 3> vertices{};
};
struct MaterialSourceRecord {
  std::string_view name, source_type, parent, raw_fingerprint,
      quantized_fingerprint;
  BoardingPlantedLegPointBounds raw_bounds, quantized_bounds;
  std::int32_t original_object{-1}, original_group{-1}, motion_group{-1},
      mesh{-1};
  std::uint32_t original_start{}, original_count{};
  BoardingInitialMaterialRelation relation{};
  bool removed{}, absent_crop{}, replacement{};
};
struct MaterialMeshRecord {
  std::uint32_t source{};
  std::span<const RigidVector3> raw_vertices;
  std::span<const MaterialQuantizedPoint> quantized_vertices;
  std::span<const MaterialTriangle> triangles;
};
struct MaterialCurveRecord {
  std::uint32_t source{}, mesh{}, point_count{};
  std::array<RigidVector3, 6> local_points{};
  OperatingTransform corrected_world;
  double radius_metres{};
};
struct MaterialSupportRecord {
  std::uint32_t source{}, mesh{};
  std::array<RigidVector3, 8> local_vertices{};
  std::array<std::array<std::uint8_t, 4>, 6> polygons{};
  OperatingTransform corrected_world;
  double bevel_width{};
};
struct MaterialMovingRecord {
  std::uint32_t source{};
  BoardingPlantedLegPointBounds raw_bounds, quantized_bounds;
  OperatingTransform authoring_delta;
};
struct MaterialPreparedView {
  std::span<const MaterialSourceRecord> sources;
  std::span<const MaterialMeshRecord> meshes;
  std::span<const MaterialCurveRecord> curves;
  std::span<const MaterialSupportRecord> supports;
  std::span<const MaterialMovingRecord> moving;
  std::size_t storage_bytes{};
  std::string_view completion_sha256, binary_sha256;
};
// Only the pinned preparation tool's generated TU defines this immutable view.
[[nodiscard]] auto origin_boarding_initial_material_prepared()
    -> MaterialPreparedView;
struct BoardingInitialMaterialAccess {
  static auto data(const OriginBoardingInitialMaterial&)
      -> const OriginBoardingInitialMaterial::Data*;
  static auto make(const NativeCraftBinding&, BoardingInitialMaterialLimits)
      -> std::expected<OriginBoardingInitialMaterial, std::string>;
};
// Diagnostic read-only view of the already authenticated retained enclosure.
// No caller geometry, pose or admission override enters this accessor.
[[nodiscard]] auto initial_material_source_envelope(
    const OriginBoardingInitialMaterial&, std::size_t)
    -> std::optional<BoardingPlantedLegPointBounds>;
[[nodiscard]] auto boarding_initial_material_bounded(
    const OriginBoardingBootSupport&, const OriginBoardingInitialMaterial&,
    BoardingInitialMaterialLimits = {})
    -> std::expected<BoardingInitialMaterialDiagnostic, std::string>;
struct MaterialPlane {
  RigidVector3 normal;
  BoardingFootSiteScalarBounds maximum;
};
struct MaterialEnclosureMathEvidence {
  bool arithmetic_supported{}, certified{};
  std::uint64_t axes_examined{};
  static constexpr bool source_qualified{false}, material_qualified{false},
      actor_qualified{false};
};
struct MaterialConstructorMathEvidence {
  bool arithmetic_supported{}, certified{};
  std::uint64_t inclusion_predicates{};
  static constexpr bool source_qualified{false}, material_qualified{false},
      actor_qualified{false};
};
[[nodiscard]] auto initial_material_curve_constructor_math(
    const MaterialSourceRecord&, const MaterialCurveRecord&,
    const MaterialMeshRecord&, std::uint64_t max_inclusions = 1680)
    -> std::expected<MaterialConstructorMathEvidence, std::string>;
[[nodiscard]] auto initial_material_support_constructor_math(
    const MaterialSourceRecord&, const MaterialSupportRecord&,
    const MaterialMeshRecord&, std::uint64_t max_inclusions = 2304)
    -> std::expected<MaterialConstructorMathEvidence, std::string>;
[[nodiscard]] auto initial_material_envelope_math(
    const BoardingSourceEndpointSurfaceCheckpointSolidBounds&, double common_y,
    BoardingPlantedLegPointBounds)
    -> std::expected<MaterialEnclosureMathEvidence, std::string>;
[[nodiscard]] auto initial_material_capsule_math(
    const BoardingSourceEndpointSurfaceCheckpointSolidBounds&, double common_y,
    BoardingPlantedLegPointBounds first, BoardingPlantedLegPointBounds second,
    double radius, std::size_t max_axes = 16)
    -> std::expected<MaterialEnclosureMathEvidence, std::string>;
[[nodiscard]] auto initial_material_primitive_math(
    const BoardingSourceEndpointSurfaceCheckpointSolidBounds&, double common_y,
    std::span<const MaterialPlane>, std::size_t max_axes = 16)
    -> std::expected<MaterialEnclosureMathEvidence, std::string>;
} // namespace apsis_drift::detail

// Narrow immutable-base binding check for the named checkpoint extension
// issuer.
namespace apsis_drift::detail {
[[nodiscard]] auto initial_material_extension_binding_matches(
    const NativeCraftBinding&, const OriginBoardingInitialMaterial&) -> bool;
} // namespace apsis_drift::detail
