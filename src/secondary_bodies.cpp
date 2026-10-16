#include "apsis_drift/secondary_bodies.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace apsis_drift {
namespace {
using Error = BodyEphemerisError;
auto orbital_period(std::uint64_t radius_km, double gm_km3_per_second2)
    -> std::expected<SimulationTick, Error> {
  const auto a = static_cast<double>(radius_km);
  const auto seconds =
      2 * std::numbers::pi * std::sqrt(((a * a) * a) / gm_km3_per_second2);
  const auto ticks = std::round(static_cast<double>(kSimulationHz) * seconds);
  if (!std::isfinite(ticks) || ticks < 1 || ticks > 9'007'199'254'740'991.0)
    return std::unexpected{Error::unsafe_arithmetic};
  return static_cast<SimulationTick>(ticks);
}
auto body_seed(const PhysicalLocalSystem& owner, std::uint64_t ordinal)
    -> Seed {
  return derive_seed(derive_seed(owner.catalog.seed, SeedDomain::secondary_body,
                                 kSecondaryBodyCatalogVersion),
                     SeedDomain::secondary_body, ordinal);
}
auto attribute(Seed seed, std::uint64_t ordinal) -> std::uint64_t {
  return derive_seed(seed, SeedDomain::secondary_body, ordinal).value;
}
auto orbit(const PhysicalLocalSystem& owner, Seed seed, std::uint64_t radius,
           SimulationTick period) -> CircularBodyOrbit {
  return {kBodyHierarchyVersion,
          owner.ephemeris_version,
          radius,
          period,
          static_cast<std::uint32_t>(attribute(seed, 3)),
          static_cast<std::int32_t>(attribute(seed, 4) % 20'000'001) -
              10'000'000,
          static_cast<std::uint32_t>(attribute(seed, 5))};
}
auto tint(Seed seed, unsigned low) -> Rgb8 {
  const auto value = attribute(seed, 8);
  return {static_cast<std::uint8_t>(low + value % 48),
          static_cast<std::uint8_t>(low + (value >> 8U) % 48),
          static_cast<std::uint8_t>(low + (value >> 16U) % 48)};
}
} // namespace

auto generate_secondary_body_catalog(const PhysicalLocalSystem& owner,
                                     std::uint32_t version)
    -> std::expected<SecondaryBodyCatalog, Error> {
  if (version != kSecondaryBodyCatalogVersion)
    return std::unexpected{Error::unsupported_version};
  if (!validate_local_system(owner))
    return std::unexpected{Error::invalid_catalog};
  const auto& parent = owner.catalog.planets.front();
  const auto parent_radius = parent.descriptor.radius.value;
  const auto moon_seed = body_seed(owner, 0);
  const auto maximum_moon_km = std::min<std::uint32_t>(1800, parent_radius / 3);
  const auto moon_km =
      600 + attribute(moon_seed, 1) % (maximum_moon_km - 600 + 1);
  // Parent GM uses exactly the existing generated surface-gravity model,
  // converted to km^3/s^2. The Hill fraction is a bounded recipe heuristic,
  // not a multi-body or long-term stability certificate.
  const auto radius = static_cast<double>(parent_radius);
  const auto parent_gm =
      (9.80665 * parent.descriptor.surface_gravity.value / 1000.0) *
      (radius * radius) / 1000.0;
  const auto hill_km =
      static_cast<double>(parent.orbit.radius_kilometres) *
      std::cbrt(parent_gm /
                (3 * static_cast<double>(owner.stellar_gm_km3_per_second2)));
  const auto upper = std::floor(.45 * hill_km);
  const std::uint64_t lower = parent_radius + moon_km + 1000;
  if (!std::isfinite(upper) || upper < static_cast<double>(lower) ||
      upper > 1'000'000)
    return std::unexpected{Error::unsafe_arithmetic};
  const auto moon_orbit =
      lower +
      attribute(moon_seed, 2) % (static_cast<std::uint64_t>(upper) - lower + 1);
  const auto moon_period = orbital_period(moon_orbit, parent_gm);
  if (!moon_period) return std::unexpected{moon_period.error()};
  const auto minor_seed = body_seed(owner, 1);
  const auto minor_km = 50 + attribute(minor_seed, 1) % 201;
  // Outside every existing planetary annulus, with a minimum2,000,000km
  // radial clearance before the small body's own radius is considered.
  const auto minor_orbit =
      owner.catalog.planets.back().orbit.radius_kilometres + 2'000'000 +
      attribute(minor_seed, 2) % 4'000'001;
  const auto minor_period = orbital_period(
      minor_orbit, static_cast<double>(owner.stellar_gm_km3_per_second2));
  if (!minor_period) return std::unexpected{minor_period.error()};
  const SecondaryBodyDescriptor moon{
      {{kBodyIdentityVersion, BodyKind::moon, attribute(moon_seed, 0)},
       body_id(parent.descriptor.id),
       orbit(owner, moon_seed, moon_orbit, *moon_period)},
      static_cast<std::uint32_t>(moon_km * 1000),
      432'000 + static_cast<std::uint32_t>(attribute(moon_seed, 6) % 8'208'001),
      static_cast<std::uint32_t>(attribute(moon_seed, 7)),
      tint(moon_seed, 160)};
  const SecondaryBodyDescriptor minor{
      {{kBodyIdentityVersion, BodyKind::minor, attribute(minor_seed, 0)},
       body_id(owner.catalog.star.id),
       orbit(owner, minor_seed, minor_orbit, *minor_period)},
      static_cast<std::uint32_t>(minor_km * 1000),
      86'400 + static_cast<std::uint32_t>(attribute(minor_seed, 6) % 1'641'601),
      static_cast<std::uint32_t>(attribute(minor_seed, 7)),
      tint(minor_seed, 104)};
  return SecondaryBodyCatalog{version, owner, {moon, minor}};
}

auto validate_secondary_body_catalog(const SecondaryBodyCatalog& catalog)
    -> std::expected<void, Error> {
  const auto expected =
      generate_secondary_body_catalog(catalog.owner, catalog.version);
  if (!expected) return std::unexpected{expected.error()};
  if (catalog != *expected) return std::unexpected{Error::invalid_catalog};
  return {};
}
} // namespace apsis_drift
