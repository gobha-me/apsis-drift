#pragma once
#include "apsis_drift/origin_boarding_intermediate_pause_support.hpp"
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingRouteIntermediateSupport01Version{1};
inline constexpr std::size_t kBoardingRouteIntermediateSupport01PhaseCount{5},
    kBoardingRouteIntermediateSupport01MaximumExpectedBytes{1792},
    kBoardingRouteIntermediateSupport01MaximumDepth{10},
    kBoardingRouteIntermediateSupport01MaximumNodes{2047},
    kBoardingRouteIntermediateSupport01MaximumLeaves{1024},
    kBoardingRouteIntermediateSupport01MaximumCellBytes{9784},
    kBoardingRouteIntermediateSupport01MaximumOutputBytes{
        16 * std::size_t{1024} * 1024},
    kBoardingRouteIntermediateSupport01MaximumScratchBytes{49152};
enum class BoardingRouteIntermediateSupport01State : std::uint8_t {
  not_run,
  accepted,
  unresolved,
  witness_refused,
  capacity,
  unsupported
};
enum class BoardingRouteIntermediateSupport01Condition : std::uint8_t {
  none,
  invalid_range,
  invalid_limits,
  invalid_binding,
  output_capacity,
  source_guard_capacity,
  source_identity,
  unsupported_arithmetic,
  phase_prerequisite,
  projection_capacity,
  projection_identity,
  definition_capacity,
  reaction_capacity,
  reaction_identity,
  sole_plane,
  source_rectangle,
  empty_intersection,
  denominator,
  sole_extrema_capacity,
  source_coordinate_capacity,
  intersection_capacity,
  midpoint_capacity,
  allocation_capacity,
  division_quotient_capacity,
  division_fold_capacity,
  pressure_capacity,
  edge_capacity,
  division_input,
  symbolic_equilibrium,
  sole_disk,
  source_disk,
  node_capacity,
  leaf_capacity,
  depth_capacity,
  unsplittable_interval,
  incomplete_cover,
  event_guard_capacity,
  event_identity,
  upper_edge_capacity,
  upper_start_witness,
  upper_departure,
  endpoint_operation_capacity,
  endpoint_overlap,
  endpoint_consistency,
};
enum class BoardingRouteIntermediateSupport01PortForceState : std::uint8_t {
  not_run,
  zero_only,
  zero_boundary_positive_interior,
  everywhere_positive
};
enum class BoardingRouteIntermediateSupport01UpperClass : std::uint8_t {
  not_run,
  strict_center_overlap,
  strict_Z_disjoint,
  mixed_possible_transition
};
enum class BoardingRouteIntermediateSupport01PlaneEvent : std::uint8_t {
  not_run,
  equal,
  above,
  below,
  equal_boundary_above_interior,
  equal_boundary_below_interior
};
enum class BoardingRouteIntermediateSupport01Protocol : std::uint8_t {
  unloaded_prefix,
  load_acquisition
};
struct BoardingRouteIntermediateSupport01Refusal {
  BoardingRouteIntermediateSupport01Condition condition{},
      predicate_condition{};
  bool source_edge{};
  BoardingFootSiteScalarBounds limiting_bound;
  BoardingRouteFootPhaseRefusal phase;
  double first{}, last{}, local_first{}, local_last{};
  std::size_t depth{};
  std::optional<std::size_t> phase_index, side, edge, axis, operation;
  std::optional<LowerCockpitTriangleKey> source_key;
  std::string_view source_name;
};
struct BoardingRouteIntermediateSupport01Counters {
  std::size_t phase_calls{};
  BoardingRouteFootPhaseCounters phase;
  std::size_t source_guards{}, projection_guards{}, definition_guards{},
      pressure_candidates{}, disk_edges{}, sole_extrema{}, source_coordinates{},
      intersection_operations{}, midpoint_operations{}, allocation_operations{},
      reaction_operations{}, division_quotients{}, division_folds{},
      upper_geometry_edges{}, endpoint_operations{}, event_guards{};
};
struct BoardingRouteIntermediateSupport01Cell {
  double global_first{}, global_last{};
  std::size_t phase_index{};
  BoardingRouteIntermediateSupport01Protocol protocol{};
  BoardingRouteFootPhaseCell phase;
  BoardingFootSiteScalarBounds port_share, star_share;
  std::array<std::array<BoardingFootSiteScalarBounds, 2>, 2> pressure_xz;
  std::array<BoardingIntermediatePauseSiteEvidence, 2> sites;
  std::array<bool, 11> projected_carrier_complete{};
  std::array<bool, 32> definition_evaluated{};
  std::array<bool, 3> reaction_evaluated{};
  std::array<std::array<bool, 2>, 2> sole_extrema_evaluated{},
      intersection_evaluated{}, midpoint_evaluated{};
  std::array<std::array<bool, 2>, 4> source_coordinate_evaluated{};
  std::array<std::array<bool, 3>, 2> allocation_evaluated{};
  std::array<std::array<bool, 4>, 2> division_quotient_evaluated{};
  std::array<std::array<std::array<bool, 2>, 3>, 2> division_fold_evaluated{};
  std::array<bool, 2> pressure_evaluated{};
  std::array<bool, 16> event_evaluated{};
  std::array<bool, 3> endpoint_scope{}, endpoint_evaluated{}, endpoint_earned{};
  BoardingRouteIntermediateSupport01UpperClass upper_class{};
  std::array<BoardingFootSiteScalarBounds, 2> upper_minimum_signed_side;
  std::array<std::array<bool, 4>, 2> upper_side_evaluated{};
  BoardingFootSiteScalarBounds departure_gap;
  std::array<std::array<BoardingFootSiteScalarBounds, 2>, 2>
      intermediate_intersection;
  std::array<bool, 2> positive_overlap{};

