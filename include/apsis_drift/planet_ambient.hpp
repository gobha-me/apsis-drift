#pragma once

#include <cstddef>

#include "apsis_drift/physical_local_system.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kPlanetAmbientGeneratorVersion{1};
inline constexpr std::size_t kMaximumPlanetAmbientRecipeBytes{1'024};

// Retain this explicit selection with any future saved consumer. Historical
// flight saves have no ambient selection and are not silently upgraded.
struct PlanetAmbientRecipe {
  std::uint32_t version{kPlanetAmbientGeneratorVersion};
  std::uint32_t physical_catalog{kPhysicalLocalSystemGeneratorVersion};
  std::uint32_t ephemeris{kAnalyticEphemerisVersion};
  SystemId system;
  Seed system_seed;
  LocalSystemKind catalog_kind{LocalSystemKind::procedural};
  std::optional<Seed> origin_universe_seed;
  PlanetId planet;
  friend auto operator==(const PlanetAmbientRecipe&, const PlanetAmbientRecipe&)
      -> bool = default;
};

enum class AtmosphericVisibilityKind : std::uint8_t {
  airless,
  clear,
  cloud,
  dust,
};

// Immutable surface envelopes, not instantaneous readings or storm state.
// Integer units avoid host-libm climate dependencies. No clock is stored here.
struct PlanetAmbientEnvironment {
  PlanetAmbientRecipe recipe;
  SurfaceGravityMilliG surface_gravity;
  AtmosphereClass atmosphere_class{};
  AtmospherePressureMillibars surface_pressure;
  std::uint32_t minimum_temperature_millikelvin{};    // 30,000..1,400,000
  std::uint32_t maximum_temperature_millikelvin{};    // min..1,400,000
  std::uint32_t radiation_nanosieverts_per_hour{};    // 0..1,000,000,000
  std::uint32_t electric_field_volts_per_metre{};     // 0..100,000
  std::uint32_t gust_millimetres_per_second{};        // 0..200,000
  std::uint32_t ground_acceleration_mm_per_second2{}; // 0..20,000
  AtmosphericVisibilityKind visibility{};
  std::uint16_t maximum_obscuration_basis_points{}; // 0..10,000
  friend auto operator==(const PlanetAmbientEnvironment&,
                         const PlanetAmbientEnvironment&) -> bool = default;
};

enum class PlanetAmbientError : std::uint8_t {
  unsupported_version,
  invalid_context,
  unknown_planet,
  owner_mismatch,
  invalid_environment,
  invalid_operation,
  invalid_recipe_document,
};

[[nodiscard]] auto generate_planet_ambient_recipe(
    const PhysicalLocalSystem&, PlanetId,
    std::uint32_t version = kPlanetAmbientGeneratorVersion)
    -> std::expected<PlanetAmbientRecipe, PlanetAmbientError>;
[[nodiscard]] auto resolve_planet_ambient_environment(
    const PhysicalLocalSystem&, const PlanetAmbientRecipe&)
    -> std::expected<PlanetAmbientEnvironment, PlanetAmbientError>;

// Bounds/coherence only for explicitly authored reference fixtures. It does
// not certify seed ownership; generated callers must use the resolver above.
[[nodiscard]] auto validate_planet_ambient_environment(
    const PlanetAmbientEnvironment&) -> std::expected<void, PlanetAmbientError>;

enum class AmbientSurfaceOperation : std::uint8_t {
  outdoor,
  landing,
  atmospheric_transit,
};
struct AmbientSurfaceExposure {
  PlanetAmbientEnvironment environment;
  // Geologic motion affects supported contact, not airborne acceleration.
  bool ground_motion_applies{};
  friend auto operator==(const AmbientSurfaceExposure&,
                         const AmbientSurfaceExposure&) -> bool = default;
};
// Surface/atmosphere design envelopes, never altitude/time-resolved weather,
// knowledge or equipment ratings. Transit still has surface reference bounds.
[[nodiscard]] auto evaluate_ambient_surface_exposure(const PhysicalLocalSystem&,
                                                     const PlanetAmbientRecipe&,
                                                     AmbientSurfaceOperation)
    -> std::expected<AmbientSurfaceExposure, PlanetAmbientError>;

[[nodiscard]] auto atmospheric_visibility_name(
    AtmosphericVisibilityKind) noexcept -> std::string_view;
// Closed, bounded recipe record; full world validation on both encode/decode.
// No generated hazard catalog or mutable simulation state is serialized.
[[nodiscard]] auto encode_planet_ambient_recipe_json(const PhysicalLocalSystem&,
                                                     const PlanetAmbientRecipe&)
    -> std::expected<std::string, PlanetAmbientError>;
[[nodiscard]] auto decode_planet_ambient_recipe_json(const PhysicalLocalSystem&,
                                                     std::string_view)
    -> std::expected<PlanetAmbientRecipe, PlanetAmbientError>;
} // namespace apsis_drift
