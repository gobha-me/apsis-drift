#include "apsis_drift/native_flight_session.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <tuple>
#include <type_traits>

namespace {
using namespace apsis_drift;
int checks{}, failures{};
std::uint64_t fingerprint{1469598103934665603ULL};
auto check(bool ok, std::string_view name) -> void {
  ++checks;
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << name << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> r) -> T {
  if (!r) {
    if constexpr (std::is_same_v<E, std::string>)
      std::cerr << r.error() << '\n';
    throw std::runtime_error("Local observation fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*r);
}
auto hash(std::string_view text) -> void {
  for (char c : text) {
    fingerprint ^= static_cast<unsigned char>(c);
    fingerprint *= 1099511628211ULL;
  }
}
struct Fixture {
  Seed seed;
  PhysicalLocalSystem system;
  FreedomKnowledge baseline;
  explicit Fixture(Seed s)
      : seed(s), system(need(generate_physical_origin_system(s, 2))),
        baseline(need(make_freedom_starting_knowledge(s, 2))) {}
  auto subject(unsigned ordinal = 0) const -> KnowledgeSubject {
    return {KnowledgeSubjectKind::planet, system.catalog.id,
            system.catalog.planets[ordinal].descriptor.id.value};
  }
  auto voyage(double radii, SimulationTick tick, unsigned ordinal = 0) const
      -> FreedomSurfaceSaveDocument {
    auto origin = make_freedom_new_game_document(seed);
    origin.state.tick = tick;
    const auto home = system.catalog.planets[0].descriptor.id;
    const auto host = need(resolve_planet_ephemeris(system, home, {tick, 0}));
    const auto body = need(resolve_planet_ephemeris(
        system, system.catalog.planets[ordinal].descriptor.id, {tick, 0}));
    const double radius =
        system.catalog.planets[ordinal].descriptor.radius.value * 1000.0;
    RigidBodyState state;
    state.craft = {kWayfarerFrameId, 1};
    state.frame = {
        RigidFrameKind::planet_relative_inertial, system.catalog.id, home, {}};
    state.tick = tick;
    state.position_metres = {
        (body.position.x - host.position.x) + radius * radii,
        body.position.y - host.position.y, body.position.z - host.position.z};
    FreedomFlightModel model;
    model.physical_catalog = 2;
    model.physical_ephemeris = 2;
    return {FreedomFlightSaveDocument{origin, state, model}, {}};
  }
  auto reading(const FreedomKnowledge& k, KnowledgeFact fact,
               SimulationTick tick, unsigned ordinal = 0) const
      -> std::optional<KnowledgeReading> {
    return need(query_freedom_knowledge(k, subject(ordinal), fact, tick));
  }
};
auto transitions(const Fixture& f) -> void {
  auto k = f.baseline;
  const auto immutable = f.system;
  for (const auto& [radius, tick, expected] :
       {std::tuple{100.0, SimulationTick{120},
                   NavigationKnowledgeLevel::contact},
        {10.0, SimulationTick{240}, NavigationKnowledgeLevel::probable},
        {2.0, SimulationTick{360}, NavigationKnowledgeLevel::resolved}}) {
    const auto before = need(encode_freedom_knowledge_json(k, tick));
    const auto voyage = f.voyage(radius, tick);
    check(observe_freedom_local_ship(k, voyage).has_value(),
          "Exact proximity boundary observes");
    k = need(observe_freedom_local_ship(k, voyage));
    check(f.reading(k, KnowledgeFact::physical, tick)->provenance.level ==
              expected,
          "Independent physical confidence follows actual distance");
    check(need(observe_freedom_local_ship(k, voyage)) == k,
          "Repeated threshold tick is idempotent");
    check(need(decode_freedom_knowledge_json(
              need(encode_freedom_knowledge_json(k, tick)), tick)) == k,
          "Immediately after threshold save/resume retains provenance");
    const auto resumed = need(decode_freedom_knowledge_json(before, tick));
    check(need(observe_freedom_local_ship(resumed, voyage)) == k,
          "Immediately before threshold resume converges");
    hash(need(encode_freedom_knowledge_json(k, tick)));
  }
  check(f.reading(k, KnowledgeFact::hazards, 360)->provenance.level ==
            NavigationKnowledgeLevel::probable,
        "Orbital resolution cannot certify surface envelopes");
  k = need(observe_freedom_local_ship(k, f.voyage(1.01, 480)));
  check(f.reading(k, KnowledgeFact::hazards, 480)->provenance.level ==
                NavigationKnowledgeLevel::resolved &&
            f.reading(k, KnowledgeFact::landing, 480)->provenance.level ==
                NavigationKnowledgeLevel::resolved &&
            !f.reading(k, KnowledgeFact::presence, 480),
        "Close sensing infers coarse models without awarding a landing visit");
  const auto home_presence = need(
      query_freedom_knowledge(k,
                              {KnowledgeSubjectKind::system,
                               f.system.catalog.id, f.system.catalog.id.value},
                              KnowledgeFact::presence, 480));
  check(home_presence && home_presence->provenance.tick == 120 &&
            home_presence->provenance.source ==
                KnowledgeSource::physical_arrival,
        "Actual free flight visits home once with original source tick");
  check(f.system == immutable &&
            f.baseline == need(make_freedom_starting_knowledge(f.seed, 2)),
        "Observation never changes bodies or starting baseline");
  for (double limit : {100.0, 10.0, 2.0}) {
    const auto exact =
        need(observe_freedom_local_ship(f.baseline, f.voyage(limit, 120)));
    auto outside_voyage = f.voyage(limit, 120);
    auto& position = std::get<FreedomFlightSaveDocument>(outside_voyage.base)
                         .flight.position_metres.x;
    position =
        std::nextafter(position, std::numeric_limits<double>::infinity());
    const auto outside =
        need(observe_freedom_local_ship(f.baseline, outside_voyage));
    const auto a = f.reading(exact, KnowledgeFact::physical, 120);
    const auto b = f.reading(outside, KnowledgeFact::physical, 120);
    check(a && (!b || b->provenance.level < a->provenance.level),
          "One binary64 step outside distance boundary cannot gain stronger "
          "confidence");
  }
  check(need(observe_freedom_local_ship(f.baseline, f.voyage(2, 119))) ==
            f.baseline,
        "Only selected authoritative cadence ticks sample");
  auto rotated = f.voyage(2, 120);
  std::get<FreedomFlightSaveDocument>(rotated.base).flight.orientation = {0, 0,
                                                                          1, 0};
  check(need(observe_freedom_local_ship(f.baseline, rotated)) ==
            need(observe_freedom_local_ship(f.baseline, f.voyage(2, 120))),
        "Authoritative attitude cannot make a camera into a discovery sensor");
  if (f.system.catalog.planets.size() > 1) {
    auto flyby =
        need(observe_freedom_local_ship(f.baseline, f.voyage(10, 120, 1)));
    const auto foreign = f.reading(flyby, KnowledgeFact::physical, 120, 1);
    check(foreign &&
              foreign->provenance.level == NavigationKnowledgeLevel::probable &&
              std::holds_alternative<std::monostate>(foreign->value),
          "Off-route flyby discovers an existing unintended body with redacted "
          "measurements");
    hash(need(encode_freedom_knowledge_json(flyby, 120)));
  }
}
auto invalid(const Fixture& f) -> void {
  const auto valid = f.voyage(2, 120);
  auto bad = valid;
  std::get<FreedomFlightSaveDocument>(bad.base).flight.position_metres.x =
      std::numeric_limits<double>::quiet_NaN();
  check(!observe_freedom_local_ship(f.baseline, bad),
        "Non-finite pose refuses before observation");
  auto k = f.baseline;
  ++k.recipe.observation_policy;
  check(!observe_freedom_local_ship(k, valid),
        "Unsupported explicit sensor policy refuses");
  check(!observe_freedom_local_ship(
            need(make_freedom_starting_knowledge(f.seed)), valid),
        "Recipe1 is never implicitly upgraded");
  k = f.baseline;
  ++k.recipe.universe_seed.value;
  check(!observe_freedom_local_ship(k, valid),
        "Knowledge/physical owner mismatch refuses");
  check(!observe_freedom_local_ship(f.baseline, valid,
                                    LocalObservationEvent::touchdown) &&
            !observe_freedom_local_ship(f.baseline, valid,
                                        LocalObservationEvent::port_capture) &&
            !observe_freedom_local_ship(
                f.baseline, valid, static_cast<LocalObservationEvent>(255)),
        "Invented landing/port/unknown events refuse transactionally");
  auto encoded = need(encode_freedom_knowledge_json(f.baseline, 120));
  auto root = nlohmann::ordered_json::parse(encoded);
  root["recipe"].erase("observation_policy");
  check(!decode_freedom_knowledge_json(root.dump(), 120),
        "Recipe2 requires explicit sensor selection");
  auto overflow = valid;
  auto& overflow_flight = std::get<FreedomFlightSaveDocument>(overflow.base);
  overflow_flight.flight.tick = std::numeric_limits<SimulationTick>::max();
  overflow_flight.origin.state.tick = overflow_flight.flight.tick;
  check(!observe_freedom_local_ship(f.baseline, overflow),
        "Overflow clock refuses");
}
auto native(const Fixture& f, const std::filesystem::path& dir) -> void {
  const auto voyage = f.voyage(1.01, 119);
  const auto flight = surface_base_flight(voyage.base);
  const FreedomResources fuel{1, flight.origin.state.craft, flight.flight.craft,
                              119};
  const FreedomKnowledgeSaveDocument save{{voyage, fuel}, f.baseline};
  const auto path = dir / "selected.json";
  need(write_freedom_knowledge_file_atomically(path, save));
  auto live =
      need(NativeFreedomFlightSession::open(need(native_continue(path))));
  const auto initial = *live.knowledge();
  const auto before = live.document();
  for (unsigned query = 0; query < 3; ++query)
    need(live.observe());
  check(live.knowledge() == std::optional{initial},
        "Read-only cockpit observation cannot write knowledge");
  need(live.advance({}));
  check(live.document().flight.tick == 120 &&
            live.knowledge() != std::optional{initial} &&
            live.resources()->tick == 120,
        "Actual committed flight transaction writes timed physical evidence");
  const auto observed = *live.knowledge();
  const auto state = live.document();
  const auto resources = live.resources();
  NativeFlightControls bad;
  bad.positive_translation.x = std::numeric_limits<double>::quiet_NaN();
  check(!live.advance(bad) && live.knowledge() == std::optional{observed} &&
            live.document() == state && live.resources() == resources,
        "Failed flight cannot partially commit sensors/resources");
  need(live.save_as(path));
  auto resumed =
      need(NativeFreedomFlightSession::open(need(native_continue(path))));
  check(resumed.knowledge() == live.knowledge() &&
            resumed.document() == state && resumed.resources() == resources,
        "Real SaveAs/Continue preserves observed ledger without re-observing");
  for (unsigned tick = 0; tick < 120; ++tick) {
    need(live.advance({}));
    need(resumed.advance({}));
  }
  check(resumed.knowledge() == live.knowledge() &&
            resumed.document() == live.document(),
        "Continuous and resumed actual ticks converge across sampling cadence");
  auto old = save;
  old.knowledge = need(make_freedom_starting_knowledge(f.seed));
  need(write_freedom_knowledge_file_atomically(path, old));
  auto legacy =
      need(NativeFreedomFlightSession::open(need(native_continue(path))));
  need(legacy.advance({}));
  check(legacy.knowledge() == std::optional{old.knowledge} &&
            legacy.document() == state && before.flight.tick == 119,
        "Version1 advances identical physics while retaining its unselected "
        "ledger");
  auto station =
      need(NativeFreedomFlightSession::open(need(native_new_game(f.seed))));
  const auto starting = *station.knowledge();
  for (unsigned tick = 0; tick < 120; ++tick)
    need(station.advance_walk({}));
  check(station.knowledge() == std::optional{starting},
        "Walking in station does not secretly operate ship sensors");
}
} // namespace
auto main() -> int {
  const auto dir =
      std::filesystem::temp_directory_path() /
      ("apsis-observations-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(dir);
  try {
    for (const Seed seed : {Seed{0}, Seed{42}, Seed{UINT64_MAX}}) {
      const Fixture f{seed};
      transitions(f);
      invalid(f);
    }
    native(Fixture{Seed{42}}, dir);
    check(
        fingerprint == 13663229935463235552ULL,
        "Fixed local observation sequence retains its canonical ledger golden");
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << e.what() << '\n';
  }
  std::filesystem::remove_all(dir);
  std::cout << checks << " observation checks, " << failures
            << " failures; fingerprint " << fingerprint << '\n';
  return failures ? 1 : 0;
}
