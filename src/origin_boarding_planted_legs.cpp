#include "apsis_drift/origin_boarding_planted_legs.hpp"
#include "apsis_drift/origin_boarding_source_endpoint.hpp"
#include "apsis_drift/origin_boarding_source_endpoint_self.hpp"
#include "origin_boarding_foot_sites_internal.hpp"
#include "origin_boarding_lower_foot_transfer_internal.hpp"
#include "origin_boarding_planted_legs_internal.hpp"
#include "origin_boarding_self_model02_internal.hpp"
#include "origin_boarding_source_endpoint_internal.hpp"
#include "origin_boarding_source_endpoint_self_internal.hpp"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace apsis_drift {
namespace {
using Vec = RigidVector3;
using Condition = BoardingPlantedLegCondition;
using Bounds = BoardingPlantedLegScalarBounds;
using Sign = detail::BoardingSelfSign;
constexpr double placement_y{.72}, thigh_length{.47285}, shin_length{.47478};
static_assert(.1 == 2 * .05);
constexpr auto infinity = std::numeric_limits<double>::infinity();
auto supported_environment() -> bool {
  if (!std::numeric_limits<double>::is_iec559 ||
      std::numeric_limits<double>::radix != 2 ||
      std::numeric_limits<double>::digits != 53 ||
      std::fegetround() != FE_TONEAREST)
    return false;
  // Volatile inputs force runtime operations. Bit comparisons do not themselves
  // treat denormals as zero when a host enables
  // flush-to-zero/denormals-are-zero.
  volatile double normal = std::numeric_limits<double>::min(), half = .5;
  volatile double subnormal = std::numeric_limits<double>::denorm_min(),
                  one = 1.;
  volatile double tie = 0x1p-53, above_tie = 0x1.8p-53;
  const double tiny_output = normal * half, preserved_input = subnormal * one;
  // Also check the actual arithmetic unit: platform-specific control can
  // disagree with fegetround(). Together these reject every directed mode.
  const double tie_sum = one + tie, above_tie_sum = one + above_tie;
  return std::bit_cast<std::uint64_t>(tiny_output) == 0x0008000000000000ULL &&
         std::bit_cast<std::uint64_t>(preserved_input) == 1 &&
         std::bit_cast<std::uint64_t>(tie_sum) == 0x3ff0000000000000ULL &&
         std::bit_cast<std::uint64_t>(above_tie_sum) == 0x3ff0000000000001ULL;
}
struct Interval {
  double low{}, high{};
  bool supported{};
};
auto interval(double low, double high) -> Interval {
  return {low, high, std::isfinite(low) && std::isfinite(high) && low <= high};
}
auto point(double v) -> Interval {
  return interval(v, v);
}
auto failed() -> Interval {
  return {};
}
auto down(double v) -> double {
  return std::nextafter(v, -infinity);
}
auto up(double v) -> double {
  return std::nextafter(v, infinity);
}
auto zero(Interval v) -> bool {
  return v.supported && v.low == 0 && v.high == 0;
}
auto add(Interval a, Interval b) -> Interval {
  if (!a.supported || !b.supported) return failed();
  if (zero(a)) return b;
  if (zero(b)) return a;
  return interval(down(a.low + b.low), up(a.high + b.high));
}
auto negate(Interval v) -> Interval {
  return {-v.high, -v.low, v.supported};
}
auto subtract(Interval a, Interval b) -> Interval {
  if (a.supported && b.supported && a.low == a.high && b.low == b.high &&
      a.low == b.low)
    return point(0);
  return add(a, negate(b));
}
auto multiply(Interval a, Interval b) -> Interval {
  if (!a.supported || !b.supported) return failed();
  if (zero(a) || zero(b)) return point(0);
  if (a.low == 1 && a.high == 1) return b;
  if (b.low == 1 && b.high == 1) return a;
  const std::array products{a.low * b.low, a.low * b.high, a.high * b.low,
                            a.high * b.high};
  return interval(down(*std::ranges::min_element(products)),
                  up(*std::ranges::max_element(products)));
}
auto divide(Interval a, Interval b) -> Interval {
  if (!a.supported || !b.supported || (b.low <= 0 && b.high >= 0))
    return failed();
  return multiply(a, interval(down(1 / b.high), up(1 / b.low)));
}
auto square(Interval v) -> Interval {
  if (!v.supported) return failed();
  if (zero(v)) return point(0);
  const auto a = v.low * v.low, b = v.high * v.high;
  return interval(
      v.low <= 0 && v.high >= 0 ? 0 : std::max(0., down(std::min(a, b))),
      up(std::max(a, b)));
}
auto absolute(Interval v) -> Interval {
  if (!v.supported || v.low >= 0) return v;
  if (v.high <= 0) return negate(v);
  return interval(0, std::max(-v.low, v.high));
}
// Certify each square-root endpoint with exact product residuals rather than
// treating a library square root or its rounded square as a proof.
auto squared_relation(double r, double x) -> Sign {
  if (!std::isfinite(r) || !std::isfinite(x) ||
      (r != 0 && 2 * std::ilogb(r) < -970))
    return Sign::unsupported;
  const auto p = r * r;
  if (!std::isfinite(p)) return Sign::unsupported;
  const std::array terms{p, std::fma(r, r, -p), -x};
  return detail::boarding_self_model02_linear_sign(terms);
}
auto root(Interval v) -> Interval {
  if (!v.supported || v.low < 0) return failed();
  if (zero(v)) return point(0);
  auto low = std::sqrt(v.low), high = std::sqrt(v.high);
  bool low_ok{}, high_ok{};
  for (std::size_t i = 0; i < 4; ++i) {
    const auto sign = squared_relation(low, v.low);
    if (sign == Sign::zero || sign == Sign::negative) {
      low_ok = true;
      break;
    }
    if (sign == Sign::unsupported) return failed();
    low = down(low);
  }
  for (std::size_t i = 0; i < 4; ++i) {
    const auto sign = squared_relation(high, v.high);
    if (sign == Sign::zero || sign == Sign::positive) {
      high_ok = true;
      break;
    }
    if (sign == Sign::unsupported) return failed();
    high = up(high);
  }
  return low_ok && high_ok ? interval(low, high) : failed();
}
auto bounds(Interval v) -> Bounds {
  return {v.low, v.high};
}
using Point = std::array<Interval, 3>;
auto point_bounds(const Point& p) -> BoardingPlantedLegPointBounds {
  return {{p[0].low, p[1].low, p[2].low}, {p[0].high, p[1].high, p[2].high}};
}
// Fixed Taylor polynomial, including its intervening zero coefficient, and
// Lagrange remainder |x|^(29)/29! (sin) or |x|^(28)/28! (cos).
// The exact degree argument is enclosed by the registered pi bracket.
auto trig_constant(double degrees, bool sine) -> Interval {
  const auto pi = interval(0x1.921fb54442d18p+1, 0x1.921fb54442d19p+1);
  const auto x = divide(multiply(point(degrees), pi), point(180));
  const auto x2 = square(x);
  auto term = sine ? x : point(1), sum = term;
  for (std::size_t i = 1; i < 14; ++i) {
    const auto a = static_cast<double>(sine ? 2 * i : 2 * i - 1);
    const auto b = static_cast<double>(sine ? 2 * i + 1 : 2 * i);
    term = negate(divide(multiply(term, x2), point(a * b)));
    sum = add(sum, term);
  }
  const double a = sine ? 28 : 27, b = sine ? 29 : 28;
  const auto remainder = divide(multiply(absolute(term), x2), point(a * b));
  if (!sum.supported || !remainder.supported) return failed();
  return interval(down(sum.low - remainder.high),
                  up(sum.high + remainder.high));
}
struct Limits {
  Interval sqrt3, sqrt2, sin20, cos20, sin65, cos65;
};
auto limits() -> const Limits& {
  static const Limits selected{
      root(point(3)),          root(point(2)),
      trig_constant(20, true), trig_constant(20, false),
      trig_constant(65, true), trig_constant(65, false)};
  return selected;
}
auto bounded(double v, double maximum) -> bool {
  return std::isfinite(v) && std::abs(v) <= maximum;
}
auto evaluate_leg(Point hip, double ankle_x, double ankle_z, double plane,
                  double l1, double l2, bool forward)
    -> BoardingPlantedLegEvidence {
  BoardingPlantedLegEvidence result;
  result.plane_metres = plane;
  result.ankle_y_terms = {plane, .1, -placement_y};
  result.boot_center_y_terms = {plane, .05, -placement_y};
  const auto refuse = [&](Condition condition) {
    result.limiting_condition = condition;
    return result;
  };
  if (!supported_environment())
    return refuse(Condition::unsupported_arithmetic);
  if (!std::ranges::all_of(hip,
                           [](Interval v) {
                             return v.supported && v.low >= -8 && v.high <= 8;
                           }) ||
      !bounded(ankle_x, 8) || !bounded(ankle_z, 8) || !bounded(plane, 8) ||
      !std::isfinite(l1) || !std::isfinite(l2) || l1 < 1e-6 || l1 > 4 ||
      l2 < 1e-6 || l2 > 4)
    return refuse(Condition::unsupported_arithmetic);
  if (!forward) return refuse(Condition::forward_branch);
  const Point ankle{point(ankle_x), add(point(plane), point(.1)),
                    point(ankle_z)};
  const Point boot{point(ankle_x), add(point(plane), point(.05)),
                   point(ankle_z)};
  auto canonical = [&](Point p) {
    p[1] = subtract(p[1], point(placement_y));
    return point_bounds(p);
  };
  result.hip = canonical(hip);
  result.ankle = canonical(ankle);
  result.boot_center = canonical(boot);
  result.plane_identity = true; // (.05-.05) and (-placement+placement) cancel.
  const Point d{subtract(ankle[0], hip[0]), subtract(ankle[1], hip[1]),
                subtract(ankle[2], hip[2])};
  const auto rho2 = add(square(d[0]), square(d[1])),
             D = add(rho2, square(d[2]));
  const auto rho = root(rho2);
  result.distance_squared = bounds(D);
  result.rho = bounds(rho);
  if (!D.supported || !rho.supported)
    return refuse(Condition::unsupported_arithmetic);
  if (rho.low <= 0 || D.low <= 0) return refuse(Condition::singular_plane);
  if (d[1].high >= 0) return refuse(Condition::forward_branch);
  const auto l1i = point(l1), l2i = point(l2);
  const auto maximum_reach = square(add(l1i, l2i)),
             minimum_reach = square(subtract(l1i, l2i));
  if (!maximum_reach.supported || !minimum_reach.supported ||
      D.high > maximum_reach.low || D.low < minimum_reach.high)
    return refuse(Condition::reach);
  const auto alpha =
      divide(add(subtract(square(l1i), square(l2i)), D), multiply(point(2), D));
  // Equivalent to (L1^2-alpha^2*D)/D, factored using the already certified
  // reach domain. This avoids a correlated subtraction near full extension.
  auto outer = subtract(maximum_reach, D), inner = subtract(D, minimum_reach);
  if (!outer.supported || !inner.supported)
    return refuse(Condition::unsupported_arithmetic);
  outer.low = std::max(0., outer.low);
  inner.low = std::max(0., inner.low);
  const auto gamma =
      root(divide(multiply(outer, inner), multiply(point(4), square(D))));
  result.alpha = bounds(alpha);
  result.gamma = bounds(gamma);
  if (!alpha.supported || !gamma.supported)
    return refuse(Condition::unsupported_arithmetic);
  // U=(-dx,-dy,0)/rho; N=(Uy,-Ux,0); q=N cross d.
  // q dot d=0, q^2=D and q.z=-rho exactly in the registered expressions.
  const Point q{divide(multiply(d[0], d[2]), rho),
                divide(multiply(d[1], d[2]), rho), negate(rho)};
  Point knee;
  for (std::size_t i = 0; i < 3; ++i)
    knee[i] = add(add(hip[i], multiply(alpha, d[i])), multiply(gamma, q[i]));
  if (!std::ranges::all_of(knee, [](Interval v) { return v.supported; }))
    return refuse(Condition::unsupported_arithmetic);
  result.knee = canonical(knee);
  result.link_identities = true;
  const auto complement = subtract(point(1), alpha);
  // v1=-F1*U-G1*Z and v2=-F2*U-G2*Z. Down/forward components determine
  // the same pitch convention as Body02, without approximate inverse angles.
  const auto F1 = add(multiply(alpha, rho), multiply(gamma, d[2])),
             G1 = subtract(multiply(gamma, rho), multiply(alpha, d[2])),
             F2 = subtract(multiply(complement, rho), multiply(gamma, d[2])),
             G2 = negate(add(multiply(complement, d[2]), multiply(gamma, rho)));
  const auto hip_sine = divide(G1, l1i), hip_cosine = divide(F1, l1i),
             shin_sine = divide(G2, l2i), shin_cosine = divide(F2, l2i),
             knee_cosine =
                 divide(subtract(subtract(D, square(l1i)), square(l2i)),
                        multiply(point(2), multiply(l1i, l2i))),
             roll_sine = divide(d[0], rho),
             roll_cosine = divide(negate(d[1]), rho);
  result.hip_sine = bounds(hip_sine);
  result.hip_cosine = bounds(hip_cosine);
  result.shin_sine = bounds(shin_sine);
  result.shin_cosine = bounds(shin_cosine);
  result.knee_cosine = bounds(knee_cosine);
  result.roll_sine = bounds(roll_sine);
  result.roll_cosine = bounds(roll_cosine);
  const std::array states{F1,          G1,         F2,         G2,
                          hip_sine,    hip_cosine, shin_sine,  shin_cosine,
                          knee_cosine, roll_sine,  roll_cosine};
  if (!std::ranges::all_of(states, [](Interval v) { return v.supported; }))
    return refuse(Condition::unsupported_arithmetic);
  const auto& selected = limits();
  const auto roll_limit =
      multiply(negate(d[1]), subtract(point(2), selected.sqrt3));
  if (!roll_limit.supported) return refuse(Condition::unsupported_arithmetic);
  if (absolute(d[0]).high > roll_limit.low) return refuse(Condition::hip_roll);
  const auto hip_lower = add(multiply(G1, selected.cos20),
                             multiply(F1, selected.sin20)),
             hip_upper = subtract(multiply(F1, selected.sin65),
                                  multiply(G1, selected.cos65));
  if (!hip_lower.supported || !hip_upper.supported)
    return refuse(Condition::unsupported_arithmetic);
  if (F1.low <= 0 || hip_lower.low < 0 || hip_upper.low < 0)
    return refuse(Condition::hip_flex);
  const auto knee_lower = negate(divide(selected.sqrt2, point(2)));
  if (!knee_lower.supported) return refuse(Condition::unsupported_arithmetic);
  if (knee_cosine.low < knee_lower.high) return refuse(Condition::knee_flex);
  const auto ankle_sector =
      subtract(multiply(F2, point(.5)),
               multiply(absolute(G2), divide(selected.sqrt3, point(2))));
  if (!ankle_sector.supported) return refuse(Condition::unsupported_arithmetic);
  if (F2.low <= 0 || ankle_sector.low < 0)
    return refuse(Condition::ankle_pitch);
  result.limits_certified = true;
  return result;
}
auto root_x_point(double t) -> Interval {
  if (t == 0) return point(0);
  if (t == 1) return point(.32);
  return multiply(point(.32),
                  multiply(square(point(t)),
                           subtract(point(3), multiply(point(2), point(t)))));
}
auto root_x(double first, double last) -> Interval {
  // Registered monotonicity 6*t*(1-t)>=0; endpoint enclosures, not samples.
  const auto a = root_x_point(first), b = root_x_point(last);
  if (!a.supported || !b.supported) return failed();
  return interval(std::max(0., a.low), std::min(.32, b.high));
}
auto fixture_leg(Interval x, std::size_t side) -> BoardingPlantedLegEvidence {
  const Point hip{add(x, point(side == 0 ? -.14 : .14)), point(.72),
                  point(-.16)};
  return evaluate_leg(hip, side == 0 ? .02 : .34, side == 0 ? -.35 : -.65,
                      side == 0 ? 0 : -.16, thigh_length, shin_length, true);
}
void report_angles(BoardingPlantedLegEvidence& out,
                   const BoardingPlantedLegEvidence& middle, std::size_t side) {
  if (!middle.link_identities) return;
  const auto value = [](Bounds b) {
    return b.lower + (b.upper - b.lower) * .5;
  };
  const auto degrees = 180 / std::numbers::pi;
  const auto hip = std::atan2(value(middle.hip_sine), value(middle.hip_cosine)),
             shin =
                 std::atan2(value(middle.shin_sine), value(middle.shin_cosine)),
             roll =
                 std::atan2(value(middle.roll_sine), value(middle.roll_cosine));
  out.reporting_hip_flex_degrees = hip * degrees;
  out.reporting_knee_flex_degrees = (hip - shin) * degrees;
  out.reporting_ankle_pitch_degrees = -shin * degrees;
  out.reporting_hip_abduction_degrees = (side == 0 ? -1. : 1.) * roll * degrees;
}
struct Subdivision {
  BoardingPlantedLegDiagnostic result;
  std::size_t depth_limit{}, node_limit{}, leaf_limit{};
  auto refuse(double first, double last, std::size_t side, std::size_t depth,
              Condition reason, Condition limiting) -> bool {
    result.first_refusal =
        BoardingPlantedLegRefusal{first, last, side, depth, reason, limiting};
    return false;
  }
  auto visit(double first, double last, std::size_t depth) -> bool {
    if (result.examined_nodes == node_limit)
      return refuse(first, last, 0, depth, Condition::subdivision_capacity,
                    Condition::none);
    ++result.examined_nodes;
    result.maximum_depth = std::max(result.maximum_depth, depth);
    const auto x = root_x(first, last);
    BoardingPlantedLegLeaf leaf{first, last, bounds(x), {}};
    std::size_t failed_side{};
    bool certified = true;
    for (std::size_t side = 0; side < 2; ++side) {
      leaf.legs[side] = fixture_leg(x, side);
      if (!leaf.legs[side].limits_certified && certified) {
        certified = false;
        failed_side = side;
      }
    }
    if (certified) {
      if (result.leaves.size() == leaf_limit)
        return refuse(first, last, 0, depth, Condition::subdivision_capacity,
                      Condition::none);
      const auto middle = first + (last - first) * .5;
      for (std::size_t side = 0; side < 2; ++side)
        report_angles(leaf.legs[side], fixture_leg(root_x_point(middle), side),
                      side);
      result.leaves.push_back(leaf);
      return true;
    }
    const auto condition = leaf.legs[failed_side].limiting_condition;
    if (depth == depth_limit)
      return refuse(first, last, failed_side, depth,
                    Condition::subdivision_capacity, condition);
    const auto middle = first + (last - first) * .5;
    if (!(first < middle && middle < last))
      return refuse(first, last, failed_side, depth,
                    Condition::unsplittable_interval, condition);
    return visit(first, middle, depth + 1) && visit(middle, last, depth + 1);
  }
};
auto assess(double first, double last, std::size_t depth, std::size_t nodes,
            std::size_t leaves)
    -> std::expected<BoardingPlantedLegDiagnostic, std::string> {
  if (!std::isfinite(first) || !std::isfinite(last) || first < 0 || first > 1 ||
      last < 0 || last > 1)
    return std::unexpected(
        "Planted leg parameter endpoints must be finite in [0,1]");
  if (depth > kBoardingPlantedLegMaximumDepth ||
      nodes > kBoardingPlantedLegMaximumNodes ||
      leaves > kBoardingPlantedLegMaximumLeaves)
    return std::unexpected("Planted leg subdivision exceeds registered limits");
  Subdivision query;
  query.result.requested_first = first;
  query.result.requested_last = last;
  query.result.reverse = last < first;
  query.depth_limit = depth;
  query.node_limit = nodes;
  query.leaf_limit = leaves;
  const auto low = std::min(first, last), high = std::max(first, last);
  if (!supported_environment()) {
    query.refuse(low, high, 0, 0, Condition::unsupported_arithmetic,
                 Condition::unsupported_arithmetic);
    return std::move(query.result);
  }
  query.result.leaves.reserve(std::min(leaves, std::size_t{16}));
  const auto covered = query.visit(low, high, 0);
  bool gap_free = covered && !query.result.leaves.empty();
  auto cursor = low;
  for (const auto& leaf : query.result.leaves) {
    gap_free = gap_free && leaf.first == cursor && leaf.first <= leaf.last;
    cursor = leaf.last;
  }
  gap_free = gap_free && cursor == high;
  query.result.complete = gap_free;
  query.result.plane_identities = gap_free;
  query.result.link_identities = gap_free;
  query.result.joint_limits_certified = gap_free;
  return std::move(query.result);
}

using TimingCondition = BoardingPlantedLegTimingCondition;
using Joint = BoardingPlantedLegJoint;
// Only the registered planted-leg expressions: no caller expression graph.
// First and second are derivatives with respect to the original parameter t.
struct TimingScalar {
  Interval value, first, second;
};
using TimingPoint = std::array<TimingScalar, 3>;
auto timing_constant(Interval value) -> TimingScalar {
  return {value, point(0), point(0)};
}
auto timing_constant(double value) -> TimingScalar {
  return timing_constant(point(value));
}
auto timing_supported(const TimingScalar& v) -> bool {
  return v.value.supported && v.first.supported && v.second.supported;
}
auto timing_add(const TimingScalar& a, const TimingScalar& b) -> TimingScalar {
  return {add(a.value, b.value), add(a.first, b.first),
          add(a.second, b.second)};
}
auto timing_negate(const TimingScalar& v) -> TimingScalar {
  return {negate(v.value), negate(v.first), negate(v.second)};
}
auto timing_subtract(const TimingScalar& a, const TimingScalar& b)
    -> TimingScalar {
  // Preserve equal-singleton cancellation separately in each component. An
  // exactly zero position difference can still have nonzero derivatives.
  return {subtract(a.value, b.value), subtract(a.first, b.first),
          subtract(a.second, b.second)};
}
auto timing_multiply(const TimingScalar& a, const TimingScalar& b)
    -> TimingScalar {
  return {multiply(a.value, b.value),
          add(multiply(a.first, b.value), multiply(a.value, b.first)),
          add(add(multiply(a.second, b.value), multiply(a.value, b.second)),
              multiply(point(2), multiply(a.first, b.first)))};
}
auto timing_square(const TimingScalar& v) -> TimingScalar {
  return {
      square(v.value), multiply(point(2), multiply(v.value, v.first)),
      multiply(point(2), add(square(v.first), multiply(v.value, v.second)))};
}
auto timing_reciprocal(const TimingScalar& v) -> TimingScalar {
  if (!timing_supported(v) || v.value.low <= 0) return {};
  const auto v2 = square(v.value), v3 = multiply(v2, v.value);
  return {divide(point(1), v.value), negate(divide(v.first, v2)),
          subtract(divide(multiply(point(2), square(v.first)), v3),
                   divide(v.second, v2))};
}
auto timing_divide(const TimingScalar& a, const TimingScalar& b)
    -> TimingScalar {
  return timing_multiply(a, timing_reciprocal(b));
}
auto timing_root(const TimingScalar& v) -> TimingScalar {
  if (!timing_supported(v) || v.value.low <= 0) return {};
  const auto y = root(v.value), twice_y = multiply(point(2), y),
             four_y3 = multiply(point(4), multiply(square(y), y));
  return {
      y, divide(v.first, twice_y),
      subtract(divide(v.second, twice_y), divide(square(v.first), four_y3))};
}
auto physical_first(Interval v, bool reverse) -> Interval {
  const auto scaled = divide(v, point(12));
  return reverse ? negate(scaled) : scaled;
}
auto physical_second(Interval v) -> Interval {
  return divide(v, point(144));
}
auto point_derivatives(const TimingPoint& p, bool reverse)
    -> BoardingPlantedLegPointDerivatives {
  Point first{}, second{};
  for (std::size_t i = 0; i < 3; ++i) {
    first[i] = physical_first(p[i].first, reverse);
    second[i] = physical_second(p[i].second);
  }
  return {point_bounds(first), point_bounds(second)};
}
struct AngularTiming {
  Interval first, second;
};
auto pitch_derivatives(const TimingScalar& F, const TimingScalar& G,
                       Interval length_squared) -> AngularTiming {
  // F^2+G^2=L^2 is the exact registered link identity, not a rounded norm.
  return {
      divide(subtract(multiply(F.value, G.first), multiply(G.value, F.first)),
             length_squared),
      divide(subtract(multiply(F.value, G.second), multiply(G.value, F.second)),
             length_squared)};
}
auto angular_derivatives(const AngularTiming& a, bool reverse)
    -> BoardingPlantedLegAngularDerivatives {
  return {bounds(physical_first(a.first, reverse)),
          bounds(physical_second(a.second))};
}
auto negate_angular(const AngularTiming& a) -> AngularTiming {
  return {negate(a.first), negate(a.second)};
}
auto speed_threshold() -> Interval {
  return divide(interval(0x1.921fb54442d18p+1, 0x1.921fb54442d19p+1), point(6));
}
auto joint_speed(Interval roll_rate, Interval pitch_rate)
    -> BoardingPlantedLegSpeedEvidence {
  BoardingPlantedLegSpeedEvidence result;
  if (!supported_environment()) return result;
  auto squared_speed = add(square(roll_rate), square(pitch_rate));
  if (!squared_speed.supported) return result;
  // A sum of squares is nonnegative even when outward addition crosses zero.
  squared_speed.low = std::max(0., squared_speed.low);
  const auto norm = root(squared_speed), threshold = square(speed_threshold());
  if (!norm.supported || !threshold.supported) return result;
  result.speed_radians_per_second = bounds(norm);
  result.supported = true;
  result.certified = squared_speed.high <= threshold.low;
  return result;
}
auto evaluate_timing_leg(const TimingPoint& hip, double ankle_x, double ankle_z,
                         double plane, double l1, double l2, bool forward,
                         bool reverse, std::size_t side)
    -> BoardingPlantedLegTimingEvidence {
  BoardingPlantedLegTimingEvidence result;
  const auto refuse = [&](TimingCondition condition, Bounds limiting = {}) {
    result.limiting_condition = condition;
    result.limiting_bound = limiting;
    return result;
  };
  if (!supported_environment() || side > 1 ||
      !std::ranges::all_of(hip, [](const TimingScalar& v) {
        return timing_supported(v) && v.value.low >= -8 && v.value.high <= 8 &&
               v.first.low >= -4096 && v.first.high <= 4096 &&
               v.second.low >= -4096 && v.second.high <= 4096;
      }))
    return refuse(TimingCondition::unsupported_arithmetic);
  const Point hip_value{hip[0].value, hip[1].value, hip[2].value};
  result.closure =
      evaluate_leg(hip_value, ankle_x, ankle_z, plane, l1, l2, forward);
  if (!result.closure.limits_certified)
    return refuse(TimingCondition::closure_prerequisite);
  const TimingPoint ankle{timing_constant(ankle_x),
                          timing_constant(add(point(plane), point(.1))),
                          timing_constant(ankle_z)};
  const TimingPoint boot{timing_constant(ankle_x),
                         timing_constant(add(point(plane), point(.05))),
                         timing_constant(ankle_z)};
  const TimingPoint d{timing_subtract(ankle[0], hip[0]),
                      timing_subtract(ankle[1], hip[1]),
                      timing_subtract(ankle[2], hip[2])};
  const auto rho2 = timing_add(timing_square(d[0]), timing_square(d[1])),
             D = timing_add(rho2, timing_square(d[2]));
  if (!timing_supported(rho2) || !timing_supported(D))
    return refuse(TimingCondition::unsupported_arithmetic);
  if (rho2.value.low <= 0)
    return refuse(TimingCondition::rho_derivative, bounds(rho2.value));
  if (D.value.low <= 0)
    return refuse(TimingCondition::distance_derivative, bounds(D.value));
  const auto rho = timing_root(rho2);
  const auto l1_squared = square(point(l1)), l2_squared = square(point(l2)),
             maximum_reach = square(add(point(l1), point(l2))),
             minimum_reach = square(subtract(point(l1), point(l2)));
  const auto alpha = timing_divide(
      timing_add(timing_constant(subtract(l1_squared, l2_squared)), D),
      timing_multiply(timing_constant(2), D));
  auto outer = timing_subtract(timing_constant(maximum_reach), D),
       inner = timing_subtract(D, timing_constant(minimum_reach));
  if (!timing_supported(outer) || !timing_supported(inner))
    return refuse(TimingCondition::unsupported_arithmetic);
  // The unchanged closure prerequisite has already proved the reach domain.
  // Tighten only values by that proof; do not differentiate this tightening.
  outer.value.low = std::max(0., outer.value.low);
  inner.value.low = std::max(0., inner.value.low);
  const auto gamma2 =
      timing_divide(timing_multiply(outer, inner),
                    timing_multiply(timing_constant(4), timing_square(D)));
  if (!timing_supported(gamma2))
    return refuse(TimingCondition::unsupported_arithmetic);
  if (gamma2.value.low <= 0)
    return refuse(TimingCondition::gamma_derivative, bounds(gamma2.value));
  const auto gamma = timing_root(gamma2);
  const TimingPoint q{timing_divide(timing_multiply(d[0], d[2]), rho),
                      timing_divide(timing_multiply(d[1], d[2]), rho),
                      timing_negate(rho)};
  TimingPoint knee;
  for (std::size_t i = 0; i < 3; ++i)
    knee[i] = timing_add(timing_add(hip[i], timing_multiply(alpha, d[i])),
                         timing_multiply(gamma, q[i]));
  if (!timing_supported(alpha) || !timing_supported(gamma) ||
      !std::ranges::all_of(knee, timing_supported))
    return refuse(TimingCondition::unsupported_arithmetic);
  const auto complement = timing_subtract(timing_constant(1), alpha),
             F1 = timing_add(timing_multiply(alpha, rho),
                             timing_multiply(gamma, d[2])),
             G1 = timing_subtract(timing_multiply(gamma, rho),
                                  timing_multiply(alpha, d[2])),
             F2 = timing_subtract(timing_multiply(complement, rho),
                                  timing_multiply(gamma, d[2])),
             G2 = timing_negate(timing_add(timing_multiply(complement, d[2]),
                                           timing_multiply(gamma, rho)));
  const auto hip_pitch = pitch_derivatives(F1, G1, l1_squared),
             shin_pitch = pitch_derivatives(F2, G2, l2_squared),
             knee_flex =
                 AngularTiming{subtract(hip_pitch.first, shin_pitch.first),
                               subtract(hip_pitch.second, shin_pitch.second)};
  const auto F = timing_negate(d[1]), G = d[0];
  const auto numerator =
      subtract(multiply(F.value, G.first), multiply(G.value, F.first));
  const AngularTiming phi{
      divide(numerator, rho2.value),
      subtract(divide(subtract(multiply(F.value, G.second),
                               multiply(G.value, F.second)),
                      rho2.value),
               divide(multiply(numerator, rho2.first), square(rho2.value)))};
  const std::array angular_states{hip_pitch, shin_pitch, knee_flex, phi};
  if (!std::ranges::all_of(angular_states, [](const AngularTiming& a) {
        return a.first.supported && a.second.supported;
      }))
    return refuse(TimingCondition::unsupported_arithmetic);
  result.hip = point_derivatives(hip, reverse);
  result.knee = point_derivatives(knee, reverse);
  result.ankle = point_derivatives(ankle, reverse);
  result.boot_center = point_derivatives(boot, reverse);
  result.hip_pitch = angular_derivatives(hip_pitch, reverse);
  result.shin_pitch = angular_derivatives(shin_pitch, reverse);
  result.knee_flex = angular_derivatives(knee_flex, reverse);
  result.ankle_pitch = angular_derivatives(negate_angular(shin_pitch), reverse);
  result.hip_abduction =
      angular_derivatives(side == 0 ? negate_angular(phi) : phi, reverse);
  // Both physical flat ankles compensate the same Rz(phi), independent of side.
  result.ankle_roll = angular_derivatives(negate_angular(phi), reverse);
  result.derivative_domains_certified = true;
  const auto phi_rate = physical_first(phi.first, reverse);
  result.hip_speed =
      joint_speed(phi_rate, physical_first(hip_pitch.first, reverse));
  result.knee_speed =
      joint_speed(point(0), physical_first(knee_flex.first, reverse));
  result.ankle_speed =
      joint_speed(phi_rate, physical_first(shin_pitch.first, reverse));
  const std::array speeds{result.hip_speed, result.knee_speed,
                          result.ankle_speed};
  const std::array joints{Joint::hip, Joint::knee, Joint::ankle};
  for (std::size_t i = 0; i < speeds.size(); ++i) {
    if (!speeds[i].supported || !speeds[i].certified) {
      result.limiting_joint = joints[i];
      return refuse(speeds[i].supported
                        ? TimingCondition::joint_speed
                        : TimingCondition::unsupported_arithmetic,
                    speeds[i].speed_radians_per_second);
    }
  }
  result.joint_speed_certified = true;
  return result;
}
auto root_first_point(double t) -> Interval {
  if (t == 0 || t == 1) return point(0);
  return multiply(multiply(point(.32), point(6)),
                  multiply(point(t), subtract(point(1), point(t))));
}
auto root_second_point(double t) -> Interval {
  return multiply(point(.32),
                  subtract(point(6), multiply(point(12), point(t))));
}
auto timing_root_x(double first, double last) -> TimingScalar {
  const auto a = root_first_point(first), b = root_first_point(last),
             critical = root_first_point(.5),
             second_a = root_second_point(first),
             second_b = root_second_point(last);
  if (!a.supported || !b.supported || !critical.supported ||
      !second_a.supported || !second_b.supported)
    return {};
  // x' increases up to .5 and decreases afterwards; x'' is decreasing affine.
  // The stored .32 expression, not a separately rounded .48, sets the maximum.
  const auto high =
      first <= .5 && .5 <= last ? critical.high : std::max(a.high, b.high);
  return {root_x(first, last),
          interval(std::max(0., std::min(a.low, b.low)),
                   std::min(high, critical.high)),
          interval(second_b.low, second_a.high)};
}
auto timing_fixture_leg(const TimingScalar& x, bool reverse, std::size_t side)
    -> BoardingPlantedLegTimingEvidence {
  const TimingPoint hip{timing_add(x, timing_constant(side == 0 ? -.14 : .14)),
                        timing_constant(.72), timing_constant(-.16)};
  return evaluate_timing_leg(hip, side == 0 ? .02 : .34,
                             side == 0 ? -.35 : -.65, side == 0 ? 0 : -.16,
                             thigh_length, shin_length, true, reverse, side);
}
struct TimingSubdivision {
  BoardingPlantedLegTimingDiagnostic result;
  std::size_t depth_limit{}, node_limit{}, leaf_limit{};
  auto refuse(double first, double last, std::size_t side, std::size_t depth,
              TimingCondition reason, TimingCondition limiting,
              Joint joint = Joint::none, Bounds limiting_bound = {}) -> bool {
    result.first_refusal = BoardingPlantedLegTimingRefusal{
        first, last, side, depth, reason, limiting, joint, limiting_bound};
    return false;
  }
  auto visit(double first, double last, std::size_t depth) -> bool {
    if (result.examined_nodes == node_limit)
      return refuse(first, last, 0, depth,
                    TimingCondition::subdivision_capacity,
                    TimingCondition::none);
    ++result.examined_nodes;
    result.maximum_depth = std::max(result.maximum_depth, depth);
    const auto x = timing_root_x(first, last);
    BoardingPlantedLegTimingLeaf leaf;
    leaf.first = first;
    leaf.last = last;
    TimingCondition condition = TimingCondition::none;
    Bounds limiting_bound;
    Joint joint = Joint::none;
    std::size_t failed_side{};
    if (!timing_supported(x)) {
      condition = TimingCondition::unsupported_arithmetic;
    } else {
      const TimingPoint root_point{x, timing_constant(.72),
                                   timing_constant(-.16)};
      leaf.root = point_derivatives(root_point, result.reverse);
      const auto speed = absolute(physical_first(x.first, result.reverse)),
                 acceleration = absolute(physical_second(x.second));
      leaf.root_speed = bounds(speed);
      leaf.root_acceleration = bounds(acceleration);
      if (!speed.supported || !acceleration.supported) {
        condition = TimingCondition::unsupported_arithmetic;
      } else if (speed.high > .25) {
        condition = TimingCondition::root_speed;
        limiting_bound = bounds(speed);
      } else if (acceleration.high > .10) {
        condition = TimingCondition::root_acceleration;
        limiting_bound = bounds(acceleration);
      }
      for (std::size_t side = 0; side < 2; ++side) {
        leaf.legs[side] = timing_fixture_leg(x, result.reverse, side);
        if (!leaf.legs[side].joint_speed_certified &&
            condition == TimingCondition::none) {
          condition = leaf.legs[side].limiting_condition;
          limiting_bound = leaf.legs[side].limiting_bound;
          joint = leaf.legs[side].limiting_joint;
          failed_side = side;
        }
      }
    }
    if (condition == TimingCondition::none) {
      if (result.leaves.size() == leaf_limit)
        return refuse(first, last, 0, depth,
                      TimingCondition::subdivision_capacity,
                      TimingCondition::none);
      const auto middle = first + (last - first) * .5;
      for (std::size_t side = 0; side < 2; ++side)
        report_angles(leaf.legs[side].closure,
                      fixture_leg(root_x_point(middle), side), side);
      result.leaves.push_back(leaf);
      return true;
    }
    if (depth == depth_limit)
      return refuse(first, last, failed_side, depth,
                    TimingCondition::subdivision_capacity, condition, joint,
                    limiting_bound);
    const auto middle = first + (last - first) * .5;
    if (!(first < middle && middle < last))
      return refuse(first, last, failed_side, depth,
                    TimingCondition::unsplittable_interval, condition, joint,
                    limiting_bound);
    return visit(first, middle, depth + 1) && visit(middle, last, depth + 1);
  }
};
auto assess_timing(double first, double last, std::size_t depth,
                   std::size_t nodes, std::size_t leaves)
    -> std::expected<BoardingPlantedLegTimingDiagnostic, std::string> {
  if (!std::isfinite(first) || !std::isfinite(last) || first < 0 || first > 1 ||
      last < 0 || last > 1)
    return std::unexpected(
        "Planted leg parameter endpoints must be finite in [0,1]");
  if (depth > kBoardingPlantedLegMaximumDepth ||
      nodes > kBoardingPlantedLegMaximumNodes ||
      leaves > kBoardingPlantedLegMaximumLeaves)
    return std::unexpected(
        "Planted leg timing subdivision exceeds registered limits");
  TimingSubdivision query;
  query.result.requested_first = first;
  query.result.requested_last = last;
  query.result.reverse = last < first;
  query.depth_limit = depth;
  query.node_limit = nodes;
  query.leaf_limit = leaves;
  auto closure = assess_origin_boarding_planted_legs(first, last);
  if (!closure) return std::unexpected(closure.error());
  query.result.closure = std::move(*closure);
  const auto low = std::min(first, last), high = std::max(first, last);
  if (!supported_environment()) {
    query.refuse(low, high, 0, 0, TimingCondition::unsupported_arithmetic,
                 TimingCondition::unsupported_arithmetic);
    return std::move(query.result);
  }
  query.result.reporting_elapsed_seconds = 12 * (high - low);
  if (!query.result.closure.complete) {
    const auto& refusal = query.result.closure.first_refusal;
    query.refuse(refusal ? refusal->first : low, refusal ? refusal->last : high,
                 refusal ? refusal->side : 0, refusal ? refusal->depth : 0,
                 TimingCondition::closure_prerequisite,
                 TimingCondition::closure_prerequisite);
    return std::move(query.result);
  }
  query.result.leaves.reserve(std::min(leaves, std::size_t{16}));
  const auto covered = query.visit(low, high, 0);
  bool gap_free = covered && !query.result.leaves.empty();
  auto cursor = low;
  for (const auto& leaf : query.result.leaves) {
    gap_free = gap_free && leaf.first == cursor && leaf.first <= leaf.last;
    cursor = leaf.last;
  }
  gap_free = gap_free && cursor == high;
  query.result.complete = gap_free;
  query.result.derivative_domains_certified = gap_free;
  query.result.root_speed_certified = gap_free;
  query.result.root_acceleration_certified = gap_free;
  query.result.leg_joint_speed_certified = gap_free;
  return std::move(query.result);
}
} // namespace
namespace {
using BodyPointId = BoardingPlantedBodyPointId;
using BodyPointEvidence = BoardingPlantedBodyPointEvidence;
using BodyParts =
    std::array<BoardingPlantedBodyPartBinding, kBoardingBodyPartCount>;
using BodyCondition = BoardingPlantedBodyCondition;
constexpr std::array<std::uint32_t, kBoardingBodyPartCount> body_weights{
    144, 540, 96, 120, 48, 12, 15, 10, 5, 120, 48, 12, 15, 10, 5};
static_assert([] {
  std::uint32_t sum{};
  for (const auto weight : body_weights)
    sum += weight;
  return sum == kBoardingBodyMassDenominator;
}());
auto body_box(BoardingBodyPartId id, BodyPointId center, Vec half,
              std::uint32_t weight) -> BoardingPlantedBodyPartBinding {
  return {id,
          BoardingPlantedBodyBoxBinding{center, half, {}},
          {center, center, weight}};
}
auto body_capsule(BoardingBodyPartId id, BodyPointId start, BodyPointId end,
                  double radius, std::uint32_t weight)
    -> BoardingPlantedBodyPartBinding {
  return {id,
          BoardingPlantedBodyCapsuleBinding{start, end, radius},
          {start, end, weight}};
}
auto body_parts() -> BodyParts {
  using Id = BoardingBodyPartId;
  using P = BodyPointId;
  return {
      body_box(Id::pelvis, P::root, {.24, .12, .18}, body_weights[0]),
      body_box(Id::trunk, P::trunk_center, {.26, .2695, .18}, body_weights[1]),
      body_box(Id::helmet, P::helmet_center, {.16, .18, .18}, body_weights[2]),
      body_capsule(Id::port_thigh, P::port_hip, P::port_knee, .105,
                   body_weights[3]),
      body_capsule(Id::port_shin, P::port_knee, P::port_ankle, .075,
                   body_weights[4]),
      body_box(Id::port_boot, P::port_boot_center, {.06, .05, .14},
               body_weights[5]),
      body_capsule(Id::port_upper_arm, P::port_shoulder, P::port_elbow, .065,
                   body_weights[6]),
      body_capsule(Id::port_forearm, P::port_elbow, P::port_wrist, .055,
                   body_weights[7]),
      body_box(Id::port_hand, P::port_wrist, {.04, .05, .02}, body_weights[8]),
      body_capsule(Id::starboard_thigh, P::starboard_hip, P::starboard_knee,
                   .105, body_weights[9]),
      body_capsule(Id::starboard_shin, P::starboard_knee, P::starboard_ankle,
                   .075, body_weights[10]),
      body_box(Id::starboard_boot, P::starboard_boot_center, {.06, .05, .14},
               body_weights[11]),
      body_capsule(Id::starboard_upper_arm, P::starboard_shoulder,
                   P::starboard_elbow, .065, body_weights[12]),
      body_capsule(Id::starboard_forearm, P::starboard_elbow,
                   P::starboard_wrist, .055, body_weights[13]),
      body_box(Id::starboard_hand, P::starboard_wrist, {.04, .05, .02},
               body_weights[14])};
}
auto body_index(BodyPointId id) -> std::size_t {
  return static_cast<std::size_t>(id);
}
auto body_intervals(const BoardingPlantedLegPointBounds& p) -> Point {
  return {interval(p.lower.x, p.upper.x), interval(p.lower.y, p.upper.y),
          interval(p.lower.z, p.upper.z)};
}
auto body_supported(const BodyPointEvidence& p) -> bool {
  for (const auto& b :
       {p.value, p.derivatives.velocity, p.derivatives.acceleration}) {
    if (!std::ranges::all_of(body_intervals(b),
                             [](Interval v) { return v.supported; }))
      return false;
  }
  return true;
}
// Retain failed interval state BEFORE flattening into public bound records.
// Assembly never appends the temporary leaf unless every conversion supported.
auto body_bounds(const Point& p, bool& supported)
    -> BoardingPlantedLegPointBounds {
  supported = supported &&
              std::ranges::all_of(p, [](Interval v) { return v.supported; });
  return point_bounds(p);
}
auto body_offset(const BodyPointEvidence& p, const Point& offset,
                 bool& supported) -> BodyPointEvidence {
  auto selected = body_intervals(p.value);
  for (std::size_t i = 0; i < selected.size(); ++i)
    selected[i] = add(selected[i], offset[i]);
  return {body_bounds(selected, supported), p.derivatives};
}
auto body_midpoint(const BodyPointEvidence& a, const BodyPointEvidence& b,
                   bool& supported) -> BodyPointEvidence {
  auto midpoint = [&](const BoardingPlantedLegPointBounds& first,
                      const BoardingPlantedLegPointBounds& second) {
    auto selected = body_intervals(first);
    const auto other = body_intervals(second);
    for (std::size_t i = 0; i < selected.size(); ++i)
      selected[i] = multiply(add(selected[i], other[i]), point(.5));
    return body_bounds(selected, supported);
  };
  return {midpoint(a.value, b.value),
          {midpoint(a.derivatives.velocity, b.derivatives.velocity),
           midpoint(a.derivatives.acceleration, b.derivatives.acceleration)}};
}
struct BodyScratch {
  BoardingPlantedBodyLeaf leaf;
  Point weighted_value;
};
static_assert(sizeof(BoardingPlantedBodyLeaf) <= 6144);
// Includes the assembly leaf, fixed table and conservative helper temporaries;
// the separately owned timing cover and bounded output vector are not scratch.
static_assert(sizeof(BodyScratch) + sizeof(BodyParts) + 4 * sizeof(Point) +
                  2 * sizeof(BodyPointEvidence) <=
              8192);
auto assemble_body(BodyScratch& scratch,
                   const BoardingPlantedLegTimingLeaf& timing,
                   const BodyParts& parts, Interval arm_component) -> bool {
  bool supported{true};
  auto& leaf = scratch.leaf;
  leaf.first = timing.first;
  leaf.last = timing.last;
  auto& points = leaf.points;
  auto at = [&](BodyPointId id) -> BodyPointEvidence& {
    return points[body_index(id)];
  };
  const auto x = root_x(timing.first, timing.last);
  if (!x.supported || !arm_component.supported) return false;
  at(BodyPointId::root) = {body_bounds({x, point(0), point(-.16)}, supported),
                           timing.root};
  const auto& root_point = at(BodyPointId::root);
  at(BodyPointId::trunk_center) =
      body_offset(root_point, {point(0), point(.3495), point(0)}, supported);
  at(BodyPointId::helmet_center) =
      body_offset(root_point, {point(0), point(.70237), point(0)}, supported);
  at(BodyPointId::eye) =
      body_offset(root_point, {point(0), point(.65237), point(0)}, supported);
  for (std::size_t side = 0; side < 2; ++side) {
    const auto base = side == 0 ? body_index(BodyPointId::port_hip)
                                : body_index(BodyPointId::starboard_hip);
    const auto& leg = timing.legs[side];
    // These are the internally owned SAME-expression canonical bounds, not a
    // newly reconstructed knee or a rounded inverse-angle body evaluation.
    points[base] = {leg.closure.hip, leg.hip};
    points[base + 1] = {leg.closure.knee, leg.knee};
    points[base + 2] = {leg.closure.ankle, leg.ankle};
    points[base + 3] = {leg.closure.boot_center, leg.boot_center};
    points[base + 4] = body_offset(
        root_point,
        {point(side == 0 ? -.20265 : .20265), point(.579), point(0)},
        supported);
    points[base + 5] = body_offset(
        points[base + 4], {point(0), arm_component, arm_component}, supported);
    points[base + 6] = body_offset(
        points[base + 5], {point(0), point(.386), point(0)}, supported);
  }
  if (!supported || !std::ranges::all_of(points, body_supported)) return false;
  scratch.weighted_value = {point(0), point(0), point(0)};
  for (std::size_t i = 0; i < parts.size(); ++i) {
    const auto& mass = parts[i].mass;
    const auto first = body_index(mass.first), second = body_index(mass.second);
    if (first >= points.size() || second >= points.size() ||
        static_cast<std::size_t>(parts[i].id) != i ||
        mass.weight != body_weights[i])
      return false;
    leaf.mass_points[i] =
        first == second
            ? points[first]
            : body_midpoint(points[first], points[second], supported);
    if (!supported || !body_supported(leaf.mass_points[i])) return false;
    const auto value = body_intervals(leaf.mass_points[i].value);
    for (std::size_t axis = 0; axis < value.size(); ++axis)
      scratch.weighted_value[axis] =
          add(scratch.weighted_value[axis],
              multiply(point(mass.weight), value[axis]));
  }
  for (auto& v : scratch.weighted_value)
    v = divide(v, point(kBoardingBodyMassDenominator));
  leaf.center_of_mass.value = body_bounds(scratch.weighted_value, supported);
  auto derivative = [&](bool second) {
    const auto& p = root_point.derivatives;
    const auto& a = at(BodyPointId::port_knee).derivatives;
    const auto& b = at(BodyPointId::starboard_knee).derivatives;
    auto selected = body_intervals(second ? p.acceleration : p.velocity);
    const auto port = body_intervals(second ? a.acceleration : a.velocity);
    const auto starboard = body_intervals(second ? b.acceleration : b.velocity);
    for (std::size_t axis = 0; axis < selected.size(); ++axis)
      selected[axis] =
          divide(add(multiply(point(960), selected[axis]),
                     multiply(point(84), add(port[axis], starboard[axis]))),
                 point(kBoardingBodyMassDenominator));
    return body_bounds(selected, supported);
  };
  leaf.center_of_mass.derivatives = {derivative(false), derivative(true)};
  return supported && body_supported(leaf.center_of_mass);
}
auto assess_body(double first, double last, std::size_t body_capacity,
                 std::size_t timing_depth, std::size_t timing_nodes,
                 std::size_t timing_leaves)
    -> std::expected<BoardingPlantedBodyDiagnostic, std::string> {
  if (body_capacity > kBoardingPlantedBodyMaximumLeaves)
    return std::unexpected("Planted body storage exceeds registered limits");
  auto timing =
      assess_timing(first, last, timing_depth, timing_nodes, timing_leaves);
  if (!timing) return std::unexpected(timing.error());
  BoardingPlantedBodyDiagnostic result;
  result.timing = std::move(*timing);
  result.parts = body_parts();
  const auto low = std::min(first, last), high = std::max(first, last);
  auto refuse = [&](BodyCondition condition, std::size_t index, double a,
                    double b) {
    result.first_refusal = BoardingPlantedBodyRefusal{condition, index, a, b};
  };
  if (!supported_environment()) {
    refuse(BodyCondition::unsupported_arithmetic, 0, low, high);
    return result;
  }
  if (!result.timing.complete) {
    const auto& old = result.timing.first_refusal;
    refuse(BodyCondition::timing_prerequisite, 0, old ? old->first : low,
           old ? old->last : high);
    return result;
  }
  const auto arm_component = negate(multiply(point(.35898), root(point(.5))));
  if (!arm_component.supported) {
    refuse(BodyCondition::unsupported_arithmetic, 0, low, high);
    return result;
  }
  result.leaves.reserve(std::min(body_capacity, result.timing.leaves.size()));
  BodyScratch scratch;
  auto cursor = low;
  for (const auto& selected : result.timing.leaves) {
    const auto index = result.leaves.size();
    if (selected.first != cursor || selected.first > selected.last) {
      refuse(BodyCondition::incomplete_cover, index, selected.first,
             selected.last);
      return result;
    }
    if (index == body_capacity) {
      refuse(BodyCondition::body_capacity, index, selected.first,
             selected.last);
      return result;
    }
    if (!assemble_body(scratch, selected, result.parts, arm_component)) {
      refuse(BodyCondition::unsupported_arithmetic, index, selected.first,
             selected.last);
      return result;
    }
    result.leaves.push_back(scratch.leaf);
    result.assembled_leaves = result.leaves.size();
    cursor = selected.last;
  }
  if (result.leaves.empty() || cursor != high) {
    refuse(BodyCondition::incomplete_cover, result.leaves.size(), cursor, high);
    return result;
  }
  result.complete = true;
  result.reservations_complete = true;
  result.mass_model_complete = true;
  result.com_derivatives_complete = true;
  return result;
}
auto assess_hip_preflight(Vec candidate, std::size_t body_leaves,
                          std::size_t timing_depth, std::size_t timing_nodes,
                          std::size_t timing_leaves)
    -> std::expected<BoardingPlantedHipPreflightDiagnostic, std::string> {
  using HipCondition = BoardingPlantedHipPreflightCondition;
  if (!bounded(candidate.x, 8) || !bounded(candidate.y, 8) ||
      !bounded(candidate.z, 8))
    return std::unexpected("Planted hip candidate must be finite within +/-8m");
  auto body =
      assess_body(0, 0, body_leaves, timing_depth, timing_nodes, timing_leaves);
  if (!body) return std::unexpected(body.error());
  BoardingPlantedHipPreflightDiagnostic result;
  result.body = std::move(*body);
  result.candidate_relative_to_root = candidate;
  result.axis_length_metres = thigh_length;
  result.hip_limit_metres = kBoardingSelfHipLengthMetres;
  auto arithmetic_refusal = [&] {
    result.first_refusal = HipCondition::unsupported_arithmetic;
    // No partial strict predicate survives unsupported arithmetic.
    result.arithmetic_supported = false;
    result.pelvis_strict_interior = false;
    result.projection_strict_interior = false;
    result.thigh_strict_interior = false;
    result.hip_region_strict_exclusion = false;
    result.strict_unowned_conflict = false;
    result.radial_distance_squared.reset();
    result.thigh_gap.reset();
  };
  if (!supported_environment()) {
    arithmetic_refusal();
    return result;
  }
  const auto& parent = result.body;
  if (!parent.complete || !parent.reservations_complete ||
      !parent.mass_model_complete || !parent.com_derivatives_complete ||
      parent.leaves.size() != 1 || !parent.timing.complete ||
      !parent.timing.derivative_domains_certified ||
      !parent.timing.root_speed_certified ||
      !parent.timing.root_acceleration_certified ||
      !parent.timing.leg_joint_speed_certified ||
      parent.timing.leaves.size() != 1 || !parent.timing.closure.complete ||
      !parent.timing.closure.plane_identities ||
      !parent.timing.closure.link_identities ||
      !parent.timing.closure.joint_limits_certified) {
    result.first_refusal = HipCondition::body_prerequisite;
    return result;
  }
  const auto* pelvis =
      std::get_if<BoardingPlantedBodyBoxBinding>(&parent.parts[0].reservation);
  const auto* thigh = std::get_if<BoardingPlantedBodyCapsuleBinding>(
      &parent.parts[3].reservation);
  const auto& leg = parent.timing.leaves.front().legs[0].closure;
  if (!pelvis || !thigh || parent.parts[0].id != BoardingBodyPartId::pelvis ||
      parent.parts[3].id != BoardingBodyPartId::port_thigh ||
      pelvis->center != BodyPointId::root ||
      pelvis->half_size_metres != Vec{.24, .12, .18} ||
      pelvis->frame.columns != BoardingBodyFrame{}.columns ||
      thigh->start != BodyPointId::port_hip ||
      thigh->end != BodyPointId::port_knee || thigh->radius_metres != .105 ||
      !leg.plane_identity || !leg.link_identities || !leg.limits_certified ||
      parent.common_translation_y_metres != placement_y ||
      !(thigh->radius_metres <= result.hip_limit_metres &&
        result.hip_limit_metres < thigh_length)) {
    result.first_refusal = HipCondition::body_prerequisite;
    return result;
  }
  result.axis_link_identity = true;
  result.thigh_radius_metres = thigh->radius_metres;
  const auto& leaf = parent.leaves.front();
  const auto hip =
      body_intervals(leaf.points[body_index(BodyPointId::port_hip)].value);
  const auto knee =
      body_intervals(leaf.points[body_index(BodyPointId::port_knee)].value);
  const auto root_point =
      body_intervals(leaf.points[body_index(BodyPointId::root)].value);
  const Point selected{point(candidate.x), point(candidate.y),
                       point(candidate.z)};
  Point axis, unit, w, canonical, placed;
  bool supported{true};
  auto scalar_bounds = [&](Interval v) {
    supported = supported && v.supported;
    return bounds(v);
  };
  auto projection = point(0), norm_squared = point(0);
  bool pelvis_inside{true};
  const std::array halves{pelvis->half_size_metres.x,
                          pelvis->half_size_metres.y,
                          pelvis->half_size_metres.z};
  for (std::size_t axis_index = 0; axis_index < axis.size(); ++axis_index) {
    // At this fixed t=0 point H-P=(-.14,0,0) EXACTLY. Both canonical
    // endpoints belong to the owned body; no report midpoint is normalized.
    axis[axis_index] = subtract(knee[axis_index], hip[axis_index]);
    unit[axis_index] = divide(axis[axis_index], point(thigh_length));
    w[axis_index] =
        subtract(selected[axis_index], point(axis_index == 0 ? -.14 : 0));
    canonical[axis_index] = add(root_point[axis_index], selected[axis_index]);
    placed[axis_index] = axis_index == 1
                             ? add(canonical[axis_index], point(placement_y))
                             : canonical[axis_index];
    const auto gap =
        subtract(point(halves[axis_index]), absolute(selected[axis_index]));
    result.pelvis_gaps[axis_index] = scalar_bounds(gap);
    pelvis_inside = pelvis_inside && gap.supported && gap.low > 0;
    projection = add(projection, multiply(unit[axis_index], w[axis_index]));
    norm_squared = add(norm_squared, square(w[axis_index]));
  }
  result.axis = body_bounds(axis, supported);
  result.unit_axis = body_bounds(unit, supported);
  result.witness_from_hip = body_bounds(w, supported);
  result.candidate_canonical = body_bounds(canonical, supported);
  result.candidate_placed = body_bounds(placed, supported);
  result.projection = scalar_bounds(projection);
  const auto upper_gap = subtract(point(thigh_length), projection);
  const auto region_gap =
      subtract(projection, point(kBoardingSelfHipLengthMetres));
  const auto radius_squared = square(point(thigh->radius_metres));
  result.segment_upper_gap = scalar_bounds(upper_gap);
  result.hip_region_gap = scalar_bounds(region_gap);
  result.radius_squared = scalar_bounds(radius_squared);
  result.witness_norm_squared = scalar_bounds(norm_squared);
  if (!supported) {
    arithmetic_refusal();
    return result;
  }
  const auto projection_inside = projection.low > 0 && upper_gap.low > 0;
  bool thigh_inside{};
  if (projection_inside) {
    auto radial = subtract(norm_squared, square(projection));
    // The exact registered unit-axis identity makes this squared norm >=0;
    // tightening its LOWER bound is not a clearance tolerance or repair.
    if (!radial.supported || radial.high < 0) {
      arithmetic_refusal();
      return result;
    }
    radial.low = std::max(0., radial.low);
    const auto gap = subtract(radius_squared, radial);
    result.radial_distance_squared = scalar_bounds(radial);
    result.thigh_gap = scalar_bounds(gap);
    if (!supported) {
      arithmetic_refusal();
      return result;
    }
    thigh_inside = gap.low > 0;
  }
  result.arithmetic_supported = true;
  result.pelvis_strict_interior = pelvis_inside;
  result.projection_strict_interior = projection_inside;
  result.thigh_strict_interior = thigh_inside;
  result.hip_region_strict_exclusion = region_gap.low > 0;
  result.strict_unowned_conflict = pelvis_inside && projection_inside &&
                                   thigh_inside &&
                                   result.hip_region_strict_exclusion;
  if (!pelvis_inside)
    result.first_refusal = HipCondition::pelvis_interior;
  else if (!projection_inside)
    result.first_refusal = HipCondition::segment_projection;
  else if (!thigh_inside)
    result.first_refusal = HipCondition::thigh_interior;
  else if (!result.hip_region_strict_exclusion)
    result.first_refusal = HipCondition::hip_region_exclusion;
  return result;
}
} // namespace
namespace detail {
auto boarding_planted_leg_numeric(RigidVector3 hip, double ankle_x,
                                  double ankle_z, double plane, double l1,
                                  double l2, bool forward)
    -> BoardingPlantedLegEvidence {
  const auto selected = evaluate_leg({point(hip.x), point(hip.y), point(hip.z)},
                                     ankle_x, ankle_z, plane, l1, l2, forward);
  auto result = selected;
  report_angles(result, selected, 1);
  return result;
}
auto boarding_planted_legs_bounded(double first, double last, std::size_t depth,
                                   std::size_t nodes, std::size_t leaves)
    -> std::expected<BoardingPlantedLegDiagnostic, std::string> {
  return assess(first, last, depth, nodes, leaves);
}
auto boarding_planted_leg_timing_numeric(
    RigidVector3 hip, RigidVector3 parameter_first,
    RigidVector3 parameter_second, double ankle_x, double ankle_z, double plane,
    double l1, double l2, bool forward, bool reverse, std::size_t side)
    -> BoardingPlantedLegTimingEvidence {
  const TimingPoint selected{
      TimingScalar{point(hip.x), point(parameter_first.x),
                   point(parameter_second.x)},
      TimingScalar{point(hip.y), point(parameter_first.y),
                   point(parameter_second.y)},
      TimingScalar{point(hip.z), point(parameter_first.z),
                   point(parameter_second.z)}};
  auto result = evaluate_timing_leg(selected, ankle_x, ankle_z, plane, l1, l2,
                                    forward, reverse, side);
  report_angles(result.closure, result.closure, side);
  return result;
}
auto boarding_planted_legs_timing_bounded(double first, double last,
                                          std::size_t depth, std::size_t nodes,
                                          std::size_t leaves)
    -> std::expected<BoardingPlantedLegTimingDiagnostic, std::string> {
  return assess_timing(first, last, depth, nodes, leaves);
}
auto boarding_planted_leg_joint_speed(Bounds roll_rate, Bounds pitch_rate)
    -> BoardingPlantedLegSpeedEvidence {
  return joint_speed(interval(roll_rate.lower, roll_rate.upper),
                     interval(pitch_rate.lower, pitch_rate.upper));
}
auto boarding_planted_leg_speed_threshold() -> Bounds {
  return supported_environment() ? bounds(speed_threshold()) : Bounds{};
}
auto boarding_planted_body_bounded(double first, double last,
                                   std::size_t body_leaves,
                                   std::size_t timing_depth,
                                   std::size_t timing_nodes,
                                   std::size_t timing_leaves)
    -> std::expected<BoardingPlantedBodyDiagnostic, std::string> {
  return assess_body(first, last, body_leaves, timing_depth, timing_nodes,
                     timing_leaves);
}
auto boarding_planted_hip_preflight_bounded(Vec candidate,
                                            std::size_t body_leaves,
                                            std::size_t timing_depth,
                                            std::size_t timing_nodes,
                                            std::size_t timing_leaves)
    -> std::expected<BoardingPlantedHipPreflightDiagnostic, std::string> {
  return assess_hip_preflight(candidate, body_leaves, timing_depth,
                              timing_nodes, timing_leaves);
}
} // namespace detail
auto assess_origin_boarding_planted_legs(double first, double last)
    -> std::expected<BoardingPlantedLegDiagnostic, std::string> {
  return assess(first, last, kBoardingPlantedLegMaximumDepth,
                kBoardingPlantedLegMaximumNodes,
                kBoardingPlantedLegMaximumLeaves);
}
auto assess_origin_boarding_planted_legs_timing(double first, double last)
    -> std::expected<BoardingPlantedLegTimingDiagnostic, std::string> {
  return assess_timing(first, last, kBoardingPlantedLegMaximumDepth,
                       kBoardingPlantedLegMaximumNodes,
                       kBoardingPlantedLegMaximumLeaves);
}
auto assess_origin_boarding_planted_body(double first, double last)
    -> std::expected<BoardingPlantedBodyDiagnostic, std::string> {
  return assess_body(first, last, kBoardingPlantedBodyMaximumLeaves,
                     kBoardingPlantedLegMaximumDepth,
                     kBoardingPlantedLegMaximumNodes,
                     kBoardingPlantedLegMaximumLeaves);
}
auto assess_origin_boarding_planted_hip_preflight()
    -> std::expected<BoardingPlantedHipPreflightDiagnostic, std::string> {
  return assess_hip_preflight(
      {-.04, -.117, -.177}, kBoardingPlantedBodyMaximumLeaves,
      kBoardingPlantedLegMaximumDepth, kBoardingPlantedLegMaximumNodes,
      kBoardingPlantedLegMaximumLeaves);
}
namespace {
using EndpointCondition = BoardingSourceEndpointCondition;
constexpr double endpoint_upper_plane{kCabinCorridorFloorMetres},
    endpoint_lower_plane{-160000.0 * 1e-6};
struct EndpointLeg {
  BoardingPlantedLegEvidence evidence;
  Point hip, knee, ankle, boot, axis;
  bool arithmetic_supported{};
};
auto endpoint_scalar(Interval v, bool& supported) -> Bounds {
  supported = supported && v.supported;
  return v.supported ? bounds(v) : Bounds{};
}
auto endpoint_leg(std::size_t side, double common_y, double root_z)
    -> EndpointLeg {
  EndpointLeg result;
  auto& evidence = result.evidence;
  const auto plane = side == 0 ? endpoint_upper_plane : endpoint_lower_plane;
  const auto ankle_z = side == 0 ? -.5 : -.8;
  evidence.plane_metres = plane;
  evidence.ankle_y_terms = {plane, .1, -common_y};
  evidence.boot_center_y_terms = {plane, .05, -common_y};
  const auto refuse = [&](Condition condition) {
    evidence.limiting_condition = condition;
    if (condition == Condition::unsupported_arithmetic) {
      result.arithmetic_supported = false;
      evidence.plane_identity = false;
      evidence.link_identities = false;
      evidence.limits_certified = false;
    }
    return result;
  };
  if (!supported_environment())
    return refuse(Condition::unsupported_arithmetic);
  bool supported{true};
  // This SAME X expression is shared by the hip, ankle and knee. The exact
  // d.X=0 identity is not inferred from subtraction of independent bounds.
  const auto x = add(point(.16), point(side == 0 ? -.14 : .14));
  const auto ankle_y = subtract(add(point(plane), point(.1)), point(common_y));
  const auto boot_y = subtract(add(point(plane), point(.05)), point(common_y));
  result.hip = {x, point(0), point(root_z)};
  result.ankle = {x, ankle_y, point(ankle_z)};
  result.boot = {x, boot_y, point(ankle_z)};
  evidence.hip = body_bounds(result.hip, supported);
  evidence.ankle = body_bounds(result.ankle, supported);
  evidence.boot_center = body_bounds(result.boot, supported);
  if (!supported) return refuse(Condition::unsupported_arithmetic);
  evidence.plane_identity = true;
  result.arithmetic_supported = true;
  const auto dy = ankle_y, dz = subtract(point(ankle_z), point(root_z));
  const auto rho = negate(dy), D = add(square(rho), square(dz));
  evidence.rho = endpoint_scalar(rho, supported);
  evidence.distance_squared = endpoint_scalar(D, supported);
  if (!supported) return refuse(Condition::unsupported_arithmetic);
  if (dy.high >= 0) return refuse(Condition::forward_branch);
  if (rho.low <= 0 || D.low <= 0) return refuse(Condition::singular_plane);
  const auto l1 = point(thigh_length), l2 = point(shin_length);
  const auto maximum_reach = square(add(l1, l2)),
             minimum_reach = square(subtract(l1, l2));
  if (!maximum_reach.supported || !minimum_reach.supported)
    return refuse(Condition::unsupported_arithmetic);
  if (D.high > maximum_reach.low || D.low < minimum_reach.high)
    return refuse(Condition::reach);
  const auto alpha =
      divide(add(subtract(square(l1), square(l2)), D), multiply(point(2), D));
  auto outer = subtract(maximum_reach, D), inner = subtract(D, minimum_reach);
  if (!outer.supported || !inner.supported)
    return refuse(Condition::unsupported_arithmetic);
  // Value-only reach identities prove these exact factors nonnegative.
  outer.low = std::max(0., outer.low);
  inner.low = std::max(0., inner.low);
  const auto gamma =
      root(divide(multiply(outer, inner), multiply(point(4), square(D))));
  evidence.alpha = endpoint_scalar(alpha, supported);
  evidence.gamma = endpoint_scalar(gamma, supported);
  if (!supported) return refuse(Condition::unsupported_arithmetic);
  const auto complement = subtract(point(1), alpha);
  const auto F1 = add(multiply(alpha, rho), multiply(gamma, dz)),
             G1 = subtract(multiply(gamma, rho), multiply(alpha, dz)),
             F2 = subtract(multiply(complement, rho), multiply(gamma, dz)),
             G2 = negate(add(multiply(complement, dz), multiply(gamma, rho)));
  // q=(0,-dz,-rho), q.dot(d)=0, q^2=D. These F/G expressions are exactly
  // alpha*d+gamma*q and its complementary shin, not normalized report vectors.
  result.axis = {point(0), negate(F1), negate(G1)};
  result.knee = {x, result.axis[1], add(point(root_z), result.axis[2])};
  evidence.knee = body_bounds(result.knee, supported);
  evidence.hip_sine = endpoint_scalar(divide(G1, l1), supported);
  evidence.hip_cosine = endpoint_scalar(divide(F1, l1), supported);
  evidence.shin_sine = endpoint_scalar(divide(G2, l2), supported);
  evidence.shin_cosine = endpoint_scalar(divide(F2, l2), supported);
  const auto knee_cosine = divide(subtract(subtract(D, square(l1)), square(l2)),
                                  multiply(point(2), multiply(l1, l2)));
  evidence.knee_cosine = endpoint_scalar(knee_cosine, supported);
  evidence.roll_sine = {0, 0};
  evidence.roll_cosine = {1, 1};
  if (!supported || !F1.supported || !G1.supported || !F2.supported ||
      !G2.supported)
    return refuse(Condition::unsupported_arithmetic);
  evidence.link_identities = true;
  const auto& selected = limits();
  const auto hip_lower = add(multiply(G1, selected.cos20),
                             multiply(F1, selected.sin20)),
             hip_upper = subtract(multiply(F1, selected.sin65),
                                  multiply(G1, selected.cos65));
  if (!hip_lower.supported || !hip_upper.supported)
    return refuse(Condition::unsupported_arithmetic);
  if (F1.low <= 0 || hip_lower.low < 0 || hip_upper.low < 0)
    return refuse(Condition::hip_flex);
  const auto knee_lower = negate(divide(selected.sqrt2, point(2)));
  if (!knee_lower.supported) return refuse(Condition::unsupported_arithmetic);
  if (knee_cosine.low < knee_lower.high) return refuse(Condition::knee_flex);
  const auto ankle_sector =
      subtract(multiply(F2, point(.5)),
               multiply(absolute(G2), divide(selected.sqrt3, point(2))));
  if (!ankle_sector.supported) return refuse(Condition::unsupported_arithmetic);
  if (F2.low <= 0 || ankle_sector.low < 0)
    return refuse(Condition::ankle_pitch);
  evidence.limits_certified = true;
  const auto display = evidence;
  report_angles(evidence, display, side);
  return result;
}
struct EndpointBodyScratch {
  BoardingSourceEndpointBody body;
  std::array<Point, kBoardingPlantedBodyPointCount> points;
  Point weighted;
};
static_assert(sizeof(BoardingSourceEndpointBody) <= 6144);
static_assert(sizeof(EndpointBodyScratch) + sizeof(BodyParts) +
                  2 * sizeof(EndpointLeg) + 4 * sizeof(Point) <=
              8192);
auto endpoint_body(EndpointBodyScratch& scratch,
                   const std::array<EndpointLeg, 2>& legs,
                   const BodyParts& parts, double root_z, bool& supported)
    -> bool {
  auto& points = scratch.points;
  const Point root_point{point(.16), point(0), point(root_z)};
  const auto offset = [&](const Point& base, const Point& delta) {
    Point result;
    for (std::size_t axis = 0; axis < result.size(); ++axis)
      result[axis] = add(base[axis], delta[axis]);
    supported = supported && std::ranges::all_of(result, [](Interval v) {
                  return v.supported;
                });
    return result;
  };
  points[body_index(BodyPointId::root)] = root_point;
  points[body_index(BodyPointId::trunk_center)] =
      offset(root_point, {point(0), point(.3495), point(0)});
  points[body_index(BodyPointId::helmet_center)] =
      offset(root_point, {point(0), point(.70237), point(0)});
  points[body_index(BodyPointId::eye)] =
      offset(root_point, {point(0), point(.65237), point(0)});
  const auto arm_component = negate(multiply(point(.35898), root(point(.5))));
  supported = supported && arm_component.supported;
  for (std::size_t side = 0; side < legs.size(); ++side) {
    const auto base = body_index(side == 0 ? BodyPointId::port_hip
                                           : BodyPointId::starboard_hip);
    points[base] = legs[side].hip;
    points[base + 1] = legs[side].knee;
    points[base + 2] = legs[side].ankle;
    points[base + 3] = legs[side].boot;
    points[base + 4] = offset(root_point, {point(side == 0 ? -.20265 : .20265),
                                           point(.579), point(0)});
    points[base + 5] =
        offset(points[base + 4], {point(0), arm_component, arm_component});
    points[base + 6] =
        offset(points[base + 5], {point(0), point(.386), point(0)});
  }
  for (std::size_t i = 0; i < points.size(); ++i)
    scratch.body.points[i] = body_bounds(points[i], supported);
  if (!supported) return false;
  scratch.weighted = {point(0), point(0), point(0)};
  for (std::size_t i = 0; i < parts.size(); ++i) {
    const auto& mass = parts[i].mass;
    const auto first = body_index(mass.first), second = body_index(mass.second);
    if (parts[i].id != static_cast<BoardingBodyPartId>(i) ||
        first >= points.size() || second >= points.size() ||
        mass.weight != body_weights[i])
      return false;
    Point mass_point;
    for (std::size_t axis = 0; axis < mass_point.size(); ++axis) {
      mass_point[axis] =
          first == second
              ? points[first][axis]
              : multiply(add(points[first][axis], points[second][axis]),
                         point(.5));
      scratch.weighted[axis] =
          add(scratch.weighted[axis],
              multiply(point(mass.weight), mass_point[axis]));
    }
    scratch.body.mass_points[i] = body_bounds(mass_point, supported);
  }
  for (auto& axis : scratch.weighted)
    axis = divide(axis, point(kBoardingBodyMassDenominator));
  scratch.body.center_of_mass = body_bounds(scratch.weighted, supported);
  return supported;
}
auto endpoint_hip(const EndpointLeg& leg, const BodyParts& parts,
                  std::size_t side) -> BoardingSourceEndpointHipRegionEvidence {
  BoardingSourceEndpointHipRegionEvidence result;
  const auto thigh_index = 3 + side * 6;
  result.second_part = side == 0 ? BoardingBodyPartId::port_thigh
                                 : BoardingBodyPartId::starboard_thigh;
  result.hip_limit_metres = kBoardingSelfHipLengthMetres;
  result.axis_length_metres = thigh_length;
  const auto* pelvis =
      std::get_if<BoardingPlantedBodyBoxBinding>(&parts[0].reservation);
  const auto* thigh = std::get_if<BoardingPlantedBodyCapsuleBinding>(
      &parts[thigh_index].reservation);
  result.bindings_valid =
      pelvis && thigh && parts[0].id == BoardingBodyPartId::pelvis &&
      parts[thigh_index].id == result.second_part &&
      pelvis->center == BodyPointId::root &&
      pelvis->half_size_metres == Vec{.24, .12, .18} &&
      pelvis->frame.columns == BoardingBodyFrame{}.columns &&
      thigh->start ==
          (side == 0 ? BodyPointId::port_hip : BodyPointId::starboard_hip) &&
      thigh->end ==
          (side == 0 ? BodyPointId::port_knee : BodyPointId::starboard_knee) &&
      thigh->radius_metres == .105 &&
      thigh->radius_metres <= result.hip_limit_metres &&
      result.hip_limit_metres < thigh_length;
  if (!result.bindings_valid || !leg.evidence.link_identities) return result;
  result.thigh_radius_metres = thigh->radius_metres;
  result.link_identity = true;
  result.structural_x_zero = true;
  bool supported{true};
  result.axis = body_bounds(leg.axis, supported);
  // P and the common placement cancel before interval evaluation. Since v.X=0,
  // H-P=(+/- .14,0,0) contributes exactly zero to the whole-pelvis support.
  const auto extent =
      add(multiply(point(pelvis->half_size_metres.y), absolute(leg.axis[1])),
          multiply(point(pelvis->half_size_metres.z), absolute(leg.axis[2])));
  const auto limit =
      multiply(point(result.hip_limit_metres), point(thigh_length));
  const auto gap = subtract(limit, extent);
  result.scaled_pelvis_extent = endpoint_scalar(extent, supported);
  result.scaled_hip_limit = endpoint_scalar(limit, supported);
  result.scaled_gap = endpoint_scalar(gap, supported);
  result.arithmetic_supported = supported;
  result.certified = supported && gap.low >= 0;
  return result;
}
auto endpoint_refuse(BoardingSourceEndpointDiagnostic& result,
                     EndpointCondition condition,
                     std::optional<std::size_t> side = {},
                     Condition leg_condition = Condition::none) -> void {
  if (!result.first_refusal)
    result.first_refusal =
        BoardingSourceEndpointRefusal{condition, side, leg_condition};
}
} // namespace
auto detail::boarding_source_endpoint_bounded(
    const OriginBoardingBootSupport& provider, double root_y, double root_z,
    std::size_t max_site_partitions, std::size_t max_body_records)
    -> std::expected<BoardingSourceEndpointDiagnostic, std::string> {
  if (!bounded(root_y, 8) || !bounded(root_z, 8) ||
      max_site_partitions > kBoardingBootSourcePartitionCount ||
      max_body_records > kBoardingSourceEndpointMaximumRecords)
    return std::unexpected(
        "Endpoint finite absolute roots <=8 and lowered capacities required");
  auto sites = boarding_foot_sites_bounded(provider, {}, max_site_partitions);
  if (!sites) return std::unexpected(sites.error());
  BoardingSourceEndpointDiagnostic result{std::move(*sites)};
  result.common_translation_y_metres = root_y;
  result.root_z_metres = root_z;
  if (!result.sites.arithmetic_supported || !supported_environment()) {
    endpoint_refuse(result, EndpointCondition::unsupported_arithmetic);
    return result;
  }
  result.arithmetic_supported = true;
  if (!result.sites.coverage_complete || !result.sites.eligible) {
    endpoint_refuse(result, EndpointCondition::sites_prerequisite);
    return result;
  }
  if (max_body_records == 0) {
    endpoint_refuse(result, EndpointCondition::body_capacity);
    return result;
  }
  std::array<EndpointLeg, 2> legs;
  for (std::size_t side = 0; side < legs.size(); ++side) {
    legs[side] = endpoint_leg(side, root_y, root_z);
    ++result.evaluated_legs;
    result.legs[side] = legs[side].evidence;
    result.arithmetic_supported =
        result.arithmetic_supported && legs[side].arithmetic_supported;
    if (!legs[side].evidence.limits_certified) {
      endpoint_refuse(result,
                      legs[side].arithmetic_supported
                          ? EndpointCondition::leg_closure
                          : EndpointCondition::unsupported_arithmetic,
                      side, legs[side].evidence.limiting_condition);
      return result;
    }
  }
  result.plane_identities = true;
  result.link_identities = true;
  result.joint_limits_certified = true;
  result.parts = body_parts();
  EndpointBodyScratch scratch;
  bool supported{true};
  if (!endpoint_body(scratch, legs, result.parts, root_z, supported)) {
    result.arithmetic_supported = result.arithmetic_supported && supported;
    endpoint_refuse(result, supported
                                ? EndpointCondition::body_bindings
                                : EndpointCondition::unsupported_arithmetic);
    return result;
  }
  result.body = scratch.body;
  result.assembled_records = 1;
  result.reservations_complete = true;
  result.mass_model_complete = true;
  result.hip_regions_certified = true;
  for (std::size_t side = 0; side < result.hips.size(); ++side) {
    auto& hip = result.hips[side];
    hip = endpoint_hip(legs[side], result.parts, side);
    ++result.examined_hip_regions;
    result.hip_regions_certified =
        result.hip_regions_certified && hip.certified;
    if (!hip.bindings_valid)
      endpoint_refuse(result, EndpointCondition::hip_region_guard, side);
    else if (!hip.arithmetic_supported) {
      result.arithmetic_supported = false;
      endpoint_refuse(result, EndpointCondition::unsupported_arithmetic, side);
    } else if (!hip.certified)
      endpoint_refuse(result, EndpointCondition::hip_region_support, side);
  }
  result.complete = result.arithmetic_supported && result.hip_regions_certified;
  return result;
}
auto assess_origin_boarding_source_endpoint(
    const OriginBoardingBootSupport& provider)
    -> std::expected<BoardingSourceEndpointDiagnostic, std::string> {
  return detail::boarding_source_endpoint_bounded(
      provider, .847, -.55, kBoardingBootSourcePartitionCount,
      kBoardingSourceEndpointMaximumRecords);
}
} // namespace apsis_drift

