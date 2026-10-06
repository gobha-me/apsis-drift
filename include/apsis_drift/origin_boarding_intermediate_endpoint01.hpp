#pragma once
#include "apsis_drift/origin_boarding_intermediate_pause_support.hpp"
#include "apsis_drift/origin_boarding_route_checkpoint_unload_self02.hpp"
#include <expected>
#include <string>
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingIntermediateEndpoint01Version{1};
inline constexpr std::size_t
    kBoardingIntermediateEndpoint01MaximumExpectedBytes{1744},
    kBoardingIntermediateEndpoint01MaximumCellBytes{11032},
    kBoardingIntermediateEndpoint01MaximumScratchBytes{49152},
    kBoardingIntermediateEndpoint01MaximumOutputBytes{16 * std::size_t{1024} *
                                                      1024};
inline constexpr std::uint64_t kBoardingIntermediateEndpoint01BodyMask{
    0x7fffffffffffffffULL};
inline constexpr std::uint32_t kBoardingIntermediateEndpoint01UnitMask{
    0x00ffffffU};
enum class BoardingIntermediateEndpoint01Candidate : std::uint8_t {
  authored_descent_midpoint = 0
};
enum class BoardingIntermediateEndpoint01State : std::uint8_t {
  not_run,
  accepted,
  unresolved,
  witness_refused,
  capacity,
  unsupported
};
enum class BoardingIntermediateEndpoint01Condition : std::uint8_t {
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
  incomplete_endpoint
};
enum class BoardingIntermediateEndpoint01SelfStage : std::uint8_t {
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
  complete
};
enum class BoardingIntermediateEndpoint01Certificate : std::uint8_t {
  not_run,
  convex_support_plane,
  cap_partner_support,
  original_axis_box_support,
  half_ray_angle_bound,
  shoulder_split,
  original_capsule_slab_complement
};
struct BoardingIntermediateEndpoint01Refusal {
  BoardingIntermediateEndpoint01Condition condition{}, predicate_condition{};
  BoardingFootSiteScalarBounds limiting_bound;
  BoardingRouteFootPhaseRefusal phase;
  std::optional<std::size_t> side, edge, axis;
  std::optional<LowerCockpitTriangleKey> source_key;
  std::string_view source_name;
  std::uint16_t self_pair{65535};
  std::uint8_t operation{255}, self_region{255}, self_axis{255}, self_sign{255};
  BoardingIntermediateEndpoint01SelfStage self_stage{};
  bool source_edge{};
};
struct BoardingIntermediateEndpoint01Counters {
  std::uint64_t phase_calls{};
  BoardingRouteFootPhaseCounters phase;
  std::uint64_t source_guards{}, projection_guards{}, definition_guards{},
      sole_extrema{}, source_coordinates{}, intersection_operations{},
      midpoint_operations{}, allocation_operations{}, pressure_candidates{},
      disk_edges{}, self_body_guards{}, self_pairs{}, self_axes{},
      self_signed_trials{}, self_owners{}, self_hip_complements{},
      unit_axis_operations{};
};
struct BoardingIntermediateEndpoint01Cell {
  BoardingRouteFootPhaseCell phase;
  BoardingFootSiteScalarBounds port_share, star_share;
  std::array<std::array<BoardingFootSiteScalarBounds, 2>, 2> pressure_xz;
  std::array<BoardingIntermediatePauseSiteEvidence, 2> sites;
  std::array<BoardingLowerFootTransferOwner, kBoardingSelfConnectedRegionCount>
      owners;
  std::array<BoardingRouteCheckpointUnloadHipComplement, 2> hip_complements;
  std::array<std::array<BoardingFootSiteScalarBounds, 3>, 4> unit_axes;
  std::array<BoardingIntermediateEndpoint01Certificate, kBoardingBodyPairCount>
      self_certificates{};
  std::array<std::uint8_t, kBoardingBodyPairCount> self_axes = [] {
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
  BoardingIntermediateEndpoint01State state{};
};
struct BoardingIntermediateEndpoint01Diagnostic {
  OriginBoardingIntermediatePauseSupport source;
  std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount> parts;
  BoardingIntermediateEndpoint01Counters work;
  std::vector<BoardingIntermediateEndpoint01Cell> cells;
  std::optional<BoardingIntermediateEndpoint01Refusal> first_refusal;
  std::uint64_t source_evaluated{};
  std::size_t output_capacity_bytes{};
  double reporting_elapsed_seconds{};
  std::uint32_t version{kBoardingIntermediateEndpoint01Version},
      self_version{1};
  BoardingIntermediateEndpoint01Candidate candidate{};
  BoardingIntermediateEndpoint01Condition stop_condition{};
  BoardingIntermediateEndpoint01State state{};
  BoardingIntermediateEndpoint01SelfStage stop_stage{};
  std::uint16_t stop_self_pair{65535};
  std::uint8_t stop_operation{255}, stop_self_region{255}, stop_self_axis{255},
      stop_self_sign{255};
  bool arithmetic_supported{}, source_enrolled{}, kinematic_complete{},
      constant_state{}, projection_complete{}, plane_identities{},
      nominal_equilibrium{}, finite_contact_supported{},
      nominal_load_supported{}, nominal_support_complete{},
      self_body_complete{}, self_complete{}, complete{};
  explicit BoardingIntermediateEndpoint01Diagnostic(
      const OriginBoardingIntermediatePauseSupport& p)
      : source(p) {}
  static constexpr bool world_qualified{false}, material_qualified{false},
      halo_qualified{false}, route_qualified{false}, seat_qualified{false},
      actor_qualified{false}, save_qualified{false}, dynamics_qualified{false},
      strength_qualified{false}, friction_qualified{false},
      first_flight_qualified{false};
};
using BoardingIntermediateEndpoint01Expected =
    std::expected<BoardingIntermediateEndpoint01Diagnostic, std::string>;
[[nodiscard]] auto assess_origin_boarding_intermediate_endpoint01(
    const OriginBoardingIntermediatePauseSupport&,
    BoardingIntermediateEndpoint01Candidate)
    -> BoardingIntermediateEndpoint01Expected;
} // namespace apsis_drift
