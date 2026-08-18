#include "apsis_drift/intersystem_jump.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace apsis_drift {
namespace {

[[nodiscard]] auto finite(SystemPositionMetres value) noexcept -> bool {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] auto finite(SystemVelocityMetresPerSecond value) noexcept
    -> bool {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] auto elapsed(const IntersystemContractState& contract) noexcept
    -> SimulationTick {
  return contract.phase_started_tick &&
                 *contract.phase_started_tick <= contract.universe_tick
             ? contract.universe_tick - *contract.phase_started_tick
             : 0;
}

auto hash_word(std::uint64_t& hash, std::uint64_t value) noexcept -> void {
  constexpr std::uint64_t prime{1099511628211ULL};
  for (int byte = 0; byte < 8; ++byte) {
    hash ^= (value >> (byte * 8)) & 0xFFU;
    hash *= prime;
  }
}

}  // namespace

auto validate_intersystem_arrival_solution(
    const IntersystemContractState& contract,
    const IntersystemArrivalSolution& solution) noexcept
    -> std::expected<void, IntersystemJumpError> {
  if (!validate_intersystem_contract_state(contract)) {
    return std::unexpected{IntersystemJumpError::invalid_contract};
  }
  if (!finite(solution.position) || !finite(solution.velocity) ||
      solution.destination !=
          (contract.committed_jump_destination
               ? *contract.committed_jump_destination
               : contract.current_system)) {
    return std::unexpected{IntersystemJumpError::invalid_arrival};
  }
  if (solution.reference_planet &&
      (*solution.reference_planet != contract.identities.target_planet ||
       solution.destination != contract.identities.target_system)) {
    return std::unexpected{IntersystemJumpError::invalid_arrival};
  }
  return {};
}

auto resolve_assisted_jump_arrival(
    const IntersystemContractState& contract,
    const LocalSystemDescriptor& destination)
    -> std::expected<IntersystemArrivalSolution, IntersystemJumpError> {
  if (!validate_intersystem_contract_state(contract)) {
    return std::unexpected{IntersystemJumpError::invalid_contract};
  }
  const bool outbound = contract.travel_phase ==
                        IntersystemTravelPhase::outbound_jump_spooling;
  const bool returning = contract.travel_phase ==
                         IntersystemTravelPhase::return_jump_spooling;
  if (!outbound && !returning) {
    return std::unexpected{IntersystemJumpError::invalid_phase};
  }
  const auto expected_destination =
      outbound ? contract.identities.target_system
               : contract.identities.origin_system;
  if (destination.id != expected_destination ||
      !validate_local_system(destination)) {
    return std::unexpected{IntersystemJumpError::invalid_destination};
  }
  if (contract.universe_tick >
      std::numeric_limits<SimulationTick>::max() - kJumpTransitTicks) {
    return std::unexpected{IntersystemJumpError::tick_overflow};
  }
  const auto arrival_tick = contract.universe_tick + kJumpTransitTicks;
  IntersystemArrivalSolution solution{
      .destination = expected_destination,
      .reference_planet = std::nullopt,
      .arrival_tick = arrival_tick,
      .position = {0.0, -kAssistedOriginArrivalRadiusMetres, 0.0},
      .velocity = {},
  };
  if (outbound) {
    const auto body = find_local_system_planet(
        destination, contract.identities.target_planet);
    if (!body) {
      return std::unexpected{IntersystemJumpError::invalid_destination};
    }
    const auto ephemeris = resolve_planet_ephemeris(
        destination, contract.identities.target_planet,
        {.tick = arrival_tick, .sub_tick_fraction = 0.0});
    if (!ephemeris) {
      return std::unexpected{IntersystemJumpError::ephemeris_failure};
    }
    const double speed = std::hypot(ephemeris->velocity.x,
                                    ephemeris->velocity.y,
                                    ephemeris->velocity.z);
    if (!std::isfinite(speed) || speed <= 0.0) {
      return std::unexpected{IntersystemJumpError::invalid_arrival};
    }
    const double standoff =
        static_cast<double>((*body)->descriptor.radius.value) * 1'000.0 *
        kAssistedTargetArrivalStandoffRadii;
    const double scale = standoff / speed;
    solution.reference_planet = contract.identities.target_planet;
    solution.position = {
        ephemeris->position.x - ephemeris->velocity.x * scale,
        ephemeris->position.y - ephemeris->velocity.y * scale,
        ephemeris->position.z - ephemeris->velocity.z * scale,
    };
    solution.velocity = ephemeris->velocity;
  }
  if (!finite(solution.position) || !finite(solution.velocity)) {
    return std::unexpected{IntersystemJumpError::invalid_arrival};
  }
  return solution;
}

