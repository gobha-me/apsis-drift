#pragma once
#include "apsis_drift/origin_boarding_route_checkpoint_unload.hpp"
#include "origin_boarding_route_port_unload_internal.hpp"
namespace apsis_drift::detail {
using BoardingRouteCheckpointUnloadLimits = BoardingRoutePortUnloadLimits;
using BoardingRouteCheckpointUnloadContext = BoardingRoutePortUnloadContext;
using BoardingRouteCheckpointUnloadCellResult =
    BoardingRoutePortUnloadCellResult;
[[nodiscard]] auto boarding_route_checkpoint_unload_controls(std::size_t)
    -> std::optional<BoardingRouteFootPhaseRequest>;
[[nodiscard]] auto boarding_route_checkpoint_unload_clock(double) -> double;
[[nodiscard]] auto prepare_boarding_route_checkpoint_unload(
    const OriginBoardingBootSupport&,
    const BoardingRouteCheckpointUnloadLimits&,
    BoardingRouteCheckpointUnloadDiagnostic&)
    -> std::expected<BoardingRouteCheckpointUnloadContext, std::string>;
[[nodiscard]] auto boarding_route_checkpoint_unload_cell(
    const BoardingRoutePortUnloadContext&, std::size_t, double, double, bool,
    const BoardingRoutePortUnloadLimits&, BoardingRoutePortUnloadCounters&,
    BoardingRoutePortUnloadCell&, BoardingRoutePortUnloadRefusal&)
    -> BoardingRoutePortUnloadCellResult;
[[nodiscard]] auto boarding_route_checkpoint_unload_pressure(
    const BoardingRouteCheckpointUnloadCellToken&,
    const BoardingRoutePortUnloadLimits&, BoardingRoutePortUnloadCounters&,
    BoardingRoutePortUnloadCell&, BoardingRoutePortUnloadRefusal&)
    -> BoardingRoutePortUnloadCellResult;
[[nodiscard]] auto boarding_route_checkpoint_unload_bounded(
    const OriginBoardingBootSupport&, double, double,
    BoardingRouteCheckpointUnloadLimits = {})
    -> std::expected<BoardingRouteCheckpointUnloadDiagnostic, std::string>;
class BoardingRouteCheckpointUnloadCellToken {
 private:
  BoardingRouteCheckpointUnloadCellToken(
      const BoardingRoutePortUnloadContext& context,
      const BoardingRoutePortUnloadCell& cell, std::size_t phase_index)
      : context_(&context), cell_(&cell), phase_index_(phase_index) {}
  const BoardingRoutePortUnloadContext* context_;
  const BoardingRoutePortUnloadCell* cell_;
  std::size_t phase_index_;
  friend auto boarding_route_checkpoint_unload_cell(
      const BoardingRoutePortUnloadContext&, std::size_t, double, double, bool,
      const BoardingRoutePortUnloadLimits&, BoardingRoutePortUnloadCounters&,
      BoardingRoutePortUnloadCell&, BoardingRoutePortUnloadRefusal&)
      -> BoardingRoutePortUnloadCellResult;
  friend auto boarding_route_checkpoint_unload_pressure(
      const BoardingRouteCheckpointUnloadCellToken&,
      const BoardingRoutePortUnloadLimits&, BoardingRoutePortUnloadCounters&,
      BoardingRoutePortUnloadCell&, BoardingRoutePortUnloadRefusal&)
      -> BoardingRoutePortUnloadCellResult;
};
} // namespace apsis_drift::detail
