#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/coordinates.hpp"
#include "apsis_drift/intersystem_contract.hpp"
#include "apsis_drift/planet.hpp"
#include "apsis_drift/simulation.hpp"

namespace apsis_drift {

// Local-system catalogs and analytic ephemerides are generated-world
// compatibility data. Changing their seed mapping or parameter interpretation
// requires a new version and new golden vectors.
inline constexpr std::uint32_t kLocalSystemGeneratorVersion{1};
inline constexpr std::uint32_t kAnalyticEphemerisVersion{1};
inline constexpr std::uint32_t kMinimumLocalSystemPlanets{3};
inline constexpr std::uint32_t kMaximumLocalSystemPlanets{6};

enum class StarSpectralClass : std::uint8_t {
  m,
  k,
  g,
  f,
};

struct StarDescriptor {
  Seed seed;
  StarId id;
  std::string display_name;
  StarSpectralClass spectral_class{};
  std::uint32_t temperature_kelvin{};
  std::uint32_t radius_kilometres{};
  Rgb8 color;

  friend auto operator==(const StarDescriptor&, const StarDescriptor&)
      -> bool = default;
};

// Version 1 deliberately uses circular analytic orbits. Integer generator
// fields are stable diagnostic/save projections; doubles exist only in the
// resolved ephemeris.
struct PlanetOrbit {
  Seed seed;
  PlanetId planet;
  std::uint32_t ordinal{};
  std::uint64_t radius_kilometres{};
  SimulationTick period_ticks{};
  std::uint32_t epoch_phase_turns{};
  std::int32_t inclination_microdegrees{};
  std::uint32_t ascending_node_turns{};

  friend auto operator==(const PlanetOrbit&, const PlanetOrbit&)
      -> bool = default;
};

struct LocalSystemPlanet {
  PlanetDescriptor descriptor;
  PlanetOrbit orbit;

  friend auto operator==(const LocalSystemPlanet&, const LocalSystemPlanet&)
      -> bool = default;
};

struct LocalSystemDescriptor {
  Seed seed;
  SystemId id;
  StarDescriptor star;
  std::vector<LocalSystemPlanet> planets;

  friend auto operator==(const LocalSystemDescriptor&,
                         const LocalSystemDescriptor&) -> bool = default;
};

// Authoritative simulation queries use a zero fraction. Renderers may sample
// between two authoritative ticks without feeding that fraction back into the
// simulation or save state.
struct EphemerisQueryTime {
  SimulationTick tick{};
  double sub_tick_fraction{};
};

struct PlanetEphemeris {
  PlanetId planet;
  SystemPositionMetres position;
  SystemVelocityMetresPerSecond velocity;
  SimulationTick cycle_tick{};
  double phase_radians{};

  friend auto operator==(const PlanetEphemeris&, const PlanetEphemeris&)
      -> bool = default;
};

enum class LocalSystemError : std::uint8_t {
  invalid_system,
  invalid_star,
  invalid_planet_catalog,
  invalid_orbit,
  unknown_planet,
  non_finite_time,
  unsafe_arithmetic,
};

[[nodiscard]] auto generate_local_system(Seed system_seed)
    -> LocalSystemDescriptor;

[[nodiscard]] auto validate_local_system(
    const LocalSystemDescriptor& system)
    -> std::expected<void, LocalSystemError>;

[[nodiscard]] auto find_local_system_planet(
    const LocalSystemDescriptor& system, PlanetId planet)
    -> std::expected<const LocalSystemPlanet*, LocalSystemError>;

[[nodiscard]] auto resolve_planet_ephemeris(
    const LocalSystemDescriptor& system, PlanetId planet,
    EphemerisQueryTime time)
    -> std::expected<PlanetEphemeris, LocalSystemError>;

[[nodiscard]] auto star_spectral_class_name(
    StarSpectralClass value) noexcept -> std::string_view;

[[nodiscard]] auto local_system_diagnostic_json(
    const LocalSystemDescriptor& system)
    -> std::expected<std::string, LocalSystemError>;

}  // namespace apsis_drift
