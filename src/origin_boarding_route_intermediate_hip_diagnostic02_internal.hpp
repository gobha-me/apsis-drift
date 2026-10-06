#pragma once
#include "apsis_drift/origin_boarding_route_intermediate_hip_diagnostic02.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
#include "origin_boarding_route_foot_phase_internal.hpp"
#include "origin_boarding_route_intermediate_hip_diagnostic01_internal.hpp"
namespace apsis_drift::detail {
struct BoardingRouteIntermediateHipDiagnostic02PhaseLimits {
  std::uint64_t graphs{1}, legs{2}, bodies{1}, sectors{3}, timing{6};
};
struct BoardingRouteIntermediateHipDiagnostic02Limits {
  BoardingRouteIntermediateHipDiagnostic02PhaseLimits phase;
  std::size_t source_guards{67}, current_guards{32}, chart_operations{21},
      generator_states{162}, generator_operations{174874}, dual_trials{162},
      primal_trials{163}, verification_guards{975},
      verification_operations{11368}, output_bytes{16777216};
};
using BoardingRouteIntermediateHipDiagnostic02DualMathLimits =
    BoardingRouteIntermediateHipDiagnostic01DualMathLimits;
using BoardingRouteIntermediateHipDiagnostic02PrimalMathLimits =
    BoardingRouteIntermediateHipDiagnostic01PrimalMathLimits;
struct BoardingRouteIntermediateHipDiagnostic02AnalyticInput {
  std::array<double, 3> half{}, hip{}, axis{}, segment{}, point{};
  double radius{}, owner_limit{}, segment_fraction{};
};
struct BoardingRouteIntermediateHipDiagnostic02AnalyticMathLimits {
  std::size_t guards{3}, anchor_operations{58}, mixture_operations{55};
};
struct BoardingRouteIntermediateHipDiagnostic02AnalyticMath {
  BoardingRouteIntermediateHipDiagnostic02PrimalCandidate anchor_candidate{},
      mixture_candidate{};
  std::uint64_t anchor_attempted{}, anchor_written{}, mixture_attempted{},
      mixture_written{};
  std::size_t guards{}, anchor_operations{}, mixture_operations{};
  double M{}, beta{}, DD{}, tz{}, R{}, a{}, b{}, epsilon_min{}, epsilon_max{},
      epsilon{}, gamma{};
  std::uint8_t guard_attempted{}, guard_written{}, failed_guard{255},
      invalid_limit_field{255}, anchor_operation{255}, mixture_operation{255};
  BoardingRouteIntermediateHipDiagnostic02AnalyticCondition overall_condition{
      BoardingRouteIntermediateHipDiagnostic02AnalyticCondition::not_run},
      anchor_condition{
          BoardingRouteIntermediateHipDiagnostic02AnalyticCondition::not_run},
      mixture_condition{
          BoardingRouteIntermediateHipDiagnostic02AnalyticCondition::not_run};
  bool anchor_ready{}, mixture_ready{};
  static constexpr bool source_qualified{false}, current_qualified{false},
      route_qualified{false}, self_qualified{false},
      nominal_support_qualified{false}, world_qualified{false},
      material_qualified{false}, halo_qualified{false}, seat_qualified{false},
      actor_qualified{false}, save_qualified{false}, dynamics_qualified{false};
};
[[nodiscard]] auto boarding_route_intermediate_hip_diagnostic02_analytic_math(
    const BoardingRouteIntermediateHipDiagnostic02AnalyticInput&,
    BoardingRouteIntermediateHipDiagnostic02AnalyticMathLimits = {})
    -> BoardingRouteIntermediateHipDiagnostic02AnalyticMath;
[[nodiscard]] auto boarding_route_intermediate_hip_diagnostic02_case(
    BoardingRouteIntermediateHipDiagnostic02Case)
    -> std::optional<BoardingRouteIntermediateHipDiagnostic02CaseMetadata>;
[[nodiscard]] auto boarding_route_intermediate_hip_diagnostic02_controls(
    BoardingRouteIntermediateHipDiagnostic02Case)
    -> std::optional<BoardingRouteFootPhaseRequest>;
[[nodiscard]] auto boarding_route_intermediate_hip_diagnostic02_bounded(
    const OriginBoardingIntermediatePauseSupport&,
    BoardingRouteIntermediateHipDiagnostic02Case,
    BoardingRouteIntermediateHipDiagnostic02Limits = {})
    -> std::expected<BoardingRouteIntermediateHipDiagnostic02Diagnostic,
                     BoardingRouteIntermediateHipDiagnostic02Error>;
class BoardingRouteIntermediateHipDiagnostic02AdmissionContext {
  const BoardingRouteIntermediateHipDiagnostic02Diagnostic* owner_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  const BoardingRouteFootPhaseRequest* request_;
  bool fresh_{};
  BoardingRouteIntermediateHipDiagnostic02AdmissionContext(
      const BoardingRouteIntermediateHipDiagnostic02Diagnostic& d,
      const BoardingRouteFootPhaseRequest& r)
      : owner_(&d),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)),
        request_(&r) {}
  friend class BoardingRouteIntermediateHipDiagnostic02CurrentToken;
  friend auto boarding_route_intermediate_hip_diagnostic02_bounded(
      const OriginBoardingIntermediatePauseSupport&,
      BoardingRouteIntermediateHipDiagnostic02Case,
      BoardingRouteIntermediateHipDiagnostic02Limits)
      -> std::expected<BoardingRouteIntermediateHipDiagnostic02Diagnostic,
                       BoardingRouteIntermediateHipDiagnostic02Error>;
  friend auto boarding_route_intermediate_hip_diagnostic02_current_cell(
      const BoardingRouteIntermediateHipDiagnostic02AdmissionContext&,
      BoardingRouteIntermediateHipDiagnostic02Diagnostic&,
      const BoardingRouteIntermediateHipDiagnostic02Limits&) -> bool;
  friend auto boarding_route_intermediate_hip_diagnostic02_chart(
      const class BoardingRouteIntermediateHipDiagnostic02CurrentToken&,
      BoardingRouteIntermediateHipDiagnostic02Diagnostic&,
      const BoardingRouteIntermediateHipDiagnostic02Limits&) -> bool;
};
class BoardingRouteIntermediateHipDiagnostic02CurrentToken {
  const BoardingRouteIntermediateHipDiagnostic02AdmissionContext* context_;
  const BoardingRouteIntermediateHipDiagnostic02Diagnostic* owner_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  const BoardingRouteFootPhaseRequest* request_;
  const BoardingRouteFootPhaseCell* cell_;
  BoardingRouteIntermediateHipDiagnostic02CurrentToken(
      const BoardingRouteIntermediateHipDiagnostic02AdmissionContext& x,
      const BoardingRouteIntermediateHipDiagnostic02Diagnostic& d)
      : context_(&x), owner_(&d),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)),
        request_(x.request_), cell_(&d.current_phase.front()) {}
  friend auto boarding_route_intermediate_hip_diagnostic02_current_cell(
      const BoardingRouteIntermediateHipDiagnostic02AdmissionContext&,
      BoardingRouteIntermediateHipDiagnostic02Diagnostic&,
      const BoardingRouteIntermediateHipDiagnostic02Limits&) -> bool;
  friend auto boarding_route_intermediate_hip_diagnostic02_chart(
      const BoardingRouteIntermediateHipDiagnostic02CurrentToken&,
      BoardingRouteIntermediateHipDiagnostic02Diagnostic&,
      const BoardingRouteIntermediateHipDiagnostic02Limits&) -> bool;
};
auto boarding_route_intermediate_hip_diagnostic02_current_cell(
    const BoardingRouteIntermediateHipDiagnostic02AdmissionContext&,
    BoardingRouteIntermediateHipDiagnostic02Diagnostic&,
    const BoardingRouteIntermediateHipDiagnostic02Limits&) -> bool;
