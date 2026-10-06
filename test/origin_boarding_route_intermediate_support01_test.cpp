#include "apsis_drift/origin_boarding_route_intermediate_support01.hpp"
#include "origin_boarding_route_intermediate_load01_internal.hpp"
#include "origin_boarding_route_intermediate_support01_internal.hpp"
#include "origin_boarding_route_intermediate_unloaded01_internal.hpp"
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
#include <optional>
#include <stdexcept>
#include <streambuf>
#include <string_view>
#include <type_traits>
#include <utility>
#if defined(__SSE2__) && defined(__x86_64__)
#include <xmmintrin.h>
#endif
namespace {
using namespace apsis_drift;
using Provider = OriginBoardingIntermediatePauseSupport;
using Diagnostic = BoardingRouteIntermediateSupport01Diagnostic;
using Cell = BoardingRouteIntermediateSupport01Cell;
using PhaseCell = BoardingRouteFootPhaseCell;
using Request = BoardingRouteFootPhaseRequest;
using Constant = BoardingRoutePhaseConstant;
using ConstantPoint = BoardingRoutePhasePointConstant;
using State = BoardingRouteIntermediateSupport01State;
using Condition = BoardingRouteIntermediateSupport01Condition;
using Scalar = BoardingFootSiteScalarBounds;
using Limits = detail::BoardingRouteIntermediateSupport01Limits;
using Access = detail::BoardingIntermediatePauseSupportAccess;
static_assert(!std::is_default_constructible_v<
              detail::BoardingRouteIntermediateSupport01AdmissionContext>);
static_assert(!std::is_constructible_v<
              detail::BoardingRouteIntermediateSupport01AdmissionContext,
              const Diagnostic&>);
static_assert(!std::is_default_constructible_v<
              detail::BoardingRouteIntermediateSupport01CurrentCellToken>);
static_assert(
    !std::is_constructible_v<
        detail::BoardingRouteIntermediateSupport01CurrentCellToken,
        const Diagnostic&, const Cell&, const BoardingRouteFootPhaseRequest&,
        const detail::BoardingRouteIntermediateSupport01AdmissionContext&>);
std::size_t checks{};
int failures{};
void check(bool yes, std::string_view why) {
  ++checks;
  if (!yes) {
    ++failures;
    std::cerr << "FAIL: " << why << '\n';
  }
}
struct Totals {
  std::array<std::uint32_t, 22> work{};
  std::uint32_t creators{}, consumers{}, oracles{}, phase_calls{};
} totals;
static_assert(sizeof(Totals) <= 128);
void charge(std::uint32_t& n, std::uint32_t maximum) {
  if (n >= maximum) throw std::runtime_error("Registered roster exhausted");
  ++n;
}
void aggregate(std::size_t index, std::uint64_t amount) {
  if (amount > std::numeric_limits<std::uint32_t>::max() - totals.work[index])
    throw std::runtime_error("Registered aggregate overflow");
  totals.work[index] += static_cast<std::uint32_t>(amount);
}
struct StreamCounter : std::streambuf {
  std::streambuf* target;
  std::size_t& bytes;
  bool truncated{};
  StreamCounter(std::streambuf* t, std::size_t& b) : target(t), bytes(b) {}
  auto overflow(int_type c) -> int_type override {
    if (traits_type::eq_int_type(c, traits_type::eof()))
      return traits_type::not_eof(c);
    if (bytes >= 16384) {
      truncated = true;
      return traits_type::eof();
    }
    ++bytes;
    return target->sputc(traits_type::to_char_type(c));
  }
  auto xsputn(const char* s, std::streamsize n) -> std::streamsize override {
    std::streamsize k{};
    while (k < n &&
           !traits_type::eq_int_type(overflow(s[k]), traits_type::eof()))
      ++k;
    return k;
  }
  auto sync() -> int override { return target->pubsync(); }
};
class Environment {
 public:
  explicit Environment(std::size_t mode) : rounding_(std::fegetround()) {
#if defined(__SSE2__) && defined(__x86_64__)
    control_ = _mm_getcsr();
#endif
    if (mode < 3) {
      const std::array modes{FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO};
      available = std::fesetround(modes[mode]) == 0;
    } else {
#if defined(__SSE2__) && defined(__x86_64__)
      _mm_setcsr(mode == 3   ? control_ | (1U << 15)
                 : mode == 4 ? control_ | (1U << 6)
                             : (control_ & ~(3U << 13)) | (1U << 13));
      available = true;
#endif
    }
  }
  ~Environment() {
    std::fesetround(rounding_);
#if defined(__SSE2__) && defined(__x86_64__)
    _mm_setcsr(control_);
#endif
  }
  Environment(const Environment&) = delete;
  auto operator=(const Environment&) -> Environment& = delete;
  bool available{};