namespace apsis_drift {
namespace {
using SelfShape = BoardingSourceEndpointSelfShape;
using SelfPair = BoardingSourceEndpointSelfPair;
using SelfRegion = BoardingSourceEndpointSelfRegion;
using SelfCert = BoardingSourceEndpointSelfCertificate;
using SelfCondition = BoardingSourceEndpointSelfCondition;
using SelfDiagnostic = BoardingSourceEndpointSelfDiagnostic;
using PartId = BoardingBodyPartId;
using Junction = BoardingSelfJunction;
struct EndpointSelfSolid {
  SelfShape shape{};
  Point first{}, second{};
  Vec half;
  double radius{};
};
struct EndpointSelfPacket {
  std::array<Point, kBoardingPlantedBodyPointCount> points;
  std::array<EndpointSelfSolid, kBoardingBodyPartCount> solids;
};
using SelfRegions = std::array<SelfRegion, kBoardingSelfConnectedRegionCount>;
struct SelfSquareScratch {
  std::array<double, 3> values{}, next{};
  std::size_t count{};
};
static_assert(sizeof(std::array<SelfPair, kBoardingBodyPairCount>) +
                  sizeof(SelfRegions) <=
              16384);
static_assert(sizeof(EndpointSelfPacket) + sizeof(BodyParts) +
                  sizeof(SelfRegions) + 14 * sizeof(Vec) + 8 * sizeof(Point) +
                  12 * sizeof(Interval) + sizeof(SelfSquareScratch) +
                  12 * sizeof(double) <=
              8192);
auto self_squared_relation(double r, double x) -> Sign {
  if (!std::isfinite(r) || !std::isfinite(x) ||
      (r != 0 && 2 * std::ilogb(r) < -970))
    return Sign::unsupported;
  const auto product = r * r;
  if (!std::isfinite(product)) return Sign::unsupported;
  // This proof has ONLY three raw terms. Bound its inherited error-free
  // TwoSum expansion accordingly instead of invoking the 256-term old kernel.
  const std::array terms{product, std::fma(r, r, -product), -x};
  SelfSquareScratch scratch;
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
auto self_root(Interval v) -> Interval {
  if (!v.supported || v.low < 0) return failed();
  if (zero(v)) return point(0);
  auto low = std::sqrt(v.low), high = std::sqrt(v.high);
  bool low_ok{}, high_ok{};
  for (std::size_t i = 0; i < 4; ++i) {
    const auto sign = self_squared_relation(low, v.low);
    if (sign == Sign::zero || sign == Sign::negative) {
      low_ok = true;
      break;
    }
    if (sign == Sign::unsupported) return failed();
    low = down(low);
  }
  for (std::size_t i = 0; i < 4; ++i) {
    const auto sign = self_squared_relation(high, v.high);
    if (sign == Sign::zero || sign == Sign::positive) {
      high_ok = true;
      break;
    }
    if (sign == Sign::unsupported) return failed();
    high = up(high);
  }
  return low_ok && high_ok ? interval(low, high) : failed();
}
auto self_offset(const Point& a, const Point& b) -> Point {
  Point result;
  for (std::size_t i = 0; i < result.size(); ++i)
    result[i] = add(a[i], b[i]);
  return result;
}
auto self_point_supported(const Point& p) -> bool {
  return std::ranges::all_of(p, [](Interval v) { return v.supported; });
}
auto self_dot(const Point& p, Vec n) -> Interval {
  return add(add(multiply(p[0], point(n.x)), multiply(p[1], point(n.y))),
             multiply(p[2], point(n.z)));
}
auto self_norm(Vec n) -> Interval {
  return self_root(
      add(add(square(point(n.x)), square(point(n.y))), square(point(n.z))));
}
auto self_maximum(Interval a, Interval b) -> Interval {
  if (!a.supported || !b.supported) return failed();
  return interval(std::max(a.low, b.low), std::max(a.high, b.high));
}
auto self_support(const EndpointSelfSolid& solid, Vec n) -> Interval {
  if (!self_point_supported(solid.first)) return failed();
  const auto center = self_dot(solid.first, n);
  if (solid.shape == SelfShape::capsule)
    return add(self_maximum(center, self_dot(solid.second, n)),
               multiply(point(solid.radius), self_norm(n)));
  const std::array half{solid.half.x, solid.half.y, solid.half.z};
  const std::array normal{n.x, n.y, n.z};
  auto extent = point(0);
  for (std::size_t i = 0; i < half.size(); ++i) {
    const auto term = multiply(point(half[i]), absolute(point(normal[i])));
    extent = add(extent, solid.shape == SelfShape::box ? term : square(term));
  }
  return add(center,
             solid.shape == SelfShape::box ? extent : self_root(extent));
}
auto self_direction(Vec n) -> bool {
  return n != Vec{} && bounded(n.x, 64) && bounded(n.y, 64) && bounded(n.z, 64);
}
auto self_binding_equal(const BoardingPlantedBodyPartBinding& actual,
                        const BoardingPlantedBodyPartBinding& expected)
    -> bool {
  if (actual.id != expected.id || actual.mass.first != expected.mass.first ||
      actual.mass.second != expected.mass.second ||
      actual.mass.weight != expected.mass.weight ||
      actual.reservation.index() != expected.reservation.index())
    return false;
  if (const auto* box =
          std::get_if<BoardingPlantedBodyBoxBinding>(&actual.reservation)) {
    const auto& other =
        std::get<BoardingPlantedBodyBoxBinding>(expected.reservation);
    return box->center == other.center &&
           box->half_size_metres == other.half_size_metres &&
           box->frame.columns == other.frame.columns;
  }
  const auto& capsule =
      std::get<BoardingPlantedBodyCapsuleBinding>(actual.reservation);
  const auto& other =
      std::get<BoardingPlantedBodyCapsuleBinding>(expected.reservation);
  return capsule.start == other.start && capsule.end == other.end &&
         capsule.radius_metres == other.radius_metres;
}
auto self_packet(const BoardingSourceEndpointDiagnostic& child,
                 EndpointSelfPacket& packet) -> bool {
  if (!child.complete || !child.body || !child.arithmetic_supported ||
      !child.plane_identities || !child.link_identities ||
      !child.joint_limits_certified || !child.reservations_complete ||
      !child.mass_model_complete || !child.hip_regions_certified ||
      child.common_translation_y_metres != .847 ||
      child.root_z_metres != -.55 ||
      child.port_x_terms != std::array{.16, -.14} ||
      child.starboard_x_terms != std::array{.16, .14})
    return false;
  const auto expected = body_parts();
  for (std::size_t i = 0; i < expected.size(); ++i)
    if (!self_binding_equal(child.parts[i], expected[i])) return false;
  // Authenticating the compiled child allows cancellation of P and the ONE
  // common Y before support. The moving leg axis comes from the owned link,
  // never a midpoint reconstruction or another evaluation of the knee graph.
  auto& p = packet.points;
  p[body_index(BodyPointId::root)] = {point(0), point(0), point(0)};
  p[body_index(BodyPointId::trunk_center)] = {point(0), point(.3495), point(0)};
  p[body_index(BodyPointId::helmet_center)] = {point(0), point(.70237),
                                               point(0)};
  p[body_index(BodyPointId::eye)] = {point(0), point(.65237), point(0)};
  const auto arm = negate(multiply(point(.35898), self_root(point(.5))));
  if (!arm.supported) return false;
  for (std::size_t side = 0; side < 2; ++side) {
    if (!child.hips[side].bindings_valid || !child.hips[side].link_identity ||
        !child.hips[side].structural_x_zero ||
        !child.hips[side].arithmetic_supported ||
        !child.legs[side].link_identities)
      return false;
    const auto base = body_index(side == 0 ? BodyPointId::port_hip
                                           : BodyPointId::starboard_hip);
    p[base] = {point(side == 0 ? -.14 : .14), point(0), point(0)};
    p[base + 1] = self_offset(p[base], body_intervals(child.hips[side].axis));
    const auto& terms = child.legs[side].ankle_y_terms;
    p[base + 2] = {
        p[base][0], add(add(point(terms[0]), point(terms[1])), point(terms[2])),
        subtract(point(side == 0 ? -.5 : -.8), point(child.root_z_metres))};
    p[base + 3] = self_offset(p[base + 2], {point(0), point(-.05), point(0)});
    p[base + 4] = {point(side == 0 ? -.20265 : .20265), point(.579), point(0)};
    p[base + 5] = self_offset(p[base + 4], {point(0), arm, arm});
    p[base + 6] = self_offset(p[base + 5], {point(0), point(.386), point(0)});
  }
  if (!std::ranges::all_of(p, self_point_supported)) return false;
  for (std::size_t i = 0; i < packet.solids.size(); ++i) {
    auto& solid = packet.solids[i];
    if (const auto* box = std::get_if<BoardingPlantedBodyBoxBinding>(
            &child.parts[i].reservation)) {
      solid.shape = i == 1 || i == 2 ? SelfShape::ellipsoid : SelfShape::box;
      solid.first = p[body_index(box->center)];
      solid.second = solid.first;
      solid.half = box->half_size_metres;
    } else {
      const auto& capsule = std::get<BoardingPlantedBodyCapsuleBinding>(
          child.parts[i].reservation);
      solid.shape = SelfShape::capsule;
      solid.first = p[body_index(capsule.start)];
      solid.second = p[body_index(capsule.end)];
      solid.radius = capsule.radius_metres;
    }
  }
  return true;
}
auto self_regions() -> SelfRegions {
  using P = BodyPointId;
  SelfRegions result{};
  result[0] = {PartId::pelvis, PartId::trunk,   Junction::waist,
               P::root,        P::trunk_center, kBoardingSelfWaistLimitMetres};
  result[1] = {PartId::trunk,   PartId::helmet,   Junction::neck,
               P::trunk_center, P::helmet_center, .2695};
  for (std::size_t side = 0; side < 2; ++side) {
    const auto base = 3 + side * 6, index = 2 + side * 6;
    const auto hip =
        static_cast<P>(body_index(side == 0 ? P::port_hip : P::starboard_hip));
    const auto point_id = [hip](std::size_t offset) {
      return static_cast<P>(body_index(hip) + offset);
    };
    const auto id = [base](std::size_t offset) {
      return static_cast<PartId>(base + offset);
    };
    result[index] = {PartId::pelvis, id(0),
                     Junction::hip,  hip,
                     point_id(1),    kBoardingSelfHipLengthMetres};
    result[index + 1] = {id(0),       id(1), Junction::knee,
                         point_id(1), hip,   kBoardingSelfKneeRadiusMetres};
    result[index + 2] = {id(1),           id(2),
                         Junction::ankle, point_id(2),
                         point_id(1),     kBoardingSelfAnkleLengthMetres};
    result[index + 3] = {PartId::trunk,      id(3),
                         Junction::shoulder, point_id(4),
                         point_id(5),        kBoardingSelfShoulderRadiusMetres};
    result[index + 4] = {id(3),           id(4),
                         Junction::elbow, point_id(5),
                         point_id(4),     kBoardingSelfElbowRadiusMetres};
    result[index + 5] = {id(4),           id(5),
                         Junction::wrist, point_id(6),
                         point_id(5),     kBoardingSelfWristLengthMetres};
  }
  std::ranges::sort(result, [](const auto& a, const auto& b) {
    return a.first == b.first ? a.second < b.second : a.first < b.first;
  });
  return result;
}
auto self_half_ray(double first, double second, double radius, Interval cosine)
    -> BoardingSourceEndpointSelfHalfRayEvidence {
  BoardingSourceEndpointSelfHalfRayEvidence result;
  const bool opposite =
      cosine.supported && cosine.low == -1 && cosine.high == -1;
  const auto sine_squared =
      opposite ? point(1) : multiply(point(.5), subtract(point(1), cosine));
  const auto maximum = std::max(first, second);
  const auto extent = square(point(maximum));
  const auto limit = multiply(square(point(radius)), sine_squared);
  // For exactly opposite rays and the SAME radius expression, the original
  // endpoint-ball bound is equality; avoid subtracting independent enclosures
  // of that identical square. This is an identity, never an epsilon repair.
  const auto gap =
      opposite && radius == maximum ? point(0) : subtract(limit, extent);
  result.arithmetic_supported = sine_squared.supported && gap.supported;
  if (!result.arithmetic_supported) return result;
  result.sine_squared = bounds(sine_squared);
  result.squared_gap = bounds(gap);
  result.certified = sine_squared.low > 0 && gap.low >= 0;
  return result;
}
auto self_owned(const BoardingSourceEndpointDiagnostic& child,
                const SelfRegion& region, SelfPair& pair) -> void {
  Interval extent, limit, secondary = point(0);
  auto certificate = SelfCert::none;
  const auto second = static_cast<std::size_t>(region.second);
  const auto side = second >= 9 ? std::size_t{1} : std::size_t{0};
  switch (region.junction) {
    case Junction::waist:
      extent = point(.12);
      limit = point(region.limit_metres);
      certificate = SelfCert::cap_partner_support;
      break;
    case Junction::neck:
      // SAME actual trunk semi-axis defines support and original cap limit.
      extent = limit = point(.2695);
      pair.structural_identity = true;
      certificate = SelfCert::cap_partner_support;
      break;
    case Junction::hip:
      extent = interval(child.hips[side].scaled_pelvis_extent.lower,
                        child.hips[side].scaled_pelvis_extent.upper);
      limit = interval(child.hips[side].scaled_hip_limit.lower,
                       child.hips[side].scaled_hip_limit.upper);
      pair.structural_identity = child.hips[side].structural_x_zero;
      certificate = SelfCert::original_axis_box_support;
      break;
    case Junction::ankle: {
      const auto& leg = child.legs[side];
      const auto sine = interval(leg.shin_sine.lower, leg.shin_sine.upper);
      const auto cosine =
          interval(leg.shin_cosine.lower, leg.shin_cosine.upper);
      if (!cosine.supported || cosine.low <= 0 || .075 > region.limit_metres ||
          region.limit_metres >= shin_length) {
        pair.arithmetic_supported = false;
        return;
      }
      // A-to-K is (0,+F2,+G2)/L2. Boot-A=(0,-.05,0): the two
      // exact .05*cosine terms cancel after authenticated positive cosine.
      extent =
          multiply(point(.14), multiply(point(shin_length), absolute(sine)));
      limit = multiply(point(region.limit_metres), point(shin_length));
      pair.structural_identity = true;
      certificate = SelfCert::original_axis_box_support;
      break;
    }
    case Junction::wrist:
      if (.055 > region.limit_metres || region.limit_metres >= .386) {
        pair.arithmetic_supported = false;
        return;
      }
      // Hand center is SAME W; W-to-E is exact -Y with length .386.
      extent = point(.05);
      limit = point(region.limit_metres);
      pair.structural_identity = true;
      certificate = SelfCert::original_axis_box_support;
      break;
    case Junction::knee:
    case Junction::elbow: {
      const auto cosine =
          region.junction == Junction::knee
              ? negate(interval(child.legs[side].knee_cosine.lower,
                                child.legs[side].knee_cosine.upper))
              : self_root(point(.5));
      const auto first_radius = region.junction == Junction::knee ? .105 : .065;
      const auto second_radius =
          region.junction == Junction::knee ? .075 : .055;
      const auto proof = self_half_ray(first_radius, second_radius,
                                       region.limit_metres, cosine);
      pair.arithmetic_supported =
          pair.arithmetic_supported && proof.arithmetic_supported;
      if (!proof.arithmetic_supported) return;
      extent = square(point(std::max(first_radius, second_radius)));
      limit = multiply(
          square(point(region.limit_metres)),
          interval(proof.sine_squared.lower, proof.sine_squared.upper));
      pair.structural_identity = true;
      certificate = SelfCert::half_ray_angle_bound;
      break;
    }
    case Junction::shoulder: {
      if (.065 > region.limit_metres) {
        pair.arithmetic_supported = false;
        return;
      }
      const auto c = self_root(point(.5));
      const auto threshold = self_root(
          subtract(square(point(region.limit_metres)), square(point(.065))));
      // Root ball is within R. Outside it the cylinder starts at threshold;
      // exact u.Z=-c and perpendicular projection length c bound its maximum Z.
      extent = add(negate(multiply(threshold, c)), multiply(point(.065), c));
      secondary = add(negate(multiply(point(.35898), c)), point(.065));
      limit = point(-.18);
      pair.structural_identity = true;
      certificate = SelfCert::shoulder_split;
      break;
    }
  }
  const auto gap = subtract(limit, extent);
  pair.arithmetic_supported = pair.arithmetic_supported && extent.supported &&
                              limit.supported && gap.supported &&
                              secondary.supported;
  if (!pair.arithmetic_supported) return;
  pair.ownership_extent = bounds(extent);
  pair.ownership_limit = bounds(limit);
  pair.ownership_secondary_extent = bounds(secondary);
  pair.certificate_gap = bounds(gap);
  const bool certified =
      region.junction == Junction::shoulder
          ? extent.high < limit.low && secondary.high < limit.low
          : gap.low >= 0;
  if (certified) {
    pair.certificate = certificate;
    pair.certified = pair.whole_owned = true;
  }
}
auto self_center(const EndpointSelfSolid& solid) -> Point {
  if (solid.shape != SelfShape::capsule) return solid.first;
  Point result;
  for (std::size_t i = 0; i < result.size(); ++i)
    result[i] = multiply(add(solid.first[i], solid.second[i]), point(.5));
  return result;
}
auto self_reporting(const Point& p) -> Vec {
  const auto midpoint = [](Interval v) {
    return v.low + (v.high - v.low) * .5;
  };
  return {midpoint(p[0]), midpoint(p[1]), midpoint(p[2])};
}
auto self_difference(Vec a, Vec b) -> Vec {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto self_negate(Vec a) -> Vec {
  return {-a.x, -a.y, -a.z};
}
auto self_separate(const EndpointSelfSolid& a, const EndpointSelfSolid& b,
                   SelfPair& pair, std::size_t max_axes) -> bool {
  std::array<Vec, 14> axes{};
  std::size_t count{};
  const auto push = [&](Vec n) {
    if (count < axes.size()) axes[count++] = n;
  };
  push({1, 0, 0});
  push({0, 1, 0});
  push({0, 0, 1});
  const auto ac = self_reporting(self_center(a)),
             bc = self_reporting(self_center(b));
  push(self_difference(bc, ac));
  for (const auto* solid : {&a, &b})
    if (solid->shape != SelfShape::capsule) {
      push({1, 0, 0});
      push({0, 1, 0});
      push({0, 0, 1});
    }
  for (const auto* solid : {&a, &b})
    if (solid->shape == SelfShape::capsule) {
      const auto other = solid == &a ? bc : ac;
      push(self_difference(self_reporting(solid->first), other));
      push(self_difference(self_reporting(solid->second), other));
    }
  for (std::size_t i = 0; i < std::min(count, max_axes); ++i) {
    ++pair.examined_axes;
    if (!self_direction(axes[i])) continue;
    for (const auto n : {axes[i], self_negate(axes[i])}) {
      ++pair.signed_support_trials;
      const auto first = self_support(a, n),
                 second = self_support(b, self_negate(n));
      const auto total = add(first, second);
      if (!total.supported) {
        pair.arithmetic_supported = false;
        continue;
      }
      if (total.high <= 0) {
        pair.certificate_gap = bounds(negate(total));
        pair.separating_direction = n;
        pair.certificate = SelfCert::convex_support_plane;
        pair.certified = true;
        return true;
      }
    }
  }
  return false;
}
auto self_refuse(SelfDiagnostic& result, SelfCondition condition,
                 std::optional<std::size_t> pair = {}) -> void {
  if (!result.first_refusal)
    result.first_refusal = BoardingSourceEndpointSelfRefusal{condition, pair};
}
} // namespace

auto detail::boarding_source_endpoint_self_bounded(
    const OriginBoardingBootSupport& provider, std::size_t max_site_partitions,
    std::size_t max_body_records, std::size_t max_pairs, std::size_t max_axes)
    -> std::expected<BoardingSourceEndpointSelfDiagnostic, std::string> {
  if (max_site_partitions > 10 || max_body_records > 1 ||
      max_pairs > kBoardingBodyPairCount || max_axes > 14)
    return std::unexpected(
        "Endpoint self reduced capacities exceed registered bounds");
  auto child = detail::boarding_source_endpoint_bounded(
      provider, .847, -.55, max_site_partitions, max_body_records);
  if (!child) return std::unexpected(child.error());
  SelfDiagnostic result{std::move(*child)};
  if (!result.endpoint.complete) {
    self_refuse(result, SelfCondition::endpoint_prerequisite);
    return result;
  }
  if (!supported_environment()) {
    self_refuse(result, SelfCondition::unsupported_arithmetic);
    return result;
  }
  EndpointSelfPacket packet;
  if (!self_packet(result.endpoint, packet)) {
    self_refuse(result, SelfCondition::invalid_binding);
    return result;
  }
  result.regions = self_regions();
  for (std::size_t i = 0; i < result.parts.size(); ++i)
    result.parts[i] = {static_cast<PartId>(i), packet.solids[i].shape,
                       result.endpoint.parts[i]};
  result.bindings_complete = result.arithmetic_supported = true;
  std::size_t index{};
  for (std::size_t a = 0; a < result.parts.size(); ++a)
    for (std::size_t b = a + 1; b < result.parts.size(); ++b) {
      auto& pair = result.pairs[index];
      pair.first = static_cast<PartId>(a);
      pair.second = static_cast<PartId>(b);
      for (std::size_t r = 0; r < result.regions.size(); ++r)
        if (result.regions[r].first == pair.first &&
            result.regions[r].second == pair.second)
          pair.connected_region_index = r;
      if (index >= max_pairs) {
        self_refuse(result, SelfCondition::pair_capacity, index);
        ++index;
        continue;
      }
      pair.examined = pair.arithmetic_supported = true;
      if (pair.connected_region_index)
        self_owned(result.endpoint,
                   result.regions[*pair.connected_region_index], pair);
      if (!pair.certified)
        self_separate(packet.solids[a], packet.solids[b], pair, max_axes);
      ++result.examined_pairs;
      result.examined_axes += pair.examined_axes;
      result.signed_support_trials += pair.signed_support_trials;
      result.arithmetic_supported =
          result.arithmetic_supported && pair.arithmetic_supported;
      if (pair.certified) ++result.certified_pairs;
      if (!pair.arithmetic_supported)
        self_refuse(result, SelfCondition::unsupported_arithmetic, index);
      else if (!pair.certified) {
        const auto proposed =
            4 + (packet.solids[a].shape == SelfShape::capsule ? 2U : 3U) +
            (packet.solids[b].shape == SelfShape::capsule ? 2U : 3U);
        self_refuse(result,
                    pair.examined_axes < proposed
                        ? SelfCondition::axis_capacity
                        : SelfCondition::unresolved_pair,
                    index);
      }
      ++index;
    }
  result.coverage_complete = result.examined_pairs == kBoardingBodyPairCount;
  result.self_qualified = result.bindings_complete &&
                          result.arithmetic_supported &&
                          result.coverage_complete &&
                          result.certified_pairs == kBoardingBodyPairCount;
  result.complete = result.self_qualified;
  return result;
}
auto assess_origin_boarding_source_endpoint_self(
    const OriginBoardingBootSupport& provider)
    -> std::expected<BoardingSourceEndpointSelfDiagnostic, std::string> {
  return detail::boarding_source_endpoint_self_bounded(
      provider, 10, 1, kBoardingBodyPairCount, 14);
}
auto detail::boarding_source_endpoint_self_support(
    const OriginBoardingBootSupport& provider, BoardingBodyPartId part,
    RigidVector3 direction)
    -> std::expected<BoardingSourceEndpointSelfSupportEvidence, std::string> {
  if (static_cast<std::size_t>(part) >= kBoardingBodyPartCount ||
      !self_direction(direction))
    return std::unexpected(
        "Endpoint self fixed part and nonzero finite direction <=64 required");
  BoardingSourceEndpointSelfSupportEvidence result;
  if (!supported_environment()) return result;
  const auto child = assess_origin_boarding_source_endpoint(provider);
  if (!child) return std::unexpected(child.error());
  EndpointSelfPacket packet;
  if (!self_packet(*child, packet)) return result;
  // The test seam reports canonical support, BEFORE common placement, whereas
  // the full graph cancels P as well before comparing both primitive supports.
  const Point canonical_root{point(.16), point(0), point(-.55)};
  const auto support = add(
      self_support(packet.solids[static_cast<std::size_t>(part)], direction),
      self_dot(canonical_root, direction));
  result.arithmetic_supported = support.supported;
  if (support.supported) result.support = bounds(support);
  return result;
}
auto detail::boarding_source_endpoint_self_half_ray(
    double first_radius, double second_radius, double region_radius,
    BoardingPlantedLegScalarBounds outgoing_cosine)
    -> std::expected<BoardingSourceEndpointSelfHalfRayEvidence, std::string> {
  const auto valid_radius = [](double v) { return bounded(v, 8) && v > 0; };
  const auto cosine = interval(outgoing_cosine.lower, outgoing_cosine.upper);
  if (!valid_radius(first_radius) || !valid_radius(second_radius) ||
      !valid_radius(region_radius) || !cosine.supported || cosine.low < -1 ||
      cosine.high > 1)
    return std::unexpected("Endpoint self half-ray positive radii <=8 and "
                           "cosine bounds in [-1,1] required");
  if (!supported_environment())
    return BoardingSourceEndpointSelfHalfRayEvidence{};
  return self_half_ray(first_radius, second_radius, region_radius, cosine);
}
namespace {
using TransferCell = BoardingLowerFootTransferCell;
using TransferCondition = BoardingLowerFootTransferCondition;
using TransferResult = detail::BoardingLowerFootTransferCellResult;
using TransferLimits = detail::BoardingLowerFootTransferLimits;
using TransferWork = BoardingLowerFootTransferCounters;
using TransferRefusal = BoardingLowerFootTransferRefusal;
struct TransferGraph {
  TimingPoint root;
  std::array<Point, 2> knee_from_hip{}, ankle_from_root{};
  std::array<Interval, 2> knee_cosine{};
  Interval arm;
};
static_assert(sizeof(TransferCell) <=
              kBoardingLowerFootTransferMaximumCellBytes);
static_assert(sizeof(TransferGraph) == 576);
// One cell, retained same-expression graph, mutually exclusive main phase,
// nested helper/return copies, eleven cover frames, all dispatcher/query
// controls and the dedicated immutable scalar limits. Owned initial/output
// storage is accounted separately. Explicit noinline phases preserve this
// split.
constexpr std::size_t transfer_main_phase_bytes{4096},
    transfer_nested_helper_bytes{2048}, transfer_query_control_bytes{512};
static_assert(sizeof(TransferCell) + sizeof(TransferGraph) +
                  transfer_main_phase_bytes + transfer_nested_helper_bytes +
                  11 * std::size_t{24} + transfer_query_control_bytes +
                  sizeof(Limits) <=
              kBoardingLowerFootTransferMaximumScratchBytes);

auto transfer_refuse(TransferRefusal& out, TransferCondition condition,
                     Interval limiting = {}) -> TransferResult {
  out.condition = condition;
  out.limiting_bound = limiting.supported ? bounds(limiting) : Bounds{};
  return condition == TransferCondition::unsupported_arithmetic ||
                 condition == TransferCondition::invalid_binding
             ? TransferResult::unsupported
             : TransferResult::unresolved;
}
auto transfer_small_root(Interval v) -> Interval {
  return v.supported && v.low >= 0 && v.high <= 16384 ? self_root(v) : failed();
}
auto transfer_root_jet(const TimingScalar& v) -> TimingScalar {
  if (!timing_supported(v) || v.value.low <= 0) return {};
  const auto y = transfer_small_root(v.value), twice_y = multiply(point(2), y),
             four_y3 = multiply(point(4), multiply(square(y), y));
  return {
      y, divide(v.first, twice_y),
      subtract(divide(v.second, twice_y), divide(square(v.first), four_y3))};
}
auto transfer_limits() -> const Limits& {
  // Dedicated small-root family; the old limits() accessor remains unchanged.
  static const Limits selected{
      transfer_small_root(point(3)), transfer_small_root(point(2)),
      trig_constant(20, true),       trig_constant(20, false),
      trig_constant(65, true),       trig_constant(65, false)};
  return selected;
}
auto transfer_polynomial(double t) -> Interval {
  if (t == 0 || t == 1) return point(t);
  return multiply(multiply(square(point(t)), point(t)),
                  add(subtract(point(10), multiply(point(15), point(t))),
                      multiply(point(6), square(point(t)))));
}
[[gnu::noinline]] auto transfer_quintic(double first, double last)
    -> TimingScalar {
  const auto lo = transfer_polynomial(first), hi = transfer_polynomial(last);
  const auto t = interval(first, last), one_minus = subtract(point(1), t);
  auto rate = multiply(point(30), multiply(square(t), square(one_minus)));
  auto second =
      multiply(point(60), multiply(multiply(t, one_minus),
                                   subtract(point(1), multiply(point(2), t))));
  if (!lo.supported || !hi.supported || !rate.supported || !second.supported)
    return {};
  rate = interval(std::max(0., rate.low), std::min(15. / 8, rate.high));
  second = interval(std::max(-6., second.low), std::min(6., second.high));
  if (first == last && (first == 0 || first == 1)) rate = second = point(0);
  return {interval(std::max(0., lo.low), std::min(1., hi.high)), rate, second};
}
auto transfer_physical_first(Interval value, bool reverse) -> Interval {
  const auto scaled = divide(value, point(4));
  return reverse ? negate(scaled) : scaled;
}
auto transfer_physical_second(Interval value) -> Interval {
  return divide(value, point(16));
}
auto transfer_point_evidence(const TimingPoint& p, bool reverse,
                             BodyPointEvidence& out) -> bool {
  Point value{}, first{}, second{};
  for (std::size_t i = 0; i < p.size(); ++i) {
    if (!timing_supported(p[i]) || p[i].value.low < -8 || p[i].value.high > 8 ||
        p[i].first.low < -4096 || p[i].first.high > 4096 ||
        p[i].second.low < -4096 || p[i].second.high > 4096)
      return false;
    value[i] = p[i].value;
    first[i] = transfer_physical_first(p[i].first, reverse);
    second[i] = transfer_physical_second(p[i].second);
  }
  if (!self_point_supported(first) || !self_point_supported(second))
    return false;
  out = {point_bounds(value), {point_bounds(first), point_bounds(second)}};
  return true;
}
auto transfer_angular(const AngularTiming& value, bool reverse)
    -> BoardingPlantedLegAngularDerivatives {
  return {bounds(transfer_physical_first(value.first, reverse)),
          bounds(transfer_physical_second(value.second))};
}
auto transfer_norm(const Point& v) -> Interval {
  auto total = add(add(square(v[0]), square(v[1])), square(v[2]));
  if (!total.supported) return failed();
  total.low = std::max(0., total.low);
  return transfer_small_root(total);
}
struct TransferLegPacket {
  TimingPoint d;
  TimingScalar rho2, F1, G1, F2, G2;
  Interval D, gamma2;
};
static_assert(sizeof(TransferLegPacket) == 624);
// Geometry: hip/ankle/boot/d/q/knee, nine scalar jets, eight scalar values,
// loop/reference controls, outward conversion copies and retained leg packet.
// All three phases release these locals before the next phase is entered.
constexpr std::size_t transfer_geometry_named_bytes =
    6 * sizeof(TimingPoint) + 9 * sizeof(TimingScalar) + 8 * sizeof(Interval) +
    32 * sizeof(std::size_t) + 2 * sizeof(BodyPointEvidence) +
    2 * sizeof(TimingPoint) + sizeof(TransferLegPacket) + sizeof(TimingScalar);
static_assert(transfer_geometry_named_bytes <= transfer_main_phase_bytes);
// Self: relative points and two genuine per-pair primitives; ownership and
// separator proposals are alternate paths. No complete static Self output.
constexpr std::size_t transfer_self_named_bytes =
    kBoardingPlantedBodyPointCount * sizeof(Point) +
    2 * sizeof(EndpointSelfSolid) + 14 * sizeof(Vec) + 12 * sizeof(Interval) +
    sizeof(BoardingSourceEndpointSelfHalfRayEvidence) +
    32 * sizeof(std::size_t) + 2 * sizeof(Point);
static_assert(transfer_self_named_bytes <= transfer_main_phase_bytes);
// Deepest root-jet/support chains: by-value jet/point arguments and returns,
// primitive copies/arrays, two nested root endpoints and the small3+3 residual.
constexpr std::size_t transfer_helper_named_bytes =
    8 * sizeof(TimingScalar) + 8 * sizeof(Point) + 12 * sizeof(Interval) +
    2 * sizeof(SelfSquareScratch) + 16 * sizeof(double) +
    16 * sizeof(std::size_t);
static_assert(transfer_helper_named_bytes <= transfer_nested_helper_bytes);

[[gnu::noinline]] auto transfer_leg_geometry(
    std::size_t side, const TimingScalar& S, bool reverse, TransferGraph& graph,
    TransferCell& out, TransferLegPacket& packet, TransferRefusal& refusal)
    -> TransferResult {
  refusal.side = side;
  const auto base = body_index(side == 0 ? BodyPointId::port_hip
                                         : BodyPointId::starboard_hip);
  const auto plane = side == 0 ? endpoint_upper_plane : endpoint_lower_plane;
  TimingPoint hip = graph.root;
  hip[0] = timing_add(hip[0], timing_constant(side == 0 ? -.14 : .14));
  const TimingPoint ankle{
      timing_constant(add(point(.16), point(side == 0 ? -.14 : .14))),
      timing_constant(subtract(add(point(plane), point(.1)), point(.847))),
      timing_constant(side == 0 ? -.5 : -.8)};
  TimingPoint boot = ankle;
  boot[1] =
      timing_constant(subtract(add(point(plane), point(.05)), point(.847)));
  const TimingPoint d{timing_multiply(timing_constant(-.07), S),
                      timing_subtract(ankle[1], hip[1]),
                      timing_subtract(ankle[2], hip[2])};
  const auto rho2 = timing_add(timing_square(d[0]), timing_square(d[1])),
             D = timing_add(rho2, timing_square(d[2]));
  if (!timing_supported(rho2) || !timing_supported(D))
    return transfer_refuse(refusal, TransferCondition::unsupported_arithmetic);
  if (d[1].value.high >= 0)
    return transfer_refuse(refusal, TransferCondition::forward_branch,
                           d[1].value);
  if (rho2.value.low <= 0 || D.value.low <= 0)
    return transfer_refuse(refusal, TransferCondition::derivative_domain,
                           rho2.value);
  const auto l1 = point(thigh_length), l2 = point(shin_length),
             l1sq = square(l1), l2sq = square(l2),
             maximum = square(add(l1, l2)), minimum = square(subtract(l1, l2));
  if (!maximum.supported || !minimum.supported)
    return transfer_refuse(refusal, TransferCondition::unsupported_arithmetic);
  if (D.value.high > maximum.low || D.value.low < minimum.high)
    return transfer_refuse(refusal, TransferCondition::reach, D.value);
  const auto rho = transfer_root_jet(rho2);
  const auto alpha =
      timing_divide(timing_add(timing_constant(subtract(l1sq, l2sq)), D),
                    timing_multiply(timing_constant(2), D));
  auto outer = timing_subtract(timing_constant(maximum), D),
       inner = timing_subtract(D, timing_constant(minimum));
  if (!timing_supported(outer) || !timing_supported(inner))
    return transfer_refuse(refusal, TransferCondition::unsupported_arithmetic);
  outer.value.low = std::max(0., outer.value.low);
  inner.value.low = std::max(0., inner.value.low);
  const auto gamma2 =
      timing_divide(timing_multiply(outer, inner),
                    timing_multiply(timing_constant(4), timing_square(D)));
  if (!timing_supported(gamma2))
    return transfer_refuse(refusal, TransferCondition::unsupported_arithmetic);
  if (gamma2.value.low <= 0)
    return transfer_refuse(refusal, TransferCondition::derivative_domain,
                           gamma2.value);
  const auto gamma = transfer_root_jet(gamma2);
  if (!timing_supported(rho) || !timing_supported(alpha) ||
      !timing_supported(gamma))
    return transfer_refuse(refusal, TransferCondition::unsupported_arithmetic);
  const TimingPoint q{timing_divide(timing_multiply(d[0], d[2]), rho),
                      timing_divide(timing_multiply(d[1], d[2]), rho),
                      timing_negate(rho)};
  TimingPoint knee;
  for (std::size_t i = 0; i < knee.size(); ++i) {
    const auto link =
        timing_add(timing_multiply(alpha, d[i]), timing_multiply(gamma, q[i]));
    graph.knee_from_hip[side][i] = link.value;
    graph.ankle_from_root[side][i] =
        i == 0 ? add(d[i].value, point(side == 0 ? -.14 : .14)) : d[i].value;
    knee[i] = timing_add(hip[i], link);
  }
  if (!transfer_point_evidence(hip, reverse, out.points[base]) ||
      !transfer_point_evidence(knee, reverse, out.points[base + 1]) ||
      !transfer_point_evidence(ankle, reverse, out.points[base + 2]) ||
      !transfer_point_evidence(boot, reverse, out.points[base + 3]))
    return transfer_refuse(refusal, TransferCondition::unsupported_arithmetic);
  const auto complement = timing_subtract(timing_constant(1), alpha);
  packet.F1 =
      timing_add(timing_multiply(alpha, rho), timing_multiply(gamma, d[2]));
  packet.G1 = timing_subtract(timing_multiply(gamma, rho),
                              timing_multiply(alpha, d[2]));
  packet.F2 = timing_subtract(timing_multiply(complement, rho),
                              timing_multiply(gamma, d[2]));
  packet.G2 = timing_negate(timing_add(timing_multiply(complement, d[2]),
                                       timing_multiply(gamma, rho)));
  graph.knee_cosine[side] = divide(subtract(subtract(D.value, l1sq), l2sq),
                                   multiply(point(2), multiply(l1, l2)));
  packet.d = d;
  packet.rho2 = rho2;
  packet.D = D.value;
  packet.gamma2 = gamma2.value;
  return TransferResult::accepted;
}
[[gnu::noinline]] auto transfer_leg_predicates(std::size_t side, bool reverse,
                                               const TransferGraph& graph,
                                               const TransferLegPacket& packet,
                                               TransferCell& out,
                                               TransferRefusal& refusal)
    -> TransferResult {
  auto& leg = out.legs[side];
  const auto& d = packet.d;
  const auto& rho2 = packet.rho2;
  const auto& F1 = packet.F1;
  const auto& G1 = packet.G1;
  const auto& F2 = packet.F2;
  const auto& G2 = packet.G2;
  const auto l1sq = square(point(thigh_length)),
             l2sq = square(point(shin_length));
  const auto& selected = transfer_limits();
  const auto roll = subtract(multiply(negate(d[1].value),
                                      subtract(point(2), selected.sqrt3)),
                             absolute(d[0].value)),
             hip_lower = add(multiply(G1.value, selected.cos20),
                             multiply(F1.value, selected.sin20)),
             hip_upper = subtract(multiply(F1.value, selected.sin65),
                                  multiply(G1.value, selected.cos65)),
             knee_margin =
                 add(graph.knee_cosine[side], divide(selected.sqrt2, point(2))),
             ankle_margin =
                 subtract(multiply(F2.value, point(.5)),
                          multiply(absolute(G2.value),
                                   divide(selected.sqrt3, point(2))));
  const std::array margins{roll,         hip_lower,  hip_upper, knee_margin,
                           ankle_margin, rho2.value, packet.D,  packet.gamma2};
  if (!std::ranges::all_of(margins, [](Interval v) { return v.supported; }) ||
      !timing_supported(F1) || !timing_supported(G1) || !timing_supported(F2) ||
      !timing_supported(G2))
    return transfer_refuse(refusal, TransferCondition::unsupported_arithmetic);
  for (std::size_t i = 0; i < margins.size(); ++i)
    leg.margins[i] = bounds(margins[i]);
  if (F1.value.low <= 0 || F2.value.low <= 0 ||
      std::ranges::any_of(margins, [](Interval v) { return v.low < 0; }))
    return transfer_refuse(refusal, TransferCondition::joint_sector);
  const auto hip_pitch = pitch_derivatives(F1, G1, l1sq),
             shin_pitch = pitch_derivatives(F2, G2, l2sq);
  const AngularTiming knee_flex{subtract(hip_pitch.first, shin_pitch.first),
                                subtract(hip_pitch.second, shin_pitch.second)};
  const auto F = timing_negate(d[1]), G = d[0];
  const auto numerator =
      subtract(multiply(F.value, G.first), multiply(G.value, F.first));
  const AngularTiming phi{
      divide(numerator, rho2.value),
      subtract(divide(subtract(multiply(F.value, G.second),
                               multiply(G.value, F.second)),
                      rho2.value),
               divide(multiply(numerator, rho2.first), square(rho2.value)))};
  const std::array angular{hip_pitch, shin_pitch, knee_flex, phi};
  if (!std::ranges::all_of(angular, [](const AngularTiming& a) {
        return a.first.supported && a.second.supported;
      }))
    return transfer_refuse(refusal, TransferCondition::unsupported_arithmetic);
  leg.hip_pitch = transfer_angular(hip_pitch, reverse);
  leg.shin_pitch = transfer_angular(shin_pitch, reverse);
  leg.knee_flex = transfer_angular(knee_flex, reverse);
  leg.ankle_pitch = transfer_angular(negate_angular(shin_pitch), reverse);
  leg.hip_abduction =
      transfer_angular(side == 0 ? negate_angular(phi) : phi, reverse);
  leg.ankle_roll = transfer_angular(negate_angular(phi), reverse);
  const auto phi_rate = transfer_physical_first(phi.first, reverse);
  const std::array pitches{hip_pitch.first, knee_flex.first, shin_pitch.first};
  const auto threshold = square(speed_threshold());
  for (std::size_t i = 0; i < pitches.size(); ++i) {
    const auto pitch = transfer_physical_first(pitches[i], reverse);
    auto squared = add(square(i == 1 ? point(0) : phi_rate), square(pitch));
    if (!squared.supported || !threshold.supported)
      return transfer_refuse(refusal,
                             TransferCondition::unsupported_arithmetic);
    squared.low = std::max(0., squared.low);
    const auto speed = transfer_small_root(squared);
    if (!speed.supported)
      return transfer_refuse(refusal,
                             TransferCondition::unsupported_arithmetic);
    leg.joint_speeds[i] = bounds(speed);
    if (squared.high > threshold.low)
      return transfer_refuse(refusal, TransferCondition::joint_speed, speed);
  }
  leg.link_identities = leg.plane_identity = leg.branch_certified =
      leg.joint_sectors_certified = leg.derivative_domains_certified =
          leg.joint_speeds_certified = true;
  return TransferResult::accepted;
}
[[gnu::noinline]] auto transfer_leg(std::size_t side, const TimingScalar& S,
                                    bool reverse, TransferGraph& graph,
                                    TransferCell& out, TransferRefusal& refusal)
    -> TransferResult {
  TransferLegPacket packet;
  const auto result =
      transfer_leg_geometry(side, S, reverse, graph, out, packet, refusal);
  return result == TransferResult::accepted
             ? transfer_leg_predicates(side, reverse, graph, packet, out,
                                       refusal)
             : result;
}
// Keep the three scratch-heavy phases in distinct call frames. No phase owns
// a second cell or overlaps another phase's jet/support workspace.
[[gnu::noinline]] auto transfer_initialize(const TimingScalar& S, bool reverse,
                                           TransferGraph& graph,
                                           TransferCell& out,
                                           TransferRefusal& refusal)
    -> TransferResult {
  graph.root = {timing_add(timing_constant(.16),
                           timing_multiply(timing_constant(.07), S)),
                timing_multiply(timing_constant(.012), S),
                timing_subtract(timing_constant(-.55),
                                timing_multiply(timing_constant(.04), S))};
  graph.arm = negate(multiply(point(.35898), transfer_small_root(point(.5))));
  auto& root_point = out.points[body_index(BodyPointId::root)];
  if (!graph.arm.supported ||
      !transfer_point_evidence(graph.root, reverse, root_point))
    return transfer_refuse(refusal, TransferCondition::unsupported_arithmetic);
  bool supported{true};
  const auto offset = [&](BodyPointId id, const Point& value) {
    out.points[body_index(id)] = body_offset(root_point, value, supported);
  };
  offset(BodyPointId::trunk_center, {point(0), point(.3495), point(0)});
  offset(BodyPointId::helmet_center, {point(0), point(.70237), point(0)});
  offset(BodyPointId::eye, {point(0), point(.65237), point(0)});
  return supported ? TransferResult::accepted
                   : transfer_refuse(refusal,
                                     TransferCondition::unsupported_arithmetic);
}
[[gnu::noinline]] auto transfer_finish_body(
    const TimingScalar& S, const BodyParts& parts, const TransferGraph& graph,
    TransferCell& out, TransferRefusal& refusal) -> TransferResult {
  bool supported{true};
  const auto& root_point = out.points[body_index(BodyPointId::root)];
  for (std::size_t side = 0; side < 2; ++side) {
    const auto base = body_index(side == 0 ? BodyPointId::port_hip
                                           : BodyPointId::starboard_hip);
    out.points[base + 4] = body_offset(
        root_point,
        {point(side == 0 ? -.20265 : .20265), point(.579), point(0)},
        supported);
    out.points[base + 5] = body_offset(
        out.points[base + 4], {point(0), graph.arm, graph.arm}, supported);
    out.points[base + 6] = body_offset(
        out.points[base + 5], {point(0), point(.386), point(0)}, supported);
  }
  if (!supported)
    return transfer_refuse(refusal, TransferCondition::unsupported_arithmetic);
  // The original table defines every midpoint and all1200 mass units.
  Point weighted{point(0), point(0), point(0)};
  for (std::size_t i = 0; i < parts.size(); ++i) {
    const auto& mass = parts[i].mass;
    const auto& a = out.points[body_index(mass.first)];
    const auto& b = out.points[body_index(mass.second)];
    out.mass_points[i] =
        mass.first == mass.second ? a : body_midpoint(a, b, supported);
    const auto value = body_intervals(out.mass_points[i].value);
    for (std::size_t axis = 0; axis < value.size(); ++axis)
      weighted[axis] =
          add(weighted[axis], multiply(point(mass.weight), value[axis]));
  }
  for (auto& v : weighted)
    v = divide(v, point(kBoardingBodyMassDenominator));
  out.center_of_mass.value = body_bounds(weighted, supported);
  const auto derivative = [&](bool second) {
    const auto& r = root_point.derivatives;
    const auto& p = out.points[body_index(BodyPointId::port_knee)].derivatives;
    const auto& s =
        out.points[body_index(BodyPointId::starboard_knee)].derivatives;
    auto selected = body_intervals(second ? r.acceleration : r.velocity);
    const auto a = body_intervals(second ? p.acceleration : p.velocity);
    const auto b = body_intervals(second ? s.acceleration : s.velocity);
    for (std::size_t i = 0; i < selected.size(); ++i)
      selected[i] = divide(add(multiply(point(960), selected[i]),
                               multiply(point(84), add(a[i], b[i]))),
                           point(1200));
    return body_bounds(selected, supported);
  };
  out.center_of_mass.derivatives = {derivative(false), derivative(true)};
  const auto speed =
                 transfer_norm(body_intervals(root_point.derivatives.velocity)),
             acceleration = transfer_norm(
                 body_intervals(root_point.derivatives.acceleration));
  if (!supported || !speed.supported || !acceleration.supported ||
      !std::ranges::all_of(out.points, body_supported) ||
      !std::ranges::all_of(out.mass_points, body_supported) ||
      !body_supported(out.center_of_mass))
    return transfer_refuse(refusal, TransferCondition::unsupported_arithmetic);
  out.root_speed = bounds(speed);
  out.root_acceleration = bounds(acceleration);
  out.port_reaction_fraction =
      bounds(subtract(point(.5), multiply(point(.25), S.value)));
  if (speed.high > .25)
    return transfer_refuse(refusal, TransferCondition::root_speed, speed);
  if (acceleration.high > .10)
    return transfer_refuse(refusal, TransferCondition::root_acceleration,
                           acceleration);
  out.arithmetic_supported = out.kinematics_complete = out.timing_complete =
      true;
  refusal.side.reset();
  refusal.condition = TransferCondition::none;
  return TransferResult::accepted;
}
[[gnu::noinline]] auto transfer_reset(double first, double last,
                                      TransferCell& out,
                                      TransferRefusal& refusal) -> void {
  out.first = refusal.first = first;
  out.last = refusal.last = last;
  out.arithmetic_supported = out.kinematics_complete = out.timing_complete =
      out.self_complete = out.positive_reactions =
          out.nominal_vertical_equilibrium_complete =
              out.finite_pressure_complete = out.complete = false;
  out.pair_certificates.fill(SelfCert::none);
  out.certificate_counts.fill(0);
  for (auto& owner : out.owners)
    owner = {};
  for (auto& pressure : out.pressures)
    pressure = {};
  for (auto& leg : out.legs)
    leg.link_identities = leg.plane_identity = leg.branch_certified =
        leg.joint_sectors_certified = leg.derivative_domains_certified =
            leg.joint_speeds_certified = false;
  refusal.condition = TransferCondition::none;
  refusal.side.reset();
  refusal.pair.reset();
  refusal.partition.reset();
}
[[gnu::noinline]] auto transfer_body(double first, double last, bool reverse,
                                     const BodyParts& parts,
                                     TransferGraph& graph, TransferCell& out,
                                     TransferRefusal& refusal)
    -> TransferResult {
  transfer_reset(first, last, out, refusal);
  if (!supported_environment() || !std::isfinite(first) ||
      !std::isfinite(last) || first < 0 || first > last || last > 1)
    return transfer_refuse(refusal, TransferCondition::unsupported_arithmetic);
  const auto S = transfer_quintic(first, last);
  if (!timing_supported(S))
    return transfer_refuse(refusal, TransferCondition::unsupported_arithmetic);
  auto result = transfer_initialize(S, reverse, graph, out, refusal);
  if (result != TransferResult::accepted) return result;
  for (std::size_t side = 0; side < 2; ++side) {
    result = transfer_leg(side, S, reverse, graph, out, refusal);
    if (result != TransferResult::accepted) return result;
  }
  return transfer_finish_body(S, parts, graph, out, refusal);
}
auto transfer_relative_points(
    const TransferGraph& graph,
    std::array<Point, kBoardingPlantedBodyPointCount>& p) -> bool {
  p[body_index(BodyPointId::root)] = {point(0), point(0), point(0)};
  p[body_index(BodyPointId::trunk_center)] = {point(0), point(.3495), point(0)};
  p[body_index(BodyPointId::helmet_center)] = {point(0), point(.70237),
                                               point(0)};
  p[body_index(BodyPointId::eye)] = {point(0), point(.65237), point(0)};
  for (std::size_t side = 0; side < 2; ++side) {
    const auto base = body_index(side == 0 ? BodyPointId::port_hip
                                           : BodyPointId::starboard_hip);
    p[base] = {point(side == 0 ? -.14 : .14), point(0), point(0)};
    p[base + 1] = self_offset(p[base], graph.knee_from_hip[side]);
    p[base + 2] = graph.ankle_from_root[side];
    p[base + 3] = self_offset(p[base + 2], {point(0), point(-.05), point(0)});
    p[base + 4] = {point(side == 0 ? -.20265 : .20265), point(.579), point(0)};
    p[base + 5] = self_offset(p[base + 4], {point(0), graph.arm, graph.arm});
    p[base + 6] = self_offset(p[base + 5], {point(0), point(.386), point(0)});
  }
  return std::ranges::all_of(p, self_point_supported);
}
auto transfer_solid(const BoardingPlantedBodyPartBinding& part,
                    const std::array<Point, kBoardingPlantedBodyPointCount>& p)
    -> EndpointSelfSolid {
  EndpointSelfSolid solid;
  if (const auto* box =
          std::get_if<BoardingPlantedBodyBoxBinding>(&part.reservation)) {
    solid.shape = part.id == PartId::trunk || part.id == PartId::helmet
                      ? SelfShape::ellipsoid
                      : SelfShape::box;
    solid.first = p[body_index(box->center)];
    solid.second = solid.first;
    solid.half = box->half_size_metres;
  } else {
    const auto& capsule =
        std::get<BoardingPlantedBodyCapsuleBinding>(part.reservation);
    solid.shape = SelfShape::capsule;
    solid.first = p[body_index(capsule.start)];
    solid.second = p[body_index(capsule.end)];
    solid.radius = capsule.radius_metres;
  }
  return solid;
}
[[gnu::noinline]] auto transfer_owner(const TransferGraph& graph,
                                      const SelfRegion& region,
                                      BoardingLowerFootTransferOwner& out)
    -> void {
  Interval extent, limit, secondary = point(0);
  auto certificate = SelfCert::none;
  const auto second = static_cast<std::size_t>(region.second);
  const auto side = second >= 9 ? std::size_t{1} : std::size_t{0};
  bool identity{true};
  switch (region.junction) {
    case Junction::waist:
      extent = point(.12);
      limit = point(region.limit_metres);
      certificate = SelfCert::cap_partner_support;
      break;
    case Junction::neck:
      extent = limit = point(.2695);
      certificate = SelfCert::cap_partner_support;
      break;
    case Junction::hip: {
      Point u;
      for (std::size_t i = 0; i < u.size(); ++i)
        u[i] = divide(graph.knee_from_hip[side][i], point(thigh_length));
      const auto sign = side == 0 ? -1. : 1.;
      extent = add(add(subtract(multiply(point(.24), absolute(u[0])),
                                multiply(point(sign * .14), u[0])),
                       multiply(point(.12), absolute(u[1]))),
                   multiply(point(.18), absolute(u[2])));
      limit = point(region.limit_metres);
      certificate = SelfCert::original_axis_box_support;
      break;
    }
    case Junction::ankle: {
      Point u;
      for (std::size_t i = 0; i < u.size(); ++i) {
        const auto hip = i == 0 ? point(side == 0 ? -.14 : .14) : point(0);
        u[i] = divide(subtract(add(hip, graph.knee_from_hip[side][i]),
                               graph.ankle_from_root[side][i]),
                      point(shin_length));
      }
      extent = add(add(multiply(point(.06), absolute(u[0])),
                       multiply(point(.14), absolute(u[2]))),
                   u[1].supported && u[1].low >= 0
                       ? point(0)
                       : subtract(multiply(point(.05), absolute(u[1])),
                                  multiply(point(.05), u[1])));
      limit = point(region.limit_metres);
      identity = u[1].supported && u[1].low >= 0;
      certificate = SelfCert::original_axis_box_support;
      if (.075 > region.limit_metres || region.limit_metres >= shin_length)
        return;
      break;
    }
    case Junction::wrist:
      if (.055 > region.limit_metres || region.limit_metres >= .386) return;
      extent = point(.05);
      limit = point(region.limit_metres);
      certificate = SelfCert::original_axis_box_support;
      break;
    case Junction::knee:
    case Junction::elbow: {
      const auto cosine = region.junction == Junction::knee
                              ? negate(graph.knee_cosine[side])
                              : transfer_small_root(point(.5));
      const auto first_radius = region.junction == Junction::knee ? .105 : .065;
      const auto second_radius =
          region.junction == Junction::knee ? .075 : .055;
      const auto proof = self_half_ray(first_radius, second_radius,
                                       region.limit_metres, cosine);
      if (!proof.arithmetic_supported) return;
      extent = square(point(std::max(first_radius, second_radius)));
      limit = multiply(
          square(point(region.limit_metres)),
          interval(proof.sine_squared.lower, proof.sine_squared.upper));
      certificate = SelfCert::half_ray_angle_bound;
      break;
    }
    case Junction::shoulder: {
      if (.065 > region.limit_metres) return;
      const auto c = transfer_small_root(point(.5));
      const auto threshold = transfer_small_root(
          subtract(square(point(region.limit_metres)), square(point(.065))));
      extent = add(negate(multiply(threshold, c)), multiply(point(.065), c));
      secondary = add(negate(multiply(point(.35898), c)), point(.065));
      limit = point(-.18);
      certificate = SelfCert::shoulder_split;
      break;
    }
  }
  const auto gap = subtract(limit, extent);
  out.arithmetic_supported = extent.supported && limit.supported &&
                             secondary.supported && gap.supported;
  if (!out.arithmetic_supported) return;
  out.extent = bounds(extent);
  out.limit = bounds(limit);
  out.secondary_extent = bounds(secondary);
  out.structural_identity = identity;
  out.certificate = certificate;
  out.certified = region.junction == Junction::shoulder
                      ? extent.high < limit.low && secondary.high < limit.low
                      : gap.low >= 0;
}
auto transfer_capacity(TransferRefusal& refusal, TransferCondition condition)
    -> TransferResult {
  refusal.condition = condition;
  return TransferResult::capacity;
}
[[gnu::noinline]] auto transfer_separate(const EndpointSelfSolid& a,
                                         const EndpointSelfSolid& b,
                                         const TransferLimits& limits,
                                         TransferWork& work,
                                         TransferRefusal& refusal)
    -> TransferResult {
  const auto world_axis = [](std::size_t i) -> Vec {
    return i == 0 ? Vec{1, 0, 0} : i == 1 ? Vec{0, 1, 0} : Vec{0, 0, 1};
  };
  const auto count = std::size_t{4} + (a.shape == SelfShape::capsule ? 2 : 3) +
                     (b.shape == SelfShape::capsule ? 2 : 3);
  // Preserve the inherited proposal order, but generate each direction only
  // after its capacity check. A zero cap performs no hidden proposal work.
  const auto proposal = [&](std::size_t i) -> Vec {
    if (i < 3) return world_axis(i);
    if (i == 3)
      return self_difference(self_reporting(self_center(b)),
                             self_reporting(self_center(a)));
    i -= 4;
    for (const auto* solid : {&a, &b})
      if (solid->shape != SelfShape::capsule) {
        if (i < 3) return world_axis(i);
        i -= 3;
      }
    for (const auto* solid : {&a, &b})
      if (solid->shape == SelfShape::capsule) {
        if (i < 2) {
          const auto& other = solid == &a ? b : a;
          return self_difference(
              self_reporting(i == 0 ? solid->first : solid->second),
              self_reporting(self_center(other)));
        }
        i -= 2;
      }
    return {};
  };
  for (std::size_t i = 0; i < count; ++i) {
    if (work.proposed_axes >= limits.proposed_axes)
      return transfer_capacity(refusal, TransferCondition::axis_capacity);
    ++work.proposed_axes;
    const auto axis = proposal(i);
    if (!self_direction(axis)) continue;
    for (const auto direction : {axis, self_negate(axis)}) {
      if (work.signed_trials >= limits.signed_trials)
        return transfer_capacity(refusal,
                                 TransferCondition::signed_trial_capacity);
      ++work.signed_trials;
      const auto sum = add(self_support(a, direction),
                           self_support(b, self_negate(direction)));
      if (!sum.supported)
        return transfer_refuse(refusal,
                               TransferCondition::unsupported_arithmetic);
      if (sum.high <= 0) return TransferResult::accepted;
    }
  }
  return transfer_refuse(refusal, TransferCondition::unresolved_self_pair);
}
[[gnu::noinline]] auto transfer_self(
    const BoardingSourceEndpointLoadDiagnostic& initial,
    const TransferGraph& graph, const TransferLimits& limits,
    TransferWork& work, TransferCell& out, TransferRefusal& refusal)
    -> TransferResult {
  std::array<Point, kBoardingPlantedBodyPointCount> relative;
  if (!transfer_relative_points(graph, relative))
    return transfer_refuse(refusal, TransferCondition::unsupported_arithmetic);
  const auto& parts = initial.self.endpoint.parts;
  const auto& regions = initial.self.regions;
  std::size_t pair_index{};
  for (std::size_t a = 0; a < parts.size(); ++a)
    for (std::size_t b = a + 1; b < parts.size(); ++b, ++pair_index) {
      refusal.pair = pair_index;
      if (work.self_pairs >= limits.self_pairs)
        return transfer_capacity(refusal, TransferCondition::pair_capacity);
      ++work.self_pairs;
      auto certificate = SelfCert::none;
      for (std::size_t r = 0; r < regions.size(); ++r)
        if (static_cast<std::size_t>(regions[r].first) == a &&
            static_cast<std::size_t>(regions[r].second) == b) {
          transfer_owner(graph, regions[r], out.owners[r]);
          if (!out.owners[r].arithmetic_supported)
            return transfer_refuse(refusal,
                                   TransferCondition::unsupported_arithmetic);
          if (out.owners[r].certified) certificate = out.owners[r].certificate;
          break;
        }
      if (certificate == SelfCert::none) {
        const auto first = transfer_solid(parts[a], relative),
                   second = transfer_solid(parts[b], relative);
        const auto result =
            transfer_separate(first, second, limits, work, refusal);
        if (result != TransferResult::accepted) return result;
        certificate = SelfCert::convex_support_plane;
      }
      out.pair_certificates[pair_index] = certificate;
      ++out.certificate_counts[static_cast<std::size_t>(certificate)];
    }
  out.self_complete = pair_index == kBoardingBodyPairCount;
  refusal.pair.reset();
  return out.self_complete
             ? TransferResult::accepted
             : transfer_refuse(refusal, TransferCondition::invalid_binding);
}
} // namespace

auto detail::boarding_lower_foot_transfer_kinematic_math(
    double first, double last, bool reverse, TransferCell& out,
    TransferRefusal& refusal) -> TransferResult {
  TransferGraph graph;
  const auto parts = body_parts();
  return transfer_body(first, last, reverse, parts, graph, out, refusal);
}
auto detail::boarding_lower_foot_transfer_cell(
    const BoardingSourceEndpointLoadDiagnostic& initial,
    const BoardingLowerFootTransferPressureContext& pressure, double first,
    double last, bool reverse, const TransferLimits& limits, TransferWork& work,
    TransferCell& out, TransferRefusal& refusal) -> TransferResult {
  if (pressure.initial_ != &initial || !initial.load.complete ||
      !initial.self.complete) {
    transfer_reset(first, last, out, refusal);
    return transfer_refuse(refusal, TransferCondition::invalid_binding);
  }
  TransferGraph graph;
  auto result = transfer_body(first, last, reverse, initial.self.endpoint.parts,
                              graph, out, refusal);
  if (result != TransferResult::accepted) return result;
  result = transfer_self(initial, graph, limits, work, out, refusal);
  if (result != TransferResult::accepted) return result;
  result = boarding_lower_foot_transfer_pressure_cell(pressure, limits, work,
                                                      out, refusal);
  if (result != TransferResult::accepted) return result;
  out.complete =
      out.kinematics_complete && out.timing_complete && out.self_complete &&
      out.nominal_vertical_equilibrium_complete && out.finite_pressure_complete;
  refusal.condition = TransferCondition::none;
  return out.complete
             ? TransferResult::accepted
             : transfer_refuse(refusal, TransferCondition::invalid_binding);
}
} // namespace apsis_drift

#include "origin_boarding_route_foot_phase_internal.hpp"

namespace apsis_drift {
namespace {
using PhaseRequest = BoardingRouteFootPhaseRequest;
using PhaseCell = BoardingRouteFootPhaseCell;
using PhaseReason = BoardingRouteFootPhaseRefusal;
using PhaseCondition = BoardingRouteFootPhaseCondition;
using PhaseState = detail::BoardingRouteFootPhaseCellResult;
using PhaseLimits = detail::BoardingRouteFootPhaseLimits;
using PhaseWork = BoardingRouteFootPhaseCounters;
using PhaseFrame = std::array<TimingPoint, 3>;
struct PhaseGraph {
  std::array<TimingPoint, kBoardingPlantedBodyPointCount> points;
  std::array<PhaseFrame, 4> frames;
  std::array<TimingPoint, 2> soles;
  AngularTiming root_yaw, torso;
  std::array<AngularTiming, 2> sole_yaw;
  TimingScalar S, share;
};
static_assert(sizeof(PhaseCell) <= kBoardingRouteFootPhaseMaximumCellBytes);
static_assert(sizeof(PhaseGraph) + sizeof(PhaseCell) + sizeof(PhaseRequest) +
                  11 * std::size_t{24} + std::size_t{8192} + std::size_t{6144} +
                  std::size_t{512} <=
              kBoardingRouteFootPhaseMaximumScratchBytes);
// At most four exact addends; no inherited 256-term expansion/root path.
auto phase_sum_sign(const std::array<double, 4>& terms) -> Sign {
  std::array<double, 4> values{}, next{};
  std::size_t size{};
  for (const auto term : terms) {
    if (!std::isfinite(term)) return Sign::unsupported;
    auto carry = term;
    std::size_t count{};
    for (std::size_t i = 0; i < size; ++i) {
      const auto high = carry + values[i], bv = high - carry, av = high - bv;
      const auto low = (carry - av) + (values[i] - bv);
      if (!std::isfinite(high) || !std::isfinite(low)) return Sign::unsupported;
      if (low != 0) {
        if (count == next.size()) return Sign::unsupported;
        next[count++] = low;
      }
      carry = high;
    }
    if (carry != 0) {
      if (count == next.size()) return Sign::unsupported;
      next[count++] = carry;
    }
    values = next;
    size = count;
  }
  return size == 0              ? Sign::zero
         : values[size - 1] < 0 ? Sign::negative
                                : Sign::positive;
}
auto phase_constant(const BoardingRoutePhaseConstant& c) -> Interval {
  if (c.count == 0 || c.count > c.terms.size()) return failed();
  std::array<double, 4> terms{};
  auto value = point(0);
  for (std::size_t i = 0; i < c.terms.size(); ++i) {
    if (!bounded(c.terms[i], 8) || (i >= c.count && c.terms[i] != 0))
      return failed();
    if (i < c.count) {
      terms[i] = c.terms[i];
      value = add(value, point(c.terms[i]));
    }
  }
  terms[3] = 8;
  const auto lower = phase_sum_sign(terms);
  terms[3] = -8;
  const auto upper = phase_sum_sign(terms);
  if (lower == Sign::negative || upper == Sign::positive ||
      lower == Sign::unsupported || upper == Sign::unsupported ||
      !value.supported)
    return failed();
  value.low = std::max(-8., value.low);
  value.high = std::min(8., value.high);
  return value;
}
auto phase_carrier(double a, double b, const TimingScalar& S) -> TimingScalar {
  if (a == b) return timing_constant(a);
  return timing_add(
      timing_constant(a),
      timing_multiply(timing_constant(subtract(point(b), point(a))), S));
}
auto phase_carrier(const BoardingRoutePhaseConstant& a,
                   const BoardingRoutePhaseConstant& b, const TimingScalar& S)
    -> TimingScalar {
  const auto start = phase_constant(a);
  if (a.count == b.count && a.terms == b.terms) return timing_constant(start);
  return timing_add(
      timing_constant(start),
      timing_multiply(timing_constant(subtract(phase_constant(b), start)), S));
}
auto phase_point(const std::array<BoardingRoutePhasePointConstant, 2>& c,
                 const TimingScalar& S) -> TimingPoint {
  TimingPoint result;
  for (std::size_t i = 0; i < 3; ++i)
    result[i] = phase_carrier(c[0].coordinates[i], c[1].coordinates[i], S);
  return result;
}
auto phase_hump_value(double u) -> Interval {
  if (u == 0 || u == 1) return point(0);
  if (u == .5) return point(1);
  const auto t = point(u), v = subtract(point(1), t);
  return multiply(point(64),
                  multiply(multiply(square(t), t), multiply(square(v), v)));
}
auto phase_hump(double first, double last) -> TimingScalar {
  const auto a = phase_hump_value(first), b = phase_hump_value(last);
  const auto u = interval(first, last), v = subtract(point(1), u),
             uv = multiply(u, v);
  auto value = interval(
      std::max(0., std::min(a.low, b.low)),
      first <= .5 && last >= .5 ? 1 : std::min(1., std::max(a.high, b.high)));
  auto rate =
      multiply(point(192),
               multiply(square(uv), subtract(point(1), multiply(point(2), u))));
  auto second = multiply(
      point(384), multiply(uv, add(subtract(point(1), multiply(point(5), u)),
                                   multiply(point(5), square(u)))));
  if (first == last && (first == 0 || first == 1)) rate = second = point(0);
  if (!a.supported || !b.supported) return {};
  return {value, rate, second};
}
auto phase_add(const TimingPoint& a, const TimingPoint& b) -> TimingPoint {
  TimingPoint result;
  for (std::size_t i = 0; i < 3; ++i)
    result[i] = timing_add(a[i], b[i]);
  return result;
}
auto phase_sub(const TimingPoint& a, const TimingPoint& b) -> TimingPoint {
  TimingPoint result;
  for (std::size_t i = 0; i < 3; ++i)
    result[i] = timing_subtract(a[i], b[i]);
  return result;
}
auto phase_scale(const TimingPoint& a, const TimingScalar& b) -> TimingPoint {
  TimingPoint result;
  for (std::size_t i = 0; i < 3; ++i)
    result[i] = timing_multiply(a[i], b);
  return result;
}
auto phase_vector(Vec v) -> TimingPoint {
  return {timing_constant(v.x), timing_constant(v.y), timing_constant(v.z)};
}
auto phase_rotate(const PhaseFrame& f, const TimingPoint& p) -> TimingPoint {
  return phase_add(phase_add(phase_scale(f[0], p[0]), phase_scale(f[1], p[1])),
                   phase_scale(f[2], p[2]));
}
auto phase_dot(const TimingPoint& a, const TimingPoint& b) -> TimingScalar {
  return timing_add(
      timing_add(timing_multiply(a[0], b[0]), timing_multiply(a[1], b[1])),
      timing_multiply(a[2], b[2]));
}
auto phase_local(const PhaseFrame& f, const TimingPoint& p) -> TimingPoint {
  return {phase_dot(f[0], p), phase_dot(f[1], p), phase_dot(f[2], p)};
}
auto phase_rotation(const TimingScalar& k, bool yaw) -> PhaseFrame {
  const auto k2 = timing_square(k), den = timing_add(timing_constant(1), k2);
  const auto c = timing_divide(timing_subtract(timing_constant(1), k2), den),
             s = timing_divide(timing_multiply(timing_constant(2), k), den),
             z = timing_constant(0), one = timing_constant(1);
  if (yaw) return {{{c, z, timing_negate(s)}, {z, one, z}, {s, z, c}}};
  return {{{one, z, z}, {z, c, s}, {z, timing_negate(s), c}}};
}
auto phase_angle(const TimingScalar& k) -> AngularTiming {
  const auto den = add(point(1), square(k.value));
  return {
      divide(multiply(point(2), k.first), den),
      subtract(divide(multiply(point(2), k.second), den),
               divide(multiply(point(4), multiply(k.value, square(k.first))),
                      square(den)))};
}
auto phase_angular(const AngularTiming& a, double T, bool reverse)
    -> BoardingPlantedLegAngularDerivatives {
  auto first = divide(a.first, point(T));
  if (reverse) first = negate(first);
  return {bounds(first), bounds(divide(a.second, square(point(T))))};
}
auto phase_evidence(const TimingPoint& p, double T, bool reverse,
                    BodyPointEvidence& out, bool workspace = true) -> bool {
  Point value{}, first{}, second{};
  for (std::size_t i = 0; i < 3; ++i) {
    if (!timing_supported(p[i]) ||
        (workspace && (p[i].value.low < -8 || p[i].value.high > 8)))
      return false;
    value[i] = p[i].value;
    first[i] = divide(p[i].first, point(T));
    if (reverse) first[i] = negate(first[i]);
    second[i] = divide(p[i].second, square(point(T)));
  }
  if (!self_point_supported(first) || !self_point_supported(second))
    return false;
  out = {point_bounds(value), {point_bounds(first), point_bounds(second)}};
  return true;
}
auto phase_norm(const Point& v) -> Interval {
  auto sum = add(add(square(v[0]), square(v[1])), square(v[2]));
  if (!sum.supported) return failed();
  sum.low = std::max(0., sum.low);
  return transfer_small_root(sum);
}
auto phase_derivative_norm(const TimingPoint& p, double T, bool second = false)
    -> Interval {
  Point v;
  const auto den = second ? square(point(T)) : point(T);
  for (std::size_t i = 0; i < 3; ++i)
    v[i] = divide(second ? p[i].second : p[i].first, den);
  return phase_norm(v);
}
auto phase_refuse(PhaseReason& out, PhaseCondition why, Interval limit = {})
    -> PhaseState {
  out.condition = why;
  out.limiting_bound = limit.supported ? bounds(limit) : Bounds{};
  return why == PhaseCondition::unsupported_arithmetic ? PhaseState::unsupported
                                                       : PhaseState::unresolved;
}
auto phase_charge(std::uint64_t& work, std::uint64_t cap, PhaseCondition reason,
                  PhaseReason& out) -> bool {
  if (work >= cap) {
    out.condition = reason;
    return false;
  }
  ++work;
  return true;
}
struct PhaseScalarLimits {
  Interval sqrt3, sqrt2, sin20, cos20, cos35, arm;
};
auto phase_scalar_limits() -> const PhaseScalarLimits& {
  static const PhaseScalarLimits result{
      transfer_small_root(point(3)), transfer_small_root(point(2)),
      trig_constant(20, true),       trig_constant(20, false),
      trig_constant(35, false),      transfer_small_root(point(.5))};
  return result;
}
auto phase_hip_sector(Interval F, Interval G)
    -> detail::BoardingRoutePhaseHipSectorEvidence;
auto phase_hip_speed(Interval axial, Interval roll_rate, Interval pitch,
                     Interval sine) -> BoardingPlantedLegSpeedEvidence;
} // namespace

auto detail::boarding_route_foot_phase_environment() -> bool {
  return supported_environment();
}
auto detail::boarding_route_foot_phase_request_valid(const PhaseRequest& r)
    -> std::expected<void, std::string> {
  auto valid_constant = [](const BoardingRoutePhaseConstant& c) {
    if (c.count == 0 || c.count > c.terms.size()) return false;
    for (std::size_t i = 0; i < c.terms.size(); ++i)
      if (!bounded(c.terms[i], 8) || (i >= c.count && c.terms[i] != 0))
        return false;
    return !supported_environment() || phase_constant(c).supported;
  };
  for (const auto& p : r.root)
    for (const auto& c : p.coordinates)
      if (!valid_constant(c))
        return std::unexpected(
            "Phase root constant requires1..3 finite terms with sum in[-8,8]");
  for (const auto& f : r.feet) {
    for (const auto& p : f.sole)
      for (const auto& c : p.coordinates)
        if (!valid_constant(c))
          return std::unexpected("Phase sole constant requires1..3 finite "
                                 "terms with sum in[-8,8]");
    for (const auto k : f.yaw_half)
      if (!bounded(k, 1))
        return std::unexpected(
            "Phase sole yaw carrier must be finite in[-1,1]");
    if (!std::isfinite(f.swing_height_metres) || f.swing_height_metres < 0 ||
        f.swing_height_metres > .25)
      return std::unexpected("Phase swing height must be finite in[0,.25]");
  }
  for (const auto k : r.root_yaw_half)
    if (!bounded(k, 1))
      return std::unexpected("Phase root yaw carrier must be finite in[-1,1]");
  for (const auto k : r.torso_lean_half)
    if (!bounded(k, 1))
      return std::unexpected("Phase torso carrier must be finite in[-1,1]");
  for (const auto w : r.port_reaction_fraction)
    if (!std::isfinite(w) || w < 0 || w > 1)
      return std::unexpected("Phase reaction cue must be finite in[0,1]");
  if (!std::isfinite(r.seconds_per_parameter) || r.seconds_per_parameter < 1 ||
      r.seconds_per_parameter > 120)
    return std::unexpected("Phase physical duration must be finite in[1,120]");
  return {};
}
auto detail::boarding_route_foot_phase_parts()
    -> std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount> {
  const auto old = body_parts();
  std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount> result;
  for (std::size_t i = 0; i < old.size(); ++i) {
    result[i].id = old[i].id;
    result[i].mass = old[i].mass;
    if (const auto* box =
            std::get_if<BoardingPlantedBodyBoxBinding>(&old[i].reservation)) {
      auto frame = BoardingRoutePhaseFrame::trunk;
      if (i == 0) frame = BoardingRoutePhaseFrame::root;
      if (i == 5) frame = BoardingRoutePhaseFrame::port_sole;
      if (i == 11) frame = BoardingRoutePhaseFrame::starboard_sole;
      result[i].reservation = BoardingRoutePhaseBoxBinding{
          box->center, box->half_size_metres, frame};
    } else
      result[i].reservation =
          std::get<BoardingPlantedBodyCapsuleBinding>(old[i].reservation);
  }
  return result;
}
namespace {
[[gnu::noinline]] auto phase_graph(const PhaseRequest& r, double first,
                                   double last, PhaseGraph& g,
                                   PhaseReason& reason) -> PhaseState {
  g.S = transfer_quintic(first, last);
  g.share = phase_carrier(r.port_reaction_fraction[0],
                          r.port_reaction_fraction[1], g.S);
  g.points[0] = phase_point(r.root, g.S);
  const auto k = phase_carrier(r.root_yaw_half[0], r.root_yaw_half[1], g.S),
             torso =
                 phase_carrier(r.torso_lean_half[0], r.torso_lean_half[1], g.S);
  g.frames[0] = phase_rotation(k, true);
  const auto relative = phase_rotation(torso, false);
  for (std::size_t i = 0; i < 3; ++i)
    g.frames[1][i] = phase_rotate(g.frames[0], relative[i]);
  g.root_yaw = phase_angle(k);
  g.torso = phase_angle(torso);
  const auto hump = phase_hump(first, last);
  for (std::size_t side = 0; side < 2; ++side) {
    g.soles[side] = phase_point(r.feet[side].sole, g.S);
    g.soles[side][1] = timing_add(
        g.soles[side][1],
        timing_multiply(timing_constant(r.feet[side].swing_height_metres),
                        hump));
    const auto fk =
        phase_carrier(r.feet[side].yaw_half[0], r.feet[side].yaw_half[1], g.S);
    g.frames[side + 2] = phase_rotation(fk, true);
    g.sole_yaw[side] = phase_angle(fk);
  }
  if (!timing_supported(g.S) || !timing_supported(g.share))
    return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
  return PhaseState::accepted;
}
[[gnu::noinline]] auto phase_leg(const PhaseRequest& r, std::size_t side,
                                 bool reverse, PhaseGraph& g, PhaseCell& out,
                                 const PhaseLimits& caps, PhaseWork& work,
                                 PhaseReason& reason) -> PhaseState {
  reason.side = side;
  const auto base = body_index(side == 0 ? BodyPointId::port_hip
                                         : BodyPointId::starboard_hip);
  const auto sign = side == 0 ? -1. : 1.;
  const auto hip = phase_add(
      g.points[0], phase_rotate(g.frames[0], phase_vector({sign * .14, 0, 0})));
  const auto ankle = phase_add(g.soles[side], phase_vector({0, .1, 0}));
  const auto boot = phase_add(g.soles[side], phase_vector({0, .05, 0}));
  const auto d = phase_local(g.frames[side + 2], phase_sub(ankle, hip));
  const auto rho2 = timing_add(timing_square(d[0]), timing_square(d[1])),
             D = timing_add(rho2, timing_square(d[2]));
  if (!timing_supported(D) || !timing_supported(rho2))
    return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
  if (d[1].value.high >= 0)
    return phase_refuse(reason, PhaseCondition::forward_branch, d[1].value);
  if (rho2.value.low <= 0 || D.value.low <= 0)
    return phase_refuse(reason, PhaseCondition::derivative_domain, rho2.value);
  const auto l1 = point(thigh_length), l2 = point(shin_length),
             l1sq = square(l1), l2sq = square(l2),
             maximum = square(add(l1, l2)), minimum = square(subtract(l1, l2));
  if (D.value.high > maximum.low || D.value.low < minimum.high)
    return phase_refuse(reason, PhaseCondition::reach, D.value);
  const auto rho = transfer_root_jet(rho2),
             alpha = timing_divide(
                 timing_add(timing_constant(subtract(l1sq, l2sq)), D),
                 timing_multiply(timing_constant(2), D));
  const auto outer = timing_subtract(timing_constant(maximum), D),
             inner = timing_subtract(D, timing_constant(minimum));
  const auto gamma2 =
      timing_divide(timing_multiply(outer, inner),
                    timing_multiply(timing_constant(4), timing_square(D)));
  if (!timing_supported(gamma2))
    return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
  if (gamma2.value.low <= 0)
    return phase_refuse(reason, PhaseCondition::derivative_domain,
                        gamma2.value);
  const auto gamma = transfer_root_jet(gamma2);
  if (!timing_supported(rho) || !timing_supported(alpha) ||
      !timing_supported(gamma))
    return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
  const TimingPoint q{timing_divide(timing_multiply(d[0], d[2]), rho),
                      timing_divide(timing_multiply(d[1], d[2]), rho),
                      timing_negate(rho)};
  const auto knee = phase_add(
      hip, phase_rotate(g.frames[side + 2], phase_add(phase_scale(d, alpha),
                                                      phase_scale(q, gamma))));
  g.points[base] = hip;
  g.points[base + 1] = knee;
  g.points[base + 2] = ankle;
  g.points[base + 3] = boot;
  const auto complement = timing_subtract(timing_constant(1), alpha),
             F1 = timing_add(timing_multiply(alpha, rho),
                             timing_multiply(gamma, d[2])),
             G1 = timing_subtract(timing_multiply(gamma, rho),
                                  timing_multiply(alpha, d[2])),
             F2 = timing_subtract(timing_multiply(complement, rho),
                                  timing_multiply(gamma, d[2])),
             G2 = timing_negate(timing_add(timing_multiply(complement, d[2]),
                                           timing_multiply(gamma, rho)));
  if (!timing_supported(F1) || !timing_supported(G1) || !timing_supported(F2) ||
      !timing_supported(G2))
    return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
  auto& leg = out.legs[side];
  leg.distance_squared = bounds(D.value);
  leg.rho_squared = bounds(rho2.value);
  leg.gamma_squared = bounds(gamma2.value);
  leg.alpha = bounds(alpha.value);
  leg.gamma = bounds(gamma.value);
  const auto hp = pitch_derivatives(F1, G1, l1sq),
             sp = pitch_derivatives(F2, G2, l2sq);
  const AngularTiming kp{subtract(hp.first, sp.first),
                         subtract(hp.second, sp.second)};
  const auto phinumerator = subtract(multiply(negate(d[1].value), d[0].first),
                                     multiply(d[0].value, negate(d[1].first)));
  const AngularTiming phi{
      divide(phinumerator, rho2.value),
      subtract(divide(subtract(multiply(negate(d[1].value), d[0].second),
                               multiply(d[0].value, negate(d[1].second))),
                      rho2.value),
               divide(multiply(phinumerator, rho2.first), square(rho2.value)))};
  const AngularTiming axial{
      subtract(g.sole_yaw[side].first, g.root_yaw.first),
      subtract(g.sole_yaw[side].second, g.root_yaw.second)};
  const std::array angles{hp, sp, kp, phi, axial};
  for (const auto& angle : angles)
    if (!angle.first.supported || !angle.second.supported)
      return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
  const auto T = r.seconds_per_parameter;
  leg.hip_pitch = phase_angular(hp, T, reverse);
  leg.shin_pitch = phase_angular(sp, T, reverse);
  leg.knee_flex = phase_angular(kp, T, reverse);
  leg.hip_axial = phase_angular(axial, T, reverse);
  leg.hip_abduction =
      phase_angular(side == 0 ? negate_angular(phi) : phi, T, reverse);
  leg.ankle_pitch = phase_angular(negate_angular(sp), T, reverse);
  leg.ankle_roll = phase_angular(negate_angular(phi), T, reverse);
  if (!phase_charge(work.sectors, caps.sectors, PhaseCondition::sector_capacity,
                    reason))
    return PhaseState::capacity;
  const auto& limits = phase_scalar_limits();
  const auto hip_sector = phase_hip_sector(F1.value, G1.value);
  if (!hip_sector.arithmetic_supported)
    return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
  const auto rollmargin = subtract(
                 multiply(negate(d[1].value), subtract(point(2), limits.sqrt3)),
                 absolute(d[0].value)),
             lower = interval(hip_sector.margins[0].lower,
                              hip_sector.margins[0].upper),
             upper = interval(hip_sector.margins[1].lower,
                              hip_sector.margins[1].upper),
             knee_cos = divide(subtract(subtract(D.value, l1sq), l2sq),
                               multiply(point(2), multiply(l1, l2))),
             kneemargin = add(knee_cos, divide(limits.sqrt2, point(2))),
             anklemargin = subtract(
                 multiply(F2.value, point(.5)),
                 multiply(absolute(G2.value), divide(limits.sqrt3, point(2)))),
             relativecos =
                 phase_dot(g.frames[0][0], g.frames[side + 2][0]).value,
             axialmargin =
                 subtract(relativecos, divide(limits.sqrt2, point(2)));
  const std::array margins{rollmargin, lower,       upper,
                           kneemargin, anklemargin, axialmargin};
  for (std::size_t i = 0; i < margins.size(); ++i) {
    leg.sector_margins[i] = bounds(margins[i]);
    if (!margins[i].supported)
      return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
    if (margins[i].low < 0)
      return phase_refuse(reason, PhaseCondition::joint_sector, margins[i]);
  }
  if (F2.value.low <= 0)
    return phase_refuse(reason, PhaseCondition::joint_sector, F2.value);
  leg.nominal_links = leg.target_sole_identity = leg.derivative_domains =
      leg.joint_sectors = true;
  if (!phase_charge(work.timing, caps.timing, PhaseCondition::timing_capacity,
                    reason))
    return PhaseState::capacity;
  const auto delta = divide(axial.first, point(T)),
             phidot = divide(phi.first, point(T)),
             hipdot = divide(hp.first, point(T)),
             shindot = divide(sp.first, point(T)),
             kneedot = divide(kp.first, point(T)),
             sinphi = divide(d[0].value, rho.value);
  const auto hip_speed = phase_hip_speed(delta, phidot, hipdot, sinphi);
  const auto anklesquared = add(square(shindot), square(phidot));
  if (!hip_speed.supported || !anklesquared.supported)
    return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
  const std::array speeds{
      interval(hip_speed.speed_radians_per_second.lower,
               hip_speed.speed_radians_per_second.upper),
      absolute(kneedot),
      transfer_small_root(
          interval(std::max(0., anklesquared.low), anklesquared.high))};
  for (std::size_t i = 0; i < speeds.size(); ++i) {
    leg.joint_speeds[i] = bounds(speeds[i]);
    if (!speeds[i].supported)
      return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
    if ((i == 0 && !hip_speed.certified) ||
        (i != 0 && speeds[i].high > speed_threshold().low))
      return phase_refuse(reason, PhaseCondition::joint_speed, speeds[i]);
  }
  leg.timing_complete = true;
  return PhaseState::accepted;
}
[[gnu::noinline]] auto phase_body(const PhaseRequest& r, bool reverse,
                                  PhaseGraph& g, PhaseCell& out,
                                  PhaseReason& reason) -> PhaseState {
  const auto offset = [&](const TimingPoint& p, Vec v) {
    return phase_add(p, phase_rotate(g.frames[1], phase_vector(v)));
  };
  g.points[1] = offset(g.points[0], {0, .3495, 0});
  g.points[2] = offset(g.points[0], {0, .70237, 0});
  g.points[3] = offset(g.points[0], {0, .65237, 0});
  const auto c = phase_scalar_limits().arm;
  if (!c.supported)
    return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
  const TimingPoint elbow_delta{
      timing_constant(0), timing_constant(negate(multiply(point(.35898), c))),
      timing_constant(negate(multiply(point(.35898), c)))};
  for (std::size_t side = 0; side < 2; ++side) {
    const auto base = body_index(side == 0 ? BodyPointId::port_hip
                                           : BodyPointId::starboard_hip);
    g.points[base + 4] =
        offset(g.points[0], {side == 0 ? -.20265 : .20265, .579, 0});
    g.points[base + 5] =
        phase_add(g.points[base + 4], phase_rotate(g.frames[1], elbow_delta));
    g.points[base + 6] = offset(g.points[base + 5], {0, .386, 0});
  }
  for (std::size_t i = 0; i < g.points.size(); ++i)
    if (!phase_evidence(g.points[i], r.seconds_per_parameter, reverse,
                        out.points[i]))
      return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
  for (std::size_t frame = 0; frame < g.frames.size(); ++frame)
    for (std::size_t column = 0; column < 3; ++column)
      if (!phase_evidence(g.frames[frame][column], r.seconds_per_parameter,
                          reverse, out.frames[frame].columns[column], false))
        return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
  const auto parts = body_parts();
  auto sum = phase_vector({0, 0, 0});
  for (std::size_t i = 0; i < parts.size(); ++i) {
    const auto& m = parts[i].mass;
    const auto a = body_index(m.first), b = body_index(m.second);
    const auto p = a == b ? g.points[a]
                          : phase_scale(phase_add(g.points[a], g.points[b]),
                                        timing_constant(.5));
    if (!phase_evidence(p, r.seconds_per_parameter, reverse,
                        out.mass_points[i]))
      return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
    sum = phase_add(
        sum, phase_scale(p, timing_constant(static_cast<double>(m.weight))));
  }
  if (!phase_evidence(
          phase_scale(sum, timing_constant(divide(point(1), point(1200)))),
          r.seconds_per_parameter, reverse, out.center_of_mass))
    return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
  return PhaseState::accepted;
}
[[gnu::noinline]] auto phase_other_predicates(
    const PhaseRequest& r, const PhaseLimits& caps, PhaseWork& work,
    const PhaseGraph& g, PhaseCell& out, PhaseReason& reason) -> PhaseState {
  reason.side.reset();
  if (!phase_charge(work.sectors, caps.sectors, PhaseCondition::sector_capacity,
                    reason))
    return PhaseState::capacity;
  const auto torso_cos =
      phase_rotation(
          phase_carrier(r.torso_lean_half[0], r.torso_lean_half[1], g.S),
          false)[1][1]
          .value;
  const auto torso_margin = subtract(torso_cos, phase_scalar_limits().cos35);
  if (!torso_margin.supported)
    return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
  if (torso_margin.low < 0)
    return phase_refuse(reason, PhaseCondition::joint_sector, torso_margin);
  const auto T = r.seconds_per_parameter;
  if (!phase_charge(work.timing, caps.timing, PhaseCondition::timing_capacity,
                    reason))
    return PhaseState::capacity;
  const auto rs = phase_derivative_norm(g.points[0], T),
             ra = phase_derivative_norm(g.points[0], T, true),
             yaw = absolute(divide(g.root_yaw.first, point(T)));
  out.root_speed = bounds(rs);
  out.root_acceleration = bounds(ra);
  out.root_yaw_speed = bounds(yaw);
  if (!rs.supported || !ra.supported || !yaw.supported)
    return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
  if (rs.high > .25)
    return phase_refuse(reason, PhaseCondition::root_speed, rs);
  if (ra.high > .10)
    return phase_refuse(reason, PhaseCondition::root_acceleration, ra);
  if (yaw.high > speed_threshold().low)
    return phase_refuse(reason, PhaseCondition::joint_speed, yaw);
  if (!phase_charge(work.timing, caps.timing, PhaseCondition::timing_capacity,
                    reason))
    return PhaseState::capacity;
  const auto torso = absolute(divide(g.torso.first, point(T)));
  out.torso_joint_speed = bounds(torso);
  if (!torso.supported)
    return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
  if (torso.high > speed_threshold().low)
    return phase_refuse(reason, PhaseCondition::joint_speed, torso);
  const auto sole_radius =
      transfer_small_root(add(square(point(.06)), square(point(.14))));
  if (!sole_radius.supported)
    return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
  for (std::size_t side = 0; side < 2; ++side) {
    reason.side = side;
    if (!phase_charge(work.timing, caps.timing, PhaseCondition::timing_capacity,
                      reason))
      return PhaseState::capacity;
    const auto center = phase_derivative_norm(g.soles[side], T),
               spin = absolute(divide(g.sole_yaw[side].first, point(T))),
               whole = add(center, multiply(sole_radius, spin));
    out.sole_center_speed[side] = bounds(center);
    out.sole_yaw_speed[side] = bounds(spin);
    out.whole_sole_speed[side] = bounds(whole);
    if (!center.supported || !spin.supported || !whole.supported)
      return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
    if (whole.high > .35)
      return phase_refuse(reason, PhaseCondition::sole_speed, whole);
  }
  reason.side.reset();
  return PhaseState::accepted;
}
} // namespace

auto detail::boarding_route_foot_phase_cell(const PhaseRequest& r, double first,
                                            double last, bool reverse,
                                            const PhaseLimits& caps,
                                            PhaseWork& work, PhaseCell& out,
                                            PhaseReason& reason) -> PhaseState {
  out = {};
  reason = {};
  out.first = first;
  out.last = last;
  const PhaseLimits original;
  if (!std::isfinite(first) || !std::isfinite(last) || first < 0 || last > 1 ||
      first > last || caps.depth > original.depth ||
      caps.nodes > original.nodes || caps.leaves > original.leaves ||
      caps.output_bytes > original.output_bytes ||
      caps.graphs > original.graphs || caps.legs > original.legs ||
      caps.bodies > original.bodies || caps.sectors > original.sectors ||
      caps.timing > original.timing ||
      !boarding_route_foot_phase_request_valid(r) || !supported_environment())
    return phase_refuse(reason, PhaseCondition::unsupported_arithmetic);
  const std::array incoming{work.graphs, work.legs, work.bodies, work.sectors,
                            work.timing};
  const std::array ceilings{caps.graphs, caps.legs, caps.bodies, caps.sectors,
                            caps.timing};
  const std::array failures{
      PhaseCondition::graph_capacity, PhaseCondition::leg_capacity,
      PhaseCondition::body_capacity, PhaseCondition::sector_capacity,
      PhaseCondition::timing_capacity};
  for (std::size_t i = 0; i < incoming.size(); ++i)
    if (incoming[i] > ceilings[i]) {
      reason.condition = failures[i];
      return PhaseState::capacity;
    }
  if (!phase_charge(work.graphs, caps.graphs, PhaseCondition::graph_capacity,
                    reason))
    return PhaseState::capacity;
  PhaseGraph g;
  auto state = phase_graph(r, first, last, g, reason);
  if (state != PhaseState::accepted) return state;
  out.port_reaction_fraction = bounds(g.share.value);
  for (std::size_t side = 0; side < 2; ++side) {
    reason.side = side;
    if (!phase_charge(work.legs, caps.legs, PhaseCondition::leg_capacity,
                      reason))
      return PhaseState::capacity;
    state = phase_leg(r, side, reverse, g, out, caps, work, reason);
    if (state != PhaseState::accepted) return state;
  }
  reason.side.reset();
  if (!phase_charge(work.bodies, caps.bodies, PhaseCondition::body_capacity,
                    reason))
    return PhaseState::capacity;
  state = phase_body(r, reverse, g, out, reason);
  if (state != PhaseState::accepted) return state;
  state = phase_other_predicates(r, caps, work, g, out, reason);
  if (state != PhaseState::accepted) return state;
  out.arithmetic_supported = out.nominal_links = out.target_sole_identities =
      out.joint_sectors = out.derivative_domains = out.timing_complete =
          out.complete = true;
  reason = {};
  return PhaseState::accepted;
}
} // namespace apsis_drift

namespace apsis_drift {
namespace {
auto phase_numeric_interval(Bounds b, double maximum) -> Interval {
  return std::isfinite(b.lower) && std::isfinite(b.upper) &&
                 b.lower >= -maximum && b.upper <= maximum
             ? interval(b.lower, b.upper)
             : failed();
}
auto phase_hip_sector(Interval F, Interval G)
    -> detail::BoardingRoutePhaseHipSectorEvidence {
  detail::BoardingRoutePhaseHipSectorEvidence out;
  if (!supported_environment()) return out;
  const auto& constants = phase_scalar_limits();
  const std::array margins{
      add(multiply(G, constants.cos20), multiply(F, constants.sin20)),
      add(multiply(F, divide(constants.sqrt3, point(2))),
          multiply(G, point(.5)))};
  for (std::size_t i = 0; i < margins.size(); ++i) {
    if (!margins[i].supported) return out;
    out.margins[i] = bounds(margins[i]);
  }
  out.arithmetic_supported = true;
  out.certified = margins[0].low >= 0 && margins[1].low >= 0;
  return out;
}
auto phase_hip_speed(Interval axial, Interval roll_rate, Interval pitch,
                     Interval sine) -> BoardingPlantedLegSpeedEvidence {
  BoardingPlantedLegSpeedEvidence result;
  if (!supported_environment()) return result;
  auto squared =
      add(add(add(square(axial), square(roll_rate)), square(pitch)),
          multiply(point(2), multiply(multiply(axial, pitch), sine)));
  if (!squared.supported) return result;
  squared.low = std::max(0., squared.low);
  const auto norm = transfer_small_root(squared);
  if (!norm.supported) return result;
  result.speed_radians_per_second = bounds(norm);
  result.supported = true;
  result.certified = squared.high <= square(speed_threshold()).low;
  return result;
}
} // namespace

auto detail::boarding_route_phase_yaw_numeric(BoardingRoutePhaseScalarJet input,
                                              double T, bool reverse)
    -> std::expected<BoardingRoutePhaseYawEvidence, std::string> {
  const TimingScalar k{phase_numeric_interval(input.value, 1),
                       phase_numeric_interval(input.first, 4096),
                       phase_numeric_interval(input.second, 4096)};
  if (!timing_supported(k) || !std::isfinite(T) || T < 1 || T > 120)
    return std::unexpected(
        "Numeric phase yaw requires ordered finite bounds and T in[1,120]");
  BoardingRoutePhaseYawEvidence result;
  if (!supported_environment()) return result;
  const auto frame = phase_rotation(k, true);
  const auto angle = phase_angle(k);
  for (std::size_t i = 0; i < 3; ++i)
    if (!phase_evidence(frame[i], T, reverse, result.frame.columns[i], false))
      return result;
  if (!angle.first.supported || !angle.second.supported) return result;
  result.angle = phase_angular(angle, T, reverse);
  result.arithmetic_supported = true;
  return result;
}
auto detail::boarding_route_phase_hip_sector_numeric(Bounds F, Bounds G)
    -> std::expected<BoardingRoutePhaseHipSectorEvidence, std::string> {
  const auto f = phase_numeric_interval(F, 8), g = phase_numeric_interval(G, 8);
  if (!f.supported || !g.supported)
    return std::unexpected(
        "Numeric hip sector requires ordered finite components abs<=8");
  return phase_hip_sector(f, g);
}
auto detail::boarding_route_phase_hip_speed_numeric(Bounds axial,
                                                    Bounds roll_rate,
                                                    Bounds pitch, Bounds sine)
    -> std::expected<BoardingPlantedLegSpeedEvidence, std::string> {
  const auto a = phase_numeric_interval(axial, 4096),
             r = phase_numeric_interval(roll_rate, 4096),
             p = phase_numeric_interval(pitch, 4096),
             s = phase_numeric_interval(sine, 1);
  if (!a.supported || !r.supported || !p.supported || !s.supported)
    return std::unexpected("Numeric hip speed requires ordered finite rates "
                           "abs<=4096 and sine in[-1,1]");
  return phase_hip_speed(a, r, p, s);
}
} // namespace apsis_drift

#include "origin_boarding_route_port_unload_internal.hpp"
namespace apsis_drift {
namespace {
using UnloadCell = BoardingRoutePortUnloadCell;
using UnloadReason = BoardingRoutePortUnloadRefusal;
using UnloadWhy = BoardingRoutePortUnloadCondition;
using UnloadState = detail::BoardingRoutePortUnloadCellResult;
using UnloadLimits = detail::BoardingRoutePortUnloadLimits;
using UnloadWork = BoardingRoutePortUnloadCounters;
struct UnloadSolid {
  EndpointSelfSolid shape;
  const BoardingRoutePhaseFrameEvidence* frame{};
};
static_assert(sizeof(UnloadCell) <= kBoardingRoutePortUnloadMaximumCellBytes);
static_assert(sizeof(UnloadCell) + sizeof(PhaseGraph) + sizeof(PhaseRequest) +
                  sizeof(detail::BoardingRoutePortUnloadContext) +
                  11 * std::size_t{24} + std::size_t{12288} +
                  std::size_t{8192} + std::size_t{512} <=
              kBoardingRoutePortUnloadMaximumScratchBytes);
auto unload_refuse(UnloadReason& r, UnloadWhy why, Interval limit = {})
    -> UnloadState {
  r.condition = why;
  r.limiting_bound = limit.supported ? bounds(limit) : Bounds{};
  return why == UnloadWhy::unsupported_arithmetic ||
                 why == UnloadWhy::invalid_binding
             ? UnloadState::unsupported
             : UnloadState::unresolved;
}
auto unload_charge(std::uint64_t& count, std::uint64_t cap, UnloadWhy why,
                   UnloadReason& r) -> bool {
  if (count >= cap) {
    r.condition = why;
    return false;
  }
  ++count;
  return true;
}
auto unload_difference(const Point& a, const Point& b) -> Point {
  Point p;
  for (std::size_t i = 0; i < 3; ++i)
    p[i] = subtract(a[i], b[i]);
  return p;
}
auto unload_scale(Point p, double scale) -> Point {
  for (auto& v : p)
    v = divide(v, point(scale));
  return p;
}
auto unload_frame_dot(const BoardingRoutePhaseFrameEvidence& f, std::size_t i,
                      const Point& u) -> Interval {
  const auto c = body_intervals(f.columns[i].value);
  return add(add(multiply(c[0], u[0]), multiply(c[1], u[1])),
             multiply(c[2], u[2]));
}
auto unload_solid(const BoardingRoutePhasePartBinding& binding,
                  const std::array<Point, kBoardingPlantedBodyPointCount>& p,
                  const BoardingRouteFootPhaseCell& cell) -> UnloadSolid {
  UnloadSolid out;
  if (const auto* box =
          std::get_if<BoardingRoutePhaseBoxBinding>(&binding.reservation)) {
    out.shape.shape =
        binding.id == PartId::trunk || binding.id == PartId::helmet
            ? SelfShape::ellipsoid
            : SelfShape::box;
    out.shape.first = out.shape.second = p[body_index(box->center)];
    out.shape.half = box->half_size_metres;
    out.frame = &cell.frames[static_cast<std::size_t>(box->frame)];
  } else {
    const auto& c =
        std::get<BoardingPlantedBodyCapsuleBinding>(binding.reservation);
    out.shape.shape = SelfShape::capsule;
    out.shape.first = p[body_index(c.start)];
    out.shape.second = p[body_index(c.end)];
    out.shape.radius = c.radius_metres;
  }
  return out;
}
auto unload_support(const UnloadSolid& solid, Vec n) -> Interval {
  const auto& s = solid.shape;
  if (!self_point_supported(s.first)) return failed();
  const auto center = self_dot(s.first, n);
  if (s.shape == SelfShape::capsule)
    return add(self_maximum(center, self_dot(s.second, n)),
               multiply(point(s.radius),
                        transfer_small_root(
                            add(add(square(point(n.x)), square(point(n.y))),
                                square(point(n.z))))));
  if (!solid.frame) return failed();
  const std::array half{s.half.x, s.half.y, s.half.z};
  auto extent = point(0);
  for (std::size_t j = 0; j < 3; ++j) {
    const auto column = body_intervals(solid.frame->columns[j].value);
    const auto term = multiply(point(half[j]), absolute(self_dot(column, n)));
    extent = add(extent, s.shape == SelfShape::box ? term : square(term));
  }
  if (s.shape == SelfShape::ellipsoid) extent = transfer_small_root(extent);
  return add(center, extent);
}
[[gnu::noinline]] auto unload_owner(
    const BoardingRouteFootPhaseCell& cell,
    const std::array<Point, kBoardingPlantedBodyPointCount>& p,
    const SelfRegion& r, BoardingLowerFootTransferOwner& out) -> void {
  const auto second = static_cast<std::size_t>(r.second),
             side = second >= 9 ? std::size_t{1} : std::size_t{0};
  const auto base = body_index(side == 0 ? BodyPointId::port_hip
                                         : BodyPointId::starboard_hip);
  auto extent = point(0), limit = point(r.limit_metres), secondary = point(0);
  auto kind = SelfCert::none;
  switch (r.junction) {
    case Junction::waist:
      extent = point(.12);
      kind = SelfCert::cap_partner_support;
      break;
    case Junction::neck:
      extent = limit = point(.2695);
      kind = SelfCert::cap_partner_support;
      break;
    case Junction::hip: {
      const auto u =
          unload_scale(unload_difference(p[base + 1], p[base]), thigh_length);
      const auto x = unload_frame_dot(cell.frames[0], 0, u),
                 y = unload_frame_dot(cell.frames[0], 1, u),
                 z = unload_frame_dot(cell.frames[0], 2, u);
      extent = add(add(subtract(multiply(point(.24), absolute(x)),
                                multiply(point(side == 0 ? -.14 : .14), x)),
                       multiply(point(.12), absolute(y))),
                   multiply(point(.18), absolute(z)));
      kind = SelfCert::original_axis_box_support;
      break;
    }
    case Junction::ankle: {
      if (.075 > r.limit_metres || r.limit_metres >= shin_length) return;
      const auto u = unload_scale(unload_difference(p[base + 1], p[base + 2]),
                                  shin_length);
      const auto x = unload_frame_dot(cell.frames[side + 2], 0, u), y = u[1],
                 z = unload_frame_dot(cell.frames[side + 2], 2, u);
      if (!y.supported) return;
      if (y.low < 0) {
        out.arithmetic_supported = true;
        return;
      }
      extent = add(multiply(point(.06), absolute(x)),
                   multiply(point(.14), absolute(z)));
      kind = SelfCert::original_axis_box_support;
      break;
    }
    case Junction::wrist:
      if (.055 > r.limit_metres || r.limit_metres >= .386) return;
      extent = point(.05);
      kind = SelfCert::original_axis_box_support;
      break;
    case Junction::knee:
    case Junction::elbow: {
      const auto l1 = point(thigh_length), l2 = point(shin_length);
      const auto cosine =
          r.junction == Junction::knee
              ? negate(divide(
                    subtract(
                        subtract(
                            interval(cell.legs[side].distance_squared.lower,
                                     cell.legs[side].distance_squared.upper),
                            square(l1)),
                        square(l2)),
                    multiply(point(2), multiply(l1, l2))))
              : transfer_small_root(point(.5));
      const auto a = r.junction == Junction::knee ? .105 : .065,
                 b = r.junction == Junction::knee ? .075 : .055;
      const auto proof = self_half_ray(a, b, r.limit_metres, cosine);
      if (!proof.arithmetic_supported) return;
      extent = square(point(std::max(a, b)));
      limit = multiply(
          square(point(r.limit_metres)),
          interval(proof.sine_squared.lower, proof.sine_squared.upper));
      kind = SelfCert::half_ray_angle_bound;
      break;
    }
    case Junction::shoulder: {
      if (.065 > r.limit_metres) return;
      const auto c = transfer_small_root(point(.5)),
                 threshold = transfer_small_root(subtract(
                     square(point(r.limit_metres)), square(point(.065))));
      extent = add(negate(multiply(threshold, c)), multiply(point(.065), c));
      secondary = add(negate(multiply(point(.35898), c)), point(.065));
      limit = point(-.18);
      kind = SelfCert::shoulder_split;
      break;
    }
  }
  const auto gap = subtract(limit, extent);
  out.arithmetic_supported = extent.supported && limit.supported &&
                             secondary.supported && gap.supported;
  if (!out.arithmetic_supported) return;
  out.extent = bounds(extent);
  out.limit = bounds(limit);
  out.secondary_extent = bounds(secondary);
  out.structural_identity = true;
  out.certificate = kind;
  out.certified = r.junction == Junction::shoulder
                      ? extent.high < limit.low && secondary.high < limit.low
                      : gap.low >= 0;
}
[[gnu::noinline]] auto unload_separate(const UnloadSolid& a,
                                       const UnloadSolid& b,
                                       const UnloadLimits& caps,
                                       UnloadWork& work, UnloadReason& reason)
    -> UnloadState {
  const auto count = std::size_t{4} +
                     (a.shape.shape == SelfShape::capsule ? 2 : 3) +
                     (b.shape.shape == SelfShape::capsule ? 2 : 3);
  const auto proposal = [&](std::size_t i) -> Vec {
    if (i < 3)
      return i == 0 ? Vec{1, 0, 0} : i == 1 ? Vec{0, 1, 0} : Vec{0, 0, 1};
    if (i == 3)
      return self_difference(self_reporting(self_center(b.shape)),
                             self_reporting(self_center(a.shape)));
    i -= 4;
    for (const auto* s : {&a, &b})
      if (s->shape.shape != SelfShape::capsule) {
        if (i < 3)
          return s->frame ? self_reporting(
                                body_intervals(s->frame->columns[i].value))
                          : Vec{};
        i -= 3;
      }
    for (const auto* s : {&a, &b})
      if (s->shape.shape == SelfShape::capsule) {
        if (i < 2) {
          const auto& other = s == &a ? b : a;
          return self_difference(
              self_reporting(i == 0 ? s->shape.first : s->shape.second),
              self_reporting(self_center(other.shape)));
        }
        i -= 2;
      }
    return {};
  };
  for (std::size_t i = 0; i < count; ++i) {
    if (!unload_charge(work.contact.proposed_axes, caps.axes,
                       UnloadWhy::axis_capacity, reason))
      return UnloadState::capacity;
    const auto axis = proposal(i);
    if (!self_direction(axis)) continue;
    for (const auto n : {axis, self_negate(axis)}) {
      if (!unload_charge(work.contact.signed_trials, caps.signed_trials,
                         UnloadWhy::signed_trial_capacity, reason))
        return UnloadState::capacity;
      const auto total =
          add(unload_support(a, n), unload_support(b, self_negate(n)));
      if (!total.supported)
        return unload_refuse(reason, UnloadWhy::unsupported_arithmetic);
      if (total.high <= 0) return UnloadState::accepted;
    }
  }
  return unload_refuse(reason, UnloadWhy::unresolved_self_pair);
}
[[gnu::noinline]] auto unload_self(const UnloadLimits& caps, UnloadWork& work,
                                   UnloadCell& out, UnloadReason& reason)
    -> UnloadState {
  static const auto parts = detail::boarding_route_foot_phase_parts();
  static const auto regions = self_regions();
  std::array<Point, kBoardingPlantedBodyPointCount> relative;
  const auto root = body_intervals(out.phase.points[0].value);
  for (std::size_t i = 0; i < relative.size(); ++i) {
    relative[i] = i == 0 ? Point{point(0), point(0), point(0)}
                         : unload_difference(
                               body_intervals(out.phase.points[i].value), root);
    if (!self_point_supported(relative[i]))
      return unload_refuse(reason, UnloadWhy::unsupported_arithmetic);
  }
  std::size_t index{};
  for (std::size_t a = 0; a < parts.size(); ++a)
    for (std::size_t b = a + 1; b < parts.size(); ++b, ++index) {
      reason.pair = index;
      if (!unload_charge(work.contact.self_pairs, caps.pairs,
                         UnloadWhy::pair_capacity, reason))
        return UnloadState::capacity;
      auto certificate = SelfCert::none;
      for (std::size_t r = 0; r < regions.size(); ++r)
        if (static_cast<std::size_t>(regions[r].first) == a &&
            static_cast<std::size_t>(regions[r].second) == b) {
          if (!unload_charge(work.ownership_attempts, caps.owners,
                             UnloadWhy::ownership_capacity, reason))
            return UnloadState::capacity;
          unload_owner(out.phase, relative, regions[r], out.owners[r]);
          if (!out.owners[r].arithmetic_supported)
            return unload_refuse(reason, UnloadWhy::unsupported_arithmetic);
          if (out.owners[r].certified) certificate = out.owners[r].certificate;
          break;
        }
      if (certificate == SelfCert::none) {
        const auto first = unload_solid(parts[a], relative, out.phase),
                   second = unload_solid(parts[b], relative, out.phase);
        const auto result = unload_separate(first, second, caps, work, reason);
        if (result != UnloadState::accepted) return result;
        certificate = SelfCert::convex_support_plane;
      }
      out.pair_certificates[index] = certificate;
      ++out.certificate_counts[static_cast<std::size_t>(certificate)];
    }
  out.self_complete = index == kBoardingBodyPairCount;
  reason.pair.reset();
  return UnloadState::accepted;
}
} // namespace
auto detail::boarding_route_port_unload_cell(
    const BoardingRoutePortUnloadContext& context, double first, double last,
    bool reverse, const UnloadLimits& caps, UnloadWork& work, UnloadCell& out,
    UnloadReason& reason) -> UnloadState {
  out = {};
  reason = {};
  out.phase.first = first;
  out.phase.last = last;
  const UnloadLimits original;
  if (!std::isfinite(first) || !std::isfinite(last) || first < 0 || last > 1 ||
      first > last || caps.pairs > original.pairs ||
      caps.axes > original.axes ||
      caps.signed_trials > original.signed_trials ||
      caps.owners > original.owners || caps.candidates > original.candidates ||
      caps.edges > original.edges ||
      caps.initial_source_partitions > original.initial_source_partitions ||
      caps.initial_body_records > original.initial_body_records ||
      caps.initial_self_pairs > original.initial_self_pairs ||
      caps.initial_self_axes > original.initial_self_axes ||
      caps.initial_pressure_partitions > original.initial_pressure_partitions ||
      caps.output_bytes > original.output_bytes || !context.source_.contact() ||
      context.source_.selected_partitions().size() != 10)
    return unload_refuse(reason, UnloadWhy::invalid_binding);
  const std::array incoming{
      work.contact.self_pairs,          work.contact.proposed_axes,
      work.contact.signed_trials,       work.ownership_attempts,
      work.contact.pressure_candidates, work.contact.disk_edges};
  const std::array ceilings{caps.pairs,  caps.axes,       caps.signed_trials,
                            caps.owners, caps.candidates, caps.edges};
  const std::array failures{
      UnloadWhy::pair_capacity,         UnloadWhy::axis_capacity,
      UnloadWhy::signed_trial_capacity, UnloadWhy::ownership_capacity,
      UnloadWhy::pressure_capacity,     UnloadWhy::edge_capacity};
  for (std::size_t i = 0; i < incoming.size(); ++i)
    if (incoming[i] > ceilings[i]) {
      reason.condition = failures[i];
      return UnloadState::capacity;
    }
  const auto request = boarding_route_port_unload_controls();
  BoardingRouteFootPhaseRefusal phase_reason;
  auto state =
      boarding_route_foot_phase_cell(request, first, last, reverse, caps.phase,
                                     work.phase, out.phase, phase_reason);
  if (state != UnloadState::accepted) {
    reason.condition = UnloadWhy::phase_predicate;
    reason.phase_condition = phase_reason.condition;
    reason.side = phase_reason.side;
    reason.limiting_bound = phase_reason.limiting_bound;
    return state;
  }
  out.arithmetic_supported = true;
  state = unload_self(caps, work, out, reason);
  if (state != UnloadState::accepted) {
    if (state == UnloadState::unsupported) out.arithmetic_supported = false;
    return state;
  }
  const BoardingRoutePortUnloadCellToken token(context, out);
  state = boarding_route_port_unload_pressure(token, caps, work, out, reason);
  if (state != UnloadState::accepted) {
    if (state == UnloadState::unsupported) out.arithmetic_supported = false;
    return state;
  }
  out.complete =
      out.phase.complete && out.self_complete && out.nonnegative_reactions &&
      out.nominal_vertical_equilibrium_complete && out.finite_pressure_complete;
  reason = {};
  return out.complete ? UnloadState::accepted
                      : unload_refuse(reason, UnloadWhy::incomplete_cover);
}
} // namespace apsis_drift

namespace apsis_drift {
auto detail::boarding_route_port_unload_support_math(
    const BoardingRoutePortUnloadNumericSolid& input, Vec direction)
    -> std::expected<BoardingRoutePortUnloadSupportMath, std::string> {
  const auto convert = [](BoardingPlantedLegPointBounds b,
                          double maximum) -> Point {
    return {phase_numeric_interval({b.lower.x, b.upper.x}, maximum),
            phase_numeric_interval({b.lower.y, b.upper.y}, maximum),
            phase_numeric_interval({b.lower.z, b.upper.z}, maximum)};
  };
  if (input.shape != SelfShape::box && input.shape != SelfShape::ellipsoid &&
      input.shape != SelfShape::capsule)
    return std::unexpected("Numeric unload solid shape is invalid");
  UnloadSolid solid;
  solid.shape.shape = input.shape;
  solid.shape.first = convert(input.first, 8);
  solid.shape.second = convert(input.second, 8);
  solid.shape.half = input.half_size_metres;
  solid.shape.radius = input.radius_metres;
  if (!self_point_supported(solid.shape.first) ||
      !self_point_supported(solid.shape.second) || !self_direction(direction))
    return std::unexpected("Numeric unload support requires ordered finite "
                           "points and nonzero bounded direction");
  BoardingRoutePhaseFrameEvidence frame;
  for (std::size_t i = 0; i < 3; ++i) {
    if (!self_point_supported(convert(input.columns[i], 1)))
      return std::unexpected(
          "Numeric unload columns require ordered bounds in[-1,1]");
    frame.columns[i].value = input.columns[i];
  }
  if (input.shape == SelfShape::capsule) {
    if (!bounded(input.radius_metres, 8) || input.radius_metres <= 0)
      return std::unexpected(
          "Numeric unload radius must be finite positive abs<=8");
  } else
    for (const auto half : {input.half_size_metres.x, input.half_size_metres.y,
                            input.half_size_metres.z})
      if (!bounded(half, 8) || half <= 0)
        return std::unexpected(
            "Numeric unload halves must be finite positive abs<=8");
  if (!bounded(input.radius_metres, 8) || input.radius_metres < 0 ||
      !bounded(input.half_size_metres.x, 8) ||
      !bounded(input.half_size_metres.y, 8) ||
      !bounded(input.half_size_metres.z, 8))
    return std::unexpected(
        "Numeric unload dimensions must be finite bounded values");
  BoardingRoutePortUnloadSupportMath out;
  if (!supported_environment()) return out;
  solid.frame = &frame;
  const auto support = unload_support(solid, direction);
  if (support.supported) {
    out.support = bounds(support);
    out.arithmetic_supported = true;
  }
  return out;
}
} // namespace apsis_drift

#include "origin_boarding_route_checkpoint_unload_internal.hpp"
namespace apsis_drift {
static_assert(sizeof(BoardingRouteCheckpointUnloadCell) + sizeof(PhaseGraph) +
                  sizeof(std::optional<PhaseRequest>) +
                  sizeof(detail::BoardingRouteCheckpointUnloadContext) +
                  11 * std::size_t{24} + std::size_t{12288} +
                  sizeof(std::expected<BoardingRouteCheckpointUnloadDiagnostic,
                                       std::string>) +
                  std::size_t{8192} + std::size_t{512} <=
              kBoardingRouteCheckpointUnloadMaximumScratchBytes);
auto detail::boarding_route_checkpoint_unload_cell(
    const BoardingRoutePortUnloadContext& context, std::size_t phase_index,
    double first, double last, bool reverse, const UnloadLimits& caps,
    UnloadWork& work, UnloadCell& out, UnloadReason& reason) -> UnloadState {
  out = {};
  reason = {};
  out.phase.first = first;
  out.phase.last = last;
  const UnloadLimits original;
  if (phase_index > 2 || !std::isfinite(first) || !std::isfinite(last) ||
      first < 0 || last > 1 || first > last || caps.pairs > original.pairs ||
      caps.axes > original.axes ||
      caps.signed_trials > original.signed_trials ||
      caps.owners > original.owners || caps.candidates > original.candidates ||
      caps.edges > original.edges ||
      caps.initial_source_partitions > original.initial_source_partitions ||
      caps.initial_body_records > original.initial_body_records ||
      caps.initial_self_pairs > original.initial_self_pairs ||
      caps.initial_self_axes > original.initial_self_axes ||
      caps.initial_pressure_partitions > original.initial_pressure_partitions ||
      caps.output_bytes > original.output_bytes || !context.source_.contact() ||
      context.source_.selected_partitions().size() != 10)
    return unload_refuse(reason, UnloadWhy::invalid_binding);
  const std::array incoming{
      work.contact.self_pairs,          work.contact.proposed_axes,
      work.contact.signed_trials,       work.ownership_attempts,
      work.contact.pressure_candidates, work.contact.disk_edges};
  const std::array ceilings{caps.pairs,  caps.axes,       caps.signed_trials,
                            caps.owners, caps.candidates, caps.edges};
  const std::array failures{
      UnloadWhy::pair_capacity,         UnloadWhy::axis_capacity,
      UnloadWhy::signed_trial_capacity, UnloadWhy::ownership_capacity,
      UnloadWhy::pressure_capacity,     UnloadWhy::edge_capacity};
  for (std::size_t i = 0; i < incoming.size(); ++i)
    if (incoming[i] > ceilings[i]) {
      reason.condition = failures[i];
      return UnloadState::capacity;
    }
  const auto request = boarding_route_checkpoint_unload_controls(phase_index);
  if (!request) return unload_refuse(reason, UnloadWhy::invalid_binding);
  BoardingRouteFootPhaseRefusal phase_reason;
  auto state =
      boarding_route_foot_phase_cell(*request, first, last, reverse, caps.phase,
                                     work.phase, out.phase, phase_reason);
  if (state != UnloadState::accepted) {
    reason.condition = UnloadWhy::phase_predicate;
    reason.phase_condition = phase_reason.condition;
    reason.side = phase_reason.side;
    reason.limiting_bound = phase_reason.limiting_bound;
    return state;
  }
  out.arithmetic_supported = true;
  state = unload_self(caps, work, out, reason);
  if (state != UnloadState::accepted) {
    if (state == UnloadState::unsupported) out.arithmetic_supported = false;
    return state;
  }
  const BoardingRouteCheckpointUnloadCellToken token(context, out, phase_index);
  state =
      boarding_route_checkpoint_unload_pressure(token, caps, work, out, reason);
  if (state != UnloadState::accepted) {
    if (state == UnloadState::unsupported) out.arithmetic_supported = false;
    return state;
  }
  out.complete =
      out.phase.complete && out.self_complete && out.nonnegative_reactions &&
      out.nominal_vertical_equilibrium_complete && out.finite_pressure_complete;
  reason = {};
  return out.complete ? UnloadState::accepted
                      : unload_refuse(reason, UnloadWhy::incomplete_cover);
}
} // namespace apsis_drift
#include "origin_boarding_route_checkpoint_unload_self02_internal.hpp"
namespace apsis_drift {
namespace {
using Self02Cell = BoardingRouteCheckpointUnloadSelf02Cell;
using Self02Limits = detail::BoardingRouteCheckpointUnloadSelf02Limits;
using Self02Reason = detail::BoardingRouteCheckpointUnloadSelf02CellRefusal;
using Self02Certificate = BoardingRouteCheckpointUnloadSelf02Certificate;
using Self02HipMath = detail::BoardingRouteCheckpointUnloadSelf02HipMath;
// Two selected policies may retain both immutable part/region caches. The
// shared phase leg's original8192 nested budget plus both1184 caches/scalars
// and this value-only expression's temporaries fits the same12288 reserve.
static_assert(std::size_t{8192} +
                  2 * (sizeof(std::array<BoardingRoutePhasePartBinding,
                                         kBoardingBodyPartCount>) +
                       sizeof(SelfRegions)) +
                  std::size_t{144} + 8 * sizeof(Interval) + 2 * sizeof(Point) +
                  sizeof(Self02HipMath) <=
              12288);
static_assert(
    sizeof(Self02Cell) + sizeof(PhaseGraph) +
        sizeof(std::optional<PhaseRequest>) +
        sizeof(detail::BoardingRouteCheckpointUnloadContext) +
        11 * std::size_t{24} + std::size_t{12288} +
        sizeof(std::expected<BoardingRouteCheckpointUnloadSelf02Diagnostic,
                             std::string>) +
        std::size_t{8192} + std::size_t{512} <=
    kBoardingRouteCheckpointUnloadMaximumScratchBytes);

// Reset's large temporary is released before the phase graph is entered.
[[gnu::noinline]] auto self02_reset(Self02Cell& cell) -> void {
  cell.assessment = {};
  cell.self02_certificates = {};
  cell.self02_certificate_counts = {};
  cell.hip_complements = {};
}
auto self02_hip_expression(const Point& u, double radius, double axial_limit,
                           double bottom) -> Self02HipMath {
  Self02HipMath result;
  const auto transverse = transfer_small_root(add(square(u[0]), square(u[2])));
  const auto extent = add(multiply(point(axial_limit), u[1]),
                          multiply(point(radius), transverse));
  const auto limit = point(bottom), gap = subtract(limit, extent);
  result.arithmetic_supported = u[0].supported && u[1].supported &&
                                u[2].supported && transverse.supported &&
                                extent.supported && limit.supported &&
                                gap.supported;
  if (!result.arithmetic_supported) return result;
  result.transverse = bounds(transverse);
  result.extent = bounds(extent);
  result.limit = bounds(limit);
  result.strict_gap = bounds(gap);
  result.negative_axis = u[1].high < 0;
  result.strict_extent_below_bottom = extent.high < limit.low;
  return result;
}
[[gnu::noinline]] auto self02_hip_complement(
    const BoardingRouteFootPhaseCell& phase,
    const std::array<Point, kBoardingPlantedBodyPointCount>& relative,
    const SelfRegion& region, std::size_t region_index, std::size_t pair,
    BoardingRouteCheckpointUnloadHipComplement& result) -> void {
  result.attempted = true;
  result.region = region_index;
  result.pair = pair;
  const auto side = static_cast<std::size_t>(region.second) >= 9
                        ? std::size_t{1}
                        : std::size_t{0};
  result.side = side;
  result.slab_limit_metres = region.limit_metres;
  const auto hip =
      side == 0 ? BodyPointId::port_hip : BodyPointId::starboard_hip;
  const auto knee =
      side == 0 ? BodyPointId::port_knee : BodyPointId::starboard_knee;
  const auto thigh = side == 0 ? PartId::port_thigh : PartId::starboard_thigh;
  if (!phase.complete || !phase.nominal_links || !phase.derivative_domains ||
      !phase.legs[side].nominal_links || !phase.legs[side].derivative_domains ||
      region.junction != Junction::hip || region.first != PartId::pelvis ||
      region.second != thigh || region.root != hip || region.toward != knee ||
      region.limit_metres != kBoardingSelfHipLengthMetres ||
      .105 > region.limit_metres || region.limit_metres >= thigh_length)
    return;
  // Only the privately compiled upright, yaw-only phase graph reaches here.
  // H-ROOT=side*.14*Rroot.X and Rroot.Y=WORLDY give exact pelvis bottom -.12.
  result.nominal_unit_identity = result.upright_pelvis_identity =
      result.original_slab_identity = true;
  const auto u = unload_scale(
      unload_difference(relative[body_index(knee)], relative[body_index(hip)]),
      thigh_length);
  for (std::size_t i = 0; i < 3; ++i) {
    if (!u[i].supported) return;
    result.axis[i] = bounds(u[i]);
  }
  if (u[1].high >= 0) {
    result.arithmetic_supported = true;
    return;
  }
  result.extent_evaluated = true;
  const auto proof = self02_hip_expression(u, .105, region.limit_metres, -.12);
  result.arithmetic_supported = proof.arithmetic_supported;
  if (!proof.arithmetic_supported) return;
  result.transverse = proof.transverse;
  result.extent = proof.extent;
  result.limit = proof.limit;
  result.strict_gap = proof.strict_gap;
  result.certified = proof.negative_axis && proof.strict_extent_below_bottom;
}
[[gnu::noinline]] auto self02_self(const Self02Limits& caps, UnloadWork& work,
                                   std::uint64_t& hip_attempts,
                                   Self02Cell& cell, Self02Reason& reason)
    -> UnloadState {
  static const auto parts = detail::boarding_route_foot_phase_parts();
  static const auto regions = self_regions();
  auto& out = cell.assessment;
  std::array<Point, kBoardingPlantedBodyPointCount> relative;
  const auto root = body_intervals(out.phase.points[0].value);
  for (std::size_t i = 0; i < relative.size(); ++i) {
    relative[i] = i == 0 ? Point{point(0), point(0), point(0)}
                         : unload_difference(
                               body_intervals(out.phase.points[i].value), root);
    if (!self_point_supported(relative[i]))
      return unload_refuse(reason, UnloadWhy::unsupported_arithmetic);
  }
  std::size_t index{};
  for (std::size_t a = 0; a < parts.size(); ++a)
    for (std::size_t b = a + 1; b < parts.size(); ++b, ++index) {
      reason.pair = index;
      if (!unload_charge(work.contact.self_pairs, caps.pairs,
                         UnloadWhy::pair_capacity, reason))
        return UnloadState::capacity;
      auto original = SelfCert::none;
      auto selected = Self02Certificate::none;
      for (std::size_t r = 0; r < regions.size(); ++r)
        if (static_cast<std::size_t>(regions[r].first) == a &&
            static_cast<std::size_t>(regions[r].second) == b) {
          if (!unload_charge(work.ownership_attempts, caps.owners,
                             UnloadWhy::ownership_capacity, reason))
            return UnloadState::capacity;
          unload_owner(out.phase, relative, regions[r], out.owners[r]);
          if (!out.owners[r].arithmetic_supported)
            return unload_refuse(reason, UnloadWhy::unsupported_arithmetic);
          if (out.owners[r].certified) {
            original = out.owners[r].certificate;
            selected = static_cast<Self02Certificate>(original);
          } else if (regions[r].junction == Junction::hip) {
            if (hip_attempts >= caps.hip_complement_attempts) {
              reason.condition = UnloadWhy::ownership_capacity;
              reason.hip_complement_capacity = true;
              return UnloadState::capacity;
            }
            ++hip_attempts;
            const auto side = b >= 9 ? std::size_t{1} : std::size_t{0};
            self02_hip_complement(out.phase, relative, regions[r], r, index,
                                  cell.hip_complements[side]);
            if (!cell.hip_complements[side].arithmetic_supported)
              return unload_refuse(reason, UnloadWhy::unsupported_arithmetic);
            if (cell.hip_complements[side].certified)
              selected = Self02Certificate::original_capsule_slab_complement;
          }
          break;
        }
      if (selected == Self02Certificate::none) {
        const auto first = unload_solid(parts[a], relative, out.phase),
                   second = unload_solid(parts[b], relative, out.phase);
        const auto state = unload_separate(first, second, caps, work, reason);
        if (state != UnloadState::accepted) return state;
        original = SelfCert::convex_support_plane;
        selected = Self02Certificate::convex_support_plane;
      }
      out.pair_certificates[index] = original;
      if (original != SelfCert::none)
        ++out.certificate_counts[static_cast<std::size_t>(original)];
      cell.self02_certificates[index] = selected;
      ++cell.self02_certificate_counts[static_cast<std::size_t>(selected)];
    }
  out.self_complete = index == kBoardingBodyPairCount;
  reason.pair.reset();
  return UnloadState::accepted;
}
} // namespace
auto detail::boarding_route_checkpoint_unload_self02_cell(
    const BoardingRoutePortUnloadContext& context, std::size_t phase_index,
    double first, double last, bool reverse, const Self02Limits& caps,
    UnloadWork& work, std::uint64_t& hip_attempts, Self02Cell& cell,
    Self02Reason& reason) -> UnloadState {
  self02_reset(cell);
  reason = {};
  auto& out = cell.assessment;
  out.phase.first = first;
  out.phase.last = last;
  const UnloadLimits original;
  if (phase_index > 2 || !std::isfinite(first) || !std::isfinite(last) ||
      first < 0 || last > 1 || first > last ||
      caps.hip_complement_attempts >
          kBoardingRouteCheckpointUnloadSelf02MaximumHipAttempts ||
      caps.pairs > original.pairs || caps.axes > original.axes ||
      caps.signed_trials > original.signed_trials ||
      caps.owners > original.owners || caps.candidates > original.candidates ||
      caps.edges > original.edges ||
      caps.initial_source_partitions > original.initial_source_partitions ||
      caps.initial_body_records > original.initial_body_records ||
      caps.initial_self_pairs > original.initial_self_pairs ||
      caps.initial_self_axes > original.initial_self_axes ||
      caps.initial_pressure_partitions > original.initial_pressure_partitions ||
      caps.output_bytes > original.output_bytes || !context.source_.contact() ||
      context.source_.selected_partitions().size() != 10)
    return unload_refuse(reason, UnloadWhy::invalid_binding);
  if (hip_attempts > caps.hip_complement_attempts) {
    reason.condition = UnloadWhy::ownership_capacity;
    reason.hip_complement_capacity = true;
    return UnloadState::capacity;
  }
  const std::array incoming{
      work.contact.self_pairs,          work.contact.proposed_axes,
      work.contact.signed_trials,       work.ownership_attempts,
      work.contact.pressure_candidates, work.contact.disk_edges};
  const std::array ceilings{caps.pairs,  caps.axes,       caps.signed_trials,
                            caps.owners, caps.candidates, caps.edges};
  const std::array failures{
      UnloadWhy::pair_capacity,         UnloadWhy::axis_capacity,
      UnloadWhy::signed_trial_capacity, UnloadWhy::ownership_capacity,
      UnloadWhy::pressure_capacity,     UnloadWhy::edge_capacity};
  for (std::size_t i = 0; i < incoming.size(); ++i)
    if (incoming[i] > ceilings[i]) {
      reason.condition = failures[i];
      return UnloadState::capacity;
    }
  const auto request = boarding_route_checkpoint_unload_controls(phase_index);
  if (!request) return unload_refuse(reason, UnloadWhy::invalid_binding);
  BoardingRouteFootPhaseRefusal phase_reason;
  auto state =
      boarding_route_foot_phase_cell(*request, first, last, reverse, caps.phase,
                                     work.phase, out.phase, phase_reason);
  if (state != UnloadState::accepted) {
    reason.condition = UnloadWhy::phase_predicate;
    reason.phase_condition = phase_reason.condition;
    reason.side = phase_reason.side;
    reason.limiting_bound = phase_reason.limiting_bound;
    return state;
  }
  out.arithmetic_supported = true;
  state = self02_self(caps, work, hip_attempts, cell, reason);
  if (state != UnloadState::accepted) {
    if (state == UnloadState::unsupported) out.arithmetic_supported = false;
    return state;
  }
  const BoardingRouteCheckpointUnloadCellToken token(context, out, phase_index);
  state =
      boarding_route_checkpoint_unload_pressure(token, caps, work, out, reason);
  if (state != UnloadState::accepted) {
    if (state == UnloadState::unsupported) out.arithmetic_supported = false;
    return state;
  }
  out.complete =
      out.phase.complete && out.self_complete && out.nonnegative_reactions &&
      out.nominal_vertical_equilibrium_complete && out.finite_pressure_complete;
  reason = {};
  return out.complete ? UnloadState::accepted
                      : unload_refuse(reason, UnloadWhy::incomplete_cover);
}
auto detail::boarding_route_checkpoint_unload_self02_hip_math(
    std::array<Bounds, 3> axis, double radius, double axial_limit,
    double bottom) -> std::expected<Self02HipMath, std::string> {
  if (!std::isfinite(radius) || !std::isfinite(axial_limit) ||
      !std::isfinite(bottom) || radius <= 0 || radius > axial_limit ||
      axial_limit > 8 || std::abs(bottom) > 8)
    return std::unexpected("Hip expression requires positive radius<=limit<=8 "
                           "and finite bottom abs<=8");
  Point u;
  for (std::size_t i = 0; i < 3; ++i) {
    if (!std::isfinite(axis[i].lower) || !std::isfinite(axis[i].upper) ||
        axis[i].lower > axis[i].upper || axis[i].lower < -1 ||
        axis[i].upper > 1)
      return std::unexpected("Hip expression requires finite ordered raw axis "
                             "components in[-1,1]");
    u[i] = interval(axis[i].lower, axis[i].upper);
  }
  if (!boarding_route_foot_phase_environment()) return Self02HipMath{};
  return self02_hip_expression(u, radius, axial_limit, bottom);
}
} // namespace apsis_drift

#include "origin_boarding_route_checkpoint_world_material_internal.hpp"
namespace apsis_drift {
namespace {
using WorldProxy = detail::BoardingRouteCheckpointWorldProxy;
using WorldProxyMath = detail::BoardingRouteCheckpointWorldProxyMath;
using WorldShape = detail::BoardingSourceEndpointSurfaceCheckpointShape;
// Staged proxy math owns no graph/cell/source copies. This conservative source
// ledger includes numeric-fixture frame storage and the genuine support
// kernel's bounded three-term residual scratch; the controller adds its outer
// live state.
static_assert(sizeof(WorldProxyMath) + sizeof(WorldProxy) +
                  sizeof(UnloadSolid) +
                  sizeof(BoardingRoutePhaseFrameEvidence) + 8 * sizeof(Point) +
                  24 * sizeof(Interval) + sizeof(SelfSquareScratch) +
                  16 * sizeof(double) +
                  sizeof(std::expected<WorldProxy, std::string>) <=
              4096);
// h(n) encloses a support MAXIMUM. A projection minimum is -h(-n), never
// h(n).lower. Keep the original full BOX for WORLD, including trunk/helmet.
[[gnu::noinline]] auto world_proxy_math(const UnloadSolid& input)
    -> WorldProxyMath {
  WorldProxyMath out;
  out.solid.first = point_bounds(input.shape.first);
  out.solid.second = point_bounds(input.shape.second);
  out.solid.radius_metres = input.shape.radius;
  if (input.shape.shape == SelfShape::box) {
    out.solid.shape = WorldShape::box;
    if (!input.frame) return out;
    const std::array half{input.shape.half.x, input.shape.half.y,
                          input.shape.half.z};
    std::array<double, 3> enclosing{};
    for (std::size_t i = 0; i < 3; ++i) {
      auto extent = point(0);
      for (std::size_t j = 0; j < 3; ++j) {
        const auto column = body_intervals(input.frame->columns[j].value);
        if (!self_point_supported(column)) return out;
        extent = add(extent, multiply(point(half[j]), absolute(column[i])));
      }
      if (!extent.supported || extent.high <= 0 || extent.high > 8) return out;
      enclosing[i] = extent.high;
    }
    out.solid.half_size_metres = {enclosing[0], enclosing[1], enclosing[2]};
  } else if (input.shape.shape == SelfShape::capsule) {
    out.solid.shape = WorldShape::capsule;
  } else
    return out;
  std::array<double, 3> lower{}, upper{};
  for (std::size_t i = 0; i < 3; ++i) {
    Vec axis{};
    if (i == 0)
      axis.x = 1;
    else if (i == 1)
      axis.y = 1;
    else
      axis.z = 1;
    const auto positive = unload_support(input, axis);
    const auto negative = unload_support(input, self_negate(axis));
    if (!positive.supported || !negative.supported) return out;
    lower[i] = -negative.high;
    upper[i] = positive.high;
  }
  out.bounds = {{lower[0], lower[1], lower[2]}, {upper[0], upper[1], upper[2]}};
  out.arithmetic_supported = self_point_supported(body_intervals(out.bounds));
  return out;
}
auto world_zero_derivatives(const BoardingPlantedBodyPointEvidence& p) -> bool {
  for (const auto& b : {p.derivatives.velocity, p.derivatives.acceleration})
    if (b.lower.x != 0 || b.upper.x != 0 || b.lower.y != 0 || b.upper.y != 0 ||
        b.lower.z != 0 || b.upper.z != 0)
      return false;
  return true;
}
auto world_stationary_sole(
    const BoardingRouteCheckpointUnloadSelf02Diagnostic& child,
    const BoardingRouteFootPhaseCell& phase, std::size_t side,
    const BoardingRoutePhaseBoxBinding& box, double& plane) -> bool {
  const auto& original = child.initial->self.endpoint;
  const auto& leg = original.legs[side];
  plane = leg.plane_metres;
  if (!original.complete || !original.plane_identities || !leg.plane_identity ||
      original.common_translation_y_metres != .847 ||
      leg.boot_center_y_terms != std::array{plane, .05, -.847} ||
      box.half_size_metres.x != .06 || box.half_size_metres.y != .05 ||
      box.half_size_metres.z != .14 ||
      box.frame != (side == 0 ? BoardingRoutePhaseFrame::port_sole
                              : BoardingRoutePhaseFrame::starboard_sole) ||
      box.center != (side == 0 ? BodyPointId::port_boot_center
                               : BodyPointId::starboard_boot_center) ||
      !phase.legs[side].target_sole_identity)
    return false;
  // The genuine issuer authenticated all three exact fixed foot controls and
  // all15 bindings ONCE. Retain the original expression and current-cell
  // target/frame/zero-jet guards here; never rerun the complete control
  // catalog.
  if (!world_zero_derivatives(phase.points[body_index(box.center)]))
    return false;
  const auto& frame = phase.frames[static_cast<std::size_t>(box.frame)];
  for (std::size_t j = 0; j < 3; ++j) {
    if (!world_zero_derivatives(frame.columns[j])) return false;
    const auto column = body_intervals(frame.columns[j].value);
    for (std::size_t i = 0; i < 3; ++i) {
      const auto expected = i == j ? 1. : 0.;
      if (!column[i].supported || column[i].low > expected ||
          column[i].high < expected)
        return false;
    }
  }
  return true;
}
} // namespace
auto detail::boarding_route_checkpoint_world_frame_box_math(
    BoardingPlantedLegPointBounds center,
    std::array<BoardingPlantedLegPointBounds, 3> columns, Vec half)
    -> std::expected<WorldProxyMath, std::string> {
  const auto valid_point = [](const BoardingPlantedLegPointBounds& b) {
    const auto p = body_intervals(b);
    return std::ranges::all_of(p, [](Interval v) {
      return v.supported && v.low >= -8 && v.high <= 8;
    });
  };
  if (!valid_point(center))
    return std::unexpected(
        "WORLD box center requires finite ordered abs<=8 bounds");
  for (const auto& column : columns)
    if (!valid_point(column))
      return std::unexpected(
          "WORLD box columns require finite ordered abs<=8 bounds");
  for (const auto value : {half.x, half.y, half.z})
    if (!bounded(value, 8) || value <= 0)
      return std::unexpected(
          "WORLD box halves require finite positive abs<=8 values");
  if (!supported_environment()) return WorldProxyMath{};
  BoardingRoutePhaseFrameEvidence frame;
  for (std::size_t i = 0; i < 3; ++i)
    frame.columns[i].value = columns[i];
  UnloadSolid input;
  input.shape.shape = SelfShape::box;
  input.shape.first = input.shape.second = body_intervals(center);
  input.shape.half = half;
  input.frame = &frame;
  return world_proxy_math(input);
}
auto detail::boarding_route_checkpoint_world_proxy(
    const BoardingRouteCheckpointWorldContext& context, std::size_t part,
    std::size_t cell) -> std::expected<WorldProxy, std::string> {
  // Validate the owning report and its current child/source BEFORE reading any
  // borrowed pointer: report moves and child reset otherwise leave a stale
  // alias.
  if (!BoardingRouteCheckpointWorldMaterialAccess::valid(context) ||
      !context.child_->complete ||
      context.child_->cells.data() != context.cell_data_ ||
      context.child_->cells.size() != context.cell_count_ ||
      cell >= context.cell_count_ || part >= kBoardingBodyPartCount ||
      !context.child_->initial->self.endpoint.sites.source.contact())
    return std::unexpected(
        "WORLD proxy requires its genuine unmoved owned child");
  const auto& child = *context.child_;
  const auto& phase = context.cell_data_[cell].assessment.phase;
  if (!phase.complete || !context.cell_data_[cell].assessment.complete)
    return std::unexpected(
        "WORLD proxy requires a complete retained child cell");
  if (!supported_environment()) return WorldProxy{};
  const auto& binding = child.parts[part];
  UnloadSolid input;
  const auto* box =
      std::get_if<BoardingRoutePhaseBoxBinding>(&binding.reservation);
  if (box) {
    const auto center = body_index(box->center);
    const auto frame = static_cast<std::size_t>(box->frame);
    if (center >= phase.points.size() || frame >= phase.frames.size())
      return std::unexpected("WORLD box binding exceeds owned graph");
    input.shape.shape = SelfShape::box;
    input.shape.first = input.shape.second =
        body_intervals(phase.points[center].value);
    input.shape.half = box->half_size_metres;
    input.frame = &phase.frames[frame];
  } else {
    const auto& capsule =
        std::get<BoardingPlantedBodyCapsuleBinding>(binding.reservation);
    const auto first = body_index(capsule.start),
               last = body_index(capsule.end);
    if (first >= phase.points.size() || last >= phase.points.size())
      return std::unexpected("WORLD capsule binding exceeds owned graph");
    input.shape.shape = SelfShape::capsule;
    input.shape.first = body_intervals(phase.points[first].value);
    input.shape.second = body_intervals(phase.points[last].value);
    input.shape.radius = capsule.radius_metres;
  }
  const auto math = world_proxy_math(input);
  WorldProxy out;
  out.solid = math.solid;
  out.bounds = math.bounds;
  out.arithmetic_supported = math.arithmetic_supported;
  out.original_world_identity = math.arithmetic_supported;
  if (math.arithmetic_supported && box && (part == 5 || part == 11)) {
    const auto side = part == 5 ? std::size_t{0} : std::size_t{1};
    if (world_stationary_sole(child, phase, side, *box, out.sole_plane) &&
        out.bounds.lower.y <= out.sole_plane &&
        out.bounds.upper.y >= out.sole_plane) {
      // Original plane + .05 boot-center offset - .05 half-height: exact
      // expression identity, not rounded center subtraction or an epsilon.
      out.bounds.lower.y = out.sole_plane;
      out.sole_expression_identity = true;
    }
  }
  return out;
}
} // namespace apsis_drift

namespace apsis_drift {
namespace {
// Genuine support maxima use the original full oriented BOX/capsule before
// the collision proxy inflates it to an axis-aligned enclosing box. An
// enclosing box's support LOWER bound cannot witness an original body point.
[[gnu::noinline]] auto world_signed_z_math(const UnloadSolid& input)
    -> detail::BoardingRouteCheckpointWorldSignedZSupportMath {
  detail::BoardingRouteCheckpointWorldSignedZSupportMath out;
  const auto positive = unload_support(input, {0, 0, 1}),
             negative = unload_support(input, {0, 0, -1});
  if (!positive.supported || !negative.supported) return out;
  out.positive = {positive.low, positive.high, positive.supported};
  out.negative = {negative.low, negative.high, negative.supported};
  out.arithmetic_supported = true;
  return out;
}
static_assert(
    sizeof(detail::BoardingRouteCheckpointWorldSignedZSupportMath) +
        sizeof(detail::BoardingRouteCheckpointWorldSignedZSupport) +
        sizeof(UnloadSolid) + sizeof(BoardingRoutePhaseFrameEvidence) +
        sizeof(SelfSquareScratch) + 16 * sizeof(Interval) +
        32 * sizeof(double) +
        sizeof(std::expected<detail::BoardingRouteCheckpointWorldSignedZSupport,
                             std::string>) <=
    4096);
} // namespace
auto detail::boarding_route_checkpoint_world_frame_box_signed_z_support_math(
    BoardingPlantedLegPointBounds center,
    std::array<BoardingPlantedLegPointBounds, 3> columns, Vec half)
    -> std::expected<detail::BoardingRouteCheckpointWorldSignedZSupportMath,
                     std::string> {
  const auto valid_point = [](const BoardingPlantedLegPointBounds& b) {
    const auto p = body_intervals(b);
    return std::ranges::all_of(p, [](Interval v) {
      return v.supported && v.low >= -8 && v.high <= 8;
    });
  };
  if (!valid_point(center))
    return std::unexpected(
        "WORLD box center requires finite ordered abs<=8 bounds");
  for (const auto& column : columns)
    if (!valid_point(column))
      return std::unexpected(
          "WORLD box columns require finite ordered abs<=8 bounds");
  for (const auto value : {half.x, half.y, half.z})
    if (!bounded(value, 8) || value <= 0)
      return std::unexpected(
          "WORLD box halves require finite positive abs<=8 values");
  if (!supported_environment())
    return detail::BoardingRouteCheckpointWorldSignedZSupportMath{};
  BoardingRoutePhaseFrameEvidence frame;
  for (std::size_t i = 0; i < 3; ++i)
    frame.columns[i].value = columns[i];
  UnloadSolid input;
  input.shape.shape = SelfShape::box;
  input.shape.first = input.shape.second = body_intervals(center);
  input.shape.half = half;
  input.frame = &frame;
  return world_signed_z_math(input);
}
auto detail::boarding_route_checkpoint_world_signed_z_support(
    const BoardingRouteCheckpointWorldContext& context, std::size_t part,
    std::size_t cell)
    -> std::expected<detail::BoardingRouteCheckpointWorldSignedZSupport,
                     std::string> {
  // Validate the owning report and its current child/source BEFORE reading any
  // borrowed pointer: report moves and child reset otherwise leave a stale
  // alias.
  if (!BoardingRouteCheckpointWorldMaterialAccess::valid(context) ||
      !context.child_->complete ||
      context.child_->cells.data() != context.cell_data_ ||
      context.child_->cells.size() != context.cell_count_ ||
      cell >= context.cell_count_ || part >= kBoardingBodyPartCount ||
      !context.child_->initial->self.endpoint.sites.source.contact())
    return std::unexpected(
        "WORLD proxy requires its genuine unmoved owned child");
  const auto& child = *context.child_;
  const auto& phase = context.cell_data_[cell].assessment.phase;
  if (!phase.complete || !context.cell_data_[cell].assessment.complete)
    return std::unexpected(
        "WORLD proxy requires a complete retained child cell");
  if (!supported_environment())
    return detail::BoardingRouteCheckpointWorldSignedZSupport{};
  const auto& binding = child.parts[part];
  UnloadSolid input;
  const auto* box =
      std::get_if<BoardingRoutePhaseBoxBinding>(&binding.reservation);
  if (box) {
    const auto center = body_index(box->center);
    const auto frame = static_cast<std::size_t>(box->frame);
    if (center >= phase.points.size() || frame >= phase.frames.size())
      return std::unexpected("WORLD box binding exceeds owned graph");
    input.shape.shape = SelfShape::box;
    input.shape.first = input.shape.second =
        body_intervals(phase.points[center].value);
    input.shape.half = box->half_size_metres;
    input.frame = &phase.frames[frame];
  } else {
    const auto& capsule =
        std::get<BoardingPlantedBodyCapsuleBinding>(binding.reservation);
    const auto first = body_index(capsule.start),
               last = body_index(capsule.end);
    if (first >= phase.points.size() || last >= phase.points.size())
      return std::unexpected("WORLD capsule binding exceeds owned graph");
    input.shape.shape = SelfShape::capsule;
    input.shape.first = body_intervals(phase.points[first].value);
    input.shape.second = body_intervals(phase.points[last].value);
    input.shape.radius = capsule.radius_metres;
  }
  const auto math = world_signed_z_math(input);
  detail::BoardingRouteCheckpointWorldSignedZSupport out;
  out.positive = math.positive;
  out.negative = math.negative;
  out.arithmetic_supported = math.arithmetic_supported;
  out.original_world_identity = math.arithmetic_supported;
  return out;
}
} // namespace apsis_drift

#include "origin_boarding_route_intermediate_support_self01_internal.hpp"
namespace apsis_drift {
namespace {
using SSD = BoardingRouteIntermediateSupportSelf01Diagnostic;
using SSC = BoardingRouteIntermediateSupportSelf01Cell;
using SSR = BoardingRouteIntermediateSupportSelf01Refusal;
using SSW = BoardingRouteIntermediateSupportSelf01Condition;
using SSS = BoardingRouteIntermediateSupportSelf01State;
using SSL = detail::BoardingRouteIntermediateSupportSelf01Limits;
using SSStage = BoardingRouteIntermediateSupportSelf01SelfStage;
using SSCert = BoardingRouteIntermediateSupportSelf01Certificate;
auto ss_fail(SSD& d, SSR& r, SSW why) -> bool {
  if (r.condition == SSW::none) r.condition = r.predicate_condition = why;
  const bool cap =
      why == SSW::source_guard_capacity || why == SSW::self_body_capacity ||
      why == SSW::self_pair_capacity || why == SSW::self_axis_capacity ||
      why == SSW::self_signed_capacity || why == SSW::self_owner_capacity ||
      why == SSW::self_hip_capacity;
  if (cap || why == SSW::unsupported_arithmetic) {
    d.stop_condition = why;
    d.state = cap ? SSS::capacity : SSS::unsupported;
    if (!cap) d.arithmetic_supported = false;
  } else if (d.state != SSS::capacity && d.state != SSS::unsupported)
    d.state = SSS::unresolved;
  return false;
}
auto ss_charge(SSD& d, SSR& r, std::size_t& work, std::size_t cap, SSW why)
    -> bool {
  if (work >= cap) return ss_fail(d, r, why);
  ++work;
  return true;
}
auto ss_valid(Interval x, double domain) -> bool {
  return x.supported && std::isfinite(x.low) && std::isfinite(x.high) &&
         x.low <= x.high && std::abs(x.low) <= domain &&
         std::abs(x.high) <= domain;
}
auto ss_point_valid(const Point& x, double domain) -> bool {
  return std::ranges::all_of(
      x, [domain](Interval v) { return ss_valid(v, domain); });
}
// One literal row at a time; no second full body catalog or persistent cache.
auto ss_part(std::size_t i) -> BoardingRoutePhasePartBinding {
  using P = BodyPointId;
  using F = BoardingRoutePhaseFrame;
  const auto box = [i](P p, RigidVector3 half, F frame, std::uint32_t mass) {
    return BoardingRoutePhasePartBinding{
        static_cast<PartId>(i),
        BoardingRoutePhaseBoxBinding{p, half, frame},
        {p, p, mass}};
  };
  if (i == 0) return box(P::root, {.24, .12, .18}, F::root, 144);
  if (i == 1) return box(P::trunk_center, {.26, .2695, .18}, F::trunk, 540);
  if (i == 2) return box(P::helmet_center, {.16, .18, .18}, F::trunk, 96);
  const auto offset = (i - 3) % 6;
  const auto base =
      i < 9 ? body_index(P::port_hip) : body_index(P::starboard_hip);
  const auto p = [base](std::size_t n) { return static_cast<P>(base + n); };
  if (offset == 2)
    return box(p(3), {.06, .05, .14}, i < 9 ? F::port_sole : F::starboard_sole,
               12);
  if (offset == 5) return box(p(6), {.04, .05, .02}, F::trunk, 5);
  const auto first = offset < 2 ? p(offset) : p(offset + 1);
  const auto last = offset < 2 ? p(offset + 1) : p(offset + 2);
  const auto radius = offset == 0   ? .105
                      : offset == 1 ? .075
                      : offset == 3 ? .065
                                    : .055;
  const std::uint32_t mass = offset == 0   ? 120
                             : offset == 1 ? 48
                             : offset == 3 ? 15
                                           : 10;
  return {static_cast<PartId>(i),
          BoardingPlantedBodyCapsuleBinding{first, last, radius},
          {first, last, mass}};
}
auto ss_same_part(const BoardingRoutePhasePartBinding& a,
                  const BoardingRoutePhasePartBinding& b) -> bool {
  if (a.id != b.id || a.mass.first != b.mass.first ||
      a.mass.second != b.mass.second || a.mass.weight != b.mass.weight ||
      a.reservation.index() != b.reservation.index())
    return false;
  if (const auto* x =
          std::get_if<BoardingRoutePhaseBoxBinding>(&a.reservation)) {
    const auto& y = std::get<BoardingRoutePhaseBoxBinding>(b.reservation);
    return x->center == y.center && x->frame == y.frame &&
           x->half_size_metres == y.half_size_metres;
  }
  const auto& x = std::get<BoardingPlantedBodyCapsuleBinding>(a.reservation);
  const auto& y = std::get<BoardingPlantedBodyCapsuleBinding>(b.reservation);
  return x.start == y.start && x.end == y.end &&
         x.radius_metres == y.radius_metres;
}
auto ss_region(std::size_t i) -> SelfRegion {
  using P = BodyPointId;
  switch (i) {
    case 0:
      return {PartId::pelvis, PartId::trunk,   Junction::waist,
              P::root,        P::trunk_center, kBoardingSelfWaistLimitMetres};
    case 1:
      return {PartId::pelvis, PartId::port_thigh, Junction::hip,
              P::port_hip,    P::port_knee,       kBoardingSelfHipLengthMetres};
    case 2:
      return {PartId::pelvis,    PartId::starboard_thigh,
              Junction::hip,     P::starboard_hip,
              P::starboard_knee, kBoardingSelfHipLengthMetres};
    case 3:
      return {PartId::trunk,   PartId::helmet,   Junction::neck,
              P::trunk_center, P::helmet_center, .2695};
    case 4:
      return {PartId::trunk,      PartId::port_upper_arm,
              Junction::shoulder, P::port_shoulder,
              P::port_elbow,      kBoardingSelfShoulderRadiusMetres};
    case 5:
      return {PartId::trunk,      PartId::starboard_upper_arm,
              Junction::shoulder, P::starboard_shoulder,
              P::starboard_elbow, kBoardingSelfShoulderRadiusMetres};
    case 6:
      return {PartId::port_thigh, PartId::port_shin,
              Junction::knee,     P::port_knee,
              P::port_hip,        kBoardingSelfKneeRadiusMetres};
    case 7:
      return {PartId::port_shin, PartId::port_boot,
              Junction::ankle,   P::port_ankle,
              P::port_knee,      kBoardingSelfAnkleLengthMetres};
    case 8:
      return {PartId::port_upper_arm, PartId::port_forearm,
              Junction::elbow,        P::port_elbow,
              P::port_shoulder,       kBoardingSelfElbowRadiusMetres};
    case 9:
      return {PartId::port_forearm, PartId::port_hand,
              Junction::wrist,      P::port_wrist,
              P::port_elbow,        kBoardingSelfWristLengthMetres};
    case 10:
      return {PartId::starboard_thigh, PartId::starboard_shin,
              Junction::knee,          P::starboard_knee,
              P::starboard_hip,        kBoardingSelfKneeRadiusMetres};
    case 11:
      return {PartId::starboard_shin, PartId::starboard_boot,
              Junction::ankle,        P::starboard_ankle,
              P::starboard_knee,      kBoardingSelfAnkleLengthMetres};
    case 12:
      return {PartId::starboard_upper_arm,
              PartId::starboard_forearm,
              Junction::elbow,
              P::starboard_elbow,
              P::starboard_shoulder,
              kBoardingSelfElbowRadiusMetres};
    default:
      return {PartId::starboard_forearm, PartId::starboard_hand,
              Junction::wrist,           P::starboard_wrist,
              P::starboard_elbow,        kBoardingSelfWristLengthMetres};
  }
}
auto ss_same_region(const SelfRegion& a, const SelfRegion& b) -> bool {
  return a.first == b.first && a.second == b.second &&
         a.junction == b.junction && a.root == b.root && a.toward == b.toward &&
         a.limit_metres == b.limit_metres;
}
auto ss_leg(const BoardingRouteFootPhaseLeg& x) -> bool {
  return x.nominal_links && x.target_sole_identity && x.joint_sectors &&
         x.derivative_domains && x.timing_complete;
}
[[gnu::noinline]] auto ss_owner(
    const BoardingRouteFootPhaseCell& phase,
    const std::array<Point, kBoardingPlantedBodyPointCount>& relative,
    const SelfRegion& region, BoardingLowerFootTransferOwner& out) -> void {
  if (region.junction != Junction::waist) {
    unload_owner(phase, relative, region, out);
    return;
  }
  // This is the full pelvis support in the ACTUAL nominal trunk axial frame.
  // Proper-frame identities are compiler provenance, never midpoint tests.
  const auto y = body_intervals(phase.frames[1].columns[1].value);
  const auto extent = add(
      add(multiply(point(.24),
                   absolute(unload_frame_dot(phase.frames[0], 0, y))),
          multiply(point(.12),
                   absolute(unload_frame_dot(phase.frames[0], 1, y)))),
      multiply(point(.18), absolute(unload_frame_dot(phase.frames[0], 2, y))));
  const auto limit = point(region.limit_metres), gap = subtract(limit, extent);
  out.arithmetic_supported =
      extent.supported && limit.supported && gap.supported &&
      std::isfinite(extent.low) && std::isfinite(extent.high) &&
      extent.low <= extent.high && std::isfinite(limit.low) &&
      std::isfinite(limit.high) && limit.low <= limit.high &&
      std::isfinite(gap.low) && std::isfinite(gap.high) && gap.low <= gap.high;
  if (!out.arithmetic_supported) return;
  out.extent = bounds(extent);
  out.limit = bounds(limit);
  out.secondary_extent = bounds(point(0));
  out.structural_identity = true;
  out.certificate = SelfCert::cap_partner_support;
  out.certified = gap.low >= 0;
}
[[gnu::noinline]] auto ss_separate(const UnloadSolid& a, const UnloadSolid& b,
                                   SSD& d, SSC& c, const SSL& l, SSR& r,
                                   std::size_t pair) -> bool {
  const auto count = std::size_t{4} +
                     (a.shape.shape == SelfShape::capsule ? 2 : 3) +
                     (b.shape.shape == SelfShape::capsule ? 2 : 3);
  const auto proposal = [&](std::size_t i) -> Vec {
    if (i < 3)
      return i == 0 ? Vec{1, 0, 0} : i == 1 ? Vec{0, 1, 0} : Vec{0, 0, 1};
    if (i == 3)
      return self_difference(self_reporting(self_center(b.shape)),
                             self_reporting(self_center(a.shape)));
    i -= 4;
    for (const auto* s : {&a, &b})
      if (s->shape.shape != SelfShape::capsule) {
        if (i < 3)
          return s->frame ? self_reporting(
                                body_intervals(s->frame->columns[i].value))
                          : Vec{};
        i -= 3;
      }
    for (const auto* s : {&a, &b})
      if (s->shape.shape == SelfShape::capsule) {
        if (i < 2) {
          const auto& other = s == &a ? b : a;
          return self_difference(
              self_reporting(i == 0 ? s->shape.first : s->shape.second),
              self_reporting(self_center(other.shape)));
        }
        i -= 2;
      }
    return {};
  };
  r.self_stage = SSStage::separation;
  for (std::size_t i = 0; i < count; ++i) {
    r.self_axis = static_cast<std::uint8_t>(i);
    r.self_sign = 255;
    if (!ss_charge(d, r, d.work.self_axes, l.self_axes,
                   SSW::self_axis_capacity))
      return false;
    const auto axis = proposal(i);
    if (!self_direction(axis)) continue;
    for (std::size_t sign = 0; sign < 2; ++sign) {
      r.self_sign = static_cast<std::uint8_t>(sign);
      if (!ss_charge(d, r, d.work.self_signed_trials, l.self_signed_trials,
                     SSW::self_signed_capacity))
        return false;
      const auto n = sign == 0 ? axis : self_negate(axis);
      const auto total =
          add(unload_support(a, n), unload_support(b, self_negate(n)));
      if (!total.supported || !std::isfinite(total.low) ||
          !std::isfinite(total.high))
        return ss_fail(d, r, SSW::unsupported_arithmetic);
      if (total.high <= 0) {
        c.self_axes[pair] = static_cast<std::uint8_t>(i | (sign == 0 ? 0 : 16));
        return true;
      }
    }
  }
  return ss_fail(d, r, SSW::unresolved_self_pair);
}
} // namespace

auto detail::boarding_route_intermediate_support_self01_enroll_self_source(
    SSD& d, const SSL& l, SSR& r) -> bool {
  const auto source = [&](std::size_t i, auto predicate) {
    if (!ss_charge(d, r, d.work.source_guards, l.source_guards,
                   SSW::source_guard_capacity))
      return false;
    d.source_evaluated[i] = true;
    return predicate() || ss_fail(d, r, SSW::source_identity);
  };
  for (std::size_t i = 0; i < 15; ++i)
    if (!source(36 + i, [&] { return ss_same_part(d.parts[i], ss_part(i)); }))
      return false;
  // Materialize only AFTER the first region record charge, before any graph.
  SelfRegions regions{};
  for (std::size_t i = 0; i < 14; ++i)
    if (!source(51 + i, [&] {
          if (i == 0) regions = self_regions();
          return ss_same_region(regions[i], ss_region(i));
        }))
      return false;
  if (!source(65, [&] {
        return kBoardingRouteFootPhaseVersion == 1 && thigh_length == .47285 &&
               shin_length == .47478 && d.parts[0].id == PartId::pelvis;
      }))
    return false;
  // The frozen nominal compiler composes proper rational rotations and exact
  // local offsets. Stored interval columns are enclosures, not affine claims.
  return source(66, [&] {
    return kBoardingRouteFootPhaseVersion == 1 &&
           d.parts[1].id == PartId::trunk && d.parts[2].id == PartId::helmet &&
           d.parts[6].id == PartId::port_upper_arm &&
           d.parts[12].id == PartId::starboard_upper_arm;
  });
}

auto detail::boarding_route_intermediate_support_self01_self_bridge(
    const BoardingRouteIntermediateSupportSelf01CurrentCellToken& token, SSD& d,
    SSC& c, const SSL& l, SSR& r) -> SSS {
  std::size_t body_index_record{};
  const auto body = [&](auto predicate, SSW why = SSW::self_body_identity) {
    r.self_stage = SSStage::body;
    r.operation = body_index_record;
    if (!ss_charge(d, r, d.work.self_body_guards, l.self_body_guards,
                   SSW::self_body_capacity))
      return false;
    c.self_body_evaluated |= std::uint64_t{1} << body_index_record++;
    return predicate() || ss_fail(d, r, why);
  };
  if (!body([&] {
        return token.context_ && token.owner_ == &d && token.cell_ == &c &&
               token.request_ && token.context_->owner_ == &d &&
               token.context_->parts_ == &d.parts &&
               token.context_->request_ == token.request_ &&
               BoardingIntermediatePauseSupportAccess::valid(d.source) &&
               token.data_ ==
                   BoardingIntermediatePauseSupportAccess::data(d.source) &&
               token.context_->data_ == token.data_;
      }))
    return d.state;
  if (!body([&] { return boarding_route_foot_phase_environment(); },
            SSW::unsupported_arithmetic))
    return d.state;
  const auto& p = c.phase;
  if (!body([&] {
        return p.complete && p.arithmetic_supported && p.nominal_links &&
               p.target_sole_identities && p.joint_sectors &&
               p.derivative_domains && p.timing_complete;
      }))
    return d.state;
  if (!body([&] {
        return c.phase_index < 5 &&
               p.first == boarding_route_intermediate_support_self01_local(
                              c.phase_index, c.global_first) &&
               p.last == boarding_route_intermediate_support_self01_local(
                             c.phase_index, c.global_last) &&
               token.request_->seconds_per_parameter ==
                   (c.phase_index == 4 ? 2 : 12) &&
               c.projection_complete;
      }))
    return d.state;
  if (!body([&] {
        return d.source_enrolled && d.self_version == 1 &&
               std::ranges::all_of(d.source_evaluated,
                                   [](bool x) { return x; });
      }))
    return d.state;
  if (!body([&] {
        return d.source_evaluated[65] && p.nominal_links &&
               p.target_sole_identities;
      }))
    return d.state;
  if (!body([&] {
        return d.source_evaluated[66] && p.arithmetic_supported &&
               c.projected_carrier_complete[0];
      }))
    return d.state;
  if (!body([&] {
        return c.nominal_support_complete && c.nominal_equilibrium &&
               c.star_support && d.stop_condition == SSW::none;
      }))
    return d.state;
  for (std::size_t i = 0; i < 18; ++i)
    if (!body(
            [&] {
              return ss_point_valid(body_intervals(p.points[i].value), 8);
            },
            SSW::unsupported_arithmetic))
      return d.state;
  for (std::size_t f = 0; f < 4; ++f)
    for (std::size_t j = 0; j < 3; ++j)
      if (!body(
              [&] {
                return ss_point_valid(
                    body_intervals(p.frames[f].columns[j].value), 8);
              },
              SSW::unsupported_arithmetic))
        return d.state;
  for (std::size_t side = 0; side < 2; ++side)
    if (!body([&] { return ss_leg(p.legs[side]) && d.source_evaluated[65]; }))
      return d.state;
  if (!body([&] {
        return ss_same_part(d.parts[0], ss_part(0)) &&
               ss_same_part(d.parts[1], ss_part(1)) && d.source_evaluated[51];
      }))
    return d.state;
  if (!body([&] {
        return ss_same_part(d.parts[2], ss_part(2)) && d.source_evaluated[54] &&
               d.source_evaluated[66];
      }))
    return d.state;
  if (!body([&] {
        return d.source_evaluated[66] && ss_same_part(d.parts[6], ss_part(6)) &&
               ss_same_part(d.parts[12], ss_part(12));
      }))
    return d.state;
  SelfRegions regions{};
  if (!body([&] {
        regions = self_regions();
        for (std::size_t i = 0; i < 14; ++i)
          if (!ss_same_region(regions[i], ss_region(i))) return false;
        return true;
      }))
    return d.state;
  std::array<Point, kBoardingPlantedBodyPointCount> relative;
  if (!body([&] {
        relative[0] = {point(0), point(0), point(0)};
        return true;
      }))
    return d.state;
  for (std::size_t i = 1; i < 18; ++i)
    if (!body(
            [&] {
              relative[i] =
                  unload_difference(body_intervals(p.points[i].value),
                                    body_intervals(p.points[0].value));
              return ss_point_valid(relative[i], 16);
            },
            SSW::unsupported_arithmetic))
      return d.state;
  if (!body([&] {
        return body_index_record == 63 &&
               c.self_body_evaluated ==
                   kBoardingRouteIntermediateSupportSelf01BodyMask &&
               token.owner_ == &d && token.cell_ == &c &&
               token.data_ ==
                   BoardingIntermediatePauseSupportAccess::data(d.source);
      }))
    return d.state;
  c.self_body_complete = true;
  std::size_t pair{};
  for (std::size_t a = 0; a < 15; ++a)
    for (std::size_t b = a + 1; b < 15; ++b, ++pair) {
      r.operation.reset();
      r.self_stage = SSStage::not_run;
      r.self_pair = static_cast<std::uint16_t>(pair);
      r.self_region = 255;
      r.self_axis = 255;
      r.self_sign = 255;
      if (!ss_charge(d, r, d.work.self_pairs, l.self_pairs,
                     SSW::self_pair_capacity))
        return d.state;
      ++c.examined_pairs;
      auto certificate = SSCert::not_run;
      for (std::size_t region = 0; region < 14; ++region)
        if (static_cast<std::size_t>(regions[region].first) == a &&
            static_cast<std::size_t>(regions[region].second) == b) {
          r.self_stage = SSStage::owner;
          r.self_region = static_cast<std::uint8_t>(region);
          if (!ss_charge(d, r, d.work.self_owners, l.self_owners,
                         SSW::self_owner_capacity))
            return d.state;
          c.owner_attempted_mask |=
              static_cast<std::uint16_t>(std::uint16_t{1} << region);
          ss_owner(p, relative, regions[region], c.owners[region]);
          if (!c.owners[region].arithmetic_supported) {
            ss_fail(d, r, SSW::unsupported_arithmetic);
            return d.state;
          }
          if (c.owners[region].certified)
            certificate = static_cast<SSCert>(c.owners[region].certificate);
          else if (regions[region].junction == Junction::hip) {
            r.self_stage = SSStage::hip;
            if (!ss_charge(d, r, d.work.self_hip_complements,
                           l.self_hip_complements, SSW::self_hip_capacity))
              return d.state;
            const auto side = b >= 9 ? std::size_t{1} : std::size_t{0};
            self02_hip_complement(p, relative, regions[region], region, pair,
                                  c.hip_complements[side]);
            if (!c.hip_complements[side].arithmetic_supported) {
              ss_fail(d, r, SSW::unsupported_arithmetic);
              return d.state;
            }
            if (c.hip_complements[side].certified)
              certificate = SSCert::original_capsule_slab_complement;
          }
          break;
        }
      if (certificate == SSCert::not_run) {
        const auto first = unload_solid(d.parts[a], relative, p),
                   second = unload_solid(d.parts[b], relative, p);
        if (!ss_separate(first, second, d, c, l, r, pair)) return d.state;
        certificate = SSCert::convex_support_plane;
      }
      c.self_certificates[pair] = certificate;
      ++c.self_certificate_counts[static_cast<std::size_t>(certificate)];
      ++c.accepted_pairs;
    }
  c.self_complete = pair == 105 && c.accepted_pairs == 105;
  if (!c.self_complete) {
    ss_fail(d, r, SSW::self_incomplete);
    return d.state;
  }
  r.self_pair = 65535;
  r.self_region = 255;
  r.self_axis = 255;
  r.self_sign = 255;
  r.self_stage = SSStage::complete;
  c.complete = true;
  c.state = d.state = SSS::accepted;
  return d.state;
}
} // namespace apsis_drift

#include "origin_boarding_route_intermediate_hip_diagnostic01_internal.hpp"
namespace apsis_drift {
namespace {
using HipD = BoardingRouteIntermediateHipDiagnostic01Diagnostic;
using HipL = detail::BoardingRouteIntermediateHipDiagnostic01Limits;
using HipW = BoardingRouteIntermediateHipDiagnostic01Condition;
auto hip_identity(HipD& d, HipW why) -> bool {
  if (d.first_refusal.condition == HipW::none) d.first_refusal.condition = why;
  if (d.state != BoardingRouteIntermediateHipDiagnostic01State::capacity &&
      d.state != BoardingRouteIntermediateHipDiagnostic01State::unsupported) {
    d.state = BoardingRouteIntermediateHipDiagnostic01State::unresolved;
    d.stop_condition = why;
  }
  return false;
}
} // namespace
auto detail::boarding_route_intermediate_hip_diagnostic01_body_source(
    HipD& d,
    const std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount>&
        parts,
    const HipL& l) -> bool {
  const auto source = [&](std::size_t i, auto predicate) {
    return boarding_route_intermediate_hip_diagnostic01_source_charge(d, i,
                                                                      l) &&
           (predicate() || hip_identity(d, HipW::source_identity));
  };
  for (std::size_t i = 0; i < 15; ++i)
    if (!source(36 + i, [&] { return ss_same_part(parts[i], ss_part(i)); }))
      return false;
  SelfRegions regions{};
  for (std::size_t i = 0; i < 14; ++i)
    if (!source(51 + i, [&] {
          if (i == 0) regions = self_regions();
          return ss_same_region(regions[i], ss_region(i));
        }))
      return false;
  if (!source(65, [&] {
        return kBoardingRouteFootPhaseVersion == 1 && thigh_length == .47285 &&
               shin_length == .47478 && parts[0].id == PartId::pelvis;
      }))
    return false;
  return source(66, [&] {
    return kBoardingRouteFootPhaseVersion == 1 &&
           parts[1].id == PartId::trunk && parts[2].id == PartId::helmet &&
           parts[6].id == PartId::port_upper_arm &&
           parts[12].id == PartId::starboard_upper_arm;
  });
}
auto detail::boarding_route_intermediate_hip_diagnostic01_current_body(
    HipD& d, const HipL& l) -> bool {
  const auto current = [&](std::size_t i, auto predicate) {
    return boarding_route_intermediate_hip_diagnostic01_current_charge(d, i,
                                                                       l) &&
           (predicate() || hip_identity(d, HipW::current_identity));
  };
  std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount> parts{};
  for (std::size_t i = 0; i < 15; ++i)
    if (!current(10 + i, [&] {
          if (i == 0) parts = boarding_route_foot_phase_parts();
          return ss_same_part(parts[i], ss_part(i));
        }))
      return false;
  if (!current(25, [&] {
        const auto regions = self_regions();
        return ss_same_region(regions[1], ss_region(1)) &&
               regions[1].first == PartId::pelvis &&
               regions[1].second == PartId::port_thigh &&
               regions[1].limit_metres == kBoardingSelfHipLengthMetres;
      }))
    return false;
  // These are provenance of the fresh frozen compiler, not enclosure-unit
  // tests.
  if (!current(26, [&] {
        return kBoardingRouteFootPhaseVersion == 1 && d.source_complete &&
               d.phase_available && parts[0].id == PartId::pelvis &&
               ss_same_part(parts[0], ss_part(0));
      }))
    return false;
  return current(27, [&] {
    return thigh_length == .47285 && parts[3].id == PartId::port_thigh &&
           ss_same_part(parts[3], ss_part(3)) &&
           d.current_phase.front().legs[0].nominal_links;
  });
}
} // namespace apsis_drift

#include "origin_boarding_route_intermediate_hip_diagnostic02_internal.hpp"
namespace apsis_drift {
namespace {
using Hip02D = BoardingRouteIntermediateHipDiagnostic02Diagnostic;
using Hip02L = detail::BoardingRouteIntermediateHipDiagnostic02Limits;
using Hip02W = BoardingRouteIntermediateHipDiagnostic02Condition;
auto hip02_identity(Hip02D& d, Hip02W why) -> bool {
  if (d.first_refusal.condition == Hip02W::none)
    d.first_refusal.condition = why;
  if (d.state != BoardingRouteIntermediateHipDiagnostic02State::capacity &&
      d.state != BoardingRouteIntermediateHipDiagnostic02State::unsupported) {
    d.state = BoardingRouteIntermediateHipDiagnostic02State::unresolved;
    d.stop_condition = why;
  }
  return false;
}
} // namespace
auto detail::boarding_route_intermediate_hip_diagnostic02_body_source(
    Hip02D& d,
    const std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount>&
        parts,
    const Hip02L& l) -> bool {
  const auto source = [&](std::size_t i, auto predicate) {
    return boarding_route_intermediate_hip_diagnostic02_source_charge(d, i,
                                                                      l) &&
           (predicate() || hip02_identity(d, Hip02W::source_identity));
  };
  for (std::size_t i = 0; i < 15; ++i)
    if (!source(36 + i, [&] { return ss_same_part(parts[i], ss_part(i)); }))
      return false;
  SelfRegions regions{};
  for (std::size_t i = 0; i < 14; ++i)
    if (!source(51 + i, [&] {
          if (i == 0) regions = self_regions();
          return ss_same_region(regions[i], ss_region(i));
        }))
      return false;
  if (!source(65, [&] {
        return kBoardingRouteFootPhaseVersion == 1 && thigh_length == .47285 &&
               shin_length == .47478 && parts[0].id == PartId::pelvis;
      }))
    return false;
  return source(66, [&] {
    return kBoardingRouteFootPhaseVersion == 1 &&
           parts[1].id == PartId::trunk && parts[2].id == PartId::helmet &&
           parts[6].id == PartId::port_upper_arm &&
           parts[12].id == PartId::starboard_upper_arm;
  });
}
auto detail::boarding_route_intermediate_hip_diagnostic02_current_body(
    Hip02D& d, const Hip02L& l) -> bool {
  const auto current = [&](std::size_t i, auto predicate) {
    return boarding_route_intermediate_hip_diagnostic02_current_charge(d, i,
                                                                       l) &&
           (predicate() || hip02_identity(d, Hip02W::current_identity));
  };
  std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount> parts{};
  for (std::size_t i = 0; i < 15; ++i)
    if (!current(10 + i, [&] {
          if (i == 0) parts = boarding_route_foot_phase_parts();
          return ss_same_part(parts[i], ss_part(i));
        }))
      return false;
  if (!current(25, [&] {
        const auto regions = self_regions();
        return ss_same_region(regions[1], ss_region(1)) &&
               regions[1].first == PartId::pelvis &&
               regions[1].second == PartId::port_thigh &&
               regions[1].limit_metres == kBoardingSelfHipLengthMetres;
      }))
    return false;
  // These are provenance of the fresh frozen compiler, not enclosure-unit
  // tests.
  if (!current(26, [&] {
        return kBoardingRouteFootPhaseVersion == 1 && d.source_complete &&
               d.phase_available && parts[0].id == PartId::pelvis &&
               ss_same_part(parts[0], ss_part(0));
      }))
    return false;
  return current(27, [&] {
    return thigh_length == .47285 && parts[3].id == PartId::port_thigh &&
           ss_same_part(parts[3], ss_part(3)) &&
           d.current_phase.front().legs[0].nominal_links;
  });
}
} // namespace apsis_drift

#include "origin_boarding_intermediate_endpoint01_internal.hpp"
namespace apsis_drift {
namespace {
using EPD = BoardingIntermediateEndpoint01Diagnostic;
using EPC = BoardingIntermediateEndpoint01Cell;
using EPR = BoardingIntermediateEndpoint01Refusal;
using EPL = detail::BoardingIntermediateEndpoint01Limits;
using EPW = BoardingIntermediateEndpoint01Condition;
using EPS = BoardingIntermediateEndpoint01State;
using EPStage = BoardingIntermediateEndpoint01SelfStage;
using EPCert = BoardingIntermediateEndpoint01Certificate;
auto ep_source_bit(const EPD& d, std::size_t i) -> bool {
  return (d.source_evaluated & (std::uint64_t{1} << i)) != 0;
}
auto ep_valid(Interval x) -> bool {
  return x.supported && std::isfinite(x.low) && std::isfinite(x.high) &&
         x.low <= x.high;
}
auto ep_scalar(const BoardingFootSiteScalarBounds& x) -> Interval {
  return x.supported ? interval(x.lower, x.upper) : failed();
}
auto ep_axis(const EPC& c, std::size_t axis) -> Point {
  return {ep_scalar(c.unit_axes[axis][0]), ep_scalar(c.unit_axes[axis][1]),
          ep_scalar(c.unit_axes[axis][2])};
}
auto ep_component(const BoardingPlantedLegPointBounds& b, std::size_t j)
    -> Interval {
  return interval(j == 0   ? b.lower.x
                  : j == 1 ? b.lower.y
                           : b.lower.z,
                  j == 0   ? b.upper.x
                  : j == 1 ? b.upper.y
                           : b.upper.z);
}
// Direct endpoint division encloses genuine D/L, never a rounded reciprocal.
auto ep_unit_divide(Interval numerator, double length) -> Interval {
  if (!ep_valid(numerator) || !std::isfinite(length) || length <= 0)
    return failed();
  if (numerator.low == 0 && numerator.high == 0) return point(0);
  const auto low = numerator.low / length, high = numerator.high / length;
  if (!std::isfinite(low) || !std::isfinite(high) ||
      low == -std::numeric_limits<double>::max() ||
      high == std::numeric_limits<double>::max())
    return failed();
  return interval(down(low), up(high));
}
[[gnu::noinline]] auto ep_units(EPD& d, EPC& c, const EPL& l, EPR& r) -> bool {
  for (std::size_t axis = 0; axis < 4; ++axis) {
    const auto side = axis % 2;
    const auto knee =
        side == 0 ? BodyPointId::port_knee : BodyPointId::starboard_knee;
    const auto from = axis < 2 ? (side == 0 ? BodyPointId::port_hip
                                            : BodyPointId::starboard_hip)
                               : (side == 0 ? BodyPointId::port_ankle
                                            : BodyPointId::starboard_ankle);
    Point displacement{};
    for (std::size_t operation = 0; operation < 6; ++operation) {
      const auto row = 6 * axis + operation, component = operation % 3;
      r = EPR{};
      r.self_stage = EPStage::unit_axes;
      r.operation = static_cast<std::uint8_t>(row);
      c.unit_axis_cursor = static_cast<std::uint8_t>(row);
      if (!detail::intermediate_endpoint01_charge(
              d, r, d.work.unit_axis_operations, l.unit_axis_operations,
              EPW::unit_axis_capacity))
        return false;
      c.unit_axis_attempted |= std::uint32_t{1} << row;
      const auto length = axis < 2 ? thigh_length : shin_length;
      if (!c.self_body_complete || !ss_leg(c.phase.legs[side]) ||
          !ep_source_bit(d, 62) ||
          (axis < 2 ? length != .47285 : length != .47478))
        return detail::intermediate_endpoint01_refuse(d, r,
                                                      EPW::unit_axis_identity);
      const auto result =
          operation < 3
              ? subtract(ep_component(c.phase.points[body_index(knee)].value,
                                      component),
                         ep_component(c.phase.points[body_index(from)].value,
                                      component))
              : ep_unit_divide(displacement[component], length);
      if (!ep_valid(result) ||
          (operation < 3 && (result.low < -16 || result.high > 16)))
        return detail::intermediate_endpoint01_refuse(
            d, r, EPW::unsupported_arithmetic);
      if (operation < 3)
        displacement[component] = result;
      else
        c.unit_axes[axis][component] = {result.low, result.high, true};
      c.unit_axis_written |= std::uint32_t{1} << row;
    }
    c.unit_axis_complete_mask |=
        static_cast<std::uint8_t>(std::uint8_t{1} << axis);
  }
  return (c.unit_axis_written == kBoardingIntermediateEndpoint01UnitMask &&
          c.unit_axis_complete_mask == 15) ||
         detail::intermediate_endpoint01_refuse(d, r, EPW::unit_axis_identity);
}
[[gnu::noinline]] auto ep_owner(
    const EPC& c,
    const std::array<Point, kBoardingPlantedBodyPointCount>& relative,
    const SelfRegion& region, BoardingLowerFootTransferOwner& out) -> void {
  if (region.junction != Junction::hip && region.junction != Junction::ankle) {
    ss_owner(c.phase, relative, region, out);
    return;
  }
  const auto side = static_cast<std::size_t>(region.second) >= 9
                        ? std::size_t{1}
                        : std::size_t{0};
  const auto u = ep_axis(c, (region.junction == Junction::hip ? 0 : 2) + side);
  auto extent = failed();
  if (region.junction == Junction::hip) {
    const auto x = unload_frame_dot(c.phase.frames[0], 0, u),
               y = unload_frame_dot(c.phase.frames[0], 1, u),
               z = unload_frame_dot(c.phase.frames[0], 2, u);
    extent = add(add(subtract(multiply(point(.24), absolute(x)),
                              multiply(point(side == 0 ? -.14 : .14), x)),
                     multiply(point(.12), absolute(y))),
                 multiply(point(.18), absolute(z)));
  } else {
    if (.075 > region.limit_metres || region.limit_metres >= shin_length ||
        !ep_valid(u[1]))
      return;
    // The original nominal sole is WORLD-Y, and bootC-A=-.05 WORLD-Y;
    // B63 authenticates that construction. Cancellation needs nonnegative uY.
    if (u[1].low < 0) {
      out.arithmetic_supported = true;
      return;
    }
    const auto x = unload_frame_dot(c.phase.frames[side + 2], 0, u),
               z = unload_frame_dot(c.phase.frames[side + 2], 2, u);
    extent = add(multiply(point(.06), absolute(x)),
                 multiply(point(.14), absolute(z)));
  }
  const auto limit = point(region.limit_metres), gap = subtract(limit, extent);
  out.arithmetic_supported =
      ep_valid(extent) && ep_valid(limit) && ep_valid(gap);
  if (!out.arithmetic_supported) return;
  out.extent = bounds(extent);
  out.limit = bounds(limit);
  out.secondary_extent = bounds(point(0));
  out.structural_identity = true;
  out.certificate = SelfCert::original_axis_box_support;
  out.certified = gap.low >= 0;
}
[[gnu::noinline]] auto ep_hip(
    const EPC& c, const SelfRegion& region, std::size_t region_index,
    std::size_t pair, BoardingRouteCheckpointUnloadHipComplement& result)
    -> void {
  result.attempted = true;
  result.region = region_index;
  result.pair = pair;
  const auto side = static_cast<std::size_t>(region.second) >= 9
                        ? std::size_t{1}
                        : std::size_t{0};
  result.side = side;
  result.slab_limit_metres = region.limit_metres;
  const auto hip =
      side == 0 ? BodyPointId::port_hip : BodyPointId::starboard_hip;
  const auto knee =
      side == 0 ? BodyPointId::port_knee : BodyPointId::starboard_knee;
  const auto thigh = side == 0 ? PartId::port_thigh : PartId::starboard_thigh;
  if (!c.self_body_complete || c.unit_axis_complete_mask != 15 ||
      !ss_leg(c.phase.legs[side]) || region.junction != Junction::hip ||
      region.first != PartId::pelvis || region.second != thigh ||
      region.root != hip || region.toward != knee ||
      region.limit_metres != kBoardingSelfHipLengthMetres ||
      .105 > region.limit_metres || region.limit_metres >= thigh_length)
    return;
  result.nominal_unit_identity = result.upright_pelvis_identity =
      result.original_slab_identity = true;
  const auto u = ep_axis(c, side);
  for (std::size_t i = 0; i < 3; ++i) {
    if (!ep_valid(u[i])) return;
    result.axis[i] = bounds(u[i]);
  }
  if (u[1].high >= 0) {
    result.arithmetic_supported = true;
    return;
  }
  result.extent_evaluated = true;
  const auto proof = self02_hip_expression(u, .105, region.limit_metres, -.12);
  result.arithmetic_supported = proof.arithmetic_supported;
  if (!proof.arithmetic_supported) return;
  result.transverse = proof.transverse;
  result.extent = proof.extent;
  result.limit = proof.limit;
  result.strict_gap = proof.strict_gap;
  result.certified = proof.negative_axis && proof.strict_extent_below_bottom;
}
[[gnu::noinline]] auto ep_separate(const UnloadSolid& a, const UnloadSolid& b,
                                   EPD& d, EPC& c, const EPL& l, EPR& r,
                                   std::size_t pair) -> bool {
  const auto count = std::size_t{4} +
                     (a.shape.shape == SelfShape::capsule ? 2 : 3) +
                     (b.shape.shape == SelfShape::capsule ? 2 : 3);
  const auto proposal = [&](std::size_t i) -> Vec {
    if (i < 3)
      return i == 0 ? Vec{1, 0, 0} : i == 1 ? Vec{0, 1, 0} : Vec{0, 0, 1};
    if (i == 3)
      return self_difference(self_reporting(self_center(b.shape)),
                             self_reporting(self_center(a.shape)));
    i -= 4;
    for (const auto* s : {&a, &b})
      if (s->shape.shape != SelfShape::capsule) {
        if (i < 3)
          return s->frame ? self_reporting(
                                body_intervals(s->frame->columns[i].value))
                          : Vec{};
        i -= 3;
      }
    for (const auto* s : {&a, &b})
      if (s->shape.shape == SelfShape::capsule) {
        if (i < 2) {
          const auto& other = s == &a ? b : a;
          return self_difference(
              self_reporting(i == 0 ? s->shape.first : s->shape.second),
              self_reporting(self_center(other.shape)));
        }
        i -= 2;
      }
    return {};
  };
  r.self_stage = EPStage::separation;
  for (std::size_t i = 0; i < count; ++i) {
    r.self_axis = static_cast<std::uint8_t>(i);
    r.self_sign = 255;
    if (!detail::intermediate_endpoint01_charge(
            d, r, d.work.self_axes, l.self_axes, EPW::self_axis_capacity))
      return false;
    const auto axis = proposal(i);
    if (!self_direction(axis)) continue;
    for (std::size_t sign = 0; sign < 2; ++sign) {
      r.self_sign = static_cast<std::uint8_t>(sign);
      if (!detail::intermediate_endpoint01_charge(
              d, r, d.work.self_signed_trials, l.self_signed_trials,
              EPW::self_signed_capacity))
        return false;
      const auto n = sign == 0 ? axis : self_negate(axis);
      const auto total =
          add(unload_support(a, n), unload_support(b, self_negate(n)));
      if (!total.supported || !std::isfinite(total.low) ||
          !std::isfinite(total.high))
        return detail::intermediate_endpoint01_refuse(
            d, r, EPW::unsupported_arithmetic);
      if (total.high <= 0) {
        c.self_axes[pair] = static_cast<std::uint8_t>(i | (sign == 0 ? 0 : 16));
        return true;
      }
    }
  }
  return detail::intermediate_endpoint01_refuse(d, r,
                                                EPW::unresolved_self_pair);
}
} // namespace
auto detail::intermediate_endpoint01_enroll_self_source(EPD& d, const EPL& l,
                                                        EPR& r) -> bool {
  const auto source = [&](std::size_t i, auto predicate) {
    r = EPR{};
    r.self_stage = EPStage::source;
    r.operation = static_cast<std::uint8_t>(i);
    if (!intermediate_endpoint01_charge(d, r, d.work.source_guards,
                                        l.source_guards, EPW::source_capacity))
      return false;
    d.source_evaluated |= std::uint64_t{1} << i;
    return predicate() ||
           intermediate_endpoint01_refuse(d, r, EPW::source_identity);
  };
  for (std::size_t i = 0; i < 15; ++i)
    if (!source(33 + i, [&] { return ss_same_part(d.parts[i], ss_part(i)); }))
      return false;
  SelfRegions regions{};
  for (std::size_t i = 0; i < 14; ++i)
    if (!source(48 + i, [&] {
          if (i == 0) regions = self_regions();
          return ss_same_region(regions[i], ss_region(i));
        }))
      return false;
  if (!source(62, [&] {
        return kBoardingRouteFootPhaseVersion == 1 && thigh_length == .47285 &&
               shin_length == .47478 && d.parts[0].id == PartId::pelvis;
      }))
    return false;
  // Exact-real nominal rational/composed rotations and fixed local arm offsets
  // are compiler provenance, never midpoint or stored-affine orthogonality.
  return source(63, [&] {
    return kBoardingRouteFootPhaseVersion == 1 &&
           d.parts[1].id == PartId::trunk && d.parts[2].id == PartId::helmet &&
           d.parts[6].id == PartId::port_upper_arm &&
           d.parts[12].id == PartId::starboard_upper_arm;
  });
}
auto detail::intermediate_endpoint01_self_bridge(
    const BoardingIntermediateEndpoint01CurrentToken& token, EPD& d, EPC& c,
    const EPL& l, EPR& r) -> EPS {
  std::size_t body_index_record{};
  const auto body = [&](auto predicate, EPW why = EPW::self_body_identity) {
    r = EPR{};
    r.self_stage = EPStage::body;
    r.operation = static_cast<std::uint8_t>(body_index_record);
    if (!intermediate_endpoint01_charge(d, r, d.work.self_body_guards,
                                        l.self_body_guards,
                                        EPW::self_body_capacity))
      return false;
    c.self_body_evaluated |= std::uint64_t{1} << body_index_record++;
    return predicate() || intermediate_endpoint01_refuse(d, r, why);
  };
  if (!body([&] {
        return token.context() && token.owner() == &d && token.cell() == &c &&
               token.request() && token.context()->owner() == &d &&
               token.context()->parts() == &d.parts &&
               token.context()->request() == token.request() &&
               BoardingIntermediatePauseSupportAccess::valid(d.source) &&
               token.data() ==
                   BoardingIntermediatePauseSupportAccess::data(d.source) &&
               token.context()->data() == token.data() && token.data() &&
               d.cells.size() == 1 && d.cells.capacity() == 1 &&
               &d.cells.front() == &c && d.work.phase_calls == 1;
      }))
    return d.state;
  if (!body([&] { return boarding_route_foot_phase_environment(); },
            EPW::unsupported_arithmetic))
    return d.state;
  const auto& p = c.phase;
  if (!body([&] {
        return p.complete && p.arithmetic_supported && p.nominal_links &&
               p.target_sole_identities && p.joint_sectors &&
               p.derivative_domains && p.timing_complete;
      }))
    return d.state;
  if (!body([&] {
        return d.candidate == BoardingIntermediateEndpoint01Candidate::
                                  authored_descent_midpoint &&
               d.version == 1 && p.first == 0 && p.last == 1 &&
               token.request()->seconds_per_parameter == 2 &&
               c.projection_complete;
      }))
    return d.state;
  if (!body([&] {
        return d.source_enrolled && d.self_version == 1 &&
               d.source_evaluated == UINT64_MAX;
      }))
    return d.state;
  if (!body([&] {
        return ep_source_bit(d, 62) && p.nominal_links &&
               p.target_sole_identities;
      }))
    return d.state;
  if (!body([&] {
        return ep_source_bit(d, 63) && p.arithmetic_supported &&
               c.projected_carrier_complete[0];
      }))
    return d.state;
  if (!body([&] {
        return c.nominal_support_complete && c.nominal_equilibrium &&
               c.star_support && d.stop_condition == EPW::none;
      }))
    return d.state;
  for (std::size_t i = 0; i < 18; ++i)
    if (!body(
            [&] {
              return ss_point_valid(body_intervals(p.points[i].value), 8);
            },
            EPW::unsupported_arithmetic))
      return d.state;
  for (std::size_t f = 0; f < 4; ++f)
    for (std::size_t j = 0; j < 3; ++j)
      if (!body(
              [&] {
                return ss_point_valid(
                    body_intervals(p.frames[f].columns[j].value), 8);
              },
              EPW::unsupported_arithmetic))
        return d.state;
  for (std::size_t side = 0; side < 2; ++side)
    if (!body([&] {
          const auto first = side == 0 ? std::size_t{36} : std::size_t{42};
          return ss_leg(p.legs[side]) && ep_source_bit(d, 62) &&
                 ep_source_bit(d, first) && ep_source_bit(d, first + 1) &&
                 ss_same_part(d.parts[first - 33], ss_part(first - 33)) &&
                 ss_same_part(d.parts[first - 32], ss_part(first - 32));
        }))
      return d.state;
  if (!body([&] {
        return ss_same_part(d.parts[0], ss_part(0)) &&
               ss_same_part(d.parts[1], ss_part(1)) && ep_source_bit(d, 33) &&
               ep_source_bit(d, 34) && ep_source_bit(d, 48);
      }))
    return d.state;
  if (!body([&] {
        return ss_same_part(d.parts[1], ss_part(1)) &&
               ss_same_part(d.parts[2], ss_part(2)) && ep_source_bit(d, 34) &&
               ep_source_bit(d, 35) && ep_source_bit(d, 51) &&
               ep_source_bit(d, 63);
      }))
    return d.state;
  if (!body([&] {
        if (!ep_source_bit(d, 63)) return false;
        for (const auto part :
             {std::size_t{6}, std::size_t{7}, std::size_t{8}, std::size_t{12},
              std::size_t{13}, std::size_t{14}})
          if (!ep_source_bit(d, 33 + part) ||
              !ss_same_part(d.parts[part], ss_part(part)))
            return false;
        for (const auto region :
             {std::size_t{4}, std::size_t{5}, std::size_t{8}, std::size_t{9},
              std::size_t{12}, std::size_t{13}})
          if (!ep_source_bit(d, 48 + region)) return false;
        return true;
      }))
    return d.state;
  SelfRegions regions{};
  if (!body([&] {
        regions = self_regions();
        for (std::size_t i = 0; i < 14; ++i)
          if (!ep_source_bit(d, 48 + i) ||
              !ss_same_region(regions[i], ss_region(i)))
            return false;
        return true;
      }))
    return d.state;
  std::array<Point, kBoardingPlantedBodyPointCount> relative;
  if (!body([&] {
        relative[0] = {point(0), point(0), point(0)};
        return true;
      }))
    return d.state;
  for (std::size_t i = 1; i < 18; ++i)
    if (!body(
            [&] {
              relative[i] =
                  unload_difference(body_intervals(p.points[i].value),
                                    body_intervals(p.points[0].value));
              return ss_point_valid(relative[i], 16);
            },
            EPW::unsupported_arithmetic))
      return d.state;
  if (!body([&] {
        return body_index_record == 63 &&
               c.self_body_evaluated ==
                   kBoardingIntermediateEndpoint01BodyMask &&
               token.owner() == &d && token.cell() == &c &&
               token.data() ==
                   BoardingIntermediatePauseSupportAccess::data(d.source);
      }))
    return d.state;
  c.self_body_complete = true;
  if (!ep_units(d, c, l, r)) return d.state;
  std::size_t pair{};
  for (std::size_t a = 0; a < 15; ++a)
    for (std::size_t b = a + 1; b < 15; ++b, ++pair) {
      r = EPR{};
      r.self_stage = EPStage::not_run;
      r.self_pair = static_cast<std::uint16_t>(pair);
      r.self_region = 255;
      r.self_axis = 255;
      r.self_sign = 255;
      r.self_stage = EPStage::pair;
      if (!intermediate_endpoint01_charge(d, r, d.work.self_pairs, l.self_pairs,
                                          EPW::self_pair_capacity))
        return d.state;
      ++c.examined_pairs;
      auto certificate = EPCert::not_run;
      for (std::size_t region = 0; region < 14; ++region)
        if (static_cast<std::size_t>(regions[region].first) == a &&
            static_cast<std::size_t>(regions[region].second) == b) {
          r.self_stage = EPStage::owner;
          r.self_region = static_cast<std::uint8_t>(region);
          if (!intermediate_endpoint01_charge(d, r, d.work.self_owners,
                                              l.self_owners,
                                              EPW::self_owner_capacity))
            return d.state;
          c.owner_attempted_mask |=
              static_cast<std::uint16_t>(std::uint16_t{1} << region);
          ep_owner(c, relative, regions[region], c.owners[region]);
          if (!c.owners[region].arithmetic_supported) {
            intermediate_endpoint01_refuse(d, r, EPW::unsupported_arithmetic);
            return d.state;
          }
          if (c.owners[region].certified)
            certificate = static_cast<EPCert>(c.owners[region].certificate);
          else if (regions[region].junction == Junction::hip) {
            r.self_stage = EPStage::hip;
            if (!intermediate_endpoint01_charge(
                    d, r, d.work.self_hip_complements, l.self_hip_complements,
                    EPW::self_hip_capacity))
              return d.state;
            const auto side = b >= 9 ? std::size_t{1} : std::size_t{0};
            ep_hip(c, regions[region], region, pair, c.hip_complements[side]);
            if (!c.hip_complements[side].arithmetic_supported) {
              intermediate_endpoint01_refuse(d, r, EPW::unsupported_arithmetic);
              return d.state;
            }
            if (c.hip_complements[side].certified)
              certificate = EPCert::original_capsule_slab_complement;
          }
          break;
        }
      if (certificate == EPCert::not_run) {
        const auto first = unload_solid(d.parts[a], relative, p),
                   second = unload_solid(d.parts[b], relative, p);
        if (!ep_separate(first, second, d, c, l, r, pair)) return d.state;
        certificate = EPCert::convex_support_plane;
      }
      c.self_certificates[pair] = certificate;
      ++c.self_certificate_counts[static_cast<std::size_t>(certificate)];
      ++c.accepted_pairs;
    }
  c.self_complete = pair == 105 && c.accepted_pairs == 105;
  if (!c.self_complete) {
    intermediate_endpoint01_refuse(d, r, EPW::incomplete_endpoint);
    return d.state;
  }
  r.self_pair = 65535;
  r.self_region = 255;
  r.self_axis = 255;
  r.self_sign = 255;
  r.self_stage = EPStage::complete;
  c.complete = true;
  c.state = d.state = EPS::accepted;
  return d.state;
}
} // namespace apsis_drift
