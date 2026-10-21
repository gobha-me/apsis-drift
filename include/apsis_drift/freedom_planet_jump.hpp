#pragma once

#include "apsis_drift/freedom_local_jump.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomPlanetJumpVersion{1};
inline constexpr std::uint32_t kFreedomRecoveryPlanetJumpVersion{2};
inline constexpr double kFreedomPlanetJumpArrivalClearanceMetres{100000.0};

// The source is the actual commit-time flight, not a whole voyage snapshot.
// The native owner separately requires a known destination and bills a charge.
struct FreedomPlanetJumpRequest {
  std::uint32_t version{kFreedomPlanetJumpVersion};
  FreedomFlightSaveDocument source;
  BodyTarget destination;
  std::uint64_t attempt{1};
  std::optional<FreedomDriveRecoveryState> recovery{};
  friend auto operator==(const FreedomPlanetJumpRequest&,
                         const FreedomPlanetJumpRequest&) -> bool = default;
};
struct FrozenFreedomPlanetJumpArrival {
  std::uint32_t version{kFreedomPlanetJumpVersion};
  FreedomPlanetJumpRequest request;
  std::uint64_t source_checksum{};
  SimulationTick arrival_tick{};
  FreedomFlightSaveDocument arrival;
  SystemPositionMetres source_system_position, arrival_system_position;
  FreedomLocalBodyClearance departure_volume, arrival_volume;
  std::optional<FrozenFreedomJumpUncertainty> recovery{};
  friend auto operator==(const FrozenFreedomPlanetJumpArrival&,
                         const FrozenFreedomPlanetJumpArrival&)
      -> bool = default;
};
enum class FreedomPlanetJumpError : std::uint8_t {
  unsupported_policy,
  invalid_owner,
  invalid_source,
  invalid_destination,
  invalid_attempt,
  tick_overflow,
  invalid_geometry,
  unclear_departure,
  unclear_arrival,
  invalid_frozen_solution,
};

// Same-system ordinary planets only. Arrival is commit+kJumpTransitTicks,
// with a policy1 baseline on the genuine future fixed +Y ray, 100km beyond its
// atmosphere/full terrain envelope and conservative craft hull radius. Preserve
// source-relative inertial velocity, full attitude/angular state and model;
// clear the source-bound orbit-hold target. No surface co-motion, orbit
// insertion, live-state mutation, fuel accounting or landing claim.
// Explicit policy2 adds recovery1 location error up to2km, leaving at least
// 98km beyond both target envelopes and hull; baseline1 remains exact.
[[nodiscard]] auto freeze_freedom_planet_jump_arrival(
    const FreedomPlanetJumpRequest&)
    -> std::expected<FrozenFreedomPlanetJumpArrival, FreedomPlanetJumpError>;
[[nodiscard]] auto validate_frozen_freedom_planet_jump_arrival(
    const FrozenFreedomPlanetJumpArrival&)
    -> std::expected<void, FreedomPlanetJumpError>;
} // namespace apsis_drift
