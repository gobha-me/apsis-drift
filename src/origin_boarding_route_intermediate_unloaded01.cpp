#include "apsis_drift/origin_boarding_route_intermediate_unloaded01.hpp"
#include "origin_boarding_route_intermediate_step02_internal.hpp"
#include "origin_boarding_route_intermediate_unloaded01_internal.hpp"
#include "origin_lower_cockpit_contact_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
namespace apsis_drift {
namespace {
using D = BoardingRouteIntermediateUnloaded01Diagnostic;
using C = BoardingRouteIntermediateUnloaded01Cell;
using R = BoardingRouteIntermediateUnloaded01Refusal;
using W = BoardingRouteIntermediateUnloaded01Condition;
using S = BoardingRouteIntermediateUnloaded01State;
using L = detail::BoardingRouteIntermediateUnloaded01Limits;
using Request = BoardingRouteFootPhaseRequest;
using E = std::expected<D, std::string>;
using A = detail::BoardingIntermediatePauseSupportAccess;
using P = BoardingPlantedBodyPointId;
using B = BoardingFootSiteScalarBounds;
struct Pending {
  double first{}, last{};
  std::size_t depth{};
};
constexpr std::array<double, 2> joins{.125, .25};
constexpr std::size_t fixed_output = 2 * sizeof(E);
static_assert(sizeof(E) <=
              kBoardingRouteIntermediateUnloaded01MaximumExpectedBytes);
static_assert(sizeof(C) <=
              kBoardingRouteIntermediateUnloaded01MaximumCellBytes);
constexpr std::size_t graph_live =
    32768 + 2 * sizeof(E) + 4096 +
    (sizeof(C) - sizeof(BoardingRouteFootPhaseCell)) + 4 * sizeof(L) + 2048 +
    11 * sizeof(Pending) + sizeof(detail::BoardingRouteFootPhaseLimits) +
    sizeof(BoardingRouteFootPhaseRefusal) + sizeof(std::optional<Request>) +
    sizeof(Request) + sizeof(R) +
    sizeof(detail::BoardingRouteIntermediateUnloaded01AdmissionContext) +
    2 * sizeof(Pending);
static_assert(graph_live <=
              kBoardingRouteIntermediateUnloaded01MaximumScratchBytes);
auto capacity(W w) -> bool {
  return w == W::output_capacity || w == W::source_guard_capacity ||
         w == W::projection_capacity || w == W::definition_capacity ||
         w == W::pressure_capacity || w == W::edge_capacity ||
         w == W::event_guard_capacity || w == W::upper_edge_capacity ||
         w == W::source_coordinate_capacity ||
         w == W::endpoint_operation_capacity || w == W::node_capacity ||
         w == W::leaf_capacity;
}
auto fail(D& d, R& r, W w) -> bool {
  if (r.condition == W::none) {
    r.condition = r.predicate_condition = w;
  }
  if (capacity(w) || w == W::unsupported_arithmetic) {
    d.stop_condition = w;
    d.state = capacity(w) ? S::capacity : S::unsupported;
    if (w == W::unsupported_arithmetic) d.arithmetic_supported = false;
  } else
    d.state = S::unresolved;
  return false;
}
auto charge(D& d, R& r, std::size_t& n, std::size_t cap, W w) -> bool {
  if (n >= cap) return fail(d, r, w);
  ++n;
  return true;
}
template <class F> auto guard(D& d, C& c, const L& l, R& r, F f, W w) -> bool {
  if (!detail::boarding_route_intermediate_unloaded01_definition_charge(d, c, l,
                                                                        r))
    return false;
  return f() || fail(d, r, w);
}
auto same(const BoardingRoutePhaseConstant& a,
          const BoardingRoutePhaseConstant& b) -> bool {
  return a.count == b.count && a.terms == b.terms;
}
auto projection_charge(D& d, const L& l, R& r) -> bool {
  return charge(d, r, d.work.projection_guards, l.projection_guards,
                W::projection_capacity);
}
auto coord(RigidVector3 v, std::size_t a) -> double {
  return a == 0 ? v.x : a == 1 ? v.y : v.z;
}
auto carrier(const BoardingPlantedBodyPointEvidence& p, std::size_t index, D& d,
             C& c, const L& l, R& r) -> bool {
  for (std::size_t kind = 0; kind < 3; ++kind) {
    const auto& b = kind == 0   ? p.value
                    : kind == 1 ? p.derivatives.velocity
                                : p.derivatives.acceleration;
    for (std::size_t axis = 0; axis < 3; ++axis) {
      if (!projection_charge(d, l, r)) return false;
      const auto lo = coord(b.lower, axis);
      const bool held = index != 0 && index != 1 && index != 3;
      if (!std::isfinite(lo) || (kind == 0 && std::abs(lo) > 8) ||
          (held && kind != 0 && lo > 0))
        return fail(d, r, W::projection_identity);
      if (!projection_charge(d, l, r)) return false;
      const auto hi = coord(b.upper, axis);
      if (!std::isfinite(hi) || lo > hi || (kind == 0 && std::abs(hi) > 8) ||
          (held && kind != 0 && hi < 0))
        return fail(d, r, W::projection_identity);
    }
  }
  c.projected_carrier_complete[index] = true;
  return true;
}
[[gnu::noinline]] auto canonical_equal(const Request& r, std::size_t phase)
    -> bool {
  const auto expected =
      detail::boarding_route_intermediate_unloaded01_controls(phase);
  if (!expected) return false;
  for (std::size_t a = 0; a < 3; ++a) {
    if (!same(r.root[0].coordinates[a], expected->root[0].coordinates[a]) ||
        !same(r.root[1].coordinates[a], expected->root[1].coordinates[a]))
      return false;
    for (std::size_t side = 0; side < 2; ++side)
      for (std::size_t end = 0; end < 2; ++end)
        if (!same(r.feet[side].sole[end].coordinates[a],
                  expected->feet[side].sole[end].coordinates[a]))
          return false;
  }
  return r.root_yaw_half == expected->root_yaw_half &&
         r.torso_lean_half == expected->torso_lean_half &&
         r.port_reaction_fraction == expected->port_reaction_fraction &&
         r.seconds_per_parameter == expected->seconds_per_parameter &&
         r.feet[0].yaw_half == expected->feet[0].yaw_half &&
         r.feet[1].yaw_half == expected->feet[1].yaw_half &&
         r.feet[0].swing_height_metres ==
             expected->feet[0].swing_height_metres &&
         r.feet[1].swing_height_metres == expected->feet[1].swing_height_metres;
}
[[gnu::noinline]] auto projection(D& d, C& c, const Request& r, const L& l,
                                  R& reason) -> bool {
  const auto& p = c.phase;
  std::optional<Request> canonical;
  if (!projection_charge(d, l, reason)) return false;
  if (!A::valid(d.source) || !A::data(d.source))
    return fail(d, reason, W::invalid_binding);
  for (const auto* flag :
       std::array{&p.complete, &p.arithmetic_supported, &p.nominal_links,
                  &p.target_sole_identities, &p.joint_sectors,
                  &p.derivative_domains, &p.timing_complete}) {
    if (!projection_charge(d, l, reason)) return false;
    if (!*flag) return fail(d, reason, W::phase_prerequisite);
  }
  if (!projection_charge(d, l, reason)) return false;
  if (p.first != detail::boarding_route_intermediate_unloaded01_local(
                     c.phase_index, c.global_first))
    return fail(d, reason, W::projection_identity);
  if (!projection_charge(d, l, reason)) return false;
  if (p.last != detail::boarding_route_intermediate_unloaded01_local(
                    c.phase_index, c.global_last))
    return fail(d, reason, W::projection_identity);
  // Compare each immutable packet tuple only after its projection charge.
  // The temporary canonical factory lives after the graph has returned.
  for (std::size_t a = 0; a < 3; ++a) {
    if (!projection_charge(d, l, reason)) return false;
    if (!canonical) {
      canonical = detail::boarding_route_intermediate_unloaded01_controls(
          c.phase_index);
      if (!canonical) return fail(d, reason, W::projection_identity);
    }
    if (!same(r.root[0].coordinates[a], canonical->root[0].coordinates[a]) ||
        !same(r.root[1].coordinates[a], canonical->root[1].coordinates[a]))
      return fail(d, reason, W::projection_identity);
  }
  for (std::size_t side = 0; side < 2; ++side)
    for (std::size_t a = 0; a < 3; ++a) {
      if (!projection_charge(d, l, reason)) return false;
      if (!same(r.feet[side].sole[0].coordinates[a],
                canonical->feet[side].sole[0].coordinates[a]) ||
          !same(r.feet[side].sole[1].coordinates[a],
                canonical->feet[side].sole[1].coordinates[a]))
        return fail(d, reason, W::projection_identity);
    }
  for (std::size_t side = 0; side < 2; ++side) {
    if (!projection_charge(d, l, reason)) return false;
    if (r.feet[side].yaw_half != std::array<double, 2>{0, 0})
      return fail(d, reason, W::projection_identity);
  }
  for (std::size_t side = 0; side < 2; ++side) {
    if (!projection_charge(d, l, reason)) return false;
    if (r.feet[side].swing_height_metres !=
        (side == 0 && c.phase_index == 1 ? .010 : 0))
      return fail(d, reason, W::projection_identity);
  }
  for (std::size_t k = 0; k < 4; ++k) {
    if (!projection_charge(d, l, reason)) return false;
    bool good =
        k == 0 ? r.root_yaw_half ==
                     (c.phase_index == 0   ? std::array<double, 2>{.25, .25}
                      : c.phase_index == 1 ? std::array<double, 2>{.25, .125}
                                           : std::array<double, 2>{.125, .125})
        : k == 1
            ? r.torso_lean_half ==
                  (c.phase_index == 0   ? std::array<double, 2>{0, 0}
                   : c.phase_index == 1 ? std::array<double, 2>{0, -.25}
                                        : std::array<double, 2>{-.25, -.25})
        : k == 2 ? r.port_reaction_fraction == std::array<double, 2>{0, 0}
                 : r.seconds_per_parameter == 12;
    if (!good) return fail(d, reason, W::projection_identity);
  }
  for (std::size_t i = 0; i < d.parts.size(); ++i) {
    if (!projection_charge(d, l, reason)) return false;
    if (static_cast<std::size_t>(d.parts[i].id) != i)
      return fail(d, reason, W::projection_identity);
  }
  for (std::size_t side = 0; side < 2; ++side) {
    const auto idx = static_cast<std::size_t>(
        side == 0 ? BoardingBodyPartId::port_boot
                  : BoardingBodyPartId::starboard_boot);
    if (!projection_charge(d, l, reason)) return false;
    const auto* b =
        std::get_if<BoardingRoutePhaseBoxBinding>(&d.parts[idx].reservation);
    if (!b) return fail(d, reason, W::projection_identity);
    if (!projection_charge(d, l, reason)) return false;
    if (b->center !=
        (side == 0 ? P::port_boot_center : P::starboard_boot_center))
      return fail(d, reason, W::projection_identity);
    if (!projection_charge(d, l, reason)) return false;
    if (b->half_size_metres != RigidVector3{.06, .05, .14})
      return fail(d, reason, W::projection_identity);
    if (!projection_charge(d, l, reason)) return false;
    if (b->frame != (side == 0 ? BoardingRoutePhaseFrame::port_sole
                               : BoardingRoutePhaseFrame::starboard_sole))
      return fail(d, reason, W::projection_identity);
    if (!projection_charge(d, l, reason)) return false;
    const auto* src = A::partition(d.source, 1);
    if (!src ||
        (side == 1 && (r.feet[side].sole[0].coordinates[1].count != 1 ||
                       r.feet[side].sole[0].coordinates[1].terms !=
                           std::array<double, 3>{src->plane_metres, 0, 0})))
      return fail(d, reason, W::sole_plane);
  }
  if (!projection_charge(d, l, reason)) return false;
  if (!std::isfinite(p.port_reaction_fraction.lower) ||
      !std::isfinite(p.port_reaction_fraction.upper) ||
      std::abs(p.port_reaction_fraction.lower) > 8 ||
      std::abs(p.port_reaction_fraction.upper) > 8 ||
      p.port_reaction_fraction.lower > 0 || p.port_reaction_fraction.upper < 0)
    return fail(d, reason, W::projection_identity);
  std::size_t i = 0;
  for (const auto* x :
       std::array{&p.center_of_mass,
                  &p.points[static_cast<std::size_t>(P::port_boot_center)],
                  &p.points[static_cast<std::size_t>(P::starboard_boot_center)],
                  &p.points[static_cast<std::size_t>(P::port_ankle)],
                  &p.points[static_cast<std::size_t>(P::starboard_ankle)]})
    if (!carrier(*x, i++, d, c, l, reason)) return false;
  for (std::size_t side = 0; side < 2; ++side)
    for (const auto& x : p.frames[side + 2].columns)
      if (!carrier(x, i++, d, c, l, reason)) return false;
  c.projection_complete = true;
  return true;
}
[[gnu::noinline]] auto join(std::size_t phase) -> bool {
  const auto a = detail::boarding_route_intermediate_unloaded01_controls(phase);
  const auto b =
      detail::boarding_route_intermediate_unloaded01_controls(phase + 1);
  return a && b &&
         detail::boarding_route_intermediate_unloaded01_join_math(*a, *b);
}
[[gnu::noinline]] auto enroll(D& d, const L& l, R& r) -> bool {
  const BoardingIntermediatePauseConstructorEvidence* summary = nullptr;
  const BoardingBootSourcePartition *port = nullptr, *star = nullptr,
                                    *upper = nullptr;
  const auto point = [](int x, int z) {
    return RigidVector3{static_cast<double>(x) * 1e-6,
                        static_cast<double>(-100000) * 1e-6,
                        static_cast<double>(z) * 1e-6};
  };
  const std::array perimeter{point(639335, -500500), point(-639335, -500500),
                             point(-672817, 60500), point(672817, 60500)};
  for (std::size_t i = 0; i < 32; ++i) {
    if (!charge(d, r, d.work.source_guards, l.source_guards,
                W::source_guard_capacity))
      return false;
    d.source_evaluated[i] = true;
    bool ok = false;
    switch (i) {
      case 0:
        ok = A::valid(d.source);
        if (!ok) {
          fail(d, r, W::invalid_binding);
          d.stop_condition = W::invalid_binding;
          return false;
        }
        break;
      case 1:
        ok = detail::boarding_route_foot_phase_environment();
        if (!ok) return fail(d, r, W::unsupported_arithmetic);
        d.arithmetic_supported = true;
        break;
      case 2:
        summary = d.source.summary();
        ok = summary && summary->complete && summary->version == 1 &&
             summary->actual_source_bytes == 4096;
        break;
      case 3: ok = A::data(d.source) != nullptr; break;
      case 4:
        port = A::partition(d.source, 0);
        ok = port;
        break;
      case 5:
        star = A::partition(d.source, 1);
        ok = star;
        break;
      case 6:
        upper = detail::boarding_route_intermediate_unloaded01_upper_partition(
            d.source);
        ok = upper;
        break;
      case 7:
        ok = summary->sources[0].keys ==
                 std::array<LowerCockpitTriangleKey, 2>{
                     {{LowerCockpitContactBuffer::halo, 0, 662},
                      {LowerCockpitContactBuffer::halo, 0, 663}}} &&
             summary->sources[0].object == 10 &&
             summary->sources[0].evaluated_faces ==
                 std::array<std::optional<std::uint32_t>, 2>{182, 183} &&
             port->faces[0].key == summary->sources[0].keys[0] &&
             port->faces[1].key == summary->sources[0].keys[1];
        break;
      case 8:
        ok = summary->sources[1].keys ==
                 std::array<LowerCockpitTriangleKey, 2>{
                     {{LowerCockpitContactBuffer::original, 0, 62119},
                      {LowerCockpitContactBuffer::original, 0, 62120}}} &&
             summary->sources[1].object == 124 &&
             star->faces[0].key == summary->sources[1].keys[0] &&
             star->faces[1].key == summary->sources[1].keys[1];
        break;
      case 9:
        ok = summary->sources[0].name ==
                 "CRAFT | pilot transition intermediate step" &&
             summary->sources[0].inventory_provenance == 735 &&
             summary->sources[1].name == "CABIN | cockpit transition step";
        break;
      case 10:
        ok = port->plane_metres == double(-230000) * 1e-6 &&
             star->plane_metres == double(-160000) * 1e-6 &&
             summary->sources[0].plane == port->plane_metres &&
             summary->sources[1].plane == star->plane_metres;
        break;
      case 11:
      case 12: {
        const auto& q = summary->quads[i - 11];
        ok = q.complete && q.arithmetic_supported && q.horizontal && q.upward &&
             q.convex && q.nondegenerate && q.corner_membership &&
             q.distinct_face_vertices && q.incidence_valid && q.diagonal_valid;
        if (i == 11) {
          const auto& v = port->perimeter_metres;
          ok = ok && v[0].z == v[1].z && v[1].x == v[2].x && v[2].z == v[3].z &&
               v[3].x == v[0].x && v[0].x > v[1].x && v[0].z < v[3].z;
        }
        break;
      }
      case 13: ok = upper->plane_metres == double(-100000) * 1e-6; break;
      case 14:
      case 15: {
        const auto& f = upper->faces[i - 14];
        ok = f.key ==
                 LowerCockpitTriangleKey{
                     LowerCockpitContactBuffer::original, 0,
                     static_cast<std::uint32_t>(88053 + i - 14)} &&
             f.object == 199 &&
             f.source_object == "CABIN | lift-out walking tile 07-1" &&
             !f.evaluated_source_triangle;
        break;
      }
      case 16: ok = upper->perimeter_metres == perimeter; break;
      case 17:
        ok = upper->faces[0].points_current_metres ==
             std::array<RigidVector3, 3>{perimeter[0], perimeter[1],
                                         perimeter[2]};
        break;
      case 18:
        ok = upper->faces[1].points_current_metres ==
             std::array<RigidVector3, 3>{perimeter[0], perimeter[2],
                                         perimeter[3]};
        break;
      case 19: ok = 639335 > 0 && 672817 > 0 && -500500 < 60500; break;
      case 20: ok = d.unloaded_version == 1; break;
      case 21:
        d.parts = detail::boarding_route_foot_phase_parts();
        ok = true;
        break;
      case 22:
        ok = d.parts.size() == 15;
        for (std::size_t j = 0; j < d.parts.size(); ++j)
          ok = ok && static_cast<std::size_t>(d.parts[j].id) == j;
        break;
      case 23:
      case 24:
      case 25: {
        const auto control =
            detail::boarding_route_intermediate_unloaded01_controls(i - 23);
        ok = control && control->seconds_per_parameter == 12 &&
             control->port_reaction_fraction == std::array<double, 2>{0, 0};
        break;
      }
      case 26:
      case 27:
        d.join_expression_identity[i - 26] = join(i - 26);
        ok = d.join_expression_identity[i - 26];
        break;
      case 28: {
        const auto x =
            detail::boarding_route_intermediate_unloaded01_controls(0);
        ok = x && x->feet[0].sole[0].coordinates[1].count == 1 &&
             x->feet[0].sole[0].coordinates[1].terms ==
                 std::array<double, 3>{upper->plane_metres, 0, 0};
        break;
      }
      case 29: {
        const auto x =
            detail::boarding_route_intermediate_unloaded01_controls(0);
        ok = x &&
             same(x->feet[0].sole[0].coordinates[0],
                  x->feet[0].sole[1].coordinates[0]) &&
             same(x->feet[0].sole[0].coordinates[1],
                  x->feet[0].sole[1].coordinates[1]) &&
             same(x->feet[0].sole[1].coordinates[2],
                  x->feet[1].sole[0].coordinates[2]);
        break;
      }
      case 30: {
        const auto x =
            detail::boarding_route_intermediate_unloaded01_controls(2);
        ok = x &&
             x->feet[0].sole[0].coordinates[1].terms ==
                 std::array<double, 3>{upper->plane_metres, 0, 0} &&
             x->feet[0].sole[1].coordinates[1].terms ==
                 std::array<double, 3>{port->plane_metres, 0, 0} &&
             x->feet[0].sole[0].coordinates[1].count == 1 &&
             x->feet[0].sole[1].coordinates[1].count == 1;
        break;
      }
      case 31:
        ok = true;
        for (std::size_t j = 0; j < 3; ++j) {
          const auto x =
              detail::boarding_route_intermediate_unloaded01_controls(j);
          if (!x) {
            ok = false;
            break;
          }
          ok = ok && x->feet[1].yaw_half == std::array<double, 2>{0, 0} &&
               x->feet[1].swing_height_metres == 0 &&
               x->feet[1].sole[0].coordinates[1].terms ==
                   std::array<double, 3>{star->plane_metres, 0, 0};
          for (std::size_t a = 0; a < 3; ++a)
            ok = ok && same(x->feet[1].sole[0].coordinates[a],
                            x->feet[1].sole[1].coordinates[a]);
        }
        break;
      default: break;
    }
    if (!ok) return fail(d, r, W::source_identity);
  }
  d.source_enrolled = true;
  d.reporting_elapsed_seconds = std::abs(
      detail::boarding_route_intermediate_unloaded01_clock(d.requested_last) -
      detail::boarding_route_intermediate_unloaded01_clock(d.requested_first));
  return true;
}
[[gnu::noinline]] auto reset(C& c, Pending p, std::size_t phase) -> void {
  c = C{};
  c.global_first = p.first;
  c.global_last = p.last;
  c.phase_index = phase;
}
[[gnu::noinline]] auto prepare_request(Request& r, std::size_t phase) -> bool {
  const auto x = detail::boarding_route_intermediate_unloaded01_controls(phase);
  if (!x) return false;
  r = *x;
  return canonical_equal(r, phase);
}
auto metadata(R& r, Pending p, std::optional<std::size_t> phase) -> void {
  r.first = p.first;
  r.last = p.last;
  r.depth = p.depth;
  r.phase_index = phase;
  if (phase) {
    r.local_first =
        detail::boarding_route_intermediate_unloaded01_local(*phase, p.first);
    r.local_last =
        detail::boarding_route_intermediate_unloaded01_local(*phase, p.last);
  }
}
auto terminal(D& d, Pending p, W w) -> void {
  R r;
  metadata(
      r, p,
      detail::boarding_route_intermediate_unloaded01_phase(p.first, p.last));
  fail(d, r, w);
  d.first_refusal = r;
  d.stop_condition = w;
}
auto verify(const D& d, double first, double last) -> bool {
  if (d.cells.empty()) return false;
  double cursor = first;
  for (const auto& c : d.cells) {
    if (c.global_first != cursor || !c.complete || !c.star_support ||
        !c.port_zero_geometry || !c.projection_complete ||
        c.phase_index != detail::boarding_route_intermediate_unloaded01_phase(
                             c.global_first, c.global_last) ||
        c.phase.first != detail::boarding_route_intermediate_unloaded01_local(
                             c.phase_index, c.global_first) ||
        c.phase.last != detail::boarding_route_intermediate_unloaded01_local(
                            c.phase_index, c.global_last))
      return false;
    cursor = c.global_last;
  }
  if (cursor != last) return false;
  for (std::size_t j = 0; j < 2; ++j)
    if (first < joins[j] && last > joins[j] && !d.qualified_joins[j])
      return false;
  return true;
}
[[gnu::noinline]] auto cover(
    const detail::BoardingRouteIntermediateUnloaded01AdmissionContext& context,
    Request& request, D& d, Pending requested, const L& l) -> void {
  std::array<Pending, 11> pending{};
  pending[0] = requested;
  std::size_t count = 1;
  C c;
  while (count) {
    const auto p = pending[--count];
    if (d.examined_nodes >= l.phase.nodes) {
      terminal(d, p, W::node_capacity);
      return;
    }
    ++d.examined_nodes;
    d.maximum_depth = std::max(d.maximum_depth, p.depth);
    std::optional<double> cut;
    if (p.first < .25 && p.last > .25)
      cut = .25;
    else if (p.first < .125 && p.last > .125)
      cut = .125;
    if (cut) {
      if (p.depth >= l.phase.depth) {
        terminal(d, p, W::depth_capacity);
        return;
      }
      if (count + 2 > pending.size()) {
        terminal(d, p, W::unsplittable_interval);
        return;
      }
      ++d.mandatory_splits;
      pending[count++] = {*cut, p.last, p.depth + 1};
      pending[count++] = {p.first, *cut, p.depth + 1};
      continue;
    }
    if (d.cells.size() >= l.phase.leaves) {
      terminal(d, p, W::leaf_capacity);
      return;
    }
    if (d.cells.size() >= d.cells.capacity()) {
      terminal(d, p, W::output_capacity);
      return;
    }
    const auto ph =
        detail::boarding_route_intermediate_unloaded01_phase(p.first, p.last);
    if (!ph || !prepare_request(request, *ph)) {
      terminal(d, p, W::phase_prerequisite);
      return;
    }
    reset(c, p, *ph);
    R reason;
    metadata(reason, p, ph);
    d.state = S::not_run;
    d.stop_condition = W::none;
    const auto state =
        detail::boarding_route_intermediate_unloaded01_current_cell(
            context, d, c, l, reason);
    if (state == S::accepted) {
      d.cells.push_back(c);
      for (std::size_t k = 0; k < 3; ++k) {
        d.endpoint_scope[k] = d.endpoint_scope[k] || c.endpoint_scope[k];
        d.endpoint_earned[k] = d.endpoint_earned[k] || c.endpoint_earned[k];
      }
      if (d.cells.size() >= 2) {
        const auto& left = d.cells[d.cells.size() - 2];
        const auto& right = d.cells.back();
        for (std::size_t j = 0; j < 2; ++j)
          if (d.join_expression_identity[j] && requested.first < joins[j] &&
              requested.last > joins[j] && left.phase_index == j &&
              right.phase_index == j + 1 && left.global_first < joins[j] &&
              left.global_last == joins[j] && right.global_first == joins[j] &&
              right.global_last > joins[j] && left.complete && right.complete)
            d.qualified_joins[j] = true;
      }
      continue;
    }
    if (state == S::capacity || state == S::unsupported) {
      d.first_refusal = reason;
      return;
    }
    if (p.first == p.last)
      reason.condition = W::unsplittable_interval;
    else if (p.depth >= l.phase.depth)
      reason.condition = W::depth_capacity;
    else {
      const auto mid = p.first + (p.last - p.first) * .5;
      if (mid > p.first && mid < p.last && count + 2 <= pending.size()) {
        pending[count++] = {mid, p.last, p.depth + 1};
        pending[count++] = {p.first, mid, p.depth + 1};
        continue;
      }
      reason.condition = W::unsplittable_interval;
    }
    d.first_refusal = reason;
    d.stop_condition = reason.condition;
    return;
  }
  d.complete = verify(d, requested.first, requested.last);
  d.kinematic_complete = d.nominal_equilibrium = d.star_support =
      d.port_zero_geometry = d.complete;
  d.state = d.complete ? S::accepted : S::unresolved;
  if (!d.complete) terminal(d, requested, W::incomplete_cover);
}
auto valid_limits(const L& l) -> bool {
  const L m;
  return l.phase.depth <= m.phase.depth && l.phase.nodes <= m.phase.nodes &&
         l.phase.leaves <= m.phase.leaves &&
         l.phase.output_bytes <= m.phase.output_bytes &&
         l.phase.graphs <= m.phase.graphs && l.phase.legs <= m.phase.legs &&
         l.phase.bodies <= m.phase.bodies &&
         l.phase.sectors <= m.phase.sectors &&
         l.phase.timing <= m.phase.timing &&
         l.source_guards <= m.source_guards &&
         l.projection_guards <= m.projection_guards &&
         l.definition_guards <= m.definition_guards &&
         l.pressure_candidates <= m.pressure_candidates &&
         l.disk_edges <= m.disk_edges &&
         l.upper_geometry_edges <= m.upper_geometry_edges &&
         l.intermediate_coordinates <= m.intermediate_coordinates &&
         l.endpoint_operations <= m.endpoint_operations &&
         l.event_guards <= m.event_guards;
}
} // namespace
auto detail::boarding_route_intermediate_unloaded01_definition_charge(
    D& d, C& c, const L& l, R& r) -> bool {
  if (!charge(d, r, d.work.definition_guards, l.definition_guards,
              W::definition_capacity))
    return false;
  const auto i = std::find(c.definition_evaluated.begin(),
                           c.definition_evaluated.end(), false);
  if (i == c.definition_evaluated.end())
    return fail(d, r, W::definition_capacity);
  *i = true;
  return true;
}
auto detail::boarding_route_intermediate_unloaded01_controls(std::size_t phase)
    -> std::optional<Request> {
  return phase < 3 ? boarding_route_intermediate_step02_controls(phase)
                   : std::nullopt;
}
auto detail::boarding_route_intermediate_unloaded01_phase(double first,
                                                          double last)
    -> std::optional<std::size_t> {
  if (!std::isfinite(first) || !std::isfinite(last) || first < 0 || last > .5 ||
      first > last || (first < .125 && last > .125) ||
      (first < .25 && last > .25))
    return {};
  return last <= .125 ? 0 : last <= .25 ? 1 : 2;
}
auto detail::boarding_route_intermediate_unloaded01_local(std::size_t phase,
                                                          double global)
    -> double {
  if (phase > 2 || !std::isfinite(global) || global < 0 || global > .5)
    return std::numeric_limits<double>::quiet_NaN();
  return phase == 0 ? 8 * global : phase == 1 ? 8 * global - 1 : 4 * global - 1;
}
auto detail::boarding_route_intermediate_unloaded01_clock(double global)
    -> double {
  if (!std::isfinite(global) || global < 0 || global > .5)
    return std::numeric_limits<double>::quiet_NaN();
  return global <= .25 ? 96 * global : 24 + 48 * (global - .25);
}
auto detail::boarding_route_intermediate_unloaded01_join_math(const Request& a,
                                                              const Request& b)
    -> bool {
  return boarding_route_intermediate_step02_join_math(a, b);
}
auto detail::boarding_route_intermediate_unloaded01_current_cell(
    const BoardingRouteIntermediateUnloaded01AdmissionContext& context, D& d,
    C& c, const L& l, R& r) -> S {
  if (!guard(
          d, c, l, r,
          [&] {
            return context.owner_ == &d && A::valid(d.source) &&
                   context.data_ == A::data(d.source) &&
                   context.parts_ == &d.parts && context.request_;
          },
          W::invalid_binding))
    return d.state;
  if (!guard(
          d, c, l, r, [] { return boarding_route_foot_phase_environment(); },
          W::unsupported_arithmetic))
    return d.state;
  const auto& request = *context.request_;
  if (!guard(
          d, c, l, r,
          [&] {
            return boarding_route_intermediate_unloaded01_phase(
                       c.global_first, c.global_last) == c.phase_index &&
                   request.seconds_per_parameter == 12;
          },
          W::phase_prerequisite))
    return d.state;
  if (!boarding_route_intermediate_unloaded01_definition_charge(d, c, l, r))
    return d.state;
  BoardingRouteFootPhaseRefusal reason;
  ++d.work.phase_calls;
  const auto status = boarding_route_foot_phase_cell(
      request,
      boarding_route_intermediate_unloaded01_local(c.phase_index,
                                                   c.global_first),
      boarding_route_intermediate_unloaded01_local(c.phase_index,
                                                   c.global_last),
      d.reverse, l.phase, d.work.phase, c.phase, reason);
  if (status != BoardingRouteFootPhaseCellResult::accepted) {
    fail(d, r, W::phase_prerequisite);
    r.phase = reason;
    r.side = reason.side;
    if (status == BoardingRouteFootPhaseCellResult::capacity) {
      d.state = S::capacity;
      d.stop_condition = W::phase_prerequisite;
    }
    if (status == BoardingRouteFootPhaseCellResult::unsupported) {
      d.state = S::unsupported;
      d.arithmetic_supported = false;
      d.stop_condition = W::unsupported_arithmetic;
    }
    return d.state;
  }
  if (!guard(
          d, c, l, r, [&] { return projection(d, c, request, l, r); },
          W::projection_identity))
    return d.state;
  if (!guard(
          d, c, l, r,
          [&] {
            return request.port_reaction_fraction ==
                       std::array<double, 2>{0, 0} &&
                   c.phase.port_reaction_fraction.lower <= 0 &&
                   c.phase.port_reaction_fraction.upper >= 0;
          },
          W::projection_identity))
    return d.state;
  c.port_share = {0, 0, true};
  c.port_zero_only = true;
  c.port_loaded = false;
  if (!guard(
          d, c, l, r,
          [&] {
            c.star_share = {1, 1, true};
            return c.port_zero_only;
          },
          W::projection_identity))
    return d.state;
  const BoardingBootSourcePartition* star = nullptr;
  if (!guard(
          d, c, l, r,
          [&] {
            star = A::partition(d.source, 1);
            return star &&
                   star->faces[0].key ==
                       LowerCockpitTriangleKey{
                           LowerCockpitContactBuffer::original, 0, 62119} &&
                   star->faces[1].key ==
                       LowerCockpitTriangleKey{
                           LowerCockpitContactBuffer::original, 0, 62120};
          },
          W::source_identity))
    return d.state;
  if (!guard(
          d, c, l, r,
          [&] {
            return request.feet[1].sole[0].coordinates[1].count == 1 &&
                   request.feet[1].sole[0].coordinates[1].terms ==
                       std::array<double, 3>{star->plane_metres, 0, 0};
          },
          W::sole_plane))
    return d.state;
  if (!guard(
          d, c, l, r, [&] { return c.phase.target_sole_identities; },
          W::sole_plane))
    return d.state;
  if (!guard(
          d, c, l, r,
          [&] {
            const auto* b = std::get_if<BoardingRoutePhaseBoxBinding>(
                &d.parts[static_cast<std::size_t>(
                             BoardingBodyPartId::starboard_boot)]
                     .reservation);
            return b && b->half_size_metres == RigidVector3{.06, .05, .14} &&
                   b->frame == BoardingRoutePhaseFrame::starboard_sole;
          },
          W::projection_identity))
    return d.state;
  if (!guard(
          d, c, l, r,
          [&] {
            return request.feet[1].yaw_half == std::array<double, 2>{0, 0};
          },
          W::projection_identity))
    return d.state;
  const BoardingRouteIntermediateUnloaded01CurrentCellToken token(context, d, c,
                                                                  request);
  return boarding_route_intermediate_unloaded01_pressure_bridge(token, d, c, l,
                                                                r);
}
auto detail::boarding_route_intermediate_unloaded01_bounded(
    const OriginBoardingIntermediatePauseSupport& p, double first, double last,
    L l) -> E {
  if (!valid_limits(l))
    return std::unexpected("Unloaded registered limits required");
  if (!std::isfinite(first) || !std::isfinite(last) ||
      std::min(first, last) < 0 || std::max(first, last) > .5)
    return std::unexpected("Unloaded finite [0,.5] range required");
  if (!boarding_route_foot_phase_environment())
    return std::unexpected("Unloaded supported arithmetic required");
  if (l.phase.output_bytes < fixed_output)
    return std::unexpected("Unloaded fixed output capacity");
  E expected(std::in_place, p);
  auto& d = *expected;
  d.requested_first = first;
  d.requested_last = last;
  d.reverse = first > last;
  d.output_capacity_bytes = fixed_output;
  R reason;
  if (!enroll(d, l, reason)) {
    d.first_refusal = reason;
    return expected;
  }
  const auto slots = std::min(
      l.phase.leaves, (l.phase.output_bytes - fixed_output) / sizeof(C));
  try {
    d.cells.reserve(slots);
  } catch (const std::bad_alloc&) {
    return std::unexpected("Unloaded reserve allocation failed");
  }
  if (d.cells.capacity() > (l.phase.output_bytes - fixed_output) / sizeof(C))
    return std::unexpected("Unloaded actual output capacity");
  d.output_capacity_bytes = fixed_output + d.cells.capacity() * sizeof(C);
  Request request;
  const BoardingRouteIntermediateUnloaded01AdmissionContext context(d, request);
  cover(context, request, d, {std::min(first, last), std::max(first, last), 0},
        l);
  return expected;
}
auto assess_origin_boarding_route_intermediate_unloaded01(
    const OriginBoardingIntermediatePauseSupport& p, double first, double last)
    -> E {
  return detail::boarding_route_intermediate_unloaded01_bounded(p, first, last);
}
} // namespace apsis_drift
