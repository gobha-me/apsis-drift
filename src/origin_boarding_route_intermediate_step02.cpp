#include "apsis_drift/origin_boarding_route_intermediate_step02.hpp"
#include "origin_boarding_route_intermediate_step02_internal.hpp"
#include "origin_boarding_route_intermediate_step_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace apsis_drift {
namespace {
using Diagnostic = BoardingRouteIntermediateStep02Diagnostic;
using Cell = BoardingRouteIntermediateStep02Cell;
using Refusal = BoardingRouteIntermediateStep02Refusal;
using Condition = BoardingRouteFootPhaseCondition;
using Limits = detail::BoardingRouteIntermediateStep02Limits;
using State = detail::BoardingRouteFootPhaseCellResult;
using Request = BoardingRouteFootPhaseRequest;
using Expected = std::expected<Diagnostic, std::string>;
struct Pending {
  double first{}, last{};
  std::size_t depth{};
};
constexpr std::size_t fixed_output_bytes = sizeof(Expected);
constexpr std::array<double, 4> joins{.125, .25, .5, .75};
static_assert(sizeof(Cell) <= kBoardingRouteIntermediateStep02MaximumCellBytes);
static_assert(sizeof(Pending) == 24);
static_assert(fixed_output_bytes +
                  sizeof(Cell) *
                      kBoardingRouteIntermediateStep02MaximumLeaves <=
              kBoardingRouteIntermediateStep02MaximumOutputBytes);
// The unchanged phase01 32KiB proof already includes its graph, output-cell
// staging, one request, helper/error frames and old caller metadata. Retain
// that whole bound, then conservatively charge both new fixed expected-result
// records, the new cover/limits/refusals and all extra cell/control storage.
// Accepted vector storage is output; no retained vector or graph is copied.
constexpr std::size_t intermediate_live_bound =
    kBoardingRouteFootPhaseMaximumScratchBytes + 2 * sizeof(Expected) +
    sizeof(Limits) + 11 * sizeof(Pending) + sizeof(Refusal) +
    sizeof(BoardingRouteFootPhaseRefusal) +
    (sizeof(Cell) - sizeof(BoardingRouteFootPhaseCell)) + std::size_t{2048};
static_assert(intermediate_live_bound <=
              kBoardingRouteIntermediateStep02MaximumScratchBytes);

auto valid_limits(const Limits& limits) -> bool {
  const detail::BoardingRouteFootPhaseLimits original;
  const auto& l = limits.phase;
  return l.depth <= original.depth && l.nodes <= original.nodes &&
         l.leaves <= original.leaves &&
         l.output_bytes <= original.output_bytes &&
         l.graphs <= original.graphs && l.legs <= original.legs &&
         l.bodies <= original.bodies && l.sectors <= original.sectors &&
         l.timing <= original.timing;
}
auto crossed_join(Pending p) -> std::optional<double> {
  if (p.first < .5 && p.last > .5) return .5;
  if (p.first < .25 && p.last > .25) return .25;
  if (p.first < .125 && p.last > .125) return .125;
  if (p.first < .75 && p.last > .75) return .75;
  return {};
}
auto refuse(Diagnostic& d, Pending p, Condition condition,
            std::optional<std::size_t> phase = {}) -> void {
  Refusal r;
  r.first = p.first;
  r.last = p.last;
  r.depth = p.depth;
  r.phase_index = phase;
  if (phase) {
    r.local_first =
        detail::boarding_route_intermediate_step02_local(*phase, p.first);
    r.local_last =
        detail::boarding_route_intermediate_step02_local(*phase, p.last);
  }
  r.condition = r.predicate_condition = condition;
  d.first_refusal = r;
}
auto complete_phase(const BoardingRouteFootPhaseCell& c) -> bool {
  return c.complete && c.arithmetic_supported && c.nominal_links &&
         c.target_sole_identities && c.joint_sectors && c.derivative_domains &&
         c.timing_complete;
}
auto covered(const Diagnostic& d, double first, double last) -> bool {
  if (d.cells.empty()) return false;
  auto cursor = first;
  for (const auto& c : d.cells) {
    const auto phase = detail::boarding_route_intermediate_step02_phase(
        c.global_first, c.global_last);
    if (c.global_first != cursor || !phase || c.phase_index != *phase ||
        c.phase.first != detail::boarding_route_intermediate_step02_local(
                             *phase, c.global_first) ||
        c.phase.last != detail::boarding_route_intermediate_step02_local(
                            *phase, c.global_last) ||
        !complete_phase(c.phase))
      return false;
    cursor = c.global_last;
  }
  if (cursor != last) return false;
  for (std::size_t j = 0; j < d.qualified_joins.size(); ++j) {
    const auto join = joins[j];
    if (first < join && last > join &&
        (!d.join_expression_identity[j] || !d.qualified_joins[j]))
      return false;
  }
  return true;
}
auto qualify_newest_join(Diagnostic& d) -> void {
  if (d.cells.size() < 2) return;
  const auto& left = d.cells[d.cells.size() - 2];
  const auto& right = d.cells.back();
  for (std::size_t j = 0; j < joins.size(); ++j) {
    const auto join = joins[j];
    if (d.join_expression_identity[j] &&
        std::min(d.requested_first, d.requested_last) < join &&
        std::max(d.requested_first, d.requested_last) > join &&
        left.phase_index == j && right.phase_index == j + 1 &&
        left.global_first < join && left.global_last == join &&
        right.global_first == join && right.global_last > join &&
        complete_phase(left.phase) && complete_phase(right.phase))
      d.qualified_joins[j] = true;
  }
}
// The five controls and part roster are initialized before graph math. Their
// constructor/return temporaries have ended when the noinline cover begins.
[[gnu::noinline]] auto initialize(Diagnostic& d, double first, double last)
    -> void {
  for (std::size_t i = 0; i < d.controls.size(); ++i)
    d.controls[i] = *detail::boarding_route_intermediate_step02_controls(i);
  d.parts = detail::boarding_route_foot_phase_parts();
  d.requested_first = first;
  d.requested_last = last;
  d.reverse = first > last;
  d.reporting_elapsed_seconds =
      std::abs(detail::boarding_route_intermediate_step02_clock(last) -
               detail::boarding_route_intermediate_step02_clock(first));
  for (std::size_t j = 0; j < d.join_expression_identity.size(); ++j)
    d.join_expression_identity[j] =
        detail::boarding_route_intermediate_step02_join_math(d.controls[j],
                                                             d.controls[j + 1]);
}
[[gnu::noinline]] auto run_cover(Diagnostic& d, Pending requested,
                                 const Limits& limits) -> void {
  std::array<Pending, kBoardingRouteIntermediateStep02MaximumDepth + 1>
      pending{};
  pending[0] = requested;
  std::size_t count{1};
  Cell cell;
  while (count != 0) {
    const auto p = pending[--count];
    if (d.examined_nodes >= limits.phase.nodes) {
      refuse(d, p, Condition::node_capacity);
      return;
    }
    if (d.cells.size() >= limits.phase.leaves) {
      refuse(d, p, Condition::leaf_capacity);
      return;
    }
    ++d.examined_nodes;
    d.maximum_depth = std::max(d.maximum_depth, p.depth);
    if (const auto join = crossed_join(p)) {
      if (p.depth >= limits.phase.depth) {
        refuse(d, p, Condition::depth_capacity);
        return;
      }
      if (count + 2 > pending.size()) {
        refuse(d, p, Condition::unsplittable_interval);
        return;
      }
      ++d.mandatory_splits;
      pending[count++] = {*join, p.last, p.depth + 1};
      pending[count++] = {p.first, *join, p.depth + 1};
      continue;
    }
    const auto phase =
        detail::boarding_route_intermediate_step02_phase(p.first, p.last);
    if (!phase) {
      refuse(d, p, Condition::incomplete_cover);
      return;
    }
    const auto local_first =
        detail::boarding_route_intermediate_step02_local(*phase, p.first);
    const auto local_last =
        detail::boarding_route_intermediate_step02_local(*phase, p.last);
    cell.global_first = p.first;
    cell.global_last = p.last;
    cell.phase_index = *phase;
    BoardingRouteFootPhaseRefusal reason;
    ++d.phase_calls;
    const auto state = detail::boarding_route_foot_phase_cell(
        d.controls[*phase], local_first, local_last, d.reverse, limits.phase,
        d.work, cell.phase, reason);
    if (state == State::accepted) {
      if (!complete_phase(cell.phase) || cell.phase.first != local_first ||
          cell.phase.last != local_last) {
        refuse(d, p, Condition::incomplete_cover, phase);
        return;
      }
      if (d.cells.size() == d.cells.capacity()) {
        refuse(d, p, Condition::output_capacity, phase);
        return;
      }
      d.cells.push_back(cell);
      qualify_newest_join(d);
      continue;
    }
    Refusal r;
    r.first = p.first;
    r.last = p.last;
    r.local_first = local_first;
    r.local_last = local_last;
    r.depth = p.depth;
    r.phase_index = phase;
    r.condition = r.predicate_condition = reason.condition;
    r.side = reason.side;
    r.limiting_bound = reason.limiting_bound;
    if (state == State::unsupported || state == State::capacity) {
      d.arithmetic_supported = state != State::unsupported;
      d.first_refusal = r;
      return;
    }
    if (p.first == p.last)
      r.condition = Condition::unsplittable_interval;
    else if (p.depth >= limits.phase.depth)
      r.condition = Condition::depth_capacity;
    else {
      const auto mid = p.first + (p.last - p.first) * .5;
      if (mid > p.first && mid < p.last && count + 2 <= pending.size()) {
        pending[count++] = {mid, p.last, p.depth + 1};
        pending[count++] = {p.first, mid, p.depth + 1};
        continue;
      }
      r.condition = Condition::unsplittable_interval;
    }
    d.first_refusal = r;
    return;
  }
  d.complete = covered(d, requested.first, requested.last);
  d.nominal_links = d.target_sole_identities = d.joint_sectors =
      d.derivative_domains = d.timing_complete = d.complete;
  if (!d.complete) refuse(d, requested, Condition::incomplete_cover);
}
} // namespace

