#include "apsis_drift/origin_system_contract.hpp"

#include <cmath>
#include <limits>
#include <utility>

#include "apsis_drift/origin_station.hpp"
#include "apsis_drift/seed.hpp"

namespace apsis_drift {
namespace {

[[nodiscard]] auto valid_phase(OriginSystemContractPhase phase) noexcept
    -> bool {
  switch (phase) {
    case OriginSystemContractPhase::offered:
    case OriginSystemContractPhase::accepted:
    case OriginSystemContractPhase::station_departure:
    case OriginSystemContractPhase::outbound_transfer:
    case OriginSystemContractPhase::target_planet:
    case OriginSystemContractPhase::objective_complete:
    case OriginSystemContractPhase::return_transfer:
    case OriginSystemContractPhase::station_rendezvous:
    case OriginSystemContractPhase::returned:
    case OriginSystemContractPhase::turned_in: return true;
  }
  return false;
}

[[nodiscard]] auto normalized_direction(SystemPositionMetres from,
                                        SystemPositionMetres to)
    -> std::expected<SystemDirection, OriginSystemContractError> {
  const double x = to.x - from.x;
  const double y = to.y - from.y;
  const double z = to.z - from.z;
  const double length = std::hypot(x, y, z);
  if (!std::isfinite(length) || length <= 1.0e-9) {
    return std::unexpected{OriginSystemContractError::invalid_flight};
  }
  return SystemDirection{x / length, y / length, z / length};
}

} // namespace

auto generate_origin_system_contract(Seed universe_seed)
    -> std::expected<OriginSystemContractBinding, OriginSystemContractError> {
  const auto system = generate_origin_system(universe_seed);
  if (!validate_local_system(system) ||
      system.planets.size() <= kOriginSystemContractTargetOrdinal) {
    return std::unexpected{OriginSystemContractError::invalid_system};
  }
  const auto station = generate_origin_station(universe_seed);
  const auto& target = system.planets[kOriginSystemContractTargetOrdinal];
  if (target.descriptor.id == station.orbit.host_planet) {
    return std::unexpected{OriginSystemContractError::invalid_binding};
  }
  const auto mission_seed = derive_seed(system.seed, SeedDomain::mission,
                                        kOriginSystemContractMissionOrdinal);
  const auto objective_seed =
      derive_seed(target.descriptor.seed, SeedDomain::encounter,
                  kOriginSystemContractObjectiveOrdinal);
  return OriginSystemContractBinding{
      .mission_seed = mission_seed,
      .contract = MissionId{mission_seed.value},
      .system = system.id,
      .station = station.id,
      .home_planet = station.orbit.host_planet,
      .target_planet = target.descriptor.id,
      .target_objective = SurfaceSignalId{objective_seed.value},
      .target_ordinal = kOriginSystemContractTargetOrdinal,
  };
}

auto initial_origin_system_contract(Seed universe_seed)
    -> std::expected<OriginSystemContractState, OriginSystemContractError> {
  auto binding = generate_origin_system_contract(universe_seed);
  if (!binding) return std::unexpected{binding.error()};
  return OriginSystemContractState{.binding = *binding};
}

auto validate_origin_system_contract(Seed universe_seed,
                                     const OriginSystemContractState& state)
    -> std::expected<void, OriginSystemContractError> {
  const auto expected = generate_origin_system_contract(universe_seed);
  if (!expected) return std::unexpected{expected.error()};
  if (state.binding != *expected) {
    return std::unexpected{OriginSystemContractError::invalid_binding};
  }
  if (!valid_phase(state.phase)) {
    return std::unexpected{OriginSystemContractError::invalid_state};
  }
  return {};
}

auto advance_origin_system_contract(OriginSystemContractState& state,
                                    SimulationTick authoritative_tick,
                                    SimulationTick command_tick,
                                    OriginSystemContractCommand command)
    -> std::expected<void, OriginSystemContractError> {
  if (command_tick != authoritative_tick) {
    return std::unexpected{OriginSystemContractError::wrong_command_tick};
  }
  auto next = state;
  const auto transition = [&](OriginSystemContractPhase from,
                              OriginSystemContractPhase to) {
    if (next.phase != from) return false;
    next.phase = to;
    return true;
  };
  bool accepted{};
  switch (command) {
    case OriginSystemContractCommand::accept:
      accepted = transition(OriginSystemContractPhase::offered,
                            OriginSystemContractPhase::accepted);
      break;
    case OriginSystemContractCommand::launch:
      accepted = transition(OriginSystemContractPhase::accepted,
                            OriginSystemContractPhase::station_departure);
      break;
    case OriginSystemContractCommand::begin_outbound_transfer:
      accepted = transition(OriginSystemContractPhase::station_departure,
                            OriginSystemContractPhase::outbound_transfer);
      break;
    case OriginSystemContractCommand::enter_target_planet:
      accepted = transition(OriginSystemContractPhase::outbound_transfer,
                            OriginSystemContractPhase::target_planet);
      break;
    case OriginSystemContractCommand::complete_objective:
      accepted = transition(OriginSystemContractPhase::target_planet,
                            OriginSystemContractPhase::objective_complete);
      break;
    case OriginSystemContractCommand::leave_target_planet:
      accepted = transition(OriginSystemContractPhase::objective_complete,
                            OriginSystemContractPhase::return_transfer);
      break;
    case OriginSystemContractCommand::begin_station_rendezvous:
      accepted = transition(OriginSystemContractPhase::return_transfer,
                            OriginSystemContractPhase::station_rendezvous);
      break;
    case OriginSystemContractCommand::dock:
      accepted = transition(OriginSystemContractPhase::station_rendezvous,
                            OriginSystemContractPhase::returned);
      break;
    case OriginSystemContractCommand::turn_in:
      accepted = transition(OriginSystemContractPhase::returned,
                            OriginSystemContractPhase::turned_in);
      break;
  }
  if (!accepted) {
    return std::unexpected{OriginSystemContractError::invalid_transition};
  }
  state = std::move(next);
  return {};
}

auto initialize_origin_system_outbound_transfer(
    Seed universe_seed, SimulationTick authoritative_tick,
    const LocalSystemDescriptor& system,
    const OriginSystemContractState& contract,
    const OriginStationFlightState& departure)
    -> std::expected<SystemFlightState, OriginSystemContractError> {
  if (!validate_origin_system_contract(universe_seed, contract) ||
      contract.phase != OriginSystemContractPhase::station_departure ||
      !validate_origin_station_flight_state(universe_seed, authoritative_tick,
                                            system, departure)) {
    return std::unexpected{OriginSystemContractError::invalid_state};
  }
  const auto guidance = resolve_origin_station_flight_guidance(
      universe_seed, authoritative_tick, system, departure);
  const auto pose = resolve_origin_station_flight_pose(
      universe_seed, authoritative_tick, system, departure);
  const auto target = resolve_planet_ephemeris(
      system, contract.binding.target_planet,
      {.tick = authoritative_tick, .sub_tick_fraction = 0.0});
  if (!guidance || guidance->arrived || !pose || !target) {
    return std::unexpected{OriginSystemContractError::invalid_flight};
  }
  const auto forward = normalized_direction(pose->position, target->position);
  if (!forward) return std::unexpected{forward.error()};
  SystemFlightState result{
      .tick = authoritative_tick,
      .system = system.id,
      .target = contract.binding.target_planet,
      .position = pose->position,
      .velocity = pose->velocity,
      .forward = *forward,
      .up = departure.up,
      .mode = FlightMode::autopilot,
      .controls = {},
      .time_scale = SystemTimeScale::one,
  };
  if (!validate_system_flight_state(system, result)) {
    return std::unexpected{OriginSystemContractError::target_unreachable};
  }
  return result;
}

auto initialize_origin_system_return_transfer(
    Seed universe_seed, const LocalSystemDescriptor& system,
    const OriginSystemContractState& contract,
    const SystemFlightState& departure)
    -> std::expected<SystemFlightState, OriginSystemContractError> {
  if (!validate_origin_system_contract(universe_seed, contract) ||
      contract.phase != OriginSystemContractPhase::objective_complete ||
      departure.system != contract.binding.system ||
      departure.target != contract.binding.target_planet ||
      !validate_system_flight_state(system, departure)) {
    return std::unexpected{OriginSystemContractError::invalid_flight};
  }
  const auto target = resolve_planet_ephemeris(
      system, contract.binding.home_planet,
      {.tick = departure.tick, .sub_tick_fraction = 0.0});
  if (!target) {
    return std::unexpected{OriginSystemContractError::invalid_system};
  }
  auto result = departure;
  result.target = contract.binding.home_planet;
  result.mode = FlightMode::autopilot;
  result.controls = {};
  result.time_scale = SystemTimeScale::one;
  const auto forward = normalized_direction(result.position, target->position);
  if (!forward) return std::unexpected{forward.error()};
  result.forward = *forward;
  if (!validate_system_flight_state(system, result)) {
    return std::unexpected{OriginSystemContractError::target_unreachable};
  }
  return result;
}

auto initialize_origin_system_station_rendezvous(
    Seed universe_seed, const LocalSystemDescriptor& system,
    const OriginSystemContractState& contract,
    const SystemFlightState& home_approach)
    -> std::expected<OriginStationFlightState, OriginSystemContractError> {
  if (!validate_origin_system_contract(universe_seed, contract) ||
      contract.phase != OriginSystemContractPhase::return_transfer ||
      home_approach.target != contract.binding.home_planet ||
      !validate_system_flight_state(system, home_approach)) {
    return std::unexpected{OriginSystemContractError::invalid_flight};
  }
  const auto guidance = resolve_system_flight_guidance(system, home_approach);
  if (!guidance || !guidance->inside_approach_boundary) {
    return std::unexpected{OriginSystemContractError::target_unreachable};
  }
  auto station =
      initialize_origin_station_approach(universe_seed, system, home_approach);
  if (!station) {
    return std::unexpected{OriginSystemContractError::invalid_flight};
  }
  return *station;
}

} // namespace apsis_drift
