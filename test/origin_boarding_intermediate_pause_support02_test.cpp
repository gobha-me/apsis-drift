#include "apsis_drift/origin_boarding_intermediate_pause_support02.hpp"
#include "origin_boarding_intermediate_pause_support02_internal.hpp"
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
using Diagnostic = BoardingIntermediatePauseSupport02Diagnostic;
using State = BoardingIntermediatePauseSupport02State;
using Condition = BoardingIntermediatePauseSupport02Condition;
using Scalar = BoardingFootSiteScalarBounds;
using Limits = detail::BoardingIntermediatePauseSupport02Limits;
using MathLimits =
    detail::BoardingIntermediatePauseSupport02AllocationMathLimits;
using MathInput = detail::BoardingIntermediatePauseSupport02AllocationMathInput;
using Access = detail::BoardingIntermediatePauseSupportAccess;
std::size_t checks{};
int failures{};
bool all_unsafe_available{true};
void check(bool yes, std::string_view why) {
  ++checks;
  if (!yes) {
    ++failures;
    std::cerr << "FAIL: " << why << '\n';
  }
}
struct Totals {
  std::size_t creators{}, consumers{}, math{}, oracles{}, child_calls{};
  std::array<std::uint64_t, 8> old{};
  std::array<std::size_t, 6> stages{}, raw{};
  std::size_t raw_guards{};
} totals;
void charge(std::size_t& count, std::size_t limit) {
  if (count >= limit) throw std::runtime_error("Registered roster exhausted");
  ++count;
}
struct StreamCounter : std::streambuf {
  std::streambuf* target;
  std::size_t& bytes;
  bool truncated{};
  StreamCounter(std::streambuf* t, std::size_t& b) : target(t), bytes(b) {}
  auto overflow(int_type ch) -> int_type override {
    if (traits_type::eq_int_type(ch, traits_type::eof()))
      return traits_type::not_eof(ch);
    if (bytes >= 16384) {
      truncated = true;
      return traits_type::eof();
    }
    ++bytes;
    return target->sputc(traits_type::to_char_type(ch));
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
      const std::array r{FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO};
      available = std::fesetround(r[mode]) == 0;
    } else {
#if defined(__SSE2__) && defined(__x86_64__)
      const auto x = mode == 3   ? control_ | (1U << 15)
                     : mode == 4 ? control_ | (1U << 6)
                                 : (control_ & ~(3U << 13)) | (1U << 13);
      _mm_setcsr(x);
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
// This independent, constant-state oracle expands the two-sphere knee
// construction in three dimensions and all fifteen original mass points.
// It executes only after each of the two FIRST producer calls has returned.
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
  Point com{};
  std::array<Point, 2> soles{};
};
[[gnu::noinline]] auto body_oracle() -> Oracle {
  Oracle out;
  auto& p = out.points;
  const auto wide = [](double x) { return static_cast<long double>(x); };
  p[0] = {wide(.16) + wide(.16) + wide(-.0075),
          wide(.847) + wide(.012) + wide(-.359),
          wide(-.55) + wide(-.17) + wide(.19)};
  const auto root = yaw(wide(.125)), upper = multiply(root, pitch(wide(-.30)));
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
  }
  out.com = scale(out.com, 1.L / 1200);
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
constexpr std::array<std::size_t Limits::*, 5> members{
    &Limits::sole_extrema, &Limits::source_coordinates,
    &Limits::intersection_operations, &Limits::midpoint_operations,
    &Limits::allocation_operations};
constexpr std::array<std::size_t MathLimits::*, 5> math_members{
    &MathLimits::sole_extrema, &MathLimits::source_coordinates,
    &MathLimits::intersection_operations, &MathLimits::midpoint_operations,
    &MathLimits::allocation_operations};
constexpr std::array<Condition, 5> capacity{
    Condition::sole_extrema_capacity, Condition::source_coordinate_capacity,
    Condition::intersection_capacity, Condition::midpoint_capacity,
    Condition::allocation_capacity};
template <class Work> auto stages(const Work& w) -> std::array<std::size_t, 5> {
  return {w.sole_extrema, w.source_coordinates, w.intersection_operations,
          w.midpoint_operations, w.allocation_operations};
}
auto old_work(const Diagnostic& d) -> std::array<std::uint64_t, 9> {
  const auto& w = d.work;
  return {w.phase.graphs,        w.phase.legs,   w.phase.bodies,
          w.phase.sectors,       w.phase.timing, w.projection_guards,
          w.pressure_candidates, w.disk_edges,   d.output_bytes};
}
void set_old(Limits& l, std::size_t i, std::uint64_t x) {
  switch (i) {
    case 0: l.phase.graphs = x; break;
    case 1: l.phase.legs = x; break;
    case 2: l.phase.bodies = x; break;
    case 3: l.phase.sectors = x; break;
    case 4: l.phase.timing = x; break;
    case 5: l.projection_guards = x; break;
    case 6: l.pressure_candidates = x; break;
    case 7: l.disk_edges = x; break;
    default: l.output_bytes = x; break;
  }
}
struct Hash {
  std::uint64_t value{1469598103934665603ULL};
  void add(std::uint64_t x) { value = (value ^ x) * 1099511628211ULL; }
  void scalar(double x) { add(std::bit_cast<std::uint64_t>(x == 0 ? 0. : x)); }
  void bound(Scalar x) {
    scalar(x.lower);
    scalar(x.upper);
    add(x.supported);
  }
  void text(std::string_view x) {
    add(x.size());
    for (const unsigned char c : x)
      add(c);
  }
};
template <class T> void downstream_authority(const T&) {
  check(!T::self_qualified && !T::material_qualified && !T::world_qualified &&
            !T::route_qualified && !T::seat_qualified && !T::actor_qualified &&
            !T::save_qualified && !T::dynamics_qualified &&
            !T::first_flight_qualified,
        "Static selected contact never grants route or physical authority");
}
auto semantic_hash(const Diagnostic& d) -> std::uint64_t {
  Hash h;
  h.add(d.version);
  h.add(static_cast<unsigned>(d.state));
  h.add(static_cast<unsigned>(d.stop_condition));
  h.add(d.arithmetic_supported);
  h.add(d.kinematic_complete);
  h.add(d.constant_state);
  h.add(d.projection_complete);
  h.add(d.intersection_complete);
  h.add(d.nominal_equilibrium);
  h.add(d.finite_contact_supported);
  h.add(d.nominal_load_supported);
  h.add(d.complete);
  h.scalar(d.duration_seconds);
  for (const auto x : d.reactions)
    h.scalar(x);
  for (const auto x : d.com_xz)
    h.bound(x);
  for (const auto& a : d.boot_centers_xz)
    for (const auto x : a)
      h.bound(x);
  for (const auto* a : {&d.port_sole_lower_xz, &d.port_sole_upper_xz,
                        &d.port_source_lower_xz, &d.port_source_upper_xz,
                        &d.intersection_lower_xz, &d.intersection_upper_xz})
    for (const auto x : *a)
      h.bound(x);
  for (const auto& a : d.pressure_xz)
    for (const auto x : a)
      h.bound(x);
  for (const auto b : d.definition_evaluated)
    h.add(b);
  for (const auto b : d.projected_carrier_complete)
    h.add(b);
  for (const auto& a : d.source_coordinate_evaluated)
    for (const auto b : a)
      h.add(b);
  for (const auto& a : d.sole_extrema_evaluated)
    for (const auto b : a)
      h.add(b);
  for (const auto& a : d.intersection_evaluated)
    for (const auto b : a)
      h.add(b);
  for (const auto& a : d.midpoint_evaluated)
    for (const auto b : a)
      h.add(b);
  for (const auto& a : d.allocation_evaluated)
    for (const auto b : a)
      h.add(b);
  for (const auto b : d.pressure_evaluated)
    h.add(b);
  for (std::size_t side = 0; side < 2; ++side) {
    h.scalar(d.source_planes[side]);
    const auto* q = Access::partition(d.source, side);
    if (q) {
      h.text(d.source.summary()->sources[side].name);
      for (const auto& k : d.source.summary()->sources[side].keys) {
        h.add(static_cast<unsigned>(k.buffer));
        h.add(k.group);
        h.add(k.triangle);
      }
    }
    const auto& s = d.sites[side];
    h.add(s.loaded);
    h.add(s.plane_identity);
    h.add(s.evaluated);
    h.add(s.complete);
    h.add(static_cast<unsigned>(s.sole_status));
    h.add(static_cast<unsigned>(s.source_status));
    for (std::size_t e = 0; e < 4; ++e) {
      h.add(s.sole_evaluated[e]);
      h.add(s.source_evaluated[e]);
      for (const auto* edge : {&s.sole_edges[e], &s.source_edges[e]}) {
        for (const auto b : {edge->signed_side, edge->edge_length_squared,
                             edge->squared_margin_gap})
          h.bound({b.lower, b.upper, b.supported});
        h.add(edge->disk_contained);
      }
    }
  }
  for (const auto x : old_work(d))
    h.add(x);
  for (const auto x : stages(d.work))
    h.add(x);
  h.add(d.work.definition_guards);
  h.add(d.work.phase_calls);
  h.add(d.first_refusal.has_value());
  if (d.first_refusal) {
    const auto& f = *d.first_refusal;
    h.add(static_cast<unsigned>(f.condition));
    h.add(f.side.value_or(3));
    h.add(f.axis.value_or(3));
    h.add(f.vertex.value_or(5));
    h.add(f.edge.value_or(5));
    h.add(f.operation.value_or(7));
    h.add(f.source_edge);
    h.bound({f.limiting_bound.lower, f.limiting_bound.upper,
             f.limiting_bound.supported});
    h.add(static_cast<unsigned>(f.phase.condition));
    h.scalar(f.phase.first);
    h.scalar(f.phase.last);
    h.add(f.phase.side.value_or(3));
  }
  return h.value;
}
struct Baseline {
  std::array<std::uint64_t, 9> old{};
  std::array<std::size_t, 5> stage{};
  std::uint64_t hash{};
  std::size_t definitions{}, phase_calls{};
  State state{};
  bool exists{};
};
struct Summary {
  std::array<Baseline, 2> first;
};
static_assert(sizeof(Summary) < 512);
auto summarize(const Diagnostic& d) -> Baseline {
  return {old_work(d),
          stages(d.work),
          semantic_hash(d),
          d.work.definition_guards,
          d.work.phase_calls,
          d.state,
          true};
}
template <class T, class L> void arithmetic_accounting(const T& d, const L& l) {
  const auto w = stages(d.work);
  for (std::size_t i = 0; i < 5; ++i) {
    if constexpr (std::is_same_v<L, Limits>)
      check(w[i] <= l.*members[i], "Actual work respects each lowered cap");
    else
      check(w[i] <= l.*math_members[i], "Raw work respects each lowered cap");
  }
  check(d.work.pressure_candidates <= l.pressure_candidates,
        "Staged candidate obligations respect actual cap");
  if (d.intersection_complete)
    for (std::size_t a = 0; a < 2; ++a)
      check(
          finite(d.intersection_lower_xz[a]) &&
              finite(d.intersection_upper_xz[a]) &&
              d.intersection_lower_xz[a].upper <
                  d.intersection_upper_xz[a].lower,
          "Only strictly nonempty supported intersections qualify this recipe");
  if (d.nominal_equilibrium) {
    check(d.intersection_complete && d.arithmetic_supported &&
              d.work.pressure_candidates == 2,
          "Moment recipe needs genuine complete clip and both candidates");
    for (const auto& p : d.pressure_xz)
      for (const auto x : p)
        check(finite(x), "Complete pressure operands are supported");
  }
}
void accounting(const Diagnostic& d, const Limits& l) {
  arithmetic_accounting(d, l);
  downstream_authority(d);
  check(d.version == 2 && d.duration_seconds == 2 &&
            d.reactions == std::array<double, 2>{.0625, .9375},
        "One immutable two-second hypothesis at fixed dyadic reactions");
  check(d.work.phase_calls <= 1 && d.work.definition_guards <= 31 &&
            d.output_bytes <= l.output_bytes && d.output_bytes <= 4096,
        "One graph, fixed definitions and bounded compact output");
  const auto ow = old_work(d);
  const std::array<std::uint64_t, 9> caps{
      l.phase.graphs,        l.phase.legs,   l.phase.bodies,
      l.phase.sectors,       l.phase.timing, l.projection_guards,
      l.pressure_candidates, l.disk_edges,   l.output_bytes};
  for (std::size_t i = 0; i < ow.size(); ++i)
    check(ow[i] <= caps[i], "Original inherited work remains bounded");
  const auto definitions = static_cast<std::size_t>(std::count(
      d.definition_evaluated.begin(), d.definition_evaluated.end(), true));
  check(definitions <= d.work.definition_guards,
        "Definition masks represent only genuinely charged records");
  if (d.projection_complete) {
    check(
        d.kinematic_complete && d.constant_state &&
            std::all_of(d.projected_carrier_complete.begin(),
                        d.projected_carrier_complete.end(),
                        [](bool b) { return b; }),
        "Projection uses complete authentic constant graph and all11 carriers");
    for (std::size_t side = 0; side < 2; ++side) {
      const auto* summary = d.source.summary();
      if (!summary) continue;
      const auto& old = summary->sources[side];
      const auto& s = d.sources[side];
      if (!s.name.empty()) {
        check(
            s.name == old.name && s.plane == old.plane &&
                d.source_planes[side] == old.plane,
            "Copied descriptors retain genuine names and exact source planes");
        for (std::size_t k = 0; k < 2; ++k)
          check(s.keys[k].buffer == old.keys[k].buffer &&
                    s.keys[k].group == old.keys[k].group &&
                    s.keys[k].triangle == old.keys[k].triangle,
                "Finite source keys retain original namespace and triangle "
                "ordinals");
      }
    }
  }
  std::size_t edges{};
  for (const auto& s : d.sites) {
    edges += static_cast<std::size_t>(
        std::count(s.sole_evaluated.begin(), s.sole_evaluated.end(), true));
    edges += static_cast<std::size_t>(
        std::count(s.source_evaluated.begin(), s.source_evaluated.end(), true));
    for (std::size_t e = 0; e < 4; ++e)
      for (const auto* edge : {&s.sole_edges[e], &s.source_edges[e]})
        check(!edge->disk_contained || (edge->signed_side.supported &&
                                        edge->squared_margin_gap.supported &&
                                        edge->signed_side.lower > 0 &&
                                        edge->squared_margin_gap.lower >= 0),
              "Original strict oriented side and unchanged margin authorize "
              "disks");
    check(
        !s.complete ||
            (s.plane_identity && s.loaded &&
             s.sole_status == BoardingIntermediatePauseDiskStatus::contained &&
             s.source_status == BoardingIntermediatePauseDiskStatus::contained),
        "Finite site support needs both original four-edge disks");
  }
  check(edges <= d.work.disk_edges, "All retained edge masks are charged");
  check(!d.complete ||
            (d.nominal_equilibrium && d.finite_contact_supported &&
             d.nominal_load_supported && d.stop_condition == Condition::none &&
             !d.first_refusal && edges == 16),
        "Selected positive support requires all16 genuine finite edge "
        "obligations");
  totals.child_calls += d.work.phase_calls;
  for (std::size_t i = 0; i < 8; ++i)
    totals.old[i] += ow[i];
  for (std::size_t i = 0; i < 5; ++i)
    totals.stages[i] += stages(d.work)[i];
  totals.stages[5] += d.work.definition_guards;
}
void print(const Diagnostic& d) {
  std::cout << " version=" << d.version << " reverse=" << d.reverse
            << " state=" << static_cast<unsigned>(d.state)
            << " complete=" << d.complete << " flags=" << d.kinematic_complete
            << d.constant_state << d.projection_complete
            << d.intersection_complete << d.nominal_equilibrium
            << d.finite_contact_supported << d.nominal_load_supported
            << " stop=" << static_cast<unsigned>(d.stop_condition)
            << " output=" << d.output_bytes << " work=" << d.work.phase_calls;
  for (const auto x : old_work(d))
    std::cout << ',' << x;
  for (const auto x : stages(d.work))
    std::cout << ',' << x;
  std::cout << ',' << d.work.definition_guards << " defs=";
  for (const auto b : d.definition_evaluated)
    std::cout << b;
  std::cout << " carriers=";
  for (const auto b : d.projected_carrier_complete)
    std::cout << b;
  std::cout << " scalar_masks=";
  for (const auto& a : d.sole_extrema_evaluated)
    for (const auto b : a)
      std::cout << b;
  std::cout << '/';
  for (const auto& a : d.source_coordinate_evaluated)
    for (const auto b : a)
      std::cout << b;
  std::cout << '/';
  for (const auto& a : d.intersection_evaluated)
    for (const auto b : a)
      std::cout << b;
  std::cout << '/';
  for (const auto& a : d.midpoint_evaluated)
    for (const auto b : a)
      std::cout << b;
  std::cout << '/';
  for (const auto& a : d.allocation_evaluated)
    for (const auto b : a)
      std::cout << b;
  std::cout << '/';
  for (const auto b : d.pressure_evaluated)
    std::cout << b;
  std::cout << " COM=";
  for (const auto x : d.com_xz)
    std::cout << '[' << x.lower << ',' << x.upper << ',' << x.supported << ']';
  std::cout << " clip=";
  for (std::size_t a = 0; a < 2; ++a)
    for (const auto x :
         {d.intersection_lower_xz[a], d.intersection_upper_xz[a]})
      std::cout << '[' << x.lower << ',' << x.upper << ',' << x.supported
                << ']';
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& s = d.sites[side];
    const auto* source = d.source.summary();
    if (source)
      std::cout << " source" << side << '=' << source->sources[side].name << ':'
                << d.source_planes[side];
    std::cout << " site" << side << '=' << static_cast<unsigned>(s.sole_status)
              << ',' << static_cast<unsigned>(s.source_status) << " COP=";
    for (const auto x : d.pressure_xz[side])
      std::cout << '[' << x.lower << ',' << x.upper << ',' << x.supported
                << ']';
    std::cout << " edges=";
    for (const auto b : s.sole_evaluated)
      std::cout << b;
    for (const auto b : s.source_evaluated)
      std::cout << b;
  }
  if (d.first_refusal) {
    const auto& f = *d.first_refusal;
    std::cout << " refusal=" << static_cast<unsigned>(f.condition) << ','
              << f.side.value_or(3) << ',' << f.axis.value_or(3) << ','
              << f.vertex.value_or(5) << ',' << f.edge.value_or(5) << ','
              << f.operation.value_or(7) << ',' << f.source_edge << " bound=["
              << f.limiting_bound.lower << ',' << f.limiting_bound.upper << ']'
              << " phase=" << static_cast<unsigned>(f.phase.condition);
  }
  std::cout << '\n' << std::flush;
}
auto wide(RigidVector3 p) -> Point {
  return {p.x, p.y, p.z};
}
void edge_oracle(const BoardingFootSiteEdgeEvidence& e, Point a, Point b,
                 Point p) {
  if (!e.signed_side.supported || !e.edge_length_squared.supported ||
      !e.squared_margin_gap.supported)
    return;
  const auto dx = b[0] - a[0], dz = b[2] - a[2];
  const auto side = dz * (p[0] - a[0]) - dx * (p[2] - a[2]);
  const auto length2 = dx * dx + dz * dz;
  const auto required =
      static_cast<long double>(.020) + static_cast<long double>(.010);
  check(enclosed({e.signed_side.lower, e.signed_side.upper, true}, side) &&
            enclosed({e.edge_length_squared.lower, e.edge_length_squared.upper,
                      true},
                     length2) &&
            enclosed(
                {e.squared_margin_gap.lower, e.squared_margin_gap.upper, true},
                side * side - required * required * length2),
        "Independent original finite edge/disk equation is enclosed");
}
[[gnu::noinline]] void first_oracle(const Diagnostic& d) {
  if (!d.kinematic_complete || !d.constant_state || !d.projection_complete)
    return;
  charge(totals.oracles, 2);
  const auto o = body_oracle();
  for (std::size_t side = 0; side < 2; ++side) {
    const auto base = side == 0 ? 4U : 11U;
    const auto t = subtract(o.points[base + 1], o.points[base]);
    const auto s = subtract(o.points[base + 2], o.points[base + 1]);
    const auto l1 = static_cast<long double>(.47285),
               l2 = static_cast<long double>(.47478);
    check(std::abs(dot(t, t) - l1 * l1) <
                  64 * std::numeric_limits<long double>::epsilon() &&
              std::abs(dot(s, s) - l2 * l2) <
                  64 * std::numeric_limits<long double>::epsilon(),
          "Independent unfactored3D knees preserve both rods");
    for (std::size_t axis = 0; axis < 2; ++axis) {
      const auto a = axis == 0 ? 0U : 2U;
      check(enclosed(d.com_xz[axis], o.com[a]) &&
                enclosed(d.boot_centers_xz[side][axis], o.points[base + 3][a]),
            "All18 original points/15 masses corroborate COM and full boots");
    }
  }
  const auto* q = Access::partition(d.source, 0);
  if (!q) return;
  Point port{}, star{};
  for (std::size_t axis = 0; axis < 2; ++axis) {
    const auto a = axis == 0 ? 0U : 2U;
    const auto half = static_cast<long double>(axis == 0 ? .06 : .14);
    const auto ls = o.soles[0][a] - half, us = o.soles[0][a] + half;
    auto lq = wide(q->perimeter_metres[0])[a], uq = lq;
    for (const auto p : q->perimeter_metres) {
      lq = std::min(lq, wide(p)[a]);
      uq = std::max(uq, wide(p)[a]);
    }
    if (d.sole_extrema_evaluated[axis][0] && finite(d.port_sole_lower_xz[axis]))
      check(enclosed(d.port_sole_lower_xz[axis], ls),
            "Full unshrunk sole lower extent enclosed");
    if (d.sole_extrema_evaluated[axis][1] && finite(d.port_sole_upper_xz[axis]))
      check(enclosed(d.port_sole_upper_xz[axis], us),
            "Full unshrunk sole upper extent enclosed");
    if (finite(d.port_source_lower_xz[axis]) &&
        finite(d.port_source_upper_xz[axis]))
      check(enclosed(d.port_source_lower_xz[axis], lq) &&
                enclosed(d.port_source_upper_xz[axis], uq),
            "All four authentic source coordinates determine clip extents");
    const auto lower = std::max(ls, lq), upper = std::min(us, uq);
    if (!d.intersection_complete) continue;
    check(lower < upper && enclosed(d.intersection_lower_xz[axis], lower) &&
              enclosed(d.intersection_upper_xz[axis], upper),
          "Independent strictly nonempty intersection enclosed");
    port[a] = .5L * (lower + upper);
    star[a] = (o.com[a] - .0625L * port[a]) / .9375L;
    if (d.pressure_evaluated[0])
      check(enclosed(d.pressure_xz[0][axis], port[a]),
            "Independent port clip midpoint enclosed");
    if (d.pressure_evaluated[1])
      check(enclosed(d.pressure_xz[1][axis], star[a]),
            "Independent fixed-share star moment solve enclosed");
    if (d.nominal_equilibrium)
      check(std::abs(.0625L * port[a] + .9375L * star[a] - o.com[a]) <=
                64 * std::numeric_limits<long double>::epsilon(),
            "Exact complementary positive shares preserve nominal moments");
  }
  if (!d.nominal_equilibrium) return;
  constexpr std::array signs{std::pair{1.L, -1.L}, std::pair{-1.L, -1.L},
                             std::pair{-1.L, 1.L}, std::pair{1.L, 1.L}};
  for (std::size_t side = 0; side < 2; ++side) {
    const auto* source = Access::partition(d.source, side);
    if (!source) continue;
    std::array<Point, 4> sole;
    auto pressure = side == 0 ? port : star;
    pressure[1] = o.soles[side][1];
    for (std::size_t e = 0; e < 4; ++e)
      sole[e] =
          add(o.soles[side], {signs[e].first * static_cast<long double>(.06), 0,
                              signs[e].second * static_cast<long double>(.14)});
    for (std::size_t e = 0; e < 4; ++e) {
      if (d.sites[side].sole_evaluated[e])
        edge_oracle(d.sites[side].sole_edges[e], sole[e], sole[(e + 1) % 4],
                    pressure);
      if (d.sites[side].source_evaluated[e])
        edge_oracle(d.sites[side].source_edges[e],
                    wide(source->perimeter_metres[e]),
                    wide(source->perimeter_metres[(e + 1) % 4]), pressure);
    }
  }
}
[[gnu::noinline]] void create_source(const NativeCraftBinding& binding,
                                     const OriginBoardingBootSupport& boots,
                                     std::optional<Provider>& provider) {
  charge(totals.creators, 1);
  auto result = make_origin_boarding_intermediate_pause_support(binding, boots);
  std::cout << "FIRST_SUPPORT02_SOURCE admitted=" << result.has_value();
  if (!result)
    std::cout << " error=" << result.error();
  else if (const auto* s = result->summary())
    std::cout << " complete=" << s->complete
              << " bytes=" << s->actual_source_bytes
              << " condition=" << static_cast<unsigned>(s->condition);
  std::cout << '\n' << std::flush;
  check(result.has_value(), "Observed genuine public source remains admitted");
  if (result) {
    check(Access::valid(*result),
          "Genuine public creator alone issues source capability");
    const auto* s = result->summary();
    check(s && s->complete &&
              s->condition == BoardingIntermediatePauseCondition::none &&
              s->actual_source_bytes == 4096,
          "Observed source admission retains its complete fixed4096 arena");
    provider.emplace(std::move(*result));
  }
}
// Mandatory regressions follow the initially unknown frozen FIRST observation.
// This helper runs after the producer returns; its frame never overlaps graph.
[[gnu::noinline]] void observed_support(const Diagnostic& d) {
  check(
      d.version == 2 && d.state == State::supported && d.complete &&
          d.arithmetic_supported && d.kinematic_complete && d.constant_state &&
          d.projection_complete && d.intersection_complete &&
          d.nominal_equilibrium && d.finite_contact_supported &&
          d.nominal_load_supported && !d.first_refusal &&
          d.stop_condition == Condition::none && d.duration_seconds == 2 &&
          d.reactions == std::array<double, 2>{.0625, .9375},
      "Observed source-backed two-second pause supports the selected1/16 load");
  check(d.work.phase_calls == 1 && d.work.definition_guards == 31 &&
            old_work(d) ==
                std::array<std::uint64_t, 9>{1, 2, 1, 3, 6, 251, 2, 16, 2496} &&
            stages(d.work) == std::array<std::size_t, 5>{4, 8, 4, 4, 6} &&
            d.output_bytes == 2496,
        "Observed fresh graph, complete projection and allocation retain exact "
        "work");
  const auto all = [](const auto& a) {
    return std::all_of(a.begin(), a.end(), [](bool b) { return b; });
  };
  check(all(d.definition_evaluated) && all(d.projected_carrier_complete) &&
            all(d.pressure_evaluated),
        "Observed success retains all31 definitions,11 carriers and two "
        "candidates");
  for (const auto& a : d.source_coordinate_evaluated)
    check(all(a), "Observed source clip reads all eight coordinates");
  for (const auto* m : {&d.sole_extrema_evaluated, &d.intersection_evaluated,
                        &d.midpoint_evaluated})
    for (const auto& a : *m)
      check(all(a), "Observed fullsole, clip and midpoint masks are complete");
  for (const auto& a : d.allocation_evaluated)
    check(all(a), "Observed star solve retains all six "
                  "multiply/subtract/divide operations");
  const auto exact = [](Scalar b, double lower, double upper) {
    return b.supported && b.lower == lower && b.upper == upper;
  };
  check(exact(d.com_xz[0], .27107849490809005, .27107849490809199) &&
            exact(d.com_xz[1], -.75275030637110374, -.75275030637109841),
        "Observed independent fullbody COM enclosure remains reproducible");
  check(exact(d.intersection_lower_xz[0], .11999999999999995,
              .12000000000000004) &&
            exact(d.intersection_upper_xz[0], .23999999999999994,
                  .24000000000000005) &&
            exact(d.intersection_lower_xz[1], -1.28, -1.2799999999999996) &&
            exact(d.intersection_upper_xz[1], -1.18316, -1.18316),
        "Observed fullsole/source intersection keeps strict finite X and Z "
        "width");
  check(
      exact(d.pressure_xz[0][0], .17999999999999988, .1800000000000001) &&
          exact(d.pressure_xz[0][1], -1.2315800000000006,
                -1.2315799999999992) &&
          exact(d.pressure_xz[1][0], .27715039456862928, .27715039456863161) &&
          exact(d.pressure_xz[1][1], -.7208283267958443, -.72082832679583797),
      "Observed constructive port midpoint and star moment-solve pressures "
      "persist");
  const auto& port = d.sources[0];
  const auto& star = d.sources[1];
  check(port.name == "CRAFT | pilot transition intermediate step" &&
            port.plane == static_cast<double>(-230000) * 1e-6 &&
            port.keys[0].buffer == LowerCockpitContactBuffer::halo &&
            port.keys[1].buffer == LowerCockpitContactBuffer::halo &&
            port.keys[0].group == 0 && port.keys[1].group == 0 &&
            port.keys[0].triangle == 662 && port.keys[1].triangle == 663 &&
            star.name == "CABIN | cockpit transition step" &&
            star.plane == static_cast<double>(-160000) * 1e-6 &&
            star.keys[0].buffer == LowerCockpitContactBuffer::original &&
            star.keys[1].buffer == LowerCockpitContactBuffer::original &&
            star.keys[0].group == 0 && star.keys[1].group == 0 &&
            star.keys[0].triangle == 62119 && star.keys[1].triangle == 62120,
        "Observed nonzero support belongs to the exact genuine HALO and "
        "original quads");
  for (const auto& site : d.sites) {
    check(site.loaded && site.plane_identity && site.evaluated &&
              site.complete &&
              site.sole_status ==
                  BoardingIntermediatePauseDiskStatus::contained &&
              site.source_status ==
                  BoardingIntermediatePauseDiskStatus::contained &&
              all(site.sole_evaluated) && all(site.source_evaluated),
          "Observed two loaded sites retain complete original four-edge disks");
    for (std::size_t e = 0; e < 4; ++e)
      for (const auto* edge : {&site.sole_edges[e], &site.source_edges[e]})
        check(
            edge->disk_contained && edge->signed_side.supported &&
                edge->squared_margin_gap.supported &&
                edge->signed_side.lower > 0 &&
                edge->squared_margin_gap.lower >= 0,
            "All16 observed finite margins satisfy unchanged strict admission");
  }
}
[[gnu::noinline]] void first_one(const Provider& p, bool reverse,
                                 Baseline& baseline) {
  charge(totals.consumers, 48);
  auto result = assess_origin_boarding_intermediate_pause_support02(p, reverse);
  std::cout << "FIRST_SUPPORT02_" << (reverse ? "A02" : "A01")
            << " admitted=" << result.has_value();
  if (!result) {
    std::cout << " error=" << result.error() << '\n' << std::flush;
    check(false, "Observed supported public assessment remains available");
    return;
  }
  print(*result);
  observed_support(*result);
  accounting(*result, Limits{});
  first_oracle(*result);
  baseline = summarize(*result);
}
// Direct producer calls belong here; no owning Expected-return wrapper exists.
[[gnu::noinline]] void assessment_one(
    const Provider& p, bool reverse, Limits l,
    const Baseline* expected = nullptr,
    std::optional<Condition> stop = std::nullopt, bool invalid = false,
    bool no_graph = false) {
  charge(totals.consumers, 48);
  auto result = detail::intermediate_pause_support02_bounded(p, reverse, l);
  if (invalid) {
    check(!result, "Overmaximum/fixed-output preflight refuses before graph");
    if (!result)
      check(!result.error().empty(),
            "Unexpected preflight retains precise error");
    return;
  }
  check(result.has_value(),
        "Valid lowered limits retain honest assessment evidence");
  if (!result) return;
  accounting(*result, l);
  if (expected)
    check(semantic_hash(*result) == expected->hash,
          "Exact reached caps replay fieldwise semantic evidence");
  if (stop)
    check(result->stop_condition == *stop,
          "Reached capacity retains exact terminal cause beside earlier disk "
          "finding");
  if (result->first_refusal &&
      result->stop_condition == Condition::phase_prerequisite) {
    const auto expected_phase =
        l.phase.graphs == 0 ? BoardingRouteFootPhaseCondition::graph_capacity
        : l.phase.legs == 0 ? BoardingRouteFootPhaseCondition::leg_capacity
        : l.phase.sectors == 0
            ? BoardingRouteFootPhaseCondition::sector_capacity
        : l.phase.timing == 0 ? BoardingRouteFootPhaseCondition::timing_capacity
        : l.phase.bodies == 0 ? BoardingRouteFootPhaseCondition::body_capacity
                              : result->first_refusal->phase.condition;
    check(
        result->first_refusal->phase.condition == expected_phase,
        "Actual original sector/timing-before-body refusal order is retained");
  }
  if (no_graph)
    check(result->work.phase_calls == 0 && result->work.phase.graphs == 0 &&
              stages(result->work) == std::array<std::size_t, 5>{} &&
              result->work.pressure_candidates == 0 &&
              result->work.disk_edges == 0,
          "Unsafe/moved preflight cannot consume default geometry or pressure");
}
[[gnu::noinline]] void immutable_request() {
  const auto original = detail::boarding_route_intermediate_step02_controls(4);
  const auto selected = detail::intermediate_pause_support02_request();
  check(original.has_value() && selected.has_value(),
        "Immutable old and selected request inspectors exist");
  if (!original || !selected) return;
  const auto same_point = [](const auto& a, const auto& b) {
    for (std::size_t axis = 0; axis < 3; ++axis)
      if (a.coordinates[axis].count != b.coordinates[axis].count ||
          a.coordinates[axis].terms != b.coordinates[axis].terms)
        return false;
    return true;
  };
  for (std::size_t end = 0; end < 2; ++end) {
    check(same_point(original->root[end], selected->root[end]),
          "Root keeps all original ordered expression terms");
    for (std::size_t side = 0; side < 2; ++side)
      check(
          same_point(original->feet[side].sole[end],
                     selected->feet[side].sole[end]),
          "Both held soles keep all original source-derived expression terms");
  }
  for (std::size_t side = 0; side < 2; ++side)
    check(original->feet[side].yaw_half == selected->feet[side].yaw_half &&
              original->feet[side].swing_height_metres ==
                  selected->feet[side].swing_height_metres,
          "Flat sole yaw and zero swing remain immutable");
  check(
      original->root_yaw_half == selected->root_yaw_half &&
          original->torso_lean_half == selected->torso_lean_half &&
          original->seconds_per_parameter == selected->seconds_per_parameter &&
          selected->seconds_per_parameter == 2 &&
          original->port_reaction_fraction == std::array<double, 2>{.25, .25} &&
          selected->port_reaction_fraction ==
              std::array<double, 2>{.0625, .0625},
      "Only reaction endpoints change; all physical geometry and time remains "
      "old");
  static_assert(!std::is_default_constructible_v<
                detail::BoardingIntermediatePauseSupport02ProjectionToken>);
}
[[gnu::noinline]] void consumer_roster(const Provider& p,
                                       const Summary& summary) {
  const Limits defaults;
  const auto& b = summary.first[0];
  if (!b.exists) return;
  for (std::size_t reverse = 0; reverse < 2; ++reverse) {
    const auto& baseline = summary.first[reverse];
    if (!baseline.exists) continue;
    auto l = defaults;
    for (std::size_t i = 0; i < 5; ++i)
      l.*members[i] = baseline.stage[i];
    for (std::size_t i = 0; i < 9; ++i)
      set_old(l, i, baseline.old[i]);
    assessment_one(p, reverse != 0, l, &baseline); // A03/A04
  }
  for (std::size_t i = 0; i < 5; ++i) { // A05..A09 isolated exact
    auto l = defaults;
    l.*members[i] = b.stage[i];
    assessment_one(p, false, l, &b);
  }
  for (std::size_t i = 0; i < 5; ++i) { // A10..A14 isolated zero
    auto l = defaults;
    l.*members[i] = 0;
    assessment_one(p, false, l, b.stage[i] == 0 ? &b : nullptr,
                   b.stage[i] > 0 ? std::optional{capacity[i]} : std::nullopt);
  }
  for (std::size_t i = 0; i < 5;
       ++i) { // A15..A19 one-less, declared duplicate skip
    if (b.stage[i] <= 1) continue;
    auto l = defaults;
    l.*members[i] = b.stage[i] - 1;
    assessment_one(p, false, l, nullptr, capacity[i]);
  }
  for (std::size_t i = 0; i < 5; ++i) { // A20..A24 raised ceilings
    auto l = defaults;
    ++(l.*members[i]);
    assessment_one(p, false, l, nullptr, std::nullopt, true);
  }
  for (std::size_t i = 0; i < 9; ++i) { // A25..A33 inherited zero
    auto l = defaults;
    set_old(l, i, 0);
    const auto terminal = i == 5 ? std::optional{Condition::projection_capacity}
                          : i == 6 ? std::optional{Condition::pressure_capacity}
                          : i == 7 ? std::optional{Condition::edge_capacity}
                                   : std::nullopt;
    assessment_one(p, false, l, b.old[i] == 0 ? &b : nullptr,
                   b.old[i] > 0 ? terminal : std::nullopt, i == 8);
  }
  for (std::size_t mode = 0; mode < 6; ++mode) { // A34..A39
    Environment env(mode);
    all_unsafe_available = all_unsafe_available && env.available;
    if (env.available)
      assessment_one(p, false, defaults, nullptr,
                     Condition::unsupported_arithmetic, false, true);
  }
  { // A40/A41: moved handle and retained owner
    auto empty = p;
    auto owner = std::move(empty);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Empty handle contract.
    assessment_one(empty, false, defaults, nullptr, Condition::invalid_binding,
                   false, true);
    assessment_one(owner, false, defaults, &b);
  }
  { // A42: copied owner survives original caller scope
    std::optional<Provider> copy;
    {
      auto owner = p;
      copy.emplace(owner);
    }
    assessment_one(*copy, false, defaults, &b);
  }
  { // A43: no second creator or fictitious distinct-source admission
    auto alias = p;
    auto retained = std::move(alias);
    assessment_one(retained, false, defaults, &b);
  }
  // A44 is predeclared SKIPPED_DUPLICATE_A33: exactly ONE output cap.
  if (b.old[8] > 1) { // A45 fixed output one-less
    auto l = defaults;
    l.output_bytes = static_cast<std::size_t>(b.old[8] - 1);
    assessment_one(p, false, l, nullptr, std::nullopt, true);
  }
  {
    auto l = defaults;
    ++l.output_bytes;
    assessment_one(p, false, l, nullptr, std::nullopt, true);
  } // A46
  {
    auto l = defaults;
    l.sole_extrema = 0;
    l.source_coordinates = 0;
    assessment_one(p, false, l, b.stage[0] == 0 ? &b : nullptr,
                   b.stage[0] > 0
                       ? std::optional{Condition::sole_extrema_capacity}
                       : std::nullopt);
  } // A47
  {
    auto l = defaults;
    l.intersection_operations = 0;
    l.allocation_operations = 0;
    assessment_one(p, false, l, b.stage[2] == 0 ? &b : nullptr,
                   b.stage[2] > 0
                       ? std::optional{Condition::intersection_capacity}
                       : std::nullopt);
  } // A48
}
auto point(double x) -> Scalar {
  return {x, x, true};
}
using Pair = std::array<Scalar, 2>;
auto pair(double x, double z) -> Pair {
  return {point(x), point(z)};
}
struct Fixture {
  MathInput input;
  MathLimits limits;
};
auto fixture(std::size_t slot) -> Fixture {
  Fixture f;
  auto& in = f.input;
  in.com_xz = pair(0, 0);
  in.port_boot_center_xz = pair(0, 0);
  in.port_source_vertices_xz = {pair(1, -1), pair(-1, -1), pair(-1, 1),
                                pair(1, 1)};
  switch (slot) {
    case 1:
      in.port_source_vertices_xz = {pair(.03125, -.0625), pair(-.03125, -.0625),
                                    pair(-.03125, .0625), pair(.03125, .0625)};
      break;
    case 2:
      in.com_xz = pair(.25, .5);
      in.port_source_vertices_xz = {pair(.03125, 0), pair(0, 0), pair(0, .0625),
                                    pair(.03125, .0625)};
      break;
    case 3: in.com_xz = pair(2, -2); break;
    case 4:
      in.com_xz = pair(-.25, -.5);
      in.port_source_vertices_xz = {pair(0, -.0625), pair(-.03125, -.0625),
                                    pair(-.03125, 0), pair(0, 0)};
      break;
    case 5:
      in.port_source_vertices_xz = {pair(2, -1), pair(1, -1), pair(1, 1),
                                    pair(2, 1)};
      break;
    case 6:
      in.port_source_vertices_xz = {pair(.5, -1), pair(.06, -1), pair(.06, 1),
                                    pair(.5, 1)};
      break;
    case 7:
      in.port_source_vertices_xz[1][0] = {-.125, .125, true};
      in.port_source_vertices_xz[2][0] = {-.125, .125, true};
      break;
    case 8: in.port_source_vertices_xz[3][1] = point(2); break;
    case 9:
      in.com_xz[0] = point(std::numeric_limits<double>::quiet_NaN());
      break;
    case 10:
      in.port_source_vertices_xz[3][1] =
          point(std::numeric_limits<double>::infinity());
      break;
    case 11: in.com_xz[1] = {.5, -.5, true}; break;
    case 12:
      in.port_source_vertices_xz[3][0] =
          point(std::nextafter(8., std::numeric_limits<double>::infinity()));
      break;
    case 13: in.port_boot_center_xz[0].supported = false; break;
    case 14: f.limits.allocation_operations = 5; break;
    case 15:
      in.port_source_vertices_xz[3][1] = point(2);
      f.limits.source_coordinates = 7;
      break;
    case 16:
      in.com_xz = pair(8, 8);
      in.port_boot_center_xz = pair(8, 8);
      in.port_source_vertices_xz = {pair(8, 7.875), pair(7.875, 7.875),
                                    pair(7.875, 8), pair(8, 8)};
      break;
    case 17:
      in.com_xz = pair(-8, -8);
      in.port_boot_center_xz = pair(-8, -8);
      in.port_source_vertices_xz = {pair(-7.875, -8), pair(-8, -8),
                                    pair(-8, -7.875), pair(-7.875, -7.875)};
      break;
    case 18: f.limits.sole_extrema = 0; break;
    case 19: f.limits.source_coordinates = 0; break;
    case 20: f.limits.intersection_operations = 0; break;
    case 21: f.limits.midpoint_operations = 0; break;
    case 22: f.limits.allocation_operations = 0; break;
    case 23: f.limits.allocation_operations = 7; break;
    default: break;
  }
  return f;
}
template <class Math> void raw_oracle(const Math& d, const MathInput& in) {
  if (!d.nominal_equilibrium) return;
  for (std::size_t axis = 0; axis < 2; ++axis) {
    const auto half = static_cast<long double>(axis == 0 ? .06 : .14);
    const auto center =
        static_cast<long double>(in.port_boot_center_xz[axis].lower);
    auto lower = static_cast<long double>(
             in.port_source_vertices_xz[0][axis].lower),
         upper = lower;
    for (const auto& p : in.port_source_vertices_xz) {
      lower = std::min(lower, static_cast<long double>(p[axis].lower));
      upper = std::max(upper, static_cast<long double>(p[axis].upper));
    }
    lower = std::max(lower, center - half);
    upper = std::min(upper, center + half);
    const auto port = .5L * (lower + upper);
    const auto star =
        (static_cast<long double>(in.com_xz[axis].lower) - .0625L * port) /
        .9375L;
    check(enclosed(d.pressure_xz[0][axis], port) &&
              enclosed(d.pressure_xz[1][axis], star),
          "Literal raw fixture independently corroborates exact "
          "midpoint/moment recipe");
  }
}
[[gnu::noinline]] void math_roster() {
  for (std::size_t slot = 0; slot < 24; ++slot) {
    const auto f = fixture(slot);
    charge(totals.math, 24);
    const auto d =
        detail::intermediate_pause_support02_allocation_math(f.input, f.limits);
    check(!decltype(d)::source_qualified && !decltype(d)::body_qualified &&
              !decltype(d)::support_qualified &&
              !decltype(d)::world_qualified && !decltype(d)::actor_qualified &&
              !decltype(d)::contact_qualified && !decltype(d)::load_qualified &&
              !decltype(d)::fixture_qualified,
          "Raw allocation cannot mint genuine source or support capability");
    if (slot == 23) {
      check(d.stop_condition == Condition::invalid_limits &&
                stages(d.work) == std::array<std::size_t, 5>{} &&
                d.work.pressure_candidates == 0 &&
                d.work.validation_guards == 0,
            "Raised raw ceiling refuses before any scalar/validation work");
      continue;
    }
    arithmetic_accounting(d, f.limits);
    check(d.work.validation_guards <= 18 &&
              static_cast<std::size_t>(std::count(
                  d.validation_evaluated.begin(), d.validation_evaluated.end(),
                  true)) <= d.work.validation_guards,
          "Raw18 validation records remain separately charged without source "
          "guards");
    raw_oracle(d, f.input);
    for (std::size_t i = 0; i < 5; ++i)
      totals.raw[i] += stages(d.work)[i];
    totals.raw[5] += d.work.pressure_candidates;
    totals.raw_guards += d.work.validation_guards;
    if (slot <= 4 || slot == 8 || slot == 16 || slot == 17)
      check(d.nominal_equilibrium,
            "Valid literal fixture completes arithmetic allocation only");
    if (slot == 5 || slot == 6 || slot == 7)
      check(
          !d.intersection_complete && d.work.pressure_candidates == 0 &&
              d.work.midpoint_operations == 0 &&
              d.work.allocation_operations == 0,
          "Empty/touching/uncertain intersection cannot fall through to COPs");
    if (slot >= 9 && slot <= 13)
      check(!d.nominal_equilibrium, "Nonfinite/inverted/outside/unsupported "
                                    "operands never default to valid pressure");
    if (slot == 8)
      check(enclosed(d.port_source_upper_xz[1], 2),
            "Last of all four source vertices defines authentic raw extent");
    if (slot == 14)
      check(d.work.allocation_operations == 5 &&
                !d.allocation_evaluated[1][2] &&
                d.stop_condition == Condition::allocation_capacity &&
                !d.nominal_equilibrium,
            "One-less allocation stops before sixth star-Z division");
    if (slot == 15)
      check(d.work.source_coordinates == 7 &&
                !d.source_coordinate_evaluated[3][1] &&
                d.stop_condition == Condition::source_coordinate_capacity &&
                d.work.intersection_operations == 0,
            "Seven source coordinates never enroll unvisited last vertex "
            "maximum");
    if (slot >= 18 && slot <= 22) {
      const auto stage = slot - 18;
      check(stages(d.work)[stage] == 0 && d.stop_condition == capacity[stage],
            "Zero raw cap refuses exactly at reached operation with honest "
            "prefix");
      if (slot == 21)
        check(d.work.pressure_candidates == 1,
              "Port candidate charged before first midpoint");
      if (slot == 22)
        check(d.work.pressure_candidates == 2,
              "Star candidate charged before first allocation");
    }
  }
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
    std::optional<Provider> p;
    Summary summary;
    immutable_request();
    create_source(*binding, *boots, p);
    if (p) {
      first_one(*p, false, summary.first[0]);
      first_one(*p, true, summary.first[1]);
      if (summary.first[0].exists && summary.first[1].exists)
        check(summary.first[0].hash == summary.first[1].hash,
              "Constant reverse retains identical physical/source/pressure "
              "evidence");
      consumer_roster(*p, summary);
    } else
      std::cout << "FIRST_SUPPORT02 NOTRUN source_refused\n" << std::flush;
    math_roster();
    constexpr std::array<std::uint64_t, 8> old_max{48,  96,    48, 144,
                                                   288, 12288, 96, 768};
    constexpr std::array<std::size_t, 6> stage_max{192, 384, 192,
                                                   192, 288, 1488};
    constexpr std::array<std::size_t, 6> raw_max{96, 192, 96, 96, 144, 48};
    for (std::size_t i = 0; i < 8; ++i)
      check(totals.old[i] <= old_max[i],
            "Fixed consumer aggregate preserves all inherited budgets");
    for (std::size_t i = 0; i < 6; ++i) {
      check(totals.stages[i] <= stage_max[i],
            "Fixed consumer aggregate respects new stage budget");
      check(totals.raw[i] <= raw_max[i],
            "Exactly24 raw allocation calls have separate aggregate budget");
    }
    check(totals.creators == 1 && totals.consumers <= 47 &&
              totals.child_calls <= 48 && totals.math == 24 &&
              totals.oracles <= 2 && totals.raw_guards <= 432,
          "One creator,48 indexed/A44 skipped,24 allocation-only calls and two "
          "FIRST-only oracles");
    std::cout << "VALIDATION creators=" << totals.creators
              << " consumers=" << totals.consumers
              << " graphs=" << totals.child_calls << " math=" << totals.math
              << " oracles=" << totals.oracles << " checks=" << checks
              << " failures=" << failures << " bytes=" << bytes << '\n'
              << std::flush;
    check(!out.truncated && !err.truncated && std::cout.good() &&
              std::cerr.good() && bytes <= 16384,
          "Entire stdout/stderr stream completed without hidden truncation");
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout.rdbuf(saved_out);
  std::cerr.rdbuf(saved_err);
  return failures == 0 ? 0 : 1;
}
