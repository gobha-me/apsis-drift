#include "apsis_drift/origin_boarding_intermediate_pause_envelope.hpp"
#include "origin_boarding_intermediate_pause_envelope_internal.hpp"
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
using Diagnostic = BoardingIntermediatePauseEnvelopeDiagnostic;
using State = BoardingIntermediatePauseEnvelopeState;
using Condition = BoardingIntermediatePauseEnvelopeCondition;
using Scalar = BoardingFootSiteScalarBounds;
using Limits = detail::BoardingIntermediatePauseEnvelopeLimits;
using MathLimits = detail::BoardingIntermediatePauseEnvelopeMathLimits;
using MathInput = detail::BoardingIntermediatePauseEnvelopeMathInput;
using Access = detail::BoardingIntermediatePauseSupportAccess;
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
  std::size_t creators{}, consumers{}, math{}, oracles{}, child_calls{};
  std::array<std::uint64_t, 8> old{};
  std::array<std::size_t, 6> stages{};
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
    &Limits::sole_records, &Limits::source_vertices, &Limits::minima,
    &Limits::weighted_operations, &Limits::gap_operations};
constexpr std::array<std::size_t MathLimits::*, 5> math_members{
    &MathLimits::sole_records, &MathLimits::source_vertices,
    &MathLimits::minima, &MathLimits::weighted_operations,
    &MathLimits::gap_operations};
constexpr std::array<Condition, 5> capacity{
    Condition::sole_capacity, Condition::source_vertex_capacity,
    Condition::minimum_capacity, Condition::weighted_capacity,
    Condition::gap_capacity};
