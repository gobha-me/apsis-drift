#pragma once

#include <optional>

#include "apsis_drift/local_system.hpp"

namespace apsis_drift {

// An explicitly selected recipe family, not an upgrade of legacy catalog v1.
inline constexpr std::string_view kPhysicalLocalSystemRecipeFamily{
    "physical_circular"};
inline constexpr std::uint32_t kPhysicalLocalSystemGeneratorVersion{1};
inline constexpr std::uint32_t kMinimumStellarMassMillisolar{80};
inline constexpr std::uint32_t kMaximumStellarMassMillisolar{1'600};
// IAU 2015 B3 nominal solar GM, exact nominal conversion constant.
inline constexpr std::uint64_t kNominalSolarGMKilometresCubedPerSecondSquared{
    132'712'440'000ULL};

struct PhysicalLocalSystem {
  std::uint32_t generator_version{kPhysicalLocalSystemGeneratorVersion};
  std::uint32_t source_catalog_generator{kLocalSystemGeneratorVersion};
  std::uint32_t ephemeris_version{kAnalyticEphemerisVersion};
  // Present only for an origin-home catalog. Never infer a universe from a
  // system seed or accept a procedural namesake in place of its authored home.
  std::optional<Seed> origin_universe_seed;
  LocalSystemDescriptor catalog;
  std::uint32_t stellar_mass_millisolar{};
  std::uint64_t stellar_gm_km3_per_second2{};

  friend auto operator==(const PhysicalLocalSystem&, const PhysicalLocalSystem&)
      -> bool = default;
};

enum class PhysicalLocalSystemError : std::uint8_t {
  unsupported_version,
  invalid_context,
  unknown_planet,
  non_finite_time,
  invalid_tick,
  unsafe_arithmetic,
};

[[nodiscard]] auto generate_physical_local_system(
    Seed system_seed,
    std::uint32_t version = kPhysicalLocalSystemGeneratorVersion)
    -> std::expected<PhysicalLocalSystem, PhysicalLocalSystemError>;

[[nodiscard]] auto generate_physical_origin_system(
    Seed universe_seed,
    std::uint32_t version = kPhysicalLocalSystemGeneratorVersion)
    -> std::expected<PhysicalLocalSystem, PhysicalLocalSystemError>;

[[nodiscard]] auto validate_local_system(const PhysicalLocalSystem& system)
    -> std::expected<void, PhysicalLocalSystemError>;

[[nodiscard]] auto find_local_system_planet(const PhysicalLocalSystem& system,
                                            PlanetId planet)
    -> std::expected<const LocalSystemPlanet*, PhysicalLocalSystemError>;

// Same integer simulation clock, unchanged circular geometry/quantization.
// The embedded catalog intentionally fails legacy catalog validation.
// No physical station, native adapter or saved-career migration is implied.
[[nodiscard]] auto resolve_planet_ephemeris(const PhysicalLocalSystem& system,
                                            PlanetId planet,
                                            EphemerisQueryTime time)
    -> std::expected<PlanetEphemeris, PhysicalLocalSystemError>;

} // namespace apsis_drift
