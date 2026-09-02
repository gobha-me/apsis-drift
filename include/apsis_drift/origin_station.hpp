#pragma once

#include <cstdint>
#include <expected>
#include <string>

#include "apsis_drift/planet.hpp"
#include "apsis_drift/seed.hpp"
#include "apsis_drift/simulation.hpp"
#include "apsis_drift/surface_signals.hpp"

namespace apsis_drift {

// Origin-station generation is generated-world compatibility data. Changing
// its derivation path, ordinals, or identifier mapping requires a new version.
inline constexpr std::uint32_t kOriginHomePlanetGeneratorVersion{1};
inline constexpr std::uint32_t kOriginStationGeneratorVersion{2};
inline constexpr std::uint32_t kHomeSignalContractGeneratorVersion{1};
inline constexpr std::uint64_t kOriginSystemOrdinal{0};
inline constexpr std::uint64_t kOriginHomePlanetOrdinal{0};
inline constexpr std::uint64_t kOriginStationOrdinal{0};
inline constexpr std::uint64_t kHomeSignalContractOrdinal{1};
inline constexpr std::uint64_t kHomeSignalTargetOrdinal{0};
inline constexpr std::uint32_t kOriginHomeMinimumRadiusKilometres{5'000};
inline constexpr std::uint32_t kOriginHomeMaximumRadiusKilometres{6'500};
inline constexpr std::uint16_t kOriginHomeMinimumGravityMilliG{750};
inline constexpr std::uint16_t kOriginHomeMaximumGravityMilliG{1'100};
inline constexpr std::uint16_t kOriginHomeMinimumPressureMillibars{700};
inline constexpr std::uint16_t kOriginHomeMaximumPressureMillibars{1'200};
inline constexpr std::uint16_t kOriginHomeMinimumWaterBasisPoints{1'000};
inline constexpr std::uint16_t kOriginHomeMaximumWaterBasisPoints{4'500};
inline constexpr std::uint32_t kOriginStationMinimumAltitudeKilometres{400};
inline constexpr std::uint32_t kOriginStationMaximumAltitudeKilometres{600};
inline constexpr SimulationTick kOriginStationMinimumPeriodTicks{90U * 60U *
                                                                 kSimulationHz};
inline constexpr SimulationTick kOriginStationMaximumPeriodTicks{120U * 60U *
                                                                 kSimulationHz};
inline constexpr std::int32_t kOriginStationMaximumInclinationMicrodegrees{
    5'000'000};

struct OriginStationId {
  std::uint64_t value{};

  friend auto operator==(const OriginStationId&, const OriginStationId&)
      -> bool = default;
};

struct HomeSignalContractId {
  std::uint64_t value{};

  friend auto operator==(const HomeSignalContractId&,
                         const HomeSignalContractId&) -> bool = default;
};

struct HomeSignalContractBinding {
  HomeSignalContractId contract;
  OriginStationId station;
  PlanetId home_planet;
  SurfaceSignalId target;

  friend auto operator==(const HomeSignalContractBinding&,
                         const HomeSignalContractBinding&) -> bool = default;
};

struct OriginStationOrbit {
  PlanetId host_planet;
  std::uint64_t radius_kilometres{};
  SimulationTick period_ticks{};
  std::uint32_t epoch_phase_turns{};
  std::int32_t inclination_microdegrees{};
  std::uint32_t ascending_node_turns{};

  friend auto operator==(const OriginStationOrbit&, const OriginStationOrbit&)
      -> bool = default;
};

struct OriginStationDescriptor {
  const Seed universe_seed;
  const Seed home_system_seed;
  const Seed station_seed;
  const OriginStationId id;
  const OriginStationOrbit orbit;

  friend auto operator==(const OriginStationDescriptor&,
                         const OriginStationDescriptor&) -> bool = default;
};

[[nodiscard]] auto generate_origin_home_planet(Seed home_system_seed)
    -> PlanetDescriptor;

[[nodiscard]] auto is_tutorial_safe_home_planet(
    const PlanetDescriptor& planet) noexcept -> bool;

// The station is a child of the origin system's settlement domain. It is not
// the universe root, system barycenter, or parent of unrelated content.
[[nodiscard]] auto generate_origin_station(Seed universe_seed) noexcept
    -> OriginStationDescriptor;

[[nodiscard]] auto origin_station_id_string(OriginStationId id) -> std::string;
[[nodiscard]] auto home_signal_contract_id_string(HomeSignalContractId id)
    -> std::string;
[[nodiscard]] auto generate_home_signal_contract(Seed universe_seed) noexcept
    -> HomeSignalContractBinding;

enum class OriginLocation : std::uint8_t {
  docked_at_origin,
  in_flight,
};

enum class FirstObjectiveStatus : std::uint8_t {
  offered,
  active,
  completed,
  returned,
  turned_in,
};

// This deliberately small model applies only to a fresh, zero-discovery
// universe. Discovery identities and world deltas remain separate state owned
// by the Signal Run persistence work.
struct OriginOnboardingState {
  OriginStationId origin_station;
  HomeSignalContractId first_contract;
  SurfaceSignalId first_target;
  OriginLocation location{OriginLocation::docked_at_origin};
  FirstObjectiveStatus first_objective{FirstObjectiveStatus::offered};

  friend auto operator==(const OriginOnboardingState&,
                         const OriginOnboardingState&) -> bool = default;
};

enum class OriginOnboardingCommand : std::uint8_t {
  accept_first_objective,
  launch,
  redock,
  complete_first_objective,
  dock_at_origin,
  turn_in,
};

enum class OriginOnboardingError : std::uint8_t {
  invalid_state,
  invalid_transition,
};

[[nodiscard]] auto initial_origin_onboarding_state(
    const OriginStationDescriptor& station) noexcept -> OriginOnboardingState;

// Applies one atomic transition. Every failure leaves state unchanged.
[[nodiscard]] auto advance_origin_onboarding(
    OriginOnboardingState& state, OriginOnboardingCommand command) noexcept
    -> std::expected<void, OriginOnboardingError>;

} // namespace apsis_drift
