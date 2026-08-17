#include "apsis_drift/intersystem_contract.hpp"

#include <format>
#include <limits>

namespace apsis_drift {
namespace {

[[nodiscard]] auto elapsed_at_least(SimulationTick now, SimulationTick start,
                                    SimulationTick duration) noexcept -> bool {
  return now >= start && now - start >= duration;
}

[[nodiscard]] auto valid_mission_phase(IntersystemMissionPhase phase) noexcept
    -> bool {
  switch (phase) {
    case IntersystemMissionPhase::offered:
    case IntersystemMissionPhase::accepted:
    case IntersystemMissionPhase::active:
    case IntersystemMissionPhase::objective_complete:
    case IntersystemMissionPhase::returned:
    case IntersystemMissionPhase::turned_in:
      return true;
  }
  return false;
}

[[nodiscard]] auto valid_travel_phase(IntersystemTravelPhase phase) noexcept
    -> bool {
  switch (phase) {
    case IntersystemTravelPhase::docked_at_origin:
    case IntersystemTravelPhase::origin_system_flight:
    case IntersystemTravelPhase::outbound_jump_spooling:
    case IntersystemTravelPhase::outbound_jump_committed:
    case IntersystemTravelPhase::target_system_flight:
    case IntersystemTravelPhase::target_planet_flight:
    case IntersystemTravelPhase::return_jump_spooling:
    case IntersystemTravelPhase::return_jump_committed:
    case IntersystemTravelPhase::origin_system_return:
      return true;
  }
  return false;
}

}  // namespace

auto generate_first_intersystem_identities(Seed universe_seed) noexcept
    -> FirstIntersystemIdentities {
  const auto station = generate_origin_station(universe_seed);
  const auto target_system_seed =
      derive_seed(universe_seed, SeedDomain::system, kFirstTargetSystemOrdinal);
  const auto target_star_seed =
      derive_seed(target_system_seed, SeedDomain::star, kSystemStarOrdinal);
  const auto target_planet_seed = derive_seed(
      target_system_seed, SeedDomain::planet, kFirstMissionTargetPlanetOrdinal);
  const auto target_orbit_seed = derive_seed(
      target_system_seed, SeedDomain::orbit, kFirstMissionTargetPlanetOrdinal);
  const auto mission_seed = derive_seed(
      station.station_seed, SeedDomain::mission, kFirstMissionOrdinal);
  return {
      .universe_seed = universe_seed,
      .origin_system_seed = station.home_system_seed,
      .origin_system = SystemId{station.home_system_seed.value},
      .origin_station = station.id,
      .target_system_seed = target_system_seed,
      .target_system = SystemId{target_system_seed.value},
      .target_star_seed = target_star_seed,
      .target_star = StarId{target_star_seed.value},
      .target_planet_seed = target_planet_seed,
      .target_planet = PlanetId{target_planet_seed.value},
      .target_orbit_seed = target_orbit_seed,
      .mission_seed = mission_seed,
      .mission = MissionId{mission_seed.value},
  };
}

auto system_id_string(SystemId id) -> std::string {
  return std::format("system-{:016x}", id.value);
}

auto star_id_string(StarId id) -> std::string {
  return std::format("star-{:016x}", id.value);
}

auto mission_id_string(MissionId id) -> std::string {
  return std::format("mission-{:016x}", id.value);
}

auto initial_intersystem_contract_state(Seed universe_seed) noexcept
    -> IntersystemContractState {
  auto identities = generate_first_intersystem_identities(universe_seed);
  return {
      .identities = identities,
      .universe_tick = 0,
      .mission_phase = IntersystemMissionPhase::offered,
      .travel_phase = IntersystemTravelPhase::docked_at_origin,
      .current_system = identities.origin_system,
      .current_planet = std::nullopt,
      .committed_jump_destination = std::nullopt,
      .phase_started_tick = std::nullopt,
  };
}

auto validate_intersystem_contract_state(
    const IntersystemContractState& state) noexcept
    -> std::expected<void, IntersystemContractError> {
  if (state.identities != generate_first_intersystem_identities(
                              state.identities.universe_seed) ||
      state.identities.origin_system == state.identities.target_system ||
      !valid_mission_phase(state.mission_phase) ||
      !valid_travel_phase(state.travel_phase)) {
    return std::unexpected{IntersystemContractError::invalid_state};
  }

  const auto origin = state.identities.origin_system;
  const auto target = state.identities.target_system;
  const auto target_planet = state.identities.target_planet;
  switch (state.travel_phase) {
    case IntersystemTravelPhase::docked_at_origin:
      if (state.current_system != origin || state.current_planet ||
          state.committed_jump_destination || state.phase_started_tick ||
          (state.mission_phase != IntersystemMissionPhase::offered &&
           state.mission_phase != IntersystemMissionPhase::accepted &&
           state.mission_phase != IntersystemMissionPhase::returned &&
           state.mission_phase != IntersystemMissionPhase::turned_in)) {
        return std::unexpected{IntersystemContractError::invalid_state};
      }
      break;
    case IntersystemTravelPhase::origin_system_flight:
      if (state.current_system != origin || state.current_planet ||
          state.committed_jump_destination || state.phase_started_tick ||
          state.mission_phase != IntersystemMissionPhase::active) {
        return std::unexpected{IntersystemContractError::invalid_state};
      }
      break;
    case IntersystemTravelPhase::outbound_jump_spooling:
      if (state.current_system != origin || state.current_planet ||
          state.committed_jump_destination || !state.phase_started_tick ||
          *state.phase_started_tick > state.universe_tick ||
          state.mission_phase != IntersystemMissionPhase::active) {
        return std::unexpected{IntersystemContractError::invalid_state};
      }
      break;
    case IntersystemTravelPhase::outbound_jump_committed:
      if (state.current_system != origin || state.current_planet ||
          state.committed_jump_destination != target ||
          !state.phase_started_tick ||
          *state.phase_started_tick > state.universe_tick ||
          state.mission_phase != IntersystemMissionPhase::active) {
        return std::unexpected{IntersystemContractError::invalid_state};
      }
      break;
    case IntersystemTravelPhase::target_system_flight:
      if (state.current_system != target || state.current_planet ||
          state.committed_jump_destination || state.phase_started_tick ||
          (state.mission_phase != IntersystemMissionPhase::active &&
           state.mission_phase !=
               IntersystemMissionPhase::objective_complete)) {
        return std::unexpected{IntersystemContractError::invalid_state};
      }
      break;
    case IntersystemTravelPhase::target_planet_flight:
      if (state.current_system != target ||
          state.current_planet != target_planet ||
          state.committed_jump_destination || state.phase_started_tick ||
          (state.mission_phase != IntersystemMissionPhase::active &&
           state.mission_phase !=
               IntersystemMissionPhase::objective_complete)) {
        return std::unexpected{IntersystemContractError::invalid_state};
      }
      break;
    case IntersystemTravelPhase::return_jump_spooling:
      if (state.current_system != target || state.current_planet ||
          state.committed_jump_destination || !state.phase_started_tick ||
          *state.phase_started_tick > state.universe_tick ||
          state.mission_phase != IntersystemMissionPhase::objective_complete) {
        return std::unexpected{IntersystemContractError::invalid_state};
      }
      break;
    case IntersystemTravelPhase::return_jump_committed:
      if (state.current_system != target || state.current_planet ||
          state.committed_jump_destination != origin ||
          !state.phase_started_tick ||
          *state.phase_started_tick > state.universe_tick ||
          state.mission_phase != IntersystemMissionPhase::objective_complete) {
        return std::unexpected{IntersystemContractError::invalid_state};
      }
      break;
    case IntersystemTravelPhase::origin_system_return:
      if (state.current_system != origin || state.current_planet ||
          state.committed_jump_destination || state.phase_started_tick ||
          state.mission_phase != IntersystemMissionPhase::objective_complete) {
        return std::unexpected{IntersystemContractError::invalid_state};
      }
      break;
  }
  return {};
}

auto advance_intersystem_time(IntersystemContractState& state,
                              SimulationTick ticks) noexcept
    -> std::expected<void, IntersystemContractError> {
  if (!validate_intersystem_contract_state(state)) {
    return std::unexpected{IntersystemContractError::invalid_state};
  }
  if (ticks == 0) {
    return std::unexpected{IntersystemContractError::invalid_time_advance};
  }
  if (ticks >
      std::numeric_limits<SimulationTick>::max() - state.universe_tick) {
    return std::unexpected{IntersystemContractError::tick_overflow};
  }
  state.universe_tick += ticks;
  return {};
}

auto advance_intersystem_contract(IntersystemContractState& state,
                                  SimulationTick command_tick,
                                  IntersystemContractCommand command) noexcept
    -> std::expected<void, IntersystemContractError> {
  if (!validate_intersystem_contract_state(state)) {
    return std::unexpected{IntersystemContractError::invalid_state};
  }
  if (command_tick != state.universe_tick) {
    return std::unexpected{IntersystemContractError::wrong_command_tick};
  }

  auto next = state;
  const auto reject = [] {
    return std::expected<void, IntersystemContractError>{
        std::unexpected{IntersystemContractError::invalid_transition}};
  };
  switch (command) {
    case IntersystemContractCommand::accept_mission:
      if (next.travel_phase != IntersystemTravelPhase::docked_at_origin ||
          next.mission_phase != IntersystemMissionPhase::offered) {
        return reject();
      }
      next.mission_phase = IntersystemMissionPhase::accepted;
      break;
    case IntersystemContractCommand::launch:
      if (next.travel_phase != IntersystemTravelPhase::docked_at_origin ||
          next.mission_phase != IntersystemMissionPhase::accepted) {
        return reject();
      }
      next.mission_phase = IntersystemMissionPhase::active;
      next.travel_phase = IntersystemTravelPhase::origin_system_flight;
      break;
    case IntersystemContractCommand::begin_outbound_jump:
      if (next.travel_phase != IntersystemTravelPhase::origin_system_flight ||
          next.mission_phase != IntersystemMissionPhase::active) {
        return reject();
      }
      next.travel_phase = IntersystemTravelPhase::outbound_jump_spooling;
      next.phase_started_tick = next.universe_tick;
      break;
    case IntersystemContractCommand::cancel_jump:
      if (next.travel_phase == IntersystemTravelPhase::outbound_jump_spooling) {
        next.travel_phase = IntersystemTravelPhase::origin_system_flight;
      } else if (next.travel_phase ==
                 IntersystemTravelPhase::return_jump_spooling) {
        next.travel_phase = IntersystemTravelPhase::target_system_flight;
      } else {
        return reject();
      }
      next.phase_started_tick.reset();
      break;
    case IntersystemContractCommand::commit_outbound_jump:
      if (next.travel_phase != IntersystemTravelPhase::outbound_jump_spooling ||
          !next.phase_started_tick ||
          !elapsed_at_least(next.universe_tick, *next.phase_started_tick,
                            kJumpSpoolTicks)) {
        return reject();
      }
      next.travel_phase = IntersystemTravelPhase::outbound_jump_committed;
      next.committed_jump_destination = next.identities.target_system;
      next.phase_started_tick = next.universe_tick;
      break;
    case IntersystemContractCommand::arrive_target_system:
      if (next.travel_phase !=
              IntersystemTravelPhase::outbound_jump_committed ||
          !next.phase_started_tick ||
          !elapsed_at_least(next.universe_tick, *next.phase_started_tick,
                            kJumpTransitTicks)) {
        return reject();
      }
      next.travel_phase = IntersystemTravelPhase::target_system_flight;
      next.current_system = next.identities.target_system;
      next.committed_jump_destination.reset();
      next.phase_started_tick.reset();
      break;
    case IntersystemContractCommand::enter_target_planet:
      if (next.travel_phase != IntersystemTravelPhase::target_system_flight ||
          next.mission_phase != IntersystemMissionPhase::active) {
        return reject();
      }
      next.travel_phase = IntersystemTravelPhase::target_planet_flight;
      next.current_planet = next.identities.target_planet;
      break;
    case IntersystemContractCommand::complete_objective:
      if (next.travel_phase != IntersystemTravelPhase::target_planet_flight ||
          next.mission_phase != IntersystemMissionPhase::active) {
        return reject();
      }
      next.mission_phase = IntersystemMissionPhase::objective_complete;
      break;
    case IntersystemContractCommand::leave_target_planet:
      if (next.travel_phase != IntersystemTravelPhase::target_planet_flight ||
          (next.mission_phase != IntersystemMissionPhase::active &&
           next.mission_phase != IntersystemMissionPhase::objective_complete)) {
        return reject();
      }
      next.travel_phase = IntersystemTravelPhase::target_system_flight;
      next.current_planet.reset();
      break;
    case IntersystemContractCommand::begin_return_jump:
      if (next.travel_phase != IntersystemTravelPhase::target_system_flight ||
          next.mission_phase != IntersystemMissionPhase::objective_complete) {
        return reject();
      }
      next.travel_phase = IntersystemTravelPhase::return_jump_spooling;
      next.phase_started_tick = next.universe_tick;
      break;
    case IntersystemContractCommand::commit_return_jump:
      if (next.travel_phase != IntersystemTravelPhase::return_jump_spooling ||
          !next.phase_started_tick ||
          !elapsed_at_least(next.universe_tick, *next.phase_started_tick,
                            kJumpSpoolTicks)) {
        return reject();
      }
      next.travel_phase = IntersystemTravelPhase::return_jump_committed;
      next.committed_jump_destination = next.identities.origin_system;
      next.phase_started_tick = next.universe_tick;
      break;
    case IntersystemContractCommand::arrive_origin_system:
      if (next.travel_phase != IntersystemTravelPhase::return_jump_committed ||
          !next.phase_started_tick ||
          !elapsed_at_least(next.universe_tick, *next.phase_started_tick,
                            kJumpTransitTicks)) {
        return reject();
      }
      next.travel_phase = IntersystemTravelPhase::origin_system_return;
      next.current_system = next.identities.origin_system;
      next.committed_jump_destination.reset();
      next.phase_started_tick.reset();
      break;
    case IntersystemContractCommand::dock_at_origin:
      if (next.travel_phase != IntersystemTravelPhase::origin_system_return ||
          next.mission_phase != IntersystemMissionPhase::objective_complete) {
        return reject();
      }
      next.travel_phase = IntersystemTravelPhase::docked_at_origin;
      next.mission_phase = IntersystemMissionPhase::returned;
      break;
    case IntersystemContractCommand::turn_in:
      if (next.travel_phase != IntersystemTravelPhase::docked_at_origin ||
          next.mission_phase != IntersystemMissionPhase::returned) {
        return reject();
      }
      next.mission_phase = IntersystemMissionPhase::turned_in;
      break;
    default:
      return reject();
  }

  if (!validate_intersystem_contract_state(next)) {
    return std::unexpected{IntersystemContractError::invalid_state};
  }
  state = next;
  return {};
}

}  // namespace apsis_drift
