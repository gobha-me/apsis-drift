#include "apsis_drift/origin_boarding_intermediate_sector_evidence02.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
#include "origin_boarding_intermediate_sector_evidence02_internal.hpp"
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
using Diagnostic = BoardingIntermediateSectorEvidence02Diagnostic;
using Expected = BoardingIntermediateSectorEvidence02Expected;
using State = BoardingIntermediateSectorEvidence02State;
using Condition = BoardingIntermediateSectorEvidence02Condition;
using Kind = BoardingIntermediateSectorEvidence02Kind;
using Sector = BoardingIntermediateSectorEvidence02Sector;
using Classification = BoardingIntermediateSectorEvidence02Classification;
using Stage = BoardingIntermediateSectorEvidence02Stage;
using Original = BoardingIntermediateSectorEvidence02OriginalResult;
using Limits = detail::BoardingIntermediateSectorEvidence02Limits;
using Access = detail::BoardingIntermediatePauseSupportAccess;
using PhaseCondition = BoardingRouteFootPhaseCondition;
constexpr std::string_view fp_error =
    "intermediate sector evidence02 unsupported floating point";
constexpr std::string_view limits_error =
    "intermediate sector evidence02 invalid limits";
constexpr std::string_view header_error =
    "intermediate sector evidence02 output headers";
std::uint32_t checks{}, failures{}, next_slot{1};
void check(bool yes, std::string_view why) {
  if (checks == std::numeric_limits<std::uint32_t>::max())
    throw std::runtime_error("SectorEvidence02 check overflow");
  ++checks;
  if (!yes) {
    if (failures == std::numeric_limits<std::uint32_t>::max())
      throw std::runtime_error("SectorEvidence02 failure overflow");
    ++failures;
    std::cerr << "FAIL: " << why << '\n';
  }
}
struct Totals {
  std::array<std::uint32_t, 11> work{};
  std::uint32_t creators{}, consumers{}, skips{}, attempted_slots{},
      skipped_slots{}, raw{}, inspectors{}, bodies{};
} totals;
struct Summary {
  std::array<std::uint64_t, 4> used{};
  std::uint64_t hash{};
  std::size_t required{}, owned{};
  bool exists{};
};
static_assert(sizeof(Totals) <= 96 && sizeof(Summary) <= 128);
static_assert(sizeof(totals) + sizeof(checks) + sizeof(failures) +
                  sizeof(next_slot) <=
              108);
static_assert(sizeof(Expected) <= 1024 && sizeof(Limits) <= 40);
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

static_assert(sizeof(StreamCounter) <= 88 && sizeof(Environment) <= 16);

using Context = detail::BoardingIntermediateSectorEvidence02AdmissionContext;
using Key = detail::BoardingIntermediateSectorEvidence02ProgramKey;
using Token = detail::BoardingIntermediateSectorEvidence02CaptureToken;
using FreshCall = detail::BoardingIntermediateSectorEvidence02FreshCallRecord;
using Refusal = BoardingIntermediateSectorEvidence02Refusal;
static_assert(!std::is_default_constructible_v<Key> &&
              !std::is_constructible_v<Key, double>);
static_assert(!std::is_default_constructible_v<Context> &&
              !std::is_copy_constructible_v<Context> &&
              !std::is_move_constructible_v<Context> &&
              !std::is_constructible_v<Context, const Diagnostic&,
                                       const BoardingRouteFootPhaseRequest&,
                                       const Key&, const FreshCall&>);
static_assert(!std::is_default_constructible_v<Token> &&
              !std::is_constructible_v<Token, const Context&, const Diagnostic&,
                                       const BoardingRouteFootPhaseCell&,
                                       const BoardingRouteFootPhaseRequest&,
                                       const FreshCall&>);
static_assert(sizeof(Key) <= 16 && sizeof(Context) <= 40 &&
              sizeof(Token) <= 40 && sizeof(FreshCall) <= 16);
static_assert(!Diagnostic::source_qualified &&
              !Diagnostic::source_body_qualified &&
              !Diagnostic::body_qualified && !Diagnostic::contact_qualified &&
              !Diagnostic::support_qualified && !Diagnostic::self_qualified &&
              !Diagnostic::material_qualified && !Diagnostic::halo_qualified &&
              !Diagnostic::world_qualified && !Diagnostic::route_qualified &&
              !Diagnostic::pan_qualified && !Diagnostic::seat_qualified &&
              !Diagnostic::actor_qualified && !Diagnostic::save_qualified &&
              !Diagnostic::dynamics_qualified &&
              !Diagnostic::strength_qualified &&
              !Diagnostic::friction_qualified &&
              !Diagnostic::first_flight_qualified);
constexpr std::array<std::size_t Limits::*, 5> members{
    &Limits::source_guards, &Limits::construction_guards,
    &Limits::construction_operations, &Limits::capture_guards,
    &Limits::output_bytes};
constexpr std::array<Condition, 4> capacity_conditions{
    Condition::source_capacity, Condition::construction_guard_capacity,
    Condition::construction_operation_capacity, Condition::capture_capacity};
constexpr std::array<Stage, 4> capacity_stages{
    Stage::source, Stage::construction_guard, Stage::construction_operation,
    Stage::capture};
