#include "apsis_drift/native_flight_session.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace apsis_drift {
auto NativeFreedomFlightSession::select_local_jump(BodyTarget target)
    -> std::expected<void, std::string> {
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
  if (!candidate.travel_->local)
    candidate.travel_->local = FreedomLocalTravelState{};
  candidate.travel_->version = kFreedomLocalTravelStateVersion;
  candidate.travel_->selected.reset();
  candidate.travel_->local->selected = target;
  if (!candidate.document_.moon && !candidate.document_.world) {
    const auto relative = candidate.relative_state();
    if (!relative || !relative->frame.planet)
      return std::unexpected{"Local departure reference unavailable"};
    candidate.document_.world = FreedomActiveWorldSelection{
        1, system_.catalog.id, *relative->frame.planet};
  }
  candidate.knowledge_->recipe.version = kFreedomTravelKnowledgeVersion;
  candidate.knowledge_->recipe.observation_policy =
      kFreedomLocalObservationPolicy;
  candidate.knowledge_->recipe.world_domain = 1;
  if (auto preview = candidate.local_jump_preview(); !preview)
    return std::unexpected{preview.error()};
  if (auto saved = candidate.travel_document(); !saved)
    return std::unexpected{saved.error()};
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::local_jump_preview() const
    -> std::expected<FrozenLocalBodyJumpArrival, std::string> {
  if (!travel_ || !travel_->local || !travel_->local->selected)
    return std::unexpected{"Select the moon or home body first"};
  const auto& local = *travel_->local;
  if (travel_->phase == FreedomJumpPhase::transit && local.committed)
    return *local.committed;
  const auto home_model =
      local.outbound ? local.outbound->request.home_model : document_.model;
  const auto result = freeze_local_body_jump_arrival(
      {1, document_, *local.selected, travel_->next_attempt, home_model});
  if (!result)
    return std::unexpected{"Local jump source, destination or clearance is "
                           "outside the supported route"};
  return *result;
}
auto NativeFreedomFlightSession::local_jump_available() const
    -> std::expected<void, std::string> {
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
  if (!moon_) {
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
  if (const auto available = local_jump_available(); !available)
    return available;
  auto candidate = *this;
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
  if (moon_) {
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
      if (!candidate.moon_) {
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
        if (!local.outbound && !frozen->request.source.moon)
          local.outbound = *frozen;
        --candidate.resources_->jump_charges;
        ++candidate.travel_->next_attempt;
        candidate.travel_->phase = FreedomJumpPhase::transit;
        candidate.docking_.reset();
        result.jump_committed = true;
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
      candidate.surface_ = FreedomSurfaceState{};
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
