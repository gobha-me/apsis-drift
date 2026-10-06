#include "apsis_drift/origin_boarding_route_checkpoint_unload.hpp"
#include "origin_boarding_route_checkpoint_unload_internal.hpp"
#include "origin_boarding_route_checkpoint_unload_self02_internal.hpp"
#include <algorithm>
#include <cmath>
#include <new>
#include <utility>
namespace apsis_drift {
namespace {
using CheckDiagnostic = BoardingRouteCheckpointUnloadDiagnostic;
using CheckCell = BoardingRouteCheckpointUnloadCell;
using CheckReason = BoardingRouteCheckpointUnloadRefusal;
using CheckWhy = BoardingRoutePortUnloadCondition;
using CheckLimits = detail::BoardingRouteCheckpointUnloadLimits;
using CheckState = detail::BoardingRouteCheckpointUnloadCellResult;
struct CheckPending {
  double first{}, last{};
  std::size_t depth{};
};
constexpr std::size_t checkpoint_fixed_output =
    sizeof(std::expected<CheckDiagnostic, std::string>) +
    sizeof(BoardingSourceEndpointLoadDiagnostic);
static_assert(sizeof(CheckCell) <=
              kBoardingRouteCheckpointUnloadMaximumCellBytes);
static_assert(checkpoint_fixed_output + 1024 * sizeof(CheckCell) <=
              kBoardingRouteCheckpointUnloadMaximumOutputBytes);
auto checkpoint_valid_limits(const CheckLimits& c) -> bool {
  const CheckLimits o;
  return c.phase.depth <= o.phase.depth && c.phase.nodes <= o.phase.nodes &&
         c.phase.leaves <= o.phase.leaves &&
         c.phase.output_bytes <= o.phase.output_bytes &&
         c.phase.graphs <= o.phase.graphs && c.phase.legs <= o.phase.legs &&
         c.phase.bodies <= o.phase.bodies &&
         c.phase.sectors <= o.phase.sectors &&
         c.phase.timing <= o.phase.timing &&
         c.initial_source_partitions <= o.initial_source_partitions &&
         c.initial_body_records <= o.initial_body_records &&
         c.initial_self_pairs <= o.initial_self_pairs &&
         c.initial_self_axes <= o.initial_self_axes &&
         c.initial_pressure_partitions <= o.initial_pressure_partitions &&
         c.output_bytes <= o.output_bytes && c.pairs <= o.pairs &&
         c.axes <= o.axes && c.signed_trials <= o.signed_trials &&
         c.owners <= o.owners && c.candidates <= o.candidates &&
         c.edges <= o.edges;
}
auto checkpoint_phase_index(double first, double last) -> std::size_t {
  // A nonpoint ending at a join belongs to the left; an exact point to the
  // right.
  if (first < .25 && last <= .25) return 0;
  if (first < .5 && last <= .5) return 1;
  return 2;
}
auto checkpoint_local(std::size_t phase, double g) -> double {
  return phase == 0 ? 4 * g : phase == 1 ? 4 * g - 1 : 2 * g - 1;
}
auto checkpoint_mandatory_join(CheckPending p) -> std::optional<double> {
  if (p.first < .5 && p.last > .5) return .5;
  if (p.first < .25 && p.last > .25) return .25;
  return {};
}
template <class Diagnostic>
auto checkpoint_refuse(Diagnostic& d, CheckPending p, CheckWhy why,
                       std::optional<std::size_t> phase = {}) -> void {
  d.first_refusal = CheckReason{.first = p.first,
                                .last = p.last,
                                .depth = p.depth,
                                .phase_index = phase,
                                .condition = why,
                                .predicate_condition = why,
                                .phase_condition = {},
                                .side = {},
                                .pair = {},
                                .partition = {},
                                .limiting_bound = {}};
}
auto checkpoint_same_point(const BoardingRoutePhasePointConstant& a,
                           const BoardingRoutePhasePointConstant& b) -> bool {
  for (std::size_t i = 0; i < 3; ++i)
    if (a.coordinates[i].count != b.coordinates[i].count ||
        a.coordinates[i].terms != b.coordinates[i].terms)
      return false;
  return true;
}
auto checkpoint_join_identity(const BoardingRouteFootPhaseRequest& a,
                              const BoardingRouteFootPhaseRequest& b) -> bool {
  if (!checkpoint_same_point(a.root[1], b.root[0]) ||
      a.root_yaw_half[1] != b.root_yaw_half[0] ||
      a.torso_lean_half[1] != b.torso_lean_half[0] ||
      a.port_reaction_fraction[1] != b.port_reaction_fraction[0] ||
      a.seconds_per_parameter != 12 || b.seconds_per_parameter != 12)
    return false;
  for (std::size_t i = 0; i < 2; ++i)
    if (!checkpoint_same_point(a.feet[i].sole[1], b.feet[i].sole[0]) ||
        a.feet[i].yaw_half[1] != b.feet[i].yaw_half[0] ||
        a.feet[i].swing_height_metres != 0 ||
        b.feet[i].swing_height_metres != 0)
      return false;
  return true;
}
template <class Diagnostic>
auto checkpoint_covered(const Diagnostic& d, double first, double last)
    -> bool {
  if (d.cells.empty()) return false;
  auto next = first;
  for (const auto& c : d.cells) {
    const auto& a = c.assessment;
    if (c.global_first != next || c.global_first > c.global_last ||
        c.phase_index > 2 ||
        checkpoint_mandatory_join({c.global_first, c.global_last, 0}) ||
        c.phase_index !=
            checkpoint_phase_index(c.global_first, c.global_last) ||
        a.phase.first != checkpoint_local(c.phase_index, c.global_first) ||
        a.phase.last != checkpoint_local(c.phase_index, c.global_last) ||
        !a.phase.complete || !a.complete || !a.arithmetic_supported ||
        !a.self_complete || !a.nonnegative_reactions ||
        !a.nominal_vertical_equilibrium_complete || !a.finite_pressure_complete)
      return false;
    next = c.global_last;
  }
  return next == last;
}
template <class Diagnostic>
auto checkpoint_qualify_joins(Diagnostic& d) -> void {
  if (d.cells.size() < 2) return;
  const auto& left = d.cells[d.cells.size() - 2];
  const auto& right = d.cells.back();
  for (std::size_t j = 0; j < 2; ++j) {
    const auto join = j == 0 ? .25 : .5;
    if (d.join_expression_identity[j] &&
        std::min(d.requested_first, d.requested_last) < join &&
        std::max(d.requested_first, d.requested_last) > join &&
        left.phase_index == j && right.phase_index == j + 1 &&
        left.global_first < join && left.global_last == join &&
        right.global_first == join && right.global_last > join &&
        left.assessment.complete && right.assessment.complete)
      d.qualified_joins[j] = true;
  }
}
struct CheckPolicy01 {
  using Diagnostic = BoardingRouteCheckpointUnloadDiagnostic;
  using Cell = BoardingRouteCheckpointUnloadCell;
  using Limits = detail::BoardingRouteCheckpointUnloadLimits;
  using CellRefusal = BoardingRoutePortUnloadRefusal;
  static auto valid(const Limits& limits) -> bool {
    return checkpoint_valid_limits(limits);
  }
  static auto prepare(const OriginBoardingBootSupport& source,
                      const Limits& limits, Diagnostic& d)
      -> std::expected<detail::BoardingRouteCheckpointUnloadContext,
                       std::string> {
    return detail::prepare_boarding_route_checkpoint_unload(source, limits, d);
  }
  static auto cell(const detail::BoardingRouteCheckpointUnloadContext& context,
                   std::size_t phase, double first, double last, bool reverse,
                   const Limits& limits, Diagnostic& d, Cell& c, CellRefusal& r)
      -> CheckState {
    return detail::boarding_route_checkpoint_unload_cell(
        context, phase, first, last, reverse, limits, d.work, c.assessment, r);
  }
  static auto assign_refusal(Diagnostic& d, const CheckReason& r,
                             const CellRefusal&) -> void {
    d.first_refusal = r;
  }
};
struct CheckPolicy02 {
  using Diagnostic = BoardingRouteCheckpointUnloadSelf02Diagnostic;
  using Cell = BoardingRouteCheckpointUnloadSelf02Cell;
  using Limits = detail::BoardingRouteCheckpointUnloadSelf02Limits;
  using CellRefusal = detail::BoardingRouteCheckpointUnloadSelf02CellRefusal;
  static auto valid(const Limits& limits) -> bool {
    return checkpoint_valid_limits(limits) &&
           limits.hip_complement_attempts <=
               kBoardingRouteCheckpointUnloadSelf02MaximumHipAttempts;
  }
  [[gnu::noinline]] static auto prepare(const OriginBoardingBootSupport& source,
                                        const Limits& limits, Diagnostic& d)
      -> std::expected<detail::BoardingRouteCheckpointUnloadContext,
                       std::string> {
    BoardingRouteCheckpointUnloadDiagnostic staged;
    auto prepared = detail::prepare_boarding_route_checkpoint_unload(
        source, limits, staged);
    d.initial = std::move(staged.initial);
    if (!prepared) return std::unexpected(prepared.error());
    return std::move(*prepared);
  }
  static auto cell(const detail::BoardingRouteCheckpointUnloadContext& context,
                   std::size_t phase, double first, double last, bool reverse,
                   const Limits& limits, Diagnostic& d, Cell& c, CellRefusal& r)
      -> CheckState {
    return detail::boarding_route_checkpoint_unload_self02_cell(
        context, phase, first, last, reverse, limits, d.work,
        d.hip_complement_attempts, c, r);
  }
  static auto assign_refusal(Diagnostic& d, const CheckReason& r,
                             const CellRefusal& c) -> void {
    BoardingRouteCheckpointUnloadSelf02Refusal out(r);
    if (c.hip_complement_capacity)
      out.self02_condition =
          BoardingRouteCheckpointUnloadSelf02Condition::hip_complement_capacity;
    d.first_refusal = out;
  }
};
static_assert(sizeof(CheckPolicy02::Cell) <=
              kBoardingRouteCheckpointUnloadMaximumCellBytes);
static_assert(sizeof(std::expected<CheckPolicy02::Diagnostic, std::string>) +
                  sizeof(BoardingSourceEndpointLoadDiagnostic) +
                  1024 * sizeof(CheckPolicy02::Cell) <=
              kBoardingRouteCheckpointUnloadMaximumOutputBytes);

} // namespace
auto detail::boarding_route_checkpoint_unload_controls(std::size_t phase)
    -> std::optional<BoardingRouteFootPhaseRequest> {
  if (phase > 2) return {};
  auto r = boarding_route_port_unload_controls();
  const BoardingRoutePhasePointConstant checkpoint{
      {{{{.16, .07, 0}, 2}, {{.847, .012, 0}, 2}, {{-.55, -.04, 0}, 2}}}};
  if (phase == 0) {
    r.root[1] = checkpoint;
    r.root_yaw_half = {0, 0};
    r.port_reaction_fraction = {.5, .25};
  } else if (phase == 1) {
    r.root = {checkpoint, checkpoint};
    r.root_yaw_half = {0, .25};
    r.port_reaction_fraction = {.25, .25};
  } else {
    r.root[0] = checkpoint;
    r.root_yaw_half = {.25, .25};
    r.port_reaction_fraction = {.25, 0};
  }
  return r;
}
auto detail::boarding_route_checkpoint_unload_clock(double g) -> double {
  return g <= .25 ? 48 * g : g <= .5 ? 12 + 48 * (g - .25) : 24 + 24 * (g - .5);
}
namespace detail {
template <class Policy>
auto checkpoint_unload_selected(const OriginBoardingBootSupport& source,
                                double first, double last,
                                typename Policy::Limits limits)
    -> std::expected<typename Policy::Diagnostic, std::string> {
  using Diagnostic = typename Policy::Diagnostic;
  using Cell = typename Policy::Cell;
  constexpr std::size_t fixed_output =
      sizeof(std::expected<Diagnostic, std::string>) +
      sizeof(BoardingSourceEndpointLoadDiagnostic);
  if (!std::isfinite(first) || !std::isfinite(last) || first < 0 || first > 1 ||
      last < 0 || last > 1 || !Policy::valid(limits))
    return std::unexpected(
        "Checkpoint unload requires finite [0,1] endpoints and lowered caps");
  Diagnostic result;
  for (std::size_t i = 0; i < 3; ++i)
    result.controls[i] = *boarding_route_checkpoint_unload_controls(i);
  result.parts = boarding_route_foot_phase_parts();
  result.requested_first = first;
  result.requested_last = last;
  result.reverse = first > last;
  result.reporting_elapsed_seconds =
      std::abs(boarding_route_checkpoint_unload_clock(last) -
               boarding_route_checkpoint_unload_clock(first));
  for (std::size_t j = 0; j < 2; ++j)
    result.join_expression_identity[j] =
        checkpoint_join_identity(result.controls[j], result.controls[j + 1]);
  const CheckPending requested{std::min(first, last), std::max(first, last), 0};
  if (limits.output_bytes < fixed_output) {
    checkpoint_refuse(result, requested, CheckWhy::output_capacity);
    return result;
  }
  auto prepared = Policy::prepare(source, limits, result);
  if (result.initial) {
    result.owned_initial_quad_guards = result.initial->load.checked_quads;
    for (const auto& q : result.initial->load.source_quads) {
      result.owned_initial_quad_edges += q.checked_edges;
      result.owned_initial_convexity_signs += q.checked_side_signs;
    }
    result.owned_initial_triangle_windings =
        2 * result.owned_initial_quad_guards;
    result.owned_initial_diagonal_incidence_guards =
        result.owned_initial_quad_guards;
  }
  if (!prepared) {
    if (!result.initial) return std::unexpected(prepared.error());
    checkpoint_refuse(result, requested,
                      result.initial->load.complete
                          ? CheckWhy::invalid_binding
                          : CheckWhy::initial_prerequisite);
    return result;
  }
  if (!boarding_route_foot_phase_environment()) {
    checkpoint_refuse(result, requested, CheckWhy::unsupported_arithmetic);
    return result;
  }
  result.arithmetic_supported = true;
  const auto capacity = first == last
                            ? std::min(std::size_t{1}, limits.phase.leaves)
                            : limits.phase.leaves;
  if (capacity > (limits.output_bytes - fixed_output) / sizeof(Cell)) {
    checkpoint_refuse(result, requested, CheckWhy::output_capacity);
    return result;
  }
  try {
    result.cells.reserve(capacity);
  } catch (const std::bad_alloc&) {
    return std::unexpected("Checkpoint unload cell allocation failed");
  }
  if (result.cells.capacity() >
      (limits.output_bytes - fixed_output) / sizeof(Cell)) {
    checkpoint_refuse(result, requested, CheckWhy::output_capacity);
    return result;
  }
  result.output_capacity_bytes =
      fixed_output + result.cells.capacity() * sizeof(Cell);
  std::array<CheckPending, 11> pending{};
  pending[0] = requested;
  std::size_t count{1};
  Cell cell;
  while (count != 0) {
    const auto p = pending[--count];
    if (result.examined_nodes >= limits.phase.nodes) {
      checkpoint_refuse(result, p, CheckWhy::node_capacity);
      return result;
    }
    if (result.cells.size() >= limits.phase.leaves) {
      checkpoint_refuse(result, p, CheckWhy::leaf_capacity);
      return result;
    }
    ++result.examined_nodes;
    result.maximum_depth = std::max(result.maximum_depth, p.depth);
    if (const auto join = checkpoint_mandatory_join(p)) {
      if (p.depth >= limits.phase.depth) {
        checkpoint_refuse(result, p, CheckWhy::depth_capacity);
        return result;
      }
      if (count + 2 > pending.size()) {
        checkpoint_refuse(result, p, CheckWhy::unsplittable_interval);
        return result;
      }
      ++result.mandatory_splits;
      pending[count++] = {*join, p.last, p.depth + 1};
      pending[count++] = {p.first, *join, p.depth + 1};
      continue;
    }
    const auto phase = checkpoint_phase_index(p.first, p.last);
    cell.global_first = p.first;
    cell.global_last = p.last;
    cell.phase_index = phase;
    typename Policy::CellRefusal reason;
    const auto state =
        Policy::cell(*prepared, phase, checkpoint_local(phase, p.first),
                     checkpoint_local(phase, p.last), result.reverse, limits,
                     result, cell, reason);
    if (state == CheckState::accepted) {
      if (!cell.assessment.complete ||
          cell.assessment.phase.first != checkpoint_local(phase, p.first) ||
          cell.assessment.phase.last != checkpoint_local(phase, p.last)) {
        checkpoint_refuse(result, p, CheckWhy::incomplete_cover, phase);
        return result;
      }
      if (result.cells.size() == result.cells.capacity()) {
        checkpoint_refuse(result, p, CheckWhy::output_capacity, phase);
        return result;
      }
      result.cells.push_back(cell);
      checkpoint_qualify_joins(result);
      continue;
    }
    CheckReason refusal{.first = p.first,
                        .last = p.last,
                        .depth = p.depth,
                        .phase_index = phase,
                        .condition = reason.condition,
                        .predicate_condition = reason.condition,
                        .phase_condition = reason.phase_condition,
                        .side = reason.side,
                        .pair = reason.pair,
                        .partition = reason.partition,
                        .limiting_bound = reason.limiting_bound};
    if (state == CheckState::unsupported || state == CheckState::capacity) {
      result.arithmetic_supported = state != CheckState::unsupported;
      Policy::assign_refusal(result, refusal, reason);
      return result;
    }
    if (p.first == p.last)
      refusal.condition = CheckWhy::unsplittable_interval;
    else if (p.depth >= limits.phase.depth)
      refusal.condition = CheckWhy::depth_capacity;
    else {
      const auto mid = p.first + (p.last - p.first) * .5;
      if (mid > p.first && mid < p.last && count + 2 <= pending.size()) {
        pending[count++] = {mid, p.last, p.depth + 1};
        pending[count++] = {p.first, mid, p.depth + 1};
        continue;
      }
      refusal.condition = CheckWhy::unsplittable_interval;
    }
    Policy::assign_refusal(result, refusal, reason);
    return result;
  }
  result.complete = checkpoint_covered(result, requested.first, requested.last);
  result.kinematics_complete = result.timing_complete = result.self_complete =
      result.nonnegative_reactions =
          result.nominal_vertical_equilibrium_complete =
              result.finite_pressure_complete = result.support_complete =
                  result.complete;
  result.endpoint_zero_port_reaction = result.complete && requested.last == 1;
  if (!result.complete)
    checkpoint_refuse(result, requested, CheckWhy::incomplete_cover);
  return result;
}
} // namespace detail
auto detail::boarding_route_checkpoint_unload_bounded(
    const OriginBoardingBootSupport& source, double first, double last,
    CheckLimits limits) -> std::expected<CheckDiagnostic, std::string> {
  return checkpoint_unload_selected<CheckPolicy01>(source, first, last, limits);
}
auto detail::boarding_route_checkpoint_unload_self02_bounded(
    const OriginBoardingBootSupport& source, double first, double last,
    BoardingRouteCheckpointUnloadSelf02Limits limits)
    -> std::expected<BoardingRouteCheckpointUnloadSelf02Diagnostic,
                     std::string> {
  return checkpoint_unload_selected<CheckPolicy02>(source, first, last, limits);
}

auto assess_origin_boarding_route_checkpoint_unload(
    const OriginBoardingBootSupport& source, double first, double last)
    -> std::expected<BoardingRouteCheckpointUnloadDiagnostic, std::string> {
  return detail::boarding_route_checkpoint_unload_bounded(source, first, last);
}
} // namespace apsis_drift
