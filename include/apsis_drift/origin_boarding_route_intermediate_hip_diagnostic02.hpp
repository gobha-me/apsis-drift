#pragma once
#include "apsis_drift/origin_boarding_route_foot_phase.hpp"
#include "apsis_drift/origin_boarding_route_intermediate_hip_diagnostic01.hpp"
#include <variant>
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingRouteIntermediateHipDiagnostic02Version{
    2};
inline constexpr std::size_t
    kBoardingRouteIntermediateHipDiagnostic02MaximumScratchBytes{49152},
    kBoardingRouteIntermediateHipDiagnostic02MaximumCreatorBytes{8192},
    kBoardingRouteIntermediateHipDiagnostic02MaximumOutputBytes{16777216},
    kBoardingRouteIntermediateHipDiagnostic02MaximumExpectedBytes{1640};
enum class BoardingRouteIntermediateHipDiagnostic02Case : std::uint8_t {
  point_0,
  point_quarter,
  point_half,
  point_three_quarters,
  point_1,
  terminal_phase1_interval
};
enum class BoardingRouteIntermediateHipDiagnostic02State : std::uint8_t {
  not_run,
  unresolved,
  excluded,
  unowned_interior,
  capacity,
  unsupported
};
enum class BoardingRouteIntermediateHipDiagnostic02Error : std::uint8_t {
  invalid_case,
  invalid_limits,
  output_preflight,
  allocation_failure,
  unsupported_environment
};
enum class BoardingRouteIntermediateHipDiagnostic02Condition : std::uint8_t {
  none,
  invalid_binding,
  source_identity,
  source_guard_capacity,
  phase_capacity,
  current_identity,
  current_guard_capacity,
  phase_prerequisite,
  chart_identity,
  chart_capacity,
  generator_state_capacity,
  generator_operation_capacity,
  dual_capacity,
  primal_capacity,
  verification_guard_capacity,
  verification_operation_capacity,
  output_capacity,
  unsupported_arithmetic,
  invalid_proposal,
  singular_system,
  invalid_multiplier,
  no_certificate,
  contradictory_certificate
};
struct BoardingRouteIntermediateHipDiagnostic02CaseMetadata {
  BoardingRouteIntermediateHipDiagnostic02Case case_id{};
  std::size_t phase_index{};
  double global_first{}, global_last{}, local_first{}, local_last{},
      seconds_per_parameter{};
};
using BoardingRouteIntermediateHipDiagnostic02Scalar =
    BoardingRouteIntermediateHipDiagnostic01Scalar;
using BoardingRouteIntermediateHipDiagnostic02GeometryInput =
    BoardingRouteIntermediateHipDiagnostic01GeometryInput;
using BoardingRouteIntermediateHipDiagnostic02DualCandidate =
    BoardingRouteIntermediateHipDiagnostic01DualCandidate;
using BoardingRouteIntermediateHipDiagnostic02PrimalCandidate =
    BoardingRouteIntermediateHipDiagnostic01PrimalCandidate;
using BoardingRouteIntermediateHipDiagnostic02DualMath =
    BoardingRouteIntermediateHipDiagnostic01DualMath;
using BoardingRouteIntermediateHipDiagnostic02PrimalMath =
    BoardingRouteIntermediateHipDiagnostic01PrimalMath;
using BoardingRouteIntermediateHipDiagnostic02Certificate =
    BoardingRouteIntermediateHipDiagnostic01Certificate;
enum class BoardingRouteIntermediateHipDiagnostic02Family : std::uint8_t {
  not_run,
  anchor,
  dual,
  mixture
};
enum class BoardingRouteIntermediateHipDiagnostic02AnalyticCondition : std::
    uint8_t {
      not_run,
      none,
      no_anchor,
      invalid_tuple,
      invalid_number,
      insufficient_mixture,
      capacity,
      unsupported_environment,
      invalid_limits
    };
