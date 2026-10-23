#include "apsis_drift/freedom_derelict.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "apsis_drift/save_file.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
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
auto crew_log_topic(const NativeFreedomFlightSession& live, bool inspected)
    -> void {
  const auto original =
      need(encode_native_save_document_json(need(live.save_document())));
  check(live.walker().has_value() && live.knowledge()->derelicts &&
            live.knowledge()->derelicts->contact &&
            live.knowledge()->derelicts->inspection.has_value() == inspected,
        "Contact conversation fixture retains actual earned log at station");
  const auto baseline = need(origin_station_crew(
      generate_origin_station(live.document().origin.recipe.universe_seed),
      *live.walker(), live.document().flight.tick));
  for (unsigned repeat = 0; repeat < 3; ++repeat) {
    const auto crew = need(live.station_crew());
    check(crew.topic_count == 4 && crew.topic_count < crew.topics.size() &&
              crew.available == baseline.available &&
              crew.reason == baseline.reason &&
              crew.station == baseline.station &&
              crew.foot_position_metres == baseline.foot_position_metres &&
              crew.heading_radians == baseline.heading_radians &&
              crew.height_metres == baseline.height_metres,
          "Earned contact adds one topic without changing actual crew "
          "eligibility or placement");
    for (std::size_t i = 0; i < baseline.topic_count; ++i)
      check(crew.topics[i].id == baseline.topics[i].id &&
                crew.topics[i].title == baseline.topics[i].title &&
                crew.topics[i].text == baseline.topics[i].text,
            "Contact discussion retains all three original topic IDs and text");
    const auto& lead = crew.topics[3];
    check(lead.id == 4 && lead.title == "Can you look at my contact log?" &&
              lead.text.find_first_of("0123456789") == std::string_view::npos,
          "Fourth topic explicitly shares the player's log without coordinates "
          "or tuning numbers");
    if (inspected) {
      check(lead.text.find("no active power or thrust") !=
                    std::string_view::npos &&
                lead.text.find("unreadable registry plate") !=
                    std::string_view::npos &&
                lead.text.find(
                    "Ownership and whether anyone is aboard remain unknown") !=
                    std::string_view::npos,
            "Inspected discussion acknowledges only recorded findings and "
            "leaves ownership and occupancy unknown");
    } else {
      check(lead.text.find("Your log records a contact") !=
                    std::string_view::npos &&
                lead.text.find("Inspect derelict exterior") !=
                    std::string_view::npos &&
                lead.text.find("unreadable") == std::string_view::npos &&
                lead.text.find("no active power") == std::string_view::npos,
            "Contact-only advice offers the existing inspection without unseen "
            "exterior findings");
    }
    check(need(encode_native_save_document_json(need(live.save_document()))) ==
              original,
          "Repeated contact conversation reads preserve complete Save, "
          "knowledge, clock and resources");
  }
}
auto contact_recovery(const NativeFreedomFlightSession& source,
                      const std::filesystem::path& output) -> void {
  const auto original =
      need(encode_native_save_document_json(need(source.save_document())));
  // An independent classified-loss clone, not exhaustion, rescue or a flown
  // return. The main actual contact/approach/inspection journey stays intact.
  auto recovered = std::make_unique<NativeFreedomFlightSession>(source);
  const auto records = recovered->knowledge()->derelicts;
  check(records && records->contact && !records->inspection,
        "Recovery clone starts from the actual contact before exterior "
        "inspection");
  need(recovered->record_loss(
      FreedomLossCause::recoverable_destruction,
      recovered->document().origin.state.craft,
      recovered->document().flight.tick,
      need(rigid_body_state_checksum(RigidBodyWorldContext{recovered->system()},
                                     recovered->document().flight))));
  need(recovered->complete_recovery());
  check(recovered->walker().has_value() &&
            recovered->knowledge()->derelicts == records &&
            !recovered->derelict_view().local,
        "Real standard recovery retains the contact-only log at station "
        "without a remote pose");
  crew_log_topic(*recovered, false);
  auto restored = std::make_unique<NativeFreedomFlightSession>(
      continued(*recovered, output / "contact-recovered.json"));
  crew_log_topic(*restored, false);
  check(need(encode_native_save_document_json(need(source.save_document()))) ==
            original,
        "Independent contact recovery and conversation preserve the main "
        "actual journey");
}
auto exercise(const std::filesystem::path& output) -> void {
  std::filesystem::create_directories(output);
  auto station = need(NativeFreedomFlightSession::open(
      need(native_uncharted_network_new_game({42}))));
  check(station.derelict_view().selected && !station.derelict_view().known &&
            !station.scan_derelict() && !station.inspect_derelict(),
        "New selected station journey hides hull and refuses space actions");
  const auto unknown =
      need(encode_native_save_document_json(need(station.save_document())));
  check(need(station.station_crew()).topic_count == 3 &&
            need(station.station_crew()).topics[3].id == 0 &&
            need(encode_native_save_document_json(
                need(station.save_document()))) == unknown,
        "Unknown selected contact exposes only the original three crew topics "
        "without changing Save");
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
  contact_recovery(live, output);
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
  // A classified-loss fixture, not a claim that the exterior inspection
  // causes destruction. Use genuine recovery to retain the note at station.
  need(resumed.record_loss(
      FreedomLossCause::recoverable_destruction,
      resumed.document().origin.state.craft, resumed.document().flight.tick,
      need(rigid_body_state_checksum(RigidBodyWorldContext{resumed.system()},
                                     resumed.document().flight))));
  need(resumed.complete_recovery());
  check(resumed.walker().has_value() &&
            resumed.knowledge()->derelicts == records &&
            resumed.derelict_view().inspected && !resumed.derelict_view().local,
        "Actual station replacement retains the historical exterior note "
        "without a moving remote pose");
  resumed = continued(resumed, output / "recovered.json");
  crew_log_topic(resumed, true);
}
auto stellar_lead_exercise(const std::filesystem::path& output) -> void {
  // Independent journey: none of the seven original derelict phases receives
  // a report. Keep a large native session off this additional fixture's stack.
  auto live = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_uncharted_network_new_game({42})))));
  const auto initial =
      need(encode_native_save_document_json(need(live->save_document())));
  check(!live->receive_station_stellar_lead(live->document().flight.tick) &&
            need(encode_native_save_document_json(
                need(live->save_document()))) == initial,
        "Report receipt requires the actual eligible attendant context");
  need(live->advance_walk({0, 0, -std::numbers::pi / 2}));
  const auto offer = need(live->station_stellar_lead_view());
  const auto crew = need(live->station_crew());
  check(offer.selected && !offer.kept && offer.can_keep &&
            offer.offer_tick == live->document().flight.tick &&
            !offer.received_tick && offer.status == "unrecorded" &&
            crew.available && crew.topic_count == 4 && crew.topics[3].id == 5 &&
            !live->knowledge()->derelicts->contact,
        "Actual nearby attendant offers stable topic5 without inventing "
        "earned contact topic4 or a navigation fix");
  *live = continued(*live, output / "lead-station.json");
  const auto before =
      need(encode_native_save_document_json(need(live->save_document())));
  const auto document = live->document();
  const auto resources = live->resources();
  auto expected_knowledge = *live->knowledge();
  for (unsigned read = 0; read < 3; ++read) {
    check(need(live->station_stellar_lead_view()).offer_tick ==
                  offer.offer_tick &&
              need(live->station_crew()).topics[3].id == 5 &&
              !live->receive_station_stellar_lead(live->document().flight.tick +
                                                  1) &&
              need(encode_native_save_document_json(
                  need(live->save_document()))) == before,
          "Report reads and stale receipt preserve complete current Save");
  }
  need(live->receive_station_stellar_lead(*offer.offer_tick));
  expected_knowledge.station_stellar_lead =
      live->knowledge()->station_stellar_lead;
  const auto kept = need(live->station_stellar_lead_view());
  check(kept.kept && !kept.can_keep && !kept.offer_tick &&
            kept.received_tick == offer.offer_tick &&
            kept.status == "unverified" &&
            *live->knowledge() == expected_knowledge &&
            live->document() == document && live->resources() == resources,
        "Explicit receipt adds only the immutable personal note, without "
        "time, fuel, facts or a chart destination");
  const auto note = *live->knowledge()->station_stellar_lead;
  const auto kept_bytes =
      need(encode_native_save_document_json(need(live->save_document())));
  check(!live->receive_station_stellar_lead(*offer.offer_tick) &&
            need(encode_native_save_document_json(
                need(live->save_document()))) == kept_bytes,
        "Duplicate Keep refuses without rerolling or changing the note");
  *live = continued(*live, output / "lead-kept.json");
  for (unsigned tick = 0; tick < 1194; ++tick)
    need(live->advance_walk({0, -1, 0}));
  need(live->begin_boarding());
  for (SimulationTick tick = 0; tick < kGameplayBoardingTicks; ++tick)
    need(live->advance_walk({}));
  need(live->release_port());
  need(live->set_assistance(false));
  auto source =
      std::get<FreedomKnowledgeSaveDocument>(need(live->save_document()));
  auto& flight =
      std::get<FreedomBoardingSaveDocument>(source.voyage.voyage.base)
          .voyage.flight;
  const auto home = need(find_local_system_planet(
      live->system(), flight.origin.recipe.home_planet));
  // The same disclosed saved-space placement as the original fixture. The
  // following search ticks and charged visit are real, not a flown ascent.
  flight.flight.position_metres = {
      0, 0, static_cast<double>(home->descriptor.radius.value) * 10000};
  flight.flight.linear_velocity_metres_per_second = {};
  flight.flight.angular_velocity_radians_per_second = {};
  flight.flight = need(canonicalize_rigid_body_state(
      RigidBodyWorldContext{live->system()}, flight.flight));
  *live = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(std::move(source)))));
  need(live->begin_star_search());
  for (SimulationTick tick = 0; tick < kFreedomStarSearchTicks; ++tick)
    need(live->advance({}));
  check(live->knowledge()->star_search->completed.size() == 1 &&
            live->knowledge()->star_search->completed[0].target ==
                note.target &&
            live->knowledge()->station_stellar_lead == note &&
            need(live->station_stellar_lead_view()).status ==
                "sensor_fix_acquired",
        "Only the unchanged real 1200-tick ship search confirms the report");
  *live = continued(*live, output / "lead-confirmed.json");
  need(live->select_jump(note.target));
  need(live->begin_jump());
  for (SimulationTick tick = 0; tick < kJumpSpoolTicks + kJumpTransitTicks;
       ++tick)
    need(live->advance({}));
  check(
      need(live->station_stellar_lead_view()).status == "visited" &&
          live->resources()->jump_charges == 2 &&
          live->knowledge()->station_stellar_lead == note,
      "Actual charged target-system arrival supplies distinct visit evidence");
  *live = continued(*live, output / "lead-visited.json");
  need(live->record_loss(
      FreedomLossCause::recoverable_destruction,
      live->document().origin.state.craft, live->document().flight.tick,
      need(rigid_body_state_checksum(RigidBodyWorldContext{live->system()},
                                     live->document().flight))));
  need(live->complete_recovery());
  check(live->walker().has_value() &&
            live->document().origin.state.craft != note.craft &&
            live->knowledge()->station_stellar_lead == note &&
            need(live->station_stellar_lead_view()).status == "visited",
        "Explicit classified recovery preserves the personal report and "
        "genuine visit, including its historical craft provenance");
  *live = continued(*live, output / "lead-recovered.json");
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
    stellar_lead_exercise(output);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    ++failures;
  }
  if (argc == 1) std::filesystem::remove_all(output);
  std::cout << "Native derelict failures: " << failures << '\n';
  return failures ? 1 : 0;
}
