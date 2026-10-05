#pragma once

#include "apsis_drift/origin_boarding_route_foot_phase.hpp"

namespace apsis_drift::detail {
struct BoardingRouteFootPhaseLimits {
  std::size_t depth{kBoardingRouteFootPhaseMaximumDepth},
      nodes{kBoardingRouteFootPhaseMaximumNodes},
      leaves{kBoardingRouteFootPhaseMaximumLeaves},
      output_bytes{kBoardingRouteFootPhaseMaximumOutputBytes};
  std::uint64_t graphs{2047}, legs{4094}, bodies{2047}, sectors{6141},
      timing{12282};
};
enum class BoardingRouteFootPhaseCellResult : std::uint8_t {
  accepted,
  unresolved,
  unsupported,
  capacity
};
[[nodiscard]] auto boarding_route_foot_phase_request_valid(
    const BoardingRouteFootPhaseRequest&) -> std::expected<void, std::string>;
[[nodiscard]] auto boarding_route_foot_phase_environment() -> bool;
[[nodiscard]] auto boarding_route_foot_phase_parts()
    -> std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount>;
// One owned graph/cell. Private controls only lower genuine work ceilings.
[[nodiscard]] auto boarding_route_foot_phase_cell(
    const BoardingRouteFootPhaseRequest&, double first, double last,
    bool reverse, const BoardingRouteFootPhaseLimits&,
    BoardingRouteFootPhaseCounters&, BoardingRouteFootPhaseCell&,
    BoardingRouteFootPhaseRefusal&) -> BoardingRouteFootPhaseCellResult;
[[nodiscard]] auto boarding_route_foot_phase_bounded(
    const BoardingRouteFootPhaseRequest&, double first, double last,
    BoardingRouteFootPhaseLimits = {})
    -> std::expected<BoardingRouteFootPhaseDiagnostic, std::string>;
} // namespace apsis_drift::detail

namespace apsis_drift::detail {
struct BoardingRoutePhaseScalarJet {
  BoardingPlantedLegScalarBounds value, first, second;
};
struct BoardingRoutePhaseYawEvidence {
  BoardingRoutePhaseFrameEvidence frame;
  BoardingPlantedLegAngularDerivatives angle;
  bool arithmetic_supported{};
};
struct BoardingRoutePhaseHipSectorEvidence {
  std::array<BoardingPlantedLegScalarBounds, 2> margins;
  bool arithmetic_supported{}, certified{};
};
// Arithmetic-only domains: yaw value[-1,1], parameter jets abs<=4096;
// hinge components abs<=8; physical rates abs<=4096 and sine[-1,1].
// No synthetic control enrolls source geometry or route/body permission.
[[nodiscard]] auto boarding_route_phase_yaw_numeric(BoardingRoutePhaseScalarJet,
                                                    double seconds,
                                                    bool reverse)
    -> std::expected<BoardingRoutePhaseYawEvidence, std::string>;
[[nodiscard]] auto boarding_route_phase_hip_sector_numeric(
    BoardingPlantedLegScalarBounds down, BoardingPlantedLegScalarBounds forward)
    -> std::expected<BoardingRoutePhaseHipSectorEvidence, std::string>;
[[nodiscard]] auto boarding_route_phase_hip_speed_numeric(
    BoardingPlantedLegScalarBounds axial_rate,
    BoardingPlantedLegScalarBounds roll_rate,
    BoardingPlantedLegScalarBounds pitch_rate,
    BoardingPlantedLegScalarBounds roll_sine)
    -> std::expected<BoardingPlantedLegSpeedEvidence, std::string>;
} // namespace apsis_drift::detail
