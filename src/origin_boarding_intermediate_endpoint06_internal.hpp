#pragma once
#include "apsis_drift/origin_boarding_intermediate_endpoint06.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
namespace apsis_drift::detail {
struct BoardingIntermediateEndpoint06Limits {
  BoardingIntermediatePausePhaseLimits phase;
  std::size_t source_guards{64}, projection_guards{256}, definition_guards{31},
      sole_extrema{4}, source_coordinates{8}, intersection_operations{4},
      midpoint_operations{4}, allocation_operations{6}, pressure_candidates{2},
      disk_edges{16}, self_body_guards{63}, self_pairs{105}, self_axes{1470},
      self_signed_trials{2940}, self_owners{14}, self_hip_complements{2},
      unit_axis_operations{24}, output_bytes{16777216}, construction_guards{75},
      construction_operations{458};
};
[[nodiscard]] auto intermediate_endpoint06_bounded(
    const OriginBoardingIntermediatePauseSupport&,
    BoardingIntermediateEndpoint06Candidate,
    BoardingIntermediateEndpoint06Limits = {})
    -> BoardingIntermediateEndpoint06Expected;
class BoardingIntermediateEndpoint06ProgramKey {
 private:
  BoardingIntermediateEndpoint06ProgramKey(
      double y, BoardingIntermediateEndpoint06Candidate candidate)
      : y_(y), version_(6), candidate_(candidate) {}
  double y_;
  std::uint32_t version_;
  BoardingIntermediateEndpoint06Candidate candidate_;
  friend auto intermediate_endpoint06_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingIntermediateEndpoint06Candidate,
      BoardingIntermediateEndpoint06Limits)
      -> BoardingIntermediateEndpoint06Expected;

