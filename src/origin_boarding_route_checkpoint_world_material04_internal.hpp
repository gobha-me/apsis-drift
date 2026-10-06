#pragma once
#include "apsis_drift/origin_boarding_route_checkpoint_world_material04.hpp"
#include "origin_boarding_route_checkpoint_world_material03_internal.hpp"
namespace apsis_drift::detail {
[[nodiscard]] auto boarding_route_checkpoint_world_material04_limits()
    -> BoardingRouteCheckpointWorldLimits;
[[nodiscard]] auto boarding_route_checkpoint_world_material04_bounded(
    const OriginBoardingBootSupport&, const OriginBoardingInitialMaterial&,
    const OriginBoardingCheckpointMaterialExtension&,
    const OriginBoardingHatchSealMaterial&,
    const OriginBoardingSeparationRingMaterial&, double, double,
    BoardingRouteCheckpointWorldLimits,
    BoardingRouteCheckpointBoundaryLimits = {})
    -> std::expected<BoardingRouteCheckpointWorldMaterial04Diagnostic,
                     std::string>;
} // namespace apsis_drift::detail
