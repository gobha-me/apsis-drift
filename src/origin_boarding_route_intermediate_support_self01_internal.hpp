#pragma once
#include "apsis_drift/origin_boarding_route_intermediate_support_self01.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
namespace apsis_drift::detail {
struct BoardingRouteIntermediateSupportSelf01Limits {
  BoardingRouteFootPhaseLimits phase;
  std::size_t source_guards{67}, projection_guards{std::size_t{251} * 2047},
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
      event_guards{std::size_t{16} * 2047},
      self_body_guards{std::size_t{63} * 2047},
      self_pairs{std::size_t{105} * 2047},
      self_axes{std::size_t{14} * 105 * 2047},
      self_signed_trials{std::size_t{2} * 14 * 105 * 2047},
      self_owners{std::size_t{14} * 2047},
      self_hip_complements{std::size_t{2} * 2047};
};
[[nodiscard]] auto boarding_route_intermediate_support_self01_controls(
    std::size_t) -> std::optional<BoardingRouteFootPhaseRequest>;
[[nodiscard]] auto boarding_route_intermediate_support_self01_phase(double,
                                                                    double)
    -> std::optional<std::size_t>;
[[nodiscard]] auto boarding_route_intermediate_support_self01_local(std::size_t,
                                                                    double)
    -> double;
[[nodiscard]] auto boarding_route_intermediate_support_self01_clock(double)
    -> double;
[[nodiscard]] auto boarding_route_intermediate_support_self01_join_math(
    const BoardingRouteFootPhaseRequest&, const BoardingRouteFootPhaseRequest&)
    -> bool;
[[nodiscard]] auto boarding_route_intermediate_support_self01_bounded(
    const OriginBoardingIntermediatePauseSupport&, double, double,
    BoardingRouteIntermediateSupportSelf01Limits = {})
    -> std::expected<BoardingRouteIntermediateSupportSelf01Diagnostic,
                     std::string>;
class BoardingRouteIntermediateSupportSelf01AdmissionContext {
 private:
  BoardingRouteIntermediateSupportSelf01AdmissionContext(
      const BoardingRouteIntermediateSupportSelf01Diagnostic& d,
      const BoardingRouteFootPhaseRequest& r)
      : owner_(&d),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)),
        parts_(&d.parts), request_(&r) {}
  const BoardingRouteIntermediateSupportSelf01Diagnostic* owner_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  const std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount>*
      parts_;
  const BoardingRouteFootPhaseRequest* request_;
  friend auto boarding_route_intermediate_support_self01_bounded(
      const OriginBoardingIntermediatePauseSupport&, double, double,
      BoardingRouteIntermediateSupportSelf01Limits)
      -> std::expected<BoardingRouteIntermediateSupportSelf01Diagnostic,
                       std::string>;
  friend auto boarding_route_intermediate_support_self01_current_cell(
      const BoardingRouteIntermediateSupportSelf01AdmissionContext&,
      BoardingRouteIntermediateSupportSelf01Diagnostic&,
      BoardingRouteIntermediateSupportSelf01Cell&,
      const BoardingRouteIntermediateSupportSelf01Limits&,
      BoardingRouteIntermediateSupportSelf01Refusal&)
      -> BoardingRouteIntermediateSupportSelf01State;
  friend auto boarding_route_intermediate_support_self01_self_bridge(
      const class BoardingRouteIntermediateSupportSelf01CurrentCellToken&,
      BoardingRouteIntermediateSupportSelf01Diagnostic&,
      BoardingRouteIntermediateSupportSelf01Cell&,
      const BoardingRouteIntermediateSupportSelf01Limits&,
      BoardingRouteIntermediateSupportSelf01Refusal&)
      -> BoardingRouteIntermediateSupportSelf01State;
  friend auto boarding_route_intermediate_support_self01_prefix_bridge(
      const class BoardingRouteIntermediateSupportSelf01CurrentCellToken&,
      BoardingRouteIntermediateSupportSelf01Diagnostic&,
      BoardingRouteIntermediateSupportSelf01Cell&,
      const BoardingRouteIntermediateSupportSelf01Limits&,
      BoardingRouteIntermediateSupportSelf01Refusal&)
      -> BoardingRouteIntermediateSupportSelf01State;
  friend auto boarding_route_intermediate_support_self01_load_bridge(
      const class BoardingRouteIntermediateSupportSelf01CurrentCellToken&,
      BoardingRouteIntermediateSupportSelf01Diagnostic&,
      BoardingRouteIntermediateSupportSelf01Cell&,
      const BoardingRouteIntermediateSupportSelf01Limits&,
      BoardingRouteIntermediateSupportSelf01Refusal&)
      -> BoardingRouteIntermediateSupportSelf01State;
  friend auto boarding_route_intermediate_support_self01_pressure_bridge(
      const class BoardingRouteIntermediateSupportSelf01CurrentCellToken&,
      BoardingRouteIntermediateSupportSelf01Diagnostic&,
      BoardingRouteIntermediateSupportSelf01Cell&,
      const BoardingRouteIntermediateSupportSelf01Limits&,
      BoardingRouteIntermediateSupportSelf01Refusal&)
      -> BoardingRouteIntermediateSupportSelf01State;
};
[[nodiscard]] auto boarding_route_intermediate_support_self01_current_cell(
    const BoardingRouteIntermediateSupportSelf01AdmissionContext&,
    BoardingRouteIntermediateSupportSelf01Diagnostic&,
    BoardingRouteIntermediateSupportSelf01Cell&,
    const BoardingRouteIntermediateSupportSelf01Limits&,
    BoardingRouteIntermediateSupportSelf01Refusal&)
    -> BoardingRouteIntermediateSupportSelf01State;
