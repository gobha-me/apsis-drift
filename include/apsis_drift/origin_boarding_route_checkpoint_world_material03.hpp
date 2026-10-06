#pragma once
#include "apsis_drift/origin_boarding_hatch_seal_material.hpp"
#include "apsis_drift/origin_boarding_route_checkpoint_world_material02.hpp"
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingRouteCheckpointWorldMaterial03Version{
    3};
struct BoardingRouteCheckpointWorldMaterial03Diagnostic {
  // One original result owns the only child and retained Self02 cover.
  BoardingRouteCheckpointWorldMaterialDiagnostic result;
  OriginBoardingCheckpointMaterialExtension extension;
  OriginBoardingHatchSealMaterial seal;
  BoardingRouteCheckpointBoundaryCounters boundary_work;
  // Associated failed seal obligation; this does not claim kernel execution.
  std::optional<std::size_t> seal_segment;
  BoardingRouteCheckpointWorldMaterial03Diagnostic(
      const OriginBoardingInitialMaterial& material,
      const OriginBoardingCheckpointMaterialExtension& added,
      const OriginBoardingHatchSealMaterial& hatch)
      : result(material), extension(added), seal(hatch) {}
  static constexpr bool route_qualified{false}, actor_qualified{false},
      seat_qualified{false}, save_qualified{false},
      first_flight_qualified{false}, free_foot_swing_qualified{false},
      dynamics_qualified{false}, strength_qualified{false},
      friction_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_route_checkpoint_world_material03(
    const OriginBoardingBootSupport&, const OriginBoardingInitialMaterial&,
    const OriginBoardingCheckpointMaterialExtension&,
    const OriginBoardingHatchSealMaterial&, double first = 0, double last = 1)
    -> std::expected<BoardingRouteCheckpointWorldMaterial03Diagnostic,
                     std::string>;
} // namespace apsis_drift
