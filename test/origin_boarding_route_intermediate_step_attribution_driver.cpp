#include "origin_boarding_route_intermediate_step_attribution_driver.hpp"

#include <cmath>

namespace apsis_drift::test_detail {
namespace {
using State = detail::BoardingRouteFootPhaseCellResult;
using CaseState = IntermediateSectorAttributionState;
using Sector = IntermediateSectorAttributionSector;
using Classification = IntermediateSectorAttributionClassification;
using Report = IntermediateSectorAttributionReport;
using Error = IntermediateSectorAttributionError;
using Limits = IntermediateSectorAttributionLimits;
using Expected = std::expected<Report, Error>;
using Bounds = BoardingPlantedLegScalarBounds;
constexpr std::array<IntermediateSectorAttributionInterval,
                     kIntermediateSectorAttributionCases>
    manifest{{{10. / 1024, 11. / 1024, 5. / 128, 11. / 256},
              {46. / 4096, 47. / 4096, 46. / 1024, 47. / 1024},
              {10. / 1024, 10. / 1024, 5. / 128, 5. / 128},
              {21. / 2048, 21. / 2048, 21. / 512, 21. / 512},
              {11. / 1024, 11. / 1024, 11. / 256, 11. / 256},
              {93. / 8192, 93. / 8192, 93. / 2048, 93. / 2048},
              {1. / 32, 1. / 32, 1. / 8, 1. / 8},
              {1. / 16, 1. / 16, 1. / 4, 1. / 4}}};
constexpr std::array sectors{Sector::roll,        Sector::hip_lower,
                             Sector::hip_upper,   Sector::knee,
                             Sector::ankle_pitch, Sector::axial};
static_assert(sizeof(Expected) <=
              kIntermediateSectorAttributionMaximumOutputBytes);
// Keep the entire original graph proof, including its reusable phase cell,
// old metadata, helper/error phases and cache initialization. Add both compact
// result/return records, the copied optional recipe, lowered caps/reason,
// immutable manifest, one closure return and measured caller/printing reserve.
constexpr std::size_t live_bound =
    kBoardingRouteFootPhaseMaximumScratchBytes + 2 * sizeof(Expected) +
    sizeof(std::optional<BoardingRouteFootPhaseRequest>) + sizeof(Limits) +
    sizeof(detail::BoardingRouteFootPhaseLimits) +
    sizeof(BoardingRouteFootPhaseRefusal) + sizeof(manifest) +
    sizeof(IntermediateSectorAttributionClosure) + std::size_t{2048};
static_assert(live_bound <= kIntermediateSectorAttributionMaximumScratchBytes);

auto finite_ordered(Bounds b) -> bool {
  return std::isfinite(b.lower) && std::isfinite(b.upper) && b.lower <= b.upper;
}
auto same_bound(Bounds a, Bounds b) -> bool {
  return a.lower == b.lower && a.upper == b.upper;
}
auto valid_limits(const Limits& limits) -> bool {
  const Limits original;
  return limits.graphs <= original.graphs && limits.legs <= original.legs &&
         limits.bodies <= original.bodies &&
         limits.sectors <= original.sectors &&
         limits.timing <= original.timing &&
         limits.output_bytes <= original.output_bytes;
}
auto case_state(State state) -> CaseState {
  switch (state) {
    case State::accepted: return CaseState::accepted;
    case State::unresolved: return CaseState::unresolved;
    case State::unsupported: return CaseState::unsupported;
    case State::capacity: return CaseState::capacity;
  }
  return CaseState::unsupported;
}
auto retain_closure(const BoardingRouteFootPhaseLeg& leg,
                    IntermediateSectorAttributionClosure& out) -> void {
  out.distance_squared = leg.distance_squared;
  out.rho_squared = leg.rho_squared;
  out.gamma_squared = leg.gamma_squared;
  out.alpha = leg.alpha;
  out.gamma = leg.gamma;
  out.angular_derivatives = {leg.hip_pitch, leg.shin_pitch,    leg.knee_flex,
                             leg.hip_axial, leg.hip_abduction, leg.ankle_pitch,
                             leg.ankle_roll};
  out.written = true;
}
// Called only AFTER the unchanged kernel returns. Prefix/default flags are not
// evidence of reaching a stage; current state/reason and source write order
// are.
auto retain_case(State state, const BoardingRouteFootPhaseCell& cell,
                 const BoardingRouteFootPhaseRefusal& reason,
                 IntermediateSectorAttributionCase& record) -> void {
  record.state = case_state(state);
  record.reason = reason;
  if (state == State::accepted) {
    record.evidence_side = 0;
    record.attribution.evaluated_mask = 0x3f;
    record.sector_margins = cell.legs[0].sector_margins;
    retain_closure(cell.legs[0], record.closure);
    return;
  }
  if (state != State::unresolved ||
      reason.condition != BoardingRouteFootPhaseCondition::joint_sector ||
      !reason.side || *reason.side >= cell.legs.size())
    return;
  const auto& leg = cell.legs[*reason.side];
  record.evidence_side = reason.side;
  // Closure/derivative fields precede the sector loop, but body/COM and the
  // opposite leg need not exist. Never retain those unwritten/default fields.
  retain_closure(leg, record.closure);
  record.attribution = intermediate_sector_attribution_classify_math(
      state, reason, leg.sector_margins);
  if (!record.attribution.classified) return;
  for (std::size_t i = 0; i < record.sector_margins.size(); ++i)
    if ((record.attribution.evaluated_mask & (std::uint8_t{1} << i)) != 0)
      record.sector_margins[i] = leg.sector_margins[i];
  const bool point = record.interval.local_first == record.interval.local_last;
  record.point_violation =
      point && (record.attribution.sector == Sector::forward_shin
                    ? record.attribution.limiting_bound.upper <= 0
                    : record.attribution.limiting_bound.upper < 0);
}
} // namespace

auto intermediate_sector_attribution_manifest()
    -> const std::array<IntermediateSectorAttributionInterval,
                        kIntermediateSectorAttributionCases>& {
  return manifest;
}
auto intermediate_sector_attribution_classify_math(
    State state, const BoardingRouteFootPhaseRefusal& reason,
    const std::array<Bounds, 6>& margins) -> Classification {
  Classification result;
  if (state != State::unresolved ||
      reason.condition != BoardingRouteFootPhaseCondition::joint_sector ||
      !reason.side || *reason.side >= 2 ||
      !finite_ordered(reason.limiting_bound))
    return result;
  for (std::size_t i = 0; i < margins.size(); ++i) {
    if (!finite_ordered(margins[i])) return {};
    result.evaluated_mask |= static_cast<std::uint8_t>(std::uint8_t{1} << i);
    if (margins[i].lower < 0) {
      if (!same_bound(margins[i], reason.limiting_bound)) return {};
      result.sector = sectors[i];
      result.limiting_bound = reason.limiting_bound;
      result.classified = true;
      return result;
    }
  }
  // All six finite/nonnegative margins were reached and passed, so only the
  // later strict F2>0 guard can yield this fresh side-present leg refusal.
  if (reason.limiting_bound.lower <= 0) {
    result.sector = Sector::forward_shin;
    result.limiting_bound = reason.limiting_bound;
    result.classified = true;
    return result;
  }
  return {};
}

auto run_intermediate_sector_attribution(Limits limits) -> Expected {
  if (!valid_limits(limits)) return std::unexpected(Error::invalid_limits);
  if (limits.output_bytes < sizeof(Expected))
    return std::unexpected(Error::output_capacity);
  auto request = detail::boarding_route_intermediate_step_controls(0);
  if (!request) return std::unexpected(Error::invalid_recipe);
  Expected result(std::in_place);
  auto& report = *result;
  report.limits = limits;
  report.output_capacity_bytes = sizeof(Expected);
  for (std::size_t i = 0; i < report.cases.size(); ++i)
    report.cases[i].interval = manifest[i];
  detail::BoardingRouteFootPhaseLimits graph_limits;
  graph_limits.graphs = limits.graphs;
  graph_limits.legs = limits.legs;
  graph_limits.bodies = limits.bodies;
  graph_limits.sectors = limits.sectors;
  graph_limits.timing = limits.timing;
  BoardingRouteFootPhaseCell cell;
  for (auto& record : report.cases) {
    cell = {};
    BoardingRouteFootPhaseRefusal reason{};
    // Count the real producer invocation, including an immediate zero-cap or
    // unsupported-environment refusal. Work stays shared across the manifest.
    ++report.attempted_cases;
    const auto state = detail::boarding_route_foot_phase_cell(
        *request, record.interval.local_first, record.interval.local_last,
        false, graph_limits, report.work, cell, reason);
    retain_case(state, cell, reason, record);
    if (state == State::capacity || state == State::unsupported) return result;
  }
  report.complete_manifest = true;
  return result;
}
} // namespace apsis_drift::test_detail
