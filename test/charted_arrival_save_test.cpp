#include "apsis_drift/freedom_knowledge_save.hpp"
#include "apsis_drift/freedom_system_network.hpp"
#include "apsis_drift/freedom_travel_save.hpp"
#include "apsis_drift/native_craft_binding.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/native_startup.hpp"
#include "apsis_drift/save_file.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <source_location>
#include <stdexcept>
#include <string>
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
          const std::source_location where = std::source_location::current())
    -> T {
  if (!value) {
    std::cerr << where.line() << ": ";
    if constexpr (requires { value.error().detail; })
      std::cerr << value.error().detail;
    else if constexpr (std::is_same_v<E, std::string>)
      std::cerr << value.error();
    else
      std::cerr << static_cast<unsigned>(value.error());
    throw std::runtime_error("Charted arrival fixture refused");
  }
  if constexpr (!std::is_void_v<T>) {
    if constexpr (std::is_trivially_copyable_v<T>)
      return *value;
    else
      return std::move(*value);
  }
}
auto target() -> std::pair<SystemId, PlanetId> {
  const auto ids = generate_first_intersystem_identities(Seed{42});
  const auto system =
      need(generate_physical_local_system(ids.target_system_seed, 2));
  return {system.catalog.id, system.catalog.planets.front().descriptor.id};
}
auto searched_knowledge() -> const FreedomKnowledge& {
  // Pure knowledge acquisition only; this does not claim native flight or a
  // sensor-qualified physical journey. Preserve its required parent witness.
  static const auto searched = [] {
    auto knowledge = need(make_freedom_starting_knowledge(Seed{42}, 6));
    const auto network = need(generate_freedom_system_network(
        Seed{42}, kFreedomUnchartedSystemNetworkVersion));
    const auto source = network.nodes.front().system;
    const auto craft = make_freedom_new_game_document(Seed{42}).state.craft;
    knowledge = need(begin_freedom_star_search(knowledge, source, craft, 0));
    for (SimulationTick tick = 1; tick <= kFreedomStarSearchTicks; ++tick)
      knowledge = need(
          advance_freedom_star_search(knowledge, source, craft, tick, true));
    return knowledge;
  }();
  return searched;
}
auto uncharted_target() -> std::pair<SystemId, PlanetId> {
  const auto network = need(generate_freedom_system_network(
      Seed{42}, kFreedomUnchartedSystemNetworkVersion));
  const auto system = need(resolve_freedom_network_system(
      network, searched_knowledge().star_search->completed.front().target));
  return {system.catalog.id, system.catalog.planets.front().descriptor.id};
}
auto uncharted_evidence(SimulationTick planet_tick) -> FreedomKnowledge {
  auto knowledge = searched_knowledge();
  const auto [system, planet] = uncharted_target();
  const auto craft = make_freedom_new_game_document(Seed{42}).state.craft;
  // Explicit metadata boundary, not a flown planetary observation. Preserve
  // every mandatory starting grant and the completed parent search witness.
  for (const auto fact :
       {KnowledgeFact::identity_type, KnowledgeFact::location})
    knowledge = need(apply_freedom_knowledge(
        knowledge,
        {{KnowledgeSubjectKind::planet, system, planet.value},
         fact,
         {NavigationKnowledgeLevel::resolved, KnowledgeSource::local_ship,
          craft.value, planet_tick, craft}},
        planet_tick));
  return knowledge;
}
auto later_evidence(FreedomKnowledge knowledge, SimulationTick tick)
    -> FreedomKnowledge {
  const auto [system, planet] = target();
  const auto craft = make_freedom_new_game_document(Seed{42}).state.craft;
  return need(apply_freedom_knowledge(
      knowledge,
      {{KnowledgeSubjectKind::planet, system, planet.value},
       KnowledgeFact::physical,
       {NavigationKnowledgeLevel::resolved, KnowledgeSource::local_ship,
        craft.value, tick, craft}},
      tick));
}
auto personal_and_historical() -> void {
  const auto [system, planet] = target();
  for (const auto recipe : {5U, 6U}) {
    auto knowledge = need(make_freedom_starting_knowledge(Seed{42}, recipe));
    const auto absent = need(encode_freedom_knowledge_json(knowledge, 100));
    check(!Json::parse(absent).contains("charted_arrivals"),
          "Historical personal absence has no JSON key");
    knowledge.charted_arrivals.emplace();
    const auto enabled = need(encode_freedom_knowledge_json(knowledge, 100));
    check(need(decode_freedom_knowledge_json(enabled, 100)) == knowledge,
          "Closed preference roundtrips on both existing planetary charts");
    for (const auto& invalid :
         {Json(nullptr), Json(false), Json::object(), Json{{"version", 0U}},
          Json{{"version", 2U}}, Json{{"version", 1.0}},
          Json{{"version", 4'294'967'297ULL}},
          Json{{"version", 1U}, {"extra", true}}}) {
      auto root = Json::parse(enabled);
      root["charted_arrivals"] = invalid;
      check(!decode_freedom_knowledge_json(root.dump(), 100),
            "Unknown, wide, float and open personal selections refuse");
    }
    knowledge.charted_arrivals.reset();
    check(need(encode_freedom_knowledge_json(knowledge, 100)) == absent,
          "Explicit removal restores original personal-absence bytes");
  }
  auto legacy = need(make_freedom_starting_knowledge(Seed{42}, 4));
  legacy.charted_arrivals.emplace();
  check(!validate_freedom_knowledge(legacy, 100),
        "Four-system-only chart cannot silently adopt this preference");
  auto historical =
      later_evidence(need(make_freedom_starting_knowledge(Seed{42}, 5)), 100);
  check(need(freedom_charted_arrival_known_at(historical, system, planet, 0,
                                              100)) &&
            !query_freedom_knowledge(
                historical,
                {KnowledgeSubjectKind::planet, system, planet.value},
                KnowledgeFact::identity_type, 0),
        "Later valid unrelated evidence does not erase genuinely earlier "
        "resolved chart proof");
  const auto [uncharted_system, uncharted_planet] = uncharted_target();
  const auto unseen = need(make_freedom_starting_knowledge(Seed{42}, 6));
  check(!need(freedom_charted_arrival_known_at(unseen, uncharted_system,
                                               uncharted_planet, 99, 100)),
        "An initially uncharted primary has no starting chart proof");
  historical = uncharted_evidence(1300);
  check(!need(freedom_charted_arrival_known_at(historical, uncharted_system,
                                               uncharted_planet, 1299, 1300)) &&
            need(freedom_charted_arrival_known_at(
                historical, uncharted_system, uncharted_planet, 1300, 1300)) &&
            !freedom_charted_arrival_known_at(historical, uncharted_system,
                                              uncharted_planet, 1301, 1300),
        "Only evidence at or before the source tick qualifies, after "
        "current-clock admission");
}
auto fixture(unsigned drive) -> FreedomTravelSaveDocument {
  // Explicit codec-only saved-space fixture. The seated pilot uses the genuine
  // station walk/boarding route; no flown ascent, waiting or landing is
  // claimed.
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
  FreedomFlightSaveDocument flight;
  origin.state.tick = 5000;
  flight.origin = origin;
  flight.flight = port.planet_relative;
  flight.flight.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  const auto home =
      need(find_local_system_planet(system, origin.recipe.home_planet));
  flight.flight.position_metres = {0, 0,
                                   home->descriptor.radius.value * 10000.0};
  flight.flight.orientation = {};
  flight.flight.linear_velocity_metres_per_second = {};
  flight.flight.angular_velocity_radians_per_second = {};
  flight.flight = need(canonicalize_rigid_body_state({system}, flight.flight));
  flight.model.physical_catalog = 2;
  flight.model.physical_ephemeris = 2;
  flight.model.terrain_policy = 3;
  flight.model.frame_policy = 2;
  flight.model.optics = 1;
  flight.model.companions = 1;
  flight.world = FreedomActiveWorldSelection{2, system.catalog.id,
                                             origin.recipe.home_planet};
  FreedomResources resources{1,
                             origin.state.craft,
                             flight.flight.craft,
                             5000,
                             kFreedomFlightCapacityQuanta - 123,
                             3};
  if (drive) {
    resources.version = drive == 1 ? kFreedomRecoveryResourceVersion
                                   : kFreedomStrainedResourceVersion;
    resources.drive = FreedomDriveRecoveryState{};
    resources.drive->version = drive == 1
                                   ? kFreedomDriveRecoveryVersion
                                   : kFreedomStrainedDriveRecoveryVersion;
  }
  FreedomTravelSaveDocument document{
      {{{flight, {}}, resources},
       need(make_freedom_starting_knowledge(seed, 5))},
      {},
      NativeStartingAssemblySelection{},
      FreedomBoardingState{FreedomBoardingPhase::seated, entry,
                           5000 - kGameplayBoardingTicks, route}};
  document.travel.version = drive == 0   ? kFreedomComposedTravelStateVersion
                            : drive == 1 ? kFreedomRecoveringTravelStateVersion
                                         : kFreedomStrainedTravelStateVersion;
  document.travel.composed.emplace();
  document.voyage.knowledge.charted_arrivals.emplace();
  return document;
}
auto tick(FreedomTravelSaveDocument& document, SimulationTick at) -> void {
  auto& flight =
      std::get<FreedomFlightSaveDocument>(document.voyage.voyage.voyage.base);
  flight.flight.tick = at;
  flight.origin.state.tick = at;
  document.voyage.voyage.resources.tick = at;
}
auto bytes(const FreedomTravelSaveDocument& document) -> std::string {
  const auto text = need(encode_freedom_travel_document_json(document));
  check(need(decode_freedom_travel_document_json(text)) == document,
        "Request policy, current personal state and existing outer owner "
        "roundtrip exactly");
  return text;
}
auto commitment(unsigned drive, bool charted,
                SimulationTick uncharted_evidence_tick = 0)
    -> FreedomTravelSaveDocument {
  auto document = fixture(drive);
  if (uncharted_evidence_tick) {
    // Explicit network2 ledger boundary. The same canonical receipt is used
    // with planetary evidence before or after commitment.
    auto& flight =
        std::get<FreedomFlightSaveDocument>(document.voyage.voyage.voyage.base);
    flight.origin.network->version = kFreedomUnchartedSystemNetworkVersion;
    document.voyage.knowledge = uncharted_evidence(uncharted_evidence_tick);
    document.voyage.knowledge.charted_arrivals.emplace();
  }
  const auto [system, planet] =
      uncharted_evidence_tick ? uncharted_target() : target();
  (void)planet;
  document.travel.selected = system;
  document.travel.phase = FreedomJumpPhase::spool;
  document.travel.spool_tick = 5000;
  tick(document, 5360);
  auto& resources = document.voyage.voyage.resources;
  const auto flight = surface_base_flight(document.voyage.voyage.voyage.base);
  FreedomJumpRequest request;
  request.version = charted ? kFreedomChartedJumpTargetingVersion
                    : drive ? kFreedomRecoveryJumpTargetingVersion
                            : kFreedomNetworkJumpTargetingVersion;
  request.universe_seed = flight.origin.recipe.universe_seed;
  request.craft = flight.origin.state.craft;
  request.source = flight.flight;
  request.destination = system;
  request.profile = IntersystemRuleProfile::assisted;
  request.attempt = 1;
  request.lineage = flight.origin.lineage;
  request.network = flight.origin.network;
  request.recovery = resources.drive;
  if (charted) request.charted_arrival.emplace();
  const auto frozen = need(freeze_freedom_jump_arrival(request));
  document.travel.committed = frozen;
  document.travel.commitment_resources = resources;
  if (resources.drive)
    resources.drive = need(advance_freedom_drive_recovery(
        *resources.drive, FreedomJumpRecoveryKind::system, 1,
        frozen.recovery->source_checksum, request.source.tick));
  --resources.jump_charges;
  document.travel.next_attempt = 2;
  document.travel.phase = FreedomJumpPhase::transit;
  if (uncharted_evidence_tick <= 5360) (void)bytes(document);
  return document;
}
auto receipts_and_corruption() -> void {
  for (unsigned drive = 0; drive < 3; ++drive) {
    auto document = commitment(drive, true);
    const auto text = bytes(document);
    const auto root = Json::parse(text);
    check(root["travel"]["committed"]["request"]["version"] == 4U &&
              root["travel"]["committed"]["request"]["charted_arrival"] ==
                  Json{{"version", 1U}},
          "Only request4 has the exact closed arrival child, independent of "
          "drive choice");
    document.voyage.knowledge.charted_arrivals.reset();
    (void)bytes(document);
    tick(document, 5480);
    document.voyage.knowledge = later_evidence(document.voyage.knowledge, 5400);
    (void)bytes(document);
    check(
        document.travel.committed->preview.request.charted_arrival.has_value(),
        "Later unrelated knowledge and current opt-out cannot rewrite a frozen "
        "request4");
    for (unsigned fault = 0; fault < 7; ++fault) {
      auto bad = root;
      auto& request = bad["travel"]["committed"]["request"];
      switch (fault) {
        case 0: request.erase("charted_arrival"); break;
        case 1: request["charted_arrival"]["version"] = 2U; break;
        case 2: request["charted_arrival"]["version"] = 4'294'967'297ULL; break;
        case 3: request["charted_arrival"]["extra"] = true; break;
        case 4: request["charted_arrival"] = nullptr; break;
        case 5: request.erase("network"); break;
        case 6: request["version"] = drive ? 3U : 2U; break;
        default: break;
      }
      check(!decode_freedom_travel_document_json(bad.dump()),
            "Malformed selection, missing network and old request adopting the "
            "child refuse");
    }
    auto legacy = commitment(drive, false);
    const auto old = bytes(legacy);
    check(!Json::parse(old)["travel"]["committed"]["request"].contains(
              "charted_arrival"),
          "Historical request2/3 remains its exact schema even while current "
          "preference is enabled");
    legacy.voyage.knowledge.charted_arrivals.reset();
    (void)bytes(legacy);
    auto known = commitment(drive, true, 5300);
    tick(known, 5480);
    (void)bytes(known);
    auto future = commitment(drive, true, 5400);
    tick(future, 5480);
    check(known.travel == future.travel &&
              known.voyage.voyage == future.voyage.voyage &&
              known.craft_binding == future.craft_binding &&
              known.seated_pilot == future.seated_pilot,
          "Before/after evidence controls retain identical receipt, geometry, "
          "resources and pilot");
    need(validate_freedom_knowledge(future.voyage.knowledge, 5480));
    check(!validate_freedom_travel_document(future),
          "A regenerated request4 cannot use future-only identity/location "
          "evidence");
  }
}
auto native_setter_and_replacement() -> void {
  auto live = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_planetary_network_new_game(Seed{42})))));
  const auto original =
      need(encode_native_save_document_json(need(live->save_document())));
  need(live->set_charted_arrivals(false));
  check(need(encode_native_save_document_json(need(live->save_document()))) ==
            original,
        "Unselected idempotent setter keeps full historical native bytes");
  need(live->set_charted_arrivals(true));
  const auto enabled =
      need(encode_native_save_document_json(need(live->save_document())));
  need(live->set_charted_arrivals(true));
  check(live->document().flight.tick == 0 &&
            live->knowledge()->charted_arrivals &&
            need(encode_native_save_document_json(
                need(live->save_document()))) == enabled &&
            !need(live->charted_arrival_view()).eligible,
        "Explicit enrollment and reads are zero-time/idempotent and do not "
        "fabricate a destination");
  // Explicitly classified recoverable loss, not a physical collision claim.
  need(live->record_loss(FreedomLossCause::recoverable_destruction,
                         live->document().origin.state.craft,
                         live->document().flight.tick,
                         need(rigid_body_state_checksum(
                             {live->system()}, live->document().flight))));
  const auto pending =
      need(encode_native_save_document_json(need(live->save_document())));
  check(!live->set_charted_arrivals(false) &&
            need(encode_native_save_document_json(
                need(live->save_document()))) == pending,
        "Pending loss refuses preference changes without rewriting the retired "
        "save");
  need(live->complete_recovery());
  const auto recovered_tick = live->document().flight.tick;
  check(live->knowledge()->charted_arrivals.has_value(),
        "Actual replacement retains the enabled personal preference");
  need(live->set_charted_arrivals(false));
  check(!live->knowledge()->charted_arrivals &&
            live->document().flight.tick == recovered_tick,
        "Retired preference does not permanently lock later explicit opt-out");
  const auto saved = need(live->save_document());
  auto restored = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(saved))));
  check(!restored.knowledge()->charted_arrivals &&
            restored.resources() == live->resources(),
        "Complete replacement Save/Continue retains later explicit absence and "
        "actual stores");
}
} // namespace

auto main() -> int {
  try {
    personal_and_historical();
    receipts_and_corruption();
    native_setter_and_replacement();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << checks << " charted arrival persistence checks, 0 failures\n";
  return 0;
}
