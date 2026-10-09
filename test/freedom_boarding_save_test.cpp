#include "apsis_drift/native_flight_session.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
using namespace apsis_drift;
int failures{};
auto check(bool value, std::string_view why) -> void {
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << why << '\n';
  }
}
template <class T, class E> auto required(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Boarding session fixture refused");
  return std::move(*value);
}
auto require_ok(std::expected<void, std::string> value) -> void {
  if (!value) throw std::runtime_error(value.error());
}
struct Files {
  std::filesystem::path dir{
      std::filesystem::temp_directory_path() /
      ("apsis-boarding-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()))};
  Files() { std::filesystem::create_directory(dir); }
  ~Files() {
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
  }
};
auto text(const std::filesystem::path& path) -> std::string {
  std::ifstream in{path};
  return {std::istreambuf_iterator<char>{in}, {}};
}
auto resume(const std::filesystem::path& path) -> NativeFreedomFlightSession {
  return required(
      NativeFreedomFlightSession::open(required(native_continue(path))));
}
auto run() -> void {
  // Buffer/shape rejection precedes source construction.
  check(!decode_freedom_boarding_document_json("{"), "truncated JSON refuses");
  check(!decode_freedom_boarding_document_json(
            std::string(kMaximumSaveDocumentBytes + 1, ' ')),
        "oversized buffer refuses");
  check(!decode_freedom_boarding_document_json(std::string(65, '[') +
                                               std::string(65, ']')),
        "excess nesting refuses");
  check(!decode_freedom_boarding_document_json(
            "{\"format_version\":22,\"format_version\":22}"),
        "duplicate version refuses");
  Files files;
  auto session = required(
      NativeFreedomFlightSession::open(required(native_new_game(Seed{42}))));
  check(session.walker().has_value() && !session.boarding(),
        "new21 remains a station actor without invented seated state");
  const auto initial = session.document();
  check(!session.begin_boarding() && session.document() == initial &&
            !session.boarding(),
        "distant boarding transaction refuses");
  for (int i = 0; i < 2960; ++i)
    (void)required(session.advance_walk({0, -1, 0}));
  const auto entry = *session.walker();
  check(entry.foot_position_metres.x < -20 &&
            entry.foot_position_metres.x > -23.12,
        "real walker reaches D1 approach");
  const auto initial_tick = session.document().flight.tick;
  require_ok(session.begin_boarding());
  check(session.boarding()->phase == FreedomBoardingPhase::boarding &&
            session.walker().has_value(),
        "boarding keeps walk presentation");
  check(session.document().flight.tick == initial_tick,
        "begin action does not invent a tick");
  const auto before = session.document();
  const auto route_before = *session.boarding();
  check(!session.advance_walk({std::numeric_limits<double>::quiet_NaN(), 0, 0}),
        "nonfinite transition input refuses");
  check(!session.advance_walk({}, SimulationSeconds{1.0 / 60}),
        "nonfixed transition step refuses");
  check(session.document() == before && *session.boarding() == route_before,
        "invalid transition input is transactional");
  check(!session.advance({}) && !session.release_port() &&
            !session.set_assistance(false),
        "flight controls stay gated while boarding");
  for (int i = 0; i < 600; ++i)
    (void)required(session.advance_walk({}));
  const auto mid = files.dir / "mid.json";
  require_ok(session.save_as(mid));
  const auto original_mid = text(mid);
  const auto saved = required(load_native_save_file(mid));
  check(std::holds_alternative<FreedomResourceSaveDocument>(saved) &&
            std::holds_alternative<FreedomBoardingSaveDocument>(
                std::get<FreedomResourceSaveDocument>(saved).voyage.base),
        "genuine board action retains24 with nested22");
  auto document = std::get<FreedomBoardingSaveDocument>(
      std::get<FreedomResourceSaveDocument>(saved).voyage.base);
  const auto encoded =
      required(encode_freedom_boarding_document_json(document));
  check(required(decode_freedom_boarding_document_json(encoded)) == document,
        "22 exact roundtrip");
  auto bad = document;
  bad.boarding.route.station_entry_eye.x =
      std::numeric_limits<double>::infinity();
  check(!validate_freedom_boarding_document(bad),
        "nonfinite saved route refuses");
  bad = document;
  ++bad.boarding.started_tick;
  check(!validate_freedom_boarding_document(bad),
        "inconsistent shared progress clock refuses");
  bad = document;
  bad.boarding.route.craft_station_position.x += .001;
  check(!validate_freedom_boarding_document(bad),
        "changed captured transform refuses");
  bad = document;
  bad.starting_assembly.frame_sha256 = "altered";
  check(!validate_freedom_boarding_document(bad),
        "changed assembly pins refuse");
  bad = document;
  bad.voyage.docking.attached = false;
  check(!validate_freedom_boarding_document(bad),
        "detached transition refuses");
  bad = document;
  bad.boarding.phase = FreedomBoardingPhase::seated;
  check(!validate_freedom_boarding_document(bad),
        "premature seated flag refuses");
  auto json = nlohmann::ordered_json::parse(encoded);
  json["boarding"]["route"]["elapsed_ticks"] = kGameplayBoardingTicks + 1;
  check(!decode_freedom_boarding_document_json(json.dump()),
        "one-past progress buffer refuses");
  json = nlohmann::ordered_json::parse(encoded);
  json["boarding"]["extra"] = 0;
  check(!decode_freedom_boarding_document_json(json.dump()),
        "unknown closed-schema field refuses");
  auto continued = resume(mid);
  check(continued.document() == session.document() &&
            continued.boarding() == session.boarding() &&
            continued.walker() == session.walker() &&
            continued.resources() == session.resources(),
        "midroute resume retains craft, clock, progress and projected actor");
  for (int i = 600; i < static_cast<int>(kGameplayBoardingTicks); ++i) {
    (void)required(session.advance_walk({}));
    (void)required(continued.advance_walk({}));
    check(session.document() == continued.document() &&
              session.boarding() == continued.boarding() &&
              session.resources() == continued.resources(),
          "resumed board continuation deterministic");
  }
  check(!session.walker() &&
            session.boarding()->phase == FreedomBoardingPhase::seated,
        "seat completion unlocks flight presentation");
  check(session.document().flight.tick == initial_tick + kGameplayBoardingTicks,
        "route and craft share exactly1440ticks");
  check(required(session.boarding_view()).hardware == OperatingProgress{},
        "hardware closes before flight");
  (void)required(session.advance_walk({}));
  check(session.document().flight.tick ==
            initial_tick + kGameplayBoardingTicks + 1,
        "neutral batch consumes post-seat remainder");
  check(!session.begin_port_approach(), "attached approach aid refuses");
  check(session.document().model.physical_catalog == 2 &&
            session.document().model.physical_ephemeris == 2,
        "New Game explicitly selects continuous motion");
  auto legacy =
      FreedomBoardingSaveDocument{{session.document(), *session.docking()},
                                  *session.starting_assembly(),
                                  *session.boarding(),
                                  {}};
  const auto legacy_system =
      required(generate_physical_origin_system(Seed{42}));
  const auto station_descriptor = generate_origin_station(Seed{42});
  const auto station_geometry =
      required(origin_station_geometry(station_descriptor));
  legacy.voyage.flight.model.physical_catalog = 1;
  legacy.voyage.flight.model.physical_ephemeris = 1;
  legacy.voyage.flight.flight = required(release_origin_port(
      legacy_system, station_descriptor, station_geometry,
      {1, legacy.voyage.docking.target, wayfarer_frame().recipe,
       session.document().flight.tick}));
  const auto legacy_path = files.dir / "legacy-seated.json";
  std::ofstream{legacy_path}
      << required(encode_freedom_boarding_document_json(legacy));
  auto historical = resume(legacy_path);
  check(historical.document() == legacy.voyage.flight &&
            historical.system().ephemeris_version == 1 &&
            !historical.resources(),
        "historical seated22 retains original rounded motion exactly");
  require_ok(historical.release_port());
  check(!historical.begin_port_approach(),
        "historical aid refuses without migrating old body");
  require_ok(session.set_assistance(false));
  require_ok(session.release_port());
  check(!session.begin_disembarking(), "detached pilot cannot disembark");
  NativeFlightControls thrust{};
  thrust.negative_translation = {0, .25, 0};
  const auto first_departure = required(session.advance(thrust));
  check(first_departure.actuation.central.propulsion.negative_force_newtons.y >
            0,
        "first departure applies the actual withdrawal thrusters");
  for (int i = 1; i < 720; ++i)
    (void)required(session.advance(thrust));
  check(required(session.assess_port()).outward_separation_metres > 80,
        "same New Game craft physically leaves the port column");
  const auto flight = files.dir / "flight.json";
  require_ok(session.save_as(flight));
  auto flight_resume = resume(flight);
  check(!flight_resume.walker() &&
            flight_resume.boarding()->phase == FreedomBoardingPhase::seated &&
            flight_resume.document() == session.document(),
        "seated freeflight22 resumes without floor actor");
  check(text(mid) == original_mid,
        "SaveAs and Continue never mutate original save");
  const auto departure = session.document();
  require_ok(session.begin_port_approach());
  check(session.document() == departure && session.port_approach().active,
        "approach command never relocates or advances craft");
  const auto active = session.port_approach();
  NativeFlightControls invalid;
  invalid.positive_translation.x = std::numeric_limits<double>::quiet_NaN();
  check(!session.advance(invalid) &&
            !session.advance({}, SimulationSeconds{1.0 / 60}) &&
            session.document() == departure &&
            session.port_approach() == active,
        "invalid input/step cannot cancel or advance approach");
  auto manual = session;
  (void)required(manual.advance(thrust));
  check(!manual.port_approach().active &&
            manual.document().flight.tick == departure.flight.tick + 1,
        "manual input cancels aid and advances ordinary actuators");
  const auto aid_save = files.dir / "aid.json";
  require_ok(session.save_as(aid_save));
  check(resume(aid_save).document() == session.document() &&
            !resume(aid_save).port_approach().active,
        "Continue retains actual body but never arms transient approach");
  std::uint32_t return_ticks{};
  while (session.port_approach().active &&
         return_ticks < kNativePortApproachTicks &&
         required(session.assess_port()).decision !=
             OriginDockDecision::capture_ready) {
    const auto applied =
        required(session.advance({}))
            .actuation.central.propulsion.applied_force_newtons;
    ++return_ticks;
    if (return_ticks % 240 == 0) {
      const auto a = required(session.assess_port());
      std::cout << "approach " << return_ticks << " out "
                << a.outward_separation_metres << " lateral "
                << a.lateral_separation_metres << " inward "
                << a.inward_speed_metres_per_second << " force " << applied.x
                << ',' << applied.y << ',' << applied.z << '\n';
    }
  }
  std::cout << "New Game return: " << return_ticks << " ticks, "
            << required(session.assess_port()).separation_metres << " m, "
            << session.port_approach().note << '\n';
  check(required(session.assess_port()).decision ==
                OriginDockDecision::capture_ready &&
            !session.docking()->attached && return_ticks > 120,
        "ordinary thrusters return within unchanged gate without auto-capture");
  for (int i = 0; i < 600; ++i) {
    (void)required(session.advance({}));
    ++return_ticks;
  }
  check(required(session.assess_port()).decision ==
                OriginDockDecision::capture_ready &&
            session.port_approach().active,
        "aid retains real readiness while the pilot chooses Capture");
  require_ok(session.capture_port());
  check(session.document().flight.tick == departure.flight.tick + return_ticks,
        "capture preserves actual journey clock");
  require_ok(session.begin_disembarking());
  check(session.walker().has_value(),
        "unboard immediately selects walk presentation");
  const auto reverse_mid = files.dir / "reverse.json";
  for (int i = 0; i < 400; ++i)
    (void)required(session.advance_walk({}));
  require_ok(session.save_as(reverse_mid));
  auto reverse_resume = resume(reverse_mid);
  for (int i = 400; i < static_cast<int>(kGameplayBoardingTicks); ++i) {
    (void)required(session.advance_walk({}));
    (void)required(reverse_resume.advance_walk({}));
  }
  check(session.walker() == entry && reverse_resume.walker() == entry,
        "disembark restores exact real entry actor");
  check(session.boarding()->phase == FreedomBoardingPhase::station,
        "unboard returns station mode");
  (void)required(session.advance_walk({0, 1, 0}));
  const auto station = files.dir / "station.json";
  require_ok(session.save_as(station));
  check(resume(station).walker() == session.walker() &&
            session.walker() != entry,
        "station save retains updated walking pose");
  require_ok(session.begin_boarding());
  check(session.boarding()->station_entry == *resume(station).walker(),
        "reboarding records current real entry");
}
} // namespace
auto main() -> int {
  try {
    run();
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
  return failures == 0 ? 0 : 1;
}
