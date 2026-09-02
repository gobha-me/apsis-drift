#include "apsis_drift/signal_collection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace apsis_drift {
namespace {

[[nodiscard]] auto valid_scanner_status(SignalScannerStatus status) noexcept
    -> bool {
  switch (status) {
    case SignalScannerStatus::no_signal:
    case SignalScannerStatus::tracking:
    case SignalScannerStatus::out_of_range:
    case SignalScannerStatus::occluded:
    case SignalScannerStatus::reached: return true;
  }
  return false;
}

[[nodiscard]] auto valid_collection_status(
    SignalCollectionStatus status) noexcept -> bool {
  switch (status) {
    case SignalCollectionStatus::approach:
    case SignalCollectionStatus::in_range:
    case SignalCollectionStatus::scanning:
    case SignalCollectionStatus::complete:
    case SignalCollectionStatus::aborted: return true;
  }
  return false;
}

[[nodiscard]] auto terminal_delta(SaveWorldDeltaKind kind) noexcept -> bool {
  switch (kind) {
    case SaveWorldDeltaKind::discovered: return false;
    case SaveWorldDeltaKind::collected:
    case SaveWorldDeltaKind::completed:
    case SaveWorldDeltaKind::removed: return true;
  }
  return false;
}

[[nodiscard]] auto signal_in_catalog(const SurfaceSignalCatalog& catalog,
                                     SurfaceSignalId id) noexcept -> bool {
  return std::ranges::find(catalog.signals, id, &SurfaceSignal::id) !=
         catalog.signals.end();
}

[[nodiscard]] auto valid_state(const SignalCollectionState& state) noexcept
    -> bool {
  if (!valid_collection_status(state.status)) return false;
  switch (state.status) {
    case SignalCollectionStatus::approach:
    case SignalCollectionStatus::aborted:
      return state.consecutive_in_range_ticks == 0 && !state.completion_tick;
    case SignalCollectionStatus::in_range:
      return state.target && state.consecutive_in_range_ticks > 0 &&
             state.consecutive_in_range_ticks <=
                 kSignalCollectionAcquireTicks &&
             !state.completion_tick;
    case SignalCollectionStatus::scanning:
      return state.target &&
             state.consecutive_in_range_ticks > kSignalCollectionAcquireTicks &&
             state.consecutive_in_range_ticks <
                 kSignalCollectionTotalInRangeTicks &&
             !state.completion_tick;
    case SignalCollectionStatus::complete:
      return state.target &&
             state.consecutive_in_range_ticks >=
                 kSignalCollectionTotalInRangeTicks &&
             state.completion_tick;
  }
  return false;
}

[[nodiscard]] auto valid_navigation(
    const SurfaceSignalCatalog& catalog,
    const SignalNavigationSolution& navigation) noexcept -> bool {
  if (!valid_scanner_status(navigation.status)) return false;
  if (navigation.status == SignalScannerStatus::no_signal) {
    return !navigation.selected;
  }
  return navigation.selected &&
         signal_in_catalog(catalog, *navigation.selected) &&
         navigation.ordinal < catalog.signals.size() &&
         catalog.signals[navigation.ordinal].id == *navigation.selected &&
         std::isfinite(navigation.distance_metres) &&
         navigation.distance_metres >= 0.0;
}

auto abort(SignalCollectionState& state, std::optional<SurfaceSignalId> target)
    -> void {
  state.status = SignalCollectionStatus::aborted;
  state.target = target;
  state.consecutive_in_range_ticks = 0;
  state.completion_tick.reset();
}

auto approach(SignalCollectionState& state,
              std::optional<SurfaceSignalId> target) -> void {
  state.status = SignalCollectionStatus::approach;
  state.target = target;
  state.consecutive_in_range_ticks = 0;
  state.completion_tick.reset();
}

} // namespace

auto advance_signal_collection(const SurfaceSignalCatalog& catalog,
                               const SignalNavigationSolution& navigation,
                               SimulationTick tick, WorldDeltaJournal& journal,
                               SignalCollectionState& state)
    -> std::expected<SignalCollectionUpdate, SignalCollectionError> {
  if (!valid_state(state)) {
    return std::unexpected{SignalCollectionError::invalid_state};
  }
  if (!valid_navigation(catalog, navigation)) {
    return std::unexpected{
        navigation.selected && !signal_in_catalog(catalog, *navigation.selected)
            ? SignalCollectionError::invalid_catalog
            : SignalCollectionError::invalid_navigation};
  }
  if (state.last_tick) {
    if (*state.last_tick == std::numeric_limits<SimulationTick>::max()) {
      return std::unexpected{SignalCollectionError::tick_overflow};
    }
    if (tick != *state.last_tick + 1U) {
      return std::unexpected{SignalCollectionError::wrong_tick};
    }
  }

  SignalCollectionState next = state;
  next.last_tick = tick;
  const auto selected = navigation.selected;

  if (!selected) {
    if (next.target && next.status != SignalCollectionStatus::complete) {
      abort(next, std::nullopt);
    } else {
      approach(next, std::nullopt);
    }
    state = next;
    return SignalCollectionUpdate{};
  }

  const auto key = surface_signal_object_key(*selected);
  if (const auto* existing = journal.state(key);
      existing && terminal_delta(existing->kind)) {
    next.status = SignalCollectionStatus::complete;
    next.target = selected;
    next.consecutive_in_range_ticks = kSignalCollectionTotalInRangeTicks;
    next.completion_tick = existing->tick;
    state = next;
    return SignalCollectionUpdate{};
  }

  if (next.target && next.target != selected) {
    abort(next, selected);
    state = next;
    return SignalCollectionUpdate{};
  }
  if (!next.target) next.target = selected;

  if (navigation.status != SignalScannerStatus::reached) {
    if (next.status == SignalCollectionStatus::in_range ||
        next.status == SignalCollectionStatus::scanning) {
      abort(next, selected);
    } else {
      approach(next, selected);
    }
    state = next;
    return SignalCollectionUpdate{};
  }

  if (next.status == SignalCollectionStatus::complete) {
    state = next;
    return SignalCollectionUpdate{};
  }
  if (next.status == SignalCollectionStatus::aborted) {
    approach(next, selected);
  }

  ++next.consecutive_in_range_ticks;
  if (next.consecutive_in_range_ticks <= kSignalCollectionAcquireTicks) {
    next.status = SignalCollectionStatus::in_range;
    state = next;
    return SignalCollectionUpdate{};
  }
  if (next.consecutive_in_range_ticks < kSignalCollectionTotalInRangeTicks) {
    next.status = SignalCollectionStatus::scanning;
    state = next;
    return SignalCollectionUpdate{};
  }

  WorldDeltaJournal next_journal = journal;
  const auto recorded =
      next_journal.record({key, SaveWorldDeltaKind::collected, tick});
  if (!recorded) {
    return std::unexpected{SignalCollectionError::journal_failure};
  }
  const auto* persisted = next_journal.state(key);
  if (persisted == nullptr || !terminal_delta(persisted->kind)) {
    return std::unexpected{SignalCollectionError::journal_failure};
  }
  next.status = SignalCollectionStatus::complete;
  next.consecutive_in_range_ticks = kSignalCollectionTotalInRangeTicks;
  next.completion_tick = persisted->tick;
  journal = std::move(next_journal);
  state = next;
  return SignalCollectionUpdate{.delta_emitted = true};
}

} // namespace apsis_drift
