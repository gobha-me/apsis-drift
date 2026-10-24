#include "apsis_drift/native_flight_session.hpp"
#include "derelict_recovery_test_support.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {
using namespace apsis_drift;
using test_support::derelict_recovery_detail::need;
unsigned checks{};
auto check(bool ok, std::string_view message) -> void {
  ++checks;
  if (!ok) throw std::runtime_error(std::string{message});
}
auto bytes(const NativeFreedomFlightSession& session) -> std::string {
  auto value =
      std::make_unique<NativeSaveDocument>(need(session.save_document()));
  return need(encode_native_save_document_json(*value));
}
auto knowledge(FreedomRecoveryVoyage& voyage) -> FreedomKnowledge& {
  return std::visit(
      [](auto& value) -> FreedomKnowledge& {
        if constexpr (std::is_same_v<std::decay_t<decltype(value)>,
                                     FreedomKnowledgeSaveDocument>)
          return value.knowledge;
        else
          return value.voyage.knowledge;
      },
      voyage);
}
auto resumed(const NativeFreedomFlightSession& source,
             const std::filesystem::path& path)
    -> std::unique_ptr<NativeFreedomFlightSession> {
  const auto original = bytes(source);
  need(source.save_as(path));
  auto result = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(need(native_continue(path)))));
  check(bytes(*result) == original && bytes(source) == original,
        "Actual Save/Continue changed source or complete recovery ownership");
  return result;
}
auto checksum(const NativeFreedomFlightSession& source) -> std::uint64_t {
  return need(
      rigid_body_state_checksum({source.system()}, source.document().flight));
}
auto record(NativeFreedomFlightSession& source) -> void {
  need(source.record_loss(FreedomLossCause::recoverable_destruction,
                          source.document().origin.state.craft,
                          source.document().flight.tick, checksum(source)));
}
auto unfinished(const NativeFreedomFlightSession& source,
                const std::filesystem::path& directory) -> void {
  auto live = std::make_unique<NativeFreedomFlightSession>(source);
  need(live->begin_derelict_recovery());
  for (unsigned tick = 0; tick < 12; ++tick)
    need(live->advance({}));
  check(live->knowledge()->derelict_recovery->active &&
            live->knowledge()->derelict_recovery->active->acquired_ticks == 12,
        "Actual partial retrieval did not advance through committed physics");
  live = resumed(*live, directory / "unfinished.json");
  const auto before = bytes(*live);
  const auto craft = live->document().origin.state.craft;
  const auto tick = live->document().flight.tick;
  const auto physical_checksum = checksum(*live);
  check(!live->record_loss(FreedomLossCause::recoverable_destruction, {}, tick,
                           physical_checksum) &&
            !live->record_loss(FreedomLossCause::recoverable_destruction, craft,
                               tick + 1, physical_checksum) &&
            !live->record_loss(FreedomLossCause::recoverable_destruction, craft,
                               tick, physical_checksum ^ 1U) &&
            !live->record_loss(static_cast<FreedomLossCause>(255), craft, tick,
                               physical_checksum) &&
            bytes(*live) == before &&
            live->knowledge()->derelict_recovery->active->acquired_ticks == 12,
        "Refused loss cleared retrieval or changed complete voyage before "
        "event admission");
  auto unsanitized = std::make_unique<FreedomRecoverySaveDocument>(
      need(live->recovery_document()));
  check(!record_freedom_loss(*unsanitized,
                             FreedomLossCause::recoverable_destruction, craft,
                             tick, physical_checksum),
        "Pure loss snapshot accepted unfinished retrieval without its native "
        "pre-freeze cancellation");
  record(*live);
  auto pending = std::make_unique<FreedomRecoverySaveDocument>(
      need(live->recovery_document()));
  const auto explanation_before = bytes(*live);
  check(need(freedom_recovery_explanation(*pending))
                    .find("Your universe, discoveries and world changes are "
                          "retained. No fee or wreck retrieval is required.") !=
                std::string::npos &&
            bytes(*live) == explanation_before,
        "Unpaid explanation changed the legacy fee sentence or mutated the "
        "frozen voyage");
  check(live->recovery_pending() &&
            !live->knowledge()->derelict_recovery->active &&
            !live->knowledge()->derelict_recovery->canister &&
            !recovery_knowledge(pending->recovery.latest->retired)
                 .knowledge.derelict_recovery->active &&
            pending->recovery.latest->source_checksum == physical_checksum &&
            recovery_flight(pending->recovery.latest->retired).flight ==
                live->document().flight,
        "Accepted loss froze active work, changed physics checksum or claimed "
        "an unfinished item");
  const auto frozen = bytes(*live);
  need(live->record_loss(FreedomLossCause::recoverable_destruction, craft, tick,
                         physical_checksum));
  check(
      bytes(*live) == frozen && !live->advance({}) &&
          !live->begin_derelict_recovery() && bytes(*live) == frozen,
      "Pending replay/ordinary tick changed frozen retired retrieval history");
  live = resumed(*live, directory / "pending.json");
  need(live->complete_recovery());
  check(!live->recovery_pending() && live->walker() &&
            live->document().origin.state.craft != craft &&
            live->knowledge()->derelict_recovery &&
            !live->knowledge()->derelict_recovery->active &&
            !live->knowledge()->derelict_recovery->canister &&
            live->resources()->flight_quanta == kFreedomFlightCapacityQuanta &&
            live->resources()->jump_charges == 3 &&
            !need(live->derelict_recovery_view()).can_begin,
        "Replacement resumed partial retrieval or fabricated remote cargo "
        "instead of safe station ownership");
  auto replacement = std::make_unique<FreedomRecoverySaveDocument>(
      need(live->recovery_document()));
  check(replacement->recovery.latest->retired ==
            pending->recovery.latest->retired,
        "Replacement rewrote the canceled retired snapshot");
  const auto replaced = bytes(*live);
  need(live->complete_recovery());
  need(live->record_loss(FreedomLossCause::recoverable_destruction, craft, tick,
                         physical_checksum));
  check(bytes(*live) == replaced,
        "Repeated completion/old loss granted another craft or reset history");
  auto continued = resumed(*live, directory / "replacement.json");
  auto erased = std::make_unique<FreedomRecoverySaveDocument>(
      need(continued->recovery_document()));
  knowledge(erased->voyage).derelict_recovery.reset();
  check(!validate_freedom_recovery_document(*erased),
        "Replacement erased its explicitly selected recovery policy");
  // An explicitly initialized enrollment after this unpaid retired voyage must
  // not retrospectively change the explanation or invent a past recovery fee.
  auto enrolled_later = std::make_unique<FreedomRecoverySaveDocument>(
      need(continued->recovery_document()));
  knowledge(enrolled_later->voyage).paid_operations = need(make_paid_operations(
      PaidIncomeCounts{}, continued->document().flight.tick, 1));
  const auto unpaid_bytes =
      need(encode_freedom_recovery_document_json(*enrolled_later));
  check(need(freedom_recovery_explanation(*enrolled_later))
                    .find("No fee or wreck retrieval is required.") !=
                std::string::npos &&
            need(encode_freedom_recovery_document_json(*enrolled_later)) ==
                unpaid_bytes,
        "Post-loss enrollment retroactively promised a fee or changed the "
        "recovery document");
}
auto carried_and_sold(const NativeFreedomFlightSession& source,
                      const std::filesystem::path& directory) -> void {
  auto live = std::make_unique<NativeFreedomFlightSession>(source);
  need(live->begin_derelict_recovery());
  for (std::uint32_t tick = 0; tick < kFreedomDerelictRetrievalTicks; ++tick)
    need(live->advance({}));
  check(live->knowledge()->derelict_recovery->canister &&
            live->knowledge()->derelict_recovery->canister->disposition ==
                FreedomCanisterDisposition::carried &&
            !live->knowledge()->derelict_recovery->active,
        "Actual matched-speed retrieval failed to claim one physical canister");
  live = resumed(*live, directory / "carried.json");
  const auto claim = *live->knowledge()->derelict_recovery->canister;
  const auto notes = *live->knowledge()->derelicts;
  auto lost = std::make_unique<NativeFreedomFlightSession>(*live);
  record(*lost);
  auto pending = std::make_unique<FreedomRecoverySaveDocument>(
      need(lost->recovery_document()));
  check(recovery_knowledge(pending->recovery.latest->retired)
                .knowledge.derelict_recovery->canister == claim,
        "Pending loss edited the retired carried claim before replacement");
  need(lost->complete_recovery());
  const auto lost_claim = *lost->knowledge()->derelict_recovery->canister;
  check(lost_claim.disposition == FreedomCanisterDisposition::lost &&
            lost_claim.completed == claim.completed &&
            lost_claim.started_tick == claim.started_tick &&
            lost->knowledge()->derelicts == notes &&
            !need(lost->derelict_recovery_sale_view()).can_sell &&
            !lost->knowledge()->paid_operations,
        "Replacement erased finite depletion/notes, carried an old item or "
        "enrolled a free wallet");
  auto replacement = std::make_unique<FreedomRecoverySaveDocument>(
      need(lost->recovery_document()));
  auto forged = std::make_unique<FreedomRecoverySaveDocument>(*replacement);
  knowledge(forged->voyage).derelict_recovery->canister.reset();
  check(!validate_freedom_recovery_document(*forged),
        "Replacement respawned a claimed source by erasing its record");
  forged = std::make_unique<FreedomRecoverySaveDocument>(*replacement);
  knowledge(forged->voyage).derelict_recovery->canister->disposition =
      FreedomCanisterDisposition::carried;
  check(!validate_freedom_recovery_document(*forged),
        "Replacement recovered cargo from its retired craft");
  forged = std::make_unique<FreedomRecoverySaveDocument>(*replacement);
  auto& fake_sale = *knowledge(forged->voyage).derelict_recovery->canister;
  fake_sale.disposition = FreedomCanisterDisposition::sold;
  fake_sale.sold_tick = lost->document().flight.tick;
  check(!validate_freedom_recovery_document(*forged),
        "Replacement sold the retired lost cargo from its note");
  lost = resumed(*lost, directory / "lost.json");
  record(*lost);
  need(lost->complete_recovery());
  check(
      *lost->knowledge()->derelict_recovery->canister == lost_claim,
      "A later actual replacement changed already-lost finite source history");

  // Explicit typed sale/financial boundary, not an actual Origin sale or flown
  // return. The completed claim/inspection above are real; only this receipt
  // and optional enrollment are initialized for recovery-retention coverage.
  auto sale_boundary = std::make_unique<FreedomRecoverySaveDocument>(
      need(live->recovery_document()));
  auto& sale_knowledge = knowledge(sale_boundary->voyage);
  sale_knowledge.derelict_recovery = need(sell_freedom_derelict_canister(
      {42}, *sale_knowledge.derelict_recovery, *sale_knowledge.derelicts,
      live->document().flight.tick));
  sale_knowledge.paid_operations = need(make_paid_operations(
      PaidIncomeCounts{0, 0, 1}, live->document().flight.tick, 0));
  auto sold = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_select_save_document(std::move(*sale_boundary))))));
  const auto sold_receipt = *sold->knowledge()->derelict_recovery->canister;
  check(need(sold->paid_operations_view()).cash == 280,
        "Typed prior-sale enrollment budget was not280");
  record(*sold);
  auto paid_loss = std::make_unique<FreedomRecoverySaveDocument>(
      need(sold->recovery_document()));
  const auto paid_bytes = bytes(*sold);
  const auto paid_explanation = need(freedom_recovery_explanation(*paid_loss));
  check(paid_explanation.find("paid recovery excess is 100 credits") !=
                std::string::npos &&
            paid_explanation.find("cash first") != std::string::npos &&
            paid_explanation.find(
                "unpaid remainder is withheld from later income") !=
                std::string::npos &&
            paid_explanation.find("without upfront cash") !=
                std::string::npos &&
            paid_explanation.find("No wreck retrieval is required.") !=
                std::string::npos &&
            paid_explanation.find("No fee") == std::string::npos &&
            bytes(*sold) == paid_bytes,
        "Paid explanation omitted the excess/deferred payment/no-upfront-cash "
        "terms or changed the frozen owner");
  need(sold->complete_recovery());
  check(*sold->knowledge()->derelict_recovery->canister == sold_receipt &&
            sold->knowledge()->paid_operations->version == 3 &&
            sold->knowledge()->paid_operations->settled_canister_count == 1 &&
            need(sold->paid_operations_view()).cash == 180 &&
            sold->knowledge()->paid_operations->recovery_due == 0,
        "Sold-history replacement forgot100 gross, charged twice or granted a "
        "second opening budget");
  auto paid = resumed(*sold, directory / "sold-replaced.json");
  const auto once = bytes(*paid);
  need(paid->complete_recovery());
  check(bytes(*paid) == once &&
            *paid->knowledge()->derelict_recovery->canister == sold_receipt,
        "Repeated paid replacement regenerated a canister or changed completed "
        "earnings");
}
auto postloss_claim_boundary(const NativeFreedomFlightSession& source) -> void {
  // Explicit typed enrollment and later completed-claim admission fixture.
  // Actual loss/replacement and subsequent neutral station ticks are real;
  // this does not claim retrieval while at the station or a flown return.
  auto selected = std::make_unique<FreedomRecoverySaveDocument>(
      need(source.recovery_document()));
  knowledge(selected->voyage).paid_operations = need(make_paid_operations(
      PaidIncomeCounts{}, source.document().flight.tick, 0));
  auto live = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_select_save_document(std::move(*selected))))));
  record(*live);
  const auto loss_tick = live->document().flight.tick;
  need(live->complete_recovery());
  check(need(live->paid_operations_view()).cash == 80,
        "Actual selected replacement did not charge100 from180 once");
  for (std::uint32_t tick = 0; tick <= kFreedomDerelictRetrievalTicks; ++tick)
    need(live->advance_walk({}));
  auto later = std::make_unique<FreedomRecoverySaveDocument>(
      need(live->recovery_document()));
  auto& learned = knowledge(later->voyage);
  auto descriptor = std::make_unique<FreedomDerelictDescriptor>(
      need(generate_freedom_derelict({42})));
  const auto hull =
      need(resolve_freedom_derelict(*descriptor, live->document().flight.tick));
  const FreedomDerelictObservation completed{
      live->document().origin.state.craft,
      live->document().flight.tick,
      {hull.position_metres.x + 120, hull.position_metres.y,
       hull.position_metres.z},
      hull.linear_velocity_metres_per_second};
  learned.derelict_recovery->canister =
      FreedomRecoveredCanister{loss_tick + 1, completed};
  check(validate_freedom_recovery_document(*later).has_value(),
        "Unclaimed retired source rejected a valid post-loss current-craft "
        "claim");
  auto bad = std::make_unique<FreedomRecoverySaveDocument>(*later);
  auto& forged = *knowledge(bad->voyage).derelict_recovery->canister;
  forged.started_tick = loss_tick - 1;
  forged.completed.tick = forged.started_tick + kFreedomDerelictRetrievalTicks;
  const auto earlier_hull =
      need(resolve_freedom_derelict(*descriptor, forged.completed.tick));
  forged.completed.position_metres = {earlier_hull.position_metres.x + 120,
                                      earlier_hull.position_metres.y,
                                      earlier_hull.position_metres.z};
  forged.completed.velocity_metres_per_second =
      earlier_hull.linear_velocity_metres_per_second;
  check(!validate_freedom_recovery_document(*bad),
        "Otherwise authentic claim from before the loss boundary was admitted");
  bad = std::make_unique<FreedomRecoverySaveDocument>(*later);
  knowledge(bad->voyage).derelict_recovery->canister->completed.craft =
      source.document().origin.state.craft;
  check(!validate_freedom_recovery_document(*bad),
        "Replacement claimed a new item using its retired craft's observation");
  learned.derelict_recovery = need(sell_freedom_derelict_canister(
      {42}, *learned.derelict_recovery, *learned.derelicts,
      live->document().flight.tick));
  learned.paid_operations = need(settle_paid_canister_income(
      *learned.paid_operations, {0, 0, 1}, live->document().flight.tick, 1));
  check(validate_freedom_recovery_document(*later).has_value() &&
            learned.paid_operations->version == 3 &&
            need(paid_operations_cash(*learned.paid_operations,
                                      PaidIncomeCounts{0, 0, 1})) == 180,
        "A legitimate post-loss sale could not promote the retained version1 "
        "budget to3 without a new grant");
}
} // namespace
auto main() -> int {
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("apsis-derelict-recovery-loss-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    std::filesystem::create_directories(directory);
    auto fixture = test_support::make_derelict_recovery_fixture();
    unfinished(*fixture, directory);
    carried_and_sold(*fixture, directory);
    postloss_claim_boundary(*fixture);
    std::filesystem::remove_all(directory);
    std::cout << "Derelict recovery loss: " << checks
              << " checks, 0 failures\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Derelict recovery loss after " << checks
              << " checks: " << error.what()
              << "; retained saves: " << directory << '\n';
    return 1;
  }
}
