#include "apsis_drift/freedom_derelict.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "apsis_drift/save_file.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace apsis_drift;
int failures{};
auto check(bool value, std::string_view message) -> void {
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> result) -> T {
  if (!result) {
    if constexpr (std::is_same_v<E, std::string>)
      std::cerr << result.error() << '\n';
    throw std::runtime_error("Derelict fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*result);
}
auto fixture() -> NativeFreedomFlightSession {
  const Seed seed{42};
  auto live = need(NativeFreedomFlightSession::open(
      need(native_uncharted_network_new_game(seed))));
  for (unsigned tick = 0; tick < 1194; ++tick)
    need(live.advance_walk({0, -1, 0}));
  need(live.begin_boarding());
  for (SimulationTick tick = 0; tick < kGameplayBoardingTicks; ++tick)
    need(live.advance_walk({}));
  need(live.release_port());
  need(live.set_assistance(false));
  auto source =
      std::get<FreedomKnowledgeSaveDocument>(need(live.save_document()));
  auto& home_flight =
      std::get<FreedomBoardingSaveDocument>(source.voyage.voyage.base)
          .voyage.flight;
  const auto home = need(find_local_system_planet(
      live.system(), home_flight.origin.recipe.home_planet));
  // Explicit saved-space Home placement, not a flown station ascent. Retain
  // the actual walked/boarded pilot, owner, clock, resources and knowledge.
  home_flight.flight.position_metres = {
      0, 0, static_cast<double>(home->descriptor.radius.value) * 10000};
  home_flight.flight.linear_velocity_metres_per_second = {};
  home_flight.flight.angular_velocity_radians_per_second = {};
  home_flight.flight = need(canonicalize_rigid_body_state(
      RigidBodyWorldContext{live.system()}, home_flight.flight));
  live = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(std::move(source)))));
  for (unsigned discovery = 0; discovery < 2; ++discovery) {
    need(live.begin_star_search());
    for (SimulationTick tick = 0; tick < kFreedomStarSearchTicks; ++tick)
      need(live.advance({}));
  }
  const auto descriptor = need(generate_freedom_derelict(seed));
  need(live.select_jump(descriptor.system));
  need(live.begin_jump());
  for (SimulationTick tick = 0; tick < kJumpSpoolTicks + kJumpTransitTicks;
       ++tick)
    need(live.advance({}));
  // Leave the frozen arrival's exact-tick boundary through ordinary physics
  // before constructing the separately labeled close-approach placement.
  need(live.advance({}));
  const auto relative = need(live.relative_state());
  check(relative.frame.system == descriptor.system &&
            relative.frame.planet == descriptor.planet &&
            live.resources()->jump_charges == 2 &&
            live.knowledge()->star_search->completed.size() == 2,
        "Approach retains genuine stellar fixes and one charged target-system "
        "arrival");
  auto voyage = need(live.travel_document());
  auto& flight =
      std::get<FreedomFlightSaveDocument>(voyage.voyage.voyage.voyage.base);
  auto approach =
      need(resolve_freedom_derelict(descriptor, flight.flight.tick));
  // Explicit close-approach placement only: discovery, charged travel and its
  // receipts above are real. This is not a flown approach from system arrival.
  approach.position_metres.x += 550;
  approach.linear_velocity_metres_per_second.x -= 10;
  flight.flight =
      need(reframe_rigid_body(RigidBodyWorldContext{descriptor.owner}, approach,
                              {flight.flight.frame, approach.tick}));
  return need(NativeFreedomFlightSession::open(
      need(native_select_save_document(std::move(voyage)))));
}

auto continued(const NativeFreedomFlightSession& live,
               const std::filesystem::path& path)
    -> NativeFreedomFlightSession {
  need(live.save_as(path));
  auto resumed =
      need(NativeFreedomFlightSession::open(need(native_continue(path))));
  check(
      need(encode_native_save_document_json(need(live.save_document()))) ==
          need(encode_native_save_document_json(need(resumed.save_document()))),
      "Continue preserves the exact current voyage and evidence");
  return resumed;
}
auto exercise(const std::filesystem::path& output) -> void {
  std::filesystem::create_directories(output);
  auto station = need(NativeFreedomFlightSession::open(
      need(native_uncharted_network_new_game({42}))));
  check(station.derelict_view().selected && !station.derelict_view().known &&
            !station.scan_derelict() && !station.inspect_derelict(),
        "New selected station journey hides hull and refuses space actions");
  station = continued(station, output / "station.json");
  auto old =
      need(NativeFreedomFlightSession::open(need(native_new_game({42}))));
  check(!old.derelict_view().selected && !old.scan_derelict(),
        "Default historical recipe remains unselected");
  auto live = fixture();
  need(live.save_as(output / "approach.json"));
  const auto before = live.document();
  const auto resources = live.resources();
  const auto facts = live.knowledge()->entries;
  const auto hidden = live.derelict_view();
  check(hidden.can_scan && !hidden.known && !hidden.local && !hidden.hull &&
            hidden.id == 0 && hidden.distance_metres == 0 &&
            hidden.history.empty() && !live.inspect_derelict(),
        "Actual local unknown hull supplies no pose, identity or history");
  need(live.scan_derelict());
  const auto contact = live.derelict_view();
  check(contact.known && contact.local && contact.hull &&
            contact.distance_metres > 500 && !contact.can_inspect &&
            contact.history.empty() && !live.inspect_derelict() &&
            live.document() == before && live.resources() == resources &&
            live.knowledge()->entries == facts,
        "Earned contact and out-of-range refusal spend no time/fuel or facts");
  auto resumed = continued(live, output / "contact.json");
  // Both independent peers coast through real committed physics ticks from
  // the labeled approach fixture. No position assignment closes the distance.
  for (unsigned tick = 0; tick < 1200; ++tick) {
    need(live.advance({}));
    need(resumed.advance({}));
  }
  check(live.document() == resumed.document() &&
            live.knowledge() == resumed.knowledge() &&
            live.derelict_view().can_inspect,
        "Continued real flight reaches the physical inspection stand-off");
  need(live.save_as(output / "near.json"));
  const auto near = live.document();
  const auto near_resources = live.resources();
  need(live.inspect_derelict());
  check(live.derelict_view().inspected &&
            !live.derelict_view().history.empty() && live.document() == near &&
            live.resources() == near_resources,
        "Exterior note is earned without moving craft or spending resources");
  resumed = continued(live, output / "inspection.json");
  const auto records = resumed.knowledge()->derelicts;
  need(resumed.inspect_derelict());
  check(resumed.knowledge()->derelicts == records,
        "Repeated inspection is idempotent after Continue");
}
} // namespace

auto main(int argc, char** argv) -> int {
  const auto output = argc == 3 && std::string_view{argv[1]} == "--fixtures"
                          ? std::filesystem::path{argv[2]}
                          : std::filesystem::temp_directory_path() /
                                ("apsis-drift-derelict-" +
                                 std::to_string(std::chrono::steady_clock::now()
                                                    .time_since_epoch()
                                                    .count()));
  try {
    exercise(output);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    ++failures;
  }
  if (argc == 1) std::filesystem::remove_all(output);
  std::cout << "Native derelict failures: " << failures << '\n';
  return failures ? 1 : 0;
}
