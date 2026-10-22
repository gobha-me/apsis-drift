#include "apsis_drift/freedom_jump_recovery.hpp"

#include "apsis_drift/intersystem_jump.hpp"

#include <algorithm>
#include <limits>

namespace apsis_drift {
namespace {
using Error = FreedomJumpRecoveryError;
auto valid_kind(FreedomJumpRecoveryKind kind) -> bool {
  return kind >= FreedomJumpRecoveryKind::system &&
         kind <= FreedomJumpRecoveryKind::minor;
}
auto strained_stamp(FreedomJumpRecoveryKind kind, std::uint64_t attempt,
                    std::uint64_t source_checksum, SimulationTick commit_tick,
                    SimulationTick duration)
    -> std::expected<FreedomJumpRecoveryStamp, Error> {
  if (!valid_kind(kind)) return std::unexpected{Error::invalid_stamp};
  if (!attempt || attempt == std::numeric_limits<std::uint64_t>::max())
    return std::unexpected{Error::invalid_attempt};
  if (duration < kFreedomDriveRecoveryTicks ||
      duration > kFreedomDriveMaximumRecoveryTicks ||
      (attempt == 1 && duration != kFreedomDriveRecoveryTicks))
    return std::unexpected{Error::invalid_stamp};
  if (commit_tick > std::numeric_limits<SimulationTick>::max() -
                        kJumpTransitTicks - duration - 3)
    return std::unexpected{Error::invalid_clock};
  return FreedomJumpRecoveryStamp{kind,
                                  attempt,
                                  source_checksum,
                                  commit_tick,
                                  commit_tick + kJumpTransitTicks,
                                  duration};
}
auto hash_word(std::uint64_t& value, std::uint64_t word) -> void {
  for (unsigned byte = 0; byte < 8; ++byte) {
    value ^= (word >> (byte * 8)) & 255U;
    value *= 1099511628211ULL;
  }
}
auto valid_target(BodyTarget target) -> bool {
  return target.system.value && target.body.value &&
         target.body.version == kBodyIdentityVersion &&
         target.body.kind >= BodyKind::planet &&
         target.body.kind <= BodyKind::minor;
}
auto target_word(std::uint64_t& value, BodyTarget target) -> void {
  hash_word(value, target.system.value);
  hash_word(value, target.body.version);
  hash_word(value, static_cast<unsigned>(target.body.kind));
  hash_word(value, target.body.value);
}
} // namespace

auto make_freedom_jump_recovery_stamp(FreedomJumpRecoveryKind kind,
                                      std::uint64_t attempt,
                                      std::uint64_t source_checksum,
                                      SimulationTick commit_tick)
    -> std::expected<FreedomJumpRecoveryStamp, FreedomJumpRecoveryError> {
  if (!valid_kind(kind)) return std::unexpected{Error::invalid_stamp};
  if (!attempt || attempt == std::numeric_limits<std::uint64_t>::max())
    return std::unexpected{Error::invalid_attempt};
  if (commit_tick > std::numeric_limits<SimulationTick>::max() -
                        kJumpTransitTicks - kFreedomDriveRecoveryTicks - 3)
    return std::unexpected{Error::invalid_clock};
  return FreedomJumpRecoveryStamp{kind, attempt, source_checksum, commit_tick,
                                  commit_tick + kJumpTransitTicks};
}
auto validate_freedom_drive_recovery(const FreedomDriveRecoveryState& state,
                                     SimulationTick now)
    -> std::expected<void, FreedomJumpRecoveryError> {
  if (state.version != kFreedomDriveRecoveryVersion &&
      state.version != kFreedomStrainedDriveRecoveryVersion)
    return std::unexpected{Error::unsupported_policy};
  if (now == std::numeric_limits<SimulationTick>::max())
    return std::unexpected{Error::invalid_clock};
  if (state.lineage && (state.lineage->version != kFreedomCraftLineageVersion ||
                        !state.lineage->generation))
    return std::unexpected{Error::invalid_owner};
  if (!state.last_use) return {};
  const auto& stamp = *state.last_use;
  const auto expected =
      state.version == kFreedomDriveRecoveryVersion
          ? make_freedom_jump_recovery_stamp(stamp.kind, stamp.attempt,
                                             stamp.source_checksum,
                                             stamp.commit_tick)
          : strained_stamp(stamp.kind, stamp.attempt, stamp.source_checksum,
                           stamp.commit_tick, stamp.recovery_ticks);
  if (!expected || *expected != stamp)
    return std::unexpected{Error::invalid_stamp};
  if (stamp.commit_tick > now || (stamp.arrival_tick > now &&
                                  stamp.arrival_tick - now > kJumpTransitTicks))
    return std::unexpected{Error::invalid_clock};
  return {};
}
auto validate_freedom_drive_recovery(
    const FreedomDriveRecoveryState& state, Seed seed, StarterCraftId craft,
    CraftFrameRecipe frame, const std::optional<FreedomCraftLineage>& lineage,
    SimulationTick now) -> std::expected<void, FreedomJumpRecoveryError> {
  if (const auto valid = validate_freedom_drive_recovery(state, now); !valid)
    return valid;
  const auto identity =
      derive_freedom_craft_identity(seed, lineage ? lineage->generation : 0);
  if (state.lineage != lineage || !identity || craft != *identity ||
      frame != CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion})
    return std::unexpected{Error::invalid_owner};
  return {};
}
auto freedom_jump_recovery_reading(const FreedomDriveRecoveryState& state,
                                   SimulationTick now)
    -> std::expected<FreedomJumpRecoveryReading, FreedomJumpRecoveryError> {
  if (const auto valid = validate_freedom_drive_recovery(state, now); !valid)
    return std::unexpected{valid.error()};
  if (!state.last_use) return FreedomJumpRecoveryReading{0, 0, true, false};
  const auto arrival = state.last_use->arrival_tick;
  const bool pending = now < arrival;
  const auto elapsed = pending ? SimulationTick{} : now - arrival;
  const auto n = state.last_use->recovery_ticks;
  const auto remaining = elapsed >= n ? 0 : n - elapsed;
  const auto denominator = n * n * n;
  // Even the loose 2,000,000 * 3 * 10800^3 bound is <2^63; adding
  // denominator-1 cannot overflow. Version1 retains the exact integer cubic.
  const auto numerator =
      static_cast<std::uint64_t>(kFreedomJumpRecoveryMaximumRadiusMillimetres) *
      remaining * remaining * (3 * n - 2 * remaining);
  const auto radius =
      static_cast<std::uint32_t>((numerator + denominator - 1) / denominator);
  const auto next = state.version == kFreedomStrainedDriveRecoveryVersion
                        ? std::min(kFreedomDriveMaximumRecoveryTicks,
                                   kFreedomDriveRecoveryTicks + remaining)
                        : kFreedomDriveRecoveryTicks;
  return FreedomJumpRecoveryReading{remaining, radius, remaining == 0,
                                    pending,   n,      next};
}
auto advance_freedom_drive_recovery(const FreedomDriveRecoveryState& prior,
                                    FreedomJumpRecoveryKind kind,
                                    std::uint64_t attempt,
                                    std::uint64_t source_checksum,
                                    SimulationTick commit_tick)
    -> std::expected<FreedomDriveRecoveryState, FreedomJumpRecoveryError> {
  if (const auto valid = validate_freedom_drive_recovery(prior, commit_tick);
      !valid)
    return std::unexpected{valid.error()};
  if ((!prior.last_use && attempt != 1) ||
      (prior.last_use && attempt != prior.last_use->attempt + 1))
    return std::unexpected{Error::invalid_attempt};
  const auto reading = freedom_jump_recovery_reading(prior, commit_tick);
  if (!reading) return std::unexpected{reading.error()};
  if (reading->pending_arrival) return std::unexpected{Error::pending_arrival};
  auto stamp = prior.version == kFreedomDriveRecoveryVersion
                   ? make_freedom_jump_recovery_stamp(
                         kind, attempt, source_checksum, commit_tick)
                   : strained_stamp(kind, attempt, source_checksum, commit_tick,
                                    reading->next_recovery_ticks);
  if (!stamp) return std::unexpected{stamp.error()};
  auto next = prior;
  next.last_use = *stamp;
  return next;
}
namespace {
auto freeze_jump_uncertainty(Seed seed, StarterCraftId craft,
                             CraftFrameRecipe frame,
                             const std::optional<FreedomCraftLineage>& lineage,
                             std::uint64_t source_checksum,
                             FreedomJumpRecoveryKind kind,
                             SimulationTick commit_tick, BodyTarget source,
                             BodyTarget destination, std::uint64_t attempt,
                             const FreedomDriveRecoveryState& state,
                             bool foreign_moon)
    -> std::expected<FrozenFreedomJumpUncertainty, FreedomJumpRecoveryError> {
  if (const auto valid = validate_freedom_drive_recovery(
          state, seed, craft, frame, lineage, commit_tick);
      !valid)
    return std::unexpected{valid.error()};
  if (state.version == kFreedomDriveRecoveryVersion) {
    // Retain the historical refusal order as well as the old sampling words.
    if (const auto stamp = make_freedom_jump_recovery_stamp(
            kind, attempt, source_checksum, commit_tick);
        !stamp)
      return std::unexpected{stamp.error()};
  }
  const auto advanced = advance_freedom_drive_recovery(
      state, kind, attempt, source_checksum, commit_tick);
  if (!advanced) return std::unexpected{advanced.error()};
  const auto reading = freedom_jump_recovery_reading(state, commit_tick);
  if (!reading) return std::unexpected{reading.error()};
  if (reading->pending_arrival) return std::unexpected{Error::pending_arrival};
  if (!valid_target(source) || !valid_target(destination) ||
      source == destination)
    return std::unexpected{Error::invalid_target};
  const bool planets = source.body.kind == BodyKind::planet &&
                       destination.body.kind == BodyKind::planet;
  const bool same_system = source.system == destination.system;
  const bool moon = (source.body.kind == BodyKind::planet &&
                     destination.body.kind == BodyKind::moon) ||
                    (source.body.kind == BodyKind::moon &&
                     destination.body.kind == BodyKind::planet);
  const bool minor = (source.body.kind == BodyKind::planet &&
                      destination.body.kind == BodyKind::minor) ||
                     (source.body.kind == BodyKind::minor &&
                      destination.body.kind == BodyKind::planet);
  if (foreign_moon) {
    if (!((same_system && source.body.kind == BodyKind::planet &&
           destination.body.kind == BodyKind::moon) ||
          (!same_system && source.body.kind == BodyKind::moon &&
           destination.body.kind == BodyKind::planet)))
      return std::unexpected{Error::invalid_target};
  } else if ((kind == FreedomJumpRecoveryKind::system &&
              (!planets || same_system)) ||
             (kind == FreedomJumpRecoveryKind::planet &&
              (!planets || !same_system)) ||
             (kind == FreedomJumpRecoveryKind::moon &&
              (!moon || !same_system)) ||
             (kind == FreedomJumpRecoveryKind::minor &&
              (!minor || !same_system)))
    return std::unexpected{Error::invalid_target};
  std::uint64_t ordinal{14695981039346656037ULL};
  for (auto word :
       {std::uint64_t{state.version},
        std::uint64_t{static_cast<unsigned>(kind)}, craft.value, frame.id.value,
        std::uint64_t{frame.version}, source_checksum, commit_tick, attempt})
    hash_word(ordinal, word);
  hash_word(ordinal, lineage.has_value());
  if (lineage) {
    hash_word(ordinal, lineage->version);
    hash_word(ordinal, lineage->generation);
  }
  target_word(ordinal, source);
  target_word(ordinal, destination);
  hash_word(ordinal, state.last_use.has_value());
  if (state.last_use) {
    const auto& stamp = *state.last_use;
    for (auto word :
         {std::uint64_t{static_cast<unsigned>(stamp.kind)}, stamp.attempt,
          stamp.source_checksum, stamp.commit_tick, stamp.arrival_tick})
      hash_word(ordinal, word);
  }
  if (state.version == kFreedomStrainedDriveRecoveryVersion) {
    // Do not add words to the historical version1 stream. The new sampler
    // authenticates both the cooling cycle and the next commitment duration.
    hash_word(ordinal, reading->cycle_ticks);
    hash_word(ordinal, advanced->last_use->recovery_ticks);
  }
  const auto sample =
      derive_seed(seed, SeedDomain::jump_recovery_uncertainty, ordinal);
  const double scale = static_cast<double>(reading->extra_radius_millimetres) /
                       (1000.0 * 1.75 * 32768.0);
  const auto component = [&](unsigned shift) {
    return (static_cast<std::int32_t>((sample.value >> shift) & 65535U) -
            32768) *
           scale;
  };
  const RigidVector3 offset =
      reading->extra_radius_millimetres
          ? RigidVector3{component(0), component(16), component(32)}
          : RigidVector3{};
  return FrozenFreedomJumpUncertainty{state.version,
                                      source_checksum,
                                      sample,
                                      reading->remaining_ticks,
                                      reading->extra_radius_millimetres,
                                      offset};
}
} // namespace