auto begin_assisted_jump(IntersystemContractState& contract) noexcept
    -> std::expected<void, IntersystemJumpError> {
  const auto command =
      contract.travel_phase == IntersystemTravelPhase::origin_system_flight
          ? IntersystemContractCommand::begin_outbound_jump
          : contract.travel_phase ==
                    IntersystemTravelPhase::target_system_flight &&
                contract.mission_phase ==
                    IntersystemMissionPhase::objective_complete
            ? IntersystemContractCommand::begin_return_jump
            : static_cast<IntersystemContractCommand>(255);
  auto next = contract;
  next.arrival_solution.reset();
  if (!advance_intersystem_contract(next, next.universe_tick, command)) {
    return std::unexpected{IntersystemJumpError::transition_failure};
  }
  contract = std::move(next);
  return {};
}

auto cancel_assisted_jump(IntersystemContractState& contract) noexcept
    -> std::expected<void, IntersystemJumpError> {
  auto next = contract;
  if (!advance_intersystem_contract(next, next.universe_tick,
                                    IntersystemContractCommand::cancel_jump)) {
    return std::unexpected{IntersystemJumpError::transition_failure};
  }
  next.arrival_solution.reset();
  contract = std::move(next);
  return {};
}

auto advance_assisted_jump_tick(IntersystemContractState& contract,
                                const LocalSystemDescriptor& destination)
    -> std::expected<IntersystemJumpAdvance, IntersystemJumpError> {
  if (!validate_intersystem_contract_state(contract)) {
    return std::unexpected{IntersystemJumpError::invalid_contract};
  }
  const bool outbound =
      contract.travel_phase ==
          IntersystemTravelPhase::outbound_jump_spooling ||
      contract.travel_phase ==
          IntersystemTravelPhase::outbound_jump_committed;
  const bool returning =
      contract.travel_phase == IntersystemTravelPhase::return_jump_spooling ||
      contract.travel_phase == IntersystemTravelPhase::return_jump_committed;
  if (!outbound && !returning) {
    return std::unexpected{IntersystemJumpError::invalid_phase};
  }
  const auto expected_destination =
      outbound ? contract.identities.target_system
               : contract.identities.origin_system;
  if (destination.id != expected_destination) {
    return std::unexpected{IntersystemJumpError::invalid_destination};
  }
  auto next = contract;
  if (!advance_intersystem_time(next, 1)) {
    return std::unexpected{IntersystemJumpError::tick_overflow};
  }
  IntersystemJumpAdvance result;
  const bool spooling =
      next.travel_phase == IntersystemTravelPhase::outbound_jump_spooling ||
      next.travel_phase == IntersystemTravelPhase::return_jump_spooling;
  if (spooling && elapsed(next) >= kJumpSpoolTicks) {
    auto solution = resolve_assisted_jump_arrival(next, destination);
    if (!solution) return std::unexpected{solution.error()};
    const auto command =
        next.travel_phase == IntersystemTravelPhase::outbound_jump_spooling
            ? IntersystemContractCommand::commit_outbound_jump
            : IntersystemContractCommand::commit_return_jump;
    if (!advance_intersystem_contract(next, next.universe_tick, command)) {
      return std::unexpected{IntersystemJumpError::transition_failure};
    }
    next.arrival_solution = std::move(*solution);
    result.committed = true;
  } else {
    const bool committed =
        next.travel_phase ==
            IntersystemTravelPhase::outbound_jump_committed ||
        next.travel_phase == IntersystemTravelPhase::return_jump_committed;
    if (committed && elapsed(next) >= kJumpTransitTicks) {
      if (!next.arrival_solution ||
          next.arrival_solution->arrival_tick != next.universe_tick) {
        return std::unexpected{IntersystemJumpError::invalid_arrival};
      }
      const auto command =
          next.travel_phase ==
                  IntersystemTravelPhase::outbound_jump_committed
              ? IntersystemContractCommand::arrive_target_system
              : IntersystemContractCommand::arrive_origin_system;
      if (!advance_intersystem_contract(next, next.universe_tick, command)) {
        return std::unexpected{IntersystemJumpError::transition_failure};
      }
      result.arrived = true;
    }
  }
  contract = std::move(next);
  return result;
}

