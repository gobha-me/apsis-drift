#include "apsis_drift/freedom_resources.hpp"

#include <array>
#include <cmath>
#include <limits>

namespace apsis_drift {
auto validate_freedom_resources(const FreedomResources& s)
    -> std::expected<void, FreedomResourceError> {
  if (s.version != kFreedomResourceVersion ||
      s.frame != CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion} ||
      s.flight_quanta > kFreedomFlightCapacityQuanta ||
      s.jump_charges > kFreedomJumpCapacity)
    return std::unexpected{FreedomResourceError::invalid_state};
  return {};
}
auto validate_freedom_resources(const FreedomResources& s,
                                const FreedomFlightSaveDocument& d)
    -> std::expected<void, FreedomResourceError> {
  if (auto valid = validate_freedom_resources(s); !valid) return valid;
  if (s.craft != d.origin.state.craft || s.frame != d.flight.craft ||
      !validate_freedom_save_document(d.origin))
    return std::unexpected{FreedomResourceError::wrong_owner};
  if (s.tick != d.flight.tick || s.tick != d.origin.state.tick)
    return std::unexpected{FreedomResourceError::wrong_tick};
  return {};
}
auto freedom_propulsion_tick_quanta(const VacuumActuation& p)
    -> std::expected<std::uint64_t, FreedomResourceError> {
  const std::array values{
      p.positive_force_newtons.x,        p.positive_force_newtons.y,
      p.positive_force_newtons.z,        p.negative_force_newtons.x,
      p.negative_force_newtons.y,        p.negative_force_newtons.z,
      p.positive_torque_newton_metres.x, p.positive_torque_newton_metres.y,
      p.positive_torque_newton_metres.z, p.negative_torque_newton_metres.x,
      p.negative_torque_newton_metres.y, p.negative_torque_newton_metres.z};
  std::uint64_t force{}, torque{};
  for (std::size_t i = 0; i < values.size(); ++i) {
    const double v = values[i];
    if (!std::isfinite(v) || v < 0 || v > 1'000'000'000)
      return std::unexpected{FreedomResourceError::invalid_measurement};
    auto q = static_cast<std::uint64_t>(std::floor(v * 1000.0 + .5));
    if (v > 0 && q == 0) q = 1;
    // Six individually bounded channels per group: no sum/product overflow.
    if (i < 6)
      force += q;
    else
      torque += q;
  }
  return 13 * force + 2 * torque;
}
auto advance_freedom_resource_tick(const FreedomResources& s,
                                   SimulationTick source_tick,
                                   std::uint64_t debit)
    -> std::expected<FreedomResources, FreedomResourceError> {
  if (auto v = validate_freedom_resources(s); !v)
    return std::unexpected{v.error()};
  if (s.tick != source_tick)
    return std::unexpected{FreedomResourceError::wrong_tick};
  if (s.tick >= std::numeric_limits<SimulationTick>::max() - 1)
    return std::unexpected{FreedomResourceError::tick_overflow};
  if (debit > s.flight_quanta)
    return std::unexpected{FreedomResourceError::invalid_measurement};
  auto next = s;
  ++next.tick;
  next.flight_quanta -= debit;
  return next;
}
auto freedom_resource_reading(const FreedomResources& s, std::uint64_t debit,
                              bool refused)
    -> std::expected<FreedomResourceReading, FreedomResourceError> {
  if (auto v = validate_freedom_resources(s); !v)
    return std::unexpected{v.error()};
  if (debit > 90'000'000'000'000ULL)
    return std::unexpected{FreedomResourceError::invalid_measurement};
  return FreedomResourceReading{
      s.flight_quanta,
      kFreedomFlightCapacityQuanta,
      debit,
      s.jump_charges,
      static_cast<double>(s.flight_quanta) / kFreedomFlightCapacityQuanta,
      static_cast<double>(s.flight_quanta) /
          (kFreedomFullMainTickQuanta * kSimulationHz),
      static_cast<double>(debit) * kSimulationHz /
          kFreedomQuantaPerNewtonSecond,
      s.flight_quanta <= kFreedomFlightReserveQuanta,
      s.flight_quanta < 2,
      refused};
}
auto replenish_freedom_resources(const FreedomResources& s,
                                 const FreedomDockingSaveDocument& d)
    -> std::expected<FreedomResources, FreedomResourceError> {
  if (!validate_freedom_docking_document(d) || !d.docking.attached)
    return std::unexpected{FreedomResourceError::unsupported_service};
  if (auto valid = validate_freedom_resources(s, d.flight); !valid)
    return std::unexpected{valid.error()};
  auto next = s;
  next.flight_quanta = kFreedomFlightCapacityQuanta;
  next.jump_charges = kFreedomJumpCapacity;
  return next;
}
namespace {
auto validate_jump_owner(const IntersystemContractState& c,
                         const FreedomResources& s)
    -> std::expected<void, FreedomResourceError> {
  if (!validate_intersystem_contract_state(c) || !validate_freedom_resources(s))
    return std::unexpected{FreedomResourceError::invalid_state};
  if (s.craft !=
      make_freedom_new_game_document(c.identities.universe_seed).state.craft)
    return std::unexpected{FreedomResourceError::wrong_owner};
  if (s.tick != c.universe_tick)
    return std::unexpected{FreedomResourceError::wrong_tick};
  return {};
}
} // namespace
auto begin_freedom_resource_jump(IntersystemContractState& c,
                                 const FreedomResources& s)
    -> std::expected<void, FreedomResourceError> {
  if (auto v = validate_jump_owner(c, s); !v) return v;
  if (!s.jump_charges)
    return std::unexpected{FreedomResourceError::unavailable_charge};
  auto candidate = c;
  if (!begin_intersystem_jump(candidate))
    return std::unexpected{FreedomResourceError::jump_refused};
  c = candidate;
  return {};
}
auto advance_freedom_resource_jump_tick(
    IntersystemContractState& c, FreedomResources& s,
    const LocalSystemDescriptor& destination,
    std::span<const FlightCommand> commands)
    -> std::expected<IntersystemJumpAdvance, FreedomResourceError> {
  if (auto v = validate_jump_owner(c, s); !v) return std::unexpected{v.error()};
  const bool spooling =
      c.travel_phase == IntersystemTravelPhase::outbound_jump_spooling ||
      c.travel_phase == IntersystemTravelPhase::return_jump_spooling;
  if (spooling && !s.jump_charges)
    return std::unexpected{FreedomResourceError::unavailable_charge};
  auto contract = c;
  auto ledger = s;
  auto step = advance_intersystem_jump_tick(contract, destination, commands);
  if (!step) return std::unexpected{FreedomResourceError::jump_refused};
  if (step->committed) --ledger.jump_charges;
  ledger.tick = contract.universe_tick;
  c = contract;
  s = ledger;
  return *step;
}
} // namespace apsis_drift
