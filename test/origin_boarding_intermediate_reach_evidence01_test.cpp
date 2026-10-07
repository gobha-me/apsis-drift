#include "apsis_drift/origin_boarding_intermediate_reach_evidence01.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
#include "origin_boarding_intermediate_reach_evidence01_internal.hpp"
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
using Diagnostic = BoardingIntermediateReachEvidence01Diagnostic;
using Expected = BoardingIntermediateReachEvidence01Expected;
using State = BoardingIntermediateReachEvidence01State;
using Condition = BoardingIntermediateReachEvidence01Condition;
using Kind = BoardingIntermediateReachEvidence01Kind;
using Classification = BoardingIntermediateReachEvidence01Classification;
using Stage = BoardingIntermediateReachEvidence01Stage;
using Original = BoardingIntermediateReachEvidence01OriginalResult;
using Bounds = BoardingIntermediateReachEvidence01Bounds;
using Limits = detail::BoardingIntermediateReachEvidence01Limits;
using Access = detail::BoardingIntermediatePauseSupportAccess;
using Candidate = BoardingIntermediateEndpoint01Candidate;
using PhaseCondition = BoardingRouteFootPhaseCondition;
using Context = detail::BoardingIntermediateReachEvidence01Context;
using Token = detail::BoardingIntermediateReachEvidence01CaptureToken;
using FreshCall = detail::BoardingIntermediateReachEvidence01FreshCallRecord;
static_assert(
    !std::is_default_constructible_v<Context> &&
    !std::is_copy_constructible_v<Context> &&
    !std::is_move_constructible_v<Context> &&
    !std::is_constructible_v<Context, const Diagnostic&,
                             const BoardingRouteFootPhaseRequest&, FreshCall&>);
static_assert(
    !std::is_default_constructible_v<Token> &&
    !std::is_constructible_v<Token, const Context&, const Diagnostic&>);
constexpr auto candidate = Candidate::authored_descent_midpoint;
constexpr std::string_view fp_error =
    "intermediate reach evidence01 unsupported floating point";
constexpr std::string_view id_error =
    "intermediate reach evidence01 invalid candidate";
constexpr std::string_view limits_error =
    "intermediate reach evidence01 invalid limits";
constexpr std::string_view header_error =
    "intermediate reach evidence01 output headers";
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
  std::array<std::uint32_t, 11> work{};
  std::uint32_t creators{}, consumers{}, skips{}, attempted_slots{},
      skipped_slots{};
} totals;
static_assert(sizeof(Totals) <= 128);
struct Summary {
  std::array<std::uint32_t, 3> used{};
  std::uint64_t hash{};
  std::size_t required{}, owned{};
  bool exists{};
};
static_assert(sizeof(Summary) <= 200);
static_assert(sizeof(Expected) <= 384 && sizeof(Limits) <= 32 &&
              sizeof(Context) <= 32 && sizeof(Token) <= 40 &&
              sizeof(FreshCall) <= 64);
static_assert(!Diagnostic::body_qualified && !Diagnostic::source_qualified &&
              !Diagnostic::support_qualified && !Diagnostic::load_qualified &&
              !Diagnostic::self_qualified && !Diagnostic::material_qualified &&
              !Diagnostic::world_qualified && !Diagnostic::route_qualified &&
              !Diagnostic::seat_qualified && !Diagnostic::actor_qualified &&
              !Diagnostic::save_qualified && !Diagnostic::dynamics_qualified);
class StreamCounter : public std::streambuf {
 public:
  StreamCounter(std::streambuf* target, std::size_t& count)
      : target_(target), bytes_(count) {}
  bool truncated{};

 protected:
  auto overflow(int_type c) -> int_type override {
    if (traits_type::eq_int_type(c, traits_type::eof()))
      return traits_type::not_eof(c);
    if (bytes_ >= 16384) {
      truncated = true;
      return traits_type::eof();
    }
    ++bytes_;
    return target_->sputc(traits_type::to_char_type(c));
  }
  auto xsputn(const char* text, std::streamsize count)
      -> std::streamsize override {
    std::streamsize written{};
    while (written < count && !traits_type::eq_int_type(overflow(text[written]),
                                                        traits_type::eof()))
      ++written;
    return written;
  }
  auto sync() -> int override { return target_->pubsync(); }

