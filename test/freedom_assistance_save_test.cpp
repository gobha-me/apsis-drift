#include "apsis_drift/freedom_assistance_save.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/native_startup.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <source_location>
#include <stdexcept>
#include <type_traits>
using namespace apsis_drift;
namespace {
using Json = nlohmann::ordered_json;
unsigned checks{};
auto check(bool value, const char* label) -> void {
  ++checks;
  if (!value) throw std::runtime_error(label);
}
template <class T, class E>
auto need(std::expected<T, E> value,
          std::source_location at = std::source_location::current()) -> T {
  if (!value) {
    std::cerr << at.line() << ": ";
    if constexpr (requires { value.error().detail; })
      std::cerr << value.error().detail;
    else if constexpr (std::is_same_v<E, std::string>)
      std::cerr << value.error();
    else if constexpr (std::is_enum_v<E>)
      std::cerr << static_cast<unsigned>(value.error());
    std::cerr << '\n';
    throw std::runtime_error("Actual fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto cycle(const FreedomAssistanceSaveDocument& document) -> std::string {
  const auto encoded = need(encode_freedom_assistance_document_json(document));
  const auto decoded = need(decode_freedom_assistance_document_json(encoded));
  check(decoded == document, "Complete outer owner/state equality");
  check(need(encode_freedom_assistance_document_json(decoded)) == encoded,
        "Canonical outer byte stability");
  return encoded;
}
auto assembly(FreedomKnowledgeSaveDocument& d)
    -> FreedomStartingAssemblySaveDocument& {
  return std::get<FreedomStartingAssemblySaveDocument>(d.voyage.voyage.base);
}
} // namespace
int main(int argc, char** argv) {
  try {
    if (argc != 3)
      throw std::runtime_error(
          "usage: assistance-save actual-landed-save output-report.json");
    Json variants = Json::array();
    for (unsigned drive = 0; drive < 3; ++drive)
      for (unsigned chart = 0; chart < 3; ++chart) {
        auto start =
            need(drive == 2   ? native_jump_strain_new_game({42}, chart)
                 : drive == 1 ? native_jump_recovery_new_game({42}, chart)
                 : chart == 0 ? native_new_game({42})
                 : chart == 1 ? native_network_new_game({42})
                              : native_planetary_network_new_game({42}));
        const auto inner =
            std::get<FreedomKnowledgeSaveDocument>(start.document);
        const auto original =
            need(encode_freedom_knowledge_document_json(inner));
        const auto doc = need(make_freedom_assistance_document(inner));
        const auto text = cycle(doc);
        check(std::get<FreedomKnowledgeSaveDocument>(doc.voyage) == inner,
              "Selection preserves entire original player voyage");
        check(Json::parse(text)["voyage"] == Json::parse(original),
              "Nested canonical player JSON unchanged");
        const auto& state = std::get<OriginServiceCraftState>(doc.actor);
        check(state.attached && state.descriptor.home_port.ordinal == 2 &&
                  state.body.tick == 0 &&
                  state.resources.cargo_quanta ==
                      kOriginServiceCargoCapacityQuanta,
              "Genuine full service actor on D2 same world tick");
        check(assistance_player_attachment(doc.voyage)->port.ordinal == 1,
              "Genuine player D1 remains selected");
        check(need(encode_freedom_knowledge_document_json(inner)) == original,
              "Selecting assistance cannot mutate old source bytes");
        variants.push_back(
            {{"chart", chart}, {"drive", drive}, {"bytes", text.size()}});
      }
    const auto start = need(native_new_game({42}));
    const auto knowledge =
        std::get<FreedomKnowledgeSaveDocument>(start.document);
    const auto source = need(make_freedom_assistance_document(knowledge));
    const auto text = cycle(source);
    const auto base = Json::parse(text);
    const auto context = need(make_origin_assistance_world_context({42}));
    check(!validate_freedom_assistance_document(
              need(make_origin_assistance_world_context({43})), source),
          "Wrong Origin context refuses");
    FreedomRecoverySaveDocument recovery{knowledge, {}};
    auto recovered = source;
    recovered.voyage = recovery;
    cycle(recovered);
    FreedomSurfaceWalkSaveDocument walk{recovery, 1, {}};
    auto suited = source;
    suited.voyage = walk;
    cycle(suited);
    FreedomConditionSaveDocument condition{
        1,
        walk,
        need(make_craft_condition(assistance_player_flight(source.voyage))),
        {}};
    auto conditioned = source;
    conditioned.voyage = condition;
    cycle(conditioned);
    auto invalid = source;
    invalid.version = 2;
    check(!encode_freedom_assistance_document_json(invalid),
          "Unknown outer selection refuses before bytes");
    invalid = source;
    std::get<OriginServiceCraftState>(invalid.actor).body.tick++;
    check(!validate_freedom_assistance_document(context, invalid),
          "Half-tick transaction refuses");
    invalid = source;
    std::get<OriginServiceCraftState>(invalid.actor).resources.cargo_quanta--;
    check(!encode_freedom_assistance_document_json(invalid),
          "Stationed cargo loss refuses");
    invalid = source;
    std::get<OriginServiceCraftState>(invalid.actor).resources.flight_quanta--;
    check(!encode_freedom_assistance_document_json(invalid),
          "Stationed propulsion loss refuses");
    invalid = source;
    std::get<OriginServiceCraftState>(invalid.actor).resources.jump_charges--;
    check(!encode_freedom_assistance_document_json(invalid),
          "Stationed charge loss refuses");
    invalid = source;
    std::get<OriginServiceCraftState>(invalid.actor).attached = false;
    check(!encode_freedom_assistance_document_json(invalid),
          "Unattached actor cannot masquerade as stationed");
    invalid = source;
    std::get<OriginServiceCraftState>(invalid.actor).body.position_metres.x =
        std::numeric_limits<double>::quiet_NaN();
    check(!encode_freedom_assistance_document_json(invalid),
          "Nonfinite actor refuses");
    invalid = source;
    // Labelled valid D2 constraint metadata, not a flown port relocation.
    auto d2 = knowledge;
    auto dock = assembly(d2).journey.voyage;
    dock.docking.target =
        std::get<OriginServiceCraftState>(source.actor).descriptor.home_port;
    dock.flight.flight = std::get<OriginServiceCraftState>(source.actor).body;
    dock.flight.world =
        FreedomActiveWorldSelection{1, dock.flight.flight.frame.system,
                                    dock.flight.origin.recipe.home_planet};
    d2.voyage.voyage.base = dock;
    d2.knowledge = need(
        make_freedom_starting_knowledge({42}, kFreedomTravelKnowledgeVersion));
    FreedomTravelSaveDocument d2_travel{
        d2, {}, NativeStartingAssemblySelection{}, {}};
    check(encode_freedom_travel_document_json(d2_travel).has_value(),
          "Labelled true D2 constraint fixture remains a valid player voyage");
    invalid.voyage = d2_travel;
    check(!encode_freedom_assistance_document_json(invalid),
          "Actual attached player D2 rejects reserved actor port");
    auto nohardware = knowledge;
    const auto nohardware_journey = assembly(nohardware).journey;
    nohardware.voyage.voyage.base = nohardware_journey;
    check(encode_freedom_knowledge_document_json(nohardware).has_value(),
          "No-hardware fixture is valid historic player voyage");
    check(!make_freedom_assistance_document(nohardware),
          "Frame alone does not authenticate selected hardware");
    auto legacy = knowledge;
    assembly(legacy).journey.voyage.flight.model.physical_catalog = 1;
    check(!make_freedom_assistance_document(legacy),
          "Legacy physical owner cannot select assistance");
    const auto recovery_start = need(native_jump_recovery_new_game({42}, 0));
    auto stripped =
        std::get<FreedomKnowledgeSaveDocument>(recovery_start.document);
    stripped.voyage.resources.drive->last_use =
        need(make_freedom_jump_recovery_stamp(FreedomJumpRecoveryKind::system,
                                              1, 123, 0));
    const auto stripped_text =
        need(encode_freedom_knowledge_document_json(stripped));
    check(!native_select_save_document(NativeSaveDocument{stripped}),
          "Used-Knowledge fixture violates existing root admission");
    invalid = source;
    invalid.voyage = stripped;
    check(!encode_freedom_assistance_document_json(invalid),
          "Outer encode preserves used-drive travel-owner requirement");
    auto forged = base;
    forged["voyage"] = Json::parse(stripped_text);
    check(!decode_freedom_assistance_document_json(forged.dump()),
          "Outer decode cannot strip a used drive travel receipt");
    auto rejects = [&](Json value, const char* label) {
      check(!decode_freedom_assistance_document_json(value.dump()), label);
    };
    auto changed = base;
    changed["extra"] = true;
    rejects(changed, "Closed outer keys");
    changed = base;
    changed.erase("version");
    rejects(changed, "Missing selection");
    changed = base;
    changed["version"] = 2U;
    rejects(changed, "Unknown selection");
    changed = base;
    changed["version"] = -1;
    rejects(changed, "Signed negative selection");
    changed = base;
    changed["version"] = 1.0;
    rejects(changed, "Floating selection");
    changed = base;
    changed["version"] = std::numeric_limits<std::uint64_t>::max();
    rejects(changed, "Overflow selection");
    changed = base;
    changed["assistance"]["kind"] = 3U;
    rejects(changed, "Unknown actor kind");
    changed = base;
    changed["assistance"]["kind"] = -1;
    rejects(changed, "Signed actor kind");
    changed = base;
    changed["assistance"]["extra"] = false;
    rejects(changed, "Closed actor envelope");
    changed = base;
    changed["voyage"] = base;
    rejects(changed, "Recursive assistance refuses");
    changed = base;
    changed["voyage"]["unknown"] = false;
    rejects(changed, "Inner malformed shape refuses");
    changed = base;
    changed["assistance"]["state"]["resources"]["tick"] = 1U;
    rejects(changed, "Decoded half-tick actor refuses");
    changed = base;
    changed["format_version"] = 60U;
    const auto unsupported =
        decode_freedom_assistance_document_json(changed.dump());
    check(!unsupported && unsupported.error().code ==
                              SaveSchemaErrorCode::unsupported_format_version,
          "Future format chains through unsupported dispatch");
    const auto old = decode_freedom_assistance_document_json(
        need(encode_freedom_knowledge_document_json(knowledge)));
    check(!old && old.error().code ==
                      SaveSchemaErrorCode::unsupported_format_version,
          "Old root remains owned by historical dispatcher");
    auto duplicate = text;
    duplicate.insert(duplicate.find('{') + 1, "\"version\":1,");
    const auto dup = decode_freedom_assistance_document_json(duplicate);
    check(!dup && dup.error().code == SaveSchemaErrorCode::duplicate_key,
          "Duplicate root key refuses");
    duplicate = text;
    const auto key = duplicate.find("\"assistance\"");
    const auto object = duplicate.find('{', key);
    duplicate.insert(object + 1, "\"kind\":0,");
    check(!decode_freedom_assistance_document_json(duplicate),
          "Duplicate nested key refuses");
    check(!decode_freedom_assistance_document_json(std::string(65, '[') +
                                                   std::string(65, ']')),
          "Overdeep nesting refuses");
    check(!decode_freedom_assistance_document_json(text + "trailing"),
          "Trailing JSON garbage refuses");
    auto padded = text;
    padded.resize(kMaximumSaveDocumentBytes, ' ');
    check(decode_freedom_assistance_document_json(padded).has_value(),
          "Exact byte cap valid whitespace accepted");
    padded.push_back(' ');
    const auto large = decode_freedom_assistance_document_json(padded);
    check(!large &&
              large.error().code == SaveSchemaErrorCode::document_too_large,
          "One byte over cap refuses");
    // Actual continuously reached Moon save. Fuel=0 is the explicit stranded
    // resource fixture; no claim that a flown burn exhausted this receiver.
    auto actual =
        need(NativeFreedomFlightSession::open(need(native_continue(argv[1]))));
    check(actual.moon() && actual.surface() && actual.surface()->moon_landed &&
              actual.resources(),
          "Actual supported lunar recipient fixture");
    const auto actual_doc = need(actual.save_document());
    const auto actual_original =
        need(encode_native_save_document_json(actual_doc));
    FreedomAssistanceVoyage lunar_voyage = std::visit(
        [](const auto& leaf) -> FreedomAssistanceVoyage {
          using T = std::decay_t<decltype(leaf)>;
          if constexpr (std::is_constructible_v<FreedomAssistanceVoyage, T>)
            return leaf;
          else
            throw std::runtime_error(
                "Actual recipient must retain one complete allowed voyage");
        },
        actual_doc);
    const auto player = assistance_player_flight(lunar_voyage);
    const auto lunar_context = need(make_origin_assistance_world_context(
        player.origin.recipe.universe_seed));
    check(lunar_context.moon() == *actual.moon(),
          "Service owner matches actual selected Moon exactly");
    auto receiver = *actual.resources();
    receiver.flight_quanta = 0;
    const auto anchor = *actual.surface()->moon_landed;
    const auto service = need(initialize_origin_service_craft(
        lunar_context.origin(), player.flight.tick));
    const OriginServiceBeacon beacon{1, player.origin.state.craft,
                                     player.origin.lineage, anchor};
    const OriginServiceSortieRecipient recipient{player, anchor, receiver};
    const auto sortie = need(begin_origin_service_sortie(
        lunar_context.origin(), lunar_context.moon(), service, beacon,
        recipient));
    FreedomAssistanceSaveDocument lunar{1, lunar_voyage, sortie};
    const auto lunar_text = cycle(lunar);
    check(assistance_player_flight(lunar.voyage) == player,
          "Active actor retains current player authority");
    check(need(encode_native_save_document_json(actual_doc)) == actual_original,
          "Actual source save never mutated by outer selection");
    auto mismatched = lunar;
    mismatched.actor = need(initialize_origin_service_craft(
        lunar_context.origin(), player.flight.tick + 1));
    check(!encode_freedom_assistance_document_json(mismatched),
          "Same-seed future stationed clock refuses");
    // Archive only a genuine last-known valid sortie. Advancing the ORIGINAL
    // player independently supplies authentic later inner clocks; no NPC future
    // pose, resource pool or failed service simulation is fabricated here.
    auto next_player = actual;
    need(next_player.advance({}));
    const auto as_inner =
        [](const NativeSaveDocument& document) -> FreedomAssistanceVoyage {
      return std::visit(
          [](const auto& leaf) -> FreedomAssistanceVoyage {
            using T = std::decay_t<decltype(leaf)>;
            if constexpr (std::is_constructible_v<FreedomAssistanceVoyage, T>)
              return leaf;
            else
              throw std::runtime_error("Expected bounded actual player voyage");
          },
          document);
    };
    const OriginServiceUnavailable lost{
        sortie, player.flight.tick + 1,
        OriginServiceUnavailableReason::route_unavailable};
    FreedomAssistanceSaveDocument history{
        1, as_inner(need(next_player.save_document())), lost};
    const auto history_text = cycle(history);
    check(std::get<OriginServiceUnavailable>(history.actor).last == sortie &&
              assistance_player_flight(history.voyage).flight.tick ==
                  lost.failed_tick,
          "Unavailable retains exact last real pose/pools/receipt at preceding "
          "tick");
    need(next_player.advance({}));
    auto later_history = history;
    later_history.voyage = as_inner(need(next_player.save_document()));
    cycle(later_history);
    check(later_history.actor == history.actor,
          "Later player clock never ages/refills historical service contact");
    auto bad_history = history;
    std::get<OriginServiceUnavailable>(bad_history.actor).reason =
        static_cast<OriginServiceUnavailableReason>(2);
    check(!encode_freedom_assistance_document_json(bad_history),
          "Unknown unavailable reason cannot encode");
    bad_history = history;
    std::get<OriginServiceUnavailable>(bad_history.actor).failed_tick =
        player.flight.tick;
    check(!encode_freedom_assistance_document_json(bad_history),
          "Unavailable failure cannot equal last known tick");
    bad_history = history;
    std::get<OriginServiceUnavailable>(bad_history.actor).failed_tick =
        player.flight.tick + 2;
    check(!encode_freedom_assistance_document_json(bad_history),
          "Unavailable first failure cannot skip a tick");
    bad_history = history;
    bad_history.voyage = lunar_voyage;
    check(!encode_freedom_assistance_document_json(bad_history),
          "Unavailable event cannot be ahead of current player clock");
    bad_history = history;
    std::get<OriginServiceUnavailable>(bad_history.actor).failed_tick =
        std::numeric_limits<SimulationTick>::max();
    check(!encode_freedom_assistance_document_json(bad_history),
          "Unavailable overflow clock refuses");
    bad_history = history;
    auto& overflowing = std::get<OriginServiceUnavailable>(bad_history.actor);
    overflowing.last.service.body.tick =
        std::numeric_limits<SimulationTick>::max();
    overflowing.last.service.resources.tick =
        std::numeric_limits<SimulationTick>::max();
    overflowing.failed_tick = 0;
    check(!encode_freedom_assistance_document_json(bad_history),
          "Last known max clock cannot wrap to zero failure");
    bad_history = history;
    std::get<OriginServiceUnavailable>(bad_history.actor)
        .last.service.resources.cargo_quanta--;
    check(!encode_freedom_assistance_document_json(bad_history),
          "Unavailable history cannot hide unreceipted cargo changes");
    bad_history = history;
    std::get<OriginServiceUnavailable>(bad_history.actor)
        .last.service.body.position_metres.x =
        std::numeric_limits<double>::quiet_NaN();
    check(!encode_freedom_assistance_document_json(bad_history),
          "Unavailable historical pose must remain authenticated finite state");
    const auto history_json = Json::parse(history_text);
    changed = history_json;
    changed["assistance"]["state"]["extra"] = true;
    rejects(changed, "Closed unavailable state keys");
    changed = history_json;
    changed["assistance"]["state"].erase("last");
    rejects(changed, "Missing historical sortie");
    changed = history_json;
    changed["assistance"]["state"]["reason"] = 2U;
    rejects(changed, "Unknown unavailable reason decoder");
    changed = history_json;
    changed["assistance"]["state"]["reason"] = -1;
    rejects(changed, "Signed negative unavailable reason");
    changed = history_json;
    changed["assistance"]["state"]["reason"] = 1.0;
    rejects(changed, "Floating unavailable reason");
    changed = history_json;
    changed["assistance"]["state"]["reason"] =
        std::numeric_limits<std::uint64_t>::max();
    rejects(changed, "Unavailable reason cannot narrow overflow");
    changed = history_json;
    changed["assistance"]["state"]["failed_tick"] =
        std::to_string(player.flight.tick);
    rejects(changed, "Decoded failure precedes actual failed boundary");
    changed = history_json;
    changed["assistance"]["state"]["failed_tick"] =
        std::to_string(player.flight.tick + 2);
    rejects(changed, "Decoded failure skips first boundary");
    changed = history_json;
    changed["assistance"]["state"]["failed_tick"] =
        "0" + std::to_string(lost.failed_tick);
    rejects(changed, "Leading-zero historical clock refuses");
    changed = history_json;
    changed["assistance"]["state"]["failed_tick"] = "18446744073709551616";
    rejects(changed, "Historical clock uint64 overflow refuses");
    changed = history_json;
    changed["assistance"]["state"]["failed_tick"] = -1;
    rejects(changed,
            "Numeric historical clock refuses canonical-string contract");
    changed = history_json;
    changed["assistance"]["state"]["last"]["service"]["resources"]
           ["cargo_quanta"] = 0U;
    rejects(changed,
            "Decoded history cannot discard unreceipted service cargo");
    changed = history_json;
    changed["assistance"]["state"]["last"]["phase_start_tick"] =
        player.flight.tick + 1;
    rejects(changed,
            "Decoded history must validate at actual last known clock");
    // D2 remains reserved even though the historical contact does not claim a
    // live pose at this clock. This is the same labelled true-constraint
    // fixture.
    auto history_d2 = d2_travel;
    auto& history_dock = std::get<FreedomDockingSaveDocument>(
        history_d2.voyage.voyage.voyage.base);
    const auto later_tick = player.flight.tick + 1;
    const auto station = generate_origin_station({42});
    const auto geometry = need(origin_station_geometry(station));
    history_dock.flight.flight =
        need(release_origin_port(lunar_context.origin(), station, geometry,
                                 {geometry.version,
                                  {station.id, 2},
                                  history_dock.flight.flight.craft,
                                  later_tick}));
    history_dock.flight.origin.state.tick = later_tick;
    history_d2.voyage.voyage.resources.tick = later_tick;
    check(encode_freedom_travel_document_json(history_d2).has_value(),
          "Historical D2 refusal fixture is a genuine same-tick constraint "
          "voyage");
    bad_history = history;
    bad_history.voyage = history_d2;
    check(!encode_freedom_assistance_document_json(bad_history),
          "Unavailable contact retains the selected D2 reservation");
    Json report{{"pass", true},
                {"scope", "Format59 codec contract; actual shared-session and "
                          "native presentation contracts run separately"},
                {"checks", checks},
                {"new_games", variants},
                {"actual_moon_tick", player.flight.tick},
                {"requested_sortie_bytes", lunar_text.size()},
                {"unavailable_bytes", history_text.size()},
                {"stationed_bytes", text.size()}};
    std::ofstream(argv[2]) << report.dump(2) << '\n';
    std::cout << checks << " checks, 0 failures\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << " after " << checks << " checks\n";
    return 1;
  }
}
