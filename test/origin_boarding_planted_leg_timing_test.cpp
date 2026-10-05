#include "apsis_drift/origin_boarding_planted_legs.hpp"
#include "origin_boarding_planted_legs_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>
#if defined(__SSE2__) && defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace {
using namespace apsis_drift;
std::size_t checks{};
int failures{};
auto check(bool value, std::string_view label) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T, class E> auto require(std::expected<T, E> value) -> T {
  if (!value)
    throw std::runtime_error("Required finite timing fixture API refused");
  return std::move(*value);
}
auto bits(double a, double b) -> bool {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
struct Point {
  long double x{}, y{}, z{};
};
auto add(Point a, Point b) -> Point {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(Point a, Point b) -> Point {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(Point p, long double s) -> Point {
  return {p.x * s, p.y * s, p.z * s};
}
auto dot(Point a, Point b) -> long double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
struct Motion {
  Point value, first, second;
};
struct AngleMotion {
  long double first{}, second{};
};
struct Oracle {
  Motion hip, knee, ankle, boot;
  AngleMotion hip_pitch, shin_pitch, knee_pitch, roll;
};
// Independent explicit unfactored sphere-intersection derivatives, not a copy
// of the production interval triples or a derivative of rounded report points.
// All inputs below are the registered stored binary64 values promoted to LD.
auto analytic(Motion H, Motion A, Motion B) -> Oracle {
  const auto d = sub(A.value, H.value), d1 = scale(H.first, -1),
             d2 = scale(H.second, -1);
  const auto D = dot(d, d), D1 = 2 * dot(d, d1),
             D2 = 2 * (dot(d1, d1) + dot(d, d2));
  const auto R = d.x * d.x + d.y * d.y, R1 = 2 * (d.x * d1.x + d.y * d1.y),
             R2 = 2 * (d1.x * d1.x + d.x * d2.x + d1.y * d1.y + d.y * d2.y);
  const auto rho = std::sqrt(R), rho1 = R1 / (2 * rho),
             rho2 = R2 / (2 * rho) - R1 * R1 / (4 * rho * rho * rho);
  const auto L1 = static_cast<long double>(.47285),
             L2 = static_cast<long double>(.47478), c = L1 * L1 - L2 * L2;
  const auto alpha = .5L + c / (2 * D), alpha1 = -c * D1 / (2 * D * D),
             alpha2 = c * D1 * D1 / (D * D * D) - c * D2 / (2 * D * D);
  const auto u = L1 * L1 / D - alpha * alpha,
             u1 = -L1 * L1 * D1 / (D * D) - 2 * alpha * alpha1,
             u2 = 2 * L1 * L1 * D1 * D1 / (D * D * D) - L1 * L1 * D2 / (D * D) -
                  2 * (alpha1 * alpha1 + alpha * alpha2);
  if (!(D > 0 && rho > 0 && u > 0))
    throw std::runtime_error("Registered derivative oracle singular domain");
  const auto gamma = std::sqrt(u), gamma1 = u1 / (2 * gamma),
             gamma2 = u2 / (2 * gamma) - u1 * u1 / (4 * gamma * gamma * gamma);
  Motion q;
  const auto quotient = [&](long double p, long double p1, long double p2) {
    return std::array{p / rho, p1 / rho - p * rho1 / (rho * rho),
                      p2 / rho - 2 * p1 * rho1 / (rho * rho) -
                          p * rho2 / (rho * rho) +
                          2 * p * rho1 * rho1 / (rho * rho * rho)};
  };
  const auto qx = quotient(d.x * d.z, d1.x * d.z + d.x * d1.z,
                           d2.x * d.z + 2 * d1.x * d1.z + d.x * d2.z);
  const auto qy = quotient(d.y * d.z, d1.y * d.z + d.y * d1.z,
                           d2.y * d.z + 2 * d1.y * d1.z + d.y * d2.z);
  q = {{qx[0], qy[0], -rho}, {qx[1], qy[1], -rho1}, {qx[2], qy[2], -rho2}};
  const Motion K{
      add(H.value, add(scale(d, alpha), scale(q.value, gamma))),
      add(H.first, add(add(scale(d, alpha1), scale(d1, alpha)),
                       add(scale(q.value, gamma1), scale(q.first, gamma)))),
      add(H.second,
          add(add(add(scale(d, alpha2), scale(d1, 2 * alpha1)),
                  scale(d2, alpha)),
              add(add(scale(q.value, gamma2), scale(q.first, 2 * gamma1)),
                  scale(q.second, gamma))))};
  const Point xy{d.x, d.y, 0}, xy1{d1.x, d1.y, 0}, xy2{d2.x, d2.y, 0};
  const auto e = scale(xy, 1 / rho),
             e1 = sub(scale(xy1, 1 / rho), scale(xy, rho1 / (rho * rho))),
             e2 = add(sub(sub(scale(xy2, 1 / rho),
                              scale(xy1, 2 * rho1 / (rho * rho))),
                          scale(xy, rho2 / (rho * rho))),
                      scale(xy, 2 * rho1 * rho1 / (rho * rho * rho)));
  const Motion thigh{sub(K.value, H.value), sub(K.first, H.first),
                     sub(K.second, H.second)},
      shin{sub(A.value, K.value), scale(K.first, -1), scale(K.second, -1)};
  const auto angle = [&](const Motion& v) {
    const auto F = dot(v.value, e), G = -v.value.z,
               F1 = dot(v.first, e) + dot(v.value, e1), G1 = -v.first.z,
               F2 = dot(v.second, e) + 2 * dot(v.first, e1) + dot(v.value, e2),
               G2 = -v.second.z;
    const auto norm = F * F + G * G, norm1 = 2 * (F * F1 + G * G1),
               cross = F * G1 - G * F1;
    return AngleMotion{cross / norm, (F * G2 - G * F2) / norm -
                                         cross * norm1 / (norm * norm)};
  };
  const auto hip = angle(thigh), shin_angle = angle(shin);
  const AngleMotion knee{hip.first - shin_angle.first,
                         hip.second - shin_angle.second};
  // Distinct acos-of-distance path cross-checks both knee derivatives away
  // from the singular straight/folded branch; no production F/G is borrowed.
  const auto C = (D - L1 * L1 - L2 * L2) / (2 * L1 * L2),
             C1 = D1 / (2 * L1 * L2), C2 = D2 / (2 * L1 * L2), w = 1 - C * C;
  const auto acos1 = -C1 / std::sqrt(w),
             acos2 = -C2 / std::sqrt(w) - C * C1 * C1 / (w * std::sqrt(w));
  const auto error = 65536 * std::numeric_limits<long double>::epsilon();
  check(std::abs(knee.first - acos1) <=
                error * std::max(1.L, std::abs(acos1)) &&
            std::abs(knee.second - acos2) <=
                error * std::max(1.L, std::abs(acos2)),
        "Independent segment-angle and cosine-distance knee derivatives agree");
  for (const auto& v : std::array{thigh, shin})
    check(std::abs(dot(v.value, v.first)) <= error &&
              std::abs(dot(v.first, v.first) + dot(v.value, v.second)) <= error,
          "Differentiated fixed Euclidean links corroborate zero length "
          "variation");
  // Alternate atan(dx/-dy) quotient rather than production B/rho² graph.
  const auto denom = -d.y, denom1 = -d1.y, denom2 = -d2.y;
  const auto r = d.x / denom,
             r1 = d1.x / denom - d.x * denom1 / (denom * denom),
             r2 = d2.x / denom - 2 * d1.x * denom1 / (denom * denom) -
                  d.x * denom2 / (denom * denom) +
                  2 * d.x * denom1 * denom1 / (denom * denom * denom);
  const auto z = 1 + r * r;
  return {H,   K,          A,    B,
          hip, shin_angle, knee, {r1 / z, r2 / z - 2 * r * r1 * r1 / (z * z)}};
}

auto oracle(double parameter, std::size_t side) -> Oracle {
  const auto t = static_cast<long double>(parameter),
             a = static_cast<long double>(.32);
  const auto x = a * t * t * (3 - 2 * t), x1 = a * 6 * t * (1 - t),
             x2 = a * (6 - 12 * t);
  const auto plane = side == 0 ? 0.L : static_cast<long double>(-.16);
  const Motion H{{x + (side == 0 ? -static_cast<long double>(.14)
                                 : static_cast<long double>(.14)),
                  static_cast<long double>(.72),
                  static_cast<long double>(-.16)},
                 {x1, 0, 0},
                 {x2, 0, 0}};
  const Motion A{{static_cast<long double>(side == 0 ? .02 : .34),
                  plane + static_cast<long double>(.1),
                  static_cast<long double>(side == 0 ? -.35 : -.65)},
                 {},
                 {}};
  auto B = A;
  B.value.y = plane + static_cast<long double>(.05);
  return analytic(H, A, B);
}

auto component(RigidVector3 p, std::size_t axis) -> double {
  return axis == 0 ? p.x : (axis == 1 ? p.y : p.z);
}
auto component(Point p, std::size_t axis) -> long double {
  return axis == 0 ? p.x : (axis == 1 ? p.y : p.z);
}
auto valid(BoardingPlantedLegScalarBounds b) -> bool {
  return std::isfinite(b.lower) && std::isfinite(b.upper) && b.lower <= b.upper;
}
auto contains(BoardingPlantedLegScalarBounds b, long double value) -> void {
  // Only corroborating a sampled analytic oracle, never granting a leaf. This
  // allowance bounds LD oracle arithmetic; production bounds remain unchanged.
  const auto error = 8192 * std::numeric_limits<long double>::epsilon() *
                     std::max(1.L, std::abs(value));
  check(valid(b) && static_cast<long double>(b.lower) <= value + error &&
            value - error <= static_cast<long double>(b.upper),
        "Sampled independent derivative belongs to outward enclosure");
}
auto point_contains(const BoardingPlantedLegPointBounds& b, Point value)
    -> void {
  for (std::size_t axis = 0; axis < 3; ++axis)
    contains({component(b.lower, axis), component(b.upper, axis)},
             component(value, axis));
}
auto scalar_equal(BoardingPlantedLegScalarBounds a,
                  BoardingPlantedLegScalarBounds b) -> bool {
  return bits(a.lower, b.lower) && bits(a.upper, b.upper);
}
auto reverse_scalar(BoardingPlantedLegScalarBounds a,
                    BoardingPlantedLegScalarBounds b) -> void {
  check(a.lower == -b.upper && a.upper == -b.lower,
        "Reverse negates only first derivative enclosure including zero");
}
auto point_reverse(const BoardingPlantedLegPointDerivatives& a,
                   const BoardingPlantedLegPointDerivatives& b) -> void {
  for (std::size_t axis = 0; axis < 3; ++axis) {
    reverse_scalar(
        {component(a.velocity.lower, axis), component(a.velocity.upper, axis)},
        {component(b.velocity.lower, axis), component(b.velocity.upper, axis)});
    check(bits(component(a.acceleration.lower, axis),
               component(b.acceleration.lower, axis)) &&
              bits(component(a.acceleration.upper, axis),
                   component(b.acceleration.upper, axis)),
          "Reverse preserves complete second derivative bounds");
  }
}
auto angular_contains(const BoardingPlantedLegAngularDerivatives& e,
                      AngleMotion a, bool reverse, double sign = 1) -> void {
  contains(e.rate, a.first * (reverse ? -sign : sign) / 12);
  contains(e.coordinate_second, a.second * sign / 144);
}
auto motion_contains(const BoardingPlantedLegPointDerivatives& e, Motion m,
                     bool reverse) -> void {
  point_contains(e.velocity, scale(m.first, (reverse ? -1.L : 1.L) / 12));
  point_contains(e.acceleration, scale(m.second, 1.L / 144));
}
auto zero(const BoardingPlantedLegPointDerivatives& p) -> void {
  for (const auto b : std::array{p.velocity, p.acceleration})
    for (std::size_t axis = 0; axis < 3; ++axis)
      check(component(b.lower, axis) == 0 && component(b.upper, axis) == 0,
            "Planted ankle/boot has exact zero physical first and second "
            "derivatives");
}
auto snapshot(const BoardingPlantedLegDiagnostic& d)
    -> std::vector<std::uint64_t> {
  std::vector<std::uint64_t> result;
  const auto number = [&](double x) {
    result.push_back(std::bit_cast<std::uint64_t>(x));
  };
  const auto integer = [&](auto x) {
    result.push_back(static_cast<std::uint64_t>(x));
  };
  const auto scalar = [&](BoardingPlantedLegScalarBounds b) {
    number(b.lower);
    number(b.upper);
  };
  const auto point = [&](BoardingPlantedLegPointBounds b) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      number(component(b.lower, axis));
      number(component(b.upper, axis));
    }
  };
  number(d.requested_first);
  number(d.requested_last);
  integer(d.reverse);
  integer(d.recipe_version);
  number(d.duration_seconds);
  number(d.placement_y_metres);
  integer(d.complete);
  integer(d.plane_identities);
  integer(d.link_identities);
  integer(d.joint_limits_certified);
  integer(d.examined_nodes);
  integer(d.maximum_depth);
  integer(d.leaves.size());
  for (const auto& leaf : d.leaves) {
    number(leaf.first);
    number(leaf.last);
    scalar(leaf.root_x);
    for (const auto& e : leaf.legs) {
      for (const auto b : std::array{e.hip, e.knee, e.ankle, e.boot_center})
        point(b);
      for (const auto b :
           std::array{e.distance_squared, e.rho, e.alpha, e.gamma, e.hip_sine,
                      e.hip_cosine, e.shin_sine, e.shin_cosine, e.knee_cosine,
                      e.roll_sine, e.roll_cosine})
        scalar(b);
      for (const auto x : e.ankle_y_terms)
        number(x);
      for (const auto x : e.boot_center_y_terms)
        number(x);
      number(e.plane_metres);
      number(e.reporting_hip_flex_degrees);
      number(e.reporting_knee_flex_degrees);
      number(e.reporting_ankle_pitch_degrees);
      number(e.reporting_hip_abduction_degrees);
      integer(e.plane_identity);
      integer(e.link_identities);
      integer(e.limits_certified);
      integer(e.limiting_condition);
    }
  }
  integer(d.first_refusal.has_value());
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    number(r.first);
    number(r.last);
    integer(r.side);
    integer(r.depth);
    integer(r.condition);
    integer(r.limiting_condition);
  }
  return result;
}
auto inspect(double first, double last) -> BoardingPlantedLegTimingDiagnostic {
  auto d = require(assess_origin_boarding_planted_legs_timing(first, last));
  const auto old = require(assess_origin_boarding_planted_legs(first, last));
  check(snapshot(d.closure) == snapshot(old),
        "Timing owns bit-for-bit original independently bounded #425 closure");
  check(d.closure.complete,
        "Registered unchanged closure prerequisite stays complete");
  check(d.timing_version == 1 && bits(d.requested_first, first) &&
            bits(d.requested_last, last) && d.reverse == (last < first) &&
            bits(d.seconds_per_parameter, 12),
        "Original parameter/clock/version retained");
  check(bits(d.reporting_elapsed_seconds, 12 * std::abs(last - first)),
        "Subinterval duration uses original clock, never retiming");
  check(d.examined_nodes <= 8191 && d.maximum_depth <= 12 &&
            d.leaves.size() <= 4096,
        "Timing cover obeys separate frozen capacity");
  check(!d.dynamics_qualified && !d.full_body_com_qualified &&
            !d.self_qualified && !d.load_qualified && !d.world_qualified &&
            !d.route_qualified && !d.actor_qualified && !d.seat_qualified &&
            !d.save_qualified && !d.free_foot_swing_qualified,
        "Leg/root timing conveys no full body or gameplay authority");
  auto edge = std::min(first, last);
  const auto high = std::max(first, last);
  for (const auto& leaf : d.leaves) {
    check(bits(leaf.first, edge) && leaf.first <= leaf.last &&
              leaf.last <= high,
          "Accepted timing leaves form ordered exact no-gap/no-overlap cover");
    edge = leaf.last;
    check(valid(leaf.root_speed) && leaf.root_speed.lower >= 0 &&
              leaf.root_speed.upper <= .25 && valid(leaf.root_acceleration) &&
              leaf.root_acceleration.lower >= 0 &&
              leaf.root_acceleration.upper <= .10,
          "Root applicable speed/acceleration certify whole-leaf bounds");
    for (const auto fraction : std::array{0., .25, .5, .75, 1.}) {
      const auto t = leaf.first + (leaf.last - leaf.first) * fraction;
      const auto o0 = oracle(t, 0);
      motion_contains(leaf.root, o0.hip, d.reverse);
      contains(leaf.root_speed, std::abs(o0.hip.first.x) / 12);
      contains(leaf.root_acceleration, std::abs(o0.hip.second.x) / 144);
      for (std::size_t side = 0; side < 2; ++side) {
        const auto o = side == 0 ? o0 : oracle(t, side);
        const auto& e = leaf.legs[side];
        check(e.derivative_domains_certified && e.joint_speed_certified &&
                  e.closure.limits_certified &&
                  e.limiting_condition ==
                      BoardingPlantedLegTimingCondition::none,
              "Accepted derivative leaf independently establishes "
              "domain/closure/joint limits");
        motion_contains(e.hip, o.hip, d.reverse);
        motion_contains(e.knee, o.knee, d.reverse);
        motion_contains(e.ankle, o.ankle, d.reverse);
        motion_contains(e.boot_center, o.boot, d.reverse);
        angular_contains(e.hip_pitch, o.hip_pitch, d.reverse);
        angular_contains(e.shin_pitch, o.shin_pitch, d.reverse);
        angular_contains(e.knee_flex, o.knee_pitch, d.reverse);
        angular_contains(e.ankle_pitch, o.shin_pitch, d.reverse, -1);
        angular_contains(e.hip_abduction, o.roll, d.reverse,
                         side == 0 ? -1 : 1);
        angular_contains(e.ankle_roll, o.roll, d.reverse, -1);
        for (const auto speed :
             std::array{e.hip_speed, e.knee_speed, e.ankle_speed})
          check(speed.supported && speed.certified &&
                    valid(speed.speed_radians_per_second) &&
                    speed.speed_radians_per_second.lower >= 0,
                "Three physical relative joint resultant speeds are "
                "bounded/certified");
        contains(e.hip_speed.speed_radians_per_second,
                 std::hypot(o.roll.first, o.hip_pitch.first) / 12);
        contains(e.knee_speed.speed_radians_per_second,
                 std::abs(o.knee_pitch.first) / 12);
        contains(e.ankle_speed.speed_radians_per_second,
                 std::hypot(o.roll.first, o.shin_pitch.first) / 12);
        zero(e.ankle);
        zero(e.boot_center);
      }
    }
  }
  if (d.complete) {
    check(
        !d.first_refusal && !d.leaves.empty() && bits(edge, high) &&
            d.derivative_domains_certified && d.root_speed_certified &&
            d.root_acceleration_certified && d.leg_joint_speed_certified &&
            d.examined_nodes == 2 * d.leaves.size() - 1,
        "Complete timing success requires whole cover and complete predicates");
  } else {
    check(d.first_refusal && !d.derivative_domains_certified &&
              !d.root_speed_certified && !d.root_acceleration_certified &&
              !d.leg_joint_speed_certified,
          "Refused prefix cannot claim complete derivative/root/joint timing");
    if (d.first_refusal) {
      const auto& r = *d.first_refusal;
      check(bits(r.first, edge) && r.first <= r.last && r.last <= high &&
                r.side < 2 && r.depth <= 12 &&
                r.condition != BoardingPlantedLegTimingCondition::none,
            "First timing refusal identifies exact first uncovered interval "
            "and bounded cause");
    }
  }
  std::cout << "timing " << first << " -> " << last
            << " complete=" << d.complete << " leaves=" << d.leaves.size()
            << " nodes=" << d.examined_nodes << " depth=" << d.maximum_depth;
  if (d.first_refusal)
    std::cout << " refusal=" << static_cast<int>(d.first_refusal->condition)
              << " limiting="
              << static_cast<int>(d.first_refusal->limiting_condition)
              << " side=" << d.first_refusal->side
              << " joint=" << static_cast<int>(d.first_refusal->joint);
  std::cout << '\n';
  return d;
}
auto reverses(const BoardingPlantedLegTimingDiagnostic& a,
              const BoardingPlantedLegTimingDiagnostic& b) -> void {
  check(a.complete == b.complete && a.leaves.size() == b.leaves.size() &&
            a.examined_nodes == b.examined_nodes &&
            a.maximum_depth == b.maximum_depth,
        "Reverse keeps identical geometric cover and bounded counters");
  for (std::size_t i = 0; i < std::min(a.leaves.size(), b.leaves.size()); ++i) {
    const auto& x = a.leaves[i];
    const auto& y = b.leaves[i];
    check(bits(x.first, y.first) && bits(x.last, y.last) &&
              scalar_equal(x.root_speed, y.root_speed) &&
              scalar_equal(x.root_acceleration, y.root_acceleration),
          "Reverse preserves spatial leaf and unsigned root magnitudes");
    point_reverse(x.root, y.root);
    for (std::size_t side = 0; side < 2; ++side) {
      const auto& e = x.legs[side];
      const auto& f = y.legs[side];
      const std::array left{e.hip, e.knee, e.ankle, e.boot_center},
          right{f.hip, f.knee, f.ankle, f.boot_center};
      for (std::size_t j = 0; j < left.size(); ++j)
        point_reverse(left[j], right[j]);
      const std::array aa{e.hip_pitch,   e.shin_pitch,    e.knee_flex,
                          e.ankle_pitch, e.hip_abduction, e.ankle_roll},
          bb{f.hip_pitch,   f.shin_pitch,    f.knee_flex,
             f.ankle_pitch, f.hip_abduction, f.ankle_roll};
      for (std::size_t j = 0; j < aa.size(); ++j) {
        reverse_scalar(aa[j].rate, bb[j].rate);
        check(scalar_equal(aa[j].coordinate_second, bb[j].coordinate_second),
              "Reverse preserves angular coordinate second derivative");
      }
      check(scalar_equal(e.hip_speed.speed_radians_per_second,
                         f.hip_speed.speed_radians_per_second) &&
                scalar_equal(e.knee_speed.speed_radians_per_second,
                             f.knee_speed.speed_radians_per_second) &&
                scalar_equal(e.ankle_speed.speed_radians_per_second,
                             f.ankle_speed.speed_radians_per_second),
            "Physical resultant speeds are traversal-direction invariant");
    }
  }
}
auto public_controls() -> void {
  const auto full = inspect(0, 1), reverse = inspect(1, 0);
  check(full.complete && reverse.complete,
        "Observed unchanged full/reverse recipe retains complete timing "
        "certification");
  reverses(full, reverse);
  for (const auto& interval :
       std::array{std::pair{0., .5}, std::pair{.5, 1.}, std::pair{.25, .5},
                  std::pair{.125, .875}}) {
    const auto forward = inspect(interval.first, interval.second),
               backward = inspect(interval.second, interval.first);
    check(forward.complete && backward.complete,
          "Observed fixed subintervals retain complete timing on the original "
          "clock");
    reverses(forward, backward);
  }
  for (const auto t : std::array{0., .25, .5, .75, 1.}) {
    const auto point = inspect(t, t);
    check(point.complete,
          "Observed fixed point queries retain certified instantaneous timing");
    if (point.complete) {
      check(!point.reverse && point.leaves.size() == 1 &&
                point.examined_nodes == 1,
            "Point query reports instantaneous original curve derivatives, not "
            "a zero-duration pause");
      const auto& root = point.leaves[0].root;
      if (t == .5)
        check(root.velocity.lower.x > 0,
              "Midcurve point keeps nonzero instantaneous root velocity");
      if (t == 0 || t == 1)
        check(root.velocity.lower.x == 0 && root.velocity.upper.x == 0 &&
                  (root.acceleration.lower.x > 0 ||
                   root.acceleration.upper.x < 0),
              "Endpoint zero speed retains nonzero curve acceleration; no C2 "
              "hold invented");
    }
  }
  const auto nan = std::numeric_limits<double>::quiet_NaN(),
             inf = std::numeric_limits<double>::infinity();
  for (const auto bad : std::array{nan, inf, -inf, std::nextafter(0., -inf),
                                   std::nextafter(1., inf)}) {
    check(!assess_origin_boarding_planted_legs_timing(bad, .5) &&
              !assess_origin_boarding_planted_legs_timing(.5, bad),
          "Invalid endpoint refuses before a timing diagnostic");
  }
}
auto speed_controls() -> void {
  const auto limit = detail::boarding_planted_leg_speed_threshold();
  const auto exact_limit = std::numbers::pi_v<long double> / 6;
  check(valid(limit) && static_cast<long double>(limit.lower) <= exact_limit &&
            exact_limit <= static_cast<long double>(limit.upper),
        "Independent pi/6 oracle lies in compiled certified threshold bracket");
  for (const auto& signs :
       std::array{std::pair{1., 1.}, std::pair{-1., 1.}, std::pair{1., -1.},
                  std::pair{-1., -1.}}) {
    const auto good = detail::boarding_planted_leg_joint_speed(
        {signs.first * .2, signs.first * .2},
        {signs.second * .2, signs.second * .2});
    check(good.supported && good.certified,
          "Small diagonal physical resultant speed is certified");
    contains(good.speed_radians_per_second,
             std::sqrt(2.L) * static_cast<long double>(.2));
    const auto bad = detail::boarding_planted_leg_joint_speed(
        {signs.first * .4, signs.first * .4},
        {signs.second * .4, signs.second * .4});
    check(.4 < limit.lower && bad.supported && !bad.certified &&
              bad.speed_radians_per_second.lower > limit.upper,
          "Two individually sublimit components cannot masquerade as an "
          "admissible resultant joint speed");
    contains(bad.speed_radians_per_second,
             std::sqrt(2.L) * static_cast<long double>(.4));
  }
  const auto crossing =
      detail::boarding_planted_leg_joint_speed({-.2, .2}, {-.2, .2});
  check(
      crossing.supported && crossing.certified &&
          crossing.speed_radians_per_second.lower == 0,
      "Signed crossing-zero components produce nonnegative bounded resultant");
  const auto below = std::nextafter(limit.lower, 0.),
             above = std::nextafter(limit.upper,
                                    std::numeric_limits<double>::infinity());
  const auto lower = detail::boarding_planted_leg_joint_speed({0, 0},
                                                              {below, below}),
             upper = detail::boarding_planted_leg_joint_speed({0, 0},
                                                              {above, above});
  check(lower.supported && upper.supported && !upper.certified,
        "Above threshold nextafter refuses; below-neighbor may conservatively "
        "remain uncertain");
  if (lower.certified)
    check(static_cast<long double>(below) < exact_limit,
          "Any accepted threshold neighbor is independently strictly within "
          "physical pi/6 limit");
  const auto nan = std::numeric_limits<double>::quiet_NaN(),
             inf = std::numeric_limits<double>::infinity();
  for (const auto bad : std::array{BoardingPlantedLegScalarBounds{nan, 0},
                                   BoardingPlantedLegScalarBounds{0, inf},
                                   BoardingPlantedLegScalarBounds{1, 0},
                                   BoardingPlantedLegScalarBounds{-inf, 0},
                                   BoardingPlantedLegScalarBounds{
                                       std::numeric_limits<double>::max(),
                                       std::numeric_limits<double>::max()}}) {
    const auto d = detail::boarding_planted_leg_joint_speed(bad, {0, 0});
    check(!d.supported && !d.certified,
          "Invalid/overflowing resultant input grants no speed certificate");
  }
}
auto numeric(RigidVector3 hip = {.02, .72, -.16}, RigidVector3 first = {},
             RigidVector3 second = {}, double ankle_x = .02,
             double ankle_z = -.35, double plane = 0, double l1 = .47285,
             double l2 = .47478, bool branch = true, bool reverse = false,
             std::size_t side = 0) -> BoardingPlantedLegTimingEvidence {
  return detail::boarding_planted_leg_timing_numeric(
      hip, first, second, ankle_x, ankle_z, plane, l1, l2, branch, reverse,
      side);
}
auto numeric_controls() -> void {
  const auto still = numeric();
  check(still.closure.limits_certified && still.derivative_domains_certified &&
            still.joint_speed_certified,
        "Genuine fixed reachable numeric input establishes closure and "
        "zero-rate derivative domain");
  for (const auto& p :
       std::array{still.hip, still.knee, still.ankle, still.boot_center})
    zero(p);
  for (const auto angle :
       std::array{still.hip_pitch, still.shin_pitch, still.knee_flex,
                  still.ankle_pitch, still.hip_abduction, still.ankle_roll})
    check(angle.rate.lower == 0 && angle.rate.upper == 0 &&
              angle.coordinate_second.lower == 0 &&
              angle.coordinate_second.upper == 0,
          "Constant hip/ankle expression gives structural zero derivatives "
          "without a pause fiction");
  const auto moving = numeric({.02, .72, -.16}, {.48, 0, 0}, {.2, 0, 0}),
             reverse = numeric({.02, .72, -.16}, {.48, 0, 0}, {.2, 0, 0}, .02,
                               -.35, 0, .47285, .47478, true, true);
  if (!moving.joint_speed_certified || !reverse.joint_speed_certified) {
    std::cerr << "Moving private diagnostic: condition="
              << static_cast<int>(moving.limiting_condition)
              << " joint=" << static_cast<int>(moving.limiting_joint)
              << " derivative=" << moving.derivative_domains_certified
              << " speeds-supported=" << moving.hip_speed.supported << ','
              << moving.knee_speed.supported << ','
              << moving.ankle_speed.supported
              << " knee-rate=" << moving.knee_flex.rate.lower << ':'
              << moving.knee_flex.rate.upper << '\n';
  }
  check(moving.derivative_domains_certified && moving.joint_speed_certified &&
            reverse.joint_speed_certified,
        "Finite private moving hip keeps genuine derivative domain and speed "
        "certificate");
  for (const auto* e : std::array{&moving, &reverse}) {
    check(e->knee_flex.rate.lower == 0 && e->knee_flex.rate.upper == 0 &&
              e->knee_speed.supported && e->knee_speed.certified &&
              e->knee_speed.speed_radians_per_second.lower == 0 &&
              e->knee_speed.speed_radians_per_second.upper == 0,
          "Pure lateral tangent has exact zero first knee rate and resultant "
          "speed");
    check(
        (e->hip_abduction.rate.lower > 0 || e->hip_abduction.rate.upper < 0) &&
            (e->hip.velocity.lower.x > 0 || e->hip.velocity.upper.x < 0),
        "Exact zero knee rate preserves genuine nonzero roll and hip velocity");
  }
  contains({moving.hip.velocity.lower.x, moving.hip.velocity.upper.x},
           static_cast<long double>(.48) / 12);
  contains({moving.hip.acceleration.lower.x, moving.hip.acceleration.upper.x},
           static_cast<long double>(.2) / 144);
  point_reverse(moving.hip, reverse.hip);
  point_reverse(moving.knee, reverse.knee);
  const Motion general_hip{
      {static_cast<long double>(.02), static_cast<long double>(.72),
       static_cast<long double>(-.16)},
      {static_cast<long double>(.2), static_cast<long double>(-.01),
       static_cast<long double>(.02)},
      {static_cast<long double>(-.03), static_cast<long double>(.015),
       static_cast<long double>(-.02)}};
  const Motion general_ankle{{static_cast<long double>(.02),
                              static_cast<long double>(.1),
                              static_cast<long double>(-.35)},
                             {},
                             {}},
      general_boot{{static_cast<long double>(.02),
                    static_cast<long double>(.05),
                    static_cast<long double>(-.35)},
                   {},
                   {}};
  const auto independent = analytic(general_hip, general_ankle, general_boot);
  for (const auto direction : std::array{false, true})
    for (const auto side : std::array<std::size_t, 2>{0, 1}) {
      const auto jet =
          numeric({.02, .72, -.16}, {.2, -.01, .02}, {-.03, .015, -.02}, .02,
                  -.35, 0, .47285, .47478, true, direction, side);
      check(jet.derivative_domains_certified && jet.joint_speed_certified,
            "Finite three-dimensional private jet establishes actual "
            "derivative domain");
      motion_contains(jet.hip, independent.hip, direction);
      motion_contains(jet.knee, independent.knee, direction);
      angular_contains(jet.hip_pitch, independent.hip_pitch, direction);
      angular_contains(jet.shin_pitch, independent.shin_pitch, direction);
      angular_contains(jet.knee_flex, independent.knee_pitch, direction);
      angular_contains(jet.ankle_pitch, independent.shin_pitch, direction, -1);
      angular_contains(jet.hip_abduction, independent.roll, direction,
                       side == 0 ? -1 : 1);
      angular_contains(jet.ankle_roll, independent.roll, direction, -1);
      contains(jet.hip_speed.speed_radians_per_second,
               std::hypot(independent.roll.first, independent.hip_pitch.first) /
                   12);
      contains(jet.knee_speed.speed_radians_per_second,
               std::abs(independent.knee_pitch.first) / 12);
      contains(
          jet.ankle_speed.speed_radians_per_second,
          std::hypot(independent.roll.first, independent.shin_pitch.first) /
              12);
    }
  const auto large = numeric({.02, .72, -.16}, {4096, 0, 0});
  check(large.derivative_domains_certified && !large.joint_speed_certified &&
            large.limiting_condition ==
                BoardingPlantedLegTimingCondition::joint_speed &&
            large.limiting_joint != BoardingPlantedLegJoint::none,
        "Within-workspace genuine large hip derivative refuses resultant joint "
        "speed without inventing a domain failure");
  const auto nan = std::numeric_limits<double>::quiet_NaN(),
             inf = std::numeric_limits<double>::infinity();
  for (const auto bad :
       std::array{nan, inf, -inf, std::nextafter(4096., inf)}) {
    const auto d = numeric({.02, .72, -.16}, {bad, 0, 0});
    check(!d.derivative_domains_certified && !d.joint_speed_certified,
          "Nonfinite/outside fixed derivative workspace refuses before "
          "derivative certification");
    const auto e = numeric({.02, .72, -.16}, {}, {bad, 0, 0});
    check(!e.derivative_domains_certified && !e.joint_speed_certified,
          "Second derivative workspace bound independently enforced");
  }
  for (const auto bad : std::array{nan, inf, std::nextafter(8., inf)}) {
    const auto d = numeric({bad, .72, -.16});
    check(!d.derivative_domains_certified && !d.joint_speed_certified,
          "World hip coordinate workspace/nonfinite guard enforced");
  }
  for (const auto& d :
       std::array{numeric({.02, .72, -.16}, {}, {}, .02, -.35, 0, .47285,
                          .47478, false),
                  numeric({.02, .1, -.16}, {}, {}, .02, -.4, 0),
                  numeric({.02, .72, -.16}, {}, {}, .02, -.35, -2.1),
                  numeric({.02, .72, -.16}, {}, {}, .02, -.35, 0, 0, .47478),
                  numeric({.02, .72, -.16}, {}, {}, .02, -.35, 0, .47285,
                          .47478, true, false, 2)})
    check(!d.derivative_domains_certified && !d.joint_speed_certified &&
              d.limiting_condition != BoardingPlantedLegTimingCondition::none,
          "Closure branch/reach/singular/length/side refusals cannot acquire "
          "timing authority");
  // Straight reach is not forced through a conservative prerequisite. A closure
  // refusal is honest and remains distinct from an established singular jet.
  const auto straight =
      numeric({0, .72, 0}, {.1, 0, 0}, {}, 0, 0, .72 - .1 - .47285 - .47478);
  check(!straight.joint_speed_certified,
        "Exact straight/uncertain reach never grants derivative timing across "
        "zero gamma");
}
auto bounded_controls() -> void {
  for (const auto cap : std::array{std::array<std::size_t, 3>{0, 0, 0},
                                   std::array<std::size_t, 3>{0, 1, 0},
                                   std::array<std::size_t, 3>{0, 1, 1},
                                   std::array<std::size_t, 3>{12, 1, 4096},
                                   std::array<std::size_t, 3>{12, 8191, 1}}) {
    const auto d = require(detail::boarding_planted_legs_timing_bounded(
        0, 1, cap[0], cap[1], cap[2]));
    check(d.closure.complete &&
              snapshot(d.closure) ==
                  snapshot(require(assess_origin_boarding_planted_legs())),
          "Reduced timing budget does not reset/truncate separate original "
          "closure budget");
    check(d.examined_nodes <= cap[1] && d.maximum_depth <= cap[0] &&
              d.leaves.size() <= cap[2],
          "Reduced timing capacities enforce exact admission bounds");
    if (!d.complete)
      check(d.first_refusal && !d.leg_joint_speed_certified,
            "Capacity-limited prefix preserves first refusal and incomplete "
            "summary");
  }
  for (const auto cap : std::array{std::array<std::size_t, 3>{13, 8191, 4096},
                                   std::array<std::size_t, 3>{12, 8192, 4096},
                                   std::array<std::size_t, 3>{12, 8191, 4097}})
    check(!detail::boarding_planted_legs_timing_bounded(0, 1, cap[0], cap[1],
                                                        cap[2]),
          "Caller cannot enlarge any registered timing capacity");
  const auto adjacent = require(
      assess_origin_boarding_planted_legs_timing(.5, std::nextafter(.5, 1.)));
  check(adjacent.complete || adjacent.first_refusal.has_value(),
        "Adjacent representable parameter interval certifies or owns truthful "
        "bounded refusal");
}
struct SavedEnvironment {
  std::fenv_t original{};
  SavedEnvironment() {
    if (std::fegetenv(&original) != 0)
      throw std::runtime_error("Cannot save floating environment");
  }
  SavedEnvironment(const SavedEnvironment&) = delete;
  auto operator=(const SavedEnvironment&) -> SavedEnvironment& = delete;
  ~SavedEnvironment() {
    check(std::fesetenv(&original) == 0,
          "Restore arithmetic environment after adversary");
  }
};
auto denied_environment() -> void {
  const auto d = require(assess_origin_boarding_planted_legs_timing(.5, .5));
  check(!d.complete && !d.derivative_domains_certified &&
            !d.root_speed_certified && !d.root_acceleration_certified &&
            !d.leg_joint_speed_certified && d.examined_nodes == 0 &&
            d.leaves.empty() && d.first_refusal &&
            d.first_refusal->condition ==
                BoardingPlantedLegTimingCondition::unsupported_arithmetic,
        "Unsupported arithmetic refuses timing before any node and complete "
        "summary");
  const auto e = numeric();
  check(!e.derivative_domains_certified && !e.joint_speed_certified &&
            e.limiting_condition ==
                BoardingPlantedLegTimingCondition::unsupported_arithmetic,
        "Private numeric jet requires actual arithmetic environment");
  const auto speed = detail::boarding_planted_leg_joint_speed({0, 0}, {0, 0});
  check(!speed.supported && !speed.certified,
        "Even zero resultant requires supported arithmetic environment");
}
auto environment_controls() -> void {
  for (const auto mode : std::array{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    SavedEnvironment saved;
    check(std::fesetround(mode) == 0, "Set directed rounding adversary");
    denied_environment();
  }
#if defined(__SSE2__) && defined(__x86_64__)
  struct SavedControl {
    unsigned original{_mm_getcsr()};
    ~SavedControl() { _mm_setcsr(original); }
  };
  for (const auto flags :
       std::array{1U << 15, 1U << 6, (1U << 15) | (1U << 6)}) {
    SavedEnvironment saved;
    SavedControl control;
    _mm_setcsr(control.original | flags);
    denied_environment();
  }
  for (const auto rc : std::array{1U, 2U, 3U}) {
    SavedEnvironment saved;
    SavedControl control;
    _mm_setcsr((control.original & ~(3U << 13)) | (rc << 13));
    denied_environment();
  }
#endif
  check(std::fegetround() == FE_TONEAREST,
        "Restore nearest arithmetic before independent oracle queries");
}
} // namespace
int main() {
  try {
    environment_controls();
    speed_controls();
    numeric_controls();
    bounded_controls();
    public_controls();
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "FAIL: " << e.what() << '\n';
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
