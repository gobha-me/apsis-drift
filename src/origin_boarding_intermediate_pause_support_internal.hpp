#pragma once
#include "apsis_drift/origin_boarding_intermediate_pause_support.hpp"
#include "origin_boarding_route_foot_phase_internal.hpp"
namespace apsis_drift::detail {
struct BoardingIntermediatePauseConstructorLimits {
  std::size_t source_bytes{4096}, base_guards{64}, metadata_rows{1},
      face_reads{2}, quad_records{2}, quad_edges{8}, quad_sides{16},
      face_vertices{12}, corner_matches{48}, face_windings{4}, incidence{8},
      diagonals{2};
};
struct BoardingIntermediatePausePhaseLimits {
  std::uint64_t graphs{1}, legs{2}, bodies{1}, sectors{3}, timing{6};
};
struct BoardingIntermediatePauseLimits {
  BoardingIntermediatePausePhaseLimits phase;
  std::size_t projection_guards{256}, pressure_candidates{2}, disk_edges{16},
      output_bytes{4096};
};
struct BoardingIntermediatePauseSourceFixture {
  std::array<BoardingBootSourcePartition, 2> partitions;
  std::array<BoardingIntermediatePauseSourceIdentity, 2> identities;
};
struct BoardingIntermediatePauseSourceMath {
  BoardingIntermediatePauseConstructorEvidence evidence;
  static constexpr bool source_qualified{false}, body_qualified{false},
      support_qualified{false}, world_qualified{false}, actor_qualified{false};
};
struct BoardingIntermediatePausePressureMath {
  std::array<BoardingPlantedLegScalarBounds, 2> barycenter_xz, delta_xz;
  std::array<std::array<BoardingPlantedLegScalarBounds, 2>, 2> pressure_xz;
  bool arithmetic_supported{};
  static constexpr bool source_qualified{false}, body_qualified{false},
      support_qualified{false}, world_qualified{false}, actor_qualified{false};
};
struct BoardingIntermediatePauseDiskMath {
  std::array<BoardingFootSiteEdgeEvidence, 4> edges;
  std::array<bool, 4> evaluated{};
  std::size_t edge_checks{};
  BoardingIntermediatePauseDiskStatus status{};
  BoardingIntermediatePauseCondition condition{};
  bool arithmetic_supported{};
  static constexpr bool source_qualified{false}, body_qualified{false},
      support_qualified{false}, world_qualified{false}, actor_qualified{false};
};
struct BoardingIntermediatePauseSupportAccess {
  static auto make(const NativeCraftBinding&, const OriginBoardingBootSupport&,
                   BoardingIntermediatePauseConstructorLimits = {},
                   BoardingIntermediatePauseConstructorEvidence* = nullptr)
      -> std::expected<OriginBoardingIntermediatePauseSupport, std::string>;
  static auto valid(const OriginBoardingIntermediatePauseSupport&) -> bool;
  static auto data(const OriginBoardingIntermediatePauseSupport&)
      -> const OriginBoardingIntermediatePauseSupport::Data*;
  static auto partition(const OriginBoardingIntermediatePauseSupport&,
                        std::size_t) -> const BoardingBootSourcePartition*;
  static auto fixture(const OriginBoardingIntermediatePauseSupport&)
      -> std::optional<BoardingIntermediatePauseSourceFixture>;
};
[[nodiscard]] auto intermediate_pause_support_bounded(
    const OriginBoardingIntermediatePauseSupport&, bool reverse,
    BoardingIntermediatePauseLimits = {})
    -> std::expected<BoardingIntermediatePauseSupportDiagnostic, std::string>;
class BoardingIntermediatePauseProjectionToken {
 private:
  BoardingIntermediatePauseProjectionToken(
      const OriginBoardingIntermediatePauseSupport& p,
      const BoardingRouteFootPhaseCell& c)
      : provider_(&p), cell_(&c) {}
  const OriginBoardingIntermediatePauseSupport* provider_;
  const BoardingRouteFootPhaseCell* cell_;
  friend auto intermediate_pause_support_bounded(
      const OriginBoardingIntermediatePauseSupport&, bool,
      BoardingIntermediatePauseLimits)
      -> std::expected<BoardingIntermediatePauseSupportDiagnostic, std::string>;
  friend auto intermediate_pause_pressure_bridge(
      const BoardingIntermediatePauseProjectionToken&,
      const BoardingIntermediatePauseLimits&,
      BoardingIntermediatePauseSupportDiagnostic&) -> void;
};
[[nodiscard]] auto intermediate_pause_source_math(
    const BoardingIntermediatePauseSourceFixture&,
    BoardingIntermediatePauseConstructorLimits = {})
    -> BoardingIntermediatePauseSourceMath;
[[nodiscard]] auto intermediate_pause_pressure_math(
    std::array<BoardingPlantedLegScalarBounds, 2> com,
    std::array<std::array<BoardingPlantedLegScalarBounds, 2>, 2> centers,
    BoardingPlantedLegScalarBounds fraction)
    -> BoardingIntermediatePausePressureMath;
[[nodiscard]] auto intermediate_pause_disk_math(
    std::array<RigidVector3, 4> perimeter,
    std::array<BoardingPlantedLegScalarBounds, 2> pressure,
    std::size_t max_edges = 4) -> BoardingIntermediatePauseDiskMath;
// Purpose-private shared arithmetic; no supplied geometry mints a source token.
[[nodiscard]] auto intermediate_pause_quad_bridge(
    const BoardingBootSourcePartition&,
    const BoardingIntermediatePauseConstructorLimits&,
    BoardingIntermediatePauseConstructorEvidence&, std::size_t side) -> bool;
auto intermediate_pause_pressure_bridge(
    const BoardingIntermediatePauseProjectionToken&,
    const BoardingIntermediatePauseLimits&,
    BoardingIntermediatePauseSupportDiagnostic&) -> void;
} // namespace apsis_drift::detail
