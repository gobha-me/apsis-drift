#pragma once
#include "apsis_drift/origin_boarding_intermediate_endpoint03.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace apsis_drift {
inline constexpr std::uint32_t kBoardingKneeCompatibilityDiagnostic01Version{1};
inline constexpr std::size_t
    kBoardingKneeCompatibilityDiagnostic01MaximumExpectedBytes{1920},
    kBoardingKneeCompatibilityDiagnostic01MaximumScratchBytes{49152},
    kBoardingKneeCompatibilityDiagnostic01MaximumPrefixBytes{4096},
    kBoardingKneeCompatibilityDiagnostic01MaximumOptimisticBytes{2048},
    kBoardingKneeCompatibilityDiagnostic01MaximumOutputBytes{
        16 * std::size_t{1024} * 1024};

enum class BoardingKneeCompatibilityDiagnostic01State : std::uint8_t {
  not_run = 0,
  source_refused,
  prefix_refused,
  evidence_complete,
  capacity,
  unsupported,
  identity,
  suffix_domain_unavailable
};
enum class BoardingKneeCompatibilityDiagnostic01PrefixState : std::uint8_t {
  not_run = 0,
  cuts_available,
  cuts_collapsed
};
enum class BoardingKneeCompatibilityDiagnostic01Classification : std::uint8_t {
  not_run = 0,
  excluded,
  necessary_compatible,
  inconclusive
};
enum class BoardingKneeCompatibilityDiagnostic01Stop : std::uint8_t {
  none = 0,
  invalid_limits,
  invalid_binding,
  output_capacity,
  source_capacity,
  source_identity,
  prefix_domain,
  guard_capacity,
  operation_capacity,
  unsupported_arithmetic,
  suffix_domain,
  source_refusal
};
// Original slice ordinals describe the fresh literal prefix. They do not
// admit a historical report or provide an original construction capability.
enum class BoardingKneeCompatibilityDiagnostic01Condition : std::uint8_t {
  none = 0,
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
using BoardingKneeCompatibilityDiagnostic01SourceCondition =
    BoardingIntermediateEndpoint03Condition;
using BoardingKneeCompatibilityDiagnostic01Stage =
    BoardingIntermediateEndpoint03SelfStage;

struct BoardingKneeCompatibilityDiagnostic01PrefixEvidence {
  std::array<std::uint64_t, 4> operation_attempted{}, operation_written{};
  std::uint64_t guard_attempted{}, guard_written{};
  BoardingFootSiteScalarBounds limiting_bound;
  double base_lo{}, base_hi{};
  std::array<double, 2> factor_lower{};
  double current_cap_PORT{}, p_PORT{}, q_PORT{};
  BoardingFootSiteScalarBounds W_PORT, threshold, Lsum;
  double L1{}, L2{};
  std::uint16_t operation{65535};
  std::uint8_t guard{255}, side{255};
  BoardingKneeCompatibilityDiagnostic01Condition condition{};
  BoardingKneeCompatibilityDiagnostic01PrefixState prefix_state{};
  std::uint8_t preflight_guards{};
  bool arithmetic_supported{};
  std::uint16_t scalar_ready{}, zero_mask{};
};
struct BoardingKneeCompatibilityDiagnostic01OptimisticEvidence {
  BoardingFootSiteScalarBounds best_cap;
  std::array<BoardingFootSiteScalarBounds, 3> scalar_current, scalar_best;
  std::array<BoardingFootSiteScalarBounds, 4> common_current, common_best;
  std::array<BoardingKneeCompatibilityDiagnostic01Classification, 4>
      classification{};
  std::uint8_t classifier_ready{};
  BoardingKneeCompatibilityDiagnostic01Condition domain_condition{};
  bool complete{};
};
struct BoardingKneeCompatibilityDiagnostic01Refusal {
  BoardingKneeCompatibilityDiagnostic01SourceCondition condition{},
      predicate_condition{};
  BoardingFootSiteScalarBounds limiting_bound;
  BoardingRouteFootPhaseRefusal phase;
  std::optional<std::size_t> side, edge, axis;
  std::optional<LowerCockpitTriangleKey> source_key;
  std::string_view source_name;
  std::uint16_t self_pair{65535}, operation{65535};
  std::uint8_t self_region{255}, self_axis{255}, self_sign{255};
  BoardingKneeCompatibilityDiagnostic01Stage self_stage{};
  bool source_edge{};
};
struct BoardingKneeCompatibilityDiagnostic01Counters {
  std::uint64_t source_guards{}, construction_operations{},
      construction_guards{};
};
struct BoardingKneeCompatibilityDiagnostic01Diagnostic {
  OriginBoardingIntermediatePauseSupport source;
  std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount> parts;
  BoardingKneeCompatibilityDiagnostic01Counters work;
  BoardingKneeCompatibilityDiagnostic01PrefixEvidence prefix;
  BoardingKneeCompatibilityDiagnostic01OptimisticEvidence optimistic;
  BoardingKneeCompatibilityDiagnostic01Refusal first_refusal;
  std::uint64_t source_evaluated{};
  std::size_t output_capacity_bytes{};
  std::uint32_t version{kBoardingKneeCompatibilityDiagnostic01Version};
  BoardingKneeCompatibilityDiagnostic01State state{};
  BoardingKneeCompatibilityDiagnostic01Stop stop_condition{};
  bool has_refusal{}, source_enrolled{}, arithmetic_supported{}, complete{};
  explicit BoardingKneeCompatibilityDiagnostic01Diagnostic(
      const OriginBoardingIntermediatePauseSupport& provider)
      : source(provider) {}
  static constexpr bool world_qualified{false}, material_qualified{false},
      halo_qualified{false}, route_qualified{false}, seat_qualified{false},
      actor_qualified{false}, save_qualified{false}, dynamics_qualified{false},
      strength_qualified{false}, friction_qualified{false},
      first_flight_qualified{false};
};
using BoardingKneeCompatibilityDiagnostic01Expected =
    std::expected<BoardingKneeCompatibilityDiagnostic01Diagnostic, std::string>;
[[nodiscard]] auto assess_origin_boarding_knee_compatibility_diagnostic01(
    const OriginBoardingIntermediatePauseSupport&)
    -> BoardingKneeCompatibilityDiagnostic01Expected;
[[nodiscard]] constexpr auto
boarding_knee_compatibility_diagnostic01_required_output_bytes()
    -> std::size_t {
  return 2 * sizeof(BoardingKneeCompatibilityDiagnostic01Expected);
}
} // namespace apsis_drift