auto intersystem_jump_snapshot(const IntersystemContractState& contract)
    -> std::expected<IntersystemJumpSnapshot, IntersystemJumpError> {
  if (!validate_intersystem_contract_state(contract) ||
      !contract.phase_started_tick) {
    return std::unexpected{IntersystemJumpError::invalid_contract};
  }
  const bool spooling =
      contract.travel_phase ==
          IntersystemTravelPhase::outbound_jump_spooling ||
      contract.travel_phase == IntersystemTravelPhase::return_jump_spooling;
  const bool committed =
      contract.travel_phase ==
          IntersystemTravelPhase::outbound_jump_committed ||
      contract.travel_phase == IntersystemTravelPhase::return_jump_committed;
  if (!spooling && !committed) {
    return std::unexpected{IntersystemJumpError::invalid_phase};
  }
  const auto duration = spooling ? kJumpSpoolTicks : kJumpTransitTicks;
  const auto ticks = std::min(elapsed(contract), duration);
  const auto destination =
      contract.travel_phase ==
                  IntersystemTravelPhase::outbound_jump_spooling ||
              contract.travel_phase ==
                  IntersystemTravelPhase::outbound_jump_committed
          ? contract.identities.target_system
          : contract.identities.origin_system;
  return IntersystemJumpSnapshot{
      .phase = spooling ? "SPOOLING" : "TRANSIT COMMITTED",
      .destination = system_id_string(destination),
      .elapsed_ticks = ticks,
      .duration_ticks = duration,
      .progress = static_cast<double>(ticks) / static_cast<double>(duration),
      .cancelable = spooling,
      .committed = committed,
  };
}

auto intersystem_arrival_checksum(
    const IntersystemContractState& contract) noexcept -> std::uint64_t {
  constexpr std::uint64_t offset{14695981039346656037ULL};
  std::uint64_t hash = offset;
  hash_word(hash, contract.universe_tick);
  hash_word(hash, contract.current_system.value);
  hash_word(hash, static_cast<std::uint64_t>(contract.travel_phase));
  if (!contract.arrival_solution) return hash;
  const auto& arrival = *contract.arrival_solution;
  hash_word(hash, arrival.destination.value);
  hash_word(hash, arrival.reference_planet
                      ? arrival.reference_planet->value
                      : std::numeric_limits<std::uint64_t>::max());
  hash_word(hash, arrival.arrival_tick);
  hash_word(hash, std::bit_cast<std::uint64_t>(arrival.position.x));
  hash_word(hash, std::bit_cast<std::uint64_t>(arrival.position.y));
  hash_word(hash, std::bit_cast<std::uint64_t>(arrival.position.z));
  hash_word(hash, std::bit_cast<std::uint64_t>(arrival.velocity.x));
  hash_word(hash, std::bit_cast<std::uint64_t>(arrival.velocity.y));
  hash_word(hash, std::bit_cast<std::uint64_t>(arrival.velocity.z));
  return hash;
}

auto render_intersystem_jump(const IntersystemJumpSnapshot& snapshot,
                             int width, int height,
                             std::span<termforge::Pixel> destination)
    -> std::expected<void, IntersystemJumpError> {
  if (width <= 0 || height <= 0 ||
      static_cast<std::size_t>(width) >
          std::numeric_limits<std::size_t>::max() /
              static_cast<std::size_t>(height) ||
      destination.size() != static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height) ||
      !std::isfinite(snapshot.progress) || snapshot.progress < 0.0 ||
      snapshot.progress > 1.0 || snapshot.duration_ticks == 0 ||
      snapshot.elapsed_ticks > snapshot.duration_ticks) {
    return std::unexpected{IntersystemJumpError::invalid_framebuffer};
  }
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x);
      const std::uint64_t noise =
          (static_cast<std::uint64_t>(x + 1) * 0x9e3779b185ebca87ULL) ^
          (static_cast<std::uint64_t>(y + 1) * 0xc2b2ae3d27d4eb4fULL) ^
          (snapshot.elapsed_ticks * 0x165667b19e3779f9ULL);
      const double dx = static_cast<double>(x) -
                        static_cast<double>(width - 1) * 0.5;
      const double dy = static_cast<double>(y) -
                        static_cast<double>(height - 1) * 0.5;
      const double radius = std::hypot(dx, dy) /
                            static_cast<double>(std::max(width, height));
      const bool star =
          (noise & 0x7ffU) < (snapshot.committed ? 18U : 5U);
      const double flare =
          snapshot.committed ? std::max(0.0, 1.0 - radius * 5.0) : 0.0;
      destination[index] = {
          static_cast<std::uint8_t>(std::clamp(
              8.0 + flare * 150.0 + (star ? 120.0 : 0.0), 0.0, 255.0)),
          static_cast<std::uint8_t>(std::clamp(
              15.0 + flare * 190.0 + (star ? 150.0 : 0.0), 0.0, 255.0)),
          static_cast<std::uint8_t>(std::clamp(
              28.0 + snapshot.progress * 55.0 + flare * 210.0 +
                  (star ? 210.0 : 0.0),
              0.0, 255.0)),
          255};
    }
  }
  return {};
}

}  // namespace apsis_drift
