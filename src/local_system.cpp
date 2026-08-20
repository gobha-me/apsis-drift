#include "apsis_drift/local_system.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <ranges>
#include <utility>

namespace apsis_drift {
namespace {

inline constexpr std::uint64_t kOrbitBandKilometres{12'000'000};
inline constexpr std::uint64_t kOrbitBaseKilometres{4'000'000};
inline constexpr std::uint64_t kOrbitJitterKilometres{4'000'000};
inline constexpr SimulationTick kTicksPerHour{3'600 * kSimulationHz};
inline constexpr SimulationTick kOrbitBasePeriodTicks{6 * kTicksPerHour};
inline constexpr SimulationTick kOrbitPeriodBandTicks{8 * kTicksPerHour};
inline constexpr SimulationTick kOrbitPeriodJitterTicks{2 * kTicksPerHour};
inline constexpr std::int32_t kMaximumInclinationMicrodegrees{10'000'000};

enum class LocalSystemStream : std::uint64_t {
  catalog = 1,
};

enum class StarStream : std::uint64_t {
  name = 1,
  physical = 2,
};

enum class OrbitStream : std::uint64_t {
  radius = 1,
  period = 2,
  phase = 3,
  orientation = 4,
};

class SplitMix64 {
 public:
  explicit SplitMix64(Seed seed) noexcept : m_state{seed.value} {}

  [[nodiscard]] auto next() noexcept -> std::uint64_t {
    auto value = (m_state += 0x9E3779B97F4A7C15ULL);
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
  }

  [[nodiscard]] auto bounded(std::uint64_t exclusive_upper) noexcept
      -> std::uint64_t {
    if (exclusive_upper == 0) return 0;
    const auto threshold =
        (std::numeric_limits<std::uint64_t>::max() - exclusive_upper + 1U) %
        exclusive_upper;
    for (;;) {
      const auto value = next();
      if (value >= threshold) return value % exclusive_upper;
    }
  }

 private:
  std::uint64_t m_state{};
};

[[nodiscard]] auto stream_seed(Seed parent, SeedDomain domain,
                               std::uint64_t stream) noexcept -> Seed {
  return derive_seed(parent, domain, stream);
}

[[nodiscard]] auto spectral_class(std::uint32_t temperature) noexcept
    -> StarSpectralClass {
  if (temperature < 3'700) return StarSpectralClass::m;
  if (temperature < 5'200) return StarSpectralClass::k;
  if (temperature < 6'000) return StarSpectralClass::g;
  return StarSpectralClass::f;
}

[[nodiscard]] auto spectral_color(StarSpectralClass value) noexcept -> Rgb8 {
  switch (value) {
    case StarSpectralClass::m: return {255, 164, 112};
    case StarSpectralClass::k: return {255, 202, 142};
    case StarSpectralClass::g: return {255, 244, 214};
    case StarSpectralClass::f: return {234, 239, 255};
  }
  return {};
}

[[nodiscard]] auto generated_star_name(Seed star_seed) -> std::string {
  constexpr std::array<std::string_view, 16> starts{
      "Al", "An", "Ar", "Be", "Ca", "Cy", "De", "El",
      "Io", "Ka", "Ly", "Na", "Or", "Si", "Ta", "Ve",
  };
  constexpr std::array<std::string_view, 16> ends{
      "car", "den", "dor", "fia", "ion", "mar", "nus", "on",
      "phi", "ra",  "rus", "tar", "tis", "via", "xis", "zar",
  };
  SplitMix64 random{stream_seed(star_seed, SeedDomain::star,
                                static_cast<std::uint64_t>(StarStream::name))};
  return std::format("{}{}", starts[random.bounded(starts.size())],
                     ends[random.bounded(ends.size())]);
}

[[nodiscard]] auto generate_star(Seed system_seed) -> StarDescriptor {
  const auto seed = derive_seed(system_seed, SeedDomain::star, 0);
  SplitMix64 physical{stream_seed(
      seed, SeedDomain::star,
      static_cast<std::uint64_t>(StarStream::physical))};
  const auto temperature =
      static_cast<std::uint32_t>(2'800 + physical.bounded(4'701));
  const auto star_class = spectral_class(temperature);
  return {
      .seed = seed,
      .id = StarId{seed.value},
      .display_name = generated_star_name(seed),
      .spectral_class = star_class,
      .temperature_kelvin = temperature,
      .radius_kilometres =
          static_cast<std::uint32_t>(350'000 + physical.bounded(1'050'001)),
      .color = spectral_color(star_class),
  };
}

[[nodiscard]] auto generate_orbit(Seed system_seed, PlanetId planet,
                                  std::uint32_t ordinal) -> PlanetOrbit {
  const auto seed = derive_seed(system_seed, SeedDomain::orbit, ordinal);
  SplitMix64 radius_random{stream_seed(
      seed, SeedDomain::orbit,
      static_cast<std::uint64_t>(OrbitStream::radius))};
  SplitMix64 period_random{stream_seed(
      seed, SeedDomain::orbit,
      static_cast<std::uint64_t>(OrbitStream::period))};
  SplitMix64 phase_random{stream_seed(
      seed, SeedDomain::orbit,
      static_cast<std::uint64_t>(OrbitStream::phase))};
  SplitMix64 orientation_random{stream_seed(
      seed, SeedDomain::orbit,
      static_cast<std::uint64_t>(OrbitStream::orientation))};
  const auto radius = kOrbitBaseKilometres +
                      static_cast<std::uint64_t>(ordinal) *
                          kOrbitBandKilometres +
                      radius_random.bounded(kOrbitJitterKilometres + 1);
  const auto period = kOrbitBasePeriodTicks +
                      static_cast<SimulationTick>(ordinal) *
                          kOrbitPeriodBandTicks +
                      period_random.bounded(kOrbitPeriodJitterTicks + 1);
  const auto inclination_width =
      static_cast<std::uint64_t>(2) *
          static_cast<std::uint64_t>(kMaximumInclinationMicrodegrees) +
      1U;
  const auto inclination = static_cast<std::int32_t>(
      static_cast<std::int64_t>(orientation_random.bounded(inclination_width)) -
      kMaximumInclinationMicrodegrees);
  return {
      .seed = seed,
      .planet = planet,
      .ordinal = ordinal,
      .radius_kilometres = radius,
      .period_ticks = period,
      .epoch_phase_turns = static_cast<std::uint32_t>(phase_random.next()),
      .inclination_microdegrees = inclination,
      .ascending_node_turns =
          static_cast<std::uint32_t>(orientation_random.next()),
  };
}

[[nodiscard]] auto valid_star(const StarDescriptor& star,
                              Seed system_seed) -> bool {
  return star == generate_star(system_seed);
}

[[nodiscard]] auto valid_planet_descriptor(
    const PlanetDescriptor& planet, Seed expected_seed) -> bool {
  return planet.seed == expected_seed && planet.id.value == expected_seed.value &&
         planet == generate_planet_descriptor(expected_seed);
}

[[nodiscard]] auto valid_orbit(const PlanetOrbit& orbit, Seed system_seed,
                               std::uint32_t ordinal,
                               PlanetId planet) -> bool {
  return orbit == generate_orbit(system_seed, planet, ordinal);
}

[[nodiscard]] auto finite(double value) noexcept -> bool {
  return std::isfinite(value);
}

[[nodiscard]] auto quantized_position(double value) noexcept -> double {
  return std::round(value);
}

[[nodiscard]] auto quantized_velocity(double value) noexcept -> double {
  constexpr double scale{1'000.0};
  return std::round(value * scale) / scale;
}

}  // namespace

auto generate_local_system(Seed system_seed) -> LocalSystemDescriptor {
  SplitMix64 catalog{stream_seed(
      system_seed, SeedDomain::system,
      static_cast<std::uint64_t>(LocalSystemStream::catalog))};
  const auto count = static_cast<std::uint32_t>(
      kMinimumLocalSystemPlanets +
      catalog.bounded(kMaximumLocalSystemPlanets -
                          kMinimumLocalSystemPlanets +
                      1U));
  std::vector<LocalSystemPlanet> planets;
  planets.reserve(count);
  for (std::uint32_t ordinal = 0; ordinal < count; ++ordinal) {
    const auto planet_seed =
        derive_seed(system_seed, SeedDomain::planet, ordinal);
    auto descriptor = generate_planet_descriptor(planet_seed);
    const auto planet = descriptor.id;
    planets.push_back({
        .descriptor = std::move(descriptor),
        .orbit = generate_orbit(system_seed, planet, ordinal),
    });
  }
  return {
      .seed = system_seed,
      .id = SystemId{system_seed.value},
      .kind = LocalSystemKind::procedural,
      .star = generate_star(system_seed),
      .planets = std::move(planets),
  };
}

auto generate_origin_system(Seed universe_seed) -> LocalSystemDescriptor {
  const auto station = generate_origin_station(universe_seed);
  auto generated = generate_local_system(station.home_system_seed);
  generated.kind = LocalSystemKind::origin_home;
  std::vector<LocalSystemPlanet> planets;
  planets.reserve(generated.planets.size());
  for (std::size_t index = 0; index < generated.planets.size(); ++index) {
    if (index == kOriginHomePlanetOrdinal) {
      planets.push_back({generate_origin_home_planet(station.home_system_seed),
                         generated.planets[index].orbit});
    } else {
      planets.push_back(generated.planets[index]);
    }
  }
  generated.planets = std::move(planets);
  return generated;
}

auto validate_local_system(const LocalSystemDescriptor& system)
    -> std::expected<void, LocalSystemError> {
  if ((system.kind != LocalSystemKind::procedural &&
       system.kind != LocalSystemKind::origin_home) ||
      system.id.value != system.seed.value) {
    return std::unexpected{LocalSystemError::invalid_system};
  }
  if (!valid_star(system.star, system.seed)) {
    return std::unexpected{LocalSystemError::invalid_star};
  }
  if (system.planets.size() < kMinimumLocalSystemPlanets ||
      system.planets.size() > kMaximumLocalSystemPlanets) {
    return std::unexpected{LocalSystemError::invalid_planet_catalog};
  }
  std::uint64_t previous_radius{};
  SimulationTick previous_period{};
  for (std::size_t index = 0; index < system.planets.size(); ++index) {
    if (index > std::numeric_limits<std::uint32_t>::max()) {
      return std::unexpected{LocalSystemError::unsafe_arithmetic};
    }
    const auto ordinal = static_cast<std::uint32_t>(index);
    const auto expected_seed =
        derive_seed(system.seed, SeedDomain::planet, ordinal);
    const auto& planet = system.planets[index];
    const bool home = system.kind == LocalSystemKind::origin_home &&
                      ordinal == kOriginHomePlanetOrdinal;
    const bool valid_descriptor =
        home ? planet.descriptor == generate_origin_home_planet(system.seed) &&
                   is_tutorial_safe_home_planet(planet.descriptor)
             : valid_planet_descriptor(planet.descriptor, expected_seed);
    if (!valid_descriptor) {
      return std::unexpected{LocalSystemError::invalid_planet_catalog};
    }
    if (!valid_orbit(planet.orbit, system.seed, ordinal,
                     planet.descriptor.id) ||
        (index != 0 &&
         (planet.orbit.radius_kilometres <= previous_radius ||
          planet.orbit.period_ticks <= previous_period))) {
      return std::unexpected{LocalSystemError::invalid_orbit};
    }
    previous_radius = planet.orbit.radius_kilometres;
    previous_period = planet.orbit.period_ticks;
  }
  return {};
}

auto find_local_system_planet(const LocalSystemDescriptor& system,
                              PlanetId planet)
    -> std::expected<const LocalSystemPlanet*, LocalSystemError> {
  if (const auto valid = validate_local_system(system); !valid) {
    return std::unexpected{valid.error()};
  }
  const auto found = std::ranges::find_if(
      system.planets, [planet](const LocalSystemPlanet& candidate) {
        return candidate.descriptor.id == planet;
      });
  if (found == system.planets.end()) {
    return std::unexpected{LocalSystemError::unknown_planet};
  }
  return &*found;
}

auto resolve_planet_ephemeris(const LocalSystemDescriptor& system,
                              PlanetId planet,
                              EphemerisQueryTime time)
    -> std::expected<PlanetEphemeris, LocalSystemError> {
  if (!finite(time.sub_tick_fraction) || time.sub_tick_fraction < 0.0 ||
      time.sub_tick_fraction >= 1.0) {
    return std::unexpected{LocalSystemError::non_finite_time};
  }
  const auto found = find_local_system_planet(system, planet);
  if (!found) return std::unexpected{found.error()};
  const auto& orbit = (*found)->orbit;
  if (orbit.period_ticks == 0) {
    return std::unexpected{LocalSystemError::invalid_orbit};
  }

  const auto cycle_tick = time.tick % orbit.period_ticks;
  constexpr double turn_scale{1.0 / 4'294'967'296.0};
  const double epoch_turns =
      static_cast<double>(orbit.epoch_phase_turns) * turn_scale;
  const double cycle_turns =
      (static_cast<double>(cycle_tick) + time.sub_tick_fraction) /
      static_cast<double>(orbit.period_ticks);
  const double phase = std::numbers::pi_v<double> * 2.0 *
                       std::fmod(epoch_turns + cycle_turns, 1.0);
  const double inclination =
      static_cast<double>(orbit.inclination_microdegrees) *
      (std::numbers::pi_v<double> / 180'000'000.0);
  const double node = std::numbers::pi_v<double> * 2.0 *
                      static_cast<double>(orbit.ascending_node_turns) *
                      turn_scale;
  const double radius =
      static_cast<double>(orbit.radius_kilometres) * 1'000.0;
  const double cos_phase = std::cos(phase);
  const double sin_phase = std::sin(phase);
  const double cos_node = std::cos(node);
  const double sin_node = std::sin(node);
  const double cos_inclination = std::cos(inclination);
  const double sin_inclination = std::sin(inclination);
  const SystemPositionMetres position{
      quantized_position(
          radius * (cos_phase * cos_node -
                    sin_phase * sin_node * cos_inclination)),
      quantized_position(
          radius * (cos_phase * sin_node +
                    sin_phase * cos_node * cos_inclination)),
      quantized_position(radius * sin_phase * sin_inclination),
  };
  const double radians_per_second =
      std::numbers::pi_v<double> * 2.0 *
      static_cast<double>(kSimulationHz) /
      static_cast<double>(orbit.period_ticks);
  const SystemVelocityMetresPerSecond velocity{
      quantized_velocity(
          radius * radians_per_second *
          (-sin_phase * cos_node -
           cos_phase * sin_node * cos_inclination)),
      quantized_velocity(
          radius * radians_per_second *
          (-sin_phase * sin_node +
           cos_phase * cos_node * cos_inclination)),
      quantized_velocity(radius * radians_per_second * cos_phase *
                         sin_inclination),
  };
  if (!finite(phase) || !finite(position.x) || !finite(position.y) ||
      !finite(position.z) || !finite(velocity.x) || !finite(velocity.y) ||
      !finite(velocity.z)) {
    return std::unexpected{LocalSystemError::unsafe_arithmetic};
  }
  return PlanetEphemeris{
      .planet = planet,
      .position = position,
      .velocity = velocity,
      .cycle_tick = cycle_tick,
      .phase_radians = phase,
  };
}

auto resolve_origin_station_ephemeris(const LocalSystemDescriptor& system,
                                      const OriginStationDescriptor& station,
                                      EphemerisQueryTime time)
    -> std::expected<OriginStationEphemeris, LocalSystemError> {
  if (system.kind != LocalSystemKind::origin_home ||
      system.seed != station.home_system_seed ||
      system.id.value != station.home_system_seed.value ||
      station.orbit.period_ticks == 0 || station.orbit.radius_kilometres == 0 ||
      !finite(time.sub_tick_fraction) || time.sub_tick_fraction < 0.0 ||
      time.sub_tick_fraction >= 1.0) {
    return std::unexpected{LocalSystemError::invalid_orbit};
  }
  const auto expected = generate_origin_station(station.universe_seed);
  if (expected != station || station.orbit.host_planet !=
                                 generate_origin_home_planet(system.seed).id) {
    return std::unexpected{LocalSystemError::invalid_orbit};
  }
  const auto host =
      resolve_planet_ephemeris(system, station.orbit.host_planet, time);
  if (!host)
    return std::unexpected{host.error()};

  const auto cycle_tick = time.tick % station.orbit.period_ticks;
  constexpr double turn_scale{1.0 / 4'294'967'296.0};
  const double epoch_turns =
      static_cast<double>(station.orbit.epoch_phase_turns) * turn_scale;
  const double cycle_turns =
      (static_cast<double>(cycle_tick) + time.sub_tick_fraction) /
      static_cast<double>(station.orbit.period_ticks);
  const double phase = std::numbers::pi_v<double> * 2.0 *
                       std::fmod(epoch_turns + cycle_turns, 1.0);
  const double inclination =
      static_cast<double>(station.orbit.inclination_microdegrees) *
      (std::numbers::pi_v<double> / 180'000'000.0);
  const double node = std::numbers::pi_v<double> * 2.0 *
                      static_cast<double>(station.orbit.ascending_node_turns) *
                      turn_scale;
  const double radius =
      static_cast<double>(station.orbit.radius_kilometres) * 1'000.0;
  const double cos_phase = std::cos(phase);
  const double sin_phase = std::sin(phase);
  const double cos_node = std::cos(node);
  const double sin_node = std::sin(node);
  const double cos_inclination = std::cos(inclination);
  const double sin_inclination = std::sin(inclination);
  const double relative_x =
      radius * (cos_phase * cos_node - sin_phase * sin_node * cos_inclination);
  const double relative_y =
      radius * (cos_phase * sin_node + sin_phase * cos_node * cos_inclination);
  const double relative_z = radius * sin_phase * sin_inclination;
  const double radians_per_second =
      std::numbers::pi_v<double> * 2.0 * static_cast<double>(kSimulationHz) /
      static_cast<double>(station.orbit.period_ticks);
  const double relative_vx =
      radius * radians_per_second *
      (-sin_phase * cos_node - cos_phase * sin_node * cos_inclination);
  const double relative_vy =
      radius * radians_per_second *
      (-sin_phase * sin_node + cos_phase * cos_node * cos_inclination);
  const double relative_vz =
      radius * radians_per_second * cos_phase * sin_inclination;
  const SystemPositionMetres position{
      quantized_position(host->position.x + relative_x),
      quantized_position(host->position.y + relative_y),
      quantized_position(host->position.z + relative_z)};
  const SystemVelocityMetresPerSecond velocity{
      quantized_velocity(host->velocity.x + relative_vx),
      quantized_velocity(host->velocity.y + relative_vy),
      quantized_velocity(host->velocity.z + relative_vz)};
  if (!finite(phase) || !finite(position.x) || !finite(position.y) ||
      !finite(position.z) || !finite(velocity.x) || !finite(velocity.y) ||
      !finite(velocity.z)) {
    return std::unexpected{LocalSystemError::unsafe_arithmetic};
  }
  return OriginStationEphemeris{
      .station = station.id,
      .host_planet = station.orbit.host_planet,
      .position = position,
      .velocity = velocity,
      .host_relative_position = {quantized_position(relative_x),
                                 quantized_position(relative_y),
                                 quantized_position(relative_z)},
      .host_relative_velocity = {quantized_velocity(relative_vx),
                                 quantized_velocity(relative_vy),
                                 quantized_velocity(relative_vz)},
      .cycle_tick = cycle_tick,
      .phase_radians = phase,
  };
}

auto star_spectral_class_name(StarSpectralClass value) noexcept
    -> std::string_view {
  switch (value) {
    case StarSpectralClass::m: return "M";
    case StarSpectralClass::k: return "K";
    case StarSpectralClass::g: return "G";
    case StarSpectralClass::f: return "F";
  }
  return "unknown";
}

auto local_system_diagnostic_json(const LocalSystemDescriptor& system)
    -> std::expected<std::string, LocalSystemError> {
  if (const auto valid = validate_local_system(system); !valid) {
    return std::unexpected{valid.error()};
  }
  auto result = std::format(
      "{{\n"
      "  \"schema_version\": 1,\n"
      "  \"generator_version\": {},\n"
      "  \"ephemeris_version\": {},\n"
      "  \"system_id\": \"{}\",\n"
      "  \"system_seed\": \"{}\",\n"
      "  \"star\": {{\"id\": \"{}\", \"name\": \"{}\", "
      "\"class\": \"{}\", \"temperature_kelvin\": {}, "
      "\"radius_kilometres\": {}, \"color\": \"#{:02x}{:02x}{:02x}\"}},\n"
      "  \"planets\": [\n",
      kLocalSystemGeneratorVersion, kAnalyticEphemerisVersion,
      system_id_string(system.id), system.seed.value,
      star_id_string(system.star.id), system.star.display_name,
      star_spectral_class_name(system.star.spectral_class),
      system.star.temperature_kelvin, system.star.radius_kilometres,
      system.star.color.red, system.star.color.green, system.star.color.blue);
  for (std::size_t index = 0; index < system.planets.size(); ++index) {
    const auto& planet = system.planets[index];
    result += std::format(
        "    {{\"ordinal\": {}, \"planet_id\": \"planet-{:016x}\", "
        "\"name\": \"{}\", \"orbit_seed\": \"{}\", "
        "\"radius_kilometres\": {}, \"period_ticks\": {}, "
        "\"epoch_phase_turns\": {}, "
        "\"inclination_microdegrees\": {}, "
        "\"ascending_node_turns\": {}}}{}\n",
        index, planet.descriptor.id.value, planet.descriptor.display_name,
        planet.orbit.seed.value, planet.orbit.radius_kilometres,
        planet.orbit.period_ticks, planet.orbit.epoch_phase_turns,
        planet.orbit.inclination_microdegrees,
        planet.orbit.ascending_node_turns,
        index + 1 == system.planets.size() ? "" : ",");
  }
  result += "  ]\n}\n";
  return result;
}

}  // namespace apsis_drift
