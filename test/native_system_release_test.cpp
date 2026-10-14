#include "apsis_drift/native_flight_session.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
using V = RigidVector3;
int checks{}, failures{};
auto check(bool accepted, std::string_view message) -> void {
  ++checks;
  if (!accepted) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E>
auto need(std::expected<T, E> value,
          std::source_location location = std::source_location::current())
    -> T {
  if (!value) {
    std::string message =
        "System release fixture at line " + std::to_string(location.line());
    if constexpr (std::is_same_v<E, std::string>)
      message += ": " + value.error();
    else if constexpr (requires { value.error().detail; })
      message += ": " + value.error().detail;
    throw std::runtime_error(message);
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto read(const std::filesystem::path& path) -> std::string {
  std::ifstream stream{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{stream}, {}};
}
auto write(const std::filesystem::path& path, std::string_view bytes) -> void {
  std::ofstream stream{path, std::ios::binary};
  stream << bytes;
  if (!stream) throw std::runtime_error("Cannot write release checkpoint");
}
auto bytes(const NativeFreedomFlightSession& session) -> std::string {
  return need(encode_native_save_document_json(need(session.save_document())));
}
auto reopen(const NativeFreedomFlightSession& session)
    -> NativeFreedomFlightSession {
  const auto original = bytes(session);
  auto result =
      need(NativeFreedomFlightSession::open(need(native_select_save_document(
          need(decode_native_save_document_json(original))))));
  check(bytes(result) == original,
        "Complete selected voyage survives exact Save/Continue");
  return result;
}
auto same(double a, double b) -> bool {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
auto same(V a, V b) -> bool {
  return same(a.x, b.x) && same(a.y, b.y) && same(a.z, b.z);
}
auto same(RigidOrientation a, RigidOrientation b) -> bool {
  return same(a.w, b.w) && same(a.x, b.x) && same(a.y, b.y) && same(a.z, b.z);
}
auto close(double actual, double expected, double offset) -> bool {
  return std::abs(actual - expected) <=
         4 * std::numeric_limits<double>::epsilon() *
             std::max({1.0, std::abs(expected), std::abs(offset)});
}
auto close(V actual, V expected, V offset) -> bool {
  return close(actual.x, expected.x, offset.x) &&
         close(actual.y, expected.y, offset.y) &&
         close(actual.z, expected.z, offset.z);
}
auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto subtract(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto host_position(const PlanetEphemeris& host) -> V {
  return {host.position.x, host.position.y, host.position.z};
}
auto host_velocity(const PlanetEphemeris& host) -> V {
  return {host.velocity.x, host.velocity.y, host.velocity.z};
}
auto selected_start() -> NativeStartup {
  auto selected = need(native_new_game({42}));
  auto& knowledge = std::get<FreedomKnowledgeSaveDocument>(selected.document);
  auto& assembly = std::get<FreedomStartingAssemblySaveDocument>(
      knowledge.voyage.voyage.base);
  auto& flight = assembly.journey.voyage.flight;
  check(flight.model.frame_policy == kFreedomRelativeFramePolicy,
        "Ordinary New Game retains historical relative frame policy");
  flight.model.frame_policy = kFreedomReleaseSystemFramePolicy;
  flight.world = FreedomActiveWorldSelection{1, flight.flight.frame.system,
                                             *flight.flight.frame.planet};
  // Explicit world ownership selects the existing wider knowledge domain;
  // retain all granted entries and provenance while selecting its recipe.
  knowledge.knowledge.recipe.version = kFreedomTravelKnowledgeVersion;
  knowledge.knowledge.recipe.world_domain = 1;
  return selected;
}
auto selected_flight(NativeStartup& selected) -> FreedomFlightSaveDocument& {
  return std::get<FreedomStartingAssemblySaveDocument>(
             std::get<FreedomKnowledgeSaveDocument>(selected.document)
                 .voyage.voyage.base)
      .journey.voyage.flight;
}
auto invalid_selection() -> void {
  auto selected = selected_start();
  const auto original =
      need(encode_native_save_document_json(selected.document));
  const auto owner =
      need(hydrate_freedom_flight_document(selected_flight(selected))).system;
  const auto refuses = [&](NativeStartup bad) {
    check(!NativeFreedomFlightSession::open(std::move(bad)) &&
              need(encode_native_save_document_json(selected.document)) ==
                  original,
          "Invalid selected owner/model/clock refuses without changing source");
  };
  std::optional<NativeStartup> bad{selected};
  selected_flight(*bad).world.reset();
  refuses(std::move(*bad));
  bad.emplace(selected);
  selected_flight(*bad).world->planet = PlanetId{0};
  refuses(std::move(*bad));
  bad.emplace(selected);
  selected_flight(*bad).world->system = SystemId{0};
  refuses(std::move(*bad));
  bad.emplace(selected);
  selected_flight(*bad).model.frame_policy = 99;
  refuses(std::move(*bad));
  bad.emplace(selected);
  ++selected_flight(*bad).origin.state.tick;
  refuses(std::move(*bad));
  bad.emplace(selected);
  selected_flight(*bad).flight.frame.planet.reset();
  refuses(std::move(*bad));
  bad.emplace(selected);
  selected_flight(*bad).flight.position_metres.x =
      std::numeric_limits<double>::quiet_NaN();
  refuses(std::move(*bad));
  bad.emplace(selected);
  selected_flight(*bad).flight.linear_velocity_metres_per_second.z =
      std::numeric_limits<double>::infinity();
  refuses(std::move(*bad));
  bad.emplace(selected);
  selected_flight(*bad).flight.position_metres.y =
      kRigidBodyMaximumPositionMetres * 2;
  refuses(std::move(*bad));
  auto foreign = need(generate_physical_origin_system({43}, 2));
  check(!relative_freedom_flight_state(selected_flight(selected), foreign),
        "Relative observation refuses another complete physical owner");
  auto flight = selected_flight(selected);
  flight.flight.angular_velocity_radians_per_second.y =
      std::numeric_limits<double>::quiet_NaN();
  check(!relative_freedom_flight_state(flight, owner),
        "Relative observation cannot hide nonfinite canonical state");
  flight = selected_flight(selected);
  flight.flight.tick = std::numeric_limits<SimulationTick>::max() - 1;
  flight.origin.state.tick = flight.flight.tick;
  check(!relative_freedom_flight_state(flight, owner),
        "Unadvanceable shared clock refuses before relative observation");
}
auto independent_relative(const NativeFreedomFlightSession& session)
    -> RigidBodyState {
  auto result = session.document().flight;
  const auto& selection = *session.document().world;
  const auto host = need(resolve_planet_ephemeris(
      session.system(), selection.planet, {result.tick, 0}));
  result.frame = {RigidFrameKind::planet_relative_inertial,
                  selection.system,
                  selection.planet,
                  {}};
  result.position_metres =
      subtract(result.position_metres, host_position(host));
  result.linear_velocity_metres_per_second =
      subtract(result.linear_velocity_metres_per_second, host_velocity(host));
  return need(canonicalize_rigid_body_state({session.system()}, result));
}
auto check_system_oracle(const NativeFreedomFlightSession& session,
                         const RigidBodyState& relative) -> void {
  const auto& actual = session.document().flight;
  const auto host = need(resolve_planet_ephemeris(
      session.system(), *relative.frame.planet, {relative.tick, 0}));
  // Independent final-tick arithmetic: never call the forward handoff to
  // construct this oracle. The established relative integrator owns forces.
  check(actual.frame == RigidCoordinateFrame{RigidFrameKind::system_inertial,
                                             relative.frame.system,
                                             {},
                                             {}} &&
            actual.tick == relative.tick && actual.craft == relative.craft &&
            same(actual.orientation, relative.orientation) &&
            same(actual.angular_velocity_radians_per_second,
                 relative.angular_velocity_radians_per_second) &&
            close(actual.position_metres,
                  add(relative.position_metres, host_position(host)),
                  host_position(host)) &&
            close(actual.linear_velocity_metres_per_second,
                  add(relative.linear_velocity_metres_per_second,
                      host_velocity(host)),
                  host_velocity(host)),
        "Canonical system state equals final ephemeris plus relative dynamics; "
        "tick/craft/attitude/body spin retain exact bits");
  check(need(session.relative_state()) == independent_relative(session),
        "Shared relative adapter equals independent same-tick subtraction");
}
auto checkpoint(const NativeFreedomFlightSession& session,
                const std::filesystem::path& output, std::string_view name,
                Json& manifest) -> void {
  const auto path = output / (std::string{name} + ".json");
  need(session.save_as(path));
  const auto& document = session.document();
  const auto relative = need(session.relative_state());
  manifest["checkpoints"].push_back(
      {{"name", name},
       {"file", path.filename().string()},
       {"tick", std::to_string(document.flight.tick)},
       {"checksum", std::to_string(need(rigid_body_state_checksum(
                        {session.system()}, document.flight)))},
       {"attached", session.docking()->attached},
       {"flight_quanta", std::to_string(session.resources()->flight_quanta)},
       {"canonical", Json::parse(need(encode_rigid_body_state_json(
                         {session.system()}, document.flight)))},
       {"relative", Json::parse(need(encode_rigid_body_state_json(
                        {session.system()}, relative)))}});
}
auto refused_commands(NativeFreedomFlightSession& session) -> void {
  const auto before = bytes(session);
  const auto approach = session.port_approach();
  const auto maneuver = session.surface_maneuver();
  const auto refuses = [&](auto action, std::string_view message) {
    check(!action() && bytes(session) == before &&
              session.port_approach() == approach &&
              session.surface_maneuver() == maneuver,
          message);
  };
  NativeFlightControls bad;
  bad.positive_translation.x = std::numeric_limits<double>::quiet_NaN();
  refuses([&] { return session.advance(bad); },
          "Nonfinite system control preserves complete voyage");
  refuses([&] { return session.advance({}, SimulationSeconds{.1}); },
          "Nonfixed system clock step preserves complete voyage");
  refuses([&] { return session.set_landing_gear(true); },
          "System gear command refuses transactionally");
  refuses([&] { return session.request_landing(); },
          "System landing aid refuses transactionally");
  const auto checksum = need(
      rigid_body_state_checksum({session.system()}, session.document().flight));
  refuses([&] { return session.commit_touchdown(checksum); },
          "System contact command refuses transactionally");
  refuses([&] { return session.release_surface(); },
          "System surface release refuses transactionally");
  refuses([&] { return session.enable_impact_consequences(); },
          "System impact selection refuses transactionally");
  refuses([&] { return session.begin_jump(); },
          "System active FTL commitment refuses transactionally");
  refuses([&] { return session.begin_port_approach(); },
          "System approach aid refuses transactionally");
  FreedomSurfaceWalkSaveDocument outside{
      need(session.recovery_document()), 1, {}};
  const auto inside = need(encode_freedom_surface_walk_document_json(outside));
  outside.actor.emplace();
  outside.actor->frame = {RigidFrameKind::planet_fixed,
                          session.document().world->system,
                          session.document().world->planet,
                          {}};
  check(!validate_freedom_surface_walk_document(outside) &&
            !encode_freedom_surface_walk_document_json(outside) &&
            bytes(session) == before,
        "Present ground actor with canonical system flight refuses safely "
        "without changing complete voyage");
  auto malformed = Json::parse(inside);
  malformed["actor"] = {{"version", kPlanetSurfaceWalkerVersion},
                        {"actor_id", 1U},
                        {"system", session.document().world->system.value},
                        {"planet", session.document().world->planet.value},
                        {"foot_position_metres", {0., 0., 0.}},
                        {"velocity_metres_per_second", {0., 0., 0.}},
                        {"heading_radians", 0.}};
  check(!decode_freedom_surface_walk_document_json(malformed.dump()) &&
            !decode_native_save_document_json(malformed.dump()) &&
            bytes(session) == before,
        "Format30 system-flight/ground-actor corruption refuses direct and "
        "native decode without dereferencing absent planet");
}
auto run(const std::filesystem::path& output) -> void {
  // Invalid inputs precede the real station/boarding/flight route.
  invalid_selection();
  auto station = need(NativeFreedomFlightSession::open(selected_start()));
  for (unsigned tick = 0; tick < 2960; ++tick)
    need(station.advance_walk({0, -1, 0}));
  need(station.begin_boarding());
  for (unsigned tick = 0; tick < kGameplayBoardingTicks; ++tick)
    need(station.advance_walk({}));
  check(station.boarding()->phase == FreedomBoardingPhase::seated &&
            !station.walker() && station.docking()->attached,
        "Selected route uses actual walking and completed boarding");
  need(station.set_assistance(false));
  need(station.select_jump(
      generate_first_intersystem_identities({42}).target_system));
  station =
      need(NativeFreedomFlightSession::open(need(native_select_save_document(
          NativeSaveDocument{need(station.recovery_document())}))));
  check(station.travel()->phase == FreedomJumpPhase::idle &&
            station.travel()->selected.has_value() &&
            std::holds_alternative<FreedomRecoverySaveDocument>(
                need(station.save_document())),
        "Actual selected starter retains complete recovery/travel/boarding "
        "chain");
  const auto pilot = station.boarding();
  const auto assembly = station.starting_assembly();
  const auto origin = station.document().origin;
  const auto binding = station.craft_binding();
  auto blocked = station;
  need(blocked.enable_impact_consequences());
  const auto selected_impact = bytes(blocked);
  check(!blocked.release_port() && bytes(blocked) == selected_impact &&
            blocked.docking()->attached && blocked.condition(),
        "Selected relative impacts prevent release without dropping condition");

  NativeFlightControls held;
  held.positive_translation.x = .005;
  held.negative_translation.y = .025;
  held.negative_translation.z = .01;
  held.positive_rotation.y = .0001;
  NativeFlightControls braking;
  // Three withdrawal ticks deliver 3*.025*144000 force-ticks. One
  // .06*208000 positive-Y tick arrests that outward motion with a small
  // inward remainder, while the real body remains outside the collar.
  braking.positive_translation.y = .06;
  Json manifest = {
      {"schema", "apsis.native-system-release-fixtures/1"},
      {"seed", "42"},
      {"scope", "Explicit policy2 complete station release, ordinary held "
                "relative-force integration, exact Save/Continue and capture; "
                "not whole #200 qualification"},
      {"frame_policy", kFreedomReleaseSystemFramePolicy},
      {"checkpoints", Json::array()},
      {"steps",
       {{{"ticks", 3},
         {"positive_translation", {.005, 0., 0.}},
         {"negative_translation", {0., .025, .01}},
         {"positive_rotation", {0., .0001, 0.}},
         {"negative_rotation", {0., 0., 0.}}},
        {{"ticks", 2},
         {"positive_translation", {0., 0., 0.}},
         {"negative_translation", {0., 0., 0.}},
         {"positive_rotation", {0., 0., 0.}},
         {"negative_rotation", {0., 0., 0.}}},
        {{"ticks", 1},
         {"positive_translation", {0., .06, 0.}},
         {"negative_translation", {0., 0., 0.}},
         {"positive_rotation", {0., 0., 0.}},
         {"negative_rotation", {0., 0., 0.}}}}},
      {"oracle", "Existing relative atmospheric integrator, then independent "
                 "same-final-tick host ephemeris addition; p/v tolerance "
                 "4*binary64_epsilon*max(1,abs(expected),abs(host_component)); "
                 "exact craft/tick/quaternion/body spin and complete saves"}};
  checkpoint(station, output, "attached", manifest);
  const auto source_path = output / "attached.json";
  const auto source_bytes = read(source_path);
  auto live = need(
      NativeFreedomFlightSession::open(need(native_continue(source_path))));
  const auto attached = live.document().flight;
  need(live.release_port());
  check(!live.docking()->attached &&
            live.document().flight.tick == attached.tick,
        "Actual release changes attachment/frame without advancing clock");
  check_system_oracle(live, attached);
  const auto released_relative = need(live.relative_state());
  const auto host = need(resolve_planet_ephemeris(
      live.system(), *attached.frame.planet, {attached.tick, 0}));
  check(
      close(released_relative.position_metres, attached.position_metres,
            host_position(host)) &&
          close(released_relative.linear_velocity_metres_per_second,
                attached.linear_velocity_metres_per_second,
                host_velocity(host)),
      "Release roundtrip error remains within documented ephemeris tolerance");
  checkpoint(live, output, "released", manifest);
  refused_commands(live);
  auto resumed = reopen(live);
  for (unsigned tick = 0; tick < 6; ++tick) {
    const auto controls = tick < 3    ? held
                          : tick == 5 ? braking
                                      : NativeFlightControls{};
    if (tick == 5)
      check(need(live.assess_port()).decision == OriginDockDecision::retreating,
            "Actual withdrawal must be braked before physical capture");
    auto expected = independent_relative(live);
    const VacuumIntent intent{
        controls.positive_translation, controls.negative_translation,
        controls.positive_rotation, controls.negative_rotation, false};
    const auto normal = need(advance_atmospheric_flight(
        {live.system()}, expected, intent, live.rotation(),
        live.document().model.atmosphere, live.document().model.central));
    const auto ledger = *live.resources();
    const auto step = need(live.advance(controls));
    need(resumed.advance(controls));
    check_system_oracle(live, expected);
    check(step.actuation.central.propulsion.positive_force_newtons ==
                  normal.central.propulsion.positive_force_newtons &&
              step.actuation.central.propulsion.negative_force_newtons ==
                  normal.central.propulsion.negative_force_newtons &&
              step.actuation.central.propulsion.positive_torque_newton_metres ==
                  normal.central.propulsion.positive_torque_newton_metres &&
              !step.propulsion_refused &&
              live.resources()->flight_quanta ==
                  ledger.flight_quanta - step.fuel_debit_quanta &&
              ((tick >= 3 && tick < 5) || step.fuel_debit_quanta > 0),
          "System adapter retains actual gross channels and exact fuel debit");
    check(bytes(live) == bytes(resumed),
          "Uninterrupted and reopened system ticks retain every outer owner");
    if (tick == 1) resumed = reopen(resumed);
  }
  check(live.document().flight.orientation != RigidOrientation{} &&
            live.document().flight.angular_velocity_radians_per_second != V{},
        "Held real rotation exercises nonidentity quaternion and nonzero spin");
  checkpoint(live, output, "stepped", manifest);
  check(
      live.boarding() == pilot && live.starting_assembly() == assembly &&
          live.craft_binding().selection() != nullptr &&
          *live.craft_binding().selection() == *binding.selection() &&
          live.document().origin.recipe == origin.recipe &&
          live.document().origin.lineage == origin.lineage &&
          live.document().origin.state.discoveries ==
              origin.state.discoveries &&
          live.document().origin.state.world_deltas ==
              origin.state.world_deltas,
      "Real system continuation preserves craft/pilot/assembly/origin history");
  check(need(live.assess_port()).decision == OriginDockDecision::capture_ready,
        "Real positive-Y braking returns actual motion to the port capture "
        "envelope");
  const auto capture_tick = live.document().flight.tick;
  need(live.capture_port());
  need(resumed.capture_port());
  check(live.docking()->attached &&
            live.document().flight.frame.kind ==
                RigidFrameKind::planet_relative_inertial &&
            live.document().flight.tick == capture_tick &&
            live.boarding() == pilot && bytes(live) == bytes(resumed),
        "Actual capture commits relative constrained body and complete seated "
        "voyage");
  live = reopen(live);
  checkpoint(live, output, "captured", manifest);
  check(
      read(source_path) == source_bytes,
      "Release/ticks/capture/Save/Continue never rewrite attached source file");
  write(output / "manifest.json", manifest.dump(2) + "\n");
}
} // namespace

auto main(int argc, char** argv) -> int {
  const bool fixtures = argc == 3 && std::string_view{argv[1]} == "--fixtures";
  if (argc != 1 && !fixtures) {
    std::cerr << "usage: native-system-release-tests [--fixtures DIRECTORY]\n";
    return 2;
  }
  const auto output = fixtures
                          ? std::filesystem::absolute(argv[2])
                          : std::filesystem::temp_directory_path() /
                                ("apsis-native-system-release-" +
                                 std::to_string(std::chrono::steady_clock::now()
                                                    .time_since_epoch()
                                                    .count()));
  try {
    std::filesystem::create_directories(output);
    run(output);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    if (!fixtures) std::filesystem::remove_all(output);
    return 1;
  }
  if (!fixtures) std::filesystem::remove_all(output);
  std::cout << checks << " native system release checks\n";
  return failures ? 1 : 0;
}
