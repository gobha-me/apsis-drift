#pragma once
#include "apsis_drift/origin_boarding_intermediate_endpoint05.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
namespace apsis_drift::detail {
struct BoardingIntermediateEndpoint05Limits {
  BoardingIntermediatePausePhaseLimits phase;
  std::size_t source_guards{64}, projection_guards{256}, definition_guards{31},
      sole_extrema{4}, source_coordinates{8}, intersection_operations{4},
      midpoint_operations{4}, allocation_operations{6}, pressure_candidates{2},
      disk_edges{16}, self_body_guards{63}, self_pairs{105}, self_axes{1470},
      self_signed_trials{2940}, self_owners{14}, self_hip_complements{2},
      unit_axis_operations{24}, output_bytes{16777216}, construction_guards{50},
      construction_operations{343};
};
[[nodiscard]] auto intermediate_endpoint05_bounded(
    const OriginBoardingIntermediatePauseSupport&,
    BoardingIntermediateEndpoint05Candidate,
    BoardingIntermediateEndpoint05Limits = {})
    -> BoardingIntermediateEndpoint05Expected;
class BoardingIntermediateEndpoint05ProgramKey {
 private:
  BoardingIntermediateEndpoint05ProgramKey(
      double y, BoardingIntermediateEndpoint05Candidate candidate)
      : y_(y), version_(5), candidate_(candidate) {}
  double y_;
  std::uint32_t version_;
  BoardingIntermediateEndpoint05Candidate candidate_;
  friend auto intermediate_endpoint05_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingIntermediateEndpoint05Candidate,
      BoardingIntermediateEndpoint05Limits)
      -> BoardingIntermediateEndpoint05Expected;