template <class T> auto bits(T mask) -> std::uint64_t {
  return static_cast<std::uint64_t>(std::popcount(mask));
}
auto prefix(std::uint64_t n) -> std::uint64_t {
  return n >= 64 ? std::numeric_limits<std::uint64_t>::max()
                 : (std::uint64_t{1} << n) - 1;
}
auto operation_bit(const std::array<std::uint64_t, 2>& mask,
                   std::uint64_t operation) -> bool {
  return operation < 78 &&
         (mask[operation / 64] & (std::uint64_t{1} << (operation % 64))) != 0;
}
auto operation_prefix(std::uint64_t count, std::size_t word) -> std::uint64_t {
  return prefix(word == 0 ? count : count > 64 ? count - 64 : 0);
}
void aggregate(std::size_t index, std::uint64_t n) {
  if (n > std::numeric_limits<std::uint32_t>::max() - totals.work[index])
    throw std::runtime_error("SectorEvidence02 aggregate overflow");
  totals.work[index] += static_cast<std::uint32_t>(n);
}
void record(std::size_t label, bool skipped) {
  if (label != next_slot || label > 30)
    throw std::runtime_error("SectorEvidence02 literal slot order");
  ++next_slot;
  const auto bit = std::uint32_t{1} << (label - 1);
  if (((totals.attempted_slots | totals.skipped_slots) & bit) != 0)
    throw std::runtime_error("SectorEvidence02 duplicate slot");
  if (skipped) {
    ++totals.skips;
    totals.skipped_slots |= bit;
    std::cout << "SKIP A" << label << " unreached_or_unavailable\n";
  } else {
    ++totals.consumers;
    totals.attempted_slots |= bit;
    aggregate(
        0, 1); // The actual fixed FP-first guard includes unexpected returns.
  }
}
auto words(const Diagnostic& d) -> std::array<std::uint64_t, 11> {
  return {d.work.preflight_guards,    d.work.source_guards,
          d.work.construction_guards, d.work.construction_operations,
          d.work.capture_guards,      d.work.phase_calls,
          d.work.phase.graphs,        d.work.phase.legs,
          d.work.phase.bodies,        d.work.phase.sectors,
          d.work.phase.timing};
}
auto used_counts(const Diagnostic& d) -> std::array<std::uint64_t, 4> {
  return {d.work.source_guards, d.work.construction_guards,
          d.work.construction_operations, d.work.capture_guards};
}
template <class B> auto valid(const B& b) -> bool {
  if constexpr (requires { b.supported; })
    if (!b.supported) return false;
  return std::isfinite(b.lower) && std::isfinite(b.upper) && b.lower <= b.upper;
}
void mix(std::uint64_t& h, std::uint64_t n) {
  h ^= n;
  h *= 1099511628211ULL;
}
void number(std::uint64_t& h, double n) {
  mix(h, std::bit_cast<std::uint64_t>(n));
}
template <class B> void hash_bound(std::uint64_t& h, const B& b) {
  if constexpr (requires { b.supported; }) {
    mix(h, b.supported);
    if (!b.supported) return;
  }
  number(h, b.lower);
  number(h, b.upper);
}
void hash_text(std::uint64_t& h, std::string_view text) {
  mix(h, text.size());
  for (char c : text)
    mix(h, static_cast<unsigned char>(c));
}
void hash_original(std::uint64_t& h, const BoardingRouteFootPhaseRefusal& r,
                   bool earned_bound) {
  mix(h, unsigned(r.condition));
  mix(h, unsigned(r.predicate_condition));
  mix(h, r.side ? *r.side + 1 : 0);
  number(h, r.first);
  number(h, r.last);
  mix(h, r.depth);
  if (earned_bound) hash_bound(h, r.limiting_bound);
}
void hash_companion(std::uint64_t& h, const Refusal& r) {
  mix(h, unsigned(r.condition));
  mix(h, unsigned(r.predicate_condition));
  mix(h, unsigned(r.stage));
  mix(h, r.operation);
  mix(h, r.side);
  mix(h, r.limiting_available);
  mix(h, r.predicate_available);
  if (r.predicate_available) mix(h, r.predicate_value);
  if (r.limiting_available) hash_bound(h, r.limiting_bound);
}
[[gnu::noinline]] void hash_source(std::uint64_t& h, const Diagnostic& d) {
  if (!d.source_enrolled) return;
  check(d.work.source_guards == 64 &&
            d.source_evaluated == std::numeric_limits<std::uint64_t>::max(),
        "Source identity hash requires actual successful full enrollment");
  if (d.work.source_guards != 64 ||
      d.source_evaluated != std::numeric_limits<std::uint64_t>::max())
    return;
  if (const auto* summary = d.source.summary()) {
    mix(h, summary->version);
    mix(h, summary->complete);
    for (const auto& s : summary->sources) {
      mix(h, s.object);
      hash_text(h, s.name);
      number(h, s.plane);
      mix(h, s.inventory_provenance ? *s.inventory_provenance + std::uint64_t{1}
                                    : 0);
      for (const auto& key : s.keys) {
        mix(h, unsigned(key.buffer));
        mix(h, key.group);
        mix(h, key.triangle);
      }
    }
  } else
    check(false, "Enrolled provider retains its genuine source summary");
  // Published identity-only binding: S64 succeeds only after actual IDs match.
  if (d.source_enrolled && d.work.source_guards == 64 &&
      d.source_evaluated == std::numeric_limits<std::uint64_t>::max())
    for (std::uint64_t id = 0; id < 15; ++id)
      mix(h, unsigned(static_cast<BoardingBodyPartId>(id)));
}
[[gnu::noinline]] auto semantic_hash(const Diagnostic& d) -> std::uint64_t {
  std::uint64_t h{1469598103934665603ULL};
  mix(h, d.version);
  mix(h, d.program_version);
  for (auto n : words(d))
    mix(h, n);
  mix(h, unsigned(d.state));
  mix(h, unsigned(d.original_result));
  mix(h, unsigned(d.kind));
  mix(h, unsigned(d.sector));
  if (d.classification_evaluated) mix(h, unsigned(d.classification));
  mix(h, unsigned(d.last_stage));
  mix(h, d.operation);
  mix(h, d.source_evaluated);
  mix(h, d.source_enrolled);
  mix(h, d.capture_cursor);
  mix(h, d.capture_attempted);
  mix(h, d.capture_written);
  mix(h, d.margin_read);
  mix(h, d.margin_written);
  mix(h, d.selected_side);
  mix(h, d.last_predicate_available);
  if (d.last_predicate_available) mix(h, d.last_predicate);
  mix(h, d.classification_evaluated);
  mix(h, d.arithmetic_supported);
  mix(h, d.evidence_complete);
  number(h, d.reporting_elapsed_seconds);
  mix(h, d.cells.size());
  mix(h, d.old_preparation_refusal_available);
  if (d.old_preparation_refusal_available) {
    const auto& r = d.old_preparation_refusal;
    mix(h, unsigned(r.condition));
    mix(h, unsigned(r.predicate_condition));
    hash_bound(h, r.limiting_bound);
    hash_original(h, r.phase, false); // Preparation never calls original phase.
    mix(h, r.side ? *r.side + 1 : 0);
    mix(h, r.edge ? *r.edge + 1 : 0);
    mix(h, r.axis ? *r.axis + 1 : 0);
    mix(h, r.source_key.has_value());
    if (r.source_key) {
      mix(h, unsigned(r.source_key->buffer));
      mix(h, r.source_key->group);
      mix(h, r.source_key->triangle);
    }
    hash_text(h, r.source_name);
    mix(h, r.self_pair);
    mix(h, r.operation);
    mix(h, r.self_region);
    mix(h, r.self_axis);
    mix(h, r.self_sign);
    mix(h, unsigned(r.self_stage));
    mix(h, r.source_edge);
  }
  mix(h, d.first_companion_refusal_available);
  mix(h, d.terminal_companion_refusal_available);
  if (d.first_companion_refusal_available)
    hash_companion(h, d.first_companion_refusal);
  if (d.terminal_companion_refusal_available)
    hash_companion(h, d.terminal_companion_refusal);
  for (std::size_t word = 0; word < 2; ++word) {
    mix(h, d.slice.operation_attempted[word]);
    mix(h, d.slice.operation_written[word]);
  }
  mix(h, d.slice.guard_attempted);
  mix(h, d.slice.guard_written);
  mix(h, d.slice.operation);
  mix(h, d.slice.guard);
  mix(h, d.slice.side);
  mix(h, unsigned(d.slice.condition));
  mix(h, d.slice.complete);
  mix(h, d.slice.arithmetic_supported);
  mix(h, d.slice.preflight_guards);
  if (operation_bit(d.slice.operation_written, 67)) number(h, d.slice.y);
  if (operation_bit(d.slice.operation_written, 63)) number(h, d.slice.lo);
  if (operation_bit(d.slice.operation_written, 64)) number(h, d.slice.hi);
  if (operation_bit(d.slice.operation_written, 40))
    mix(h, d.slice.zero_mask & 1U);
  if (operation_bit(d.slice.operation_written, 47))
    mix(h, d.slice.zero_mask & 2U);
  hash_bound(h, d.slice.limiting_bound);
  if (d.work.phase_calls == 1) {
    hash_original(h, d.original_reason, d.selected_limiting_available);
    for (const auto& c : d.cells) {
      number(h, c.first);
      number(h, c.last);
      mix(h, c.complete);
      mix(h, c.arithmetic_supported);
      for (const auto& leg : c.legs) {
        mix(h, leg.nominal_links);
        mix(h, leg.target_sole_identity);
        mix(h, leg.joint_sectors);
        mix(h, leg.derivative_domains);
        mix(h, leg.timing_complete);
      }
    }
  }
  for (std::size_t i = 0; i < 6; ++i)
    if (d.margin_written & (1U << i)) hash_bound(h, d.sector_margins[i]);
  mix(h, d.selected_limiting_available);
  if (d.selected_limiting_available) hash_bound(h, d.selected_limiting_bound);
  hash_source(h, d);
  return h;
}
void no_capture(const Diagnostic& d) {
  check(d.kind == Kind::not_run && d.sector == Sector::not_run &&
            d.selected_side == 255 && d.margin_read == 0 &&
            d.margin_written == 0 && !d.selected_limiting_available &&
            !d.classification_evaluated &&
            d.classification == Classification::not_run &&
            !d.evidence_complete && !d.arithmetic_supported,
        "Unreached attribution does not promote default margins or "
        "classification");
}
void cursor(Stage stage, std::uint8_t operation) {
  const auto maximum = stage == Stage::source                   ? 63
                       : stage == Stage::construction_guard     ? 20
                       : stage == Stage::construction_operation ? 77
                       : stage == Stage::capture                ? 7
                                                                : 255;
  check(stage == Stage::complete ? operation == 7
        : maximum == 255         ? operation == 255
                                 : operation <= maximum,
        "Stage selects its actual bounded cursor family");
}
void companion_accounting(const Refusal& r) {
  cursor(r.stage, r.operation);
  check(r.side == 255 || r.side < 2,
        "Companion side is absent or original leg index");
  if (r.limiting_available)
    check(valid(r.limiting_bound),
          "Published stop bound is actually supported finite ordered");
}
[[gnu::noinline]] void slice_accounting(const Diagnostic& d, const Limits& l) {
  const auto& x = d.slice;
  check(d.work.construction_guards <= l.construction_guards &&
            d.work.construction_operations <= l.construction_operations,
        "Fresh construction work obeys its own effective lowered caps");
  for (std::size_t word = 0; word < 2; ++word)
    check(x.operation_attempted[word] ==
                  operation_prefix(d.work.construction_operations, word) &&
              (x.operation_written[word] & ~x.operation_attempted[word]) == 0,
          "Each actual construction word distinguishes attempted from written");
  check(x.guard_attempted == prefix(d.work.construction_guards) &&
            (x.guard_written & ~x.guard_attempted) == 0 &&
            (x.guard_attempted & ~0x1fffffU) == 0 &&
            (x.operation_attempted[1] & ~0x3fffULL) == 0 &&
            (x.operation_written[1] & ~0x3fffULL) == 0 &&
            x.preflight_guards == 0,
        "Literal G21/O78 unused bits and old wrapper-only preflight stay "
        "unearned");
  check((x.operation == 255 || x.operation < 78) &&
            (x.guard == 255 || x.guard < 21) && (x.side == 255 || x.side < 2) &&
            (x.zero_mask & ~3U) == 0,
        "Construction cursor and zero masks are literal bounded fields");
  if (x.zero_mask & 1U)
    check(operation_bit(x.operation_written, 40),
          "PORT zero choice requires O41 write");
  if (x.zero_mask & 2U)
    check(operation_bit(x.operation_written, 47),
          "STAR zero choice requires O48 write");
  if (operation_bit(x.operation_written, 63))
    check(std::isfinite(x.lo), "Written O64 lo is finite");
  if (operation_bit(x.operation_written, 64))
    check(std::isfinite(x.hi), "Written O65 hi is finite");
  if (operation_bit(x.operation_written, 67))
    check(std::isfinite(x.y), "Written O68 coefficient is finite");
  if (x.guard_written & (1U << 9))
    check(operation_bit(x.operation_written, 63) &&
              operation_bit(x.operation_written, 64) && x.lo < x.hi,
          "G10 requires actual earned nonempty interval");
  if (x.guard_written & (1U << 11))
    check(operation_bit(x.operation_written, 67) && x.lo < x.y,
          "G12 uses actual strictly interior stored Y");
  if (x.guard_written & (1U << 12))
    check(operation_bit(x.operation_written, 67) && x.y < x.hi,
          "G13 uses actual strictly interior stored Y");
  if (x.complete)
    check(
        x.operation_attempted[0] == UINT64_MAX &&
            x.operation_written[0] == UINT64_MAX &&
            x.operation_attempted[1] == 0x3fff &&
            x.operation_written[1] == 0x3fff &&
            x.guard_attempted == 0x1fffffU && x.guard_written == 0x1fffffU &&
            d.work.construction_operations == 78 &&
            d.work.construction_guards == 21 && x.operation == 77 &&
            x.guard == 20 && x.side == 255 && x.arithmetic_supported &&
            x.condition == BoardingIntermediateEndpoint03SliceCondition::none &&
            d.source_enrolled && x.lo < x.y && x.y < x.hi,
        "Complete Slice is earned independently of later phase/capture status");
  check(d.reporting_elapsed_seconds == 0 || d.reporting_elapsed_seconds == 2,
        "Clock is only unearned0 or requested hold2");
  if (d.reporting_elapsed_seconds == 2)
    check(x.complete, "Requested clock requires actual complete construction "
                      "and private Key");
  if (!x.complete)
    check(d.reporting_elapsed_seconds == 0 && d.work.phase_calls == 0 &&
              d.cells.empty(),
          "Incomplete construction cannot borrow graph or requested clock");
}
[[gnu::noinline]] void capture_accounting(const Diagnostic& d) {
  check((d.capture_written & ~d.capture_attempted) == 0 &&
            bits(d.capture_attempted) == d.work.capture_guards &&
            d.capture_attempted == prefix(d.work.capture_guards),
        "Successful capture charge owns one attempted bit per row");
  check(d.capture_cursor == 255 || d.capture_cursor < 8,
        "Capture cursor is registered zero-based row");
  check((d.margin_read & ~0x3fU) == 0 &&
            (d.margin_written & ~d.margin_read) == 0 &&
            d.margin_read == prefix(bits(d.margin_read)),
        "Only actual original reached margin prefix is read");
  if (d.last_predicate_available)
    check(d.capture_cursor < 8 &&
              (d.capture_written & (1U << d.capture_cursor)) != 0,
          "Last predicate availability requires actual evaluated CHECK");
  if (d.margin_read != 0)
    check((d.capture_attempted & 16U) != 0 && d.selected_side < 2 &&
              d.cells.size() == 1,
          "Only actual C05 with earned original side can borrow sector slots");
  for (std::size_t i = 0; i < 6; ++i)
    if (d.margin_written & (1U << i)) {
      check(valid(d.sector_margins[i]),
            "Only written original margin is numerically inspected");
      if (d.selected_side < 2 && d.cells.size() == 1) {
        const auto& original =
            d.cells[0].legs[d.selected_side].sector_margins[i];
        check(d.sector_margins[i].lower == original.lower &&
                  d.sector_margins[i].upper == original.upper,
              "Captured original margin endpoints are copied exactly without "
              "reconstruction");
      }
      if (i + 1 < 6 && (d.margin_written & (1U << (i + 1))))
        check(d.sector_margins[i].lower >= 0,
              "Original write order stops before any margin after first "
              "negative");
    }
  if (d.kind == Kind::not_run) {
    no_capture(d);
    if ((d.capture_written & 4U) != 0)
      check(d.capture_cursor == 2 && d.last_predicate_available &&
                !d.last_predicate,
            "Ineligible C03 writes false readiness without inventing a kind");
  } else {
    check(d.work.phase_calls == 1 && (d.capture_written & 4U) != 0,
          "Kind requires fresh original return and evaluated C03");
    if (d.kind == Kind::joint_sector_refusal)
      check(d.original_result == Original::unresolved &&
                d.original_reason.condition == PhaseCondition::joint_sector &&
                d.original_reason.side && *d.original_reason.side < 2,
            "Joint kind has genuine original side-present refusal");
    else {
      check(d.kind == Kind::intentional_body_stop &&
                d.original_result == Original::capacity &&
                d.original_reason.condition == PhaseCondition::body_capacity &&
                !d.original_reason.side && d.work.phase.graphs == 1 &&
                d.work.phase.legs == 2 && d.work.phase.bodies == 0 &&
                d.work.phase.sectors == 2 && d.work.phase.timing == 2,
            "Intentional body0 is a hard stop with completed original sectors");
      if (d.cells.size() == 1)
        for (const auto& leg : d.cells[0].legs)
          check(leg.nominal_links && leg.target_sole_identity &&
                    leg.joint_sectors && leg.derivative_domains &&
                    leg.timing_complete,
                "Both original legs' five flags are earned at body stop");
    }
  }
  if (d.selected_side < 2) {
    check((d.capture_written & 8U) != 0,
          "Selected side is only published after C04 predicate");
    if (d.kind == Kind::joint_sector_refusal)
      check(d.original_reason.side == d.selected_side &&
                d.work.phase.graphs == 1 &&
                d.work.phase.legs == d.selected_side + std::uint64_t{1} &&
                d.work.phase.bodies == 0 &&
                d.work.phase.sectors == d.selected_side + std::uint64_t{1} &&
                d.work.phase.timing == d.selected_side,
            "Selected original side matches exact lazy sector/timing work "
            "prefix");
    else
      check(d.kind == Kind::intentional_body_stop && d.selected_side == 1,
            "Body-stop compact passed record uses STARBOARD");
  }
  if (d.sector != Sector::not_run) {
    check(d.kind == Kind::joint_sector_refusal &&
              (d.capture_written & 16U) != 0 && unsigned(d.sector) <= 6,
          "Only successful C05 can name one of seven original stopping "
          "alternatives");
    if (unsigned(d.sector) < 6)
      check((d.margin_written & (1U << unsigned(d.sector))) != 0 &&
                d.margin_written == prefix(unsigned(d.sector) + 1) &&
                d.sector_margins[unsigned(d.sector)].lower < 0,
            "Named margin is exactly the first negative reached original lower "
            "bound");
    else {
      check(d.margin_written == 63,
            "Positive-shin attribution follows all six nonnegative margins");
      for (std::size_t i = 0; i < 6; ++i)
        if (d.margin_written & (1U << i))
          check(d.sector_margins[i].lower >= 0,
                "Every published positive-shin predecessor passed its margin");
    }
  }
  if (d.selected_limiting_available) {
    check((d.capture_written & 32U) != 0 &&
              d.kind == Kind::joint_sector_refusal &&
              valid(d.selected_limiting_bound) &&
              valid(d.original_reason.limiting_bound),
          "Only successful C06 publishes persistent supported selected "
          "limiting evidence");
    check(d.selected_limiting_bound.lower ==
                  d.original_reason.limiting_bound.lower &&
              d.selected_limiting_bound.upper ==
                  d.original_reason.limiting_bound.upper,
          "Owned selected evidence matches original reason exactly and "
          "survives later row scratch reset");
    if (unsigned(d.sector) < 6)
      check(d.selected_limiting_bound.lower ==
                    d.sector_margins[unsigned(d.sector)].lower &&
                d.selected_limiting_bound.upper ==
                    d.sector_margins[unsigned(d.sector)].upper &&
                d.selected_limiting_bound.lower < 0,
            "C06 names actual margin by exact bound equality, never closeness");
    else
      check(d.sector == Sector::positive_shin && d.margin_written == 63 &&
                d.selected_limiting_bound.lower <= 0,
            "Separate strict-positive shin guard uses its authentic original "
            "reason");
  } else
    check(!d.selected_limiting_bound.supported,
          "Unavailable persistent selection is not a default supported bound");
  if (d.kind == Kind::intentional_body_stop) {
    check(d.sector == Sector::not_run && !d.selected_limiting_available,
          "Passed-sector body stop does not manufacture failure ordinal or "
          "limiting bound");
    for (std::size_t i = 0; i < 6; ++i)
      if (d.margin_written & (1U << i))
        check(
            d.sector_margins[i].lower >= 0,
            "Body stop only retains actually passed nonnegative sector slots");
  }
  if (d.classification_evaluated) {
    check((d.capture_written & 64U) != 0 && d.kind != Kind::not_run,
          "Evaluated classification belongs to actual C07, not final default "
          "flags");
    if (d.kind == Kind::intentional_body_stop)
      check(d.classification == Classification::original_sectors_complete &&
                d.margin_written == 63,
            "Body-stop complete sectors remain distinct from failed-sector "
            "classification");
    else {
      check(d.selected_limiting_available,
            "Joint classification requires persistent genuine C06 bound");
      if (d.selected_limiting_available)
        check(d.classification ==
                  ((d.sector == Sector::positive_shin
                        ? d.selected_limiting_bound.upper <= 0
                        : d.selected_limiting_bound.upper < 0)
                       ? Classification::strict_necessary_violation
                       : Classification::interval_inconclusive),
              "Exact strict converse distinguishes nonnegative margins from "
              "positive shin without tolerance");
    }
  } else
    check(d.classification == Classification::not_run,
          "Unexecuted C07 never classifies");
  if (d.evidence_complete)
    check(d.capture_written == 255 && d.capture_attempted == 255 &&
              d.classification_evaluated && d.arithmetic_supported &&
              d.kind != Kind::not_run && d.last_stage == Stage::complete &&
              d.operation == 7 && d.capture_cursor == 7 &&
              d.last_predicate_available && d.last_predicate,
          "Final evidence requires all evaluated successful capture rows, not "
          "original body acceptance");
  if (d.arithmetic_supported)
    check((d.capture_attempted & 16U) != 0 && d.margin_written != 0 &&
              d.original_result != Original::unsupported,
          "Partial companion arithmetic starts only at actual supported C05 "
          "prefix");
}
[[gnu::noinline]] void accounting(const Diagnostic& d, const Limits& limits) {
  check(d.version == 2 && d.program_version == 3 &&
            d.work.preflight_guards == 1,
        "Structured report retains registered program/version and its actual "
        "FP-first guard");
  const auto w = words(d);
  constexpr std::array<std::uint64_t, 11> maximum{1, 64, 21, 78, 8, 1,
                                                  1, 2,  0,  3,  6};
  for (std::size_t i = 0; i < w.size(); ++i) {
    check(w[i] <= maximum[i], "Original and companion bounded charge ceilings");
    if (i != 0) aggregate(i, w[i]);
  }
  const auto counts = used_counts(d);
  for (std::size_t i = 0; i < counts.size(); ++i)
    check(counts[i] <= limits.*members[i],
          "Effective lower-only successful counts");
  check(d.required_output_bytes ==
            boarding_intermediate_sector_evidence02_required_output_bytes(),
        "Required budget is immutable two headers plus one original slot");
  check(d.cells.size() <= 1 && d.cells.capacity() <= 1 &&
            d.cells.size() <= d.cells.capacity() &&
            d.output_capacity_bytes ==
                2 * sizeof(Expected) +
                    d.cells.capacity() * sizeof(BoardingRouteFootPhaseCell) &&
            d.output_capacity_bytes <= limits.output_bytes,
        "Actual one-slot allocation and header bytes are truthful");
  check(d.source_evaluated == prefix(d.work.source_guards),
        "Only actual source charges own S64 prefix bits");
  if (d.source_enrolled)
    check(d.work.source_guards == 64 &&
              d.source_evaluated == std::numeric_limits<std::uint64_t>::max() &&
              Access::valid(d.source),
          "Final true source enrollment is required even when all64 bits were "
          "attempted");
  slice_accounting(d, limits);
  if (d.old_preparation_refusal_available) {
    check(d.work.phase_calls == 0 && d.cells.empty(),
          "Full old refusal is metadata from actual failed preparation only");
    const auto& r = d.old_preparation_refusal;
    check(r.operation == 255 || r.operation < 78,
          "Retained old upcoming operation has its original bounded cursor");
    if (r.limiting_bound.supported)
      check(
          valid(r.limiting_bound),
          "Actual old supported refusal bound retains all original endpoints");
    if (r.source_key) {
      bool found{};
      for (std::size_t side = 0; side < 2; ++side)
        if (const auto* q = Access::partition(d.source, side))
          found = found || ((q->faces[0].key == *r.source_key ||
                             q->faces[1].key == *r.source_key) &&
                            q->faces[0].source_object == r.source_name);
      check(found, "Old optional source key/name remains anchored to genuine "
                   "immutable provider");
    }
  }
  if (d.work.phase_calls == 0) {
    check(d.original_result == Original::not_run && d.work.phase.graphs == 0 &&
              d.work.phase.legs == 0 && d.work.capture_guards == 0 &&
              d.capture_attempted == 0 && d.cells.empty(),
          "Earlier refusal cannot borrow graph/body/capture");
    no_capture(d);
  } else {
    check(d.cells.size() == 1 && d.source_enrolled && d.slice.complete &&
              d.reporting_elapsed_seconds == 2,
          "Exactly one fresh original call follows new issuer's source/program "
          "admission");
    if (d.cells.size() == 1)
      check(d.cells[0].first == 0 && d.cells[0].last == 1 &&
                !d.cells[0].complete && !d.cells[0].arithmetic_supported,
            "Fixed whole forward body0 call leaves aggregate body flags "
            "unearned");
    check(d.original_reason.first == 0 && d.original_reason.last == 0 &&
              d.original_reason.depth == 0 &&
              d.original_reason.predicate_condition == PhaseCondition::none,
          "Original private default reason metadata is not invocation range");
  }
  cursor(d.last_stage, d.operation);
  if (d.last_stage == Stage::complete)
    check(
        d.operation == 7 && d.capture_cursor == 7 &&
            d.work.capture_guards == 8 && d.capture_attempted == 255 &&
            d.capture_written == 255 && d.evidence_complete &&
            d.last_predicate_available && d.last_predicate &&
            d.classification_evaluated && d.arithmetic_supported,
        "Complete stage retains the actually completed C08 cursor and closure");
  if (d.first_companion_refusal_available)
    companion_accounting(d.first_companion_refusal);
  if (d.terminal_companion_refusal_available)
    companion_accounting(d.terminal_companion_refusal);
  capture_accounting(d);
  if (d.original_result == Original::capacity)
    check(d.state == State::capacity,
          "Original capacity remains hard despite later companion finding");
  if (d.original_result == Original::unsupported)
    check(d.state == State::unsupported && !d.arithmetic_supported,
          "Original unsupported result is never promoted by captured default "
          "geometry");
  if (d.terminal_companion_refusal_available) {
    const auto condition = d.terminal_companion_refusal.condition;
    if (condition == Condition::capture_capacity ||
        condition == Condition::source_capacity ||
        condition == Condition::construction_guard_capacity ||
        condition == Condition::construction_operation_capacity ||
        condition == Condition::output_capacity)
      check(d.state == State::capacity ||
                d.original_result == Original::unsupported,
            "Later companion capacity preserves any stronger original "
            "unsupported state");
    if (condition == Condition::unsupported_arithmetic)
      check(!d.arithmetic_supported &&
                (d.state == State::unsupported ||
                 d.original_result == Original::capacity),
            "New attempted unsupported arithmetic clears only its partial "
            "flag, original hard state survives");
  }
}

