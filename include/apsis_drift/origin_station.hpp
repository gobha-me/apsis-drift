#pragma once

#include <cstdint>
#include <expected>
#include <string>

#include "apsis_drift/seed.hpp"

namespace apsis_drift {

// Origin-station generation is generated-world compatibility data. Changing
// its derivation path, ordinals, or identifier mapping requires a new version.
inline constexpr std::uint32_t kOriginStationGeneratorVersion{1};
inline constexpr std::uint64_t kOriginSystemOrdinal{0};
inline constexpr std::uint64_t kOriginStationOrdinal{0};

struct OriginStationId {
  std::uint64_t value{};

  friend auto operator==(const OriginStationId&, const OriginStationId&)
      -> bool = default;
};

struct OriginStationDescriptor {
  const Seed universe_seed;
  const Seed home_system_seed;
  const Seed station_seed;
  const OriginStationId id;

  friend auto operator==(const OriginStationDescriptor&,
                         const OriginStationDescriptor&) -> bool = default;
};

// The station is a child of the origin system's settlement domain. It is not
// the universe root, system barycenter, or parent of unrelated content.
[[nodiscard]] auto generate_origin_station(Seed universe_seed) noexcept
    -> OriginStationDescriptor;

[[nodiscard]] auto origin_station_id_string(OriginStationId id)
    -> std::string;

enum class OriginLocation : std::uint8_t {
  docked_at_origin,
  in_flight,
};

enum class FirstObjectiveStatus : std::uint8_t {
  offered,
  active,
  completed,
};

// This deliberately small model applies only to a fresh, zero-discovery
// universe. Discovery identities and world deltas remain separate state owned
// by the Signal Run persistence work.
struct OriginOnboardingState {
  OriginStationId origin_station;
  OriginLocation location{OriginLocation::docked_at_origin};
  FirstObjectiveStatus first_objective{FirstObjectiveStatus::offered};

  friend auto operator==(const OriginOnboardingState&,
                         const OriginOnboardingState&) -> bool = default;
};

enum class OriginOnboardingCommand : std::uint8_t {
  accept_first_objective,
  launch,
  complete_first_objective,
  return_to_origin,
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

}  // namespace apsis_drift
