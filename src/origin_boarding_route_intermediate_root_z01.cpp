#include "apsis_drift/origin_boarding_route_intermediate_root_z01.hpp"
#include "origin_boarding_route_intermediate_load01_internal.hpp"
#include "origin_boarding_route_intermediate_root_z01_arithmetic_internal.hpp"
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
using D = detail::RootZ01SupportBorrow;
using C = BoardingRouteIntermediateSupportSelf01Cell;
using R = BoardingRouteIntermediateSupportSelf01Refusal;
using W = BoardingRouteIntermediateSupportSelf01Condition;
using S = BoardingRouteIntermediateSupportSelf01State;
using L = detail::BoardingRouteIntermediateSupportSelf01Limits;
using Request = BoardingRouteFootPhaseRequest;
using A = detail::BoardingIntermediatePauseSupportAccess;
using P = BoardingPlantedBodyPointId;
using B = BoardingFootSiteScalarBounds;
using Force = BoardingRouteIntermediateSupportSelf01PortForceState;
struct Pending {
  double first{}, last{};
  std::size_t depth{};
};
constexpr std::array<double, 4> joins{.125, .25, .5, .75};
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
  if (!detail::root_z01_definition_charge(d, c, l, r)) return false;
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
      detail::boarding_route_intermediate_root_z01_controls(phase);
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
  if (p.first != detail::boarding_route_intermediate_root_z01_local(
                     c.phase_index, c.global_first))
    return fail(d, reason, W::projection_identity);
  if (!projection_charge(d, l, reason)) return false;
  if (p.last != detail::boarding_route_intermediate_root_z01_local(
                    c.phase_index, c.global_last))
    return fail(d, reason, W::projection_identity);
  // Compare each immutable packet tuple only after its projection charge.
  // The temporary canonical factory lives after the graph has returned.
  for (std::size_t a = 0; a < 3; ++a) {
    if (!projection_charge(d, l, reason)) return false;
    if (!canonical) {
      canonical =
          detail::boarding_route_intermediate_root_z01_controls(c.phase_index);
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
  const auto a = detail::boarding_route_intermediate_root_z01_controls(phase);
  const auto b =
      detail::boarding_route_intermediate_root_z01_controls(phase + 1);
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
  for (std::size_t i = 1; i < 36; ++i) {
    if (!charge(d, r, d.work.source_guards, l.source_guards,
                W::source_guard_capacity))
      return false;
    d.source_evaluated[i] = true;
    bool ok = false;
    switch (i) {
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
            detail::boarding_route_intermediate_root_z01_controls(i - 23);
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
        const auto x = detail::boarding_route_intermediate_root_z01_controls(0);
        ok = x && x->feet[0].sole[0].coordinates[1].count == 1 &&
             x->feet[0].sole[0].coordinates[1].terms ==
                 std::array<double, 3>{upper->plane_metres, 0, 0};
        break;
      }
      case 29: {
        const auto x = detail::boarding_route_intermediate_root_z01_controls(0);
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
        const auto x = detail::boarding_route_intermediate_root_z01_controls(2);
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
              detail::boarding_route_intermediate_root_z01_controls(j);
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
            detail::boarding_route_intermediate_root_z01_controls(i - 29);
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
  if (!detail::root_z01_enroll_self_source(d, l, r)) return false;
  d.source_enrolled = true;
  d.reporting_elapsed_seconds = std::abs(
      detail::boarding_route_intermediate_root_z01_clock(d.requested_last) -
      detail::boarding_route_intermediate_root_z01_clock(d.requested_first));
  return true;
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
auto detail::root_z01_definition_charge(D& d, C& c, const L& l, R& r) -> bool {
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
[[gnu::noinline]] auto detail::root_z01_allocate(D& d, C& c, const L& l, R& r)
    -> bool {
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

} // namespace apsis_drift

#include "origin_boarding_checkpoint_material_extension_internal.hpp"
#include "origin_boarding_hatch_seal_material_internal.hpp"
#include "origin_boarding_initial_material_internal.hpp"
#include "origin_boarding_separation_ring_material_internal.hpp"
namespace apsis_drift {
struct OriginBoardingRouteIntermediateRootZ01::Data {
  NativeCraftBinding binding;
  OriginBoardingBootSupport boots;
  OriginBoardingIntermediatePauseSupport pause;
  OriginBoardingInitialMaterial material;
  OriginBoardingCheckpointMaterialExtension extension;
  OriginBoardingHatchSealMaterial seal;
  OriginBoardingSeparationRingMaterial ring;
  BoardingRouteIntermediateRootZ01ConstructorEvidence evidence;
  Data(const NativeCraftBinding& b, const OriginBoardingBootSupport& s,
       const OriginBoardingIntermediatePauseSupport& p,
       const OriginBoardingInitialMaterial& m,
       const OriginBoardingCheckpointMaterialExtension& e,
       const OriginBoardingHatchSealMaterial& h,
       const OriginBoardingSeparationRingMaterial& r,
       const BoardingRouteIntermediateRootZ01ConstructorEvidence& v)
      : binding(b), boots(s), pause(p), material(m), extension(e), seal(h),
        ring(r), evidence(v) {}
};
namespace {
using RD = BoardingRouteIntermediateRootZ01Diagnostic;
using RC = BoardingRouteIntermediateRootZ01Cell;
using RR = BoardingRouteIntermediateRootZ01Refusal;
using RW = BoardingRouteIntermediateRootZ01Condition;
using RS = BoardingRouteIntermediateRootZ01State;
using RG = BoardingRouteIntermediateRootZ01Stage;
using RL = detail::BoardingRouteIntermediateRootZ01Limits;
using RA = detail::BoardingRouteIntermediateRootZ01Access;
using RE = BoardingRouteIntermediateRootZ01Expected;
using Event = BoardingRouteIntermediateRootZ01ContactEvent;
constexpr std::size_t root_fixed_output = 2 * sizeof(RE);
// Rebound shared control block and Data occupy one charged 1024-byte arena.
// No separate allocation or old/new arena growth occurs.
template <class T> struct RootZ01ArenaAllocator {
  using value_type = T;
  RootZ01ArenaAllocator() = default;
  template <class U> RootZ01ArenaAllocator(const RootZ01ArenaAllocator<U>&) {}
  auto allocate(std::size_t n) -> T* {
    static_assert(alignof(T) <= alignof(std::max_align_t));
    if (n > kBoardingRouteIntermediateRootZ01MaximumSourceBytes / sizeof(T))
      throw std::bad_alloc();
    return static_cast<T*>(
        ::operator new(kBoardingRouteIntermediateRootZ01MaximumSourceBytes));
  }
  auto deallocate(T* p, std::size_t) -> void { ::operator delete(p); }
};
template <class T, class U>
auto operator==(RootZ01ArenaAllocator<T>, RootZ01ArenaAllocator<U>) -> bool {
  return true;
}
auto root_refuse(RD& d, RR& r, RW why, RG stage, RS state, bool terminal = true)
    -> RS {
  if (r.condition == RW::none) r.condition = r.predicate_condition = why;
  r.stage = stage;
  if (terminal && !d.first_refusal) d.first_refusal = r;
  d.stop_condition = why;
  d.stop_stage = stage;
  d.state = state;
  d.complete = d.route_qualified = false;
  if (state == RS::unsupported) d.arithmetic_supported = false;
  return state;
}
auto root_support_result(RD& d, RR& r, const D& b, RG stage) -> RS {
  if (b.state == S::accepted) return RS::accepted;
  const auto state = b.state == S::capacity      ? RS::capacity
                     : b.state == S::unsupported ? RS::unsupported
                     : r.support_self.condition == W::invalid_binding
                         ? RS::identity
                         : RS::unresolved;
  const auto why = state == RS::capacity      ? RW::work_capacity
                   : state == RS::unsupported ? RW::unsupported_arithmetic
                   : state == RS::identity    ? RW::invalid_binding
                   : stage == RG::phase       ? RW::phase_refused
                   : stage == RG::self        ? RW::self_refused
                   : stage == RG::load        ? RW::load_refused
                                              : RW::contact_refused;
  return root_refuse(d, r, why, stage, state, state != RS::unresolved);
}
auto root_limits_valid(const RL& l) -> bool {
  const RL m;
  const auto& a = l.world;
  const auto& b = m.world;
  return valid_limits(l.support_self) && l.source_bytes <= m.source_bytes &&
         l.output_bytes <= m.output_bytes && a.pair_axes <= b.pair_axes &&
         a.roster_entries <= b.roster_entries &&
         a.effective_sources <= b.effective_sources &&
         a.union_envelope_pairs <= b.union_envelope_pairs &&
         a.union_proxy_preparations <= b.union_proxy_preparations &&
         a.domain_union_checks <= b.domain_union_checks &&
         a.domain_cell_checks <= b.domain_cell_checks &&
         a.proxy_preparations <= b.proxy_preparations &&
         a.refined_envelope_pairs <= b.refined_envelope_pairs &&
         a.material_triangle_visits <= b.material_triangle_visits &&
         a.halo_metadata_visits <= b.halo_metadata_visits &&
         a.halo_triangle_visits <= b.halo_triangle_visits &&
         a.triangle_union_pairs <= b.triangle_union_pairs &&
         a.refined_triangle_pairs <= b.refined_triangle_pairs &&
         a.base_enclosure_relations <= b.base_enclosure_relations &&
         a.refined_enclosure_relations <= b.refined_enclosure_relations &&
         a.axes <= b.axes &&
         a.direction_entries_prepared <= b.direction_entries_prepared &&
         a.sole_vertex_guards <= b.sole_vertex_guards &&
         a.boundary_witness_attempts <= b.boundary_witness_attempts &&
         a.boundary_signed_support_calls <= b.boundary_signed_support_calls &&
         a.boundary_width_attempts <= b.boundary_width_attempts;
}
auto root_event(std::size_t phase, bool reverse) -> Event {
  if (phase == 0)
    return reverse ? Event::upper_reacquisition : Event::upper_release;
  if (phase == 1) return Event::unloaded_motion;
  if (phase == 2)
    return reverse ? Event::unloaded_motion : Event::intermediate_reacquisition;
  if (phase == 3)
    return reverse ? Event::intermediate_unload : Event::load_acquisition;
  return Event::supported_pause;
}
auto root_metadata(RR& r, Pending p, std::optional<std::size_t> phase,
                   bool reverse) -> void {
  r.first = p.first;
  r.last = p.last;
  r.depth = p.depth;
  r.phase_index = phase;
  if (phase) {
    r.local_first =
        detail::boarding_route_intermediate_root_z01_local(*phase, p.first);
    r.local_last =
        detail::boarding_route_intermediate_root_z01_local(*phase, p.last);
    r.event = root_event(*phase, reverse);
  }
  r.support_self.first = r.first;
  r.support_self.last = r.last;
  r.support_self.depth = r.depth;
  r.support_self.phase_index = r.phase_index;
  r.support_self.local_first = r.local_first;
  r.support_self.local_last = r.local_last;
}
} // namespace
OriginBoardingRouteIntermediateRootZ01::OriginBoardingRouteIntermediateRootZ01(
    std::shared_ptr<const Data> d)
    : data_(std::move(d)) {
}
auto OriginBoardingRouteIntermediateRootZ01::summary() const
    -> const BoardingRouteIntermediateRootZ01ConstructorEvidence* {
  return data_ ? &data_->evidence : nullptr;
}
auto detail::BoardingRouteIntermediateRootZ01Access::data(
    const OriginBoardingRouteIntermediateRootZ01& p)
    -> const OriginBoardingRouteIntermediateRootZ01::Data* {
  return p.data_.get();
}
auto detail::BoardingRouteIntermediateRootZ01Access::valid(
    const OriginBoardingRouteIntermediateRootZ01& p) -> bool {
  const auto* d = data(p);
  return d && d->evidence.version == 1 && d->evidence.authenticated &&
         d->evidence.same_base && d->evidence.controls_authenticated &&
         d->evidence.required_source_bytes == 1024 &&
         d->evidence.actual_source_bytes == 1024 &&
         std::all_of(d->evidence.input_identity_checked.begin(),
                     d->evidence.input_identity_checked.end(),
                     [](bool x) { return x; }) &&
         std::all_of(d->evidence.control_identity_checked.begin(),
                     d->evidence.control_identity_checked.end(),
                     [](bool x) { return x; }) &&
         std::all_of(d->evidence.input_identity_valid.begin(),
                     d->evidence.input_identity_valid.end(),
                     [](bool x) { return x; }) &&
         std::all_of(d->evidence.control_identity_valid.begin(),
                     d->evidence.control_identity_valid.end(),
                     [](bool x) { return x; });
}
#define ROOT_Z01_SOURCE_VIEW(name, type)                                       \
  auto detail::BoardingRouteIntermediateRootZ01Access::name(                   \
      const OriginBoardingRouteIntermediateRootZ01& p) -> const type* {        \
    const auto* d = data(p);                                                   \
    return valid(p) ? &d->name : nullptr;                                      \
  }
ROOT_Z01_SOURCE_VIEW(binding, NativeCraftBinding)
ROOT_Z01_SOURCE_VIEW(boots, OriginBoardingBootSupport)
ROOT_Z01_SOURCE_VIEW(pause, OriginBoardingIntermediatePauseSupport)
ROOT_Z01_SOURCE_VIEW(material, OriginBoardingInitialMaterial)
ROOT_Z01_SOURCE_VIEW(extension, OriginBoardingCheckpointMaterialExtension)
ROOT_Z01_SOURCE_VIEW(seal, OriginBoardingHatchSealMaterial)
ROOT_Z01_SOURCE_VIEW(ring, OriginBoardingSeparationRingMaterial)
#undef ROOT_Z01_SOURCE_VIEW

auto detail::BoardingRouteIntermediateRootZ01Access::make(
    const NativeCraftBinding& binding, const OriginBoardingBootSupport& boots,
    const OriginBoardingIntermediatePauseSupport& pause,
    const OriginBoardingInitialMaterial& material,
    const OriginBoardingCheckpointMaterialExtension& extension,
    const OriginBoardingHatchSealMaterial& seal,
    const OriginBoardingSeparationRingMaterial& ring,
    BoardingRouteIntermediateRootZ01ConstructorLimits l,
    BoardingRouteIntermediateRootZ01ConstructorEvidence* out)
    -> std::expected<OriginBoardingRouteIntermediateRootZ01, std::string> {
  BoardingRouteIntermediateRootZ01ConstructorEvidence e;
  e.required_source_bytes = kBoardingRouteIntermediateRootZ01MaximumSourceBytes;
  const auto error = [&](RW why, const char* text)
      -> std::expected<OriginBoardingRouteIntermediateRootZ01, std::string> {
    e.condition = why;
    if (out) *out = e;
    return std::unexpected(text);
  };
  if (l.source_bytes > 1024 || l.base_guards > 64 || l.control_guards > 64)
    return error(RW::invalid_limits, "RootZ01 lowered creator limits required");
  if (l.source_bytes < 1024)
    return error(RW::source_capacity, "RootZ01 source allocation capacity");
  e.arithmetic_supported = boarding_route_foot_phase_environment();
  if (!e.arithmetic_supported)
    return error(RW::unsupported_arithmetic,
                 "RootZ01 supported arithmetic required");
  for (std::size_t i = 0; i < 7; ++i) {
    if (e.base_guards >= l.base_guards)
      return error(RW::source_capacity, "RootZ01 base guard capacity");
    ++e.base_guards;
    e.input_identity_checked[i] = true;
    bool ok = false;
    switch (i) {
      case 0:
        ok = binding.contact() && binding.selection() &&
             binding.selection()->hardware == OperatingProgress{1, 1, 1, 0};
        break;
      case 1:
        ok = boots.contact() && boots.selected_partitions().size() == 10;
        break;
      case 2: ok = root_z01_pause_base_matches(pause, binding, boots); break;
      case 3: ok = root_z01_material_base_matches(binding, material); break;
      case 4:
        ok = BoardingCheckpointMaterialExtensionAccess::valid(extension,
                                                              material);
        break;
      case 5:
        ok = BoardingHatchSealMaterialAccess::valid(seal, material);
        break;
      case 6:
        ok = BoardingSeparationRingMaterialAccess::valid(ring, material);
        break;
      default: break;
    }
    e.input_identity_valid[i] = ok;
    if (!ok)
      return error(RW::invalid_binding,
                   "RootZ01 genuine same-base sources required");
  }
  e.same_base = true;
  for (std::size_t i = 0; i < 5; ++i) {
    if (e.control_guards >= l.control_guards)
      return error(RW::source_capacity, "RootZ01 control guard capacity");
    ++e.control_guards;
    e.control_identity_checked[i] = true;
    const auto r = boarding_route_intermediate_root_z01_controls(i);
    e.control_identity_valid[i] =
        r && canonical_equal(*r, i) && r->root[0].coordinates[2].count == 2 &&
        r->root[1].coordinates[2].count == 2 &&
        r->root[0].coordinates[2].terms ==
            std::array<double, 3>{-.55, -.17, 0} &&
        r->root[1].coordinates[2].terms == std::array<double, 3>{-.55, -.17, 0};
    if (!e.control_identity_valid[i])
      return error(RW::control_identity,
                   "RootZ01 frozen root-Z recipe required");
  }
  e.controls_authenticated = e.authenticated = true;
  e.actual_source_bytes = 1024;
  try {
    auto d = std::allocate_shared<OriginBoardingRouteIntermediateRootZ01::Data>(
        RootZ01ArenaAllocator<OriginBoardingRouteIntermediateRootZ01::Data>{},
        binding, boots, pause, material, extension, seal, ring, e);
    if (out) *out = e;
    return OriginBoardingRouteIntermediateRootZ01(std::move(d));
  } catch (const std::bad_alloc&) {
    e.actual_source_bytes = 0;
    e.authenticated = false;
    return error(RW::allocation_capacity, "RootZ01 source allocation failed");
  }
}
auto make_origin_boarding_route_intermediate_root_z01(
    const NativeCraftBinding& b, const OriginBoardingBootSupport& s,
    const OriginBoardingIntermediatePauseSupport& p,
    const OriginBoardingInitialMaterial& m,
    const OriginBoardingCheckpointMaterialExtension& e,
    const OriginBoardingHatchSealMaterial& h,
    const OriginBoardingSeparationRingMaterial& r)
    -> std::expected<OriginBoardingRouteIntermediateRootZ01, std::string> {
  return RA::make(b, s, p, m, e, h, r);
}
auto detail::boarding_route_intermediate_root_z01_controls(std::size_t phase)
    -> std::optional<Request> {
  auto r = phase < 3   ? boarding_route_intermediate_unloaded01_controls(phase)
           : phase < 5 ? boarding_route_intermediate_load01_controls(phase)
                       : std::nullopt;
  if (r)
    for (auto& root : r->root)
      root.coordinates[2] = BoardingRoutePhaseConstant{{-.55, -.17, 0}, 2};
  return r;
}
auto detail::boarding_route_intermediate_root_z01_phase(double first,
                                                        double last)
    -> std::optional<std::size_t> {
  return boarding_route_intermediate_support_self01_phase(first, last);
}
auto detail::boarding_route_intermediate_root_z01_local(std::size_t phase,
                                                        double g) -> double {
  return boarding_route_intermediate_support_self01_local(phase, g);
}
auto detail::boarding_route_intermediate_root_z01_clock(double g) -> double {
  return boarding_route_intermediate_support_self01_clock(g);
}
} // namespace apsis_drift

namespace apsis_drift {
auto detail::BoardingRouteIntermediateRootZ01CurrentAccess::valid(
    const BoardingRouteIntermediateRootZ01Context& ctx, const RD& d,
    const Request& request) -> bool {
  return ctx.owner_ == &d && ctx.data_ && ctx.data_ == RA::data(d.source) &&
         RA::valid(d.source) && ctx.parts_ == &d.parts &&
         ctx.request_ == &request && ctx.epoch_ &&
         *ctx.epoch_ == ctx.expected_epoch_ && d.source_enrolled &&
         std::all_of(d.source_evaluated.begin(), d.source_evaluated.end(),
                     [](bool x) { return x; });
}
auto detail::BoardingRouteIntermediateRootZ01CurrentAccess::valid(
    const BoardingRouteIntermediateRootZ01CurrentToken& t, const RD& d,
    const RC& c) -> bool {
  return t.context_ && t.owner_ == &d && t.cell_ == &c && t.request_ &&
         t.data_ == RA::data(d.source) && valid(*t.context_, d, *t.request_) &&
         canonical_equal(*t.request_, c.support_self.phase_index) &&
         boarding_route_intermediate_root_z01_phase(
             c.support_self.global_first, c.support_self.global_last) ==
             c.support_self.phase_index &&
         c.support_self.phase.first ==
             boarding_route_intermediate_root_z01_local(
                 c.support_self.phase_index, c.support_self.global_first) &&
         c.support_self.phase.last ==
             boarding_route_intermediate_root_z01_local(
                 c.support_self.phase_index, c.support_self.global_last) &&
         t.request_->seconds_per_parameter ==
             (c.support_self.phase_index == 4 ? 2 : 12) &&
         c.support_self.phase.complete &&
         c.support_self.phase.arithmetic_supported &&
         c.support_self.projection_complete && c.fresh_control_identity;
}
auto detail::BoardingRouteIntermediateRootZ01CurrentAccess::request(
    const BoardingRouteIntermediateRootZ01CurrentToken& t) -> const Request* {
  return t.owner_ && t.cell_ && valid(t, *t.owner_, *t.cell_) ? t.request_
                                                              : nullptr;
}
auto detail::boarding_route_intermediate_root_z01_current_cell(
    const BoardingRouteIntermediateRootZ01Context& context, RD& owner, RC& cell,
    const RL& limits, RR& reason) -> RS {
  auto& c = cell.support_self;
  auto& r = reason.support_self;
  const auto& l = limits.support_self;
  // WORLD renewal uses the exact supplied borrow and the already earned cell.
  // It issues no graph/support call and resets no shared operation ledger.
  if (cell.world.complete) {
    if (owner.work.support_self.event_guards >= l.event_guards)
      return root_refuse(owner, reason, RW::work_capacity, RG::world,
                         RS::capacity);
    ++owner.work.support_self.event_guards;
    const auto* request =
        BoardingRouteIntermediateRootZ01CurrentAccess::request(context);
    if (!request ||
        !BoardingRouteIntermediateRootZ01CurrentAccess::valid(context, owner,
                                                              *request) ||
        !cell.self_complete || !cell.contact_complete || !cell.load_complete ||
        !canonical_equal(*request, c.phase_index))
      return root_refuse(owner, reason, RW::invalid_binding, RG::world,
                         RS::identity);
    const BoardingRouteIntermediateRootZ01CurrentToken token(context, owner,
                                                             cell, *request);
    return boarding_route_intermediate_root_z01_world_bridge(token, owner, cell,
                                                             limits, reason);
  }
  // Charge the original first definition guard before any borrowed source.
  if (owner.work.support_self.definition_guards >= l.definition_guards)
    return root_refuse(owner, reason, RW::work_capacity, RG::phase,
                       RS::capacity);
  ++owner.work.support_self.definition_guards;
  c.definition_evaluated[0] = true;
  if (!context.request_ ||
      !BoardingRouteIntermediateRootZ01CurrentAccess::valid(context, owner,
                                                            *context.request_))
    return root_refuse(owner, reason, RW::invalid_binding, RG::phase,
                       RS::identity);
  const auto* pause = RA::pause(owner.source);
  if (!pause)
    return root_refuse(owner, reason, RW::invalid_binding, RG::phase,
                       RS::identity);
  D d(owner, *pause);
  const auto& request = *context.request_;
  const auto project = [&]() -> S {
    if (!guard(
            d, c, l, r, [] { return boarding_route_foot_phase_environment(); },
            W::unsupported_arithmetic))
      return d.state;
    if (!guard(
            d, c, l, r,
            [&] {
              return boarding_route_intermediate_root_z01_phase(
                         c.global_first, c.global_last) == c.phase_index &&
                     request.seconds_per_parameter ==
                         (c.phase_index == 4 ? 2 : 12);
            },
            W::phase_prerequisite))
      return d.state;
    if (!root_z01_definition_charge(d, c, l, r)) return d.state;
    BoardingRouteFootPhaseRefusal reason;
    ++d.work.phase_calls;
    const auto status = boarding_route_foot_phase_cell(
        request,
        boarding_route_intermediate_root_z01_local(c.phase_index,
                                                   c.global_first),
        boarding_route_intermediate_root_z01_local(c.phase_index,
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
    if (c.phase_index < 3) {
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
                return b &&
                       b->half_size_metres == RigidVector3{.06, .05, .14} &&
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
    }
    return S::accepted;
  };
  const auto projected = project();
  if (projected != S::accepted)
    return root_support_result(owner, reason, d, RG::phase);
  cell.fresh_control_identity = canonical_equal(request, c.phase_index);
  if (!cell.fresh_control_identity)
    return root_refuse(owner, reason, RW::control_identity, RG::phase,
                       RS::identity);
  const BoardingRouteIntermediateRootZ01CurrentToken token(context, owner, cell,
                                                           request);
  const auto supported = boarding_route_intermediate_root_z01_support_bridge(
      token, owner, cell, limits, reason);
  if (supported != RS::accepted) return supported;
  return boarding_route_intermediate_root_z01_self_bridge(token, owner, cell,
                                                          limits, reason);
}
auto detail::boarding_route_intermediate_root_z01_support_bridge(
    const BoardingRouteIntermediateRootZ01CurrentToken& token, RD& owner,
    RC& cell, const RL& l, RR& r) -> RS {
  if (owner.work.support_self.event_guards >= l.support_self.event_guards)
    return root_refuse(owner, r, RW::work_capacity, RG::contact, RS::capacity);
  ++owner.work.support_self.event_guards;
  if (!BoardingRouteIntermediateRootZ01CurrentAccess::valid(token, owner, cell))
    return root_refuse(owner, r, RW::invalid_binding, RG::contact,
                       RS::identity);
  const auto* request =
      BoardingRouteIntermediateRootZ01CurrentAccess::request(token);
  const auto* pause = RA::pause(owner.source);
  D borrow(owner, *pause);
  const auto state =
      cell.support_self.phase_index < 3
          ? root_z01_prefix_math(*request, borrow, cell.support_self,
                                 l.support_self, r.support_self)
          : root_z01_load_math(*request, borrow, cell.support_self,
                               l.support_self, r.support_self);
  cell.support_self.complete = false;
  if (state != S::accepted)
    return root_support_result(owner, r, borrow,
                               cell.support_self.phase_index < 3 ? RG::contact
                                                                 : RG::load);
  cell.contact_complete = cell.support_self.star_support &&
                          (cell.support_self.phase_index < 3
                               ? cell.support_self.prefix_zero_geometry_complete
                               : cell.support_self.port_contact_geometry);
  cell.load_complete = cell.support_self.nominal_support_complete &&
                       cell.support_self.nominal_equilibrium &&
                       cell.support_self.star_everywhere_positive;
  if (!cell.contact_complete || !cell.load_complete)
    return root_refuse(owner, r, RW::contact_refused, RG::contact,
                       RS::unresolved, false);
  return RS::accepted;
}
auto detail::boarding_route_intermediate_root_z01_self_bridge(
    const BoardingRouteIntermediateRootZ01CurrentToken& token, RD& owner,
    RC& cell, const RL& l, RR& r) -> RS {
  if (owner.work.support_self.event_guards >= l.support_self.event_guards)
    return root_refuse(owner, r, RW::work_capacity, RG::self, RS::capacity);
  ++owner.work.support_self.event_guards;
  if (!BoardingRouteIntermediateRootZ01CurrentAccess::valid(token, owner,
                                                            cell) ||
      !cell.contact_complete || !cell.load_complete)
    return root_refuse(owner, r, RW::invalid_binding, RG::self, RS::identity);
  D borrow(owner, *RA::pause(owner.source));
  const auto state = root_z01_self_math(
      *BoardingRouteIntermediateRootZ01CurrentAccess::request(token), borrow,
      cell.support_self, l.support_self, r.support_self);
  if (state != S::accepted)
    return root_support_result(owner, r, borrow, RG::self);
  cell.self_complete =
      cell.support_self.self_body_complete && cell.support_self.self_complete;
  return cell.self_complete ? RS::accepted
                            : root_refuse(owner, r, RW::self_refused, RG::self,
                                          RS::unresolved, false);
}
} // namespace apsis_drift

namespace apsis_drift {
auto detail::BoardingRouteIntermediateRootZ01CurrentAccess::request(
    const BoardingRouteIntermediateRootZ01Context& c) -> const Request* {
  if (!c.owner_ || !c.request_ || !valid(c, *c.owner_, *c.request_))
    return nullptr;
  return c.request_;
}
auto detail::boarding_route_intermediate_root_z01_current_proxy(
    const BoardingRouteIntermediateRootZ01Context& context, RD& d, RC& c,
    std::size_t part, const RL& l, RR& r)
    -> std::expected<BoardingRouteCheckpointWorldProxy, std::string> {
  if (d.work.world.proxy_preparations >= l.world.proxy_preparations) {
    root_refuse(d, r, RW::work_capacity, RG::world, RS::capacity, false);
    return std::unexpected("RootZ01 proxy preparation capacity");
  }
  ++d.work.world.proxy_preparations;
  const auto* request =
      BoardingRouteIntermediateRootZ01CurrentAccess::request(context);
  if (!request ||
      !BoardingRouteIntermediateRootZ01CurrentAccess::valid(context, d,
                                                            *request) ||
      part >= d.parts.size() || !c.contact_complete || !c.load_complete ||
      !c.self_complete || !c.support_self.self_body_complete ||
      !c.support_self.self_complete || c.support_self.accepted_pairs != 105 ||
      !canonical_equal(*request, c.support_self.phase_index) ||
      boarding_route_intermediate_root_z01_phase(c.support_self.global_first,
                                                 c.support_self.global_last) !=
          c.support_self.phase_index) {
    root_refuse(d, r, RW::invalid_binding, RG::world, RS::identity, false);
    return std::unexpected("RootZ01 fresh staged proxy identity required");
  }
  bool owned = false;
  for (const auto& current : d.cells)
    if (&current == &c) {
      owned = true;
      break;
    }
  if (!owned) {
    root_refuse(d, r, RW::invalid_binding, RG::world, RS::identity, false);
    return std::unexpected("RootZ01 actual retained cell required");
  }
  const BoardingRouteIntermediateRootZ01CurrentToken token(context, d, c,
                                                           *request);
  return root_z01_proxy_math(token, d, c, part);
}
auto detail::boarding_route_intermediate_root_z01_world_bridge(
    const BoardingRouteIntermediateRootZ01CurrentToken& t, RD& d, RC& c,
    const RL&, RR& r) -> RS {
  if (!BoardingRouteIntermediateRootZ01CurrentAccess::valid(t, d, c) ||
      !c.contact_complete || !c.load_complete || !c.self_complete)
    return root_refuse(d, r, RW::invalid_binding, RG::world, RS::identity);
  if (!c.world.complete || !c.world.arithmetic_supported ||
      !std::all_of(c.world.domain_complete.begin(),
                   c.world.domain_complete.end(), [](bool x) { return x; }) ||
      !std::all_of(c.world.material_complete.begin(),
                   c.world.material_complete.end(), [](bool x) { return x; }) ||
      !std::all_of(c.world.halo_complete.begin(), c.world.halo_complete.end(),
                   [](bool x) { return x; }))
    return root_refuse(d, r, RW::world_refused, RG::world, RS::unresolved);
  c.complete = true;
  return RS::accepted;
}
} // namespace apsis_drift

namespace apsis_drift {
namespace {
using RootZ01CellIssuer = RS (*)(void*, RC&, RR&);
[[gnu::noinline]] auto root_z01_reset_cell(RC& cell) -> void {
  // Keep the complete reset temporary out of the phase-evaluation call stack.
  cell = RC{};
}
[[gnu::noinline]] auto root_z01_cover(RD& d, Request& request, const RL& l,
                                      void* issue_context,
                                      RootZ01CellIssuer issue_cell) -> bool {
  const auto first = d.requested_first, last = d.requested_last;
  RR reason;
  std::array<Pending, 11> pending{};
  pending[0] = {std::min(first, last), std::max(first, last), 0};
  std::size_t count = 1;
  RC cell;
  while (count) {
    const auto p = pending[--count];
    reason = RR{};
    const auto phase =
        detail::boarding_route_intermediate_root_z01_phase(p.first, p.last);
    root_metadata(reason, p, phase, d.reverse);
    if (d.examined_nodes >= l.support_self.phase.nodes) {
      root_refuse(d, reason, RW::node_capacity, RG::coverage, RS::capacity);
      return false;
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
    const auto split = [&](double mid) {
      if (count + 2 > pending.size()) return false;
      if (d.reverse) {
        pending[count++] = {p.first, mid, p.depth + 1};
        pending[count++] = {mid, p.last, p.depth + 1};
      } else {
        pending[count++] = {mid, p.last, p.depth + 1};
        pending[count++] = {p.first, mid, p.depth + 1};
      }
      return true;
    };
    if (cut) {
      if (p.depth >= l.support_self.phase.depth) {
        root_refuse(d, reason, RW::depth_capacity, RG::coverage, RS::capacity);
        return false;
      }
      if (!split(*cut)) {
        root_refuse(d, reason, RW::unsplittable_interval, RG::coverage,
                    RS::unresolved);
        return false;
      }
      ++d.mandatory_splits;
      continue;
    }
    if (d.cells.size() >= l.support_self.phase.leaves) {
      root_refuse(d, reason, RW::leaf_capacity, RG::coverage, RS::capacity);
      return false;
    }
    if (d.cells.size() >= d.cells.capacity()) {
      root_refuse(d, reason, RW::output_capacity, RG::coverage, RS::capacity);
      return false;
    }
    if (!phase) {
      root_refuse(d, reason, RW::phase_refused, RG::phase, RS::identity);
      return false;
    }
    {
      const auto control =
          detail::boarding_route_intermediate_root_z01_controls(*phase);
      if (!control) {
        root_refuse(d, reason, RW::control_identity, RG::phase, RS::identity);
        return false;
      }
      request = *control;
    }
    root_z01_reset_cell(cell);
    auto& c = cell.support_self;
    c.global_first = p.first;
    c.global_last = p.last;
    c.phase_index = *phase;
    c.protocol =
        *phase < 3
            ? BoardingRouteIntermediateSupportSelf01Protocol::unloaded_prefix
            : BoardingRouteIntermediateSupportSelf01Protocol::load_acquisition;
    cell.event = root_event(*phase, d.reverse);
    d.state = RS::not_run;
    d.stop_condition = RW::none;
    d.stop_stage = RG::not_run;
    const auto state = issue_cell(issue_context, cell, reason);
    if (state == RS::accepted) {
      d.cells.push_back(cell);
      for (std::size_t k = 0; k < 3 && *phase < 3; ++k) {
        d.prefix_endpoint_scope[k] =
            d.prefix_endpoint_scope[k] || c.endpoint_scope[k];
        d.prefix_endpoint_earned[k] =
            d.prefix_endpoint_earned[k] || c.endpoint_earned[k];
      }
      continue;
    }
    if (state == RS::capacity || state == RS::unsupported ||
        state == RS::identity)
      return false;
    if (p.first == p.last) {
      root_refuse(d, reason, RW::unsplittable_interval, reason.stage,
                  RS::unresolved);
      return false;
    }
    if (p.depth >= l.support_self.phase.depth) {
      root_refuse(d, reason, RW::depth_capacity, reason.stage, RS::capacity);
      return false;
    }
    const auto mid = p.first + (p.last - p.first) * .5;
    if (!(mid > p.first && mid < p.last) || !split(mid)) {
      root_refuse(d, reason, RW::unsplittable_interval, reason.stage,
                  RS::unresolved);
      return false;
    }
  }
  return true;
}
} // namespace
auto detail::boarding_route_intermediate_root_z01_bounded(
    const OriginBoardingRouteIntermediateRootZ01& provider, double first,
    double last, RL l) -> RE {
  if (!std::isfinite(first) || !std::isfinite(last) ||
      std::min(first, last) < 0 || std::max(first, last) > 1)
    return std::unexpected("RootZ01 finite [0,1] range required");
  if (!root_limits_valid(l))
    return std::unexpected("RootZ01 lowered registered limits required");
  if (!boarding_route_foot_phase_environment())
    return std::unexpected("RootZ01 supported arithmetic required");
  const auto output =
      std::min(l.output_bytes, l.support_self.phase.output_bytes);
  if (output < sizeof(RE))
    return std::unexpected("RootZ01 fixed output capacity");
  RE expected(std::in_place, provider);
  auto& d = *expected;
  d.requested_first = first;
  d.requested_last = last;
  d.reverse = first > last;
  d.arithmetic_supported = true;
  d.output_capacity_bytes = sizeof(RE);
  RR reason;
  root_metadata(reason, {std::min(first, last), std::max(first, last), 0}, {},
                d.reverse);
  if (output < root_fixed_output) {
    root_refuse(d, reason, RW::output_capacity, RG::preflight, RS::capacity);
    return expected;
  }
  d.output_capacity_bytes = root_fixed_output;
  // S0 is the first source attempt. In particular, source_guards0 reads no
  // provider Data, Pause, part catalog, source partition, or request control.
  if (d.work.support_self.source_guards >= l.support_self.source_guards) {
    root_refuse(d, reason, RW::source_capacity, RG::source, RS::capacity);
    return expected;
  }
  ++d.work.support_self.source_guards;
  d.source_evaluated[0] = true;
  if (l.source_bytes < kBoardingRouteIntermediateRootZ01MaximumSourceBytes) {
    root_refuse(d, reason, RW::source_capacity, RG::source, RS::capacity);
    return expected;
  }
  if (!RA::valid(provider)) {
    root_refuse(d, reason, RW::invalid_binding, RG::source, RS::identity);
    return expected;
  }
  const auto* pause = RA::pause(provider);
  if (!pause) {
    root_refuse(d, reason, RW::invalid_binding, RG::source, RS::identity);
    return expected;
  }
  {
    D borrow(d, *pause);
    if (!enroll(borrow, l.support_self, reason.support_self)) {
      const auto capacity = borrow.state == S::capacity;
      const auto unsupported = borrow.state == S::unsupported;
      root_refuse(d, reason,
                  capacity      ? RW::source_capacity
                  : unsupported ? RW::unsupported_arithmetic
                                : RW::source_identity,
                  RG::source,
                  capacity      ? RS::capacity
                  : unsupported ? RS::unsupported
                                : RS::identity);
      return expected;
    }
  }
  const auto slots = std::min(l.support_self.phase.leaves,
                              (output - root_fixed_output) / sizeof(RC));
  try {
    d.cells.reserve(slots);
  } catch (const std::bad_alloc&) {
    root_refuse(d, reason, RW::allocation_capacity, RG::preflight,
                RS::capacity);
    return expected;
  }
  if (d.cells.capacity() > (output - root_fixed_output) / sizeof(RC)) {
    root_refuse(d, reason, RW::output_capacity, RG::preflight, RS::capacity);
    return expected;
  }
  d.output_capacity_bytes = root_fixed_output + d.cells.capacity() * sizeof(RC);
  Request request;
  std::size_t epoch{};
  struct CellController {
    RD& owner;
    Request& request;
    std::size_t& epoch;
    const RL& limits;
  };
  CellController cell_controller{d, request, epoch, l};
  const auto issue_cell = +[](void* opaque, RC& cell, RR& reason) -> RS {
    auto& state = *static_cast<CellController*>(opaque);
    ++state.epoch;
    const BoardingRouteIntermediateRootZ01Context context(
        state.owner, state.request, state.epoch);
    return boarding_route_intermediate_root_z01_current_cell(
        context, state.owner, cell, state.limits, reason);
  };
  if (!root_z01_cover(d, request, l, &cell_controller, issue_cell))
    return expected;
  // Graph/support/SELF scratch has ended. No second child report or cover is
  // created. This callback renews only the exact retained cell/recipe/epoch.
  struct ProxyController {
    RD& owner;
    Request& request;
    std::size_t& epoch;
    const RL& limits;
  } proxy_controller{d, request, epoch, l};
  const auto get_proxy =
      +[](void* context, std::size_t cell, std::size_t part, RR& r)
      -> std::expected<BoardingRouteCheckpointWorldProxy, std::string> {
    auto& controller = *static_cast<ProxyController*>(context);
    auto& owner = controller.owner;
    if (cell >= owner.cells.size())
      return std::unexpected("RootZ01 retained cell ordinal required");
    {
      const auto control = boarding_route_intermediate_root_z01_controls(
          owner.cells[cell].support_self.phase_index);
      if (!control) return std::unexpected("RootZ01 retained recipe required");
      controller.request = *control;
    }
    ++controller.epoch;
    const BoardingRouteIntermediateRootZ01Context current(
        owner, controller.request, controller.epoch);
    return boarding_route_intermediate_root_z01_current_proxy(
        current, owner, owner.cells[cell], part, controller.limits, r);
  };
  reason = RR{};
  root_metadata(reason, {std::min(first, last), std::max(first, last), 0}, {},
                d.reverse);
  const auto world =
      root_z01_world_sweep(d, l, &proxy_controller, get_proxy, reason);
  if (world != RS::accepted) {
    root_refuse(d, reason, reason.condition, RG::world, world);
    return expected;
  }
  double cursor = first;
  bool cover = !d.cells.empty();
  for (std::size_t i = 0; i < d.cells.size(); ++i) {
    auto& c = d.cells[i];
    const auto& p = c.support_self;
    reason = RR{};
    root_metadata(reason, {p.global_first, p.global_last, 0}, p.phase_index,
                  d.reverse);
    reason.cell = i;
    {
      const auto control =
          boarding_route_intermediate_root_z01_controls(p.phase_index);
      if (!control) {
        cover = false;
        break;
      }
      request = *control;
    }
    ++epoch;
    {
      const BoardingRouteIntermediateRootZ01Context context(d, request, epoch);
      const auto closed = boarding_route_intermediate_root_z01_current_cell(
          context, d, c, l, reason);
      if (closed != RS::accepted) return expected;
    }
    cover = cover && c.complete && p.phase.complete && p.projection_complete &&
            p.self_body_complete && p.self_complete &&
            p.accepted_pairs == 105 &&
            (d.reverse ? p.global_last == cursor : p.global_first == cursor);
    cursor = d.reverse ? p.global_first : p.global_last;
    if (i) {
      const auto& previous = d.cells[i - 1].support_self;
      for (std::size_t j = 0; j < 4; ++j)
        if (std::min(first, last) < joins[j] &&
            std::max(first, last) > joins[j] && d.join_expression_identity[j] &&
            (d.reverse ? previous.phase_index == j + 1 && p.phase_index == j &&
                             previous.global_first == joins[j] &&
                             p.global_last == joins[j]
                       : previous.phase_index == j && p.phase_index == j + 1 &&
                             previous.global_last == joins[j] &&
                             p.global_first == joins[j]))
          d.qualified_joins[j] = true;
    }
  }
  cover = cover && cursor == last;
  for (std::size_t j = 0; j < 4; ++j)
    if (std::min(first, last) < joins[j] && std::max(first, last) > joins[j])
      cover = cover && d.join_expression_identity[j] && d.qualified_joins[j];
  if (!cover) {
    root_refuse(d, reason, RW::incomplete_cover, RG::coverage, RS::unresolved);
    return expected;
  }
  d.kinematic_complete = d.contact_complete = d.nominal_support_complete =
      d.self_complete = d.world_complete = true;
  d.complete = d.route_qualified = true;
  d.state = RS::accepted;
  d.stop_condition = RW::none;
  d.stop_stage = RG::not_run;
  return expected;
}
auto assess_origin_boarding_route_intermediate_root_z01(
    const OriginBoardingRouteIntermediateRootZ01& p, double first, double last)
    -> RE {
  return detail::boarding_route_intermediate_root_z01_bounded(p, first, last);
}
auto boarding_route_intermediate_root_z01_required_output_bytes()
    -> std::size_t {
  return root_fixed_output;
}
} // namespace apsis_drift
