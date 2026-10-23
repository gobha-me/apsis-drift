#include "apsis_drift/freedom_recovery_save.hpp"
#include "apsis_drift/native_startup.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <numbers>
#include <source_location>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

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
    auto text =
        "Specimen persistence fixture line " + std::to_string(where.line());
    if constexpr (std::is_same_v<E, std::string>)
      text += ": " + result.error();
    else if constexpr (requires { result.error().detail; })
      text += ": " + result.error().detail;
    throw std::runtime_error(text);
  }
  if constexpr (!std::is_void_v<T>) return std::move(*result);
}
auto record_key(const FreedomFieldSampleRecord& record) {
  return std::tuple{record.target.system.value,
                    static_cast<unsigned>(record.target.body.kind),
                    record.target.body.value, record.site.value};
}
auto records() -> const std::vector<FreedomFieldSampleRecord>& {
  static const auto result = [] {
    // Generated identities authenticate persistence fixtures only. No flown
    // inspection/collection or terrain contact is asserted by these fixtures.
    const auto owner = need(generate_physical_origin_system({42}, 2));
    const auto& body = owner.catalog.planets.front().descriptor;
    const auto craft = need(derive_freedom_craft_identity({42}, 0));
    const auto radius = static_cast<double>(body.radius.value) * 1000;
    std::vector<FreedomFieldSampleRecord> found;
    for (unsigned sample = 0; sample < 512 && found.size() < 4; ++sample) {
      const auto angle = 2 * std::numbers::pi * sample / 512;
      const SurfaceLandmarkCell base{
          static_cast<std::int32_t>(std::floor(radius * std::cos(angle) / 512)),
          static_cast<std::int32_t>(std::floor(radius * std::sin(angle) / 512)),
          0};
      for (std::int32_t x = -1; x <= 1; ++x)
        for (std::int32_t y = -1; y <= 1; ++y)
          for (std::int32_t z = -1; z <= 1; ++z) {
            const SurfaceLandmarkCell cell{base.x + x, base.y + y, base.z + z};
            const auto identity = need(identify_surface_landmark(body, cell));
            if (!identity || found.size() == 4 ||
                std::ranges::any_of(found, [&](const auto& r) {
                  return r.site == identity->id;
                }))
              continue;
            found.push_back({{owner.catalog.id, body_id(body.id)},
                             cell,
                             identity->id,
                             3,
                             identity->kind,
                             craft,
                             8});
          }
    }
    check(found.size() == 4, "Four authentic specimen identities unavailable");
    std::ranges::sort(found, {}, record_key);
    return found;
  }();
  return result;
}
auto with_pouch(std::uint32_t version = 5) -> FreedomKnowledge {
  auto knowledge = need(make_freedom_starting_knowledge({42}, version));
  knowledge.surface_landmarks = FreedomSurfaceLandmarkState{};
  for (const auto& record : records())
    knowledge.surface_landmarks->inspections.push_back(
        {record.target.system, PlanetId{record.target.body.value}, record.cell,
         record.site, record.terrain_recipe, 7});
  knowledge.field_samples =
      FreedomFieldSampleState{1, records().front().craft, records(), records()};
  return knowledge;
}
auto counts(const FreedomKnowledge& knowledge) -> PaidIncomeCounts {
  return {knowledge.survey_commissions
              ? knowledge.survey_commissions->paid_count
              : 0U,
          knowledge.specimen_sales ? knowledge.specimen_sales->sold_count : 0U};
}
auto initialize_sale(FreedomKnowledge& knowledge,
                     std::vector<FreedomFieldSampleRecord> specimens,
                     SimulationTick tick, std::uint64_t generation = 0)
    -> void {
  // Explicit typed receipt/account fixture, not a native buyer action.
  const auto previous =
      knowledge.specimen_sales ? knowledge.specimen_sales->sold_count : 0U;
  const auto units = static_cast<std::uint32_t>(specimens.size());
  const auto seller = knowledge.field_samples->carrier;
  for (const auto& record : specimens)
    std::erase(knowledge.field_samples->carried, record);
  knowledge.specimen_sales = FreedomSpecimenSalesAccount{
      1, previous + units, {seller, tick, std::move(specimens)}};
  if (knowledge.paid_operations)
    knowledge.paid_operations = need(settle_paid_specimen_income(
        *knowledge.paid_operations, counts(knowledge), tick, generation,
        units));
}
auto snapshot_tick(FreedomKnowledgeSaveDocument& document, SimulationTick tick)
    -> void {
  // Whole authoritative-clock/Origin support snapshot, not elapsed flight.
  auto& assembly = std::get<FreedomStartingAssemblySaveDocument>(
      document.voyage.voyage.base);
  auto& flight = assembly.journey.voyage.flight;
  const auto owner = need(generate_physical_origin_system({42}, 2));
  const auto station = generate_origin_station({42});
  const auto geometry = need(origin_station_geometry(station));
  flight.flight = need(release_origin_port(
      owner, station, geometry,
      {geometry.version, assembly.journey.voyage.docking.target,
       flight.flight.craft, tick}));
  flight.origin.state.tick = tick;
  document.voyage.resources.tick = tick;
}
auto source() -> FreedomKnowledgeSaveDocument {
  auto document = std::get<FreedomKnowledgeSaveDocument>(
      need(native_planetary_network_new_game({42})).document);
  document.knowledge = with_pouch();
  snapshot_tick(document, 20);
  return document;
}
auto loss(const FreedomRecoverySaveDocument& source)
    -> FreedomRecoverySaveDocument {
  const auto body = recovery_flight(source.voyage);
  return need(record_freedom_loss(
      source, FreedomLossCause::recoverable_destruction,
      body.origin.state.craft, body.flight.tick,
      need(freedom_recovery_source_checksum(source.voyage))));
}
auto metadata() -> void {
  for (std::uint32_t version = 1; version <= 6; ++version) {
    const auto empty = need(make_freedom_starting_knowledge({42}, version));
    const auto before = need(encode_freedom_knowledge_json(empty, 20));
    check(!Json::parse(before).contains("specimen_sales") &&
              need(encode_freedom_knowledge_json(
                  need(decode_freedom_knowledge_json(before, 20)), 20)) ==
                  before,
          "Absent sales preserve every previous recipe's exact bytes");
    auto knowledge = with_pouch(version);
    initialize_sale(knowledge, {records().front()}, 9);
    if (version < 5) {
      check(!validate_freedom_knowledge(knowledge, 20),
            "Sale expanded unsupported knowledge recipe");
      continue;
    }
    const auto text = need(encode_freedom_knowledge_json(knowledge, 20));
    check(need(decode_freedom_knowledge_json(text, 20)) == knowledge &&
              knowledge.field_samples->carried.size() == 3 &&
              knowledge.field_samples->journal == records(),
          "One sale changes physical carried records without rewriting first "
          "notes");
    auto recollected = knowledge;
    auto again = records().front();
    again.tick = 12;
    recollected.field_samples->carried.push_back(again);
    std::ranges::sort(recollected.field_samples->carried, {}, record_key);
    initialize_sale(recollected, {again}, 13);
    check(need(decode_freedom_knowledge_json(
              need(encode_freedom_knowledge_json(recollected, 20)), 20)) ==
                  recollected &&
              recollected.specimen_sales->sold_count == 2 &&
              recollected.field_samples->journal == records(),
          "Genuine recollection can have a later receipt and retain original "
          "notes");
  }
  auto sold = with_pouch();
  initialize_sale(sold, records(), 9);
  const auto text = need(encode_freedom_knowledge_json(sold, 20));
  const auto root = Json::parse(text);
  auto overlapping = sold;
  overlapping.field_samples->carried = {
      overlapping.specimen_sales->latest.specimens.front()};
  check(!validate_freedom_knowledge(overlapping, 20) &&
            !encode_freedom_knowledge_json(overlapping, 20),
        "The exact sold collection cannot remain in the physical pouch");
  auto overlap_json = root;
  overlap_json["field_samples"]["carried"] =
      Json::array({root["specimen_sales"]["latest"]["specimens"][0]});
  check(!decode_freedom_knowledge_json(overlap_json.dump(), 20),
        "Codec admitted one collection as both sold and carried");
  auto recollected = sold;
  auto later = sold.specimen_sales->latest.specimens.front();
  later.tick = sold.specimen_sales->latest.sold_tick + 1;
  recollected.field_samples->carried = {later};
  check(need(decode_freedom_knowledge_json(
            need(encode_freedom_knowledge_json(recollected, 20)), 20)) ==
                recollected &&
            recollected.specimen_sales == sold.specimen_sales &&
            recollected.field_samples->journal == sold.field_samples->journal,
        "A genuinely later collection from a sold site retains its first note "
        "and receipt");
  check(root["specimen_sales"].size() == 3 &&
            root["specimen_sales"]["latest"].size() == 3 &&
            sold.field_samples->carried.empty() &&
            sold.specimen_sales->sold_count == 4,
        "Four-unit sale has bounded receipt and clears only the carried pouch");
  for (unsigned section = 0; section < 3; ++section) {
    const auto subtree = [section](auto& document) -> auto& {
      auto* selected = &document["specimen_sales"];
      if (section > 0) selected = &(*selected)["latest"];
      if (section == 2) selected = &(*selected)["specimens"][0];
      return *selected;
    };
    for (const auto& item : subtree(root).items()) {
      auto missing = root;
      subtree(missing).erase(item.key());
      check(!decode_freedom_knowledge_json(missing.dump(), 20),
            "Missing sale/receipt/record field accepted");
      for (const auto& value : {Json(nullptr), Json(true), Json(-1), Json(1.0),
                                Json("1"), Json::object()}) {
        auto bad = root;
        subtree(bad)[item.key()] = value;
        check(!decode_freedom_knowledge_json(bad.dump(), 20),
              "Malformed sale field type accepted");
      }
    }
  }
  for (unsigned fault = 0; fault < 21; ++fault) {
    auto bad = root;
    auto& sale = bad["specimen_sales"];
    auto& receipt = sale["latest"];
    auto& record = receipt["specimens"][0];
    switch (fault) {
      case 0: sale["extra"] = 1; break;
      case 1: sale["version"] = 2U; break;
      case 2: sale["sold_count"] = 0U; break;
      case 3: sale["sold_count"] = UINT64_C(4294967296); break;
      case 4: sale["sold_count"] = 3U; break;
      case 5: receipt["extra"] = 1; break;
      case 6: receipt["sold_tick"] = 21U; break;
      case 7: receipt["sold_tick"] = 7U; break;
      case 8: receipt["seller"] = "01"; break;
      case 9:
        receipt["seller"] =
            std::to_string(need(derive_freedom_craft_identity({43}, 0)).value);
        break;
      case 10: receipt["specimens"] = Json::array(); break;
      case 11: receipt["specimens"].push_back(record); break;
      case 12:
        record["craft"] =
            std::to_string(need(derive_freedom_craft_identity({42}, 1)).value);
        break;
      case 13: record["site"] = 0U; break;
      case 14: record["tick"] = 10U; break;
      case 15: record["cell"][0] = UINT64_MAX; break;
      case 16: record["terrain_recipe"] = UINT64_C(4294967296); break;
      case 17: record["kind"] = 255U; break;
      case 18: bad["field_samples"]["journal"] = Json::array(); break;
      case 19: bad.erase("field_samples"); break;
      case 20:
        std::swap(receipt["specimens"][0], receipt["specimens"][1]);
        break;
      default: break;
    }
    check(!decode_freedom_knowledge_json(bad.dump(), 20),
          "Invalid sale provenance/order/bounds accepted");
  }
  auto future = sold;
  future.specimen_sales->latest.seller =
      need(derive_freedom_craft_identity({42}, 1));
  for (auto& record : future.specimen_sales->latest.specimens)
    record.craft = future.specimen_sales->latest.seller;
  check(!validate_freedom_knowledge(future, 20),
        "Future lineage impersonated sale seller");
  auto duplicate = text;
  duplicate.insert(duplicate.find("\"specimen_sales\":"),
                   "\"specimen_sales\":null,");
  check(!decode_freedom_knowledge_json(duplicate, 20) &&
            !decode_freedom_knowledge_json(
                std::string(kMaximumFreedomNetworkKnowledgeBytes + 1, ' '), 20),
        "Duplicate sales or oversized buffer admitted");
  auto huge = root;
  huge["specimen_sales"]["sold_count"] = UINT32_MAX;
  check(decode_freedom_knowledge_json(huge.dump(), 20).has_value(),
        "Bounded exhausted sold counter cannot be loaded");
  huge["specimen_sales"]["sold_count"] = UINT64_MAX;
  auto unsafe = huge.dump();
  const auto position = unsafe.find("18446744073709551615");
  unsafe.replace(position, 20, "18446744073709551616");
  check(!decode_freedom_knowledge_json(unsafe, 20),
        "Above-u64 sale counter narrowed into range");
}
auto finance_metadata() -> void {
  auto knowledge = with_pouch();
  knowledge.paid_operations = need(make_paid_operations(0, 0, 0));
  const auto legacy = need(encode_freedom_knowledge_json(knowledge, 20));
  check(Json::parse(legacy)["paid_operations"].size() == 9 &&
            !Json::parse(legacy)["paid_operations"].contains(
                "settled_specimen_count"),
        "Paid-v1 retains exact nine-field contract");
  initialize_sale(knowledge, {records().front()}, 9);
  const auto encoded = need(encode_freedom_knowledge_json(knowledge, 20));
  auto root = Json::parse(encoded);
  check(root["paid_operations"].size() == 10 &&
            root["paid_operations"]["version"] == 2 &&
            knowledge.paid_operations->settled_specimen_count == 1 &&
            need(paid_operations_cash(*knowledge.paid_operations,
                                      counts(knowledge))) == 220,
        "First enrolled sale promotes v2 without another opening grant");
  check(need(decode_freedom_knowledge_json(encoded, 20)) == knowledge,
        "Paid-v2 income join did not roundtrip");
  for (unsigned fault = 0; fault < 10; ++fault) {
    auto bad = root;
    auto& account = bad["paid_operations"];
    switch (fault) {
      case 0: account.erase("settled_specimen_count"); break;
      case 1: account["settled_specimen_count"] = 0U; break;
      case 2: account["settled_specimen_count"] = UINT64_C(4294967296); break;
      case 3: account["settled_specimen_count"] = 2U; break;
      case 4: account["version"] = 1U; break;
      case 5: bad.erase("specimen_sales"); break;
      case 6: account["version"] = 3U; break;
      case 7: account["cash"] = 220U; break;
      case 8: account["charged_generation"] = 1U; break;
      case 9: account["spent_credits"] = 241U; break;
      default: break;
    }
    check(!decode_freedom_knowledge_json(bad.dump(), 20),
          "Paid version/count/cash/lineage join accepted malformed state");
  }
  for (const auto& value : {Json(nullptr), Json(true), Json(-1), Json(1.0),
                            Json("1"), Json::array(), Json::object()}) {
    auto bad = root;
    bad["paid_operations"]["settled_specimen_count"] = value;
    check(!decode_freedom_knowledge_json(bad.dump(), 20),
          "Nonunsigned specimen finance count accepted");
  }
  auto old_shape = Json::parse(legacy);
  old_shape["paid_operations"]["settled_specimen_count"] = 0U;
  check(!decode_freedom_knowledge_json(old_shape.dump(), 20),
        "Paid-v1 accepts an extra zero specimen count");
  old_shape = root;
  old_shape["paid_operations"].erase("settled_specimen_count");
  old_shape["paid_operations"]["version"] = 1U;
  check(!decode_freedom_knowledge_json(old_shape.dump(), 20),
        "Survey-only paid-v1 accepts actual sales");
  auto free = with_pouch();
  initialize_sale(free, {records().front()}, 9);
  free.paid_operations = need(make_paid_operations(counts(free), 20, 0));
  check(free.paid_operations->version == 2 &&
            need(paid_operations_cash(*free.paid_operations, counts(free))) ==
                220 &&
            need(decode_freedom_knowledge_json(
                need(encode_freedom_knowledge_json(free, 20)), 20)) == free,
        "Enrollment with prior sales loses income or adds a second grant");
}
auto recovery_metadata() -> void {
  auto document = source();
  document.knowledge.paid_operations = need(make_paid_operations(0, 0, 0));
  initialize_sale(document.knowledge, {records().front()}, 9);
  const auto& retained = document.knowledge.specimen_sales;
  const auto& notes = document.knowledge.field_samples->journal;
  auto pending = loss(FreedomRecoverySaveDocument{document, {}});
  check(recovery_knowledge(pending.voyage) == document &&
            recovery_knowledge(pending.recovery.latest->retired) == document,
        "Pending loss changed its frozen sale/pouch/account source");
  auto replacement = need(complete_freedom_recovery(pending));
  const auto& replaced = recovery_knowledge(replacement.voyage).knowledge;
  check(replaced.specimen_sales == retained &&
            replaced.field_samples->carried.empty() &&
            replaced.field_samples->journal == notes &&
            replaced.paid_operations->charged_generation == 1 &&
            need(paid_operations_cash(*replaced.paid_operations,
                                      counts(replaced))) == 120 &&
            need(complete_freedom_recovery(replacement)) == replacement,
        "Replacement invented sale, dropped income/notes or duplicated "
        "recovery fee");
  check(need(decode_freedom_recovery_document_json(
            need(encode_freedom_recovery_document_json(replacement)))) ==
            replacement,
        "Paid-v2 sale/replacement failed whole Save/Continue");
  auto free_source = source();
  initialize_sale(free_source.knowledge, {records().front()}, 9);
  auto enrolled = need(complete_freedom_recovery(
      loss(FreedomRecoverySaveDocument{free_source, {}})));
  auto& enrolled_knowledge =
      std::get<FreedomKnowledgeSaveDocument>(enrolled.voyage).knowledge;
  const auto enrollment_tick = recovery_flight(enrolled.voyage).flight.tick;
  enrolled_knowledge.paid_operations = need(
      make_paid_operations(counts(enrolled_knowledge), enrollment_tick, 1));
  check(enrolled_knowledge.specimen_sales ==
                free_source.knowledge.specimen_sales &&
            need(paid_operations_cash(*enrolled_knowledge.paid_operations,
                                      counts(enrolled_knowledge))) == 220 &&
            validate_freedom_recovery_document(enrolled).has_value(),
        "Explicit post-loss enrollment retains prior sales without a "
        "retroactive fee");
  auto early_enrollment = enrolled;
  std::get<FreedomKnowledgeSaveDocument>(early_enrollment.voyage)
      .knowledge.paid_operations->started_tick = enrollment_tick - 1;
  check(!validate_freedom_recovery_document(early_enrollment),
        "Post-loss enrollment backdated its new opening profile before loss");
  for (unsigned fault = 0; fault < 5; ++fault) {
    auto bad = replacement;
    auto& knowledge =
        std::get<FreedomKnowledgeSaveDocument>(bad.voyage).knowledge;
    switch (fault) {
      case 0: knowledge.specimen_sales.reset(); break;
      case 1:
        knowledge.specimen_sales->sold_count = 2;
        knowledge.paid_operations->settled_specimen_count = 2;
        break;
      case 2: knowledge.specimen_sales->latest.sold_tick = 10; break;
      case 3: knowledge.paid_operations->spent_credits -= 1; break;
      case 4:
        knowledge.paid_operations->version = 1;
        knowledge.paid_operations->settled_specimen_count = 0;
        break;
      default: break;
    }
    check(!validate_freedom_recovery_document(bad),
          "Loss retention permitted sales/receipt/account forgery");
  }
  auto poor = source();
  poor.knowledge.paid_operations = need(make_paid_operations(0, 0, 0));
  poor.knowledge.paid_operations->spent_credits = 200;
  poor.knowledge.paid_operations->operating_tab = 200;
  auto owing = need(
      complete_freedom_recovery(loss(FreedomRecoverySaveDocument{poor, {}})));
  auto& renewed = std::get<FreedomKnowledgeSaveDocument>(owing.voyage);
  check(!renewed.knowledge.specimen_sales &&
            renewed.knowledge.field_samples->carried.empty() &&
            renewed.knowledge.paid_operations->recovery_due == 100,
        "Losing unsold specimens invented income or lost the actual excess");
  const auto craft = recovery_flight(owing.voyage).origin.state.craft;
  auto fresh = records();
  for (auto& record : fresh) {
    record.craft = craft;
    record.tick = 21;
  }
  renewed.knowledge.field_samples->carried = fresh;
  snapshot_tick(renewed, 22);
  initialize_sale(renewed.knowledge, fresh, 22, 1);
  check(renewed.knowledge.paid_operations->version == 2 &&
            renewed.knowledge.paid_operations->recovery_due == 0 &&
            renewed.knowledge.paid_operations->operating_tab == 170 &&
            need(paid_operations_cash(*renewed.knowledge.paid_operations,
                                      counts(renewed.knowledge))) == 30 &&
            validate_freedom_recovery_document(owing).has_value(),
        "Actual replacement's four-specimen receipt must settle100 insurance "
        "then30 tab without a grant");
  auto forged = owing;
  auto& wrong = std::get<FreedomKnowledgeSaveDocument>(forged.voyage).knowledge;
  wrong.paid_operations->spent_credits -= 100;
  check(!validate_freedom_recovery_document(forged),
        "Sales income bypassed retained insurance-first conservation");
  forged = owing;
  auto& retired = *std::get<FreedomKnowledgeSaveDocument>(forged.voyage)
                       .knowledge.specimen_sales;
  retired.latest.seller = records().front().craft;
  for (auto& record : retired.latest.specimens)
    record.craft = retired.latest.seller;
  check(!validate_freedom_recovery_document(forged),
        "Replacement sold its retired pouch instead of new collection");
  forged = owing;
  auto& backdated = *std::get<FreedomKnowledgeSaveDocument>(forged.voyage)
                         .knowledge.specimen_sales;
  for (auto& record : backdated.latest.specimens)
    record.tick = 19;
  backdated.latest.sold_tick = 19;
  check(!validate_freedom_recovery_document(forged),
        "New sale receipt backdated before loss boundary");
  const auto second = need(complete_freedom_recovery(loss(owing)));
  const auto& second_knowledge = recovery_knowledge(second.voyage).knowledge;
  check(second_knowledge.specimen_sales == renewed.knowledge.specimen_sales &&
            second_knowledge.paid_operations->charged_generation == 2 &&
            second_knowledge.paid_operations->recovery_due == 70 &&
            need(paid_operations_cash(*second_knowledge.paid_operations,
                                      counts(second_knowledge))) == 0,
        "Second actual replacement changed sales or reset/doubled fee funding");
}
} // namespace
int main() {
  try {
    metadata();
    finance_metadata();
    recovery_metadata();
    std::cout << "Specimen sale persistence: " << checks
              << " checks, 0 failures\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
