#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <span>

#include "apsis_drift/intersystem_contract.hpp"
#include "apsis_drift/planetary_flight.hpp"
#include "apsis_drift/signal_collection.hpp"
#include "apsis_drift/terrain_tiles.hpp"

namespace apsis_drift {

struct IntersystemPlanetfallState {
  std::shared_ptr<const PlanetDescriptor> planet;
  SurfaceSignalCatalog catalog;
  PlanetaryFlightState flight;
  SignalScannerState scanner;
  SignalNavigationSolution navigation;
  WorldDeltaJournal journal;
  SignalCollectionState collection;
};

struct IntersystemPlanetfallUpdate {
  bool objective_completed{};

  friend auto operator==(const IntersystemPlanetfallUpdate&,
                         const IntersystemPlanetfallUpdate&) -> bool = default;
};

enum class IntersystemPlanetfallError : std::uint8_t {
  invalid_planet,
  invalid_target,
  invalid_state,
  terrain_failure,
  flight_failure,
  navigation_failure,
  collection_failure,
  journal_failure,
};

[[nodiscard]] auto initialize_intersystem_planetfall(
    const PlanetDescriptor& planet, SurfaceSignalId target,
    const PlanetaryFlightState& flight,
    std::span<const SaveWorldDelta> world_deltas, TerrainTileCache& cache)
    -> std::expected<IntersystemPlanetfallState, IntersystemPlanetfallError>;

[[nodiscard]] auto validate_intersystem_planetfall_state(
    const IntersystemPlanetfallState& state, SurfaceSignalId target) noexcept
    -> std::expected<void, IntersystemPlanetfallError>;

// Advances exactly one authoritative planetary tick. Terrain sampling,
// flight, navigation, collection, and the journal commit together.
[[nodiscard]] auto advance_intersystem_planetfall(
    IntersystemPlanetfallState& state, TerrainTileCache& cache,
    std::span<const FlightCommand> commands, IntersystemRuleProfile profile)
    -> std::expected<IntersystemPlanetfallUpdate, IntersystemPlanetfallError>;

} // namespace apsis_drift
