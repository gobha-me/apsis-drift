#include "origin_boarding_intermediate_endpoint04_internal.hpp"
#include "origin_boarding_intermediate_pause_support02_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
namespace apsis_drift {
namespace {
using D = BoardingIntermediateEndpoint04Diagnostic;
using C = BoardingIntermediateEndpoint04Cell;
using R = BoardingIntermediateEndpoint04Refusal;
using W = BoardingIntermediateEndpoint04Condition;
using S = BoardingIntermediateEndpoint04State;
using Stage = BoardingIntermediateEndpoint04SelfStage;
using L = detail::BoardingIntermediateEndpoint04Limits;
using E = BoardingIntermediateEndpoint04Expected;
using Request = BoardingRouteFootPhaseRequest;
using A = detail::BoardingIntermediatePauseSupportAccess;
using P = BoardingPlantedBodyPointId;
constexpr std::size_t fixed_output = 2 * sizeof(E);
static_assert(sizeof(E) <= kBoardingIntermediateEndpoint04MaximumExpectedBytes);
static_assert(sizeof(C) <= kBoardingIntermediateEndpoint04MaximumCellBytes);
static_assert(sizeof(R) <= 184);
static_assert(sizeof(BoardingIntermediateEndpoint04SliceEvidence) <= 144);
static_assert(sizeof(detail::BoardingIntermediateEndpoint04ProgramKey) <= 16);
static_assert(sizeof(detail::BoardingIntermediateEndpoint04AdmissionContext) <=
              40);
static_assert(sizeof(detail::BoardingIntermediateEndpoint04CurrentToken) <= 40);
static_assert(sizeof(L) == 200);
// Every historical source reservation is preserved whole and distinct.
constexpr auto graph_live =
    32768 + fixed_output + 4096 + sizeof(C) -
    sizeof(BoardingRouteFootPhaseCell) + 4 * sizeof(L) + 2048 +
    sizeof(detail::BoardingRouteFootPhaseLimits) +
    sizeof(BoardingRouteFootPhaseRefusal) + sizeof(std::optional<Request>) +
    sizeof(Request) + 2 * sizeof(R) +
    sizeof(detail::BoardingIntermediateEndpoint04AdmissionContext) +
    sizeof(detail::BoardingIntermediateEndpoint04CurrentToken) +
    2 * sizeof(detail::BoardingIntermediateEndpoint04ProgramKey) + 64 + 152;
constexpr auto source_live =
    33208 + fixed_output + 4 * sizeof(L) + 2 * sizeof(R) +
    sizeof(detail::BoardingIntermediateEndpoint04AdmissionContext) +
    sizeof(detail::BoardingIntermediateEndpoint04CurrentToken) +
    2 * sizeof(detail::BoardingIntermediateEndpoint04ProgramKey) + 64 +
    sizeof(Request) + sizeof(std::optional<Request>) + 152;
constexpr auto construction_live =
    fixed_output + 4096 + 4 * sizeof(L) + 2048 + sizeof(Request) +
    sizeof(std::optional<Request>) + 2 * sizeof(R) +
    2 * sizeof(detail::BoardingIntermediateEndpoint04ProgramKey) +
    sizeof(detail::BoardingIntermediateEndpoint04AdmissionContext) + 176 +
    4096 + 512 + 64 + 152;
constexpr auto body_audit_live =
    16800 + sizeof(C) + fixed_output + 4096 + 4 * sizeof(L) + 2048 +
    sizeof(Request) + sizeof(std::optional<Request>) + 8192 + 1024 + 40 + 152;
static_assert(graph_live <= kBoardingIntermediateEndpoint04MaximumScratchBytes);
static_assert(source_live + 4096 <=
              kBoardingIntermediateEndpoint04MaximumScratchBytes);
static_assert(construction_live <=
              kBoardingIntermediateEndpoint04MaximumScratchBytes);
static_assert(body_audit_live <=
              kBoardingIntermediateEndpoint04MaximumScratchBytes);
static_assert(fixed_output + sizeof(C) <=
              kBoardingIntermediateEndpoint04MaximumOutputBytes);
auto cap(W w) -> bool {
  return w == W::output_capacity || w == W::source_capacity ||
         w == W::projection_capacity || w == W::definition_capacity ||
         w == W::sole_extrema_capacity || w == W::source_coordinate_capacity ||
         w == W::intersection_capacity || w == W::midpoint_capacity ||
         w == W::allocation_capacity || w == W::pressure_capacity ||
         w == W::edge_capacity || w == W::self_body_capacity ||
         w == W::unit_axis_capacity || w == W::self_pair_capacity ||
         w == W::self_axis_capacity || w == W::self_signed_capacity ||
         w == W::self_owner_capacity || w == W::self_hip_capacity ||
         w == W::slice_guard_capacity || w == W::slice_operation_capacity;
}
auto same(const BoardingRoutePhaseConstant& a,
          const BoardingRoutePhaseConstant& b) -> bool {
  return a.count == b.count && a.terms == b.terms;
}
auto valid_limits(const L& l) -> bool {
  const L max;
  return l.phase.graphs <= max.phase.graphs && l.phase.legs <= max.phase.legs &&
         l.phase.bodies <= max.phase.bodies &&
         l.phase.sectors <= max.phase.sectors &&
         l.phase.timing <= max.phase.timing &&
         l.source_guards <= max.source_guards &&
         l.projection_guards <= max.projection_guards &&
         l.definition_guards <= max.definition_guards &&
         l.sole_extrema <= max.sole_extrema &&
         l.source_coordinates <= max.source_coordinates &&
         l.intersection_operations <= max.intersection_operations &&
         l.midpoint_operations <= max.midpoint_operations &&
         l.allocation_operations <= max.allocation_operations &&
         l.pressure_candidates <= max.pressure_candidates &&
         l.disk_edges <= max.disk_edges &&
         l.self_body_guards <= max.self_body_guards &&
         l.self_pairs <= max.self_pairs && l.self_axes <= max.self_axes &&
         l.self_signed_trials <= max.self_signed_trials &&
         l.self_owners <= max.self_owners &&
         l.self_hip_complements <= max.self_hip_complements &&
         l.unit_axis_operations <= max.unit_axis_operations &&
         l.output_bytes <= max.output_bytes &&
         l.construction_guards <= max.construction_guards &&
         l.construction_operations <= max.construction_operations;
}
auto coord(const RigidVector3& v, std::size_t i) -> double {
  return i == 0 ? v.x : i == 1 ? v.y : v.z;
}
auto projection_charge(D& d, const L& l, R& r) -> bool {
  r = R{};
  r.self_stage = Stage::projection;
  r.operation = static_cast<std::uint8_t>(d.work.projection_guards);
  return detail::intermediate_endpoint04_charge(d, r, d.work.projection_guards,
                                                l.projection_guards,
                                                W::projection_capacity);
}
auto point_constant(const BoardingRoutePhaseConstant& x,
                    const std::array<double, 3>& terms, std::size_t count)
    -> bool {
  return x.count == count && x.terms == terms;
}
auto carrier_value(const C& c, std::size_t index)
    -> const BoardingPlantedBodyPointEvidence& {
  if (index == 0) return c.phase.center_of_mass;
  if (index < 5) {
    const std::array<P, 4> ids{P::port_boot_center, P::starboard_boot_center,
                               P::port_ankle, P::starboard_ankle};
    return c.phase.points[static_cast<std::size_t>(ids[index - 1])];
  }
  return c.phase.frames[2 + (index - 5) / 3].columns[(index - 5) % 3];
}
auto carrier(D& d, C& c, const L& l, R& r, std::size_t index) -> bool {
  for (std::size_t kind = 0; kind < 3; ++kind)
    for (std::size_t a = 0; a < 3; ++a) {
      if (!projection_charge(d, l, r)) return false;
      const auto& value = carrier_value(c, index);
      const auto& b = kind == 0   ? value.value
                      : kind == 1 ? value.derivatives.velocity
                                  : value.derivatives.acceleration;
      const auto low = coord(b.lower, a);
      if (!std::isfinite(low) || (kind == 0 && std::abs(low) > 8) ||
          (kind != 0 && low > 0))
        return detail::intermediate_endpoint04_refuse(
            d, r, W::unsupported_arithmetic);
      if (!projection_charge(d, l, r)) return false;
      const auto high = coord(b.upper, a);
      if (!std::isfinite(high) || low > high ||
          (kind == 0 && std::abs(high) > 8) || (kind != 0 && high < 0))
        return detail::intermediate_endpoint04_refuse(
            d, r, W::unsupported_arithmetic);
    }
  c.projected_carrier_complete[index] = true;
  return true;
}
[[gnu::noinline]] auto projection(
    D& d, C& c, const Request& request,
    const detail::BoardingIntermediateEndpoint04ProgramKey& key, const L& l,
    R& r) -> bool {
  const auto& p = c.phase;
  for (std::size_t i = 0; i < 7; ++i) {
    if (!projection_charge(d, l, r)) return false;
    const bool ok = i == 0   ? p.complete
                    : i == 1 ? p.arithmetic_supported
                    : i == 2 ? p.nominal_links
                    : i == 3 ? p.target_sole_identities
                    : i == 4 ? p.joint_sectors
                    : i == 5 ? p.derivative_domains
                             : p.timing_complete;
    if (!ok)
      return detail::intermediate_endpoint04_refuse(d, r,
                                                    W::phase_prerequisite);
  }
  if (!projection_charge(d, l, r)) return false;
  if (p.first != 0)
    return detail::intermediate_endpoint04_refuse(d, r, W::projection_identity);
  if (!projection_charge(d, l, r)) return false;
  if (p.last != 1)
    return detail::intermediate_endpoint04_refuse(d, r, W::projection_identity);
  std::optional<Request> canonical;
  for (std::size_t a = 0; a < 3; ++a) {
    if (!projection_charge(d, l, r)) return false;
    if (!canonical) canonical = detail::intermediate_endpoint04_canonical(key);
    if (!canonical ||
        !same(request.root[0].coordinates[a],
              canonical->root[0].coordinates[a]) ||
        !same(request.root[1].coordinates[a],
              canonical->root[1].coordinates[a]) ||
        !same(request.root[0].coordinates[a], request.root[1].coordinates[a]))
      return detail::intermediate_endpoint04_refuse(d, r,
                                                    W::projection_identity);
  }
  for (std::size_t side = 0; side < 2; ++side) {
    for (std::size_t a = 0; a < 3; ++a) {
      if (!projection_charge(d, l, r)) return false;
      if (!same(request.feet[side].sole[0].coordinates[a],
                canonical->feet[side].sole[0].coordinates[a]) ||
          !same(request.feet[side].sole[1].coordinates[a],
                canonical->feet[side].sole[1].coordinates[a]) ||
          !same(request.feet[side].sole[0].coordinates[a],
                request.feet[side].sole[1].coordinates[a]))
        return detail::intermediate_endpoint04_refuse(d, r,
                                                      W::projection_identity);
    }
    if (!projection_charge(d, l, r)) return false;
    if (request.feet[side].yaw_half != std::array<double, 2>{0, 0})
      return detail::intermediate_endpoint04_refuse(d, r,
                                                    W::projection_identity);
    if (!projection_charge(d, l, r)) return false;
    if (request.feet[side].swing_height_metres != 0)
      return detail::intermediate_endpoint04_refuse(d, r,
                                                    W::projection_identity);
  }
  for (std::size_t i = 0; i < 4; ++i) {
    if (!projection_charge(d, l, r)) return false;
    const bool ok =
        i == 0   ? request.root_yaw_half == canonical->root_yaw_half
        : i == 1 ? request.torso_lean_half == canonical->torso_lean_half
        : i == 2
            ? request.port_reaction_fraction ==
                  canonical->port_reaction_fraction
            : request.seconds_per_parameter == canonical->seconds_per_parameter;
    if (!ok)
      return detail::intermediate_endpoint04_refuse(d, r,
                                                    W::projection_identity);
  }
  for (std::size_t i = 0; i < 15; ++i) {
    if (!projection_charge(d, l, r)) return false;
    if (static_cast<std::size_t>(d.parts[i].id) != i)
      return detail::intermediate_endpoint04_refuse(d, r,
                                                    W::projection_identity);
  }
  for (std::size_t side = 0; side < 2; ++side) {
    const auto index = static_cast<std::size_t>(
        side == 0 ? BoardingBodyPartId::port_boot
                  : BoardingBodyPartId::starboard_boot);
    if (!projection_charge(d, l, r)) return false;
    const auto* box =
        std::get_if<BoardingRoutePhaseBoxBinding>(&d.parts[index].reservation);
    if (!box)
      return detail::intermediate_endpoint04_refuse(d, r,
                                                    W::projection_identity);
    if (!projection_charge(d, l, r)) return false;
    if (box->center !=
        (side == 0 ? P::port_boot_center : P::starboard_boot_center))
      return detail::intermediate_endpoint04_refuse(d, r,
                                                    W::projection_identity);
    if (!projection_charge(d, l, r)) return false;
    if (box->half_size_metres != RigidVector3{.06, .05, .14})
      return detail::intermediate_endpoint04_refuse(d, r,
                                                    W::projection_identity);
    if (!projection_charge(d, l, r)) return false;
    if (box->frame != (side == 0 ? BoardingRoutePhaseFrame::port_sole
                                 : BoardingRoutePhaseFrame::starboard_sole))
      return detail::intermediate_endpoint04_refuse(d, r,
                                                    W::projection_identity);
    if (!projection_charge(d, l, r)) return false;
    const auto* source = A::partition(d.source, side);
    if (!source ||
        !point_constant(request.feet[side].sole[0].coordinates[1],
                        {source->plane_metres, 0, 0}, 1) ||
        !point_constant(request.feet[side].sole[1].coordinates[1],
                        {source->plane_metres, 0, 0}, 1))
      return detail::intermediate_endpoint04_refuse(d, r, W::sole_plane);
    c.sites[side].plane_identity = true;
  }
  if (!projection_charge(d, l, r)) return false;
  const auto share = p.port_reaction_fraction;
  if (!std::isfinite(share.lower) || !std::isfinite(share.upper) ||
      share.lower > share.upper || share.lower > .0625 || share.upper < .0625)
    return detail::intermediate_endpoint04_refuse(d, r,
                                                  W::unsupported_arithmetic);
  c.constant_state = true;
  for (std::size_t i = 0; i < 11; ++i)
    if (!carrier(d, c, l, r, i)) return false;
  c.projection_complete = true;
  return true;
}
[[gnu::noinline]] auto reset(C& c) -> void {
  c = C{};
}
} // namespace

auto detail::intermediate_endpoint04_refuse(D& d, R& r, W w) -> bool {
  r.condition = r.predicate_condition = w;
  if (!d.first_refusal) d.first_refusal = r;
  const bool hard =
      cap(w) || w == W::unsupported_arithmetic || w == W::invalid_binding;
  if (hard) {
    d.stop_condition = w;
    d.stop_stage = r.self_stage;
    d.stop_operation = r.operation;
    d.stop_self_pair = r.self_pair;
    d.stop_self_region = r.self_region;
    d.stop_self_axis = r.self_axis;
    d.stop_self_sign = r.self_sign;
    d.state = cap(w)                           ? S::capacity
              : w == W::unsupported_arithmetic ? S::unsupported
                                               : S::unresolved;
    if (w == W::unsupported_arithmetic) {
      d.arithmetic_supported = false;
      if (!d.slice.complete) d.slice.arithmetic_supported = false;
    }
  } else if (d.state != S::capacity && d.state != S::unsupported) {
    d.state = S::unresolved;
    if (w != W::sole_disk && w != W::source_disk) {
      d.stop_condition = w;
      d.stop_stage = r.self_stage;
      d.stop_operation = r.operation;
      d.stop_self_pair = r.self_pair;
      d.stop_self_region = r.self_region;
      d.stop_self_axis = r.self_axis;
      d.stop_self_sign = r.self_sign;
    }
  }
  return false;
}
auto detail::intermediate_endpoint04_charge(D& d, R& r, std::uint64_t& work,
                                            std::size_t limit, W why) -> bool {
  if (work >= limit) return intermediate_endpoint04_refuse(d, r, why);
  ++work;
  return true;
}
auto detail::intermediate_endpoint04_definition_charge(D& d, C& c, const L& l,
                                                       R& r) -> bool {
  r = R{};
  r.self_stage = Stage::definition;
  r.operation = static_cast<std::uint8_t>(d.work.definition_guards);
  if (!intermediate_endpoint04_charge(d, r, d.work.definition_guards,
                                      l.definition_guards,
                                      W::definition_capacity))
    return false;
  c.definition_evaluated |= std::uint32_t{1} << r.operation;
  return true;
}
auto detail::intermediate_endpoint04_current_cell(
    const BoardingIntermediateEndpoint04AdmissionContext& context, D& d, C& c,
    const L& l, R& r) -> S {
  if (context.owner() != &d || !context.request() ||
      context.parts() != &d.parts || context.data() != A::data(d.source) ||
      !A::valid(d.source) || !context.key() ||
      context.key()->candidate() != d.candidate ||
      context.key()->version() != 4 || context.key()->y() != d.slice.y ||
      !d.slice.complete || d.cells.size() != 1 || d.cells.capacity() != 1 ||
      &d.cells[0] != &c || !d.source_enrolled) {
    r = R{};
    r.self_stage = Stage::phase;
    intermediate_endpoint04_refuse(d, r, W::invalid_binding);
    return d.state;
  }
  BoardingRouteFootPhaseLimits phase_limits;
  phase_limits.graphs = l.phase.graphs;
  phase_limits.legs = l.phase.legs;
  phase_limits.bodies = l.phase.bodies;
  phase_limits.sectors = l.phase.sectors;
  phase_limits.timing = l.phase.timing;
  BoardingRouteFootPhaseRefusal original;
  ++d.work.phase_calls;
  const auto phase = boarding_route_foot_phase_cell(
      *context.request(), 0, 1, false, phase_limits, d.work.phase, c.phase,
      original);
  if (!intermediate_endpoint04_definition_charge(d, c, l, r)) return d.state;
  if (phase != BoardingRouteFootPhaseCellResult::accepted) {
    r.self_stage = Stage::phase;
    r.operation = 255;
    r.phase = original;
    intermediate_endpoint04_refuse(d, r, W::phase_prerequisite);
    if (phase == BoardingRouteFootPhaseCellResult::capacity)
      d.state = S::capacity;
    if (phase == BoardingRouteFootPhaseCellResult::unsupported) {
      d.state = S::unsupported;
      d.arithmetic_supported = false;
      if (!d.slice.complete) d.slice.arithmetic_supported = false;
    }
    d.stop_condition = W::phase_prerequisite;
    d.stop_stage = Stage::phase;
    return d.state;
  }
  d.arithmetic_supported = true;
  c.arithmetic_supported = c.kinematic_complete = true;
  // P01 is a charged revalidation of this actual fresh call, not the earlier
  // precompiler admission check or a caller-written accepted flag.
  if (!projection_charge(d, l, r)) return d.state;
  if (context.owner() != &d || !context.request() ||
      context.parts() != &d.parts || !A::valid(d.source) || !context.data() ||
      context.data() != A::data(d.source) || d.cells.size() != 1 ||
      d.cells.capacity() != 1 || &d.cells[0] != &c || !context.key() ||
      context.key()->version() != 4 ||
      context.key()->candidate() != d.candidate || !d.slice.complete ||
      context.key()->y() != d.slice.y || d.work.phase_calls != 1 ||
      !d.source_enrolled) {
    intermediate_endpoint04_refuse(d, r, W::projection_identity);
    return d.state;
  }
  if (!projection(d, c, *context.request(), *context.key(), l, r))
    return d.state;
  const BoardingIntermediateEndpoint04CurrentToken token(context, d, c,
                                                         *context.request());
  const auto pressure =
      intermediate_endpoint04_pressure_bridge(token, d, c, l, r);
  if (pressure != S::accepted || !c.nominal_support_complete) return d.state;
  c.complete = false;
  return intermediate_endpoint04_self_bridge(token, d, c, l, r);
}
[[gnu::noinline]] auto detail::intermediate_endpoint04_bounded(
    const OriginBoardingIntermediatePauseSupport& provider,
    BoardingIntermediateEndpoint04Candidate candidate, L limits) -> E {
  if (!boarding_route_foot_phase_environment())
    return std::unexpected(
        "intermediate endpoint04 unsupported floating point");
  if (candidate != BoardingIntermediateEndpoint04Candidate::
                       root_y_both_ankle_reach_roll_slice)
    return std::unexpected("intermediate endpoint04 invalid candidate");
  if (!valid_limits(limits))
    return std::unexpected("intermediate endpoint04 invalid limits");
  if (limits.output_bytes < fixed_output)
    return std::unexpected("intermediate endpoint04 output headers");
  E result(std::in_place, provider);
  auto& d = *result;
  d.candidate = candidate;
  d.slice.preflight_guards = 1;
  d.output_capacity_bytes = fixed_output;
  Request request;
  R reason;
  if (limits.output_bytes <
      boarding_intermediate_endpoint04_required_output_bytes()) {
    intermediate_endpoint04_refuse(d, reason, W::output_capacity);
    return result;
  }
  if (!intermediate_endpoint04_source_enroll(d, request, limits, reason))
    return result;
  if (!intermediate_endpoint04_construct(d, request, limits, reason))
    return result;
  const BoardingIntermediateEndpoint04ProgramKey key(d.slice.y, candidate);
  d.reporting_elapsed_seconds = 2;
  const BoardingIntermediateEndpoint04AdmissionContext context(d, request, key);
  std::uint32_t definition_prefix{};
  for (std::uint8_t row = 0; row < 3; ++row) {
    reason = R{};
    reason.self_stage = Stage::definition;
    reason.operation = row;
    if (!intermediate_endpoint04_charge(d, reason, d.work.definition_guards,
                                        limits.definition_guards,
                                        W::definition_capacity))
      return result;
    definition_prefix |= std::uint32_t{1} << row;
    // The D mask belongs to the actual Cell; accumulate this charged prefix
    // without creating a full temporary Cell before the output room check.
    const bool good =
        row == 0
            ? context.owner() == &d && context.parts() == &d.parts &&
                  context.request() == &request && context.key() == &key &&
                  context.data() == A::data(d.source) && A::valid(d.source) &&
                  A::valid(provider) && A::data(d.source) != nullptr &&
                  A::data(d.source) == A::data(provider)
        : row == 1
            ? boarding_route_foot_phase_environment()
            : request.seconds_per_parameter == 2 &&
                  request.port_reaction_fraction ==
                      std::array<double, 2>{.0625, .0625} &&
                  d.source_enrolled &&
                  d.source_evaluated ==
                      std::numeric_limits<std::uint64_t>::max() &&
                  d.version == 4 && key.version() == 4 &&
                  key.candidate() == d.candidate && key.y() == d.slice.y &&
                  d.slice.complete &&
                  intermediate_endpoint04_template_valid(request, true,
                                                         key.y()) &&
                  d.candidate == BoardingIntermediateEndpoint04Candidate::
                                     root_y_both_ankle_reach_roll_slice;
    if (!good) {
      intermediate_endpoint04_refuse(d, reason,
                                     row == 0   ? W::invalid_binding
                                     : row == 1 ? W::unsupported_arithmetic
                                                : W::phase_prerequisite);
      return result;
    }
  }
  try {
    d.cells.reserve(1);
  } catch (const std::bad_alloc&) {
    reason = R{};
    intermediate_endpoint04_refuse(d, reason, W::output_capacity);
    return result;
  }
  if (d.cells.capacity() != 1) {
    std::vector<C>().swap(d.cells);
    reason = R{};
    intermediate_endpoint04_refuse(d, reason, W::output_capacity);
    return result;
  }
  d.output_capacity_bytes = fixed_output + sizeof(C);
  d.cells.emplace_back();
  reset(d.cells.front());
  auto& cell = d.cells.front();
  cell.definition_evaluated = definition_prefix;
  const auto status =
      intermediate_endpoint04_current_cell(context, d, cell, limits, reason);
  cell.state = status;
  cell.arithmetic_supported = d.arithmetic_supported;
  d.kinematic_complete = cell.kinematic_complete;
  d.constant_state = cell.constant_state;
  d.projection_complete = cell.projection_complete;
  d.plane_identities = cell.plane_identities;
  d.nominal_equilibrium = cell.nominal_equilibrium;
  d.finite_contact_supported = cell.finite_contact_supported;
  d.nominal_load_supported = cell.nominal_load_supported;
  d.nominal_support_complete = cell.nominal_support_complete;
  d.self_body_complete = cell.self_body_complete;
  d.self_complete = cell.self_complete;
  d.complete = cell.complete && cell.self_complete &&
               cell.nominal_support_complete && status == S::accepted;
  d.state = d.complete ? S::accepted : status;
  return result;
}
auto assess_origin_boarding_intermediate_endpoint04(
    const OriginBoardingIntermediatePauseSupport& source,
    BoardingIntermediateEndpoint04Candidate candidate) -> E {
  return detail::intermediate_endpoint04_bounded(source, candidate);
}
} // namespace apsis_drift
