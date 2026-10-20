#pragma once

#include "apsis_drift/freedom_flight_save.hpp"
#include "apsis_drift/freedom_jump_targeting.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomLocalBodyJumpVersion{1};
inline constexpr double kFreedomLocalMoonArrivalClearanceMetres{10000.0};
inline constexpr std::uint32_t kFreedomMinorLocalBodyJumpVersion{2};
inline constexpr double kFreedomLocalMinorArrivalClearanceMetres{11000.0};

// source.tick is the actual COMMIT tick after the ordinary kJumpSpoolTicks.
// The outer native journey owns selection/spooling, charges and provenance of
// home_model on return. Outbound home_model must equal the actual source model.
struct FreedomLocalBodyJumpRequest {
  std::uint32_t version{kFreedomLocalBodyJumpVersion};
  FreedomFlightSaveDocument source;
  BodyTarget destination;
  std::uint64_t attempt{1};
  FreedomFlightModel home_model;
  friend auto operator==(const FreedomLocalBodyJumpRequest&,
                         const FreedomLocalBodyJumpRequest&) -> bool = default;
};
struct FreedomLocalBodyClearance {
  FreedomArrivalVolumeAssessment primary;
  bool intersects_moon{}, intersects_minor{};
  [[nodiscard]] auto clear() const -> bool {
    return primary.clear() && !intersects_moon && !intersects_minor;
  }
  friend auto operator==(const FreedomLocalBodyClearance&,
                         const FreedomLocalBodyClearance&) -> bool = default;
};
struct FrozenLocalBodyJumpArrival {
  std::uint32_t version{kFreedomLocalBodyJumpVersion};
  FreedomLocalBodyJumpRequest request;
  std::uint64_t source_checksum{};
  SimulationTick arrival_tick{};
  FreedomFlightSaveDocument arrival;
  SystemPositionMetres source_system_position, arrival_system_position;
  FreedomLocalBodyClearance departure_volume, arrival_volume;
  friend auto operator==(const FrozenLocalBodyJumpArrival&,
                         const FrozenLocalBodyJumpArrival&) -> bool = default;
};
enum class FreedomLocalBodyJumpError : std::uint8_t {
  unsupported_policy,
  invalid_owner,
  invalid_source,
  invalid_destination,
  invalid_home_model,
  invalid_attempt,
  tick_overflow,
  invalid_geometry,
  unclear_departure,
  unclear_arrival,
  invalid_frozen_solution,
};
// Pure deterministic same-system home<->moon geometry. No random scatter,
// relocation of the live craft, charge debit/refill, new clock or risk policy.
// Arrival occurs at source.tick+kJumpTransitTicks and is not a landing promise.
[[nodiscard]] auto freeze_local_body_jump_arrival(
    const FreedomLocalBodyJumpRequest&)
    -> std::expected<FrozenLocalBodyJumpArrival, FreedomLocalBodyJumpError>;
[[nodiscard]] auto validate_frozen_local_body_jump_arrival(
    const FrozenLocalBodyJumpArrival&)
    -> std::expected<void, FreedomLocalBodyJumpError>;
} // namespace apsis_drift
