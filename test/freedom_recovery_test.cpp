#include "apsis_drift/freedom_recovery_save.hpp"
#include "apsis_drift/native_flight_session.hpp"

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
int checks{}, failures{};
std::uint64_t fingerprint{14695981039346656037ULL};
auto hash(std::string_view text) -> void {
  for (char c : text) {
    fingerprint ^= static_cast<unsigned char>(c);
    fingerprint *= 1099511628211ULL;
  }
}
auto check(bool value, std::string_view message) -> void {
  ++checks;
  if (!value) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) {
    if constexpr (std::is_same_v<E, SaveSchemaError>)
      throw std::runtime_error(value.error().detail);
    else if constexpr (std::is_same_v<E, SaveFileError>)
      throw std::runtime_error(save_file_error_message(value.error()));
    else if constexpr (std::is_same_v<E, std::string>)
      throw std::runtime_error(value.error());
    else
      throw std::runtime_error("Recovery fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto session_contract(const std::filesystem::path& path) -> void {
  auto selected = need(native_new_game({42}));
  auto& empty = std::get<FreedomKnowledgeSaveDocument>(selected.document)
                    .voyage.resources;
  empty.flight_quanta = 0;
  empty.jump_charges = 0;
  auto live = need(NativeFreedomFlightSession::open(std::move(selected)));
  for (int i = 0; i < 120; ++i)
    need(live.advance_walk({}));
  const auto original = need(live.recovery_document());
  check(!original.recovery.latest && !live.recovery_pending() &&
            live.resources()->flight_quanta == 0 &&
            live.resources()->jump_charges == 0,
        "Empty resources do not invent destruction or a free replacement");
  const auto craft = live.document().origin.state.craft;
  const auto tick = live.document().flight.tick;
  const auto checksum = need(freedom_recovery_source_checksum(original.voyage));
  need(live.save_as(path));
  auto before =
      need(NativeFreedomFlightSession::open(need(native_continue(path))));
  check(need(before.recovery_document()) == original &&
            !before.complete_recovery(),
        "Preloss Continue preserves historical startup and cannot grant "
        "replacement");
  need(live.record_loss(FreedomLossCause::irrecoverable_destruction, craft,
                        tick, checksum));
  const auto pending = need(live.recovery_document());
  NativeFlightControls bad;
  bad.positive_translation.x = std::numeric_limits<double>::quiet_NaN();
  check(live.recovery_pending() && !live.advance({}) && !live.advance(bad) &&
            !live.advance_walk({}) && !live.set_assistance(false) &&
            !live.set_hold({}) && !live.begin_boarding() &&
            !live.begin_disembarking() && !live.release_port() &&
            !live.capture_port() && !live.select_port(1) &&
            !live.begin_port_approach() && !live.set_landing_gear(true) &&
            !live.request_landing() && !live.release_surface() &&
            !live.commit_touchdown(checksum) && !live.begin_jump() &&
            !live.cancel_jump() &&
            !live.select_jump(
                generate_first_intersystem_identities({42}).target_system) &&
            !live.replenish_resources() &&
            need(live.recovery_document()) == pending,
        "Pending loss freezes all flight/walk/control/service mutations and "
        "clock");
  need(live.save_as(path));
  auto resumed =
      need(NativeFreedomFlightSession::open(need(native_continue(path))));
  check(resumed.recovery_pending() &&
            need(resumed.recovery_document()) == pending,
        "Pending loss Continue keeps the exact frozen sole owner");
  need(live.complete_recovery());
  need(resumed.complete_recovery());
  check(need(live.recovery_document()) == need(resumed.recovery_document()) &&
            live.walker() && live.docking()->attached && !live.boarding() &&
            !live.travel() && !live.recovery_pending(),
        "Live and resumed completion converge to actual fresh "
        "station/walker/skin ownership");
  need(live.save_as(path));
  resumed = need(NativeFreedomFlightSession::open(need(native_continue(path))));
  check(need(resumed.recovery_document()) == need(live.recovery_document()),
        "Completed Continue retains replacement identity and history owner");
  need(live.advance_walk({.forward = 1}));
  need(resumed.advance_walk({.forward = 1}));
  const auto moved = need(live.recovery_document());
  need(live.complete_recovery());
  need(live.record_loss(FreedomLossCause::irrecoverable_destruction, craft,
                        tick, checksum));
  check(need(live.recovery_document()) == moved &&
            need(resumed.recovery_document()) == moved &&
            live.document().flight.tick == tick + 1,
        "Replacement walking works; repeated old event/completion cannot reset "
        "clock, pose or fuel");
  const auto bare = std::get<FreedomKnowledgeSaveDocument>(moved.voyage);
  need(write_freedom_knowledge_file_atomically(path, bare));
  check(load_native_save_file(path).has_value() && !native_continue(path) &&
            !NativeFreedomFlightSession::open(
                {NativeStartup::Mode::freedom,
                 bare,
                 live.system().catalog.planets[0].descriptor,
                 {}}),
        "Lower codec remains usable internally but bare replacement cannot "
        "bypass recovery authority");
  need(live.save_as(path));
  auto invalid = moved;
  invalid.recovery.latest->source_checksum = checksum ^ 1;
  check(
      !write_freedom_recovery_file_atomically(path, invalid) &&
          need(NativeFreedomFlightSession::open(need(native_continue(path))))
                  .document() == live.document(),
      "Invalid replacement write preserves the previous valid save atomically");
  const auto unchanged = need(live.recovery_document());
  check(!live.record_loss(FreedomLossCause::recoverable_destruction, craft,
                          tick, checksum) &&
            need(live.recovery_document()) == unchanged,
        "Contradictory repeated cause cannot destroy the replacement");
  need(live.select_jump(
      generate_first_intersystem_identities({42}).target_system));
  for (unsigned t = 0; t < 2960; ++t)
    need(live.advance_walk({0, -1, 0}));
  need(live.begin_boarding());
  for (unsigned t = 0; t < kGameplayBoardingTicks; ++t)
    need(live.advance_walk({}));
  need(live.release_port());
  NativeFlightControls withdrawal;
  withdrawal.negative_translation.y = 1;
  for (unsigned t = 0; t < 720; ++t)
    need(live.advance(withdrawal));
  need(live.begin_jump());
  need(live.save_as(path));
  resumed = need(NativeFreedomFlightSession::open(need(native_continue(path))));
  for (unsigned t = 0; t < kJumpSpoolTicks + kJumpTransitTicks; ++t) {
    need(live.advance({}));
    need(resumed.advance({}));
    if (t + 1 == kJumpSpoolTicks ||
        t + 1 == kJumpSpoolTicks + kJumpTransitTicks) {
      need(resumed.save_as(path));
      resumed =
          need(NativeFreedomFlightSession::open(need(native_continue(path))));
    }
  }
  check(need(live.recovery_document()) == need(resumed.recovery_document()) &&
            live.resources()->jump_charges == 2 &&
            live.travel()->next_attempt == 2 &&
            live.travel()->committed->preview.request.lineage ==
                live.document().origin.lineage &&
            live.system().catalog.id ==
                generate_first_intersystem_identities({42}).target_system,
        "Replacement actually walks, boards, departs, scans and jumps; "
        "source/commit/arrival Continue converges");
  const auto onward = need(live.recovery_document());
  const auto current_system = live.system().catalog.id;
  const KnowledgeEvidence retired_evidence{
      {KnowledgeSubjectKind::system, current_system, current_system.value},
      KnowledgeFact::presence,
      {NavigationKnowledgeLevel::visited, KnowledgeSource::physical_arrival,
       craft.value, live.document().flight.tick, craft}};
  check(!live.record_observation(retired_evidence) &&
            need(live.recovery_document()) == onward,
        "Retired craft cannot claim new physical evidence after replacement");
  hash(need(
      encode_freedom_recovery_document_json(need(live.recovery_document()))));
}
auto provider(Seed seed, FreedomLossCause cause, bool stale_checkpoint)
    -> void {
  auto selected = need(native_new_game(seed));
  auto voyage = std::get<FreedomKnowledgeSaveDocument>(selected.document);
  auto& initial =
      std::get<FreedomStartingAssemblySaveDocument>(voyage.voyage.voyage.base);
  initial.journey.voyage.flight.origin.state.discoveries.push_back({{77}, 0});
  initial.journey.voyage.flight.origin.state.world_deltas.push_back(
      {"signal:77", SaveWorldDeltaKind::collected, 0});
  selected.document = voyage;
  auto session = need(NativeFreedomFlightSession::open(std::move(selected)));
  for (int i = 0; i < 240; ++i)
    need(session.advance_walk({}));
  voyage = {{session.surface_document(), *session.resources()},
            *session.knowledge()};
  FreedomRecoverySaveDocument before{voyage, {}};
  const auto& source = recovery_flight(before.voyage);
  const auto craft = source.origin.state.craft;
  const auto tick = source.flight.tick;
  const auto checksum = need(freedom_recovery_source_checksum(before.voyage));
  before.recovery.checkpoint =
      FreedomSafeStationCheckpoint{1, {source.origin.state.station, 1}, 0};
  if (stale_checkpoint) before.recovery.checkpoint->port.station.value ^= 1;
  const auto before_text = need(encode_freedom_recovery_document_json(before));
  check(need(decode_freedom_recovery_document_json(before_text)) == before &&
            !complete_freedom_recovery(before),
        "Before-loss state stays live and cannot mint a ship");
  const auto pending =
      need(record_freedom_loss(before, cause, craft, tick, checksum));
  check(pending.recovery.pending && pending.voyage == before.voyage &&
            need(record_freedom_loss(pending, cause, craft, tick, checksum)) ==
                pending,
        "Loss freezes the actual voyage and matching events are idempotent");
  const auto pending_text =
      need(encode_freedom_recovery_document_json(pending));
  const auto resumed =
      need(decode_freedom_recovery_document_json(pending_text));
  const auto complete = need(complete_freedom_recovery(pending));
  check(need(complete_freedom_recovery(resumed)) == complete &&
            need(complete_freedom_recovery(complete)) == complete &&
            need(record_freedom_loss(complete, cause, craft, tick, checksum)) ==
                complete,
        "Pending save and duplicate completion/event converge exactly once");
  const auto& fresh = recovery_flight(complete.voyage);
  const auto& resources = recovery_knowledge(complete.voyage).voyage.resources;
  const auto& knowledge = recovery_knowledge(complete.voyage).knowledge;
  check(
      !resolve_freedom_knowledge_chart(knowledge, fresh.flight.frame.system,
                                       resources) &&
          resolve_freedom_knowledge_chart(knowledge, fresh.flight.frame.system,
                                          resources, true, &fresh.origin)
              .has_value() &&
          !resolve_freedom_knowledge_chart(knowledge, fresh.flight.frame.system,
                                           resources, true, &source.origin),
      "Replacement chart requires its exact current identity/clock; retired "
      "owner cannot authorize it");
  const auto& assembly = std::get<FreedomStartingAssemblySaveDocument>(
      recovery_knowledge(complete.voyage).voyage.voyage.base);
  check(!complete.recovery.pending && fresh.origin.lineage->generation == 1 &&
            fresh.origin.state.craft != craft &&
            fresh.origin.recipe == source.origin.recipe &&
            fresh.flight.tick == tick && fresh.origin.state.tick == tick &&
            fresh.origin.state.discoveries == source.origin.state.discoveries &&
            fresh.origin.state.world_deltas ==
                source.origin.state.world_deltas &&
            resources.craft == fresh.origin.state.craft &&
            resources.tick == tick &&
            resources.flight_quanta == kFreedomFlightCapacityQuanta &&
            resources.jump_charges == 3 &&
            assembly.journey.voyage.docking.attached &&
            assembly.journey.voyage.docking.target.ordinal == 1,
        "Exactly one fresh full starter at real D1 preserves universe, clock "
        "and changes");
  check(need(resolve_freedom_recovery_station(pending)).fallback ==
                stale_checkpoint &&
            need(resolve_freedom_recovery_station(complete)).fallback ==
                stale_checkpoint,
        "Remembered station and validated fallback are deterministic");
  auto future_knowledge = recovery_knowledge(complete.voyage).knowledge;
  const auto future_craft = need(derive_freedom_craft_identity(seed, 2));
  const auto route = generate_first_intersystem_identities(seed);
  const KnowledgeEvidence future_evidence{
      {KnowledgeSubjectKind::system, route.target_system,
       route.target_system.value},
      KnowledgeFact::presence,
      {NavigationKnowledgeLevel::visited, KnowledgeSource::physical_arrival,
       future_craft.value, tick, future_craft}};
  check(!apply_freedom_knowledge(future_knowledge, future_evidence, tick),
        "Selected provenance ceiling refuses a not-yet-owned future craft");
  future_knowledge.recipe.craft_lineage->generation = 2;
  const auto future_ledger =
      apply_freedom_knowledge(future_knowledge, future_evidence, tick);
  check(future_ledger.has_value(),
        "Explicit later lineage admits its own craft without reinterpreting "
        "retained provenance");
  const auto explanation = need(freedom_recovery_explanation(pending));
  check(
      explanation.contains(cause == FreedomLossCause::irrecoverable_destruction
                               ? "no reachable wreck"
                               : "recoverable wreck") &&
          explanation.contains("three jump charges") &&
          explanation.contains("retained"),
      "Cause, continuation and retained state are explained");
  const auto text = need(encode_freedom_recovery_document_json(complete));
  hash(text);
  check(need(decode_freedom_recovery_document_json(text)) == complete &&
            text.size() < 100'000,
        "Completed owner persists with bounded nonrecursive retired history");
  check(
      !record_freedom_loss(before, cause, {craft.value ^ 1}, tick, checksum) &&
          !record_freedom_loss(before, cause, craft, tick + 1, checksum) &&
          !record_freedom_loss(before, cause, craft, tick, checksum ^ 1) &&
          !record_freedom_loss(before, static_cast<FreedomLossCause>(255),
                               craft, tick, checksum),
      "Unknown cause and stale identity/clock/source fail without mutation");
  auto corrupt = pending;
  corrupt.recovery.latest->source_checksum = checksum ^ 1;
  check(!validate_freedom_recovery_document(corrupt),
        "Corrupt loss checksum refused");
  corrupt = pending;
  corrupt.recovery.checkpoint->tick = tick + 1;
  check(!validate_freedom_recovery_document(corrupt),
        "Future checkpoint refused");
  corrupt = complete;
  auto& modified = std::get<FreedomStartingAssemblySaveDocument>(
      std::get<FreedomKnowledgeSaveDocument>(corrupt.voyage)
          .voyage.voyage.base);
  modified.journey.voyage.flight.origin.state.world_deltas.clear();
  check(!validate_freedom_recovery_document(corrupt),
        "Rolled-back persistent changes refused");
  auto duplicate = text;
  duplicate.insert(duplicate.find('{') + 1, "\"format_version\":29,");
  auto duplicate_result = decode_freedom_recovery_document_json(duplicate);
  check(!duplicate_result &&
            duplicate_result.error().code == SaveSchemaErrorCode::duplicate_key,
        "Repeated owner key refuses rather than overwriting authority");
  auto root = Json::parse(text);
  for (const auto& value :
       {Json(2U), Json(4'294'967'296ULL), Json(-1), Json("1")}) {
    auto bad = root;
    bad["recovery"]["version"] = value;
    check(!decode_freedom_recovery_document_json(bad.dump()),
          "Wide/unknown/type recovery version refused before narrowing");
  }
  auto bad = root;
  bad["recovery"]["latest"]["retired"] = root;
  check(!decode_freedom_recovery_document_json(bad.dump()),
        "Recursive recovery history refused");
  bad = root;
  bad["recovery"]["checkpoint"]["port"] = 4'294'967'296ULL;
  check(!decode_freedom_recovery_document_json(bad.dump()),
        "Wide port refused before narrowing");
  check(!decode_freedom_recovery_document_json(std::string(65, '[') + "0" +
                                               std::string(65, ']')) &&
            !decode_freedom_recovery_document_json(
                std::string(kMaximumSaveDocumentBytes + 1, ' ')),
        "Deep and oversized wrappers refuse before JSON allocation");
  const auto second_source = recovery_flight(complete.voyage);
  const auto second = need(record_freedom_loss(
      complete, cause, second_source.origin.state.craft, tick,
      need(freedom_recovery_source_checksum(complete.voyage))));
  const auto twice = need(complete_freedom_recovery(second));
  check(recovery_flight(twice.voyage).origin.lineage->generation == 2 &&
            recovery_flight(twice.voyage).origin.state.craft !=
                fresh.origin.state.craft &&
            !record_freedom_loss(twice, cause, craft, tick, checksum) &&
            need(encode_freedom_recovery_document_json(twice)).size() < 100'000,
        "A distinct later loss replaces once, retires generations, and bounds "
        "latest history");
}
auto limits(Seed seed) -> void {
  auto selected = need(native_new_game(seed));
  auto session = need(NativeFreedomFlightSession::open(std::move(selected)));
  auto source = need(session.recovery_document());
  const auto& start = recovery_flight(source.voyage);
  auto pending = need(record_freedom_loss(
      source, FreedomLossCause::irrecoverable_destruction,
      start.origin.state.craft, start.flight.tick,
      need(freedom_recovery_source_checksum(source.voyage))));
  auto limit = need(complete_freedom_recovery(pending));
  auto& voyage = std::get<FreedomKnowledgeSaveDocument>(limit.voyage);
  auto& flight =
      std::get<FreedomStartingAssemblySaveDocument>(voyage.voyage.voyage.base)
          .journey.voyage.flight;
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  flight.origin.lineage->generation = maximum - 2;
  flight.origin.state.craft =
      need(derive_freedom_craft_identity(seed, maximum - 2));
  voyage.voyage.resources.craft = flight.origin.state.craft;
  voyage.knowledge.recipe.craft_lineage = flight.origin.lineage;
  limit.recovery.latest = FreedomLossRecord{
      FreedomLossCause::irrecoverable_destruction, limit.voyage,
      need(freedom_recovery_source_checksum(limit.voyage))};
  limit.recovery.pending = true;
  const auto last = need(complete_freedom_recovery(limit));
  check(recovery_flight(last.voyage).origin.lineage->generation ==
                maximum - 1 &&
            !record_freedom_loss(
                last, FreedomLossCause::irrecoverable_destruction,
                recovery_flight(last.voyage).origin.state.craft, 0,
                need(freedom_recovery_source_checksum(last.voyage))),
        "Last unique lineage is usable; next loss refuses before cycling an "
        "instance");
  limit.voyage = last.voyage;
  limit.recovery.latest = FreedomLossRecord{
      FreedomLossCause::irrecoverable_destruction, limit.voyage,
      need(freedom_recovery_source_checksum(limit.voyage))};
  check(!validate_freedom_recovery_document(limit),
        "Exhausted pending lineage refuses rather than trapping Continue");
  auto& clock_voyage = std::get<FreedomKnowledgeSaveDocument>(limit.voyage);
  auto& clock_flight = std::get<FreedomStartingAssemblySaveDocument>(
                           clock_voyage.voyage.voyage.base)
                           .journey.voyage.flight;
  clock_flight.origin.lineage->generation = 1;
  clock_flight.origin.state.craft =
      need(derive_freedom_craft_identity(seed, 1));
  clock_flight.origin.state.tick = maximum - 2;
  const auto station = generate_origin_station(seed);
  const auto geometry = need(origin_station_geometry(station));
  const auto system = need(generate_physical_origin_system(seed, 2));
  clock_flight.flight = need(release_origin_port(
      system, station, geometry,
      {1, {station.id, 1}, wayfarer_frame().recipe, maximum - 2}));
  clock_voyage.voyage.resources.craft = clock_flight.origin.state.craft;
  clock_voyage.knowledge.recipe.craft_lineage = clock_flight.origin.lineage;
  clock_voyage.voyage.resources.tick = maximum - 2;
  limit.recovery.latest = FreedomLossRecord{
      FreedomLossCause::irrecoverable_destruction, limit.voyage,
      need(freedom_recovery_source_checksum(limit.voyage))};
  check(!validate_freedom_recovery_document(limit),
        "Pending loss cannot consume the last nonadvanceable simulation clock");
}
auto lineage(Seed seed) -> void {
  check(!decode_freedom_save_document_json(std::string(65, '[') + "0" +
                                           std::string(65, ']')),
        "Bare origin decoder bounds malformed nesting before lineage/tree "
        "allocation");
  auto original = make_freedom_new_game_document(seed);
  const auto before = need(encode_freedom_save_document_json(original));
  const auto legacy = Json::parse(before);
  check(legacy["format_version"] == 17 && !legacy.contains("craft_lineage") &&
            need(decode_freedom_save_document_json(before)) == original &&
            need(derive_freedom_craft_identity(seed, 0)) ==
                original.state.craft,
        "Unselected starter keeps the legacy identity and closed format17");
  auto replacement = original;
  replacement.lineage = FreedomCraftLineage{1, 1};
  replacement.state.craft = need(derive_freedom_craft_identity(seed, 1));
  replacement.state.tick = 900;
  replacement.state.discoveries.push_back({{77}, 120});
  replacement.state.world_deltas.push_back(
      {"signal:77", SaveWorldDeltaKind::collected, 240});
  check(replacement.state.craft.value &&
            replacement.state.craft != original.state.craft &&
            need(derive_freedom_craft_identity(seed, 2)) !=
                replacement.state.craft &&
            replacement.recipe == original.recipe,
        "Replacement differs from retired instances without rerolling world");
  const auto text = need(encode_freedom_save_document_json(replacement));
  const auto root = Json::parse(text);
  const auto restored = need(decode_freedom_save_document_json(text));
  check(root["format_version"] == 28 &&
            root["craft_lineage"]["generation"] == "1" &&
            restored == replacement &&
            need(encode_freedom_save_document_json(restored)) == text,
        "Selected lineage preserves identity, recipe, clock and all history");
  auto corrupt = root;
  const auto refusal = [](const Json& value) {
    check(!decode_freedom_save_document_json(value.dump()),
          "Invalid selected lineage refuses before replacement");
  };
  for (const auto& value :
       {Json("0"), Json("01"), Json(1U), Json("18446744073709551615"),
        Json("18446744073709551616")}) {
    corrupt = root;
    corrupt["craft_lineage"]["generation"] = value;
    refusal(corrupt);
  }
  corrupt = root;
  corrupt["craft_lineage"]["version"] = 2U;
  refusal(corrupt);
  corrupt = root;
  corrupt["craft_lineage"]["extra"] = true;
  refusal(corrupt);
  corrupt = root;
  corrupt["state"]["starter_craft_id"] =
      std::to_string(original.state.craft.value);
  refusal(corrupt);
  corrupt = root;
  corrupt["format_version"] = 17U;
  refusal(corrupt);
  corrupt.erase("craft_lineage");
  corrupt["state"]["starter_craft_id"] =
      std::to_string(replacement.state.craft.value);
  refusal(corrupt);
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  const auto last = need(derive_freedom_craft_identity(seed, maximum - 1));
  check(last.value == (original.state.craft.value == 1
                           ? maximum
                           : original.state.craft.value - 1) &&
            !derive_freedom_craft_identity(seed, maximum),
        "Identity wrap excludes zero and refuses before a repeated starter");
}
} // namespace
auto main() -> int {
  const auto path =
      std::filesystem::temp_directory_path() /
      ("apsis-drift-recovery-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(path);
  try {
    for (auto seed :
         {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}}) {
      lineage(seed);
      limits(seed);
      for (auto cause : {FreedomLossCause::recoverable_destruction,
                         FreedomLossCause::irrecoverable_destruction})
        for (bool stale : {false, true})
          provider(seed, cause, stale);
    }
    session_contract(path / "recovery.json");
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    std::filesystem::remove_all(path);
    return 1;
  }
  std::filesystem::remove_all(path);
  std::cout << "Freedom recovery: " << checks << " checks, " << failures
            << " failures, fingerprint " << fingerprint << '\n';
  return failures ? 1 : 0;
}
