#include "origin_boarding_intermediate_pause_support02_internal.hpp"
#include "origin_boarding_route_intermediate_step02_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
namespace apsis_drift {
namespace {
using Access = detail::BoardingIntermediatePauseSupportAccess;
using Diagnostic = BoardingIntermediatePauseSupport02Diagnostic;
using Limits = detail::BoardingIntermediatePauseSupport02Limits;
using Math = detail::BoardingIntermediatePauseSupport02AllocationMath;
using MathLimits =
    detail::BoardingIntermediatePauseSupport02AllocationMathLimits;
using Scalar = BoardingFootSiteScalarBounds;
using Why = BoardingIntermediatePauseSupport02Condition;
using State = BoardingIntermediatePauseSupport02State;
using Expected = std::expected<Diagnostic, std::string>;
using Part = BoardingBodyPartId;
using PointId = BoardingPlantedBodyPointId;
constexpr std::size_t source_bound =
    32768 + 2 * sizeof(Expected) + 4096 +
    sizeof(BoardingRouteFootPhaseRequest) + 4 * sizeof(Limits) +
    sizeof(detail::BoardingRouteFootPhaseLimits) +
    sizeof(BoardingRouteFootPhaseRefusal) + 2048;
static_assert(sizeof(Expected) <= 3072);
static_assert(sizeof(Expected) <=
              kBoardingIntermediatePauseSupport02OutputBytes);
static_assert(source_bound <= kBoardingIntermediatePauseSupport02ScratchBytes);
auto coordinate(RigidVector3 v, std::size_t a) -> double {
  return a == 0 ? v.x : a == 1 ? v.y : v.z;
}
auto finite(double x) -> bool {
  return std::isfinite(x) && std::abs(x) <= 8;
}
auto valid(Scalar x, double domain) -> bool {
  return x.supported && std::isfinite(x.lower) && std::isfinite(x.upper) &&
         x.lower <= x.upper && std::abs(x.lower) <= domain &&
         std::abs(x.upper) <= domain;
}
auto point(double x) -> Scalar {
  return {x, x, true};
}
auto down(double x) -> double {
  return std::nextafter(x, -std::numeric_limits<double>::infinity());
}
auto up(double x) -> double {
  return std::nextafter(x, std::numeric_limits<double>::infinity());
}
auto add(Scalar a, Scalar b) -> Scalar {
  return {down(a.lower + b.lower), up(a.upper + b.upper),
          a.supported && b.supported};
}
auto subtract(Scalar a, Scalar b) -> Scalar {
  return add(a, {-b.upper, -b.lower, b.supported});
}
auto multiply(Scalar a, Scalar b) -> Scalar {
  const std::array products{a.lower * b.lower, a.lower * b.upper,
                            a.upper * b.lower, a.upper * b.upper};
  return {down(*std::min_element(products.begin(), products.end())),
          up(*std::max_element(products.begin(), products.end())),
          a.supported && b.supported};
}
// Registered DIVIDE: the positive denominator is the exact dyadic point15/16.
// Two outward endpoint divisions; no reciprocal/root/hidden extra candidate.
auto divide_star(Scalar a) -> Scalar {
  return {down(a.lower / .9375), up(a.upper / .9375), a.supported};
}
template <typename E>
auto fail(E& d, Why why, std::optional<std::size_t> side = {},
          std::optional<std::size_t> axis = {},
          std::optional<std::size_t> vertex = {},
          std::optional<std::size_t> operation = {}) -> bool {
  d.stop_condition = why;
  if (!d.first_refusal) {
    BoardingIntermediatePauseSupport02Refusal r;
    r.condition = why;
    r.side = side;
    r.axis = axis;
    r.vertex = vertex;
    r.operation = operation;
    d.first_refusal = r;
  }
  const bool cap =
      why == Why::projection_capacity || why == Why::definition_capacity ||
      why == Why::sole_extrema_capacity ||
      why == Why::source_coordinate_capacity ||
      why == Why::intersection_capacity || why == Why::midpoint_capacity ||
      why == Why::allocation_capacity || why == Why::pressure_capacity ||
      why == Why::edge_capacity;
  d.state = cap                                  ? State::capacity
            : why == Why::unsupported_arithmetic ? State::unsupported
            : why == Why::empty_intersection     ? State::unresolved
                                                 : State::prerequisite_refused;
  if (why == Why::unsupported_arithmetic) d.arithmetic_supported = false;
  return false;
}
template <typename E>
auto charge(E& d, std::size_t& used, std::size_t cap, Why why,
            std::optional<std::size_t> axis = {},
            std::optional<std::size_t> vertex = {},
            std::optional<std::size_t> operation = {}) -> bool {
  if (used >= cap) return fail(d, why, {}, axis, vertex, operation);
  ++used;
  return true;
}
auto projection_charge(Diagnostic& d, const Limits& l) -> bool {
  return charge(d, d.work.projection_guards, l.projection_guards,
                Why::projection_capacity);
}
template <typename E, typename Predicate>
auto guard(E& d, Predicate predicate, Why why = Why::projection_identity)
    -> bool {
  if constexpr (std::is_same_v<E, Diagnostic>) {
    if (!detail::intermediate_pause_support02_definition_charge(d))
      return false;
  } else {
    if (d.work.validation_guards >= d.validation_evaluated.size())
      return fail(d, Why::definition_capacity);
    d.validation_evaluated[d.work.validation_guards++] = true;
  }
  if (!predicate()) return fail(d, why);
  return true;
}
auto projected_carrier(const BoardingPlantedBodyPointEvidence& p, Diagnostic& d,
                       const Limits& l, std::size_t index) -> bool {
  for (std::size_t kind = 0; kind < 3; ++kind) {
    const auto& b = kind == 0   ? p.value
                    : kind == 1 ? p.derivatives.velocity
                                : p.derivatives.acceleration;
    for (std::size_t a = 0; a < 3; ++a) {
      if (!projection_charge(d, l)) return false;
      const auto low = coordinate(b.lower, a);
      if (!finite(low) || (kind != 0 && low > 0))
        return fail(d, Why::projection_identity);
      if (!projection_charge(d, l)) return false;
      const auto high = coordinate(b.upper, a);
      if (!finite(high) || low > high || (kind != 0 && high < 0))
        return fail(d, Why::projection_identity);
    }
  }
  d.projected_carrier_complete[index] = true;
  return true;
}
auto same_constant(const BoardingRoutePhaseConstant& a,
                   const BoardingRoutePhaseConstant& b) -> bool {
  return a.count == b.count && a.terms == b.terms;
}
[[gnu::noinline]] auto projection(
    const OriginBoardingIntermediatePauseSupport& provider,
    const BoardingRouteFootPhaseCell& c, Diagnostic& d, const Limits& l,
    const BoardingRouteFootPhaseRequest& r) -> bool {
  if (!projection_charge(d, l)) return false;
  const auto* data = Access::data(provider);
  if (!Access::valid(provider) || !data) return fail(d, Why::invalid_binding);
  for (const auto* flag :
       std::array{&c.complete, &c.arithmetic_supported, &c.nominal_links,
                  &c.target_sole_identities, &c.joint_sectors,
                  &c.derivative_domains, &c.timing_complete}) {
    if (!projection_charge(d, l)) return false;
    if (!*flag) return fail(d, Why::phase_prerequisite);
  }
  if (!projection_charge(d, l)) return false;
  if (c.first != 0) return fail(d, Why::projection_identity);
  if (!projection_charge(d, l)) return false;
  if (c.last != 1) return fail(d, Why::projection_identity);
  for (std::size_t a = 0; a < 3; ++a) {
    if (!projection_charge(d, l)) return false;
    if (!same_constant(r.root[0].coordinates[a], r.root[1].coordinates[a]))
      return fail(d, Why::projection_identity);
  }
  for (std::size_t side = 0; side < 2; ++side) {
    for (std::size_t a = 0; a < 3; ++a) {
      if (!projection_charge(d, l)) return false;
      if (!same_constant(r.feet[side].sole[0].coordinates[a],
                         r.feet[side].sole[1].coordinates[a]))
        return fail(d, Why::projection_identity, side);
    }
    if (!projection_charge(d, l)) return false;
    if (r.feet[side].yaw_half != std::array<double, 2>{0, 0})
      return fail(d, Why::projection_identity, side);
    if (!projection_charge(d, l)) return false;
    if (r.feet[side].swing_height_metres != 0)
      return fail(d, Why::projection_identity, side);
  }
  for (std::size_t check = 0; check < 4; ++check) {
    if (!projection_charge(d, l)) return false;
    const auto equal =
        check == 0   ? r.root_yaw_half[0] == r.root_yaw_half[1]
        : check == 1 ? r.torso_lean_half[0] == r.torso_lean_half[1]
        : check == 2
            ? r.port_reaction_fraction == std::array<double, 2>{.0625, .0625}
            : r.seconds_per_parameter == 2;
    if (!equal) return fail(d, Why::projection_identity);
  }
  if (!projection_charge(d, l)) return false;
  const auto parts = detail::boarding_route_foot_phase_parts();
  for (std::size_t p = 0; p < parts.size(); ++p) {
    if (p != 0 && !projection_charge(d, l)) return false;
    if (static_cast<std::size_t>(parts[p].id) != p)
      return fail(d, Why::projection_identity);
  }
  for (std::size_t side = 0; side < 2; ++side) {
    const auto index = static_cast<std::size_t>(
        side == 0 ? Part::port_boot : Part::starboard_boot);
    if (!projection_charge(d, l)) return false;
    const auto* b =
        std::get_if<BoardingRoutePhaseBoxBinding>(&parts[index].reservation);
    if (!b) return fail(d, Why::projection_identity, side);
    if (!projection_charge(d, l)) return false;
    if (b->center != (side == 0 ? PointId::port_boot_center
                                : PointId::starboard_boot_center))
      return fail(d, Why::projection_identity, side);
    if (!projection_charge(d, l)) return false;
    if (b->half_size_metres != RigidVector3{.06, .05, .14})
      return fail(d, Why::projection_identity, side);
    if (!projection_charge(d, l)) return false;
    if (b->frame != (side == 0 ? BoardingRoutePhaseFrame::port_sole
                               : BoardingRoutePhaseFrame::starboard_sole))
      return fail(d, Why::projection_identity, side);
    if (!projection_charge(d, l)) return false;
    const auto* source = Access::partition(provider, side);
    const auto& y = r.feet[side].sole[0].coordinates[1];
    if (!source || y.count != 1 ||
        y.terms != std::array<double, 3>{source->plane_metres, 0, 0})
      return fail(d, Why::sole_plane, side);
    d.source_planes[side] = source->plane_metres;
    d.sites[side].plane_identity = true;
  }
  if (!projection_charge(d, l)) return false;
  if (c.port_reaction_fraction.lower != .0625 ||
      c.port_reaction_fraction.upper != .0625)
    return fail(d, Why::projection_identity);
  std::size_t carrier_index{};
  for (const auto* carrier : std::array{
           &c.center_of_mass,
           &c.points[static_cast<std::size_t>(PointId::port_boot_center)],
           &c.points[static_cast<std::size_t>(PointId::starboard_boot_center)],
           &c.points[static_cast<std::size_t>(PointId::port_ankle)],
           &c.points[static_cast<std::size_t>(PointId::starboard_ankle)]})
    if (!projected_carrier(*carrier, d, l, carrier_index++)) return false;
  for (std::size_t side = 0; side < 2; ++side)
    for (const auto& column : c.frames[side + 2].columns)
      if (!projected_carrier(column, d, l, carrier_index++)) return false;
  d.constant_state = d.projection_complete = true;
  return true;
}
auto minimum(Scalar a, Scalar b) -> Scalar {
  return {std::min(a.lower, b.lower), std::min(a.upper, b.upper),
          a.supported && b.supported};
}
auto maximum(Scalar a, Scalar b) -> Scalar {
  return {std::max(a.lower, b.lower), std::max(a.upper, b.upper),
          a.supported && b.supported};
}
template <typename E, typename Caps, typename Center, typename Vertex>
[[gnu::noinline]] auto allocation(E& d, const Caps& l, Center center,
                                  Vertex vertex) -> bool {
  for (std::size_t a = 0; a < 2; ++a) {
    const auto half = point(a == 0 ? .06 : .14);
    for (std::size_t which = 0; which < 2; ++which) {
      if (!charge(d, d.work.sole_extrema, l.sole_extrema,
                  Why::sole_extrema_capacity, a, {}, which))
        return false;
      d.sole_extrema_evaluated[a][which] = true;
      (which == 0 ? d.port_sole_lower_xz[a] : d.port_sole_upper_xz[a]) =
          which == 0 ? subtract(center(a), half) : add(center(a), half);
    }
  }
  if (!guard(
          d,
          [&] {
            for (std::size_t a = 0; a < 2; ++a) {
              if (!valid(d.port_sole_lower_xz[a], 16) ||
                  !valid(d.port_sole_upper_xz[a], 16))
                return false;
            }
            return true;
          },
          Why::unsupported_arithmetic))
    return false;
  std::array<std::array<Scalar, 2>, 4> vertices;
  std::array<Scalar, 2> lows, highs;
  for (std::size_t v = 0; v < 4; ++v)
    for (std::size_t a = 0; a < 2; ++a) {
      if (!charge(d, d.work.source_coordinates, l.source_coordinates,
                  Why::source_coordinate_capacity, a, v))
        return false;
      d.source_coordinate_evaluated[v][a] = true;
      const auto value = vertex(v, a);
      vertices[v][a] = value;
      if (!valid(value, 8))
        return fail(d, Why::unsupported_arithmetic, {}, a, v);
      lows[a] = v == 0 ? value : minimum(lows[a], value);
      highs[a] = v == 0 ? value : maximum(highs[a], value);
    }
  d.port_source_lower_xz = lows;
  d.port_source_upper_xz = highs;
  if (!guard(
          d,
          [&] {
            return valid(lows[0], 8) && valid(lows[1], 8) &&
                   valid(highs[0], 8) && valid(highs[1], 8);
          },
          Why::unsupported_arithmetic))
    return false;
  if constexpr (std::is_same_v<E, Diagnostic>) {
    auto equal = [](Scalar a, Scalar b) {
      return a.lower == a.upper && b.lower == b.upper && a.lower == b.lower;
    };
    if (!guard(
            d,
            [&] {
              return equal(vertices[0][0], vertices[3][0]) &&
                     equal(vertices[1][0], vertices[2][0]) &&
                     equal(vertices[0][1], vertices[1][1]) &&
                     equal(vertices[2][1], vertices[3][1]);
            },
            Why::source_rectangle))
      return false;
    if (!guard(
            d,
            [&] {
              return vertices[1][0].upper < vertices[0][0].lower &&
                     vertices[0][1].upper < vertices[2][1].lower;
            },
            Why::source_rectangle))
      return false;
  }
  if (!guard(
          d,
          [&] {
            if constexpr (std::is_same_v<E, Diagnostic>)
              return d.reactions[0] == .0625 && d.reactions[0] > .010;
            else
              return .0625 > .010;
          },
          Why::denominator))
    return false;
  if (!guard(
          d,
          [&] {
            if constexpr (std::is_same_v<E, Diagnostic>)
              return d.reactions[1] == .9375 && d.reactions[1] > .010;
            else
              return .9375 > .010;
          },
          Why::denominator))
    return false;
  for (std::size_t a = 0; a < 2; ++a) {
    for (std::size_t which = 0; which < 2; ++which) {
      if (!charge(d, d.work.intersection_operations, l.intersection_operations,
                  Why::intersection_capacity, a, {}, which))
        return false;
      d.intersection_evaluated[a][which] = true;
      (which == 0 ? d.intersection_lower_xz[a] : d.intersection_upper_xz[a]) =
          which == 0 ? maximum(d.port_sole_lower_xz[a], lows[a])
                     : minimum(d.port_sole_upper_xz[a], highs[a]);
    }
    if (!guard(
            d,
            [&] {
              return valid(d.intersection_lower_xz[a], 32) &&
                     valid(d.intersection_upper_xz[a], 32) &&
                     d.intersection_lower_xz[a].upper <
                         d.intersection_upper_xz[a].lower;
            },
            Why::empty_intersection))
      return false;
  }
  d.intersection_complete = true;
  if (!charge(d, d.work.pressure_candidates, l.pressure_candidates,
              Why::pressure_capacity, {}, {}, 0))
    return false;
  for (std::size_t a = 0; a < 2; ++a) {
    Scalar sum;
    if (!charge(d, d.work.midpoint_operations, l.midpoint_operations,
                Why::midpoint_capacity, a, {}, 0))
      return false;
    d.midpoint_evaluated[a][0] = true;
    sum = add(d.intersection_lower_xz[a], d.intersection_upper_xz[a]);
    if (!charge(d, d.work.midpoint_operations, l.midpoint_operations,
                Why::midpoint_capacity, a, {}, 1))
      return false;
    d.midpoint_evaluated[a][1] = true;
    d.pressure_xz[0][a] = multiply(point(.5), sum);
    if (!guard(
            d, [&] { return valid(d.pressure_xz[0][a], 32); },
            Why::unsupported_arithmetic))
      return false;
  }
  d.pressure_evaluated[0] = true;
  if (!charge(d, d.work.pressure_candidates, l.pressure_candidates,
              Why::pressure_capacity, {}, {}, 1))
    return false;
  for (std::size_t a = 0; a < 2; ++a) {
    Scalar weighted, residual;
    if (!charge(d, d.work.allocation_operations, l.allocation_operations,
                Why::allocation_capacity, a, {}, 0))
      return false;
    d.allocation_evaluated[a][0] = true;
    weighted = multiply(point(.0625), d.pressure_xz[0][a]);
    if (!charge(d, d.work.allocation_operations, l.allocation_operations,
                Why::allocation_capacity, a, {}, 1))
      return false;
    d.allocation_evaluated[a][1] = true;
    residual = subtract(d.com_xz[a], weighted);
    if (!guard(
            d, [&] { return valid(residual, 32); },
            Why::unsupported_arithmetic))
      return false;
    if (!charge(d, d.work.allocation_operations, l.allocation_operations,
                Why::allocation_capacity, a, {}, 2))
      return false;
    d.allocation_evaluated[a][2] = true;
    d.pressure_xz[1][a] = divide_star(residual);
    if (!guard(
            d, [&] { return valid(d.pressure_xz[1][a], 32); },
            Why::unsupported_arithmetic))
      return false;
  }
  d.pressure_evaluated[1] = true;
  // Dyadic coefficients are the registered symbolic force identity. Moment
  // uses this SAME stored Pport in the residual and SAME star coefficient as
  // denominator; no interval overlap/equality is treated as an identity proof.
  if (!guard(d, [] { return .0625 + .9375 == 1; }, Why::symbolic_equilibrium))
    return false;
  if (!guard(
          d,
          [&] {
            return d.pressure_evaluated[0] && d.pressure_evaluated[1] &&
                   d.work.midpoint_operations == 4 &&
                   d.work.allocation_operations == 6;
          },
          Why::symbolic_equilibrium))
    return false;
  d.nominal_equilibrium = true;
  return true;
}
auto valid_limits(const Limits& l) -> bool {
  return l.phase.graphs <= 1 && l.phase.legs <= 2 && l.phase.bodies <= 1 &&
         l.phase.sectors <= 3 && l.phase.timing <= 6 &&
         l.projection_guards <= 256 && l.pressure_candidates <= 2 &&
         l.disk_edges <= 16 && l.output_bytes <= 4096 && l.sole_extrema <= 4 &&
         l.source_coordinates <= 8 && l.intersection_operations <= 4 &&
         l.midpoint_operations <= 4 && l.allocation_operations <= 6;
}
auto valid_limits(const MathLimits& l) -> bool {
  return l.sole_extrema <= 4 && l.source_coordinates <= 8 &&
         l.intersection_operations <= 4 && l.midpoint_operations <= 4 &&
         l.allocation_operations <= 6 && l.pressure_candidates <= 2;
}
} // namespace

auto detail::intermediate_pause_support02_definition_charge(Diagnostic& d)
    -> bool {
  if (d.work.definition_guards >= d.definition_evaluated.size())
    return fail(d, Why::definition_capacity);
  d.definition_evaluated[d.work.definition_guards++] = true;
  return true;
}
auto detail::intermediate_pause_support02_refuse(
    Diagnostic& d, Why why, std::optional<std::size_t> side,
    std::optional<std::size_t> edge, bool source_edge) -> void {
  if (why != Why::sole_disk && why != Why::source_disk) {
    fail(d, why, side);
  } else if (!d.first_refusal) {
    BoardingIntermediatePauseSupport02Refusal r;
    r.condition = why;
    r.side = side;
    d.first_refusal = r;
  }
  if (d.first_refusal && d.first_refusal->condition == why &&
      d.first_refusal->side == side && !d.first_refusal->edge) {
    d.first_refusal->edge = edge;
    d.first_refusal->source_edge = source_edge;
  }
}
auto detail::intermediate_pause_support02_request()
    -> std::optional<BoardingRouteFootPhaseRequest> {
  auto request = boarding_route_intermediate_step02_controls(4);
  if (request) {
    request->port_reaction_fraction = {.0625, .0625};
  }
  return request;
}
[[gnu::noinline]] auto detail::intermediate_pause_support02_allocate(
    Diagnostic& d, const Limits& l) -> bool {
  const BoardingBootSourcePartition* port{};
  for (std::size_t side = 0; side < 2; ++side) {
    if (!guard(
            d,
            [&] {
              const auto* partition = Access::partition(d.source, side);
              const auto* summary = d.source.summary();
              if (!partition || !summary || !summary->complete) return false;
              const auto& identity = summary->sources[side];
              if (identity.keys[0] != partition->faces[0].key ||
                  identity.keys[1] != partition->faces[1].key ||
                  identity.plane != partition->plane_metres ||
                  identity.plane != d.source_planes[side] ||
                  identity.name.empty())
                return false;
              d.sources[side] = {identity.name, identity.keys, identity.plane};
              if (side == 0) port = partition;
              return true;
            },
            Why::invalid_binding))
      return false;
  }
  return allocation(
      d, l, [&](std::size_t a) { return d.boot_centers_xz[0][a]; },
      [&](std::size_t v, std::size_t a) {
        const auto p = port->perimeter_metres[v];
        return point(a == 0 ? p.x : p.z);
      });
}
[[gnu::noinline]] auto detail::intermediate_pause_support02_allocation_math(
    const BoardingIntermediatePauseSupport02AllocationMathInput& input,
    MathLimits l) -> Math {
  Math d;
  if (!valid_limits(l)) {
    fail(d, Why::invalid_limits);
    return d;
  }
  if (!boarding_route_foot_phase_environment()) {
    fail(d, Why::unsupported_arithmetic);
    return d;
  }
  d.arithmetic_supported = true;
  for (std::size_t carrier = 0; carrier < 2; ++carrier)
    for (std::size_t a = 0; a < 2; ++a) {
      if (!guard(
              d,
              [&] {
                const auto value = carrier == 0 ? input.com_xz[a]
                                                : input.port_boot_center_xz[a];
                if (!valid(value, 8)) return false;
                (carrier == 0 ? d.com_xz[a] : d.port_boot_center_xz[a]) = value;
                return true;
              },
              Why::unsupported_arithmetic))
        return d;
    }
  if (allocation(
          d, l, [&](std::size_t a) { return d.port_boot_center_xz[a]; },
          [&](std::size_t v, std::size_t a) {
            return input.port_source_vertices_xz[v][a];
          })) {
    d.allocation_complete = d.complete = true;
    d.state = State::supported;
  }
  return d;
}
[[gnu::noinline]] auto detail::intermediate_pause_support02_bounded(
    const OriginBoardingIntermediatePauseSupport& p, bool reverse, Limits l)
    -> Expected {
  if (!valid_limits(l))
    return std::unexpected("Support02 lowered caps required");
  if (l.output_bytes < sizeof(Expected))
    return std::unexpected("Support02 fixed output capacity refused");
  Expected result(std::in_place, p);
  auto& d = *result;
  d.reverse = reverse;
  d.output_bytes = sizeof(Expected);
  if (!guard(d, [&] { return Access::valid(p); }, Why::invalid_binding))
    return result;
  if (!guard(
          d, [] { return boarding_route_foot_phase_environment(); },
          Why::unsupported_arithmetic))
    return result;
  d.arithmetic_supported = true;
  std::optional<BoardingRouteFootPhaseRequest> request;
  if (!guard(
          d,
          [&] {
            request = intermediate_pause_support02_request();
            return request && request->seconds_per_parameter == 2 &&
                   request->port_reaction_fraction ==
                       std::array<double, 2>{.0625, .0625};
          },
          Why::phase_prerequisite))
    return result;
  BoardingRouteFootPhaseLimits phase_caps;
  phase_caps.graphs = l.phase.graphs;
  phase_caps.legs = l.phase.legs;
  phase_caps.bodies = l.phase.bodies;
  phase_caps.sectors = l.phase.sectors;
  phase_caps.timing = l.phase.timing;
  BoardingRouteFootPhaseCell cell;
  BoardingRouteFootPhaseRefusal reason;
  ++d.work.phase_calls;
  const auto state = boarding_route_foot_phase_cell(
      *request, 0, 1, reverse, phase_caps, d.work.phase, cell, reason);
  if (!guard(
          d,
          [&] { return state == BoardingRouteFootPhaseCellResult::accepted; },
          Why::phase_prerequisite)) {
    d.first_refusal->phase = reason;
    d.arithmetic_supported =
        state != BoardingRouteFootPhaseCellResult::unsupported;
    if (!d.arithmetic_supported) d.state = State::unsupported;
    return result;
  }
  d.kinematic_complete = true;
  if (!projection(p, cell, d, l, *request)) return result;
  const BoardingIntermediatePauseSupport02ProjectionToken token(d.source, cell,
                                                                *request, d);
  intermediate_pause_support02_pressure_bridge(token, l, d);
  return result;
}
auto assess_origin_boarding_intermediate_pause_support02(
    const OriginBoardingIntermediatePauseSupport& p, bool reverse) -> Expected {
  return detail::intermediate_pause_support02_bounded(p, reverse);
}
} // namespace apsis_drift
