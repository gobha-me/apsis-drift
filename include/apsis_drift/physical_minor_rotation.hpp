#pragma once

#include "apsis_drift/rigid_body.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kPhysicalMinorRotationVersion{1};
inline constexpr double kPhysicalMinorRotationMaximumObserverComponentMetres{
    1e12};

// Minor-owned geometry only. The original secondary period/epoch word and the
// physical recipe's system +Z pole are authoritative; no planet rotation
// recipe, rendering clock or new orbit is substituted.
struct PhysicalMinorRotationGeometry {
  std::uint32_t version{kPhysicalMinorRotationVersion};
  PhysicalMinorDescriptor descriptor;
  SimulationTick tick{}, cycle_tick{};
  double phase_radians{};
  RigidOrientation fixed_to_system;
  SystemDirection angular_velocity_radians_per_second;
  SystemPositionMetres body_position;
  SystemVelocityMetresPerSecond body_velocity;
  StarId star;
  RigidVector3 star_position_fixed, observer_position_fixed;
  RigidVector3 observer_to_star_fixed;
  SystemDirection observer_to_star_system;
  double observer_star_distance_metres{};
  friend auto operator==(const PhysicalMinorRotationGeometry&,
                         const PhysicalMinorRotationGeometry&)
      -> bool = default;
};
enum class PhysicalMinorRotationError : std::uint8_t {
  unsupported_version,
  invalid_owner,
  invalid_tick,
  invalid_observer,
  observer_at_star,
  unsafe_arithmetic,
};

// Exactly one integer simulation tick; zero observer means the body centre.
// Geometry authenticates the complete physical owner before ephemeris/math.
// Point-star illumination is finite-distance geometry, not occultation or a
// climate/landing claim. Source descriptors and rigid states remain untouched.
[[nodiscard]] auto resolve_physical_minor_rotation(
    const PhysicalMinorDescriptor&, SimulationTick,
    RigidVector3 observer_fixed = {})
    -> std::expected<PhysicalMinorRotationGeometry, PhysicalMinorRotationError>;
} // namespace apsis_drift
