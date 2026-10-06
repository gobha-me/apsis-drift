#pragma once

#include "origin_boarding_route_intermediate_step_internal.hpp"

namespace apsis_drift::test_detail {
inline constexpr std::uint32_t kIntermediateSectorAttributionVersion{1};
inline constexpr std::size_t kIntermediateSectorAttributionCases{8},
    kIntermediateSectorAttributionMaximumOutputBytes{8192},
    kIntermediateSectorAttributionMaximumScratchBytes{49152},
    kIntermediateSectorAttributionMaximumStreamBytes{16384};
struct IntermediateSectorAttributionInterval {
  double global_first{}, global_last{}, local_first{}, local_last{};
};
enum class IntermediateSectorAttributionState : std::uint8_t {
  not_run,
  accepted,
  unresolved,
  unsupported,
  capacity
};
enum class IntermediateSectorAttributionSector : std::uint8_t {
  none,
  roll,
  hip_lower,
  hip_upper,
  knee,
  ankle_pitch,
  axial,
  forward_shin
};
enum class IntermediateSectorAttributionError : std::uint8_t {
  invalid_limits,
  output_capacity,
  invalid_recipe
};
struct IntermediateSectorAttributionLimits {
  std::uint64_t graphs{8}, legs{16}, bodies{8}, sectors{24}, timing{48};
  std::size_t output_bytes{kIntermediateSectorAttributionMaximumOutputBytes};
};
struct IntermediateSectorAttributionClassification {
  IntermediateSectorAttributionSector sector{
      IntermediateSectorAttributionSector::none};
  std::uint8_t evaluated_mask{};
  BoardingPlantedLegScalarBounds limiting_bound;
  bool classified{};
  static constexpr bool source_qualified{false}, body_qualified{false},
      support_qualified{false}, route_qualified{false};
};
// Written before the genuine leg-sector loop, under its preceding supported
// branch guards. Angular entries are rates/coordinate second derivatives,
// NOT absolute angles: hip, shin, knee, axial, abduction, ankle pitch, roll.
struct IntermediateSectorAttributionClosure {
  BoardingPlantedLegScalarBounds distance_squared, rho_squared, gamma_squared,
      alpha, gamma;
  std::array<BoardingPlantedLegAngularDerivatives, 7> angular_derivatives;
  bool written{};
};
struct IntermediateSectorAttributionCase {
  IntermediateSectorAttributionInterval interval;
  IntermediateSectorAttributionState state{
      IntermediateSectorAttributionState::not_run};
  BoardingRouteFootPhaseRefusal reason;
  std::optional<std::size_t> evidence_side;
  IntermediateSectorAttributionClassification attribution;
  std::array<BoardingPlantedLegScalarBounds, 6> sector_margins;
  IntermediateSectorAttributionClosure closure;
  bool point_violation{};
};
struct IntermediateSectorAttributionReport {
  std::uint32_t attribution_version{kIntermediateSectorAttributionVersion};
  std::array<IntermediateSectorAttributionCase,
             kIntermediateSectorAttributionCases>
      cases;
  IntermediateSectorAttributionLimits limits;
  BoardingRouteFootPhaseCounters work;
  std::size_t attempted_cases{}, output_capacity_bytes{};
  // All eight ordinary accepted/unresolved results completed. A capacity or
  // unsupported hardstop keeps this false even if it occurs on the last case.
  bool complete_manifest{};
  static constexpr bool source_qualified{false}, support_qualified{false},
      load_qualified{false}, self_qualified{false}, material_qualified{false},
      world_qualified{false}, route_qualified{false}, actor_qualified{false},
      seat_qualified{false}, save_qualified{false}, dynamics_qualified{false};
};
[[nodiscard]] auto intermediate_sector_attribution_manifest()
    -> const std::array<IntermediateSectorAttributionInterval,
                        kIntermediateSectorAttributionCases>&;
// Arithmetic-only classifier fixture; it cannot authenticate caller data.
// The named test driver calls it only after its fresh unchanged cell replay.
[[nodiscard]] auto intermediate_sector_attribution_classify_math(
    detail::BoardingRouteFootPhaseCellResult,
    const BoardingRouteFootPhaseRefusal&,
    const std::array<BoardingPlantedLegScalarBounds, 6>&)
    -> IntermediateSectorAttributionClassification;
[[nodiscard]] auto run_intermediate_sector_attribution(
    IntermediateSectorAttributionLimits = {})
    -> std::expected<IntermediateSectorAttributionReport,
                     IntermediateSectorAttributionError>;
} // namespace apsis_drift::test_detail
