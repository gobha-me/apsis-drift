#pragma once

#include "apsis_drift/orbital_telemetry.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kAtmosphericFlightVersion{1};
struct AtmosphericFlightRecipe {
  std::uint32_t version{kAtmosphericFlightVersion};
  friend auto operator==(const AtmosphericFlightRecipe&,
                         const AtmosphericFlightRecipe&) -> bool = default;
};
enum class AtmosphericFlightErrorCode : std::uint8_t {
  unsupported_version,
  gravity_failure,
  rotation_failure,
  invalid_atmosphere,
  unsupported_craft,
  unsafe_arithmetic,
  dynamics_failure,
  observation_failure,
};
struct AtmosphericFlightError {
  AtmosphericFlightErrorCode code;
  std::optional<CentralBodyError> gravity;
  std::optional<PlanetRotationError> rotation;
  std::optional<VacuumDynamicsError> dynamics;
};
struct AtmosphericFlightSample {
  PlanetId planet;
  AtmosphereClass atmosphere_class{};
  double altitude_metres{}, space_boundary_altitude_metres{};
  double density_kg_per_cubic_metre{}, pressure_millibars{};
  double air_speed_metres_per_second{}, dynamic_pressure_pascals{};
  double angle_of_attack_radians{}, radial_rate_metres_per_second{};
  RigidVector3 air_relative_velocity_metres_per_second;
  RigidVector3 drag_force_body_newtons, lift_force_body_newtons;
  RigidVector3 passive_torque_body_newton_metres;
  RigidVector3 control_torque_body_newton_metres;
  RigidVector3 control_authority_fraction;
  bool within_rated_envelope{};
  friend auto operator==(const AtmosphericFlightSample&,
                         const AtmosphericFlightSample&) -> bool = default;
};
struct AtmosphericFlightActuation {
  CentralBodyActuation central;
  AtmosphericFlightSample initial, after;
  OrbitalTelemetry observation_after;
  RigidVector3 aerodynamic_linear_impulse_newton_seconds;
  RigidVector3 aerodynamic_angular_impulse_newton_metre_seconds;
};

// Qualified physical owner and named nonrotating planet frame only. The query
// accepts rotation control fractions because surface torque depends on them.
// No weather, collision, thermal damage, fuel or hidden controller history.
[[nodiscard]] auto evaluate_atmospheric_flight(
    const RigidBodyWorldContext&, const RigidBodyState&, const VacuumIntent&,
    const PhysicalPlanetRotationRecipe&, AtmosphericFlightRecipe = {},
    CentralBodyDynamicsRecipe = {})
    -> std::expected<AtmosphericFlightSample, AtmosphericFlightError>;

// Uses the same coupled RK4 and central gravity at every stage. Environmental
// impulses stay separate from gross propulsion and cannot become fuel channels.
// Candidate dynamics and post-step observation must both qualify before commit.
[[nodiscard]] auto advance_atmospheric_flight(
    const RigidBodyWorldContext&, RigidBodyState&, const VacuumIntent&,
    const PhysicalPlanetRotationRecipe&, AtmosphericFlightRecipe = {},
    CentralBodyDynamicsRecipe = {}, SimulationSeconds = kSimulationStep)
    -> std::expected<AtmosphericFlightActuation, AtmosphericFlightError>;
} // namespace apsis_drift
