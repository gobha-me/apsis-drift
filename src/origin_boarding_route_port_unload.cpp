#include "apsis_drift/origin_boarding_route_port_unload.hpp"
#include "origin_boarding_route_port_unload_internal.hpp"
#include "origin_boarding_source_endpoint_load_internal.hpp"
#include <algorithm>
#include <cmath>
#include <new>
#include <utility>
namespace apsis_drift {
namespace {
using D = BoardingRoutePortUnloadDiagnostic;
using C = BoardingRoutePortUnloadCell;
using R = BoardingRoutePortUnloadRefusal;
using Why = BoardingRoutePortUnloadCondition;
using Limits = detail::BoardingRoutePortUnloadLimits;
using State = detail::BoardingRoutePortUnloadCellResult;
struct Pending {
  double first{}, last{};
  std::size_t depth{};
};
constexpr std::size_t fixed_output =
    sizeof(std::expected<D, std::string>) +
    sizeof(BoardingSourceEndpointLoadDiagnostic);
static_assert(sizeof(C) <= kBoardingRoutePortUnloadMaximumCellBytes);
static_assert(fixed_output + sizeof(C) * 1024 <=
              kBoardingRoutePortUnloadMaximumOutputBytes);
auto valid_limits(const Limits& c) -> bool {
  const Limits o;
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
auto refuse(D& d, Pending p, Why why) -> void {
  d.first_refusal = R{.first = p.first,
                      .last = p.last,
                      .depth = p.depth,
                      .condition = why,
                      .predicate_condition = why,
                      .phase_condition = {},
                      .side = {},
                      .pair = {},
                      .partition = {},
                      .limiting_bound = {}};
}
auto covered(const D& d, double first, double last) -> bool {
  if (d.cells.empty()) return false;
  auto next = first;
  for (const auto& c : d.cells) {
    if (c.phase.first != next || c.phase.first > c.phase.last ||
        !c.phase.complete || !c.complete || !c.arithmetic_supported ||
        !c.self_complete || !c.nonnegative_reactions ||
        !c.nominal_vertical_equilibrium_complete || !c.finite_pressure_complete)
      return false;
    next = c.phase.last;
  }
  return next == last;
}
} // namespace
auto detail::boarding_route_port_unload_controls()
    -> BoardingRouteFootPhaseRequest {
  BoardingRouteFootPhaseRequest r;
  const auto constant = [](double a) -> BoardingRoutePhaseConstant {
    return {{a, 0, 0}, 1};
  };
  const auto sum = [](double a, double b) -> BoardingRoutePhaseConstant {
    return {{a, b, 0}, 2};
  };
  r.root[0].coordinates = {constant(.16), constant(.847), constant(-.55)};
  r.root[1].coordinates = {sum(.16, .16), sum(.847, .012), sum(-.55, -.17)};
  for (std::size_t i = 0; i < 2; ++i) {
    const auto sole = BoardingRoutePhasePointConstant{
        {sum(.16, i == 0 ? -.14 : .14),
         constant(i == 0 ? -100000.0 * 1e-6 : -160000.0 * 1e-6),
         constant(i == 0 ? -.5 : -.8)}};
    r.feet[i].sole = {sole, sole};
  }
  r.root_yaw_half = {0, .25};
  r.seconds_per_parameter = 12;
  r.port_reaction_fraction = {.5, 0};
  return r;
}
auto detail::boarding_route_port_unload_bounded(
    const OriginBoardingBootSupport& source, double first, double last,
    Limits limits) -> std::expected<D, std::string> {
  if (!std::isfinite(first) || !std::isfinite(last) || first < 0 || first > 1 ||
      last < 0 || last > 1 || !valid_limits(limits))
    return std::unexpected(
        "Port unload requires finite [0,1] endpoints and lowered caps");
  D result;
  result.controls = boarding_route_port_unload_controls();
  result.parts = boarding_route_foot_phase_parts();
  result.requested_first = first;
  result.requested_last = last;
  result.reverse = first > last;
  result.reporting_elapsed_seconds = 12 * std::abs(last - first);
  const Pending requested{std::min(first, last), std::max(first, last), 0};
  if (limits.output_bytes < fixed_output) {
    refuse(result, requested, Why::output_capacity);
    return result;
  }
  auto prepared = prepare_boarding_route_port_unload(source, limits, result);
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
    refuse(result, requested,
           result.initial->load.complete ? Why::invalid_binding
                                         : Why::initial_prerequisite);
    return result;
  }
  if (!boarding_route_foot_phase_environment()) {
    refuse(result, requested, Why::unsupported_arithmetic);
    return result;
  }
  result.arithmetic_supported = true;
  const auto capacity = first == last
                            ? std::min(std::size_t{1}, limits.phase.leaves)
                            : limits.phase.leaves;
  if (limits.output_bytes < fixed_output ||
      capacity > (limits.output_bytes - fixed_output) / sizeof(C)) {
    refuse(result, requested, Why::output_capacity);
    return result;
  }
  try {
    result.cells.reserve(capacity);
  } catch (const std::bad_alloc&) {
    return std::unexpected("Port unload cell allocation failed");
  }
  if (result.cells.capacity() >
      (limits.output_bytes - fixed_output) / sizeof(C)) {
    refuse(result, requested, Why::output_capacity);
    return result;
  }
  result.output_capacity_bytes =
      fixed_output + result.cells.capacity() * sizeof(C);
  std::array<Pending, 11> pending{};
  pending[0] = requested;
  std::size_t count{1};
  C cell;
  while (count != 0) {
    const auto p = pending[--count];
    if (result.examined_nodes >= limits.phase.nodes) {
      refuse(result, p, Why::node_capacity);
      return result;
    }
    if (result.cells.size() >= limits.phase.leaves) {
      refuse(result, p, Why::leaf_capacity);
      return result;
    }
    ++result.examined_nodes;
    result.maximum_depth = std::max(result.maximum_depth, p.depth);
    R reason;
    const auto state = boarding_route_port_unload_cell(
        *prepared, p.first, p.last, result.reverse, limits, result.work, cell,
        reason);
    reason.first = p.first;
    reason.last = p.last;
    reason.depth = p.depth;
    reason.predicate_condition = reason.condition;
    if (state == State::accepted) {
      if (!cell.complete || cell.phase.first != p.first ||
          cell.phase.last != p.last) {
        refuse(result, p, Why::incomplete_cover);
        return result;
      }
      if (result.cells.size() == result.cells.capacity()) {
        refuse(result, p, Why::output_capacity);
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
      reason.condition = Why::unsplittable_interval;
    else if (p.depth >= limits.phase.depth)
      reason.condition = Why::depth_capacity;
    else {
      const auto mid = p.first + (p.last - p.first) * .5;
      if (mid > p.first && mid < p.last && count + 2 <= pending.size()) {
        pending[count++] = {mid, p.last, p.depth + 1};
        pending[count++] = {p.first, mid, p.depth + 1};
        continue;
      }
      reason.condition = Why::unsplittable_interval;
    }
    result.first_refusal = reason;
    return result;
  }
  result.complete = covered(result, requested.first, requested.last);
  result.kinematics_complete = result.timing_complete = result.self_complete =
      result.nonnegative_reactions =
          result.nominal_vertical_equilibrium_complete =
              result.finite_pressure_complete = result.support_complete =
                  result.complete;
  result.endpoint_zero_port_reaction = result.complete && requested.last == 1;
  if (!result.complete) refuse(result, requested, Why::incomplete_cover);
  return result;
}
auto assess_origin_boarding_route_port_unload(
    const OriginBoardingBootSupport& source, double first, double last)
    -> std::expected<BoardingRoutePortUnloadDiagnostic, std::string> {
  return detail::boarding_route_port_unload_bounded(source, first, last);
}
} // namespace apsis_drift
