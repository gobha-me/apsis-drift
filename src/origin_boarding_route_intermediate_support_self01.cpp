#include "apsis_drift/origin_boarding_route_intermediate_support_self01.hpp"
#include "origin_boarding_route_intermediate_load01_internal.hpp"
#include "origin_boarding_route_intermediate_step02_internal.hpp"
#include "origin_boarding_route_intermediate_support_self01_internal.hpp"
#include "origin_boarding_route_intermediate_unloaded01_internal.hpp"
#include "origin_lower_cockpit_contact_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
namespace apsis_drift {
namespace {
using D = BoardingRouteIntermediateSupportSelf01Diagnostic;
using C = BoardingRouteIntermediateSupportSelf01Cell;
using R = BoardingRouteIntermediateSupportSelf01Refusal;
using W = BoardingRouteIntermediateSupportSelf01Condition;
using S = BoardingRouteIntermediateSupportSelf01State;
using L = detail::BoardingRouteIntermediateSupportSelf01Limits;
using Request = BoardingRouteFootPhaseRequest;
using E = std::expected<D, std::string>;
using A = detail::BoardingIntermediatePauseSupportAccess;
using P = BoardingPlantedBodyPointId;
using B = BoardingFootSiteScalarBounds;
using Force = BoardingRouteIntermediateSupportSelf01PortForceState;
struct Pending {
  double first{}, last{};
  std::size_t depth{};
};
constexpr std::array<double, 4> joins{.125, .25, .5, .75};
constexpr std::size_t fixed_output = 2 * sizeof(E);
static_assert(sizeof(E) <=
              kBoardingRouteIntermediateSupportSelf01MaximumExpectedBytes);
static_assert(sizeof(C) <=
              kBoardingRouteIntermediateSupportSelf01MaximumCellBytes);
constexpr std::size_t graph_live =
    32768 + 2 * sizeof(E) + 4096 +
    (sizeof(C) - sizeof(BoardingRouteFootPhaseCell)) + 4 * sizeof(L) + 2048 +
    11 * sizeof(Pending) + sizeof(detail::BoardingRouteFootPhaseLimits) +
    sizeof(BoardingRouteFootPhaseRefusal) + sizeof(std::optional<Request>) +
    sizeof(Request) + sizeof(R) +
    sizeof(detail::BoardingRouteIntermediateSupportSelf01AdmissionContext) +
    2 * sizeof(Pending) + 32;
static_assert(sizeof(L) == 248);
static_assert(sizeof(R) <= 256);
static_assert(sizeof(C) - sizeof(BoardingRouteFootPhaseCell) <= 3296);
static_assert(fixed_output + 1024 * sizeof(C) <=
              kBoardingRouteIntermediateSupportSelf01MaximumOutputBytes);
static_assert(graph_live <=
              kBoardingRouteIntermediateSupportSelf01MaximumScratchBytes);
auto capacity(W w) -> bool {
  return w == W::source_guard_capacity || w == W::projection_capacity ||
         w == W::definition_capacity || w == W::reaction_capacity ||
         w == W::sole_extrema_capacity || w == W::source_coordinate_capacity ||
         w == W::intersection_capacity || w == W::midpoint_capacity ||
         w == W::allocation_capacity || w == W::division_quotient_capacity ||
         w == W::division_fold_capacity || w == W::pressure_capacity ||
         w == W::edge_capacity || w == W::output_capacity ||
         w == W::node_capacity || w == W::leaf_capacity ||
         w == W::event_guard_capacity || w == W::upper_edge_capacity ||
         w == W::endpoint_operation_capacity || w == W::self_body_capacity ||
         w == W::self_pair_capacity || w == W::self_axis_capacity ||
         w == W::self_signed_capacity || w == W::self_owner_capacity ||
         w == W::self_hip_capacity;
}
auto fail(D& d, R& r, W w, std::optional<std::size_t> axis = {},
          std::optional<std::size_t> operation = {}) -> bool {
  if (r.condition == W::none) {
    r.condition = r.predicate_condition = w;
    r.axis = axis;
    r.operation = operation;
  }
  if (capacity(w) || w == W::unsupported_arithmetic) {
    d.stop_condition = w;
    d.state = capacity(w) ? S::capacity : S::unsupported;
    if (w == W::unsupported_arithmetic) d.arithmetic_supported = false;
  } else if (d.state != S::capacity && d.state != S::unsupported)
    d.state = S::unresolved;
  return false;
}
auto charge(D& d, R& r, std::size_t& n, std::size_t cap, W w,
            std::optional<std::size_t> axis = {},
            std::optional<std::size_t> operation = {}) -> bool {
  if (n >= cap) return fail(d, r, w, axis, operation);
  ++n;
  return true;
}
template <class F> auto guard(D& d, C& c, const L& l, R& r, F f, W w) -> bool {
  if (!detail::boarding_route_intermediate_support_self01_definition_charge(
          d, c, l, r))
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
auto valid(B x, double domain) -> bool {
  return x.supported && std::isfinite(x.lower) && std::isfinite(x.upper) &&
         x.lower <= x.upper && std::abs(x.lower) <= domain &&
         std::abs(x.upper) <= domain;
}
auto point(double x) -> B {
  return {x, x, true};
}
auto down(double x) -> double {
  return std::nextafter(x, -std::numeric_limits<double>::infinity());
}
auto up(double x) -> double {
  return std::nextafter(x, std::numeric_limits<double>::infinity());
}
auto add(B a, B b) -> B {
  return {down(a.lower + b.lower), up(a.upper + b.upper),
          a.supported && b.supported};
}
auto sub(B a, B b) -> B {
  return add(a, {-b.upper, -b.lower, b.supported});
}
auto mul(B a, B b) -> B {
  const std::array p{a.lower * b.lower, a.lower * b.upper, a.upper * b.lower,
                     a.upper * b.upper};
  return {down(*std::min_element(p.begin(), p.end())),
          up(*std::max_element(p.begin(), p.end())),
          a.supported && b.supported};
}
auto minimum(B a, B b) -> B {
  return {std::min(a.lower, b.lower), std::min(a.upper, b.upper),
          a.supported && b.supported};
}
auto maximum(B a, B b) -> B {
  return {std::max(a.lower, b.lower), std::max(a.upper, b.upper),
          a.supported && b.supported};
}
auto intersect(B a, B b) -> B {
  return {std::max(a.lower, b.lower), std::min(a.upper, b.upper),
          a.supported && b.supported};
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
      const bool held =
          index != 0 && (c.phase_index >= 3 || (index != 1 && index != 3));
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
      detail::boarding_route_intermediate_support_self01_controls(phase);
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
  if (p.first != detail::boarding_route_intermediate_support_self01_local(
                     c.phase_index, c.global_first))
    return fail(d, reason, W::projection_identity);
  if (!projection_charge(d, l, reason)) return false;
  if (p.last != detail::boarding_route_intermediate_support_self01_local(
                    c.phase_index, c.global_last))
    return fail(d, reason, W::projection_identity);
  // Compare each immutable packet tuple only after its projection charge.
  // The temporary canonical factory lives after the graph has returned.
  for (std::size_t a = 0; a < 3; ++a) {
    if (!projection_charge(d, l, reason)) return false;
    if (!canonical) {
      canonical = detail::boarding_route_intermediate_support_self01_controls(
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
    const bool good =
        k == 0   ? r.root_yaw_half == canonical->root_yaw_half
        : k == 1 ? r.torso_lean_half == canonical->torso_lean_half
        : k == 2 ? r.port_reaction_fraction == canonical->port_reaction_fraction
                 : r.seconds_per_parameter == canonical->seconds_per_parameter;
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
    const auto* src = A::partition(d.source, c.phase_index < 3 ? 1 : side);
    if (!src || ((side == 1 || c.phase_index >= 3) &&
                 (r.feet[side].sole[0].coordinates[1].count != 1 ||
                  r.feet[side].sole[0].coordinates[1].terms !=
                      std::array<double, 3>{src->plane_metres, 0, 0})))
      return fail(d, reason, W::sole_plane);
    if (side == 1 || c.phase_index >= 3) c.sites[side].plane_identity = true;
  }
  if (!projection_charge(d, l, reason)) return false;
  if (!std::isfinite(p.port_reaction_fraction.lower) ||
      !std::isfinite(p.port_reaction_fraction.upper) ||
      std::abs(p.port_reaction_fraction.lower) > 8 ||
      std::abs(p.port_reaction_fraction.upper) > 8 ||
      p.port_reaction_fraction.lower > p.port_reaction_fraction.upper ||
      (c.phase_index < 3 && (p.port_reaction_fraction.lower > 0 ||
                             p.port_reaction_fraction.upper < 0)))
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
template <class Q, class F>
[[gnu::noinline]] auto divide(B n, B den, B& out, std::array<B, 4>* saved,
                              std::array<bool, 4>& qm,
                              std::array<std::array<bool, 2>, 3>& fm, Q qcharge,
                              F fcharge) -> W {
  B run;

  for (std::size_t i = 0; i < 4; ++i) {
    if (!qcharge(i)) return W::division_quotient_capacity;
    qm[i] = true;

    const auto q =
        (i < 2 ? n.lower : n.upper) / (i % 2 == 0 ? den.lower : den.upper);
    const B b{down(q), up(q), true};

    if (saved) (*saved)[i] = b;
    if (!valid(b, 32)) return W::unsupported_arithmetic;

    if (i == 0) {
      run = b;
      continue;
    }
    if (!fcharge(i - 1, 0)) return W::division_fold_capacity;
    fm[i - 1][0] = true;
    run.lower = std::min(run.lower, b.lower);

    if (!fcharge(i - 1, 1)) return W::division_fold_capacity;
    fm[i - 1][1] = true;
    run.upper = std::max(run.upper, b.upper);
  }
  out = run;
  return W::none;
}
[[gnu::noinline]] auto join(std::size_t phase) -> bool {
  const auto a =
      detail::boarding_route_intermediate_support_self01_controls(phase);
  const auto b =
      detail::boarding_route_intermediate_support_self01_controls(phase + 1);
  return a && b &&
         detail::boarding_route_intermediate_support_self01_join_math(*a, *b);
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
  for (std::size_t i = 0; i < 36; ++i) {
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
      case 20:
        ok = d.support_version == 1 &&
             kBoardingBootPressureRadiusMetres == .020 &&
             kBoardingBootDiskEdgeMarginMetres == .010 &&
             kBoardingBootLoadMarginMetres == .010;
        break;
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
            detail::boarding_route_intermediate_support_self01_controls(i - 23);
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
            detail::boarding_route_intermediate_support_self01_controls(0);
        ok = x && x->feet[0].sole[0].coordinates[1].count == 1 &&
             x->feet[0].sole[0].coordinates[1].terms ==
                 std::array<double, 3>{upper->plane_metres, 0, 0};
        break;
      }
      case 29: {
        const auto x =
            detail::boarding_route_intermediate_support_self01_controls(0);
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
            detail::boarding_route_intermediate_support_self01_controls(2);
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
              detail::boarding_route_intermediate_support_self01_controls(j);
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
      case 32:
      case 33: {
        const auto x =
            detail::boarding_route_intermediate_support_self01_controls(i - 29);
        ok = x && x->seconds_per_parameter == (i == 32 ? 12 : 2) &&
             x->port_reaction_fraction ==
                 (i == 32 ? std::array<double, 2>{0, .0625}
                          : std::array<double, 2>{.0625, .0625}) &&
             x->feet[0].yaw_half == std::array<double, 2>{0, 0} &&
             x->feet[1].yaw_half == std::array<double, 2>{0, 0} &&
             x->feet[0].swing_height_metres == 0 &&
             x->feet[1].swing_height_metres == 0;
        if (x)
          for (std::size_t side = 0; side < 2; ++side) {
            const auto* q = side == 0 ? port : star;
            ok = ok && x->feet[side].sole[0].coordinates[1].count == 1 &&
                 x->feet[side].sole[0].coordinates[1].terms ==
                     std::array<double, 3>{q->plane_metres, 0, 0};
            for (std::size_t a = 0; a < 3; ++a)
              ok = ok && same(x->feet[side].sole[0].coordinates[a],
                              x->feet[side].sole[1].coordinates[a]);
          }
        break;
      }
      case 34:
      case 35:
        d.join_expression_identity[i - 32] = join(i - 32);
        ok = d.join_expression_identity[i - 32];
        break;
      default: break;
    }
    if (!ok) return fail(d, r, W::source_identity);
  }
  if (!detail::boarding_route_intermediate_support_self01_enroll_self_source(
          d, l, r))
    return false;
  d.source_enrolled = true;
  d.reporting_elapsed_seconds =
      std::abs(detail::boarding_route_intermediate_support_self01_clock(
                   d.requested_last) -
               detail::boarding_route_intermediate_support_self01_clock(
                   d.requested_first));
  return true;
}
[[gnu::noinline]] auto reset(C& c, Pending p, std::size_t phase) -> void {
  c = C{};
  c.global_first = p.first;
  c.global_last = p.last;
  c.phase_index = phase;
  c.protocol =
      phase < 3
          ? BoardingRouteIntermediateSupportSelf01Protocol::unloaded_prefix
          : BoardingRouteIntermediateSupportSelf01Protocol::load_acquisition;
}
[[gnu::noinline]] auto prepare_request(Request& r, std::size_t phase) -> bool {
  const auto x =
      detail::boarding_route_intermediate_support_self01_controls(phase);
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
    r.local_first = detail::boarding_route_intermediate_support_self01_local(
        *phase, p.first);
    r.local_last = detail::boarding_route_intermediate_support_self01_local(
        *phase, p.last);
  }
}
auto terminal(D& d, Pending p, W w) -> void {
  R r;
  metadata(r, p,
           detail::boarding_route_intermediate_support_self01_phase(p.first,
                                                                    p.last));
  fail(d, r, w);
  d.first_refusal = r;
  d.stop_condition = w;
}
auto verify(const D& d, double first, double last) -> bool {
  if (d.cells.empty()) return false;
  double cursor = first;
  for (const auto& c : d.cells) {
    if (c.global_first != cursor || !c.complete || !c.star_support ||
        !c.nominal_support_complete || !c.self_body_complete ||
        !c.self_complete || !c.projection_complete ||
        c.phase_index !=
            detail::boarding_route_intermediate_support_self01_phase(
                c.global_first, c.global_last) ||
        c.phase.first !=
            detail::boarding_route_intermediate_support_self01_local(
                c.phase_index, c.global_first) ||
        c.phase.last !=
            detail::boarding_route_intermediate_support_self01_local(
                c.phase_index, c.global_last))
      return false;
    cursor = c.global_last;
  }
  if (cursor != last) return false;
  for (std::size_t j = 0; j < 4; ++j)
    if (first < joins[j] && last > joins[j] && !d.qualified_joins[j])
      return false;
  return true;
}
[[gnu::noinline]] auto cover(
    const detail::BoardingRouteIntermediateSupportSelf01AdmissionContext&
        context,
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
    if (p.first < .5 && p.last > .5)
      cut = .5;
    else if (p.first < .25 && p.last > .25)
      cut = .25;
    else if (p.first < .125 && p.last > .125)
      cut = .125;
    else if (p.first < .75 && p.last > .75)
      cut = .75;
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
    const auto ph = detail::boarding_route_intermediate_support_self01_phase(
        p.first, p.last);
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
        detail::boarding_route_intermediate_support_self01_current_cell(
            context, d, c, l, reason);
    if (state == S::accepted) {
      d.cells.push_back(c);
      for (std::size_t k = 0; k < 3 && c.phase_index < 3; ++k) {
        d.prefix_endpoint_scope[k] =
            d.prefix_endpoint_scope[k] || c.endpoint_scope[k];
        d.prefix_endpoint_earned[k] =
            d.prefix_endpoint_earned[k] || c.endpoint_earned[k];
      }
      if (d.cells.size() >= 2) {
        const auto& left = d.cells[d.cells.size() - 2];
        const auto& right = d.cells.back();
        for (std::size_t j = 0; j < 4; ++j)
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
  d.kinematic_complete = d.nonnegative_reactions = d.nominal_equilibrium =
      d.star_support = d.nominal_support_complete = d.self_body_complete =
          d.self_complete = d.complete;
  d.prefix_zero_geometry_complete =
      d.complete &&
      std::any_of(d.cells.begin(), d.cells.end(),
                  [](const auto& x) { return x.phase_index < 3; }) &&
      std::all_of(d.cells.begin(), d.cells.end(), [](const auto& x) {
        return x.phase_index >= 3 || x.prefix_zero_geometry_complete;
      });
  d.state = d.complete ? S::accepted : S::unresolved;
  if (!d.complete) terminal(d, requested, W::incomplete_cover);
}
auto valid_limits(const L& l) -> bool {
  const L m;
  const auto& a = l.phase;
  const auto& b = m.phase;

  return a.depth <= b.depth && a.nodes <= b.nodes && a.leaves <= b.leaves &&
         a.output_bytes <= b.output_bytes && a.graphs <= b.graphs &&
         a.legs <= b.legs && a.bodies <= b.bodies && a.sectors <= b.sectors &&
         a.timing <= b.timing && l.source_guards <= m.source_guards &&
         l.projection_guards <= m.projection_guards &&
         l.definition_guards <= m.definition_guards &&
         l.pressure_candidates <= m.pressure_candidates &&
         l.disk_edges <= m.disk_edges && l.sole_extrema <= m.sole_extrema &&
         l.source_coordinates <= m.source_coordinates &&
         l.intersection_operations <= m.intersection_operations &&
         l.midpoint_operations <= m.midpoint_operations &&
         l.allocation_operations <= m.allocation_operations &&
         l.reaction_operations <= m.reaction_operations &&
         l.division_quotients <= m.division_quotients &&
         l.division_folds <= m.division_folds &&
         l.upper_geometry_edges <= m.upper_geometry_edges &&
         l.endpoint_operations <= m.endpoint_operations &&
         l.event_guards <= m.event_guards &&
         l.self_body_guards <= m.self_body_guards &&
         l.self_pairs <= m.self_pairs && l.self_axes <= m.self_axes &&
         l.self_signed_trials <= m.self_signed_trials &&
         l.self_owners <= m.self_owners &&
         l.self_hip_complements <= m.self_hip_complements;
}
} // namespace
auto detail::boarding_route_intermediate_support_self01_definition_charge(
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
[[gnu::noinline]] auto detail::
    boarding_route_intermediate_support_self01_controls(std::size_t phase)
        -> std::optional<Request> {
  return phase < 3   ? boarding_route_intermediate_unloaded01_controls(phase)
         : phase < 5 ? boarding_route_intermediate_load01_controls(phase)
                     : std::nullopt;
}
auto detail::boarding_route_intermediate_support_self01_phase(double first,
                                                              double last)
    -> std::optional<std::size_t> {
  if (!std::isfinite(first) || !std::isfinite(last) || first < 0 || last > 1 ||
      first > last)
    return {};
  for (const auto j : joins)
    if (first < j && last > j) return {};
  return last <= .125  ? 0
         : last <= .25 ? 1
         : last <= .5  ? 2
         : last <= .75 ? 3
                       : 4;
}
auto detail::boarding_route_intermediate_support_self01_local(std::size_t phase,
                                                              double g)
    -> double {
  if (phase > 4 || !std::isfinite(g) || g < 0 || g > 1)
    return std::numeric_limits<double>::quiet_NaN();
  return phase == 0   ? 8 * g
         : phase == 1 ? 8 * g - 1
                      : 4 * g - static_cast<double>(phase - 1);
}
auto detail::boarding_route_intermediate_support_self01_clock(double g)
    -> double {
  if (!std::isfinite(g) || g < 0 || g > 1)
    return std::numeric_limits<double>::quiet_NaN();
  return g <= .25   ? 96 * g
         : g <= .75 ? 24 + 48 * (g - .25)
                    : 48 + 8 * (g - .75);
}
auto detail::boarding_route_intermediate_support_self01_join_math(
    const Request& a, const Request& b) -> bool {
  return boarding_route_intermediate_step02_join_math(a, b);
}
[[gnu::noinline]] auto detail::
    boarding_route_intermediate_support_self01_allocate(D& d, C& c, const L& l,
                                                        R& r) -> bool {
  std::array<B, 2> com;
  std::array<std::array<B, 2>, 2> centers;

  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t a = 0; a < 2; ++a) {
      if (!guard(
              d, c, l, r,
              [&] {
                const auto& v =
                    i == 0 ? c.phase.center_of_mass.value
                           : c.phase
                                 .points[static_cast<std::size_t>(
                                     i == 1 ? P::port_boot_center
                                            : P::starboard_boot_center)]
                                 .value;

                const B b{a == 0 ? v.lower.x : v.lower.z,
                          a == 0 ? v.upper.x : v.upper.z, true};
                if (!valid(b, 8)) return false;
                (i == 0 ? com[a] : centers[i - 1][a]) = b;
                return true;
              },
              W::unsupported_arithmetic))
        return false;
    }
  const BoardingBootSourcePartition* port{};

  for (std::size_t side = 0; side < 2; ++side) {
    if (!guard(
            d, c, l, r,
            [&] {
              const auto* src = A::partition(d.source, side);
              const auto* summary = d.source.summary();
              if (!src || !summary || !summary->complete) return false;
              const auto& id = summary->sources[side];
              if (id.keys[0] != src->faces[0].key ||
                  id.keys[1] != src->faces[1].key ||
                  id.plane != src->plane_metres || id.name.empty())
                return false;
              if (side == 0) port = src;
              return true;
            },
            W::source_identity))
      return false;
  }
  std::array<B, 2> solelow, solehigh, lows, highs;
  std::array<std::array<B, 2>, 4> vertices;

  for (std::size_t a = 0; a < 2; ++a)
    for (std::size_t which = 0; which < 2; ++which) {
      if (!charge(d, r, d.work.sole_extrema, l.sole_extrema,
                  W::sole_extrema_capacity, a, which))
        return false;
      c.sole_extrema_evaluated[a][which] = true;
      (which == 0 ? solelow[a] : solehigh[a]) =
          which == 0 ? sub(centers[0][a], point(a == 0 ? .06 : .14))
                     : add(centers[0][a], point(a == 0 ? .06 : .14));
    }
  if (!guard(
          d, c, l, r,
          [&] {
            return valid(solelow[0], 16) && valid(solelow[1], 16) &&
                   valid(solehigh[0], 16) && valid(solehigh[1], 16);
          },
          W::unsupported_arithmetic))
    return false;

  for (std::size_t v = 0; v < 4; ++v)
    for (std::size_t a = 0; a < 2; ++a) {
      if (!charge(d, r, d.work.source_coordinates, l.source_coordinates,
                  W::source_coordinate_capacity, a, v))
        return false;
      c.source_coordinate_evaluated[v][a] = true;
      const auto p = port->perimeter_metres[v];
      const B b = point(a == 0 ? p.x : p.z);
      if (!valid(b, 8)) return fail(d, r, W::unsupported_arithmetic, a, v);
      vertices[v][a] = b;
      lows[a] = v == 0 ? b : minimum(lows[a], b);
      highs[a] = v == 0 ? b : maximum(highs[a], b);
    }
  if (!guard(
          d, c, l, r,
          [&] {
            return valid(lows[0], 8) && valid(lows[1], 8) &&
                   valid(highs[0], 8) && valid(highs[1], 8);
          },
          W::unsupported_arithmetic))
    return false;

  auto equal = [](B a, B b) {
    return a.lower == a.upper && b.lower == b.upper && a.lower == b.lower;
  };

  if (!guard(
          d, c, l, r,
          [&] {
            return equal(vertices[0][0], vertices[3][0]) &&
                   equal(vertices[1][0], vertices[2][0]) &&
                   equal(vertices[0][1], vertices[1][1]) &&
                   equal(vertices[2][1], vertices[3][1]);
          },
          W::source_rectangle))
    return false;

  if (!guard(
          d, c, l, r,
          [&] {
            return vertices[1][0].upper < vertices[0][0].lower &&
                   vertices[0][1].upper < vertices[2][1].lower;
          },
          W::source_rectangle))
    return false;

  const bool zero =
      c.phase_index == 3 && c.phase.first == 0 && c.phase.last == 0;

  const bool full =
      c.phase_index == 4 || (c.phase.first == 1 && c.phase.last == 1);

  if (!charge(d, r, d.work.reaction_operations, l.reaction_operations,
              W::reaction_capacity, {}, 0))
    return false;
  c.reaction_evaluated[0] = true;

  const B reported{c.phase.port_reaction_fraction.lower,
                   c.phase.port_reaction_fraction.upper, true};

  c.port_share = intersect(reported, zero   ? point(0)
                                     : full ? point(.0625)
                                            : B{0, .0625, true});

  if (!guard(
          d, c, l, r,
          [&] {
            if (!valid(c.port_share, .0625) || c.port_share.lower < 0)
              return false;

            if (c.phase_index == 3 && c.phase.first == 0 &&
                (reported.lower > 0 || reported.upper < 0))
              return false;

            if ((c.phase_index == 4 || c.phase.last == 1) &&
                (reported.lower > .0625 || reported.upper < .0625))
              return false;

            c.port_force_state = zero ? Force::zero_only
                                 : c.phase_index == 3 && c.phase.first == 0
                                     ? Force::zero_boundary_positive_interior
                                     : Force::everywhere_positive;

            c.port_positive_interior = c.port_force_state != Force::zero_only;
            c.contains_zero_endpoint = c.phase_index == 3 && c.phase.first == 0;

            c.checkpoint_threshold_met = full && .0625 > .010;
            return true;
          },
          W::reaction_identity))
    return false;

  if (!charge(d, r, d.work.reaction_operations, l.reaction_operations,
              W::reaction_capacity, {}, 1))
    return false;
  c.reaction_evaluated[1] = true;
  const auto rawcomplement = sub(point(1), c.port_share);

  if (!charge(d, r, d.work.reaction_operations, l.reaction_operations,
              W::reaction_capacity, {}, 2))
    return false;
  c.reaction_evaluated[2] = true;
  c.star_share = intersect(rawcomplement, zero   ? point(1)
                                          : full ? point(.9375)
                                                 : B{.9375, 1, true});

  if (!guard(
          d, c, l, r,
          [&] {
            if (!valid(c.star_share, 1) || c.star_share.lower < .9375)
              return false;
            if (c.contains_zero_endpoint &&
                (rawcomplement.lower > 1 || rawcomplement.upper < 1))
              return false;
            if ((c.phase_index == 4 || c.phase.last == 1) &&
                (rawcomplement.lower > .9375 || rawcomplement.upper < .9375))
              return false;
            c.star_everywhere_positive = true;
            return true;
          },
          W::denominator))
    return false;

  std::array<B, 2> lo, hi;

  for (std::size_t a = 0; a < 2; ++a) {
    for (std::size_t which = 0; which < 2; ++which) {
      if (!charge(d, r, d.work.intersection_operations,
                  l.intersection_operations, W::intersection_capacity, a,
                  which))
        return false;
      c.intersection_evaluated[a][which] = true;
      (which == 0 ? lo[a] : hi[a]) = which == 0
                                         ? maximum(solelow[a], lows[a])
                                         : minimum(solehigh[a], highs[a]);
    }
    if (!guard(
            d, c, l, r,
            [&] {
              return valid(lo[a], 32) && valid(hi[a], 32) &&
                     lo[a].upper < hi[a].lower;
            },
            W::empty_intersection))
      return false;
  }
  if (!charge(d, r, d.work.pressure_candidates, l.pressure_candidates,
              W::pressure_capacity, {}, 0))
    return false;

  for (std::size_t a = 0; a < 2; ++a) {
    if (!charge(d, r, d.work.midpoint_operations, l.midpoint_operations,
                W::midpoint_capacity, a, 0))
      return false;
    c.midpoint_evaluated[a][0] = true;
    const auto sum = add(lo[a], hi[a]);
    if (!charge(d, r, d.work.midpoint_operations, l.midpoint_operations,
                W::midpoint_capacity, a, 1))
      return false;
    c.midpoint_evaluated[a][1] = true;
    c.pressure_xz[0][a] = mul(point(.5), sum);
    if (!guard(
            d, c, l, r, [&] { return valid(c.pressure_xz[0][a], 32); },
            W::unsupported_arithmetic))
      return false;
  }
  c.pressure_evaluated[0] = true;

  if (!charge(d, r, d.work.pressure_candidates, l.pressure_candidates,
              W::pressure_capacity, {}, 1))
    return false;

  for (std::size_t a = 0; a < 2; ++a) {
    if (!charge(d, r, d.work.allocation_operations, l.allocation_operations,
                W::allocation_capacity, a, 0))
      return false;
    c.allocation_evaluated[a][0] = true;
    const auto weighted = mul(c.port_share, c.pressure_xz[0][a]);

    if (!charge(d, r, d.work.allocation_operations, l.allocation_operations,
                W::allocation_capacity, a, 1))
      return false;
    c.allocation_evaluated[a][1] = true;
    const auto residual = sub(com[a], weighted);

    if (!guard(
            d, c, l, r, [&] { return valid(residual, 32); },
            W::unsupported_arithmetic))
      return false;

    if (!charge(d, r, d.work.allocation_operations, l.allocation_operations,
                W::allocation_capacity, a, 2))
      return false;
    c.allocation_evaluated[a][2] = true;

    const auto why = divide(
        residual, c.star_share, c.pressure_xz[1][a], nullptr,
        c.division_quotient_evaluated[a], c.division_fold_evaluated[a],
        [&](std::size_t i) {
          return charge(d, r, d.work.division_quotients, l.division_quotients,
                        W::division_quotient_capacity, a, i);
        },
        [&](std::size_t i, std::size_t side) {
          return charge(d, r, d.work.division_folds, l.division_folds,
                        W::division_fold_capacity, a, 2 * i + side);
        });

    if (why != W::none) return fail(d, r, why, a);
    if (!guard(
            d, c, l, r, [&] { return valid(c.pressure_xz[1][a], 32); },
            W::unsupported_arithmetic))
      return false;
  }
  c.pressure_evaluated[1] = true;

  // Symbolic identities use the SAME narrowed genuine function and stored
  // port COP: star=(COM-w*port)/(1-w), hence force=1 and moments=COM.
  if (!guard(
          d, c, l, r,
          [&] {
            return c.reaction_evaluated ==
                       std::array<bool, 3>{true, true, true} &&
                   c.star_everywhere_positive;
          },
          W::symbolic_equilibrium))
    return false;

  if (!guard(
          d, c, l, r,
          [&] {
            return c.pressure_evaluated == std::array<bool, 2>{true, true} &&
                   std::all_of(c.allocation_evaluated.begin(),
                               c.allocation_evaluated.end(),
                               [](const auto& x) {
                                 return std::all_of(x.begin(), x.end(),
                                                    [](bool y) { return y; });
                               }) &&
                   std::all_of(c.division_quotient_evaluated.begin(),
                               c.division_quotient_evaluated.end(),
                               [](const auto& x) {
                                 return std::all_of(x.begin(), x.end(),
                                                    [](bool y) { return y; });
                               }) &&
                   std::all_of(c.division_fold_evaluated.begin(),
                               c.division_fold_evaluated.end(),
                               [](const auto& x) {
                                 return std::all_of(x.begin(), x.end(),
                                                    [](const auto& y) {
                                                      return y[0] && y[1];
                                                    });
                               });
          },
          W::symbolic_equilibrium))
    return false;

  c.nominal_equilibrium = true;
  return true;
}
auto detail::boarding_route_intermediate_support_self01_current_cell(
    const BoardingRouteIntermediateSupportSelf01AdmissionContext& context, D& d,
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
            return boarding_route_intermediate_support_self01_phase(
                       c.global_first, c.global_last) == c.phase_index &&
                   request.seconds_per_parameter ==
                       (c.phase_index == 4 ? 2 : 12);
          },
          W::phase_prerequisite))
    return d.state;
  if (!boarding_route_intermediate_support_self01_definition_charge(d, c, l, r))
    return d.state;
  BoardingRouteFootPhaseRefusal reason;
  ++d.work.phase_calls;
  const auto status = boarding_route_foot_phase_cell(
      request,
      boarding_route_intermediate_support_self01_local(c.phase_index,
                                                       c.global_first),
      boarding_route_intermediate_support_self01_local(c.phase_index,
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
  if (c.phase_index < 3) {
    if (!guard(
            d, c, l, r, [&] { return projection(d, c, request, l, r); },
            W::projection_identity))
      return d.state;
  } else if (!projection(d, c, request, l, r))
    return d.state;
  if (c.phase_index >= 3) {
    const BoardingRouteIntermediateSupportSelf01CurrentCellToken token(
        context, d, c, request);
    return boarding_route_intermediate_support_self01_pressure_bridge(token, d,
                                                                      c, l, r);
  }
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
  c.port_force_state = Force::zero_only;
  c.star_everywhere_positive = true;
  if (!guard(
          d, c, l, r,
          [&] {
            c.star_share = {1, 1, true};
            return c.port_force_state == Force::zero_only;
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
  const BoardingRouteIntermediateSupportSelf01CurrentCellToken token(
      context, d, c, request);
  return boarding_route_intermediate_support_self01_pressure_bridge(token, d, c,
                                                                    l, r);
}
auto detail::boarding_route_intermediate_support_self01_pressure_bridge(
    const BoardingRouteIntermediateSupportSelf01CurrentCellToken& t, D& d, C& c,
    const L& l, R& r) -> S {
  const auto state =
      c.phase_index < 3
          ? boarding_route_intermediate_support_self01_prefix_bridge(t, d, c, l,
                                                                     r)
          : boarding_route_intermediate_support_self01_load_bridge(t, d, c, l,
                                                                   r);
  if (state != S::accepted) return state;
  c.complete = false;
  return boarding_route_intermediate_support_self01_self_bridge(t, d, c, l, r);
}

auto detail::boarding_route_intermediate_support_self01_bounded(
    const OriginBoardingIntermediatePauseSupport& p, double first, double last,
    L l) -> E {
  if (!valid_limits(l))
    return std::unexpected("Support registered limits required");
  if (!std::isfinite(first) || !std::isfinite(last) ||
      std::min(first, last) < 0 || std::max(first, last) > 1)
    return std::unexpected("Support finite [0,1] range required");
  if (!boarding_route_foot_phase_environment())
    return std::unexpected("Support supported arithmetic required");
  if (l.phase.output_bytes < fixed_output)
    return std::unexpected("Support fixed output capacity");
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
    return std::unexpected("Support reserve allocation failed");
  }
  if (d.cells.capacity() > (l.phase.output_bytes - fixed_output) / sizeof(C))
    return std::unexpected("Support actual output capacity");
  d.output_capacity_bytes = fixed_output + d.cells.capacity() * sizeof(C);
  Request request;
  const BoardingRouteIntermediateSupportSelf01AdmissionContext context(d,
                                                                       request);
  cover(context, request, d, {std::min(first, last), std::max(first, last), 0},
        l);
  return expected;
}
auto assess_origin_boarding_route_intermediate_support_self01(
    const OriginBoardingIntermediatePauseSupport& p, double first, double last)
    -> E {
  return detail::boarding_route_intermediate_support_self01_bounded(p, first,
                                                                    last);
}
} // namespace apsis_drift
