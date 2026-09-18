#pragma once

#include <cstdint>
#include <expected>

#include "apsis_drift/coordinates.hpp"
#include "apsis_drift/planet.hpp"
#include "apsis_drift/simulation.hpp"

namespace apsis_drift {

// Celestial geometry is generated-world compatibility data. The version is
// recorded in saves independently from the planet descriptor generator so a
// lighting change cannot silently alter an existing world's solar geometry.
inline constexpr std::uint32_t kLocalSunGeneratorVersion{1};
inline constexpr SimulationTick kLocalDayTicks{72'000};

struct LocalSunGeometry {
  PlanetFixedDirection planet_to_sun;
  SimulationTick cycle_tick{};
  double phase_radians{};
  double declination_radians{};

  friend auto operator==(const LocalSunGeometry&, const LocalSunGeometry&)
      -> bool = default;
};

enum class LocalSunError : std::uint8_t {
  invalid_planet,
  invalid_geometry,
};

// Resolves a finite, deterministic planet-fixed direction from immutable
// generated identity and authoritative fixed-step time. Render cadence and
// wall-clock time are never inputs.
[[nodiscard]] auto resolve_local_sun(const PlanetDescriptor& planet,
                                     SimulationTick tick) noexcept
    -> std::expected<LocalSunGeometry, LocalSunError>;

// Returns signed solar elevation as the cosine between a surface up direction
// and the planet-to-sun direction: +1 is local noon, 0 the geometric horizon,
// and -1 local midnight.
[[nodiscard]] auto local_solar_elevation(
    const LocalSunGeometry& sun, PlanetFixedDirection surface_up) noexcept
    -> std::expected<double, LocalSunError>;

} // namespace apsis_drift
