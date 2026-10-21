#include "apsis_drift/freedom_condition_save.hpp"
#include "apsis_drift/native_startup.hpp"
#include "apsis_drift/save_file.hpp"

#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
auto check(bool good, const char* message) -> void {
  if (!good) throw std::runtime_error(message);
}
template <class T, class E> auto need(std::expected<T, E> v) -> T {
  if (!v) throw std::runtime_error("Condition save fixture refused");
  return std::move(*v);
}
auto composed_wrappers() -> void {
  // Wrapper metadata fixture at the actual starting station, not a flown jump.
  const auto startup = need(native_planetary_network_new_game({42}));
  const auto& knowledge =
      std::get<FreedomKnowledgeSaveDocument>(startup.document);
  FreedomTravelSaveDocument travel{
      knowledge, {}, NativeStartingAssemblySelection{}, {}};
  travel.travel.version = kFreedomComposedTravelStateVersion;
  travel.travel.composed.emplace();
  const auto inner = need(encode_freedom_travel_document_json(travel));
  FreedomConditionSaveDocument document{
      1,
      travel,
      need(make_craft_condition(
          surface_base_flight(knowledge.voyage.voyage.base))),
      {}};
  const auto direct = need(encode_freedom_condition_document_json(document));
  check(Json::parse(direct)["voyage"] == Json::parse(inner) &&
            need(decode_freedom_condition_document_json(direct)) == document &&
            need(encode_freedom_condition_document_json(need(
                decode_freedom_condition_document_json(direct)))) == direct,
        "Condition34 preserves the complete explicitly selected composed56 "
        "voyage");
  document.voyage = FreedomRecoverySaveDocument{travel, {}};
  const auto nested = need(encode_freedom_condition_document_json(document));
  check(need(decode_freedom_condition_document_json(nested)) == document &&
            std::get<FreedomConditionSaveDocument>(
                need(decode_native_save_document_json(nested))) == document,
        "Condition34 through recovery29 admits composed56 without flattening "
        "owners");
  for (unsigned fault = 0; fault < 4; ++fault) {
    auto bad = Json::parse(nested);
    auto& voyage = bad["voyage"]["voyage"];
    switch (fault) {
      case 0: voyage["format_version"] = 57U; break;
      case 1:
        voyage["format_version"] = kFreedomNetworkTravelSaveFormatVersion;
        break;
      case 2: voyage["travel"]["composed"]["version"] = 2U; break;
      case 3: voyage["travel"]["version"] = 4U; break;
      default: break;
    }
    check(!decode_freedom_condition_document_json(bad.dump()),
          "Nested condition/recovery rejects unknown or mismatched composed "
          "policy");
  }
  check(need(encode_freedom_travel_document_json(travel)) == inner,
        "Wrapper reads leave the underlying selected travel bytes untouched");
}
auto run() -> void {
  const auto assembly =
      need(make_freedom_starting_assembly_new_game_document({42}));
  const auto flight = assembly.journey.voyage.flight;
  FreedomKnowledgeSaveDocument knowledge{};
  knowledge.voyage.voyage.base.emplace<FreedomStartingAssemblySaveDocument>(
      assembly);
  knowledge.voyage.resources = {1, flight.origin.state.craft,
                                flight.flight.craft, flight.flight.tick};
  knowledge.knowledge = need(
      make_freedom_starting_knowledge({42}, kFreedomObservedKnowledgeVersion));
  const auto legacy = need(encode_freedom_knowledge_document_json(knowledge));
  FreedomConditionSaveDocument d{
      1, knowledge, need(make_craft_condition(flight)), {}};
  const auto text = need(encode_freedom_condition_document_json(d));
  const auto root = Json::parse(text);
  check(root["format_version"] == 34 && root["voyage"] == Json::parse(legacy) &&
            need(decode_freedom_condition_document_json(text)) == d,
        "Outer34 selects consequences without altering complete historical "
        "voyage");
  check(need(encode_freedom_knowledge_document_json(
            need(decode_freedom_knowledge_document_json(legacy)))) == legacy,
        "Unselected knowledge25 retains exact original bytes");
  check(std::get<FreedomConditionSaveDocument>(
            need(decode_native_save_document_json(text))) == d,
        "Native save dispatch admits explicit condition envelope");
  const auto rejects = [&](const Json& bad) {
    check(!decode_freedom_condition_document_json(bad.dump()),
          "Malformed condition envelope refuses without fallback");
  };
  for (auto field : {"format_version", "voyage", "consequences", "condition",
                     "retired_condition"}) {
    auto bad = root;
    bad.erase(field);
    rejects(bad);
  }
  for (const auto& v : {Json(true), Json(1.0), Json("1"), Json(2)}) {
    auto bad = root;
    bad["consequences"]["version"] = v;
    rejects(bad);
  }
  for (const auto& v : {Json(true), Json(1.0), Json("0"), Json(4),
                        Json(18446744073709551615ULL)}) {
    auto bad = root;
    bad["condition"]["forebody"] = v;
    rejects(bad);
  }
  auto bad = root;
  bad["condition"]["craft"] = "1";
  rejects(bad);
  bad = root;
  bad["condition"]["tick"] = "1";
  rejects(bad);
  bad = root;
  bad["condition"]["extra"] = false;
  rejects(bad);
  bad = root;
  bad["voyage"] = root;
  rejects(bad);
  bad = root;
  bad["retired_condition"] = root["condition"];
  rejects(bad);
  check(!decode_freedom_condition_document_json(std::string(65, '[') + "0" +
                                                std::string(65, ']')),
        "Excessive nesting refuses before condition JSON delegation");
  const FreedomRecoverySaveDocument recovery{knowledge, {}};
  d.voyage = recovery;
  check(need(decode_freedom_condition_document_json(
            need(encode_freedom_condition_document_json(d)))) == d,
        "Recovery29 is retained without forced actor/suit selection");
  d.voyage = FreedomSurfaceWalkSaveDocument{recovery, 1, {}};
  check(need(decode_freedom_condition_document_json(
            need(encode_freedom_condition_document_json(d)))) == d,
        "Surface-walk30 retains absent actual actor through outer34");
  auto pending = need(record_freedom_loss(
      recovery, FreedomLossCause::irrecoverable_destruction,
      flight.origin.state.craft, flight.flight.tick,
      need(freedom_recovery_source_checksum(recovery.voyage))));
  d.voyage = pending;
  d.retired_condition = d.condition;
  check(need(decode_freedom_condition_document_json(
            need(encode_freedom_condition_document_json(d)))) == d,
        "Explicit non-impact loss retains exact pristine retired condition");
  const auto completed = need(complete_freedom_recovery(pending));
  d.voyage = completed;
  d.condition = need(make_craft_condition(recovery_flight(completed.voyage)));
  check(need(decode_freedom_condition_document_json(
            need(encode_freedom_condition_document_json(d)))) == d &&
            d.condition.craft != d.retired_condition->craft &&
            !d.condition.latest,
        "One replacement has fresh condition and exact retired owner");
  auto wrong = d;
  wrong.retired_condition->craft = wrong.condition.craft;
  check(!validate_freedom_condition_document(wrong),
        "Retired condition cannot alias replacement");
  // Serializer/reducer fixture only: physical sweep witness generation has its
  // own provider tests. Here the exact before/candidate checksums are retained.
  auto before = flight;
  before.world = FreedomActiveWorldSelection{
      1, before.flight.frame.system, before.flight.frame.planet.value()};
  auto candidate = before;
  ++candidate.flight.tick;
  candidate.origin.state.tick = candidate.flight.tick;
  const auto owner = need(hydrate_freedom_flight_document(before));
  TerrainImpactWitness witness;
  witness.terrain_policy = before.model.terrain_policy;
  witness.craft = before.origin.state.craft;
  witness.craft_frame = before.flight.craft;
  witness.system = before.flight.frame.system;
  witness.planet = before.flight.frame.planet.value();
  witness.tick = candidate.flight.tick;
  witness.source_checksum =
      need(rigid_body_state_checksum({owner.system}, before.flight));
  witness.candidate_checksum =
      need(rigid_body_state_checksum({owner.system}, candidate.flight));
  witness.fraction = .5;
  witness.triangle.planet = witness.planet;
  witness.region = CraftImpactRegion::forebody;
  witness.outward_normal_planet_fixed = {1, 0, 0};
  witness.pre_response_velocity_planet_fixed = {-80, 0, 0};
  witness.closing_speed_metres_per_second = 80;
  const auto destroyed = need(reduce_craft_condition(
      need(make_craft_condition(before)), before, candidate,
      {1, TerrainImpactClass::impact, 0, witness}));
  FreedomKnowledgeSaveDocument departed{};
  departed.voyage.voyage.base.emplace<FreedomFlightSaveDocument>(candidate);
  departed.voyage.resources = {1, candidate.origin.state.craft,
                               candidate.flight.craft, candidate.flight.tick};
  departed.knowledge = need(
      make_freedom_starting_knowledge({42}, kFreedomTravelKnowledgeVersion));
  const FreedomTravelSaveDocument travel{
      departed, {}, NativeStartingAssemblySelection{}, {}};
  FreedomConditionSaveDocument catastrophe{1, travel, destroyed.condition, {}};
  check(destroyed.catastrophic &&
            !validate_freedom_condition_document(catastrophe),
        "Catastrophic condition cannot publish without the matching recovery "
        "transaction");
  const FreedomRecoverySaveDocument source{travel, {}};
  const auto loss = need(record_freedom_loss(
      source, FreedomLossCause::irrecoverable_destruction,
      candidate.origin.state.craft, candidate.flight.tick,
      need(freedom_recovery_source_checksum(source.voyage))));
  catastrophe.voyage = loss;
  catastrophe.retired_condition = destroyed.condition;
  check(need(decode_freedom_condition_document_json(
            need(encode_freedom_condition_document_json(catastrophe)))) ==
            catastrophe,
        "Catastrophic receipt and pending recovery roundtrip as one selected "
        "document");
  const auto replacement = need(complete_freedom_recovery(loss));
  catastrophe.voyage = replacement;
  catastrophe.condition =
      need(make_craft_condition(recovery_flight(replacement.voyage)));
  check(need(decode_freedom_condition_document_json(
            need(encode_freedom_condition_document_json(catastrophe)))) ==
                catastrophe &&
            !catastrophe.condition.forebody &&
            catastrophe.retired_condition->latest->catastrophic,
        "Recovery retains retired event while installing pristine replacement "
        "condition");
}
} // namespace
int main() {
  try {
    run();
    composed_wrappers();
    std::cout << "Freedom condition save contract passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
