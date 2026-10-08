#pragma once
#include "apsis_drift/origin_boarding_intermediate_pause_support.hpp"
#include "apsis_drift/origin_boarding_route_checkpoint_unload_self02.hpp"
#include <expected>
#include <string>
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingIntermediateEndpoint06Version{6};
inline constexpr std::size_t
    kBoardingIntermediateEndpoint06MaximumExpectedBytes{1744},
    kBoardingIntermediateEndpoint06MaximumCellBytes{11032},
    kBoardingIntermediateEndpoint06MaximumScratchBytes{49152},
    kBoardingIntermediateEndpoint06MaximumOutputBytes{16 * std::size_t{1024} *
                                                      1024};
inline constexpr std::uint64_t kBoardingIntermediateEndpoint06BodyMask{
    0x7fffffffffffffffULL};
inline constexpr std::uint32_t kBoardingIntermediateEndpoint06UnitMask{
    0x00ffffffU};
enum class BoardingIntermediateEndpoint06Candidate : std::uint8_t {
  root_y_same_base_chart_factor_both_hip_ankle_reach_roll_slice = 0
};
enum class BoardingIntermediateEndpoint06State : std::uint8_t {
  not_run,
  accepted,
  unresolved,
  witness_refused,
  capacity,
  unsupported
};
enum class BoardingIntermediateEndpoint06Condition : std::uint8_t {
  none = 0,
  invalid_candidate,
  invalid_limits,
  invalid_binding,
  output_capacity,
  source_capacity,
  source_identity,
  phase_prerequisite,
  projection_capacity,
  projection_identity,
  definition_capacity,
  sole_plane,
  source_rectangle,
  empty_intersection,
  denominator,
  sole_extrema_capacity,
  source_coordinate_capacity,
  intersection_capacity,
  midpoint_capacity,
  allocation_capacity,
  pressure_capacity,
  edge_capacity,
  symbolic_equilibrium,
  sole_disk,
  source_disk,
  self_body_capacity,
  self_body_identity,
  unit_axis_capacity,
  unit_axis_identity,
  self_pair_capacity,
  self_axis_capacity,
  self_signed_capacity,
  self_owner_capacity,
  self_hip_capacity,
  self_region_identity,
  unresolved_self_pair,
  unsupported_arithmetic,
  incomplete_endpoint,
  slice_unavailable,
  slice_identity,
  slice_guard_capacity,
  slice_operation_capacity
};
enum class BoardingIntermediateEndpoint06SelfStage : std::uint8_t {
  not_run = 0,
  source,
  phase,
  projection,
  definition,
  sole_extrema,
  source_coordinates,
  intersection,
  midpoint,
  allocation,
  pressure_candidate,
  disk,
  body,
  unit_axes,
  pair,
  owner,
  hip,
  separation,
  complete,
  slice_guard,
  slice_operation
};
enum class BoardingIntermediateEndpoint06Certificate : std::uint8_t {
  not_run,
  convex_support_plane,
  cap_partner_support,
  original_axis_box_support,
  half_ray_angle_bound,
  shoulder_split,
  original_capsule_slab_complement
};
enum class BoardingIntermediateEndpoint06SliceCondition : std::uint8_t {
  none,
  identity,
  no_positive_upper,
  empty_inward_interval,
  candidate_domain,
  midpoint_unavailable,
  verification_inconclusive,
  guard_capacity,
  operation_capacity,
  unsupported_arithmetic,
  no_positive_roll_factor,
  no_tau_interval,
  endpoint_domain_unavailable,
  no_radial_interval,
  hip_threshold_domain,
  hip_descent_unavailable,
  hip_verification_inconclusive,
  base_interval_unavailable,
  height_floor_unavailable,
  chart_factor_unavailable
};
struct BoardingIntermediateEndpoint06SliceEvidence {
  std::array<std::uint64_t, 8> operation_attempted{}, operation_written{};
  std::array<std::uint64_t, 2> guard_attempted{}, guard_written{};
  BoardingFootSiteScalarBounds limiting_bound;
  double y{}, lo{}, hi{}, base_lo{}, base_hi{};
  std::array<double, 2> factor_lower{};
  std::uint16_t operation{65535};
  std::uint8_t guard{255}, side{255};
  BoardingIntermediateEndpoint06SliceCondition condition{};
  bool arithmetic_supported{}, complete{};
  std::uint8_t preflight_guards{};
  std::uint16_t zero_mask{};
};
struct BoardingIntermediateEndpoint06Refusal {
  BoardingIntermediateEndpoint06Condition condition{}, predicate_condition{};
  BoardingFootSiteScalarBounds limiting_bound;
  BoardingRouteFootPhaseRefusal phase;
  std::optional<std::size_t> side, edge, axis;
  std::optional<LowerCockpitTriangleKey> source_key;
  std::string_view source_name;
  std::uint16_t self_pair{65535};
  std::uint16_t operation{65535};
  std::uint8_t self_region{255}, self_axis{255}, self_sign{255};
  BoardingIntermediateEndpoint06SelfStage self_stage{};
  bool source_edge{};
};
struct BoardingIntermediateEndpoint06Counters {
  std::uint64_t phase_calls{};
  BoardingRouteFootPhaseCounters phase;
  std::uint64_t source_guards{}, projection_guards{}, definition_guards{},
      sole_extrema{}, source_coordinates{}, intersection_operations{},
      midpoint_operations{}, allocation_operations{}, pressure_candidates{},
      disk_edges{}, self_body_guards{}, self_pairs{}, self_axes{},
      self_signed_trials{}, self_owners{}, self_hip_complements{},
      unit_axis_operations{}, construction_guards{}, construction_operations{};
};
struct BoardingIntermediateEndpoint06Cell {
  BoardingRouteFootPhaseCell phase;
  BoardingFootSiteScalarBounds port_share, star_share;
  std::array<std::array<BoardingFootSiteScalarBounds, 2>, 2> pressure_xz;
  std::array<BoardingIntermediatePauseSiteEvidence, 2> sites;
  std::array<BoardingLowerFootTransferOwner, kBoardingSelfConnectedRegionCount>
      owners;
  std::array<BoardingRouteCheckpointUnloadHipComplement, 2> hip_complements;
  std::array<std::array<BoardingFootSiteScalarBounds, 3>, 4> unit_axes;
  std::array<BoardingIntermediateEndpoint06Certificate, kBoardingBodyPairCount>
      self_certificates{};
  std::array<std::uint8_t, kBoardingBodyPairCount> self_axes = []() noexcept {
    std::array<std::uint8_t, kBoardingBodyPairCount> a{};
    a.fill(255);
    return a;
  }();
  std::array<std::uint16_t, 7> self_certificate_counts{};
  std::uint64_t self_body_evaluated{};
  std::uint32_t definition_evaluated{}, unit_axis_attempted{},
      unit_axis_written{};
  std::uint16_t owner_attempted_mask{}, examined_pairs{}, accepted_pairs{};
  std::array<bool, 11> projected_carrier_complete{};
  std::array<std::array<bool, 2>, 2> sole_extrema_evaluated{},
      intersection_evaluated{}, midpoint_evaluated{};
  std::array<std::array<bool, 2>, 4> source_coordinate_evaluated{};
  std::array<std::array<bool, 3>, 2> allocation_evaluated{};
  std::array<bool, 2> pressure_evaluated{};
  std::uint8_t unit_axis_complete_mask{}, unit_axis_cursor{255};
  bool arithmetic_supported{}, kinematic_complete{}, constant_state{},
      projection_complete{}, plane_identities{}, nominal_equilibrium{},
      finite_contact_supported{}, nominal_load_supported{},
      nominal_support_complete{}, star_support{}, self_body_complete{},
      self_complete{}, complete{};
  BoardingIntermediateEndpoint06State state{};
};
struct BoardingIntermediateEndpoint06Diagnostic {
  OriginBoardingIntermediatePauseSupport source;
  std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount> parts;
  BoardingIntermediateEndpoint06Counters work;
  BoardingIntermediateEndpoint06SliceEvidence slice;
  std::vector<BoardingIntermediateEndpoint06Cell> cells;
  std::optional<BoardingIntermediateEndpoint06Refusal> first_refusal;
  std::uint64_t source_evaluated{};
  std::size_t output_capacity_bytes{};
  double reporting_elapsed_seconds{};
  std::uint32_t version{kBoardingIntermediateEndpoint06Version},
      self_version{1};
  BoardingIntermediateEndpoint06Candidate candidate{};
  BoardingIntermediateEndpoint06Condition stop_condition{};
  BoardingIntermediateEndpoint06State state{};
  BoardingIntermediateEndpoint06SelfStage stop_stage{};
  std::uint16_t stop_self_pair{65535};
  std::uint16_t stop_operation{65535};
  std::uint8_t stop_self_region{255}, stop_self_axis{255}, stop_self_sign{255};
  bool arithmetic_supported{}, source_enrolled{}, kinematic_complete{},
      constant_state{}, projection_complete{}, plane_identities{},
      nominal_equilibrium{}, finite_contact_supported{},
      nominal_load_supported{}, nominal_support_complete{},
      self_body_complete{}, self_complete{}, complete{};
  explicit BoardingIntermediateEndpoint06Diagnostic(
      const OriginBoardingIntermediatePauseSupport& p)
      : source(p) {}
  static constexpr bool world_qualified{false}, material_qualified{false},
      halo_qualified{false}, route_qualified{false}, seat_qualified{false},
      actor_qualified{false}, save_qualified{false}, dynamics_qualified{false},
      strength_qualified{false}, friction_qualified{false},
      first_flight_qualified{false};
};
using BoardingIntermediateEndpoint06Expected =
    std::expected<BoardingIntermediateEndpoint06Diagnostic, std::string>;
[[nodiscard]] auto assess_origin_boarding_intermediate_endpoint06(
    const OriginBoardingIntermediatePauseSupport&,
    BoardingIntermediateEndpoint06Candidate)
    -> BoardingIntermediateEndpoint06Expected;
[[nodiscard]] constexpr auto
boarding_intermediate_endpoint06_required_output_bytes() -> std::size_t {
  return 2 * sizeof(BoardingIntermediateEndpoint06Expected) +
         sizeof(BoardingIntermediateEndpoint06Cell);
}
} // namespace apsis_drift
