#include "apsis_drift/origin_boarding_route_intermediate_root_z01.hpp"
#include "origin_boarding_route_intermediate_root_z01_internal.hpp"

#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <streambuf>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
using namespace apsis_drift;
using Provider = OriginBoardingRouteIntermediateRootZ01;
using Expected = BoardingRouteIntermediateRootZ01Expected;
using Diagnostic = BoardingRouteIntermediateRootZ01Diagnostic;
using Cell = BoardingRouteIntermediateRootZ01Cell;
using State = BoardingRouteIntermediateRootZ01State;
using Condition = BoardingRouteIntermediateRootZ01Condition;
using Stage = BoardingRouteIntermediateRootZ01Stage;
using Event = BoardingRouteIntermediateRootZ01ContactEvent;
using Limits = detail::BoardingRouteIntermediateRootZ01Limits;
using Access = detail::BoardingRouteIntermediateRootZ01Access;
using ConstructorLimits =
    detail::BoardingRouteIntermediateRootZ01ConstructorLimits;
using ConstructorEvidence = BoardingRouteIntermediateRootZ01ConstructorEvidence;
using Context = detail::BoardingRouteIntermediateRootZ01Context;
using Token = detail::BoardingRouteIntermediateRootZ01CurrentToken;
static_assert(!std::is_default_constructible_v<Context>);
static_assert(!std::is_copy_constructible_v<Context>);
static_assert(!std::is_default_constructible_v<Token>);
static_assert(!std::is_copy_constructible_v<Token>);
static_assert(!std::is_constructible_v<Context, const Diagnostic&,
                                       const BoardingRouteFootPhaseRequest&,
                                       const std::size_t&>);
static_assert(!std::is_constructible_v<Token, const Context&, const Diagnostic&,
                                       const Cell&,
                                       const BoardingRouteFootPhaseRequest&>);
static_assert(
    !std::is_convertible_v<OriginBoardingIntermediatePauseSupport, Provider>);
static_assert(
    !std::is_convertible_v<
        detail::BoardingRouteIntermediateSupportSelf01CurrentCellToken, Token>);
static_assert(!Diagnostic::seat_qualified && !Diagnostic::actor_qualified &&
              !Diagnostic::save_qualified &&
              !Diagnostic::first_flight_qualified);

constexpr std::uint64_t maximum_checks{86042624};
constexpr std::size_t maximum_transcript{262144};
struct Summary {
  std::uint64_t checks{}, failures{}, assessment_mask{},
      called_assessment_mask{};
  std::uint64_t block_start{}, block_limit{16384};
  std::size_t bytes{};
  unsigned creators{}, assessments{}, fixture_calls{}, creator_mask{},
      terminal_id{}, fixture_failure{};
  unsigned active_call{};
  bool regression{}, active_creator{}, attempt_open{}, protocol_error{},
      output_error{};
  std::string_view first_failure;
} summary;
static_assert(sizeof(Summary) <= 128);
auto healthy() -> bool {
  return summary.failures == 0 && summary.fixture_failure == 0 &&
         !summary.protocol_error && !summary.output_error && std::cout.good();
}
void check(bool yes, std::string_view why) {
  if (summary.checks >= maximum_checks ||
      summary.checks - summary.block_start >= summary.block_limit) {
    summary.protocol_error = true;
    if (summary.first_failure.empty())
      summary.first_failure = "Test check budget";
    return;
  }
  ++summary.checks;
  if (!yes) {
    ++summary.failures;
    if (summary.first_failure.empty()) summary.first_failure = why;
  }
}
struct CheckBlock {
  std::uint64_t start{summary.block_start}, limit{summary.block_limit};
  std::uint64_t begin{summary.checks};
  explicit CheckBlock(std::uint64_t maximum) {
    summary.block_start = summary.checks;
    summary.block_limit = maximum;
  }
  ~CheckBlock() {
    summary.block_start = start + (summary.checks - begin);
    summary.block_limit = limit;
  }
  CheckBlock(const CheckBlock&) = delete;
  auto operator=(const CheckBlock&) -> CheckBlock& = delete;
};
class CountedSink : public std::streambuf {
 public:
  explicit CountedSink(std::streambuf* target) : target_(target) {}

 private:
  std::streambuf* target_;
  auto overflow(int_type c) -> int_type override {
    if (traits_type::eq_int_type(c, traits_type::eof()))
      return traits_type::not_eof(c);
    if (summary.bytes >= maximum_transcript) {
      summary.output_error = true;
      return traits_type::eof();
    }
    ++summary.bytes;
    return target_->sputc(traits_type::to_char_type(c));
  }
  auto xsputn(const char* s, std::streamsize n) -> std::streamsize override {
    std::streamsize i{};
    while (i < n &&
           !traits_type::eq_int_type(overflow(s[i]), traits_type::eof()))
      ++i;
    return i;
  }
  auto sync() -> int override { return target_->pubsync(); }
};
void text(std::string_view s) {
  std::size_t encoded{2};
  for (const auto c : s)
    encoded += c < 32 || c > 126 ? 4U : c == '"' || c == '\\' ? 2U : 1U;
  if (encoded > 512) summary.protocol_error = true;
  std::cout << '"';
  for (const auto c : s) {
    if (c < 32 || c > 126) {
      summary.protocol_error = true;
      const auto u = static_cast<unsigned char>(c);
      std::cout << "\\x" << "0123456789abcdef"[u >> 4]
                << "0123456789abcdef"[u & 15];
      continue;
    }
    if (c == '"' || c == '\\') std::cout << '\\';
    std::cout << c;
  }
  std::cout << '"';
}
template <class T> void optional(const std::optional<T>& v) {
  if (v)
    std::cout << *v;
  else
    std::cout << '_';
}
void key(const std::optional<LowerCockpitTriangleKey>& v) {
  if (!v) {
    std::cout << '_';
    return;
  }
  std::cout << static_cast<unsigned>(v->buffer) << ':' << v->group << ':'
            << v->triangle;
}
void print_finding(const BoardingRouteIntermediateRootZ01Refusal& r) {
  const auto& s = r.support_self;
  const auto& w = r.world;
  std::cout << " finding=" << static_cast<unsigned>(r.condition) << ','
            << static_cast<unsigned>(r.predicate_condition) << ','
            << static_cast<unsigned>(r.stage) << ','
            << static_cast<unsigned>(r.event) << ',' << r.first << ',' << r.last
            << ',' << r.local_first << ',' << r.local_last << ',' << r.depth
            << ',';
  optional(r.phase_index);
  std::cout << ',';
  optional(r.cell);
  std::cout << " support=" << static_cast<unsigned>(s.condition) << ','
            << static_cast<unsigned>(s.predicate_condition) << ','
            << s.source_edge << ',' << s.limiting_bound.lower << ','
            << s.limiting_bound.upper << ',' << s.first << ',' << s.last << ','
            << s.local_first << ',' << s.local_last << ',' << s.depth << ',';
  optional(s.phase_index);
  std::cout << ',';
  optional(s.side);
  std::cout << ',';
  optional(s.edge);
  std::cout << ',';
  optional(s.axis);
  std::cout << ',';
  optional(s.operation);
  std::cout << ',';
  key(s.source_key);
  std::cout << ',';
  text(s.source_name);
  std::cout << ',' << s.self_pair << ',' << static_cast<unsigned>(s.self_region)
            << ',' << static_cast<unsigned>(s.self_axis) << ','
            << static_cast<unsigned>(s.self_sign) << ','
            << static_cast<unsigned>(s.self_stage) << " phase=" << s.phase.first
            << ',' << s.phase.last << ',' << s.phase.depth << ','
            << static_cast<unsigned>(s.phase.condition) << ','
            << static_cast<unsigned>(s.phase.predicate_condition) << ',';
  optional(s.phase.side);
  std::cout << ',' << s.phase.limiting_bound.lower << ','
            << s.phase.limiting_bound.upper
            << " world=" << static_cast<unsigned>(w.condition) << ',';
  optional(w.part);
  std::cout << ',';
  optional(w.source);
  std::cout << ',';
  optional(w.cell);
  std::cout << ',';
  optional(w.triangle);
  std::cout << ',';
  key(w.source_key);
  std::cout << ',';
  text(w.source_object);
  std::cout << ',' << static_cast<unsigned>(w.relation) << ',' << w.global_first
            << ',' << w.global_last << ',' << w.phase_index;
}
void record_bound(std::size_t before, std::size_t maximum) {
  check(summary.bytes >= before && summary.bytes - before <= maximum,
        "Finite complete record without truncation");
}
void skip(unsigned id, std::string_view why) {
  if (id >= 1 && id <= 41)
    summary.assessment_mask |= std::uint64_t{1} << (id - 1);
  const auto before = summary.bytes;
  std::cout << "A" << id << " SKIP calls=0 prerequisite=" << summary.terminal_id
            << " fixture=" << summary.fixture_failure << ' ';
  text(why);
  std::cout << '\n';
  record_bound(before, 4096);
}

