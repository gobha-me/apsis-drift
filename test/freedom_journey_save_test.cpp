#include "apsis_drift/native_flight_session.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <numbers>
#include <stdexcept>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
auto check(bool condition, std::string_view message) -> void {
  if (!condition) throw std::runtime_error{std::string{message}};
}
template <class T, class E> auto require(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error{"journey prerequisite refused"};
  return std::move(*value);
}
template <class E> auto require(std::expected<void, E> value) -> void {
  if (!value) throw std::runtime_error{"journey prerequisite refused"};
}
auto contents(const std::filesystem::path& path) -> std::string {
  std::ifstream input{path};
  return {std::istreambuf_iterator<char>{input}, {}};
}
auto current(const NativeFreedomFlightSession& session)
    -> FreedomJourneySaveDocument {
  return {{session.document(), *session.docking()}, *session.walker()};
}
auto terrain_nested_roundtrips() -> void {
  auto old = require(make_freedom_journey_new_game_document(Seed{42}));
  const auto old_text = require(encode_freedom_journey_document_json(old));
  auto selected = old;
  selected.voyage.flight.model.terrain_policy =
      kFreedomLocalDetailTerrainPolicy;
  selected.voyage.flight.world = FreedomActiveWorldSelection{
      1, selected.voyage.flight.flight.frame.system,
      selected.voyage.flight.flight.frame.planet.value()};
  const auto encoded = require(encode_freedom_journey_document_json(selected));
  const auto root = Json::parse(encoded);
  check(root["format_version"] == kFreedomTerrainJourneySaveFormatVersion &&
            require(decode_freedom_journey_document_json(encoded)) ==
                selected &&
            Json::parse(old_text)["format_version"] ==
                kFreedomJourneySaveFormatVersion &&
            require(encode_freedom_journey_document_json(require(
                decode_freedom_journey_document_json(old_text)))) == old_text,
        "station journey33 retains selection while old20 remains byte-stable");
  const auto docking =
      require(encode_freedom_docking_document_json(selected.voyage));
  check(Json::parse(docking)["format_version"] ==
                kFreedomTerrainDockingSaveFormatVersion &&
            require(decode_freedom_docking_document_json(docking)) ==
                selected.voyage,
        "docking32 restores selected flight31 rather than legacy18");
  auto bad = root;
  bad["format_version"] = kFreedomJourneySaveFormatVersion;
  check(!decode_native_save_document_json(bad.dump()),
        "selected station model refuses under historical journey20");
  bad = Json::parse(docking);
  bad["format_version"] = kFreedomDockingSaveFormatVersion;
  check(!decode_native_save_document_json(bad.dump()),
        "selected docking model refuses under historical docking19");
  for (const auto policy :
       {kFreedomLegacyTerrainPolicy, kFreedomLocalDetailTerrainPolicy}) {
    auto flight = selected.voyage.flight;
    flight.model.terrain_policy = policy;
    FreedomSurfaceSaveDocument mismatch{
        flight,
        {true, LandedCraftAnchor{1,
                                 policy == kFreedomLegacyTerrainPolicy
                                     ? kFreedomLocalDetailTerrainPolicy
                                     : kFreedomLegacyTerrainPolicy,
                                 1, 1, flight.flight}}};
    const auto rejected = validate_freedom_surface_document(mismatch);
    check(!rejected &&
              rejected.error().path == "$.surface.landed.terrain_policy",
          "anchor and base terrain policy mismatch refuses before contact "
          "hydration");
  }

  const auto roundtrip = [](const NativeSaveDocument& d) {
    const auto text = require(encode_native_save_document_json(d));
    check(
        require(decode_native_save_document_json(text)) == d,
        "nested native serializer preserves explicit physical terrain choice");
  };
  roundtrip(selected);
  roundtrip(selected.voyage);
  const FreedomStartingAssemblySaveDocument assembly{selected, {}};
  roundtrip(assembly);
  for (const FreedomSurfaceBaseSave& base :
       {FreedomSurfaceBaseSave{selected.voyage.flight},
        FreedomSurfaceBaseSave{selected.voyage},
        FreedomSurfaceBaseSave{selected}, FreedomSurfaceBaseSave{assembly}}) {
    const FreedomSurfaceSaveDocument surface{base, {}};
    roundtrip(surface);
    const auto flight = surface_base_flight(base);
    const FreedomResourceSaveDocument resources{surface,
                                                {1, flight.origin.state.craft,
                                                 flight.flight.craft,
                                                 flight.flight.tick}};
    roundtrip(resources);
    const FreedomKnowledgeSaveDocument knowledge{
        resources, require(make_freedom_starting_knowledge(
                       Seed{42}, kFreedomTravelKnowledgeVersion))};
    roundtrip(knowledge);
    if (std::holds_alternative<FreedomStartingAssemblySaveDocument>(base)) {
      const FreedomTravelSaveDocument travel{
          knowledge, {}, NativeStartingAssemblySelection{}, {}};
      roundtrip(travel);
      const FreedomRecoverySaveDocument recovery{knowledge, {}};
      roundtrip(recovery);
      roundtrip(FreedomSurfaceWalkSaveDocument{recovery, 1, {}});
      roundtrip(FreedomRecoverySaveDocument{travel, {}});
    }
  }
}
auto release_frame_nested_roundtrips() -> void {
  auto selected = require(make_freedom_journey_new_game_document(Seed{42}));
  auto& flight = selected.voyage.flight;
  flight.model.frame_policy = kFreedomReleaseSystemFramePolicy;
  flight.world = FreedomActiveWorldSelection{
      1, flight.flight.frame.system, flight.flight.frame.planet.value()};
  auto invalid = selected;
  invalid.voyage.flight.world.reset();
  check(!validate_freedom_journey_document(invalid),
        "selected station journey requires explicit physical owner");
  invalid = selected;
  invalid.voyage.flight.flight.frame = {
      RigidFrameKind::system_inertial, flight.flight.frame.system, {}, {}};
  check(!validate_freedom_journey_document(invalid),
        "station actor cannot claim a system-frame attached body");
  const auto roundtrip = [](const NativeSaveDocument& document) {
    const auto text = require(encode_native_save_document_json(document));
    check(require(decode_native_save_document_json(text)) == document,
          "complete nested voyage preserves frame selection, actor, assembly, "
          "resources, knowledge and recovery");
  };
  for (const auto policy :
       {kFreedomLegacyTerrainPolicy, kFreedomLocalDetailTerrainPolicy}) {
    flight.model.terrain_policy = policy;
    const auto text = require(encode_freedom_journey_document_json(selected));
    const auto root = Json::parse(text);
    auto mutation = root;
    mutation["flight_model"].erase("terrain_policy");
    check(!decode_freedom_journey_document_json(mutation.dump()),
          "format38 explicitly retains legacy or selected terrain policy");
    mutation = root;
    mutation["flight_model"].erase("frame_policy");
    check(!decode_freedom_journey_document_json(mutation.dump()),
          "format38 requires release frame policy");
    for (const auto legacy : {20U, 33U}) {
      mutation = root;
      mutation["format_version"] = legacy;
      check(!decode_native_save_document_json(mutation.dump()),
            "selected journey cannot be downgraded under historical schemas");
    }
    check(root["format_version"] == kFreedomReleaseJourneySaveFormatVersion &&
              require(decode_freedom_journey_document_json(text)) == selected,
          "format38 retains relative attached body while selecting release "
          "policy");
    roundtrip(selected);
    const FreedomStartingAssemblySaveDocument assembly{selected, {}};
    roundtrip(assembly);
    for (const FreedomSurfaceBaseSave& base :
         {FreedomSurfaceBaseSave{flight},
          FreedomSurfaceBaseSave{selected.voyage},
          FreedomSurfaceBaseSave{selected}, FreedomSurfaceBaseSave{assembly}}) {
      const FreedomSurfaceSaveDocument surface{base, {}};
      roundtrip(surface);
      const FreedomResourceSaveDocument resources{surface,
                                                  {1, flight.origin.state.craft,
                                                   flight.flight.craft,
                                                   flight.flight.tick}};
      roundtrip(resources);
      const FreedomKnowledgeSaveDocument knowledge{
          resources, require(make_freedom_starting_knowledge(
                         Seed{42}, kFreedomTravelKnowledgeVersion))};
      roundtrip(knowledge);
      if (std::holds_alternative<FreedomStartingAssemblySaveDocument>(base)) {
        const FreedomTravelSaveDocument travel{
            knowledge, {}, NativeStartingAssemblySelection{}, {}};
        roundtrip(travel);
        roundtrip(FreedomRecoverySaveDocument{travel, {}});
        const FreedomRecoverySaveDocument recovery{knowledge, {}};
        roundtrip(recovery);
        roundtrip(FreedomSurfaceWalkSaveDocument{recovery, 1, {}});
      }
    }
  }
}
auto invalid() -> void {
  const auto good = require(make_freedom_journey_new_game_document(Seed{42}));
  const auto text = require(encode_freedom_journey_document_json(good));
  auto rejected = [&](FreedomJourneySaveDocument bad) {
    check(!validate_freedom_journey_document(bad) &&
              !encode_freedom_journey_document_json(bad),
          "invalid journey refuses before serialization");
  };
  auto bad = good;
  bad.actor.geometry_version = 2;
  rejected(bad);
  bad = good;
  bad.actor.actor_id = 0;
  rejected(bad);
  bad = good;
  bad.actor.foot_position_metres = {100, 0, 0};
  rejected(bad);
  bad = good;
  bad.actor.foot_position_metres.y = 10;
  rejected(bad);
  bad = good;
  bad.voyage.docking.attached = false;
  rejected(bad);
  bad = good;
  bad.voyage.docking.target.ordinal = 2;
  rejected(bad);
  for (double value : {std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()}) {
    bad = good;
    bad.actor.heading_radians = value;
    rejected(bad);
    bad = good;
    bad.actor.foot_position_metres.x = value;
    rejected(bad);
    bad = good;
    bad.actor.velocity_metres_per_second.z = value;
    rejected(bad);
  }
  const auto root = Json::parse(text);
  auto quoted = root;
  quoted["application_version"] =
      std::string{"\"\\{[}]"} + std::string(50, '{');
  check(
      require(decode_freedom_journey_document_json(quoted.dump())) == good,
      "quoted delimiters and escaped quotes/backslashes are not JSON nesting");
  auto deeply_nested = text;
  const std::string flight_prefix{"\"flight\": {"};
  const auto flight_key = deeply_nested.find(flight_prefix);
  check(flight_key != std::string::npos, "flight fixture field present");
  deeply_nested.insert(flight_key + flight_prefix.size(),
                       "\"unknown\":" + std::string(100000, '[') + "0" +
                           std::string(100000, ']') + ",");
  check(!decode_freedom_journey_document_json(deeply_nested),
        "deep unknown array refuses before JSON tree allocation or recursive "
        "dump");
  const auto nested_path =
      std::filesystem::temp_directory_path() /
      ("apsis-journey-deep-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".json");
  {
    std::ofstream output{nested_path};
    output << deeply_nested;
  }
  check(!load_native_save_file(nested_path) &&
            contents(nested_path) == deeply_nested,
        "native load rejects deep unknown array without fallback or source "
        "mutation");
  std::filesystem::remove(nested_path);
  for (const auto malformed : {"{]", "[{]}", "[[0]", "\"unterminated\\"})
    check(!decode_freedom_journey_document_json(malformed),
          "unbalanced nesting or unterminated escaped string refuses");
  const auto at_limit = decode_freedom_journey_document_json(
      std::string(64, '[') + "0" + std::string(64, ']'));
  const auto over_limit = decode_freedom_journey_document_json(
      std::string(65, '[') + "0" + std::string(65, ']'));
  check(!at_limit &&
            at_limit.error().code == SaveSchemaErrorCode::invalid_state &&
            !over_limit &&
            over_limit.error().code == SaveSchemaErrorCode::malformed_json,
        "64-level boundary reaches schema validation and level65 refuses "
        "lexical preflight");
  auto duplicated_actor = text;
  const auto actor_key = duplicated_actor.find("\"actor_id\":");
  check(actor_key != std::string::npos, "actor identity fixture field present");
  duplicated_actor.insert(actor_key, "\"actor_id\": \"1\", ");
  const auto duplicate = decode_freedom_journey_document_json(duplicated_actor);
  check(!duplicate &&
            duplicate.error().code == SaveSchemaErrorCode::duplicate_key,
        "nested actor duplicate key refuses before delegated dumping");
  std::array<Json, 15> mutations;
  mutations.fill(root);
  mutations[0]["actor"]["extra"] = true;
  mutations[1]["actor"].erase("frame");
  mutations[2]["actor"]["geometry_version"] = 1.0;
  mutations[3]["actor"]["actor_id"] = "01";
  mutations[4]["actor"]["actor_id"] = "18446744073709551616";
  mutations[5]["actor"]["actor_id"] = 1;
  mutations[6]["actor"]["frame"]["station_id"] = "0";
  mutations[7]["actor"]["frame"]["kind"] = "planet_relative";
  mutations[8]["actor"]["frame"]["tick"] = "0";
  mutations[9]["actor"]["foot_position_metres"] = Json::array({0, 0});
  mutations[10]["actor"]["velocity_metres_per_second"] =
      Json::array({0, 0, "0"});
  mutations[11]["actor"]["heading_radians"] = nullptr;
  mutations[12]["state"]["location"] = "docked_at_origin_port";
  mutations[13]["docking"]["attached"] = false;
  mutations[14]["flight_model"]["extra"] = 1;
  for (const auto& value : mutations)
    check(!decode_freedom_journey_document_json(value.dump()),
          "closed actor/shared voyage schema rejects corruption");
  check(!decode_freedom_journey_document_json(
            "{\"format_version\":20,\"format_version\":19}") &&
            !decode_freedom_journey_document_json("{bad") &&
            !decode_freedom_journey_document_json(
                std::string(kMaximumSaveDocumentBytes + 1, ' ')),
        "duplicates/malformed/oversize reject before hydration");
  auto session = require(
      NativeFreedomFlightSession::open(require(native_new_game(Seed{42}))));
  const auto before = current(session);
  check(!session.advance({}) && !session.release_port() &&
            !session.capture_port() && !session.select_port(2) &&
            !session.set_assistance(false) && !session.set_hold({}) &&
            current(session) == before,
        "outside craft cannot operate flight or change tick");
  for (const auto controls :
       {OriginWalkControls{2, 0, 0}, OriginWalkControls{0, -2, 0},
        OriginWalkControls{0, 0, std::numeric_limits<double>::quiet_NaN()}})
    check(!session.advance_walk(controls) && current(session) == before,
          "invalid walk command atomic");
  for (const auto step :
       {SimulationSeconds{0}, SimulationSeconds{1},
        SimulationSeconds{std::numeric_limits<double>::infinity()}})
    check(!session.advance_walk({}, step) && current(session) == before,
          "invalid walk cadence atomic");
  auto terminal = good;
  auto& flight = terminal.voyage.flight;
  flight.flight.tick = std::numeric_limits<SimulationTick>::max() - 2;
  flight.origin.state.tick = flight.flight.tick;
  const auto physical = require(
      generate_physical_origin_system(Seed{42}, flight.model.physical_catalog));
  const auto station = generate_origin_station(Seed{42});
  const auto geometry = require(origin_station_geometry(station));
  flight.flight = require(release_origin_port(
      physical, station, geometry,
      {1, {station.id, 1}, wayfarer_frame().recipe, flight.flight.tick}));
  const auto home =
      physical.catalog.planets[kOriginHomePlanetOrdinal].descriptor;
  auto stopped = require(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom, terminal, home, {}}));
  check(!stopped.advance_walk({0, -1, 0}) && current(stopped) == terminal,
        "terminal shared clock refuses without actor mutation");
}
auto roundtrip() -> void {
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("apsis-journey-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(directory);
  for (auto seed :
       {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}}) {
    auto selected = require(native_new_game(seed));
    const auto fresh =
        std::get<FreedomStartingAssemblySaveDocument>(
            std::get<FreedomKnowledgeSaveDocument>(selected.document)
                .voyage.voyage.base)
            .journey;
    const OriginWalkerState spawn{1, 1, {}, {}, std::numbers::pi / 2.0};
    check(fresh.actor == spawn && fresh.voyage.docking.attached &&
              fresh.voyage.flight.flight.tick == 0 &&
              fresh.voyage.flight.origin.recipe.universe_seed == seed &&
              !selected.source_save,
          "fresh New Game supported hub with same-seed attached Wayfarer");
    auto oriented = require(NativeFreedomFlightSession::open(selected));
    require(oriented.advance_walk({1, 0, fresh.actor.heading_radians}));
    check(oriented.walker()->foot_position_metres.x < 0 &&
              std::abs(oriented.walker()->foot_position_metres.z) < 1e-12 &&
              oriented.document().flight.tick == 1 &&
              fresh.voyage.flight.flight.tick == 0,
          "fresh heading faces the D1 route and forward movement owns exactly "
          "one tick");
    const auto format17 =
        require(encode_freedom_save_document_json(fresh.voyage.flight.origin));
    const auto format18 =
        require(encode_freedom_flight_document_json(fresh.voyage.flight));
    const auto format19 =
        require(encode_freedom_docking_document_json(fresh.voyage));
    const auto text = require(encode_freedom_journey_document_json(fresh));
    check(require(decode_freedom_journey_document_json(text)) == fresh &&
              require(encode_freedom_save_document_json(
                  fresh.voyage.flight.origin)) == format17 &&
              require(encode_freedom_flight_document_json(
                  fresh.voyage.flight)) == format18 &&
              require(encode_freedom_docking_document_json(fresh.voyage)) ==
                  format19,
          "format20 roundtrip leaves existing representations intact");
    const auto source = directory / "source.json";
    require(write_freedom_journey_file_atomically(source, fresh));
    const auto original = contents(source);
    auto session = require(
        NativeFreedomFlightSession::open(require(native_continue(source))));
    auto docked =
        require(NativeFreedomFlightSession::open({NativeStartup::Mode::freedom,
                                                  fresh.voyage,
                                                  selected.home_planet,
                                                  {}}));
    for (int n = 0; n < 240; ++n) {
      const auto walked_step = require(session.advance_walk({0, -1, 0}));
      const auto docked_step = require(docked.advance({}));
      check(
          session.document() == docked.document() &&
              session.docking() == docked.docking() &&
              require(session.observe()) == require(docked.observe()) &&
              walked_step.actuation.central.propulsion.applied_force_newtons ==
                  docked_step.actuation.central.propulsion
                      .applied_force_newtons &&
              walked_step.actuation.central.propulsion
                      .applied_torque_newton_metres ==
                  docked_step.actuation.central.propulsion
                      .applied_torque_newton_metres &&
              walked_step.applied_hold_force_body_newtons ==
                  docked_step.applied_hold_force_body_newtons,
          "walking and actorless docked ticks have identical craft, clock "
          "and observed applied actuation");
      check(session.walker() && !docked.walker() &&
                session.walker()->actor_id == fresh.actor.actor_id &&
                session.walker()->geometry_version ==
                    fresh.actor.geometry_version,
            "successful walking tick retains its supported actor identity");
    }
    const auto walked = current(session);
    check(walked.actor.foot_position_metres.x < -3.9 &&
              walked.voyage.flight.flight.tick == 240 &&
              walked.voyage.flight.origin.state.tick == 240 &&
              session.source_save() == source,
          "walking advances actor and constrained craft on sole shared clock");
    auto remembered = walked;
    remembered.voyage.flight.origin.state.discoveries.push_back(
        {SurfaceSignalId{77}, 8});
    remembered.voyage.flight.origin.state.world_deltas.push_back(
        {"signal:77", SaveWorldDeltaKind::discovered, 9});
    const auto history_path = directory / "history.json";
    require(write_freedom_journey_file_atomically(history_path, remembered));
    auto historical = require(NativeFreedomFlightSession::open(
        require(native_continue(history_path))));
    require(historical.advance_walk({}));
    check(historical.document().origin.state.discoveries ==
                  remembered.voyage.flight.origin.state.discoveries &&
              historical.document().origin.state.world_deltas ==
                  remembered.voyage.flight.origin.state.world_deltas,
          "walk continuation preserves mutable survey/world history");
    const auto destination = directory / "copy.json";
    require(session.save_as(destination));
    auto continued = require(NativeFreedomFlightSession::open(
        require(native_continue(destination))));
    check(
        current(continued) == walked && contents(source) == original &&
            session.source_save() == source,
        "Save As/Continue preserves motion/history/clock and source identity");
    for (int n = 0; n < 120; ++n) {
      require(session.advance_walk({0, 1, .1}));
      require(continued.advance_walk({0, 1, .1}));
    }
    check(current(session) == current(continued),
          "continued replay identical after persisted moving actor");
    auto bad = walked;
    bad.actor.geometry_version = 2;
    const auto previous = contents(destination);
    check(!write_freedom_journey_file_atomically(destination, bad) &&
              contents(destination) == previous &&
              !session.save_as("relative.json") && contents(source) == original,
          "bad writer/destination preserves files");
    auto broken = Json::parse(original);
    broken["actor"]["frame"]["station_id"] = "0";
    const auto rejected = directory / "bad.json";
    {
      std::ofstream output{rejected};
      output << broken.dump();
    }
    check(!load_native_save_file(rejected) &&
              contents(rejected) == broken.dump(),
          "corrupt format20 cannot fall back to old formats or rewrite source");
  }
  auto a = require(
      NativeFreedomFlightSession::open(require(native_new_game(Seed{42}))));
  auto b = a;
  for (int n = 0; n < 1920; ++n)
    require(a.advance_walk({0, n < 960 ? -1.0 : 1.0, 0}));
  for (int frame = 0; frame < 960; ++frame)
    for (int tick = 0; tick < 2; ++tick)
      require(b.advance_walk({0, frame < 480 ? -1.0 : 1.0, 0}));
  check(current(a) == current(b),
        "120Hz and two-tick presentation cadence identical");
  check(std::abs(a.walker()->foot_position_metres.x) < 1e-10 &&
            a.document().flight.tick == 1920,
        "actual supported left/right route returns to hub without relocating "
        "actor");
  std::filesystem::remove_all(directory);
}
} // namespace
int main() {
  try {
    invalid();
    release_frame_nested_roundtrips();
    terrain_nested_roundtrips();
    roundtrip();
    std::cout << "Freedom journey save contract passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
