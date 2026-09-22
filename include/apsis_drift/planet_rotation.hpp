#pragma once

#include <cstdint>
#include <expected>

#include "apsis_drift/physical_local_system.hpp"
#include "apsis_drift/rigid_body.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kPlanetRotationGeneratorVersion{1};
inline constexpr SimulationTick kPlanetRotationMinimumPeriodTicks{
    6ULL * 3'600ULL * kSimulationHz};
inline constexpr SimulationTick kPlanetRotationMaximumPeriodTicks{
    72ULL * 3'600ULL * kSimulationHz};
inline constexpr double kPlanetRotationMaximumObserverComponentMetres{1.0e12};

// Planet-only recipe. Exact owner/context rederivation is required; this does
// not add a BodyId catalog or a save migration. Source generator dependencies
// are documented in PLANET_ROTATION.md. Existing legacy recipes are unchanged.
struct PlanetRotationRecipe {
  std::uint32_t version{kPlanetRotationGeneratorVersion};
  SystemId system;
  LocalSystemKind catalog_kind{LocalSystemKind::procedural};
  PlanetId planet;
  // Sidereal, not solar-day period. Epoch is authoritative simulation tick 0.
  SimulationTick period_ticks{};
  SimulationTick epoch_phase_tick{};
  std::uint32_t tilt_microdegrees{};
  std::uint32_t pole_azimuth_turns{}; // Full turn = 2^32.
  friend auto operator==(const PlanetRotationRecipe&,
                         const PlanetRotationRecipe&) -> bool = default;
};

struct PlanetRotationGeometry {
  PlanetRotationRecipe recipe;
  SimulationTick tick{};
  SimulationTick cycle_tick{};
  double phase_radians{};
  // Active Hamilton WXYZ planet-fixed -> system-inertial, +Z spin north.
  RigidOrientation fixed_to_system;
  SystemDirection angular_velocity_radians_per_second;
  SystemPositionMetres planet_position;
  SystemVelocityMetresPerSecond planet_velocity;
  StarId star;
  PlanetFixedPositionMetres star_position_fixed;
  PlanetFixedPositionMetres observer_position_fixed;
  PlanetFixedDirection observer_to_star_fixed;
  SystemDirection observer_to_star_system;
  double observer_star_distance_metres{};
  friend auto operator==(const PlanetRotationGeometry&,
                         const PlanetRotationGeometry&) -> bool = default;
};

// A distinct owner wrapper: matching numeric system/planet IDs never turns a
// legacy rotation recipe into a physical-catalog recipe. No implicit unwrap.
enum class PlanetRotationOwnerFamily : std::uint8_t {
  physical_circular = 1,
};
inline constexpr std::uint32_t kPhysicalPlanetRotationOwnerVersion{1};

struct PhysicalPlanetRotationRecipe {
  PlanetRotationOwnerFamily catalog_family{
      PlanetRotationOwnerFamily::physical_circular};
  std::uint32_t owner_version{kPhysicalPlanetRotationOwnerVersion};
  std::uint32_t physical_catalog_generator{
      kPhysicalLocalSystemGeneratorVersion};
  std::uint32_t source_catalog_generator{kLocalSystemGeneratorVersion};
  std::uint32_t ephemeris_version{kAnalyticEphemerisVersion};
  std::optional<Seed> origin_universe_seed;
  PlanetRotationRecipe rotation;

  friend auto operator==(const PhysicalPlanetRotationRecipe&,
                         const PhysicalPlanetRotationRecipe&) -> bool = default;
};

struct PhysicalPlanetRotationGeometry {
  PhysicalPlanetRotationRecipe recipe;
  // Reused numerical payload, not an independently legacy-owned result. Keep
  // the outer recipe with it; no native flight-frame or save migration implied.
  PlanetRotationGeometry geometry;

  friend auto operator==(const PhysicalPlanetRotationGeometry&,
                         const PhysicalPlanetRotationGeometry&)
      -> bool = default;
};

enum class PlanetRotationError : std::uint8_t {
  unsupported_version,
  invalid_world_context,
  unknown_planet,
  owner_mismatch,
  invalid_recipe,
  invalid_tick,
  invalid_observer,
  observer_at_star,
  unsafe_arithmetic,
};

[[nodiscard]] auto generate_planet_rotation_recipe(
    const LocalSystemDescriptor& system, PlanetId planet,
    std::uint32_t version = kPlanetRotationGeneratorVersion)
    -> std::expected<PlanetRotationRecipe, PlanetRotationError>;

// One authoritative integer tick, no wall clock or render fraction. The sole
// existing system star is at the system origin. Optional fixed-frame observer
// permits finite-distance illumination; zero means the planet centre. This is
// point-star geometry, not visibility/occlusion, stellar-disc or climate logic.
// Generated pole and final orientation each normalize once; stored
// RigidBodyState is untouched.
[[nodiscard]] auto resolve_planet_rotation(
    const LocalSystemDescriptor& system, const PlanetRotationRecipe& recipe,
    SimulationTick tick, PlanetFixedPositionMetres observer = {})
    -> std::expected<PlanetRotationGeometry, PlanetRotationError>;

[[nodiscard]] auto generate_planet_rotation_recipe(
    const PhysicalLocalSystem& system, PlanetId planet,
    std::uint32_t version = kPlanetRotationGeneratorVersion)
    -> std::expected<PhysicalPlanetRotationRecipe, PlanetRotationError>;

[[nodiscard]] auto resolve_planet_rotation(
    const PhysicalLocalSystem& system,
    const PhysicalPlanetRotationRecipe& recipe, SimulationTick tick,
    PlanetFixedPositionMetres observer = {})
    -> std::expected<PhysicalPlanetRotationGeometry, PlanetRotationError>;

} // namespace apsis_drift
