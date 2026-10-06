#pragma once

#include "apsis_drift/origin_boarding_route_foot_phase.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kBoardingRouteIntermediateStepVersion{1};
inline constexpr std::size_t kBoardingRouteIntermediateStepPhaseCount{4},
    kBoardingRouteIntermediateStepMaximumDepth{10},
    kBoardingRouteIntermediateStepMaximumNodes{2047},
    kBoardingRouteIntermediateStepMaximumLeaves{1024},
    kBoardingRouteIntermediateStepMaximumCellBytes{12288},
    kBoardingRouteIntermediateStepMaximumOutputBytes{16 * std::size_t{1024} *
                                                     1024},
    kBoardingRouteIntermediateStepMaximumScratchBytes{49152};
// This owns one unchanged phase01 numerical cell, not a source/body capability.
struct BoardingRouteIntermediateStepCell {
  double global_first{}, global_last{};
  std::size_t phase_index{};
  BoardingRouteFootPhaseCell phase;
};
struct BoardingRouteIntermediateStepRefusal {
  double first{}, last{}, local_first{}, local_last{};
  std::size_t depth{};
  std::optional<std::size_t> phase_index;
  BoardingRouteFootPhaseCondition condition{}, predicate_condition{};
  std::optional<std::size_t> side;
  BoardingPlantedLegScalarBounds limiting_bound;
};
struct BoardingRouteIntermediateStepDiagnostic {
  std::uint32_t step_version{kBoardingRouteIntermediateStepVersion};
  std::array<BoardingRouteFootPhaseRequest,
             kBoardingRouteIntermediateStepPhaseCount>
      controls;
  std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount> parts;
  double requested_first{}, requested_last{}, reporting_elapsed_seconds{};
  bool reverse{}, arithmetic_supported{}, nominal_links{},
      target_sole_identities{}, joint_sectors{}, derivative_domains{},
      timing_complete{}, complete{};
  std::array<bool, 3> join_expression_identity{}, qualified_joins{};
  std::size_t examined_nodes{}, mandatory_splits{}, maximum_depth{},
      output_capacity_bytes{};
  BoardingRouteFootPhaseCounters work;
  std::vector<BoardingRouteIntermediateStepCell> cells;
  std::optional<BoardingRouteIntermediateStepRefusal> first_refusal;
  static constexpr bool self_qualified{false}, source_qualified{false},
      material_qualified{false}, world_qualified{false},
      support_qualified{false}, load_qualified{false}, actor_qualified{false},
      route_qualified{false}, seat_qualified{false}, save_qualified{false},
      dynamics_qualified{false};
};
// Only the registered immutable numerical recipe is assessed. A positive
// kinematic cover cannot authorize physical contact, support or gameplay.
[[nodiscard]] auto assess_origin_boarding_route_intermediate_step(
    double first = 0, double last = 1)
    -> std::expected<BoardingRouteIntermediateStepDiagnostic, std::string>;
} // namespace apsis_drift
