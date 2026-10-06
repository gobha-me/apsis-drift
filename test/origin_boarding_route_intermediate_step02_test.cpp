#include "apsis_drift/origin_boarding_route_intermediate_step02.hpp"
#include "origin_boarding_route_checkpoint_unload_internal.hpp"
#include "origin_boarding_route_intermediate_step02_internal.hpp"
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
#include <streambuf>
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
[[gnu::noinline]] auto oracle(const Request& r, double u, bool reverse)
    -> Oracle {
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
auto expected_controls() -> std::array<Request, 5> {
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
  std::array<Request, 5> r{};
  for (auto& q : r) {
    q.root = {E, E};
    q.root_yaw_half = {.125, .125};
    q.torso_lean_half = {-.25, -.25};
    q.feet[0].sole = {I, I};
    q.feet[1].sole = {star, star};
    q.seconds_per_parameter = 12;
    q.port_reaction_fraction = {0, 0};
  }
  auto P = U;
  P.coordinates[2] = expected_constant({-.8});
  r[0].root = {C, C};
  r[0].root_yaw_half = {.25, .25};
  r[0].torso_lean_half = {0, 0};
  r[0].feet[0].sole = {U, P};
  r[1].root = {C, E};
  r[1].root_yaw_half = {.25, .125};
  r[1].torso_lean_half = {0, -.25};
  r[1].feet[0].sole = {P, H};
  r[1].feet[0].swing_height_metres = .010;
  r[2].feet[0].sole = {H, I};
  r[3].torso_lean_half = {-.25, -.30};
  r[3].port_reaction_fraction = {0, .25};
  r[4].torso_lean_half = {-.30, -.30};
  r[4].port_reaction_fraction = {.25, .25};
  r[4].seconds_per_parameter = 2;
  return r;
}

constexpr std::array<double, 6> cuts{0, .125, .25, .5, .75, 1};
auto clock_oracle(double g) -> double {
  if (g <= .25) return 96 * g;
  if (g <= .5) return 24 + 48 * (g - .25);
  if (g <= .75) return 36 + 48 * (g - .5);
  return 48 + 8 * (g - .75);
}
auto local_oracle(std::size_t phase, double g) -> double {
  return phase < 2 ? 8 * g - static_cast<double>(phase)
                   : 4 * g - static_cast<double>(phase - 1);
}
struct Hash {
  std::uint64_t value{1469598103934665603ULL};
  void integer(std::uint64_t x) {
    value ^= x;
    value *= 1099511628211ULL;
  }
  void scalar(double x) {
    integer(std::bit_cast<std::uint64_t>(x == 0 ? 0. : x));
  }
  void bounds(BoardingPlantedLegScalarBounds b, bool negate = false) {
    scalar(negate ? -b.upper : b.lower);
    scalar(negate ? -b.lower : b.upper);
  }
  void point_evidence(const BoardingPlantedBodyPointEvidence& p, bool reverse) {
    for (std::size_t kind = 0; kind < 3; ++kind) {
      const auto b = kind == 0   ? p.value
                     : kind == 1 ? p.derivatives.velocity
                                 : p.derivatives.acceleration;
      for (std::size_t a = 0; a < 3; ++a) {
        const auto lo = component(b.lower, a), hi = component(b.upper, a);
        scalar(kind == 1 && reverse ? -hi : lo);
        scalar(kind == 1 && reverse ? -lo : hi);
      }
    }
  }
  void cell(const Cell& c, bool reverse) {
    scalar(c.first);
    scalar(c.last);
    for (const auto& p : c.points)
      point_evidence(p, reverse);
    for (const auto& p : c.mass_points)
      point_evidence(p, reverse);
    point_evidence(c.center_of_mass, reverse);
    for (const auto& f : c.frames)
      for (const auto& p : f.columns)
        point_evidence(p, reverse);
    for (const auto& l : c.legs) {
      for (auto b : std::array{l.distance_squared, l.rho_squared,
                               l.gamma_squared, l.alpha, l.gamma})
        bounds(b);
      for (const auto& a :
           std::array{l.hip_pitch, l.shin_pitch, l.knee_flex, l.hip_axial,
                      l.hip_abduction, l.ankle_pitch, l.ankle_roll}) {
        bounds(a.rate, reverse);
        bounds(a.coordinate_second);
      }
      for (auto b : l.joint_speeds) {
        bounds(b);
      }
      for (auto b : l.sector_margins) {
        bounds(b);
      }
      for (bool b :
           std::array{l.nominal_links, l.target_sole_identity,
                      l.derivative_domains, l.joint_sectors, l.timing_complete})
        integer(b);
    }
    for (auto b :
         std::array{c.root_speed, c.root_acceleration, c.root_yaw_speed,
                    c.torso_joint_speed, c.port_reaction_fraction})
      bounds(b);
    for (auto b : c.sole_center_speed) {
      bounds(b);
    }
    for (auto b : c.sole_yaw_speed) {
      bounds(b);
    }
    for (auto b : c.whole_sole_speed) {
      bounds(b);
    }
    for (bool b :
         std::array{c.arithmetic_supported, c.nominal_links,
                    c.target_sole_identities, c.joint_sectors,
                    c.derivative_domains, c.timing_complete, c.complete})
      integer(b);
  }
};
struct StreamCounter : std::streambuf {
  std::streambuf* destination;
  std::size_t& bytes;
  StreamCounter(std::streambuf* target, std::size_t& count)
      : destination(target), bytes(count) {}
  auto overflow(int_type c) -> int_type override {
    if (traits_type::eq_int_type(c, traits_type::eof()))
      return traits_type::not_eof(c);
    if (bytes >= 16384) return traits_type::eof();
    ++bytes;
    return destination->sputc(traits_type::to_char_type(c));
  }
  auto sync() -> int override { return destination->pubsync(); }
};

using Diagnostic = BoardingRouteIntermediateStep02Diagnostic;
using Refusal = BoardingRouteIntermediateStep02Refusal;
using Limits = detail::BoardingRouteIntermediateStep02Limits;
using Condition = BoardingRouteFootPhaseCondition;
using Expected = std::expected<Diagnostic, std::string>;
struct Totals {
  std::size_t api{}, phase_calls{}, oracle_samples{};
  BoardingRouteFootPhaseCounters work;
} totals;
void before_query() {
  if (totals.api >= 64)
    throw std::runtime_error("Registered64 assessment ceiling reached");
  ++totals.api;
}
void audit(const Expected& r) {
  if (!r) return;
  totals.phase_calls += r->phase_calls;
  totals.work.graphs += r->work.graphs;
  totals.work.legs += r->work.legs;
  totals.work.bodies += r->work.bodies;
  totals.work.sectors += r->work.sectors;
  totals.work.timing += r->work.timing;
  if (totals.phase_calls > 100303 || totals.work.graphs > 75739 ||
      totals.work.legs > 151478 || totals.work.bodies > 75739 ||
      totals.work.sectors > 227217 || totals.work.timing > 454434)
    throw std::runtime_error("Registered executable work ceiling exceeded");
}
struct Baseline {
  std::uint64_t evidence_hash{}, topology_hash{};
  BoardingRouteFootPhaseCounters work;
  std::size_t nodes{}, navigation{}, depth{}, cells{}, phase_calls{}, output{},
      required_leaves{};
  std::optional<Refusal> refusal;
  std::array<bool, 4> joins{};
  bool valid{}, complete{}, arithmetic{};
};
struct Pair {
  std::uint64_t evidence_hash{}, topology_hash{};
  bool valid{}, complete{};
};
struct Summary {
  Baseline whole, point0;
  Pair pending;
  std::array<std::uint64_t, 21> first_hashes{};
};
static_assert(sizeof(Summary) < 1024);
void hash_request(Hash& h, const Request& r) {
  for (const auto& p : r.root)
    for (const auto& c : p.coordinates) {
      h.integer(c.count);
      for (double x : c.terms)
        h.scalar(x);
    }
  for (double x : r.root_yaw_half) {
    h.scalar(x);
  }
  for (double x : r.torso_lean_half) {
    h.scalar(x);
  }
  for (const auto& f : r.feet) {
    for (const auto& p : f.sole)
      for (const auto& c : p.coordinates) {
        h.integer(c.count);
        for (double x : c.terms)
          h.scalar(x);
      }
    for (double x : f.yaw_half) {
      h.scalar(x);
    }
    h.scalar(f.swing_height_metres);
  }
  h.scalar(r.seconds_per_parameter);
  for (double x : r.port_reaction_fraction)
    h.scalar(x);
}
auto summarize(const Diagnostic& d) -> Baseline {
  Baseline b;
  b.valid = true;
  b.complete = d.complete;
  b.arithmetic = d.arithmetic_supported;
  b.work = d.work;
  b.nodes = d.examined_nodes;
  b.navigation = d.mandatory_splits;
  b.depth = d.maximum_depth;
  b.cells = d.cells.size();
  b.phase_calls = d.phase_calls;
  b.output = d.output_capacity_bytes;
  b.refusal = d.first_refusal;
  b.joins = d.qualified_joins;
  b.required_leaves = b.cells;
  if (!b.complete && b.refusal) {
    if (b.refusal->condition == Condition::leaf_capacity ||
        b.refusal->condition == Condition::output_capacity)
      b.required_leaves = kBoardingRouteIntermediateStep02MaximumLeaves;
    else if (b.nodes > 0 &&
             b.cells < kBoardingRouteIntermediateStep02MaximumLeaves)
      ++b.required_leaves;
  }
  Hash evidence, topology;
  evidence.scalar(std::min(d.requested_first, d.requested_last));
  evidence.scalar(std::max(d.requested_first, d.requested_last));
  evidence.scalar(d.reporting_elapsed_seconds);
  evidence.integer(d.step_version);
  for (const auto& r : d.controls)
    hash_request(evidence, r);
  for (bool x : std::array{d.arithmetic_supported, d.nominal_links,
                           d.target_sole_identities, d.joint_sectors,
                           d.derivative_domains, d.timing_complete, d.complete})
    evidence.integer(x);
  for (bool x : d.join_expression_identity) {
    evidence.integer(x);
  }
  for (bool x : d.qualified_joins) {
    evidence.integer(x);
  }
  for (const auto& c : d.cells) {
    topology.scalar(c.global_first);
    topology.scalar(c.global_last);
    topology.integer(c.phase_index);
    evidence.scalar(c.global_first);
    evidence.scalar(c.global_last);
    evidence.integer(c.phase_index);
    evidence.cell(c.phase, d.reverse);
  }
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    for (double x : std::array{r.first, r.last, r.local_first, r.local_last}) {
      topology.scalar(x);
      evidence.scalar(x);
    }
    for (auto x : std::array<std::uint64_t, 5>{
             r.depth, static_cast<unsigned>(r.condition),
             static_cast<unsigned>(r.predicate_condition),
             r.phase_index ? *r.phase_index + 1 : 0,
             r.side ? *r.side + 1 : 0}) {
      topology.integer(x);
      evidence.integer(x);
    }
    evidence.bounds(r.limiting_bound);
  }
  b.evidence_hash = evidence.value;
  b.topology_hash = topology.value;
  return b;
}
void denied(const Diagnostic& d) {
  check(!d.self_qualified && !d.source_qualified && !d.material_qualified &&
            !d.world_qualified && !d.support_qualified && !d.load_qualified &&
            !d.actor_qualified && !d.route_qualified && !d.seat_qualified &&
            !d.save_qualified && !d.dynamics_qualified,
        "Numerical five-phase recipe grants no game authority");
}
[[gnu::noinline]] void structural_controls() {
  const auto expected = expected_controls();
  for (std::size_t phase = 0; phase < 5; ++phase) {
    const auto actual =
        detail::boarding_route_intermediate_step02_controls(phase);
    check(actual && same_request(*actual, expected[phase]),
          "Five immutable controls preserve exact registered terms/physicalT "
          "and ordering");
    check(detail::boarding_route_intermediate_step02_phase(
              cuts[phase], cuts[phase + 1]) == phase,
          "Positive phase interval has one genuine phase");
    for (double u : std::array{0., .125, .5, .875, 1.}) {
      const auto g = cuts[phase] + (cuts[phase + 1] - cuts[phase]) * u;
      check(detail::boarding_route_intermediate_step02_local(phase, g) == u &&
                local_oracle(phase, g) == u &&
                detail::boarding_route_intermediate_step02_clock(g) ==
                    clock_oracle(g),
            "Original8/4 local map and piecewise50s physical clock agree "
            "independently");
    }
    if (phase + 1 < 5) {
      check(detail::boarding_route_intermediate_step02_join_math(
                expected[phase], expected[phase + 1]),
            "Registered endpoint terms imply structural C2 control tie");
      auto changed = expected[phase + 1];
      changed.root[0].coordinates[0].terms[0] += .001;
      check(!detail::boarding_route_intermediate_step02_join_math(
                expected[phase], changed),
            "Changed root endpoint cannot inherit structural tie");
      changed = expected[phase + 1];
      changed.feet[0].sole[0].coordinates[2].terms[0] += .001;
      check(!detail::boarding_route_intermediate_step02_join_math(
                expected[phase], changed),
            "Changed sole endpoint cannot inherit structural tie");
      changed = expected[phase + 1];
      changed.port_reaction_fraction[0] += .01;
      check(!detail::boarding_route_intermediate_step02_join_math(
                expected[phase], changed),
            "Changed share endpoint cannot inherit tie");
      const auto g = cuts[phase + 1];
      check(detail::boarding_route_intermediate_step02_phase(g, g) == phase &&
                detail::boarding_route_intermediate_step02_phase(
                    g, std::nextafter(g, 1.)) == phase + 1 &&
                !detail::boarding_route_intermediate_step02_phase(
                    std::nextafter(g, 0.), std::nextafter(g, 1.)),
            "Join point belongs to preceding phase while right interval uses "
            "following phase; crossing interval refuses");
    }
  }
  check(!detail::boarding_route_intermediate_step02_controls(5),
        "No sixth unregistered phase");
  const auto old_start = detail::boarding_route_intermediate_step_controls(0),
             old_end = detail::boarding_route_intermediate_step_controls(3);
  check(
      old_start && old_end &&
          same_point(expected[0].root[0], old_start->root[0]) &&
          same_point(expected[0].feet[0].sole[0], old_start->feet[0].sole[0]) &&
          same_request(expected[4], *old_end),
      "Original Step01 start and constant final endpoint remain exact without "
      "new old-producer query");
  const auto prefix = detail::boarding_route_checkpoint_unload_controls(2);
  check(prefix && same_point(expected[0].root[0], prefix->root[1]) &&
            expected[0].root_yaw_half[0] == prefix->root_yaw_half[1] &&
            expected[0].torso_lean_half[0] == prefix->torso_lean_half[1] &&
            expected[0].port_reaction_fraction[0] ==
                prefix->port_reaction_fraction[1],
        "Earlier accepted prefix expression identity preserves zero-port "
        "Step02 start");
  if (prefix)
    for (std::size_t side = 0; side < 2; ++side)
      check(
          same_point(expected[0].feet[side].sole[0],
                     prefix->feet[side].sole[1]),
          "Original two sole endpoint expressions remain tied to prior prefix");
}
[[gnu::noinline]] void validate_report(const Diagnostic& d, Limits l = {}) {
  denied(d);
  const auto& cap = l.phase;
  check(d.step_version == 2 && d.examined_nodes <= cap.nodes &&
            d.maximum_depth <= cap.depth && d.cells.size() <= cap.leaves &&
            d.work.graphs <= cap.graphs && d.work.legs <= cap.legs &&
            d.work.bodies <= cap.bodies && d.work.sectors <= cap.sectors &&
            d.work.timing <= cap.timing,
        "Every global/phase work counter respects genuine lowered ceilings");
  check(d.mandatory_splits <= d.examined_nodes &&
            d.phase_calls <= d.examined_nodes - d.mandatory_splits &&
            d.work.graphs <= d.phase_calls &&
            d.work.legs <= 2 * d.work.graphs &&
            d.work.bodies <= d.work.graphs &&
            d.work.sectors <= 3 * d.work.graphs &&
            d.work.timing <= 6 * d.work.graphs,
        "Guard calls are counted separately from graph work and mandatory "
        "navigation");
  check(d.output_capacity_bytes <= cap.output_bytes ||
            (d.cells.empty() && d.first_refusal &&
             d.first_refusal->condition == Condition::output_capacity),
        "Output reports actual allocation or explicit pre-cover required "
        "capacity refusal");
  for (std::size_t i = 0; i < 5; ++i) {
    const auto recipe = detail::boarding_route_intermediate_step02_controls(i);
    check(recipe && same_request(d.controls[i], *recipe),
          "Owned control recipe never changes with request or cap");
  }
  for (std::size_t i = 0; i < 15; ++i) {
    const auto& p = d.parts[i];
    const auto& m = masses[i];
    check(static_cast<std::size_t>(p.id) == i &&
              static_cast<std::size_t>(p.mass.first) == m.first &&
              static_cast<std::size_t>(p.mass.second) == m.second &&
              p.mass.weight == m.weight,
          "Original15 fixed1200-mass bindings preserved");
    if (const auto* box =
            std::get_if<BoardingRoutePhaseBoxBinding>(&p.reservation)) {
      const RigidVector3 h = i == 0   ? RigidVector3{.24, .12, .18}
                             : i == 1 ? RigidVector3{.26, .2695, .18}
                             : i == 2 ? RigidVector3{.16, .18, .18}
                             : (i == 5 || i == 11)
                                 ? RigidVector3{.06, .05, .14}
                                 : RigidVector3{.04, .05, .02};
      const auto frame = i == 0    ? BoardingRoutePhaseFrame::root
                         : i == 5  ? BoardingRoutePhaseFrame::port_sole
                         : i == 11 ? BoardingRoutePhaseFrame::starboard_sole
                                   : BoardingRoutePhaseFrame::trunk;
      check(box->half_size_metres == h && box->frame == frame &&
                static_cast<std::size_t>(box->center) == m.first &&
                m.first == m.second,
            "All seven rigid WORLD boxes retain complete dimensions, original "
            "center and genuine frame");
    } else {
      const auto k = (i - 3) % 6;
      const auto& capsule =
          std::get<BoardingPlantedBodyCapsuleBinding>(p.reservation);
      check(capsule.radius_metres == (k == 0   ? .105
                                      : k == 1 ? .075
                                      : k == 3 ? .065
                                               : .055) &&
                static_cast<std::size_t>(capsule.start) == m.first &&
                static_cast<std::size_t>(capsule.end) == m.second,
            "All eight original capsule radii and both rod endpoints remain "
            "fixed");
    }
  }
  auto cursor = std::min(d.requested_first, d.requested_last);
  for (const auto& c : d.cells) {
    check(c.global_first == cursor && c.global_first <= c.global_last &&
              c.phase_index < 5 && c.phase.complete,
          "One shared report retains actual closed contiguous accepted prefix");
    if (c.phase_index >= 5) continue;
    check(
        detail::boarding_route_intermediate_step02_phase(
            c.global_first, c.global_last) == c.phase_index &&
            c.phase.first == local_oracle(c.phase_index, c.global_first) &&
            c.phase.last == local_oracle(c.phase_index, c.global_last),
        "Every retained cell uses genuine immutable phase and exact local map");
    check(c.phase.arithmetic_supported && c.phase.nominal_links &&
              c.phase.target_sole_identities && c.phase.joint_sectors &&
              c.phase.derivative_domains && c.phase.timing_complete,
          "Accepted cells retain all original graph predicates");
    accepted_timing(c.phase);
    cursor = c.global_last;
    check(c.phase.sole_center_speed[1].lower == 0 &&
              c.phase.sole_center_speed[1].upper == 0 &&
              c.phase.sole_yaw_speed[1].lower == 0 &&
              c.phase.sole_yaw_speed[1].upper == 0,
          "Loaded star sole remains exactly fixed");
    if (c.phase_index < 3)
      check(c.phase.port_reaction_fraction.lower == 0 &&
                c.phase.port_reaction_fraction.upper == 0,
            "Preposition/lower/descent declare zero port share without "
            "pretending airborne contact or pressure");
    if (c.phase_index == 4) {
      check(
          c.phase.port_reaction_fraction.lower == .25 &&
              c.phase.port_reaction_fraction.upper == .25,
          "Nonzero pause control is retained without supported-load authority");
      zero_endpoint_jets(c.phase);
    }
    if (c.phase.first == c.phase.last &&
        (c.phase.first == 0 || c.phase.first == 1))
      zero_endpoint_jets(c.phase);
  }
  check(
      d.reporting_elapsed_seconds ==
              std::abs(clock_oracle(d.requested_last) -
                       clock_oracle(d.requested_first)) &&
          d.reverse == (d.requested_first > d.requested_last),
      "Elapsed physicalT and reverse convention preserve original timing once");
  if (d.complete)
    check(!d.first_refusal &&
              cursor == std::max(d.requested_first, d.requested_last) &&
              d.arithmetic_supported && d.nominal_links &&
              d.target_sole_identities && d.joint_sectors &&
              d.derivative_domains && d.timing_complete,
          "Complete requires entire genuine closed cover, not successful "
          "samples");
  else
    check(d.first_refusal.has_value(),
          "Incomplete request retains original explicit stopping obligation");
  for (std::size_t j = 0; j < 4; ++j) {
    bool reached = false;
    for (std::size_t i = 1; i < d.cells.size(); ++i) {
      const auto& a = d.cells[i - 1];
      const auto& b = d.cells[i];
      if (a.phase_index == j && b.phase_index == j + 1 &&
          a.global_last == cuts[j + 1] && b.global_first == cuts[j + 1] &&
          a.global_first < a.global_last && b.global_first < b.global_last)
        reached = true;
    }
    check(d.join_expression_identity[j] && d.qualified_joins[j] == reached,
          "Join qualification needs actual two-sided accepted prefix, not "
          "endpoint tie alone");
  }
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    check(r.first >= cursor && r.first <= r.last &&
              r.last <= std::max(d.requested_first, d.requested_last) &&
              r.depth <= cap.depth,
          "Refusal names actual canonical prefix successor and honest global "
          "depth");
    if (r.phase_index)
      check(*r.phase_index < 5 &&
                r.local_first == local_oracle(*r.phase_index, r.first) &&
                r.local_last == local_oracle(*r.phase_index, r.last),
            "Refusal keeps source phase/local interval mapping");
  }
}
[[gnu::noinline]] void first_oracles(const Diagnostic& d) {
  for (const auto& c : d.cells) {
    if (c.phase_index >= 5 || !c.phase.complete) continue;
    for (double u : std::array{
             c.phase.first, c.phase.first + (c.phase.last - c.phase.first) * .5,
             c.phase.last}) {
      if (totals.oracle_samples >= 64512)
        throw std::runtime_error("FIRST-only body oracle ceiling reached");
      ++totals.oracle_samples;
      cell_oracle(d.controls[c.phase_index], c.phase, d.reverse, u);
    }
  }
}
void print_first(std::size_t index, const Diagnostic& d) {
  std::cout << std::setprecision(17) << "FIRST_STEP02 " << index
            << " request=" << d.requested_first << ',' << d.requested_last
            << " complete=" << d.complete << " reverse=" << d.reverse
            << " elapsed=" << d.reporting_elapsed_seconds
            << " cells=" << d.cells.size() << " nodes=" << d.examined_nodes
            << " navigation=" << d.mandatory_splits
            << " depth=" << d.maximum_depth << " phase_calls=" << d.phase_calls
            << " output=" << d.output_capacity_bytes
            << " work=" << d.work.graphs << ',' << d.work.legs << ','
            << d.work.bodies << ',' << d.work.sectors << ',' << d.work.timing
            << " joins=";
  for (bool b : d.qualified_joins)
    std::cout << b;
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    std::cout << " refusal=" << static_cast<unsigned>(r.condition)
              << " predicate=" << static_cast<unsigned>(r.predicate_condition)
              << " interval=" << r.first << ',' << r.last
              << " local=" << r.local_first << ',' << r.local_last << " phase=";
    if (r.phase_index)
      std::cout << *r.phase_index;
    else
      std::cout << "none";
    std::cout << " side=";
    if (r.side)
      std::cout << *r.side;
    else
      std::cout << "none";
    std::cout << " bound=" << r.limiting_bound.lower << ','
              << r.limiting_bound.upper;
  }
  std::cout << '\n' << std::flush;
}
// Added only after the21 requests on frozen2eedd45 agreed byte-for-byte on
// GCC and pinnedClang20. These qualify kinematics, never contact or gameplay.
[[gnu::noinline]] void observed_outcome(std::size_t index,
                                        const Diagnostic& d) {
  check(d.complete && !d.first_refusal && d.arithmetic_supported &&
            d.nominal_links && d.target_sole_identities && d.joint_sectors &&
            d.derivative_domains && d.timing_complete && !d.cells.empty(),
        "Every frozen FIRST request retains complete original kinematics and "
        "exact closed requested cover");
  for (std::size_t phase = 0; phase < 5; ++phase)
    check(d.controls[phase].seconds_per_parameter == (phase == 4 ? 2. : 12.),
          "Frozen physical phase durations remain12/12/12/12/2 without "
          "normalized-time rescaling");
  check(same_point(d.controls[0].root[0], d.controls[0].root[1]) &&
            same_constant(d.controls[0].feet[0].sole[0].coordinates[0],
                          d.controls[0].feet[0].sole[1].coordinates[0]) &&
            same_constant(d.controls[0].feet[0].sole[0].coordinates[1],
                          d.controls[0].feet[0].sole[1].coordinates[1]) &&
            same_constant(d.controls[0].feet[0].sole[1].coordinates[2],
                          d.controls[0].feet[1].sole[0].coordinates[2]) &&
            same_point(d.controls[0].feet[0].sole[1],
                       d.controls[1].feet[0].sole[0]) &&
            d.controls[0].feet[0].swing_height_metres == 0 &&
            d.controls[1].feet[0].swing_height_metres == .010,
        "Registered preposition and exact endpoint ordering remain unchanged "
        "after observed success");
  std::size_t cells{}, nodes{}, navigation{}, depth{}, calls{}, output{};
  double elapsed{};
  BoardingRouteFootPhaseCounters work;
  std::array<bool, 4> joins{};
  if (index < 2) {
    cells = 66;
    nodes = 131;
    navigation = 4;
    depth = 10;
    calls = 127;
    output = 7950864;
    elapsed = 50;
    work = {127, 216, 66, 279, 431};
    joins = {true, true, true, true};
  } else if (index < 12) {
    const auto phase = (index - 2) / 2;
    constexpr std::array<std::size_t, 5> cell_counts{11, 47, 6, 1, 1},
        node_counts{21, 93, 11, 1, 1}, depths{5, 7, 3, 0, 0};
    constexpr std::array<BoardingRouteFootPhaseCounters, 5> works{
        {{21, 32, 11, 43, 66},
         {93, 163, 47, 207, 317},
         {11, 17, 6, 23, 36},
         {1, 2, 1, 3, 6},
         {1, 2, 1, 3, 6}}};
    cells = cell_counts[phase];
    nodes = node_counts[phase];
    depth = depths[phase];
    calls = nodes;
    output = 7950864;
    elapsed = phase == 4 ? 2 : 12;
    work = works[phase];
  } else if (index < 14) {
    cells = 55;
    nodes = 109;
    navigation = 3;
    depth = 9;
    calls = 106;
    output = 7950864;
    elapsed = 37;
    work = {106, 184, 55, 236, 365};
    joins = {false, true, true, true};
  } else {
    cells = 1;
    nodes = 1;
    calls = 1;
    output = 12384;
    work = {1, 2, 1, 3, 6};
  }
  check(d.cells.size() == cells && d.examined_nodes == nodes &&
            d.mandatory_splits == navigation && d.maximum_depth == depth &&
            d.phase_calls == calls && d.output_capacity_bytes == output &&
            d.reporting_elapsed_seconds == elapsed &&
            d.qualified_joins == joins && d.work.graphs == work.graphs &&
            d.work.legs == work.legs && d.work.bodies == work.bodies &&
            d.work.sectors == work.sectors && d.work.timing == work.timing,
        "Frozen recipe preserves actual cover/guard/work/allocation/clock and "
        "earned physical join observations");
  if (!d.cells.empty())
    check(d.cells.front().global_first ==
                  std::min(d.requested_first, d.requested_last) &&
              d.cells.back().global_last ==
                  std::max(d.requested_first, d.requested_last),
          "Observed complete request retains both exact requested endpoints");
}
[[gnu::noinline]] auto first_one(std::size_t index, double first, double last)
    -> Baseline {
  before_query();
  auto result = assess_origin_boarding_route_intermediate_step02(first, last);
  audit(result);
  if (!result) {
    std::cout << "FIRST_STEP02 " << index << " api_error=" << result.error()
              << '\n'
              << std::flush;
    check(false, "Registered FIRST API error preserved");
    return {};
  }
  print_first(index, *result);
  validate_report(*result);
  first_oracles(*result);
  observed_outcome(index, *result);
  return summarize(*result);
}
[[gnu::noinline]] auto first_roster() -> Summary {
  Summary summary;
  std::size_t index{};
  auto retain = [&](double first, double last, bool paired) {
    const auto b = first_one(index, first, last);
    summary.first_hashes[index] = b.evidence_hash;
    if (index == 0) {
      summary.whole = b;
    }
    if (index == 14) {
      summary.point0 = b;
    }
    if (paired)
      check(
          b.valid == summary.pending.valid &&
              (!b.valid || (b.complete == summary.pending.complete &&
                            b.topology_hash == summary.pending.topology_hash &&
                            b.evidence_hash == summary.pending.evidence_hash)),
          "FIRST reverse preserves canonical cover/refusal/value/acceleration "
          "and negated physical first jets");
    else
      summary.pending = {b.evidence_hash, b.topology_hash, b.valid, b.complete};
    ++index;
  };
  retain(0, 1, false);
  retain(1, 0, true);
  for (std::size_t phase = 0; phase < 5; ++phase) {
    retain(cuts[phase], cuts[phase + 1], false);
    retain(cuts[phase + 1], cuts[phase], true);
  }
  retain(.125, .875, false);
  retain(.875, .125, true);
  for (double g : cuts) {
    retain(g, g, false);
  }
  retain(.8125, .8125, false);
  check(index == 21, "Exactly21 preregistered FIRST assessment invocations");
  return summary;
}