class BoardingRouteIntermediateSupportSelf01CurrentCellToken {
 private:
  BoardingRouteIntermediateSupportSelf01CurrentCellToken(
      const BoardingRouteIntermediateSupportSelf01AdmissionContext& context,
      const BoardingRouteIntermediateSupportSelf01Diagnostic& d,
      const BoardingRouteIntermediateSupportSelf01Cell& c,
      const BoardingRouteFootPhaseRequest& r)
      : context_(&context), owner_(&d), cell_(&c), request_(&r),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)) {}
  const BoardingRouteIntermediateSupportSelf01AdmissionContext* context_;
  const BoardingRouteIntermediateSupportSelf01Diagnostic* owner_;
  const BoardingRouteIntermediateSupportSelf01Cell* cell_;
  const BoardingRouteFootPhaseRequest* request_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  friend auto boarding_route_intermediate_support_self01_current_cell(
      const BoardingRouteIntermediateSupportSelf01AdmissionContext&,
      BoardingRouteIntermediateSupportSelf01Diagnostic&,
      BoardingRouteIntermediateSupportSelf01Cell&,
      const BoardingRouteIntermediateSupportSelf01Limits&,
      BoardingRouteIntermediateSupportSelf01Refusal&)
      -> BoardingRouteIntermediateSupportSelf01State;
  friend auto boarding_route_intermediate_support_self01_self_bridge(
      const class BoardingRouteIntermediateSupportSelf01CurrentCellToken&,
      BoardingRouteIntermediateSupportSelf01Diagnostic&,
      BoardingRouteIntermediateSupportSelf01Cell&,
      const BoardingRouteIntermediateSupportSelf01Limits&,
      BoardingRouteIntermediateSupportSelf01Refusal&)
      -> BoardingRouteIntermediateSupportSelf01State;
  friend auto boarding_route_intermediate_support_self01_prefix_bridge(
      const class BoardingRouteIntermediateSupportSelf01CurrentCellToken&,
      BoardingRouteIntermediateSupportSelf01Diagnostic&,
      BoardingRouteIntermediateSupportSelf01Cell&,
      const BoardingRouteIntermediateSupportSelf01Limits&,
      BoardingRouteIntermediateSupportSelf01Refusal&)
      -> BoardingRouteIntermediateSupportSelf01State;
  friend auto boarding_route_intermediate_support_self01_load_bridge(
      const class BoardingRouteIntermediateSupportSelf01CurrentCellToken&,
      BoardingRouteIntermediateSupportSelf01Diagnostic&,
      BoardingRouteIntermediateSupportSelf01Cell&,
      const BoardingRouteIntermediateSupportSelf01Limits&,
      BoardingRouteIntermediateSupportSelf01Refusal&)
      -> BoardingRouteIntermediateSupportSelf01State;
  friend auto boarding_route_intermediate_support_self01_pressure_bridge(
      const BoardingRouteIntermediateSupportSelf01CurrentCellToken&,
      BoardingRouteIntermediateSupportSelf01Diagnostic&,
      BoardingRouteIntermediateSupportSelf01Cell&,
      const BoardingRouteIntermediateSupportSelf01Limits&,
      BoardingRouteIntermediateSupportSelf01Refusal&)
      -> BoardingRouteIntermediateSupportSelf01State;
};
auto boarding_route_intermediate_support_self01_definition_charge(
    BoardingRouteIntermediateSupportSelf01Diagnostic&,
    BoardingRouteIntermediateSupportSelf01Cell&,
    const BoardingRouteIntermediateSupportSelf01Limits&,
    BoardingRouteIntermediateSupportSelf01Refusal&) -> bool;
