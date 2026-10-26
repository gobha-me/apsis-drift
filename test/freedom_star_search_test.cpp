#include "apsis_drift/freedom_knowledge_save.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
using namespace apsis_drift;
int checks{}, failures{};
auto check(bool value, std::string_view message) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <typename T, typename E> auto need(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Star-search fixture refused");
  if constexpr (std::is_void_v<T>)
    return;
  else
    return std::move(*value);
}
constexpr Seed kSeed{42};
auto subject(SystemId id) -> KnowledgeSubject {
  return {KnowledgeSubjectKind::system, id, id.value};
}
auto roundtrip(const FreedomKnowledge& k, SimulationTick tick)
    -> FreedomKnowledge {
  const auto text = need(encode_freedom_knowledge_json(k, tick));
  auto restored = need(decode_freedom_knowledge_json(text, tick));
  check(restored == k &&
            need(encode_freedom_knowledge_json(restored, tick)) == text,
        "Selected acquisition and typed provenance roundtrip exact canonical "
        "bytes");
  return restored;
}
auto chart(const FreedomKnowledge& k, SimulationTick tick)
    -> UniverseNavigationView {
  // Labelled metadata owner for this pure chart test, not a flown departure.
  // Selected network keeps the original authenticated Home pose/catalog.
  auto owner = need(make_freedom_network_new_game_document(kSeed));
  owner.network->version = freedom_knowledge_network_version(k.recipe.version);
  owner.state.tick = tick;
  const FreedomResources resources{
      1, owner.state.craft, {kWayfarerFrameId, kWayfarerFrameVersion}, tick};
  need(validate_freedom_save_document(owner));
  return need(resolve_freedom_knowledge_chart(
      k, generate_first_intersystem_identities(kSeed).origin_system, resources,
      true, &owner));
}
auto chart_contains(const UniverseNavigationView& view, SystemId id) -> bool {
  return std::ranges::any_of(view.destinations,
                             [&](const auto& row) { return row.system == id; });
}
auto initial_and_legacy() -> void {
  const auto craft = need(derive_freedom_craft_identity(kSeed, 0));
  const auto network = need(generate_freedom_system_network(kSeed, 2));
  for (std::uint32_t version = 1; version <= 5; ++version) {
    const auto old = need(make_freedom_starting_knowledge(kSeed, version));
    const auto text = need(encode_freedom_knowledge_json(old, 0));
    check(!old.star_search &&
              text.find("\"star_search\"") == std::string::npos &&
              !begin_freedom_star_search(old, network.nodes[0].system, craft,
                                         0) &&
              need(decode_freedom_knowledge_json(text, 0)) == old &&
              need(encode_freedom_knowledge_json(old, 0)) == text,
          "Historical starting recipes never select or silently adopt star "
          "search");
  }
  const auto initial = need(make_freedom_starting_knowledge(kSeed, 6));
  check(initial.recipe.version == 6 && initial.recipe.chart == 4 &&
            initial.star_search && initial.star_search->version == 1 &&
            !initial.star_search->active &&
            initial.star_search->completed.empty(),
        "New recipe explicitly owns an empty selected acquisition ledger");
  const auto listed = need(known_freedom_subjects(initial, 0));
  std::size_t systems{};
  for (const auto& item : listed) {
    if (item.kind == KnowledgeSubjectKind::system) ++systems;
    check(
        item.system != network.nodes[4].system &&
            item.system != network.nodes[5].system,
        "Starting rows reveal neither hidden system nor any hidden local body");
  }
  const auto view = chart(initial, 0);
  check(systems == 4 && view.destinations.size() == 4,
        "New starting chart grants exactly the original four system anchors");
  for (std::size_t i = 0; i < network.nodes.size(); ++i) {
    const auto id = network.nodes[i].system;
    check(chart_contains(view, id) == (i < 4),
          "Destination admission follows known anchors rather than generated "
          "roster");
    if (i < 4) continue;
    const auto physical = need(resolve_freedom_network_system(network, id));
    for (const auto& planet : physical.catalog.planets) {
      const KnowledgeSubject body{KnowledgeSubjectKind::planet, id,
                                  planet.descriptor.id.value};
      for (const auto fact :
           {KnowledgeFact::identity_type, KnowledgeFact::location,
            KnowledgeFact::physical, KnowledgeFact::presence})
        check(!need(query_freedom_knowledge(initial, body, fact, 0)),
              "Undiscovered-system planets remain absent, not a hidden catalog "
              "dump");
    }
    // An independent copy detects mutation by refused operations.
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Snapshot.
    const auto frozen = initial;
    for (const auto fact :
         {KnowledgeFact::identity_type, KnowledgeFact::location}) {
      check(!apply_freedom_knowledge(
                initial,
                {subject(id),
                 fact,
                 {NavigationKnowledgeLevel::resolved,
                  KnowledgeSource::local_ship, craft.value, 10, craft}},
                10) &&
                !apply_freedom_knowledge(initial,
                                         {subject(id),
                                          fact,
                                          {NavigationKnowledgeLevel::resolved,
                                           KnowledgeSource::starting_chart,
                                           4,
                                           0,
                                           {}}},
                                         0) &&
                initial == frozen,
            "Guessing a genuine hidden ID cannot fabricate acquisition or "
            "baseline grants");
    }
  }
  roundtrip(initial, 0);
  auto missing = initial;
  missing.star_search.reset();
  auto old = need(make_freedom_starting_knowledge(kSeed, 5));
  old.star_search = initial.star_search;
  check(!validate_freedom_knowledge(missing, 0) &&
            !encode_freedom_knowledge_json(missing, 0) &&
            !validate_freedom_knowledge(old, 0) &&
            !encode_freedom_knowledge_json(old, 0),
        "Selected search cannot disappear or be relabeled as historical "
        "knowledge");
}
auto search_sequence() -> void {
  const auto network = need(generate_freedom_system_network(kSeed, 2));
  const auto source = network.nodes[0].system;
  const auto craft = need(derive_freedom_craft_identity(kSeed, 0));
  const auto initial = need(make_freedom_starting_knowledge(kSeed, 6));
  auto active = need(begin_freedom_star_search(initial, source, craft, 0));
  const auto first = active.star_search->active->target;
  const auto a = need(resolve_freedom_system_network_route(
      network, source, network.nodes[4].system));
  const auto b = need(resolve_freedom_system_network_route(
      network, source, network.nodes[5].system));
  const auto expected =
      std::pair{a.squared_distance_light_seconds, a.destination.value} <
              std::pair{b.squared_distance_light_seconds, b.destination.value}
          ? a.destination
          : b.destination;
  check(first == expected && active.entries == initial.entries &&
            active.star_search->active->acquired_ticks == 0,
        "Begin chooses nearest with ID tie-break but grants no target facts");
  const auto frozen = active;
  check(!begin_freedom_star_search(active, source, craft, 0) &&
            !begin_freedom_star_search(initial, source, StarterCraftId{0}, 0) &&
            !advance_freedom_star_search(active, network.nodes[1].system, craft,
                                         1, true) &&
            !advance_freedom_star_search(active, source, StarterCraftId{0}, 1,
                                         true) &&
            !advance_freedom_star_search(
                active, source, craft,
                std::numeric_limits<SimulationTick>::max(), true) &&
            active == frozen,
        "Active/source/craft/overflow refusals are atomic");
  for (unsigned damage = 0; damage < 7; ++damage) {
    auto bad = active;
    auto& acquisition = *bad.star_search->active;
    switch (damage) {
      case 0: bad.star_search->version = 2; break;
      case 1: acquisition.source = SystemId{0}; break;
      case 2: acquisition.target = source; break;
      case 3: acquisition.craft = StarterCraftId{0}; break;
      case 4: acquisition.started_tick = 1; break;
      case 5: acquisition.last_tick = 2; break;
      case 6: acquisition.acquired_ticks = kFreedomStarSearchTicks; break;
    }
    const auto original = bad;
    check(!validate_freedom_knowledge(bad, 0) &&
              !advance_freedom_star_search(bad, source, craft, 1, true) &&
              !encode_freedom_knowledge_json(bad, 0) && bad == original,
          "Malformed active version/owner/target/clock/counter refuses "
          "atomically");
  }
  for (const auto id : {SystemId{0}, network.nodes[4].system})
    check(!begin_freedom_star_search(initial, id, craft, 0) &&
              initial == need(make_freedom_starting_knowledge(kSeed, 6)),
          "Search cannot start from unknown or undiscovered source identity");
  const auto leap =
      need(advance_freedom_star_search(active, source, craft, 1000, true));
  check(leap.star_search->active->acquired_ticks == 1 &&
            leap.star_search->completed.empty(),
        "Elapsed gaps do not invent skipped eligible acquisition ticks");
  active = need(advance_freedom_star_search(active, source, craft, 1, false));
  check(active.star_search->active->acquired_ticks == 0 &&
            need(advance_freedom_star_search(active, source, craft, 1, true)) ==
                active &&
            need(advance_freedom_star_search(active, source, craft, 1,
                                             false)) == active,
        "Held and repeated same-tick reads never accelerate or requalify "
        "progress");
  check(!advance_freedom_star_search(active, source, craft, 0, true),
        "Regressing authoritative clock refuses without rewriting active "
        "acquisition");
  SimulationTick tick{1};
  for (std::uint32_t i = 1; i <= kFreedomStarSearchTicks; ++i) {
    ++tick;
    active =
        need(advance_freedom_star_search(active, source, craft, tick, true));
    if (i < kFreedomStarSearchTicks)
      check(active.star_search->active &&
                active.star_search->active->acquired_ticks == i &&
                active.star_search->completed.empty() &&
                active.entries == initial.entries,
            "Each actual eligible one-tick advance counts once without early "
            "disclosure");
    if (i == 600) active = roundtrip(active, tick);
  }
  check(
      !active.star_search->active &&
          active.star_search->completed.size() == 1 &&
          active.entries.size() == initial.entries.size() + 2 &&
          active.star_search->completed.front() ==
              FreedomStarSearchRecord{source, first, craft, 0, tick},
      "Exactly1200 eligible ticks grant one system identity/location receipt");
  const auto first_complete = active;
  for (const auto fact :
       {KnowledgeFact::identity_type, KnowledgeFact::location}) {
    const auto reading =
        need(query_freedom_knowledge(active, subject(first), fact, tick));
    check(reading && reading->provenance ==
                         KnowledgeTransition{NavigationKnowledgeLevel::resolved,
                                             KnowledgeSource::local_ship,
                                             craft.value, tick, craft},
          "Completion records actual craft/tick sensor provenance rather than "
          "chart grant");
  }
  check(!need(query_freedom_knowledge(active, subject(first),
                                      KnowledgeFact::presence, tick)) &&
            chart(active, tick).destinations.size() == 5,
        "Discovery unlocks one destination without claiming a physical visit");
  roundtrip(active, tick);
  for (unsigned damage = 0; damage < 6; ++damage) {
    auto bad = active;
    auto& record = bad.star_search->completed.front();
    switch (damage) {
      case 0: record.source = SystemId{0}; break;
      case 1: record.target = source; break;
      case 2: record.craft = StarterCraftId{0}; break;
      case 3: ++record.completed_tick; break;
      case 4: record.started_tick = record.completed_tick; break;
      case 5: bad.star_search->completed.push_back(record); break;
    }
    const auto original = bad;
    check(!validate_freedom_knowledge(bad, tick) &&
              !begin_freedom_star_search(bad, source, craft, tick) &&
              !encode_freedom_knowledge_json(bad, tick) && bad == original,
          "Forged completed receipt cannot authenticate facts or start another "
          "search");
  }
  active = need(begin_freedom_star_search(active, source, craft, tick));
  const auto second = active.star_search->active->target;
  check(second != first && active.star_search->completed ==
                               first_complete.star_search->completed,
        "Second acquisition chooses only the remaining undiscovered system");
  for (std::uint32_t i = 0; i < kFreedomStarSearchTicks; ++i)
    active =
        need(advance_freedom_star_search(active, source, craft, ++tick, true));
  check(!active.star_search->active &&
            active.star_search->completed.size() == 2 &&
            active.star_search->completed.front() ==
                first_complete.star_search->completed.front() &&
            chart(active, tick).destinations.size() == 6 &&
            !begin_freedom_star_search(active, source, craft, tick) &&
            !advance_freedom_star_search(active, source, craft, tick + 1, true),
        "Two finite discoveries exhaust search while preserving the first "
        "receipt");
  roundtrip(active, tick);
  for (const auto id : {first, second}) {
    const auto physical = need(resolve_freedom_network_system(network, id));
    const KnowledgeSubject planet{
        KnowledgeSubjectKind::planet, id,
        physical.catalog.planets.front().descriptor.id.value};
    check(!need(query_freedom_knowledge(active, planet,
                                        KnowledgeFact::identity_type, tick)) &&
              !need(query_freedom_knowledge(active, planet,
                                            KnowledgeFact::location, tick)) &&
              !need(query_freedom_knowledge(active, subject(id),
                                            KnowledgeFact::presence, tick)),
          "Completed system search still grants no planet roster, properties "
          "or visit");
  }
}
auto outer_search() -> void {
  const auto network = need(generate_freedom_system_network(
      kSeed, kFreedomOuterSystemNetworkVersion));
  const auto craft = need(derive_freedom_craft_identity(kSeed, 0));
  const auto initial = need(
      make_freedom_starting_knowledge(kSeed, kFreedomOuterKnowledgeVersion));
  const auto legacy = need(make_freedom_starting_knowledge(
      kSeed, kFreedomUnchartedKnowledgeVersion));
  check(
      initial.recipe.chart == 5 && initial.star_search &&
          initial.star_search->version == 2 &&
          initial.star_search->completed.empty() &&
          chart(initial, 0).destinations.size() == 4,
      "Outer profile selects chart5/search2 while granting only four anchors");
  for (const auto& entry : initial.entries)
    for (std::size_t i = 4; i < network.nodes.size(); ++i)
      check(
          entry.subject.system != network.nodes[i].system,
          "Outer starting chart contains no hidden system or planetary facts");
  const auto frozen = need(encode_freedom_knowledge_json(initial, 0));
  for (unsigned read = 0; read < 3; ++read) {
    check(!need(freedom_star_search_signal_available(
              initial, network.nodes[0].system, 0)) &&
              !need(freedom_star_search_signal_available(
                  initial, network.nodes[1].system, 0)) &&
              need(freedom_star_search_signal_available(
                  initial, network.nodes[2].system, 0)) &&
              need(freedom_star_search_signal_available(
                  initial, network.nodes[3].system, 0)) &&
              need(encode_freedom_knowledge_json(initial, 0)) == frozen,
          "Only waypoint anchors have an in-reach unknown signal; repeated "
          "preview grants no facts and does not mutate canonical Save bytes");
  }
  check(need(freedom_star_search_signal_available(
            legacy, network.nodes[0].system, 0)) &&
            !begin_freedom_star_search(initial, network.nodes[0].system, craft,
                                       0),
        "The six-node profile keeps its global search; outer Home has no "
        "in-reach sensor fix and cannot begin a fabricated acquisition");
  // Hearsay is explicitly unbounded and remains no navigation evidence. It is
  // distinct from sensor reach, even though both queries share Home's owner.
  const auto rumor =
      need(offer_freedom_station_stellar_lead(initial, craft, 0));
  const auto old_rumor =
      need(offer_freedom_station_stellar_lead(legacy, craft, 0));
  auto noted = need(receive_freedom_station_stellar_lead(initial, rumor, 0));
  check(rumor.target == old_rumor.target && noted.entries == initial.entries &&
            need(freedom_station_stellar_lead_status(noted, 0)) ==
                FreedomStationStellarLeadStatus::unverified &&
            !need(freedom_star_search_signal_available(
                noted, network.nodes[0].system, 0)),
        "Retaining an out-of-reach attendant report neither grants nor "
        "extends a sensor fix");
  noted = roundtrip(noted, 0);
  const auto waypoint = network.nodes[2].system;
  auto live = need(begin_freedom_star_search(noted, waypoint, craft, 10));
  const auto first = live.star_search->active->target;
  const auto first_node =
      need(find_freedom_system_network_node(network, first));
  const auto route_a = need(resolve_freedom_system_network_route(
      network, waypoint, network.nodes[4].system));
  const auto route_b = need(resolve_freedom_system_network_route(
      network, waypoint, network.nodes[5].system));
  const auto expected_first =
      std::pair{route_a.squared_distance_light_seconds,
                route_a.destination.value} <
              std::pair{route_b.squared_distance_light_seconds,
                        route_b.destination.value}
          ? route_a.destination
          : route_b.destination;
  check(first == expected_first && first_node.ordinal >= 4 &&
            first_node.ordinal < 6 && live.entries == noted.entries,
        "Ranged acquisition filters outer nodes before nearest-distance/ID "
        "selection and grants no early target facts");
  for (unsigned fault = 0; fault < 4; ++fault) {
    auto bad = live;
    auto& acquisition = *bad.star_search->active;
    if (fault == 0) acquisition.source = network.nodes[0].system;
    if (fault == 1) acquisition.target = network.nodes[6].system;
    // Remain future at both the read tick10 and attempted advance tick11.
    if (fault == 2) acquisition.last_tick = 12;
    if (fault == 3) acquisition.acquired_ticks = kFreedomStarSearchTicks;
    check(!validate_freedom_knowledge(bad, 10) &&
              !freedom_star_search_signal_available(bad, waypoint, 10) &&
              !advance_freedom_star_search(bad, waypoint, craft, 11, true),
          "Active ranged target/source, future clock and counter corruption "
          "cannot become a different acquisition");
  }
  SimulationTick tick{10};
  live =
      need(advance_freedom_star_search(live, waypoint, craft, ++tick, false));
  check(live.star_search->active->acquired_ticks == 0 &&
            live.star_search->active->target == first &&
            need(advance_freedom_star_search(live, waypoint, craft, tick,
                                             true)) == live,
        "Held and duplicate ticks retain one immutable ranged target");
  // Pure acquisition-source fixtures: these known-source changes are not a
  // flown waypoint/outer voyage. Native travel owns that later proof.
  for (std::uint32_t i = 0; i < kFreedomStarSearchTicks; ++i) {
    live =
        need(advance_freedom_star_search(live, waypoint, craft, ++tick, true));
    if (i == 599) live = roundtrip(live, tick);
  }
  const auto first_complete = live;
  const auto other_hidden = first == network.nodes[4].system
                                ? network.nodes[5].system
                                : network.nodes[4].system;
  check(!need(freedom_star_search_signal_available(
            live, network.nodes[0].system, tick)) &&
            !need(freedom_star_search_signal_available(live, other_hidden,
                                                       tick)) &&
            need(freedom_star_search_signal_available(live, first, tick)) &&
            !begin_freedom_star_search(live, other_hidden, craft, tick),
        "Completed discovery admits its source for onward search, not the "
        "remaining unknown source or an out-of-reach Home fix");
  live = need(begin_freedom_star_search(live, first, craft, tick));
  const auto outer = live.star_search->active->target;
  const auto expected_outer =
      network.nodes[first_node.ordinal == 4 ? 6 : 7].system;
  check(outer == expected_outer &&
            live.star_search->completed ==
                first_complete.star_search->completed &&
            !need(query_freedom_knowledge(live, subject(outer),
                                          KnowledgeFact::identity_type, tick)),
        "An acquired frontier source chooses its reachable outer contact "
        "without a premature name or location");
  for (std::uint32_t i = 0; i < kFreedomStarSearchTicks; ++i)
    live = need(advance_freedom_star_search(live, first, craft, ++tick, true));
  live = roundtrip(live, tick);
  const auto identity = need(query_freedom_knowledge(
      live, subject(outer), KnowledgeFact::identity_type, tick));
  const auto expected_name =
      first_node.ordinal == 4 ? "Outer frontier A" : "Outer frontier B";
  check(identity &&
            std::get<KnownIdentity>(identity->value).name == expected_name &&
            chart(live, tick).destinations.size() == 6 &&
            !need(query_freedom_knowledge(live, subject(outer),
                                          KnowledgeFact::presence, tick)) &&
            !need(freedom_star_search_signal_available(live, outer, tick)),
        "Only completed outer acquisition reveals its bounded authoritative "
        "name; source exhaustion reveals no remote count or invented visit");
  for (unsigned fault = 0; fault < 9; ++fault) {
    auto bad = live;
    switch (fault) {
      case 0: bad.star_search->version = 1; break;
      case 1: bad.recipe.chart = 4; break;
      case 2: bad.recipe.physical_catalog = 1; break;
      case 3:
        bad.star_search->completed[0].source = network.nodes[0].system;
        break;
      case 4:
        bad.star_search->completed[1].source = network.nodes[0].system;
        break;
      case 5: bad.star_search->completed[1].target = other_hidden; break;
      case 6: bad.star_search->completed[1].completed_tick = tick + 1; break;
      case 7:
        bad.star_search->completed.push_back(
            bad.star_search->completed.front());
        break;
      case 8: bad.star_search->completed[1].craft = StarterCraftId{}; break;
      default: break;
    }
    const auto snapshot = bad;
    check(!validate_freedom_knowledge(bad, tick) &&
              !encode_freedom_knowledge_json(bad, tick) &&
              !freedom_star_search_signal_available(bad, waypoint, tick) &&
              !begin_freedom_star_search(bad, waypoint, craft, tick) &&
              bad == snapshot,
          "Version, recipe, ranged history, future, duplicate and craft "
          "corruptions refuse without rewriting the snapshot");
  }
  check(!freedom_star_search_signal_available(initial, SystemId{}, 0) &&
            !freedom_star_search_signal_available(
                initial, waypoint, std::numeric_limits<SimulationTick>::max()),
        "Malformed source or unrepresentable time refuses the lightweight "
        "preview");
  auto missing = initial;
  missing.star_search.reset();
  auto too_many = live;
  while (too_many.star_search->completed.size() <= 4)
    too_many.star_search->completed.push_back(
        live.star_search->completed.front());
  check(!freedom_star_search_signal_available(missing, waypoint, tick) &&
            !freedom_star_search_signal_available(too_many, waypoint, tick) &&
            !validate_freedom_knowledge(too_many, tick),
        "Missing selected state or more than four records fails closed");
}
} // namespace
int main() {
  try {
    initial_and_legacy();
    search_sequence();
    outer_search();
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << error.what() << '\n';
  }
  std::cout << checks << " star-search checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