auto measured_limits(const Baseline& b) -> Limits {
  Limits l;
  l.phase.depth = b.depth;
  l.phase.nodes = b.nodes;
  l.phase.leaves = b.required_leaves;
  l.phase.output_bytes = b.output;
  l.phase.graphs = b.work.graphs;
  l.phase.legs = b.work.legs;
  l.phase.bodies = b.work.bodies;
  l.phase.sectors = b.work.sectors;
  l.phase.timing = b.work.timing;
  return l;
}
void work_equal(const BoardingRouteFootPhaseCounters& a,
                const BoardingRouteFootPhaseCounters& b) {
  check(a.graphs == b.graphs && a.legs == b.legs && a.bodies == b.bodies &&
            a.sectors == b.sectors && a.timing == b.timing,
        "Exact genuine aggregate work replay never resets between phases");
}
[[gnu::noinline]] auto validation_one(double first, double last, Limits limits,
                                      const Baseline* exact = nullptr,
                                      std::optional<Condition> capacity = {})
    -> std::size_t {
  before_query();
  auto result =
      detail::boarding_route_intermediate_step02_bounded(first, last, limits);
  audit(result);
  if (!result) {
    std::cerr << "VALIDATION api_error=" << result.error() << '\n';
    check(false, "Finite lowered validation call returned API refusal");
    return 0;
  }
  validate_report(*result, limits);
  if (exact) {
    const auto b = summarize(*result);
    check(b.evidence_hash == exact->evidence_hash &&
              b.topology_hash == exact->topology_hash &&
              b.complete == exact->complete && b.nodes == exact->nodes &&
              b.navigation == exact->navigation && b.depth == exact->depth &&
              b.cells == exact->cells && b.phase_calls == exact->phase_calls &&
              b.joins == exact->joins,
          "Exact required leaves preserve original failed/complete prefix and "
          "original stopping obligation");
    work_equal(b.work, exact->work);
  }
  if (capacity)
    check(!result->complete && result->first_refusal &&
              result->first_refusal->condition == *capacity,
          "Measured one-less/zero budget reaches its own genuine stopping "
          "stage without earlier predicate masking");
  if (capacity == Condition::output_capacity)
    check(result->phase_calls == 0 && result->work.graphs == 0,
          "One less actual allocation refuses before any original phase "
          "invocation");
  return result->output_capacity_bytes;
}
[[gnu::noinline]] void pregeometry_inputs() {
  const auto nan = std::numeric_limits<double>::quiet_NaN(),
             inf = std::numeric_limits<double>::infinity();
  for (const auto range :
       std::array{std::array{nan, 0.}, std::array{0., nan}, std::array{inf, 0.},
                  std::array{0., -inf}, std::array{-.001, 1.},
                  std::array{0., 1.001}}) {
    before_query();
    const auto r =
        assess_origin_boarding_route_intermediate_step02(range[0], range[1]);
    audit(r);
    check(!r, "Six invalid global endpoint ranges refuse before geometry");
  }
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
    before_query();
    const auto r = detail::boarding_route_intermediate_step02_bounded(0, 1, l);
    audit(r);
    check(
        !r,
        "Every raised private limit refuses before original phase invocation");
  }
  for (double x : std::array{nan, inf, -inf, -.001, 1.001}) {
    check(std::isnan(detail::boarding_route_intermediate_step02_clock(x)) &&
              std::isnan(
                  detail::boarding_route_intermediate_step02_local(0, x)) &&
              !detail::boarding_route_intermediate_step02_phase(x, x),
          "Invalid pure maps never create finite clock or phase");
  }
  check(std::isnan(detail::boarding_route_intermediate_step02_local(5, 0)),
        "No sixth phase has a local map");
}
[[gnu::noinline]] void unsafe_environments() {
  const auto saved = std::fegetround();
  for (int mode : std::array{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    const auto set = std::fesetround(mode);
    check(set == 0, "Non-nearest test environment installed");
    if (set != 0) continue;
    before_query();
    const auto r = assess_origin_boarding_route_intermediate_step02();
    std::fesetround(saved);
    audit(r);
    check(r && !r->arithmetic_supported && !r->complete && r->cells.empty() &&
              r->work.graphs == 0 && r->first_refusal &&
              r->first_refusal->condition == Condition::unsupported_arithmetic,
          "Unsafe rounding refuses before any graph or geometry");
    if (r) validate_report(*r);
  }
  std::fesetround(saved);
#if defined(__SSE2__) && defined(__x86_64__)
  const auto mxcsr = _mm_getcsr();
  for (unsigned bit :
       std::array{unsigned{1} << 15, unsigned{1} << 6, unsigned{1} << 13}) {
    _mm_setcsr(mxcsr | bit);
    before_query();
    const auto r = assess_origin_boarding_route_intermediate_step02();
    _mm_setcsr(mxcsr);
    audit(r);
    check(r && !r->arithmetic_supported && !r->complete && r->cells.empty() &&
              r->work.graphs == 0 && r->first_refusal &&
              r->first_refusal->condition == Condition::unsupported_arithmetic,
          "FTZ/DAZ/MXCSR refuses without stale accepted body");
    if (r) validate_report(*r);
  }
#endif
}
[[gnu::noinline]] void navigation_controls() {
  for (std::size_t depth : std::array<std::size_t, 3>{0, 2, 3}) {
    Limits l;
    l.phase.depth = depth;
    before_query();
    const auto r = detail::boarding_route_intermediate_step02_bounded(0, 1, l);
    audit(r);
    check(r.has_value(), "Fixed join-depth control returns actual report");
    if (!r) continue;
    validate_report(*r, l);
    if (depth < 3)
      check(
          !r->complete && r->phase_calls == 0 && r->work.graphs == 0 &&
              r->mandatory_splits == depth && r->examined_nodes == depth + 1 &&
              r->first_refusal &&
              r->first_refusal->condition == Condition::depth_capacity,
          "Depth0/2 cannot cross all mandatory joins or invoke a phase kernel");
    else
      check(r->phase_calls > 0 && r->work.graphs > 0 && r->maximum_depth == 3,
            "Depth3 reaches real phase0 graph without presuming broad "
            "candidate legality");
  }
}
[[gnu::noinline]] void validation_roster(const Summary& summary) {
  const auto before_samples = totals.oracle_samples;
  std::size_t whole_output{};
  if (summary.whole.valid && summary.whole.nodes > 0) {
    const auto l = measured_limits(summary.whole);
    whole_output = validation_one(0, 1, l, &summary.whole);
  }
  if (summary.point0.valid && summary.point0.nodes > 0) {
    const auto l = measured_limits(summary.point0);
    validation_one(0, 0, l, &summary.point0);
  }
  const auto values = [](const Baseline& b) {
    return std::array{b.work.graphs, b.work.legs, b.work.bodies, b.work.sectors,
                      b.work.timing};
  };
  const auto whole = values(summary.whole), point = values(summary.point0);
  const std::array conditions{Condition::graph_capacity,
                              Condition::leg_capacity, Condition::body_capacity,
                              Condition::sector_capacity,
                              Condition::timing_capacity};
  for (std::size_t stage = 0; stage < 5; ++stage) {
    const auto use_whole = summary.whole.valid && whole[stage] > 0;
    const auto* anchor = use_whole ? &summary.whole
                         : summary.point0.valid && point[stage] > 0
                             ? &summary.point0
                             : nullptr;
    if (!anchor) continue;
    const auto used = use_whole ? whole[stage] : point[stage];
    for (bool zero : std::array{false, true}) {
      if (zero && used == 1) continue;
      auto l = measured_limits(*anchor);
      const auto lowered = zero ? 0 : used - 1;
      switch (stage) {
        case 0: l.phase.graphs = lowered; break;
        case 1: l.phase.legs = lowered; break;
        case 2: l.phase.bodies = lowered; break;
        case 3: l.phase.sectors = lowered; break;
        default: l.phase.timing = lowered; break;
      }
      validation_one(0, use_whole ? 1. : 0., l, nullptr, conditions[stage]);
    }
  }
  if (summary.whole.valid && summary.whole.nodes > 0) {
    const auto base = measured_limits(summary.whole);
    for (std::size_t nodes :
         std::array{std::size_t{0}, summary.whole.nodes - 1}) {
      if (nodes == 0 && summary.whole.nodes == 1) continue;
      auto l = base;
      l.phase.nodes = nodes;
      validation_one(0, 1, l, nullptr, Condition::node_capacity);
    }
    if (summary.whole.nodes == 1) {
      auto l = base;
      l.phase.nodes = 0;
      validation_one(0, 1, l, nullptr, Condition::node_capacity);
    }
    if (summary.whole.required_leaves > 0) {
      auto l = base;
      l.phase.leaves = 0;
      validation_one(0, 1, l, nullptr, Condition::leaf_capacity);
      if (summary.whole.required_leaves > 1) {
        l = base;
        l.phase.leaves = summary.whole.required_leaves - 1;
        validation_one(0, 1, l, nullptr, Condition::leaf_capacity);
      }
    }
    if (whole_output > 0) {
      auto l = base;
      l.phase.output_bytes = whole_output;
      validation_one(0, 1, l, &summary.whole);
      --l.phase.output_bytes;
      validation_one(0, 1, l, nullptr, Condition::output_capacity);
    }
    if (summary.whole.depth > 0) {
      auto l = base;
      l.phase.depth = summary.whole.depth - 1;
      validation_one(0, 1, l, nullptr, Condition::depth_capacity);
    }
  }
  navigation_controls();
  unsafe_environments();
  pregeometry_inputs();
  check(totals.oracle_samples == before_samples,
        "Validation never adds LD body samples beyond fixed FIRST roster");
}
} // namespace
int main() {
  std::size_t bytes{};
  StreamCounter out(std::cout.rdbuf(), bytes), err(std::cerr.rdbuf(), bytes);
  auto* old_out = std::cout.rdbuf(&out);
  auto* old_err = std::cerr.rdbuf(&err);
  try {
    structural_controls();
    const auto summary = first_roster();
    validation_roster(summary);
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "FAIL: " << e.what() << '\n';
  }
  check(totals.api <= 64 && totals.phase_calls <= 100303 &&
            totals.work.graphs <= 75739 && totals.work.legs <= 151478 &&
            totals.work.bodies <= 75739 && totals.work.sectors <= 227217 &&
            totals.work.timing <= 454434 && totals.oracle_samples <= 64512,
        "Step02Validation64 actual full-executable operation/sample accounting "
        "is bounded");
  check(bytes + 256 <= 16384 && std::cout.good() && std::cerr.good(),
        "Complete stream fits16KiB without hidden truncation");
  std::cout << "VALIDATION api=" << totals.api
            << " phase_calls=" << totals.phase_calls
            << " work=" << totals.work.graphs << ',' << totals.work.legs << ','
            << totals.work.bodies << ',' << totals.work.sectors << ','
            << totals.work.timing << " oracle=" << totals.oracle_samples
            << " summary=" << sizeof(Summary) << " checks=" << checks
            << " failures=" << failures << " bytes_before_summary=" << bytes
            << '\n';
  std::cout.flush();
  std::cerr.flush();
  const auto stream_good = std::cout.good() && std::cerr.good();
  std::cout.rdbuf(old_out);
  std::cerr.rdbuf(old_err);
  return failures == 0 && stream_good ? 0 : 1;
}
