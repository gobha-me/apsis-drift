#include "apsis_drift/origin_boarding_route_foot_phase.hpp"
#include "origin_boarding_route_foot_phase_internal.hpp"
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
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
namespace {
using namespace apsis_drift;
using Request = BoardingRouteFootPhaseRequest;
using Constant = BoardingRoutePhaseConstant;
using ConstantPoint = BoardingRoutePhasePointConstant;
using Diagnostic = BoardingRouteFootPhaseDiagnostic;
using Cell = BoardingRouteFootPhaseCell;
using Condition = BoardingRouteFootPhaseCondition;
using Limits = detail::BoardingRouteFootPhaseLimits;
std::size_t checks{};
int failures{};
void check(bool okay, std::string_view label) {
  ++checks;
  if (!okay) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T> auto require(std::expected<T, std::string> result) -> T {
  if (!result) throw std::runtime_error(result.error());
  return std::move(*result);
}
auto constant(double x) -> Constant {
  return {{x, 0, 0}, 1};
}
auto sum(double a, double b) -> Constant {
  return {{a, b, 0}, 2};
}
auto point(double x, double y, double z) -> ConstantPoint {
  return {{constant(x), constant(y), constant(z)}};
}
auto fixture() -> Request {
  Request r;
  r.root[0] = r.root[1] = point(.16, .847, -.55);
  for (std::size_t side = 0; side < 2; ++side) {
    auto p = point(0, side == 0 ? -100000.0 * 1e-6 : -160000.0 * 1e-6,
                   side == 0 ? -.5 : -.8);
    p.coordinates[0] = sum(.16, side == 0 ? -.14 : .14);
    r.feet[side].sole[0] = r.feet[side].sole[1] = p;
  }
  return r;
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
void endpoint_c2(const Diagnostic& d) {
  if (!d.complete) return;
  check(d.cells.size() == 1, "Accepted endpoint point owns one graph cell");
  for (const auto& p : d.cells[0].points)
    for (const auto& b :
         std::array{p.derivatives.velocity, p.derivatives.acceleration})
      for (std::size_t axis = 0; axis < 3; ++axis)
        check(component(b.lower, axis) == 0 && component(b.upper, axis) == 0,
              "Exact carrier and hump endpoint body derivatives are zero");
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
void denied(const Diagnostic& d) {
  check(!d.self_qualified && !d.source_qualified && !d.material_qualified &&
            !d.world_qualified && !d.support_qualified && !d.load_qualified &&
            !d.actor_qualified && !d.route_qualified && !d.seat_qualified &&
            !d.save_qualified && !d.dynamics_qualified,
        "Source-free numerical phase grants no game permission");
}
void validate_report(const Diagnostic& d) {
  denied(d);
  check(d.work.graphs <= d.examined_nodes && d.work.legs <= 2 * d.work.graphs &&
            d.work.bodies <= d.work.graphs &&
            d.work.sectors <= 3 * d.work.graphs &&
            d.work.timing <= 6 * d.work.graphs,
        "Actual bounded per-node stage accounting");
  check(d.maximum_depth <= 10 && d.examined_nodes <= 2047 &&
            d.cells.size() <= 1024 &&
            d.output_capacity_bytes <=
                kBoardingRouteFootPhaseMaximumOutputBytes,
        "Original actual storage/cover caps");
  auto cursor = std::min(d.requested_first, d.requested_last);
  for (const auto& c : d.cells) {
    check(c.first == cursor && c.first <= c.last && c.complete &&
              c.arithmetic_supported && c.nominal_links &&
              c.target_sole_identities && c.joint_sectors &&
              c.derivative_domains && c.timing_complete,
          "Accepted prefix has closed contiguous genuine complete cells");
    cursor = c.last;
    accepted_timing(c);
    cell_oracle(d.request, c, d.reverse, c.first);
    cell_oracle(d.request, c, d.reverse, c.first + (c.last - c.first) * .5);
    if (c.last != c.first) cell_oracle(d.request, c, d.reverse, c.last);
  }
  if (d.complete)
    check(!d.first_refusal &&
              cursor == std::max(d.requested_first, d.requested_last) &&
              d.arithmetic_supported && d.nominal_links &&
              d.target_sole_identities && d.joint_sectors &&
              d.derivative_domains && d.timing_complete,
          "Full exact requested closed coverage and aggregate flags");
  else
    check(d.first_refusal.has_value(),
          "Every incomplete curve retains actual refusal");
  check(d.reporting_elapsed_seconds ==
            d.request.seconds_per_parameter *
                std::abs(d.requested_last - d.requested_first),
        "Subinterval uses original physical clock");
  for (std::size_t i = 0; i < 15; ++i) {
    const auto& part = d.parts[i];
    const auto& m = masses[i];
    check(static_cast<std::size_t>(part.id) == i &&
              static_cast<std::size_t>(part.mass.first) == m.first &&
              static_cast<std::size_t>(part.mass.second) == m.second &&
              part.mass.weight == m.weight,
          "Original fifteen named mass/point bindings");
    if (const auto* b =
            std::get_if<BoardingRoutePhaseBoxBinding>(&part.reservation)) {
      const RigidVector3 h = i == 0   ? RigidVector3{.24, .12, .18}
                             : i == 1 ? RigidVector3{.26, .2695, .18}
                             : i == 2 ? RigidVector3{.16, .18, .18}
                             : (i == 5 || i == 11)
                                 ? RigidVector3{.06, .05, .14}
                                 : RigidVector3{.04, .05, .02};
      check(b->half_size_metres == h,
            "Full fixed WORLD box dimensions, no ellipsoid substitution");
    } else {
      const auto& cap =
          std::get<BoardingPlantedBodyCapsuleBinding>(part.reservation);
      const auto k = (i - 3) % 6;
      check(cap.radius_metres == (k == 0   ? .105
                                  : k == 1 ? .075
                                  : k == 3 ? .065
                                           : .055),
            "Original limb WORLD capsule radii");
    }
  }
}
void log_first(std::string_view label, const Diagnostic& d) {
  std::cout << label << " complete=" << d.complete
            << " cells=" << d.cells.size() << " nodes=" << d.examined_nodes
            << " work=" << d.work.graphs << ',' << d.work.legs << ','
            << d.work.bodies << ',' << d.work.sectors << ',' << d.work.timing;
  if (d.first_refusal)
    std::cout << " refusal="
              << static_cast<unsigned>(d.first_refusal->condition)
              << " predicate="
              << static_cast<unsigned>(d.first_refusal->predicate_condition)
              << " interval=" << d.first_refusal->first << ','
              << d.first_refusal->last;
  std::cout << '\n';
}
void invalid_controls() {
  const auto inf = std::numeric_limits<double>::infinity(),
             nan = std::numeric_limits<double>::quiet_NaN();
  auto bad = [&](const Request& r) {
    check(!assess_origin_boarding_route_foot_phase(r),
          "Invalid original control refuses before graph work");
  };
  for (double x : {nan, inf, -inf}) {
    auto r = fixture();
    for (std::size_t endpoint = 0; endpoint < 2; ++endpoint) {
      r = fixture();
      r.root_yaw_half[endpoint] = x;
      bad(r);
      r = fixture();
      r.torso_lean_half[endpoint] = x;
      bad(r);
      r = fixture();
      r.port_reaction_fraction[endpoint] = x;
      bad(r);
      for (std::size_t side = 0; side < 2; ++side) {
        r = fixture();
        r.feet[side].yaw_half[endpoint] = x;
        bad(r);
      }
    }
    r = fixture();
    r.seconds_per_parameter = x;
    bad(r);
    for (std::size_t side = 0; side < 2; ++side) {
      r = fixture();
      r.feet[side].swing_height_metres = x;
      bad(r);
    }
    for (std::size_t endpoint = 0; endpoint < 2; ++endpoint)
      for (std::size_t axis = 0; axis < 3; ++axis)
        for (std::size_t term = 0; term < 3; ++term) {
          r = fixture();
          r.root[endpoint].coordinates[axis].terms[term] = x;
          bad(r);
          for (std::size_t side = 0; side < 2; ++side) {
            r = fixture();
            r.feet[side].sole[endpoint].coordinates[axis].terms[term] = x;
            bad(r);
          }
        }
    check(!assess_origin_boarding_route_foot_phase(fixture(), x, 1) &&
              !assess_origin_boarding_route_foot_phase(fixture(), 0, x),
          "Nonfinite requested interval refuses");
  }
  for (double x : {-1., std::nextafter(1., inf)})
    check(!assess_origin_boarding_route_foot_phase(fixture(), x, 0),
          "Requested interval strictly bounded");
  for (std::uint8_t count : {std::uint8_t{0}, std::uint8_t{4}}) {
    auto r = fixture();
    r.root[0].coordinates[0].count = count;
    bad(r);
  }
  for (double x : {-8.0001, 8.0001}) {
    auto r = fixture();
    r.root[0].coordinates[0] = constant(x);
    bad(r);
  }
  {
    auto r = fixture();
    r.root[0].coordinates[0] = {{8, 1, 0}, 2};
    bad(r);
    r.root[0].coordinates[0] = {{8, -1, 1}, 3};
    check(detail::boarding_route_foot_phase_request_valid(r).has_value(),
          "Exact original three-term sum at workspace boundary admitted as "
          "control");
    r.root[0].coordinates[0] = {
        {8, std::numeric_limits<double>::denorm_min(), 0}, 2};
    bad(r);
  }
  {
    auto r = fixture();
    r.root[0].coordinates[0].terms[2] = 1;
    bad(r);
  }
  for (double x : {std::nextafter(-1., -inf), std::nextafter(1., inf)}) {
    auto r = fixture();
    r.root_yaw_half[1] = x;
    bad(r);
    r = fixture();
    r.feet[1].yaw_half[0] = x;
    bad(r);
    r = fixture();
    r.torso_lean_half[0] = x;
    bad(r);
  }
  for (double x : {std::nextafter(1., 0.), std::nextafter(120., inf)}) {
    auto r = fixture();
    r.seconds_per_parameter = x;
    bad(r);
  }
  for (double x : {-.001, std::nextafter(.25, inf)}) {
    auto r = fixture();
    r.feet[0].swing_height_metres = x;
    bad(r);
  }
  for (double x : {-.001, std::nextafter(1., inf)}) {
    auto r = fixture();
    r.port_reaction_fraction[1] = x;
    bad(r);
  }
  auto limits = Limits{};
  ++limits.nodes;
  check(!detail::boarding_route_foot_phase_bounded(fixture(), 0, 1, limits),
        "Private controls cannot increase registered cover cap");
  for (int which = 0; which < 4; ++which) {
    Limits l;
    Condition expected{};
    if (which == 0) {
      l.nodes = 0;
      expected = Condition::node_capacity;
    }
    if (which == 1) {
      l.leaves = 0;
      expected = Condition::leaf_capacity;
    }
    if (which == 2) {
      l.output_bytes = 0;
      expected = Condition::output_capacity;
    }
    if (which == 3) {
      l.graphs = 0;
      expected = Condition::graph_capacity;
    }
    const auto d =
        require(detail::boarding_route_foot_phase_bounded(fixture(), 0, 0, l));
    check(!d.complete && d.first_refusal &&
              d.first_refusal->condition == expected && d.work.graphs == 0 &&
              d.cells.empty(),
          "Reached zero capacity before actual graph work");
    denied(d);
  }
}
void unsafe_environment() {
  struct Guard {
    int rounding = std::fegetround();
#if defined(__SSE__)
    unsigned csr = _mm_getcsr();
#endif
    ~Guard() {
      std::fesetround(rounding);
#if defined(__SSE__)
      _mm_setcsr(csr);
#endif
    }
  } guard;
  auto inspect = []() {
    const auto r = assess_origin_boarding_route_foot_phase(fixture());
    check(!r ||
              (!r->complete && !r->arithmetic_supported && !r->nominal_links &&
               !r->target_sole_identities && !r->timing_complete),
          "Unsafe FP cannot retain positive graph flags");
  };
  for (int mode : {FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO})
    if (std::fesetround(mode) == 0) inspect();
  std::fesetround(FE_TONEAREST);
#if defined(__SSE__)
  for (unsigned bit : {1U << 15, 1U << 6, 1U << 13}) {
    _mm_setcsr(guard.csr | bit);
    inspect();
    _mm_setcsr(guard.csr);
  }
#endif
}
void numerical_controls() {
  using B = BoardingPlantedLegScalarBounds;
  const auto exact = [](double x) { return B{x, x}; };
  for (bool reverse : {false, true}) {
    const detail::BoardingRoutePhaseScalarJet q{exact(.4), exact(3), exact(-2)};
    const auto e =
        require(detail::boarding_route_phase_yaw_numeric(q, 4, reverse));
    check(e.arithmetic_supported, "Original rational yaw arithmetic supported");
    const Jet k{.4, (reverse ? -3.L : 3.L) / 4, -2.L / 16};
    const auto theta = literal(2) * angle(literal(1), k);
    check(contains(e.angle.rate, theta.first) &&
              contains(e.angle.coordinate_second, theta.second),
          "Yaw physical first/reverse second corroborated independently");
    const auto expected = yaw(k);
    for (std::size_t col = 0; col < 3; ++col)
      point_contains(e.frame.columns[col],
                     {expected[0][col], expected[1][col], expected[2][col]});
  }
  for (const auto& direction : std::array<std::array<double, 3>, 5>{
           {{-.25, 1, 1}, {0, 1, 1}, {-1, 1, 0}, {1, -.5, 0}, {-.25, -1, 0}}}) {
    const auto e = require(detail::boarding_route_phase_hip_sector_numeric(
        exact(direction[0]), exact(direction[1])));
    check(e.arithmetic_supported && e.certified == (direction[2] == 1),
          "Directed parent120 sector includes F<0 and excludes wrong cone");
  }
  for (double sine : {-.2, .2}) {
    const auto e = require(detail::boarding_route_phase_hip_speed_numeric(
        exact(.35), exact(0), exact(.35), exact(sine)));
    const auto expected =
        std::sqrt(2.L * .35L * .35L * (1 + static_cast<long double>(sine)));
    check(e.supported && contains(e.speed_radians_per_second, expected),
          "Hip resultant includes actual signed axial/pitch cross term");
    check(e.certified == (sine < 0),
          "Cross term changes30deg speed-threshold outcome");
  }
  check(!detail::boarding_route_phase_hip_sector_numeric({1, -1}, exact(1)),
        "Malformed numeric hinge bounds refuse");
  check(!detail::boarding_route_phase_yaw_numeric(
            {exact(2), exact(0), exact(0)}, 8, false),
        "Numeric yaw cannot expand carrier family");
  check(!detail::boarding_route_phase_hip_speed_numeric(exact(0), exact(0),
                                                        exact(0), exact(1.01)),
        "Numeric cross-term sine domain bounded");
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
void observed_point_budgets() {
  const auto request = fixture();
  const auto base =
      require(assess_origin_boarding_route_foot_phase(request, 0, 0));
  log_first("POINT_BASE", base);
  validate_report(base);
  if (!base.complete) return;
  check(base.cells.size() == 1,
        "Complete instantaneous point has one owned cell");
  Limits exact;
  exact.nodes = base.examined_nodes;
  exact.leaves = base.cells.size();
  exact.output_bytes = base.output_capacity_bytes;
  exact.graphs = base.work.graphs;
  exact.legs = base.work.legs;
  exact.bodies = base.work.bodies;
  exact.sectors = base.work.sectors;
  exact.timing = base.work.timing;
  const auto replay =
      require(detail::boarding_route_foot_phase_bounded(request, 0, 0, exact));
  check(replay.complete && snapshot(replay.cells[0]) == snapshot(base.cells[0]),
        "Exact actual capacity point replay retains complete fieldwise graph");
  for (int i = 0; i < 8; ++i) {
    auto l = exact;
    Condition condition{};
    bool used = true;
    switch (i) {
      case 0:
        used = l.graphs > 0;
        if (used) --l.graphs;
        condition = Condition::graph_capacity;
        break;
      case 1:
        used = l.legs > 0;
        if (used) --l.legs;
        condition = Condition::leg_capacity;
        break;
      case 2:
        used = l.bodies > 0;
        if (used) --l.bodies;
        condition = Condition::body_capacity;
        break;
      case 3:
        used = l.sectors > 0;
        if (used) --l.sectors;
        condition = Condition::sector_capacity;
        break;
      case 4:
        used = l.timing > 0;
        if (used) --l.timing;
        condition = Condition::timing_capacity;
        break;
      case 5:
        used = l.nodes > 0;
        if (used) --l.nodes;
        condition = Condition::node_capacity;
        break;
      case 6:
        used = l.leaves > 0;
        if (used) --l.leaves;
        condition = Condition::leaf_capacity;
        break;
      default:
        used = l.output_bytes > 0;
        if (used) --l.output_bytes;
        condition = Condition::output_capacity;
        break;
    }
    if (!used) continue;
    const auto refusal =
        require(detail::boarding_route_foot_phase_bounded(request, 0, 0, l));
    check(
        !refusal.complete && refusal.first_refusal &&
            refusal.first_refusal->condition == condition,
        "One less actual used budget honestly reaches original stage refusal");
    denied(refusal);
  }
}
void graph_cases() {
  const auto original = fixture();
  const auto first = require(assess_origin_boarding_route_foot_phase(original));
  log_first("FIRST_PUBLIC_WHOLE", first);
  validate_report(first);
  // The first registered GCC/Clang20 logs are retained before these now-known
  // outcome regressions; every fresh run still prints its observation first.
  check(first.complete, "Observed stationary whole graph remains complete");
  observed_point_budgets();
  for (const auto endpoints : std::array<std::array<double, 2>, 5>{
           {{1, 0}, {.125, .875}, {.875, .125}, {.5, .5}, {1, 1}}}) {
    const auto d = require(assess_origin_boarding_route_foot_phase(
        original, endpoints[0], endpoints[1]));
    log_first("STATIONARY_REQUEST", d);
    validate_report(d);
    check(d.complete, "Observed complete stationary graph also covers valid "
                      "reverse/sub/point");
  }
  std::array<Request, 6> motions;
  motions.fill(original);
  motions[0].root[1].coordinates[0] = sum(.16, .07);
  motions[0].root[1].coordinates[1] = sum(.847, .012);
  motions[0].root[1].coordinates[2] = sum(-.55, -.04);
  motions[1].port_reaction_fraction = {.25, 0};
  motions[2].feet[0].swing_height_metres = .02;
  motions[3].root_yaw_half[1] = .05;
  motions[3].feet[0].yaw_half[1] = .05;
  motions[3].feet[1].yaw_half[1] = .05;
  motions[4].feet[0].sole[1].coordinates[0] = sum(.16, -.12);
  motions[4].feet[0].yaw_half[1] = .05;
  motions[5].torso_lean_half[1] = .08;
  for (std::size_t i = 0; i < motions.size(); ++i) {
    const auto& request = motions[i];
    const auto d = require(assess_origin_boarding_route_foot_phase(request));
    log_first("MOTION", d);
    validate_report(d);
    if (i == 2) {
      check(!d.complete && d.first_refusal && !d.cells.empty(),
            "Observed port lift retains a valid prefix before refusing");
      if (d.first_refusal) {
        check(d.first_refusal->condition == Condition::depth_capacity &&
                  d.first_refusal->predicate_condition ==
                      Condition::joint_sector &&
                  d.first_refusal->first == .1240234375 &&
                  d.first_refusal->last == .125,
              "Observed port lift preserves its exact closed joint-sector "
              "refusal");
        if (!d.cells.empty())
          check(
              d.cells.front().first == 0 &&
                  d.cells.back().last == d.first_refusal->first,
              "Observed port lift prefix ends at the actual refused interval");
      }
    } else {
      check(d.complete, "Observed five moving whole graphs remain complete");
    }
    for (const auto range :
         std::array<std::array<double, 2>, 3>{{{1, 0}, {.25, .75}, {.5, .5}}}) {
      const auto report = require(
          assess_origin_boarding_route_foot_phase(request, range[0], range[1]));
      validate_report(report);
      if (i != 2)
        check(report.complete, "Observed passing motion also covers reverse, "
                               "subinterval and point");
    }
    for (double endpoint : {0., 1.})
      endpoint_c2(require(assess_origin_boarding_route_foot_phase(
          request, endpoint, endpoint)));
    if (i == 2 || i == 4) {
      const auto point =
          require(assess_origin_boarding_route_foot_phase(request, .25, .25));
      if (point.complete) {
        const auto& o = oracle(request, .25, false);
        const auto base = 4U;
        check(std::abs(o.points[base + 2][i == 2 ? 1 : 0].first) > 1e-6L,
              "Moving target ankle has actual nonzero derivative");
      }
    }
  }
  auto bad = original;
  bad.feet[0].sole[0] = bad.feet[0].sole[1] = point(.02, 2, -.5);
  auto refused = require(assess_origin_boarding_route_foot_phase(bad, 0, 0));
  check(!refused.complete && refused.first_refusal,
        "Forward-up leg geometry refuses, no source permission");
  bad = original;
  bad.feet[0].sole[0] = bad.feet[0].sole[1] = point(.02, -4, -.5);
  refused = require(assess_origin_boarding_route_foot_phase(bad, 0, 0));
  check(!refused.complete && refused.first_refusal,
        "Beyond nominal link reach refuses");
  bad = original;
  bad.root_yaw_half = {-.2, -.2};
  bad.feet[0].yaw_half = {.2, .2};
  refused = require(assess_origin_boarding_route_foot_phase(bad, 0, 0));
  check(!refused.complete && refused.first_refusal,
        "Opposite directed yaw beyond45 refuses axial sector");
  // Exact expression controls retain the original binary64 limb lengths;
  // rounded straight/collapsed endpoint literals would test a different graph.
  for (int singular = 0; singular < 3; ++singular) {
    bad = original;
    auto sole = point(0, -.10, -.55);
    sole.coordinates[0] = sum(.16, -.14);
    if (singular == 0)
      sole.coordinates[1] = sum(.847, -.10);
    else
      bad.root[0].coordinates[1] = bad.root[1].coordinates[1] =
          singular == 1 ? sum(.47285, .47478) : sum(.47478, -.47285);
    bad.feet[0].sole[0] = bad.feet[0].sole[1] = sole;
    const auto singular_report =
        require(assess_origin_boarding_route_foot_phase(bad, 0, 0));
    check(!singular_report.complete && singular_report.first_refusal,
          "Exact rho-zero, straight, or collapsed nominal geometry refuses "
          "derivative permission");
    denied(singular_report);
  }
  bad = original;
  bad.seconds_per_parameter = 1;
  bad.torso_lean_half[1] = .3;
  const auto fast_torso =
      require(assess_origin_boarding_route_foot_phase(bad, .5, .5));
  check(!fast_torso.complete && fast_torso.first_refusal,
        "Legal torso position with excessive actual angular speed refuses "
        "timing");
  if (fast_torso.first_refusal)
    check(fast_torso.first_refusal->condition == Condition::joint_speed ||
              fast_torso.first_refusal->predicate_condition ==
                  Condition::joint_speed,
          "Instantaneous torso refusal preserves angular speed cause");
  bad = original;
  bad.seconds_per_parameter = 1;
  bad.root[1].coordinates[0] = sum(.16, .2);
  const auto fast_root =
      require(assess_origin_boarding_route_foot_phase(bad, .5, .5));
  check(!fast_root.complete && fast_root.first_refusal,
        "Excessive actual root translation refuses timing without changed "
        "limits");
  auto live = original;
  const auto owned =
      require(assess_origin_boarding_route_foot_phase(live, 0, 0));
  live.root[0].coordinates[0].terms[0] = 99;
  check(owned.request.root[0].coordinates[0].terms[0] == .16,
        "Owned request preserves original controls after caller mutation");
}
} // namespace
int main() {
  try {
    invalid_controls();
    unsafe_environment();
    numerical_controls();
    graph_cases();
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
