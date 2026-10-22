#include "apsis_drift/native_flight_session.hpp"

#include <array>
#include <type_traits>
#include <utility>

namespace apsis_drift {
auto NativeFreedomFlightSession::origin_assistance_port_reserved(
    OriginPortId port) const -> bool {
  if (!assistance_) return false;
  return std::visit(
      [&](const auto& actor) {
        using T = std::decay_t<decltype(actor)>;
        if constexpr (std::is_same_v<T, OriginServiceCraftState>)
          return port == actor.descriptor.home_port;
        else if constexpr (std::is_same_v<T, OriginServiceSortie>)
          return port == actor.service.descriptor.home_port;
        else
          return port == actor.last.service.descriptor.home_port;
      },
      *assistance_);
}

auto NativeFreedomFlightSession::save_document() const
    -> std::expected<NativeSaveDocument, std::string> {
  auto player = player_save_document();
  if (!player) return std::unexpected{player.error()};
  if (!assistance_) return player;
  if (!assistance_world_)
    return std::unexpected{"Origin service owner is unavailable"};
  auto voyage = std::visit(
      [](const auto& value)
          -> std::expected<FreedomAssistanceVoyage, std::string> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, FreedomKnowledgeSaveDocument> ||
                      std::is_same_v<T, FreedomTravelSaveDocument> ||
                      std::is_same_v<T, FreedomRecoverySaveDocument> ||
                      std::is_same_v<T, FreedomSurfaceWalkSaveDocument> ||
                      std::is_same_v<T, FreedomConditionSaveDocument>)
          return FreedomAssistanceVoyage{value};
        else
          return std::unexpected{
              "Origin service requires its complete player voyage"};
      },
      *player);
  if (!voyage) return std::unexpected{voyage.error()};
  FreedomAssistanceSaveDocument wrapped{1, std::move(*voyage), *assistance_};
  if (const auto valid =
          validate_freedom_assistance_document(*assistance_world_, wrapped);
      !valid)
    return std::unexpected{valid.error().detail};
  return NativeSaveDocument{std::move(wrapped)};
}

auto NativeFreedomFlightSession::assistance_recipient_supported() const
    -> bool {
  // Request is seated-only; later a supported outside actor does not move or
  // invalidate the same intact parked vessel. No second player is constructed.
  if (!assistance_world_ || !moon_ || *moon_ != assistance_world_->moon() ||
      !surface_ || !surface_->moon_landed || !surface_->gear_deployed ||
      !resources_ || !resources_->jump_charges || actor_ ||
      (docking_ && docking_->attached) ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle) ||
      (recovery_ && (recovery_->pending || recovery_->latest)) ||
      (condition_ &&
       (!condition_allows_landing(*condition_) ||
        (condition_->latest && condition_->latest->catastrophic))) ||
      !validate_freedom_resources(*resources_, document_))
    return false;
  const auto actual = resolve_moon_landed_craft(*moon_, *surface_->moon_landed,
                                                document_.flight.tick);
  return actual && *actual == document_.flight;
}

