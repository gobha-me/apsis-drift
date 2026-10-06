#pragma once
#include "apsis_drift/origin_boarding_route_intermediate_hip_diagnostic01.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
#include "origin_boarding_route_foot_phase_internal.hpp"
namespace apsis_drift::detail {
struct BoardingRouteIntermediateHipDiagnostic01PhaseLimits {
  std::uint64_t graphs{1}, legs{2}, bodies{1}, sectors{3}, timing{6};
};
struct BoardingRouteIntermediateHipDiagnostic01Limits {
  BoardingRouteIntermediateHipDiagnostic01PhaseLimits phase;
  std::size_t source_guards{67}, current_guards{32}, chart_operations{21},
      generator_states{162}, generator_operations{243666}, dual_trials{162},
      primal_trials{3240}, verification_guards{10206},
      verification_operations{97524}, output_bytes{16777216};
};
struct BoardingRouteIntermediateHipDiagnostic01DualMathLimits {
  std::size_t guards{3}, operations{42};
};
struct BoardingRouteIntermediateHipDiagnostic01PrimalMathLimits {
  std::size_t guards{3}, operations{28};
};
[[nodiscard]] auto boarding_route_intermediate_hip_diagnostic01_case(
    BoardingRouteIntermediateHipDiagnostic01Case)
    -> std::optional<BoardingRouteIntermediateHipDiagnostic01CaseMetadata>;
[[nodiscard]] auto boarding_route_intermediate_hip_diagnostic01_controls(
    BoardingRouteIntermediateHipDiagnostic01Case)
    -> std::optional<BoardingRouteFootPhaseRequest>;
[[nodiscard]] auto boarding_route_intermediate_hip_diagnostic01_dual_math(
    const BoardingRouteIntermediateHipDiagnostic01GeometryInput&,
    BoardingRouteIntermediateHipDiagnostic01DualCandidate,
    BoardingRouteIntermediateHipDiagnostic01DualMathLimits = {})
    -> BoardingRouteIntermediateHipDiagnostic01DualMath;
[[nodiscard]] auto boarding_route_intermediate_hip_diagnostic01_primal_math(
    const BoardingRouteIntermediateHipDiagnostic01GeometryInput&,
    BoardingRouteIntermediateHipDiagnostic01PrimalCandidate,
    BoardingRouteIntermediateHipDiagnostic01PrimalMathLimits = {})
    -> BoardingRouteIntermediateHipDiagnostic01PrimalMath;
[[nodiscard]] auto boarding_route_intermediate_hip_diagnostic01_bounded(
    const OriginBoardingIntermediatePauseSupport&,
    BoardingRouteIntermediateHipDiagnostic01Case,
    BoardingRouteIntermediateHipDiagnostic01Limits = {})
    -> std::expected<BoardingRouteIntermediateHipDiagnostic01Diagnostic,
                     BoardingRouteIntermediateHipDiagnostic01Error>;
class BoardingRouteIntermediateHipDiagnostic01AdmissionContext {
  const BoardingRouteIntermediateHipDiagnostic01Diagnostic* owner_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  const BoardingRouteFootPhaseRequest* request_;
  bool fresh_{};
  BoardingRouteIntermediateHipDiagnostic01AdmissionContext(
      const BoardingRouteIntermediateHipDiagnostic01Diagnostic& d,
      const BoardingRouteFootPhaseRequest& r)
      : owner_(&d),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)),
        request_(&r) {}
  friend class BoardingRouteIntermediateHipDiagnostic01CurrentToken;
  friend auto boarding_route_intermediate_hip_diagnostic01_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingRouteIntermediateHipDiagnostic01Case,
      BoardingRouteIntermediateHipDiagnostic01Limits)
      -> std::expected<BoardingRouteIntermediateHipDiagnostic01Diagnostic,
                       BoardingRouteIntermediateHipDiagnostic01Error>;
  friend auto boarding_route_intermediate_hip_diagnostic01_current_cell(
      const BoardingRouteIntermediateHipDiagnostic01AdmissionContext&,
      BoardingRouteIntermediateHipDiagnostic01Diagnostic&,
      const BoardingRouteIntermediateHipDiagnostic01Limits&) -> bool;
  friend auto boarding_route_intermediate_hip_diagnostic01_chart(
      const class BoardingRouteIntermediateHipDiagnostic01CurrentToken&,
      BoardingRouteIntermediateHipDiagnostic01Diagnostic&,
      const BoardingRouteIntermediateHipDiagnostic01Limits&) -> bool;
};
class BoardingRouteIntermediateHipDiagnostic01CurrentToken {
  const BoardingRouteIntermediateHipDiagnostic01AdmissionContext* context_;
  const BoardingRouteIntermediateHipDiagnostic01Diagnostic* owner_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  const BoardingRouteFootPhaseRequest* request_;
  const BoardingRouteFootPhaseCell* cell_;
  BoardingRouteIntermediateHipDiagnostic01CurrentToken(
      const BoardingRouteIntermediateHipDiagnostic01AdmissionContext& x,
      const BoardingRouteIntermediateHipDiagnostic01Diagnostic& d)
      : context_(&x), owner_(&d),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)),
        request_(x.request_), cell_(&d.current_phase.front()) {}
  friend auto boarding_route_intermediate_hip_diagnostic01_current_cell(
      const BoardingRouteIntermediateHipDiagnostic01AdmissionContext&,
      BoardingRouteIntermediateHipDiagnostic01Diagnostic&,
      const BoardingRouteIntermediateHipDiagnostic01Limits&) -> bool;
  friend auto boarding_route_intermediate_hip_diagnostic01_chart(
      const BoardingRouteIntermediateHipDiagnostic01CurrentToken&,
      BoardingRouteIntermediateHipDiagnostic01Diagnostic&,
      const BoardingRouteIntermediateHipDiagnostic01Limits&) -> bool;
};
auto boarding_route_intermediate_hip_diagnostic01_current_cell(
    const BoardingRouteIntermediateHipDiagnostic01AdmissionContext&,
    BoardingRouteIntermediateHipDiagnostic01Diagnostic&,
    const BoardingRouteIntermediateHipDiagnostic01Limits&) -> bool;
auto boarding_route_intermediate_hip_diagnostic01_chart(
    const BoardingRouteIntermediateHipDiagnostic01CurrentToken&,
    BoardingRouteIntermediateHipDiagnostic01Diagnostic&,
    const BoardingRouteIntermediateHipDiagnostic01Limits&) -> bool;
auto boarding_route_intermediate_hip_diagnostic01_source_charge(
    BoardingRouteIntermediateHipDiagnostic01Diagnostic&, std::size_t,
    const BoardingRouteIntermediateHipDiagnostic01Limits&) -> bool;
auto boarding_route_intermediate_hip_diagnostic01_body_source(
    BoardingRouteIntermediateHipDiagnostic01Diagnostic&,
    const std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount>&,
    const BoardingRouteIntermediateHipDiagnostic01Limits&) -> bool;
auto boarding_route_intermediate_hip_diagnostic01_current_body(
    BoardingRouteIntermediateHipDiagnostic01Diagnostic&,
    const BoardingRouteIntermediateHipDiagnostic01Limits&) -> bool;
} // namespace apsis_drift::detail

namespace apsis_drift::detail {
auto boarding_route_intermediate_hip_diagnostic01_current_charge(
    BoardingRouteIntermediateHipDiagnostic01Diagnostic&, std::size_t,
    const BoardingRouteIntermediateHipDiagnostic01Limits&) -> bool;
}
