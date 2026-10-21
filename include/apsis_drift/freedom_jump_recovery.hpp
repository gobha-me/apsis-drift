#pragma once

#include "apsis_drift/body_ephemeris.hpp"
#include "apsis_drift/freedom_save.hpp"
#include "apsis_drift/rigid_body.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomDriveRecoveryVersion{1};
inline constexpr SimulationTick kFreedomDriveRecoveryTicks{5400};
inline constexpr std::uint32_t kFreedomJumpRecoveryMaximumRadiusMillimetres{
    2000000};
enum class FreedomJumpRecoveryKind : std::uint8_t {
  system,
  planet,
  moon,
  minor
};
struct FreedomJumpRecoveryStamp {
  FreedomJumpRecoveryKind kind{};
  std::uint64_t attempt{}, source_checksum{};
  SimulationTick commit_tick{}, arrival_tick{};
  friend auto operator==(const FreedomJumpRecoveryStamp&,
                         const FreedomJumpRecoveryStamp&) -> bool = default;
};
struct FreedomDriveRecoveryState {
  std::uint32_t version{kFreedomDriveRecoveryVersion};
  std::optional<FreedomCraftLineage> lineage{};
  std::optional<FreedomJumpRecoveryStamp> last_use{};
  friend auto operator==(const FreedomDriveRecoveryState&,
                         const FreedomDriveRecoveryState&) -> bool = default;
};
struct FreedomJumpRecoveryReading {
  SimulationTick remaining_ticks{};
  std::uint32_t extra_radius_millimetres{};
  bool ready{}, pending_arrival{};
  friend auto operator==(const FreedomJumpRecoveryReading&,
                         const FreedomJumpRecoveryReading&) -> bool = default;
};
struct FrozenFreedomJumpUncertainty {
  std::uint32_t version{kFreedomDriveRecoveryVersion};
  std::uint64_t source_checksum{};
  Seed sample_seed;
  SimulationTick remaining_ticks{};
  std::uint32_t extra_radius_millimetres{};
  RigidVector3 offset_metres;
  friend auto operator==(const FrozenFreedomJumpUncertainty&,
                         const FrozenFreedomJumpUncertainty&) -> bool = default;
};
enum class FreedomJumpRecoveryError : std::uint8_t {
  unsupported_policy,
  invalid_owner,
  invalid_state,
  invalid_stamp,
  invalid_clock,
  invalid_attempt,
  invalid_target,
  pending_arrival
};
// Shape/clock validation permits a last arrival up to240 ticks in the future
// only after its commit. The outer travel owner must authenticate that receipt.
[[nodiscard]] auto validate_freedom_drive_recovery(
    const FreedomDriveRecoveryState&, SimulationTick)
    -> std::expected<void, FreedomJumpRecoveryError>;
[[nodiscard]] auto validate_freedom_drive_recovery(
    const FreedomDriveRecoveryState&, Seed, StarterCraftId, CraftFrameRecipe,
    const std::optional<FreedomCraftLineage>&, SimulationTick)
    -> std::expected<void, FreedomJumpRecoveryError>;
[[nodiscard]] auto freedom_jump_recovery_reading(
    const FreedomDriveRecoveryState&, SimulationTick)
    -> std::expected<FreedomJumpRecoveryReading, FreedomJumpRecoveryError>;
[[nodiscard]] auto make_freedom_jump_recovery_stamp(
    FreedomJumpRecoveryKind, std::uint64_t attempt,
    std::uint64_t source_checksum, SimulationTick commit_tick)
    -> std::expected<FreedomJumpRecoveryStamp, FreedomJumpRecoveryError>;
// The family authenticates actual typed geometry; no caller-owned position or
// replacement universe is supplied. All inputs are retained in its request.
[[nodiscard]] auto freeze_freedom_jump_uncertainty(
    Seed, StarterCraftId, CraftFrameRecipe,
    const std::optional<FreedomCraftLineage>&, std::uint64_t source_checksum,
    FreedomJumpRecoveryKind, SimulationTick commit_tick, BodyTarget source,
    BodyTarget destination, std::uint64_t attempt,
    const FreedomDriveRecoveryState&)
    -> std::expected<FrozenFreedomJumpUncertainty, FreedomJumpRecoveryError>;
} // namespace apsis_drift