auto NativeFreedomFlightSession::origin_assistance_available() const
    -> std::expected<void, std::string> {
  if (!assistance_ || !assistance_world_)
    return std::unexpected{"This journey has no selected Origin service craft"};
  if (std::holds_alternative<OriginServiceUnavailable>(*assistance_))
    return std::unexpected{"Origin service contact is unavailable"};
  const auto* service = std::get_if<OriginServiceCraftState>(&*assistance_);
  if (!service)
    return std::unexpected{
        "The one service sortie is already selected or completed"};
  if (!boarding_ || boarding_->phase != FreedomBoardingPhase::seated ||
      surface_walker_ || actor_ || !starting_assembly_ ||
      *starting_assembly_ != NativeStartingAssemblySelection{} ||
      !assistance_recipient_supported())
    return std::unexpected{
        "Request from the intact seated Wayfarer supported on the Origin Moon"};
  if (service->body.tick != document_.flight.tick || !service->attached ||
      !validate_origin_service_craft(assistance_world_->origin(), *service) ||
      service->resources.jump_charges < 2 ||
      service->resources.flight_quanta < 2 * kFreedomFlightReserveQuanta ||
      service->resources.cargo_quanta !=
          service->descriptor.cargo_capacity_quanta)
    return std::unexpected{"Origin service craft lacks its finite roundtrip "
                           "supplies or attachment"};
  if (!knowledge_) return std::unexpected{"Origin contact is unavailable"};
  const auto station =
      generate_origin_station(document_.origin.recipe.universe_seed);
  const auto known = query_freedom_knowledge(
      *knowledge_,
      {KnowledgeSubjectKind::station, assistance_world_->origin().catalog.id,
       station.id.value},
      KnowledgeFact::identity_type, document_.flight.tick);
  if (!known || !(*known) ||
      !std::holds_alternative<KnownIdentity>((**known).value))
    return std::unexpected{
        "The Origin contact must be known before requesting service"};
  // Actual supported thrust/gravity/tilt predicate, not a nominal gravity or
  // a rescue permission bit. Empty flight fuel does not fabricate lift force.
  if (!release_moon_landed_craft(*moon_, *surface_->moon_landed,
                                 document_.flight.tick))
    return std::unexpected{
        "The supported craft lacks actual Moon liftoff authority"};
  if (resources_->flight_quanta == kFreedomFlightCapacityQuanta)
    return std::unexpected{"The flight tank is already full"};
  return {};
}

