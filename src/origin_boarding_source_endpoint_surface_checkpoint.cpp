#include "apsis_drift/origin_boarding_source_endpoint_surface_checkpoint.hpp"
#include "origin_boarding_lower_foot_transfer_surface_sweep_internal.hpp"
#include "origin_boarding_source_endpoint_load_internal.hpp"
#include "origin_boarding_source_endpoint_surface_checkpoint_internal.hpp"
#include "origin_lower_cockpit_contact_internal.hpp"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <functional>
#include <limits>
#include <span>
#include <utility>

namespace apsis_drift {
namespace {
using Vec = RigidVector3;
using Shape = detail::BoardingSourceEndpointSurfaceCheckpointShape;
using Solid = detail::BoardingSourceEndpointSurfaceCheckpointSolidBounds;
using Pair = detail::BoardingSourceEndpointSurfaceCheckpointPairMathEvidence;
using Payload = BoardingSourceEndpointSurfaceCheckpointPayload;
using Condition = BoardingSourceEndpointSurfaceCheckpointCondition;
using PointBounds = BoardingPlantedLegPointBounds;
constexpr auto infinity = std::numeric_limits<double>::infinity();
constexpr std::array<Vec, 3> world_axes{Vec{1, 0, 0}, Vec{0, 1, 0},
                                        Vec{0, 0, 1}};
struct Interval {
  double low{}, high{};
  bool supported{};
};
struct WorkingPart {
  PointBounds first, second;
  Vec half;
  double radius{}, sole_plane{};
  BoardingBodyPartId id{};
  Shape shape{};
  bool sole_identity{};
};
struct Workspace {
  WorkingPart part;
  std::array<Vec, 16> axes{};
  std::array<Vec, 3> triangle_edges{};
  std::array<Vec, 2> reporting_endpoints{};
  std::array<Interval, 3> minimum{}, maximum{}, triangle_projection{};
  std::array<Interval, 2> support_pair{};
};
struct SquareScratch {
  std::array<double, 3> values{}, next{};
  std::size_t count{};
};
// Structured working records and conservative simultaneous nested allowances,
// excluding the ONE prerequisite child and output. Interval arguments/returns
// and approximate closest-feature points are counted, not only named locals.
constexpr std::size_t nested_scratch =
    sizeof(SquareScratch) + 3 * sizeof(double) + 28 * sizeof(Interval) +
    12 * sizeof(Vec) + 32 * sizeof(double) + 16 * sizeof(std::size_t) +
    sizeof(Pair);
using VisitorFunction =
    std::function<bool(const detail::LowerCockpitEffectiveTriangle&)>;
using VisitorResult =
    std::expected<detail::LowerCockpitEffectiveVisit, std::string>;
// Scan context has two references and three scalar controls; std::cref keeps
// only a reference wrapper in std::function (no copied workspace/child).
// Include the expected result while the function temporary is still live.
constexpr std::size_t callback_scratch = sizeof(VisitorFunction) +
                                         5 * sizeof(std::uint64_t) +
                                         sizeof(VisitorResult) + sizeof(void*);
constexpr std::size_t return_and_callback_scratch =
    sizeof(WorkingPart) + 3 * sizeof(Vec) + 6 * sizeof(double) +
    callback_scratch;
// Existing immutable visitor: borrowed view, local visit counts, span/loop
// state and reference captures remain live while the numeric callback runs.
// The alternate crop call's argument copies/return fit this same pool.
constexpr std::size_t contact_helper_scratch =
    sizeof(detail::LowerCockpitEffectiveTriangle) +
    sizeof(detail::LowerCockpitEffectiveVisit) +
    4 * sizeof(std::span<const detail::CabinContactObstacle>) +
    8 * sizeof(std::size_t) + 4 * sizeof(void*);
static_assert(2 * sizeof(detail::CabinContactBox) + 2 * sizeof(Vec) +
                  sizeof(std::expected<bool, std::string>) +
                  4 * sizeof(std::size_t) <=
              contact_helper_scratch);
static_assert(sizeof(WorkingPart) <= 144);
static_assert(sizeof(Workspace) <= 912);
static_assert(sizeof(Pair) <= 72);
static_assert(sizeof(BoardingSourceEndpointSurfaceCheckpointCoverage) <= 56);
static_assert(sizeof(BoardingSourceEndpointSurfaceCheckpointRefusal) <= 80);
static_assert(sizeof(Payload) <= 4096);
static_assert(sizeof(Payload) == 1032);
static_assert(sizeof(Workspace) + nested_scratch + return_and_callback_scratch +
                  contact_helper_scratch <=
              4096);
static_assert(
    sizeof(std::function<bool(const detail::LowerCockpitEffectiveTriangle&)>) +
        4 * sizeof(void*) <=
    64);
static_assert(sizeof(detail::LowerCockpitEffectiveVisit) <= 32);

auto environment_supported() -> bool {
  if (!std::numeric_limits<double>::is_iec559 ||
      std::numeric_limits<double>::radix != 2 ||
      std::numeric_limits<double>::digits != 53 ||
      std::fegetround() != FE_TONEAREST)
    return false;
  volatile double normal = std::numeric_limits<double>::min(), half = .5;
  volatile double subnormal = std::numeric_limits<double>::denorm_min(),
                  one = 1.;
  volatile double tie = 0x1p-53, above_tie = 0x1.8p-53;
  const double tiny = normal * half, preserved = subnormal * one,
               tie_sum = one + tie, above_sum = one + above_tie;
  return std::bit_cast<std::uint64_t>(tiny) == 0x0008000000000000ULL &&
         std::bit_cast<std::uint64_t>(preserved) == 1 &&
         std::bit_cast<std::uint64_t>(tie_sum) == 0x3ff0000000000000ULL &&
         std::bit_cast<std::uint64_t>(above_sum) == 0x3ff0000000000001ULL;
}
auto bounded(double x, double limit = 8) -> bool {
  return std::isfinite(x) && std::abs(x) <= limit;
}
auto bounded(Vec v, double limit = 8) -> bool {
  return bounded(v.x, limit) && bounded(v.y, limit) && bounded(v.z, limit);
}
auto valid_bounds(const PointBounds& p) -> bool {
  return bounded(p.lower) && bounded(p.upper) && p.lower.x <= p.upper.x &&
         p.lower.y <= p.upper.y && p.lower.z <= p.upper.z;
}
auto valid_solid(const Solid& s) -> bool {
  if (!valid_bounds(s.first) || !valid_bounds(s.second) ||
      !bounded(s.half_size_metres) || !bounded(s.radius_metres))
    return false;
  if (s.shape == Shape::box)
    return s.half_size_metres.x > 0 && s.half_size_metres.y > 0 &&
           s.half_size_metres.z > 0 && s.radius_metres == 0;
  return s.shape == Shape::capsule && s.radius_metres > 0 &&
         s.half_size_metres == Vec{};
}
auto finite_triangle(const std::array<Vec, 3>& p) -> bool {
  return std::ranges::all_of(p, [](Vec v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
  });
}
auto valid_triangle(const std::array<Vec, 3>& p) -> bool {
  // Numeric controls only. Immutable source triangles are finite, and are
  // NOT truncated to the private caller's eight-metre workspace.
  return std::ranges::all_of(p, [](Vec v) { return bounded(v); });
}
auto component(Vec v, std::size_t i) -> double {
  return i == 0 ? v.x : i == 1 ? v.y : v.z;
}
auto interval(double low, double high) -> Interval {
  return {low, high, std::isfinite(low) && std::isfinite(high) && low <= high};
}
auto point(double x) -> Interval {
  return interval(x, x);
}
auto down(double x) -> double {
  return std::nextafter(x, -infinity);
}
auto up(double x) -> double {
  return std::nextafter(x, infinity);
}
auto zero(Interval x) -> bool {
  return x.supported && x.low == 0 && x.high == 0;
}
auto add(Interval a, Interval b) -> Interval {
  if (!a.supported || !b.supported) return {};
  if (zero(a)) return b;
  if (zero(b)) return a;
  return interval(down(a.low + b.low), up(a.high + b.high));
}
auto negative(Interval x) -> Interval {
  return {-x.high, -x.low, x.supported};
}
auto subtract(Interval a, Interval b) -> Interval {
  if (a.supported && b.supported && a.low == a.high && b.low == b.high &&
      a.low == b.low)
    return point(0);
  return add(a, negative(b));
}
auto multiply(Interval a, Interval b) -> Interval {
  if (!a.supported || !b.supported) return {};
  if (zero(a) || zero(b)) return point(0);
  if (a.low == 1 && a.high == 1) return b;
  if (b.low == 1 && b.high == 1) return a;
  const std::array products{a.low * b.low, a.low * b.high, a.high * b.low,
                            a.high * b.high};
  return interval(down(*std::ranges::min_element(products)),
                  up(*std::ranges::max_element(products)));
}
auto square(Interval x) -> Interval {
  if (!x.supported) return {};
  if (zero(x)) return point(0);
  const auto a = x.low * x.low, b = x.high * x.high;
  return interval(
      x.low <= 0 && x.high >= 0 ? 0 : std::max(0., down(std::min(a, b))),
      up(std::max(a, b)));
}
enum class Sign : std::uint8_t { zero, negative, positive, unsupported };
auto squared_relation(double r, double x) -> Sign {
  if (!std::isfinite(r) || !std::isfinite(x) ||
      (r != 0 && 2 * std::ilogb(r) < -970))
    return Sign::unsupported;
  const auto product = r * r;
  if (!std::isfinite(product)) return Sign::unsupported;
  const std::array terms{product, std::fma(r, r, -product), -x};
  SquareScratch scratch;
  for (const auto term : terms) {
    if (!std::isfinite(term)) return Sign::unsupported;
    if (term == 0) continue;
    scratch.next = {};
    std::size_t count{};
    auto carry = term;
    for (std::size_t i = 0; i < scratch.count; ++i) {
      const auto high = carry + scratch.values[i];
      const auto bv = high - carry, av = high - bv;
      const auto low = (carry - av) + (scratch.values[i] - bv);
      if (!std::isfinite(high) || !std::isfinite(low)) return Sign::unsupported;
      if (low != 0) {
        if (count == scratch.next.size()) return Sign::unsupported;
        scratch.next[count++] = low;
      }
      carry = high;
    }
    if (carry != 0) {
      if (count == scratch.next.size()) return Sign::unsupported;
      scratch.next[count++] = carry;
    }
    scratch.values = scratch.next;
    scratch.count = count;
  }
  return scratch.count == 0                      ? Sign::zero
         : scratch.values[scratch.count - 1] < 0 ? Sign::negative
                                                 : Sign::positive;
}
auto verified_root(Interval x) -> Interval {
  if (!x.supported || x.low < 0 || x.high > 16384) return {};
  if (zero(x)) return point(0);
  auto low = std::sqrt(x.low), high = std::sqrt(x.high);
  bool low_ok{}, high_ok{};
  for (std::size_t i = 0; i < 4; ++i) {
    const auto sign = squared_relation(low, x.low);
    if (sign == Sign::zero || sign == Sign::negative) {
      low_ok = true;
      break;
    }
    if (sign == Sign::unsupported) return {};
    low = down(low);
  }
  for (std::size_t i = 0; i < 4; ++i) {
    const auto sign = squared_relation(high, x.high);
    if (sign == Sign::zero || sign == Sign::positive) {
      high_ok = true;
      break;
    }
    if (sign == Sign::unsupported) return {};
    high = up(high);
  }
  return low_ok && high_ok ? interval(low, high) : Interval{};
}
auto norm(Vec n) -> Interval {
  return verified_root(
      add(add(square(point(n.x)), square(point(n.y))), square(point(n.z))));
}
auto dot_bounds(const PointBounds& p, Vec n) -> Interval {
  return add(add(multiply(interval(p.lower.x, p.upper.x), point(n.x)),
                 multiply(interval(p.lower.y, p.upper.y), point(n.y))),
             multiply(interval(p.lower.z, p.upper.z), point(n.z)));
}
auto dot_point(Vec p, Vec n) -> Interval {
  return add(
      add(multiply(point(p.x), point(n.x)), multiply(point(p.y), point(n.y))),
      multiply(point(p.z), point(n.z)));
}
auto support(const WorkingPart& part, double common_y, Vec n) -> Interval {
  auto projected = dot_bounds(part.first, n);
  if (part.shape == Shape::box) {
    auto extent = point(0);
    for (std::size_t i = 0; i < 3; ++i)
      extent = add(extent, multiply(point(component(part.half, i)),
                                    point(std::abs(component(n, i)))));
    projected = add(projected, extent);
  } else {
    const auto second = dot_bounds(part.second, n);
    if (!projected.supported || !second.supported) return {};
    projected = add(interval(std::max(projected.low, second.low),
                             std::max(projected.high, second.high)),
                    multiply(point(part.radius), norm(n)));
  }
  return add(projected, multiply(point(common_y), point(n.y)));
}
auto scalar_bounds(Interval x) -> BoardingFootSiteScalarBounds {
  return x.supported ? BoardingFootSiteScalarBounds{x.low, x.high, true}
                     : BoardingFootSiteScalarBounds{};
}
auto negate(Vec v) -> Vec {
  return {-v.x, -v.y, -v.z};
}
auto proposal_subtract(Vec a, Vec b) -> Vec {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto proposal_add(Vec a, Vec b) -> Vec {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto proposal_scale(Vec a, double x) -> Vec {
  return {a.x * x, a.y * x, a.z * x};
}
auto proposal_dot(Vec a, Vec b) -> double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
auto proposal_cross(Vec a, Vec b) -> Vec {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
// These unchanged historical formulas propose axes only.
auto closest_segment(Vec p, Vec a, Vec b) -> Vec {
  const auto d = proposal_subtract(b, a);
  const auto denominator = proposal_dot(d, d);
  if (!(denominator > 0) || !std::isfinite(denominator)) return a;
  return proposal_add(
      a, proposal_scale(d, std::clamp(proposal_dot(proposal_subtract(p, a), d) /
                                          denominator,
                                      0., 1.)));
}
auto closest_triangle(Vec p, const std::array<Vec, 3>& v) -> Vec {
  const auto ab = proposal_subtract(v[1], v[0]),
             ac = proposal_subtract(v[2], v[0]),
             ap = proposal_subtract(p, v[0]);
  const auto d1 = proposal_dot(ab, ap), d2 = proposal_dot(ac, ap);
  if (d1 <= 0 && d2 <= 0) return v[0];
  const auto bp = proposal_subtract(p, v[1]);
  const auto d3 = proposal_dot(ab, bp), d4 = proposal_dot(ac, bp);
  if (d3 >= 0 && d4 <= d3) return v[1];
  const auto vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0)
    return proposal_add(v[0], proposal_scale(ab, d1 / (d1 - d3)));
  const auto cp = proposal_subtract(p, v[2]);
  const auto d5 = proposal_dot(ab, cp), d6 = proposal_dot(ac, cp);
  if (d6 >= 0 && d5 <= d6) return v[2];
  const auto vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0)
    return proposal_add(v[0], proposal_scale(ac, d2 / (d2 - d6)));
  const auto va = d3 * d6 - d5 * d4;
  if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0)
    return proposal_add(v[1],
                        proposal_scale(proposal_subtract(v[2], v[1]),
                                       (d4 - d3) / ((d4 - d3) + (d5 - d6))));
  const auto denominator = va + vb + vc;
  if (denominator == 0 || !std::isfinite(denominator)) return v[0];
  return proposal_add(proposal_add(v[0], proposal_scale(ab, vb / denominator)),
                      proposal_scale(ac, vc / denominator));
}
auto closest_segments(Vec p, Vec q, Vec a, Vec b) -> Vec {
  const auto d = proposal_subtract(q, p), e = proposal_subtract(b, a),
             r = proposal_subtract(p, a);
  const auto dd = proposal_dot(d, d), ee = proposal_dot(e, e),
             de = proposal_dot(d, e), dr = proposal_dot(d, r),
             er = proposal_dot(e, r);
  if (!(dd > 0)) return proposal_subtract(p, closest_segment(p, a, b));
  if (!(ee > 0)) return proposal_subtract(closest_segment(a, p, q), a);
  const auto denominator = dd * ee - de * de;
  auto s = denominator > 0
               ? std::clamp((de * er - dr * ee) / denominator, 0., 1.)
               : 0.;
  auto t = (de * s + er) / ee;
  if (t < 0) {
    t = 0;
    s = std::clamp(-dr / dd, 0., 1.);
  } else if (t > 1) {
    t = 1;
    s = std::clamp((de - dr) / dd, 0., 1.);
  }
  return proposal_subtract(proposal_add(p, proposal_scale(d, s)),
                           proposal_add(a, proposal_scale(e, t)));
}
auto midpoint(const PointBounds& p, double common_y) -> Vec {
  // Reporting/proposal values only; never a support or contact input.
  return {(p.lower.x + p.upper.x) * .5, (p.lower.y + p.upper.y) * .5 + common_y,
          (p.lower.z + p.upper.z) * .5};
}
auto numeric_part(const Solid& solid) -> WorkingPart {
  return {solid.first,
          solid.second,
          solid.half_size_metres,
          solid.radius_metres,
          0,
          {},
          solid.shape,
          false};
}
auto owned_part(const BoardingSourceEndpointDiagnostic& endpoint,
                std::size_t index, WorkingPart& out) -> bool {
  if (!endpoint.body || index >= endpoint.parts.size()) return false;
  const auto& binding = endpoint.parts[index];
  out = {};
  out.id = binding.id;
  if (static_cast<std::size_t>(binding.id) != index) return false;
  if (const auto* box =
          std::get_if<BoardingPlantedBodyBoxBinding>(&binding.reservation)) {
    const auto center = static_cast<std::size_t>(box->center);
    if (center >= endpoint.body->points.size() ||
        box->frame.columns != BoardingBodyFrame{}.columns ||
        !bounded(box->half_size_metres) || box->half_size_metres.x <= 0 ||
        box->half_size_metres.y <= 0 || box->half_size_metres.z <= 0)
      return false;
    out.shape = Shape::box;
    out.first = endpoint.body->points[center];
    out.second = out.first;
    out.half = box->half_size_metres;
    if (binding.id == BoardingBodyPartId::port_boot ||
        binding.id == BoardingBodyPartId::starboard_boot) {
      const auto side = binding.id == BoardingBodyPartId::port_boot ? 0U : 1U;
      const auto& leg = endpoint.legs[side];
      if (box->center !=
              (side == 0 ? BoardingPlantedBodyPointId::port_boot_center
                         : BoardingPlantedBodyPointId::starboard_boot_center) ||
          box->half_size_metres != Vec{.06, .05, .14} || !leg.plane_identity ||
          leg.boot_center_y_terms !=
              std::array{leg.plane_metres, .05,
                         -endpoint.common_translation_y_metres})
        return false;
      out.sole_identity = true;
      out.sole_plane = leg.plane_metres;
    }
  } else if (const auto* capsule =
                 std::get_if<BoardingPlantedBodyCapsuleBinding>(
                     &binding.reservation)) {
    const auto a = static_cast<std::size_t>(capsule->start),
               b = static_cast<std::size_t>(capsule->end);
    if (a >= endpoint.body->points.size() ||
        b >= endpoint.body->points.size() || !bounded(capsule->radius_metres) ||
        capsule->radius_metres <= 0)
      return false;
    out.shape = Shape::capsule;
    out.first = endpoint.body->points[a];
    out.second = endpoint.body->points[b];
    out.radius = capsule->radius_metres;
  } else
    return false;
  return valid_bounds(out.first) && valid_bounds(out.second) &&
         (!out.sole_identity || bounded(out.sole_plane));
}
auto enclosing_bounds(Workspace& w, double common_y) -> bool {
  for (std::size_t i = 0; i < world_axes.size(); ++i) {
    w.maximum[i] = support(w.part, common_y, world_axes[i]);
    w.minimum[i] = negative(support(w.part, common_y, negate(world_axes[i])));
    if (!w.maximum[i].supported || !w.minimum[i].supported) return false;
  }
  // This is the ONE registered exact-bound tightening, not crop clipping.
  if (w.part.sole_identity) {
    if (w.minimum[1].low > w.part.sole_plane ||
        w.minimum[1].high < w.part.sole_plane)
      return false;
    w.minimum[1] = point(w.part.sole_plane);
  }
  return true;
}
auto broad_certificate(const Workspace& w, const std::array<Vec, 3>& triangle,
                       Pair& result) -> bool {
  for (std::size_t i = 0; i < world_axes.size(); ++i) {
    auto low = component(triangle[0], i), high = low;
    for (std::size_t v = 1; v < triangle.size(); ++v) {
      low = std::min(low, component(triangle[v], i));
      high = std::max(high, component(triangle[v], i));
    }
    if (high <= w.minimum[i].low || w.maximum[i].high <= low) {
      result.certified = true;
      result.arithmetic_supported = true;
      result.separating_direction = world_axes[i];
      result.certificate_gap =
          scalar_bounds(high <= w.minimum[i].low
                            ? subtract(point(w.minimum[i].low), point(high))
                            : subtract(point(low), point(w.maximum[i].high)));
      return true;
    }
  }
  return false;
}
auto make_axes(Workspace& w, double common_y,
               const std::array<Vec, 3>& triangle) -> std::size_t {
  std::size_t count{};
  for (const auto n : world_axes)
    w.axes[count++] = n;
  for (std::size_t i = 0; i < triangle.size(); ++i)
    w.triangle_edges[i] =
        proposal_subtract(triangle[(i + 1) % triangle.size()], triangle[i]);
  if (w.part.shape == Shape::box) {
    for (std::size_t i = 0; i < world_axes.size(); ++i)
      w.axes[count++] = proposal_cross(world_axes[i], world_axes[(i + 1) % 3]);
    w.axes[count++] = proposal_cross(w.triangle_edges[0], w.triangle_edges[1]);
    for (const auto edge : w.triangle_edges)
      for (const auto column : world_axes)
        w.axes[count++] = proposal_cross(edge, column);
  } else {
    w.reporting_endpoints = {midpoint(w.part.first, common_y),
                             midpoint(w.part.second, common_y)};
    const auto a = w.reporting_endpoints[0], b = w.reporting_endpoints[1],
               segment = proposal_subtract(b, a);
    w.axes[count++] = proposal_cross(w.triangle_edges[0], w.triangle_edges[1]);
    w.axes[count++] = segment;
    for (const auto edge : w.triangle_edges)
      w.axes[count++] = proposal_cross(segment, edge);
    w.axes[count++] = proposal_subtract(a, closest_triangle(a, triangle));
    w.axes[count++] = proposal_subtract(b, closest_triangle(b, triangle));
    for (const auto p : triangle)
      w.axes[count++] = proposal_subtract(p, closest_segment(p, a, b));
    for (std::size_t i = 0; i < triangle.size(); ++i)
      w.axes[count++] = closest_segments(a, b, triangle[i],
                                         triangle[(i + 1) % triangle.size()]);
  }
  return count;
}
auto sole_certificate(const WorkingPart& part,
                      const std::array<Vec, 3>& triangle, Pair& result)
    -> bool {
  if (!part.sole_identity || !std::ranges::all_of(triangle, [&](Vec p) {
        return p.y <= part.sole_plane;
      }))
    return false;
  const auto maximum_y =
      std::max({triangle[0].y, triangle[1].y, triangle[2].y});
  result.certified = true;
  result.arithmetic_supported = true;
  result.sole_contact = true;
  result.separating_direction = world_axes[1];
  result.certificate_gap =
      scalar_bounds(subtract(point(part.sole_plane), point(maximum_y)));
  return true;
}
auto pair_certificate(Workspace& w, double common_y,
                      const std::array<Vec, 3>& triangle, std::size_t max_axes)
    -> Pair {
  Pair result;
  if (!finite_triangle(triangle)) return result;
  if (broad_certificate(w, triangle, result)) return result;
  const auto count = make_axes(w, common_y, triangle);
  bool finite_trial{};
  for (std::size_t i = 0; i < std::min(count, max_axes); ++i) {
    ++result.axes_examined;
    const auto magnitude = std::max(
        {std::abs(w.axes[i].x), std::abs(w.axes[i].y), std::abs(w.axes[i].z)});
    if (!bounded(w.axes[i], std::numeric_limits<double>::max()) ||
        !(magnitude > 0)) {
      ++result.unsupported_axes;
      continue;
    }
    const Vec n{w.axes[i].x / magnitude, w.axes[i].y / magnitude,
                w.axes[i].z / magnitude};
    if (!bounded(n, 1)) {
      ++result.unsupported_axes;
      continue;
    }
    if (i == 1 && sole_certificate(w.part, triangle, result)) return result;
    w.support_pair[0] = negative(support(w.part, common_y, negate(n)));
    w.support_pair[1] = support(w.part, common_y, n);
    bool supported = w.support_pair[0].supported && w.support_pair[1].supported;
    for (std::size_t v = 0; v < triangle.size(); ++v) {
      w.triangle_projection[v] = dot_point(triangle[v], n);
      supported = supported && w.triangle_projection[v].supported;
    }
    if (!supported) {
      ++result.unsupported_axes;
      continue;
    }
    finite_trial = true;
    const auto lo =
        std::min({w.triangle_projection[0].low, w.triangle_projection[1].low,
                  w.triangle_projection[2].low});
    const auto hi =
        std::max({w.triangle_projection[0].high, w.triangle_projection[1].high,
                  w.triangle_projection[2].high});
    if (hi <= w.support_pair[0].low || w.support_pair[1].high <= lo) {
      result.certified = true;
      result.arithmetic_supported = true;
      result.separating_direction = n;
      result.certificate_gap = scalar_bounds(
          hi <= w.support_pair[0].low
              ? subtract(point(w.support_pair[0].low), point(hi))
              : subtract(point(lo), point(w.support_pair[1].high)));
      return result;
    }
  }
  result.arithmetic_supported = finite_trial;
  result.truncated = max_axes < count;
  return result;
}
auto refusal(Payload& out, Condition condition, BoardingBodyPartId part = {},
             const detail::LowerCockpitEffectiveTriangle* triangle = nullptr,
             const Pair* pair = nullptr) -> void {
  if (out.first_refusal) return;
  out.first_refusal.emplace();
  auto& failed = *out.first_refusal;
  failed.condition = condition;
  failed.part = part;
  if (triangle) {
    failed.key = triangle->key;
    failed.source_object = triangle->source_object;
    failed.object = triangle->object;
    failed.evaluated_source_triangle = triangle->evaluated_source_triangle;
    failed.pair_index = condition == Condition::pair_capacity
                            ? out.examined_pairs
                            : out.examined_pairs - 1;
  }
  if (pair) {
    failed.axes_examined = pair->axes_examined;
    failed.unsupported_axes = pair->unsupported_axes;
  }
}
auto valid_load(const BoardingSourceEndpointLoadDiagnostic& load) -> bool {
  const auto& result = load.load;
  const auto& self = load.self;
  const auto& endpoint = self.endpoint;
  return result.complete && result.load_qualified && result.bindings_complete &&
         result.arithmetic_supported && result.placement_nonpenetrating &&
         result.contact_supported && result.projected_margin_certified &&
         result.paired_com_x_identity && result.positive_reactions &&
         result.vertical_force_identity && result.vertical_moment_identity &&
         self.complete && self.bindings_complete && self.self_qualified &&
         endpoint.complete && endpoint.body && endpoint.reservations_complete &&
         endpoint.mass_model_complete && endpoint.plane_identities &&
         endpoint.link_identities && endpoint.joint_limits_certified &&
         endpoint.common_translation_y_metres == .847;
}
} // namespace

namespace detail {
auto boarding_source_endpoint_surface_checkpoint_support_math(
    const Solid& solid, double common_y, Vec direction)
    -> std::expected<BoardingFootSiteScalarBounds, std::string> {
  if (!valid_solid(solid) || !bounded(common_y) || !bounded(direction, 64))
    return std::unexpected("Surface support finite bounded "
                           "box/capsule/translation/direction required");
  if (!environment_supported()) return BoardingFootSiteScalarBounds{};
  return scalar_bounds(support(numeric_part(solid), common_y, direction));
}
auto boarding_source_endpoint_surface_checkpoint_pair_math(
    const Solid& solid, double common_y, const std::array<Vec, 3>& triangle,
    std::size_t max_axes) -> std::expected<Pair, std::string> {
  if (!valid_solid(solid) || !bounded(common_y) || !valid_triangle(triangle) ||
      max_axes > kBoardingSourceEndpointSurfaceMaximumAxes)
    return std::unexpected(
        "Surface pair finite bounded box/capsule/translation/triangle and "
        "axes<=16 required");
  if (!environment_supported()) return Pair{};
  Workspace work;
  work.part = numeric_part(solid);
  if (!enclosing_bounds(work, common_y)) return Pair{};
  return pair_certificate(work, common_y, triangle, max_axes);
}
auto boarding_source_endpoint_surface_checkpoint_sole_math(
    std::array<double, 3> center_y_terms, double common_y, double half_y,
    const std::array<Vec, 3>& triangle)
    -> std::expected<BoardingSourceEndpointSurfaceCheckpointSoleMathEvidence,
                     std::string> {
  if (!std::ranges::all_of(center_y_terms,
                           [](double x) { return bounded(x); }) ||
      !bounded(common_y) || !bounded(half_y) || half_y <= 0 ||
      !valid_triangle(triangle))
    return std::unexpected(
        "Surface sole finite bounded terms/positive half/triangle required");
  BoardingSourceEndpointSurfaceCheckpointSoleMathEvidence result;
  if (!environment_supported()) return result;
  result.arithmetic_supported = true;
  result.plane_metres = center_y_terms[0];
  result.template_matches = common_y == .847 && half_y == .05 &&
                            center_y_terms[1] == .05 &&
                            center_y_terms[2] == -.847;
  result.all_vertices_at_or_below =
      result.template_matches && std::ranges::all_of(triangle, [&](Vec p) {
        return p.y <= result.plane_metres;
      });
  return result;
}
auto boarding_source_endpoint_surface_checkpoint_bounded(
    const OriginBoardingBootSupport& provider,
    std::size_t max_source_partitions, std::size_t max_body_records,
    std::size_t max_self_pairs, std::size_t max_self_axes,
    std::size_t max_pressure_partitions, std::uint64_t max_pairs,
    std::size_t max_axes)
    -> std::expected<BoardingSourceEndpointSurfaceCheckpointDiagnostic,
                     std::string> {
  if (max_pairs > kBoardingSourceEndpointSurfaceMaximumPairs ||
      max_axes > kBoardingSourceEndpointSurfaceMaximumAxes)
    return std::unexpected("Surface lowered pair/axis capacities required");
  auto child = boarding_source_endpoint_load_bounded(
      provider, max_source_partitions, max_body_records, max_self_pairs,
      max_self_axes, max_pressure_partitions);
  if (!child) return std::unexpected(child.error());
  BoardingSourceEndpointSurfaceCheckpointDiagnostic result{std::move(*child),
                                                           {}};
  auto& out = result.surface;
  const auto& endpoint = result.load.self.endpoint;
  const auto* contact = endpoint.sites.source.contact();
  if (!contact || !contact->stowed_partition())
    return std::unexpected("Surface immutable selected contact required");
  for (std::size_t i = 0; i < out.coverage.size(); ++i)
    out.coverage[i].part = static_cast<BoardingBodyPartId>(i);
  {
    const auto roster = visit_effective_lower_cockpit_contact(
        *contact, [](const auto&) { return false; });
    if (!roster) return std::unexpected(roster.error());
    out.effective_triangle_count = roster->total_triangles;
    out.metadata_visited_triangles = roster->visited_triangles;
  } // The metadata result is not retained beside later traversal results.
  if (out.effective_triangle_count >
      std::numeric_limits<std::uint64_t>::max() / kBoardingBodyPartCount) {
    refusal(out, Condition::source_capacity);
    return result;
  }
  out.expected_pairs = out.effective_triangle_count * kBoardingBodyPartCount;
  if (out.effective_triangle_count == 0 ||
      out.effective_triangle_count >
          kBoardingSourceEndpointSurfaceMaximumTriangles ||
      out.expected_pairs > kBoardingSourceEndpointSurfaceMaximumPairs) {
    refusal(out, Condition::source_capacity);
    return result;
  }
  if (!valid_load(result.load)) {
    refusal(out, Condition::load_prerequisite);
    return result;
  }
  if (!environment_supported()) {
    refusal(out, Condition::unsupported_arithmetic);
    return result;
  }
  Workspace work;
  out.arithmetic_supported = true;
  for (std::size_t i = 0; i < out.coverage.size(); ++i) {
    auto& coverage = out.coverage[i];
    coverage.examined = true;
    if (!owned_part(endpoint, i, work.part)) {
      refusal(out, Condition::invalid_binding, coverage.part);
      return result;
    }
    if (!enclosing_bounds(work, endpoint.common_translation_y_metres)) {
      out.arithmetic_supported = false;
      refusal(out, Condition::unsupported_arithmetic, coverage.part);
      return result;
    }
    coverage.arithmetic_supported = true;
    coverage.lower_metres = {work.minimum[0].low, work.minimum[1].low,
                             work.minimum[2].low};
    coverage.upper_metres = {work.maximum[0].high, work.maximum[1].high,
                             work.maximum[2].high};
    const auto covered = covers_lower_cockpit_bounds(
        *contact, {coverage.lower_metres, coverage.upper_metres});
    if (!covered) return std::unexpected(covered.error());
    coverage.covered = *covered;
    if (!coverage.covered) {
      refusal(out, Condition::crop_uncovered, coverage.part);
      return result;
    }
  }
  out.coverage_complete = true;
  // The separately accounted Scan context is borrowed through std::cref.
  // Pair evidence is bounded; strings/geometry stay in the owned child.
  struct Scan {
    Workspace& work;
    Payload& out;
    double common_y;
    std::uint64_t pair_cap;
    std::size_t axis_cap;
    auto operator()(const LowerCockpitEffectiveTriangle& triangle) const
        -> bool {
      ++out.visited_triangles;
      if (out.examined_pairs == pair_cap) {
        refusal(out, Condition::pair_capacity, work.part.id, &triangle);
        return false;
      }
      ++out.examined_pairs;
      if (!triangle.obstacle || !finite_triangle(triangle.obstacle->points)) {
        out.arithmetic_supported = false;
        refusal(out, Condition::unsupported_arithmetic, work.part.id,
                &triangle);
        return false;
      }
      const auto pair =
          pair_certificate(work, common_y, triangle.obstacle->points, axis_cap);
      out.axes_examined += pair.axes_examined;
      out.unsupported_axes += pair.unsupported_axes;
      if (pair.certified) {
        ++out.certified_pairs;
        if (pair.axes_examined == 0)
          ++out.broad_certified_pairs;
        else if (pair.sole_contact)
          ++out.sole_certified_pairs;
        else
          ++out.support_certified_pairs;
        return true;
      }
      const auto reason = pair.truncated ? Condition::axis_capacity
                          : !pair.arithmetic_supported
                              ? Condition::unsupported_arithmetic
                              : Condition::no_separation_certificate;
      if (reason == Condition::unsupported_arithmetic)
        out.arithmetic_supported = false;
      refusal(out, reason, work.part.id, &triangle, &pair);
      return false;
    }
  };
  // Scan itself is an explicitly accounted callback/capture value. References
  // avoid copying Workspace, child or output into std::function storage.
  static_assert(sizeof(Scan) <= 5 * sizeof(std::uint64_t));
  static_assert(sizeof(Scan) + sizeof(VisitorFunction) + sizeof(VisitorResult) +
                    sizeof(std::reference_wrapper<const Scan>) <=
                callback_scratch);
  const Scan scan{work, out, endpoint.common_translation_y_metres, max_pairs,
                  max_axes};
  for (std::size_t i = 0; i < out.coverage.size(); ++i) {
    if (!owned_part(endpoint, i, work.part) ||
        !enclosing_bounds(work, endpoint.common_translation_y_metres)) {
      out.arithmetic_supported = false;
      refusal(out, Condition::unsupported_arithmetic, out.coverage[i].part);
      return result;
    }
    const auto visit =
        visit_effective_lower_cockpit_contact(*contact, std::cref(scan));
    if (!visit) return std::unexpected(visit.error());
    if (visit->total_triangles != out.effective_triangle_count) {
      refusal(out, Condition::source_roster_changed, work.part.id);
      return result;
    }
    if (!visit->complete) return result;
  }
  out.comparisons_complete =
      out.examined_pairs == out.expected_pairs &&
      out.certified_pairs == out.expected_pairs &&
      out.certified_pairs == out.broad_certified_pairs +
                                 out.sole_certified_pairs +
                                 out.support_certified_pairs;
  out.surface_qualified = out.coverage_complete && out.arithmetic_supported &&
                          out.comparisons_complete && !out.first_refusal;
  out.complete = out.surface_qualified;
  return result;
}
} // namespace detail

auto assess_origin_boarding_source_endpoint_surface_checkpoint(
    const OriginBoardingBootSupport& provider)
    -> std::expected<BoardingSourceEndpointSurfaceCheckpointDiagnostic,
                     std::string> {
  return detail::boarding_source_endpoint_surface_checkpoint_bounded(provider);
}
namespace {
using SweepContext = detail::BoardingLowerFootTransferSurfaceContext;
using SweepCoverage = BoardingLowerFootTransferSurfaceSweepCoverage;
using SweepPayload = BoardingLowerFootTransferSurfaceSweepPayload;
static_assert(sizeof(SweepPayload) <=
              kBoardingLowerFootTransferSurfaceMaximumPayloadBytes);
static_assert(sizeof(SweepContext) == 872);
// The owned child/fixed payload are separate. Include the immutable union
// context, one reused kernel workspace, original nested/closest-feature pool,
// callback/contact state, adapter arguments/returns and extra consumer
// controls.
constexpr std::size_t sweep_adapter_control_scratch =
    2 * sizeof(std::expected<SweepCoverage, std::string>) +
    2 * sizeof(std::expected<Pair, std::string>) + 32 * sizeof(std::size_t) +
    sizeof(std::expected<SweepContext, std::string>);
// Includes lowered immutable limits, the new refusal return/emplacement pool,
// Root's <=256-byte Scan/pair/std::function/visitor/reference-wrapper pool,
// 64 additional scalar/pointer controls and the crop's expected<bool> return.
// The existing callback/contact pools are retained conservatively as well.
constexpr std::size_t sweep_consumer_control_scratch =
    sizeof(detail::BoardingLowerFootTransferSurfaceSweepLimits) +
    sizeof(BoardingLowerFootTransferSurfaceSweepRefusal) + 256 +
    64 * sizeof(std::size_t) + sizeof(std::expected<bool, std::string>);
// Short literal helper errors may coexist with their propagated copy; account
// for character storage as well as the expected/string objects above.
constexpr std::size_t sweep_error_character_scratch{512};
constexpr std::size_t sweep_live_scratch =
    sizeof(SweepContext) + sizeof(Workspace) + nested_scratch +
    return_and_callback_scratch + contact_helper_scratch +
    sweep_adapter_control_scratch + sweep_consumer_control_scratch +
    sizeof(SweepPayload) + sweep_error_character_scratch;
// The extra fixed payload conservatively covers non-NRVO move/return staging.
static_assert(sizeof(SweepPayload) == 1200);
static_assert(sweep_live_scratch == 8176);
static_assert(sweep_live_scratch <=
              kBoardingLowerFootTransferSurfaceMaximumScratchBytes);

auto sweep_binding_equal(const BoardingPlantedBodyPartBinding& a,
                         const BoardingPlantedBodyPartBinding& b) -> bool {
  if (a.id != b.id || a.mass.first != b.mass.first ||
      a.mass.second != b.mass.second || a.mass.weight != b.mass.weight ||
      a.reservation.index() != b.reservation.index())
    return false;
  if (const auto* box =
          std::get_if<BoardingPlantedBodyBoxBinding>(&a.reservation)) {
    const auto& other = std::get<BoardingPlantedBodyBoxBinding>(b.reservation);
    return box->center == other.center &&
           box->half_size_metres == other.half_size_metres &&
           box->frame.columns == other.frame.columns;
  }
  const auto& capsule =
      std::get<BoardingPlantedBodyCapsuleBinding>(a.reservation);
  const auto& other =
      std::get<BoardingPlantedBodyCapsuleBinding>(b.reservation);
  return capsule.start == other.start && capsule.end == other.end &&
         capsule.radius_metres == other.radius_metres;
}
auto sweep_evidence_valid(const BoardingPlantedBodyPointEvidence& point)
    -> bool {
  if (!valid_bounds(point.value)) return false;
  for (const auto& d :
       {point.derivatives.velocity, point.derivatives.acceleration})
    if (!bounded(d.lower, 4096) || !bounded(d.upper, 4096) ||
        d.lower.x > d.upper.x || d.lower.y > d.upper.y || d.lower.z > d.upper.z)
      return false;
  return true;
}
auto sweep_same_bounds(const PointBounds& a, const PointBounds& b) -> bool {
  return a.lower == b.lower && a.upper == b.upper;
}
auto sweep_stationary_boot(const BoardingLowerFootTransferCell& cell,
                           const BoardingSourceEndpointDiagnostic& endpoint,
                           std::size_t side) -> bool {
  const auto point = static_cast<std::size_t>(
      side == 0 ? BoardingPlantedBodyPointId::port_boot_center
                : BoardingPlantedBodyPointId::starboard_boot_center);
  const auto& boot = cell.points[point];
  const auto& initial = endpoint.body->points[point];
  return cell.legs[side].plane_identity &&
         sweep_same_bounds(boot.value, initial) &&
         boot.derivatives.velocity.lower == Vec{} &&
         boot.derivatives.velocity.upper == Vec{} &&
         boot.derivatives.acceleration.lower == Vec{} &&
         boot.derivatives.acceleration.upper == Vec{};
}
auto sweep_world_part(const BoardingLowerFootTransferDiagnostic& child,
                      std::size_t part_index, const PointBounds* union_points,
                      const BoardingLowerFootTransferCell* cell,
                      WorkingPart& out) -> bool {
  if (part_index >= child.parts.size() ||
      (union_points == nullptr) == (cell == nullptr))
    return false;
  const auto& binding = child.parts[part_index];
  if (static_cast<std::size_t>(binding.id) != part_index) return false;
  const auto point_bounds =
      [&](BoardingPlantedBodyPointId id) -> const PointBounds& {
    const auto index = static_cast<std::size_t>(id);
    return cell ? cell->points[index].value : union_points[index];
  };
  out = {};
  out.id = binding.id;
  if (const auto* box =
          std::get_if<BoardingPlantedBodyBoxBinding>(&binding.reservation)) {
    if (static_cast<std::size_t>(box->center) >=
            kBoardingPlantedBodyPointCount ||
        box->frame.columns != BoardingBodyFrame{}.columns ||
        !bounded(box->half_size_metres) || box->half_size_metres.x <= 0 ||
        box->half_size_metres.y <= 0 || box->half_size_metres.z <= 0)
      return false;
    out.shape =
        Shape::box; // Full WORLD trunk/helmet boxes, never self ellipsoids.
    out.first = point_bounds(box->center);
    out.second = out.first;
    out.half = box->half_size_metres;
    if (binding.id == BoardingBodyPartId::port_boot ||
        binding.id == BoardingBodyPartId::starboard_boot) {
      const auto side = binding.id == BoardingBodyPartId::port_boot ? 0U : 1U;
      const auto& leg = child.initial.self.endpoint.legs[side];
      if (box->center !=
              (side == 0 ? BoardingPlantedBodyPointId::port_boot_center
                         : BoardingPlantedBodyPointId::starboard_boot_center) ||
          box->half_size_metres != Vec{.06, .05, .14} || !leg.plane_identity ||
          leg.boot_center_y_terms != std::array{leg.plane_metres, .05, -.847} ||
          child.common_translation_y_metres != .847)
        return false;
      out.sole_identity = true;
      out.sole_plane = leg.plane_metres;
    }
  } else {
    const auto& capsule =
        std::get<BoardingPlantedBodyCapsuleBinding>(binding.reservation);
    if (static_cast<std::size_t>(capsule.start) >=
            kBoardingPlantedBodyPointCount ||
        static_cast<std::size_t>(capsule.end) >=
            kBoardingPlantedBodyPointCount ||
        !bounded(capsule.radius_metres) || capsule.radius_metres <= 0)
      return false;
    out.shape = Shape::capsule;
    out.first = point_bounds(capsule.start);
    out.second = point_bounds(capsule.end);
    out.radius = capsule.radius_metres;
  }
  return valid_bounds(out.first) && valid_bounds(out.second);
}
auto sweep_coverage(const Workspace& work) -> SweepCoverage {
  return {{work.minimum[0].low, work.minimum[1].low, work.minimum[2].low},
          {work.maximum[0].high, work.maximum[1].high, work.maximum[2].high},
          work.part.id,
          true,
          true,
          false};
}
} // namespace

namespace detail {
auto prepare_boarding_lower_foot_transfer_surface(
    const BoardingLowerFootTransferDiagnostic& child)
    -> std::expected<SweepContext, std::string> {
  const auto& endpoint = child.initial.self.endpoint;
  const auto* contact = endpoint.sites.source.contact();
  if (!child.complete || !child.arithmetic_supported ||
      !child.kinematics_complete || !child.timing_complete ||
      !child.self_complete || !child.nominal_vertical_equilibrium_complete ||
      !child.finite_pressure_complete || child.first_refusal ||
      child.transfer_version != kBoardingLowerFootTransferVersion ||
      child.initial.load.load_version != kBoardingSourceEndpointLoadVersion ||
      child.initial.self.self_version != kBoardingSourceEndpointSelfVersion ||
      endpoint.endpoint_version != kBoardingSourceEndpointVersion ||
      endpoint.sites.sites_version != kBoardingFootSitesVersion ||
      !valid_load(child.initial) || !contact || !contact->stowed_partition() ||
      child.common_translation_y_metres != .847 ||
      child.seconds_per_parameter != 4 ||
      !std::isfinite(child.requested_first) ||
      !std::isfinite(child.requested_last) || child.requested_first < 0 ||
      child.requested_first > 1 || child.requested_last < 0 ||
      child.requested_last > 1 ||
      child.reverse != (child.requested_first > child.requested_last) ||
      child.reporting_elapsed_seconds !=
          4 * std::abs(child.requested_last - child.requested_first) ||
      child.cells.empty() ||
      child.cells.size() > kBoardingLowerFootTransferMaximumLeaves)
    return std::unexpected("Surface sweep requires genuine complete registered "
                           "transfer and selected source");
  if (!environment_supported())
    return std::unexpected(
        "Surface sweep requires supported RN/subnormal arithmetic");
  WorkingPart part;
  for (std::size_t i = 0; i < child.parts.size(); ++i)
    if (!sweep_binding_equal(child.parts[i], endpoint.parts[i]) ||
        !owned_part(endpoint, i, part))
      return std::unexpected("Surface sweep original WORLD binding required");
  SweepContext result(child);
  auto previous = std::min(child.requested_first, child.requested_last);
  bool first{true};
  for (const auto& cell : child.cells) {
    if (!cell.complete || !cell.arithmetic_supported ||
        !cell.kinematics_complete || !cell.timing_complete ||
        !cell.self_complete || !cell.positive_reactions ||
        !cell.nominal_vertical_equilibrium_complete ||
        !cell.finite_pressure_complete || !std::isfinite(cell.first) ||
        !std::isfinite(cell.last) || cell.first != previous ||
        cell.first > cell.last || !sweep_evidence_valid(cell.center_of_mass) ||
        !std::ranges::all_of(cell.mass_points, sweep_evidence_valid) ||
        !sweep_stationary_boot(cell, endpoint, 0) ||
        !sweep_stationary_boot(cell, endpoint, 1))
      return std::unexpected("Surface sweep requires unchanged complete closed "
                             "cells and stationary original soles");
    for (std::size_t i = 0; i < cell.points.size(); ++i) {
      const auto& p = cell.points[i];
      if (!sweep_evidence_valid(p))
        return std::unexpected(
            "Surface sweep finite ordered supported point jets required");
      auto& u = result.union_points_[i];
      if (first)
        u = p.value;
      else {
        u.lower = {std::min(u.lower.x, p.value.lower.x),
                   std::min(u.lower.y, p.value.lower.y),
                   std::min(u.lower.z, p.value.lower.z)};
        u.upper = {std::max(u.upper.x, p.value.upper.x),
                   std::max(u.upper.y, p.value.upper.y),
                   std::max(u.upper.z, p.value.upper.z)};
      }
    }
    first = false;
    previous = cell.last;
  }
  if (previous != std::max(child.requested_first, child.requested_last))
    return std::unexpected(
        "Surface sweep complete requested closed cover required");
  return result;
}
auto boarding_lower_foot_transfer_surface_union_bounds(
    const SweepContext& context, std::size_t part)
    -> std::expected<SweepCoverage, std::string> {
  if (!context.child_ || part >= context.child_->parts.size())
    return std::unexpected("Surface sweep owned part index required");
  if (!environment_supported()) return SweepCoverage{};
  Workspace work;
  if (!sweep_world_part(*context.child_, part, context.union_points_.data(),
                        nullptr, work.part))
    return std::unexpected(
        "Surface sweep genuine original union binding required");
  if (!enclosing_bounds(work, context.child_->common_translation_y_metres))
    return SweepCoverage{};
  return sweep_coverage(work);
}
auto boarding_lower_foot_transfer_surface_union_broad(
    const SweepCoverage& coverage, const std::array<Vec, 3>& triangle)
    -> std::expected<Pair, std::string> {
  if (!coverage.arithmetic_supported || !finite_triangle(triangle) ||
      !bounded(coverage.lower_metres, std::numeric_limits<double>::max()) ||
      !bounded(coverage.upper_metres, std::numeric_limits<double>::max()) ||
      coverage.lower_metres.x > coverage.upper_metres.x ||
      coverage.lower_metres.y > coverage.upper_metres.y ||
      coverage.lower_metres.z > coverage.upper_metres.z)
    return std::unexpected("Surface union broad finite original triangle and "
                           "immutable ordered AABB required");
  if (!environment_supported()) return Pair{};
  Workspace work;
  for (std::size_t i = 0; i < world_axes.size(); ++i) {
    work.minimum[i] = point(component(coverage.lower_metres, i));
    work.maximum[i] = point(component(coverage.upper_metres, i));
  }
  Pair result;
  result.arithmetic_supported = true;
  static_cast<void>(broad_certificate(work, triangle, result));
  return result;
}
auto boarding_lower_foot_transfer_surface_cell_pair(
    const SweepContext& context, std::size_t part, std::size_t cell,
    const LowerCockpitEffectiveTriangle& triangle, std::size_t max_axes)
    -> std::expected<Pair, std::string> {
  if (!context.child_ || part >= context.child_->parts.size() ||
      cell >= context.child_->cells.size() || !triangle.obstacle ||
      !finite_triangle(triangle.obstacle->points) ||
      max_axes > kBoardingLowerFootTransferSurfaceMaximumPairAxes)
    return std::unexpected("Surface sweep genuine part/cell and finite "
                           "original source triangle/axes required");
  if (!environment_supported()) return Pair{};
  Workspace work;
  if (!sweep_world_part(*context.child_, part, nullptr,
                        &context.child_->cells[cell], work.part))
    return std::unexpected(
        "Surface sweep original per-cell WORLD binding required");
  if (!enclosing_bounds(work, context.child_->common_translation_y_metres))
    return Pair{};
  // The selected visitor supplied this actual triangle. Its coordinates are
  // finite-only; never pass through the old <=8 private numeric seam.
  return pair_certificate(work, context.child_->common_translation_y_metres,
                          triangle.obstacle->points, max_axes);
}
auto boarding_lower_foot_transfer_surface_union_bounds_math(const Solid& solid,
                                                            double common_y)
    -> std::expected<SweepCoverage, std::string> {
  if (!valid_solid(solid) || !bounded(common_y))
    return std::unexpected("Numeric union WORLD box/capsule finite bounds and "
                           "common placement required");
  if (!environment_supported()) return SweepCoverage{};
  Workspace work;
  work.part = numeric_part(solid); // No source authority or sole enrollment.
  if (!enclosing_bounds(work, common_y)) return SweepCoverage{};
  return sweep_coverage(work);
}
} // namespace detail
} // namespace apsis_drift
