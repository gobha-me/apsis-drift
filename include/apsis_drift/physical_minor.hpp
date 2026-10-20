#pragma once

#include <cstdint>
#include <expected>

#include "apsis_drift/secondary_bodies.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kPhysicalMinorRecipeVersion{1};
inline constexpr double kPhysicalMinorDensityKilogramsPerCubicMetre{3000.0};
// Recipe1 nominal SI value; not a new source-catalog mass measurement.
inline constexpr double kPhysicalMinorGravitationalConstantSI{6.67430e-11};

// An explicit physical extension of the existing minor, not a PlanetDescriptor
// carrier or a replacement catalog. The complete source owner is retained.
// Recipe1 fixes zero atmosphere/water and a system +Z pole with zero tilt;
// inherited epoch phase remains an exact 32-bit full-turn word.
struct PhysicalMinorDescriptor {
  std::uint32_t version{kPhysicalMinorRecipeVersion};
  SecondaryBodyCatalog catalog;
  BodyId body;
  BodyId parent;
  std::uint32_t radius_metres{};
  std::uint32_t spin_period_ticks{};
  std::uint32_t epoch_spin_phase_turns{};
  Rgb8 tint;
  double density_kilograms_per_cubic_metre{
      kPhysicalMinorDensityKilogramsPerCubicMetre};
  double gravitational_constant_si{kPhysicalMinorGravitationalConstantSI};
  SystemDirection spin_pole_system{0.0, 0.0, 1.0};
  std::uint32_t atmosphere_pressure_millibars{};
  std::uint32_t water_coverage_basis_points{};
  friend auto operator==(const PhysicalMinorDescriptor&,
                         const PhysicalMinorDescriptor&) -> bool = default;
};

// Scalar properties retain their exact authenticated descriptor/owner. This
// adds no ephemeris, local frame, force integration, surface or save admission.
struct PhysicalMinorProperties {
  PhysicalMinorDescriptor descriptor;
  double mass_kilograms{};
  double gravitational_parameter_metres_cubed_per_second_squared{};
  double surface_gravity_metres_per_second_squared{};
  friend auto operator==(const PhysicalMinorProperties&,
                         const PhysicalMinorProperties&) -> bool = default;
};

enum class PhysicalMinorError : std::uint8_t {
  unsupported_version,
  invalid_catalog,
  invalid_body,
  descriptor_mismatch,
  unsafe_arithmetic,
};

[[nodiscard]] auto generate_physical_minor_descriptor(
    const SecondaryBodyCatalog&, BodyId,
    std::uint32_t version = kPhysicalMinorRecipeVersion)
    -> std::expected<PhysicalMinorDescriptor, PhysicalMinorError>;
[[nodiscard]] auto validate_physical_minor_descriptor(
    const PhysicalMinorDescriptor&) -> std::expected<void, PhysicalMinorError>;
[[nodiscard]] auto resolve_physical_minor_properties(
    const PhysicalMinorDescriptor&)
    -> std::expected<PhysicalMinorProperties, PhysicalMinorError>;

} // namespace apsis_drift