auto NativeFreedomFlightSession::request_origin_assistance()
    -> std::expected<void, std::string> {
  if (const auto ready = origin_assistance_available(); !ready) return ready;
  const OriginServiceBeacon beacon{1, document_.origin.state.craft,
                                   document_.origin.lineage,
                                   *surface_->moon_landed};
  const OriginServiceSortieRecipient recipient{
      document_, *surface_->moon_landed, *resources_};
  const auto sortie = begin_origin_service_sortie(
      assistance_world_->origin(), assistance_world_->moon(),
      std::get<OriginServiceCraftState>(*assistance_), beacon, recipient);
  if (!sortie) return std::unexpected{sortie.error()};
  auto candidate = *this;
  candidate.assistance_ = *sortie;
  candidate.assistance_propulsion_ = {};
  const auto saved = candidate.save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (const auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}

auto NativeFreedomFlightSession::cancel_origin_assistance()
    -> std::expected<void, std::string> {
  if (!assistance_ || !assistance_world_)
    return std::unexpected{"No Origin service sortie"};
  if (std::holds_alternative<OriginServiceUnavailable>(*assistance_))
    return std::unexpected{"Origin service contact is unavailable; no active "
                           "sortie can be canceled"};
  const auto* sortie = std::get_if<OriginServiceSortie>(&*assistance_);
  if (!sortie || sortie->phase == OriginServiceSortiePhase::completed ||
      sortie->phase == OriginServiceSortiePhase::canceled)
    return std::unexpected{"No active Origin service sortie"};
  const auto canceled = cancel_origin_service_sortie(
      assistance_world_->origin(), assistance_world_->moon(), *sortie);
  if (!canceled) return std::unexpected{canceled.error()};
  auto candidate = *this;
  // No actual departure or debit occurred: preserve the exact existing actor,
  // not a regenerated/refilled craft, so changing the request is harmless.
  if (sortie->phase == OriginServiceSortiePhase::requested &&
      canceled->service == sortie->service)
    candidate.assistance_ = canceled->service;
  else
    candidate.assistance_ = *canceled;
  candidate.assistance_propulsion_ = {};
  const auto saved = candidate.save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (const auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}

auto NativeFreedomFlightSession::advance_origin_assistance_tick()
    -> std::expected<void, std::string> {
  if (!assistance_ || !assistance_world_)
    return std::unexpected{"Missing selected Origin service owner"};
  const auto& world = *assistance_world_;
  const auto tick = document_.flight.tick;
  std::array<OriginDockConstraint, 1> occupants{};
  std::span<const OriginDockConstraint> roster;
  if (docking_ && docking_->attached) {
    occupants[0] = {docking_->geometry_version, docking_->target,
                    document_.flight.craft, tick};
    roster = occupants;
  }
  assistance_propulsion_ = {};
  // The attachment roster is an authoritative player constraint, not an NPC
  // route-failure signal. Never retire a contact to hide invalid input.
  if (!roster.empty()) {
    const auto station =
        generate_origin_station(document_.origin.recipe.universe_seed);
    const auto geometry = origin_station_geometry(station);
    if (!geometry || !resolve_origin_docked_pose(world.origin(), station,
                                                 *geometry, roster.front()))
      return std::unexpected{
          "Origin service received an invalid player attachment"};
  }
  if (auto* archived = std::get_if<OriginServiceUnavailable>(&*assistance_)) {
    if (!archived->failed_tick ||
        archived->failed_tick - 1 != archived->last.service.body.tick ||
        tick < archived->failed_tick)
      return std::unexpected{
          "Origin unavailable-contact history has an invalid clock"};
    // Immutable historical last contact: no later NPC position or force
    // claimed.
  } else if (auto* parked =
                 std::get_if<OriginServiceCraftState>(&*assistance_)) {
    if (parked->body.tick + 1 != tick || !parked->attached ||
        !validate_origin_service_craft(world.origin(), *parked))
      return std::unexpected{"Stationed Origin service clock or owner differs"};
    if (docking_ && docking_->attached &&
        docking_->target == parked->descriptor.home_port)
      return std::unexpected{"D2 is reserved for the Origin service craft"};
    const auto station =
        generate_origin_station(parked->descriptor.universe_seed);
    const auto geometry = origin_station_geometry(station);
    if (!geometry)
      return std::unexpected{"Origin service geometry unavailable"};
    const auto body =
        release_origin_port(world.origin(), station, *geometry,
                            {geometry->version, parked->descriptor.home_port,
                             parked->descriptor.frame, tick});
    const auto next = advance_origin_service_resources(
        parked->descriptor, parked->resources, parked->body.tick, 0);
    if (!body || !next)
      return std::unexpected{"Origin service attached tick refused"};
    parked->body = *body;
    parked->resources = *next;
  } else {
    std::optional<OriginServiceSortieRecipient> recipient;
    if (assistance_recipient_supported())
      recipient.emplace(OriginServiceSortieRecipient{
          document_, *surface_->moon_landed, *resources_});
    const auto original = std::get<OriginServiceSortie>(*assistance_);
    // Refuse malformed source/clock; only a genuine next route failure may be
    // recorded as historical unavailable contact.
    if (!tick || original.service.body.tick != tick - 1)
      return std::unexpected{
          "Origin service source is not the previous world tick"};
    if (const auto valid = validate_origin_service_sortie(
            world.origin(), world.moon(), original);
        !valid)
      return std::unexpected{valid.error()};
    const bool transit =
        original.phase == OriginServiceSortiePhase::outward_transit ||
        original.phase == OriginServiceSortiePhase::return_transit;
    if (transit && (!original.pending || tick > original.pending->arrival_tick))
      return std::unexpected{"Origin service pending transit clock is invalid"};
    auto stepped = advance_origin_service_sortie(
        world.origin(), world.moon(), original, tick,
        recipient ? &*recipient : nullptr, roster);
    if (!stepped) {
      const bool can_return =
          original.phase == OriginServiceSortiePhase::departing ||
          original.phase == OriginServiceSortiePhase::outward_spool ||
          original.phase == OriginServiceSortiePhase::approaching ||
          original.phase == OriginServiceSortiePhase::delivering;
      if (can_return) {
        const auto canceled = cancel_origin_service_sortie(
            world.origin(), world.moon(), original);
        if (!canceled) return std::unexpected{canceled.error()};
        // One real diversion from the last committed clear source, with no
        // receiver, credit, charge refund or speculative extra attempt.
        stepped = advance_origin_service_sortie(
            world.origin(), world.moon(), *canceled, tick, nullptr, roster);
        if (stepped && (stepped->receiver || stepped->transferred_quanta))
          return std::unexpected{
              "Origin return diversion unexpectedly credited fuel"};
      }
    }
    if (!stepped) {
      assistance_ = OriginServiceUnavailable{original, tick};
      // Exact pre-step source remains history, not an actor frozen at a future
      // position. The player's already valid step/fuel stay committed together.
    } else {
      if (stepped->receiver.has_value() != (stepped->transferred_quanta != 0))
        return std::unexpected{"Origin service transfer result is incomplete"};
      if (stepped->receiver) {
        if (!resources_ ||
            resources_->flight_quanta > kFreedomFlightCapacityQuanta ||
            stepped->transferred_quanta >
                kFreedomFlightCapacityQuanta - resources_->flight_quanta)
          return std::unexpected{
              "Origin service credit exceeds the actual receiver capacity"};
        auto expected = *resources_;
        expected.flight_quanta += stepped->transferred_quanta;
        if (*stepped->receiver != expected)
          return std::unexpected{
              "Origin service changed receiver metadata instead of fuel only"};
        resources_ = *stepped->receiver;
      }
      assistance_ = stepped->sortie;
      assistance_propulsion_ = stepped->propulsion;
    }
  }
  // Both component owners already authenticated and stepped their candidate.
  // Check their paired clock/identity and reservation here; full voyage/JSON
  // admission remains at request/cancel/Save/Continue boundaries.
  const auto& service = *std::visit(
      [](const auto& actor) -> const OriginServiceCraftState* {
        using T = std::decay_t<decltype(actor)>;
        if constexpr (std::is_same_v<T, OriginServiceCraftState>)
          return &actor;
        else if constexpr (std::is_same_v<T, OriginServiceSortie>)
          return &actor.service;
        else
          return &actor.last.service;
      },
      *assistance_);
  const auto* archived = std::get_if<OriginServiceUnavailable>(&*assistance_);
  const bool npc_clock =
      archived ? (archived->failed_tick &&
                  archived->failed_tick - 1 == service.body.tick &&
                  service.resources.tick == service.body.tick &&
                  tick >= archived->failed_tick)
               : (service.body.tick == tick && service.resources.tick == tick);
  if (!resources_ || document_.origin.state.tick != tick ||
      resources_->tick != tick ||
      resources_->craft != document_.origin.state.craft ||
      resources_->frame != document_.flight.craft || !npc_clock ||
      (docking_ && docking_->attached &&
       docking_->target == service.descriptor.home_port))
    return std::unexpected{"Origin service/player clock, receiver identity or "
                           "reserved D2 differs"};
  return {};
}

auto NativeFreedomFlightSession::advance(const NativeFlightControls& controls,
                                         SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (!assistance_) return advance_player(controls, step);
  auto candidate = *this;
  auto result = candidate.advance_player(controls, step);
  if (!result) return std::unexpected{result.error()};
  if (const auto npc = candidate.advance_origin_assistance_tick(); !npc)
    return std::unexpected{npc.error()};
  *this = std::move(candidate);
  return result;
}
auto NativeFreedomFlightSession::advance_walk(
    const OriginWalkControls& controls, SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (!assistance_) return advance_player_walk(controls, step);
  auto candidate = *this;
  auto result = candidate.advance_player_walk(controls, step);
  if (!result) return std::unexpected{result.error()};
  if (const auto npc = candidate.advance_origin_assistance_tick(); !npc)
    return std::unexpected{npc.error()};
  *this = std::move(candidate);
  return result;
}
auto NativeFreedomFlightSession::advance_surface_walk(
    const OriginWalkControls& controls, SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (!assistance_) return advance_player_surface_walk(controls, step);
  auto candidate = *this;
  auto result = candidate.advance_player_surface_walk(controls, step);
  if (!result) return std::unexpected{result.error()};
  if (const auto npc = candidate.advance_origin_assistance_tick(); !npc)
    return std::unexpected{npc.error()};
  *this = std::move(candidate);
  return result;
}
} // namespace apsis_drift
