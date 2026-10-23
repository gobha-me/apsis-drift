#include "apsis_drift/freedom_recovery_save.hpp"
#include "apsis_drift/native_startup.hpp"

#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
int checks{}, failures{};
auto check(bool value, std::string_view message) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E>
auto need(std::expected<T, E> value,
          const std::source_location where = std::source_location::current())
    -> T {
  if (!value) {
    auto message = std::string{where.file_name()} + ":" +
                   std::to_string(where.line()) + ": ";
    if constexpr (std::is_same_v<E, SaveSchemaError>)
      message += value.error().detail;
    else if constexpr (std::is_same_v<E, std::string>)
      message += value.error();
    else
      message += "Survey commission fixture refused";
    throw std::runtime_error(message);
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto target() -> std::pair<SystemId, PlanetId> {
  const auto ids = generate_first_intersystem_identities(Seed{42});
  const auto owner =
      need(generate_physical_local_system(ids.target_system_seed, 2));
  return {owner.catalog.id, owner.catalog.planets.front().descriptor.id};
}
auto accepted(SimulationTick tick = 0) -> SurveyCommissionAccount {
  const auto [system, planet] = target();
  return need(accept_survey_commission({}, system, planet, tick));
}
auto complete(SurveyCommissionAccount account, StarterCraftId craft)
    -> SurveyCommissionAccount {
  const auto first = account.active->accepted_tick + 1;
  for (SimulationTick i = 0; i < kSurveyCommissionMeasurementTicks; ++i)
    account = need(advance_survey_commission(account, craft, first + i, true));
  return account;
}
auto metadata() -> void {
  const Seed seed{42};
  const auto craft = need(derive_freedom_craft_identity(seed, 0));
  auto account = accepted();
  account = need(advance_survey_commission(account, craft, 1, true));
  auto ready = complete(accepted(), craft);
  const auto completed_tick = ready.active->measurement->last_tick;
  const auto paid = need(submit_survey_commission(ready, 1, completed_tick));
  for (std::uint32_t version = 1; version <= 6; ++version) {
    auto knowledge = need(make_freedom_starting_knowledge(seed, version));
    const auto before = need(encode_freedom_knowledge_json(knowledge, 1));
    check(!Json::parse(before).contains("survey_commissions") &&
              need(encode_freedom_knowledge_json(
                  need(decode_freedom_knowledge_json(before, 1)), 1)) == before,
          "All historical recipes retain absent account bytes exactly");
    knowledge.survey_commissions = account;
    if (version < 5) {
      auto forged = Json::parse(before);
      auto supported = need(make_freedom_starting_knowledge(seed, 5));
      supported.survey_commissions = account;
      forged["survey_commissions"] = Json::parse(need(
          encode_freedom_knowledge_json(supported, 1)))["survey_commissions"];
      check(!validate_freedom_knowledge(knowledge, 1) &&
                !decode_freedom_knowledge_json(forged.dump(), 1),
            "An optional account cannot enroll an older chart recipe");
      continue;
    }
    const auto text = need(encode_freedom_knowledge_json(knowledge, 1));
    const auto decoded = need(decode_freedom_knowledge_json(text, 1));
    auto normalized = Json::parse(text);
    normalized.erase("survey_commissions");
    check(decoded == knowledge && normalized.dump(2) + '\n' == before &&
              need(encode_freedom_knowledge_json(decoded, 1)) == text,
          "Explicit account roundtrips without changing any knowledge facts "
          "or terrain-survey history");
    for (const auto& value : {ready, paid}) {
      knowledge.survey_commissions = value;
      check(need(decode_freedom_knowledge_json(
                need(encode_freedom_knowledge_json(knowledge, completed_tick)),
                completed_tick)) == knowledge,
            "Completed measurements and one exact payout receipt survive "
            "closed metadata encoding");
    }
  }
  auto knowledge = need(make_freedom_starting_knowledge(seed, 5));
  knowledge.survey_commissions = account;
  const auto text = need(encode_freedom_knowledge_json(knowledge, 1));
  for (unsigned fault = 0; fault < 25; ++fault) {
    auto root = Json::parse(text);
    auto& a = root["survey_commissions"];
    auto& c = a["active"];
    auto& m = c["measurement"];
    switch (fault) {
      case 0: a = nullptr; break;
      case 1: a["version"] = 2U; break;
      case 2: a["version"] = UINT64_MAX; break;
      case 3: a["next_sequence"] = UINT64_MAX; break;
      case 4: a["paid_count"] = UINT64_MAX; break;
      case 5: a["paid_count"] = 1U; break;
      case 6: a["balance"] = 100U; break;
      case 7: a.erase("latest_paid"); break;
      case 8: c["sequence"] = UINT64_MAX; break;
      case 9: c["system"] = 0U; break;
      case 10: c["planet"] = 0U; break;
      case 11: c["accepted_tick"] = -1; break;
      case 12:
        c["deadline_tick"] = kSurveyCommissionReservationTicks + 1;
        break;
      case 13: c["measurement"] = Json::array(); break;
      case 14: m["craft"] = "1"; break;
      case 15:
        m["craft"] = need(derive_freedom_craft_identity(seed, 1)).value;
        break;
      case 16: m["acquired_ticks"] = UINT64_MAX; break;
      case 17: m["acquired_ticks"] = 2U; break;
      case 18: m["last_tick"] = 2U; break;
      case 19: m["started_tick"] = 0U; break;
      case 20: m["last_tick"] = 1.0; break;
      case 21: m["last_tick"] = std::numeric_limits<double>::infinity(); break;
      case 22: m["extra"] = 0; break;
      case 23: a["next_sequence"] = "2"; break;
      case 24: a["latest_paid"] = Json::object(); break;
      default: break;
    }
    check(!decode_freedom_knowledge_json(root.dump(), 1),
          "Closed account refuses nonunsigned/nonfinite/wide fields, "
          "unknown fields, impossible counters, clocks and source crafts");
  }
  for (unsigned fault = 0; fault < 3; ++fault) {
    auto invalid = knowledge;
    const auto ids = generate_first_intersystem_identities(Seed{43});
    const auto foreign =
        need(generate_physical_local_system(ids.target_system_seed, 2));
    if (fault == 0)
      invalid.survey_commissions->active->system = foreign.catalog.id;
    else if (fault == 1)
      invalid.survey_commissions->active->planet =
          foreign.catalog.planets.front().descriptor.id;
    else
      invalid.survey_commissions->active->system =
          generate_first_intersystem_identities(seed).origin_system;
    check(!validate_freedom_knowledge(invalid, 1),
          "Commission target is the exact recipe's first charted neighbor "
          "planet, never another universe or a Home substitute");
  }
  knowledge.survey_commissions = paid;
  const auto paid_json = Json::parse(
      need(encode_freedom_knowledge_json(knowledge, completed_tick)));
  for (unsigned fault = 0; fault < 6; ++fault) {
    auto malformed = paid_json;
    auto& a = malformed["survey_commissions"];
    auto& receipt = a["latest_paid"];
    switch (fault) {
      case 0: receipt["extra"] = 0U; break;
      case 1: receipt["commission"] = nullptr; break;
      case 2: receipt["submitted_tick"] = "3600"; break;
      case 3: receipt["submitted_tick"] = completed_tick - 1; break;
      case 4:
        receipt["commission"]["measurement"]["acquired_ticks"] = 3599U;
        break;
      case 5: a["paid_count"] = 0U; break;
      default: break;
    }
    check(!decode_freedom_knowledge_json(malformed.dump(), completed_tick),
          "Settlement receipt is closed, typed, complete and bound to the "
          "cumulative paid counter and actual measurement chronology");
  }
  auto duplicate = text;
  duplicate.insert(duplicate.find("\"survey_commissions\":"),
                   "\"survey_commissions\": null, ");
  check(
      !decode_freedom_knowledge_json(duplicate, 1) &&
          !decode_freedom_knowledge_json(
              text + std::string(kMaximumFreedomNetworkKnowledgeBytes, ' '), 1),
      "Account retains duplicate-key and bounded-input protection");
}
auto fixture() -> FreedomKnowledgeSaveDocument {
  return std::get<FreedomKnowledgeSaveDocument>(
      need(native_planetary_network_new_game(Seed{42})).document);
}
auto snapshot_tick(FreedomKnowledgeSaveDocument& document, SimulationTick tick)
    -> void {
  // Labelled account/owner metadata snapshot, not a flown commission. Resolve
  // genuine moving Origin support and retain craft, resources and pilot.
  auto& assembly = std::get<FreedomStartingAssemblySaveDocument>(
      document.voyage.voyage.base);
  auto& f = assembly.journey.voyage.flight;
  const auto owner = need(generate_physical_origin_system(Seed{42}, 2));
  const auto station = generate_origin_station(Seed{42});
  const auto geometry = need(origin_station_geometry(station));
  f.flight = need(release_origin_port(owner, station, geometry,
                                      {geometry.version,
                                       assembly.journey.voyage.docking.target,
                                       f.flight.craft, tick}));
  f.origin.state.tick = tick;
  document.voyage.resources.tick = tick;
}
auto replace(const FreedomKnowledgeSaveDocument& source)
    -> FreedomRecoverySaveDocument {
  FreedomRecoverySaveDocument original{source, {}};
  const auto& f = recovery_flight(original.voyage);
  return need(complete_freedom_recovery(need(record_freedom_loss(
      original, FreedomLossCause::irrecoverable_destruction,
      f.origin.state.craft, f.flight.tick,
      need(freedom_recovery_source_checksum(original.voyage))))));
}
auto recovery() -> void {
  auto source = fixture();
  const auto craft = source.voyage.resources.craft;
  auto partial = accepted();
  for (SimulationTick i = 1; i <= 17; ++i)
    partial = need(advance_survey_commission(partial, craft, i, true));
  source.knowledge.survey_commissions = partial;
  snapshot_tick(source, 20);
  const auto replacement = replace(source);
  const auto& current_document =
      std::get<FreedomKnowledgeSaveDocument>(replacement.voyage);
  const auto current = current_document.voyage.resources.craft;
  auto expected = partial;
  expected.active->measurement.reset();
  check(current != craft &&
            current_document.knowledge.survey_commissions == expected &&
            recovery_knowledge(replacement.recovery.latest->retired)
                    .knowledge.survey_commissions == partial &&
            need(decode_freedom_recovery_document_json(
                need(encode_freedom_recovery_document_json(replacement)))) ==
                replacement,
        "Replacement preserves accepted terms/sequence and exact retired "
        "account while discarding unfinished measurement");
  for (unsigned fault = 0; fault < 5; ++fault) {
    auto bad = replacement;
    auto& document = std::get<FreedomKnowledgeSaveDocument>(bad.voyage);
    auto& account = document.knowledge.survey_commissions;
    switch (fault) {
      case 0: account.reset(); break;
      case 1: account = SurveyCommissionAccount{}; break;
      case 2:
        ++account->active->accepted_tick;
        ++account->active->deadline_tick;
        break;
      case 3:
        account->active->measurement = partial.active->measurement;
        account->active->measurement->craft = current;
        break;
      case 4: account->active->measurement = partial.active->measurement; break;
      default: break;
    }
    if (fault != 4)
      need(validate_freedom_knowledge_document(document));
    else
      check(validate_freedom_knowledge(document.knowledge, 20).has_value() &&
                !validate_freedom_knowledge_document(document),
            "Historical craft provenance alone cannot authorize unfinished "
            "work on the current replacement craft");
    check(!validate_freedom_recovery_document(bad),
          "Replacement cannot drop/reset counters, rewrite accepted terms "
          "or keep/rebind retired partial work");
  }
}
auto completed_recovery() -> void {
  auto source = fixture();
  const auto craft = source.voyage.resources.craft;
  auto account = complete(accepted(), craft);
  auto tick = account.active->measurement->last_tick;
  account = need(submit_survey_commission(account, 1, tick));
  const auto [system, planet] = target();
  account = complete(
      need(accept_survey_commission(account, system, planet, tick)), craft);
  tick = account.active->measurement->last_tick;
  source.knowledge.survey_commissions = account;
  snapshot_tick(source, tick);
  auto restored = replace(source);
  auto& document = std::get<FreedomKnowledgeSaveDocument>(restored.voyage);
  auto& current_account = *document.knowledge.survey_commissions;
  check(current_account == account &&
            survey_commission_earnings(current_account) == 100 &&
            need(decode_freedom_recovery_document_json(need(
                encode_freedom_recovery_document_json(restored)))) == restored,
        "Completed original-craft report and paid receipt/earnings survive "
        "replacement without selling or resetting either");
  auto changed_paid = restored;
  auto& changed_account =
      *std::get<FreedomKnowledgeSaveDocument>(changed_paid.voyage)
           .knowledge.survey_commissions;
  changed_account.active.reset();
  ++changed_account.latest_paid->submitted_tick;
  need(validate_freedom_knowledge_document(
      std::get<FreedomKnowledgeSaveDocument>(changed_paid.voyage)));
  check(!validate_freedom_recovery_document(changed_paid),
        "An otherwise valid account cannot rewrite its retained last payout "
        "while keeping the same earned count");
  changed_account = account;
  changed_account.paid_count = 0;
  changed_account.latest_paid.reset();
  need(validate_freedom_knowledge_document(
      std::get<FreedomKnowledgeSaveDocument>(changed_paid.voyage)));
  check(!validate_freedom_recovery_document(changed_paid),
        "Replacement cannot rewind lifetime earnings even when the edited "
        "account remains structurally valid");
  current_account = need(submit_survey_commission(current_account, 2, tick));
  check(survey_commission_earnings(current_account) == 200 &&
            !submit_survey_commission(current_account, 2, tick) &&
            need(encode_freedom_recovery_document_json(restored)).size() <
                100000,
        "A retained completed report pays exactly once after replacement");
  auto rewritten = restored;
  auto& receipt = std::get<FreedomKnowledgeSaveDocument>(rewritten.voyage)
                      .knowledge.survey_commissions->latest_paid;
  --receipt->submitted_tick;
  --receipt->commission.measurement->last_tick;
  --receipt->commission.measurement->started_tick;
  check(!validate_freedom_recovery_document(rewritten),
        "A post-loss settlement cannot be moved before its loss boundary");
  const auto current = document.voyage.resources.craft;
  current_account = complete(
      need(accept_survey_commission(current_account, system, planet, tick)),
      current);
  tick = current_account.active->measurement->last_tick;
  current_account = need(submit_survey_commission(current_account, 3, tick));
  snapshot_tick(document, tick);
  check(survey_commission_earnings(current_account) == 300 &&
            need(decode_freedom_recovery_document_json(need(
                encode_freedom_recovery_document_json(restored)))) == restored,
        "Fresh post-loss work may append a genuinely new payment without "
        "rewriting retired earnings or retaining an unbounded history");
  auto inflated = restored;
  auto& excess = *std::get<FreedomKnowledgeSaveDocument>(inflated.voyage)
                      .knowledge.survey_commissions;
  excess.next_sequence = 5;
  excess.paid_count = 4;
  excess.latest_paid->commission.sequence = 4;
  need(validate_freedom_knowledge_document(
      std::get<FreedomKnowledgeSaveDocument>(inflated.voyage)));
  check(!validate_freedom_recovery_document(inflated),
        "An individually valid newest receipt cannot mint more payments "
        "than retained ready work plus fresh elapsed acquisition permits");
}
} // namespace
auto main() -> int {
  try {
    metadata();
    recovery();
    completed_recovery();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "Freedom survey commission save: " << checks << " checks, "
            << failures << " failures\n";
  return failures ? 1 : 0;
}
