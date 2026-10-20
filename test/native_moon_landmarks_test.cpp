#include "apsis_drift/native_flight_session.hpp"
#include "moon_contact_fixture.hpp"
#include "native_minor_route_fixture.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <numbers>
#include <stdexcept>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
using minor_route_fixture::bytes;
using minor_route_fixture::continued;
using minor_route_fixture::need;
unsigned checks{};
auto check(bool good, const char* message) -> void {
  ++checks;
  if (!good) throw std::runtime_error(message);
}
// Identity-only bounded shell corpus, never terrain/site/actor relocation or a
// claim that these records were acquired on the physical voyage. This gives
// independently authenticated IDs for closed-schema and shared-cap tests.
auto identities(const PhysicalMoonDescriptor& moon)
    -> std::vector<FreedomMoonSurfaceLandmarkInspection> {
  std::vector<FreedomMoonSurfaceLandmarkInspection> result;
  for (unsigned i = 0; i < 512 && result.size() < 65; ++i) {
    const double angle = 2 * std::numbers::pi * i / 512;
    const SurfaceLandmarkCell base{
        static_cast<std::int32_t>(
            std::floor(moon.radius_metres * std::cos(angle) / 512)),
        static_cast<std::int32_t>(
            std::floor(moon.radius_metres * std::sin(angle) / 512)),
        0};
    for (std::int32_t x = -1; x <= 1; ++x)
      for (std::int32_t y = -1; y <= 1; ++y)
        for (std::int32_t z = -1; z <= 1; ++z) {
          const SurfaceLandmarkCell cell{base.x + x, base.y + y, base.z + z};
          const auto id = need(identify_moon_surface_landmark(moon, cell));
          if (!id || result.size() == 65 ||
              std::ranges::any_of(
                  result, [&](const auto& old) { return old.site == id->id; }))
            continue;
          result.push_back(
              {{moon.catalog.owner.catalog.id, moon.body}, cell, id->id, 1, 7});
        }
  }
  check(result.size() == 65,
        "Bounded identity corpus supplies distinct Moon records");
  std::ranges::sort(result, {},
                    [](const auto& item) { return item.site.value; });
  return result;
}
auto planet_record(const PhysicalLocalSystem& system)
    -> FreedomSurfaceLandmarkInspection {
  const auto& planet = system.catalog.planets.front().descriptor;
  const auto base = static_cast<std::int32_t>(
      std::floor(static_cast<double>(planet.radius.value) * 1000 / 512));
  for (std::int32_t x = -1; x <= 1; ++x)
    for (std::int32_t y = -1; y <= 1; ++y)
      for (std::int32_t z = -1; z <= 1; ++z) {
        const SurfaceLandmarkCell cell{base + x, y, z};
        const auto identity = need(identify_surface_landmark(planet, cell));
        if (identity)
          return {system.catalog.id, planet.id, cell, identity->id, 3, 7};
      }
  throw std::runtime_error("Bounded planetary identity fixture unavailable");
}
auto metadata(const PhysicalMoonDescriptor& moon,
              const std::vector<FreedomMoonSurfaceLandmarkInspection>& records)
    -> void {
  for (const auto version :
       {kFreedomKnowledgeVersion, kFreedomObservedKnowledgeVersion,
        kFreedomTravelKnowledgeVersion}) {
    auto k = need(make_freedom_starting_knowledge(Seed{42}, version));
    const auto old = need(encode_freedom_knowledge_json(k, 7));
    check(!Json::parse(old).contains("moon_surface_landmarks") &&
              need(encode_freedom_knowledge_json(
                  need(decode_freedom_knowledge_json(old, 7)), 7)) == old,
          "Historical/default knowledge remains Moon-component absent and "
          "byte-exact");
    k.moon_surface_landmarks = FreedomMoonSurfaceLandmarkState{};
    if (version == kFreedomKnowledgeVersion) {
      check(!validate_freedom_knowledge(k, 7),
            "Chart-only knowledge cannot select Moon inspections");
      auto raw = Json::parse(old);
      raw["moon_surface_landmarks"] = {{"version", 1U},
                                       {"inspections", Json::array()}};
      check(!decode_freedom_knowledge_json(raw.dump(), 7),
            "Chart-only JSON refuses Moon selection");
      continue;
    }
    const auto empty = need(encode_freedom_knowledge_json(k, 7));
    check(need(decode_freedom_knowledge_json(empty, 7)) == k,
          "Explicit empty Moon selection survives exactly");
    k.moon_surface_landmarks->inspections = {records.front(), records[1]};
    const auto facts = k.entries;
    const auto encoded = need(encode_freedom_knowledge_json(k, 7));
    check(need(decode_freedom_knowledge_json(encoded, 7)) == k &&
              k.entries == facts,
          "Typed Moon inspections roundtrip without adding fact history");
  }
  auto selected = need(make_freedom_starting_knowledge(
      Seed{42}, kFreedomTravelKnowledgeVersion));
  selected.moon_surface_landmarks =
      FreedomMoonSurfaceLandmarkState{1, {records.front()}};
  const auto canonical = need(encode_freedom_knowledge_json(selected, 7));
  for (unsigned fault = 0; fault < 12; ++fault) {
    auto bad = selected;
    auto& state = *bad.moon_surface_landmarks;
    auto& record = state.inspections.front();
    switch (fault) {
      case 0: state.version = 2; break;
      case 1: record.target.system = {}; break;
      case 2: record.target.body.version = 2; break;
      case 3: record.target.body.kind = BodyKind::minor; break;
      case 4: ++record.target.body.value; break;
      case 5: record.tick = 8; break;
      case 6: record.terrain_recipe = 0; break;
      case 7: record.terrain_recipe = 2; break;
      case 8: ++record.site.value; break;
      case 9: record.cell.x = std::numeric_limits<std::int32_t>::max(); break;
      case 10: state.inspections.push_back(record); break;
      case 11: state.inspections.assign(records.begin(), records.end()); break;
    }
    check(!validate_freedom_knowledge(bad, 7) &&
              !encode_freedom_knowledge_json(bad, 7),
          "Wrong Moon identity/recipe/tick/duplicate/oversize record refuses");
  }
  for (unsigned fault = 0; fault < 10; ++fault) {
    auto raw = Json::parse(canonical);
    auto& state = raw["moon_surface_landmarks"];
    auto& item = state["inspections"][0];
    switch (fault) {
      case 0: state["extra"] = 0; break;
      case 1: state["version"] = 2U; break;
      case 2: item["extra"] = 0; break;
      case 3: item["cell"][0] = 0.5; break;
      case 4: item["cell"][1] = nullptr; break;
      case 5: item["cell"][2] = 32768; break;
      case 6: item["terrain_recipe"] = 4294967296ULL; break;
      case 7: item["tick"] = -1; break;
      case 8: item["target"]["extra"] = 0; break;
      case 9: state = nullptr; break;
    }
    check(!decode_freedom_knowledge_json(raw.dump(), 7),
          "Malformed closed Moon metadata refuses");
  }
  auto reversed = selected;
  reversed.moon_surface_landmarks->inspections = {records[1], records[0]};
  check(!validate_freedom_knowledge(reversed, 7),
        "Moon inspection order is strict and canonical");
  auto target_raw = Json::parse(canonical);
  auto& target =
      target_raw["moon_surface_landmarks"]["inspections"][0]["target"];
  target["identity"] = "0" + target["identity"].get<std::string>();
  check(!decode_freedom_knowledge_json(target_raw.dump(), 7),
        "Moon metadata rejects noncanonical typed decimal identity");
  auto full = selected;
  full.moon_surface_landmarks->inspections.assign(records.begin(),
                                                  records.begin() + 64);
  check(need(decode_freedom_knowledge_json(
            need(encode_freedom_knowledge_json(full, 7)), 7)) == full,
        "Exactly64 authenticated Moon records fit the bounded save");
  const auto planet = planet_record(moon.catalog.owner);
  full.surface_landmarks = FreedomSurfaceLandmarkState{1, {planet}};
  check(!validate_freedom_knowledge(full, 7),
        "Combined65 planetary/Moon records refuse");
  full.moon_surface_landmarks->inspections.pop_back();
  check(validate_freedom_knowledge(full, 7).has_value(),
        "Combined64 planetary/Moon records validate");
  auto old_planet = selected;
  old_planet.moon_surface_landmarks.reset();
  old_planet.surface_landmarks = FreedomSurfaceLandmarkState{1, {planet}};
  const auto old_bytes = need(encode_freedom_knowledge_json(old_planet, 7));
  auto coexist = old_planet;
  coexist.moon_surface_landmarks = *selected.moon_surface_landmarks;
  auto raw = Json::parse(need(encode_freedom_knowledge_json(coexist, 7)));
  raw.erase("moon_surface_landmarks");
  check(raw.dump(2) + '\n' == old_bytes,
        "Adding Moon metadata preserves every existing planetary JSON byte");
}
auto supported_session() -> NativeFreedomFlightSession {
  auto session = minor_route_fixture::departure();
  check(!session.knowledge()->moon_surface_landmarks,
        "NewGame does not silently adopt Moon inspections");
  const auto catalog = need(generate_secondary_body_catalog(session.system()));
  need(session.select_local_jump(
      {session.system().catalog.id, catalog.bodies[0].node.id}));
  minor_route_fixture::leg(session);
  // Explicit unit support placement. This is NOT the continuous gameplay
  // descent; the existing Moon-voyage Godot proof supplies that separate gate.
  need(session.advance({}));
  auto travel = need(session.travel_document());
  auto flight = session.document();
  const auto contact = test_fixture::make_moon_contact_fixture();
  auto fixed = contact.fixed;
  fixed.tick = flight.flight.tick;
  flight.flight =
      need(reframe_rigid_body(RigidBodyWorldContext{contact.moon}, fixed,
                              {flight.flight.frame, fixed.tick}, contact.moon));
  travel.voyage.voyage.voyage.base = flight;
  travel.voyage.voyage.voyage.surface = {};
  session = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(NativeSaveDocument{travel}))));
  need(session.set_landing_gear(true));
  need(session.commit_touchdown(need(rigid_body_state_checksum(
      RigidBodyWorldContext{*session.moon()}, session.document().flight))));
  need(session.begin_surface_walk());
  return session;
}
auto native_commands(
    const std::vector<FreedomMoonSurfaceLandmarkInspection>& records) -> void {
  auto session = supported_session();
  const auto original = bytes(session);
  const auto tick = session.document().flight.tick;
  const auto resources = session.resources();
  const auto facts = session.knowledge()->entries;
  const auto historical = session.knowledge()->surface_landmarks;
  const auto view = need(session.moon_surface_landmarks_view());
  check(!view.selected && view.can_enable && view.sites.empty() &&
            bytes(session) == original,
        "Historical Moon read remains unselected, quiet and complete-save "
        "neutral");
  need(session.mark_surface_marker());
  const auto marker = session.knowledge()->surface_marker;
  const auto before_enable = bytes(session);
  need(session.enable_surface_landmarks());
  auto no_adoption = need(session.surface_walk_document());
  std::get<FreedomTravelSaveDocument>(no_adoption.voyage.voyage)
      .voyage.knowledge.moon_surface_landmarks.reset();
  check(need(encode_native_save_document_json(
            NativeSaveDocument{no_adoption})) == before_enable,
        "Removing only the new optional component restores the complete "
        "pre-enable save bytes");
  check(session.document().flight.tick == tick &&
            session.resources() == resources &&
            session.knowledge()->entries == facts &&
            session.knowledge()->surface_landmarks == historical &&
            session.knowledge()->surface_marker == marker &&
            session.knowledge()->moon_surface_landmarks &&
            session.knowledge()->moon_surface_landmarks->inspections.empty(),
        "Explicit enable changes only optional Moon metadata at the committed "
        "tick");
  const auto enabled = bytes(session);
  const auto enabled_view = need(session.moon_surface_landmarks_view());
  check(enabled_view.selected && !enabled_view.can_enable &&
            enabled_view.inspection_count == 0 &&
            enabled_view.sites.size() <= 8 && bytes(session) == enabled,
        "Selected Moon field projects at most8 actual sites without save "
        "mutation");
  SurfaceLandmarkId foreign{};
  while (std::ranges::any_of(enabled_view.sites, [&](const auto& site) {
    return site.site.identity.id == foreign;
  }))
    ++foreign.value;
  check(!session.enable_surface_landmarks() &&
            !session.inspect_surface_landmark(foreign) &&
            bytes(session) == enabled,
        "Duplicate enable and foreign inspection refuse atomically");
  session = continued(session);
  // Retained metadata fixture, not a player inspection claim. Recovery must
  // preserve every complete authenticated record, including its first tick.
  auto document = need(session.surface_walk_document());
  auto& knowledge = std::get<FreedomTravelSaveDocument>(document.voyage.voyage)
                        .voyage.knowledge;
  knowledge.moon_surface_landmarks->inspections = {records.front()};
  session = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(NativeSaveDocument{document}))));
  const auto retained = session.knowledge()->moon_surface_landmarks;
  need(session.record_loss(
      FreedomLossCause::recoverable_destruction,
      session.document().origin.state.craft, session.document().flight.tick,
      need(rigid_body_state_checksum(RigidBodyWorldContext{*session.moon()},
                                     session.document().flight))));
  const auto pending = bytes(session);
  check(!session.enable_surface_landmarks() &&
            !session.inspect_surface_landmark(records.front().site) &&
            bytes(session) == pending,
        "Pending recovery refuses Moon metadata commands without mutation");
  need(session.complete_recovery());
  check(
      session.knowledge()->moon_surface_landmarks == retained &&
          session.knowledge()->surface_marker == marker && !session.moon(),
      "Home replacement retains independent Moon records and personal marker");
  session = continued(session);
  auto recovered = need(session.recovery_document());
  auto& active =
      std::get<FreedomKnowledgeSaveDocument>(recovered.voyage).knowledge;
  active.moon_surface_landmarks.reset();
  check(!validate_freedom_recovery_document(recovered),
        "Recovery refuses discarding retained Moon selection/history");
  recovered = need(session.recovery_document());
  auto& altered =
      std::get<FreedomKnowledgeSaveDocument>(recovered.voyage).knowledge;
  ++altered.moon_surface_landmarks->inspections.front().tick;
  check(!validate_freedom_recovery_document(recovered),
        "Recovery refuses rewriting a retained Moon first-inspection tick");
  recovered = need(session.recovery_document());
  auto& backdated =
      std::get<FreedomKnowledgeSaveDocument>(recovered.voyage).knowledge;
  backdated.moon_surface_landmarks->inspections.push_back(records[1]);
  check(
      !validate_freedom_recovery_document(recovered),
      "Recovery refuses backdating a newly added Moon inspection before loss");
}
} // namespace
int main() {
  try {
    const auto fixture = test_fixture::make_moon_contact_fixture();
    const auto records = identities(fixture.moon);
    metadata(fixture.moon, records);
    native_commands(records);
    std::cout << "PASS native Moon landmarks " << checks << " checks\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL native Moon landmarks: " << error.what() << '\n';
    return 1;
  }
}
