#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "apsis_drift/render_profile.hpp"
#include "apsis_drift/signal_collection.hpp"

namespace apsis_drift {

inline constexpr std::string_view kSignalNavigationAcceptanceScenario{
    "v0.4-signal-collection"};
inline constexpr std::uint32_t kSignalNavigationAcceptanceSeed{42U};
inline constexpr std::uint32_t kSignalNavigationAcceptanceTargetOrdinal{0U};
inline constexpr double kSignalNavigationAcceptanceStartOffsetMetres{2'000.0};
inline constexpr SimulationTick kSignalNavigationAcceptanceMaximumTicks{20'000};

struct SignalNavigationAcceptanceState {
  SurfaceSignalCatalog catalog;
  PlanetaryFlightState flight;
  SignalScannerState scanner;
  SignalNavigationSolution navigation;
  WorldDeltaJournal journal;
  SignalCollectionState collection;
  std::optional<SimulationTick> reached_tick;
  std::size_t command_count{};
};

enum class SignalNavigationAcceptanceError : std::uint8_t {
  terrain_failure,
  flight_failure,
  scanner_failure,
  journal_failure,
  collection_failure,
  incomplete_path,
};

[[nodiscard]] auto initial_signal_navigation_acceptance(
    const PlanetDescriptor& planet, TerrainTileCache& cache)
    -> std::expected<SignalNavigationAcceptanceState,
                     SignalNavigationAcceptanceError>;

// Advances one fixed simulation tick. The returned boolean becomes true after
// the deterministic acquisition and scan dwell emit a persistent delta.
[[nodiscard]] auto advance_signal_navigation_acceptance(
    const PlanetDescriptor& planet, TerrainTileCache& cache,
    SignalNavigationAcceptanceState& state)
    -> std::expected<bool, SignalNavigationAcceptanceError>;

[[nodiscard]] auto replay_signal_navigation_acceptance()
    -> std::expected<SignalNavigationAcceptanceState,
                     SignalNavigationAcceptanceError>;

struct SignalNavigationAcceptanceReport {
  SurfaceSignalId target_id;
  SimulationTick reached_tick{};
  SimulationTick completion_tick{};
  std::size_t command_count{};
  std::size_t world_delta_count{};
  double final_distance_metres{};
  std::uint64_t flight_checksum{};
  RenderConfiguration render_configuration{};
  std::string_view presentation;
  std::uint64_t framebuffer_checksum{};
};

[[nodiscard]] auto signal_navigation_acceptance_json(
    const SignalNavigationAcceptanceReport& report) -> std::string;

} // namespace apsis_drift
