#pragma once
#include "apsis_drift/origin_boarding_intermediate_reach_evidence01.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
namespace apsis_drift::detail {
struct BoardingIntermediateReachEvidence01Limits {
  std::size_t capture_guards{8}, threshold_operations{4},
      comparison_operations{4}, output_bytes{16777216};
};
[[nodiscard]] auto intermediate_reach_evidence01_bounded(
    const OriginBoardingIntermediatePauseSupport&,
    BoardingIntermediateEndpoint01Candidate,
    BoardingIntermediateReachEvidence01Limits = {})
    -> BoardingIntermediateReachEvidence01Expected;
struct BoardingIntermediateReachEvidence01FreshCallRecord {
  std::uint64_t calls{};
  bool completed{}, issued{};
};
class BoardingIntermediateReachEvidence01Context {
 private:
  BoardingIntermediateReachEvidence01Context(
      const BoardingIntermediateReachEvidence01Diagnostic& d,
      const BoardingRouteFootPhaseRequest& r,
      BoardingIntermediateReachEvidence01FreshCallRecord& call)
      : owner_(&d),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)),
        request_(&r), call_(&call) {}
  const BoardingIntermediateReachEvidence01Diagnostic* owner_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  const BoardingRouteFootPhaseRequest* request_;
  BoardingIntermediateReachEvidence01FreshCallRecord* call_;
  friend auto intermediate_reach_evidence01_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingIntermediateEndpoint01Candidate,
      BoardingIntermediateReachEvidence01Limits)
      -> BoardingIntermediateReachEvidence01Expected;

 public:
  BoardingIntermediateReachEvidence01Context(
      const BoardingIntermediateReachEvidence01Context&) = delete;
  BoardingIntermediateReachEvidence01Context(
      BoardingIntermediateReachEvidence01Context&&) = delete;
  auto operator=(const BoardingIntermediateReachEvidence01Context&)
      -> BoardingIntermediateReachEvidence01Context& = delete;
  auto operator=(BoardingIntermediateReachEvidence01Context&&)
      -> BoardingIntermediateReachEvidence01Context& = delete;
  [[nodiscard]] auto owner() const { return owner_; }
  [[nodiscard]] auto data() const { return data_; }
  [[nodiscard]] auto request() const { return request_; }
  [[nodiscard]] auto call() const
      -> const BoardingIntermediateReachEvidence01FreshCallRecord* {
    return call_;
  }
};
class BoardingIntermediateReachEvidence01CaptureToken {
 private:
  BoardingIntermediateReachEvidence01CaptureToken(
      const BoardingIntermediateReachEvidence01Context& x,
      const BoardingIntermediateReachEvidence01Diagnostic& d)
      : context_(&x), owner_(&d), cell_(&d.cells.front()),
        request_(x.request()), call_(x.call()) {}
  const BoardingIntermediateReachEvidence01Context* context_;
  const BoardingIntermediateReachEvidence01Diagnostic* owner_;
  const BoardingRouteFootPhaseCell* cell_;
  const BoardingRouteFootPhaseRequest* request_;
  const BoardingIntermediateReachEvidence01FreshCallRecord* call_;
  friend auto intermediate_reach_evidence01_phase_call(
      const BoardingIntermediateReachEvidence01Context&,
      BoardingIntermediateReachEvidence01Diagnostic&,
      BoardingIntermediateReachEvidence01FreshCallRecord&,
      std::optional<BoardingIntermediateReachEvidence01CaptureToken>&) -> bool;

 public:
  [[nodiscard]] auto context() const { return context_; }
  [[nodiscard]] auto owner() const { return owner_; }
  [[nodiscard]] auto cell() const { return cell_; }
  [[nodiscard]] auto request() const { return request_; }
  [[nodiscard]] auto call() const { return call_; }
};
auto intermediate_reach_evidence01_phase_call(
    const BoardingIntermediateReachEvidence01Context&,
    BoardingIntermediateReachEvidence01Diagnostic&,
    BoardingIntermediateReachEvidence01FreshCallRecord&,
    std::optional<BoardingIntermediateReachEvidence01CaptureToken>&) -> bool;
auto intermediate_reach_evidence01_refuse(
    BoardingIntermediateReachEvidence01Diagnostic&,
    BoardingIntermediateReachEvidence01Condition,
    BoardingIntermediateReachEvidence01Stage, std::uint8_t) -> bool;
auto intermediate_reach_evidence01_capture_charge(
    BoardingIntermediateReachEvidence01Diagnostic&,
    const BoardingIntermediateReachEvidence01Limits&, std::uint8_t) -> bool;
auto intermediate_reach_evidence01_token_valid(
    const BoardingIntermediateReachEvidence01CaptureToken&,
    const BoardingIntermediateReachEvidence01Diagnostic&) -> bool;
auto intermediate_reach_evidence01_thresholds(
    const BoardingIntermediateReachEvidence01CaptureToken&,
    BoardingIntermediateReachEvidence01Diagnostic&,
    const BoardingIntermediateReachEvidence01Limits&) -> bool;
} // namespace apsis_drift::detail
