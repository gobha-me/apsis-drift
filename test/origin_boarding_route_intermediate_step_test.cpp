#include "apsis_drift/origin_boarding_route_intermediate_step.hpp"
#include "origin_boarding_route_checkpoint_unload_internal.hpp"
#include "origin_boarding_route_intermediate_step_internal.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#if defined(__SSE2__) && defined(__x86_64__)
#include <xmmintrin.h>
#endif
namespace {
using namespace apsis_drift;
using Request = BoardingRouteFootPhaseRequest;
using Constant = BoardingRoutePhaseConstant;
using ConstantPoint = BoardingRoutePhasePointConstant;
using Cell = BoardingRouteFootPhaseCell;
std::size_t checks{};
int failures{};
void check(bool yes, std::string_view why) {
  ++checks;
  if (!yes) {
    ++failures;
    std::cerr << "FAIL: " << why << '\n';
  }
}
template <class T> auto require(std::expected<T, std::string> r) -> T {
  if (!r)
    throw std::runtime_error("Required intermediate numerical API refused: " +
                             r.error());
  return std::move(*r);
}
// Independent long-double automatic differentiation and unfactored sphere
// intersection. Numeric corroboration never admits source or actor geometry.
struct Jet {
  long double value{}, first{}, second{};
};
auto operator+(Jet a, Jet b) -> Jet {
  return {a.value + b.value, a.first + b.first, a.second + b.second};
}
auto operator-(Jet a) -> Jet {
  return {-a.value, -a.first, -a.second};
}
auto operator-(Jet a, Jet b) -> Jet {
  return a + -b;
}
auto operator*(Jet a, Jet b) -> Jet {
  return {a.value * b.value, a.first * b.value + a.value * b.first,
          a.second * b.value + 2 * a.first * b.first + a.value * b.second};
}
auto reciprocal(Jet a) -> Jet {
  const auto inv = 1 / a.value;
  return {inv, -a.first * inv * inv,
          2 * a.first * a.first * inv * inv * inv - a.second * inv * inv};
}
auto operator/(Jet a, Jet b) -> Jet {
  return a * reciprocal(b);
}
auto root(Jet a) -> Jet {
  const auto r = std::sqrt(a.value);
  return {r, a.first / (2 * r),
          a.second / (2 * r) - a.first * a.first / (4 * r * r * r)};
}
auto literal(double x) -> Jet {
  return {static_cast<long double>(x), 0, 0};
}
using Point = std::array<Jet, 3>;
auto add(Point a, Point b) -> Point {
  for (std::size_t axis = 0; axis < 3; ++axis)
    a[axis] = a[axis] + b[axis];
  return a;
}
auto subtract(Point a, Point b) -> Point {
  for (std::size_t axis = 0; axis < 3; ++axis)
    a[axis] = a[axis] - b[axis];
  return a;
}
auto scale(Point a, Jet s) -> Point {
  for (auto& x : a)
    x = x * s;
  return a;
}
auto dot(Point a, Point b) -> Jet {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
auto cross(Point a, Point b) -> Point {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}
auto constant_point(double x, double y, double z) -> Point {
  return {literal(x), literal(y), literal(z)};
}
auto angle(Jet x, Jet y) -> Jet {
  const auto denominator = x.value * x.value + y.value * y.value;
  const auto numerator = x.value * y.first - y.value * x.first;
  return {std::atan2(y.value, x.value), numerator / denominator,
          (x.value * y.second - y.value * x.second) / denominator -
              numerator * 2 * (x.value * x.first + y.value * y.first) /
                  (denominator * denominator)};
}
struct Mass {
  std::size_t first{}, second{};
  std::uint32_t weight{};
};
constexpr std::array<Mass, 15> masses{{{0, 0, 144},
                                       {1, 1, 540},
                                       {2, 2, 96},
                                       {4, 5, 120},
                                       {5, 6, 48},
                                       {7, 7, 12},
                                       {8, 9, 15},
                                       {9, 10, 10},
                                       {10, 10, 5},
                                       {11, 12, 120},
                                       {12, 13, 48},
                                       {14, 14, 12},
                                       {15, 16, 15},
                                       {16, 17, 10},
                                       {17, 17, 5}}};
auto sin_jet(Jet a) -> Jet {
  return {std::sin(a.value), std::cos(a.value) * a.first,
          std::cos(a.value) * a.second - std::sin(a.value) * a.first * a.first};
}
auto cos_jet(Jet a) -> Jet {
  return {std::cos(a.value), -std::sin(a.value) * a.first,
          -std::sin(a.value) * a.second -
              std::cos(a.value) * a.first * a.first};
}
using Matrix = std::array<Point, 3>;
auto transform(const Matrix& m, Point p) -> Point {
  Point out{};
  for (std::size_t row = 0; row < 3; ++row)
    for (std::size_t col = 0; col < 3; ++col)
      out[row] = out[row] + m[row][col] * p[col];
  return out;
}
auto transpose(const Matrix& m) -> Matrix {
  Matrix out{};
  for (std::size_t row = 0; row < 3; ++row)
    for (std::size_t col = 0; col < 3; ++col)
      out[row][col] = m[col][row];
  return out;
}
auto multiply(const Matrix& a, const Matrix& b) -> Matrix {
  Matrix out{};
  for (std::size_t row = 0; row < 3; ++row)
    for (std::size_t col = 0; col < 3; ++col)
      for (std::size_t k = 0; k < 3; ++k)
        out[row][col] = out[row][col] + a[row][k] * b[k][col];
  return out;
}
auto yaw(Jet half) -> Matrix {
  const auto theta = literal(2) * angle(literal(1), half), c = cos_jet(theta),
             s = sin_jet(theta);
  return {{Point{c, literal(0), s}, Point{literal(0), literal(1), literal(0)},
           Point{-s, literal(0), c}}};
}
auto pitch(Jet half) -> Matrix {
  const auto theta = literal(2) * angle(literal(1), half), c = cos_jet(theta),
             s = sin_jet(theta);
  return {{Point{literal(1), literal(0), literal(0)}, Point{literal(0), c, -s},
           Point{literal(0), s, c}}};
}
auto expression(const Constant& c) -> Jet {
  Jet out{};
  for (std::size_t i = 0; i < c.count; ++i)
    out = out + literal(c.terms[i]);
  return out;
}
auto expression(const ConstantPoint& p) -> Point {
  return {expression(p.coordinates[0]), expression(p.coordinates[1]),
          expression(p.coordinates[2])};
}
auto lerp(Jet a, Jet b, Jet s) -> Jet {
  return a + (b - a) * s;
}
auto interpolate(const std::array<double, 2>& a, Jet s) -> Jet {
  return lerp(literal(a[0]), literal(a[1]), s);
}
struct Oracle {
  std::array<Point, 18> points{};
  std::array<Point, 15> mass_points{};
  Point com{};
  std::array<Matrix, 4> frames{};
  std::array<std::array<Jet, 7>, 2> angles{};
  std::array<Jet, 2> D{}, rho2{}, gamma2{}, alpha{}, gamma{};
  std::array<std::array<long double, 3>, 2> speeds{};
  std::array<Point, 2> soles{};
  Jet reaction{}, root_yaw{}, torso{};
  std::array<Jet, 2> sole_yaw{};
};
auto oracle(const Request& r, double u, bool reverse) -> Oracle {
  Oracle out;
  const Jet t{u, (reverse ? -1.L : 1.L) / r.seconds_per_parameter, 0};
  const auto one = literal(1), two = literal(2),
             s = t * t * t *
                 (literal(10) - literal(15) * t + literal(6) * t * t),
             w = literal(64) * t * t * t * (one - t) * (one - t) * (one - t);
  auto& p = out.points;
  const auto root0 = expression(r.root[0]), root1 = expression(r.root[1]);
  for (std::size_t a = 0; a < 3; ++a)
    p[0][a] = lerp(root0[a], root1[a], s);
  const auto root_half = interpolate(r.root_yaw_half, s),
             torso_half = interpolate(r.torso_lean_half, s);
  out.root_yaw = two * angle(one, root_half);
  out.torso = two * angle(one, torso_half);
  out.frames[0] = yaw(root_half);
  out.frames[1] = multiply(out.frames[0], pitch(torso_half));
  out.reaction = interpolate(r.port_reaction_fraction, s);
  for (std::size_t i = 1; i < 4; ++i) {
    const auto height = i == 1 ? .3495 : i == 2 ? .70237 : .65237;
    p[i] = add(p[0], transform(out.frames[1], constant_point(0, height, 0)));
  }
  const auto l1 = literal(.47285), l2 = literal(.47478),
             arm = -literal(.35898) * root(literal(.5));
  for (std::size_t side = 0; side < 2; ++side) {
    const auto base = side == 0 ? 4U : 11U;
    const double sign = side == 0 ? -1. : 1.;
    const auto half = interpolate(r.feet[side].yaw_half, s);
    const auto flat = yaw(half);
    out.frames[side + 2] = flat;
    out.sole_yaw[side] = two * angle(one, half);
    p[base] =
        add(p[0], transform(out.frames[0], constant_point(sign * .14, 0, 0)));
    const auto a = expression(r.feet[side].sole[0]),
               b = expression(r.feet[side].sole[1]);
    Point sole;
    for (std::size_t k = 0; k < 3; ++k)
      sole[k] = lerp(a[k], b[k], s);
    sole[1] = sole[1] + literal(r.feet[side].swing_height_metres) * w;
    out.soles[side] = sole;
    p[base + 2] = add(sole, constant_point(0, .10, 0));
    p[base + 3] = add(sole, constant_point(0, .05, 0));
    const auto d = transform(transpose(flat), subtract(p[base + 2], p[base]));
    const auto rho = root(d[0] * d[0] + d[1] * d[1]), D = dot(d, d);
    const auto alpha = (l1 * l1 - l2 * l2 + D) / (two * D);
    const auto gamma2 = (l1 * l1 - alpha * alpha * D) / D, gamma = root(gamma2);
    const Point U{-d[0] / rho, -d[1] / rho, literal(0)},
        N{U[1], -U[0], literal(0)};
    const auto q = cross(N, d);
    p[base + 1] =
        add(p[base], transform(flat, add(scale(d, alpha), scale(q, gamma))));
    out.D[side] = D;
    out.rho2[side] = rho * rho;
    out.alpha[side] = alpha;
    out.gamma2[side] = gamma2;
    out.gamma[side] = gamma;
    const auto f1 = alpha * rho + gamma * d[2], g1 = gamma * rho - alpha * d[2],
               f2 = (one - alpha) * rho - gamma * d[2],
               g2 = -((one - alpha) * d[2] + gamma * rho);
    const auto theta1 = angle(f1, g1), theta2 = angle(f2, g2),
               phi = angle(-d[1], d[0]),
               delta = out.sole_yaw[side] - out.root_yaw;
    out.angles[side] = {
        theta1,  theta2, theta1 - theta2, delta, literal(sign) * phi,
        -theta2, -phi};
    const auto hip2 = delta.first * delta.first + phi.first * phi.first +
                      theta1.first * theta1.first +
                      2 * delta.first * theta1.first * std::sin(phi.value);
    out.speeds[side] = {std::sqrt(std::max(0.L, hip2)),
                        std::abs(theta1.first - theta2.first),
                        std::hypot(theta2.first, phi.first)};
    p[base + 4] = add(
        p[0], transform(out.frames[1], constant_point(sign * .20265, .579, 0)));
    p[base + 5] =
        add(p[base + 4], transform(out.frames[1], Point{literal(0), arm, arm}));
    p[base + 6] =
        add(p[base + 5], transform(out.frames[1], constant_point(0, .386, 0)));
  }
  for (std::size_t i = 0; i < 15; ++i) {
    const auto& m = masses[i];
    out.mass_points[i] = scale(add(p[m.first], p[m.second]), literal(.5));
    out.com = add(out.com, scale(out.mass_points[i],
                                 literal(static_cast<double>(m.weight))));
  }
  out.com = scale(out.com, literal(1) / literal(1200));
  return out;
}
auto component(RigidVector3 p, std::size_t i) -> double {
  return i == 0 ? p.x : i == 1 ? p.y : p.z;
}
auto contains(BoardingPlantedLegScalarBounds b, long double x) -> bool {
  // Independent long-double corroboration tolerance is not producer permission.
  const auto allowance = 2e-12L * std::max(1.L, std::abs(x));
  return std::isfinite(b.lower) && std::isfinite(b.upper) &&
         b.lower <= b.upper && x >= b.lower - allowance &&
         x <= b.upper + allowance;
}
void point_contains(const BoardingPlantedBodyPointEvidence& b, const Point& p) {
  for (std::size_t i = 0; i < 3; ++i) {
    check(contains({component(b.value.lower, i), component(b.value.upper, i)},
                   p[i].value),
          "Independent point value enclosed");
    check(contains({component(b.derivatives.velocity.lower, i),
                    component(b.derivatives.velocity.upper, i)},
                   p[i].first),
          "Independent physical point first derivative enclosed");
    check(contains({component(b.derivatives.acceleration.lower, i),
                    component(b.derivatives.acceleration.upper, i)},
                   p[i].second),
          "Independent physical point second derivative enclosed");
  }
}
auto physical_norm(const Point& p, bool acceleration = false) -> long double {
  long double squared{};
  for (const auto& a : p) {
    const auto v = acceleration ? a.second : a.first;
    squared += v * v;
  }
  return std::sqrt(squared);
}
void timing_oracle(const Cell& c, const Oracle& o) {
  check(contains(c.root_speed, physical_norm(o.points[0])) &&
            contains(c.root_acceleration, physical_norm(o.points[0], true)),
        "Independent root physical vector norms enclosed");
  check(contains(c.root_yaw_speed, std::abs(o.root_yaw.first)) &&
            contains(c.torso_joint_speed, std::abs(o.torso.first)),
        "Independent actual root and torso angular speeds enclosed");
  check(
      contains(c.port_reaction_fraction, o.reaction.value),
      "Declared interpolated reaction control enclosed without load authority");
  for (std::size_t side = 0; side < 2; ++side) {
    const auto center = physical_norm(o.soles[side]),
               spin = std::abs(o.sole_yaw[side].first),
               whole = center + std::hypot(.06L, .14L) * spin;
    check(contains(c.sole_center_speed[side], center) &&
              contains(c.sole_yaw_speed[side], spin) &&
              contains(c.whole_sole_speed[side], whole),
          "Moving whole sole includes translation and corner-radius rotation");
    const auto base = side == 0 ? 4U : 11U;
    const auto thigh = subtract(o.points[base + 1], o.points[base]),
               shin = subtract(o.points[base + 2], o.points[base + 1]);
    check(std::abs(std::sqrt(dot(thigh, thigh).value) - .47285L) < 2e-15L &&
              std::abs(std::sqrt(dot(shin, shin).value) - .47478L) < 2e-15L,
          "Independent actual three-dimensional rods retain both original "
          "lengths");
  }
}
void accepted_timing(const Cell& c) {
  const auto angular = std::numbers::pi / 6;
  check(c.root_speed.upper <= .25 && c.root_acceleration.upper <= .10 &&
            c.root_yaw_speed.upper <= angular &&
            c.torso_joint_speed.upper <= angular,
        "Accepted root and torso respect registered physical timing limits");
  for (std::size_t side = 0; side < 2; ++side) {
    check(c.whole_sole_speed[side].upper <= .35,
          "Accepted full sole respects translation plus rotation limit");
    for (auto b : c.legs[side].joint_speeds)
      check(b.upper <= angular,
            "Accepted physical joint resultant respects angular limit");
    for (auto b : c.legs[side].sector_margins)
      check(b.lower >= 0,
            "Accepted original directed sector margins are nonnegative");
  }
}
void zero_endpoint_jets(const Cell& c) {
  for (const auto& p : c.points)
    for (const auto& b :
         std::array{p.derivatives.velocity, p.derivatives.acceleration})
      for (std::size_t axis = 0; axis < 3; ++axis)
        check(component(b.lower, axis) == 0 && component(b.upper, axis) == 0,
              "Registered C2 carrier/hump endpoint has exact structural zero "
              "physical body jets");
}
void cell_oracle(const Request& r, const Cell& c, bool reverse, double u) {
  const auto o = oracle(r, u, reverse);
  timing_oracle(c, o);
  for (std::size_t i = 0; i < 18; ++i)
    point_contains(c.points[i], o.points[i]);
  for (std::size_t i = 0; i < 15; ++i)
    point_contains(c.mass_points[i], o.mass_points[i]);
  point_contains(c.center_of_mass, o.com);
  for (std::size_t f = 0; f < 4; ++f)
    for (std::size_t col = 0; col < 3; ++col)
      point_contains(
          c.frames[f].columns[col],
          {o.frames[f][0][col], o.frames[f][1][col], o.frames[f][2][col]});
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& l = c.legs[side];
    check(contains(l.distance_squared, o.D[side].value) &&
              contains(l.rho_squared, o.rho2[side].value) &&
              contains(l.alpha, o.alpha[side].value) &&
              contains(l.gamma_squared, o.gamma2[side].value) &&
              contains(l.gamma, o.gamma[side].value),
          "Independent unfactored sphere/plane graph enclosed");
    const std::array jets{l.hip_pitch, l.shin_pitch,    l.knee_flex,
                          l.hip_axial, l.hip_abduction, l.ankle_pitch,
                          l.ankle_roll};
    for (std::size_t j = 0; j < jets.size(); ++j)
      check(contains(jets[j].rate, o.angles[side][j].first) &&
                contains(jets[j].coordinate_second, o.angles[side][j].second),
            "Every actual angle first/second enclosed");
    for (std::size_t j = 0; j < 3; ++j)
      check(contains(l.joint_speeds[j], o.speeds[side][j]),
            "Resultant joint speed including hip cross term enclosed");
  }
}
using Diagnostic = BoardingRouteIntermediateStepDiagnostic;
using Limits = detail::BoardingRouteIntermediateStepLimits;
using Condition = BoardingRouteFootPhaseCondition;
auto same_constant(const Constant& a, const Constant& b) -> bool {
  return a.count == b.count && a.terms == b.terms;
}
auto same_point(const ConstantPoint& a, const ConstantPoint& b) -> bool {
  for (std::size_t i = 0; i < 3; ++i)
    if (!same_constant(a.coordinates[i], b.coordinates[i])) return false;
  return true;
}
auto same_request(const Request& a, const Request& b) -> bool {
  if (a.root_yaw_half != b.root_yaw_half ||
      a.torso_lean_half != b.torso_lean_half ||
      a.seconds_per_parameter != b.seconds_per_parameter ||
      a.port_reaction_fraction != b.port_reaction_fraction)
    return false;
  for (std::size_t i = 0; i < 2; ++i) {
    if (!same_point(a.root[i], b.root[i]) ||
        a.feet[i].yaw_half != b.feet[i].yaw_half ||
        a.feet[i].swing_height_metres != b.feet[i].swing_height_metres)
      return false;
    for (std::size_t j = 0; j < 2; ++j)
      if (!same_point(a.feet[i].sole[j], b.feet[i].sole[j])) return false;
  }
  return true;
}
auto expected_constant(std::initializer_list<double> terms) -> Constant {
  Constant c{};
  c.count = static_cast<std::uint8_t>(terms.size());
  std::copy(terms.begin(), terms.end(), c.terms.begin());
  return c;
}
auto expected_point(Constant x, Constant y, Constant z) -> ConstantPoint {
  return {{x, y, z}};
}
auto expected_controls() -> std::array<Request, 4> {
  const auto C = expected_point(expected_constant({.16, .16}),
                                expected_constant({.847, .012}),
                                expected_constant({-.55, -.17}));
  const auto E = expected_point(expected_constant({.16, .16, -.0075}),
                                expected_constant({.847, .012, -.359}),
                                expected_constant({-.55, -.17, .19}));
  const auto U =
      expected_point(expected_constant({.16, -.14}),
                     expected_constant({static_cast<double>(-100000) * 1.e-6}),
                     expected_constant({-.5}));
  const auto H = expected_point(expected_constant({.16, -.14, .16}),
                                U.coordinates[1], expected_constant({-1.14}));
  const auto I =
      expected_point(H.coordinates[0],
                     expected_constant({static_cast<double>(-230000) * 1.e-6}),
                     H.coordinates[2]);
  const auto star =
      expected_point(expected_constant({.16, .14}),
                     expected_constant({static_cast<double>(-160000) * 1.e-6}),
                     expected_constant({-.8}));
  std::array<Request, 4> r{};
  for (auto& q : r) {
    q.root = {E, E};
    q.root_yaw_half = {.125, .125};
    q.torso_lean_half = {-.25, -.25};
    q.feet[0].sole = {I, I};
    q.feet[1].sole = {star, star};
    q.seconds_per_parameter = 12;
    q.port_reaction_fraction = {0, 0};
  }
  r[0].root = {C, E};
  r[0].root_yaw_half = {.25, .125};
  r[0].torso_lean_half = {0, -.25};
  r[0].feet[0].sole = {U, H};
  r[0].feet[0].swing_height_metres = .010;
  r[1].feet[0].sole = {H, I};
  r[2].torso_lean_half = {-.25, -.30};
  r[2].port_reaction_fraction = {0, .25};
  r[3].torso_lean_half = {-.30, -.30};
  r[3].port_reaction_fraction = {.25, .25};
  r[3].seconds_per_parameter = 2;
  return r;
}
auto clock_oracle(double g) -> double {
  if (g <= .25) return 48 * g;
  if (g <= .5) return 12 + 12 * (4 * g - 1);
  if (g <= .75) return 24 + 12 * (4 * g - 2);
  return 36 + 2 * (4 * g - 3);
}
void controls_and_maps() {
  const auto expected = expected_controls();
  for (std::size_t i = 0; i < 4; ++i) {
    const auto r = detail::boarding_route_intermediate_step_controls(i);
    check(r && same_request(*r, expected[i]),
          "Frozen four-phase controls preserve every original ordered "
          "term/duration/foot/yaw/hump/share");
    check(detail::boarding_route_intermediate_step_phase(
              static_cast<double>(i) / 4, static_cast<double>(i + 1) / 4) == i,
          "Positive-width quarter belongs to its registered phase");
    for (double u : std::array{0., .125, .5, .875, 1.}) {
      const auto g = (static_cast<double>(i) + u) / 4;
      check(detail::boarding_route_intermediate_step_local(i, g) == u,
            "Global quarter mapping retains exact local parameter");
      check(detail::boarding_route_intermediate_step_clock(g) ==
                clock_oracle(g),
            "Piecewise physical clock preserves12/12/12/2 instead of uniform38 "
            "seconds");
    }
    if (i < 3) {
      check(detail::boarding_route_intermediate_step_join_math(expected[i],
                                                               expected[i + 1]),
            "Matching endpoint expressions qualify structural physical C2 "
            "across different durations");
      auto altered = expected[i + 1];
      altered.root[0].coordinates[0].terms[0] += .001;
      check(!detail::boarding_route_intermediate_step_join_math(expected[i],
                                                                altered),
            "Changed root endpoint cannot spoof a structural join");
      altered = expected[i + 1];
      altered.feet[0].sole[0].coordinates[2].terms[0] += .001;
      check(!detail::boarding_route_intermediate_step_join_math(expected[i],
                                                                altered),
            "Changed sole endpoint cannot spoof acquisition join");
      altered = expected[i + 1];
      altered.port_reaction_fraction[0] += .01;
      check(!detail::boarding_route_intermediate_step_join_math(expected[i],
                                                                altered),
            "Reaction jump cannot be admitted as C2 load control");
    }
  }
  check(!detail::boarding_route_intermediate_step_controls(4) &&
            !detail::boarding_route_intermediate_step_controls(
                std::numeric_limits<std::size_t>::max()),
        "Past-end immutable phase lookup refuses");
  for (double join : std::array{.25, .5, .75}) {
    const auto preceding = static_cast<std::size_t>(4 * join) - 1;
    check(detail::boarding_route_intermediate_step_phase(join, join) ==
              preceding,
          "Exact point join canonically belongs to preceding phase");
    check(detail::boarding_route_intermediate_step_phase(
              join, std::nextafter(join, 1.)) == preceding + 1,
          "Positive interval starting at join belongs to following phase");
    check(!detail::boarding_route_intermediate_step_phase(
              std::nextafter(join, 0.), std::nextafter(join, 1.)),
          "Cross-event cell cannot enter a single-phase predicate");
  }
  const auto old = detail::boarding_route_checkpoint_unload_controls(2);
  check(old && same_point(old->root[1], expected[0].root[0]) &&
            old->root_yaw_half[1] == expected[0].root_yaw_half[0] &&
            old->torso_lean_half[1] == expected[0].torso_lean_half[0] &&
            old->port_reaction_fraction[1] ==
                expected[0].port_reaction_fraction[0],
        "Continuation starts at authentic original C/zero-port checkpoint "
        "expression");
  if (old)
    for (std::size_t side = 0; side < 2; ++side)
      check(
          same_point(old->feet[side].sole[1], expected[0].feet[side].sole[0]) &&
              old->feet[side].yaw_half[1] == expected[0].feet[side].yaw_half[0],
          "Original upper/transition sole identity is preserved at "
          "continuation start");
}
void denied(const Diagnostic& d) {
  check(!d.self_qualified && !d.source_qualified && !d.material_qualified &&
            !d.world_qualified && !d.support_qualified && !d.load_qualified &&
            !d.actor_qualified && !d.route_qualified && !d.seat_qualified &&
            !d.save_qualified && !d.dynamics_qualified,
        "Kinematic preflight has no "
        "source/contact/self/material/WORLD/gameplay authority");
}
void log(std::string_view name, const Diagnostic& d) {
  std::cout << std::setprecision(17) << name << " complete=" << d.complete
            << " reverse=" << d.reverse << " request=" << d.requested_first
            << ',' << d.requested_last
            << " elapsed=" << d.reporting_elapsed_seconds
            << " arithmetic=" << d.arithmetic_supported
            << " cells=" << d.cells.size() << " nodes=" << d.examined_nodes
            << " mandatory=" << d.mandatory_splits
            << " depth=" << d.maximum_depth
            << " output=" << d.output_capacity_bytes
            << " work=" << d.work.graphs << ',' << d.work.legs << ','
            << d.work.bodies << ',' << d.work.sectors << ',' << d.work.timing
            << " joins=" << d.qualified_joins[0] << ',' << d.qualified_joins[1]
            << ',' << d.qualified_joins[2];
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    std::cout << " condition=" << static_cast<unsigned>(r.condition)
              << " predicate=" << static_cast<unsigned>(r.predicate_condition)
              << " interval=" << r.first << ',' << r.last
              << " local=" << r.local_first << ',' << r.local_last
              << " refusal_depth=" << r.depth;
    if (r.phase_index) std::cout << " phase=" << *r.phase_index;
    if (r.side) std::cout << " side=" << *r.side;
    std::cout << " bound=" << r.limiting_bound.lower << ','
              << r.limiting_bound.upper;
  }
  std::cout << '\n' << std::flush;
}
void original_parts(const Diagnostic& d) {
  for (std::size_t i = 0; i < 15; ++i) {
    const auto& p = d.parts[i];
    const auto& m = masses[i];
    check(
        static_cast<std::size_t>(p.id) == i &&
            static_cast<std::size_t>(p.mass.first) == m.first &&
            static_cast<std::size_t>(p.mass.second) == m.second &&
            p.mass.weight == m.weight,
        "Every original part and1200 total nominal mass binding is preserved");
    if (const auto* b =
            std::get_if<BoardingRoutePhaseBoxBinding>(&p.reservation)) {
      const RigidVector3 h = i == 0   ? RigidVector3{.24, .12, .18}
                             : i == 1 ? RigidVector3{.26, .2695, .18}
                             : i == 2 ? RigidVector3{.16, .18, .18}
                             : (i == 5 || i == 11)
                                 ? RigidVector3{.06, .05, .14}
                                 : RigidVector3{.04, .05, .02};
      check(b->half_size_metres == h,
            "All seven original WORLD boxes retain full rigid dimensions");
    } else {
      const auto& c =
          std::get<BoardingPlantedBodyCapsuleBinding>(p.reservation);
      const auto k = (i - 3) % 6;
      check(c.radius_metres == (k == 0   ? .105
                                : k == 1 ? .075
                                : k == 3 ? .065
                                         : .055),
            "All eight original WORLD capsules retain original radii");
    }
  }
}
void report(const Diagnostic& d, Limits l = {}) {
  denied(d);
  original_parts(d);
  const auto& caps = l.phase;
  check(d.examined_nodes <= caps.nodes && d.maximum_depth <= caps.depth &&
            d.cells.size() <= caps.leaves &&
            (d.output_capacity_bytes <= caps.output_bytes ||
             (d.cells.empty() && d.first_refusal &&
              d.first_refusal->condition == Condition::output_capacity)) &&
            d.work.graphs <= caps.graphs && d.work.legs <= caps.legs &&
            d.work.bodies <= caps.bodies && d.work.sectors <= caps.sectors &&
            d.work.timing <= caps.timing,
        "One shared global cover and actual work remain below all lowered "
        "ceilings");
  check(d.mandatory_splits <= d.examined_nodes &&
            d.work.graphs <= d.examined_nodes - d.mandatory_splits &&
            d.work.legs <= 2 * d.work.graphs &&
            d.work.bodies <= d.work.graphs &&
            d.work.sectors <= 3 * d.work.graphs &&
            d.work.timing <= 6 * d.work.graphs,
        "Counted navigation nodes do not acquire graph work or reset aggregate "
        "phase counters");
  const auto expected = expected_controls();
  for (std::size_t phase = 0; phase < 4; ++phase)
    check(same_request(d.controls[phase], expected[phase]),
          "Every report owns the frozen immutable numerical recipe once");
  auto cursor = std::min(d.requested_first, d.requested_last);
  for (const auto& cell : d.cells) {
    check(cell.global_first == cursor &&
              cell.global_first <= cell.global_last && cell.phase_index < 4 &&
              cell.phase.complete,
          "Accepted cells retain a closed contiguous genuine global prefix");
    if (cell.phase_index >= 4) continue;
    check(detail::boarding_route_intermediate_step_phase(
              cell.global_first, cell.global_last) == cell.phase_index &&
              cell.phase.first == 4 * cell.global_first -
                                      static_cast<double>(cell.phase_index) &&
              cell.phase.last ==
                  4 * cell.global_last - static_cast<double>(cell.phase_index),
          "No accepted cell crosses a contact event or uses the wrong "
          "global/local phase mapping");
    cursor = cell.global_last;
    check(cell.phase.arithmetic_supported && cell.phase.nominal_links &&
              cell.phase.target_sole_identities && cell.phase.joint_sectors &&
              cell.phase.derivative_domains && cell.phase.timing_complete,
          "Every appended interval has all genuine numerical graph predicates");
    accepted_timing(cell.phase);
    for (double u : std::array{cell.phase.first,
                               cell.phase.first +
                                   (cell.phase.last - cell.phase.first) * .5,
                               cell.phase.last})
      cell_oracle(d.controls[cell.phase_index], cell.phase, d.reverse, u);
    if (cell.phase.first == cell.phase.last &&
        (cell.phase.first == 0 || cell.phase.first == 1))
      zero_endpoint_jets(cell.phase);
    if (cell.phase_index < 2)
      check(cell.phase.port_reaction_fraction.lower == 0 &&
                cell.phase.port_reaction_fraction.upper == 0,
            "Release and descent preserve exact zero-port declaration without "
            "pressure or contact authority");
    check(cell.phase.sole_center_speed[1].lower == 0 &&
              cell.phase.sole_center_speed[1].upper == 0 &&
              cell.phase.sole_yaw_speed[1].lower == 0 &&
              cell.phase.sole_yaw_speed[1].upper == 0,
          "Original star sole remains exactly fixed even while root and port "
          "move");
    if (cell.phase_index == 3) {
      check(cell.phase.port_reaction_fraction.lower == .25 &&
                cell.phase.port_reaction_fraction.upper == .25,
            "Pause retains declared nonzero port share without implying "
            "supported load");
      zero_endpoint_jets(cell.phase);
    }
  }
  if (d.complete)
    check(!d.first_refusal &&
              cursor == std::max(d.requested_first, d.requested_last) &&
              d.arithmetic_supported && d.nominal_links &&
              d.target_sole_identities && d.joint_sectors &&
              d.derivative_domains && d.timing_complete,
          "Completed preflight retains exact entire requested interval and "
          "aggregate graph predicates");
  else
    check(d.first_refusal.has_value(),
          "Every incomplete new candidate retains an explicit real refusal");
  check(d.reporting_elapsed_seconds ==
            std::abs(clock_oracle(d.requested_last) -
                     clock_oracle(d.requested_first)),
        "Elapsed duration follows original piecewise physical clock even for "
        "reverse/subrange");
  for (std::size_t i = 0; i < 3; ++i) {
    check(d.join_expression_identity[i],
          "Frozen neighboring endpoint expressions retain structural C2 "
          "identity");
    bool reached = false;
    for (std::size_t j = 1; j < d.cells.size(); ++j) {
      const auto& a = d.cells[j - 1];
      const auto& b = d.cells[j];
      const auto join = static_cast<double>(i + 1) / 4;
      if (a.phase_index == i && b.phase_index == i + 1 &&
          a.global_last == join && b.global_first == join &&
          a.global_first < a.global_last && b.global_first < b.global_last)
        reached = true;
    }
    check(d.qualified_joins[i] == reached,
          "Physical join qualifies only from actual accepted two-sided prefix "
          "coverage");
  }
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    check(r.first <= r.last && r.first >= cursor &&
              r.first >= std::min(d.requested_first, d.requested_last) &&
              r.last <= std::max(d.requested_first, d.requested_last) &&
              r.depth <= caps.depth,
          "Failure retains real global prefix successor and shared depth");
    if (r.phase_index) {
      check(*r.phase_index < 4 &&
                r.local_first ==
                    4 * r.first - static_cast<double>(*r.phase_index) &&
                r.local_last ==
                    4 * r.last - static_cast<double>(*r.phase_index),
            "Predicate refusal binds original phase and local interval without "
            "reinterpretation");
    }
  }
}
auto snapshot(const Cell& c) -> std::vector<std::uint64_t> {
  std::vector<std::uint64_t> out;
  auto scalar = [&](double x) {
    out.push_back(std::bit_cast<std::uint64_t>(x));
  };
  auto bounds = [&](BoardingPlantedLegScalarBounds b) {
    scalar(b.lower);
    scalar(b.upper);
  };
  auto pt = [&](const BoardingPlantedBodyPointEvidence& p) {
    for (const auto& b : std::array{p.value, p.derivatives.velocity,
                                    p.derivatives.acceleration})
      for (auto v : std::array{b.lower, b.upper})
        for (std::size_t a = 0; a < 3; ++a)
          scalar(component(v, a));
  };
  scalar(c.first);
  scalar(c.last);
  for (const auto& p : c.points)
    pt(p);
  for (const auto& p : c.mass_points)
    pt(p);
  pt(c.center_of_mass);
  for (const auto& f : c.frames)
    for (const auto& p : f.columns)
      pt(p);
  for (const auto& l : c.legs) {
    for (auto b : std::array{l.distance_squared, l.rho_squared, l.gamma_squared,
                             l.alpha, l.gamma})
      bounds(b);
    for (auto j :
         std::array{l.hip_pitch, l.shin_pitch, l.knee_flex, l.hip_axial,
                    l.hip_abduction, l.ankle_pitch, l.ankle_roll}) {
      bounds(j.rate);
      bounds(j.coordinate_second);
    }
    for (auto b : l.joint_speeds)
      bounds(b);
    for (auto b : l.sector_margins)
      bounds(b);
    for (bool b :
         std::array{l.nominal_links, l.target_sole_identity,
                    l.derivative_domains, l.joint_sectors, l.timing_complete})
      out.push_back(b);
  }
  for (auto b : std::array{c.root_speed, c.root_acceleration, c.root_yaw_speed,
                           c.torso_joint_speed, c.port_reaction_fraction})
    bounds(b);
  for (auto b : c.sole_center_speed)
    bounds(b);
  for (auto b : c.sole_yaw_speed)
    bounds(b);
  for (auto b : c.whole_sole_speed)
    bounds(b);
  for (bool b : std::array{c.arithmetic_supported, c.nominal_links,
                           c.target_sole_identities, c.joint_sectors,
                           c.derivative_domains, c.timing_complete, c.complete})
    out.push_back(b);
  return out;
}

