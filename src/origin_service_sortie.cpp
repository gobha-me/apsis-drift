#include "apsis_drift/origin_service_sortie.hpp"
#include <limits>

namespace apsis_drift {
namespace {
using Phase = OriginServiceSortiePhase;
auto terminal(Phase p) -> bool {
  return p == Phase::completed || p == Phase::canceled;
}
auto transit(Phase p) -> bool {
  return p == Phase::outward_transit || p == Phase::return_transit;
}
auto lunar(const OriginServiceCraftState& s) -> bool {
  return s.body.frame.kind == RigidFrameKind::body_relative_inertial;
}
auto phase(OriginServiceSortie& s, Phase p) -> void {
  s.phase = p;
  s.phase_start_tick = s.service.body.tick;
  const auto duration =
      transit(p) ? kJumpTransitTicks
      : (p == Phase::outward_spool || p == Phase::return_spool)
          ? kOriginServiceSortieSpoolTicks
          : kOriginServiceSortiePhaseTicks;
  s.deadline_tick = terminal(p) ? 0 : s.phase_start_tick + duration;
}
auto same_target(const PhysicalMoonDescriptor& moon,
                 const OriginServiceSortie& s,
                 const OriginServiceSortieRecipient* r, SimulationTick tick)
    -> bool {
  if (!r || r->flight.origin.state.craft != s.beacon.target ||
      r->flight.origin.lineage != s.beacon.lineage ||
      r->anchor != s.beacon.anchor || r->flight.flight.tick != tick ||
      r->resources.tick != tick || !r->resources.jump_charges ||
      !validate_freedom_resources(r->resources, r->flight))
    return false;
  const auto owner = hydrate_freedom_moon_flight_document(r->flight);
  if (!owner || owner->moon != moon) return false;
  const auto actual = resolve_moon_landed_craft(moon, s.beacon.anchor, tick);
  return actual && *actual == r->flight.flight;
}
auto return_only(OriginServiceSortie& s) -> void {
  s.return_requested = true;
  if (s.phase == Phase::requested)
    phase(s, Phase::canceled);
  else if (s.phase == Phase::departing || s.phase == Phase::outward_spool)
    phase(s, Phase::returning);
  else if (s.phase == Phase::approaching || s.phase == Phase::delivering)
    phase(s, Phase::withdrawing);
  // An actual committed transit must finish before any return route is begun.
}
auto attached_tick(const PhysicalLocalSystem& system, OriginServiceSortie& s,
                   SimulationTick tick) -> std::expected<void, std::string> {
  const auto station =
      generate_origin_station(s.service.descriptor.universe_seed);
  const auto geometry = origin_station_geometry(station);
  if (!geometry) return std::unexpected("Service station geometry unavailable");
  const auto body =
      release_origin_port(system, station, *geometry,
                          {geometry->version, s.service.descriptor.home_port,
                           s.service.descriptor.frame, tick});
  const auto resources = advance_origin_service_resources(
      s.service.descriptor, s.service.resources, s.service.body.tick, 0);
  if (!body || !resources)
    return std::unexpected("Service attached tick unavailable");
  s.service.body = *body;
  s.service.resources = *resources;
  return {};
}
auto occupied_port(const PhysicalLocalSystem& system,
                   const OriginServiceSortie& s, SimulationTick tick,
                   std::span<const OriginDockConstraint> occupants)
    -> std::expected<bool, std::string> {
  if (occupants.size() > 2)
    return std::unexpected("Invalid station attachment roster");
  const auto station =
      generate_origin_station(s.service.descriptor.universe_seed);
  const auto geometry = origin_station_geometry(station);
  if (!geometry) return std::unexpected("Invalid station attachment owner");
  std::array<bool, 2> seen{};
  bool occupied{};
  for (const auto& c : occupants) {
    if (c.tick != tick || c.port.ordinal < 1 || c.port.ordinal > 2 ||
        seen[c.port.ordinal - 1] ||
        !resolve_origin_docked_pose(system, station, *geometry, c))
      return std::unexpected("Invalid same-clock station attachment");
    seen[c.port.ordinal - 1] = true;
    occupied |= c.port == s.service.descriptor.home_port;
  }
  return occupied;
}
auto commit(OriginServiceSortie& s, OriginServiceRouteArrival arrival, Phase p)
    -> std::expected<void, std::string> {
  const auto debit = spend_origin_service_jump_charge(
      s.service.descriptor, s.service.resources, s.service.body.tick);
  if (!debit) return std::unexpected("Service jump charge unavailable");
  s.commitment_resources = s.service.resources;
  s.pending = arrival;
  s.service.resources = *debit;
  phase(s, p);
  return {};
}
} // namespace
auto validate_origin_service_sortie(const PhysicalLocalSystem& system,
                                    const PhysicalMoonDescriptor& moon,
                                    const OriginServiceSortie& s)
    -> std::expected<void, std::string> {
  if (s.version != kOriginServiceSortieVersion ||
      static_cast<unsigned>(s.phase) > static_cast<unsigned>(Phase::canceled) ||
      s.start_tick > s.phase_start_tick ||
      s.phase_start_tick > s.service.body.tick ||
      (!terminal(s.phase) &&
       (s.phase_start_tick > std::numeric_limits<SimulationTick>::max() -
                                 kOriginServiceSortiePhaseTicks - 3 ||
        s.deadline_tick < s.service.body.tick)))
    return std::unexpected("Invalid service phase clock/version");
  const auto catalog = generate_secondary_body_catalog(system);
  if (!catalog || moon.catalog.owner != system ||
      moon.body != catalog->bodies[0].node.id)
    return std::unexpected(
        "Service sortie requires the genuine Origin home Moon");
  if (!validate_origin_service_craft(system, s.service,
                                     lunar(s.service) ? &moon : nullptr) ||
      !validate_origin_service_beacon(s.service.descriptor, moon, s.beacon,
                                      s.service.body.tick))
    return std::unexpected("Invalid service or immutable beacon owner");
  const auto duration =
      transit(s.phase) ? kJumpTransitTicks
      : (s.phase == Phase::outward_spool || s.phase == Phase::return_spool)
          ? kOriginServiceSortieSpoolTicks
          : kOriginServiceSortiePhaseTicks;
  if (s.return_requested &&
      (s.phase == Phase::requested || s.phase == Phase::departing ||
       s.phase == Phase::outward_spool || s.phase == Phase::approaching ||
       s.phase == Phase::delivering))
    return std::unexpected("Invalid return-only phase could continue delivery");
  if (s.deadline_tick !=
          (terminal(s.phase) ? 0 : s.phase_start_tick + duration) ||
      transit(s.phase) != s.pending.has_value() ||
      transit(s.phase) != s.commitment_resources.has_value() ||
      s.service.attached !=
          (s.phase == Phase::requested || terminal(s.phase)) ||
      (lunar(s.service) !=
       (s.phase == Phase::approaching || s.phase == Phase::delivering ||
        s.phase == Phase::withdrawing || s.phase == Phase::return_spool ||
        s.phase == Phase::return_transit)))
    return std::unexpected("Invalid service phase/pose ownership");
  if (s.service.delivery) {
    const auto anchor = rigid_body_state_checksum(RigidBodyWorldContext{moon},
                                                  s.beacon.anchor.fixed);
    if (!anchor || s.service.delivery->target_craft != s.beacon.target ||
        s.service.delivery->target_body !=
            BodyTarget{system.catalog.id, moon.body} ||
        s.service.delivery->target_anchor_checksum != *anchor)
      return std::unexpected("Service receipt names another beacon");
  }
  if (transit(s.phase)) {
    const auto& f = *s.pending;
    const auto& bill = *s.commitment_resources;
    if (f.source.tick != s.phase_start_tick ||
        f.arrival_tick != s.deadline_tick ||
        f.source.craft != s.service.body.craft ||
        f.craft != s.service.descriptor.id ||
        f.descriptor != s.service.descriptor || f.source.tick != bill.tick ||
        bill.jump_charges == 0 ||
        s.service.resources.jump_charges != bill.jump_charges - 1 ||
        s.service.resources.flight_quanta != bill.flight_quanta ||
        s.service.resources.cargo_quanta != bill.cargo_quanta ||
        s.service.resources.craft != bill.craft)
      return std::unexpected("Service committed bill/source mismatch");
    auto source = s.service;
    source.body = f.source;
    source.resources = bill;
    auto held = f.source;
    held.tick = s.service.body.tick;
    if (held != s.service.body)
      return std::unexpected("Service transit changed its held source");
    const auto expected =
        s.phase == Phase::outward_transit
            ? freeze_origin_service_moon_arrival(system, source, moon, s.beacon)
            : freeze_origin_service_home_arrival(system, source, moon);
    if (!expected || *expected != f)
      return std::unexpected("Service frozen arrival failed rederivation");
  }
  return {};
}
auto begin_origin_service_sortie(const PhysicalLocalSystem& system,
                                 const PhysicalMoonDescriptor& moon,
                                 const OriginServiceCraftState& service,
                                 const OriginServiceBeacon& beacon,
                                 const OriginServiceSortieRecipient& recipient)
    -> std::expected<OriginServiceSortie, std::string> {
  if (!service.attached || service.delivery ||
      service.resources.cargo_quanta !=
          service.descriptor.cargo_capacity_quanta ||
      service.resources.jump_charges < 2 ||
      service.resources.flight_quanta < 2 * kFreedomFlightReserveQuanta ||
      service.body.tick > std::numeric_limits<SimulationTick>::max() -
                              8 * kOriginServiceSortiePhaseTicks)
    return std::unexpected(
        "Service craft lacks the one-sortie reserve or original attachment");
  OriginServiceSortie s;
  s.service = service;
  s.beacon = beacon;
  s.start_tick = service.body.tick;
  phase(s, Phase::requested);
  if (const auto valid = validate_origin_service_sortie(system, moon, s);
      !valid)
    return std::unexpected(valid.error());
  if (!same_target(moon, s, &recipient, service.body.tick) ||
      recipient.resources.flight_quanta == kFreedomFlightCapacityQuanta)
    return std::unexpected("No supported recipient with finite tank room");
  return s;
}
auto cancel_origin_service_sortie(const PhysicalLocalSystem& system,
                                  const PhysicalMoonDescriptor& moon,
                                  const OriginServiceSortie& source)
    -> std::expected<OriginServiceSortie, std::string> {
  if (const auto valid = validate_origin_service_sortie(system, moon, source);
      !valid)
    return std::unexpected(valid.error());
  auto result = source;
  if (!terminal(result.phase)) return_only(result);
  return result;
}
auto advance_origin_service_sortie(
    const PhysicalLocalSystem& system, const PhysicalMoonDescriptor& moon,
    const OriginServiceSortie& source, SimulationTick world_tick,
    const OriginServiceSortieRecipient* recipient,
    std::span<const OriginDockConstraint> occupants)
    -> std::expected<OriginServiceSortieStep, std::string> {
  if (const auto valid = validate_origin_service_sortie(system, moon, source);
      !valid)
    return std::unexpected(valid.error());
  if (source.service.body.tick >=
          std::numeric_limits<SimulationTick>::max() - 1 ||
      world_tick != source.service.body.tick + 1)
    return std::unexpected("Service must share exactly the next world tick");
  const auto occupied = occupied_port(system, source, world_tick, occupants);
  if (!occupied) return std::unexpected(occupied.error());
  auto s = source;
  if (!terminal(s.phase) && !s.return_requested &&
      (!same_target(moon, s, recipient, world_tick) ||
       s.service.resources.flight_quanta < kFreedomFlightReserveQuanta))
    return_only(s);
  OriginServiceSortieStep result;
  if (terminal(s.phase)) {
    if (*occupied)
      return std::unexpected("Service attachment conflicts with occupied D2");
    if (const auto advanced = attached_tick(system, s, world_tick); !advanced)
      return std::unexpected(advanced.error());
    // Preserve the actual completion/cancel event; terminal deadline is0.
  } else if (transit(s.phase)) {
    const auto next = advance_origin_service_resources(
        s.service.descriptor, s.service.resources, s.service.body.tick, 0);
    if (!next) return std::unexpected("Service transit resource tick refused");
    s.service.resources = *next;
    ++s.service.body.tick;
    if (world_tick == s.pending->arrival_tick) {
      const bool outward = s.phase == Phase::outward_transit;
      s.service.body = s.pending->body;
      s.pending.reset();
      s.commitment_resources.reset();
      phase(s, outward ? (s.return_requested ? Phase::withdrawing
                                             : Phase::approaching)
                       : Phase::returning);
    }
  } else {
    if (s.phase == Phase::requested) {
      if (*occupied)
        return std::unexpected("Service cannot depart an occupied D2");
      s.service.attached = false;
      phase(s, Phase::departing);
    }
    const auto original_phase = s.phase;
    std::expected<OriginServiceRouteStep, std::string> moved =
        std::unexpected("Invalid service phase");
    if (s.phase == Phase::departing || s.phase == Phase::outward_spool)
      moved = advance_origin_service_home_route(system, s.service, 500);
    else if (s.phase == Phase::returning)
      moved = advance_origin_service_home_route(system, s.service,
                                                *occupied ? 500 : 0);
    else
      moved = advance_origin_service_moon_route(
          system, moon, s.beacon, s.service,
          (s.phase == Phase::approaching || s.phase == Phase::delivering)
              ? kOriginServiceHoverHeightMetres
              : kOriginServiceArrivalHeightMetres);
    if (!moved) return std::unexpected(moved.error());
    result.propulsion_quanta = moved->fuel_debit_quanta;
    result.propulsion = moved->propulsion;
    if (s.phase == Phase::departing) {
      // The freezer is the existing full-source withdrawal admission.
      if (freeze_origin_service_moon_arrival(system, s.service, moon, s.beacon))
        phase(s, Phase::outward_spool);
    } else if (s.phase == Phase::outward_spool &&
               world_tick == s.deadline_tick) {
      const auto arrival =
          freeze_origin_service_moon_arrival(system, s.service, moon, s.beacon);
      if (!arrival) return std::unexpected(arrival.error());
      if (const auto committed = commit(s, *arrival, Phase::outward_transit);
          !committed)
        return std::unexpected(committed.error());
    } else if (s.phase == Phase::approaching && moved->link_ready) {
      phase(s, Phase::delivering);
    } else if (s.phase == Phase::delivering) {
      if (!same_target(moon, s, recipient, world_tick))
        return_only(s);
      else if (moved->link_ready) {
        const auto paired =
            transfer_origin_service_fuel(moon, s.service, recipient->flight,
                                         s.beacon.anchor, recipient->resources);
        if (!paired) return std::unexpected("Service paired transfer refused");
        s.service = paired->service;
        result.transferred_quanta = paired->transferred_quanta;
        if (paired->transferred_quanta) result.receiver = paired->receiver;
        if (!s.service.resources.cargo_quanta ||
            paired->receiver.flight_quanta == kFreedomFlightCapacityQuanta)
          phase(s, Phase::withdrawing);
      }
    } else if (s.phase == Phase::withdrawing &&
               moved->target_distance_metres >=
                   kOriginServiceWithdrawalMetres) {
      phase(s, Phase::return_spool);
    } else if (s.phase == Phase::return_spool &&
               world_tick == s.deadline_tick) {
      const auto arrival =
          freeze_origin_service_home_arrival(system, s.service, moon);
      if (!arrival) return std::unexpected(arrival.error());
      if (const auto committed = commit(s, *arrival, Phase::return_transit);
          !committed)
        return std::unexpected(committed.error());
    } else if (s.phase == Phase::returning && !*occupied) {
      const auto station =
          generate_origin_station(s.service.descriptor.universe_seed);
      const auto geometry = origin_station_geometry(station);
      if (!geometry)
        return std::unexpected("Service return geometry unavailable");
      const auto assessment =
          assess_origin_dock(system, station, *geometry,
                             s.service.descriptor.home_port, s.service.body);
      if (!assessment)
        return std::unexpected("Service return capture assessment unavailable");
      if (assessment->decision == OriginDockDecision::capture_ready) {
        const auto constraint =
            capture_origin_port(system, station, *geometry,
                                s.service.descriptor.home_port, s.service.body);
        if (!constraint)
          return std::unexpected("Service mechanical capture refused");
        const auto attached =
            release_origin_port(system, station, *geometry, *constraint);
        if (!attached)
          return std::unexpected("Service constrained return unavailable");
        s.service.body = *attached;
        s.service.attached = true;
        phase(s, Phase::completed);
      }
    }
    if (s.phase == original_phase && world_tick >= s.deadline_tick)
      return std::unexpected(
          "Service phase exhausted its finite route deadline");
  }
  if (const auto valid = validate_origin_service_sortie(system, moon, s);
      !valid)
    return std::unexpected(valid.error());
  result.sortie = s;
  return result;
}
} // namespace apsis_drift
