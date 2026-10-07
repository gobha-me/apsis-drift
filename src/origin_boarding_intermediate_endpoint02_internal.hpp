#pragma once
#include "apsis_drift/origin_boarding_intermediate_endpoint02.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
namespace apsis_drift::detail {
struct BoardingIntermediateEndpoint02Limits {
  BoardingIntermediatePausePhaseLimits phase;
  std::size_t source_guards{64}, projection_guards{256}, definition_guards{31},
      sole_extrema{4}, source_coordinates{8}, intersection_operations{4},
      midpoint_operations{4}, allocation_operations{6}, pressure_candidates{2},
      disk_edges{16}, self_body_guards{63}, self_pairs{105}, self_axes{1470},
      self_signed_trials{2940}, self_owners{14}, self_hip_complements{2},
      unit_axis_operations{24}, output_bytes{16777216}, construction_guards{18},
      construction_operations{64};
};
[[nodiscard]] auto intermediate_endpoint02_bounded(
    const OriginBoardingIntermediatePauseSupport&,
    BoardingIntermediateEndpoint02Candidate,
    BoardingIntermediateEndpoint02Limits = {})
    -> BoardingIntermediateEndpoint02Expected;
class BoardingIntermediateEndpoint02ProgramKey {
 private:
  BoardingIntermediateEndpoint02ProgramKey(
      double y, BoardingIntermediateEndpoint02Candidate candidate)
      : y_(y), version_(2), candidate_(candidate) {}
  double y_;
  std::uint32_t version_;
  BoardingIntermediateEndpoint02Candidate candidate_;
  friend auto intermediate_endpoint02_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingIntermediateEndpoint02Candidate,
      BoardingIntermediateEndpoint02Limits)
      -> BoardingIntermediateEndpoint02Expected;

