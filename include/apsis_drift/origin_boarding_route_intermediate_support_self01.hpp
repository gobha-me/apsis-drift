#pragma once
#include "apsis_drift/origin_boarding_intermediate_pause_support.hpp"
#include "apsis_drift/origin_boarding_route_checkpoint_unload_self02.hpp"
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingRouteIntermediateSupportSelf01Version{
    1};
inline constexpr std::size_t kBoardingRouteIntermediateSupportSelf01PhaseCount{
    5},
    kBoardingRouteIntermediateSupportSelf01MaximumExpectedBytes{1744},
    kBoardingRouteIntermediateSupportSelf01MaximumDepth{10},
    kBoardingRouteIntermediateSupportSelf01MaximumNodes{2047},
    kBoardingRouteIntermediateSupportSelf01MaximumLeaves{1024},
    kBoardingRouteIntermediateSupportSelf01MaximumCellBytes{11032},
    kBoardingRouteIntermediateSupportSelf01MaximumOutputBytes{
        16 * std::size_t{1024} * 1024},
    kBoardingRouteIntermediateSupportSelf01MaximumScratchBytes{49152};
enum class BoardingRouteIntermediateSupportSelf01State : std::uint8_t {
  not_run,
  accepted,
  unresolved,
  witness_refused,
  capacity,
  unsupported
};
enum class BoardingRouteIntermediateSupportSelf01Condition : std::uint8_t {
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
  self_body_capacity,
  self_body_identity,
  self_pair_capacity,
  self_axis_capacity,
  self_signed_capacity,
  self_owner_capacity,
  self_hip_capacity,
  self_region_identity,
  unresolved_self_pair,
  self_incomplete,
};
enum class BoardingRouteIntermediateSupportSelf01PortForceState : std::uint8_t {
  not_run,
  zero_only,
  zero_boundary_positive_interior,
  everywhere_positive
};
enum class BoardingRouteIntermediateSupportSelf01UpperClass : std::uint8_t {
  not_run,
  strict_center_overlap,
  strict_Z_disjoint,
  mixed_possible_transition
};
enum class BoardingRouteIntermediateSupportSelf01PlaneEvent : std::uint8_t {
  not_run,
  equal,
  above,
  below,
  equal_boundary_above_interior,
  equal_boundary_below_interior
};
enum class BoardingRouteIntermediateSupportSelf01Protocol : std::uint8_t {
  unloaded_prefix,
  load_acquisition
};
enum class BoardingRouteIntermediateSupportSelf01Certificate : std::uint8_t {
  not_run,
  convex_support_plane,
  cap_partner_support,
  original_axis_box_support,
  half_ray_angle_bound,
  shoulder_split,
  original_capsule_slab_complement
};
enum class BoardingRouteIntermediateSupportSelf01SelfStage : std::uint8_t {
  not_run,
  body,
  owner,
  hip,
  separation,
  complete
};
inline constexpr std::uint8_t
    kBoardingRouteIntermediateSupportSelf01NoSelectedAxis{255};
inline constexpr std::uint64_t kBoardingRouteIntermediateSupportSelf01BodyMask{
    0x7fffffffffffffffULL};
struct BoardingRouteIntermediateSupportSelf01Refusal {
  BoardingRouteIntermediateSupportSelf01Condition condition{},
      predicate_condition{};
  bool source_edge{};
  BoardingFootSiteScalarBounds limiting_bound;
  BoardingRouteFootPhaseRefusal phase;
  double first{}, last{}, local_first{}, local_last{};
  std::size_t depth{};
  std::optional<std::size_t> phase_index, side, edge, axis, operation;
  std::optional<LowerCockpitTriangleKey> source_key;
  std::string_view source_name;
  std::uint16_t self_pair{65535};
  std::uint8_t self_region{255}, self_axis{255}, self_sign{255};
  BoardingRouteIntermediateSupportSelf01SelfStage self_stage{};
};
struct BoardingRouteIntermediateSupportSelf01Counters {
  std::size_t phase_calls{};
  BoardingRouteFootPhaseCounters phase;
  std::size_t source_guards{}, projection_guards{}, definition_guards{},
      pressure_candidates{}, disk_edges{}, sole_extrema{}, source_coordinates{},
      intersection_operations{}, midpoint_operations{}, allocation_operations{},
      reaction_operations{}, division_quotients{}, division_folds{},
      upper_geometry_edges{}, endpoint_operations{}, event_guards{},
      self_body_guards{}, self_pairs{}, self_axes{}, self_signed_trials{},
      self_owners{}, self_hip_complements{};
};
struct BoardingRouteIntermediateSupportSelf01Cell {
  double global_first{}, global_last{};
  std::size_t phase_index{};
  BoardingRouteIntermediateSupportSelf01Protocol protocol{};
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
  BoardingRouteIntermediateSupportSelf01UpperClass upper_class{};
  std::array<BoardingFootSiteScalarBounds, 2> upper_minimum_signed_side;
  std::array<std::array<bool, 4>, 2> upper_side_evaluated{};
  BoardingFootSiteScalarBounds departure_gap;
  std::array<std::array<BoardingFootSiteScalarBounds, 2>, 2>
      intermediate_intersection;
  std::array<bool, 2> positive_overlap{};

