#pragma once
#include "apsis_drift/origin_boarding_route_checkpoint_unload.hpp"
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingRouteCheckpointUnloadSelf02Version{2};
inline constexpr std::uint64_t
    kBoardingRouteCheckpointUnloadSelf02MaximumHipAttempts{4094};
enum class BoardingRouteCheckpointUnloadSelf02Certificate : std::uint8_t {
  none,
  convex_support_plane,
  cap_partner_support,
  original_axis_box_support,
  half_ray_angle_bound,
  shoulder_split,
  original_capsule_slab_complement
};
struct BoardingRouteCheckpointUnloadHipComplement {
  std::size_t side{}, region{}, pair{};
  double slab_limit_metres{};
  std::array<BoardingPlantedLegScalarBounds, 3> axis;
  BoardingPlantedLegScalarBounds transverse, extent, limit, strict_gap;
  bool attempted{}, arithmetic_supported{}, nominal_unit_identity{},
      upright_pelvis_identity{}, original_slab_identity{}, extent_evaluated{},
      certified{};
};
// One geometry packet. Inherited original certificates retain the original
// method's evidence; the separate seven-kind table governs this selected
// policy. assessment.self_complete/complete are selected-policy aggregates, not
// a claim that the original six-kind sufficient methods accepted every pair.
struct BoardingRouteCheckpointUnloadSelf02Cell
    : BoardingRouteCheckpointUnloadCell {
  std::array<BoardingRouteCheckpointUnloadSelf02Certificate,
             kBoardingBodyPairCount>
      self02_certificates{};
  std::array<std::uint16_t, 7> self02_certificate_counts{};
  std::array<BoardingRouteCheckpointUnloadHipComplement, 2> hip_complements;
};
enum class BoardingRouteCheckpointUnloadSelf02Condition : std::uint8_t {
  none,
  hip_complement_capacity
};
struct BoardingRouteCheckpointUnloadSelf02Refusal
    : BoardingRouteCheckpointUnloadRefusal {
  BoardingRouteCheckpointUnloadSelf02Condition self02_condition{};
  BoardingRouteCheckpointUnloadSelf02Refusal() = default;
  BoardingRouteCheckpointUnloadSelf02Refusal(
      const BoardingRouteCheckpointUnloadRefusal& original)
      : BoardingRouteCheckpointUnloadRefusal(original) {}
};
struct BoardingRouteCheckpointUnloadSelf02Diagnostic {
  std::unique_ptr<BoardingSourceEndpointLoadDiagnostic> initial;
  std::uint32_t self_policy_version{
      kBoardingRouteCheckpointUnloadSelf02Version};
  std::uint64_t hip_complement_attempts{};
  std::uint32_t unload_version{kBoardingRouteCheckpointUnloadVersion};
  std::array<BoardingRouteFootPhaseRequest, 3> controls;
  std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount> parts;
  double requested_first{}, requested_last{}, reporting_elapsed_seconds{};
  bool reverse{}, arithmetic_supported{}, kinematics_complete{},
      timing_complete{}, self_complete{}, nonnegative_reactions{},
      nominal_vertical_equilibrium_complete{}, finite_pressure_complete{},
      support_complete{}, endpoint_zero_port_reaction{}, complete{};
  // Expression ties alone do not qualify a requested two-sided physical join.
  std::array<bool, 2> join_expression_identity{}, qualified_joins{};
  std::size_t examined_nodes{}, mandatory_splits{}, maximum_depth{},
      output_capacity_bytes{};
  std::size_t owned_initial_quad_guards{}, owned_initial_quad_edges{},
      owned_initial_convexity_signs{}, owned_initial_triangle_windings{},
      owned_initial_diagonal_incidence_guards{};
  BoardingRoutePortUnloadCounters work;
  std::vector<BoardingRouteCheckpointUnloadSelf02Cell> cells;
  std::optional<BoardingRouteCheckpointUnloadSelf02Refusal> first_refusal;
  static constexpr bool world_qualified{false}, material_qualified{false},
      source_surface_sweep_qualified{false}, route_qualified{false},
      actor_qualified{false}, seat_qualified{false}, save_qualified{false},
      first_flight_qualified{false}, free_foot_swing_qualified{false},
      dynamics_qualified{false}, strength_qualified{false},
      friction_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_route_checkpoint_unload_self02(
    const OriginBoardingBootSupport&, double first = 0, double last = 1)
    -> std::expected<BoardingRouteCheckpointUnloadSelf02Diagnostic,
                     std::string>;
} // namespace apsis_drift
