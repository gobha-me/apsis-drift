#pragma once
#include "apsis_drift/origin_boarding_intermediate_pause_support.hpp"
#include "apsis_drift/origin_boarding_route_foot_phase.hpp"
#include <variant>
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingRouteIntermediateHipDiagnostic01Version{
    1};
inline constexpr std::size_t
    kBoardingRouteIntermediateHipDiagnostic01MaximumScratchBytes{49152},
    kBoardingRouteIntermediateHipDiagnostic01MaximumOutputBytes{16777216},
    kBoardingRouteIntermediateHipDiagnostic01MaximumExpectedBytes{2048};
enum class BoardingRouteIntermediateHipDiagnostic01Case : std::uint8_t {
  point_0,
  point_quarter,
  point_half,
  point_three_quarters,
  point_1,
  terminal_phase1_interval
};
enum class BoardingRouteIntermediateHipDiagnostic01State : std::uint8_t {
  not_run,
  unresolved,
  excluded,
  unowned_interior,
  capacity,
  unsupported
};
enum class BoardingRouteIntermediateHipDiagnostic01Error : std::uint8_t {
  invalid_case,
  invalid_limits,
  output_preflight,
  allocation_failure,
  unsupported_environment
};
enum class BoardingRouteIntermediateHipDiagnostic01Condition : std::uint8_t {
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
struct BoardingRouteIntermediateHipDiagnostic01CaseMetadata {
  BoardingRouteIntermediateHipDiagnostic01Case case_id{};
  std::size_t phase_index{};
  double global_first{}, global_last{}, local_first{}, local_last{},
      seconds_per_parameter{};
};
using BoardingRouteIntermediateHipDiagnostic01Scalar =
    BoardingFootSiteScalarBounds;
struct BoardingRouteIntermediateHipDiagnostic01GeometryInput {
  std::array<BoardingRouteIntermediateHipDiagnostic01Scalar, 3> half{}, hip{},
      axis{}, segment{};
  BoardingRouteIntermediateHipDiagnostic01Scalar radius{}, owner_limit{};
};
struct BoardingRouteIntermediateHipDiagnostic01DualCandidate {
  std::array<double, 3> normal{};
  double multiplier{};
};
struct BoardingRouteIntermediateHipDiagnostic01PrimalCandidate {
  std::array<double, 3> point{};
  double segment_fraction{};
};
struct BoardingRouteIntermediateHipDiagnostic01DualMath {
  BoardingRouteIntermediateHipDiagnostic01DualCandidate candidate;
  BoardingRouteIntermediateHipDiagnostic01Scalar boxsum{}, hipdot{},
      segmentmax{}, normal_square{}, lower_bound{}, radius_squared{}, gap{};
  std::uint64_t primitive_evaluated{};
  std::uint8_t guard_evaluated{}, failed_operation{255};
  std::size_t guards{}, operations{};
  bool complete{}, arithmetic_supported{}, excluded{};
  BoardingRouteIntermediateHipDiagnostic01Condition condition{};
  static constexpr bool source_qualified{false};
};
struct BoardingRouteIntermediateHipDiagnostic01PrimalMath {
  BoardingRouteIntermediateHipDiagnostic01PrimalCandidate candidate;
  std::array<BoardingRouteIntermediateHipDiagnostic01Scalar, 6> box_gaps{};
  BoardingRouteIntermediateHipDiagnostic01Scalar axial_projection{},
      axial_gap{}, residual_squared{}, radius_squared{}, residual_gap{};
  std::uint64_t primitive_evaluated{};
  std::uint8_t guard_evaluated{}, failed_operation{255};
  std::size_t guards{}, operations{};
  bool complete{}, arithmetic_supported{}, unowned_interior{};
  BoardingRouteIntermediateHipDiagnostic01Condition condition{};
  static constexpr bool source_qualified{false};
};
using BoardingRouteIntermediateHipDiagnostic01Certificate =
    std::variant<std::monostate,
                 BoardingRouteIntermediateHipDiagnostic01DualMath,
                 BoardingRouteIntermediateHipDiagnostic01PrimalMath>;
struct BoardingRouteIntermediateHipDiagnostic01Work {
  std::size_t phase_calls{}, source_guards{}, current_guards{},
      chart_operations{}, generator_states{}, generator_operations{},
      dual_trials{}, primal_trials{}, verification_guards{},
      verification_operations{};
};
struct BoardingRouteIntermediateHipDiagnostic01Refusal {
  BoardingRouteIntermediateHipDiagnostic01Condition condition{};
  std::uint16_t state{65535};
  std::uint8_t delta{255}, alpha{255}, operation{255}, pivot{255}, row{255};
};
struct BoardingRouteIntermediateHipDiagnostic01Diagnostic {
  OriginBoardingIntermediatePauseSupport source;
  BoardingRouteIntermediateHipDiagnostic01CaseMetadata selection;
  std::vector<BoardingRouteFootPhaseCell> current_phase;
  BoardingRouteFootPhaseCounters phase_work;
  BoardingRouteFootPhaseRefusal phase_refusal;
  BoardingRouteIntermediateHipDiagnostic01Work work;
  BoardingRouteIntermediateHipDiagnostic01GeometryInput geometry;
  BoardingRouteIntermediateHipDiagnostic01Certificate winner, last_partial;
  BoardingRouteIntermediateHipDiagnostic01Refusal first_refusal;
  std::array<std::uint64_t, 2> source_evaluated{};
  std::array<std::uint64_t, 3> states_evaluated{}, states_skipped{};
  std::uint32_t current_evaluated{}, chart_evaluated{};
  BoardingRouteIntermediateHipDiagnostic01State state{
      BoardingRouteIntermediateHipDiagnostic01State::not_run};
  BoardingRouteIntermediateHipDiagnostic01Condition stop_condition{};
  std::size_t skipped_systems{}, skipped_proposals{}, output_capacity_bytes{};
  std::uint16_t last_state{65535};
  std::uint8_t last_delta{255}, last_alpha{255}, last_pivot{255}, last_row{255};
  bool source_complete{}, phase_available{}, current_complete{},
      chart_complete{}, certificate_assessed{}, excluded{}, unowned_interior{},
      arithmetic_supported{};
  std::uint32_t version{1};
  static constexpr bool route_qualified{false}, self_qualified{false},
      nominal_support_qualified{false}, material_qualified{false},
      world_qualified{false}, halo_qualified{false}, seat_qualified{false},
      actor_qualified{false}, save_qualified{false}, dynamics_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_route_intermediate_hip_diagnostic01(
    const OriginBoardingIntermediatePauseSupport&,
    BoardingRouteIntermediateHipDiagnostic01Case)
    -> std::expected<BoardingRouteIntermediateHipDiagnostic01Diagnostic,
                     BoardingRouteIntermediateHipDiagnostic01Error>;
} // namespace apsis_drift