struct Fixture {
  std::optional<NativeCraftBinding> binding;
  std::optional<OriginBoardingInitialMaterial> material;
  std::optional<OriginBoardingBootSupport> boots;
  std::optional<OriginBoardingIntermediatePauseSupport> pause;
  std::optional<OriginBoardingCheckpointMaterialExtension> extension;
  std::optional<OriginBoardingHatchSealMaterial> seal;
  std::optional<OriginBoardingSeparationRingMaterial> ring;
  auto ready() const -> bool {
    return binding && material && boots && pause && extension && seal && ring;
  }
};
template <class T>
auto retain_fixture(unsigned id, std::expected<T, std::string>&& r)
    -> std::optional<T> {
  const auto before = summary.bytes;
  std::cout << "F" << id << " created=" << r.has_value();
  if (!r) {
    if (!summary.fixture_failure) summary.fixture_failure = id;
    std::cout << " original_error=";
    text(r.error());
    std::cout << '\n';
    record_bound(before, 1024);
    return {};
  }
  std::cout << '\n';
  record_bound(before, 1024);
  return std::move(*r);
}
template <class Maker> auto make_fixture(unsigned id, const Maker& make) {
  ++summary.fixture_calls;
  check(summary.fixture_calls <= 7 && id == summary.fixture_calls,
        "Charge each immutable fixture creator before invocation");
  return retain_fixture(id, make());
}
auto fixture_once() -> Fixture {
  Fixture f;
  try {
    f.binding = make_fixture(1, [] {
      return make_native_starting_assembly_binding(
          NativeStartingAssemblySelection{});
    });
    if (f.binding)
      f.material = make_fixture(
          2, [&] { return make_origin_boarding_initial_material(*f.binding); });
    if (f.material)
      f.boots = make_fixture(
          3, [&] { return make_origin_boarding_boot_support(*f.binding); });
    if (f.boots)
      f.pause = make_fixture(4, [&] {
        return make_origin_boarding_intermediate_pause_support(*f.binding,
                                                               *f.boots);
      });
    if (f.pause)
      f.extension = make_fixture(5, [&] {
        return make_origin_boarding_checkpoint_material_extension_boundary03(
            *f.binding, *f.material);
      });
    if (f.extension)
      f.seal = make_fixture(6, [&] {
        return make_origin_boarding_hatch_seal_material(*f.binding,
                                                        *f.material);
      });
    if (f.seal)
      f.ring = make_fixture(7, [&] {
        return make_origin_boarding_separation_ring_material(*f.binding,
                                                             *f.material);
      });
  } catch (const std::exception& e) {
    if (!summary.fixture_failure)
      summary.fixture_failure = summary.fixture_calls;
    const auto before = summary.bytes;
    std::cout << "F" << summary.fixture_failure << " original_exception=";
    text(e.what());
    std::cout << '\n';
    record_bound(before, 1024);
  }
  for (unsigned i = summary.fixture_calls + 1; i <= 7; ++i) {
    const auto before = summary.bytes;
    std::cout << "F" << i
              << " SKIP calls=0 prerequisite=" << summary.fixture_failure
              << '\n';
    record_bound(before, 1024);
  }
  return f;
}
void startup_inventory(const Fixture& f) {
  const auto before = summary.bytes;
  std::cout << "STARTUP one_fixture=" << f.ready()
            << " named_package_ceiling=33951744 native_capacity=UNMEASURED"
            << " boot_allocation=UNMEASURED process_peak=UNKNOWN";
  if (f.material) {
    const auto* s = f.material->summary();
    check(s && s->source_bytes <= kBoardingInitialMaterialMaximumSourceBytes,
          "Initial material selected source ceiling");
    if (s)
      std::cout << " material=" << s->source_bytes << ','
                << s->effective_objects;
  }
  if (f.boots)
    check(f.boots->selected_partitions().size() == 10,
          "Ten genuine boot partitions");
  if (f.pause) {
    const auto* s = f.pause->summary();
    check(s && s->actual_source_bytes == 4096,
          "Actual pause arena including control block");
  }
  if (f.extension) {
    const auto* s = f.extension->summary();
    check(s && s->source_bytes <=
                   kBoardingCheckpointMaterialExtensionMaximumSourceBytes,
          "Distinct extension source ceiling");
    if (s) std::cout << " extension=" << s->source_bytes;
  }
  if (f.seal) {
    const auto* s = f.seal->summary();
    check(s && s->source_bytes <= kBoardingHatchSealMaterialMaximumSourceBytes,
          "Distinct seal source ceiling");
    if (s) std::cout << " seal=" << s->source_bytes;
  }
  if (f.ring) {
    const auto* s = f.ring->summary();
    check(s && s->source_bytes <=
                   kBoardingSeparationRingMaterialMaximumSourceBytes,
          "Distinct ring source ceiling");
    if (s) std::cout << " ring=" << s->source_bytes;
  }
  std::cout << '\n';
  record_bound(before, 1024);
  // No vector reserve request or JSON input byte limit is a retained heap
  // bound. Full native/boot/cold owner inventory belongs to the separate
  // admission gate.
}
auto creator(unsigned id, const Fixture& f, ConstructorLimits limits = {})
    -> Provider {
  CheckBlock block(256);
  const auto bit = 1U << (id - 1);
  check(id >= 1 && id <= 4 && !(summary.creator_mask & bit),
        "Four literal creator bindings once");
  summary.creator_mask |= bit;
  if (!f.ready()) {
    const auto before = summary.bytes;
    std::cout << "C" << id
              << " SKIP calls=0 fixture=" << summary.fixture_failure << '\n';
    record_bound(before, 4096);
    return {};
  }
  ++summary.creators;
  ConstructorEvidence evidence;
  const auto before = summary.bytes;
  summary.active_call = id;
  summary.active_creator = true;
  auto r =
      id == 4 ? make_origin_boarding_route_intermediate_root_z01(
                    *f.binding, *f.boots, *f.pause, *f.material, *f.extension,
                    *f.seal, *f.ring)
              : Access::make(*f.binding, *f.boots, *f.pause, *f.material,
                             *f.extension, *f.seal, *f.ring, limits, &evidence);
  summary.active_call = 0;
  if (summary.regression) {
    check(r.has_value() == (id == 4), "Original creator variants remain three "
                                      "cap refusals then genuine authority");
    if (id != 4) {
      check(evidence.condition == Condition::source_capacity &&
                evidence.required_source_bytes == 1024 &&
                evidence.actual_source_bytes == 0 &&
                evidence.base_guards == (id == 3 ? 7U : 0U) &&
                evidence.control_guards == 0,
            "Original bounded creator source and guard work remain exact");
      if (!r)
        check(r.error() == (id == 1   ? "RootZ01 source allocation capacity"
                            : id == 2 ? "RootZ01 base guard capacity"
                                      : "RootZ01 control guard capacity"),
              "Original lowered creator errors remain exact");
    }
  }
  std::cout << "C" << id << " created=" << r.has_value();
  if (id != 4) {
    check(evidence.base_guards <= limits.base_guards &&
              evidence.control_guards <= limits.control_guards,
          "Constructor guards charge before access");
    check(!evidence.authenticated && !r.has_value(),
          "Exhausted creator cap cannot mint authority");
    std::cout << " condition=" << static_cast<unsigned>(evidence.condition)
              << " bytes=" << evidence.required_source_bytes << ','
              << evidence.actual_source_bytes
              << " guards=" << evidence.base_guards << ','
              << evidence.control_guards;
  }
  if (!r) {
    std::cout << " original_error=";
    text(r.error());
    std::cout << '\n';
    record_bound(before, 4096);
    return {};
  }
  const auto* s = r->summary();
  check(s && s->authenticated && s->same_base && s->controls_authenticated,
        "Creator authenticates source and recipe, not a route");
  if (s) {
    if (summary.regression)
      check(s->required_source_bytes == 1024 && s->actual_source_bytes == 1024,
            "Original genuine creator retains its fixed source arena");
    check(s->actual_source_bytes <= 1024 && s->base_guards <= 64 &&
              s->control_guards <= 64,
          "Creator immutable storage and shared guard ceilings");
    for (std::size_t i = 0; i < 7; ++i)
      check(s->input_identity_checked[i] && s->input_identity_valid[i],
            "All genuine input identities");
    for (std::size_t i = 0; i < 5; ++i)
      check(s->control_identity_checked[i] && s->control_identity_valid[i],
            "All exact control packets");
    std::cout << " bytes=" << s->required_source_bytes << ','
              << s->actual_source_bytes;
  }
  std::cout << '\n';
  record_bound(before, 4096);
  return std::move(*r);
}

