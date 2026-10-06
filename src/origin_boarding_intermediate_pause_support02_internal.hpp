#pragma once
#include "apsis_drift/origin_boarding_intermediate_pause_support02.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
namespace apsis_drift::detail {
struct BoardingIntermediatePauseSupport02Limits {
  BoardingIntermediatePausePhaseLimits phase;
  std::size_t projection_guards{256}, pressure_candidates{2}, disk_edges{16},
      output_bytes{4096}, sole_extrema{4}, source_coordinates{8},
      intersection_operations{4}, midpoint_operations{4},
      allocation_operations{6};
};
struct BoardingIntermediatePauseSupport02AllocationMathLimits {
  std::size_t sole_extrema{4}, source_coordinates{8},
      intersection_operations{4}, midpoint_operations{4},
      allocation_operations{6}, pressure_candidates{2};
};
struct BoardingIntermediatePauseSupport02AllocationMathInput {
  std::array<BoardingFootSiteScalarBounds, 2> com_xz, port_boot_center_xz;
  std::array<std::array<BoardingFootSiteScalarBounds, 2>, 4>
      port_source_vertices_xz;
};
struct BoardingIntermediatePauseSupport02AllocationCounters {
  std::size_t sole_extrema{}, source_coordinates{}, intersection_operations{},
      midpoint_operations{}, allocation_operations{}, pressure_candidates{},
      validation_guards{};
};
struct BoardingIntermediatePauseSupport02AllocationMath {
  std::array<BoardingFootSiteScalarBounds, 2> com_xz, port_boot_center_xz,
      port_sole_lower_xz, port_sole_upper_xz, port_source_lower_xz,
      port_source_upper_xz, intersection_lower_xz, intersection_upper_xz;
  std::array<std::array<BoardingFootSiteScalarBounds, 2>, 2> pressure_xz;
  std::array<std::array<bool, 2>, 4> source_coordinate_evaluated{};
  std::array<std::array<bool, 2>, 2> sole_extrema_evaluated{},
      intersection_evaluated{}, midpoint_evaluated{};
  std::array<std::array<bool, 3>, 2> allocation_evaluated{};
  std::array<bool, 2> pressure_evaluated{};
  std::array<bool, 18> validation_evaluated{};
  BoardingIntermediatePauseSupport02AllocationCounters work;
  std::optional<BoardingIntermediatePauseSupport02Refusal> first_refusal;
  BoardingIntermediatePauseSupport02Condition stop_condition{};
  BoardingIntermediatePauseSupport02State state{};
  bool arithmetic_supported{}, intersection_complete{}, nominal_equilibrium{},
      allocation_complete{}, complete{};
  static constexpr bool source_qualified{false}, body_qualified{false},
      fixture_qualified{false}, contact_qualified{false}, load_qualified{false},
      support_qualified{false}, self_qualified{false},
      material_qualified{false}, world_qualified{false}, route_qualified{false},
      seat_qualified{false}, actor_qualified{false}, save_qualified{false},
      dynamics_qualified{false}, first_flight_qualified{false};
};
[[nodiscard]] auto intermediate_pause_support02_request()
    -> std::optional<BoardingRouteFootPhaseRequest>;
[[nodiscard]] auto intermediate_pause_support02_bounded(
    const OriginBoardingIntermediatePauseSupport&, bool reverse,
    BoardingIntermediatePauseSupport02Limits = {})
    -> std::expected<BoardingIntermediatePauseSupport02Diagnostic, std::string>;
class BoardingIntermediatePauseSupport02ProjectionToken {
 private:
  BoardingIntermediatePauseSupport02ProjectionToken(
      const OriginBoardingIntermediatePauseSupport& p,
      const BoardingRouteFootPhaseCell& c,
      const BoardingRouteFootPhaseRequest& r,
      const BoardingIntermediatePauseSupport02Diagnostic& owner)
      : provider_(&p), cell_(&c), request_(&r), owner_(&owner) {}
  const OriginBoardingIntermediatePauseSupport* provider_;
  const BoardingRouteFootPhaseCell* cell_;
  const BoardingRouteFootPhaseRequest* request_;
  const BoardingIntermediatePauseSupport02Diagnostic* owner_;
  friend auto intermediate_pause_support02_bounded(
      const OriginBoardingIntermediatePauseSupport&, bool,
      BoardingIntermediatePauseSupport02Limits)
      -> std::expected<BoardingIntermediatePauseSupport02Diagnostic,
                       std::string>;
  friend auto intermediate_pause_support02_pressure_bridge(
      const BoardingIntermediatePauseSupport02ProjectionToken&,
      const BoardingIntermediatePauseSupport02Limits&,
      BoardingIntermediatePauseSupport02Diagnostic&) -> void;
};
[[nodiscard]] auto intermediate_pause_support02_allocation_math(
    const BoardingIntermediatePauseSupport02AllocationMathInput&,
    BoardingIntermediatePauseSupport02AllocationMathLimits = {})
    -> BoardingIntermediatePauseSupport02AllocationMath;
// Used only in the named production sequence, after private projection
// issuance. Arithmetic completion cannot itself set finite-contact/load
// permission.
[[nodiscard]] auto intermediate_pause_support02_allocate(
    BoardingIntermediatePauseSupport02Diagnostic&,
    const BoardingIntermediatePauseSupport02Limits&) -> bool;
[[nodiscard]] auto intermediate_pause_support02_definition_charge(
    BoardingIntermediatePauseSupport02Diagnostic&) -> bool;
auto intermediate_pause_support02_refuse(
    BoardingIntermediatePauseSupport02Diagnostic&,
    BoardingIntermediatePauseSupport02Condition,
    std::optional<std::size_t> side = {}, std::optional<std::size_t> edge = {},
    bool source_edge = false) -> void;
auto intermediate_pause_support02_pressure_bridge(
    const BoardingIntermediatePauseSupport02ProjectionToken&,
    const BoardingIntermediatePauseSupport02Limits&,
    BoardingIntermediatePauseSupport02Diagnostic&) -> void;
} // namespace apsis_drift::detail
