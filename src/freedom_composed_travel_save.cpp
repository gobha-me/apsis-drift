#include "freedom_composed_travel_save_internal.hpp"

#include "apsis_drift/freedom_system_network.hpp"
#include "freedom_local_travel_save_internal.hpp"
#include "freedom_resource_save_internal.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace apsis_drift::detail {
namespace {
using Json = nlohmann::ordered_json;
auto failure(std::string message) -> SaveSchemaError {
  return {SaveSchemaErrorCode::invalid_state, "$.travel.composed",
          std::move(message)};
}
auto fields(const Json& j, std::initializer_list<std::string_view> names)
    -> bool {
  if (!j.is_object() || j.size() != names.size()) return false;
  for (const auto name : names)
    if (!j.contains(std::string{name})) return false;
  return true;
}
auto uints(const Json& j, std::initializer_list<std::string_view> names)
    -> bool {
  for (const auto name : names)
    if (!j.contains(std::string{name}) ||
        !j[std::string{name}].is_number_unsigned())
      return false;
  return true;
}
auto planet_json(const FrozenFreedomPlanetJumpArrival& f)
    -> std::expected<Json, SaveSchemaError> {
  const auto source = encode_freedom_flight_document_json(f.request.source);
  const auto arrival = encode_freedom_flight_document_json(f.arrival);
  const auto target = encode_body_target(f.request.destination);
  if (!source || !arrival || !target)
    return std::unexpected{failure("Planet receipt owner cannot encode")};
  Json result{{"version", f.version},
              {"source", Json::parse(*source)},
              {"destination", Json::parse(*target)},
              {"attempt", f.request.attempt},
              {"source_checksum", f.source_checksum},
              {"arrival_tick", f.arrival_tick},
              {"arrival", Json::parse(*arrival)}};
  if (f.request.recovery)
    result["recovery_input"] = drive_json(*f.request.recovery);
  if (f.recovery) result["recovery"] = uncertainty_json(*f.recovery);
  return result;
}
auto planet_value(const Json& j)
    -> std::expected<FrozenFreedomPlanetJumpArrival, SaveSchemaError> {
  const bool recovering = j.is_object() && j.contains("version") &&
                          j["version"].is_number_unsigned() &&
                          j["version"] == kFreedomRecoveryPlanetJumpVersion;
  auto shape = j;
  if (recovering) {
    if (!j.contains("recovery_input") || !j.contains("recovery"))
      return std::unexpected{failure("Missing planet recovery input/sample")};
    shape.erase("recovery_input");
    shape.erase("recovery");
  }
  if (!fields(shape, {"version", "source", "destination", "attempt",
                      "source_checksum", "arrival_tick", "arrival"}) ||
      !uints(j, {"version", "attempt", "source_checksum", "arrival_tick"}) ||
      (j["version"] != kFreedomPlanetJumpVersion && !recovering))
    return std::unexpected{failure("Invalid closed planet receipt")};
  auto source = decode_freedom_flight_document_json(j["source"].dump());
  const auto arrival = decode_freedom_flight_document_json(j["arrival"].dump());
  const auto target = decode_body_target(j["destination"].dump());
  if (!source || !arrival || !target)
    return std::unexpected{failure("Invalid planet receipt owner")};
  FreedomPlanetJumpRequest request{j["version"].get<std::uint32_t>(),
                                   std::move(*source), *target,
                                   j["attempt"].get<std::uint64_t>()};
  if (recovering) {
    auto drive = drive_value(j["recovery_input"], request.source.flight.tick);
    if (!drive) return std::unexpected{drive.error()};
    request.recovery = *drive;
  }
  const auto frozen = freeze_freedom_planet_jump_arrival(request);
  if (!frozen || frozen->arrival != *arrival ||
      frozen->arrival_tick != j["arrival_tick"].get<SimulationTick>() ||
      frozen->source_checksum != j["source_checksum"].get<std::uint64_t>())
    return std::unexpected{
        failure("Planet receipt differs from frozen solution")};
  if (recovering) {
    const auto sample = uncertainty_value(j["recovery"]);
    if (!sample || !frozen->recovery || *sample != *frozen->recovery)
      return std::unexpected{
          failure("Planet recovery sample differs from its input")};
  }
  return *frozen;
}
auto checkpoint_json(const FreedomComposedLocalCheckpoint& c)
    -> std::expected<Json, SaveSchemaError> {
  Json committed = nullptr, outbound = nullptr;
  if (c.committed) {
    auto encoded = travel_local_frozen_json(*c.committed);
    if (!encoded) return std::unexpected{encoded.error()};
    committed = std::move(*encoded);
  }
  if (c.outbound) {
    auto encoded = travel_local_frozen_json(*c.outbound);
    if (!encoded) return std::unexpected{encoded.error()};
    outbound = std::move(*encoded);
  }
  return Json{{"committed", std::move(committed)},
              {"commitment_resources",
               c.commitment_resources
                   ? travel_resources_json(*c.commitment_resources)
                   : Json(nullptr)},
              {"outbound", std::move(outbound)}};
}
auto checkpoint_value(const Json& j)
    -> std::expected<FreedomComposedLocalCheckpoint, SaveSchemaError> {
  if (!fields(j, {"committed", "commitment_resources", "outbound"}))
    return std::unexpected{failure("Invalid closed companion checkpoint")};
  FreedomComposedLocalCheckpoint c;
  if (!j["committed"].is_null()) {
    auto value = travel_local_frozen_value(j["committed"]);
    if (!value) return std::unexpected{value.error()};
    c.committed = std::move(*value);
  }
  if (!j["outbound"].is_null()) {
    auto value = travel_local_frozen_value(j["outbound"]);
    if (!value) return std::unexpected{value.error()};
    c.outbound = std::move(*value);
  }
  if (!j["commitment_resources"].is_null()) {
    auto value = travel_resources_value(j["commitment_resources"]);
    if (!value) return std::unexpected{value.error()};
    c.commitment_resources = *value;
  }
  return c;
}
// Origin history can gain authentic discoveries/deltas after a receipt. A
// receipt may retain a prefix, never rewrite or discard its earlier entries.
auto same_owner(const FreedomFlightSaveDocument& source,
                const FreedomFlightSaveDocument& current) -> bool {
  const auto& a = source.origin;
  const auto& b = current.origin;
  return a.recipe == b.recipe && a.lineage == b.lineage &&
         a.network == b.network && a.state.station == b.state.station &&
         a.state.craft == b.state.craft &&
         source.flight.craft == current.flight.craft &&
         a.state.tick <= b.state.tick &&
         a.state.discoveries.size() <= b.state.discoveries.size() &&
         std::equal(a.state.discoveries.begin(), a.state.discoveries.end(),
                    b.state.discoveries.begin()) &&
         a.state.world_deltas.size() <= b.state.world_deltas.size() &&
         std::equal(a.state.world_deltas.begin(), a.state.world_deltas.end(),
                    b.state.world_deltas.begin());
}
auto known_planet(const FreedomKnowledge& knowledge, BodyTarget target,
                  SimulationTick now) -> bool {
  if (target.body.kind != BodyKind::planet || target.body.version != 1)
    return false;
  const KnowledgeSubject subject{KnowledgeSubjectKind::planet, target.system,
                                 target.body.value};
  for (const auto fact :
       {KnowledgeFact::identity_type, KnowledgeFact::location}) {
    const auto reading = query_freedom_knowledge(knowledge, subject, fact, now);
    if (!reading || !*reading ||
        (**reading).provenance.level != NavigationKnowledgeLevel::resolved)
      return false;
  }
  return true;
}
struct Event {
  FreedomComposedRouteKind kind{};
  std::uint64_t attempt{};
  SimulationTick source_tick{}, arrival_tick{};
  BodyTarget source, destination;
  const FreedomResources* bill{};
  const FreedomFlightSaveDocument* source_flight{};
  const FreedomFlightSaveDocument* arrival{};
  const FrozenLocalBodyJumpArrival* local{};
  std::uint64_t source_checksum{};
  const FreedomDriveRecoveryState* recovery_input{};
};
auto recovery_kind(FreedomComposedRouteKind kind) -> FreedomJumpRecoveryKind {
  switch (kind) {
    case FreedomComposedRouteKind::system:
      return FreedomJumpRecoveryKind::system;
    case FreedomComposedRouteKind::planet:
      return FreedomJumpRecoveryKind::planet;
    case FreedomComposedRouteKind::moon: return FreedomJumpRecoveryKind::moon;
    case FreedomComposedRouteKind::minor: return FreedomJumpRecoveryKind::minor;
  }
  return static_cast<FreedomJumpRecoveryKind>(255);
}
auto stamp_of(const Event& e)
    -> std::expected<FreedomJumpRecoveryStamp, FreedomJumpRecoveryError> {
  if (!e.recovery_input)
    return std::unexpected{FreedomJumpRecoveryError::invalid_state};
  const auto next = advance_freedom_drive_recovery(
      *e.recovery_input, recovery_kind(e.kind), e.attempt, e.source_checksum,
      e.source_tick);
  if (!next) return std::unexpected{next.error()};
  return *next->last_use;
}
auto event(const FrozenLocalBodyJumpArrival& f, FreedomComposedRouteKind kind,
           const FreedomResources* bill) -> Event {
  return {kind,
          f.request.attempt,
          f.request.source.flight.tick,
          f.arrival_tick,
          *travel_target_of(f.request.source),
          f.request.destination,
          bill,
          &f.request.source,
          &f.arrival,
          &f,
          f.source_checksum,
          f.request.recovery ? &*f.request.recovery : nullptr};
}
auto bill_valid(const FreedomResources& bill,
                const FreedomFlightSaveDocument& source,
                std::uint32_t resource_version,
                const std::optional<FreedomDriveRecoveryState>& input) -> bool {
  return validate_freedom_resources(bill, source).has_value() &&
         bill.jump_charges > 0 && bill.version == resource_version &&
         bill.drive == input;
}
auto receipt_valid(const FrozenLocalBodyJumpArrival& f,
                   const FreedomFlightSaveDocument& current,
                   std::uint32_t policy) -> bool {
  return f.version == policy && validate_frozen_local_body_jump_arrival(f) &&
         same_owner(f.request.source, current) &&
         f.request.attempt < std::numeric_limits<std::uint64_t>::max() &&
         f.arrival_tick < std::numeric_limits<SimulationTick>::max() - 2 &&
         f.request.source.flight.tick <= current.flight.tick;
}
auto exact_system_arrival(const FrozenFreedomJumpArrival& f,
                          const FreedomFlightSaveDocument& flight) -> bool {
  const auto system = travel_source_world(f.preview.request.universe_seed,
                                          f.preview.request.destination,
                                          f.preview.request.network);
  if (!system) return false;
  const auto centre = resolve_planet_ephemeris(
      *system, f.preview.reference_planet, {f.preview.arrival_tick, 0});
  if (!centre) return false;
  auto pose = f.preview.request.source;
  pose.tick = f.preview.arrival_tick;
  pose.frame = {RigidFrameKind::planet_relative_inertial,
                f.preview.request.destination,
                f.preview.reference_planet,
                {}};
  pose.position_metres = {f.point.x - centre->position.x,
                          f.point.y - centre->position.y,
                          f.point.z - centre->position.z};
  pose.linear_velocity_metres_per_second = {
      f.preview.arrival_velocity.x - centre->velocity.x,
      f.preview.arrival_velocity.y - centre->velocity.y,
      f.preview.arrival_velocity.z - centre->velocity.z};
  const auto canonical =
      canonicalize_rigid_body_state(RigidBodyWorldContext{*system}, pose);
  return canonical && flight.flight == *canonical;
}
} // namespace