// These frozen term lists are independent checks, never caller geometry inputs.
enum class LiteralPoint { c, e, u, p, h, i, star };
auto terms(LiteralPoint p, std::size_t axis) -> BoardingRoutePhaseConstant {
  if (p == LiteralPoint::c || p == LiteralPoint::e) {
    if (axis == 0)
      return p == LiteralPoint::c
                 ? BoardingRoutePhaseConstant{{.16, .16, 0}, 2}
                 : BoardingRoutePhaseConstant{{.16, .16, -.0075}, 3};
    if (axis == 1)
      return p == LiteralPoint::c
                 ? BoardingRoutePhaseConstant{{.847, .012, 0}, 2}
                 : BoardingRoutePhaseConstant{{.847, .012, -.359}, 3};
    return {{-.55, -.17, +0.0}, 2};
  }
  if (axis == 0) {
    if (p == LiteralPoint::star) return {{.16, .14, 0}, 2};
    if (p == LiteralPoint::u || p == LiteralPoint::p)
      return {{.16, -.14, 0}, 2};
    return {{.16, -.14, .16}, 3};
  }
  if (axis == 1)
    return {{double(p == LiteralPoint::star ? -160000
                    : p == LiteralPoint::i  ? -230000
                                            : -100000) *
                 1e-6,
             0, 0},
            1};
  return {{p == LiteralPoint::u                              ? -.5
           : p == LiteralPoint::p || p == LiteralPoint::star ? -.8
                                                             : -1.14,
           0, 0},
          1};
}
auto sum(LiteralPoint p, std::size_t axis) -> long double {
  const auto c = terms(p, axis);
  long double v{};
  for (std::size_t i = 0; i < c.count; ++i)
    v += static_cast<long double>(c.terms[i]);
  return v;
}
auto root_point(std::size_t phase, bool last) -> LiteralPoint {
  return phase == 0 || (phase == 1 && !last) ? LiteralPoint::c
                                             : LiteralPoint::e;
}
auto port_point(std::size_t phase, bool last) -> LiteralPoint {
  if (phase == 0) return last ? LiteralPoint::p : LiteralPoint::u;
  if (phase == 1) return last ? LiteralPoint::h : LiteralPoint::p;
  if (phase == 2) return last ? LiteralPoint::i : LiteralPoint::h;
  return LiteralPoint::i;
}
constexpr std::array<double, 6> cuts{0, .125, .25, .5, .75, 1};
constexpr std::array<std::array<std::uint8_t, 2>, 14> connected_pairs{
    {{0, 1},
     {0, 3},
     {0, 9},
     {1, 2},
     {1, 6},
     {1, 12},
     {3, 4},
     {4, 5},
     {6, 7},
     {7, 8},
     {9, 10},
     {10, 11},
     {12, 13},
     {13, 14}}};
auto local(std::size_t phase, double g) -> double {
  return phase < 2 ? 8 * g - static_cast<double>(phase)
                   : 4 * g - static_cast<double>(phase - 1);
}
auto clock(double g) -> double {
  return g <= .25   ? 96 * g
         : g <= .75 ? 24 + 48 * (g - .25)
                    : 48 + 8 * (g - .75);
}
void literal_protocol() {
  check(boarding_route_intermediate_root_z01_required_output_bytes() ==
            2 * sizeof(Expected),
        "Full current and pending fixed Expected output");
  Provider empty;
  check(!Access::valid(empty) && !Access::data(empty) && !empty.summary(),
        "Empty means no source authority");
  for (std::size_t phase = 0; phase < 5; ++phase) {
    const auto r = detail::boarding_route_intermediate_root_z01_controls(phase);
    check(r.has_value(), "Five literal selected packets");
    if (!r) continue;
    for (std::size_t end = 0; end < 2; ++end) {
      for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto a = terms(root_point(phase, end != 0), axis);
        const auto b = terms(port_point(phase, end != 0), axis);
        const auto s = terms(LiteralPoint::star, axis);
        check(r->root[end].coordinates[axis].count == a.count &&
                  r->root[end].coordinates[axis].terms == a.terms,
              "Exact unchanged X/Y and selected C-Z term identity");
        check(r->feet[0].sole[end].coordinates[axis].count == b.count &&
                  r->feet[0].sole[end].coordinates[axis].terms == b.terms,
              "Original port target term identity");
        check(r->feet[1].sole[end].coordinates[axis].count == s.count &&
                  r->feet[1].sole[end].coordinates[axis].terms == s.terms,
              "Original star source target term identity");
      }
      check(r->root_yaw_half[end] ==
                (phase == 0 || (phase == 1 && end == 0) ? .25 : .125),
            "Original yaw half");
      check(r->torso_lean_half[end] ==
                (phase == 0 || (phase == 1 && end == 0)   ? 0.
                 : phase == 4 || (phase == 3 && end == 1) ? -.30
                                                          : -.25),
            "Original torso half");
      check(r->feet[0].yaw_half[end] == 0 && r->feet[1].yaw_half[end] == 0,
            "Unchanged sole yaw");
      check(r->port_reaction_fraction[end] ==
                (phase == 4 || (phase == 3 && end == 1) ? .0625 : 0.),
            "Fixed load packet");
    }
    check(r->seconds_per_parameter == (phase == 4 ? 2 : 12) &&
              r->feet[0].swing_height_metres == (phase == 1 ? .010 : 0.) &&
              r->feet[1].swing_height_metres == 0,
          "Original clock and finite hump");
    check(detail::boarding_route_intermediate_root_z01_local(
              phase, cuts[phase]) == 0 &&
              detail::boarding_route_intermediate_root_z01_local(
                  phase, cuts[phase + 1]) == 1,
          "Global subrange to physical local parameter");
    const auto old =
        detail::boarding_route_intermediate_support_self01_controls(phase);
    if (phase > 0 && old)
      check(old->root[1].coordinates[2].count == 3 &&
                old->root[1].coordinates[2].terms[2] == .19,
            "Original selected E packet remains original");
  }
  for (std::size_t i = 0; i < cuts.size(); ++i) {
    const auto p =
        detail::boarding_route_intermediate_root_z01_phase(cuts[i], cuts[i]);
    check(p && *p == (i == 0 ? 0 : i - 1),
          "Preceding phase owns exact point join");
    check(detail::boarding_route_intermediate_root_z01_clock(cuts[i]) ==
              clock(cuts[i]),
          "Original physical elapsed time");
  }
  check(!detail::boarding_route_intermediate_root_z01_controls(5),
        "No sixth control packet");
}

