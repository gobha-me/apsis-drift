#include "apsis_drift/origin_boarding_intermediate_endpoint02.hpp"
#include "origin_boarding_intermediate_endpoint02_internal.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <initializer_list>
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
#include <variant>
#if defined(__SSE2__) && defined(__x86_64__)
#include <xmmintrin.h>
#endif
namespace {
using namespace apsis_drift;
using Provider = OriginBoardingIntermediatePauseSupport;
using Diagnostic = BoardingIntermediateEndpoint02Diagnostic;
using Cell = BoardingIntermediateEndpoint02Cell;
using Expected = BoardingIntermediateEndpoint02Expected;
using Candidate = BoardingIntermediateEndpoint02Candidate;
using Condition = BoardingIntermediateEndpoint02Condition;
using State = BoardingIntermediateEndpoint02State;
using Stage = BoardingIntermediateEndpoint02SelfStage;
using SelfCertificate = BoardingIntermediateEndpoint02Certificate;
using PhaseCell = BoardingRouteFootPhaseCell;
using Request = BoardingRouteFootPhaseRequest;
using Constant = BoardingRoutePhaseConstant;
using ConstantPoint = BoardingRoutePhasePointConstant;
using Scalar = BoardingFootSiteScalarBounds;
using Limits = detail::BoardingIntermediateEndpoint02Limits;
using Access = detail::BoardingIntermediatePauseSupportAccess;
constexpr auto candidate = Candidate::root_y_reach_slice;
using SliceCondition = BoardingIntermediateEndpoint02SliceCondition;
using Context = detail::BoardingIntermediateEndpoint02AdmissionContext;
using Token = detail::BoardingIntermediateEndpoint02CurrentToken;
using Key = detail::BoardingIntermediateEndpoint02ProgramKey;
static_assert(!std::is_constructible_v<Key, double, Candidate>);
static_assert(!std::is_constructible_v<Context, const Diagnostic&,
                                       const Request&, const Key&>);
static_assert(!std::is_copy_constructible_v<Context> &&
              !std::is_move_constructible_v<Context>);
static_assert(!std::is_constructible_v<Token, const Context&, const Diagnostic&,
                                       const Cell&, const Request&>);
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
  std::array<std::uint32_t, 26> work{};
  std::uint32_t creators{}, consumers{}, skips{}, next_slot{1}, bodies{},
      records{};
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
    std::fegetexceptflag(&exceptions_, FE_ALL_EXCEPT);
#if defined(__SSE2__) && defined(__x86_64__)
    control_ = _mm_getcsr();
#endif
    if (mode < 3) {
      const std::array modes{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
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
    std::fesetexceptflag(&exceptions_, FE_ALL_EXCEPT);
#if defined(__SSE2__) && defined(__x86_64__)
    _mm_setcsr(control_);
#endif
  }
  Environment(const Environment&) = delete;
  auto operator=(const Environment&) -> Environment& = delete;
  bool available{};

 private:
  int rounding_{};
  fexcept_t exceptions_{};
#if defined(__SSE2__) && defined(__x86_64__)
  unsigned control_{};
#endif
};
class ExceptionState {
 public:
  ExceptionState() {
    std::fegetexceptflag(&exceptions_, FE_ALL_EXCEPT);
#if defined(__SSE2__) && defined(__x86_64__)
    control_ = _mm_getcsr();
#endif
  }
  ~ExceptionState() {
    std::fesetexceptflag(&exceptions_, FE_ALL_EXCEPT);
#if defined(__SSE2__) && defined(__x86_64__)
    _mm_setcsr(control_);
#endif
  }
  ExceptionState(const ExceptionState&) = delete;
  auto operator=(const ExceptionState&) -> ExceptionState& = delete;

