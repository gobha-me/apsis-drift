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
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>
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
    throw std::runtime_error("Required finite planted-body API refused");
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
// Independent unfactored sphere-intersection graph; no producer expression
// helper, midpoint report or inverse angle constructs this oracle.
auto planted_leg(Motion H, Motion A, Motion B) -> std::array<Motion, 4> {
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
  const Motion thigh{sub(K.value, H.value), sub(K.first, H.first),
                     sub(K.second, H.second)},
      shin{sub(A.value, K.value), scale(K.first, -1), scale(K.second, -1)};
  const auto allowance = 65536 * std::numeric_limits<long double>::epsilon();
  for (const auto& link : std::array{thigh, shin})
    check(std::abs(dot(link.value, link.first)) <= allowance &&
              std::abs(dot(link.first, link.first) +
                       dot(link.value, link.second)) <= allowance,
          "Independent differentiated exact links preserve length");
  check(
      std::abs(dot(thigh.value, thigh.value) - L1 * L1) <= allowance &&
          std::abs(dot(shin.value, shin.value) - L2 * L2) <= allowance,
      "Independent unfactored planted knee retains both nominal link lengths");
  return {H, K, A, B};
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
auto shifted(Motion m, Point offset) -> Motion {
  m.value = add(m.value, offset);
  return m;
}
auto average(Motion a, Motion b) -> Motion {
  return {scale(add(a.value, b.value), .5L), scale(add(a.first, b.first), .5L),
          scale(add(a.second, b.second), .5L)};
}
struct ExpectedPart {
  bool capsule{};
  std::size_t first{}, second{};
  Point half;
  double radius{};
  std::uint32_t weight{};
};
// Independent explicit Body02 dimensional/mass table, in existing enum order.
const std::array<ExpectedPart, 15> expected_parts{
    {{false, 0, 0, {.24, .12, .18}, 0, 144},
     {false, 1, 1, {.26, .2695, .18}, 0, 540},
     {false, 2, 2, {.16, .18, .18}, 0, 96},
     {true, 4, 5, {}, .105, 120},
     {true, 5, 6, {}, .075, 48},
     {false, 7, 7, {.06, .05, .14}, 0, 12},
     {true, 8, 9, {}, .065, 15},
     {true, 9, 10, {}, .055, 10},
     {false, 10, 10, {.04, .05, .02}, 0, 5},
     {true, 11, 12, {}, .105, 120},
     {true, 12, 13, {}, .075, 48},
     {false, 14, 14, {.06, .05, .14}, 0, 12},
     {true, 15, 16, {}, .065, 15},
     {true, 16, 17, {}, .055, 10},
     {false, 17, 17, {.04, .05, .02}, 0, 5}}};
struct OracleBody {
  std::array<Motion, 18> points;
  std::array<Motion, 15> masses;
  Motion com, collapsed_com_derivatives;
};
auto body_oracle(double parameter) -> OracleBody {
  const auto t = static_cast<long double>(parameter),
             a = static_cast<long double>(.32);
  const auto x = a * t * t * (3 - 2 * t), x1 = a * 6 * t * (1 - t),
             x2 = a * (6 - 12 * t);
  OracleBody o;
  o.points[0] = {
      {x, 0, static_cast<long double>(-.16)}, {x1, 0, 0}, {x2, 0, 0}};
  o.points[1] = shifted(o.points[0], {0, static_cast<long double>(.3495), 0});
  o.points[2] = shifted(o.points[0], {0, static_cast<long double>(.70237), 0});
  o.points[3] = shifted(o.points[0], {0, static_cast<long double>(.65237), 0});
  const auto c = std::sqrt(.5L), upper = static_cast<long double>(.35898),
             placement = static_cast<long double>(.72);
  for (std::size_t side = 0; side < 2; ++side) {
    const auto sign = side == 0 ? -1.L : 1.L,
               plane = side == 0 ? 0.L : static_cast<long double>(-.16);
    const Motion H{{x + sign * static_cast<long double>(.14), placement,
                    static_cast<long double>(-.16)},
                   {x1, 0, 0},
                   {x2, 0, 0}},
        A{{static_cast<long double>(side == 0 ? .02 : .34),
           plane + static_cast<long double>(.1),
           static_cast<long double>(side == 0 ? -.35 : -.65)},
          {},
          {}},
        B{{A.value.x, plane + static_cast<long double>(.05), A.value.z},
          {},
          {}};
    const auto leg = planted_leg(H, A, B);
    const auto base = 4 + side * 7;
    for (std::size_t i = 0; i < 4; ++i)
      o.points[base + i] = shifted(leg[i], {0, -placement, 0});
    o.points[base + 4] =
        shifted(o.points[0], {sign * static_cast<long double>(.20265),
                              static_cast<long double>(.579), 0});
    o.points[base + 5] =
        shifted(o.points[base + 4], {0, -upper * c, -upper * c});
    o.points[base + 6] =
        shifted(o.points[base + 5], {0, static_cast<long double>(.386), 0});
  }
  std::uint32_t denominator{};
  for (std::size_t i = 0; i < 15; ++i) {
    const auto& p = expected_parts[i];
    o.masses[i] = p.first == p.second
                      ? o.points[p.first]
                      : average(o.points[p.first], o.points[p.second]);
    denominator += p.weight;
    o.com.value = add(o.com.value, scale(o.masses[i].value, p.weight));
    o.com.first = add(o.com.first, scale(o.masses[i].first, p.weight));
    o.com.second = add(o.com.second, scale(o.masses[i].second, p.weight));
  }
  check(denominator == 1200, "All15 independent surrogate masses sum to1200");
  o.com = {scale(o.com.value, 1.L / 1200), scale(o.com.first, 1.L / 1200),
           scale(o.com.second, 1.L / 1200)};
  o.collapsed_com_derivatives.first =
      scale(add(scale(o.points[0].first, 960),
                scale(add(o.points[5].first, o.points[12].first), 84)),
            1.L / 1200);
  o.collapsed_com_derivatives.second =
      scale(add(scale(o.points[0].second, 960),
                scale(add(o.points[5].second, o.points[12].second), 84)),
            1.L / 1200);
  for (std::size_t axis = 0; axis < 3; ++axis) {
    const auto error = 65536 * std::numeric_limits<long double>::epsilon();
    check(std::abs(component(o.com.first, axis) -
                   component(o.collapsed_com_derivatives.first, axis)) <=
                  error &&
              std::abs(component(o.com.second, axis) -
                       component(o.collapsed_com_derivatives.second, axis)) <=
                  error,
          "Full15 mass derivative reconstruction agrees with independent "
          "integer960/84 identity");
  }
  return o;
}
// Field-wise snapshot includes all runtime old fields; padding is never read.
struct Snapshot {
  std::vector<std::uint64_t> values;
  auto number(double n) -> void {
    values.push_back(std::bit_cast<std::uint64_t>(n));
  }
  template <class T> auto integer(T n) -> void {
    values.push_back(static_cast<std::uint64_t>(n));
  }
  auto scalar(BoardingPlantedLegScalarBounds b) -> void {
    number(b.lower);
    number(b.upper);
  }
  auto point(const BoardingPlantedLegPointBounds& b) -> void {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      number(component(b.lower, axis));
      number(component(b.upper, axis));
    }
  }
  auto derivatives(const BoardingPlantedLegPointDerivatives& e) -> void {
    point(e.velocity);
    point(e.acceleration);
  }
  auto closure(const BoardingPlantedLegEvidence& e) -> void {
    for (const auto& p : std::array{e.hip, e.knee, e.ankle, e.boot_center})
      point(p);
    for (const auto& b :
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
  auto closure(const BoardingPlantedLegDiagnostic& d) -> void {
    integer(d.recipe_version);
    number(d.requested_first);
    number(d.requested_last);
    number(d.duration_seconds);
    number(d.placement_y_metres);
    integer(d.reverse);
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
      for (const auto& e : leaf.legs)
        closure(e);
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
  }
  auto timing(const BoardingPlantedLegTimingEvidence& e) -> void {
    closure(e.closure);
    for (const auto& p : std::array{e.hip, e.knee, e.ankle, e.boot_center})
      derivatives(p);
    for (const auto& a :
         std::array{e.hip_pitch, e.shin_pitch, e.knee_flex, e.ankle_pitch,
                    e.hip_abduction, e.ankle_roll}) {
      scalar(a.rate);
      scalar(a.coordinate_second);
    }
    for (const auto& s : std::array{e.hip_speed, e.knee_speed, e.ankle_speed}) {
      scalar(s.speed_radians_per_second);
      integer(s.supported);
      integer(s.certified);
    }
    integer(e.derivative_domains_certified);
    integer(e.joint_speed_certified);
    integer(e.limiting_condition);
    integer(e.limiting_joint);
    scalar(e.limiting_bound);
  }
  auto timing(const BoardingPlantedLegTimingDiagnostic& d) -> void {
    integer(d.timing_version);
    closure(d.closure);
    number(d.requested_first);
    number(d.requested_last);
    number(d.seconds_per_parameter);
    number(d.reporting_elapsed_seconds);
    integer(d.reverse);
    integer(d.complete);
    integer(d.derivative_domains_certified);
    integer(d.root_speed_certified);
    integer(d.root_acceleration_certified);
    integer(d.leg_joint_speed_certified);
    integer(d.examined_nodes);
    integer(d.maximum_depth);
    integer(d.leaves.size());
    for (const auto& leaf : d.leaves) {
      number(leaf.first);
      number(leaf.last);
      derivatives(leaf.root);
      scalar(leaf.root_speed);
      scalar(leaf.root_acceleration);
      for (const auto& e : leaf.legs)
        timing(e);
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
      integer(r.joint);
      scalar(r.limiting_bound);
    }
  }
};
auto snapshot(const BoardingPlantedLegTimingDiagnostic& d)
    -> std::vector<std::uint64_t> {
  Snapshot s;
  s.timing(d);
  return std::move(s.values);
}
auto point_equal(const BoardingPlantedLegPointBounds& a,
                 const BoardingPlantedLegPointBounds& b) -> bool {
  for (std::size_t axis = 0; axis < 3; ++axis)
    if (!scalar_equal({component(a.lower, axis), component(a.upper, axis)},
                      {component(b.lower, axis), component(b.upper, axis)}))
      return false;
  return true;
}
auto derivatives_equal(const BoardingPlantedLegPointDerivatives& a,
                       const BoardingPlantedLegPointDerivatives& b) -> bool {
  return point_equal(a.velocity, b.velocity) &&
         point_equal(a.acceleration, b.acceleration);
}
auto zero(const BoardingPlantedLegPointDerivatives& d) -> void {
  for (const auto& b : std::array{d.velocity, d.acceleration})
    for (std::size_t axis = 0; axis < 3; ++axis)
      check(component(b.lower, axis) == 0 && component(b.upper, axis) == 0,
            "Fixed planted foot derivatives remain exactly zero");
}
template <class E>
auto motion_contains(const E& e, Motion m, bool reverse) -> void {
  point_contains(e.value, m.value);
  point_contains(e.derivatives.velocity,
                 scale(m.first, (reverse ? -1.L : 1.L) / 12));
  point_contains(e.derivatives.acceleration, scale(m.second, 1.L / 144));
}
template <class E>
auto translated_contains(const E& e, Point canonical) -> void {
  for (std::size_t axis = 0; axis < 3; ++axis) {
    const auto placement = axis == 1 ? static_cast<long double>(.72) : 0.L;
    // Add the same exact placement to the reported bounds for this oracle
    // comparison; never collapse it into a new rounded world point.
    const auto low = static_cast<long double>(component(e.value.lower, axis)) +
                     placement,
               high = static_cast<long double>(component(e.value.upper, axis)) +
                      placement,
               wanted = component(canonical, axis) + placement,
               allowance = 8192 * std::numeric_limits<long double>::epsilon() *
                           std::max(1.L, std::abs(wanted));
    check(low <= wanted + allowance && wanted - allowance <= high,
          "Common placement applied exactly once encloses genuine world point");
  }
}
static_assert(kBoardingPlantedBodyPointCount == 18 &&
              kBoardingBodyPartCount == 15 &&
              kBoardingPlantedBodyMaximumLeaves == 4096);
static_assert(sizeof(BoardingPlantedBodyLeaf) <= 6144);
static_assert(sizeof(BoardingPlantedBodyLeaf) +
                  sizeof(std::array<BoardingPlantedBodyPartBinding, 15>) <=
              8192);
auto roster(const BoardingPlantedBodyDiagnostic& d) -> void {
  using P = BoardingPlantedBodyPointId;
  using B = BoardingBodyPartId;
  const std::array points{P::root,
                          P::trunk_center,
                          P::helmet_center,
                          P::eye,
                          P::port_hip,
                          P::port_knee,
                          P::port_ankle,
                          P::port_boot_center,
                          P::port_shoulder,
                          P::port_elbow,
                          P::port_wrist,
                          P::starboard_hip,
                          P::starboard_knee,
                          P::starboard_ankle,
                          P::starboard_boot_center,
                          P::starboard_shoulder,
                          P::starboard_elbow,
                          P::starboard_wrist};
  const std::array parts{B::pelvis,
                         B::trunk,
                         B::helmet,
                         B::port_thigh,
                         B::port_shin,
                         B::port_boot,
                         B::port_upper_arm,
                         B::port_forearm,
                         B::port_hand,
                         B::starboard_thigh,
                         B::starboard_shin,
                         B::starboard_boot,
                         B::starboard_upper_arm,
                         B::starboard_forearm,
                         B::starboard_hand};
  for (std::size_t i = 0; i < points.size(); ++i)
    check(static_cast<std::size_t>(points[i]) == i,
          "Exact18 canonical point slots stay in registered order");
  std::uint32_t weight{};
  std::size_t boxes{}, capsules{};
  for (std::size_t i = 0; i < 15; ++i) {
    const auto& p = d.parts[i];
    const auto& expected = expected_parts[i];
    check(p.id == parts[i] && p.mass.first == points[expected.first] &&
              p.mass.second == points[expected.second] &&
              p.mass.weight == expected.weight,
          "Every named part retains exact shared mass identities and integer "
          "weight");
    weight += p.mass.weight;
    if (expected.capsule) {
      const auto* capsule =
          std::get_if<BoardingPlantedBodyCapsuleBinding>(&p.reservation);
      check(capsule, "Actual capsule kind matches fixed part roster");
      if (capsule) {
        ++capsules;
        check(capsule->start == points[expected.first] &&
                  capsule->end == points[expected.second] &&
                  bits(capsule->radius_metres, expected.radius),
              "Complete capsule uses genuine shared endpoints and unchanged "
              "radius");
      }
    } else {
      const auto* box =
          std::get_if<BoardingPlantedBodyBoxBinding>(&p.reservation);
      check(box, "Actual box kind matches fixed part roster");
      if (box) {
        ++boxes;
        check(box->center == points[expected.first],
              "World box center names its genuine point");
        for (std::size_t axis = 0; axis < 3; ++axis) {
          check(bits(component(box->half_size_metres, axis),
                     static_cast<double>(component(expected.half, axis))),
                "World box dimensions remain full nominal sizes");
          for (std::size_t row = 0; row < 3; ++row)
            check(bits(component(box->frame.columns[axis], row),
                       axis == row ? 1. : 0.),
                  "New fixed world box columns are exactly identity, never "
                  "normalized Body02 matrices");
        }
      }
    }
  }
  check(weight == 1200 && boxes == 7 && capsules == 8,
        "Whole15 roster conserves surrogate mass and exact type inventory");
}
auto snapshot(const BoardingPlantedBodyDiagnostic& d)
    -> std::vector<std::uint64_t> {
  Snapshot s;
  s.integer(d.recipe_version);
  s.timing(d.timing);
  s.number(d.common_translation_y_metres);
  s.integer(d.assembled_leaves);
  s.integer(d.complete);
  s.integer(d.reservations_complete);
  s.integer(d.mass_model_complete);
  s.integer(d.com_derivatives_complete);
  for (const auto& p : d.parts) {
    s.integer(p.id);
    s.integer(p.reservation.index());
    if (const auto* b =
            std::get_if<BoardingPlantedBodyBoxBinding>(&p.reservation)) {
      s.integer(b->center);
      for (std::size_t axis = 0; axis < 3; ++axis) {
        s.number(component(b->half_size_metres, axis));
        for (std::size_t row = 0; row < 3; ++row)
          s.number(component(b->frame.columns[axis], row));
      }
    } else {
      const auto& c =
          std::get<BoardingPlantedBodyCapsuleBinding>(p.reservation);
      s.integer(c.start);
      s.integer(c.end);
      s.number(c.radius_metres);
    }
    s.integer(p.mass.first);
    s.integer(p.mass.second);
    s.integer(p.mass.weight);
  }
  s.integer(d.leaves.size());
  const auto evidence = [&](const BoardingPlantedBodyPointEvidence& e) {
    s.point(e.value);
    s.derivatives(e.derivatives);
  };
  for (const auto& leaf : d.leaves) {
    s.number(leaf.first);
    s.number(leaf.last);
    for (const auto& p : leaf.points)
      evidence(p);
    for (const auto& p : leaf.mass_points)
      evidence(p);
    evidence(leaf.center_of_mass);
  }
  s.integer(d.first_refusal.has_value());
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    s.integer(r.condition);
    s.integer(r.leaf_index);
    s.number(r.first);
    s.number(r.last);
  }
  return std::move(s.values);
}
auto inspect(double first, double last) -> BoardingPlantedBodyDiagnostic {
  auto d = require(assess_origin_boarding_planted_body(first, last));
  check(snapshot(d.timing) ==
            snapshot(require(
                assess_origin_boarding_planted_legs_timing(first, last))),
        "New body owns every closure/timing bit, report, counter and refusal "
        "unchanged");
  check(d.recipe_version == 1 && bits(d.common_translation_y_metres, .72) &&
            d.assembled_leaves == d.leaves.size() && d.leaves.size() <= 4096,
        "Body version, ONE common placement and finite assembled-count "
        "identity retained");
  check(!d.self_qualified && !d.load_qualified && !d.world_qualified &&
            !d.sweep_qualified && !d.dynamics_qualified && !d.route_qualified &&
            !d.actor_qualified && !d.seat_qualified && !d.save_qualified &&
            !d.free_foot_swing_qualified,
        "Complete body/COM cannot grant self, physical support, gameplay or "
        "swing authority");
  roster(d);
  auto edge = std::min(first, last);
  const auto high = std::max(first, last);
  for (std::size_t i = 0; i < d.leaves.size(); ++i) {
    const auto& leaf = d.leaves[i];
    check(bits(leaf.first, edge) && leaf.first <= leaf.last &&
              leaf.last <= high && i < d.timing.leaves.size(),
          "Assembled body retains exact ordered timing cover with no extra "
          "adaptive evaluator");
    edge = leaf.last;
    if (i >= d.timing.leaves.size()) continue;
    const auto& timing = d.timing.leaves[i];
    check(bits(leaf.first, timing.first) && bits(leaf.last, timing.last),
          "Each body leaf binds exact owned accepted timing leaf endpoints");
    check(leaf.points[0].value.lower.y == 0 &&
              leaf.points[0].value.upper.y == 0 &&
              leaf.points[0].value.lower.z == -.16 &&
              leaf.points[0].value.upper.z == -.16 &&
              derivatives_equal(leaf.points[0].derivatives, timing.root),
          "Canonical root is unplaced and shares original physical derivative "
          "bounds");
    for (std::size_t side = 0; side < 2; ++side) {
      const auto base = 4 + 7 * side;
      const auto& e = timing.legs[side];
      const std::array positions{e.closure.hip, e.closure.knee, e.closure.ankle,
                                 e.closure.boot_center};
      const std::array derivatives{e.hip, e.knee, e.ankle, e.boot_center};
      for (std::size_t j = 0; j < 4; ++j)
        check(point_equal(leaf.points[base + j].value, positions[j]) &&
                  derivatives_equal(leaf.points[base + j].derivatives,
                                    derivatives[j]),
              "Every leg point copies exact same-expression closure BOUNDS and "
              "timing derivatives, not reports");
      for (const auto j : std::array<std::size_t, 3>{4, 5, 6})
        check(derivatives_equal(leaf.points[base + j].derivatives, timing.root),
              "Upright arm/hand points translate rigidly with same root "
              "derivatives");
      zero(leaf.points[base + 2].derivatives);
      zero(leaf.points[base + 3].derivatives);
      const auto plane = side == 0 ? 0. : -.16;
      const std::array ankle_terms{plane, .1, -.72},
          boot_terms{plane, .05, -.72};
      for (std::size_t j = 0; j < 3; ++j)
        check(bits(e.closure.ankle_y_terms[j], ankle_terms[j]) &&
                  bits(e.closure.boot_center_y_terms[j], boot_terms[j]),
              "Owned exact plane-height-placement terms preserve original "
              "planted identity");
    }
    for (const auto j : std::array<std::size_t, 3>{1, 2, 3})
      check(derivatives_equal(leaf.points[j].derivatives, timing.root),
            "Trunk/helmet/eye derivatives are rigid root translations");
    for (const auto fraction : std::array{0., .25, .5, .75, 1.}) {
      const auto t = leaf.first + (leaf.last - leaf.first) * fraction;
      const auto oracle = body_oracle(t);
      for (std::size_t j = 0; j < 18; ++j) {
        motion_contains(leaf.points[j], oracle.points[j], d.timing.reverse);
        translated_contains(leaf.points[j], oracle.points[j].value);
      }
      for (std::size_t j = 0; j < 15; ++j)
        motion_contains(leaf.mass_points[j], oracle.masses[j],
                        d.timing.reverse);
      motion_contains(leaf.center_of_mass, oracle.com, d.timing.reverse);
      point_contains(leaf.center_of_mass.derivatives.velocity,
                     scale(oracle.collapsed_com_derivatives.first,
                           (d.timing.reverse ? -1.L : 1.L) / 12));
      point_contains(leaf.center_of_mass.derivatives.acceleration,
                     scale(oracle.collapsed_com_derivatives.second, 1.L / 144));
      // Independently accumulate PLACED mass points; it must equal canonical
      // COM plus ONE .72 because all15 integer weights sum to1200.
      Point weighted_world;
      for (std::size_t j = 0; j < 15; ++j)
        weighted_world = add(weighted_world,
                             scale(add(oracle.masses[j].value,
                                       {0, static_cast<long double>(.72), 0}),
                                   expected_parts[j].weight));
      const auto world_com = scale(weighted_world, 1.L / 1200);
      for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto offset = axis == 1 ? static_cast<long double>(.72) : 0.L,
                   allowance =
                       65536 * std::numeric_limits<long double>::epsilon();
        check(std::abs(component(world_com, axis) -
                       component(oracle.com.value, axis) - offset) <= allowance,
              "Full world mass sum independently confirms common placement "
              "exactly once");
      }
      translated_contains(leaf.center_of_mass, oracle.com.value);
      for (std::size_t side = 0; side < 2; ++side) {
        const auto base = 4 + 7 * side;
        const auto plane = side == 0 ? 0.L : static_cast<long double>(-.16);
        const auto world_bottom = oracle.points[base + 3].value.y +
                                  static_cast<long double>(.72) -
                                  static_cast<long double>(.05);
        check(std::abs(world_bottom - plane) <=
                  65536 * std::numeric_limits<long double>::epsilon(),
              "Placed genuine boot bottom remains exactly its registered "
              "source-free plane");
      }
    }
  }
  if (d.complete)
    check(d.timing.complete && !d.first_refusal && d.reservations_complete &&
              d.mass_model_complete && d.com_derivatives_complete &&
              !d.leaves.empty() && d.leaves.size() == d.timing.leaves.size() &&
              bits(edge, high),
          "Full body success requires complete prerequisite, whole15 assembly "
          "and closed cover");
  else
    check(d.first_refusal && !d.reservations_complete &&
              !d.mass_model_complete && !d.com_derivatives_complete,
          "Partial or refused body cannot qualify whole-request "
          "reservations/mass/COM");
  std::cout << "body " << first << " -> " << last << " complete=" << d.complete
            << " leaves=" << d.leaves.size() << " timing=" << d.timing.complete;
  if (d.first_refusal)
    std::cout << " refusal=" << static_cast<int>(d.first_refusal->condition)
              << " leaf=" << d.first_refusal->leaf_index;
  std::cout << '\n';
  return d;
}

