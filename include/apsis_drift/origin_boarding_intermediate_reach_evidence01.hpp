#pragma once
#include "apsis_drift/origin_boarding_intermediate_endpoint01.hpp"
#include "apsis_drift/origin_boarding_route_foot_phase.hpp"
#include <expected>
#include <string>
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingIntermediateReachEvidence01Version{1};
inline constexpr std::size_t
    kBoardingIntermediateReachEvidence01MaximumExpectedBytes{384},
    kBoardingIntermediateReachEvidence01MaximumScratchBytes{49152},
    kBoardingIntermediateReachEvidence01MaximumOutputBytes{16777216};
enum class BoardingIntermediateReachEvidence01State : std::uint8_t {
  not_run = 0,
  evidence = 1,
  unavailable = 2,
  unsupported = 3,
  capacity = 4
};
enum class BoardingIntermediateReachEvidence01Condition : std::uint8_t {
  none = 0,
  invalid_binding = 1,
  source_identity = 2,
  capture_identity = 3,
  evidence_unavailable = 4,
  unsupported_arithmetic = 5,
  capture_capacity = 6,
  threshold_capacity = 7,
  comparison_capacity = 8,
  output_capacity = 9,
  original_capacity = 10,
  original_unsupported = 11,
  inconsistent_evidence = 12,
  allocation_failure = 13
};
enum class BoardingIntermediateReachEvidence01Kind : std::uint8_t {
  not_run = 0,
  reach_refusal = 1,
  intentional_body_stop = 2
};
enum class BoardingIntermediateReachEvidence01Classification : std::uint8_t {
  not_run = 0,
  sufficient_inclusion = 1,
  strict_too_long = 2,
  strict_too_short = 3,
  unresolved_enclosure = 4
};
enum class BoardingIntermediateReachEvidence01Stage : std::uint8_t {
  not_run = 0,
  preflight = 1,
  source = 2,
  output = 3,
  phase = 4,
  capture = 5,
  threshold = 6,
  comparison = 7
};
enum class BoardingIntermediateReachEvidence01OriginalResult : std::uint8_t {
  not_run = 0,
  accepted = 1,
  unresolved = 2,
  unsupported = 3,
  capacity = 4
};
struct BoardingIntermediateReachEvidence01Bounds {
  double lower{}, upper{};
  bool supported{};
};
struct BoardingIntermediateReachEvidence01Work {
  std::uint64_t preflight_guards{}, source_guards{}, phase_calls{},
      capture_guards{}, threshold_operations{}, comparison_operations{};
  BoardingRouteFootPhaseCounters phase;
};
struct BoardingIntermediateReachEvidence01Diagnostic {
  std::uint32_t version{kBoardingIntermediateReachEvidence01Version};
  OriginBoardingIntermediatePauseSupport source;
  BoardingIntermediateEndpoint01Candidate candidate{};
  std::vector<BoardingRouteFootPhaseCell> cells;
  BoardingRouteFootPhaseRefusal phase_reason;
  BoardingIntermediateReachEvidence01Work work;
  BoardingIntermediateReachEvidence01Bounds distance_squared, minimum_squared,
      maximum_squared;
  std::uint64_t source_evaluated{};
  std::size_t output_capacity_bytes{}, required_output_bytes{};
  double actual_first{0}, actual_last{1};
  BoardingIntermediateReachEvidence01OriginalResult original_result{};
  BoardingIntermediateReachEvidence01State state{};
  BoardingIntermediateReachEvidence01Condition stop_condition{};
  BoardingIntermediateReachEvidence01Kind kind{};
  BoardingIntermediateReachEvidence01Classification classification{};
  BoardingIntermediateReachEvidence01Stage stop_stage{};
  std::uint8_t stop_operation{255}, source_operation{255},
      capture_operation{255}, threshold_operation{255},
      comparison_operation{255}, captured_side{255};
  std::uint8_t capture_attempted{}, capture_written{}, threshold_attempted{},
      threshold_written{}, comparison_attempted{}, comparison_written{};
  std::array<bool, 4> comparisons{};
  bool source_admitted{}, phase_invoked{}, evidence_complete{},
      arithmetic_supported{};
  explicit BoardingIntermediateReachEvidence01Diagnostic(
      const OriginBoardingIntermediatePauseSupport& p)
      : source(p) {}
  static constexpr bool body_qualified{false}, source_qualified{false},
      support_qualified{false}, load_qualified{false}, self_qualified{false},
      material_qualified{false}, world_qualified{false}, route_qualified{false},
      seat_qualified{false}, actor_qualified{false}, save_qualified{false},
      dynamics_qualified{false};
};
using BoardingIntermediateReachEvidence01Expected =
    std::expected<BoardingIntermediateReachEvidence01Diagnostic, std::string>;
[[nodiscard]] auto assess_origin_boarding_intermediate_reach_evidence01(
    const OriginBoardingIntermediatePauseSupport&,
    BoardingIntermediateEndpoint01Candidate =
        BoardingIntermediateEndpoint01Candidate::authored_descent_midpoint)
    -> BoardingIntermediateReachEvidence01Expected;
} // namespace apsis_drift
