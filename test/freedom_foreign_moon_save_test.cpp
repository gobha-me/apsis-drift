#include "apsis_drift/freedom_recovery_save.hpp"
#include "apsis_drift/freedom_system_network.hpp"
#include "apsis_drift/freedom_travel_save.hpp"
#include "apsis_drift/moon_surface_landmarks.hpp"
#include "apsis_drift/native_craft_binding.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <tuple>

#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <source_location>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
unsigned checks{};
auto check(bool value, const char* message) -> void {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
template <class T, class E>
auto need(std::expected<T, E> value,
          std::source_location location = std::source_location::current())
    -> T {
  if (!value) {
    std::cerr << location.file_name() << ':' << location.line() << ' ';
    if constexpr (requires { value.error().detail; })
      std::cerr << value.error().detail << '\n';
    else if constexpr (std::is_enum_v<E>)
      std::cerr << "provider refusal " << static_cast<unsigned>(value.error())
                << '\n';
    throw std::runtime_error("Composed codec fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto flight_of(const FreedomTravelSaveDocument& d)
    -> FreedomFlightSaveDocument {
  return surface_base_flight(d.voyage.voyage.voyage.base);
}
auto roundtrip(const FreedomTravelSaveDocument& d) -> std::string {
  const auto encoded = need(encode_freedom_travel_document_json(d));
  const auto decoded = need(decode_freedom_travel_document_json(encoded));
  check(decoded == d &&
            need(encode_freedom_travel_document_json(decoded)) == encoded,
        "Explicit56 preserves current voyage, fixed receipts, bill and "
        "canonical bytes");
  check(Json::parse(encoded)["format_version"] ==
            (d.travel.version == kFreedomForeignMoonTravelStateVersion ? 61U
                                                                       : 56U),
        "Composed travel preserves its explicit outer envelope");
  return encoded;
}
auto tick(FreedomTravelSaveDocument& d, SimulationTick time) -> void {
  auto& flight =
      std::get<FreedomFlightSaveDocument>(d.voyage.voyage.voyage.base);
  flight.flight.tick = time;
  flight.origin.state.tick = time;
  d.voyage.voyage.resources.tick = time;
}
auto fixture() -> FreedomTravelSaveDocument {
  // Saved-space codec fixture, not a claimed flown departure or descent.
  // The pilot is constructed from the genuine station entry/boarding route.
  const Seed seed{42};
  auto origin = need(make_freedom_network_new_game_document(seed));
  const auto system = need(generate_physical_origin_system(seed, 2));
  const auto station = generate_origin_station(seed);
  const auto geometry = need(origin_station_geometry(station));
  const auto port = need(resolve_origin_port_pose(system, station, geometry,
                                                  {station.id, 1}, 5000));
  OriginWalkerState entry;
  for (unsigned i = 0; i < 2960; ++i)
    entry = need(advance_origin_walker(entry, {0, -1, 0}));
  const auto binding = need(make_native_starting_assembly_binding({}));
  const RigidVector3 eye{entry.foot_position_metres.x,
                         entry.foot_position_metres.y +
                             kOriginWalkerEyeHeightMetres,
                         entry.foot_position_metres.z};
  const auto route = need(step_origin_gameplay_boarding(
      need(begin_origin_gameplay_boarding(binding, port, eye,
                                          entry.heading_radians,
                                          GameplayBoardingDirection::board)),
      kGameplayBoardingTicks));
  const FreedomBoardingState pilot{FreedomBoardingPhase::seated, entry,
                                   5000 - kGameplayBoardingTicks, route};
  origin.state.tick = 5000;
  FreedomFlightSaveDocument flight;
  flight.origin = origin;
  flight.model.physical_catalog = 2;
  flight.model.physical_ephemeris = 2;
  flight.model.terrain_policy = 3;
  flight.model.frame_policy = 2;
  flight.model.optics = 1;
  flight.model.companions = 1;
  flight.world = FreedomActiveWorldSelection{2, system.catalog.id,
                                             origin.recipe.home_planet};
  flight.flight = port.planet_relative;
  flight.flight.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  const auto home =
      need(find_local_system_planet(system, origin.recipe.home_planet));
  flight.flight.position_metres = {
      0, 0, static_cast<double>(home->descriptor.radius.value) * 10000};
  flight.flight.orientation = {};
  flight.flight.linear_velocity_metres_per_second = {};
  flight.flight.angular_velocity_radians_per_second = {};
  flight.flight = need(canonicalize_rigid_body_state(
      RigidBodyWorldContext{system}, flight.flight));
  const FreedomSurfaceSaveDocument surface{flight, {}};
  const FreedomResources resources{1,
                                   origin.state.craft,
                                   flight.flight.craft,
                                   5000,
                                   kFreedomFlightCapacityQuanta - 123,
                                   3};
  const FreedomResourceSaveDocument fueled{surface, resources};
  FreedomKnowledgeSaveDocument mapped{
      fueled, need(make_freedom_starting_knowledge(seed, 5))};
  FreedomTravelSaveDocument result{
      mapped, {}, NativeStartingAssemblySelection{}, pilot};
  result.travel.version = kFreedomComposedTravelStateVersion;
  result.travel.composed.emplace();
  return result;
}
auto system_leg(FreedomTravelSaveDocument& d, SystemId destination) -> void {
  auto& s = d.travel;
  s.composed->active = FreedomComposedRouteKind::system;
  s.composed->selected_body.reset();
  s.selected = destination;
  roundtrip(d);
  s.phase = FreedomJumpPhase::spool;
  s.spool_tick = flight_of(d).flight.tick;
  roundtrip(d);
  tick(d, s.spool_tick + kJumpSpoolTicks);
  const auto source = flight_of(d);
  const auto& ledger = d.voyage.voyage.resources;
  const auto frozen = need(freeze_freedom_jump_arrival(
      {ledger.drive ? kFreedomRecoveryJumpTargetingVersion
                    : kFreedomNetworkJumpTargetingVersion,
       source.origin.recipe.universe_seed, source.origin.state.craft,
       source.flight, destination, IntersystemRuleProfile::assisted,
       s.next_attempt, source.origin.lineage, source.origin.network,
       ledger.drive}));
  s.committed = frozen;
  s.commitment_resources = d.voyage.voyage.resources;
  if (ledger.drive)
    d.voyage.voyage.resources.drive = need(advance_freedom_drive_recovery(
        *ledger.drive, FreedomJumpRecoveryKind::system, s.next_attempt,
        frozen.recovery->source_checksum, source.flight.tick));
  --d.voyage.voyage.resources.jump_charges;
  ++s.next_attempt;
  s.phase = FreedomJumpPhase::transit;
  roundtrip(d);
  const auto network =
      need(generate_freedom_system_network(source.origin.recipe.universe_seed));
  const auto system =
      need(resolve_freedom_network_system(network, destination));
  const auto centre =
      need(resolve_planet_ephemeris(system, frozen.preview.reference_planet,
                                    {frozen.preview.arrival_tick, 0}));
  auto arrival = source;
  arrival.flight.tick = frozen.preview.arrival_tick;
  arrival.origin.state.tick = arrival.flight.tick;
  arrival.flight.frame = {RigidFrameKind::planet_relative_inertial,
                          destination,
                          frozen.preview.reference_planet,
                          {}};
  arrival.flight.position_metres = {frozen.point.x - centre.position.x,
                                    frozen.point.y - centre.position.y,
                                    frozen.point.z - centre.position.z};
  arrival.flight.linear_velocity_metres_per_second = {
      frozen.preview.arrival_velocity.x - centre.velocity.x,
      frozen.preview.arrival_velocity.y - centre.velocity.y,
      frozen.preview.arrival_velocity.z - centre.velocity.z};
  arrival.flight = need(canonicalize_rigid_body_state(
      RigidBodyWorldContext{system}, arrival.flight));
  arrival.world = FreedomActiveWorldSelection{2, destination,
                                              frozen.preview.reference_planet};
  arrival.model.hold = {};
  d.voyage.voyage.voyage.base = arrival;
  d.voyage.voyage.resources.tick = arrival.flight.tick;
  s.selected.reset();
  s.phase = FreedomJumpPhase::idle;
  s.spool_tick = 0;
  roundtrip(d);
}
auto home(const FreedomTravelSaveDocument& d) -> BodyTarget {
  const auto f = flight_of(d);
  return {generate_first_intersystem_identities(f.origin.recipe.universe_seed)
              .origin_system,
          body_id(f.origin.recipe.home_planet)};
}
auto foreign_target(const FreedomTravelSaveDocument& d) -> BodyTarget {
  const auto f = flight_of(d);
  const auto owner = need(hydrate_freedom_flight_document(f)).system;
  const auto catalog = need(generate_secondary_body_catalog(owner));
  return {owner.catalog.id, catalog.bodies.front().node.id};
}
auto foreign_leg(FreedomTravelSaveDocument& d, BodyTarget target) -> void {
  auto& s = d.travel;
  auto& c = *s.composed;
  c.active = FreedomComposedRouteKind::foreign_moon;
  c.selected_body = target;
  s.selected.reset();
  roundtrip(d);
  s.phase = FreedomJumpPhase::spool;
  s.spool_tick = flight_of(d).flight.tick;
  roundtrip(d);
  tick(d, s.spool_tick + kJumpSpoolTicks);
  const auto source = flight_of(d);
  auto profile =
      source.moon ? c.foreign_moon.outbound->request.home_model : source.model;
  if (!source.moon) profile.hold = {};
  const auto& before = d.voyage.voyage.resources;
  const auto frozen = need(freeze_local_body_jump_arrival(
      {before.drive ? kFreedomRecoveryForeignMoonJumpVersion
                    : kFreedomForeignMoonJumpVersion,
       source, target, s.next_attempt, profile, before.drive}));
  c.foreign_moon.committed = frozen;
  c.foreign_moon.commitment_resources = before;
  if (!source.moon) c.foreign_moon.outbound = frozen;
  if (before.drive)
    d.voyage.voyage.resources.drive = need(advance_freedom_drive_recovery(
        *before.drive, FreedomJumpRecoveryKind::moon, s.next_attempt,
        frozen.source_checksum, source.flight.tick));
  --d.voyage.voyage.resources.jump_charges;
  ++s.next_attempt;
  s.phase = FreedomJumpPhase::transit;
  roundtrip(d);
  tick(d, frozen.arrival_tick - 1);
  roundtrip(d);
  d.voyage.voyage.voyage.base = frozen.arrival;
  d.voyage.voyage.resources.tick = frozen.arrival_tick;
  s.phase = FreedomJumpPhase::idle;
  s.spool_tick = 0;
  c.selected_body.reset();
  roundtrip(d);
}
auto visit_system(FreedomTravelSaveDocument& d) -> void {
  // The same system-presence event granted by actual system arrival. This
  // does not fabricate planetary contact, landing or surface observation.
  const auto f = flight_of(d);
  const auto craft = f.origin.state.craft;
  d.voyage.knowledge = need(apply_freedom_knowledge(
      d.voyage.knowledge,
      {{KnowledgeSubjectKind::system, f.world->system, f.world->system.value},
       KnowledgeFact::presence,
       {NavigationKnowledgeLevel::visited, KnowledgeSource::physical_arrival,
        craft.value, f.flight.tick, craft}},
      f.flight.tick));
}
auto identity(const PhysicalMoonDescriptor& moon)
    -> MoonSurfaceLandmarkIdentity {
  // Bounded identity-only metadata fixture; no terrain/site relocation or
  // claim that this test walked to the generated exposure.
  const auto shell = static_cast<std::int32_t>(std::floor(
      static_cast<double>(moon.radius_metres) / kSurfaceLandmarkCellMetres));
  for (std::int32_t x = -1; x <= 1; ++x)
    for (std::int32_t y = -1; y <= 1; ++y)
      for (std::int32_t z = -1; z <= 1; ++z) {
        const auto found =
            need(identify_moon_surface_landmark(moon, {shell + x, y, z}));
        if (found) return *found;
      }
  throw std::runtime_error("No Moon identity in bounded fixture cells");
}
auto metadata(FreedomTravelSaveDocument& d) -> void {
  tick(d, flight_of(d).flight.tick + 1);
  const auto f = flight_of(d);
  const auto moon = *need(hydrate_freedom_flight_document(f)).moon;
  const auto site = identity(moon.moon);
  const auto now = f.flight.tick;
  const auto target = f.moon->target;
  auto& k = d.voyage.knowledge;
  k.surface_marker = FreedomSurfaceMarkerState{
      kFreedomNetworkMoonSurfaceMarkerVersion,
      FreedomMoonSurfaceMarkerPoint{target.system, target.body,
                                    site.reference_position_metres, now}};
  k.moon_surface_landmarks = FreedomMoonSurfaceLandmarkState{
      kFreedomNetworkMoonSurfaceLandmarkVersion,
      {{target, site.cell, site.id, kMoonTerrainRecipeVersion, now}}};
  const FreedomFieldSampleRecord sample{target,    site.cell,
                                        site.id,   kMoonTerrainRecipeVersion,
                                        site.kind, f.origin.state.craft,
                                        now};
  k.field_samples = FreedomFieldSampleState{kFreedomNetworkFieldSamplesVersion,
                                            f.origin.state.craft,
                                            {sample},
                                            {sample}};
  const auto origin =
      need(generate_physical_origin_system(f.origin.recipe.universe_seed, 2));
  const auto origin_catalog = need(generate_secondary_body_catalog(origin));
  const auto origin_moon = need(generate_physical_moon_descriptor(
      origin_catalog, origin_catalog.bodies.front().node.id));
  const auto origin_site = identity(origin_moon);
  const BodyTarget origin_target{origin.catalog.id, origin_moon.body};
  k.moon_surface_landmarks->inspections.push_back(
      {origin_target, origin_site.cell, origin_site.id,
       kMoonTerrainRecipeVersion, 7});
  k.field_samples->journal.push_back(
      {origin_target, origin_site.cell, origin_site.id,
       kMoonTerrainRecipeVersion, origin_site.kind, f.origin.state.craft, 7});
  std::ranges::sort(
      k.moon_surface_landmarks->inspections, {}, [](const auto& r) {
        return std::tuple{r.target.system.value, r.target.body.version,
                          r.target.body.kind, r.target.body.value,
                          r.site.value};
      });
  std::ranges::sort(k.field_samples->journal, {}, [](const auto& r) {
    return std::tuple{r.target.system.value,
                      static_cast<unsigned>(r.target.body.kind),
                      r.target.body.value, r.site.value};
  });
  const auto bytes = roundtrip(d);
  const auto original = need(encode_freedom_knowledge_json(k, now));
  check(need(decode_freedom_knowledge_json(original, now)) == k,
        "Foreign typed tools preserve the exact point, inspection and carried "
        "first note");
  for (unsigned fault = 0; fault < 8; ++fault) {
    auto bad = k;
    switch (fault) {
      case 0:
        bad.surface_marker->version = kFreedomMoonSurfaceMarkerVersion;
        break;
      case 1:
        bad.moon_surface_landmarks->version =
            kFreedomMoonSurfaceLandmarkVersion;
        break;
      case 2: bad.field_samples->version = kFreedomFieldSamplesVersion; break;
      case 3: bad.field_samples->carried.front().craft.value ^= 1U; break;
      case 4:
        for (auto& note : bad.field_samples->journal)
          if (note.target == target) ++note.tick;
        break;
      case 5:
        bad.moon_surface_landmarks->inspections.front().target.body.version = 2;
        break;
      case 6:
        bad.surface_marker->point = FreedomMoonSurfaceMarkerPoint{
            target.system,
            target.body,
            {std::numeric_limits<double>::quiet_NaN(), 0, 0},
            now};
        break;
      case 7:
        bad.field_samples->carried.front().target.system.value ^= 1U;
        break;
      default: break;
    }
    check(!encode_freedom_knowledge_json(bad, now),
          "Malformed/old-version foreign tool source refuses");
  }
  check(roundtrip(d) == bytes,
        "Refused codec candidates leave original complete Save exact");
  auto future_visit = std::make_unique<FreedomTravelSaveDocument>(d);
  const auto source_tick =
      d.travel.composed->foreign_moon.outbound->request.source.flight.tick;
  bool moved{};
  for (auto& entry : future_visit->voyage.knowledge.entries)
    if (entry.subject == KnowledgeSubject{KnowledgeSubjectKind::system,
                                          target.system, target.system.value} &&
        entry.fact == KnowledgeFact::presence)
      for (auto& evidence : entry.transitions)
        if (evidence.level == NavigationKnowledgeLevel::visited) {
          evidence.tick = source_tick + 1;
          moved = true;
        }
  need(validate_freedom_knowledge_document(future_visit->voyage));
  check(moved && !encode_freedom_travel_document_json(*future_visit),
        "Individually valid current visit evidence after departure cannot "
        "authorize the earlier Moon commitment");
  check(roundtrip(d) == bytes, "Later valid inspections and specimen notes "
                               "retain authentic historical outbound proof");
}
auto invalids(const FreedomTravelSaveDocument& d) -> void {
  const auto text = roundtrip(d);
  for (unsigned fault = 0; fault < 15; ++fault) {
    auto bad = Json::parse(text);
    auto& s = bad["travel"];
    auto& c = s["composed"];
    switch (fault) {
      case 0: bad["format_version"] = 56U; break;
      case 1: s["version"] = 7U; break;
      case 2: c["version"] = 1U; break;
      case 3: c.erase("foreign_moon"); break;
      case 4: c["foreign_moon"]["unknown"] = 0U; break;
      case 5: c["foreign_moon"]["commitment_resources"] = nullptr; break;
      case 6: c["foreign_moon"]["outbound"] = nullptr; break;
      case 7: ++s["next_attempt"].get_ref<Json::number_unsigned_t&>(); break;
      case 8: c["foreign_moon"]["committed"]["source_checksum"] = 0U; break;
      case 9: c["foreign_moon"]["committed"]["arrival_tick"] = 0U; break;
      case 10: c["foreign_moon"]["outbound"]["version"] = 1U; break;
      case 11:
        c["foreign_moon"]["committed"]["arrival"]["world_owner"]["version"] =
            1U;
        break;
      case 12: c["active"] = -1; break;
      case 13: c["active"] = std::numeric_limits<std::uint64_t>::max(); break;
      case 14:
        c["foreign_moon"]["committed"]["version"] =
            std::numeric_limits<std::uint64_t>::max();
        break;
      default: break;
    }
    check(!decode_freedom_travel_document_json(bad.dump()),
          "Closed61 refuses malformed versions, owners, clocks, receipts, bill "
          "and counter");
  }
}
auto recover(const FreedomTravelSaveDocument& d) -> void {
  FreedomRecoverySaveDocument wrapper{d, {}};
  const auto source = need(freedom_recovery_source_checksum(wrapper.voyage));
  const auto f = flight_of(d);
  const auto pending = need(
      record_freedom_loss(wrapper, FreedomLossCause::irrecoverable_destruction,
                          f.origin.state.craft, f.flight.tick, source));
  const auto replacement = need(complete_freedom_recovery(pending));
  const auto bytes = need(encode_freedom_recovery_document_json(replacement));
  const auto decoded = need(decode_freedom_recovery_document_json(bytes));
  check(decoded == replacement &&
            decoded.recovery.latest->retired == wrapper.voyage,
        "Recovery29 retains exact retired61 foreign receipts and specimen "
        "provenance");
  const auto& k = recovery_knowledge(decoded.voyage).knowledge;
  check(k.field_samples &&
            k.field_samples->version == kFreedomNetworkFieldSamplesVersion &&
            k.field_samples->carried.empty() &&
            k.field_samples->journal ==
                d.voyage.knowledge.field_samples->journal &&
            k.moon_surface_landmarks ==
                d.voyage.knowledge.moon_surface_landmarks &&
            k.surface_marker == d.voyage.knowledge.surface_marker,
        "Replacement retains selected foreign tools/notes while the carried "
        "pouch is lost");
  auto bad = replacement;
  std::get<FreedomKnowledgeSaveDocument>(bad.voyage)
      .knowledge.field_samples->journal.clear();
  check(!encode_freedom_recovery_document_json(bad),
        "Replacement cannot drop retired foreign first notes");
}
auto explicit_metadata_upgrade() -> void {
  auto d = std::make_unique<FreedomTravelSaveDocument>(fixture());
  const auto f = flight_of(*d);
  const auto owner =
      need(generate_physical_origin_system(f.origin.recipe.universe_seed, 2));
  const auto catalog = need(generate_secondary_body_catalog(owner));
  const auto moon = need(generate_physical_moon_descriptor(
      catalog, catalog.bodies.front().node.id));
  const auto site = identity(moon);
  const BodyTarget target{owner.catalog.id, moon.body};
  auto& k = d->voyage.knowledge;
  k.moon_surface_landmarks = FreedomMoonSurfaceLandmarkState{
      kFreedomMoonSurfaceLandmarkVersion,
      {{target, site.cell, site.id, kMoonTerrainRecipeVersion, 7}}};
  k.field_samples = FreedomFieldSampleState{
      kFreedomFieldSamplesVersion,
      f.origin.state.craft,
      {},
      {{target, site.cell, site.id, kMoonTerrainRecipeVersion, site.kind,
        f.origin.state.craft, 7}}};
  roundtrip(*d);
  // Preserve the complete supported starter owner, including authored craft
  // binding and its actual seated pilot, rather than stripping to raw Flight.
  FreedomRecoverySaveDocument wrapper{*d, {}};
  const auto pending = need(record_freedom_loss(
      wrapper, FreedomLossCause::irrecoverable_destruction,
      f.origin.state.craft, f.flight.tick,
      need(freedom_recovery_source_checksum(wrapper.voyage))));
  auto replacement = need(complete_freedom_recovery(pending));
  auto& upgraded =
      std::get<FreedomKnowledgeSaveDocument>(replacement.voyage).knowledge;
  upgraded.moon_surface_landmarks->version =
      kFreedomNetworkMoonSurfaceLandmarkVersion;
  upgraded.field_samples->version = kFreedomNetworkFieldSamplesVersion;
  const auto bytes = need(encode_freedom_recovery_document_json(replacement));
  check(need(decode_freedom_recovery_document_json(bytes)) == replacement &&
            upgraded.field_samples->journal == k.field_samples->journal &&
            upgraded.moon_surface_landmarks->inspections ==
                k.moon_surface_landmarks->inspections,
        "Explicit post-replacement network tool selection preserves exact old "
        "Origin records");
}
auto outing(std::uint32_t resource_version) -> void {
  // Saved-space/profile/ledger codec fixture only. Existing native tests own
  // actual thruster spool and landing/walking; no flown descent is asserted.
  auto d = std::make_unique<FreedomTravelSaveDocument>(fixture());
  const auto old = roundtrip(*d);
  check(!Json::parse(old)["travel"]["composed"].contains("foreign_moon"),
        "Historical composed1 bytes omit new checkpoint");
  d->travel.version = kFreedomForeignMoonTravelStateVersion;
  d->travel.composed->version = kFreedomForeignMoonComposedRoutesVersion;
  auto& resources = d->voyage.voyage.resources;
  resources.version = resource_version;
  if (resource_version != kFreedomResourceVersion)
    resources.drive = FreedomDriveRecoveryState{
        resource_version == kFreedomStrainedResourceVersion
            ? kFreedomStrainedDriveRecoveryVersion
            : kFreedomDriveRecoveryVersion,
        {},
        {}};
  roundtrip(*d);
  const auto target_system =
      generate_first_intersystem_identities({42}).target_system;
  system_leg(*d, target_system);
  visit_system(*d);
  const auto moon = foreign_target(*d);
  auto& parent =
      std::get<FreedomFlightSaveDocument>(d->voyage.voyage.voyage.base);
  tick(*d, parent.flight.tick + 1);
  // Keep the actual frozen foreign arrival; select a body-bound hold only
  // to verify that the expedition profile clears it without moving the craft.
  parent.model.hold.target =
      OrbitHoldTarget{parent.world->planet,
                      std::hypot(parent.flight.position_metres.x,
                                 parent.flight.position_metres.y,
                                 parent.flight.position_metres.z) +
                          1'000'000,
                      {0, 0, 1}};
  foreign_leg(*d, moon);
  check(d->travel.composed->foreign_moon.outbound->request.source.model.hold
                .target.has_value() &&
            !d->travel.composed->foreign_moon.outbound->request.home_model.hold
                 .target,
        "Foreign frozen decoder preserves actual source hold but clears "
        "expedition hold only");
  invalids(*d);
  metadata(*d);
  recover(*d);
  const auto notes = d->voyage.knowledge;
  foreign_leg(*d, home(*d));
  check(d->voyage.voyage.resources.jump_charges == 0 &&
            d->travel.next_attempt == 4 && d->voyage.knowledge == notes &&
            flight_of(*d).world->system == home(*d).system,
        "Three typed paid commitments return to real Home with exact tools and "
        "no fabricated parent visit");
  const auto retained = d->travel.composed->foreign_moon;
  tick(*d, flight_of(*d).flight.tick + 1);
  d->travel.composed->active = FreedomComposedRouteKind::system;
  d->travel.selected = target_system;
  roundtrip(*d);
  check(d->travel.composed->foreign_moon == retained,
        "Later ordinary route selection retains completed foreign excursion");
}
} // namespace
int main() {
  try {
    explicit_metadata_upgrade();
    for (const auto version :
         {kFreedomResourceVersion, kFreedomRecoveryResourceVersion,
          kFreedomStrainedResourceVersion})
      outing(version);
    std::cout << checks << " foreign Moon codec checks passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
