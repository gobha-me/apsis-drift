#include "origin_boarding_intermediate_pause_envelope_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace apsis_drift {
namespace {
using Scalar = BoardingFootSiteScalarBounds;
using Diagnostic = BoardingIntermediatePauseEnvelopeDiagnostic;
using Math = detail::BoardingIntermediatePauseEnvelopeMath;
using Limits = detail::BoardingIntermediatePauseEnvelopeLimits;
using MathLimits = detail::BoardingIntermediatePauseEnvelopeMathLimits;
using Condition = BoardingIntermediatePauseEnvelopeCondition;
using State = BoardingIntermediatePauseEnvelopeState;
using Access = detail::BoardingIntermediatePauseSupportAccess;
using Expected = std::expected<Diagnostic, std::string>;
using Child = BoardingIntermediatePauseSupportDiagnostic;
static_assert(sizeof(Expected) <=
              kBoardingIntermediatePauseEnvelopeOutputBytes);
static_assert(44440 + 2 * sizeof(Expected) + 4 * sizeof(Limits) + 2048 <=
              kBoardingIntermediatePauseEnvelopeScratchBytes);

// This tiny interval kernel belongs only to the registered fixed-Z gate.
auto down(double x) -> double {
  return std::nextafter(x, -std::numeric_limits<double>::infinity());
}
auto up(double x) -> double {
  return std::nextafter(x, std::numeric_limits<double>::infinity());
}
auto valid(Scalar x, double domain) -> bool {
  return x.supported && std::isfinite(x.lower) && std::isfinite(x.upper) &&
         x.lower <= x.upper && std::abs(x.lower) <= domain &&
         std::abs(x.upper) <= domain;
}
auto point(double x) -> Scalar {
  return {x, x, true};
}
auto add(Scalar a, Scalar b) -> Scalar {
  return {down(a.lower + b.lower), up(a.upper + b.upper),
          a.supported && b.supported};
}
auto multiply(Scalar a, Scalar b) -> Scalar {
  const std::array products{a.lower * b.lower, a.lower * b.upper,
                            a.upper * b.lower, a.upper * b.upper};
  return {down(*std::min_element(products.begin(), products.end())),
          up(*std::max_element(products.begin(), products.end())),
          a.supported && b.supported};
}
auto subtract(Scalar a, Scalar b) -> Scalar {
  return add(a, {-b.upper, -b.lower, b.supported});
}
template <typename Evidence>
auto refuse(Evidence& d, Condition why, State state,
            std::optional<std::size_t> side = {},
            std::optional<std::size_t> vertex = {},
            std::optional<std::size_t> operation = {}) -> bool {
  d.first_refusal =
      BoardingIntermediatePauseEnvelopeRefusal{why, side, vertex, operation};
  d.state = state;
  if (state == State::unsupported) d.arithmetic_supported = false;
  return false;
}
template <typename Evidence>
auto charge(Evidence& d, std::size_t& used, std::size_t cap, Condition why,
            std::optional<std::size_t> side = {},
            std::optional<std::size_t> vertex = {},
            std::optional<std::size_t> operation = {}) -> bool {
  if (used >= cap)
    return refuse(d, why, State::capacity, side, vertex, operation);
  ++used;
  return true;
}
template <typename Predicate>
auto definition(Diagnostic& d, Predicate predicate,
                Condition why = Condition::definition_refused,
                State state = State::prerequisite_refused,
                std::optional<std::size_t> side = {}) -> bool {
  const auto index = d.work.definition_guards;
  if (!charge(d, d.work.definition_guards,
              kBoardingIntermediatePauseEnvelopeDefinitionGuards,
              Condition::internal_guard_capacity, side, {}, index))
    return false;
  d.definition_evaluated[index] = true;
  if (!predicate()) return refuse(d, why, state, side, {}, index);
  return true;
}
template <typename Caps> auto valid_caps(const Caps& l) -> bool {
  return l.sole_records <= 2 && l.source_vertices <= 8 && l.minima <= 2 &&
         l.weighted_operations <= 3 && l.gap_operations <= 1;
}

// The genuine and raw paths both enter here with exactly one already charged
// sole record per side. Each source vertex is read only after its own charge.
template <typename Evidence, typename Vertex>
auto continuation(Evidence& d, const MathLimits& l, Vertex vertex) -> bool {
  for (std::size_t side = 0; side < 2; ++side) {
    Scalar maximum;
    for (std::size_t i = 0; i < 4; ++i) {
      if (!charge(d, d.work.source_vertices, l.source_vertices,
                  Condition::source_vertex_capacity, side, i))
        return false;
      const auto v = vertex(side, i);
      d.sites[side].source_vertex_evaluated[i] = true;
      if (!valid(v, 8))
        return refuse(d, Condition::unsupported, State::unsupported, side, i);
      if (i == 0)
        maximum = v;
      else {
        maximum.lower = std::max(maximum.lower, v.lower);
        maximum.upper = std::max(maximum.upper, v.upper);
      }
    }
    // An incomplete maximum never gets published as a supported operand.
    auto& site = d.sites[side];
    site.source_max_z = maximum;
    if (!charge(d, d.work.minima, l.minima, Condition::minimum_capacity, side))
      return false;
    site.upper_z = {std::min(site.sole_max_z.lower, maximum.lower),
                    std::min(site.sole_max_z.upper, maximum.upper), true};
    site.minimum_evaluated = true;
    if (!valid(site.upper_z, 32))
      return refuse(d, Condition::unsupported, State::unsupported, side);
  }
  std::array<Scalar, 2> products;
  for (std::size_t i = 0; i < 2; ++i) {
    if (!charge(d, d.work.weighted_operations, l.weighted_operations,
                Condition::weighted_capacity, {}, {}, i))
      return false;
    products[i] = multiply(point(i == 0 ? .25 : .75), d.sites[i].upper_z);
    d.weighted_evaluated[i] = true;
    if (!valid(products[i], 32))
      return refuse(d, Condition::unsupported, State::unsupported, {}, {}, i);
  }
  if (!charge(d, d.work.weighted_operations, l.weighted_operations,
              Condition::weighted_capacity, {}, {}, 2))
    return false;
  d.weighted_upper_z = add(products[0], products[1]);
  d.weighted_evaluated[2] = true;
  if (!valid(d.weighted_upper_z, 32))
    return refuse(d, Condition::unsupported, State::unsupported, {}, {}, 2);
  if (!charge(d, d.work.gap_operations, l.gap_operations,
              Condition::gap_capacity, {}, {}, 0))
    return false;
  d.gap_z = subtract(d.com_z, d.weighted_upper_z);
  d.gap_evaluated = true;
  return true;
}
template <typename Evidence> auto finish(Evidence& d) -> bool {
  if (!valid(d.gap_z, 32))
    return refuse(d, Condition::unsupported, State::unsupported);
  d.necessary_refuted = d.gap_z.lower > 0;
  d.envelope_assessed = true;
  d.state = d.necessary_refuted ? State::necessary_refuted : State::not_refuted;
  return true;
}
auto math_limits(const Limits& l) -> MathLimits {
  return {l.sole_records, l.source_vertices, l.minima, l.weighted_operations,
          l.gap_operations};
}
auto same_plane_expression(const BoardingRoutePhaseConstant& x, double plane)
    -> bool {
  return x.count == 1 && x.terms == std::array{plane, 0., 0.};
}

// Mandatory frame boundary: the 960-byte original catalog exists only AFTER
// the unchanged child graph has returned, and only after the first sole charge.
[[gnu::noinline]] auto post_child(Diagnostic& d, const Child& child,
                                  const Limits& l) -> void {
  if (!charge(d, d.work.sole_records, l.sole_records, Condition::sole_capacity,
              0))
    return;
  const auto parts = detail::boarding_route_foot_phase_parts();
  std::array<const BoardingBootSourcePartition*, 2> partitions{};
  for (std::size_t side = 0; side < 2; ++side) {
    if (side != 0 && !charge(d, d.work.sole_records, l.sole_records,
                             Condition::sole_capacity, side))
      return;
    const BoardingBootSourcePartition* partition = nullptr;
    const BoardingIntermediatePauseSourceIdentity* identity = nullptr;
    if (!definition(
            d,
            [&] {
              partition = Access::partition(d.source, side);
              const auto* summary = d.source.summary();
              if (!partition || !summary || !summary->complete) return false;
              identity = &summary->sources[side];
              return !identity->name.empty() &&
                     partition->faces[0].key == identity->keys[0] &&
                     partition->faces[1].key == identity->keys[1] &&
                     partition->plane_metres == identity->plane;
            },
            Condition::definition_refused, State::prerequisite_refused, side))
      return;
    partitions[side] = partition;
    auto& site = d.sites[side];
    site.name = identity->name;
    site.keys = identity->keys;
    site.plane = identity->plane;
    const auto index = side == 0 ? 5U : 11U;
    if (!definition(
            d,
            [&] {
              return parts[index].id ==
                     (side == 0 ? BoardingBodyPartId::port_boot
                                : BoardingBodyPartId::starboard_boot);
            },
            Condition::definition_refused, State::prerequisite_refused, side))
      return;
    const BoardingRoutePhaseBoxBinding* box = nullptr;
    if (!definition(
            d,
            [&] {
              box = std::get_if<BoardingRoutePhaseBoxBinding>(
                  &parts[index].reservation);
              return box != nullptr;
            },
            Condition::definition_refused, State::prerequisite_refused, side))
      return;
    if (!definition(
            d,
            [&] {
              return box->center ==
                     (side == 0
                          ? BoardingPlantedBodyPointId::port_boot_center
                          : BoardingPlantedBodyPointId::starboard_boot_center);
            },
            Condition::definition_refused, State::prerequisite_refused, side))
      return;
    if (!definition(
            d,
            [&] {
              return box->frame ==
                     (side == 0 ? BoardingRoutePhaseFrame::port_sole
                                : BoardingRoutePhaseFrame::starboard_sole);
            },
            Condition::definition_refused, State::prerequisite_refused, side))
      return;
    if (!definition(
            d,
            [&] {
              const auto h = box->half_size_metres;
              return std::isfinite(h.x) && std::isfinite(h.y) &&
                     std::isfinite(h.z) && h.x == .06 && h.y == .05 &&
                     h.z == .14;
            },
            Condition::definition_refused, State::prerequisite_refused, side))
      return;
    if (!definition(
            d,
            [&] {
              return child.request.feet[side].yaw_half == std::array{0., 0.};
            },
            Condition::definition_refused, State::prerequisite_refused, side))
      return;
    if (!definition(
            d,
            [&] {
              // The genuine graph enrolled targetsole -> bootcenter+.05 ->
              // fullhalfheight-.05 as an exact expression, never a tolerance.
              return std::isfinite(site.plane) &&
                     child.source_planes[side] == site.plane &&
                     same_plane_expression(
                         child.request.feet[side].sole[0].coordinates[1],
                         site.plane) &&
                     same_plane_expression(
                         child.request.feet[side].sole[1].coordinates[1],
                         site.plane);
            },
            Condition::definition_refused, State::prerequisite_refused, side))
      return;
    Scalar center;
    if (!definition(
            d,
            [&] {
              const auto z = child.boot_centers_xz[side][1];
              center = {z.lower, z.upper, true};
              return valid(center, 8);
            },
            Condition::unsupported, State::unsupported, side))
      return;
    // Real yaw-zero full BOX support, not support of an inflated AABB.
    site.sole_max_z = add(center, point(box->half_size_metres.z));
    site.sole_evaluated = true;
    if (!valid(site.sole_max_z, 32)) {
      refuse(d, Condition::unsupported, State::unsupported, side);
      return;
    }
  }
  const auto caps = math_limits(l);
  if (!continuation(d, caps, [&](std::size_t side, std::size_t i) {
        // Pointer is genuine and retained; no new source query or authority.
        return point(partitions[side]->perimeter_metres[i].z);
      }))
    return;
  if (!definition(
          d, [&] { return valid(d.gap_z, 32); }, Condition::unsupported,
          State::unsupported))
    return;
  finish(d);
}
} // namespace

