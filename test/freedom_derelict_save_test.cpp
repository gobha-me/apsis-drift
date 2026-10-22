#include "apsis_drift/freedom_recovery_save.hpp"
#include "apsis_drift/native_startup.hpp"
#include "apsis_drift/physical_minor.hpp"
#include "apsis_drift/physical_moon.hpp"

#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
int checks{}, failures{};
auto check(bool value, std::string_view message) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) {
    if constexpr (std::is_same_v<E, SaveSchemaError>)
      throw std::runtime_error(value.error().detail);
    else
      throw std::runtime_error("Derelict save fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto flight(FreedomKnowledgeSaveDocument& d) -> FreedomFlightSaveDocument& {
  return std::get<FreedomStartingAssemblySaveDocument>(d.voyage.voyage.base)
      .journey.voyage.flight;
}
auto fixture() -> FreedomKnowledgeSaveDocument {
  auto start = need(native_uncharted_network_new_game(Seed{42}));
  auto result =
      std::get<FreedomKnowledgeSaveDocument>(std::move(start.document));
  flight(result).origin.derelicts = FreedomDerelictSelection{};
  result.knowledge.derelicts = FreedomDerelictState{};
  return result;
}
auto observation(StarterCraftId craft, SimulationTick tick)
    -> FreedomDerelictObservation {
  const auto descriptor = need(generate_freedom_derelict(Seed{42}));
  const auto body = need(resolve_freedom_derelict(descriptor, tick));
  return {craft,
          tick,
          {body.position_metres.x + 200, body.position_metres.y,
           body.position_metres.z},
          body.linear_velocity_metres_per_second};
}
auto origin_and_envelopes() -> void {
  auto selected = fixture();
  auto& f = flight(selected);
  const auto bytes = need(encode_freedom_knowledge_document_json(selected));
  check(need(decode_freedom_knowledge_document_json(bytes)) == selected,
        "Selected empty metadata survives complete station/assembly envelopes");
  const auto origin = need(encode_freedom_save_document_json(f.origin));
  const auto encoded = Json::parse(origin);
  check(encoded["format_version"] == kFreedomDerelictSaveFormatVersion &&
            need(decode_freedom_save_document_json(origin)) == f.origin,
        "Explicit origin60 retains its network2 selection");
  for (unsigned fault = 0; fault < 6; ++fault) {
    auto bad = encoded;
    switch (fault) {
      case 0: bad["format_version"] = kFreedomNetworkSaveFormatVersion; break;
      case 1: bad.erase("derelicts"); break;
      case 2: bad["network"]["version"] = 1U; break;
      case 3: bad["derelicts"]["version"] = std::uint64_t{1} << 40; break;
      case 4: bad["derelicts"]["extra"] = 0U; break;
      case 5: bad["derelicts"] = nullptr; break;
      default: break;
    }
    check(!decode_freedom_save_document_json(bad.dump()),
          "Origin selection refuses old formats, omission, foreign network and "
          "malformed versions");
  }
  auto mismatched = selected;
  mismatched.knowledge.derelicts.reset();
  check(!validate_freedom_knowledge_document(mismatched),
        "Selected origin cannot lose knowledge selection");
  mismatched = selected;
  flight(mismatched).origin.derelicts.reset();
  check(!validate_freedom_knowledge_document(mismatched),
        "Knowledge selection cannot appear without its origin owner");
  f.origin.derelicts.reset();
  selected.knowledge.derelicts.reset();
  const auto absent = need(encode_freedom_knowledge_document_json(selected));
  auto baseline = need(native_uncharted_network_new_game(Seed{42}));
  auto& historical = std::get<FreedomKnowledgeSaveDocument>(baseline.document);
  flight(historical).origin.derelicts.reset();
  historical.knowledge.derelicts.reset();
  check(absent == need(encode_freedom_knowledge_document_json(historical)) &&
            need(encode_freedom_knowledge_document_json(need(
                decode_freedom_knowledge_document_json(absent)))) == absent,
        "Unselected historical chart6 keeps absent fields and exact Continue "
        "bytes");

  // Saved-space codec fixtures only; no companion flight or landing claimed.
  const auto system = need(generate_physical_origin_system(Seed{42}, 2));
  const auto catalog = need(generate_secondary_body_catalog(system));
  const auto moon = need(
      generate_physical_moon_descriptor(catalog, catalog.bodies[0].node.id));
  const auto minor = need(
      generate_physical_minor_descriptor(catalog, catalog.bodies[1].node.id));
  for (const bool lunar : {true, false}) {
    auto body = f;
    body.origin.derelicts = FreedomDerelictSelection{};
    body.world.reset();
    body.model = {};
    body.model.physical_catalog = 2;
    body.model.physical_ephemeris = 2;
    const auto id = lunar ? moon.body : minor.body;
    if (lunar)
      body.moon = FreedomMoonWorldSelection{1, {system.catalog.id, id}};
    else
      body.minor = FreedomMinorWorldSelection{1, {system.catalog.id, id}};
    body.flight.frame = {
        RigidFrameKind::body_relative_inertial, system.catalog.id, {}, {}, id};
    body.flight.position_metres = {
        (lunar ? moon.radius_metres : minor.radius_metres) + 50000.0, 100, 0};
    body.flight.linear_velocity_metres_per_second = {0, 0, 100};
    const auto text = need(encode_freedom_flight_document_json(body));
    check(need(decode_freedom_flight_document_json(text)) == body,
          "Moon54/minor55 restore explicit origin60 without a planet alias");
  }
}
auto metadata() -> void {
  auto k = need(make_freedom_starting_knowledge(
      Seed{42}, kFreedomUnchartedKnowledgeVersion));
  const auto craft = need(derive_freedom_craft_identity(Seed{42}, 0));
  k.derelicts =
      FreedomDerelictState{1, observation(craft, 1), observation(craft, 2)};
  const auto text = need(encode_freedom_knowledge_json(k, 3));
  check(need(decode_freedom_knowledge_json(text, 3)) == k,
        "Generated contact/inspection metadata roundtrips exactly");
  for (unsigned fault = 0; fault < 9; ++fault) {
    auto bad = Json::parse(text);
    auto& state = bad["derelicts"];
    switch (fault) {
      case 0: state["extra"] = 0U; break;
      case 1: state["version"] = std::uint64_t{1} << 40; break;
      case 2: state["contact"]["craft"] = -1; break;
      case 3: state["contact"]["tick"] = 4U; break;
      case 4:
        state["inspection"]["position_metres"] = Json::array({0, 0});
        break;
      case 5:
        state["inspection"]["velocity_metres_per_second"][0] = nullptr;
        break;
      case 6:
        state["contact"]["craft"] =
            need(derive_freedom_craft_identity(Seed{42}, 1)).value;
        break;
      case 7: state["contact"] = nullptr; break;
      case 8:
        state["inspection"]["position_metres"][0] =
            std::numeric_limits<double>::infinity();
        break;
      default: break;
    }
    check(!decode_freedom_knowledge_json(bad.dump(), 3),
          "Closed observation codec refuses wide, nonunsigned, future, foreign "
          "and nonfinite input");
  }
  auto old = need(make_freedom_starting_knowledge(
      Seed{42}, kFreedomPlanetaryKnowledgeVersion));
  old.derelicts = FreedomDerelictState{};
  check(!encode_freedom_knowledge_json(old, 3),
        "Old chart recipes cannot acquire a derelict selection implicitly");
  for (const auto value : {std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
    auto bad = k;
    bad.derelicts->inspection->position_metres.x = value;
    check(!encode_freedom_knowledge_json(bad, 3),
          "Nonfinite in-memory observations refuse before JSON conversion");
  }
}
auto recovery() -> void {
  auto d = fixture();
  auto& f = flight(d);
  const auto craft = f.origin.state.craft;
  // Historical observation fixture plus the real D1 constraint at its clock.
  const auto owner = need(generate_physical_origin_system(Seed{42}, 2));
  const auto station = generate_origin_station(Seed{42});
  const auto geometry = need(origin_station_geometry(station));
  f.flight = need(release_origin_port(
      owner, station, geometry,
      {geometry.version, {station.id, 1}, f.flight.craft, 3}));
  f.origin.state.tick = 3;
  d.voyage.resources.tick = 3;
  d.knowledge.derelicts->contact = observation(craft, 1);
  FreedomRecoverySaveDocument source{d, {}};
  const auto pending = need(record_freedom_loss(
      source, FreedomLossCause::irrecoverable_destruction, craft, 3,
      need(freedom_recovery_source_checksum(source.voyage))));
  const auto replaced = need(complete_freedom_recovery(pending));
  check(recovery_knowledge(replaced.voyage).knowledge.derelicts ==
                d.knowledge.derelicts &&
            recovery_flight(replaced.voyage).origin.derelicts ==
                f.origin.derelicts &&
            need(decode_freedom_recovery_document_json(need(
                encode_freedom_recovery_document_json(replaced)))) == replaced,
        "Recovery keeps exact contact and world selection through Continue");
  const auto rejects_valid_leaf = [&](const auto& candidate,
                                      std::string_view message) {
    need(validate_freedom_knowledge_document(
        std::get<FreedomKnowledgeSaveDocument>(candidate.voyage)));
    check(!validate_freedom_recovery_document(candidate), message);
  };
  auto modified = replaced;
  auto& leaf = std::get<FreedomKnowledgeSaveDocument>(modified.voyage);
  leaf.knowledge.derelicts->contact->position_metres.x += 1;
  rejects_valid_leaf(
      modified,
      "Valid replacement metadata cannot rewrite the retained contact");
  modified = replaced;
  const auto new_craft = recovery_flight(modified.voyage).origin.state.craft;
  leaf.knowledge.derelicts->inspection = observation(new_craft, 3);
  need(validate_freedom_recovery_document(modified));
  leaf.knowledge.derelicts->inspection = observation(new_craft, 2);
  rejects_valid_leaf(modified,
                     "New inspection cannot predate the loss boundary");
  leaf.knowledge.derelicts->inspection = observation(craft, 3);
  rejects_valid_leaf(modified,
                     "A retired craft cannot supply a new inspection");
  modified = replaced;
  leaf.knowledge.derelicts.reset();
  flight(leaf).origin.derelicts.reset();
  rejects_valid_leaf(
      modified,
      "Replacement cannot discard the explicit selection and notes together");
  auto historical = d;
  flight(historical).origin.derelicts.reset();
  historical.knowledge.derelicts.reset();
  FreedomRecoverySaveDocument absent{std::move(historical), {}};
  const auto absent_loss = need(record_freedom_loss(
      absent, FreedomLossCause::irrecoverable_destruction, craft, 3,
      need(freedom_recovery_source_checksum(absent.voyage))));
  auto adopted = need(complete_freedom_recovery(absent_loss));
  auto& adopted_leaf = std::get<FreedomKnowledgeSaveDocument>(adopted.voyage);
  flight(adopted_leaf).origin.derelicts = FreedomDerelictSelection{};
  adopted_leaf.knowledge.derelicts = FreedomDerelictState{};
  rejects_valid_leaf(adopted,
                     "Recovery cannot opt a historical journey into derelicts");
  d.knowledge.derelicts->inspection = observation(craft, 2);
  FreedomRecoverySaveDocument inspected{d, {}};
  const auto inspected_loss = need(record_freedom_loss(
      inspected, FreedomLossCause::irrecoverable_destruction, craft, 3,
      need(freedom_recovery_source_checksum(inspected.voyage))));
  auto inspected_replacement = need(complete_freedom_recovery(inspected_loss));
  auto& retained =
      std::get<FreedomKnowledgeSaveDocument>(inspected_replacement.voyage)
          .knowledge.derelicts;
  check(retained == d.knowledge.derelicts,
        "Recovery retains both original contact and inspection byte values");
  retained->inspection->velocity_metres_per_second.x += .01;
  rejects_valid_leaf(
      inspected_replacement,
      "Otherwise valid velocity metadata cannot rewrite a retained inspection");
}
} // namespace
auto main() -> int {
  try {
    origin_and_envelopes();
    metadata();
    recovery();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "Freedom derelict save: " << checks << " checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
