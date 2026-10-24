#pragma once

#include "apsis_drift/freedom_derelict.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomDerelictRecoveryVersion{1};
inline constexpr std::uint32_t kFreedomDerelictRetrievalTicks{3600};
inline constexpr double kFreedomDerelictRetrievalMinimumMetres{100.0};
inline constexpr double kFreedomDerelictRetrievalMaximumMetres{150.0};
inline constexpr double kFreedomDerelictRetrievalMaximumSpeedMetresPerSecond{
    2.0};
inline constexpr std::uint64_t kFreedomDerelictCanisterCredits{100};

struct FreedomDerelictRetrieval {
  SimulationTick started_tick{}, last_tick{};
  std::uint32_t acquired_ticks{};
  FreedomDerelictObservation observer;
  friend auto operator==(const FreedomDerelictRetrieval&,
                         const FreedomDerelictRetrieval&) -> bool = default;
};
enum class FreedomCanisterDisposition : std::uint8_t { carried, lost, sold };
struct FreedomRecoveredCanister {
  SimulationTick started_tick{};
  FreedomDerelictObservation completed;
  FreedomCanisterDisposition disposition{FreedomCanisterDisposition::carried};
  std::optional<SimulationTick> sold_tick{};
  friend auto operator==(const FreedomRecoveredCanister&,
                         const FreedomRecoveredCanister&) -> bool = default;
};
// Presence selects the bounded policy. One permanent claim depletes the source;
// only a carried disposition represents physical cargo, not inspection notes.
struct FreedomDerelictRecoveryState {
  std::uint32_t version{kFreedomDerelictRecoveryVersion};
  std::optional<FreedomDerelictRetrieval> active{};
  std::optional<FreedomRecoveredCanister> canister{};
  friend auto operator==(const FreedomDerelictRecoveryState&,
                         const FreedomDerelictRecoveryState&) -> bool = default;
};
struct FreedomDerelictRetrievalAssessment {
  bool eligible{};
  double distance_metres{}, relative_speed_metres_per_second{};
  friend auto operator==(const FreedomDerelictRetrievalAssessment&,
                         const FreedomDerelictRetrievalAssessment&)
      -> bool = default;
};

// Historical observations may precede now. The voyage owner additionally
// authenticates lineage, current active craft/body and Origin sale admission.
[[nodiscard]] auto validate_freedom_derelict_recovery(
    Seed, const FreedomDerelictRecoveryState&, const FreedomDerelictState&,
    SimulationTick now) -> std::expected<void, std::string>;
[[nodiscard]] auto assess_freedom_derelict_retrieval(
    Seed, const FreedomDerelictObservation&)
    -> std::expected<FreedomDerelictRetrievalAssessment, std::string>;
[[nodiscard]] auto begin_freedom_derelict_retrieval(
    Seed, const FreedomDerelictRecoveryState&, const FreedomDerelictState&,
    const FreedomDerelictObservation&)
    -> std::expected<FreedomDerelictRecoveryState, std::string>;
// One actual consecutive post-step observation earns one interval. A valid
// forward gap, craft change or envelope exit cancels without backfilling time.
[[nodiscard]] auto advance_freedom_derelict_retrieval(
    Seed, const FreedomDerelictRecoveryState&, const FreedomDerelictState&,
    const FreedomDerelictObservation&)
    -> std::expected<FreedomDerelictRecoveryState, std::string>;
[[nodiscard]] auto cancel_freedom_derelict_retrieval(
    const FreedomDerelictRecoveryState&) noexcept
    -> FreedomDerelictRecoveryState;
[[nodiscard]] auto lose_freedom_derelict_canister(
    const FreedomDerelictRecoveryState&) noexcept
    -> FreedomDerelictRecoveryState;
[[nodiscard]] auto sell_freedom_derelict_canister(
    Seed, const FreedomDerelictRecoveryState&, const FreedomDerelictState&,
    SimulationTick now)
    -> std::expected<FreedomDerelictRecoveryState, std::string>;
} // namespace apsis_drift
