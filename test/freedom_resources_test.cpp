#include "apsis_drift/godot/saved_flight.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "flight_endurance_meter.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
int checks{}, failures{};
auto check(bool ok, std::string_view message) -> void {
  ++checks;
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <typename T, typename E> auto need(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Resource fixture unexpectedly refused");
  return std::move(*value);
}
auto bytes(const std::filesystem::path& path) -> std::string {
  std::ifstream file{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{file}, {}};
}
auto document(std::uint64_t quantity = kFreedomFlightCapacityQuanta,
              bool assistance = false, double altitude = 200'000)
    -> FreedomResourceSaveDocument {
  auto assembly = need(make_freedom_starting_assembly_new_game_document({42}));
  auto flight = assembly.journey.voyage.flight;
  const auto system = need(generate_physical_origin_system({42}, 2));
  const auto& planet =
      system.catalog.planets[kOriginHomePlanetOrdinal].descriptor;
  flight.flight.position_metres = {planet.radius.value * 1000.0 + altitude, 0,
                                   0};
  flight.flight.linear_velocity_metres_per_second = {0, 0, -100};
  flight.flight.orientation = {};
  flight.flight.angular_velocity_radians_per_second = {};
  flight.model.assistance = assistance;
  const FreedomResources ledger{1,
                                flight.origin.state.craft,
                                flight.flight.craft,
                                flight.flight.tick,
                                quantity,
                                3};
  return {{flight, {}}, ledger};
}
auto open(const FreedomResourceSaveDocument& d) -> NativeFreedomFlightSession {
  const auto flight = surface_base_flight(d.voyage.base);
  const auto hydrated = need(hydrate_freedom_flight_document(flight));
  return need(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom,
       d,
       hydrated.system.catalog.planets[kOriginHomePlanetOrdinal].descriptor,
       {}}));
}
auto arithmetic() -> void {
  VacuumActuation p{};
  check(need(freedom_propulsion_tick_quanta(p)) == 0, "Neutral bill is zero");
  p.negative_force_newtons.z = 360'000;
  check(need(freedom_propulsion_tick_quanta(p)) == 4'680'000'000ULL,
        "Full main exact independent integer vector");
  p.positive_force_newtons.x = 56'000;
  p.negative_force_newtons.x = 56'000;
  p.positive_torque_newton_metres.y = 532'000;
  p.negative_torque_newton_metres.y = 532'000;
  check(need(freedom_propulsion_tick_quanta(p)) == 8'264'000'000ULL,
        "Gross opposing force and torque retain their bill");
  research::FlightEnduranceMeter oracle;
  oracle.sample(p);
  check(need(freedom_propulsion_tick_quanta(p)) == oracle.effort_quanta,
        "Production agrees with previously selected research meter");
  p = {};
  p.positive_torque_newton_metres.x = std::numeric_limits<double>::denorm_min();
  check(need(freedom_propulsion_tick_quanta(p)) == 2,
        "Smallest firing is not free");
  p.positive_force_newtons.x = .0015;
  check(need(freedom_propulsion_tick_quanta(p)) == 28,
        "Positive half tie rounds upward");
  for (double invalid : {-1.0, std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN(),
                         std::nextafter(1'000'000'000.0, INFINITY)}) {
    p.negative_force_newtons.z = invalid;
    check(!freedom_propulsion_tick_quanta(p),
          "Invalid channel refuses measurement");
  }
  const auto d = document();
  auto s = d.resources;
  check(validate_freedom_resources(s, surface_base_flight(d.voyage.base))
            .has_value(),
        "Actual seeded owner qualifies");
  s.flight_quanta = kFreedomFlightCapacityQuanta + 1;
  check(!validate_freedom_resources(s), "Over-capacity refuses");
  s = d.resources;
  ++s.craft.value;
  check(!validate_freedom_resources(s, surface_base_flight(d.voyage.base)),
        "Wrong craft refuses");
  s = d.resources;
  ++s.frame.version;
  check(!validate_freedom_resources(s), "Unsupported frame refuses");
  s = d.resources;
  s.jump_charges = 4;
  check(!validate_freedom_resources(s), "Over-capacity charges refuse");
  s = d.resources;
  ++s.tick;
  check(!validate_freedom_resources(s, surface_base_flight(d.voyage.base)),
        "Mismatched shared tick refuses");
  check(!advance_freedom_resource_tick(d.resources, 1, 0) &&
            !advance_freedom_resource_tick(d.resources, 0, UINT64_MAX),
        "Wrong tick and overflowing debit refuse without subtraction");
  s = d.resources;
  s.tick = UINT64_MAX - 1;
  check(!advance_freedom_resource_tick(s, s.tick, 0),
        "Unadvanceable tick refuses");
  s = d.resources;
  s.flight_quanta = kFreedomFlightReserveQuanta;
  check(need(freedom_resource_reading(s, 0, false)).reserve,
        "Reserve threshold includes equality");
  s.flight_quanta = kFreedomFlightReserveQuanta + 1;
  check(!need(freedom_resource_reading(s, 0, false)).reserve,
        "Above reserve is not a warning");
}
auto flight_and_save(const std::filesystem::path& directory) -> void {
  auto session = open(document());
  const NativeFlightControls main{{}, {0, 0, 1}, {}, {}};
  auto last = need(session.advance(main));
  check(last.fuel_debit_quanta == kFreedomFullMainTickQuanta &&
            !last.propulsion_refused &&
            session.resources()->flight_quanta ==
                kFreedomFlightCapacityQuanta - kFreedomFullMainTickQuanta &&
            session.resources()->jump_charges == 3 &&
            session.resources()->tick == session.document().flight.tick,
        "Real main tick debits one pool and commits the shared clock");
  const auto source = session.document();
  const auto ledger = *session.resources();
  auto invalid = main;
  invalid.positive_rotation.x = NAN;
  check(!session.advance(invalid) && session.document() == source &&
            *session.resources() == ledger,
        "Invalid actual input refuses physics and resource atomically");
  const auto save = directory / "resources.json";
  check(session.save_as(save).has_value(), "Resource Save As succeeds");
  const auto original = bytes(save);
  auto resumed =
      need(NativeFreedomFlightSession::open(need(native_continue(save))));
  check(resumed.document() == session.document() &&
            resumed.resources() == session.resources(),
        "Resource save resumes exact owner, flight and quantity");
  for (unsigned i = 0; i < 240; ++i) {
    const auto a = need(session.advance(main));
    const auto b = need(resumed.advance(main));
    check(a.fuel_debit_quanta == b.fuel_debit_quanta &&
              session.document() == resumed.document() &&
              session.resources() == resumed.resources(),
          "Continued next ticks reproduce both physics and bills");
  }
  check(bytes(save) == original,
        "Continue and live stepping preserve source bytes");
  auto exact = open(document(kFreedomFullMainTickQuanta));
  check(!need(exact.advance(main)).propulsion_refused &&
            exact.resources()->flight_quanta == 0,
        "Exactly affordable last tick applies thrust and empties the pool");
  auto dry_document = document(1, true);
  auto& dry_flight =
      std::get<FreedomFlightSaveDocument>(dry_document.voyage.base);
  dry_flight.flight.angular_velocity_radians_per_second = {.1, -.05, .02};
  auto dry = open(dry_document);
  check(
      dry.set_hold({1, OrbitHoldTarget{*dry.document().flight.frame.planet,
                                       dry.document().flight.position_metres.x,
                                       {0, 1, 0}}})
          .has_value(),
      "Qualified hold selected before dry test");
  auto passive = dry.document().flight;
  const auto passive_report = need(advance_atmospheric_flight(
      {dry.system()}, passive, {{}, {}, {}, {}, false}, dry.rotation(),
      dry.document().model.atmosphere, dry.document().model.central));
  const auto preferences = dry.document().model;
  const auto failed = need(dry.advance(main));
  check(failed.propulsion_refused && failed.fuel_debit_quanta == 0 &&
            dry.resources()->flight_quanta == 1 &&
            dry.document().flight == passive &&
            dry.document().model == preferences &&
            failed.actuation.central.propulsion.positive_torque_newton_metres ==
                RigidVector3{} &&
            failed.actuation.central.propulsion.negative_torque_newton_metres ==
                RigidVector3{} &&
            failed.actuation.after == passive_report.after &&
            failed.actuation.observation_after ==
                passive_report.observation_after &&
            failed.actuation.aerodynamic_linear_impulse_newton_seconds ==
                passive_report.aerodynamic_linear_impulse_newton_seconds,
        "Unfunded hold/stabilization/main retries exact passive physics and "
        "preserves residual/preferences");
  auto smaller = open(document(kFreedomFullMainTickQuanta / 2));
  check(need(smaller.advance(main)).propulsion_refused &&
            smaller.resources()->flight_quanta ==
                kFreedomFullMainTickQuanta / 2,
        "Unaffordable request retains smaller deliverable remainder");
  auto half = main;
  half.negative_translation.z = .5;
  check(!need(smaller.advance(half)).propulsion_refused &&
            smaller.resources()->flight_quanta == 0,
        "Smaller manual input can spend retained residual");
  auto neutral = open(document());
  const auto coast = need(neutral.advance({}));
  check(coast.fuel_debit_quanta == 0 &&
            neutral.resources()->flight_quanta ==
                kFreedomFlightCapacityQuanta &&
            neutral.document().flight.tick == 1,
        "Coast advances without burn");
}
auto render_batches() -> void {
  using godot_spike::SavedFlightWorld;
  SavedFlightWorld single{open(document()), {}, {}, 0};
  SavedFlightWorld batch{open(document()), {}, {}, 0};
  const std::array<double, 12> main{0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
  const auto before = *batch.session.resources();
  batch.advance(60, main, true);
  check(*batch.session.resources() == before &&
            batch.session.document().flight.tick == 0,
        "Paused wall time neither burns nor refills nor advances clock");
  for (unsigned i = 0; i < 120; ++i)
    single.advance(kSimulationStep.count(), main, false);
  for (unsigned i = 0; i < 60; ++i)
    batch.advance(2 * kSimulationStep.count(), main, false);
  check(single.session.document() == batch.session.document() &&
            single.session.resources() == batch.session.resources() &&
            batch.session.document().flight.tick == 120 &&
            batch.session.resources()->flight_quanta ==
                kFreedomFlightCapacityQuanta - 561'600'000'000ULL,
        "Changed render scheduling preserves the same actual 120 ticks and "
        "exact fuel");
}
auto schema(const std::filesystem::path& directory) -> void {
  const auto d = document();
  const auto encoded = need(encode_freedom_resource_document_json(d));
  check(need(decode_freedom_resource_document_json(encoded)) == d,
        "Closed format24 exact roundtrip");
  const auto root = Json::parse(encoded);
  for (auto field : {"version", "craft", "frame_id", "frame_version", "tick",
                     "flight_quanta", "jump_charges"}) {
    for (const Json& invalid : {Json(-1), Json(1.5), Json("wrong"),
                                Json(nullptr), Json(UINT64_MAX)}) {
      auto corrupt = root;
      corrupt["resources"][field] = invalid;
      check(!decode_freedom_resource_document_json(corrupt.dump()),
            "Invalid/wrong owner/overflow scalar refuses");
    }
    auto missing = root;
    missing["resources"].erase(field);
    check(!decode_freedom_resource_document_json(missing.dump()),
          "Missing resource field refuses");
  }
  auto extra = root;
  extra["resources"]["capacity"] = kFreedomFlightCapacityQuanta;
  check(!decode_freedom_resource_document_json(extra.dump()),
        "Capacity cannot become a mutable save knob");
  auto duplicate = encoded;
  const auto pos = duplicate.find("\"flight_quanta\":");
  duplicate.insert(pos, "\"flight_quanta\": 0, ");
  check(!decode_freedom_resource_document_json(duplicate),
        "Duplicate resource key refuses");
  duplicate = encoded;
  const auto nested = duplicate.find("\"tick\":");
  duplicate.insert(nested, "\"tick\": 0, ");
  check(!decode_freedom_resource_document_json(duplicate),
        "Duplicate nested voyage key refuses before flattening");
  check(!decode_freedom_resource_document_json(
            std::string(kMaximumSaveDocumentBytes + 1, ' ')) &&
            !decode_freedom_resource_document_json(std::string(65, '[') +
                                                   std::string(65, ']')),
        "Oversized and excessive nesting refuse");
  const auto rejected = directory / "corrupt.json";
  std::ofstream{rejected} << duplicate;
  check(!native_continue(rejected) && bytes(rejected) == duplicate,
        "Failed native load preserves corrupt source bytes");
  const auto historical = directory / "historical.json";
  const auto assembly =
      need(make_freedom_starting_assembly_new_game_document({42}));
  check(write_freedom_starting_assembly_file_atomically(historical, assembly)
            .has_value(),
        "Legacy fixture writes");
  const auto old_bytes = bytes(historical);
  auto legacy =
      need(NativeFreedomFlightSession::open(need(native_continue(historical))));
  check(!legacy.resources() && !legacy.replenish_resources() &&
            legacy.save_as(directory / "legacy-copy.json").has_value() &&
            bytes(historical) == old_bytes &&
            bytes(directory / "legacy-copy.json") == old_bytes,
        "Legacy21 remains resource-unselected and byte-preserving");
}
auto service() -> void {
  auto session =
      need(NativeFreedomFlightSession::open(need(native_new_game({42}))));
  check(session.walker().has_value() && session.resources() &&
            session.resources()->flight_quanta ==
                kFreedomFlightCapacityQuanta &&
            session.resources()->jump_charges == 3,
        "New Game starts on station with full explicit resources");
  const auto ledger = *session.resources();
  for (unsigned i = 0; i < 10; ++i)
    (void)need(session.advance_walk({}));
  check(session.resources()->flight_quanta == ledger.flight_quanta &&
            session.resources()->tick == 10,
        "Walking advances shared clock without fuel or passive refill");
  auto depleted = *session.resources();
  depleted.flight_quanta = 0;
  depleted.jump_charges = 0;
  const FreedomDockingSaveDocument attached{session.document(),
                                            *session.docking()};
  const auto full = need(replenish_freedom_resources(depleted, attached));
  check(full.flight_quanta == kFreedomFlightCapacityQuanta &&
            full.jump_charges == 3 && full.tick == depleted.tick &&
            need(replenish_freedom_resources(full, attached)) == full,
        "Attached free service refills both pools idempotently without "
        "advancing time");
  auto wrong = attached;
  wrong.docking.attached = false;
  check(!replenish_freedom_resources(depleted, wrong),
        "Nearby free flight is not service proof");
  ++depleted.craft.value;
  check(!replenish_freedom_resources(depleted, attached),
        "Service rejects different craft ledger");
}
auto jumps() -> void {
  auto c = initial_intersystem_contract_state({42});
  check(advance_intersystem_contract(c, c.universe_tick,
                                     IntersystemContractCommand::accept_mission)
                .has_value() &&
            advance_intersystem_contract(c, c.universe_tick,
                                         IntersystemContractCommand::launch)
                .has_value(),
        "Retained jump-owner fixture launches");
  FreedomResources s{1,
                     make_freedom_new_game_document({42}).state.craft,
                     {kWayfarerFrameId, 1},
                     c.universe_tick};
  const auto target = generate_local_system(c.identities.target_system_seed);
  check(begin_freedom_resource_jump(c, s).has_value(),
        "Charge checked before actual spool");
  for (unsigned i = 0; i < 7; ++i)
    (void)need(advance_freedom_resource_jump_tick(c, s, target));
  check(cancel_intersystem_jump(c).has_value() && s.jump_charges == 3,
        "Canceled actual spool consumes no charge");
  check(begin_freedom_resource_jump(c, s).has_value(),
        "Canceled spool can restart without prepayment");
  const auto before_c = c;
  const auto before_s = s;
  const auto wrong = generate_local_system({123});
  check(!advance_freedom_resource_jump_tick(c, s, wrong) && c == before_c &&
            s == before_s,
        "Invalid destination leaves both owners unchanged");
  for (SimulationTick i = 0; i < kJumpSpoolTicks; ++i)
    (void)need(advance_freedom_resource_jump_tick(c, s, target));
  check(s.jump_charges == 2, "Actual outbound commit debits exactly once");
  auto committed = c;
  auto committed_s = s;
  for (SimulationTick i = 0; i < kJumpTransitTicks; ++i) {
    (void)need(advance_freedom_resource_jump_tick(c, s, target));
    (void)need(
        advance_freedom_resource_jump_tick(committed, committed_s, target));
  }
  check(c == committed && s == committed_s && s.jump_charges == 2,
        "Committed checkpoint continuation does not bill arrival again");
  // This adapter qualifies the retained historical jump owner. Its legacy
  // mission transitions are not introduced into native Freedom movement.
  for (auto command : {IntersystemContractCommand::enter_target_planet,
                       IntersystemContractCommand::complete_objective,
                       IntersystemContractCommand::leave_target_planet})
    check(advance_intersystem_contract(c, c.universe_tick, command).has_value(),
          "Retained historical owner qualifies its return phase");
  check(begin_freedom_resource_jump(c, s).has_value(),
        "Valid return spool starts");
  const auto origin = generate_origin_system({42});
  for (SimulationTick i = 0; i < kJumpSpoolTicks + kJumpTransitTicks; ++i)
    (void)need(advance_freedom_resource_jump_tick(c, s, origin));
  check(s.jump_charges == 1 &&
            s.flight_quanta == kFreedomFlightCapacityQuanta &&
            c.travel_phase == IntersystemTravelPhase::origin_system_return,
        "Actual outbound/return leaves one charge and unchanged flight fuel");
  auto last_c = before_c;
  auto last_s = before_s;
  last_s.jump_charges = 1;
  for (SimulationTick i = 0; i < kJumpSpoolTicks + kJumpTransitTicks; ++i)
    (void)need(advance_freedom_resource_jump_tick(last_c, last_s, target));
  check(last_s.jump_charges == 0 &&
            !begin_freedom_resource_jump(last_c, last_s),
        "Last charge permits arrival then refuses another spool");
  check(!advance_freedom_resource_jump_tick(last_c, last_s, target),
        "Repeated completion refuses without debit");
}
} // namespace
auto main() -> int {
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("apsis-resources-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(directory);
  try {
    arithmetic();
    flight_and_save(directory);
    schema(directory);
    render_batches();
    service();
    jumps();
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << e.what() << '\n';
  }
  std::filesystem::remove_all(directory);
  std::cout << checks << " resource checks; " << failures << " failures\n";
  return failures ? 1 : 0;
}
