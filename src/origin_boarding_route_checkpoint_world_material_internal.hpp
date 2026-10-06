#pragma once
#include "apsis_drift/origin_boarding_route_checkpoint_world_material.hpp"
#include "origin_boarding_initial_material_internal.hpp"
#include "origin_boarding_route_checkpoint_unload_self02_internal.hpp"
#include "origin_lower_cockpit_contact_internal.hpp"
#include <utility>
namespace apsis_drift::detail {
struct BoardingRouteCheckpointWorldLimits {
  BoardingRouteCheckpointUnloadSelf02Limits child;
  std::size_t output_bytes{kBoardingRouteCheckpointWorldMaximumOutputBytes},
      pair_axes{16};
  std::uint64_t roster_entries{1759}, effective_sources{1751},
      union_envelope_pairs{26265}, union_proxy_preparations{15360},
      domain_union_checks{15}, domain_cell_checks{15360},
      proxy_preparations{2373632}, refined_envelope_pairs{1048576},
      material_triangle_visits{349087}, halo_metadata_visits{75},
      halo_triangle_visits{8100}, triangle_union_pairs{5357805},
      refined_triangle_pairs{1048576}, base_enclosure_relations{240},
      refined_enclosure_relations{245760}, axes{16777216},
      direction_entries_prepared{20217856}, sole_vertex_guards{192};
};
[[nodiscard]] auto boarding_route_checkpoint_world_limits_valid(
    const BoardingRouteCheckpointWorldLimits&) -> bool;
struct BoardingRouteCheckpointWorldProxy {
  BoardingSourceEndpointSurfaceCheckpointSolidBounds solid;
  BoardingPlantedLegPointBounds bounds;
  double sole_plane{};
  bool arithmetic_supported{}, original_world_identity{},
      sole_expression_identity{};
};
struct BoardingRouteCheckpointWorldProxyMath {
  BoardingSourceEndpointSurfaceCheckpointSolidBounds solid;
  BoardingPlantedLegPointBounds bounds;
  bool arithmetic_supported{};
  static constexpr bool body_qualified{false}, self_qualified{false},
      source_qualified{false}, material_qualified{false},
      world_qualified{false}, route_qualified{false}, actor_qualified{false},
      sole_qualified{false};
};
class BoardingRouteCheckpointWorldContext;
struct BoardingRouteCheckpointWorldMaterialAccess;
[[nodiscard]] auto prepare_boarding_route_checkpoint_world(
    const OriginBoardingBootSupport&, const OriginBoardingInitialMaterial&,
    double, double, const BoardingRouteCheckpointWorldLimits&,
    BoardingRouteCheckpointWorldMaterialDiagnostic&)
    -> std::expected<BoardingRouteCheckpointWorldContext, std::string>;
[[nodiscard]] auto boarding_route_checkpoint_world_proxy(
    const BoardingRouteCheckpointWorldContext&, std::size_t part,
    std::size_t cell)
    -> std::expected<BoardingRouteCheckpointWorldProxy, std::string>;
[[nodiscard]] auto boarding_route_checkpoint_world_frame_box_math(
    BoardingPlantedLegPointBounds center,
    std::array<BoardingPlantedLegPointBounds, 3> columns, RigidVector3 half)
    -> std::expected<BoardingRouteCheckpointWorldProxyMath, std::string>;
class BoardingRouteCheckpointWorldContext {
 public:
  BoardingRouteCheckpointWorldContext(
      const BoardingRouteCheckpointWorldContext&) = delete;
  auto operator=(const BoardingRouteCheckpointWorldContext&)
      -> BoardingRouteCheckpointWorldContext& = delete;
  BoardingRouteCheckpointWorldContext(
      BoardingRouteCheckpointWorldContext&& other) noexcept
      : child_(std::exchange(other.child_, nullptr)),
        owner_(std::exchange(other.owner_, nullptr)),
        material_(std::exchange(other.material_, nullptr)),
        cell_data_(std::exchange(other.cell_data_, nullptr)),
        cell_count_(std::exchange(other.cell_count_, 0)),
        sole_planes_(other.sole_planes_), sole_sources_(other.sole_sources_),
        sole_object_clear_(other.sole_object_clear_) {}
  auto operator=(BoardingRouteCheckpointWorldContext&&)
      -> BoardingRouteCheckpointWorldContext& = delete;

