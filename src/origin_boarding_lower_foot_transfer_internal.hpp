#pragma once

#include "apsis_drift/origin_boarding_lower_foot_transfer.hpp"

namespace apsis_drift::detail {
struct BoardingLowerFootTransferLimits {
  std::size_t source_partitions{kBoardingBootSourcePartitionCount},
      body_records{kBoardingSourceEndpointMaximumRecords},
      initial_self_pairs{kBoardingBodyPairCount},
      initial_self_axes{kBoardingSourceEndpointSelfMaximumAxes},
      initial_pressure_partitions{kBoardingBootSourcePartitionCount},
      depth{kBoardingLowerFootTransferMaximumDepth},
      nodes{kBoardingLowerFootTransferMaximumNodes},
      leaves{kBoardingLowerFootTransferMaximumLeaves};
  std::uint64_t self_pairs{kBoardingLowerFootTransferMaximumPairs},
      proposed_axes{kBoardingLowerFootTransferMaximumAxes},
      signed_trials{kBoardingLowerFootTransferMaximumSignedTrials},
      pressure_candidates{kBoardingLowerFootTransferMaximumPressureCandidates},
      disk_edges{kBoardingLowerFootTransferMaximumEdges};
};
enum class BoardingLowerFootTransferCellResult : std::uint8_t {
  accepted,
  unresolved,
  unsupported,
  capacity
};
class BoardingLowerFootTransferPressureContext;
[[nodiscard]] auto prepare_boarding_lower_foot_transfer_pressure(
    const BoardingSourceEndpointLoadDiagnostic&)
    -> std::expected<BoardingLowerFootTransferPressureContext, std::string>;
class BoardingLowerFootTransferPressureContext {
 public:
  BoardingLowerFootTransferPressureContext(
      const BoardingLowerFootTransferPressureContext&) = default;
  auto operator=(const BoardingLowerFootTransferPressureContext&)
      -> BoardingLowerFootTransferPressureContext& = default;

 private:
  explicit BoardingLowerFootTransferPressureContext(
      const BoardingSourceEndpointLoadDiagnostic& initial)
      : initial_(&initial) {}
  const BoardingSourceEndpointLoadDiagnostic* initial_;
  friend auto prepare_boarding_lower_foot_transfer_pressure(
      const BoardingSourceEndpointLoadDiagnostic&)
      -> std::expected<BoardingLowerFootTransferPressureContext, std::string>;
  friend auto boarding_lower_foot_transfer_cell(
      const BoardingSourceEndpointLoadDiagnostic&,
      const BoardingLowerFootTransferPressureContext&, double, double, bool,
      const BoardingLowerFootTransferLimits&,
      BoardingLowerFootTransferCounters&, BoardingLowerFootTransferCell&,
      BoardingLowerFootTransferRefusal&) -> BoardingLowerFootTransferCellResult;
  friend auto boarding_lower_foot_transfer_pressure_cell(
      const BoardingLowerFootTransferPressureContext&,
      const BoardingLowerFootTransferLimits&,
      BoardingLowerFootTransferCounters&, BoardingLowerFootTransferCell&,
      BoardingLowerFootTransferRefusal&) -> BoardingLowerFootTransferCellResult;
};
// One purpose-specific cell graph and all predicates; no nested cover/child.
[[nodiscard]] auto boarding_lower_foot_transfer_cell(
    const BoardingSourceEndpointLoadDiagnostic&,
    const BoardingLowerFootTransferPressureContext&, double first, double last,
    bool reverse, const BoardingLowerFootTransferLimits&,
    BoardingLowerFootTransferCounters&, BoardingLowerFootTransferCell&,
    BoardingLowerFootTransferRefusal&) -> BoardingLowerFootTransferCellResult;
[[nodiscard]] auto boarding_lower_foot_transfer_pressure_cell(
    const BoardingLowerFootTransferPressureContext&,
    const BoardingLowerFootTransferLimits&, BoardingLowerFootTransferCounters&,
    BoardingLowerFootTransferCell&, BoardingLowerFootTransferRefusal&)
    -> BoardingLowerFootTransferCellResult;
[[nodiscard]] auto boarding_lower_foot_transfer_bounded(
    const OriginBoardingBootSupport&, double first, double last,
    BoardingLowerFootTransferLimits = {})
    -> std::expected<BoardingLowerFootTransferDiagnostic, std::string>;
// Only the registered compiled kinematic graph; no self/source permission.
[[nodiscard]] auto boarding_lower_foot_transfer_kinematic_math(
    double first, double last, bool reverse, BoardingLowerFootTransferCell&,
    BoardingLowerFootTransferRefusal&) -> BoardingLowerFootTransferCellResult;
} // namespace apsis_drift::detail
