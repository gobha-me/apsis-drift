#pragma once
#include "apsis_drift/origin_boarding_route_port_unload.hpp"
#include "origin_boarding_route_foot_phase_internal.hpp"
namespace apsis_drift::detail {
struct BoardingRoutePortUnloadLimits {
  BoardingRouteFootPhaseLimits phase;
  std::size_t initial_source_partitions{10},
      initial_body_records{kBoardingSourceEndpointMaximumRecords},
      initial_self_pairs{105}, initial_self_axes{14},
      initial_pressure_partitions{10},
      output_bytes{kBoardingRoutePortUnloadMaximumOutputBytes};
  std::uint64_t pairs{214935}, axes{3009090}, signed_trials{6018180},
      owners{28658}, candidates{40940}, edges{180136};
};
using BoardingRoutePortUnloadCellResult = BoardingRouteFootPhaseCellResult;
class BoardingRoutePortUnloadContext;
class BoardingRoutePortUnloadCellToken;
class BoardingRouteCheckpointUnloadCellToken;
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
[[nodiscard]] auto boarding_route_port_unload_controls()
    -> BoardingRouteFootPhaseRequest;
[[nodiscard]] auto prepare_boarding_route_port_unload(
    const OriginBoardingBootSupport&, const BoardingRoutePortUnloadLimits&,
    BoardingRoutePortUnloadDiagnostic&)
    -> std::expected<BoardingRoutePortUnloadContext, std::string>;
[[nodiscard]] auto boarding_route_port_unload_cell(
    const BoardingRoutePortUnloadContext&, double, double, bool,
    const BoardingRoutePortUnloadLimits&, BoardingRoutePortUnloadCounters&,
    BoardingRoutePortUnloadCell&, BoardingRoutePortUnloadRefusal&)
    -> BoardingRoutePortUnloadCellResult;
[[nodiscard]] auto boarding_route_port_unload_pressure(
    const BoardingRoutePortUnloadCellToken&,
    const BoardingRoutePortUnloadLimits&, BoardingRoutePortUnloadCounters&,
    BoardingRoutePortUnloadCell&, BoardingRoutePortUnloadRefusal&)
    -> BoardingRoutePortUnloadCellResult;
[[nodiscard]] auto boarding_route_port_unload_bounded(
    const OriginBoardingBootSupport&, double, double,
    BoardingRoutePortUnloadLimits = {})
    -> std::expected<BoardingRoutePortUnloadDiagnostic, std::string>;
class BoardingRoutePortUnloadContext {
 public:
  BoardingRoutePortUnloadContext(const BoardingRoutePortUnloadContext&) =
      default;
  BoardingRoutePortUnloadContext(BoardingRoutePortUnloadContext&&) noexcept =
      default;
  auto operator=(const BoardingRoutePortUnloadContext&)
      -> BoardingRoutePortUnloadContext& = default;
  auto operator=(BoardingRoutePortUnloadContext&&) noexcept
      -> BoardingRoutePortUnloadContext& = default;

 private:
  explicit BoardingRoutePortUnloadContext(
      const BoardingSourceEndpointLoadDiagnostic& initial)
      : source_(initial.self.endpoint.sites.source),
        guards_(initial.load.source_quads) {}
  OriginBoardingBootSupport source_;
  std::array<BoardingSourceEndpointLoadQuadMathEvidence, 10> guards_;
  friend auto prepare_boarding_route_port_unload(
      const OriginBoardingBootSupport&, const BoardingRoutePortUnloadLimits&,
      BoardingRoutePortUnloadDiagnostic&)
      -> std::expected<BoardingRoutePortUnloadContext, std::string>;
  friend auto boarding_route_port_unload_cell(
      const BoardingRoutePortUnloadContext&, double, double, bool,
      const BoardingRoutePortUnloadLimits&, BoardingRoutePortUnloadCounters&,
      BoardingRoutePortUnloadCell&, BoardingRoutePortUnloadRefusal&)
      -> BoardingRoutePortUnloadCellResult;
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
  friend auto boarding_route_port_unload_pressure(
      const BoardingRoutePortUnloadCellToken&,
      const BoardingRoutePortUnloadLimits&, BoardingRoutePortUnloadCounters&,
      BoardingRoutePortUnloadCell&, BoardingRoutePortUnloadRefusal&)
      -> BoardingRoutePortUnloadCellResult;
};
class BoardingRoutePortUnloadCellToken {
 private:
  BoardingRoutePortUnloadCellToken(const BoardingRoutePortUnloadContext& c,
                                   const BoardingRoutePortUnloadCell& cell)
      : context_(&c), cell_(&cell) {}
  const BoardingRoutePortUnloadContext* context_;
  const BoardingRoutePortUnloadCell* cell_;
  friend auto boarding_route_port_unload_cell(
      const BoardingRoutePortUnloadContext&, double, double, bool,
      const BoardingRoutePortUnloadLimits&, BoardingRoutePortUnloadCounters&,
      BoardingRoutePortUnloadCell&, BoardingRoutePortUnloadRefusal&)
      -> BoardingRoutePortUnloadCellResult;
  friend auto boarding_route_port_unload_pressure(
      const BoardingRoutePortUnloadCellToken&,
      const BoardingRoutePortUnloadLimits&, BoardingRoutePortUnloadCounters&,
      BoardingRoutePortUnloadCell&, BoardingRoutePortUnloadRefusal&)
      -> BoardingRoutePortUnloadCellResult;
};
} // namespace apsis_drift::detail
namespace apsis_drift::detail {
struct BoardingRoutePortUnloadPressureMath {
  std::array<BoardingPlantedLegScalarBounds, 2> barycenter_xz, delta_xz;
  std::array<std::array<BoardingPlantedLegScalarBounds, 2>, 2> pressure_xz;
  bool arithmetic_supported{}, selected_plane_equal{};
  static constexpr bool source_qualified{false}, body_qualified{false},
      self_qualified{false}, support_qualified{false}, actor_qualified{false};
};
[[nodiscard]] auto boarding_route_port_unload_pressure_math(
    std::array<BoardingPlantedLegScalarBounds, 2> com,
    std::array<std::array<BoardingPlantedLegScalarBounds, 2>, 2> centers,
    BoardingPlantedLegScalarBounds port_fraction,
    std::array<double, 2> sole_planes, double selected_source_plane,
    std::size_t selected_side)
    -> std::expected<BoardingRoutePortUnloadPressureMath, std::string>;
struct BoardingRoutePortUnloadNumericSolid {
  BoardingSourceEndpointSelfShape shape{};
  BoardingPlantedLegPointBounds first, second;
  std::array<BoardingPlantedLegPointBounds, 3> columns;
  RigidVector3 half_size_metres;
  double radius_metres{};
};
// Encloses h(n), the SUPPORT MAXIMUM; this is not a whole projection range.
struct BoardingRoutePortUnloadSupportMath {
  BoardingPlantedLegScalarBounds support;
  bool arithmetic_supported{};
  static constexpr bool source_qualified{false}, body_qualified{false},
      self_qualified{false}, support_qualified{false}, actor_qualified{false};
};
[[nodiscard]] auto boarding_route_port_unload_support_math(
    const BoardingRoutePortUnloadNumericSolid&, RigidVector3 direction)
    -> std::expected<BoardingRoutePortUnloadSupportMath, std::string>;
} // namespace apsis_drift::detail
