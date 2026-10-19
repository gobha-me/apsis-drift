#pragma once

#include <cstdint>
#include <expected>

#include "apsis_drift/secondary_bodies.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kPhysicalMoonRecipeVersion{1};
inline constexpr double kPhysicalMoonDensityKilogramsPerCubicMetre{3000.0};
// Recipe1 nominal SI value; not a new source-catalog mass measurement.
inline constexpr double kPhysicalMoonGravitationalConstantSI{6.67430e-11};

// An explicit physical extension of the existing moon, not a PlanetDescriptor
// carrier or a replacement catalog. The complete source owner is retained.
// Recipe1 fixes zero atmosphere/water and a system +Z pole with zero tilt;
// inherited epoch phase remains an exact 32-bit full-turn word.
struct PhysicalMoonDescriptor {
  std::uint32_t version{kPhysicalMoonRecipeVersion};
  SecondaryBodyCatalog catalog;
  BodyId body;
  BodyId parent;
  std::uint32_t radius_metres{};
  std::uint32_t spin_period_ticks{};
  std::uint32_t epoch_spin_phase_turns{};
  Rgb8 tint;
  Seed terrain_seed;
  double density_kilograms_per_cubic_metre{
      kPhysicalMoonDensityKilogramsPerCubicMetre};
  double gravitational_constant_si{kPhysicalMoonGravitationalConstantSI};
  SystemDirection spin_pole_system{0.0, 0.0, 1.0};
  std::uint32_t atmosphere_pressure_millibars{};
  std::uint32_t water_coverage_basis_points{};
  friend auto operator==(const PhysicalMoonDescriptor&,
                         const PhysicalMoonDescriptor&) -> bool = default;
};

// Scalar properties retain their exact authenticated descriptor/owner. This
// adds no ephemeris, local frame, force integration, terrain or save admission.
struct PhysicalMoonProperties {
  PhysicalMoonDescriptor descriptor;
  double mass_kilograms{};
  double gravitational_parameter_metres_cubed_per_second_squared{};
  double surface_gravity_metres_per_second_squared{};
  friend auto operator==(const PhysicalMoonProperties&,
                         const PhysicalMoonProperties&) -> bool = default;
};

enum class PhysicalMoonError : std::uint8_t {
  unsupported_version,
  invalid_catalog,
  invalid_body,
  descriptor_mismatch,
  unsafe_arithmetic,
};

[[nodiscard]] auto generate_physical_moon_descriptor(
    const SecondaryBodyCatalog&, BodyId,
    std::uint32_t version = kPhysicalMoonRecipeVersion)
    -> std::expected<PhysicalMoonDescriptor, PhysicalMoonError>;
[[nodiscard]] auto validate_physical_moon_descriptor(
    const PhysicalMoonDescriptor&) -> std::expected<void, PhysicalMoonError>;
[[nodiscard]] auto resolve_physical_moon_properties(
    const PhysicalMoonDescriptor&)
    -> std::expected<PhysicalMoonProperties, PhysicalMoonError>;

} // namespace apsis_drift
