#pragma once
#include "apsis_drift/origin_boarding_route_checkpoint_world_material03.hpp"
#include "origin_boarding_route_checkpoint_world_material02_internal.hpp"
namespace apsis_drift::detail {
[[nodiscard]] auto boarding_route_checkpoint_world_material03_limits()
    -> BoardingRouteCheckpointWorldLimits;
[[nodiscard]] auto boarding_route_checkpoint_world_material03_bounded(
    const OriginBoardingBootSupport&, const OriginBoardingInitialMaterial&,
    const OriginBoardingCheckpointMaterialExtension&,
    const OriginBoardingHatchSealMaterial&, double, double,
    BoardingRouteCheckpointWorldLimits,
    BoardingRouteCheckpointBoundaryLimits = {})
    -> std::expected<BoardingRouteCheckpointWorldMaterial03Diagnostic,
                     std::string>;
} // namespace apsis_drift::detail
