#include "apsis_drift/moon_terrain_impact.hpp"
#include "apsis_drift/native_flight_session.hpp"

#include <utility>

namespace apsis_drift {
namespace {
auto compressible_gear_contact(const PhysicalMoonDescriptor& moon,
                               const RigidBodyState& before,
                               const RigidBodyState& candidate,
                               const MoonTerrainImpactWitness& witness,
                               std::uint32_t terrain) -> bool {
  if (witness.region != MoonCraftImpactRegion::landing_structure) return false;
  const auto snapshot =
      MoonContactSnapshot::create(moon, candidate, true, terrain);
  if (!snapshot) return false;
  const auto contact = snapshot->query();
  // Partial contact may use existing rated stroke; all other margins remain.
  if (!contact.assessment ||
      contact.assessment->failed_margins !=
          static_cast<std::uint32_t>(TouchdownMargin::separated_support) ||
      witness.support_index >= contact.support_count)
    return false;
  for (std::uint8_t index = 0; index < contact.support_count; ++index) {
    const auto& pad = contact.supports[index];
    if (!pad.geometry || !pad.geometry->has_value() || !pad.support ||
        !pad.support->has_value())
      return false;
  }
  const auto& struck = contact.supports[witness.support_index];
  if (!struck.triangle || struck.triangle->id != witness.triangle) return false;
  const auto hull = snapshot->hull_clearance();
  if (!hull || !hull->certified) return false;
  // The first gear contact must not conceal a later hull hit in this tick.
  const auto swept_hull =
      assess_moon_terrain_impact(moon, before, candidate, false, terrain);
  return swept_hull &&
         (swept_hull->classification == MoonTerrainImpactClass::clear ||
          swept_hull->classification == MoonTerrainImpactClass::near);
}
} // namespace
auto NativeFreedomFlightSession::advance_moon_tick(
    const NativeFlightControls& controls, SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (!moon_ || !document_.moon)
    return std::unexpected{"Moon flight requires its selected physical owner"};
  if ((docking_ && docking_->attached) || port_approach_.active)
    return std::unexpected{"Moon flight cannot own an Origin port attachment"};
  if (surface_ && surface_->moon_landed)
    return advance_moon_supported_tick(controls, step);
  const RigidBodyWorldContext context{*moon_};
  const VacuumIntent intent{
      controls.positive_translation, controls.negative_translation,
      controls.positive_rotation, controls.negative_rotation,
      document_.model.assistance};
  auto candidate = document_;
  auto actuation =
      advance_moon_body_dynamics(context, candidate.flight, intent,
                                 {document_.moon->dynamics_recipe}, step);
  if (!actuation)
    return std::unexpected{"Moon flight input or integration refused"};
  auto ledger = resources_;
  std::uint64_t debit{};
  bool refused{};
  if (ledger) {
    if (!validate_freedom_resources(*ledger, document_))
      return std::unexpected{"Moon resource owner or clock refused"};
    const auto bill = freedom_propulsion_tick_quanta(actuation->propulsion);
    if (!bill) return std::unexpected{"Moon propulsion bill refused"};
    debit = *bill;
    if (debit > ledger->flight_quanta) {
      candidate = document_;
      const VacuumIntent passive{{}, {}, {}, {}, false};
      actuation =
          advance_moon_body_dynamics(context, candidate.flight, passive,
                                     {document_.moon->dynamics_recipe}, step);
      if (!actuation)
        return std::unexpected{"Passive depleted moon flight refused"};
      debit = 0;
      refused = true;
    }
    const auto next =
        advance_freedom_resource_tick(*ledger, document_.flight.tick, debit);
    if (!next) return std::unexpected{"Moon resource transaction refused"};
    ledger = *next;
  }
  candidate.origin.state.tick = candidate.flight.tick;
  if (const auto valid = hydrate_freedom_moon_flight_document(candidate);
      !valid)
    return std::unexpected{"Moon candidate cannot be persisted: " +
                           valid.error().detail};
  NativeFlightStep result{};
  result.fuel_debit_quanta = debit;
  result.propulsion_refused = refused;
  result.moon = *actuation;
  // Planet observations/hold remain unavailable on this explicit owner. The
  // same gross propulsion and resource transaction above own the actual tick.
  auto next = *this;
  next.document_ = std::move(candidate);
  next.resources_ = ledger;
  if (next.condition_) next.condition_->tick = next.document_.flight.tick;
  next.try_moon_surface_contact();
  if (!next.surface_ || !next.surface_->moon_landed) {
    const auto sweep = assess_moon_terrain_impact(
        *moon_, document_.flight, next.document_.flight,
        surface_ && surface_->gear_deployed, document_.moon->terrain_recipe);
    if (!sweep) return std::unexpected{sweep.error()};
    if (sweep->classification == MoonTerrainImpactClass::unknown)
      return std::unexpected{
          "Moon terrain sweep unresolved; flight tick retained"};
    const bool compressing =
        sweep->witness && surface_ && surface_->gear_deployed &&
        (!condition_ || condition_allows_landing(*condition_)) &&
        compressible_gear_contact(*moon_, document_.flight,
                                  next.document_.flight, *sweep->witness,
                                  document_.moon->terrain_recipe);
    if (sweep->witness && sweep->witness->closing_speed_metres_per_second > 0 &&
        !compressing) {
      const auto response = respond_moon_terrain_impact(
          *moon_, document_.flight, next.document_.flight, *sweep->witness);
      if (!response) return std::unexpected{response.error()};
      next.document_.flight = *response;
      next.cancel_surface_maneuver();
      next.surface_maneuver_.note =
          "Moon terrain contact: inward motion stopped";
    }
  }
  *this = std::move(next);
  return result;
}
} // namespace apsis_drift
