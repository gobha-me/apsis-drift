#include "apsis_drift/freedom_system_network.hpp"
#include "apsis_drift/freedom_travel_save.hpp"
#include "apsis_drift/native_craft_binding.hpp"

#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <source_location>
#include <stdexcept>
#include <type_traits>

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
          std::source_location location = std::source_location::current())
    -> T {
  if (!value) {
    std::cerr << location.file_name() << ':' << location.line() << ' ';
    if constexpr (requires { value.error().detail; })
      std::cerr << value.error().detail << '\n';
    else if constexpr (std::is_enum_v<E>)
      std::cerr << "provider refusal " << static_cast<unsigned>(value.error())
                << '\n';
    throw std::runtime_error("Composed codec fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
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
        "Explicit56 preserves current voyage, fixed receipts, bill and "
        "canonical bytes");
  check(Json::parse(encoded)["format_version"] == 56,
        "Composed travel has distinct outer56");
  return encoded;
}
auto tick(FreedomTravelSaveDocument& d, SimulationTick time) -> void {
  auto& flight =
      std::get<FreedomFlightSaveDocument>(d.voyage.voyage.voyage.base);
  flight.flight.tick = time;
  flight.origin.state.tick = time;
  d.voyage.voyage.resources.tick = time;
}
auto fixture() -> FreedomTravelSaveDocument {
  // Saved-space codec fixture, not a claimed flown departure or descent.
  // The pilot is constructed from the genuine station entry/boarding route.
  const Seed seed{42};
  auto origin = need(make_freedom_network_new_game_document(seed));
  const auto system = need(generate_physical_origin_system(seed, 2));
  const auto station = generate_origin_station(seed);
  const auto geometry = need(origin_station_geometry(station));
  const auto port = need(resolve_origin_port_pose(system, station, geometry,
                                                  {station.id, 1}, 5000));
  OriginWalkerState entry;
  for (unsigned i = 0; i < 2960; ++i)
    entry = need(advance_origin_walker(entry, {0, -1, 0}));
  const auto binding = need(make_native_starting_assembly_binding({}));
  const RigidVector3 eye{entry.foot_position_metres.x,
                         entry.foot_position_metres.y +
                             kOriginWalkerEyeHeightMetres,
                         entry.foot_position_metres.z};
  const auto route = need(step_origin_gameplay_boarding(
      need(begin_origin_gameplay_boarding(binding, port, eye,
                                          entry.heading_radians,
                                          GameplayBoardingDirection::board)),
      kGameplayBoardingTicks));
  const FreedomBoardingState pilot{FreedomBoardingPhase::seated, entry,
                                   5000 - kGameplayBoardingTicks, route};
  origin.state.tick = 5000;
  FreedomFlightSaveDocument flight;
  flight.origin = origin;
  flight.model.physical_catalog = 2;
  flight.model.physical_ephemeris = 2;
  flight.model.terrain_policy = 3;
  flight.model.frame_policy = 2;
  flight.model.optics = 1;
  flight.model.companions = 1;
  flight.world = FreedomActiveWorldSelection{2, system.catalog.id,
                                             origin.recipe.home_planet};
  flight.flight = port.planet_relative;
  flight.flight.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  const auto home =
      need(find_local_system_planet(system, origin.recipe.home_planet));
  flight.flight.position_metres = {
      0, 0, static_cast<double>(home->descriptor.radius.value) * 10000};
  flight.flight.orientation = {};
  flight.flight.linear_velocity_metres_per_second = {};
  flight.flight.angular_velocity_radians_per_second = {};
  flight.flight = need(canonicalize_rigid_body_state(
      RigidBodyWorldContext{system}, flight.flight));
  const FreedomSurfaceSaveDocument surface{flight, {}};
  const FreedomResources resources{1,
                                   origin.state.craft,
                                   flight.flight.craft,
                                   5000,
                                   kFreedomFlightCapacityQuanta - 123,
                                   3};
  const FreedomResourceSaveDocument fueled{surface, resources};
  FreedomKnowledgeSaveDocument mapped{
      fueled, need(make_freedom_starting_knowledge(seed, 5))};
  FreedomTravelSaveDocument result{
      mapped, {}, NativeStartingAssemblySelection{}, pilot};
  result.travel.version = kFreedomComposedTravelStateVersion;
  result.travel.composed.emplace();
  return result;
}
auto home_target(const FreedomTravelSaveDocument& d) -> BodyTarget {
  const auto flight = flight_of(d);
  const auto system = need(
      generate_physical_origin_system(flight.origin.recipe.universe_seed, 2));
  return {system.catalog.id, body_id(flight.origin.recipe.home_planet)};
}
auto companion_target(const FreedomTravelSaveDocument& d,
                      FreedomComposedRouteKind kind) -> BodyTarget {
  const auto flight = flight_of(d);
  const auto system = need(
      generate_physical_origin_system(flight.origin.recipe.universe_seed, 2));
  const auto catalog = need(generate_secondary_body_catalog(system));
  return {
      system.catalog.id,
      catalog.bodies[kind == FreedomComposedRouteKind::moon ? 0 : 1].node.id};
}
auto planet_target(const FreedomTravelSaveDocument& d) -> BodyTarget {
  const auto flight = flight_of(d);
  const auto system = need(hydrate_freedom_flight_document(flight)).system;
  const auto current = *flight.world;
  for (const auto& planet : system.catalog.planets)
    if (planet.descriptor.id != current.planet)
      return {system.catalog.id, body_id(planet.descriptor.id)};
  throw std::runtime_error("Expected another genuine ordinary planet");
}
auto select(FreedomTravelSaveDocument& d, FreedomComposedRouteKind kind,
            BodyTarget target) -> void {
  auto& c = *d.travel.composed;
  c.active = kind;
  c.selected_body = target;
  d.travel.selected.reset();
  roundtrip(d);
}
auto commit_body(FreedomTravelSaveDocument& d) -> void {
  auto& c = *d.travel.composed;
  const auto source = flight_of(d);
  if (c.active == FreedomComposedRouteKind::planet) {
    c.planet_committed = need(freeze_freedom_planet_jump_arrival(
        {1, source, *c.selected_body, d.travel.next_attempt}));
    c.planet_commitment_resources = d.voyage.voyage.resources;
  } else {
    auto& local = c.active == FreedomComposedRouteKind::moon ? c.moon : c.minor;
    const auto model = source.moon || source.minor
                           ? local.outbound->request.home_model
                           : source.model;
    const auto policy = c.active == FreedomComposedRouteKind::moon ? 1U : 2U;
    const auto frozen = need(freeze_local_body_jump_arrival(
        {policy, source, *c.selected_body, d.travel.next_attempt, model}));
    local.committed = frozen;
    local.commitment_resources = d.voyage.voyage.resources;
    if (!source.moon && !source.minor) local.outbound = frozen;
  }
  --d.voyage.voyage.resources.jump_charges;
  ++d.travel.next_attempt;
  d.travel.phase = FreedomJumpPhase::transit;
}
auto arrive_body(FreedomTravelSaveDocument& d) -> void {
  const auto& c = *d.travel.composed;
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Replace
  // owner after snapshot
  const auto arrival = c.active == FreedomComposedRouteKind::planet
                           ? c.planet_committed->arrival
                           : (c.active == FreedomComposedRouteKind::moon
                                  ? c.moon.committed->arrival
                                  : c.minor.committed->arrival);
  d.voyage.voyage.voyage.base = arrival;
  d.voyage.voyage.resources.tick = arrival.flight.tick;
  d.travel.phase = FreedomJumpPhase::idle;
  d.travel.spool_tick = 0;
  d.travel.composed->selected_body.reset();
}
auto body_leg(FreedomTravelSaveDocument& d) -> FreedomTravelSaveDocument {
  d.travel.phase = FreedomJumpPhase::spool;
  d.travel.spool_tick = flight_of(d).flight.tick;
  roundtrip(d);
  tick(d, d.travel.spool_tick + kJumpSpoolTicks);
  commit_body(d);
  roundtrip(d);
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Frozen
  // transit counterexample
  const auto committed = d;
  tick(d, flight_of(d).flight.tick + kJumpTransitTicks - 1);
  roundtrip(d);
  arrive_body(d);
  roundtrip(d);
  return committed;
}
auto codec_budget(FreedomTravelSaveDocument& d) -> void {
  // Explicit codec ledger fixture, NOT a claim of attached station service.
  // Native tests separately own real thruster/spool/service transactions.
  tick(d, flight_of(d).flight.tick + 1);
  d.voyage.voyage.resources.jump_charges = 3;
}
auto system_leg(FreedomTravelSaveDocument& d, SystemId destination) -> void {
  auto& s = d.travel;
  s.composed->active = FreedomComposedRouteKind::system;
  s.composed->selected_body.reset();
  s.selected = destination;
  roundtrip(d);
  s.phase = FreedomJumpPhase::spool;
  s.spool_tick = flight_of(d).flight.tick;
  roundtrip(d);
  tick(d, s.spool_tick + kJumpSpoolTicks);
  const auto source = flight_of(d);
  const auto frozen = need(freeze_freedom_jump_arrival(
      {2, source.origin.recipe.universe_seed, source.origin.state.craft,
       source.flight, destination, IntersystemRuleProfile::assisted,
       s.next_attempt, source.origin.lineage, source.origin.network}));
  s.committed = frozen;
  s.commitment_resources = d.voyage.voyage.resources;
  --d.voyage.voyage.resources.jump_charges;
  ++s.next_attempt;
  s.phase = FreedomJumpPhase::transit;
  roundtrip(d);
  const auto network =
      need(generate_freedom_system_network(source.origin.recipe.universe_seed));
  const auto system =
      need(resolve_freedom_network_system(network, destination));
  const auto centre =
      need(resolve_planet_ephemeris(system, frozen.preview.reference_planet,
                                    {frozen.preview.arrival_tick, 0}));
  auto arrival = source;
  arrival.flight.tick = frozen.preview.arrival_tick;
  arrival.origin.state.tick = arrival.flight.tick;
  arrival.flight.frame = {RigidFrameKind::planet_relative_inertial,
                          destination,
                          frozen.preview.reference_planet,
                          {}};
  arrival.flight.position_metres = {frozen.point.x - centre.position.x,
                                    frozen.point.y - centre.position.y,
                                    frozen.point.z - centre.position.z};
  arrival.flight.linear_velocity_metres_per_second = {
      frozen.preview.arrival_velocity.x - centre.velocity.x,
      frozen.preview.arrival_velocity.y - centre.velocity.y,
      frozen.preview.arrival_velocity.z - centre.velocity.z};
  arrival.flight = need(canonicalize_rigid_body_state(
      RigidBodyWorldContext{system}, arrival.flight));
  arrival.world = FreedomActiveWorldSelection{2, destination,
                                              frozen.preview.reference_planet};
  arrival.model.hold = {};
  d.voyage.voyage.voyage.base = arrival;
  d.voyage.voyage.resources.tick = arrival.flight.tick;
  s.selected.reset();
  s.phase = FreedomJumpPhase::idle;
  s.spool_tick = 0;
  roundtrip(d);
}
auto invalids(const FreedomTravelSaveDocument& transit) -> void {
  for (unsigned fault = 0; fault < 23; ++fault) {
    auto bad = transit;
    auto& s = bad.travel;
    auto& c = *s.composed;
    switch (fault) {
      case 0: s.version = 4; break;
      case 1: s.composed.reset(); break;
      case 2: c.version = 2; break;
      case 3: c.active = static_cast<FreedomComposedRouteKind>(4); break;
      case 4: s.local.emplace(); break;
      case 5: s.prior_moon.emplace(); break;
      case 6: s.origin_routes.emplace(); break;
      case 7: s.selected = flight_of(bad).world->system; break;
      case 8: c.selected_body.reset(); break;
      case 9: ++s.next_attempt; break;
      case 10: ++s.spool_tick; break;
      case 11: c.planet_commitment_resources.reset(); break;
      case 12: ++c.planet_commitment_resources->tick; break;
      case 13: ++bad.voyage.voyage.resources.jump_charges; break;
      case 14: ++bad.voyage.voyage.resources.flight_quanta; break;
      case 15: c.planet_committed->source_checksum ^= 1U; break;
      case 16: c.planet_committed->arrival.flight.position_metres.x += 1; break;
      case 17: c.selected_body->body.kind = BodyKind::moon; break;
      case 18: c.selected_body->body.version = 2; break;
      case 19: c.planet_committed->request.source.origin.network.reset(); break;
      case 20:
        std::get<FreedomFlightSaveDocument>(bad.voyage.voyage.voyage.base)
            .flight.position_metres.x =
            std::numeric_limits<double>::quiet_NaN();
        break;
      case 21:
        s.next_attempt = std::numeric_limits<std::uint64_t>::max();
        break;
      case 22: bad.craft_binding.reset(); break;
      default: break;
    }
    check(!validate_freedom_travel_document(bad) &&
              !encode_freedom_travel_document_json(bad),
          "Composed malformed owner/route/bill/phase/nonfinite/counter refuses "
          "atomically");
  }
  auto encoded = Json::parse(roundtrip(transit));
  for (unsigned fault = 0; fault < 8; ++fault) {
    auto bad = encoded;
    switch (fault) {
      case 0: bad["format_version"] = 53U; break;
      case 1: bad["travel"]["version"] = 4U; break;
      case 2: bad["travel"]["composed"]["extra"] = 0U; break;
      case 3: bad["travel"]["composed"]["moon"]["extra"] = 0U; break;
      case 4:
        bad["travel"]["composed"]["planet_committed"]["source_checksum"] = 0U;
        break;
      case 5: bad["travel"]["composed"]["active"] = -1; break;
      case 6: bad["travel"]["composed"]["selected_body"] = nullptr; break;
      case 7: bad["travel"]["next_attempt"] = 1.5; break;
      default: break;
    }
    check(!decode_freedom_travel_document_json(bad.dump()),
          "Closed56 rejects wrong envelope, extra/null fields and malformed "
          "scalar types");
  }
  auto duplicate = roundtrip(transit);
  duplicate.insert(duplicate.find("\"format_version\""),
                   "\"format_version\":56,");
  check(!decode_freedom_travel_document_json(duplicate),
        "56 rejects duplicate keys before dispatch");
  check(!decode_freedom_travel_document_json(
            std::string(kMaximumSaveDocumentBytes + 1, ' ')),
        "56 public admission bounds input bytes before parsing");
}
auto run() -> void {
  auto d = fixture();
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Immutability
  // witness
  const auto knowledge = d.voyage.knowledge;
  roundtrip(d);
  auto wrong_recipe = d;
  wrong_recipe.voyage.knowledge =
      need(make_freedom_starting_knowledge(Seed{42}, 4));
  check(!validate_freedom_travel_document(wrong_recipe),
        "Old chart4 cannot silently opt into composition");
  auto old = wrong_recipe;
  old.travel = {};
  const auto bytes53 = need(encode_freedom_travel_document_json(old));
  check(Json::parse(bytes53)["format_version"] == 53 &&
            need(encode_freedom_travel_document_json(
                need(decode_freedom_travel_document_json(bytes53)))) == bytes53,
        "Unselected old network53 stays byte-exact and unadopted");
  const auto home = home_target(d);
  const auto other = planet_target(d);
  select(d, FreedomComposedRouteKind::planet, other);
  const auto transit = body_leg(d);
  invalids(transit);
  select(d, FreedomComposedRouteKind::planet, home);
  body_leg(d);
  codec_budget(d);
  select(d, FreedomComposedRouteKind::moon,
         companion_target(d, FreedomComposedRouteKind::moon));
  body_leg(d);
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Preserve
  // outgoing witness across return
  const auto outbound = d.travel.composed->moon.outbound;
  auto missing_outbound = d;
  missing_outbound.travel.composed->moon.outbound.reset();
  check(!validate_freedom_travel_document(missing_outbound),
        "Actual companion arrival cannot infer a missing home-model witness");
  select(d, FreedomComposedRouteKind::moon, home);
  body_leg(d);
  check(d.travel.composed->moon.outbound == outbound,
        "Moon return retains actual excursion source and home model");
  codec_budget(d);
  select(d, FreedomComposedRouteKind::minor,
         companion_target(d, FreedomComposedRouteKind::minor));
  body_leg(d);
  select(d, FreedomComposedRouteKind::minor, home);
  body_leg(d);
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Independent
  // retained route snapshot
  const auto moon = d.travel.composed->moon;
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Independent
  // retained route snapshot
  const auto minor = d.travel.composed->minor;
  codec_budget(d);
  const auto network = need(generate_freedom_system_network(Seed{42}));
  system_leg(d, network.nodes[1].system);
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Historical
  // receipt must remain exact
  const auto system_proof = d.travel.committed;
  select(d, FreedomComposedRouteKind::planet, planet_target(d));
  body_leg(d);
  check(d.travel.committed == system_proof && d.travel.composed->moon == moon &&
            d.travel.composed->minor == minor &&
            d.voyage.knowledge == knowledge,
        "Ordinary travel composes with retained system/Moon/minor proofs and "
        "personal knowledge");
  codec_budget(d);
  system_leg(d, home.system);
  select(d, FreedomComposedRouteKind::moon,
         companion_target(d, FreedomComposedRouteKind::moon));
  body_leg(d);
  check(d.travel.composed->moon.outbound != outbound &&
            d.travel.composed->moon.outbound ==
                d.travel.composed->moon.committed,
        "Next actual outbound refreshes one bounded excursion witness without "
        "fake first history");
  auto bad = d;
  bad.travel.composed->moon.outbound = outbound;
  check(!validate_freedom_travel_document(bad),
        "Stale excursion witness cannot own a new departure");
  auto reversed = d;
  auto request = reversed.travel.composed->planet_committed->request;
  const auto earlier =
      reversed.travel.composed->minor.committed->request.source.flight.tick;
  request.source.flight.tick = earlier;
  request.source.origin.state.tick = earlier;
  const auto regenerated = need(freeze_freedom_planet_jump_arrival(request));
  check(validate_frozen_freedom_planet_jump_arrival(regenerated).has_value(),
        "Chronology counterexample is an independently valid regenerated "
        "planet proof");
  reversed.travel.composed->planet_committed = regenerated;
  reversed.travel.composed->planet_commitment_resources->tick = earlier;
  const auto refused = validate_freedom_travel_document(reversed);
  check(
      !refused && refused.error().detail ==
                      "Retained routes reverse clock or adjacent body owner",
      "Individually valid stale clocks cannot reverse composed route history");

  auto padded = fixture();
  auto& padded_flight =
      std::get<FreedomFlightSaveDocument>(padded.voyage.voyage.voyage.base);
  for (unsigned i = 0; i < 2500; ++i)
    padded_flight.origin.state.world_deltas.push_back(
        {"codec:" + std::to_string(i) + std::string(100, 'x'),
         SaveWorldDeltaKind::discovered, 1});
  // Each individual flight and the one current voyage fit. The bounded
  // composed receipt duplicates source history, so its COMPLETE save must
  // still refuse above the existing1MiB transaction boundary.
  need(encode_freedom_flight_document_json(padded_flight));
  need(encode_freedom_knowledge_document_json(padded.voyage));
  padded.travel.composed->active = FreedomComposedRouteKind::planet;
  padded.travel.composed->selected_body = planet_target(padded);
  padded.travel.phase = FreedomJumpPhase::spool;
  padded.travel.spool_tick = padded_flight.flight.tick;
  tick(padded, padded.travel.spool_tick + kJumpSpoolTicks);
  commit_body(padded);
  check(validate_freedom_travel_document(padded).has_value() &&
            !encode_freedom_travel_document_json(padded),
        "Otherwise valid composed transaction refuses complete-save byte "
        "overflow");
}
} // namespace
int main() {
  try {
    run();
    std::cout << "PASS composed travel codec " << checks << " checks\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL composed travel codec: " << e.what() << '\n';
    return 1;
  }
}
