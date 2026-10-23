#include "apsis_drift/freedom_recovery_save.hpp"
#include "apsis_drift/native_startup.hpp"
#include "apsis_drift/paid_operations.hpp"

#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <source_location>
#include <stdexcept>
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
auto need(std::expected<T, E> value,
          std::source_location where = std::source_location::current()) -> T {
  if (!value) {
    auto message =
        "Paid persistence fixture at line " + std::to_string(where.line());
    if constexpr (std::is_same_v<E, std::string>)
      message += ": " + value.error();
    else if constexpr (requires { value.error().detail; })
      message += ": " + value.error().detail;
    throw std::runtime_error(message);
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto target() -> std::pair<SystemId, PlanetId> {
  const auto ids = generate_first_intersystem_identities({42});
  const auto owner =
      need(generate_physical_local_system(ids.target_system_seed, 2));
  return {owner.catalog.id, owner.catalog.planets.front().descriptor.id};
}
auto accepted(SimulationTick now) -> SurveyCommissionAccount {
  const auto [system, planet] = target();
  return need(accept_survey_commission({}, system, planet, now));
}
auto finish(SurveyCommissionAccount account, StarterCraftId craft)
    -> SurveyCommissionAccount {
  const auto tick = account.active->accepted_tick;
  for (SimulationTick t = 1; t <= kSurveyCommissionMeasurementTicks; ++t)
    account = need(advance_survey_commission(account, craft, tick + t, true));
  return account;
}
auto snapshot_tick(FreedomKnowledgeSaveDocument& document, SimulationTick tick)
    -> void {
  // Explicit financial/persistence snapshot at authentic moving Origin support;
  // no claim of flown elapsed time or earned physical commission observations.
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
auto paid_fixture() -> FreedomKnowledgeSaveDocument {
  auto source = std::get<FreedomKnowledgeSaveDocument>(
      need(native_planetary_network_new_game({42})).document);
  source.knowledge.paid_operations = need(make_paid_operations(0, 0, 0));
  return source;
}
auto loss(const FreedomRecoverySaveDocument& document)
    -> FreedomRecoverySaveDocument {
  const auto flight = recovery_flight(document.voyage);
  return need(record_freedom_loss(
      document, FreedomLossCause::recoverable_destruction,
      flight.origin.state.craft, flight.flight.tick,
      need(freedom_recovery_source_checksum(document.voyage))));
}
auto loss(const FreedomKnowledgeSaveDocument& source)
    -> FreedomRecoverySaveDocument {
  return loss(FreedomRecoverySaveDocument{source, {}});
}
auto metadata() -> void {
  for (std::uint32_t version = 1; version <= 6; ++version) {
    auto knowledge = need(make_freedom_starting_knowledge({42}, version));
    const auto before = need(encode_freedom_knowledge_json(knowledge, 1));
    check(!Json::parse(before).contains("paid_operations") &&
              need(encode_freedom_knowledge_json(
                  need(decode_freedom_knowledge_json(before, 1)), 1)) == before,
          "Every old knowledge recipe preserves absent paid-profile bytes");
    knowledge.paid_operations = need(make_paid_operations(0, 0, 0));
    if (version < 5) {
      check(!validate_freedom_knowledge(knowledge, 1),
            "Paid profile cannot implicitly expand an older chart");
      continue;
    }
    const auto text = need(encode_freedom_knowledge_json(knowledge, 1));
    auto normalized = Json::parse(text);
    normalized.erase("paid_operations");
    check(normalized.dump(2) + '\n' == before &&
              need(decode_freedom_knowledge_json(text, 1)) == knowledge,
          "Selecting optional personal finances changes no other knowledge "
          "bytes");
  }
  auto knowledge = need(make_freedom_starting_knowledge({42}, 5));
  knowledge.paid_operations = need(make_paid_operations(0, 0, 0));
  const auto text = need(encode_freedom_knowledge_json(knowledge, 1));
  const auto root = Json::parse(text);
  const auto& account = root["paid_operations"];
  check(account.size() == 9 && !account.contains("cash") &&
            !account.contains("wallet"),
        "Paid schema has only the fixed account, no duplicate editable wallet");
  for (const auto& item : account.items()) {
    auto missing = root;
    missing["paid_operations"].erase(item.key());
    check(!decode_freedom_knowledge_json(missing.dump(), 1),
          "Missing closed paid field accepted");
    for (const auto& value : {Json(nullptr), Json(true), Json(-1), Json(1.0),
                              Json("1"), Json::array(), Json::object()}) {
      auto bad = root;
      bad["paid_operations"][item.key()] = value;
      check(!decode_freedom_knowledge_json(bad.dump(), 1),
            "Nonunsigned/float/boolean/container paid field accepted");
    }
  }
  for (unsigned fault = 0; fault < 13; ++fault) {
    auto bad = root;
    auto& a = bad["paid_operations"];
    switch (fault) {
      case 0: a = nullptr; break;
      case 1: a["version"] = 2U; break;
      case 2: a["version"] = UINT64_MAX; break;
      case 3: a["settled_paid_count"] = UINT64_MAX; break;
      case 4: a["supported_sequence"] = UINT64_MAX; break;
      case 5: a["started_tick"] = 2U; break;
      case 6: a["billed_through_hours"] = 3U; break;
      case 7: a["spent_credits"] = 201U; break;
      case 8: a["operating_tab"] = 201U; break;
      case 9: a["recovery_due"] = 101U; break;
      case 10: a["settled_paid_count"] = 1U; break;
      case 11: a["charged_generation"] = 1U; break;
      case 12: a["cash"] = 200U; break;
      default: break;
    }
    check(!decode_freedom_knowledge_json(bad.dump(), 1),
          "Wide counters, chronology, generation, debt/cash bounds or extra "
          "field accepted");
  }
  auto overflow = text;
  const auto position = overflow.find("\"spent_credits\": 20");
  check(position != std::string::npos, "Encoded paid integer fixture missing");
  overflow.replace(position, std::string{"\"spent_credits\": 20"}.size(),
                   "\"spent_credits\": 18446744073709551616");
  check(!decode_freedom_knowledge_json(overflow, 1),
        "Above-u64 paid amount narrowed");
  auto duplicate = text;
  duplicate.insert(duplicate.find("\"paid_operations\":"),
                   "\"paid_operations\": null,");
  check(
      !decode_freedom_knowledge_json(duplicate, 1) &&
          !decode_freedom_knowledge_json(
              text + std::string(kMaximumFreedomNetworkKnowledgeBytes, ' '), 1),
      "Paid extension bypassed duplicate-key or buffer boundary");
  auto wrong = knowledge;
  wrong.recipe.universe_seed = Seed{43};
  check(!validate_freedom_knowledge(wrong, 1),
        "Paid whole knowledge accepted a foreign seed owner");
  wrong = knowledge;
  wrong.recipe.craft_lineage = FreedomCraftLineage{1, 1};
  check(!validate_freedom_knowledge(wrong, 1),
        "Paid generation detached from current lineage");
  knowledge.paid_operations->supported_sequence = 1;
  check(!validate_freedom_knowledge(knowledge, 1),
        "Retainer serial without commission account accepted");
  knowledge.survey_commissions = accepted(0);
  need(validate_freedom_knowledge(knowledge, 1));
  knowledge.paid_operations->supported_sequence = 2;
  check(!validate_freedom_knowledge(knowledge, 1),
        "Retainer serial outside admitted acceptance history accepted");
  knowledge.paid_operations->supported_sequence = 1;
  const auto lagged =
      need(encode_freedom_knowledge_json(knowledge, 6 * kPaidHourTicks));
  const auto raw = *knowledge.paid_operations;
  for (unsigned i = 0; i < 3; ++i) {
    need(decode_freedom_knowledge_json(lagged, 6 * kPaidHourTicks));
    check(need(encode_freedom_knowledge_json(knowledge, 6 * kPaidHourTicks)) ==
                  lagged &&
              *knowledge.paid_operations == raw,
          "Read/Save/Continue posted deferred berth bills");
  }
  const auto effective =
      need(reconcile_paid_berth(raw, 0, 6 * kPaidHourTicks, 0));
  check(effective.billed_through_hours > raw.billed_through_hours &&
            effective.spent_credits > raw.spent_credits &&
            *knowledge.paid_operations == raw,
        "Deferred raw account has no independently derived effective bill");
}

auto funded_source(bool completed) -> FreedomKnowledgeSaveDocument {
  auto source = paid_fixture();
  const SimulationTick accepted_tick = 40 * kPaidHourTicks + 20;
  source.knowledge.survey_commissions = accepted(accepted_tick);
  auto effective = need(reconcile_paid_berth(*source.knowledge.paid_operations,
                                             0, accepted_tick, 0));
  const auto funded = need(fund_paid_retainer(
      effective, 0, accepted_tick, 0, 1,
      source.knowledge.survey_commissions->active->deadline_tick, 85));
  source.knowledge.paid_operations = funded.account;
  const auto craft = source.voyage.resources.craft;
  if (completed)
    source.knowledge.survey_commissions =
        finish(*source.knowledge.survey_commissions, craft);
  else
    for (SimulationTick i = 1; i <= 17; ++i)
      source.knowledge.survey_commissions =
          need(advance_survey_commission(*source.knowledge.survey_commissions,
                                         craft, accepted_tick + i, true));
  snapshot_tick(source,
                accepted_tick +
                    (completed ? kSurveyCommissionMeasurementTicks : 17));
  need(validate_freedom_knowledge_document(source));
  check(need(paid_operations_cash(*source.knowledge.paid_operations, 0)) == 0 &&
            source.knowledge.paid_operations->operating_tab > 0,
        "Financial snapshot did not reach genuine cash exhaustion and "
        "operating debt");
  return source;
}
auto recovery() -> void {
  const auto cash_replacement =
      need(complete_freedom_recovery(loss(paid_fixture())));
  const auto& cash_account =
      *recovery_knowledge(cash_replacement.voyage).knowledge.paid_operations;
  check(
      cash_account.spent_credits == 120 && cash_account.recovery_due == 0 &&
          cash_account.charged_generation == 1 &&
          need(paid_operations_cash(cash_account, 0)) == 80,
      "Actual funded replacement did not consume available cash before excess");
  auto source = funded_source(false);
  const auto retired_account = *source.knowledge.paid_operations;
  const auto pending = loss(source);
  check(recovery_knowledge(pending.voyage).knowledge.paid_operations ==
                retired_account &&
            pending.voyage == pending.recovery.latest->retired,
        "Recording pending loss charged or rewrote frozen financial source");
  const auto pending_bytes =
      need(encode_freedom_recovery_document_json(pending));
  auto replaced = need(complete_freedom_recovery(pending));
  check(need(encode_freedom_recovery_document_json(pending)) == pending_bytes,
        "Completing replacement mutated the retired loss argument");
  auto& current = std::get<FreedomKnowledgeSaveDocument>(replaced.voyage);
  const auto expected = need(charge_paid_recovery(
      retired_account, 0, current.voyage.resources.tick, 1));
  check(current.knowledge.paid_operations == expected &&
            expected.recovery_due == 100 &&
            !current.knowledge.survey_commissions->active->measurement &&
            recovery_knowledge(replaced.recovery.latest->retired)
                    .knowledge.paid_operations == retired_account,
        "Actual replacement missed excess or changed retired/unfinished "
        "account history");
  const auto replaced_bytes =
      need(encode_freedom_recovery_document_json(replaced));
  check(
      need(encode_freedom_recovery_document_json(
          need(complete_freedom_recovery(replaced)))) == replaced_bytes &&
          need(encode_freedom_recovery_document_json(
              need(decode_freedom_recovery_document_json(replaced_bytes)))) ==
              replaced_bytes,
      "Repeated completion/Continue charged excess or regranted opening cash");
  for (unsigned fault = 0; fault < 7; ++fault) {
    auto bad = replaced;
    auto& account = std::get<FreedomKnowledgeSaveDocument>(bad.voyage)
                        .knowledge.paid_operations;
    switch (fault) {
      case 0: account.reset(); break;
      case 1: account = need(make_paid_operations(0, 0, 1)); break;
      case 2: account->recovery_due = 0; break;
      case 3: --account->spent_credits; break;
      case 4: --account->supported_sequence; break;
      case 5: ++account->started_tick; break;
      case 6: --account->billed_through_hours; break;
      default: break;
    }
    check(!validate_freedom_recovery_document(bad),
          "Individually plausible replacement erased/reset finance anchors, "
          "fee or monotone counters");
  }
  // Repeated actual replacement caps outstanding insurance, not a new grant.
  const auto second = need(complete_freedom_recovery(loss(replaced)));
  const auto& second_account =
      *recovery_knowledge(second.voyage).knowledge.paid_operations;
  check(second_account.charged_generation == 2 &&
            second_account.recovery_due == 100 &&
            second_account.spent_credits >= expected.spent_credits &&
            need(paid_operations_cash(second_account, 0)) == 0,
        "Repeated loss created cash, duplicated opening credit or exceeded "
        "excess cap");

  auto ready = funded_source(true);
  auto recovered = need(complete_freedom_recovery(loss(ready)));
  auto& document = std::get<FreedomKnowledgeSaveDocument>(recovered.voyage);
  auto tick = document.voyage.resources.tick;
  const auto retained_report = document.knowledge.survey_commissions->active;
  check(retained_report == ready.knowledge.survey_commissions->active,
        "Paid recovery discarded completed old-craft report");
  document.knowledge.survey_commissions = need(submit_survey_commission(
      *document.knowledge.survey_commissions, retained_report->sequence, tick));
  document.knowledge.paid_operations = need(settle_paid_survey_income(
      *document.knowledge.paid_operations, 1, tick, 1));
  check(document.knowledge.paid_operations->recovery_due == 0 &&
            need(paid_operations_cash(*document.knowledge.paid_operations,
                                      1)) == 0 &&
            document.knowledge.paid_operations->operating_tab ==
                ready.knowledge.paid_operations->operating_tab,
        "Retired100-credit report bypassed insurance priority or repaid tab "
        "before excess");
  need(validate_freedom_recovery_document(recovered));
  auto skipped = recovered;
  auto& cheated = std::get<FreedomKnowledgeSaveDocument>(skipped.voyage)
                      .knowledge.paid_operations;
  cheated->spent_credits -= 100;
  check(
      !validate_freedom_recovery_document(skipped),
      "Settled count join alone allowed recovered report to become free cash");
  const auto [system, planet] = target();
  auto jobs = need(accept_survey_commission(
      *document.knowledge.survey_commissions, system, planet, tick));
  jobs = finish(jobs, document.voyage.resources.craft);
  tick = jobs.active->measurement->last_tick;
  document.knowledge.survey_commissions =
      need(submit_survey_commission(jobs, jobs.active->sequence, tick));
  document.knowledge.paid_operations = need(settle_paid_survey_income(
      *document.knowledge.paid_operations, 2, tick, 1));
  snapshot_tick(document, tick);
  check(document.knowledge.paid_operations->recovery_due == 0 &&
            document.knowledge.paid_operations->operating_tab ==
                ready.knowledge.paid_operations->operating_tab - 50 &&
            need(paid_operations_cash(*document.knowledge.paid_operations,
                                      2)) == 50,
        "Later fresh income failed the disclosed half-tab repayment after "
        "insurance");
  need(validate_freedom_recovery_document(recovered));
  auto skipped_tab = recovered;
  std::get<FreedomKnowledgeSaveDocument>(skipped_tab.voyage)
      .knowledge.paid_operations->spent_credits -= 50;
  check(
      !validate_freedom_recovery_document(skipped_tab),
      "Later income avoided mandatory old-tab settlement in retained lineage");
  jobs = *document.knowledge.survey_commissions;
  for (unsigned i = 0; i < 3; ++i) {
    jobs = need(accept_survey_commission(jobs, system, planet, tick));
    const auto supported = need(fund_paid_retainer(
        *document.knowledge.paid_operations, jobs.paid_count, tick, 1,
        jobs.active->sequence, jobs.active->deadline_tick, 85));
    document.knowledge.paid_operations = supported.account;
    document.knowledge.survey_commissions = jobs;
    check(!fund_paid_retainer(supported.account, jobs.paid_count, tick, 1,
                              jobs.active->sequence, jobs.active->deadline_tick,
                              85),
          "Identical commission serial stacked support");
    if (i == 2)
      check(supported.account.operating_tab == 200 &&
                supported.subsidized_credits > 0,
            "Bounded retainer cap removed the explicit subsidy floor");
    jobs = need(abandon_survey_commission(jobs, jobs.active->sequence, tick));
    document.knowledge.survey_commissions = jobs;
    need(validate_freedom_recovery_document(recovered));
  }
  const auto after_support =
      need(encode_freedom_recovery_document_json(recovered));
  check(need(encode_freedom_recovery_document_json(
            need(decode_freedom_recovery_document_json(after_support)))) ==
            after_support,
        "Later legal capped support cannot persist after replacement");

  // Historical loss before enrollment must not charge a retrospective fee.
  auto free_source = paid_fixture();
  free_source.knowledge.paid_operations.reset();
  snapshot_tick(free_source, 20);
  auto enrolled = need(complete_freedom_recovery(loss(free_source)));
  auto& enrolled_document =
      std::get<FreedomKnowledgeSaveDocument>(enrolled.voyage);
  enrolled_document.knowledge.paid_operations =
      need(make_paid_operations(0, 20, 1));
  need(validate_freedom_recovery_document(enrolled));
  auto too_old = enrolled;
  std::get<FreedomKnowledgeSaveDocument>(too_old.voyage)
      .knowledge.paid_operations->started_tick = 19;
  check(!validate_freedom_recovery_document(too_old),
        "Post-loss enrollment claimed a pre-loss opening anchor");
}
} // namespace
int main() {
  try {
    metadata();
    recovery();
    std::cout << "Paid operation save: " << checks << " checks, 0 failures\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
