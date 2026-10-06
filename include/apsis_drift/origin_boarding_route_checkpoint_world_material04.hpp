#pragma once
#include "apsis_drift/origin_boarding_route_checkpoint_world_material03.hpp"
#include "apsis_drift/origin_boarding_separation_ring_material.hpp"
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingRouteCheckpointWorldMaterial04Version{
    4};
struct BoardingRouteCheckpointWorldMaterial04Diagnostic {
  // The previous result owns the only original child and Self02 cover.
  BoardingRouteCheckpointWorldMaterial03Diagnostic previous;
  OriginBoardingSeparationRingMaterial ring;
  // Names the failed genuine ring obligation, not kernel execution.
  std::optional<std::size_t> ring_segment;
  BoardingRouteCheckpointWorldMaterial04Diagnostic(
      const OriginBoardingInitialMaterial& material,
      const OriginBoardingCheckpointMaterialExtension& extension,
      const OriginBoardingHatchSealMaterial& seal,
      const OriginBoardingSeparationRingMaterial& separation)
      : previous(material, extension, seal), ring(separation) {}
  static constexpr bool route_qualified{false}, actor_qualified{false},
      seat_qualified{false}, save_qualified{false},
      first_flight_qualified{false}, free_foot_swing_qualified{false},
      dynamics_qualified{false}, strength_qualified{false},
      friction_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_route_checkpoint_world_material04(
    const OriginBoardingBootSupport&, const OriginBoardingInitialMaterial&,
    const OriginBoardingCheckpointMaterialExtension&,
    const OriginBoardingHatchSealMaterial&,
    const OriginBoardingSeparationRingMaterial&, double first = 0,
    double last = 1)
    -> std::expected<BoardingRouteCheckpointWorldMaterial04Diagnostic,
                     std::string>;
} // namespace apsis_drift