 private:
  int rounding_{};
#if defined(__SSE2__) && defined(__x86_64__)
  unsigned control_{};
#endif
};
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
void timing_oracle(const PhaseCell& c, const Oracle& o) {
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
void accepted_timing(const PhaseCell& c) {
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
void zero_endpoint_jets(const PhaseCell& c) {
  for (const auto& p : c.points)
    for (const auto& b :
         std::array{p.derivatives.velocity, p.derivatives.acceleration})
      for (std::size_t axis = 0; axis < 3; ++axis)
        check(component(b.lower, axis) == 0 && component(b.upper, axis) == 0,
              "Registered C2 carrier/hump endpoint has exact structural zero "
              "physical body jets");
}
void cell_oracle(const PhaseCell& c, const Oracle& o) {
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
auto finite(Scalar x) -> bool {
  return x.supported && std::isfinite(x.lower) && std::isfinite(x.upper) &&
         x.lower <= x.upper;
}
auto mask_count(bool x) -> std::size_t {
  return x ? 1 : 0;
}
template <class T, std::size_t N>
auto mask_count(const std::array<T, N>& xs) -> std::size_t {
  std::size_t n{};
  for (const auto& x : xs)
    n += mask_count(x);
  return n;
}
using Protocol = BoardingRouteIntermediateSupport01Protocol;
using Force = BoardingRouteIntermediateSupport01PortForceState;
constexpr std::array<std::size_t Limits::*, 16> members{
    &Limits::source_guards,       &Limits::projection_guards,
    &Limits::definition_guards,   &Limits::pressure_candidates,
    &Limits::disk_edges,          &Limits::sole_extrema,
    &Limits::source_coordinates,  &Limits::intersection_operations,
    &Limits::midpoint_operations, &Limits::allocation_operations,
    &Limits::reaction_operations, &Limits::division_quotients,
    &Limits::division_folds,      &Limits::upper_geometry_edges,
    &Limits::endpoint_operations, &Limits::event_guards};
auto new_work(const BoardingRouteIntermediateSupport01Counters& w)
    -> std::array<std::size_t, 16> {
  return {w.source_guards,       w.projection_guards,
          w.definition_guards,   w.pressure_candidates,
          w.disk_edges,          w.sole_extrema,
          w.source_coordinates,  w.intersection_operations,
          w.midpoint_operations, w.allocation_operations,
          w.reaction_operations, w.division_quotients,
          w.division_folds,      w.upper_geometry_edges,
          w.endpoint_operations, w.event_guards};
}
auto get_limit(const Limits& l, std::size_t i) -> std::size_t {
  switch (i) {
    case 0: return l.phase.depth;
    case 1: return l.phase.nodes;
    case 2: return l.phase.leaves;
    case 3: return l.phase.output_bytes;
    case 4: return l.phase.graphs;
    case 5: return l.phase.legs;
    case 6: return l.phase.bodies;
    case 7: return l.phase.sectors;
    case 8: return l.phase.timing;
    default: return l.*members[i - 9];
  }
}
void set_limit(Limits& l, std::size_t i, std::size_t n) {
  switch (i) {
    case 0: l.phase.depth = n; break;
    case 1: l.phase.nodes = n; break;
    case 2: l.phase.leaves = n; break;
    case 3: l.phase.output_bytes = n; break;
    case 4: l.phase.graphs = n; break;
    case 5: l.phase.legs = n; break;
    case 6: l.phase.bodies = n; break;
    case 7: l.phase.sectors = n; break;
    case 8: l.phase.timing = n; break;
    default: l.*members[i - 9] = n; break;
  }
}
std::uint64_t recipe_hash{};
struct Hash {
  std::uint64_t value{1469598103934665603ULL};
  void add(std::uint64_t n) { value = (value ^ n) * 1099511628211ULL; }
  void number(double n) { add(std::bit_cast<std::uint64_t>(n == 0 ? 0. : n)); }
  void bound(Scalar b) {
    number(b.lower);
    number(b.upper);
    add(b.supported);
  }
  void point(BoardingPlantedLegPointBounds p) {
    for (auto x :
         {p.lower.x, p.lower.y, p.lower.z, p.upper.x, p.upper.y, p.upper.z})
      number(x);
  }
  void text(std::string_view s) {
    add(s.size());
    for (unsigned char c : s)
      add(c);
  }
};
void hash_mask(Hash& h, bool b) {
  h.add(b);
}
template <class T, std::size_t N>
void hash_mask(Hash& h, const std::array<T, N>& a) {
  for (const auto& b : a)
    hash_mask(h, b);
}
void hash_jet(Hash& h, const BoardingPlantedBodyPointEvidence& p,
              bool reverse) {
  h.point(p.value);
  auto v = p.derivatives.velocity;
  if (reverse) {
    const auto old = v;
    v.lower = {-old.upper.x, -old.upper.y, -old.upper.z};
    v.upper = {-old.lower.x, -old.lower.y, -old.lower.z};
  }
  h.point(v);
  h.point(p.derivatives.acceleration);
}
[[gnu::noinline]] auto semantic_hash(const Diagnostic& d) -> std::uint64_t {
  Hash h;
  h.add(recipe_hash);
  h.add(d.support_version);
  h.number(std::min(d.requested_first, d.requested_last));
  h.number(std::max(d.requested_first, d.requested_last));
  h.add(static_cast<unsigned>(d.state));
  h.add(static_cast<unsigned>(d.stop_condition));
  h.add(d.complete);
  h.number(d.reporting_elapsed_seconds);
  for (auto n : {d.examined_nodes, d.maximum_depth, d.mandatory_splits,
                 d.work.phase_calls})
    h.add(n);
  for (auto n : new_work(d.work))
    h.add(n);
  for (auto n : {d.work.phase.graphs, d.work.phase.legs, d.work.phase.bodies,
                 d.work.phase.sectors, d.work.phase.timing})
    h.add(n);
  hash_mask(h, d.source_evaluated);
  hash_mask(h, d.join_expression_identity);
  hash_mask(h, d.qualified_joins);
  hash_mask(h, d.prefix_endpoint_scope);
  hash_mask(h, d.prefix_endpoint_earned);
  h.add(d.cells.size());
  if (d.source_enrolled)
    for (const auto& p : d.parts) {
      h.add(static_cast<unsigned>(p.id));
      h.add(static_cast<unsigned>(p.mass.first));
      h.add(static_cast<unsigned>(p.mass.second));
      h.add(p.mass.weight);
      h.add(p.reservation.index());
      if (const auto* b =
              std::get_if<BoardingRoutePhaseBoxBinding>(&p.reservation)) {
        h.add(static_cast<unsigned>(b->center));
        h.add(static_cast<unsigned>(b->frame));
        h.number(b->half_size_metres.x);
        h.number(b->half_size_metres.y);
        h.number(b->half_size_metres.z);
      } else {
        const auto& c =
            std::get<BoardingPlantedBodyCapsuleBinding>(p.reservation);
        h.add(static_cast<unsigned>(c.start));
        h.add(static_cast<unsigned>(c.end));
        h.number(c.radius_metres);
      }
    }
  for (const auto& c : d.cells) {
    h.number(c.global_first);
    h.number(c.global_last);
    h.add(c.phase_index);
    h.add(static_cast<unsigned>(c.protocol));
    h.bound(c.port_share);
    h.bound(c.star_share);
    for (const auto& p : c.phase.points)
      hash_jet(h, p, d.reverse);
    for (const auto& p : c.phase.mass_points)
      hash_jet(h, p, d.reverse);
    hash_jet(h, c.phase.center_of_mass, d.reverse);
    for (const auto& f : c.phase.frames)
      for (const auto& p : f.columns)
        hash_jet(h, p, d.reverse);
    for (const auto& side : c.pressure_xz)
      for (auto b : side)
        h.bound(b);
    for (const auto& s : c.sites) {
      h.add(s.loaded);
      h.add(s.complete);
      h.add(s.plane_identity);
      hash_mask(h, s.sole_evaluated);
      hash_mask(h, s.source_evaluated);
      for (std::size_t e = 0; e < 4; ++e)
        for (const auto* b : {&s.sole_edges[e], &s.source_edges[e]}) {
          h.bound(b->signed_side);
          h.bound(b->edge_length_squared);
          h.bound(b->squared_margin_gap);
          h.add(b->disk_contained);
        }
    }
    hash_mask(h, c.projected_carrier_complete);
    hash_mask(h, c.definition_evaluated);
    hash_mask(h, c.reaction_evaluated);
    hash_mask(h, c.sole_extrema_evaluated);
    hash_mask(h, c.source_coordinate_evaluated);
    hash_mask(h, c.intersection_evaluated);
    hash_mask(h, c.midpoint_evaluated);
    hash_mask(h, c.allocation_evaluated);
    hash_mask(h, c.division_quotient_evaluated);
    hash_mask(h, c.division_fold_evaluated);
    hash_mask(h, c.pressure_evaluated);
    hash_mask(h, c.event_evaluated);
    hash_mask(h, c.endpoint_scope);
    hash_mask(h, c.endpoint_evaluated);
    hash_mask(h, c.endpoint_earned);
    hash_mask(h, c.upper_side_evaluated);
    hash_mask(h, c.endpoint_operation_evaluated);
    h.add(c.endpoint_operation_count);
    h.add(static_cast<unsigned>(c.upper_class));
    h.add(static_cast<unsigned>(c.upper_plane_event));
    h.add(static_cast<unsigned>(c.intermediate_plane_event));
    h.add(static_cast<unsigned>(c.port_force_state));
    for (bool flag : {c.port_contact_geometry, c.port_positive_interior,
                      c.star_everywhere_positive, c.contains_zero_endpoint,
                      c.checkpoint_threshold_met, c.projection_complete,
                      c.plane_identities, c.nominal_equilibrium, c.star_support,
                      c.prefix_zero_geometry_complete,
                      c.nominal_support_complete, c.complete})
      h.add(flag);
    for (auto b : c.upper_minimum_signed_side) {
      h.bound(b);
    }
    h.bound(c.departure_gap);
    for (const auto& pair : c.intermediate_intersection)
      for (auto b : pair)
        h.bound(b);
  }
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    h.add(static_cast<unsigned>(r.condition));
    h.add(static_cast<unsigned>(r.predicate_condition));
    for (auto n : {r.first, r.last, r.local_first, r.local_last})
      h.number(n);
    h.add(r.depth);
    h.add(r.phase_index.value_or(5));
    h.add(r.side.value_or(2));
    h.add(r.edge.value_or(4));
    h.add(r.axis.value_or(2));
    h.add(r.operation.value_or(12));
    h.bound(r.limiting_bound);
    h.text(r.source_name);
    h.add(r.source_edge);
    h.add(static_cast<unsigned>(r.phase.condition));
    h.add(r.phase.side.value_or(2));
    if (r.source_key) {
      h.add(static_cast<unsigned>(r.source_key->buffer));
      h.add(r.source_key->group);
      h.add(r.source_key->triangle);
    }
  }
  return h.value;
}
using Used = std::array<std::uint32_t, 25>;
struct Baseline {
  Used used{};
  std::uint64_t hash{};
  std::uint32_t reached{};
  bool exists{}, complete{};
};
struct Summary {
  Baseline whole, anchors;
};
static_assert(sizeof(Baseline) <= 200 && sizeof(Summary) <= 400);
[[gnu::noinline]] void capture(const Diagnostic& d, Baseline& b,
                               bool anchors = false, std::size_t slot = 0) {
  if (!anchors) {
    b.used[0] = static_cast<std::uint32_t>(d.maximum_depth);
    b.used[1] = static_cast<std::uint32_t>(d.examined_nodes);
    const auto slots =
        d.cells.size() + (!d.complete && d.source_enrolled ? 1U : 0U);
    b.used[2] = static_cast<std::uint32_t>(slots);
    b.used[3] = static_cast<std::uint32_t>(
        2 * sizeof(std::expected<Diagnostic, std::string>) +
        slots * sizeof(Cell));
    const std::array old{d.work.phase.graphs, d.work.phase.legs,
                         d.work.phase.bodies, d.work.phase.sectors,
                         d.work.phase.timing};
    for (std::size_t i = 0; i < 5; ++i)
      b.used[i + 4] = static_cast<std::uint32_t>(old[i]);
    const auto nw = new_work(d.work);
    for (std::size_t i = 0; i < 16; ++i)
      b.used[i + 9] = static_cast<std::uint32_t>(nw[i]);
    b.hash = semantic_hash(d);
    b.exists = true;
    b.complete = d.complete;
    return;
  }
  const auto nw = new_work(d.work);
  for (std::size_t i = 9; i < 25; ++i) {
    const bool take = (slot == 5 && i >= 14 && i <= 21) ||
                      (slot == 12 && (i == 22 || i == 24)) ||
                      (slot == 13 && i == 23);
    if (take) {
      b.used[i] = static_cast<std::uint32_t>(nw[i - 9]);
      b.reached |= std::uint32_t{1} << i;
    }
  }
}
void authority() {
  check(
      !Diagnostic::self_qualified && !Diagnostic::material_qualified &&
          !Diagnostic::world_qualified && !Diagnostic::route_qualified &&
          !Diagnostic::seat_qualified && !Diagnostic::actor_qualified &&
          !Diagnostic::save_qualified && !Diagnostic::friction_qualified &&
          !Diagnostic::strength_qualified && !Diagnostic::dynamics_qualified &&
          !Diagnostic::first_flight_qualified,
      "Nominal support never grants body/self/material/WORLD/actor authority");
}
constexpr std::array<Condition, 16> capacity_conditions{
    Condition::source_guard_capacity,
    Condition::projection_capacity,
    Condition::definition_capacity,
    Condition::pressure_capacity,
    Condition::edge_capacity,
    Condition::sole_extrema_capacity,
    Condition::source_coordinate_capacity,
    Condition::intersection_capacity,
    Condition::midpoint_capacity,
    Condition::allocation_capacity,
    Condition::reaction_capacity,
    Condition::division_quotient_capacity,
    Condition::division_fold_capacity,
    Condition::upper_edge_capacity,
    Condition::endpoint_operation_capacity,
    Condition::event_guard_capacity};
[[gnu::noinline]] void accounting(const Diagnostic& d, const Limits& l) {
  authority();
  check(d.support_version == 1, "Selected support version");
  if (std::ranges::find(capacity_conditions, d.stop_condition) !=
      capacity_conditions.end())
    check(d.state == State::capacity, "Any actual hard stage stop remains "
                                      "terminal through ordinary wrappers");
  if (d.stop_condition == Condition::unsupported_arithmetic)
    check(d.state == State::unsupported && !d.arithmetic_supported,
          "Actual unsupported arithmetic cannot become an ordinary witness");
  if (d.source_enrolled) {
    check(d.work.source_guards == 36 && mask_count(d.source_evaluated) == 36,
          "Genuine once-source36 complete");
    check(d.reporting_elapsed_seconds ==
              std::abs(detail::boarding_route_intermediate_support01_clock(
                           d.requested_last) -
                       detail::boarding_route_intermediate_support01_clock(
                           d.requested_first)),
          "Physical clock assigned only after enrollment");
  } else
    check(d.reporting_elapsed_seconds == 0,
          "Unenrolled clock remains unassigned");
  check(d.examined_nodes <= l.phase.nodes && d.maximum_depth <= l.phase.depth &&
            d.cells.size() <= l.phase.leaves &&
            d.output_capacity_bytes <= l.phase.output_bytes,
        "One shared navigation/output budget");
  if (d.source_enrolled)
    check(d.output_capacity_bytes ==
              2 * sizeof(std::expected<Diagnostic, std::string>) +
                  d.cells.capacity() * sizeof(Cell),
          "Two owned headers and one actual no-growth capacity");
  const std::array old{d.work.phase.graphs, d.work.phase.legs,
                       d.work.phase.bodies, d.work.phase.sectors,
                       d.work.phase.timing};
  for (std::size_t i = 0; i < 5; ++i) {
    check(old[i] <= get_limit(l, i + 4), "Original work cap");
    aggregate(i, old[i]);
  }
  const auto nw = new_work(d.work);
  for (std::size_t i = 0; i < 16; ++i) {
    check(nw[i] <= l.*members[i], "Charged new work cap");
    aggregate(i + 5, nw[i]);
  }
  aggregate(21, d.work.phase_calls);
  if (d.work.phase_calls > 229264 - totals.phase_calls)
    throw std::runtime_error("Compiler roster exhausted");
  totals.phase_calls += static_cast<std::uint32_t>(d.work.phase_calls);
  check(d.work.phase.graphs <= d.work.phase_calls &&
            d.work.phase_calls <= d.examined_nodes,
        "Compiler calls include genuine guarded attempts");
  const auto first = std::min(d.requested_first, d.requested_last),
             last = std::max(d.requested_first, d.requested_last);
  auto cursor = first;
  std::array<bool, 4> joins{};
  std::array<bool, 3> endpoints{};
  const Cell* previous{};
  std::array<std::size_t, 16> saved{};
  for (const auto& c : d.cells) {
    check(c.global_first == cursor && c.global_last >= cursor &&
              c.global_last <= last,
          "Closed accepted prefix without gaps");
    cursor = c.global_last;
    check(detail::boarding_route_intermediate_support01_phase(
              c.global_first, c.global_last) == c.phase_index &&
              c.phase.first ==
                  detail::boarding_route_intermediate_support01_local(
                      c.phase_index, c.global_first) &&
              c.phase.last ==
                  detail::boarding_route_intermediate_support01_local(
                      c.phase_index, c.global_last),
          "Canonical phase and actual local interval");
    check(c.complete && c.phase.complete && c.projection_complete &&
              c.nominal_equilibrium && c.star_support &&
              c.nominal_support_complete && c.plane_identities,
          "Only fresh complete support cells append");
    check(mask_count(c.projected_carrier_complete) == 11,
          "All current projection carriers earned");
    accepted_timing(c.phase);
    if (c.phase.first == c.phase.last &&
        (c.phase.first == 0 || c.phase.first == 1))
      zero_endpoint_jets(c.phase);
    const bool prefix = c.phase_index < 3;
    check(c.protocol ==
              (prefix ? Protocol::unloaded_prefix : Protocol::load_acquisition),
          "Branch dispatch matches immutable phase");
    check(mask_count(c.definition_evaluated) == (prefix ? 32U : 31U) &&
              (!prefix ? !c.definition_evaluated[31] : true),
          "Distinct32/31 definition protocols, no fakeD32");
    check(finite(c.port_share) && finite(c.star_share) &&
              c.port_share.lower >= 0 && c.port_share.upper <= .0625 &&
              c.star_share.lower >= .9375 && c.star_share.upper <= 1 &&
              c.star_everywhere_positive,
          "Functional nonnegative same-complement reactions");
    check(c.sites[1].complete && c.sites[1].plane_identity &&
              c.sites[1].loaded && mask_count(c.sites[1].sole_evaluated) == 4 &&
              mask_count(c.sites[1].source_evaluated) == 4,
          "Loaded star full finite disk proof");
    for (std::size_t side = prefix ? 1U : 0U; side < 2; ++side) {
      check(c.pressure_evaluated[side] && c.sites[side].complete &&
                c.sites[side].plane_identity,
            "Actual required pressure witness");
      for (auto b : c.pressure_xz[side])
        check(finite(b),
              "Pressure published only with supported finite bounds");
      for (std::size_t e = 0; e < 4; ++e)
        for (const auto* b :
             {&c.sites[side].sole_edges[e], &c.sites[side].source_edges[e]})
          check(b->disk_contained && finite(b->signed_side) &&
                    finite(b->squared_margin_gap) && b->signed_side.lower > 0 &&
                    b->squared_margin_gap.lower >= 0,
                "All original .020+.010 metric disk edges certified");
    }
    if (prefix) {
      check(c.port_share.lower == 0 && c.port_share.upper == 0 &&
                c.star_share.lower == 1 && c.star_share.upper == 1 &&
                c.port_force_state == Force::zero_only &&
                !c.port_positive_interior && !c.port_contact_geometry,
            "Unloaded port geometry never creates load");
      check(!c.pressure_evaluated[0] && !c.sites[0].complete &&
                !c.sites[0].loaded &&
                mask_count(c.sites[0].sole_evaluated) == 0 &&
                mask_count(c.sites[0].source_evaluated) == 0 &&
                !c.pressure_xz[0][0].supported &&
                !c.pressure_xz[0][1].supported,
            "Prefix port pressure NOT_EVALUATED");
      check(mask_count(c.reaction_evaluated) == 0 &&
                mask_count(c.sole_extrema_evaluated) == 0 &&
                mask_count(c.intersection_evaluated) == 0 &&
                mask_count(c.midpoint_evaluated) == 0 &&
                mask_count(c.allocation_evaluated) == 0 &&
                mask_count(c.division_quotient_evaluated) == 0 &&
                mask_count(c.division_fold_evaluated) == 0,
            "Prefix load-only arithmetic NOT_EVALUATED");
      check(mask_count(c.event_evaluated) == 16 &&
                c.prefix_zero_geometry_complete,
            "Prefix16 events complete");
      for (std::size_t axis = 0; axis < 2; ++axis) {
        const auto a = axis == 0 ? 0U : 2U;
        check(c.pressure_xz[1][axis].lower ==
                      component(c.phase.center_of_mass.value.lower, a) &&
                  c.pressure_xz[1][axis].upper ==
                      component(c.phase.center_of_mass.value.upper, a),
              "Prefix starCOP is SAME actual COM");
      }
      const std::array boundaries{0., .125, .5};
      for (std::size_t i = 0; i < 3; ++i) {
        const bool contains_endpoint =
            c.global_first <= boundaries[i] && c.global_last >= boundaries[i];
        check(c.endpoint_scope[i] == contains_endpoint &&
                  c.endpoint_evaluated[i] == contains_endpoint &&
                  c.endpoint_earned[i] == contains_endpoint,
              "Only accepted prefix containing cell earns endpoint geometry");
        endpoints[i] = endpoints[i] || c.endpoint_earned[i];
      }
      if (c.endpoint_earned[0])
        check(finite(c.upper_minimum_signed_side[0]) &&
                  c.upper_minimum_signed_side[0].lower > 0 &&
                  mask_count(c.upper_side_evaluated[0]) == 4,
              "Authentic upper trapezoid strict start witness");
      if (c.endpoint_earned[1])
        check(finite(c.departure_gap) && c.departure_gap.lower > 0,
              "Full unshrunk sole endpoint departure");
      if (c.endpoint_earned[2])
        for (std::size_t a = 0; a < 2; ++a)
          check(c.positive_overlap[a] &&
                    finite(c.intermediate_intersection[a][0]) &&
                    finite(c.intermediate_intersection[a][1]) &&
                    c.intermediate_intersection[a][0].upper <
                        c.intermediate_intersection[a][1].lower,
                "Finite intermediate positive area geometry without port "
                "pressure");
      check(mask_count(c.endpoint_operation_evaluated) ==
                c.endpoint_operation_count,
            "Actual endpoint arithmetic prefix masks");
      if (c.phase_index == 0)
        check(c.upper_class !=
                      BoardingRouteIntermediateSupport01UpperClass::not_run &&
                  c.upper_plane_event ==
                      BoardingRouteIntermediateSupport01PlaneEvent::equal,
              "Coplanar phase0 retains honest mixed/disjoint/overlap "
              "information");
      else
        check(c.upper_class ==
                  BoardingRouteIntermediateSupport01UpperClass::not_run,
              "Upper whole-cell geometry not invented outside phase0");
    } else {
      check(mask_count(c.event_evaluated) == 0 &&
                mask_count(c.endpoint_scope) == 0 &&
                mask_count(c.endpoint_evaluated) == 0 &&
                mask_count(c.endpoint_earned) == 0 &&
                mask_count(c.upper_side_evaluated) == 0 &&
                mask_count(c.endpoint_operation_evaluated) == 0 &&
                c.upper_class ==
                    BoardingRouteIntermediateSupport01UpperClass::not_run,
            "Loading cannot borrow prefix endpoint/event authority");
      check(mask_count(c.reaction_evaluated) == 3 &&
                mask_count(c.sole_extrema_evaluated) == 4 &&
                mask_count(c.source_coordinate_evaluated) == 8 &&
                mask_count(c.intersection_evaluated) == 4 &&
                mask_count(c.midpoint_evaluated) == 4 &&
                mask_count(c.allocation_evaluated) == 6 &&
                mask_count(c.division_quotient_evaluated) == 8 &&
                mask_count(c.division_fold_evaluated) == 12,
            "All load allocation operations and positive variable division "
            "earned");
      const bool zero_boundary = c.phase_index == 3 && c.phase.first == 0;
      check(c.port_force_state == (zero_boundary
                                       ? Force::zero_boundary_positive_interior
                                       : Force::everywhere_positive) &&
                c.contains_zero_endpoint == zero_boundary &&
                c.port_positive_interior && c.port_contact_geometry &&
                c.sites[0].loaded == !zero_boundary,
            "Positive interior requires finite port contact without false "
            "closed-boundary loadedness");
      const bool full =
          c.phase_index == 4 || (c.phase.first == 1 && c.phase.last == 1);
      check(c.checkpoint_threshold_met == full,
            "Dimensionless threshold only exact full/held endpoint, never "
            "tiny-load exemption");
    }
    saved[1] += 251;
    saved[2] += mask_count(c.definition_evaluated);
    saved[3] += mask_count(c.pressure_evaluated);
    saved[4] += prefix ? 8 : 16;
    saved[5] += mask_count(c.sole_extrema_evaluated);
    saved[6] += mask_count(c.source_coordinate_evaluated);
    saved[7] += mask_count(c.intersection_evaluated);
    saved[8] += mask_count(c.midpoint_evaluated);
    saved[9] += mask_count(c.allocation_evaluated);
    saved[10] += mask_count(c.reaction_evaluated);
    saved[11] += mask_count(c.division_quotient_evaluated);
    saved[12] += mask_count(c.division_fold_evaluated);
    saved[13] += mask_count(c.upper_side_evaluated);
    saved[14] += mask_count(c.endpoint_operation_evaluated);
    saved[15] += mask_count(c.event_evaluated);
    if (previous)
      for (std::size_t j = 0; j < 4; ++j) {
        const std::array cuts{.125, .25, .5, .75};
        if (previous->global_last == cuts[j] && c.global_first == cuts[j] &&
            previous->global_first < previous->global_last &&
            c.global_first < c.global_last && previous->phase_index == j &&
            c.phase_index == j + 1)
          joins[j] = true;
      }
    previous = &c;
  }
  for (std::size_t i = 1; i < 16; ++i)
    check(saved[i] <= nw[i],
          "Retained masks fit aggregate attempts without per-phase reset");
  for (std::size_t j = 0; j < 4; ++j) {
    check(d.qualified_joins[j] == (joins[j] && d.join_expression_identity[j]),
          "Only accepted two-sided physicalC2 joins earned");
    if (d.complete) {
      const std::array cuts{.125, .25, .5, .75};
      if (first < cuts[j] && last > cuts[j])
        check(d.qualified_joins[j],
              "Complete requested crossing earns every interior join");
    }
  }
  check(d.prefix_endpoint_scope == endpoints &&
            d.prefix_endpoint_earned == endpoints,
        "Prefix endpoint OR never borrows a load-only result");
  if (d.complete)
    check(!d.cells.empty() && cursor == last && !d.first_refusal &&
              d.stop_condition == Condition::none && d.source_enrolled &&
              d.kinematic_complete && d.nonnegative_reactions &&
              d.nominal_equilibrium && d.star_support &&
              d.nominal_support_complete,
          "Complete genuinely requested nominal cover");
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    check(r.first >= first && r.last <= last && r.first <= r.last,
          "Terminal closed refusal lies in actual request");
    if (r.source_key) {
      bool found = false;
      for (std::size_t side = 0; side < 2; ++side)
        if (const auto* q = Access::partition(d.source, side))
          found = found || ((*r.source_key == q->faces[0].key ||
                             *r.source_key == q->faces[1].key) &&
                            r.source_name == q->faces[0].source_object);
      if (const auto* q =
              detail::boarding_route_intermediate_unloaded01_upper_partition(
                  d.source))
        found = found || ((*r.source_key == q->faces[0].key ||
                           *r.source_key == q->faces[1].key) &&
                          r.source_name == q->faces[0].source_object);
      check(found, "Refusal identity and name retained by actual source owner");
    }
  }
}
using PlainPoint = std::array<long double, 3>;
auto plain(RigidVector3 v) -> PlainPoint {
  return {v.x, v.y, v.z};
}
auto side_value(PlainPoint a, PlainPoint b, PlainPoint p) -> long double {
  return (b[2] - a[2]) * (p[0] - a[0]) - (b[0] - a[0]) * (p[2] - a[2]);
}
auto enclosed(Scalar b, long double x) -> bool {
  return finite(b) && contains({b.lower, b.upper}, x);
}
void edge_oracle(const BoardingFootSiteEdgeEvidence& e, PlainPoint a,
                 PlainPoint b, PlainPoint p) {
  const auto side = side_value(a, b, p), dx = b[0] - a[0], dz = b[2] - a[2];
  const auto length = dx * dx + dz * dz;
  const auto radius =
      static_cast<long double>(.020) + static_cast<long double>(.010);
  check(enclosed(e.signed_side, side) &&
            enclosed(e.edge_length_squared, length) &&
            enclosed(e.squared_margin_gap,
                     side * side - radius * radius * length),
        "Independent original oriented side/length/full disk gap enclosed");
}
[[gnu::noinline]] void first_oracles(const Diagnostic& d) {
  for (double g : {0., .125, .25, .5, .75}) {
    const auto selected =
        detail::boarding_route_intermediate_support01_phase(g, g);
    const Cell* c{};
    for (const auto& candidate : d.cells)
      if (selected && candidate.phase_index == *selected &&
          candidate.global_first <= g && candidate.global_last >= g) {
        c = &candidate;
        break;
      }
    if (!c || !c->phase.complete || !c->projection_complete) continue;
    charge(totals.oracles, 10);
    const auto request =
        detail::boarding_route_intermediate_support01_controls(c->phase_index);
    check(request.has_value(),
          "Immutable actual phase packet exists for accepted FIRST oracle");
    if (!request) continue;
    const auto u =
        detail::boarding_route_intermediate_support01_local(c->phase_index, g);
    const auto o = oracle(*request, u, d.reverse);
    cell_oracle(c->phase, o);
    std::array<PlainPoint, 2> pressure{};
    pressure[1] = {o.com[0].value, 0, o.com[2].value};
    const bool loaded = c->phase_index >= 3;
    if (loaded) {
      const auto* q = Access::partition(d.source, 0);
      check(q != nullptr, "Genuine intermediate source for load oracle");
      if (!q) continue;
      const auto weight = o.reaction.value;
      check(enclosed(c->port_share, weight) &&
                enclosed(c->star_share, 1 - weight),
            "Independent immutable reaction and SAME complement enclosed");
      for (std::size_t a = 0; a < 2; ++a) {
        const auto axis = a == 0 ? 0U : 2U;
        const auto half = static_cast<long double>(a == 0 ? .06 : .14);
        long double lo = component(q->perimeter_metres[0], axis), hi = lo;
        for (auto v : q->perimeter_metres) {
          lo = std::min(lo, static_cast<long double>(component(v, axis)));
          hi = std::max(hi, static_cast<long double>(component(v, axis)));
        }
        lo = std::max(lo, o.soles[0][axis].value - half);
        hi = std::min(hi, o.soles[0][axis].value + half);
        check(lo < hi,
              "Actual source/fullsole positive area at corroboration point");
        pressure[0][axis] = (lo + hi) * .5L;
        pressure[1][axis] =
            (o.com[axis].value - weight * pressure[0][axis]) / (1 - weight);
        check(enclosed(c->pressure_xz[0][a], pressure[0][axis]),
              "Actual fixed source-centered portCOP enclosed");
        check(std::abs(weight * pressure[0][axis] +
                       (1 - weight) * pressure[1][axis] - o.com[axis].value) <
                  256 * std::numeric_limits<long double>::epsilon(),
              "Independent nominal moment balance");
      }
    }
    for (std::size_t side = loaded ? 0U : 1U; side < 2; ++side) {
      const auto* q = Access::partition(d.source, side);
      check(q != nullptr, "Same-owned genuine finite source for edge oracle");
      if (!q) continue;
      for (std::size_t a = 0; a < 2; ++a)
        check(enclosed(c->pressure_xz[side][a], pressure[side][a == 0 ? 0 : 2]),
              "Independent genuine pressure expression enclosed");
      const auto x = o.soles[side][0].value, z = o.soles[side][2].value;
      const auto hx = static_cast<long double>(.06),
                 hz = static_cast<long double>(.14);
      const std::array<PlainPoint, 4> corners{{{x + hx, 0, z - hz},
                                               {x - hx, 0, z - hz},
                                               {x - hx, 0, z + hz},
                                               {x + hx, 0, z + hz}}};
      for (std::size_t e = 0; e < 4; ++e) {
        edge_oracle(c->sites[side].sole_edges[e], corners[e],
                    corners[(e + 1) % 4], pressure[side]);
        edge_oracle(c->sites[side].source_edges[e],
                    plain(q->perimeter_metres[e]),
                    plain(q->perimeter_metres[(e + 1) % 4]), pressure[side]);
      }
    }
    if (g == 0 && c->endpoint_earned[0]) {
      const auto* upper =
          detail::boarding_route_intermediate_unloaded01_upper_partition(
              d.source);
      check(upper != nullptr,
            "Genuine provider-owned finite trapezoid borrowed");
      if (upper) {
        PlainPoint port{o.soles[0][0].value, 0, o.soles[0][2].value};
        long double minimum = std::numeric_limits<long double>::infinity();
        for (std::size_t e = 0; e < 4; ++e)
          minimum = std::min(
              minimum,
              side_value(plain(upper->perimeter_metres[e]),
                         plain(upper->perimeter_metres[(e + 1) % 4]), port));
        check(enclosed(c->upper_minimum_signed_side[0], minimum) && minimum > 0,
              "Actual upper polygon strict witness corroborated without "
              "pressure disk");
      }
    }
    if (g == .125 && c->endpoint_earned[1]) {
      const auto* upper =
          detail::boarding_route_intermediate_unloaded01_upper_partition(
              d.source);
      if (upper) {
        long double minz = upper->perimeter_metres[0].z;
        for (auto v : upper->perimeter_metres)
          minz = std::min(minz, static_cast<long double>(v.z));
        check(
            enclosed(c->departure_gap, minz - (o.soles[0][2].value +
                                               static_cast<long double>(.14))),
            "Independent full footprint departure, not center-only separation");
      }
    }
    if (g == .5 && c->endpoint_earned[2]) {
      const auto* q = Access::partition(d.source, 0);
      if (q)
        for (std::size_t a = 0; a < 2; ++a) {
          const auto axis = a == 0 ? 0U : 2U;
          const auto half = static_cast<long double>(a == 0 ? .06 : .14);
          long double lo = component(q->perimeter_metres[0], axis), hi = lo;
          for (auto v : q->perimeter_metres) {
            lo = std::min(lo, static_cast<long double>(component(v, axis)));
            hi = std::max(hi, static_cast<long double>(component(v, axis)));
          }
          lo = std::max(lo, o.soles[0][axis].value - half);
          hi = std::min(hi, o.soles[0][axis].value + half);
          check(enclosed(c->intermediate_intersection[a][0], lo) &&
                    enclosed(c->intermediate_intersection[a][1], hi) && lo < hi,
                "Actual zero-force full rectangle intersection corroborated");
        }
    }
  }
}
constexpr std::array<std::array<double, 2>, 19> requests{{{0, 1},
                                                          {1, 0},
                                                          {0, .5},
                                                          {.5, 0},
                                                          {.5, 1},
                                                          {1, .5},
                                                          {0, .125},
                                                          {.125, .25},
                                                          {.25, .5},
                                                          {.5, .75},
                                                          {.75, 1},
                                                          {0, 0},
                                                          {.5, .5},
                                                          {1, 1},
                                                          {.125, .125},
                                                          {.25, .25},
                                                          {.75, .75},
                                                          {.0625, .375},
                                                          {.4375, .8125}}};
[[gnu::noinline]] void print(std::size_t slot, const Diagnostic& d) {
  std::cout << "FIRST_SUPPORT01 A" << slot << " range=" << d.requested_first
            << ',' << d.requested_last
            << " state=" << static_cast<unsigned>(d.state)
            << " complete=" << d.complete << " flags=" << d.source_enrolled
            << d.arithmetic_supported << d.kinematic_complete
            << d.nonnegative_reactions << d.nominal_equilibrium
            << d.star_support << d.prefix_zero_geometry_complete
            << d.nominal_support_complete
            << " clock=" << d.reporting_elapsed_seconds
            << " cells=" << d.cells.size() << " nodes=" << d.examined_nodes
            << " splits=" << d.mandatory_splits << " depth=" << d.maximum_depth
            << " calls=" << d.work.phase_calls << " old=" << d.work.phase.graphs
            << ',' << d.work.phase.legs << ',' << d.work.phase.bodies << ','
            << d.work.phase.sectors << ',' << d.work.phase.timing << " new=";
  for (auto n : new_work(d.work))
    std::cout << n << ',';
  std::cout << " joins=";
  for (bool b : d.qualified_joins)
    std::cout << b;
  std::cout << " endpoints=";
  for (bool b : d.prefix_endpoint_earned)
    std::cout << b;
  std::cout << " bytes=" << d.output_capacity_bytes
            << " stop=" << static_cast<unsigned>(d.stop_condition);
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    std::cout << " refusal=" << static_cast<unsigned>(r.condition) << '/'
              << static_cast<unsigned>(r.predicate_condition)
              << " interval=" << r.first << ',' << r.last
              << " phase=" << r.phase_index.value_or(5)
              << " side=" << r.side.value_or(2)
              << " edge=" << r.edge.value_or(4)
              << " op=" << r.operation.value_or(12)
              << " bound=" << r.limiting_bound.lower << ','
              << r.limiting_bound.upper << ':' << r.limiting_bound.supported
              << " oldcause=" << static_cast<unsigned>(r.phase.condition);
    if (r.source_key)
      std::cout << " key=" << static_cast<unsigned>(r.source_key->buffer) << ','
                << r.source_key->group << ',' << r.source_key->triangle;
    std::cout << " name=" << r.source_name;
  }
  std::cout << '\n' << std::flush;
}
[[gnu::noinline]] void first_one(const Provider& p, std::size_t slot,
                                 Summary& summary) {
  charge(totals.consumers, 112);
  const auto r = requests[slot - 1];
  auto result =
      assess_origin_boarding_route_intermediate_support01(p, r[0], r[1]);
  if (!result) {
    std::cout << "FIRST_SUPPORT01 A" << slot << " API_ERROR=" << result.error()
              << '\n'
              << std::flush;
    check(!result.error().empty(),
          "Unknown FIRST API refusal preserved without retry");
    return;
  }
  print(slot, *result);
  accounting(*result, Limits{});
  if (slot == 1) capture(*result, summary.whole);
  if (slot == 2 && summary.whole.exists)
    check(semantic_hash(*result) == summary.whole.hash,
          "Whole reverse retains canonical geometry/cause and physical jets");
  if (slot == 5 || slot == 12 || slot == 13)
    capture(*result, summary.anchors, true, slot);
  if (slot <= 2) first_oracles(*result);
}
[[gnu::noinline]] void create_source(const NativeCraftBinding& binding,
                                     const OriginBoardingBootSupport& boots,
                                     std::optional<Provider>& p) {
  charge(totals.creators, 1);
  auto result = make_origin_boarding_intermediate_pause_support(binding, boots);
  std::cout << "FIRST_SUPPORT01_SOURCE admitted=" << result.has_value();
  if (!result)
    std::cout << " error=" << result.error();
  else if (const auto* e = result->summary())
    std::cout << " complete=" << e->complete
              << " bytes=" << e->actual_source_bytes
              << " condition=" << static_cast<unsigned>(e->condition)
              << " work=" << e->work.base_guards << ',' << e->work.metadata_rows
              << ',' << e->work.face_reads << ',' << e->work.quad_records;
  std::cout << '\n' << std::flush;
  if (result) {
    check(Access::valid(*result),
          "Only genuine unchanged public creator issues source");
    p.emplace(std::move(*result));
  }
}
void hash_request(Hash& h, const Request& r) {
  for (const auto& rootpoint : r.root)
    for (const auto& c : rootpoint.coordinates) {
      h.add(c.count);
      for (auto n : c.terms)
        h.number(n);
    }
  for (const auto& feet : r.feet) {
    for (const auto& p : feet.sole)
      for (const auto& c : p.coordinates) {
        h.add(c.count);
        for (auto n : c.terms)
          h.number(n);
      }
    for (auto n : feet.yaw_half)
      h.number(n);
    h.number(feet.swing_height_metres);
  }
  for (const auto& a :
       {r.root_yaw_half, r.torso_lean_half, r.port_reaction_fraction}) {
    for (auto n : a)
      h.number(n);
  }
  h.number(r.seconds_per_parameter);
}
auto equal_request(const Request& a, const Request& b) -> bool {
  for (std::size_t end = 0; end < 2; ++end)
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const auto& x = a.root[end].coordinates[axis];
      const auto& y = b.root[end].coordinates[axis];
      if (x.count != y.count || x.terms != y.terms) return false;
      for (std::size_t side = 0; side < 2; ++side) {
        const auto& u = a.feet[side].sole[end].coordinates[axis];
        const auto& v = b.feet[side].sole[end].coordinates[axis];
        if (u.count != v.count || u.terms != v.terms) return false;
      }
    }
  for (std::size_t side = 0; side < 2; ++side)
    if (a.feet[side].yaw_half != b.feet[side].yaw_half ||
        a.feet[side].swing_height_metres != b.feet[side].swing_height_metres)
      return false;
  return a.root_yaw_half == b.root_yaw_half &&
         a.torso_lean_half == b.torso_lean_half &&
         a.port_reaction_fraction == b.port_reaction_fraction &&
         a.seconds_per_parameter == b.seconds_per_parameter;
}
[[gnu::noinline]] void immutable_controls() {
  Hash all;
  for (std::size_t i = 0; i < 5; ++i) {
    const auto current =
        detail::boarding_route_intermediate_support01_controls(i);
    const auto old =
        i < 3 ? detail::boarding_route_intermediate_unloaded01_controls(i)
              : detail::boarding_route_intermediate_load01_controls(i);
    check(current.has_value() && old.has_value(),
          "Exact authentic immutable inspector packets available");
    if (!current || !old) continue;
    check(equal_request(*current, *old),
          "Every original control/term/hump/load/T retained exactly");
    hash_request(all, *current);
    check(current->seconds_per_parameter == (i == 4 ? 2 : 12),
          "Original physical12/12/12/12/2 duration");
  }
  recipe_hash = all.value;
  for (std::size_t join = 0; join < 4; ++join) {
    const auto left =
        detail::boarding_route_intermediate_support01_controls(join);
    const auto right =
        detail::boarding_route_intermediate_support01_controls(join + 1);
    if (!left || !right) continue;
    check(
        detail::boarding_route_intermediate_support01_join_math(*left, *right),
        "Exact four structural physicalC2 joins (hump/T may differ)");
    for (std::size_t field = 0; field < 4; ++field) {
      auto changed = *right;
      auto& value = field == 0   ? changed.root[0].coordinates[0].terms[0]
                    : field == 1 ? changed.root_yaw_half[0]
                    : field == 2 ? changed.torso_lean_half[0]
                                 : changed.port_reaction_fraction[0];
      value = std::nextafter(value, std::numeric_limits<double>::infinity());
      check(!detail::boarding_route_intermediate_support01_join_math(*left,
                                                                     changed),
            "OneULP immutable endpoint tuple mismatch rejects exact join, no "
            "source authority");
    }
  }
  constexpr std::array cuts{0., .125, .25, .5, .75, 1.};
  constexpr std::array seconds{0., 12., 24., 36., 48., 50.};
  for (std::size_t i = 0; i < 6; ++i)
    check(detail::boarding_route_intermediate_support01_clock(cuts[i]) ==
              seconds[i],
          "Physical piecewise clock exact cuts");
  for (std::size_t i = 1; i < 5; ++i) {
    check(detail::boarding_route_intermediate_support01_phase(
              cuts[i], cuts[i]) == i - 1 &&
              detail::boarding_route_intermediate_support01_phase(
                  cuts[i], cuts[i + 1]) == i,
          "Preceding points/following positive widths");
  }
}
void lower_refusal(const Diagnostic& d, std::size_t field) {
  if (field < 4) {
    const std::array c{Condition::depth_capacity, Condition::node_capacity,
                       Condition::leaf_capacity, Condition::output_capacity};
    check(d.stop_condition == c[field],
          "Reached shared navigation/output cap is exact terminal");
  } else if (field < 9) {
    const std::array c{BoardingRouteFootPhaseCondition::graph_capacity,
                       BoardingRouteFootPhaseCondition::leg_capacity,
                       BoardingRouteFootPhaseCondition::body_capacity,
                       BoardingRouteFootPhaseCondition::sector_capacity,
                       BoardingRouteFootPhaseCondition::timing_capacity};
    check(d.first_refusal && d.first_refusal->phase.condition == c[field - 4],
          "Original phase capacity evidence/cause preserved");
  } else
    check(d.stop_condition == capacity_conditions[field - 9] &&
              d.state == State::capacity,
          "Reached hard capacity survives outer ordinary wrappers");
}
[[gnu::noinline]] void assessment_one(
    const Provider& p, double first, double last, const Limits& l,
    std::optional<std::size_t> lower = std::nullopt, bool invalid = false,
    bool empty = false, const std::uint64_t* hash = nullptr) {
  charge(totals.consumers, 112);
  auto result =
      detail::boarding_route_intermediate_support01_bounded(p, first, last, l);
  if (invalid) {
    check(!result,
          "Invalid range/upper/output/environment rejects before report");
    return;
  }
  if (!result) {
    check(false,
          "Valid fixed control returns honest report, actual error follows");
    std::cerr << "CONTROL_ERROR " << result.error() << '\n';
    return;
  }
  accounting(*result, l);
  if (lower) lower_refusal(*result, *lower);
  if (hash)
    check(semantic_hash(*result) == *hash,
          "Exact/lifetime replay retains canonical physical evidence");
  if (empty)
    check(!result->source_enrolled && result->reporting_elapsed_seconds == 0 &&
              result->work.phase_calls == 0 && result->work.phase.graphs == 0 &&
              result->work.projection_guards == 0 &&
              result->work.pressure_candidates == 0 && result->first_refusal &&
              result->first_refusal->condition == Condition::invalid_binding,
          "Genuine moved-empty owner refuses before child/envelope work");
}
constexpr auto anchor_slot(std::size_t field) -> std::size_t {
  return field >= 14 && field <= 21   ? 5
         : field == 22 || field == 24 ? 12
         : field == 23                ? 13
                                      : 1;
}
[[gnu::noinline]] void cap_roster(const Provider& p, const Summary& summary) {
  for (std::size_t field = 0; field < 25; ++field) {
    const auto r = requests[anchor_slot(field) - 1];
    Limits l;
    set_limit(l, field, 0);
    assessment_one(p, r[0], r[1], l, field, field == 3);
  }
  for (std::size_t field = 0; field < 25; ++field) {
    const auto r = requests[anchor_slot(field) - 1];
    Limits l;
    set_limit(l, field, get_limit(l, field) + 1);
    assessment_one(p, r[0], r[1], l, std::nullopt, true);
  }
  for (std::size_t field = 0; field < 25; ++field) {
    const bool anchor = field >= 14;
    const auto& b = anchor ? summary.anchors : summary.whole;
    const bool exists =
        anchor ? (b.reached & (std::uint32_t{1} << field)) != 0 : b.exists;
    const auto used = b.used[field];
    if (!exists || used <= 1) {
      std::cout << "SKIP A" << 70 + field << " unreached-or-duplicate\n";
      continue;
    }
    const auto r = requests[anchor_slot(field) - 1];
    Limits l;
    set_limit(l, field, used - 1);
    assessment_one(p, r[0], r[1], l, field,
                   field == 3 &&
                       used - 1 <
                           2 * sizeof(std::expected<Diagnostic, std::string>));
  }
}
[[gnu::noinline]] void exact_roster(const Provider& p, const Summary& s) {
  if (!s.whole.exists) {
    std::cout << "SKIP A95-A96 no-whole\n";
    return;
  }
  Limits l;
  for (std::size_t i = 0; i < 25; ++i)
    set_limit(l, i, s.whole.used[i]);
  assessment_one(p, 0, 1, l, std::nullopt, false, false, &s.whole.hash);
  assessment_one(p, 1, 0, l, std::nullopt, false, false, &s.whole.hash);
}
[[gnu::noinline]] void remaining_roster(Provider& p, const Summary& s) {
  for (std::size_t mode = 0; mode < 6; ++mode) {
    Environment e(mode);
    if (e.available)
      assessment_one(p, 0, 1, Limits{}, std::nullopt, true);
    else
      std::cout << "SKIP A" << 97 + mode << " unsupported-FP-platform\n";
  }
  assessment_one(p, std::numeric_limits<double>::quiet_NaN(), 1, Limits{},
                 std::nullopt, true);
  assessment_one(p, 0, std::numeric_limits<double>::infinity(), Limits{},
                 std::nullopt, true);
  assessment_one(p,
                 std::nextafter(0., -std::numeric_limits<double>::infinity()),
                 1, Limits{}, std::nullopt, true);
  assessment_one(p, 0,
                 std::nextafter(1., std::numeric_limits<double>::infinity()),
                 Limits{}, std::nullopt, true);
  {
    auto alias = p;
    auto held = std::move(alias);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Empty handle contract.
    assessment_one(alias, 0, 1, Limits{}, std::nullopt, false, true);
    alias = std::move(held);
  }
  {
    auto held = std::move(p);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Empty handle contract.
    assessment_one(p, 0, 1, Limits{}, std::nullopt, false, true);
    p = std::move(held);
  }
  {
    auto alias = p;
    p = std::move(alias);
    assessment_one(p, 0, 1, Limits{}, std::nullopt, false, false,
                   s.whole.exists ? &s.whole.hash : nullptr);
  }
  {
    auto moved = std::move(p);
    assessment_one(moved, 0, 1, Limits{}, std::nullopt, false, false,
                   s.whole.exists ? &s.whole.hash : nullptr);
    p = std::move(moved);
  }
  {
    Limits l;
    l.phase.bodies = 0;
    l.phase.sectors = 0;
    assessment_one(p, 0, 1, l, 7);
  }
  {
    Limits l;
    l.reaction_operations = 0;
    l.division_quotients = 0;
    assessment_one(p, 1, 1, l, 19);
  }
}
[[gnu::noinline]] void final_counts() {
  check(totals.creators == 1 && totals.consumers <= 112 &&
            totals.oracles <= 10 && totals.phase_calls <= 229264,
        "Finite112/onecreator/raw0/max10 roster");
  std::cout << "VALIDATION creators=" << totals.creators
            << " consumers=" << totals.consumers
            << " calls=" << totals.phase_calls
            << " raw=0 poses=" << totals.oracles << " checks=" << checks
            << " failures=" << failures << " work=";
  for (auto n : totals.work)
    std::cout << n << ',';
  std::cout << '\n';
}
} // namespace
int main() {
  std::size_t bytes{};
  StreamCounter out(std::cout.rdbuf(), bytes), err(std::cerr.rdbuf(), bytes);
  auto* oldout = std::cout.rdbuf(&out);
  auto* olderr = std::cerr.rdbuf(&err);
  try {
    std::cout << std::setprecision(17);
    auto binding = make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{});
    if (!binding) throw std::runtime_error(binding.error());
    auto boots = make_origin_boarding_boot_support(*binding);
    if (!boots) throw std::runtime_error(boots.error());
    Summary summary;
    std::optional<Provider> source;
    create_source(*binding, *boots, source);
    if (source) {
      immutable_controls();
      for (std::size_t slot = 1; slot <= 19; ++slot)
        first_one(*source, slot, summary);
      cap_roster(*source, summary);
      exact_roster(*source, summary);
      remaining_roster(*source, summary);
    }
    final_counts();
  } catch (const std::exception& e) {
    check(false, "Unexpected harness exception");
    std::cerr << e.what() << '\n';
  }
  const bool streams = std::cout.good() && std::cerr.good() && !out.truncated &&
                       !err.truncated && bytes <= 16384;
  std::cout.rdbuf(oldout);
  std::cerr.rdbuf(olderr);
  check(streams, "Shared16KiB execution stream not truncated");
  return failures ? 1 : 0;
}
