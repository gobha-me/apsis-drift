#include "apsis_drift/celestial.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace apsis_drift {
namespace {

[[nodiscard]] auto mix64(std::uint64_t value) noexcept -> std::uint64_t {
  value ^= value >> 30U;
  value *= 0xBF58476D1CE4E5B9ULL;
  value ^= value >> 27U;
  value *= 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

[[nodiscard]] auto finite(PlanetFixedDirection value) noexcept -> bool {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] auto length(PlanetFixedDirection value) noexcept -> double {
  return std::hypot(value.x, value.y, value.z);
}

[[nodiscard]] auto quantized(double value) noexcept -> double {
  constexpr double scale{1'000'000'000.0};
  return std::round(value * scale) / scale;
}

[[nodiscard]] auto valid_planet_identity(
    const PlanetDescriptor& planet) noexcept -> bool {
  return planet.id.value == planet.seed.value &&
         planet.radius.value >= PlanetRadiusKm::min &&
         planet.radius.value <= PlanetRadiusKm::max;
}

} // namespace

auto resolve_local_sun(const PlanetDescriptor& planet,
                       SimulationTick tick) noexcept
    -> std::expected<LocalSunGeometry, LocalSunError> {
  if (!valid_planet_identity(planet)) {
    return std::unexpected{LocalSunError::invalid_planet};
  }

  const auto seed =
      derive_planet_stream_seed(planet.seed, PlanetDescriptorStream::celestial);
  const auto phase_seed = mix64(seed.value ^ 0xA0761D6478BD642FULL);
  const auto declination_seed = mix64(seed.value ^ 0xE7037ED1A0B428DBULL);
  const SimulationTick phase_offset = phase_seed % kLocalDayTicks;
  const SimulationTick cycle_tick =
      (phase_offset + tick % kLocalDayTicks) % kLocalDayTicks;

  constexpr double tau{2.0 * std::numbers::pi_v<double>};
  const double phase = tau * static_cast<double>(cycle_tick) /
                       static_cast<double>(kLocalDayTicks);
  // One-degree steps bound the v1 seasonal declination while keeping every
  // generated world away from a permanent polar day/night extreme.
  const int declination_degrees = static_cast<int>(declination_seed % 61U) - 30;
  const double declination = static_cast<double>(declination_degrees) *
                             std::numbers::pi_v<double> / 180.0;
  const double horizontal = std::cos(declination);
  LocalSunGeometry result{
      .planet_to_sun = {quantized(horizontal * std::cos(phase)),
                        quantized(horizontal * std::sin(phase)),
                        quantized(std::sin(declination))},
      .cycle_tick = cycle_tick,
      .phase_radians = quantized(phase),
      .declination_radians = quantized(declination),
  };
  const double magnitude = length(result.planet_to_sun);
  if (!finite(result.planet_to_sun) || !std::isfinite(result.phase_radians) ||
      !std::isfinite(result.declination_radians) || !std::isfinite(magnitude) ||
      magnitude < 0.999999 || magnitude > 1.000001) {
    return std::unexpected{LocalSunError::invalid_geometry};
  }
  return result;
}

auto local_solar_elevation(const LocalSunGeometry& sun,
                           PlanetFixedDirection surface_up) noexcept
    -> std::expected<double, LocalSunError> {
  const double sun_length = length(sun.planet_to_sun);
  const double up_length = length(surface_up);
  if (!finite(sun.planet_to_sun) || !finite(surface_up) ||
      !std::isfinite(sun_length) || !std::isfinite(up_length) ||
      sun_length <= 1.0e-12 || up_length <= 1.0e-12) {
    return std::unexpected{LocalSunError::invalid_geometry};
  }
  const double dot = sun.planet_to_sun.x * surface_up.x +
                     sun.planet_to_sun.y * surface_up.y +
                     sun.planet_to_sun.z * surface_up.z;
  return std::clamp(dot / (sun_length * up_length), -1.0, 1.0);
}

} // namespace apsis_drift
