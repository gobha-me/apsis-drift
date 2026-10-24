#include "apsis_drift/freedom_condition_save.hpp"
#include "apsis_drift/native_startup.hpp"
#include "apsis_drift/paid_operations.hpp"
#include "apsis_drift/save_file.hpp"

#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <source_location>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
auto check(bool good, const char* message) -> void {
  if (!good) throw std::runtime_error(message);
}
template <class T, class E>
auto need(std::expected<T, E> v,
          std::source_location at = std::source_location::current()) -> T {
  if (!v) {
    auto message = "Condition save fixture line " + std::to_string(at.line());
    if constexpr (std::is_same_v<E, std::string>)
      message += ": " + v.error();
    else if constexpr (requires { v.error().detail; })
      message += ": " + v.error().detail;
    throw std::runtime_error(message);
  }
  if constexpr (!std::is_void_v<T>) {
    if constexpr (std::is_trivially_copyable_v<T>)
      return *v;
    else
      return std::move(*v);
  }
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
auto repaired_envelopes() -> void {
  auto startup = std::make_unique<NativeStartup>(
      need(native_planetary_network_new_game({42})));
  auto knowledge = std::make_unique<FreedomKnowledgeSaveDocument>(
      std::get<FreedomKnowledgeSaveDocument>(std::move(startup->document)));
  auto& assembly = std::get<FreedomStartingAssemblySaveDocument>(
      knowledge->voyage.voyage.base);
  auto before = std::make_unique<FreedomFlightSaveDocument>(
      assembly.journey.voyage.flight);
  const auto owner = need(hydrate_freedom_flight_document(*before));
  const auto station =
      generate_origin_station(before->origin.recipe.universe_seed);
  const auto geometry = need(origin_station_geometry(station));
  // Explicit serializer/reducer boundary: the actual generated Origin port
  // supplies both co-moving poses. The witness is initialized, not evidence of
  // a flown collision, a damaged return, or a native cash purchase.
  auto service =
      std::make_unique<FreedomDockingSaveDocument>(assembly.journey.voyage);
  ++service->flight.flight.tick;
  service->flight.origin.state.tick = service->flight.flight.tick;
  service->flight.flight = need(release_origin_port(
      owner.system, station, geometry,
      {service->docking.geometry_version, service->docking.target,
       before->flight.craft, service->flight.flight.tick}));
  check(
      validate_freedom_docking_document(*service).has_value(),
      "Initialized repair boundary is not an authenticated Origin attachment");
  TerrainImpactWitness witness;
  witness.terrain_policy = before->model.terrain_policy;
  witness.craft = before->origin.state.craft;
  witness.craft_frame = before->flight.craft;
  witness.system = before->flight.frame.system;
  witness.planet = before->flight.frame.planet.value();
  witness.tick = service->flight.flight.tick;
  witness.source_checksum =
      need(rigid_body_state_checksum({owner.system}, before->flight));
  witness.candidate_checksum =
      need(rigid_body_state_checksum({owner.system}, service->flight.flight));
  witness.fraction = .5;
  witness.triangle.planet = witness.planet;
  witness.region = CraftImpactRegion::forebody;
  witness.outward_normal_planet_fixed = {1, 0, 0};
  witness.pre_response_velocity_planet_fixed = {-6, 0, 0};
  witness.closing_speed_metres_per_second = 6;
  const auto damaged =
      need(reduce_craft_condition(need(make_craft_condition(*before)), *before,
                                  service->flight,
                                  {1, TerrainImpactClass::impact, 0, witness}))
          .condition;
  check(damaged.forebody == 1 && damaged.latest &&
            !damaged.latest->catastrophic,
        "Reducer boundary did not retain a genuine noncatastrophic impact "
        "receipt");
  const auto legacy = need(encode_craft_condition_json(damaged));
  check(Json::parse(legacy).size() == 12 &&
            !Json::parse(legacy).contains("latest_repair"),
        "Unrepaired condition no longer has its exact historical field set");
  assembly.journey.voyage = *service;
  knowledge->voyage.resources.tick = service->flight.flight.tick;
  auto free = std::make_unique<FreedomConditionSaveDocument>();
  free->voyage.emplace<FreedomKnowledgeSaveDocument>(*knowledge);
  free->condition = need(repair_craft_condition(damaged, *service));
  const auto free_text = need(encode_freedom_condition_document_json(*free));
  check(
      free->condition.version == kRepairedCraftConditionVersion &&
          !free->condition.forebody && !free->condition.landing_structure &&
          free->condition.latest == damaged.latest &&
          free->condition.next_event == damaged.next_event &&
          free->condition.contact_armed == damaged.contact_armed &&
          free->condition.clear_ticks == damaged.clear_ticks &&
          Json::parse(free_text)["format_version"] == 34 &&
          need(decode_freedom_condition_document_json(free_text)) == *free,
      "Repair lost impact/gate/serial or changed the condition voyage family");
  check(need(encode_craft_condition_json(damaged)) == legacy,
        "Repair mutated or re-encoded the v1 source condition");
  auto paid = std::make_unique<FreedomConditionSaveDocument>(*free);
  auto& paid_knowledge =
      std::get<FreedomKnowledgeSaveDocument>(paid->voyage).knowledge;
  // Initialized cumulative account metadata tests persistence joins. Native
  // service/payment integration owns the later atomic debit command.
  paid_knowledge.paid_operations =
      need(make_paid_operations(PaidIncomeCounts{}, 0, 0));
  paid_knowledge.paid_operations->spent_credits +=
      kCraftOriginRepairUnitCredits;
  paid->condition = need(repair_craft_condition(
      damaged, *service,
      {true, paid_knowledge.paid_operations->started_tick,
       paid_knowledge.paid_operations->spent_credits}));
  const auto paid_text = need(encode_freedom_condition_document_json(*paid));
  check(need(decode_freedom_condition_document_json(paid_text)) == *paid &&
            std::get<FreedomConditionSaveDocument>(
                need(decode_native_save_document_json(paid_text))) == *paid,
        "Paid repaired condition failed strict outer/native admission");
  for (unsigned fault = 0; fault < 3; ++fault) {
    auto bad = std::make_unique<FreedomConditionSaveDocument>(*paid);
    auto& account = std::get<FreedomKnowledgeSaveDocument>(bad->voyage)
                        .knowledge.paid_operations;
    if (fault == 0)
      account.reset();
    else if (fault == 1)
      ++account->started_tick;
    else
      --account->spent_credits;
    check(!validate_freedom_condition_document(*bad),
          "Paid repair accepted absent, foreign-start or insufficient "
          "cumulative payment");
  }
  auto enrolled = std::make_unique<FreedomConditionSaveDocument>(*free);
  auto& enrolled_knowledge =
      std::get<FreedomKnowledgeSaveDocument>(enrolled->voyage).knowledge;
  enrolled_knowledge.paid_operations = need(
      make_paid_operations(PaidIncomeCounts{}, service->flight.flight.tick, 0));
  check(validate_freedom_condition_document(*enrolled).has_value(),
        "Technical repair rejected legitimate same-tick later paid enrollment");
  enrolled_knowledge.paid_operations =
      need(make_paid_operations(PaidIncomeCounts{}, 0, 0));
  check(!validate_freedom_condition_document(*enrolled),
        "Technical repair bypassed a paid account already selected before "
        "service");
  // A later same-port clock is initialized explicitly; no flown elapsed time.
  ++service->flight.flight.tick;
  service->flight.origin.state.tick = service->flight.flight.tick;
  service->flight.flight = need(release_origin_port(
      owner.system, station, geometry,
      {service->docking.geometry_version, service->docking.target,
       before->flight.craft, service->flight.flight.tick}));
  auto& enrolled_voyage =
      std::get<FreedomKnowledgeSaveDocument>(enrolled->voyage);
  std::get<FreedomStartingAssemblySaveDocument>(
      enrolled_voyage.voyage.voyage.base)
      .journey.voyage = *service;
  enrolled_voyage.voyage.resources.tick = service->flight.flight.tick;
  enrolled->condition.tick = service->flight.flight.tick;
  enrolled_knowledge.paid_operations = need(
      make_paid_operations(PaidIncomeCounts{}, service->flight.flight.tick, 0));
  check(need(decode_freedom_condition_document_json(need(
            encode_freedom_condition_document_json(*enrolled)))) == *enrolled,
        "Technical history imposed a retrospective fee on later enrollment");
  // Full nested wrapper and retirement tests keep one actual immutable owner.
  auto recovery = std::make_unique<FreedomRecoverySaveDocument>();
  recovery->voyage.emplace<FreedomKnowledgeSaveDocument>(
      std::get<FreedomKnowledgeSaveDocument>(paid->voyage));
  const auto loss_checksum =
      need(freedom_recovery_source_checksum(recovery->voyage));
  auto pending =
      std::make_unique<FreedomRecoverySaveDocument>(need(record_freedom_loss(
          *recovery, FreedomLossCause::recoverable_destruction,
          paid->condition.craft, paid->condition.tick, loss_checksum)));
  auto retired = std::make_unique<FreedomConditionSaveDocument>(*paid);
  retired->voyage.emplace<FreedomRecoverySaveDocument>(*pending);
  retired->retired_condition = paid->condition;
  check(need(decode_freedom_condition_document_json(need(
            encode_freedom_condition_document_json(*retired)))) == *retired,
        "Pending loss did not retain exact current/retired v2 payment history");
  auto unequal = std::make_unique<FreedomConditionSaveDocument>(*retired);
  unequal->condition =
      need(make_craft_condition(condition_flight(unequal->voyage)));
  check(!validate_freedom_condition_document(*unequal),
        "Pending repaired loss accepted a different current condition");
  auto completed = std::make_unique<FreedomRecoverySaveDocument>(
      need(complete_freedom_recovery(*pending)));
  retired->voyage.emplace<FreedomRecoverySaveDocument>(*completed);
  retired->condition =
      need(make_craft_condition(recovery_flight(completed->voyage)));
  check(retired->condition.version == kCraftConditionVersion &&
            !retired->condition.latest_repair &&
            retired->retired_condition == paid->condition &&
            need(decode_freedom_condition_document_json(need(
                encode_freedom_condition_document_json(*retired)))) == *retired,
        "Replacement discarded retired repaired history or repaired the new "
        "craft");
  auto wrong_wallet = std::make_unique<FreedomConditionSaveDocument>(*retired);
  auto& wrong_recovery =
      std::get<FreedomRecoverySaveDocument>(wrong_wallet->voyage);
  auto& wrong_source = std::get<FreedomKnowledgeSaveDocument>(
      wrong_recovery.recovery.latest->retired);
  --wrong_source.knowledge.paid_operations->spent_credits;
  wrong_recovery.recovery.latest->source_checksum =
      need(freedom_recovery_source_checksum(
          wrong_recovery.recovery.latest->retired));
  check(validate_freedom_recovery_document(wrong_recovery).has_value() &&
            !validate_freedom_condition_document(*wrong_wallet),
        "Retired paid repair borrowed its replacement's larger cumulative "
        "wallet");
  auto surface = std::make_unique<FreedomConditionSaveDocument>(*retired);
  surface->voyage.emplace<FreedomSurfaceWalkSaveDocument>(*completed, 1U,
                                                          std::nullopt);
  check(need(decode_freedom_condition_document_json(need(
            encode_freedom_condition_document_json(*surface)))) == *surface,
        "Nested surface selection dropped exact retired v2 history");
  auto travel = std::make_unique<FreedomConditionSaveDocument>(*paid);
  auto& composed = travel->voyage.emplace<FreedomTravelSaveDocument>();
  composed.voyage = std::get<FreedomKnowledgeSaveDocument>(paid->voyage);
  composed.craft_binding = NativeStartingAssemblySelection{};
  composed.travel.version = kFreedomComposedTravelStateVersion;
  composed.travel.composed.emplace();
  check(need(decode_freedom_condition_document_json(
            need(encode_freedom_condition_document_json(*travel)))) == *travel,
        "Composed travel failed repaired payment admission without a new "
        "voyage family");
  for (unsigned fault = 0; fault < 3; ++fault) {
    auto bad = Json::parse(paid_text);
    if (fault == 0)
      bad["condition"]["version"] = 1U;
    else if (fault == 1)
      bad["condition"]["unknown_repair"] = false;
    else
      bad["retired_condition"] = bad["condition"];
    check(!decode_freedom_condition_document_json(bad.dump()),
          "Malformed, downgraded or ownerless repaired envelope was accepted");
  }
  auto duplicate = paid_text;
  duplicate.insert(duplicate.find('{') + 1,
                   "\"consequences\":{\"version\":1},");
  check(!decode_freedom_condition_document_json(duplicate),
        "Repaired envelope accepted a duplicate consequence declaration");
  check(!decode_freedom_condition_document_json(
            std::string(kMaximumSaveDocumentBytes + 1, 'x')),
        "Repaired condition decoder relaxed the existing buffer bound");
  check(
      need(encode_freedom_condition_document_json(*paid)) == paid_text &&
          need(encode_freedom_condition_document_json(*free)) == free_text,
      "Reads, invalid admission or retirement changed original repaired saves");
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
    repaired_envelopes();
    std::cout << "Freedom condition save contract passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
