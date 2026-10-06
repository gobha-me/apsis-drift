#include "apsis_drift/origin_boarding_route_intermediate_hip_diagnostic01.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
#include "origin_boarding_route_intermediate_hip_diagnostic01_internal.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <expected>
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
using Access = detail::BoardingIntermediatePauseSupportAccess;
using Diagnostic = BoardingRouteIntermediateHipDiagnostic01Diagnostic;
using CaseId = BoardingRouteIntermediateHipDiagnostic01Case;
using State = BoardingRouteIntermediateHipDiagnostic01State;
using Condition = BoardingRouteIntermediateHipDiagnostic01Condition;
using Error = BoardingRouteIntermediateHipDiagnostic01Error;
using Limits = detail::BoardingRouteIntermediateHipDiagnostic01Limits;
using Scalar = BoardingFootSiteScalarBounds;
using Request = BoardingRouteFootPhaseRequest;
using PhaseCell = BoardingRouteFootPhaseCell;
std::size_t checks{};
int failures{};
void check(bool yes, std::string_view why) {
  ++checks;
  if (!yes) {
    ++failures;
    std::cerr << "FAIL: " << why << '\n';
  }
}
constexpr std::array<std::uint32_t, 14> ceilings{
    1, 2, 1, 3, 6, 67, 32, 21, 162, 243666, 162, 3240, 10206, 97524};
constexpr std::array<unsigned, 15> widths{1, 2,  1, 2,  3,  7,  6, 5,
                                          8, 18, 8, 12, 14, 17, 1};
constexpr std::array<unsigned, 15> offsets{0,  1,  3,  4,  6,  9,  16, 22,
                                           27, 35, 53, 61, 73, 87, 104};