 private:
  BoardingRouteCheckpointWorldContext(
      const BoardingRouteCheckpointWorldMaterialDiagnostic& owner,
      const OriginBoardingInitialMaterial::Data* material)
      : child_(owner.child.get()), owner_(&owner), material_(material),
        cell_data_(owner.child->cells.data()),
        cell_count_(owner.child->cells.size()) {}
  const BoardingRouteCheckpointUnloadSelf02Diagnostic* child_{};
  // Private borrow: parent report must outlive context; moving/resetting its
  // child is detected through this anchor before dereferencing the child.
  const BoardingRouteCheckpointWorldMaterialDiagnostic* owner_{};
  const OriginBoardingInitialMaterial::Data* material_{};
  const BoardingRouteCheckpointUnloadSelf02Cell* cell_data_{};
  std::size_t cell_count_{};
  std::array<double, 2> sole_planes_{};
  std::array<std::size_t, 2> sole_sources_{};
  std::array<bool, 2> sole_object_clear_{};
  friend struct BoardingRouteCheckpointWorldMaterialAccess;
  friend auto prepare_boarding_route_checkpoint_world(
      const OriginBoardingBootSupport&, const OriginBoardingInitialMaterial&,
      double, double, const BoardingRouteCheckpointWorldLimits&,
      BoardingRouteCheckpointWorldMaterialDiagnostic&)
      -> std::expected<BoardingRouteCheckpointWorldContext, std::string>;
  friend auto boarding_route_checkpoint_world_proxy(
      const BoardingRouteCheckpointWorldContext&, std::size_t, std::size_t)
      -> std::expected<BoardingRouteCheckpointWorldProxy, std::string>;
};
// Authenticated read-only source access, never caller geometry enrollment.
struct BoardingRouteCheckpointWorldMaterialAccess {
  [[nodiscard]] static auto valid(const BoardingRouteCheckpointWorldContext&)
      -> bool;
  [[nodiscard]] static auto source_count(
      const BoardingRouteCheckpointWorldContext&) -> std::size_t;
  [[nodiscard]] static auto source(const BoardingRouteCheckpointWorldContext&,
                                   std::size_t) -> const MaterialSourceRecord*;
  [[nodiscard]] static auto envelope(const BoardingRouteCheckpointWorldContext&,
                                     std::size_t)
      -> std::optional<BoardingPlantedLegPointBounds>;
  [[nodiscard]] static auto triangle_count(
      const BoardingRouteCheckpointWorldContext&, std::size_t) -> std::size_t;
  [[nodiscard]] static auto triangle(const BoardingRouteCheckpointWorldContext&,
                                     std::size_t, std::size_t, bool& collapsed)
      -> std::expected<std::array<RigidVector3, 3>, std::string>;
  [[nodiscard]] static auto encloser_count(
      const BoardingRouteCheckpointWorldContext&, std::size_t) -> std::size_t;
  [[nodiscard]] static auto enclosure(
      const BoardingRouteCheckpointWorldContext&, std::size_t, std::size_t,
      const BoardingRouteCheckpointWorldProxy&, std::size_t axes)
      -> std::expected<MaterialEnclosureMathEvidence, std::string>;
  [[nodiscard]] static auto sole_volume_clear(
      const BoardingRouteCheckpointWorldContext&, std::size_t, std::size_t)
      -> bool;
};
[[nodiscard]] auto boarding_route_checkpoint_world_bounded(
    const OriginBoardingBootSupport&, const OriginBoardingInitialMaterial&,
    double, double, BoardingRouteCheckpointWorldLimits = {})
    -> std::expected<BoardingRouteCheckpointWorldMaterialDiagnostic,
                     std::string>;
struct BoardingRouteCheckpointWorldSweepMath {
  BoardingRouteCheckpointWorldCounters work;
  std::optional<std::size_t> refused_triangle, refused_cell;
  BoardingRouteCheckpointWorldCondition condition{};
  bool arithmetic_supported{}, complete{};
  static constexpr bool source_qualified{false}, body_qualified{false},
      world_qualified{false}, material_qualified{false}, actor_qualified{false};
};
// At most eight arithmetic-only cells/triangles, no source or sole enrollment.
[[nodiscard]] auto boarding_route_checkpoint_world_sweep_math(
    std::span<const BoardingSourceEndpointSurfaceCheckpointSolidBounds> cells,
    std::span<const std::array<RigidVector3, 3>> triangles,
    BoardingRouteCheckpointWorldLimits = {})
    -> std::expected<BoardingRouteCheckpointWorldSweepMath, std::string>;
[[nodiscard]] auto boarding_route_checkpoint_world_domain_math(
    const OriginLowerCockpitContact&,
    std::span<const BoardingPlantedLegPointBounds>,
    BoardingRouteCheckpointWorldLimits = {})
    -> std::expected<BoardingRouteCheckpointWorldSweepMath, std::string>;
} // namespace apsis_drift::detail
