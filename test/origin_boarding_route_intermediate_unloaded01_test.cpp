#include "apsis_drift/origin_boarding_route_intermediate_unloaded01.hpp"
#include "origin_boarding_route_intermediate_step02_internal.hpp"
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
using Diagnostic = BoardingRouteIntermediateUnloaded01Diagnostic;
using Cell = BoardingRouteIntermediateUnloaded01Cell;
using PhaseCell = BoardingRouteFootPhaseCell;
using Request = BoardingRouteFootPhaseRequest;
using Constant = BoardingRoutePhaseConstant;
using ConstantPoint = BoardingRoutePhasePointConstant;
using State = BoardingRouteIntermediateUnloaded01State;
using Condition = BoardingRouteIntermediateUnloaded01Condition;
using Scalar = BoardingFootSiteScalarBounds;
using Limits = detail::BoardingRouteIntermediateUnloaded01Limits;
using GeometryLimits =
    detail::BoardingRouteIntermediateUnloaded01GeometryLimits;
using GeometryInput = detail::BoardingRouteIntermediateUnloaded01GeometryInput;
using GeometryMode = detail::BoardingRouteIntermediateUnloaded01GeometryMode;
using Access = detail::BoardingIntermediatePauseSupportAccess;
static_assert(!std::is_default_constructible_v<
              detail::BoardingRouteIntermediateUnloaded01AdmissionContext>);
static_assert(!std::is_constructible_v<
              detail::BoardingRouteIntermediateUnloaded01AdmissionContext,
              const Diagnostic&>);
static_assert(!std::is_default_constructible_v<
              detail::BoardingRouteIntermediateUnloaded01CurrentCellToken>);