auto reverses(const BoardingPlantedBodyDiagnostic& a,
              const BoardingPlantedBodyDiagnostic& b) -> void {
  check(a.complete == b.complete && a.leaves.size() == b.leaves.size() &&
            a.assembled_leaves == b.assembled_leaves,
        "Reverse retains body completeness and identical spatial cover");
  for (std::size_t i = 0; i < std::min(a.leaves.size(), b.leaves.size()); ++i) {
    const auto& x = a.leaves[i];
    const auto& y = b.leaves[i];
    check(bits(x.first, y.first) && bits(x.last, y.last),
          "Reverse does not reorder or retime body leaves");
    const auto evidence = [&](const BoardingPlantedBodyPointEvidence& e,
                              const BoardingPlantedBodyPointEvidence& f) {
      check(point_equal(e.value, f.value),
            "Reverse retains canonical body/mass/COM values bit-for-bit");
      point_reverse(e.derivatives, f.derivatives);
    };
    for (std::size_t j = 0; j < 18; ++j)
      evidence(x.points[j], y.points[j]);
    for (std::size_t j = 0; j < 15; ++j)
      evidence(x.mass_points[j], y.mass_points[j]);
    evidence(x.center_of_mass, y.center_of_mass);
  }
}
auto observed_success(const BoardingPlantedBodyDiagnostic& d,
                      std::size_t leaves) -> void {
  check(d.complete && d.reservations_complete && d.mass_model_complete &&
            d.com_derivatives_complete && !d.first_refusal &&
            d.timing.complete && !d.timing.first_refusal &&
            d.leaves.size() == leaves && d.assembled_leaves == leaves &&
            d.timing.leaves.size() == leaves,
        "Observed unchanged body requests retain full completion and exact "
        "leaf count");
}
auto public_controls() -> void {
  const auto full = inspect(0, 1), reverse = inspect(1, 0);
  observed_success(full, 20);
  observed_success(reverse, 20);
  reverses(full, reverse);
  for (const auto& interval :
       std::array{std::pair{0., .5}, std::pair{.5, 1.}, std::pair{.25, .5},
                  std::pair{.125, .875}}) {
    const auto forward = inspect(interval.first, interval.second),
               backward = inspect(interval.second, interval.first);
    const std::size_t leaves =
        interval.first == .25 ? 7 : (interval.first == .125 ? 16 : 10);
    observed_success(forward, leaves);
    observed_success(backward, leaves);
    reverses(forward, backward);
  }
  for (const auto t : std::array{0., .25, .5, .75, 1.}) {
    const auto d = inspect(t, t);
    observed_success(d, 1);
    if (d.complete) {
      check(d.leaves.size() == 1 && !d.timing.reverse,
            "Complete point body owns instantaneous, forward, nonpaused "
            "evidence");
      const auto& root = d.leaves[0].points[0];
      if (t == .5)
        check(root.derivatives.velocity.lower.x > 0,
              "Midcurve point retains nonzero instantaneous body velocity");
      if (t == 0 || t == 1)
        check(root.derivatives.velocity.lower.x == 0 &&
                  root.derivatives.velocity.upper.x == 0 &&
                  (root.derivatives.acceleration.lower.x > 0 ||
                   root.derivatives.acceleration.upper.x < 0),
              "Endpoint zero speed preserves nonzero curve acceleration, not a "
              "supported pause");
    }
  }
  const auto nan = std::numeric_limits<double>::quiet_NaN(),
             inf = std::numeric_limits<double>::infinity();
  for (const auto bad : std::array{nan, inf, -inf, std::nextafter(0., -inf),
                                   std::nextafter(1., inf)})
    check(!assess_origin_boarding_planted_body(bad, .5) &&
              !assess_origin_boarding_planted_body(.5, bad),
          "Invalid/nonfinite public body parameters refuse before diagnostic "
          "construction");
}
auto bounded_controls() -> void {
  const auto original = require(assess_origin_boarding_planted_legs_timing());
  check(original.complete && !original.leaves.empty(),
        "Unchanged registered timing prerequisite remains complete");
  if (original.leaves.empty()) return;
  for (const auto capacity : std::array<std::size_t, 4>{0, 1, 2, 4096}) {
    const auto d =
        require(detail::boarding_planted_body_bounded(0, 1, capacity));
    check(snapshot(d.timing) == snapshot(original),
          "Reduced body capacity preserves full original timing/closure "
          "evidence");
    check(d.assembled_leaves == d.leaves.size() && d.leaves.size() <= capacity,
          "Independent body capacity accounts only fully assembled leaves");
    if (capacity == 0)
      check(
          d.leaves.empty() && !d.complete && d.first_refusal &&
              d.first_refusal->condition ==
                  BoardingPlantedBodyCondition::body_capacity &&
              d.first_refusal->leaf_index == 0 &&
              bits(d.first_refusal->first, original.leaves[0].first) &&
              bits(d.first_refusal->last, original.leaves[0].last),
          "Zero body capacity refuses at exact first owned leaf before append");
    if (!d.complete) {
      check(d.first_refusal && !d.reservations_complete &&
                !d.mass_model_complete && !d.com_derivatives_complete,
            "Body prefix cannot qualify complete reservations/mass/COM");
      if (d.first_refusal && d.first_refusal->condition ==
                                 BoardingPlantedBodyCondition::body_capacity) {
        const auto index = d.first_refusal->leaf_index;
        check(index == capacity && index == d.leaves.size() &&
                  index < original.leaves.size(),
              "Capacity refusal owns exact first missing body leaf");
        if (index < original.leaves.size())
          check(bits(d.first_refusal->first, original.leaves[index].first) &&
                    bits(d.first_refusal->last, original.leaves[index].last),
                "First missing body interval binds original timing endpoints");
      }
    }
    check(d.leaves.size() <= original.leaves.size(),
          "Body prefix never exceeds owned timing cover");
    for (std::size_t i = 0;
         i < std::min(d.leaves.size(), original.leaves.size()); ++i)
      check(bits(d.leaves[i].first, original.leaves[i].first) &&
                bits(d.leaves[i].last, original.leaves[i].last),
            "Partial body preserves exact original accepted prefix");
  }
  check(!detail::boarding_planted_body_bounded(0, 1, 4097),
        "Body storage cannot exceed registered maximum");
  for (const auto& cap : std::array{std::array<std::size_t, 3>{12, 0, 4096},
                                    std::array<std::size_t, 3>{12, 8191, 0},
                                    std::array<std::size_t, 3>{0, 8191, 4096},
                                    std::array<std::size_t, 3>{12, 1, 4096}}) {
    const auto d = require(detail::boarding_planted_body_bounded(
        0, 1, 4096, cap[0], cap[1], cap[2]));
    const auto expected = require(detail::boarding_planted_legs_timing_bounded(
        0, 1, cap[0], cap[1], cap[2]));
    check(snapshot(d.timing) == snapshot(expected) && !d.timing.complete &&
              d.timing.first_refusal && d.leaves.empty() &&
              d.assembled_leaves == 0 && !d.complete &&
              !d.reservations_complete && !d.mass_model_complete &&
              !d.com_derivatives_complete && d.first_refusal &&
              d.first_refusal->condition ==
                  BoardingPlantedBodyCondition::timing_prerequisite,
          "Lowered timing prerequisite retains its honest refusal and "
          "assembles zero body leaves");
    if (d.first_refusal && d.timing.first_refusal)
      check(bits(d.first_refusal->first, d.timing.first_refusal->first) &&
                bits(d.first_refusal->last, d.timing.first_refusal->last),
            "Body prerequisite refusal preserves exact underlying missing "
            "interval");
  }
  for (const auto& cap : std::array{std::array<std::size_t, 3>{13, 8191, 4096},
                                    std::array<std::size_t, 3>{12, 8192, 4096},
                                    std::array<std::size_t, 3>{12, 8191, 4097}})
    check(
        !detail::boarding_planted_body_bounded(0, 1, 4096, cap[0], cap[1],
                                               cap[2]),
        "Caller cannot expand underlying closure/timing registered capacities");
}
auto ownership_controls() -> void {
  auto retained = []() {
    auto original = require(assess_origin_boarding_planted_body(.25, .5));
    auto copied = original;
    return copied;
  }();
  const auto saved = snapshot(retained);
  {
    auto copied = retained;
    auto moved = std::move(copied);
    check(snapshot(moved) == saved,
          "Copied then moved body owns every point/mass/timing/refusal record");
    moved.common_translation_y_metres = 100;
    moved.parts[0].mass.weight = 0;
    if (!moved.leaves.empty()) moved.leaves[0].points[0].value.lower.x = 100;
  }
  check(snapshot(retained) == saved,
        "Destroying and mutating another diagnostic does not alias retained "
        "body evidence");
  check(snapshot(require(assess_origin_boarding_planted_body(.25, .5))) ==
            saved,
        "New body query is independent of copied report mutation and old "
        "prerequisite lifetime");
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
          "Restore original arithmetic environment after adversary");
  }
};
auto denied_environment() -> void {
  const auto d = require(assess_origin_boarding_planted_body(.5, .5));
  check(
      snapshot(d.timing) ==
          snapshot(require(assess_origin_boarding_planted_legs_timing(.5, .5))),
      "Arithmetic refusal retains unchanged old timing and closure "
      "diagnostics");
  check(!d.complete && !d.reservations_complete && !d.mass_model_complete &&
            !d.com_derivatives_complete && d.assembled_leaves == 0 &&
            d.leaves.empty() && d.first_refusal &&
            d.first_refusal->condition ==
                BoardingPlantedBodyCondition::unsupported_arithmetic &&
            !d.timing.complete && d.timing.first_refusal &&
            d.timing.first_refusal->condition ==
                BoardingPlantedLegTimingCondition::unsupported_arithmetic,
        "Unsupported environment refuses body before assembly with no success "
        "laundering");
}
auto environment_controls() -> void {
  for (const auto mode : std::array{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    SavedEnvironment saved;
    check(std::fesetround(mode) == 0, "Set directed-rounding body adversary");
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
        "Restore nearest mode before body/oracle queries");
}
} // namespace
int main() {
  try {
    environment_controls();
    bounded_controls();
    ownership_controls();
    public_controls();
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "FAIL: " << e.what() << '\n';
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