  std::array<bool, 12> endpoint_operation_evaluated{};
  std::size_t endpoint_operation_count{};
  BoardingRouteIntermediateSupportSelf01PlaneEvent upper_plane_event{},
      intermediate_plane_event{};
  BoardingRouteIntermediateSupportSelf01PortForceState port_force_state{};
  bool port_contact_geometry{}, port_positive_interior{},
      star_everywhere_positive{}, contains_zero_endpoint{},
      checkpoint_threshold_met{}, projection_complete{}, plane_identities{},
      nominal_equilibrium{}, star_support{}, prefix_zero_geometry_complete{},
      nominal_support_complete{}, complete{};
  BoardingRouteIntermediateSupportSelf01State state{};
  std::array<BoardingLowerFootTransferOwner, kBoardingSelfConnectedRegionCount>
      owners;
  std::array<BoardingRouteCheckpointUnloadHipComplement, 2> hip_complements;
  std::array<BoardingRouteIntermediateSupportSelf01Certificate,
             kBoardingBodyPairCount>
      self_certificates{};
  std::array<std::uint8_t, kBoardingBodyPairCount> self_axes = [] {
    std::array<std::uint8_t, kBoardingBodyPairCount> result{};
    result.fill(kBoardingRouteIntermediateSupportSelf01NoSelectedAxis);
    return result;
  }();
  std::array<std::uint16_t, 7> self_certificate_counts{};
  std::uint64_t self_body_evaluated{};
  std::uint16_t owner_attempted_mask{}, examined_pairs{}, accepted_pairs{};
  bool self_body_complete{}, self_complete{};
};
struct BoardingRouteIntermediateSupportSelf01Diagnostic {
  OriginBoardingIntermediatePauseSupport source;
  std::uint32_t support_version{kBoardingRouteIntermediateSupportSelf01Version},
      self_version{1};
  std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount> parts;
  double requested_first{}, requested_last{}, reporting_elapsed_seconds{};
  bool reverse{};
  BoardingRouteIntermediateSupportSelf01Counters work;
  std::size_t examined_nodes{}, mandatory_splits{}, maximum_depth{},
      output_capacity_bytes{};
  std::array<bool, 67> source_evaluated{};
  std::array<bool, 4> join_expression_identity{}, qualified_joins{};
  std::array<bool, 3> prefix_endpoint_scope{}, prefix_endpoint_earned{};
  std::vector<BoardingRouteIntermediateSupportSelf01Cell> cells;
  std::optional<BoardingRouteIntermediateSupportSelf01Refusal> first_refusal;
  BoardingRouteIntermediateSupportSelf01Condition stop_condition{};
  BoardingRouteIntermediateSupportSelf01State state{};
  bool arithmetic_supported{}, source_enrolled{}, kinematic_complete{},
      nonnegative_reactions{}, nominal_equilibrium{}, star_support{},
      prefix_zero_geometry_complete{}, nominal_support_complete{},
      self_body_complete{}, self_complete{}, complete{};
  explicit BoardingRouteIntermediateSupportSelf01Diagnostic(
      const OriginBoardingIntermediatePauseSupport& p)
      : source(p) {}
  static constexpr bool material_qualified{false}, world_qualified{false},
      route_qualified{false}, seat_qualified{false}, actor_qualified{false},
      save_qualified{false}, friction_qualified{false},
      strength_qualified{false}, dynamics_qualified{false},
      first_flight_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_route_intermediate_support_self01(
    const OriginBoardingIntermediatePauseSupport&, double first = 0,
    double last = 1)
    -> std::expected<BoardingRouteIntermediateSupportSelf01Diagnostic,
                     std::string>;
} // namespace apsis_drift
