#pragma once

#include "apsis_drift/origin_boarding_lower_foot_transfer_surface_sweep.hpp"
#include "origin_boarding_lower_foot_transfer_internal.hpp"
#include "origin_boarding_source_endpoint_surface_checkpoint_internal.hpp"
#include "origin_lower_cockpit_contact_internal.hpp"
#include <span>

namespace apsis_drift::detail {
struct BoardingLowerFootTransferSurfaceSweepLimits {
  BoardingLowerFootTransferLimits child{};
  std::uint64_t source_triangles{
      kBoardingLowerFootTransferSurfaceMaximumTriangles},
      base_pairs{kBoardingLowerFootTransferSurfaceMaximumBasePairs},
      refined_pairs{kBoardingLowerFootTransferSurfaceMaximumRefinedPairs},
      axes{kBoardingLowerFootTransferSurfaceMaximumAxes};
  std::size_t pair_axes{kBoardingLowerFootTransferSurfaceMaximumPairAxes};
};
class BoardingLowerFootTransferSurfaceContext;
[[nodiscard]] auto prepare_boarding_lower_foot_transfer_surface(
    const BoardingLowerFootTransferDiagnostic&)
    -> std::expected<BoardingLowerFootTransferSurfaceContext, std::string>;
class BoardingLowerFootTransferSurfaceContext {
 public:
  BoardingLowerFootTransferSurfaceContext(
      const BoardingLowerFootTransferSurfaceContext&) = default;
  auto operator=(const BoardingLowerFootTransferSurfaceContext&)
      -> BoardingLowerFootTransferSurfaceContext& = default;

 private:
  explicit BoardingLowerFootTransferSurfaceContext(
      const BoardingLowerFootTransferDiagnostic& child)
      : child_(&child) {}
  const BoardingLowerFootTransferDiagnostic* child_;
  std::array<BoardingPlantedLegPointBounds, kBoardingPlantedBodyPointCount>
      union_points_{};
  friend auto prepare_boarding_lower_foot_transfer_surface(
      const BoardingLowerFootTransferDiagnostic&)
      -> std::expected<BoardingLowerFootTransferSurfaceContext, std::string>;
  friend auto boarding_lower_foot_transfer_surface_union_bounds(
      const BoardingLowerFootTransferSurfaceContext&, std::size_t)
      -> std::expected<BoardingLowerFootTransferSurfaceSweepCoverage,
                       std::string>;
  friend auto boarding_lower_foot_transfer_surface_cell_pair(
      const BoardingLowerFootTransferSurfaceContext&, std::size_t, std::size_t,
      const LowerCockpitEffectiveTriangle&, std::size_t)
      -> std::expected<BoardingSourceEndpointSurfaceCheckpointPairMathEvidence,
                       std::string>;
};
[[nodiscard]] auto boarding_lower_foot_transfer_surface_union_bounds(
    const BoardingLowerFootTransferSurfaceContext&, std::size_t part)
    -> std::expected<BoardingLowerFootTransferSurfaceSweepCoverage,
                     std::string>;
// Finite-only original triangles; the immutable union AABB is never a cell's
// overwritten Workspace. This scalar arithmetic record admits no source/body.
[[nodiscard]] auto boarding_lower_foot_transfer_surface_union_broad(
    const BoardingLowerFootTransferSurfaceSweepCoverage&,
    const std::array<RigidVector3, 3>&)
    -> std::expected<BoardingSourceEndpointSurfaceCheckpointPairMathEvidence,
                     std::string>;
[[nodiscard]] auto boarding_lower_foot_transfer_surface_cell_pair(
    const BoardingLowerFootTransferSurfaceContext&, std::size_t part,
    std::size_t cell, const LowerCockpitEffectiveTriangle&,
    std::size_t max_axes = kBoardingLowerFootTransferSurfaceMaximumPairAxes)
    -> std::expected<BoardingSourceEndpointSurfaceCheckpointPairMathEvidence,
                     std::string>;
// Arbitrary local full boxes/capsules, without sole/source enrollment.
[[nodiscard]] auto boarding_lower_foot_transfer_surface_union_bounds_math(
    const BoardingSourceEndpointSurfaceCheckpointSolidBounds&, double common_y)
    -> std::expected<BoardingLowerFootTransferSurfaceSweepCoverage,
                     std::string>;
[[nodiscard]] auto boarding_lower_foot_transfer_surface_sweep_bounded(
    const OriginBoardingBootSupport&, double first, double last,
    BoardingLowerFootTransferSurfaceSweepLimits = {})
    -> std::expected<BoardingLowerFootTransferSurfaceSweepDiagnostic,
                     std::string>;
struct BoardingLowerFootTransferSurfaceSweepMathEvidence {
  // One local part against supplied finite small triangles, sharing the same
  // two-stage base helper. No immutable source or actor permission.
  BoardingLowerFootTransferSurfaceSweepPayload work;
  static constexpr bool body_qualified{false}, source_qualified{false},
      surface_qualified{false}, world_qualified{false}, volume_qualified{false},
      actor_qualified{false}, route_qualified{false};
};
[[nodiscard]] auto boarding_lower_foot_transfer_surface_sweep_math(
    std::span<const BoardingSourceEndpointSurfaceCheckpointSolidBounds> cells,
    double common_y, std::span<const std::array<RigidVector3, 3>> triangles,
    BoardingLowerFootTransferSurfaceSweepLimits = {})
    -> std::expected<BoardingLowerFootTransferSurfaceSweepMathEvidence,
                     std::string>;
} // namespace apsis_drift::detail
