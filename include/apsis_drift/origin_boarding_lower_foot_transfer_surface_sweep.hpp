#pragma once

#include "apsis_drift/origin_boarding_lower_foot_transfer.hpp"
#include "apsis_drift/origin_boarding_source_endpoint_surface_checkpoint.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace apsis_drift {
inline constexpr std::uint32_t kBoardingLowerFootTransferSurfaceSweepVersion{1};
inline constexpr std::uint64_t
    kBoardingLowerFootTransferSurfaceMaximumTriangles{524288},
    kBoardingLowerFootTransferSurfaceMaximumBasePairs{7864320},
    kBoardingLowerFootTransferSurfaceMaximumRefinedPairs{1048576},
    kBoardingLowerFootTransferSurfaceMaximumAxes{16777216};
inline constexpr std::size_t kBoardingLowerFootTransferSurfaceMaximumPairAxes{
    16},
    kBoardingLowerFootTransferSurfaceMaximumPayloadBytes{4096},
    kBoardingLowerFootTransferSurfaceMaximumScratchBytes{8192};
using BoardingLowerFootTransferSurfaceSweepCoverage =
    BoardingSourceEndpointSurfaceCheckpointCoverage;
enum class BoardingLowerFootTransferSurfaceSweepCondition : std::uint8_t {
  child_prerequisite,
  invalid_binding,
  unsupported_arithmetic,
  source_capacity,
  source_roster_changed,
  crop_uncovered,
  base_pair_capacity,
  refined_pair_capacity,
  pair_axis_capacity,
  aggregate_axis_capacity,
  no_separation_certificate
};
struct BoardingLowerFootTransferSurfaceSweepRefusal {
  std::optional<LowerCockpitTriangleKey> key;
  // Borrowed immutable name storage remains owned through transfer.initial.
  std::string_view source_object;
  std::optional<std::uint64_t> base_pair_index;
  std::optional<std::size_t> cell_index;
  std::optional<std::uint32_t> evaluated_source_triangle;
  std::optional<std::array<RigidVector3, 3>> actual_triangle_metres;
  double first{}, last{};
  std::uint64_t axes_examined{}, unsupported_axes{};
  std::uint32_t object{};
  BoardingBodyPartId part{};
  BoardingLowerFootTransferSurfaceSweepCondition condition{};
};
struct BoardingLowerFootTransferSurfaceSweepPayload {
  std::uint32_t surface_version{kBoardingLowerFootTransferSurfaceSweepVersion};
  // Immutable union WORLD bounds. Per-cell refinement cannot overwrite them.
  std::array<BoardingLowerFootTransferSurfaceSweepCoverage,
             kBoardingBodyPartCount>
      coverage{};
  std::uint64_t effective_triangle_count{}, cell_count{}, expected_base_pairs{},
      logical_expected_comparisons{}, logical_certified_comparisons{},
      examined_base_pairs{}, completed_base_pairs{},
      union_broad_certified_pairs{}, refined_completed_base_pairs{},
      examined_cell_pairs{}, certified_cell_pairs{},
      cell_broad_certified_pairs{}, cell_sole_certified_pairs{},
      cell_support_certified_pairs{}, axes_examined{}, unsupported_axes{},
      visited_triangles{}, metadata_visited_triangles{};
  bool arithmetic_supported{}, coverage_complete{}, comparisons_complete{},
      partial_continuous_surface_qualified{}, complete{};
  std::optional<BoardingLowerFootTransferSurfaceSweepRefusal> first_refusal;
};
struct BoardingLowerFootTransferSurfaceSweepDiagnostic {
  // One unchanged registered child; no copied or reevaluated body graph.
  BoardingLowerFootTransferDiagnostic transfer;
  BoardingLowerFootTransferSurfaceSweepPayload surface;
  static constexpr bool world_qualified{false}, volume_qualified{false},
      material_qualified{false}, strength_qualified{false},
      dynamics_qualified{false}, friction_qualified{false},
      route_qualified{false}, actor_qualified{false}, seat_qualified{false},
      save_qualified{false}, first_flight_qualified{false},
      foot_acquisition_qualified{false}, free_foot_swing_qualified{false},
      complete_lower_step_to_seat_transfer_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_lower_foot_transfer_surface_sweep(
    const OriginBoardingBootSupport&, double first = 0, double last = 1)
    -> std::expected<BoardingLowerFootTransferSurfaceSweepDiagnostic,
                     std::string>;
} // namespace apsis_drift