auto detail::intermediate_pause_envelope_bounded(
    const OriginBoardingIntermediatePauseSupport& p, bool reverse, Limits l)
    -> Expected {
  if (!valid_caps(l) ||
      l.output_bytes > kBoardingIntermediatePauseEnvelopeOutputBytes)
    return std::unexpected("Envelope lowered caps required");
  if (l.output_bytes < sizeof(Expected))
    return std::unexpected("Envelope fixed output capacity refused");
  Expected result(std::in_place, p);
  auto& d = *result;
  d.reverse = reverse;
  d.output_bytes = sizeof(Expected);
  if (!definition(
          d, [&] { return Access::valid(p); }, Condition::invalid_binding))
    return result;
  if (!definition(
          d, [] { return boarding_route_foot_phase_environment(); },
          Condition::unsupported, State::unsupported))
    return result;
  d.arithmetic_supported = true;
  ++d.work.child_calls;
  const auto child_result =
      intermediate_pause_support_bounded(p, reverse, l.pause);
  if (!definition(
          d, [&] { return child_result.has_value(); },
          Condition::child_unavailable))
    return result;
  const auto& child = *child_result;
  d.work.pause = child.work;
  d.child_first_refusal = child.first_refusal;
  d.child_stop_condition = child.stop_condition;
  d.child_output_bytes = child.output_bytes;
  if (!definition(
          d, [&] { return child.version == 1; },
          Condition::child_prerequisite) ||
      !definition(
          d, [&] { return Access::data(child.source) == Access::data(p); },
          Condition::child_prerequisite) ||
      !definition(
          d, [&] { return child.work.phase_calls == 1; },
          Condition::child_prerequisite) ||
      !definition(
          d, [&] { return child.arithmetic_supported; }, Condition::unsupported,
          State::unsupported) ||
      !definition(
          d, [&] { return child.kinematic_complete; },
          Condition::child_prerequisite) ||
      !definition(
          d, [&] { return child.constant_state; },
          Condition::child_prerequisite) ||
      !definition(
          d, [&] { return child.projection_complete; },
          Condition::child_prerequisite) ||
      !definition(
          d,
          [&] {
            return child.stop_condition ==
                   BoardingIntermediatePauseCondition::none;
          },
          Condition::child_terminal) ||
      !definition(
          d,
          [&] {
            return !child.first_refusal ||
                   child.first_refusal->condition ==
                       BoardingIntermediatePauseCondition::sole_disk ||
                   child.first_refusal->condition ==
                       BoardingIntermediatePauseCondition::source_disk;
          },
          Condition::child_prerequisite) ||
      !definition(
          d,
          [&] {
            return child.duration_seconds == 2 &&
                   child.request.seconds_per_parameter == 2 &&
                   child.request.port_reaction_fraction ==
                       std::array{.25, .25} &&
                   child.reactions == std::array{.25, .75} &&
                   child.nominal_equilibrium &&
                   child.work.pressure_candidates == 2 &&
                   child.work.disk_edges == 16;
          },
          Condition::child_prerequisite) ||
      !definition(
          d,
          [&] {
            d.com_z = {child.com_xz[1].lower, child.com_xz[1].upper, true};
            return valid(d.com_z, 8);
          },
          Condition::unsupported, State::unsupported))
    return result;
  d.projection_available = true;
  post_child(d, child, l);
  return result;
}
auto detail::intermediate_pause_envelope_math(
    const BoardingIntermediatePauseEnvelopeMathInput& input, MathLimits l)
    -> Math {
  Math d;
  if (!valid_caps(l)) {
    refuse(d, Condition::invalid_limits, State::prerequisite_refused);
    return d;
  }
  if (!boarding_route_foot_phase_environment() || !valid(input.com_z, 8)) {
    refuse(d, Condition::unsupported, State::unsupported);
    return d;
  }
  d.arithmetic_supported = true;
  d.com_z = input.com_z;
  for (std::size_t side = 0; side < 2; ++side) {
    if (!charge(d, d.work.sole_records, l.sole_records,
                Condition::sole_capacity, side))
      return d;
    const auto sole = input.sole_max_z[side];
    d.sites[side].sole_evaluated = true;
    if (!valid(sole, 16)) {
      refuse(d, Condition::unsupported, State::unsupported, side);
      return d;
    }
    d.sites[side].sole_max_z = sole;
  }
  if (continuation(d, l, [&](std::size_t side, std::size_t i) {
        return input.source_vertices_z[side][i];
      }))
    finish(d);
  return d;
}
auto assess_origin_boarding_intermediate_pause_envelope(
    const OriginBoardingIntermediatePauseSupport& p, bool reverse) -> Expected {
  return detail::intermediate_pause_envelope_bounded(p, reverse);
}
} // namespace apsis_drift
