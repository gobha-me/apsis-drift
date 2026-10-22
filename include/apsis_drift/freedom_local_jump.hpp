#pragma once

#include "apsis_drift/freedom_flight_save.hpp"
#include "apsis_drift/freedom_jump_targeting.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomLocalBodyJumpVersion{1};
inline constexpr double kFreedomLocalMoonArrivalClearanceMetres{10000.0};
inline constexpr std::uint32_t kFreedomMinorLocalBodyJumpVersion{2};
inline constexpr std::uint32_t kFreedomRecoveryLocalBodyJumpVersion{3};
inline constexpr std::uint32_t kFreedomRecoveryMinorLocalBodyJumpVersion{4};
inline constexpr double kFreedomLocalMinorArrivalClearanceMetres{11000.0};
inline constexpr std::uint32_t kFreedomForeignMoonJumpVersion{5};
inline constexpr std::uint32_t kFreedomRecoveryForeignMoonJumpVersion{6};
inline constexpr std::uint32_t kFreedomForeignMinorJumpVersion{7};
inline constexpr std::uint32_t kFreedomRecoveryForeignMinorJumpVersion{8};

// source.tick is the actual COMMIT tick after the ordinary kJumpSpoolTicks.
// The outer native journey owns selection/spooling, charges and provenance of
// home_model on return. Legacy outbound home_model equals the source model.
// Foreign5/6 use the actual parent expedition model with only hold cleared;
// their outer foreign checkpoint binds that same profile on direct Home return.
// Foreign7/8 use that same expedition-profile rule from the first entry planet;
// the minor retains its actual stellar parent.
struct FreedomLocalBodyJumpRequest {
  std::uint32_t version{kFreedomLocalBodyJumpVersion};
  FreedomFlightSaveDocument source;
  BodyTarget destination;
  std::uint64_t attempt{1};
  FreedomFlightModel home_model;
  std::optional<FreedomDriveRecoveryState> recovery{};
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
  std::optional<FrozenFreedomJumpUncertainty> recovery{};
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
  beyond_qualified_policy,
};
// Legacy1/2 preserve exact home<->Moon/minor geometry. Recovery3/4 require
// explicit recovery1 input and add its bounded independent location error.
// No relocation of the live craft, charge debit/refill or independent clock.
// Arrival occurs at source.tick+kJumpTransitTicks and is not a landing promise.
[[nodiscard]] auto freeze_local_body_jump_arrival(
    const FreedomLocalBodyJumpRequest&)
    -> std::expected<FrozenLocalBodyJumpArrival, FreedomLocalBodyJumpError>;
[[nodiscard]] auto validate_frozen_local_body_jump_arrival(
    const FrozenLocalBodyJumpArrival&)
    -> std::expected<void, FreedomLocalBodyJumpError>;
} // namespace apsis_drift