 public:
  [[nodiscard]] auto y() const -> double { return y_; }
  [[nodiscard]] auto version() const -> std::uint32_t { return version_; }
  [[nodiscard]] auto candidate() const
      -> BoardingIntermediateEndpoint02Candidate {
    return candidate_;
  }
};
class BoardingIntermediateEndpoint02CurrentToken;
class BoardingIntermediateEndpoint02AdmissionContext {
 private:
  BoardingIntermediateEndpoint02AdmissionContext(
      const BoardingIntermediateEndpoint02Diagnostic& d,
      const BoardingRouteFootPhaseRequest& r,
      const BoardingIntermediateEndpoint02ProgramKey& key)
      : owner_(&d),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)),
        parts_(&d.parts), request_(&r), key_(&key) {}
  const BoardingIntermediateEndpoint02Diagnostic* owner_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  const std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount>*
      parts_;
  const BoardingRouteFootPhaseRequest* request_;
  const BoardingIntermediateEndpoint02ProgramKey* key_;
  friend auto intermediate_endpoint02_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingIntermediateEndpoint02Candidate,
      BoardingIntermediateEndpoint02Limits)
      -> BoardingIntermediateEndpoint02Expected;

 public:
  BoardingIntermediateEndpoint02AdmissionContext(
      const BoardingIntermediateEndpoint02AdmissionContext&) = delete;
  BoardingIntermediateEndpoint02AdmissionContext(
      BoardingIntermediateEndpoint02AdmissionContext&&) = delete;
  auto operator=(const BoardingIntermediateEndpoint02AdmissionContext&)
      -> BoardingIntermediateEndpoint02AdmissionContext& = delete;
  auto operator=(BoardingIntermediateEndpoint02AdmissionContext&&)
      -> BoardingIntermediateEndpoint02AdmissionContext& = delete;
  [[nodiscard]] auto key() const
      -> const BoardingIntermediateEndpoint02ProgramKey* {
    return key_;
  }
  [[nodiscard]] auto owner() const
      -> const BoardingIntermediateEndpoint02Diagnostic* {
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
class BoardingIntermediateEndpoint02CurrentToken {
 private:
  BoardingIntermediateEndpoint02CurrentToken(
      const BoardingIntermediateEndpoint02AdmissionContext& x,
      const BoardingIntermediateEndpoint02Diagnostic& d,
      const BoardingIntermediateEndpoint02Cell& c,
      const BoardingRouteFootPhaseRequest& r)
      : context_(&x), owner_(&d), cell_(&c), request_(&r),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)) {}
  const BoardingIntermediateEndpoint02AdmissionContext* context_;
  const BoardingIntermediateEndpoint02Diagnostic* owner_;
  const BoardingIntermediateEndpoint02Cell* cell_;
  const BoardingRouteFootPhaseRequest* request_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  friend auto intermediate_endpoint02_current_cell(
      const BoardingIntermediateEndpoint02AdmissionContext&,
      BoardingIntermediateEndpoint02Diagnostic&,
      BoardingIntermediateEndpoint02Cell&,
      const BoardingIntermediateEndpoint02Limits&,
      BoardingIntermediateEndpoint02Refusal&)
      -> BoardingIntermediateEndpoint02State;

 public:
  [[nodiscard]] auto context() const
      -> const BoardingIntermediateEndpoint02AdmissionContext* {
    return context_;
  }
  [[nodiscard]] auto owner() const
      -> const BoardingIntermediateEndpoint02Diagnostic* {
    return owner_;
  }
  [[nodiscard]] auto cell() const -> const BoardingIntermediateEndpoint02Cell* {
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
auto intermediate_endpoint02_current_cell(
    const BoardingIntermediateEndpoint02AdmissionContext&,
    BoardingIntermediateEndpoint02Diagnostic&,
    BoardingIntermediateEndpoint02Cell&,
    const BoardingIntermediateEndpoint02Limits&,
    BoardingIntermediateEndpoint02Refusal&)
    -> BoardingIntermediateEndpoint02State;
auto intermediate_endpoint02_refuse(BoardingIntermediateEndpoint02Diagnostic&,
                                    BoardingIntermediateEndpoint02Refusal&,
                                    BoardingIntermediateEndpoint02Condition)
    -> bool;
auto intermediate_endpoint02_charge(BoardingIntermediateEndpoint02Diagnostic&,
                                    BoardingIntermediateEndpoint02Refusal&,
                                    std::uint64_t&, std::size_t,
                                    BoardingIntermediateEndpoint02Condition)
    -> bool;
auto intermediate_endpoint02_definition_charge(
    BoardingIntermediateEndpoint02Diagnostic&,
    BoardingIntermediateEndpoint02Cell&,
    const BoardingIntermediateEndpoint02Limits&,
    BoardingIntermediateEndpoint02Refusal&) -> bool;
auto intermediate_endpoint02_pressure_bridge(
    const BoardingIntermediateEndpoint02CurrentToken&,
    BoardingIntermediateEndpoint02Diagnostic&,
    BoardingIntermediateEndpoint02Cell&,
    const BoardingIntermediateEndpoint02Limits&,
    BoardingIntermediateEndpoint02Refusal&)
    -> BoardingIntermediateEndpoint02State;
auto intermediate_endpoint02_source_enroll(
    BoardingIntermediateEndpoint02Diagnostic&, BoardingRouteFootPhaseRequest&,
    const BoardingIntermediateEndpoint02Limits&,
    BoardingIntermediateEndpoint02Refusal&) -> bool;
auto intermediate_endpoint02_construct(
    BoardingIntermediateEndpoint02Diagnostic&, BoardingRouteFootPhaseRequest&,
    const BoardingIntermediateEndpoint02Limits&,
    BoardingIntermediateEndpoint02Refusal&) -> bool;
auto intermediate_endpoint02_canonical(
    const BoardingIntermediateEndpoint02ProgramKey&)
    -> std::optional<BoardingRouteFootPhaseRequest>;
auto intermediate_endpoint02_template_valid(
    const BoardingRouteFootPhaseRequest&, bool generated, double y) -> bool;
auto intermediate_endpoint02_self_bridge(
    const BoardingIntermediateEndpoint02CurrentToken&,
    BoardingIntermediateEndpoint02Diagnostic&,
    BoardingIntermediateEndpoint02Cell&,
    const BoardingIntermediateEndpoint02Limits&,
    BoardingIntermediateEndpoint02Refusal&)
    -> BoardingIntermediateEndpoint02State;
} // namespace apsis_drift::detail
