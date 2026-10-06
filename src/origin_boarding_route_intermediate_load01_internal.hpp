#pragma once
#include "apsis_drift/origin_boarding_route_intermediate_load01.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
namespace apsis_drift::detail {
struct BoardingRouteIntermediateLoad01Limits {
  BoardingRouteFootPhaseLimits phase;
  std::size_t source_guards{18}, projection_guards{std::size_t{251} * 2047},
      definition_guards{std::size_t{31} * 2047},
      pressure_candidates{std::size_t{2} * 2047},
      disk_edges{std::size_t{16} * 2047}, sole_extrema{std::size_t{4} * 2047},
      source_coordinates{std::size_t{8} * 2047},
      intersection_operations{std::size_t{4} * 2047},
      midpoint_operations{std::size_t{4} * 2047},
      allocation_operations{std::size_t{6} * 2047},
      reaction_operations{std::size_t{3} * 2047},
      division_quotients{std::size_t{8} * 2047},
      division_folds{std::size_t{12} * 2047};
};
struct BoardingRouteIntermediateLoad01DivisionLimits {
  std::size_t quotients{4}, folds{6};
};
struct BoardingRouteIntermediateLoad01DivisionCounters {
  std::size_t quotients{}, folds{}, validation_guards{};
};
struct BoardingRouteIntermediateLoad01DivisionMath {
  BoardingFootSiteScalarBounds numerator, denominator, bound;
  std::array<BoardingFootSiteScalarBounds, 4> quotients;
  std::array<bool, 4> quotient_evaluated{};
  std::array<std::array<bool, 2>, 3> fold_evaluated{};
  std::array<bool, 3> input_validation_evaluated{};
  BoardingRouteIntermediateLoad01DivisionCounters work;
  BoardingRouteIntermediateLoad01Condition condition{};
  bool arithmetic_supported{}, input_validated{}, complete{};
  static constexpr bool source_qualified{false}, body_qualified{false},
      contact_qualified{false}, load_qualified{false}, support_qualified{false},
      self_qualified{false}, material_qualified{false}, world_qualified{false},
      route_qualified{false}, seat_qualified{false}, actor_qualified{false},
      save_qualified{false}, dynamics_qualified{false},
      first_flight_qualified{false};
};
[[nodiscard]] auto boarding_route_intermediate_load01_controls(
    std::size_t canonical_phase)
    -> std::optional<BoardingRouteFootPhaseRequest>;
[[nodiscard]] auto boarding_route_intermediate_load01_phase(double first,
                                                            double last)
    -> std::optional<std::size_t>;
[[nodiscard]] auto boarding_route_intermediate_load01_local(
    std::size_t canonical_phase, double global) -> double;
[[nodiscard]] auto boarding_route_intermediate_load01_clock(double global)
    -> double;
[[nodiscard]] auto boarding_route_intermediate_load01_join_math(
    const BoardingRouteFootPhaseRequest&, const BoardingRouteFootPhaseRequest&)
    -> bool;
[[nodiscard]] auto boarding_route_intermediate_load01_bounded(
    const OriginBoardingIntermediatePauseSupport&, double first, double last,
    BoardingRouteIntermediateLoad01Limits = {})
    -> std::expected<BoardingRouteIntermediateLoad01Diagnostic, std::string>;
[[nodiscard]] auto boarding_route_intermediate_load01_division_math(
    BoardingFootSiteScalarBounds numerator,
    BoardingFootSiteScalarBounds denominator,
    BoardingRouteIntermediateLoad01DivisionLimits = {})
    -> BoardingRouteIntermediateLoad01DivisionMath;
class BoardingRouteIntermediateLoad01AdmissionContext {
 private:
  explicit BoardingRouteIntermediateLoad01AdmissionContext(
      const BoardingRouteIntermediateLoad01Diagnostic& d)
      : owner_(&d),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)),
        controls_(&d.controls), parts_(&d.parts) {}
  const BoardingRouteIntermediateLoad01Diagnostic* owner_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  const std::array<BoardingRouteFootPhaseRequest, 2>* controls_;
  const std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount>*
      parts_;
  friend auto boarding_route_intermediate_load01_bounded(
      const OriginBoardingIntermediatePauseSupport&, double, double,
      BoardingRouteIntermediateLoad01Limits)
      -> std::expected<BoardingRouteIntermediateLoad01Diagnostic, std::string>;
  friend auto boarding_route_intermediate_load01_current_cell(
      const BoardingRouteIntermediateLoad01AdmissionContext&,
      BoardingRouteIntermediateLoad01Diagnostic&,
      BoardingRouteIntermediateLoad01Cell&,
      const BoardingRouteIntermediateLoad01Limits&,
      BoardingRouteIntermediateLoad01Refusal&)
      -> BoardingRouteIntermediateLoad01State;
  friend auto boarding_route_intermediate_load01_pressure_bridge(
      const class BoardingRouteIntermediateLoad01CurrentCellToken&,
      BoardingRouteIntermediateLoad01Diagnostic&,
      BoardingRouteIntermediateLoad01Cell&,
      const BoardingRouteIntermediateLoad01Limits&,
      BoardingRouteIntermediateLoad01Refusal&)
      -> BoardingRouteIntermediateLoad01State;
};
// An unreturned context can be constructed only after the named consumer's
// actual once-source admission; caller-created report flags cannot issue it.
[[nodiscard]] auto boarding_route_intermediate_load01_current_cell(
    const BoardingRouteIntermediateLoad01AdmissionContext&,
    BoardingRouteIntermediateLoad01Diagnostic&,
    BoardingRouteIntermediateLoad01Cell&,
    const BoardingRouteIntermediateLoad01Limits&,
    BoardingRouteIntermediateLoad01Refusal&)
    -> BoardingRouteIntermediateLoad01State;
