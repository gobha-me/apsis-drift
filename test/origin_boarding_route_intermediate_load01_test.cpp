#include "apsis_drift/origin_boarding_route_intermediate_load01.hpp"
#include "origin_boarding_route_intermediate_load01_internal.hpp"
#include "origin_boarding_route_intermediate_step02_internal.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
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
using Diagnostic = BoardingRouteIntermediateLoad01Diagnostic;
using Cell = BoardingRouteIntermediateLoad01Cell;
using State = BoardingRouteIntermediateLoad01State;
using Condition = BoardingRouteIntermediateLoad01Condition;
using Force = BoardingRouteIntermediateLoad01PortForceState;
using Scalar = BoardingFootSiteScalarBounds;
using Limits = detail::BoardingRouteIntermediateLoad01Limits;
using DivisionLimits = detail::BoardingRouteIntermediateLoad01DivisionLimits;
using Access = detail::BoardingIntermediatePauseSupportAccess;
static_assert(!std::is_default_constructible_v<
              detail::BoardingRouteIntermediateLoad01AdmissionContext>);
static_assert(!std::is_constructible_v<
              detail::BoardingRouteIntermediateLoad01AdmissionContext,
              const Diagnostic&>);
static_assert(!std::is_default_constructible_v<
              detail::BoardingRouteIntermediateLoad01CurrentCellToken>);
