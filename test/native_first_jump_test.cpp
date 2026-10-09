#include "apsis_drift/native_flight_session.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
int checks{}, failures{};
std::uint64_t fingerprint{14695981039346656037ULL};
auto check(bool ok, std::string_view label) -> void {
  ++checks;
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> r) -> T {
  if (!r) {
    if constexpr (std::is_same_v<E, std::string>)
      std::cerr << r.error() << '\n';
    throw std::runtime_error("Native jump fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*r);
}
auto hash(std::string_view value) -> void {
  for (char c : value) {
    fingerprint ^= static_cast<unsigned char>(c);
    fingerprint *= 1099511628211ULL;
  }
}
auto fixture(Seed seed, bool assisted = true) -> NativeFreedomFlightSession {
  // Valid saved spaceflight fixture. Existing station/boarding tests own the
  // earlier walk/seat/departure route; every jump below uses actual session
  // ticks.
  const auto system = need(generate_physical_origin_system(seed, 2));
  const auto& body = system.catalog.planets[0].descriptor;
  FreedomFlightSaveDocument flight;
  flight.origin = make_freedom_new_game_document(seed);
  flight.origin.state.tick = 240;
  flight.model.physical_catalog = 2;
  flight.model.physical_ephemeris = 2;
  flight.model.assistance = assisted;
  flight.flight.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  flight.flight.frame = {
      RigidFrameKind::planet_relative_inertial, system.catalog.id, body.id, {}};
  flight.flight.tick = 240;
  flight.flight.position_metres = {0, 0, body.radius.value * 10000.0};
  flight.world = FreedomActiveWorldSelection{1, system.catalog.id, body.id};
  FreedomResources resources{1, flight.origin.state.craft, flight.flight.craft,
                             240};
  auto knowledge = need(make_freedom_starting_knowledge(seed, 3));
  FreedomSurfaceSaveDocument surface{flight, {}};
  FreedomResourceSaveDocument fueled{std::move(surface), resources};
  FreedomKnowledgeSaveDocument mapped{std::move(fueled), std::move(knowledge)};
  FreedomTravelSaveDocument document{
      std::move(mapped), {}, NativeStartingAssemblySelection{}};
  return need(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom, document, body, {}}));
}
auto resume(const NativeFreedomFlightSession& session,
            const std::filesystem::path& path) -> NativeFreedomFlightSession {
  need(session.save_as(path));
  auto copy =
      need(NativeFreedomFlightSession::open(need(native_continue(path))));
  check(need(copy.travel_document()) == need(session.travel_document()),
        "Every saved native phase resumes exact current owner/history/binding");
  return copy;
}
auto leg(NativeFreedomFlightSession& live, const std::filesystem::path& path)
    -> void {
  need(live.begin_jump());
  auto resumed = resume(live, path);
  const auto source_tick = live.document().flight.tick;
  const auto before_charges = live.resources()->jump_charges;
  for (SimulationTick t = 1; t <= kJumpSpoolTicks + kJumpTransitTicks; ++t) {
    const auto step = need(live.advance({}));
    need(resumed.advance({}));
    if (t == 1 || t == kJumpSpoolTicks - 1 || t == kJumpSpoolTicks ||
        t == kJumpSpoolTicks + 1 ||
        t == kJumpSpoolTicks + kJumpTransitTicks - 1 ||
        t == kJumpSpoolTicks + kJumpTransitTicks)
      resumed = resume(resumed, path);
    if (t < kJumpSpoolTicks)
      check(live.resources()->jump_charges == before_charges &&
                !step.jump_committed,
            "Spool cannot bill a jump charge");
    if (t == kJumpSpoolTicks) {
      check(step.jump_committed &&
                live.resources()->jump_charges == before_charges - 1 &&
                live.travel()->committed,
            "One real charge commits atomically with frozen source/point");
      const auto frozen = *live.travel()->committed;
      check(!live.cancel_jump() && need(live.jump_preview()) == frozen.preview,
            "Committed transit cannot cancel or change preview sampling");
      check(!live.select_port(1) && !live.capture_port() &&
                !live.set_assistance(false) && !live.set_hold({}) &&
                !live.set_landing_gear(true),
            "Transit refuses conflicting control/constraint mutations");
    }
    if (t > kJumpSpoolTicks)
      check(step.fuel_debit_quanta == 0 &&
                live.resources()->flight_quanta ==
                    live.travel()->commitment_resources->flight_quanta,
            "Transit advances clocks with no hidden flight-fuel charge");
    if (t == kJumpSpoolTicks + kJumpTransitTicks)
      check(step.jump_arrived,
            "Actual session performs scheduled physical handoff");
  }
  check(live.document().flight.tick ==
                source_tick + kJumpSpoolTicks + kJumpTransitTicks &&
            need(live.travel_document()) == need(resumed.travel_document()),
        "Uninterrupted and all-boundary resumed native leg converge exactly");
  const auto id = live.system().catalog.id;
  const auto presence = need(query_freedom_knowledge(
      *live.knowledge(), {KnowledgeSubjectKind::system, id, id.value},
      KnowledgeFact::presence, live.document().flight.tick));
  check(presence &&
            presence->provenance.level == NavigationKnowledgeLevel::visited &&
            !live.docking() && !live.walker(),
        "Actual arrival visits system and retains free physical craft");
  check(live.craft_binding().selection() != nullptr,
        "Authored Wayfarer binding survives physical handoff and resume");
}
auto roundtrip(Seed seed, bool assisted, const std::filesystem::path& path)
    -> void {
  auto live = fixture(seed, assisted);
  const auto ids = generate_first_intersystem_identities(seed);
  const auto original = need(live.travel_document());
  check(!live.select_jump({0}) && need(live.travel_document()) == original,
        "Unknown selection preserves exact session");
  need(live.select_jump(ids.target_system));
  live = resume(live, path);
  need(live.begin_jump());
  need(live.advance({}));
  need(live.cancel_jump());
  check(live.resources()->jump_charges == 3 &&
            live.travel()->next_attempt == 1 && !live.travel()->committed,
        "Canceled actual spool consumes no charge or random attempt");
  leg(live, path);
  check(live.system().catalog.id == ids.target_system &&
            live.document().world->system == ids.target_system &&
            live.resources()->jump_charges == 2,
        "Outbound installs actual seeded neighbor with two charges");
  const auto before_service = need(live.travel_document());
  check(!live.replenish_resources() && !live.select_port(1) &&
            need(live.travel_document()) == before_service,
        "Neighbor has no phantom origin attachment/free service");
  need(live.select_jump(ids.origin_system));
  leg(live, path);
  check(live.system().catalog.id == ids.origin_system &&
            live.resources()->jump_charges == 1 &&
            live.travel()->next_attempt == 3,
        "Physical return spends second charge and leaves one reserve");
  hash(need(encode_freedom_travel_document_json(need(live.travel_document()))));
}
auto invalid(const std::filesystem::path& path) -> void {
  auto live = fixture({42});
  need(live.select_jump(
      generate_first_intersystem_identities({42}).target_system));
  need(live.begin_jump());
  const auto before = need(live.travel_document());
  NativeFlightControls bad;
  bad.positive_translation.x = std::numeric_limits<double>::quiet_NaN();
  check(!live.advance(bad) && need(live.travel_document()) == before,
        "Nonfinite controls preserve active source/phase/resources");
  check(!live.advance({}, SimulationSeconds{.1}) &&
            need(live.travel_document()) == before,
        "Nonfixed jump tick preserves full state");
  for (SimulationTick t = 0; t < kJumpSpoolTicks; ++t)
    need(live.advance({}));
  const auto root = Json::parse(
      need(encode_freedom_travel_document_json(need(live.travel_document()))));
  const auto refuses = [&](Json j) {
    check(!decode_freedom_travel_document_json(j.dump()),
          "Corrupt committed save refuses before state replacement");
  };
  auto corrupt = root;
  corrupt["travel"]["committed"]["point"][0] = 0.;
  refuses(corrupt);
  corrupt = root;
  corrupt["travel"]["committed"]["sample_seed"] = 0U;
  refuses(corrupt);
  corrupt = root;
  corrupt["travel"]["next_attempt"] = 99U;
  refuses(corrupt);
  corrupt = root;
  corrupt["travel"]["phase"] = 255U;
  refuses(corrupt);
  corrupt = root;
  corrupt["travel"]["commitment_resources"]["jump_charges"] = 0U;
  refuses(corrupt);
  corrupt = root;
  corrupt["craft_binding"] = nullptr;
  refuses(corrupt);
  corrupt = root;
  corrupt["travel"]["spool_tick"] = std::numeric_limits<std::uint64_t>::max();
  refuses(corrupt);
  check(!decode_freedom_travel_document_json(std::string(65, '[') + "0" +
                                             std::string(65, ']')),
        "Excessive root nesting is bounded before JSON parse");
  live = resume(live, path);
}
auto station_selection(const std::filesystem::path& path) -> void {
  auto live =
      need(NativeFreedomFlightSession::open(need(native_new_game({42}))));
  const auto old = live.document();
  need(live.select_jump(
      generate_first_intersystem_identities({42}).target_system));
  check(live.walker() && live.docking()->attached && !live.jump_available() &&
            live.document().flight == old.flight &&
            live.resources()->jump_charges == 3,
        "Selecting on station opts into travel without moving or authorizing "
        "departure");
  live = resume(live, path);
  need(live.advance_walk({}));
  live = resume(live, path);
  check(live.walker() && live.docking()->attached && !live.begin_jump(),
        "Travel Save/Continue preserves the real walking/attachment owner");
}
} // namespace
auto main() -> int {
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("apsis-native-jump-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    std::filesystem::create_directories(directory);
    invalid(directory / "phase.json");
    station_selection(directory / "phase.json");
    for (auto seed :
         {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}})
      for (bool assisted : {true, false})
        roundtrip(seed, assisted, directory / "phase.json");
    std::filesystem::remove_all(directory);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    std::filesystem::remove_all(directory);
    return 1;
  }
  check(fingerprint == 13968603585411398936ULL,
        "Three-seed dual-profile physical round-trip golden remains exact");
  std::cout << checks << " native jump checks; fingerprint " << fingerprint
            << '\n';
  return failures ? 1 : 0;
}
