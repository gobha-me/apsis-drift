#pragma once
#include "apsis_drift/origin_boarding_intermediate_endpoint03.hpp"
#include <cstdint>
#include <expected>
#include <string>
#include <vector>
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingIntermediateSectorEvidence02Version{2};
inline constexpr std::size_t
    kBoardingIntermediateSectorEvidence02MaximumExpectedBytes{1024},
    kBoardingIntermediateSectorEvidence02MaximumScratchBytes{49152},
    kBoardingIntermediateSectorEvidence02MaximumOutputBytes{16777216};
enum class BoardingIntermediateSectorEvidence02State : std::uint8_t {
  not_run = 0,
  unresolved = 1,
  capacity = 2,
  unsupported = 3
};
enum class BoardingIntermediateSectorEvidence02OriginalResult : std::uint8_t {
  not_run = 0,
  accepted = 1,
  unresolved = 2,
  capacity = 3,
  unsupported = 4
};
enum class BoardingIntermediateSectorEvidence02Kind : std::uint8_t {
  not_run = 0,
  joint_sector_refusal = 1,
  intentional_body_stop = 2
};
enum class BoardingIntermediateSectorEvidence02Sector : std::uint8_t {
  roll = 0,
  hip_lower = 1,
  hip_upper = 2,
  knee = 3,
  ankle_pitch = 4,
  axial = 5,
  positive_shin = 6,
  not_run = 255
};
enum class BoardingIntermediateSectorEvidence02Classification : std::uint8_t {
  not_run = 0,
  interval_inconclusive = 1,
  strict_necessary_violation = 2,
  // Original leg tuples only; no torso/body/whole-phase completion.
  original_sectors_complete = 3
};
enum class BoardingIntermediateSectorEvidence02Stage : std::uint8_t {
  not_run = 0,
  preflight = 1,
  output = 2,
  source = 3,
  construction_guard = 4,
  construction_operation = 5,
  phase = 6,
  capture = 7,
  complete = 8
};
enum class BoardingIntermediateSectorEvidence02Condition : std::uint8_t {
  none = 0,
  invalid_binding = 1,
  source_capacity = 2,
  source_identity = 3,
  construction_guard_capacity = 4,
  construction_operation_capacity = 5,
  slice_unavailable = 6,
  slice_identity = 7,
  capture_capacity = 8,
  capture_identity = 9,
  unsupported_arithmetic = 10,
  output_capacity = 11,
  original_prerequisite = 12,
  incomplete_capture = 13
};
struct BoardingIntermediateSectorEvidence02Refusal {
  BoardingIntermediateSectorEvidence02Condition condition{},
      predicate_condition{};
  BoardingIntermediateSectorEvidence02Stage stage{};
  std::uint8_t operation{255}, side{255};
  BoardingFootSiteScalarBounds limiting_bound;
  bool limiting_available{}, predicate_available{}, predicate_value{};
};
struct BoardingIntermediateSectorEvidence02Counters {
  std::uint64_t source_guards{}, construction_guards{},
      construction_operations{}, capture_guards{}, phase_calls{};
  BoardingRouteFootPhaseCounters phase;
  std::uint64_t preflight_guards{};
};
struct BoardingIntermediateSectorEvidence02Diagnostic {
  OriginBoardingIntermediatePauseSupport source;
  std::vector<BoardingRouteFootPhaseCell> cells;
  BoardingIntermediateEndpoint03SliceEvidence slice;
  BoardingIntermediateSectorEvidence02Counters work;
  BoardingIntermediateEndpoint03Refusal old_preparation_refusal;
  BoardingRouteFootPhaseRefusal original_reason;
  BoardingIntermediateSectorEvidence02Refusal first_companion_refusal,
      terminal_companion_refusal;
  std::array<BoardingPlantedLegScalarBounds, 6> sector_margins;
  BoardingFootSiteScalarBounds selected_limiting_bound;
  std::uint64_t source_evaluated{};
  std::size_t required_output_bytes{}, output_capacity_bytes{};
  double reporting_elapsed_seconds{};
  std::uint32_t version{kBoardingIntermediateSectorEvidence02Version},
      program_version{3};
  BoardingIntermediateSectorEvidence02State state{};
  BoardingIntermediateSectorEvidence02OriginalResult original_result{};
  BoardingIntermediateSectorEvidence02Kind kind{};
  BoardingIntermediateSectorEvidence02Sector sector{
      BoardingIntermediateSectorEvidence02Sector::not_run};
  BoardingIntermediateSectorEvidence02Classification classification{};
  BoardingIntermediateSectorEvidence02Stage last_stage{};
  std::uint8_t operation{255}, selected_side{255}, capture_cursor{255},
      capture_attempted{}, capture_written{}, margin_read{}, margin_written{};
  bool old_preparation_refusal_available{}, first_companion_refusal_available{},
      terminal_companion_refusal_available{}, selected_limiting_available{},
      last_predicate{}, last_predicate_available{}, classification_evaluated{},
      arithmetic_supported{}, evidence_complete{}, source_enrolled{};
  explicit BoardingIntermediateSectorEvidence02Diagnostic(
      const OriginBoardingIntermediatePauseSupport& p)
      : source(p) {}
  static constexpr bool source_qualified{false}, source_body_qualified{false},
      body_qualified{false}, contact_qualified{false}, support_qualified{false},
      self_qualified{false}, material_qualified{false}, halo_qualified{false},
      world_qualified{false}, route_qualified{false}, pan_qualified{false},
      seat_qualified{false}, actor_qualified{false}, save_qualified{false},
      dynamics_qualified{false}, strength_qualified{false},
      friction_qualified{false}, first_flight_qualified{false};
};
using BoardingIntermediateSectorEvidence02Expected =
    std::expected<BoardingIntermediateSectorEvidence02Diagnostic, std::string>;
[[nodiscard]] auto assess_origin_boarding_intermediate_sector_evidence02(
    const OriginBoardingIntermediatePauseSupport&)
    -> BoardingIntermediateSectorEvidence02Expected;
[[nodiscard]] constexpr auto
boarding_intermediate_sector_evidence02_required_output_bytes() -> std::size_t {
  return 2 * sizeof(BoardingIntermediateSectorEvidence02Expected) +
         sizeof(BoardingRouteFootPhaseCell);
}
} // namespace apsis_drift
