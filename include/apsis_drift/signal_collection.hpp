#pragma once

#include <cstdint>
#include <expected>
#include <optional>

#include "apsis_drift/signal_scanner.hpp"
#include "apsis_drift/world_delta_journal.hpp"

namespace apsis_drift {

inline constexpr SimulationTick kSignalCollectionAcquireTicks{kSimulationHz /
                                                              2};
inline constexpr SimulationTick kSignalCollectionScanTicks{kSimulationHz * 3};
inline constexpr SimulationTick kSignalCollectionTotalInRangeTicks{
    kSignalCollectionAcquireTicks + kSignalCollectionScanTicks};

enum class SignalCollectionStatus : std::uint8_t {
  approach,
  in_range,
  scanning,
  complete,
  aborted,
};

struct SignalCollectionState {
  SignalCollectionStatus status{SignalCollectionStatus::approach};
  std::optional<SurfaceSignalId> target;
  SimulationTick consecutive_in_range_ticks{};
  std::optional<SimulationTick> last_tick;
  std::optional<SimulationTick> completion_tick;

  friend auto operator==(const SignalCollectionState&,
                         const SignalCollectionState&) -> bool = default;
};

struct SignalCollectionUpdate {
  bool delta_emitted{};

  friend auto operator==(const SignalCollectionUpdate&,
                         const SignalCollectionUpdate&) -> bool = default;
};

enum class SignalCollectionError : std::uint8_t {
  invalid_catalog,
  invalid_navigation,
  invalid_state,
  wrong_tick,
  tick_overflow,
  journal_failure,
};

// Advances exactly one application-owned simulation tick. Acquisition and
// scanning require consecutive ticks inside the scanner's reached radius.
// Rejected updates leave both state and journal untouched.
[[nodiscard]] auto advance_signal_collection(
    const SurfaceSignalCatalog& catalog,
    const SignalNavigationSolution& navigation, SimulationTick tick,
    WorldDeltaJournal& journal, SignalCollectionState& state)
    -> std::expected<SignalCollectionUpdate, SignalCollectionError>;

} // namespace apsis_drift
