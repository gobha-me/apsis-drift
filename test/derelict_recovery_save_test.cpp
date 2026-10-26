#include "derelict_recovery_test_support.hpp"

#include "apsis_drift/freedom_knowledge_save.hpp"
#include "apsis_drift/freedom_system_network.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <numbers>
#include <stdexcept>

namespace {
using namespace apsis_drift;
using test_support::derelict_recovery_detail::need;
using Json = nlohmann::ordered_json;
unsigned checks{};
auto check(bool value, const char* message) -> void {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
auto stable(const FreedomKnowledge& knowledge, SimulationTick tick)
    -> std::string {
  const auto text = need(encode_freedom_knowledge_json(knowledge, tick));
  const auto decoded = need(decode_freedom_knowledge_json(text, tick));
  check(decoded == knowledge &&
            need(encode_freedom_knowledge_json(decoded, tick)) == text,
        "Knowledge metadata must roundtrip with byte stability");
  return text;
}
auto station_income_fixture(const FreedomKnowledge& knowledge,
                            SimulationTick tick)
    -> std::unique_ptr<NativeFreedomFlightSession> {
  // Explicit same-craft station/financial metadata fixture, not a flown return.
  auto saved = std::make_unique<NativeSaveDocument>(std::move(
      need(native_derelict_recovery_new_game({42}, 0, false)).document));
  auto& mapped = std::get<FreedomKnowledgeSaveDocument>(*saved);
  mapped.knowledge = knowledge;
  auto& assembly =
      std::get<FreedomStartingAssemblySaveDocument>(mapped.voyage.voyage.base);
  auto& flight = assembly.journey.voyage.flight;
  const auto owner = need(generate_physical_origin_system({42}, 2));
  const auto station = generate_origin_station({42});
  const auto geometry = need(origin_station_geometry(station));
  flight.flight = need(release_origin_port(
      owner, station, geometry,
      {geometry.version, assembly.journey.voyage.docking.target,
       flight.flight.craft, tick}));
  flight.origin.state.tick = tick;
  mapped.voyage.resources.tick = tick;
  return std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_select_save_document(std::move(*saved))))));
}
auto historical() -> void {
  for (std::uint32_t recipe = 1; recipe <= 6; ++recipe) {
    auto knowledge = need(make_freedom_starting_knowledge({42}, recipe));
    const auto absent = stable(knowledge, 10);
    check(!Json::parse(absent).contains("derelict_recovery"),
          "Historical recipes must keep the recovery extension absent");
    knowledge.derelict_recovery.emplace();
    if (recipe != 6) {
      check(!encode_freedom_knowledge_json(knowledge, 10),
            "Old recipes cannot adopt recovery policy");
      continue;
    }
    check(!encode_freedom_knowledge_json(knowledge, 10),
          "Recovery selection requires the existing population selection");
    knowledge.derelicts.emplace();
    auto selected = Json::parse(stable(knowledge, 10));
    selected.erase("derelict_recovery");
    selected.erase("derelicts");
    check(selected.dump(2) + '\n' == absent,
          "Selecting empty recovery changes only explicitly selected metadata");
  }
  auto legacy = need(make_freedom_starting_knowledge({42}, 5));
  legacy.paid_operations = need(make_paid_operations(0, 0, 0));
  const auto text = stable(legacy, 10);
  check(Json::parse(text)["paid_operations"].size() == 9 &&
            !Json::parse(text)["paid_operations"].contains(
                "settled_specimen_count") &&
            !Json::parse(text)["paid_operations"].contains(
                "settled_canister_count"),
        "Legacy paid-v1 retains exactly its nine fields");
}
auto specimen_income_metadata() -> FreedomKnowledge {
  // Identity-only metadata, using the established bounded shell/cell fixture.
  // This is not a physical exposure inspection, collection or buyer command.
  auto knowledge = need(make_freedom_starting_knowledge({42}, 5));
  const auto owner = need(generate_physical_origin_system({42}, 2));
  const auto& planet = owner.catalog.planets.front().descriptor;
  const auto craft = need(derive_freedom_craft_identity({42}, 0));
  std::optional<FreedomFieldSampleRecord> record;
  const double radius = planet.radius.value * 1000.0;
  for (unsigned sample = 0; sample < 512 && !record; ++sample) {
    const double angle = 2 * std::numbers::pi * sample / 512;
    const SurfaceLandmarkCell base{
        static_cast<std::int32_t>(std::floor(radius * std::cos(angle) / 512)),
        static_cast<std::int32_t>(std::floor(radius * std::sin(angle) / 512)),
        0};
    for (std::int32_t x = -1; x <= 1 && !record; ++x)
      for (std::int32_t y = -1; y <= 1 && !record; ++y)
        for (std::int32_t z = -1; z <= 1 && !record; ++z) {
          const SurfaceLandmarkCell cell{base.x + x, base.y + y, base.z + z};
          const auto site = need(identify_surface_landmark(planet, cell));
          if (site)
            record =
                FreedomFieldSampleRecord{{owner.catalog.id, body_id(planet.id)},
                                         cell,
                                         site->id,
                                         3,
                                         site->kind,
                                         craft,
                                         8};
        }
  }
  check(record.has_value(),
        "Existing generated specimen metadata fixture unavailable");
  knowledge.surface_landmarks = FreedomSurfaceLandmarkState{};
  knowledge.surface_landmarks->inspections.push_back(
      {owner.catalog.id, planet.id, record->cell, record->site, 3, 7});
  knowledge.field_samples = FreedomFieldSampleState{1, craft, {}, {*record}};
  knowledge.specimen_sales =
      FreedomSpecimenSalesAccount{1, 1, {craft, 9, {*record}}};
  knowledge.paid_operations =
      need(make_paid_operations(PaidIncomeCounts{0, 1, 0}, 10, 0));
  return knowledge;
}
auto legacy_specimen_income() -> void {
  const auto knowledge = specimen_income_metadata();
  const auto text = stable(knowledge, 10);
  const auto account = Json::parse(text)["paid_operations"];
  check(account.size() == 10 && account["version"] == 2 &&
            account["settled_specimen_count"] == 1 &&
            !account.contains("settled_canister_count"),
        "Legacy paid-v2 retains its ten-field schema without a canister count");
}
auto retrieval_and_codecs() -> void {
  auto live = test_support::make_derelict_recovery_fixture();
  need(live->begin_derelict_recovery());
  for (unsigned tick = 0; tick < 60; ++tick)
    need(live->advance({}));
  const auto now = live->document().flight.tick;
  const auto& original = *live->knowledge();
  check(
      original.derelict_recovery->active &&
          original.derelict_recovery->active->acquired_ticks == 60,
      "Actual initialized approach must retain sixty real retrieval intervals");
  const auto text = stable(original, now);
  const auto root = Json::parse(text);
  check(need(decode_freedom_knowledge_json(text, now + 1)) == original &&
            !decode_freedom_knowledge_json(text, now - 1),
        "Historical last tick is admitted without backfill; future records "
        "refuse");
  auto saved =
      std::make_unique<NativeSaveDocument>(need(live->save_document()));
  const auto bytes = need(encode_native_save_document_json(*saved));
  auto restored = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(need(native_select_save_document(
          need(decode_native_save_document_json(bytes)))))));
  check(
      need(encode_native_save_document_json(need(restored->save_document()))) ==
          bytes,
      "Actual active retrieval survives complete travel Save/Continue exactly");
  auto& leaf = std::get<FreedomTravelSaveDocument>(*saved).voyage;
  need(validate_freedom_knowledge_document(leaf));
  // Preserve a fully valid resource/flight document while testing the new
  // active-source body join, independently of outer travel receipt validation.
  auto other = std::make_unique<FreedomKnowledgeSaveDocument>(leaf);
  auto& flight = std::get<FreedomFlightSaveDocument>(other->voyage.voyage.base);
  const auto descriptor = need(generate_freedom_derelict({42}));
  bool other_parent_checked{};
  for (const auto& planet : descriptor.owner.catalog.planets) {
    if (planet.descriptor.id == descriptor.planet) continue;
    flight.world->planet = planet.descriptor.id;
    flight.flight.frame.planet = planet.descriptor.id;
    need(validate_freedom_resource_document(other->voyage));
    const auto refused = validate_freedom_knowledge_document(*other);
    check(
        !refused &&
            refused.error().detail ==
                "active retrieval source craft or parent owner mismatch",
        "Valid alternate planet ownership must not carry an active retrieval");
    other_parent_checked = true;
    break;
  }
  check(other_parent_checked,
        "Seed42 owner must supply the alternate-parent boundary fixture");
  for (unsigned fault = 0; fault < 19; ++fault) {
    auto bad = root;
    auto& state = bad["derelict_recovery"];
    switch (fault) {
      case 0: state = nullptr; break;
      case 1: state["extra"] = 0U; break;
      case 2: state.erase("active"); break;
      case 3: state["version"] = std::uint64_t{1} << 40; break;
      case 4: state["version"] = 1.0; break;
      case 5: state["active"]["extra"] = false; break;
      case 6: state["active"]["started_tick"] = -1; break;
      case 7: state["active"]["last_tick"] = now + 1; break;
      case 8: state["active"]["acquired_ticks"] = UINT64_MAX; break;
      case 9: state["active"]["acquired_ticks"] = 60.0; break;
      case 10: state["active"]["observer"]["craft"] = true; break;
      case 11: state["active"]["observer"]["tick"] = now - 1; break;
      case 12:
        state["active"]["observer"]["position_metres"] = Json::array({0, 0});
        break;
      case 13:
        state["active"]["observer"]["velocity_metres_per_second"][0] = nullptr;
        break;
      case 14: state["active"]["observer"]["position_metres"][0] = 1e200; break;
      case 15: state["active"]["observer"]["craft"] = 1U; break;
      case 16: bad.erase("derelicts"); break;
      case 17: state["canister"] = Json::object(); break;
      case 18:
        state["active"]["observer"]["position_metres"][0] =
            std::numeric_limits<double>::infinity();
        break;
      default: break;
    }
    check(!decode_freedom_knowledge_json(bad.dump(), now),
          "Malformed retrieval types, clocks, bounds and owners must refuse");
  }
  auto duplicate = text;
  duplicate.insert(duplicate.find("\"derelict_recovery\":"),
                   "\"derelict_recovery\":null,");
  check(!decode_freedom_knowledge_json(duplicate, now),
        "Duplicate recovery selection must refuse");
  auto overflow = text;
  const auto at = overflow.find("\"acquired_ticks\": 60");
  check(at != std::string::npos, "Retrieval counter fixture missing");
  overflow.replace(at, std::string{"\"acquired_ticks\": 60"}.size(),
                   "\"acquired_ticks\": 18446744073709551616");
  check(!decode_freedom_knowledge_json(overflow, now),
        "Above-u64 counter cannot narrow");
  auto nonfinite = original;
  nonfinite.derelict_recovery->active->observer.position_metres.x =
      std::numeric_limits<double>::quiet_NaN();
  check(!encode_freedom_knowledge_json(nonfinite, now) &&
            stable(original, now) == text,
        "Nonfinite in-memory state refuses without changing the valid source");

  // Typed completion/sale metadata only, not an additional physical flight or
  // an Origin sale command. Native real-command coverage owns those claims.
  auto claimed = original;
  const auto completion_tick = claimed.derelict_recovery->active->started_tick +
                               kFreedomDerelictRetrievalTicks;
  const auto hull = need(resolve_freedom_derelict(descriptor, completion_tick));
  claimed.derelict_recovery->canister = FreedomRecoveredCanister{
      claimed.derelict_recovery->active->started_tick,
      {live->document().origin.state.craft,
       completion_tick,
       {hull.position_metres.x + 120, hull.position_metres.y,
        hull.position_metres.z},
       hull.linear_velocity_metres_per_second}};
  claimed.derelict_recovery->active.reset();
  stable(claimed, completion_tick);
  claimed.paid_operations =
      need(make_paid_operations(PaidIncomeCounts{}, now, 0));
  claimed.derelict_recovery = need(sell_freedom_derelict_canister(
      {42}, *claimed.derelict_recovery, *claimed.derelicts, completion_tick));
  claimed.paid_operations = need(settle_paid_canister_income(
      *claimed.paid_operations, {0, 0, 1}, completion_tick, 0));
  const auto sold_text = stable(claimed, completion_tick);
  const auto sold = Json::parse(sold_text);
  check(sold["paid_operations"].size() == 11 &&
            sold["paid_operations"]["settled_specimen_count"] == 0 &&
            sold["paid_operations"]["settled_canister_count"] == 1,
        "Paid-v3 requires exactly eleven fields including zero specimen count");
  {
    // Explicit composite historical-income metadata; no claim of a second
    // physical collection, return voyage or earned profit. Each underlying
    // sample/canister source still passes the full knowledge admission.
    auto all = claimed;
    const auto specimens = specimen_income_metadata();
    all.surface_landmarks = specimens.surface_landmarks;
    all.field_samples = specimens.field_samples;
    all.specimen_sales = specimens.specimen_sales;
    const auto network = need(generate_freedom_system_network({42}, 2));
    const auto waypoint = need(
        resolve_freedom_network_system(network, network.nodes[2].system, 2));
    all.survey_commissions = need(accept_targeted_survey_commission(
        {}, waypoint.catalog.id, waypoint.catalog.planets.front().descriptor.id,
        completion_tick));
    all.paid_operations = need(
        make_paid_operations(PaidIncomeCounts{0, 1, 1}, completion_tick, 0));
    const auto selected = stable(all, completion_tick);
    check(Json::parse(selected)["paid_operations"]["settled_specimen_count"] ==
                  1 &&
              Json::parse(
                  selected)["paid_operations"]["settled_canister_count"] == 1,
          "Alternative acceptance must retain both existing non-survey income "
          "witnesses");
    for (SimulationTick step = 1; step <= kSurveyCommissionMeasurementTicks;
         ++step)
      all.survey_commissions = need(advance_survey_commission(
          *all.survey_commissions, live->document().origin.state.craft,
          completion_tick + step, true));
    const auto submitted = completion_tick + kSurveyCommissionMeasurementTicks;
    all.survey_commissions =
        need(submit_survey_commission(*all.survey_commissions, 1, submitted));
    all.paid_operations = need(settle_paid_survey_income(
        *all.paid_operations, PaidIncomeCounts{1, 1, 1}, submitted, 0));
    const auto joined = Json::parse(stable(all, submitted));
    check(joined["survey_commissions"]["version"] == 3 &&
              joined["paid_operations"]["settled_paid_count"] == 1 &&
              joined["paid_operations"]["settled_specimen_count"] == 1 &&
              joined["paid_operations"]["settled_canister_count"] == 1 &&
              need(paid_operations_cash(*all.paid_operations,
                                        PaidIncomeCounts{1, 1, 1})) == 420,
          "Chosen payout joins all three real-source counters under the "
          "unchanged closed paid-v3 codec");
    // Exercise the actual Session transaction seams with these disclosed
    // historical-income/zero-cash boundaries. No new measurement is claimed.
    auto station_knowledge = all;
    station_knowledge.survey_commissions.reset();
    station_knowledge.paid_operations =
        need(make_paid_operations(PaidIncomeCounts{0, 1, 1}, submitted, 0));
    station_knowledge.paid_operations->spent_credits = 340;
    auto customer = station_income_fixture(station_knowledge, submitted);
    const BodyTarget target{
        waypoint.catalog.id,
        body_id(waypoint.catalog.planets.front().descriptor.id)};
    const auto offer = need(customer->survey_commission_view(
        SurveyCommissionKind::orbital, target));
    need(customer->accept_survey_commission_target(target, offer.offer_until,
                                                   offer.offer_sequence));
    {
      auto deficit =
          std::make_unique<NativeSaveDocument>(need(customer->save_document()));
      auto& d = std::get<FreedomKnowledgeSaveDocument>(*deficit);
      d.voyage.resources.jump_charges = 0;
      d.voyage.resources.flight_quanta = 0;
      customer = std::make_unique<NativeFreedomFlightSession>(
          need(NativeFreedomFlightSession::open(
              need(native_select_save_document(std::move(*deficit))))));
    }
    const auto support = need(customer->paid_operations_view());
    check(support.can_retainer, "Three-income source joins must not gate a "
                                "zero-charge chosen-job retainer");
    need(customer->request_paid_retainer(support.token));
    auto ready_knowledge = *customer->knowledge();
    for (SimulationTick step = 1; step <= kSurveyCommissionMeasurementTicks;
         ++step)
      ready_knowledge.survey_commissions = need(advance_survey_commission(
          *ready_knowledge.survey_commissions,
          customer->document().origin.state.craft, submitted + step, true));
    customer = station_income_fixture(
        ready_knowledge, submitted + kSurveyCommissionMeasurementTicks);
    need(customer->submit_survey_commission(1));
    check(customer->knowledge()->paid_operations->settled_paid_count == 1 &&
              customer->knowledge()->paid_operations->settled_specimen_count ==
                  1 &&
              customer->knowledge()->paid_operations->settled_canister_count ==
                  1 &&
              customer->knowledge()->specimen_sales ==
                  station_knowledge.specimen_sales &&
              customer->knowledge()->derelict_recovery ==
                  station_knowledge.derelict_recovery,
          "Actual chosen acceptance/support/payout must preserve specimen and "
          "canister source receipts");
    const auto retired = std::make_unique<FreedomRecoverySaveDocument>(
        need(customer->recovery_document()));
    need(customer->record_loss(
        FreedomLossCause::recoverable_destruction,
        customer->document().origin.state.craft,
        customer->document().flight.tick,
        need(freedom_recovery_source_checksum(retired->voyage))));
    need(customer->complete_recovery());
    check(customer->knowledge()->paid_operations->settled_paid_count == 1 &&
              customer->knowledge()->paid_operations->settled_specimen_count ==
                  1 &&
              customer->knowledge()->paid_operations->settled_canister_count ==
                  1 &&
              need(paid_operations_cash(*customer->knowledge()->paid_operations,
                                        PaidIncomeCounts{1, 1, 1})) == 0,
          "Actual replacement retains all three income counts with the "
          "existing insurance/retainer ordering");
  }
  for (unsigned fault = 0; fault < 10; ++fault) {
    auto bad = sold;
    switch (fault) {
      case 0: bad["paid_operations"].erase("settled_specimen_count"); break;
      case 1: bad["paid_operations"].erase("settled_canister_count"); break;
      case 2: bad["paid_operations"]["settled_canister_count"] = 0U; break;
      case 3:
        bad["paid_operations"]["settled_canister_count"] = UINT64_MAX;
        break;
      case 4: bad["paid_operations"]["settled_canister_count"] = 1.0; break;
      case 5: bad["paid_operations"]["version"] = 2U; break;
      case 6:
        bad["derelict_recovery"]["canister"]["disposition"] = "unknown";
        break;
      case 7:
        bad["derelict_recovery"]["canister"]["sold_tick"] = nullptr;
        break;
      case 8:
        bad["derelict_recovery"]["canister"]["sold_tick"] = completion_tick - 1;
        break;
      case 9:
        bad["derelict_recovery"]["canister"]["completed"]["extra"] = 0U;
        break;
      default: break;
    }
    check(!decode_freedom_knowledge_json(bad.dump(), completion_tick),
          "Malformed paid-v3 or finite sale receipt must refuse");
  }
}
} // namespace
auto main() -> int {
  try {
    historical();
    legacy_specimen_income();
    retrieval_and_codecs();
    std::cout << checks << " derelict recovery save checks, 0 failures\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Derelict recovery save: " << error.what() << '\n';
    return 1;
  }
}
