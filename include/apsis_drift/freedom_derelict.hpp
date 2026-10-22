#pragma once

#include "apsis_drift/freedom_save.hpp"
#include "apsis_drift/freedom_system_network.hpp"
#include "apsis_drift/rigid_body.hpp"

#include <cstdint>
#include <expected>
#include <optional>

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomDerelictVersion{1};
inline constexpr double kFreedomDerelictContactRangeMetres{100'000'000.0};
inline constexpr double kFreedomDerelictInspectionMinimumMetres{100.0};
inline constexpr double kFreedomDerelictInspectionMaximumMetres{500.0};
inline constexpr double kFreedomDerelictInspectionMaximumSpeedMetresPerSecond{
    20.0};

struct FreedomDerelictDescriptor {
  std::uint32_t version{kFreedomDerelictVersion};
  Seed universe_seed;
  std::uint64_t id{};
  SystemId system;
  PlanetId planet;
  PhysicalLocalSystem owner;
  std::uint64_t radius_kilometres{};
  SimulationTick period_ticks{};
  std::uint32_t epoch_phase_turns{};
  std::int32_t inclination_microdegrees{};
  std::uint32_t ascending_node_turns{};
  friend auto operator==(const FreedomDerelictDescriptor&,
                         const FreedomDerelictDescriptor&) -> bool = default;
};
// Observer craft state in the descriptor's actual parent-planet-relative,
// system-aligned inertial axes, not system position or planet-fixed
// coordinates.
struct FreedomDerelictObservation {
  StarterCraftId craft;
  SimulationTick tick{};
  RigidVector3 position_metres, velocity_metres_per_second;
  friend auto operator==(const FreedomDerelictObservation&,
                         const FreedomDerelictObservation&) -> bool = default;
};
struct FreedomDerelictState {
  std::uint32_t version{kFreedomDerelictVersion};
  std::optional<FreedomDerelictObservation> contact, inspection;
  friend auto operator==(const FreedomDerelictState&,
                         const FreedomDerelictState&) -> bool = default;
};
enum class FreedomDerelictError : std::uint8_t {
  unsupported_version,
  invalid_owner,
  invalid_descriptor,
  invalid_tick,
  unsafe_arithmetic,
  invalid_observation,
  contact_out_of_range,
  inspection_out_of_range,
  inspection_speed_exceeded,
  invalid_state,
};

[[nodiscard]] auto generate_freedom_derelict(Seed)
    -> std::expected<FreedomDerelictDescriptor, FreedomDerelictError>;
[[nodiscard]] auto validate_freedom_derelict_descriptor(
    const FreedomDerelictDescriptor&)
    -> std::expected<void, FreedomDerelictError>;
// Canonical unpowered Wayfarer pose in the genuine parent's relative inertial
// frame. No active pilot, drive, contact/collision response or simulation tick.
[[nodiscard]] auto resolve_freedom_derelict(const FreedomDerelictDescriptor&,
                                            SimulationTick)
    -> std::expected<RigidBodyState, FreedomDerelictError>;
// Historical craft/lineage authorization belongs to the voyage save owner.
// Here each finite record authenticates the same canonical object at its tick.
[[nodiscard]] auto validate_freedom_derelict_state(Seed,
                                                   const FreedomDerelictState&,
                                                   SimulationTick now)
    -> std::expected<void, FreedomDerelictError>;
} // namespace apsis_drift
