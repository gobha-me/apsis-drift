#pragma once
#include "apsis_drift/origin_boarding_intermediate_pause_support.hpp"
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingRouteIntermediateUnloaded01Version{1};
inline constexpr std::size_t
    kBoardingRouteIntermediateUnloaded01MaximumCellBytes{9000},
    kBoardingRouteIntermediateUnloaded01MaximumExpectedBytes{2048},
    kBoardingRouteIntermediateUnloaded01MaximumScratchBytes{49152},
    kBoardingRouteIntermediateUnloaded01MaximumOutputBytes{
        16 * std::size_t{1024} * 1024};
enum class BoardingRouteIntermediateUnloaded01State : std::uint8_t {
  not_run,
  accepted,
  unresolved,
  witness_refused,
  capacity,
  unsupported
};
enum class BoardingRouteIntermediateUnloaded01Condition : std::uint8_t {
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
  sole_plane,
  pressure_capacity,
  edge_capacity,
  sole_disk,
  source_disk,
  event_guard_capacity,
  event_identity,
  upper_edge_capacity,
  upper_start_witness,
  upper_departure,
  source_coordinate_capacity,
  endpoint_operation_capacity,
  source_rectangle,
  endpoint_overlap,
  endpoint_consistency,
  node_capacity,
  leaf_capacity,
  depth_capacity,
  unsplittable_interval,
  incomplete_cover
};
enum class BoardingRouteIntermediateUnloaded01UpperClass : std::uint8_t {
  not_run,
  strict_center_overlap,
  strict_Z_disjoint,
  mixed_possible_transition
};
enum class BoardingRouteIntermediateUnloaded01PlaneEvent : std::uint8_t {
  not_run,
  equal,
  above,
  below,
  equal_boundary_above_interior,
  equal_boundary_below_interior
};
struct BoardingRouteIntermediateUnloaded01Refusal {
  BoardingRouteIntermediateUnloaded01Condition condition{},
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
struct BoardingRouteIntermediateUnloaded01Counters {
  std::size_t phase_calls{};
  BoardingRouteFootPhaseCounters phase;
  std::size_t source_guards{}, projection_guards{}, definition_guards{},
      pressure_candidates{}, disk_edges{}, upper_geometry_edges{},
      intermediate_coordinates{}, endpoint_operations{}, event_guards{};
};
struct BoardingRouteIntermediateUnloaded01Cell {
  double global_first{}, global_last{};
  std::size_t phase_index{};
  BoardingRouteFootPhaseCell phase;
  BoardingIntermediatePauseSiteEvidence star_site;
  std::array<BoardingFootSiteScalarBounds, 2> com_xz, starcop_xz;
  BoardingFootSiteScalarBounds port_share, star_share;
  std::array<bool, 11> projected_carrier_complete{};
  std::array<bool, 32> definition_evaluated{};
  std::array<bool, 16> event_evaluated{};
  std::array<bool, 3> endpoint_scope{}, endpoint_evaluated{}, endpoint_earned{};
  BoardingRouteIntermediateUnloaded01UpperClass upper_class{};
  std::array<BoardingFootSiteScalarBounds, 2> upper_minimum_signed_side;
  std::array<std::array<bool, 4>, 2> upper_side_evaluated{};
  BoardingFootSiteScalarBounds departure_gap;
  std::array<std::array<BoardingFootSiteScalarBounds, 2>, 2>
      intermediate_intersection;
  std::array<bool, 2> positive_overlap{};
  std::array<bool, 8> intermediate_coordinate_evaluated{};
  std::array<bool, 12> endpoint_operation_evaluated{};
  std::size_t endpoint_operation_count{};
  BoardingRouteIntermediateUnloaded01PlaneEvent upper_plane_event{},
      intermediate_plane_event{};
  bool port_loaded{}, port_zero_only{}, projection_complete{},
      nominal_equilibrium{}, star_support{}, port_zero_geometry{}, complete{};
  BoardingRouteIntermediateUnloaded01State state{};
};
struct BoardingRouteIntermediateUnloaded01Diagnostic {
  OriginBoardingIntermediatePauseSupport source;
  std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount> parts;
  std::uint32_t unloaded_version{kBoardingRouteIntermediateUnloaded01Version};
  double requested_first{}, requested_last{}, reporting_elapsed_seconds{};
  bool reverse{};
  BoardingRouteIntermediateUnloaded01Counters work;
  std::size_t examined_nodes{}, mandatory_splits{}, maximum_depth{},
      output_capacity_bytes{};
  std::array<bool, 32> source_evaluated{};
  std::array<bool, 2> join_expression_identity{}, qualified_joins{};
  std::array<bool, 3> endpoint_scope{}, endpoint_earned{};
  std::vector<BoardingRouteIntermediateUnloaded01Cell> cells;
  std::optional<BoardingRouteIntermediateUnloaded01Refusal> first_refusal;
  BoardingRouteIntermediateUnloaded01Condition stop_condition{};
  BoardingRouteIntermediateUnloaded01State state{};
  bool complete{}, arithmetic_supported{}, source_enrolled{},
      kinematic_complete{}, nominal_equilibrium{}, star_support{},
      port_zero_geometry{};
  explicit BoardingRouteIntermediateUnloaded01Diagnostic(
      const OriginBoardingIntermediatePauseSupport& p)
      : source(p) {}
  static constexpr bool self_qualified{false}, material_qualified{false},
      world_qualified{false}, route_qualified{false}, seat_qualified{false},
      actor_qualified{false}, save_qualified{false}, dynamics_qualified{false},
      first_flight_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_route_intermediate_unloaded01(
    const OriginBoardingIntermediatePauseSupport&, double first = 0,
    double last = .5)
    -> std::expected<BoardingRouteIntermediateUnloaded01Diagnostic,
                     std::string>;
} // namespace apsis_drift