auto freeze_freedom_jump_uncertainty(
    Seed seed, StarterCraftId craft, CraftFrameRecipe frame,
    const std::optional<FreedomCraftLineage>& lineage,
    std::uint64_t source_checksum, FreedomJumpRecoveryKind kind,
    SimulationTick commit_tick, BodyTarget source, BodyTarget destination,
    std::uint64_t attempt, const FreedomDriveRecoveryState& state)
    -> std::expected<FrozenFreedomJumpUncertainty, FreedomJumpRecoveryError> {
  return freeze_jump_uncertainty(seed, craft, frame, lineage, source_checksum,
                                 kind, commit_tick, source, destination,
                                 attempt, state, false);
}
auto freeze_freedom_foreign_moon_uncertainty(
    Seed seed, StarterCraftId craft, CraftFrameRecipe frame,
    const std::optional<FreedomCraftLineage>& lineage,
    std::uint64_t source_checksum, SimulationTick commit_tick,
    BodyTarget source, BodyTarget destination, std::uint64_t attempt,
    const FreedomDriveRecoveryState& state)
    -> std::expected<FrozenFreedomJumpUncertainty, FreedomJumpRecoveryError> {
  return freeze_jump_uncertainty(seed, craft, frame, lineage, source_checksum,
                                 FreedomJumpRecoveryKind::moon, commit_tick,
                                 source, destination, attempt, state, true);
}
} // namespace apsis_drift
