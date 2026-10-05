#include "apsis_drift/origin_boarding_planted_legs.hpp"
#include "apsis_drift/origin_boarding_source_endpoint.hpp"
#include "origin_boarding_foot_sites_internal.hpp"
#include "origin_boarding_planted_legs_internal.hpp"
#include "origin_boarding_self_model02_internal.hpp"
#include "origin_boarding_source_endpoint_internal.hpp"
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
