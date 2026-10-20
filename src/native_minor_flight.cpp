#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"

#include <cmath>
#include <utility>

namespace apsis_drift {
auto NativeFreedomFlightSession::observe_minor() const
    -> std::expected<NativeFlightObservation, std::string> {
  if (!minor_ || !document_.minor)
    return std::unexpected{"Minor inspection owner unavailable"};
  const RigidBodyWorldContext context{*minor_};
  const auto gravity = evaluate_minor_body_gravity(context, document_.flight);
  const auto fixed = reframe_rigid_body(
      context, document_.flight,
      {{RigidFrameKind::body_fixed, system_.catalog.id, {}, {}, minor_->body},
       document_.flight.tick},
      *minor_);
  if (!gravity || !fixed) return std::unexpected{"Minor observation refused"};
  const auto rotation = resolve_physical_minor_rotation(
      *minor_, document_.flight.tick, fixed->position_metres);
  if (!rotation) return std::unexpected{"Minor lighting unavailable"};
  const auto p = document_.flight.position_metres;
  const auto v = document_.flight.linear_velocity_metres_per_second;
  const double distance = gravity->distance_metres;
  const double radial = ((p.x * v.x + p.y * v.y) + p.z * v.z) / distance;
  const RigidVector3 tangent{v.x - radial * p.x / distance,
                             v.y - radial * p.y / distance,
                             v.z - radial * p.z / distance};
  return NativeFlightObservation{
      {},
      {},
      {},
      NativeMinorFlightObservation{
          *gravity, *rotation, distance - minor_->radius_metres,
          std::hypot(v.x, v.y, v.z), radial,
          std::hypot(tangent.x, tangent.y, tangent.z)}};
}

auto NativeFreedomFlightSession::advance_minor_tick(
    const NativeFlightControls& controls, SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (!minor_ || !document_.minor || docking_ || actor_ || surface_walker_ ||
      surface_)
    return std::unexpected{
        "Minor inspection requires unconstrained owned flight"};
  const RigidBodyWorldContext context{*minor_};
  const VacuumIntent intent{
      controls.positive_translation, controls.negative_translation,
      controls.positive_rotation, controls.negative_rotation,
      document_.model.assistance};
  auto candidate = document_;
  auto actuation =
      advance_minor_body_dynamics(context, candidate.flight, intent,
                                  {document_.minor->dynamics_recipe}, step);
  if (!actuation)
    return std::unexpected{"Minor flight input/integration refused"};
  auto ledger = resources_;
  std::uint64_t debit{};
  bool refused{};
  if (ledger) {
    if (!validate_freedom_resources(*ledger, document_))
      return std::unexpected{"Minor resource owner/clock refused"};
    const auto bill = freedom_propulsion_tick_quanta(actuation->propulsion);
    if (!bill) return std::unexpected{"Minor propulsion bill refused"};
    debit = *bill;
    if (debit > ledger->flight_quanta) {
      candidate = document_;
      actuation = advance_minor_body_dynamics(
          context, candidate.flight, {{}, {}, {}, {}, false},
          {document_.minor->dynamics_recipe}, step);
      if (!actuation) return std::unexpected{"Passive minor flight refused"};
      debit = 0;
      refused = true;
    }
    const auto next =
        advance_freedom_resource_tick(*ledger, document_.flight.tick, debit);
    if (!next) return std::unexpected{"Minor resource clock refused"};
    ledger = *next;
  }
  const auto sweep =
      assess_minor_exclusion_sweep(*minor_, document_.flight, candidate.flight);
  if (!sweep || sweep->classification == MinorExclusionClass::unknown)
    return std::unexpected{
        "Minor stand-off geometry unresolved; tick retained"};
  const bool constrained =
      sweep->classification == MinorExclusionClass::intersects;
  if (constrained) {
    const auto response = respond_minor_exclusion_sweep(
        *minor_, document_.flight, candidate.flight);
    if (!response) return std::unexpected{"Minor stand-off response refused"};
    candidate.flight = *response;
  }
  candidate.origin.state.tick = candidate.flight.tick;
  if (const auto valid = hydrate_freedom_minor_flight_document(candidate);
      !valid)
    return std::unexpected{"Minor candidate cannot persist: " +
                           valid.error().detail};
  NativeFlightStep result{};
  result.fuel_debit_quanta = debit;
  result.propulsion_refused = refused;
  result.minor = *actuation;
  result.minor_standoff_limited = constrained;
  document_ = std::move(candidate);
  resources_ = ledger;
  if (condition_) condition_->tick = document_.flight.tick;
  surface_maneuver_.note =
      constrained ? "Minor stand-off boundary: inward motion stopped; surface "
                    "approach unsupported"
                  : "Minor inspection: stand-off flight; no landing";
  return result;
}
} // namespace apsis_drift
