#include "apsis_drift/freedom_recovery_save.hpp"
#include "apsis_drift/native_craft_binding.hpp"
#include "apsis_drift/native_flight_session.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <source_location>
#include <stdexcept>
#include <type_traits>

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
template <class T, class E>
auto need(std::expected<T, E> value,
          const std::source_location where = std::source_location::current())
    -> T {
  if (!value) {
    const auto context = std::string{where.file_name()} + ":" +
                         std::to_string(where.line()) + ": ";
    if constexpr (std::is_same_v<E, SaveSchemaError>)
      throw std::runtime_error(context + value.error().detail);
    else if constexpr (std::is_same_v<E, SaveFileError>)
      throw std::runtime_error(context +
                               save_file_error_message(value.error()));
    else if constexpr (std::is_same_v<E, std::string>)
      throw std::runtime_error(context + value.error());
    else
      throw std::runtime_error(context + "Recovery fixture refused");
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
            !live.set_hold({}) && !live.current_orbit_hold_target() &&
            !live.hold_current_orbit() && !live.begin_boarding() &&
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
auto landmark_retention() -> void {
  const Seed seed{42};
  const auto owner = need(generate_physical_origin_system(seed, 2));
  const auto& planet = owner.catalog.planets.front().descriptor;
  std::vector<FreedomSurfaceLandmarkInspection> records;
  const auto shell_x = static_cast<std::int32_t>(
      std::floor(static_cast<double>(planet.radius.value) * 1000 /
                 kSurfaceLandmarkCellMetres));
  // Identity-only compatibility fixtures; actual in-range player inspection
  // is separately exercised by the landed-craft session test.
  for (std::int32_t x = -1; x <= 1; ++x)
    for (std::int32_t y = -2; y <= 2; ++y)
      for (std::int32_t z = -2; z <= 2; ++z) {
        const SurfaceLandmarkCell cell{shell_x + x, y, z};
        const auto identity = need(identify_surface_landmark(planet, cell));
        if (identity && records.size() < 3)
          records.push_back(
              {owner.catalog.id, planet.id, cell, identity->id, 3, 7});
      }
  if (records.size() != 3)
    throw std::runtime_error("Recovery landmark fixtures unavailable");
  std::ranges::sort(records, {},
                    [](const auto& record) { return record.site.value; });
  auto session =
      need(NativeFreedomFlightSession::open(need(native_new_game(seed))));
  for (unsigned tick = 0; tick < 120; ++tick)
    need(session.advance_walk({}));
  const auto source = need(session.recovery_document());
  const auto complete_with =
      [&](std::optional<FreedomSurfaceLandmarkState> state) {
        auto before = source;
        std::get<FreedomKnowledgeSaveDocument>(before.voyage)
            .knowledge.surface_landmarks = std::move(state);
        const auto flight = recovery_flight(before.voyage);
        return need(complete_freedom_recovery(need(record_freedom_loss(
            before, FreedomLossCause::recoverable_destruction,
            flight.origin.state.craft, flight.flight.tick,
            need(freedom_recovery_source_checksum(before.voyage))))));
      };
  const auto complete =
      complete_with(FreedomSurfaceLandmarkState{1, {records[0], records[1]}});
  const auto text = need(encode_freedom_recovery_document_json(complete));
  check(need(decode_freedom_recovery_document_json(text)) == complete,
        "Completed replacement retains all inspection records exactly");
  for (unsigned fault = 0; fault < 4; ++fault) {
    auto bad = Json::parse(text);
    auto& knowledge = bad["voyage"]["knowledge"];
    auto& landmarks = knowledge["surface_landmarks"];
    switch (fault) {
      case 0: knowledge.erase("surface_landmarks"); break;
      case 1: landmarks["inspections"][0]["tick"] = 8U; break;
      case 2: landmarks["inspections"].erase(0); break;
      case 3: landmarks["inspections"][0]["terrain_policy"] = 2U; break;
      default: break;
    }
    check(!decode_freedom_recovery_document_json(bad.dump()),
          "Completed recovery cannot drop selection or old inspections, "
          "rewrite first ticks or change their contact policy");
  }
  const auto loss_tick = recovery_flight(source.voyage).flight.tick;
  auto onward = complete;
  auto& inspections = std::get<FreedomKnowledgeSaveDocument>(onward.voyage)
                          .knowledge.surface_landmarks->inspections;
  auto addition = records[2];
  addition.tick = loss_tick;
  inspections.push_back(addition);
  check(need(decode_freedom_recovery_document_json(
            need(encode_freedom_recovery_document_json(onward)))) == onward,
        "Replacement may append valid inspections at or after the loss tick");
  inspections.back().tick = loss_tick - 1;
  check(!encode_freedom_recovery_document_json(onward),
        "Replacement cannot backdate newly appended inspection history");
  auto adopted = complete_with(std::nullopt);
  auto& selection = std::get<FreedomKnowledgeSaveDocument>(adopted.voyage)
                        .knowledge.surface_landmarks;
  selection = FreedomSurfaceLandmarkState{};
  check(need(decode_freedom_recovery_document_json(
            need(encode_freedom_recovery_document_json(adopted)))) == adopted,
        "Historical replacement may explicitly adopt an empty selection");
  selection->inspections = {addition};
  check(need(decode_freedom_recovery_document_json(
            need(encode_freedom_recovery_document_json(adopted)))) == adopted,
        "Adoption permits new valid inspection history after loss");
  selection->inspections.front().tick = loss_tick - 1;
  check(!encode_freedom_recovery_document_json(adopted),
        "Historical adoption cannot invent inspections before its loss");

  // Identity-only persistence fixtures. Actual supported collection is covered
  // by the planet/Moon session routes, not by this constructed history.
  auto sampled = source;
  auto& sample_knowledge =
      std::get<FreedomKnowledgeSaveDocument>(sampled.voyage).knowledge;
  sample_knowledge.surface_landmarks = FreedomSurfaceLandmarkState{1, records};
  const auto old_craft = recovery_flight(sampled.voyage).origin.state.craft;
  const auto specimen = [&](std::size_t index, StarterCraftId collector,
                            SimulationTick tick) {
    const auto& inspection = records[index];
    const auto identity =
        need(identify_surface_landmark(planet, inspection.cell));
    if (!identity) throw std::runtime_error("Sample identity unavailable");
    return FreedomFieldSampleRecord{{owner.catalog.id, body_id(planet.id)},
                                    inspection.cell,
                                    inspection.site,
                                    inspection.terrain_policy,
                                    identity->kind,
                                    collector,
                                    tick};
  };
  const std::vector notes{specimen(0, old_craft, 20),
                          specimen(1, old_craft, 21)};
  sample_knowledge.field_samples =
      FreedomFieldSampleState{1, old_craft, notes, notes};
  const auto loss = need(record_freedom_loss(
      sampled, FreedomLossCause::recoverable_destruction, old_craft, loss_tick,
      need(freedom_recovery_source_checksum(sampled.voyage))));
  auto replaced = need(complete_freedom_recovery(loss));
  const auto new_craft = recovery_flight(replaced.voyage).origin.state.craft;
  const auto& retained = recovery_knowledge(replaced.recovery.latest->retired)
                             .knowledge.field_samples;
  const auto& replacement_samples =
      recovery_knowledge(replaced.voyage).knowledge.field_samples;
  check(retained == sample_knowledge.field_samples && replacement_samples &&
            replacement_samples->carrier == new_craft &&
            replacement_samples->carried.empty() &&
            replacement_samples->journal == notes,
        "Replacement clears carried specimens, rebinds the pouch and retains "
        "first collection notes and the complete retired specimen snapshot");
  const auto sample_text =
      need(encode_freedom_recovery_document_json(replaced));
  check(need(decode_freedom_recovery_document_json(sample_text)) == replaced &&
            need(freedom_recovery_explanation(replaced))
                .contains("collection notes are retained"),
        "Sample loss and retained notes survive the actual recovery envelope "
        "and are explained to the player");
  for (unsigned fault = 0; fault < 5; ++fault) {
    auto forged = replaced;
    auto& state = std::get<FreedomKnowledgeSaveDocument>(forged.voyage)
                      .knowledge.field_samples;
    switch (fault) {
      case 0: state.reset(); break;
      case 1: state->journal.erase(state->journal.begin()); break;
      case 2: ++state->journal.front().tick; break;
      case 3: state->carried = {specimen(0, new_craft, loss_tick - 1)}; break;
      case 4:
        state->journal.push_back(specimen(2, new_craft, loss_tick - 1));
        break;
      default: break;
    }
    check(!encode_freedom_recovery_document_json(forged),
          "Replacement refuses dropped/rewritten notes and backdated new "
          "specimens or collection history");
  }
  auto& recollected = std::get<FreedomKnowledgeSaveDocument>(replaced.voyage)
                          .knowledge.field_samples;
  recollected->carried = {specimen(0, new_craft, loss_tick)};
  recollected->journal.push_back(specimen(2, new_craft, loss_tick));
  check(need(decode_freedom_recovery_document_json(need(
            encode_freedom_recovery_document_json(replaced)))) == replaced &&
            recollected->journal.front() == notes.front(),
        "A later valid specimen can reuse an old source while its first note "
        "remains exact; new notes obey the replacement clock and collector");
}
auto provider(Seed seed, FreedomLossCause cause, bool stale_checkpoint,
              std::uint32_t optics = 1,
              std::uint32_t terrain_policy = kFreedomSeaDatumTerrainPolicy)
    -> void {
  auto selected = need(native_new_game(seed));
  auto voyage = std::get<FreedomKnowledgeSaveDocument>(selected.document);
  auto& initial =
      std::get<FreedomStartingAssemblySaveDocument>(voyage.voyage.voyage.base);
  initial.journey.voyage.flight.model.optics = optics;
  initial.journey.voyage.flight.model.companions = optics ? 1U : 0U;
  initial.journey.voyage.flight.model.terrain_policy = terrain_policy;
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
  check(source.model.companions == (optics ? 1U : 0U) &&
            fresh.model.companions == source.model.companions &&
            recovery_flight(resumed.voyage).model.companions ==
                source.model.companions &&
            source.model.optics == optics && fresh.model.optics == optics &&
            recovery_flight(resumed.voyage).model.optics == optics &&
            source.model.terrain_policy == terrain_policy &&
            fresh.model.terrain_policy == terrain_policy &&
            recovery_flight(resumed.voyage).model.terrain_policy ==
                terrain_policy,
        "loss Continue and replacement preserve selected or historical terrain "
        "and optics");
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
auto composed_fixture() -> FreedomTravelSaveDocument {
  // Saved-space codec fixture, not a claimed flown departure or descent.
  // The pilot is constructed from the genuine station entry/boarding route.
  const Seed seed{42};
  auto origin = need(make_freedom_network_new_game_document(seed));
  const auto system = need(generate_physical_origin_system(seed, 2));
  const auto station = generate_origin_station(seed);
  const auto geometry = need(origin_station_geometry(station));
  const auto port = need(resolve_origin_port_pose(system, station, geometry,
                                                  {station.id, 1}, 5000));
  OriginWalkerState entry;
  for (unsigned i = 0; i < 2960; ++i)
    entry = need(advance_origin_walker(entry, {0, -1, 0}));
  const auto binding = need(make_native_starting_assembly_binding({}));
  const RigidVector3 eye{entry.foot_position_metres.x,
                         entry.foot_position_metres.y +
                             kOriginWalkerEyeHeightMetres,
                         entry.foot_position_metres.z};
  const auto route = need(step_origin_gameplay_boarding(
      need(begin_origin_gameplay_boarding(binding, port, eye,
                                          entry.heading_radians,
                                          GameplayBoardingDirection::board)),
      kGameplayBoardingTicks));
  const FreedomBoardingState pilot{FreedomBoardingPhase::seated, entry,
                                   5000 - kGameplayBoardingTicks, route};
  origin.state.tick = 5000;
  FreedomFlightSaveDocument flight;
  flight.origin = origin;
  flight.model.physical_catalog = 2;
  flight.model.physical_ephemeris = 2;
  flight.model.terrain_policy = 3;
  flight.model.frame_policy = 2;
  flight.model.optics = 1;
  flight.model.companions = 1;
  flight.world = FreedomActiveWorldSelection{2, system.catalog.id,
                                             origin.recipe.home_planet};
  flight.flight = port.planet_relative;
  flight.flight.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  const auto home =
      need(find_local_system_planet(system, origin.recipe.home_planet));
  flight.flight.position_metres = {
      0, 0, static_cast<double>(home->descriptor.radius.value) * 10000};
  flight.flight.orientation = {};
  flight.flight.linear_velocity_metres_per_second = {};
  flight.flight.angular_velocity_radians_per_second = {};
  flight.flight = need(canonicalize_rigid_body_state(
      RigidBodyWorldContext{system}, flight.flight));
  const FreedomSurfaceSaveDocument surface{flight, {}};
  const FreedomResources resources{1,
                                   origin.state.craft,
                                   flight.flight.craft,
                                   5000,
                                   kFreedomFlightCapacityQuanta - 123,
                                   3};
  const FreedomResourceSaveDocument fueled{surface, resources};
  FreedomKnowledgeSaveDocument mapped{
      fueled, need(make_freedom_starting_knowledge(
                  seed, kFreedomPlanetaryKnowledgeVersion))};
  FreedomTravelSaveDocument result{
      mapped, {}, NativeStartingAssemblySelection{}, pilot};
  result.travel.version = kFreedomComposedTravelStateVersion;
  result.travel.composed.emplace();
  return result;
}
auto composed_recovery() -> void {
  const auto legacy_fingerprint = fingerprint;
  auto travel = composed_fixture();
  auto& flight =
      std::get<FreedomFlightSaveDocument>(travel.voyage.voyage.voyage.base);
  const auto owner = need(hydrate_freedom_flight_document(flight)).system;
  const auto& planet = owner.catalog.planets.front().descriptor;
  auto& knowledge = travel.voyage.knowledge;
  const auto shell_x = static_cast<std::int32_t>(
      std::floor(static_cast<double>(planet.radius.value) * 1000 /
                 kSurfaceLandmarkCellMetres));
  // Identity-only persistence metadata, not a claimed walk, inspection or
  // collection.
  std::optional<FreedomFieldSampleRecord> note;
  for (std::int32_t x = -1; x <= 1 && !note; ++x)
    for (std::int32_t y = -2; y <= 2 && !note; ++y)
      for (std::int32_t z = -2; z <= 2 && !note; ++z) {
        const SurfaceLandmarkCell cell{shell_x + x, y, z};
        const auto identity = need(identify_surface_landmark(planet, cell));
        if (!identity) continue;
        knowledge.surface_landmarks = FreedomSurfaceLandmarkState{
            1, {{owner.catalog.id, planet.id, cell, identity->id, 3, 7}}};
        note = FreedomFieldSampleRecord{{owner.catalog.id, body_id(planet.id)},
                                        cell,
                                        identity->id,
                                        3,
                                        identity->kind,
                                        flight.origin.state.craft,
                                        20};
      }
  if (!note) throw std::runtime_error("Composed sample metadata unavailable");
  knowledge.field_samples =
      FreedomFieldSampleState{1, flight.origin.state.craft, {*note}, {*note}};
  auto& state = travel.travel;
  auto& composed = *state.composed;
  composed.active = FreedomComposedRouteKind::planet;
  for (const auto& destination : owner.catalog.planets)
    if (destination.descriptor.id != flight.world->planet) {
      composed.selected_body =
          BodyTarget{owner.catalog.id, body_id(destination.descriptor.id)};
      break;
    }
  if (!composed.selected_body)
    throw std::runtime_error("Composed destination unavailable");
  state.phase = FreedomJumpPhase::spool;
  state.spool_tick = flight.flight.tick;
  flight.flight.tick += kJumpSpoolTicks;
  flight.origin.state.tick = flight.flight.tick;
  travel.voyage.voyage.resources.tick = flight.flight.tick;
  composed.planet_committed = need(freeze_freedom_planet_jump_arrival(
      {1, flight, *composed.selected_body, state.next_attempt}));
  composed.planet_commitment_resources = travel.voyage.voyage.resources;
  --travel.voyage.voyage.resources.jump_charges;
  ++state.next_attempt;
  // Frozen provider arrival is codec metadata, not a claim of actual native
  // travel.
  travel.voyage.voyage.voyage.base = composed.planet_committed->arrival;
  travel.voyage.voyage.resources.tick =
      composed.planet_committed->arrival.flight.tick;
  state.phase = FreedomJumpPhase::idle;
  state.spool_tick = 0;
  composed.selected_body.reset();
  const auto original = need(encode_freedom_travel_document_json(travel));
  const FreedomRecoverySaveDocument source{travel, {}};
  const auto plain = need(encode_freedom_recovery_document_json(source));
  check(need(decode_freedom_recovery_document_json(plain)) == source &&
            Json::parse(plain)["voyage"] == Json::parse(original),
        "Recovery29 nests full composed56 receipt, billed resources and chart5 "
        "without flattening");
  const auto current = recovery_flight(source.voyage);
  const auto pending = need(record_freedom_loss(
      source, FreedomLossCause::irrecoverable_destruction,
      current.origin.state.craft, current.flight.tick,
      need(freedom_recovery_source_checksum(source.voyage))));
  const auto pending_text =
      need(encode_freedom_recovery_document_json(pending));
  const auto resumed =
      need(decode_freedom_recovery_document_json(pending_text));
  check(resumed == pending && resumed.recovery.latest->retired == source.voyage,
        "Pending composed loss retains the exact playable and retired full "
        "voyage");
  const auto replacement = need(complete_freedom_recovery(resumed));
  const auto& retained =
      std::get<FreedomTravelSaveDocument>(replacement.recovery.latest->retired);
  const auto& replacement_knowledge =
      recovery_knowledge(replacement.voyage).knowledge;
  const auto replaced_flight = recovery_flight(replacement.voyage);
  check(retained == travel &&
            need(encode_freedom_travel_document_json(retained)) == original &&
            replacement_knowledge.recipe.version ==
                kFreedomPlanetaryKnowledgeVersion &&
            replacement_knowledge.recipe.chart ==
                kFreedomPlanetaryStartingChartVersion &&
            replacement_knowledge.recipe.craft_lineage ==
                replaced_flight.origin.lineage &&
            replaced_flight.origin.lineage &&
            replaced_flight.origin.lineage->generation == 1 &&
            replacement_knowledge.entries == knowledge.entries &&
            replacement_knowledge.surface_landmarks ==
                knowledge.surface_landmarks &&
            replaced_flight.origin.network == current.origin.network &&
            replaced_flight.world->version == 2,
        "Replacement preserves chart5 grants and authentic full retired "
        "receipts while rebinding only the new craft");
  check(replacement_knowledge.field_samples &&
            replacement_knowledge.field_samples->carrier ==
                replaced_flight.origin.state.craft &&
            replacement_knowledge.field_samples->carried.empty() &&
            replacement_knowledge.field_samples->journal ==
                std::vector{*note} &&
            retained.voyage.knowledge.field_samples == knowledge.field_samples,
        "Composed replacement clears only the physical pouch and preserves "
        "exact first notes and retired specimens");
  const auto text = need(encode_freedom_recovery_document_json(replacement));
  check(need(decode_freedom_recovery_document_json(text)) == replacement &&
            need(complete_freedom_recovery(resumed)) == replacement,
        "Completed composed replacement roundtrips and resumed loss produces "
        "the same replacement");
  for (unsigned fault = 0; fault < 5; ++fault) {
    auto bad = Json::parse(text);
    auto& retired = bad["recovery"]["latest"]["retired"];
    switch (fault) {
      case 0: retired["format_version"] = 57U; break;
      case 1:
        retired["format_version"] = kFreedomNetworkTravelSaveFormatVersion;
        break;
      case 2:
        retired["travel"]["composed"]["planet_committed"]["source_checksum"] =
            0U;
        break;
      case 3:
        bad["voyage"]["knowledge"]["field_samples"]["journal"][0]["tick"] = 21U;
        break;
      case 4:
        bad["voyage"]["knowledge"]["field_samples"]["carrier"] =
            std::to_string(current.origin.state.craft.value);
        break;
      default: break;
    }
    check(!decode_freedom_recovery_document_json(bad.dump()),
          "Composed recovery refuses unknown/mismatched nested policies, "
          "forged receipts, rewritten notes and stale carrier");
  }
  check(need(encode_freedom_travel_document_json(travel)) == original &&
            fingerprint == legacy_fingerprint,
        "Composed wrapper checks leave source bytes and historical-only "
        "fingerprint unchanged");
}
auto network_recovery(const std::filesystem::path& path) -> void {
  auto live = need(
      NativeFreedomFlightSession::open(need(native_network_new_game({42}))));
  const auto owner = live.document().origin;
  const auto original = need(live.recovery_document());
  need(live.record_loss(
      FreedomLossCause::irrecoverable_destruction, owner.state.craft,
      owner.state.tick,
      need(freedom_recovery_source_checksum(original.voyage))));
  need(live.save_as(path));
  auto copy =
      need(NativeFreedomFlightSession::open(need(native_continue(path))));
  need(live.complete_recovery());
  need(copy.complete_recovery());
  check(live.document().origin.network == owner.network &&
            live.document().world->version == 2 &&
            live.knowledge()->recipe.version == 4 && live.docking()->attached &&
            live.walker() &&
            need(copy.recovery_document()) == need(live.recovery_document()),
        "Network recovery retains the selected map across frozen loss and "
        "replacement Continue");
  auto tampered = need(live.recovery_document());
  auto& voyage = std::get<FreedomKnowledgeSaveDocument>(tampered.voyage);
  auto& assembly =
      std::get<FreedomStartingAssemblySaveDocument>(voyage.voyage.voyage.base);
  assembly.journey.voyage.flight.origin.network.reset();
  check(!validate_freedom_recovery_document(tampered),
        "Recovery cannot silently replace the selected network with a pair");
}
auto star_search_recovery() -> void {
  const Seed seed{42};
  auto selected = need(native_uncharted_network_new_game(seed));
  FreedomRecoverySaveDocument source{
      std::get<FreedomKnowledgeSaveDocument>(std::move(selected.document)), {}};
  const auto craft = recovery_flight(source.voyage).origin.state.craft;
  const auto system = recovery_flight(source.voyage).flight.frame.system;
  // Metadata-only saved-station snapshots, not a flown search acquisition.
  // Resolve the real station constraint at each snapshot's new clock.
  const auto snapshot_tick = [&](auto& document, SimulationTick tick) {
    auto& knowledge = std::get<FreedomKnowledgeSaveDocument>(document.voyage);
    auto& assembly = std::get<FreedomStartingAssemblySaveDocument>(
        knowledge.voyage.voyage.base);
    auto& flight = assembly.journey.voyage.flight;
    const auto owner = need(generate_physical_origin_system(seed, 2));
    const auto station = generate_origin_station(seed);
    const auto geometry = need(origin_station_geometry(station));
    flight.flight = need(release_origin_port(
        owner, station, geometry,
        {geometry.version, assembly.journey.voyage.docking.target,
         flight.flight.craft, tick}));
    flight.origin.state.tick = tick;
    knowledge.voyage.resources.tick = tick;
  };
  const auto finish_search = [&](FreedomKnowledge knowledge,
                                 StarterCraftId collector,
                                 SimulationTick started) {
    auto searching =
        need(begin_freedom_star_search(knowledge, system, collector, started));
    auto& active = *searching.star_search->active;
    active.last_tick = started + kFreedomStarSearchTicks;
    active.acquired_ticks = kFreedomStarSearchTicks - 1;
    return need(advance_freedom_star_search(
        searching, system, collector, started + kFreedomStarSearchTicks + 1,
        true));
  };
  auto& knowledge =
      std::get<FreedomKnowledgeSaveDocument>(source.voyage).knowledge;
  knowledge = finish_search(knowledge, craft, 0);
  const SimulationTick loss_tick = kFreedomStarSearchTicks + 2;
  knowledge =
      need(begin_freedom_star_search(knowledge, system, craft, loss_tick));
  snapshot_tick(source, loss_tick);
  need(validate_freedom_recovery_document(source));
  const auto pending = need(record_freedom_loss(
      source, FreedomLossCause::irrecoverable_destruction, craft, loss_tick,
      need(freedom_recovery_source_checksum(source.voyage))));
  const auto completed = need(complete_freedom_recovery(pending));
  const auto& previous = recovery_knowledge(source.voyage).knowledge;
  const auto& replacement = recovery_knowledge(completed.voyage).knowledge;
  const auto current = recovery_flight(completed.voyage).origin.state.craft;
  check(recovery_flight(completed.voyage).origin.network ==
                recovery_flight(source.voyage).origin.network &&
            replacement.recipe.version == kFreedomUnchartedKnowledgeVersion &&
            replacement.entries == previous.entries &&
            replacement.star_search && !replacement.star_search->active &&
            replacement.star_search->completed ==
                previous.star_search->completed &&
            recovery_knowledge(completed.recovery.latest->retired).knowledge ==
                previous &&
            need(decode_freedom_recovery_document_json(
                need(encode_freedom_recovery_document_json(completed)))) ==
                completed,
        "Replacement retains exact searched-region records and network2, "
        "clears only the active search, and roundtrips retired provenance");
  const auto rejects_valid_knowledge = [&](const auto& candidate,
                                           std::string_view message) {
    need(validate_freedom_knowledge_document(
        std::get<FreedomKnowledgeSaveDocument>(candidate.voyage)));
    check(!validate_freedom_recovery_document(candidate), message);
  };
  auto rewritten = completed;
  auto& rewritten_search =
      std::get<FreedomKnowledgeSaveDocument>(rewritten.voyage)
          .knowledge.star_search;
  ++rewritten_search->completed.front().started_tick;
  rejects_valid_knowledge(
      rewritten,
      "Individually valid search metadata cannot rewrite the retained prefix");
  rewritten_search->completed = previous.star_search->completed;
  rewritten_search->active = previous.star_search->active;
  check(!validate_freedom_knowledge(
            std::get<FreedomKnowledgeSaveDocument>(rewritten.voyage).knowledge,
            loss_tick) &&
            !validate_freedom_recovery_document(rewritten),
        "The retired craft's active search fails both current-craft knowledge "
        "and replacement recovery admission");
  auto onward = completed;
  auto& next = std::get<FreedomKnowledgeSaveDocument>(onward.voyage).knowledge;
  next = need(begin_freedom_star_search(next, system, current, loss_tick));
  need(validate_freedom_recovery_document(onward));
  --next.star_search->active->started_tick;
  rejects_valid_knowledge(onward, "A new craft's active search cannot be "
                                  "backdated before its loss boundary");
  next.star_search->active.reset();
  next = finish_search(next, current, loss_tick);
  snapshot_tick(onward, loss_tick + kFreedomStarSearchTicks + 1);
  check(
      need(decode_freedom_recovery_document_json(
          need(encode_freedom_recovery_document_json(onward)))) == onward &&
          next.star_search->completed.size() == 2 &&
          next.star_search->completed.back().craft == current,
      "The replacement may append and Continue its own newly completed search");
  --next.star_search->completed.back().started_tick;
  rejects_valid_knowledge(onward, "An otherwise valid added search cannot "
                                  "begin before the replacement loss");
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
    for (const auto policy :
         {kFreedomLegacyTerrainPolicy, kFreedomLocalDetailTerrainPolicy})
      provider(Seed{42}, FreedomLossCause::recoverable_destruction, false, 0,
               policy);
    network_recovery(path / "network-recovery.json");
    star_search_recovery();
    session_contract(path / "recovery.json");
    landmark_retention();
    composed_recovery();
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
