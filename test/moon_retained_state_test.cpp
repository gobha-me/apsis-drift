#include "apsis_drift/craft_condition.hpp"
#include "apsis_drift/freedom_local_jump.hpp"
#include "apsis_drift/freedom_recovery_save.hpp"
#include "apsis_drift/station_geometry.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace apsis_drift;
auto check(bool good, const char* message) -> void {
  if (!good) throw std::runtime_error(message);
}
template <class T, class E> auto need(std::expected<T, E> result) -> T {
  if (!result) {
    if constexpr (requires { result.error().detail; })
      std::cerr << result.error().detail << '\n';
    throw std::runtime_error("Moon retained-state fixture refused");
  }
  return std::move(*result);
}
auto run() -> void {
  const Seed seed{42};
  const auto assembly =
      need(make_freedom_starting_assembly_new_game_document(seed));
  const auto assembly_bytes =
      need(encode_freedom_starting_assembly_document_json(assembly));
  auto home = assembly.journey.voyage.flight;
  const auto physical = need(generate_physical_origin_system(seed, 2));
  const auto station = generate_origin_station(seed);
  const auto geometry = need(origin_station_geometry(station));
  home.flight = need(resolve_origin_port_pose(physical, station, geometry,
                                              {station.id, 1}, 1200, 12))
                    .planet_relative;
  home.flight.craft = assembly.journey.voyage.flight.flight.craft;
  home.origin.state.tick = home.flight.tick;
  home.world = FreedomActiveWorldSelection{1, physical.catalog.id,
                                           home.origin.recipe.home_planet};
  home.model.terrain_policy = 3;
  home.model.optics = 1;
  home.model.frame_policy = 2;
  home.model.companions = 1;
  check(hydrate_freedom_flight_document(home).has_value(),
        "Actual selected home profile validates");

  auto next = home;
  ++next.flight.tick;
  next.origin.state.tick = next.flight.tick;
  // Serializer/reducer-binding fixture, as in craft_condition_test: this is
  // retained metadata, not a claim that the port pose physically collided.
  TerrainImpactWitness witness;
  witness.terrain_policy = home.model.terrain_policy;
  witness.craft = home.origin.state.craft;
  witness.craft_frame = home.flight.craft;
  witness.system = home.flight.frame.system;
  witness.planet = *home.flight.frame.planet;
  witness.tick = next.flight.tick;
  witness.source_checksum = need(
      rigid_body_state_checksum(RigidBodyWorldContext{physical}, home.flight));
  witness.candidate_checksum = need(
      rigid_body_state_checksum(RigidBodyWorldContext{physical}, next.flight));
  witness.fraction = .5;
  witness.triangle.planet = witness.planet;
  witness.region = CraftImpactRegion::forebody;
  witness.outward_normal_planet_fixed = {1, 0, 0};
  witness.pre_response_velocity_planet_fixed = {-6, 0, 0};
  witness.closing_speed_metres_per_second = 6;
  const auto prior =
      need(reduce_craft_condition(need(make_craft_condition(home)), home, next,
                                  {1, TerrainImpactClass::impact, 0, witness}))
          .condition;
  check(validate_craft_condition(prior, next).has_value(),
        "Planetary receipt authenticates its actual original owner");

  const auto catalog = need(generate_secondary_body_catalog(physical));
  const auto frozen = need(freeze_local_body_jump_arrival(
      {1,
       next,
       {physical.catalog.id, catalog.bodies[0].node.id},
       1,
       next.model}));
  const auto& flight = frozen.arrival;
  const auto moon = need(hydrate_freedom_moon_flight_document(flight)).moon;
  FreedomResources resources{1,
                             flight.origin.state.craft,
                             flight.flight.craft,
                             flight.flight.tick,
                             kFreedomFlightCapacityQuanta - 123456,
                             2};
  auto knowledge = need(
      make_freedom_starting_knowledge(seed, kFreedomTravelKnowledgeVersion));
  const auto fact_bytes =
      need(encode_freedom_knowledge_json(knowledge, flight.flight.tick));
  FreedomSurfaceSaveDocument surface;
  surface.base = flight;
  FreedomResourceSaveDocument resource;
  resource.voyage = std::move(surface);
  resource.resources = resources;
  FreedomKnowledgeSaveDocument retained;
  retained.voyage = std::move(resource);
  retained.knowledge = knowledge;
  const auto encoded = need(encode_freedom_knowledge_document_json(retained));
  const auto continued = need(decode_freedom_knowledge_document_json(encoded));
  check(
      continued == retained &&
          need(encode_freedom_knowledge_document_json(continued)) == encoded &&
          continued.voyage.resources == resources &&
          need(encode_freedom_knowledge_json(continued.knowledge,
                                             flight.flight.tick)) == fact_bytes,
      "Actual Moon Continue retains resources and existing fact provenance "
      "exactly");
  check(need(encode_freedom_starting_assembly_document_json(assembly)) ==
            assembly_bytes,
        "Pure local source/arrival work leaves genuine authored assembly "
        "untouched");
  const auto checksum = need(
      rigid_body_state_checksum(RigidBodyWorldContext{moon}, flight.flight));
  check(
      need(freedom_recovery_source_checksum(retained)) == checksum &&
          !rigid_body_state_checksum(RigidBodyWorldContext{physical},
                                     flight.flight),
      "Loss checksum authenticates actual Moon context, never parent PlanetId");
  check(!validate_freedom_recovery_document({retained, {}}),
        "Bare Moon knowledge cannot infer a retained starter assembly binding");

  auto condition = prior;
  condition.tick = flight.flight.tick;
  check(validate_craft_condition(condition, flight).has_value() &&
            condition.latest == prior.latest &&
            condition.latest->witness.terrain_policy == 3 &&
            flight.model.terrain_policy == 1 &&
            need(decode_craft_condition_json(
                need(encode_craft_condition_json(condition)))) == condition,
        "Moon recipe never reinterprets a retained original policy3 planet "
        "receipt");
  auto changed_planet = next;
  changed_planet.model.terrain_policy = 2;
  check(hydrate_freedom_flight_document(changed_planet).has_value() &&
            !validate_craft_condition(prior, changed_planet),
        "Historical planet terrain-selection equality remains exact");
  auto wrong = condition;
  wrong.latest->witness.planet.value = moon.body.value;
  wrong.latest->witness.triangle.planet = wrong.latest->witness.planet;
  check(!validate_craft_condition(wrong, flight),
        "Typed Moon identity cannot masquerade as retained PlanetId witness");
  wrong = condition;
  wrong.latest->witness.terrain_policy = 4;
  check(!validate_craft_condition(wrong, flight),
        "Unsupported retained planetary recipe still refuses");
  wrong = condition;
  ++wrong.tick;
  check(!validate_craft_condition(wrong, flight),
        "Moon does not loosen retained condition authoritative clock");
  auto invalid = retained;
  invalid.knowledge.recipe.version = kFreedomObservedKnowledgeVersion;
  invalid.knowledge.recipe.world_domain = 0;
  check(!validate_freedom_knowledge_document(invalid),
        "Actual Moon requires explicitly selected travel knowledge domain");
  invalid = retained;
  ++invalid.voyage.resources.tick;
  check(!validate_freedom_knowledge_document(invalid),
        "Moon resource ownership remains tied to the sole flight clock");
}
} // namespace
auto main() -> int {
  try {
    run();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "Moon retained-state contracts passed\n";
}
