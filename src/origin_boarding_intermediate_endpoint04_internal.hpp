#pragma once
#include "apsis_drift/origin_boarding_intermediate_endpoint04.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
namespace apsis_drift::detail {
struct BoardingIntermediateEndpoint04Limits {
  BoardingIntermediatePausePhaseLimits phase;
  std::size_t source_guards{64}, projection_guards{256}, definition_guards{31},
      sole_extrema{4}, source_coordinates{8}, intersection_operations{4},
      midpoint_operations{4}, allocation_operations{6}, pressure_candidates{2},
      disk_edges{16}, self_body_guards{63}, self_pairs{105}, self_axes{1470},
      self_signed_trials{2940}, self_owners{14}, self_hip_complements{2},
      unit_axis_operations{24}, output_bytes{16777216}, construction_guards{37},
      construction_operations{226};
};
[[nodiscard]] auto intermediate_endpoint04_bounded(
    const OriginBoardingIntermediatePauseSupport&,
    BoardingIntermediateEndpoint04Candidate,
    BoardingIntermediateEndpoint04Limits = {})
    -> BoardingIntermediateEndpoint04Expected;
class BoardingIntermediateEndpoint04ProgramKey {
 private:
  BoardingIntermediateEndpoint04ProgramKey(
      double y, BoardingIntermediateEndpoint04Candidate candidate)
      : y_(y), version_(4), candidate_(candidate) {}
  double y_;
  std::uint32_t version_;
  BoardingIntermediateEndpoint04Candidate candidate_;
  friend auto intermediate_endpoint04_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingIntermediateEndpoint04Candidate,
      BoardingIntermediateEndpoint04Limits)
      -> BoardingIntermediateEndpoint04Expected;

