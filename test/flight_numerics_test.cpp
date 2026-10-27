#include "apsis_drift/flight_numerics.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>

namespace {
using namespace apsis_drift;
using Profile = FlightNumericalProfile;
int checks{}, failures{};
auto check(bool good, std::string_view message) -> void {
  ++checks;
  if (good) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}
auto bits(double value) -> std::uint64_t {
  return std::bit_cast<std::uint64_t>(value);
}
auto same(double a, double b) -> bool {
  return bits(a) == bits(b);
}
auto quiet_nan(double value) -> bool {
  constexpr std::uint64_t quiet_bit = 0x0008000000000000ULL;
  return std::isnan(value) && (bits(value) & quiet_bit) != 0;
}
auto positive_zero(double value) -> bool {
  return same(value, 0.0);
}
auto controlled(double x, double y) -> double {
  return flight_numerics::hypot(x, y, Profile::controlled_hypot_v1);
}
auto forged(std::uint32_t raw) -> Profile {
  return static_cast<Profile>(raw);
}

constexpr double inf = std::numeric_limits<double>::infinity();
constexpr double qnan = std::numeric_limits<double>::quiet_NaN();
constexpr double snan = std::numeric_limits<double>::signaling_NaN();
constexpr std::array forged_profiles{2U,          3U,          0x100U,
                                     0x7fffffffU, 0x80000000U, 0x80000001U,
                                     0xfffffffeU, 0xffffffffU};

// Replay identity values are serialized, so the enumerators are part of the
// contract rather than an implementation detail.
static_assert(std::is_same_v<std::underlying_type_t<Profile>, std::uint32_t>);
static_assert(static_cast<std::uint32_t>(Profile::legacy_host) == 0);
static_assert(static_cast<std::uint32_t>(Profile::controlled_hypot_v1) == 1);
static_assert(kCurrentFlightNumericalProfile == Profile::controlled_hypot_v1);
static_assert(valid_flight_numerical_profile(Profile::legacy_host));
static_assert(valid_flight_numerical_profile(Profile::controlled_hypot_v1));
static_assert(valid_flight_numerical_profile(kCurrentFlightNumericalProfile));
static_assert(!valid_flight_numerical_profile(static_cast<Profile>(2)));
static_assert(!valid_flight_numerical_profile(static_cast<Profile>(~0U)));
static_assert(noexcept(flight_numerics::hypot(0.0, 0.0, Profile{})));

auto profile_validation() -> void {
  check(valid_flight_numerical_profile(forged(0)) &&
            valid_flight_numerical_profile(forged(1)),
        "known profile encodings are valid at runtime");
  for (const auto raw : forged_profiles)
    check(!valid_flight_numerical_profile(forged(raw)),
          "forged profile encoding is refused");
}

auto exact_triangles() -> void {
  check(same(controlled(3.0, 4.0), 5.0), "3-4-5 is exact");
  check(same(controlled(4.0, 3.0), 5.0), "4-3-5 is exact");
  check(same(controlled(-3.0, 4.0), 5.0) && same(controlled(3.0, -4.0), 5.0) &&
            same(controlled(-3.0, -4.0), 5.0) &&
            same(controlled(-4.0, -3.0), 5.0),
        "signed 3-4-5 is exact and positive");
  struct Triple {
    double a, b, c;
  };
  constexpr std::array triples{
      Triple{5, 12, 13},  Triple{8, 15, 17},     Triple{7, 24, 25},
      Triple{20, 21, 29}, Triple{9, 40, 41},     Triple{12, 35, 37},
      Triple{65, 72, 97}, Triple{119, 120, 169}, Triple{0.75, 1, 1.25},
      Triple{1, 0, 1},    Triple{0, 7, 7},       Triple{1e-3, 0, 1e-3}};
  for (const auto& [a, b, c] : triples) {
    check(same(controlled(a, b), c) && same(controlled(b, a), c),
          "Pythagorean triple is exact in either order");
    check(same(controlled(-a, b), c) && same(controlled(a, -b), c),
          "Pythagorean triple ignores input signs");
  }
  // Scaled triples exercise the large and tiny rescaling branches.
  check(same(controlled(0x3p1000, 0x4p1000), 0x5p1000),
        "huge 3-4-5 is exact through the large rescale");
  check(same(controlled(0x4p1000, 0x3p1000), 0x5p1000),
        "huge 4-3-5 is exact through the large rescale");
  check(same(controlled(0x3p-1060, 0x4p-1060), 0x5p-1060),
        "subnormal 3-4-5 is exact through the tiny rescale");
  check(same(controlled(0x4p-1060, 0x3p-1060), 0x5p-1060),
        "subnormal 4-3-5 is exact through the tiny rescale");
  check(same(controlled(0x3p-540, 0x4p-540), 0x5p-540),
        "tiny normal 3-4-5 is exact through the tiny rescale");
}

auto zeros_and_dominance() -> void {
  check(positive_zero(controlled(0.0, 0.0)), "hypot(+0, +0) is +0");
  check(positive_zero(controlled(-0.0, 0.0)), "hypot(-0, +0) is +0");
  check(positive_zero(controlled(0.0, -0.0)), "hypot(+0, -0) is +0");
  check(positive_zero(controlled(-0.0, -0.0)), "hypot(-0, -0) is +0");
  constexpr std::array magnitudes{DBL_TRUE_MIN, DBL_MIN, 0x1p-459, 1.0,
                                  0x1p511,      0x1p600, DBL_MAX};
  for (const auto m : magnitudes) {
    check(same(controlled(m, 0.0), m) && same(controlled(0.0, m), m) &&
              same(controlled(-m, -0.0), m) && same(controlled(-0.0, -m), m),
          "a zero leg returns the other magnitude exactly");
  }
  // At or below 2^-54 relative, the minor leg cannot move the rounded result.
  check(same(controlled(1.0, 0x1p-54), 1.0), "eps-dominated minor leg");
  check(same(controlled(1.0, 0x1p-60), 1.0), "deeply dominated minor leg");
  check(same(controlled(1.0, 0x1p-27), 1.0),
        "sqrt(1 + 2^-54) rounds to one through the kernel");
  check(same(controlled(0x1p600, 1.0), 0x1p600), "huge dominated leg");
  check(same(controlled(DBL_MAX, 1.0), DBL_MAX), "DBL_MAX dominated leg");
  check(same(controlled(1.0, DBL_TRUE_MIN), 1.0), "subnormal minor leg");
  check(same(controlled(0x1p-1000, DBL_TRUE_MIN), 0x1p-1000),
        "tiny dominated leg");
}

auto overflow_and_underflow() -> void {
  check(same(controlled(DBL_MAX, DBL_MAX), inf),
        "hypot(DBL_MAX, DBL_MAX) overflows to +inf");
  check(same(controlled(-DBL_MAX, DBL_MAX), inf),
        "signed DBL_MAX overflow is +inf");
  const auto near_max = controlled(DBL_MAX / 2, DBL_MAX / 2);
  check(std::isfinite(near_max) && near_max > DBL_MAX / 2,
        "half DBL_MAX legs stay finite");
  check(same(controlled(1e300, 1e300), controlled(-1e300, 1e300)) &&
            std::isfinite(controlled(1e300, 1e300)),
        "1e300 legs do not overflow intermediate squares");
  const auto smallest = controlled(DBL_TRUE_MIN, DBL_TRUE_MIN);
  check(smallest > 0.0 && std::isfinite(smallest),
        "denormal legs do not underflow to zero");
  check(same(controlled(1e-300, 1e-300), controlled(1e-300, -1e-300)) &&
            controlled(1e-300, 1e-300) > 1e-300,
        "1e-300 legs do not underflow intermediate squares");
}

auto non_finite_inputs() -> void {
  constexpr std::array nans{qnan, -qnan, snan, -snan};
  constexpr std::array others{0.0, -0.0, 1.0, -3.0, DBL_MAX, DBL_TRUE_MIN};
  for (const auto infinity : {inf, -inf}) {
    check(same(controlled(infinity, infinity), inf) &&
              same(controlled(infinity, -infinity), inf),
          "infinite legs return +inf");
    for (const auto n : nans)
      check(same(controlled(infinity, n), inf) &&
                same(controlled(n, infinity), inf),
            "infinity takes precedence over every NaN in either slot");
    for (const auto o : others)
      check(same(controlled(infinity, o), inf) &&
                same(controlled(o, infinity), inf),
            "infinity with a finite leg returns +inf");
  }
  for (const auto n : nans) {
    for (const auto o : others)
      check(quiet_nan(controlled(n, o)) && quiet_nan(controlled(o, n)),
            "NaN with a finite leg returns a quiet NaN");
    for (const auto m : nans)
      check(quiet_nan(controlled(n, m)), "two NaNs return a quiet NaN");
  }
  // Payload-bearing NaN encodings must not leak through as signalling NaNs.
  const auto payload_snan = std::bit_cast<double>(0x7ff0000000000001ULL);
  const auto payload_negative = std::bit_cast<double>(0xfff4000000000abcULL);
  check(quiet_nan(controlled(payload_snan, 1.0)) &&
            quiet_nan(controlled(1.0, payload_negative)),
        "payload signalling NaNs are quieted");
}

auto invalid_profiles() -> void {
  constexpr std::array inputs{0.0, 3.0, -4.0, DBL_MAX, inf, -inf, qnan, snan};
  for (const auto raw : forged_profiles)
    for (const auto x : inputs)
      for (const auto y : inputs)
        check(quiet_nan(flight_numerics::hypot(x, y, forged(raw))),
              "forged profile returns quiet NaN, even over infinity");
}

auto ordinary_profiles() -> void {
  // legacy_host is host std::hypot; it is deliberately not compared with
  // controlled_hypot_v1 because hosts may disagree in the final bit.
  volatile double lx = 0.1;
  volatile double ly = 0.2;
  check(same(flight_numerics::hypot(lx, ly, Profile::legacy_host),
             std::hypot(lx, ly)),
        "legacy_host dispatches to host std::hypot");
  const auto legacy = flight_numerics::hypot(3.0, 4.0, Profile::legacy_host);
  check(std::isfinite(legacy) && std::abs(legacy - 5.0) <= 0x1p-50,
        "legacy_host is finite for ordinary legs");
  const auto current =
      flight_numerics::hypot(0.1, 0.2, kCurrentFlightNumericalProfile);
  check(same(current, controlled(0.1, 0.2)),
        "current profile is the controlled kernel");
  check(std::isfinite(current) && current > 0.2 && current < 0.3,
        "controlled result is finite for ordinary legs");
  for (int i = 0; i < 8; ++i)
    check(same(controlled(0.1, 0.2), current), "controlled result is pure");
}

// Pinned replay bits for non-exact inputs. Each value was checked as the
// correctly rounded binary64 square root of the exact sum of squares.
auto pinned_replay_bits() -> void {
  struct Case {
    double x, y;
    std::uint64_t expected;
  };
  constexpr std::array cases{
      Case{0.1, 0.2, 0x3fcc9f25c5bfeddaULL},
      Case{1.0, 1.0, 0x3ff6a09e667f3bcdULL},
      Case{6371000.0, 1234.5, 0x41584dae07a7963eULL},
      Case{1.4959787e11, 3.844e8, 0x42416a60f06ddb5eULL},
      Case{1e-310, 3e-310, 0x00003a365ff2ea11ULL},
      Case{1e300, 2e300, 0x7e4ab629052228b2ULL},
  };
  for (const auto& [x, y, expected] : cases) {
    check(bits(controlled(x, y)) == expected, "pinned controlled replay bits");
    check(bits(controlled(y, -x)) == expected, "pinned bits are symmetric");
  }
}

auto next_bits(std::uint64_t& state) -> std::uint64_t {
  state += 0x9e3779b97f4a7c15ULL;
  auto z = state;
  z = (z ^ (z >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27U)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31U);
}
auto finite_from(std::uint64_t raw) -> double {
  // Clamp the biased exponent to finite encodings; keeps subnormals.
  constexpr std::uint64_t exponent_mask = 0x7ff0000000000000ULL;
  if ((raw & exponent_mask) == exponent_mask) raw &= ~(1ULL << 62U);
  return std::bit_cast<double>(raw);
}
auto near_exponent(std::uint64_t raw, double other) -> double {
  // Pair legs within a few binades so the kernel, not dominance, decides.
  const int shift = static_cast<int>(raw % 16U) - 8;
  const auto mantissa = 1.0 + static_cast<double>(raw >> 12U) * 0x1p-52;
  return std::ldexp(mantissa, std::min(std::ilogb(other) + shift, 1023));
}

auto symmetric(double x, double y, double r) -> bool {
  return same(controlled(y, x), r) && same(controlled(-x, y), r) &&
         same(controlled(x, -y), r) && same(controlled(-y, -x), r);
}

auto randomized_properties() -> void {
  std::uint64_t state = 255;
  int asymmetric{}, out_of_bounds{}, over_ulp{};
  constexpr bool wide_reference =
      std::numeric_limits<long double>::digits > 53 &&
      std::numeric_limits<long double>::max_exponent > 2 * 1024 + 64;
  for (int i = 0; i < 200000; ++i) {
    const auto x = finite_from(next_bits(state));
    const auto y = (i % 2 == 0 || x == 0.0)
                       ? finite_from(next_bits(state))
                       : near_exponent(next_bits(state), x);
    const auto r = controlled(x, y);
    if (!symmetric(x, y, r)) ++asymmetric;
    const auto ax = std::abs(x);
    const auto ay = std::abs(y);
    const auto major = ax > ay ? ax : ay;
    if (!(r >= major) || std::signbit(r) || std::isnan(r)) ++out_of_bounds;
    if constexpr (wide_reference) {
      const auto lx = static_cast<long double>(ax);
      const auto ly = static_cast<long double>(ay);
      const auto reference = std::sqrt(lx * lx + ly * ly);
      if (std::isinf(r)) {
        if (reference < static_cast<long double>(DBL_MAX)) ++over_ulp;
      } else {
        const auto ulp = static_cast<long double>(std::nextafter(r, inf) - r);
        if (std::abs(static_cast<long double>(r) - reference) > ulp) ++over_ulp;
      }
    }
  }
  check(asymmetric == 0, "sweep results ignore order and sign");
  check(out_of_bounds == 0, "sweep results are at least the major leg");
  check(over_ulp == 0, "sweep results are within one ulp of extended sqrt");
}
} // namespace

auto main() -> int {
  profile_validation();
  exact_triangles();
  zeros_and_dominance();
  overflow_and_underflow();
  non_finite_inputs();
  invalid_profiles();
  ordinary_profiles();
  pinned_replay_bits();
  randomized_properties();
  std::cout << "Flight numerics contract: " << checks << " checks, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
