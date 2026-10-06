#pragma once
#include "apsis_drift/origin_boarding_route_intermediate_support01.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
namespace apsis_drift::detail {
struct BoardingRouteIntermediateSupport01Limits {
  BoardingRouteFootPhaseLimits phase;
  std::size_t source_guards{36}, projection_guards{std::size_t{251} * 2047},
      definition_guards{std::size_t{32} * 2047},
      pressure_candidates{std::size_t{2} * 2047},
      disk_edges{std::size_t{16} * 2047}, sole_extrema{std::size_t{4} * 2047},
      source_coordinates{std::size_t{8} * 2047},
      intersection_operations{std::size_t{4} * 2047},
      midpoint_operations{std::size_t{4} * 2047},
      allocation_operations{std::size_t{6} * 2047},
      reaction_operations{std::size_t{3} * 2047},
      division_quotients{std::size_t{8} * 2047},
      division_folds{std::size_t{12} * 2047},
      upper_geometry_edges{std::size_t{8} * 2047},
      endpoint_operations{std::size_t{12} * 2047},
      event_guards{std::size_t{16} * 2047};
};
[[nodiscard]] auto boarding_route_intermediate_support01_controls(std::size_t)
    -> std::optional<BoardingRouteFootPhaseRequest>;
[[nodiscard]] auto boarding_route_intermediate_support01_phase(double, double)
    -> std::optional<std::size_t>;
[[nodiscard]] auto boarding_route_intermediate_support01_local(std::size_t,
                                                               double)
    -> double;
[[nodiscard]] auto boarding_route_intermediate_support01_clock(double)
    -> double;
[[nodiscard]] auto boarding_route_intermediate_support01_join_math(
    const BoardingRouteFootPhaseRequest&, const BoardingRouteFootPhaseRequest&)
    -> bool;
[[nodiscard]] auto boarding_route_intermediate_support01_bounded(
    const OriginBoardingIntermediatePauseSupport&, double, double,
    BoardingRouteIntermediateSupport01Limits = {})
    -> std::expected<BoardingRouteIntermediateSupport01Diagnostic, std::string>;
class BoardingRouteIntermediateSupport01AdmissionContext {
 private:
  BoardingRouteIntermediateSupport01AdmissionContext(
      const BoardingRouteIntermediateSupport01Diagnostic& d,
      const BoardingRouteFootPhaseRequest& r)
      : owner_(&d),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)),
        parts_(&d.parts), request_(&r) {}
  const BoardingRouteIntermediateSupport01Diagnostic* owner_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  const std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount>*
      parts_;
  const BoardingRouteFootPhaseRequest* request_;
  friend auto boarding_route_intermediate_support01_bounded(
      const OriginBoardingIntermediatePauseSupport&, double, double,
      BoardingRouteIntermediateSupport01Limits)
      -> std::expected<BoardingRouteIntermediateSupport01Diagnostic,
                       std::string>;
  friend auto boarding_route_intermediate_support01_current_cell(
      const BoardingRouteIntermediateSupport01AdmissionContext&,
      BoardingRouteIntermediateSupport01Diagnostic&,
      BoardingRouteIntermediateSupport01Cell&,
      const BoardingRouteIntermediateSupport01Limits&,
      BoardingRouteIntermediateSupport01Refusal&)
      -> BoardingRouteIntermediateSupport01State;
  friend auto boarding_route_intermediate_support01_prefix_bridge(
      const class BoardingRouteIntermediateSupport01CurrentCellToken&,
      BoardingRouteIntermediateSupport01Diagnostic&,
      BoardingRouteIntermediateSupport01Cell&,
      const BoardingRouteIntermediateSupport01Limits&,
      BoardingRouteIntermediateSupport01Refusal&)
      -> BoardingRouteIntermediateSupport01State;
  friend auto boarding_route_intermediate_support01_load_bridge(
      const class BoardingRouteIntermediateSupport01CurrentCellToken&,
      BoardingRouteIntermediateSupport01Diagnostic&,
      BoardingRouteIntermediateSupport01Cell&,
      const BoardingRouteIntermediateSupport01Limits&,
      BoardingRouteIntermediateSupport01Refusal&)
      -> BoardingRouteIntermediateSupport01State;
  friend auto boarding_route_intermediate_support01_pressure_bridge(
      const class BoardingRouteIntermediateSupport01CurrentCellToken&,
      BoardingRouteIntermediateSupport01Diagnostic&,
      BoardingRouteIntermediateSupport01Cell&,
      const BoardingRouteIntermediateSupport01Limits&,
      BoardingRouteIntermediateSupport01Refusal&)
      -> BoardingRouteIntermediateSupport01State;
};
[[nodiscard]] auto boarding_route_intermediate_support01_current_cell(
    const BoardingRouteIntermediateSupport01AdmissionContext&,
    BoardingRouteIntermediateSupport01Diagnostic&,
    BoardingRouteIntermediateSupport01Cell&,
    const BoardingRouteIntermediateSupport01Limits&,
    BoardingRouteIntermediateSupport01Refusal&)
    -> BoardingRouteIntermediateSupport01State;