auto boarding_route_intermediate_hip_diagnostic02_chart(
    const BoardingRouteIntermediateHipDiagnostic02CurrentToken&,
    BoardingRouteIntermediateHipDiagnostic02Diagnostic&,
    const BoardingRouteIntermediateHipDiagnostic02Limits&) -> bool;
auto boarding_route_intermediate_hip_diagnostic02_source_charge(
    BoardingRouteIntermediateHipDiagnostic02Diagnostic&, std::size_t,
    const BoardingRouteIntermediateHipDiagnostic02Limits&) -> bool;
auto boarding_route_intermediate_hip_diagnostic02_body_source(
    BoardingRouteIntermediateHipDiagnostic02Diagnostic&,
    const std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount>&,
    const BoardingRouteIntermediateHipDiagnostic02Limits&) -> bool;
auto boarding_route_intermediate_hip_diagnostic02_current_body(
    BoardingRouteIntermediateHipDiagnostic02Diagnostic&,
    const BoardingRouteIntermediateHipDiagnostic02Limits&) -> bool;
} // namespace apsis_drift::detail

namespace apsis_drift::detail {
auto boarding_route_intermediate_hip_diagnostic02_current_charge(
    BoardingRouteIntermediateHipDiagnostic02Diagnostic&, std::size_t,
    const BoardingRouteIntermediateHipDiagnostic02Limits&) -> bool;
}