void print_bound(std::string_view name, const BoardingFootSiteScalarBounds& b) {
  std::cout << ' ' << name << '=';
  if (b.supported)
    std::cout << '[' << b.lower << ',' << b.upper << ']';
  else
    std::cout << "NOT_RUN";
}
[[gnu::noinline]] void print_first(const Diagnostic& d) {
  std::cout << "FIRST_INTERMEDIATE_SECTOR_EVIDENCE02 A01 version=" << d.version
            << " program=" << d.program_version
            << " state=" << unsigned(d.state)
            << " original=" << unsigned(d.original_result)
            << " kind=" << unsigned(d.kind) << " sector=" << unsigned(d.sector)
            << " class=" << unsigned(d.classification)
            << " complete=" << d.evidence_complete
            << " arithmetic=" << d.arithmetic_supported
            << " source=" << d.source_enrolled << '/' << d.source_evaluated
            << " clock=" << d.reporting_elapsed_seconds
            << " capacity=" << d.cells.capacity()
            << " owned=" << d.output_capacity_bytes
            << " required=" << d.required_output_bytes << " work=";
  for (auto n : words(d))
    std::cout << n << ',';
  std::cout << " slice=" << unsigned(d.slice.condition) << '/'
            << d.slice.complete << '/' << d.slice.arithmetic_supported
            << " masks=" << d.slice.operation_attempted[0] << ','
            << d.slice.operation_attempted[1] << ':'
            << d.slice.operation_written[0] << ','
            << d.slice.operation_written[1] << '/' << d.slice.guard_attempted
            << ',' << d.slice.guard_written
            << " cursors=" << unsigned(d.slice.operation) << ','
            << unsigned(d.slice.guard) << ',' << unsigned(d.slice.side)
            << " zero=" << unsigned(d.slice.zero_mask)
            << " slice_preflight=" << unsigned(d.slice.preflight_guards);
  if (operation_bit(d.slice.operation_written, 67))
    std::cout << " Y=" << d.slice.y;
  if (operation_bit(d.slice.operation_written, 63))
    std::cout << " lo=" << d.slice.lo;
  if (operation_bit(d.slice.operation_written, 64))
    std::cout << " hi=" << d.slice.hi;
  std::cout << " last_predicate=";
  if (d.last_predicate_available)
    std::cout << d.last_predicate;
  else
    std::cout << "NOT_RUN";
  std::cout << " capture=" << unsigned(d.capture_attempted) << ','
            << unsigned(d.capture_written) << '/' << unsigned(d.capture_cursor)
            << " margins=" << unsigned(d.margin_read) << ','
            << unsigned(d.margin_written)
            << " side=" << unsigned(d.selected_side) << " phase=";
  if (d.work.phase_calls == 1) {
    std::cout << unsigned(d.original_reason.condition) << '/'
              << unsigned(d.original_reason.predicate_condition)
              << " phase_side=";
    if (d.original_reason.side)
      std::cout << *d.original_reason.side;
    else
      std::cout << "NONE";
    std::cout << " private=[" << d.original_reason.first << ','
              << d.original_reason.last << "] depth=" << d.original_reason.depth
              << " nested_bound="
              << (d.selected_limiting_available ? "CAPTURE_EARNED"
                                                : "NOT_INTERPRETED");
  } else
    std::cout << "NOT_RUN";
  std::cout << " old_preparation=";
  if (d.old_preparation_refusal_available)
    std::cout << unsigned(d.old_preparation_refusal.condition) << '/'
              << unsigned(d.old_preparation_refusal.operation);
  else
    std::cout << "NOT_RUN";
  for (std::size_t i = 0; i < 6; ++i)
    if (d.margin_written & (1U << i))
      std::cout << " m" << i << "=[" << d.sector_margins[i].lower << ','
                << d.sector_margins[i].upper << ']';
  if (d.selected_limiting_available)
    print_bound("selected", d.selected_limiting_bound);
  std::cout << " stop=";
  if (d.terminal_companion_refusal_available)
    std::cout << unsigned(d.terminal_companion_refusal.condition) << '/'
              << unsigned(d.terminal_companion_refusal.stage) << '/'
              << unsigned(d.terminal_companion_refusal.operation);
  else
    std::cout << "NOT_RUN";
  std::cout << '\n' << std::flush;
}
[[gnu::noinline]] void create_source(const NativeCraftBinding& binding,
                                     const OriginBoardingBootSupport& boots,
                                     std::optional<Provider>& provider) {
  if (totals.creators != 0)
    throw std::runtime_error("Only one genuine creator");
  ++totals.creators;
  auto result = make_origin_boarding_intermediate_pause_support(binding, boots);
  std::cout << "FIRST_SECTOR_EVIDENCE02_SOURCE admitted=" << result.has_value();
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
          "Only genuine unchanged public creator admits provider");
    provider.emplace(std::move(*result));
  } else
    check(!result.error().empty(),
          "Actual creator refusal is retained without retry");
  // The creator Expected and its moved-empty provider are destroyed before A01.
}
[[gnu::noinline]] void first_one(const Provider& provider, Summary& summary) {
  record(1, false);
  const auto result =
      assess_origin_boarding_intermediate_sector_evidence02(provider);
  if (!result) {
    std::cout << "FIRST_INTERMEDIATE_SECTOR_EVIDENCE02 A01 unexpected="
              << result.error() << '\n'
              << std::flush;
    check(!result.error().empty(),
          "Actual unexpected FIRST is observable without retry");
    return;
  }
  print_first(*result);
  const Limits limits;
  accounting(*result, limits);
  check(Access::data(result->source) == Access::data(provider),
        "Fresh report retains genuine provider owner");
  summary.exists = true;
  summary.used = used_counts(*result);
  summary.hash = semantic_hash(*result);
  summary.required = result->required_output_bytes;
  summary.owned = result->output_capacity_bytes;
}
[[gnu::noinline]] void assessment(const Provider& provider, std::size_t slot,
                                  const Limits& limits, const Summary& summary,
                                  std::string_view error = {},
                                  std::size_t field = 5, bool exact = false,
                                  bool empty = false) {
  record(slot, false);
  const auto result =
      detail::intermediate_sector_evidence02_bounded(provider, limits);
  if (!error.empty()) {
    check(!result, "Registered preflight returns unexpected rather than "
                   "fabricated report");
    if (!result)
      check(result.error() == error,
            "Exact FP-first or lowered-limit/header error");
    return;
  }
  check(result.has_value(),
        "Finite control retains structured actual refusal/evidence");
  if (!result) return;
  accounting(*result, limits);
  check(Access::data(result->source) == Access::data(provider),
        "Fresh alias report binds same actual provider");
  if (field < 4) {
    check(result->state == State::capacity ||
              result->original_result == Original::unsupported,
          "Reached companion capacity remains hard without downgrading "
          "original unsupported");
    check(result->terminal_companion_refusal_available &&
              result->terminal_companion_refusal.condition ==
                  capacity_conditions[field] &&
              result->terminal_companion_refusal.stage ==
                  capacity_stages[field] &&
              !result->evidence_complete,
          "Isolated reached cap owns exact current terminal cause and stage");
    const auto count = used_counts(*result)[field];
    const auto cap = limits.*members[field];
    check(count == cap && result->operation == cap &&
              result->terminal_companion_refusal.operation == cap,
          "Upcoming failed charge retains cursor without an extra successful "
          "count");
    if (field == 0)
      check(result->source_evaluated == prefix(cap) &&
                !result->source_enrolled &&
                result->work.construction_guards == 0 &&
                result->work.construction_operations == 0 &&
                result->work.phase_calls == 0,
            "Source cap precedes every construction and original phase "
            "operation");
    if (field == 1)
      check(
          cap < 21 &&
              (result->slice.guard_attempted & (std::uint32_t{1} << cap)) ==
                  0 &&
              (result->slice.guard_written & (std::uint32_t{1} << cap)) == 0 &&
              result->work.phase_calls == 0,
          "Failed construction guard earns no attempted/written bit or phase");
    if (field == 2)
      check(cap < 78 &&
                !operation_bit(result->slice.operation_attempted, cap) &&
                !operation_bit(result->slice.operation_written, cap) &&
                result->work.phase_calls == 0,
            "Failed construction operation earns no attempted/written bit or "
            "phase");
    if (field == 3) {
      check(cap < 8 && (result->capture_attempted & (1U << cap)) == 0 &&
                (result->capture_written & (1U << cap)) == 0 &&
                result->capture_cursor == cap,
            "Failed capture charge reads no new row");
      if (cap == 0) no_capture(*result);
      if (cap == 7 && result->kind == Kind::joint_sector_refusal)
        check(result->selected_limiting_available &&
                  result->classification_evaluated,
              "C08 one-less retains earlier persistent C06 bound and C07 "
              "classification");
    }
  }
  if (field == 4)
    check(result->state == State::capacity &&
              result->terminal_companion_refusal_available &&
              result->terminal_companion_refusal.condition ==
                  Condition::output_capacity &&
              result->terminal_companion_refusal.stage == Stage::output &&
              result->terminal_companion_refusal.operation == 255 &&
              result->work.source_guards == 0 &&
              result->work.phase_calls == 0 && result->cells.empty() &&
              result->cells.capacity() == 0,
          "Required-minus-one room refusal precedes source/graph and owns only "
          "headers");
  if (empty)
    check(result->state == State::unresolved &&
              result->terminal_companion_refusal_available &&
              result->terminal_companion_refusal.condition ==
                  Condition::invalid_binding &&
              result->work.source_guards == 1 &&
              result->source_evaluated == 1 && !result->source_enrolled &&
              result->work.construction_guards == 0 &&
              result->work.phase_calls == 0,
          "Genuinely moved-empty source refuses charged original S01");
  if (exact && summary.exists)
    check(semantic_hash(*result) == summary.hash &&
              result->required_output_bytes == summary.required &&
              result->output_capacity_bytes == summary.owned,
          "Exact used or genuine lifetime replay preserves all earned bytes "
          "and truthful owned allocation");
}
[[gnu::noinline]] void isolated(const Provider& provider,
                                const Summary& summary) {
  const Limits defaults;
  for (std::size_t field = 0; field < 5; ++field) {
    if (field < 4 && (!summary.exists || summary.used[field] == 0)) {
      record(2 + field, true);
      continue;
    }
    auto limits = defaults;
    limits.*members[field] = 0;
    assessment(provider, 2 + field, limits, summary,
               field == 4 ? header_error : std::string_view{}, field);
  }
  for (std::size_t field = 0; field < 5; ++field) {
    auto limits = defaults;
    limits.*members[field] += 1;
    assessment(provider, 7 + field, limits, summary, limits_error);
  }
  for (std::size_t field = 0; field < 5; ++field) {
    if (!summary.exists || (field < 4 && summary.used[field] <= 1)) {
      record(12 + field, true);
      continue;
    }
    auto limits = defaults;
    limits.*members[field] =
        (field < 4 ? summary.used[field] : summary.required) - 1;
    assessment(provider, 12 + field, limits, summary, {}, field);
  }
}
[[gnu::noinline]] void exact_control(const Provider& provider,
                                     const Summary& summary) {
  if (!summary.exists) {
    record(17, true);
    return;
  }
  Limits limits;
  for (std::size_t i = 0; i < 4; ++i)
    limits.*members[i] = summary.used[i];
  limits.output_bytes = summary.required;
  assessment(provider, 17, limits, summary, {}, 5, true);
}
[[gnu::noinline]] void environments(const Provider& provider,
                                    const Summary& summary) {
  const Limits limits;
  for (std::size_t mode = 0; mode < 6; ++mode) {
    Environment changed(mode);
    if (changed.available)
      assessment(provider, 18 + mode, limits, summary, fp_error);
    else
      record(18 + mode, true);
  }
}
[[gnu::noinline]] void lifetime_controls(Provider& provider,
                                         const Summary& summary) {
  const Limits limits;
  {
    auto alias = provider;
    {
      auto discarded = std::move(alias);
      check(Access::valid(discarded),
            "Invalid-source alias discarded as genuine handle");
    }
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Defined empty-handle test.
    assessment(alias, 24, limits, summary, {}, 5, false, true);
  }
  {
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Copy test.
    const auto alias = provider;
    assessment(alias, 25, limits, summary, {}, 5, summary.exists);
  }
  {
    auto alias = provider;
    auto moved = std::move(alias);
    assessment(moved, 26, limits, summary, {}, 5, summary.exists);
  }
  {
    auto survivor = provider;
    {
      auto discarded = std::move(provider);
      check(Access::valid(discarded),
            "Original owner enters scoped discarded lifetime");
    }
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Defined empty-handle test.
    check(!Access::valid(provider),
          "Original truly empty after discarded owner destruction");
    assessment(survivor, 27, limits, summary, {}, 5, summary.exists);
    provider = std::move(survivor);
  }
  assessment(provider, 28, limits, summary, {}, 5, summary.exists);
}
[[gnu::noinline]] void mixed(const Provider& provider, const Summary& summary) {
  const Limits defaults;
  if (summary.exists && summary.used[0] > 0) {
    auto limits = defaults;
    limits.source_guards = 0;
    limits.construction_guards = 0;
    assessment(provider, 29, limits, summary, {}, 0);
  } else
    record(29, true);
  if (summary.exists && summary.used[1] > 0) {
    auto limits = defaults;
    limits.construction_guards = 0;
    limits.construction_operations = 0;
    assessment(provider, 30, limits, summary, {}, 1);
  } else
    record(30, true);
}
[[gnu::noinline]] void final_counts() {
  check(totals.creators == 1 && totals.consumers + totals.skips == 30 &&
            next_slot == 31 &&
            (totals.attempted_slots | totals.skipped_slots) ==
                ((std::uint32_t{1} << 30) - 1) &&
            (totals.attempted_slots & totals.skipped_slots) == 0,
        "One creator and every literal slot executes or honestly skips");
  check(totals.work[0] == totals.consumers && totals.raw == 0 &&
            totals.inspectors == 0 && totals.bodies == 0,
        "Every actual API includes exactly one fixed FP-first check and no "
        "extra seam or body query");
  constexpr std::array<std::uint32_t, 11> maximum{30, 1920, 630, 2340, 240, 30,
                                                  30, 60,   0,   90,   180};
  for (std::size_t i = 0; i < maximum.size(); ++i)
    check(totals.work[i] <= maximum[i],
          "Finite aggregate maximum includes every selected attempt");
  std::cout << "SECTOR_EVIDENCE02_VALIDATION creator=" << totals.creators
            << " consumers=" << totals.consumers << " skips=" << totals.skips
            << " inspectors=0 raw=0 bodies=0 oracles=0 checks=" << checks
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
      isolated(*source, summary);
      exact_control(*source, summary);
      environments(*source, summary);
      lifetime_controls(*source, summary);
      mixed(*source, summary);
    } else
      for (std::size_t slot = 1; slot <= 30; ++slot)
        record(slot, true);
    final_counts();
    std::cout.flush();
    std::cerr.flush();
    check(bytes <= 16384 && !out.truncated && !err.truncated &&
              std::cout.good() && std::cerr.good(),
          "Shared stdout/stderr budget and stream states cannot silently "
          "truncate");
  } catch (const std::exception& e) {
    check(false, e.what());
  }
  std::cout.rdbuf(oldout);
  std::cerr.rdbuf(olderr);
  return failures == 0 ? 0 : 1;
}
