#include "apsis_drift/origin_boarding_knee_compatibility_diagnostic01.hpp"
#include "apsis_drift/origin_boarding_self_model02.hpp"
#include "origin_boarding_knee_compatibility_diagnostic01_internal.hpp"
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
using Diagnostic = BoardingKneeCompatibilityDiagnostic01Diagnostic;
using Expected = BoardingKneeCompatibilityDiagnostic01Expected;
using Limits = detail::BoardingKneeCompatibilityDiagnostic01Limits;
using Prefix = BoardingKneeCompatibilityDiagnostic01PrefixEvidence;
using Refusal = BoardingKneeCompatibilityDiagnostic01Refusal;
using State = BoardingKneeCompatibilityDiagnostic01State;
using Stop = BoardingKneeCompatibilityDiagnostic01Stop;
using PrefixState = BoardingKneeCompatibilityDiagnostic01PrefixState;
using Classification = BoardingKneeCompatibilityDiagnostic01Classification;
using Condition = BoardingKneeCompatibilityDiagnostic01Condition;
using SourceCondition = BoardingKneeCompatibilityDiagnostic01SourceCondition;
using Stage = BoardingKneeCompatibilityDiagnostic01Stage;
using Scalar = BoardingFootSiteScalarBounds;
using Request = BoardingRouteFootPhaseRequest;
using Constant = BoardingRoutePhaseConstant;
using Access = detail::BoardingIntermediatePauseSupportAccess;
using PublicAssessment = Expected (*)(const Provider&);
using BoundedAssessment = Expected (*)(const Provider&, Limits);
static_assert(std::is_same_v<
              decltype(&assess_origin_boarding_knee_compatibility_diagnostic01),
              PublicAssessment>);
static_assert(
    std::is_same_v<decltype(&detail::knee_compatibility_diagnostic01_bounded),
                   BoundedAssessment>);
static_assert(!std::is_default_constructible_v<Provider>);
static_assert(std::is_copy_constructible_v<Provider> &&
              std::is_move_constructible_v<Provider> &&
              std::is_copy_assignable_v<Provider> &&
              std::is_move_assignable_v<Provider>);
static_assert(std::is_constructible_v<Diagnostic, const Provider&> &&
              !std::is_convertible_v<const Provider&, Diagnostic>);
static_assert(sizeof(Expected) <= 1920 && sizeof(Provider) <= 16 &&
              sizeof(Limits) <= 32);
static_assert(
    boarding_knee_compatibility_diagnostic01_required_output_bytes() ==
    2 * sizeof(Expected));
static_assert(!Diagnostic::world_qualified && !Diagnostic::material_qualified &&
              !Diagnostic::halo_qualified && !Diagnostic::route_qualified &&
              !Diagnostic::seat_qualified && !Diagnostic::actor_qualified &&
              !Diagnostic::save_qualified && !Diagnostic::dynamics_qualified &&
              !Diagnostic::strength_qualified &&
              !Diagnostic::friction_qualified &&
              !Diagnostic::first_flight_qualified);
