#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/save_file.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace apsis_drift {
auto NativeFreedomFlightSession::select_local_jump(BodyTarget target)
    -> std::expected<void, std::string> {
  const auto origin_id =
      generate_first_universe_route(document_.origin.recipe.universe_seed)
          .origin;
  if (document_.minor &&
      document_.minor->version == kFreedomForeignMinorWorldVersion)
    return select_foreign_minor_jump(target);
  if (system_.catalog.id != origin_id && !moon_ && !minor_ &&
      target.body.kind == BodyKind::minor)
    return select_foreign_minor_jump(target);
  if ((document_.moon &&
       document_.moon->version == kFreedomForeignMoonWorldVersion) ||
      system_.catalog.id != origin_id)
    return select_foreign_moon_jump(target);
  if ((travel_ &&
       (travel_->version == kFreedomForeignMoonTravelStateVersion ||
        travel_->version == kFreedomForeignMinorTravelStateVersion)) ||
      (resources_ && resources_->drive) ||
      (knowledge_ &&
       freedom_knowledge_has_planetary_chart(knowledge_->recipe.version)))
    return select_composed_local_jump(target);
  if (recovery_pending() ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle))
    return std::unexpected{
        "Select local travel outside recovery or an active jump"};
  if (!resources_ || !knowledge_ || !starting_assembly_ || !boarding_ ||
      boarding_->phase != FreedomBoardingPhase::seated || actor_ ||
      surface_walker_ || (docking_ && docking_->attached) ||
      (surface_ && (surface_->landed || surface_->moon_landed)) ||
      document_.model.physical_catalog != 2 ||
      document_.model.physical_ephemeris != 2)
    return std::unexpected{
        "Local travel requires the occupied free-flight Wayfarer voyage"};
  const auto origin =
      generate_physical_origin_system(document_.origin.recipe.universe_seed, 2);
  if (!origin || origin->catalog.id != system_.catalog.id)
    return std::unexpected{
        "The first local route belongs to the origin system"};
  auto candidate = *this;
  if (!candidate.travel_) candidate.travel_ = FreedomTravelState{};
  const auto catalog = generate_secondary_body_catalog(*origin);
  if (!catalog) return std::unexpected{"Local catalog unavailable"};
  const auto moon_target =
      BodyTarget{origin->catalog.id, catalog->bodies[0].node.id};
  const auto minor_target =
      BodyTarget{origin->catalog.id, catalog->bodies[1].node.id};
  const auto home_target = BodyTarget{
      origin->catalog.id, body_id(document_.origin.recipe.home_planet)};
  const bool at_home = !moon_ && !minor_;
  if ((at_home && target != moon_target && target != minor_target) ||
      (!at_home && target != home_target))
    return std::unexpected{
        "Select a local destination from home, or return home first"};
  if (at_home && target == moon_target &&
      candidate.travel_->version == kFreedomMinorTravelStateVersion) {
    if (!candidate.travel_->local ||
        candidate.travel_->local->committed.has_value() !=
            candidate.travel_->local->outbound.has_value() ||
        (candidate.travel_->local->committed &&
         (candidate.travel_->local->committed->arrival.moon ||
          candidate.travel_->local->committed->arrival.minor)))
      return std::unexpected{
          "Complete the minor route home before reusing origin routes"};
    FreedomOriginLocalRoutes routes;
    routes.legacy = {candidate.travel_->version, *candidate.travel_->local,
                     candidate.travel_->prior_moon,
                     candidate.travel_->next_attempt};
    candidate.travel_->origin_routes = std::move(routes);
    candidate.travel_->local = FreedomLocalTravelState{};
    candidate.travel_->prior_moon.reset();
    candidate.travel_->version = kFreedomOriginLocalTravelStateVersion;
  }
  if (candidate.travel_->version == kFreedomOriginLocalTravelStateVersion) {
    if (!candidate.travel_->origin_routes || !candidate.travel_->local)
      return std::unexpected{"Origin route checkpoints unavailable"};
    auto& routes = *candidate.travel_->origin_routes;
    if (at_home) {
      const auto requested = target == moon_target
                                 ? FreedomLocalRouteKind::moon
                                 : FreedomLocalRouteKind::minor;
      if (requested != routes.active) {
        // A selection is not a retained journey proof. Both checkpoints are
        // idle at home, and switching never grants a direct body-to-body leg.
        candidate.travel_->local->selected.reset();
        FreedomLocalRouteCheckpoint previous{
            std::move(*candidate.travel_->local),
            std::move(routes.excursion_outbound)};
        candidate.travel_->local = std::move(routes.inactive.local);
        routes.excursion_outbound =
            std::move(routes.inactive.excursion_outbound);
        routes.inactive = std::move(previous);
        routes.active = requested;
      }
    } else if ((moon_ && routes.active != FreedomLocalRouteKind::moon) ||
               (minor_ && routes.active != FreedomLocalRouteKind::minor)) {
      return std::unexpected{"The active route does not own this departure"};
    }
  } else {
    const bool minor_route =
        minor_.has_value() ||
        target == BodyTarget{origin->catalog.id, catalog->bodies[1].node.id};
    if (minor_route && moon_)
      return std::unexpected{"Return home before selecting minor inspection"};
    if (!minor_route &&
        candidate.travel_->version == kFreedomMinorTravelStateVersion)
      return std::unexpected{
          "Minor inspection retains its selected local history"};
    if (minor_route &&
        candidate.travel_->version != kFreedomMinorTravelStateVersion) {
      if (candidate.travel_->local && candidate.travel_->local->outbound) {
        if (!candidate.travel_->local->committed ||
            candidate.travel_->local->committed->arrival.moon ||
            candidate.travel_->local->committed->arrival.minor)
          return std::unexpected{"Complete the old Moon route home before "
                                 "adopting minor inspection"};
        candidate.travel_->prior_moon =
            FreedomLocalMoonHistory{*candidate.travel_->local};
        candidate.travel_->prior_moon->local.selected.reset();
      }
      candidate.travel_->local = FreedomLocalTravelState{};
    }
    if (!candidate.travel_->local)
      candidate.travel_->local = FreedomLocalTravelState{};
    candidate.travel_->version = minor_route ? kFreedomMinorTravelStateVersion
                                             : kFreedomLocalTravelStateVersion;
  }
  candidate.travel_->selected.reset();
  candidate.travel_->local->selected = target;
  if (!candidate.document_.moon && !candidate.document_.minor &&
      !candidate.document_.world) {
    const auto relative = candidate.relative_state();
    if (!relative || !relative->frame.planet)
      return std::unexpected{"Local departure reference unavailable"};
    candidate.document_.world = FreedomActiveWorldSelection{
        candidate.document_.origin.network ? 2U : 1U, system_.catalog.id,
        *relative->frame.planet};
  }
  if (!candidate.document_.origin.network) {
    candidate.knowledge_->recipe.version = kFreedomTravelKnowledgeVersion;
    candidate.knowledge_->recipe.observation_policy =
        kFreedomLocalObservationPolicy;
    candidate.knowledge_->recipe.world_domain = 1;
  }
  if (auto preview = candidate.local_jump_preview(); !preview)
    return std::unexpected{preview.error()};
  if (auto saved = candidate.travel_document(); !saved)
    return std::unexpected{saved.error()};
  // Adoption and switching add bounded history. Qualify the complete outer
  // save's encoding limit before publishing any selected/checkpoint state.
  const auto saved = candidate.player_save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (const auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::local_jump_preview() const
    -> std::expected<FrozenLocalBodyJumpArrival, std::string> {
  if (travel_ && travel_->composed) {
    const auto& route = *travel_->composed;
    if (route.active != FreedomComposedRouteKind::moon &&
        route.active != FreedomComposedRouteKind::minor &&
        route.active != FreedomComposedRouteKind::foreign_moon &&
        route.active != FreedomComposedRouteKind::foreign_minor)
      return std::unexpected{"Select a companion route first"};
    const auto& slot =
        route.active == FreedomComposedRouteKind::moon    ? route.moon
        : route.active == FreedomComposedRouteKind::minor ? route.minor
        : route.active == FreedomComposedRouteKind::foreign_moon
            ? route.foreign_moon
            : route.foreign_minor;
    if (travel_->phase == FreedomJumpPhase::transit && slot.committed)
      return *slot.committed;
    if (!route.selected_body)
      return std::unexpected{"Select the companion or home body first"};
    auto model = document_.model;
    if (moon_ || minor_) {
      if (!slot.outbound)
        return std::unexpected{
            "The current excursion has no authenticated home departure"};
      model = slot.outbound->request.home_model;
    }
    const bool foreign_moon =
        route.active == FreedomComposedRouteKind::foreign_moon;
    const bool foreign_minor =
        route.active == FreedomComposedRouteKind::foreign_minor;
    if ((foreign_moon || foreign_minor) && !moon_ && !minor_)
      model.hold = OrbitHoldRequest{};
    const auto policy = foreign_minor  ? kFreedomForeignMinorJumpVersion
                        : foreign_moon ? kFreedomForeignMoonJumpVersion
                        : route.active == FreedomComposedRouteKind::moon
                            ? kFreedomLocalBodyJumpVersion
                            : kFreedomMinorLocalBodyJumpVersion;
    FreedomLocalBodyJumpRequest request{policy, document_, *route.selected_body,
                                        travel_->next_attempt, model};
    if (resources_ && resources_->drive) {
      request.version = foreign_minor  ? kFreedomRecoveryForeignMinorJumpVersion
                        : foreign_moon ? kFreedomRecoveryForeignMoonJumpVersion
                        : route.active == FreedomComposedRouteKind::moon
                            ? kFreedomRecoveryLocalBodyJumpVersion
                            : kFreedomRecoveryMinorLocalBodyJumpVersion;
      request.recovery = resources_->drive;
    }
    const auto result = freeze_local_body_jump_arrival(request);
    if (!result)
      return std::unexpected{
          "Companion source, destination or full clearance is unavailable"};
    return *result;
  }
  if (!travel_ || !travel_->local || !travel_->local->selected)
    return std::unexpected{"Select the moon or home body first"};
  const auto& local = *travel_->local;
  if (travel_->phase == FreedomJumpPhase::transit && local.committed)
    return *local.committed;
  auto home_model =
      local.outbound ? local.outbound->request.home_model : document_.model;
  auto policy = travel_->version == kFreedomMinorTravelStateVersion
                    ? kFreedomMinorLocalBodyJumpVersion
                    : kFreedomLocalBodyJumpVersion;
  if (travel_->version == kFreedomOriginLocalTravelStateVersion) {
    if (!travel_->origin_routes)
      return std::unexpected{"Origin route checkpoint unavailable"};
    const auto& routes = *travel_->origin_routes;
    policy = routes.active == FreedomLocalRouteKind::minor
                 ? kFreedomMinorLocalBodyJumpVersion
                 : kFreedomLocalBodyJumpVersion;
    if (moon_ || minor_) {
      if (!routes.excursion_outbound)
        return std::unexpected{"The current excursion has no home departure"};
      home_model = routes.excursion_outbound->request.home_model;
    } else {
      home_model = document_.model;
    }
  }
  const auto result = freeze_local_body_jump_arrival(
      {policy, document_, *local.selected, travel_->next_attempt, home_model});
  if (!result) {
    if (result.error() == FreedomLocalBodyJumpError::unclear_departure)
      return std::unexpected{
          "Departure volume is obstructed. Fly clear of nearby bodies and "
          "terrain before retrying this jump."};
    if (result.error() == FreedomLocalBodyJumpError::unclear_arrival)
      return std::unexpected{"Destination arrival volume is obstructed. This "
                             "local jump is unavailable."};
    return std::unexpected{"Local jump source, destination or clearance is "
                           "outside the supported route"};
  }
  return *result;
}
auto NativeFreedomFlightSession::local_jump_available() const
    -> std::expected<void, std::string> {
  if (travel_ && travel_->composed) return composed_body_jump_available();
  if (recovery_pending() || !travel_ || !travel_->local ||
      travel_->phase != FreedomJumpPhase::idle)
    return std::unexpected{"Select an idle local jump first"};
  if (!starting_assembly_ || !boarding_ ||
      boarding_->phase != FreedomBoardingPhase::seated || actor_ ||
      surface_walker_ || (docking_ && docking_->attached) ||
      (surface_ && (surface_->landed || surface_->moon_landed)))
    return std::unexpected{"Local jump requires a seated pilot in free flight"};
  if (!resources_ || !resources_->jump_charges || !knowledge_ ||
      !validate_freedom_resources(*resources_, document_) ||
      !validate_freedom_knowledge(*knowledge_, document_.flight.tick))
    return std::unexpected{
        "Local jump needs valid resources, knowledge and a jump charge"};
  if (travel_->next_attempt == std::numeric_limits<std::uint64_t>::max() ||
      document_.flight.tick > std::numeric_limits<SimulationTick>::max() -
                                  kJumpSpoolTicks - kJumpTransitTicks - 3)
    return std::unexpected{"Local jump cannot retain an advanceable arrival"};
  if (!moon_ && !minor_) {
    const auto observed = observe();
    if (!observed || observed->atmosphere.altitude_metres <
                         observed->atmosphere.space_boundary_altitude_metres)
      return std::unexpected{"Reach space before the local jump"};
  }
  if (const auto preview = local_jump_preview(); !preview)
    return std::unexpected{preview.error()};
  return {};
}
auto NativeFreedomFlightSession::begin_local_jump()
    -> std::expected<void, std::string> {
  if (travel_ && travel_->composed) return begin_composed_body_jump();
  if (const auto available = local_jump_available(); !available)
    return available;
  auto candidate = *this;
  if (candidate.knowledge_->orbital_surveys)
    candidate.knowledge_->orbital_surveys->active.reset();
  candidate.travel_->phase = FreedomJumpPhase::spool;
  candidate.travel_->spool_tick = document_.flight.tick;
  candidate.gear_clearance_.reset();
  candidate.cancel_port_approach();
  candidate.cancel_surface_maneuver();
  if (const auto saved = candidate.travel_document(); !saved)
    return std::unexpected{saved.error()};
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::advance_local_jump(
    const NativeFlightControls& controls, SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (travel_ && travel_->composed)
    return advance_composed_body_jump(controls, step);
  if (!std::isfinite(step.count()) || step != kSimulationStep || !travel_ ||
      !travel_->local || travel_->phase == FreedomJumpPhase::idle ||
      !resources_)
    return std::unexpected{"Local travel requires an active fixed tick"};
  const VacuumIntent intent{
      controls.positive_translation, controls.negative_translation,
      controls.positive_rotation, controls.negative_rotation,
      document_.model.assistance};
  // Validate input in the actual physical owner, including during frozen
  // transit.
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
      return std::unexpected{"Invalid home transit input"};
  }
  auto candidate = *this;
  NativeFlightStep result{};
  if (travel_->phase == FreedomJumpPhase::spool) {
    const auto tick = candidate.advance_craft_tick(controls, step);
    if (!tick) return std::unexpected{tick.error()};
    result = *tick;
    if (candidate.surface_ &&
        (candidate.surface_->landed || candidate.surface_->moon_landed)) {
      candidate.travel_->phase = FreedomJumpPhase::idle;
      candidate.travel_->spool_tick = 0;
    } else if (candidate.document_.flight.tick -
                   candidate.travel_->spool_tick ==
               kJumpSpoolTicks) {
      const auto frozen = candidate.local_jump_preview();
      bool atmosphere_clear = true;
      if (!candidate.moon_ && !candidate.minor_) {
        const auto observed = candidate.observe();
        atmosphere_clear =
            observed && observed->atmosphere.altitude_metres >=
                            observed->atmosphere.space_boundary_altitude_metres;
      }
      if (!frozen || !atmosphere_clear || !candidate.resources_->jump_charges) {
        candidate.travel_->phase = FreedomJumpPhase::idle;
        candidate.travel_->spool_tick = 0;
      } else {
        auto& local = *candidate.travel_->local;
        local.committed = *frozen;
        local.commitment_resources = *candidate.resources_;
        if (!frozen->request.source.moon && !frozen->request.source.minor) {
          if (!local.outbound) local.outbound = *frozen;
          if (candidate.travel_->origin_routes)
            candidate.travel_->origin_routes->excursion_outbound = *frozen;
        }
        --candidate.resources_->jump_charges;
        ++candidate.travel_->next_attempt;
        candidate.travel_->phase = FreedomJumpPhase::transit;
        candidate.docking_.reset();
        result.jump_committed = true;
        if (candidate.knowledge_ && candidate.knowledge_->star_search)
          candidate.knowledge_->star_search->active.reset();
      }
    }
  } else {
    if (!travel_->local->committed)
      return std::unexpected{"Local transit has no frozen commitment"};
    const auto& frozen = *travel_->local->committed;
    const auto ledger =
        advance_freedom_resource_tick(*resources_, document_.flight.tick, 0);
    if (!ledger) return std::unexpected{"Local transit resource clock refused"};
    candidate.resources_ = *ledger;
    ++candidate.document_.flight.tick;
    candidate.document_.origin.state.tick = candidate.document_.flight.tick;
    if (candidate.condition_)
      candidate.condition_->tick = candidate.document_.flight.tick;
    if (candidate.document_.flight.tick == frozen.arrival_tick) {
      candidate.document_ = frozen.arrival;
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
      candidate.travel_->phase = FreedomJumpPhase::idle;
      candidate.travel_->spool_tick = 0;
      candidate.travel_->local->selected.reset();
      result.jump_arrived = true;
    }
  }
  if (const auto saved = candidate.travel_document(); !saved)
    return std::unexpected{saved.error()};
  *this = std::move(candidate);
  return result;
}
} // namespace apsis_drift
