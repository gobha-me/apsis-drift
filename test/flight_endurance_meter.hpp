#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "apsis_drift/vacuum_dynamics.hpp"

namespace apsis_drift::research {
// Test-only v1 contract accounting. No fuel state or integrator mutation.
// Effective torque arm6.5m: numerator13*force_mN +2*torque_mNm per120Hz tick.
// Divide accumulated numerator by1,560,000 for equivalent newton-seconds.
inline constexpr std::uint64_t kEffortQuantaPerNewtonSecond{1'560'000};
inline constexpr std::uint64_t kStarterFlightCapacityQuanta{
    5'400ULL * 360'000ULL * kEffortQuantaPerNewtonSecond};
inline constexpr std::uint64_t kStarterFlightReserveQuanta{
    kStarterFlightCapacityQuanta / 3};
struct FlightEnduranceMeter {
  std::uint64_t ticks{}, firing_ticks{}, effort_quanta{};
  // Force positiveXYZ/negativeXYZ, then torque positiveXYZ/negativeXYZ.
  std::array<std::uint64_t, 12> channel_milliunit_ticks{};
  friend auto operator==(const FlightEnduranceMeter&,
                         const FlightEnduranceMeter&) -> bool = default;

  static auto quantize(double v) -> std::uint64_t {
    if (!std::isfinite(v) || v < 0 || v > 1'000'000'000)
      throw std::invalid_argument("Invalid gross actuator measurement");
    // Explicit binary64 order; positive half-quantum ties round upward.
    const auto q = static_cast<std::uint64_t>(std::floor(v * 1000.0 + .5));
    // A positive firing cannot become free through quantization's dead band.
    return v > 0 && q == 0 ? 1 : q;
  }
  static auto add(std::uint64_t a, std::uint64_t b) -> std::uint64_t {
    if (b > std::numeric_limits<std::uint64_t>::max() - a)
      throw std::overflow_error("Endurance measurement overflow");
    return a + b;
  }
  auto sample(const VacuumActuation& p) -> void {
    const std::array values{
        p.positive_force_newtons.x,        p.positive_force_newtons.y,
        p.positive_force_newtons.z,        p.negative_force_newtons.x,
        p.negative_force_newtons.y,        p.negative_force_newtons.z,
        p.positive_torque_newton_metres.x, p.positive_torque_newton_metres.y,
        p.positive_torque_newton_metres.z, p.negative_torque_newton_metres.x,
        p.negative_torque_newton_metres.y, p.negative_torque_newton_metres.z};
    auto candidate = *this;
    std::uint64_t force{}, torque{};
    for (std::size_t i = 0; i < values.size(); ++i) {
      const auto q = quantize(values[i]);
      candidate.channel_milliunit_ticks[i] = add(channel_milliunit_ticks[i], q);
      if (i < 6)
        force = add(force, q);
      else
        torque = add(torque, q);
    }
    // Twelve individually bounded channels make the total <=9e13.
    const auto effort = add(13 * force, 2 * torque);
    candidate.effort_quanta = add(effort_quanta, effort);
    candidate.ticks = add(ticks, 1);
    candidate.firing_ticks = add(firing_ticks, effort != 0 ? 1 : 0);
    *this = candidate;
  }
};
} // namespace apsis_drift::research
