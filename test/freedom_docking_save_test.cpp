#include "apsis_drift/native_flight_session.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
int failures{};
auto check(bool x, std::string_view message) -> void {
  if (!x) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
template <class T, class E> auto require(std::expected<T, E> x) -> T {
  if (!x) throw std::runtime_error("docking save fixture refused");
  return *x;
}
auto contents(const std::filesystem::path& p) -> std::string {
  std::ifstream f{p};
  return {std::istreambuf_iterator<char>{f}, {}};
}
struct Fixture {
  Seed seed;
  PhysicalLocalSystem system;
  OriginStationDescriptor station;
  OriginStationGeometry geometry;
  PlanetDescriptor planet;
  explicit Fixture(Seed s = Seed{42}, std::uint32_t version = 1)
      : seed(s), system(require(generate_physical_origin_system(s, version))),
        station(generate_origin_station(s)),
        geometry(require(origin_station_geometry(station))),
        planet(system.catalog.planets[kOriginHomePlanetOrdinal].descriptor) {}
  auto document(std::uint32_t port = 1, double separation = .1) const
      -> FreedomDockingSaveDocument {
    auto origin = make_freedom_new_game_document(seed);
    origin.state.tick = 25;
    origin.state.discoveries.push_back({{77}, 8});
    origin.state.world_deltas.push_back(
        {"signal:77", SaveWorldDeltaKind::discovered, 9});
    auto body =
        require(resolve_origin_port_pose(system, station, geometry,
                                         {station.id, port}, 25, separation))
            .planet_relative;
    body.craft = wayfarer_frame().recipe;
    FreedomDockingSaveDocument result{{origin, body, {}},
                                      {1, {station.id, port}, false}};
    result.flight.model.physical_catalog = system.generator_version;
    result.flight.model.physical_ephemeris = system.ephemeris_version;
    return result;
  }
  auto session(FreedomDockingSaveDocument d) const
      -> NativeFreedomFlightSession {
    return require(NativeFreedomFlightSession::open(
        {NativeStartup::Mode::freedom, std::move(d), planet, {}}));
  }
};
auto invalid() -> void {
  const Fixture f;
  const auto good = f.document();
  auto reject = [](const FreedomDockingSaveDocument& d) {
    return !validate_freedom_docking_document(d) &&
           !encode_freedom_docking_document_json(d);
  };
  for (const auto target : {OriginPortId{{0}, 1}, OriginPortId{f.station.id, 0},
                            OriginPortId{f.station.id, 3}}) {
    auto bad = good;
    bad.docking.target = target;
    check(reject(bad), "unknown/foreign port refuses before persistence");
  }
  auto bad = good;
  bad.docking.geometry_version = 2;
  check(reject(bad), "unknown geometry version refuses");
  bad = good;
  bad.flight.flight.craft = starter_shuttle_frame().recipe;
  check(reject(bad), "legacy craft cannot impersonate Wayfarer collar");
  bad = good;
  bad.docking.attached = true;
  check(reject(bad), "attachment cannot claim the uncaptured approach pose");
  for (const double v : {std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity()}) {
    bad = good;
    bad.flight.flight.position_metres.x = v;
    check(reject(bad), "nonfinite body refuses");
    auto session = f.session(good);
    NativeFlightControls controls;
    controls.negative_translation.z = v;
    check(!session.advance(controls) && session.document() == good.flight &&
              session.docking() == good.docking,
          "nonfinite command refuses atomically");
  }
  const auto text = require(encode_freedom_docking_document_json(good));
  const auto root = Json::parse(text);
  std::array<Json, 13> mutations;
  mutations.fill(root);
  mutations[0]["docking"]["extra"] = 1;
  mutations[1]["docking"].erase("attached");
  mutations[2]["docking"]["attached"] = 1;
  mutations[3]["docking"]["target"]["ordinal"] = -1;
  mutations[4]["docking"]["target"]["ordinal"] = 1.0;
  mutations[5]["docking"]["target"]["ordinal"] = 4294967297ULL;
  mutations[6]["docking"]["target"]["station_id"] = "01";
  mutations[7]["docking"]["target"]["station_id"] = "18446744073709551616";
  mutations[8]["docking"]["target"]["station_id"] = f.station.id.value;
  mutations[9]["state"]["location"] = "docked_at_origin_port";
  mutations[10]["docking"]["geometry_version"] = 1.0;
  mutations[11]["flight_model"]["extra"] = 1;
  mutations[12]["flight"] = nullptr;
  for (const auto& x : mutations)
    check(!decode_freedom_docking_document_json(x.dump()),
          "strict corrupt docking JSON refuses");
  auto duplicate = text;
  duplicate.insert(duplicate.find('}'), ", \"application\": \"duplicate\"");
  // Duplicate nested keys, too, must be detected before delegated decoding.
  const auto duplicated = "{\"format_version\":19,\"format_version\":18}";
  check(!decode_freedom_docking_document_json(duplicate) &&
            !decode_freedom_docking_document_json(duplicated),
        "duplicate keys refuse");
  check(!decode_freedom_docking_document_json(
            std::string(kMaximumSaveDocumentBytes + 1, ' ')),
        "oversized document refuses before parsing");
  auto session = f.session(good);
  const auto before = session.document();
  check(!session.select_port(0) && !session.select_port(3) &&
            !session.release_port() && session.document() == before &&
            session.docking() == good.docking,
        "invalid lifecycle actions preserve owner");
  auto far = f.session(f.document(1, 12));
  check(!far.capture_port() && !far.docking()->attached,
        "capture does not teleport a distant approach to the port");
  for (const auto decision :
       {OriginDockDecision::wrong_side, OriginDockDecision::wrong_attitude,
        OriginDockDecision::outside_reservation,
        OriginDockDecision::excessive_closure,
        OriginDockDecision::excessive_angular_motion}) {
    auto approach = good;
    auto& body = approach.flight.flight;
    switch (decision) {
      case OriginDockDecision::wrong_side: body.position_metres.y += .2; break;
      case OriginDockDecision::wrong_attitude: body.orientation = {}; break;
      case OriginDockDecision::outside_reservation:
        body.position_metres.x += 25;
        break;
      case OriginDockDecision::excessive_closure:
        body.linear_velocity_metres_per_second.y += 1;
        break;
      case OriginDockDecision::excessive_angular_motion:
        body.angular_velocity_radians_per_second.y = .03;
        break;
      default: throw std::runtime_error("invalid refusal fixture selection");
    }
    auto refused = f.session(approach);
    check(require(refused.assess_port()).decision == decision &&
              !refused.capture_port() &&
              refused.document() == approach.flight &&
              refused.docking() == approach.docking,
          "public capture retains the canonical physical refusal and source");
  }
  auto terminal = good;
  terminal.flight.origin.state.tick =
      std::numeric_limits<SimulationTick>::max() - 2;
  terminal.flight.flight = require(
      release_origin_port(f.system, f.station, f.geometry,
                          {1, good.docking.target, wayfarer_frame().recipe,
                           terminal.flight.origin.state.tick}));
  terminal.docking.attached = true;
  auto held = f.session(terminal);
  check(!held.advance({}) && held.document() == terminal.flight &&
            held.docking() == terminal.docking,
        "terminal constrained clock refuses without losing persistable state");
}
auto lifecycle(const std::filesystem::path& dir) -> void {
  for (const auto seed :
       {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}}) {
    const Fixture f{seed};
    for (std::uint32_t ordinal : {1U, 2U}) {
      const auto input = f.document(ordinal);
      const auto encoded = require(encode_freedom_docking_document_json(input));
      check(require(decode_freedom_docking_document_json(encoded)) == input,
            "approach roundtrip keeps body, port, clock and history");
      auto session = f.session(input);
      check(require(session.assess_port()).decision ==
                OriginDockDecision::capture_ready,
            "canonical in-corridor approach is capture-ready");
      check(session.capture_port().has_value() && session.docking()->attached,
            "public capture succeeds");
      const auto captured = session.document();
      check(!session.current_orbit_hold_target() &&
                !session.hold_current_orbit() &&
                !session.set_hold(
                    {1, OrbitHoldTarget{f.planet.id,
                                        captured.flight.position_metres.x,
                                        {0, 0, 1}}}) &&
                session.document() == captured,
            "attached hold selection refuses before any saved state mutation");
      const auto pose = require(release_origin_port(
          f.system, f.station, f.geometry,
          {1, {f.station.id, ordinal}, wayfarer_frame().recipe, 25}));
      check(captured.flight == pose && captured.origin == input.flight.origin,
            "mechanical capture arrests accepted residuals without "
            "time/history changes");
      check(!session.select_port(3 - ordinal) && !session.capture_port(),
            "attached craft cannot select a second port or recapture");
      NativeFlightControls firing;
      firing.negative_translation.z = .5;
      check(!session.advance(firing) && session.document() == captured,
            "constraint refuses propulsion before state mutation");
      auto source = dir / (std::to_string(seed.value) + "-" +
                           std::to_string(ordinal) + ".json");
      check(session.save_as(source).has_value(),
            "attached atomic Save As succeeds");
      const auto source_bytes = contents(source);
      auto reopened = require(
          NativeFreedomFlightSession::open(require(native_continue(source))));
      check(reopened.document() == captured &&
                reopened.docking() == session.docking() &&
                reopened.source_save() == source,
            "ordinary Continue restores exact attachment");
      for (int n = 0; n < 240; ++n) {
        const auto step = require(session.advance({}));
        (void)require(reopened.advance({}));
        check(step.actuation.central.propulsion.applied_force_newtons ==
                      RigidVector3{} &&
                  step.actuation.central.propulsion.negative_force_newtons ==
                      RigidVector3{},
              "constrained ephemeris motion is not propulsion");
      }
      check(session.document() == reopened.document() &&
                session.document().flight.tick == 265 &&
                session.document().origin.state.tick == 265,
            "one shared clock advances attachment and history identically "
            "after reopen");
      const auto constrained = session.document();
      check(session.release_port().has_value() &&
                reopened.release_port().has_value() &&
                session.document() == constrained &&
                session.document() == reopened.document(),
            "release is same-tick, same-pose and same-momentum without "
            "withdrawal");
      auto released_path = dir / "released.json";
      check(session.save_as(released_path).has_value(),
            "released target persists");
      auto loaded = require(NativeFreedomFlightSession::open(
          require(native_continue(released_path))));
      check(loaded.document() == session.document() &&
                loaded.docking() == session.docking() &&
                !loaded.docking()->attached,
            "released Continue remains free flight with selected port");
      for (int n = 0; n < 120; ++n) {
        (void)require(session.advance(firing));
        (void)require(loaded.advance(firing));
      }
      check(session.document() == loaded.document() &&
                session.document().flight != constrained.flight,
            "released craft responds to actual normal actuators identically "
            "after reopen");
      check(contents(source) == source_bytes,
            "source attached save remains untouched");
      auto corrupt = Json::parse(source_bytes);
      corrupt["docking"]["target"]["station_id"] = "0";
      auto bad_path = dir / "bad.json";
      std::ofstream{bad_path} << corrupt.dump();
      const auto bad_bytes = contents(bad_path);
      check(!native_continue(bad_path) && contents(bad_path) == bad_bytes,
            "corrupt format19 refuses without older-save fallback or rewrite");
      std::ofstream{bad_path} << "keep destination";
      auto bad = input;
      bad.docking.attached = true;
      check(!write_freedom_docking_file_atomically(bad_path, bad) &&
                contents(bad_path) == "keep destination",
            "invalid writer preserves prior destination");
    }
  }
  const Fixture f;
  auto selected = f.document();
  auto legacy = require(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom, selected.flight, f.planet, {}}));
  auto p = dir / "original18.json";
  check(!legacy.docking() && legacy.save_as(p).has_value() &&
            contents(p) ==
                require(encode_freedom_flight_document_json(selected.flight)),
        "unselected format18 Save As retains historical bytes and meaning");
  check(legacy.select_port(2).has_value() &&
            legacy.docking()->target.ordinal == 2,
        "port selection is an explicit lifecycle state change");
}
auto approach_aid() -> void {
  const Fixture fixture{Seed{42}, 2};
  for (unsigned mutation = 0; mutation < 5; ++mutation) {
    auto d = fixture.document(1, 12);
    auto& b = d.flight.flight;
    switch (mutation) {
      case 0: b.position_metres.y += 13; break;
      case 1: b.position_metres.y -= 150; break;
      case 2: b.position_metres.x += 25; break;
      case 3: b.orientation = {}; break;
      case 4: b.linear_velocity_metres_per_second.y += 41; break;
      default: break;
    }
    auto session = fixture.session(d);
    const auto initial = session.document();
    check(!session.begin_port_approach() && session.document() == initial &&
              !session.port_approach().active,
          "wrong-side/distant/outside-column/unaligned/fast aid refuses "
          "atomically");
  }
  for (const auto seed :
       {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}}) {
    const Fixture f{seed, 2};
    for (const auto port : {1U, 2U}) {
      auto session = f.session(f.document(port, 12));
      check(session.begin_port_approach().has_value(),
            "both aligned port approaches accept");
      std::uint32_t elapsed{};
      bool used_propulsion{};
      while (session.port_approach().active &&
             elapsed < kNativePortApproachTicks &&
             require(session.assess_port()).decision !=
                 OriginDockDecision::capture_ready) {
        const auto step = require(session.advance({}));
        used_propulsion |=
            step.actuation.central.propulsion.applied_force_newtons !=
            RigidVector3{};
        ++elapsed;
      }
      check(used_propulsion && !session.docking()->attached &&
                require(session.assess_port()).decision ==
                    OriginDockDecision::capture_ready &&
                session.capture_port().has_value(),
            "both seeded port aids use real propulsion and the original "
            "capture gate");
    }
  }
}
} // namespace
auto main() -> int {
  auto dir = std::filesystem::temp_directory_path() /
             ("apsis-docking-save-" +
              std::to_string(
                  std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(dir);
  try {
    invalid();
    lifecycle(dir);
    approach_aid();
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    ++failures;
  }
  std::filesystem::remove_all(dir);
  std::cout << "Freedom docking saves: " << failures << " failures\n";
  return failures ? 1 : 0;
}
