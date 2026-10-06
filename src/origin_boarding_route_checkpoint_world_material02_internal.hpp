#pragma once
#include "apsis_drift/origin_boarding_route_checkpoint_world_material02.hpp"
#include "origin_boarding_route_checkpoint_world_material_internal.hpp"
namespace apsis_drift::detail {
struct BoardingRouteCheckpointBoundaryLimits {
  std::uint64_t witness_attempts{15360}, signed_support_calls{30720},
      width_attempts{15360};
};
struct BoardingRouteCheckpointBoundaryWitnessMath {
  BoardingRouteCheckpointBoundaryCounters work;
  BoardingRouteCheckpointWorldCondition condition{};
  bool arithmetic_supported{}, certified{};
  static constexpr bool source_qualified{false}, body_qualified{false},
      material_qualified{false}, world_qualified{false}, actor_qualified{false};
};
[[nodiscard]] auto boarding_route_checkpoint_boundary_witness_math(
    BoardingFootSiteScalarBounds positive,
    BoardingFootSiteScalarBounds negative, BoardingPlantedLegPointBounds,
    BoardingRouteCheckpointBoundaryLimits = {})
    -> std::expected<BoardingRouteCheckpointBoundaryWitnessMath, std::string>;
[[nodiscard]] auto boarding_route_checkpoint_world_material02_limits()
    -> BoardingRouteCheckpointWorldLimits;
[[nodiscard]] auto boarding_route_checkpoint_world_material02_bounded(
    const OriginBoardingBootSupport&, const OriginBoardingInitialMaterial&,
    const OriginBoardingCheckpointMaterialExtension&, double, double,
    BoardingRouteCheckpointWorldLimits,
    BoardingRouteCheckpointBoundaryLimits = {})
    -> std::expected<BoardingRouteCheckpointWorldMaterial02Diagnostic,
                     std::string>;
} // namespace apsis_drift::detail
