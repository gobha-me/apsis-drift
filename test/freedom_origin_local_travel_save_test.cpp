#include "apsis_drift/freedom_condition_save.hpp"
#include "apsis_drift/freedom_recovery_save.hpp"
#include "apsis_drift/moon_surface_landmarks.hpp"
#include "native_minor_route_fixture.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <utility>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
using minor_route_fixture::need;
unsigned checks{};
auto check(bool good, const char* message) -> void {
  ++checks;
  if (!good) throw std::runtime_error(message);
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
        "Origin48 canonical roundtrip preserves complete typed state");
  check(Json::parse(encoded)["format_version"] == 48,
        "Origin route composition has explicit outer48");
  return encoded;
}
auto adopt_codec(const FreedomTravelSaveDocument& old, BodyTarget selected)
    -> FreedomTravelSaveDocument {
  auto d = old;
  d.travel.origin_routes = FreedomOriginLocalRoutes{};
  d.travel.origin_routes->legacy = {old.travel.version, *old.travel.local,
                                    old.travel.prior_moon,
                                    old.travel.next_attempt};
  d.travel.version = kFreedomOriginLocalTravelStateVersion;
  d.travel.local = FreedomLocalTravelState{};
  d.travel.local->selected = selected;
  d.travel.prior_moon.reset();
  return d;
}
auto adoption() -> FreedomTravelSaveDocument {
  auto native = minor_route_fixture::departure();
  need(native.select_local_jump(minor_route_fixture::minor_target(native)));
  const auto catalog = need(generate_secondary_body_catalog(native.system()));
  const BodyTarget moon_target{native.system().catalog.id,
                               catalog.bodies[0].node.id};
  const auto merely_selected = need(native.travel_document());
  const auto change_mind = adopt_codec(merely_selected, moon_target);
  roundtrip(change_mind);
  check(change_mind.travel.origin_routes->legacy.local ==
                *merely_selected.travel.local &&
            !change_mind.travel.origin_routes->legacy.local.committed &&
            change_mind.voyage == merely_selected.voyage &&
            change_mind.travel.next_attempt == 1,
        "Selected-but-uncommitted minor47 can change mind at home without a "
        "fabricated proof or charge");
  minor_route_fixture::leg(native);
  need(native.select_local_jump(minor_route_fixture::home_target(native)));
  minor_route_fixture::leg(native);
  const auto old = need(native.travel_document());
  const auto old_bytes = need(encode_freedom_travel_document_json(old));
  check(Json::parse(old_bytes)["format_version"] == 47 &&
            need(encode_freedom_travel_document_json(need(
                decode_freedom_travel_document_json(old_bytes)))) == old_bytes,
        "Actual completed minor legacy47 remains byte-exact");
  auto d = adopt_codec(old, moon_target);
  roundtrip(d);
  check(d.voyage == old.voyage &&
            d.travel.next_attempt == old.travel.next_attempt &&
            d.travel.origin_routes->legacy.local == *old.travel.local &&
            !d.travel.local->outbound &&
            !d.travel.origin_routes->inactive.local.outbound &&
            !d.travel.origin_routes->excursion_outbound,
        "Explicit adoption retains exact old proofs/resources and starts empty "
        "live routes");
  return d;
}
auto advance_tick(FreedomTravelSaveDocument& d, SimulationTick tick) -> void {
  auto& flight =
      std::get<FreedomFlightSaveDocument>(d.voyage.voyage.voyage.base);
  flight.flight.tick = tick;
  flight.origin.state.tick = tick;
  d.voyage.voyage.resources.tick = tick;
}
auto commit(FreedomTravelSaveDocument& d) -> void {
  const auto flight = flight_of(d);
  const auto& routes = *d.travel.origin_routes;
  const bool home = !flight.moon && !flight.minor;
  const auto model =
      home ? flight.model : routes.excursion_outbound->request.home_model;
  const auto frozen = need(freeze_local_body_jump_arrival(
      {routes.active == FreedomLocalRouteKind::moon ? 1U : 2U, flight,
       *d.travel.local->selected, d.travel.next_attempt, model}));
  auto& local = *d.travel.local;
  local.committed = frozen;
  local.commitment_resources = d.voyage.voyage.resources;
  if (!local.outbound) local.outbound = frozen;
  if (home) d.travel.origin_routes->excursion_outbound = frozen;
  --d.voyage.voyage.resources.jump_charges;
  ++d.travel.next_attempt;
  d.travel.phase = FreedomJumpPhase::transit;
}
auto arrive(FreedomTravelSaveDocument& d) -> void {
  const auto arrival = d.travel.local->committed->arrival;
  d.voyage.voyage.voyage.base = arrival;
  d.voyage.voyage.resources.tick = arrival.flight.tick;
  d.travel.phase = FreedomJumpPhase::idle;
  d.travel.spool_tick = 0;
  d.travel.local->selected.reset();
}
auto spool(FreedomTravelSaveDocument& d) -> void {
  d.travel.phase = FreedomJumpPhase::spool;
  d.travel.spool_tick = flight_of(d).flight.tick;
  roundtrip(d);
  advance_tick(d, d.travel.spool_tick + kJumpSpoolTicks);
  commit(d);
  roundtrip(d);
}
auto personal_metadata_fixture(FreedomTravelSaveDocument& d) -> void {
  // Authenticated identity/codec fixture only. These records do NOT claim a
  // physical planetary landing or an additional Moon walk by this route.
  const auto flight = flight_of(d);
  const auto system = need(
      generate_physical_origin_system(flight.origin.recipe.universe_seed, 2));
  const auto catalog = need(generate_secondary_body_catalog(system));
  const auto moon = need(
      generate_physical_moon_descriptor(catalog, catalog.bodies[0].node.id));
  auto& knowledge = d.voyage.knowledge;
  knowledge.surface_marker = FreedomSurfaceMarkerState{
      2, FreedomMoonSurfaceMarkerPoint{
             system.catalog.id,
             moon.body,
             {0, static_cast<double>(moon.radius_metres) - 100, 0},
             7}};
  knowledge.surface_landmarks = FreedomSurfaceLandmarkState{};
  knowledge.moon_surface_landmarks = FreedomMoonSurfaceLandmarkState{};
  const auto& planet = system.catalog.planets.front().descriptor;
  const auto planet_x = static_cast<std::int32_t>(
      std::floor(static_cast<double>(planet.radius.value) * 1000 / 512));
  const auto moon_x =
      static_cast<std::int32_t>(std::floor(moon.radius_metres / 512));
  for (std::int32_t x = -1; x <= 1; ++x)
    for (std::int32_t y = -1; y <= 1; ++y)
      for (std::int32_t z = -1; z <= 1; ++z) {
        if (knowledge.surface_landmarks->inspections.empty()) {
          const SurfaceLandmarkCell cell{planet_x + x, y, z};
          const auto identity = need(identify_surface_landmark(planet, cell));
          if (identity)
            knowledge.surface_landmarks->inspections.push_back(
                {system.catalog.id, planet.id, cell, identity->id, 3, 7});
        }
        if (knowledge.moon_surface_landmarks->inspections.empty()) {
          const SurfaceLandmarkCell cell{moon_x + x, y, z};
          const auto identity =
              need(identify_moon_surface_landmark(moon, cell));
          if (identity)
            knowledge.moon_surface_landmarks->inspections.push_back(
                {{system.catalog.id, moon.body}, cell, identity->id, 1, 7});
        }
      }
  check(knowledge.surface_landmarks->inspections.size() == 1 &&
            knowledge.moon_surface_landmarks->inspections.size() == 1,
        "Bounded codec fixture has genuine typed planetary and Moon inspection "
        "identities");
  roundtrip(d);
}
auto exercise(FreedomTravelSaveDocument d) -> void {
  personal_metadata_fixture(d);
  const auto personal = d.voyage.knowledge;
  const auto legacy = d.travel.origin_routes->legacy;
  // Codec-only ledger fixture, not an assertion of actual attached service.
  // The independent native reuse route proves real approach/capture/service.
  advance_tick(d, flight_of(d).flight.tick + 1);
  d.voyage.voyage.resources.jump_charges = 3;
  const auto home = flight_of(d);
  const auto physical = need(
      generate_physical_origin_system(home.origin.recipe.universe_seed, 2));
  const auto catalog = need(generate_secondary_body_catalog(physical));
  const BodyTarget home_target{physical.catalog.id,
                               body_id(home.origin.recipe.home_planet)};
  const BodyTarget moon_target{physical.catalog.id, catalog.bodies[0].node.id};
  spool(d);
  const auto transit = d;
  for (unsigned fault = 0; fault < 15; ++fault) {
    auto bad = transit;
    auto& routes = *bad.travel.origin_routes;
    switch (fault) {
      case 0: routes.version = 2; break;
      case 1: routes.active = static_cast<FreedomLocalRouteKind>(2); break;
      case 2: routes.legacy.version = 4; break;
      case 3: ++routes.legacy.next_attempt; break;
      case 4: routes.legacy.local.committed.reset(); break;
      case 5: ++bad.travel.next_attempt; break;
      case 6: routes.excursion_outbound.reset(); break;
      case 7: bad.travel.local->commitment_resources.reset(); break;
      case 8: ++bad.voyage.voyage.resources.jump_charges; break;
      case 9: ++bad.voyage.voyage.resources.flight_quanta; break;
      case 10: ++bad.travel.spool_tick; break;
      case 11: bad.travel.local->selected = home_target; break;
      case 12:
        routes.inactive.local = *bad.travel.local;
        routes.inactive.local.selected.reset();
        routes.inactive.excursion_outbound = routes.excursion_outbound;
        break;
      case 13:
        ++routes.excursion_outbound->request.home_model.terrain_policy;
        break;
      case 14:
        bad.travel.local->outbound->request.attempt =
            std::numeric_limits<std::uint64_t>::max();
        break;
    }
    check(!validate_freedom_travel_document(bad) &&
              !encode_freedom_travel_document_json(bad),
          "Malformed origin owner/history/clock/debit/checkpoint refuses");
  }
  arrive(d);
  roundtrip(d);
  check(flight_of(d).moon.has_value(),
        "Codec excursion arrives at a real typed Moon");
  d.travel.local->selected = home_target;
  spool(d);
  arrive(d);
  roundtrip(d);
  check(flight_of(d).model == home.model &&
            d.travel.origin_routes->legacy == legacy,
        "Return restores actual outgoing model and preserves immutable legacy");
  // Swap only complete home checkpoints, retaining the original first Moon
  // outbound while the other route remains an independently empty checkpoint.
  auto& routes = *d.travel.origin_routes;
  std::swap(*d.travel.local, routes.inactive.local);
  std::swap(routes.excursion_outbound, routes.inactive.excursion_outbound);
  routes.active = FreedomLocalRouteKind::minor;
  d.travel.local->selected =
      BodyTarget{physical.catalog.id, catalog.bodies[1].node.id};
  const auto switched = roundtrip(d);
  const auto first_moon_proof = d.travel.origin_routes->inactive.local.outbound;
  check(!d.travel.local->outbound && routes.inactive.local.outbound &&
            routes.legacy == legacy,
        "Idle home route swap preserves first/history and does not fabricate "
        "minor outbound");
  auto wrong = d;
  wrong.travel.origin_routes->inactive.local.selected = moon_target;
  check(!validate_freedom_travel_document(wrong),
        "Inactive route cannot retain live selection");
  wrong = d;
  wrong.travel.origin_routes->inactive.local.committed->request.destination =
      moon_target;
  check(!validate_freedom_travel_document(wrong),
        "Inactive route must be a completed home arrival");
  for (unsigned fault = 0; fault < 10; ++fault) {
    auto raw = Json::parse(switched);
    auto& history = raw["travel"]["origin_routes"];
    switch (fault) {
      case 0: history["extra"] = 0; break;
      case 1: history["version"] = nullptr; break;
      case 2: history["active"] = 2U; break;
      case 3: history["inactive"]["extra"] = 0; break;
      case 4: history["inactive"]["local"]["extra"] = 0; break;
      case 5: history["legacy"]["extra"] = 0; break;
      case 6: history["legacy"]["version"] = 4U; break;
      case 7: history["legacy"]["next_attempt"] = -1; break;
      case 8: raw["travel"]["prior_moon"] = nullptr; break;
      case 9:
        history["inactive"]["excursion_outbound"]["destination"]["identity"] =
            "00";
        break;
    }
    check(!decode_freedom_travel_document_json(raw.dump()),
          "Closed48 malformed nested schema refuses");
  }
  auto at_limit = switched;
  at_limit.append(kMaximumSaveDocumentBytes - at_limit.size(), ' ');
  check(need(decode_freedom_travel_document_json(at_limit)) == d,
        "Exactly1MiB bounded input preserves canonical origin state");
  auto oversize = at_limit;
  oversize.push_back(' ');
  check(!decode_freedom_travel_document_json(oversize),
        "Origin codec retains whole-document byte cap");
  auto recovered = FreedomRecoverySaveDocument{d, {}};
  check(need(decode_freedom_recovery_document_json(need(
            encode_freedom_recovery_document_json(recovered)))) == recovered,
        "Recovery wrapper admits explicit48 without a new outer version");
  const auto source = flight_of(d);
  const auto lost = need(record_freedom_loss(
      recovered, FreedomLossCause::recoverable_destruction,
      source.origin.state.craft, source.flight.tick,
      need(freedom_recovery_source_checksum(recovered.voyage))));
  const auto replacement = need(complete_freedom_recovery(lost));
  const auto& retained = recovery_knowledge(replacement.voyage).knowledge;
  check(retained.surface_marker == personal.surface_marker &&
            retained.surface_landmarks == personal.surface_landmarks &&
            retained.moon_surface_landmarks ==
                personal.moon_surface_landmarks &&
            need(decode_freedom_recovery_document_json(
                need(encode_freedom_recovery_document_json(replacement)))) ==
                replacement,
        "Recovery preserves both inspection histories and marker with "
        "retired48 travel admitted");
  auto condition = need(make_craft_condition(flight_of(d)));
  FreedomConditionSaveDocument owned{1, d, condition, {}};
  check(need(decode_freedom_condition_document_json(
            need(encode_freedom_condition_document_json(owned)))) == owned,
        "Condition wrapper admits explicit48 without a new outer version");
  // New excursion after a home preference change uses a fresh true source
  // profile, while the first new48 Moon proof stays in the inactive slot.
  advance_tick(d, flight_of(d).flight.tick + 1);
  d.voyage.voyage.resources.jump_charges = 3;
  auto& flight =
      std::get<FreedomFlightSaveDocument>(d.voyage.voyage.voyage.base);
  flight.model.assistance = !flight.model.assistance;
  const auto new_model = flight.model;
  spool(d);
  arrive(d);
  d.travel.local->selected = home_target;
  spool(d);
  arrive(d);
  roundtrip(d);
  check(flight_of(d).model == new_model &&
            d.travel.origin_routes->legacy == legacy &&
            d.travel.origin_routes->inactive.local.outbound ==
                first_moon_proof &&
            d.voyage.knowledge == personal,
        "Other route excursion uses actual fresh home model without rewriting "
        "old span");
  // Complete a second Minor excursion so its historical first5 differs from
  // its current outbound7/return8. This is the same labelled codec fixture,
  // not another claimed flight/service/landing route.
  advance_tick(d, flight_of(d).flight.tick + 1);
  d.voyage.voyage.resources.jump_charges = 3;
  d.travel.local->selected =
      BodyTarget{physical.catalog.id, catalog.bodies[1].node.id};
  spool(d);
  arrive(d);
  d.travel.local->selected = home_target;
  spool(d);
  arrive(d);
  roundtrip(d);
  auto reversed_clock = d;
  auto request = reversed_clock.travel.local->outbound->request;
  const auto earlier_source = reversed_clock.travel.origin_routes->inactive
                                  .local.outbound->request.source.flight.tick;
  request.source.flight.tick = earlier_source;
  request.source.origin.state.tick = earlier_source;
  const auto regenerated = need(freeze_local_body_jump_arrival(request));
  check(validate_frozen_local_body_jump_arrival(regenerated).has_value(),
        "Cross-route clock counterexample is a fully regenerated valid frozen "
        "leg");
  reversed_clock.travel.local->outbound = regenerated;
  const auto refused = validate_freedom_travel_document(reversed_clock);
  check(!refused &&
            refused.error().detail ==
                "Origin known receipts reverse the shared clock" &&
            !encode_freedom_travel_document_json(reversed_clock),
        "Known first5 before first3 arrival refuses specifically shared-clock "
        "chronology");
}
} // namespace
int main() {
  try {
    auto d = adoption();
    roundtrip(d);
    for (const auto oldversion :
         {kFreedomLocalTravelStateVersion, kFreedomMinorTravelStateVersion,
          kFreedomTravelStateVersion}) {
      auto wrong = d;
      wrong.travel.version = oldversion;
      check(!validate_freedom_travel_document(wrong),
            "Origin routes cannot hide in an old travel version");
    }
    exercise(std::move(d));
    std::cout << "PASS origin local travel codec " << checks << " checks\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL origin local travel codec: " << error.what() << '\n';
    return 1;
  }
}