class BoardingRouteIntermediateSupport01CurrentCellToken {
 private:
  BoardingRouteIntermediateSupport01CurrentCellToken(
      const BoardingRouteIntermediateSupport01AdmissionContext& context,
      const BoardingRouteIntermediateSupport01Diagnostic& d,
      const BoardingRouteIntermediateSupport01Cell& c,
      const BoardingRouteFootPhaseRequest& r)
      : context_(&context), owner_(&d), cell_(&c), request_(&r),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)) {}
  const BoardingRouteIntermediateSupport01AdmissionContext* context_;
  const BoardingRouteIntermediateSupport01Diagnostic* owner_;
  const BoardingRouteIntermediateSupport01Cell* cell_;
  const BoardingRouteFootPhaseRequest* request_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  friend auto boarding_route_intermediate_support01_current_cell(
      const BoardingRouteIntermediateSupport01AdmissionContext&,
      BoardingRouteIntermediateSupport01Diagnostic&,
      BoardingRouteIntermediateSupport01Cell&,
      const BoardingRouteIntermediateSupport01Limits&,
      BoardingRouteIntermediateSupport01Refusal&)
      -> BoardingRouteIntermediateSupport01State;
  friend auto boarding_route_intermediate_support01_prefix_bridge(
      const class BoardingRouteIntermediateSupport01CurrentCellToken&,
      BoardingRouteIntermediateSupport01Diagnostic&,
      BoardingRouteIntermediateSupport01Cell&,
      const BoardingRouteIntermediateSupport01Limits&,
      BoardingRouteIntermediateSupport01Refusal&)
      -> BoardingRouteIntermediateSupport01State;
  friend auto boarding_route_intermediate_support01_load_bridge(
      const class BoardingRouteIntermediateSupport01CurrentCellToken&,
      BoardingRouteIntermediateSupport01Diagnostic&,
      BoardingRouteIntermediateSupport01Cell&,
      const BoardingRouteIntermediateSupport01Limits&,
      BoardingRouteIntermediateSupport01Refusal&)
      -> BoardingRouteIntermediateSupport01State;
  friend auto boarding_route_intermediate_support01_pressure_bridge(
      const BoardingRouteIntermediateSupport01CurrentCellToken&,
      BoardingRouteIntermediateSupport01Diagnostic&,
      BoardingRouteIntermediateSupport01Cell&,
      const BoardingRouteIntermediateSupport01Limits&,
      BoardingRouteIntermediateSupport01Refusal&)
      -> BoardingRouteIntermediateSupport01State;
};
auto boarding_route_intermediate_support01_definition_charge(
    BoardingRouteIntermediateSupport01Diagnostic&,
    BoardingRouteIntermediateSupport01Cell&,
    const BoardingRouteIntermediateSupport01Limits&,
    BoardingRouteIntermediateSupport01Refusal&) -> bool;
auto boarding_route_intermediate_support01_pressure_bridge(
    const BoardingRouteIntermediateSupport01CurrentCellToken&,
    BoardingRouteIntermediateSupport01Diagnostic&,
    BoardingRouteIntermediateSupport01Cell&,
    const BoardingRouteIntermediateSupport01Limits&,
    BoardingRouteIntermediateSupport01Refusal&)
    -> BoardingRouteIntermediateSupport01State;
auto boarding_route_intermediate_support01_prefix_bridge(
    const BoardingRouteIntermediateSupport01CurrentCellToken&,
    BoardingRouteIntermediateSupport01Diagnostic&,
    BoardingRouteIntermediateSupport01Cell&,
    const BoardingRouteIntermediateSupport01Limits&,
    BoardingRouteIntermediateSupport01Refusal&)
    -> BoardingRouteIntermediateSupport01State;
auto boarding_route_intermediate_support01_load_bridge(
    const BoardingRouteIntermediateSupport01CurrentCellToken&,
    BoardingRouteIntermediateSupport01Diagnostic&,
    BoardingRouteIntermediateSupport01Cell&,
    const BoardingRouteIntermediateSupport01Limits&,
    BoardingRouteIntermediateSupport01Refusal&)
    -> BoardingRouteIntermediateSupport01State;
auto boarding_route_intermediate_support01_allocate(
    BoardingRouteIntermediateSupport01Diagnostic&,
    BoardingRouteIntermediateSupport01Cell&,
    const BoardingRouteIntermediateSupport01Limits&,
    BoardingRouteIntermediateSupport01Refusal&) -> bool;
} // namespace apsis_drift::detail
