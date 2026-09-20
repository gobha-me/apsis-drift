#include "apsis_drift/physical_local_system.hpp"
#include "local_system_internal.hpp"

#include <cmath>
#include <limits>
#include <numbers>
#include <ranges>
#include <utility>

namespace apsis_drift {
namespace {
static_assert(kSeedDerivationVersion == 1 &&
              kLocalSystemGeneratorVersion == 1 &&
              kAnalyticEphemerisVersion == 1 && kPlanetGeneratorVersion == 1 &&
              kOriginHomePlanetGeneratorVersion == 1 &&
              kOriginStationGeneratorVersion == 2);
static_assert(kNominalSolarGMKilometresCubedPerSecondSquared % 1'000 == 0);
constexpr double tau{2.0 * std::numbers::pi_v<double>};
constexpr std::uint64_t minimum_radius_km{4'000'000};
constexpr std::uint64_t maximum_radius_km{68'000'000};
// Conservative bounds enclosing the entire radius/mass recipe domain. Both
// period and cycle_tick convert exactly to binary64, even at maximum tick.
constexpr SimulationTick minimum_period_ticks{13'000'000};
constexpr SimulationTick maximum_period_ticks{4'200'000'000ULL};

auto mass_for(Seed star_seed) -> std::uint32_t {
  // Star streams 1=name and 2=legacy physical remain untouched. Stream 3 is
  // this recipe family's independent mass stream; fixed SplitMix64 finalizer.
  auto value = derive_seed(star_seed, SeedDomain::star, 3).value;
  value += 0x9E3779B97F4A7C15ULL;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  value ^= value >> 31U;
  return kMinimumStellarMassMillisolar +
         static_cast<std::uint32_t>(value %
                                    (kMaximumStellarMassMillisolar -
                                     kMinimumStellarMassMillisolar + 1ULL));
}

auto physical_catalog(LocalSystemDescriptor catalog,
                      std::optional<Seed> universe_seed)
    -> std::expected<PhysicalLocalSystem, PhysicalLocalSystemError> {
  const auto mass = mass_for(catalog.star.seed);
  const std::uint64_t gm =
      (kNominalSolarGMKilometresCubedPerSecondSquared / 1'000) * mass;
  for (auto& planet : catalog.planets) {
    const auto radius = planet.orbit.radius_kilometres;
    if (radius < minimum_radius_km || radius > maximum_radius_km)
      return std::unexpected{PhysicalLocalSystemError::unsafe_arithmetic};
    const double a = static_cast<double>(radius);
    // Recipe operation order is deliberate. No integer cube, pow, host clock
    // or independently sampled velocity period. Positive ties round upward.
    const double cube = (a * a) * a;
    const double seconds = tau * std::sqrt(cube / static_cast<double>(gm));
    const double ticks =
        std::round(static_cast<double>(kSimulationHz) * seconds);
    if (!std::isfinite(ticks) || ticks < minimum_period_ticks ||
        ticks > maximum_period_ticks)
      return std::unexpected{PhysicalLocalSystemError::unsafe_arithmetic};
    planet.orbit.period_ticks = static_cast<SimulationTick>(ticks);
  }
  return PhysicalLocalSystem{.origin_universe_seed = universe_seed,
                             .catalog = std::move(catalog),
                             .stellar_mass_millisolar = mass,
                             .stellar_gm_km3_per_second2 = gm};
}
} // namespace

auto generate_physical_local_system(Seed system_seed, std::uint32_t version)
    -> std::expected<PhysicalLocalSystem, PhysicalLocalSystemError> {
  if (version != kPhysicalLocalSystemGeneratorVersion)
    return std::unexpected{PhysicalLocalSystemError::unsupported_version};
  return physical_catalog(generate_local_system(system_seed), std::nullopt);
}

auto generate_physical_origin_system(Seed universe_seed, std::uint32_t version)
    -> std::expected<PhysicalLocalSystem, PhysicalLocalSystemError> {
  if (version != kPhysicalLocalSystemGeneratorVersion)
    return std::unexpected{PhysicalLocalSystemError::unsupported_version};
  return physical_catalog(generate_origin_system(universe_seed), universe_seed);
}

auto validate_local_system(const PhysicalLocalSystem& system)
    -> std::expected<void, PhysicalLocalSystemError> {
  if (system.generator_version != kPhysicalLocalSystemGeneratorVersion ||
      system.source_catalog_generator != kLocalSystemGeneratorVersion ||
      system.ephemeris_version != kAnalyticEphemerisVersion)
    return std::unexpected{PhysicalLocalSystemError::unsupported_version};
  if ((system.catalog.kind != LocalSystemKind::procedural &&
       system.catalog.kind != LocalSystemKind::origin_home) ||
      (system.catalog.kind == LocalSystemKind::origin_home) !=
          system.origin_universe_seed.has_value() ||
      system.catalog.id.value != system.catalog.seed.value ||
      system.catalog.planets.size() < kMinimumLocalSystemPlanets ||
      system.catalog.planets.size() > kMaximumLocalSystemPlanets ||
      system.stellar_mass_millisolar < kMinimumStellarMassMillisolar ||
      system.stellar_mass_millisolar > kMaximumStellarMassMillisolar)
    return std::unexpected{PhysicalLocalSystemError::invalid_context};
  const auto expected =
      system.origin_universe_seed
          ? generate_physical_origin_system(*system.origin_universe_seed)
          : generate_physical_local_system(system.catalog.seed);
  if (!expected) return std::unexpected{expected.error()};
  if (system != *expected)
    return std::unexpected{PhysicalLocalSystemError::invalid_context};
  return {};
}

auto find_local_system_planet(const PhysicalLocalSystem& system,
                              PlanetId planet)
    -> std::expected<const LocalSystemPlanet*, PhysicalLocalSystemError> {
  const auto valid = validate_local_system(system);
  if (!valid) return std::unexpected{valid.error()};
  const auto found = std::ranges::find_if(
      system.catalog.planets, [planet](const LocalSystemPlanet& body) {
        return body.descriptor.id == planet;
      });
  if (found == system.catalog.planets.end())
    return std::unexpected{PhysicalLocalSystemError::unknown_planet};
  return &*found;
}

auto resolve_planet_ephemeris(const PhysicalLocalSystem& system,
                              PlanetId planet, EphemerisQueryTime time)
    -> std::expected<PlanetEphemeris, PhysicalLocalSystemError> {
  if (!std::isfinite(time.sub_tick_fraction) || time.sub_tick_fraction < 0 ||
      time.sub_tick_fraction >= 1)
    return std::unexpected{PhysicalLocalSystemError::non_finite_time};
  if (time.tick == std::numeric_limits<SimulationTick>::max())
    return std::unexpected{PhysicalLocalSystemError::invalid_tick};
  const auto body = find_local_system_planet(system, planet);
  if (!body) return std::unexpected{body.error()};
  const auto result =
      detail::resolve_validated_circular_orbit((*body)->orbit, time);
  if (!result)
    return std::unexpected{PhysicalLocalSystemError::unsafe_arithmetic};
  return *result;
}
} // namespace apsis_drift
