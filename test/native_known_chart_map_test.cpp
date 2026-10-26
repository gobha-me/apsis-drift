#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <numbers>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace apsis_drift {
namespace {
unsigned checks{};
auto check(bool good, const char* message) -> void {
  ++checks;
  if (!good) throw std::runtime_error(message);
}
template <class T, class E>
auto need(std::expected<T, E> value,
          std::source_location at = std::source_location::current()) -> T {
  if (!value) {
    auto message = "Known-chart fixture line " + std::to_string(at.line());
    if constexpr (std::is_same_v<E, std::string>)
      message += ": " + value.error();
    else if constexpr (requires { value.error().detail; })
      message += ": " + value.error().detail;
    throw std::runtime_error(message);
  }
  if constexpr (!std::is_void_v<T>) {
    if constexpr (std::is_trivially_copyable_v<T>)
      return *value;
    else
      return std::move(*value);
  }
}
auto bytes(const NativeFreedomFlightSession& session) -> std::string {
  auto saved =
      std::make_unique<NativeSaveDocument>(need(session.save_document()));
  return need(encode_native_save_document_json(*saved));
}
auto continued(const NativeFreedomFlightSession& source)
    -> std::unique_ptr<NativeFreedomFlightSession> {
  const auto saved = bytes(source);
  auto result = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(need(native_select_save_document(
          need(decode_native_save_document_json(saved)))))));
  check(bytes(*result) == saved,
        "Known-chart Continue changed complete Save bytes");
  return result;
}
auto reading(const NativeFreedomFlightSession& session, SystemId system,
             KnowledgeFact fact) -> KnowledgeReading {
  const auto result = need(query_freedom_knowledge(
      *session.knowledge(),
      {KnowledgeSubjectKind::system, system, system.value}, fact,
      session.document().flight.tick));
  check(result &&
            result->provenance.level >= NavigationKnowledgeLevel::resolved,
        "Map oracle attempted to inspect an unresolved endpoint");
  return *result;
}
auto location(const NativeFreedomFlightSession& session, SystemId system)
    -> UniversePositionMetres {
  const auto value = reading(session, system, KnowledgeFact::location);
  check(std::holds_alternative<UniversePositionMetres>(value.value),
        "A local body/orbit reading was treated as a universe anchor");
  return std::get<UniversePositionMetres>(value.value);
}
auto current_selection(const NativeFreedomFlightSession& session)
    -> std::optional<SystemId> {
  if (!session.travel() || session.travel()->local ||
      (session.travel()->composed &&
       session.travel()->composed->active != FreedomComposedRouteKind::system))
    return {};
  return session.travel()->selected;
}
auto view(const NativeFreedomFlightSession& session, std::uint32_t count)
    -> NativeKnownChartMapView {
  const auto before = bytes(session);
  const auto result = need(session.known_chart_map_view());
  check(need(session.known_chart_map_view()) == result &&
            bytes(session) == before,
        "Repeated map reads changed the authoritative complete voyage");
  const auto version =
      session.knowledge()->recipe.version == kFreedomOuterKnowledgeVersion ? 2U
                                                                           : 1U;
  check(result.version == version &&
            result.tick == session.document().flight.tick &&
            !result.note.empty() && result.note.size() <= 1024 &&
            result.note.find('\0') == std::string::npos,
        "Map returned an invalid owner clock or explanation");
  check(result.available && result.node_count == count && count >= 2 &&
            count <= (version == 2 ? kNativeKnownChartMapCapacity
                                   : kFreedomUnchartedSystemNetworkSize),
        "Map count disclosed a hidden slot or omitted an admitted system");
  const auto chart = need(resolve_freedom_knowledge_chart(
      *session.knowledge(), session.system().catalog.id, *session.resources(),
      true, &session.document().origin));
  std::vector<SystemId> admitted;
  for (const auto& row : chart.destinations) {
    bool resolved = true;
    for (const auto fact :
         {KnowledgeFact::identity_type, KnowledgeFact::location}) {
      const auto item = need(query_freedom_knowledge(
          *session.knowledge(),
          {KnowledgeSubjectKind::system, row.system, row.system.value}, fact,
          session.document().flight.tick));
      resolved = resolved && item &&
                 item->provenance.level >= NavigationKnowledgeLevel::resolved;
    }
    if (resolved) admitted.push_back(row.system);
  }
  check(admitted.size() == count, "Independent admitted chart count differs");
  const auto route = generate_first_universe_route(
      session.document().origin.recipe.universe_seed);
  const auto home = location(session, route.origin);
  const auto nearby = location(session, route.destination);
  const std::array<std::int64_t, 3> delta{nearby.x - home.x, nearby.y - home.y,
                                          nearby.z - home.z};
  const auto axis = static_cast<std::size_t>(std::distance(
      delta.begin(),
      std::ranges::find_if(delta, [](auto value) { return value != 0; })));
  check(axis < 3 && std::ranges::count_if(
                        delta, [](auto value) { return value != 0; }) == 1,
        "Admitted Home/Nearby do not supply a cardinal basis");
  const long double separation =
      std::abs(static_cast<long double>(delta[axis]));
  const long double sign = delta[axis] < 0 ? -1 : 1;
  unsigned current{}, selected{};
  for (std::size_t index = 0; index < result.nodes.size(); ++index) {
    const auto& node = result.nodes[index];
    if (index >= count) {
      check(node == NativeKnownChartMapNode{},
            "Unused map node leaked hidden identity/coordinates");
      continue;
    }
    check(node.system == admitted[index] && node.system.value &&
              !node.name.empty() && node.name.size() <= 256 &&
              node.name.find('\0') == std::string::npos,
          "Map changed compacted admitted chart order or name bounds");
    const auto identity =
        reading(session, node.system, KnowledgeFact::identity_type);
    check(node.name == std::get<KnownIdentity>(identity.value).name,
          "Map invented a name instead of using admitted identity");
    check(node.current == (node.system == session.system().catalog.id) &&
              node.selected == (current_selection(session) == node.system),
          "Map markers used visited history or a stale typed system selection");
    current += node.current ? 1U : 0U;
    selected += node.selected ? 1U : 0U;
    const auto point = location(session, node.system);
    const std::array<long double, 3> offset{
        static_cast<long double>(point.x) - home.x,
        static_cast<long double>(point.y) - home.y,
        static_cast<long double>(point.z) - home.z};
    const long double a = sign * offset[axis];
    const long double b = offset[(axis + 1) % 3];
    const long double c = offset[(axis + 2) % 3];
    const auto expected_x =
        static_cast<double>((2 * a + b - c) / (2 * separation));
    const auto expected_y = static_cast<double>(.5L - (b + c) / separation);
    check(std::isfinite(node.plot_x) && std::isfinite(node.plot_y) &&
              node.plot_x >= 0 && node.plot_x <= 1 && node.plot_y >= 0 &&
              node.plot_y <= 1 && std::abs(node.plot_x - expected_x) < 1e-15 &&
              std::abs(node.plot_y - expected_y) < 1e-15,
          "Map projection lost the signed axis, third component or normalized "
          "bounds");
    for (std::size_t previous = 0; previous < index; ++previous)
      check(node.system != result.nodes[previous].system &&
                (node.plot_x != result.nodes[previous].plot_x ||
                 node.plot_y != result.nodes[previous].plot_y),
            "Known third-axis anchors collapsed to overlapping map points");
    if (node.system == route.origin)
      check(node.plot_x == 0 && node.plot_y == .5,
            "Home moved from fixed diagram origin");
    if (node.system == route.destination)
      check(node.plot_x == 1 && node.plot_y == .5,
            "Signed Nearby axis was mirrored");
  }
  check(current == 1 && selected <= 1,
        "Known-map current/selected markers are not unique");
  return result;
}
auto stable(const NativeKnownChartMapView& before,
            const NativeKnownChartMapView& after) -> void {
  for (std::size_t i = 0; i < before.node_count; ++i) {
    const auto& first = before.nodes[i];
    const auto found = std::ranges::find(after.nodes, first.system,
                                         &NativeKnownChartMapNode::system);
    check(
        found != after.nodes.end() && found->name == first.name &&
            found->plot_x == first.plot_x && found->plot_y == first.plot_y,
        "New knowledge, selection or replacement moved an already-known point");
  }
}
auto board(NativeFreedomFlightSession& session) -> void {
  for (unsigned tick = 0; tick < 1194; ++tick)
    need(session.advance_walk({0, -1, 0}));
  need(session.begin_boarding());
  for (SimulationTick tick = 0; tick < kGameplayBoardingTicks; ++tick)
    need(session.advance_walk({}));
  need(session.release_port());
}
auto basic_charts() -> void {
  auto pair = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(need(native_new_game({42})))));
  const auto pair_map = view(*pair, 2);
  check(generate_first_universe_route({42}).direction ==
            UniverseAxisDirection::negative_z,
        "Signed-axis fixture no longer uses the existing negative-Z route");
  auto four = std::make_unique<NativeFreedomFlightSession>(need(
      NativeFreedomFlightSession::open(need(native_network_new_game({42})))));
  const auto four_map = view(*four, 4);
  stable(pair_map, four_map);
  auto planetary = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_planetary_network_new_game({42})))));
  check(view(*planetary, 4).nodes == four_map.nodes,
        "Ordinary-planet facts changed a system-only diagram");
  auto bare = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_select_save_document(pair->document())))));
  const auto original = bytes(*bare);
  const auto missing = need(bare->known_chart_map_view());
  check(!missing.available && missing.node_count == 0 &&
            missing.nodes == std::array<NativeKnownChartMapNode,
                                        kNativeKnownChartMapCapacity>{} &&
            !missing.note.empty() && bytes(*bare) == original,
        "Missing ledgers generated a hidden chart or changed a bare flight");
}
auto signed_axes() -> void {
  // A bounded route-only unit-test search supplies one genuine seed per
  // orientation. Only those six actual four-known sessions are opened; no
  // world census, boarding voyage or invented anchor fixture is involved.
  std::array<std::optional<Seed>, 6> selected{};
  for (std::uint64_t candidate = 0; candidate < 64; ++candidate) {
    const auto route = generate_first_universe_route({candidate});
    const auto direction = static_cast<std::size_t>(route.direction);
    check(direction < selected.size() && route.distance_metres > 0,
          "Actual route supplied an invalid signed cardinal direction");
    if (!selected[direction]) selected[direction] = Seed{candidate};
  }
  for (std::size_t direction = 0; direction < selected.size(); ++direction) {
    check(selected[direction].has_value(),
          "Bounded genuine route fixtures did not cover all six signed axes");
    const auto seed = *selected[direction];
    auto live = std::make_unique<NativeFreedomFlightSession>(need(
        NativeFreedomFlightSession::open(need(native_network_new_game(seed)))));
    check(static_cast<std::size_t>(
              generate_first_universe_route(seed).direction) == direction,
          "Selected signed-axis seed changed its actual route");
    const auto result = view(*live, 4);
    check(need(continued(*live)->known_chart_map_view()) == result,
          "Signed-axis projection changed through ordinary Continue");
  }
}
auto no_lead_slot() -> void {
  auto live = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_uncharted_network_new_game({42})))));
  need(live->advance_walk({0, 0, -std::numbers::pi / 2}));
  const auto before = view(*live, 4);
  const auto offer = need(live->station_stellar_lead_view());
  check(offer.can_keep && offer.offer_tick,
        "Actual nearby attendant has no eligible report");
  need(live->receive_station_stellar_lead(*offer.offer_tick));
  check(need(live->station_stellar_lead_view()).status == "unverified" &&
            view(*live, 4) == before,
        "Unverified personal lead exposed an unknown system slot or changed "
        "extent");
}
auto malformed_and_empty(const NativeFreedomFlightSession& source) -> void {
  const auto original = bytes(source);
  // Ledger-only boundary fixtures retain the actual boarded body/world/tick.
  auto empty = std::make_unique<FreedomTravelSaveDocument>(
      need(source.travel_document()));
  empty->voyage.voyage.resources.jump_charges = 0;
  empty->voyage.voyage.resources.flight_quanta = 0;
  auto drained = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_select_save_document(std::move(*empty))))));
  check(view(*drained, 4) == view(source, 4),
        "Passive chart was gated on fuel or charges");
  for (unsigned fault = 0; fault < 7; ++fault) {
    auto invalid = std::make_unique<FreedomTravelSaveDocument>(
        need(source.travel_document()));
    auto& resource = invalid->voyage.voyage.resources;
    auto& knowledge = invalid->voyage.knowledge;
    switch (fault) {
      case 0: ++resource.tick; break;
      case 1: ++resource.craft.value; break;
      case 2: resource.jump_charges = kFreedomJumpCapacity + 1; break;
      case 3: ++knowledge.recipe.universe_seed.value; break;
      case 4:
        knowledge.entries.front().transitions.front().tick =
            source.document().flight.tick + 1;
        break;
      case 5: knowledge.recipe.physical_catalog = 999; break;
      default:
        std::get<FreedomBoardingSaveDocument>(
            invalid->voyage.voyage.voyage.base)
            .voyage.flight.flight.position_metres.x =
            std::numeric_limits<double>::quiet_NaN();
        break;
    }
    check(!native_select_save_document(std::move(*invalid)),
          "Malformed clock, owner, quantity, knowledge or nonfinite body "
          "passed admission");
  }
  check(bytes(source) == original,
        "Boundary admissions mutated their source voyage");
}
auto actual_route() -> void {
  auto live = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_planetary_network_new_game({42})))));
  const auto network = need(generate_freedom_system_network({42}));
  need(live->select_jump(network.nodes[1].system));
  const auto fixed = view(*live, 4);
  check(live->walker() && live->docking()->attached && !live->jump_available(),
        "Walking chart was confused with physical jump readiness");
  board(*live);
  malformed_and_empty(*live);
  auto typed = continued(*live);
  const auto other = std::ranges::find_if(
      typed->system().catalog.planets, [&](const auto& planet) {
        return planet.descriptor.id !=
               typed->document().origin.recipe.home_planet;
      });
  check(other != typed->system().catalog.planets.end(),
        "Seed lacks another ordinary planet");
  need(typed->select_planet_jump(
      {typed->system().catalog.id, body_id(other->descriptor.id)}));
  stable(fixed, view(*typed, 4));
  for (unsigned index = 0; index < 2; ++index) {
    auto local = continued(*live);
    const auto bodies = need(generate_secondary_body_catalog(local->system()));
    need(local->select_local_jump(
        {local->system().catalog.id, bodies.bodies[index].node.id}));
    stable(fixed, view(*local, 4));
  }
  need(live->begin_jump());
  stable(fixed, view(*live, 4));
  auto replay = continued(*live);
  for (SimulationTick tick = 1; tick <= kJumpSpoolTicks + kJumpTransitTicks;
       ++tick) {
    const auto step = need(live->advance({}));
    need(replay->advance({}));
    if (tick == kJumpSpoolTicks) {
      check(step.jump_committed, "Actual chart route did not commit");
      stable(fixed, view(*live, 4));
    }
    if (tick == kJumpSpoolTicks + kJumpTransitTicks)
      check(step.jump_arrived, "Actual chart route did not arrive");
  }
  check(bytes(*live) == bytes(*replay),
        "Map reads changed frozen arrival/resource replay");
  stable(fixed, view(*live, 4));
  check(live->system().catalog.id == network.nodes[1].system,
        "Current map marker lacks an actual system handoff");
  need(live->advance({}));
  const auto digest = need(
      rigid_body_state_checksum({live->system()}, live->document().flight));
  need(live->record_loss(FreedomLossCause::recoverable_destruction,
                         live->document().origin.state.craft,
                         live->document().flight.tick, digest));
  // This explicitly classified loss is not caused by the passive map or a
  // flown impact. Existing replacement preserves real chart history/anchors.
  need(live->complete_recovery());
  stable(fixed, view(*live, 4));
  check(live->walker() && live->system().catalog.id == network.nodes[0].system,
        "Replacement did not restore actual Origin ownership");
}
auto actual_acquisition() -> void {
  auto live = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_uncharted_network_new_game({42})))));
  auto prior = view(*live, 4);
  board(*live);
  for (std::uint32_t search = 0; search < 2; ++search) {
    need(live->begin_star_search());
    for (SimulationTick tick = 1; tick <= kFreedomStarSearchTicks; ++tick) {
      need(live->advance({}));
      if (tick == kFreedomStarSearchTicks - 1)
        stable(prior, view(*live, 4 + search));
    }
    check(live->knowledge()->star_search->completed.size() == search + 1,
          "Actual stellar acquisition did not earn the next observation");
    const auto next = view(*live, 5 + search);
    stable(prior, next);
    check(need(continued(*live)->known_chart_map_view()) == next,
          "Continue changed actual fifth/sixth observed positions");
    prior = next;
  }
  check(prior.node_count == kFreedomUnchartedSystemNetworkSize &&
            prior.version == 1,
        "Actual six-known fixture omitted the independently separated "
        "third-axis nodes");
}
auto outer_acquisition(const std::filesystem::path& output) -> void {
  auto live = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_frontier_network_new_game({42})))));
  const auto network = need(
      generate_freedom_system_network({42}, kFreedomOuterSystemNetworkVersion));
  const auto reach =
      generate_first_universe_route({42}).distance_light_seconds /
      kFreedomOuterSearchRangeDivisor;
  auto prior = view(*live, 4);
  check(prior.version == 2 && live->star_search_view().version == 2 &&
            !live->star_search_view().can_begin,
        "Outer station selects packet2 without claiming eligible flight or "
        "hidden slots");
  board(*live);
  const auto home_bytes = bytes(*live);
  const auto no_signal = live->star_search_view();
  check(no_signal.version == 2 && !no_signal.can_begin &&
            no_signal.note.find(std::to_string(reach)) != std::string::npos &&
            no_signal.note.find("between system anchors") !=
                std::string::npos &&
            !live->begin_star_search() && bytes(*live) == home_bytes,
        "Eligible Home flight truthfully refuses a local fix with its declared "
        "anchor reach, without advancing or changing the complete Save");
  const auto jump_to = [&](SystemId target) {
    need(live->select_jump(target));
    stable(prior, view(*live, prior.node_count));
    need(live->begin_jump());
    for (SimulationTick i = 0; i < kJumpSpoolTicks + kJumpTransitTicks; ++i)
      need(live->advance({}));
    check(live->system().catalog.id == target,
          "Actual outer map jump did not hand off to its selected system");
  };
  const auto acquire = [&]() -> SystemId {
    const auto source_bytes = bytes(*live);
    const auto ready = live->star_search_view();
    check(ready.version == 2 && ready.can_begin && !ready.active &&
              ready.note.find(std::to_string(reach)) != std::string::npos &&
              bytes(*live) == source_bytes,
          "Actual eligible outer source has a pure bounded signal preview");
    need(live->begin_star_search());
    for (SimulationTick i = 1; i <= kFreedomStarSearchTicks; ++i) {
      need(live->advance({}));
      if (i == kFreedomStarSearchTicks - 1)
        stable(prior, view(*live, prior.node_count));
    }
    const auto result = live->knowledge()->star_search->completed.back().target;
    const auto next = view(*live, prior.node_count + 1);
    stable(prior, next);
    check(need(continued(*live)->known_chart_map_view()) == next,
          "Continue changed the actually acquired outer map or packet version");
    prior = next;
    return result;
  };
  // Four real acquisitions and three existing charged handoffs. This is a
  // chart/projection proof, not a return, refueling or endurance voyage.
  jump_to(network.nodes[2].system);
  const auto first = acquire();
  const auto first_node =
      need(find_freedom_system_network_node(network, first));
  check(first_node.ordinal == 4 || first_node.ordinal == 5,
        "The first waypoint fix skipped its reachable inner frontier");
  jump_to(first);
  const auto outer = acquire();
  check(
      outer == network.nodes[first_node.ordinal == 4 ? 6 : 7].system,
      "The first discovered frontier did not acquire its nearer outer contact");
  const auto second = acquire();
  check(second == network.nodes[first_node.ordinal == 4 ? 5 : 4].system,
        "Inclusive h reach failed to retain the remaining inner frontier");
  jump_to(second);
  const auto last = acquire();
  check(last == network.nodes[first_node.ordinal == 4 ? 7 : 6].system &&
            prior.version == 2 && prior.node_count == 8 &&
            live->resources()->jump_charges == 0 &&
            !live->star_search_view().can_begin,
        "Eight admitted positions require actual acquisitions and exactly "
        "three original charges, with no new pool or hidden count");
  if (!output.empty()) {
    std::filesystem::create_directories(output);
    need(live->save_as(output / "frontier-eight.json"));
    auto restored = std::make_unique<NativeFreedomFlightSession>(
        need(NativeFreedomFlightSession::open(
            need(native_continue(output / "frontier-eight.json")))));
    check(bytes(*restored) == bytes(*live) &&
              need(restored->known_chart_map_view()) == prior,
          "Exported actual eight-anchor Save changed through file Continue");
    using Json = nlohmann::ordered_json;
    Json nodes = Json::array();
    for (std::uint32_t i = 0; i < prior.node_count; ++i) {
      const auto& node = prior.nodes[i];
      nodes.push_back({{"system_id", system_id_string(node.system)},
                       {"name", node.name},
                       {"current", node.current},
                       {"selected", node.selected},
                       {"plot_x", node.plot_x},
                       {"plot_y", node.plot_y}});
    }
    const Json packet{{"version", prior.version},
                      {"available", prior.available},
                      {"tick", std::to_string(prior.tick)},
                      {"note", prior.note},
                      {"nodes", nodes}};
    const Json manifest{
        {"schema", "apsis.frontier-chart-map-fixture/1"},
        {"completed", true},
        {"save", "frontier-eight.json"},
        {"map", "frontier-eight-map.json"},
        {"seed", 42},
        {"tick", std::to_string(prior.tick)},
        {"source",
         "Actual frontier New Game, station walk/boarding/release, "
         "three charged system handoffs and four 1200-tick acquisitions; "
         "no initialized pose, extra charge, return or refueling claim."}};
    const auto write = [&](const char* name, const Json& value) {
      std::ofstream stream(output / name);
      stream << value.dump(2) << '\n';
      stream.close();
      check(!stream.fail(),
            "Could not write actual frontier map fixture metadata");
    };
    write("frontier-eight-map.json", packet);
    write("manifest.json", manifest);
  }
}
} // namespace
} // namespace apsis_drift

auto main(int argc, char** argv) -> int {
  using namespace apsis_drift;
  std::filesystem::path output;
  const bool exporting =
      argc == 3 && std::string_view{argv[1]} == "--export-frontier";
  if (exporting) output = argv[2];
  if (argc != 1 && (!exporting || !output.is_absolute())) {
    std::cerr << "Expected --export-frontier ABS_DIR or no arguments\n";
    return 2;
  }
  try {
    basic_charts();
    signed_axes();
    no_lead_slot();
    actual_route();
    actual_acquisition();
    outer_acquisition(output);
    std::cout << checks << " known-chart-map checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << checks << " checks completed: " << error.what() << '\n';
    return 1;
  }
}
