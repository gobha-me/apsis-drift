#pragma once

#include "apsis_drift/body_ephemeris.hpp"
#include "apsis_drift/freedom_save.hpp"
#include "apsis_drift/rigid_body.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomDriveRecoveryVersion{1};
inline constexpr std::uint32_t kFreedomStrainedDriveRecoveryVersion{2};
inline constexpr SimulationTick kFreedomDriveRecoveryTicks{5400};
inline constexpr SimulationTick kFreedomDriveMaximumRecoveryTicks{10800};
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
  SimulationTick recovery_ticks{kFreedomDriveRecoveryTicks};
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
  SimulationTick cycle_ticks{kFreedomDriveRecoveryTicks};
  SimulationTick next_recovery_ticks{kFreedomDriveRecoveryTicks};
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
// Commit from the actual prior state. Version1 retains its fixed45s policy;
// version2 carries remaining arrival-based debt, capped at90s. No debit or
// mutable clock is owned here. Pending prior transit cannot commit another use.
[[nodiscard]] auto advance_freedom_drive_recovery(
    const FreedomDriveRecoveryState&, FreedomJumpRecoveryKind,
    std::uint64_t attempt, std::uint64_t source_checksum,
    SimulationTick commit_tick)
    -> std::expected<FreedomDriveRecoveryState, FreedomJumpRecoveryError>;
// The family authenticates actual typed geometry; no caller-owned position or
// replacement universe is supplied. All inputs are retained in its request.
[[nodiscard]] auto freeze_freedom_jump_uncertainty(
    Seed, StarterCraftId, CraftFrameRecipe,
    const std::optional<FreedomCraftLineage>&, std::uint64_t source_checksum,
    FreedomJumpRecoveryKind, SimulationTick commit_tick, BodyTarget source,
    BodyTarget destination, std::uint64_t attempt,
    const FreedomDriveRecoveryState&)
    -> std::expected<FrozenFreedomJumpUncertainty, FreedomJumpRecoveryError>;
// Policy6's owning family authenticates the actual foreign parent/Moon and
// charted Origin. This entrypoint alone admits planet->Moon within one system
// or Moon->planet across systems; historical local/system target rules stay
// unchanged. It retains the Moon stamp kind and actual domain20 target words.
[[nodiscard]] auto freeze_freedom_foreign_moon_uncertainty(
    Seed, StarterCraftId, CraftFrameRecipe,
    const std::optional<FreedomCraftLineage>&, std::uint64_t source_checksum,
    SimulationTick commit_tick, BodyTarget source, BodyTarget destination,
    std::uint64_t attempt, const FreedomDriveRecoveryState&)
    -> std::expected<FrozenFreedomJumpUncertainty, FreedomJumpRecoveryError>;
// Policy8's owning family authenticates the star-parented minor and actual
// entry planet/Origin. Retains the minor stamp kind and domain20 words; only
// planet->minor within one system or minor->planet across systems is admitted.
[[nodiscard]] auto freeze_freedom_foreign_minor_uncertainty(
    Seed, StarterCraftId, CraftFrameRecipe,
    const std::optional<FreedomCraftLineage>&, std::uint64_t source_checksum,
    SimulationTick commit_tick, BodyTarget source, BodyTarget destination,
    std::uint64_t attempt, const FreedomDriveRecoveryState&)
    -> std::expected<FrozenFreedomJumpUncertainty, FreedomJumpRecoveryError>;
} // namespace apsis_drift
