#pragma once

#include "apsis_drift/origin_boarding_route_intermediate_step.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kBoardingRouteIntermediateStep02Version{2};
inline constexpr std::size_t kBoardingRouteIntermediateStep02PhaseCount{5},
    kBoardingRouteIntermediateStep02MaximumDepth{10},
    kBoardingRouteIntermediateStep02MaximumNodes{2047},
    kBoardingRouteIntermediateStep02MaximumLeaves{1024},
    kBoardingRouteIntermediateStep02MaximumCellBytes{12288},
    kBoardingRouteIntermediateStep02MaximumOutputBytes{16 * std::size_t{1024} *
                                                       1024},
    kBoardingRouteIntermediateStep02MaximumScratchBytes{49152};
// Retained phase records are unchanged; Step02 adds only the named cover.
using BoardingRouteIntermediateStep02Cell = BoardingRouteIntermediateStepCell;
using BoardingRouteIntermediateStep02Refusal =
    BoardingRouteIntermediateStepRefusal;
struct BoardingRouteIntermediateStep02Diagnostic {
  std::uint32_t step_version{kBoardingRouteIntermediateStep02Version};
  std::array<BoardingRouteFootPhaseRequest,
             kBoardingRouteIntermediateStep02PhaseCount>
      controls;
  std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount> parts;
  double requested_first{}, requested_last{}, reporting_elapsed_seconds{};
  bool reverse{}, arithmetic_supported{}, nominal_links{},
      target_sole_identities{}, joint_sectors{}, derivative_domains{},
      timing_complete{}, complete{};
  std::array<bool, 4> join_expression_identity{}, qualified_joins{};
  // Count every original compiler invocation, including guard refusals.
  std::size_t phase_calls{}, examined_nodes{}, mandatory_splits{},
      maximum_depth{}, output_capacity_bytes{};
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
[[nodiscard]] auto assess_origin_boarding_route_intermediate_step02(
    double first = 0, double last = 1)
    -> std::expected<BoardingRouteIntermediateStep02Diagnostic, std::string>;
} // namespace apsis_drift
