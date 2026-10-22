#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/save_file.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace apsis_drift {
auto NativeFreedomFlightSession::prepare_composed_travel()
    -> std::expected<void, std::string> {
  const bool recovering = resources_ && resources_->drive;
  if (!knowledge_ || (!recovering && (knowledge_->recipe.version !=
                                          kFreedomPlanetaryKnowledgeVersion ||
                                      !document_.origin.network)))
    return std::unexpected{"Travel requires its explicitly selected chart"};
  const auto version =
      recovering
          ? (resources_->drive->version == kFreedomStrainedDriveRecoveryVersion
                 ? kFreedomStrainedTravelStateVersion
                 : kFreedomRecoveringTravelStateVersion)
          : kFreedomComposedTravelStateVersion;
  if (travel_) {
    if (travel_->version != version || !travel_->composed)
      return std::unexpected{
          "Historical travel cannot silently adopt a new policy"};
  } else {
    travel_.emplace();
    travel_->version = version;
    travel_->composed.emplace();
    if (recovering) {
      const auto relative = relative_state();
      if (!relative || !relative->frame.planet)
        return std::unexpected{"Travel source reference is unavailable"};
      document_.world = FreedomActiveWorldSelection{
          document_.origin.network ? 2U : 1U, document_.flight.frame.system,
          *relative->frame.planet};
      if (!document_.origin.network) {
        knowledge_->recipe.version = kFreedomTravelKnowledgeVersion;
        knowledge_->recipe.observation_policy = kFreedomLocalObservationPolicy;
        knowledge_->recipe.world_domain = 1;
      }
    }
  }
  return {};
}
auto NativeFreedomFlightSession::admit_composed_travel() const
    -> std::expected<void, std::string> {
  if (const auto document = travel_document(); !document)
    return std::unexpected{document.error()};
  const auto saved = save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (const auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  return {};
}
auto NativeFreedomFlightSession::planet_jump_selection_available(
    BodyTarget target) const -> std::expected<void, std::string> {
  if (recovery_pending() ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle))
    return std::unexpected{
        "Select planetary travel outside recovery or an active jump"};
  if (!knowledge_ ||
      knowledge_->recipe.version != kFreedomPlanetaryKnowledgeVersion ||
      !document_.origin.network || !resources_ || !starting_assembly_ ||
      document_.model.physical_catalog != 2 ||
      document_.model.physical_ephemeris != 2 ||
      document_.flight.craft !=
          CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion})
    return std::unexpected{
        "This voyage has no selected planetary navigation chart"};
  if (moon_ || minor_ || document_.moon || document_.minor)
    return std::unexpected{
        "Return to Origin home before selecting an ordinary planet"};
  if (travel_ && ((travel_->version != kFreedomComposedTravelStateVersion &&
                   travel_->version != kFreedomRecoveringTravelStateVersion &&
                   travel_->version != kFreedomStrainedTravelStateVersion) ||
                  !travel_->composed))
    return std::unexpected{
        "Historical travel cannot silently adopt a new policy"};
  if (!validate_freedom_resources(*resources_, document_) ||
      !validate_freedom_knowledge(*knowledge_, document_.flight.tick))
    return std::unexpected{
        "Current planetary knowledge or resources are invalid"};
  const auto relative = relative_state();
  if (!relative || !relative->frame.planet ||
      relative->frame.system != system_.catalog.id)
    return std::unexpected{"Current ordinary-planet owner is unavailable"};
  const BodyTarget current{system_.catalog.id,
                           body_id(*relative->frame.planet)};
  const auto rows = resolve_freedom_known_planets(*knowledge_, current,
                                                  document_.flight.tick);
  if (!rows) return std::unexpected{"Known-planet chart cannot be resolved"};
  for (const auto& row : *rows)
    if (row.target == target && !row.current) return {};
  return std::unexpected{
      "Select another known ordinary planet in the current system"};
}
auto NativeFreedomFlightSession::select_planet_jump(BodyTarget target)
    -> std::expected<void, std::string> {
  if (const auto available = planet_jump_selection_available(target);
      !available)
    return available;
  auto candidate = *this;
  if (const auto prepared = candidate.prepare_composed_travel(); !prepared)
    return prepared;
  candidate.travel_->composed->active = FreedomComposedRouteKind::planet;
  candidate.travel_->composed->selected_body = target;
  candidate.travel_->selected.reset();
  if (const auto admitted = candidate.admit_composed_travel(); !admitted)
    return admitted;
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::planet_jump_preview() const
    -> std::expected<FrozenFreedomPlanetJumpArrival, std::string> {
  if (!travel_ || !travel_->composed ||
      travel_->composed->active != FreedomComposedRouteKind::planet)
    return std::unexpected{"Select a known ordinary planet first"};
  const auto& route = *travel_->composed;
  if (travel_->phase == FreedomJumpPhase::transit && route.planet_committed)
    return *route.planet_committed;
  if (!route.selected_body)
    return std::unexpected{"Select a known ordinary planet first"};
  FreedomPlanetJumpRequest request{kFreedomPlanetJumpVersion, document_,
                                   *route.selected_body, travel_->next_attempt};
  if (resources_ && resources_->drive) {
    request.version = kFreedomRecoveryPlanetJumpVersion;
    request.recovery = resources_->drive;
  }
  const auto frozen = freeze_freedom_planet_jump_arrival(request);
  if (!frozen) {
    if (frozen.error() == FreedomPlanetJumpError::unclear_departure)
      return std::unexpected{"Departure volume is obstructed. Fly clear of "
                             "atmosphere, terrain and nearby bodies first."};
    if (frozen.error() == FreedomPlanetJumpError::unclear_arrival)
      return std::unexpected{"The planetary arrival volume is obstructed"};
    return std::unexpected{
        "Planetary source, destination or clearance is unavailable"};
  }
  return *frozen;
}
auto NativeFreedomFlightSession::select_composed_local_jump(BodyTarget target)
    -> std::expected<void, std::string> {
  if (recovery_pending() ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle))
    return std::unexpected{
        "Select local travel outside recovery or an active jump"};
  if (!resources_ || !knowledge_ || !starting_assembly_ || !boarding_ ||
      boarding_->phase != FreedomBoardingPhase::seated || actor_ ||
      surface_walker_ || (docking_ && docking_->attached) ||
      (surface_ && (surface_->landed || surface_->moon_landed)))
    return std::unexpected{
        "Local travel requires the occupied free-flight Wayfarer voyage"};
  const auto origin =
      generate_physical_origin_system(document_.origin.recipe.universe_seed, 2);
  if (!origin || *origin != system_)
    return std::unexpected{
        "Companion travel belongs to the Origin home planet"};
  const auto catalog = generate_secondary_body_catalog(*origin);
  if (!catalog) return std::unexpected{"Origin companion catalog unavailable"};
  const BodyTarget home{origin->catalog.id,
                        body_id(document_.origin.recipe.home_planet)};
  const BodyTarget moon{origin->catalog.id, catalog->bodies[0].node.id};
  const BodyTarget minor{origin->catalog.id, catalog->bodies[1].node.id};
  const bool returning = moon_ || minor_;
  const auto relative =
      returning ? std::expected<RigidBodyState, std::string>{document_.flight}
                : relative_state();
  if ((!returning &&
       (!relative ||
        relative->frame.planet != document_.origin.recipe.home_planet ||
        (target != moon && target != minor))) ||
      (returning && target != home))
    return std::unexpected{
        "Depart companions only from Origin home, or return home first"};
  auto candidate = *this;
  if (const auto prepared = candidate.prepare_composed_travel(); !prepared)
    return prepared;
  auto& composed = *candidate.travel_->composed;
  composed.active = (minor_ || target == minor)
                        ? FreedomComposedRouteKind::minor
                        : FreedomComposedRouteKind::moon;
  composed.selected_body = target;
  candidate.travel_->selected.reset();
  if (const auto preview = candidate.local_jump_preview(); !preview)
    return std::unexpected{preview.error()};
  if (const auto admitted = candidate.admit_composed_travel(); !admitted)
    return admitted;
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::composed_body_jump_available() const
    -> std::expected<void, std::string> {
  if (recovery_pending() || !travel_ || !travel_->composed ||
      travel_->composed->active == FreedomComposedRouteKind::system ||
      travel_->phase != FreedomJumpPhase::idle)
    return std::unexpected{"Select an idle typed body jump first"};
  if (!starting_assembly_ || !boarding_ ||
      boarding_->phase != FreedomBoardingPhase::seated || actor_ ||
      surface_walker_ || (docking_ && docking_->attached) ||
      (surface_ && (surface_->landed || surface_->moon_landed)))
    return std::unexpected{"Jump requires a seated pilot in free flight"};
  if (!resources_ || !resources_->jump_charges || !knowledge_ ||
      !validate_freedom_resources(*resources_, document_) ||
      !validate_freedom_knowledge(*knowledge_, document_.flight.tick))
    return std::unexpected{
        "Jump needs valid resources, knowledge and a jump charge"};
  if (travel_->next_attempt == std::numeric_limits<std::uint64_t>::max() ||
      document_.flight.tick > std::numeric_limits<SimulationTick>::max() -
                                  kJumpSpoolTicks - kJumpTransitTicks - 3)
    return std::unexpected{"Jump cannot retain an advanceable arrival"};
  if (!moon_ && !minor_) {
    const auto observed = observe();
    if (!observed || observed->atmosphere.altitude_metres <
                         observed->atmosphere.space_boundary_altitude_metres)
      return std::unexpected{"Reach space before jumping"};
  }
  if (travel_->composed->active == FreedomComposedRouteKind::planet) {
    if (const auto preview = planet_jump_preview(); !preview)
      return std::unexpected{preview.error()};
  } else if (const auto preview = local_jump_preview(); !preview) {
    return std::unexpected{preview.error()};
  }
  return {};
}
auto NativeFreedomFlightSession::begin_composed_body_jump()
    -> std::expected<void, std::string> {
  if (const auto available = composed_body_jump_available(); !available)
    return available;
  auto candidate = *this;
  if (!candidate.moon_ && !candidate.minor_ &&
      candidate.document_.flight.frame.kind ==
          RigidFrameKind::system_inertial) {
    const auto relative = candidate.relative_state();
    if (!relative) return std::unexpected{relative.error()};
    candidate.document_.flight = *relative;
  }
  if (candidate.knowledge_->orbital_surveys)
    candidate.knowledge_->orbital_surveys->active.reset();
  candidate.travel_->phase = FreedomJumpPhase::spool;
  candidate.travel_->spool_tick = candidate.document_.flight.tick;
  candidate.gear_clearance_.reset();
  candidate.cancel_port_approach();
  candidate.cancel_surface_maneuver();
  if (const auto admitted = candidate.admit_composed_travel(); !admitted)
    return admitted;
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::advance_composed_body_jump(
    const NativeFlightControls& controls, SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (!std::isfinite(step.count()) || step != kSimulationStep || !travel_ ||
      !travel_->composed ||
      travel_->composed->active == FreedomComposedRouteKind::system ||
      travel_->phase == FreedomJumpPhase::idle || !resources_)
    return std::unexpected{"Body travel requires an active fixed tick"};
  const VacuumIntent intent{
      controls.positive_translation, controls.negative_translation,
      controls.positive_rotation, controls.negative_rotation,
      document_.model.assistance};
  // Frozen transit still authenticates every input in the actual typed owner.
  if (minor_) {
    auto checked = document_.flight;
    if (!advance_minor_body_dynamics(RigidBodyWorldContext{*minor_}, checked,
                                     intent, {document_.minor->dynamics_recipe},
                                     step))
      return std::unexpected{"Invalid minor transit input"};
  } else if (moon_) {
    auto checked = document_.flight;
    if (!advance_moon_body_dynamics(RigidBodyWorldContext{*moon_}, checked,
                                    intent, {document_.moon->dynamics_recipe},
                                    step))
      return std::unexpected{"Invalid Moon transit input"};
  } else {
    const auto relative = relative_state();
    if (!relative ||
        !evaluate_atmospheric_flight(
            RigidBodyWorldContext{system_}, *relative, intent, rotation_,
            document_.model.atmosphere, document_.model.central))
      return std::unexpected{"Invalid planetary transit input"};
  }
  auto candidate = *this;
  NativeFlightStep result{};
  auto& state = *candidate.travel_;
  auto& composed = *state.composed;
  const bool planetary = composed.active == FreedomComposedRouteKind::planet;
  if (state.phase == FreedomJumpPhase::spool) {
    const auto tick = candidate.advance_craft_tick(controls, step);
    if (!tick) return std::unexpected{tick.error()};
    result = *tick;
    if (candidate.surface_ &&
        (candidate.surface_->landed || candidate.surface_->moon_landed)) {
      state.phase = FreedomJumpPhase::idle;
      state.spool_tick = 0;
    } else if (candidate.document_.flight.tick - state.spool_tick ==
               kJumpSpoolTicks) {
      const auto planet =
          planetary
              ? candidate.planet_jump_preview()
              : std::expected<FrozenFreedomPlanetJumpArrival, std::string>{
                    std::unexpected{"Not planetary"}};
      const auto local =
          !planetary ? candidate.local_jump_preview()
                     : std::expected<FrozenLocalBodyJumpArrival, std::string>{
                           std::unexpected{"Not local"}};
      bool space = true;
      if (!candidate.moon_ && !candidate.minor_) {
        const auto observed = candidate.observe();
        space =
            observed && observed->atmosphere.altitude_metres >=
                            observed->atmosphere.space_boundary_altitude_metres;
      }
      if ((!planet && !local) || !space ||
          !candidate.resources_->jump_charges) {
        state.phase = FreedomJumpPhase::idle;
        state.spool_tick = 0;
      } else {
        if (planetary) {
          composed.planet_committed = *planet;
          composed.planet_commitment_resources = *candidate.resources_;
        } else {
          auto& slot = composed.active == FreedomComposedRouteKind::moon
                           ? composed.moon
                           : composed.minor;
          slot.committed = *local;
          slot.commitment_resources = *candidate.resources_;
          if (!local->request.source.moon && !local->request.source.minor)
            slot.outbound = *local;
        }
        if (candidate.resources_->drive) {
          const auto kind = planetary ? FreedomJumpRecoveryKind::planet
                            : composed.active == FreedomComposedRouteKind::moon
                                ? FreedomJumpRecoveryKind::moon
                                : FreedomJumpRecoveryKind::minor;
          const auto checksum =
              planetary ? planet->source_checksum : local->source_checksum;
          const auto next_drive = advance_freedom_drive_recovery(
              *candidate.resources_->drive, kind, state.next_attempt, checksum,
              candidate.document_.flight.tick);
          if (!next_drive)
            return std::unexpected{"Recovery commitment clock refused"};
          candidate.resources_->drive = *next_drive;
        }
        --candidate.resources_->jump_charges;
        ++state.next_attempt;
        state.phase = FreedomJumpPhase::transit;
        candidate.docking_.reset();
        result.jump_committed = true;
      }
    }
  } else {
    const auto& slot = composed.active == FreedomComposedRouteKind::moon
                           ? composed.moon
                           : composed.minor;
    if ((planetary && !composed.planet_committed) ||
        (!planetary && !slot.committed))
      return std::unexpected{"Body transit has no frozen commitment"};
    const auto arrival_tick = planetary
                                  ? composed.planet_committed->arrival_tick
                                  : slot.committed->arrival_tick;
    const auto ledger =
        advance_freedom_resource_tick(*resources_, document_.flight.tick, 0);
    if (!ledger) return std::unexpected{"Body transit resource clock refused"};
    candidate.resources_ = *ledger;
    ++candidate.document_.flight.tick;
    candidate.document_.origin.state.tick = candidate.document_.flight.tick;
    if (candidate.condition_)
      candidate.condition_->tick = candidate.document_.flight.tick;
    if (candidate.document_.flight.tick == arrival_tick) {
      candidate.document_ = planetary ? composed.planet_committed->arrival
                                      : slot.committed->arrival;
      auto owner = hydrate_freedom_flight_document(candidate.document_);
      if (!owner) return std::unexpected{owner.error().detail};
      candidate.system_ = std::move(owner->system);
      candidate.rotation_ = owner->rotation;
      candidate.moon_ = owner->moon
                            ? std::optional{std::move(owner->moon->moon)}
                            : std::nullopt;
      candidate.minor_ = owner->minor
                             ? std::optional{std::move(owner->minor->minor)}
                             : std::nullopt;
      candidate.surface_ = candidate.minor_
                               ? std::optional<FreedomSurfaceState>{}
                               : std::optional{FreedomSurfaceState{}};
      candidate.gear_clearance_.reset();
      candidate.surface_walk_terrain_.reset();
      candidate.moon_walk_terrain_.reset();
      candidate.surface_landmark_field_.reset();
      state.phase = FreedomJumpPhase::idle;
      state.spool_tick = 0;
      composed.selected_body.reset();
      if (planetary) {
        if (const auto learned = candidate.refresh_knowledge(
                LocalObservationEvent::system_arrival);
            !learned)
          return std::unexpected{learned.error()};
      }
      result.jump_arrived = true;
    }
  }
  if (const auto admitted = candidate.admit_composed_travel(); !admitted)
    return std::unexpected{admitted.error()};
  *this = std::move(candidate);
  return result;
}
} // namespace apsis_drift
