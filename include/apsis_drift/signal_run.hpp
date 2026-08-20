#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "apsis_drift/save_schema.hpp"
#include "apsis_drift/signal_collection.hpp"

namespace apsis_drift {

inline constexpr double kOriginRendezvousOffsetMetres{40'000.0};
inline constexpr double kOriginRendezvousArrivalRadiusMetres{5'000.0};
inline constexpr double kOriginRendezvousAboveOrbitMetres{50'000.0};

struct OriginRendezvous {
  GeodeticPosition position;
  double arrival_radius_metres{kOriginRendezvousArrivalRadiusMetres};

  friend auto operator==(const OriginRendezvous&, const OriginRendezvous&)
      -> bool = default;
};

struct OriginNavigationSolution {
  double absolute_bearing_radians{};
  double relative_bearing_radians{};
  double distance_metres{};
  TargetRelativeMotion motion;
  bool arrived{};

  friend auto operator==(const OriginNavigationSolution&,
                         const OriginNavigationSolution&) -> bool = default;
};

// Presentation-only observations for contextual cockpit guidance. These are
// deliberately excluded from save projection and all authoritative checksums.
struct SignalRunGuidanceState {
  bool attitude_observed{};
  bool translation_observed{};
  bool thrust_observed{};
  bool coast_observed{};
  bool braking_observed{};
  bool targeting_observed{};
  bool redocking_observed{};

  friend auto operator==(const SignalRunGuidanceState&,
                         const SignalRunGuidanceState&) -> bool = default;
};

struct SignalRunState {
  SaveRecipe recipe;
  OnboardingProgress career_onboarding;
  OriginOnboardingState onboarding;
  std::optional<IntersystemContractState> career;
  std::optional<OriginSystemContractState> origin_system_contract;
  LocalSystemDescriptor origin_system;
  std::shared_ptr<const PlanetDescriptor> planet;
  SurfaceSignalCatalog catalog;
  std::optional<OriginRendezvous> rendezvous;
  std::optional<PlanetaryFlightState> flight;
  std::optional<OriginStationFlightState> station_flight;
  SignalScannerState scanner;
  SignalNavigationSolution signal_navigation;
  std::optional<OriginNavigationSolution> origin_navigation;
  WorldDeltaJournal journal;
  SignalCollectionState collection;
  std::vector<SaveDiscovery> discoveries;
  std::vector<SaveDiscovery> origin_system_discoveries;
  std::vector<SaveWorldDelta> origin_system_world_deltas;
  SignalRunGuidanceState guidance;
};

enum class SignalRunError : std::uint8_t {
  invalid_document,
  terrain_failure,
  invalid_target,
  inconsistent_state,
  invalid_transition,
  flight_failure,
  navigation_failure,
  collection_failure,
  journal_failure,
};

[[nodiscard]] auto signal_run_error_name(SignalRunError error) noexcept
    -> std::string_view;

[[nodiscard]] auto hydrate_signal_run(const SaveDocument& document,
                                      TerrainTileCache& cache)
    -> std::expected<SignalRunState, SignalRunError>;

[[nodiscard]] auto accept_signal_run(SignalRunState& state)
    -> std::expected<void, SignalRunError>;

[[nodiscard]] auto launch_signal_run(SignalRunState& state,
                                     TerrainTileCache& cache)
    -> std::expected<void, SignalRunError>;

[[nodiscard]] auto begin_signal_run_planetfall(SignalRunState& state,
                                               TerrainTileCache& cache)
    -> std::expected<void, SignalRunError>;

[[nodiscard]] auto advance_signal_run_station_flight(
    SignalRunState& state, std::span<const FlightCommand> commands)
    -> std::expected<void, SignalRunError>;

[[nodiscard]] auto depart_signal_run_home_planet(SignalRunState& state)
    -> std::expected<void, SignalRunError>;

[[nodiscard]] auto select_signal_run_target(SignalRunState& state,
                                            SignalSelectionCommand command)
    -> std::expected<void, SignalRunError>;

// Advances exactly one fixed simulation tick. Rejected updates leave the
// authoritative state unchanged.
[[nodiscard]] auto advance_signal_run(
    SignalRunState& state, TerrainTileCache& cache,
    std::span<const FlightCommand> commands)
    -> std::expected<void, SignalRunError>;

[[nodiscard]] auto return_signal_run_to_origin(SignalRunState& state)
    -> std::expected<void, SignalRunError>;

[[nodiscard]] auto turn_in_signal_run(SignalRunState& state)
    -> std::expected<void, SignalRunError>;

[[nodiscard]] auto project_signal_run_save(const SignalRunState& state)
    -> std::expected<SaveDocument, SignalRunError>;

}  // namespace apsis_drift
