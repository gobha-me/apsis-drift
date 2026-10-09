#include "apsis_drift/native_flight_session.hpp"

#include <cmath>
#include <limits>

namespace apsis_drift {
auto NativeFreedomFlightSession::travel_document() const
    -> std::expected<FreedomTravelSaveDocument, std::string> {
  if (!travel_ || !resources_ || !knowledge_)
    return std::unexpected{"No selected native travel owner"};
  FreedomTravelSaveDocument result{
      {{surface_document(), *resources_}, *knowledge_},
      *travel_,
      starting_assembly_};
  if (auto v = validate_freedom_travel_document(result); !v)
    return std::unexpected{v.error().detail};
  return result;
}
auto NativeFreedomFlightSession::select_jump(SystemId destination)
    -> std::expected<void, std::string> {
  if (travel_ && travel_->phase != FreedomJumpPhase::idle)
    return std::unexpected{
        "Cancel the active spool before changing destination"};
  if (!resources_ || !knowledge_ || !starting_assembly_ ||
      document_.model.physical_catalog != 2 ||
      document_.model.physical_ephemeris != 2 ||
      document_.flight.craft !=
          CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion})
    return std::unexpected{"This save lacks the qualified native starter "
                           "world/resources/craft selection"};
  const auto tick = document_.flight.tick;
  if (!validate_freedom_resources(*resources_, document_) ||
      !validate_freedom_knowledge(*knowledge_, tick))
    return std::unexpected{"Current knowledge/resources are stale or invalid"};
  const auto reading = query_freedom_knowledge(
      *knowledge_,
      {KnowledgeSubjectKind::system, destination, destination.value},
      KnowledgeFact::location, tick);
  if (!reading || !*reading ||
      (*reading)->provenance.level < NavigationKnowledgeLevel::resolved)
    return std::unexpected{"A resolved system location is required"};
  const auto route =
      generate_first_universe_route(document_.origin.recipe.universe_seed);
  if (destination == document_.flight.frame.system ||
      (destination != route.origin && destination != route.destination))
    return std::unexpected{"Select the other known system"};
  auto candidate = *this;
  if (!candidate.travel_) {
    if (document_.flight.frame.system != route.origin)
      return std::unexpected{"Native travel history is required outside home"};
    candidate.travel_ = FreedomTravelState{};
    candidate.document_.world = FreedomActiveWorldSelection{
        1, document_.flight.frame.system, *document_.flight.frame.planet};
    candidate.knowledge_->recipe.version = kFreedomTravelKnowledgeVersion;
    candidate.knowledge_->recipe.observation_policy =
        kFreedomLocalObservationPolicy;
    candidate.knowledge_->recipe.world_domain = 1;
  }
  candidate.travel_->selected = destination;
  if (auto v = candidate.travel_document(); !v)
    return std::unexpected{v.error()};
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::jump_preview() const
    -> std::expected<FreedomJumpPreview, std::string> {
  if (travel_ && travel_->phase == FreedomJumpPhase::transit &&
      travel_->committed)
    return travel_->committed->preview;
  if (!travel_ || !travel_->selected || !resources_ || !knowledge_)
    return std::unexpected{"Select a known destination first"};
  if (!validate_freedom_resources(*resources_, document_) ||
      !validate_freedom_knowledge(*knowledge_, document_.flight.tick))
    return std::unexpected{"Jump source knowledge/resources are invalid"};
  FreedomJumpRequest request;
  request.universe_seed = document_.origin.recipe.universe_seed;
  request.craft = document_.origin.state.craft;
  request.source = document_.flight;
  request.destination = *travel_->selected;
  request.attempt = travel_->next_attempt;
  request.profile = document_.model.assistance
                        ? IntersystemRuleProfile::assisted
                        : IntersystemRuleProfile::pilot;
  auto result = preview_freedom_jump(request);
  if (!result)
    return std::unexpected{
        "Actual jump geometry is outside the qualified starter policy"};
  return *result;
}
auto NativeFreedomFlightSession::jump_available() const
    -> std::expected<void, std::string> {
  if (!travel_ || travel_->phase != FreedomJumpPhase::idle)
    return std::unexpected{"Select an idle native jump first"};
  if (actor_ || (docking_ && docking_->attached) ||
      (surface_ && surface_->landed))
    return std::unexpected{"Jump requires a seated pilot in free flight"};
  if (!resources_ || !resources_->jump_charges)
    return std::unexpected{"No jump charges remain"};
  if (travel_->next_attempt == std::numeric_limits<std::uint64_t>::max() ||
      document_.flight.tick > std::numeric_limits<SimulationTick>::max() -
                                  kJumpSpoolTicks - kJumpTransitTicks - 3)
    return std::unexpected{"Jump cannot retain an advanceable saved arrival"};
  const auto observed = observe();
  if (!observed || observed->atmosphere.altitude_metres <
                       observed->atmosphere.space_boundary_altitude_metres)
    return std::unexpected{
        "Atmospheric jumping is not qualified by this starter policy"};
  const auto preview = jump_preview();
  if (!preview) return std::unexpected{preview.error()};
  const auto departure = assess_freedom_arrival_volume(
      system_, document_.flight.tick, preview->source_position, 0);
  if (!departure || !departure->clear() || !preview->volume ||
      !preview->volume->clear())
    return std::unexpected{"Body intersection or extended-range consequences "
                           "are not yet qualified"};
  return {};
}
auto NativeFreedomFlightSession::begin_jump()
    -> std::expected<void, std::string> {
  if (auto available = jump_available(); !available) return available;
  auto candidate = *this;
  candidate.travel_->phase = FreedomJumpPhase::spool;
  candidate.travel_->spool_tick = document_.flight.tick;
  candidate.cancel_port_approach();
  candidate.cancel_surface_maneuver();
  if (auto v = candidate.travel_document(); !v)
    return std::unexpected{v.error()};
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::cancel_jump()
    -> std::expected<void, std::string> {
  if (!travel_ || travel_->phase != FreedomJumpPhase::spool)
    return std::unexpected{"Only an uncommitted spool can be canceled"};
  auto candidate = *this;
  candidate.travel_->phase = FreedomJumpPhase::idle;
  candidate.travel_->spool_tick = 0;
  if (auto v = candidate.travel_document(); !v)
    return std::unexpected{v.error()};
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::advance_jump(
    const NativeFlightControls& controls, SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (!std::isfinite(step.count()) || step != kSimulationStep || !travel_ ||
      travel_->phase == FreedomJumpPhase::idle)
    return std::unexpected{"Jump step requires an active fixed tick"};
  const VacuumIntent intent{
      controls.positive_translation, controls.negative_translation,
      controls.positive_rotation, controls.negative_rotation,
      document_.model.assistance};
  const auto initial = evaluate_atmospheric_flight(
      RigidBodyWorldContext{system_}, document_.flight, intent, rotation_,
      document_.model.atmosphere, document_.model.central);
  if (!initial) return std::unexpected{"Invalid flight input during jump"};
  auto candidate = *this;
  NativeFlightStep result{};
  if (travel_->phase == FreedomJumpPhase::spool) {
    auto flight = candidate.advance_craft_tick(controls, step);
    if (!flight) return std::unexpected{flight.error()};
    result = *flight;
    if (candidate.surface_ && candidate.surface_->landed) {
      candidate.travel_->phase = FreedomJumpPhase::idle;
      candidate.travel_->spool_tick = 0;
    } else if (candidate.document_.flight.tick -
                   candidate.travel_->spool_tick ==
               kJumpSpoolTicks) {
      const auto preview = candidate.jump_preview();
      const auto observation = candidate.observe();
      const auto departure =
          preview
              ? assess_freedom_arrival_volume(candidate.system_,
                                              candidate.document_.flight.tick,
                                              preview->source_position, 0)
              : std::expected<FreedomArrivalVolumeAssessment,
                              FreedomJumpTargetingError>{
                    std::unexpected{FreedomJumpTargetingError::invalid_source}};
      if (!preview || !preview->volume || !preview->volume->clear() ||
          !departure || !departure->clear() || !observation ||
          observation->atmosphere.altitude_metres <
              observation->atmosphere.space_boundary_altitude_metres ||
          (candidate.surface_ && candidate.surface_->landed) ||
          !candidate.resources_->jump_charges) {
        // Source drift may invalidate a draft. Keep the ordinary physics tick
        // and its actual flight-fuel bill, while declining jump commitment.
        candidate.travel_->phase = FreedomJumpPhase::idle;
        candidate.travel_->spool_tick = 0;
      } else {
        const auto frozen = freeze_freedom_jump_arrival(preview->request);
        if (!frozen || !frozen->point_assessment.clear())
          return std::unexpected{"Frozen arrival is not qualified"};
        candidate.travel_->committed = *frozen;
        candidate.travel_->commitment_resources = *candidate.resources_;
        --candidate.resources_->jump_charges;
        ++candidate.travel_->next_attempt;
        candidate.travel_->phase = FreedomJumpPhase::transit;
        candidate.docking_.reset();
        candidate.boarding_.reset();
        result.jump_committed = true;
      }
    }
  } else {
    const auto& frozen = *travel_->committed;
    const auto next =
        advance_freedom_resource_tick(*resources_, document_.flight.tick, 0);
    if (!next) return std::unexpected{"Transit resource clock refused"};
    candidate.resources_ = *next;
    ++candidate.document_.flight.tick;
    candidate.document_.origin.state.tick = candidate.document_.flight.tick;
    if (candidate.document_.flight.tick == frozen.preview.arrival_tick) {
      const auto& request = frozen.preview.request;
      const auto ids =
          generate_first_intersystem_identities(request.universe_seed);
      auto system =
          request.destination == ids.origin_system
              ? generate_physical_origin_system(request.universe_seed, 2)
              : generate_physical_local_system(ids.target_system_seed, 2);
      if (!system)
        return std::unexpected{"Arrival physical world is unavailable"};
      const auto body =
          resolve_planet_ephemeris(*system, frozen.preview.reference_planet,
                                   {frozen.preview.arrival_tick, 0});
      if (!body) return std::unexpected{"Arrival reference is unavailable"};
      auto pose = request.source;
      pose.tick = frozen.preview.arrival_tick;
      pose.frame = {RigidFrameKind::planet_relative_inertial,
                    request.destination,
                    frozen.preview.reference_planet,
                    {}};
      pose.position_metres = {frozen.point.x - body->position.x,
                              frozen.point.y - body->position.y,
                              frozen.point.z - body->position.z};
      pose.linear_velocity_metres_per_second = {
          frozen.preview.arrival_velocity.x - body->velocity.x,
          frozen.preview.arrival_velocity.y - body->velocity.y,
          frozen.preview.arrival_velocity.z - body->velocity.z};
      auto canonical =
          canonicalize_rigid_body_state(RigidBodyWorldContext{*system}, pose);
      if (!canonical)
        return std::unexpected{"Arrival rigid handoff is invalid"};
      candidate.document_.flight = *canonical;
      candidate.document_.world = FreedomActiveWorldSelection{
          1, request.destination, frozen.preview.reference_planet};
      candidate.document_.model.hold = OrbitHoldRequest{};
      auto hydrated = hydrate_freedom_flight_document(candidate.document_);
      if (!hydrated) return std::unexpected{hydrated.error().detail};
      candidate.system_ = std::move(hydrated->system);
      candidate.rotation_ = hydrated->rotation;
      candidate.travel_->phase = FreedomJumpPhase::idle;
      candidate.travel_->spool_tick = 0;
      candidate.travel_->selected.reset();
      if (auto learned = candidate.refresh_knowledge(
              LocalObservationEvent::system_arrival);
          !learned)
        return std::unexpected{learned.error()};
      result.jump_arrived = true;
    }
    const auto after = candidate.observe();
    if (!after) return std::unexpected{after.error()};
    result.actuation.initial = *initial;
    result.actuation.after = after->atmosphere;
    result.actuation.observation_after = after->orbit;
  }
  if (auto v = candidate.travel_document(); !v)
    return std::unexpected{v.error()};
  *this = std::move(candidate);
  return result;
}
} // namespace apsis_drift
