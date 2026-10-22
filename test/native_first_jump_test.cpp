#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "native_local_route_reuse_fixture.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
int checks{}, failures{};
std::uint64_t fingerprint{14695981039346656037ULL};
auto check(bool ok, std::string_view label) -> void {
  ++checks;
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> r) -> T {
  if (!r) {
    if constexpr (std::is_same_v<E, std::string>)
      std::cerr << r.error() << '\n';
    throw std::runtime_error("Native jump fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*r);
}
auto hash(std::string_view value) -> void {
  for (char c : value) {
    fingerprint ^= static_cast<unsigned char>(c);
    fingerprint *= 1099511628211ULL;
  }
}
auto fixture(Seed seed, bool assisted = true, bool network = false)
    -> NativeFreedomFlightSession {
  // Valid saved spaceflight fixture. Existing station/boarding tests own the
  // earlier walk/seat/departure route; every jump below uses actual session
  // ticks.
  const auto system = need(generate_physical_origin_system(seed, 2));
  const auto& body = system.catalog.planets[0].descriptor;
  FreedomFlightSaveDocument flight;
  flight.origin = network ? need(make_freedom_network_new_game_document(seed))
                          : make_freedom_new_game_document(seed);
  flight.origin.state.tick = 240;
  flight.model.physical_catalog = 2;
  flight.model.physical_ephemeris = 2;
  flight.model.assistance = assisted;
  flight.flight.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  flight.flight.frame = {
      RigidFrameKind::planet_relative_inertial, system.catalog.id, body.id, {}};
  flight.flight.tick = 240;
  flight.flight.position_metres = {0, 0, body.radius.value * 10000.0};
  flight.world = FreedomActiveWorldSelection{network ? 2U : 1U,
                                             system.catalog.id, body.id};
  FreedomResources resources{1, flight.origin.state.craft, flight.flight.craft,
                             240};
  auto knowledge = need(make_freedom_starting_knowledge(
      seed, network ? kFreedomNetworkKnowledgeVersion
                    : kFreedomTravelKnowledgeVersion));
  FreedomSurfaceSaveDocument surface{flight, {}};
  FreedomResourceSaveDocument fueled{std::move(surface), resources};
  FreedomKnowledgeSaveDocument mapped{std::move(fueled), std::move(knowledge)};
  FreedomTravelSaveDocument document{
      std::move(mapped), {}, NativeStartingAssemblySelection{}, {}};
  return need(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom, document, body, {}}));
}
auto resume(const NativeFreedomFlightSession& session,
            const std::filesystem::path& path) -> NativeFreedomFlightSession {
  need(session.save_as(path));
  auto copy =
      need(NativeFreedomFlightSession::open(need(native_continue(path))));
  check(need(copy.travel_document()) == need(session.travel_document()),
        "Every saved native phase resumes exact current owner/history/binding");
  return copy;
}
auto leg(NativeFreedomFlightSession& live, const std::filesystem::path& path)
    -> void {
  need(live.begin_jump());
  auto resumed = resume(live, path);
  const auto source_tick = live.document().flight.tick;
  const auto companions = live.document().model.companions;
  const auto before_charges = live.resources()->jump_charges;
  for (SimulationTick t = 1; t <= kJumpSpoolTicks + kJumpTransitTicks; ++t) {
    const auto step = need(live.advance({}));
    need(resumed.advance({}));
    if (t == 1 || t == kJumpSpoolTicks - 1 || t == kJumpSpoolTicks ||
        t == kJumpSpoolTicks + 1 ||
        t == kJumpSpoolTicks + kJumpTransitTicks - 1 ||
        t == kJumpSpoolTicks + kJumpTransitTicks)
      resumed = resume(resumed, path);
    if (t < kJumpSpoolTicks)
      check(live.resources()->jump_charges == before_charges &&
                !step.jump_committed,
            "Spool cannot bill a jump charge");
    if (t == kJumpSpoolTicks) {
      check(step.jump_committed &&
                live.resources()->jump_charges == before_charges - 1 &&
                live.travel()->committed,
            "One real charge commits atomically with frozen source/point");
      const auto frozen = *live.travel()->committed;
      check(!live.cancel_jump() && need(live.jump_preview()) == frozen.preview,
            "Committed transit cannot cancel or change preview sampling");
      check(!live.select_port(1) && !live.capture_port() &&
                !live.set_assistance(false) && !live.set_hold({}) &&
                !live.current_orbit_hold_target() &&
                !live.hold_current_orbit() && !live.set_landing_gear(true),
            "Transit refuses conflicting control/constraint mutations");
    }
    if (t > kJumpSpoolTicks)
      check(step.fuel_debit_quanta == 0 &&
                live.resources()->flight_quanta ==
                    live.travel()->commitment_resources->flight_quanta,
            "Transit advances clocks with no hidden flight-fuel charge");
    if (t == kJumpSpoolTicks + kJumpTransitTicks)
      check(step.jump_arrived,
            "Actual session performs scheduled physical handoff");
  }
  check(live.document().model.companions == companions &&
            live.document().flight.tick ==
                source_tick + kJumpSpoolTicks + kJumpTransitTicks &&
            need(live.travel_document()) == need(resumed.travel_document()),
        "Uninterrupted and all-boundary resumed native leg converge exactly");
  const auto id = live.system().catalog.id;
  const auto presence = need(query_freedom_knowledge(
      *live.knowledge(), {KnowledgeSubjectKind::system, id, id.value},
      KnowledgeFact::presence, live.document().flight.tick));
  check(presence &&
            presence->provenance.level == NavigationKnowledgeLevel::visited &&
            !live.docking() && !live.walker(),
        "Actual arrival visits system and retains free physical craft");
  check(live.craft_binding().selection() != nullptr,
        "Authored Wayfarer binding survives physical handoff and resume");
}
auto roundtrip(Seed seed, bool assisted, const std::filesystem::path& path)
    -> void {
  auto live = fixture(seed, assisted);
  const auto ids = generate_first_intersystem_identities(seed);
  const auto original = need(live.travel_document());
  check(!live.select_jump({0}) && need(live.travel_document()) == original,
        "Unknown selection preserves exact session");
  need(live.select_jump(ids.target_system));
  live = resume(live, path);
  need(live.begin_jump());
  need(live.advance({}));
  need(live.cancel_jump());
  check(live.resources()->jump_charges == 3 &&
            live.travel()->next_attempt == 1 && !live.travel()->committed,
        "Canceled actual spool consumes no charge or random attempt");
  leg(live, path);
  check(live.system().catalog.id == ids.target_system &&
            live.document().world->system == ids.target_system &&
            live.resources()->jump_charges == 2,
        "Outbound installs actual seeded neighbor with two charges");
  const auto before_service = need(live.travel_document());
  check(!live.replenish_resources() && !live.select_port(1) &&
            need(live.travel_document()) == before_service,
        "Neighbor has no phantom origin attachment/free service");
  need(live.select_jump(ids.origin_system));
  leg(live, path);
  check(live.system().catalog.id == ids.origin_system &&
            live.resources()->jump_charges == 1 &&
            live.travel()->next_attempt == 3,
        "Physical return spends second charge and leaves one reserve");
  hash(need(encode_freedom_travel_document_json(need(live.travel_document()))));
}
auto invalid(const std::filesystem::path& path) -> void {
  auto live = fixture({42});
  need(live.select_jump(
      generate_first_intersystem_identities({42}).target_system));
  need(live.begin_jump());
  const auto before = need(live.travel_document());
  NativeFlightControls bad;
  bad.positive_translation.x = std::numeric_limits<double>::quiet_NaN();
  check(!live.advance(bad) && need(live.travel_document()) == before,
        "Nonfinite controls preserve active source/phase/resources");
  check(!live.advance({}, SimulationSeconds{.1}) &&
            need(live.travel_document()) == before,
        "Nonfixed jump tick preserves full state");
  for (SimulationTick t = 0; t < kJumpSpoolTicks; ++t)
    need(live.advance({}));
  const auto root = Json::parse(
      need(encode_freedom_travel_document_json(need(live.travel_document()))));
  const auto refuses = [&](Json j) {
    check(!decode_freedom_travel_document_json(j.dump()),
          "Corrupt committed save refuses before state replacement");
  };
  auto corrupt = root;
  corrupt["travel"]["committed"]["point"][0] = 0.;
  refuses(corrupt);
  corrupt = root;
  corrupt["travel"]["committed"]["sample_seed"] = 0U;
  refuses(corrupt);
  corrupt = root;
  corrupt["travel"]["next_attempt"] = 99U;
  refuses(corrupt);
  corrupt = root;
  corrupt["travel"]["phase"] = 255U;
  refuses(corrupt);
  corrupt = root;
  corrupt["travel"]["commitment_resources"]["jump_charges"] = 0U;
  refuses(corrupt);
  corrupt = root;
  corrupt["craft_binding"] = nullptr;
  refuses(corrupt);
  corrupt = root;
  corrupt["travel"]["spool_tick"] = std::numeric_limits<std::uint64_t>::max();
  refuses(corrupt);
  check(!decode_freedom_travel_document_json(std::string(65, '[') + "0" +
                                             std::string(65, ']')),
        "Excessive root nesting is bounded before JSON parse");
  live = resume(live, path);
}
auto seated_continuity(const std::filesystem::path& path) -> void {
  auto on_station =
      need(NativeFreedomFlightSession::open(need(native_new_game({42}))));
  const OriginWalkControls walk{0, -1, 0};
  for (SimulationTick t = 0; t < 2960; ++t)
    need(on_station.advance_walk(walk));
  need(on_station.begin_boarding());
  for (SimulationTick t = 0; t < kGameplayBoardingTicks; ++t)
    need(on_station.advance_walk({}));
  const auto pilot = *on_station.boarding();
  check(pilot.phase == FreedomBoardingPhase::seated && !on_station.walker(),
        "Pilot history comes from actual walking and completed boarding");
  // Jump contract starts in a valid space fixture. The composed route test owns
  // actual departure/braking/rendezvous; retain the real pilot, not a fake
  // seat.
  auto document = need(fixture({42}).travel_document());
  auto& flight =
      std::get<FreedomFlightSaveDocument>(document.voyage.voyage.voyage.base);
  flight.model.optics = on_station.document().model.optics;
  flight.model.companions = on_station.document().model.companions;
  check(flight.model.companions == 1,
        "Selected NewGame companion component enters the seated travel route");
  flight.flight.tick = on_station.document().flight.tick;
  flight.origin.state.tick = flight.flight.tick;
  document.voyage.voyage.resources.tick = flight.flight.tick;
  document.seated_pilot = pilot;
  auto live = need(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom,
       document,
       on_station.system().catalog.planets[0].descriptor,
       {}}));
  live = resume(live, path);
  check(live.boarding() == pilot && need(live.boarding_view()).seated,
        "Independent seated history resumes without a phantom current port");
  const auto ids = generate_first_intersystem_identities({42});
  need(live.select_jump(ids.target_system));
  leg(live, path);
  check(live.boarding() == pilot && need(live.boarding_view()).seated &&
            need(live.travel_document()).seated_pilot == pilot,
        "Outbound jump preserves actual pilot, entry and seat presentation");
  auto corrupt = Json::parse(
      need(encode_freedom_travel_document_json(need(live.travel_document()))));
  corrupt["seated_pilot"]["phase"] = "station";
  check(!decode_freedom_travel_document_json(corrupt.dump()),
        "Free travel cannot manufacture a walking actor from a seat record");
  corrupt = Json::parse(
      need(encode_freedom_travel_document_json(need(live.travel_document()))));
  corrupt["seated_pilot"]["started_tick"] = "18446744073709551615";
  check(!decode_freedom_travel_document_json(corrupt.dump()),
        "Seat history requires an already completed route on the shared clock");
  const auto before = need(live.travel_document());
  check(!live.begin_disembarking() && need(live.travel_document()) == before,
        "Preserved entry does not create a neighbor station for disembarking");
  need(live.select_jump(ids.origin_system));
  leg(live, path);
  live = resume(live, path);
  need(live.select_port(1));
  check(live.boarding() == pilot &&
            !need(live.travel_document()).seated_pilot &&
            std::holds_alternative<FreedomBoardingSaveDocument>(
                live.surface_document().base),
        "Real home port selection transfers sole pilot ownership to boarding "
        "voyage");
  live = resume(live, path);
  check(live.boarding() == pilot && need(live.boarding_view()).seated,
        "Home port Save/Continue preserves the original entry without "
        "reboarding");
  auto duplicate = need(live.travel_document());
  duplicate.seated_pilot = pilot;
  check(!validate_freedom_travel_document(duplicate),
        "A nested boarding voyage and outer seat cannot both own a pilot");
}
auto station_selection(const std::filesystem::path& path) -> void {
  auto live =
      need(NativeFreedomFlightSession::open(need(native_new_game({42}))));
  const auto old = live.document();
  need(live.select_jump(
      generate_first_intersystem_identities({42}).target_system));
  check(live.walker() && live.docking()->attached && !live.jump_available() &&
            live.document().flight == old.flight &&
            live.resources()->jump_charges == 3,
        "Selecting on station opts into travel without moving or authorizing "
        "departure");
  live = resume(live, path);
  need(live.advance_walk({}));
  live = resume(live, path);
  check(live.walker() && live.docking()->attached && !live.begin_jump(),
        "Travel Save/Continue preserves the real walking/attachment owner");
}
auto marker_travel(const std::filesystem::path& path) -> void {
  auto source = fixture({42});
  auto document = need(source.travel_document());
  const auto& home = source.system().catalog.planets.front().descriptor;
  // A valid saved metadata fixture; actual actor-only recording is exercised
  // by the landed surface-session contract, not synthesized by this fixture.
  const FreedomSurfaceMarkerPoint point{
      source.system().catalog.id,
      home.id,
      {static_cast<double>(home.radius.value) * 1000 + 123.5, 0, 0},
      source.document().flight.tick};
  document.voyage.knowledge.surface_marker =
      FreedomSurfaceMarkerState{1, point};
  auto live = need(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom, document, home, {}}));
  const auto marker = live.knowledge()->surface_marker;
  check(need(live.surface_marker_view()).current_body,
        "Current-body saved marker has a planet-relative projection before "
        "travel");
  auto system_document = document;
  auto& system_flight = std::get<FreedomFlightSaveDocument>(
      system_document.voyage.voyage.voyage.base);
  system_flight.model.frame_policy = kFreedomReleaseSystemFramePolicy;
  system_flight.flight = need(reframe_rigid_body(
      RigidBodyWorldContext{source.system()}, system_flight.flight,
      {{RigidFrameKind::system_inertial, source.system().catalog.id, {}, {}},
       system_flight.flight.tick}));
  auto system_session = need(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom, system_document, home, {}}));
  const auto system_view = need(system_session.surface_marker_view());
  check(system_session.document().flight.frame.kind ==
                RigidFrameKind::system_inertial &&
            system_view.current_body && system_view.point == marker->point &&
            system_view.target_craft_relative_metres &&
            system_view.straight_line_distance_metres,
        "Release-policy system flight projects the marker through the same "
        "qualified current planet-relative reference");
  need(live.select_jump(
      generate_first_intersystem_identities({42}).target_system));
  leg(live, path);
  const auto offbody = need(live.surface_marker_view());
  check(live.knowledge()->surface_marker == marker &&
            offbody.point == marker->point && !offbody.current_body &&
            !offbody.target_craft_relative_metres &&
            !offbody.straight_line_distance_metres &&
            offbody.known_system_name && offbody.known_body_name,
        "Real system arrival preserves the exact marked point/tick with known "
        "identity and no fake current-body projection");
  live = resume(live, path);
  const auto tick = live.document().flight.tick;
  const auto facts = live.knowledge()->entries;
  need(live.clear_surface_marker());
  check(live.knowledge()->surface_marker &&
            !live.knowledge()->surface_marker->point &&
            live.knowledge()->entries == facts &&
            live.document().flight.tick == tick,
        "Off-body Clear retains selected metadata and changes no travel clock "
        "or facts");

  auto hidden = need(source.travel_document());
  const auto& unseen = source.system().catalog.planets[1].descriptor;
  hidden.voyage.knowledge.surface_marker = FreedomSurfaceMarkerState{
      1, FreedomSurfaceMarkerPoint{
             source.system().catalog.id,
             unseen.id,
             {static_cast<double>(unseen.radius.value) * 1000 + 123.5, 0, 0},
             0}};
  auto hidden_session = need(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom, hidden, home, {}}));
  const auto hidden_view = need(hidden_session.surface_marker_view());
  check(
      hidden_view.point && !hidden_view.current_body &&
          hidden_view.known_system_name && !hidden_view.known_body_name &&
          hidden_session.knowledge()->entries == source.knowledge()->entries,
      "Another-body metadata neither invents a body name nor grants knowledge");
}
auto network_roundtrip(const std::filesystem::path& path) -> void {
  const Seed seed{42};
  const auto network = need(generate_freedom_system_network(seed));
  const auto start = need(native_network_new_game(seed));
  auto station = need(NativeFreedomFlightSession::open(start));
  check(station.document().origin.network &&
            station.knowledge()->recipe.version == 4 &&
            station.docking()->attached && station.walker(),
        "Explicit four-system new game starts walking on the actual station");
  need(station.save_as(path));
  auto station_copy =
      need(NativeFreedomFlightSession::open(need(native_continue(path))));
  check(need(station_copy.save_document()) == need(station.save_document()),
        "Network station start saves and continues with the exact selected "
        "owner");
  auto neighbor = fixture(seed, true, true);
  need(neighbor.select_jump(network.nodes[1].system));
  leg(neighbor, path);
  check(
      neighbor.system().catalog.id == network.nodes[1].system,
      "The original neighboring catalog remains an actual network destination");
  auto live = std::move(station_copy);
  for (unsigned t = 0; t < 1194; ++t)
    need(live.advance_walk({0, -1, 0}));
  need(live.begin_boarding());
  for (unsigned t = 0; t < kGameplayBoardingTicks; ++t)
    need(live.advance_walk({}));
  need(live.release_port());
  NativeFlightControls withdrawal;
  withdrawal.negative_translation.y = .25;
  for (unsigned t = 0; t < 720; ++t)
    need(live.advance(withdrawal));
  // Existing composed routes own physical departure/braking. Begin the new
  // three-leg proof from an explicit valid space fixture, retaining the real
  // boarded pilot, origin history, observed facts and already-spent departure
  // fuel. This boundary is not an uninterrupted station-to-space flight proof.
  auto space = need(fixture(seed, true, true).travel_document());
  auto& flight =
      std::get<FreedomFlightSaveDocument>(space.voyage.voyage.voyage.base);
  flight.origin = live.document().origin;
  flight.model = live.document().model;
  flight.flight.orientation = live.document().flight.orientation;
  flight.flight.tick = live.document().flight.tick;
  space.voyage.voyage.resources = *live.resources();
  space.voyage.knowledge = *live.knowledge();
  space.seated_pilot = *live.boarding();
  live = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(std::move(space)))));
  const auto home = live.document().origin;
  for (const auto ordinal : {2U, 3U, 0U}) {
    const auto destination = network.nodes[ordinal].system;
    need(live.select_jump(destination));
    const auto preview = need(live.jump_preview());
    check(preview.request.version == kFreedomNetworkJumpTargetingVersion &&
              preview.request.network == home.network &&
              preview.request.destination == destination,
          "Native preview authenticates the selected network and actual "
          "requested leg");
    leg(live, path);
    check(live.system() ==
                  need(resolve_freedom_network_system(network, destination)) &&
              live.document().origin.network == home.network &&
              live.document().world->version == 2 &&
              live.knowledge()->recipe.version == 4,
          "Actual charged arrival installs the selected catalog without losing "
          "the network owner");
    if (ordinal != 0) {
      const auto saved = need(live.travel_document());
      check(!live.replenish_resources() && !live.select_port(1) &&
                need(live.travel_document()) == saved,
            "Added systems have no invented station or free service");
    }
  }
  check(live.resources()->jump_charges == 0 &&
            live.travel()->next_attempt == 4 &&
            live.system().catalog.id == network.nodes[0].system,
        "Home to A to B to Home spends exactly three real charges");
  const auto arrival = need(live.travel_document());
  check(!live.replenish_resources() && need(live.travel_document()) == arrival,
        "Being in the Home system does not replenish fuel before physical "
        "docking");
  // Reuse the established physical return pilot: an intersystem arrival is
  // not guaranteed to start inside the optional rendezvous aid's approach gate.
  const auto fuel_before_capture = live.resources()->flight_quanta;
  const auto capture_ticks = origin_route_reuse_fixture::capture_home(live);
  check(capture_ticks > 0 &&
            live.resources()->flight_quanta < fuel_before_capture,
        "Actual network return flies to the moving station with rated fuelled "
        "thrusters");
  check(live.docking()->attached && live.resources()->jump_charges == 0,
        "Real thrusters and explicit port capture return the empty drive to "
        "service");
  need(live.replenish_resources());
  check(live.resources()->jump_charges == 3 &&
            live.resources()->flight_quanta == kFreedomFlightCapacityQuanta,
        "Origin service replenishes the physically docked four-system craft");
  live = resume(live, path);
}
auto uncharted_roundtrip(const std::filesystem::path& path) -> void {
  const Seed seed{42};
  const auto network = need(generate_freedom_system_network(seed, 2));
  auto live = need(NativeFreedomFlightSession::open(
      need(native_uncharted_network_new_game(seed))));
  check(live.document().origin.network == FreedomNetworkSelection{2} &&
            live.knowledge()->recipe.version == 6 && live.walker() &&
            live.docking()->attached && !live.star_search_view().can_begin,
        "Explicit uncharted selection starts on the station, not in a "
        "discovery fixture");
  const auto initial =
      need(encode_native_save_document_json(need(live.save_document())));
  check(!live.begin_star_search() && need(encode_native_save_document_json(need(
                                         live.save_document()))) == initial,
        "Station search refusal preserves the exact selected journey");
  for (unsigned t = 0; t < 1194; ++t)
    need(live.advance_walk({0, -1, 0}));
  need(live.begin_boarding());
  for (SimulationTick t = 0; t < kGameplayBoardingTicks; ++t)
    need(live.advance_walk({}));
  need(live.release_port());
  auto source =
      std::get<FreedomKnowledgeSaveDocument>(need(live.save_document()));
  auto& flight =
      std::get<FreedomBoardingSaveDocument>(source.voyage.voyage.base)
          .voyage.flight;
  const auto home = need(find_local_system_planet(
      live.system(), flight.origin.recipe.home_planet));
  // Explicit saved-space placement; actual station walk/boarding is retained.
  // Searches, jumps and final powered station capture below advance real ticks.
  flight.flight.position_metres = {
      0, 0, static_cast<double>(home->descriptor.radius.value) * 10000};
  flight.flight.linear_velocity_metres_per_second = {};
  flight.flight.angular_velocity_radians_per_second = {};
  flight.flight = need(canonicalize_rigid_body_state(
      RigidBodyWorldContext{live.system()}, flight.flight));
  live = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(std::move(source)))));
  const auto frozen =
      need(encode_native_save_document_json(need(live.save_document())));
  for (auto i : {4U, 5U})
    check(!live.select_jump(network.nodes[i].system) &&
              need(encode_native_save_document_json(
                  need(live.save_document()))) == frozen,
          "Guessed genuine but unobserved system IDs cannot select a native "
          "jump");
  for (unsigned discovery = 0; discovery < 2; ++discovery) {
    const auto before = live.resources()->jump_charges;
    need(live.begin_star_search());
    for (unsigned t = 0; t < kFreedomStarSearchTicks; ++t) {
      need(live.advance({}));
      if (t == 599) {
        const auto text =
            need(encode_native_save_document_json(need(live.save_document())));
        auto copy = need(
            NativeFreedomFlightSession::open(need(native_select_save_document(
                need(decode_native_save_document_json(text))))));
        check(copy.star_search_view().acquired_ticks == 600 &&
                  need(encode_native_save_document_json(
                      need(copy.save_document()))) == text,
              "Partial stellar search survives actual native Save/Continue "
              "exactly");
        if (discovery == 0) {
          need(copy.select_jump(network.nodes[1].system));
          need(copy.begin_jump());
          need(copy.advance({}));
          check(copy.star_search_view().active &&
                    copy.star_search_view().acquired_ticks == 600,
                "Uncommitted spool holds an unfinished stellar search");
          need(copy.cancel_jump());
          auto committing = copy;
          need(committing.begin_jump());
          for (SimulationTick n = 0; n < kJumpSpoolTicks; ++n)
            need(committing.advance({}));
          check(!committing.star_search_view().active &&
                    committing.resources()->jump_charges == 2,
                "Actual jump commitment discards unfinished acquisition");
          auto malformed = need(committing.travel_document());
          malformed.voyage.knowledge.star_search->active =
              copy.knowledge()->star_search->active;
          const auto forged = malformed;
          check(!validate_freedom_travel_document(malformed) &&
                    !encode_freedom_travel_document_json(malformed) &&
                    malformed == forged,
                "Continue refuses restored precommit search in genuine "
                "committed transit");
        }
        live = std::move(copy);
      }
    }
    const auto& search = *live.knowledge()->star_search;
    check(!search.active && search.completed.size() == discovery + 1 &&
              live.resources()->jump_charges == before,
          "Search completes on real flight ticks without spending jump fuel");
    const auto target = search.completed.back().target;
    const auto unseen = need(resolve_freedom_network_system(network, target));
    for (const auto& body : unseen.catalog.planets)
      check(
          !need(query_freedom_knowledge(
              *live.knowledge(),
              {KnowledgeSubjectKind::planet, target, body.descriptor.id.value},
              KnowledgeFact::identity_type, live.document().flight.tick)),
          "Stellar fix grants no remote planetary identities");
    need(live.select_jump(target));
    leg(live, path);
    check(live.system() == unseen &&
              live.resources()->jump_charges == before - 1 &&
              !live.replenish_resources(),
          "Actual charged arrival installs the generated previously unknown "
          "catalog without service");
  }
  check(!live.begin_star_search() && live.star_search_view().completed == 2,
        "Exhausted finite search cannot reroll contacts");
  need(live.select_jump(network.nodes[0].system));
  leg(live, path);
  check(live.resources()->jump_charges == 0,
        "Home to uncharted X to uncharted Y to Home spends exactly three "
        "charges");
  const auto fuel = live.resources()->flight_quanta;
  check(origin_route_reuse_fixture::capture_home(live) > 0 &&
            live.resources()->flight_quanta < fuel && live.docking()->attached,
        "The empty drive returns by physical fuelled station capture");
  need(live.replenish_resources());
  check(live.resources()->jump_charges == 3 &&
            live.knowledge()->star_search->completed.size() == 2,
        "Physical Origin service restores charges while discoveries remain "
        "recorded");
  live = resume(live, path);
}
} // namespace
auto main(int argc, char** argv) -> int {
  const bool uncharted =
      argc == 2 && std::string_view{argv[1]} == "--uncharted";
  if (argc != 1 && !uncharted) return 2;
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("apsis-native-jump-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    std::filesystem::create_directories(directory);
    if (uncharted) {
      uncharted_roundtrip(directory / "uncharted.json");
      std::filesystem::remove_all(directory);
      std::cout << checks << " uncharted native checks\n";
      return failures ? 1 : 0;
    }
    network_roundtrip(directory / "network.json");
    invalid(directory / "phase.json");
    station_selection(directory / "phase.json");
    seated_continuity(directory / "phase.json");
    marker_travel(directory / "marker.json");
    for (auto seed :
         {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}})
      for (bool assisted : {true, false})
        roundtrip(seed, assisted, directory / "phase.json");
    std::filesystem::remove_all(directory);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    std::filesystem::remove_all(directory);
    return 1;
  }
  check(fingerprint == 14140637698110517716ULL,
        "Three-seed dual-profile physical round-trip golden remains exact");
  std::cout << checks << " native jump checks; fingerprint " << fingerprint
            << '\n';
  return failures ? 1 : 0;
}
