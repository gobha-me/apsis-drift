#include "origin_boarding_route_intermediate_hip_diagnostic01_internal.hpp"
#include "origin_boarding_route_intermediate_support_self01_internal.hpp"
#include "origin_boarding_route_intermediate_unloaded01_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
namespace apsis_drift {
namespace {
using D = BoardingRouteIntermediateHipDiagnostic01Diagnostic;
using S = BoardingRouteIntermediateHipDiagnostic01State;
using W = BoardingRouteIntermediateHipDiagnostic01Condition;
using Error = BoardingRouteIntermediateHipDiagnostic01Error;
using B = BoardingRouteIntermediateHipDiagnostic01Scalar;
using G = BoardingRouteIntermediateHipDiagnostic01GeometryInput;
using DC = BoardingRouteIntermediateHipDiagnostic01DualCandidate;
using PC = BoardingRouteIntermediateHipDiagnostic01PrimalCandidate;
using DM = BoardingRouteIntermediateHipDiagnostic01DualMath;
using PM = BoardingRouteIntermediateHipDiagnostic01PrimalMath;
using Case = BoardingRouteIntermediateHipDiagnostic01Case;
using Request = BoardingRouteFootPhaseRequest;
using L = detail::BoardingRouteIntermediateHipDiagnostic01Limits;
using A = detail::BoardingIntermediatePauseSupportAccess;
using E = std::expected<D, Error>;
enum class Op : std::uint8_t { add, sub, mul, div, neg, abs, min, max };
constexpr double infinity = std::numeric_limits<double>::infinity();
constexpr double largest = std::numeric_limits<double>::max();
constexpr std::size_t fixed_output = 2 * sizeof(E);
constexpr std::size_t graph_live =
    32768 + 4096 + 2 * sizeof(E) + sizeof(Request) +
    sizeof(std::optional<Request>) + 4 * sizeof(L) +
    sizeof(BoardingRouteIntermediateHipDiagnostic01Refusal) +
    sizeof(detail::BoardingRouteIntermediateHipDiagnostic01AdmissionContext) +
    2048 + sizeof(detail::BoardingRouteFootPhaseLimits) +
    sizeof(BoardingRouteFootPhaseRefusal) +
    sizeof(detail::BoardingRouteIntermediateHipDiagnostic01CurrentToken) +
    sizeof(BoardingRouteFootPhaseCounters) + sizeof(std::size_t);
static_assert(sizeof(E) <= 2048);
static_assert(sizeof(L) == 120);
static_assert(graph_live <= 49152);
auto finite(double x) -> bool {
  return std::isfinite(x);
}
auto valid(B x) -> bool {
  return x.supported && finite(x.lower) && finite(x.upper) &&
         x.lower <= x.upper;
}
auto point(double x) -> B {
  return {x, x, true};
}
auto checked(double a, double b, Op op, double& out) -> bool {
  if (!finite(a) || !finite(b)) return false;
  const double aa = std::abs(a), bb = std::abs(b);
  switch (op) {
    case Op::add:
      if (a != 0 && b != 0 && std::signbit(a) == std::signbit(b) &&
          aa > std::nextafter(largest - bb, 0.0))
        return false;
      out = a + b;
      break;
    case Op::sub:
      if (a != 0 && b != 0 && std::signbit(a) != std::signbit(b) &&
          aa > std::nextafter(largest - bb, 0.0))
        return false;
      out = a - b;
      break;
    case Op::mul:
      if (a != 0 && b != 0 && bb >= 1 && aa > std::nextafter(largest / bb, 0.0))
        return false;
      out = a * b;
      break;
    case Op::div:
      if (b == 0) return false;
      if (a != 0 && bb < 1 && aa > std::nextafter(largest * bb, 0.0))
        return false;
      out = a / b;
      break;
    case Op::neg: out = -a; break;
    case Op::abs: out = aa; break;
    case Op::min: out = std::min(a, b); break;
    case Op::max: out = std::max(a, b); break;
  }
  return finite(out);
}
auto expand(double lo, double hi, B& out) -> bool {
  if (!finite(lo) || !finite(hi) || lo > hi || lo == -largest || hi == largest)
    return false;
  out = {std::nextafter(lo, -infinity), std::nextafter(hi, infinity), true};
  return valid(out);
}
auto interval(Op op, B a, B b) -> B {
  if (!valid(a) || !valid(b)) return {};
  double lo{}, hi{};
  B result;
  if (op == Op::neg) return {-a.upper, -a.lower, true};
  if (op == Op::abs) {
    if (a.lower >= 0) return a;
    if (a.upper <= 0) return {-a.upper, -a.lower, true};
    return {0, std::max(-a.lower, a.upper), true};
  }
  if (op == Op::min)
    return {std::min(a.lower, b.lower), std::min(a.upper, b.upper), true};
  if (op == Op::max)
    return {std::max(a.lower, b.lower), std::max(a.upper, b.upper), true};
  if (op == Op::add || op == Op::sub) {
    if (!checked(a.lower, op == Op::add ? b.lower : b.upper, op, lo) ||
        !checked(a.upper, op == Op::add ? b.upper : b.lower, op, hi))
      return {};
  } else if (op == Op::div && b.lower == b.upper) {
    if (b.lower <= 0 || !checked(a.lower, b.lower, Op::div, lo) ||
        !checked(a.upper, b.upper, Op::div, hi))
      return {};
  } else {
    if (op == Op::div && b.lower <= 0) return {};
    std::array<double, 4> values{};
    for (std::size_t i = 0; i < 4; ++i)
      if (!checked(i < 2 ? a.lower : a.upper, i % 2 == 0 ? b.lower : b.upper,
                   op, values[i]))
        return {};
    lo = *std::min_element(values.begin(), values.end());
    hi = *std::max_element(values.begin(), values.end());
  }
  return expand(lo, hi, result) ? result : B{};
}
auto geometry_valid(const G& g) -> bool {
  for (const auto* values : std::array{&g.half, &g.hip, &g.axis, &g.segment})
    for (B v : *values)
      if (!valid(v)) return false;
  for (B h : g.half)
    if (h.lower <= 0) return false;
  return valid(g.radius) && g.radius.lower > 0 && valid(g.owner_limit) &&
         g.owner_limit.lower >= 0;
}
template <class M> struct Verifier {
  M& m;
  std::size_t guard_cap, operation_cap;
  auto guard(bool (*predicate)()) -> bool {
    if (m.guards >= guard_cap) {
      m.condition = W::verification_guard_capacity;
      return false;
    }
    m.guard_evaluated |= static_cast<std::uint8_t>(1U << m.guards);
    ++m.guards;
    if (!predicate()) {
      m.condition = W::unsupported_arithmetic;
      return false;
    }
    return true;
  }
  template <class F> auto tuple(F predicate) -> bool {
    if (m.guards >= guard_cap) {
      m.condition = W::verification_guard_capacity;
      return false;
    }
    m.guard_evaluated |= static_cast<std::uint8_t>(1U << m.guards);
    ++m.guards;
    if (!predicate()) {
      m.condition = W::invalid_proposal;
      return false;
    }
    return true;
  }
  template <class F> auto op(Op kind, F inputs) -> B {
    if (m.condition != W::none) return {};
    m.failed_operation = static_cast<std::uint8_t>(m.operations);
    if (m.operations >= operation_cap) {
      m.condition = W::verification_operation_capacity;
      return {};
    }
    m.primitive_evaluated |= std::uint64_t{1} << m.operations;
    ++m.operations;
    const auto values = inputs();
    const B result = interval(kind, values.first, values.second);
    if (!valid(result)) m.condition = W::unsupported_arithmetic;
    return result;
  }
};
auto hard(W w) -> bool {
  return w == W::source_guard_capacity || w == W::current_guard_capacity ||
         w == W::chart_capacity || w == W::generator_state_capacity ||
         w == W::generator_operation_capacity || w == W::dual_capacity ||
         w == W::primal_capacity || w == W::verification_guard_capacity ||
         w == W::verification_operation_capacity || w == W::output_capacity ||
         w == W::phase_capacity;
}
auto fail(D& d, W w) -> bool {
  if (d.first_refusal.condition == W::none)
    d.first_refusal = {w,   d.last_state, d.last_delta, d.last_alpha,
                       255, d.last_pivot, d.last_row};
  if (hard(w)) {
    d.state = S::capacity;
    d.stop_condition = w;
  } else if (w == W::unsupported_arithmetic || w == W::invalid_binding) {
    d.state = w == W::invalid_binding ? S::unresolved : S::unsupported;
    d.stop_condition = w;
    if (w == W::unsupported_arithmetic) d.arithmetic_supported = false;
  } else if (d.state != S::capacity && d.state != S::unsupported) {
    d.state = S::unresolved;
    d.stop_condition = w;
  }
  return false;
}
auto charge(D& d, std::size_t& work, std::size_t cap, W w) -> bool {
  if (work >= cap) return fail(d, w);
  ++work;
  return true;
}
auto same(const BoardingRoutePhaseConstant& a,
          const BoardingRoutePhaseConstant& b) -> bool {
  return a.count == b.count && a.terms == b.terms;
}
[[gnu::noinline]] auto canonical(const Request& r, Case id) -> bool {
  const auto e =
      detail::boarding_route_intermediate_hip_diagnostic01_controls(id);
  if (!e) return false;
  for (std::size_t j = 0; j < 3; ++j) {
    for (std::size_t k = 0; k < 2; ++k)
      if (!same(r.root[k].coordinates[j], e->root[k].coordinates[j]))
        return false;
    for (std::size_t side = 0; side < 2; ++side)
      for (std::size_t k = 0; k < 2; ++k)
        if (!same(r.feet[side].sole[k].coordinates[j],
                  e->feet[side].sole[k].coordinates[j]))
          return false;
  }
  return r.root_yaw_half == e->root_yaw_half &&
         r.torso_lean_half == e->torso_lean_half &&
         r.port_reaction_fraction == e->port_reaction_fraction &&
         r.seconds_per_parameter == e->seconds_per_parameter &&
         r.feet[0].yaw_half == e->feet[0].yaw_half &&
         r.feet[1].yaw_half == e->feet[1].yaw_half &&
         r.feet[0].swing_height_metres == e->feet[0].swing_height_metres &&
         r.feet[1].swing_height_metres == e->feet[1].swing_height_metres;
}
[[gnu::noinline]] auto join(std::size_t i) -> bool {
  const auto a = detail::boarding_route_intermediate_support_self01_controls(i),
             b = detail::boarding_route_intermediate_support_self01_controls(i +
                                                                             1);
  return a && b &&
         detail::boarding_route_intermediate_support_self01_join_math(*a, *b);
}

[[gnu::noinline]] auto enroll(D& d, const L& l, Request& request) -> bool {
  std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount> parts{};
  std::array<bool, 4> joins{};
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
    if (!detail::boarding_route_intermediate_hip_diagnostic01_source_charge(
            d, i, l))
      return false;

    bool ok = false;
    switch (i) {
      case 0:
        ok = A::valid(d.source);
        if (!ok) {
          fail(d, W::invalid_binding);
          d.stop_condition = W::invalid_binding;
          return false;
        }
        break;
      case 1:
        ok = detail::boarding_route_foot_phase_environment();
        if (!ok) return fail(d, W::unsupported_arithmetic);
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
        ok = d.version == 1 && kBoardingBootPressureRadiusMetres == .020 &&
             kBoardingBootDiskEdgeMarginMetres == .010 &&
             kBoardingBootLoadMarginMetres == .010;
        break;
      case 21:
        parts = detail::boarding_route_foot_phase_parts();
        ok = true;
        break;
      case 22:
        ok = parts.size() == 15;
        for (std::size_t j = 0; j < parts.size(); ++j)
          ok = ok && static_cast<std::size_t>(parts[j].id) == j;
        break;
      case 23:
      case 24:
      case 25: {
        const auto control =
            detail::boarding_route_intermediate_support_self01_controls(i - 23);
        if (control && d.selection.phase_index == i - 23) request = *control;
        ok = control && control->seconds_per_parameter == 12 &&
             control->port_reaction_fraction == std::array<double, 2>{0, 0};
        break;
      }
      case 26:
      case 27:
        joins[i - 26] = join(i - 26);
        ok = joins[i - 26];
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
        if (x && d.selection.phase_index == i - 29) request = *x;
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
        joins[i - 32] = join(i - 32);
        ok = joins[i - 32];
        break;
      default: break;
    }
    if (!ok) return fail(d, W::source_identity);
  }
  if (!detail::boarding_route_intermediate_hip_diagnostic01_body_source(
          d, parts, l))
    return false;
  d.source_complete = true;
  return true;
}

} // namespace
auto detail::boarding_route_intermediate_hip_diagnostic01_case(Case id)
    -> std::optional<BoardingRouteIntermediateHipDiagnostic01CaseMetadata> {
  switch (id) {
    case Case::point_0: return {{id, 0, 0, 0, 0, 0, 12}};
    case Case::point_quarter: return {{id, 1, .25, .25, 1, 1, 12}};
    case Case::point_half: return {{id, 2, .5, .5, 1, 1, 12}};
    case Case::point_three_quarters: return {{id, 3, .75, .75, 1, 1, 12}};
    case Case::point_1: return {{id, 4, 1, 1, 1, 1, 2}};
    case Case::terminal_phase1_interval:
      return {{id, 1, 145.0 / 1024, 146.0 / 1024, 17.0 / 128, 9.0 / 64, 12}};
  }
  return {};
}
[[gnu::noinline]] auto detail::
    boarding_route_intermediate_hip_diagnostic01_controls(Case id)
        -> std::optional<Request> {
  const auto c = boarding_route_intermediate_hip_diagnostic01_case(id);
  if (!c) return {};
  return boarding_route_intermediate_support_self01_controls(c->phase_index);
}
[[gnu::noinline]] auto detail::
    boarding_route_intermediate_hip_diagnostic01_dual_math(
        const G& g, DC c,
        BoardingRouteIntermediateHipDiagnostic01DualMathLimits limits) -> DM {
  DM m;
  if (limits.guards > 3 || limits.operations > 42) {
    m.condition = W::invalid_proposal;
    return m;
  }
  Verifier v{m, limits.guards, limits.operations};
  if (!v.guard(boarding_route_foot_phase_environment) ||
      !v.tuple([&] { return geometry_valid(g); }) || !v.tuple([&] {
        return std::ranges::all_of(c.normal, finite) && finite(c.multiplier) &&
               c.multiplier >= 0;
      }))
    return m;
  m.candidate = c;
  std::array<B, 3> a{}, terms{};
  for (std::size_t j = 0; j < 3; ++j) {
    B twice =
        v.op(Op::mul, [&] { return std::pair{point(2), point(c.normal[j])}; });
    B scale = v.op(Op::mul,
                   [&] { return std::pair{point(c.multiplier), g.axis[j]}; });
    a[j] = v.op(Op::sub, [&] { return std::pair{twice, scale}; });
  }
  for (std::size_t j = 0; j < 3; ++j) {
    B magnitude = v.op(Op::abs, [&] { return std::pair{a[j], point(0)}; });
    terms[j] = v.op(Op::mul, [&] { return std::pair{g.half[j], magnitude}; });
  }
  B sum = v.op(Op::add, [&] { return std::pair{terms[0], terms[1]}; });
  m.boxsum = v.op(Op::add, [&] { return std::pair{sum, terms[2]}; });
  for (std::size_t j = 0; j < 3; ++j)
    terms[j] = v.op(Op::mul, [&] { return std::pair{a[j], g.hip[j]}; });
  sum = v.op(Op::add, [&] { return std::pair{terms[0], terms[1]}; });
  m.hipdot = v.op(Op::add, [&] { return std::pair{sum, terms[2]}; });
  for (std::size_t j = 0; j < 3; ++j)
    terms[j] = v.op(
        Op::mul, [&] { return std::pair{point(c.normal[j]), g.segment[j]}; });
  sum = v.op(Op::add, [&] { return std::pair{terms[0], terms[1]}; });
  B dot = v.op(Op::add, [&] { return std::pair{sum, terms[2]}; });
  B twice = v.op(Op::mul, [&] { return std::pair{point(2), dot}; });
  m.segmentmax = v.op(Op::max, [&] { return std::pair{point(0), twice}; });
  B lambda = v.op(
      Op::mul, [&] { return std::pair{point(c.multiplier), g.owner_limit}; });
  for (std::size_t j = 0; j < 3; ++j)
    terms[j] = v.op(Op::mul, [&] {
      return std::pair{point(c.normal[j]), point(c.normal[j])};
    });
  sum = v.op(Op::add, [&] { return std::pair{terms[0], terms[1]}; });
  m.normal_square = v.op(Op::add, [&] { return std::pair{sum, terms[2]}; });
  B bound = v.op(Op::neg, [&] { return std::pair{m.boxsum, point(0)}; });
  bound = v.op(Op::sub, [&] { return std::pair{bound, m.hipdot}; });
  bound = v.op(Op::sub, [&] { return std::pair{bound, m.segmentmax}; });
  bound = v.op(Op::add, [&] { return std::pair{bound, lambda}; });
  m.lower_bound =
      v.op(Op::sub, [&] { return std::pair{bound, m.normal_square}; });
  m.radius_squared =
      v.op(Op::mul, [&] { return std::pair{g.radius, g.radius}; });
  m.gap =
      v.op(Op::sub, [&] { return std::pair{m.lower_bound, m.radius_squared}; });
  if (m.condition == W::none) {
    m.complete = m.arithmetic_supported = true;
    m.excluded = m.gap.lower >= 0;
    m.failed_operation = 255;
  }
  return m;
}
[[gnu::noinline]] auto detail::
    boarding_route_intermediate_hip_diagnostic01_primal_math(
        const G& g, PC c,
        BoardingRouteIntermediateHipDiagnostic01PrimalMathLimits limits) -> PM {
  PM m;
  if (limits.guards > 3 || limits.operations > 28) {
    m.condition = W::invalid_proposal;
    return m;
  }
  Verifier v{m, limits.guards, limits.operations};
  if (!v.guard(boarding_route_foot_phase_environment) ||
      !v.tuple([&] { return geometry_valid(g); }) || !v.tuple([&] {
        return std::ranges::all_of(c.point, finite) &&
               finite(c.segment_fraction) && c.segment_fraction >= 0 &&
               c.segment_fraction <= 1;
      }))
    return m;
  m.candidate = c;
  std::array<B, 3> delta{}, residual{}, terms{};
  for (std::size_t j = 0; j < 3; ++j) {
    delta[j] =
        v.op(Op::sub, [&] { return std::pair{point(c.point[j]), g.hip[j]}; });
    B along = v.op(Op::mul, [&] {
      return std::pair{point(c.segment_fraction), g.segment[j]};
    });
    residual[j] = v.op(Op::sub, [&] { return std::pair{delta[j], along}; });
  }
  for (std::size_t j = 0; j < 3; ++j)
    terms[j] =
        v.op(Op::mul, [&] { return std::pair{residual[j], residual[j]}; });
  B sum = v.op(Op::add, [&] { return std::pair{terms[0], terms[1]}; });
  m.residual_squared = v.op(Op::add, [&] { return std::pair{sum, terms[2]}; });
  for (std::size_t j = 0; j < 3; ++j)
    terms[j] = v.op(Op::mul, [&] { return std::pair{g.axis[j], delta[j]}; });
  sum = v.op(Op::add, [&] { return std::pair{terms[0], terms[1]}; });
  m.axial_projection = v.op(Op::add, [&] { return std::pair{sum, terms[2]}; });
  for (std::size_t j = 0; j < 3; ++j) {
    m.box_gaps[j * 2] =
        v.op(Op::add, [&] { return std::pair{g.half[j], point(c.point[j])}; });
    m.box_gaps[j * 2 + 1] =
        v.op(Op::sub, [&] { return std::pair{g.half[j], point(c.point[j])}; });
  }
  m.axial_gap = v.op(
      Op::sub, [&] { return std::pair{m.axial_projection, g.owner_limit}; });
  m.radius_squared =
      v.op(Op::mul, [&] { return std::pair{g.radius, g.radius}; });
  m.residual_gap = v.op(
      Op::sub, [&] { return std::pair{m.radius_squared, m.residual_squared}; });
  if (m.condition == W::none) {
    m.complete = m.arithmetic_supported = true;
    m.unowned_interior =
        std::ranges::all_of(m.box_gaps, [](B b) { return b.lower > 0; }) &&
        m.axial_gap.lower > 0 && m.residual_gap.lower > 0;
    m.failed_operation = 255;
  }
  return m;
}
auto detail::boarding_route_intermediate_hip_diagnostic01_source_charge(
    D& d, std::size_t i, const L& l) -> bool {
  if (!charge(d, d.work.source_guards, l.source_guards,
              W::source_guard_capacity))
    return false;
  d.source_evaluated[i / 64] |= std::uint64_t{1} << (i % 64);
  return true;
}
auto detail::boarding_route_intermediate_hip_diagnostic01_current_charge(
    D& d, std::size_t i, const L& l) -> bool {
  if (!charge(d, d.work.current_guards, l.current_guards,
              W::current_guard_capacity))
    return false;
  d.current_evaluated |= std::uint32_t{1} << i;
  return true;
}
namespace {
auto coord(RigidVector3 p, std::size_t j) -> double {
  return j == 0 ? p.x : j == 1 ? p.y : p.z;
}
auto value(const BoardingPlantedBodyPointEvidence& p, std::size_t j) -> B {
  return {coord(p.value.lower, j), coord(p.value.upper, j), true};
}
auto point_valid(const BoardingPlantedBodyPointEvidence& p) -> bool {
  for (std::size_t j = 0; j < 3; ++j) {
    const B b = value(p, j);
    if (!valid(b) || std::abs(b.lower) > 8 || std::abs(b.upper) > 8)
      return false;
  }
  return true;
}
} // namespace
auto detail::boarding_route_intermediate_hip_diagnostic01_current_cell(
    const BoardingRouteIntermediateHipDiagnostic01AdmissionContext& x, D& d,
    const L& l) -> bool {
  const auto record = [&](std::size_t i, auto predicate) {
    return boarding_route_intermediate_hip_diagnostic01_current_charge(d, i,
                                                                       l) &&
           (predicate() || fail(d, W::current_identity));
  };
  if (!record(0, [&] {
        return x.owner_ == &d && x.data_ && x.data_ == A::data(d.source) &&
               x.fresh_ && x.request_ && A::valid(d.source) &&
               d.source_complete && d.current_phase.size() == 1 &&
               d.current_phase.capacity() == 1;
      }))
    return false;
  const auto& p = d.current_phase.front();
  for (std::size_t i = 0; i < 7; ++i)
    if (!record(i + 1, [&] {
          switch (i) {
            case 0: return p.complete;
            case 1: return p.arithmetic_supported;
            case 2: return p.nominal_links;
            case 3: return p.target_sole_identities;
            case 4: return p.joint_sectors;
            case 5: return p.derivative_domains;
            default: return p.timing_complete;
          }
        }))
      return false;
  if (!record(8, [&] { return p.first == d.selection.local_first; }) ||
      !record(9, [&] { return p.last == d.selection.local_last; }))
    return false;
  if (!boarding_route_intermediate_hip_diagnostic01_current_body(d, l))
    return false;
  if (!record(28, [&] {
        const auto& a = p.legs[0];
        return a.nominal_links && a.target_sole_identity && a.joint_sectors &&
               a.derivative_domains && a.timing_complete &&
               x.request_->seconds_per_parameter ==
                   d.selection.seconds_per_parameter;
      }))
    return false;
  if (!record(29, [&] {
        return std::ranges::all_of(p.frames[0].columns, point_valid) ||
               fail(d, W::unsupported_arithmetic);
      }))
    return false;
  if (!record(30, [&] {
        using P = BoardingPlantedBodyPointId;
        return (point_valid(p.points[static_cast<std::size_t>(P::root)]) &&
                point_valid(p.points[static_cast<std::size_t>(P::port_hip)]) &&
                point_valid(
                    p.points[static_cast<std::size_t>(P::port_knee)])) ||
               fail(d, W::unsupported_arithmetic);
      }))
    return false;
  if (!record(31, [&] { return canonical(*x.request_, d.selection.case_id); }))
    return false;
  d.current_complete = true;
  const BoardingRouteIntermediateHipDiagnostic01CurrentToken token{x, d};
  return boarding_route_intermediate_hip_diagnostic01_chart(token, d, l);
}
auto detail::boarding_route_intermediate_hip_diagnostic01_chart(
    const BoardingRouteIntermediateHipDiagnostic01CurrentToken& t, D& d,
    const L& l) -> bool {
  if (t.owner_ != &d || !t.context_ || t.context_->owner_ != &d ||
      t.context_->data_ != t.data_ || t.data_ != A::data(d.source) ||
      !t.context_->fresh_ || t.request_ != t.context_->request_ ||
      d.current_phase.size() != 1 || t.cell_ != &d.current_phase.front() ||
      !d.current_complete)
    return fail(d, W::chart_identity);
  G g;
  g.half = {point(.24), point(.12), point(.18)};
  g.hip = {point(-.14), point(0), point(0)};
  g.radius = point(.105);
  g.owner_limit = point(kBoardingSelfHipLengthMetres);
  const auto op = [&](Op kind, auto inputs) -> B {
    if (d.state == S::capacity || d.state == S::unsupported) return {};
    if (!charge(d, d.work.chart_operations, l.chart_operations,
                W::chart_capacity))
      return {};
    d.chart_evaluated |= std::uint32_t{1} << (d.work.chart_operations - 1);
    const auto pair = inputs();
    B b = interval(kind, pair.first, pair.second);
    if (!valid(b)) fail(d, W::unsupported_arithmetic);
    return b;
  };
  using P = BoardingPlantedBodyPointId;
  std::array<B, 3> delta{};
  for (std::size_t j = 0; j < 3; ++j) {
    delta[j] = op(Op::sub, [&] {
      return std::pair{
          value(t.cell_->points[static_cast<std::size_t>(P::port_knee)], j),
          value(t.cell_->points[static_cast<std::size_t>(P::port_hip)], j)};
    });
    if (valid(delta[j]) &&
        (std::abs(delta[j].lower) > 16 || std::abs(delta[j].upper) > 16))
      return fail(d, W::chart_identity);
  }
  for (std::size_t j = 0; j < 3; ++j) {
    std::array<B, 3> products{};
    for (std::size_t a = 0; a < 3; ++a)
      products[a] = op(Op::mul, [&] {
        return std::pair{value(t.cell_->frames[0].columns[j], a), delta[a]};
      });
    B sum = op(Op::add, [&] { return std::pair{products[0], products[1]}; });
    g.segment[j] = op(Op::add, [&] { return std::pair{sum, products[2]}; });
  }
  for (std::size_t j = 0; j < 3; ++j)
    g.axis[j] =
        op(Op::div, [&] { return std::pair{g.segment[j], point(.47285)}; });
  if (d.state == S::capacity || d.state == S::unsupported) return false;
  d.geometry = g;
  d.chart_complete = true;
  return true;
}
namespace {
struct Generator {
  D& d;
  const L& l;
  bool invalid{};
  auto take() -> bool {
    if (invalid) return false;
    if (!charge(d, d.work.generator_operations, l.generator_operations,
                W::generator_operation_capacity)) {
      invalid = true;
      return false;
    }
    return true;
  }
  auto move(double x) -> double {
    if (!take()) return 0;
    if (!finite(x)) {
      invalid = true;
      return 0;
    }
    return x;
  }
  auto op(Op kind, double a, double b = 0) -> double {
    if (!take()) return 0;
    double result{};
    if (!checked(a, b, kind, result)) {
      invalid = true;
      return 0;
    }
    return result;
  }
};
struct Proposal {
  std::array<double, 3> q{}, normal{};
  double s{}, lambda{};
};
using Matrix = std::array<std::array<double, 6>, 5>;
[[gnu::noinline]] auto solve(D& d, Generator& g, std::size_t state,
                             const std::array<double, 3>& axis,
                             const std::array<double, 3>& segment,
                             Proposal& proposal) -> bool {
  const std::array<std::size_t, 3> status{state % 3, (state / 3) % 3,
                                          (state / 9) % 3};
  const std::size_t segment_status = (state / 27) % 3, slab_status = state / 81;
  std::array<int, 3> indices{-1, -1, -1};
  std::array<double, 3> q{};
  std::size_t n{};
  constexpr std::array<double, 3> half{.24, .12, .18}, hip{-.14, 0, 0};
  for (std::size_t j = 0; j < 3; ++j) {
    if (status[j] == 0)
      indices[j] = static_cast<int>(n++);
    else
      q[j] = status[j] == 1 ? -half[j] : half[j];
  }
  const int si = segment_status == 0 ? static_cast<int>(n++) : -1;
  const double s = segment_status == 2 ? 1 : 0;
  const int li = slab_status == 1 ? static_cast<int>(n++) : -1;
  Matrix a{};
  const auto fixed = [&](std::size_t row, double coefficient, int index,
                         double value) {
    if (index >= 0)
      a[row][static_cast<std::size_t>(index)] = coefficient;
    else if (coefficient != 0)
      a[row][n] = g.op(Op::sub, a[row][n], g.op(Op::mul, coefficient, value));
  };
  for (std::size_t j = 0; j < 3; ++j)
    if (indices[j] >= 0) {
      const auto row = static_cast<std::size_t>(indices[j]);
      a[row][n] = g.op(Op::mul, 2, hip[j]);
      for (std::size_t k = 0; k < 3; ++k)
        fixed(row, j == k ? 2 : 0, indices[k], q[k]);
      fixed(row, g.op(Op::neg, g.op(Op::mul, 2, segment[j])), si, s);
      if (li >= 0)
        a[row][static_cast<std::size_t>(li)] = g.op(Op::neg, axis[j]);
    }
  if (si >= 0) {
    const auto row = static_cast<std::size_t>(si);
    std::array<double, 3> dots{}, squares{};
    for (std::size_t j = 0; j < 3; ++j)
      dots[j] = g.op(Op::mul, segment[j], hip[j]);
    const double dot = g.op(Op::add, g.op(Op::add, dots[0], dots[1]), dots[2]);
    a[row][n] = g.op(Op::neg, g.op(Op::mul, 2, dot));
    for (std::size_t j = 0; j < 3; ++j)
      squares[j] = g.op(Op::mul, segment[j], segment[j]);
    const double square =
        g.op(Op::add, g.op(Op::add, squares[0], squares[1]), squares[2]);
    a[row][row] = g.op(Op::mul, 2, square);
    for (std::size_t j = 0; j < 3; ++j)
      fixed(row, g.op(Op::neg, g.op(Op::mul, 2, segment[j])), indices[j], q[j]);
  }
  if (li >= 0) {
    const auto row = static_cast<std::size_t>(li);
    std::array<double, 3> terms{};
    for (std::size_t j = 0; j < 3; ++j)
      terms[j] = g.op(Op::mul, axis[j], hip[j]);
    const double dot =
        g.op(Op::add, g.op(Op::add, terms[0], terms[1]), terms[2]);
    a[row][n] = g.op(Op::sub, g.op(Op::neg, kBoardingSelfHipLengthMetres), dot);
    for (std::size_t j = 0; j < 3; ++j)
      fixed(row, g.op(Op::neg, axis[j]), indices[j], q[j]);
  }
  if (g.invalid) return false;
  for (std::size_t k = 0; k < n; ++k) {
    d.last_pivot = static_cast<std::uint8_t>(k);
    std::size_t chosen = n;
    for (std::size_t i = k; i < n; ++i) {
      d.last_row = static_cast<std::uint8_t>(i);
      if (!g.take()) return false;
      if (!finite(a[i][k])) {
        g.invalid = true;
        return false;
      }
      if (a[i][k] != 0) {
        chosen = i;
        break;
      }
    }
    if (chosen == n) return false;
    if (chosen != k)
      for (std::size_t j = 0; j <= n; ++j) {
        if (!g.take()) return false;
        std::swap(a[k][j], a[chosen][j]);
      }
    const double pivot = g.move(a[k][k]);
    for (std::size_t j = k; j <= n; ++j)
      a[k][j] = g.op(Op::div, a[k][j], pivot);
    if (g.invalid) return false;
    a[k][k] = 1;
    for (std::size_t i = 0; i < n; ++i)
      if (i != k) {
        const double factor = g.move(a[i][k]);
        for (std::size_t j = k; j <= n; ++j) {
          const double product = g.op(Op::mul, factor, a[k][j]);
          a[i][j] = g.op(Op::sub, a[i][j], product);
        }
        if (g.invalid) return false;
        a[i][k] = 0;
      }
  }
  for (std::size_t j = 0; j < 3; ++j)
    proposal.q[j] = g.move(
        indices[j] < 0 ? q[j] : a[static_cast<std::size_t>(indices[j])][n]);
  proposal.s = g.move(si < 0 ? s : a[static_cast<std::size_t>(si)][n]);
  proposal.lambda = g.move(li < 0 ? 0 : a[static_cast<std::size_t>(li)][n]);
  if (!g.take() || proposal.lambda < 0) {
    g.invalid = true;
    return false;
  }
  for (std::size_t j = 0; j < 3; ++j) {
    const double along = g.op(Op::mul, proposal.s, segment[j]);
    const double delta = g.op(Op::sub, proposal.q[j], hip[j]);
    proposal.normal[j] = g.op(Op::sub, delta, along);
  }
  return !g.invalid;
}
template <class M> auto verification_stop(D& d, const M& math) -> bool {
  if (math.condition == W::verification_guard_capacity ||
      math.condition == W::verification_operation_capacity)
    return !fail(d, math.condition);
  if (math.condition == W::unsupported_arithmetic && math.guards == 1 &&
      math.operations == 0)
    return !fail(d, W::unsupported_arithmetic);
  return false;
}
[[gnu::noinline]] auto search(D& d, const L& l) -> void {
  std::array<double, 3> axis{}, segment{};
  bool reporting = false;
  for (std::size_t state = 0; state < 162; ++state) {
    d.last_partial = std::monostate{};
    d.last_state = static_cast<std::uint16_t>(state);
    d.last_delta = d.last_alpha = d.last_pivot = d.last_row = 255;
    if (!charge(d, d.work.generator_states, l.generator_states,
                W::generator_state_capacity))
      return;
    d.states_evaluated[state / 64] |= std::uint64_t{1} << (state % 64);
    Generator gen{d, l};
    if (!reporting) {
      for (std::size_t kind = 0; kind < 2; ++kind)
        for (std::size_t j = 0; j < 3; ++j) {
          const B b = kind == 0 ? d.geometry.axis[j] : d.geometry.segment[j];
          const double difference = gen.op(Op::sub, b.upper, b.lower);
          const double half = gen.op(Op::mul, difference, .5);
          const double midpoint = gen.op(Op::add, b.lower, half);
          (kind == 0 ? axis : segment)[j] = midpoint;
        }
      if (gen.invalid) {
        if (d.state != S::capacity) fail(d, W::invalid_proposal);
        return;
      }
      reporting = true;
    }
    Proposal p;
    if (!solve(d, gen, state, axis, segment, p)) {
      if (d.state == S::capacity) return;
      d.states_skipped[state / 64] |= std::uint64_t{1} << (state % 64);
      ++d.skipped_systems;
      continue;
    }
    d.last_partial = std::monostate{};
    if (!charge(d, d.work.dual_trials, l.dual_trials, W::dual_capacity)) return;
    const auto dual =
        detail::boarding_route_intermediate_hip_diagnostic01_dual_math(
            d.geometry, {p.normal, p.lambda},
            {std::min(std::size_t{3},
                      l.verification_guards - d.work.verification_guards),
             std::min(std::size_t{42}, l.verification_operations -
                                           d.work.verification_operations)});
    d.work.verification_guards += dual.guards;
    d.work.verification_operations += dual.operations;
    d.last_partial = dual;
    d.certificate_assessed = true;
    if (verification_stop(d, dual)) return;
    if (dual.excluded) {
      d.winner = dual;
      d.excluded = true;
      d.state = S::excluded;
      return;
    }
    if (!dual.complete) ++d.skipped_proposals;
    constexpr std::array<double, 4> delta_coefficients{0, .25, .5, .75};
    constexpr std::array<double, 5> alphas{1, .5, .75, .875, .9375};
    constexpr std::array<double, 3> hip{-.14, 0, 0};
    for (std::size_t di = 0; di < 4; ++di)
      for (std::size_t ai = 0; ai < 5; ++ai) {
        d.last_partial = std::monostate{};
        d.last_delta = static_cast<std::uint8_t>(di);
        d.last_alpha = static_cast<std::uint8_t>(ai);
        if (!charge(d, d.work.primal_trials, l.primal_trials,
                    W::primal_capacity))
          return;
        Generator variant{d, l};
        const double delta = variant.op(Op::mul, .105, delta_coefficients[di]);
        std::array<double, 3> shifted{};
        for (std::size_t j = 0; j < 3; ++j) {
          const double amount = variant.op(Op::mul, delta, axis[j]);
          shifted[j] = variant.op(Op::add, p.q[j], amount);
        }
        const double shift = variant.op(Op::div, delta, .47285);
        const double raw_s = variant.op(Op::add, p.s, shift);
        const double nonnegative = variant.op(Op::max, raw_s, 0);
        const double shifted_s = variant.op(Op::min, nonnegative, 1);
        PC candidate;
        for (std::size_t j = 0; j < 3; ++j) {
          const double difference = variant.op(Op::sub, shifted[j], hip[j]);
          const double scaled = variant.op(Op::mul, alphas[ai], difference);
          candidate.point[j] = variant.op(Op::add, hip[j], scaled);
        }
        candidate.segment_fraction = variant.op(Op::mul, alphas[ai], shifted_s);
        if (variant.invalid) {
          if (d.state == S::capacity) return;
          ++d.skipped_proposals;
          continue;
        }
        const auto primal =
            detail::boarding_route_intermediate_hip_diagnostic01_primal_math(
                d.geometry, candidate,
                {std::min(std::size_t{3},
                          l.verification_guards - d.work.verification_guards),
                 std::min(std::size_t{28},
                          l.verification_operations -
                              d.work.verification_operations)});
        d.work.verification_guards += primal.guards;
        d.work.verification_operations += primal.operations;
        d.last_partial = primal;
        d.certificate_assessed = true;
        if (verification_stop(d, primal)) return;
        if (primal.unowned_interior) {
          d.winner = primal;
          d.unowned_interior = true;
          d.state = S::unowned_interior;
          return;
        }
        if (!primal.complete) ++d.skipped_proposals;
      }
  }
  fail(d, W::no_certificate);
}
[[gnu::noinline]] auto initialize(BoardingRouteFootPhaseCell& cell) -> void {
  cell = BoardingRouteFootPhaseCell{};
}
auto limits_valid(const L& l) -> bool {
  const L m;
  return l.phase.graphs <= m.phase.graphs && l.phase.legs <= m.phase.legs &&
         l.phase.bodies <= m.phase.bodies &&
         l.phase.sectors <= m.phase.sectors &&
         l.phase.timing <= m.phase.timing &&
         l.source_guards <= m.source_guards &&
         l.current_guards <= m.current_guards &&
         l.chart_operations <= m.chart_operations &&
         l.generator_states <= m.generator_states &&
         l.generator_operations <= m.generator_operations &&
         l.dual_trials <= m.dual_trials && l.primal_trials <= m.primal_trials &&
         l.verification_guards <= m.verification_guards &&
         l.verification_operations <= m.verification_operations &&
         l.output_bytes <= m.output_bytes;
}
} // namespace
[[gnu::noinline]] auto
detail::boarding_route_intermediate_hip_diagnostic01_bounded(
    const OriginBoardingIntermediatePauseSupport& source, Case id, L l) -> E {
  const auto selection = boarding_route_intermediate_hip_diagnostic01_case(id);
  if (!selection) return std::unexpected(Error::invalid_case);
  if (!limits_valid(l)) return std::unexpected(Error::invalid_limits);
  if (!boarding_route_foot_phase_environment())
    return std::unexpected(Error::unsupported_environment);
  if (l.output_bytes < fixed_output)
    return std::unexpected(Error::output_preflight);
  E result{std::in_place, source, *selection};
  D& d = *result;
  d.output_capacity_bytes = fixed_output;
  d.state = S::unresolved;
  Request request;
  if (!enroll(d, l, request)) return result;
  if (l.output_bytes < fixed_output + sizeof(BoardingRouteFootPhaseCell)) {
    fail(d, W::output_capacity);
    return result;
  }
  try {
    d.current_phase.reserve(1);
    if (d.current_phase.capacity() != 1) {
      std::vector<BoardingRouteFootPhaseCell>{}.swap(d.current_phase);
      fail(d, W::output_capacity);
      return result;
    }
    d.output_capacity_bytes =
        fixed_output +
        d.current_phase.capacity() * sizeof(BoardingRouteFootPhaseCell);
    d.current_phase.resize(1);
  } catch (const std::bad_alloc&) {
    return std::unexpected(Error::allocation_failure);
  }
  initialize(d.current_phase.front());
  BoardingRouteIntermediateHipDiagnostic01AdmissionContext context{d, request};
  BoardingRouteFootPhaseLimits phase_limits;
  phase_limits.graphs = l.phase.graphs;
  phase_limits.legs = l.phase.legs;
  phase_limits.bodies = l.phase.bodies;
  phase_limits.sectors = l.phase.sectors;
  phase_limits.timing = l.phase.timing;
  ++d.work.phase_calls;
  const auto phase_result = boarding_route_foot_phase_cell(
      request, selection->local_first, selection->local_last, false,
      phase_limits, d.phase_work, d.current_phase.front(), d.phase_refusal);
  if (phase_result != BoardingRouteFootPhaseCellResult::accepted) {
    if (phase_result == BoardingRouteFootPhaseCellResult::capacity)
      fail(d, W::phase_capacity);
    else if (phase_result == BoardingRouteFootPhaseCellResult::unsupported)
      fail(d, W::unsupported_arithmetic);
    else
      fail(d, W::phase_prerequisite);
    return result;
  }
  context.fresh_ = true;
  d.phase_available = true;
  if (!boarding_route_intermediate_hip_diagnostic01_current_cell(context, d, l))
    return result;
  search(d, l);
  return result;
}
auto assess_origin_boarding_route_intermediate_hip_diagnostic01(
    const OriginBoardingIntermediatePauseSupport& source, Case id) -> E {
  return detail::boarding_route_intermediate_hip_diagnostic01_bounded(source,
                                                                      id);
}
} // namespace apsis_drift
