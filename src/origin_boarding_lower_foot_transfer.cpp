#include "apsis_drift/origin_boarding_lower_foot_transfer.hpp"

#include "origin_boarding_lower_foot_transfer_internal.hpp"
#include "origin_boarding_source_endpoint_load_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <new>
#include <utility>

namespace apsis_drift {
namespace {
using Diagnostic = BoardingLowerFootTransferDiagnostic;
using Condition = BoardingLowerFootTransferCondition;
using Cell = BoardingLowerFootTransferCell;
using Refusal = BoardingLowerFootTransferRefusal;
using Limits = detail::BoardingLowerFootTransferLimits;
using CellResult = detail::BoardingLowerFootTransferCellResult;
struct Pending {
  double first{}, last{};
  std::size_t depth{};
};
constexpr auto fixed_output_bytes =
    sizeof(Diagnostic) - sizeof(BoardingSourceEndpointLoadDiagnostic);
static_assert(sizeof(Cell) <= kBoardingLowerFootTransferMaximumCellBytes);
static_assert(fixed_output_bytes +
                  sizeof(Cell) * kBoardingLowerFootTransferMaximumLeaves <=
              kBoardingLowerFootTransferMaximumOutputBytes);
static_assert(sizeof(Pending) == 24);

auto valid_limits(const Limits& limits) -> bool {
  return limits.source_partitions <= kBoardingBootSourcePartitionCount &&
         limits.body_records <= kBoardingSourceEndpointMaximumRecords &&
         limits.initial_self_pairs <= kBoardingBodyPairCount &&
         limits.initial_self_axes <= kBoardingSourceEndpointSelfMaximumAxes &&
         limits.initial_pressure_partitions <=
             kBoardingBootSourcePartitionCount &&
         limits.depth <= kBoardingLowerFootTransferMaximumDepth &&
         limits.nodes <= kBoardingLowerFootTransferMaximumNodes &&
         limits.leaves <= kBoardingLowerFootTransferMaximumLeaves &&
         limits.self_pairs <= kBoardingLowerFootTransferMaximumPairs &&
         limits.proposed_axes <= kBoardingLowerFootTransferMaximumAxes &&
         limits.signed_trials <=
             kBoardingLowerFootTransferMaximumSignedTrials &&
         limits.pressure_candidates <=
             kBoardingLowerFootTransferMaximumPressureCandidates &&
         limits.disk_edges <= kBoardingLowerFootTransferMaximumEdges;
}
auto initial_result(const OriginBoardingBootSupport& provider,
                    const Limits& limits)
    -> std::expected<Diagnostic, std::string> {
  auto child = detail::boarding_source_endpoint_load_bounded(
      provider, limits.source_partitions, limits.body_records,
      limits.initial_self_pairs, limits.initial_self_axes,
      limits.initial_pressure_partitions);
  if (!child) return std::unexpected(child.error());
  return Diagnostic{.initial = std::move(*child),
                    .parts = {},
                    .work = {},
                    .cells = {},
                    .first_refusal = {}};
}
auto retain_refusal(Diagnostic& result, const Pending& pending,
                    Condition condition) -> void {
  result.first_refusal = Refusal{};
  result.first_refusal->first = pending.first;
  result.first_refusal->last = pending.last;
  result.first_refusal->depth = pending.depth;
  result.first_refusal->condition = condition;
}
auto complete_cover(const Diagnostic& result, double first, double last)
    -> bool {
  if (result.cells.empty()) return false;
  auto previous = first;
  for (const auto& cell : result.cells) {
    if (!std::isfinite(cell.first) || !std::isfinite(cell.last) ||
        cell.first != previous || cell.first > cell.last || !cell.complete ||
        !cell.arithmetic_supported || !cell.kinematics_complete ||
        !cell.timing_complete || !cell.self_complete ||
        !cell.positive_reactions ||
        !cell.nominal_vertical_equilibrium_complete ||
        !cell.finite_pressure_complete)
      return false;
    previous = cell.last;
  }
  return previous == last;
}
} // namespace

auto detail::boarding_lower_foot_transfer_bounded(
    const OriginBoardingBootSupport& provider, double first, double last,
    Limits limits) -> std::expected<Diagnostic, std::string> {
  if (!std::isfinite(first) || !std::isfinite(last) || first < 0 || first > 1 ||
      last < 0 || last > 1 || !valid_limits(limits))
    return std::unexpected("Transfer requires finite [0,1] endpoints and "
                           "lowered registered capacities");
  // The moved-from child return dies before any new graph work starts.
  auto prepared = initial_result(provider, limits);
  if (!prepared) return prepared;
  auto& result = *prepared;
  result.requested_first = first;
  result.requested_last = last;
  result.reverse = first > last;
  result.reporting_elapsed_seconds = 4 * std::abs(last - first);
  result.arithmetic_supported = result.initial.load.arithmetic_supported;
  const auto lower = std::min(first, last), upper = std::max(first, last);
  const Pending requested{lower, upper, 0};
  if (!result.initial.load.complete) {
    retain_refusal(result, requested, Condition::initial_prerequisite);
    return prepared;
  }
  result.parts = result.initial.self.endpoint.parts;
  auto pressure = prepare_boarding_lower_foot_transfer_pressure(result.initial);
  if (!pressure) {
    retain_refusal(result, requested, Condition::invalid_binding);
    return prepared;
  }
  result.checked_source_quads = result.initial.load.checked_quads;
  const auto capacity =
      first == last ? std::min(limits.leaves, std::size_t{1}) : limits.leaves;
  try {
    result.cells.reserve(capacity);
  } catch (const std::bad_alloc&) {
    return std::unexpected("Transfer output allocation failed");
  }
  // Bound the actual capacity, including fixed new metadata. No append may grow
  // it.
  if (result.cells.capacity() >
      (kBoardingLowerFootTransferMaximumOutputBytes - fixed_output_bytes) /
          sizeof(Cell)) {
    retain_refusal(result, requested, Condition::output_capacity);
    return prepared;
  }
  result.output_capacity_bytes =
      fixed_output_bytes + result.cells.capacity() * sizeof(Cell);
  std::array<Pending, kBoardingLowerFootTransferMaximumDepth + 1> pending{};
  pending[0] = requested;
  std::size_t count{1};
  // One uncommitted body record; the compound helper's staged live ledger
  // includes it.
  Cell cell{};
  while (count != 0) {
    const auto current = pending[--count];
    if (result.examined_nodes >= limits.nodes) {
      retain_refusal(result, current, Condition::node_capacity);
      return prepared;
    }
    if (result.cells.size() >= limits.leaves) {
      retain_refusal(result, current, Condition::leaf_capacity);
      return prepared;
    }
    Refusal refusal{};
    ++result.examined_nodes;
    result.maximum_depth = std::max(result.maximum_depth, current.depth);
    const auto state = boarding_lower_foot_transfer_cell(
        result.initial, *pressure, current.first, current.last, result.reverse,
        limits, result.work, cell, refusal);
    refusal.first = current.first;
    refusal.last = current.last;
    refusal.depth = current.depth;
    refusal.predicate_condition = refusal.condition;
    if (state == CellResult::accepted) {
      if (!cell.complete || cell.first != current.first ||
          cell.last != current.last) {
        retain_refusal(result, current, Condition::invalid_binding);
        return prepared;
      }
      if (result.cells.size() == result.cells.capacity()) {
        retain_refusal(result, current, Condition::output_capacity);
        return prepared;
      }
      result.cells.push_back(cell);
      continue;
    }
    if (state == CellResult::unsupported || state == CellResult::capacity) {
      result.arithmetic_supported =
          result.arithmetic_supported && state != CellResult::unsupported;
      result.first_refusal = refusal;
      return prepared;
    }
    if (current.first == current.last) {
      refusal.condition = Condition::unsplittable_interval;
      result.first_refusal = refusal;
      return prepared;
    }
    if (current.depth >= limits.depth) {
      refusal.condition = Condition::depth_capacity;
      result.first_refusal = refusal;
      return prepared;
    }
    const auto middle = current.first + (current.last - current.first) * .5;
    if (middle <= current.first || middle >= current.last ||
        count + 2 > pending.size()) {
      refusal.condition = Condition::unsplittable_interval;
      result.first_refusal = refusal;
      return prepared;
    }
    // LIFO right-then-left yields canonical ascending leaves and shared
    // endpoints.
    pending[count++] = {middle, current.last, current.depth + 1};
    pending[count++] = {current.first, middle, current.depth + 1};
  }
  if (!complete_cover(result, lower, upper)) {
    retain_refusal(result, requested, Condition::incomplete_cover);
    return prepared;
  }
  result.arithmetic_supported = result.kinematics_complete =
      result.timing_complete = result.self_complete =
          result.nominal_vertical_equilibrium_complete =
              result.finite_pressure_complete = result.complete = true;
  return prepared;
}
auto assess_origin_boarding_lower_foot_transfer(
    const OriginBoardingBootSupport& provider, double first, double last)
    -> std::expected<Diagnostic, std::string> {
  return detail::boarding_lower_foot_transfer_bounded(provider, first, last);
}
} // namespace apsis_drift