static_assert(
    !std::is_constructible_v<
        detail::BoardingRouteIntermediateLoad01CurrentCellToken,
        const Diagnostic&, const Cell&, const BoardingRouteFootPhaseRequest&,
        const detail::BoardingRouteIntermediateLoad01AdmissionContext&>);
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
  std::array<std::uint32_t, 21> work{};
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
// Independent unfactored body construction, evaluated only after FIRST graphs.
using Point = std::array<long double, 3>;
using Matrix = std::array<Point, 3>;
auto add(Point a, Point b) -> Point {
  for (std::size_t i = 0; i < 3; ++i)
    a[i] += b[i];
  return a;
}
auto subtract(Point a, Point b) -> Point {
  for (std::size_t i = 0; i < 3; ++i)
    a[i] -= b[i];
  return a;
}
auto scale(Point a, long double s) -> Point {
  for (auto& x : a)
    x *= s;
  return a;
}
auto dot(Point a, Point b) -> long double {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
auto cross(Point a, Point b) -> Point {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}
auto transform(const Matrix& m, Point p) -> Point {
  Point out{};
  for (std::size_t r = 0; r < 3; ++r)
    for (std::size_t c = 0; c < 3; ++c)
      out[r] += m[r][c] * p[c];
  return out;
}
auto multiply(const Matrix& a, const Matrix& b) -> Matrix {
  Matrix out{};
  for (std::size_t r = 0; r < 3; ++r)
    for (std::size_t c = 0; c < 3; ++c)
      for (std::size_t k = 0; k < 3; ++k)
        out[r][c] += a[r][k] * b[k][c];
  return out;
}
auto yaw(long double h) -> Matrix {
  const auto t = 2 * std::atan(h), c = std::cos(t), s = std::sin(t);
  return {Point{c, 0, s}, Point{0, 1, 0}, Point{-s, 0, c}};
}
auto pitch(long double h) -> Matrix {
  const auto t = 2 * std::atan(h), c = std::cos(t), s = std::sin(t);
  return {Point{1, 0, 0}, Point{0, c, -s}, Point{0, s, c}};
}
struct Oracle {
  std::array<Point, 18> points{};
  std::array<Point, 15> mass_points{};
  Point com{}, com_velocity{}, com_acceleration{};
  std::array<Point, 18> velocities{}, accelerations{};
  std::array<Point, 2> soles{};
};
[[gnu::noinline]] auto body_oracle(double global, bool reverse) -> Oracle {
  Oracle out;
  auto& p = out.points;
  const auto wide = [](double x) { return static_cast<long double>(x); };
  p[0] = {wide(.16) + wide(.16) + wide(-.0075),
          wide(.847) + wide(.012) + wide(-.359),
          wide(-.55) + wide(-.17) + wide(.19)};
  const auto u = global < .75 ? static_cast<long double>(4 * global - 2) : 1.L;
  const auto s = u * u * u * (10 + u * (-15 + 6 * u));
  const auto torso = wide(-.25) + s * (wide(-.30) - wide(-.25));
  const auto root = yaw(wide(.125)), upper = multiply(root, pitch(torso));
  for (std::size_t i = 1; i < 4; ++i)
    p[i] = add(p[0], transform(upper, {0,
                                       wide(i == 1   ? .3495
                                            : i == 2 ? .70237
                                                     : .65237),
                                       0}));
  const auto l1 = wide(.47285), l2 = wide(.47478);
  const auto arm = -wide(.35898) * std::sqrt(wide(.5));
  for (std::size_t side = 0; side < 2; ++side) {
    const auto base = side == 0 ? 4U : 11U;
    const auto sign = side == 0 ? -1.L : 1.L;
    const Point sole =
        side == 0
            ? Point{wide(.16) + wide(-.14) + wide(.16),
                    wide(static_cast<double>(-230000) * 1e-6), wide(-1.14)}
            : Point{wide(.16) + wide(.14),
                    wide(static_cast<double>(-160000) * 1e-6), wide(-.8)};
    out.soles[side] = sole;
    p[base] = add(p[0], transform(root, {sign * wide(.14), 0, 0}));
    p[base + 2] = add(sole, {0, wide(.10), 0});
    p[base + 3] = add(sole, {0, wide(.05), 0});
    const auto d = subtract(p[base + 2], p[base]);
    const auto D = dot(d, d), rho = std::hypot(d[0], d[1]);
    const auto alpha = (l1 * l1 - l2 * l2 + D) / (2 * D);
    const auto gamma = std::sqrt((l1 * l1 - alpha * alpha * D) / D);
    const Point U{-d[0] / rho, -d[1] / rho, 0}, N{U[1], -U[0], 0};
    p[base + 1] = add(p[base], add(scale(d, alpha), scale(cross(N, d), gamma)));
    p[base + 4] =
        add(p[0], transform(upper, {sign * wide(.20265), wide(.579), 0}));
    p[base + 5] = add(p[base + 4], transform(upper, {0, arm, arm}));
    p[base + 6] = add(p[base + 5], transform(upper, {0, wide(.386), 0}));
  }
  const auto delta = wide(-.30) - wide(-.25);
  const auto hp =
      global < .75 ? delta * 30 * u * u * (1 - u) * (1 - u) / 12 : 0.L;
  const auto hpp =
      global < .75 ? delta * 60 * u * (1 - u) * (1 - 2 * u) / (12 * 12) : 0.L;
  const auto theta_first =
      (reverse ? -1.L : 1.L) * 2 * hp / (1 + torso * torso);
  const auto theta_second =
      2 * hpp / (1 + torso * torso) -
      4 * torso * hp * hp / ((1 + torso * torso) * (1 + torso * torso));
  const auto axis = transform(root, {1, 0, 0});
  for (const std::size_t j : {1U, 2U, 3U, 8U, 9U, 10U, 15U, 16U, 17U}) {
    const auto r = subtract(p[j], p[0]), tangent = cross(axis, r);
    out.velocities[j] = scale(tangent, theta_first);
    out.accelerations[j] =
        add(scale(tangent, theta_second),
            scale(cross(axis, tangent), theta_first * theta_first));
  }
  struct Mass {
    std::size_t first, second;
    std::uint32_t weight;
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
  for (std::size_t i = 0; i < masses.size(); ++i) {
    const auto& m = masses[i];
    out.mass_points[i] = scale(add(p[m.first], p[m.second]), .5L);
    out.com = add(out.com, scale(out.mass_points[i], m.weight));
    out.com_velocity =
        add(out.com_velocity,
            scale(add(out.velocities[m.first], out.velocities[m.second]),
                  .5L * m.weight));
    out.com_acceleration =
        add(out.com_acceleration,
            scale(add(out.accelerations[m.first], out.accelerations[m.second]),
                  .5L * m.weight));
  }
  out.com = scale(out.com, 1.L / 1200);
  out.com_velocity = scale(out.com_velocity, 1.L / 1200);
  out.com_acceleration = scale(out.com_acceleration, 1.L / 1200);
  return out;
}
auto finite(Scalar b) -> bool {
  return b.supported && std::isfinite(b.lower) && std::isfinite(b.upper) &&
         b.lower <= b.upper;
}
auto enclosed(Scalar b, long double x) -> bool {
  // Independent LD/libm corroboration only; never changes strict admission.
  const auto e = 256 * std::numeric_limits<long double>::epsilon() *
                 std::max(1.L, std::abs(x));
  return finite(b) && x >= static_cast<long double>(b.lower) - e &&
         x <= static_cast<long double>(b.upper) + e;
}
constexpr std::array<std::size_t Limits::*, 13> new_members{
    &Limits::source_guards,       &Limits::projection_guards,
    &Limits::definition_guards,   &Limits::pressure_candidates,
    &Limits::disk_edges,          &Limits::sole_extrema,
    &Limits::source_coordinates,  &Limits::intersection_operations,
    &Limits::midpoint_operations, &Limits::allocation_operations,
    &Limits::reaction_operations, &Limits::division_quotients,
    &Limits::division_folds};
constexpr std::array<Condition, 13> new_capacity{
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
    Condition::division_fold_capacity};
auto new_work(const BoardingRouteIntermediateLoad01Counters& w)
    -> std::array<std::size_t, 13> {
  return {w.source_guards,       w.projection_guards,
          w.definition_guards,   w.pressure_candidates,
          w.disk_edges,          w.sole_extrema,
          w.source_coordinates,  w.intersection_operations,
          w.midpoint_operations, w.allocation_operations,
          w.reaction_operations, w.division_quotients,
          w.division_folds};
}
auto limit_value(const Limits& l, std::size_t i) -> std::uint64_t {
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
    default: return l.*new_members[i - 9];
  }
}
void set_limit(Limits& l, std::size_t i, std::uint64_t value) {
  switch (i) {
    case 0: l.phase.depth = value; break;
    case 1: l.phase.nodes = value; break;
    case 2: l.phase.leaves = value; break;
    case 3: l.phase.output_bytes = value; break;
    case 4: l.phase.graphs = value; break;
    case 5: l.phase.legs = value; break;
    case 6: l.phase.bodies = value; break;
    case 7: l.phase.sectors = value; break;
    case 8: l.phase.timing = value; break;
    default: l.*new_members[i - 9] = value; break;
  }
}
auto mask_count(bool b) -> std::size_t {
  return b ? 1 : 0;
}
template <class T, std::size_t N>
auto mask_count(const std::array<T, N>& a) -> std::size_t {
  std::size_t n{};
  for (const auto& b : a)
    n += mask_count(b);
  return n;
}
struct Hash {
  std::uint64_t value{1469598103934665603ULL};
  void add(std::uint64_t n) { value = (value ^ n) * 1099511628211ULL; }
  void scalar(double x) { add(std::bit_cast<std::uint64_t>(x == 0 ? 0. : x)); }
  void bound(Scalar x) {
    scalar(x.lower);
    scalar(x.upper);
    add(x.supported);
  }
  void point(BoardingPlantedLegPointBounds p) {
    for (auto x :
         {p.lower.x, p.lower.y, p.lower.z, p.upper.x, p.upper.y, p.upper.z})
      scalar(x);
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
  auto velocity = p.derivatives.velocity;
  if (reverse) {
    const auto old = velocity;
    velocity.lower = {-old.upper.x, -old.upper.y, -old.upper.z};
    velocity.upper = {-old.lower.x, -old.lower.y, -old.lower.z};
  }
  h.point(velocity);
  h.point(p.derivatives.acceleration);
}
[[gnu::noinline]] auto semantic_hash(const Diagnostic& d) -> std::uint64_t {
  Hash h;
  h.add(d.load_version);
  h.scalar(std::min(d.requested_first, d.requested_last));
  h.scalar(std::max(d.requested_first, d.requested_last));
  h.add(static_cast<unsigned>(d.state));
  h.add(static_cast<unsigned>(d.stop_condition));
  h.add(d.complete);
  h.add(d.qualified_join);
  h.add(d.examined_nodes);
  h.add(d.mandatory_splits);
  h.add(d.maximum_depth);
  h.add(d.work.phase_calls);
  for (auto x : new_work(d.work))
    h.add(x);
  for (auto x : {d.work.phase.graphs, d.work.phase.legs, d.work.phase.bodies,
                 d.work.phase.sectors, d.work.phase.timing})
    h.add(x);
  h.add(d.cells.size());
  if (d.source_enrolled) {
    for (const auto& r : d.controls) {
      h.scalar(r.seconds_per_parameter);
      for (auto x : r.port_reaction_fraction)
        h.scalar(x);
      for (auto x : r.torso_lean_half)
        h.scalar(x);
      for (auto x : r.root_yaw_half)
        h.scalar(x);
      for (const auto& p : r.root)
        for (const auto& axis : p.coordinates) {
          h.add(axis.count);
          for (auto x : axis.terms)
            h.scalar(x);
        }
    }
  }
  for (const auto& c : d.cells) {
    h.scalar(c.global_first);
    h.scalar(c.global_last);
    h.add(c.phase_index);
    h.bound(c.port_share);
    h.bound(c.star_share);
    for (const auto& p : c.phase.points)
      hash_jet(h, p, d.reverse);
    for (const auto& p : c.phase.mass_points)
      hash_jet(h, p, d.reverse);
    hash_jet(h, c.phase.center_of_mass, d.reverse);
    for (const auto& frame : c.phase.frames)
      for (const auto& column : frame.columns)
        hash_jet(h, column, d.reverse);
    for (const auto& side : c.pressure_xz)
      for (auto p : side)
        h.bound(p);
    for (const auto& site : c.sites) {
      h.add(site.loaded);
      h.add(site.complete);
      h.add(static_cast<unsigned>(site.sole_status));
      h.add(static_cast<unsigned>(site.source_status));
      for (std::size_t i = 0; i < 4; ++i) {
        h.add(site.sole_evaluated[i]);
        h.add(site.source_evaluated[i]);
        for (const auto* e : {&site.sole_edges[i], &site.source_edges[i]}) {
          h.bound(e->signed_side);
          h.bound(e->squared_margin_gap);
          h.add(e->disk_contained);
        }
      }
    }
    h.add(static_cast<unsigned>(c.port_force_state));
    h.add(c.contains_zero_endpoint);
    h.add(c.port_positive_interior);
    h.add(c.checkpoint_threshold_met);
  }
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    h.add(static_cast<unsigned>(r.condition));
    h.add(static_cast<unsigned>(r.predicate_condition));
    h.scalar(r.first);
    h.scalar(r.last);
    h.add(r.depth);
    h.add(r.phase_index.value_or(5));
    h.add(r.side.value_or(2));
    h.add(r.edge.value_or(4));
    h.add(r.axis.value_or(2));
    h.add(r.operation.value_or(20));
    h.bound(r.limiting_bound);
    h.add(r.source_edge);
    h.text(r.source_name);
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
  std::array<std::uint64_t, 22> used{};
  std::uint64_t hash{};
  std::uint32_t phase_calls{};
  State state{};
  bool exists{}, complete{};
};
struct Summary {
  std::array<Baseline, 2> whole;
};
static_assert(sizeof(Baseline) <= 200 && sizeof(Summary) <= 400);
[[gnu::noinline]] auto summarize(const Diagnostic& d) -> Baseline {
  Baseline b;
  b.exists = true;
  b.complete = d.complete;
  b.state = d.state;
  b.hash = semantic_hash(d);
  b.phase_calls = static_cast<std::uint32_t>(d.work.phase_calls);
  b.used[0] = d.maximum_depth;
  b.used[1] = d.examined_nodes;
  b.used[2] = d.cells.size() + (d.complete ? 0 : 1);
  b.used[3] = 2 * sizeof(std::expected<Diagnostic, std::string>) +
              b.used[2] * sizeof(Cell);
  b.used[4] = d.work.phase.graphs;
  b.used[5] = d.work.phase.legs;
  b.used[6] = d.work.phase.bodies;
  b.used[7] = d.work.phase.sectors;
  b.used[8] = d.work.phase.timing;
  const auto nw = new_work(d.work);
  for (std::size_t i = 0; i < nw.size(); ++i)
    b.used[9 + i] = nw[i];
  return b;
}
void authority(const Diagnostic&) {
  check(!Diagnostic::self_qualified && !Diagnostic::material_qualified &&
            !Diagnostic::world_qualified && !Diagnostic::route_qualified &&
            !Diagnostic::seat_qualified && !Diagnostic::actor_qualified &&
            !Diagnostic::save_qualified && !Diagnostic::friction_qualified &&
            !Diagnostic::strength_qualified &&
            !Diagnostic::dynamics_qualified &&
            !Diagnostic::first_flight_qualified,
        "Partial nominal contact never grants world, actor or physical "
        "authority");
}
[[gnu::noinline]] void accounting(const Diagnostic& d, const Limits& l) {
  authority(d);
  check(d.load_version == 1, "Immutable Load01 version");
  if (d.source_enrolled)
    check(d.reporting_elapsed_seconds ==
              std::abs(detail::boarding_route_intermediate_load01_clock(
                           d.requested_last) -
                       detail::boarding_route_intermediate_load01_clock(
                           d.requested_first)),
          "Original physical clock assigned only after genuine once-source "
          "enrollment");
  else
    check(d.reporting_elapsed_seconds == 0,
          "Unenrolled refusal retains unassigned clock rather than invented "
          "duration");
  if (d.source_enrolled)
    check(d.work.source_guards == 18 && mask_count(d.source_evaluated) == 18,
          "Genuine once-source enrollment retains all18 charged identities");
  check(d.work.phase_calls <= 2047 && d.work.phase.graphs <= l.phase.graphs &&
            d.work.phase.legs <= l.phase.legs &&
            d.work.phase.bodies <= l.phase.bodies &&
            d.work.phase.sectors <= l.phase.sectors &&
            d.work.phase.timing <= l.phase.timing,
        "Original compiler work respects shared caps");
  check(
      d.examined_nodes <= l.phase.nodes && d.cells.size() <= l.phase.leaves &&
          d.maximum_depth <= l.phase.depth &&
          d.output_capacity_bytes <= l.phase.output_bytes,
      "One cover respects navigation, leaves and actual owned output capacity");
  const auto nw = new_work(d.work);
  for (std::size_t i = 0; i < 13; ++i) {
    check(nw[i] <= l.*new_members[i],
          "Charge before each lowered work operation");
    aggregate(i + 5, nw[i]);
  }
  for (std::size_t i = 0; i < 5; ++i)
    aggregate(i, std::array{d.work.phase.graphs, d.work.phase.legs,
                            d.work.phase.bodies, d.work.phase.sectors,
                            d.work.phase.timing}[i]);
  totals.phase_calls += d.work.phase_calls;
  check(d.work.phase.graphs <= d.work.phase_calls &&
            d.work.phase_calls <= d.examined_nodes,
        "Calls include guarded original compiler invocations");
  check(mask_count(d.source_evaluated) <= d.work.source_guards,
        "Source mask cannot invent uncharged identity checks");
  const auto first = std::min(d.requested_first, d.requested_last),
             last = std::max(d.requested_first, d.requested_last);
  auto cursor = first;
  std::size_t edges{}, definitions{}, quotients{}, folds{};
  for (const auto& c : d.cells) {
    check(c.global_first == cursor && c.global_last >= c.global_first &&
              c.global_last <= last,
          "Accepted cells retain an exact closed prefix without gaps");
    cursor = c.global_last;
    check(c.phase_index == detail::boarding_route_intermediate_load01_phase(
                               c.global_first, c.global_last) &&
              c.phase.first == detail::boarding_route_intermediate_load01_local(
                                   c.phase_index, c.global_first) &&
              c.phase.last == detail::boarding_route_intermediate_load01_local(
                                  c.phase_index, c.global_last),
          "Canonical phase and original local interval retained");
    check(c.phase.derivative_domains && c.phase.timing_complete,
          "Accepted original physical derivative/timing proof retained");
    const auto valid_point = [](BoardingPlantedLegPointBounds p) {
      return std::isfinite(p.lower.x) && std::isfinite(p.upper.x) &&
             p.lower.x <= p.upper.x && std::isfinite(p.lower.y) &&
             std::isfinite(p.upper.y) && p.lower.y <= p.upper.y &&
             std::isfinite(p.lower.z) && std::isfinite(p.upper.z) &&
             p.lower.z <= p.upper.z;
    };
    for (const auto& p : c.phase.points)
      check(valid_point(p.derivatives.velocity) &&
                valid_point(p.derivatives.acceleration),
            "Supported original physical body derivatives remain finite and "
            "ordered without invented magnitude cap");
    check(valid_point(c.phase.center_of_mass.derivatives.velocity) &&
              valid_point(c.phase.center_of_mass.derivatives.acceleration),
          "Authentic moving COM has finite ordered physical jets");
    check(c.complete && c.phase.complete && c.projection_complete &&
              c.plane_identities && c.nominal_equilibrium &&
              c.finite_contact_complete && c.nominal_load_complete,
          "Only complete genuine current-cell contact can enter output");
    check(mask_count(c.projected_carrier_complete) == 11 &&
              mask_count(c.definition_evaluated) == 31 &&
              mask_count(c.reaction_evaluated) == 3,
          "Accepted cell retains all original projection carriers and "
          "definitions");
    check(finite(c.port_share) && finite(c.star_share) &&
              c.port_share.lower >= 0 && c.port_share.upper <= .0625 &&
              c.star_share.lower >= .9375 && c.star_share.upper <= 1,
          "Functional share intersection is complementary and nonnegative");
    check(c.star_everywhere_positive && c.sites[1].loaded,
          "Authentic star foot remains positive throughout");
    const bool zero = c.global_first == .5 && c.global_last == .5;
    const bool boundary = c.global_first == .5 && c.global_last > .5;
    check(c.contains_zero_endpoint == (c.global_first == .5) &&
              c.port_positive_interior == (!zero),
          "Zero endpoint and positive interior remain distinct events");
    check(c.port_force_state == (zero ? Force::zero_only
                                 : boundary
                                     ? Force::zero_boundary_positive_interior
                                     : Force::everywhere_positive) &&
              c.sites[0].loaded == (!zero && !boundary),
          "Air carries no force and closed zero-boundary cell is not "
          "everywhere loaded");
    if (c.global_last == 1)
      check(c.checkpoint_threshold_met,
            "Endpoint-only checkpoint threshold earned at held final share");
    for (const auto& p : c.pressure_xz)
      for (auto x : p)
        check(finite(x),
              "Nominal moment solution uses supported pressure bounds");
    for (const auto& s : c.sites) {
      edges += mask_count(s.sole_evaluated) + mask_count(s.source_evaluated);
      for (std::size_t e = 0; e < 4; ++e)
        for (const auto* edge : {&s.sole_edges[e], &s.source_edges[e]})
          check(edge->disk_contained && edge->signed_side.supported &&
                    edge->squared_margin_gap.supported &&
                    edge->signed_side.lower > 0 &&
                    edge->squared_margin_gap.lower >= 0,
                "Accepted original disk evidence preserves strict oriented "
                "side and full margin");
    }
    definitions += mask_count(c.definition_evaluated);
    quotients += mask_count(c.division_quotient_evaluated);
    folds += mask_count(c.division_fold_evaluated);
  }
  check(edges <= d.work.disk_edges && definitions <= d.work.definition_guards &&
            quotients <= d.work.division_quotients &&
            folds <= d.work.division_folds,
        "Retained evidence is charged even when failed attempts consumed "
        "additional work");
  if (d.qualified_join) {
    check(first < .75 && last > .75 && d.join_expression_identity,
          "Join needs authentic positive-width coverage on both sides");
    bool left{}, right{};
    for (const auto& c : d.cells) {
      left = left || (c.global_first < .75 && c.global_last == .75 &&
                      c.phase_index == 3);
      right = right || (c.global_first == .75 && c.global_last > .75 &&
                        c.phase_index == 4);
    }
    check(left && right, "Earned join retains both actual adjacent cells");
  }
  if (d.complete)
    check(
        !d.cells.empty() && cursor == last && !d.first_refusal &&
            d.stop_condition == Condition::none && d.source_enrolled &&
            d.kinematic_complete && d.nonnegative_reactions &&
            d.nominal_equilibrium && d.finite_contact_complete &&
            d.nominal_load_complete &&
            (!(first < .75 && last > .75) || d.qualified_join),
        "Completion requires exact requested cover and original load evidence");
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    check(r.first >= first && r.last <= last && r.first <= r.last,
          "Original refused closed obligation lies in request");
    if (r.source_key && r.side && *r.side < 2) {
      const auto* s = d.source.summary();
      if (s) {
        const auto& keys = s->sources[*r.side].keys;
        check((*r.source_key == keys[0] || *r.source_key == keys[1]) &&
                  r.source_name == s->sources[*r.side].name,
              "Refusal keys and names are anchored by retained genuine source");
      }
    }
  }
}
auto coordinate(RigidVector3 p, std::size_t a) -> double {
  return a == 0 ? p.x : a == 1 ? p.y : p.z;
}
auto bound(BoardingPlantedLegPointBounds p, std::size_t a) -> Scalar {
  return {coordinate(p.lower, a), coordinate(p.upper, a), true};
}
auto wide(RigidVector3 p) -> Point {
  return {p.x, p.y, p.z};
}
void edge_oracle(const BoardingFootSiteEdgeEvidence& e, Point a, Point b,
                 Point pressure) {
  if (!e.signed_side.supported || !e.edge_length_squared.supported ||
      !e.squared_margin_gap.supported)
    return;
  const auto dx = b[0] - a[0], dz = b[2] - a[2],
             s = dz * (pressure[0] - a[0]) - dx * (pressure[2] - a[2]);
  const auto length = dx * dx + dz * dz,
             margin = static_cast<long double>(.020) +
                      static_cast<long double>(.010);
  check(enclosed(e.signed_side, s) && enclosed(e.edge_length_squared, length) &&
            enclosed(e.squared_margin_gap, s * s - margin * margin * length),
        "Independent full finite oriented edge and unchanged metric margin are "
        "enclosed");
}
[[gnu::noinline]] void first_oracles(const Diagnostic& d) {
  for (const double global : {.5, .625, .75, 1.}) {
    const Cell* selected{};
    for (const auto& c : d.cells) {
      if (c.complete && c.phase.complete && c.projection_complete &&
          global >= c.global_first && global <= c.global_last) {
        if (!selected || (global == .75 && c.phase_index == 3)) selected = &c;
      }
    }
    if (!selected)
      continue; // No failed/default packet or replacement location.
    charge(totals.oracles, 8);
    const auto& c = *selected;
    const auto o = body_oracle(global, d.reverse);
    for (std::size_t i = 0; i < 18; ++i)
      for (std::size_t a = 0; a < 3; ++a)
        check(enclosed(bound(c.phase.points[i].value, a), o.points[i][a]),
              "Independent unfactored18 point pose lies in genuine interval "
              "evidence");
    for (std::size_t i = 0; i < 18; ++i)
      for (std::size_t a = 0; a < 3; ++a) {
        check(enclosed(bound(c.phase.points[i].derivatives.velocity, a),
                       o.velocities[i][a]) &&
                  enclosed(bound(c.phase.points[i].derivatives.acceleration, a),
                           o.accelerations[i][a]),
              "Analytic original upper-body physical T12/T2 jets and reversed "
              "first jets are enclosed");
      }
    for (std::size_t a = 0; a < 3; ++a)
      check(enclosed(bound(c.phase.center_of_mass.derivatives.velocity, a),
                     o.com_velocity[a]) &&
                enclosed(
                    bound(c.phase.center_of_mass.derivatives.acceleration, a),
                    o.com_acceleration[a]),
            "Independent fifteen-mass physical COM jets retain reverse "
            "first/same second derivative");
    for (std::size_t i = 0; i < 15; ++i)
      for (std::size_t a = 0; a < 3; ++a)
        check(enclosed(bound(c.phase.mass_points[i].value, a),
                       o.mass_points[i][a]),
              "Original15 mass points retained at selected FIRST coordinates");
    for (std::size_t a = 0; a < 3; ++a)
      check(enclosed(bound(c.phase.center_of_mass.value, a), o.com[a]),
            "Full1200 mass COM independent oracle");
    for (std::size_t side = 0; side < 2; ++side) {
      const auto base = side == 0 ? 4U : 11U;
      for (const auto pair : std::array<std::array<std::size_t, 2>, 2>{
               {{base, base + 1}, {base + 1, base + 2}}}) {
        const auto rod = subtract(o.points[pair[1]], o.points[pair[0]]);
        const auto length =
            static_cast<long double>(pair[0] == base ? .47285 : .47478);
        check(std::abs(dot(rod, rod) - length * length) <
                  256 * std::numeric_limits<long double>::epsilon(),
              "Independent3D two-sphere rods preserve original lengths");
      }
    }
    const auto u =
        global < .75 ? static_cast<long double>(4 * global - 2) : 1.L;
    const auto w = .0625L * u * u * u * (10 + u * (-15 + 6 * u));
    check(enclosed(c.port_share, w) && enclosed(c.star_share, 1 - w),
          "Same quintic force and same complement independently enclosed");
    const auto* q = Access::partition(d.source, 0);
    if (!q) continue;
    Point port{}, star{};
    for (std::size_t axis = 0; axis < 2; ++axis) {
      const auto a = axis == 0 ? 0U : 2U;
      const auto half = static_cast<long double>(axis == 0 ? .06 : .14);
      auto lo = wide(q->perimeter_metres[0])[a], hi = lo;
      for (auto vertex : q->perimeter_metres) {
        lo = std::min(lo, wide(vertex)[a]);
        hi = std::max(hi, wide(vertex)[a]);
      }
      lo = std::max(lo, o.soles[0][a] - half);
      hi = std::min(hi, o.soles[0][a] + half);
      check(lo < hi, "Independent authentic source/fullsole intersection is "
                     "nonempty at corroboration coordinate");
      port[a] = .5L * (lo + hi);
      star[a] = (o.com[a] - w * port[a]) / (1 - w);
      check(enclosed(c.pressure_xz[0][axis], port[a]) &&
                enclosed(c.pressure_xz[1][axis], star[a]),
            "Variable-denominator COP preserves nominal moment equation");
      check(std::abs(w * port[a] + (1 - w) * star[a] - o.com[a]) <
                256 * std::numeric_limits<long double>::epsilon(),
            "Unfactored independent nominal moment balance");
    }
    for (std::size_t side = 0; side < 2; ++side) {
      const auto* source = Access::partition(d.source, side);
      if (!source) continue;
      const auto p = side == 0 ? port : star;
      const auto x = o.soles[side][0], z = o.soles[side][2],
                 hx = static_cast<long double>(.06),
                 hz = static_cast<long double>(.14);
      const std::array<Point, 4> sole{{{x + hx, 0, z - hz},
                                       {x - hx, 0, z - hz},
                                       {x - hx, 0, z + hz},
                                       {x + hx, 0, z + hz}}};
      for (std::size_t e = 0; e < 4; ++e) {
        if (c.sites[side].sole_evaluated[e])
          edge_oracle(c.sites[side].sole_edges[e], sole[e], sole[(e + 1) % 4],
                      p);
        if (c.sites[side].source_evaluated[e])
          edge_oracle(c.sites[side].source_edges[e],
                      wide(source->perimeter_metres[e]),
                      wide(source->perimeter_metres[(e + 1) % 4]), p);
      }
    }
  }
}
[[gnu::noinline]] void print(std::size_t slot, const Diagnostic& d) {
  std::cout << "FIRST_LOAD01 A" << slot << " range=" << d.requested_first << ','
            << d.requested_last << " state=" << static_cast<unsigned>(d.state)
            << " complete=" << d.complete << " flags=" << d.arithmetic_supported
            << d.source_enrolled << d.kinematic_complete
            << d.nonnegative_reactions << d.nominal_equilibrium
            << d.finite_contact_complete << d.nominal_load_complete
            << " cells=" << d.cells.size() << " nav=" << d.examined_nodes << ','
            << d.mandatory_splits << ',' << d.maximum_depth
            << " join=" << d.join_expression_identity << d.qualified_join
            << " output=" << d.output_capacity_bytes
            << " calls=" << d.work.phase_calls
            << " phase=" << d.work.phase.graphs << ',' << d.work.phase.legs
            << ',' << d.work.phase.bodies << ',' << d.work.phase.sectors << ','
            << d.work.phase.timing << " new=";
  for (auto x : new_work(d.work))
    std::cout << x << ',';
  std::cout << " stop=" << static_cast<unsigned>(d.stop_condition);
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    std::cout << " refusal=" << static_cast<unsigned>(r.condition) << ','
              << static_cast<unsigned>(r.predicate_condition) << " interval=["
              << r.first << ',' << r.last << "] local=[" << r.local_first << ','
              << r.local_last << "] phase=" << r.phase_index.value_or(5) << ','
              << static_cast<unsigned>(r.phase.condition)
              << " side/edge=" << r.side.value_or(2) << ','
              << r.edge.value_or(4) << " source=" << r.source_name << " bound=["
              << r.limiting_bound.lower << ',' << r.limiting_bound.upper << ','
              << r.limiting_bound.supported << ']';
  }
  std::cout << '\n' << std::flush;
}
constexpr std::array<std::array<double, 2>, 16> requests{{{.5, 1},
                                                          {1, .5},
                                                          {.5, .75},
                                                          {.75, .5},
                                                          {.75, 1},
                                                          {1, .75},
                                                          {.5, .5},
                                                          {.75, .75},
                                                          {1, 1},
                                                          {.625, .625},
                                                          {.625, .875},
                                                          {.875, .625},
                                                          {.5, .5625},
                                                          {.5625, .5},
                                                          {.5625, .5625},
                                                          {.75, .875}}};