static_assert(
    !std::is_constructible_v<
        detail::BoardingRouteIntermediateUnloaded01CurrentCellToken,
        const Diagnostic&, const Cell&, const BoardingRouteFootPhaseRequest&,
        const detail::BoardingRouteIntermediateUnloaded01AdmissionContext&>);
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
  std::array<std::uint32_t, 18> work{};
  std::size_t creators{}, consumers{}, math{}, oracles{}, phase_calls{};
} totals;
static_assert(sizeof(Totals) <= 128);
void charge(std::size_t& n, std::size_t maximum) {
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
// Restores exception flags as well as modes between literal division slots.
class SavedEnvironment {
 public:
  SavedEnvironment() {
    saved_ = std::fegetenv(&environment_) == 0;
#if defined(__SSE2__) && defined(__x86_64__)
    control_ = _mm_getcsr();
#endif
  }
  ~SavedEnvironment() {
    if (saved_) std::fesetenv(&environment_);
#if defined(__SSE2__) && defined(__x86_64__)
    _mm_setcsr(control_);
#endif
  }
  SavedEnvironment(const SavedEnvironment&) = delete;
  auto operator=(const SavedEnvironment&) -> SavedEnvironment& = delete;

 private:
  std::fenv_t environment_{};
  bool saved_{};
#if defined(__SSE2__) && defined(__x86_64__)
  unsigned control_{};
#endif
};
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
auto scalar(double x) -> Scalar {
  return {x, x, true};
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
constexpr std::array<std::size_t Limits::*, 9> members{
    &Limits::source_guards,
    &Limits::projection_guards,
    &Limits::definition_guards,
    &Limits::pressure_candidates,
    &Limits::disk_edges,
    &Limits::upper_geometry_edges,
    &Limits::intermediate_coordinates,
    &Limits::endpoint_operations,
    &Limits::event_guards};
auto new_work(const BoardingRouteIntermediateUnloaded01Counters& w)
    -> std::array<std::size_t, 9> {
  return {w.source_guards,
          w.projection_guards,
          w.definition_guards,
          w.pressure_candidates,
          w.disk_edges,
          w.upper_geometry_edges,
          w.intermediate_coordinates,
          w.endpoint_operations,
          w.event_guards};
}
auto get_limit(const Limits& l, std::size_t i) -> std::uint64_t {
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
void set_limit(Limits& l, std::size_t i, std::uint64_t n) {
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
  h.add(d.unloaded_version);
  h.number(std::min(d.requested_first, d.requested_last));
  h.number(std::max(d.requested_first, d.requested_last));
  h.add(static_cast<unsigned>(d.state));
  h.add(static_cast<unsigned>(d.stop_condition));
  h.add(d.complete);
  h.number(d.reporting_elapsed_seconds);
  h.add(d.examined_nodes);
  h.add(d.maximum_depth);
  h.add(d.mandatory_splits);
  h.add(d.work.phase_calls);
  for (auto x : new_work(d.work))
    h.add(x);
  for (auto x : {d.work.phase.graphs, d.work.phase.legs, d.work.phase.bodies,
                 d.work.phase.sectors, d.work.phase.timing})
    h.add(x);
  for (bool x : d.qualified_joins)
    h.add(x);
  for (bool x : d.endpoint_earned)
    h.add(x);
  h.add(d.cells.size());
  if (d.source_enrolled)
    for (const auto& p : d.parts) {
      h.add(static_cast<unsigned>(p.id));
      h.add(static_cast<unsigned>(p.mass.first));
      h.add(static_cast<unsigned>(p.mass.second));
      h.add(p.mass.weight);
      h.add(p.reservation.index());
      if (const auto* box =
              std::get_if<BoardingRoutePhaseBoxBinding>(&p.reservation)) {
        h.add(static_cast<unsigned>(box->center));
        h.add(static_cast<unsigned>(box->frame));
        h.number(box->half_size_metres.x);
        h.number(box->half_size_metres.y);
        h.number(box->half_size_metres.z);
      } else {
        const auto& cap =
            std::get<BoardingPlantedBodyCapsuleBinding>(p.reservation);
        h.add(static_cast<unsigned>(cap.start));
        h.add(static_cast<unsigned>(cap.end));
        h.number(cap.radius_metres);
      }
    }
  for (const auto& c : d.cells) {
    h.number(c.global_first);
    h.number(c.global_last);
    h.add(c.phase_index);
    h.bound(c.port_share);
    h.bound(c.star_share);
    for (const auto& p : c.phase.points)
      hash_jet(h, p, d.reverse);
    for (const auto& p : c.phase.mass_points)
      hash_jet(h, p, d.reverse);
    hash_jet(h, c.phase.center_of_mass, d.reverse);
    for (const auto& f : c.phase.frames)
      for (const auto& col : f.columns)
        hash_jet(h, col, d.reverse);
    for (auto b : c.starcop_xz)
      h.bound(b);
    for (std::size_t e = 0; e < 4; ++e) {
      h.add(c.star_site.sole_evaluated[e]);
      h.add(c.star_site.source_evaluated[e]);
      for (const auto* a :
           {&c.star_site.sole_edges[e], &c.star_site.source_edges[e]}) {
        h.bound(a->signed_side);
        h.bound(a->squared_margin_gap);
        h.add(a->disk_contained);
      }
    }
    for (bool b : c.endpoint_scope)
      h.add(b);
    for (bool b : c.endpoint_earned)
      h.add(b);
    for (bool b : c.event_evaluated)
      h.add(b);
    h.add(static_cast<unsigned>(c.upper_class));
    h.add(static_cast<unsigned>(c.upper_plane_event));
    h.add(static_cast<unsigned>(c.intermediate_plane_event));
    for (auto b : c.upper_minimum_signed_side)
      h.bound(b);
    h.bound(c.departure_gap);
    for (const auto& pair : c.intermediate_intersection)
      for (auto b : pair)
        h.bound(b);
  }
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    h.add(static_cast<unsigned>(r.condition));
    h.add(static_cast<unsigned>(r.predicate_condition));
    h.number(r.first);
    h.number(r.last);
    h.number(r.local_first);
    h.number(r.local_last);
    h.add(r.depth);
    h.add(r.phase_index.value_or(3));
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
struct Baseline {
  std::array<std::uint32_t, 18> used{}, endpoint_used{};
  std::uint64_t hash{}, endpoint_hash{};
  bool exists{}, complete{}, endpoint_exists{}, endpoint_complete{};
};
struct Summary {
  std::array<Baseline, 2> whole;
};
static_assert(sizeof(Baseline) <= 200 && sizeof(Summary) <= 400);
[[gnu::noinline]] void summarize(const Diagnostic& d, Baseline& b,
                                 bool endpoint = false) {
  auto& used = endpoint ? b.endpoint_used : b.used;
  used[0] = static_cast<std::uint32_t>(d.maximum_depth);
  used[1] = static_cast<std::uint32_t>(d.examined_nodes);
  const auto slots =
      d.cells.size() + (!d.complete && d.source_enrolled ? 1U : 0U);
  used[2] = static_cast<std::uint32_t>(slots);
  used[3] = static_cast<std::uint32_t>(
      2 * sizeof(std::expected<Diagnostic, std::string>) +
      slots * sizeof(Cell));
  const std::array old{d.work.phase.graphs, d.work.phase.legs,
                       d.work.phase.bodies, d.work.phase.sectors,
                       d.work.phase.timing};
  for (std::size_t i = 0; i < 5; ++i)
    used[i + 4] = static_cast<std::uint32_t>(old[i]);
  const auto nw = new_work(d.work);
  for (std::size_t i = 0; i < 9; ++i)
    used[i + 9] = static_cast<std::uint32_t>(nw[i]);
  if (endpoint) {
    b.endpoint_hash = semantic_hash(d);
    b.endpoint_exists = true;
    b.endpoint_complete = d.complete;
  } else {
    b.hash = semantic_hash(d);
    b.exists = true;
    b.complete = d.complete;
  }
}
void authority() {
  check(!Diagnostic::self_qualified && !Diagnostic::material_qualified &&
            !Diagnostic::world_qualified && !Diagnostic::route_qualified &&
            !Diagnostic::seat_qualified && !Diagnostic::actor_qualified &&
            !Diagnostic::save_qualified && !Diagnostic::dynamics_qualified &&
            !Diagnostic::first_flight_qualified,
        "Nominal support/events never grant full route or actor authority");
}
[[gnu::noinline]] void accounting(const Diagnostic& d, const Limits& l) {
  authority();
  check(d.unloaded_version == 1, "Immutable selected version");
  if (d.source_enrolled) {
    check(d.work.source_guards == 32 && mask_count(d.source_evaluated) == 32,
          "Source enrolled once with all32 actual identity guards");
    check(d.reporting_elapsed_seconds ==
              std::abs(detail::boarding_route_intermediate_unloaded01_clock(
                           d.requested_last) -
                       detail::boarding_route_intermediate_unloaded01_clock(
                           d.requested_first)),
          "Physical clock assigned only after source admission");
  } else
    check(d.reporting_elapsed_seconds == 0,
          "Unenrolled report retains unassigned clock");
  check(d.examined_nodes <= l.phase.nodes && d.maximum_depth <= l.phase.depth &&
            d.cells.size() <= l.phase.leaves &&
            d.output_capacity_bytes <= l.phase.output_bytes,
        "Shared navigation/leaf/actual allocation caps");
  check(d.output_capacity_bytes ==
                2 * sizeof(std::expected<Diagnostic, std::string>) +
                    d.cells.capacity() * sizeof(Cell) ||
            !d.source_enrolled,
        "Owned output includes two headers and one actual vector capacity");
  const std::array old{d.work.phase.graphs, d.work.phase.legs,
                       d.work.phase.bodies, d.work.phase.sectors,
                       d.work.phase.timing};
  for (std::size_t i = 0; i < 5; ++i) {
    check(old[i] <= get_limit(l, i + 4),
          "Original compiler counters respect actual caps");
    aggregate(i, old[i]);
  }
  const auto nw = new_work(d.work);
  for (std::size_t i = 0; i < 9; ++i) {
    check(nw[i] <= l.*members[i], "Every new operation charged before work");
    aggregate(i + 5, nw[i]);
  }
  totals.phase_calls += d.work.phase_calls;
  check(d.work.phase.graphs <= d.work.phase_calls &&
            d.work.phase_calls <= d.examined_nodes,
        "Guarded calls counted separately from accepted graphs/navigation");
  check(mask_count(d.source_evaluated) <= d.work.source_guards,
        "No uncharged source mask");
  const auto first = std::min(d.requested_first, d.requested_last),
             last = std::max(d.requested_first, d.requested_last);
  auto cursor = first;
  std::size_t definitions{}, events{}, edges{}, upper{}, coordinates{},
      operations{};
  std::array<bool, 2> joins{};
  std::array<bool, 3> endpoints{};
  const Cell* previous{};
  for (const auto& c : d.cells) {
    check(c.global_first == cursor && c.global_last >= cursor &&
              c.global_last <= last,
          "Actual accepted closed prefix without gaps");
    cursor = c.global_last;
    check(detail::boarding_route_intermediate_unloaded01_phase(
              c.global_first, c.global_last) == c.phase_index &&
              c.phase.first ==
                  detail::boarding_route_intermediate_unloaded01_local(
                      c.phase_index, c.global_first) &&
              c.phase.last ==
                  detail::boarding_route_intermediate_unloaded01_local(
                      c.phase_index, c.global_last),
          "Canonical preceding-point/following-width phase mapping");
    check(c.complete && c.phase.complete && c.projection_complete &&
              c.nominal_equilibrium && c.star_support && c.port_zero_geometry,
          "Only complete genuine current support/event cells append");
    check(mask_count(c.projected_carrier_complete) == 11 &&
              mask_count(c.definition_evaluated) == 32 &&
              mask_count(c.event_evaluated) == 16,
          "All accepted current projection/definition/event masks complete");
    accepted_timing(c.phase);
    if (c.phase.first == c.phase.last &&
        (c.phase.first == 0 || c.phase.first == 1))
      zero_endpoint_jets(c.phase);
    check(finite(c.port_share) && c.port_share.lower == 0 &&
              c.port_share.upper == 0 && finite(c.star_share) &&
              c.star_share.lower == 1 && c.star_share.upper == 1 &&
              !c.port_loaded && c.port_zero_only && c.star_site.loaded,
          "Port exact functional zero remains unloaded; star exact complement "
          "one");
    check(c.phase.port_reaction_fraction.lower <= 0 &&
              c.phase.port_reaction_fraction.upper >= 0,
          "Original share enclosure preserved without clamping");
    check(c.star_site.complete && c.star_site.plane_identity &&
              mask_count(c.star_site.sole_evaluated) == 4 &&
              mask_count(c.star_site.source_evaluated) == 4,
          "Loaded star uses both full finite polygons");
    for (std::size_t a = 0; a < 2; ++a)
      check(finite(c.com_xz[a]) && finite(c.starcop_xz[a]) &&
                c.com_xz[a].lower == c.starcop_xz[a].lower &&
                c.com_xz[a].upper == c.starcop_xz[a].upper,
            "Star COP is SAME actual COM expression");
    for (std::size_t e = 0; e < 4; ++e)
      for (const auto* b :
           {&c.star_site.sole_edges[e], &c.star_site.source_edges[e]})
        check(b->disk_contained && finite(b->signed_side) &&
                  finite(b->squared_margin_gap) && b->signed_side.lower > 0 &&
                  b->squared_margin_gap.lower >= 0,
              "Unchanged strict side and full .030 disk radius proof");
    const std::array boundaries{0., .125, .5};
    for (std::size_t i = 0; i < 3; ++i) {
      const bool contains_endpoint =
          c.global_first <= boundaries[i] && c.global_last >= boundaries[i];
      check(c.endpoint_scope[i] == contains_endpoint &&
                c.endpoint_evaluated[i] == contains_endpoint &&
                c.endpoint_earned[i] == contains_endpoint,
            "Endpoint geometry only from accepted containing cell");
      endpoints[i] = endpoints[i] || c.endpoint_earned[i];
    }
    if (c.endpoint_scope[0])
      check(finite(c.upper_minimum_signed_side[0]) &&
                c.upper_minimum_signed_side[0].lower > 0 &&
                mask_count(c.upper_side_evaluated[0]) == 4,
            "Actual upper trapezoid strict zero-load start witness");
    if (c.endpoint_scope[1])
      check(finite(c.departure_gap) && c.departure_gap.lower > 0,
            "Full sole endpoint departure rather than center-only claim");
    if (c.endpoint_scope[2])
      for (std::size_t a = 0; a < 2; ++a)
        check(c.positive_overlap[a] &&
                  finite(c.intermediate_intersection[a][0]) &&
                  finite(c.intermediate_intersection[a][1]) &&
                  c.intermediate_intersection[a][0].upper <
                      c.intermediate_intersection[a][1].lower,
              "Unshrunk finite rectangle overlap strict on both axes");
    if (c.phase_index == 0)
      check(c.upper_plane_event ==
                    BoardingRouteIntermediateUnloaded01PlaneEvent::equal &&
                c.upper_class !=
                    BoardingRouteIntermediateUnloaded01UpperClass::not_run,
            "Zero-force phase0 coplanar glide keeps honest relation "
            "classification");
    else
      check(c.upper_class ==
                BoardingRouteIntermediateUnloaded01UpperClass::not_run,
            "No fabricated upper whole-cell relation outside phase0");
    if (previous)
      for (std::size_t j = 0; j < 2; ++j) {
        const auto g = j == 0 ? .125 : .25;
        if (previous->global_last == g && c.global_first == g &&
            previous->global_last > previous->global_first &&
            c.global_last > c.global_first && previous->phase_index == j &&
            c.phase_index == j + 1)
          joins[j] = true;
      }
    previous = &c;
    definitions += mask_count(c.definition_evaluated);
    events += mask_count(c.event_evaluated);
    edges += 8;
    upper += mask_count(c.upper_side_evaluated);
    coordinates += mask_count(c.intermediate_coordinate_evaluated);
    operations += mask_count(c.endpoint_operation_evaluated);
    check(mask_count(c.endpoint_operation_evaluated) ==
              c.endpoint_operation_count,
          "Endpoint operation masks actual performed prefix");
  }
  check(definitions <= d.work.definition_guards &&
            events <= d.work.event_guards && edges <= d.work.disk_edges &&
            upper <= d.work.upper_geometry_edges &&
            coordinates <= d.work.intermediate_coordinates &&
            operations <= d.work.endpoint_operations,
        "Output evidence is charged within aggregate attempted work");
  for (std::size_t j = 0; j < 2; ++j)
    check(d.qualified_joins[j] == (joins[j] && d.join_expression_identity[j]),
          "Two-sided positive-width accepted joins earned from same cover");
  for (std::size_t i = 0; i < 3; ++i)
    check(d.endpoint_earned[i] == endpoints[i],
          "Report cannot borrow endpoint geometry from unrelated call");
  if (d.complete)
    check(!d.cells.empty() && cursor == last && !d.first_refusal &&
              d.stop_condition == Condition::none && d.star_support &&
              d.port_zero_geometry,
          "Complete selected cover has all required closed support/events");
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
  for (double g : {0., .125, .25, .5}) {
    const auto selected =
        detail::boarding_route_intermediate_unloaded01_phase(g, g);
    const Cell* c{};
    for (const auto& candidate : d.cells)
      if (selected && candidate.phase_index == *selected &&
          candidate.global_first <= g && candidate.global_last >= g) {
        c = &candidate;
        break;
      }
    if (!c || !c->phase.complete || !c->projection_complete) continue;
    charge(totals.oracles, 8);
    const auto request =
        detail::boarding_route_intermediate_unloaded01_controls(c->phase_index);
    check(request.has_value(),
          "Immutable actual phase packet exists for accepted FIRST oracle");
    if (!request) continue;
    const auto u =
        detail::boarding_route_intermediate_unloaded01_local(c->phase_index, g);
    const auto o = oracle(*request, u, d.reverse);
    cell_oracle(c->phase, o);
    PlainPoint pressure{o.com[0].value, 0, o.com[2].value};
    for (std::size_t a = 0; a < 2; ++a)
      check(enclosed(c->com_xz[a], o.com[a == 0 ? 0 : 2].value) &&
                enclosed(c->starcop_xz[a], pressure[a == 0 ? 0 : 2]),
            "Independent fifteen-mass COM is SAME star-only COP");
    const auto* star = Access::partition(d.source, 1);
    check(star != nullptr, "Genuine same-owner star partition borrowed");
    if (star) {
      const auto x = o.soles[1][0].value, z = o.soles[1][2].value,
                 hx = static_cast<long double>(.06),
                 hz = static_cast<long double>(.14);
      const std::array<PlainPoint, 4> corners{{{x + hx, 0, z - hz},
                                               {x - hx, 0, z - hz},
                                               {x - hx, 0, z + hz},
                                               {x + hx, 0, z + hz}}};
      for (std::size_t e = 0; e < 4; ++e) {
        edge_oracle(c->star_site.sole_edges[e], corners[e],
                    corners[(e + 1) % 4], pressure);
        edge_oracle(c->star_site.source_edges[e],
                    plain(star->perimeter_metres[e]),
                    plain(star->perimeter_metres[(e + 1) % 4]), pressure);
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
constexpr std::array<std::array<double, 2>, 20> requests{
    {{0, .5},        {.5, 0},        {0, .125},     {.125, 0},
     {.125, .25},    {.25, .125},    {.25, .5},     {.5, .25},
     {0, 0},         {.125, .125},   {.25, .25},    {.5, .5},
     {.0625, .1875}, {.1875, .0625}, {.0625, .375}, {.375, .0625},
     {.125, .1875},  {.1875, .125},  {.375, .5},    {.5, .375}}};
[[gnu::noinline]] void print(std::size_t slot, const Diagnostic& d) {
  std::cout << "FIRST_UNLOADED01 A" << slot << " range=" << d.requested_first
            << ',' << d.requested_last
            << " state=" << static_cast<unsigned>(d.state)
            << " complete=" << d.complete << " flags=" << d.source_enrolled
            << d.arithmetic_supported << d.kinematic_complete
            << d.nominal_equilibrium << d.star_support << d.port_zero_geometry
            << " clock=" << d.reporting_elapsed_seconds
            << " cells=" << d.cells.size() << " nodes=" << d.examined_nodes
            << " split=" << d.mandatory_splits << " depth=" << d.maximum_depth
            << " calls=" << d.work.phase_calls << " old=" << d.work.phase.graphs
            << ',' << d.work.phase.legs << ',' << d.work.phase.bodies << ','
            << d.work.phase.sectors << ',' << d.work.phase.timing << " new=";
  for (auto n : new_work(d.work))
    std::cout << n << ',';
  std::cout << " joins=" << d.qualified_joins[0] << d.qualified_joins[1]
            << " endpoints=" << d.endpoint_earned[0] << d.endpoint_earned[1]
            << d.endpoint_earned[2] << " bytes=" << d.output_capacity_bytes
            << " stop=" << static_cast<unsigned>(d.stop_condition);
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    std::cout << " refusal=" << static_cast<unsigned>(r.condition) << '/'
              << static_cast<unsigned>(r.predicate_condition)
              << " interval=" << r.first << ',' << r.last
              << " phase=" << r.phase_index.value_or(3)
              << " side=" << r.side.value_or(2)
              << " edge=" << r.edge.value_or(4)
              << " bound=" << r.limiting_bound.lower << ','
              << r.limiting_bound.upper << ':' << r.limiting_bound.supported
              << " original=" << static_cast<unsigned>(r.phase.condition);
    if (r.source_key)
      std::cout << " key=" << static_cast<unsigned>(r.source_key->buffer) << ','
                << r.source_key->group << ',' << r.source_key->triangle;
    std::cout << " name=" << r.source_name;
  }
  std::cout << '\n' << std::flush;
}
// Mandatory regressions follow the preserved, outcome-unknown FIRST log.
// This table records the frozen FIRST; it does not add assessments or poses.
[[gnu::noinline]] void observed_outcome(std::size_t slot, const Diagnostic& d) {
  struct Observed {
    std::size_t cells, nodes, splits, depth, calls;
    double seconds;
    std::array<std::size_t, 5> old_work;
    std::array<std::size_t, 9> new_work;
    std::array<bool, 2> joins;
    std::array<bool, 3> endpoints;
  };
  static constexpr std::array<Observed, 9> observed{{
      {64,
       127,
       2,
       9,
       125,
       36,
       {125, 212, 64, 273, 419},
       {32, 16064, 2292, 64, 512, 48, 8, 32, 1024},
       {true, true},
       {true, true, true}},
      {11,
       21,
       0,
       5,
       21,
       12,
       {21, 32, 11, 43, 66},
       {32, 2761, 392, 11, 88, 48, 0, 17, 176},
       {false, false},
       {true, true, false}},
      {47,
       93,
       0,
       7,
       93,
       12,
       {93, 163, 47, 207, 317},
       {32, 11797, 1688, 47, 376, 0, 0, 4, 752},
       {false, false},
       {false, true, false}},
      {6,
       11,
       0,
       3,
       11,
       12,
       {11, 17, 6, 23, 36},
       {32, 1506, 212, 6, 48, 0, 8, 11, 96},
       {false, false},
       {false, false, true}},
      {1,
       1,
       0,
       0,
       1,
       0,
       {1, 2, 1, 3, 6},
       {32, 251, 32, 1, 8, 8, 0, 2, 16},
       {false, false},
       {true, false, false}},
      {1,
       1,
       0,
       0,
       1,
       0,
       {1, 2, 1, 3, 6},
       {32, 251, 32, 1, 8, 4, 0, 5, 16},
       {false, false},
       {false, true, false}},
      {1,
       1,
       0,
       0,
       1,
       0,
       {1, 2, 1, 3, 6},
       {32, 251, 32, 1, 8, 0, 0, 0, 16},
       {false, false},
       {false, false, false}},
      {1,
       1,
       0,
       0,
       1,
       0,
       {1, 2, 1, 3, 6},
       {32, 251, 32, 1, 8, 0, 8, 11, 16},
       {false, false},
       {false, false, true}},
      {15,
       29,
       1,
       6,
       28,
       12,
       {28, 45, 15, 59, 99},
       {32, 3765, 532, 15, 120, 4, 0, 9, 240},
       {true, false},
       {false, true, false}},
  }};
  static constexpr std::array<std::size_t, 14> indices{0, 0, 1, 1, 2, 2, 3,
                                                       3, 4, 5, 6, 7, 8, 8};
  const auto& expected = observed[indices[slot - 1]];
  check(d.state == State::accepted && d.complete && d.source_enrolled &&
            d.arithmetic_supported && d.kinematic_complete &&
            d.nominal_equilibrium && d.star_support && d.port_zero_geometry &&
            !d.first_refusal && d.stop_condition == Condition::none,
        "Observed FIRST14 complete nominal support and zero-force geometry");
  check(d.cells.size() == expected.cells &&
            d.examined_nodes == expected.nodes &&
            d.mandatory_splits == expected.splits &&
            d.maximum_depth == expected.depth &&
            d.work.phase_calls == expected.calls &&
            d.reporting_elapsed_seconds == expected.seconds,
        "Observed shared cover, phase calls and physical clock retained");
  check(std::array{d.work.phase.graphs, d.work.phase.legs, d.work.phase.bodies,
                   d.work.phase.sectors,
                   d.work.phase.timing} == expected.old_work &&
            new_work(d.work) == expected.new_work,
        "Observed old and newly charged work retained fieldwise");
  check(d.qualified_joins == expected.joins &&
            d.endpoint_scope == expected.endpoints &&
            d.endpoint_earned == expected.endpoints &&
            mask_count(d.source_evaluated) == 32 &&
            d.output_capacity_bytes == 9104352,
        "Observed earned joins, endpoint scope, source mask and owned output");
}
[[gnu::noinline]] void first_one(const Provider& p, std::size_t slot,
                                 Summary& summary) {
  charge(totals.consumers, 96);
  const auto r = requests[slot - 1];
  auto result =
      assess_origin_boarding_route_intermediate_unloaded01(p, r[0], r[1]);
  if (!result) {
    std::cout << "FIRST_UNLOADED01 A" << slot << " API_ERROR=" << result.error()
              << '\n'
              << std::flush;
    check(false, "Observed FIRST14 must retain an accepted API result");
    return;
  }
  print(slot, *result);
  observed_outcome(slot, *result);
  accounting(*result, Limits{});
  if (slot <= 2) summarize(*result, summary.whole[slot - 1]);
  if (slot == 9) summarize(*result, summary.whole[0], true);
  if (slot == 12) summarize(*result, summary.whole[1], true);
  if (slot <= 2) first_oracles(*result);
}
[[gnu::noinline]] void create_source(const NativeCraftBinding& binding,
                                     const OriginBoardingBootSupport& boots,
                                     std::optional<Provider>& p) {
  charge(totals.creators, 1);
  auto result = make_origin_boarding_intermediate_pause_support(binding, boots);
  std::cout << "FIRST_UNLOADED01_SOURCE admitted=" << result.has_value();
  if (!result)
    std::cout << " error=" << result.error();
  else if (const auto* e = result->summary())
    std::cout << " complete=" << e->complete
              << " bytes=" << e->actual_source_bytes
              << " condition=" << static_cast<unsigned>(e->condition)
              << " work=" << e->work.base_guards << ',' << e->work.metadata_rows
              << ',' << e->work.face_reads << ',' << e->work.quad_records;
  std::cout << '\n' << std::flush;
  check(result.has_value(), "Observed genuine source admission retained");
  if (result) {
    const auto* e = result->summary();
    check(e && e->complete && e->actual_source_bytes == 4096 &&
              e->condition == BoardingIntermediatePauseCondition::none &&
              e->work.base_guards == 21 && e->work.metadata_rows == 1 &&
              e->work.face_reads == 2 && e->work.quad_records == 2,
          "Observed genuine source identity work and single arena retained");
    check(Access::valid(*result),
          "Only genuine public unchanged creator issues source");
    p.emplace(std::move(*result));
  }
}
[[gnu::noinline]] void immutable_controls() {
  Hash hash;
  for (std::size_t phase = 0; phase < 3; ++phase) {
    const auto selected =
        detail::boarding_route_intermediate_unloaded01_controls(phase);
    const auto old = detail::boarding_route_intermediate_step02_controls(phase);
    check(selected.has_value() && old.has_value(),
          "Original phase inspector exists");
    if (!selected || !old) continue;
    check(selected->seconds_per_parameter == 12 &&
              selected->port_reaction_fraction == std::array<double, 2>{0, 0},
          "Exact T12 zero-force immutable packet");
    hash.add(phase);
    hash.number(selected->seconds_per_parameter);
    for (auto x : selected->port_reaction_fraction)
      hash.number(x);
    for (std::size_t end = 0; end < 2; ++end) {
      check(selected->root_yaw_half[end] == old->root_yaw_half[end] &&
                selected->torso_lean_half[end] == old->torso_lean_half[end],
            "Original root yaw/torso preserved");
      hash.number(selected->root_yaw_half[end]);
      hash.number(selected->torso_lean_half[end]);
      for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto& a = selected->root[end].coordinates[axis];
        const auto& b = old->root[end].coordinates[axis];
        check(a.count == b.count && a.terms == b.terms,
              "Original root expression order unchanged");
        hash.add(a.count);
        for (auto x : a.terms)
          hash.number(x);
      }
      for (std::size_t side = 0; side < 2; ++side) {
        const auto& f = selected->feet[side];
        check(f.yaw_half == old->feet[side].yaw_half &&
                  f.swing_height_metres == old->feet[side].swing_height_metres,
              "Original sole yaw/hump unchanged");
        hash.number(f.yaw_half[end]);
        hash.number(f.swing_height_metres);
        for (std::size_t axis = 0; axis < 3; ++axis) {
          const auto& a = f.sole[end].coordinates[axis];
          const auto& b = old->feet[side].sole[end].coordinates[axis];
          check(a.count == b.count && a.terms == b.terms,
                "Original sole source terms unchanged");
          hash.add(a.count);
          for (auto x : a.terms)
            hash.number(x);
        }
      }
    }
  }
  recipe_hash = hash.value;
  check(!detail::boarding_route_intermediate_unloaded01_controls(3),
        "No later phase enters selected prefix");
  check(detail::boarding_route_intermediate_unloaded01_clock(0) == 0 &&
            detail::boarding_route_intermediate_unloaded01_clock(.125) == 12 &&
            detail::boarding_route_intermediate_unloaded01_clock(.25) == 24 &&
            detail::boarding_route_intermediate_unloaded01_clock(.5) == 36,
        "Immutable clock12/12/12 with piecewise slope");
  check(detail::boarding_route_intermediate_unloaded01_phase(.125, .125) == 0 &&
            detail::boarding_route_intermediate_unloaded01_phase(.125, .25) ==
                1 &&
            detail::boarding_route_intermediate_unloaded01_phase(.25, .25) ==
                1 &&
            detail::boarding_route_intermediate_unloaded01_phase(.25, .5) == 2,
        "Point joins precede; positive-width starts follow");
}
[[gnu::noinline]] void ordinary_roster(const Provider& p);
using Used = std::array<std::uint32_t, 18>;
auto exact_limits(const Used& used) -> Limits {
  Limits l;
  for (std::size_t i = 0; i < 18; ++i)
    set_limit(l, i, used[i]);
  return l;
}
constexpr std::array<Condition, 9> capacity_conditions{
    Condition::source_guard_capacity,
    Condition::projection_capacity,
    Condition::definition_capacity,
    Condition::pressure_capacity,
    Condition::edge_capacity,
    Condition::upper_edge_capacity,
    Condition::source_coordinate_capacity,
    Condition::endpoint_operation_capacity,
    Condition::event_guard_capacity};
void lower_refusal(const Diagnostic& d, std::size_t field) {
  if (field < 4) {
    const std::array c{Condition::depth_capacity, Condition::node_capacity,
                       Condition::leaf_capacity, Condition::output_capacity};
    check(d.stop_condition == c[field],
          "Reached navigation/output reduction retains exact terminal cause");
  } else if (field < 9) {
    const std::array c{BoardingRouteFootPhaseCondition::graph_capacity,
                       BoardingRouteFootPhaseCondition::leg_capacity,
                       BoardingRouteFootPhaseCondition::body_capacity,
                       BoardingRouteFootPhaseCondition::sector_capacity,
                       BoardingRouteFootPhaseCondition::timing_capacity};
    check(d.first_refusal && d.first_refusal->phase.condition == c[field - 4],
          "Original compiler stage capacity/cause preserved");
  } else
    check(d.stop_condition == capacity_conditions[field - 9],
          "Later reached capacity remains terminal despite first ordinary "
          "finding");
}
[[gnu::noinline]] void assessment_one(
    const Provider& p, double first, double last, Limits l,
    const std::uint64_t* hash = nullptr,
    std::optional<std::size_t> lowered = std::nullopt, bool invalid = false,
    bool empty = false) {
  charge(totals.consumers, 96);
  auto result =
      detail::boarding_route_intermediate_unloaded01_bounded(p, first, last, l);
  if (invalid) {
    check(!result,
          "Invalid range/raised/output/unsafe preflight rejects before report");
    return;
  }
  if (!result) {
    check(false, "Registered valid control must return an honest diagnostic, "
                 "error follows");
    std::cerr << "CONTROL_ERROR " << result.error() << '\n';
    return;
  }
  accounting(*result, l);
  if (hash)
    check(semantic_hash(*result) == *hash,
          "Exact capacities/lifetime retain canonical fieldwise evidence and "
          "physical jets");
  if (lowered) lower_refusal(*result, *lowered);
  if (empty)
    check(!result->source_enrolled &&
              result->stop_condition == Condition::invalid_binding &&
              result->work.phase_calls == 0 &&
              result->work.projection_guards == 0 &&
              result->work.pressure_candidates == 0 && result->cells.empty(),
          "Publicly emptied handle refuses before graph/current geometry; "
          "actual identity count retained");
}
auto anchor_range(std::size_t field) -> std::array<double, 2> {
  return field == 14                  ? std::array<double, 2>{0, 0}
         : field == 15 || field == 16 ? std::array<double, 2>{.5, .5}
                                      : std::array<double, 2>{0, .5};
}
auto anchor_used(const Summary& s, std::size_t field) -> const Used* {
  if (field == 14)
    return s.whole[0].endpoint_exists ? &s.whole[0].endpoint_used : nullptr;
  if (field == 15 || field == 16)
    return s.whole[1].endpoint_exists ? &s.whole[1].endpoint_used : nullptr;
  return s.whole[0].exists ? &s.whole[0].used : nullptr;
}
[[gnu::noinline]] void ordinary_roster(const Provider& p) {
  for (std::size_t slot = 15; slot <= 20; ++slot) {
    const auto r = requests[slot - 1];
    assessment_one(p, r[0], r[1], Limits{});
  }
}
[[gnu::noinline]] void lowered_roster(const Provider& p, const Summary& s) {
  const Limits defaults;
  for (std::size_t i = 0; i < 2; ++i)
    if (s.whole[i].exists)
      assessment_one(p, i == 0 ? 0 : .5, i == 0 ? .5 : 0,
                     exact_limits(s.whole[i].used), &s.whole[i].hash);
  // A23--40: isolated zero, with fixed endpoint anchors for geometry stages.
  for (std::size_t i = 0; i < 18; ++i) {
    auto l = defaults;
    set_limit(l, i, 0);
    const auto r = anchor_range(i);
    const auto* used = anchor_used(s, i);
    assessment_one(p, r[0], r[1], l, nullptr,
                   used && (*used)[i] > 0 && i != 3 ? std::optional{i}
                                                    : std::nullopt,
                   i == 3);
  }
  // A41--58: every individual ceiling, never an all-raised masking fixture.
  for (std::size_t i = 0; i < 18; ++i) {
    auto l = defaults;
    set_limit(l, i, get_limit(defaults, i) + 1);
    assessment_one(p, 0, .5, l, nullptr, std::nullopt, true);
  }
  // A59--76: only genuinely reached, nonduplicate fixed anchors; no substitute.
  for (std::size_t i = 0; i < 18; ++i) {
    const auto* used = anchor_used(s, i);
    if (!used || (*used)[i] <= 1) continue;
    auto l = defaults;
    set_limit(l, i, (*used)[i] - 1);
    const auto r = anchor_range(i);
    const bool preflight =
        i == 3 &&
        get_limit(l, 3) < 2 * sizeof(std::expected<Diagnostic, std::string>);
    assessment_one(p, r[0], r[1], l, nullptr,
                   preflight ? std::nullopt : std::optional{i}, preflight);
  }
}
[[gnu::noinline]] void remaining_roster(Provider& p, const Summary& s) {
  for (std::size_t mode = 0; mode < 6; ++mode) {
    SavedEnvironment saved;
    Environment changed(mode);
    if (changed.available)
      assessment_one(p, 0, .5, Limits{}, nullptr, std::nullopt, true);
  }
  const auto inf = std::numeric_limits<double>::infinity();
  for (const auto r : std::array<std::array<double, 2>, 4>{
           {{std::numeric_limits<double>::quiet_NaN(), .5},
            {0, inf},
            {std::nextafter(0., -inf), .5},
            {0, std::nextafter(.5, inf)}}})
    assessment_one(p, r[0], r[1], Limits{}, nullptr, std::nullopt, true);
  {
    auto alias = p;
    auto held =
        std::move(alias); // Publicly emptied alias is the contract under test.
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Empty handle contract.
    assessment_one(alias, 0, .5, Limits{}, nullptr, std::nullopt, false, true);
    check(Access::valid(held), "Moved-to alias retains sole genuine arena");
  }
  {
    auto held = std::move(p);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Empty handle contract.
    assessment_one(p, 0, .5, Limits{}, nullptr, std::nullopt, false, true);
    p = std::move(held);
  }
  {
    auto copy = p;
    p = std::move(copy);
    assessment_one(p, 0, .5, Limits{},
                   s.whole[0].exists ? &s.whole[0].hash : nullptr);
    check(Access::valid(p),
          "Copied same-owner source survives releasing original handle");
  }
  {
    auto held = std::move(p);
    assessment_one(held, 0, .5, Limits{},
                   s.whole[0].exists ? &s.whole[0].hash : nullptr);
    p = std::move(held);
  }
  {
    Limits l;
    l.phase.nodes = 0;
    l.phase.graphs = 0;
    assessment_one(p, 0, .5, l, nullptr,
                   s.whole[0].exists && s.whole[0].used[1] > 0
                       ? std::optional<std::size_t>{1}
                       : std::nullopt);
  }
  {
    Limits l;
    l.phase.bodies = 0;
    l.phase.sectors = 0;
    assessment_one(p, 0, .5, l, nullptr,
                   s.whole[0].exists && s.whole[0].used[7] > 0
                       ? std::optional<std::size_t>{7}
                       : std::nullopt);
  }
  {
    Limits l;
    l.upper_geometry_edges = 0;
    l.endpoint_operations = 0;
    assessment_one(p, 0, 0, l, nullptr,
                   s.whole[0].endpoint_exists &&
                           s.whole[0].endpoint_used[16] > 0
                       ? std::optional<std::size_t>{16}
                       : std::nullopt);
  }
  {
    Limits l;
    l.intermediate_coordinates = 0;
    l.endpoint_operations = 0;
    assessment_one(p, .5, .5, l, nullptr,
                   s.whole[1].endpoint_exists &&
                           s.whole[1].endpoint_used[16] > 0
                       ? std::optional<std::size_t>{16}
                       : std::nullopt);
  }
  if (s.whole[0].endpoint_exists)
    assessment_one(p, 0, 0, exact_limits(s.whole[0].endpoint_used),
                   &s.whole[0].endpoint_hash);
  if (s.whole[1].endpoint_exists)
    assessment_one(p, .5, .5, exact_limits(s.whole[1].endpoint_used),
                   &s.whole[1].endpoint_hash);
}
struct Fixture {
  GeometryInput input;
  GeometryLimits limits;
};
[[gnu::noinline]] auto fixture(std::size_t slot) -> Fixture {
  Fixture f;
  auto& i = f.input;
  i.mode = slot <= 8 ? GeometryMode::upper_point
                     : GeometryMode::intermediate_overlap;
  i.center_xz = {scalar(0), scalar(0)};
  const std::array<RigidVector3, 4> t{
      {{.5, 0, -1}, {-.5, 0, -1}, {-1, 0, 1}, {1, 0, 1}}},
      r{{{1, 0, -1}, {-1, 0, -1}, {-1, 0, 1}, {1, 0, 1}}};
  i.source_vertices =
      slot <= 8 || slot == 13 || (slot >= 21 && slot <= 23) ? t : r;
  const auto inf = std::numeric_limits<double>::infinity();
  switch (slot) {
    case 2: i.center_xz = {scalar(.75), scalar(-.75)}; break;
    case 3: i.center_xz = {scalar(0), scalar(-1)}; break;
    case 4: i.center_xz[0] = {1, 0, true}; break;
    case 5:
      i.source_vertices[2].x = std::numeric_limits<double>::quiet_NaN();
      break;
    case 6: i.center_xz[1] = {0, 0, false}; break;
    case 7: f.limits.upper_edges = 0; break;
    case 8: f.limits.upper_edges = 3; break;
    case 9:
      f.limits.source_coordinates = 8;
      f.limits.operations = 8;
      break;
    case 10:
      i.source_vertices = {{{.03125, 0, -.0625},
                            {-.03125, 0, -.0625},
                            {-.03125, 0, .0625},
                            {.03125, 0, .0625}}};
      break;
    case 11:
      i.source_vertices = {{{2, 0, -1}, {1, 0, -1}, {1, 0, 1}, {2, 0, 1}}};
      break;
    case 12:
      i.source_vertices = {{{1, 0, -1}, {.06, 0, -1}, {.06, 0, 1}, {1, 0, 1}}};
      break;
    case 14: f.limits.source_coordinates = 7; break;
    case 15: f.limits.operations = 7; break;
    case 16: f.limits.operations = 13; break;
    case 17:
      i.source_plane = 1;
      i.sole_plane = std::nextafter(1., inf);
      for (auto& v : i.source_vertices)
        v.y = 1;
      break;
    case 18:
      i.source_plane = i.sole_plane = std::nextafter(8., inf);
      for (auto& v : i.source_vertices)
        v.y = i.source_plane;
      break;
    case 19:
      i.mode = GeometryMode::upper_departure;
      i.center_xz = {scalar(0), scalar(-2)};
      break;
    case 20:
      i.mode = GeometryMode::upper_departure;
      i.center_xz = {
          scalar(0),
          {std::nextafter(-1.14, -inf), std::nextafter(-1.14, inf), true}};
      break;
    case 21:
      i.mode = GeometryMode::upper_cell;
      i.center_xz = {Scalar{-.125, .125, true}, Scalar{-.125, .125, true}};
      break;
    case 22:
      i.mode = GeometryMode::upper_cell;
      i.center_xz = {scalar(0), scalar(-2)};
      break;
    case 23:
      i.mode = GeometryMode::upper_cell;
      i.center_xz = {scalar(0), {-2, 0, true}};
      break;
    default: break;
  }
  return f;
}
[[gnu::noinline]] void raw_one(std::size_t slot, const Fixture& f) {
  charge(totals.math, 24);
  const auto d = detail::boarding_route_intermediate_unloaded01_geometry_math(
      f.input, f.limits);
  using Math = decltype(d);
  check(!Math::source_qualified && !Math::body_qualified &&
            !Math::contact_qualified && !Math::load_qualified &&
            !Math::support_qualified && !Math::self_qualified &&
            !Math::material_qualified && !Math::world_qualified &&
            !Math::route_qualified && !Math::actor_qualified &&
            !Math::seat_qualified && !Math::save_qualified &&
            !Math::dynamics_qualified,
        "Literal raw geometry never creates source/current-cell authority");
  aggregate(14, d.work.upper_edges);
  aggregate(15, d.work.source_coordinates);
  aggregate(16, d.work.operations);
  aggregate(17, d.work.validation_guards);
  check(d.work.upper_edges <= f.limits.upper_edges &&
            d.work.source_coordinates <= f.limits.source_coordinates &&
            d.work.operations <= f.limits.operations &&
            d.work.validation_guards <= 3,
        "Single raw call has finite operation caps");
  check(mask_count(d.side_evaluated) == d.work.upper_edges &&
            mask_count(d.source_coordinate_evaluated) ==
                d.work.source_coordinates &&
            mask_count(d.operation_evaluated) == d.work.operations &&
            mask_count(d.input_validation_evaluated) ==
                d.work.validation_guards,
        "Attempted raw masks retain charged exact prefixes");
  if (d.input_validated) {
    const auto& i = f.input;
    for (std::size_t e = 0; e < 4; ++e)
      if (d.side_evaluated[e])
        for (double x : {i.center_xz[0].lower, i.center_xz[0].upper})
          for (double z : {i.center_xz[1].lower, i.center_xz[1].upper})
            check(enclosed(d.signed_sides[e],
                           side_value(plain(i.source_vertices[e]),
                                      plain(i.source_vertices[(e + 1) % 4]),
                                      {x, 0, z})),
                  "Independent corner cross-products enclosed by side-only "
                  "kernel");
    if (i.mode == GeometryMode::intermediate_overlap &&
        d.work.source_coordinates == 8 && d.work.operations == 8) {
      for (std::size_t a = 0; a < 2; ++a) {
        const auto axis = a == 0 ? 0U : 2U;
        long double lo = component(i.source_vertices[0], axis), hi = lo;
        for (auto v : i.source_vertices) {
          lo = std::min(lo, static_cast<long double>(component(v, axis)));
          hi = std::max(hi, static_cast<long double>(component(v, axis)));
        }
        check(enclosed(d.source_extrema[a][0], lo) &&
                  enclosed(d.source_extrema[a][1], hi),
              "Every source corner contributes authentic raw extrema");
        if (d.work.operations >= 8) {
          const auto half = static_cast<long double>(a == 0 ? .06 : .14);
          for (double center : {i.center_xz[a].lower, i.center_xz[a].upper}) {
            const auto lower = std::max(static_cast<long double>(center) - half,
                                        lo),
                       upper = std::min(static_cast<long double>(center) + half,
                                        hi);
            check(enclosed(d.intersection[a][0], lower) &&
                      enclosed(d.intersection[a][1], upper),
                  "Independent fullsole/source intersection extrema enclosed");
          }
        }
      }
    }
  }
  if (slot == 1 || slot == 9 || slot == 10 || slot == 19 || slot == 21 ||
      slot == 22 || slot == 23)
    check(d.complete && d.arithmetic_supported && d.input_validated,
          "Analytically selected literal geometry stage completes without "
          "authority");
  if (slot == 2 || slot == 3 || slot == 11 || slot == 12 || slot == 20)
    check(!d.complete, "Outside/zero-area/touch literal cannot certify strict "
                       "contact/departure");
  if (slot == 4 || slot == 5 || slot == 6 || slot == 13 || slot == 17 ||
      slot == 18)
    check(!d.input_validated && !d.complete && d.work.upper_edges == 0 &&
              d.work.operations == 0 && d.work.source_coordinates == 0,
          "Malformed/domain/plane/rectangle fixture refuses before geometry");
  if (slot == 7)
    check(d.condition == Condition::upper_edge_capacity &&
              d.work.upper_edges == 0,
          "Zero upper-side cap means no signed-side calculation");
  if (slot == 8)
    check(d.condition == Condition::upper_edge_capacity &&
              d.work.upper_edges == 3 && !d.side_evaluated[3],
          "Fourth source-side remains NOT_RUN at cap3");
  if (slot == 9)
    check(d.work.source_coordinates == 8 && d.work.operations == 8 &&
              d.positive_overlap[0] && d.positive_overlap[1],
          "Exact full raw operation budgets reach both positive overlap axes");
  if (slot == 14)
    check(d.condition == Condition::source_coordinate_capacity &&
              d.work.source_coordinates == 7 &&
              !d.source_coordinate_evaluated[7],
          "Last duplicated-max corner still mandatory and NOT_RUN at cap7");
  if (slot == 15)
    check(d.condition == Condition::endpoint_operation_capacity &&
              d.work.operations == 7 && !d.operation_evaluated[7] &&
              std::ranges::all_of(d.source_extrema,
                                  [](const auto& pair) {
                                    return !pair[0].supported &&
                                           !pair[1].supported;
                                  }) &&
              std::ranges::all_of(d.sole_extrema,
                                  [](const auto& pair) {
                                    return !pair[0].supported &&
                                           !pair[1].supported;
                                  }),
          "Last upper intersection NOT_RUN; saved extrema unpublished at cap7");
  if (slot == 16)
    check(d.condition == Condition::invalid_limits &&
              d.work.validation_guards == 0 && d.work.operations == 0,
          "Isolated raised raw operation limit refuses prework");
  if (slot == 19)
    check(d.work.operations == 2 && finite(d.departure_gap) &&
              d.departure_gap.lower > 0,
          "Registered ADD plus SUB both charged for strict departure gap");
  if (slot == 21)
    check(d.classification == BoardingRouteIntermediateUnloaded01UpperClass::
                                  strict_center_overlap,
          "Whole raw center enclosure strictly inside actual polygon");
  if (slot == 22)
    check(
        d.classification == BoardingRouteIntermediateUnloaded01UpperClass::
                                strict_Z_disjoint &&
            d.work.operations == 2,
        "Fullsole ADD and signed gap SUB earn departure after negative sides");
  if (slot == 23)
    check(d.classification == BoardingRouteIntermediateUnloaded01UpperClass::
                                  mixed_possible_transition &&
              d.work.operations == 2,
          "Mixed whole-cell relation is classification, never a support "
          "failure or permission");
  if (slot == 24)
    check(!d.complete && !d.arithmetic_supported && d.work.upper_edges == 0 &&
              d.work.operations == 0 && d.work.source_coordinates == 0 &&
              d.condition == Condition::unsupported_arithmetic,
          "Unsafe original environment refuses before raw geometry");
}
[[gnu::noinline]] void geometry_roster() {
  for (std::size_t slot = 1; slot <= 24; ++slot) {
    SavedEnvironment saved;
    const auto f = fixture(slot);
    if (slot == 24) {
      Environment changed(0);
      if (changed.available) raw_one(slot, f);
    } else
      raw_one(slot, f);
  }
}
[[gnu::noinline]] void final_accounting() {
  constexpr std::array<std::uint64_t, 18> maximum{
      196512,   393024,  196512, 589536,  1179072, 3072,
      49324512, 6288384, 196512, 1572096, 1572096, 1572096,
      2358144,  3144192, 192,    192,     288,     72};
  for (std::size_t i = 0; i < maximum.size(); ++i)
    check(totals.work[i] <= maximum[i],
          "Registered roster bounds every aggregate operation");
  check(totals.creators == 1 && totals.consumers <= 96 && totals.math <= 24 &&
            totals.math >= 23 && totals.oracles <= 8 &&
            totals.phase_calls <= 196512,
        "One creator,96 slots,24 raw slots andeight FIRST-only poses; skipped "
        "controls never replaced");
  std::cout << "VALIDATION creators=" << totals.creators
            << " consumers=" << totals.consumers
            << " calls=" << totals.phase_calls << " raw=" << totals.math
            << " poses=" << totals.oracles << " checks=" << checks
            << " failures=" << failures << " work=";
  for (auto n : totals.work)
    std::cout << n << ',';
  std::cout << '\n' << std::flush;
}
} // namespace
int main() {
  std::size_t bytes{};
  StreamCounter out(std::cout.rdbuf(), bytes), err(std::cerr.rdbuf(), bytes);
  auto* saved_out = std::cout.rdbuf(&out);
  auto* saved_err = std::cerr.rdbuf(&err);
  try {
    std::cout << std::setprecision(17);
    const auto binding = make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{});
    if (!binding) throw std::runtime_error(binding.error());
    const auto boots = make_origin_boarding_boot_support(*binding);
    if (!boots) throw std::runtime_error(boots.error());
    std::optional<Provider> provider;
    Summary summary;
    immutable_controls();
    create_source(*binding, *boots, provider);
    if (provider) {
      for (std::size_t slot = 1; slot <= 14; ++slot)
        first_one(*provider, slot, summary);
      if (summary.whole[0].exists && summary.whole[1].exists)
        check(summary.whole[0].hash == summary.whole[1].hash,
              "Whole reverse preserves canonical geometry, physical jets and "
              "source cause");
      ordinary_roster(*provider);
      lowered_roster(*provider, summary);
      remaining_roster(*provider, summary);
    } else
      std::cout << "FIRST_UNLOADED01 NOT_RUN source_refused\n" << std::flush;
    geometry_roster();
    final_accounting();
    check(!out.truncated && !err.truncated && std::cout.good() &&
              std::cerr.good() && bytes <= 16384,
          "Shared stdout/stderr stream remains bounded and untruncated");
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout.rdbuf(saved_out);
  std::cerr.rdbuf(saved_err);
  return failures == 0 ? 0 : 1;
}