auto validate_composed_travel(const FreedomTravelSaveDocument& d)
    -> std::expected<void, SaveSchemaError> {
  if (const auto v = validate_freedom_knowledge_document(d.voyage); !v)
    return v;
  const auto& s = d.travel;
  const auto& surface = d.voyage.voyage.voyage;
  const auto flight = surface_base_flight(surface.base);
  const auto current = travel_target_of(flight);
  const auto& ledger = d.voyage.voyage.resources;
  const auto now = flight.flight.tick;
  const bool strained = s.version == kFreedomStrainedTravelStateVersion;
  const bool recovering =
      strained || s.version == kFreedomRecoveringTravelStateVersion;
  const auto resource_version = strained     ? kFreedomStrainedResourceVersion
                                : recovering ? kFreedomRecoveryResourceVersion
                                             : kFreedomResourceVersion;
  const auto drive_version = strained ? kFreedomStrainedDriveRecoveryVersion
                                      : kFreedomDriveRecoveryVersion;
  const auto knowledge_version = d.voyage.knowledge.recipe.version;
  const bool knowledge_matches =
      recovering ? (knowledge_version == kFreedomTravelKnowledgeVersion ||
                    freedom_knowledge_uses_network(knowledge_version))
                 : (flight.origin.network &&
                    knowledge_version == kFreedomPlanetaryKnowledgeVersion);
  if ((!recovering && s.version != kFreedomComposedTravelStateVersion) ||
      !s.composed || s.local || s.prior_moon || s.origin_routes ||
      !s.next_attempt || !knowledge_matches ||
      ledger.version != resource_version ||
      ledger.drive.has_value() != recovering ||
      (ledger.drive && ledger.drive->version != drive_version) || !current ||
      !d.craft_binding ||
      *d.craft_binding != NativeStartingAssemblySelection{} ||
      !travel_valid_pilot_binding(d, flight))
    return std::unexpected{failure("Composed travel requires explicit recipe5, "
                                   "one current world and actual craft/pilot")};
  const auto& c = *s.composed;
  if (c.version != kFreedomComposedRoutesVersion ||
      c.active > FreedomComposedRouteKind::minor ||
      (c.active == FreedomComposedRouteKind::system && c.selected_body) ||
      (c.active != FreedomComposedRouteKind::system && s.selected))
    return std::unexpected{failure("Invalid composed route selection")};
  std::optional<FreedomSystemNetwork> network;
  if (flight.origin.network) {
    auto generated = generate_freedom_system_network(
        flight.origin.recipe.universe_seed, flight.origin.network->version);
    if (!generated)
      return std::unexpected{failure("Composed network unavailable")};
    network = *generated;
  }
  const auto home_system =
      generate_physical_origin_system(flight.origin.recipe.universe_seed, 2);
  if (!home_system)
    return std::unexpected{failure("Composed source network unavailable")};
  const auto catalog = generate_secondary_body_catalog(*home_system);
  if (!catalog)
    return std::unexpected{failure("Origin companions unavailable")};
  const BodyTarget home{home_system->catalog.id,
                        body_id(flight.origin.recipe.home_planet)};
  const BodyTarget moon{home.system, catalog->bodies[0].node.id};
  const BodyTarget minor{home.system, catalog->bodies[1].node.id};
  const auto pair =
      generate_first_intersystem_identities(flight.origin.recipe.universe_seed);
  if (s.selected &&
      (!flight.world || *s.selected == current->system ||
       (network ? !find_freedom_system_network_node(*network, *s.selected)
                : (*s.selected != pair.origin_system &&
                   *s.selected != pair.target_system))))
    return std::unexpected{failure("Invalid known system destination")};
  if (c.selected_body) {
    bool allowed{};
    switch (c.active) {
      case FreedomComposedRouteKind::planet:
        allowed = knowledge_version == kFreedomPlanetaryKnowledgeVersion &&
                  flight.world && c.selected_body->system == current->system &&
                  *c.selected_body != *current &&
                  known_planet(d.voyage.knowledge, *c.selected_body, now);
        break;
      case FreedomComposedRouteKind::moon:
        allowed = (*current == home && *c.selected_body == moon) ||
                  (*current == moon && *c.selected_body == home);
        break;
      case FreedomComposedRouteKind::minor:
        allowed = (*current == home && *c.selected_body == minor) ||
                  (*current == minor && *c.selected_body == home);
        break;
      default: break;
    }
    if (!allowed)
      return std::unexpected{failure("Invalid typed composed destination")};
  }
  std::array<Event, 6> events{};
  std::size_t count{};
  const auto add = [&](Event value) { events[count++] = value; };
  if (s.committed.has_value() != s.commitment_resources.has_value() ||
      c.planet_committed.has_value() !=
          c.planet_commitment_resources.has_value())
    return std::unexpected{failure("Unpaired composed commitment bill")};
  if (s.committed) {
    const auto& f = *s.committed;
    const auto& r = f.preview.request;
    const auto& bill = *s.commitment_resources;
    if (!validate_frozen_freedom_jump_arrival(f) ||
        r.version != (recovering ? kFreedomRecoveryJumpTargetingVersion
                                 : kFreedomNetworkJumpTargetingVersion) ||
        !f.preview.volume || !f.preview.volume->clear() ||
        !f.point_assessment.clear() ||
        r.universe_seed != flight.origin.recipe.universe_seed ||
        r.network != flight.origin.network ||
        r.lineage != flight.origin.lineage || r.craft != ledger.craft ||
        !r.source.frame.planet ||
        r.attempt == std::numeric_limits<std::uint64_t>::max() ||
        f.preview.arrival_tick >=
            std::numeric_limits<SimulationTick>::max() - 2 ||
        r.source.tick > now || !validate_freedom_resources(bill) ||
        bill.craft != r.craft || bill.frame != r.source.craft ||
        bill.tick != r.source.tick || !bill.jump_charges ||
        bill.version != resource_version || bill.drive != r.recovery ||
        (recovering && (!bill.drive || !f.recovery ||
                        !validate_freedom_drive_recovery(
                            *bill.drive, r.universe_seed, r.craft,
                            r.source.craft, r.lineage, r.source.tick))))
      return std::unexpected{failure("Invalid composed system proof/bill")};
    add({FreedomComposedRouteKind::system,
         r.attempt,
         r.source.tick,
         f.preview.arrival_tick,
         {r.source.frame.system, body_id(*r.source.frame.planet)},
         {r.destination, body_id(f.preview.reference_planet)},
         &bill,
         nullptr,
         nullptr,
         nullptr,
         recovering ? f.recovery->source_checksum : 0,
         r.recovery ? &*r.recovery : nullptr});
  }
  if (c.planet_committed) {
    const auto& f = *c.planet_committed;
    if (knowledge_version != kFreedomPlanetaryKnowledgeVersion ||
        f.version != (recovering ? kFreedomRecoveryPlanetJumpVersion
                                 : kFreedomPlanetJumpVersion) ||
        !validate_frozen_freedom_planet_jump_arrival(f) ||
        !same_owner(f.request.source, flight) ||
        f.request.attempt == std::numeric_limits<std::uint64_t>::max() ||
        f.arrival_tick >= std::numeric_limits<SimulationTick>::max() - 2 ||
        !bill_valid(*c.planet_commitment_resources, f.request.source,
                    resource_version, f.request.recovery))
      return std::unexpected{failure("Invalid composed planet proof/bill")};
    add({FreedomComposedRouteKind::planet, f.request.attempt,
         f.request.source.flight.tick, f.arrival_tick,
         *travel_target_of(f.request.source), f.request.destination,
         &*c.planet_commitment_resources, &f.request.source, &f.arrival,
         nullptr, f.source_checksum,
         f.request.recovery ? &*f.request.recovery : nullptr});
  }
  for (const auto kind :
       {FreedomComposedRouteKind::moon, FreedomComposedRouteKind::minor}) {
    const auto& local =
        kind == FreedomComposedRouteKind::moon ? c.moon : c.minor;
    const auto target = kind == FreedomComposedRouteKind::moon ? moon : minor;
    const auto policy =
        kind == FreedomComposedRouteKind::moon
            ? (recovering ? kFreedomRecoveryLocalBodyJumpVersion
                          : kFreedomLocalBodyJumpVersion)
            : (recovering ? kFreedomRecoveryMinorLocalBodyJumpVersion
                          : kFreedomMinorLocalBodyJumpVersion);
    if (local.committed.has_value() != local.commitment_resources.has_value() ||
        local.committed.has_value() != local.outbound.has_value())
      return std::unexpected{
          failure("Unpaired companion proof, bill or outbound")};
    if (!local.committed) continue;
    const auto& last = *local.committed;
    const auto& out = *local.outbound;
    if (!receipt_valid(last, flight, policy) ||
        !receipt_valid(out, flight, policy) ||
        travel_target_of(out.request.source) != home ||
        out.request.destination != target ||
        !bill_valid(*local.commitment_resources, last.request.source,
                    resource_version, last.request.recovery))
      return std::unexpected{
          failure("Invalid companion source or current outbound")};
    if (travel_target_of(last.request.source) == target) {
      if (last.request.destination != home ||
          last.request.attempt != out.request.attempt + 1 ||
          last.request.home_model != out.request.home_model ||
          out.arrival_tick > last.request.source.flight.tick)
        return std::unexpected{
            failure("Companion return differs from real outbound")};
    } else if (last != out)
      return std::unexpected{
          failure("Open companion route differs from outbound")};
    add(event(last, kind, &*local.commitment_resources));
    add(event(out, kind, nullptr));
  }
  const Event* latest{};
  for (std::size_t i = 0; i < count; ++i) {
    const auto& e = events[i];
    if (!latest || e.attempt > latest->attempt) latest = &e;
    for (std::size_t j = 0; j < i; ++j) {
      const auto& other = events[j];
      if (e.attempt == other.attempt) {
        if (!e.local || !other.local || *e.local != *other.local)
          return std::unexpected{
              failure("Different routes reuse one commitment attempt")};
        continue;
      }
      const auto& earlier = e.attempt < other.attempt ? e : other;
      const auto& later = e.attempt < other.attempt ? other : e;
      if (earlier.arrival_tick > later.source_tick ||
          (later.attempt == earlier.attempt + 1 &&
           earlier.destination != later.source))
        return std::unexpected{
            failure("Retained routes reverse clock or adjacent body owner")};
    }
  }
  if (s.next_attempt != (latest ? latest->attempt + 1 : 1))
    return std::unexpected{
        failure("Composed attempt counter differs from latest proof")};
  if (recovering) {
    if (!latest) {
      if (ledger.drive->last_use)
        return std::unexpected{
            failure("Unused recovering journey retains a drive use")};
    } else {
      const auto stamp = stamp_of(*latest);
      if (!stamp || ledger.drive->last_use != std::optional{*stamp})
        return std::unexpected{
            failure("Current drive use differs from latest typed commitment")};
    }
    for (std::size_t i = 0; i < count; ++i) {
      const auto& e = events[i];
      if (!e.recovery_input || e.recovery_input->version != drive_version ||
          (!e.recovery_input->last_use && e.attempt != 1) ||
          (e.recovery_input->last_use &&
           (e.recovery_input->last_use->attempt >=
                std::numeric_limits<std::uint64_t>::max() - 1 ||
            e.recovery_input->last_use->attempt + 1 != e.attempt ||
            e.recovery_input->last_use->arrival_tick > e.source_tick)))
        return std::unexpected{
            failure("Recovery input differs from preceding actual attempt")};
      if (!e.recovery_input->last_use) continue;
      for (std::size_t j = 0; j < count; ++j)
        if (events[j].attempt == e.recovery_input->last_use->attempt) {
          const auto prior = stamp_of(events[j]);
          if (!prior || *prior != *e.recovery_input->last_use)
            return std::unexpected{
                failure("Recovery input rewrites a retained typed use")};
        }
    }
  }
  for (const auto* checkpoint : {&c.moon, &c.minor})
    if (checkpoint->committed &&
        checkpoint->committed->request.destination != home &&
        (!latest || latest->local != &*checkpoint->committed))
      return std::unexpected{
          failure("Another route overlaps an open companion excursion")};
  switch (s.phase) {
    case FreedomJumpPhase::idle:
      if (s.spool_tick)
        return std::unexpected{
            failure("Idle composition retains a spool clock")};
      break;
    case FreedomJumpPhase::spool:
      if ((!s.selected && !c.selected_body) || !travel_free_pilot(d) ||
          surface.surface.landed || surface.surface.moon_landed ||
          s.spool_tick > now || now - s.spool_tick >= kJumpSpoolTicks ||
          s.spool_tick > std::numeric_limits<SimulationTick>::max() -
                             kJumpSpoolTicks - kJumpTransitTicks - 3)
        return std::unexpected{
            failure("Invalid composed physical spool/clock")};
      break;
    case FreedomJumpPhase::transit: {
      if (!latest || latest->kind != c.active || !latest->bill ||
          !std::holds_alternative<FreedomFlightSaveDocument>(surface.base) ||
          surface.surface.landed || surface.surface.moon_landed ||
          s.spool_tick >
              std::numeric_limits<SimulationTick>::max() - kJumpSpoolTicks ||
          latest->source_tick != s.spool_tick + kJumpSpoolTicks ||
          latest->source_tick > now || now >= latest->arrival_tick ||
          latest->source != *current ||
          ledger.flight_quanta != latest->bill->flight_quanta ||
          ledger.jump_charges != latest->bill->jump_charges - 1)
        return std::unexpected{
            failure("Transit owner, clock or single charge differs")};
      if (c.active == FreedomComposedRouteKind::system) {
        const auto& r = s.committed->preview.request;
        auto pose = flight.flight;
        pose.tick = r.source.tick;
        if (!s.selected || *s.selected != r.destination || pose != r.source ||
            r.profile != (flight.model.assistance
                              ? IntersystemRuleProfile::assisted
                              : IntersystemRuleProfile::pilot))
          return std::unexpected{
              failure("Composed system transit source changed")};
      } else {
        auto source = flight;
        source.flight.tick = latest->source_flight->flight.tick;
        source.origin.state.tick = latest->source_flight->origin.state.tick;
        if (!c.selected_body || *c.selected_body != latest->destination ||
            source != *latest->source_flight)
          return std::unexpected{
              failure("Composed body transit source changed")};
      }
      return {};
    }
    default: return std::unexpected{failure("Unknown composed phase")};
  }
  if (!latest) {
    if (*current != home)
      return std::unexpected{
          failure("New composition must start at genuine Home")};
    return {};
  }
  if (latest->arrival_tick > now || latest->destination != *current ||
      (s.phase == FreedomJumpPhase::spool &&
       latest->arrival_tick > s.spool_tick))
    return std::unexpected{
        failure("Current world/clock differs from latest arrival")};
  if (now == latest->arrival_tick) {
    if (!latest->bill || ledger.flight_quanta != latest->bill->flight_quanta ||
        ledger.jump_charges != latest->bill->jump_charges - 1 ||
        (latest->arrival ? (flight.flight != latest->arrival->flight ||
                            flight.origin != latest->arrival->origin)
                         : !exact_system_arrival(*s.committed, flight)))
      return std::unexpected{
          failure("Instant arrival pose or bill differs from frozen receipt")};
  }
  return {};
}

