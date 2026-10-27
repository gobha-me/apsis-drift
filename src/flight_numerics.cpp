/* Euclidean distance function.  Double/Binary64 version.
   Copyright (C) 2021-2025 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <https://www.gnu.org/licenses/>.  */

// Adapted for Apsis Drift: namespaced C++ entry point, fixed non-FMA kernel,
// explicit profile dispatch, no glibc aliases/errno/fenv plumbing. Original
// source and license texts are retained in third_party/glibc/.
#include "apsis_drift/flight_numerics.hpp"

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <limits>

static_assert(FLT_EVAL_METHOD == 0);
static_assert(std::numeric_limits<double>::is_iec559 &&
              std::numeric_limits<double>::digits == 53 &&
              std::numeric_limits<double>::max_exponent == 1024);

namespace apsis_drift::flight_numerics {
namespace {
// Keep this evaluation order; FMA or reassociation changes the replay recipe.
[[nodiscard]] auto kernel(double ax, double ay) noexcept -> double {
  double h = std::sqrt(ax * ax + ay * ay);
  double t1{};
  double t2{};
  if (h <= 2.0 * ay) {
    const double delta = h - ay;
    t1 = ax * (2.0 * delta - ax);
    t2 = (delta - 2.0 * (ax - ay)) * delta;
  } else {
    const double delta = h - ax;
    t1 = 2.0 * delta * (ax - 2.0 * ay);
    t2 = (4.0 * delta - ay) * ay + delta * delta;
  }
  h -= (t1 + t2) / (2.0 * h);
  return h;
}

[[nodiscard]] auto controlled_hypot(double x, double y) noexcept -> double {
  // Classify encodings before any FP comparison, including signaling NaNs.
  constexpr std::uint64_t magnitude_mask = 0x7fffffffffffffffULL;
  constexpr std::uint64_t infinity_bits = 0x7ff0000000000000ULL;
  const auto x_bits = std::bit_cast<std::uint64_t>(x) & magnitude_mask;
  const auto y_bits = std::bit_cast<std::uint64_t>(y) & magnitude_mask;
  if (x_bits == infinity_bits || y_bits == infinity_bits)
    return std::numeric_limits<double>::infinity();
  if (x_bits > infinity_bits || y_bits > infinity_bits)
    return std::numeric_limits<double>::quiet_NaN();
  x = std::abs(x);
  y = std::abs(y);
  const double ax = std::max(x, y);
  const double ay = std::min(x, y);
  constexpr double scale = 0x1p-600;
  constexpr double large = 0x1p+511;
  constexpr double tiny = 0x1p-459;
  constexpr double eps = 0x1p-54;
  if (ax > large) {
    if (ay <= ax * eps) return ax + ay;
    return kernel(ax * scale, ay * scale) / scale;
  }
  if (ay < tiny) {
    if (ax >= ay / eps) return ax + ay;
    return kernel(ax / scale, ay / scale) * scale;
  }
  if (ay <= ax * eps) return ax + ay;
  return kernel(ax, ay);
}
} // namespace

auto hypot(double x, double y, FlightNumericalProfile profile) noexcept
    -> double {
  switch (profile) {
    case FlightNumericalProfile::legacy_host: return std::hypot(x, y);
    case FlightNumericalProfile::controlled_hypot_v1:
      return controlled_hypot(x, y);
  }
  return std::numeric_limits<double>::quiet_NaN();
}
} // namespace apsis_drift::flight_numerics
