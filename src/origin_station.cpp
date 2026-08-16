#include "apsis_drift/origin_station.hpp"

#include <format>

namespace apsis_drift {
namespace {

[[nodiscard]] auto valid_state(const OriginOnboardingState& state) noexcept
    -> bool {
  switch (state.location) {
    case OriginLocation::docked_at_origin:
      return state.first_objective == FirstObjectiveStatus::offered ||
             state.first_objective == FirstObjectiveStatus::active ||
             state.first_objective == FirstObjectiveStatus::completed;
    case OriginLocation::in_flight:
      return state.first_objective == FirstObjectiveStatus::active ||
             state.first_objective == FirstObjectiveStatus::completed;
  }
  return false;
}

}  // namespace

auto generate_origin_station(Seed universe_seed) noexcept
    -> OriginStationDescriptor {
  const auto home_system_seed =
      derive_seed(universe_seed, SeedDomain::system, kOriginSystemOrdinal);
  const auto station_seed = derive_seed(
      home_system_seed, SeedDomain::settlement, kOriginStationOrdinal);
  return OriginStationDescriptor{
      .universe_seed = universe_seed,
      .home_system_seed = home_system_seed,
      .station_seed = station_seed,
      .id = OriginStationId{station_seed.value},
  };
}

auto origin_station_id_string(OriginStationId id) -> std::string {
  return std::format("station-{:016x}", id.value);
}

auto initial_origin_onboarding_state(
    const OriginStationDescriptor& station) noexcept -> OriginOnboardingState {
  return OriginOnboardingState{
      .origin_station = station.id,
      .location = OriginLocation::docked_at_origin,
      .first_objective = FirstObjectiveStatus::offered,
  };
}

auto advance_origin_onboarding(
    OriginOnboardingState& state, OriginOnboardingCommand command) noexcept
    -> std::expected<void, OriginOnboardingError> {
  if (!valid_state(state)) {
    return std::unexpected{OriginOnboardingError::invalid_state};
  }

  auto next = state;
  switch (command) {
    case OriginOnboardingCommand::accept_first_objective:
      if (state.location != OriginLocation::docked_at_origin ||
          state.first_objective != FirstObjectiveStatus::offered) {
        return std::unexpected{OriginOnboardingError::invalid_transition};
      }
      next.first_objective = FirstObjectiveStatus::active;
      break;
    case OriginOnboardingCommand::launch:
      if (state.location != OriginLocation::docked_at_origin ||
          state.first_objective != FirstObjectiveStatus::active) {
        return std::unexpected{OriginOnboardingError::invalid_transition};
      }
      next.location = OriginLocation::in_flight;
      break;
    case OriginOnboardingCommand::complete_first_objective:
      if (state.location != OriginLocation::in_flight ||
          state.first_objective != FirstObjectiveStatus::active) {
        return std::unexpected{OriginOnboardingError::invalid_transition};
      }
      next.first_objective = FirstObjectiveStatus::completed;
      break;
    case OriginOnboardingCommand::return_to_origin:
      if (state.location != OriginLocation::in_flight ||
          state.first_objective != FirstObjectiveStatus::completed) {
        return std::unexpected{OriginOnboardingError::invalid_transition};
      }
      next.location = OriginLocation::docked_at_origin;
      break;
    default:
      return std::unexpected{OriginOnboardingError::invalid_transition};
  }

  state = next;
  return {};
}

}  // namespace apsis_drift
