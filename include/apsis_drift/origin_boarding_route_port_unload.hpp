#pragma once
#include "apsis_drift/origin_boarding_lower_foot_transfer.hpp"
#include "apsis_drift/origin_boarding_route_foot_phase.hpp"
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingRoutePortUnloadVersion{1};
inline constexpr std::size_t kBoardingRoutePortUnloadMaximumCellBytes{12288},
    kBoardingRoutePortUnloadMaximumOutputBytes{16 * std::size_t{1024} * 1024},
    kBoardingRoutePortUnloadMaximumScratchBytes{48 * std::size_t{1024}};
// endpoint_zero_port_reaction is a witness at canonical u1, not a zero share
// throughout.
struct BoardingRoutePortUnloadCell {
  BoardingRouteFootPhaseCell phase;
  std::array<BoardingLowerFootTransferOwner, kBoardingSelfConnectedRegionCount>
      owners;
  std::array<BoardingSourceEndpointSelfCertificate, kBoardingBodyPairCount>
      pair_certificates{};
  std::array<std::uint16_t, 6> certificate_counts{};
  std::array<BoardingLowerFootTransferPressure, 2> pressures;
  BoardingPlantedLegScalarBounds port_reaction_fraction;
  std::array<BoardingPlantedLegScalarBounds, 2> barycenter_xz,
      common_pressure_delta_xz;
  bool arithmetic_supported{}, self_complete{}, nonnegative_reactions{},
      nominal_vertical_equilibrium_complete{}, finite_pressure_complete{},
      endpoint_zero_port_reaction{}, complete{};
};
struct BoardingRoutePortUnloadCounters {
  BoardingRouteFootPhaseCounters phase;
  BoardingLowerFootTransferCounters contact;
  std::uint64_t ownership_attempts{};
};
enum class BoardingRoutePortUnloadCondition : std::uint8_t {
  none,
  initial_prerequisite,
  invalid_binding,
  unsupported_arithmetic,
  phase_predicate,
  unresolved_self_pair,
  sole_disk_margin,
  source_disk_margin,
  node_capacity,
  leaf_capacity,
  depth_capacity,
  output_capacity,
  pair_capacity,
  axis_capacity,
  signed_trial_capacity,
  ownership_capacity,
  pressure_capacity,
  edge_capacity,
  unsplittable_interval,
  incomplete_cover
};
struct BoardingRoutePortUnloadRefusal {
  double first{}, last{};
  std::size_t depth{};
  BoardingRoutePortUnloadCondition condition{}, predicate_condition{};
  BoardingRouteFootPhaseCondition phase_condition{};
  std::optional<std::size_t> side, pair, partition;
  BoardingPlantedLegScalarBounds limiting_bound;
};
// Endpoint evidence refers to canonical u1; reverse requests reload that foot.
struct BoardingRoutePortUnloadDiagnostic {
  std::unique_ptr<BoardingSourceEndpointLoadDiagnostic> initial;
  std::uint32_t unload_version{kBoardingRoutePortUnloadVersion};
  BoardingRouteFootPhaseRequest controls;
  std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount> parts;
  double requested_first{}, requested_last{}, reporting_elapsed_seconds{};
  bool reverse{}, arithmetic_supported{}, kinematics_complete{},
      timing_complete{}, self_complete{}, nonnegative_reactions{},
      nominal_vertical_equilibrium_complete{}, finite_pressure_complete{},
      support_complete{}, endpoint_zero_port_reaction{}, complete{};
  std::size_t examined_nodes{}, maximum_depth{}, output_capacity_bytes{};
  // Actual successful guard work owned by the single initial child, not new
  // work.
  std::size_t owned_initial_quad_guards{}, owned_initial_quad_edges{},
      owned_initial_convexity_signs{}, owned_initial_triangle_windings{},
      owned_initial_diagonal_incidence_guards{};
  BoardingRoutePortUnloadCounters work;
  std::vector<BoardingRoutePortUnloadCell> cells;
  std::optional<BoardingRoutePortUnloadRefusal> first_refusal;
  static constexpr bool world_qualified{false}, material_qualified{false},
      source_surface_sweep_qualified{false}, route_qualified{false},
      actor_qualified{false}, seat_qualified{false}, save_qualified{false},
      first_flight_qualified{false}, free_foot_swing_qualified{false},
      dynamics_qualified{false}, strength_qualified{false},
      friction_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_route_port_unload(
    const OriginBoardingBootSupport&, double first = 0, double last = 1)
    -> std::expected<BoardingRoutePortUnloadDiagnostic, std::string>;
} // namespace apsis_drift
