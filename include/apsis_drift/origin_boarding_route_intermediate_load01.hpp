#pragma once
#include "apsis_drift/origin_boarding_intermediate_pause_support.hpp"
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingRouteIntermediateLoad01Version{1};
inline constexpr std::size_t kBoardingRouteIntermediateLoad01PhaseCount{2},
    kBoardingRouteIntermediateLoad01MaximumDepth{10},
    kBoardingRouteIntermediateLoad01MaximumNodes{2047},
    kBoardingRouteIntermediateLoad01MaximumLeaves{1024},
    kBoardingRouteIntermediateLoad01MaximumCellBytes{9784},
    kBoardingRouteIntermediateLoad01MaximumOutputBytes{16 * std::size_t{1024} *
                                                       1024},
    kBoardingRouteIntermediateLoad01MaximumScratchBytes{49152};
enum class BoardingRouteIntermediateLoad01State : std::uint8_t {
  not_run,
  accepted,
  unresolved,
  witness_refused,
  capacity,
  unsupported
};
enum class BoardingRouteIntermediateLoad01Condition : std::uint8_t {
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
  incomplete_cover
};
enum class BoardingRouteIntermediateLoad01PortForceState : std::uint8_t {
  not_run,
  zero_only,
  zero_boundary_positive_interior,
  everywhere_positive
};
struct BoardingRouteIntermediateLoad01Refusal {
  BoardingRouteIntermediateLoad01Condition condition{}, predicate_condition{};
  bool source_edge{};
  BoardingFootSiteScalarBounds limiting_bound;
  BoardingRouteFootPhaseRefusal phase;
  double first{}, last{}, local_first{}, local_last{};
  std::size_t depth{};
  std::optional<std::size_t> phase_index, side, edge, axis, operation;
  std::optional<LowerCockpitTriangleKey> source_key;
  std::string_view source_name;
};
struct BoardingRouteIntermediateLoad01Counters {
  std::size_t phase_calls{};
  BoardingRouteFootPhaseCounters phase;
  std::size_t source_guards{}, projection_guards{}, definition_guards{},
      pressure_candidates{}, disk_edges{}, sole_extrema{}, source_coordinates{},
      intersection_operations{}, midpoint_operations{}, allocation_operations{},
      reaction_operations{}, division_quotients{}, division_folds{};
};
struct BoardingRouteIntermediateLoad01Cell {
  double global_first{}, global_last{};
  std::size_t phase_index{};
  BoardingRouteFootPhaseCell phase;
  BoardingFootSiteScalarBounds port_share, star_share;
  std::array<std::array<BoardingFootSiteScalarBounds, 2>, 2> pressure_xz;
  std::array<BoardingIntermediatePauseSiteEvidence, 2> sites;
  std::array<bool, 11> projected_carrier_complete{};
  std::array<bool, 31> definition_evaluated{};
  std::array<bool, 3> reaction_evaluated{};
  std::array<std::array<bool, 2>, 2> sole_extrema_evaluated{},
      intersection_evaluated{}, midpoint_evaluated{};
  std::array<std::array<bool, 2>, 4> source_coordinate_evaluated{};
  std::array<std::array<bool, 3>, 2> allocation_evaluated{};
  std::array<std::array<bool, 4>, 2> division_quotient_evaluated{};
  std::array<std::array<std::array<bool, 2>, 3>, 2> division_fold_evaluated{};
  std::array<bool, 2> pressure_evaluated{};
  BoardingRouteIntermediateLoad01PortForceState port_force_state{};
  bool port_contact_geometry{}, port_positive_interior{},
      star_everywhere_positive{}, contains_zero_endpoint{},
      checkpoint_threshold_met{}, projection_complete{}, plane_identities{},
      nominal_equilibrium{}, finite_contact_complete{}, nominal_load_complete{},
      complete{};
};
struct BoardingRouteIntermediateLoad01Diagnostic {
  OriginBoardingIntermediatePauseSupport source;
  std::uint32_t load_version{kBoardingRouteIntermediateLoad01Version};
  std::array<BoardingRouteFootPhaseRequest, 2> controls;
  std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount> parts;
  double requested_first{}, requested_last{}, reporting_elapsed_seconds{};
  bool reverse{};
  BoardingRouteIntermediateLoad01Counters work;
  std::size_t examined_nodes{}, mandatory_splits{}, maximum_depth{},
      output_capacity_bytes{};
  std::array<bool, 18> source_evaluated{};
  bool join_expression_identity{}, qualified_join{};
  std::vector<BoardingRouteIntermediateLoad01Cell> cells;
  std::optional<BoardingRouteIntermediateLoad01Refusal> first_refusal;
  BoardingRouteIntermediateLoad01Condition stop_condition{};
  BoardingRouteIntermediateLoad01State state{};
  bool arithmetic_supported{}, source_enrolled{}, kinematic_complete{},
      nonnegative_reactions{}, nominal_equilibrium{}, finite_contact_complete{},
      nominal_load_complete{}, complete{};
  explicit BoardingRouteIntermediateLoad01Diagnostic(
      const OriginBoardingIntermediatePauseSupport& p)
      : source(p) {}
  static constexpr bool self_qualified{false}, material_qualified{false},
      world_qualified{false}, route_qualified{false}, seat_qualified{false},
      actor_qualified{false}, save_qualified{false}, friction_qualified{false},
      strength_qualified{false}, dynamics_qualified{false},
      first_flight_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_route_intermediate_load01(
    const OriginBoardingIntermediatePauseSupport&, double first = .5,
    double last = 1)
    -> std::expected<BoardingRouteIntermediateLoad01Diagnostic, std::string>;
} // namespace apsis_drift