struct Totals {
  std::array<std::uint32_t, 17> work{};
  std::uint32_t creators{}, consumers{}, raw{}, oracles{};
} totals;
static_assert(sizeof(Totals) <= 128);
void charge(std::uint32_t& n, std::uint32_t ceiling) {
  if (n >= ceiling) throw std::runtime_error("Registered roster exhausted");
  ++n;
}
void aggregate(std::size_t i, std::uint64_t n) {
  if (n > std::numeric_limits<std::uint32_t>::max() - totals.work[i])
    throw std::runtime_error("Registered aggregate overflow");
  totals.work[i] += static_cast<std::uint32_t>(n);
}
struct StreamCounter : std::streambuf {
  std::streambuf* target;
  std::size_t& bytes;
  bool truncated{};
  StreamCounter(std::streambuf* p, std::size_t& n) : target(p), bytes(n) {}
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
  auto xsputn(const char* p, std::streamsize n) -> std::streamsize override {
    std::streamsize i{};
    while (i < n &&
           !traits_type::eq_int_type(overflow(p[i]), traits_type::eof()))
      ++i;
    return i;
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
      const std::array values{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
      available = std::fesetround(values[mode]) == 0;
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
struct FirstRecord {
  std::array<std::uint64_t, 2> counts{};
  std::uint64_t hash{};
  std::uint8_t exists{}, has_report{}, state{}, condition{}, error{},
      has_slot{};
  std::array<std::uint8_t, 2> reserved{};
};
static_assert(sizeof(FirstRecord) == 32);
struct Anchor {
  std::array<std::uint32_t, 15> counts{};
  std::array<std::uint8_t, 15> cases{};
  std::array<std::uint8_t, 5> flags{};
};
static_assert(sizeof(Anchor) == 80);
struct Summary {
  std::array<FirstRecord, 6> first{};
  Anchor anchors{};
  std::uint64_t recipe_hash{};
  std::uint32_t skipped{};
  bool source_admitted{};
};
static_assert(sizeof(Summary) <= 400);
void put(FirstRecord& b, std::size_t field, std::uint64_t value) {
  const auto width = widths[field], bit = offsets[field];
  if (value >= (std::uint64_t{1} << width))
    throw std::runtime_error("Packed count overflow");
  for (unsigned i = 0; i < width; ++i) {
    const auto pos = bit + i;
    if ((value >> i) & 1U) b.counts[pos / 64] |= std::uint64_t{1} << (pos % 64);
  }
}
auto get(const FirstRecord& b, std::size_t field) -> std::uint32_t {
  std::uint32_t value{};
  for (unsigned i = 0; i < widths[field]; ++i) {
    const auto pos = offsets[field] + i;
    value |= static_cast<std::uint32_t>((b.counts[pos / 64] >> (pos % 64)) & 1U)
             << i;
  }
  return value;
}
struct Hash {
  std::uint64_t value{1469598103934665603ULL};
  void add(std::uint64_t x) {
    value ^= x;
    value *= 1099511628211ULL;
  }
  void number(double x) { add(std::bit_cast<std::uint64_t>(x == 0 ? 0.0 : x)); }
  void scalar(const Scalar& x) {
    number(x.lower);
    number(x.upper);
    add(x.supported);
  }
};
auto finite(const Scalar& x) -> bool {
  return x.supported && std::isfinite(x.lower) && std::isfinite(x.upper) &&
         x.lower <= x.upper;
}
auto point(double x) -> Scalar {
  return {x, x, true};
}
struct Ld {
  long double lo{}, hi{};
};
using LdPoint = std::array<Ld, 3>;
auto outward(long double lo, long double hi) -> Ld {
  return {std::nextafter(lo, -std::numeric_limits<long double>::infinity()),
          std::nextafter(hi, std::numeric_limits<long double>::infinity())};
}
auto ld(const Scalar& x) -> Ld {
  return {x.lower, x.upper};
}
auto ld(double x) -> Ld {
  return {x, x};
}
auto operator+(Ld a, Ld b) -> Ld {
  return outward(a.lo + b.lo, a.hi + b.hi);
}
auto operator-(Ld a) -> Ld {
  return {-a.hi, -a.lo};
}
auto operator-(Ld a, Ld b) -> Ld {
  return a + -b;
}
auto operator*(Ld a, Ld b) -> Ld {
  const std::array values{a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi};
  return outward(*std::min_element(values.begin(), values.end()),
                 *std::max_element(values.begin(), values.end()));
}
auto divide(Ld a, double b) -> Ld {
  check(b > 0 && std::isfinite(b), "Oracle positive original link length");
  return outward(a.lo / b, a.hi / b);
}
auto absolute(Ld a) -> Ld {
  if (a.lo >= 0) return a;
  if (a.hi <= 0) return -a;
  return {0, std::max(-a.lo, a.hi)};
}
auto dot(const LdPoint& a, const LdPoint& b) -> Ld {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
auto contains(const Scalar& bound, Ld x) -> bool {
  const auto allowance = 64 * std::numeric_limits<long double>::epsilon() *
                         std::max({1.0L, std::abs(x.lo), std::abs(x.hi)});
  return finite(bound) &&
         static_cast<long double>(bound.lower) <= x.lo + allowance &&
         static_cast<long double>(bound.upper) >= x.hi - allowance;
}
void hash_request(Hash& h, const Request& r) {
  for (const auto& p : r.root)
    for (const auto& c : p.coordinates) {
      h.add(c.count);
      for (auto t : c.terms)
        h.number(t);
    }
  for (const auto& f : r.feet) {
    for (const auto& p : f.sole)
      for (const auto& c : p.coordinates) {
        h.add(c.count);
        for (auto t : c.terms)
          h.number(t);
      }
    for (auto y : f.yaw_half)
      h.number(y);
    h.number(f.swing_height_metres);
  }
  for (const auto& a :
       {r.root_yaw_half, r.torso_lean_half, r.port_reaction_fraction})
    for (auto x : a)
      h.number(x);
  h.number(r.seconds_per_parameter);
}
constexpr std::array<CaseId, 6> cases{
    CaseId::point_0,    CaseId::point_quarter,
    CaseId::point_half, CaseId::point_three_quarters,
    CaseId::point_1,    CaseId::terminal_phase1_interval};
constexpr std::array<std::array<double, 2>, 6> globals{
    {{0, 0},
     {.25, .25},
     {.5, .5},
     {.75, .75},
     {1, 1},
     {.1416015625, .142578125}}};
constexpr std::array<std::size_t, 6> phases{0, 1, 2, 3, 4, 1};
constexpr std::array<std::array<double, 2>, 6> locals{
    {{0, 0}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {.1328125, .140625}}};
} // namespace
namespace {
using Geometry = BoardingRouteIntermediateHipDiagnostic01GeometryInput;
using DualCandidate = BoardingRouteIntermediateHipDiagnostic01DualCandidate;
using PrimalCandidate = BoardingRouteIntermediateHipDiagnostic01PrimalCandidate;
using DualMath = BoardingRouteIntermediateHipDiagnostic01DualMath;
using PrimalMath = BoardingRouteIntermediateHipDiagnostic01PrimalMath;
using DualLimits =
    detail::BoardingRouteIntermediateHipDiagnostic01DualMathLimits;
using PrimalLimits =
    detail::BoardingRouteIntermediateHipDiagnostic01PrimalMathLimits;
using Expected = std::expected<Diagnostic, Error>;
using Context =
    detail::BoardingRouteIntermediateHipDiagnostic01AdmissionContext;
using Token = detail::BoardingRouteIntermediateHipDiagnostic01CurrentToken;
static_assert(!std::is_default_constructible_v<Context>);
static_assert(
    !std::is_constructible_v<Context, const Diagnostic&, const Request&>);
static_assert(!std::is_default_constructible_v<Token>);
static_assert(
    !std::is_constructible_v<Token, const Context&, const Diagnostic&>);
static_assert(!Diagnostic::route_qualified && !Diagnostic::self_qualified &&
              !Diagnostic::nominal_support_qualified &&
              !Diagnostic::material_qualified && !Diagnostic::world_qualified &&
              !Diagnostic::halo_qualified && !Diagnostic::seat_qualified &&
              !Diagnostic::actor_qualified && !Diagnostic::save_qualified &&
              !Diagnostic::dynamics_qualified);
static_assert(!DualMath::source_qualified && !PrimalMath::source_qualified);
auto counts(const Diagnostic& d) -> std::array<std::uint64_t, 14> {
  return {d.phase_work.graphs,        d.phase_work.legs,
          d.phase_work.bodies,        d.phase_work.sectors,
          d.phase_work.timing,        d.work.source_guards,
          d.work.current_guards,      d.work.chart_operations,
          d.work.generator_states,    d.work.generator_operations,
          d.work.dual_trials,         d.work.primal_trials,
          d.work.verification_guards, d.work.verification_operations};
}
auto limit(const Limits& l, std::size_t i) -> std::uint64_t {
  switch (i) {
    case 0: return l.phase.graphs;
    case 1: return l.phase.legs;
    case 2: return l.phase.bodies;
    case 3: return l.phase.sectors;
    case 4: return l.phase.timing;
    case 5: return l.source_guards;
    case 6: return l.current_guards;
    case 7: return l.chart_operations;
    case 8: return l.generator_states;
    case 9: return l.generator_operations;
    case 10: return l.dual_trials;
    case 11: return l.primal_trials;
    case 12: return l.verification_guards;
    case 13: return l.verification_operations;
    default: return l.output_bytes;
  }
}
void set_limit(Limits& l, std::size_t i, std::uint64_t n) {
  switch (i) {
    case 0: l.phase.graphs = n; break;
    case 1: l.phase.legs = n; break;
    case 2: l.phase.bodies = n; break;
    case 3: l.phase.sectors = n; break;
    case 4: l.phase.timing = n; break;
    case 5: l.source_guards = n; break;
    case 6: l.current_guards = n; break;
    case 7: l.chart_operations = n; break;
    case 8: l.generator_states = n; break;
    case 9: l.generator_operations = n; break;
    case 10: l.dual_trials = n; break;
    case 11: l.primal_trials = n; break;
    case 12: l.verification_guards = n; break;
    case 13: l.verification_operations = n; break;
    default: l.output_bytes = n; break;
  }
}
constexpr std::array<Condition, 14> capacity{
    Condition::phase_capacity,
    Condition::phase_capacity,
    Condition::phase_capacity,
    Condition::phase_capacity,
    Condition::phase_capacity,
    Condition::source_guard_capacity,
    Condition::current_guard_capacity,
    Condition::chart_capacity,
    Condition::generator_state_capacity,
    Condition::generator_operation_capacity,
    Condition::dual_capacity,
    Condition::primal_capacity,
    Condition::verification_guard_capacity,
    Condition::verification_operation_capacity};
auto pop(std::uint64_t bits) -> std::size_t {
  return std::popcount(bits);
}
template <std::size_t N>
auto pop(const std::array<std::uint64_t, N>& a) -> std::size_t {
  std::size_t n{};
  for (auto bits : a)
    n += pop(bits);
  return n;
}
auto coordinate(const RigidVector3& p, std::size_t i) -> double {
  return i == 0 ? p.x : i == 1 ? p.y : p.z;
}
auto component(const BoardingPlantedLegPointBounds& p, std::size_t i)
    -> Scalar {
  return {coordinate(p.lower, i), coordinate(p.upper, i), true};
}
void hash_point(Hash& h, const BoardingPlantedLegPointBounds& p) {
  for (std::size_t i = 0; i < 3; ++i)
    h.scalar(component(p, i));
}
void hash_body_point(Hash& h, const BoardingPlantedBodyPointEvidence& p) {
  hash_point(h, p.value);
  hash_point(h, p.derivatives.velocity);
  hash_point(h, p.derivatives.acceleration);
}
void hash_geometry(Hash& h, const Geometry& g) {
  for (const auto* a : {&g.half, &g.hip, &g.axis, &g.segment})
    for (const auto& x : *a)
      h.scalar(x);
  h.scalar(g.radius);
  h.scalar(g.owner_limit);
}
void hash_dual(Hash& h, const DualMath& m) {
  for (auto x : m.candidate.normal)
    h.number(x);
  h.number(m.candidate.multiplier);
  h.add(m.primitive_evaluated);
  h.add(m.guard_evaluated);
  h.add(m.guards);
  h.add(m.operations);
  h.add(m.failed_operation);
  h.add(m.complete);
  h.add(m.arithmetic_supported);
  h.add(m.excluded);
  h.add(static_cast<unsigned>(m.condition));
  for (const auto* b : {&m.boxsum, &m.hipdot, &m.segmentmax, &m.normal_square,
                        &m.lower_bound, &m.radius_squared, &m.gap})
    h.scalar(*b);
}
void hash_primal(Hash& h, const PrimalMath& m) {
  for (auto x : m.candidate.point)
    h.number(x);
  h.number(m.candidate.segment_fraction);
  h.add(m.primitive_evaluated);
  h.add(m.guard_evaluated);
  h.add(m.guards);
  h.add(m.operations);
  h.add(m.failed_operation);
  h.add(m.complete);
  h.add(m.arithmetic_supported);
  h.add(m.unowned_interior);
  h.add(static_cast<unsigned>(m.condition));
  for (const auto& x : m.box_gaps)
    h.scalar(x);
  for (const auto* b : {&m.axial_projection, &m.axial_gap, &m.residual_squared,
                        &m.radius_squared, &m.residual_gap})
    h.scalar(*b);
}
void hash_certificate(
    Hash& h, const BoardingRouteIntermediateHipDiagnostic01Certificate& c) {
  h.add(c.index());
  if (const auto* d = std::get_if<DualMath>(&c)) hash_dual(h, *d);
  if (const auto* p = std::get_if<PrimalMath>(&c)) hash_primal(h, *p);
}
auto semantic_hash(const Diagnostic& d, std::uint64_t recipe) -> std::uint64_t {
  Hash h;
  h.add(recipe);
  h.add(static_cast<unsigned>(d.selection.case_id));
  h.add(d.selection.phase_index);
  h.number(d.selection.global_first);
  h.number(d.selection.global_last);
  h.number(d.selection.local_first);
  h.number(d.selection.local_last);
  h.number(d.selection.seconds_per_parameter);
  h.add(d.version);
  h.add(static_cast<unsigned>(d.state));
  h.add(static_cast<unsigned>(d.stop_condition));
  h.add(static_cast<unsigned>(d.first_refusal.condition));
  h.add(d.first_refusal.state);
  h.add(d.first_refusal.delta);
  h.add(d.first_refusal.alpha);
  h.add(d.first_refusal.operation);
  h.add(d.first_refusal.pivot);
  h.add(d.first_refusal.row);
  h.add(d.source_complete);
  h.add(d.phase_available);
  h.add(d.current_complete);
  h.add(d.chart_complete);
  h.add(d.certificate_assessed);
  h.add(d.excluded);
  h.add(d.unowned_interior);
  h.add(d.arithmetic_supported);
  for (auto n : d.source_evaluated)
    h.add(n);
  for (auto n : d.states_evaluated)
    h.add(n);
  for (auto n : d.states_skipped)
    h.add(n);
  h.add(d.current_evaluated);
  h.add(d.chart_evaluated);
  h.add(d.last_state);
  h.add(d.last_delta);
  h.add(d.last_alpha);
  h.add(d.last_pivot);
  h.add(d.last_row);
  h.add(d.skipped_systems);
  h.add(d.skipped_proposals);
  h.add(static_cast<unsigned>(d.phase_refusal.condition));
  h.add(static_cast<unsigned>(d.phase_refusal.predicate_condition));
  h.add(d.phase_refusal.side.value_or(255));
  if (d.phase_available && !d.current_phase.empty()) {
    const auto& c = d.current_phase.front();
    h.number(c.first);
    h.number(c.last);
    h.add(c.complete);
    h.add(c.arithmetic_supported);
    h.add(c.nominal_links);
    h.add(c.target_sole_identities);
    h.add(c.joint_sectors);
    h.add(c.derivative_domains);
    h.add(c.timing_complete);
    if (c.complete) {
      for (const auto& p : c.points)
        hash_body_point(h, p);
      for (const auto& p : c.mass_points)
        hash_body_point(h, p);
      hash_body_point(h, c.center_of_mass);
      for (const auto& frame : c.frames)
        for (const auto& p : frame.columns)
          hash_body_point(h, p);
    }
  }
  if (d.chart_complete) hash_geometry(h, d.geometry);
  hash_certificate(h, d.winner);
  hash_certificate(h, d.last_partial);
  return h.value;
}
template <class Math> void math_masks(const Math& m, std::size_t operations) {
  check(m.guards <= 3 && m.operations <= operations,
        "Finite verifier work ceilings");
  check(pop(m.guard_evaluated) == m.guards &&
            pop(m.primitive_evaluated) == m.operations,
        "Verifier attempted masks retain exact work");
  check((m.guard_evaluated >> 3) == 0 &&
            (m.primitive_evaluated >> operations) == 0,
        "Verifier unused mask bits remain zero");
  if (m.complete)
    check(m.arithmetic_supported && m.guards == 3 && m.operations == operations,
          "Complete math requires every actual predicate operation");
}
[[gnu::noinline]] void accounting(const Diagnostic& d, const Limits& l) {
  check(d.version == 1, "Registered version retained");
  const auto work = counts(d);
  for (std::size_t i = 0; i < 14; ++i) {
    check(work[i] <= limit(l, i) && work[i] <= ceilings[i],
          "Actual charged work bounded");
    aggregate(i, work[i]);
  }
  check(d.work.phase_calls <= 1 && d.phase_work.graphs <= d.work.phase_calls,
        "At most one fresh original compiler invocation");
  aggregate(14, d.work.phase_calls);
  check(d.current_phase.size() <= 1 && d.current_phase.capacity() <= 1,
        "Exactly one no-growth original phase slot");
  check(d.output_capacity_bytes <= l.output_bytes &&
            d.output_capacity_bytes ==
                2 * sizeof(Expected) +
                    d.current_phase.capacity() * sizeof(PhaseCell),
        "Actual one-slot allocation and both owned headers counted");
  check(pop(d.source_evaluated) == d.work.source_guards &&
            (d.source_evaluated[1] >> 3) == 0,
        "Once67 lazy source mask");
  check(pop(d.current_evaluated) == d.work.current_guards &&
            pop(d.chart_evaluated) == d.work.chart_operations &&
            (d.chart_evaluated >> 21) == 0,
        "Current32 and chart21 lazy masks");
  check(pop(d.states_evaluated) == d.work.generator_states &&
            (d.states_evaluated[2] >> 34) == 0 &&
            (d.states_skipped[2] >> 34) == 0,
        "Finite162 state masks");
  for (std::size_t i = 0; i < 3; ++i)
    check((d.states_skipped[i] & ~d.states_evaluated[i]) == 0,
          "Skipped states were attempted");
  check(pop(d.states_skipped) == d.skipped_systems,
        "Skipped systems retain actual census");
  check(d.work.dual_trials <= d.work.generator_states &&
            d.work.primal_trials <= 20 * d.work.generator_states &&
            d.work.verification_guards <=
                3 * (d.work.dual_trials + d.work.primal_trials) &&
            d.work.verification_operations <=
                42 * d.work.dual_trials + 28 * d.work.primal_trials,
        "Finite candidate and verifier relationships");
  if (d.source_complete)
    check(d.work.source_guards == 67, "Fresh source67 completed");
  if (d.current_complete)
    check(d.source_complete && d.phase_available && d.work.current_guards == 32,
          "Fresh original current proof completed");
  if (d.chart_complete) {
    check(d.current_complete && d.work.chart_operations == 21,
          "Whole original local chart");
    for (const auto* a : {&d.geometry.half, &d.geometry.hip, &d.geometry.axis,
                          &d.geometry.segment})
      for (const auto& b : *a)
        check(finite(b), "Chart publishes usable finite bounds");
    check(finite(d.geometry.radius) && finite(d.geometry.owner_limit),
          "Original radius/slab retained");
  }
  if (d.phase_available) {
    check(d.current_phase.size() == 1, "Available phase owns its actual slot");
    if (!d.current_phase.empty()) {
      const auto& c = d.current_phase.front();
      check(c.complete && c.arithmetic_supported && c.nominal_links &&
                c.target_sole_identities && c.joint_sectors &&
                c.derivative_domains && c.timing_complete,
            "Available phase requires all seven original flags");
      check(c.first == d.selection.local_first &&
                c.last == d.selection.local_last,
            "Original evaluated interval is the selected immutable case");
    }
  }
  if (std::ranges::find(capacity, d.stop_condition) != capacity.end())
    check(d.state == State::capacity,
          "Hard capacity survives ordinary wrappers");
  if (d.stop_condition == Condition::unsupported_arithmetic)
    check(d.state == State::unsupported && !d.arithmetic_supported,
          "Unsupported terminal cannot become ordinary");
  check(!(d.excluded && d.unowned_interior),
        "No contradictory geometry authority");
  check(d.excluded == (d.state == State::excluded) &&
            d.unowned_interior == (d.state == State::unowned_interior),
        "Earned state and result agree");
  if (d.excluded) {
    const auto* m = std::get_if<DualMath>(&d.winner);
    check(d.chart_complete && m && m->complete && m->excluded,
          "Only verified winning dual excludes");
  }
  if (d.unowned_interior) {
    const auto* m = std::get_if<PrimalMath>(&d.winner);
    check(d.chart_complete && m && m->complete && m->unowned_interior,
          "Only whole-interval strict primal claims conflict");
  }
  if (const auto* m = std::get_if<DualMath>(&d.last_partial))
    math_masks(*m, 42);
  if (const auto* m = std::get_if<PrimalMath>(&d.last_partial))
    math_masks(*m, 28);
}
} // namespace
namespace {
auto ld_vector(const std::array<Scalar, 3>& p) -> LdPoint {
  return {ld(p[0]), ld(p[1]), ld(p[2])};
}
auto literal_vector(const std::array<double, 3>& p) -> LdPoint {
  return {ld(p[0]), ld(p[1]), ld(p[2])};
}
auto subtract(LdPoint a, const LdPoint& b) -> LdPoint {
  for (std::size_t i = 0; i < 3; ++i)
    a[i] = a[i] - b[i];
  return a;
}
auto multiply(LdPoint a, Ld b) -> LdPoint {
  for (auto& x : a)
    x = x * b;
  return a;
}
auto squared(Ld a) -> Ld {
  if (a.lo >= 0) return outward(a.lo * a.lo, a.hi * a.hi);
  if (a.hi <= 0) return outward(a.hi * a.hi, a.lo * a.lo);
  return {0, std::nextafter(std::max(a.lo * a.lo, a.hi * a.hi),
                            std::numeric_limits<long double>::infinity())};
}
auto norm_square(const LdPoint& p) -> Ld {
  return squared(p[0]) + squared(p[1]) + squared(p[2]);
}
[[gnu::noinline]] void dual_oracle(const Geometry& g, const DualMath& m) {
  if (!m.complete) return;
  const auto normal = literal_vector(m.candidate.normal);
  const auto lambda = ld(m.candidate.multiplier);
  const auto a =
      subtract(multiply(normal, ld(2.0)), multiply(ld_vector(g.axis), lambda));
  Ld box{};
  for (std::size_t i = 0; i < 3; ++i)
    box = box + ld(g.half[i]) * absolute(a[i]);
  const auto hipdot = dot(a, ld_vector(g.hip));
  auto segmentmax = ld(2.0) * dot(normal, ld_vector(g.segment));
  segmentmax = {std::max(0.0L, segmentmax.lo), std::max(0.0L, segmentmax.hi)};
  const auto normal_square = norm_square(normal);
  const auto lower =
      -box - hipdot - segmentmax + lambda * ld(g.owner_limit) - normal_square;
  const auto radius = squared(ld(g.radius));
  check(contains(m.boxsum, box) && contains(m.hipdot, hipdot) &&
            contains(m.segmentmax, segmentmax) &&
            contains(m.normal_square, normal_square),
        "Independent full box and both capsule endpoint support terms");
  check(contains(m.lower_bound, lower) && contains(m.radius_squared, radius) &&
            contains(m.gap, lower - radius),
        "Independent dual interval lower bound");
  check(m.excluded == (m.gap.lower >= 0), "Unchanged outward dual comparison");
  if (m.excluded)
    check(m.lower_bound.lower >= m.radius_squared.upper,
          "Strict capsule interior excluded conservatively at tangency");
}
[[gnu::noinline]] void primal_oracle(const Geometry& g, const PrimalMath& m) {
  if (!m.complete) return;
  const auto point_value = literal_vector(m.candidate.point);
  const auto delta = subtract(point_value, ld_vector(g.hip));
  const auto residual = subtract(
      delta, multiply(ld_vector(g.segment), ld(m.candidate.segment_fraction)));
  const auto residual_square = norm_square(residual);
  const auto axial = dot(ld_vector(g.axis), delta);
  const auto radius = squared(ld(g.radius));
  bool strict = true;
  for (std::size_t i = 0; i < 3; ++i) {
    check(contains(m.box_gaps[2 * i], ld(g.half[i]) + point_value[i]) &&
              contains(m.box_gaps[2 * i + 1], ld(g.half[i]) - point_value[i]),
          "Independent original six box gaps");
    strict = strict && m.box_gaps[2 * i].lower > 0 &&
             m.box_gaps[2 * i + 1].lower > 0;
  }
  check(contains(m.axial_projection, axial) &&
            contains(m.axial_gap, axial - ld(g.owner_limit)) &&
            contains(m.residual_squared, residual_square) &&
            contains(m.radius_squared, radius) &&
            contains(m.residual_gap, radius - residual_square),
        "Independent whole-interval slab and full capsule residual");
  strict = strict && m.axial_gap.lower > 0 && m.residual_gap.lower > 0;
  check(m.unowned_interior == strict,
        "Only all strict original inequalities witness interior");
}
[[gnu::noinline]] void first_audit(const Diagnostic& d) {
  if (!d.chart_complete || !d.phase_available || d.current_phase.empty() ||
      std::holds_alternative<std::monostate>(d.winner))
    return;
  charge(totals.oracles, 6);
  const auto& c = d.current_phase.front();
  const auto& hip =
      c.points[static_cast<std::size_t>(BoardingPlantedBodyPointId::port_hip)]
          .value;
  const auto& knee =
      c.points[static_cast<std::size_t>(BoardingPlantedBodyPointId::port_knee)]
          .value;
  LdPoint delta;
  for (std::size_t axis = 0; axis < 3; ++axis)
    delta[axis] = ld(component(knee, axis)) - ld(component(hip, axis));
  for (std::size_t axis = 0; axis < 3; ++axis) {
    LdPoint column;
    for (std::size_t j = 0; j < 3; ++j)
      column[j] = ld(component(c.frames[0].columns[axis].value, j));
    const auto local_segment = dot(column, delta);
    check(contains(d.geometry.segment[axis], local_segment) &&
              contains(d.geometry.axis[axis], divide(local_segment, .47285)),
          "Independent authentic retained ROOT chart, no midpoint inverse");
  }
  constexpr std::array half{.24, .12, .18};
  constexpr std::array hip_value{-.14, 0.0, 0.0};
  for (std::size_t i = 0; i < 3; ++i)
    check(d.geometry.half[i].lower == half[i] &&
              d.geometry.half[i].upper == half[i] &&
              d.geometry.hip[i].lower == hip_value[i] &&
              d.geometry.hip[i].upper == hip_value[i],
          "Original full pelvis and authentic hip preserved");
  check(d.geometry.radius.lower == .105 && d.geometry.radius.upper == .105 &&
            d.geometry.owner_limit.lower == 0x1.bb0cd605d7512p-3 &&
            d.geometry.owner_limit.upper == 0x1.bb0cd605d7512p-3,
        "Original full capsule and original finite slab preserved");
  if (const auto* m = std::get_if<DualMath>(&d.winner))
    dual_oracle(d.geometry, *m);
  if (const auto* m = std::get_if<PrimalMath>(&d.winner))
    primal_oracle(d.geometry, *m);
}
[[gnu::noinline]] void immutable_cases(Summary& summary) {
  Hash h;
  for (std::size_t i = 0; i < 6; ++i) {
    const auto meta =
        detail::boarding_route_intermediate_hip_diagnostic01_case(cases[i]);
    const auto request =
        detail::boarding_route_intermediate_hip_diagnostic01_controls(cases[i]);
    check(meta && request,
          "Only immutable case inspector produces control packet");
    if (!meta || !request) continue;
    check(meta->case_id == cases[i] && meta->phase_index == phases[i] &&
              meta->global_first == globals[i][0] &&
              meta->global_last == globals[i][1] &&
              meta->local_first == locals[i][0] &&
              meta->local_last == locals[i][1] &&
              meta->seconds_per_parameter == (i == 4 ? 2 : 12) &&
              request->seconds_per_parameter == meta->seconds_per_parameter,
          "Exact original dyadic cases and physical clock");
    h.add(i);
    hash_request(h, *request);
  }
  check(!detail::boarding_route_intermediate_hip_diagnostic01_case(
            static_cast<CaseId>(6)) &&
            !detail::boarding_route_intermediate_hip_diagnostic01_controls(
                static_cast<CaseId>(255)),
        "Inspectors reject invalid cases without geometry");
  summary.recipe_hash = h.value;
}
auto required_bytes(const Diagnostic& d) -> std::size_t {
  return 2 * sizeof(Expected) + d.current_phase.capacity() * sizeof(PhaseCell);
}
void capture(const Diagnostic& d, std::size_t index, Summary& summary) {
  auto& b = summary.first[index];
  b.exists = 1;
  b.has_report = 1;
  b.state = static_cast<std::uint8_t>(d.state);
  b.condition = static_cast<std::uint8_t>(d.stop_condition);
  b.has_slot = static_cast<std::uint8_t>(d.current_phase.capacity() == 1);
  b.hash = semantic_hash(d, summary.recipe_hash);
  const auto work = counts(d);
  for (std::size_t i = 0; i < 14; ++i) {
    check(work[i] <= ceilings[i],
          "Checked packed count preserves all actual work");
    put(b, i, work[i]);
    if (work[i] > 0 && !(summary.anchors.flags[i / 8] & (1U << (i % 8)))) {
      summary.anchors.counts[i] = static_cast<std::uint32_t>(work[i]);
      summary.anchors.cases[i] = static_cast<std::uint8_t>(index);
      summary.anchors.flags[i / 8] |= static_cast<std::uint8_t>(1U << (i % 8));
    }
  }
  put(b, 14, d.work.phase_calls);
  if (!(summary.anchors.flags[1] & (1U << 6))) {
    summary.anchors.counts[14] = static_cast<std::uint32_t>(required_bytes(d));
    summary.anchors.cases[14] = static_cast<std::uint8_t>(index);
    summary.anchors.flags[1] |= 1U << 6;
  }
  check((b.counts[1] >> 41) == 0, "Packed unused23 bits are zero");
}
[[gnu::noinline]] void print_first(std::size_t slot, const Diagnostic& d) {
  std::cout << "FIRST_HIP_DIAGNOSTIC01 A" << slot
            << " case=" << unsigned(d.selection.case_id)
            << " phase=" << d.selection.phase_index << " global=["
            << d.selection.global_first << ',' << d.selection.global_last
            << "] local=[" << d.selection.local_first << ','
            << d.selection.local_last
            << "] T=" << d.selection.seconds_per_parameter
            << " state=" << unsigned(d.state)
            << " stop=" << unsigned(d.stop_condition)
            << " flags=" << d.source_complete << d.phase_available
            << d.current_complete << d.chart_complete << d.certificate_assessed
            << d.excluded << d.unowned_interior << " work=";
  for (auto n : counts(d))
    std::cout << n << ',';
  std::cout << " calls=" << d.work.phase_calls
            << " masks=" << d.source_evaluated[0] << ','
            << d.source_evaluated[1] << ',' << d.current_evaluated << ','
            << d.chart_evaluated << " statecursor=" << d.last_state << ','
            << unsigned(d.last_delta) << ',' << unsigned(d.last_alpha)
            << " skipped=" << d.skipped_systems << ',' << d.skipped_proposals
            << " reason=" << unsigned(d.first_refusal.condition) << ','
            << d.first_refusal.state << ','
            << unsigned(d.first_refusal.operation)
            << " old=" << unsigned(d.phase_refusal.condition)
            << " slot=" << d.current_phase.size() << '/'
            << d.current_phase.capacity()
            << " owned=" << d.output_capacity_bytes;
  if (const auto* m = std::get_if<DualMath>(&d.winner)) {
    std::cout << " dual=" << m->candidate.normal[0] << ','
              << m->candidate.normal[1] << ',' << m->candidate.normal[2] << ','
              << m->candidate.multiplier << " LB=[" << m->lower_bound.lower
              << ',' << m->lower_bound.upper << "] gap=[" << m->gap.lower << ','
              << m->gap.upper << ']';
  }
  if (const auto* m = std::get_if<PrimalMath>(&d.winner)) {
    std::cout << " primal=" << m->candidate.point[0] << ','
              << m->candidate.point[1] << ',' << m->candidate.point[2] << ','
              << m->candidate.segment_fraction << " axial=["
              << m->axial_gap.lower << ',' << m->axial_gap.upper << "] radial=["
              << m->residual_gap.lower << ',' << m->residual_gap.upper << ']';
  }
  std::cout << '\n' << std::flush;
}
[[gnu::noinline]] void first_one(const Provider& p, std::size_t i,
                                 Summary& summary) {
  charge(totals.consumers, 73);
  auto result =
      assess_origin_boarding_route_intermediate_hip_diagnostic01(p, cases[i]);
  if (!result) {
    std::cout << "FIRST_HIP_DIAGNOSTIC01 A" << i + 1
              << " error=" << unsigned(result.error()) << '\n'
              << std::flush;
    auto& b = summary.first[i];
    b.exists = 1;
    b.error = static_cast<std::uint8_t>(result.error());
    return;
  }
  print_first(i + 1, *result);
  accounting(*result, Limits{});
  capture(*result, i, summary);
  first_audit(*result);
}
[[gnu::noinline]] void create_source(const NativeCraftBinding& binding,
                                     const OriginBoardingBootSupport& boots,
                                     std::optional<Provider>& source) {
  charge(totals.creators, 1);
  auto result = make_origin_boarding_intermediate_pause_support(binding, boots);
  std::cout << "FIRST_HIP_DIAGNOSTIC01_SOURCE admitted=" << result.has_value();
  if (!result)
    std::cout << " error=" << result.error();
  else if (const auto* s = result->summary())
    std::cout << " complete=" << s->complete
              << " bytes=" << s->actual_source_bytes
              << " condition=" << unsigned(s->condition)
              << " work=" << s->work.base_guards << ',' << s->work.metadata_rows
              << ',' << s->work.face_reads << ',' << s->work.quad_records;
  std::cout << '\n' << std::flush;
  if (result) {
    check(Access::valid(*result), "Only genuine public creator issues source");
    source.emplace(std::move(*result));
  }
}
} // namespace
namespace {
[[gnu::noinline]] void assessment_one(const Provider& p, CaseId id,
                                      const Limits& l,
                                      std::optional<std::size_t> capped = {},
                                      std::optional<Error> error = {},
                                      const FirstRecord* exact = nullptr,
                                      std::uint64_t recipe = 0,
                                      bool empty = false) {
  charge(totals.consumers, 73);
  auto result =
      detail::boarding_route_intermediate_hip_diagnostic01_bounded(p, id, l);
  if (error) {
    check(!result && result.error() == *error,
          "Typed preflight refusal before work");
    return;
  }
  if (!result) {
    check(capped && *capped == 14 &&
              (result.error() == Error::output_preflight ||
               result.error() == Error::allocation_failure),
          "Only actual output preflight may replace report");
    return;
  }
  accounting(*result, l);
  if (capped && *capped == 14)
    check(result->state == State::capacity &&
              result->stop_condition == Condition::output_capacity &&
              result->work.phase_calls == 0 && result->current_phase.empty(),
          "Required slot output refuses before the original compiler");
  if (capped && *capped < 14)
    check(result->state == State::capacity &&
              result->stop_condition == capacity[*capped],
          "Reached lowered stage has exact hard terminal capacity");
  if (exact) {
    check(result->state == static_cast<State>(exact->state) &&
              result->stop_condition ==
                  static_cast<Condition>(exact->condition) &&
              semantic_hash(*result, recipe) == exact->hash,
          "Exact/lifetime replay retains full original and published proof "
          "evidence");
    const auto work = counts(*result);
    for (std::size_t i = 0; i < 14; ++i)
      check(work[i] == get(*exact, i), "Exact replay preserves used work");
    check(result->work.phase_calls == get(*exact, 14),
          "Exact replay preserves guarded call count");
  }
  if (empty)
    check(!result->source_complete && result->work.phase_calls == 0 &&
              result->work.current_guards == 0 &&
              result->work.chart_operations == 0 &&
              result->work.generator_states == 0 &&
              result->first_refusal.condition == Condition::invalid_binding,
          "Moved genuine provider refuses before original graph and theorem "
          "math");
}
auto reached(const Summary& summary, std::size_t i) -> bool {
  return (summary.anchors.flags[i / 8] & (1U << (i % 8))) != 0;
}
void skip(Summary& summary, std::size_t slot, std::string_view why) {
  ++summary.skipped;
  std::cout << "SKIP A" << slot << ' ' << why << '\n';
}
[[gnu::noinline]] void cap_roster(const Provider& p, Summary& summary) {
  for (std::size_t i = 0; i < 15; ++i) {
    if (!reached(summary, i)) {
      skip(summary, 7 + i, "unreached");
      continue;
    }
    Limits l;
    set_limit(l, i, 0);
    assessment_one(p, cases[summary.anchors.cases[i]], l, i);
  }
  for (std::size_t i = 0; i < 15; ++i) {
    Limits l;
    set_limit(l, i, limit(l, i) + 1);
    assessment_one(p, cases[0], l, {}, Error::invalid_limits);
  }
  for (std::size_t i = 0; i < 15; ++i) {
    if (!reached(summary, i) || summary.anchors.counts[i] <= 1) {
      skip(summary, 37 + i, "unreached-or-count1");
      continue;
    }
    Limits l;
    set_limit(l, i, summary.anchors.counts[i] - 1);
    assessment_one(p, cases[summary.anchors.cases[i]], l, i);
  }
  for (std::size_t i = 0; i < 6; ++i) {
    const auto& b = summary.first[i];
    if (!b.has_report) {
      skip(summary, 52 + i, "FIRST-has-no-report");
      continue;
    }
    Limits l;
    for (std::size_t field = 0; field < 14; ++field)
      set_limit(l, field, get(b, field));
    l.output_bytes =
        2 * sizeof(Expected) + (b.has_slot ? sizeof(PhaseCell) : 0);
    assessment_one(p, cases[i], l, {}, {}, &b, summary.recipe_hash);
  }
}
[[gnu::noinline]] void remaining_roster(Provider& p,
                                        const NativeCraftBinding& binding,
                                        Summary& summary) {
  for (std::size_t mode = 0; mode < 6; ++mode) {
    Environment environment(mode);
    if (!environment.available) {
      skip(summary, 58 + mode, "unsafe-mode-unavailable");
      continue;
    }
    assessment_one(p, cases[0], Limits{}, {}, Error::unsupported_environment);
  }
  assessment_one(p, static_cast<CaseId>(6), Limits{}, {}, Error::invalid_case);
  assessment_one(p, static_cast<CaseId>(255), Limits{}, {},
                 Error::invalid_case);
  {
    auto alias = p;
    auto moved = std::move(alias);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Empty alias contract.
    assessment_one(alias, cases[0], Limits{}, {}, {}, nullptr, 0, true);
    alias = std::move(moved);
    check(Access::valid(alias), "Copied moved alias restored");
  }
  {
    auto held = std::move(p);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Empty owner contract.
    assessment_one(p, cases[0], Limits{}, {}, {}, nullptr, 0, true);
    p = std::move(held);
  }
  {
    const auto* data = Access::data(p);
    auto alias = p;
    p = std::move(alias);
    check(Access::data(p) == data,
          "Copied alias survives release of original handle");
    const auto* exact =
        summary.first[0].has_report ? &summary.first[0] : nullptr;
    assessment_one(p, cases[0], Limits{}, {}, {}, exact, summary.recipe_hash);
  }
  {
    auto moved = std::move(p);
    const auto* exact =
        summary.first[0].has_report ? &summary.first[0] : nullptr;
    assessment_one(moved, cases[0], Limits{}, {}, {}, exact,
                   summary.recipe_hash);
    p = std::move(moved);
  }
  {
    const auto cached = make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{});
    check(cached && cached->contact() == binding.contact(),
          "Existing native factory has same genuine owner");
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Copy test.
    const auto alias = p;
    check(Access::data(alias) == Access::data(p),
          "Same cached source alias, no second creator");
    const auto* exact =
        summary.first[0].has_report ? &summary.first[0] : nullptr;
    assessment_one(alias, cases[0], Limits{}, {}, {}, exact,
                   summary.recipe_hash);
  }
  {
    Limits l;
    l.phase.bodies = 0;
    l.phase.sectors = 0;
    const auto cap = summary.first[0].has_report && get(summary.first[0], 3) > 0
                         ? std::optional<std::size_t>{3}
                         : std::nullopt;
    assessment_one(p, cases[0], l, cap);
  }
  if (reached(summary, 8)) {
    Limits l;
    l.generator_states = 0;
    l.dual_trials = 0;
    assessment_one(p, cases[summary.anchors.cases[8]], l, 8);
  } else
    skip(summary, 72, "generator-unreached");
  if (reached(summary, 10)) {
    Limits l;
    l.dual_trials = 0;
    l.primal_trials = 0;
    assessment_one(p, cases[summary.anchors.cases[10]], l, 10);
  } else
    skip(summary, 73, "dual-unreached");
}
} // namespace
namespace {
auto dual_geometry() -> Geometry {
  Geometry g;
  g.half = {point(.24), point(.12), point(.18)};
  g.hip = {point(-.14), point(0), point(0)};
  g.axis = {point(0), point(-1), point(0)};
  g.segment = {point(0), point(-.47285), point(0)};
  g.radius = point(.105);
  g.owner_limit = point(0x1.bb0cd605d7512p-3);
  return g;
}
auto primal_geometry() -> Geometry {
  Geometry g;
  g.half = {point(1), point(1), point(1)};
  g.hip = {point(0), point(0), point(0)};
  g.axis = {point(1), point(0), point(0)};
  g.segment = g.axis;
  g.radius = point(.5);
  g.owner_limit = point(.25);
  return g;
}
[[gnu::noinline]] void raw_dual(std::size_t slot) {
  auto g = dual_geometry();
  DualCandidate c{{0, 0, 0}, .25};
  DualLimits l;
  switch (slot) {
    case 2: c.multiplier = 0; break;
    case 3:
      c.normal = {0, 1, 0};
      c.multiplier = 0;
      break;
    case 4:
      c.normal = {0, -1, 0};
      c.multiplier = 0;
      break;
    case 5:
      g.half = {point(.25), point(.25), point(.25)};
      g.hip = {point(.75), point(0), point(0)};
      g.axis = {point(0), point(0), point(0)};
      g.segment = g.axis;
      g.radius = point(.5);
      g.owner_limit = point(0);
      c = {{-.5, 0, 0}, 0};
      break;
    case 6: c.multiplier = -1; break;
    case 7: c.normal[0] = std::numeric_limits<double>::quiet_NaN(); break;
    case 8: g.axis[0] = {1, -1, true}; break;
    case 9: g.segment[2].supported = false; break;
    case 10: g.half[0] = point(0); break;
    case 11: g.radius = point(0); break;
    case 12:
      c.normal[0] = std::numeric_limits<double>::max();
      c.multiplier = 0;
      break;
    case 14: l.guards = 0; break;
    case 15: l.guards = 2; break;
    case 16: l = {3, 42}; break;
    case 17: l.operations = 0; break;
    case 18: l.operations = 41; break;
    case 19: l.guards = 4; break;
    case 20: l.operations = 43; break;
    default: break;
  }
  fexcept_t flags{};
  std::fegetexceptflag(&flags, FE_ALL_EXCEPT);
  std::optional<Environment> unsafe;
  if (slot == 13) {
    unsafe.emplace(1);
    if (!unsafe->available) {
      std::cout << "SKIP M13 FP-unavailable\n";
      return;
    }
  }
  if (slot == 12) std::feclearexcept(FE_OVERFLOW);
  charge(totals.raw, 40);
  const auto m =
      detail::boarding_route_intermediate_hip_diagnostic01_dual_math(g, c, l);
  math_masks(m, 42);
  aggregate(15, m.guards);
  aggregate(16, m.operations);
  if (slot == 19 || slot == 20)
    check(m.guards == 0 && m.operations == 0 && !m.complete,
          "Raised raw limit refuses prework");
  else if (slot == 13)
    check(!m.arithmetic_supported && !m.complete && m.operations == 0,
          "Raw unsupported FP has no primitive result");
  else if (slot == 14)
    check(m.guards == 0 && m.operations == 0 &&
              m.condition == Condition::verification_guard_capacity,
          "Zero raw guard budget before first guard");
  else if (slot == 15)
    check(m.guards == 2 && m.operations == 0 &&
              m.condition == Condition::verification_guard_capacity,
          "Candidate guard not evaluated after two guard cap");
  else if (slot == 17)
    check(m.guards == 3 && m.operations == 0 &&
              m.condition == Condition::verification_operation_capacity,
          "Zero raw primitive budget preserves three guards");
  else if (slot == 18)
    check(m.guards == 3 && m.operations == 41 && !m.complete && !m.excluded &&
              m.condition == Condition::verification_operation_capacity,
          "Last dual gap subtraction NOT_RUN at one-less budget");
  else if (slot == 12) {
    check(m.guards == 3 && m.operations == 1 && m.failed_operation == 0 &&
              !m.complete && !m.excluded && !m.arithmetic_supported &&
              m.condition == Condition::unsupported_arithmetic,
          "Finite overflow rejected inside first charged primitive");
    check((std::fetestexcept(FE_OVERFLOW) & FE_OVERFLOW) == 0,
          "Conservative binary64 precheck executes no overflow");
  } else if (slot >= 6 && slot <= 11)
    check(!m.complete && !m.excluded && m.operations == 0,
          "Malformed geometry/candidate has no numerical certificate");
  else {
    check(m.complete && m.arithmetic_supported && m.guards == 3 &&
              m.operations == 42,
          "Valid raw dual fully evaluates exact primitive roster");
    dual_oracle(g, m);
    if (slot == 1 || slot == 16)
      check(m.excluded && m.gap.lower > 0,
            "Analytic empty clipped box strict exclusion");
    if (slot == 2)
      check(!m.excluded, "Zero dual proposal cannot invent clearance");
    if (slot == 3)
      check(m.segmentmax.lower <= 0 && m.segmentmax.upper >= 0,
            "Root capsule cap branch retained");
    if (slot == 4)
      check(m.segmentmax.lower > 0, "Distal capsule cap branch retained");
    if (slot == 5)
      check(m.gap.lower <= 0 && m.gap.upper >= 0,
            "Analytic touching dual encloses zero gap");
  }
  unsafe.reset();
  std::fesetexceptflag(&flags, FE_ALL_EXCEPT);
}
[[gnu::noinline]] void raw_primal(std::size_t slot) {
  auto g = primal_geometry();
  PrimalCandidate c{{.5, .25, 0}, .5};
  PrimalLimits l;
  switch (slot) {
    case 21:
      g = dual_geometry();
      g.axis[0] = {
          std::nextafter(3.0 / 5.0, -std::numeric_limits<double>::infinity()),
          std::nextafter(3.0 / 5.0, std::numeric_limits<double>::infinity()),
          true};
      g.axis[1] = {
          std::nextafter(-4.0 / 5.0, -std::numeric_limits<double>::infinity()),
          std::nextafter(-4.0 / 5.0, std::numeric_limits<double>::infinity()),
          true};
      g.segment[0] = {.2837099999999999, .2837100000000001, true};
      g.segment[1] = {-.3782800000000001, -.3782799999999999, true};
      c = {{.072, -.116, 0}, .22 / .47285};
      break;
    case 22: c = {{0, 0, 0}, 0}; break;
    case 23:
      c.point = {.5, 1, 0};
      g.radius = point(2);
      break;
    case 24: c = {{.25, .25, 0}, .25}; break;
    case 25: c.point = {.5, .5, 0}; break;
    case 26:
      c.segment_fraction =
          std::nextafter(0.0, -std::numeric_limits<double>::infinity());
      break;
    case 27:
      c.segment_fraction =
          std::nextafter(1.0, std::numeric_limits<double>::infinity());
      break;
    case 28: c.point[2] = std::numeric_limits<double>::quiet_NaN(); break;
    case 29: g.segment[1] = {1, -1, true}; break;
    case 30: g.axis[0].supported = false; break;
    case 31: g.half[2] = point(-1); break;
    case 32: g.radius = point(0); break;
    case 34: l.guards = 0; break;
    case 35: l.guards = 2; break;
    case 36: l = {3, 28}; break;
    case 37: l.operations = 0; break;
    case 38: l.operations = 27; break;
    case 39: l.guards = 4; break;
    case 40: l.operations = 29; break;
    default: break;
  }
  fexcept_t flags{};
  std::fegetexceptflag(&flags, FE_ALL_EXCEPT);
  std::optional<Environment> unsafe;
  if (slot == 33) {
    unsafe.emplace(0);
    if (!unsafe->available) {
      std::cout << "SKIP M33 FP-unavailable\n";
      return;
    }
  }
  charge(totals.raw, 40);
  const auto m =
      detail::boarding_route_intermediate_hip_diagnostic01_primal_math(g, c, l);
  math_masks(m, 28);
  aggregate(15, m.guards);
  aggregate(16, m.operations);
  if (slot == 39 || slot == 40)
    check(m.guards == 0 && m.operations == 0 && !m.complete,
          "Raised primal cap refuses prework");
  else if (slot == 33)
    check(!m.arithmetic_supported && !m.complete && m.operations == 0,
          "Primal unsafe FP refuses arithmetic");
  else if (slot == 34)
    check(m.guards == 0 && m.operations == 0 &&
              m.condition == Condition::verification_guard_capacity,
          "Zero primal guards before access");
  else if (slot == 35)
    check(m.guards == 2 && m.operations == 0 &&
              m.condition == Condition::verification_guard_capacity,
          "Primal candidate guard remains NOT_RUN");
  else if (slot == 37)
    check(m.guards == 3 && m.operations == 0 &&
              m.condition == Condition::verification_operation_capacity,
          "Zero primal operations retains input checks");
  else if (slot == 38)
    check(m.guards == 3 && m.operations == 27 && !m.complete &&
              !m.unowned_interior &&
              m.condition == Condition::verification_operation_capacity,
          "Last strict residual subtraction NOT_RUN");
  else if (slot >= 26 && slot <= 32)
    check(!m.complete && !m.unowned_interior && m.operations == 0,
          "Invalid raw primal never evaluates or grants geometry");
  else {
    check(m.complete && m.arithmetic_supported && m.guards == 3 &&
              m.operations == 28,
          "Valid primal evaluates all original strict predicates");
    primal_oracle(g, m);
    if (slot == 21 || slot == 36)
      check(m.unowned_interior,
            "Independent strict synthetic unowned interior");
    else
      check(!m.unowned_interior,
            "Owned or exactly touching points are not strict witnesses");
    if (slot == 23)
      check(m.box_gaps[3].lower <= 0 && m.box_gaps[3].upper >= 0,
            "Exact box face keeps strict gap unresolved");
    if (slot == 24)
      check(m.axial_gap.lower <= 0 && m.axial_gap.upper >= 0,
            "Exact slab boundary remains owned");
    if (slot == 25)
      check(m.residual_gap.lower <= 0 && m.residual_gap.upper >= 0,
            "Full capsule tangency is not strict interior");
  }
  unsafe.reset();
  std::fesetexceptflag(&flags, FE_ALL_EXCEPT);
}
[[gnu::noinline]] void raw_roster() {
  for (std::size_t slot = 1; slot <= 20; ++slot)
    raw_dual(slot);
  for (std::size_t slot = 21; slot <= 40; ++slot)
    raw_primal(slot);
}
[[gnu::noinline]] void final_counts(const Summary& summary) {
  check(totals.creators == 1 && totals.consumers <= 73 && totals.raw <= 40 &&
            totals.oracles <= 6 && totals.work[14] <= 73,
        "Registered onecreator/73/40/max6 bounded roster");
  for (std::size_t i = 0; i < 14; ++i)
    check(totals.work[i] <= 73 * ceilings[i],
          "Aggregate original and proposal work bounded");
  check(totals.work[15] <= 120 && totals.work[16] <= 1400,
        "Forty raw verifier work bound");
  std::cout << "VALIDATION creators=" << totals.creators
            << " consumers=" << totals.consumers << " raw=" << totals.raw
            << " audits=" << totals.oracles << " skipped=" << summary.skipped
            << " checks=" << checks << " failures=" << failures << " work=";
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
      summary.source_admitted = true;
      immutable_cases(summary);
      for (std::size_t i = 0; i < 6; ++i)
        first_one(*source, i, summary);
      cap_roster(*source, summary);
      remaining_roster(*source, *binding, summary);
    }
    raw_roster();
    final_counts(summary);
  } catch (const std::exception& e) {
    check(false, "Unexpected harness exception");
    std::cerr << e.what() << '\n';
  }
  const bool streams = std::cout.good() && std::cerr.good() && !out.truncated &&
                       !err.truncated && bytes <= 16384;
  std::cout.rdbuf(oldout);
  std::cerr.rdbuf(olderr);
  check(streams, "Shared16KiB stream remains complete");
  return failures ? 1 : 0;
}
