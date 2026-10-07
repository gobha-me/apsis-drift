#include "apsis_drift/origin_boarding_hip_joint_plane_method01.hpp"
#include "apsis_drift/origin_boarding_self_model02.hpp"
#include "origin_boarding_hip_joint_plane_method01_internal.hpp"
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
#include <variant>
#if defined(__SSE2__) && defined(__x86_64__)
#include <xmmintrin.h>
#endif
namespace {
using namespace apsis_drift;
using Provider = OriginBoardingIntermediatePauseSupport;
using Diagnostic = BoardingHipJointPlaneMethod01Diagnostic;
using Expected = BoardingHipJointPlaneMethod01Expected;
using Data = BoardingHipJointPlaneMethod01Data;
using Feature = BoardingHipJointPlaneMethod01Feature;
using State = BoardingHipJointPlaneMethod01State;
using Outcome = BoardingHipJointPlaneMethod01Outcome;
using Condition = BoardingHipJointPlaneMethod01Condition;
using Stage = BoardingHipJointPlaneMethod01Stage;
using Limits = detail::BoardingHipJointPlaneMethod01Limits;
using Context = detail::BoardingHipJointPlaneMethod01FreshCallContext;
using Scalar = BoardingFootSiteScalarBounds;
static_assert(sizeof(Expected) <= 176 && sizeof(Diagnostic) <= 168 &&
              sizeof(Data) <= 9216 && sizeof(Limits) <= 16);
static_assert(!std::is_copy_constructible_v<Expected> &&
              std::is_move_constructible_v<Expected> &&
              !std::is_copy_constructible_v<Diagnostic> &&
              std::is_move_constructible_v<Diagnostic>);
static_assert(!std::is_constructible_v<
              Context, const BoardingIntermediateEndpoint04Diagnostic&,
              Diagnostic&, const Provider&>);
static_assert(!std::is_copy_constructible_v<Context> &&
              !std::is_move_constructible_v<Context> &&
              !std::is_copy_assignable_v<Context> &&
              !std::is_move_assignable_v<Context>);
static_assert(
    std::is_same_v<decltype(&detail::hip_joint_plane_method01_bounded),
                   Expected (*)(const Provider&, Limits)>);
static_assert(
    std::is_same_v<decltype(&assess_origin_boarding_hip_joint_plane_method01),
                   Expected (*)(const Provider&)>);
static_assert(!Diagnostic::self_qualified && !Diagnostic::endpoint_qualified &&
              !Diagnostic::world_qualified && !Diagnostic::route_qualified &&
              !Diagnostic::actor_qualified && !Diagnostic::seat_qualified &&
              !Diagnostic::save_qualified && !Diagnostic::dynamics_qualified &&
              !Diagnostic::material_qualified &&
              !Diagnostic::strength_qualified &&
              !Diagnostic::friction_qualified &&
              !Diagnostic::first_flight_qualified);
std::uint64_t checks{};
std::uint32_t failures{};
std::size_t shared_bytes{};
struct Totals {
  std::array<std::uint32_t, 4> work{};
  std::array<std::uint32_t, 3> fixed{};
  std::uint32_t available{}, unavailable{}, enclosed{}, overlap{};
  std::uint16_t creators{}, consumers{}, skips{}, next_slot{1},
      geometry_audits{}, distance_audits{};
} totals;
static_assert(sizeof(Totals) <= 96 && sizeof(Totals) + sizeof(checks) +
                                              sizeof(failures) +
                                              sizeof(shared_bytes) <=
                                          140);
void check(bool yes, std::string_view why) {
  if (checks == std::numeric_limits<std::uint64_t>::max() ||
      (!yes && failures == std::numeric_limits<std::uint32_t>::max()))
    throw std::runtime_error("Test counter overflow");
  ++checks;
  if (!yes) {
    ++failures;
    std::cerr << "FAIL: " << why << '\n';
  }
}
void aggregate(std::uint32_t& n, std::uint32_t amount) {
  if (amount > std::numeric_limits<std::uint32_t>::max() - n)
    throw std::runtime_error("Test aggregate overflow");
  n += amount;
}
void slot(std::uint16_t label, bool called) {
  check(label == totals.next_slot && label <= 29,
        "Literal ordered29-purpose roster");
  if (label != totals.next_slot || label > 29)
    throw std::runtime_error("Invalid roster slot");
  ++totals.next_slot;
  if (called)
    ++totals.consumers;
  else {
    ++totals.skips;
    std::cout << "SKIP A" << label << '\n';
  }
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
    std::fegetexceptflag(&exceptions_, FE_ALL_EXCEPT);
#if defined(__SSE2__) && defined(__x86_64__)
    control_ = _mm_getcsr();
#endif
    if (mode < 3) {
      const std::array modes{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
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

static_assert(sizeof(StreamCounter) <= 88 && sizeof(Environment) <= 32 &&
              sizeof(ExceptionState) <= 32);
struct T {
  long double lo{}, hi{};
};
using V = std::array<T, 3>;
struct Workspace {
  std::array<V, 12> vectors;
  std::array<V, 3> matrix;
  std::array<T, 32> scalars;
};
struct RecordWorkspace {
  std::array<V, 5> vectors;
  std::array<T, 4> scalars;
};
struct SectionWorkspace {
  std::array<V, 1> vectors;
  std::array<T, 9> scalars;
};
static_assert(sizeof(RecordWorkspace) <= 608 &&
              sizeof(SectionWorkspace) <= 384);
struct Reconstruction {
  std::array<V, 3> points, frame;
  V unit, xi, center;
  T owner, radius, distance;
  bool geometry_available{}, distance_available{}, section_inapplicable{};
};
static_assert(sizeof(long double) <= 16 && sizeof(T) <= 32 &&
              sizeof(Workspace) <= 2464 && sizeof(Reconstruction) <= 1024);
auto valid(T t) -> bool {
  return std::isfinite(t.lo) && std::isfinite(t.hi) && t.lo <= t.hi;
}
auto down(long double x) -> long double {
  return std::nextafter(x, -std::numeric_limits<long double>::infinity());
}
auto up(long double x) -> long double {
  return std::nextafter(x, std::numeric_limits<long double>::infinity());
}
void point(double x, T& t) {
  t = {static_cast<long double>(x), static_cast<long double>(x)};
}
auto add(const T& a, const T& b, T& t) -> bool {
  if (!valid(a) || !valid(b)) return false;
  t = {down(a.lo + b.lo), up(a.hi + b.hi)};
  return valid(a) && valid(b) && valid(t);
}
auto sub(const T& a, const T& b, T& t) -> bool {
  if (!valid(a) || !valid(b)) return false;
  t = {down(a.lo - b.hi), up(a.hi - b.lo)};
  return valid(a) && valid(b) && valid(t);
}
auto neg(const T& a, T& t) -> bool {
  if (!valid(a)) return false;
  t = {-a.hi, -a.lo};
  return valid(a) && valid(t);
}
auto mul(const T& a, const T& b, T& t) -> bool {
  if (!valid(a) || !valid(b)) return false;
  const std::array<long double, 4> p{a.lo * b.lo, a.lo * b.hi, a.hi * b.lo,
                                     a.hi * b.hi};
  if (!std::all_of(p.begin(), p.end(),
                   [](long double x) { return std::isfinite(x); }))
    return false;
  t = {down(*std::min_element(p.begin(), p.end())),
       up(*std::max_element(p.begin(), p.end()))};
  return valid(t);
}
auto divide(const T& a, const T& b, T& t) -> bool {
  if (!valid(a) || !valid(b) || !(b.lo > 0 || b.hi < 0)) return false;
  const std::array<long double, 4> q{a.lo / b.lo, a.lo / b.hi, a.hi / b.lo,
                                     a.hi / b.hi};
  if (!std::all_of(q.begin(), q.end(),
                   [](long double x) { return std::isfinite(x); }))
    return false;
  t = {down(*std::min_element(q.begin(), q.end())),
       up(*std::max_element(q.begin(), q.end()))};
  return valid(t);
}
auto square(const T& a, T& t) -> bool {
  if (!valid(a)) return false;
  const auto x = a.lo * a.lo, y = a.hi * a.hi;
  t = {a.lo <= 0 && a.hi >= 0 ? 0.L : down(std::min(x, y)), up(std::max(x, y))};
  return valid(t);
}
auto absolute(const T& a, T& t) -> bool {
  if (!valid(a)) return false;
  t = a.lo >= 0 ? a : a.hi <= 0 ? T{-a.hi, -a.lo} : T{0, std::max(-a.lo, a.hi)};
  return valid(t);
}
auto root(const T& a, T& t) -> bool {
  if (!valid(a) || a.lo < 0) return false;
  const auto lo = std::sqrt(a.lo), hi = std::sqrt(a.hi),
             e = 512 * std::numeric_limits<long double>::epsilon();
  t = {std::max(0.L, down(lo - e * std::max(1.L, std::abs(lo)))),
       up(hi + e * std::max(1.L, std::abs(hi)))};
  return valid(t);
}
auto dot(const V& a, const V& b, T& t) -> bool {
  T x, y, z, s;
  return mul(a[0], b[0], x) && mul(a[1], b[1], y) && mul(a[2], b[2], z) &&
         add(x, y, s) && add(s, z, t);
}
auto norm(const V& a, T& t) -> bool {
  T x, y, z, s;
  return square(a[0], x) && square(a[1], y) && square(a[2], z) &&
         add(x, y, s) && add(s, z, t);
}
auto subtract(const V& a, const V& b, V& v) -> bool {
  for (std::size_t i = 0; i < 3; ++i)
    if (!sub(a[i], b[i], v[i])) return false;
  return true;
}
auto scale(const V& a, const T& b, V& v) -> bool {
  for (std::size_t i = 0; i < 3; ++i)
    if (!mul(a[i], b, v[i])) return false;
  return true;
}
auto sum(const V& a, const V& b, V& v) -> bool {
  for (std::size_t i = 0; i < 3; ++i)
    if (!add(a[i], b[i], v[i])) return false;
  return true;
}
auto cross(const V& a, const V& b, V& v) -> bool {
  T x, y;
  for (std::size_t i = 0; i < 3; ++i)
    if (!mul(a[(i + 1) % 3], b[(i + 2) % 3], x) ||
        !mul(a[(i + 2) % 3], b[(i + 1) % 3], y) || !sub(x, y, v[i]))
      return false;
  return true;
}
auto distance(const V& a, const V& b, T& t) -> bool {
  V d;
  return subtract(a, b, d) && norm(d, t);
}
void unavailable(std::string_view why) {
  aggregate(totals.unavailable, 1);
  std::cout << "ORACLE unavailable: " << why << '\n';
}
auto component(const RigidVector3& p, std::size_t i) -> double {
  return i == 0 ? p.x : i == 1 ? p.y : p.z;
}
auto supported(const Scalar& b) -> bool {
  return b.supported && std::isfinite(b.lower) && std::isfinite(b.upper) &&
         b.lower <= b.upper;
}
void compare(const T& t, const Scalar& b) {
  check(valid(t) && supported(b),
        "Earned independent/producer scalar bounds finite ordered");
  if (!valid(t) || !supported(b)) return;
  aggregate(totals.available, 1);
  const bool overlap = t.lo <= static_cast<long double>(b.upper) &&
                       t.hi >= static_cast<long double>(b.lower);
  check(overlap,
        "Independent nominal corroboration overlaps producer enclosure");
  if (t.lo >= static_cast<long double>(b.lower) &&
      t.hi <= static_cast<long double>(b.upper))
    aggregate(totals.enclosed, 1);
  else
    aggregate(totals.overlap, 1);
}
void compare_point(const V& v, const BoardingPlantedLegPointBounds& b) {
  for (std::size_t i = 0; i < 3; ++i)
    compare(v[i], Scalar{component(b.lower, i), component(b.upper, i), true});
}
[[gnu::noinline]] auto reconstruct(double y, Reconstruction& r) -> bool {
  Workspace w;
  auto& s = w.scalars;
  auto& v = w.vectors;
  const std::array<double, 32> coefficients{0,
                                            1,
                                            2,
                                            .125,
                                            .14,
                                            .47285,
                                            .47478,
                                            kBoardingSelfHipLengthMetres,
                                            .105,
                                            .10,
                                            0,
                                            0,
                                            0,
                                            0,
                                            0,
                                            0,
                                            0,
                                            0,
                                            0,
                                            0,
                                            0,
                                            .24,
                                            .12,
                                            .18,
                                            static_cast<double>(-230000) * 1e-6,
                                            .16,
                                            -.0075,
                                            -.55,
                                            -.17,
                                            .19,
                                            -.14,
                                            -1.14};
  for (std::size_t i = 0; i < s.size(); ++i)
    point(coefficients[i], s[i]);
  if (!add(s[25], s[25], r.points[0][0]) ||
      !add(r.points[0][0], s[26], r.points[0][0]) ||
      !add(s[27], s[28], r.points[0][2]) ||
      !add(r.points[0][2], s[29], r.points[0][2]))
    return false;
  point(y, r.points[0][1]);
  if (!square(s[3], s[10]) || !add(s[1], s[10], s[11]) ||
      !sub(s[1], s[10], s[18]) || !divide(s[18], s[11], s[12]) ||
      !mul(s[2], s[3], s[18]) || !neg(s[18], s[18]) ||
      !divide(s[18], s[11], s[13]))
    return false;
  r.frame[0] = {s[12], s[0], s[13]};
  r.frame[1] = {s[0], s[1], s[0]};
  if (!neg(s[13], s[18])) return false;
  r.frame[2] = {s[18], s[0], s[12]};
  if (!scale(r.frame[0], s[30], v[0]) || !sum(r.points[0], v[0], r.points[1]))
    return false;
  if (!add(s[25], s[30], v[1][0]) || !add(v[1][0], s[25], v[1][0]) ||
      !add(s[24], s[9], v[1][1]))
    return false;
  v[1][2] = s[31];
  if (!subtract(v[1], r.points[1], v[0]) || !norm(v[0], s[14]) ||
      !square(v[0][0], s[18]) || !square(v[0][1], s[19]) ||
      !add(s[18], s[19], s[18]) || !root(s[18], s[15]))
    return false;
  if (!square(s[5], s[18]) || !square(s[6], s[19]) ||
      !sub(s[18], s[19], s[20]) || !add(s[20], s[14], s[20]) ||
      !mul(s[2], s[14], s[19]) || !divide(s[20], s[19], s[16]))
    return false;
  if (!square(s[16], s[19]) || !mul(s[19], s[14], s[19]) ||
      !sub(s[18], s[19], s[19]) || !divide(s[19], s[14], s[19]) ||
      !root(s[19], s[17]))
    return false;
  if (!neg(v[0][1], s[18]) || !divide(s[18], s[15], v[2][0]) ||
      !divide(v[0][0], s[15], v[2][1]))
    return false;
  v[2][2] = s[0];
  if (!cross(v[2], v[0], v[3]) || !scale(v[0], s[16], v[4]) ||
      !scale(v[3], s[17], v[5]) || !sum(v[4], v[5], v[4]) ||
      !sum(r.points[1], v[4], r.points[2]) ||
      !subtract(r.points[2], r.points[1], v[6]))
    return false;
  for (std::size_t i = 0; i < 3; ++i)
    if (!divide(v[6][i], s[5], r.unit[i])) return false;
  for (std::size_t i = 0; i < 3; ++i)
    if (!dot(r.frame[i], r.unit, r.xi[i]) || !mul(s[7], r.xi[i], r.center[i]))
      return false;
  if (!add(r.center[0], s[30], r.center[0]) || !square(s[8], r.radius))
    return false;
  for (std::size_t i = 0; i < 3; ++i)
    if (!absolute(r.xi[i], s[18 + i]) || !mul(s[21 + i], s[18 + i], v[7][i]))
      return false;
  if (!add(v[7][0], v[7][1], s[18]) || !add(s[18], v[7][2], s[18]) ||
      !mul(s[4], r.xi[0], s[19]) || !add(s[18], s[19], r.owner))
    return false;
  r.geometry_available = true;
  return true;
}
constexpr std::array<std::array<std::uint8_t, 2>, 12> edges{{{0, 1},
                                                             {0, 2},
                                                             {0, 4},
                                                             {1, 3},
                                                             {1, 5},
                                                             {2, 3},
                                                             {2, 6},
                                                             {3, 7},
                                                             {4, 5},
                                                             {4, 6},
                                                             {5, 7},
                                                             {6, 7}}};
void vertex(std::uint8_t id, V& v) {
  for (std::size_t i = 0; i < 3; ++i)
    point((id & (1U << i) ? 1. : -1.) * (i == 0   ? .24
                                         : i == 1 ? .12
                                                  : .18),
          v[i]);
}
auto residual(const V& p, const Reconstruction& r, T& t) -> bool {
  V v = p;
  T a, l;
  point(.14, a);
  point(kBoardingSelfHipLengthMetres, l);
  return add(v[0], a, v[0]) && dot(v, r.xi, t) && sub(t, l, t);
}
auto source_feature(const Feature& f, const Reconstruction& r, V& out) -> bool {
  if (f.source_feature < 8) {
    vertex(f.source_feature, out);
    return true;
  }
  if (f.source_feature >= 20) return false;
  V a, b;
  T fa, fb, den, lambda;
  const auto ids = edges[f.source_feature - 8];
  vertex(ids[0], a);
  vertex(ids[1], b);
  if (!residual(a, r, fa) || !residual(b, r, fb) ||
      !((fa.hi < 0 && fb.lo > 0) || (fb.hi < 0 && fa.lo > 0)) ||
      !sub(fa, fb, den) || !divide(fa, den, lambda) || lambda.lo < 0 ||
      lambda.hi > 1 || !subtract(b, a, out) || !scale(out, lambda, out) ||
      !sum(a, out, out))
    return false;
  return true;
}
[[gnu::noinline]] void record_audit(const Data& d, const Reconstruction& r) {
  RecordWorkspace w;
  for (std::size_t i = 0; i < d.feature_count; ++i) {
    const auto& f = d.features[i];
    if (!source_feature(f, r, w.vectors[0])) {
      unavailable("source feature denominator");
      continue;
    }
    for (std::size_t a = 0; a < 3; ++a)
      compare(w.vectors[0][a], f.point[a]);
    if (i < d.feature_distance_count) {
      if (distance(w.vectors[0], r.center, w.scalars[0]))
        compare(w.scalars[0], d.feature_distances[i]);
      else
        unavailable("source point-distance arithmetic");
    }
  }
  std::size_t k{};
  for (std::size_t i = 0; i < d.feature_count; ++i)
    for (std::size_t j = i + 1;
         j < d.feature_count && k < d.chord_distance_count; ++j, ++k) {
      if (!source_feature(d.features[i], r, w.vectors[0]) ||
          !source_feature(d.features[j], r, w.vectors[1]) ||
          !subtract(w.vectors[1], w.vectors[0], w.vectors[2]) ||
          !subtract(r.center, w.vectors[0], w.vectors[3]) ||
          !norm(w.vectors[2], w.scalars[0]) ||
          !dot(w.vectors[3], w.vectors[2], w.scalars[1]) ||
          !divide(w.scalars[1], w.scalars[0], w.scalars[2])) {
        unavailable("source chord denominator");
        continue;
      }
      w.scalars[2] = {std::clamp(w.scalars[2].lo, 0.L, 1.L),
                      std::clamp(w.scalars[2].hi, 0.L, 1.L)};
      if (!scale(w.vectors[2], w.scalars[2], w.vectors[4]) ||
          !subtract(w.vectors[3], w.vectors[4], w.vectors[4]) ||
          !norm(w.vectors[4], w.scalars[3])) {
        unavailable("source chord arithmetic");
        continue;
      }
      compare(w.scalars[3], d.chord_distances[k]);
    }
}
auto candidate(const V& p, std::uint8_t active, const Reconstruction& r,
               bool& have, T& best) -> bool {
  bool ambiguous{}, outside{};
  for (std::size_t a = 0; a < 3; ++a)
    if (!(active & (1U << a))) {
      const long double h = static_cast<long double>(a == 0   ? .24
                                                     : a == 1 ? .12
                                                              : .18);
      outside = outside || p[a].lo > h || p[a].hi < -h;
      ambiguous = ambiguous || p[a].lo < -h || p[a].hi > h;
    }
  if (outside) return true;
  if (ambiguous) return false;
  T d;
  if (!distance(p, r.center, d)) return false;
  if (!have) {
    best = d;
    have = true;
  } else {
    best = {std::min(best.lo, d.lo), std::min(best.hi, d.hi)};
  }
  return true;
}
[[gnu::noinline]] auto section_distance(Reconstruction& r) -> bool {
  SectionWorkspace w;
  auto& s = w.scalars;
  auto& v = w.vectors;
  point(1, s[0]);
  point(.14, s[1]);
  point(kBoardingSelfHipLengthMetres, s[2]);
  if (r.owner.hi <= s[2].lo) {
    r.section_inapplicable = true;
    return false;
  }
  if (!(r.owner.lo > s[2].hi) || !mul(s[1], r.xi[0], s[3]) ||
      !sub(s[2], s[3], s[3]))
    return false;
  bool have{};
  if (!candidate(r.center, 0, r, have, r.distance)) return false;
  for (std::size_t face = 0; face < 6; ++face) {
    const auto a = face / 2;
    point((face % 2 ? 1. : -1.) * (a == 0 ? .24 : a == 1 ? .12 : .18), s[4]);
    if (!square(r.xi[a], s[5]) || !sub(s[0], s[5], s[5]) || !(s[5].lo > 0) ||
        !sub(s[4], r.center[a], s[6]) || !divide(s[6], s[5], s[6]))
      return false;
    for (std::size_t k = 0; k < 3; ++k) {
      point(k == a ? 1. : 0., s[7]);
      if (!mul(r.xi[a], r.xi[k], s[8]) || !sub(s[7], s[8], s[8]) ||
          !mul(s[6], s[8], s[8]) || !add(r.center[k], s[8], v[0][k]))
        return false;
    }
    v[0][a] = s[4];
    if (!candidate(v[0], static_cast<std::uint8_t>(1U << a), r, have,
                   r.distance))
      return false;
  }
  for (std::size_t first = 0; first < 6; ++first)
    for (std::size_t second = first + 1; second < 6; ++second) {
      const auto a = first / 2, b = second / 2;
      if (a == b) continue;
      const auto k = 3 - a - b;
      point((first % 2 ? 1. : -1.) * (a == 0   ? .24
                                      : a == 1 ? .12
                                               : .18),
            v[0][a]);
      point((second % 2 ? 1. : -1.) * (b == 0   ? .24
                                       : b == 1 ? .12
                                                : .18),
            v[0][b]);
      if (!mul(r.xi[a], v[0][a], s[7]) || !mul(r.xi[b], v[0][b], s[8]) ||
          !sub(s[3], s[7], s[7]) || !sub(s[7], s[8], s[7]) ||
          !divide(s[7], r.xi[k], v[0][k]))
        return false;
      if (!candidate(v[0], static_cast<std::uint8_t>((1U << a) | (1U << b)), r,
                     have, r.distance))
        return false;
    }
  r.distance_available = have;
  return have;
}
struct Digest {
  std::uint64_t value{14695981039346656037ULL};
  void integer(std::uint64_t x, std::size_t bytes) {
    for (std::size_t i = 0; i < bytes; ++i) {
      value ^= x & 255U;
      value *= 1099511628211ULL;
      x >>= 8;
    }
  }
  void boolean(bool b) { integer(b ? 1 : 0, 1); }
  template <class E> void enumeration(E e) {
    integer(static_cast<std::uint8_t>(e), 1);
  }
  void real(double d) {
    check(std::isfinite(d), "Digest only earned finite scalar");
    if (!std::isfinite(d)) return;
    integer(std::bit_cast<std::uint64_t>(d), 8);
  }
  void scalar(const Scalar& s) {
    boolean(s.supported);
    if (s.supported) {
      check(supported(s), "Digest ordered supported bounds");
      if (!supported(s)) return;
      real(s.lower);
      real(s.upper);
    }
  }
  void key(const LowerCockpitTriangleKey& k) {
    enumeration(k.buffer);
    integer(k.group, 4);
    integer(k.triangle, 4);
  }
  void point_bounds(const BoardingPlantedLegPointBounds& p) {
    for (std::size_t i = 0; i < 3; ++i) {
      real(component(p.lower, i));
      real(component(p.upper, i));
    }
  }
};
static_assert(sizeof(Digest) <= 8);
struct Summary {
  std::uint64_t digest{};
  std::size_t required{}, owned{};
  std::array<std::uint16_t, 4> used{};
  std::uint32_t original_flags{};
  std::uint16_t availability{}, original_pair{65535};
  State state{};
  Outcome outcome{};
  Condition condition{};
  BoardingIntermediateEndpoint04State original_state{};
  BoardingIntermediateEndpoint04Condition original_condition{};
  BoardingIntermediateEndpoint04SelfStage original_stage{};
  Stage stage{}, operation_stage{};
  std::uint16_t operation_row{65535}, chord_cursor{65535};
  std::uint8_t capture_cursor{255}, feature_cursor{255}, operation_cursor{255},
      original_region{255}, original_axis{255}, original_sign{255};
  std::array<std::uint8_t, 3> fixed{};
  bool exists{}, original_available{}, arithmetic_supported{}, complete{},
      owner_branch{}, center_branch{}, feature_branch{};
};
static_assert(sizeof(Summary) <= 128);
auto low_bits(std::size_t n) -> std::uint64_t {
  return n == 0 ? 0 : (std::uint64_t{1} << n) - 1;
}
auto get_limit(const Limits& l, std::size_t f) -> std::size_t {
  return f == 0   ? l.capture_checks
         : f == 1 ? l.feature_checks
         : f == 2 ? l.chord_checks
         : f == 3 ? l.operations
                  : l.output_bytes;
}
void set_limit(Limits& l, std::size_t f, std::size_t n) {
  if (f < 4 && n > std::numeric_limits<std::uint16_t>::max())
    throw std::runtime_error("Limit narrowing");
  if (f == 0)
    l.capture_checks = static_cast<std::uint16_t>(n);
  else if (f == 1)
    l.feature_checks = static_cast<std::uint16_t>(n);
  else if (f == 2)
    l.chord_checks = static_cast<std::uint16_t>(n);
  else if (f == 3)
    l.operations = static_cast<std::uint16_t>(n);
  else
    l.output_bytes = n;
}
void source_identity(const Data& d, const Provider& provider) {
  const auto& p = d.source_identities[0];
  const auto& s = d.source_identities[1];
  const auto* original = provider.summary();
  check(original && original->complete,
        "Captured metadata retains genuine source owner");
  if (original)
    for (std::size_t i = 0; i < 2; ++i) {
      const auto& a = d.source_identities[i];
      const auto& b = original->sources[i];
      check(a.keys == b.keys && a.evaluated_faces == b.evaluated_faces &&
                a.name == b.name && a.object == b.object &&
                a.inventory_provenance == b.inventory_provenance &&
                a.plane == b.plane,
            "Full source identity copy matches genuine provider metadata");
    }
  check(p.object == 10 &&
            p.name == "CRAFT | pilot transition intermediate step" &&
            p.inventory_provenance == 735 &&
            p.evaluated_faces ==
                std::array<std::optional<std::uint32_t>, 2>{182, 183},
        "PORT original source metadata");
  check(s.object == 124 && s.name == "CABIN | cockpit transition step" &&
            provider.summary() &&
            s.evaluated_faces == provider.summary()->sources[1].evaluated_faces,
        "STAR source optional evaluated faces copied from genuine provider");
  check(p.keys ==
                std::array<LowerCockpitTriangleKey, 2>{
                    {{LowerCockpitContactBuffer::halo, 0, 662},
                     {LowerCockpitContactBuffer::halo, 0, 663}}} &&
            s.keys ==
                std::array<LowerCockpitTriangleKey, 2>{
                    {{LowerCockpitContactBuffer::original, 0, 62119},
                     {LowerCockpitContactBuffer::original, 0, 62120}}},
        "Original source keys use distinct exact namespaces");
  check(p.plane == static_cast<double>(-230000) * 1e-6 &&
            s.plane == static_cast<double>(-160000) * 1e-6,
        "Original stored source plane coefficients");
}
void source_hash(Digest& h, const BoardingIntermediatePauseSourceIdentity& s) {
  for (const auto& k : s.keys)
    h.key(k);
  for (const auto& f : s.evaluated_faces) {
    h.boolean(f.has_value());
    if (f) h.integer(*f, 4);
  }
  h.integer(s.object, 4);
  h.boolean(s.inventory_provenance.has_value());
  if (s.inventory_provenance) h.integer(*s.inventory_provenance, 4);
  h.real(s.plane);
}
void work_hash(Digest& h, const BoardingIntermediateEndpoint04Counters& w) {
  const std::array values{w.phase_calls,
                          w.phase.graphs,
                          w.phase.legs,
                          w.phase.bodies,
                          w.phase.sectors,
                          w.phase.timing,
                          w.source_guards,
                          w.projection_guards,
                          w.definition_guards,
                          w.sole_extrema,
                          w.source_coordinates,
                          w.intersection_operations,
                          w.midpoint_operations,
                          w.allocation_operations,
                          w.pressure_candidates,
                          w.disk_edges,
                          w.self_body_guards,
                          w.self_pairs,
                          w.self_axes,
                          w.self_signed_trials,
                          w.self_owners,
                          w.self_hip_complements,
                          w.unit_axis_operations,
                          w.construction_guards,
                          w.construction_operations};
  for (auto x : values)
    h.integer(x, 8);
}
void slice_hash(Digest& h,
                const BoardingIntermediateEndpoint04SliceEvidence& s) {
  for (auto x : s.operation_attempted)
    h.integer(x, 8);
  for (auto x : s.operation_written)
    h.integer(x, 8);
  h.integer(s.guard_attempted, 8);
  h.integer(s.guard_written, 8);
  h.scalar(s.limiting_bound);
  if (s.complete) {
    h.real(s.y);
    h.real(s.lo);
    h.real(s.hi);
  }
  h.integer(s.operation, 1);
  h.integer(s.guard, 1);
  h.integer(s.side, 1);
  h.enumeration(s.condition);
  h.boolean(s.arithmetic_supported);
  h.boolean(s.complete);
  h.integer(s.preflight_guards, 1);
  h.integer(s.zero_mask, 1);
}
void refusal_hash(Digest& h, const BoardingIntermediateEndpoint04Refusal& r) {
  h.enumeration(r.condition);
  h.enumeration(r.predicate_condition);
  h.scalar(r.limiting_bound);
  h.integer(r.phase.depth, 8);
  h.enumeration(r.phase.condition);
  h.enumeration(r.phase.predicate_condition);
  h.boolean(r.phase.side.has_value());
  if (r.phase.side) h.integer(*r.phase.side, 8);
  // The phase bounds have no supported flag; default/legacy numeric values
  // are not consumed as a new geometric fact.
  for (const auto& x : {r.side, r.edge, r.axis}) {
    h.boolean(x.has_value());
    if (x) h.integer(*x, 8);
  }
  h.boolean(r.source_key.has_value());
  if (r.source_key) h.key(*r.source_key);
  h.integer(r.self_pair, 2);
  h.integer(r.operation, 1);
  h.integer(r.self_region, 1);
  h.integer(r.self_axis, 1);
  h.integer(r.self_sign, 1);
  h.enumeration(r.self_stage);
  h.boolean(r.source_edge);
}
void part_hash(Digest& h, const BoardingRoutePhasePartBinding& p) {
  h.enumeration(p.id);
  h.integer(p.reservation.index(), 1);
  if (const auto* b =
          std::get_if<BoardingRoutePhaseBoxBinding>(&p.reservation)) {
    h.enumeration(b->center);
    for (std::size_t i = 0; i < 3; ++i)
      h.real(component(b->half_size_metres, i));
    h.enumeration(b->frame);
  } else if (const auto* c = std::get_if<BoardingPlantedBodyCapsuleBinding>(
                 &p.reservation)) {
    h.enumeration(c->start);
    h.enumeration(c->end);
    h.real(c->radius_metres);
  } else
    check(false, "Original selected part variant bound");
  h.enumeration(p.mass.first);
  h.enumeration(p.mass.second);
  h.integer(p.mass.weight, 4);
}
void minimum_provenance(const Data& d) {
  // This is an earned-record consistency fold, not a second geometric oracle.
  if (d.feature_distance_count == 0 && d.chord_distance_count == 0) {
    check(d.minimum_lower_kind == 255 && d.minimum_upper_kind == 255 &&
              d.minimum_lower_index == 65535 && d.minimum_upper_index == 65535,
          "No fabricated minimum provenance without a folded record");
    return;
  }
  bool have{};
  double lo{}, hi{};
  std::uint8_t lower_kind{255}, upper_kind{255};
  std::uint16_t lower_index{65535}, upper_index{65535};
  for (std::uint8_t kind = 0; kind < 2; ++kind) {
    const auto count =
        kind == 0 ? d.feature_distance_count : d.chord_distance_count;
    for (std::uint16_t i = 0; i < count; ++i) {
      const auto& b = kind == 0 ? d.feature_distances[i] : d.chord_distances[i];
      if (!supported(b)) return;
      if (!have || b.lower < lo) {
        lo = b.lower;
        lower_kind = kind;
        lower_index = i;
      }
      if (!have || b.upper < hi) {
        hi = b.upper;
        upper_kind = kind;
        upper_index = i;
      }
      have = true;
    }
  }
  check(supported(d.minimum_distance_squared) &&
            d.minimum_distance_squared.lower == lo &&
            d.minimum_distance_squared.upper == hi &&
            d.minimum_lower_kind == lower_kind &&
            d.minimum_upper_kind == upper_kind &&
            d.minimum_lower_index == lower_index &&
            d.minimum_upper_index == upper_index,
        "Earned componentwise minimum provenance preserves first-on-equality");
}
auto payload_valid(const Data& d, const Provider& provider) -> bool {
  const bool counts =
      d.feature_count <= 20 && d.feature_distance_count <= d.feature_count &&
      d.chord_distance_count <= 190 &&
      d.chord_distance_count <= d.feature_count * (d.feature_count - 1) / 2 &&
      (d.availability & ~std::uint16_t{8191}) == 0;
  check(counts, "Data prefix capacities and availability bits");
  if (!counts) return false;
  if (d.availability & 1U) source_identity(d, provider);
  if (d.availability & (1U << 1))
    check(d.original_work.source_guards == 64 &&
              d.original_work.construction_guards == 37 &&
              d.original_work.construction_operations == 226,
          "C03 copied exact original construction/source work");
  if (d.availability & (1U << 2)) {
    const auto& s = d.original_slice;
    check(s.complete && s.arithmetic_supported &&
              s.guard_written == 0x1fffffffffULL &&
              s.operation_written ==
                  std::array<std::uint64_t, 4>{UINT64_MAX, UINT64_MAX,
                                               UINT64_MAX, 0x3ffffffffULL},
          "C03 complete original construction masks");
    if (s.complete)
      check(s.arithmetic_supported && std::isfinite(s.y) &&
                std::isfinite(s.lo) && std::isfinite(s.hi) && s.lo < s.y &&
                s.y < s.hi,
            "Earned actual original strict slice membership");
    check((s.guard_written & ~s.guard_attempted) == 0,
          "Copied full original guard masks");
    for (std::size_t i = 0; i < 4; ++i)
      check((s.operation_written[i] & ~s.operation_attempted[i]) == 0,
            "Copied four-word original slice mask containment");
  }
  if ((d.availability & (1U << 3)) && d.original_first_refusal &&
      !d.original_first_refusal->source_name.empty())
    check(d.original_first_refusal->source_name ==
                  "CRAFT | pilot transition intermediate step" ||
              d.original_first_refusal->source_name ==
                  "CABIN | cockpit transition step",
          "Copied refusal source name matches immutable original declarations");
  if (d.availability & (1U << 4)) {
    check(d.original_work.phase_calls == 1 &&
              d.original_work.phase.graphs == 1 &&
              d.original_work.phase.legs == 2 &&
              d.original_work.phase.bodies == 1 &&
              d.original_work.phase.sectors == 3 &&
              d.original_work.phase.timing == 6,
          "C06 retained exact one original phase graph work");
    const auto* box = std::get_if<BoardingRoutePhaseBoxBinding>(
        &d.selected_parts[0].reservation);
    const auto* capsule = std::get_if<BoardingPlantedBodyCapsuleBinding>(
        &d.selected_parts[1].reservation);
    check(box && capsule &&
              d.selected_parts[0].id == BoardingBodyPartId::pelvis &&
              d.selected_parts[1].id == BoardingBodyPartId::port_thigh,
          "Original selected pelvis/PORT thigh tuple");
    if (box && capsule)
      check(box->center == BoardingPlantedBodyPointId::root &&
                box->frame == BoardingRoutePhaseFrame::root &&
                box->half_size_metres == RigidVector3{.24, .12, .18} &&
                capsule->start == BoardingPlantedBodyPointId::port_hip &&
                capsule->end == BoardingPlantedBodyPointId::port_knee &&
                capsule->radius_metres == .105,
            "Original full capsule and proper nominal root-frame binding");
    check(d.pelvis_half_size_metres == RigidVector3{.24, .12, .18} &&
              d.hip_offset_metres == .14 && d.thigh_length_metres == .47285 &&
              d.radius_metres == .105 &&
              d.axial_limit_metres == kBoardingSelfHipLengthMetres,
          "Stored dimensions and original hex axial limit");
  }
  for (std::size_t i = 0; i < d.feature_count; ++i) {
    const auto& f = d.features[i];
    check(f.ready && f.source_feature < 20,
          "Only ready earned original feature IDs");
    if (!f.ready || f.source_feature >= 20) return false;
    if (f.source_feature < 8)
      check(f.kind == 0 &&
                f.endpoints == std::array<std::uint8_t, 2>{f.source_feature,
                                                           f.source_feature},
            "Original on-plane vertex provenance");
    else
      check(f.kind == 1 && f.endpoints == edges[f.source_feature - 8],
            "Original crossing edge provenance");
    for (const auto& b : f.point)
      check(supported(b), "Earned feature coordinate enclosure");
    if (i)
      check(d.features[i - 1].source_feature < f.source_feature,
            "Active original feature order without guessed deduplication");
  }
  for (std::size_t i = 0; i < d.feature_distance_count; ++i)
    check(supported(d.feature_distances[i]), "Earned point-distance prefix");
  for (std::size_t i = 0; i < d.chord_distance_count; ++i)
    check(supported(d.chord_distances[i]), "Earned chord-distance prefix");
  for (std::size_t i = 0; i < 3; ++i) {
    if (d.availability & (1U << 5))
      for (std::size_t a = 0; a < 3; ++a)
        check(std::isfinite(component(d.points[i].lower, a)) &&
                  std::isfinite(component(d.points[i].upper, a)) &&
                  component(d.points[i].lower, a) <=
                      component(d.points[i].upper, a),
              "Earned actual ROOT/H/K finite ordered capture");
    if (d.availability & (1U << 6))
      for (std::size_t a = 0; a < 3; ++a)
        check(std::isfinite(component(d.root_columns[i].lower, a)) &&
                  std::isfinite(component(d.root_columns[i].upper, a)) &&
                  component(d.root_columns[i].lower, a) <=
                      component(d.root_columns[i].upper, a),
              "Earned actual root column finite ordered capture");
    if (d.availability & (1U << 7))
      check(supported(d.unit_axis[i]),
            "Earned actual Unit24 supported capture");
    if (d.availability & (1U << 8))
      check(supported(d.xi[i]), "Earned xi supported capture");
    if (d.availability & (1U << 9))
      check(supported(d.center[i]), "Earned center supported capture");
  }
  minimum_provenance(d);
  return true;
}
void data_hash(Digest& h, const Data& d) {
  h.integer(d.availability, 2);
  if (d.availability & (1U << 1)) work_hash(h, d.original_work);
  if (d.availability & (1U << 2)) slice_hash(h, d.original_slice);
  if (d.availability & (1U << 3)) {
    h.boolean(d.original_first_refusal.has_value());
    if (d.original_first_refusal) refusal_hash(h, *d.original_first_refusal);
  }
  if (d.availability & 1U)
    for (const auto& x : d.source_identities)
      source_hash(h, x);
  if (d.availability & (1U << 4))
    for (const auto& p : d.selected_parts)
      part_hash(h, p);
  if (d.availability & (1U << 5))
    for (const auto& p : d.points)
      h.point_bounds(p);
  if (d.availability & (1U << 6))
    for (const auto& p : d.root_columns)
      h.point_bounds(p);
  if (d.availability & (1U << 7))
    for (const auto& b : d.unit_axis)
      h.scalar(b);
  if (d.availability & (1U << 8))
    for (const auto& b : d.xi)
      h.scalar(b);
  if (d.availability & (1U << 9))
    for (const auto& b : d.center)
      h.scalar(b);
  if (d.availability & (1U << 4)) {
    for (std::size_t i = 0; i < 3; ++i)
      h.real(component(d.pelvis_half_size_metres, i));
    for (auto x : {d.hip_offset_metres, d.thigh_length_metres, d.radius_metres,
                   d.axial_limit_metres})
      h.real(x);
  }
  for (std::size_t i = 0; i < d.feature_count; ++i) {
    const auto& f = d.features[i];
    for (const auto& b : f.point)
      h.scalar(b);
    h.integer(f.source_feature, 1);
    h.integer(f.kind, 1);
    for (auto e : f.endpoints)
      h.integer(e, 1);
    h.boolean(f.ready);
  }
  for (std::size_t i = 0; i < d.feature_distance_count; ++i)
    h.scalar(d.feature_distances[i]);
  for (std::size_t i = 0; i < d.chord_distance_count; ++i)
    h.scalar(d.chord_distances[i]);
  if (d.availability & (1U << 10)) h.scalar(d.owner_extent);
  if (d.availability & (1U << 11)) h.scalar(d.radius_squared);
  h.scalar(d.minimum_distance_squared);
  h.integer(d.chord_distance_count, 2);
  h.integer(d.feature_count, 1);
  h.integer(d.feature_distance_count, 1);
  h.integer(d.minimum_lower_kind, 1);
  h.integer(d.minimum_upper_kind, 1);
  h.integer(d.minimum_lower_index, 2);
  h.integer(d.minimum_upper_index, 2);
  h.boolean(d.owner_branch);
  h.boolean(d.center_branch);
  h.boolean(d.feature_branch);
  h.boolean(d.minimum_complete);
}
void accounting(const Diagnostic& d, const Limits& l, Summary& s) {
  check(static_cast<unsigned>(d.state) <= 4 &&
            static_cast<unsigned>(d.outcome) <= 3 &&
            static_cast<unsigned>(d.condition) <= 14 &&
            static_cast<unsigned>(d.stage) <= 11 &&
            static_cast<unsigned>(d.operation_stage) <= 11,
        "Registered enum bounds");
  check(d.work.preflight_guards == 1 && d.work.return_metadata_checks <= 1 &&
            d.work.endpoint_calls <= 1,
        "Fixed guard/metadata/one original call counts");
  const std::array<std::uint16_t, 4> used{
      d.work.capture_checks, d.work.feature_checks, d.work.chord_checks,
      d.work.operations};
  for (std::size_t i = 0; i < 4; ++i) {
    check(used[i] <= get_limit(l, i), "Lower-only successful charge bound");
    aggregate(totals.work[i], used[i]);
  }
  aggregate(totals.fixed[0], d.work.preflight_guards);
  aggregate(totals.fixed[1], d.work.return_metadata_checks);
  aggregate(totals.fixed[2], d.work.endpoint_calls);
  check(d.required_output_bytes ==
                boarding_hip_joint_plane_method01_required_output_bytes() &&
            d.output_capacity_bytes ==
                2 * sizeof(Expected) + (d.data ? sizeof(Data) : 0) &&
            d.output_capacity_bytes <= l.output_bytes,
        "Exact REQUIRED versus owned output capacities");
  check((d.original_flags & ~std::uint32_t{32767}) == 0 &&
            d.work.operations <= 7049,
        "Original earned flags and finite logical operation upper bound");
  if (!d.original_available)
    check(d.original_flags == 0 && !d.data && d.work.capture_checks == 0,
          "No invented original authority before a child return");
  check(d.capture_attempted == d.capture_written &&
            d.capture_attempted == low_bits(d.work.capture_checks),
        "Evaluated capture tuple written even false; no failed-charge bit");
  check((d.feature_written & ~d.feature_attempted) == 0 &&
            (d.feature_attempted & ~std::uint32_t{1048575}) == 0 &&
            std::popcount(d.feature_attempted) == d.work.feature_checks,
        "Finite feature attempted/written count");
  check((d.operation_written & ~d.operation_attempted) == 0 &&
            (d.operation_attempted & ~low_bits(35)) == 0,
        "Current-row supported mask subset");
  if (d.operation_stage == Stage::not_run)
    check(d.operation_row == 65535 && d.operation_cursor == 255 &&
              d.operation_attempted == 0 && d.operation_written == 0,
          "Absent arithmetic ledger defaults");
  else {
    const auto stage = d.operation_stage;
    const std::size_t size = stage == Stage::chart    ? 15
                             : stage == Stage::vertex ? 8
                             : stage == Stage::center
                                 ? (d.operation_row == 0 ? 8 : 1)
                             : stage == Stage::vertex_distance ? 9
                             : stage == Stage::edge            ? 11
                             : stage == Stage::chord           ? 35
                                                               : 0;
    check(size > 0 && d.operation_cursor < size,
          "Exact fused row/cursor bounds");
    if (stage == Stage::chart) check(d.operation_row == 0, "Chart row0");
    if (stage == Stage::vertex)
      check(d.operation_row < 8, "Original vertex row ID");
    if (stage == Stage::center)
      check(d.operation_row <= 1, "Center0 or distinct zero-COPY1");
    if (stage == Stage::vertex_distance)
      check(d.operation_row < 20, "Active V point-distance9 row");
    if (stage == Stage::edge)
      check(d.operation_row < 12, "Original edge11 row");
    if (stage == Stage::chord)
      check(d.operation_row < 190, "Lexicographic chord35 row");
    if (size)
      check((d.operation_attempted & ~low_bits(size)) == 0,
            "No bit beyond the actual current row");
  }
  if (d.original_available &&
      (d.original_state == BoardingIntermediateEndpoint04State::capacity ||
       d.original_state == BoardingIntermediateEndpoint04State::unsupported))
    check(!d.data && d.work.capture_checks == 0 &&
              d.state == (d.original_state ==
                                  BoardingIntermediateEndpoint04State::capacity
                              ? State::capacity
                              : State::unsupported),
          "Original hard state never downgraded or replaced by allocation");
  bool good = true;
  if (d.data) good = payload_valid(*d.data, d.source);
  if (d.data && good) {
    const auto& p = *d.data;
    check(p.feature_count <= d.work.feature_checks &&
              p.chord_distance_count <= d.work.chord_checks,
          "Earned output prefixes bounded by successful charges");
    if (d.evidence_complete) {
      check(d.state == State::evidence_complete && d.work.capture_checks == 8 &&
                d.capture_cursor == 7,
            "C08 complete evidence with actual terminal capture7");
      if (p.owner_branch) {
        check((p.availability & (1U << 10)) && supported(p.owner_extent) &&
                  p.owner_extent.upper <= p.axial_limit_metres &&
                  d.outcome == Outcome::contained &&
                  d.condition == Condition::none &&
                  !(p.availability & ((1U << 11) | (1U << 12))),
              "Owned whole-box shortcut neither reads nor fabricates "
              "radius/minimum");
      } else {
        check((p.availability & ((1U << 10) | (1U << 11) | (1U << 12))) ==
                      ((1U << 10) | (1U << 11) | (1U << 12)) &&
                  supported(p.owner_extent) && supported(p.radius_squared) &&
                  supported(p.minimum_distance_squared) &&
                  p.owner_extent.lower > p.axial_limit_metres &&
                  p.radius_squared.lower > 0 && p.minimum_complete,
              "Supported interior/distance theorem gates");
        if (supported(p.minimum_distance_squared) &&
            supported(p.radius_squared)) {
          const auto expected =
              p.minimum_distance_squared.upper < p.radius_squared.lower
                  ? Outcome::strict_unowned_exists
              : p.minimum_distance_squared.lower >= p.radius_squared.upper
                  ? Outcome::contained
                  : Outcome::unresolved;
          check(d.outcome == expected &&
                    d.condition == (expected == Outcome::unresolved
                                        ? Condition::classification_unresolved
                                        : Condition::none),
                "Exact strict/equality/straddle publication rule");
        }
        if (p.center_branch)
          check(p.minimum_distance_squared.lower == 0 &&
                    p.minimum_distance_squared.upper == 0,
                "Certified center branch exact zero minimum");
        if (p.feature_branch)
          check(d.work.feature_checks == 20 && d.feature_written == 1048575 &&
                    p.feature_count >= 3 &&
                    p.feature_distance_count == p.feature_count &&
                    p.chord_distance_count ==
                        p.feature_count * (p.feature_count - 1) / 2,
                "Complete finite section all features and every pair chord");
      }
    }
  }
  Digest h;
  h.boolean(d.data != nullptr);
  if (d.data && good) data_hash(h, *d.data);
  h.integer(d.work.preflight_guards, 1);
  h.integer(d.work.return_metadata_checks, 1);
  h.integer(d.work.endpoint_calls, 1);
  for (auto x : used)
    h.integer(x, 2);
  h.integer(d.operation_attempted, 8);
  h.integer(d.operation_written, 8);
  h.integer(d.feature_attempted, 4);
  h.integer(d.feature_written, 4);
  h.integer(d.original_flags, 4);
  h.integer(d.capture_attempted, 1);
  h.integer(d.capture_written, 1);
  h.integer(d.capture_cursor, 1);
  h.integer(d.feature_cursor, 1);
  h.integer(d.operation_cursor, 1);
  h.integer(d.chord_cursor, 2);
  h.integer(d.operation_row, 2);
  h.enumeration(d.stage);
  h.enumeration(d.operation_stage);
  h.enumeration(d.state);
  h.enumeration(d.outcome);
  h.enumeration(d.condition);
  h.boolean(d.original_available);
  if (d.original_available) {
    h.enumeration(d.original_state);
    h.enumeration(d.original_condition);
    h.enumeration(d.original_stage);
    h.integer(d.original_pair, 2);
    h.integer(d.original_region, 1);
    h.integer(d.original_axis, 1);
    h.integer(d.original_sign, 1);
  }
  h.boolean(d.arithmetic_supported);
  h.boolean(d.evidence_complete);
  h.integer(d.required_output_bytes, 8);
  h.integer(d.output_capacity_bytes, 8);
  s.digest = h.value;
  s.required = d.required_output_bytes;
  s.owned = d.output_capacity_bytes;
  s.used = used;
  s.original_flags = d.original_flags;
  s.state = d.state;
  s.outcome = d.outcome;
  s.condition = d.condition;
  s.original_state = d.original_state;
  s.original_condition = d.original_condition;
  s.original_stage = d.original_stage;
  s.original_pair = d.original_pair;
  s.original_region = d.original_region;
  s.original_axis = d.original_axis;
  s.original_sign = d.original_sign;
  s.stage = d.stage;
  s.operation_stage = d.operation_stage;
  s.operation_row = d.operation_row;
  s.chord_cursor = d.chord_cursor;
  s.capture_cursor = d.capture_cursor;
  s.feature_cursor = d.feature_cursor;
  s.operation_cursor = d.operation_cursor;
  s.fixed = {d.work.preflight_guards, d.work.return_metadata_checks,
             d.work.endpoint_calls};
  s.exists = true;
  s.original_available = d.original_available;
  s.arithmetic_supported = d.arithmetic_supported;
  s.complete = d.evidence_complete;
  if (d.data && good) {
    s.availability = d.data->availability;
    s.owner_branch = d.data->owner_branch;
    s.center_branch = d.data->center_branch;
    s.feature_branch = d.data->feature_branch;
  }
}
[[gnu::noinline]] void first_audit(const Diagnostic& d) {
  ++totals.geometry_audits;
  if (!d.data) {
    unavailable("no earned payload");
    return;
  }
  const auto& p = *d.data;
  if (!payload_valid(p, d.source)) return;
  if ((p.availability & ((1U << 2) | (1U << 5) | (1U << 6) | (1U << 7))) !=
          ((1U << 2) | (1U << 5) | (1U << 6) | (1U << 7)) ||
      !p.original_slice.complete) {
    unavailable("partial original capture");
    return;
  }
  const ExceptionState saved;
  if (std::numeric_limits<long double>::radix != 2 ||
      std::numeric_limits<long double>::digits < 64) {
    unavailable("long double platform");
    return;
  }
  Reconstruction r;
  if (!reconstruct(p.original_slice.y, r)) {
    unavailable("original chart domain");
    return;
  }
  for (std::size_t i = 0; i < 3; ++i) {
    compare_point(r.points[i], p.points[i]);
    compare_point(r.frame[i], p.root_columns[i]);
    compare(r.unit[i], p.unit_axis[i]);
    if (p.availability & (1U << 8)) compare(r.xi[i], p.xi[i]);
    if (p.availability & (1U << 9)) compare(r.center[i], p.center[i]);
  }
  if (p.availability & (1U << 10)) compare(r.owner, p.owner_extent);
  if (p.availability & (1U << 11)) compare(r.radius, p.radius_squared);
  record_audit(p, r);
  ++totals.distance_audits;
  if (!section_distance(r)) {
    unavailable(r.section_inapplicable
                    ? "section interior inapplicable"
                    : "active-face denominator or membership");
    return;
  }
  if (p.availability & (1U << 12))
    compare(r.distance, p.minimum_distance_squared);
  std::cout << "ORACLE active-face distance=" << r.distance.lo << ','
            << r.distance.hi << " corroboration_only=1\n";
}
void telemetry(const Diagnostic& d) {
  std::cout << " state=" << unsigned(d.state)
            << " outcome=" << unsigned(d.outcome)
            << " condition=" << unsigned(d.condition)
            << " original=" << unsigned(d.original_state) << ','
            << unsigned(d.original_condition)
            << " original_flags=" << d.original_flags
            << " work=" << unsigned(d.work.preflight_guards) << ','
            << unsigned(d.work.return_metadata_checks) << ','
            << unsigned(d.work.endpoint_calls) << ',' << d.work.capture_checks
            << ',' << d.work.feature_checks << ',' << d.work.chord_checks << ','
            << d.work.operations << " ledger=" << unsigned(d.operation_stage)
            << ',' << d.operation_row << ',' << unsigned(d.operation_cursor)
            << ',' << d.operation_attempted << ',' << d.operation_written
            << " required=" << d.required_output_bytes
            << " owned=" << d.output_capacity_bytes;
  if (d.data) {
    const auto& p = *d.data;
    std::cout << " availability=" << p.availability
              << " prefixes=" << unsigned(p.feature_count) << ','
              << unsigned(p.feature_distance_count) << ','
              << p.chord_distance_count;
    if ((p.availability & (1U << 2)) && p.original_slice.complete)
      std::cout << " y=" << p.original_slice.y
                << " y_range=" << p.original_slice.lo << ','
                << p.original_slice.hi;
    const auto print = [](std::string_view tag, const Scalar& b) {
      if (supported(b))
        std::cout << ' ' << tag << '=' << b.lower << ',' << b.upper;
    };
    if (p.availability & (1U << 10)) print("O", p.owner_extent);
    if (p.availability & (1U << 11)) print("r2", p.radius_squared);
    if (p.availability & (1U << 12)) print("d2", p.minimum_distance_squared);
  }
}
void unexpected_account(std::string_view error) {
  aggregate(totals.fixed[0], 1);
  if (error == "hip joint plane01 unsupported floating point" ||
      error == "hip joint plane01 invalid limits" ||
      error == "hip joint plane01 output headers")
    return;
  if (error == "intermediate endpoint04 unsupported floating point" ||
      error == "intermediate endpoint04 invalid candidate" ||
      error == "intermediate endpoint04 invalid limits" ||
      error == "intermediate endpoint04 output headers") {
    aggregate(totals.fixed[1], 1);
    aggregate(totals.fixed[2], 1);
    return;
  }
  check(false,
        "Unclassified unexpected error has no guessed child-call ledger");
}
[[gnu::noinline]] void first_one(const Provider& p, Summary& summary) {
  slot(1, true);
  const Limits defaults;
  auto result = detail::hip_joint_plane_method01_bounded(p, defaults);
  std::cout << "FIRST_HIP_JOINT_PLANE01 admitted=" << result.has_value();
  if (result)
    telemetry(*result);
  else
    std::cout << " error=" << result.error();
  std::cout << '\n' << std::flush;
  if (!result) {
    unexpected_account(result.error());
    check(!result.error().empty(),
          "Unexpected FIRST retained without substitute");
    return;
  }
  accounting(*result, defaults, summary);
  first_audit(*result);
}
constexpr std::array<Condition, 4> capacity_conditions{
    Condition::capture_capacity, Condition::feature_capacity,
    Condition::chord_capacity, Condition::operation_capacity};
[[gnu::noinline]] void assessment_one(const Provider& p, std::uint16_t label,
                                      const Limits& limits,
                                      const Summary& baseline,
                                      std::string_view error = {},
                                      std::size_t field = 5, bool exact = false,
                                      bool empty = false) {
  slot(label, true);
  auto result = detail::hip_joint_plane_method01_bounded(p, limits);
  std::cout << "A" << label << " admitted=" << result.has_value();
  if (result)
    telemetry(*result);
  else
    std::cout << " error=" << result.error();
  std::cout << '\n';
  if (!result) {
    unexpected_account(result.error());
    check(!result.error().empty(), "Unexpected result is retained");
    if (!error.empty())
      check(result.error() == error, "Exact frozen preflight error");
    else
      check(false, "Reached structured control unexpectedly failed");
    return;
  }
  check(error.empty(), "No structured replacement of expected preflight error");
  Summary current;
  accounting(*result, limits, current);
  if (field < 4)
    check(
        result->state == State::capacity &&
            result->condition == capacity_conditions[field] &&
            current.used[field] == get_limit(limits, field),
        "Reached lowered field exact capacity condition and successful prefix");
  if (field == 4)
    check(result->state == State::capacity &&
              result->condition == Condition::output_capacity &&
              result->work.endpoint_calls == 0 && !result->data,
          "Required-room capacity precedes original call and allocation");
  if (exact && baseline.exists)
    check(current.digest == baseline.digest && current.used == baseline.used,
          "Exact-used/lifetime replay preserves checked earned evidence");
  if (empty)
    check(result->original_available &&
              result->original_condition ==
                  BoardingIntermediateEndpoint04Condition::invalid_binding &&
              result->original_state ==
                  BoardingIntermediateEndpoint04State::unresolved &&
              result->state == State::unresolved &&
              result->condition == Condition::original_unavailable &&
              result->work.endpoint_calls == 1 &&
              result->work.return_metadata_checks == 1 &&
              result->work.operations == 0,
          "Genuine moved-empty invalid binding remains original unavailable");
  if (field == 0)
    check(result->work.return_metadata_checks == 1 &&
              result->work.endpoint_calls == 1,
          "Capture0 still retains actual fixed return-metadata work");
  if (label == 28)
    check(result->work.feature_checks == 0 &&
              result->condition == Condition::capture_capacity,
          "Capture priority before feature work");
  if (label == 29)
    check(result->work.feature_checks == 0 && result->work.chord_checks == 0 &&
              result->operation_stage == Stage::chart &&
              result->operation_row == 0 && result->operation_cursor == 0 &&
              result->operation_attempted == 0 &&
              result->operation_written == 0,
          "Chart operation capacity resets empty row before feature/chord "
          "priority");
}
[[gnu::noinline]] void create_source(const NativeCraftBinding& binding,
                                     const OriginBoardingBootSupport& boots,
                                     std::optional<Provider>& p) {
  ++totals.creators;
  auto result = make_origin_boarding_intermediate_pause_support(binding, boots);
  std::cout << "FIRST_HIP_JOINT_PLANE01_SOURCE admitted=" << result.has_value();
  if (!result)
    std::cout << " error=" << result.error();
  else if (const auto* e = result->summary())
    std::cout << " complete=" << e->complete
              << " bytes=" << e->actual_source_bytes;
  std::cout << '\n' << std::flush;
  if (result) {
    check(result->summary() && result->summary()->complete,
          "One unchanged genuine provider creator");
    p.emplace(std::move(*result));
  } else
    check(false, "Creator failure has no replacement");
}
[[gnu::noinline]] void isolated_roster(const Provider& p, const Summary& s) {
  const Limits defaults;
  for (std::size_t f = 0; f < 5; ++f) {
    if (f < 4 && (!s.exists || s.used[f] == 0)) {
      slot(static_cast<std::uint16_t>(2 + f), false);
      continue;
    }
    auto l = defaults;
    set_limit(l, f, 0);
    assessment_one(p, static_cast<std::uint16_t>(2 + f), l, s,
                   f == 4 ? "hip joint plane01 output headers" : "",
                   f < 4 ? f : 5);
  }
  for (std::size_t f = 0; f < 5; ++f) {
    auto l = defaults;
    set_limit(l, f, get_limit(defaults, f) + 1);
    assessment_one(p, static_cast<std::uint16_t>(7 + f), l, s,
                   "hip joint plane01 invalid limits");
  }
  for (std::size_t f = 0; f < 5; ++f) {
    if (f < 4 && (!s.exists || s.used[f] == 0)) {
      slot(static_cast<std::uint16_t>(12 + f), false);
      continue;
    }
    auto l = defaults;
    set_limit(
        l, f,
        (f < 4 ? s.used[f]
               : boarding_hip_joint_plane_method01_required_output_bytes()) -
            1);
    assessment_one(p, static_cast<std::uint16_t>(12 + f), l, s, {}, f);
  }
}
[[gnu::noinline]] void exact_roster(const Provider& p, const Summary& s) {
  if (!s.exists) {
    slot(17, false);
    return;
  }
  Limits l;
  for (std::size_t f = 0; f < 4; ++f)
    set_limit(l, f, s.used[f]);
  l.output_bytes = boarding_hip_joint_plane_method01_required_output_bytes();
  assessment_one(p, 17, l, s, {}, 5, true);
}
[[gnu::noinline]] void remaining_roster(Provider& p, const Summary& s) {
  const Limits defaults;
  for (std::size_t mode = 0; mode < 6; ++mode) {
    const auto saved_rounding = std::fegetround(),
               saved_exceptions = std::fetestexcept(FE_ALL_EXCEPT);
#if defined(__SSE2__) && defined(__x86_64__)
    const auto saved_control = _mm_getcsr();
#endif
    {
      Environment changed(mode);
      bool independent = true;
#if defined(__SSE2__) && defined(__x86_64__)
      if (mode == 5)
        independent = std::fegetround() == FE_TONEAREST &&
                      (_mm_getcsr() & (3U << 13)) == (1U << 13);
#endif
      if (changed.available && independent)
        assessment_one(p, static_cast<std::uint16_t>(18 + mode), defaults, s,
                       "hip joint plane01 unsupported floating point");
      else
        slot(static_cast<std::uint16_t>(18 + mode), false);
    }
    check(std::fegetround() == saved_rounding &&
              std::fetestexcept(FE_ALL_EXCEPT) == saved_exceptions,
          "Unsafe-FP scope restores original fenv");
#if defined(__SSE2__) && defined(__x86_64__)
    check(_mm_getcsr() == saved_control,
          "Unsafe-FP scope restores complete original MXCSR");
#endif
  }
  {
    auto alias = p;
    auto saved = std::move(alias);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Defined empty-handle summary.
    check(alias.summary() == nullptr, "Genuine moved-empty alias");
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Defined empty-handle API.
    assessment_one(alias, 24, defaults, s, {}, 5, false, true);
    alias = std::move(saved);
  }
  {
    auto moved = std::move(p);
    assessment_one(moved, 25, defaults, s, {}, 5, s.exists);
    p = std::move(moved);
  }
  {
    auto survivor = p;
    {
      auto discarded = std::move(p);
      check(discarded.summary() != nullptr,
            "Original owner in discarded scope");
    }
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Defined empty-handle summary.
    check(p.summary() == nullptr,
          "Original empty after scoped owner destruction");
    assessment_one(survivor, 26, defaults, s, {}, 5, s.exists);
    p = std::move(survivor);
  }
  assessment_one(p, 27, defaults, s, {}, 5, s.exists);
  if (s.exists && s.used[0] > 0) {
    auto l = defaults;
    l.capture_checks = 0;
    l.feature_checks = 0;
    assessment_one(p, 28, l, s, {}, 0);
  } else
    slot(28, false);
  if (s.exists && s.used[3] > 0) {
    auto l = defaults;
    l.operations = 0;
    l.feature_checks = 0;
    l.chord_checks = 0;
    assessment_one(p, 29, l, s, {}, 3);
  } else
    slot(29, false);
}
void final_counts() {
  check(totals.creators == 1 && totals.consumers + totals.skips == 29 &&
            totals.next_slot == 30 && totals.geometry_audits <= 1 &&
            totals.distance_audits <= 1,
        "One creator literal29 A01-only independent audits");
  const Limits l;
  for (std::size_t f = 0; f < 4; ++f)
    check(totals.work[f] <= 29 * get_limit(l, f),
          "Finite aggregate successful charge ceiling");
  check(totals.fixed[0] == totals.consumers &&
            totals.fixed[1] <= totals.fixed[2] &&
            totals.fixed[2] <= totals.consumers,
        "Fixed FP/metadata/original-call totals with no extra query");
  std::cout << "HIP_JOINT_PLANE01_VALIDATION creators=" << totals.creators
            << " consumers=" << totals.consumers << " skips=" << totals.skips
            << " raw=0 inspectors=0 geometry=" << totals.geometry_audits
            << " distance=" << totals.distance_audits
            << " comparisons=" << totals.available << ',' << totals.unavailable
            << ',' << totals.enclosed << ',' << totals.overlap
            << " checks=" << checks << " failures=" << failures << " fixed=";
  for (auto x : totals.fixed)
    std::cout << x << ',';
  std::cout << " work=";
  for (auto x : totals.work)
    std::cout << x << ',';
  std::cout << '\n';
}
} // namespace
int main() {
  StreamCounter out(std::cout.rdbuf(), shared_bytes),
      err(std::cerr.rdbuf(), shared_bytes);
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
      isolated_roster(*source, summary);
      exact_roster(*source, summary);
      remaining_roster(*source, summary);
    } else
      for (std::uint16_t i = 1; i <= 29; ++i)
        slot(i, false);
    final_counts();
  } catch (const std::exception& e) {
    check(false, "Unexpected harness exception");
    std::cerr << e.what() << '\n';
  }
  check(std::cout.good() && std::cerr.good() && !out.truncated &&
            !err.truncated && shared_bytes <= 16384,
        "Shared16KiB output complete");
  std::cout.rdbuf(oldout);
  std::cerr.rdbuf(olderr);
  return failures ? 1 : 0;
}
