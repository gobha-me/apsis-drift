#pragma once
#include "apsis_drift/origin_boarding_checkpoint_material_extension.hpp"
#include "apsis_drift/origin_boarding_route_checkpoint_world_material.hpp"
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingRouteCheckpointWorldMaterial02Version{
    2};
struct BoardingRouteCheckpointWorldMaterial02Diagnostic {
  // One original report owns the only child and retained cover. Its source1
  // summary stays source1; the explicit extension describes the added policy.
  BoardingRouteCheckpointWorldMaterialDiagnostic result;
  OriginBoardingCheckpointMaterialExtension extension;
  BoardingRouteCheckpointWorldMaterial02Diagnostic(
      const OriginBoardingInitialMaterial& material,
      const OriginBoardingCheckpointMaterialExtension& added)
      : result(material), extension(added) {}
  static constexpr bool route_qualified{false}, actor_qualified{false},
      seat_qualified{false}, save_qualified{false},
      first_flight_qualified{false}, free_foot_swing_qualified{false},
      dynamics_qualified{false}, strength_qualified{false},
      friction_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_route_checkpoint_world_material02(
    const OriginBoardingBootSupport&, const OriginBoardingInitialMaterial&,
    const OriginBoardingCheckpointMaterialExtension&, double first = 0,
    double last = 1)
    -> std::expected<BoardingRouteCheckpointWorldMaterial02Diagnostic,
                     std::string>;
} // namespace apsis_drift
