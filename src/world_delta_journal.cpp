#include "apsis_drift/world_delta_journal.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <string_view>
#include <utility>

namespace apsis_drift {
namespace {

inline constexpr std::string_view kSurfaceSignalObjectPrefix{"signal-"};

[[nodiscard]] auto valid_kind(SaveWorldDeltaKind kind) noexcept -> bool {
  switch (kind) {
    case SaveWorldDeltaKind::discovered:
    case SaveWorldDeltaKind::collected:
    case SaveWorldDeltaKind::completed:
    case SaveWorldDeltaKind::removed:
      return true;
  }
  return false;
}

[[nodiscard]] auto active_kind(SaveWorldDeltaKind kind) noexcept -> bool {
  return kind == SaveWorldDeltaKind::discovered;
}

auto canonicalize(std::vector<SaveWorldDelta>& entries) -> void {
  std::ranges::sort(entries, [](const SaveWorldDelta& left,
                               const SaveWorldDelta& right) {
    if (left.tick != right.tick) return left.tick < right.tick;
    return left.object_key < right.object_key;
  });
}

}  // namespace

auto WorldDeltaJournal::create(std::span<const SaveWorldDelta> deltas)
    -> std::expected<WorldDeltaJournal, WorldDeltaJournalError> {
  if (deltas.size() > kMaximumSaveWorldDeltas) {
    return std::unexpected{WorldDeltaJournalError::journal_capacity_exceeded};
  }
  WorldDeltaJournal journal;
  for (const auto& delta : deltas) {
    if (auto recorded = journal.record(delta); !recorded) {
      return std::unexpected{recorded.error()};
    }
  }
  return journal;
}

auto WorldDeltaJournal::record(SaveWorldDelta delta)
    -> std::expected<void, WorldDeltaJournalError> {
  if (!parse_surface_signal_object_key(delta.object_key)) {
    return std::unexpected{WorldDeltaJournalError::invalid_object_key};
  }
  if (!valid_kind(delta.kind)) {
    return std::unexpected{WorldDeltaJournalError::invalid_delta_kind};
  }

  const auto existing = std::ranges::find(m_entries, delta.object_key,
                                          &SaveWorldDelta::object_key);
  if (existing == m_entries.end()) {
    if (m_entries.size() >= kMaximumSaveWorldDeltas) {
      return std::unexpected{WorldDeltaJournalError::journal_capacity_exceeded};
    }
    m_entries.push_back(std::move(delta));
    canonicalize(m_entries);
    return {};
  }
  if (delta.tick < existing->tick) return {};
  *existing = std::move(delta);
  canonicalize(m_entries);
  return {};
}

auto WorldDeltaJournal::state(std::string_view object_key) const noexcept
    -> const SaveWorldDelta* {
  const auto found =
      std::ranges::find(m_entries, object_key, &SaveWorldDelta::object_key);
  return found == m_entries.end() ? nullptr : &*found;
}

auto surface_signal_object_key(SurfaceSignalId id) -> std::string {
  return surface_signal_id_string(id);
}

auto parse_surface_signal_object_key(std::string_view key) noexcept
    -> std::expected<SurfaceSignalId, WorldDeltaJournalError> {
  constexpr std::size_t encoded_hex_bytes{16};
  if (!key.starts_with(kSurfaceSignalObjectPrefix) ||
      key.size() != kSurfaceSignalObjectPrefix.size() + encoded_hex_bytes) {
    return std::unexpected{WorldDeltaJournalError::invalid_object_key};
  }
  const auto hexadecimal = key.substr(kSurfaceSignalObjectPrefix.size());
  for (const char value : hexadecimal) {
    const bool digit = value >= '0' && value <= '9';
    const bool lower_hex = value >= 'a' && value <= 'f';
    if (!digit && !lower_hex) {
      return std::unexpected{WorldDeltaJournalError::invalid_object_key};
    }
  }
  std::uint64_t value{};
  const auto [end, error] = std::from_chars(
      hexadecimal.data(), hexadecimal.data() + hexadecimal.size(), value, 16);
  if (error != std::errc{} || end != hexadecimal.data() + hexadecimal.size()) {
    return std::unexpected{WorldDeltaJournalError::invalid_object_key};
  }
  return SurfaceSignalId{value};
}

auto apply_world_delta_journal(const SurfaceSignalCatalog& catalog,
                               const WorldDeltaJournal& journal)
    -> std::expected<SurfaceSignalWorldProjection, WorldDeltaJournalError> {
  SurfaceSignalWorldProjection projection{.planet = catalog.planet,
                                          .signals = {}};
  for (std::size_t index = 0; index < catalog.signals.size(); ++index) {
    projection.signals[index] =
        SurfaceSignalWorldEntry{.generated = catalog.signals[index],
                                .delta = std::nullopt,
                                .active = true};
  }

  for (const auto& delta : journal.entries()) {
    const auto parsed = parse_surface_signal_object_key(delta.object_key);
    if (!parsed) return std::unexpected{parsed.error()};
    const auto signal = std::ranges::find(
        projection.signals, *parsed, [](const SurfaceSignalWorldEntry& entry) {
          return entry.generated.id;
        });
    if (signal == projection.signals.end()) {
      return std::unexpected{WorldDeltaJournalError::unknown_object_key};
    }
    signal->delta = delta;
    signal->active = active_kind(delta.kind);
  }
  return projection;
}

}  // namespace apsis_drift