 public:
  [[nodiscard]] auto y() const -> double { return y_; }
  [[nodiscard]] auto version() const -> std::uint32_t { return version_; }
  [[nodiscard]] auto candidate() const
      -> BoardingIntermediateEndpoint06Candidate {
    return candidate_;
  }
};
class BoardingIntermediateEndpoint06CurrentToken;
class BoardingIntermediateEndpoint06AdmissionContext {
 private:
  BoardingIntermediateEndpoint06AdmissionContext(
      const BoardingIntermediateEndpoint06Diagnostic& d,
      const BoardingRouteFootPhaseRequest& r,
      const BoardingIntermediateEndpoint06ProgramKey& key)
      : owner_(&d),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)),
        parts_(&d.parts), request_(&r), key_(&key) {}
  const BoardingIntermediateEndpoint06Diagnostic* owner_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  const std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount>*
      parts_;
  const BoardingRouteFootPhaseRequest* request_;
  const BoardingIntermediateEndpoint06ProgramKey* key_;
  friend auto intermediate_endpoint06_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingIntermediateEndpoint06Candidate,
      BoardingIntermediateEndpoint06Limits)
      -> BoardingIntermediateEndpoint06Expected;

 public:
  BoardingIntermediateEndpoint06AdmissionContext(
      const BoardingIntermediateEndpoint06AdmissionContext&) = delete;
  BoardingIntermediateEndpoint06AdmissionContext(
      BoardingIntermediateEndpoint06AdmissionContext&&) = delete;
  auto operator=(const BoardingIntermediateEndpoint06AdmissionContext&)
      -> BoardingIntermediateEndpoint06AdmissionContext& = delete;
  auto operator=(BoardingIntermediateEndpoint06AdmissionContext&&)
      -> BoardingIntermediateEndpoint06AdmissionContext& = delete;
  [[nodiscard]] auto key() const
      -> const BoardingIntermediateEndpoint06ProgramKey* {
    return key_;
  }
  [[nodiscard]] auto owner() const
      -> const BoardingIntermediateEndpoint06Diagnostic* {
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
class BoardingIntermediateEndpoint06CurrentToken {
 private:
  BoardingIntermediateEndpoint06CurrentToken(
      const BoardingIntermediateEndpoint06AdmissionContext& x,
      const BoardingIntermediateEndpoint06Diagnostic& d,
      const BoardingIntermediateEndpoint06Cell& c,
      const BoardingRouteFootPhaseRequest& r)
      : context_(&x), owner_(&d), cell_(&c), request_(&r),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)) {}
  const BoardingIntermediateEndpoint06AdmissionContext* context_;
  const BoardingIntermediateEndpoint06Diagnostic* owner_;
  const BoardingIntermediateEndpoint06Cell* cell_;
  const BoardingRouteFootPhaseRequest* request_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  friend auto intermediate_endpoint06_current_cell(
      const BoardingIntermediateEndpoint06AdmissionContext&,
      BoardingIntermediateEndpoint06Diagnostic&,
      BoardingIntermediateEndpoint06Cell&,
      const BoardingIntermediateEndpoint06Limits&,
      BoardingIntermediateEndpoint06Refusal&)
      -> BoardingIntermediateEndpoint06State;

 public:
  [[nodiscard]] auto context() const
      -> const BoardingIntermediateEndpoint06AdmissionContext* {
    return context_;
  }
  [[nodiscard]] auto owner() const
      -> const BoardingIntermediateEndpoint06Diagnostic* {
    return owner_;
  }
  [[nodiscard]] auto cell() const -> const BoardingIntermediateEndpoint06Cell* {
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
auto intermediate_endpoint06_current_cell(
    const BoardingIntermediateEndpoint06AdmissionContext&,
    BoardingIntermediateEndpoint06Diagnostic&,
    BoardingIntermediateEndpoint06Cell&,
    const BoardingIntermediateEndpoint06Limits&,
    BoardingIntermediateEndpoint06Refusal&)
    -> BoardingIntermediateEndpoint06State;
auto intermediate_endpoint06_refuse(BoardingIntermediateEndpoint06Diagnostic&,
                                    BoardingIntermediateEndpoint06Refusal&,
                                    BoardingIntermediateEndpoint06Condition)
    -> bool;
auto intermediate_endpoint06_charge(BoardingIntermediateEndpoint06Diagnostic&,
                                    BoardingIntermediateEndpoint06Refusal&,
                                    std::uint64_t&, std::size_t,
                                    BoardingIntermediateEndpoint06Condition)
    -> bool;
auto intermediate_endpoint06_definition_charge(
    BoardingIntermediateEndpoint06Diagnostic&,
    BoardingIntermediateEndpoint06Cell&,
    const BoardingIntermediateEndpoint06Limits&,
    BoardingIntermediateEndpoint06Refusal&) -> bool;
auto intermediate_endpoint06_pressure_bridge(
    const BoardingIntermediateEndpoint06CurrentToken&,
    BoardingIntermediateEndpoint06Diagnostic&,
    BoardingIntermediateEndpoint06Cell&,
    const BoardingIntermediateEndpoint06Limits&,
    BoardingIntermediateEndpoint06Refusal&)
    -> BoardingIntermediateEndpoint06State;
auto intermediate_endpoint06_source_enroll(
    BoardingIntermediateEndpoint06Diagnostic&, BoardingRouteFootPhaseRequest&,
    const BoardingIntermediateEndpoint06Limits&,
    BoardingIntermediateEndpoint06Refusal&) -> bool;
auto intermediate_endpoint06_construct(
    BoardingIntermediateEndpoint06Diagnostic&, BoardingRouteFootPhaseRequest&,
    const BoardingIntermediateEndpoint06Limits&,
    BoardingIntermediateEndpoint06Refusal&) -> bool;
auto intermediate_endpoint06_canonical(
    const BoardingIntermediateEndpoint06ProgramKey&)
    -> std::optional<BoardingRouteFootPhaseRequest>;
auto intermediate_endpoint06_template_valid(
    const BoardingRouteFootPhaseRequest&, bool generated, double y) -> bool;
auto intermediate_endpoint06_self_bridge(
    const BoardingIntermediateEndpoint06CurrentToken&,
    BoardingIntermediateEndpoint06Diagnostic&,
    BoardingIntermediateEndpoint06Cell&,
    const BoardingIntermediateEndpoint06Limits&,
    BoardingIntermediateEndpoint06Refusal&)
    -> BoardingIntermediateEndpoint06State;
} // namespace apsis_drift::detail