struct Jet {
  long double value{}, first{}, second{};
};
auto operator+(Jet a, Jet b) -> Jet {
  return {a.value + b.value, a.first + b.first, a.second + b.second};
}
auto operator-(Jet a, Jet b) -> Jet {
  return {a.value - b.value, a.first - b.first, a.second - b.second};
}
auto operator*(Jet a, Jet b) -> Jet {
  return {a.value * b.value, a.first * b.value + a.value * b.first,
          a.second * b.value + 2 * a.first * b.first + a.value * b.second};
}
auto scalar(long double v) -> Jet {
  return {v, 0, 0};
}
auto within(long double v, double lower, double upper) -> bool {
  // Corroboration only: this does not issue continuous contact or collision
  // proof.
  constexpr long double tolerance{2e-12L};
  return std::isfinite(v) && std::isfinite(lower) && std::isfinite(upper) &&
         lower <= upper && v >= static_cast<long double>(lower) - tolerance &&
         v <= static_cast<long double>(upper) + tolerance;
}
auto component(RigidVector3 v, std::size_t axis) -> double {
  return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}
void jet_check(const BoardingPlantedBodyPointEvidence& p, std::size_t axis,
               Jet j) {
  check(within(j.value, component(p.value.lower, axis),
               component(p.value.upper, axis)),
        "Independent value corroborates enclosure");
  check(within(j.first, component(p.derivatives.velocity.lower, axis),
               component(p.derivatives.velocity.upper, axis)),
        "Independent signed physical velocity");
  check(within(j.second, component(p.derivatives.acceleration.lower, axis),
               component(p.derivatives.acceleration.upper, axis)),
        "Independent physical acceleration");
}
void independent_sample(const Cell& c, double u, bool reverse) {
  const auto phase = c.support_self.phase_index;
  const Jet t{u, (reverse ? -1.L : 1.L) / (phase == 4 ? 2.L : 12.L), 0};
  const auto one = scalar(1),
             s = t * t * t * (scalar(10) - scalar(15) * t + scalar(6) * t * t),
             h = scalar(64) * t * t * t * (one - t) * (one - t) * (one - t);
  for (std::size_t axis = 0; axis < 3; ++axis) {
    const auto a = scalar(sum(root_point(phase, false), axis)),
               b = scalar(sum(root_point(phase, true), axis));
    jet_check(c.support_self.phase.points[0], axis, a + (b - a) * s);
    const auto p = scalar(sum(port_point(phase, false), axis)),
               q = scalar(sum(port_point(phase, true), axis));
    auto port = p + (q - p) * s;
    if (axis == 1)
      port = port + scalar(.05) + (phase == 1 ? scalar(.010) * h : scalar(0));
    jet_check(c.support_self.phase.points[7], axis, port);
    jet_check(c.support_self.phase.points[14], axis,
              scalar(sum(LiteralPoint::star, axis) +
                     (axis == 1 ? static_cast<long double>(.05) : 0.L)));
  }
  const auto reaction = phase < 3    ? scalar(0)
                        : phase == 4 ? scalar(.0625)
                                     : scalar(.0625) * s;
  check(within(reaction.value,
               c.support_self.phase.port_reaction_fraction.lower,
               c.support_self.phase.port_reaction_fraction.upper),
        "Independent fixed load schedule");
}
void counters(const Diagnostic& d, const Limits& l) {
  const auto& a = d.work.support_self;
  const auto& b = l.support_self;
#define SUPPORT_CAP(name) check(a.name <= b.name, "Shared support/SELF " #name)
  SUPPORT_CAP(source_guards);
  SUPPORT_CAP(projection_guards);
  SUPPORT_CAP(definition_guards);
  SUPPORT_CAP(pressure_candidates);
  SUPPORT_CAP(disk_edges);
  SUPPORT_CAP(sole_extrema);
  SUPPORT_CAP(source_coordinates);
  SUPPORT_CAP(intersection_operations);
  SUPPORT_CAP(midpoint_operations);
  SUPPORT_CAP(allocation_operations);
  SUPPORT_CAP(reaction_operations);
  SUPPORT_CAP(division_quotients);
  SUPPORT_CAP(division_folds);
  SUPPORT_CAP(upper_geometry_edges);
  SUPPORT_CAP(endpoint_operations);
  SUPPORT_CAP(event_guards);
  SUPPORT_CAP(self_body_guards);
  SUPPORT_CAP(self_pairs);
  SUPPORT_CAP(self_axes);
  SUPPORT_CAP(self_signed_trials);
  SUPPORT_CAP(self_owners);
  SUPPORT_CAP(self_hip_complements);
#undef SUPPORT_CAP
#define PHASE_CAP(name)                                                        \
  check(a.phase.name <= b.phase.name, "Only shared phase " #name)
  PHASE_CAP(graphs);
  PHASE_CAP(legs);
  PHASE_CAP(bodies);
  PHASE_CAP(sectors);
  PHASE_CAP(timing);
#undef PHASE_CAP
  const auto& w = d.work.world;
  const auto& x = l.world;
#define WORLD_CAP(name) check(w.name <= x.name, "Full same-cover WORLD " #name)
  WORLD_CAP(roster_entries);
  WORLD_CAP(effective_sources);
  WORLD_CAP(union_envelope_pairs);
  WORLD_CAP(union_proxy_preparations);
  WORLD_CAP(domain_union_checks);
  WORLD_CAP(domain_cell_checks);
  WORLD_CAP(proxy_preparations);
  WORLD_CAP(refined_envelope_pairs);
  WORLD_CAP(material_triangle_visits);
  WORLD_CAP(halo_metadata_visits);
  WORLD_CAP(halo_triangle_visits);
  WORLD_CAP(triangle_union_pairs);
  WORLD_CAP(refined_triangle_pairs);
  WORLD_CAP(base_enclosure_relations);
  WORLD_CAP(refined_enclosure_relations);
  WORLD_CAP(direction_entries_prepared);
  WORLD_CAP(sole_vertex_guards);
#undef WORLD_CAP
  check(w.axes_examined <= x.axes, "World axes ceiling");
  check(d.work.boundary.witness_attempts <= x.boundary_witness_attempts &&
            d.work.boundary.signed_support_calls <=
                x.boundary_signed_support_calls &&
            d.work.boundary.width_attempts <= x.boundary_width_attempts,
        "Finite WORLD boundary work");
  check(d.examined_nodes <= b.phase.nodes && d.maximum_depth <= b.phase.depth &&
            d.cells.size() <= b.phase.leaves,
        "One global cover, no phase budget resets");
}
void cell_check(const Diagnostic& d, std::size_t index) {
  CheckBlock block(2048);
  const auto& c = d.cells[index];
  const auto reverse = d.reverse;
  const auto& s = c.support_self;
  check(s.phase_index < 5 && s.global_first <= s.global_last,
        "Valid current compound interval");
  if (s.phase_index >= 5) return;
  check(s.global_first >= cuts[s.phase_index] &&
            s.global_last <= cuts[s.phase_index + 1],
        "Same phase interval");
  if (c.complete)
    check(c.fresh_control_identity && c.contact_complete && c.load_complete &&
              c.self_complete && c.world.complete && s.complete &&
              s.phase.complete && s.nominal_support_complete && s.self_complete,
          "Every accepted cell earns all grouped obligations");
  check(s.examined_pairs <= 105 && s.accepted_pairs <= s.examined_pairs,
        "Finite actual SELF cursor");
  check((s.self_body_evaluated &
         ~kBoardingRouteIntermediateSupportSelf01BodyMask) == 0 &&
            (s.owner_attempted_mask & 0xc000U) == 0,
        "No invented body or connected-owner evaluated bits");
  if (s.self_body_complete)
    check(s.self_body_evaluated ==
              kBoardingRouteIntermediateSupportSelf01BodyMask,
          "Complete SELF body metadata earns all original 63 bits");
  if (s.self_complete)
    check(s.examined_pairs == 105 && s.accepted_pairs == 105,
          "All original SELF pairs");
  if (s.self_complete)
    check(s.self_body_complete && s.owner_attempted_mask == 0x3fff,
          "Accepted SELF retains body identity and all fourteen attempted "
          "original owners");
  for (std::size_t region = 0; region < s.owners.size(); ++region) {
    const auto& owner = s.owners[region];
    const bool attempted = (s.owner_attempted_mask & (1U << region)) != 0;
    check(static_cast<unsigned>(owner.certificate) <= 5,
          "Only original finite owner certificate families");
    if (owner.certified)
      check(attempted && owner.arithmetic_supported &&
                owner.structural_identity &&
                owner.certificate !=
                    BoardingSourceEndpointSelfCertificate::none,
            "Certified owner actually attempted its authentic finite identity");
    if (!attempted)
      check(!owner.certified && owner.certificate ==
                                    BoardingSourceEndpointSelfCertificate::none,
            "Unvisited original owner cannot acquire a certificate");
  }
  std::uint32_t accepted{};
  for (std::size_t i = 1; i < s.self_certificate_counts.size(); ++i)
    accepted += s.self_certificate_counts[i];
  check(s.self_certificate_counts[0] == 0 && accepted == s.accepted_pairs,
        "No invented SELF certificate family");
  std::uint16_t positive{};
  std::array<std::uint16_t, 7> families{};
  std::size_t first_part{}, second_part{1};
  for (std::size_t i = 0; i < 105; ++i) {
    const auto family = static_cast<unsigned>(s.self_certificates[i]);
    check(family <= 6, "Original SELF family or unvisited sentinel");
    if (family != 0 && family <= 6) {
      ++positive;
      ++families[family];
    }
    const auto axis = s.self_axes[i];
    std::size_t region = connected_pairs.size();
    for (std::size_t j = 0; j < connected_pairs.size(); ++j)
      if (connected_pairs[j][0] == first_part &&
          connected_pairs[j][1] == second_part)
        region = j;
    if (family == 0)
      check(axis == 255,
            "Uncertified pair retains the unselected-axis sentinel");
    else {
      check(i < s.examined_pairs,
            "Earned pair is inside the actual examined cursor");
      if (family == 1) {
        const auto capsule = [](std::size_t p) {
          return p == 3 || p == 4 || p == 6 || p == 7 || p == 9 || p == 10 ||
                 p == 12 || p == 13;
        };
        const auto proposals = 4U + (capsule(first_part) ? 2U : 3U) +
                               (capsule(second_part) ? 2U : 3U);
        check((axis & 0xe0U) == 0 && (axis & 15U) < proposals,
              "Actual convex axis/sign has only original registered bits");
      } else {
        check(axis == 255 && region < 14,
              "Original owner certificate cannot invent an axis or "
              "nonconnected pair");
        if (region < 14 && family < 6)
          check(s.owners[region].certified &&
                    static_cast<unsigned>(s.owners[region].certificate) ==
                        family,
                "Selected family names its actual retained original owner");
        if (region < 14 && family == 6) {
          const auto side = second_part >= 9 ? 1U : 0U;
          const auto& h = s.hip_complements[side];
          check(!s.owners[region].certified && h.attempted && h.certified &&
                    h.side == side && h.region == region && h.pair == i,
                "Complement retains authentic failed owner and exact original "
                "pair");
        }
      }
    }
    if (i >= s.examined_pairs)
      check(family == 0 && axis == 255,
            "Unvisited pair has neither certificate nor selected axis");
    if (++second_part == 15) {
      ++first_part;
      second_part = first_part + 1;
    }
  }
  check(positive == s.accepted_pairs,
        "Every positive SELF code is actually earned");
  for (std::size_t i = 1; i < families.size(); ++i)
    check(families[i] == s.self_certificate_counts[i],
          "Each actual SELF family histogram matches its literal codes");
  if (d.complete) {
    check(c.complete, "Complete route retains no provisional cover cell");
    const auto begin = reverse ? s.global_last : s.global_first;
    const auto end = reverse ? s.global_first : s.global_last;
    if (index == 0)
      check(begin == d.requested_first, "Exact requested cover start");
    else {
      const auto& previous = d.cells[index - 1].support_self;
      check(begin == (reverse ? previous.global_first : previous.global_last),
            "Exact contiguous traversal without a gap or overlapping interior");
    }
    if (index + 1 == d.cells.size())
      check(end == d.requested_last, "Exact requested cover end");
    check(
        d.requested_first == d.requested_last
            ? d.cells.size() == 1 && begin == end
            : s.global_first < s.global_last,
        "Point cover canonical; positive-width cover has no point substitutes");
  }
  for (std::size_t i = 0; i < kBoardingBodyPartCount; ++i) {
    if (c.world.complete)
      check(c.world.body_identity[i] && c.world.domain_complete[i] &&
                c.world.material_complete[i] && c.world.halo_complete[i],
            "Current finite WORLD part is fully closed");
    if (c.world.material_complete[i])
      check(c.world.material_sources_closed[i] == 1751,
            "Complete effective material source closure");
    check(c.world.halo_triangles_closed[i] <= 8100,
          "HALO visits not invented for envelope-closed groups");
  }
  for (std::size_t i = 0; i < 3; ++i) {
    check(!s.endpoint_earned[i] ||
              (s.endpoint_scope[i] && s.endpoint_evaluated[i]),
          "Earned prefix endpoint retains its own scoped evaluation");
    check(!s.endpoint_evaluated[i] || s.endpoint_scope[i],
          "Unscoped prefix endpoint remains unevaluated");
  }
  if (s.nominal_support_complete)
    check(s.star_everywhere_positive && s.star_support && s.nominal_equilibrium,
          "Star support and genuine nominal equilibrium throughout");
  if (s.port_positive_interior)
    check(
        s.port_contact_geometry && c.contact_complete &&
            s.pressure_evaluated[0] &&
            s.port_force_state !=
                BoardingRouteIntermediateSupportSelf01PortForceState::zero_only,
        "Strict positive-load interior requires genuine finite acquired "
        "contact");
  if (c.complete &&
      (s.phase_index == 4 || (s.phase_index == 3 && s.global_last > .5)))
    check(s.port_positive_interior && c.contact_complete && c.load_complete,
          "Selected positive-load interval cannot omit previously acquired "
          "contact");
  if (s.nominal_support_complete && s.phase_index < 3)
    check(s.port_share.lower == 0 && s.port_share.upper == 0 &&
              !s.port_positive_interior,
          "Release and swing remain unloaded");
  if (c.contact_complete && s.phase_index == 4)
    check(c.event == Event::supported_pause && s.port_share.lower <= .0625 &&
              s.port_share.upper >= .0625,
          "Pause earns actual finite support");
  const auto lo = local(s.phase_index, s.global_first),
             hi = local(s.phase_index, s.global_last);
  if (s.phase.complete) {
    independent_sample(c, lo, reverse);
    independent_sample(c, lo + (hi - lo) * .5, reverse);
    independent_sample(c, hi, reverse);
  }
}
auto inspect(const Diagnostic& d, const Provider& p, double first, double last,
             const Limits& l) -> bool {
  if (d.cells.size() > 1024 || d.cells.size() > l.support_self.phase.leaves) {
    check(false, "Reject oversized cover before any traversal or size product");
    summary.protocol_error = true;
    return false;
  }
  check(Access::data(d.source) == Access::data(p),
        "Report retains genuine source ownership");
  check(d.requested_first == first && d.requested_last == last &&
            d.reverse == (first > last),
        "Canonical range and direction");
  counters(d, l);
  if (d.output_capacity_bytes >
      std::min(l.output_bytes, l.support_self.phase.output_bytes))
    check(d.state == State::capacity &&
              d.stop_condition == Condition::output_capacity &&
              d.stop_stage == Stage::preflight && d.cells.empty(),
          "Recorded required fixed headers do not falsely claim an allocated "
          "fit");
  if (d.source_enrolled)
    check(std::all_of(d.source_evaluated.begin(), d.source_evaluated.end(),
                      [](bool b) { return b; }),
          "All 67 authentic source roles before body work");
  for (std::size_t part = 0; part < 15; ++part) {
    const auto& w = d.world_parts[part];
    if (d.kinematic_complete)
      check(static_cast<std::size_t>(d.parts[part].id) == part,
            "Earned kinematic record retains original literal body part "
            "identity");
    if (w.original_world_identity || w.domain_complete || w.complete)
      check(static_cast<std::size_t>(w.id) == part,
            "Earned WORLD record retains original literal part identity");
    check(!w.complete || (w.original_world_identity && w.domain_complete &&
                          w.material_sources_closed == 1751 &&
                          w.material_cells_closed == 1751 * d.cells.size() &&
                          w.halo_triangles_closed == 8100),
          "Complete WORLD part closes every effective source/cell and HALO "
          "traversal");
    if (d.world_complete)
      check(w.complete,
            "Complete WORLD means all fifteen original parts complete");
  }
  for (std::size_t endpoint = 0; endpoint < 3; ++endpoint) {
    bool scoped{}, earned{};
    for (const auto& c : d.cells) {
      scoped = scoped || c.support_self.endpoint_scope[endpoint];
      earned = earned || c.support_self.endpoint_earned[endpoint];
    }
    check(
        d.prefix_endpoint_scope[endpoint] == scoped &&
            d.prefix_endpoint_earned[endpoint] == earned,
        "Global prefix scope/earned OR only authentic retained current cells");
  }
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    check(r.condition != Condition::none && r.stage != Stage::not_run &&
              r.first <= r.last && (!r.phase_index || *r.phase_index < 5),
          "Exact first finding retains real phase/interval");
    if (r.world.condition ==
        BoardingRouteCheckpointWorldCondition::missing_relation)
      check(r.world.source.has_value() && !d.world_complete && !d.complete,
            "Missing full-object authority remains unresolved, not a top-face "
            "waiver");
  }
  for (std::size_t i = 0; i < d.cells.size(); ++i)
    cell_check(d, i);
  if (d.complete || d.route_qualified || d.state == State::accepted) {
    check(d.complete && d.route_qualified && d.state == State::accepted &&
              d.source_enrolled && d.kinematic_complete && d.contact_complete &&
              d.nominal_support_complete && d.self_complete &&
              d.world_complete && !d.first_refusal && !d.cells.empty(),
          "Partial/kinematic-only success is never route acceptance");
    for (std::size_t i = 0; i < 4; ++i) {
      const bool crossed = std::min(first, last) < cuts[i + 1] &&
                           std::max(first, last) > cuts[i + 1];
      check(!crossed || (d.join_expression_identity[i] && d.qualified_joins[i]),
            "Derived-body joins need both supported sides");
    }
  }
  return d.complete && d.route_qualified && d.state == State::accepted &&
         healthy();
}
enum class Kind {
  empty,
  range,
  raised,
  one_header,
  two_headers,
  source_zero_header,
  moved,
  positive,
  source_zero,
  node_zero,
  leaf_zero,
  depth_zero,
  graph_zero,
  projection_zero,
  self_zero,
  world_zero,
  unsupported
};
auto named_capacity(const Diagnostic& d, Kind kind) -> bool {
  if (d.state != State::capacity || d.complete || d.route_qualified)
    return false;
  const auto& w = d.work.support_self;
  const auto* r = d.first_refusal ? &*d.first_refusal : nullptr;
  switch (kind) {
    case Kind::source_zero:
      return d.stop_condition == Condition::source_capacity &&
             d.stop_stage == Stage::source && w.source_guards == 0 &&
             std::none_of(d.source_evaluated.begin(), d.source_evaluated.end(),
                          [](bool b) { return b; });
    case Kind::node_zero:
      return d.stop_condition == Condition::node_capacity &&
             d.stop_stage == Stage::coverage && d.examined_nodes == 0;
    case Kind::leaf_zero:
      return d.stop_condition == Condition::leaf_capacity &&
             d.stop_stage == Stage::coverage && d.cells.empty();
    case Kind::depth_zero:
      return d.stop_condition == Condition::depth_capacity &&
             d.stop_stage == Stage::coverage && d.examined_nodes == 1 &&
             d.mandatory_splits == 0 && d.maximum_depth == 0;
    case Kind::graph_zero:
      return d.stop_condition == Condition::work_capacity &&
             d.stop_stage == Stage::phase && w.phase.graphs == 0 && r &&
             r->support_self.phase.condition ==
                 BoardingRouteFootPhaseCondition::graph_capacity;
    case Kind::projection_zero:
      return d.stop_condition == Condition::work_capacity &&
             d.stop_stage == Stage::phase && w.projection_guards == 0 && r &&
             r->support_self.condition ==
                 BoardingRouteIntermediateSupportSelf01Condition::
                     projection_capacity;
    case Kind::self_zero:
      return d.stop_condition == Condition::work_capacity &&
             d.stop_stage == Stage::self && w.self_pairs == 0 && r &&
             r->support_self.condition ==
                 BoardingRouteIntermediateSupportSelf01Condition::
                     self_pair_capacity;
    case Kind::world_zero:
      return d.stop_condition == Condition::work_capacity &&
             d.stop_stage == Stage::world && d.work.world.roster_entries == 0 &&
             r &&
             r->world.condition ==
                 BoardingRouteCheckpointWorldCondition::roster_capacity;
    default: return false;
  }
}
void original_whole_regression(const Expected& result) {
  // Paired original FIRST stdout SHA256 89e468902e07a4c22b46e375bbf85d18
  // 738180e02041b5219915c25b79c33704. This is a terminal SELF observation,
  // not a positive SELF, WORLD, route or boarding qualification.
  check(result.has_value(),
        "Original A16 retains its structured Expected variant");
  if (!result) return;
  const auto& d = *result;
  check(
      d.requested_first == 0 && d.requested_last == 1 && !d.reverse &&
          d.state == State::capacity &&
          d.stop_condition == Condition::depth_capacity &&
          d.stop_stage == Stage::self && !d.complete && !d.route_qualified &&
          d.cells.size() == 21 && d.examined_nodes == 50 &&
          d.maximum_depth == 10,
      "Original whole range, depth stop and retained cover work remain exact");
  check(d.first_refusal.has_value(),
        "Original first terminal predicate remains owned");
  if (!d.first_refusal) return;
  const auto& r = *d.first_refusal;
  check(r.condition == Condition::self_refused &&
            r.predicate_condition == Condition::self_refused &&
            r.stage == Stage::self && r.event == Event::unloaded_motion &&
            r.first == 0.142578125 && r.last == 0.1435546875 &&
            r.local_first == 0.140625 && r.local_last == 0.1484375 &&
            r.depth == 10 && r.phase_index == 1 && !r.cell,
        "Original first terminal SELF interval/event/phase is separate from "
        "later depth stop");
  const auto& s = r.support_self;
  check(s.condition == BoardingRouteIntermediateSupportSelf01Condition::
                           unresolved_self_pair &&
            s.predicate_condition ==
                BoardingRouteIntermediateSupportSelf01Condition::
                    unresolved_self_pair &&
            !s.source_edge && s.limiting_bound.lower == 0 &&
            s.limiting_bound.upper == 0 && s.first == 0.142578125 &&
            s.last == 0.1435546875 && s.local_first == 0.140625 &&
            s.local_last == 0.1484375 && s.depth == 10 && s.phase_index == 1 &&
            !s.side && !s.edge && !s.axis && !s.operation && !s.source_key &&
            s.source_name.empty() && s.self_pair == 2 && s.self_region == 1 &&
            s.self_axis == 8 && s.self_sign == 1 &&
            s.self_stage ==
                BoardingRouteIntermediateSupportSelf01SelfStage::separation,
        "Original nested pair/region/axis/sign/separation finding and absent "
        "source fields remain exact");
  check(s.phase.first == 0 && s.phase.last == 0 && s.phase.depth == 0 &&
            s.phase.condition == BoardingRouteFootPhaseCondition::none &&
            s.phase.predicate_condition ==
                BoardingRouteFootPhaseCondition::none &&
            !s.phase.side && s.phase.limiting_bound.lower == 0 &&
            s.phase.limiting_bound.upper == 0,
        "Original unevaluated nested phase finding stays unevaluated");
  const auto& w = r.world;
  check(w.condition == BoardingRouteCheckpointWorldCondition::none && !w.part &&
            !w.source && !w.cell && !w.triangle && !w.source_key &&
            w.source_object.empty() &&
            w.relation == BoardingInitialMaterialRelation::unknown &&
            w.global_first == 0 && w.global_last == 0 && w.phase_index == 0,
        "Original unevaluated WORLD finding cannot be replaced by invented "
        "authority");
}
void assessment(unsigned id, const Provider& p, double first, double last,
                Kind kind) {
  CheckBlock block(1024);
  const auto bit = std::uint64_t{1} << (id - 1);
  check(id >= 1 && id <= 41 && !(summary.assessment_mask & bit),
        "41 literal purposes bound once");
  summary.assessment_mask |= bit;
  if (id >= 16 && !healthy()) {
    summary.attempt_open = false;
    if (!summary.terminal_id) summary.terminal_id = id;
    skip(id, "Prior evidence/protocol/output/restoration failure closes "
             "genuine route calls");
    return;
  }
  if ((kind == Kind::empty || kind == Kind::moved || kind == Kind::one_header ||
       kind == Kind::two_headers || kind == Kind::source_zero_header) &&
      !detail::boarding_route_foot_phase_environment()) {
    skip(id, "Host original arithmetic environment unsupported");
    return;
  }
  if ((id >= 16 && !Access::valid(p)) ||
      (id == 15 && !Access::valid(p) && !summary.creators)) {
    skip(id, "Genuine creator unavailable");
    return;
  }
  if (id >= 17 && !summary.attempt_open) {
    skip(id, "Selected route attempt closed");
    return;
  }
  Limits l;
  switch (kind) {
    case Kind::raised: l.support_self.phase.nodes = 2048; break;
    case Kind::one_header:
      l.output_bytes = l.support_self.phase.output_bytes = sizeof(Expected) - 1;
      break;
    case Kind::two_headers:
      l.output_bytes = l.support_self.phase.output_bytes =
          2 * sizeof(Expected) - 1;
      break;
    case Kind::source_zero_header:
      l.output_bytes = l.support_self.phase.output_bytes = 2 * sizeof(Expected);
      l.support_self.source_guards = 0;
      break;
    case Kind::source_zero: l.support_self.source_guards = 0; break;
    case Kind::node_zero: l.support_self.phase.nodes = 0; break;
    case Kind::leaf_zero: l.support_self.phase.leaves = 0; break;
    case Kind::depth_zero: l.support_self.phase.depth = 0; break;
    case Kind::graph_zero: l.support_self.phase.graphs = 0; break;
    case Kind::projection_zero: l.support_self.projection_guards = 0; break;
    case Kind::self_zero: l.support_self.self_pairs = 0; break;
    case Kind::world_zero: l.world.roster_entries = 0; break;
    default: break;
  }
  ++summary.assessments;
  summary.called_assessment_mask |= bit;
  summary.active_call = id;
  summary.active_creator = false;
  auto r =
      id == 16
          ? assess_origin_boarding_route_intermediate_root_z01(p, first, last)
          : detail::boarding_route_intermediate_root_z01_bounded(p, first, last,
                                                                 l);
  summary.active_call = 0;
  const auto before = summary.bytes;
  std::cout << "A" << id << " called=1 range=" << first << ',' << last
            << " value=" << r.has_value();
  bool complete{};
  if (!r) {
    std::cout << " original_error=";
    text(r.error());
    if (kind == Kind::range)
      check(r.error() == "RootZ01 finite [0,1] range required",
            "Exact invalid range refusal");
    else if (kind == Kind::raised)
      check(r.error() == "RootZ01 lowered registered limits required",
            "Exact invalid lowered limits refusal");
    else if (kind == Kind::one_header)
      check(r.error() == "RootZ01 fixed output capacity",
            "Exact one-header refusal");
    else if (kind == Kind::unsupported)
      check(r.error() == "RootZ01 supported arithmetic required",
            "Exact unsupported FP refusal");
    else if (kind == Kind::empty || kind == Kind::moved ||
             kind == Kind::two_headers || kind == Kind::source_zero_header)
      check(false, "Valid structural owner preflight requires the registered "
                   "structured result");
  } else {
    complete = inspect(*r, p, first, last, l);
    if (kind == Kind::range || kind == Kind::raised ||
        kind == Kind::one_header || kind == Kind::unsupported)
      check(false, "Structural preflight must retain unexpected literal");
    if (kind == Kind::empty || kind == Kind::moved)
      check(r->state == State::identity &&
                r->stop_condition == Condition::invalid_binding &&
                r->stop_stage == Stage::source &&
                r->work.support_self.phase_calls == 0,
            "Empty/moved source refuses before unsafe borrowed work");
    if (kind == Kind::two_headers)
      check(r->state == State::capacity &&
                r->stop_condition == Condition::output_capacity &&
                r->stop_stage == Stage::preflight &&
                r->work.support_self.source_guards == 0,
            "Two fixed-header boundary before source");
    if (kind == Kind::source_zero_header || kind == Kind::source_zero)
      check(r->state == State::capacity &&
                r->stop_condition == Condition::source_capacity &&
                r->stop_stage == Stage::source &&
                r->work.support_self.source_guards == 0 &&
                r->work.support_self.phase_calls == 0,
            "Zero source guard stops before Data or partition access");
    std::cout << " state=" << static_cast<unsigned>(r->state)
              << " stop=" << static_cast<unsigned>(r->stop_condition) << ','
              << static_cast<unsigned>(r->stop_stage)
              << " complete=" << r->complete << ',' << r->route_qualified
              << " cells=" << r->cells.size() << " cover=" << r->examined_nodes
              << ',' << r->maximum_depth;
    if (r->first_refusal) print_finding(*r->first_refusal);
  }
  if (id >= 34)
    std::cout << " intentional_named_cap=" << (r && named_capacity(*r, kind));
  std::cout << '\n';
  record_bound(before, 4096);
  if (summary.regression && id == 16) original_whole_regression(r);
  complete = complete && healthy();
  if (id == 16) {
    summary.attempt_open = complete;
    if (!complete && !summary.terminal_id) summary.terminal_id = id;
  } else if (id >= 17) {
    const bool intentional_capacity = id >= 34 && r && named_capacity(*r, kind);
    if (!healthy() || (!complete && !intentional_capacity)) {
      summary.attempt_open = false;
      if (!summary.terminal_id) summary.terminal_id = id;
    }
  }
}
class Environment {
 public:
  explicit Environment(int mode) {
    saved_ = std::fegetenv(&environment_) == 0;
    available = saved_ && std::fesetround(mode) == 0;
  }
  ~Environment() {
    if (saved_ && !restored_) restore();
  }
  auto restore() -> bool {
    if (restored_) return restoration_ok_;
    restored_ = true;
    restoration_ok_ = !saved_ || std::fesetenv(&environment_) == 0;
    check(restoration_ok_,
          "Full original FP environment restoration succeeded");
    if (!restoration_ok_) {
      summary.protocol_error = true;
      summary.attempt_open = false;
    }
    return restoration_ok_;
  }
  Environment(const Environment&) = delete;
  auto operator=(const Environment&) -> Environment& = delete;
  bool available{};

