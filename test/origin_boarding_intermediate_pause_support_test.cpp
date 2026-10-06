#include "apsis_drift/origin_boarding_intermediate_pause_support.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
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
#include <string>
#include <string_view>
#include <utility>
#if defined(__SSE2__) && defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace {
using namespace apsis_drift;
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
  std::uint64_t constructors{}, consumers{}, math{}, oracles{};
  std::uint64_t phase_calls{};
  std::array<std::uint64_t, 8> work{};
} totals;
void charge(std::uint64_t& count, std::uint64_t maximum) {
  if (count >= maximum)
    throw std::runtime_error("Registered query roster exhausted");
  ++count;
}
struct StreamCounter : std::streambuf {
  std::streambuf* target;
  std::size_t& bytes;
  bool truncated{};
  StreamCounter(std::streambuf* destination, std::size_t& count)
      : target(destination), bytes(count) {}
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
  auto xsputn(const char* data, std::streamsize n) -> std::streamsize override {
    std::streamsize written{};
    while (written < n) {
      if (traits_type::eq_int_type(overflow(data[written]), traits_type::eof()))
        break;
      ++written;
    }
    return written;
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
      const std::array rounds{FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO};
      available = std::fesetround(rounds[mode]) == 0;
    } else {
#if defined(__SSE2__) && defined(__x86_64__)
      const auto changed = mode == 3   ? control_ | (1U << 15)
                           : mode == 4 ? control_ | (1U << 6)
                                       : (control_ & ~(3U << 13)) | (1U << 13);
      _mm_setcsr(changed);
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
struct Hash {
  std::uint64_t value{1469598103934665603ULL};
  void add(std::uint64_t n) { value = (value ^ n) * 1099511628211ULL; }
  void scalar(double x) { add(std::bit_cast<std::uint64_t>(x == 0 ? 0. : x)); }
  void text(std::string_view s) {
    add(s.size());
    for (const unsigned char c : s)
      add(c);
  }
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
auto enclosed(BoardingPlantedLegScalarBounds b, long double x) -> bool {
  // This allowance covers independent LD expression rounding only; admission
  // always uses the producer's unchanged outward lower/upper predicates.
  const auto rounding = 256 * std::numeric_limits<long double>::epsilon() *
                        std::max(1.L, std::abs(x));
  return std::isfinite(b.lower) && std::isfinite(b.upper) &&
         b.lower <= b.upper &&
         x >= static_cast<long double>(b.lower) - rounding &&
         x <= static_cast<long double>(b.upper) + rounding;
}
auto wide(RigidVector3 p) -> Point {
  return {p.x, p.y, p.z};
}
auto edge_side(Point a, Point b, Point p) -> long double {
  return (b[2] - a[2]) * (p[0] - a[0]) - (b[0] - a[0]) * (p[2] - a[2]);
}
void edge_oracle(const BoardingFootSiteEdgeEvidence& e, Point a, Point b,
                 Point p) {
  if (!e.signed_side.supported) return;
  const auto side = edge_side(a, b, p);
  const auto dx = b[0] - a[0], dz = b[2] - a[2], length2 = dx * dx + dz * dz;
  const auto required =
      static_cast<long double>(kBoardingBootPressureRadiusMetres) +
      kBoardingBootDiskEdgeMarginMetres;
  check(enclosed({e.signed_side.lower, e.signed_side.upper}, side) &&
            enclosed({e.edge_length_squared.lower, e.edge_length_squared.upper},
                     length2) &&
            enclosed({e.squared_margin_gap.lower, e.squared_margin_gap.upper},
                     side * side - required * required * length2),
        "Independent oriented edge/disk equation is enclosed");
  check(!e.disk_contained ||
            (e.signed_side.lower > 0 && e.squared_margin_gap.lower >= 0),
        "Disk admission uses original outward strict side and margin bounds");
}
using Provider = OriginBoardingIntermediatePauseSupport;
using Diagnostic = BoardingIntermediatePauseSupportDiagnostic;
using Evidence = BoardingIntermediatePauseConstructorEvidence;
using Condition = BoardingIntermediatePauseCondition;
using Status = BoardingIntermediatePauseDiskStatus;
using Access = detail::BoardingIntermediatePauseSupportAccess;
using ConstructorLimits = detail::BoardingIntermediatePauseConstructorLimits;
using Limits = detail::BoardingIntermediatePauseLimits;
using Fixture = detail::BoardingIntermediatePauseSourceFixture;
constexpr std::array<std::size_t ConstructorLimits::*, 12> constructor_members{
    &ConstructorLimits::source_bytes,   &ConstructorLimits::base_guards,
    &ConstructorLimits::metadata_rows,  &ConstructorLimits::face_reads,
    &ConstructorLimits::quad_records,   &ConstructorLimits::quad_edges,
    &ConstructorLimits::quad_sides,     &ConstructorLimits::face_vertices,
    &ConstructorLimits::corner_matches, &ConstructorLimits::face_windings,
    &ConstructorLimits::incidence,      &ConstructorLimits::diagonals};
auto counts(const Evidence& e) -> std::array<std::size_t, 12> {
  const auto& w = e.work;
  return {e.required_source_bytes,
          w.base_guards,
          w.metadata_rows,
          w.face_reads,
          w.quad_records,
          w.quad_edges,
          w.quad_sides,
          w.face_vertices,
          w.corner_matches,
          w.face_windings,
          w.incidence,
          w.diagonals};
}
auto caps(const Limits& l) -> std::array<std::uint64_t, 9> {
  return {l.phase.graphs,        l.phase.legs,   l.phase.bodies,
          l.phase.sectors,       l.phase.timing, l.projection_guards,
          l.pressure_candidates, l.disk_edges,   l.output_bytes};
}
void set_cap(Limits& l, std::size_t field, std::uint64_t value) {
  switch (field) {
    case 0: l.phase.graphs = value; break;
    case 1: l.phase.legs = value; break;
    case 2: l.phase.bodies = value; break;
    case 3: l.phase.sectors = value; break;
    case 4: l.phase.timing = value; break;
    case 5: l.projection_guards = static_cast<std::size_t>(value); break;
    case 6: l.pressure_candidates = static_cast<std::size_t>(value); break;
    case 7: l.disk_edges = static_cast<std::size_t>(value); break;
    default: l.output_bytes = static_cast<std::size_t>(value); break;
  }
}
auto counts(const Diagnostic& d) -> std::array<std::uint64_t, 9> {
  const auto& w = d.work;
  return {w.phase.graphs,        w.phase.legs,   w.phase.bodies,
          w.phase.sectors,       w.phase.timing, w.projection_guards,
          w.pressure_candidates, w.disk_edges,   d.output_bytes};
}
template <class T> void no_authority(const T&) {
  check(!T::source_qualified && !T::body_qualified && !T::support_qualified &&
            !T::world_qualified && !T::actor_qualified,
        "Raw fixture cannot mint source/body/support/WORLD/actor authority");
}
void denied(const Diagnostic&) {
  check(
      !Diagnostic::self_qualified && !Diagnostic::material_qualified &&
          !Diagnostic::world_qualified && !Diagnostic::route_qualified &&
          !Diagnostic::seat_qualified && !Diagnostic::actor_qualified &&
          !Diagnostic::save_qualified && !Diagnostic::strength_qualified &&
          !Diagnostic::friction_qualified && !Diagnostic::dynamics_qualified &&
          !Diagnostic::first_flight_qualified,
      "Nominal pause never supplies self/material/WORLD/route/actor authority");
}
void constructor_accounting(const Evidence& e, ConstructorLimits l) {
  const auto n = counts(e);
  for (std::size_t i = 1; i < n.size(); ++i)
    check(n[i] <= l.*constructor_members[i],
          "Constructor charge occurs before each bounded operation");
  check(
      e.actual_source_bytes <= l.source_bytes &&
          e.actual_source_bytes <= e.required_source_bytes,
      "Required source allocation is distinct from bounded actual allocation");
  check(!e.complete ||
            (e.arithmetic_supported && e.condition == Condition::none &&
             e.actual_source_bytes == e.required_source_bytes),
        "Complete creator requires supported source proof and allocation");
  for (std::size_t side = 0; side < 2; ++side)
    check(!e.complete || (e.identity_checked[side] && e.identity_valid[side] &&
                          e.geometry_evaluated[side] && e.quads[side].complete),
          "Complete source issuer authenticates both actual triangle pairs");
}
void print_constructor(bool admitted, std::string_view error,
                       const Evidence& e) {
  std::cout << "FIRST_INTERMEDIATE_PAUSE_CONSTRUCTOR accepted=" << admitted
            << " complete=" << e.complete
            << " arithmetic=" << e.arithmetic_supported
            << " condition=" << static_cast<unsigned>(e.condition)
            << " version=" << e.version << " bytes=" << e.required_source_bytes
            << ',' << e.actual_source_bytes << " work=";
  for (const auto n : counts(e))
    std::cout << n << ',';
  std::cout << " identity=" << e.identity_valid[0] << e.identity_valid[1]
            << " geometry=" << e.geometry_evaluated[0]
            << e.geometry_evaluated[1];
  if (e.side) std::cout << " side=" << *e.side;
  if (!admitted) std::cout << " error=" << error;
  std::cout << '\n' << std::flush;
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& s = e.sources[side];
    const auto& q = e.quads[side];
    std::cout << "FIRST_PAUSE_SOURCE side=" << side << " name=" << s.name
              << " object=" << s.object << " plane=" << std::setprecision(17)
              << s.plane << " keys=";
    for (std::size_t i = 0; i < 2; ++i) {
      std::cout << static_cast<unsigned>(s.keys[i].buffer) << ':'
                << s.keys[i].group << ':' << s.keys[i].triangle;
      if (s.evaluated_faces[i])
        std::cout << "/evaluated" << *s.evaluated_faces[i];
      std::cout << ',';
    }
    if (s.inventory_provenance)
      std::cout << " inventory=" << *s.inventory_provenance;
    std::cout << " quad=" << q.complete << ',' << q.horizontal << ','
              << q.convex << ',' << q.nondegenerate << ',' << q.upward << ','
              << q.corner_membership << ',' << q.distinct_face_vertices << ','
              << q.incidence_valid << ',' << q.diagonal_valid
              << " checked=" << q.checked_edges << ',' << q.checked_sides << ','
              << q.checked_vertices << ',' << q.checked_windings << ','
              << q.checked_incidence << '\n'
              << std::flush;
  }
}
struct ConstructorBaseline {
  std::array<std::size_t, 12> work{};
  Condition condition{};
  bool complete{}, admitted{};
};
struct Baseline {
  std::array<std::uint64_t, 9> work{};
  std::uint64_t hash{};
  std::size_t phase_calls{};
  Condition condition{};
  BoardingRouteFootPhaseCondition phase_condition{};
  std::optional<std::size_t> side, edge;
  bool exists{}, complete{}, source_edge{};
};
struct Summary {
  ConstructorBaseline constructor;
  std::array<Baseline, 2> consumers;
};
static_assert(sizeof(Summary) < 1024);
void hash_bound(Hash& h, BoardingPlantedLegScalarBounds b) {
  h.scalar(b.lower);
  h.scalar(b.upper);
}
void hash_edge(Hash& h, const BoardingFootSiteEdgeEvidence& e) {
  for (const auto b :
       {e.signed_side, e.edge_length_squared, e.squared_margin_gap}) {
    h.scalar(b.lower);
    h.scalar(b.upper);
    h.add(b.supported);
  }
  h.add(e.disk_contained);
}
auto report_hash(const Diagnostic& d) -> std::uint64_t {
  Hash h;
  h.add(d.version);
  h.scalar(d.duration_seconds);
  for (const auto& r : d.request.root)
    for (const auto& c : r.coordinates) {
      h.add(c.count);
      for (const auto t : c.terms)
        h.scalar(t);
    }
  for (const auto& f : d.request.feet) {
    for (const auto& p : f.sole)
      for (const auto& c : p.coordinates) {
        h.add(c.count);
        for (const auto t : c.terms)
          h.scalar(t);
      }
    for (const auto x : f.yaw_half)
      h.scalar(x);
    h.scalar(f.swing_height_metres);
  }
  for (const auto x : d.request.root_yaw_half)
    h.scalar(x);
  for (const auto x : d.request.torso_lean_half)
    h.scalar(x);
  for (const auto x : d.request.port_reaction_fraction)
    h.scalar(x);
  h.scalar(d.request.seconds_per_parameter);
  for (const auto& pair : {d.com_xz, d.barycenter_xz, d.delta_xz})
    for (const auto b : pair)
      hash_bound(h, b);
  for (std::size_t side = 0; side < 2; ++side) {
    for (const auto b : d.boot_centers_xz[side])
      hash_bound(h, b);
    const auto& s = d.sites[side];
    for (const auto b : s.pressure_xz)
      hash_bound(h, b);
    h.scalar(d.source_planes[side]);
    h.scalar(d.reactions[side]);
    h.add(s.loaded);
    h.add(s.plane_identity);
    h.add(s.evaluated);
    h.add(s.complete);
    h.add(static_cast<unsigned>(s.sole_status));
    h.add(static_cast<unsigned>(s.source_status));
    for (std::size_t edge = 0; edge < 4; ++edge) {
      h.add(s.sole_evaluated[edge]);
      h.add(s.source_evaluated[edge]);
      if (s.sole_evaluated[edge]) hash_edge(h, s.sole_edges[edge]);
      if (s.source_evaluated[edge]) hash_edge(h, s.source_edges[edge]);
    }
  }
  for (const auto n : counts(d))
    h.add(n);
  h.add(d.work.phase_calls);
  h.add(d.arithmetic_supported);
  h.add(d.kinematic_complete);
  h.add(d.constant_state);
  h.add(d.projection_complete);
  h.add(d.nominal_equilibrium);
  h.add(d.finite_contact_supported);
  h.add(d.nominal_load_supported);
  h.add(d.complete);
  h.add(static_cast<unsigned>(d.stop_condition));
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    h.add(static_cast<unsigned>(r.condition));
    h.add(r.side.value_or(2));
    h.add(r.edge.value_or(4));
    h.add(r.source_edge);
    hash_bound(h, r.limiting_bound);
    h.add(static_cast<unsigned>(r.phase.condition));
    h.add(static_cast<unsigned>(r.phase.predicate_condition));
    h.scalar(r.phase.first);
    h.scalar(r.phase.last);
    h.add(r.phase.side.value_or(2));
    hash_bound(h, r.phase.limiting_bound);
  }
  return h.value;
}
auto summarize(const Diagnostic& d) -> Baseline {
  Baseline b;
  b.exists = true;
  b.complete = d.complete;
  b.work = counts(d);
  b.hash = report_hash(d);
  b.phase_calls = d.work.phase_calls;
  if (d.first_refusal) {
    b.condition = d.first_refusal->condition;
    b.phase_condition = d.first_refusal->phase.condition;
    b.side = d.first_refusal->side;
    b.edge = d.first_refusal->edge;
    b.source_edge = d.first_refusal->source_edge;
  }
  return b;
}
void report_accounting(const Diagnostic& d, const Limits& l) {
  denied(d);
  const auto n = counts(d), ceilings = caps(l);
  for (std::size_t i = 0; i < 8; ++i) {
    check(n[i] <= ceilings[i],
          "Consumer actual work is below its isolated cap");
    totals.work[i] += n[i];
  }
  totals.phase_calls += d.work.phase_calls;
  const auto output_refusal = d.first_refusal && d.first_refusal->condition ==
                                                     Condition::output_capacity;
  check(d.work.phase_calls <= 1 && d.work.phase.graphs <= d.work.phase_calls &&
            d.output_bytes <= kBoardingIntermediatePauseOutputBytes &&
            (d.output_bytes <= l.output_bytes ||
             (output_refusal && d.work.phase_calls == 0)),
        "One fresh compiler call and bounded compact output");
  check(!d.complete || (d.kinematic_complete && d.constant_state &&
                        d.projection_complete && d.arithmetic_supported &&
                        d.nominal_equilibrium && d.finite_contact_supported &&
                        d.nominal_load_supported && !d.first_refusal),
        "Complete nominal pause requires every current source/load predicate");
  std::size_t edges{};
  for (const auto& s : d.sites) {
    edges +=
        static_cast<std::size_t>(std::ranges::count(s.sole_evaluated, true));
    edges +=
        static_cast<std::size_t>(std::ranges::count(s.source_evaluated, true));
    check(!s.complete || (s.loaded && s.plane_identity && s.evaluated &&
                          s.sole_status == Status::contained &&
                          s.source_status == Status::contained),
          "Each completed loaded site retains actual sole and finite-source "
          "disks");
  }
  check(edges == d.work.disk_edges,
        "Retained evaluated edges match actual charged work");
  if (d.kinematic_complete) {
    check(d.duration_seconds == 2 && d.request.seconds_per_parameter == 2 &&
              d.reactions == std::array{.25, .75},
          "Fresh phase4 hold retains exact two-second complementary reactions");
    constexpr std::array root{std::array{.16, .16, -.0075},
                              std::array{.847, .012, -.359},
                              std::array{-.55, -.17, .19}};
    for (const auto& endpoint : d.request.root)
      for (std::size_t a = 0; a < 3; ++a)
        check(endpoint.coordinates[a].count == 3 &&
                  endpoint.coordinates[a].terms == root[a],
              "Both root endpoints retain the exact registered three-term E "
              "expression");
    check(d.request.root_yaw_half == std::array{.125, .125} &&
              d.request.torso_lean_half == std::array{-.30, -.30} &&
              d.request.port_reaction_fraction == std::array{.25, .25},
          "No new yaw, lean or reaction policy enters the fixed phase4 pause");
    for (std::size_t side = 0; side < 2; ++side) {
      bool equal = true;
      for (std::size_t a = 0; a < 3; ++a) {
        const auto& c0 = d.request.feet[side].sole[0].coordinates[a];
        const auto& c1 = d.request.feet[side].sole[1].coordinates[a];
        equal = equal && c0.count == c1.count && c0.terms == c1.terms;
      }
      check(equal && d.request.feet[side].swing_height_metres == 0 &&
                d.request.feet[side].yaw_half == std::array{0., 0.},
            "Whole pause feet have identical expressions and zero yaw/hump");
      const auto& sole = d.request.feet[side].sole[0].coordinates;
      check(sole[0].count == (side == 0 ? 3U : 2U) &&
                sole[0].terms == (side == 0 ? std::array{.16, -.14, .16}
                                            : std::array{.16, .14, 0.}) &&
                sole[1].count == 1 &&
                sole[1].terms[0] ==
                    static_cast<double>(side == 0 ? -230000 : -160000) * 1e-6 &&
                sole[2].count == 1 &&
                sole[2].terms[0] == (side == 0 ? -1.14 : -.8),
            "Port intermediate and original loaded star use unchanged exact "
            "source-plane leaves");
    }
  }
}
void print_report(std::size_t ordinal,
                  const std::expected<Diagnostic, std::string>& r) {
  std::cout << "FIRST_INTERMEDIATE_PAUSE_ASSESSMENT ordinal=" << ordinal
            << " accepted=" << r.has_value();
  if (!r) {
    std::cout << " error=" << r.error() << '\n' << std::flush;
    return;
  }
  const auto& d = *r;
  std::cout << std::setprecision(17) << " reverse=" << d.reverse
            << " interval=0,1 duration=" << d.duration_seconds
            << " complete=" << d.complete
            << " arithmetic=" << d.arithmetic_supported
            << " graph=" << d.kinematic_complete
            << " constant=" << d.constant_state
            << " projection=" << d.projection_complete
            << " equilibrium=" << d.nominal_equilibrium
            << " contact=" << d.finite_contact_supported
            << " load=" << d.nominal_load_supported
            << " stop=" << static_cast<unsigned>(d.stop_condition)
            << " calls=" << d.work.phase_calls << " output=" << d.output_bytes
            << " work=";
  for (const auto n : counts(d))
    std::cout << n << ',';
  for (std::size_t s = 0; s < 2; ++s)
    std::cout << " site" << s << '='
              << static_cast<unsigned>(d.sites[s].sole_status) << ','
              << static_cast<unsigned>(d.sites[s].source_status)
              << ":P=" << d.sites[s].pressure_xz[0].lower << ','
              << d.sites[s].pressure_xz[0].upper << ','
              << d.sites[s].pressure_xz[1].lower << ','
              << d.sites[s].pressure_xz[1].upper
              << ":plane=" << d.source_planes[s]
              << ":reaction=" << d.reactions[s];
  if (d.first_refusal) {
    const auto& f = *d.first_refusal;
    std::cout << " refusal=" << static_cast<unsigned>(f.condition)
              << " phase=" << static_cast<unsigned>(f.phase.condition);
    if (f.side) std::cout << " side=" << *f.side;
    if (f.edge) std::cout << " edge=" << *f.edge;
    std::cout << " source_edge=" << f.source_edge
              << " bound=" << f.limiting_bound.lower << ','
              << f.limiting_bound.upper << " phase_interval=" << f.phase.first
              << ',' << f.phase.last;
  }
  std::cout << '\n' << std::flush;
}
[[gnu::noinline]] void first_oracle(const Diagnostic& d) {
  if (!d.kinematic_complete || !d.projection_complete) return;
  charge(totals.oracles, 2);
  const auto o = body_oracle();
  for (std::size_t side = 0; side < 2; ++side) {
    const auto base = side == 0 ? 4U : 11U;
    const auto thigh = subtract(o.points[base + 1], o.points[base]);
    const auto shin = subtract(o.points[base + 2], o.points[base + 1]);
    const auto l1 = static_cast<long double>(.47285),
               l2 = static_cast<long double>(.47478);
    check(std::abs(dot(thigh, thigh) - l1 * l1) <
                  64 * std::numeric_limits<long double>::epsilon() &&
              std::abs(dot(shin, shin) - l2 * l2) <
                  64 * std::numeric_limits<long double>::epsilon(),
          "Independent unfactored three-dimensional knees preserve both rods");
    for (std::size_t axis = 0; axis < 2; ++axis) {
      const auto a = axis == 0 ? 0U : 2U;
      check(enclosed(d.com_xz[axis], o.com[a]) &&
                enclosed(d.boot_centers_xz[side][axis], o.points[base + 3][a]),
            "Original eighteen points and fifteen masses enclose current "
            "COM/boots");
    }
  }
  if (!d.projection_complete || !d.nominal_equilibrium) return;
  std::array<Point, 2> pressure;
  Point barycenter{}, delta{};
  for (std::size_t a = 0; a < 3; ++a) {
    barycenter[a] = .25L * o.soles[0][a] + .75L * o.soles[1][a];
    delta[a] = o.com[a] - barycenter[a];
    pressure[0][a] = o.soles[0][a] + delta[a];
    pressure[1][a] = o.soles[1][a] + delta[a];
  }
  for (std::size_t axis = 0; axis < 2; ++axis) {
    const auto a = axis == 0 ? 0U : 2U;
    check(enclosed(d.barycenter_xz[axis], barycenter[a]) &&
              enclosed(d.delta_xz[axis], delta[a]),
          "Independent current COM and complementary barycenter/offset are "
          "enclosed");
    const auto moment = .25L * pressure[0][a] + .75L * pressure[1][a];
    check(std::abs(moment - o.com[a]) <=
              64 * std::numeric_limits<long double>::epsilon(),
          "Nominal positive complementary reactions preserve X/Z moments");
    for (std::size_t side = 0; side < 2; ++side)
      check(enclosed(d.sites[side].pressure_xz[axis], pressure[side][a]),
            "Independent shared-offset pressure expression is enclosed");
  }
  constexpr std::array signs{std::pair{1.L, -1.L}, std::pair{-1.L, -1.L},
                             std::pair{-1.L, 1.L}, std::pair{1.L, 1.L}};
  for (std::size_t side = 0; side < 2; ++side) {
    const auto* source = Access::partition(d.source, side);
    check(source != nullptr,
          "Genuine retained source owns the nominated finite perimeter");
    if (!source) continue;
    std::array<Point, 4> sole;
    for (std::size_t e = 0; e < 4; ++e)
      sole[e] =
          add(o.soles[side], {signs[e].first * static_cast<long double>(.06), 0,
                              signs[e].second * static_cast<long double>(.14)});
    for (std::size_t e = 0; e < 4; ++e) {
      if (d.sites[side].sole_evaluated[e])
        edge_oracle(d.sites[side].sole_edges[e], sole[e], sole[(e + 1) % 4],
                    pressure[side]);
      if (d.sites[side].source_evaluated[e])
        edge_oracle(
            d.sites[side].source_edges[e], wide(source->perimeter_metres[e]),
            wide(source->perimeter_metres[(e + 1) % 4]), pressure[side]);
    }
  }
}
[[gnu::noinline]] void first_constructor(const NativeCraftBinding& binding,
                                         const OriginBoardingBootSupport& boots,
                                         std::optional<Provider>& provider,
                                         ConstructorBaseline& baseline) {
  Evidence evidence;
  charge(totals.constructors, 64);
  auto result = Access::make(binding, boots, {}, &evidence);
  print_constructor(result.has_value(),
                    result ? std::string_view{} : result.error(), evidence);
  constructor_accounting(evidence, {});
  baseline = {counts(evidence), evidence.condition, evidence.complete,
              result.has_value()};
  check(result.has_value() == evidence.complete,
        "Genuine creator admission and structured proof agree without retry");
  if (result) {
    provider.emplace(std::move(*result));
    check(Access::valid(*provider) && provider->summary() &&
              provider->summary()->version == 1,
          "Genuine constructor alone issues an immutable provider");
  }
}
[[gnu::noinline]] void first_assessment(const Provider& provider, bool reverse,
                                        Baseline& baseline) {
  charge(totals.consumers, 64);
  const auto result =
      assess_origin_boarding_intermediate_pause_support(provider, reverse);
  print_report(reverse ? 2U : 1U, result);
  if (!result) return;
  report_accounting(*result, {});
  first_oracle(*result);
  baseline = summarize(*result);
  check(result->reverse == reverse,
        "Fresh constant assessment retains requested reversal");
}
constexpr std::array<Condition, 12> constructor_capacity{
    Condition::source_capacity,    Condition::base_capacity,
    Condition::metadata_capacity,  Condition::face_capacity,
    Condition::quad_capacity,      Condition::quad_edge_capacity,
    Condition::quad_side_capacity, Condition::vertex_capacity,
    Condition::corner_capacity,    Condition::winding_capacity,
    Condition::incidence_capacity, Condition::diagonal_capacity};
[[gnu::noinline]] void constructor_one(const NativeCraftBinding& binding,
                                       const OriginBoardingBootSupport& boots,
                                       ConstructorLimits limits,
                                       const ConstructorBaseline& first,
                                       std::optional<Condition> expected = {},
                                       bool same = false, bool stale = false) {
  Evidence evidence;
  if (stale) {
    evidence.version = 999;
    evidence.complete = true;
    evidence.arithmetic_supported = true;
    evidence.identity_valid.fill(true);
    evidence.geometry_evaluated.fill(true);
    evidence.actual_source_bytes = 999;
  }
  charge(totals.constructors, 64);
  const auto result = Access::make(binding, boots, limits, &evidence);
  constructor_accounting(evidence, limits);
  check(evidence.version == 1 && result.has_value() == evidence.complete,
        "Every creator replay resets evidence and earns current admission");
  if (expected)
    check(!result && !evidence.complete && evidence.condition == *expected,
          "Reached constructor capacity or invalid binding retains exact "
          "refusal");
  if (same)
    check(result.has_value() == first.admitted &&
              evidence.complete == first.complete &&
              evidence.condition == first.condition &&
              counts(evidence) == first.work,
          "Exact reached source budgets replay the genuine first outcome "
          "fieldwise");
  if (expected == Condition::invalid_limits)
    check(evidence.actual_source_bytes == 0 && evidence.work.base_guards == 0 &&
              !evidence.arithmetic_supported,
          "Raised limits clear stale authority before source work/allocation");
}
[[gnu::noinline]] void constructor_roster(
    const NativeCraftBinding& binding, const OriginBoardingBootSupport& boots,
    const ConstructorBaseline& first) {
  ConstructorLimits exact;
  for (std::size_t i = 0; i < 12; ++i)
    if (first.work[i]) exact.*constructor_members[i] = first.work[i];
  constructor_one(binding, boots, exact, first, {}, true); // C02.
  for (std::size_t i = 0; i < 12; ++i) {                   // C03–C14.
    ConstructorLimits l;
    l.*constructor_members[i] = first.work[i];
    constructor_one(binding, boots, l, first, {}, first.work[i] != 0);
  }
  for (std::size_t i = 0; i < 12; ++i) { // C15–C26.
    ConstructorLimits l;
    l.*constructor_members[i] = 0;
    constructor_one(binding, boots, l, first,
                    first.work[i] ? std::optional{constructor_capacity[i]}
                                  : std::nullopt);
  }
  for (std::size_t i = 0; i < 12; ++i) { // C27–C38; zero duplicates skip.
    if (first.work[i] <= 1) continue;
    ConstructorLimits l;
    l.*constructor_members[i] = first.work[i] - 1;
    constructor_one(binding, boots, l, first, constructor_capacity[i]);
  }
  for (std::size_t i = 0; i < 12; ++i) { // C39–C50.
    ConstructorLimits l;
    ++(l.*constructor_members[i]);
    constructor_one(binding, boots, l, first, Condition::invalid_limits, false,
                    true);
  }
  for (std::size_t mode = 0; mode < 6; ++mode) { // C51–C56.
    const Environment environment(mode);
    if (!environment.available) continue;
    constructor_one(binding, boots, {}, first,
                    Condition::unsupported_arithmetic);
  }
}
[[gnu::noinline]] void constructor_binding_roster(
    const NativeCraftBinding& binding, const OriginBoardingBootSupport& boots,
    const ConstructorBaseline& first) {
  {
    auto empty = binding;
    const auto retained = std::move(empty);
    // NOLINTBEGIN(bugprone-use-after-move) -- The empty binding must refuse the
    // issuer while its copied genuine source remains retained.
    constructor_one(empty, boots, {}, first,
                    Condition::invalid_binding); // C58.
    // NOLINTEND(bugprone-use-after-move) -- End intentional empty-handle
    // control.
    check(retained.contact() != nullptr,
          "Moving binding retains its genuine source");
  }
  {
    auto empty = boots;
    const auto retained = std::move(empty);
    // NOLINTBEGIN(bugprone-use-after-move) -- A moved-from immutable boot
    // provider cannot authenticate the current pause source.
    constructor_one(binding, empty, {}, first,
                    Condition::invalid_binding); // C59.
    // NOLINTEND(bugprone-use-after-move) -- End intentional empty-handle
    // control.
    check(retained.selected_partitions().size() == 10,
          "Moving boots preserves ten legacy partitions");
  }
  { // C60; one public wrapper call, no private retry.
    const auto other = make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{});
    if (!other) throw std::runtime_error(other.error());
    check(other->contact() && binding.contact() &&
              other->contact()->original_geometry() !=
                  binding.contact()->original_geometry(),
          "Mismatch control uses independently owned genuine original source");
    charge(totals.constructors, 64);
    const auto result =
        make_origin_boarding_intermediate_pause_support(*other, boots);
    check(!result,
          "Public issuer rejects differently owned genuine source identity");
  }
  for (std::size_t i = 0; i < 4; ++i) { // C61–C64, existing input API only.
    NativeStartingAssemblySelection selection;
    if (i == 0) selection.hardware.roof_transfer = 0;
    if (i == 1) selection.hardware.inner_door = 0;
    if (i == 2) selection.hardware.seat_boarding = 0;
    if (i == 3) selection.hardware.station_closure = 1;
    const auto wrong = make_native_starting_assembly_binding(selection);
    if (wrong)
      constructor_one(*wrong, boots, {}, first, Condition::invalid_binding);
    else
      check(
          !wrong.error().empty(),
          "Invalid hardware prerequisite refuses without forged issuer input");
  }
}
constexpr std::array<BoardingRouteFootPhaseCondition, 5> phase_capacity{
    BoardingRouteFootPhaseCondition::graph_capacity,
    BoardingRouteFootPhaseCondition::leg_capacity,
    BoardingRouteFootPhaseCondition::body_capacity,
    BoardingRouteFootPhaseCondition::sector_capacity,
    BoardingRouteFootPhaseCondition::timing_capacity};