void invalid_and_environment() {
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto inf = std::numeric_limits<double>::infinity();
  for (const auto range : std::array{
           std::array{nan, 0.}, std::array{0., nan}, std::array{inf, 0.},
           std::array{0., -inf}, std::array{-.001, 1.}, std::array{0., 1.001}})
    check(!assess_origin_boarding_route_intermediate_step(range[0], range[1]),
          "Nonfinite/outside global requests refuse before candidate geometry");
  for (double x : std::array{nan, inf, -inf, -.001, 1.001}) {
    check(std::isnan(detail::boarding_route_intermediate_step_clock(x)),
          "Invalid global clock has no finite duration");
    check(!detail::boarding_route_intermediate_step_phase(x, x),
          "Invalid global point has no phase");
    check(std::isnan(detail::boarding_route_intermediate_step_local(0, x)),
          "Invalid local mapping refuses");
  }
  check(std::isnan(detail::boarding_route_intermediate_step_local(4, 0)),
        "Unknown phase cannot create a local clock");
  for (std::size_t field = 0; field < 9; ++field) {
    Limits l;
    switch (field) {
      case 0: ++l.phase.depth; break;
      case 1: ++l.phase.nodes; break;
      case 2: ++l.phase.leaves; break;
      case 3: ++l.phase.output_bytes; break;
      case 4: ++l.phase.graphs; break;
      case 5: ++l.phase.legs; break;
      case 6: ++l.phase.bodies; break;
      case 7: ++l.phase.sectors; break;
      default: ++l.phase.timing; break;
    }
    check(!detail::boarding_route_intermediate_step_bounded(0, 1, l),
          "Private capacities cannot raise registered ceilings");
  }
  const auto saved = std::fegetround();
  for (int mode : std::array{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    const auto set = std::fesetround(mode);
    check(set == 0, "Test controls installed non-nearest rounding");
    if (set != 0) continue;
    auto raw = assess_origin_boarding_route_intermediate_step();
    std::fesetround(saved);
    if (!raw) {
      check(false, "Unsafe environment returns honest diagnostic rather than "
                   "input API error");
      continue;
    }
    check(!raw->arithmetic_supported && !raw->complete && raw->cells.empty() &&
              raw->work.graphs == 0 && raw->first_refusal &&
              raw->first_refusal->condition ==
                  Condition::unsupported_arithmetic,
          "Unsafe arithmetic refuses before any graph and grants no authority");
    denied(*raw);
  }
  std::fesetround(saved);
#if defined(__SSE2__) && defined(__x86_64__)
  const auto mxcsr = _mm_getcsr();
  for (unsigned bit :
       std::array{unsigned{1} << 15, unsigned{1} << 6, unsigned{1} << 13}) {
    _mm_setcsr(mxcsr | bit);
    auto raw = assess_origin_boarding_route_intermediate_step();
    _mm_setcsr(mxcsr);
    check(
        raw && !raw->arithmetic_supported && !raw->complete &&
            raw->cells.empty() && raw->work.graphs == 0 && raw->first_refusal &&
            raw->first_refusal->condition == Condition::unsupported_arithmetic,
        "FTZ/DAZ/MXCSR non-nearest cannot certify the graph");
  }
#endif
  for (std::size_t field = 0; field < 5; ++field) {
    Limits l;
    Condition expected{};
    switch (field) {
      case 0:
        l.phase.nodes = 0;
        expected = Condition::node_capacity;
        break;
      case 1:
        l.phase.leaves = 0;
        expected = Condition::leaf_capacity;
        break;
      case 2:
        l.phase.depth = 0;
        expected = Condition::depth_capacity;
        break;
      case 3:
        l.phase.output_bytes = 0;
        expected = Condition::output_capacity;
        break;
      default:
        l.phase.graphs = 0;
        expected = Condition::graph_capacity;
        break;
    }
    const auto d =
        require(detail::boarding_route_intermediate_step_bounded(0, 1, l));
    check(!d.complete && d.first_refusal &&
              d.first_refusal->condition == expected && d.cells.empty() &&
              d.work.graphs == 0,
          "Global navigation/allocation/graph capacities refuse at genuine "
          "reached stage without pose assumptions");
    denied(d);
  }
}
void local_replay(const Diagnostic& d) {
  for (const auto& cell : d.cells) {
    Cell actual;
    BoardingRouteFootPhaseCounters work;
    BoardingRouteFootPhaseRefusal refusal;
    const auto result = detail::boarding_route_foot_phase_cell(
        d.controls[cell.phase_index], cell.phase.first, cell.phase.last,
        d.reverse, {}, work, actual, refusal);
    check(result == detail::BoardingRouteFootPhaseCellResult::accepted &&
              snapshot(actual) == snapshot(cell.phase),
          "Actual retained interval replays fieldwise through unchanged phase "
          "graph with same physical clock");
  }
}
void point_budgets(const Diagnostic& base) {
  if (!base.complete) return;
  Limits exact;
  exact.phase.nodes = base.examined_nodes;
  exact.phase.leaves = base.cells.size();
  exact.phase.output_bytes = base.output_capacity_bytes;
  exact.phase.graphs = base.work.graphs;
  exact.phase.legs = base.work.legs;
  exact.phase.bodies = base.work.bodies;
  exact.phase.sectors = base.work.sectors;
  exact.phase.timing = base.work.timing;
  const auto replay = require(detail::boarding_route_intermediate_step_bounded(
      base.requested_first, base.requested_last, exact));
  check(replay.complete && replay.cells.size() == base.cells.size(),
        "Exact actual used capacities preserve complete point");
  if (!replay.cells.empty())
    check(snapshot(replay.cells.front().phase) ==
              snapshot(base.cells.front().phase),
          "Exact budget point replay preserves full fieldwise evidence");
  for (std::size_t field = 0; field < 8; ++field) {
    auto l = exact;
    Condition expected{};
    bool used = true;
    switch (field) {
      case 0:
        used = l.phase.nodes > 0;
        if (used) --l.phase.nodes;
        expected = Condition::node_capacity;
        break;
      case 1:
        used = l.phase.leaves > 0;
        if (used) --l.phase.leaves;
        expected = Condition::leaf_capacity;
        break;
      case 2:
        used = l.phase.output_bytes > 0;
        if (used) --l.phase.output_bytes;
        expected = Condition::output_capacity;
        break;
      case 3:
        used = l.phase.graphs > 0;
        if (used) --l.phase.graphs;
        expected = Condition::graph_capacity;
        break;
      case 4:
        used = l.phase.legs > 0;
        if (used) --l.phase.legs;
        expected = Condition::leg_capacity;
        break;
      case 5:
        used = l.phase.bodies > 0;
        if (used) --l.phase.bodies;
        expected = Condition::body_capacity;
        break;
      case 6:
        used = l.phase.sectors > 0;
        if (used) --l.phase.sectors;
        expected = Condition::sector_capacity;
        break;
      default:
        used = l.phase.timing > 0;
        if (used) --l.phase.timing;
        expected = Condition::timing_capacity;
        break;
    }
    if (!used) continue;
    const auto r = require(detail::boarding_route_intermediate_step_bounded(
        base.requested_first, base.requested_last, l));
    check(!r.complete && r.first_refusal &&
              r.first_refusal->condition == expected,
          "One less actually used capacity reaches its own honest refusal "
          "without an earlier pose mask");
    denied(r);
  }
}
// These expectations were recorded only after frozen b8151cc FIRST observations
// agreed byte-for-byte on GCC and Clang20. They preserve the candidate's
// refusal.
void observed_counts(const Diagnostic& d, bool complete, std::size_t cells,
                     std::size_t nodes, std::size_t navigation,
                     std::size_t depth, std::size_t output,
                     BoardingRouteFootPhaseCounters work,
                     std::array<bool, 3> joins) {
  check(d.complete == complete && d.cells.size() == cells &&
            d.examined_nodes == nodes && d.mandatory_splits == navigation &&
            d.maximum_depth == depth && d.output_capacity_bytes == output &&
            d.work.graphs == work.graphs && d.work.legs == work.legs &&
            d.work.bodies == work.bodies && d.work.sectors == work.sectors &&
            d.work.timing == work.timing && d.qualified_joins == joins,
        "Frozen first observation retains actual outcome, shared work, "
        "allocated output, and earned joins");
  check(d.arithmetic_supported &&
            (complete ? !d.first_refusal : d.first_refusal.has_value()),
        "Recorded numerical completeness/refusal remains explicit and "
        "arithmetic supported");
}
void observed_release_refusal(const Diagnostic& d, bool standalone) {
  observed_counts(d, false, standalone ? 7 : 3, standalone ? 21 : 15,
                  standalone ? 0 : 2, 10, 7950176,
                  standalone
                      ? BoardingRouteFootPhaseCounters{21, 28, 7, 34, 42}
                      : BoardingRouteFootPhaseCounters{13, 16, 3, 18, 18},
                  {false, false, false});
  if (!d.first_refusal) return;
  const auto& r = *d.first_refusal;
  const auto first = standalone ? .01123046875 : .009765625;
  const auto last = standalone ? .011474609375 : .0107421875;
  check(r.condition == Condition::depth_capacity &&
            r.predicate_condition == Condition::joint_sector &&
            r.phase_index == 0 && r.side == 0 && r.depth == 10 &&
            r.first == first && r.last == last && r.local_first == 4 * first &&
            r.local_last == 4 * last && !d.cells.empty() &&
            d.cells.back().global_last == first && r.limiting_bound.lower < 0 &&
            r.limiting_bound.upper > 0,
        "Original phase-zero port sector refusal retains exact canonical "
        "closed interval and accepted prefix");
}
void complete_subrange_budgets(const Diagnostic& base) {
  if (!base.complete) return;
  Limits exact;
  exact.phase.depth = base.maximum_depth;
  exact.phase.nodes = base.examined_nodes;
  exact.phase.leaves = base.cells.size();
  exact.phase.output_bytes = base.output_capacity_bytes;
  exact.phase.graphs = base.work.graphs;
  exact.phase.legs = base.work.legs;
  exact.phase.bodies = base.work.bodies;
  exact.phase.sectors = base.work.sectors;
  exact.phase.timing = base.work.timing;
  const auto replay = require(
      detail::boarding_route_intermediate_step_bounded(.125, .875, exact));
  log("EXACT_SHARED_SUBRANGE", replay);
  report(replay, exact);
  check(replay.complete && replay.cells.size() == base.cells.size() &&
            replay.qualified_joins == base.qualified_joins &&
            replay.examined_nodes == base.examined_nodes &&
            replay.mandatory_splits == base.mandatory_splits &&
            replay.work.graphs == base.work.graphs &&
            replay.work.legs == base.work.legs &&
            replay.work.bodies == base.work.bodies &&
            replay.work.sectors == base.work.sectors &&
            replay.work.timing == base.work.timing,
        "Exact actual complete-cover budgets replay all four phases and three "
        "shared joins without resetting work");
  for (std::size_t i = 0; i < std::min(replay.cells.size(), base.cells.size());
       ++i)
    check(
        replay.cells[i].phase_index == base.cells[i].phase_index &&
            replay.cells[i].global_first == base.cells[i].global_first &&
            replay.cells[i].global_last == base.cells[i].global_last &&
            snapshot(replay.cells[i].phase) == snapshot(base.cells[i].phase),
        "Reduced allocation preserves every original accepted cell fieldwise");
  exact.phase.output_bytes = replay.output_capacity_bytes;
  const auto tight = require(
      detail::boarding_route_intermediate_step_bounded(.125, .875, exact));
  check(tight.complete &&
            tight.output_capacity_bytes == exact.phase.output_bytes &&
            tight.cells.size() == base.cells.size(),
        "Exact measured allocation fits complete shared cover including joins");
  for (std::size_t field = 0; field < 9; ++field) {
    auto l = exact;
    Condition expected{};
    switch (field) {
      case 0:
        --l.phase.depth;
        expected = Condition::depth_capacity;
        break;
      case 1:
        --l.phase.nodes;
        expected = Condition::node_capacity;
        break;
      case 2:
        --l.phase.leaves;
        expected = Condition::leaf_capacity;
        break;
      case 3:
        --l.phase.output_bytes;
        expected = Condition::output_capacity;
        break;
      case 4:
        --l.phase.graphs;
        expected = Condition::graph_capacity;
        break;
      case 5:
        --l.phase.legs;
        expected = Condition::leg_capacity;
        break;
      case 6:
        --l.phase.bodies;
        expected = Condition::body_capacity;
        break;
      case 7:
        --l.phase.sectors;
        expected = Condition::sector_capacity;
        break;
      default:
        --l.phase.timing;
        expected = Condition::timing_capacity;
        break;
    }
    const auto r = require(
        detail::boarding_route_intermediate_step_bounded(.125, .875, l));
    log("ONE_LESS_SHARED_" + std::to_string(field), r);
    report(r, l);
    check(!r.complete && r.first_refusal &&
              r.first_refusal->condition == expected,
          "One less measured complete-cover capacity refuses its own stage "
          "across shared phase/physical joins");
  }
}
void observations() {
  auto raw = assess_origin_boarding_route_intermediate_step();
  if (!raw)
    std::cout << "FIRST_PUBLIC_INTERMEDIATE_STEP api_error=" << raw.error()
              << '\n'
              << std::flush;
  else
    log("FIRST_PUBLIC_INTERMEDIATE_STEP", *raw);
  const auto whole = require(std::move(raw));
  report(whole);
  observed_release_refusal(whole, false);
  const auto reverse =
      require(assess_origin_boarding_route_intermediate_step(1, 0));
  log("FIRST_REVERSE", reverse);
  report(reverse);
  observed_release_refusal(reverse, false);
  check(reverse.complete == whole.complete &&
            reverse.cells.size() == whole.cells.size() &&
            reverse.examined_nodes == whole.examined_nodes &&
            reverse.qualified_joins == whole.qualified_joins,
        "Reversal retains the canonical cover and actual numerical outcome");
  if (whole.first_refusal && reverse.first_refusal) {
    const auto& a = *whole.first_refusal;
    const auto& b = *reverse.first_refusal;
    check(a.condition == b.condition &&
              a.predicate_condition == b.predicate_condition &&
              a.first == b.first && a.last == b.last &&
              a.phase_index == b.phase_index && a.side == b.side,
          "Reverse refusal retains original canonical interval and limiting "
          "predicate");
  }
  const auto n = std::min(whole.cells.size(), reverse.cells.size());
  for (std::size_t i = 0; i < n; ++i) {
    const auto& a = whole.cells[i];
    const auto& b = reverse.cells[i];
    check(a.global_first == b.global_first && a.global_last == b.global_last &&
              a.phase_index == b.phase_index,
          "Reverse traversal preserves canonical retained cell order");
  }
  for (std::size_t phase = 0; phase < 4; ++phase) {
    const auto a = static_cast<double>(phase) / 4,
               b = static_cast<double>(phase + 1) / 4;
    auto forward =
        require(assess_origin_boarding_route_intermediate_step(a, b));
    log("FIRST_PHASE_" + std::to_string(phase), forward);
    report(forward);
    auto backward =
        require(assess_origin_boarding_route_intermediate_step(b, a));
    log("FIRST_PHASE_REVERSE_" + std::to_string(phase), backward);
    report(backward);
    check(forward.complete == backward.complete &&
              forward.cells.size() == backward.cells.size(),
          "Standalone phase reversal retains outcome without assuming movement "
          "success");
    if (phase == 0) {
      observed_release_refusal(forward, true);
      observed_release_refusal(backward, true);
    } else {
      const auto work = phase == 1
                            ? BoardingRouteFootPhaseCounters{11, 17, 6, 23, 36}
                            : BoardingRouteFootPhaseCounters{1, 2, 1, 3, 6};
      const auto cells = phase == 1 ? 6 : 1;
      const auto nodes = phase == 1 ? 11 : 1;
      const auto depth = phase == 1 ? 3 : 0;
      observed_counts(forward, true, cells, nodes, 0, depth, 7950176, work,
                      {false, false, false});
      observed_counts(backward, true, cells, nodes, 0, depth, 7950176, work,
                      {false, false, false});
    }
    local_replay(forward);
  }
  const auto sub =
      require(assess_origin_boarding_route_intermediate_step(.125, .875));
  log("FIRST_SUBRANGE", sub);
  report(sub);
  observed_counts(sub, true, 41, 81, 3, 8, 7950176, {78, 133, 41, 173, 270},
                  {true, true, true});
  complete_subrange_budgets(sub);
  for (double g : std::array{0., .25, .5, .75, 1., .8125}) {
    const auto point =
        require(assess_origin_boarding_route_intermediate_step(g, g));
    log("FIRST_POINT_" + std::to_string(g), point);
    report(point);
    observed_counts(point, true, 1, 1, 0, 0, 11696, {1, 2, 1, 3, 6},
                    {false, false, false});
    local_replay(point);
    point_budgets(point);
  }
}
} // namespace
int main() {
  try {
    controls_and_maps();
    invalid_and_environment();
    observations();
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "FAIL: " << e.what() << '\n';
  }
  std::cout << "Intermediate-step contract checks=" << checks
            << " failures=" << failures << '\n';
  return failures == 0 ? 0 : 1;
}