 private:
  fenv_t environment_{};
  bool saved_{};
  bool restored_{}, restoration_ok_{};
};
void floating(unsigned id, const Provider& empty, int mode) {
  Environment e(mode);
  if (!e.available)
    skip(id, "Host cannot select frozen FP control");
  else
    assessment(id, empty, 0, 1, Kind::unsupported);
  const auto restored = e.restore();
  const auto before = summary.bytes;
  std::cout << "FP_RESTORE A" << id << " success=" << restored << '\n';
  record_bound(before, 256);
}
void purposes(Provider& p) {
  const Provider empty;
  assessment(1, empty, 0, 1, Kind::empty);
  assessment(2, empty, std::numeric_limits<double>::quiet_NaN(), 1,
             Kind::range);
  assessment(3, empty, 0, std::numeric_limits<double>::quiet_NaN(),
             Kind::range);
  assessment(4, empty, std::numeric_limits<double>::infinity(), 1, Kind::range);
  assessment(5, empty, 0, -std::numeric_limits<double>::infinity(),
             Kind::range);
  assessment(6, empty,
             std::nextafter(0., -std::numeric_limits<double>::infinity()), 1,
             Kind::range);
  assessment(7, empty, 0,
             std::nextafter(1., std::numeric_limits<double>::infinity()),
             Kind::range);
  assessment(8, empty, 0, 1, Kind::raised);
  assessment(9, empty, 0, 1, Kind::one_header);
  assessment(10, empty, 0, 1, Kind::two_headers);
  assessment(11, empty, 0, 1, Kind::source_zero_header);
  floating(12, empty, FE_DOWNWARD);
  floating(13, empty, FE_UPWARD);
  floating(14, empty, FE_TOWARDZERO);
  if (Access::valid(p)) {
    auto retained = std::move(p);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Test moved handle.
    assessment(15, p, 0, 1, Kind::moved);
    p = std::move(retained);
  } else
    skip(15, "Genuine move owner unavailable");
  assessment(16, p, 0, 1, Kind::positive);
  assessment(17, p, 1, 0, Kind::positive);
  assessment(18, p, 0, .125, Kind::positive);
  assessment(19, p, .125, .25, Kind::positive);
  assessment(20, p, .25, .5, Kind::positive);
  assessment(21, p, .5, .75, Kind::positive);
  assessment(22, p, .75, 1, Kind::positive);
  assessment(23, p, 1, .75, Kind::positive);
  assessment(24, p, 0, 0, Kind::positive);
  assessment(25, p, .125, .125, Kind::positive);
  assessment(26, p, .25, .25, Kind::positive);
  assessment(27, p, .5, .5, Kind::positive);
  assessment(28, p, .75, .75, Kind::positive);
  assessment(29, p, 1, 1, Kind::positive);
  assessment(30, p, std::nextafter(.125, 0.), std::nextafter(.125, 1.),
             Kind::positive);
  assessment(31, p, std::nextafter(.25, 0.), std::nextafter(.25, 1.),
             Kind::positive);
  assessment(32, p, std::nextafter(.5, 0.), std::nextafter(.5, 1.),
             Kind::positive);
  assessment(33, p, std::nextafter(.75, 0.), std::nextafter(.75, 1.),
             Kind::positive);
  assessment(34, p, 0, 1, Kind::source_zero);
  assessment(35, p, 0, 1, Kind::node_zero);
  assessment(36, p, 0, 1, Kind::leaf_zero);
  assessment(37, p, 0, 1, Kind::depth_zero);
  assessment(38, p, 0, 1, Kind::graph_zero);
  assessment(39, p, 0, 1, Kind::projection_zero);
  assessment(40, p, 0, 1, Kind::self_zero);
  assessment(41, p, 0, 1, Kind::world_zero);
}
} // namespace
int main(int argc, char** argv) {
  // FIRST authorization/freeze is external and once-only. Regression is
  // released only after its original observations are retained; no mode runs at
  // compile.
  if (argc != 2 || (std::string_view(argv[1]) != "--first" &&
                    std::string_view(argv[1]) != "--regression")) {
    std::cerr
        << "RootZ Test requires explicit --first or --regression dispatch\n";
    return 2;
  }
  summary.regression = std::string_view(argv[1]) == "--regression";
  auto* original = std::cout.rdbuf();
  CountedSink sink(original);
  std::cout.rdbuf(&sink);
  std::cout << std::setprecision(17);
  const auto before_mode = summary.bytes;
  std::cout << "MODE " << argv[1] << " fixed_protocol=4C41A\n";
  record_bound(before_mode, 256);
  try {
    literal_protocol();
    const auto f = fixture_once();
    startup_inventory(f);
    ConstructorLimits l;
    l.source_bytes = 0;
    {
      const auto c = creator(1, f, l);
      check(!Access::valid(c), "Source-cap creator issues no authority");
    }
    l = ConstructorLimits{};
    l.base_guards = 0;
    {
      const auto c = creator(2, f, l);
      check(!Access::valid(c), "Base-cap creator issues no authority");
    }
    l = ConstructorLimits{};
    l.control_guards = 0;
    {
      const auto c = creator(3, f, l);
      check(!Access::valid(c), "Control-cap creator issues no authority");
    }
    auto p = creator(4, f);
    const auto* identity = Access::data(p);
    {
      auto alias = p;
      check(Access::data(alias) == identity, "Shared retained source alias");
      alias = Provider{};
      check(Access::data(p) == identity,
            "Reset one alias, keep original owner");
    }
    purposes(p);
    check(Access::data(p) == identity,
          "Move restore preserves exact owned source");
    check(summary.assessment_mask == (std::uint64_t{1} << 41) - 1,
          "Every frozen assessment purpose called or honestly skipped once");
  } catch (const std::exception& e) {
    summary.protocol_error = true;
    if (summary.active_call) {
      const auto before = summary.bytes;
      std::cout << (summary.active_creator ? "C" : "A") << summary.active_call
                << " calls=1 original_exception=";
      text(e.what());
      std::cout << '\n';
      record_bound(before, 4096);
      summary.active_call = 0;
    }
    const auto before = summary.bytes;
    std::cout << "TEST_EXCEPTION original_error=";
    text(e.what());
    std::cout << '\n';
    record_bound(before, 1024);
  }
  for (unsigned id = 1; id <= 4; ++id)
    if (!(summary.creator_mask & (1U << (id - 1)))) {
      const auto before = summary.bytes;
      summary.creator_mask |= 1U << (id - 1);
      std::cout << "C" << id << " SKIP calls=0 prerequisite=TestException\n";
      record_bound(before, 4096);
    }
  for (unsigned id = 1; id <= 41; ++id)
    if (!(summary.assessment_mask & (std::uint64_t{1} << (id - 1))))
      skip(id,
           "Test exception prevented this fixed purpose; no additional query");
  if (summary.regression) {
    check(summary.fixture_calls == 7 && summary.fixture_failure == 0 &&
              summary.creators == 4 && summary.creator_mask == 0xf &&
              summary.assessments == 16 &&
              summary.called_assessment_mask == 0xffff &&
              summary.assessment_mask == (std::uint64_t{1} << 41) - 1 &&
              summary.terminal_id == 16 && !summary.attempt_open &&
              summary.active_call == 0,
          "Original four creators/seven fixture calls/A01-A16 calls/A17-A41 "
          "skips remain exact");
  }
  const auto before_summary = summary.bytes;
  std::cout << "SUMMARY creators=" << summary.creators
            << " assessments=" << summary.assessments
            << " fixture=" << summary.fixture_calls
            << " checks=" << summary.checks << " failures=" << summary.failures
            << " protocol=" << summary.protocol_error
            << " terminal=" << summary.terminal_id << " bytes=" << summary.bytes
            << " first_failure=";
  text(summary.first_failure);
  std::cout << '\n' << std::flush;
  record_bound(before_summary, 1024);
  const bool output_failed = summary.output_error || !std::cout.good();
  std::cout.rdbuf(original);
  if (output_failed)
    std::cerr << "FAIL: RootZ Test finite output capacity; complete transcript "
                 "not retained\n";
  return summary.failures || summary.protocol_error || output_failed ||
                 summary.fixture_failure
             ? 1
             : 0;
}
