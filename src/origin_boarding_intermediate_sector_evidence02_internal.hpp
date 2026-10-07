#pragma once
#include "apsis_drift/origin_boarding_intermediate_sector_evidence02.hpp"
#include "origin_boarding_intermediate_endpoint03_internal.hpp"
#include <optional>
namespace apsis_drift::detail {
struct BoardingIntermediateSectorEvidence02Limits {
  std::size_t source_guards{64}, construction_guards{21},
      construction_operations{78}, capture_guards{8}, output_bytes{16777216};
};
[[nodiscard]] auto intermediate_sector_evidence02_bounded(
    const OriginBoardingIntermediatePauseSupport&,
    BoardingIntermediateSectorEvidence02Limits = {})
    -> BoardingIntermediateSectorEvidence02Expected;
struct BoardingIntermediateSectorEvidence02FreshCallRecord {
  std::uint64_t calls{};
  BoardingIntermediateSectorEvidence02OriginalResult result{};
  bool prepared{}, invoked{}, completed{};
};
class BoardingIntermediateSectorEvidence02ProgramKey {
 private:
  BoardingIntermediateSectorEvidence02ProgramKey(double y) : y_(y) {}
  double y_;
  std::uint32_t version_{3};
  BoardingIntermediateEndpoint03Candidate candidate_{
      BoardingIntermediateEndpoint03Candidate::root_y_reach_roll_slice};
  friend auto intermediate_sector_evidence02_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingIntermediateSectorEvidence02Limits)
      -> BoardingIntermediateSectorEvidence02Expected;

 public:
  [[nodiscard]] auto y() const -> double { return y_; }
  [[nodiscard]] auto version() const -> std::uint32_t { return version_; }
  [[nodiscard]] auto candidate() const
      -> BoardingIntermediateEndpoint03Candidate {
    return candidate_;
  }
};
class BoardingIntermediateSectorEvidence02AdmissionContext {
 private:
  BoardingIntermediateSectorEvidence02AdmissionContext(
      const BoardingIntermediateSectorEvidence02Diagnostic& d,
      const BoardingRouteFootPhaseRequest& r,
      const BoardingIntermediateSectorEvidence02ProgramKey& k,
      const BoardingIntermediateSectorEvidence02FreshCallRecord& f)
      : owner_(&d),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)),
        request_(&r), key_(&k), fresh_(&f) {}
  const BoardingIntermediateSectorEvidence02Diagnostic* owner_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  const BoardingRouteFootPhaseRequest* request_;
  const BoardingIntermediateSectorEvidence02ProgramKey* key_;
  const BoardingIntermediateSectorEvidence02FreshCallRecord* fresh_;
  friend auto intermediate_sector_evidence02_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingIntermediateSectorEvidence02Limits)
      -> BoardingIntermediateSectorEvidence02Expected;

 public:
  BoardingIntermediateSectorEvidence02AdmissionContext(
      const BoardingIntermediateSectorEvidence02AdmissionContext&) = delete;
  BoardingIntermediateSectorEvidence02AdmissionContext(
      BoardingIntermediateSectorEvidence02AdmissionContext&&) = delete;
  auto operator=(const BoardingIntermediateSectorEvidence02AdmissionContext&)
      -> BoardingIntermediateSectorEvidence02AdmissionContext& = delete;
  auto operator=(BoardingIntermediateSectorEvidence02AdmissionContext&&)
      -> BoardingIntermediateSectorEvidence02AdmissionContext& = delete;
  [[nodiscard]] auto owner() const
      -> const BoardingIntermediateSectorEvidence02Diagnostic* {
    return owner_;
  }
  [[nodiscard]] auto data() const
      -> const OriginBoardingIntermediatePauseSupport::Data* {
    return data_;
  }
  [[nodiscard]] auto request() const -> const BoardingRouteFootPhaseRequest* {
    return request_;
  }
  [[nodiscard]] auto key() const
      -> const BoardingIntermediateSectorEvidence02ProgramKey* {
    return key_;
  }
  [[nodiscard]] auto fresh() const
      -> const BoardingIntermediateSectorEvidence02FreshCallRecord* {
    return fresh_;
  }
};
class BoardingIntermediateSectorEvidence02CaptureToken {
 private:
  BoardingIntermediateSectorEvidence02CaptureToken(
      const BoardingIntermediateSectorEvidence02AdmissionContext& x,
      const BoardingIntermediateSectorEvidence02Diagnostic& d,
      const BoardingRouteFootPhaseCell& c,
      const BoardingRouteFootPhaseRequest& r,
      const BoardingIntermediateSectorEvidence02FreshCallRecord& f)
      : context_(&x), owner_(&d), cell_(&c), request_(&r), fresh_(&f) {}
  const BoardingIntermediateSectorEvidence02AdmissionContext* context_;
  const BoardingIntermediateSectorEvidence02Diagnostic* owner_;
  const BoardingRouteFootPhaseCell* cell_;
  const BoardingRouteFootPhaseRequest* request_;
  const BoardingIntermediateSectorEvidence02FreshCallRecord* fresh_;
  friend auto intermediate_sector_evidence02_current_call(
      const BoardingIntermediateSectorEvidence02AdmissionContext&,
      BoardingIntermediateSectorEvidence02Diagnostic&,
      BoardingRouteFootPhaseCell&,
      BoardingIntermediateSectorEvidence02FreshCallRecord&)
      -> std::optional<BoardingIntermediateSectorEvidence02CaptureToken>;

 public:
  [[nodiscard]] auto context() const
      -> const BoardingIntermediateSectorEvidence02AdmissionContext* {
    return context_;
  }
  [[nodiscard]] auto owner() const
      -> const BoardingIntermediateSectorEvidence02Diagnostic* {
    return owner_;
  }
  [[nodiscard]] auto cell() const -> const BoardingRouteFootPhaseCell* {
    return cell_;
  }
  [[nodiscard]] auto request() const -> const BoardingRouteFootPhaseRequest* {
    return request_;
  }
  [[nodiscard]] auto fresh() const
      -> const BoardingIntermediateSectorEvidence02FreshCallRecord* {
    return fresh_;
  }
};
[[nodiscard]] auto intermediate_sector_evidence02_current_call(
    const BoardingIntermediateSectorEvidence02AdmissionContext&,
    BoardingIntermediateSectorEvidence02Diagnostic&,
    BoardingRouteFootPhaseCell&,
    BoardingIntermediateSectorEvidence02FreshCallRecord&)
    -> std::optional<BoardingIntermediateSectorEvidence02CaptureToken>;
} // namespace apsis_drift::detail