 public:
  [[nodiscard]] auto y() const -> double { return y_; }
  [[nodiscard]] auto version() const -> std::uint32_t { return version_; }
  [[nodiscard]] auto candidate() const
      -> BoardingIntermediateEndpoint05Candidate {
    return candidate_;
  }
};
class BoardingIntermediateEndpoint05CurrentToken;
class BoardingIntermediateEndpoint05AdmissionContext {
 private:
  BoardingIntermediateEndpoint05AdmissionContext(
      const BoardingIntermediateEndpoint05Diagnostic& d,
      const BoardingRouteFootPhaseRequest& r,
      const BoardingIntermediateEndpoint05ProgramKey& key)
      : owner_(&d),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)),
        parts_(&d.parts), request_(&r), key_(&key) {}
  const BoardingIntermediateEndpoint05Diagnostic* owner_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  const std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount>*
      parts_;
  const BoardingRouteFootPhaseRequest* request_;
  const BoardingIntermediateEndpoint05ProgramKey* key_;
  friend auto intermediate_endpoint05_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingIntermediateEndpoint05Candidate,
      BoardingIntermediateEndpoint05Limits)
      -> BoardingIntermediateEndpoint05Expected;

 public:
  BoardingIntermediateEndpoint05AdmissionContext(
      const BoardingIntermediateEndpoint05AdmissionContext&) = delete;
  BoardingIntermediateEndpoint05AdmissionContext(
      BoardingIntermediateEndpoint05AdmissionContext&&) = delete;
  auto operator=(const BoardingIntermediateEndpoint05AdmissionContext&)
      -> BoardingIntermediateEndpoint05AdmissionContext& = delete;
  auto operator=(BoardingIntermediateEndpoint05AdmissionContext&&)
      -> BoardingIntermediateEndpoint05AdmissionContext& = delete;
  [[nodiscard]] auto key() const
      -> const BoardingIntermediateEndpoint05ProgramKey* {
    return key_;
  }
  [[nodiscard]] auto owner() const
      -> const BoardingIntermediateEndpoint05Diagnostic* {
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
class BoardingIntermediateEndpoint05CurrentToken {
 private:
  BoardingIntermediateEndpoint05CurrentToken(
      const BoardingIntermediateEndpoint05AdmissionContext& x,
      const BoardingIntermediateEndpoint05Diagnostic& d,
      const BoardingIntermediateEndpoint05Cell& c,
      const BoardingRouteFootPhaseRequest& r)
      : context_(&x), owner_(&d), cell_(&c), request_(&r),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)) {}
  const BoardingIntermediateEndpoint05AdmissionContext* context_;
  const BoardingIntermediateEndpoint05Diagnostic* owner_;
  const BoardingIntermediateEndpoint05Cell* cell_;
  const BoardingRouteFootPhaseRequest* request_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  friend auto intermediate_endpoint05_current_cell(
      const BoardingIntermediateEndpoint05AdmissionContext&,
      BoardingIntermediateEndpoint05Diagnostic&,
      BoardingIntermediateEndpoint05Cell&,
      const BoardingIntermediateEndpoint05Limits&,
      BoardingIntermediateEndpoint05Refusal&)
      -> BoardingIntermediateEndpoint05State;

 public:
  [[nodiscard]] auto context() const
      -> const BoardingIntermediateEndpoint05AdmissionContext* {
    return context_;
  }
  [[nodiscard]] auto owner() const
      -> const BoardingIntermediateEndpoint05Diagnostic* {
    return owner_;
  }
  [[nodiscard]] auto cell() const -> const BoardingIntermediateEndpoint05Cell* {
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
auto intermediate_endpoint05_current_cell(
    const BoardingIntermediateEndpoint05AdmissionContext&,
    BoardingIntermediateEndpoint05Diagnostic&,
    BoardingIntermediateEndpoint05Cell&,
    const BoardingIntermediateEndpoint05Limits&,
    BoardingIntermediateEndpoint05Refusal&)
    -> BoardingIntermediateEndpoint05State;
auto intermediate_endpoint05_refuse(BoardingIntermediateEndpoint05Diagnostic&,
                                    BoardingIntermediateEndpoint05Refusal&,
                                    BoardingIntermediateEndpoint05Condition)
    -> bool;
auto intermediate_endpoint05_charge(BoardingIntermediateEndpoint05Diagnostic&,
                                    BoardingIntermediateEndpoint05Refusal&,
                                    std::uint64_t&, std::size_t,
                                    BoardingIntermediateEndpoint05Condition)
    -> bool;
auto intermediate_endpoint05_definition_charge(
    BoardingIntermediateEndpoint05Diagnostic&,
    BoardingIntermediateEndpoint05Cell&,
    const BoardingIntermediateEndpoint05Limits&,
    BoardingIntermediateEndpoint05Refusal&) -> bool;
auto intermediate_endpoint05_pressure_bridge(
    const BoardingIntermediateEndpoint05CurrentToken&,
    BoardingIntermediateEndpoint05Diagnostic&,
    BoardingIntermediateEndpoint05Cell&,
    const BoardingIntermediateEndpoint05Limits&,
    BoardingIntermediateEndpoint05Refusal&)
    -> BoardingIntermediateEndpoint05State;
auto intermediate_endpoint05_source_enroll(
    BoardingIntermediateEndpoint05Diagnostic&, BoardingRouteFootPhaseRequest&,
    const BoardingIntermediateEndpoint05Limits&,
    BoardingIntermediateEndpoint05Refusal&) -> bool;
auto intermediate_endpoint05_construct(
    BoardingIntermediateEndpoint05Diagnostic&, BoardingRouteFootPhaseRequest&,
    const BoardingIntermediateEndpoint05Limits&,
    BoardingIntermediateEndpoint05Refusal&) -> bool;
auto intermediate_endpoint05_canonical(
    const BoardingIntermediateEndpoint05ProgramKey&)
    -> std::optional<BoardingRouteFootPhaseRequest>;
auto intermediate_endpoint05_template_valid(
    const BoardingRouteFootPhaseRequest&, bool generated, double y) -> bool;
auto intermediate_endpoint05_self_bridge(
    const BoardingIntermediateEndpoint05CurrentToken&,
    BoardingIntermediateEndpoint05Diagnostic&,
    BoardingIntermediateEndpoint05Cell&,
    const BoardingIntermediateEndpoint05Limits&,
    BoardingIntermediateEndpoint05Refusal&)
    -> BoardingIntermediateEndpoint05State;
} // namespace apsis_drift::detail
