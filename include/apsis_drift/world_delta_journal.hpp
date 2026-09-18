#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/save_schema.hpp"
#include "apsis_drift/surface_signals.hpp"

namespace apsis_drift {

enum class WorldDeltaJournalError : std::uint8_t {
  invalid_object_key,
  invalid_delta_kind,
  journal_capacity_exceeded,
  unknown_object_key,
};

class WorldDeltaJournal {
 public:
  [[nodiscard]] static auto create(std::span<const SaveWorldDelta> deltas = {})
      -> std::expected<WorldDeltaJournal, WorldDeltaJournalError>;

  // The greatest tick wins for one object key. An equal-tick update wins by
  // source order; a stale update is accepted as an idempotent no-op.
  [[nodiscard]] auto record(SaveWorldDelta delta)
      -> std::expected<void, WorldDeltaJournalError>;

  [[nodiscard]] auto state(std::string_view object_key) const noexcept
      -> const SaveWorldDelta*;
  [[nodiscard]] auto entries() const noexcept
      -> std::span<const SaveWorldDelta> {
    return m_entries;
  }

 private:
  std::vector<SaveWorldDelta> m_entries;
};

struct SurfaceSignalWorldEntry {
  SurfaceSignal generated;
  std::optional<SaveWorldDelta> delta;
  bool active{true};

  friend auto operator==(const SurfaceSignalWorldEntry&,
                         const SurfaceSignalWorldEntry&) -> bool = default;
};

struct SurfaceSignalWorldProjection {
  PlanetId planet;
  std::array<SurfaceSignalWorldEntry, kSurfaceSignalCount> signals;

  friend auto operator==(const SurfaceSignalWorldProjection&,
                         const SurfaceSignalWorldProjection&) -> bool = default;
};

[[nodiscard]] auto surface_signal_object_key(SurfaceSignalId id) -> std::string;
[[nodiscard]] auto parse_surface_signal_object_key(
    std::string_view key) noexcept
    -> std::expected<SurfaceSignalId, WorldDeltaJournalError>;

// Applies mutable state after deterministic regeneration without modifying the
// generated catalog. Unknown keys fail before a partial projection is exposed.
[[nodiscard]] auto apply_world_delta_journal(
    const SurfaceSignalCatalog& catalog, const WorldDeltaJournal& journal)
    -> std::expected<SurfaceSignalWorldProjection, WorldDeltaJournalError>;

} // namespace apsis_drift
