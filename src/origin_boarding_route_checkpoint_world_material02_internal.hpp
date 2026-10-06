#pragma once
#include "apsis_drift/origin_boarding_route_checkpoint_world_material02.hpp"
#include "origin_boarding_route_checkpoint_world_material_internal.hpp"
namespace apsis_drift::detail {
[[nodiscard]] auto boarding_route_checkpoint_world_material02_limits()
    -> BoardingRouteCheckpointWorldLimits;
[[nodiscard]] auto boarding_route_checkpoint_world_material02_bounded(
    const OriginBoardingBootSupport&, const OriginBoardingInitialMaterial&,
    const OriginBoardingCheckpointMaterialExtension&, double, double,
    BoardingRouteCheckpointWorldLimits)
    -> std::expected<BoardingRouteCheckpointWorldMaterial02Diagnostic,
                     std::string>;
} // namespace apsis_drift::detail
