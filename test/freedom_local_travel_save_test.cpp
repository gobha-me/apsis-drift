#include "apsis_drift/freedom_recovery_save.hpp"
#include "apsis_drift/freedom_travel_save.hpp"
#include "apsis_drift/native_craft_binding.hpp"

#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <source_location>
#include <stdexcept>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
auto check(bool good, const char* message) -> void {
  if (!good) throw std::runtime_error(message);
}
template <class T, class E>
auto need(std::expected<T, E> value,
          std::source_location location = std::source_location::current())
    -> T {
  if (!value) {
    std::cerr << location.file_name() << ':' << location.line() << ' ';
    if constexpr (requires { value.error().detail; })
      std::cerr << value.error().detail << '\n';
    throw std::runtime_error("Local travel codec fixture refused");
  }
  return std::move(*value);
}
auto set_tick(FreedomTravelSaveDocument& d, SimulationTick tick) -> void {
  auto& flight =
      std::get<FreedomFlightSaveDocument>(d.voyage.voyage.voyage.base);
  flight.flight.tick = tick;
  flight.origin.state.tick = tick;
  d.voyage.voyage.resources.tick = tick;
}
auto flight_of(const FreedomTravelSaveDocument& d)
    -> FreedomFlightSaveDocument {
  return surface_base_flight(d.voyage.voyage.voyage.base);
}
auto roundtrip(const FreedomTravelSaveDocument& d) -> void {
  // Keep an independent pre-call value to detect provider mutation.
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Immutability
  const auto before = d;
  const auto encoded = need(encode_freedom_travel_document_json(d));
  const auto decoded = need(decode_freedom_travel_document_json(encoded));
  check(d == before && decoded == d &&
            need(encode_freedom_travel_document_json(decoded)) == encoded &&
            Json::parse(encoded)["format_version"] == 45,
        "Explicit45 Continue preserves one owner, proof, actual binding and "
        "charge exactly");
}
auto fixture() -> FreedomTravelSaveDocument {
  const Seed seed{42};
  auto history = make_freedom_new_game_document(seed);
  const auto physical = need(generate_physical_origin_system(seed, 2));
  const auto station = generate_origin_station(seed);
  const auto geometry = need(origin_station_geometry(station));
  const auto port = need(resolve_origin_port_pose(physical, station, geometry,
                                                  {station.id, 1}, 5000));
  const auto departed = need(resolve_origin_port_pose(
      physical, station, geometry, {station.id, 1}, 5000, 12));
  history.state.tick = 5000;
  FreedomFlightSaveDocument flight{
      history,
      departed.planet_relative,
      {},
      FreedomActiveWorldSelection{1, physical.catalog.id,
                                  history.recipe.home_planet}};
  flight.model.physical_catalog = 2;
  flight.model.physical_ephemeris = 2;
  flight.model.terrain_policy = 3;
  flight.model.frame_policy = 2;
  flight.model.optics = 1;
  flight.model.companions = 1;
  flight.flight.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  OriginWalkerState entry;
  for (int i = 0; i < 2960; ++i)
    entry = need(advance_origin_walker(entry, {0, -1, 0}));
  const auto binding = need(make_native_starting_assembly_binding({}));
  const auto eye =
      RigidVector3{entry.foot_position_metres.x,
                   entry.foot_position_metres.y + kOriginWalkerEyeHeightMetres,
                   entry.foot_position_metres.z};
  const auto route = need(step_origin_gameplay_boarding(
      need(begin_origin_gameplay_boarding(binding, port, eye,
                                          entry.heading_radians,
                                          GameplayBoardingDirection::board)),
      kGameplayBoardingTicks));
  FreedomBoardingState pilot{FreedomBoardingPhase::seated, entry,
                             5000 - kGameplayBoardingTicks, route};
  const FreedomSurfaceSaveDocument surface{flight, {}};
  const FreedomResources resources{1,
                                   history.state.craft,
                                   flight.flight.craft,
                                   5000,
                                   kFreedomFlightCapacityQuanta - 123,
                                   3};
  const FreedomResourceSaveDocument fueled{surface, resources};
  const FreedomKnowledgeSaveDocument mapped{
      fueled, need(make_freedom_starting_knowledge(seed, 3))};
  FreedomTravelSaveDocument d{
      mapped, {}, NativeStartingAssemblySelection{}, pilot};
  d.travel.version = kFreedomLocalTravelStateVersion;
  d.travel.local.emplace();
  const auto catalog = need(generate_secondary_body_catalog(physical));
  d.travel.local->selected =
      BodyTarget{physical.catalog.id, catalog.bodies[0].node.id};
  return d;
}
auto commit(FreedomTravelSaveDocument& d, FreedomFlightModel home_model)
    -> void {
  const auto source = flight_of(d);
  const auto frozen =
      need(freeze_local_body_jump_arrival({1, source, *d.travel.local->selected,
                                           d.travel.next_attempt, home_model}));
  auto& local = *d.travel.local;
  local.committed = frozen;
  local.commitment_resources = d.voyage.voyage.resources;
  if (!local.outbound) local.outbound = frozen;
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
auto run() -> void {
  auto d = fixture();
  const auto home = flight_of(d);
  const auto original = d;
  roundtrip(d);
  auto historical = d;
  historical.travel = {};
  const auto bytes27 = need(encode_freedom_travel_document_json(historical));
  check(Json::parse(bytes27)["format_version"] == 27 &&
            need(encode_freedom_travel_document_json(
                need(decode_freedom_travel_document_json(bytes27)))) == bytes27,
        "Unselected historical27 retains unchanged canonical serialization");
  // A genuine old targeting solution remains bounded prior history when an
  // already-returned journey explicitly selects its first local route.
  const auto ids =
      generate_first_intersystem_identities(home.origin.recipe.universe_seed);
  const auto neighbor =
      need(generate_physical_local_system(ids.target_system_seed, 2));
  const auto& neighbor_planet = neighbor.catalog.planets[0].descriptor;
  auto previous_source = home.flight;
  previous_source.tick = 500;
  previous_source.frame = {RigidFrameKind::planet_relative_inertial,
                           ids.target_system,
                           neighbor_planet.id,
                           {}};
  previous_source.position_metres = {0, 0,
                                     neighbor_planet.radius.value * 10000.0};
  previous_source.linear_velocity_metres_per_second = {};
  const auto previous = need(freeze_freedom_jump_arrival(
      {1, home.origin.recipe.universe_seed, home.origin.state.craft,
       previous_source, ids.origin_system, IntersystemRuleProfile::assisted, 2,
       home.origin.lineage}));
  auto adopted = d;
  adopted.travel.committed = previous;
  adopted.travel.commitment_resources =
      FreedomResources{1,
                       home.origin.state.craft,
                       home.flight.craft,
                       previous_source.tick,
                       kFreedomFlightCapacityQuanta,
                       2};
  adopted.travel.next_attempt = 3;
  roundtrip(adopted);
  auto wrong_history = adopted;
  wrong_history.travel.next_attempt = 1;
  check(
      !validate_freedom_travel_document(wrong_history),
      "Retained genuine intersystem proof cannot lose its attempt provenance");
  d.travel.phase = FreedomJumpPhase::spool;
  d.travel.spool_tick = home.flight.tick;
  roundtrip(d);
  set_tick(d, home.flight.tick + kJumpSpoolTicks);
  commit(d, home.model);
  roundtrip(d);
  const auto outbound = *d.travel.local->outbound;
  check(d.voyage.voyage.resources.jump_charges == 2 &&
            d.travel.local->commitment_resources->jump_charges == 3,
        "Spool commit retains paired original ledger and exactly one debit");
  set_tick(d, flight_of(d).flight.tick + 1);
  roundtrip(d);
  auto bad = d;
  ++std::get<FreedomFlightSaveDocument>(bad.voyage.voyage.voyage.base)
        .flight.position_metres.x;
  check(!validate_freedom_travel_document(bad),
        "Transit pose cannot drift from its immutable source");
  bad = d;
  ++bad.voyage.voyage.resources.jump_charges;
  check(!validate_freedom_travel_document(bad),
        "Transit cannot restore or double-debit the committed charge");
  bad = d;
  --bad.voyage.voyage.resources.flight_quanta;
  check(!validate_freedom_travel_document(bad),
        "Transit cannot invent a flight-fuel debit");
  arrive(d);
  roundtrip(d);
  check(flight_of(d).moon && !flight_of(d).world &&
            need(freedom_recovery_source_checksum(d)) ==
                need(rigid_body_state_checksum(
                    RigidBodyWorldContext{
                        need(hydrate_freedom_moon_flight_document(flight_of(d)))
                            .moon},
                    flight_of(d).flight)),
        "Actual45 Moon ownership survives retained recovery checksum dispatch");
  const auto recovery = FreedomRecoverySaveDocument{d, {}};
  check(need(decode_freedom_recovery_document_json(
            need(encode_freedom_recovery_document_json(recovery)))) == recovery,
        "Recovery29 nests explicit45 with genuine retained craft binding");
  bad = d;
  bad.seated_pilot.reset();
  check(!validate_freedom_travel_document(bad),
        "A Moon frame never implies actual pilot seating");
  bad = d;
  bad.craft_binding.reset();
  check(!validate_freedom_travel_document(bad),
        "A Moon frame never implies authored hardware selection");
  d.travel.local->selected =
      BodyTarget{home.world->system, body_id(home.world->planet)};
  d.travel.phase = FreedomJumpPhase::spool;
  d.travel.spool_tick = flight_of(d).flight.tick;
  roundtrip(d);
  set_tick(d, flight_of(d).flight.tick + kJumpSpoolTicks);
  commit(d, outbound.request.home_model);
  roundtrip(d);
  check(d.travel.local->outbound == outbound &&
            d.voyage.voyage.resources.jump_charges == 1,
        "Return spends one charge while preserving original outbound proof");
  bad = d;
  auto request = bad.travel.local->committed->request;
  request.home_model.terrain_policy = 1;
  request.home_model.optics = 0;
  request.home_model.companions = 0;
  bad.travel.local->committed = need(freeze_local_body_jump_arrival(request));
  check(!validate_freedom_travel_document(bad),
        "Even a valid alternative return solution cannot rewrite retained home "
        "profile");
  arrive(d);
  roundtrip(d);
  check(!flight_of(d).moon && flight_of(d).world == home.world &&
            flight_of(d).model == home.model &&
            d.travel.local->outbound == outbound &&
            d.voyage.voyage.resources.jump_charges == 1 &&
            d.voyage.knowledge == original.voyage.knowledge,
        "Home return restores genuine selected profile without refund or "
        "invented observations");
  const auto root = Json::parse(need(encode_freedom_travel_document_json(d)));
  const auto rejects = [](const Json& value) {
    check(!decode_freedom_travel_document_json(value.dump()),
          "Malformed45 refuses without historical fallback");
  };
  auto corrupt = root;
  corrupt["format_version"] = 27;
  rejects(corrupt);
  corrupt = root;
  corrupt["travel"]["version"] = 1;
  rejects(corrupt);
  corrupt = root;
  corrupt["travel"]["local"]["outbound"] = nullptr;
  rejects(corrupt);
  corrupt = root;
  corrupt["travel"]["local"]["committed"]["source_checksum"] = 0;
  rejects(corrupt);
  corrupt = root;
  corrupt["travel"]["local"]["selected"] = Json::array();
  rejects(corrupt);
  corrupt = root;
  corrupt["travel"]["local"]["extra"] = true;
  rejects(corrupt);
  corrupt = root;
  corrupt["travel"]["next_attempt"] = std::numeric_limits<std::uint64_t>::max();
  rejects(corrupt);
  check(!decode_freedom_travel_document_json(std::string(65, '[') + "0" +
                                             std::string(65, ']')) &&
            !decode_freedom_travel_document_json(
                "{\"format_version\":45,\"format_version\":45}"),
        "Depth and duplicate-key checks run before45 dispatch");
}
} // namespace

auto main() -> int {
  try {
    run();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "Local travel save contracts passed\n";
}
