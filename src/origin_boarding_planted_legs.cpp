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

#include "origin_boarding_intermediate_reach_evidence01_internal.hpp"
namespace apsis_drift {
namespace {
using ReachD = BoardingIntermediateReachEvidence01Diagnostic;
using ReachL = detail::BoardingIntermediateReachEvidence01Limits;
using ReachW = BoardingIntermediateReachEvidence01Condition;
using ReachStage = BoardingIntermediateReachEvidence01Stage;
using ReachKind = BoardingIntermediateReachEvidence01Kind;
using ReachToken = detail::BoardingIntermediateReachEvidence01CaptureToken;
auto reach_threshold_valid(Interval value) -> bool {
  return value.supported && std::isfinite(value.low) &&
         std::isfinite(value.high) && value.low <= value.high;
}
auto reach_public_bound_valid(
    const BoardingIntermediateReachEvidence01Bounds& b) -> bool {
  return b.supported && std::isfinite(b.lower) && std::isfinite(b.upper) &&
         b.lower <= b.upper;
}
auto reach_threshold_charge(ReachD& d, const ReachL& l, std::uint8_t row)
    -> bool {
  d.threshold_operation = row;
  if (d.work.threshold_operations >= l.threshold_operations)
    return detail::intermediate_reach_evidence01_refuse(
        d, ReachW::threshold_capacity, ReachStage::threshold, row);
  ++d.work.threshold_operations;
  d.threshold_attempted |= static_cast<std::uint8_t>(std::uint8_t{1} << row);
  return true;
}
} // namespace
[[gnu::noinline]] auto detail::intermediate_reach_evidence01_thresholds(
    const ReachToken& token, ReachD& d, const ReachL& limits) -> bool {
  // C06 owns the actual immutable family/length reads, before any point or T.
  if (!intermediate_reach_evidence01_capture_charge(d, limits, 5)) return false;
  if (!intermediate_reach_evidence01_token_valid(token, d) ||
      kBoardingRouteFootPhaseVersion != 1 || d.version != 1 ||
      (d.kind != ReachKind::reach_refusal &&
       d.kind != ReachKind::intentional_body_stop) ||
      d.captured_side > 1 || thigh_length != .47285 || shin_length != .47478 ||
      !std::isfinite(thigh_length) || !std::isfinite(shin_length) ||
      thigh_length <= 0 || shin_length <= 0)
    return intermediate_reach_evidence01_refuse(d, ReachW::capture_identity,
                                                ReachStage::capture, 5);
  d.capture_written |= static_cast<std::uint8_t>(std::uint8_t{1} << 5);
  Interval sum, difference;
  for (std::uint8_t row = 0; row < 4; ++row) {
    if (row == 1) d.maximum_squared = {};
    if (row == 3) d.minimum_squared = {};
    if (!reach_threshold_charge(d, limits, row)) return false;
    Interval value;
    switch (row) {
      case 0: value = add(point(thigh_length), point(shin_length)); break;
      case 1: value = square(sum); break;
      case 2: value = subtract(point(thigh_length), point(shin_length)); break;
      case 3: value = square(difference); break;
      default: break;
    }
    if (!reach_threshold_valid(value))
      return intermediate_reach_evidence01_refuse(
          d, ReachW::unsupported_arithmetic, ReachStage::threshold, row);
    if (row == 0) sum = value;
    if (row == 1) d.maximum_squared = {value.low, value.high, true};
    if (row == 2) difference = value;
    if (row == 3) d.minimum_squared = {value.low, value.high, true};
    d.threshold_written |= static_cast<std::uint8_t>(std::uint8_t{1} << row);
  }
  if (!intermediate_reach_evidence01_capture_charge(d, limits, 6)) return false;
  if (d.threshold_attempted != 15 || d.threshold_written != 15 ||
      !reach_public_bound_valid(d.minimum_squared) ||
      !reach_public_bound_valid(d.maximum_squared))
    return intermediate_reach_evidence01_refuse(
        d, ReachW::unsupported_arithmetic, ReachStage::capture, 6);
  d.capture_written |= static_cast<std::uint8_t>(std::uint8_t{1} << 6);
  return true;
}
} // namespace apsis_drift