auto detail::boarding_route_intermediate_step02_controls(std::size_t phase)
    -> std::optional<Request> {
  if (phase >= kBoardingRouteIntermediateStep02PhaseCount) return {};
  // Reuse all original term lists and physical durations verbatim. P is U
  // with only its Z term list replaced by the original fixed starboard list.
  auto original =
      boarding_route_intermediate_step_controls(phase <= 1 ? 0 : phase - 1);
  if (!original) return {};
  auto& r = *original;
  if (phase <= 1) {
    auto P = r.feet[0].sole[0];
    P.coordinates[2] = r.feet[1].sole[0].coordinates[2];
    if (phase == 0) {
      r.root[1] = r.root[0];
      r.root_yaw_half[1] = r.root_yaw_half[0];
      r.torso_lean_half[1] = r.torso_lean_half[0];
      r.port_reaction_fraction[1] = r.port_reaction_fraction[0];
      r.feet[0].sole[1] = P;
      r.feet[0].swing_height_metres = 0;
    } else {
      r.feet[0].sole[0] = P;
    }
  }
  return original;
}
auto detail::boarding_route_intermediate_step02_clock(double global) -> double {
  if (!std::isfinite(global) || global < 0 || global > 1)
    return std::numeric_limits<double>::quiet_NaN();
  if (global <= .25) return 96 * global;
  if (global <= .5) return 24 + 48 * (global - .25);
  if (global <= .75) return 36 + 48 * (global - .5);
  return 48 + 8 * (global - .75);
}
auto detail::boarding_route_intermediate_step02_local(std::size_t phase,
                                                      double global) -> double {
  if (phase >= kBoardingRouteIntermediateStep02PhaseCount ||
      !std::isfinite(global) || global < 0 || global > 1)
    return std::numeric_limits<double>::quiet_NaN();
  if (phase == 0) return 8 * global;
  if (phase == 1) return 8 * global - 1;
  return 4 * global - static_cast<double>(phase - 1);
}
auto detail::boarding_route_intermediate_step02_phase(double first, double last)
    -> std::optional<std::size_t> {
  if (!std::isfinite(first) || !std::isfinite(last) || first < 0 || last > 1 ||
      first > last || crossed_join({first, last, 0}))
    return {};
  // Using the closed last bound gives the preceding-phase tie for points,
  // while every positive-width interval starting at a join advances normally.
  if (last <= .125) return 0;
  if (last <= .25) return 1;
  if (last <= .5) return 2;
  if (last <= .75) return 3;
  return 4;
}
auto detail::boarding_route_intermediate_step02_join_math(const Request& a,
                                                          const Request& b)
    -> bool {
  return boarding_route_intermediate_step_join_math(a, b);
}
auto detail::boarding_route_intermediate_step02_bounded(double first,
                                                        double last,
                                                        Limits limits)
    -> Expected {
  if (!std::isfinite(first) || !std::isfinite(last) || first < 0 || first > 1 ||
      last < 0 || last > 1 || !valid_limits(limits))
    return std::unexpected(
        "Intermediate step02 needs finite [0,1] endpoints and lowered caps");
  Expected result(std::in_place);
  auto& d = *result;
  initialize(d, first, last);
  const Pending requested{std::min(first, last), std::max(first, last), 0};
  // Count fixed output even for a refusal, before any vector or graph work.
  d.output_capacity_bytes = fixed_output_bytes;
  if (limits.phase.output_bytes < fixed_output_bytes) {
    refuse(d, requested, Condition::output_capacity);
    return result;
  }
  if (!boarding_route_foot_phase_environment()) {
    refuse(d, requested, Condition::unsupported_arithmetic);
    return result;
  }
  d.arithmetic_supported = true;
  if (!std::all_of(d.join_expression_identity.begin(),
                   d.join_expression_identity.end(),
                   [](bool v) { return v; })) {
    refuse(d, requested, Condition::incomplete_cover);
    return result;
  }
  const auto capacity = first == last
                            ? std::min(limits.phase.leaves, std::size_t{1})
                            : limits.phase.leaves;
  if (capacity >
      (limits.phase.output_bytes - fixed_output_bytes) / sizeof(Cell)) {
    refuse(d, requested, Condition::output_capacity);
    return result;
  }
  try {
    d.cells.reserve(capacity);
  } catch (const std::bad_alloc&) {
    return std::unexpected("Intermediate step02 cell allocation failed");
  }
  // Account actual capacity even if a library reserve exceeded its request.
  if (d.cells.capacity() >
      (std::numeric_limits<std::size_t>::max() - fixed_output_bytes) /
          sizeof(Cell)) {
    d.output_capacity_bytes = std::numeric_limits<std::size_t>::max();
    refuse(d, requested, Condition::output_capacity);
    return result;
  }
  d.output_capacity_bytes =
      fixed_output_bytes + d.cells.capacity() * sizeof(Cell);
  if (d.cells.capacity() >
      (limits.phase.output_bytes - fixed_output_bytes) / sizeof(Cell)) {
    refuse(d, requested, Condition::output_capacity);
    return result;
  }
  run_cover(d, requested, limits);
  return result;
}
auto assess_origin_boarding_route_intermediate_step02(double first, double last)
    -> Expected {
  return detail::boarding_route_intermediate_step02_bounded(first, last);
}
} // namespace apsis_drift
