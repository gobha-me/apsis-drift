#include "origin_boarding_route_intermediate_step_attribution_driver.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#if defined(__SSE2__) && defined(__x86_64__)
#include <xmmintrin.h>
#endif
namespace {
using namespace apsis_drift;
using Bounds = BoardingPlantedLegScalarBounds;
std::size_t checks{};
int failures{};
void check(bool yes, std::string_view why) {
  ++checks;
  if (!yes) {
    ++failures;
    std::cerr << "FAIL: " << why << '\n';
  }
}
using Vec = std::array<long double, 3>;
using Frame = std::array<Vec, 3>;
auto sum(const BoardingRoutePhaseConstant& c) -> long double {
  long double value = 0;
  for (std::size_t i = 0; i < c.count; ++i)
    value += static_cast<long double>(c.terms[i]);
  return value;
}
auto point(const BoardingRoutePhasePointConstant& p) -> Vec {
  return {sum(p.coordinates[0]), sum(p.coordinates[1]), sum(p.coordinates[2])};
}
auto add(Vec a, Vec b) -> Vec {
  for (std::size_t i = 0; i < 3; ++i)
    a[i] += b[i];
  return a;
}
auto sub(Vec a, Vec b) -> Vec {
  for (std::size_t i = 0; i < 3; ++i)
    a[i] -= b[i];
  return a;
}
auto scale(Vec a, long double s) -> Vec {
  for (auto& x : a)
    x *= s;
  return a;
}
auto dot(Vec a, Vec b) -> long double {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
auto yaw(long double k) -> Frame {
  const auto c = (1 - k * k) / (1 + k * k), s = 2 * k / (1 + k * k);
  return {{{c, 0, -s},
           {0, 1, 0},
           {s, 0, c}}}; // Columns, matching original root yaw orientation.
}
auto world(const Frame& f, Vec p) -> Vec {
  Vec v{};
  for (std::size_t i = 0; i < 3; ++i)
    v = add(v, scale(f[i], p[i]));
  return v;
}
auto local(const Frame& f, Vec p) -> Vec {
  return {dot(f[0], p), dot(f[1], p), dot(f[2], p)};
}
auto lerp(long double a, long double b, long double s) -> long double {
  return a + (b - a) * s;
}
auto interpolate(Vec a, Vec b, long double s) -> Vec {
  for (std::size_t i = 0; i < 3; ++i) {
    a[i] = lerp(a[i], b[i], s);
  }
  return a;
}
struct LegOracle {
  Vec hip{}, ankle{}, boot{}, d{};
  long double D{}, rho2{}, alpha{}, gamma2{}, gamma{}, F1{}, G1{}, F2{}, G2{};
  std::array<long double, 6> margins{};
  std::array<long double, 7> angles{};
  bool valid{};
};
[[gnu::noinline]] auto oracle(double parameter, std::size_t side) -> LegOracle {
  // Independent unfactored sphere closure. This is a reporting oracle, never
  // an outward proof or source/body/support/world authority.
  LegOracle o;
  const auto request = detail::boarding_route_intermediate_step_controls(0);
  if (!request || side >= 2) return o;
  const auto& r = *request;
  const auto u = static_cast<long double>(parameter);
  const auto S = u * u * u * (10 - 15 * u + 6 * u * u),
             H = 64 * u * u * u * (1 - u) * (1 - u) * (1 - u);
  const auto root = interpolate(point(r.root[0]), point(r.root[1]), S);
  const auto root_half = lerp(r.root_yaw_half[0], r.root_yaw_half[1], S);
  const auto sole_half =
      lerp(r.feet[side].yaw_half[0], r.feet[side].yaw_half[1], S);
  const auto root_frame = yaw(root_half), sole_frame = yaw(sole_half);
  auto sole =
      interpolate(point(r.feet[side].sole[0]), point(r.feet[side].sole[1]), S);
  sole[1] += static_cast<long double>(r.feet[side].swing_height_metres) * H;
  const auto hip_width = static_cast<long double>(.14);
  o.hip =
      add(root, world(root_frame, {side == 0 ? -hip_width : hip_width, 0, 0}));
  o.ankle = add(sole, {0, static_cast<long double>(.1), 0});
  o.boot = add(sole, {0, static_cast<long double>(.05), 0});
  o.d = local(sole_frame, sub(o.ankle, o.hip));
  o.rho2 = o.d[0] * o.d[0] + o.d[1] * o.d[1];
  o.D = dot(o.d, o.d);
  if (!(o.rho2 > 0 && o.D > 0 && o.d[1] < 0)) return o;
  const auto l1 = static_cast<long double>(.47285),
             l2 = static_cast<long double>(.47478);
  const auto rho = std::sqrt(o.rho2);
  o.alpha = (l1 * l1 - l2 * l2 + o.D) / (2 * o.D);
  o.gamma2 = (l1 * l1 - o.alpha * o.alpha * o.D) / o.D;
  if (!(o.gamma2 > 0)) return o;
  o.gamma = std::sqrt(o.gamma2);
  o.F1 = o.alpha * rho + o.gamma * o.d[2];
  o.G1 = o.gamma * rho - o.alpha * o.d[2];
  o.F2 = (1 - o.alpha) * rho - o.gamma * o.d[2];
  o.G2 = -((1 - o.alpha) * o.d[2] + o.gamma * rho);
  const auto pi = std::numbers::pi_v<long double>;
  o.margins = {-o.d[1] * (2 - std::sqrt(3.L)) - std::abs(o.d[0]),
               o.G1 * std::cos(pi / 9) + o.F1 * std::sin(pi / 9),
               o.F1 * std::sqrt(3.L) / 2 + o.G1 / 2,
               (o.D - l1 * l1 - l2 * l2) / (2 * l1 * l2) + std::sqrt(2.L) / 2,
               o.F2 / 2 - std::abs(o.G2) * std::sqrt(3.L) / 2,
               dot(root_frame[0], sole_frame[0]) - std::sqrt(2.L) / 2};
  const auto hp = std::atan2(o.G1, o.F1), sp = std::atan2(o.G2, o.F2),
             roll = std::atan2(o.d[0], -o.d[1]);
  o.angles = {hp,
              sp,
              hp - sp,
              2 * (std::atan(sole_half) - std::atan(root_half)),
              side == 0 ? -roll : roll,
              -sp,
              -roll};
  o.valid = true;
  return o;
}
auto contains(Bounds b, long double value) -> bool {
  // Only independent numeric corroboration tolerates long-double libm/ordering
  // roundoff. Actual classification and sector signs use original bounds.
  const auto rounding = 2.e-12L * std::max(1.L, std::abs(value));
  return std::isfinite(value) &&
         static_cast<long double>(b.lower) - rounding <= value &&
         value <= static_cast<long double>(b.upper) + rounding;
}

using namespace apsis_drift::test_detail;
using Report = IntermediateSectorAttributionReport;
using Case = IntermediateSectorAttributionCase;
using Limits = IntermediateSectorAttributionLimits;
using State = IntermediateSectorAttributionState;
using Sector = IntermediateSectorAttributionSector;
using Error = IntermediateSectorAttributionError;
using CellState = detail::BoardingRouteFootPhaseCellResult;
using Condition = BoardingRouteFootPhaseCondition;
std::size_t actual_calls{};
struct StreamCounter : std::streambuf {
  std::streambuf* destination;
  std::size_t& bytes;
  StreamCounter(std::streambuf* target, std::size_t& count)
      : destination(target), bytes(count) {}
  auto overflow(int_type c) -> int_type override {
    if (traits_type::eq_int_type(c, traits_type::eof()))
      return traits_type::not_eof(c);
    if (bytes >= kIntermediateSectorAttributionMaximumStreamBytes)
      return traits_type::eof();
    ++bytes;
    return destination->sputc(traits_type::to_char_type(c));
  }
  auto sync() -> int override { return destination->pubsync(); }
};
struct Summary {
  std::array<std::uint64_t, 8> records{};
  BoardingRouteFootPhaseCounters work;
  std::size_t attempted{}, output{};
  bool complete{};
};
static_assert(sizeof(Summary) < 1024);
static_assert(sizeof(Report) <=
              kIntermediateSectorAttributionMaximumOutputBytes);
auto hash_case(const Case& c) -> std::uint64_t {
  std::uint64_t h = 1469598103934665603ULL;
  auto integer = [&](std::uint64_t x) {
    h ^= x;
    h *= 1099511628211ULL;
  };
  auto scalar = [&](double x) { integer(std::bit_cast<std::uint64_t>(x)); };
  auto bounds = [&](Bounds b) {
    scalar(b.lower);
    scalar(b.upper);
  };
  for (auto x : std::array{c.interval.global_first, c.interval.global_last,
                           c.interval.local_first, c.interval.local_last,
                           c.reason.first, c.reason.last})
    scalar(x);
  integer(static_cast<unsigned>(c.state));
  integer(static_cast<unsigned>(c.reason.condition));
  integer(static_cast<unsigned>(c.reason.predicate_condition));
  integer(c.reason.depth);
  integer(c.reason.side ? *c.reason.side + 1 : 0);
  bounds(c.reason.limiting_bound);
  integer(c.evidence_side ? *c.evidence_side + 1 : 0);
  integer(static_cast<unsigned>(c.attribution.sector));
  integer(c.attribution.evaluated_mask);
  integer(c.attribution.classified);
  bounds(c.attribution.limiting_bound);
  for (auto b : c.sector_margins)
    bounds(b);
  for (auto b :
       std::array{c.closure.distance_squared, c.closure.rho_squared,
                  c.closure.gamma_squared, c.closure.alpha, c.closure.gamma})
    bounds(b);
  for (const auto& a : c.closure.angular_derivatives) {
    bounds(a.rate);
    bounds(a.coordinate_second);
  }
  integer(c.closure.written);
  integer(c.point_violation);
  return h;
}
auto summary(const Report& r) -> Summary {
  Summary s;
  s.work = r.work;
  s.attempted = r.attempted_cases;
  s.output = r.output_capacity_bytes;
  s.complete = r.complete_manifest;
  for (std::size_t i = 0; i < 8; ++i) {
    s.records[i] = hash_case(r.cases[i]);
  }
  return s;
}
void denied(const Report& r) {
  check(!r.source_qualified && !r.support_qualified && !r.load_qualified &&
            !r.self_qualified && !r.material_qualified && !r.world_qualified &&
            !r.route_qualified && !r.actor_qualified && !r.seat_qualified &&
            !r.save_qualified && !r.dynamics_qualified,
        "Arithmetic replay grants no game authority");
}
void audit(const std::expected<Report, Error>& result) {
  if (result) actual_calls += result->attempted_cases;
}
void validate_report(const Report& r) {
  denied(r);
  check(r.output_capacity_bytes <= r.limits.output_bytes &&
            r.output_capacity_bytes <= 8192 && r.attempted_cases <= 8,
        "Actual compact payload and cell calls obey registered bounds");
  check(r.work.graphs <= r.limits.graphs && r.work.legs <= r.limits.legs &&
            r.work.bodies <= r.limits.bodies &&
            r.work.sectors <= r.limits.sectors &&
            r.work.timing <= r.limits.timing &&
            r.work.graphs <= r.attempted_cases &&
            r.work.legs <= 2 * r.work.graphs &&
            r.work.bodies <= r.work.graphs &&
            r.work.sectors <= 3 * r.work.graphs &&
            r.work.timing <= 6 * r.work.graphs,
        "All eight cases share one never-reset work ledger");
  const auto& manifest = intermediate_sector_attribution_manifest();
  for (std::size_t i = 0; i < 8; ++i) {
    const auto& c = r.cases[i];
    const auto& m = manifest[i];
    check(c.interval.global_first == m.global_first &&
              c.interval.global_last == m.global_last &&
              c.interval.local_first == m.local_first &&
              c.interval.local_last == m.local_last,
          "Every result retains its immutable location");
    if (i >= r.attempted_cases) {
      check(c.state == State::not_run && !c.closure.written &&
                !c.attribution.classified &&
                c.attribution.evaluated_mask == 0 && !c.evidence_side &&
                !c.point_violation,
            "Stopped manifest tail has no stale reached evidence");
      continue;
    }
    check(c.state != State::not_run,
          "Attempt count names actual attempted cases only");
    if (c.state == State::capacity || c.state == State::unsupported)
      check(i + 1 == r.attempted_cases && !r.complete_manifest,
            "Capacity/unsupported stops before every later case");
    if (c.attribution.classified) {
      check(c.state == State::unresolved &&
                c.reason.condition == Condition::joint_sector &&
                c.reason.side && *c.reason.side < 2 &&
                c.evidence_side == c.reason.side && c.closure.written,
            "Attribution comes only from genuine reached leg joint-sector "
            "refusal");
      const auto ordinal = static_cast<unsigned>(c.attribution.sector);
      if (ordinal >= 1 && ordinal <= 6) {
        const auto j = ordinal - 1;
        const auto b = c.sector_margins[j];
        check(c.attribution.evaluated_mask == ((unsigned{1} << (j + 1)) - 1) &&
                  b.lower < 0 && b.lower == c.reason.limiting_bound.lower &&
                  b.upper == c.reason.limiting_bound.upper,
              "Classified failed margin is original first refusing bound with "
              "exact evaluated prefix");
        for (std::size_t k = 0; k < j; ++k)
          check(c.sector_margins[k].lower >= 0,
                "Every earlier reached sector passed");
      } else
        check(c.attribution.sector == Sector::forward_shin &&
                  c.attribution.evaluated_mask == 63 &&
                  c.reason.limiting_bound.lower <= 0,
              "Strict F2 guard remains separate from six passed sectors");
      const auto witnessed = c.interval.local_first == c.interval.local_last &&
                             (c.attribution.sector == Sector::forward_shin
                                  ? c.reason.limiting_bound.upper <= 0
                                  : c.reason.limiting_bound.upper < 0);
      check(c.point_violation == witnessed,
            "Only original outward point bound can witness necessary-sector "
            "violation");
    } else
      check(!c.point_violation,
            "Unclassified packet cannot manufacture a violation witness");
  }
  check(r.complete_manifest == (r.attempted_cases == 8 &&
                                r.cases.back().state != State::capacity &&
                                r.cases.back().state != State::unsupported),
        "Complete diagnostic means manifest executed, not movement qualified");
}
void print_first(const Report& r) {
  std::cout << std::setprecision(17)
            << "FIRST_SECTOR_ATTRIBUTION version=" << r.attribution_version
            << " attempts=" << r.attempted_cases
            << " manifest=" << r.complete_manifest
            << " output=" << r.output_capacity_bytes
            << " work=" << r.work.graphs << ',' << r.work.legs << ','
            << r.work.bodies << ',' << r.work.sectors << ',' << r.work.timing
            << '\n';
  for (std::size_t i = 0; i < 8; ++i) {
    const auto& c = r.cases[i];
    std::cout << "FIRST_CASE " << i
              << " state=" << static_cast<unsigned>(c.state)
              << " global=" << c.interval.global_first << ','
              << c.interval.global_last << " local=" << c.interval.local_first
              << ',' << c.interval.local_last
              << " reason=" << static_cast<unsigned>(c.reason.condition)
              << " side=";
    if (c.reason.side)
      std::cout << *c.reason.side;
    else
      std::cout << "none";
    std::cout << " sector=" << static_cast<unsigned>(c.attribution.sector)
              << " mask=" << static_cast<unsigned>(c.attribution.evaluated_mask)
              << " bound=" << c.reason.limiting_bound.lower << ','
              << c.reason.limiting_bound.upper
              << " closure=" << c.closure.written
              << " point_violation=" << c.point_violation << " margins=";
    for (std::size_t j = 0; j < 6; ++j) {
      if ((c.attribution.evaluated_mask & (unsigned{1} << j)) != 0)
        std::cout << c.sector_margins[j].lower << ':'
                  << c.sector_margins[j].upper;
      else
        std::cout << "UNEVALUATED";
      if (j != 5) std::cout << ',';
    }
    std::cout << '\n';
  }
  std::cout << std::flush;
}
void point_oracles(const Report& r) {
  std::array<bool, 2> coarse{};
  for (std::size_t i = 2; i < 8; ++i) {
    const auto& c = r.cases[i];
    const auto u = c.interval.local_first;
    const auto port = oracle(u, 0), star = oracle(u, 1);
    const auto side = c.evidence_side.value_or(0);
    const auto& o = side == 0 ? port : star;
    std::cout << std::setprecision(21) << "FIRST_LD_POINT " << i
              << " side=" << side << " port_domain=" << port.valid
              << " star_domain=" << star.valid << " F=" << o.F1 << ',' << o.G1
              << ',' << o.F2 << ',' << o.G2 << " margins=";
    for (auto x : o.margins)
      std::cout << x << ',';
    std::cout << " angles=";
    for (auto x : o.angles)
      std::cout << x << ',';
    std::cout << '\n';
    if (i >= 6)
      coarse[i - 6] = o.valid && o.F2 > 0 && o.margins[4] < 0 &&
                      o.angles[5] > std::numbers::pi_v<long double> / 6;
    if (!c.closure.written) continue;
    check(side < 2 && o.valid, "Actually reached leg closure has independent "
                               "positive unfactored sphere solution");
    if (!o.valid) continue;
    check(contains(c.closure.distance_squared, o.D) &&
              contains(c.closure.rho_squared, o.rho2) &&
              contains(c.closure.alpha, o.alpha) &&
              contains(c.closure.gamma_squared, o.gamma2) &&
              contains(c.closure.gamma, o.gamma),
          "Independent closure corroborates actual selected-side original "
          "value bounds");
    for (std::size_t j = 0; j < 6; ++j)
      if (c.attribution.evaluated_mask & (unsigned{1} << j))
        check(contains(c.sector_margins[j], o.margins[j]),
              "Reached original point margin encloses independent threshold "
              "calculation");
    if (c.attribution.sector == Sector::forward_shin)
      check(contains(c.reason.limiting_bound, o.F2),
            "Strict forward-shin bound corroborates original F2");
  }
  std::cout << std::flush;
  for (bool corroborated : coarse)
    check(corroborated,
          "Independent coarse-point oracle corroborates ankle pitch "
          "beyond30degrees with positive forward-shin component");
}
[[gnu::noinline]] void static_guards() {
  const std::array<std::array<double, 2>, 8> locations{
      {{5. / 128, 11. / 256},
       {46. / 1024, 47. / 1024},
       {5. / 128, 5. / 128},
       {21. / 512, 21. / 512},
       {11. / 256, 11. / 256},
       {93. / 2048, 93. / 2048},
       {1. / 8, 1. / 8},
       {1. / 4, 1. / 4}}};
  const auto& m = intermediate_sector_attribution_manifest();
  for (std::size_t i = 0; i < 8; ++i)
    check(m[i].local_first == locations[i][0] &&
              m[i].local_last == locations[i][1] &&
              4 * m[i].global_first == m[i].local_first &&
              4 * m[i].global_last == m[i].local_last,
          "Frozen eight locations map global to local once");
  for (std::size_t ordinal = 0; ordinal < 6; ++ordinal) {
    std::array<Bounds, 6> margins;
    for (auto& b : margins)
      b = {1, 2};
    margins[ordinal] = {-.5, .25};
    BoardingRouteFootPhaseRefusal reason;
    reason.condition = Condition::joint_sector;
    reason.side = 0;
    reason.limiting_bound = margins[ordinal];
    auto c = intermediate_sector_attribution_classify_math(
        CellState::unresolved, reason, margins);
    check(c.classified && static_cast<unsigned>(c.sector) == ordinal + 1 &&
              c.evaluated_mask == ((unsigned{1} << (ordinal + 1)) - 1),
          "Synthetic classifier preserves original order without evaluating "
          "later slots");
    check(!c.source_qualified && !c.body_qualified && !c.support_qualified &&
              !c.route_qualified,
          "Classifier cannot authenticate caller geometry");
    for (std::size_t later = ordinal + 1; later < 6; ++later)
      margins[later] = {std::numeric_limits<double>::quiet_NaN(), 0};
    check(intermediate_sector_attribution_classify_math(CellState::unresolved,
                                                        reason, margins)
              .classified,
          "Unreached later slots never enter first-failure attribution");
    reason.limiting_bound = {-.25, .25};
    check(!intermediate_sector_attribution_classify_math(CellState::unresolved,
                                                         reason, margins)
               .classified,
          "Mismatched reason remains unclassified");
  }
  std::array<Bounds, 6> margins;
  for (auto& b : margins)
    b = {1, 2};
  BoardingRouteFootPhaseRefusal reason;
  reason.condition = Condition::joint_sector;
  reason.side = 1;
  reason.limiting_bound = {-.1, 0};
  check(intermediate_sector_attribution_classify_math(CellState::unresolved,
                                                      reason, margins)
                .sector == Sector::forward_shin,
        "All six passed sectors distinguish strict F2 failure");
  for (auto state : std::array{CellState::accepted, CellState::capacity,
                               CellState::unsupported})
    check(!intermediate_sector_attribution_classify_math(state, reason, margins)
               .classified,
          "Other states cannot retain stale classification");
  reason.side.reset();
  check(!intermediate_sector_attribution_classify_math(CellState::unresolved,
                                                       reason, margins)
             .classified,
        "Side-less torso is unclassified");
  reason.side = 2;
  check(!intermediate_sector_attribution_classify_math(CellState::unresolved,
                                                       reason, margins)
             .classified,
        "Invalid side is unclassified");
  reason.side = 1;
  margins[0] = {-.5, .25};
  reason.limiting_bound = {-.125, .125};
  check(!intermediate_sector_attribution_classify_math(CellState::unresolved,
                                                       reason, margins)
             .classified,
        "Distinct wrong-leg reason cannot match current leg's failed bound");
  for (auto& b : margins)
    b = {1, 2};
  reason.side = 0;
  reason.condition = Condition::reach;
  check(!intermediate_sector_attribution_classify_math(CellState::unresolved,
                                                       reason, margins)
             .classified,
        "Other predicates are unclassified");
  reason.condition = Condition::joint_sector;
  reason.limiting_bound = {.1, .2};
  check(!intermediate_sector_attribution_classify_math(CellState::unresolved,
                                                       reason, margins)
             .classified,
        "All nonnegative margins plus positive F2 cannot invent a failure");
  reason.limiting_bound = {0, 0};
  auto stale = intermediate_sector_attribution_classify_math(
      CellState::unresolved, reason, margins);
  check(stale.classified && stale.sector == Sector::forward_shin,
        "Exact zero fails strict positive forward-shin requirement");
  stale = intermediate_sector_attribution_classify_math(CellState::capacity,
                                                        reason, margins);
  check(
      !stale.classified && stale.evaluated_mask == 0 &&
          stale.sector == Sector::none,
      "Reusing classifier output on capacity clears previous complete prefix");
  margins[0] = {2, 1};
  check(!intermediate_sector_attribution_classify_math(CellState::unresolved,
                                                       reason, margins)
             .classified,
        "Malformed reached prefix is unclassified");
  margins[0] = {std::numeric_limits<double>::quiet_NaN(), 1};
  check(!intermediate_sector_attribution_classify_math(CellState::unresolved,
                                                       reason, margins)
             .classified,
        "Nonfinite reached prefix is unclassified");
  for (std::size_t field = 0; field < 6; ++field) {
    Limits l;
    switch (field) {
      case 0: ++l.graphs; break;
      case 1: ++l.legs; break;
      case 2: ++l.bodies; break;
      case 3: ++l.sectors; break;
      case 4: ++l.timing; break;
      default: ++l.output_bytes; break;
    }
    const auto r = run_intermediate_sector_attribution(l);
    audit(r);
    check(!r && r.error() == Error::invalid_limits,
          "Raised driver ceiling refuses before any replay");
  }
}
// Added only after both frozen09f77bb FIRST logs agreed. These are retained
// diagnostic outcomes, not a replacement path or continuous-motion proof.
void observed_outcomes(const Report& r) {
  check(r.complete_manifest && r.attempted_cases == 8 &&
            r.output_capacity_bytes == 4672 && r.work.graphs == 8 &&
            r.work.legs == 12 && r.work.bodies == 4 && r.work.sectors == 16 &&
            r.work.timing == 24,
        "Frozen diagnostic retains all eight actual calls and one original "
        "aggregate work ledger");
  const std::array<Bounds, 2> interval_bounds{
      {{-5.457909659112304e-05, .00039041924679814049},
       {-1.4626336539147469e-06, .00013341588569426579}}};
  const std::array<Bounds, 2> violation_bounds{
      {{-.0061777525694312893, -.0061777525694240712},
       {-.024489139830267435, -.024489139830260211}}};
  for (std::size_t i = 0; i < 8; ++i) {
    const auto& c = r.cases[i];
    check(c.closure.written && c.evidence_side == 0,
          "Every recorded case reached authentic port-leg closure");
    if (i >= 2 && i <= 5) {
      check(c.state == State::accepted &&
                c.reason.condition == Condition::none && !c.reason.side &&
                !c.attribution.classified &&
                c.attribution.sector == Sector::none &&
                c.attribution.evaluated_mask == 63 && !c.point_violation,
            "Four frozen early point replays pass original kinematics without "
            "whole-interval authority");
      for (auto margin : c.sector_margins)
        check(margin.lower > 0, "All six original early point sectors have "
                                "positive outward margin");
      continue;
    }
    check(c.state == State::unresolved &&
              c.reason.condition == Condition::joint_sector &&
              c.reason.side == 0 && c.attribution.classified &&
              c.attribution.sector == Sector::ankle_pitch &&
              c.attribution.evaluated_mask == 31,
          "Original first refusing obligation is port ankle pitch, not another "
          "sector or strict F2 guard");
    const auto bound = i < 2 ? interval_bounds[i] : violation_bounds[i - 6];
    check(c.reason.limiting_bound.lower == bound.lower &&
              c.reason.limiting_bound.upper == bound.upper &&
              c.sector_margins[4].lower == bound.lower &&
              c.sector_margins[4].upper == bound.upper,
          "Frozen original ankle-sector bound remains unchanged");
    if (i < 2)
      check(bound.lower < 0 && bound.upper > 0 && !c.point_violation,
            "Retained interval straddles remain unresolved without proving an "
            "exact forbidden pose");
    else
      check(bound.upper < 0 && c.point_violation &&
                c.interval.local_first == c.interval.local_last,
            "Two preregistered coarse points have outward necessary "
            "ankle-sector violations");
  }
}
[[gnu::noinline]] auto first_observation() -> Summary {
  auto r = run_intermediate_sector_attribution();
  audit(r);
  if (!r) {
    std::cout << "FIRST_SECTOR_ATTRIBUTION api_error="
              << static_cast<unsigned>(r.error()) << '\n'
              << std::flush;
    throw std::runtime_error("Registered attribution API refused");
  }
  print_first(*r);
  point_oracles(*r);
  validate_report(*r);
  observed_outcomes(*r);
  return summary(*r);
}
void same_work(const BoardingRouteFootPhaseCounters& a,
               const BoardingRouteFootPhaseCounters& b) {
  check(a.graphs == b.graphs && a.legs == b.legs && a.bodies == b.bodies &&
            a.sectors == b.sectors && a.timing == b.timing,
        "Exact shared-work replay preserves aggregate accounting");
}
[[gnu::noinline]] void validation_replays(const Summary& base) {
  Limits exact;
  exact.graphs = base.work.graphs;
  exact.legs = base.work.legs;
  exact.bodies = base.work.bodies;
  exact.sectors = base.work.sectors;
  exact.timing = base.work.timing;
  {
    const auto r = run_intermediate_sector_attribution(exact);
    audit(r);
    check(r.has_value(), "Exact work replay produces diagnostic");
    if (r) {
      validate_report(*r);
      const auto s = summary(*r);
      same_work(s.work, base.work);
      check(s.records == base.records && s.complete == base.complete &&
                s.attempted == base.attempted,
            "Exact work replay preserves every copied evidence field");
    }
  }
  const std::array consumed{base.work.graphs, base.work.legs, base.work.bodies,
                            base.work.sectors, base.work.timing};
  const std::array conditions{Condition::graph_capacity,
                              Condition::leg_capacity, Condition::body_capacity,
                              Condition::sector_capacity,
                              Condition::timing_capacity};
  for (std::size_t field = 0; field < 5; ++field)
    if (consumed[field] > 0) {
      for (bool zero : std::array{false, true}) {
        auto l = exact;
        const auto limit = zero ? 0 : consumed[field] - 1;
        switch (field) {
          case 0: l.graphs = limit; break;
          case 1: l.legs = limit; break;
          case 2: l.bodies = limit; break;
          case 3: l.sectors = limit; break;
          default: l.timing = limit; break;
        }
        const auto r = run_intermediate_sector_attribution(l);
        audit(r);
        check(r.has_value(), "Lowered stage produces honest compact report");
        if (r) {
          validate_report(*r);
          check(r->attempted_cases > 0 && !r->complete_manifest &&
                    r->cases[r->attempted_cases - 1].state == State::capacity &&
                    r->cases[r->attempted_cases - 1].reason.condition ==
                        conditions[field],
                "Reached stage zero/one-less stops at correct capacity, not "
                "earlier ordinary refusal");
        }
      }
    }
  {
    auto l = exact;
    l.output_bytes = base.output;
    const auto r = run_intermediate_sector_attribution(l);
    audit(r);
    check(r && summary(*r).records == base.records,
          "Exact measured compact output retains original records");
  }
  if (base.output > 0) {
    auto l = exact;
    l.output_bytes = base.output - 1;
    const auto r = run_intermediate_sector_attribution(l);
    audit(r);
    check(!r && r.error() == Error::output_capacity,
          "One-less output stops before geometry");
  }
  const auto saved = std::fegetround();
  for (int mode : std::array{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    const auto changed = std::fesetround(mode);
    check(changed == 0, "Unsafe FP mode installed");
    if (changed != 0) continue;
    const auto r = run_intermediate_sector_attribution();
    audit(r);
    std::fesetround(saved);
    check(r && r->attempted_cases == 1 &&
              r->cases[0].state == State::unsupported && r->work.graphs == 0,
          "Non-nearest stops at first call without graph work");
    if (r) validate_report(*r);
  }
#if defined(__SSE2__) && defined(__x86_64__)
  const auto mxcsr = _mm_getcsr();
  for (unsigned bit :
       std::array{unsigned{1} << 15, unsigned{1} << 6, unsigned{1} << 13}) {
    _mm_setcsr(mxcsr | bit);
    const auto r = run_intermediate_sector_attribution();
    audit(r);
    _mm_setcsr(mxcsr);
    check(r && r->attempted_cases == 1 &&
              r->cases[0].state == State::unsupported && r->work.graphs == 0,
          "FTZ/DAZ/MXCSR stops first without stale evidence");
    if (r) validate_report(*r);
  }
#endif
  check(actual_calls <= 110, "All FIRST and fixed validation replays stay "
                             "within registered110-call total");
  std::cout << "VALIDATION attempted_calls=" << actual_calls
            << " baseline_work=" << base.work.graphs << ',' << base.work.legs
            << ',' << base.work.bodies << ',' << base.work.sectors << ','
            << base.work.timing << '\n';
}
} // namespace
int main() {
  std::size_t bytes{};
  StreamCounter cout_counter(std::cout.rdbuf(), bytes),
      cerr_counter(std::cerr.rdbuf(), bytes);
  auto* old_out = std::cout.rdbuf(&cout_counter);
  auto* old_err = std::cerr.rdbuf(&cerr_counter);
  try {
    static_guards();
    const auto base = first_observation();
    validation_replays(base);
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "FAIL: " << e.what() << '\n';
  }
  check(bytes + 128 <= 16384,
        "Complete bounded executable output fits16KiB including summary");
  std::cout << "Attribution checks=" << checks << " failures=" << failures
            << " stream_bytes_before_summary=" << bytes << '\n';
  std::cout.flush();
  std::cerr.flush();
  std::cout.rdbuf(old_out);
  std::cerr.rdbuf(old_err);
  return failures == 0 ? 0 : 1;
}
