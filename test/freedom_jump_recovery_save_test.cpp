#include "apsis_drift/freedom_condition_save.hpp"
#include "apsis_drift/freedom_system_network.hpp"
#include "apsis_drift/native_craft_binding.hpp"
#include "apsis_drift/save_file.hpp"

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
auto need(std::expected<T, E> result,
          std::source_location where = std::source_location::current()) -> T {
  if (!result) {
    std::cerr << where.file_name() << ':' << where.line() << ' ';
    if constexpr (requires { result.error().detail; })
      std::cerr << result.error().detail;
    else if constexpr (std::is_enum_v<E>)
      std::cerr << static_cast<unsigned>(result.error());
    std::cerr << '\n';
    throw std::runtime_error("Recovery save fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*result);
}
auto flight_of(const FreedomTravelSaveDocument& d)
    -> FreedomFlightSaveDocument {
  return surface_base_flight(d.voyage.voyage.voyage.base);
}
auto tick(FreedomTravelSaveDocument& d, SimulationTick at) -> void {
  auto& f = std::get<FreedomFlightSaveDocument>(d.voyage.voyage.voyage.base);
  f.flight.tick = at;
  f.origin.state.tick = at;
  d.voyage.voyage.resources.tick = at;
}
auto bytes(const FreedomTravelSaveDocument& d) -> std::string {
  const auto text = need(encode_freedom_travel_document_json(d));
  const auto restored = need(decode_freedom_travel_document_json(text));
  check(restored == d &&
            need(encode_freedom_travel_document_json(restored)) == text &&
            std::get<FreedomTravelSaveDocument>(
                need(decode_native_save_document_json(text))) == d,
        "Recovery57 full-save Continue preserves actual typed owner, "
        "readiness, frozen outcome and bill");
  check(Json::parse(text)["format_version"] ==
            kFreedomRecoveringTravelSaveFormatVersion,
        "Recovery travel has explicit outer57");
  return text;
}
auto fixture(unsigned chart) -> FreedomTravelSaveDocument {
  // Explicit codec-only saved-space setup with a genuine completed station
  // boarding pilot. No flown ascent, waiting, service or descent is claimed.
  const Seed seed{42};
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
  FreedomFlightSaveDocument f;
  f.origin = chart ? need(make_freedom_network_new_game_document(seed))
                   : make_freedom_new_game_document(seed);
  f.origin.state.tick = 5000;
  f.flight = port.planet_relative;
  f.flight.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  f.flight.position_metres = {
      0, 0, system.catalog.planets[0].descriptor.radius.value * 10000.0};
  f.flight.linear_velocity_metres_per_second = {};
  f.flight.orientation = {};
  f.flight.angular_velocity_radians_per_second = {};
  f.model.physical_catalog = 2;
  f.model.physical_ephemeris = 2;
  f.model.terrain_policy = 3;
  f.model.frame_policy = 2;
  f.model.optics = 1;
  f.model.companions = 1;
  f.world = FreedomActiveWorldSelection{chart ? 2U : 1U, system.catalog.id,
                                        f.origin.recipe.home_planet};
  FreedomResources ledger{2,
                          f.origin.state.craft,
                          f.flight.craft,
                          5000,
                          kFreedomFlightCapacityQuanta - 123,
                          3,
                          FreedomDriveRecoveryState{}};
  const FreedomSurfaceSaveDocument surface{f, {}};
  const FreedomResourceSaveDocument fueled{surface, ledger};
  const FreedomKnowledgeSaveDocument mapped{
      fueled, need(make_freedom_starting_knowledge(seed, chart == 2   ? 5U
                                                         : chart == 1 ? 4U
                                                                      : 3U))};
  FreedomTravelSaveDocument d{
      mapped,
      {},
      NativeStartingAssemblySelection{},
      FreedomBoardingState{FreedomBoardingPhase::seated, entry,
                           5000 - kGameplayBoardingTicks, route}};
  d.travel.version = kFreedomRecoveringTravelStateVersion;
  d.travel.composed.emplace();
  return d;
}
auto target(const FreedomTravelSaveDocument& d, FreedomComposedRouteKind kind,
            bool home = false) -> BodyTarget {
  const auto f = flight_of(d);
  const auto system =
      need(generate_physical_origin_system(f.origin.recipe.universe_seed, 2));
  if (home) return {system.catalog.id, body_id(f.origin.recipe.home_planet)};
  if (kind == FreedomComposedRouteKind::planet)
    return {system.catalog.id,
            body_id(system.catalog.planets[1].descriptor.id)};
  const auto catalog = need(generate_secondary_body_catalog(system));
  return {
      system.catalog.id,
      catalog.bodies[kind == FreedomComposedRouteKind::moon ? 0 : 1].node.id};
}
auto body_leg(FreedomTravelSaveDocument& d, FreedomComposedRouteKind kind,
              BodyTarget destination) -> FreedomTravelSaveDocument {
  auto& s = d.travel;
  auto& c = *s.composed;
  c.active = kind;
  c.selected_body = destination;
  s.selected.reset();
  s.phase = FreedomJumpPhase::spool;
  s.spool_tick = flight_of(d).flight.tick;
  bytes(d);
  tick(d, s.spool_tick + kJumpSpoolTicks);
  const auto source = flight_of(d);
  const auto& before = d.voyage.voyage.resources;
  FreedomFlightSaveDocument arrival;
  std::uint64_t checksum{};
  if (kind == FreedomComposedRouteKind::planet) {
    c.planet_committed = need(freeze_freedom_planet_jump_arrival(
        {2, source, destination, s.next_attempt, before.drive}));
    c.planet_commitment_resources = before;
    arrival = c.planet_committed->arrival;
    checksum = c.planet_committed->source_checksum;
  } else {
    auto& slot = kind == FreedomComposedRouteKind::moon ? c.moon : c.minor;
    const auto model = source.moon || source.minor
                           ? slot.outbound->request.home_model
                           : source.model;
    const auto frozen = need(freeze_local_body_jump_arrival(
        {kind == FreedomComposedRouteKind::moon ? 3U : 4U, source, destination,
         s.next_attempt, model, before.drive}));
    slot.committed = frozen;
    slot.commitment_resources = before;
    if (!source.moon && !source.minor) slot.outbound = frozen;
    arrival = frozen.arrival;
    checksum = frozen.source_checksum;
  }
  const auto drive_kind =
      kind == FreedomComposedRouteKind::planet ? FreedomJumpRecoveryKind::planet
      : kind == FreedomComposedRouteKind::moon ? FreedomJumpRecoveryKind::moon
                                               : FreedomJumpRecoveryKind::minor;
  d.voyage.voyage.resources.drive->last_use =
      need(make_freedom_jump_recovery_stamp(drive_kind, s.next_attempt,
                                            checksum, source.flight.tick));
  --d.voyage.voyage.resources.jump_charges;
  ++s.next_attempt;
  s.phase = FreedomJumpPhase::transit;
  bytes(d);
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Preserve
  // committed negative fixture
  const auto committed = d;
  tick(d, arrival.flight.tick - 1);
  bytes(d);
  d.voyage.voyage.voyage.base = arrival;
  d.voyage.voyage.resources.tick = arrival.flight.tick;
  s.phase = FreedomJumpPhase::idle;
  s.spool_tick = 0;
  c.selected_body.reset();
  bytes(d);
  return committed;
}
auto system_leg(FreedomTravelSaveDocument& d, SystemId destination) -> void {
  auto& s = d.travel;
  s.composed->active = FreedomComposedRouteKind::system;
  s.composed->selected_body.reset();
  s.selected = destination;
  s.phase = FreedomJumpPhase::spool;
  s.spool_tick = flight_of(d).flight.tick;
  bytes(d);
  tick(d, s.spool_tick + kJumpSpoolTicks);
  const auto source = flight_of(d);
  const auto& bill = d.voyage.voyage.resources;
  s.committed = need(freeze_freedom_jump_arrival(
      {3, source.origin.recipe.universe_seed, source.origin.state.craft,
       source.flight, destination, IntersystemRuleProfile::assisted,
       s.next_attempt, source.origin.lineage, source.origin.network,
       bill.drive}));
  s.commitment_resources = bill;
  const auto& frozen = *s.committed;
  d.voyage.voyage.resources.drive->last_use =
      need(make_freedom_jump_recovery_stamp(
          FreedomJumpRecoveryKind::system, s.next_attempt,
          frozen.recovery->source_checksum, source.flight.tick));
  --d.voyage.voyage.resources.jump_charges;
  ++s.next_attempt;
  s.phase = FreedomJumpPhase::transit;
  bytes(d);
  const auto ids =
      generate_first_intersystem_identities(source.origin.recipe.universe_seed);
  const auto system = source.origin.network
                          ? need(resolve_freedom_network_system(
                                need(generate_freedom_system_network(
                                    source.origin.recipe.universe_seed)),
                                destination))
                          : (destination == ids.origin_system
                                 ? need(generate_physical_origin_system(
                                       source.origin.recipe.universe_seed, 2))
                                 : need(generate_physical_local_system(
                                       ids.target_system_seed, 2)));
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
  arrival.world =
      FreedomActiveWorldSelection{source.origin.network ? 2U : 1U, destination,
                                  frozen.preview.reference_planet};
  arrival.model.hold = {};
  d.voyage.voyage.voyage.base = arrival;
  d.voyage.voyage.resources.tick = arrival.flight.tick;
  s.phase = FreedomJumpPhase::idle;
  s.spool_tick = 0;
  s.selected.reset();
  bytes(d);
}
auto budget(FreedomTravelSaveDocument& d) -> void {
  // Labelled codec ledger budget; NOT proof of actual attached service.
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Retained
  // readiness witness
  const auto state = d.voyage.voyage.resources.drive;
  tick(d, flight_of(d).flight.tick + 1);
  d.voyage.voyage.resources.jump_charges = 3;
  check(d.voyage.voyage.resources.drive == state,
        "Codec quantity budget cannot reset readiness");
}
auto malformed(const FreedomTravelSaveDocument& transit) -> void {
  // Generic inner codecs must support stamped ledgers inside travel57, but
  // those same bytes cannot be stripped into an independently playable root.
  const auto plain_knowledge =
      need(encode_freedom_knowledge_document_json(transit.voyage));
  const auto plain_resources =
      need(encode_freedom_resource_document_json(transit.voyage.voyage));
  check(!decode_native_save_document_json(plain_knowledge) &&
            !decode_native_save_document_json(plain_resources) &&
            !encode_native_save_document_json(
                NativeSaveDocument{transit.voyage}) &&
            !encode_native_save_document_json(
                NativeSaveDocument{transit.voyage.voyage}),
        "Stamped resource24/knowledge25 cannot discard their authoritative "
        "travel57 owner");
  FreedomRecoverySaveDocument stripped{transit.voyage, {}};
  check(!validate_freedom_recovery_document(stripped),
        "A recovery wrapper cannot hide a stripped stamped knowledge owner");
  FreedomConditionSaveDocument stripped_condition{
      1, transit.voyage, need(make_craft_condition(flight_of(transit))), {}};
  check(!validate_freedom_condition_document(stripped_condition),
        "A condition wrapper cannot hide a stripped stamped knowledge owner");
  for (unsigned fault = 0; fault < 13; ++fault) {
    auto bad = transit;
    auto& s = bad.travel;
    auto& ledger = bad.voyage.voyage.resources;
    switch (fault) {
      case 0: s.version = 5; break;
      case 1: ledger.version = 1; break;
      case 2: ledger.drive.reset(); break;
      case 3: ledger.drive->last_use.reset(); break;
      case 4:
        ledger.drive->last_use->kind = FreedomJumpRecoveryKind::minor;
        break;
      case 5: ++ledger.drive->last_use->source_checksum; break;
      case 6: ++ledger.drive->last_use->arrival_tick; break;
      case 7: ledger.drive->lineage = FreedomCraftLineage{1, 1}; break;
      case 8:
        s.composed->planet_commitment_resources->drive = ledger.drive;
        break;
      case 9: s.composed->planet_committed->request.recovery.reset(); break;
      case 10:
        s.composed->planet_committed->recovery->offset_metres.x += 1;
        break;
      case 11: s.composed->planet_commitment_resources->version = 1; break;
      case 12: ++s.next_attempt; break;
      default: break;
    }
    check(!validate_freedom_travel_document(bad) &&
              !encode_freedom_travel_document_json(bad),
          "Inconsistent current use, bill, lineage, policy and frozen result "
          "refuse");
  }
  const auto root = Json::parse(bytes(transit));
  for (unsigned fault = 0; fault < 12; ++fault) {
    auto bad = root;
    auto& drive = bad["voyage"]["voyage"]["resources"]["drive"];
    switch (fault) {
      case 0: bad["format_version"] = 56U; break;
      case 1: bad["travel"]["version"] = 5U; break;
      case 2: drive["extra"] = true; break;
      case 3: drive["last_use"]["kind"] = 4U; break;
      case 4: drive["last_use"]["attempt"] = 1.5; break;
      case 5: drive["lineage"] = {{"version", 1U}, {"generation", "01"}}; break;
      case 6: drive = nullptr; break;
      case 7:
        bad["travel"]["composed"]["planet_committed"]["recovery_input"] =
            nullptr;
        break;
      case 8:
        bad["travel"]["composed"]["planet_committed"]["recovery"]["extra"] = 0U;
        break;
      case 9:
        bad["travel"]["composed"]["planet_committed"]["recovery"]
           ["offset_metres"] = Json::array({nullptr, 0, 0});
        break;
      case 10:
        bad["travel"]["composed"]["planet_committed"]["recovery"]
           ["extra_radius_millimetres"] = 2000001U;
        break;
      case 11:
        bad["travel"]["composed"]["planet_committed"]["recovery"]
           ["sample_seed"] = 0U;
        break;
      default: break;
    }
    check(!decode_freedom_travel_document_json(bad.dump()),
          "Closed recovery JSON refuses malformed nested samples/stamps");
  }
}
auto replacement(const FreedomTravelSaveDocument& d) -> void {
  FreedomRecoverySaveDocument original{d, {}};
  const auto source = flight_of(d);
  const auto checksum = need(freedom_recovery_source_checksum(original.voyage));
  const auto pending = need(record_freedom_loss(
      original, FreedomLossCause::irrecoverable_destruction,
      source.origin.state.craft, source.flight.tick, checksum));
  const auto completed = need(complete_freedom_recovery(pending));
  const auto& ledger = recovery_knowledge(completed.voyage).voyage.resources;
  check(
      completed.recovery.latest->retired == pending.recovery.latest->retired &&
          ledger.version == 2 && ledger.drive && !ledger.drive->last_use &&
          ledger.drive->lineage ==
              recovery_flight(completed.voyage).origin.lineage &&
          ledger.craft != d.voyage.voyage.resources.craft &&
          need(freedom_jump_recovery_reading(*ledger.drive, ledger.tick)).ready,
      "Replacement starts selected new craft ready while full retired "
      "debt/outcome stays exact");
  const auto encoded = need(encode_freedom_recovery_document_json(completed));
  check(need(decode_freedom_recovery_document_json(encoded)) == completed,
        "Replacement resource2 and retired57 Continue exact");
  auto dropped = completed;
  auto& dropped_resources =
      std::get<FreedomKnowledgeSaveDocument>(dropped.voyage).voyage.resources;
  dropped_resources.version = 1;
  dropped_resources.drive.reset();
  check(!validate_freedom_recovery_document(dropped),
        "Replacement cannot silently discard the selected drive policy");
  FreedomConditionSaveDocument condition{
      1,
      completed,
      need(make_craft_condition(recovery_flight(completed.voyage))),
      {}};
  const auto nested = need(encode_freedom_condition_document_json(condition));
  check(need(decode_freedom_condition_document_json(nested)) == condition,
        "Existing condition34/recovery29 retain nested57 without confusing "
        "impact condition and drive state");
}
auto run() -> void {
  for (unsigned chart = 0; chart < 2; ++chart) {
    auto d = fixture(chart);
    bytes(d);
    const auto ids = generate_first_intersystem_identities(Seed{42});
    system_leg(d, ids.target_system);
    system_leg(d, ids.origin_system);
    check(d.voyage.knowledge.recipe.version == (chart ? 4U : 3U),
          "Drive selection cannot enlarge or change chart knowledge");
    body_leg(d, FreedomComposedRouteKind::moon,
             target(d, FreedomComposedRouteKind::moon));
  }
  auto d = fixture(2);
  auto historical = d;
  historical.travel.version = 5;
  historical.voyage.voyage.resources.version = 1;
  historical.voyage.voyage.resources.drive.reset();
  const auto old = need(encode_freedom_travel_document_json(historical));
  check(Json::parse(old)["format_version"] == 56 &&
            need(encode_freedom_travel_document_json(
                need(decode_freedom_travel_document_json(old)))) == old,
        "Unselected old56 retains exact bytes and no inferred drive use");
  const auto transit = body_leg(d, FreedomComposedRouteKind::planet,
                                target(d, FreedomComposedRouteKind::planet));
  malformed(transit);
  body_leg(d, FreedomComposedRouteKind::planet,
           target(d, FreedomComposedRouteKind::planet, true));
  budget(d);
  body_leg(d, FreedomComposedRouteKind::moon,
           target(d, FreedomComposedRouteKind::moon));
  body_leg(d, FreedomComposedRouteKind::moon,
           target(d, FreedomComposedRouteKind::moon, true));
  budget(d);
  body_leg(d, FreedomComposedRouteKind::minor,
           target(d, FreedomComposedRouteKind::minor));
  body_leg(d, FreedomComposedRouteKind::minor,
           target(d, FreedomComposedRouteKind::minor, true));
  budget(d);
  const auto ids = generate_first_intersystem_identities(Seed{42});
  system_leg(d, ids.target_system);
  system_leg(d, ids.origin_system);
  check(d.voyage.voyage.resources.drive->last_use->kind ==
                FreedomJumpRecoveryKind::system &&
            d.travel.composed->planet_committed->recovery &&
            d.travel.composed->moon.committed->recovery &&
            d.travel.composed->minor.committed->recovery,
        "Four fixed typed histories share one latest drive use and retain "
        "their independent frozen spread");
  replacement(d);
}
} // namespace
int main() {
  try {
    run();
    std::cout << "PASS jump recovery save " << checks << " checks\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL jump recovery save: " << e.what() << '\n';
    return 1;
  }
}