auto encode_composed_travel(const FreedomTravelSaveDocument& d)
    -> std::expected<std::string, SaveSchemaError> {
  if (const auto v = validate_composed_travel(d); !v)
    return std::unexpected{v.error()};
  const auto voyage = encode_freedom_knowledge_document_json(d.voyage);
  if (!voyage) return std::unexpected{voyage.error()};
  const auto& s = d.travel;
  const auto& c = *s.composed;
  Json system = nullptr, planet = nullptr, target = nullptr, pilot = nullptr;
  if (s.committed) {
    auto value = travel_frozen_json(*s.committed);
    if (!value) return std::unexpected{value.error()};
    system = std::move(*value);
  }
  if (c.planet_committed) {
    auto value = planet_json(*c.planet_committed);
    if (!value) return std::unexpected{value.error()};
    planet = std::move(*value);
  }
  if (c.selected_body) {
    auto value = encode_body_target(*c.selected_body);
    if (!value)
      return std::unexpected{failure("Composed destination cannot encode")};
    target = Json::parse(*value);
  }
  if (d.seated_pilot) {
    auto value = encode_freedom_boarding_state_json(
        *d.seated_pilot,
        surface_base_flight(d.voyage.voyage.voyage.base).flight.tick);
    if (!value) return std::unexpected{value.error()};
    pilot = Json::parse(*value);
  }
  auto moon = checkpoint_json(c.moon), minor = checkpoint_json(c.minor);
  if (!moon) return std::unexpected{moon.error()};
  if (!minor) return std::unexpected{minor.error()};
  const Json root{
      {"format_version", s.version == kFreedomStrainedTravelStateVersion
                             ? kFreedomStrainedTravelSaveFormatVersion
                         : s.version == kFreedomRecoveringTravelStateVersion
                             ? kFreedomRecoveringTravelSaveFormatVersion
                             : kFreedomComposedTravelSaveFormatVersion},
      {"voyage", Json::parse(*voyage)},
      {"travel",
       {{"version", s.version},
        {"phase", static_cast<unsigned>(s.phase)},
        {"selected", s.selected ? Json(s.selected->value) : Json(nullptr)},
        {"spool_tick", s.spool_tick},
        {"next_attempt", s.next_attempt},
        {"committed", std::move(system)},
        {"commitment_resources",
         s.commitment_resources ? travel_resources_json(*s.commitment_resources)
                                : Json(nullptr)},
        {"composed",
         {{"version", c.version},
          {"active", static_cast<unsigned>(c.active)},
          {"selected_body", std::move(target)},
          {"planet_committed", std::move(planet)},
          {"planet_commitment_resources",
           c.planet_commitment_resources
               ? travel_resources_json(*c.planet_commitment_resources)
               : Json(nullptr)},
          {"moon", std::move(*moon)},
          {"minor", std::move(*minor)}}}}},
      {"craft_binding", travel_binding_json(*d.craft_binding)},
      {"seated_pilot", std::move(pilot)}};
  auto result = root.dump(2) + '\n';
  if (result.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{failure("Composed travel save exceeds limit")};
  return result;
}
auto decode_composed_travel(const Json& root)
    -> std::expected<FreedomTravelSaveDocument, SaveSchemaError> {
  if (!fields(root, {"format_version", "voyage", "travel", "craft_binding",
                     "seated_pilot"}) ||
      (root["format_version"] != kFreedomComposedTravelSaveFormatVersion &&
       root["format_version"] != kFreedomRecoveringTravelSaveFormatVersion &&
       root["format_version"] != kFreedomStrainedTravelSaveFormatVersion))
    return std::unexpected{failure("Invalid closed composed root")};
  const auto& s = root["travel"];
  const auto version =
      root["format_version"] == kFreedomStrainedTravelSaveFormatVersion
          ? kFreedomStrainedTravelStateVersion
      : root["format_version"] == kFreedomRecoveringTravelSaveFormatVersion
          ? kFreedomRecoveringTravelStateVersion
          : kFreedomComposedTravelStateVersion;
  if (!fields(s, {"version", "phase", "selected", "spool_tick", "next_attempt",
                  "committed", "commitment_resources", "composed"}) ||
      !uints(s, {"version", "phase", "spool_tick", "next_attempt"}) ||
      s["version"] != version ||
      s["phase"] > static_cast<unsigned>(FreedomJumpPhase::transit))
    return std::unexpected{failure("Invalid composed shared phase")};
  const auto& c = s["composed"];
  if (!fields(c, {"version", "active", "selected_body", "planet_committed",
                  "planet_commitment_resources", "moon", "minor"}) ||
      !uints(c, {"version", "active"}) ||
      c["version"] != kFreedomComposedRoutesVersion ||
      c["active"] > static_cast<unsigned>(FreedomComposedRouteKind::minor) ||
      root["craft_binding"] !=
          travel_binding_json(NativeStartingAssemblySelection{}))
    return std::unexpected{
        failure("Invalid composed routes or authored binding")};
  auto voyage = decode_freedom_knowledge_document_json(root["voyage"].dump());
  if (!voyage) return std::unexpected{voyage.error()};
  FreedomTravelSaveDocument d;
  d.voyage = std::move(*voyage);
  d.craft_binding = NativeStartingAssemblySelection{};
  auto& state = d.travel;
  state.version = version;
  state.phase = static_cast<FreedomJumpPhase>(s["phase"].get<unsigned>());
  state.spool_tick = s["spool_tick"].get<SimulationTick>();
  state.next_attempt = s["next_attempt"].get<std::uint64_t>();
  state.composed.emplace();
  auto& routes = *state.composed;
  routes.active =
      static_cast<FreedomComposedRouteKind>(c["active"].get<unsigned>());
  if (!s["selected"].is_null()) {
    if (!s["selected"].is_number_unsigned())
      return std::unexpected{failure("Invalid composed system destination")};
    state.selected = SystemId{s["selected"].get<std::uint64_t>()};
  }
  if (!s["committed"].is_null()) {
    auto value = travel_frozen_value(s["committed"]);
    if (!value) return std::unexpected{value.error()};
    state.committed = std::move(*value);
  }
  if (!s["commitment_resources"].is_null()) {
    auto value = travel_resources_value(s["commitment_resources"]);
    if (!value) return std::unexpected{value.error()};
    state.commitment_resources = *value;
  }
  if (!c["selected_body"].is_null()) {
    auto value = decode_body_target(c["selected_body"].dump());
    if (!value)
      return std::unexpected{failure("Invalid composed typed destination")};
    routes.selected_body = *value;
  }
  if (!c["planet_committed"].is_null()) {
    auto value = planet_value(c["planet_committed"]);
    if (!value) return std::unexpected{value.error()};
    routes.planet_committed = std::move(*value);
  }
  if (!c["planet_commitment_resources"].is_null()) {
    auto value = travel_resources_value(c["planet_commitment_resources"]);
    if (!value) return std::unexpected{value.error()};
    routes.planet_commitment_resources = *value;
  }
  auto moon = checkpoint_value(c["moon"]), minor = checkpoint_value(c["minor"]);
  if (!moon) return std::unexpected{moon.error()};
  if (!minor) return std::unexpected{minor.error()};
  routes.moon = std::move(*moon);
  routes.minor = std::move(*minor);
  if (!root["seated_pilot"].is_null()) {
    auto value = decode_freedom_boarding_state_json(
        root["seated_pilot"].dump(),
        surface_base_flight(d.voyage.voyage.voyage.base).flight.tick);
    if (!value) return std::unexpected{value.error()};
    d.seated_pilot = *value;
  }
  if (const auto v = validate_composed_travel(d); !v)
    return std::unexpected{v.error()};
  return d;
}
} // namespace apsis_drift::detail
