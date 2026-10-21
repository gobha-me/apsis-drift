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
using V = RigidVector3;
auto sample_sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto sample_scale(V a, double k) -> V {
  return {k * a.x, k * a.y, k * a.z};
}
auto sample_dot(V a, V b) -> double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
auto sample_length(V a) -> double {
  return std::hypot(a.x, a.y, a.z);
}
auto sample_rotate(RigidOrientation q, V v) -> V {
  const V u{q.x, q.y, q.z};
  const auto cross = [](V a, V b) -> V {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
  };
  const auto a = cross(u, v), b = cross(u, a);
  const auto k = 2 / (q.w * q.w + sample_dot(u, u));
  return {v.x + k * (q.w * a.x + b.x), v.y + k * (q.w * a.y + b.y),
          v.z + k * (q.w * a.z + b.z)};
}
auto sample_walk_tick(NativeFreedomFlightSession& session, V target) -> void {
  const auto foot = session.surface_walker()->foot_position_metres;
  const auto delta = sample_sub(target, foot);
  const auto distance = sample_length(delta);
  check(std::isfinite(distance) && distance > .000001,
        "Moon specimen walk has a finite nonzero actual bearing");
  const auto up = sample_scale(foot, 1 / sample_length(foot));
  const auto craft_right = sample_rotate(
      session.surface()->moon_landed->fixed.orientation, {1, 0, 0});
  const auto projected =
      sample_sub(craft_right, sample_scale(up, sample_dot(craft_right, up)));
  const auto right = sample_scale(projected, 1 / sample_length(projected));
  const V back{right.y * up.z - right.z * up.y, right.z * up.x - right.x * up.z,
               right.x * up.y - right.y * up.x};
  need(session.advance_surface_walk({-sample_dot(delta, back) / distance,
                                     sample_dot(delta, right) / distance, 0}));
  check(!session.surface_walk_blockage(),
        "Moon specimen foot route remains genuinely supported");
}
auto field_samples(
    const std::vector<FreedomMoonSurfaceLandmarkInspection>& records) -> void {
  // Keep the existing supported-contact unit fixture and actual charged route.
  // It is not the separately qualified continuous Moon descent. From its real
  // hatch, walk toward ONE nearest existing generated exposure; no site/actor
  // relocation, alternate centre selection or terrain changes.
  auto session = supported_session();
  need(session.enable_surface_landmarks());
  const auto initial_view = need(session.moon_surface_landmarks_view());
  check(!initial_view.sites.empty(),
        "Supported Moon fixture has a real nearby specimen exposure");
  const auto site = initial_view.sites.front().site;
  const auto id = site.identity.id;
  const V point{site.position_metres.x, site.position_metres.y,
                site.position_metres.z};
  bool reached{};
  for (unsigned step = 0; step < 24000; ++step) {
    if (sample_length(sample_sub(
            point, session.surface_walker()->foot_position_metres)) <= 7.5) {
      reached = true;
      break;
    }
    sample_walk_tick(session, point);
  }
  check(
      reached,
      "Actual supported Moon walking reaches the fixed exposure within8metres");
  const auto before_inspect = bytes(session);
  check(!session.field_sample_collection_available(id) &&
            !session.collect_field_sample(id) &&
            bytes(session) == before_inspect &&
            !session.knowledge()->field_samples,
        "Reachable Moon collection requires inspection and cannot silently "
        "select sample state");
  need(session.inspect_surface_landmark(id));
  const BodyTarget target{session.system().catalog.id, session.moon()->body};
  const auto tick = session.document().flight.tick;
  const auto before_collect = bytes(session);
  check(session.field_sample_collection_available(id).has_value() &&
            session.field_sample_collection_available(id).has_value() &&
            bytes(session) == before_collect,
        "Actual Moon collection availability is repeatable and complete-save "
        "neutral");
  // Explicit authenticated retained-metadata fixture supplies a full pouch;
  // these four remote specimens are NOT claimed as physically collected here.
  auto full_document = need(session.surface_walk_document());
  auto& full_knowledge =
      std::get<FreedomTravelSaveDocument>(full_document.voyage.voyage)
          .voyage.knowledge;
  FreedomFieldSampleState full_samples{kFreedomFieldSamplesVersion,
                                       session.document().origin.state.craft,
                                       {},
                                       {}};
  for (const auto& inspection : records) {
    if (inspection.site == id) continue;
    if (full_samples.carried.size() == kFreedomFieldSampleCapacity) break;
    const auto identity =
        need(identify_moon_surface_landmark(*session.moon(), inspection.cell));
    check(identity.has_value(),
          "Retained pouch source authenticates to the actual Moon field");
    full_knowledge.moon_surface_landmarks->inspections.push_back(inspection);
    const FreedomFieldSampleRecord record{
        inspection.target, inspection.cell,
        inspection.site,   inspection.terrain_recipe,
        identity->kind,    full_samples.carrier,
        inspection.tick};
    full_samples.carried.push_back(record);
    full_samples.journal.push_back(record);
  }
  std::ranges::sort(full_knowledge.moon_surface_landmarks->inspections, {},
                    [](const auto& record) { return record.site.value; });
  full_knowledge.field_samples = full_samples;
  auto full = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(NativeSaveDocument{full_document}))));
  const auto full_before = bytes(full);
  const auto available = full.field_sample_collection_available(id);
  check(!available && available.error().find("full") != std::string::npos &&
            !full.collect_field_sample(id) && bytes(full) == full_before,
        "A genuinely reachable inspected Moon site refuses the labelled "
        "full-pouch fixture atomically");
  need(full.discard_field_sample(full_samples.carried.front().target,
                                 full_samples.carried.front().site));
  need(full.collect_field_sample(id));
  check(full.knowledge()->field_samples->carried.size() ==
                kFreedomFieldSampleCapacity &&
            full.knowledge()->field_samples->journal.size() ==
                kFreedomFieldSampleCapacity + 1,
        "Discarded slot admits actual local collection without deleting any "
        "first notes");

  need(session.collect_field_sample(id));
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Immutability
  const auto collected = *session.knowledge()->field_samples;
  check(
      collected.carried.size() == 1 && collected.journal == collected.carried &&
          collected.carrier == session.document().origin.state.craft &&
          collected.carried.front() ==
              FreedomFieldSampleRecord{target, site.identity.cell, id,
                                       session.document().moon->terrain_recipe,
                                       site.identity.kind, collected.carrier,
                                       tick},
      "Actual Moon specimen records exact typed site/kind/current "
      "terrain/craft and first tick");
  auto without_samples = need(session.surface_walk_document());
  std::get<FreedomTravelSaveDocument>(without_samples.voyage.voyage)
      .voyage.knowledge.field_samples.reset();
  check(need(encode_native_save_document_json(
            NativeSaveDocument{without_samples})) == before_collect,
        "Moon collection changes only optional sample metadata, preserving all "
        "actor/resources/facts/receipt bytes");
  const auto saved = bytes(session);
  auto foreign = target;
  ++foreign.body.version;
  check(!session.collect_field_sample(id) &&
            !session.field_sample_collection_available(id) &&
            !session.collect_field_sample({}) &&
            !session.discard_field_sample(foreign, id) &&
            bytes(session) == saved,
        "Moon duplicate/foreign/unknown commands refuse without Save mutation");
  auto resumed = continued(session);
  need(session.advance_surface_walk({}));
  need(resumed.advance_surface_walk({}));
  check(bytes(session) == bytes(resumed) &&
            session.knowledge()->field_samples == collected,
        "Moon complete Continue and a genuine supported tick retain all "
        "specimen provenance");
  need(session.discard_field_sample(target, id));
  check(session.knowledge()->field_samples->carried.empty() &&
            session.knowledge()->field_samples->journal == collected.journal &&
            session.document().flight.tick == tick + 1,
        "Moon discard keeps the first journal record without advancing time");
  need(session.collect_field_sample(id));
  check(session.knowledge()->field_samples->carried.front().tick == tick + 1 &&
            session.knowledge()->field_samples->journal == collected.journal,
        "Moon recollection records the new actual tick without rewriting first "
        "notes");
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Immutability
  const auto pouch = *session.knowledge()->field_samples;
  bool returned{};
  for (unsigned step = 0; step < 24000; ++step) {
    const auto access = need(session.surface_return_access());
    if (access.within_reach) {
      returned = true;
      break;
    }
    sample_walk_tick(session, access.foot_position_metres);
  }
  check(returned, "Moon specimen returns by actual supported foot movement to "
                  "its original hatch");
  need(session.return_from_surface_walk());
  const auto aboard = bytes(session);
  check(session.knowledge()->field_samples == pouch &&
            !session.collect_field_sample(id) &&
            !session.field_sample_collection_available(id) &&
            bytes(session) == aboard,
        "Moon reboarding retains the pouch and refuses seated collection "
        "atomically");
  {
    // Clone the genuinely collected/recollected specimen voyage. Reuse the
    // existing Moon marker contract's physical ascent, not a space placement;
    // the original session remains aboard for its independent discard proof.
    auto homebound = session;
    const auto original = bytes(session);
    const auto craft = homebound.document().origin.state.craft;
    const auto ascent_tick = homebound.document().flight.tick;
    const auto ascent_fuel = homebound.resources()->flight_quanta;
    const auto charges = homebound.resources()->jump_charges;
    const auto attempt = homebound.travel()->next_attempt;
    need(homebound.release_surface());
    const double departure_radius =
        homebound.moon()->radius_metres + kMoonTerrainMaximumReliefMetres + 100;
    NativeFlightControls ascent;
    ascent.positive_translation.y = .5;
    bool clear{};
    for (unsigned step = 0; step < 18000; ++step) {
      const auto& position = homebound.document().flight.position_metres;
      if (std::hypot(position.x, position.y, position.z) > departure_radius) {
        clear = true;
        break;
      }
      const auto applied = need(homebound.advance(ascent));
      check(!applied.propulsion_refused && !homebound.surface()->moon_landed,
            "Present specimen ascends through actual paid Moon thrust without "
            "recapture");
    }
    check(clear && homebound.document().flight.tick > ascent_tick &&
              homebound.resources()->flight_quanta < ascent_fuel &&
              homebound.resources()->jump_charges == charges &&
              homebound.travel()->next_attempt == attempt &&
              homebound.knowledge()->field_samples == pouch,
          "Actual bounded Moon ascent retains exact carried specimens/first "
          "notes and spends only flight fuel");
    homebound = continued(homebound);
    const auto departure_tick = homebound.document().flight.tick;
    need(homebound.select_local_jump(
        minor_route_fixture::home_target(homebound)));
    minor_route_fixture::leg(homebound);
    check(!homebound.moon() && !homebound.minor() &&
              homebound.document().world &&
              homebound.document().world->system == target.system &&
              homebound.document().world->planet ==
                  homebound.document().origin.recipe.home_planet &&
              homebound.document().flight.tick ==
                  departure_tick + kJumpSpoolTicks + kJumpTransitTicks &&
              homebound.document().origin.state.craft == craft &&
              craft == pouch.carrier && homebound.resources()->craft == craft &&
              homebound.resources()->jump_charges == charges - 1 &&
              homebound.travel()->next_attempt == attempt + 1 &&
              homebound.resources()->flight_quanta < ascent_fuel &&
              homebound.knowledge()->field_samples == pouch,
          "Actual charged Moon-to-Home spool/transit/arrival keeps the same "
          "carrier, specimens and first notes without refill");
    const auto arrived = bytes(homebound);
    homebound = continued(homebound);
    check(bytes(homebound) == arrived &&
              homebound.knowledge()->field_samples == pouch &&
              bytes(session) == original,
          "Present specimens survive exact Home Continue while the original "
          "aboard-discard voyage remains unchanged");
  }
  need(session.discard_field_sample(target, id));
  check(
      session.knowledge()->field_samples->carried.empty() &&
          session.knowledge()->field_samples->journal == collected.journal,
      "Moon samples can be discarded aboard without deleting the first notes");
  resumed = continued(session);
  check(bytes(resumed) == bytes(session),
        "Empty Moon pouch with retained notes survives complete Continue");
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
    field_samples(records);
    std::cout << "PASS native Moon landmarks " << checks << " checks\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL native Moon landmarks: " << error.what() << '\n';
    return 1;
  }
}
