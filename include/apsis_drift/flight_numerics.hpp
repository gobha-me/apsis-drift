#pragma once

#include <cstdint>

namespace apsis_drift {

// Replay recipe identity, separate from integer world generation versions.
enum class FlightNumericalProfile : std::uint32_t {
  legacy_host = 0,
  controlled_hypot_v1 = 1,
};
inline constexpr auto kCurrentFlightNumericalProfile =
    FlightNumericalProfile::controlled_hypot_v1;

[[nodiscard]] constexpr auto valid_flight_numerical_profile(
    FlightNumericalProfile profile) noexcept -> bool {
  return profile == FlightNumericalProfile::legacy_host ||
         profile == FlightNumericalProfile::controlled_hypot_v1;
}

namespace flight_numerics {
// Binary64 non-FMA glibc-2.41 algorithm. Infinity takes precedence over NaN;
// otherwise NaN produces a quiet NaN, and both zeros produce positive zero.
// No errno/exception-flag contract. Requires nearest rounding, gradual
// underflow and masked FP exceptions. Invalid profiles return quiet NaN.
[[nodiscard]] auto hypot(double x, double y,
                         FlightNumericalProfile profile) noexcept -> double;
} // namespace flight_numerics
} // namespace apsis_drift