 private:
  fexcept_t exceptions_{};
#if defined(__SSE2__) && defined(__x86_64__)
  unsigned control_{};
#endif
};
template <class T> auto bit_count(T value) -> std::size_t {
  return static_cast<std::size_t>(std::popcount(value));
}
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
struct RegionRow {
  std::size_t first, second;
  BoardingSelfJunction junction;
  double limit;
};
constexpr std::array<RegionRow, 14> original_regions{
    {{0, 1, BoardingSelfJunction::waist, kBoardingSelfWaistLimitMetres},
     {0, 3, BoardingSelfJunction::hip, kBoardingSelfHipLengthMetres},
     {0, 9, BoardingSelfJunction::hip, kBoardingSelfHipLengthMetres},
     {1, 2, BoardingSelfJunction::neck, .2695},
     {1, 6, BoardingSelfJunction::shoulder, kBoardingSelfShoulderRadiusMetres},
     {1, 12, BoardingSelfJunction::shoulder, kBoardingSelfShoulderRadiusMetres},
     {3, 4, BoardingSelfJunction::knee, kBoardingSelfKneeRadiusMetres},
     {4, 5, BoardingSelfJunction::ankle, kBoardingSelfAnkleLengthMetres},
     {6, 7, BoardingSelfJunction::elbow, kBoardingSelfElbowRadiusMetres},
     {7, 8, BoardingSelfJunction::wrist, kBoardingSelfWristLengthMetres},
     {9, 10, BoardingSelfJunction::knee, kBoardingSelfKneeRadiusMetres},
     {10, 11, BoardingSelfJunction::ankle, kBoardingSelfAnkleLengthMetres},
     {12, 13, BoardingSelfJunction::elbow, kBoardingSelfElbowRadiusMetres},
     {13, 14, BoardingSelfJunction::wrist, kBoardingSelfWristLengthMetres}}};
auto region_index(std::size_t a, std::size_t b) -> std::size_t {
  for (std::size_t i = 0; i < original_regions.size(); ++i)
    if (original_regions[i].first == a && original_regions[i].second == b)
      return i;
  return original_regions.size();
}
auto pair_index(std::size_t a, std::size_t b) -> std::size_t {
  return a * (29 - a) / 2 + b - a - 1;
}
// These original24-byte intervals only recover the concrete reported axis.
// Certification below uses independent long-double complete-solid support.
struct RecoveryInterval {
  double low{}, high{};
  bool supported{true};
};
static_assert(sizeof(RecoveryInterval) == 24);
using RecoveryPoint = std::array<RecoveryInterval, 3>;
auto ri(double a, double b) -> RecoveryInterval {
  return {a, b, std::isfinite(a) && std::isfinite(b) && a <= b};
}
auto ri_add(RecoveryInterval a, RecoveryInterval b) -> RecoveryInterval {
  if (!a.supported || !b.supported) return {0, 0, false};
  if (a.low == 0 && a.high == 0) return b;
  if (b.low == 0 && b.high == 0) return a;
  return ri(
      std::nextafter(a.low + b.low, -std::numeric_limits<double>::infinity()),
      std::nextafter(a.high + b.high, std::numeric_limits<double>::infinity()));
}
auto ri_neg(RecoveryInterval a) -> RecoveryInterval {
  return {-a.high, -a.low, a.supported};
}
auto ri_sub(RecoveryInterval a, RecoveryInterval b) -> RecoveryInterval {
  if (a.supported && b.supported && a.low == a.high && b.low == b.high &&
      a.low == b.low)
    return {};
  return ri_add(a, ri_neg(b));
}
auto ri_mul(RecoveryInterval a, RecoveryInterval b) -> RecoveryInterval {
  if (!a.supported || !b.supported) return {0, 0, false};
  if ((a.low == 0 && a.high == 0) || (b.low == 0 && b.high == 0)) return {};
  if (a.low == 1 && a.high == 1) return b;
  if (b.low == 1 && b.high == 1) return a;
  const std::array p{a.low * b.low, a.low * b.high, a.high * b.low,
                     a.high * b.high};
  return ri(std::nextafter(*std::min_element(p.begin(), p.end()),
                           -std::numeric_limits<double>::infinity()),
            std::nextafter(*std::max_element(p.begin(), p.end()),
                           std::numeric_limits<double>::infinity()));
}
auto recovery_point(const BoardingPlantedLegPointBounds& b) -> RecoveryPoint {
  RecoveryPoint p;
  for (std::size_t i = 0; i < 3; ++i)
    p[i] = ri(component(b.lower, i), component(b.upper, i));
  return p;
}
using Direction = std::array<double, 3>;
auto reported(const RecoveryPoint& p) -> Direction {
  Direction n;
  for (std::size_t i = 0; i < 3; ++i) {
    check(p[i].supported,
          "Reported direction uses only supported interval components");
    n[i] = p[i].low + (p[i].high - p[i].low) * .5;
  }
  return n;
}
auto direction_difference(Direction a, Direction b) -> Direction {
  for (std::size_t i = 0; i < 3; ++i) {
    a[i] -= b[i];
  }
  return a;
}
struct AuditSolid {
  const BoardingRoutePhasePartBinding& binding;
  const PhaseCell& phase;
  const std::array<RecoveryPoint, 18>& relative;
  auto box() const -> const BoardingRoutePhaseBoxBinding* {
    return std::get_if<BoardingRoutePhaseBoxBinding>(&binding.reservation);
  }
  auto first() const -> const RecoveryPoint& {
    if (const auto* b = box())
      return relative[static_cast<std::size_t>(b->center)];
    return relative[static_cast<std::size_t>(
        std::get<BoardingPlantedBodyCapsuleBinding>(binding.reservation)
            .start)];
  }
  auto second() const -> const RecoveryPoint& {
    if (box()) return first();
    return relative[static_cast<std::size_t>(
        std::get<BoardingPlantedBodyCapsuleBinding>(binding.reservation).end)];
  }
  auto center() const -> RecoveryPoint {
    if (box()) return first();
    RecoveryPoint p;
    for (std::size_t i = 0; i < 3; ++i)
      p[i] = ri_mul(ri_add(first()[i], second()[i]), ri(.5, .5));
    return p;
  }
  auto column(std::size_t i) const -> RecoveryPoint {
    const auto* b = box();
    return b ? recovery_point(phase.frames[static_cast<std::size_t>(b->frame)]
                                  .columns[i]
                                  .value)
             : RecoveryPoint{};
  }
};
auto selected_direction(const AuditSolid& a, const AuditSolid& b,
                        std::uint8_t code) -> Direction {
  std::size_t ordinal = code & 15U;
  Direction n{};
  if (ordinal < 3)
    n[ordinal] = 1;
  else if (ordinal == 3)
    n = direction_difference(reported(b.center()), reported(a.center()));
  else {
    ordinal -= 4;
    bool found{};
    for (const auto* s : {&a, &b})
      if (s->box()) {
        if (ordinal < 3) {
          n = reported(s->column(ordinal));
          found = true;
          break;
        }
        ordinal -= 3;
      }
    if (!found)
      for (const auto* s : {&a, &b})
        if (!s->box()) {
          if (ordinal < 2) {
            const auto& other = s == &a ? b : a;
            n = direction_difference(
                reported(ordinal == 0 ? s->first() : s->second()),
                reported(other.center()));
            found = true;
            break;
          }
          ordinal -= 2;
        }
    check(found, "Encoded proposal belongs to original grouped axis sequence");
  }
  if (code & 16U)
    for (auto& v : n)
      v = -v;
  check(
      std::ranges::all_of(
          n, [](double v) { return std::isfinite(v) && std::abs(v) <= 64; }) &&
          std::ranges::any_of(n, [](double v) { return v != 0; }),
      "Actual selected concrete direction is finite nonzero without "
      "normalization");
  return n;
}
struct LdBounds {
  long double low{}, high{};
};
auto ld(RecoveryInterval a) -> LdBounds {
  return {a.low, a.high};
}
template <class Bound> auto ld(Bound a) -> LdBounds {
  return {a.lower, a.upper};
}
auto ld_add(LdBounds a, LdBounds b) -> LdBounds {
  return {a.low + b.low, a.high + b.high};
}
auto ld_neg(LdBounds a) -> LdBounds {
  return {-a.high, -a.low};
}
auto ld_sub(LdBounds a, LdBounds b) -> LdBounds {
  return ld_add(a, ld_neg(b));
}
auto ld_mul(LdBounds a, LdBounds b) -> LdBounds {
  const std::array p{a.low * b.low, a.low * b.high, a.high * b.low,
                     a.high * b.high};
  return {*std::min_element(p.begin(), p.end()),
          *std::max_element(p.begin(), p.end())};
}
auto ld_abs(LdBounds a) -> LdBounds {
  return {a.low <= 0 && a.high >= 0
              ? 0
              : std::min(std::abs(a.low), std::abs(a.high)),
          std::max(std::abs(a.low), std::abs(a.high))};
}
auto ld_square(LdBounds a) -> LdBounds {
  const auto b = ld_abs(a);
  return {b.low * b.low, b.high * b.high};
}
auto ld_root(LdBounds a) -> LdBounds {
  check(a.low >= 0 && a.high >= a.low, "Independent nonnegative root domain");
  return {std::sqrt(std::max(0.L, a.low)), std::sqrt(std::max(0.L, a.high))};
}
auto ld_dot(const RecoveryPoint& p, const Direction& n) -> LdBounds {
  LdBounds v{};
  for (std::size_t i = 0; i < 3; ++i) {
    v = ld_add(v, ld_mul(ld(p[i]), {n[i], n[i]}));
  }
  return v;
}
auto ld_dot(const RecoveryPoint& a, const RecoveryPoint& b) -> LdBounds {
  LdBounds v{};
  for (std::size_t i = 0; i < 3; ++i)
    v = ld_add(v, ld_mul(ld(a[i]), ld(b[i])));
  return v;
}
auto ld_scale(LdBounds a, double s) -> LdBounds {
  return ld_mul(a, {s, s});
}
template <class Bound>
void ld_retained(Bound b, LdBounds v, std::string_view label) {
  // Only independent arithmetic corroboration has a rounding allowance.
  // No allowance participates in the producer's certificate inequality.
  const auto allowance = 512 * std::numeric_limits<long double>::epsilon() *
                         std::max({1.L, std::abs(v.low), std::abs(v.high)});
  check(std::isfinite(b.lower) && std::isfinite(b.upper) &&
            b.lower <= b.upper && b.lower <= v.low + allowance &&
            b.upper >= v.high - allowance,
        label);
}
auto ld_support(const AuditSolid& s, const Direction& n) -> long double {
  const auto center = ld_dot(s.first(), n).high;
  if (!s.box()) {
    const auto& cap =
        std::get<BoardingPlantedBodyCapsuleBinding>(s.binding.reservation);
    const auto length = std::sqrt(static_cast<long double>(n[0]) * n[0] +
                                  static_cast<long double>(n[1]) * n[1] +
                                  static_cast<long double>(n[2]) * n[2]);
    return std::max(center, ld_dot(s.second(), n).high) +
           cap.radius_metres * length;
  }
  const auto& b = *s.box();
  long double extent{};
  const bool ellipse = s.binding.id == BoardingBodyPartId::trunk ||
                       s.binding.id == BoardingBodyPartId::helmet;
  for (std::size_t j = 0; j < 3; ++j) {
    const auto term =
        component(b.half_size_metres, j) * ld_abs(ld_dot(s.column(j), n)).high;
    extent += ellipse ? term * term : term;
  }
  return center + (ellipse ? std::sqrt(extent) : extent);
}

struct Hash {
  std::uint64_t value{1469598103934665603ULL};
  void add(std::uint64_t n) { value = (value ^ n) * 1099511628211ULL; }
  void number(double n) { add(std::bit_cast<std::uint64_t>(n)); }
  void bound(Scalar b) {
    number(b.lower);
    number(b.upper);
    add(b.supported);
  }
  void bound(BoardingPlantedLegScalarBounds b) {
    number(b.lower);
    number(b.upper);
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
struct Summary {
  std::array<std::uint32_t, 24> used{};
  std::uint64_t hash{}, output{};
  bool exists{};
};
static_assert(sizeof(Summary) <= 200);
constexpr std::array<std::size_t Limits::*, 20> limit_members{
    &Limits::source_guards,
    &Limits::projection_guards,
    &Limits::definition_guards,
    &Limits::sole_extrema,
    &Limits::source_coordinates,
    &Limits::intersection_operations,
    &Limits::midpoint_operations,
    &Limits::allocation_operations,
    &Limits::pressure_candidates,
    &Limits::disk_edges,
    &Limits::self_body_guards,
    &Limits::self_pairs,
    &Limits::self_axes,
    &Limits::self_signed_trials,
    &Limits::self_owners,
    &Limits::self_hip_complements,
    &Limits::unit_axis_operations,
    &Limits::output_bytes,
    &Limits::construction_guards,
    &Limits::construction_operations};
auto get_limit(const Limits& l, std::size_t i) -> std::size_t {
  switch (i) {
    case 0: return l.phase.graphs;
    case 1: return l.phase.legs;
    case 2: return l.phase.bodies;
    case 3: return l.phase.sectors;
    case 4: return l.phase.timing;
    default: return l.*limit_members[i - 5];
  }
}
void set_limit(Limits& l, std::size_t i, std::size_t n) {
  switch (i) {
    case 0: l.phase.graphs = n; break;
    case 1: l.phase.legs = n; break;
    case 2: l.phase.bodies = n; break;
    case 3: l.phase.sectors = n; break;
    case 4: l.phase.timing = n; break;
    default: l.*limit_members[i - 5] = n; break;
  }
}
auto work(const BoardingIntermediateEndpoint02Counters& w)
    -> std::array<std::uint64_t, 24> {
  return {w.phase.graphs,
          w.phase.legs,
          w.phase.bodies,
          w.phase.sectors,
          w.phase.timing,
          w.source_guards,
          w.projection_guards,
          w.definition_guards,
          w.sole_extrema,
          w.source_coordinates,
          w.intersection_operations,
          w.midpoint_operations,
          w.allocation_operations,
          w.pressure_candidates,
          w.disk_edges,
          w.self_body_guards,
          w.self_pairs,
          w.self_axes,
          w.self_signed_trials,
          w.self_owners,
          w.self_hip_complements,
          w.unit_axis_operations,
          w.construction_guards,
          w.construction_operations};
}
[[gnu::noinline]] auto independent_request(double y) -> Request {
  Request r{};
  const auto assign = [](Constant& c, std::initializer_list<double> xs) {
    c.count = xs.size();
    std::copy(xs.begin(), xs.end(), c.terms.begin());
  };
  for (std::size_t e = 0; e < 2; ++e) {
    assign(r.root[e].coordinates[0], {.16, .16, -.0075});
    assign(r.root[e].coordinates[1], {y});
    assign(r.root[e].coordinates[2], {-.55, -.17, .19});
    assign(r.feet[0].sole[e].coordinates[0], {.16, -.14, .16});
    assign(r.feet[0].sole[e].coordinates[1], {double(-230000) * 1e-6});
    assign(r.feet[0].sole[e].coordinates[2], {-1.14});
    assign(r.feet[1].sole[e].coordinates[0], {.16, .14});
    assign(r.feet[1].sole[e].coordinates[1], {double(-160000) * 1e-6});
    assign(r.feet[1].sole[e].coordinates[2], {-.8});
  }
  r.root_yaw_half = {.125, .125};
  r.torso_lean_half = {-.30, -.30};
  r.port_reaction_fraction = {.0625, .0625};
  r.seconds_per_parameter = 2;
  return r;
}

[[gnu::noinline]] void audit_owner(const Cell& c, std::size_t r) {
  const auto& row = original_regions[r];
  const auto& o = c.owners[r];
  if (!o.arithmetic_supported) return;
  if (!o.structural_identity) {
    check(!o.certified,
          "Early sufficient-owner refusal publishes no certified expression");
    return;
  }
  if (o.certified)
    check(row.junction == BoardingSelfJunction::shoulder
              ? o.extent.upper < o.limit.lower &&
                    o.secondary_extent.upper < o.limit.lower
              : o.extent.upper <= o.limit.lower,
          "Original owner inequality uses unchanged non-strict/strict policy");
  const auto side = row.second >= 9 ? 1U : 0U;
  const auto frame_dot = [&](std::size_t frame, std::size_t column,
                             const RecoveryPoint& u) {
    return ld_dot(recovery_point(c.phase.frames[frame].columns[column].value),
                  u);
  };
  switch (row.junction) {
    case BoardingSelfJunction::waist: {
      LdBounds e{};
      const auto y = recovery_point(c.phase.frames[1].columns[1].value);
      constexpr std::array half{.24, .12, .18};
      for (std::size_t j = 0; j < 3; ++j)
        e = ld_add(e, ld_scale(ld_abs(frame_dot(0, j, y)), half[j]));
      ld_retained(o.extent, e,
                  "Moving waist uses actual root/trunk interval columns and "
                  "full pelvis");
      ld_retained(o.limit, {row.limit, row.limit},
                  "Original waist cap unchanged");
      break;
    }
    case BoardingSelfJunction::neck:
      ld_retained(o.extent, {.2695, .2695},
                  "Authentic trunk semiaxis neck cap");
      ld_retained(o.limit, {.2695, .2695},
                  "Neck support and cap use same source identity");
      break;
    case BoardingSelfJunction::hip:
    case BoardingSelfJunction::ankle: {
      std::array<LdBounds, 3> u;
      const bool hip = row.junction == BoardingSelfJunction::hip;
      const auto axis_index = hip ? side : side + 2;
      if ((c.unit_axis_complete_mask & (1U << axis_index)) == 0) return;
      for (std::size_t i = 0; i < 3; ++i)
        u[i] = ld(c.unit_axes[axis_index][i]);
      if (!hip && u[1].low < 0) {
        check(!o.certified,
              "Unsupported ankle cancellation sign earns no owner");
        return;
      }
      const auto axis_dot = [&](std::size_t frame, std::size_t column) {
        const auto axis =
            recovery_point(c.phase.frames[frame].columns[column].value);
        LdBounds value{};
        for (std::size_t i = 0; i < 3; ++i)
          value = ld_add(value, ld_mul(ld(axis[i]), u[i]));
        return value;
      };
      const auto x = axis_dot(hip ? 0 : side + 2, 0),
                 z = axis_dot(hip ? 0 : side + 2, 2);
      auto e = ld_add(ld_scale(ld_abs(x), hip ? .24 : .06),
                      ld_scale(ld_abs(z), hip ? .18 : .14));
      if (hip)
        e = ld_add(ld_sub(e, ld_scale(x, side == 0 ? -.14 : .14)),
                   ld_scale(ld_abs(axis_dot(0, 1)), .12));
      ld_retained(o.extent, e,
                  "Hip/ankle owner corroborates original full box support "
                  "along actual axis");
      ld_retained(o.limit, {row.limit, row.limit},
                  "Original axial slab limit unchanged");
      break;
    }
    case BoardingSelfJunction::wrist:
      ld_retained(o.extent, {.05, .05},
                  "Rigid folded hand original full wrist support");
      ld_retained(o.limit, {row.limit, row.limit},
                  "Original finite wrist slab unchanged");
      break;
    case BoardingSelfJunction::shoulder: {
      const auto cosine = std::sqrt(.5L);
      const auto threshold =
          std::sqrt(static_cast<long double>(row.limit) * row.limit -
                    static_cast<long double>(.065) * .065);
      const auto e = -threshold * cosine + .065 * cosine;
      const auto secondary = -static_cast<long double>(.35898) * cosine + .065;
      ld_retained(o.extent, {e, e},
                  "Both original proximal shoulder split terms retained");
      ld_retained(o.secondary_extent, {secondary, secondary},
                  "Original finite distal shoulder split retained");
      ld_retained(o.limit, {-.18, -.18},
                  "Shoulder proof uses actual trunk semiaxis");
      break;
    }
    case BoardingSelfJunction::knee:
    case BoardingSelfJunction::elbow: {
      const bool knee = row.junction == BoardingSelfJunction::knee;
      const auto radius = knee ? .105 : .065;
      LdBounds cosine{std::sqrt(.5L), std::sqrt(.5L)};
      if (knee) {
        const auto ds = ld(c.phase.legs[side].distance_squared);
        const auto a = static_cast<long double>(.47285),
                   b = static_cast<long double>(.47478);
        cosine =
            ld_neg(ld_mul(ld_sub(ld_sub(ds, {a * a, a * a}), {b * b, b * b}),
                          {1 / (2 * a * b), 1 / (2 * a * b)}));
      }
      const auto sine = ld_scale(ld_sub({1, 1}, cosine), .5);
      const auto e = static_cast<long double>(radius) * radius;
      ld_retained(o.extent, {e, e}, "Original half-ray radius squared");
      const auto region_square =
          static_cast<long double>(row.limit) * row.limit;
      ld_retained(o.limit, ld_mul(sine, {region_square, region_square}),
                  "Original finite ball half-ray bound");
      break;
    }
  }
}
[[gnu::noinline]] void audit_hip(const Cell& c, std::size_t side) {
  const auto& h = c.hip_complements[side];
  if (!h.attempted || !h.extent_evaluated || !h.arithmetic_supported) return;
  const auto x = ld(h.axis[0]), y = ld(h.axis[1]), z = ld(h.axis[2]);
  const auto transverse = ld_root(ld_add(ld_square(x), ld_square(z)));
  const auto extent =
      ld_add(ld_scale(y, h.slab_limit_metres), ld_scale(transverse, .105));
  ld_retained(h.transverse, transverse,
              "Complement transverse full interval norm");
  ld_retained(h.extent, extent,
              "Original slab complement extent without shrinking radius");
  ld_retained(h.limit, {-.12, -.12},
              "Original unchanged rigid pelvis underside");
  ld_retained(h.strict_gap, ld_sub({-.12, -.12}, extent),
              "Actual complement strict gap retained");
}
static_assert(!std::is_default_constructible_v<Context>);
static_assert(!std::is_default_constructible_v<Token>);
auto finite(Scalar b) -> bool {
  return b.supported && std::isfinite(b.lower) && std::isfinite(b.upper) &&
         b.lower <= b.upper;
}
auto mask_count(bool b) -> std::size_t {
  return b ? 1 : 0;
}
template <class T, std::size_t N>
auto mask_count(const std::array<T, N>& xs) -> std::size_t {
  std::size_t n{};
  for (const auto& x : xs)
    n += mask_count(x);
  return n;
}
constexpr std::array<Condition, 19> capacity_conditions{
    Condition::source_capacity,
    Condition::projection_capacity,
    Condition::definition_capacity,
    Condition::sole_extrema_capacity,
    Condition::source_coordinate_capacity,
    Condition::intersection_capacity,
    Condition::midpoint_capacity,
    Condition::allocation_capacity,
    Condition::pressure_capacity,
    Condition::edge_capacity,
    Condition::self_body_capacity,
    Condition::self_pair_capacity,
    Condition::self_axis_capacity,
    Condition::self_signed_capacity,
    Condition::self_owner_capacity,
    Condition::self_hip_capacity,
    Condition::unit_axis_capacity,
    Condition::slice_guard_capacity,
    Condition::slice_operation_capacity};
void authority() {
  check(
      !Diagnostic::world_qualified && !Diagnostic::material_qualified &&
          !Diagnostic::halo_qualified && !Diagnostic::route_qualified &&
          !Diagnostic::seat_qualified && !Diagnostic::actor_qualified &&
          !Diagnostic::save_qualified && !Diagnostic::dynamics_qualified &&
          !Diagnostic::strength_qualified && !Diagnostic::friction_qualified &&
          !Diagnostic::first_flight_qualified,
      "Static nominal support and SELF do not grant downstream qualification");
}
auto operation_max(Stage s) -> std::uint8_t {
  switch (s) {
    case Stage::source: return 63;
    case Stage::projection: return 250;
    case Stage::definition: return 30;
    case Stage::sole_extrema: return 3;
    case Stage::source_coordinates: return 7;
    case Stage::intersection: return 3;
    case Stage::midpoint: return 3;
    case Stage::allocation: return 5;
    case Stage::pressure_candidate: return 1;
    case Stage::disk: return 15;
    case Stage::body: return 62;
    case Stage::unit_axes: return 23;
    case Stage::slice_guard: return 17;
    case Stage::slice_operation: return 63;
    default: return 255;
  }
}
void cursor(Stage s, std::uint8_t op, std::uint16_t pair, std::uint8_t region,
            std::uint8_t axis, std::uint8_t sign) {
  const auto maximum = operation_max(s);
  check(maximum == 255 ? op == 255 : op <= maximum,
        "Stage discriminates original zero-based operation");
  check(pair == 65535 || pair < 105, "Only original pair cursor");
  check(region == 255 || region < 14, "Only original region cursor");
  check(axis == 255 || axis < 14, "Bounded upcoming original proposal cursor");
  check(sign == 255 || sign < 2, "Only original signed trial");
  if (pair < 105 && region < 14)
    check(pair_index(original_regions[region].first,
                     original_regions[region].second) == pair,
          "Pair and connected region are the same original obligation");
}
auto prefix_mask(std::uint64_t count) -> std::uint64_t {
  return count >= 64 ? std::numeric_limits<std::uint64_t>::max()
                     : (std::uint64_t{1} << count) - 1;
}
[[gnu::noinline]] void self_accounting(const Cell& c, const Diagnostic& d) {
  check((c.self_body_evaluated & (std::uint64_t{1} << 63)) == 0 &&
            bit_count(c.self_body_evaluated) == d.work.self_body_guards &&
            c.self_body_evaluated == prefix_mask(d.work.self_body_guards),
        "Body63 high bit is unused and charged prefix is honest");
  check((c.unit_axis_attempted & 0xff000000U) == 0 &&
            (c.unit_axis_written & ~c.unit_axis_attempted) == 0 &&
            bit_count(c.unit_axis_attempted) == d.work.unit_axis_operations &&
            c.unit_axis_attempted == prefix_mask(d.work.unit_axis_operations) &&
            (c.unit_axis_complete_mask & 0xf0U) == 0,
        "Unit24 attempt/write/complete masks preserve actual charge semantics");
  check(c.unit_axis_cursor == 255 || c.unit_axis_cursor < 24,
        "Unit cursor is zero-based or NOT_RUN");
  for (std::size_t a = 0; a < 4; ++a) {
    const auto block = 0x3fU << (6 * a);
    if ((c.unit_axis_complete_mask & (1U << a)) != 0) {
      check((c.unit_axis_written & block) == block,
            "Complete axis needs all SUB3/DIV3 writes");
      for (auto b : c.unit_axes[a])
        check(finite(b), "Only genuine complete axis is usable");
    }
    for (std::size_t j = 0; j < 3; ++j)
      if ((c.unit_axis_written & (1U << (6 * a + 3 + j))) != 0)
        check(finite(c.unit_axes[a][j]),
              "Written direct quotient is finite supported");
  }
  if (c.self_body_complete)
    check(c.nominal_support_complete &&
              c.self_body_evaluated == kBoardingIntermediateEndpoint02BodyMask,
          "Fresh SELF body only after same-cell support and all63 guards");
  if (d.work.unit_axis_operations < 24)
    check(d.work.self_pairs == 0 && c.examined_pairs == 0 &&
              c.accepted_pairs == 0 && c.owner_attempted_mask == 0,
          "Incomplete unit prelude cannot reach later SELF math");
  check(c.accepted_pairs <= c.examined_pairs && c.examined_pairs <= 105 &&
            c.examined_pairs == d.work.self_pairs &&
            (c.owner_attempted_mask & 0xc000U) == 0 &&
            bit_count(c.owner_attempted_mask) <= d.work.self_owners,
        "Retained pair and owner masks fit actually charged work");
  std::array<std::uint16_t, 7> counts{};
  std::size_t pair{};
  for (std::size_t a = 0; a < 15; ++a)
    for (std::size_t b = a + 1; b < 15; ++b, ++pair) {
      const auto code = static_cast<unsigned>(c.self_certificates[pair]);
      check(
          code <= 6 && (pair < c.accepted_pairs ? code > 0 : code == 0),
          "Selected certificates are a genuine accepted lexicographic prefix");
      if (code < counts.size() && code) ++counts[code];
      if (code == 1) {
        const auto cap = [](std::size_t p) {
          return p == 3 || p == 4 || p == 6 || p == 7 || p == 9 || p == 10 ||
                 p == 12 || p == 13;
        };
        const auto n = 4U + (cap(a) ? 2U : 3U) + (cap(b) ? 2U : 3U);
        check((c.self_axes[pair] & 0xe0U) == 0 && (c.self_axes[pair] & 15U) < n,
              "Selected axis ordinal fits actual grouped proposal count8..10");
        const auto r = region_index(a, b);
        if (r < 14)
          check((c.owner_attempted_mask & (1U << r)) != 0 &&
                    !c.owners[r].certified,
                "Adjacent separating plane retains attempted failed original "
                "owner");
      } else
        check(c.self_axes[pair] == 255,
              "No fake selected plane for owner/NOT_RUN pair");
      if (code > 1) {
        const auto r = region_index(a, b);
        check(r < 14, "Only original adjacent pair can use an owner");
        if (r >= 14) continue;
        check((c.owner_attempted_mask & (1U << r)) != 0,
              "Owner charge precedes certificate");
        if (code == 6) {
          const auto side = b >= 9 ? 1U : 0U;
          const auto& h = c.hip_complements[side];
          check(!c.owners[r].certified && h.attempted && h.certified &&
                    h.region == r && h.pair == pair,
                "Complement retains genuinely failed original owner");
        } else
          check(c.owners[r].certified && c.owners[r].arithmetic_supported &&
                    c.owners[r].structural_identity &&
                    unsigned(c.owners[r].certificate) == code,
                "Actual original owner is the selected certificate");
      }
    }
  check(counts == c.self_certificate_counts && counts[0] == 0,
        "Seven counts retain selected accepted certificates only");
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& h = c.hip_complements[side];
    if (!h.attempted) continue;
    const auto r = side == 0 ? 1U : 2U;
    check(h.side == side && h.region == r &&
              h.pair == pair_index(0, side == 0 ? 3 : 9) &&
              !c.owners[r].certified &&
              h.slab_limit_metres == original_regions[r].limit,
          "Genuine hip complement uses unchanged region and failed owner");
    if (h.arithmetic_supported && h.nominal_unit_identity) {
      check((c.unit_axis_complete_mask & (1U << side)) != 0,
            "Hip complement borrows a genuinely completed stored direct unit "
            "axis");
      for (std::size_t j = 0; j < 3; ++j)
        check(h.axis[j].lower == c.unit_axes[side][j].lower &&
                  h.axis[j].upper == c.unit_axes[side][j].upper,
              "Hip owner and complement use the same stored normalized "
              "expression");
    }
    if (h.certified)
      check(h.arithmetic_supported && h.nominal_unit_identity &&
                h.upright_pelvis_identity && h.original_slab_identity &&
                h.extent_evaluated && h.axis[1].upper < 0 &&
                h.extent.upper < h.limit.lower && h.strict_gap.lower > 0,
            "Complement needs genuine negative unit axis and strict "
            "full-capsule proof");
  }
  if (c.self_complete)
    check(
        c.self_body_complete && c.unit_axis_complete_mask == 15 &&
            c.accepted_pairs == 105 && c.examined_pairs == 105 &&
            c.owner_attempted_mask == 0x3fff,
        "FullSELF needs all105 pairs,14 original owners and genuine four axes");
}
[[gnu::noinline]] void slice_accounting(const Diagnostic& d, const Limits& l) {
  const auto& x = d.slice;
  check(x.preflight_guards == 1,
        "Every structured invocation retains its actual FP-FIRST guard");
  check(d.work.construction_operations <= l.construction_operations &&
            d.work.construction_operations <= 64 &&
            d.work.construction_guards <= l.construction_guards &&
            d.work.construction_guards <= 18,
        "Construction successful charges obey the isolated ceilings");
  check(x.operation_attempted == prefix_mask(d.work.construction_operations) &&
            bit_count(x.guard_attempted) == d.work.construction_guards &&
            x.guard_attempted == prefix_mask(d.work.construction_guards) &&
            (x.operation_written & ~x.operation_attempted) == 0 &&
            (x.guard_written & ~x.guard_attempted) == 0 &&
            (x.guard_attempted & ~0x3ffffU) == 0,
        "Construction attempted and written masks distinguish lazy success");
  check((x.operation == 255 || x.operation < 64) &&
            (x.guard == 255 || x.guard < 18) && (x.side == 255 || x.side < 2) &&
            (x.zero_mask & ~3U) == 0,
        "Only registered operation, guard, side and zero-branch indices");
  for (const auto& [operation, guard] :
       {std::pair{44U, 6U}, {51U, 7U}, {55U, 8U}, {58U, 11U}, {61U, 13U}})
    if (d.work.construction_operations > operation)
      check((x.guard_written & (1U << guard)) != 0,
            "A later operation requires its actual interleaved guard");
  if (x.zero_mask & 1U)
    check((x.operation_written & (std::uint64_t{1} << 40)) != 0,
          "PORT max-zero branch is earned only at O41");
  if (x.zero_mask & 2U)
    check((x.operation_written & (std::uint64_t{1} << 47)) != 0,
          "STAR max-zero branch is earned only at O48");
  if (x.operation_written & (std::uint64_t{1} << 53))
    check(std::isfinite(x.lo),
          "Actually published O54 lower endpoint is finite");
  if (x.operation_written & (std::uint64_t{1} << 54))
    check(std::isfinite(x.hi),
          "Actually published O55 upper endpoint is finite");
  if (x.operation_written & (std::uint64_t{1} << 57))
    check(std::isfinite(x.y), "Actually stored O58 RN coefficient is finite");
  if (x.guard_written & (1U << 10))
    check(x.y > x.lo, "G11 compares actual stored Y strictly above actual lo");
  if (x.guard_written & (1U << 11))
    check(x.y < x.hi, "G12 compares actual stored Y strictly below actual hi");
  if (x.complete)
    check(x.operation_written == std::numeric_limits<std::uint64_t>::max() &&
              x.guard_written == 0x3ffffU && x.arithmetic_supported &&
              x.condition == SliceCondition::none && d.source_enrolled &&
              x.lo < x.y && x.y < x.hi && x.y >= -8 && x.y <= 8 &&
              d.reporting_elapsed_seconds == 2,
          "Complete construction earns its private-key requested T2 cue only");
  else
    check(d.reporting_elapsed_seconds == 0 && d.work.phase_calls == 0 &&
              d.cells.empty() && !d.kinematic_complete && !d.complete,
          "Incomplete construction retains clock0 and never calls original "
          "graph");
  if (x.condition == SliceCondition::guard_capacity)
    check(d.state == State::capacity &&
              d.stop_condition == Condition::slice_guard_capacity,
          "Construction guard capacity remains an actual hard terminal");
  if (x.condition == SliceCondition::operation_capacity)
    check(d.state == State::capacity &&
              d.stop_condition == Condition::slice_operation_capacity,
          "Construction operation capacity remains an actual hard terminal");
  if (x.condition == SliceCondition::unsupported_arithmetic)
    check(!x.arithmetic_supported && d.state == State::unsupported,
          "Attempted construction arithmetic failure clears only earned slice "
          "arithmetic");
  if (d.work.phase_calls != 0 || !d.cells.empty())
    check(x.complete && d.reporting_elapsed_seconds == 2,
          "Reserve and original compiler require genuine generated packet "
          "admission");
}
[[gnu::noinline]] void accounting(const Diagnostic& d, const Limits& l) {
  authority();
  check(d.version == 2 && d.self_version == 1 && d.candidate == candidate,
        "Immutable selected version and candidate");
  check(d.cells.size() <= 1 && d.cells.capacity() <= 1 &&
            d.output_capacity_bytes ==
                2 * sizeof(Expected) + d.cells.capacity() * sizeof(Cell) &&
            d.output_capacity_bytes <= l.output_bytes,
        "Actual two-header plus one-slot ownership is truthful");
  check(bit_count(d.source_evaluated) == d.work.source_guards &&
            d.source_evaluated == prefix_mask(d.work.source_guards),
        "Source64 records are actually charged, never a shift by64");
  if (d.source_enrolled)
    check(d.source_evaluated == std::numeric_limits<std::uint64_t>::max() &&
              d.work.source_guards == 64 && Access::valid(d.source),
          "Only genuine full source64 enrollment authenticates the seed");
  slice_accounting(d, l);
  const auto w = work(d.work);
  for (std::size_t i = 0; i < w.size(); ++i) {
    check(w[i] <= get_limit(l, i < 22 ? i : i + 1),
          "Successful charges fit isolated lowered caps");
    aggregate(i, w[i]);
  }
  aggregate(24, d.work.phase_calls);
  check(d.work.phase_calls <= 1 && d.work.phase.graphs <= d.work.phase_calls,
        "Original compiler invocations include genuine guarded attempts");
  if (std::ranges::find(capacity_conditions, d.stop_condition) !=
      capacity_conditions.end())
    check(d.state == State::capacity,
          "Hard capacity survives earlier ordinary findings");
  if (d.stop_condition == Condition::unsupported_arithmetic)
    check(d.state == State::unsupported && !d.arithmetic_supported,
          "Unsupported stop is never ordinary success");
  cursor(d.stop_stage, d.stop_operation, d.stop_self_pair, d.stop_self_region,
         d.stop_self_axis, d.stop_self_sign);
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    cursor(r.self_stage, r.operation, r.self_pair, r.self_region, r.self_axis,
           r.self_sign);
    if (r.source_key) {
      bool found{};
      for (std::size_t side = 0; side < 2; ++side)
        if (const auto* q = Access::partition(d.source, side))
          found = found || ((q->faces[0].key == *r.source_key ||
                             q->faces[1].key == *r.source_key) &&
                            q->faces[0].source_object == r.source_name);
      check(found, "Source refusal key/name belongs to actual retained source");
    }
  }
  for (const auto& c : d.cells) {
    check((c.definition_evaluated & 0x80000000U) == 0 &&
              bit_count(c.definition_evaluated) == d.work.definition_guards,
          "Definition31 lazy attempted mask matches charge prefix");
    if (c.phase.complete) {
      check(c.phase.arithmetic_supported && c.phase.nominal_links &&
                c.phase.target_sole_identities && c.phase.joint_sectors &&
                c.phase.derivative_domains && c.phase.timing_complete &&
                c.phase.first == 0 && c.phase.last == 1,
            "Usable phase is actual original accepted whole constant hold");
      accepted_timing(c.phase);
    }
    if (c.projection_complete)
      check(c.phase.complete && c.constant_state &&
                mask_count(c.projected_carrier_complete) == 11 &&
                d.work.projection_guards == 251,
            "All11 current carriers and original packet gates earn projection");
    check(mask_count(c.sole_extrema_evaluated) <= d.work.sole_extrema &&
              mask_count(c.source_coordinate_evaluated) <=
                  d.work.source_coordinates &&
              mask_count(c.intersection_evaluated) <=
                  d.work.intersection_operations &&
              mask_count(c.midpoint_evaluated) <= d.work.midpoint_operations &&
              mask_count(c.allocation_evaluated) <=
                  d.work.allocation_operations,
          "Attempted scalar masks never invent later completed arithmetic");
    if (c.nominal_support_complete) {
      check(c.projection_complete && c.plane_identities &&
                c.nominal_equilibrium && c.finite_contact_supported &&
                c.nominal_load_supported && c.star_support &&
                c.definition_evaluated == 0x7fffffffU &&
                c.port_share.lower == .0625 && c.port_share.upper == .0625 &&
                c.star_share.lower == .9375 && c.star_share.upper == .9375,
            "Fixed exact1/16 nominal support uses all original definitions");
      for (std::size_t side = 0; side < 2; ++side) {
        const auto& site = c.sites[side];
        check(site.loaded && site.complete && site.plane_identity &&
                  mask_count(site.sole_evaluated) == 4 &&
                  mask_count(site.source_evaluated) == 4,
              "Both full finite loaded disks are earned");
        for (std::size_t e = 0; e < 4; ++e)
          for (const auto* edge : {&site.sole_edges[e], &site.source_edges[e]})
            check(edge->disk_contained && finite(edge->signed_side) &&
                      finite(edge->squared_margin_gap) &&
                      edge->signed_side.lower > 0 &&
                      edge->squared_margin_gap.lower >= 0,
                  "Original strict .020+.010 disk certificate");
      }
    }
    self_accounting(c, d);
  }
  if (d.complete)
    check(d.state == State::accepted && d.cells.size() == 1 &&
              d.cells[0].complete && d.source_enrolled &&
              d.kinematic_complete && d.constant_state &&
              d.projection_complete && d.nominal_support_complete &&
              d.self_complete && !d.first_refusal &&
              d.stop_condition == Condition::none,
          "Complete endpoint earns same-cell phase/support/all105 SELF");
}
void hash_parts(Hash& h, const Diagnostic& d) {
  if (!d.source_enrolled) return;
  for (std::size_t side = 0; side < 2; ++side) {
    const auto* q = Access::partition(d.source, side);
    check(q != nullptr, "Enrolled genuine source has both original partitions");
    if (!q) continue;
    h.number(q->plane_metres);
    for (const auto& face : q->faces) {
      h.text(face.source_object);
      h.add(unsigned(face.key.buffer));
      h.add(face.key.group);
      h.add(face.key.triangle);
    }
    for (const auto& v : q->perimeter_metres) {
      h.number(v.x);
      h.number(v.y);
      h.number(v.z);
    }
  }
  for (const auto& p : d.parts) {
    h.add(unsigned(p.id));
    h.add(unsigned(p.mass.first));
    h.add(unsigned(p.mass.second));
    h.add(p.mass.weight);
    h.add(p.reservation.index());
    if (const auto* b =
            std::get_if<BoardingRoutePhaseBoxBinding>(&p.reservation)) {
      h.add(unsigned(b->center));
      h.add(unsigned(b->frame));
      h.number(b->half_size_metres.x);
      h.number(b->half_size_metres.y);
      h.number(b->half_size_metres.z);
    } else {
      const auto& capsule =
          std::get<BoardingPlantedBodyCapsuleBinding>(p.reservation);
      h.add(unsigned(capsule.start));
      h.add(unsigned(capsule.end));
      h.number(capsule.radius_metres);
    }
  }
}
void hash_refusal(Hash& h, const BoardingIntermediateEndpoint02Refusal& r) {
  h.add(unsigned(r.condition));
  h.add(unsigned(r.predicate_condition));
  h.add(unsigned(r.self_stage));
  h.add(r.operation);
  h.add(r.self_pair);
  h.add(r.self_region);
  h.add(r.self_axis);
  h.add(r.self_sign);
  h.add(r.side.value_or(2));
  h.add(r.edge.value_or(4));
  h.add(r.axis.value_or(2));
  h.add(r.source_edge);
  h.text(r.source_name);
  h.add(r.limiting_bound.supported);
  if (finite(r.limiting_bound)) h.bound(r.limiting_bound);
  h.add(unsigned(r.phase.condition));
  h.add(r.phase.side.value_or(2));
  if (r.phase.condition != BoardingRouteFootPhaseCondition::none) {
    h.number(r.phase.first);
    h.number(r.phase.last);
    const auto cause = r.phase.condition;
    if (cause == BoardingRouteFootPhaseCondition::derivative_domain ||
        cause == BoardingRouteFootPhaseCondition::reach ||
        cause == BoardingRouteFootPhaseCondition::forward_branch ||
        cause == BoardingRouteFootPhaseCondition::joint_sector ||
        cause == BoardingRouteFootPhaseCondition::root_speed ||
        cause == BoardingRouteFootPhaseCondition::root_acceleration ||
        cause == BoardingRouteFootPhaseCondition::joint_speed ||
        cause == BoardingRouteFootPhaseCondition::sole_speed)
      h.bound(r.phase.limiting_bound);
    h.add(r.phase.depth);
    h.add(unsigned(r.phase.predicate_condition));
  }
  if (r.source_key) {
    h.add(unsigned(r.source_key->buffer));
    h.add(r.source_key->group);
    h.add(r.source_key->triangle);
  }
}
void hash_slice(Hash& h, const Diagnostic& d) {
  const auto& x = d.slice;
  h.add(x.preflight_guards);
  h.add(x.operation_attempted);
  h.add(x.operation_written);
  h.add(x.guard_attempted);
  h.add(x.guard_written);
  h.add(x.operation);
  h.add(x.guard);
  h.add(x.side);
  h.add(unsigned(x.condition));
  h.add(x.arithmetic_supported);
  h.add(x.complete);
  if (x.operation_written & (std::uint64_t{1} << 57)) h.number(x.y);
  if (x.operation_written & (std::uint64_t{1} << 53)) h.number(x.lo);
  if (x.operation_written & (std::uint64_t{1} << 54)) h.number(x.hi);
  if (x.operation_written & (std::uint64_t{1} << 40)) h.add(x.zero_mask & 1U);
  if (x.operation_written & (std::uint64_t{1} << 47)) h.add(x.zero_mask & 2U);
  h.add(x.limiting_bound.supported);
  if (finite(x.limiting_bound)) h.bound(x.limiting_bound);
}
[[gnu::noinline]] auto semantic_hash(const Diagnostic& d) -> std::uint64_t {
  Hash h;
  h.add(d.version);
  h.add(d.self_version);
  h.add(unsigned(d.candidate));
  h.add(d.source_evaluated);
  h.add(unsigned(d.state));
  h.add(unsigned(d.stop_condition));
  h.add(unsigned(d.stop_stage));
  h.add(d.stop_operation);
  h.add(d.stop_self_pair);
  h.add(d.stop_self_region);
  h.add(d.stop_self_axis);
  h.add(d.stop_self_sign);
  for (bool b :
       {d.arithmetic_supported, d.source_enrolled, d.kinematic_complete,
        d.constant_state, d.projection_complete, d.plane_identities,
        d.nominal_equilibrium, d.finite_contact_supported,
        d.nominal_load_supported, d.nominal_support_complete,
        d.self_body_complete, d.self_complete, d.complete})
    h.add(b);
  h.number(d.reporting_elapsed_seconds);
  h.add(d.work.phase_calls);
  for (auto n : work(d.work))
    h.add(n);
  hash_slice(h, d);
  hash_parts(h, d);
  h.add(d.cells.size());
  for (const auto& c : d.cells) {
    h.add(unsigned(c.state));
    h.add(c.definition_evaluated);
    h.add(c.self_body_evaluated);
    h.add(c.unit_axis_attempted);
    h.add(c.unit_axis_written);
    h.add(c.unit_axis_complete_mask);
    h.add(c.unit_axis_cursor);
    h.add(c.owner_attempted_mask);
    h.add(c.examined_pairs);
    h.add(c.accepted_pairs);
    for (auto b : c.self_certificates)
      h.add(unsigned(b));
    for (auto b : c.self_axes)
      h.add(b);
    for (auto b : c.self_certificate_counts)
      h.add(b);
    hash_mask(h, c.projected_carrier_complete);
    hash_mask(h, c.sole_extrema_evaluated);
    hash_mask(h, c.source_coordinate_evaluated);
    hash_mask(h, c.intersection_evaluated);
    hash_mask(h, c.midpoint_evaluated);
    hash_mask(h, c.allocation_evaluated);
    hash_mask(h, c.pressure_evaluated);
    for (bool b :
         {c.arithmetic_supported, c.kinematic_complete, c.constant_state,
          c.projection_complete, c.plane_identities, c.nominal_equilibrium,
          c.finite_contact_supported, c.nominal_load_supported,
          c.nominal_support_complete, c.star_support, c.self_body_complete,
          c.self_complete, c.complete})
      h.add(b);
    h.add(c.phase.complete);
    if (c.phase.complete) {
      for (const auto& p : c.phase.points)
        hash_jet(h, p, false);
      for (const auto& p : c.phase.mass_points)
        hash_jet(h, p, false);
      hash_jet(h, c.phase.center_of_mass, false);
      for (const auto& f : c.phase.frames)
        for (const auto& p : f.columns)
          hash_jet(h, p, false);
    }
    for (auto b : {c.port_share, c.star_share}) {
      h.add(b.supported);
      if (finite(b)) h.bound(b);
    }
    for (std::size_t side = 0; side < 2; ++side) {
      for (auto b : c.pressure_xz[side]) {
        h.add(b.supported);
        if (finite(b)) h.bound(b);
      }
      const auto& s = c.sites[side];
      h.add(s.loaded);
      h.add(s.complete);
      h.add(s.plane_identity);
      h.add(unsigned(s.sole_status));
      h.add(unsigned(s.source_status));
      hash_mask(h, s.sole_evaluated);
      hash_mask(h, s.source_evaluated);
      for (std::size_t e = 0; e < 4; ++e)
        for (const auto* b : {&s.sole_edges[e], &s.source_edges[e]}) {
          h.add(b->disk_contained);
          for (auto v : {b->signed_side, b->edge_length_squared,
                         b->squared_margin_gap}) {
            h.add(v.supported);
            if (finite(v)) h.bound(v);
          }
        }
    }
    for (std::size_t a = 0; a < 4; ++a)
      for (std::size_t j = 0; j < 3; ++j)
        if (c.unit_axis_written & (1U << (6 * a + 3 + j)))
          h.bound(c.unit_axes[a][j]);
    for (std::size_t r = 0; r < 14; ++r) {
      const auto& o = c.owners[r];
      h.add(o.arithmetic_supported);
      h.add(o.structural_identity);
      h.add(o.certified);
      h.add(unsigned(o.certificate));
      const auto side = original_regions[r].second >= 9 ? 1U : 0U;
      const bool sign_ok =
          original_regions[r].junction != BoardingSelfJunction::ankle ||
          ((c.unit_axis_complete_mask & (1U << (side + 2))) &&
           c.unit_axes[side + 2][1].lower >= 0);
      if ((c.owner_attempted_mask & (1U << r)) && o.arithmetic_supported &&
          o.structural_identity && sign_ok) {
        h.bound(o.extent);
        h.bound(o.limit);
        if (original_regions[r].junction == BoardingSelfJunction::shoulder)
          h.bound(o.secondary_extent);
      }
    }
    for (const auto& x : c.hip_complements) {
      h.add(x.attempted);
      h.add(x.arithmetic_supported);
      h.add(x.nominal_unit_identity);
      h.add(x.upright_pelvis_identity);
      h.add(x.original_slab_identity);
      h.add(x.extent_evaluated);
      h.add(x.certified);
      if (x.attempted) {
        h.add(x.side);
        h.add(x.region);
        h.add(x.pair);
        h.number(x.slab_limit_metres);
      }
      if (x.attempted && x.arithmetic_supported && x.nominal_unit_identity)
        for (auto b : x.axis)
          h.bound(b);
      if (x.extent_evaluated && x.arithmetic_supported)
        for (auto b : {x.transverse, x.extent, x.limit, x.strict_gap})
          h.bound(b);
    }
  }
  h.add(d.first_refusal.has_value());
  if (d.first_refusal) hash_refusal(h, *d.first_refusal);
  return h.value;
}
[[gnu::noinline]] void capture(const Diagnostic& d, Summary& s) {
  const auto w = work(d.work);
  for (std::size_t i = 0; i < w.size(); ++i) {
    if (w[i] > std::numeric_limits<std::uint32_t>::max())
      throw std::runtime_error("Baseline counter overflow");
    s.used[i] = static_cast<std::uint32_t>(w[i]);
  }
  check(d.output_capacity_bytes <= 16777216,
        "Baseline owned output is bounded");
  s.output = static_cast<std::uint32_t>(d.output_capacity_bytes);
  s.hash = semantic_hash(d);
  s.exists = true;
}
[[gnu::noinline]] void unit_oracle(const Cell& c) {
  for (std::size_t a = 0; a < 4; ++a) {
    if ((c.unit_axis_complete_mask & (1U << a)) == 0) continue;
    const auto side = a % 2;
    const auto base = side == 0 ? 4U : 11U;
    const auto start = base + (a < 2 ? 0U : 2U);
    const auto len = static_cast<long double>(a < 2 ? .47285 : .47478);
    for (std::size_t j = 0; j < 3; ++j) {
      const auto& k = c.phase.points[base + 1].value;
      const auto& h = c.phase.points[start].value;
      const LdBounds delta{static_cast<long double>(component(k.lower, j)) -
                               component(h.upper, j),
                           static_cast<long double>(component(k.upper, j)) -
                               component(h.lower, j)};
      check(finite(c.unit_axes[a][j]),
            "Actual normalized axis has supported direct quotient");
      ld_retained(c.unit_axes[a][j], {delta.low / len, delta.high / len},
                  "Direct current endpoint SUB/DIV encloses genuine nominal "
                  "unit component");
    }
  }
}
[[gnu::noinline]] void interval_self_audit(const Diagnostic& d) {
  if (d.cells.size() != 1) return;
  const auto& c = d.cells[0];
  if (!c.phase.complete || !c.self_body_complete) return;
  const ExceptionState exceptions;
  std::size_t owner_records{}, hip_records{}, pair_records{};
  unit_oracle(c);
  std::array<RecoveryPoint, 18> relative;
  const auto rootp = recovery_point(c.phase.points[0].value);
  for (std::size_t p = 0; p < 18; ++p) {
    const auto source = recovery_point(c.phase.points[p].value);
    for (std::size_t j = 0; j < 3; ++j)
      relative[p][j] =
          p == 0 ? RecoveryInterval{} : ri_sub(source[j], rootp[j]);
  }
  for (std::size_t r = 0; r < 14; ++r)
    if (c.owner_attempted_mask & (1U << r)) {
      ++owner_records;
      charge(totals.records, 121);
      audit_owner(c, r);
    }
  for (std::size_t side = 0; side < 2; ++side)
    if (c.hip_complements[side].attempted) {
      ++hip_records;
      charge(totals.records, 121);
      audit_hip(c, side);
    }
  std::size_t pair{};
  for (std::size_t a = 0; a < 15; ++a)
    for (std::size_t b = a + 1; b < 15; ++b, ++pair) {
      if (c.self_certificates[pair] == SelfCertificate::not_run) continue;
      ++pair_records;
      charge(totals.records, 121);
      if (c.self_certificates[pair] != SelfCertificate::convex_support_plane)
        continue;
      const AuditSolid first{d.parts[a], c.phase, relative},
          second{d.parts[b], c.phase, relative};
      const auto n = selected_direction(first, second, c.self_axes[pair]);
      auto minus = n;
      for (auto& v : minus)
        v = -v;
      const auto sa = ld_support(first, n), sb = ld_support(second, minus);
      const auto allowance = 512 * std::numeric_limits<long double>::epsilon() *
                             std::max({1.L, std::abs(sa), std::abs(sb)});
      check(sa + sb <= allowance, "Actual selected interval plane excludes "
                                  "full original reservation interiors");
    }
  check(owner_records <= 14 && hip_records <= 2 && pair_records <= 105,
        "FIRST interval audit has no hidden extra records or body poses");
  std::cout << "FIRST_ENDPOINT02_INTERVAL_AUDIT owners=" << owner_records
            << " hips=" << hip_records << " pairs=" << pair_records << '\n';
}
using PlainPoint = std::array<long double, 3>;
auto plain(RigidVector3 v) -> PlainPoint {
  return {v.x, v.y, v.z};
}
auto side_value(PlainPoint a, PlainPoint b, PlainPoint p) -> long double {
  return (b[2] - a[2]) * (p[0] - a[0]) - (b[0] - a[0]) * (p[2] - a[2]);
}
auto enclosed(Scalar b, long double v) -> bool {
  return finite(b) && contains({b.lower, b.upper}, v);
}
void edge_oracle(const BoardingFootSiteEdgeEvidence& e, PlainPoint a,
                 PlainPoint b, PlainPoint p) {
  if (!finite(e.signed_side) || !finite(e.edge_length_squared) ||
      !finite(e.squared_margin_gap))
    return;
  const auto s = side_value(a, b, p), dx = b[0] - a[0], dz = b[2] - a[2];
  const auto l = dx * dx + dz * dz, r = static_cast<long double>(.020) +
                                        static_cast<long double>(.010);
  check(enclosed(e.signed_side, s) && enclosed(e.edge_length_squared, l) &&
            enclosed(e.squared_margin_gap, s * s - r * r * l),
        "Independent original finite quad disk expression enclosed");
}
[[gnu::noinline]] void first_body_audit(const Diagnostic& d) {
  if (d.cells.size() != 1 || !d.cells[0].phase.complete || !d.slice.complete)
    return;
  const ExceptionState exceptions;
  charge(totals.bodies, 1);
  const auto& c = d.cells[0];
  const auto r = independent_request(d.slice.y);
  const auto o = oracle(r, 0, false);
  cell_oracle(c.phase, o);
  if (!c.projection_complete) return;
  std::array<PlainPoint, 2> pressure{};
  const auto* port = Access::partition(d.source, 0);
  if (!port) return;
  bool port_ready = true;
  for (std::size_t a = 0; a < 2; ++a) {
    const auto axis = a == 0 ? 0U : 2U;
    const auto half = static_cast<long double>(a == 0 ? .06 : .14);
    long double lo = component(port->perimeter_metres[0], axis), hi = lo;
    for (auto v : port->perimeter_metres) {
      lo = std::min(lo, static_cast<long double>(component(v, axis)));
      hi = std::max(hi, static_cast<long double>(component(v, axis)));
    }
    lo = std::max(lo, o.soles[0][axis].value - half);
    hi = std::min(hi, o.soles[0][axis].value + half);
    pressure[0][axis] = (lo + hi) * .5L;
    pressure[1][axis] =
        (o.com[axis].value - .0625L * pressure[0][axis]) / .9375L;
    if (finite(c.pressure_xz[0][a]))
      check(enclosed(c.pressure_xz[0][a], pressure[0][axis]),
            "Full authentic source/sole intersection midpoint encloses actual "
            "port witness");
    else
      port_ready = false;
  }
  if (!port_ready) return;
  for (std::size_t a = 0; a < 2; ++a)
    if (finite(c.pressure_xz[1][a]))
      check(enclosed(c.pressure_xz[1][a], pressure[1][a == 0 ? 0U : 2U]),
            "Same port expression and exact15/16 denominator conserve nominal "
            "moment");
  for (std::size_t side = 0; side < 2; ++side) {
    const auto* q = Access::partition(d.source, side);
    if (!q) continue;
    const auto x = o.soles[side][0].value, z = o.soles[side][2].value;
    const auto hx = static_cast<long double>(.06),
               hz = static_cast<long double>(.14);
    const std::array<PlainPoint, 4> corners{{{x + hx, 0, z - hz},
                                             {x - hx, 0, z - hz},
                                             {x - hx, 0, z + hz},
                                             {x + hx, 0, z + hz}}};
    for (std::size_t e = 0; e < 4; ++e) {
      if (c.sites[side].sole_evaluated[e])
        edge_oracle(c.sites[side].sole_edges[e], corners[e],
                    corners[(e + 1) % 4], pressure[side]);
      if (c.sites[side].source_evaluated[e])
        edge_oracle(c.sites[side].source_edges[e],
                    plain(q->perimeter_metres[e]),
                    plain(q->perimeter_metres[(e + 1) % 4]), pressure[side]);
    }
  }
}
[[gnu::noinline]] void print_refusal(
    const BoardingIntermediateEndpoint02Refusal& r) {
  std::cout << " cause=" << unsigned(r.condition) << '/'
            << unsigned(r.predicate_condition)
            << " stage=" << unsigned(r.self_stage)
            << " op=" << unsigned(r.operation) << " side=" << r.side.value_or(2)
            << " edge=" << r.edge.value_or(4) << " axis=" << r.axis.value_or(2)
            << " SELF=" << r.self_pair << ',' << unsigned(r.self_region) << ','
            << unsigned(r.self_axis) << ',' << unsigned(r.self_sign)
            << " phase=" << unsigned(r.phase.condition) << '/'
            << unsigned(r.phase.predicate_condition) << ":"
            << r.phase.side.value_or(2)
            << " bound_supported=" << r.limiting_bound.supported;
  if (finite(r.limiting_bound))
    std::cout << " bound=" << r.limiting_bound.lower << ','
              << r.limiting_bound.upper;
  if (r.source_key)
    std::cout << " key=" << unsigned(r.source_key->buffer) << ','
              << r.source_key->group << ',' << r.source_key->triangle;
  std::cout << " name=" << r.source_name;
}
[[gnu::noinline]] void print_first(const Diagnostic& d) {
  std::cout << "FIRST_INTERMEDIATE_ENDPOINT02 A01 candidate="
            << unsigned(d.candidate) << " state=" << unsigned(d.state)
            << " complete=" << d.complete << " flags=" << d.arithmetic_supported
            << d.source_enrolled << d.kinematic_complete << d.constant_state
            << d.projection_complete << d.plane_identities
            << d.nominal_equilibrium << d.finite_contact_supported
            << d.nominal_load_supported << d.nominal_support_complete
            << d.self_body_complete << d.self_complete
            << " clock=" << d.reporting_elapsed_seconds
            << " cells=" << d.cells.size() << " capacity=" << d.cells.capacity()
            << " output=" << d.output_capacity_bytes
            << " phase_calls=" << d.work.phase_calls << " work=";
  for (auto n : work(d.work))
    std::cout << n << ',';
  std::cout << " source_mask=" << d.source_evaluated
            << " stop=" << unsigned(d.stop_condition)
            << " stop_stage=" << unsigned(d.stop_stage)
            << " stop_op=" << unsigned(d.stop_operation)
            << " stop_SELF=" << d.stop_self_pair << ','
            << unsigned(d.stop_self_region) << ',' << unsigned(d.stop_self_axis)
            << ',' << unsigned(d.stop_self_sign);
  if (d.first_refusal) print_refusal(*d.first_refusal);
  std::cout << '\n';
  std::cout << "FIRST_ENDPOINT02_SLICE masks=" << d.slice.operation_attempted
            << ',' << d.slice.operation_written << ','
            << d.slice.guard_attempted << ',' << d.slice.guard_written
            << " cursors=" << unsigned(d.slice.operation) << ','
            << unsigned(d.slice.guard) << ',' << unsigned(d.slice.side)
            << " condition=" << unsigned(d.slice.condition)
            << " flags=" << d.slice.arithmetic_supported << d.slice.complete
            << " preflight=" << unsigned(d.slice.preflight_guards);
  if (d.slice.operation_written & (std::uint64_t{1} << 57))
    std::cout << " Y=" << d.slice.y;
  else
    std::cout << " Y=NOT_RUN";
  if (d.slice.operation_written & (std::uint64_t{1} << 53))
    std::cout << " lo=" << d.slice.lo;
  else
    std::cout << " lo=NOT_RUN";
  if (d.slice.operation_written & (std::uint64_t{1} << 54))
    std::cout << " hi=" << d.slice.hi;
  else
    std::cout << " hi=NOT_RUN";
  std::cout << " zero=";
  if (d.slice.operation_written & (std::uint64_t{1} << 40))
    std::cout << unsigned(d.slice.zero_mask & 1U);
  else
    std::cout << "NOT_RUN";
  std::cout << ',';
  if (d.slice.operation_written & (std::uint64_t{1} << 47))
    std::cout << unsigned((d.slice.zero_mask >> 1) & 1U);
  else
    std::cout << "NOT_RUN";
  if (finite(d.slice.limiting_bound))
    std::cout << " bound=" << d.slice.limiting_bound.lower << ','
              << d.slice.limiting_bound.upper;
  std::cout << '\n';
  for (const auto& c : d.cells) {
    std::cout << "FIRST_ENDPOINT02_CELL phase=" << c.phase.complete
              << " current=" << c.projection_complete
              << " support=" << c.nominal_support_complete
              << " body=" << c.self_body_complete << " self=" << c.self_complete
              << " D=" << c.definition_evaluated
              << " B=" << c.self_body_evaluated
              << " unit=" << c.unit_axis_attempted << ',' << c.unit_axis_written
              << ',' << unsigned(c.unit_axis_complete_mask) << ','
              << unsigned(c.unit_axis_cursor) << " pairs=" << c.examined_pairs
              << ',' << c.accepted_pairs << " owners=" << c.owner_attempted_mask
              << " masks=" << mask_count(c.projected_carrier_complete) << ','
              << mask_count(c.sole_extrema_evaluated) << ','
              << mask_count(c.source_coordinate_evaluated) << ','
              << mask_count(c.intersection_evaluated) << ','
              << mask_count(c.midpoint_evaluated) << ','
              << mask_count(c.allocation_evaluated) << ','
              << mask_count(c.pressure_evaluated) << " codes=";
    for (auto n : c.self_certificate_counts)
      std::cout << n << ',';
    std::cout << '\n';
    for (std::size_t a = 0; a < 4; ++a)
      if (c.unit_axis_complete_mask & (1U << a)) {
        std::cout << "FIRST_ENDPOINT02_UNIT " << a;
        for (auto b : c.unit_axes[a])
          std::cout << ' ' << b.lower << ',' << b.upper << ':' << b.supported;
        std::cout << '\n';
      }
    for (std::size_t side = 0; side < 2; ++side) {
      std::cout << "FIRST_ENDPOINT02_CONTACT " << side << " pressure=";
      for (auto b : c.pressure_xz[side]) {
        if (finite(b))
          std::cout << b.lower << ',' << b.upper << ';';
        else
          std::cout << "NOT_RUN;";
      }
      std::cout << " disk=" << unsigned(c.sites[side].sole_status) << ','
                << unsigned(c.sites[side].source_status)
                << " edges=" << mask_count(c.sites[side].sole_evaluated) << ','
                << mask_count(c.sites[side].source_evaluated) << '\n';
    }
    for (std::size_t r = 0; r < 14; ++r)
      if (c.owner_attempted_mask & (1U << r)) {
        const auto& o = c.owners[r];
        std::cout << "FIRST_ENDPOINT02_OWNER " << r
                  << " flags=" << o.arithmetic_supported
                  << o.structural_identity << o.certified
                  << " certificate=" << unsigned(o.certificate);
        const auto side = original_regions[r].second >= 9 ? 1U : 0U;
        const bool usable =
            original_regions[r].junction != BoardingSelfJunction::ankle ||
            ((c.unit_axis_complete_mask & (1U << (side + 2))) &&
             c.unit_axes[side + 2][1].lower >= 0);
        if (o.arithmetic_supported && o.structural_identity && usable)
          std::cout << " extent=" << o.extent.lower << ',' << o.extent.upper
                    << " limit=" << o.limit.lower << ',' << o.limit.upper;
        else
          std::cout << " expression=NOT_RUN";
        if (o.arithmetic_supported && o.structural_identity &&
            original_regions[r].junction == BoardingSelfJunction::shoulder)
          std::cout << " secondary=" << o.secondary_extent.lower << ','
                    << o.secondary_extent.upper;
        std::cout << '\n';
      }
    for (const auto& h : c.hip_complements)
      if (h.attempted) {
        std::cout << "FIRST_ENDPOINT02_HIP " << h.side
                  << " flags=" << h.arithmetic_supported
                  << h.nominal_unit_identity << h.upright_pelvis_identity
                  << h.original_slab_identity << h.extent_evaluated
                  << h.certified;
        if (h.arithmetic_supported && h.nominal_unit_identity) {
          std::cout << " axis=";
          for (auto b : h.axis)
            std::cout << b.lower << ',' << b.upper << ';';
        }
        if (h.extent_evaluated && h.arithmetic_supported)
          std::cout << " E=" << h.extent.lower << ',' << h.extent.upper
                    << " limit=" << h.limit.lower << ',' << h.limit.upper
                    << " gap=" << h.strict_gap.lower << ','
                    << h.strict_gap.upper;
        std::cout << '\n';
      }
  }
  std::cout << std::flush;
}
void slot(std::size_t n, bool executed) {
  check(n == totals.next_slot && n >= 1 && n <= 91,
        "Every registered label is visited once in literal order");
  charge(totals.next_slot, 92);
  if (executed) {
    charge(totals.consumers, 91);
    aggregate(25, 1); // The genuine exported FP-FIRST runs even on unexpected.
  } else {
    charge(totals.skips, 91);
    std::cout << "SKIP A" << n << " unreached_or_unavailable\n";
  }
}
[[gnu::noinline]] void first_one(const Provider& p, Summary& summary) {
  slot(1, true);
  const auto result =
      assess_origin_boarding_intermediate_endpoint02(p, candidate);
  if (!result) {
    std::cout << "FIRST_INTERMEDIATE_ENDPOINT02 A01 API_ERROR="
              << result.error() << '\n'
              << std::flush;
    check(!result.error().empty(),
          "Unknown FIRST refusal retained without retry");
    return;
  }
  print_first(*result);
  accounting(*result, Limits{});
  capture(*result, summary);
  first_body_audit(*result);
  interval_self_audit(*result);
}
[[gnu::noinline]] void create_source(const NativeCraftBinding& binding,
                                     const OriginBoardingBootSupport& boots,
                                     std::optional<Provider>& p) {
  charge(totals.creators, 1);
  auto result = make_origin_boarding_intermediate_pause_support(binding, boots);
  std::cout << "FIRST_INTERMEDIATE_ENDPOINT02_SOURCE admitted="
            << result.has_value();
  if (!result)
    std::cout << " error=" << result.error();
  else if (const auto* e = result->summary())
    std::cout << " complete=" << e->complete
              << " bytes=" << e->actual_source_bytes
              << " condition=" << unsigned(e->condition)
              << " work=" << e->work.base_guards << ',' << e->work.metadata_rows
              << ',' << e->work.face_reads << ',' << e->work.quad_records;
  std::cout << '\n' << std::flush;
  if (result) {
    check(Access::valid(*result) && result->summary() &&
              result->summary()->complete,
          "Only unchanged genuine creator issues the retained source");
    p.emplace(std::move(*result));
  } else
    check(!result.error().empty(), "Creator refusal has no substitute");
  // Result and moved-empty source handle die BEFORE A01 and owner-release
  // tests.
}
constexpr std::array<Condition, 17> field_conditions{
    Condition::source_capacity,
    Condition::projection_capacity,
    Condition::definition_capacity,
    Condition::sole_extrema_capacity,
    Condition::source_coordinate_capacity,
    Condition::intersection_capacity,
    Condition::midpoint_capacity,
    Condition::allocation_capacity,
    Condition::pressure_capacity,
    Condition::edge_capacity,
    Condition::self_body_capacity,
    Condition::self_pair_capacity,
    Condition::self_axis_capacity,
    Condition::self_signed_capacity,
    Condition::self_owner_capacity,
    Condition::self_hip_capacity,
    Condition::unit_axis_capacity};
constexpr std::array<BoardingRouteFootPhaseCondition, 5> old_capacity{
    BoardingRouteFootPhaseCondition::graph_capacity,
    BoardingRouteFootPhaseCondition::leg_capacity,
    BoardingRouteFootPhaseCondition::body_capacity,
    BoardingRouteFootPhaseCondition::sector_capacity,
    BoardingRouteFootPhaseCondition::timing_capacity};
auto used(const Summary& s, std::size_t field) -> std::uint32_t {
  return s.used[field < 22 ? field : field - 1];
}
void lower_refusal(const Diagnostic& d, std::size_t field) {
  check(!d.complete && d.state == State::capacity,
        "A genuinely reached lowered budget cannot grant endpoint completion");
  if (field < 5)
    check(d.first_refusal &&
              d.first_refusal->condition == Condition::phase_prerequisite &&
              d.first_refusal->phase.condition == old_capacity[field],
          "Original compiler exact lowered cause is retained");
  else if (field == 22)
    check(d.stop_condition == Condition::output_capacity &&
              d.work.phase_calls == 0 && d.work.source_guards == 0 &&
              d.work.construction_guards == 0 && d.cells.empty(),
          "Required full-slot shortfall precedes source and construction");
  else if (field == 23 || field == 24)
    check(d.stop_condition == (field == 23
                                   ? Condition::slice_guard_capacity
                                   : Condition::slice_operation_capacity),
          "Actually reached construction budget has its own hard terminal");
  else
    check(d.stop_condition == field_conditions[field - 5],
          "Later reached capacity remains terminal despite ordinary findings");
}
[[gnu::noinline]] void assessment_one(const Provider& p, std::size_t label,
                                      const Limits& l, const Summary& s,
                                      std::size_t field = 25,
                                      std::string_view error = {},
                                      bool exact = false, bool empty = false,
                                      Candidate id = candidate) {
  slot(label, true);
  const auto result = detail::intermediate_endpoint02_bounded(p, id, l);
  if (!error.empty()) {
    check(!result, "Registered preflight returns unexpected without report");
    if (!result)
      check(result.error() == error, "Exact preflight error literal");
    return;
  }
  check(result.has_value(),
        "Safe finite replay returns honest structured evidence");
  if (!result) return;
  accounting(*result, l);
  if (field < 25) lower_refusal(*result, field);
  if (empty)
    check(result->first_refusal &&
              result->first_refusal->condition == Condition::invalid_binding &&
              result->stop_condition == Condition::invalid_binding &&
              result->work.source_guards == 1 &&
              result->source_evaluated == 1 &&
              result->work.construction_guards == 0 &&
              result->work.construction_operations == 0 &&
              result->work.phase_calls == 0 && result->cells.empty() &&
              !result->source_enrolled && !result->complete,
          "Publicly emptied genuine alias fails charged S01 without "
          "construction");
  else if (Access::valid(p))
    check(Access::data(result->source) == Access::data(p),
          "New report retains the same actual genuine source owner");
  if (exact && s.exists) {
    check(semantic_hash(*result) == s.hash,
          "Exact genuine masks, work, causes and earned scalar bits replay");
    check(result->output_capacity_bytes == s.output,
          "Exact replay separately retains actual output ownership");
  }
  if (label == 89)
    check(result->slice.operation_attempted == 0 &&
              result->slice.guard_attempted == 0 &&
              result->work.phase_calls == 0,
          "Mixed source zero wins before any new construction record");
  if (label == 90)
    check(result->slice.guard == 0 && result->slice.guard_attempted == 0 &&
              result->slice.operation_attempted == 0 &&
              result->work.phase_calls == 0,
          "Mixed G01 failed charge leaves O01 NOT_RUN");
  if (label == 91)
    check(
        result->slice.operation == 0 &&
            result->slice.operation_attempted == 0 &&
            result->slice.guard_written == 0x3fU &&
            result->work.phase_calls == 0,
        "Mixed O01 zero follows actual G01 through G06 before original graph");
}
[[gnu::noinline]] void isolated_roster(const Provider& p, const Summary& s) {
  const Limits defaults;
  for (std::size_t field = 0; field < 25; ++field) {
    if (field != 22 && (!s.exists || used(s, field) == 0)) {
      slot(2 + field, false);
      continue;
    }
    auto l = defaults;
    set_limit(l, field, 0);
    assessment_one(p, 2 + field, l, s, field,
                   field == 22 ? "intermediate endpoint02 output headers" : "");
  }
  for (std::size_t field = 0; field < 25; ++field) {
    auto l = defaults;
    set_limit(l, field, get_limit(defaults, field) + 1);
    assessment_one(p, 27 + field, l, s, 25,
                   "intermediate endpoint02 invalid limits");
  }
  for (std::size_t field = 0; field < 25; ++field) {
    if (!s.exists || (field != 22 && used(s, field) <= 1)) {
      slot(52 + field, false);
      continue;
    }
    auto l = defaults;
    set_limit(l, field,
              (field == 22
                   ? boarding_intermediate_endpoint02_required_output_bytes()
                   : used(s, field)) -
                  1);
    assessment_one(p, 52 + field, l, s, field);
  }
}
[[gnu::noinline]] void exact_roster(const Provider& p, const Summary& s) {
  if (!s.exists) {
    slot(77, false);
    return;
  }
  Limits l;
  for (std::size_t field = 0; field < 25; ++field)
    if (field != 22) set_limit(l, field, used(s, field));
  l.output_bytes = boarding_intermediate_endpoint02_required_output_bytes();
  assessment_one(p, 77, l, s, 25, {}, true);
}
[[gnu::noinline]] void remaining_roster(Provider& p, const Summary& s) {
  const Limits defaults;
  for (std::size_t mode = 0; mode < 6; ++mode) {
    Environment changed(mode);
    if (changed.available)
      assessment_one(p, 78 + mode, defaults, s, 25,
                     "intermediate endpoint02 unsupported floating point");
    else
      slot(78 + mode, false);
  }
  assessment_one(p, 84, defaults, s, 25,
                 "intermediate endpoint02 invalid candidate", false, false,
                 static_cast<Candidate>(255));
  {
    auto alias = p;
    auto saved = std::move(alias);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Empty-handle API contract.
    assessment_one(alias, 85, defaults, s, 25, {}, false, true);
    alias = std::move(saved);
  }
  {
    auto moved = std::move(p);
    assessment_one(moved, 86, defaults, s, 25, {}, s.exists);
    p = std::move(moved);
  }
  {
    auto survivor = p;
    {
      auto discarded = std::move(p);
      check(Access::valid(discarded), "Original owner in discarded scope");
    }
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Defined empty-handle check.
    check(!Access::valid(p),
          "Original empty AFTER discarded owner destruction");
    assessment_one(survivor, 87, defaults, s, 25, {}, s.exists);
    p = std::move(survivor);
  }
  assessment_one(p, 88, defaults, s, 25, {}, s.exists);
  if (s.exists && used(s, 5) > 0) {
    auto l = defaults;
    l.source_guards = 0;
    l.construction_guards = 0;
    assessment_one(p, 89, l, s, 5);
  } else
    slot(89, false);
  if (s.exists && used(s, 23) > 0) {
    auto l = defaults;
    l.construction_guards = 0;
    l.construction_operations = 0;
    assessment_one(p, 90, l, s, 23);
  } else
    slot(90, false);
  if (s.exists && used(s, 24) > 0) {
    auto l = defaults;
    l.construction_operations = 0;
    l.phase.graphs = 0;
    assessment_one(p, 91, l, s, 24);
  } else
    slot(91, false);
}
[[gnu::noinline]] void final_counts() {
  check(totals.creators == 1 && totals.consumers + totals.skips == 91 &&
            totals.next_slot == 92 && totals.bodies <= 1 &&
            totals.records <= 121,
        "Literal91/onecreator/raw0/inspector0/A01-only bounded audit roster");
  const Limits l;
  for (std::size_t i = 0; i < 24; ++i)
    check(totals.work[i] <= 91 * get_limit(l, i < 22 ? i : i + 1),
          "Finite harness actual charged stage ceiling");
  check(totals.work[24] <= 91 && totals.work[25] == totals.consumers,
        "Original invocations distinct from graphs and actual FP-FIRST count");
  std::cout << "ENDPOINT02_VALIDATION creator=" << totals.creators
            << " consumers=" << totals.consumers << " skips=" << totals.skips
            << " raw=0 inspectors=0 bodies=" << totals.bodies
            << " interval_records=" << totals.records << " checks=" << checks
            << " failures=" << failures << " aggregate=";
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
      first_one(*source, summary);
      isolated_roster(*source, summary);
      exact_roster(*source, summary);
      remaining_roster(*source, summary);
    } else
      for (std::size_t i = 1; i <= 91; ++i)
        slot(i, false);
    final_counts();
  } catch (const std::exception& e) {
    check(false, "Unexpected harness exception");
    std::cerr << e.what() << '\n';
  }
  const bool streams = std::cout.good() && std::cerr.good() && !out.truncated &&
                       !err.truncated && bytes <= 16384;
  check(streams, "Shared16KiB stream is complete, not truncated");
  std::cout.rdbuf(oldout);
  std::cerr.rdbuf(olderr);
  return failures ? 1 : 0;
}