auto boarding_route_intermediate_support_self01_pressure_bridge(
    const BoardingRouteIntermediateSupportSelf01CurrentCellToken&,
    BoardingRouteIntermediateSupportSelf01Diagnostic&,
    BoardingRouteIntermediateSupportSelf01Cell&,
    const BoardingRouteIntermediateSupportSelf01Limits&,
    BoardingRouteIntermediateSupportSelf01Refusal&)
    -> BoardingRouteIntermediateSupportSelf01State;
auto boarding_route_intermediate_support_self01_prefix_bridge(
    const BoardingRouteIntermediateSupportSelf01CurrentCellToken&,
    BoardingRouteIntermediateSupportSelf01Diagnostic&,
    BoardingRouteIntermediateSupportSelf01Cell&,
    const BoardingRouteIntermediateSupportSelf01Limits&,
    BoardingRouteIntermediateSupportSelf01Refusal&)
    -> BoardingRouteIntermediateSupportSelf01State;
auto boarding_route_intermediate_support_self01_load_bridge(
    const BoardingRouteIntermediateSupportSelf01CurrentCellToken&,
    BoardingRouteIntermediateSupportSelf01Diagnostic&,
    BoardingRouteIntermediateSupportSelf01Cell&,
    const BoardingRouteIntermediateSupportSelf01Limits&,
    BoardingRouteIntermediateSupportSelf01Refusal&)
    -> BoardingRouteIntermediateSupportSelf01State;
auto boarding_route_intermediate_support_self01_allocate(
    BoardingRouteIntermediateSupportSelf01Diagnostic&,
    BoardingRouteIntermediateSupportSelf01Cell&,
    const BoardingRouteIntermediateSupportSelf01Limits&,
    BoardingRouteIntermediateSupportSelf01Refusal&) -> bool;
auto boarding_route_intermediate_support_self01_enroll_self_source(
    BoardingRouteIntermediateSupportSelf01Diagnostic&,
    const BoardingRouteIntermediateSupportSelf01Limits&,
    BoardingRouteIntermediateSupportSelf01Refusal&) -> bool;
auto boarding_route_intermediate_support_self01_self_bridge(
    const BoardingRouteIntermediateSupportSelf01CurrentCellToken&,
    BoardingRouteIntermediateSupportSelf01Diagnostic&,
    BoardingRouteIntermediateSupportSelf01Cell&,
    const BoardingRouteIntermediateSupportSelf01Limits&,
    BoardingRouteIntermediateSupportSelf01Refusal&)
    -> BoardingRouteIntermediateSupportSelf01State;
} // namespace apsis_drift::detail
