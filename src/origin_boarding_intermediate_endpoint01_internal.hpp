#pragma once
#include "apsis_drift/origin_boarding_intermediate_endpoint01.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
namespace apsis_drift::detail {
struct BoardingIntermediateEndpoint01Limits {
  BoardingIntermediatePausePhaseLimits phase;
  std::size_t source_guards{64}, projection_guards{256}, definition_guards{31},
      sole_extrema{4}, source_coordinates{8}, intersection_operations{4},
      midpoint_operations{4}, allocation_operations{6}, pressure_candidates{2},
      disk_edges{16}, self_body_guards{63}, self_pairs{105}, self_axes{1470},
      self_signed_trials{2940}, self_owners{14}, self_hip_complements{2},
      unit_axis_operations{24}, output_bytes{16777216};
};
[[nodiscard]] auto intermediate_endpoint01_request(
    BoardingIntermediateEndpoint01Candidate)
    -> std::optional<BoardingRouteFootPhaseRequest>;
[[nodiscard]] auto intermediate_endpoint01_bounded(
    const OriginBoardingIntermediatePauseSupport&,
    BoardingIntermediateEndpoint01Candidate,
    BoardingIntermediateEndpoint01Limits = {})
    -> BoardingIntermediateEndpoint01Expected;
class BoardingIntermediateEndpoint01CurrentToken;
class BoardingIntermediateEndpoint01AdmissionContext {
 private:
  BoardingIntermediateEndpoint01AdmissionContext(
      const BoardingIntermediateEndpoint01Diagnostic& d,
      const BoardingRouteFootPhaseRequest& r)
      : owner_(&d),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)),
        parts_(&d.parts), request_(&r) {}
  const BoardingIntermediateEndpoint01Diagnostic* owner_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  const std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount>*
      parts_;
  const BoardingRouteFootPhaseRequest* request_;
  friend auto intermediate_endpoint01_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingIntermediateEndpoint01Candidate,
      BoardingIntermediateEndpoint01Limits)
      -> BoardingIntermediateEndpoint01Expected;

 public:
  [[nodiscard]] auto owner() const
      -> const BoardingIntermediateEndpoint01Diagnostic* {
    return owner_;
  }
  [[nodiscard]] auto data() const
      -> const OriginBoardingIntermediatePauseSupport::Data* {
    return data_;
  }
  [[nodiscard]] auto parts() const -> const
      std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount>* {
    return parts_;
  }
  [[nodiscard]] auto request() const -> const BoardingRouteFootPhaseRequest* {
    return request_;
  }
};
class BoardingIntermediateEndpoint01CurrentToken {
 private:
  BoardingIntermediateEndpoint01CurrentToken(
      const BoardingIntermediateEndpoint01AdmissionContext& x,
      const BoardingIntermediateEndpoint01Diagnostic& d,
      const BoardingIntermediateEndpoint01Cell& c,
      const BoardingRouteFootPhaseRequest& r)
      : context_(&x), owner_(&d), cell_(&c), request_(&r),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)) {}
  const BoardingIntermediateEndpoint01AdmissionContext* context_;
  const BoardingIntermediateEndpoint01Diagnostic* owner_;
  const BoardingIntermediateEndpoint01Cell* cell_;
  const BoardingRouteFootPhaseRequest* request_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  friend auto intermediate_endpoint01_current_cell(
      const BoardingIntermediateEndpoint01AdmissionContext&,
      BoardingIntermediateEndpoint01Diagnostic&,
      BoardingIntermediateEndpoint01Cell&,
      const BoardingIntermediateEndpoint01Limits&,
      BoardingIntermediateEndpoint01Refusal&)
      -> BoardingIntermediateEndpoint01State;

 public:
  [[nodiscard]] auto context() const
      -> const BoardingIntermediateEndpoint01AdmissionContext* {
    return context_;
  }
  [[nodiscard]] auto owner() const
      -> const BoardingIntermediateEndpoint01Diagnostic* {
    return owner_;
  }
  [[nodiscard]] auto cell() const -> const BoardingIntermediateEndpoint01Cell* {
    return cell_;
  }
  [[nodiscard]] auto request() const -> const BoardingRouteFootPhaseRequest* {
    return request_;
  }
  [[nodiscard]] auto data() const
      -> const OriginBoardingIntermediatePauseSupport::Data* {
    return data_;
  }
};
auto intermediate_endpoint01_current_cell(
    const BoardingIntermediateEndpoint01AdmissionContext&,
    BoardingIntermediateEndpoint01Diagnostic&,
    BoardingIntermediateEndpoint01Cell&,
    const BoardingIntermediateEndpoint01Limits&,
    BoardingIntermediateEndpoint01Refusal&)
    -> BoardingIntermediateEndpoint01State;
auto intermediate_endpoint01_refuse(BoardingIntermediateEndpoint01Diagnostic&,
                                    BoardingIntermediateEndpoint01Refusal&,
                                    BoardingIntermediateEndpoint01Condition)
    -> bool;
auto intermediate_endpoint01_charge(BoardingIntermediateEndpoint01Diagnostic&,
                                    BoardingIntermediateEndpoint01Refusal&,
                                    std::uint64_t&, std::size_t,
                                    BoardingIntermediateEndpoint01Condition)
    -> bool;
auto intermediate_endpoint01_definition_charge(
    BoardingIntermediateEndpoint01Diagnostic&,
    BoardingIntermediateEndpoint01Cell&,
    const BoardingIntermediateEndpoint01Limits&,
    BoardingIntermediateEndpoint01Refusal&) -> bool;
auto intermediate_endpoint01_pressure_bridge(
    const BoardingIntermediateEndpoint01CurrentToken&,
    BoardingIntermediateEndpoint01Diagnostic&,
    BoardingIntermediateEndpoint01Cell&,
    const BoardingIntermediateEndpoint01Limits&,
    BoardingIntermediateEndpoint01Refusal&)
    -> BoardingIntermediateEndpoint01State;
auto intermediate_endpoint01_enroll_self_source(
    BoardingIntermediateEndpoint01Diagnostic&,
    const BoardingIntermediateEndpoint01Limits&,
    BoardingIntermediateEndpoint01Refusal&) -> bool;
auto intermediate_endpoint01_source_request_valid(
    const BoardingRouteFootPhaseRequest&) -> bool;
auto intermediate_endpoint01_self_bridge(
    const BoardingIntermediateEndpoint01CurrentToken&,
    BoardingIntermediateEndpoint01Diagnostic&,
    BoardingIntermediateEndpoint01Cell&,
    const BoardingIntermediateEndpoint01Limits&,
    BoardingIntermediateEndpoint01Refusal&)
    -> BoardingIntermediateEndpoint01State;
} // namespace apsis_drift::detail
