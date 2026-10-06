#pragma once
#include "apsis_drift/origin_boarding_checkpoint_material_extension.hpp"
#include "origin_boarding_initial_material_internal.hpp"
namespace apsis_drift::detail {
struct CheckpointMaterialExtensionPreparedView;
struct CheckpointMaterialExtensionFrameRecord;
enum class BoardingCheckpointMaterialExtensionRelation : std::uint8_t {
  none,
  frame_annulus,
  retained_cut_skin,
  closed_frame_boundary
};
enum class BoardingCheckpointMaterialExtensionCondition : std::uint8_t {
  none,
  invalid_binding,
  source_identity,
  source_capacity,
  unsupported_arithmetic,
  base_capacity,
  vertex_capacity,
  index_capacity,
  plane_capacity,
  base_geometry,
  raw_containment,
  quantized_containment,
  pair_capacity
};
struct BoardingCheckpointMaterialExtensionLimits {
  std::size_t source_bytes{
      kBoardingCheckpointMaterialExtensionMaximumSourceBytes};
  std::uint64_t base_guards{4096}, vertex_guards{20448}, index_guards{17097},
      plane_guards{147456};
  std::uint64_t adjacent_pair_attempts{8192};
};
struct BoardingCheckpointMaterialExtensionConstructorMath {
  BoardingCheckpointMaterialExtensionSummary work;
  BoardingCheckpointMaterialExtensionCondition condition{};
  std::optional<std::size_t> source, triangle, sector, vertex, plane;
  std::uint64_t adjacent_pair_attempts{};
  bool quantized{}, complete{};
  static constexpr bool source_qualified{false}, material_qualified{false},
      world_qualified{false}, body_qualified{false}, actor_qualified{false};
};
struct BoardingCheckpointMaterialExtensionPrismMath {
  std::array<MaterialPlane, 6> raw_planes, game_planes;
  bool arithmetic_supported{};
  static constexpr bool source_qualified{false}, material_qualified{false},
      world_qualified{false}, body_qualified{false}, actor_qualified{false};
};
enum class BoardingCheckpointMaterialBoundaryCondition : std::uint8_t {
  none,
  invalid_binding,
  source_capacity,
  input_validation,
  unsupported_arithmetic,
  bound_capacity,
  triangle_capacity,
  edge_capacity,
  edge_comparison_capacity,
  source_bounds,
  degenerate_triangle,
  closed_chain,
  vertex_capacity,
  index_capacity,
  invalid_limits
};
struct BoardingCheckpointMaterialBoundaryLimits {
  std::size_t source_bytes{
      kBoardingCheckpointMaterialExtensionMaximumSourceBytes};
  std::uint64_t base_guards{4096}, vertex_guards{20448}, index_guards{17097},
      bound_coordinate_guards{20448}, nondegenerate_triangles{512},
      edge_occurrences{1536}, edge_comparisons{2359296};
};
struct BoardingCheckpointMaterialBoundaryEvidence {
  BoardingCheckpointMaterialExtensionSummary work;
  BoardingCheckpointMaterialBoundaryCondition condition{};
  BoardingCheckpointMaterialExtensionCondition input_condition{};
  std::optional<std::size_t> source, triangle, edge, other_triangle, vertex,
      axis;
  std::uint64_t bound_coordinate_guards{}, nondegenerate_triangles{},
      edge_occurrences{}, edge_comparisons{};
  bool complete{};
  static constexpr bool source_qualified{false}, material_qualified{false},
      world_qualified{false}, body_qualified{false}, actor_qualified{false};
};
struct BoardingCheckpointMaterialExtensionAccess {
  [[nodiscard]] static auto data(
      const OriginBoardingCheckpointMaterialExtension&)
      -> const OriginBoardingCheckpointMaterialExtension::Data*;
  [[nodiscard]] static auto valid(
      const OriginBoardingCheckpointMaterialExtension&,
      const OriginBoardingInitialMaterial&) -> bool;
  [[nodiscard]] static auto make(
      const NativeCraftBinding&, const OriginBoardingInitialMaterial&,
      BoardingCheckpointMaterialExtensionLimits,
      BoardingCheckpointMaterialExtensionConstructorMath* evidence = nullptr)
      -> std::expected<OriginBoardingCheckpointMaterialExtension, std::string>;
  [[nodiscard]] static auto make_union02(
      const NativeCraftBinding&, const OriginBoardingInitialMaterial&,
      BoardingCheckpointMaterialExtensionLimits,
      BoardingCheckpointMaterialExtensionConstructorMath* evidence = nullptr)
      -> std::expected<OriginBoardingCheckpointMaterialExtension, std::string>;
  [[nodiscard]] static auto make_boundary03(
      const NativeCraftBinding&, const OriginBoardingInitialMaterial&,
      BoardingCheckpointMaterialBoundaryLimits,
      BoardingCheckpointMaterialBoundaryEvidence* evidence = nullptr)
      -> std::expected<OriginBoardingCheckpointMaterialExtension, std::string>;
  [[nodiscard]] static auto relation(
      const OriginBoardingCheckpointMaterialExtension&,
      const OriginBoardingInitialMaterial&, std::size_t source)
      -> BoardingCheckpointMaterialExtensionRelation;
  [[nodiscard]] static auto planes(
      const OriginBoardingCheckpointMaterialExtension&,
      const OriginBoardingInitialMaterial&, std::size_t source,
      std::size_t sector) -> std::span<const MaterialPlane>;
  [[nodiscard]] static auto mesh(
      const OriginBoardingCheckpointMaterialExtension&,
      const OriginBoardingInitialMaterial&, std::size_t source)
      -> const MaterialMeshRecord*;
  [[nodiscard]] static auto triangle(
      const OriginBoardingCheckpointMaterialExtension&,
      const OriginBoardingInitialMaterial&, std::size_t source,
      std::size_t ordinal, bool& collapsed)
      -> std::expected<std::array<RigidVector3, 3>, std::string>;
};
// Immutable prepared records are validated, but caller records never mint a
// capability.
[[nodiscard]] auto checkpoint_material_extension_constructor_math(
    const CheckpointMaterialExtensionPreparedView&,
    BoardingCheckpointMaterialExtensionLimits = {})
    -> std::expected<BoardingCheckpointMaterialExtensionConstructorMath,
                     std::string>;
[[nodiscard]] auto checkpoint_material_extension_prism_math(
    const CheckpointMaterialExtensionFrameRecord&, std::size_t sector)
    -> std::expected<BoardingCheckpointMaterialExtensionPrismMath, std::string>;
[[nodiscard]] auto checkpoint_material_extension_union02_constructor_math(
    const CheckpointMaterialExtensionPreparedView&,
    BoardingCheckpointMaterialExtensionLimits = {})
    -> std::expected<BoardingCheckpointMaterialExtensionConstructorMath,
                     std::string>;
// Arithmetic-only adjacent-band proof; no caller planes mint a capability.
[[nodiscard]] auto checkpoint_material_extension_adjacent_union_math(
    std::span<const MaterialPlane> a, std::span<const MaterialPlane> b,
    const std::array<RigidVector3, 3>& triangle, bool quantized = false,
    BoardingCheckpointMaterialExtensionLimits = {})
    -> std::expected<BoardingCheckpointMaterialExtensionConstructorMath,
                     std::string>;
[[nodiscard]] auto checkpoint_material_extension_boundary03_constructor_math(
    const CheckpointMaterialExtensionPreparedView&,
    BoardingCheckpointMaterialBoundaryLimits = {})
    -> std::expected<BoardingCheckpointMaterialBoundaryEvidence, std::string>;
// Small arbitrary mesh arithmetic fixture, never source admission.
[[nodiscard]] auto checkpoint_material_extension_closed_boundary_math(
    const MaterialMeshRecord&, BoardingCheckpointMaterialBoundaryLimits = {})
    -> std::expected<BoardingCheckpointMaterialBoundaryEvidence, std::string>;
} // namespace apsis_drift::detail
