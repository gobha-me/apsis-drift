#include "apsis_drift/freedom_condition_save.hpp"
#include "jump_strain_fixture.hpp"

#include <nlohmann/json.hpp>

namespace {
using namespace jump_strain_fixture;
using Json = nlohmann::ordered_json;
auto roundtrip(const FreedomTravelSaveDocument& document) -> std::string {
  const auto text = need(encode_freedom_travel_document_json(document));
  check(need(decode_freedom_travel_document_json(text)) == document &&
            std::get<FreedomTravelSaveDocument>(
                need(decode_native_save_document_json(text))) == document,
        "Outer58 direct/native Continue retains every typed bill, duration and "
        "sample");
  const auto root = Json::parse(text);
  check(root["format_version"] == 58U && root["travel"]["version"] == 7U,
        "Strain uses an explicit outer58/logical7");
  return text;
}
auto malformed(const FreedomTravelSaveDocument& transit) -> void {
  const auto root = Json::parse(roundtrip(transit));
  for (unsigned fault = 0; fault < 11; ++fault) {
    auto bad = root;
    auto& resources = bad["voyage"]["voyage"]["resources"];
    auto& drive = resources["drive"];
    auto& stamp = drive["last_use"];
    switch (fault) {
      case 0: bad["format_version"] = 57U; break;
      case 1: bad["travel"]["version"] = 6U; break;
      case 2: resources["version"] = 2U; break;
      case 3: drive["version"] = 1U; break;
      case 4: stamp.erase("recovery_ticks"); break;
      case 5: stamp["recovery_ticks"] = 5399U; break;
      case 6: stamp["recovery_ticks"] = 10801U; break;
      case 7: stamp["recovery_ticks"] = 5400U; break;
      case 8: stamp["recovery_ticks"] = 9000.5; break;
      case 9: stamp["extra"] = true; break;
      case 10: drive["last_use"] = nullptr; break;
      default: break;
    }
    check(!decode_freedom_travel_document_json(bad.dump()),
          "Closed58 refuses mixed recipes, missing/noninteger/out-of-bound or "
          "forged duration");
  }
  // Regenerate a genuinely self-consistent receipt using a shape-valid altered
  // prior cycle. Provider validation alone permits the input; composed history
  // must bind it to the actual still-retained Moon commitment.
  auto forged = transit;
  auto& planet = *forged.travel.composed->planet_committed;
  planet.request.recovery->last_use->recovery_ticks = 5400;
  planet = need(freeze_freedom_planet_jump_arrival(planet.request));
  need(validate_frozen_freedom_planet_jump_arrival(planet));
  forged.travel.composed->planet_commitment_resources->drive =
      planet.request.recovery;
  forged.voyage.voyage.resources.drive = need(advance_freedom_drive_recovery(
      *planet.request.recovery, FreedomJumpRecoveryKind::planet,
      planet.request.attempt, planet.source_checksum,
      planet.request.source.flight.tick));
  check(!validate_freedom_travel_document(forged),
        "A regenerated typed receipt cannot rewrite a retained cross-route "
        "recovery cycle");
  auto selected = transit;
  selected.travel.version = kFreedomRecoveringTravelStateVersion;
  check(!encode_freedom_travel_document_json(selected),
        "Historical57 cannot adopt a resource3/drive2 journey");
  const auto stripped =
      need(encode_freedom_knowledge_document_json(transit.voyage));
  check(!decode_native_save_document_json(stripped) &&
            !native_select_save_document(NativeSaveDocument{transit.voyage}),
        "Used drive2 cannot discard its authenticated travel58 owner");
}
auto service(const FreedomTravelSaveDocument& document) -> void {
  // Labelled dock-constraint fixture, not a claimed piloted station return.
  // Resolve the actual port at the retained stamp's real clock and verify the
  // existing service provider preserves debt while replenishing quantities.
  auto flight = surface_base_flight(document.voyage.voyage.voyage.base);
  const auto system = need(
      generate_physical_origin_system(flight.origin.recipe.universe_seed, 2));
  const auto station =
      generate_origin_station(flight.origin.recipe.universe_seed);
  const auto geometry = need(origin_station_geometry(station));
  const auto& ledger = document.voyage.voyage.resources;
  flight.flight = need(release_origin_port(
      system, station, geometry,
      {geometry.version, {station.id, 1}, ledger.frame, ledger.tick}));
  flight.world = FreedomActiveWorldSelection{2, system.catalog.id,
                                             station.orbit.host_planet};
  const auto serviced = need(replenish_freedom_resources(
      ledger, {flight, {geometry.version, {station.id, 1}, true}}));
  check(serviced.drive == ledger.drive && serviced.tick == ledger.tick &&
            serviced.version == kFreedomStrainedResourceVersion &&
            serviced.jump_charges == 3 &&
            serviced.flight_quanta == kFreedomFlightCapacityQuanta,
        "Authentic dock service refills quantities without repairing temporary "
        "strain");
}
auto replacement(const FreedomTravelSaveDocument& document) -> void {
  const auto flight = surface_base_flight(document.voyage.voyage.voyage.base);
  const FreedomRecoverySaveDocument original{document, {}};
  const auto pending = need(record_freedom_loss(
      original, FreedomLossCause::irrecoverable_destruction,
      flight.origin.state.craft, flight.flight.tick,
      need(freedom_recovery_source_checksum(original.voyage))));
  const auto replaced = need(complete_freedom_recovery(pending));
  const auto& ledger = recovery_knowledge(replaced.voyage).voyage.resources;
  check(
      replaced.recovery.latest->retired == pending.recovery.latest->retired &&
          ledger.version == kFreedomStrainedResourceVersion && ledger.drive &&
          ledger.drive->version == kFreedomStrainedDriveRecoveryVersion &&
          !ledger.drive->last_use &&
          ledger.drive->lineage ==
              recovery_flight(replaced.voyage).origin.lineage &&
          ledger.craft != document.voyage.voyage.resources.craft &&
          need(freedom_jump_recovery_reading(*ledger.drive, ledger.tick)).ready,
      "Replacement retains selected strain protocol but starts new actual "
      "craft ready");
  const auto saved = need(encode_freedom_recovery_document_json(replaced));
  check(need(decode_freedom_recovery_document_json(saved)) == replaced,
        "Recovery retains retired58 exactly and restores current resource3");
  auto wrong = replaced;
  auto& current =
      std::get<FreedomKnowledgeSaveDocument>(wrong.voyage).voyage.resources;
  current.version = kFreedomRecoveryResourceVersion;
  current.drive->version = kFreedomDriveRecoveryVersion;
  check(!validate_freedom_recovery_document(wrong),
        "Replacement cannot silently drop selected temporary-strain policy");
  const FreedomConditionSaveDocument conditioned{
      1, document, need(make_craft_condition(flight)), {}};
  const auto condition =
      need(encode_freedom_condition_document_json(conditioned));
  check(need(decode_freedom_condition_document_json(condition)) == conditioned,
        "Condition wrapper explicitly dispatches58 without losing receipts");
}
} // namespace
int main() {
  try {
    auto s = source();
    const auto home = current(s);
    need(s.select_local_jump(companion(false)));
    leg(s, FreedomJumpRecoveryKind::moon);
    need(s.select_local_jump(home));
    leg(s, FreedomJumpRecoveryKind::moon);
    need(s.select_planet_jump(planet(s)));
    FreedomTravelSaveDocument transit;
    leg(s, FreedomJumpRecoveryKind::planet, &transit);
    roundtrip(need(s.travel_document()));
    malformed(transit);
    service(need(s.travel_document()));
    replacement(need(s.travel_document()));
    auto old = source(2, false);
    need(old.select_planet_jump(planet(old)));
    leg(old, FreedomJumpRecoveryKind::planet);
    const auto text = bytes(old);
    const auto old_json = Json::parse(text);
    check(old_json["format_version"] == 57U &&
              text.find("recovery_ticks") == std::string::npos &&
              reading(old).cycle_ticks == 5400,
          "Historical drive1 keeps its57 encoding without new duration fields");
    std::cout << checks << " jump strain persistence checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
