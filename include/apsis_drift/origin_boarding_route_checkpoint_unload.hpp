#pragma once
#include "apsis_drift/origin_boarding_route_port_unload.hpp"
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingRouteCheckpointUnloadVersion{1};
inline constexpr std::size_t kBoardingRouteCheckpointUnloadMaximumCellBytes{
    12288},
    kBoardingRouteCheckpointUnloadMaximumOutputBytes{16 * std::size_t{1024} *
                                                     1024},
    kBoardingRouteCheckpointUnloadMaximumScratchBytes{48 * std::size_t{1024}};
// Outer endpoints are global. The embedded phase endpoints are LOCAL to
// phase_index.
struct BoardingRouteCheckpointUnloadCell {
  double global_first{}, global_last{};
  std::size_t phase_index{};
  BoardingRoutePortUnloadCell assessment;
};
struct BoardingRouteCheckpointUnloadRefusal {
  double first{}, last{};
  std::size_t depth{};
  std::optional<std::size_t> phase_index;
  BoardingRoutePortUnloadCondition condition{}, predicate_condition{};
  BoardingRouteFootPhaseCondition phase_condition{};
  std::optional<std::size_t> side, pair, partition;
  BoardingPlantedLegScalarBounds limiting_bound;
};
struct BoardingRouteCheckpointUnloadDiagnostic {
  std::unique_ptr<BoardingSourceEndpointLoadDiagnostic> initial;
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
  std::vector<BoardingRouteCheckpointUnloadCell> cells;
  std::optional<BoardingRouteCheckpointUnloadRefusal> first_refusal;
  static constexpr bool world_qualified{false}, material_qualified{false},
      source_surface_sweep_qualified{false}, route_qualified{false},
      actor_qualified{false}, seat_qualified{false}, save_qualified{false},
      first_flight_qualified{false}, free_foot_swing_qualified{false},
      dynamics_qualified{false}, strength_qualified{false},
      friction_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_route_checkpoint_unload(
    const OriginBoardingBootSupport&, double first = 0, double last = 1)
    -> std::expected<BoardingRouteCheckpointUnloadDiagnostic, std::string>;
} // namespace apsis_drift