 public:
  [[nodiscard]] auto y() const -> double { return y_; }
  [[nodiscard]] auto version() const -> std::uint32_t { return version_; }
  [[nodiscard]] auto candidate() const
      -> BoardingIntermediateEndpoint04Candidate {
    return candidate_;
  }
};
class BoardingIntermediateEndpoint04CurrentToken;
class BoardingIntermediateEndpoint04AdmissionContext {
 private:
  BoardingIntermediateEndpoint04AdmissionContext(
      const BoardingIntermediateEndpoint04Diagnostic& d,
      const BoardingRouteFootPhaseRequest& r,
      const BoardingIntermediateEndpoint04ProgramKey& key)
      : owner_(&d),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)),
        parts_(&d.parts), request_(&r), key_(&key) {}
  const BoardingIntermediateEndpoint04Diagnostic* owner_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  const std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount>*
      parts_;
  const BoardingRouteFootPhaseRequest* request_;
  const BoardingIntermediateEndpoint04ProgramKey* key_;
  friend auto intermediate_endpoint04_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingIntermediateEndpoint04Candidate,
      BoardingIntermediateEndpoint04Limits)
      -> BoardingIntermediateEndpoint04Expected;

 public:
  BoardingIntermediateEndpoint04AdmissionContext(
      const BoardingIntermediateEndpoint04AdmissionContext&) = delete;
  BoardingIntermediateEndpoint04AdmissionContext(
      BoardingIntermediateEndpoint04AdmissionContext&&) = delete;
  auto operator=(const BoardingIntermediateEndpoint04AdmissionContext&)
      -> BoardingIntermediateEndpoint04AdmissionContext& = delete;
  auto operator=(BoardingIntermediateEndpoint04AdmissionContext&&)
      -> BoardingIntermediateEndpoint04AdmissionContext& = delete;
  [[nodiscard]] auto key() const
      -> const BoardingIntermediateEndpoint04ProgramKey* {
    return key_;
  }
  [[nodiscard]] auto owner() const
      -> const BoardingIntermediateEndpoint04Diagnostic* {
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
class BoardingIntermediateEndpoint04CurrentToken {
 private:
  BoardingIntermediateEndpoint04CurrentToken(
      const BoardingIntermediateEndpoint04AdmissionContext& x,
      const BoardingIntermediateEndpoint04Diagnostic& d,
      const BoardingIntermediateEndpoint04Cell& c,
      const BoardingRouteFootPhaseRequest& r)
      : context_(&x), owner_(&d), cell_(&c), request_(&r),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)) {}
  const BoardingIntermediateEndpoint04AdmissionContext* context_;
  const BoardingIntermediateEndpoint04Diagnostic* owner_;
  const BoardingIntermediateEndpoint04Cell* cell_;
  const BoardingRouteFootPhaseRequest* request_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  friend auto intermediate_endpoint04_current_cell(
      const BoardingIntermediateEndpoint04AdmissionContext&,
      BoardingIntermediateEndpoint04Diagnostic&,
      BoardingIntermediateEndpoint04Cell&,
      const BoardingIntermediateEndpoint04Limits&,
      BoardingIntermediateEndpoint04Refusal&)
      -> BoardingIntermediateEndpoint04State;

 public:
  [[nodiscard]] auto context() const
      -> const BoardingIntermediateEndpoint04AdmissionContext* {
    return context_;
  }
  [[nodiscard]] auto owner() const
      -> const BoardingIntermediateEndpoint04Diagnostic* {
    return owner_;
  }
  [[nodiscard]] auto cell() const -> const BoardingIntermediateEndpoint04Cell* {
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
auto intermediate_endpoint04_current_cell(
    const BoardingIntermediateEndpoint04AdmissionContext&,
    BoardingIntermediateEndpoint04Diagnostic&,
    BoardingIntermediateEndpoint04Cell&,
    const BoardingIntermediateEndpoint04Limits&,
    BoardingIntermediateEndpoint04Refusal&)
    -> BoardingIntermediateEndpoint04State;
auto intermediate_endpoint04_refuse(BoardingIntermediateEndpoint04Diagnostic&,
                                    BoardingIntermediateEndpoint04Refusal&,
                                    BoardingIntermediateEndpoint04Condition)
    -> bool;
auto intermediate_endpoint04_charge(BoardingIntermediateEndpoint04Diagnostic&,
                                    BoardingIntermediateEndpoint04Refusal&,
                                    std::uint64_t&, std::size_t,
                                    BoardingIntermediateEndpoint04Condition)
    -> bool;
auto intermediate_endpoint04_definition_charge(
    BoardingIntermediateEndpoint04Diagnostic&,
    BoardingIntermediateEndpoint04Cell&,
    const BoardingIntermediateEndpoint04Limits&,
    BoardingIntermediateEndpoint04Refusal&) -> bool;
auto intermediate_endpoint04_pressure_bridge(
    const BoardingIntermediateEndpoint04CurrentToken&,
    BoardingIntermediateEndpoint04Diagnostic&,
    BoardingIntermediateEndpoint04Cell&,
    const BoardingIntermediateEndpoint04Limits&,
    BoardingIntermediateEndpoint04Refusal&)
    -> BoardingIntermediateEndpoint04State;
auto intermediate_endpoint04_source_enroll(
    BoardingIntermediateEndpoint04Diagnostic&, BoardingRouteFootPhaseRequest&,
    const BoardingIntermediateEndpoint04Limits&,
    BoardingIntermediateEndpoint04Refusal&) -> bool;
auto intermediate_endpoint04_construct(
    BoardingIntermediateEndpoint04Diagnostic&, BoardingRouteFootPhaseRequest&,
    const BoardingIntermediateEndpoint04Limits&,
    BoardingIntermediateEndpoint04Refusal&) -> bool;
auto intermediate_endpoint04_canonical(
    const BoardingIntermediateEndpoint04ProgramKey&)
    -> std::optional<BoardingRouteFootPhaseRequest>;
auto intermediate_endpoint04_template_valid(
    const BoardingRouteFootPhaseRequest&, bool generated, double y) -> bool;
auto intermediate_endpoint04_self_bridge(
    const BoardingIntermediateEndpoint04CurrentToken&,
    BoardingIntermediateEndpoint04Diagnostic&,
    BoardingIntermediateEndpoint04Cell&,
    const BoardingIntermediateEndpoint04Limits&,
    BoardingIntermediateEndpoint04Refusal&)
    -> BoardingIntermediateEndpoint04State;
} // namespace apsis_drift::detail
