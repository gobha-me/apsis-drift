#include "apsis_drift/native_flight_session.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
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
