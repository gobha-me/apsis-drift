#include "apsis_drift/freedom_derelict_recovery.hpp"

#include <cmath>
#include <limits>

namespace apsis_drift {
namespace {
auto observation_shape(const FreedomDerelictObservation& observation) -> bool {
  if (!observation.craft.value ||
      observation.tick == std::numeric_limits<SimulationTick>::max())
    return false;
  for (double value :
       {observation.position_metres.x, observation.position_metres.y,
        observation.position_metres.z})
    if (!std::isfinite(value) ||
        std::abs(value) > kRigidBodyMaximumPositionMetres)
      return false;
  for (double value : {observation.velocity_metres_per_second.x,
                       observation.velocity_metres_per_second.y,
                       observation.velocity_metres_per_second.z})
    if (!std::isfinite(value) ||
        std::abs(value) > kRigidBodyMaximumVelocityMetresPerSecond)
      return false;
  return true;
}
auto separation(RigidVector3 a, RigidVector3 b) -> double {
  return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}
auto eligible_observation(Seed seed, const FreedomDerelictObservation& observer)
    -> std::expected<void, std::string> {
  const auto assessment = assess_freedom_derelict_retrieval(seed, observer);
  if (!assessment) return std::unexpected{assessment.error()};
  if (!assessment->eligible)
    return std::unexpected{
        "Retrieval observation is outside the matched-speed envelope"};
  return {};
}
} // namespace

auto assess_freedom_derelict_retrieval(
    Seed seed, const FreedomDerelictObservation& observer)
    -> std::expected<FreedomDerelictRetrievalAssessment, std::string> {
  if (!observation_shape(observer))
    return std::unexpected{
        "Invalid retrieval observation identity, tick or geometry"};
  const auto descriptor = generate_freedom_derelict(seed);
  if (!descriptor)
    return std::unexpected{"Derelict recovery source is unavailable"};
  const auto hull = resolve_freedom_derelict(*descriptor, observer.tick);
  if (!hull) return std::unexpected{"Derelict recovery pose is unavailable"};
  const double distance =
      separation(observer.position_metres, hull->position_metres);
  const double speed = separation(observer.velocity_metres_per_second,
                                  hull->linear_velocity_metres_per_second);
  if (!std::isfinite(distance) || !std::isfinite(speed))
    return std::unexpected{"Unsafe retrieval separation arithmetic"};
  return FreedomDerelictRetrievalAssessment{
      distance >= kFreedomDerelictRetrievalMinimumMetres &&
          distance <= kFreedomDerelictRetrievalMaximumMetres &&
          speed <= kFreedomDerelictRetrievalMaximumSpeedMetresPerSecond,
      distance, speed};
}

auto validate_freedom_derelict_recovery(
    Seed seed, const FreedomDerelictRecoveryState& state,
    const FreedomDerelictState& inspection, SimulationTick now)
    -> std::expected<void, std::string> {
  if (state.version != kFreedomDerelictRecoveryVersion ||
      now == std::numeric_limits<SimulationTick>::max() ||
      (state.active && state.canister))
    return std::unexpected{
        "Invalid derelict recovery policy, clock or exclusive slot"};
  if (!validate_freedom_derelict_state(seed, inspection, now))
    return std::unexpected{"Invalid derelict contact or inspection provenance"};
  if ((state.active || state.canister) && !inspection.inspection)
    return std::unexpected{
        "Derelict recovery requires an earned exterior inspection"};
  if (state.active) {
    const auto& active = *state.active;
    if (active.started_tick > active.last_tick || active.last_tick > now ||
        active.observer.tick != active.last_tick ||
        active.acquired_ticks >= kFreedomDerelictRetrievalTicks ||
        active.last_tick - active.started_tick != active.acquired_ticks ||
        inspection.inspection->tick > active.started_tick)
      return std::unexpected{
          "Invalid consecutive retrieval interval or historical clock"};
    if (auto result = eligible_observation(seed, active.observer); !result)
      return result;
  }
  if (state.canister) {
    const auto& item = *state.canister;
    if (item.started_tick > item.completed.tick || item.completed.tick > now ||
        item.completed.tick - item.started_tick !=
            kFreedomDerelictRetrievalTicks ||
        inspection.inspection->tick > item.started_tick ||
        (item.disposition != FreedomCanisterDisposition::carried &&
         item.disposition != FreedomCanisterDisposition::lost &&
         item.disposition != FreedomCanisterDisposition::sold) ||
        item.sold_tick.has_value() !=
            (item.disposition == FreedomCanisterDisposition::sold) ||
        (item.sold_tick &&
         (*item.sold_tick < item.completed.tick || *item.sold_tick > now)))
      return std::unexpected{
          "Invalid permanent canister claim or sale receipt"};
    if (auto result = eligible_observation(seed, item.completed); !result)
      return result;
  }
  return {};
}

auto begin_freedom_derelict_retrieval(
    Seed seed, const FreedomDerelictRecoveryState& state,
    const FreedomDerelictState& inspection,
    const FreedomDerelictObservation& observer)
    -> std::expected<FreedomDerelictRecoveryState, std::string> {
  if (auto result = validate_freedom_derelict_recovery(seed, state, inspection,
                                                       observer.tick);
      !result)
    return std::unexpected{result.error()};
  if (state.active || state.canister || !inspection.inspection ||
      inspection.inspection->tick > observer.tick ||
      observer.tick > std::numeric_limits<SimulationTick>::max() - 1 -
                          kFreedomDerelictRetrievalTicks)
    return std::unexpected{
        "Retrieval requires an inspected unclaimed source and a usable clock"};
  if (auto result = eligible_observation(seed, observer); !result)
    return std::unexpected{result.error()};
  auto candidate = state;
  candidate.active =
      FreedomDerelictRetrieval{observer.tick, observer.tick, 0, observer};
  return candidate;
}

auto advance_freedom_derelict_retrieval(
    Seed seed, const FreedomDerelictRecoveryState& state,
    const FreedomDerelictState& inspection,
    const FreedomDerelictObservation& observer)
    -> std::expected<FreedomDerelictRecoveryState, std::string> {
  const auto assessment = assess_freedom_derelict_retrieval(seed, observer);
  if (!assessment) return std::unexpected{assessment.error()};
  if (auto result = validate_freedom_derelict_recovery(seed, state, inspection,
                                                       observer.tick);
      !result)
    return std::unexpected{result.error()};
  if (!state.active || observer.tick <= state.active->last_tick)
    return std::unexpected{
        "Retrieval needs an active operation and a strictly later actual tick"};
  const auto& previous = *state.active;
  if (observer.tick - previous.last_tick != 1 ||
      observer.craft != previous.observer.craft || !assessment->eligible)
    return cancel_freedom_derelict_retrieval(state);
  auto candidate = state;
  const auto count = previous.acquired_ticks + 1;
  if (count == kFreedomDerelictRetrievalTicks) {
    candidate.active.reset();
    candidate.canister =
        FreedomRecoveredCanister{previous.started_tick, observer};
  } else {
    candidate.active = FreedomDerelictRetrieval{previous.started_tick,
                                                observer.tick, count, observer};
  }
  return candidate;
}

auto cancel_freedom_derelict_retrieval(
    const FreedomDerelictRecoveryState& state) noexcept
    -> FreedomDerelictRecoveryState {
  auto candidate = state;
  candidate.active.reset();
  return candidate;
}
auto lose_freedom_derelict_canister(
    const FreedomDerelictRecoveryState& state) noexcept
    -> FreedomDerelictRecoveryState {
  auto candidate = cancel_freedom_derelict_retrieval(state);
  if (candidate.canister &&
      candidate.canister->disposition == FreedomCanisterDisposition::carried)
    candidate.canister->disposition = FreedomCanisterDisposition::lost;
  return candidate;
}
auto sell_freedom_derelict_canister(Seed seed,
                                    const FreedomDerelictRecoveryState& state,
                                    const FreedomDerelictState& inspection,
                                    SimulationTick now)
    -> std::expected<FreedomDerelictRecoveryState, std::string> {
  if (auto result =
          validate_freedom_derelict_recovery(seed, state, inspection, now);
      !result)
    return std::unexpected{result.error()};
  if (!state.canister ||
      state.canister->disposition != FreedomCanisterDisposition::carried)
    return std::unexpected{"No recovered physical canister is carried"};
  auto candidate = state;
  candidate.canister->disposition = FreedomCanisterDisposition::sold;
  candidate.canister->sold_tick = now;
  return candidate;
}
} // namespace apsis_drift
