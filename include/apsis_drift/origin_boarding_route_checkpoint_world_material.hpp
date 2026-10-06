#pragma once
#include "apsis_drift/origin_boarding_initial_material.hpp"
#include "apsis_drift/origin_boarding_route_checkpoint_unload_self02.hpp"
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingRouteCheckpointWorldMaterialVersion{1};
inline constexpr std::size_t kBoardingRouteCheckpointWorldMaximumOutputBytes{
    std::size_t{16} * std::size_t{1024} * std::size_t{1024}};
enum class BoardingRouteCheckpointWorldCondition : std::uint8_t {
  none,
  invalid_binding,
  child_prerequisite,
  unsupported_arithmetic,
  output_capacity,
  roster_capacity,
  domain_capacity,
  domain_uncovered,
  proxy_capacity,
  envelope_capacity,
  triangle_capacity,
  pair_capacity,
  axis_capacity,
  preparation_capacity,
  sole_guard_capacity,
  missing_relation,
  enclosure_unresolved,
  sheet_unresolved,
  source_identity,
  boundary_witness_capacity,
  boundary_support_capacity,
  boundary_width_capacity,
  boundary_witness_unresolved
};
struct BoardingRouteCheckpointWorldCounters {
  std::uint64_t roster_entries{}, effective_sources{}, union_envelope_pairs{},
      union_proxy_preparations{}, domain_union_checks{}, domain_cell_checks{},
      proxy_preparations{}, refined_envelope_pairs{},
      material_triangle_visits{}, halo_metadata_visits{},
      halo_triangle_visits{}, collapsed_triangles{}, triangle_union_pairs{},
      refined_triangle_pairs{}, base_enclosure_relations{},
      refined_enclosure_relations{}, axes_examined{},
      direction_entries_prepared{}, sole_vertex_guards{},
      sole_triangle_exclusions{}, sole_volume_exclusions{};
};
struct BoardingRouteCheckpointWorldPart {
  BoardingBodyPartId id{};
  BoardingPlantedLegPointBounds trajectory_union;
  std::uint64_t material_sources_closed{}, material_cells_closed{},
      material_triangles_closed{}, halo_triangles_closed{};
  bool domain_complete{}, union_domain_complete{}, original_world_identity{},
      complete{};
};
struct BoardingRouteCheckpointWorldRefusal {
  BoardingRouteCheckpointWorldCondition condition{};
  std::optional<std::size_t> part, source, cell;
  std::optional<std::uint32_t> triangle;
  std::optional<LowerCockpitTriangleKey> source_key;
  std::string_view source_object;
  BoardingInitialMaterialRelation relation{};
  double global_first{}, global_last{};
  std::size_t phase_index{};
};
struct BoardingRouteCheckpointWorldPayload {
  std::uint32_t world_version{kBoardingRouteCheckpointWorldMaterialVersion};
  BoardingInitialMaterialSourceSummary source;
  std::array<BoardingRouteCheckpointWorldPart, kBoardingBodyPartCount> parts;
  BoardingRouteCheckpointWorldCounters work;
  std::size_t output_capacity_bytes{};
  bool bindings_complete{}, source_complete{}, arithmetic_supported{},
      domain_complete{}, material_exclusion{}, halo_surface_exclusion{},
      complete{};
  std::optional<BoardingRouteCheckpointWorldRefusal> first_refusal;
};
struct BoardingRouteCheckpointWorldMaterialDiagnostic {
  // ONE original Self02 assessment and its owned prerequisite/cover.
  std::unique_ptr<BoardingRouteCheckpointUnloadSelf02Diagnostic> child;
  OriginBoardingInitialMaterial source;
  BoardingRouteCheckpointWorldPayload world;
  explicit BoardingRouteCheckpointWorldMaterialDiagnostic(
      const OriginBoardingInitialMaterial& material)
      : source(material) {}
  static constexpr bool route_qualified{false}, actor_qualified{false},
      seat_qualified{false}, save_qualified{false},
      first_flight_qualified{false}, free_foot_swing_qualified{false},
      dynamics_qualified{false}, strength_qualified{false},
      friction_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_route_checkpoint_world_material(
    const OriginBoardingBootSupport&, const OriginBoardingInitialMaterial&,
    double first = 0, double last = 1)
    -> std::expected<BoardingRouteCheckpointWorldMaterialDiagnostic,
                     std::string>;
} // namespace apsis_drift
