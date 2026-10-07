#pragma once
#include "apsis_drift/origin_boarding_intermediate_sector_evidence01.hpp"
#include "origin_boarding_intermediate_endpoint02_internal.hpp"
#include <optional>
namespace apsis_drift::detail {
struct BoardingIntermediateSectorEvidence01Limits {
  std::size_t source_guards{64}, construction_guards{18},
      construction_operations{64}, capture_guards{8}, output_bytes{16777216};
};
[[nodiscard]] auto intermediate_sector_evidence01_bounded(
    const OriginBoardingIntermediatePauseSupport&,
    BoardingIntermediateSectorEvidence01Limits = {})
    -> BoardingIntermediateSectorEvidence01Expected;
struct BoardingIntermediateSectorEvidence01FreshCallRecord {
  std::uint64_t calls{};
  BoardingIntermediateSectorEvidence01OriginalResult result{};
  bool prepared{}, invoked{}, completed{};
};
class BoardingIntermediateSectorEvidence01ProgramKey {
 private:
  BoardingIntermediateSectorEvidence01ProgramKey(double y) : y_(y) {}
  double y_;
  std::uint32_t version_{2};
  friend auto intermediate_sector_evidence01_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingIntermediateSectorEvidence01Limits)
      -> BoardingIntermediateSectorEvidence01Expected;

 public:
  [[nodiscard]] auto y() const -> double { return y_; }
  [[nodiscard]] auto version() const -> std::uint32_t { return version_; }
};
class BoardingIntermediateSectorEvidence01AdmissionContext {
 private:
  BoardingIntermediateSectorEvidence01AdmissionContext(
      const BoardingIntermediateSectorEvidence01Diagnostic& d,
      const BoardingRouteFootPhaseRequest& r,
      const BoardingIntermediateSectorEvidence01ProgramKey& k,
      const BoardingIntermediateSectorEvidence01FreshCallRecord& f)
      : owner_(&d),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)),
        request_(&r), key_(&k), fresh_(&f) {}
  const BoardingIntermediateSectorEvidence01Diagnostic* owner_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  const BoardingRouteFootPhaseRequest* request_;
  const BoardingIntermediateSectorEvidence01ProgramKey* key_;
  const BoardingIntermediateSectorEvidence01FreshCallRecord* fresh_;
  friend auto intermediate_sector_evidence01_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingIntermediateSectorEvidence01Limits)
      -> BoardingIntermediateSectorEvidence01Expected;

 public:
  BoardingIntermediateSectorEvidence01AdmissionContext(
      const BoardingIntermediateSectorEvidence01AdmissionContext&) = delete;
  BoardingIntermediateSectorEvidence01AdmissionContext(
      BoardingIntermediateSectorEvidence01AdmissionContext&&) = delete;
  auto operator=(const BoardingIntermediateSectorEvidence01AdmissionContext&)
      -> BoardingIntermediateSectorEvidence01AdmissionContext& = delete;
  auto operator=(BoardingIntermediateSectorEvidence01AdmissionContext&&)
      -> BoardingIntermediateSectorEvidence01AdmissionContext& = delete;
  [[nodiscard]] auto owner() const
      -> const BoardingIntermediateSectorEvidence01Diagnostic* {
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
      -> const BoardingIntermediateSectorEvidence01ProgramKey* {
    return key_;
  }
  [[nodiscard]] auto fresh() const
      -> const BoardingIntermediateSectorEvidence01FreshCallRecord* {
    return fresh_;
  }
};
class BoardingIntermediateSectorEvidence01CaptureToken {
 private:
  BoardingIntermediateSectorEvidence01CaptureToken(
      const BoardingIntermediateSectorEvidence01AdmissionContext& x,
      const BoardingIntermediateSectorEvidence01Diagnostic& d,
      const BoardingRouteFootPhaseCell& c,
      const BoardingRouteFootPhaseRequest& r,
      const BoardingIntermediateSectorEvidence01FreshCallRecord& f)
      : context_(&x), owner_(&d), cell_(&c), request_(&r), fresh_(&f) {}
  const BoardingIntermediateSectorEvidence01AdmissionContext* context_;
  const BoardingIntermediateSectorEvidence01Diagnostic* owner_;
  const BoardingRouteFootPhaseCell* cell_;
  const BoardingRouteFootPhaseRequest* request_;
  const BoardingIntermediateSectorEvidence01FreshCallRecord* fresh_;
  friend auto intermediate_sector_evidence01_current_call(
      const BoardingIntermediateSectorEvidence01AdmissionContext&,
      BoardingIntermediateSectorEvidence01Diagnostic&,
      BoardingRouteFootPhaseCell&,
      BoardingIntermediateSectorEvidence01FreshCallRecord&)
      -> std::optional<BoardingIntermediateSectorEvidence01CaptureToken>;

 public:
  [[nodiscard]] auto context() const
      -> const BoardingIntermediateSectorEvidence01AdmissionContext* {
    return context_;
  }
  [[nodiscard]] auto owner() const
      -> const BoardingIntermediateSectorEvidence01Diagnostic* {
    return owner_;
  }
  [[nodiscard]] auto cell() const -> const BoardingRouteFootPhaseCell* {
    return cell_;
  }
  [[nodiscard]] auto request() const -> const BoardingRouteFootPhaseRequest* {
    return request_;
  }
  [[nodiscard]] auto fresh() const
      -> const BoardingIntermediateSectorEvidence01FreshCallRecord* {
    return fresh_;
  }
};
[[nodiscard]] auto intermediate_sector_evidence01_current_call(
    const BoardingIntermediateSectorEvidence01AdmissionContext&,
    BoardingIntermediateSectorEvidence01Diagnostic&,
    BoardingRouteFootPhaseCell&,
    BoardingIntermediateSectorEvidence01FreshCallRecord&)
    -> std::optional<BoardingIntermediateSectorEvidence01CaptureToken>;
} // namespace apsis_drift::detail