struct BoardingRouteIntermediateHipDiagnostic02Work {
  std::size_t phase_calls{}, source_guards{}, current_guards{},
      chart_operations{}, generator_states{}, generator_operations{},
      dual_trials{}, primal_trials{}, verification_guards{},
      verification_operations{};
};
struct BoardingRouteIntermediateHipDiagnostic02Refusal {
  BoardingRouteIntermediateHipDiagnostic02Condition condition{};
  std::uint16_t state{65535};
  BoardingRouteIntermediateHipDiagnostic02Family family{
      BoardingRouteIntermediateHipDiagnostic02Family::not_run};
  std::uint8_t operation{255}, pivot{255}, row{255};
};
struct BoardingRouteIntermediateHipDiagnostic02Diagnostic {
  OriginBoardingIntermediatePauseSupport source;
  BoardingRouteIntermediateHipDiagnostic02CaseMetadata selection;
  std::vector<BoardingRouteFootPhaseCell> current_phase;
  BoardingRouteFootPhaseCounters phase_work;
  BoardingRouteFootPhaseRefusal phase_refusal;
  BoardingRouteIntermediateHipDiagnostic02Work work;
  BoardingRouteIntermediateHipDiagnostic02GeometryInput geometry;
  BoardingRouteIntermediateHipDiagnostic02Certificate winner, last_partial;
  BoardingRouteIntermediateHipDiagnostic02Refusal first_refusal;
  std::array<std::uint64_t, 2> source_evaluated{};
  std::array<std::uint64_t, 3> states_attempted{}, systems_attempted{},
      systems_skipped{};
  std::uint32_t current_evaluated{}, chart_evaluated{};
  BoardingRouteIntermediateHipDiagnostic02State state{
      BoardingRouteIntermediateHipDiagnostic02State::not_run};
  BoardingRouteIntermediateHipDiagnostic02Condition stop_condition{};
  std::size_t skipped_systems{}, skipped_proposals{}, output_capacity_bytes{};
  std::uint16_t last_state{65535};
  std::uint8_t last_pivot{255}, last_row{255}, anchor_operation{255},
      mixture_operation{255};
  BoardingRouteIntermediateHipDiagnostic02Family last_family{
      BoardingRouteIntermediateHipDiagnostic02Family::not_run},
      winner_family{BoardingRouteIntermediateHipDiagnostic02Family::not_run};
  BoardingRouteIntermediateHipDiagnostic02AnalyticCondition anchor_condition{
      BoardingRouteIntermediateHipDiagnostic02AnalyticCondition::not_run},
      mixture_condition{
          BoardingRouteIntermediateHipDiagnostic02AnalyticCondition::not_run};
  std::uint32_t reporting_attempted{}, reporting_written{};
  std::uint64_t anchor_attempted{}, anchor_written{}, mixture_attempted{},
      mixture_written{};
  BoardingRouteIntermediateHipDiagnostic02PrimalCandidate anchor_candidate{};
  double last_epsilon{}, winner_epsilon{};
  std::size_t skipped_mixtures{};
  bool anchor_ready{}, anchor_skipped{};
  bool source_complete{}, phase_available{}, current_complete{},
      chart_complete{}, certificate_assessed{}, excluded{}, unowned_interior{},
      arithmetic_supported{};
  std::uint32_t version{2};
  static constexpr bool route_qualified{false}, self_qualified{false},
      nominal_support_qualified{false}, material_qualified{false},
      world_qualified{false}, halo_qualified{false}, seat_qualified{false},
      actor_qualified{false}, save_qualified{false}, dynamics_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_route_intermediate_hip_diagnostic02(
    const OriginBoardingIntermediatePauseSupport&,
    BoardingRouteIntermediateHipDiagnostic02Case)
    -> std::expected<BoardingRouteIntermediateHipDiagnostic02Diagnostic,
                     BoardingRouteIntermediateHipDiagnostic02Error>;
} // namespace apsis_drift