template <class Work> auto stages(const Work& w) -> std::array<std::size_t, 5> {
  return {w.sole_records, w.source_vertices, w.minima, w.weighted_operations,
          w.gap_operations};
}
auto old_work(const Diagnostic& d) -> std::array<std::uint64_t, 9> {
  const auto& w = d.work.pause;
  return {w.phase.graphs,        w.phase.legs,   w.phase.bodies,
          w.phase.sectors,       w.phase.timing, w.projection_guards,
          w.pressure_candidates, w.disk_edges,   d.child_output_bytes};
}
void set_old(Limits& l, std::size_t i, std::uint64_t x) {
  switch (i) {
    case 0: l.pause.phase.graphs = x; break;
    case 1: l.pause.phase.legs = x; break;
    case 2: l.pause.phase.bodies = x; break;
    case 3: l.pause.phase.sectors = x; break;
    case 4: l.pause.phase.timing = x; break;
    case 5: l.pause.projection_guards = x; break;
    case 6: l.pause.pressure_candidates = x; break;
    case 7: l.pause.disk_edges = x; break;
    default: l.pause.output_bytes = x; break;
  }
}
struct Hash {
  std::uint64_t value{1469598103934665603ULL};
  void add(std::uint64_t x) { value = (value ^ x) * 1099511628211ULL; }
  void scalar(double x) { add(std::bit_cast<std::uint64_t>(x == 0 ? 0. : x)); }
  void bound(Scalar b) {
    scalar(b.lower);
    scalar(b.upper);
    add(b.supported);
  }
  void text(std::string_view s) {
    add(s.size());
    for (const unsigned char c : s)
      add(c);
  }
};
auto semantic_hash(const Diagnostic& d) -> std::uint64_t {
  Hash h;
  h.add(d.version);
  h.add(static_cast<unsigned>(d.axis));
  h.add(static_cast<unsigned>(d.state));
  h.add(d.arithmetic_supported);
  h.add(d.envelope_assessed);
  h.add(d.necessary_refuted);
  h.add(d.projection_available);
  for (const auto b : d.definition_evaluated)
    h.add(b);
  for (const auto b : d.weighted_evaluated)
    h.add(b);
  h.add(d.gap_evaluated);
  for (const auto b : {d.com_z, d.weighted_upper_z, d.gap_z})
    h.bound(b);
  for (const auto& s : d.sites) {
    h.text(s.name);
    h.scalar(s.plane);
    h.bound(s.sole_max_z);
    h.bound(s.source_max_z);
    h.bound(s.upper_z);
    h.add(s.sole_evaluated);
    h.add(s.minimum_evaluated);
    for (const auto b : s.source_vertex_evaluated)
      h.add(b);
    for (const auto& k : s.keys) {
      h.add(static_cast<unsigned>(k.buffer));
      h.add(k.group);
      h.add(k.triangle);
    }
  }
  for (const auto x : old_work(d))
    h.add(x);
  for (const auto x : stages(d.work))
    h.add(x);
  h.add(d.work.definition_guards);
  h.add(d.work.child_calls);
  h.add(d.work.pause.phase_calls);
  h.add(d.output_bytes);
  h.add(static_cast<unsigned>(d.child_stop_condition));
  h.add(d.child_first_refusal.has_value());
  if (d.child_first_refusal) {
    const auto& f = *d.child_first_refusal;
    h.add(static_cast<unsigned>(f.condition));
    h.add(f.side.value_or(3));
    h.add(f.edge.value_or(5));
    h.add(f.source_edge);
    h.scalar(f.limiting_bound.lower);
    h.scalar(f.limiting_bound.upper);
    h.add(static_cast<unsigned>(f.phase.condition));
    h.scalar(f.phase.first);
    h.scalar(f.phase.last);
  }
  h.add(d.first_refusal.has_value());
  if (d.first_refusal) {
    const auto& f = *d.first_refusal;
    h.add(static_cast<unsigned>(f.condition));
    h.add(f.side.value_or(3));
    h.add(f.vertex.value_or(5));
    h.add(f.operation.value_or(5));
  }
  return h.value;
}
struct Baseline {
  std::array<std::uint64_t, 9> old{};
  std::array<std::size_t, 5> stage{};
  std::uint64_t hash{};
  std::size_t output{}, definitions{}, child_calls{};
  State state{};
  bool exists{}, assessed{};
};
struct Summary {
  std::array<Baseline, 2> first;
};
static_assert(sizeof(Summary) < 512);
auto summarize(const Diagnostic& d) -> Baseline {
  return {old_work(d),
          stages(d.work),
          semantic_hash(d),
          d.output_bytes,
          d.work.definition_guards,
          d.work.child_calls,
          d.state,
          true,
          d.envelope_assessed};
}
template <class T> void authority(const T&) {
  check(!T::body_qualified && !T::source_qualified && !T::contact_qualified &&
            !T::load_qualified && !T::support_qualified &&
            !T::fixture_qualified && !T::first_flight_qualified &&
            !T::self_qualified && !T::material_qualified &&
            !T::world_qualified && !T::route_qualified && !T::seat_qualified &&
            !T::actor_qualified && !T::save_qualified && !T::dynamics_qualified,
        "Necessary envelope or raw arithmetic never grants downstream "
        "permission");
}
template <class T, class L> void arithmetic_accounting(const T& d, const L& l) {
  const auto w = stages(d.work);
  for (std::size_t i = 0; i < 5; ++i) {
    if constexpr (std::is_same_v<L, Limits>)
      check(w[i] <= l.*members[i],
            "Envelope operations charged within lowered stage caps");
    else
      check(w[i] <= l.*math_members[i],
            "Raw operations charged within lowered stage caps");
  }
  check(!d.envelope_assessed || (d.arithmetic_supported && finite(d.gap_z) &&
                                 finite(d.weighted_upper_z)),
        "Assessed envelope requires complete supported arithmetic");
  check(!d.necessary_refuted || (d.envelope_assessed && d.gap_z.lower > 0),
        "Only a strict supported positive lower gap may refute nominal "
        "allocation");
  check(!d.envelope_assessed ||
            (d.gap_evaluated &&
             std::all_of(d.weighted_evaluated.begin(),
                         d.weighted_evaluated.end(), [](bool b) { return b; })),
        "Assessed envelope requires all three weighted operations and actual "
        "gap evaluation");
  check(
      !d.envelope_assessed ||
          d.state == (d.necessary_refuted ? State::necessary_refuted
                                          : State::not_refuted),
      "Completed gap classification grants refutation or only non-refutation");
  for (const auto& s : d.sites) {
    if (d.envelope_assessed)
      check(
          s.sole_evaluated && finite(s.sole_max_z),
          "Completed envelope uses a successfully evaluated full sole maximum");
    if (s.minimum_evaluated)
      check(finite(s.upper_z) && finite(s.source_max_z) && s.sole_evaluated &&
                std::all_of(s.source_vertex_evaluated.begin(),
                            s.source_vertex_evaluated.end(),
                            [](bool b) { return b; }),
            "A side minimum needs the complete fullsole and all four source "
            "vertices");
  }
  authority(d);
}
void accounting(const Diagnostic& d, const Limits& l) {
  arithmetic_accounting(d, l);
  check(d.output_bytes <= l.output_bytes && d.output_bytes <= 1024,
        "Compact envelope output obeys its preflight budget");
  check(d.work.child_calls <= 1 && d.work.definition_guards <= 33,
        "One old child and bounded definition records only");
  check(d.work.pause.phase_calls <= 1 &&
            static_cast<std::size_t>(std::count(
                d.definition_evaluated.begin(), d.definition_evaluated.end(),
                true)) <= d.work.definition_guards,
        "Evaluated definition masks and old phase calls retain honest actual "
        "prefix");
  if (d.envelope_assessed)
    check(d.projection_available && std::all_of(d.definition_evaluated.begin(),
                                                d.definition_evaluated.end(),
                                                [](bool b) { return b; }),
          "An assessed genuine envelope retains every immutable definition "
          "prerequisite");
  const auto ow = old_work(d);
  const std::array<std::uint64_t, 9> cap{
      l.pause.phase.graphs,        l.pause.phase.legs,
      l.pause.phase.bodies,        l.pause.phase.sectors,
      l.pause.phase.timing,        l.pause.projection_guards,
      l.pause.pressure_candidates, l.pause.disk_edges,
      l.pause.output_bytes};
  for (std::size_t i = 0; i < 9; ++i)
    check(ow[i] <= cap[i],
          "Inherited child retains actual bounded work/output");
  totals.child_calls += d.work.child_calls;
  for (std::size_t i = 0; i < 8; ++i)
    totals.old[i] += ow[i];
  for (std::size_t i = 0; i < 5; ++i)
    totals.stages[i] += stages(d.work)[i];
  totals.stages[5] += d.work.definition_guards;
  if (d.child_stop_condition != BoardingIntermediatePauseCondition::none)
    check(!d.envelope_assessed &&
              (d.work.sole_records == 0 && d.work.source_vertices == 0 &&
               d.work.minima == 0 && d.work.weighted_operations == 0 &&
               d.work.gap_operations == 0),
          "Child terminal stop cannot become an envelope despite earlier "
          "copied projection");
}
void print(const Diagnostic& d, std::size_t ordinal) {
  std::cout << "FIRST_PAUSE_ENVELOPE ordinal=" << ordinal
            << " version=" << d.version
            << " axis=" << static_cast<unsigned>(d.axis)
            << " reverse=" << d.reverse
            << " state=" << static_cast<unsigned>(d.state)
            << " arithmetic=" << d.arithmetic_supported
            << " assessed=" << d.envelope_assessed
            << " refuted=" << d.necessary_refuted
            << " output=" << d.output_bytes
            << " child_calls=" << d.work.child_calls
            << " definition=" << d.work.definition_guards << " old=";
  for (const auto x : old_work(d))
    std::cout << x << ',';
  std::cout << " new=";
  for (const auto x : stages(d.work))
    std::cout << x << ',';
  std::cout << " COM=" << std::setprecision(17) << d.com_z.lower << ','
            << d.com_z.upper << " weighted=" << d.weighted_upper_z.lower << ','
            << d.weighted_upper_z.upper << " gap=" << d.gap_z.lower << ','
            << d.gap_z.upper
            << " child_stop=" << static_cast<unsigned>(d.child_stop_condition);
  if (d.child_first_refusal) {
    const auto& f = *d.child_first_refusal;
    std::cout << " child_finding=" << static_cast<unsigned>(f.condition)
              << " child_side=" << f.side.value_or(3)
              << " child_edge=" << f.edge.value_or(5)
              << " child_source=" << f.source_edge;
  }
  if (d.first_refusal) {
    const auto& f = *d.first_refusal;
    std::cout << " refusal=" << static_cast<unsigned>(f.condition)
              << " side=" << f.side.value_or(3)
              << " vertex=" << f.vertex.value_or(5)
              << " operation=" << f.operation.value_or(5);
  }
  std::cout << '\n';
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& s = d.sites[side];
    std::cout << "FIRST_ENVELOPE_SITE side=" << side << " name=" << s.name
              << " plane=" << s.plane << " sole=" << s.sole_evaluated << ':'
              << s.sole_max_z.lower << ',' << s.sole_max_z.upper
              << " source=" << s.source_max_z.lower << ','
              << s.source_max_z.upper << " vertices=";
    for (const bool b : s.source_vertex_evaluated)
      std::cout << b;
    std::cout << " minimum=" << s.minimum_evaluated << ':' << s.upper_z.lower
              << ',' << s.upper_z.upper << " keys=";
    for (const auto& k : s.keys)
      std::cout << static_cast<unsigned>(k.buffer) << ':' << k.group << ':'
                << k.triangle << ',';
    std::cout << '\n';
  }
  std::cout << std::flush;
}
[[gnu::noinline]] void first_oracle(const Diagnostic& d) {
  // These genuine compact projections are populated only after all old constant
  // graph/source-plane prerequisites; failed child geometry is never inspected.
  if (!d.projection_available) return;
  check(finite(d.com_z),
        "Authentic completed projection retains finite current COM");
  charge(totals.oracles, 2);
  const auto o = body_oracle();
  check(enclosed(d.com_z, o.com[2]),
        "Independent unfactored18points/15masses corroborate current COMz");
  std::array<long double, 2> u{};
  bool complete = true;
  for (std::size_t side = 0; side < 2; ++side) {
    const auto b = side == 0 ? 4U : 11U;
    const auto thigh = subtract(o.points[b + 1], o.points[b]);
    const auto shin = subtract(o.points[b + 2], o.points[b + 1]);
    const auto l1 = static_cast<long double>(.47285),
               l2 = static_cast<long double>(.47478);
    check(std::abs(dot(thigh, thigh) - l1 * l1) <
                  64 * std::numeric_limits<long double>::epsilon() &&
              std::abs(dot(shin, shin) - l2 * l2) <
                  64 * std::numeric_limits<long double>::epsilon(),
          "Independent three-dimensional knees preserve both original rods");
    long double sole = -std::numeric_limits<long double>::infinity();
    for (const auto sign : std::array{-1.L, 1.L})
      for (const auto xsign : std::array{-1.L, 1.L}) {
        const auto corner =
            add(o.soles[side], {xsign * static_cast<long double>(.06), 0,
                                sign * static_cast<long double>(.14)});
        sole = std::max(sole, corner[2]);
      }
    if (d.sites[side].sole_evaluated)
      check(enclosed(d.sites[side].sole_max_z, sole),
            "Four original unshrunk sole corners enclose full +Z maximum");
    const auto* source = Access::partition(d.source, side);
    check(source != nullptr,
          "Oracle borrows the genuine finite source without caller authority");
    if (!source) {
      complete = false;
      continue;
    }
    long double maximum = -std::numeric_limits<long double>::infinity();
    for (std::size_t k = 0; k < 4; ++k) {
      maximum = std::max(
          maximum, static_cast<long double>(source->perimeter_metres[k].z));
      complete = complete && d.sites[side].source_vertex_evaluated[k];
    }
    if (std::all_of(d.sites[side].source_vertex_evaluated.begin(),
                    d.sites[side].source_vertex_evaluated.end(),
                    [](bool b) { return b; }))
      check(enclosed(d.sites[side].source_max_z, maximum),
            "All four captured decoded source corners supply its maximum");
    u[side] = std::min(sole, maximum);
    if (d.sites[side].minimum_evaluated)
      check(enclosed(d.sites[side].upper_z, u[side]),
            "Independent fullsole/source minimum is enclosed");
    else
      complete = false;
  }
  if (complete && d.envelope_assessed) {
    const auto weighted = .25L * u[0] + .75L * u[1];
    check(enclosed(d.weighted_upper_z, weighted) &&
              enclosed(d.gap_z, o.com[2] - weighted),
          "Independent fixed-share weighted bound and necessary gap are "
          "enclosed");
  }
}
[[gnu::noinline]] void create_source(const NativeCraftBinding& binding,
                                     const OriginBoardingBootSupport& boots,
                                     std::optional<Provider>& provider) {
  charge(totals.creators, 1);
  auto result = make_origin_boarding_intermediate_pause_support(binding, boots);
  std::cout << "FIRST_ENVELOPE_SOURCE admitted=" << result.has_value();
  if (!result)
    std::cout << " error=" << result.error();
  else {
    const auto* s = result->summary();
    std::cout << " complete=" << (s && s->complete);
    if (s) {
      std::cout << " bytes=" << s->required_source_bytes << ','
                << s->actual_source_bytes << " work=" << s->work.base_guards
                << ',' << s->work.metadata_rows << ',' << s->work.face_reads
                << ',' << s->work.quad_records << ',' << s->work.quad_edges
                << ',' << s->work.quad_sides << ',' << s->work.face_vertices
                << ',' << s->work.corner_matches << ',' << s->work.face_windings
                << ',' << s->work.incidence << ',' << s->work.diagonals;
    }
  }
  std::cout << '\n' << std::flush;
  if (result) {
    check(Access::valid(*result),
          "Only genuine public creator supplies the source capability");
    provider.emplace(std::move(*result));
  }
}
[[gnu::noinline]] void first_one(const Provider& p, bool reverse,
                                 Baseline& summary) {
  charge(totals.consumers, 48);
  const auto result =
      assess_origin_boarding_intermediate_pause_envelope(p, reverse);
  if (!result) {
    std::cout << "FIRST_PAUSE_ENVELOPE ordinal=" << (reverse ? 2 : 1)
              << " unavailable=" << result.error() << '\n'
              << std::flush;
    return;
  }
  print(*result, reverse ? 2U : 1U);
  accounting(*result, {});
  first_oracle(*result);
  summary = summarize(*result);
  check(result->reverse == reverse,
        "Fresh envelope preserves selected traversal direction");
}
[[gnu::noinline]] void assessment_one(const Provider& p, bool reverse, Limits l,
                                      const Baseline& first, bool same = false,
                                      std::optional<Condition> expected = {},
                                      bool preflight = false,
                                      bool output = false,
                                      bool oldstop = false) {
  charge(totals.consumers, 48);
  const auto result =
      detail::intermediate_pause_envelope_bounded(p, reverse, l);
  if (preflight || output) {
    check(!result, "Overmaximum/undersized output preflight refuses before "
                   "child allocation");
    return;
  }
  check(result.has_value(),
        "Valid bounded envelope returns its honest fixed evidence");
  if (!result) return;
  accounting(*result, l);
  if (same && first.exists)
    check(semantic_hash(*result) == first.hash,
          "Exact caps/lifetime/reverse replay retains semantic FIRST evidence");
  if (expected)
    check(result->first_refusal &&
              result->first_refusal->condition == *expected &&
              !result->envelope_assessed,
          "Reached capacity/binding/environment preserves exact new refusal");
  if (expected)
    for (std::size_t i = 0; i < 5; ++i)
      if (*expected == capacity[i])
        check(
            stages(result->work)[i] == l.*members[i],
            "Reached stage refusal preserves exact consumed lower-cap prefix");
  if (oldstop)
    check(!result->envelope_assessed && result->work.sole_records == 0 &&
              result->work.source_vertices == 0 && result->work.minima == 0 &&
              result->work.weighted_operations == 0 &&
              result->work.gap_operations == 0,
          "Incomplete/terminal old child cannot qualify a new envelope");
  if (expected == Condition::invalid_binding ||
      expected == Condition::unsupported)
    check(result->work.child_calls == 0 && result->work.pause.phase.graphs == 0,
          "Invalid source/environment preflight charges no child invocation or "
          "graph");
}
[[gnu::noinline]] void consumer_roster(const Provider& p,
                                       const Summary& summary) {
  const auto& first = summary.first[0];
  Limits exact;
  if (first.exists) {
    for (std::size_t i = 0; i < 5; ++i)
      if (first.stage[i]) exact.*members[i] = first.stage[i];
    for (std::size_t i = 0; i < 9; ++i)
      if (first.old[i]) set_old(exact, i, first.old[i]);
    exact.output_bytes = first.output;
  }
  assessment_one(p, false, exact, first, true);           // A03.
  assessment_one(p, true, exact, summary.first[1], true); // A04.
  for (std::size_t i = 0; i < 5; ++i) {
    Limits l;
    l.*members[i] = first.stage[i];
    assessment_one(p, false, l, first, first.exists);
  } // A05-A09.
  for (std::size_t i = 0; i < 5; ++i) {
    Limits l;
    l.*members[i] = 0;
    assessment_one(p, false, l, first, false,
                   first.stage[i] ? std::optional{capacity[i]} : std::nullopt);
  } // A10-A14.
  for (std::size_t i = 0; i < 5; ++i) {
    if (first.stage[i] <= 1) continue;
    Limits l;
    l.*members[i] = first.stage[i] - 1;
    assessment_one(p, false, l, first, false, capacity[i]);
  } // A15-A19, no replacement.
  for (std::size_t i = 0; i < 5; ++i) {
    Limits l;
    ++(l.*members[i]);
    assessment_one(p, false, l, first, false, {}, true);
  } // A20-A24.
  for (std::size_t i = 0; i < 9; ++i) {
    Limits l;
    set_old(l, i, 0);
    assessment_one(p, false, l, first, false, {}, false, false, true);
  } // A25-A33.
  for (std::size_t mode = 0; mode < 6; ++mode) {
    const Environment env(mode);
    if (!env.available) continue;
    assessment_one(p, false, {}, first, false, Condition::unsupported);
  } // A34-A39.
  {
    auto empty = p;
    const auto retained = std::move(empty);
    // NOLINTBEGIN(bugprone-use-after-move) -- Empty-source contract.
    assessment_one(empty, false, {}, first, false,
                   Condition::invalid_binding); // A40.
    // NOLINTEND(bugprone-use-after-move) -- End empty-source control.
    assessment_one(retained, false, {}, first, true);
  } // A41.
  {
    std::optional<Provider> survivor;
    {
      // A separate owner dies before assessment of the retained copy.
      // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Owner.
      const auto original = p;
      survivor.emplace(original);
    }
    assessment_one(*survivor, false, {}, first, true);
  } // A42.
  {
    auto alias = p;
    std::optional<Provider> copy;
    copy.emplace(alias);
    {
      auto caller = std::move(alias);
      check(Access::valid(caller),
            "Cached genuine source alias remains valid before caller release");
    }
    assessment_one(*copy, false, {}, first, true);
  } // A43, no additional creator.
  {
    Limits l;
    l.output_bytes = 0;
    assessment_one(p, false, l, first, false, {}, false, true);
  } // A44.
  if (first.output > 1) {
    Limits l;
    l.output_bytes = first.output - 1;
    assessment_one(p, false, l, first, false, {}, false, true);
  } // A45.
  {
    Limits l;
    ++l.output_bytes;
    assessment_one(p, false, l, first, false, {}, true);
  } // A46.
  {
    Limits l;
    l.sole_records = l.source_vertices = 0;
    assessment_one(p, false, l, first, false,
                   first.stage[0] ? std::optional{Condition::sole_capacity}
                                  : std::nullopt);
  } // A47.
  {
    Limits l;
    l.minima = l.gap_operations = 0;
    assessment_one(p, false, l, first, false,
                   first.stage[2] ? std::optional{Condition::minimum_capacity}
                                  : std::nullopt);
  } // A48.
}
auto point(double x) -> Scalar {
  return {x, x, true};
}
auto interval(double a, double b) -> Scalar {
  return {a, b, true};
}
struct Fixture {
  MathInput input;
  MathLimits limits;
};
[[gnu::noinline]] auto fixture(std::size_t ordinal) -> Fixture {
  Fixture f;
  f.input.sole_max_z = {point(0), point(0)};
  for (auto& side : f.input.source_vertices_z)
    side.fill(point(0));
  f.input.com_z = point(.5);
  switch (ordinal) {
    case 1: break;
    case 2: f.input.com_z = point(0); break;
    case 3: f.input.com_z = point(-.5); break;
    case 4: f.input.com_z = interval(-.25, .25); break;
    case 5:
      f.input.sole_max_z = {point(2), point(1)};
      f.input.source_vertices_z[0].fill(point(1));
      f.input.source_vertices_z[1].fill(point(2));
      f.input.com_z = point(1.5);
      break;
    case 6:
      f.input.sole_max_z = {point(0), point(2)};
      f.input.source_vertices_z[0].fill(point(2));
      break;
    case 7:
      f.input.sole_max_z = {interval(0, 2), point(0)};
      f.input.source_vertices_z[0].fill(interval(1, 3));
      f.input.com_z = interval(0, 1);
      break;
    case 8:
    case 16:
      f.input.sole_max_z[0] = point(1);
      f.input.source_vertices_z[0][3] = point(1);
      if (ordinal == 16) f.limits.source_vertices = 3;
      break;
    case 9:
      f.input.source_vertices_z[0].fill(point(8));
      f.input.com_z = point(8);
      break;
    case 10:
      f.input.com_z = point(std::numeric_limits<double>::quiet_NaN());
      break;
    case 11:
      f.input.source_vertices_z[0][2] =
          point(std::numeric_limits<double>::infinity());
      break;
    case 12: f.input.com_z = interval(.5, -.5); break;
    case 13:
      f.input.source_vertices_z[0][3] =
          point(std::nextafter(8., std::numeric_limits<double>::infinity()));
      break;
    case 14: f.input.sole_max_z[0].supported = false; break;
    case 15: f.limits.gap_operations = 0; break;
    default: throw std::runtime_error("Unregistered fixture ordinal");
  }
  return f;
}
[[gnu::noinline]] void raw_oracle(
    const MathInput& input,
    const detail::BoardingIntermediatePauseEnvelopeMath& result) {
  std::array<std::array<long double, 2>, 2> u{};
  for (std::size_t side = 0; side < 2; ++side) {
    std::array<long double, 2> maximum{
        -std::numeric_limits<long double>::infinity(),
        -std::numeric_limits<long double>::infinity()};
    for (const auto b : input.source_vertices_z[side]) {
      maximum[0] = std::max(maximum[0], static_cast<long double>(b.lower));
      maximum[1] = std::max(maximum[1], static_cast<long double>(b.upper));
    }
    for (std::size_t end = 0; end < 2; ++end) {
      const auto sole = end == 0 ? input.sole_max_z[side].lower
                                 : input.sole_max_z[side].upper;
      u[side][end] = std::min(static_cast<long double>(sole), maximum[end]);
      check(enclosed(result.sites[side].source_max_z, maximum[end]) &&
                enclosed(result.sites[side].upper_z, u[side][end]),
            "Literal source max and componentwise sole/source min enclosures "
            "preserve both ends");
    }
  }
  const auto lo = .25L * u[0][0] + .75L * u[1][0],
             hi = .25L * u[0][1] + .75L * u[1][1];
  check(enclosed(result.weighted_upper_z, lo) &&
            enclosed(result.weighted_upper_z, hi) &&
            enclosed(result.gap_z,
                     static_cast<long double>(input.com_z.lower) - hi) &&
            enclosed(result.gap_z,
                     static_cast<long double>(input.com_z.upper) - lo),
        "Independent interval extrema and fixed weighted subtraction enclose "
        "literal fixture");
}
[[gnu::noinline]] void math_roster() {
  for (std::size_t ordinal = 1; ordinal <= 16; ++ordinal) {
    const auto f = fixture(ordinal);
    charge(totals.math, 16);
    const auto result =
        detail::intermediate_pause_envelope_math(f.input, f.limits);
    arithmetic_accounting(result, f.limits);
    if (ordinal <= 9) {
      check(result.envelope_assessed && result.arithmetic_supported,
            "Valid literal raw fixture completes shared bounded algebra");
      if (result.envelope_assessed) raw_oracle(f.input, result);
      if (ordinal == 2 || ordinal == 3 || ordinal == 4 || ordinal == 7)
        check(!result.necessary_refuted && result.state == State::not_refuted,
              "Touch/negative/straddle never becomes existence or support");
      else
        check(result.necessary_refuted &&
                  result.state == State::necessary_refuted,
              "Strict literal positive lower gap refutes raw nominal algebra "
              "only");
    }
    if (ordinal >= 10 && ordinal <= 14)
      check(!result.envelope_assessed && !result.necessary_refuted,
            "Malformed/unsupported/outside-domain scalar cannot become "
            "arithmetic envelope");
    if (ordinal == 15)
      check(result.first_refusal &&
                result.first_refusal->condition == Condition::gap_capacity &&
                result.work.weighted_operations == 3 &&
                result.work.gap_operations == 0 && !result.gap_evaluated &&
                !result.envelope_assessed,
            "Gap zero-cap stops after all three genuinely reached weighted "
            "operations");
    if (ordinal == 16)
      check(result.first_refusal &&
                result.first_refusal->condition ==
                    Condition::source_vertex_capacity &&
                result.work.source_vertices == 3 &&
                result.sites[0].source_vertex_evaluated[0] &&
                result.sites[0].source_vertex_evaluated[1] &&
                result.sites[0].source_vertex_evaluated[2] &&
                !result.sites[0].source_vertex_evaluated[3] &&
                !result.sites[0].minimum_evaluated && result.work.minima == 0 &&
                result.work.gap_operations == 0 && !result.envelope_assessed,
            "Last-vertex cap preserves exact three-read prefix and refuses "
            "incomplete maximum");
  }
}
} // namespace
int main() {
  std::size_t bytes{};
  StreamCounter out(std::cout.rdbuf(), bytes), err(std::cerr.rdbuf(), bytes);
  auto* saved_out = std::cout.rdbuf(&out);
  auto* saved_err = std::cerr.rdbuf(&err);
  try {
    const auto binding = make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{});
    if (!binding) throw std::runtime_error(binding.error());
    const auto boots = make_origin_boarding_boot_support(*binding);
    if (!boots) throw std::runtime_error(boots.error());
    std::optional<Provider> provider;
    Summary summary;
    create_source(*binding, *boots, provider);
    if (provider) {
      first_one(*provider, false, summary.first[0]);
      first_one(*provider, true, summary.first[1]);
      if (summary.first[0].exists && summary.first[1].exists)
        check(summary.first[0].hash == summary.first[1].hash,
              "Constant reverse traversal preserves the same physical envelope "
              "and findings");
      consumer_roster(*provider, summary);
    } else
      std::cout << "FIRST_PAUSE_ENVELOPE NOTRUN source_refused\n" << std::flush;
    math_roster();
    constexpr std::array<std::uint64_t, 8> old_ceiling{48,  96,    48, 144,
                                                       288, 12288, 96, 768};
    constexpr std::array<std::size_t, 6> new_ceiling{96,  384, 96,
                                                     144, 48,  1584};
    for (std::size_t i = 0; i < 8; ++i)
      check(totals.old[i] <= old_ceiling[i],
            "Entire fixed consumer roster respects inherited aggregate work");
    for (std::size_t i = 0; i < 6; ++i)
      check(totals.stages[i] <= new_ceiling[i],
            "Entire fixed consumer roster respects new aggregate work");
    check(totals.creators == 1 && totals.consumers <= 48 &&
              totals.child_calls <= 48 && totals.math == 16 &&
              totals.oracles <= 2,
          "Exactly one creator, bounded48/16 roster and two FIRST-only "
          "candidate oracles");
    check(std::cout.good() && std::cerr.good() && !out.truncated &&
              !err.truncated && bytes <= 16128,
          "Complete shared execution stream remains below sixteen KiB with "
          "summary reserve");
    std::cout << "VALIDATION creators=" << totals.creators
              << " consumers=" << totals.consumers << " math=" << totals.math
              << " oracles=" << totals.oracles
              << " child_calls=" << totals.child_calls << " old=";
    for (const auto x : totals.old)
      std::cout << x << ',';
    std::cout << " stages=";
    for (const auto x : totals.stages)
      std::cout << x << ',';
    std::cout << " summary=" << sizeof(Summary) << " checks=" << checks
              << " failures=" << failures << " bytes_before_summary=" << bytes
              << '\n'
              << std::flush;
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "ERROR: " << e.what() << '\n';
  }
  const bool bad =
      out.truncated || err.truncated || !std::cout.good() || !std::cerr.good();
  std::cout.rdbuf(saved_out);
  std::cerr.rdbuf(saved_err);
  return failures == 0 && !bad ? 0 : 1;
}