#include "origin_boarding_intermediate_endpoint02_internal.hpp"
namespace apsis_drift {
namespace {
using EP2D = BoardingIntermediateEndpoint02Diagnostic;
using EP2C = BoardingIntermediateEndpoint02Cell;
using EP2R = BoardingIntermediateEndpoint02Refusal;
using EP2L = detail::BoardingIntermediateEndpoint02Limits;
using EP2W = BoardingIntermediateEndpoint02Condition;
using EP2S = BoardingIntermediateEndpoint02State;
using EP2Stage = BoardingIntermediateEndpoint02SelfStage;
using EP2Cert = BoardingIntermediateEndpoint02Certificate;
auto ep2_source_bit(const EP2D& d, std::size_t i) -> bool {
  return (d.source_evaluated & (std::uint64_t{1} << i)) != 0;
}
auto ep2_valid(Interval x) -> bool {
  return x.supported && std::isfinite(x.low) && std::isfinite(x.high) &&
         x.low <= x.high;
}
auto ep2_scalar(const BoardingFootSiteScalarBounds& x) -> Interval {
  return x.supported ? interval(x.lower, x.upper) : failed();
}
auto ep2_axis(const EP2C& c, std::size_t axis) -> Point {
  return {ep2_scalar(c.unit_axes[axis][0]), ep2_scalar(c.unit_axes[axis][1]),
          ep2_scalar(c.unit_axes[axis][2])};
}
auto ep2_component(const BoardingPlantedLegPointBounds& b, std::size_t j)
    -> Interval {
  return interval(j == 0   ? b.lower.x
                  : j == 1 ? b.lower.y
                           : b.lower.z,
                  j == 0   ? b.upper.x
                  : j == 1 ? b.upper.y
                           : b.upper.z);
}
// Direct endpoint division encloses genuine D/L, never a rounded reciprocal.
auto ep2_unit_divide(Interval numerator, double length) -> Interval {
  if (!ep2_valid(numerator) || !std::isfinite(length) || length <= 0)
    return failed();
  if (numerator.low == 0 && numerator.high == 0) return point(0);
  const auto low = numerator.low / length, high = numerator.high / length;
  if (!std::isfinite(low) || !std::isfinite(high) ||
      low == -std::numeric_limits<double>::max() ||
      high == std::numeric_limits<double>::max())
    return failed();
  return interval(down(low), up(high));
}
[[gnu::noinline]] auto ep2_units(EP2D& d, EP2C& c, const EP2L& l, EP2R& r)
    -> bool {
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
      r = EP2R{};
      r.self_stage = EP2Stage::unit_axes;
      r.operation = static_cast<std::uint8_t>(row);
      c.unit_axis_cursor = static_cast<std::uint8_t>(row);
      if (!detail::intermediate_endpoint02_charge(
              d, r, d.work.unit_axis_operations, l.unit_axis_operations,
              EP2W::unit_axis_capacity))
        return false;
      c.unit_axis_attempted |= std::uint32_t{1} << row;
      const auto length = axis < 2 ? thigh_length : shin_length;
      if (!c.self_body_complete || !ss_leg(c.phase.legs[side]) ||
          !ep2_source_bit(d, 62) ||
          (axis < 2 ? length != .47285 : length != .47478))
        return detail::intermediate_endpoint02_refuse(d, r,
                                                      EP2W::unit_axis_identity);
      const auto result =
          operation < 3
              ? subtract(ep2_component(c.phase.points[body_index(knee)].value,
                                       component),
                         ep2_component(c.phase.points[body_index(from)].value,
                                       component))
              : ep2_unit_divide(displacement[component], length);
      if (!ep2_valid(result) ||
          (operation < 3 && (result.low < -16 || result.high > 16)))
        return detail::intermediate_endpoint02_refuse(
            d, r, EP2W::unsupported_arithmetic);
      if (operation < 3)
        displacement[component] = result;
      else
        c.unit_axes[axis][component] = {result.low, result.high, true};
      c.unit_axis_written |= std::uint32_t{1} << row;
    }
    c.unit_axis_complete_mask |=
        static_cast<std::uint8_t>(std::uint8_t{1} << axis);
  }
  return (c.unit_axis_written == kBoardingIntermediateEndpoint02UnitMask &&
          c.unit_axis_complete_mask == 15) ||
         detail::intermediate_endpoint02_refuse(d, r, EP2W::unit_axis_identity);
}
[[gnu::noinline]] auto ep2_owner(
    const EP2C& c,
    const std::array<Point, kBoardingPlantedBodyPointCount>& relative,
    const SelfRegion& region, BoardingLowerFootTransferOwner& out) -> void {
  if (region.junction != Junction::hip && region.junction != Junction::ankle) {
    ss_owner(c.phase, relative, region, out);
    return;
  }
  const auto side = static_cast<std::size_t>(region.second) >= 9
                        ? std::size_t{1}
                        : std::size_t{0};
  const auto u = ep2_axis(c, (region.junction == Junction::hip ? 0 : 2) + side);
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
        !ep2_valid(u[1]))
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
      ep2_valid(extent) && ep2_valid(limit) && ep2_valid(gap);
  if (!out.arithmetic_supported) return;
  out.extent = bounds(extent);
  out.limit = bounds(limit);
  out.secondary_extent = bounds(point(0));
  out.structural_identity = true;
  out.certificate = SelfCert::original_axis_box_support;
  out.certified = gap.low >= 0;
}
[[gnu::noinline]] auto ep2_hip(
    const EP2C& c, const SelfRegion& region, std::size_t region_index,
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
  const auto u = ep2_axis(c, side);
  for (std::size_t i = 0; i < 3; ++i) {
    if (!ep2_valid(u[i])) return;
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
[[gnu::noinline]] auto ep2_separate(const UnloadSolid& a, const UnloadSolid& b,
                                    EP2D& d, EP2C& c, const EP2L& l, EP2R& r,
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
  r.self_stage = EP2Stage::separation;
  for (std::size_t i = 0; i < count; ++i) {
    r.self_axis = static_cast<std::uint8_t>(i);
    r.self_sign = 255;
    if (!detail::intermediate_endpoint02_charge(
            d, r, d.work.self_axes, l.self_axes, EP2W::self_axis_capacity))
      return false;
    const auto axis = proposal(i);
    if (!self_direction(axis)) continue;
    for (std::size_t sign = 0; sign < 2; ++sign) {
      r.self_sign = static_cast<std::uint8_t>(sign);
      if (!detail::intermediate_endpoint02_charge(
              d, r, d.work.self_signed_trials, l.self_signed_trials,
              EP2W::self_signed_capacity))
        return false;
      const auto n = sign == 0 ? axis : self_negate(axis);
      const auto total =
          add(unload_support(a, n), unload_support(b, self_negate(n)));
      if (!total.supported || !std::isfinite(total.low) ||
          !std::isfinite(total.high))
        return detail::intermediate_endpoint02_refuse(
            d, r, EP2W::unsupported_arithmetic);
      if (total.high <= 0) {
        c.self_axes[pair] = static_cast<std::uint8_t>(i | (sign == 0 ? 0 : 16));
        return true;
      }
    }
  }
  return detail::intermediate_endpoint02_refuse(d, r,
                                                EP2W::unresolved_self_pair);
}
} // namespace
auto detail::intermediate_endpoint02_self_bridge(
    const BoardingIntermediateEndpoint02CurrentToken& token, EP2D& d, EP2C& c,
    const EP2L& l, EP2R& r) -> EP2S {
  std::size_t body_index_record{};
  const auto body = [&](auto predicate, EP2W why = EP2W::self_body_identity) {
    r = EP2R{};
    r.self_stage = EP2Stage::body;
    r.operation = static_cast<std::uint8_t>(body_index_record);
    if (!intermediate_endpoint02_charge(d, r, d.work.self_body_guards,
                                        l.self_body_guards,
                                        EP2W::self_body_capacity))
      return false;
    c.self_body_evaluated |= std::uint64_t{1} << body_index_record++;
    return predicate() || intermediate_endpoint02_refuse(d, r, why);
  };
  if (!body([&] {
        return token.context() && token.owner() == &d && token.cell() == &c &&
               token.request() && token.context()->owner() == &d &&
               token.context()->parts() == &d.parts &&
               token.context()->request() == token.request() &&
               token.context()->key() &&
               token.context()->key()->version() == 2 &&
               token.context()->key()->candidate() == d.candidate &&
               token.context()->key()->y() == d.slice.y && d.slice.complete &&
               d.work.phase_calls == 1 &&
               BoardingIntermediatePauseSupportAccess::valid(d.source) &&
               token.data() ==
                   BoardingIntermediatePauseSupportAccess::data(d.source) &&
               token.context()->data() == token.data() && token.data() &&
               d.cells.size() == 1 && d.cells.capacity() == 1 &&
               &d.cells.front() == &c && d.work.phase_calls == 1;
      }))
    return d.state;
  if (!body([&] { return boarding_route_foot_phase_environment(); },
            EP2W::unsupported_arithmetic))
    return d.state;
  const auto& p = c.phase;
  if (!body([&] {
        return p.complete && p.arithmetic_supported && p.nominal_links &&
               p.target_sole_identities && p.joint_sectors &&
               p.derivative_domains && p.timing_complete;
      }))
    return d.state;
  if (!body([&] {
        return d.candidate == BoardingIntermediateEndpoint02Candidate::
                                  root_y_reach_slice &&
               d.version == 2 && p.first == 0 && p.last == 1 &&
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
        return ep2_source_bit(d, 62) && p.nominal_links &&
               p.target_sole_identities;
      }))
    return d.state;
  if (!body([&] {
        return ep2_source_bit(d, 63) && p.arithmetic_supported &&
               c.projected_carrier_complete[0];
      }))
    return d.state;
  if (!body([&] {
        return c.nominal_support_complete && c.nominal_equilibrium &&
               c.star_support && d.stop_condition == EP2W::none;
      }))
    return d.state;
  for (std::size_t i = 0; i < 18; ++i)
    if (!body(
            [&] {
              return ss_point_valid(body_intervals(p.points[i].value), 8);
            },
            EP2W::unsupported_arithmetic))
      return d.state;
  for (std::size_t f = 0; f < 4; ++f)
    for (std::size_t j = 0; j < 3; ++j)
      if (!body(
              [&] {
                return ss_point_valid(
                    body_intervals(p.frames[f].columns[j].value), 8);
              },
              EP2W::unsupported_arithmetic))
        return d.state;
  for (std::size_t side = 0; side < 2; ++side)
    if (!body([&] {
          const auto first = side == 0 ? std::size_t{36} : std::size_t{42};
          return ss_leg(p.legs[side]) && ep2_source_bit(d, 62) &&
                 ep2_source_bit(d, first) && ep2_source_bit(d, first + 1) &&
                 ss_same_part(d.parts[first - 33], ss_part(first - 33)) &&
                 ss_same_part(d.parts[first - 32], ss_part(first - 32));
        }))
      return d.state;
  if (!body([&] {
        return ss_same_part(d.parts[0], ss_part(0)) &&
               ss_same_part(d.parts[1], ss_part(1)) && ep2_source_bit(d, 33) &&
               ep2_source_bit(d, 34) && ep2_source_bit(d, 48);
      }))
    return d.state;
  if (!body([&] {
        return ss_same_part(d.parts[1], ss_part(1)) &&
               ss_same_part(d.parts[2], ss_part(2)) && ep2_source_bit(d, 34) &&
               ep2_source_bit(d, 35) && ep2_source_bit(d, 51) &&
               ep2_source_bit(d, 63);
      }))
    return d.state;
  if (!body([&] {
        if (!ep2_source_bit(d, 63)) return false;
        for (const auto part :
             {std::size_t{6}, std::size_t{7}, std::size_t{8}, std::size_t{12},
              std::size_t{13}, std::size_t{14}})
          if (!ep2_source_bit(d, 33 + part) ||
              !ss_same_part(d.parts[part], ss_part(part)))
            return false;
        for (const auto region :
             {std::size_t{4}, std::size_t{5}, std::size_t{8}, std::size_t{9},
              std::size_t{12}, std::size_t{13}})
          if (!ep2_source_bit(d, 48 + region)) return false;
        return true;
      }))
    return d.state;
  SelfRegions regions{};
  if (!body([&] {
        regions = self_regions();
        for (std::size_t i = 0; i < 14; ++i)
          if (!ep2_source_bit(d, 48 + i) ||
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
            EP2W::unsupported_arithmetic))
      return d.state;
  if (!body([&] {
        return body_index_record == 63 &&
               c.self_body_evaluated ==
                   kBoardingIntermediateEndpoint02BodyMask &&
               token.owner() == &d && token.cell() == &c &&
               token.data() ==
                   BoardingIntermediatePauseSupportAccess::data(d.source);
      }))
    return d.state;
  c.self_body_complete = true;
  if (!ep2_units(d, c, l, r)) return d.state;
  std::size_t pair{};
  for (std::size_t a = 0; a < 15; ++a)
    for (std::size_t b = a + 1; b < 15; ++b, ++pair) {
      r = EP2R{};
      r.self_stage = EP2Stage::not_run;
      r.self_pair = static_cast<std::uint16_t>(pair);
      r.self_region = 255;
      r.self_axis = 255;
      r.self_sign = 255;
      r.self_stage = EP2Stage::pair;
      if (!intermediate_endpoint02_charge(d, r, d.work.self_pairs, l.self_pairs,
                                          EP2W::self_pair_capacity))
        return d.state;
      ++c.examined_pairs;
      auto certificate = EP2Cert::not_run;
      for (std::size_t region = 0; region < 14; ++region)
        if (static_cast<std::size_t>(regions[region].first) == a &&
            static_cast<std::size_t>(regions[region].second) == b) {
          r.self_stage = EP2Stage::owner;
          r.self_region = static_cast<std::uint8_t>(region);
          if (!intermediate_endpoint02_charge(d, r, d.work.self_owners,
                                              l.self_owners,
                                              EP2W::self_owner_capacity))
            return d.state;
          c.owner_attempted_mask |=
              static_cast<std::uint16_t>(std::uint16_t{1} << region);
          ep2_owner(c, relative, regions[region], c.owners[region]);
          if (!c.owners[region].arithmetic_supported) {
            intermediate_endpoint02_refuse(d, r, EP2W::unsupported_arithmetic);
            return d.state;
          }
          if (c.owners[region].certified)
            certificate = static_cast<EP2Cert>(c.owners[region].certificate);
          else if (regions[region].junction == Junction::hip) {
            r.self_stage = EP2Stage::hip;
            if (!intermediate_endpoint02_charge(
                    d, r, d.work.self_hip_complements, l.self_hip_complements,
                    EP2W::self_hip_capacity))
              return d.state;
            const auto side = b >= 9 ? std::size_t{1} : std::size_t{0};
            ep2_hip(c, regions[region], region, pair, c.hip_complements[side]);
            if (!c.hip_complements[side].arithmetic_supported) {
              intermediate_endpoint02_refuse(d, r,
                                             EP2W::unsupported_arithmetic);
              return d.state;
            }
            if (c.hip_complements[side].certified)
              certificate = EP2Cert::original_capsule_slab_complement;
          }
          break;
        }
      if (certificate == EP2Cert::not_run) {
        const auto first = unload_solid(d.parts[a], relative, p),
                   second = unload_solid(d.parts[b], relative, p);
        if (!ep2_separate(first, second, d, c, l, r, pair)) return d.state;
        certificate = EP2Cert::convex_support_plane;
      }
      c.self_certificates[pair] = certificate;
      ++c.self_certificate_counts[static_cast<std::size_t>(certificate)];
      ++c.accepted_pairs;
    }
  c.self_complete = pair == 105 && c.accepted_pairs == 105;
  if (!c.self_complete) {
    intermediate_endpoint02_refuse(d, r, EP2W::incomplete_endpoint);
    return d.state;
  }
  r.self_pair = 65535;
  r.self_region = 255;
  r.self_axis = 255;
  r.self_sign = 255;
  r.self_stage = EP2Stage::complete;
  c.complete = true;
  c.state = d.state = EP2S::accepted;
  return d.state;
}
} // namespace apsis_drift

namespace apsis_drift {
namespace {
using SliceWhy = BoardingIntermediateEndpoint02SliceCondition;
using EP2Request = BoardingRouteFootPhaseRequest;
auto ep2_packet(const BoardingRoutePhaseConstant& c,
                const std::array<double, 3>& terms, std::size_t count) -> bool {
  return c.count == count && c.terms == terms;
}
auto ep2_packet_domains(const EP2Request& request) -> bool {
  const auto valid = [](const BoardingRoutePhaseConstant& c) {
    if (c.count == 0 || c.count > 3) return false;
    for (std::size_t i = 0; i < 3; ++i)
      if (!std::isfinite(c.terms[i]) || std::abs(c.terms[i]) > 8 ||
          (i >= c.count && (c.terms[i] != 0 || std::signbit(c.terms[i]))))
        return false;
    return true;
  };
  for (const auto& root : request.root)
    for (const auto& c : root.coordinates)
      if (!valid(c)) return false;
  for (const auto& foot : request.feet)
    for (const auto& sole : foot.sole)
      for (const auto& c : sole.coordinates)
        if (!valid(c)) return false;
  return std::isfinite(request.seconds_per_parameter) &&
         std::isfinite(request.root_yaw_half[0]) &&
         std::isfinite(request.root_yaw_half[1]) &&
         std::isfinite(request.torso_lean_half[0]) &&
         std::isfinite(request.torso_lean_half[1]) &&
         std::isfinite(request.port_reaction_fraction[0]) &&
         std::isfinite(request.port_reaction_fraction[1]);
}
auto ep2_construction_packet_domains(const EP2Request& request) -> bool {
  for (std::size_t i = 0; i < 8; ++i) {
    const auto& c =
        i < 2 ? request.root[0].coordinates[i == 0 ? 0 : 2]
              : request.feet[i < 5 ? 0 : 1].sole[0].coordinates[(i - 2) % 3];
    if (c.count == 0 || c.count > 3) return false;
    for (std::size_t j = 0; j < 3; ++j)
      if (!std::isfinite(c.terms[j]) || std::abs(c.terms[j]) > 8 ||
          (j >= c.count && (c.terms[j] != 0 || std::signbit(c.terms[j]))))
        return false;
  }
  return true;
}
auto ep2_slice_stop(EP2D& d, EP2R& r, EP2W why, SliceWhy condition) -> bool {
  d.slice.condition = condition;
  r.limiting_bound = d.slice.limiting_bound;
  return detail::intermediate_endpoint02_refuse(d, r, why);
}
} // namespace
// This is a literal packet predicate, not a source issuer or new factory.
auto detail::intermediate_endpoint02_template_valid(const EP2Request& request,
                                                    bool generated, double y)
    -> bool {
  for (const auto& root : request.root) {
    if (!ep2_packet(root.coordinates[0], {.16, .16, -.0075}, 3) ||
        !ep2_packet(root.coordinates[2], {-.55, -.17, .19}, 3) ||
        !(generated ? ep2_packet(root.coordinates[1], {y, 0, 0}, 1)
                    : ep2_packet(root.coordinates[1],
                                 {.847, .012, (-.359) / 2.0}, 3)))
      return false;
  }
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& foot = request.feet[side];
    for (const auto& sole : foot.sole) {
      if (side == 0) {
        if (!ep2_packet(sole.coordinates[0], {.16, -.14, .16}, 3) ||
            !ep2_packet(sole.coordinates[1],
                        {static_cast<double>(-230000) * 1e-6, 0, 0}, 1) ||
            !ep2_packet(sole.coordinates[2], {-1.14, 0, 0}, 1))
          return false;
      } else {
        if (!ep2_packet(sole.coordinates[0], {.16, .14, 0}, 2) ||
            !ep2_packet(sole.coordinates[1],
                        {static_cast<double>(-160000) * 1e-6, 0, 0}, 1) ||
            !ep2_packet(sole.coordinates[2], {-.8, 0, 0}, 1))
          return false;
      }
    }
    if (foot.yaw_half != std::array<double, 2>{0, 0} ||
        foot.swing_height_metres != 0)
      return false;
  }
  return request.root_yaw_half == std::array<double, 2>{.125, .125} &&
         request.torso_lean_half == std::array<double, 2>{-.30, -.30} &&
         request.port_reaction_fraction ==
             std::array<double, 2>{.0625, .0625} &&
         request.seconds_per_parameter == 2;
}
[[gnu::noinline]] auto detail::intermediate_endpoint02_construct(
    EP2D& d, EP2Request& request, const EP2L& limits, EP2R& r) -> bool {
  // Scalar construction only: no PhaseCell, region catalog or owning return.
  std::array<Interval, 8> input{};
  std::array<Interval, 6> yaw{};
  std::array<Interval, 4> ca{};
  std::array<Interval, 2> thresholds{};
  std::array<Interval, 8> leg{};
  std::array<Interval, 4> proof{};
  std::array<double, 6> selected{};
  const auto guard = [&](std::uint8_t row, std::uint8_t side, auto predicate,
                         SliceWhy ordinary) {
    r = EP2R{};
    r.self_stage = EP2Stage::slice_guard;
    r.operation = row;
    if (side < 2) r.side = side;
    d.slice.guard = row;
    d.slice.side = side;
    d.slice.limiting_bound = {};
    if (!intermediate_endpoint02_charge(d, r, d.work.construction_guards,
                                        limits.construction_guards,
                                        EP2W::slice_guard_capacity)) {
      d.slice.condition = SliceWhy::guard_capacity;
      return false;
    }
    d.slice.guard_attempted |= std::uint32_t{1} << row;
    EP2W why = ordinary == SliceWhy::identity ? EP2W::slice_identity
                                              : EP2W::slice_unavailable;
    const bool good = predicate(why);
    if (!good)
      return ep2_slice_stop(d, r, why,
                            why == EP2W::unsupported_arithmetic
                                ? SliceWhy::unsupported_arithmetic
                                : ordinary);
    d.slice.guard_written |= std::uint32_t{1} << row;
    return true;
  };
  const auto operation = [&](std::uint8_t row, std::uint8_t side,
                             Interval& output, auto compute) {
    r = EP2R{};
    r.self_stage = EP2Stage::slice_operation;
    r.operation = row;
    if (side < 2) r.side = side;
    d.slice.operation = row;
    d.slice.side = side;
    d.slice.limiting_bound = {};
    if (!intermediate_endpoint02_charge(d, r, d.work.construction_operations,
                                        limits.construction_operations,
                                        EP2W::slice_operation_capacity)) {
      d.slice.condition = SliceWhy::operation_capacity;
      return false;
    }
    d.slice.operation_attempted |= std::uint64_t{1} << row;
    const auto value = compute();
    if (!ep2_valid(value))
      return ep2_slice_stop(d, r, EP2W::unsupported_arithmetic,
                            SliceWhy::unsupported_arithmetic);
    output = value;
    d.slice.limiting_bound = {value.low, value.high, true};
    d.slice.operation_written |= std::uint64_t{1} << row;
    d.slice.arithmetic_supported = true;
    return true;
  };
  if (!guard(
          0, 255,
          [&](EP2W&) {
            return d.source_enrolled && d.source_evaluated == UINT64_MAX &&
                   BoardingIntermediatePauseSupportAccess::valid(d.source) &&
                   BoardingIntermediatePauseSupportAccess::data(d.source) &&
                   d.parts.size() == 15;
          },
          SliceWhy::identity))
    return false;
  if (!guard(
          1, 255,
          [&](EP2W& why) {
            why = EP2W::unsupported_arithmetic;
            return boarding_route_foot_phase_environment();
          },
          SliceWhy::unsupported_arithmetic))
    return false;
  if (!guard(
          2, 255,
          [&](EP2W&) {
            return d.version == 2 &&
                   d.candidate == BoardingIntermediateEndpoint02Candidate::
                                      root_y_reach_slice &&
                   intermediate_endpoint02_template_valid(request, false, 0);
          },
          SliceWhy::identity))
    return false;
  if (!guard(
          3, 255,
          [&](EP2W& why) {
            why = EP2W::unsupported_arithmetic;
            return ep2_construction_packet_domains(request);
          },
          SliceWhy::identity))
    return false;
  if (!guard(
          4, 255,
          [&](EP2W& why) {
            if (!std::isfinite(thigh_length) || !std::isfinite(shin_length)) {
              why = EP2W::unsupported_arithmetic;
              return false;
            }
            return kBoardingRouteFootPhaseVersion == 1 &&
                   thigh_length == .47285 && shin_length == .47478 &&
                   thigh_length > 0 && shin_length > 0 && thigh_length <= 1 &&
                   shin_length <= 1 && ep2_source_bit(d, 62);
          },
          SliceWhy::identity))
    return false;
  if (!guard(
          5, 255,
          [&](EP2W&) {
            // The source compiler's exact-real nominal rational yaw
            // construction, not midpoint or rounded affine orthogonality, owns
            // these identities.
            return ep2_source_bit(d, 63) &&
                   kBoardingRouteFootPhaseVersion == 1 &&
                   intermediate_endpoint02_template_valid(request, false, 0) &&
                   request.feet[0].yaw_half == std::array<double, 2>{0, 0} &&
                   request.feet[1].yaw_half == std::array<double, 2>{0, 0};
          },
          SliceWhy::identity))
    return false;
  // O01--O08: each actual CONST is called only after its own charge.
  for (std::uint8_t i = 0; i < 8; ++i)
    if (!operation(i,
                   i < 2   ? 255
                   : i < 5 ? 0
                           : 1,
                   input[i], [&] {
                     const auto& coordinate =
                         i < 2 ? request.root[0].coordinates[i == 0 ? 0 : 2]
                               : request.feet[i < 5 ? 0 : 1]
                                     .sole[0]
                                     .coordinates[(i - 2) % 3];
                     return phase_constant(coordinate);
                   }))
      return false;
  if (!operation(8, 255, yaw[0],
                 [&] { return square(point(request.root_yaw_half[0])); }) ||
      !operation(9, 255, yaw[1], [&] { return add(point(1), yaw[0]); }) ||
      !operation(10, 255, yaw[2], [&] { return subtract(point(1), yaw[0]); }) ||
      !operation(11, 255, yaw[3], [&] { return divide(yaw[2], yaw[1]); }) ||
      !operation(12, 255, yaw[2],
                 [&] {
                   return multiply(point(2), point(request.root_yaw_half[0]));
                 }) ||
      !operation(13, 255, yaw[4], [&] { return divide(yaw[2], yaw[1]); }) ||
      !operation(14, 255, yaw[5], [&] { return negate(yaw[4]); }))
    return false;
  for (std::uint8_t side = 0; side < 2; ++side) {
    const auto base = static_cast<std::uint8_t>(side == 0 ? 15 : 25);
    if (!operation(
            base, side, leg[0],
            [&] { return multiply(point(side == 0 ? -.14 : .14), yaw[3]); }) ||
        !operation(
            static_cast<std::uint8_t>(base + 1), side, leg[1],
            [&] { return multiply(point(side == 0 ? -.14 : .14), yaw[5]); }) ||
        !operation(static_cast<std::uint8_t>(base + 2), side, leg[2],
                   [&] { return add(input[0], leg[0]); }) ||
        !operation(static_cast<std::uint8_t>(base + 3), side, leg[3],
                   [&] { return add(input[1], leg[1]); }) ||
        !operation(static_cast<std::uint8_t>(base + 4), side, leg[4],
                   [&] { return subtract(input[2 + 3 * side], leg[2]); }) ||
        !operation(static_cast<std::uint8_t>(base + 5), side, leg[5],
                   [&] { return subtract(input[4 + 3 * side], leg[3]); }) ||
        !operation(static_cast<std::uint8_t>(base + 6), side, leg[6],
                   [&] { return square(leg[4]); }) ||
        !operation(static_cast<std::uint8_t>(base + 7), side, leg[7],
                   [&] { return square(leg[5]); }) ||
        !operation(static_cast<std::uint8_t>(base + 8), side,
                   ca[std::size_t{2} * side],
                   [&] { return add(leg[6], leg[7]); }) ||
        !operation(static_cast<std::uint8_t>(base + 9), side, ca[2 * side + 1],
                   [&] { return add(input[3 + 3 * side], point(.1)); }))
      return false;
  }
  if (!operation(
          35, 255, thresholds[0],
          [&] { return add(point(thigh_length), point(shin_length)); }) ||
      !operation(36, 255, thresholds[0],
                 [&] { return square(thresholds[0]); }) ||
      !operation(
          37, 255, thresholds[1],
          [&] { return subtract(point(thigh_length), point(shin_length)); }) ||
      !operation(38, 255, thresholds[1], [&] { return square(thresholds[1]); }))
    return false;
  for (std::uint8_t side = 0; side < 2; ++side) {
    const auto base = static_cast<std::uint8_t>(side == 0 ? 39 : 46);
    if (!operation(base, side, leg[0], [&] {
          return subtract(point(thresholds[1].high),
                          point(ca[std::size_t{2} * side].low));
        }))
      return false;
    bool zero_branch{};
    if (!operation(static_cast<std::uint8_t>(base + 1), side, leg[1], [&] {
          zero_branch = leg[0].high <= 0;
          return point(zero_branch ? +0.0 : leg[0].high);
        }))
      return false;
    if (zero_branch)
      d.slice.zero_mask |= static_cast<std::uint8_t>(std::uint8_t{1} << side);
    if (!operation(static_cast<std::uint8_t>(base + 2), side, leg[2],
                   [&] { return transfer_small_root(leg[1]); }) ||
        !operation(static_cast<std::uint8_t>(base + 3), side, leg[3], [&] {
          return add(point(ca[2 * side + 1].high), point(leg[2].high));
        }))
      return false;
    selected[std::size_t{2} * side] = leg[3].high;
    if (!operation(static_cast<std::uint8_t>(base + 4), side, leg[4], [&] {
          return subtract(point(thresholds[0].low),
                          point(ca[std::size_t{2} * side].high));
        }))
      return false;
    if (!guard(
            static_cast<std::uint8_t>(6 + side), side,
            [&](EP2W& why) {
              d.slice.limiting_bound = {leg[4].low, leg[4].high,
                                        leg[4].supported};
              if (!ep2_valid(leg[4]) || leg[4].high > 16384) {
                why = EP2W::unsupported_arithmetic;
                return false;
              }
              return leg[4].low > 0;
            },
            SliceWhy::no_positive_upper))
      return false;
    if (!operation(static_cast<std::uint8_t>(base + 5), side, leg[5],
                   [&] { return transfer_small_root(point(leg[4].low)); }) ||
        !operation(static_cast<std::uint8_t>(base + 6), side, leg[6], [&] {
          return add(point(ca[2 * side + 1].low), point(leg[5].low));
        }))
      return false;
    selected[2 * side + 1] = leg[6].low;
  }
  if (!operation(53, 255, leg[0], [&] {
        return point(selected[0] < selected[2] ? selected[2] : selected[0]);
      }))
    return false;
  d.slice.lo = leg[0].low;
  if (!operation(54, 255, leg[1], [&] {
        return point(selected[3] < selected[1] ? selected[3] : selected[1]);
      }))
    return false;
  d.slice.hi = leg[1].low;
  if (!guard(
          8, 255,
          [&](EP2W& why) {
            d.slice.limiting_bound = {d.slice.lo, d.slice.lo,
                                      std::isfinite(d.slice.lo)};
            if (!std::isfinite(d.slice.lo) || !std::isfinite(d.slice.hi)) {
              why = EP2W::unsupported_arithmetic;
              return false;
            }
            return d.slice.lo < d.slice.hi;
          },
          SliceWhy::empty_inward_interval))
    return false;
  if (!operation(55, 255, leg[2],
                 [&] { return point(d.slice.hi - d.slice.lo); }) ||
      !operation(56, 255, leg[3], [&] { return point(leg[2].low * .5); }) ||
      !operation(57, 255, leg[4],
                 [&] { return point(d.slice.lo + leg[3].low); }))
    return false;
  d.slice.y = leg[4].low;
  if (!guard(
          9, 255,
          [&](EP2W& why) {
            d.slice.limiting_bound = {d.slice.y, d.slice.y,
                                      std::isfinite(d.slice.y)};
            if (!std::isfinite(d.slice.y)) {
              why = EP2W::unsupported_arithmetic;
              return false;
            }
            return std::abs(d.slice.y) <= 8;
          },
          SliceWhy::candidate_domain) ||
      !guard(
          10, 255,
          [&](EP2W&) {
            d.slice.limiting_bound = {d.slice.y, d.slice.y, true};
            return d.slice.y > d.slice.lo;
          },
          SliceWhy::midpoint_unavailable) ||
      !guard(
          11, 255,
          [&](EP2W&) {
            d.slice.limiting_bound = {d.slice.y, d.slice.y, true};
            return d.slice.y < d.slice.hi;
          },
          SliceWhy::midpoint_unavailable))
    return false;
  for (std::uint8_t side = 0; side < 2; ++side) {
    const auto base = static_cast<std::uint8_t>(58 + 3 * side);
    if (!operation(
            base, side, proof[0],
            [&] { return subtract(point(d.slice.y), ca[2 * side + 1]); }) ||
        !operation(static_cast<std::uint8_t>(base + 1), side, proof[1],
                   [&] { return square(proof[0]); }) ||
        !operation(static_cast<std::uint8_t>(base + 2), side, proof[2 + side],
                   [&] { return add(ca[std::size_t{2} * side], proof[1]); }))
      return false;
    if (!guard(
            static_cast<std::uint8_t>(12 + 2 * side), side,
            [&](EP2W&) {
              const auto& a = ca[2 * side + 1];
              d.slice.limiting_bound = {a.low, a.high, a.supported};
              return d.slice.y > a.high;
            },
            SliceWhy::verification_inconclusive) ||
        !guard(
            static_cast<std::uint8_t>(13 + 2 * side), side,
            [&](EP2W& why) {
              const auto& distance = proof[2 + side];
              d.slice.limiting_bound = {distance.low, distance.high,
                                        distance.supported};
              if (!ep2_valid(distance)) {
                why = EP2W::unsupported_arithmetic;
                return false;
              }
              return distance.low > thresholds[1].high &&
                     distance.high < thresholds[0].low;
            },
            SliceWhy::verification_inconclusive))
      return false;
  }
  if (!guard(
          16, 255,
          [&](EP2W&) {
            if (!intermediate_endpoint02_template_valid(request, false, 0))
              return false;
            for (auto& root : request.root)
              root.coordinates[1] = {{d.slice.y, 0, 0}, 1};
            return intermediate_endpoint02_template_valid(request, true,
                                                          d.slice.y);
          },
          SliceWhy::identity))
    return false;
  if (!guard(
          17, 255,
          [&](EP2W& why) {
            if (!boarding_route_foot_phase_environment()) {
              why = EP2W::unsupported_arithmetic;
              return false;
            }
            if (!ep2_packet_domains(request)) {
              why = EP2W::unsupported_arithmetic;
              return false;
            }
            if (!intermediate_endpoint02_template_valid(request, true,
                                                        d.slice.y))
              return false;
            const auto valid = boarding_route_foot_phase_request_valid(request);
            if (!valid) {
              why = EP2W::unsupported_arithmetic;
              return false;
            }
            if (!boarding_route_foot_phase_environment()) {
              why = EP2W::unsupported_arithmetic;
              return false;
            }
            return true;
          },
          SliceWhy::identity))
    return false;
  d.slice.complete = d.slice.operation_written == UINT64_MAX &&
                     d.slice.guard_written == 0x0003ffffU;
  return d.slice.complete;
}
} // namespace apsis_drift

#include "origin_boarding_intermediate_endpoint03_internal.hpp"
namespace apsis_drift {
namespace {
using EP3D = BoardingIntermediateEndpoint03Diagnostic;
using EP3C = BoardingIntermediateEndpoint03Cell;
using EP3R = BoardingIntermediateEndpoint03Refusal;
using EP3L = detail::BoardingIntermediateEndpoint03Limits;
using EP3W = BoardingIntermediateEndpoint03Condition;
using EP3S = BoardingIntermediateEndpoint03State;
using EP3Stage = BoardingIntermediateEndpoint03SelfStage;
using EP3Cert = BoardingIntermediateEndpoint03Certificate;
auto ep3_source_bit(const EP3D& d, std::size_t i) -> bool {
  return (d.source_evaluated & (std::uint64_t{1} << i)) != 0;
}
auto ep3_valid(Interval x) -> bool {
  return x.supported && std::isfinite(x.low) && std::isfinite(x.high) &&
         x.low <= x.high;
}
auto ep3_scalar(const BoardingFootSiteScalarBounds& x) -> Interval {
  return x.supported ? interval(x.lower, x.upper) : failed();
}
auto ep3_axis(const EP3C& c, std::size_t axis) -> Point {
  return {ep3_scalar(c.unit_axes[axis][0]), ep3_scalar(c.unit_axes[axis][1]),
          ep3_scalar(c.unit_axes[axis][2])};
}
auto ep3_component(const BoardingPlantedLegPointBounds& b, std::size_t j)
    -> Interval {
  return interval(j == 0   ? b.lower.x
                  : j == 1 ? b.lower.y
                           : b.lower.z,
                  j == 0   ? b.upper.x
                  : j == 1 ? b.upper.y
                           : b.upper.z);
}
// Direct endpoint division encloses genuine D/L, never a rounded reciprocal.
auto ep3_unit_divide(Interval numerator, double length) -> Interval {
  if (!ep3_valid(numerator) || !std::isfinite(length) || length <= 0)
    return failed();
  if (numerator.low == 0 && numerator.high == 0) return point(0);
  const auto low = numerator.low / length, high = numerator.high / length;
  if (!std::isfinite(low) || !std::isfinite(high) ||
      low == -std::numeric_limits<double>::max() ||
      high == std::numeric_limits<double>::max())
    return failed();
  return interval(down(low), up(high));
}
[[gnu::noinline]] auto ep3_units(EP3D& d, EP3C& c, const EP3L& l, EP3R& r)
    -> bool {
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
      r = EP3R{};
      r.self_stage = EP3Stage::unit_axes;
      r.operation = static_cast<std::uint8_t>(row);
      c.unit_axis_cursor = static_cast<std::uint8_t>(row);
      if (!detail::intermediate_endpoint03_charge(
              d, r, d.work.unit_axis_operations, l.unit_axis_operations,
              EP3W::unit_axis_capacity))
        return false;
      c.unit_axis_attempted |= std::uint32_t{1} << row;
      const auto length = axis < 2 ? thigh_length : shin_length;
      if (!c.self_body_complete || !ss_leg(c.phase.legs[side]) ||
          !ep3_source_bit(d, 62) ||
          (axis < 2 ? length != .47285 : length != .47478))
        return detail::intermediate_endpoint03_refuse(d, r,
                                                      EP3W::unit_axis_identity);
      const auto result =
          operation < 3
              ? subtract(ep3_component(c.phase.points[body_index(knee)].value,
                                       component),
                         ep3_component(c.phase.points[body_index(from)].value,
                                       component))
              : ep3_unit_divide(displacement[component], length);
      if (!ep3_valid(result) ||
          (operation < 3 && (result.low < -16 || result.high > 16)))
        return detail::intermediate_endpoint03_refuse(
            d, r, EP3W::unsupported_arithmetic);
      if (operation < 3)
        displacement[component] = result;
      else
        c.unit_axes[axis][component] = {result.low, result.high, true};
      c.unit_axis_written |= std::uint32_t{1} << row;
    }
    c.unit_axis_complete_mask |=
        static_cast<std::uint8_t>(std::uint8_t{1} << axis);
  }
  return (c.unit_axis_written == kBoardingIntermediateEndpoint03UnitMask &&
          c.unit_axis_complete_mask == 15) ||
         detail::intermediate_endpoint03_refuse(d, r, EP3W::unit_axis_identity);
}
[[gnu::noinline]] auto ep3_owner(
    const EP3C& c,
    const std::array<Point, kBoardingPlantedBodyPointCount>& relative,
    const SelfRegion& region, BoardingLowerFootTransferOwner& out) -> void {
  if (region.junction != Junction::hip && region.junction != Junction::ankle) {
    ss_owner(c.phase, relative, region, out);
    return;
  }
  const auto side = static_cast<std::size_t>(region.second) >= 9
                        ? std::size_t{1}
                        : std::size_t{0};
  const auto u = ep3_axis(c, (region.junction == Junction::hip ? 0 : 2) + side);
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
        !ep3_valid(u[1]))
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
      ep3_valid(extent) && ep3_valid(limit) && ep3_valid(gap);
  if (!out.arithmetic_supported) return;
  out.extent = bounds(extent);
  out.limit = bounds(limit);
  out.secondary_extent = bounds(point(0));
  out.structural_identity = true;
  out.certificate = SelfCert::original_axis_box_support;
  out.certified = gap.low >= 0;
}
[[gnu::noinline]] auto ep3_hip(
    const EP3C& c, const SelfRegion& region, std::size_t region_index,
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
  const auto u = ep3_axis(c, side);
  for (std::size_t i = 0; i < 3; ++i) {
    if (!ep3_valid(u[i])) return;
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
[[gnu::noinline]] auto ep3_separate(const UnloadSolid& a, const UnloadSolid& b,
                                    EP3D& d, EP3C& c, const EP3L& l, EP3R& r,
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
  r.self_stage = EP3Stage::separation;
  for (std::size_t i = 0; i < count; ++i) {
    r.self_axis = static_cast<std::uint8_t>(i);
    r.self_sign = 255;
    if (!detail::intermediate_endpoint03_charge(
            d, r, d.work.self_axes, l.self_axes, EP3W::self_axis_capacity))
      return false;
    const auto axis = proposal(i);
    if (!self_direction(axis)) continue;
    for (std::size_t sign = 0; sign < 2; ++sign) {
      r.self_sign = static_cast<std::uint8_t>(sign);
      if (!detail::intermediate_endpoint03_charge(
              d, r, d.work.self_signed_trials, l.self_signed_trials,
              EP3W::self_signed_capacity))
        return false;
      const auto n = sign == 0 ? axis : self_negate(axis);
      const auto total =
          add(unload_support(a, n), unload_support(b, self_negate(n)));
      if (!total.supported || !std::isfinite(total.low) ||
          !std::isfinite(total.high))
        return detail::intermediate_endpoint03_refuse(
            d, r, EP3W::unsupported_arithmetic);
      if (total.high <= 0) {
        c.self_axes[pair] = static_cast<std::uint8_t>(i | (sign == 0 ? 0 : 16));
        return true;
      }
    }
  }
  return detail::intermediate_endpoint03_refuse(d, r,
                                                EP3W::unresolved_self_pair);
}
} // namespace
auto detail::intermediate_endpoint03_self_bridge(
    const BoardingIntermediateEndpoint03CurrentToken& token, EP3D& d, EP3C& c,
    const EP3L& l, EP3R& r) -> EP3S {
  std::size_t body_index_record{};
  const auto body = [&](auto predicate, EP3W why = EP3W::self_body_identity) {
    r = EP3R{};
    r.self_stage = EP3Stage::body;
    r.operation = static_cast<std::uint8_t>(body_index_record);
    if (!intermediate_endpoint03_charge(d, r, d.work.self_body_guards,
                                        l.self_body_guards,
                                        EP3W::self_body_capacity))
      return false;
    c.self_body_evaluated |= std::uint64_t{1} << body_index_record++;
    return predicate() || intermediate_endpoint03_refuse(d, r, why);
  };
  if (!body([&] {
        return token.context() && token.owner() == &d && token.cell() == &c &&
               token.request() && token.context()->owner() == &d &&
               token.context()->parts() == &d.parts &&
               token.context()->request() == token.request() &&
               token.context()->key() &&
               token.context()->key()->version() == 3 &&
               token.context()->key()->candidate() == d.candidate &&
               token.context()->key()->y() == d.slice.y && d.slice.complete &&
               d.work.phase_calls == 1 &&
               BoardingIntermediatePauseSupportAccess::valid(d.source) &&
               token.data() ==
                   BoardingIntermediatePauseSupportAccess::data(d.source) &&
               token.context()->data() == token.data() && token.data() &&
               d.cells.size() == 1 && d.cells.capacity() == 1 &&
               &d.cells.front() == &c && d.work.phase_calls == 1;
      }))
    return d.state;
  if (!body([&] { return boarding_route_foot_phase_environment(); },
            EP3W::unsupported_arithmetic))
    return d.state;
  const auto& p = c.phase;
  if (!body([&] {
        return p.complete && p.arithmetic_supported && p.nominal_links &&
               p.target_sole_identities && p.joint_sectors &&
               p.derivative_domains && p.timing_complete;
      }))
    return d.state;
  if (!body([&] {
        return d.candidate == BoardingIntermediateEndpoint03Candidate::
                                  root_y_reach_roll_slice &&
               d.version == 3 && p.first == 0 && p.last == 1 &&
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
        return ep3_source_bit(d, 62) && p.nominal_links &&
               p.target_sole_identities;
      }))
    return d.state;
  if (!body([&] {
        return ep3_source_bit(d, 63) && p.arithmetic_supported &&
               c.projected_carrier_complete[0];
      }))
    return d.state;
  if (!body([&] {
        return c.nominal_support_complete && c.nominal_equilibrium &&
               c.star_support && d.stop_condition == EP3W::none;
      }))
    return d.state;
  for (std::size_t i = 0; i < 18; ++i)
    if (!body(
            [&] {
              return ss_point_valid(body_intervals(p.points[i].value), 8);
            },
            EP3W::unsupported_arithmetic))
      return d.state;
  for (std::size_t f = 0; f < 4; ++f)
    for (std::size_t j = 0; j < 3; ++j)
      if (!body(
              [&] {
                return ss_point_valid(
                    body_intervals(p.frames[f].columns[j].value), 8);
              },
              EP3W::unsupported_arithmetic))
        return d.state;
  for (std::size_t side = 0; side < 2; ++side)
    if (!body([&] {
          const auto first = side == 0 ? std::size_t{36} : std::size_t{42};
          return ss_leg(p.legs[side]) && ep3_source_bit(d, 62) &&
                 ep3_source_bit(d, first) && ep3_source_bit(d, first + 1) &&
                 ss_same_part(d.parts[first - 33], ss_part(first - 33)) &&
                 ss_same_part(d.parts[first - 32], ss_part(first - 32));
        }))
      return d.state;
  if (!body([&] {
        return ss_same_part(d.parts[0], ss_part(0)) &&
               ss_same_part(d.parts[1], ss_part(1)) && ep3_source_bit(d, 33) &&
               ep3_source_bit(d, 34) && ep3_source_bit(d, 48);
      }))
    return d.state;
  if (!body([&] {
        return ss_same_part(d.parts[1], ss_part(1)) &&
               ss_same_part(d.parts[2], ss_part(2)) && ep3_source_bit(d, 34) &&
               ep3_source_bit(d, 35) && ep3_source_bit(d, 51) &&
               ep3_source_bit(d, 63);
      }))
    return d.state;
  if (!body([&] {
        if (!ep3_source_bit(d, 63)) return false;
        for (const auto part :
             {std::size_t{6}, std::size_t{7}, std::size_t{8}, std::size_t{12},
              std::size_t{13}, std::size_t{14}})
          if (!ep3_source_bit(d, 33 + part) ||
              !ss_same_part(d.parts[part], ss_part(part)))
            return false;
        for (const auto region :
             {std::size_t{4}, std::size_t{5}, std::size_t{8}, std::size_t{9},
              std::size_t{12}, std::size_t{13}})
          if (!ep3_source_bit(d, 48 + region)) return false;
        return true;
      }))
    return d.state;
  SelfRegions regions{};
  if (!body([&] {
        regions = self_regions();
        for (std::size_t i = 0; i < 14; ++i)
          if (!ep3_source_bit(d, 48 + i) ||
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
            EP3W::unsupported_arithmetic))
      return d.state;
  if (!body([&] {
        return body_index_record == 63 &&
               c.self_body_evaluated ==
                   kBoardingIntermediateEndpoint03BodyMask &&
               token.owner() == &d && token.cell() == &c &&
               token.data() ==
                   BoardingIntermediatePauseSupportAccess::data(d.source);
      }))
    return d.state;
  c.self_body_complete = true;
  if (!ep3_units(d, c, l, r)) return d.state;
  std::size_t pair{};
  for (std::size_t a = 0; a < 15; ++a)
    for (std::size_t b = a + 1; b < 15; ++b, ++pair) {
      r = EP3R{};
      r.self_stage = EP3Stage::not_run;
      r.self_pair = static_cast<std::uint16_t>(pair);
      r.self_region = 255;
      r.self_axis = 255;
      r.self_sign = 255;
      r.self_stage = EP3Stage::pair;
      if (!intermediate_endpoint03_charge(d, r, d.work.self_pairs, l.self_pairs,
                                          EP3W::self_pair_capacity))
        return d.state;
      ++c.examined_pairs;
      auto certificate = EP3Cert::not_run;
      for (std::size_t region = 0; region < 14; ++region)
        if (static_cast<std::size_t>(regions[region].first) == a &&
            static_cast<std::size_t>(regions[region].second) == b) {
          r.self_stage = EP3Stage::owner;
          r.self_region = static_cast<std::uint8_t>(region);
          if (!intermediate_endpoint03_charge(d, r, d.work.self_owners,
                                              l.self_owners,
                                              EP3W::self_owner_capacity))
            return d.state;
          c.owner_attempted_mask |=
              static_cast<std::uint16_t>(std::uint16_t{1} << region);
          ep3_owner(c, relative, regions[region], c.owners[region]);
          if (!c.owners[region].arithmetic_supported) {
            intermediate_endpoint03_refuse(d, r, EP3W::unsupported_arithmetic);
            return d.state;
          }
          if (c.owners[region].certified)
            certificate = static_cast<EP3Cert>(c.owners[region].certificate);
          else if (regions[region].junction == Junction::hip) {
            r.self_stage = EP3Stage::hip;
            if (!intermediate_endpoint03_charge(
                    d, r, d.work.self_hip_complements, l.self_hip_complements,
                    EP3W::self_hip_capacity))
              return d.state;
            const auto side = b >= 9 ? std::size_t{1} : std::size_t{0};
            ep3_hip(c, regions[region], region, pair, c.hip_complements[side]);
            if (!c.hip_complements[side].arithmetic_supported) {
              intermediate_endpoint03_refuse(d, r,
                                             EP3W::unsupported_arithmetic);
              return d.state;
            }
            if (c.hip_complements[side].certified)
              certificate = EP3Cert::original_capsule_slab_complement;
          }
          break;
        }
      if (certificate == EP3Cert::not_run) {
        const auto first = unload_solid(d.parts[a], relative, p),
                   second = unload_solid(d.parts[b], relative, p);
        if (!ep3_separate(first, second, d, c, l, r, pair)) return d.state;
        certificate = EP3Cert::convex_support_plane;
      }
      c.self_certificates[pair] = certificate;
      ++c.self_certificate_counts[static_cast<std::size_t>(certificate)];
      ++c.accepted_pairs;
    }
  c.self_complete = pair == 105 && c.accepted_pairs == 105;
  if (!c.self_complete) {
    intermediate_endpoint03_refuse(d, r, EP3W::incomplete_endpoint);
    return d.state;
  }
  r.self_pair = 65535;
  r.self_region = 255;
  r.self_axis = 255;
  r.self_sign = 255;
  r.self_stage = EP3Stage::complete;
  c.complete = true;
  c.state = d.state = EP3S::accepted;
  return d.state;
}
} // namespace apsis_drift

namespace apsis_drift {
namespace {
using Slice3Why = BoardingIntermediateEndpoint03SliceCondition;
using EP3Request = BoardingRouteFootPhaseRequest;
auto ep3_packet(const BoardingRoutePhaseConstant& c,
                const std::array<double, 3>& terms, std::size_t count) -> bool {
  return c.count == count && c.terms == terms;
}
auto ep3_packet_domains(const EP3Request& request) -> bool {
  const auto valid = [](const BoardingRoutePhaseConstant& c) {
    if (c.count == 0 || c.count > 3) return false;
    for (std::size_t i = 0; i < 3; ++i)
      if (!std::isfinite(c.terms[i]) || std::abs(c.terms[i]) > 8 ||
          (i >= c.count && (c.terms[i] != 0 || std::signbit(c.terms[i]))))
        return false;
    return true;
  };
  for (const auto& root : request.root)
    for (const auto& c : root.coordinates)
      if (!valid(c)) return false;
  for (const auto& foot : request.feet)
    for (const auto& sole : foot.sole)
      for (const auto& c : sole.coordinates)
        if (!valid(c)) return false;
  return std::isfinite(request.seconds_per_parameter) &&
         std::isfinite(request.root_yaw_half[0]) &&
         std::isfinite(request.root_yaw_half[1]) &&
         std::isfinite(request.torso_lean_half[0]) &&
         std::isfinite(request.torso_lean_half[1]) &&
         std::isfinite(request.port_reaction_fraction[0]) &&
         std::isfinite(request.port_reaction_fraction[1]);
}
auto ep3_construction_packet_domains(const EP3Request& request) -> bool {
  for (std::size_t i = 0; i < 8; ++i) {
    const auto& c =
        i < 2 ? request.root[0].coordinates[i == 0 ? 0 : 2]
              : request.feet[i < 5 ? 0 : 1].sole[0].coordinates[(i - 2) % 3];
    if (c.count == 0 || c.count > 3) return false;
    for (std::size_t j = 0; j < 3; ++j)
      if (!std::isfinite(c.terms[j]) || std::abs(c.terms[j]) > 8 ||
          (j >= c.count && (c.terms[j] != 0 || std::signbit(c.terms[j]))))
        return false;
  }
  return true;
}
auto ep3_slice_stop(EP3D& d, EP3R& r, EP3W why, Slice3Why condition) -> bool {
  d.slice.condition = condition;
  r.limiting_bound = d.slice.limiting_bound;
  return detail::intermediate_endpoint03_refuse(d, r, why);
}
} // namespace
// This is a literal packet predicate, not a source issuer or new factory.
auto detail::intermediate_endpoint03_template_valid(const EP3Request& request,
                                                    bool generated, double y)
    -> bool {
  for (const auto& root : request.root) {
    if (!ep3_packet(root.coordinates[0], {.16, .16, -.0075}, 3) ||
        !ep3_packet(root.coordinates[2], {-.55, -.17, .19}, 3) ||
        !(generated ? ep3_packet(root.coordinates[1], {y, 0, 0}, 1)
                    : ep3_packet(root.coordinates[1],
                                 {.847, .012, (-.359) / 2.0}, 3)))
      return false;
  }
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& foot = request.feet[side];
    for (const auto& sole : foot.sole) {
      if (side == 0) {
        if (!ep3_packet(sole.coordinates[0], {.16, -.14, .16}, 3) ||
            !ep3_packet(sole.coordinates[1],
                        {static_cast<double>(-230000) * 1e-6, 0, 0}, 1) ||
            !ep3_packet(sole.coordinates[2], {-1.14, 0, 0}, 1))
          return false;
      } else {
        if (!ep3_packet(sole.coordinates[0], {.16, .14, 0}, 2) ||
            !ep3_packet(sole.coordinates[1],
                        {static_cast<double>(-160000) * 1e-6, 0, 0}, 1) ||
            !ep3_packet(sole.coordinates[2], {-.8, 0, 0}, 1))
          return false;
      }
    }
    if (foot.yaw_half != std::array<double, 2>{0, 0} ||
        foot.swing_height_metres != 0)
      return false;
  }
  return request.root_yaw_half == std::array<double, 2>{.125, .125} &&
         request.torso_lean_half == std::array<double, 2>{-.30, -.30} &&
         request.port_reaction_fraction ==
             std::array<double, 2>{.0625, .0625} &&
         request.seconds_per_parameter == 2;
}
[[gnu::noinline]] auto detail::intermediate_endpoint03_construct(
    EP3D& d, EP3Request& request, const EP3L& limits, EP3R& r) -> bool {
  // Scalar construction only: no PhaseCell, region catalog or owning return.
  std::array<Interval, 8> input{};
  std::array<Interval, 6> yaw{};
  std::array<Interval, 4> ca{};
  std::array<Interval, 2> thresholds{};
  std::array<Interval, 8> leg{};
  std::array<Interval, 4> proof{};
  std::array<double, 8> selected{};
  std::array<Interval, 2> lateral{}, absolute_lateral{}, roll_factor{};
  const auto guard = [&](std::uint8_t row, std::uint8_t side, auto predicate,
                         Slice3Why ordinary) {
    r = EP3R{};
    r.self_stage = EP3Stage::slice_guard;
    r.operation = row;
    if (side < 2) r.side = side;
    d.slice.guard = row;
    d.slice.side = side;
    d.slice.limiting_bound = {};
    if (!intermediate_endpoint03_charge(d, r, d.work.construction_guards,
                                        limits.construction_guards,
                                        EP3W::slice_guard_capacity)) {
      d.slice.condition = Slice3Why::guard_capacity;
      return false;
    }
    d.slice.guard_attempted |= std::uint32_t{1} << row;
    EP3W why = ordinary == Slice3Why::identity ? EP3W::slice_identity
                                               : EP3W::slice_unavailable;
    const bool good = predicate(why);
    if (!good)
      return ep3_slice_stop(d, r, why,
                            why == EP3W::unsupported_arithmetic
                                ? Slice3Why::unsupported_arithmetic
                                : ordinary);
    d.slice.guard_written |= std::uint32_t{1} << row;
    return true;
  };
  const auto operation = [&](std::uint8_t row, std::uint8_t side,
                             Interval& output, auto compute) {
    r = EP3R{};
    r.self_stage = EP3Stage::slice_operation;
    r.operation = row;
    if (side < 2) r.side = side;
    d.slice.operation = row;
    d.slice.side = side;
    d.slice.limiting_bound = {};
    if (!intermediate_endpoint03_charge(d, r, d.work.construction_operations,
                                        limits.construction_operations,
                                        EP3W::slice_operation_capacity)) {
      d.slice.condition = Slice3Why::operation_capacity;
      return false;
    }
    d.slice.operation_attempted[row / 64] |= std::uint64_t{1} << (row % 64);
    const auto value = compute();
    if (!ep3_valid(value))
      return ep3_slice_stop(d, r, EP3W::unsupported_arithmetic,
                            Slice3Why::unsupported_arithmetic);
    output = value;
    d.slice.limiting_bound = {value.low, value.high, true};
    d.slice.operation_written[row / 64] |= std::uint64_t{1} << (row % 64);
    d.slice.arithmetic_supported = true;
    return true;
  };
  if (!guard(
          0, 255,
          [&](EP3W&) {
            return d.source_enrolled && d.work.source_guards == 64 &&
                   d.source_evaluated == UINT64_MAX &&
                   BoardingIntermediatePauseSupportAccess::valid(d.source) &&
                   BoardingIntermediatePauseSupportAccess::data(d.source) &&
                   d.parts.size() == 15;
          },
          Slice3Why::identity))
    return false;
  if (!guard(
          1, 255,
          [&](EP3W& why) {
            why = EP3W::unsupported_arithmetic;
            return boarding_route_foot_phase_environment();
          },
          Slice3Why::unsupported_arithmetic))
    return false;
  if (!guard(
          2, 255,
          [&](EP3W&) {
            return d.version == 3 &&
                   d.candidate == BoardingIntermediateEndpoint03Candidate::
                                      root_y_reach_roll_slice &&
                   intermediate_endpoint03_template_valid(request, false, 0);
          },
          Slice3Why::identity))
    return false;
  if (!guard(
          3, 255,
          [&](EP3W& why) {
            why = EP3W::unsupported_arithmetic;
            return ep3_construction_packet_domains(request);
          },
          Slice3Why::identity))
    return false;
  if (!guard(
          4, 255,
          [&](EP3W& why) {
            if (!std::isfinite(thigh_length) || !std::isfinite(shin_length)) {
              why = EP3W::unsupported_arithmetic;
              return false;
            }
            return kBoardingRouteFootPhaseVersion == 1 &&
                   thigh_length == .47285 && shin_length == .47478 &&
                   thigh_length > 0 && shin_length > 0 && thigh_length <= 1 &&
                   shin_length <= 1 && ep3_source_bit(d, 62);
          },
          Slice3Why::identity))
    return false;
  if (!guard(
          5, 255,
          [&](EP3W&) {
            // The source compiler's exact-real nominal rational yaw
            // construction, not midpoint or rounded affine orthogonality, owns
            // these identities.
            return ep3_source_bit(d, 63) &&
                   kBoardingRouteFootPhaseVersion == 1 &&
                   intermediate_endpoint03_template_valid(request, false, 0) &&
                   request.feet[0].yaw_half == std::array<double, 2>{0, 0} &&
                   request.feet[1].yaw_half == std::array<double, 2>{0, 0};
          },
          Slice3Why::identity))
    return false;
  // O01--O08: each actual CONST is called only after its own charge.
  for (std::uint8_t i = 0; i < 8; ++i)
    if (!operation(i,
                   i < 2   ? 255
                   : i < 5 ? 0
                           : 1,
                   input[i], [&] {
                     const auto& coordinate =
                         i < 2 ? request.root[0].coordinates[i == 0 ? 0 : 2]
                               : request.feet[i < 5 ? 0 : 1]
                                     .sole[0]
                                     .coordinates[(i - 2) % 3];
                     return phase_constant(coordinate);
                   }))
      return false;
  if (!operation(8, 255, yaw[0],
                 [&] { return square(point(request.root_yaw_half[0])); }) ||
      !operation(9, 255, yaw[1], [&] { return add(point(1), yaw[0]); }) ||
      !operation(10, 255, yaw[2], [&] { return subtract(point(1), yaw[0]); }) ||
      !operation(11, 255, yaw[3], [&] { return divide(yaw[2], yaw[1]); }) ||
      !operation(12, 255, yaw[2],
                 [&] {
                   return multiply(point(2), point(request.root_yaw_half[0]));
                 }) ||
      !operation(13, 255, yaw[4], [&] { return divide(yaw[2], yaw[1]); }) ||
      !operation(14, 255, yaw[5], [&] { return negate(yaw[4]); }))
    return false;
  for (std::uint8_t side = 0; side < 2; ++side) {
    const auto base = static_cast<std::uint8_t>(side == 0 ? 15 : 25);
    if (!operation(
            base, side, leg[0],
            [&] { return multiply(point(side == 0 ? -.14 : .14), yaw[3]); }) ||
        !operation(
            static_cast<std::uint8_t>(base + 1), side, leg[1],
            [&] { return multiply(point(side == 0 ? -.14 : .14), yaw[5]); }) ||
        !operation(static_cast<std::uint8_t>(base + 2), side, leg[2],
                   [&] { return add(input[0], leg[0]); }) ||
        !operation(static_cast<std::uint8_t>(base + 3), side, leg[3],
                   [&] { return add(input[1], leg[1]); }) ||
        !operation(static_cast<std::uint8_t>(base + 4), side, lateral[side],
                   [&] { return subtract(input[2 + 3 * side], leg[2]); }) ||
        !operation(static_cast<std::uint8_t>(base + 5), side, leg[5],
                   [&] { return subtract(input[4 + 3 * side], leg[3]); }) ||
        !operation(static_cast<std::uint8_t>(base + 6), side, leg[6],
                   [&] { return square(lateral[side]); }) ||
        !operation(static_cast<std::uint8_t>(base + 7), side, leg[7],
                   [&] { return square(leg[5]); }) ||
        !operation(static_cast<std::uint8_t>(base + 8), side,
                   ca[std::size_t{2} * side],
                   [&] { return add(leg[6], leg[7]); }) ||
        !operation(static_cast<std::uint8_t>(base + 9), side, ca[2 * side + 1],
                   [&] { return add(input[3 + 3 * side], point(.1)); }))
      return false;
  }
  if (!operation(
          35, 255, thresholds[0],
          [&] { return add(point(thigh_length), point(shin_length)); }) ||
      !operation(36, 255, thresholds[0],
                 [&] { return square(thresholds[0]); }) ||
      !operation(
          37, 255, thresholds[1],
          [&] { return subtract(point(thigh_length), point(shin_length)); }) ||
      !operation(38, 255, thresholds[1], [&] { return square(thresholds[1]); }))
    return false;
  for (std::uint8_t side = 0; side < 2; ++side) {
    const auto base = static_cast<std::uint8_t>(side == 0 ? 39 : 46);
    if (!operation(base, side, leg[0], [&] {
          return subtract(point(thresholds[1].high),
                          point(ca[std::size_t{2} * side].low));
        }))
      return false;
    bool zero_branch{};
    if (!operation(static_cast<std::uint8_t>(base + 1), side, leg[1], [&] {
          zero_branch = leg[0].high <= 0;
          return point(zero_branch ? +0.0 : leg[0].high);
        }))
      return false;
    if (zero_branch)
      d.slice.zero_mask |= static_cast<std::uint8_t>(std::uint8_t{1} << side);
    if (!operation(static_cast<std::uint8_t>(base + 2), side, leg[2],
                   [&] { return transfer_small_root(leg[1]); }) ||
        !operation(static_cast<std::uint8_t>(base + 3), side, leg[3], [&] {
          return add(point(ca[2 * side + 1].high), point(leg[2].high));
        }))
      return false;
    selected[std::size_t{2} * side] = leg[3].high;
    if (!operation(static_cast<std::uint8_t>(base + 4), side, leg[4], [&] {
          return subtract(point(thresholds[0].low),
                          point(ca[std::size_t{2} * side].high));
        }))
      return false;
    if (!guard(
            static_cast<std::uint8_t>(6 + side), side,
            [&](EP3W& why) {
              d.slice.limiting_bound = {leg[4].low, leg[4].high,
                                        leg[4].supported};
              if (!ep3_valid(leg[4]) || leg[4].high > 16384) {
                why = EP3W::unsupported_arithmetic;
                return false;
              }
              return leg[4].low > 0;
            },
            Slice3Why::no_positive_upper))
      return false;
    if (!operation(static_cast<std::uint8_t>(base + 5), side, leg[5],
                   [&] { return transfer_small_root(point(leg[4].low)); }) ||
        !operation(static_cast<std::uint8_t>(base + 6), side, leg[6], [&] {
          return add(point(ca[2 * side + 1].low), point(leg[5].low));
        }))
      return false;
    selected[2 * side + 1] = leg[6].low;
  }
  // Certified original roll factor and both full ABS enclosures remain
  // live through lower-height construction and direct final verification.
  if (!operation(53, 255, roll_factor[0],
                 [&] { return transfer_small_root(point(3)); }) ||
      !operation(54, 255, roll_factor[1],
                 [&] { return subtract(point(2), roll_factor[0]); }))
    return false;
  if (!guard(
          8, 255,
          [&](EP3W& why) {
            const auto& b = roll_factor[1];
            d.slice.limiting_bound = {b.low, b.high, b.supported};
            if (!ep3_valid(b)) {
              why = EP3W::unsupported_arithmetic;
              return false;
            }
            return b.low > 0;
          },
          Slice3Why::no_positive_roll_factor))
    return false;
  for (std::uint8_t side = 0; side < 2; ++side) {
    const auto base = static_cast<std::uint8_t>(side == 0 ? 55 : 58);
    if (!operation(base, side, absolute_lateral[side],
                   [&] { return absolute(lateral[side]); }) ||
        !operation(
            static_cast<std::uint8_t>(base + 1), side, leg[0],
            [&] { return divide(absolute_lateral[side], roll_factor[1]); }) ||
        !operation(static_cast<std::uint8_t>(base + 2), side, leg[1], [&] {
          return add(point(ca[std::size_t{2} * side + 1].high),
                     point(leg[0].high));
        }))
      return false;
    selected[6 + side] = leg[1].high;
  }
  // Merge reach, PORT roll, STAR roll in that order. Equality preserves the
  // first operand bits, before ONE final stored midpoint is constructed.
  if (!operation(61, 255, leg[0],
                 [&] {
                   return point(selected[0] < selected[2] ? selected[2]
                                                          : selected[0]);
                 }) ||
      !operation(62, 255, leg[0],
                 [&] {
                   return point(leg[0].low < selected[6] ? selected[6]
                                                         : leg[0].low);
                 }) ||
      !operation(63, 255, leg[0], [&] {
        return point(leg[0].low < selected[7] ? selected[7] : leg[0].low);
      }))
    return false;
  d.slice.lo = leg[0].low;
  if (!operation(64, 255, leg[1], [&] {
        return point(selected[3] < selected[1] ? selected[3] : selected[1]);
      }))
    return false;
  d.slice.hi = leg[1].low;
  if (!guard(
          9, 255,
          [&](EP3W& why) {
            d.slice.limiting_bound = {d.slice.lo, d.slice.lo,
                                      std::isfinite(d.slice.lo)};
            if (!std::isfinite(d.slice.lo) || !std::isfinite(d.slice.hi)) {
              why = EP3W::unsupported_arithmetic;
              return false;
            }
            return d.slice.lo < d.slice.hi;
          },
          Slice3Why::empty_inward_interval))
    return false;
  if (!operation(65, 255, leg[2],
                 [&] { return point(d.slice.hi - d.slice.lo); }) ||
      !operation(66, 255, leg[3], [&] { return point(leg[2].low * .5); }) ||
      !operation(67, 255, leg[4],
                 [&] { return point(d.slice.lo + leg[3].low); }))
    return false;
  d.slice.y = leg[4].low;
  if (!guard(
          10, 255,
          [&](EP3W& why) {
            d.slice.limiting_bound = {d.slice.y, d.slice.y,
                                      std::isfinite(d.slice.y)};
            if (!std::isfinite(d.slice.y)) {
              why = EP3W::unsupported_arithmetic;
              return false;
            }
            return std::abs(d.slice.y) <= 8;
          },
          Slice3Why::candidate_domain) ||
      !guard(
          11, 255,
          [&](EP3W&) {
            d.slice.limiting_bound = {d.slice.y, d.slice.y, true};
            return d.slice.y > d.slice.lo;
          },
          Slice3Why::midpoint_unavailable) ||
      !guard(
          12, 255,
          [&](EP3W&) {
            d.slice.limiting_bound = {d.slice.y, d.slice.y, true};
            return d.slice.y < d.slice.hi;
          },
          Slice3Why::midpoint_unavailable))
    return false;
  for (std::uint8_t side = 0; side < 2; ++side) {
    const auto base = static_cast<std::uint8_t>(side == 0 ? 68 : 73);
    const auto first_guard = static_cast<std::uint8_t>(side == 0 ? 13 : 16);
    if (!operation(base, side, proof[0],
                   [&] {
                     return subtract(point(d.slice.y),
                                     ca[std::size_t{2} * side + 1]);
                   }) ||
        !operation(static_cast<std::uint8_t>(base + 1), side, proof[1],
                   [&] { return square(proof[0]); }) ||
        !operation(static_cast<std::uint8_t>(base + 2), side, proof[2 + side],
                   [&] { return add(ca[std::size_t{2} * side], proof[1]); }))
      return false;
    if (!guard(
            first_guard, side,
            [&](EP3W&) {
              const auto& a = ca[std::size_t{2} * side + 1];
              d.slice.limiting_bound = {a.low, a.high, a.supported};
              return d.slice.y > a.high;
            },
            Slice3Why::verification_inconclusive) ||
        !guard(
            static_cast<std::uint8_t>(first_guard + 1), side,
            [&](EP3W& why) {
              const auto& distance = proof[2 + side];
              d.slice.limiting_bound = {distance.low, distance.high,
                                        distance.supported};
              if (!ep3_valid(distance)) {
                why = EP3W::unsupported_arithmetic;
                return false;
              }
              return distance.low > thresholds[1].high &&
                     distance.high < thresholds[0].low;
            },
            Slice3Why::verification_inconclusive))
      return false;
    // Square and distance were consumed; reuse only those proof slots.
    // Full height in proof[0], factor b and per-side ABS remain unmodified.
    if (!operation(static_cast<std::uint8_t>(base + 3), side, proof[1],
                   [&] { return multiply(proof[0], roll_factor[1]); }) ||
        !operation(static_cast<std::uint8_t>(base + 4), side, proof[2 + side],
                   [&] { return subtract(proof[1], absolute_lateral[side]); }))
      return false;
    if (!guard(
            static_cast<std::uint8_t>(first_guard + 2), side,
            [&](EP3W& why) {
              const auto& margin = proof[2 + side];
              d.slice.limiting_bound = {margin.low, margin.high,
                                        margin.supported};
              if (!ep3_valid(margin)) {
                why = EP3W::unsupported_arithmetic;
                return false;
              }
              return margin.low >= 0;
            },
            Slice3Why::verification_inconclusive))
      return false;
  }
  if (!guard(
          19, 255,
          [&](EP3W&) {
            if (!intermediate_endpoint03_template_valid(request, false, 0))
              return false;
            for (auto& root : request.root)
              root.coordinates[1] = {{d.slice.y, +0.0, +0.0}, 1};
            return intermediate_endpoint03_template_valid(request, true,
                                                          d.slice.y);
          },
          Slice3Why::identity))
    return false;
  if (!guard(
          20, 255,
          [&](EP3W& why) {
            if (!boarding_route_foot_phase_environment()) {
              why = EP3W::unsupported_arithmetic;
              return false;
            }
            if (!intermediate_endpoint03_template_valid(request, true,
                                                        d.slice.y))
              return false;
            if (!ep3_packet_domains(request)) {
              why = EP3W::unsupported_arithmetic;
              return false;
            }
            const auto valid = boarding_route_foot_phase_request_valid(request);
            if (!valid) {
              why = EP3W::unsupported_arithmetic;
              return false;
            }
            if (!boarding_route_foot_phase_environment()) {
              why = EP3W::unsupported_arithmetic;
              return false;
            }
            return true;
          },
          Slice3Why::identity))
    return false;
  d.slice.complete =
      d.slice.operation_written ==
          std::array<std::uint64_t, 2>{UINT64_MAX, 0x0000000000003fffULL} &&
      d.slice.guard_written == 0x001fffffU;
  return d.slice.complete;
}
} // namespace apsis_drift

#include "origin_boarding_intermediate_endpoint04_internal.hpp"
namespace apsis_drift {
namespace {
using EP4D = BoardingIntermediateEndpoint04Diagnostic;
using EP4C = BoardingIntermediateEndpoint04Cell;
using EP4R = BoardingIntermediateEndpoint04Refusal;
using EP4L = detail::BoardingIntermediateEndpoint04Limits;
using EP4W = BoardingIntermediateEndpoint04Condition;
using EP4S = BoardingIntermediateEndpoint04State;
using EP4Stage = BoardingIntermediateEndpoint04SelfStage;
using EP4Cert = BoardingIntermediateEndpoint04Certificate;
auto ep4_source_bit(const EP4D& d, std::size_t i) -> bool {
  return (d.source_evaluated & (std::uint64_t{1} << i)) != 0;
}
auto ep4_valid(Interval x) -> bool {
  return x.supported && std::isfinite(x.low) && std::isfinite(x.high) &&
         x.low <= x.high;
}
auto ep4_scalar(const BoardingFootSiteScalarBounds& x) -> Interval {
  return x.supported ? interval(x.lower, x.upper) : failed();
}
auto ep4_axis(const EP4C& c, std::size_t axis) -> Point {
  return {ep4_scalar(c.unit_axes[axis][0]), ep4_scalar(c.unit_axes[axis][1]),
          ep4_scalar(c.unit_axes[axis][2])};
}
auto ep4_component(const BoardingPlantedLegPointBounds& b, std::size_t j)
    -> Interval {
  return interval(j == 0   ? b.lower.x
                  : j == 1 ? b.lower.y
                           : b.lower.z,
                  j == 0   ? b.upper.x
                  : j == 1 ? b.upper.y
                           : b.upper.z);
}
// Direct endpoint division encloses genuine D/L, never a rounded reciprocal.
auto ep4_unit_divide(Interval numerator, double length) -> Interval {
  if (!ep4_valid(numerator) || !std::isfinite(length) || length <= 0)
    return failed();
  if (numerator.low == 0 && numerator.high == 0) return point(0);
  const auto low = numerator.low / length, high = numerator.high / length;
  if (!std::isfinite(low) || !std::isfinite(high) ||
      low == -std::numeric_limits<double>::max() ||
      high == std::numeric_limits<double>::max())
    return failed();
  return interval(down(low), up(high));
}
[[gnu::noinline]] auto ep4_units(EP4D& d, EP4C& c, const EP4L& l, EP4R& r)
    -> bool {
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
      r = EP4R{};
      r.self_stage = EP4Stage::unit_axes;
      r.operation = static_cast<std::uint8_t>(row);
      c.unit_axis_cursor = static_cast<std::uint8_t>(row);
      if (!detail::intermediate_endpoint04_charge(
              d, r, d.work.unit_axis_operations, l.unit_axis_operations,
              EP4W::unit_axis_capacity))
        return false;
      c.unit_axis_attempted |= std::uint32_t{1} << row;
      const auto length = axis < 2 ? thigh_length : shin_length;
      if (!c.self_body_complete || !ss_leg(c.phase.legs[side]) ||
          !ep4_source_bit(d, 62) ||
          (axis < 2 ? length != .47285 : length != .47478))
        return detail::intermediate_endpoint04_refuse(d, r,
                                                      EP4W::unit_axis_identity);
      const auto result =
          operation < 3
              ? subtract(ep4_component(c.phase.points[body_index(knee)].value,
                                       component),
                         ep4_component(c.phase.points[body_index(from)].value,
                                       component))
              : ep4_unit_divide(displacement[component], length);
      if (!ep4_valid(result) ||
          (operation < 3 && (result.low < -16 || result.high > 16)))
        return detail::intermediate_endpoint04_refuse(
            d, r, EP4W::unsupported_arithmetic);
      if (operation < 3)
        displacement[component] = result;
      else
        c.unit_axes[axis][component] = {result.low, result.high, true};
      c.unit_axis_written |= std::uint32_t{1} << row;
    }
    c.unit_axis_complete_mask |=
        static_cast<std::uint8_t>(std::uint8_t{1} << axis);
  }
  return (c.unit_axis_written == kBoardingIntermediateEndpoint04UnitMask &&
          c.unit_axis_complete_mask == 15) ||
         detail::intermediate_endpoint04_refuse(d, r, EP4W::unit_axis_identity);
}
[[gnu::noinline]] auto ep4_owner(
    const EP4C& c,
    const std::array<Point, kBoardingPlantedBodyPointCount>& relative,
    const SelfRegion& region, BoardingLowerFootTransferOwner& out) -> void {
  if (region.junction != Junction::hip && region.junction != Junction::ankle) {
    ss_owner(c.phase, relative, region, out);
    return;
  }
  const auto side = static_cast<std::size_t>(region.second) >= 9
                        ? std::size_t{1}
                        : std::size_t{0};
  const auto u = ep4_axis(c, (region.junction == Junction::hip ? 0 : 2) + side);
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
        !ep4_valid(u[1]))
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
      ep4_valid(extent) && ep4_valid(limit) && ep4_valid(gap);
  if (!out.arithmetic_supported) return;
  out.extent = bounds(extent);
  out.limit = bounds(limit);
  out.secondary_extent = bounds(point(0));
  out.structural_identity = true;
  out.certificate = SelfCert::original_axis_box_support;
  out.certified = gap.low >= 0;
}
[[gnu::noinline]] auto ep4_hip(
    const EP4C& c, const SelfRegion& region, std::size_t region_index,
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
  const auto u = ep4_axis(c, side);
  for (std::size_t i = 0; i < 3; ++i) {
    if (!ep4_valid(u[i])) return;
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
[[gnu::noinline]] auto ep4_separate(const UnloadSolid& a, const UnloadSolid& b,
                                    EP4D& d, EP4C& c, const EP4L& l, EP4R& r,
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
  r.self_stage = EP4Stage::separation;
  for (std::size_t i = 0; i < count; ++i) {
    r.self_axis = static_cast<std::uint8_t>(i);
    r.self_sign = 255;
    if (!detail::intermediate_endpoint04_charge(
            d, r, d.work.self_axes, l.self_axes, EP4W::self_axis_capacity))
      return false;
    const auto axis = proposal(i);
    if (!self_direction(axis)) continue;
    for (std::size_t sign = 0; sign < 2; ++sign) {
      r.self_sign = static_cast<std::uint8_t>(sign);
      if (!detail::intermediate_endpoint04_charge(
              d, r, d.work.self_signed_trials, l.self_signed_trials,
              EP4W::self_signed_capacity))
        return false;
      const auto n = sign == 0 ? axis : self_negate(axis);
      const auto total =
          add(unload_support(a, n), unload_support(b, self_negate(n)));
      if (!total.supported || !std::isfinite(total.low) ||
          !std::isfinite(total.high))
        return detail::intermediate_endpoint04_refuse(
            d, r, EP4W::unsupported_arithmetic);
      if (total.high <= 0) {
        c.self_axes[pair] = static_cast<std::uint8_t>(i | (sign == 0 ? 0 : 16));
        return true;
      }
    }
  }
  return detail::intermediate_endpoint04_refuse(d, r,
                                                EP4W::unresolved_self_pair);
}
} // namespace
auto detail::intermediate_endpoint04_self_bridge(
    const BoardingIntermediateEndpoint04CurrentToken& token, EP4D& d, EP4C& c,
    const EP4L& l, EP4R& r) -> EP4S {
  std::size_t body_index_record{};
  const auto body = [&](auto predicate, EP4W why = EP4W::self_body_identity) {
    r = EP4R{};
    r.self_stage = EP4Stage::body;
    r.operation = static_cast<std::uint8_t>(body_index_record);
    if (!intermediate_endpoint04_charge(d, r, d.work.self_body_guards,
                                        l.self_body_guards,
                                        EP4W::self_body_capacity))
      return false;
    c.self_body_evaluated |= std::uint64_t{1} << body_index_record++;
    return predicate() || intermediate_endpoint04_refuse(d, r, why);
  };
  if (!body([&] {
        return token.context() && token.owner() == &d && token.cell() == &c &&
               token.request() && token.context()->owner() == &d &&
               token.context()->parts() == &d.parts &&
               token.context()->request() == token.request() &&
               token.context()->key() &&
               token.context()->key()->version() == 4 &&
               token.context()->key()->candidate() == d.candidate &&
               token.context()->key()->y() == d.slice.y && d.slice.complete &&
               d.work.phase_calls == 1 &&
               BoardingIntermediatePauseSupportAccess::valid(d.source) &&
               token.data() ==
                   BoardingIntermediatePauseSupportAccess::data(d.source) &&
               token.context()->data() == token.data() && token.data() &&
               d.cells.size() == 1 && d.cells.capacity() == 1 &&
               &d.cells.front() == &c && d.work.phase_calls == 1;
      }))
    return d.state;
  if (!body([&] { return boarding_route_foot_phase_environment(); },
            EP4W::unsupported_arithmetic))
    return d.state;
  const auto& p = c.phase;
  if (!body([&] {
        return p.complete && p.arithmetic_supported && p.nominal_links &&
               p.target_sole_identities && p.joint_sectors &&
               p.derivative_domains && p.timing_complete;
      }))
    return d.state;
  if (!body([&] {
        return d.candidate == BoardingIntermediateEndpoint04Candidate::
                                  root_y_both_ankle_reach_roll_slice &&
               d.version == 4 && p.first == 0 && p.last == 1 &&
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
        return ep4_source_bit(d, 62) && p.nominal_links &&
               p.target_sole_identities;
      }))
    return d.state;
  if (!body([&] {
        return ep4_source_bit(d, 63) && p.arithmetic_supported &&
               c.projected_carrier_complete[0];
      }))
    return d.state;
  if (!body([&] {
        return c.nominal_support_complete && c.nominal_equilibrium &&
               c.star_support && d.stop_condition == EP4W::none;
      }))
    return d.state;
  for (std::size_t i = 0; i < 18; ++i)
    if (!body(
            [&] {
              return ss_point_valid(body_intervals(p.points[i].value), 8);
            },
            EP4W::unsupported_arithmetic))
      return d.state;
  for (std::size_t f = 0; f < 4; ++f)
    for (std::size_t j = 0; j < 3; ++j)
      if (!body(
              [&] {
                return ss_point_valid(
                    body_intervals(p.frames[f].columns[j].value), 8);
              },
              EP4W::unsupported_arithmetic))
        return d.state;
  for (std::size_t side = 0; side < 2; ++side)
    if (!body([&] {
          const auto first = side == 0 ? std::size_t{36} : std::size_t{42};
          return ss_leg(p.legs[side]) && ep4_source_bit(d, 62) &&
                 ep4_source_bit(d, first) && ep4_source_bit(d, first + 1) &&
                 ss_same_part(d.parts[first - 33], ss_part(first - 33)) &&
                 ss_same_part(d.parts[first - 32], ss_part(first - 32));
        }))
      return d.state;
  if (!body([&] {
        return ss_same_part(d.parts[0], ss_part(0)) &&
               ss_same_part(d.parts[1], ss_part(1)) && ep4_source_bit(d, 33) &&
               ep4_source_bit(d, 34) && ep4_source_bit(d, 48);
      }))
    return d.state;
  if (!body([&] {
        return ss_same_part(d.parts[1], ss_part(1)) &&
               ss_same_part(d.parts[2], ss_part(2)) && ep4_source_bit(d, 34) &&
               ep4_source_bit(d, 35) && ep4_source_bit(d, 51) &&
               ep4_source_bit(d, 63);
      }))
    return d.state;
  if (!body([&] {
        if (!ep4_source_bit(d, 63)) return false;
        for (const auto part :
             {std::size_t{6}, std::size_t{7}, std::size_t{8}, std::size_t{12},
              std::size_t{13}, std::size_t{14}})
          if (!ep4_source_bit(d, 33 + part) ||
              !ss_same_part(d.parts[part], ss_part(part)))
            return false;
        for (const auto region :
             {std::size_t{4}, std::size_t{5}, std::size_t{8}, std::size_t{9},
              std::size_t{12}, std::size_t{13}})
          if (!ep4_source_bit(d, 48 + region)) return false;
        return true;
      }))
    return d.state;
  SelfRegions regions{};
  if (!body([&] {
        regions = self_regions();
        for (std::size_t i = 0; i < 14; ++i)
          if (!ep4_source_bit(d, 48 + i) ||
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
            EP4W::unsupported_arithmetic))
      return d.state;
  if (!body([&] {
        return body_index_record == 63 &&
               c.self_body_evaluated ==
                   kBoardingIntermediateEndpoint04BodyMask &&
               token.owner() == &d && token.cell() == &c &&
               token.data() ==
                   BoardingIntermediatePauseSupportAccess::data(d.source);
      }))
    return d.state;
  c.self_body_complete = true;
  if (!ep4_units(d, c, l, r)) return d.state;
  std::size_t pair{};
  for (std::size_t a = 0; a < 15; ++a)
    for (std::size_t b = a + 1; b < 15; ++b, ++pair) {
      r = EP4R{};
      r.self_stage = EP4Stage::not_run;
      r.self_pair = static_cast<std::uint16_t>(pair);
      r.self_region = 255;
      r.self_axis = 255;
      r.self_sign = 255;
      r.self_stage = EP4Stage::pair;
      if (!intermediate_endpoint04_charge(d, r, d.work.self_pairs, l.self_pairs,
                                          EP4W::self_pair_capacity))
        return d.state;
      ++c.examined_pairs;
      auto certificate = EP4Cert::not_run;
      for (std::size_t region = 0; region < 14; ++region)
        if (static_cast<std::size_t>(regions[region].first) == a &&
            static_cast<std::size_t>(regions[region].second) == b) {
          r.self_stage = EP4Stage::owner;
          r.self_region = static_cast<std::uint8_t>(region);
          if (!intermediate_endpoint04_charge(d, r, d.work.self_owners,
                                              l.self_owners,
                                              EP4W::self_owner_capacity))
            return d.state;
          c.owner_attempted_mask |=
              static_cast<std::uint16_t>(std::uint16_t{1} << region);
          ep4_owner(c, relative, regions[region], c.owners[region]);
          if (!c.owners[region].arithmetic_supported) {
            intermediate_endpoint04_refuse(d, r, EP4W::unsupported_arithmetic);
            return d.state;
          }
          if (c.owners[region].certified)
            certificate = static_cast<EP4Cert>(c.owners[region].certificate);
          else if (regions[region].junction == Junction::hip) {
            r.self_stage = EP4Stage::hip;
            if (!intermediate_endpoint04_charge(
                    d, r, d.work.self_hip_complements, l.self_hip_complements,
                    EP4W::self_hip_capacity))
              return d.state;
            const auto side = b >= 9 ? std::size_t{1} : std::size_t{0};
            ep4_hip(c, regions[region], region, pair, c.hip_complements[side]);
            if (!c.hip_complements[side].arithmetic_supported) {
              intermediate_endpoint04_refuse(d, r,
                                             EP4W::unsupported_arithmetic);
              return d.state;
            }
            if (c.hip_complements[side].certified)
              certificate = EP4Cert::original_capsule_slab_complement;
          }
          break;
        }
      if (certificate == EP4Cert::not_run) {
        const auto first = unload_solid(d.parts[a], relative, p),
                   second = unload_solid(d.parts[b], relative, p);
        if (!ep4_separate(first, second, d, c, l, r, pair)) return d.state;
        certificate = EP4Cert::convex_support_plane;
      }
      c.self_certificates[pair] = certificate;
      ++c.self_certificate_counts[static_cast<std::size_t>(certificate)];
      ++c.accepted_pairs;
    }
  c.self_complete = pair == 105 && c.accepted_pairs == 105;
  if (!c.self_complete) {
    intermediate_endpoint04_refuse(d, r, EP4W::incomplete_endpoint);
    return d.state;
  }
  r.self_pair = 65535;
  r.self_region = 255;
  r.self_axis = 255;
  r.self_sign = 255;
  r.self_stage = EP4Stage::complete;
  c.complete = true;
  c.state = d.state = EP4S::accepted;
  return d.state;
}
} // namespace apsis_drift

namespace apsis_drift {
namespace {
using Slice4Why = BoardingIntermediateEndpoint04SliceCondition;
using EP4Request = BoardingRouteFootPhaseRequest;
auto ep4_packet(const BoardingRoutePhaseConstant& c,
                const std::array<double, 3>& terms, std::size_t count) -> bool {
  return c.count == count && c.terms == terms;
}
// Comparison-only structure; no numeric sum or construction evaluation.
// Called only within the already charged G04/G37 deferred predicates.
auto ep4_packet_structure(const EP4Request& request) -> bool {
  const auto canonical = [](const BoardingRoutePhaseConstant& c) {
    if (c.count == 0 || c.count > c.terms.size()) return false;
    for (std::size_t i = c.count; i < c.terms.size(); ++i)
      if (c.terms[i] != 0 || std::signbit(c.terms[i])) return false;
    return true;
  };
  for (const auto& root : request.root)
    for (const auto& c : root.coordinates)
      if (!canonical(c)) return false;
  for (const auto& foot : request.feet)
    for (const auto& sole : foot.sole)
      for (const auto& c : sole.coordinates)
        if (!canonical(c)) return false;
  return true;
}
auto ep4_packet_domains(const EP4Request& request) -> bool {
  const auto valid = [](const BoardingRoutePhaseConstant& c) {
    if (c.count == 0 || c.count > 3) return false;
    for (std::size_t i = 0; i < 3; ++i)
      if (!std::isfinite(c.terms[i]) || std::abs(c.terms[i]) > 8 ||
          (i >= c.count && (c.terms[i] != 0 || std::signbit(c.terms[i]))))
        return false;
    return true;
  };
  for (const auto& root : request.root)
    for (const auto& c : root.coordinates)
      if (!valid(c)) return false;
  for (const auto& foot : request.feet)
    for (const auto& sole : foot.sole)
      for (const auto& c : sole.coordinates)
        if (!valid(c)) return false;
  return std::isfinite(request.seconds_per_parameter) &&
         std::isfinite(request.root_yaw_half[0]) &&
         std::isfinite(request.root_yaw_half[1]) &&
         std::isfinite(request.torso_lean_half[0]) &&
         std::isfinite(request.torso_lean_half[1]) &&
         std::isfinite(request.port_reaction_fraction[0]) &&
         std::isfinite(request.port_reaction_fraction[1]);
}
auto ep4_construction_packet_domains(const EP4Request& request) -> bool {
  for (std::size_t i = 0; i < 8; ++i) {
    const auto& c =
        i < 2 ? request.root[0].coordinates[i == 0 ? 0 : 2]
              : request.feet[i < 5 ? 0 : 1].sole[0].coordinates[(i - 2) % 3];
    if (c.count == 0 || c.count > 3) return false;
    for (std::size_t j = 0; j < 3; ++j)
      if (!std::isfinite(c.terms[j]) || std::abs(c.terms[j]) > 8 ||
          (j >= c.count && (c.terms[j] != 0 || std::signbit(c.terms[j]))))
        return false;
  }
  return true;
}
auto ep4_slice_stop(EP4D& d, EP4R& r, EP4W why, Slice4Why condition) -> bool {
  d.slice.condition = condition;
  r.limiting_bound = d.slice.limiting_bound;
  return detail::intermediate_endpoint04_refuse(d, r, why);
}
} // namespace
// This is a literal packet predicate, not a source issuer or new factory.
auto detail::intermediate_endpoint04_template_valid(const EP4Request& request,
                                                    bool generated, double y)
    -> bool {
  for (const auto& root : request.root) {
    if (!ep4_packet(root.coordinates[0], {.16, .16, -.0075}, 3) ||
        !ep4_packet(root.coordinates[2], {-.55, -.17, .19}, 3) ||
        !(generated ? ep4_packet(root.coordinates[1], {y, 0, 0}, 1)
                    : ep4_packet(root.coordinates[1],
                                 {.847, .012, (-.359) / 2.0}, 3)))
      return false;
  }
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& foot = request.feet[side];
    for (const auto& sole : foot.sole) {
      if (side == 0) {
        if (!ep4_packet(sole.coordinates[0], {.16, -.14, .16}, 3) ||
            !ep4_packet(sole.coordinates[1],
                        {static_cast<double>(-230000) * 1e-6, 0, 0}, 1) ||
            !ep4_packet(sole.coordinates[2], {-1.14, 0, 0}, 1))
          return false;
      } else {
        if (!ep4_packet(sole.coordinates[0], {.16, .14, 0}, 2) ||
            !ep4_packet(sole.coordinates[1],
                        {static_cast<double>(-160000) * 1e-6, 0, 0}, 1) ||
            !ep4_packet(sole.coordinates[2], {-.8, 0, 0}, 1))
          return false;
      }
    }
    if (foot.yaw_half != std::array<double, 2>{0, 0} ||
        foot.swing_height_metres != 0)
      return false;
  }
  return request.root_yaw_half == std::array<double, 2>{.125, .125} &&
         request.torso_lean_half == std::array<double, 2>{-.30, -.30} &&
         request.port_reaction_fraction ==
             std::array<double, 2>{.0625, .0625} &&
         request.seconds_per_parameter == 2;
}
[[gnu::noinline]] auto detail::intermediate_endpoint04_construct(
    EP4D& d, EP4Request& request, const EP4L& limits, EP4R& r) -> bool {
  // All construction storage dies before Key issuance and the original graph.
  std::array<Interval, 8> input{};
  std::array<Interval, 6> yaw{};
  // Per side: signed X/Z, X2/Z2, C, A, full ABS(X), rollLower.
  std::array<Interval, 16> chart{};
  // Lsum, M, Ldiff, m, L1sq, L2sq, Delta, sqrt3half, b.
  std::array<Interval, 9> link{};
  // Per side: W, cutlow, cuthi, cutturn, p, q; no overwritten origins.
  std::array<Interval, 12> cut{};
  // Per side: rho_p/rho_q, ankle lower/upper, reach lower/upper;
  // the last six slots are sequential endpoint/current primitive scratch.
  std::array<Interval, 18> endpoint{};
  std::array<Interval, 18> direct{};
  std::array<double, 10> cuts{};
  std::array<double, 8> selected{};
  std::array<bool, 2> endpoint_certificate{};
  const auto guard = [&](std::uint8_t row, std::uint8_t side, auto predicate,
                         Slice4Why ordinary) {
    r = EP4R{};
    r.self_stage = EP4Stage::slice_guard;
    r.operation = row;
    if (side < 2) r.side = side;
    d.slice.guard = row;
    d.slice.side = side;
    d.slice.limiting_bound = {};
    if (!intermediate_endpoint04_charge(d, r, d.work.construction_guards,
                                        limits.construction_guards,
                                        EP4W::slice_guard_capacity)) {
      d.slice.condition = Slice4Why::guard_capacity;
      return false;
    }
    d.slice.guard_attempted |= std::uint64_t{1} << row;
    EP4W why = ordinary == Slice4Why::identity ? EP4W::slice_identity
                                               : EP4W::slice_unavailable;
    const bool good = predicate(why);
    if (!good)
      return ep4_slice_stop(d, r, why,
                            why == EP4W::unsupported_arithmetic
                                ? Slice4Why::unsupported_arithmetic
                                : ordinary);
    d.slice.guard_written |= std::uint64_t{1} << row;
    return true;
  };
  const auto operation = [&](std::uint8_t row, std::uint8_t side,
                             Interval& output, auto compute) {
    r = EP4R{};
    r.self_stage = EP4Stage::slice_operation;
    r.operation = row;
    if (side < 2) r.side = side;
    d.slice.operation = row;
    d.slice.side = side;
    d.slice.limiting_bound = {};
    if (!intermediate_endpoint04_charge(d, r, d.work.construction_operations,
                                        limits.construction_operations,
                                        EP4W::slice_operation_capacity)) {
      d.slice.condition = Slice4Why::operation_capacity;
      return false;
    }
    d.slice.operation_attempted[row / 64] |= std::uint64_t{1} << (row % 64);
    const auto value = compute();
    if (!ep4_valid(value))
      return ep4_slice_stop(d, r, EP4W::unsupported_arithmetic,
                            Slice4Why::unsupported_arithmetic);
    output = value;
    d.slice.limiting_bound = {value.low, value.high, true};
    d.slice.operation_written[row / 64] |= std::uint64_t{1} << (row % 64);
    d.slice.arithmetic_supported = true;
    return true;
  };
  // These helpers are invoked exclusively inside successfully charged rows.
  const auto supported = [&](EP4W& why, const Interval& v) {
    if (ep4_valid(v)) return true;
    why = EP4W::unsupported_arithmetic;
    return false;
  };
  const auto attach = [&](const Interval& v) {
    d.slice.limiting_bound = {v.low, v.high, true};
    return false;
  };
  if (!guard(
          0, 255,
          [&](EP4W&) {
            return d.source_enrolled && d.work.source_guards == 64 &&
                   d.source_evaluated == UINT64_MAX &&
                   BoardingIntermediatePauseSupportAccess::valid(d.source) &&
                   BoardingIntermediatePauseSupportAccess::data(d.source) &&
                   d.parts.size() == 15;
          },
          Slice4Why::identity))
    return false;
  if (!guard(
          1, 255,
          [&](EP4W& why) {
            why = EP4W::unsupported_arithmetic;
            return boarding_route_foot_phase_environment();
          },
          Slice4Why::unsupported_arithmetic))
    return false;
  if (!guard(
          2, 255,
          [&](EP4W&) {
            return d.version == 4 &&
                   d.candidate == BoardingIntermediateEndpoint04Candidate::
                                      root_y_both_ankle_reach_roll_slice &&
                   intermediate_endpoint04_template_valid(request, false, 0);
          },
          Slice4Why::identity))
    return false;
  if (!guard(
          3, 255,
          [&](EP4W& why) {
            if (!ep4_packet_structure(request) ||
                !intermediate_endpoint04_template_valid(request, false, 0))
              return false;
            if (!ep4_packet_domains(request) ||
                !ep4_construction_packet_domains(request)) {
              why = EP4W::unsupported_arithmetic;
              return false;
            }
            return true;
          },
          Slice4Why::identity))
    return false;
  if (!guard(
          4, 255,
          [&](EP4W& why) {
            if (!std::isfinite(thigh_length) || !std::isfinite(shin_length) ||
                thigh_length <= 0 || shin_length <= 0 || thigh_length > 1 ||
                shin_length > 1) {
              why = EP4W::unsupported_arithmetic;
              return false;
            }
            return kBoardingRouteFootPhaseVersion == 1 &&
                   thigh_length == .47285 && shin_length == .47478 &&
                   thigh_length > 0 && shin_length > 0 && thigh_length <= 1 &&
                   shin_length <= 1 && ep4_source_bit(d, 62);
          },
          Slice4Why::identity))
    return false;
  if (!guard(
          5, 255,
          [&](EP4W&) {
            // The source compiler's exact-real nominal rational yaw
            // construction, not midpoint or rounded affine orthogonality, owns
            // these identities.
            return ep4_source_bit(d, 63) &&
                   kBoardingRouteFootPhaseVersion == 1 &&
                   intermediate_endpoint04_template_valid(request, false, 0) &&
                   request.feet[0].yaw_half == std::array<double, 2>{0, 0} &&
                   request.feet[1].yaw_half == std::array<double, 2>{0, 0};
          },
          Slice4Why::identity))
    return false;
  // O001
  if (!operation(0, 255, input[0], [&] {
        return phase_constant(request.root[0].coordinates[0]);
      }))
    return false;
  // O002
  if (!operation(1, 255, input[1], [&] {
        return phase_constant(request.root[0].coordinates[2]);
      }))
    return false;
  // O003
  if (!operation(2, 255, input[2], [&] {
        return phase_constant(request.feet[0].sole[0].coordinates[0]);
      }))
    return false;
  // O004
  if (!operation(3, 255, input[3], [&] {
        return phase_constant(request.feet[0].sole[0].coordinates[1]);
      }))
    return false;
  // O005
  if (!operation(4, 255, input[4], [&] {
        return phase_constant(request.feet[0].sole[0].coordinates[2]);
      }))
    return false;
  // O006
  if (!operation(5, 255, input[5], [&] {
        return phase_constant(request.feet[1].sole[0].coordinates[0]);
      }))
    return false;
  // O007
  if (!operation(6, 255, input[6], [&] {
        return phase_constant(request.feet[1].sole[0].coordinates[1]);
      }))
    return false;
  // O008
  if (!operation(7, 255, input[7], [&] {
        return phase_constant(request.feet[1].sole[0].coordinates[2]);
      }))
    return false;
  // O009
  if (!operation(8, 255, yaw[0],
                 [&] { return square(point(request.root_yaw_half[0])); }))
    return false;
  // O010
  if (!operation(9, 255, yaw[1], [&] { return add(point(1), yaw[0]); }))
    return false;
  // O011
  if (!operation(10, 255, yaw[2], [&] { return subtract(point(1), yaw[0]); }))
    return false;
  // O012
  if (!operation(11, 255, yaw[3], [&] { return divide(yaw[2], yaw[1]); }))
    return false;
  // O013
  if (!operation(12, 255, yaw[2], [&] {
        return multiply(point(2), point(request.root_yaw_half[0]));
      }))
    return false;
  // O014
  if (!operation(13, 255, yaw[4], [&] { return divide(yaw[2], yaw[1]); }))
    return false;
  // O015
  if (!operation(14, 255, yaw[5], [&] { return negate(yaw[4]); })) return false;
  // O016
  if (!operation(15, 0, endpoint[12],
                 [&] { return multiply(point(-.14), yaw[3]); }))
    return false;
  // O017
  if (!operation(16, 0, endpoint[13],
                 [&] { return multiply(point(-.14), yaw[5]); }))
    return false;
  // O018
  if (!operation(17, 0, endpoint[14],
                 [&] { return add(input[0], endpoint[12]); }))
    return false;
  // O019
  if (!operation(18, 0, endpoint[15],
                 [&] { return add(input[1], endpoint[13]); }))
    return false;
  // O020
  if (!operation(19, 0, chart[0],
                 [&] { return subtract(input[2], endpoint[14]); }))
    return false;
  // O021
  if (!operation(20, 0, chart[1],
                 [&] { return subtract(input[4], endpoint[15]); }))
    return false;
  // O022
  if (!operation(21, 0, chart[2], [&] { return square(chart[0]); }))
    return false;
  // O023
  if (!operation(22, 0, chart[3], [&] { return square(chart[1]); }))
    return false;
  // O024
  if (!operation(23, 0, chart[4], [&] { return add(chart[2], chart[3]); }))
    return false;
  // O025
  if (!operation(24, 0, chart[5], [&] { return add(input[3], point(.1)); }))
    return false;
  // O026
  if (!operation(25, 1, endpoint[12],
                 [&] { return multiply(point(.14), yaw[3]); }))
    return false;
  // O027
  if (!operation(26, 1, endpoint[13],
                 [&] { return multiply(point(.14), yaw[5]); }))
    return false;
  // O028
  if (!operation(27, 1, endpoint[14],
                 [&] { return add(input[0], endpoint[12]); }))
    return false;
  // O029
  if (!operation(28, 1, endpoint[15],
                 [&] { return add(input[1], endpoint[13]); }))
    return false;
  // O030
  if (!operation(29, 1, chart[8],
                 [&] { return subtract(input[5], endpoint[14]); }))
    return false;
  // O031
  if (!operation(30, 1, chart[9],
                 [&] { return subtract(input[7], endpoint[15]); }))
    return false;
  // O032
  if (!operation(31, 1, chart[10], [&] { return square(chart[8]); }))
    return false;
  // O033
  if (!operation(32, 1, chart[11], [&] { return square(chart[9]); }))
    return false;
  // O034
  if (!operation(33, 1, chart[12], [&] { return add(chart[10], chart[11]); }))
    return false;
  // O035
  if (!operation(34, 1, chart[13], [&] { return add(input[6], point(.1)); }))
    return false;
  // O036
  if (!operation(35, 255, link[0],
                 [&] { return add(point(thigh_length), point(shin_length)); }))
    return false;
  // O037
  if (!operation(36, 255, link[1], [&] { return square(link[0]); }))
    return false;
  // O038
  if (!operation(37, 255, link[2], [&] {
        return subtract(point(thigh_length), point(shin_length));
      }))
    return false;
  // O039
  if (!operation(38, 255, link[3], [&] { return square(link[2]); }))
    return false;
  // O040
  if (!operation(39, 255, link[4], [&] { return square(point(thigh_length)); }))
    return false;
  // O041
  if (!operation(40, 255, link[5], [&] { return square(point(shin_length)); }))
    return false;
  // O042
  if (!operation(41, 255, link[6], [&] { return subtract(link[4], link[5]); }))
    return false;
  // O043
  if (!operation(42, 255, yaw[0],
                 [&] { return transfer_small_root(point(3)); }))
    return false;
  // O044
  if (!operation(43, 255, link[7], [&] { return divide(yaw[0], point(2)); }))
    return false;
  // O045
  if (!operation(44, 255, link[8], [&] { return subtract(point(2), yaw[0]); }))
    return false;
  // G07
  if (!guard(
          6, 255,
          [&](EP4W& why) {
            if (!supported(why, link[8])) return false;
            return link[8].low > 0 || attach(link[8]);
          },
          Slice4Why::no_positive_roll_factor))
    return false;
  // O046
  if (!operation(45, 0, cut[0], [&] { return negate(chart[1]); })) return false;
  // O047
  if (!operation(46, 0, endpoint[12],
                 [&] { return subtract(cut[0], point(thigh_length)); }))
    return false;
  // O048
  if (!operation(47, 0, cut[1],
                 [&] { return divide(endpoint[12], point(shin_length)); }))
    return false;
  // O049
  if (!operation(48, 0, endpoint[12],
                 [&] { return add(cut[0], point(thigh_length)); }))
    return false;
  // O050
  if (!operation(49, 0, cut[2],
                 [&] { return divide(endpoint[12], point(shin_length)); }))
    return false;
  // O051
  if (!operation(50, 0, cut[3], [&] { return divide(cut[0], link[0]); }))
    return false;
  // O052
  if (!operation(51, 0, cut[4],
                 [&] { return point(std::max(-.5, cut[1].high)); }))
    return false;
  // O053
  if (!operation(52, 0, cut[5],
                 [&] { return point(std::min(.5, cut[2].low)); }))
    return false;
  // O054
  if (!operation(53, 0, cut[5],
                 [&] { return point(std::min(cut[5].low, cut[3].low)); }))
    return false;
  // G08
  if (!guard(
          7, 0,
          [&](EP4W& why) {
            for (const auto* v :
                 {&cut[0], &cut[1], &cut[2], &cut[3], &cut[4], &cut[5]})
              if (!supported(why, *v)) return false;
            if (cut[4].low != cut[4].high || cut[5].low != cut[5].high) {
              why = EP4W::unsupported_arithmetic;
              return false;
            }
            if (cut[4].low < -.5) return attach(cut[4]);
            if (cut[4].low >= cut[5].low || cut[5].low > .5)
              return attach(cut[5]);
            if (cut[4].low < cut[1].high) return attach(cut[4]);
            if (cut[5].low > cut[2].low || cut[5].low > cut[3].low)
              return attach(cut[5]);
            endpoint_certificate[0] = true;
            return true;
          },
          Slice4Why::no_tau_interval))
    return false;
  // O055
  if (!operation(54, 0, endpoint[12], [&] { return square(cut[4]); }))
    return false;
  // O056
  if (!operation(55, 0, endpoint[13],
                 [&] { return subtract(point(1), endpoint[12]); }))
    return false;
  // G09
  if (!guard(
          8, 0,
          [&](EP4W& why) {
            if (!endpoint_certificate[0]) return false;
            if (!supported(why, endpoint[13])) return false;
            return endpoint[13].low > 0 || attach(endpoint[13]);
          },
          Slice4Why::endpoint_domain_unavailable))
    return false;
  // O057
  if (!operation(56, 0, endpoint[13],
                 [&] { return transfer_small_root(endpoint[13]); }))
    return false;
  // O058
  if (!operation(57, 0, endpoint[14],
                 [&] { return multiply(point(shin_length), endpoint[13]); }))
    return false;
  // O059
  if (!operation(58, 0, endpoint[15],
                 [&] { return multiply(point(shin_length), cut[4]); }))
    return false;
  // O060
  if (!operation(59, 0, endpoint[15],
                 [&] { return subtract(cut[0], endpoint[15]); }))
    return false;
  // O061
  if (!operation(60, 0, endpoint[16], [&] { return square(endpoint[15]); }))
    return false;
  // O062
  if (!operation(61, 0, endpoint[16],
                 [&] { return subtract(link[4], endpoint[16]); }))
    return false;
  // G10
  if (!guard(
          9, 0,
          [&](EP4W& why) {
            if (!endpoint_certificate[0]) return false;
            for (const auto* v : {&cut[0], &cut[4], &cut[5], &endpoint[16]})
              if (!supported(why, *v)) return false;
            return endpoint[16].high >= 0 || attach(endpoint[16]);
          },
          Slice4Why::endpoint_domain_unavailable))
    return false;
  // O063
  if (!operation(62, 0, endpoint[16], [&] {
        return [&] {
          const bool zero_branch = endpoint[16].low <= 0;
          const auto value = interval(zero_branch ? +0.0 : endpoint[16].low,
                                      endpoint[16].high);
          if (ep4_valid(value) && zero_branch)
            d.slice.zero_mask |= std::uint8_t{1} << 2;
          return value;
        }();
      }))
    return false;
  // O064
  if (!operation(63, 0, endpoint[17],
                 [&] { return transfer_small_root(endpoint[16]); }))
    return false;
  // O065
  if (!operation(64, 0, endpoint[0],
                 [&] { return add(endpoint[17], endpoint[14]); }))
    return false;
  // O066
  if (!operation(65, 0, endpoint[12], [&] { return square(cut[5]); }))
    return false;
  // O067
  if (!operation(66, 0, endpoint[13],
                 [&] { return subtract(point(1), endpoint[12]); }))
    return false;
  // G11
  if (!guard(
          10, 0,
          [&](EP4W& why) {
            if (!endpoint_certificate[0]) return false;
            if (!supported(why, endpoint[13])) return false;
            return endpoint[13].low > 0 || attach(endpoint[13]);
          },
          Slice4Why::endpoint_domain_unavailable))
    return false;
  // O068
  if (!operation(67, 0, endpoint[13],
                 [&] { return transfer_small_root(endpoint[13]); }))
    return false;
  // O069
  if (!operation(68, 0, endpoint[14],
                 [&] { return multiply(point(shin_length), endpoint[13]); }))
    return false;
  // O070
  if (!operation(69, 0, endpoint[15],
                 [&] { return multiply(point(shin_length), cut[5]); }))
    return false;
  // O071
  if (!operation(70, 0, endpoint[15],
                 [&] { return subtract(cut[0], endpoint[15]); }))
    return false;
  // O072
  if (!operation(71, 0, endpoint[16], [&] { return square(endpoint[15]); }))
    return false;
  // O073
  if (!operation(72, 0, endpoint[16],
                 [&] { return subtract(link[4], endpoint[16]); }))
    return false;
  // G12
  if (!guard(
          11, 0,
          [&](EP4W& why) {
            if (!endpoint_certificate[0]) return false;
            for (const auto* v : {&cut[0], &cut[4], &cut[5], &endpoint[16]})
              if (!supported(why, *v)) return false;
            return endpoint[16].high >= 0 || attach(endpoint[16]);
          },
          Slice4Why::endpoint_domain_unavailable))
    return false;
  // O074
  if (!operation(73, 0, endpoint[16], [&] {
        return [&] {
          const bool zero_branch = endpoint[16].low <= 0;
          const auto value = interval(zero_branch ? +0.0 : endpoint[16].low,
                                      endpoint[16].high);
          if (ep4_valid(value) && zero_branch)
            d.slice.zero_mask |= std::uint8_t{1} << 3;
          return value;
        }();
      }))
    return false;
  // O075
  if (!operation(74, 0, endpoint[17],
                 [&] { return transfer_small_root(endpoint[16]); }))
    return false;
  // O076
  if (!operation(75, 0, endpoint[1],
                 [&] { return add(endpoint[17], endpoint[14]); }))
    return false;
  // O077
  if (!operation(76, 0, endpoint[12], [&] { return point(endpoint[0].high); }))
    return false;
  cuts[0] = endpoint[12].low;
  // O078
  if (!operation(77, 0, endpoint[13], [&] { return point(endpoint[1].low); }))
    return false;
  cuts[1] = endpoint[13].low;
  // G13
  if (!guard(
          12, 0,
          [&](EP4W& why) {
            if (!supported(why, endpoint[12]) || !supported(why, endpoint[13]))
              return false;
            if (endpoint[12].low != endpoint[12].high ||
                endpoint[13].low != endpoint[13].high) {
              why = EP4W::unsupported_arithmetic;
              return false;
            }
            if (cuts[0] < 0) return attach(endpoint[12]);
            return cuts[0] < cuts[1] || attach(endpoint[13]);
          },
          Slice4Why::no_radial_interval))
    return false;
  // O079
  if (!operation(78, 0, endpoint[12], [&] { return square(point(cuts[0])); }))
    return false;
  // O080
  if (!operation(79, 0, endpoint[12],
                 [&] { return subtract(endpoint[12], point(chart[2].low)); }))
    return false;
  // O081
  if (!operation(80, 0, endpoint[12], [&] {
        return [&] {
          const bool zero_branch = endpoint[12].high <= 0;
          const auto value = point(zero_branch ? +0.0 : endpoint[12].high);
          if (ep4_valid(value) && zero_branch)
            d.slice.zero_mask |= std::uint8_t{1} << 6;
          return value;
        }();
      }))
    return false;
  // O082
  if (!operation(81, 0, endpoint[12],
                 [&] { return transfer_small_root(endpoint[12]); }))
    return false;
  // O083
  if (!operation(82, 0, endpoint[2], [&] {
        return add(point(chart[5].high), point(endpoint[12].high));
      }))
    return false;
  // O084
  if (!operation(83, 0, endpoint[12], [&] { return square(point(cuts[1])); }))
    return false;
  // O085
  if (!operation(84, 0, endpoint[12],
                 [&] { return subtract(endpoint[12], point(chart[2].high)); }))
    return false;
  // G14
  if (!guard(
          13, 0,
          [&](EP4W& why) {
            if (!supported(why, endpoint[12])) return false;
            return endpoint[12].low > 0 || attach(endpoint[12]);
          },
          Slice4Why::no_positive_upper))
    return false;
  // O086
  if (!operation(85, 0, endpoint[12],
                 [&] { return transfer_small_root(point(endpoint[12].low)); }))
    return false;
  // O087
  if (!operation(86, 0, endpoint[3], [&] {
        return add(point(chart[5].low), point(endpoint[12].low));
      }))
    return false;
  // O088
  if (!operation(87, 0, endpoint[12], [&] {
        return subtract(point(link[3].high), point(chart[4].low));
      }))
    return false;
  // O089
  if (!operation(88, 0, endpoint[12], [&] {
        return [&] {
          const bool zero_branch = endpoint[12].high <= 0;
          const auto value = point(zero_branch ? +0.0 : endpoint[12].high);
          if (ep4_valid(value) && zero_branch)
            d.slice.zero_mask |= std::uint8_t{1} << 0;
          return value;
        }();
      }))
    return false;
  // O090
  if (!operation(89, 0, endpoint[12],
                 [&] { return transfer_small_root(endpoint[12]); }))
    return false;
  // O091
  if (!operation(90, 0, endpoint[4], [&] {
        return add(point(chart[5].high), point(endpoint[12].high));
      }))
    return false;
  // O092
  if (!operation(91, 0, endpoint[12], [&] {
        return subtract(point(link[1].low), point(chart[4].high));
      }))
    return false;
  // G15
  if (!guard(
          14, 0,
          [&](EP4W& why) {
            if (!supported(why, endpoint[12])) return false;
            return endpoint[12].low > 0 || attach(endpoint[12]);
          },
          Slice4Why::no_positive_upper))
    return false;
  // O093
  if (!operation(92, 0, endpoint[12],
                 [&] { return transfer_small_root(point(endpoint[12].low)); }))
    return false;
  // O094
  if (!operation(93, 0, endpoint[5], [&] {
        return add(point(chart[5].low), point(endpoint[12].low));
      }))
    return false;
  // O095
  if (!operation(94, 0, chart[6], [&] { return absolute(chart[0]); }))
    return false;
  // O096
  if (!operation(95, 0, endpoint[12],
                 [&] { return divide(chart[6], link[8]); }))
    return false;
  // O097
  if (!operation(96, 0, chart[7], [&] {
        return add(point(chart[5].high), point(endpoint[12].high));
      }))
    return false;
  // O098
  if (!operation(97, 1, cut[6], [&] { return negate(chart[9]); })) return false;
  // O099
  if (!operation(98, 1, endpoint[12],
                 [&] { return subtract(cut[6], point(thigh_length)); }))
    return false;
  // O100
  if (!operation(99, 1, cut[7],
                 [&] { return divide(endpoint[12], point(shin_length)); }))
    return false;
  // O101
  if (!operation(100, 1, endpoint[12],
                 [&] { return add(cut[6], point(thigh_length)); }))
    return false;
  // O102
  if (!operation(101, 1, cut[8],
                 [&] { return divide(endpoint[12], point(shin_length)); }))
    return false;
  // O103
  if (!operation(102, 1, cut[9], [&] { return divide(cut[6], link[0]); }))
    return false;
  // O104
  if (!operation(103, 1, cut[10],
                 [&] { return point(std::max(-.5, cut[7].high)); }))
    return false;
  // O105
  if (!operation(104, 1, cut[11],
                 [&] { return point(std::min(.5, cut[8].low)); }))
    return false;
  // O106
  if (!operation(105, 1, cut[11],
                 [&] { return point(std::min(cut[11].low, cut[9].low)); }))
    return false;
  // G16
  if (!guard(
          15, 1,
          [&](EP4W& why) {
            for (const auto* v :
                 {&cut[6], &cut[7], &cut[8], &cut[9], &cut[10], &cut[11]})
              if (!supported(why, *v)) return false;
            if (cut[10].low != cut[10].high || cut[11].low != cut[11].high) {
              why = EP4W::unsupported_arithmetic;
              return false;
            }
            if (cut[10].low < -.5) return attach(cut[10]);
            if (cut[10].low >= cut[11].low || cut[11].low > .5)
              return attach(cut[11]);
            if (cut[10].low < cut[7].high) return attach(cut[10]);
            if (cut[11].low > cut[8].low || cut[11].low > cut[9].low)
              return attach(cut[11]);
            endpoint_certificate[1] = true;
            return true;
          },
          Slice4Why::no_tau_interval))
    return false;
  // O107
  if (!operation(106, 1, endpoint[12], [&] { return square(cut[10]); }))
    return false;
  // O108
  if (!operation(107, 1, endpoint[13],
                 [&] { return subtract(point(1), endpoint[12]); }))
    return false;
  // G17
  if (!guard(
          16, 1,
          [&](EP4W& why) {
            if (!endpoint_certificate[1]) return false;
            if (!supported(why, endpoint[13])) return false;
            return endpoint[13].low > 0 || attach(endpoint[13]);
          },
          Slice4Why::endpoint_domain_unavailable))
    return false;
  // O109
  if (!operation(108, 1, endpoint[13],
                 [&] { return transfer_small_root(endpoint[13]); }))
    return false;
  // O110
  if (!operation(109, 1, endpoint[14],
                 [&] { return multiply(point(shin_length), endpoint[13]); }))
    return false;
  // O111
  if (!operation(110, 1, endpoint[15],
                 [&] { return multiply(point(shin_length), cut[10]); }))
    return false;
  // O112
  if (!operation(111, 1, endpoint[15],
                 [&] { return subtract(cut[6], endpoint[15]); }))
    return false;
  // O113
  if (!operation(112, 1, endpoint[16], [&] { return square(endpoint[15]); }))
    return false;
  // O114
  if (!operation(113, 1, endpoint[16],
                 [&] { return subtract(link[4], endpoint[16]); }))
    return false;
  // G18
  if (!guard(
          17, 1,
          [&](EP4W& why) {
            if (!endpoint_certificate[1]) return false;
            for (const auto* v : {&cut[6], &cut[10], &cut[11], &endpoint[16]})
              if (!supported(why, *v)) return false;
            return endpoint[16].high >= 0 || attach(endpoint[16]);
          },
          Slice4Why::endpoint_domain_unavailable))
    return false;
  // O115
  if (!operation(114, 1, endpoint[16], [&] {
        return [&] {
          const bool zero_branch = endpoint[16].low <= 0;
          const auto value = interval(zero_branch ? +0.0 : endpoint[16].low,
                                      endpoint[16].high);
          if (ep4_valid(value) && zero_branch)
            d.slice.zero_mask |= std::uint8_t{1} << 4;
          return value;
        }();
      }))
    return false;
  // O116
  if (!operation(115, 1, endpoint[17],
                 [&] { return transfer_small_root(endpoint[16]); }))
    return false;
  // O117
  if (!operation(116, 1, endpoint[6],
                 [&] { return add(endpoint[17], endpoint[14]); }))
    return false;
  // O118
  if (!operation(117, 1, endpoint[12], [&] { return square(cut[11]); }))
    return false;
  // O119
  if (!operation(118, 1, endpoint[13],
                 [&] { return subtract(point(1), endpoint[12]); }))
    return false;
  // G19
  if (!guard(
          18, 1,
          [&](EP4W& why) {
            if (!endpoint_certificate[1]) return false;
            if (!supported(why, endpoint[13])) return false;
            return endpoint[13].low > 0 || attach(endpoint[13]);
          },
          Slice4Why::endpoint_domain_unavailable))
    return false;
  // O120
  if (!operation(119, 1, endpoint[13],
                 [&] { return transfer_small_root(endpoint[13]); }))
    return false;
  // O121
  if (!operation(120, 1, endpoint[14],
                 [&] { return multiply(point(shin_length), endpoint[13]); }))
    return false;
  // O122
  if (!operation(121, 1, endpoint[15],
                 [&] { return multiply(point(shin_length), cut[11]); }))
    return false;
  // O123
  if (!operation(122, 1, endpoint[15],
                 [&] { return subtract(cut[6], endpoint[15]); }))
    return false;
  // O124
  if (!operation(123, 1, endpoint[16], [&] { return square(endpoint[15]); }))
    return false;
  // O125
  if (!operation(124, 1, endpoint[16],
                 [&] { return subtract(link[4], endpoint[16]); }))
    return false;
  // G20
  if (!guard(
          19, 1,
          [&](EP4W& why) {
            if (!endpoint_certificate[1]) return false;
            for (const auto* v : {&cut[6], &cut[10], &cut[11], &endpoint[16]})
              if (!supported(why, *v)) return false;
            return endpoint[16].high >= 0 || attach(endpoint[16]);
          },
          Slice4Why::endpoint_domain_unavailable))
    return false;
  // O126
  if (!operation(125, 1, endpoint[16], [&] {
        return [&] {
          const bool zero_branch = endpoint[16].low <= 0;
          const auto value = interval(zero_branch ? +0.0 : endpoint[16].low,
                                      endpoint[16].high);
          if (ep4_valid(value) && zero_branch)
            d.slice.zero_mask |= std::uint8_t{1} << 5;
          return value;
        }();
      }))
    return false;
  // O127
  if (!operation(126, 1, endpoint[17],
                 [&] { return transfer_small_root(endpoint[16]); }))
    return false;
  // O128
  if (!operation(127, 1, endpoint[7],
                 [&] { return add(endpoint[17], endpoint[14]); }))
    return false;
  // O129
  if (!operation(128, 1, endpoint[12], [&] { return point(endpoint[6].high); }))
    return false;
  cuts[2] = endpoint[12].low;
  // O130
  if (!operation(129, 1, endpoint[13], [&] { return point(endpoint[7].low); }))
    return false;
  cuts[3] = endpoint[13].low;
  // G21
  if (!guard(
          20, 1,
          [&](EP4W& why) {
            if (!supported(why, endpoint[12]) || !supported(why, endpoint[13]))
              return false;
            if (endpoint[12].low != endpoint[12].high ||
                endpoint[13].low != endpoint[13].high) {
              why = EP4W::unsupported_arithmetic;
              return false;
            }
            if (cuts[2] < 0) return attach(endpoint[12]);
            return cuts[2] < cuts[3] || attach(endpoint[13]);
          },
          Slice4Why::no_radial_interval))
    return false;
  // O131
  if (!operation(130, 1, endpoint[12], [&] { return square(point(cuts[2])); }))
    return false;
  // O132
  if (!operation(131, 1, endpoint[12],
                 [&] { return subtract(endpoint[12], point(chart[10].low)); }))
    return false;
  // O133
  if (!operation(132, 1, endpoint[12], [&] {
        return [&] {
          const bool zero_branch = endpoint[12].high <= 0;
          const auto value = point(zero_branch ? +0.0 : endpoint[12].high);
          if (ep4_valid(value) && zero_branch)
            d.slice.zero_mask |= std::uint8_t{1} << 7;
          return value;
        }();
      }))
    return false;
  // O134
  if (!operation(133, 1, endpoint[12],
                 [&] { return transfer_small_root(endpoint[12]); }))
    return false;
  // O135
  if (!operation(134, 1, endpoint[8], [&] {
        return add(point(chart[13].high), point(endpoint[12].high));
      }))
    return false;
  // O136
  if (!operation(135, 1, endpoint[12], [&] { return square(point(cuts[3])); }))
    return false;
  // O137
  if (!operation(136, 1, endpoint[12],
                 [&] { return subtract(endpoint[12], point(chart[10].high)); }))
    return false;
  // G22
  if (!guard(
          21, 1,
          [&](EP4W& why) {
            if (!supported(why, endpoint[12])) return false;
            return endpoint[12].low > 0 || attach(endpoint[12]);
          },
          Slice4Why::no_positive_upper))
    return false;
  // O138
  if (!operation(137, 1, endpoint[12],
                 [&] { return transfer_small_root(point(endpoint[12].low)); }))
    return false;
  // O139
  if (!operation(138, 1, endpoint[9], [&] {
        return add(point(chart[13].low), point(endpoint[12].low));
      }))
    return false;
  // O140
  if (!operation(139, 1, endpoint[12], [&] {
        return subtract(point(link[3].high), point(chart[12].low));
      }))
    return false;
  // O141
  if (!operation(140, 1, endpoint[12], [&] {
        return [&] {
          const bool zero_branch = endpoint[12].high <= 0;
          const auto value = point(zero_branch ? +0.0 : endpoint[12].high);
          if (ep4_valid(value) && zero_branch)
            d.slice.zero_mask |= std::uint8_t{1} << 1;
          return value;
        }();
      }))
    return false;
  // O142
  if (!operation(141, 1, endpoint[12],
                 [&] { return transfer_small_root(endpoint[12]); }))
    return false;
  // O143
  if (!operation(142, 1, endpoint[10], [&] {
        return add(point(chart[13].high), point(endpoint[12].high));
      }))
    return false;
  // O144
  if (!operation(143, 1, endpoint[12], [&] {
        return subtract(point(link[1].low), point(chart[12].high));
      }))
    return false;
  // G23
  if (!guard(
          22, 1,
          [&](EP4W& why) {
            if (!supported(why, endpoint[12])) return false;
            return endpoint[12].low > 0 || attach(endpoint[12]);
          },
          Slice4Why::no_positive_upper))
    return false;
  // O145
  if (!operation(144, 1, endpoint[12],
                 [&] { return transfer_small_root(point(endpoint[12].low)); }))
    return false;
  // O146
  if (!operation(145, 1, endpoint[11], [&] {
        return add(point(chart[13].low), point(endpoint[12].low));
      }))
    return false;
  // O147
  if (!operation(146, 1, chart[14], [&] { return absolute(chart[8]); }))
    return false;
  // O148
  if (!operation(147, 1, endpoint[12],
                 [&] { return divide(chart[14], link[8]); }))
    return false;
  // O149
  if (!operation(148, 1, chart[15], [&] {
        return add(point(chart[13].high), point(endpoint[12].high));
      }))
    return false;
  // O150
  if (!operation(149, 255, endpoint[12], [&] {
        return point(std::max(endpoint[2].high, endpoint[8].high));
      }))
    return false;
  selected[0] = endpoint[12].low;
  // O151
  if (!operation(150, 255, endpoint[12], [&] {
        return point(std::max(selected[0], endpoint[4].high));
      }))
    return false;
  selected[0] = endpoint[12].low;
  // O152
  if (!operation(151, 255, endpoint[12], [&] {
        return point(std::max(selected[0], endpoint[10].high));
      }))
    return false;
  selected[0] = endpoint[12].low;
  // O153
  if (!operation(152, 255, endpoint[12],
                 [&] { return point(std::max(selected[0], chart[7].high)); }))
    return false;
  selected[0] = endpoint[12].low;
  // O154
  if (!operation(153, 255, endpoint[12],
                 [&] { return point(std::max(selected[0], chart[15].high)); }))
    return false;
  selected[0] = endpoint[12].low;
  d.slice.lo = selected[0];
  // O155
  if (!operation(154, 255, endpoint[12], [&] {
        return point(std::min(endpoint[3].low, endpoint[9].low));
      }))
    return false;
  selected[1] = endpoint[12].low;
  // O156
  if (!operation(155, 255, endpoint[12],
                 [&] { return point(std::min(selected[1], endpoint[5].low)); }))
    return false;
  selected[1] = endpoint[12].low;
  // O157
  if (!operation(156, 255, endpoint[12], [&] {
        return point(std::min(selected[1], endpoint[11].low));
      }))
    return false;
  selected[1] = endpoint[12].low;
  d.slice.hi = selected[1];
  // G24
  if (!guard(
          23, 255,
          [&](EP4W& why) {
            if (!std::isfinite(d.slice.lo) || !std::isfinite(d.slice.hi)) {
              why = EP4W::unsupported_arithmetic;
              return false;
            }
            return d.slice.lo < d.slice.hi || attach(point(d.slice.hi));
          },
          Slice4Why::empty_inward_interval))
    return false;
  // O158
  if (!operation(157, 255, endpoint[12],
                 [&] { return point(d.slice.hi - d.slice.lo); }))
    return false;
  selected[2] = endpoint[12].low;
  // O159
  if (!operation(158, 255, endpoint[12],
                 [&] { return point(selected[2] * .5); }))
    return false;
  selected[3] = endpoint[12].low;
  // O160
  if (!operation(159, 255, endpoint[12],
                 [&] { return point(d.slice.lo + selected[3]); }))
    return false;
  selected[4] = endpoint[12].low;
  d.slice.y = selected[4];
  // G25
  if (!guard(
          24, 255,
          [&](EP4W& why) {
            if (!std::isfinite(d.slice.y)) {
              why = EP4W::unsupported_arithmetic;
              return false;
            }
            return std::abs(d.slice.y) <= 8 || attach(point(d.slice.y));
          },
          Slice4Why::candidate_domain))
    return false;
  // G26
  if (!guard(
          25, 255,
          [&](EP4W& why) {
            (void)why;
            return d.slice.y > d.slice.lo || attach(point(d.slice.y));
          },
          Slice4Why::midpoint_unavailable))
    return false;
  // G27
  if (!guard(
          26, 255,
          [&](EP4W& why) {
            (void)why;
            return d.slice.y < d.slice.hi || attach(point(d.slice.y));
          },
          Slice4Why::midpoint_unavailable))
    return false;
  // O161
  if (!operation(160, 0, direct[0],
                 [&] { return subtract(point(d.slice.y), chart[5]); }))
    return false;
  // O162
  if (!operation(161, 0, direct[1], [&] { return square(direct[0]); }))
    return false;
  // O163
  if (!operation(162, 0, direct[1], [&] { return add(chart[2], direct[1]); }))
    return false;
  // O164
  if (!operation(163, 0, direct[2], [&] { return add(direct[1], chart[3]); }))
    return false;
  // G28
  if (!guard(
          27, 0,
          [&](EP4W& why) {
            for (const auto* v : {&direct[0], &direct[1], &direct[2]})
              if (!supported(why, *v)) return false;
            if (direct[0].low <= 0) return attach(direct[0]);
            if (direct[1].low <= 0) return attach(direct[1]);
            if (direct[2].low <= link[3].high) return attach(direct[2]);
            return direct[2].high < link[1].low || attach(direct[2]);
          },
          Slice4Why::verification_inconclusive))
    return false;
  // O165
  if (!operation(164, 0, direct[3],
                 [&] { return transfer_small_root(direct[1]); }))
    return false;
  // O166
  if (!operation(165, 0, direct[4], [&] { return add(link[6], direct[2]); }))
    return false;
  // O167
  if (!operation(166, 0, direct[5],
                 [&] { return multiply(point(2), direct[2]); }))
    return false;
  // O168
  if (!operation(167, 0, direct[4],
                 [&] { return divide(direct[4], direct[5]); }))
    return false;
  // O169
  if (!operation(168, 0, direct[6],
                 [&] { return subtract(link[1], direct[2]); }))
    return false;
  // O170
  if (!operation(169, 0, direct[7],
                 [&] { return subtract(direct[2], link[3]); }))
    return false;
  // O171
  if (!operation(170, 0, direct[6],
                 [&] { return multiply(direct[6], direct[7]); }))
    return false;
  // O172
  if (!operation(171, 0, direct[7], [&] { return square(direct[2]); }))
    return false;
  // O173
  if (!operation(172, 0, direct[7],
                 [&] { return multiply(point(4), direct[7]); }))
    return false;
  // O174
  if (!operation(173, 0, direct[7],
                 [&] { return divide(direct[6], direct[7]); }))
    return false;
  // G29
  if (!guard(
          28, 0,
          [&](EP4W& why) {
            if (!supported(why, direct[7])) return false;
            return direct[7].low > 0 || attach(direct[7]);
          },
          Slice4Why::verification_inconclusive))
    return false;
  // O175
  if (!operation(174, 0, direct[7],
                 [&] { return transfer_small_root(direct[7]); }))
    return false;
  // O176
  if (!operation(175, 0, direct[5],
                 [&] { return subtract(point(1), direct[4]); }))
    return false;
  // O177
  if (!operation(176, 0, direct[8],
                 [&] { return multiply(direct[4], direct[3]); }))
    return false;
  // O178
  if (!operation(177, 0, direct[9],
                 [&] { return multiply(direct[7], chart[1]); }))
    return false;
  // O179
  if (!operation(178, 0, direct[8], [&] { return add(direct[8], direct[9]); }))
    return false;
  // O180
  if (!operation(179, 0, direct[10],
                 [&] { return multiply(direct[7], direct[3]); }))
    return false;
  // O181
  if (!operation(180, 0, direct[11],
                 [&] { return multiply(direct[4], chart[1]); }))
    return false;
  // O182
  if (!operation(181, 0, direct[11],
                 [&] { return subtract(direct[10], direct[11]); }))
    return false;
  // O183
  if (!operation(182, 0, direct[12],
                 [&] { return multiply(direct[5], direct[3]); }))
    return false;
  // O184
  if (!operation(183, 0, direct[12],
                 [&] { return subtract(direct[12], direct[9]); }))
    return false;
  // O185
  if (!operation(184, 0, direct[13],
                 [&] { return multiply(direct[5], chart[1]); }))
    return false;
  // O186
  if (!operation(185, 0, direct[13],
                 [&] { return add(direct[13], direct[10]); }))
    return false;
  // O187
  if (!operation(186, 0, direct[13], [&] { return negate(direct[13]); }))
    return false;
  // G30
  if (!guard(
          29, 0,
          [&](EP4W& why) {
            for (const auto* v :
                 {&direct[8], &direct[12], &direct[11], &direct[13]})
              if (!supported(why, *v)) return false;
            if (direct[8].low <= 0) return attach(direct[8]);
            return direct[12].low > 0 || attach(direct[12]);
          },
          Slice4Why::verification_inconclusive))
    return false;
  // O188
  if (!operation(187, 0, direct[14], [&] { return absolute(direct[13]); }))
    return false;
  // O189
  if (!operation(188, 0, direct[15],
                 [&] { return multiply(direct[12], point(.5)); }))
    return false;
  // O190
  if (!operation(189, 0, direct[14],
                 [&] { return multiply(direct[14], link[7]); }))
    return false;
  // O191
  if (!operation(190, 0, direct[15],
                 [&] { return subtract(direct[15], direct[14]); }))
    return false;
  // O192
  if (!operation(191, 0, direct[16],
                 [&] { return multiply(direct[0], link[8]); }))
    return false;
  // O193
  if (!operation(192, 0, direct[16],
                 [&] { return subtract(direct[16], chart[6]); }))
    return false;
  // G31
  if (!guard(
          30, 0,
          [&](EP4W& why) {
            if (!supported(why, direct[15]) || !supported(why, direct[16]))
              return false;
            if (direct[15].low < 0) return attach(direct[15]);
            return direct[16].low >= 0 || attach(direct[16]);
          },
          Slice4Why::verification_inconclusive))
    return false;
  // O194
  if (!operation(193, 1, direct[0],
                 [&] { return subtract(point(d.slice.y), chart[13]); }))
    return false;
  // O195
  if (!operation(194, 1, direct[1], [&] { return square(direct[0]); }))
    return false;
  // O196
  if (!operation(195, 1, direct[1], [&] { return add(chart[10], direct[1]); }))
    return false;
  // O197
  if (!operation(196, 1, direct[2], [&] { return add(direct[1], chart[11]); }))
    return false;
  // G32
  if (!guard(
          31, 1,
          [&](EP4W& why) {
            for (const auto* v : {&direct[0], &direct[1], &direct[2]})
              if (!supported(why, *v)) return false;
            if (direct[0].low <= 0) return attach(direct[0]);
            if (direct[1].low <= 0) return attach(direct[1]);
            if (direct[2].low <= link[3].high) return attach(direct[2]);
            return direct[2].high < link[1].low || attach(direct[2]);
          },
          Slice4Why::verification_inconclusive))
    return false;
  // O198
  if (!operation(197, 1, direct[3],
                 [&] { return transfer_small_root(direct[1]); }))
    return false;
  // O199
  if (!operation(198, 1, direct[4], [&] { return add(link[6], direct[2]); }))
    return false;
  // O200
  if (!operation(199, 1, direct[5],
                 [&] { return multiply(point(2), direct[2]); }))
    return false;
  // O201
  if (!operation(200, 1, direct[4],
                 [&] { return divide(direct[4], direct[5]); }))
    return false;
  // O202
  if (!operation(201, 1, direct[6],
                 [&] { return subtract(link[1], direct[2]); }))
    return false;
  // O203
  if (!operation(202, 1, direct[7],
                 [&] { return subtract(direct[2], link[3]); }))
    return false;
  // O204
  if (!operation(203, 1, direct[6],
                 [&] { return multiply(direct[6], direct[7]); }))
    return false;
  // O205
  if (!operation(204, 1, direct[7], [&] { return square(direct[2]); }))
    return false;
  // O206
  if (!operation(205, 1, direct[7],
                 [&] { return multiply(point(4), direct[7]); }))
    return false;
  // O207
  if (!operation(206, 1, direct[7],
                 [&] { return divide(direct[6], direct[7]); }))
    return false;
  // G33
  if (!guard(
          32, 1,
          [&](EP4W& why) {
            if (!supported(why, direct[7])) return false;
            return direct[7].low > 0 || attach(direct[7]);
          },
          Slice4Why::verification_inconclusive))
    return false;
  // O208
  if (!operation(207, 1, direct[7],
                 [&] { return transfer_small_root(direct[7]); }))
    return false;
  // O209
  if (!operation(208, 1, direct[5],
                 [&] { return subtract(point(1), direct[4]); }))
    return false;
  // O210
  if (!operation(209, 1, direct[8],
                 [&] { return multiply(direct[4], direct[3]); }))
    return false;
  // O211
  if (!operation(210, 1, direct[9],
                 [&] { return multiply(direct[7], chart[9]); }))
    return false;
  // O212
  if (!operation(211, 1, direct[8], [&] { return add(direct[8], direct[9]); }))
    return false;
  // O213
  if (!operation(212, 1, direct[10],
                 [&] { return multiply(direct[7], direct[3]); }))
    return false;
  // O214
  if (!operation(213, 1, direct[11],
                 [&] { return multiply(direct[4], chart[9]); }))
    return false;
  // O215
  if (!operation(214, 1, direct[11],
                 [&] { return subtract(direct[10], direct[11]); }))
    return false;
  // O216
  if (!operation(215, 1, direct[12],
                 [&] { return multiply(direct[5], direct[3]); }))
    return false;
  // O217
  if (!operation(216, 1, direct[12],
                 [&] { return subtract(direct[12], direct[9]); }))
    return false;
  // O218
  if (!operation(217, 1, direct[13],
                 [&] { return multiply(direct[5], chart[9]); }))
    return false;
  // O219
  if (!operation(218, 1, direct[13],
                 [&] { return add(direct[13], direct[10]); }))
    return false;
  // O220
  if (!operation(219, 1, direct[13], [&] { return negate(direct[13]); }))
    return false;
  // G34
  if (!guard(
          33, 1,
          [&](EP4W& why) {
            for (const auto* v :
                 {&direct[8], &direct[12], &direct[11], &direct[13]})
              if (!supported(why, *v)) return false;
            if (direct[8].low <= 0) return attach(direct[8]);
            return direct[12].low > 0 || attach(direct[12]);
          },
          Slice4Why::verification_inconclusive))
    return false;
  // O221
  if (!operation(220, 1, direct[14], [&] { return absolute(direct[13]); }))
    return false;
  // O222
  if (!operation(221, 1, direct[15],
                 [&] { return multiply(direct[12], point(.5)); }))
    return false;
  // O223
  if (!operation(222, 1, direct[14],
                 [&] { return multiply(direct[14], link[7]); }))
    return false;
  // O224
  if (!operation(223, 1, direct[15],
                 [&] { return subtract(direct[15], direct[14]); }))
    return false;
  // O225
  if (!operation(224, 1, direct[16],
                 [&] { return multiply(direct[0], link[8]); }))
    return false;
  // O226
  if (!operation(225, 1, direct[16],
                 [&] { return subtract(direct[16], chart[14]); }))
    return false;
  // G35
  if (!guard(
          34, 1,
          [&](EP4W& why) {
            if (!supported(why, direct[15]) || !supported(why, direct[16]))
              return false;
            if (direct[15].low < 0) return attach(direct[15]);
            return direct[16].low >= 0 || attach(direct[16]);
          },
          Slice4Why::verification_inconclusive))
    return false;
  if (!guard(
          35, 255,
          [&](EP4W&) {
            if (!intermediate_endpoint04_template_valid(request, false, 0))
              return false;
            for (auto& root : request.root)
              root.coordinates[1] = {{d.slice.y, +0.0, +0.0}, 1};
            return intermediate_endpoint04_template_valid(request, true,
                                                          d.slice.y);
          },
          Slice4Why::identity))
    return false;
  if (!guard(
          36, 255,
          [&](EP4W& why) {
            if (!boarding_route_foot_phase_environment()) {
              why = EP4W::unsupported_arithmetic;
              return false;
            }
            if (!ep4_packet_structure(request) || d.version != 4 ||
                d.candidate != BoardingIntermediateEndpoint04Candidate::
                                   root_y_both_ankle_reach_roll_slice ||
                !intermediate_endpoint04_template_valid(request, true,
                                                        d.slice.y))
              return false;
            for (const auto& root : request.root)
              if (std::bit_cast<std::uint64_t>(root.coordinates[1].terms[0]) !=
                  std::bit_cast<std::uint64_t>(d.slice.y))
                return false;
            if (!ep4_packet_domains(request)) {
              why = EP4W::unsupported_arithmetic;
              return false;
            }
            const auto valid = boarding_route_foot_phase_request_valid(request);
            if (!valid) {
              why = EP4W::unsupported_arithmetic;
              return false;
            }
            if (!boarding_route_foot_phase_environment()) {
              why = EP4W::unsupported_arithmetic;
              return false;
            }
            return true;
          },
          Slice4Why::identity))
    return false;
  d.slice.complete =
      d.slice.operation_written ==
          std::array<std::uint64_t, 4>{UINT64_MAX, UINT64_MAX, UINT64_MAX,
                                       0x00000003ffffffffULL} &&
      d.slice.guard_written == 0x0000001fffffffffULL;
  d.slice.side = 255;
  return d.slice.complete;
}
} // namespace apsis_drift

// Endpoint05: all preceding source bytes are the immutable registered prefix.
#include "origin_boarding_intermediate_endpoint05_internal.hpp"
namespace apsis_drift {
namespace {
using EP5D = BoardingIntermediateEndpoint05Diagnostic;
using EP5C = BoardingIntermediateEndpoint05Cell;
using EP5R = BoardingIntermediateEndpoint05Refusal;
using EP5L = detail::BoardingIntermediateEndpoint05Limits;
using EP5W = BoardingIntermediateEndpoint05Condition;
using EP5S = BoardingIntermediateEndpoint05State;
using EP5Stage = BoardingIntermediateEndpoint05SelfStage;
using EP5Cert = BoardingIntermediateEndpoint05Certificate;
auto ep5_source_bit(const EP5D& d, std::size_t i) -> bool {
  return (d.source_evaluated & (std::uint64_t{1} << i)) != 0;
}
auto ep5_valid(Interval x) -> bool {
  return x.supported && std::isfinite(x.low) && std::isfinite(x.high) &&
         x.low <= x.high;
}
auto ep5_scalar(const BoardingFootSiteScalarBounds& x) -> Interval {
  return x.supported ? interval(x.lower, x.upper) : failed();
}
auto ep5_axis(const EP5C& c, std::size_t axis) -> Point {
  return {ep5_scalar(c.unit_axes[axis][0]), ep5_scalar(c.unit_axes[axis][1]),
          ep5_scalar(c.unit_axes[axis][2])};
}
auto ep5_component(const BoardingPlantedLegPointBounds& b, std::size_t j)
    -> Interval {
  return interval(j == 0   ? b.lower.x
                  : j == 1 ? b.lower.y
                           : b.lower.z,
                  j == 0   ? b.upper.x
                  : j == 1 ? b.upper.y
                           : b.upper.z);
}
// Direct endpoint division encloses genuine D/L, never a rounded reciprocal.
auto ep5_unit_divide(Interval numerator, double length) -> Interval {
  if (!ep5_valid(numerator) || !std::isfinite(length) || length <= 0)
    return failed();
  if (numerator.low == 0 && numerator.high == 0) return point(0);
  const auto low = numerator.low / length, high = numerator.high / length;
  if (!std::isfinite(low) || !std::isfinite(high) ||
      low == -std::numeric_limits<double>::max() ||
      high == std::numeric_limits<double>::max())
    return failed();
  return interval(down(low), up(high));
}
[[gnu::noinline]] auto ep5_units(EP5D& d, EP5C& c, const EP5L& l, EP5R& r)
    -> bool {
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
      r = EP5R{};
      r.self_stage = EP5Stage::unit_axes;
      r.operation = static_cast<std::uint16_t>(row);
      c.unit_axis_cursor = static_cast<std::uint8_t>(row);
      if (!detail::intermediate_endpoint05_charge(
              d, r, d.work.unit_axis_operations, l.unit_axis_operations,
              EP5W::unit_axis_capacity))
        return false;
      c.unit_axis_attempted |= std::uint32_t{1} << row;
      const auto length = axis < 2 ? thigh_length : shin_length;
      if (!c.self_body_complete || !ss_leg(c.phase.legs[side]) ||
          !ep5_source_bit(d, 62) ||
          (axis < 2 ? length != .47285 : length != .47478))
        return detail::intermediate_endpoint05_refuse(d, r,
                                                      EP5W::unit_axis_identity);
      const auto result =
          operation < 3
              ? subtract(ep5_component(c.phase.points[body_index(knee)].value,
                                       component),
                         ep5_component(c.phase.points[body_index(from)].value,
                                       component))
              : ep5_unit_divide(displacement[component], length);
      if (!ep5_valid(result) ||
          (operation < 3 && (result.low < -16 || result.high > 16)))
        return detail::intermediate_endpoint05_refuse(
            d, r, EP5W::unsupported_arithmetic);
      if (operation < 3)
        displacement[component] = result;
      else
        c.unit_axes[axis][component] = {result.low, result.high, true};
      c.unit_axis_written |= std::uint32_t{1} << row;
    }
    c.unit_axis_complete_mask |=
        static_cast<std::uint8_t>(std::uint8_t{1} << axis);
  }
  return (c.unit_axis_written == kBoardingIntermediateEndpoint05UnitMask &&
          c.unit_axis_complete_mask == 15) ||
         detail::intermediate_endpoint05_refuse(d, r, EP5W::unit_axis_identity);
}
[[gnu::noinline]] auto ep5_owner(
    const EP5C& c,
    const std::array<Point, kBoardingPlantedBodyPointCount>& relative,
    const SelfRegion& region, BoardingLowerFootTransferOwner& out) -> void {
  if (region.junction != Junction::hip && region.junction != Junction::ankle) {
    ss_owner(c.phase, relative, region, out);
    return;
  }
  const auto side = static_cast<std::size_t>(region.second) >= 9
                        ? std::size_t{1}
                        : std::size_t{0};
  const auto u = ep5_axis(c, (region.junction == Junction::hip ? 0 : 2) + side);
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
        !ep5_valid(u[1]))
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
      ep5_valid(extent) && ep5_valid(limit) && ep5_valid(gap);
  if (!out.arithmetic_supported) return;
  out.extent = bounds(extent);
  out.limit = bounds(limit);
  out.secondary_extent = bounds(point(0));
  out.structural_identity = true;
  out.certificate = SelfCert::original_axis_box_support;
  out.certified = gap.low >= 0;
}
[[gnu::noinline]] auto ep5_hip(
    const EP5C& c, const SelfRegion& region, std::size_t region_index,
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
  const auto u = ep5_axis(c, side);
  for (std::size_t i = 0; i < 3; ++i) {
    if (!ep5_valid(u[i])) return;
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
[[gnu::noinline]] auto ep5_separate(const UnloadSolid& a, const UnloadSolid& b,
                                    EP5D& d, EP5C& c, const EP5L& l, EP5R& r,
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
  r.self_stage = EP5Stage::separation;
  for (std::size_t i = 0; i < count; ++i) {
    r.self_axis = static_cast<std::uint8_t>(i);
    r.self_sign = 255;
    if (!detail::intermediate_endpoint05_charge(
            d, r, d.work.self_axes, l.self_axes, EP5W::self_axis_capacity))
      return false;
    const auto axis = proposal(i);
    if (!self_direction(axis)) continue;
    for (std::size_t sign = 0; sign < 2; ++sign) {
      r.self_sign = static_cast<std::uint8_t>(sign);
      if (!detail::intermediate_endpoint05_charge(
              d, r, d.work.self_signed_trials, l.self_signed_trials,
              EP5W::self_signed_capacity))
        return false;
      const auto n = sign == 0 ? axis : self_negate(axis);
      const auto total =
          add(unload_support(a, n), unload_support(b, self_negate(n)));
      if (!total.supported || !std::isfinite(total.low) ||
          !std::isfinite(total.high))
        return detail::intermediate_endpoint05_refuse(
            d, r, EP5W::unsupported_arithmetic);
      if (total.high <= 0) {
        c.self_axes[pair] = static_cast<std::uint8_t>(i | (sign == 0 ? 0 : 16));
        return true;
      }
    }
  }
  return detail::intermediate_endpoint05_refuse(d, r,
                                                EP5W::unresolved_self_pair);
}
} // namespace
auto detail::intermediate_endpoint05_self_bridge(
    const BoardingIntermediateEndpoint05CurrentToken& token, EP5D& d, EP5C& c,
    const EP5L& l, EP5R& r) -> EP5S {
  std::size_t body_index_record{};
  const auto body = [&](auto predicate, EP5W why = EP5W::self_body_identity) {
    r = EP5R{};
    r.self_stage = EP5Stage::body;
    r.operation = static_cast<std::uint16_t>(body_index_record);
    if (!intermediate_endpoint05_charge(d, r, d.work.self_body_guards,
                                        l.self_body_guards,
                                        EP5W::self_body_capacity))
      return false;
    c.self_body_evaluated |= std::uint64_t{1} << body_index_record++;
    return predicate() || intermediate_endpoint05_refuse(d, r, why);
  };
  if (!body([&] {
        return token.context() && token.owner() == &d && token.cell() == &c &&
               token.request() && token.context()->owner() == &d &&
               token.context()->parts() == &d.parts &&
               token.context()->request() == token.request() &&
               token.context()->key() &&
               token.context()->key()->version() == 5 &&
               token.context()->key()->candidate() == d.candidate &&
               token.context()->key()->y() == d.slice.y && d.slice.complete &&
               d.work.phase_calls == 1 &&
               BoardingIntermediatePauseSupportAccess::valid(d.source) &&
               token.data() ==
                   BoardingIntermediatePauseSupportAccess::data(d.source) &&
               token.context()->data() == token.data() && token.data() &&
               d.cells.size() == 1 && d.cells.capacity() == 1 &&
               &d.cells.front() == &c && d.work.phase_calls == 1;
      }))
    return d.state;
  if (!body([&] { return boarding_route_foot_phase_environment(); },
            EP5W::unsupported_arithmetic))
    return d.state;
  const auto& p = c.phase;
  if (!body([&] {
        return p.complete && p.arithmetic_supported && p.nominal_links &&
               p.target_sole_identities && p.joint_sectors &&
               p.derivative_domains && p.timing_complete;
      }))
    return d.state;
  if (!body([&] {
        return d.candidate == BoardingIntermediateEndpoint05Candidate::
                                  root_y_both_hip_ankle_reach_roll_slice &&
               d.version == 5 && p.first == 0 && p.last == 1 &&
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
        return ep5_source_bit(d, 62) && p.nominal_links &&
               p.target_sole_identities;
      }))
    return d.state;
  if (!body([&] {
        return ep5_source_bit(d, 63) && p.arithmetic_supported &&
               c.projected_carrier_complete[0];
      }))
    return d.state;
  if (!body([&] {
        return c.nominal_support_complete && c.nominal_equilibrium &&
               c.star_support && d.stop_condition == EP5W::none;
      }))
    return d.state;
  for (std::size_t i = 0; i < 18; ++i)
    if (!body(
            [&] {
              return ss_point_valid(body_intervals(p.points[i].value), 8);
            },
            EP5W::unsupported_arithmetic))
      return d.state;
  for (std::size_t f = 0; f < 4; ++f)
    for (std::size_t j = 0; j < 3; ++j)
      if (!body(
              [&] {
                return ss_point_valid(
                    body_intervals(p.frames[f].columns[j].value), 8);
              },
              EP5W::unsupported_arithmetic))
        return d.state;
  for (std::size_t side = 0; side < 2; ++side)
    if (!body([&] {
          const auto first = side == 0 ? std::size_t{36} : std::size_t{42};
          return ss_leg(p.legs[side]) && ep5_source_bit(d, 62) &&
                 ep5_source_bit(d, first) && ep5_source_bit(d, first + 1) &&
                 ss_same_part(d.parts[first - 33], ss_part(first - 33)) &&
                 ss_same_part(d.parts[first - 32], ss_part(first - 32));
        }))
      return d.state;
  if (!body([&] {
        return ss_same_part(d.parts[0], ss_part(0)) &&
               ss_same_part(d.parts[1], ss_part(1)) && ep5_source_bit(d, 33) &&
               ep5_source_bit(d, 34) && ep5_source_bit(d, 48);
      }))
    return d.state;
  if (!body([&] {
        return ss_same_part(d.parts[1], ss_part(1)) &&
               ss_same_part(d.parts[2], ss_part(2)) && ep5_source_bit(d, 34) &&
               ep5_source_bit(d, 35) && ep5_source_bit(d, 51) &&
               ep5_source_bit(d, 63);
      }))
    return d.state;
  if (!body([&] {
        if (!ep5_source_bit(d, 63)) return false;
        for (const auto part :
             {std::size_t{6}, std::size_t{7}, std::size_t{8}, std::size_t{12},
              std::size_t{13}, std::size_t{14}})
          if (!ep5_source_bit(d, 33 + part) ||
              !ss_same_part(d.parts[part], ss_part(part)))
            return false;
        for (const auto region :
             {std::size_t{4}, std::size_t{5}, std::size_t{8}, std::size_t{9},
              std::size_t{12}, std::size_t{13}})
          if (!ep5_source_bit(d, 48 + region)) return false;
        return true;
      }))
    return d.state;
  SelfRegions regions{};
  if (!body([&] {
        regions = self_regions();
        for (std::size_t i = 0; i < 14; ++i)
          if (!ep5_source_bit(d, 48 + i) ||
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
            EP5W::unsupported_arithmetic))
      return d.state;
  if (!body([&] {
        return body_index_record == 63 &&
               c.self_body_evaluated ==
                   kBoardingIntermediateEndpoint05BodyMask &&
               token.owner() == &d && token.cell() == &c &&
               token.data() ==
                   BoardingIntermediatePauseSupportAccess::data(d.source);
      }))
    return d.state;
  c.self_body_complete = true;
  if (!ep5_units(d, c, l, r)) return d.state;
  std::size_t pair{};
  for (std::size_t a = 0; a < 15; ++a)
    for (std::size_t b = a + 1; b < 15; ++b, ++pair) {
      r = EP5R{};
      r.self_stage = EP5Stage::not_run;
      r.self_pair = static_cast<std::uint16_t>(pair);
      r.self_region = 255;
      r.self_axis = 255;
      r.self_sign = 255;
      r.self_stage = EP5Stage::pair;
      if (!intermediate_endpoint05_charge(d, r, d.work.self_pairs, l.self_pairs,
                                          EP5W::self_pair_capacity))
        return d.state;
      ++c.examined_pairs;
      auto certificate = EP5Cert::not_run;
      for (std::size_t region = 0; region < 14; ++region)
        if (static_cast<std::size_t>(regions[region].first) == a &&
            static_cast<std::size_t>(regions[region].second) == b) {
          r.self_stage = EP5Stage::owner;
          r.self_region = static_cast<std::uint8_t>(region);
          if (!intermediate_endpoint05_charge(d, r, d.work.self_owners,
                                              l.self_owners,
                                              EP5W::self_owner_capacity))
            return d.state;
          c.owner_attempted_mask |=
              static_cast<std::uint16_t>(std::uint16_t{1} << region);
          ep5_owner(c, relative, regions[region], c.owners[region]);
          if (!c.owners[region].arithmetic_supported) {
            intermediate_endpoint05_refuse(d, r, EP5W::unsupported_arithmetic);
            return d.state;
          }
          if (c.owners[region].certified)
            certificate = static_cast<EP5Cert>(c.owners[region].certificate);
          else if (regions[region].junction == Junction::hip) {
            r.self_stage = EP5Stage::hip;
            if (!intermediate_endpoint05_charge(
                    d, r, d.work.self_hip_complements, l.self_hip_complements,
                    EP5W::self_hip_capacity))
              return d.state;
            const auto side = b >= 9 ? std::size_t{1} : std::size_t{0};
            ep5_hip(c, regions[region], region, pair, c.hip_complements[side]);
            if (!c.hip_complements[side].arithmetic_supported) {
              intermediate_endpoint05_refuse(d, r,
                                             EP5W::unsupported_arithmetic);
              return d.state;
            }
            if (c.hip_complements[side].certified)
              certificate = EP5Cert::original_capsule_slab_complement;
          }
          break;
        }
      if (certificate == EP5Cert::not_run) {
        const auto first = unload_solid(d.parts[a], relative, p),
                   second = unload_solid(d.parts[b], relative, p);
        if (!ep5_separate(first, second, d, c, l, r, pair)) return d.state;
        certificate = EP5Cert::convex_support_plane;
      }
      c.self_certificates[pair] = certificate;
      ++c.self_certificate_counts[static_cast<std::size_t>(certificate)];
      ++c.accepted_pairs;
    }
  c.self_complete = pair == 105 && c.accepted_pairs == 105;
  if (!c.self_complete) {
    intermediate_endpoint05_refuse(d, r, EP5W::incomplete_endpoint);
    return d.state;
  }
  r.self_pair = 65535;
  r.self_region = 255;
  r.self_axis = 255;
  r.self_sign = 255;
  r.self_stage = EP5Stage::complete;
  c.complete = true;
  c.state = d.state = EP5S::accepted;
  return d.state;
}
} // namespace apsis_drift

namespace apsis_drift {
namespace {
using Slice5Why = BoardingIntermediateEndpoint05SliceCondition;
using EP5Request = BoardingRouteFootPhaseRequest;
auto ep5_packet(const BoardingRoutePhaseConstant& c,
                const std::array<double, 3>& terms, std::size_t count) -> bool {
  return c.count == count && c.terms == terms;
}
// Comparison-only structure; no numeric sum or construction evaluation.
// Called only within the already charged G04/G50 deferred predicates.
auto ep5_packet_structure(const EP5Request& request) -> bool {
  const auto canonical = [](const BoardingRoutePhaseConstant& c) {
    if (c.count == 0 || c.count > c.terms.size()) return false;
    for (std::size_t i = c.count; i < c.terms.size(); ++i)
      if (c.terms[i] != 0 || std::signbit(c.terms[i])) return false;
    return true;
  };
  for (const auto& root : request.root)
    for (const auto& c : root.coordinates)
      if (!canonical(c)) return false;
  for (const auto& foot : request.feet)
    for (const auto& sole : foot.sole)
      for (const auto& c : sole.coordinates)
        if (!canonical(c)) return false;
  return true;
}
auto ep5_packet_domains(const EP5Request& request) -> bool {
  const auto valid = [](const BoardingRoutePhaseConstant& c) {
    if (c.count == 0 || c.count > 3) return false;
    for (std::size_t i = 0; i < 3; ++i)
      if (!std::isfinite(c.terms[i]) || std::abs(c.terms[i]) > 8 ||
          (i >= c.count && (c.terms[i] != 0 || std::signbit(c.terms[i]))))
        return false;
    return true;
  };
  for (const auto& root : request.root)
    for (const auto& c : root.coordinates)
      if (!valid(c)) return false;
  for (const auto& foot : request.feet)
    for (const auto& sole : foot.sole)
      for (const auto& c : sole.coordinates)
        if (!valid(c)) return false;
  return std::isfinite(request.seconds_per_parameter) &&
         std::isfinite(request.root_yaw_half[0]) &&
         std::isfinite(request.root_yaw_half[1]) &&
         std::isfinite(request.torso_lean_half[0]) &&
         std::isfinite(request.torso_lean_half[1]) &&
         std::isfinite(request.port_reaction_fraction[0]) &&
         std::isfinite(request.port_reaction_fraction[1]);
}
auto ep5_construction_packet_domains(const EP5Request& request) -> bool {
  for (std::size_t i = 0; i < 8; ++i) {
    const auto& c =
        i < 2 ? request.root[0].coordinates[i == 0 ? 0 : 2]
              : request.feet[i < 5 ? 0 : 1].sole[0].coordinates[(i - 2) % 3];
    if (c.count == 0 || c.count > 3) return false;
    for (std::size_t j = 0; j < 3; ++j)
      if (!std::isfinite(c.terms[j]) || std::abs(c.terms[j]) > 8 ||
          (j >= c.count && (c.terms[j] != 0 || std::signbit(c.terms[j]))))
        return false;
  }
  return true;
}
auto ep5_slice_stop(EP5D& d, EP5R& r, EP5W why, Slice5Why condition) -> bool {
  d.slice.condition = condition;
  r.limiting_bound = d.slice.limiting_bound;
  return detail::intermediate_endpoint05_refuse(d, r, why);
}
} // namespace
// This is a literal packet predicate, not a source issuer or new factory.
auto detail::intermediate_endpoint05_template_valid(const EP5Request& request,
                                                    bool generated, double y)
    -> bool {
  for (const auto& root : request.root) {
    if (!ep5_packet(root.coordinates[0], {.16, .16, -.0075}, 3) ||
        !ep5_packet(root.coordinates[2], {-.55, -.17, .19}, 3) ||
        !(generated ? ep5_packet(root.coordinates[1], {y, 0, 0}, 1)
                    : ep5_packet(root.coordinates[1],
                                 {.847, .012, (-.359) / 2.0}, 3)))
      return false;
  }
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& foot = request.feet[side];
    for (const auto& sole : foot.sole) {
      if (side == 0) {
        if (!ep5_packet(sole.coordinates[0], {.16, -.14, .16}, 3) ||
            !ep5_packet(sole.coordinates[1],
                        {static_cast<double>(-230000) * 1e-6, 0, 0}, 1) ||
            !ep5_packet(sole.coordinates[2], {-1.14, 0, 0}, 1))
          return false;
      } else {
        if (!ep5_packet(sole.coordinates[0], {.16, .14, 0}, 2) ||
            !ep5_packet(sole.coordinates[1],
                        {static_cast<double>(-160000) * 1e-6, 0, 0}, 1) ||
            !ep5_packet(sole.coordinates[2], {-.8, 0, 0}, 1))
          return false;
      }
    }
    if (foot.yaw_half != std::array<double, 2>{0, 0} ||
        foot.swing_height_metres != 0)
      return false;
  }
  return request.root_yaw_half == std::array<double, 2>{.125, .125} &&
         request.torso_lean_half == std::array<double, 2>{-.30, -.30} &&
         request.port_reaction_fraction ==
             std::array<double, 2>{.0625, .0625} &&
         request.seconds_per_parameter == 2;
}
namespace {
// Direct local aggregate: all registered fields retain their complete lifetime.
struct Endpoint05ConstructorWorkspace {
  // All construction storage dies before Key issuance and the original graph.
  std::array<Interval, 8> input{};
  std::array<Interval, 6> yaw{};
  // Per side: signed X/Z, X2/Z2, C, A, full ABS(X), rollLower.
  std::array<Interval, 16> chart{};
  // Lsum, M, Ldiff, m, L1sq, L2sq, Delta, sqrt3half, b.
  std::array<Interval, 9> link{};
  // Per side: W, cutlow, cuthi, cutturn, p, q; no overwritten origins.
  std::array<Interval, 12> cut{};
  // Per side: rho_p/rho_q, ankle lower/upper, reach lower/upper;
  // the last six slots are sequential endpoint/current primitive scratch.
  std::array<Interval, 18> endpoint{};
  std::array<Interval, 18> direct{};
  std::array<double, 10> cuts{};
  std::array<double, 8> selected{};
  std::array<bool, 2> endpoint_certificate{};
};
// Borrowed numerical controls only; this object issues no program authority.
struct Endpoint05ConstructorControl {
  EP5D& d;
  EP5R& r;
  const EP5L& limits;
  template <class Predicate>
  auto guard(std::uint8_t row, std::uint8_t side, Predicate&& predicate,
             Slice5Why ordinary) -> bool {
    r = EP5R{};
    r.self_stage = EP5Stage::slice_guard;
    r.operation = row;
    if (side < 2) r.side = side;
    d.slice.guard = row;
    d.slice.side = side;
    d.slice.limiting_bound = {};
    if (!detail::intermediate_endpoint05_charge(
            d, r, d.work.construction_guards, limits.construction_guards,
            EP5W::slice_guard_capacity)) {
      d.slice.condition = Slice5Why::guard_capacity;
      return false;
    }
    d.slice.guard_attempted |= std::uint64_t{1} << row;
    EP5W why = ordinary == Slice5Why::identity ? EP5W::slice_identity
                                               : EP5W::slice_unavailable;
    const bool good = predicate(why);
    if (!good)
      return ep5_slice_stop(
          d, r, why,
          why == EP5W::unsupported_arithmetic
              ? Slice5Why::unsupported_arithmetic
              : ((row == 36 || row == 43) &&
                         d.slice.condition == Slice5Why::hip_descent_unavailable
                     ? Slice5Why::hip_descent_unavailable
                     : ordinary));
    d.slice.guard_written |= std::uint64_t{1} << row;
    return true;
  }
  template <class Compute>
  auto operation(std::uint16_t row, std::uint8_t side, Interval& output,
                 Compute&& compute) -> bool {
    r = EP5R{};
    r.self_stage = EP5Stage::slice_operation;
    r.operation = row;
    if (side < 2) r.side = side;
    d.slice.operation = row;
    d.slice.side = side;
    d.slice.limiting_bound = {};
    if (!detail::intermediate_endpoint05_charge(
            d, r, d.work.construction_operations,
            limits.construction_operations, EP5W::slice_operation_capacity)) {
      d.slice.condition = Slice5Why::operation_capacity;
      return false;
    }
    d.slice.operation_attempted[row / 64] |= std::uint64_t{1} << (row % 64);
    const auto value = compute();
    if (!ep5_valid(value))
      return ep5_slice_stop(d, r, EP5W::unsupported_arithmetic,
                            Slice5Why::unsupported_arithmetic);
    output = value;
    d.slice.limiting_bound = {value.low, value.high, true};
    d.slice.operation_written[row / 64] |= std::uint64_t{1} << (row % 64);
    d.slice.arithmetic_supported = true;
    return true;
  }
  auto supported(EP5W& why, const Interval& v) -> bool {
    if (ep5_valid(v)) return true;
    why = EP5W::unsupported_arithmetic;
    return false;
  }
  auto attach(const Interval& v) -> bool {
    d.slice.limiting_bound = {v.low, v.high, true};
    return false;
  }
};
} // namespace
[[gnu::noinline]] auto detail::intermediate_endpoint05_construct(
    EP5D& d, EP5Request& request, const EP5L& limits, EP5R& r) -> bool {
  // Directly initialized; no factory, copy, aggregate return, heap or arena.
  Endpoint05ConstructorWorkspace workspace{};
  Endpoint05ConstructorControl control{d, r, limits};
  // G01
  if (!control.guard(
          0, 255,
          [&](EP5W&) {
            return d.source_enrolled && d.work.source_guards == 64 &&
                   d.source_evaluated == UINT64_MAX &&
                   BoardingIntermediatePauseSupportAccess::valid(d.source) &&
                   BoardingIntermediatePauseSupportAccess::data(d.source) &&
                   d.parts.size() == 15;
          },
          Slice5Why::identity))
    return false;
  // G02
  if (!control.guard(
          1, 255,
          [&](EP5W& why) {
            why = EP5W::unsupported_arithmetic;
            return boarding_route_foot_phase_environment();
          },
          Slice5Why::unsupported_arithmetic))
    return false;
  // G03
  if (!control.guard(
          2, 255,
          [&](EP5W&) {
            return d.version == 5 &&
                   d.candidate == BoardingIntermediateEndpoint05Candidate::
                                      root_y_both_hip_ankle_reach_roll_slice &&
                   intermediate_endpoint05_template_valid(request, false, 0);
          },
          Slice5Why::identity))
    return false;
  // G04
  if (!control.guard(
          3, 255,
          [&](EP5W& why) {
            if (!ep5_packet_structure(request) ||
                !intermediate_endpoint05_template_valid(request, false, 0))
              return false;
            if (!ep5_packet_domains(request) ||
                !ep5_construction_packet_domains(request)) {
              why = EP5W::unsupported_arithmetic;
              return false;
            }
            return true;
          },
          Slice5Why::identity))
    return false;
  // G05
  if (!control.guard(
          4, 255,
          [&](EP5W& why) {
            if (!std::isfinite(thigh_length) || !std::isfinite(shin_length) ||
                thigh_length <= 0 || shin_length <= 0 || thigh_length > 1 ||
                shin_length > 1) {
              why = EP5W::unsupported_arithmetic;
              return false;
            }
            const auto* pelvis = std::get_if<BoardingRoutePhaseBoxBinding>(
                &d.parts[0].reservation);
            const auto* port = std::get_if<BoardingPlantedBodyCapsuleBinding>(
                &d.parts[3].reservation);
            const auto* star = std::get_if<BoardingPlantedBodyCapsuleBinding>(
                &d.parts[9].reservation);
            const auto port_region = ss_region(1), star_region = ss_region(2);
            if (kBoardingRouteFootPhaseVersion != 1 || thigh_length != .47285 ||
                shin_length != .47478 || !ep5_source_bit(d, 62) || !pelvis ||
                !port || !star || d.parts[0].id != PartId::pelvis ||
                d.parts[3].id != PartId::port_thigh ||
                d.parts[9].id != PartId::starboard_thigh ||
                pelvis->center != BodyPointId::root ||
                pelvis->frame != BoardingRoutePhaseFrame::root ||
                pelvis->half_size_metres != RigidVector3{.24, .12, .18} ||
                port->start != BodyPointId::port_hip ||
                port->end != BodyPointId::port_knee ||
                star->start != BodyPointId::starboard_hip ||
                star->end != BodyPointId::starboard_knee ||
                port->radius_metres != .105 || star->radius_metres != .105 ||
                port_region.first != PartId::pelvis ||
                port_region.second != PartId::port_thigh ||
                star_region.first != PartId::pelvis ||
                star_region.second != PartId::starboard_thigh ||
                port_region.junction != Junction::hip ||
                star_region.junction != Junction::hip ||
                port_region.root != BodyPointId::port_hip ||
                port_region.toward != BodyPointId::port_knee ||
                star_region.root != BodyPointId::starboard_hip ||
                star_region.toward != BodyPointId::starboard_knee ||
                port_region.limit_metres != kBoardingSelfHipLengthMetres ||
                star_region.limit_metres != kBoardingSelfHipLengthMetres ||
                kBoardingSelfHipLengthMetres != 0x1.bb0cd605d7512p-3) {
              why = EP5W::slice_identity;
              return false;
            }
            if (!std::isfinite(kBoardingSelfHipLengthMetres)) {
              why = EP5W::unsupported_arithmetic;
              return false;
            }
            return .12 > 0 && kBoardingSelfHipLengthMetres > .12 && .105 > 0 &&
                   .105 <= kBoardingSelfHipLengthMetres &&
                   kBoardingSelfHipLengthMetres < thigh_length;
          },
          Slice5Why::hip_threshold_domain))
    return false;
  // G06
  if (!control.guard(
          5, 255,
          [&](EP5W&) {
            // The source compiler's exact-real nominal rational workspace.yaw
            // construction, not midpoint or rounded affine orthogonality, owns
            // these identities.
            return ep5_source_bit(d, 63) &&
                   kBoardingRouteFootPhaseVersion == 1 &&
                   intermediate_endpoint05_template_valid(request, false, 0) &&
                   request.feet[0].yaw_half == std::array<double, 2>{0, 0} &&
                   request.feet[1].yaw_half == std::array<double, 2>{0, 0};
          },
          Slice5Why::identity))
    return false;
  // O001
  if (!control.operation(0, 255, workspace.input[0], [&] {
        return phase_constant(request.root[0].coordinates[0]);
      }))
    return false;
  // O002
  if (!control.operation(1, 255, workspace.input[1], [&] {
        return phase_constant(request.root[0].coordinates[2]);
      }))
    return false;
  // O003
  if (!control.operation(2, 255, workspace.input[2], [&] {
        return phase_constant(request.feet[0].sole[0].coordinates[0]);
      }))
    return false;
  // O004
  if (!control.operation(3, 255, workspace.input[3], [&] {
        return phase_constant(request.feet[0].sole[0].coordinates[1]);
      }))
    return false;
  // O005
  if (!control.operation(4, 255, workspace.input[4], [&] {
        return phase_constant(request.feet[0].sole[0].coordinates[2]);
      }))
    return false;
  // O006
  if (!control.operation(5, 255, workspace.input[5], [&] {
        return phase_constant(request.feet[1].sole[0].coordinates[0]);
      }))
    return false;
  // O007
  if (!control.operation(6, 255, workspace.input[6], [&] {
        return phase_constant(request.feet[1].sole[0].coordinates[1]);
      }))
    return false;
  // O008
  if (!control.operation(7, 255, workspace.input[7], [&] {
        return phase_constant(request.feet[1].sole[0].coordinates[2]);
      }))
    return false;
  // O009
  if (!control.operation(8, 255, workspace.yaw[0], [&] {
        return square(point(request.root_yaw_half[0]));
      }))
    return false;
  // O010
  if (!control.operation(9, 255, workspace.yaw[1],
                         [&] { return add(point(1), workspace.yaw[0]); }))
    return false;
  // O011
  if (!control.operation(10, 255, workspace.yaw[2],
                         [&] { return subtract(point(1), workspace.yaw[0]); }))
    return false;
  // O012
  if (!control.operation(11, 255, workspace.yaw[3], [&] {
        return divide(workspace.yaw[2], workspace.yaw[1]);
      }))
    return false;
  // O013
  if (!control.operation(12, 255, workspace.yaw[2], [&] {
        return multiply(point(2), point(request.root_yaw_half[0]));
      }))
    return false;
  // O014
  if (!control.operation(13, 255, workspace.yaw[4], [&] {
        return divide(workspace.yaw[2], workspace.yaw[1]);
      }))
    return false;
  // O015
  if (!control.operation(14, 255, workspace.yaw[5],
                         [&] { return negate(workspace.yaw[4]); }))
    return false;
  // O016
  if (!control.operation(15, 0, workspace.endpoint[12], [&] {
        return multiply(point(-.14), workspace.yaw[3]);
      }))
    return false;
  // O017
  if (!control.operation(16, 0, workspace.endpoint[13], [&] {
        return multiply(point(-.14), workspace.yaw[5]);
      }))
    return false;
  // O018
  if (!control.operation(17, 0, workspace.endpoint[14], [&] {
        return add(workspace.input[0], workspace.endpoint[12]);
      }))
    return false;
  // O019
  if (!control.operation(18, 0, workspace.endpoint[15], [&] {
        return add(workspace.input[1], workspace.endpoint[13]);
      }))
    return false;
  // O020
  if (!control.operation(19, 0, workspace.chart[0], [&] {
        return subtract(workspace.input[2], workspace.endpoint[14]);
      }))
    return false;
  // O021
  if (!control.operation(20, 0, workspace.chart[1], [&] {
        return subtract(workspace.input[4], workspace.endpoint[15]);
      }))
    return false;
  // O022
  if (!control.operation(21, 0, workspace.chart[2],
                         [&] { return square(workspace.chart[0]); }))
    return false;
  // O023
  if (!control.operation(22, 0, workspace.chart[3],
                         [&] { return square(workspace.chart[1]); }))
    return false;
  // O024
  if (!control.operation(23, 0, workspace.chart[4], [&] {
        return add(workspace.chart[2], workspace.chart[3]);
      }))
    return false;
  // O025
  if (!control.operation(24, 0, workspace.chart[5],
                         [&] { return add(workspace.input[3], point(.1)); }))
    return false;
  // O026
  if (!control.operation(25, 1, workspace.endpoint[12], [&] {
        return multiply(point(.14), workspace.yaw[3]);
      }))
    return false;
  // O027
  if (!control.operation(26, 1, workspace.endpoint[13], [&] {
        return multiply(point(.14), workspace.yaw[5]);
      }))
    return false;
  // O028
  if (!control.operation(27, 1, workspace.endpoint[14], [&] {
        return add(workspace.input[0], workspace.endpoint[12]);
      }))
    return false;
  // O029
  if (!control.operation(28, 1, workspace.endpoint[15], [&] {
        return add(workspace.input[1], workspace.endpoint[13]);
      }))
    return false;
  // O030
  if (!control.operation(29, 1, workspace.chart[8], [&] {
        return subtract(workspace.input[5], workspace.endpoint[14]);
      }))
    return false;
  // O031
  if (!control.operation(30, 1, workspace.chart[9], [&] {
        return subtract(workspace.input[7], workspace.endpoint[15]);
      }))
    return false;
  // O032
  if (!control.operation(31, 1, workspace.chart[10],
                         [&] { return square(workspace.chart[8]); }))
    return false;
  // O033
  if (!control.operation(32, 1, workspace.chart[11],
                         [&] { return square(workspace.chart[9]); }))
    return false;
  // O034
  if (!control.operation(33, 1, workspace.chart[12], [&] {
        return add(workspace.chart[10], workspace.chart[11]);
      }))
    return false;
  // O035
  if (!control.operation(34, 1, workspace.chart[13],
                         [&] { return add(workspace.input[6], point(.1)); }))
    return false;
  // O036
  if (!control.operation(35, 255, workspace.link[0], [&] {
        return add(point(thigh_length), point(shin_length));
      }))
    return false;
  // O037
  if (!control.operation(36, 255, workspace.link[1],
                         [&] { return square(workspace.link[0]); }))
    return false;
  // O038
  if (!control.operation(37, 255, workspace.link[2], [&] {
        return subtract(point(thigh_length), point(shin_length));
      }))
    return false;
  // O039
  if (!control.operation(38, 255, workspace.link[3],
                         [&] { return square(workspace.link[2]); }))
    return false;
  // O040
  if (!control.operation(39, 255, workspace.link[4],
                         [&] { return square(point(thigh_length)); }))
    return false;
  // O041
  if (!control.operation(40, 255, workspace.link[5],
                         [&] { return square(point(shin_length)); }))
    return false;
  // O042
  if (!control.operation(41, 255, workspace.link[6], [&] {
        return subtract(workspace.link[4], workspace.link[5]);
      }))
    return false;
  // O043
  if (!control.operation(42, 255, workspace.yaw[0],
                         [&] { return transfer_small_root(point(3)); }))
    return false;
  // O044
  if (!control.operation(43, 255, workspace.link[7],
                         [&] { return divide(workspace.yaw[0], point(2)); }))
    return false;
  // O045
  if (!control.operation(44, 255, workspace.link[8],
                         [&] { return subtract(point(2), workspace.yaw[0]); }))
    return false;
  // G07
  if (!control.guard(
          6, 255,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.link[8])) return false;
            return workspace.link[8].low > 0 ||
                   control.attach(workspace.link[8]);
          },
          Slice5Why::no_positive_roll_factor))
    return false;
  // O046
  if (!control.operation(45, 255, workspace.direct[0],
                         [&] { return square(workspace.link[8]); }))
    return false;
  // O047
  if (!control.operation(46, 255, workspace.direct[0],
                         [&] { return add(point(1), workspace.direct[0]); }))
    return false;
  // O048
  if (!control.operation(47, 255, workspace.direct[0], [&] {
        return transfer_small_root(workspace.direct[0]);
      }))
    return false;
  // O049
  if (!control.operation(48, 255, workspace.direct[1],
                         [&] { return divide(point(1), workspace.direct[0]); }))
    return false;
  // G08
  if (!control.guard(
          7, 255,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.direct[1])) return false;
            if (workspace.direct[1].low <= 0 || workspace.direct[1].high >= 1)
              return control.attach(workspace.direct[1]);
            return true;
          },
          Slice5Why::hip_threshold_domain))
    return false;
  // O050
  if (!control.operation(49, 255, workspace.direct[2], [&] {
        return square(point(kBoardingSelfHipLengthMetres));
      }))
    return false;
  // O051
  if (!control.operation(50, 255, workspace.direct[3],
                         [&] { return square(point(.105)); }))
    return false;
  // O052
  if (!control.operation(51, 255, workspace.direct[4],
                         [&] { return square(point(.12)); }))
    return false;
  // O053
  if (!control.operation(52, 255, workspace.direct[2], [&] {
        return add(workspace.direct[2], workspace.direct[3]);
      }))
    return false;
  // O054
  if (!control.operation(53, 255, workspace.direct[3], [&] {
        return subtract(workspace.direct[2], workspace.direct[4]);
      }))
    return false;
  // G09
  if (!control.guard(
          8, 255,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.direct[2]) ||
                !control.supported(why, workspace.direct[3]))
              return false;
            if (workspace.direct[2].low <= 0)
              return control.attach(workspace.direct[2]);
            return workspace.direct[3].low > 0 ||
                   control.attach(workspace.direct[3]);
          },
          Slice5Why::hip_threshold_domain))
    return false;
  // O055
  if (!control.operation(54, 255, workspace.direct[3], [&] {
        return transfer_small_root(workspace.direct[3]);
      }))
    return false;
  // O056
  if (!control.operation(55, 255, workspace.direct[3], [&] {
        return multiply(point(.105), workspace.direct[3]);
      }))
    return false;
  // O057
  if (!control.operation(56, 255, workspace.direct[4], [&] {
        return multiply(point(kBoardingSelfHipLengthMetres), point(.12));
      }))
    return false;
  // O058
  if (!control.operation(57, 255, workspace.direct[3], [&] {
        return add(workspace.direct[4], workspace.direct[3]);
      }))
    return false;
  // O059
  if (!control.operation(58, 255, workspace.direct[3], [&] {
        return divide(workspace.direct[3], workspace.direct[2]);
      }))
    return false;
  // G10
  if (!control.guard(
          9, 255,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.direct[3])) return false;
            if (workspace.direct[3].low <= 0 || workspace.direct[3].high >= 1)
              return control.attach(workspace.direct[3]);
            return true;
          },
          Slice5Why::hip_threshold_domain))
    return false;
  // O060
  if (!control.operation(59, 255, workspace.direct[4], [&] {
        return multiply(point(kBoardingSelfHipLengthMetres),
                        workspace.direct[3]);
      }))
    return false;
  // O061
  if (!control.operation(60, 255, workspace.direct[4], [&] {
        return subtract(workspace.direct[4], point(.12));
      }))
    return false;
  // G11
  if (!control.guard(
          10, 255,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.direct[4])) return false;
            return workspace.direct[4].low > 0 ||
                   control.attach(workspace.direct[4]);
          },
          Slice5Why::hip_threshold_domain))
    return false;
  // O062
  if (!control.operation(61, 255, workspace.direct[3], [&] {
        return divide(workspace.direct[3], workspace.direct[1]);
      }))
    return false;
  // O063
  if (!control.operation(62, 255, workspace.direct[3],
                         [&] { return point(workspace.direct[3].high); }))
    return false;
  workspace.selected[5] = workspace.direct[3].low;
  // G12
  if (!control.guard(
          11, 255,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.direct[3])) return false;
            if (workspace.direct[3].low != workspace.direct[3].high) {
              why = EP5W::unsupported_arithmetic;
              return false;
            }
            if (workspace.direct[3].low <= 0 || workspace.direct[3].high >= 1)
              return control.attach(workspace.direct[3]);
            return true;
          },
          Slice5Why::hip_descent_unavailable))
    return false;
  // O064
  if (!control.operation(63, 255, workspace.direct[3],
                         [&] { return square(point(workspace.selected[5])); }))
    return false;
  // O065
  if (!control.operation(64, 255, workspace.direct[3], [&] {
        return subtract(point(1), workspace.direct[3]);
      }))
    return false;
  // G13
  if (!control.guard(
          12, 255,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.direct[3])) return false;
            return workspace.direct[3].low > 0 ||
                   control.attach(workspace.direct[3]);
          },
          Slice5Why::hip_descent_unavailable))
    return false;
  // O066
  if (!control.operation(65, 255, workspace.direct[3], [&] {
        return transfer_small_root(workspace.direct[3]);
      }))
    return false;
  // O067
  if (!control.operation(66, 255, workspace.direct[3], [&] {
        return multiply(point(thigh_length), workspace.direct[3]);
      }))
    return false;
  // O068
  if (!control.operation(67, 255, workspace.direct[4],
                         [&] { return point(workspace.direct[3].low); }))
    return false;
  workspace.selected[5] = workspace.direct[4].low;
  // G14
  if (!control.guard(
          13, 255,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.direct[3]) ||
                !control.supported(why, workspace.direct[4]))
              return false;
            if (workspace.direct[4].low != workspace.direct[4].high ||
                workspace.direct[4].low != workspace.direct[3].low ||
                workspace.selected[5] != workspace.direct[4].low) {
              why = EP5W::unsupported_arithmetic;
              return false;
            }
            return workspace.direct[4].low > 0 ||
                   control.attach(workspace.direct[4]);
          },
          Slice5Why::hip_descent_unavailable))
    return false;
  // O069
  if (!control.operation(68, 0, workspace.cut[0],
                         [&] { return negate(workspace.chart[1]); }))
    return false;
  // O070
  if (!control.operation(69, 0, workspace.endpoint[12], [&] {
        return subtract(workspace.cut[0], point(thigh_length));
      }))
    return false;
  // O071
  if (!control.operation(70, 0, workspace.cut[1], [&] {
        return divide(workspace.endpoint[12], point(shin_length));
      }))
    return false;
  // O072
  if (!control.operation(71, 0, workspace.endpoint[12], [&] {
        return add(workspace.cut[0], point(thigh_length));
      }))
    return false;
  // O073
  if (!control.operation(72, 0, workspace.cut[2], [&] {
        return divide(workspace.endpoint[12], point(shin_length));
      }))
    return false;
  // O074
  if (!control.operation(73, 0, workspace.cut[3], [&] {
        return divide(workspace.cut[0], workspace.link[0]);
      }))
    return false;
  // O075
  if (!control.operation(74, 0, workspace.cut[4], [&] {
        return point(std::max(-.5, workspace.cut[1].high));
      }))
    return false;
  // O076
  if (!control.operation(75, 0, workspace.cut[5], [&] {
        return point(std::min(.5, workspace.cut[2].low));
      }))
    return false;
  // O077
  if (!control.operation(76, 0, workspace.cut[5], [&] {
        return point(std::min(workspace.cut[5].low, workspace.cut[3].low));
      }))
    return false;
  // O078
  if (!control.operation(77, 0, workspace.endpoint[12], [&] {
        return subtract(workspace.cut[0], point(workspace.selected[5]));
      }))
    return false;
  // O079
  if (!control.operation(78, 0, workspace.endpoint[12], [&] {
        return divide(workspace.endpoint[12], point(shin_length));
      }))
    return false;
  // O080
  if (!control.operation(79, 0, workspace.cut[4], [&] {
        return point(
            std::max(workspace.cut[4].low, workspace.endpoint[12].high));
      }))
    return false;
  // O081
  if (!control.operation(80, 0, workspace.endpoint[13], [&] {
        return divide(workspace.cut[0], point(shin_length));
      }))
    return false;
  // O082
  if (!control.operation(81, 0, workspace.cut[5], [&] {
        return point(
            std::min(workspace.cut[5].low, workspace.endpoint[13].low));
      }))
    return false;
  // G15
  if (!control.guard(
          14, 0,
          [&](EP5W& why) {
            for (const auto* v :
                 {&workspace.cut[0], &workspace.cut[1], &workspace.cut[2],
                  &workspace.cut[3], &workspace.cut[4], &workspace.cut[5]})
              if (!control.supported(why, *v)) return false;
            if (workspace.cut[4].low != workspace.cut[4].high ||
                workspace.cut[5].low != workspace.cut[5].high) {
              why = EP5W::unsupported_arithmetic;
              return false;
            }
            if (workspace.cut[4].low < -.5)
              return control.attach(workspace.cut[4]);
            if (workspace.cut[4].low >= workspace.cut[5].low ||
                workspace.cut[5].low > .5)
              return control.attach(workspace.cut[5]);
            if (workspace.cut[4].low < workspace.cut[1].high)
              return control.attach(workspace.cut[4]);
            if (workspace.cut[5].low > workspace.cut[2].low ||
                workspace.cut[5].low > workspace.cut[3].low)
              return control.attach(workspace.cut[5]);
            if (!control.supported(why, workspace.endpoint[12]) ||
                !control.supported(why, workspace.endpoint[13]))
              return false;
            if (workspace.cut[4].low < workspace.endpoint[12].high)
              return control.attach(workspace.cut[4]);
            if (workspace.cut[5].low > workspace.endpoint[13].low)
              return control.attach(workspace.cut[5]);
            if (!std::isfinite(workspace.selected[5]) ||
                workspace.selected[5] <= 0) {
              why = EP5W::unsupported_arithmetic;
              return false;
            }
            workspace.endpoint_certificate[0] = true;
            return true;
          },
          Slice5Why::no_tau_interval))
    return false;
  // O083
  if (!control.operation(82, 0, workspace.endpoint[12],
                         [&] { return square(workspace.cut[4]); }))
    return false;
  // O084
  if (!control.operation(83, 0, workspace.endpoint[13], [&] {
        return subtract(point(1), workspace.endpoint[12]);
      }))
    return false;
  // G16
  if (!control.guard(
          15, 0,
          [&](EP5W& why) {
            if (!workspace.endpoint_certificate[0]) return false;
            if (!control.supported(why, workspace.endpoint[13])) return false;
            return workspace.endpoint[13].low > 0 ||
                   control.attach(workspace.endpoint[13]);
          },
          Slice5Why::endpoint_domain_unavailable))
    return false;
  // O085
  if (!control.operation(84, 0, workspace.endpoint[13], [&] {
        return transfer_small_root(workspace.endpoint[13]);
      }))
    return false;
  // O086
  if (!control.operation(85, 0, workspace.endpoint[14], [&] {
        return multiply(point(shin_length), workspace.endpoint[13]);
      }))
    return false;
  // O087
  if (!control.operation(86, 0, workspace.endpoint[15], [&] {
        return multiply(point(shin_length), workspace.cut[4]);
      }))
    return false;
  // O088
  if (!control.operation(87, 0, workspace.endpoint[15], [&] {
        return subtract(workspace.cut[0], workspace.endpoint[15]);
      }))
    return false;
  // O089
  if (!control.operation(88, 0, workspace.endpoint[16],
                         [&] { return square(workspace.endpoint[15]); }))
    return false;
  // O090
  if (!control.operation(89, 0, workspace.endpoint[16], [&] {
        return subtract(workspace.link[4], workspace.endpoint[16]);
      }))
    return false;
  // G17
  if (!control.guard(
          16, 0,
          [&](EP5W& why) {
            if (!workspace.endpoint_certificate[0]) return false;
            for (const auto* v : {&workspace.cut[0], &workspace.cut[4],
                                  &workspace.cut[5], &workspace.endpoint[16]})
              if (!control.supported(why, *v)) return false;
            return workspace.endpoint[16].high >= 0 ||
                   control.attach(workspace.endpoint[16]);
          },
          Slice5Why::endpoint_domain_unavailable))
    return false;
  // O091
  if (!control.operation(90, 0, workspace.endpoint[16], [&] {
        return [&] {
          const bool zero_branch = workspace.endpoint[16].low <= 0;
          const auto value =
              interval(zero_branch ? +0.0 : workspace.endpoint[16].low,
                       workspace.endpoint[16].high);
          if (ep5_valid(value) && zero_branch)
            d.slice.zero_mask |= std::uint8_t{1} << 2;
          return value;
        }();
      }))
    return false;
  // O092
  if (!control.operation(91, 0, workspace.endpoint[17], [&] {
        return transfer_small_root(workspace.endpoint[16]);
      }))
    return false;
  // O093
  if (!control.operation(92, 0, workspace.endpoint[0], [&] {
        return add(workspace.endpoint[17], workspace.endpoint[14]);
      }))
    return false;
  // O094
  if (!control.operation(93, 0, workspace.endpoint[12],
                         [&] { return square(workspace.cut[5]); }))
    return false;
  // O095
  if (!control.operation(94, 0, workspace.endpoint[13], [&] {
        return subtract(point(1), workspace.endpoint[12]);
      }))
    return false;
  // G18
  if (!control.guard(
          17, 0,
          [&](EP5W& why) {
            if (!workspace.endpoint_certificate[0]) return false;
            if (!control.supported(why, workspace.endpoint[13])) return false;
            return workspace.endpoint[13].low > 0 ||
                   control.attach(workspace.endpoint[13]);
          },
          Slice5Why::endpoint_domain_unavailable))
    return false;
  // O096
  if (!control.operation(95, 0, workspace.endpoint[13], [&] {
        return transfer_small_root(workspace.endpoint[13]);
      }))
    return false;
  // O097
  if (!control.operation(96, 0, workspace.endpoint[14], [&] {
        return multiply(point(shin_length), workspace.endpoint[13]);
      }))
    return false;
  // O098
  if (!control.operation(97, 0, workspace.endpoint[15], [&] {
        return multiply(point(shin_length), workspace.cut[5]);
      }))
    return false;
  // O099
  if (!control.operation(98, 0, workspace.endpoint[15], [&] {
        return subtract(workspace.cut[0], workspace.endpoint[15]);
      }))
    return false;
  // O100
  if (!control.operation(99, 0, workspace.endpoint[16],
                         [&] { return square(workspace.endpoint[15]); }))
    return false;
  // O101
  if (!control.operation(100, 0, workspace.endpoint[16], [&] {
        return subtract(workspace.link[4], workspace.endpoint[16]);
      }))
    return false;
  // G19
  if (!control.guard(
          18, 0,
          [&](EP5W& why) {
            if (!workspace.endpoint_certificate[0]) return false;
            for (const auto* v : {&workspace.cut[0], &workspace.cut[4],
                                  &workspace.cut[5], &workspace.endpoint[16]})
              if (!control.supported(why, *v)) return false;
            return workspace.endpoint[16].high >= 0 ||
                   control.attach(workspace.endpoint[16]);
          },
          Slice5Why::endpoint_domain_unavailable))
    return false;
  // O102
  if (!control.operation(101, 0, workspace.endpoint[16], [&] {
        return [&] {
          const bool zero_branch = workspace.endpoint[16].low <= 0;
          const auto value =
              interval(zero_branch ? +0.0 : workspace.endpoint[16].low,
                       workspace.endpoint[16].high);
          if (ep5_valid(value) && zero_branch)
            d.slice.zero_mask |= std::uint8_t{1} << 3;
          return value;
        }();
      }))
    return false;
  // O103
  if (!control.operation(102, 0, workspace.endpoint[17], [&] {
        return transfer_small_root(workspace.endpoint[16]);
      }))
    return false;
  // O104
  if (!control.operation(103, 0, workspace.endpoint[1], [&] {
        return add(workspace.endpoint[17], workspace.endpoint[14]);
      }))
    return false;
  // O105
  if (!control.operation(104, 0, workspace.endpoint[12],
                         [&] { return point(workspace.endpoint[0].high); }))
    return false;
  workspace.cuts[0] = workspace.endpoint[12].low;
  // O106
  if (!control.operation(105, 0, workspace.endpoint[13],
                         [&] { return point(workspace.endpoint[1].low); }))
    return false;
  workspace.cuts[1] = workspace.endpoint[13].low;
  // G20
  if (!control.guard(
          19, 0,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.endpoint[12]) ||
                !control.supported(why, workspace.endpoint[13]))
              return false;
            if (workspace.endpoint[12].low != workspace.endpoint[12].high ||
                workspace.endpoint[13].low != workspace.endpoint[13].high) {
              why = EP5W::unsupported_arithmetic;
              return false;
            }
            if (workspace.cuts[0] < 0)
              return control.attach(workspace.endpoint[12]);
            return workspace.cuts[0] < workspace.cuts[1] ||
                   control.attach(workspace.endpoint[13]);
          },
          Slice5Why::no_radial_interval))
    return false;
  // O107
  if (!control.operation(106, 0, workspace.endpoint[12],
                         [&] { return square(point(workspace.cuts[0])); }))
    return false;
  // O108
  if (!control.operation(107, 0, workspace.endpoint[12], [&] {
        return subtract(workspace.endpoint[12], point(workspace.chart[2].low));
      }))
    return false;
  // O109
  if (!control.operation(108, 0, workspace.endpoint[12], [&] {
        return [&] {
          const bool zero_branch = workspace.endpoint[12].high <= 0;
          const auto value =
              point(zero_branch ? +0.0 : workspace.endpoint[12].high);
          if (ep5_valid(value) && zero_branch)
            d.slice.zero_mask |= std::uint8_t{1} << 6;
          return value;
        }();
      }))
    return false;
  // O110
  if (!control.operation(109, 0, workspace.endpoint[12], [&] {
        return transfer_small_root(workspace.endpoint[12]);
      }))
    return false;
  // O111
  if (!control.operation(110, 0, workspace.endpoint[2], [&] {
        return add(point(workspace.chart[5].high),
                   point(workspace.endpoint[12].high));
      }))
    return false;
  // O112
  if (!control.operation(111, 0, workspace.endpoint[12],
                         [&] { return square(point(workspace.cuts[1])); }))
    return false;
  // O113
  if (!control.operation(112, 0, workspace.endpoint[12], [&] {
        return subtract(workspace.endpoint[12], point(workspace.chart[2].high));
      }))
    return false;
  // G21
  if (!control.guard(
          20, 0,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.endpoint[12])) return false;
            return workspace.endpoint[12].low > 0 ||
                   control.attach(workspace.endpoint[12]);
          },
          Slice5Why::no_positive_upper))
    return false;
  // O114
  if (!control.operation(113, 0, workspace.endpoint[12], [&] {
        return transfer_small_root(point(workspace.endpoint[12].low));
      }))
    return false;
  // O115
  if (!control.operation(114, 0, workspace.endpoint[3], [&] {
        return add(point(workspace.chart[5].low),
                   point(workspace.endpoint[12].low));
      }))
    return false;
  // O116
  if (!control.operation(115, 0, workspace.endpoint[12], [&] {
        return subtract(point(workspace.link[3].high),
                        point(workspace.chart[4].low));
      }))
    return false;
  // O117
  if (!control.operation(116, 0, workspace.endpoint[12], [&] {
        return [&] {
          const bool zero_branch = workspace.endpoint[12].high <= 0;
          const auto value =
              point(zero_branch ? +0.0 : workspace.endpoint[12].high);
          if (ep5_valid(value) && zero_branch)
            d.slice.zero_mask |= std::uint8_t{1} << 0;
          return value;
        }();
      }))
    return false;
  // O118
  if (!control.operation(117, 0, workspace.endpoint[12], [&] {
        return transfer_small_root(workspace.endpoint[12]);
      }))
    return false;
  // O119
  if (!control.operation(118, 0, workspace.endpoint[4], [&] {
        return add(point(workspace.chart[5].high),
                   point(workspace.endpoint[12].high));
      }))
    return false;
  // O120
  if (!control.operation(119, 0, workspace.endpoint[12], [&] {
        return subtract(point(workspace.link[1].low),
                        point(workspace.chart[4].high));
      }))
    return false;
  // G22
  if (!control.guard(
          21, 0,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.endpoint[12])) return false;
            return workspace.endpoint[12].low > 0 ||
                   control.attach(workspace.endpoint[12]);
          },
          Slice5Why::no_positive_upper))
    return false;
  // O121
  if (!control.operation(120, 0, workspace.endpoint[12], [&] {
        return transfer_small_root(point(workspace.endpoint[12].low));
      }))
    return false;
  // O122
  if (!control.operation(121, 0, workspace.endpoint[5], [&] {
        return add(point(workspace.chart[5].low),
                   point(workspace.endpoint[12].low));
      }))
    return false;
  // O123
  if (!control.operation(122, 0, workspace.chart[6],
                         [&] { return absolute(workspace.chart[0]); }))
    return false;
  // O124
  if (!control.operation(123, 0, workspace.endpoint[12], [&] {
        return divide(workspace.chart[6], workspace.link[8]);
      }))
    return false;
  // O125
  if (!control.operation(124, 0, workspace.chart[7], [&] {
        return add(point(workspace.chart[5].high),
                   point(workspace.endpoint[12].high));
      }))
    return false;
  // O126
  if (!control.operation(125, 1, workspace.cut[6],
                         [&] { return negate(workspace.chart[9]); }))
    return false;
  // O127
  if (!control.operation(126, 1, workspace.endpoint[12], [&] {
        return subtract(workspace.cut[6], point(thigh_length));
      }))
    return false;
  // O128
  if (!control.operation(127, 1, workspace.cut[7], [&] {
        return divide(workspace.endpoint[12], point(shin_length));
      }))
    return false;
  // O129
  if (!control.operation(128, 1, workspace.endpoint[12], [&] {
        return add(workspace.cut[6], point(thigh_length));
      }))
    return false;
  // O130
  if (!control.operation(129, 1, workspace.cut[8], [&] {
        return divide(workspace.endpoint[12], point(shin_length));
      }))
    return false;
  // O131
  if (!control.operation(130, 1, workspace.cut[9], [&] {
        return divide(workspace.cut[6], workspace.link[0]);
      }))
    return false;
  // O132
  if (!control.operation(131, 1, workspace.cut[10], [&] {
        return point(std::max(-.5, workspace.cut[7].high));
      }))
    return false;
  // O133
  if (!control.operation(132, 1, workspace.cut[11], [&] {
        return point(std::min(.5, workspace.cut[8].low));
      }))
    return false;
  // O134
  if (!control.operation(133, 1, workspace.cut[11], [&] {
        return point(std::min(workspace.cut[11].low, workspace.cut[9].low));
      }))
    return false;
  // O135
  if (!control.operation(134, 1, workspace.endpoint[12], [&] {
        return subtract(workspace.cut[6], point(workspace.selected[5]));
      }))
    return false;
  // O136
  if (!control.operation(135, 1, workspace.endpoint[12], [&] {
        return divide(workspace.endpoint[12], point(shin_length));
      }))
    return false;
  // O137
  if (!control.operation(136, 1, workspace.cut[10], [&] {
        return point(
            std::max(workspace.cut[10].low, workspace.endpoint[12].high));
      }))
    return false;
  // O138
  if (!control.operation(137, 1, workspace.endpoint[13], [&] {
        return divide(workspace.cut[6], point(shin_length));
      }))
    return false;
  // O139
  if (!control.operation(138, 1, workspace.cut[11], [&] {
        return point(
            std::min(workspace.cut[11].low, workspace.endpoint[13].low));
      }))
    return false;
  // G23
  if (!control.guard(
          22, 1,
          [&](EP5W& why) {
            for (const auto* v :
                 {&workspace.cut[6], &workspace.cut[7], &workspace.cut[8],
                  &workspace.cut[9], &workspace.cut[10], &workspace.cut[11]})
              if (!control.supported(why, *v)) return false;
            if (workspace.cut[10].low != workspace.cut[10].high ||
                workspace.cut[11].low != workspace.cut[11].high) {
              why = EP5W::unsupported_arithmetic;
              return false;
            }
            if (workspace.cut[10].low < -.5)
              return control.attach(workspace.cut[10]);
            if (workspace.cut[10].low >= workspace.cut[11].low ||
                workspace.cut[11].low > .5)
              return control.attach(workspace.cut[11]);
            if (workspace.cut[10].low < workspace.cut[7].high)
              return control.attach(workspace.cut[10]);
            if (workspace.cut[11].low > workspace.cut[8].low ||
                workspace.cut[11].low > workspace.cut[9].low)
              return control.attach(workspace.cut[11]);
            if (!control.supported(why, workspace.endpoint[12]) ||
                !control.supported(why, workspace.endpoint[13]))
              return false;
            if (workspace.cut[10].low < workspace.endpoint[12].high)
              return control.attach(workspace.cut[10]);
            if (workspace.cut[11].low > workspace.endpoint[13].low)
              return control.attach(workspace.cut[11]);
            if (!std::isfinite(workspace.selected[5]) ||
                workspace.selected[5] <= 0) {
              why = EP5W::unsupported_arithmetic;
              return false;
            }
            workspace.endpoint_certificate[1] = true;
            return true;
          },
          Slice5Why::no_tau_interval))
    return false;
  // O140
  if (!control.operation(139, 1, workspace.endpoint[12],
                         [&] { return square(workspace.cut[10]); }))
    return false;
  // O141
  if (!control.operation(140, 1, workspace.endpoint[13], [&] {
        return subtract(point(1), workspace.endpoint[12]);
      }))
    return false;
  // G24
  if (!control.guard(
          23, 1,
          [&](EP5W& why) {
            if (!workspace.endpoint_certificate[1]) return false;
            if (!control.supported(why, workspace.endpoint[13])) return false;
            return workspace.endpoint[13].low > 0 ||
                   control.attach(workspace.endpoint[13]);
          },
          Slice5Why::endpoint_domain_unavailable))
    return false;
  // O142
  if (!control.operation(141, 1, workspace.endpoint[13], [&] {
        return transfer_small_root(workspace.endpoint[13]);
      }))
    return false;
  // O143
  if (!control.operation(142, 1, workspace.endpoint[14], [&] {
        return multiply(point(shin_length), workspace.endpoint[13]);
      }))
    return false;
  // O144
  if (!control.operation(143, 1, workspace.endpoint[15], [&] {
        return multiply(point(shin_length), workspace.cut[10]);
      }))
    return false;
  // O145
  if (!control.operation(144, 1, workspace.endpoint[15], [&] {
        return subtract(workspace.cut[6], workspace.endpoint[15]);
      }))
    return false;
  // O146
  if (!control.operation(145, 1, workspace.endpoint[16],
                         [&] { return square(workspace.endpoint[15]); }))
    return false;
  // O147
  if (!control.operation(146, 1, workspace.endpoint[16], [&] {
        return subtract(workspace.link[4], workspace.endpoint[16]);
      }))
    return false;
  // G25
  if (!control.guard(
          24, 1,
          [&](EP5W& why) {
            if (!workspace.endpoint_certificate[1]) return false;
            for (const auto* v : {&workspace.cut[6], &workspace.cut[10],
                                  &workspace.cut[11], &workspace.endpoint[16]})
              if (!control.supported(why, *v)) return false;
            return workspace.endpoint[16].high >= 0 ||
                   control.attach(workspace.endpoint[16]);
          },
          Slice5Why::endpoint_domain_unavailable))
    return false;
  // O148
  if (!control.operation(147, 1, workspace.endpoint[16], [&] {
        return [&] {
          const bool zero_branch = workspace.endpoint[16].low <= 0;
          const auto value =
              interval(zero_branch ? +0.0 : workspace.endpoint[16].low,
                       workspace.endpoint[16].high);
          if (ep5_valid(value) && zero_branch)
            d.slice.zero_mask |= std::uint8_t{1} << 4;
          return value;
        }();
      }))
    return false;
  // O149
  if (!control.operation(148, 1, workspace.endpoint[17], [&] {
        return transfer_small_root(workspace.endpoint[16]);
      }))
    return false;
  // O150
  if (!control.operation(149, 1, workspace.endpoint[6], [&] {
        return add(workspace.endpoint[17], workspace.endpoint[14]);
      }))
    return false;
  // O151
  if (!control.operation(150, 1, workspace.endpoint[12],
                         [&] { return square(workspace.cut[11]); }))
    return false;
  // O152
  if (!control.operation(151, 1, workspace.endpoint[13], [&] {
        return subtract(point(1), workspace.endpoint[12]);
      }))
    return false;
  // G26
  if (!control.guard(
          25, 1,
          [&](EP5W& why) {
            if (!workspace.endpoint_certificate[1]) return false;
            if (!control.supported(why, workspace.endpoint[13])) return false;
            return workspace.endpoint[13].low > 0 ||
                   control.attach(workspace.endpoint[13]);
          },
          Slice5Why::endpoint_domain_unavailable))
    return false;
  // O153
  if (!control.operation(152, 1, workspace.endpoint[13], [&] {
        return transfer_small_root(workspace.endpoint[13]);
      }))
    return false;
  // O154
  if (!control.operation(153, 1, workspace.endpoint[14], [&] {
        return multiply(point(shin_length), workspace.endpoint[13]);
      }))
    return false;
  // O155
  if (!control.operation(154, 1, workspace.endpoint[15], [&] {
        return multiply(point(shin_length), workspace.cut[11]);
      }))
    return false;
  // O156
  if (!control.operation(155, 1, workspace.endpoint[15], [&] {
        return subtract(workspace.cut[6], workspace.endpoint[15]);
      }))
    return false;
  // O157
  if (!control.operation(156, 1, workspace.endpoint[16],
                         [&] { return square(workspace.endpoint[15]); }))
    return false;
  // O158
  if (!control.operation(157, 1, workspace.endpoint[16], [&] {
        return subtract(workspace.link[4], workspace.endpoint[16]);
      }))
    return false;
  // G27
  if (!control.guard(
          26, 1,
          [&](EP5W& why) {
            if (!workspace.endpoint_certificate[1]) return false;
            for (const auto* v : {&workspace.cut[6], &workspace.cut[10],
                                  &workspace.cut[11], &workspace.endpoint[16]})
              if (!control.supported(why, *v)) return false;
            return workspace.endpoint[16].high >= 0 ||
                   control.attach(workspace.endpoint[16]);
          },
          Slice5Why::endpoint_domain_unavailable))
    return false;
  // O159
  if (!control.operation(158, 1, workspace.endpoint[16], [&] {
        return [&] {
          const bool zero_branch = workspace.endpoint[16].low <= 0;
          const auto value =
              interval(zero_branch ? +0.0 : workspace.endpoint[16].low,
                       workspace.endpoint[16].high);
          if (ep5_valid(value) && zero_branch)
            d.slice.zero_mask |= std::uint8_t{1} << 5;
          return value;
        }();
      }))
    return false;
  // O160
  if (!control.operation(159, 1, workspace.endpoint[17], [&] {
        return transfer_small_root(workspace.endpoint[16]);
      }))
    return false;
  // O161
  if (!control.operation(160, 1, workspace.endpoint[7], [&] {
        return add(workspace.endpoint[17], workspace.endpoint[14]);
      }))
    return false;
  // O162
  if (!control.operation(161, 1, workspace.endpoint[12],
                         [&] { return point(workspace.endpoint[6].high); }))
    return false;
  workspace.cuts[2] = workspace.endpoint[12].low;
  // O163
  if (!control.operation(162, 1, workspace.endpoint[13],
                         [&] { return point(workspace.endpoint[7].low); }))
    return false;
  workspace.cuts[3] = workspace.endpoint[13].low;
  // G28
  if (!control.guard(
          27, 1,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.endpoint[12]) ||
                !control.supported(why, workspace.endpoint[13]))
              return false;
            if (workspace.endpoint[12].low != workspace.endpoint[12].high ||
                workspace.endpoint[13].low != workspace.endpoint[13].high) {
              why = EP5W::unsupported_arithmetic;
              return false;
            }
            if (workspace.cuts[2] < 0)
              return control.attach(workspace.endpoint[12]);
            return workspace.cuts[2] < workspace.cuts[3] ||
                   control.attach(workspace.endpoint[13]);
          },
          Slice5Why::no_radial_interval))
    return false;
  // O164
  if (!control.operation(163, 1, workspace.endpoint[12],
                         [&] { return square(point(workspace.cuts[2])); }))
    return false;
  // O165
  if (!control.operation(164, 1, workspace.endpoint[12], [&] {
        return subtract(workspace.endpoint[12], point(workspace.chart[10].low));
      }))
    return false;
  // O166
  if (!control.operation(165, 1, workspace.endpoint[12], [&] {
        return [&] {
          const bool zero_branch = workspace.endpoint[12].high <= 0;
          const auto value =
              point(zero_branch ? +0.0 : workspace.endpoint[12].high);
          if (ep5_valid(value) && zero_branch)
            d.slice.zero_mask |= std::uint8_t{1} << 7;
          return value;
        }();
      }))
    return false;
  // O167
  if (!control.operation(166, 1, workspace.endpoint[12], [&] {
        return transfer_small_root(workspace.endpoint[12]);
      }))
    return false;
  // O168
  if (!control.operation(167, 1, workspace.endpoint[8], [&] {
        return add(point(workspace.chart[13].high),
                   point(workspace.endpoint[12].high));
      }))
    return false;
  // O169
  if (!control.operation(168, 1, workspace.endpoint[12],
                         [&] { return square(point(workspace.cuts[3])); }))
    return false;
  // O170
  if (!control.operation(169, 1, workspace.endpoint[12], [&] {
        return subtract(workspace.endpoint[12],
                        point(workspace.chart[10].high));
      }))
    return false;
  // G29
  if (!control.guard(
          28, 1,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.endpoint[12])) return false;
            return workspace.endpoint[12].low > 0 ||
                   control.attach(workspace.endpoint[12]);
          },
          Slice5Why::no_positive_upper))
    return false;
  // O171
  if (!control.operation(170, 1, workspace.endpoint[12], [&] {
        return transfer_small_root(point(workspace.endpoint[12].low));
      }))
    return false;
  // O172
  if (!control.operation(171, 1, workspace.endpoint[9], [&] {
        return add(point(workspace.chart[13].low),
                   point(workspace.endpoint[12].low));
      }))
    return false;
  // O173
  if (!control.operation(172, 1, workspace.endpoint[12], [&] {
        return subtract(point(workspace.link[3].high),
                        point(workspace.chart[12].low));
      }))
    return false;
  // O174
  if (!control.operation(173, 1, workspace.endpoint[12], [&] {
        return [&] {
          const bool zero_branch = workspace.endpoint[12].high <= 0;
          const auto value =
              point(zero_branch ? +0.0 : workspace.endpoint[12].high);
          if (ep5_valid(value) && zero_branch)
            d.slice.zero_mask |= std::uint8_t{1} << 1;
          return value;
        }();
      }))
    return false;
  // O175
  if (!control.operation(174, 1, workspace.endpoint[12], [&] {
        return transfer_small_root(workspace.endpoint[12]);
      }))
    return false;
  // O176
  if (!control.operation(175, 1, workspace.endpoint[10], [&] {
        return add(point(workspace.chart[13].high),
                   point(workspace.endpoint[12].high));
      }))
    return false;
  // O177
  if (!control.operation(176, 1, workspace.endpoint[12], [&] {
        return subtract(point(workspace.link[1].low),
                        point(workspace.chart[12].high));
      }))
    return false;
  // G30
  if (!control.guard(
          29, 1,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.endpoint[12])) return false;
            return workspace.endpoint[12].low > 0 ||
                   control.attach(workspace.endpoint[12]);
          },
          Slice5Why::no_positive_upper))
    return false;
  // O178
  if (!control.operation(177, 1, workspace.endpoint[12], [&] {
        return transfer_small_root(point(workspace.endpoint[12].low));
      }))
    return false;
  // O179
  if (!control.operation(178, 1, workspace.endpoint[11], [&] {
        return add(point(workspace.chart[13].low),
                   point(workspace.endpoint[12].low));
      }))
    return false;
  // O180
  if (!control.operation(179, 1, workspace.chart[14],
                         [&] { return absolute(workspace.chart[8]); }))
    return false;
  // O181
  if (!control.operation(180, 1, workspace.endpoint[12], [&] {
        return divide(workspace.chart[14], workspace.link[8]);
      }))
    return false;
  // O182
  if (!control.operation(181, 1, workspace.chart[15], [&] {
        return add(point(workspace.chart[13].high),
                   point(workspace.endpoint[12].high));
      }))
    return false;
  // O183
  if (!control.operation(182, 255, workspace.endpoint[12], [&] {
        return point(
            std::max(workspace.endpoint[2].high, workspace.endpoint[8].high));
      }))
    return false;
  workspace.selected[0] = workspace.endpoint[12].low;
  // O184
  if (!control.operation(183, 255, workspace.endpoint[12], [&] {
        return point(
            std::max(workspace.selected[0], workspace.endpoint[4].high));
      }))
    return false;
  workspace.selected[0] = workspace.endpoint[12].low;
  // O185
  if (!control.operation(184, 255, workspace.endpoint[12], [&] {
        return point(
            std::max(workspace.selected[0], workspace.endpoint[10].high));
      }))
    return false;
  workspace.selected[0] = workspace.endpoint[12].low;
  // O186
  if (!control.operation(185, 255, workspace.endpoint[12], [&] {
        return point(std::max(workspace.selected[0], workspace.chart[7].high));
      }))
    return false;
  workspace.selected[0] = workspace.endpoint[12].low;
  // O187
  if (!control.operation(186, 255, workspace.endpoint[12], [&] {
        return point(std::max(workspace.selected[0], workspace.chart[15].high));
      }))
    return false;
  workspace.selected[0] = workspace.endpoint[12].low;
  d.slice.lo = workspace.selected[0];
  // O188
  if (!control.operation(187, 255, workspace.endpoint[12], [&] {
        return point(
            std::min(workspace.endpoint[3].low, workspace.endpoint[9].low));
      }))
    return false;
  workspace.selected[1] = workspace.endpoint[12].low;
  // O189
  if (!control.operation(188, 255, workspace.endpoint[12], [&] {
        return point(
            std::min(workspace.selected[1], workspace.endpoint[5].low));
      }))
    return false;
  workspace.selected[1] = workspace.endpoint[12].low;
  // O190
  if (!control.operation(189, 255, workspace.endpoint[12], [&] {
        return point(
            std::min(workspace.selected[1], workspace.endpoint[11].low));
      }))
    return false;
  workspace.selected[1] = workspace.endpoint[12].low;
  d.slice.hi = workspace.selected[1];
  // G31
  if (!control.guard(
          30, 255,
          [&](EP5W& why) {
            if (!std::isfinite(d.slice.lo) || !std::isfinite(d.slice.hi)) {
              why = EP5W::unsupported_arithmetic;
              return false;
            }
            return d.slice.lo < d.slice.hi || control.attach(point(d.slice.hi));
          },
          Slice5Why::empty_inward_interval))
    return false;
  // O191
  if (!control.operation(190, 255, workspace.endpoint[12],
                         [&] { return point(d.slice.hi - d.slice.lo); }))
    return false;
  workspace.selected[2] = workspace.endpoint[12].low;
  // O192
  if (!control.operation(191, 255, workspace.endpoint[12],
                         [&] { return point(workspace.selected[2] * .5); }))
    return false;
  workspace.selected[3] = workspace.endpoint[12].low;
  // O193
  if (!control.operation(192, 255, workspace.endpoint[12], [&] {
        return point(d.slice.lo + workspace.selected[3]);
      }))
    return false;
  workspace.selected[4] = workspace.endpoint[12].low;
  d.slice.y = workspace.selected[4];
  // G32
  if (!control.guard(
          31, 255,
          [&](EP5W& why) {
            if (!std::isfinite(d.slice.y)) {
              why = EP5W::unsupported_arithmetic;
              return false;
            }
            return std::abs(d.slice.y) <= 8 || control.attach(point(d.slice.y));
          },
          Slice5Why::candidate_domain))
    return false;
  // G33
  if (!control.guard(
          32, 255,
          [&](EP5W& why) {
            (void)why;
            return d.slice.y > d.slice.lo || control.attach(point(d.slice.y));
          },
          Slice5Why::midpoint_unavailable))
    return false;
  // G34
  if (!control.guard(
          33, 255,
          [&](EP5W& why) {
            (void)why;
            return d.slice.y < d.slice.hi || control.attach(point(d.slice.y));
          },
          Slice5Why::midpoint_unavailable))
    return false;
  // O194
  if (!control.operation(193, 0, workspace.direct[0], [&] {
        return subtract(point(d.slice.y), workspace.chart[5]);
      }))
    return false;
  // O195
  if (!control.operation(194, 0, workspace.direct[1],
                         [&] { return square(workspace.direct[0]); }))
    return false;
  // O196
  if (!control.operation(195, 0, workspace.direct[1], [&] {
        return add(workspace.chart[2], workspace.direct[1]);
      }))
    return false;
  // O197
  if (!control.operation(196, 0, workspace.direct[2], [&] {
        return add(workspace.direct[1], workspace.chart[3]);
      }))
    return false;
  // G35
  if (!control.guard(
          34, 0,
          [&](EP5W& why) {
            for (const auto* v : {&workspace.direct[0], &workspace.direct[1],
                                  &workspace.direct[2]})
              if (!control.supported(why, *v)) return false;
            if (workspace.direct[0].low <= 0)
              return control.attach(workspace.direct[0]);
            if (workspace.direct[1].low <= 0)
              return control.attach(workspace.direct[1]);
            if (workspace.direct[2].low <= workspace.link[3].high)
              return control.attach(workspace.direct[2]);
            return workspace.direct[2].high < workspace.link[1].low ||
                   control.attach(workspace.direct[2]);
          },
          Slice5Why::verification_inconclusive))
    return false;
  // O198
  if (!control.operation(197, 0, workspace.direct[3], [&] {
        return transfer_small_root(workspace.direct[1]);
      }))
    return false;
  // O199
  if (!control.operation(198, 0, workspace.direct[4], [&] {
        return add(workspace.link[6], workspace.direct[2]);
      }))
    return false;
  // O200
  if (!control.operation(199, 0, workspace.direct[5], [&] {
        return multiply(point(2), workspace.direct[2]);
      }))
    return false;
  // O201
  if (!control.operation(200, 0, workspace.direct[4], [&] {
        return divide(workspace.direct[4], workspace.direct[5]);
      }))
    return false;
  // O202
  if (!control.operation(201, 0, workspace.direct[6], [&] {
        return subtract(workspace.link[1], workspace.direct[2]);
      }))
    return false;
  // O203
  if (!control.operation(202, 0, workspace.direct[7], [&] {
        return subtract(workspace.direct[2], workspace.link[3]);
      }))
    return false;
  // O204
  if (!control.operation(203, 0, workspace.direct[6], [&] {
        return multiply(workspace.direct[6], workspace.direct[7]);
      }))
    return false;
  // O205
  if (!control.operation(204, 0, workspace.direct[7],
                         [&] { return square(workspace.direct[2]); }))
    return false;
  // O206
  if (!control.operation(205, 0, workspace.direct[7], [&] {
        return multiply(point(4), workspace.direct[7]);
      }))
    return false;
  // O207
  if (!control.operation(206, 0, workspace.direct[7], [&] {
        return divide(workspace.direct[6], workspace.direct[7]);
      }))
    return false;
  // G36
  if (!control.guard(
          35, 0,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.direct[7])) return false;
            return workspace.direct[7].low > 0 ||
                   control.attach(workspace.direct[7]);
          },
          Slice5Why::verification_inconclusive))
    return false;
  // O208
  if (!control.operation(207, 0, workspace.direct[7], [&] {
        return transfer_small_root(workspace.direct[7]);
      }))
    return false;
  // O209
  if (!control.operation(208, 0, workspace.direct[5], [&] {
        return subtract(point(1), workspace.direct[4]);
      }))
    return false;
  // O210
  if (!control.operation(209, 0, workspace.direct[8], [&] {
        return multiply(workspace.direct[4], workspace.direct[3]);
      }))
    return false;
  // O211
  if (!control.operation(210, 0, workspace.direct[9], [&] {
        return multiply(workspace.direct[7], workspace.chart[1]);
      }))
    return false;
  // O212
  if (!control.operation(211, 0, workspace.direct[8], [&] {
        return add(workspace.direct[8], workspace.direct[9]);
      }))
    return false;
  // O213
  if (!control.operation(212, 0, workspace.direct[10], [&] {
        return multiply(workspace.direct[7], workspace.direct[3]);
      }))
    return false;
  // O214
  if (!control.operation(213, 0, workspace.direct[11], [&] {
        return multiply(workspace.direct[4], workspace.chart[1]);
      }))
    return false;
  // O215
  if (!control.operation(214, 0, workspace.direct[11], [&] {
        return subtract(workspace.direct[10], workspace.direct[11]);
      }))
    return false;
  // O216
  if (!control.operation(215, 0, workspace.direct[12], [&] {
        return multiply(workspace.direct[5], workspace.direct[3]);
      }))
    return false;
  // O217
  if (!control.operation(216, 0, workspace.direct[12], [&] {
        return subtract(workspace.direct[12], workspace.direct[9]);
      }))
    return false;
  // O218
  if (!control.operation(217, 0, workspace.direct[13], [&] {
        return multiply(workspace.direct[5], workspace.chart[1]);
      }))
    return false;
  // O219
  if (!control.operation(218, 0, workspace.direct[13], [&] {
        return add(workspace.direct[13], workspace.direct[10]);
      }))
    return false;
  // O220
  if (!control.operation(219, 0, workspace.direct[13],
                         [&] { return negate(workspace.direct[13]); }))
    return false;
  // G37
  if (!control.guard(
          36, 0,
          [&](EP5W& why) {
            for (const auto* v : {&workspace.direct[8], &workspace.direct[12],
                                  &workspace.direct[11], &workspace.direct[13]})
              if (!control.supported(why, *v)) return false;
            if (workspace.direct[8].low <= 0)
              return control.attach(workspace.direct[8]);
            if (workspace.direct[12].low <= 0)
              return control.attach(workspace.direct[12]);
            if (workspace.direct[11].low <= 0) {
              d.slice.condition = Slice5Why::hip_descent_unavailable;
              return control.attach(workspace.direct[11]);
            }
            return true;
          },
          Slice5Why::verification_inconclusive))
    return false;
  // O221
  if (!control.operation(220, 0, workspace.direct[14],
                         [&] { return absolute(workspace.direct[13]); }))
    return false;
  // O222
  if (!control.operation(221, 0, workspace.direct[15], [&] {
        return multiply(workspace.direct[12], point(.5));
      }))
    return false;
  // O223
  if (!control.operation(222, 0, workspace.direct[14], [&] {
        return multiply(workspace.direct[14], workspace.link[7]);
      }))
    return false;
  // O224
  if (!control.operation(223, 0, workspace.direct[15], [&] {
        return subtract(workspace.direct[15], workspace.direct[14]);
      }))
    return false;
  // O225
  if (!control.operation(224, 0, workspace.direct[16], [&] {
        return multiply(workspace.direct[0], workspace.link[8]);
      }))
    return false;
  // O226
  if (!control.operation(225, 0, workspace.direct[16], [&] {
        return subtract(workspace.direct[16], workspace.chart[6]);
      }))
    return false;
  // G38
  if (!control.guard(
          37, 0,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.direct[15]) ||
                !control.supported(why, workspace.direct[16]))
              return false;
            if (workspace.direct[15].low < 0)
              return control.attach(workspace.direct[15]);
            return workspace.direct[16].low >= 0 ||
                   control.attach(workspace.direct[16]);
          },
          Slice5Why::verification_inconclusive))
    return false;
  // O227
  if (!control.operation(226, 0, workspace.direct[1], [&] {
        return multiply(workspace.chart[0], workspace.chart[1]);
      }))
    return false;
  // O228
  if (!control.operation(227, 0, workspace.direct[1], [&] {
        return divide(workspace.direct[1], workspace.direct[3]);
      }))
    return false;
  // O229
  if (!control.operation(228, 0, workspace.direct[2],
                         [&] { return negate(workspace.direct[0]); }))
    return false;
  // O230
  if (!control.operation(229, 0, workspace.direct[5], [&] {
        return multiply(workspace.direct[2], workspace.chart[1]);
      }))
    return false;
  // O231
  if (!control.operation(230, 0, workspace.direct[5], [&] {
        return divide(workspace.direct[5], workspace.direct[3]);
      }))
    return false;
  // O232
  if (!control.operation(231, 0, workspace.direct[6],
                         [&] { return negate(workspace.direct[3]); }))
    return false;
  // O233
  if (!control.operation(232, 0, workspace.direct[8], [&] {
        return multiply(workspace.direct[4], workspace.chart[0]);
      }))
    return false;
  // O234
  if (!control.operation(233, 0, workspace.direct[9], [&] {
        return multiply(workspace.direct[4], workspace.direct[2]);
      }))
    return false;
  // O235
  if (!control.operation(234, 0, workspace.direct[10], [&] {
        return multiply(workspace.direct[4], workspace.chart[1]);
      }))
    return false;
  // O236
  if (!control.operation(235, 0, workspace.direct[11], [&] {
        return multiply(workspace.direct[7], workspace.direct[1]);
      }))
    return false;
  // O237
  if (!control.operation(236, 0, workspace.direct[12], [&] {
        return multiply(workspace.direct[7], workspace.direct[5]);
      }))
    return false;
  // O238
  if (!control.operation(237, 0, workspace.direct[13], [&] {
        return multiply(workspace.direct[7], workspace.direct[6]);
      }))
    return false;
  // O239
  if (!control.operation(238, 0, workspace.direct[14], [&] {
        return add(workspace.direct[8], workspace.direct[11]);
      }))
    return false;
  // O240
  if (!control.operation(239, 0, workspace.direct[15], [&] {
        return add(workspace.direct[9], workspace.direct[12]);
      }))
    return false;
  // O241
  if (!control.operation(240, 0, workspace.direct[16], [&] {
        return add(workspace.direct[10], workspace.direct[13]);
      }))
    return false;
  // O242
  if (!control.operation(241, 0, workspace.direct[1], [&] {
        return multiply(workspace.direct[14], point(1));
      }))
    return false;
  // O243
  if (!control.operation(242, 0, workspace.direct[2], [&] {
        return multiply(workspace.direct[15], point(0));
      }))
    return false;
  // O244
  if (!control.operation(243, 0, workspace.direct[1], [&] {
        return add(workspace.direct[1], workspace.direct[2]);
      }))
    return false;
  // O245
  if (!control.operation(244, 0, workspace.direct[3], [&] {
        return multiply(workspace.direct[16], point(0));
      }))
    return false;
  // O246
  if (!control.operation(245, 0, workspace.direct[4], [&] {
        return add(workspace.direct[1], workspace.direct[3]);
      }))
    return false;
  // O247
  if (!control.operation(246, 0, workspace.direct[1], [&] {
        return multiply(workspace.direct[14], point(0));
      }))
    return false;
  // O248
  if (!control.operation(247, 0, workspace.direct[2], [&] {
        return multiply(workspace.direct[15], point(1));
      }))
    return false;
  // O249
  if (!control.operation(248, 0, workspace.direct[1], [&] {
        return add(workspace.direct[1], workspace.direct[2]);
      }))
    return false;
  // O250
  if (!control.operation(249, 0, workspace.direct[3], [&] {
        return multiply(workspace.direct[16], point(0));
      }))
    return false;
  // O251
  if (!control.operation(250, 0, workspace.direct[7], [&] {
        return add(workspace.direct[1], workspace.direct[3]);
      }))
    return false;
  // O252
  if (!control.operation(251, 0, workspace.direct[1], [&] {
        return multiply(workspace.direct[14], point(0));
      }))
    return false;
  // O253
  if (!control.operation(252, 0, workspace.direct[2], [&] {
        return multiply(workspace.direct[15], point(0));
      }))
    return false;
  // O254
  if (!control.operation(253, 0, workspace.direct[1], [&] {
        return add(workspace.direct[1], workspace.direct[2]);
      }))
    return false;
  // O255
  if (!control.operation(254, 0, workspace.direct[3], [&] {
        return multiply(workspace.direct[16], point(1));
      }))
    return false;
  // O256
  if (!control.operation(255, 0, workspace.direct[0], [&] {
        return add(workspace.direct[1], workspace.direct[3]);
      }))
    return false;
  // G39
  if (!control.guard(
          38, 0,
          [&](EP5W& why) {
            if (!ep5_source_bit(d, 63) || kBoardingRouteFootPhaseVersion != 1 ||
                !intermediate_endpoint05_template_valid(request, false, 0) ||
                request.feet[0].yaw_half != std::array<double, 2>{0, 0}) {
              why = EP5W::slice_identity;
              return false;
            }
            for (const auto* v : {&workspace.direct[4], &workspace.direct[7],
                                  &workspace.direct[0]}) {
              if (!control.supported(why, *v)) return false;
              if (v->low < -16 || v->high > 16) {
                why = EP5W::unsupported_arithmetic;
                return control.attach(*v);
              }
            }
            return true;
          },
          Slice5Why::hip_verification_inconclusive))
    return false;
  // O257
  if (!control.operation(256, 0, workspace.direct[4], [&] {
        return ep5_unit_divide(workspace.direct[4], thigh_length);
      }))
    return false;
  // O258
  if (!control.operation(257, 0, workspace.direct[7], [&] {
        return ep5_unit_divide(workspace.direct[7], thigh_length);
      }))
    return false;
  // O259
  if (!control.operation(258, 0, workspace.direct[0], [&] {
        return ep5_unit_divide(workspace.direct[0], thigh_length);
      }))
    return false;
  // G40
  if (!control.guard(
          39, 0,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.direct[4]) ||
                !control.supported(why, workspace.direct[7]) ||
                !control.supported(why, workspace.direct[0]))
              return false;
            return workspace.direct[7].high < 0 ||
                   control.attach(workspace.direct[7]);
          },
          Slice5Why::hip_verification_inconclusive))
    return false;
  // O260
  if (!control.operation(259, 0, workspace.direct[1],
                         [&] { return square(workspace.direct[4]); }))
    return false;
  // O261
  if (!control.operation(260, 0, workspace.direct[2],
                         [&] { return square(workspace.direct[0]); }))
    return false;
  // O262
  if (!control.operation(261, 0, workspace.direct[1], [&] {
        return add(workspace.direct[1], workspace.direct[2]);
      }))
    return false;
  // O263
  if (!control.operation(262, 0, workspace.direct[1], [&] {
        return transfer_small_root(workspace.direct[1]);
      }))
    return false;
  // O264
  if (!control.operation(263, 0, workspace.direct[2], [&] {
        return multiply(point(kBoardingSelfHipLengthMetres),
                        workspace.direct[7]);
      }))
    return false;
  // O265
  if (!control.operation(264, 0, workspace.direct[3], [&] {
        return multiply(point(.105), workspace.direct[1]);
      }))
    return false;
  // O266
  if (!control.operation(265, 0, workspace.direct[14], [&] {
        return add(workspace.direct[2], workspace.direct[3]);
      }))
    return false;
  // O267
  if (!control.operation(266, 0, workspace.direct[15],
                         [&] { return negate(point(.12)); }))
    return false;
  // O268
  if (!control.operation(267, 0, workspace.direct[16], [&] {
        return subtract(workspace.direct[15], workspace.direct[14]);
      }))
    return false;
  // G41
  if (!control.guard(
          40, 0,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.direct[1]) ||
                !control.supported(why, workspace.direct[14]) ||
                !control.supported(why, workspace.direct[15]) ||
                !control.supported(why, workspace.direct[16]))
              return false;
            return workspace.direct[14].high < workspace.direct[15].low ||
                   control.attach(workspace.direct[14]);
          },
          Slice5Why::hip_verification_inconclusive))
    return false;
  // O269
  if (!control.operation(268, 1, workspace.direct[0], [&] {
        return subtract(point(d.slice.y), workspace.chart[13]);
      }))
    return false;
  // O270
  if (!control.operation(269, 1, workspace.direct[1],
                         [&] { return square(workspace.direct[0]); }))
    return false;
  // O271
  if (!control.operation(270, 1, workspace.direct[1], [&] {
        return add(workspace.chart[10], workspace.direct[1]);
      }))
    return false;
  // O272
  if (!control.operation(271, 1, workspace.direct[2], [&] {
        return add(workspace.direct[1], workspace.chart[11]);
      }))
    return false;
  // G42
  if (!control.guard(
          41, 1,
          [&](EP5W& why) {
            for (const auto* v : {&workspace.direct[0], &workspace.direct[1],
                                  &workspace.direct[2]})
              if (!control.supported(why, *v)) return false;
            if (workspace.direct[0].low <= 0)
              return control.attach(workspace.direct[0]);
            if (workspace.direct[1].low <= 0)
              return control.attach(workspace.direct[1]);
            if (workspace.direct[2].low <= workspace.link[3].high)
              return control.attach(workspace.direct[2]);
            return workspace.direct[2].high < workspace.link[1].low ||
                   control.attach(workspace.direct[2]);
          },
          Slice5Why::verification_inconclusive))
    return false;
  // O273
  if (!control.operation(272, 1, workspace.direct[3], [&] {
        return transfer_small_root(workspace.direct[1]);
      }))
    return false;
  // O274
  if (!control.operation(273, 1, workspace.direct[4], [&] {
        return add(workspace.link[6], workspace.direct[2]);
      }))
    return false;
  // O275
  if (!control.operation(274, 1, workspace.direct[5], [&] {
        return multiply(point(2), workspace.direct[2]);
      }))
    return false;
  // O276
  if (!control.operation(275, 1, workspace.direct[4], [&] {
        return divide(workspace.direct[4], workspace.direct[5]);
      }))
    return false;
  // O277
  if (!control.operation(276, 1, workspace.direct[6], [&] {
        return subtract(workspace.link[1], workspace.direct[2]);
      }))
    return false;
  // O278
  if (!control.operation(277, 1, workspace.direct[7], [&] {
        return subtract(workspace.direct[2], workspace.link[3]);
      }))
    return false;
  // O279
  if (!control.operation(278, 1, workspace.direct[6], [&] {
        return multiply(workspace.direct[6], workspace.direct[7]);
      }))
    return false;
  // O280
  if (!control.operation(279, 1, workspace.direct[7],
                         [&] { return square(workspace.direct[2]); }))
    return false;
  // O281
  if (!control.operation(280, 1, workspace.direct[7], [&] {
        return multiply(point(4), workspace.direct[7]);
      }))
    return false;
  // O282
  if (!control.operation(281, 1, workspace.direct[7], [&] {
        return divide(workspace.direct[6], workspace.direct[7]);
      }))
    return false;
  // G43
  if (!control.guard(
          42, 1,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.direct[7])) return false;
            return workspace.direct[7].low > 0 ||
                   control.attach(workspace.direct[7]);
          },
          Slice5Why::verification_inconclusive))
    return false;
  // O283
  if (!control.operation(282, 1, workspace.direct[7], [&] {
        return transfer_small_root(workspace.direct[7]);
      }))
    return false;
  // O284
  if (!control.operation(283, 1, workspace.direct[5], [&] {
        return subtract(point(1), workspace.direct[4]);
      }))
    return false;
  // O285
  if (!control.operation(284, 1, workspace.direct[8], [&] {
        return multiply(workspace.direct[4], workspace.direct[3]);
      }))
    return false;
  // O286
  if (!control.operation(285, 1, workspace.direct[9], [&] {
        return multiply(workspace.direct[7], workspace.chart[9]);
      }))
    return false;
  // O287
  if (!control.operation(286, 1, workspace.direct[8], [&] {
        return add(workspace.direct[8], workspace.direct[9]);
      }))
    return false;
  // O288
  if (!control.operation(287, 1, workspace.direct[10], [&] {
        return multiply(workspace.direct[7], workspace.direct[3]);
      }))
    return false;
  // O289
  if (!control.operation(288, 1, workspace.direct[11], [&] {
        return multiply(workspace.direct[4], workspace.chart[9]);
      }))
    return false;
  // O290
  if (!control.operation(289, 1, workspace.direct[11], [&] {
        return subtract(workspace.direct[10], workspace.direct[11]);
      }))
    return false;
  // O291
  if (!control.operation(290, 1, workspace.direct[12], [&] {
        return multiply(workspace.direct[5], workspace.direct[3]);
      }))
    return false;
  // O292
  if (!control.operation(291, 1, workspace.direct[12], [&] {
        return subtract(workspace.direct[12], workspace.direct[9]);
      }))
    return false;
  // O293
  if (!control.operation(292, 1, workspace.direct[13], [&] {
        return multiply(workspace.direct[5], workspace.chart[9]);
      }))
    return false;
  // O294
  if (!control.operation(293, 1, workspace.direct[13], [&] {
        return add(workspace.direct[13], workspace.direct[10]);
      }))
    return false;
  // O295
  if (!control.operation(294, 1, workspace.direct[13],
                         [&] { return negate(workspace.direct[13]); }))
    return false;
  // G44
  if (!control.guard(
          43, 1,
          [&](EP5W& why) {
            for (const auto* v : {&workspace.direct[8], &workspace.direct[12],
                                  &workspace.direct[11], &workspace.direct[13]})
              if (!control.supported(why, *v)) return false;
            if (workspace.direct[8].low <= 0)
              return control.attach(workspace.direct[8]);
            if (workspace.direct[12].low <= 0)
              return control.attach(workspace.direct[12]);
            if (workspace.direct[11].low <= 0) {
              d.slice.condition = Slice5Why::hip_descent_unavailable;
              return control.attach(workspace.direct[11]);
            }
            return true;
          },
          Slice5Why::verification_inconclusive))
    return false;
  // O296
  if (!control.operation(295, 1, workspace.direct[14],
                         [&] { return absolute(workspace.direct[13]); }))
    return false;
  // O297
  if (!control.operation(296, 1, workspace.direct[15], [&] {
        return multiply(workspace.direct[12], point(.5));
      }))
    return false;
  // O298
  if (!control.operation(297, 1, workspace.direct[14], [&] {
        return multiply(workspace.direct[14], workspace.link[7]);
      }))
    return false;
  // O299
  if (!control.operation(298, 1, workspace.direct[15], [&] {
        return subtract(workspace.direct[15], workspace.direct[14]);
      }))
    return false;
  // O300
  if (!control.operation(299, 1, workspace.direct[16], [&] {
        return multiply(workspace.direct[0], workspace.link[8]);
      }))
    return false;
  // O301
  if (!control.operation(300, 1, workspace.direct[16], [&] {
        return subtract(workspace.direct[16], workspace.chart[14]);
      }))
    return false;
  // G45
  if (!control.guard(
          44, 1,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.direct[15]) ||
                !control.supported(why, workspace.direct[16]))
              return false;
            if (workspace.direct[15].low < 0)
              return control.attach(workspace.direct[15]);
            return workspace.direct[16].low >= 0 ||
                   control.attach(workspace.direct[16]);
          },
          Slice5Why::verification_inconclusive))
    return false;
  // O302
  if (!control.operation(301, 1, workspace.direct[1], [&] {
        return multiply(workspace.chart[8], workspace.chart[9]);
      }))
    return false;
  // O303
  if (!control.operation(302, 1, workspace.direct[1], [&] {
        return divide(workspace.direct[1], workspace.direct[3]);
      }))
    return false;
  // O304
  if (!control.operation(303, 1, workspace.direct[2],
                         [&] { return negate(workspace.direct[0]); }))
    return false;
  // O305
  if (!control.operation(304, 1, workspace.direct[5], [&] {
        return multiply(workspace.direct[2], workspace.chart[9]);
      }))
    return false;
  // O306
  if (!control.operation(305, 1, workspace.direct[5], [&] {
        return divide(workspace.direct[5], workspace.direct[3]);
      }))
    return false;
  // O307
  if (!control.operation(306, 1, workspace.direct[6],
                         [&] { return negate(workspace.direct[3]); }))
    return false;
  // O308
  if (!control.operation(307, 1, workspace.direct[8], [&] {
        return multiply(workspace.direct[4], workspace.chart[8]);
      }))
    return false;
  // O309
  if (!control.operation(308, 1, workspace.direct[9], [&] {
        return multiply(workspace.direct[4], workspace.direct[2]);
      }))
    return false;
  // O310
  if (!control.operation(309, 1, workspace.direct[10], [&] {
        return multiply(workspace.direct[4], workspace.chart[9]);
      }))
    return false;
  // O311
  if (!control.operation(310, 1, workspace.direct[11], [&] {
        return multiply(workspace.direct[7], workspace.direct[1]);
      }))
    return false;
  // O312
  if (!control.operation(311, 1, workspace.direct[12], [&] {
        return multiply(workspace.direct[7], workspace.direct[5]);
      }))
    return false;
  // O313
  if (!control.operation(312, 1, workspace.direct[13], [&] {
        return multiply(workspace.direct[7], workspace.direct[6]);
      }))
    return false;
  // O314
  if (!control.operation(313, 1, workspace.direct[14], [&] {
        return add(workspace.direct[8], workspace.direct[11]);
      }))
    return false;
  // O315
  if (!control.operation(314, 1, workspace.direct[15], [&] {
        return add(workspace.direct[9], workspace.direct[12]);
      }))
    return false;
  // O316
  if (!control.operation(315, 1, workspace.direct[16], [&] {
        return add(workspace.direct[10], workspace.direct[13]);
      }))
    return false;
  // O317
  if (!control.operation(316, 1, workspace.direct[1], [&] {
        return multiply(workspace.direct[14], point(1));
      }))
    return false;
  // O318
  if (!control.operation(317, 1, workspace.direct[2], [&] {
        return multiply(workspace.direct[15], point(0));
      }))
    return false;
  // O319
  if (!control.operation(318, 1, workspace.direct[1], [&] {
        return add(workspace.direct[1], workspace.direct[2]);
      }))
    return false;
  // O320
  if (!control.operation(319, 1, workspace.direct[3], [&] {
        return multiply(workspace.direct[16], point(0));
      }))
    return false;
  // O321
  if (!control.operation(320, 1, workspace.direct[4], [&] {
        return add(workspace.direct[1], workspace.direct[3]);
      }))
    return false;
  // O322
  if (!control.operation(321, 1, workspace.direct[1], [&] {
        return multiply(workspace.direct[14], point(0));
      }))
    return false;
  // O323
  if (!control.operation(322, 1, workspace.direct[2], [&] {
        return multiply(workspace.direct[15], point(1));
      }))
    return false;
  // O324
  if (!control.operation(323, 1, workspace.direct[1], [&] {
        return add(workspace.direct[1], workspace.direct[2]);
      }))
    return false;
  // O325
  if (!control.operation(324, 1, workspace.direct[3], [&] {
        return multiply(workspace.direct[16], point(0));
      }))
    return false;
  // O326
  if (!control.operation(325, 1, workspace.direct[7], [&] {
        return add(workspace.direct[1], workspace.direct[3]);
      }))
    return false;
  // O327
  if (!control.operation(326, 1, workspace.direct[1], [&] {
        return multiply(workspace.direct[14], point(0));
      }))
    return false;
  // O328
  if (!control.operation(327, 1, workspace.direct[2], [&] {
        return multiply(workspace.direct[15], point(0));
      }))
    return false;
  // O329
  if (!control.operation(328, 1, workspace.direct[1], [&] {
        return add(workspace.direct[1], workspace.direct[2]);
      }))
    return false;
  // O330
  if (!control.operation(329, 1, workspace.direct[3], [&] {
        return multiply(workspace.direct[16], point(1));
      }))
    return false;
  // O331
  if (!control.operation(330, 1, workspace.direct[0], [&] {
        return add(workspace.direct[1], workspace.direct[3]);
      }))
    return false;
  // G46
  if (!control.guard(
          45, 1,
          [&](EP5W& why) {
            if (!ep5_source_bit(d, 63) || kBoardingRouteFootPhaseVersion != 1 ||
                !intermediate_endpoint05_template_valid(request, false, 0) ||
                request.feet[1].yaw_half != std::array<double, 2>{0, 0}) {
              why = EP5W::slice_identity;
              return false;
            }
            for (const auto* v : {&workspace.direct[4], &workspace.direct[7],
                                  &workspace.direct[0]}) {
              if (!control.supported(why, *v)) return false;
              if (v->low < -16 || v->high > 16) {
                why = EP5W::unsupported_arithmetic;
                return control.attach(*v);
              }
            }
            return true;
          },
          Slice5Why::hip_verification_inconclusive))
    return false;
  // O332
  if (!control.operation(331, 1, workspace.direct[4], [&] {
        return ep5_unit_divide(workspace.direct[4], thigh_length);
      }))
    return false;
  // O333
  if (!control.operation(332, 1, workspace.direct[7], [&] {
        return ep5_unit_divide(workspace.direct[7], thigh_length);
      }))
    return false;
  // O334
  if (!control.operation(333, 1, workspace.direct[0], [&] {
        return ep5_unit_divide(workspace.direct[0], thigh_length);
      }))
    return false;
  // G47
  if (!control.guard(
          46, 1,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.direct[4]) ||
                !control.supported(why, workspace.direct[7]) ||
                !control.supported(why, workspace.direct[0]))
              return false;
            return workspace.direct[7].high < 0 ||
                   control.attach(workspace.direct[7]);
          },
          Slice5Why::hip_verification_inconclusive))
    return false;
  // O335
  if (!control.operation(334, 1, workspace.direct[1],
                         [&] { return square(workspace.direct[4]); }))
    return false;
  // O336
  if (!control.operation(335, 1, workspace.direct[2],
                         [&] { return square(workspace.direct[0]); }))
    return false;
  // O337
  if (!control.operation(336, 1, workspace.direct[1], [&] {
        return add(workspace.direct[1], workspace.direct[2]);
      }))
    return false;
  // O338
  if (!control.operation(337, 1, workspace.direct[1], [&] {
        return transfer_small_root(workspace.direct[1]);
      }))
    return false;
  // O339
  if (!control.operation(338, 1, workspace.direct[2], [&] {
        return multiply(point(kBoardingSelfHipLengthMetres),
                        workspace.direct[7]);
      }))
    return false;
  // O340
  if (!control.operation(339, 1, workspace.direct[3], [&] {
        return multiply(point(.105), workspace.direct[1]);
      }))
    return false;
  // O341
  if (!control.operation(340, 1, workspace.direct[14], [&] {
        return add(workspace.direct[2], workspace.direct[3]);
      }))
    return false;
  // O342
  if (!control.operation(341, 1, workspace.direct[15],
                         [&] { return negate(point(.12)); }))
    return false;
  // O343
  if (!control.operation(342, 1, workspace.direct[16], [&] {
        return subtract(workspace.direct[15], workspace.direct[14]);
      }))
    return false;
  // G48
  if (!control.guard(
          47, 1,
          [&](EP5W& why) {
            if (!control.supported(why, workspace.direct[1]) ||
                !control.supported(why, workspace.direct[14]) ||
                !control.supported(why, workspace.direct[15]) ||
                !control.supported(why, workspace.direct[16]))
              return false;
            return workspace.direct[14].high < workspace.direct[15].low ||
                   control.attach(workspace.direct[14]);
          },
          Slice5Why::hip_verification_inconclusive))
    return false;
  // G49
  if (!control.guard(
          48, 255,
          [&](EP5W&) {
            if (!intermediate_endpoint05_template_valid(request, false, 0))
              return false;
            for (auto& root : request.root)
              root.coordinates[1] = {{d.slice.y, +0.0, +0.0}, 1};
            return intermediate_endpoint05_template_valid(request, true,
                                                          d.slice.y);
          },
          Slice5Why::identity))
    return false;
  // G50
  if (!control.guard(
          49, 255,
          [&](EP5W& why) {
            if (!boarding_route_foot_phase_environment()) {
              why = EP5W::unsupported_arithmetic;
              return false;
            }
            if (!ep5_packet_structure(request) || d.version != 5 ||
                d.candidate != BoardingIntermediateEndpoint05Candidate::
                                   root_y_both_hip_ankle_reach_roll_slice ||
                !intermediate_endpoint05_template_valid(request, true,
                                                        d.slice.y))
              return false;
            for (const auto& root : request.root)
              if (std::bit_cast<std::uint64_t>(root.coordinates[1].terms[0]) !=
                  std::bit_cast<std::uint64_t>(d.slice.y))
                return false;
            if (!ep5_packet_domains(request)) {
              why = EP5W::unsupported_arithmetic;
              return false;
            }
            const auto valid = boarding_route_foot_phase_request_valid(request);
            if (!valid) {
              why = EP5W::unsupported_arithmetic;
              return false;
            }
            if (!boarding_route_foot_phase_environment()) {
              why = EP5W::unsupported_arithmetic;
              return false;
            }
            return true;
          },
          Slice5Why::identity))
    return false;
  d.slice.complete = d.slice.operation_written ==
                         std::array<std::uint64_t, 6>{
                             UINT64_MAX, UINT64_MAX, UINT64_MAX,
                             UINT64_MAX, UINT64_MAX, 0x00000000007fffffULL} &&
                     d.slice.guard_written == 0x0003ffffffffffffULL;
  d.slice.side = 255;
  return d.slice.complete;
}
} // namespace apsis_drift
