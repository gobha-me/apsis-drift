#pragma once

#include "apsis_drift/origin_boarding_source_endpoint_surface_checkpoint.hpp"

namespace apsis_drift::detail {
enum class BoardingSourceEndpointSurfaceCheckpointShape : std::uint8_t {
  box,
  capsule
};
struct BoardingSourceEndpointSurfaceCheckpointSolidBounds {
  BoardingPlantedLegPointBounds first, second;
  RigidVector3 half_size_metres;
  double radius_metres{};
  BoardingSourceEndpointSurfaceCheckpointShape shape{};
};
struct BoardingSourceEndpointSurfaceCheckpointPairMathEvidence {
  bool arithmetic_supported{}, certified{}, truncated{}, sole_contact{};
  std::uint64_t axes_examined{}, unsupported_axes{};
  RigidVector3 separating_direction;
  BoardingFootSiteScalarBounds certificate_gap;
  static constexpr bool body_qualified{false}, self_qualified{false},
      load_qualified{false}, surface_qualified{false}, world_qualified{false},
      route_qualified{false}, actor_qualified{false};
};
struct BoardingSourceEndpointSurfaceCheckpointSoleMathEvidence {
  double plane_metres{};
  bool arithmetic_supported{}, template_matches{}, all_vertices_at_or_below{};
  static constexpr bool body_qualified{false}, self_qualified{false},
      load_qualified{false}, surface_qualified{false}, world_qualified{false},
      route_qualified{false}, actor_qualified{false};
};
[[nodiscard]] auto boarding_source_endpoint_surface_checkpoint_bounded(
    const OriginBoardingBootSupport&,
    std::size_t max_source_partitions = kBoardingBootSourcePartitionCount,
    std::size_t max_body_records = kBoardingSourceEndpointMaximumRecords,
    std::size_t max_self_pairs = kBoardingBodyPairCount,
    std::size_t max_self_axes = kBoardingSourceEndpointSelfMaximumAxes,
    std::size_t max_pressure_partitions = kBoardingBootSourcePartitionCount,
    std::uint64_t max_pairs = kBoardingSourceEndpointSurfaceMaximumPairs,
    std::size_t max_axes = kBoardingSourceEndpointSurfaceMaximumAxes)
    -> std::expected<BoardingSourceEndpointSurfaceCheckpointDiagnostic,
                     std::string>;
[[nodiscard]] auto boarding_source_endpoint_surface_checkpoint_pair_math(
    const BoardingSourceEndpointSurfaceCheckpointSolidBounds&, double common_y,
    const std::array<RigidVector3, 3>& triangle,
    std::size_t max_axes = kBoardingSourceEndpointSurfaceMaximumAxes)
    -> std::expected<BoardingSourceEndpointSurfaceCheckpointPairMathEvidence,
                     std::string>;
[[nodiscard]] auto boarding_source_endpoint_surface_checkpoint_support_math(
    const BoardingSourceEndpointSurfaceCheckpointSolidBounds&, double common_y,
    RigidVector3 direction)
    -> std::expected<BoardingFootSiteScalarBounds, std::string>;
// Fixed identity sole template only; no caller frame or enrollment flag.
[[nodiscard]] auto boarding_source_endpoint_surface_checkpoint_sole_math(
    std::array<double, 3> center_y_terms, double common_y, double half_y,
    const std::array<RigidVector3, 3>& triangle)
    -> std::expected<BoardingSourceEndpointSurfaceCheckpointSoleMathEvidence,
                     std::string>;
} // namespace apsis_drift::detail

namespace apsis_drift::detail {
// Arithmetic-only bridge for finite authored source triangles beyond the old
// fixture workspace. It enrolls neither a source, a body nor a sole. The WORLD
// controller binds actual source occurrences and an authenticated child first.
[[nodiscard]] auto boarding_checkpoint_world_finite_triangle_pair_math(
    const BoardingSourceEndpointSurfaceCheckpointSolidBounds&, double common_y,
    const std::array<RigidVector3, 3>& triangle,
    std::size_t max_axes = kBoardingSourceEndpointSurfaceMaximumAxes)
    -> std::expected<BoardingSourceEndpointSurfaceCheckpointPairMathEvidence,
                     std::string>;
} // namespace apsis_drift::detail
