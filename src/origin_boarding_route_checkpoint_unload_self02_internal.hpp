#pragma once
#include "apsis_drift/origin_boarding_route_checkpoint_unload_self02.hpp"
#include "origin_boarding_route_checkpoint_unload_internal.hpp"
namespace apsis_drift::detail {
struct BoardingRouteCheckpointUnloadSelf02Limits
    : BoardingRouteCheckpointUnloadLimits {
  std::uint64_t hip_complement_attempts{
      kBoardingRouteCheckpointUnloadSelf02MaximumHipAttempts};
};
struct BoardingRouteCheckpointUnloadSelf02CellRefusal
    : BoardingRoutePortUnloadRefusal {
  bool hip_complement_capacity{};
};
[[nodiscard]] auto boarding_route_checkpoint_unload_self02_bounded(
    const OriginBoardingBootSupport&, double, double,
    BoardingRouteCheckpointUnloadSelf02Limits = {})
    -> std::expected<BoardingRouteCheckpointUnloadSelf02Diagnostic,
                     std::string>;
[[nodiscard]] auto boarding_route_checkpoint_unload_self02_cell(
    const BoardingRoutePortUnloadContext&, std::size_t, double, double, bool,
    const BoardingRouteCheckpointUnloadSelf02Limits&,
    BoardingRoutePortUnloadCounters&, std::uint64_t&,
    BoardingRouteCheckpointUnloadSelf02Cell&,
    BoardingRouteCheckpointUnloadSelf02CellRefusal&)
    -> BoardingRoutePortUnloadCellResult;
struct BoardingRouteCheckpointUnloadSelf02HipMath {
  BoardingPlantedLegScalarBounds transverse, extent, limit, strict_gap;
  bool arithmetic_supported{}, negative_axis{}, strict_extent_below_bottom{};
  static constexpr bool unit_qualified{false}, source_qualified{false},
      body_qualified{false}, self_qualified{false}, support_qualified{false},
      actor_qualified{false};
};
// Raw-component expression only: this seam never establishes a unit axis.
[[nodiscard]] auto boarding_route_checkpoint_unload_self02_hip_math(
    std::array<BoardingPlantedLegScalarBounds, 3> axis, double radius,
    double axial_limit, double pelvis_bottom)
    -> std::expected<BoardingRouteCheckpointUnloadSelf02HipMath, std::string>;
} // namespace apsis_drift::detail