  std::array<bool, 12> endpoint_operation_evaluated{};
  std::size_t endpoint_operation_count{};
  BoardingRouteIntermediateSupport01PlaneEvent upper_plane_event{},
      intermediate_plane_event{};
  BoardingRouteIntermediateSupport01PortForceState port_force_state{};
  bool port_contact_geometry{}, port_positive_interior{},
      star_everywhere_positive{}, contains_zero_endpoint{},
      checkpoint_threshold_met{}, projection_complete{}, plane_identities{},
      nominal_equilibrium{}, star_support{}, prefix_zero_geometry_complete{},
      nominal_support_complete{}, complete{};
  BoardingRouteIntermediateSupport01State state{};
};
struct BoardingRouteIntermediateSupport01Diagnostic {
  OriginBoardingIntermediatePauseSupport source;
  std::uint32_t support_version{kBoardingRouteIntermediateSupport01Version};
  std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount> parts;
  double requested_first{}, requested_last{}, reporting_elapsed_seconds{};
  bool reverse{};
  BoardingRouteIntermediateSupport01Counters work;
  std::size_t examined_nodes{}, mandatory_splits{}, maximum_depth{},
      output_capacity_bytes{};
  std::array<bool, 36> source_evaluated{};
  std::array<bool, 4> join_expression_identity{}, qualified_joins{};
  std::array<bool, 3> prefix_endpoint_scope{}, prefix_endpoint_earned{};
  std::vector<BoardingRouteIntermediateSupport01Cell> cells;
  std::optional<BoardingRouteIntermediateSupport01Refusal> first_refusal;
  BoardingRouteIntermediateSupport01Condition stop_condition{};
  BoardingRouteIntermediateSupport01State state{};
  bool arithmetic_supported{}, source_enrolled{}, kinematic_complete{},
      nonnegative_reactions{}, nominal_equilibrium{}, star_support{},
      prefix_zero_geometry_complete{}, nominal_support_complete{}, complete{};
  explicit BoardingRouteIntermediateSupport01Diagnostic(
      const OriginBoardingIntermediatePauseSupport& p)
      : source(p) {}
  static constexpr bool self_qualified{false}, material_qualified{false},
      world_qualified{false}, route_qualified{false}, seat_qualified{false},
      actor_qualified{false}, save_qualified{false}, friction_qualified{false},
      strength_qualified{false}, dynamics_qualified{false},
      first_flight_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_route_intermediate_support01(
    const OriginBoardingIntermediatePauseSupport&, double first = 0,
    double last = 1)
    -> std::expected<BoardingRouteIntermediateSupport01Diagnostic, std::string>;
} // namespace apsis_drift