 private:
  std::streambuf* target_;
  std::size_t& bytes_;
};
class Environment {
 public:
  explicit Environment(std::size_t mode) : rounding_(std::fegetround()) {
    std::fegetexceptflag(&exceptions_, FE_ALL_EXCEPT);
#if defined(__SSE2__) && defined(__x86_64__)
    control_ = _mm_getcsr();
#endif
    if (mode < 3) {
      constexpr std::array modes{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
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
constexpr std::array<std::size_t Limits::*, 4> members{
    &Limits::capture_guards, &Limits::threshold_operations,
    &Limits::comparison_operations, &Limits::output_bytes};
constexpr std::array<Condition, 3> capacities{Condition::capture_capacity,
                                              Condition::threshold_capacity,
                                              Condition::comparison_capacity};
constexpr std::array<Stage, 3> stages{Stage::capture, Stage::threshold,
                                      Stage::comparison};
template <class T> auto mask_count(T mask) -> std::uint64_t {
  return static_cast<std::uint64_t>(std::popcount(mask));
}
void add_work(std::size_t index, std::uint64_t count) {
  if (count > std::numeric_limits<std::uint32_t>::max() - totals.work[index])
    throw std::runtime_error("Registered reach evidence total overflow");
  totals.work[index] += static_cast<std::uint32_t>(count);
}
void record(std::size_t slot, bool skipped) {
  if (slot < 1 || slot > 28)
    throw std::runtime_error("Registered reach evidence slot range");
  const auto bit = std::uint32_t{1} << (slot - 1);
  if (((totals.attempted_slots | totals.skipped_slots) & bit) != 0)
    throw std::runtime_error("Registered reach evidence duplicate slot");
  if (skipped) {
    ++totals.skips;
    totals.skipped_slots |= bit;
    std::cout << "SKIP A" << slot << '\n';
  } else {
    ++totals.consumers;
    totals.attempted_slots |= bit;
    // This method's fixed guard precedes even unexpected returns.
    add_work(0, 1);
  }
}
auto valid(Bounds b) -> bool {
  return b.supported && std::isfinite(b.lower) && std::isfinite(b.upper) &&
         b.lower <= b.upper;
}
auto words(const Diagnostic& d) -> std::array<std::uint64_t, 11> {
  return {d.work.preflight_guards,     d.work.source_guards,
          d.work.phase_calls,          d.work.phase.graphs,
          d.work.phase.legs,           d.work.phase.bodies,
          d.work.phase.sectors,        d.work.phase.timing,
          d.work.capture_guards,       d.work.threshold_operations,
          d.work.comparison_operations};
}
void no_scalar(const Bounds& b) {
  check(!b.supported, "NOT_RUN scalar never gains supported authority");
}
void no_evidence(const Diagnostic& d) {
  no_scalar(d.distance_squared);
  no_scalar(d.minimum_squared);
  no_scalar(d.maximum_squared);
  check(d.threshold_attempted == 0 && d.threshold_written == 0 &&
            d.comparison_attempted == 0 && d.comparison_written == 0 &&
            !d.evidence_complete && d.classification == Classification::not_run,
        "Unreached D stage leaves later evidence NOT_RUN");
}
void bounds_hash(std::uint64_t& h, const Bounds& b);
void mix(std::uint64_t& h, std::uint64_t n) {
  h ^= n;
  h *= 1099511628211ULL;
}
void scalar_hash(std::uint64_t& h, double n) {
  mix(h, std::bit_cast<std::uint64_t>(n));
}
void bounds_hash(std::uint64_t& h, const Bounds& b) {
  mix(h, b.supported);
  if (b.supported) {
    scalar_hash(h, b.lower);
    scalar_hash(h, b.upper);
  }
}
auto semantic_hash(const Diagnostic& d) -> std::uint64_t {
  std::uint64_t h{1469598103934665603ULL};
  mix(h, d.version);
  mix(h, static_cast<unsigned>(d.candidate));
  for (auto n : words(d))
    mix(h, n);
  for (auto n :
       {static_cast<unsigned>(d.original_result),
        static_cast<unsigned>(d.state), static_cast<unsigned>(d.stop_condition),
        static_cast<unsigned>(d.kind), static_cast<unsigned>(d.classification),
        static_cast<unsigned>(d.stop_stage)})
    mix(h, n);
  for (auto n :
       {d.stop_operation, d.source_operation, d.capture_operation,
        d.threshold_operation, d.comparison_operation, d.captured_side,
        d.capture_attempted, d.capture_written, d.threshold_attempted,
        d.threshold_written, d.comparison_attempted, d.comparison_written})
    mix(h, n);
  mix(h, d.source_evaluated);
  mix(h, d.source_admitted);
  mix(h, d.phase_invoked);
  mix(h, d.evidence_complete);
  mix(h, d.arithmetic_supported);
  scalar_hash(h, d.actual_first);
  scalar_hash(h, d.actual_last);
  mix(h, d.cells.size());
  bounds_hash(h, d.distance_squared);
  bounds_hash(h, d.minimum_squared);
  bounds_hash(h, d.maximum_squared);
  for (std::size_t i = 0; i < 4; ++i)
    if ((d.comparison_written & (1U << i)) != 0) mix(h, d.comparisons[i]);
  if (d.phase_invoked) {
    mix(h, static_cast<unsigned>(d.phase_reason.condition));
    mix(h, static_cast<unsigned>(d.phase_reason.predicate_condition));
    mix(h, d.phase_reason.side ? *d.phase_reason.side + 1 : 0);
    scalar_hash(h, d.phase_reason.first);
    scalar_hash(h, d.phase_reason.last);
    mix(h, d.phase_reason.depth);
    if (d.kind == Kind::reach_refusal) {
      scalar_hash(h, d.phase_reason.limiting_bound.lower);
      scalar_hash(h, d.phase_reason.limiting_bound.upper);
    }
  }
  if (const auto* s = d.source.summary()) {
    mix(h, s->version);
    mix(h, s->complete);
    for (const auto& identity : s->sources) {
      mix(h, identity.object);
      for (const auto& key : identity.keys) {
        mix(h, static_cast<unsigned>(key.buffer));
        mix(h, key.group);
        mix(h, key.triangle);
      }
      mix(h, identity.inventory_provenance
                 ? *identity.inventory_provenance + std::uint64_t{1}
                 : 0);
      scalar_hash(h, identity.plane);
      for (char c : identity.name)
        mix(h, static_cast<unsigned char>(c));
    }
  }
  return h;
}
[[gnu::noinline]] void accounting(const Diagnostic& d, const Limits& limits) {
  check(d.version == 1 && d.candidate == candidate,
        "Only registered version/candidate is represented");
  check(d.work.preflight_guards == 1,
        "Every structured report retains the fixed FP-first guard");
  const auto w = words(d);
  constexpr std::array<std::uint64_t, 8> fixed{1, 64, 1, 1, 2, 0, 3, 6};
  for (std::size_t i = 0; i < fixed.size(); ++i)
    check(w[i] <= fixed[i], "Original immutable one-call work ceiling");
  check(w[8] <= limits.capture_guards && w[9] <= limits.threshold_operations &&
            w[10] <= limits.comparison_operations,
        "Actual companion work stays within its effective lowered caps");
  for (std::size_t i = 1; i < w.size(); ++i)
    add_work(i, w[i]);
  check(d.actual_first == 0 && d.actual_last == 1,
        "Actual fixed call metadata is separate from private reason defaults");
  const auto fixed_bytes = 2 * sizeof(Expected);
  check(d.required_output_bytes ==
            fixed_bytes + sizeof(BoardingRouteFootPhaseCell),
        "Required input budget includes the original slot before S64");
  check(d.output_capacity_bytes ==
                fixed_bytes +
                    d.cells.capacity() * sizeof(BoardingRouteFootPhaseCell) &&
            d.output_capacity_bytes <= limits.output_bytes &&
            d.cells.capacity() <= 1 && d.cells.size() <= d.cells.capacity(),
        "Truthful one-allocation owned headers/slot boundary");
  check(mask_count(d.source_evaluated) == d.work.source_guards &&
            (!d.source_admitted ||
             d.source_evaluated == std::numeric_limits<std::uint64_t>::max()),
        "Actual S64 admission has a complete mask, never retrospective bits");
  check(d.phase_invoked == (d.work.phase_calls == 1) &&
            (!d.phase_invoked || (d.source_admitted && d.cells.size() == 1)),
        "One fresh source-bound original phase call owns its slot");
  if (!d.phase_invoked) {
    check(d.original_result == Original::not_run && d.work.phase.graphs == 0 &&
              d.work.phase.legs == 0 && d.work.capture_guards == 0,
          "Earlier source/output refusal cannot borrow phase or capture");
    no_evidence(d);
  } else {
    check(d.cells[0].first == 0 && d.cells[0].last == 1 &&
              d.phase_reason.first == 0 && d.phase_reason.last == 0 &&
              d.phase_reason.depth == 0 &&
              d.phase_reason.predicate_condition == PhaseCondition::none,
          "Private original temporal/predicate defaults remain unmodified");
    check(!d.cells[0].complete && !d.cells[0].arithmetic_supported,
          "Structural body0 prevents original aggregate qualification");
  }
  check((d.capture_written & ~d.capture_attempted) == 0 &&
            (d.threshold_written & ~d.threshold_attempted) == 0 &&
            (d.comparison_written & ~d.comparison_attempted) == 0 &&
            (d.threshold_attempted & 0xf0U) == 0 &&
            (d.comparison_attempted & 0xf0U) == 0 &&
            mask_count(d.capture_attempted) == d.work.capture_guards &&
            mask_count(d.threshold_attempted) == d.work.threshold_operations &&
            mask_count(d.comparison_attempted) == d.work.comparison_operations,
        "Successful charges produce attempted bits and written is a subset");
  if ((d.capture_written & (1U << 4)) == 0)
    no_evidence(d);
  else
    check(valid(d.distance_squared), "C05 alone publishes supported actual D");
  if ((d.threshold_written & (1U << 1)) == 0)
    no_scalar(d.maximum_squared);
  else
    check(valid(d.maximum_squared), "T02 publishes genuine supported maximum");
  if ((d.threshold_written & (1U << 3)) == 0)
    no_scalar(d.minimum_squared);
  else
    check(valid(d.minimum_squared), "T04 publishes genuine supported minimum");
  if (d.kind == Kind::not_run) {
    check(d.captured_side == 255 && (d.capture_written & (1U << 2)) == 0,
          "Ineligible C03 cannot issue a kind/side");
    no_evidence(d);
  } else {
    check(d.phase_invoked && (d.capture_written & (1U << 2)) != 0 &&
              d.captured_side < 2,
          "Capture kind and side require actual C03 readiness");
    if (d.kind == Kind::reach_refusal) {
      check(d.original_result == Original::unresolved &&
                d.phase_reason.condition == PhaseCondition::reach &&
                d.phase_reason.side == d.captured_side &&
                d.work.phase.graphs == 1 &&
                d.work.phase.legs == d.captured_side + std::uint64_t{1} &&
                d.work.phase.bodies == 0,
            "Reach-kind D has original same-call side/condition provenance");
      if (d.distance_squared.supported)
        check(d.distance_squared.lower == d.phase_reason.limiting_bound.lower &&
                  d.distance_squared.upper ==
                      d.phase_reason.limiting_bound.upper,
              "Reached refusal D is copied rather than default leg distance");
    } else {
      check(d.kind == Kind::intentional_body_stop &&
                d.original_result == Original::capacity &&
                d.phase_reason.condition == PhaseCondition::body_capacity &&
                !d.phase_reason.side && d.captured_side == 0 &&
                d.work.phase.graphs == 1 && d.work.phase.legs == 2 &&
                d.work.phase.bodies == 0 &&
                (d.state == State::capacity || d.state == State::unsupported),
            "Only intentional body0 stop supplies an earned PORT distance");
      for (const auto& leg : d.cells[0].legs)
        check(leg.nominal_links && leg.target_sole_identity &&
                  leg.derivative_domains && leg.joint_sectors &&
                  leg.timing_complete,
              "Both actual legs' five earned flags authenticate body-stop D");
      if (d.distance_squared.supported)
        check(d.distance_squared.lower ==
                      d.cells[0].legs[0].distance_squared.lower &&
                  d.distance_squared.upper ==
                      d.cells[0].legs[0].distance_squared.upper,
              "Only earned port leg distance is captured at body stop");
    }
  }
  if (d.comparison_attempted != 0)
    check(valid(d.distance_squared) && valid(d.minimum_squared) &&
              valid(d.maximum_squared) && d.threshold_written == 15 &&
              (d.capture_written & (1U << 6)) != 0,
          "Q comparisons only read C07-supported original bound tuples");
  if ((d.comparison_written & 1U) != 0)
    check(d.comparisons[0] ==
              (d.distance_squared.upper <= d.maximum_squared.lower),
          "Original safe upper inclusion is exact");
  if ((d.comparison_written & 2U) != 0)
    check(d.comparisons[1] ==
              (d.distance_squared.lower >= d.minimum_squared.upper),
          "Original safe lower inclusion is exact");
  if ((d.comparison_written & 4U) != 0)
    check(d.comparisons[2] ==
              (d.distance_squared.lower > d.maximum_squared.upper),
          "Strict too-long uses OUTER maximum with no equality relaxation");
  if ((d.comparison_written & 8U) != 0)
    check(d.comparisons[3] ==
              (d.distance_squared.upper < d.minimum_squared.lower),
          "Strict too-short uses OUTER minimum with no equality relaxation");
  check(d.evidence_complete == ((d.capture_written & 128U) != 0),
        "C08 alone earns complete comparison evidence");
  if (!d.evidence_complete)
    check(d.classification == Classification::not_run,
          "Partial bools never silently classify");
  else {
    check(d.capture_written == 255 && d.threshold_written == 15 &&
              d.comparison_written == 15 && d.arithmetic_supported,
          "Complete evidence requires all written supported stages");
    const bool included = d.comparisons[0] && d.comparisons[1];
    const auto expected = included ? Classification::sufficient_inclusion
                          : d.comparisons[2] ? Classification::strict_too_long
                          : d.comparisons[3]
                              ? Classification::strict_too_short
                              : Classification::unresolved_enclosure;
    check(d.classification == expected &&
              !(d.comparisons[2] && d.comparisons[3]) &&
              !(included && (d.comparisons[2] || d.comparisons[3])),
          "Classification uses earned comparisons with consistent strict "
          "converse");
    if (d.kind == Kind::reach_refusal)
      check(!included && d.state == State::evidence &&
                d.stop_condition == Condition::none,
            "Reach-refusal inclusion mismatch cannot earn evidence");
    else
      check(included && d.state == State::capacity &&
                d.stop_condition == Condition::original_capacity,
            "Complete reach evidence preserves original body capacity");
  }
  if (d.arithmetic_supported)
    check((d.capture_written & 16U) != 0 && valid(d.distance_squared) &&
              d.original_result != Original::unsupported,
          "New partial arithmetic flag has C05 support, not old aggregate");
  if ((d.capture_written & 16U) != 0 &&
      d.original_result != Original::unsupported &&
      d.stop_condition != Condition::unsupported_arithmetic)
    check(
        d.arithmetic_supported,
        "Later capacity/ordinary stop preserves C05-earned partial arithmetic");
  if (d.original_result == Original::unsupported)
    check(d.state == State::unsupported && !d.arithmetic_supported,
          "Original unsupported result is never promoted");
  if (d.original_result == Original::capacity)
    check(
        d.state == State::capacity || d.state == State::unsupported,
        "Companion ordinary readiness cannot downgrade original hard capacity");
  if (d.stop_condition == Condition::unsupported_arithmetic)
    check(d.state == State::unsupported && !d.arithmetic_supported,
          "Actual new arithmetic/FP failure clears partial support");
}
void print_bound(std::string_view name, Bounds b) {
  std::cout << ' ' << name << '=';
  if (b.supported)
    std::cout << '[' << b.lower << ',' << b.upper << ']';
  else
    std::cout << "NOT_RUN";
}
[[gnu::noinline]] void print_first(const Diagnostic& d) {
  std::cout << "FIRST_INTERMEDIATE_REACH_EVIDENCE01 A01 version=" << d.version
            << " candidate=" << unsigned(d.candidate)
            << " state=" << unsigned(d.state)
            << " original=" << unsigned(d.original_result)
            << " kind=" << unsigned(d.kind)
            << " class=" << unsigned(d.classification)
            << " complete=" << d.evidence_complete
            << " arithmetic=" << d.arithmetic_supported
            << " source=" << d.source_admitted << '/' << d.source_evaluated
            << " actual=[" << d.actual_first << ',' << d.actual_last << "]"
            << " capacity=" << d.cells.capacity()
            << " owned=" << d.output_capacity_bytes
            << " required=" << d.required_output_bytes << " work=";
  for (auto n : words(d))
    std::cout << n << ',';
  std::cout << " masks=" << unsigned(d.capture_attempted) << ','
            << unsigned(d.capture_written) << '/'
            << unsigned(d.threshold_attempted) << ','
            << unsigned(d.threshold_written) << '/'
            << unsigned(d.comparison_attempted) << ','
            << unsigned(d.comparison_written)
            << " cursors=" << unsigned(d.source_operation) << ','
            << unsigned(d.capture_operation) << ','
            << unsigned(d.threshold_operation) << ','
            << unsigned(d.comparison_operation)
            << " captured_side=" << unsigned(d.captured_side)
            << " stop=" << unsigned(d.stop_condition) << '/'
            << unsigned(d.stop_stage) << '/' << unsigned(d.stop_operation)
            << " phase=" << unsigned(d.phase_reason.condition) << '/'
            << unsigned(d.phase_reason.predicate_condition) << " side=";
  if (d.phase_reason.side)
    std::cout << *d.phase_reason.side;
  else
    std::cout << "NONE";
  std::cout << " private=[" << d.phase_reason.first << ','
            << d.phase_reason.last << "] depth=" << d.phase_reason.depth;
  print_bound("D", d.distance_squared);
  print_bound("min", d.minimum_squared);
  print_bound("max", d.maximum_squared);
  std::cout << " comparisons=";
  for (std::size_t i = 0; i < 4; ++i) {
    if ((d.comparison_written & (1U << i)) != 0)
      std::cout << d.comparisons[i];
    else
      std::cout << 'N';
  }
  std::cout << '\n' << std::flush;
}
[[gnu::noinline]] void create_source(const NativeCraftBinding& binding,
                                     const OriginBoardingBootSupport& boots,
                                     std::optional<Provider>& provider) {
  if (totals.creators != 0)
    throw std::runtime_error("Only one genuine creator");
  ++totals.creators;
  auto result = make_origin_boarding_intermediate_pause_support(binding, boots);
  std::cout << "FIRST_REACH_EVIDENCE01_SOURCE admitted=" << result.has_value();
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
          "Only unchanged genuine public constructor issues source");
    provider.emplace(std::move(*result));
  } else
    check(!result.error().empty(),
          "Actual source refusal is retained without retry");
  // The Expected and its moved-empty source die before FIRST/lifetime controls.
}
[[gnu::noinline]] void first_one(const Provider& provider, Summary& summary) {
  record(1, false);
  const auto result =
      assess_origin_boarding_intermediate_reach_evidence01(provider, candidate);
  if (!result) {
    std::cout << "FIRST_INTERMEDIATE_REACH_EVIDENCE01 A01 unexpected="
              << result.error() << '\n'
              << std::flush;
    check(!result.error().empty(),
          "Unexpected FIRST remains observable without retry");
    return;
  }
  print_first(*result); // Retain FIRST before any outcome/geometry expectation.
  check(result->state == State::evidence &&
            result->original_result == Original::unresolved &&
            result->kind == Kind::reach_refusal &&
            result->classification == Classification::strict_too_long &&
            result->captured_side == 0 && result->source_admitted &&
            result->phase_invoked && result->evidence_complete &&
            result->arithmetic_supported,
        "Retained A01 earns strict too-long PORT reach evidence");
  check(
      result->source_evaluated == 18446744073709551615ULL &&
          result->capture_attempted == 255 && result->capture_written == 255 &&
          result->threshold_attempted == 15 &&
          result->threshold_written == 15 &&
          result->comparison_attempted == 15 &&
          result->comparison_written == 15 && result->source_operation == 63 &&
          result->capture_operation == 7 && result->threshold_operation == 3 &&
          result->comparison_operation == 3 &&
          result->stop_condition == Condition::none &&
          result->stop_stage == Stage::not_run && result->stop_operation == 255,
      "Retained source and companion masks/cursors are fully earned");
  check(
      result->phase_reason.condition == PhaseCondition::reach &&
          result->phase_reason.predicate_condition == PhaseCondition::none &&
          result->phase_reason.side == 0 && result->phase_reason.first == 0 &&
          result->phase_reason.last == 0 && result->phase_reason.depth == 0 &&
          result->phase_reason.limiting_bound.lower == 1.0706311153846131 &&
          result->phase_reason.limiting_bound.upper == 1.0706311153846169,
      "Authentic original PORT refusal retains its earned bound and metadata");
  check(result->distance_squared.supported &&
            result->distance_squared.lower == 1.0706311153846131 &&
            result->distance_squared.upper == 1.0706311153846169 &&
            result->minimum_squared.supported &&
            result->minimum_squared.lower == 3.7248999999999497e-06 &&
            result->minimum_squared.upper == 3.7248999999999523e-06 &&
            result->maximum_squared.supported &&
            result->maximum_squared.lower == 0.89800261689999961 &&
            result->maximum_squared.upper == 0.89800261690000027,
        "Retained outward distance and original thresholds remain exact");
  check(
      !result->comparisons[0] && result->comparisons[1] &&
          result->comparisons[2] && !result->comparisons[3],
      "Retained written comparisons are 0110 with a strict too-long converse");
  check(result->work.preflight_guards == 1 &&
            result->work.source_guards == 64 && result->work.phase_calls == 1 &&
            result->work.phase.graphs == 1 && result->work.phase.legs == 1 &&
            result->work.phase.bodies == 0 && result->work.phase.sectors == 0 &&
            result->work.phase.timing == 0 &&
            result->work.capture_guards == 8 &&
            result->work.threshold_operations == 4 &&
            result->work.comparison_operations == 4,
        "Retained work stops the original phase before body or timing work");
  check(result->actual_first == 0 && result->actual_last == 1 &&
            result->cells.size() == 1 && result->cells.capacity() == 1 &&
            result->output_capacity_bytes == 8456 &&
            result->required_output_bytes == 8456,
        "Retained whole call owns one original slot and exact bounded output");
  const Limits limits;
  accounting(*result, limits);
  check(Access::data(result->source) == Access::data(provider),
        "Fresh diagnostic owns the same genuine source");
  summary.exists = true;
  const std::array counts{result->work.capture_guards,
                          result->work.threshold_operations,
                          result->work.comparison_operations};
  for (std::size_t i = 0; i < counts.size(); ++i) {
    check(counts[i] <= std::numeric_limits<std::uint32_t>::max(),
          "Compact baseline narrowing");
    summary.used[i] = static_cast<std::uint32_t>(counts[i]);
  }
  summary.hash = semantic_hash(*result);
  summary.required = result->required_output_bytes;
  summary.owned = result->output_capacity_bytes;
}
[[gnu::noinline]] void assessment(const Provider& provider, std::size_t slot,
                                  const Limits& limits, const Summary& summary,
                                  std::string_view error = {},
                                  std::size_t field = 4, bool exact = false,
                                  bool empty = false,
                                  Candidate id = candidate) {
  record(slot, false);
  const auto result =
      detail::intermediate_reach_evidence01_bounded(provider, id, limits);
  if (!error.empty()) {
    check(!result,
          "Preflight is unexpected, not a structured fabricated refusal");
    if (!result)
      check(result.error() == error,
            "Registered exact FP/candidate/cap/header error");
    return;
  }
  check(result.has_value(),
        "Finite companion control returns structured evidence/refusal");
  if (!result) return;
  accounting(*result, limits);
  check(Access::data(result->source) == Access::data(provider),
        "Copied/moved genuine owner binding is preserved");
  if (field < 3) {
    check(result->state == State::capacity &&
              result->stop_condition == capacities[field] &&
              result->stop_stage == stages[field] &&
              !result->evidence_complete &&
              result->classification == Classification::not_run,
          "Reached capacity remains hard without promoting partial classifier");
    const std::array counts{result->work.capture_guards,
                            result->work.threshold_operations,
                            result->work.comparison_operations};
    const std::array attempted{result->capture_attempted,
                               result->threshold_attempted,
                               result->comparison_attempted};
    const std::array written{result->capture_written, result->threshold_written,
                             result->comparison_written};
    const std::array cursors{result->capture_operation,
                             result->threshold_operation,
                             result->comparison_operation};
    const auto limit = limits.*members[field];
    check(counts[field] == limit && cursors[field] == limit &&
              result->stop_operation == limit &&
              (attempted[field] & (1U << limit)) == 0 &&
              (written[field] & (1U << limit)) == 0,
          "Failed upcoming charge has cursor but no count/attempt/written bit");
    if (field == 0 && limit == 0) no_evidence(*result);
    if (field == 1)
      check(result->comparison_attempted == 0 &&
                result->comparison_written == 0,
            "Threshold capacity cannot run a later comparison");
    if (field == 0 && limit == 7)
      check(result->threshold_written == 15 && result->comparison_written == 15,
            "C08 one-less retains earned T/Q while classifier stays NOT_RUN");
  }
  if (field == 3)
    check(result->state == State::capacity &&
              result->stop_condition == Condition::output_capacity &&
              result->stop_stage == Stage::output &&
              result->stop_operation == 255 &&
              result->work.source_guards == 0 &&
              result->work.phase_calls == 0 && result->cells.empty() &&
              result->cells.capacity() == 0,
          "Required-output-minus1 refuses room before source and graph");
  if (empty)
    check(result->state == State::unavailable &&
              result->stop_condition == Condition::invalid_binding &&
              result->work.source_guards == 1 &&
              result->source_evaluated == 1 && !result->source_admitted &&
              result->work.phase_calls == 0,
          "Moved-empty genuine alias refuses actual original S01");
  if (exact && summary.exists)
    check(
        semantic_hash(*result) == summary.hash &&
            result->required_output_bytes == summary.required &&
            result->output_capacity_bytes == summary.owned,
        "Exact-used/valid lifetime replay preserves earned semantic evidence");
}
[[gnu::noinline]] void isolated(const Provider& provider,
                                const Summary& summary) {
  const Limits defaults;
  for (std::size_t field = 0; field < 4; ++field) {
    if (field < 3 && (!summary.exists || summary.used[field] == 0)) {
      record(2 + field, true);
      continue;
    }
    auto limits = defaults;
    limits.*members[field] = 0;
    assessment(provider, 2 + field, limits, summary,
               field == 3 ? header_error : std::string_view{}, field);
  }
  for (std::size_t field = 0; field < 4; ++field) {
    if (!summary.exists || (field < 3 && summary.used[field] <= 1)) {
      record(6 + field, true);
      continue;
    }
    auto limits = defaults;
    limits.*members[field] =
        (field < 3 ? summary.used[field] : summary.required) - 1;
    assessment(provider, 6 + field, limits, summary, {}, field);
  }
  for (std::size_t field = 0; field < 4; ++field) {
    auto limits = defaults;
    limits.*members[field] += 1;
    assessment(provider, 10 + field, limits, summary, limits_error);
  }
}
[[gnu::noinline]] void exact_control(const Provider& provider,
                                     const Summary& summary) {
  if (!summary.exists) {
    record(14, true);
    return;
  }
  Limits limits;
  for (std::size_t i = 0; i < 3; ++i)
    limits.*members[i] = summary.used[i];
  limits.output_bytes = summary.required;
  assessment(provider, 14, limits, summary, {}, 4, true);
}
[[gnu::noinline]] void environments(const Provider& provider,
                                    const Summary& summary) {
  const Limits limits;
  for (std::size_t mode = 0; mode < 6; ++mode) {
    Environment changed(mode);
    if (changed.available)
      assessment(provider, 15 + mode, limits, summary, fp_error);
    else
      record(15 + mode, true);
  }
}
[[gnu::noinline]] void lifetime_controls(Provider& provider,
                                         const Summary& summary) {
  const Limits limits;
  assessment(provider, 21, limits, summary, id_error, 4, false, false,
             static_cast<Candidate>(255));
  {
    auto alias = provider;
    auto saved = std::move(alias);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Defined empty-handle test.
    assessment(alias, 22, limits, summary, {}, 4, false, true);
    alias = std::move(saved);
    check(Access::valid(alias),
          "Emptied alias restoration retains genuine owner");
  }
  {
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Copy test.
    const auto alias = provider;
    assessment(alias, 23, limits, summary, {}, 4, summary.exists);
  }
  {
    auto moved = std::move(provider);
    assessment(moved, 24, limits, summary, {}, 4, summary.exists);
    provider = std::move(moved);
  }
  {
    auto alias = provider;
    {
      auto discarded = std::move(provider);
      check(Access::valid(discarded),
            "Genuine original owner enters discarded scope");
    }
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Defined empty-handle test.
    check(!Access::valid(provider),
          "Original handle empty after discarded destruction");
    assessment(alias, 25, limits, summary, {}, 4, summary.exists);
    provider = std::move(alias);
  }
  assessment(provider, 26, limits, summary, {}, 4, summary.exists);
}
[[gnu::noinline]] void mixed(const Provider& provider, const Summary& summary) {
  const Limits defaults;
  if (summary.exists && summary.used[0] > 0) {
    auto limits = defaults;
    limits.capture_guards = 0;
    limits.threshold_operations = 0;
    assessment(provider, 27, limits, summary, {}, 0);
  } else
    record(27, true);
  if (summary.exists && summary.used[1] > 0) {
    auto limits = defaults;
    limits.threshold_operations = 0;
    limits.comparison_operations = 0;
    assessment(provider, 28, limits, summary, {}, 1);
  } else
    record(28, true);
}
[[gnu::noinline]] void final_counts() {
  check(
      totals.creators == 1 && totals.consumers + totals.skips == 28 &&
          (totals.attempted_slots | totals.skipped_slots) ==
              ((std::uint32_t{1} << 28) - 1) &&
          (totals.attempted_slots & totals.skipped_slots) == 0,
      "Exactly one creator and every literal slot executes or honestly skips");
  check(totals.work[0] == totals.consumers,
        "Fixed preflight guard count includes each unexpected invocation");
  constexpr std::array<std::uint32_t, 11> maximum{28, 1792, 28,  28,  56, 0,
                                                  84, 168,  224, 112, 112};
  for (std::size_t i = 0; i < maximum.size(); ++i)
    check(totals.work[i] <= maximum[i],
          "Finite aggregate caps before narrowing");
  std::cout << "REACH_EVIDENCE01_VALIDATION creator=" << totals.creators
            << " consumers=" << totals.consumers << " skips=" << totals.skips
            << " inspectors=0 raw=0 bodies=0 pairs=0 owners=0 hips=0"
            << " checks=" << checks << " failures=" << failures
            << " aggregate=";
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
      isolated(*source, summary);
      exact_control(*source, summary);
      environments(*source, summary);
      lifetime_controls(*source, summary);
      mixed(*source, summary);
    } else
      for (std::size_t slot = 1; slot <= 28; ++slot)
        record(slot, true);
    final_counts();
    std::cout.flush();
    std::cerr.flush();
    check(bytes <= 16384 && !out.truncated && !err.truncated &&
              std::cout.good() && std::cerr.good(),
          "Shared output budget and stream states do not silently truncate");
  } catch (const std::exception& e) {
    check(false, e.what());
  }
  std::cout.rdbuf(oldout);
  std::cerr.rdbuf(olderr);
  return failures == 0 ? 0 : 1;
}
