#include "apsis_drift/origin_boarding_route_foot_phase.hpp"
#include "origin_boarding_route_foot_phase_internal.hpp"

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace apsis_drift {
namespace {
using Diagnostic = BoardingRouteFootPhaseDiagnostic;
using Cell = BoardingRouteFootPhaseCell;
using Condition = BoardingRouteFootPhaseCondition;
using Refusal = BoardingRouteFootPhaseRefusal;
using Limits = detail::BoardingRouteFootPhaseLimits;
using State = detail::BoardingRouteFootPhaseCellResult;
struct Pending {
  double first{}, last{};
  std::size_t depth{};
};
static_assert(sizeof(Cell) <= kBoardingRouteFootPhaseMaximumCellBytes);
static_assert(sizeof(BoardingRouteFootPhaseRequest) <= 2048);
constexpr std::size_t fixed_output_bytes =
    sizeof(std::expected<Diagnostic, std::string>);
static_assert(fixed_output_bytes +
                  sizeof(Cell) * kBoardingRouteFootPhaseMaximumLeaves <=
              kBoardingRouteFootPhaseMaximumOutputBytes);
static_assert(sizeof(Pending) == 24);
auto valid_limits(const Limits& l) -> bool {
  const Limits original;
  return l.depth <= original.depth && l.nodes <= original.nodes &&
         l.leaves <= original.leaves &&
         l.output_bytes <= original.output_bytes &&
         l.graphs <= original.graphs && l.legs <= original.legs &&
         l.bodies <= original.bodies && l.sectors <= original.sectors &&
         l.timing <= original.timing;
}
auto refusal(Diagnostic& d, Pending p, Condition c) -> void {
  d.first_refusal = Refusal{.first = p.first,
                            .last = p.last,
                            .depth = p.depth,
                            .condition = c,
                            .predicate_condition = c,
                            .side = {},
                            .limiting_bound = {}};
}
auto covered(const Diagnostic& d, double first, double last) -> bool {
  if (d.cells.empty()) return false;
  auto cursor = first;
  for (const auto& c : d.cells) {
    if (c.first != cursor || c.first > c.last || !c.complete ||
        !c.arithmetic_supported || !c.nominal_links ||
        !c.target_sole_identities || !c.joint_sectors ||
        !c.derivative_domains || !c.timing_complete)
      return false;
    cursor = c.last;
  }
  return cursor == last;
}
} // namespace

auto detail::boarding_route_foot_phase_bounded(
    const BoardingRouteFootPhaseRequest& request, double first, double last,
    Limits limits) -> std::expected<Diagnostic, std::string> {
  if (!std::isfinite(first) || !std::isfinite(last) || first < 0 || first > 1 ||
      last < 0 || last > 1 || !valid_limits(limits))
    return std::unexpected(
        "Route foot phase needs finite [0,1] endpoints and lowered caps");
  if (const auto valid = boarding_route_foot_phase_request_valid(request);
      !valid)
    return std::unexpected(valid.error());
  Diagnostic result;
  result.request = request;
  result.parts = boarding_route_foot_phase_parts();
  result.requested_first = first;
  result.requested_last = last;
  result.reverse = first > last;
  result.reporting_elapsed_seconds =
      request.seconds_per_parameter * std::abs(last - first);
  const Pending requested{std::min(first, last), std::max(first, last), 0};
  if (!boarding_route_foot_phase_environment()) {
    refusal(result, requested, Condition::unsupported_arithmetic);
    return result;
  }
  result.arithmetic_supported = true;
  const auto capacity =
      first == last ? std::min(limits.leaves, std::size_t{1}) : limits.leaves;
  if (limits.output_bytes < fixed_output_bytes ||
      capacity > (limits.output_bytes - fixed_output_bytes) / sizeof(Cell)) {
    refusal(result, requested, Condition::output_capacity);
    return result;
  }
  try {
    result.cells.reserve(capacity);
  } catch (const std::bad_alloc&) {
    return std::unexpected("Route foot phase allocation failed");
  }
  if (result.cells.capacity() >
      (limits.output_bytes - fixed_output_bytes) / sizeof(Cell)) {
    refusal(result, requested, Condition::output_capacity);
    return result;
  }
  result.output_capacity_bytes =
      fixed_output_bytes + result.cells.capacity() * sizeof(Cell);
  std::array<Pending, kBoardingRouteFootPhaseMaximumDepth + 1> pending{};
  pending[0] = requested;
  std::size_t count{1};
  Cell cell{};
  while (count != 0) {
    const auto p = pending[--count];
    if (result.examined_nodes >= limits.nodes) {
      refusal(result, p, Condition::node_capacity);
      return result;
    }
    if (result.cells.size() >= limits.leaves) {
      refusal(result, p, Condition::leaf_capacity);
      return result;
    }
    ++result.examined_nodes;
    result.maximum_depth = std::max(result.maximum_depth, p.depth);
    Refusal reason{};
    const auto state =
        boarding_route_foot_phase_cell(request, p.first, p.last, result.reverse,
                                       limits, result.work, cell, reason);
    reason.first = p.first;
    reason.last = p.last;
    reason.depth = p.depth;
    reason.predicate_condition = reason.condition;
    if (state == State::accepted) {
      if (!cell.complete || cell.first != p.first || cell.last != p.last) {
        refusal(result, p, Condition::incomplete_cover);
        return result;
      }
      if (result.cells.size() == result.cells.capacity()) {
        refusal(result, p, Condition::output_capacity);
        return result;
      }
      result.cells.push_back(cell);
      continue;
    }
    if (state == State::unsupported || state == State::capacity) {
      result.arithmetic_supported = state != State::unsupported;
      result.first_refusal = reason;
      return result;
    }
    if (p.first == p.last)
      reason.condition = Condition::unsplittable_interval;
    else if (p.depth >= limits.depth)
      reason.condition = Condition::depth_capacity;
    else {
      const auto mid = p.first + (p.last - p.first) * .5;
      if (mid > p.first && mid < p.last && count + 2 <= pending.size()) {
        pending[count++] = {mid, p.last, p.depth + 1};
        pending[count++] = {p.first, mid, p.depth + 1};
        continue;
      }
      reason.condition = Condition::unsplittable_interval;
    }
    result.first_refusal = reason;
    return result;
  }
  result.complete = covered(result, requested.first, requested.last);
  result.nominal_links = result.target_sole_identities = result.joint_sectors =
      result.derivative_domains = result.timing_complete = result.complete;
  if (!result.complete) refusal(result, requested, Condition::incomplete_cover);
  return result;
}
auto assess_origin_boarding_route_foot_phase(
    const BoardingRouteFootPhaseRequest& request, double first, double last)
    -> std::expected<Diagnostic, std::string> {
  return detail::boarding_route_foot_phase_bounded(request, first, last);
}
} // namespace apsis_drift