class BoardingRouteIntermediateLoad01CurrentCellToken {
 private:
  BoardingRouteIntermediateLoad01CurrentCellToken(
      const BoardingRouteIntermediateLoad01Diagnostic& d,
      const BoardingRouteIntermediateLoad01Cell& c,
      const BoardingRouteFootPhaseRequest& r,
      const BoardingRouteIntermediateLoad01AdmissionContext& context)
      : context_(&context), owner_(&d), cell_(&c), request_(&r),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)) {}
  const BoardingRouteIntermediateLoad01AdmissionContext* context_;
  const BoardingRouteIntermediateLoad01Diagnostic* owner_;
  const BoardingRouteIntermediateLoad01Cell* cell_;
  const BoardingRouteFootPhaseRequest* request_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  friend auto boarding_route_intermediate_load01_current_cell(
      const BoardingRouteIntermediateLoad01AdmissionContext&,
      BoardingRouteIntermediateLoad01Diagnostic&,
      BoardingRouteIntermediateLoad01Cell&,
      const BoardingRouteIntermediateLoad01Limits&,
      BoardingRouteIntermediateLoad01Refusal&)
      -> BoardingRouteIntermediateLoad01State;
  friend auto boarding_route_intermediate_load01_pressure_bridge(
      const BoardingRouteIntermediateLoad01CurrentCellToken&,
      BoardingRouteIntermediateLoad01Diagnostic&,
      BoardingRouteIntermediateLoad01Cell&,
      const BoardingRouteIntermediateLoad01Limits&,
      BoardingRouteIntermediateLoad01Refusal&)
      -> BoardingRouteIntermediateLoad01State;
};
[[nodiscard]] auto boarding_route_intermediate_load01_definition_charge(
    BoardingRouteIntermediateLoad01Diagnostic&,
    BoardingRouteIntermediateLoad01Cell&,
    const BoardingRouteIntermediateLoad01Limits&,
    BoardingRouteIntermediateLoad01Refusal&) -> bool;
[[nodiscard]] auto boarding_route_intermediate_load01_allocate(
    BoardingRouteIntermediateLoad01Diagnostic&,
    BoardingRouteIntermediateLoad01Cell&,
    const BoardingRouteIntermediateLoad01Limits&,
    BoardingRouteIntermediateLoad01Refusal&) -> bool;
[[nodiscard]] auto boarding_route_intermediate_load01_pressure_bridge(
    const BoardingRouteIntermediateLoad01CurrentCellToken&,
    BoardingRouteIntermediateLoad01Diagnostic&,
    BoardingRouteIntermediateLoad01Cell&,
    const BoardingRouteIntermediateLoad01Limits&,
    BoardingRouteIntermediateLoad01Refusal&)
    -> BoardingRouteIntermediateLoad01State;
} // namespace apsis_drift::detail