struct Summary {
  std::array<std::uint32_t, 3> used{};
  std::uint64_t digest{}, output{};
  std::uint16_t eligibility{};
  bool exists{};
};
struct Totals {
  std::array<std::uint32_t, 4> work{};
  std::uint16_t creators{}, consumers{}, skips{}, next_slot{1}, oracle_calls{},
      oracle_slacks{}, classifiers_checked{};
};
static_assert(sizeof(Summary) <= 120 && sizeof(Totals) <= 120);
// Source reservations: creator772/1024; caller1887/2048; mutable globals156.
Totals totals;
std::uint64_t checks{};
std::uint32_t failures{};
std::size_t shared_bytes{};
std::string_view first_failure{};
void check(bool yes, std::string_view why) {
  if (checks >= 65536 || (!yes && failures >= 65536))
    throw std::runtime_error("Test counter overflow");
  ++checks;
  if (!yes) {
    ++failures;
    if (first_failure.empty()) first_failure = why;
  }
}
void charge(std::uint16_t& n, std::uint32_t maximum) {
  if (maximum > std::numeric_limits<std::uint16_t>::max() || n >= maximum)
    throw std::runtime_error("Registered roster exhausted");
  ++n;
}
void aggregate(std::size_t index, std::uint64_t amount) {
  if (index >= totals.work.size() ||
      amount > std::numeric_limits<std::uint32_t>::max() - totals.work[index])
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
  explicit Environment(std::size_t mode) {
    saved_valid_ = std::fegetenv(&saved_) == 0;
    available = saved_valid_;
#if defined(__SSE2__) && defined(__x86_64__)
    control_ = _mm_getcsr();
#endif
    if (!available) return;
    if (mode < 3) {
      available = std::fesetround(mode == 0   ? FE_DOWNWARD
                                  : mode == 1 ? FE_UPWARD
                                              : FE_TOWARDZERO) == 0;
    } else {
#if defined(__SSE2__) && defined(__x86_64__)
      _mm_setcsr(mode == 3   ? control_ | (1U << 15)
                 : mode == 4 ? control_ | (1U << 6)
                             : (control_ & ~(3U << 13)) | (1U << 13));
#else
      available = false;
#endif
    }
  }
  ~Environment() {
    if (saved_valid_) std::fesetenv(&saved_);
#if defined(__SSE2__) && defined(__x86_64__)
    _mm_setcsr(control_);
#endif
  }
  Environment(const Environment&) = delete;
  auto operator=(const Environment&) -> Environment& = delete;
  bool available{};

 private:
  fenv_t saved_{};
  bool saved_valid_{};
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
auto finite(const Scalar& b) -> bool {
  return b.supported && std::isfinite(b.lower) && std::isfinite(b.upper) &&
         b.lower <= b.upper;
}
auto prefix_mask(std::uint64_t n) -> std::uint64_t {
  if (n == 0) return 0;
  if (n >= 64) return std::numeric_limits<std::uint64_t>::max();
  return (std::uint64_t{1} << n) - 1;
}
auto operation_word(std::uint64_t n, std::size_t word) -> std::uint64_t {
  return n <= 64 * word ? 0 : prefix_mask(n - 64 * word);
}
auto operation_written(const Prefix& p, std::size_t row) -> bool {
  return row >= 1 && row <= 255 &&
         (p.operation_written[(row - 1) / 64] &
          (std::uint64_t{1} << ((row - 1) % 64))) != 0;
}
auto guard_written(const Prefix& p, std::size_t row) -> bool {
  return row >= 1 && row <= 50 &&
         (p.guard_written & (std::uint64_t{1} << (row - 1))) != 0;
}
auto guard_attempted(const Prefix& p, std::size_t row) -> bool {
  return row >= 1 && row <= 50 &&
         (p.guard_attempted & (std::uint64_t{1} << (row - 1))) != 0;
}
auto collapsed(const Diagnostic& d) -> bool {
  return d.prefix.prefix_state == PrefixState::cuts_collapsed &&
         guard_attempted(d.prefix, 42) && !guard_written(d.prefix, 42) &&
         d.has_refusal && d.prefix.condition == Condition::no_tau_interval;
}
auto guards_through(const Diagnostic& d, std::size_t n, bool hole) -> bool {
  auto needed = prefix_mask(n);
  if (hole && n >= 42 && collapsed(d)) needed &= ~(std::uint64_t{1} << 41);
  return (d.prefix.guard_written & needed) == needed;
}
auto operations_through(const Prefix& p, std::size_t n) -> bool {
  for (std::size_t word = 0; word < 4; ++word)
    if ((p.operation_written[word] & operation_word(n, word)) !=
        operation_word(n, word))
      return false;
  return true;
}
auto source_complete(const Diagnostic& d) -> bool {
  return d.source_enrolled && d.work.source_guards == 64 &&
         d.source_evaluated == std::numeric_limits<std::uint64_t>::max();
}
void interleave(const Diagnostic& d,
                std::initializer_list<std::pair<unsigned, unsigned>> rows) {
  for (const auto& [guard, operation] : rows) {
    if (guard_attempted(d.prefix, guard))
      check(operation == 0 || operation_written(d.prefix, operation),
            "Reached guard requires its preceding literal arithmetic");
    if (d.work.construction_operations > operation)
      check(guard_written(d.prefix, guard) || (guard == 42 && collapsed(d)),
            "Later arithmetic requires its actual guard except localized G42 "
            "collapse");
  }
}
auto expected_weight(std::size_t i) -> std::uint32_t {
  switch (i) {
    case 0: return 144;
    case 1: return 540;
    case 2: return 96;
    case 3:
    case 9: return 120;
    case 4:
    case 10: return 48;
    case 5:
    case 11: return 12;
    case 6:
    case 12: return 15;
    case 7:
    case 13: return 10;
    default: return 5;
  }
}
void exact_box(const BoardingRoutePhasePartBinding& part,
               BoardingPlantedBodyPointId center, BoardingRoutePhaseFrame frame,
               double x, double y, double z) {
  const auto* b = std::get_if<BoardingRoutePhaseBoxBinding>(&part.reservation);
  check(b && b->center == center && b->frame == frame &&
            b->half_size_metres.x == x && b->half_size_metres.y == y &&
            b->half_size_metres.z == z && part.mass.first == center &&
            part.mass.second == center,
        "Exact authentic box/frame/center/mass tuple");
}
void exact_capsule(const BoardingRoutePhasePartBinding& part,
                   BoardingPlantedBodyPointId first,
                   BoardingPlantedBodyPointId last, double radius) {
  const auto* c =
      std::get_if<BoardingPlantedBodyCapsuleBinding>(&part.reservation);
  check(c && c->start == first && c->end == last &&
            c->radius_metres == radius && part.mass.first == first &&
            part.mass.second == last,
        "Exact authentic capsule/link/radius/mass tuple");
}
void source_identity(const Diagnostic& d, const Provider& p) {
  check(Access::data(d.source) == Access::data(p),
        "Current report owns the exact borrowed provider Data");
  if (!source_complete(d)) return;
  check(Access::valid(d.source) && d.source.summary() &&
            d.source.summary()->complete,
        "Complete enrollment keeps the authentic complete source");
  for (std::size_t side = 0; side < 2; ++side) {
    const auto* partition = Access::partition(d.source, side);
    const auto* original = Access::partition(p, side);
    check(partition && partition == original,
          "Both source partitions belong to the current owning Data");
    if (!partition) continue;
    check(partition->plane_metres ==
              double(side == 0 ? -230000 : -160000) * 1e-6,
          "Original binary64 sole plane products are preserved");
    check(partition->faces[0].source_object ==
                  partition->faces[1].source_object &&
              partition->faces[0].key != partition->faces[1].key,
          "Original named two distinct source faces survive enrollment");
  }
  using P = BoardingPlantedBodyPointId;
  for (std::size_t i = 0; i < d.parts.size(); ++i)
    check(unsigned(d.parts[i].id) == i &&
              d.parts[i].mass.weight == expected_weight(i),
          "All fifteen authenticated ordered source part IDs and masses");
  exact_box(d.parts[0], P::root, BoardingRoutePhaseFrame::root, .24, .12, .18);
  exact_box(d.parts[1], P::trunk_center, BoardingRoutePhaseFrame::trunk, .26,
            .2695, .18);
  exact_box(d.parts[2], P::helmet_center, BoardingRoutePhaseFrame::trunk, .16,
            .18, .18);
  exact_capsule(d.parts[3], P::port_hip, P::port_knee, .105);
  exact_capsule(d.parts[4], P::port_knee, P::port_ankle, .075);
  exact_box(d.parts[5], P::port_boot_center, BoardingRoutePhaseFrame::port_sole,
            .06, .05, .14);
  exact_capsule(d.parts[6], P::port_shoulder, P::port_elbow, .065);
  exact_capsule(d.parts[7], P::port_elbow, P::port_wrist, .055);
  exact_box(d.parts[8], P::port_wrist, BoardingRoutePhaseFrame::trunk, .04, .05,
            .02);
  exact_capsule(d.parts[9], P::starboard_hip, P::starboard_knee, .105);
  exact_capsule(d.parts[10], P::starboard_knee, P::starboard_ankle, .075);
  exact_box(d.parts[11], P::starboard_boot_center,
            BoardingRoutePhaseFrame::starboard_sole, .06, .05, .14);
  exact_capsule(d.parts[12], P::starboard_shoulder, P::starboard_elbow, .065);
  exact_capsule(d.parts[13], P::starboard_elbow, P::starboard_wrist, .055);
  exact_box(d.parts[14], P::starboard_wrist, BoardingRoutePhaseFrame::trunk,
            .04, .05, .02);
  const auto* pelvis =
      std::get_if<BoardingRoutePhaseBoxBinding>(&d.parts[0].reservation);
  check(pelvis && pelvis->center == P::root &&
            pelvis->frame == BoardingRoutePhaseFrame::root &&
            pelvis->half_size_metres.x == .24 &&
            pelvis->half_size_metres.y == .12 &&
            pelvis->half_size_metres.z == .18,
        "Authentic pelvis/root-frame threshold tuple");
  for (std::size_t side = 0; side < 2; ++side) {
    const auto* thigh = std::get_if<BoardingPlantedBodyCapsuleBinding>(
        &d.parts[side == 0 ? 3 : 9].reservation);
    const auto* shin = std::get_if<BoardingPlantedBodyCapsuleBinding>(
        &d.parts[side == 0 ? 4 : 10].reservation);
    const auto* boot = std::get_if<BoardingRoutePhaseBoxBinding>(
        &d.parts[side == 0 ? 5 : 11].reservation);
    check(thigh &&
              thigh->start == (side == 0 ? P::port_hip : P::starboard_hip) &&
              thigh->end == (side == 0 ? P::port_knee : P::starboard_knee) &&
              thigh->radius_metres == .105,
          "Authentic BOTH thigh capsule threshold operands");
    check(shin &&
              shin->start == (side == 0 ? P::port_knee : P::starboard_knee) &&
              shin->end == (side == 0 ? P::port_ankle : P::starboard_ankle) &&
              shin->radius_metres == .075,
          "Authentic BOTH shin link tuple");
    check(boot &&
              boot->frame == (side == 0
                                  ? BoardingRoutePhaseFrame::port_sole
                                  : BoardingRoutePhaseFrame::starboard_sole) &&
              boot->half_size_metres.x == .06 &&
              boot->half_size_metres.y == .05 &&
              boot->half_size_metres.z == .14,
          "Authentic flat-sole frame and original boot dimensions");
  }
}
auto scalar_value(const Prefix& p, std::size_t bit) -> double {
  switch (bit) {
    case 0: return p.base_lo;
    case 1: return p.base_hi;
    case 2: return p.factor_lower[0];
    case 3: return p.factor_lower[1];
    case 4: return p.current_cap_PORT;
    case 5: return p.p_PORT;
    default: return p.q_PORT;
  }
}
auto scalar_row(std::size_t bit) -> std::size_t {
  switch (bit) {
    case 0: return 154;
    case 1: return 157;
    case 2: return 183;
    case 3: return 200;
    case 4: return 190;
    case 5: return 219;
    default: return 221;
  }
}
auto slack_row(std::size_t group, std::size_t index) -> std::size_t {
  switch (group) {
    case 0: return index == 0 ? 228 : index == 1 ? 230 : 233;
    case 1: return index == 0 ? 234 : index == 1 ? 236 : 239;
    case 2: return index == 0 ? 240 : index == 1 ? 242 : index == 2 ? 245 : 247;
    default:
      return index == 0 ? 248 : index == 1 ? 250 : index == 2 ? 253 : 255;
  }
}
[[gnu::noinline]] auto slack(const Diagnostic& d, std::size_t group,
                             std::size_t index) -> const Scalar& {
  switch (group) {
    case 0: return d.optimistic.scalar_current[index];
    case 1: return d.optimistic.scalar_best[index];
    case 2: return d.optimistic.common_current[index];
    default: return d.optimistic.common_best[index];
  }
}
auto classify(const Diagnostic& d, std::size_t group) -> Classification {
  bool positive = true;
  for (std::size_t index = 0; index < (group < 2 ? 3U : 4U); ++index) {
    const auto& b = slack(d, group, index);
    if (!operation_written(d.prefix, slack_row(group, index)) || !finite(b))
      return Classification::not_run;
    if (b.upper <= 0) return Classification::excluded;
    positive = positive && b.lower > 0;
  }
  return positive ? Classification::necessary_compatible
                  : Classification::inconclusive;
}
void ledger_checks(const Diagnostic& d, const Limits& l) {
  check(d.version == 1 && unsigned(d.state) <= 7 &&
            unsigned(d.stop_condition) <= 11 && d.prefix.preflight_guards == 1,
        "Known fresh purpose states and FP-FIRST preflight");
  check(d.output_capacity_bytes ==
            boarding_knee_compatibility_diagnostic01_required_output_bytes(),
        "Required output is exactly TWO actual Expected owners");
  check(d.work.source_guards <= l.source_guards && d.work.source_guards <= 64 &&
            d.source_evaluated == prefix_mask(d.work.source_guards),
        "Source64 charge and evaluated prefix are exact");
  check(d.work.construction_operations <= l.construction_operations &&
            d.work.construction_operations <= 255 &&
            d.work.construction_guards <= l.construction_guards &&
            d.work.construction_guards <= 50,
        "Fresh arithmetic and guard charges obey actual limits");
  for (std::size_t word = 0; word < 4; ++word)
    check(d.prefix.operation_attempted[word] ==
                  operation_word(d.work.construction_operations, word) &&
              (d.prefix.operation_written[word] &
               ~d.prefix.operation_attempted[word]) == 0,
          "Four exact attempted words with only earned write subsets");
  check(d.prefix.guard_attempted == prefix_mask(d.work.construction_guards) &&
            (d.prefix.guard_written & ~d.prefix.guard_attempted) == 0 &&
            (d.prefix.guard_written & ~std::uint64_t{0x3ffffffffffff}) == 0,
        "Single exact50 guard word and unused high bits");
  check((d.prefix.operation == 65535 || d.prefix.operation < 255) &&
            (d.prefix.guard == 255 || d.prefix.guard < 50) &&
            (d.prefix.side == 255 || d.prefix.side < 2) &&
            (d.prefix.scalar_ready & ~std::uint16_t{0x7f}) == 0 &&
            (d.prefix.zero_mask & ~std::uint16_t{0xff}) == 0,
        "Typed unset cursors and bounded scalar/zero flags");
}
void interleave_checks(const Diagnostic& d) {
  interleave(d, {{1, 0},
                 {2, 0},
                 {3, 0},
                 {4, 0},
                 {5, 0},
                 {6, 0},
                 {7, 45},
                 {8, 54},
                 {9, 56},
                 {10, 62},
                 {11, 67},
                 {12, 73},
                 {13, 78},
                 {14, 85},
                 {15, 92}});
  interleave(d, {{16, 106},
                 {17, 108},
                 {18, 114},
                 {19, 119},
                 {20, 125},
                 {21, 130},
                 {22, 137},
                 {23, 144},
                 {24, 157},
                 {25, 157},
                 {26, 161},
                 {27, 166},
                 {28, 171},
                 {29, 173},
                 {30, 175}});
  interleave(d, {{31, 179},
                 {32, 183},
                 {33, 185},
                 {34, 187},
                 {35, 190},
                 {36, 192},
                 {37, 196},
                 {38, 200},
                 {39, 202},
                 {40, 204},
                 {41, 207},
                 {42, 221},
                 {43, 223},
                 {44, 225},
                 {45, 227}});
  interleave(d, {{46, 233}, {47, 239}, {48, 247}, {49, 255}, {50, 255}});
  for (const auto& [bit, row] : {std::pair{0U, 89U},
                                 {1U, 141U},
                                 {2U, 63U},
                                 {3U, 74U},
                                 {4U, 115U},
                                 {5U, 126U},
                                 {6U, 81U},
                                 {7U, 133U}})
    if (d.prefix.zero_mask & (1U << bit))
      check(operation_written(d.prefix, row),
            "Each original zero choice has its own genuine arithmetic write");
}
void evidence_checks(const Diagnostic& d) {
  for (std::size_t bit = 0; bit < 7; ++bit) {
    check(bool(d.prefix.scalar_ready & (1U << bit)) ==
              operation_written(d.prefix, scalar_row(bit)),
          "Scalar-ready tags coincide with direct owned publication rows");
    if (d.prefix.scalar_ready & (1U << bit))
      check(std::isfinite(scalar_value(d.prefix, bit)),
            "Only earned scalar publications are finite observations");
  }
  if (guard_written(d.prefix, 5))
    check(d.prefix.L1 == .47285 && d.prefix.L2 == .47478 && d.prefix.L1 > 0 &&
              d.prefix.L2 > 0,
          "G05 directly copies exact positive original lengths");
  if (operation_written(d.prefix, 36))
    check(finite(d.prefix.Lsum), "Written Lsum interval is supported finite");
  if (operation_written(d.prefix, 171))
    check(finite(d.prefix.threshold),
          "Written original threshold interval is supported finite");
  if (operation_written(d.prefix, 208))
    check(finite(d.prefix.W_PORT),
          "Written W keeps signed original chart enclosure");
  if (guard_written(d.prefix, 24))
    check(d.prefix.base_lo < d.prefix.base_hi &&
              (d.prefix.scalar_ready & 3U) == 3U,
          "BASE inward certification follows both owned publications");
  for (std::size_t side = 0; side < 2; ++side)
    if (guard_written(d.prefix, side == 0 ? 32 : 38))
      check(guard_written(d.prefix, 24) && d.prefix.factor_lower[side] > 0 &&
                d.prefix.factor_lower[side] <= 1,
            "Certified same-BASE factors retain exact strict domains");
  if (guard_written(d.prefix, 35))
    check(d.prefix.current_cap_PORT > 0 &&
              d.prefix.current_cap_PORT < d.prefix.L1,
          "Current cap follows original positive inward guard");
  if (d.prefix.prefix_state == PrefixState::cuts_available)
    check(guard_written(d.prefix, 42) && d.prefix.p_PORT < d.prefix.q_PORT &&
              d.prefix.q_PORT <= .5,
          "Stored successful G42 uses exact strict order without tolerance");
  if (d.prefix.prefix_state == PrefixState::cuts_collapsed)
    check(
        collapsed(d) && guards_through(d, 41, false) &&
            operations_through(d.prefix, 221) &&
            d.prefix.p_PORT >= d.prefix.q_PORT && d.prefix.q_PORT <= .5,
        "Localized supported collapse preserves actual G42 hole and exact p/q");
  check((d.optimistic.classifier_ready & 0xf0U) == 0,
        "Only four distinct classifier readiness bits exist");
  for (std::size_t group = 0; group < 4; ++group) {
    const bool ready = (d.optimistic.classifier_ready & (1U << group)) != 0;
    check(ready == guard_written(d.prefix, 46 + group),
          "Each classifier certification is its own guard write");
    for (std::size_t index = 0; index < (group < 2 ? 3U : 4U); ++index)
      if (operation_written(d.prefix, slack_row(group, index)))
        check(finite(slack(d, group, index)),
              "Each written signed slack interval is supported");
    if (ready) {
      charge(totals.classifiers_checked, 144);
      check(d.optimistic.classification[group] == classify(d, group),
            "Ascending first upper<=0 exclusion otherwise all lower>0 uses no "
            "tolerance");
    } else
      check(d.optimistic.classification[group] == Classification::not_run,
            "Uncertified classifier is explicitly NOT_RUN");
  }
  if (operation_written(d.prefix, 225))
    check(
        finite(d.optimistic.best_cap),
        "Whole ideal best-cap publication remains finite unclipped enclosure");
  if (d.complete || d.optimistic.complete)
    check(d.complete && d.optimistic.complete &&
              d.state == State::evidence_complete &&
              d.stop_condition == Stop::none &&
              d.work.construction_operations == 255 &&
              d.work.construction_guards == 50 && source_complete(d) &&
              operations_through(d.prefix, 255) &&
              guards_through(d, 50, true) &&
              d.optimistic.classifier_ready == 15,
          "G50 complete retains every literal operation and all distinct "
          "classifiers");
  if (d.has_refusal) {
    const auto& r = d.first_refusal;
    check((!r.limiting_bound.supported || finite(r.limiting_bound)) &&
              (!r.side || *r.side < 2) && (!r.edge || *r.edge < 4) &&
              (!r.axis || *r.axis < 3),
          "Supported first-refusal bounds and original optional cursors remain "
          "typed");
    check(unsigned(r.self_stage) <= 20 &&
              (r.operation == 65535 || r.operation < 255) &&
              (r.self_pair == 65535 || r.self_pair < 105) &&
              (r.self_region == 255 || r.self_region < 14) &&
              (r.self_axis == 255 || r.self_axis < 14) &&
              (r.self_sign == 255 || r.self_sign < 2),
          "Full original refusal metadata retains typed source/stage cursors");
    if (collapsed(d))
      check(
          r.condition == SourceCondition::slice_unavailable &&
              r.operation == 41 && r.self_stage == Stage::slice_guard &&
              r.limiting_bound.supported &&
              r.limiting_bound.lower == d.prefix.q_PORT &&
              r.limiting_bound.upper == d.prefix.q_PORT,
          "First authentic G42 finding is retained through later suffix stops");
  }
  check(!d.prefix.limiting_bound.supported || finite(d.prefix.limiting_bound),
        "Only supported finite current limiting metadata is readable");
  if (d.stop_condition == Stop::source_refusal)
    check(d.state == State::source_refused && d.has_refusal &&
              !d.source_enrolled && d.work.construction_operations == 0,
          "Ordinary original source refusal uses separate fresh stop11/state1");
  aggregate(0, d.work.source_guards);
  aggregate(1, d.work.construction_operations);
  aggregate(2, d.work.construction_guards);
}
[[gnu::always_inline]] inline void accounting(const Diagnostic& d,
                                              const Provider& p,
                                              const Limits& l) {
  source_identity(d, p);
  ledger_checks(d, l);
  interleave_checks(d);
  evidence_checks(d);
}
struct Hash {
  std::uint64_t value{14695981039346656037ULL};
  void add(std::uint64_t n) { value = (value ^ n) * 1099511628211ULL; }
  void number(double n) { add(std::bit_cast<std::uint64_t>(n)); }
  void bound(const Scalar& b) {
    add(b.supported);
    if (b.supported) {
      number(b.lower);
      number(b.upper);
    }
  }
  void text(std::string_view s) {
    add(s.size());
    for (unsigned char c : s)
      add(c);
  }
};
void hash_optional(Hash& h, const std::optional<std::size_t>& v) {
  h.add(v.has_value());
  if (v) h.add(*v);
}
void hash_refusal(Hash& h, const Refusal& r) {
  h.add(unsigned(r.condition));
  h.add(unsigned(r.predicate_condition));
  h.add(unsigned(r.self_stage));
  h.add(r.operation);
  h.add(r.self_pair);
  h.add(r.self_region);
  h.add(r.self_axis);
  h.add(r.self_sign);
  hash_optional(h, r.side);
  hash_optional(h, r.edge);
  hash_optional(h, r.axis);
  h.add(r.source_key.has_value());
  if (r.source_key) {
    h.add(unsigned(r.source_key->buffer));
    h.add(r.source_key->group);
    h.add(r.source_key->triangle);
  }
  h.text(r.source_name);
  h.add(r.source_edge);
  h.bound(r.limiting_bound);
  h.add(unsigned(r.phase.condition));
  h.add(unsigned(r.phase.predicate_condition));
  hash_optional(h, r.phase.side);
  h.add(r.phase.condition != BoardingRouteFootPhaseCondition::none);
  if (r.phase.condition != BoardingRouteFootPhaseCondition::none) {
    h.number(r.phase.first);
    h.number(r.phase.last);
    h.add(r.phase.depth);
    h.number(r.phase.limiting_bound.lower);
    h.number(r.phase.limiting_bound.upper);
  }
}
[[gnu::noinline]] auto semantic_hash(const Diagnostic& d) -> std::uint64_t {
  Hash h;
  h.add(d.version);
  h.add(unsigned(d.state));
  h.add(unsigned(d.stop_condition));
  h.add(d.has_refusal);
  if (d.has_refusal) hash_refusal(h, d.first_refusal);
  h.add(d.source_enrolled);
  h.add(d.source_evaluated);
  h.add(d.work.source_guards);
  h.add(d.work.construction_operations);
  h.add(d.work.construction_guards);
  h.add(d.output_capacity_bytes);
  h.add(d.arithmetic_supported);
  h.add(d.complete);
  h.add(source_complete(d));
  if (source_complete(d)) {
    for (std::size_t side = 0; side < 2; ++side)
      if (const auto* q = Access::partition(d.source, side)) {
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
    for (const auto& part : d.parts) {
      h.add(unsigned(part.id));
      h.add(unsigned(part.mass.first));
      h.add(unsigned(part.mass.second));
      h.add(part.mass.weight);
      h.add(part.reservation.index());
      if (const auto* b =
              std::get_if<BoardingRoutePhaseBoxBinding>(&part.reservation)) {
        h.add(unsigned(b->center));
        h.add(unsigned(b->frame));
        h.number(b->half_size_metres.x);
        h.number(b->half_size_metres.y);
        h.number(b->half_size_metres.z);
      } else if (const auto* c = std::get_if<BoardingPlantedBodyCapsuleBinding>(
                     &part.reservation)) {
        h.add(unsigned(c->start));
        h.add(unsigned(c->end));
        h.number(c->radius_metres);
      }
    }
  }
  const auto& p = d.prefix;
  h.add(unsigned(p.prefix_state));
  h.add(unsigned(p.condition));
  h.add(p.operation);
  h.add(p.guard);
  h.add(p.side);
  h.add(p.preflight_guards);
  h.add(p.arithmetic_supported);
  h.add(p.scalar_ready);
  h.add(p.zero_mask);
  h.bound(p.limiting_bound);
  for (std::size_t word = 0; word < 4; ++word) {
    h.add(p.operation_attempted[word]);
    h.add(p.operation_written[word]);
  }
  h.add(p.guard_attempted);
  h.add(p.guard_written);
  for (std::size_t bit = 0; bit < 7; ++bit) {
    h.add(bool(p.scalar_ready & (1U << bit)));
    if (p.scalar_ready & (1U << bit)) h.number(scalar_value(p, bit));
  }
  h.add(guard_written(p, 5));
  if (guard_written(p, 5)) {
    h.number(p.L1);
    h.number(p.L2);
  }
  h.add(operation_written(p, 36));
  if (operation_written(p, 36)) h.bound(p.Lsum);
  h.add(operation_written(p, 171));
  if (operation_written(p, 171)) h.bound(p.threshold);
  h.add(operation_written(p, 208));
  if (operation_written(p, 208)) h.bound(p.W_PORT);
  h.add(operation_written(p, 225));
  if (operation_written(p, 225)) h.bound(d.optimistic.best_cap);
  h.add(d.optimistic.classifier_ready);
  h.add(unsigned(d.optimistic.domain_condition));
  h.add(d.optimistic.complete);
  for (std::size_t group = 0; group < 4; ++group) {
    h.add(bool(d.optimistic.classifier_ready & (1U << group)));
    if (d.optimistic.classifier_ready & (1U << group))
      h.add(unsigned(d.optimistic.classification[group]));
    for (std::size_t index = 0; index < (group < 2 ? 3U : 4U); ++index) {
      h.add(operation_written(p, slack_row(group, index)));
      if (operation_written(p, slack_row(group, index)))
        h.bound(slack(d, group, index));
    }
  }
  return h.value;
}
auto crossing_cap(std::size_t index) -> std::size_t {
  switch (index) {
    case 0: return 64;
    case 1: return 65;
    case 2: return 128;
    case 3: return 129;
    case 4: return 192;
    default: return 193;
  }
}
auto crossing_guards(std::size_t index) -> std::size_t {
  return index < 2 ? 10 : index < 4 ? 20 : 36;
}
[[gnu::noinline]] void capture(const Diagnostic& d, Summary& s) {
  s.used[0] = static_cast<std::uint32_t>(d.work.source_guards);
  s.used[1] = static_cast<std::uint32_t>(d.work.construction_operations);
  s.used[2] = static_cast<std::uint32_t>(d.work.construction_guards);
  s.output = d.output_capacity_bytes;
  s.digest = semantic_hash(d);
  s.exists = true;
  if (!source_complete(d)) return;
  for (std::size_t index = 0; index < 6; ++index)
    if (d.work.construction_operations > crossing_cap(index) &&
        operations_through(d.prefix, crossing_cap(index)) &&
        guards_through(d, crossing_guards(index), false))
      s.eligibility |= static_cast<std::uint16_t>(1U << index);
  if (d.work.construction_guards > 41 && operations_through(d.prefix, 221) &&
      guards_through(d, 41, false))
    s.eligibility |= 1U << 6;
  if (d.work.construction_operations > 221 &&
      operations_through(d.prefix, 221) && guards_through(d, 42, true) &&
      guard_attempted(d.prefix, 42))
    s.eligibility |= 1U << 7;
  if (d.work.construction_operations > 254 &&
      operations_through(d.prefix, 254) && guards_through(d, 48, true))
    s.eligibility |= 1U << 8;
  if (d.work.construction_guards > 49 && operations_through(d.prefix, 255) &&
      guards_through(d, 49, true))
    s.eligibility |= 1U << 9;
}
// Independent long-double corroboration uses ONE fixed workspace. It supplies
// neither production enclosure authority nor a selected root-Y/body posture.
struct LdBounds {
  long double low{}, high{};
};
struct OracleWorkspace {
  std::array<LdBounds, 24> bounds{};
  std::array<long double, 64> scalars{};
};
static_assert(sizeof(LdBounds) <= 32 && sizeof(OracleWorkspace) <= 1792);
static_assert(std::numeric_limits<long double>::digits >=
              std::numeric_limits<double>::digits);
// Full helper pool3072/4096 remains charged: workspace1792, primitive current/
// pending192, full-expression320, reference/control256, numeric library512.
enum class Outcome : std::uint8_t {
  not_run,
  corroborated,
  inconclusive,
  failed
};
auto ready(const LdBounds& a) -> bool {
  return std::isfinite(a.low) && std::isfinite(a.high) && a.low <= a.high;
}
auto unavailable(LdBounds& out) -> bool {
  out.low = out.high = std::numeric_limits<long double>::quiet_NaN();
  return false;
}
auto lift(double a, LdBounds& out) -> bool {
  if (!std::isfinite(a)) return unavailable(out);
  out.low = out.high = static_cast<long double>(a);
  return true;
}
auto lower(long double a) -> long double {
  return std::nextafterl(a, -std::numeric_limits<long double>::infinity());
}
auto upper(long double a) -> long double {
  return std::nextafterl(a, std::numeric_limits<long double>::infinity());
}
[[gnu::noinline]] auto add(const LdBounds& a, const LdBounds& b, LdBounds& out)
    -> bool {
  if (!ready(a) || !ready(b)) return unavailable(out);
  const auto lo = a.low + b.low, hi = a.high + b.high;
  if (!std::isfinite(lo) || !std::isfinite(hi)) return unavailable(out);
  out.low = lower(lo);
  out.high = upper(hi);
  return ready(out);
}
auto negate(const LdBounds& a, LdBounds& out) -> bool {
  if (!ready(a)) return unavailable(out);
  const auto lo = -a.high, hi = -a.low;
  out.low = lower(lo);
  out.high = upper(hi);
  return ready(out);
}
[[gnu::noinline]] auto subtract(const LdBounds& a, const LdBounds& b,
                                LdBounds& out) -> bool {
  if (!ready(a) || !ready(b)) return unavailable(out);
  const auto lo = a.low - b.high, hi = a.high - b.low;
  if (!std::isfinite(lo) || !std::isfinite(hi)) return unavailable(out);
  out.low = lower(lo);
  out.high = upper(hi);
  return ready(out);
}
auto multiply(const LdBounds& a, const LdBounds& b, LdBounds& out) -> bool {
  if (!ready(a) || !ready(b)) return unavailable(out);
  if ((a.low == 0 && a.high == 0) || (b.low == 0 && b.high == 0)) {
    out.low = out.high = 0;
    return true;
  }
  // Four scalar products, not a hidden owning coefficient array. All operand
  // reads finish before the borrowed destination may overwrite either input.
  const auto p0 = a.low * b.low, p1 = a.low * b.high, p2 = a.high * b.low,
             p3 = a.high * b.high;
  if (!std::isfinite(p0) || !std::isfinite(p1) || !std::isfinite(p2) ||
      !std::isfinite(p3))
    return unavailable(out);
  out.low = lower(std::min(std::min(p0, p1), std::min(p2, p3)));
  out.high = upper(std::max(std::max(p0, p1), std::max(p2, p3)));
  return ready(out);
}
auto square(const LdBounds& a, LdBounds& out) -> bool {
  if (!ready(a)) return unavailable(out);
  const auto lo = a.low >= 0    ? a.low * a.low
                  : a.high <= 0 ? a.high * a.high
                                : 0;
  const auto hi = std::max(a.low * a.low, a.high * a.high);
  if (!std::isfinite(lo) || !std::isfinite(hi)) return unavailable(out);
  if (a.low == 0 && a.high == 0) {
    out.low = out.high = 0;
    return true;
  }
  out.low = lo == 0 ? 0 : lower(lo);
  out.high = upper(hi);
  return ready(out);
}
auto absolute(const LdBounds& a, LdBounds& out) -> bool {
  if (!ready(a)) return unavailable(out);
  const auto lo = a.low >= 0 ? a.low : a.high <= 0 ? -a.high : 0;
  const auto hi = std::max(std::abs(a.low), std::abs(a.high));
  if (hi == 0) {
    out.low = out.high = 0;
    return true;
  }
  out.low = lo == 0 ? 0 : lower(lo);
  out.high = upper(hi);
  return ready(out);
}
auto divide(const LdBounds& a, const LdBounds& b, LdBounds& out) -> bool {
  if (!ready(a) || !ready(b) || (b.low <= 0 && b.high >= 0))
    return unavailable(out);
  LdBounds reciprocal{lower(1 / b.high), upper(1 / b.low)};
  return multiply(a, reciprocal, out);
}
auto root(const LdBounds& a, LdBounds& out) -> bool {
  if (!ready(a) || a.low < 0) return unavailable(out);
  const auto lo = std::sqrt(a.low), hi = std::sqrt(a.high);
  if (!std::isfinite(lo) || !std::isfinite(hi)) return unavailable(out);
  out.low = lo == 0 ? 0 : lower(lo);
  out.high = hi == 0 ? 0 : upper(hi);
  return ready(out);
}
auto extreme(const LdBounds& a, const LdBounds& b, bool maximum, LdBounds& out)
    -> bool {
  if (!ready(a) || !ready(b)) return unavailable(out);
  const auto lo = maximum ? std::max(a.low, b.low) : std::min(a.low, b.low);
  const auto hi = maximum ? std::max(a.high, b.high) : std::min(a.high, b.high);
  out.low = lo;
  out.high = hi;
  return ready(out);
}
auto constant_into(const Constant& c, LdBounds& out) -> bool {
  if (c.count == 0 || c.count > c.terms.size()) return unavailable(out);
  LdBounds term;
  if (!lift(c.terms[0], out)) return false;
  for (std::size_t index = 1; index < c.count; ++index)
    if (!lift(c.terms[index], term) || !add(out, term, out)) return false;
  for (std::size_t index = c.count; index < c.terms.size(); ++index)
    check(c.terms[index] == 0,
          "Independent source constant retains zero unused terms");
  return ready(out);
}
auto yaw(double half, LdBounds& c, LdBounds& negative_s) -> bool {
  if (half == 0) return lift(1, c) && lift(0, negative_s);
  std::array<LdBounds, 4> t{}; // 128 of the disjoint primitive extension192.
  return lift(half, t[0]) && square(t[0], t[1]) && lift(1, t[2]) &&
         add(t[2], t[1], t[3]) && subtract(t[2], t[1], c) &&
         divide(c, t[3], c) && lift(-2, t[2]) &&
         multiply(t[2], t[0], negative_s) &&
         divide(negative_s, t[3], negative_s);
}
[[gnu::noinline]] void horizontal_request_into(Request& r) {
  const auto assign = [](Constant& c, std::initializer_list<double> terms) {
    c.count = static_cast<std::uint8_t>(terms.size());
    std::copy(terms.begin(), terms.end(), c.terms.begin());
  };
  for (std::size_t endpoint = 0; endpoint < 2; ++endpoint) {
    assign(r.root[endpoint].coordinates[0], {.16, .16, -.0075});
    // The unused root-Y carrier is left at its default; no Y is selected/read.
    assign(r.root[endpoint].coordinates[2], {-.55, -.17, .19});
    assign(r.feet[0].sole[endpoint].coordinates[0], {.16, -.14, .16});
    assign(r.feet[0].sole[endpoint].coordinates[1], {double(-230000) * 1e-6});
    assign(r.feet[0].sole[endpoint].coordinates[2], {-1.14});
    assign(r.feet[1].sole[endpoint].coordinates[0], {.16, .14});
    assign(r.feet[1].sole[endpoint].coordinates[1], {double(-160000) * 1e-6});
    assign(r.feet[1].sole[endpoint].coordinates[2], {-.8});
  }
  r.root_yaw_half = {.125, .125};
  r.torso_lean_half = {-.30, -.30};
  r.port_reaction_fraction = {.0625, .0625};
  r.seconds_per_parameter = 2;
}
auto same_side(const Request& r, std::size_t side, OracleWorkspace& w) -> bool {
  // Proper rational root columns X=(c,0,-s), Z=(s,0,c). Identity
  // sole frames make local x,z the horizontal world components. Each genuine
  // side has a distinct hip offset/origin; no root-Y term enters this chart.
  if (!(yaw(r.root_yaw_half[0], w.bounds[18], w.bounds[19]) &&
        lift(side == 0 ? -.14 : .14, w.bounds[20]) &&
        multiply(w.bounds[18], w.bounds[20], w.bounds[21]) &&
        constant_into(r.root[0].coordinates[0], w.bounds[0]) &&
        add(w.bounds[0], w.bounds[21], w.bounds[0]) &&
        constant_into(r.feet[side].sole[0].coordinates[0], w.bounds[21]) &&
        subtract(w.bounds[21], w.bounds[0], w.bounds[0]) &&
        multiply(w.bounds[19], w.bounds[20], w.bounds[21]) &&
        constant_into(r.root[0].coordinates[2], w.bounds[1]) &&
        add(w.bounds[1], w.bounds[21], w.bounds[1]) &&
        constant_into(r.feet[side].sole[0].coordinates[2], w.bounds[21]) &&
        subtract(w.bounds[21], w.bounds[1], w.bounds[1]) &&
        negate(w.bounds[1], w.bounds[1]) &&
        constant_into(r.feet[side].sole[0].coordinates[1], w.bounds[2]) &&
        lift(.1, w.bounds[20]) && add(w.bounds[2], w.bounds[20], w.bounds[2]) &&
        yaw(r.feet[side].yaw_half[0], w.bounds[20], w.bounds[21])))
    return false;
  check(r.root_yaw_half[0] == .125 && r.root_yaw_half[1] == .125 &&
            r.feet[side].yaw_half[0] == 0 && r.feet[side].yaw_half[1] == 0 &&
            w.bounds[20].low == 1 && w.bounds[20].high == 1 &&
            w.bounds[21].low == 0 && w.bounds[21].high == 0,
        "Independent authentic fixed horizontal chart and proper identity sole "
        "frames");
  return ready(w.bounds[0]) && ready(w.bounds[1]) && ready(w.bounds[2]);
}
auto nominal_cuts(OracleWorkspace& w) -> bool {
  return subtract(w.bounds[1], w.bounds[13], w.bounds[18]) &&
         divide(w.bounds[18], w.bounds[14], w.bounds[3]) &&
         lift(-.5, w.bounds[18]) &&
         extreme(w.bounds[3], w.bounds[18], true, w.bounds[3]) &&
         add(w.bounds[1], w.bounds[13], w.bounds[18]) &&
         divide(w.bounds[18], w.bounds[14], w.bounds[4]) &&
         lift(.5, w.bounds[18]) &&
         extreme(w.bounds[4], w.bounds[18], false, w.bounds[4]) &&
         divide(w.bounds[1], w.bounds[15], w.bounds[19]) &&
         extreme(w.bounds[4], w.bounds[19], false, w.bounds[4]);
}
auto cut_domain(const OracleWorkspace& w) -> bool {
  return ready(w.bounds[3]) && ready(w.bounds[4]) && w.bounds[3].low >= -.5 &&
         w.bounds[4].high <= .5 && w.bounds[3].high < w.bounds[4].low;
}
auto closed_radial(OracleWorkspace& w, const LdBounds& tau, LdBounds& out)
    -> bool {
  if (!(square(tau, w.bounds[18]) && lift(1, w.bounds[19]) &&
        subtract(w.bounds[19], w.bounds[18], w.bounds[19]) &&
        root(w.bounds[19], w.bounds[19]) &&
        multiply(w.bounds[14], w.bounds[19], w.bounds[20]) &&
        multiply(w.bounds[14], tau, w.bounds[18]) &&
        subtract(w.bounds[1], w.bounds[18], w.bounds[18]) &&
        square(w.bounds[18], w.bounds[19]) &&
        square(w.bounds[13], w.bounds[21]) &&
        subtract(w.bounds[21], w.bounds[19], w.bounds[19])))
    return false;
  if (!ready(w.bounds[19]) || w.bounds[19].high < 0) return false;
  // Only the authentic independently proved closed cut theorem permits this
  // thigh-radicand intersection; no arbitrary negative-domain repair.
  w.bounds[19].low = std::max(0.L, w.bounds[19].low);
  return root(w.bounds[19], w.bounds[19]) &&
         add(w.bounds[19], w.bounds[20], out);
}
auto projection_root(LdBounds& a) -> bool {
  if (!ready(a)) return false;
  // The analytic nominal LOWER squared-height function is MAX(0,value).
  a.low = std::max(0.L, a.low);
  a.high = std::max(0.L, a.high);
  return root(a, a);
}
auto images(OracleWorkspace& w) -> bool {
  if (!(square(w.bounds[0], w.bounds[18]) &&
        square(w.bounds[5], w.bounds[19]) &&
        subtract(w.bounds[19], w.bounds[18], w.bounds[19]) &&
        projection_root(w.bounds[19]) &&
        add(w.bounds[2], w.bounds[19], w.bounds[7]) &&
        square(w.bounds[6], w.bounds[19]) &&
        subtract(w.bounds[19], w.bounds[18], w.bounds[19]) &&
        root(w.bounds[19], w.bounds[19]) &&
        add(w.bounds[2], w.bounds[19], w.bounds[8]) &&
        square(w.bounds[1], w.bounds[19]) &&
        add(w.bounds[18], w.bounds[19], w.bounds[18]) &&
        subtract(w.bounds[13], w.bounds[14], w.bounds[19]) &&
        square(w.bounds[19], w.bounds[19]) &&
        subtract(w.bounds[19], w.bounds[18], w.bounds[19]) &&
        projection_root(w.bounds[19]) &&
        add(w.bounds[2], w.bounds[19], w.bounds[19]) &&
        extreme(w.bounds[7], w.bounds[19], true, w.bounds[7]) &&
        square(w.bounds[15], w.bounds[19]) &&
        subtract(w.bounds[19], w.bounds[18], w.bounds[19]) &&
        root(w.bounds[19], w.bounds[19]) &&
        add(w.bounds[2], w.bounds[19], w.bounds[19]) &&
        extreme(w.bounds[8], w.bounds[19], false, w.bounds[8]) &&
        absolute(w.bounds[0], w.bounds[18])))
    return false;
  w.bounds[19].low = w.scalars[0];
  w.bounds[19].high = w.scalars[1];
  return divide(w.bounds[18], w.bounds[19], w.bounds[18]) &&
         add(w.bounds[2], w.bounds[18], w.bounds[18]) &&
         extreme(w.bounds[7], w.bounds[18], true, w.bounds[7]);
}
auto factor(OracleWorkspace& w) -> bool {
  w.bounds[11] = w.bounds[9];
  if (!(subtract(w.bounds[11], w.bounds[2], w.bounds[11])) ||
      w.bounds[11].low <= 0)
    return false;
  if (w.bounds[0].low == 0 && w.bounds[0].high == 0) {
    if (!lift(1, w.bounds[11])) return false;
  } else if (!(square(w.bounds[0], w.bounds[18]) &&
               square(w.bounds[11], w.bounds[19]) &&
               add(w.bounds[18], w.bounds[19], w.bounds[18]) &&
               root(w.bounds[18], w.bounds[18]) &&
               divide(w.bounds[11], w.bounds[18], w.bounds[11])))
    return false;
  w.bounds[18].low = w.scalars[0];
  w.bounds[18].high = w.scalars[1];
  return square(w.bounds[18], w.bounds[18]) && lift(1, w.bounds[19]) &&
         add(w.bounds[19], w.bounds[18], w.bounds[18]) &&
         root(w.bounds[18], w.bounds[18]) &&
         divide(w.bounds[19], w.bounds[18], w.bounds[18]) &&
         extreme(w.bounds[11], w.bounds[18], true, w.bounds[11]);
}
auto threshold(const Diagnostic& d, OracleWorkspace& w) -> bool {
  const auto* pelvis =
      std::get_if<BoardingRoutePhaseBoxBinding>(&d.parts[0].reservation);
  const auto* thigh =
      std::get_if<BoardingPlantedBodyCapsuleBinding>(&d.parts[3].reservation);
  if (!pelvis || !thigh) return false;
  return lift(kBoardingSelfHipLengthMetres, w.bounds[18]) &&
         lift(thigh->radius_metres, w.bounds[19]) &&
         lift(pelvis->half_size_metres.y, w.bounds[20]) &&
         w.bounds[18].low > w.bounds[20].high && w.bounds[20].low > 0 &&
         w.bounds[19].low > 0 && w.bounds[19].high <= w.bounds[18].low &&
         w.bounds[18].high < w.bounds[13].low &&
         square(w.bounds[18], w.bounds[21]) &&
         square(w.bounds[19], w.bounds[22]) &&
         add(w.bounds[21], w.bounds[22], w.bounds[21]) &&
         square(w.bounds[20], w.bounds[22]) &&
         subtract(w.bounds[21], w.bounds[22], w.bounds[22]) &&
         root(w.bounds[22], w.bounds[22]) &&
         multiply(w.bounds[19], w.bounds[22], w.bounds[22]) &&
         multiply(w.bounds[18], w.bounds[20], w.bounds[23]) &&
         add(w.bounds[23], w.bounds[22], w.bounds[22]) &&
         divide(w.bounds[22], w.bounds[21], w.bounds[12]) &&
         multiply(w.bounds[18], w.bounds[12], w.bounds[22]) &&
         subtract(w.bounds[22], w.bounds[20], w.bounds[22]) &&
         w.bounds[12].low > 0 && w.bounds[12].high < 1 && w.bounds[22].low > 0;
}
auto best_cap(OracleWorkspace& w) -> bool {
  return square(w.bounds[12], w.bounds[18]) && lift(1, w.bounds[19]) &&
         subtract(w.bounds[19], w.bounds[18], w.bounds[18]) &&
         w.bounds[18].low > 0 && root(w.bounds[18], w.bounds[18]) &&
         multiply(w.bounds[13], w.bounds[18], w.bounds[16]);
}
auto one_sided(const LdBounds& independent, double reported,
               bool reported_minus, bool structural_equal = false) -> Outcome {
  if (!ready(independent) || !std::isfinite(reported))
    return Outcome::inconclusive;
  const auto reference = static_cast<long double>(reported);
  const auto allowance =
      2e-12L *
      std::max(std::max(1.L, std::abs(independent.low)),
               std::max(std::abs(independent.high), std::abs(reference)));
  if (structural_equal && independent.low == reference &&
      independent.high == reference)
    return Outcome::corroborated;
  const auto lo = lower(reported_minus ? reference - independent.high
                                       : independent.low - reference);
  const auto hi = upper(reported_minus ? reference - independent.low
                                       : independent.high - reference);
  if (!std::isfinite(lo) || !std::isfinite(hi)) return Outcome::inconclusive;
  if (lo > allowance) return Outcome::corroborated;
  return hi < -allowance ? Outcome::failed : Outcome::inconclusive;
}
auto enclosure(const Scalar& reported, const LdBounds& independent) -> Outcome {
  if (!finite(reported) || !ready(independent)) return Outcome::inconclusive;
  const auto allowance =
      2e-12L *
      std::max(
          std::max(1.L, std::abs(independent.low)),
          std::max(
              std::abs(independent.high),
              std::max(std::abs(static_cast<long double>(reported.lower)),
                       std::abs(static_cast<long double>(reported.upper)))));
  // Two signed margins, each with the frozen allowance. Overlap is
  // INCONCLUSIVE rather than promoted into rigorous interval inclusion.
  const auto lower_lo = lower(independent.low - reported.lower),
             lower_hi = upper(independent.high - reported.lower);
  const auto upper_lo = lower(reported.upper - independent.high),
             upper_hi = upper(reported.upper - independent.low);
  if (lower_hi < -allowance || upper_hi < -allowance) return Outcome::failed;
  if (lower_lo > allowance && upper_lo > allowance)
    return Outcome::corroborated;
  if (independent.low == independent.high && reported.lower == reported.upper &&
      independent.low == static_cast<long double>(reported.lower))
    return Outcome::corroborated;
  return Outcome::inconclusive;
}
void observe(Outcome observation, Outcome& status, std::uint16_t& stages,
             std::size_t stage, const std::string_view& why) {
  const auto prior = static_cast<Outcome>((stages >> (2 * stage)) & 3U);
  const auto next =
      prior == Outcome::failed || observation == Outcome::failed
          ? Outcome::failed
      : prior == Outcome::inconclusive || observation == Outcome::inconclusive
          ? Outcome::inconclusive
          : observation;
  stages = static_cast<std::uint16_t>(
      (stages & ~(std::uint16_t{3} << (2 * stage))) |
      (std::uint16_t(unsigned(next)) << (2 * stage)));
  check(observation != Outcome::failed, why);
  if (observation == Outcome::failed)
    status = Outcome::failed;
  else if (status == Outcome::not_run)
    status = observation;
  else if (observation == Outcome::inconclusive && status != Outcome::failed)
    status = Outcome::inconclusive;
}
auto independent_slack(const Diagnostic& d, std::size_t group,
                       std::size_t index, OracleWorkspace& w) -> bool {
  // 17 is overwritten after each comparison; no independent14-slack archive.
  if (!(lift(.5, w.bounds[18]) &&
        multiply(w.bounds[14], w.bounds[18], w.bounds[18])))
    return false;
  if (group == 0 || group == 2) {
    if (!lift(d.prefix.current_cap_PORT, w.bounds[19])) return false;
  } else
    w.bounds[19] = w.bounds[16];
  if (group < 2) {
    if (index == 0) return add(w.bounds[1], w.bounds[18], w.bounds[17]);
    if (index == 1)
      return add(w.bounds[19], w.bounds[18], w.bounds[17]) &&
             subtract(w.bounds[17], w.bounds[1], w.bounds[17]);
    return multiply(w.bounds[19], w.bounds[15], w.bounds[17]) &&
           divide(w.bounds[17], w.bounds[13], w.bounds[17]) &&
           subtract(w.bounds[17], w.bounds[1], w.bounds[17]);
  }
  if (!lift(d.prefix.W_PORT.lower, w.bounds[20]) ||
      !lift(d.prefix.W_PORT.upper, w.bounds[21]))
    return false;
  if (index == 0) return add(w.bounds[20], w.bounds[18], w.bounds[17]);
  if (index == 1)
    return add(w.bounds[19], w.bounds[18], w.bounds[17]) &&
           subtract(w.bounds[17], w.bounds[21], w.bounds[17]);
  if (index == 2)
    return multiply(w.bounds[14], w.bounds[20], w.bounds[17]) &&
           divide(w.bounds[17], w.bounds[15], w.bounds[17]) &&
           add(w.bounds[19], w.bounds[17], w.bounds[17]) &&
           subtract(w.bounds[17], w.bounds[21], w.bounds[17]);
  return subtract(w.bounds[21], w.bounds[20], w.bounds[17]) &&
         subtract(w.bounds[19], w.bounds[17], w.bounds[17]);
}
[[gnu::noinline]] auto oracle_utility(const Diagnostic& d, const Request& r)
    -> std::uint16_t {
  const ExceptionState exceptions;
  OracleWorkspace w{};
  auto status = Outcome::not_run;
  std::uint16_t stages{};
  if (!(lift(.47285, w.bounds[13]) && lift(.47478, w.bounds[14]) &&
        add(w.bounds[13], w.bounds[14], w.bounds[15]) &&
        lift(3, w.bounds[18]) && root(w.bounds[18], w.bounds[18]) &&
        lift(2, w.bounds[19]) &&
        subtract(w.bounds[19], w.bounds[18], w.bounds[19])))
    return std::uint16_t{2} << 14;
  w.scalars[0] = w.bounds[19].low;
  w.scalars[1] = w.bounds[19].high;
  if (operation_written(d.prefix, 36))
    observe(enclosure(d.prefix.Lsum, w.bounds[15]), status, stages, 0,
            "Independent genuine Lsum signed enclosure margins");
  // NOMINAL_BASE needs both actual certified publications, but failing or
  // missing this stage does not suppress threshold/slack stages below.
  if (guard_written(d.prefix, 24) && (d.prefix.scalar_ready & 3U) == 3U) {
    bool nominal_ready = true;
    for (std::size_t side = 0; side < 2; ++side) {
      if (!(same_side(r, side, w) && nominal_cuts(w) && cut_domain(w) &&
            closed_radial(w, w.bounds[3], w.bounds[5]) &&
            closed_radial(w, w.bounds[4], w.bounds[6]) && images(w))) {
        nominal_ready = false;
        break;
      }
      if (side == 0) {
        w.bounds[9] = w.bounds[7];
        w.bounds[10] = w.bounds[8];
      } else if (!(extreme(w.bounds[9], w.bounds[7], true, w.bounds[9]) &&
                   extreme(w.bounds[10], w.bounds[8], false, w.bounds[10]))) {
        nominal_ready = false;
        break;
      }
    }
    if (nominal_ready) {
      observe(one_sided(w.bounds[9], d.prefix.base_lo, true), status, stages, 1,
              "Independent nominal BOTH BASE lower inward margin");
      observe(one_sided(w.bounds[10], d.prefix.base_hi, false), status, stages,
              1, "Independent nominal BOTH BASE upper inward margin");
    } else
      observe(Outcome::inconclusive, status, stages, 1,
              "Ambiguous independent nominal BASE domain remains inconclusive");
    // Independent nominal BOTH9/10 have reached their last consumers. These
    // slots now hold exactly the allowed published BASE comparison domain.
    lift(d.prefix.base_lo, w.bounds[9]);
    lift(d.prefix.base_hi, w.bounds[10]);
    for (std::size_t side = 0; side < 2; ++side)
      if (guard_written(d.prefix, side == 0 ? 32 : 38)) {
        if (same_side(r, side, w) && factor(w))
          observe(one_sided(w.bounds[11], d.prefix.factor_lower[side], false,
                            w.bounds[0].low == 0 && w.bounds[0].high == 0),
                  status, stages, 2,
                  "Independent safe same-published-BASE factor margin");
        else
          observe(
              Outcome::inconclusive, status, stages, 2,
              "Ambiguous independent same-BASE factor remains inconclusive");
      }
  }
  const bool chart_ready = same_side(r, 0, w);
  if (chart_ready && operation_written(d.prefix, 208))
    observe(enclosure(d.prefix.W_PORT, w.bounds[1]), status, stages, 0,
            "Independent signed PORT W source enclosure margins");
  const bool threshold_ready = guard_written(d.prefix, 25) && threshold(d, w);
  if (threshold_ready && operation_written(d.prefix, 171))
    observe(enclosure(d.prefix.threshold, w.bounds[12]), status, stages, 3,
            "Independent authentic unsquared hip threshold enclosure margins");
  if (threshold_ready && guard_written(d.prefix, 29) &&
      guard_written(d.prefix, 32) && guard_written(d.prefix, 35)) {
    if (lift(d.prefix.factor_lower[0], w.bounds[18]) &&
        divide(w.bounds[12], w.bounds[18], w.bounds[19]) &&
        w.bounds[19].low > 0 && w.bounds[19].high < 1 &&
        square(w.bounds[19], w.bounds[18]) && lift(1, w.bounds[19]) &&
        subtract(w.bounds[19], w.bounds[18], w.bounds[18]) &&
        root(w.bounds[18], w.bounds[18]) &&
        multiply(w.bounds[13], w.bounds[18], w.bounds[16]))
      observe(one_sided(w.bounds[16], d.prefix.current_cap_PORT, false), status,
              stages, 4,
              "Independent used current cap upper constraint margin");
    else
      observe(Outcome::inconclusive, status, stages, 4,
              "Ambiguous independent current cap domain remains inconclusive");
  }
  // Restricted p/q are independent nominal formulas with the genuinely
  // selected finite g. They need no new p<q premise and may also collapse.
  if (chart_ready && guards_through(d, 41, false) && nominal_cuts(w)) {
    if (lift(d.prefix.current_cap_PORT, w.bounds[18]) &&
        subtract(w.bounds[1], w.bounds[18], w.bounds[18]) &&
        divide(w.bounds[18], w.bounds[14], w.bounds[18]) &&
        extreme(w.bounds[3], w.bounds[18], true, w.bounds[3]) &&
        divide(w.bounds[1], w.bounds[14], w.bounds[18]) &&
        extreme(w.bounds[4], w.bounds[18], false, w.bounds[4])) {
      if (operation_written(d.prefix, 219))
        observe(one_sided(w.bounds[3], d.prefix.p_PORT, true), status, stages,
                4, "Independent restricted current p inward margin");
      if (operation_written(d.prefix, 221))
        observe(one_sided(w.bounds[4], d.prefix.q_PORT, false), status, stages,
                4, "Independent restricted current q inward margin");
    } else
      observe(Outcome::inconclusive, status, stages, 4,
              "Ambiguous independent restricted cut remains inconclusive");
  }
  const bool ideal_ready = threshold_ready && best_cap(w);
  if (ideal_ready && operation_written(d.prefix, 225))
    observe(enclosure(d.optimistic.best_cap, w.bounds[16]), status, stages, 5,
            "Independent authentic ideal best cap enclosure margins");
  for (std::size_t group = 0; group < 4; ++group) {
    if (!guard_written(d.prefix, 5) || (group < 2 && !chart_ready) ||
        (group >= 2 && !operation_written(d.prefix, 208)) ||
        ((group == 1 || group == 3) && !ideal_ready) ||
        ((group == 0 || group == 2) && !guard_written(d.prefix, 35)))
      continue;
    for (std::size_t index = 0; index < (group < 2 ? 3U : 4U); ++index) {
      if (!operation_written(d.prefix, slack_row(group, index))) continue;
      charge(totals.oracle_slacks, 14);
      if (independent_slack(d, group, index, w))
        observe(
            enclosure(slack(d, group, index), w.bounds[17]), status, stages, 6,
            "Independent streamed signed scalar or universal slack margins");
      else
        observe(Outcome::inconclusive, status, stages, 6,
                "Ambiguous independent signed slack remains inconclusive");
    }
  }
  return static_cast<std::uint16_t>(stages |
                                    (std::uint16_t(unsigned(status)) << 14));
}
[[gnu::noinline]] auto construction_audit(const Diagnostic& d)
    -> std::uint16_t {
  if (!source_complete(d)) return 0;
  charge(totals.oracle_calls, 1);
  Request request{};
  horizontal_request_into(request);
  return oracle_utility(d, request);
}
void slot(std::size_t n, bool executed) {
  check(n == totals.next_slot && n >= 1 && n <= 36,
        "Every literal A01-A36 purpose is visited exactly once in order");
  charge(totals.next_slot, 37);
  if (executed) {
    charge(totals.consumers, 36);
    aggregate(3, 1);
  } else {
    charge(totals.skips, 36);
    std::cout << "SKIP A" << n << " unreached_or_unavailable\n";
  }
}
void print_bound(const Scalar& b) {
  std::cout << " supported=" << b.supported;
  if (b.supported) std::cout << " lower=" << b.lower << " upper=" << b.upper;
}
[[gnu::noinline]] void print_first(const Diagnostic& d, std::uint16_t oracle) {
  std::cout << "FIRST_KNEE_COMPATIBILITY01 A01 state=" << unsigned(d.state)
            << " stop=" << unsigned(d.stop_condition)
            << " source=" << d.work.source_guards
            << " operations=" << d.work.construction_operations
            << " guards=" << d.work.construction_guards
            << " prefix=" << unsigned(d.prefix.prefix_state)
            << " complete=" << d.complete << " has_refusal=" << d.has_refusal
            << " required=" << d.output_capacity_bytes << '\n';
  std::cout << "FIRST_COMPATIBILITY01_LEDGER op=" << d.prefix.operation
            << " guard=" << unsigned(d.prefix.guard)
            << " side=" << unsigned(d.prefix.side)
            << " condition=" << unsigned(d.prefix.condition)
            << " scalar_ready=" << d.prefix.scalar_ready
            << " zero=" << d.prefix.zero_mask
            << " source_mask=" << d.source_evaluated
            << " guard_attempted=" << d.prefix.guard_attempted
            << " guard_written=" << d.prefix.guard_written << " op_attempted=";
  for (auto word : d.prefix.operation_attempted)
    std::cout << word << ',';
  std::cout << " op_written=";
  for (auto word : d.prefix.operation_written)
    std::cout << word << ',';
  std::cout << '\n';
  for (std::size_t bit = 0; bit < 7; ++bit) {
    std::cout << "FIRST_COMPATIBILITY01_SCALAR bit=" << bit;
    if (d.prefix.scalar_ready & (1U << bit))
      std::cout << " value=" << scalar_value(d.prefix, bit);
    else
      std::cout << " NOT_RUN";
    std::cout << '\n';
  }
  if (guard_written(d.prefix, 5))
    std::cout << "FIRST_COMPATIBILITY01_LENGTHS L1=" << d.prefix.L1
              << " L2=" << d.prefix.L2 << '\n';
  if (operation_written(d.prefix, 36)) {
    std::cout << "FIRST_COMPATIBILITY01_LSUM";
    print_bound(d.prefix.Lsum);
    std::cout << '\n';
  }
  if (operation_written(d.prefix, 171)) {
    std::cout << "FIRST_COMPATIBILITY01_THRESHOLD";
    print_bound(d.prefix.threshold);
    std::cout << '\n';
  }
  if (operation_written(d.prefix, 208)) {
    std::cout << "FIRST_COMPATIBILITY01_W_PORT";
    print_bound(d.prefix.W_PORT);
    std::cout << '\n';
  }
  if (operation_written(d.prefix, 225)) {
    std::cout << "FIRST_COMPATIBILITY01_BEST_CAP";
    print_bound(d.optimistic.best_cap);
    std::cout << '\n';
  }
  for (std::size_t group = 0; group < 4; ++group) {
    std::cout << "FIRST_COMPATIBILITY01_CLASSIFIER group=" << group << " ready="
              << bool(d.optimistic.classifier_ready & (1U << group))
              << " class=" << unsigned(d.optimistic.classification[group])
              << '\n';
    for (std::size_t index = 0; index < (group < 2 ? 3U : 4U); ++index) {
      std::cout << "FIRST_COMPATIBILITY01_SLACK group=" << group
                << " index=" << index;
      if (operation_written(d.prefix, slack_row(group, index)))
        print_bound(slack(d, group, index));
      else
        std::cout << " NOT_RUN";
      std::cout << '\n';
    }
  }
  if (d.has_refusal) {
    const auto& r = d.first_refusal;
    std::cout << "FIRST_COMPATIBILITY01_REFUSAL condition="
              << unsigned(r.condition)
              << " predicate=" << unsigned(r.predicate_condition)
              << " stage=" << unsigned(r.self_stage)
              << " operation=" << r.operation << " pair=" << r.self_pair
              << " region=" << unsigned(r.self_region)
              << " axis=" << unsigned(r.self_axis)
              << " sign=" << unsigned(r.self_sign);
    print_bound(r.limiting_bound);
    std::cout << " side=" << r.side.value_or(255)
              << " edge=" << r.edge.value_or(255)
              << " coordinate=" << r.axis.value_or(255)
              << " source_edge=" << r.source_edge
              << " source_name=" << r.source_name
              << " phase_condition=" << unsigned(r.phase.condition)
              << " phase_predicate=" << unsigned(r.phase.predicate_condition)
              << " phase_side=" << r.phase.side.value_or(255)
              << " phase_depth=" << r.phase.depth;
    if (r.source_key)
      std::cout << " key=" << unsigned(r.source_key->buffer) << ','
                << r.source_key->group << ',' << r.source_key->triangle;
    if (r.phase.condition != BoardingRouteFootPhaseCondition::none)
      std::cout << " phase_first=" << r.phase.first
                << " phase_last=" << r.phase.last
                << " phase_lower=" << r.phase.limiting_bound.lower
                << " phase_upper=" << r.phase.limiting_bound.upper;
    std::cout << '\n';
  }
  std::cout
      << "FIRST_COMPATIBILITY01_ORACLE outcome=" << unsigned(oracle >> 14)
      << " (0=not_run,1=corroborated,2=inconclusive,3=failed) streamed_slacks="
      << totals.oracle_slacks
      << " original_geometry_qualified=0 boarding_success=0\n";
  std::cout << "FIRST_COMPATIBILITY01_ORACLE_STAGES chart="
            << ((oracle >> 0) & 3U) << " nominal_BASE=" << ((oracle >> 2) & 3U)
            << " same_BASE_factor=" << ((oracle >> 4) & 3U)
            << " threshold=" << ((oracle >> 6) & 3U)
            << " current_cap_cut=" << ((oracle >> 8) & 3U)
            << " ideal_best_cap=" << ((oracle >> 10) & 3U)
            << " signed_slacks=" << ((oracle >> 12) & 3U) << '\n'
            << std::flush;
}
[[gnu::noinline]] void first_one(const Provider& p, Summary& summary) {
  slot(1, true); // A01: sole public default purpose, never substituted/retried.
  const auto result = assess_origin_boarding_knee_compatibility_diagnostic01(p);
  if (!result) {
    check(!result.error().empty(),
          "Authentic unexpected A01 is retained without replacement");
    std::cout << "FIRST_KNEE_COMPATIBILITY01 A01 API_ERROR=" << result.error()
              << '\n'
              << std::flush;
    return;
  }
  accounting(*result, p, Limits{});
  capture(*result, summary);
  const auto oracle = construction_audit(*result);
  // All independent scratch and exterior current/pending Request owners have
  // died before reporting; the full Expected/provider/caller remain charged.
  print_first(*result, oracle);
}
[[gnu::noinline]] void create_source(const NativeCraftBinding& binding,
                                     const OriginBoardingBootSupport& boots,
                                     std::optional<Provider>& p) {
  charge(totals.creators, 1);
  auto result = make_origin_boarding_intermediate_pause_support(binding, boots);
  if (result) {
    check(Access::valid(*result) && result->summary() &&
              result->summary()->complete,
          "Only the unchanged genuine creator issues the retained source");
    p.emplace(std::move(*result));
    std::cout << "FIRST_COMPATIBILITY01_SOURCE admitted=1\n";
  } else {
    check(!result.error().empty(),
          "Creator refusal has no substitute or retry");
    std::cout << "FIRST_COMPATIBILITY01_SOURCE admitted=0 error="
              << result.error() << '\n';
  }
  // Creator Expected/error and moved-empty handle die before any consumer.
}
void same_evidence(const Diagnostic& d, const Summary& s) {
  if (!s.exists) return;
  check(d.work.source_guards == s.used[0] &&
            d.work.construction_operations == s.used[1] &&
            d.work.construction_guards == s.used[2] &&
            d.output_capacity_bytes == s.output && semantic_hash(d) == s.digest,
        "Genuine copied/moved/exact-used owner retains only authentic A01 "
        "semantic evidence");
}
void capacity_check(const Diagnostic& d, const Limits& l, std::size_t field) {
  const auto expected = field == 0   ? Stop::source_capacity
                        : field == 1 ? Stop::operation_capacity
                        : field == 2 ? Stop::guard_capacity
                                     : Stop::output_capacity;
  check(d.state == State::capacity && d.stop_condition == expected &&
            d.has_refusal && !d.complete,
        "Exact failed charge category is distinct from first ordinary finding");
  if (field == 0) {
    check(d.work.source_guards == l.source_guards &&
              d.source_evaluated == prefix_mask(l.source_guards) &&
              !d.source_enrolled && d.work.construction_operations == 0 &&
              d.work.construction_guards == 0,
          "Source capacity publishes no uncharged row or prefix");
    check(d.first_refusal.condition == SourceCondition::source_capacity &&
              d.first_refusal.operation == l.source_guards &&
              d.first_refusal.self_stage == Stage::source,
          "Full original source-capacity metadata is translated unchanged");
  } else if (field == 1) {
    check(
        d.work.construction_operations == l.construction_operations &&
            d.prefix.operation == l.construction_operations,
        "Operation capacity retains upcoming uint16 cursor without increment");
    check(!operation_written(d.prefix, l.construction_operations + 1),
          "Failed operation charge has no uncomputed write");
    for (std::size_t word = 0; word < 4; ++word)
      check(d.prefix.operation_attempted[word] ==
                    operation_word(l.construction_operations, word) &&
                d.prefix.operation_written[word] ==
                    operation_word(l.construction_operations, word),
            "Eligible deterministic operation capacity retains exact "
            "predecessor attempts and writes");
  } else if (field == 2) {
    check(d.work.construction_guards == l.construction_guards &&
              d.prefix.guard == l.construction_guards &&
              !guard_attempted(d.prefix, l.construction_guards + 1),
          "Guard capacity retains upcoming uint8 cursor and no failed "
          "attempted bit");
    check(guards_through(d, l.construction_guards, true),
          "Eligible guard capacity retains predecessor writes except genuine "
          "G42 finding");
  } else {
    check(
        d.output_capacity_bytes ==
                boarding_knee_compatibility_diagnostic01_required_output_bytes() &&
            d.work.source_guards == 0 && d.source_evaluated == 0 &&
            d.work.construction_operations == 0 &&
            d.work.construction_guards == 0,
        "Structured TWO-header output refusal precedes source and arithmetic");
    check(d.first_refusal.condition == SourceCondition::output_capacity &&
              d.first_refusal.operation == 65535,
          "Output refusal preserves unset fresh cursor and original condition");
  }
}
void handoff_check(const Diagnostic& d, std::size_t n) {
  if (n == 33)
    check(d.work.construction_operations == 221 &&
              d.work.construction_guards == 41 &&
              operations_through(d.prefix, 221) &&
              guards_through(d, 41, false) && !guard_attempted(d.prefix, 42) &&
              d.prefix.prefix_state == PrefixState::not_run &&
              d.optimistic.classifier_ready == 0,
          "A33 stops before G42 ordinary predicate despite readable p/q");
  if (n == 34)
    check(d.work.construction_operations == 221 &&
              d.work.construction_guards == 42 &&
              guard_attempted(d.prefix, 42) &&
              (d.prefix.prefix_state == PrefixState::cuts_available ||
               collapsed(d)) &&
              d.optimistic.classifier_ready == 0 &&
              !operation_written(d.prefix, 222),
          "A34 preserves supported actual G42 finding with no first suffix "
          "publication");
  if (n == 35)
    check(d.work.construction_operations == 254 &&
              d.optimistic.classifier_ready == 7 &&
              operation_written(d.prefix, 248) &&
              operation_written(d.prefix, 250) &&
              operation_written(d.prefix, 253) &&
              !operation_written(d.prefix, 255) &&
              !guard_attempted(d.prefix, 49) &&
              d.optimistic.classification[3] == Classification::not_run,
          "A35 retains first three classifier groups and only three "
          "common-best slacks");
  if (n == 36)
    check(d.work.construction_operations == 255 &&
              d.work.construction_guards == 49 && d.prefix.operation == 254 &&
              d.prefix.guard == 49 && d.optimistic.classifier_ready == 15 &&
              !d.complete && !d.optimistic.complete &&
              !guard_attempted(d.prefix, 50),
          "A36 preserves all arithmetic/four classifications but no final G50 "
          "completion");
}
[[gnu::noinline]] void assessment_one(const Provider& p, std::size_t n,
                                      const Limits& l, const Summary& s,
                                      std::size_t field = 4,
                                      const std::string_view& error = {},
                                      bool equivalent = false,
                                      bool empty = false) {
  slot(n, true);
  const auto result = detail::knee_compatibility_diagnostic01_bounded(p, l);
  if (!error.empty()) {
    check(!result && result.error() == error,
          "FP/raised/ONE-header preflight retains exact registered literal");
  } else {
    if (n >= 22 && n <= 24 && !s.exists)
      check(result.has_value() || !result.error().empty(),
            "Unavailable A01 allows authentic valid-owner structured or "
            "unexpected evidence");
    else
      check(result.has_value(),
            "Eligible purpose returns structured evidence without substitute");
    if (result) {
      accounting(*result, p, l);
      if (field < 4) capacity_check(*result, l, field);
      if (equivalent) same_evidence(*result, s);
      if (empty)
        check(result->state == State::identity &&
                  result->stop_condition == Stop::invalid_binding &&
                  result->work.source_guards == 1 &&
                  result->source_evaluated == 1 && result->has_refusal &&
                  result->first_refusal.condition ==
                      SourceCondition::invalid_binding &&
                  result->first_refusal.operation == 0 &&
                  result->first_refusal.self_stage == Stage::source &&
                  result->work.construction_operations == 0 &&
                  result->work.construction_guards == 0,
              "A21 genuinely moved-empty source refuses charged original row0 "
              "with no prefix");
      if (n >= 33) handoff_check(*result, n);
    }
  }
  // No Request/helper scratch remains here. One compact purpose line only.
  if (result)
    std::cout << "A" << n << " state=" << unsigned(result->state)
              << " stop=" << unsigned(result->stop_condition)
              << " work=" << result->work.source_guards << ','
              << result->work.construction_operations << ','
              << result->work.construction_guards << " classifiers="
              << unsigned(result->optimistic.classifier_ready) << '\n';
  else
    std::cout << "A" << n << " API_ERROR=" << result.error() << '\n';
}
void zero_one(const Provider& p, const Summary& s, std::size_t n,
              std::size_t field) {
  if (field < 3 && (!s.exists || s.used[field] == 0)) {
    slot(n, false);
    return;
  }
  Limits l;
  if (field == 0)
    l.source_guards = 0;
  else if (field == 1)
    l.construction_operations = 0;
  else if (field == 2)
    l.construction_guards = 0;
  else
    l.output_bytes = 0;
  assessment_one(p, n, l, s, field,
                 field == 3 ? "compatibility01 output headers" : "");
}
void raised_one(const Provider& p, const Summary& s, std::size_t n,
                std::size_t field) {
  Limits l;
  if (field == 0)
    ++l.source_guards;
  else if (field == 1)
    ++l.construction_operations;
  else if (field == 2)
    ++l.construction_guards;
  else
    ++l.output_bytes;
  assessment_one(p, n, l, s, 4, "compatibility01 invalid limits");
}
void minus_one(const Provider& p, const Summary& s, std::size_t n,
               std::size_t field) {
  if (field < 3 && (!s.exists || s.used[field] <= 1)) {
    slot(n, false);
    return;
  }
  Limits l;
  if (field == 0)
    l.source_guards = s.used[0] - 1;
  else if (field == 1)
    l.construction_operations = s.used[1] - 1;
  else if (field == 2)
    l.construction_guards = s.used[2] - 1;
  else
    l.output_bytes =
        boarding_knee_compatibility_diagnostic01_required_output_bytes() - 1;
  assessment_one(p, n, l, s, field);
}
void fp_one(const Provider& p, const Summary& s, std::size_t n,
            std::size_t mode) {
  const auto rounding = std::fegetround(),
             flags = std::fetestexcept(FE_ALL_EXCEPT);
#if defined(__SSE2__) && defined(__x86_64__)
  const auto control = _mm_getcsr();
#endif
  {
    const Environment changed(mode);
    if (changed.available)
      assessment_one(p, n, Limits{}, s, 4,
                     "compatibility01 unsupported floating point");
    else
      slot(n, false);
  }
  check(std::fegetround() == rounding &&
            std::fetestexcept(FE_ALL_EXCEPT) == flags,
        "FP control restores original rounding and exception flags");
#if defined(__SSE2__) && defined(__x86_64__)
  check(_mm_getcsr() == control,
        "FP control restores every original MXCSR bit including traps");
#endif
}
void crossing_one(const Provider& p, const Summary& s, std::size_t n,
                  std::size_t index) {
  if (!s.exists || (s.eligibility & (1U << index)) == 0) {
    slot(n, false);
    return;
  }
  Limits l;
  l.construction_operations = crossing_cap(index);
  assessment_one(p, n, l, s, 1);
}
void handoff_one(const Provider& p, const Summary& s, std::size_t n,
                 std::size_t index) {
  if (!s.exists || (s.eligibility & (1U << index)) == 0) {
    slot(n, false);
    return;
  }
  Limits l;
  if (n == 33)
    l.construction_guards = 41;
  else if (n == 34)
    l.construction_operations = 221;
  else if (n == 35)
    l.construction_operations = 254;
  else
    l.construction_guards = 49;
  assessment_one(p, n, l, s, n == 33 || n == 36 ? 2 : 1);
}
[[gnu::noinline]] void remaining_roster(Provider& p, const Summary& s) {
  // Literal36 inventory. Every helper makes exactly one bounded call or SKIP;
  // ownership branches are real lifetimes, never synthetic report fixtures.
  zero_one(p, s, 2, 0);   // A02
  zero_one(p, s, 3, 1);   // A03
  zero_one(p, s, 4, 2);   // A04
  zero_one(p, s, 5, 3);   // A05
  raised_one(p, s, 6, 0); // A06
  raised_one(p, s, 7, 1); // A07
  raised_one(p, s, 8, 2); // A08
  raised_one(p, s, 9, 3); // A09
  minus_one(p, s, 10, 0); // A10
  minus_one(p, s, 11, 1); // A11
  minus_one(p, s, 12, 2); // A12
  minus_one(p, s, 13, 3); // A13
  if (s.exists) {
    Limits l;
    l.source_guards = s.used[0];
    l.construction_operations = s.used[1];
    l.construction_guards = s.used[2];
    l.output_bytes =
        boarding_knee_compatibility_diagnostic01_required_output_bytes();
    assessment_one(p, 14, l, s, 4, {}, true);
  } else
    slot(14, false);   // A14
  fp_one(p, s, 15, 0); // A15
  fp_one(p, s, 16, 1); // A16
  fp_one(p, s, 17, 2); // A17
  fp_one(p, s, 18, 3); // A18
  fp_one(p, s, 19, 4); // A19
  fp_one(p, s, 20, 5); // A20
  {
    auto alias = p;
    auto saved = std::move(alias);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Test moved-empty handle.
    assessment_one(alias, 21, Limits{}, s, 4, {}, false, true); // A21
    alias = std::move(saved);
  }
  {
    auto moved = std::move(p);
    assessment_one(moved, 22, Limits{}, s, 4, {}, s.exists);
    p = std::move(moved);
  } // A22
  {
    auto survivor = p;
    {
      auto discarded = std::move(p);
      check(Access::valid(discarded),
            "Original provider exists in the scoped discard owner");
    }
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Test discarded handle.
    check(!Access::valid(p),
          "Original genuinely empty after discard owner destruction");
    assessment_one(survivor, 23, Limits{}, s, 4, {}, s.exists);
    p = std::move(survivor); // A23
  }
  assessment_one(p, 24, Limits{}, s, 4, {}, s.exists); // A24
  if (s.exists && s.used[0] > 0) {
    Limits l;
    l.source_guards = 0;
    l.construction_guards = 0;
    assessment_one(p, 25, l, s, 0);
  } else
    slot(25, false); // A25
  if (s.exists && s.used[2] > 0) {
    Limits l;
    l.construction_guards = 0;
    l.construction_operations = 0;
    assessment_one(p, 26, l, s, 2);
  } else
    slot(26, false);         // A26
  crossing_one(p, s, 27, 0); // A27
  crossing_one(p, s, 28, 1); // A28
  crossing_one(p, s, 29, 2); // A29
  crossing_one(p, s, 30, 3); // A30
  crossing_one(p, s, 31, 4); // A31
  crossing_one(p, s, 32, 5); // A32
  handoff_one(p, s, 33, 6);  // A33
  handoff_one(p, s, 34, 7);  // A34
  handoff_one(p, s, 35, 8);  // A35
  handoff_one(p, s, 36, 9);  // A36
}
[[gnu::noinline]] void final_counts() {
  check(totals.creators == 1 && totals.consumers + totals.skips == 36 &&
            totals.next_slot == 37 && totals.oracle_calls <= 1 &&
            totals.oracle_slacks <= 14 && totals.classifiers_checked <= 144,
        "Literal36 one-creator one-oracle program preserves finite bounds");
  check(
      totals.work[0] <= 2340 && totals.work[1] <= 9216 &&
          totals.work[2] <= 1836 && totals.work[3] <= 36 &&
          totals.work[3] == totals.consumers,
      "Finite aggregate cap-only proof includes all authentic FP-FIRST calls");
  check(checks <= 41984 && failures <= checks,
        "Finite41984 computed checks fit selected65536 storage proof");
  std::cout << "COMPATIBILITY01_VALIDATION creator=" << totals.creators
            << " consumers=" << totals.consumers << " skips=" << totals.skips
            << " oracle=" << totals.oracle_calls
            << " slacks=" << totals.oracle_slacks
            << " classifiers=" << totals.classifiers_checked
            << " checks=" << checks << " failures=" << failures
            << " aggregate=" << totals.work[0] << ',' << totals.work[1] << ','
            << totals.work[2] << ',' << totals.work[3] << '\n';
}
} // namespace
int main() {
  StreamCounter out(std::cout.rdbuf(), shared_bytes),
      err(std::cerr.rdbuf(), shared_bytes);
  auto* oldout = std::cout.rdbuf(&out);
  auto* olderr = std::cerr.rdbuf(&err);
  try {
    std::cout << std::setprecision(17);
    // WORLD/binding/boot prerequisite errors precede source creation and have
    // separate nonzero process ownership. Numeric-origin errors remain capped.
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
      remaining_roster(*source, summary);
    } else
      for (std::size_t n = 1; n <= 36; ++n)
        slot(n, false);
    final_counts();
  } catch (const std::exception& e) {
    check(false, "Unexpected harness exception has no replacement purpose");
    std::cerr << e.what() << '\n';
  }
  if (!first_failure.empty()) std::cerr << "FAIL: " << first_failure << '\n';
  const bool streams = std::cout.good() && std::cerr.good() && !out.truncated &&
                       !err.truncated && shared_bytes <= 16384;
  check(streams, "Shared16KiB output is complete and truncation is a failure");
  std::cout.rdbuf(oldout);
  std::cerr.rdbuf(olderr);
  return failures ? 1 : 0;
}
