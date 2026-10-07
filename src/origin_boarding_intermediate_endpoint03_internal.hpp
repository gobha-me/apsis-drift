#pragma once
#include "apsis_drift/origin_boarding_intermediate_endpoint03.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
namespace apsis_drift::detail {
struct BoardingIntermediateEndpoint03Limits {
  BoardingIntermediatePausePhaseLimits phase;
  std::size_t source_guards{64}, projection_guards{256}, definition_guards{31},
      sole_extrema{4}, source_coordinates{8}, intersection_operations{4},
      midpoint_operations{4}, allocation_operations{6}, pressure_candidates{2},
      disk_edges{16}, self_body_guards{63}, self_pairs{105}, self_axes{1470},
      self_signed_trials{2940}, self_owners{14}, self_hip_complements{2},
      unit_axis_operations{24}, output_bytes{16777216}, construction_guards{21},
      construction_operations{78};
};
[[nodiscard]] auto intermediate_endpoint03_bounded(
    const OriginBoardingIntermediatePauseSupport&,
    BoardingIntermediateEndpoint03Candidate,
    BoardingIntermediateEndpoint03Limits = {})
    -> BoardingIntermediateEndpoint03Expected;
class BoardingIntermediateEndpoint03ProgramKey {
 private:
  BoardingIntermediateEndpoint03ProgramKey(
      double y, BoardingIntermediateEndpoint03Candidate candidate)
      : y_(y), version_(3), candidate_(candidate) {}
  double y_;
  std::uint32_t version_;
  BoardingIntermediateEndpoint03Candidate candidate_;
  friend auto intermediate_endpoint03_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingIntermediateEndpoint03Candidate,
      BoardingIntermediateEndpoint03Limits)
      -> BoardingIntermediateEndpoint03Expected;

