#include "apsis_drift/native_flight_session.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <numbers>
#include <type_traits>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
int checks{}, failures{};
std::uint64_t fingerprint{1469598103934665603ULL};
auto check(bool ok, std::string_view message) -> void {
  ++checks;
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <typename T, typename E> auto need(std::expected<T, E> r) -> T {
  if (!r) throw std::runtime_error("Knowledge fixture refused");
  if constexpr (std::is_void_v<T>)
    return;
  else
    return std::move(*r);
}
auto bytes(const std::filesystem::path& p) -> std::string {
  std::ifstream f{p, std::ios::binary};
  return {std::istreambuf_iterator<char>{f}, {}};
}
auto hash(std::string_view s) -> void {
  for (const auto c : s) {
    fingerprint ^= static_cast<unsigned char>(c);
    fingerprint *= 1099511628211ULL;
  }
}
auto event(Seed seed, KnowledgeSubject subject, KnowledgeFact fact,
           NavigationKnowledgeLevel level, SimulationTick tick)
    -> KnowledgeEvidence {
  const auto craft = make_freedom_new_game_document(seed).state.craft;
  return {subject,
          fact,
          {level,
           fact == KnowledgeFact::presence ? KnowledgeSource::physical_arrival
                                           : KnowledgeSource::local_ship,
           craft.value, tick, craft}};
}
auto fixture(Seed seed) -> void {
  const auto truth = need(generate_physical_origin_system(seed, 2));
  const auto route = generate_first_universe_route(seed);
  const auto home = truth.catalog.planets[0].descriptor.id;
  const KnowledgeSubject body{KnowledgeSubjectKind::planet, route.origin,
                              home.value};
  auto k = need(make_freedom_starting_knowledge(seed));
  check(validate_freedom_knowledge(k, 0).has_value(),
        "Explicit chart baseline qualifies at new-game tick");
  hash(need(encode_freedom_knowledge_json(k, 0)));
  for (const auto& e : k.entries) {
    check(e.transitions.size() == 1 && e.transitions.front().tick == 0 &&
              e.transitions.front().source == KnowledgeSource::starting_chart &&
              e.transitions.front().level ==
                  NavigationKnowledgeLevel::resolved &&
              e.transitions.front().flight == StarterCraftId{} &&
              (e.fact == KnowledgeFact::identity_type ||
               e.fact == KnowledgeFact::location ||
               e.fact == KnowledgeFact::station_ports),
          "Granted infrastructure is not a ship observation, visit or hazard");
  }
  check(
      !need(query_freedom_knowledge(k, body, KnowledgeFact::physical, 0)) &&
          !need(
              query_freedom_knowledge(k, body, KnowledgeFact::atmosphere, 0)) &&
          !need(query_freedom_knowledge(k, body, KnowledgeFact::hazards, 0)) &&
          !need(query_freedom_knowledge(k, body, KnowledgeFact::presence, 0)),
      "Known location grants no physical/environment/visit facts");
  const auto station = generate_origin_station(seed);
  const auto orbit = *need(query_freedom_knowledge(
      k, {KnowledgeSubjectKind::station, route.origin, station.id.value},
      KnowledgeFact::location, 0));
  check(std::get<KnownOrbit>(orbit.value).host_planet ==
            station.orbit.host_planet,
        "Station orbit retains its actual granted host relation");
  const KnowledgeSubject hidden{KnowledgeSubjectKind::planet, route.origin,
                                truth.catalog.planets[1].descriptor.id.value};
  const KnowledgeSubject invalid{KnowledgeSubjectKind::planet, route.origin,
                                 home.value ^ 1ULL};
  check(!need(query_freedom_knowledge(k, hidden, KnowledgeFact::physical, 0)) &&
            !need(query_freedom_knowledge(k, invalid, KnowledgeFact::physical,
                                          0)),
        "Hidden valid and nonexistent subjects have the same absent query");
  const auto listed = need(known_freedom_subjects(k, 0));
  check(std::ranges::find(listed, hidden) == listed.end() &&
            std::ranges::find(listed, invalid) == listed.end(),
        "Listing comes from earned/granted records, not generated catalogs");
  const auto baseline_text = need(encode_freedom_knowledge_json(k, 0));
  check(need(decode_freedom_knowledge_json(baseline_text, 0)) == k,
        "Baseline recipe/provenance roundtrips exactly");
  auto resumed = need(decode_freedom_knowledge_json(baseline_text, 0));
  for (unsigned step = 0; step < 3; ++step) {
    const auto level = static_cast<NavigationKnowledgeLevel>(step);
    const auto evidence =
        event(seed, body, KnowledgeFact::physical, level, step + 1);
    k = need(apply_freedom_knowledge(k, evidence, step + 1));
    resumed = need(apply_freedom_knowledge(resumed, evidence, step + 1));
    const auto encoded = need(encode_freedom_knowledge_json(k, step + 1));
    hash(encoded);
    resumed = need(decode_freedom_knowledge_json(encoded, step + 1));
    check(
        resumed == k &&
            need(apply_freedom_knowledge(k, evidence, step + 1)) == k,
        "Save/resume each transition and duplicate evidence converge exactly");
    const auto reading = *need(
        query_freedom_knowledge(k, body, KnowledgeFact::physical, step + 1));
    check(reading.provenance == evidence.transition &&
              ((step < 2 &&
                std::holds_alternative<std::monostate>(reading.value)) ||
               (step == 2 &&
                std::get<KnownPhysical>(reading.value) ==
                    KnownPhysical{
                        truth.catalog.planets[0].descriptor.radius.value,
                        truth.catalog.planets[0]
                            .descriptor.surface_gravity.value})),
          "Only resolved physical evidence discloses precise physical values");
    check(!need(query_freedom_knowledge(k, body, KnowledgeFact::atmosphere,
                                        step + 1)) &&
              !need(query_freedom_knowledge(k, body, KnowledgeFact::hazards,
                                            step + 1)),
          "Physical confidence cannot unlock unrelated facts");
  }
  check(
      need(apply_freedom_knowledge(k,
                                   event(seed, body, KnowledgeFact::physical,
                                         NavigationKnowledgeLevel::contact, 1),
                                   3)) == k,
      "Out-of-order lower evidence adds no history or downgrade");
  for (auto fact : {KnowledgeFact::atmosphere, KnowledgeFact::hazards,
                    KnowledgeFact::landing}) {
    k = need(apply_freedom_knowledge(
        k, event(seed, body, fact, NavigationKnowledgeLevel::resolved, 4), 4));
    check(!std::holds_alternative<std::monostate>(
              need(query_freedom_knowledge(k, body, fact, 4))->value),
          "Independently resolved facts have their own typed redacted value");
  }
  const auto found_hazards = std::get<KnownHazards>(
      need(query_freedom_knowledge(k, body, KnowledgeFact::hazards, 4))->value);
  const auto ambient = need(resolve_planet_ambient_environment(
      truth, need(generate_planet_ambient_recipe(truth, home))));
  check(found_hazards.min_temperature_mk ==
                ambient.minimum_temperature_millikelvin &&
            found_hazards.radiation_nsv_per_hour ==
                ambient.radiation_nanosieverts_per_hour,
        "Knowledge selects the actual origin ambient recipe, without copying "
        "its seed");
  FreedomResources ledger{1,
                          make_freedom_new_game_document(seed).state.craft,
                          {kWayfarerFrameId, kWayfarerFrameVersion},
                          4};
  auto view =
      need(resolve_freedom_knowledge_chart(k, route.destination, ledger));
  check(std::ranges::all_of(view.destinations,
                            [](const auto& r) {
                              return r.knowledge ==
                                     NavigationKnowledgeLevel::resolved;
                            }),
        "Changing current system does not invent an arrival");
  const KnowledgeSubject destination{
      KnowledgeSubjectKind::system, route.destination, route.destination.value};
  k = need(
      apply_freedom_knowledge(k,
                              event(seed, destination, KnowledgeFact::presence,
                                    NavigationKnowledgeLevel::visited, 5),
                              5));
  ledger.tick = 5;
  ledger.jump_charges = 0;
  view = need(resolve_freedom_knowledge_chart(k, route.destination, ledger));
  check(view.destinations[1].knowledge == NavigationKnowledgeLevel::visited &&
            view.destinations[0].knowledge ==
                NavigationKnowledgeLevel::resolved &&
            view.destinations[0].known && !view.destinations[0].affordable &&
            !view.destinations[0].selectable,
        "Recorded arrival marks only that system and keeps unaffordable home "
        "known");
  hash(need(encode_freedom_knowledge_json(k, 5)));
  check(need(generate_physical_origin_system(seed, 2)) == truth &&
            generate_first_universe_route(seed) == route,
        "Learning and serialization change no generated world");
}

auto invalid_inputs() -> void {
  const Seed seed{42};
  const auto k = need(make_freedom_starting_knowledge(seed));
  const auto route = generate_first_universe_route(seed);
  const auto truth = need(generate_physical_origin_system(seed, 2));
  const KnowledgeSubject body{KnowledgeSubjectKind::planet, route.origin,
                              truth.catalog.planets[0].descriptor.id.value};
  auto good = event(seed, body, KnowledgeFact::physical,
                    NavigationKnowledgeLevel::resolved, 1);
  for (unsigned fault = 0; fault < 10; ++fault) {
    auto bad = good;
    switch (fault) {
      case 0: ++bad.subject.identity; break;
      case 1: bad.subject.kind = static_cast<KnowledgeSubjectKind>(255); break;
      case 2: bad.fact = KnowledgeFact::station_ports; break;
      case 3: ++bad.transition.source_id; break;
      case 4: ++bad.transition.flight.value; break;
      case 5: bad.transition.tick = 2; break;
      case 6: bad.transition.source = KnowledgeSource::starting_chart; break;
      case 7: bad.transition.level = NavigationKnowledgeLevel::visited; break;
      case 8: bad.transition.source = static_cast<KnowledgeSource>(255); break;
      case 9:
        bad.transition.level = static_cast<NavigationKnowledgeLevel>(255);
        break;
      default: break;
    }
    check(!apply_freedom_knowledge(k, bad, 1),
          "Bad subjects, sources, ticks and impossible confidence refuse "
          "atomically");
  }
  auto contact =
      need(apply_freedom_knowledge(k,
                                   event(seed, body, KnowledgeFact::physical,
                                         NavigationKnowledgeLevel::contact, 1),
                                   1));
  check(!apply_freedom_knowledge(contact, good, 1),
        "Conflicting confidence from the same fact/source/tick refuses");
  for (unsigned fault = 0; fault < 10; ++fault) {
    auto bad = k;
    switch (fault) {
      case 0: ++bad.recipe.version; break;
      case 1: ++bad.recipe.chart; break;
      case 2: ++bad.recipe.topology; break;
      case 3: ++bad.recipe.physical_catalog; break;
      case 4: ++bad.recipe.ephemeris; break;
      case 5: ++bad.recipe.ambient; break;
      case 6: bad.entries.erase(bad.entries.begin()); break;
      case 7: bad.entries.push_back(bad.entries.back()); break;
      case 8:
        bad.entries.front().transitions.front().level =
            NavigationKnowledgeLevel::visited;
        break;
      case 9: bad.entries.resize(129, bad.entries.front()); break;
      default: break;
    }
    check(!validate_freedom_knowledge(bad, 1) &&
              !encode_freedom_knowledge_json(bad, 1),
          "Invalid version, missing baseline, duplicate or oversized ledger "
          "refuses");
  }
  check(!validate_freedom_knowledge(k, UINT64_MAX),
        "Unadvanceable clock refuses");
  const auto encoded = need(encode_freedom_knowledge_json(contact, 1));
  for (unsigned fault = 0; fault < 11; ++fault) {
    auto root = Json::parse(encoded);
    auto& t = root["entries"].back()["transitions"][0];
    switch (fault) {
      case 0: root["recipe"]["version"] = UINT64_MAX; break;
      case 1: root["recipe"]["universe_seed"] = -1; break;
      case 2: root["recipe"]["topology"] = 1.0; break;
      case 3: root["extra"] = 0; break;
      case 4: root["entries"][0]["kind"] = UINT64_MAX; break;
      case 5: root["entries"][0]["fact"] = 255; break;
      case 6: t["source"] = 255; break;
      case 7: t["level"] = 255; break;
      case 8: t["tick"] = UINT64_MAX; break;
      case 9: t["flight"] = "42"; break;
      case 10: root["entries"][0]["transitions"] = Json::array(); break;
      default: break;
    }
    check(!decode_freedom_knowledge_json(root.dump(), 1),
          "Closed JSON fields, enum/unsigned bounds and provenance are "
          "validated");
  }
  auto duplicate = encoded;
  const auto pos = duplicate.find("\"version\": 1");
  duplicate.replace(pos, 12, "\"version\": 1, \"version\": 1");
  check(!decode_freedom_knowledge_json(duplicate, 1),
        "Nested duplicate JSON key refuses");
  check(!decode_freedom_knowledge_json(std::string(65537, ' '), 1) &&
            !decode_freedom_knowledge_json(
                std::string(65, '[') + std::string(65, ']'), 1),
        "Byte and nesting bounds refuse before unbounded parsing");
}

auto surface_marker_metadata() -> void {
  const Seed seed{42};
  const auto system = need(generate_physical_origin_system(seed, 2));
  const auto& planet = system.catalog.planets.front().descriptor;
  const FreedomSurfaceMarkerPoint point{
      system.catalog.id,
      planet.id,
      {static_cast<double>(planet.radius.value) * 1000 + 123.5, -0.0, 0},
      7};
  for (auto version :
       {kFreedomKnowledgeVersion, kFreedomObservedKnowledgeVersion,
        kFreedomTravelKnowledgeVersion}) {
    auto legacy = need(make_freedom_starting_knowledge(seed, version));
    const auto text = need(encode_freedom_knowledge_json(legacy, 7));
    check(Json::parse(text).size() == 2 &&
              !Json::parse(text).contains("surface_marker") &&
              need(encode_freedom_knowledge_json(
                  need(decode_freedom_knowledge_json(text, 7)), 7)) == text,
          "Unselected historical knowledge recipes retain their exact JSON");
    legacy.surface_marker = FreedomSurfaceMarkerState{1, point};
    if (version == kFreedomKnowledgeVersion) {
      check(!validate_freedom_knowledge(legacy, 7) &&
                !encode_freedom_knowledge_json(legacy, 7),
            "Historical chart-only policy cannot select marker metadata");
      continue;
    }
    const auto entries = legacy.entries;
    const auto recipe = legacy.recipe;
    const auto encoded = need(encode_freedom_knowledge_json(legacy, 7));
    auto expected = Json::parse(text);
    expected["surface_marker"] = {
        {"version", 1U},
        {"point",
         {{"system", point.system.value},
          {"planet", point.planet.value},
          {"body_fixed_metres",
           Json::array({point.position.x, point.position.y, point.position.z})},
          {"tick", point.tick}}}};
    check(encoded == expected.dump(2) + '\n',
          "Original selected planetary marker retains exact ordered bytes");
    check(need(decode_freedom_knowledge_json(encoded, 7)) == legacy &&
              legacy.entries == entries && legacy.recipe == recipe,
          "Selected marker roundtrips point and tick without changing facts "
          "or recipe domains");
    legacy.surface_marker->point.reset();
    const auto cleared = need(encode_freedom_knowledge_json(legacy, 7));
    check(Json::parse(cleared)["surface_marker"]["point"].is_null() &&
              need(decode_freedom_knowledge_json(cleared, 7)) == legacy,
          "Explicitly cleared metadata retains selected version1");
  }
  auto marked = need(
      make_freedom_starting_knowledge(seed, kFreedomObservedKnowledgeVersion));
  marked.surface_marker = FreedomSurfaceMarkerState{1, point};
  for (unsigned fault = 0; fault < 10; ++fault) {
    auto bad = marked;
    auto& m = std::get<FreedomSurfaceMarkerPoint>(*bad.surface_marker->point);
    switch (fault) {
      case 0: bad.surface_marker->version = 2; break;
      case 1: m.system = {}; break;
      case 2: m.planet = {}; break;
      case 3: m.tick = 8; break;
      case 4: m.position.x = std::numeric_limits<double>::quiet_NaN(); break;
      case 5: m.position.y = std::numeric_limits<double>::infinity(); break;
      case 6: m.position = {}; break;
      case 7: m.position = {1e8 + 1, 0, 0}; break;
      case 8: m.position = {1, 0, 0}; break;
      case 9:
        m.planet = need(generate_physical_origin_system(Seed{43}, 2))
                       .catalog.planets.front()
                       .descriptor.id;
        break;
      default: break;
    }
    check(!validate_freedom_knowledge(bad, 7) &&
              !encode_freedom_knowledge_json(bad, 7),
          "Invalid marker version, owner, tick or finite spatial envelope "
          "refuses before encoding");
  }
  const auto encoded = need(encode_freedom_knowledge_json(marked, 7));
  for (unsigned fault = 0; fault < 13; ++fault) {
    auto root = Json::parse(encoded);
    auto& marker = root["surface_marker"];
    switch (fault) {
      case 0: marker = nullptr; break;
      case 1: marker["version"] = 2U; break;
      case 2: marker["version"] = 1.0; break;
      case 3: marker["extra"] = 0; break;
      case 4: marker.erase("point"); break;
      case 5: marker["point"]["system"] = -1; break;
      case 6: marker["point"]["planet"] = 0U; break;
      case 7: marker["point"]["tick"] = 8U; break;
      case 8: marker["point"]["body_fixed_metres"] = Json::array({0, 0}); break;
      case 9: marker["point"]["body_fixed_metres"][0] = "0"; break;
      case 10: marker["point"]["body_fixed_metres"][0] = 1e100; break;
      case 11: marker["point"]["extra"] = 0; break;
      case 12: root["recipe"]["version"] = 1U; break;
      default: break;
    }
    check(!decode_freedom_knowledge_json(root.dump(), 7),
          "Marker decoder enforces the complete closed selected schema");
  }
  auto duplicate = encoded;
  const auto at = duplicate.find("\"surface_marker\": {");
  duplicate.insert(at, "\"surface_marker\": null, ");
  check(!decode_freedom_knowledge_json(duplicate, 7) &&
            !decode_freedom_knowledge_json(
                encoded + std::string(kMaximumFreedomKnowledgeBytes, ' '), 7),
        "Marker metadata cannot bypass duplicate-key or bounded input limits");
}

auto moon_surface_marker_metadata() -> void {
  const Seed seed{42};
  const auto system = need(generate_physical_origin_system(seed, 2));
  const auto catalog = need(generate_secondary_body_catalog(system));
  const auto moon = need(generate_physical_moon_descriptor(
      catalog, catalog.bodies.front().node.id));
  const auto foreign_system =
      need(generate_physical_origin_system(Seed{43}, 2));
  const auto foreign_catalog =
      need(generate_secondary_body_catalog(foreign_system));
  const FreedomMoonSurfaceMarkerPoint point{
      system.catalog.id,
      moon.body,
      {static_cast<double>(moon.radius_metres) - 123.5, -0.0, 0},
      7};
  check(point.body.kind == BodyKind::moon &&
            std::hypot(point.position.x, point.position.y, point.position.z) <
                moon.radius_metres,
        "Moon marker fixture uses genuine typed owner and below-datum dry "
        "navigation metadata, not a renewed terrain certificate");
  for (const auto version :
       {kFreedomObservedKnowledgeVersion, kFreedomTravelKnowledgeVersion}) {
    auto selected = need(make_freedom_starting_knowledge(seed, version));
    const auto baseline = need(encode_freedom_knowledge_json(selected, 7));
    selected.surface_marker =
        FreedomSurfaceMarkerState{kFreedomMoonSurfaceMarkerVersion, point};
    const auto encoded = need(encode_freedom_knowledge_json(selected, 7));
    const auto decoded = need(decode_freedom_knowledge_json(encoded, 7));
    auto root = Json::parse(encoded);
    check(decoded == selected && root["surface_marker"]["version"] == 2U &&
              root["surface_marker"]["point"].size() == 3 &&
              root["surface_marker"]["point"]["target"] ==
                  Json::parse(
                      need(encode_body_target({point.system, point.body}))) &&
              std::signbit(std::get<FreedomMoonSurfaceMarkerPoint>(
                               *decoded.surface_marker->point)
                               .position.y),
          "Nested version2 Moon point roundtrips canonical BodyTarget and "
          "signed coordinates through unchanged outer knowledge recipe");
    root.erase("surface_marker");
    check(root.dump(2) + '\n' == baseline,
          "Recording Moon navigation metadata grants no facts or recipe "
          "changes");
    selected.surface_marker->point.reset();
    const auto cleared = need(encode_freedom_knowledge_json(selected, 7));
    check(Json::parse(cleared)["surface_marker"] ==
                  Json{{"version", 2U}, {"point", nullptr}} &&
              need(decode_freedom_knowledge_json(cleared, 7)) == selected,
          "Clearing Moon marker preserves selected nested version2");
    // Only explicit replacement changes the component's selected body/version.
    const auto& planet = system.catalog.planets.front().descriptor;
    selected.surface_marker = FreedomSurfaceMarkerState{
        kFreedomSurfaceMarkerVersion,
        FreedomSurfaceMarkerPoint{
            system.catalog.id,
            planet.id,
            {static_cast<double>(planet.radius.value) * 1000 + 123.5, 0, 0},
            7}};
    const auto replaced = need(encode_freedom_knowledge_json(selected, 7));
    check(Json::parse(replaced)["surface_marker"]["version"] == 1U &&
              !Json::parse(replaced)["surface_marker"]["point"].contains(
                  "target") &&
              need(decode_freedom_knowledge_json(replaced, 7)) == selected,
          "Explicit replacement back onto planet uses original version1 "
          "point encoding");
  }
  auto selected = need(
      make_freedom_starting_knowledge(seed, kFreedomTravelKnowledgeVersion));
  selected.surface_marker =
      FreedomSurfaceMarkerState{kFreedomMoonSurfaceMarkerVersion, point};
  const auto encoded = need(encode_freedom_knowledge_json(selected, 7));
  for (unsigned fault = 0; fault < 17; ++fault) {
    auto bad = selected;
    auto& m =
        std::get<FreedomMoonSurfaceMarkerPoint>(*bad.surface_marker->point);
    switch (fault) {
      case 0: bad.surface_marker->version = 1; break;
      case 1: bad.surface_marker->version = 3; break;
      case 2: m.system = {}; break;
      case 3: m.system = foreign_system.catalog.id; break;
      case 4: m.body.value = 0; break;
      case 5: m.body.version = 2; break;
      case 6: m.body.kind = BodyKind::planet; break;
      case 7: m.body = catalog.bodies.back().node.id; break;
      case 8: m.body = foreign_catalog.bodies.front().node.id; break;
      case 9: m.tick = 8; break;
      case 10: m.position.x = std::numeric_limits<double>::quiet_NaN(); break;
      case 11: m.position.y = std::numeric_limits<double>::infinity(); break;
      case 12: m.position = {}; break;
      case 13: m.position = {1, 0, 0}; break;
      case 14: m.position = {1e8 + 1, 0, 0}; break;
      case 15: bad.surface_marker->point = FreedomSurfaceMarkerPoint{}; break;
      case 16: bad.recipe.version = kFreedomKnowledgeVersion; break;
      default: break;
    }
    check(!validate_freedom_knowledge(bad, 7) &&
              !encode_freedom_knowledge_json(bad, 7),
          "Moon marker refuses mismatched component/variant, owner/kind, "
          "tick and finite radial envelope before encoding");
  }
  // A genuine neighbor Moon is still outside the currently selected origin-
  // Moon physical domain; matching an arbitrary catalog label is insufficient.
  const auto ids = generate_first_intersystem_identities(seed);
  const auto neighbor =
      need(generate_physical_local_system(ids.target_system_seed, 2));
  const auto neighbor_catalog = need(generate_secondary_body_catalog(neighbor));
  auto wrong_domain = selected;
  auto& neighbor_point = std::get<FreedomMoonSurfaceMarkerPoint>(
      *wrong_domain.surface_marker->point);
  neighbor_point.system = neighbor.catalog.id;
  neighbor_point.body = neighbor_catalog.bodies.front().node.id;
  check(
      !validate_freedom_knowledge(wrong_domain, 7),
      "Current Moon marker cannot silently enroll a neighboring physical Moon");
  for (const double radius : {std::nextafter(1.0, 2.0), 1e8}) {
    auto boundary = selected;
    std::get<FreedomMoonSurfaceMarkerPoint>(*boundary.surface_marker->point)
        .position = {radius, 0, 0};
    check(validate_freedom_knowledge(boundary, 7).has_value(),
          "Moon navigation coordinate envelope keeps exact supported walker "
          "radial bounds without claiming support");
  }
  for (unsigned fault = 0; fault < 22; ++fault) {
    auto root = Json::parse(encoded);
    auto& marker = root["surface_marker"];
    auto& m = marker["point"];
    switch (fault) {
      case 0: marker["version"] = 3U; break;
      case 1: marker["version"] = 2.0; break;
      case 2: marker["version"] = 1U; break;
      case 3: marker["extra"] = 0; break;
      case 4: marker.erase("point"); break;
      case 5: m["planet"] = 1U; break;
      case 6: m.erase("target"); break;
      case 7: m["target"] = nullptr; break;
      case 8: m["target"]["version"] = 2U; break;
      case 9: m["target"]["version"] = 1.0; break;
      case 10: m["target"]["kind"] = "planet"; break;
      case 11: m["target"]["kind"] = "minor"; break;
      case 12: m["target"]["identity"] = "0"; break;
      case 13:
        m["target"]["identity"] = "0" + std::to_string(moon.body.value);
        break;
      case 14: m["target"]["system"] = point.system.value; break;
      case 15: m["target"]["extra"] = 0; break;
      case 16: m["tick"] = 8U; break;
      case 17: m["tick"] = 7.0; break;
      case 18: m["body_fixed_metres"] = Json::array({0, 0}); break;
      case 19: m["body_fixed_metres"][0] = "0"; break;
      case 20: m["body_fixed_metres"][0] = 1e100; break;
      case 21: root["recipe"]["version"] = 1U; break;
      default: break;
    }
    check(!decode_freedom_knowledge_json(root.dump(), 7),
          "Moon marker decoder enforces exact closed component/target shape, "
          "types and source identity");
  }
  auto duplicate = encoded;
  const auto at = duplicate.find("\"target\": {");
  check(at != std::string::npos, "Canonical Moon marker includes typed target");
  duplicate.insert(at, "\"target\": null, ");
  check(!decode_freedom_knowledge_json(duplicate, 7) &&
            !decode_freedom_knowledge_json(
                encoded + std::string(kMaximumFreedomKnowledgeBytes, ' '), 7) &&
            need(encode_freedom_knowledge_json(selected, 7)) == encoded,
        "Moon metadata retains duplicate/byte limits and leaves accepted "
        "source unchanged after all refusals");
}

auto surface_landmark_metadata() -> void {
  const Seed seed{42};
  const auto system = need(generate_physical_origin_system(seed, 2));
  const auto& body = system.catalog.planets.front().descriptor;
  std::vector<FreedomSurfaceLandmarkInspection> records;
  const auto radius = static_cast<double>(body.radius.value) * 1000;
  // Identity-only shell samples authenticate bounded codec fixtures without
  // creating contact geometry or pretending they were player inspections.
  for (unsigned sample = 0; sample < 512 && records.size() < 65; ++sample) {
    const auto angle = 2 * std::numbers::pi * sample / 512;
    const SurfaceLandmarkCell base{
        static_cast<std::int32_t>(
            std::floor(radius * std::cos(angle) / kSurfaceLandmarkCellMetres)),
        static_cast<std::int32_t>(
            std::floor(radius * std::sin(angle) / kSurfaceLandmarkCellMetres)),
        0};
    for (std::int32_t x = -1; x <= 1; ++x)
      for (std::int32_t y = -1; y <= 1; ++y)
        for (std::int32_t z = -1; z <= 1; ++z) {
          const SurfaceLandmarkCell cell{base.x + x, base.y + y, base.z + z};
          const auto identity = need(identify_surface_landmark(body, cell));
          if (!identity || records.size() == 65 ||
              std::ranges::any_of(records, [&](const auto& record) {
                return record.site == identity->id;
              }))
            continue;
          records.push_back(
              {system.catalog.id, body.id, cell, identity->id, 3, 7});
        }
  }
  if (records.size() != 65)
    throw std::runtime_error("Landmark identity fixtures unavailable");
  std::ranges::sort(records, {},
                    [](const auto& record) { return record.site.value; });
  for (auto version :
       {kFreedomKnowledgeVersion, kFreedomObservedKnowledgeVersion,
        kFreedomTravelKnowledgeVersion}) {
    auto knowledge = need(make_freedom_starting_knowledge(seed, version));
    const auto historical = need(encode_freedom_knowledge_json(knowledge, 7));
    check(!Json::parse(historical).contains("surface_landmarks") &&
              need(encode_freedom_knowledge_json(
                  need(decode_freedom_knowledge_json(historical, 7)), 7)) ==
                  historical,
          "Unselected landmarks preserve each historical recipe encoding");
    knowledge.surface_landmarks = FreedomSurfaceLandmarkState{};
    if (version == kFreedomKnowledgeVersion) {
      auto malformed = Json::parse(historical);
      malformed["surface_landmarks"] = {{"version", 1U},
                                        {"inspections", Json::array()}};
      check(!validate_freedom_knowledge(knowledge, 7) &&
                !encode_freedom_knowledge_json(knowledge, 7) &&
                !decode_freedom_knowledge_json(malformed.dump(), 7),
            "Chart-only historical recipes cannot select landmarks");
      continue;
    }
    const auto empty = need(encode_freedom_knowledge_json(knowledge, 7));
    check(need(decode_freedom_knowledge_json(empty, 7)) == knowledge &&
              Json::parse(empty)["surface_landmarks"]["inspections"].empty(),
          "Explicit empty landmark selection retains its version");
    knowledge.surface_landmarks->inspections = {records[0], records[1]};
    knowledge.surface_marker = FreedomSurfaceMarkerState{};
    const auto facts = knowledge.entries;
    const auto recipe = knowledge.recipe;
    const auto selected = need(encode_freedom_knowledge_json(knowledge, 7));
    check(need(decode_freedom_knowledge_json(selected, 7)) == knowledge &&
              knowledge.entries == facts && knowledge.recipe == recipe,
          "Landmarks and marker coexist without adding sensor facts or "
          "changing recipe domains");
    if (version == kFreedomTravelKnowledgeVersion) {
      knowledge.recipe.craft_lineage = FreedomCraftLineage{1, 1};
      check(need(decode_freedom_knowledge_json(
                need(encode_freedom_knowledge_json(knowledge, 7)), 7)) ==
                knowledge,
            "Inspection metadata is independent of recovered craft identity");
    }
  }
  auto selected = need(
      make_freedom_starting_knowledge(seed, kFreedomObservedKnowledgeVersion));
  selected.surface_landmarks = FreedomSurfaceLandmarkState{1, {records[0]}};
  for (std::uint32_t policy = 1; policy <= 3; ++policy) {
    selected.surface_landmarks->inspections.front().terrain_policy = policy;
    check(validate_freedom_knowledge(selected, 7).has_value(),
          "Inspections retain their explicit supported contact policy");
  }
  auto full = selected;
  full.surface_landmarks->inspections.assign(records.begin(),
                                             records.end() - 1);
  check(need(decode_freedom_knowledge_json(
            need(encode_freedom_knowledge_json(full, 7)), 7)) == full,
        "Exactly64 authenticated sorted inspections fit the metadata budget");
  for (unsigned fault = 0; fault < 11; ++fault) {
    auto bad = selected;
    auto& state = bad.surface_landmarks.value();
    auto& record = state.inspections.front();
    switch (fault) {
      case 0: state.version = 2; break;
      case 1: record.system = {}; break;
      case 2: record.planet = {}; break;
      case 3: record.site.value ^= 1; break;
      case 4: record.cell = {}; break;
      case 5: record.cell.x = 32768; break;
      case 6: record.terrain_policy = 0; break;
      case 7: record.terrain_policy = 4; break;
      case 8: record.tick = 8; break;
      case 9: state.inspections.push_back(record); break;
      case 10: state.inspections = records; break;
      default: break;
    }
    check(!validate_freedom_knowledge(bad, 7) &&
              !encode_freedom_knowledge_json(bad, 7),
          "Invalid landmark selectors, identities, bounds and provenance "
          "refuse before encoding");
  }
  auto reversed = selected;
  reversed.surface_landmarks->inspections = {records[1], records[0]};
  check(!validate_freedom_knowledge(reversed, 7),
        "Reordered authenticated inspections cannot silently recanonicalize");
  const auto encoded = need(encode_freedom_knowledge_json(selected, 7));
  for (unsigned fault = 0; fault < 26; ++fault) {
    auto root = Json::parse(encoded);
    auto& state = root["surface_landmarks"];
    auto& record = state["inspections"][0];
    switch (fault) {
      case 0: state = nullptr; break;
      case 1: state["version"] = 2U; break;
      case 2: state["version"] = 1.0; break;
      case 3: state["extra"] = 0; break;
      case 4: state.erase("inspections"); break;
      case 5: state["inspections"] = nullptr; break;
      case 6: record = nullptr; break;
      case 7: record["extra"] = 0; break;
      case 8: record.erase("site"); break;
      case 9: record["system"] = -1; break;
      case 10: record["planet"] = 0U; break;
      case 11: record["site"] = "1"; break;
      case 12: record["site"] = 1.0; break;
      case 13: record["tick"] = 8U; break;
      case 14: record["terrain_policy"] = 0U; break;
      case 15: record["terrain_policy"] = UINT64_MAX; break;
      case 16: record["cell"] = Json::array({0, 0}); break;
      case 17: record["cell"][0] = 1.0; break;
      case 18: record["cell"][0] = "0"; break;
      case 19: record["cell"][0] = UINT64_MAX; break;
      case 20: record["cell"][0] = INT64_MIN; break;
      case 21: record["cell"][0] = 32768; break;
      case 22: record["cell"][0] = nullptr; break;
      case 23: state["inspections"].push_back(record); break;
      case 24:
        while (state["inspections"].size() < 65)
          state["inspections"].push_back(state["inspections"][0]);
        break;
      case 25: root["extra"] = 0; break;
      default: break;
    }
    check(!decode_freedom_knowledge_json(root.dump(), 7),
          "Landmark decoder refuses malformed closed records and numbers");
  }
  auto duplicate = encoded;
  const auto at = duplicate.find("\"surface_landmarks\": {");
  duplicate.insert(at, "\"surface_landmarks\": null, ");
  auto nested_duplicate = encoded;
  nested_duplicate.insert(nested_duplicate.find("\"site\":"), "\"site\": 0, ");
  check(!decode_freedom_knowledge_json(duplicate, 7) &&
            !decode_freedom_knowledge_json(nested_duplicate, 7) &&
            !decode_freedom_knowledge_json(
                encoded + std::string(kMaximumFreedomKnowledgeBytes, ' '), 7),
        "Landmark metadata preserves duplicate-key and complete input bounds");
}

auto native_saves(const std::filesystem::path& dir) -> void {
  const Seed seed{42};
  auto selected = need(native_new_game(seed));
  const auto starting =
      std::get<FreedomKnowledgeSaveDocument>(selected.document);
  auto session = need(NativeFreedomFlightSession::open(selected));
  check(session.knowledge() == std::optional{starting.knowledge} &&
            session.document().origin.state.discoveries.empty() &&
            session.document().origin.state.world_deltas.empty(),
        "Ordinary New Game selects baseline without synthetic career history");
  for (unsigned tick = 0; tick < 3; ++tick)
    need(session.advance_walk({}));
  const auto route = generate_first_universe_route(seed);
  const KnowledgeSubject home{KnowledgeSubjectKind::planet, route.origin,
                              selected.home_planet.id.value};
  const auto path = dir / "knowledge.json";
  for (unsigned level = 0; level < 3; ++level) {
    const auto e =
        event(seed, home, KnowledgeFact::physical,
              static_cast<NavigationKnowledgeLevel>(level), level + 1);
    check(session.record_observation(e).has_value(),
          "Native owner records bounded domain evidence");
    check(session.save_as(path).has_value(),
          "Actual owner Save As persists knowledge");
    const auto source = bytes(path);
    auto resumed =
        need(NativeFreedomFlightSession::open(need(native_continue(path))));
    check(
        resumed.knowledge() == session.knowledge() &&
            resumed.document() == session.document() &&
            resumed.resources() == session.resources() && bytes(path) == source,
        "Native before/after transition Continue retains all existing owners");
  }
  const auto source = bytes(path);
  const auto before = *session.knowledge();
  auto future = event(seed, home, KnowledgeFact::atmosphere,
                      NavigationKnowledgeLevel::resolved, 4);
  check(!session.record_observation(future) &&
            session.knowledge() == std::optional{before},
        "Future observation does not partially replace native knowledge");
  auto forged = starting;
  ++forged.knowledge.recipe.universe_seed.value;
  check(!write_freedom_knowledge_file_atomically(path, forged) &&
            bytes(path) == source,
        "Refused knowledge save preserves destination bytes");
  const NativeStartup forged_home{selected.mode, selected.document,
                                  generate_planet_descriptor({99}),
                                  selected.source_save};
  check(!native_save_freedom(forged_home, path) && bytes(path) == source,
        "Startup Save As validates selected physical home before writing");
  const auto old = dir / "old24.json";
  check(
      write_freedom_resource_file_atomically(old, starting.voyage).has_value(),
      "Retained resource-only document encodes independently");
  const auto old_bytes = bytes(old);
  auto legacy =
      need(NativeFreedomFlightSession::open(need(native_continue(old))));
  const auto old_copy = dir / "old24-copy.json";
  check(!legacy.knowledge() && legacy.save_as(old_copy).has_value() &&
            bytes(old_copy) == old_bytes && bytes(old) == old_bytes,
        "Old24 stays knowledge-unselected with byte-identical Save As");
  const auto snapshot = session.document();
  const auto fuel = *session.resources();
  const auto ktext =
      need(encode_freedom_knowledge_json(*session.knowledge(), fuel.tick));
  const auto chart = need(resolve_freedom_knowledge_chart(*session.knowledge(),
                                                          route.origin, fuel));
  UniverseNavigationSelectionState selection{1, {}};
  need(advance_universe_navigation_selection(
      chart, selection, UniverseNavigationSelectionCommand::select));
  check(session.document() == snapshot &&
            session.resources() == std::optional{fuel} &&
            need(encode_freedom_knowledge_json(*session.knowledge(),
                                               fuel.tick)) == ktext,
        "Chart projection and selection award no evidence, fuel or time");
}
} // namespace

auto main() -> int {
  const auto dir =
      std::filesystem::temp_directory_path() /
      ("apsis-knowledge-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(dir);
  try {
    for (auto seed : {Seed{0}, Seed{42}, Seed{UINT64_MAX}})
      fixture(seed);
    invalid_inputs();
    surface_marker_metadata();
    moon_surface_marker_metadata();
    surface_landmark_metadata();
    native_saves(dir);
    check(fingerprint == 2913154867708375063ULL,
          "Version1 ordered ledger and observation sequence retain their "
          "golden checksum");
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << e.what() << '\n';
  }
  std::filesystem::remove_all(dir);
  std::cout << checks << " knowledge checks, " << failures
            << " failures; fingerprint " << fingerprint << '\n';
  return failures == 0 ? 0 : 1;
}