// Frozen FIRST0104c25 established these outcomes before this regression was
// added. It adds no requests, fixtures or oracle positions; the original FIRST
// prints remain.
[[gnu::noinline]] void observed_outcome(std::size_t slot, const Diagnostic& d) {
  constexpr std::array<std::size_t, 10> graphs{12, 12, 11, 11, 1,
                                               1,  1,  1,  1,  1};
  constexpr std::array<std::size_t, 10> leaves{7, 7, 6, 6, 1, 1, 1, 1, 1, 1};
  constexpr std::array<std::size_t, 10> nodes{13, 13, 11, 11, 1, 1, 1, 1, 1, 1};
  constexpr std::array<std::size_t, 10> depths{4, 4, 3, 3, 0, 0, 0, 0, 0, 0};
  const auto n = graphs[slot - 1];
  check(d.state == State::accepted && d.complete && !d.first_refusal &&
            d.stop_condition == Condition::none && d.arithmetic_supported &&
            d.source_enrolled && d.kinematic_complete &&
            d.nonnegative_reactions && d.nominal_equilibrium &&
            d.finite_contact_complete && d.nominal_load_complete,
        "Observed ten selected FIRST requests genuinely complete partial "
        "nominal load");
  check(d.cells.size() == leaves[slot - 1] &&
            d.examined_nodes == nodes[slot - 1] &&
            d.maximum_depth == depths[slot - 1] &&
            d.mandatory_splits == (slot <= 2 ? 1U : 0U) &&
            d.work.phase_calls == n,
        "Frozen FIRST cell/navigation/original-call accounting retained");
  check(d.work.phase.graphs == n && d.work.phase.legs == 2 * n &&
            d.work.phase.bodies == n && d.work.phase.sectors == 3 * n &&
            d.work.phase.timing == 6 * n,
        "Frozen FIRST original compiler work retained across ramp, pause and "
        "points");
  const std::array<std::size_t, 13> expected{
      18,    251 * n, 31 * n, 2 * n, 16 * n, 4 * n, 8 * n,
      4 * n, 4 * n,   6 * n,  3 * n, 8 * n,  12 * n};
  check(new_work(d.work) == expected && mask_count(d.source_evaluated) == 18,
        "Frozen FIRST source/projection/allocation/reaction/division work "
        "retained");
  check(d.output_capacity_bytes == 9623184 && d.join_expression_identity &&
            d.qualified_join == (slot <= 2),
        "Observed output and two-sided join distinct from point or "
        "single-phase expression identity");
  std::size_t ramp{}, hold{}, boundary{}, zero{};
  for (const auto& c : d.cells) {
    ramp += c.phase_index == 3 ? 1U : 0U;
    hold += c.phase_index == 4 ? 1U : 0U;
    boundary +=
        c.port_force_state == Force::zero_boundary_positive_interior ? 1U : 0U;
    zero += c.port_force_state == Force::zero_only ? 1U : 0U;
    check(mask_count(c.projected_carrier_complete) == 11 &&
              mask_count(c.definition_evaluated) == 31 &&
              mask_count(c.reaction_evaluated) == 3 &&
              mask_count(c.sole_extrema_evaluated) == 4 &&
              mask_count(c.source_coordinate_evaluated) == 8 &&
              mask_count(c.intersection_evaluated) == 4 &&
              mask_count(c.midpoint_evaluated) == 4 &&
              mask_count(c.allocation_evaluated) == 6 &&
              mask_count(c.division_quotient_evaluated) == 8 &&
              mask_count(c.division_fold_evaluated) == 12 &&
              mask_count(c.pressure_evaluated) == 2,
          "Observed accepted cells retain every charged scalar/division mask");
    check(c.port_contact_geometry && c.star_everywhere_positive &&
              c.plane_identities && c.sites[0].complete &&
              c.sites[1].complete &&
              mask_count(c.sites[0].sole_evaluated) == 4 &&
              mask_count(c.sites[0].source_evaluated) == 4 &&
              mask_count(c.sites[1].sole_evaluated) == 4 &&
              mask_count(c.sites[1].source_evaluated) == 4,
          "Observed full sixteen-edge finite contact geometry includes "
          "zero-force endpoint");
    check(c.checkpoint_threshold_met ==
              (c.phase_index == 4 ||
               (c.global_first == .75 && c.global_last == .75)),
          "Endpoint-only dimensionless checkpoint stays distinct from "
          "positive-interior geometry");
  }
  check(boundary == (slot <= 4 ? 1U : 0U) && zero == (slot == 7 ? 1U : 0U),
        "Observed force event topology distinguishes initial zero point, "
        "boundary and positive interiors");
  if (slot <= 2)
    check(ramp == 6 && hold == 1, "Whole cover retains six ramp cells and one "
                                  "supported hold on same shared cover");
  else if (slot <= 4)
    check(ramp == 6 && hold == 0,
          "Ramp-only cover retains original six accepted ramp cells");
  else
    check(
        ramp == ((slot == 5 || slot == 6 || slot == 9) ? 0U : 1U) &&
            hold == ((slot == 5 || slot == 6 || slot == 9) ? 1U : 0U),
        "Observed single-cell cases retain canonical preceding join ownership");
  if (slot == 7)
    check(d.cells.front().port_share.lower == 0 &&
              d.cells.front().port_share.upper == 0 &&
              d.cells.front().star_share.lower == 1 &&
              d.cells.front().star_share.upper == 1 &&
              !d.cells.front().sites[0].loaded,
          "Observed exact unloaded port has no positive force while genuine "
          "star carries all nominal weight");
  if (slot == 8 || slot == 9)
    check(d.cells.front().port_share.lower == .0625 &&
              d.cells.front().port_share.upper == .0625 &&
              d.cells.front().star_share.lower == .9375 &&
              d.cells.front().star_share.upper == .9375,
          "Observed endpoints retain exact proved dyadic forces");
}
[[gnu::noinline]] void first_one(const Provider& p, std::size_t slot,
                                 Baseline* baseline) {
  charge(totals.consumers, 96);
  const auto r = requests[slot - 1];
  auto result = assess_origin_boarding_route_intermediate_load01(p, r[0], r[1]);
  if (!result) {
    std::cout << "FIRST_LOAD01 A" << slot << " API_ERROR=" << result.error()
              << '\n'
              << std::flush;
    check(false,
          "Frozen FIRST valid request must still admit its observed result");
    return;
  }
  print(slot, *result);
  observed_outcome(slot, *result);
  accounting(*result, Limits{});
  if (baseline) *baseline = summarize(*result);
  if (slot <= 2) first_oracles(*result);
}
[[gnu::noinline]] void create_source(const NativeCraftBinding& binding,
                                     const OriginBoardingBootSupport& boots,
                                     std::optional<Provider>& p) {
  charge(totals.creators, 1);
  auto result = make_origin_boarding_intermediate_pause_support(binding, boots);
  std::cout << "FIRST_LOAD01_SOURCE admitted=" << result.has_value();
  if (!result)
    std::cout << " error=" << result.error();
  else if (const auto* s = result->summary())
    std::cout << " complete=" << s->complete
              << " bytes=" << s->actual_source_bytes
              << " condition=" << static_cast<unsigned>(s->condition);
  std::cout << '\n' << std::flush;
  check(result.has_value(),
        "Observed unchanged genuine public source issuer remains admitted");
  if (result) {
    const auto* source = result->summary();
    check(source && source->complete && source->actual_source_bytes == 4096 &&
              source->condition == BoardingIntermediatePauseCondition::none,
          "Observed source admission retains genuine complete fixed4096 arena");
  }
  if (result) {
    check(Access::valid(*result),
          "Only genuine unchanged source creator issues capability");
    p.emplace(std::move(*result));
  }
}
[[gnu::noinline]] void immutable_controls() {
  for (std::size_t phase = 3; phase <= 4; ++phase) {
    auto selected = detail::boarding_route_intermediate_load01_controls(phase);
    auto original = detail::boarding_route_intermediate_step02_controls(phase);
    check(selected.has_value() && original.has_value(),
          "Exact existing phase controls are inspectable");
    if (!selected || !original) continue;
    check(selected->seconds_per_parameter == (phase == 3 ? 12 : 2) &&
              selected->port_reaction_fraction ==
                  (phase == 3 ? std::array<double, 2>{0, .0625}
                              : std::array<double, 2>{.0625, .0625}),
          "Only prescribed dyadic reaction hypothesis changes");
    original->port_reaction_fraction = selected->port_reaction_fraction;
    // Fieldwise comparison, never packet padding.
    for (std::size_t end = 0; end < 2; ++end) {
      for (std::size_t a = 0; a < 3; ++a) {
        const auto& x = selected->root[end].coordinates[a];
        const auto& y = original->root[end].coordinates[a];
        check(x.count == y.count && x.terms == y.terms,
              "Root term identity unchanged");
      }
      check(selected->root_yaw_half[end] == original->root_yaw_half[end] &&
                selected->torso_lean_half[end] ==
                    original->torso_lean_half[end],
            "Root/torso endpoints retain original motion");
      for (std::size_t side = 0; side < 2; ++side) {
        check(selected->feet[side].yaw_half[end] ==
                      original->feet[side].yaw_half[end] &&
                  selected->feet[side].swing_height_metres ==
                      original->feet[side].swing_height_metres,
              "Feet yaw/hump remain exact");
        for (std::size_t a = 0; a < 3; ++a) {
          const auto& x = selected->feet[side].sole[end].coordinates[a];
          const auto& y = original->feet[side].sole[end].coordinates[a];
          check(x.count == y.count && x.terms == y.terms,
                "Sole source expressions retain exact original terms");
        }
      }
    }
  }
  check(detail::boarding_route_intermediate_load01_clock(.5) == 36 &&
            detail::boarding_route_intermediate_load01_clock(.75) == 48 &&
            detail::boarding_route_intermediate_load01_clock(1) == 50,
        "Physical12+2 second clock is immutable");
  check(detail::boarding_route_intermediate_load01_phase(.75, .75) == 3 &&
            detail::boarding_route_intermediate_load01_phase(.75, 1) == 4,
        "Point join belongs to preceding phase, width to following");
}
auto exact_limits(const Baseline& b) -> Limits {
  Limits l;
  for (std::size_t i = 0; i < 22; ++i)
    set_limit(l, i, b.used[i]);
  return l;
}
void lower_refusal(const Diagnostic& d, std::size_t index) {
  if (index >= 9) {
    check(d.stop_condition == new_capacity[index - 9],
          "Reached new lowered cap preserves actual terminal cause");
    return;
  }
  if (index <= 3) {
    const std::array expected{
        Condition::depth_capacity, Condition::node_capacity,
        Condition::leaf_capacity, Condition::output_capacity};
    check(
        d.stop_condition == expected[index],
        "Lowered shared navigation/output cap stops before next compound work");
    return;
  }
  const std::array expected{BoardingRouteFootPhaseCondition::graph_capacity,
                            BoardingRouteFootPhaseCondition::leg_capacity,
                            BoardingRouteFootPhaseCondition::body_capacity,
                            BoardingRouteFootPhaseCondition::sector_capacity,
                            BoardingRouteFootPhaseCondition::timing_capacity};
  check(d.first_refusal &&
            d.first_refusal->phase.condition == expected[index - 4],
        "Original compiler retains its own actual stage capacity refusal");
}
[[gnu::noinline]] void assessment_one(
    const Provider& p, double first, double last, Limits l,
    const Baseline* exact = nullptr,
    std::optional<std::size_t> lowered = std::nullopt, bool invalid = false,
    bool unsafe = false, bool moved = false) {
  charge(totals.consumers, 96);
  auto result =
      detail::boarding_route_intermediate_load01_bounded(p, first, last, l);
  if (invalid) {
    check(!result, "Invalid limits/ranges/output preflight returns API refusal "
                   "without graph");
    return;
  }
  if (!result) {
    check(false,
          "Registered valid assessment unexpectedly refused API admission");
    return;
  }
  accounting(*result, l);
  if (exact)
    check(semantic_hash(*result) == exact->hash,
          "Exact capacities/caller lifetime replay preserves fieldwise "
          "canonical proof and refusal");
  if (lowered) lower_refusal(*result, *lowered);
  if (unsafe)
    check(result->stop_condition == Condition::unsupported_arithmetic &&
              result->work.phase_calls == 0 && result->cells.empty() &&
              result->work.projection_guards == 0 &&
              result->work.division_quotients == 0,
          "Unsafe FP refuses before graph or current-cell math");
  if (moved)
    check(result->stop_condition == Condition::invalid_binding &&
              result->work.phase_calls == 0 && result->cells.empty() &&
              result->work.projection_guards == 0,
          "Moved provider cannot enroll borrowed source or caller-positive "
          "flags");
}
[[gnu::noinline]] void ordinary_roster(const Provider& p) {
  for (std::size_t slot = 11; slot <= 16; ++slot) {
    const auto r = requests[slot - 1];
    assessment_one(p, r[0], r[1], Limits{});
  }
}
[[gnu::noinline]] void lowered_roster(const Provider& p,
                                      const Summary& summary) {
  const auto& b = summary.whole[0];
  const Limits defaults;
  if (b.exists) {
    assessment_one(p, .5, 1, exact_limits(b), &b);
    if (summary.whole[1].exists)
      assessment_one(p, 1, .5, exact_limits(summary.whole[1]),
                     &summary.whole[1]);
  }
  // A19--40 isolated zero for each of22 registered fields.
  for (std::size_t i = 0; i < 22; ++i) {
    auto l = defaults;
    set_limit(l, i, 0);
    const bool preflight = i == 3;
    const bool reached = b.exists && b.used[i] > 0;
    assessment_one(
        p, .5, 1, l, b.exists && !reached && !preflight ? &b : nullptr,
        reached && !preflight ? std::optional{i} : std::nullopt, preflight);
  }
  // A41--62 one raised field at a time, no combined-cap masking.
  for (std::size_t i = 0; i < 22; ++i) {
    auto l = defaults;
    set_limit(l, i, limit_value(defaults, i) + 1);
    assessment_one(p, .5, 1, l, nullptr, std::nullopt, true);
  }
  constexpr std::array<std::size_t, 14> lower_fields{1,  2,  0,  4,  9,  10, 11,
                                                     12, 13, 19, 20, 21, 18, 3};
  if (b.exists)
    for (const auto i : lower_fields) {
      if (b.used[i] <= 1)
        continue; // Declared NOTRUN_ZERO/DUPLICATE; no substitute.
      auto l = defaults;
      set_limit(l, i, b.used[i] - 1);
      const bool preflight =
          i == 3 && l.phase.output_bytes <
                        2 * sizeof(std::expected<Diagnostic, std::string>);
      assessment_one(p, .5, 1, l, nullptr,
                     preflight ? std::nullopt : std::optional{i}, preflight);
    }
}
[[gnu::noinline]] void remaining_roster(const Provider& p,
                                        const Summary& summary) {
  const auto& b = summary.whole[0];
  const Limits defaults;
  // A77--82 six unsafe modes with honest platform availability.
  for (std::size_t mode = 0; mode < 6; ++mode) {
    Environment environment(mode);
    if (environment.available)
      assessment_one(p, .5, 1, defaults, nullptr, std::nullopt, false, true);
  }
  assessment_one(p, std::nextafter(.5, 0.), 1, defaults, nullptr, std::nullopt,
                 true); // A83
  assessment_one(p, .5,
                 std::nextafter(1., std::numeric_limits<double>::infinity()),
                 defaults, nullptr, std::nullopt, true);
  assessment_one(p, std::numeric_limits<double>::quiet_NaN(), 1, defaults,
                 nullptr, std::nullopt, true);
  assessment_one(p, .5, std::numeric_limits<double>::infinity(), defaults,
                 nullptr, std::nullopt, true);
  {
    auto empty = p;
    auto retained = std::move(empty);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Empty-handle contract test.
    assessment_one(empty, .5, 1, defaults, nullptr, std::nullopt, false, false,
                   true); // A87
    assessment_one(retained, .5, 1, defaults, b.exists ? &b : nullptr);
  }
  {
    std::optional<Provider> survives;
    {
      auto original = p;
      survives.emplace(original);
    }
    assessment_one(*survives, .5, 1, defaults, b.exists ? &b : nullptr);
  } // A89
  {
    std::optional<Provider> caller(p);
    auto alias = *caller;
    caller.reset();
    assessment_one(alias, .5, 1, defaults, b.exists ? &b : nullptr);
  } // A90
  {
    auto l = defaults;
    l.reaction_operations = 0;
    l.division_quotients = 0;
    assessment_one(p, .5, 1, l, b.exists && b.used[19] == 0 ? &b : nullptr,
                   b.exists && b.used[19] > 0 ? std::optional<std::size_t>{19}
                                              : std::nullopt);
  } // A91
  {
    auto l = defaults;
    l.phase.sectors = 0;
    l.phase.bodies = 0;
    assessment_one(p, .5, 1, l, nullptr,
                   b.exists && b.used[7] > 0 ? std::optional<std::size_t>{7}
                                             : std::nullopt);
  } // A92 original sectors/timing before body
  if (b.exists)
    for (const std::size_t i : {20U, 21U, 19U, 9U}) {
      auto l = defaults;
      set_limit(l, i, b.used[i]);
      assessment_one(p, .5, 1, l, &b);
    } // A93--96
}
struct DivisionFixture {
  Scalar numerator, denominator;
  DivisionLimits limits;
};
auto point(double x) -> Scalar {
  return {x, x, true};
}
auto division_fixture(std::size_t slot) -> DivisionFixture {
  DivisionFixture f{point(1), {.9375, 1, true}, {}};
  switch (slot) {
    case 1: f.numerator = point(0); break;
    case 2: break;
    case 3: f.numerator = point(-1); break;
    case 4: f.numerator = {-1, 1, true}; break;
    case 5: f.numerator = {.25, .5, true}; break;
    case 6: f.numerator = {-.5, -.25, true}; break;
    case 7: f.numerator = point(8); break;
    case 8: f.numerator = point(-8); break;
    case 9: f.denominator = point(std::nextafter(0., 1.)); break;
    case 10:
      f.numerator = {-2, -1, true};
      f.denominator = point(1);
      break;
    case 11:
      f.numerator = {-2, -1, true};
      f.denominator = point(.9375);
      break;
    case 12: f.numerator = {-2, -1, true}; break;
    case 13: f.denominator = point(0); break;
    case 14: f.denominator = {-1, -.9375, true}; break;
    case 15: f.denominator = {-.9375, .9375, true}; break;
    case 16: f.denominator = {1, .9375, true}; break;
    case 17: f.numerator = {1, -1, true}; break;
    case 18:
      f.numerator = point(std::numeric_limits<double>::quiet_NaN());
      break;
    case 19:
      f.denominator = point(std::numeric_limits<double>::infinity());
      break;
    case 20: f.numerator.supported = false; break;
    case 21:
      f.denominator = point(.9375);
      f.denominator.supported = false;
      break;
    case 22:
    case 23:
    case 24:
      f.numerator = {-2, -1, true};
      f.limits.quotients = slot == 22 ? 0 : slot == 23 ? 3 : 5;
      break;
    case 25:
      f.numerator = point(32);
      f.denominator = point(1);
      break;
    case 26:
      f.numerator =
          point(std::nextafter(32., std::numeric_limits<double>::infinity()));
      f.denominator = point(1);
      break;
    case 27:
      f.numerator = point(.5);
      f.denominator = point(1);
      break;
    case 28:
      f.numerator = point(.5);
      f.denominator =
          point(std::nextafter(1., std::numeric_limits<double>::infinity()));
      break;
    case 29:
    case 30:
    case 31:
      f.numerator = {-2, -1, true};
      f.limits.folds = slot == 29 ? 0 : slot == 30 ? 5 : 7;
      break;
    case 32:
      f.numerator = point(0);
      f.denominator = point(std::nextafter(0., 1.));
      break;
    default: throw std::runtime_error("Unregistered raw division slot");
  }
  return f;
}
[[gnu::noinline]] void division_roster() {
  for (std::size_t slot = 1; slot <= 32; ++slot) {
    SavedEnvironment restore;
    const auto f = division_fixture(slot);
    charge(totals.math, 32);
    const auto d = detail::boarding_route_intermediate_load01_division_math(
        f.numerator, f.denominator, f.limits);
    check(!decltype(d)::source_qualified && !decltype(d)::body_qualified &&
              !decltype(d)::contact_qualified && !decltype(d)::load_qualified &&
              !decltype(d)::support_qualified &&
              !decltype(d)::world_qualified && !decltype(d)::actor_qualified &&
              !decltype(d)::route_qualified,
          "Raw quotient arithmetic never mints source, contact or load "
          "authority");
    aggregate(18, d.work.quotients);
    aggregate(19, d.work.folds);
    aggregate(20, d.work.validation_guards);
    check(d.work.quotients <= f.limits.quotients &&
              d.work.folds <= f.limits.folds && d.work.validation_guards <= 3 &&
              mask_count(d.quotient_evaluated) == d.work.quotients &&
              mask_count(d.fold_evaluated) == d.work.folds &&
              mask_count(d.input_validation_evaluated) ==
                  d.work.validation_guards,
          "Division masks preserve actual charged prefix");
    if (d.complete) {
      check(d.arithmetic_supported && d.input_validated &&
                d.work.quotients == 4 && d.work.folds == 6 && finite(d.bound),
            "Only allfour endpoints andsix folds publish quotient hull");
      const auto nlo = static_cast<long double>(f.numerator.lower),
                 nhi = static_cast<long double>(f.numerator.upper),
                 dlo = static_cast<long double>(f.denominator.lower),
                 dhi = static_cast<long double>(f.denominator.upper);
      const std::array q{nlo / dlo, nlo / dhi, nhi / dlo, nhi / dhi};
      for (std::size_t i = 0; i < 4; ++i)
        check(enclosed(d.quotients[i], q[i]) && enclosed(d.bound, q[i]),
              "All independently computed endpoint quotients retained and hull "
              "encloses each");
      check(enclosed(d.bound, *std::min_element(q.begin(), q.end())) &&
                enclosed(d.bound, *std::max_element(q.begin(), q.end())),
            "Negative numerator finalupper winner and crossing signs included");
    }
    if (slot <= 8 || (slot >= 10 && slot <= 12) || slot == 27 || slot == 32)
      check(d.complete, "Analytically valid bounded literal division completes "
                        "arithmetic only");
    if ((slot >= 13 && slot <= 21) || slot == 26 || slot == 28)
      check(!d.complete && !d.input_validated && d.work.quotients == 0 &&
                d.work.folds == 0 && d.condition == Condition::division_input,
            "Malformed/unsupported input refuses before quotient authority");
    if (slot == 9)
      check(d.input_validated && !d.complete && d.work.quotients == 1 &&
                d.work.folds == 0 &&
                d.condition == Condition::unsupported_arithmetic,
            "Valid tinypositive denominator overflow is charged unsupported "
            "arithmetic");
    if (slot == 25)
      check(d.input_validated && d.condition != Condition::division_input,
            "Closed numerator32 is input-valid even if outward result exceeds "
            "workspace");
    if (slot == 22)
      check(d.input_validated && d.work.quotients == 0 &&
                d.condition == Condition::division_quotient_capacity,
            "Zero quotient cap does no quotient operation");
    if (slot == 23)
      check(d.work.quotients == 3 && d.work.folds == 4 &&
                !d.quotient_evaluated[3] &&
                d.condition == Condition::division_quotient_capacity,
            "Last endpoint NOTRUN with exactthree quotient cap");
    if (slot == 24 || slot == 31)
      check(!d.complete && d.work.validation_guards == 0 &&
                d.work.quotients == 0 && d.work.folds == 0 &&
                d.condition == Condition::invalid_limits,
            "Raised individual raw limit refuses before guards or math");
    if (slot == 29)
      check(d.work.quotients == 2 && d.work.folds == 0 &&
                d.condition == Condition::division_fold_capacity,
            "First fold cap reached afterq0 andq1, no partial hull");
    if (slot == 30)
      check(d.work.quotients == 4 && d.work.folds == 5 &&
                !d.fold_evaluated[2][1] &&
                d.condition == Condition::division_fold_capacity,
            "Finalupper fold cannot use uncharged result");
  }
}
[[gnu::noinline]] void final_accounting() {
  constexpr std::array<std::uint64_t, 21> maximum{
      196512,  393024, 196512,  589536,  1179072, 1728,   49324512,
      6091872, 393024, 3144192, 786048,  1572096, 786048, 786048,
      1179072, 589536, 1572096, 2358144, 128,     192,    96};
  for (std::size_t i = 0; i < maximum.size(); ++i)
    check(totals.work[i] <= maximum[i],
          "Literal fixed roster has finite aggregate work");
  check(totals.creators == 1 && totals.consumers <= 96 &&
            totals.phase_calls <= 196512 && totals.math == 32 &&
            totals.oracles <= 8,
        "One creator,96 consumer slots,32 single-seam literals andeight "
        "FIRST-only poses");
  std::cout << "VALIDATION creators=" << totals.creators
            << " consumers=" << totals.consumers
            << " phase_calls=" << totals.phase_calls << " math=" << totals.math
            << " poses=" << totals.oracles << " checks=" << checks
            << " failures=" << failures << " work=";
  for (auto n : totals.work) {
    std::cout << n << ',';
  }
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
      first_one(*provider, 1, &summary.whole[0]);
      first_one(*provider, 2, &summary.whole[1]);
      for (std::size_t slot = 3; slot <= 10; ++slot)
        first_one(*provider, slot, nullptr);
      if (summary.whole[0].exists && summary.whole[1].exists)
        check(summary.whole[0].hash == summary.whole[1].hash,
              "Forward/reverse retain same canonical closed geometry, source "
              "causes and work");
      ordinary_roster(*provider);
      lowered_roster(*provider, summary);
      remaining_roster(*provider, summary);
    } else
      std::cout << "FIRST_LOAD01 NOTRUN source_refused\n" << std::flush;
    division_roster();
    final_accounting();
    check(!out.truncated && !err.truncated && std::cout.good() &&
              std::cerr.good() && bytes <= 16384,
          "Entire shared stdout/stderr stream remains untruncated");
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout.rdbuf(saved_out);
  std::cerr.rdbuf(saved_err);
  return failures == 0 ? 0 : 1;
}