 public:
  [[nodiscard]] auto y() const -> double { return y_; }
  [[nodiscard]] auto version() const -> std::uint32_t { return version_; }
  [[nodiscard]] auto candidate() const
      -> BoardingIntermediateEndpoint03Candidate {
    return candidate_;
  }
};
class BoardingIntermediateEndpoint03CurrentToken;
class BoardingIntermediateEndpoint03AdmissionContext {
 private:
  BoardingIntermediateEndpoint03AdmissionContext(
      const BoardingIntermediateEndpoint03Diagnostic& d,
      const BoardingRouteFootPhaseRequest& r,
      const BoardingIntermediateEndpoint03ProgramKey& key)
      : owner_(&d),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)),
        parts_(&d.parts), request_(&r), key_(&key) {}
  const BoardingIntermediateEndpoint03Diagnostic* owner_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  const std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount>*
      parts_;
  const BoardingRouteFootPhaseRequest* request_;
  const BoardingIntermediateEndpoint03ProgramKey* key_;
  friend auto intermediate_endpoint03_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingIntermediateEndpoint03Candidate,
      BoardingIntermediateEndpoint03Limits)
      -> BoardingIntermediateEndpoint03Expected;

 public:
  BoardingIntermediateEndpoint03AdmissionContext(
      const BoardingIntermediateEndpoint03AdmissionContext&) = delete;
  BoardingIntermediateEndpoint03AdmissionContext(
      BoardingIntermediateEndpoint03AdmissionContext&&) = delete;
  auto operator=(const BoardingIntermediateEndpoint03AdmissionContext&)
      -> BoardingIntermediateEndpoint03AdmissionContext& = delete;
  auto operator=(BoardingIntermediateEndpoint03AdmissionContext&&)
      -> BoardingIntermediateEndpoint03AdmissionContext& = delete;
  [[nodiscard]] auto key() const
      -> const BoardingIntermediateEndpoint03ProgramKey* {
    return key_;
  }
  [[nodiscard]] auto owner() const
      -> const BoardingIntermediateEndpoint03Diagnostic* {
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
class BoardingIntermediateEndpoint03CurrentToken {
 private:
  BoardingIntermediateEndpoint03CurrentToken(
      const BoardingIntermediateEndpoint03AdmissionContext& x,
      const BoardingIntermediateEndpoint03Diagnostic& d,
      const BoardingIntermediateEndpoint03Cell& c,
      const BoardingRouteFootPhaseRequest& r)
      : context_(&x), owner_(&d), cell_(&c), request_(&r),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)) {}
  const BoardingIntermediateEndpoint03AdmissionContext* context_;
  const BoardingIntermediateEndpoint03Diagnostic* owner_;
  const BoardingIntermediateEndpoint03Cell* cell_;
  const BoardingRouteFootPhaseRequest* request_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  friend auto intermediate_endpoint03_current_cell(
      const BoardingIntermediateEndpoint03AdmissionContext&,
      BoardingIntermediateEndpoint03Diagnostic&,
      BoardingIntermediateEndpoint03Cell&,
      const BoardingIntermediateEndpoint03Limits&,
      BoardingIntermediateEndpoint03Refusal&)
      -> BoardingIntermediateEndpoint03State;

 public:
  [[nodiscard]] auto context() const
      -> const BoardingIntermediateEndpoint03AdmissionContext* {
    return context_;
  }
  [[nodiscard]] auto owner() const
      -> const BoardingIntermediateEndpoint03Diagnostic* {
    return owner_;
  }
  [[nodiscard]] auto cell() const -> const BoardingIntermediateEndpoint03Cell* {
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
auto intermediate_endpoint03_current_cell(
    const BoardingIntermediateEndpoint03AdmissionContext&,
    BoardingIntermediateEndpoint03Diagnostic&,
    BoardingIntermediateEndpoint03Cell&,
    const BoardingIntermediateEndpoint03Limits&,
    BoardingIntermediateEndpoint03Refusal&)
    -> BoardingIntermediateEndpoint03State;
auto intermediate_endpoint03_refuse(BoardingIntermediateEndpoint03Diagnostic&,
                                    BoardingIntermediateEndpoint03Refusal&,
                                    BoardingIntermediateEndpoint03Condition)
    -> bool;
auto intermediate_endpoint03_charge(BoardingIntermediateEndpoint03Diagnostic&,
                                    BoardingIntermediateEndpoint03Refusal&,
                                    std::uint64_t&, std::size_t,
                                    BoardingIntermediateEndpoint03Condition)
    -> bool;
auto intermediate_endpoint03_definition_charge(
    BoardingIntermediateEndpoint03Diagnostic&,
    BoardingIntermediateEndpoint03Cell&,
    const BoardingIntermediateEndpoint03Limits&,
    BoardingIntermediateEndpoint03Refusal&) -> bool;
auto intermediate_endpoint03_pressure_bridge(
    const BoardingIntermediateEndpoint03CurrentToken&,
    BoardingIntermediateEndpoint03Diagnostic&,
    BoardingIntermediateEndpoint03Cell&,
    const BoardingIntermediateEndpoint03Limits&,
    BoardingIntermediateEndpoint03Refusal&)
    -> BoardingIntermediateEndpoint03State;
auto intermediate_endpoint03_source_enroll(
    BoardingIntermediateEndpoint03Diagnostic&, BoardingRouteFootPhaseRequest&,
    const BoardingIntermediateEndpoint03Limits&,
    BoardingIntermediateEndpoint03Refusal&) -> bool;
auto intermediate_endpoint03_construct(
    BoardingIntermediateEndpoint03Diagnostic&, BoardingRouteFootPhaseRequest&,
    const BoardingIntermediateEndpoint03Limits&,
    BoardingIntermediateEndpoint03Refusal&) -> bool;
auto intermediate_endpoint03_canonical(
    const BoardingIntermediateEndpoint03ProgramKey&)
    -> std::optional<BoardingRouteFootPhaseRequest>;
auto intermediate_endpoint03_template_valid(
    const BoardingRouteFootPhaseRequest&, bool generated, double y) -> bool;
auto intermediate_endpoint03_self_bridge(
    const BoardingIntermediateEndpoint03CurrentToken&,
    BoardingIntermediateEndpoint03Diagnostic&,
    BoardingIntermediateEndpoint03Cell&,
    const BoardingIntermediateEndpoint03Limits&,
    BoardingIntermediateEndpoint03Refusal&)
    -> BoardingIntermediateEndpoint03State;
} // namespace apsis_drift::detail