[[gnu::noinline]] void assessment_one(const Provider& provider, bool reverse,
                                      Limits limits, const Baseline& first,
                                      bool same = false,
                                      std::optional<std::size_t> reduced = {},
                                      bool invalid = false,
                                      bool unsafe = false) {
  charge(totals.consumers, 64);
  const auto result =
      detail::intermediate_pause_support_bounded(provider, reverse, limits);
  if (invalid) {
    if (Access::valid(provider))
      check(!result, "Overmaximum limits refuse before fresh graph");
    else if (result) {
      report_accounting(*result, limits);
      check(result->first_refusal &&
                result->first_refusal->condition ==
                    Condition::invalid_binding &&
                !result->complete && !result->nominal_load_supported &&
                result->work.phase_calls == 0 &&
                result->work.projection_guards == 0 &&
                result->work.pressure_candidates == 0 &&
                result->work.disk_edges == 0,
            "Moved provider retains honest invalid-binding refusal before any "
            "graph/load work");
    } else
      check(!result.error().empty(),
            "Empty provider retains a precise API refusal");
    return;
  }
  if (!result) {
    check(unsafe || (reduced && *reduced == 8),
          "Only unsupported environment or fixed output refusal precedes "
          "diagnostic");
    return;
  }
  const auto& d = *result;
  report_accounting(d, limits);
  if (unsafe) {
    check(!d.arithmetic_supported && !d.complete && d.work.phase.graphs == 0 &&
              !d.nominal_load_supported,
          "Unsafe arithmetic cannot certify or execute a geometric graph");
    return;
  }
  if (same && first.exists)
    check(report_hash(d) == first.hash,
          "Exact caps/copy/lifetime/reversal retain semantic source and "
          "pressure evidence");
  if (!reduced || !first.exists || first.work[*reduced] == 0) return;
  const auto field = *reduced;
  check(!d.complete && d.first_refusal.has_value(),
        "Genuinely reached reduced work refuses current completeness");
  if (!d.first_refusal) return;
  if (field < 5)
    check(d.first_refusal->condition == Condition::phase_prerequisite &&
              d.first_refusal->phase.condition == phase_capacity[field],
          "Inherited phase capacity preserves its actual original cause");
  else {
    const std::array reasons{
        Condition::projection_capacity, Condition::pressure_capacity,
        Condition::edge_capacity, Condition::output_capacity};
    check(d.stop_condition == reasons[field - 5],
          "Actual later stage stop is retained independently from earlier disk "
          "finding");
    const auto first_edge = first.side.value_or(2) * 8 +
                            (first.source_edge ? 4U : 0U) +
                            first.edge.value_or(4);
    const bool prior_disk = field == 7 &&
                            (first.condition == Condition::sole_disk ||
                             first.condition == Condition::source_disk) &&
                            first_edge < limits.disk_edges;
    if (prior_disk)
      check(d.first_refusal->condition == first.condition &&
                d.first_refusal->side == first.side &&
                d.first_refusal->edge == first.edge &&
                d.first_refusal->source_edge == first.source_edge,
            "Later capacity stop preserves earlier actual failed disk witness");
    else
      check(d.first_refusal->condition == reasons[field - 5],
            "Reduced new-stage capacity exposes its own reached obligation");
  }
  if (field == 8)
    check(d.output_bytes > limits.output_bytes && d.work.phase_calls == 0,
          "Fixed output refusal retains required bytes without geometric work");
  else
    check(counts(d)[field] == caps(limits)[field],
          "Reduced stage consumes exactly its allowed prefix before next "
          "operation");
}
[[gnu::noinline]] void consumer_roster(const Provider& provider,
                                       const std::array<Baseline, 2>& first) {
  const auto& forward = first[0];
  Limits exact;
  for (std::size_t i = 0; i < 9; ++i)
    if (forward.work[i]) set_cap(exact, i, forward.work[i]);
  assessment_one(provider, false, exact, forward, true); // A03.
  assessment_one(provider, true, exact, first[1], true); // A04.
  for (std::size_t i = 0; i < 9; ++i) {                  // A05–A13.
    Limits l;
    set_cap(l, i, forward.work[i]);
    assessment_one(provider, false, l, forward, forward.work[i] != 0);
  }
  for (std::size_t i = 0; i < 9; ++i) { // A14–A22.
    Limits l;
    set_cap(l, i, 0);
    assessment_one(provider, false, l, forward, false, i);
  }
  for (std::size_t i = 0; i < 9; ++i) { // A23–A31, zero duplicates skip.
    if (forward.work[i] <= 1) continue;
    Limits l;
    set_cap(l, i, forward.work[i] - 1);
    assessment_one(provider, false, l, forward, false, i);
  }
  for (std::size_t i = 0; i < 9; ++i) { // A32–A40.
    Limits l;
    set_cap(l, i, caps(l)[i] + 1);
    assessment_one(provider, false, l, forward, false, {}, true);
  }
  for (std::size_t mode = 0; mode < 6; ++mode) { // A41–A46.
    const Environment environment(mode);
    if (environment.available)
      assessment_one(provider, false, {}, forward, false, {}, false, true);
  }
  {
    auto empty = provider;
    const auto retained = std::move(empty);
    // NOLINTBEGIN(bugprone-use-after-move) -- Moved-from provider must refuse
    // without accepting its stale former source summary.
    assessment_one(empty, false, {}, forward, false, {}, true); // A47.
    // NOLINTEND(bugprone-use-after-move) -- End moved-from provider contract.
    assessment_one(retained, false, {}, forward, true); // A48.
  }
  {
    std::optional<Provider> survivor;
    {
      // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Copy an
      // owner to test survival after its destruction.
      const auto original = provider;
      survivor.emplace(original);
    }
    assessment_one(*survivor, false, {}, forward, true); // A49.
    check(survivor->summary() &&
              survivor->summary()->sources[0].name ==
                  "CRAFT | pilot transition intermediate step",
          "Retained source/name views survive original caller destruction");
  }
  assessment_one(provider, false, {}, forward, true); // A50 after C57 failure.
  constexpr std::array precedence{0U, 1U, 3U, 3U, 4U, 5U, 6U};
  for (std::size_t pair = 0; pair < 7; ++pair) { // A51–A64.
    Limits l;
    set_cap(l, pair, 0);
    set_cap(l, pair + 1, 0);
    assessment_one(provider, false, l, forward, false, precedence[pair]);
    assessment_one(provider, true, l, first[1], false, precedence[pair]);
  }
}
void rebuild_faces(BoardingBootSourcePartition& p) {
  const auto& q = p.perimeter_metres;
  p.faces[0].points_current_metres = {q[0], q[1], q[2]};
  p.faces[1].points_current_metres = {q[0], q[2], q[3]};
  for (auto& face : p.faces)
    face.unit_normal_current = {0, 1, 0};
}
auto fixture(const OriginBoardingBootSupport& boots) -> Fixture {
  Fixture f;
  auto& p = f.partitions[0];
  p.plane_metres = static_cast<double>(-230000) * 1e-6;
  const auto corner = [&](int x, int z) {
    return RigidVector3{static_cast<double>(x) * 1e-6, p.plane_metres,
                        static_cast<double>(z) * 1e-6};
  };
  p.perimeter_metres = {corner(550000, -1346840), corner(-550000, -1346840),
                        corner(-550000, -1183160), corner(550000, -1183160)};
  for (std::size_t i = 0; i < 2; ++i) {
    auto& face = p.faces[i];
    face.key = {LowerCockpitContactBuffer::halo, 0,
                static_cast<std::uint32_t>(662 + i)};
    face.object = 10;
    face.source_object = "CRAFT | pilot transition intermediate step";
    face.evaluated_source_triangle = static_cast<std::uint32_t>(182 + i);
  }
  rebuild_faces(p);
  f.partitions[1] = boots.selected_partitions()[9];
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& s = f.partitions[side];
    auto& id = f.identities[side];
    id.name = s.faces[0].source_object;
    id.object = s.faces[0].object;
    id.plane = s.plane_metres;
    for (std::size_t i = 0; i < 2; ++i) {
      id.keys[i] = s.faces[i].key;
      id.evaluated_faces[i] = s.faces[i].evaluated_source_triangle;
    }
    if (side == 0) id.inventory_provenance = 735;
  }
  return f;
}
void mutate(Fixture& f, std::size_t side, std::size_t change) {
  auto& p = f.partitions[side];
  auto& id = f.identities[side];
  switch (change) {
    case 0: break;
    case 1:
      p.faces[0].points_current_metres[0].x =
          std::numeric_limits<double>::quiet_NaN();
      break;
    case 2:
      p.plane_metres = std::nextafter(p.plane_metres,
                                      std::numeric_limits<double>::infinity());
      break;
    case 3:
      p.perimeter_metres[1] = p.perimeter_metres[0];
      rebuild_faces(p);
      break;
    case 4:
      p.perimeter_metres[2].x = 0;
      p.perimeter_metres[2].z =
          p.perimeter_metres[0].z +
          .25 * (p.perimeter_metres[3].z - p.perimeter_metres[0].z);
      rebuild_faces(p);
      break;
    case 5:
      std::swap(p.perimeter_metres[1], p.perimeter_metres[3]);
      rebuild_faces(p);
      break;
    case 6: p.faces[1] = p.faces[0]; break;
    case 7:
      p.faces[1].points_current_metres = {
          p.perimeter_metres[0], p.perimeter_metres[1], p.perimeter_metres[3]};
      break;
    case 8:
      p.faces[1].points_current_metres[2] = p.faces[1].points_current_metres[1];
      break;
    case 9:
      id.keys[0].buffer = side == 0 ? LowerCockpitContactBuffer::original
                                    : LowerCockpitContactBuffer::halo;
      break;
    case 10: ++id.keys[0].group; break;
    case 11: ++id.keys[0].triangle; break;
    case 12: ++id.object; break;
    case 13:
      id.evaluated_faces[0] =
          side == 0 ? std::nullopt : std::optional<std::uint32_t>{182};
      break;
    case 14: id.name = "Wrong source category/name"; break;
    default: p.faces[0].points_current_metres[0].x = 9; break;
  }
}
[[gnu::noinline]] void source_math_roster(const Fixture& original) {
  for (std::size_t side = 0; side < 2; ++side)
    for (std::size_t change = 0; change < 16; ++change) { // M01–M32.
      auto f = original;
      mutate(f, side, change);
      charge(totals.math, 128);
      const auto math = detail::intermediate_pause_source_math(f);
      no_authority(math);
      const auto& e = math.evidence;
      const auto& q = e.quads[side];
      constructor_accounting(e, {});
      if (change == 0)
        check(e.complete && e.identity_valid[side] && q.complete,
              "Unchanged arithmetic source clone satisfies pins and finite "
              "triangle coverage only");
      else
        check(!e.complete,
              "Malformed source clone never qualifies a genuine source");
      if (change >= 3 && change <= 8) {
        check(e.geometry_evaluated[side] && q.evaluated && !q.complete,
              "Malformed finite geometry reaches shared coverage kernel "
              "independently of pins");
        if (change == 3)
          check(!q.nondegenerate && q.checked_edges > 0,
                "Repeated corner reaches edge nondegeneracy");
        if (change == 4)
          check(!q.convex && q.checked_sides > 0,
                "Concave corner reaches strict convex-side predicates");
        if (change == 5)
          check(!q.upward && q.checked_windings > 0,
                "Reversed winding reaches actual emitted triangle winding");
        if (change == 6)
          check(!q.incidence_valid && q.checked_incidence > 0,
                "Duplicated face reaches genuine corner-incidence counts");
        if (change == 7)
          check(!q.diagonal_valid && q.checked_incidence > 0,
                "Adjacent shared edge cannot substitute for opposite-corner "
                "diagonal");
        if (change == 8)
          check(!q.distinct_face_vertices && q.checked_vertices > 0,
                "Repeated face corner reaches distinct-corner predicate");
      }
      if (change == 2)
        check(e.identity_checked[side] && !e.identity_valid[side] &&
                  e.geometry_evaluated[side] && !q.horizontal,
              "One-ULP decoded source plane mismatch is never repaired by "
              "proximity");
      if (change >= 9 && change <= 14)
        check(e.identity_checked[side] && !e.identity_valid[side] &&
                  e.geometry_evaluated[side] && q.complete,
              "Metadata/key-only mutation fails identity while unchanged "
              "geometry remains explicit");
    }
}
auto bound(double x) -> BoardingPlantedLegScalarBounds {
  return {x, x};
}
[[gnu::noinline]] void algebra_roster() {
  constexpr std::array weights{0., .25, 1.};
  constexpr std::array coms{std::array{0., 0.}, std::array{.25, .25},
                            std::array{1., 1.}, std::array{-1., -1.}};
  for (const auto w : weights)
    for (std::size_t orientation = 0; orientation < 2; ++orientation)
      for (const auto m : coms) { // M33–M56.
        const std::array<BoardingPlantedLegScalarBounds, 2> com{bound(m[0]),
                                                                bound(m[1])};
        const std::array<std::array<BoardingPlantedLegScalarBounds, 2>, 2>
            centers{std::array{bound(0), bound(0)},
                    std::array{bound(orientation == 0 ? 1. : 0.),
                               bound(orientation == 0 ? 0. : 1.)}};
        charge(totals.math, 128);
        const auto e =
            detail::intermediate_pause_pressure_math(com, centers, bound(w));
        no_authority(e);
        check(e.arithmetic_supported,
              "Registered finite algebra fixture is supported");
        for (std::size_t a = 0; a < 2; ++a) {
          const auto c1 = static_cast<long double>(centers[1][a].lower);
          const auto B = (1 - static_cast<long double>(w)) * c1;
          const auto delta = static_cast<long double>(m[a]) - B;
          check(enclosed(e.barycenter_xz[a], B) &&
                    enclosed(e.delta_xz[a], delta) &&
                    enclosed(e.pressure_xz[0][a], delta) &&
                    enclosed(e.pressure_xz[1][a], c1 + delta),
                "Independent complementary barycenter and common-offset "
                "pressures are enclosed");
          check(std::abs(static_cast<long double>(w) * delta +
                         (1 - static_cast<long double>(w)) * (c1 + delta) -
                         m[a]) <=
                    32 * std::numeric_limits<long double>::epsilon(),
                "Exact raw endpoint/complementary shares preserve both nominal "
                "moments");
        }
      }
}
auto rotate(RigidVector3 p, std::size_t quarter) -> RigidVector3 {
  switch (quarter) {
    case 0: return p;
    case 1: return {-p.z, p.y, p.x};
    case 2: return {-p.x, p.y, -p.z};
    default: return {p.z, p.y, -p.x};
  }
}
auto square(double halfz) -> std::array<RigidVector3, 4> {
  return {RigidVector3{1, 0, -halfz}, RigidVector3{-1, 0, -halfz},
          RigidVector3{-1, 0, halfz}, RigidVector3{1, 0, halfz}};
}
void disk_accounting(const detail::BoardingIntermediatePauseDiskMath& e,
                     std::size_t cap = 4) {
  no_authority(e);
  check(e.edge_checks <= cap &&
            e.edge_checks ==
                static_cast<std::size_t>(std::ranges::count(e.evaluated, true)),
        "Raw disk charges exact retained edge prefix before work");
  check(
      e.status != Status::contained ||
          (e.arithmetic_supported && e.edge_checks == 4 &&
           std::ranges::all_of(
               e.edges, [](const auto& edge) { return edge.disk_contained; })),
      "Contained raw disk requires every finite edge without real source "
      "authority");
}
[[gnu::noinline]] void disk_roster() {
  const auto required =
      kBoardingBootPressureRadiusMetres + kBoardingBootDiskEdgeMarginMetres;
  for (const auto halfz : {1., .5})
    for (std::size_t quarter = 0; quarter < 4; ++quarter)
      for (std::size_t control = 0; control < 8; ++control) { // M57–M120.
        auto q = square(halfz);
        RigidVector3 center{};
        if (control >= 1 && control <= 3) center.x = 1 - required;
        if (control == 2) center.x = std::nextafter(center.x, 0.);
        if (control == 3)
          center.x =
              std::nextafter(center.x, std::numeric_limits<double>::infinity());
        if (control == 4) center.x = 1;
        if (control == 5) center.x = 1.25;
        for (auto& p : q)
          p = rotate(p, quarter);
        center = rotate(center, quarter);
        std::array pressure{bound(center.x), bound(center.z)};
        if (control == 6) pressure[0] = {1, -1};
        if (control == 7)
          pressure[1] = {std::numeric_limits<double>::quiet_NaN(), 0};
        charge(totals.math, 128);
        const auto e = detail::intermediate_pause_disk_math(q, pressure);
        disk_accounting(e);
        if (control >= 6) {
          check(!e.arithmetic_supported && e.edge_checks == 0 &&
                    e.status != Status::contained,
                "Malformed disk bounds refuse before edge products");
          continue;
        }
        check(e.arithmetic_supported && e.edge_checks == 4,
              "Finite disk fixture visits all four edges even after ordinary "
              "refusal");
        for (std::size_t edge = 0; edge < 4; ++edge)
          if (e.evaluated[edge])
            edge_oracle(e.edges[edge], wide(q[edge]), wide(q[(edge + 1) % 4]),
                        wide(center));
        if (control == 0)
          check(e.status == Status::contained,
                "Central disk certifies on each exact signed-permutation quad");
        if (control == 4 || control == 5)
          check(e.status == Status::refuted,
                "Disk centered on or beyond finite edge is genuinely refuted");
        // Boundary/one-ULP fixtures intentionally allow outward straddling.
      }
  const auto q = square(1.);
  const std::array pressure{bound(0), bound(0)};
  for (std::size_t mode = 0; mode < 6; ++mode) { // M121–M126.
    const Environment environment(mode);
    if (!environment.available) continue;
    charge(totals.math, 128);
    const auto e = detail::intermediate_pause_disk_math(q, pressure);
    no_authority(e);
    check(!e.arithmetic_supported && e.edge_checks == 0 &&
              e.status != Status::contained,
          "Unsafe environment cannot grant raw disk coverage");
  }
  for (const auto cap : {0U, 3U}) { // M127–M128.
    charge(totals.math, 128);
    const auto e = detail::intermediate_pause_disk_math(q, pressure, cap);
    disk_accounting(e, cap);
    check(e.edge_checks == cap && e.status != Status::contained &&
              e.condition == Condition::edge_capacity,
          "Reduced raw edge cap retains authentic partial prefix and exact "
          "capacity refusal");
  }
}
[[gnu::noinline]] void math_roster(const OriginBoardingBootSupport& boots,
                                   std::optional<Provider>& provider) {
  auto raw = fixture(boots);
  if (provider) {
    const auto captured = Access::fixture(*provider);
    check(captured.has_value(),
          "Genuine source fixture getter returns arithmetic records only");
    if (captured) raw = *captured;
  }
  // The copied names are anchored by the still-owned original binding/boots;
  // no issuer token or provider pointer is retained in this arithmetic record.
  provider.reset();
  source_math_roster(raw);
  algebra_roster();
  disk_roster();
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
    check(boots->selected_partitions().size() == 10,
          "Unchanged ten-partition boot provider remains retained");
    Summary summary;
    std::optional<Provider> provider;
    first_constructor(
        *binding, *boots, provider,
        summary.constructor); // C01 precedes all altered-source math.
    if (provider) {
      first_assessment(*provider, false, summary.consumers[0]); // A01.
      first_assessment(*provider, true, summary.consumers[1]);  // A02.
      if (summary.consumers[0].exists && summary.consumers[1].exists)
        check(summary.consumers[0].hash == summary.consumers[1].hash,
              "Reversal of the authentic constant hold preserves physical "
              "evidence fieldwise");
    } else
      std::cout
          << "FIRST_INTERMEDIATE_PAUSE_ASSESSMENTS NOTRUN constructor_refused\n"
          << std::flush;
    // C57 is the sole registered failed creator needed before A50. It cannot
    // allocate a second provider while FIRST's immutable arena remains owned.
    constructor_one(NativeCraftBinding{}, *boots, {}, summary.constructor,
                    Condition::invalid_binding); // C57.
    if (provider) consumer_roster(*provider, summary.consumers);
    math_roster(*boots,
                provider); // Releases FIRST arena before raw proofs/replays.
    constructor_roster(*binding, *boots, summary.constructor); // C02–C56.
    constructor_binding_roster(*binding, *boots,
                               summary.constructor); // C58–C64.
    constexpr std::array<std::uint64_t, 8> aggregate{64,  128,   64,  192,
                                                     384, 16384, 128, 1024};
    for (std::size_t i = 0; i < 8; ++i)
      check(totals.work[i] <= aggregate[i],
            "All consumer replays stay inside registered aggregate work");
    check(totals.constructors <= 64 && totals.consumers <= 64 &&
              totals.phase_calls <= 64 && totals.math <= 128 &&
              totals.oracles <= 2,
          "Exact finite manifests include all queries and only two FIRST "
          "candidate oracles");
    check(std::cout.good() && std::cerr.good() && !out.truncated &&
              !err.truncated && bytes <= 16128,
          "Shared sixteen-KiB execution stream is complete and reserves its "
          "final summary");
    std::cout << "VALIDATION constructors=" << totals.constructors
              << " consumers=" << totals.consumers << " math=" << totals.math
              << " oracles=" << totals.oracles
              << " phase_calls=" << totals.phase_calls << " work=";
    for (const auto n : totals.work)
      std::cout << n << ',';
    std::cout << " summary=" << sizeof(Summary) << " checks=" << checks
              << " failures=" << failures << " bytes_before_summary=" << bytes
              << '\n';
    std::cout.flush();
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "ERROR: " << e.what() << '\n';
  }
  const bool stream_bad =
      out.truncated || err.truncated || !std::cout.good() || !std::cerr.good();
  std::cout.rdbuf(saved_out);
  std::cerr.rdbuf(saved_err);
  return failures == 0 && !stream_bad ? 0 : 1;
}
