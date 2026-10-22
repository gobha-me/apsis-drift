#include "apsis_drift/freedom_flight_save.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/save_file.hpp"
#include "moon_contact_fixture.hpp"

#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

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
  if (!value) throw std::runtime_error("Moon flight save fixture refused");
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto fixture() -> FreedomFlightSaveDocument {
  auto origin = make_freedom_new_game_document(Seed{42});
  origin.state.tick = 27;
  const auto system = need(generate_physical_origin_system(Seed{42}, 2));
  const auto catalog = need(generate_secondary_body_catalog(system));
  const auto moon = need(
      generate_physical_moon_descriptor(catalog, catalog.bodies[0].node.id));
  FreedomFlightSaveDocument document{origin, {}, {}, {}};
  document.moon = FreedomMoonWorldSelection{1, {system.catalog.id, moon.body}};
  document.model.physical_catalog = 2;
  document.model.physical_ephemeris = 2;
  document.flight.tick = origin.state.tick;
  document.flight.craft = wayfarer_frame().recipe;
  document.flight.frame = {RigidFrameKind::body_relative_inertial,
                           system.catalog.id,
                           {},
                           {},
                           moon.body};
  document.flight.position_metres = {moon.radius_metres + 50000.0, 100, 0};
  document.flight.linear_velocity_metres_per_second = {0, 0, 100};
  return document;
}
auto network_companion_roundtrips() -> void {
  auto moon_source = fixture();
  const auto system = need(generate_physical_origin_system(Seed{42}, 2));
  const auto catalog = need(generate_secondary_body_catalog(system));
  const auto minor = need(
      generate_physical_minor_descriptor(catalog, catalog.bodies[1].node.id));
  auto minor_source = moon_source;
  minor_source.moon.reset();
  minor_source.minor =
      FreedomMinorWorldSelection{1, {system.catalog.id, minor.body}};
  minor_source.flight.frame.body = minor.body;
  minor_source.flight.position_metres = {minor.radius_metres + 50000.0, 100, 0};
  for (auto source : {moon_source, minor_source}) {
    const auto old_bytes = need(encode_freedom_flight_document_json(source));
    const auto old_format = source.moon ? kFreedomMoonFlightSaveFormatVersion
                                        : kFreedomMinorFlightSaveFormatVersion;
    const auto selected_format =
        source.moon ? kFreedomNetworkMoonFlightSaveFormatVersion
                    : kFreedomNetworkMinorFlightSaveFormatVersion;
    source.origin.network = FreedomNetworkSelection{};
    for (const bool lineage : {false, true}) {
      if (lineage) {
        source.origin.lineage = FreedomCraftLineage{1, 1};
        source.origin.state.craft =
            need(derive_freedom_craft_identity(Seed{42}, 1));
      }
      const auto bytes = need(encode_freedom_flight_document_json(source));
      const auto json = Json::parse(bytes);
      const auto restored = need(decode_freedom_flight_document_json(bytes));
      const auto owner = need(hydrate_freedom_flight_document(restored));
      check(json["format_version"] == selected_format && restored == source &&
                need(encode_freedom_flight_document_json(restored)) == bytes &&
                owner.system == system &&
                need(relative_freedom_flight_state(restored, system)) ==
                    source.flight,
            "network54/55 retain genuine typed companion state, selector and "
            "lineage");
      auto bad = json;
      bad["format_version"] = old_format;
      check(!decode_freedom_flight_document_json(bad.dump()),
            "old42/46 cannot admit a network descriptor");
      bad = json;
      bad.erase("network");
      check(!decode_freedom_flight_document_json(bad.dump()),
            "new54/55 cannot infer an absent selector");
      bad = json;
      bad["network"]["version"] = 3U;
      check(!decode_freedom_flight_document_json(bad.dump()),
            "typed companion saves authenticate the network recipe");
      bad = json;
      bad["world_owner"]["target"]["system"] = "0";
      check(!decode_freedom_flight_document_json(bad.dump()),
            "selected network does not extend companion routes to a foreign "
            "owner");
    }
    check(need(encode_freedom_flight_document_json(need(
              decode_freedom_flight_document_json(old_bytes)))) == old_bytes,
          "old typed companion bytes remain exactly repeatable");
  }
}
auto continuation(FreedomFlightSaveDocument document)
    -> FreedomFlightSaveDocument {
  const auto owner = need(hydrate_freedom_moon_flight_document(document));
  const RigidBodyWorldContext context{owner.moon};
  VacuumIntent intent;
  intent.assistance = document.model.assistance;
  intent.positive_translation.x = .2;
  for (unsigned tick = 0; tick < 120; ++tick) {
    need(advance_moon_body_dynamics(context, document.flight, intent));
    document.origin.state.tick = document.flight.tick;
  }
  return document;
}
auto invalid_documents(const FreedomFlightSaveDocument& document) -> void {
  auto reject = [](const FreedomFlightSaveDocument& bad) {
    check(!hydrate_freedom_moon_flight_document(bad) &&
              !encode_freedom_flight_document_json(bad),
          "Invalid moon document refuses hydration and encoding");
  };
  auto bad = document;
  bad.world = FreedomActiveWorldSelection{1, document.moon->target.system,
                                          document.origin.recipe.home_planet};
  reject(bad);
  bad = document;
  bad.moon.reset();
  reject(bad);
  bad = document;
  ++bad.moon->target.body.value;
  reject(bad);
  bad = document;
  bad.moon->target.body.kind = BodyKind::planet;
  reject(bad);
  bad = document;
  ++bad.moon->target.system.value;
  reject(bad);
  bad = document;
  ++bad.moon->version;
  reject(bad);
  bad = document;
  ++bad.moon->physical_recipe;
  reject(bad);
  bad = document;
  ++bad.moon->rotation_recipe;
  reject(bad);
  bad = document;
  ++bad.moon->dynamics_recipe;
  reject(bad);
  bad = document;
  ++bad.moon->terrain_recipe;
  reject(bad);
  bad = document;
  bad.model.optics = 1;
  reject(bad);
  bad = document;
  bad.model.terrain_policy = 2;
  reject(bad);
  bad = document;
  bad.model.hold.target =
      OrbitHoldTarget{document.origin.recipe.home_planet, 1e7, {0, 0, 1}};
  reject(bad);
  bad = document;
  ++bad.origin.state.tick;
  reject(bad);
  bad = document;
  bad.flight.position_metres.x = std::numeric_limits<double>::infinity();
  reject(bad);
  bad = document;
  bad.flight.frame.planet = document.origin.recipe.home_planet;
  reject(bad);
}
auto malformed(std::string_view bytes) -> void {
  const auto source = Json::parse(bytes);
  auto reject = [](const Json& bad) {
    check(!decode_freedom_flight_document_json(bad.dump()),
          "Malformed moon save refuses the public flight decoder");
  };
  auto bad = source;
  bad["world_owner"]["planet_id"] = "1";
  reject(bad);
  bad = source;
  bad["flight_model"]["atmosphere"] = 1;
  reject(bad);
  bad = source;
  bad["world_owner"]["terrain_recipe"] = 2;
  reject(bad);
  bad = source;
  bad["world_owner"]["physical_recipe"] = true;
  reject(bad);
  bad = source;
  bad["state"]["location"] = "planetary_flight";
  reject(bad);
  bad = source;
  bad["format_version"] = kFreedomOpticalFlightSaveFormatVersion;
  reject(bad);
  bad = source;
  bad["world_owner"]["target"] = nullptr;
  reject(bad);
  bad = source;
  bad["flight"]["frame"]["planet_id"] = "1";
  reject(bad);
  std::string duplicate{bytes};
  duplicate.insert(duplicate.find('{') + 1, "\"format_version\":42,");
  check(!decode_freedom_flight_document_json(duplicate),
        "Duplicate keys refuse before moon dispatch");
  check(!decode_freedom_system_flight_import_json(bytes),
        "Old system-import decoder cannot admit moon format42");
}
auto suspension_landing() -> void {
  const auto contact = test_fixture::make_moon_contact_fixture();
  auto fixed = contact.fixed;
  const auto n = contact.triangle.outward_normal;
  fixed.position_metres.x += .12 * n.x;
  fixed.position_metres.y += .12 * n.y;
  fixed.position_metres.z += .12 * n.z;
  fixed.linear_velocity_metres_per_second = {-.1 * n.x, -.1 * n.y, -.1 * n.z};
  auto flight = fixture();
  flight.flight = need(
      reframe_rigid_body(RigidBodyWorldContext{contact.moon}, fixed,
                         {contact.source.frame, fixed.tick}, contact.moon));
  flight.origin.state.tick = fixed.tick;
  const FreedomSurfaceSaveDocument surface{flight, {true, {}, {}}};
  auto session = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(NativeSaveDocument{surface}))));
  for (unsigned tick = 0; tick < 600 && !session.surface()->moon_landed; ++tick)
    need(session.advance({}));
  check(session.surface()->moon_landed.has_value(),
        "Gentle real descent compresses rated gear until all genuine pads "
        "engage");
  check(session.document().flight.tick > flight.flight.tick,
        "Suspension landing requires actual flight ticks, never a pose snap "
        "into support");
}
auto surface_saves() -> void {
  const auto contact = test_fixture::make_moon_contact_fixture();
  const RigidBodyWorldContext context{contact.moon};
  const auto checksum =
      need(rigid_body_state_checksum(context, contact.source));
  const auto anchor = need(
      prepare_moon_landed_craft(contact.moon, contact.source, true, checksum));
  auto flight = fixture();
  flight.flight = need(
      resolve_moon_landed_craft(contact.moon, anchor, contact.source.tick));
  flight.origin.state.tick = flight.flight.tick;
  FreedomSurfaceSaveDocument surface{flight, {true, {}, anchor}};
  const auto bytes = need(encode_freedom_surface_document_json(surface));
  const auto decoded = need(decode_freedom_surface_document_json(bytes));
  check(decoded == surface &&
            need(encode_freedom_surface_document_json(decoded)) == bytes,
        "Moon gear and genuine supported anchor roundtrip exactly through "
        "surface43");
  auto bad = surface;
  bad.surface.gear_deployed = false;
  check(!encode_freedom_surface_document_json(bad),
        "Moon anchor cannot persist with stowed gear");
  bad = surface;
  ++bad.surface.moon_landed->fixed.frame.body->value;
  check(!encode_freedom_surface_document_json(bad),
        "Foreign anchor cannot adopt moon flight");
  bad = surface;
  std::get<FreedomFlightSaveDocument>(bad.base).flight.position_metres.x += .1;
  check(!encode_freedom_surface_document_json(bad),
        "Stored moon flight must equal its actual constrained pose");
  auto malformed = Json::parse(bytes);
  malformed["format_version"] = kFreedomSurfaceSaveFormatVersion;
  check(!decode_freedom_surface_document_json(malformed.dump()),
        "Old surface23 cannot reinterpret a moon anchor");
  malformed = Json::parse(bytes);
  malformed["surface"]["landed"] = nullptr;
  check(!decode_freedom_surface_document_json(malformed.dump()),
        "Moon surface43 remains a closed distinct schema");
  FreedomResources ledger;
  ledger.craft = flight.origin.state.craft;
  ledger.tick = flight.flight.tick;
  const FreedomResourceSaveDocument resource{surface, ledger};
  const auto native_bytes =
      need(encode_native_save_document_json(NativeSaveDocument{resource}));
  auto session =
      need(NativeFreedomFlightSession::open(need(native_select_save_document(
          need(decode_native_save_document_json(native_bytes))))));
  check(session.moon() && session.surface() &&
            session.surface()->moon_landed == anchor &&
            session.document() == flight && session.resources() == ledger,
        "Native Continue retains the same moon landing and resource clock");
  check(need(encode_native_save_document_json(need(session.save_document()))) ==
            native_bytes,
        "Native moon landed Save reproduces complete Continue bytes");
  const auto before = session.document();
  NativeFlightControls thrust{};
  thrust.positive_translation.y = 1;
  check(!session.advance(thrust) && session.document() == before &&
            session.resources() == ledger,
        "Supported moon craft refuses propulsion without spending a tick");
  need(session.advance({}));
  check(session.document().flight.tick == before.flight.tick + 1 &&
            session.resources()->tick == ledger.tick + 1 &&
            session.resources()->flight_quanta == ledger.flight_quanta &&
            session.surface()->moon_landed == anchor,
        "Landed Moon craft and resource ledger advance one shared supported "
        "tick");
  const auto supported = need(session.save_document());
  const auto continued = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(need(decode_native_save_document_json(
          need(encode_native_save_document_json(supported))))))));
  check(continued.document() == session.document() &&
            continued.resources() == session.resources(),
        "Supported native tick can Continue at the same Moon anchor");
  need(session.release_surface());
  check(!session.surface()->moon_landed &&
            session.document().flight ==
                need(release_moon_landed_craft(contact.moon, anchor,
                                               session.document().flight.tick)),
        "Moon liftoff removes support without a synthetic launch impulse");
  const auto release = session.document().flight;
  for (unsigned tick = 0; tick < 30; ++tick)
    need(session.advance(thrust));
  check(!session.surface()->moon_landed &&
            session.document().flight.tick == release.tick + 30,
        "Actual outward thrust leaves deployed Moon gear without immediate "
        "recapture");
  need(session.set_landing_gear(false));
  check(!session.surface()->gear_deployed,
        "Moon gear commands dispatch to actual Moon owner");
}
} // namespace
int main() {
  try {
    const auto document = fixture();
    const auto encoded = need(encode_freedom_flight_document_json(document));
    const auto decoded = need(decode_freedom_flight_document_json(encoded));
    check(decoded == document &&
              need(encode_freedom_flight_document_json(decoded)) == encoded,
          "Actual typed moon state roundtrips through public flight codec "
          "exactly");
    const auto native = need(decode_native_save_document_json(encoded));
    const auto* native_flight = std::get_if<FreedomFlightSaveDocument>(&native);
    check(native_flight && *native_flight == document,
          "Top-level native save dispatch admits the same explicit moon "
          "document");
    const auto hydrated = need(hydrate_freedom_flight_document(decoded));
    check(hydrated.moon &&
              hydrated.moon->moon.body == document.moon->target.body &&
              hydrated.system == hydrated.moon->moon.catalog.owner,
          "Common hydration retains actual typed moon and source system");
    check(need(relative_freedom_flight_state(decoded, hydrated.system)) ==
              decoded.flight,
          "Relative view authenticates the actual moon without a parent alias");
    const auto continued = continuation(document);
    check(continued == continuation(decoded) &&
              need(encode_freedom_flight_document_json(continued)) ==
                  need(encode_freedom_flight_document_json(
                      continuation(decoded))),
          "Save/Load preserves a full second of real moon physics and exact "
          "output");
    const auto selected =
        need(native_select_save_document(NativeSaveDocument{document}));
    auto session = need(NativeFreedomFlightSession::open(selected));
    const auto observed = need(session.observe());
    check(session.moon() && observed.moon &&
              observed.moon->gravity.body == document.moon->target.body,
          "Native session opens and observes the genuine moon owner");
    const auto before_hold = session.document();
    check(!session.hold_current_orbit() && session.document() == before_hold,
          "Unsupported planet-only hold cannot mutate the moon route");
    NativeFlightControls controls{};
    controls.positive_translation.x = .2;
    for (unsigned tick = 0; tick < 120; ++tick)
      need(session.advance(controls));
    check(
        session.document() == continued,
        "Native moon tick dispatch uses the exact same authoritative physics");
    const auto saved = need(session.save_document());
    const auto resumed = need(NativeFreedomFlightSession::open(
        need(native_select_save_document(need(decode_native_save_document_json(
            need(encode_native_save_document_json(saved))))))));
    check(resumed.document() == session.document() &&
              resumed.moon() == session.moon(),
          "Native Save/Continue retains actual moon state and selected owner");
    network_companion_roundtrips();
    surface_saves();
    suspension_landing();
    invalid_documents(document);
    malformed(encoded);
    std::uint64_t hash{14695981039346656037ULL};
    for (const char c : encoded) {
      hash ^= static_cast<unsigned char>(c);
      hash *= 1099511628211ULL;
    }
    std::cout << "Moon flight save fingerprint " << hash << '\n';
  } catch (const std::exception& error) {
    check(false, error.what());
  }
  std::cout << checks << " moon save checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
