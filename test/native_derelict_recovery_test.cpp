#include "derelict_recovery_test_support.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <string_view>

namespace {
using namespace apsis_drift;
using test_support::derelict_recovery_detail::need;
using Json = nlohmann::ordered_json;
using Session = NativeFreedomFlightSession;
unsigned checks{};
constexpr std::array<std::string_view, 17> phase_names{"station-selected",
                                                       "ready",
                                                       "started",
                                                       "half",
                                                       "near",
                                                       "complete",
                                                       "cancelled",
                                                       "interrupted",
                                                       "origin-carried",
                                                       "origin-paid",
                                                       "sold",
                                                       "paid-sold",
                                                       "prior-sale-enrolled",
                                                       "lost-cargo",
                                                       "unfinished-loss",
                                                       "origin-debt",
                                                       "debt-sold"};
auto check(bool ok, std::string_view message) -> void {
  ++checks;
  if (!ok) throw std::runtime_error(std::string{message});
}
auto bytes(const Session& session) -> std::string {
  auto document =
      std::make_unique<NativeSaveDocument>(need(session.save_document()));
  return need(encode_native_save_document_json(*document));
}
auto read(const std::filesystem::path& path) -> std::string {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("Cannot read canonical phase Save");
  return {std::istreambuf_iterator<char>{stream},
          std::istreambuf_iterator<char>{}};
}
auto write_manifest(const std::filesystem::path& output, const Json& manifest)
    -> void {
  std::ofstream stream(output / "manifest.json", std::ios::binary);
  stream << manifest.dump(2) << '\n';
  if (!stream)
    throw std::runtime_error("Cannot write recovery scenario manifest");
}
auto phase(const Session& session, const std::filesystem::path& output,
           std::string_view name, std::string_view source,
           std::string_view action, std::string_view scope, Json& manifest)
    -> void {
  check(manifest["phases"].size() < phase_names.size() &&
            name == phase_names[manifest["phases"].size()],
        "Scenario phase order changed");
  const auto path = output / (std::string{name} + ".json");
  const auto before = bytes(session);
  const auto progress = need(session.derelict_recovery_view());
  const auto sale = need(session.derelict_recovery_sale_view());
  const auto account = session.knowledge()->paid_operations;
  const auto cash = account ? need(session.paid_operations_view()).cash : 0;
  need(session.save_as(path));
  auto continued = std::make_unique<Session>(
      need(Session::open(need(native_continue(path)))));
  check(read(path) == before && bytes(*continued) == before &&
            bytes(session) == before,
        "Phase Save/Continue changed source or complete actor/craft/history "
        "bytes");
  const auto continued_view = need(continued->derelict_recovery_view());
  check(continued_view.status == progress.status &&
            continued_view.acquired_ticks == progress.acquired_ticks &&
            continued_view.tick == progress.tick,
        "Continue changed actual retrieval or cargo status");
  manifest["phases"].push_back(
      {{"file", path.filename().string()},
       {"tick", std::to_string(session.document().flight.tick)},
       {"status", progress.status},
       {"acquired_ticks", progress.acquired_ticks},
       {"sold_count", sale.earned_credits ? 1U : 0U},
       {"paid_version", account ? account->version : 0U},
       {"cash", std::to_string(cash)},
       {"earned_credits", std::to_string(sale.earned_credits)},
       {"source", source},
       {"action", action},
       {"scope", scope}});
  manifest["checks"] = checks;
  write_manifest(output, manifest);
}
auto origin_boundary(const Session& collected) -> std::unique_ptr<Session> {
  // Explicit same-tick Origin attachment, not a flown return. Copy actual
  // resources, knowledge, completed entry route and origin history. This
  // constructed knowledge/boarding save deliberately drops travel commitments;
  // the remote source and all of its real travel receipts remain untouched.
  auto startup = std::make_unique<NativeStartup>(
      need(native_derelict_recovery_new_game({42}, 0, false)));
  auto& document = std::get<FreedomKnowledgeSaveDocument>(startup->document);
  auto& assembly = std::get<FreedomStartingAssemblySaveDocument>(
      document.voyage.voyage.base);
  auto& flight = assembly.journey.voyage.flight;
  flight.origin = collected.document().origin;
  flight.model = collected.document().model;
  auto owner = std::make_unique<PhysicalLocalSystem>(
      need(generate_physical_origin_system({42}, 2)));
  const auto station = generate_origin_station({42});
  const auto geometry = need(origin_station_geometry(station));
  const auto tick = collected.document().flight.tick;
  flight.flight = need(release_origin_port(
      *owner, station, geometry,
      {geometry.version, assembly.journey.voyage.docking.target,
       flight.flight.craft, tick}));
  flight.origin.state.tick = tick;
  flight.world = FreedomActiveWorldSelection{2, owner->catalog.id,
                                             station.orbit.host_planet};
  document.voyage.resources = *collected.resources();
  document.knowledge = *collected.knowledge();
  check(collected.boarding() &&
            collected.boarding()->phase == FreedomBoardingPhase::seated &&
            collected.starting_assembly(),
        "Origin boundary lacks the actual completed boarding route");
  FreedomBoardingSaveDocument boarding{assembly.journey.voyage,
                                       *collected.starting_assembly(),
                                       *collected.boarding(),
                                       {}};
  document.voyage.voyage.base = std::move(boarding);
  auto result = std::make_unique<Session>(need(Session::open(
      need(native_select_save_document(std::move(startup->document))))));
  check(result->docking() && result->docking()->attached &&
            result->knowledge() == collected.knowledge() &&
            result->resources() == collected.resources() &&
            result->document().flight.tick == tick &&
            result->boarding() == collected.boarding() && !result->travel() &&
            need(result->derelict_recovery_sale_view()).at_origin,
        "Declared Origin boundary lost cargo/history/resources/time or "
        "retained inapplicable travel receipts");
  return result;
}
auto lose(Session& session) -> void {
  const auto checksum = need(
      rigid_body_state_checksum({session.system()}, session.document().flight));
  need(session.record_loss(FreedomLossCause::recoverable_destruction,
                           session.document().origin.state.craft,
                           session.document().flight.tick, checksum));
  need(session.complete_recovery());
}
auto sell(Session& session, std::uint64_t insurance, std::uint64_t tab,
          std::uint64_t net) -> void {
  const auto quote = need(session.derelict_recovery_sale_view());
  check(quote.can_sell && quote.carried_count == 1 &&
            quote.gross_credits == 100 &&
            quote.insurance_credits == insurance && quote.tab_credits == tab &&
            quote.net_credits == net,
        "Reviewed actual canister quote differs from fixed independent "
        "accounting");
  const auto before = bytes(session);
  check(!session.sell_derelict_recovery(quote.token + "x") &&
            !session.sell_derelict_recovery("") && bytes(session) == before,
        "Stale/empty sale token changed whole voyage");
  need(session.sell_derelict_recovery(quote.token));
  const auto sold = bytes(session);
  check(
      !session.sell_derelict_recovery(quote.token) && bytes(session) == sold &&
          need(session.derelict_recovery_sale_view()).carried_count == 0 &&
          need(session.derelict_recovery_sale_view()).earned_credits == 100,
      "Repeat sale paid twice, retained physical cargo or lost earned receipt");
}
auto survey_after_paid_sale(const Session& sold,
                            const std::filesystem::path& output) -> void {
  // Independent branch of the real sale. Its existing same-tick Origin
  // attachment is an initialized transaction boundary, not a flown return.
  auto live = std::make_unique<Session>(sold);
  const auto source = bytes(sold);
  const auto tick = sold.document().flight.tick;
  const auto account = *sold.knowledge()->paid_operations;
  const auto cargo = sold.knowledge()->derelict_recovery;
  const auto notes = sold.knowledge()->derelicts;
  const auto stores = sold.resources();
  const auto retained_sale = [&] {
    const auto quote = need(live->derelict_recovery_sale_view());
    return live->document().flight.tick == tick &&
           live->resources() == stores &&
           live->knowledge()->paid_operations == account &&
           live->knowledge()->derelict_recovery == cargo &&
           live->knowledge()->derelicts == notes && account.version == 3 &&
           account.settled_canister_count == 1 &&
           need(live->paid_operations_view()).cash == 280 &&
           quote.carried_count == 0 && quote.earned_credits == 100 &&
           !quote.can_sell;
  };
  const auto offer = need(live->survey_commission_view());
  check(offer.at_origin && offer.can_accept && offer.offer_sequence == 1 &&
            offer.offer_until > tick && retained_sale() &&
            need(live->survey_commission_view()).can_accept &&
            bytes(*live) == source,
        "Paid canister sale lost its advertised commission or read purity");
  check(!live->accept_survey_commission(0, offer.offer_sequence) &&
            !live->accept_survey_commission(offer.offer_until, 0) &&
            bytes(*live) == source,
        "Stale commission offer after paid sale changed complete Save");
  need(live->accept_survey_commission(offer.offer_until, offer.offer_sequence));
  const auto& accepted = *live->knowledge()->survey_commissions;
  check(accepted.active && accepted.active->sequence == offer.offer_sequence &&
            accepted.active->accepted_tick == tick &&
            !accepted.active->measurement && accepted.paid_count == 0 &&
            !accepted.latest_paid && accepted.next_sequence == 2 &&
            retained_sale(),
        "Accepting survey after paid sale lost or paid canister income twice");
  const auto accepted_bytes = bytes(*live);
  check(!live->accept_survey_commission(offer.offer_until,
                                        offer.offer_sequence) &&
            !live->submit_survey_commission(offer.offer_sequence) &&
            bytes(*live) == accepted_bytes,
        "Duplicate acceptance or incomplete submission mutated paid sale");
  // Temporary continuation evidence is removed; the original17-phase exporter
  // roster and every exported source session remain unchanged.
  const auto path = output / "paid-sale-survey-continue.tmp.json";
  need(live->save_as(path));
  auto continued = std::make_unique<Session>(
      need(Session::open(need(native_continue(path)))));
  check(read(path) == accepted_bytes && bytes(*continued) == accepted_bytes &&
            bytes(*live) == accepted_bytes,
        "Accepted post-sale commission lost complete Save/Continue parity");
  check(std::filesystem::remove(path),
        "Could not remove temporary post-sale commission evidence");
  live.swap(continued);
  need(live->abandon_survey_commission(offer.offer_sequence));
  const auto& abandoned = *live->knowledge()->survey_commissions;
  check(!abandoned.active && abandoned.paid_count == 0 &&
            !abandoned.latest_paid && abandoned.next_sequence == 2 &&
            retained_sale(),
        "Abandoning survey erased or repeated settled canister income");
  const auto abandoned_bytes = bytes(*live);
  const auto sold_quote = need(live->derelict_recovery_sale_view());
  check(!live->abandon_survey_commission(offer.offer_sequence) &&
            !live->sell_derelict_recovery(sold_quote.token) &&
            bytes(*live) == abandoned_bytes,
        "Repeated abandonment or canister sale created wealth after survey");
  const auto next = need(live->survey_commission_view());
  check(next.at_origin && next.can_accept && next.offer_sequence == 2 &&
            next.offer_until == offer.offer_until,
        "Abandonment locked the paid canister owner out of fresh work");
  need(live->accept_survey_commission(next.offer_until, next.offer_sequence));
  check(live->knowledge()->survey_commissions->active &&
            live->knowledge()->survey_commissions->active->sequence == 2 &&
            !live->knowledge()->survey_commissions->active->measurement &&
            retained_sale(),
        "Fresh acceptance reset prior canister settlement or invented work");
  need(live->abandon_survey_commission(next.offer_sequence));
  check(!live->knowledge()->survey_commissions->active &&
            live->knowledge()->survey_commissions->paid_count == 0 &&
            !live->knowledge()->survey_commissions->latest_paid &&
            live->knowledge()->survey_commissions->next_sequence == 3 &&
            retained_sale() && bytes(sold) == source,
        "Repeated fresh commission lifecycle changed settled income or source");
}
auto scenario(const std::filesystem::path& output) -> void {
  Json manifest{
      {"schema", "apsis.derelict-recovery-scenario/1"},
      {"completed", false},
      {"checks", 0},
      {"interruption_ticks", 0},
      {"interruption_controls", {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
      {"phases", Json::array()},
      {"scope",
       "Actual opt-in starter, walking/boarding, stellar acquisition, charged "
       "travel, exterior inspection and consecutive retrieval. Home space, "
       "close approach and same-clock attached Origin sale states are "
       "explicitly initialized; financial debt is an aggregate fixture. Origin "
       "construction drops inapplicable travel commitments while preserving "
       "actual cargo, notes, resources and entry route. No uninterrupted flown "
       "approach/return, transfer animation, profit, balance, hardware or GPU "
       "claim."}};
  auto station = std::make_unique<Session>(need(
      Session::open(need(native_derelict_recovery_new_game({42}, 0, false)))));
  const auto untouched = bytes(*station);
  const auto unknown = need(station->derelict_recovery_view());
  check(unknown.selected && !unknown.known && !unknown.can_begin &&
            !unknown.active && unknown.status == "unclaimed" &&
            !station->begin_derelict_recovery() && bytes(*station) == untouched,
        "Actual unknown station source disclosed or retrieved an unobserved "
        "object");
  phase(*station, output, "station-selected", "factory", "new-game",
        "Actual optional factory; no contact coordinates or income.", manifest);
  auto ready = test_support::make_derelict_recovery_fixture();
  const auto ready_bytes = bytes(*ready);
  for (unsigned repeat = 0; repeat < 3; ++repeat) {
    check(need(ready->derelict_recovery_view()).can_begin &&
              !need(ready->derelict_recovery_sale_view()).can_sell,
          "Inspected matched-speed remote source lost admission or allowed "
          "remote sale");
  }
  check(!ready->sell_derelict_recovery(
            need(ready->derelict_recovery_sale_view()).token) &&
            bytes(*ready) == ready_bytes,
        "Remote quote/refusal changed the actual ready voyage");
  phase(*ready, output, "ready", "shared-helper", "scan-inspect",
        "Real discovery, charged jump and inspection; two declared Home/120m "
        "pose initializations.",
        manifest);
  auto acquisition = std::make_unique<Session>(*ready);
  need(acquisition->begin_derelict_recovery());
  const auto begun = bytes(*acquisition);
  check(!acquisition->begin_derelict_recovery() && bytes(*acquisition) == begun,
        "Repeated Begin changed a live operation");
  phase(*acquisition, output, "started", "ready", "begin",
        "Actual explicit Begin, zero acquired intervals.", manifest);
  auto started = std::make_unique<Session>(*acquisition);
  for (unsigned tick = 0; tick < 1800; ++tick)
    need(acquisition->advance({}));
  check(need(acquisition->derelict_recovery_view()).acquired_ticks == 1800,
        "1800 ordinary physics intervals did not give half progress");
  phase(*acquisition, output, "half", "started", "advance-1800-neutral",
        "Actual ordinary committed flight ticks.", manifest);
  auto halfway = std::make_unique<Session>(*acquisition);
  for (unsigned tick = 1800; tick < 3599; ++tick)
    need(acquisition->advance({}));
  check(need(acquisition->derelict_recovery_view()).active &&
            need(acquisition->derelict_recovery_view()).acquired_ticks ==
                3599 &&
            !acquisition->knowledge()->derelict_recovery->canister,
        "3599 ticks claimed a canister early");
  phase(*acquisition, output, "near", "half", "advance-1799-neutral",
        "Actual ordinary committed flight ticks; one interval still required.",
        manifest);
  need(acquisition->advance({}));
  check(need(acquisition->derelict_recovery_view()).status == "carried" &&
            need(acquisition->derelict_recovery_view()).acquired_ticks ==
                3600 &&
            !acquisition->begin_derelict_recovery(),
        "Completion did not deplete one source exactly once");
  phase(*acquisition, output, "complete", "near", "advance-one-neutral",
        "Actual3600th interval and physical claim.", manifest);
  auto canceled = std::make_unique<Session>(*halfway);
  need(canceled->cancel_derelict_recovery());
  check(!canceled->knowledge()->derelict_recovery->active &&
            !canceled->knowledge()->derelict_recovery->canister,
        "Explicit cancel claimed or banked unfinished work");
  phase(*canceled, output, "cancelled", "half", "cancel",
        "Explicit cancellation after1800 real intervals; source unclaimed.",
        manifest);
  auto interrupted = std::make_unique<Session>(*started);
  NativeFlightControls thrust;
  thrust.positive_translation.x = 1;
  unsigned applied{}, elapsed{};
  while (interrupted->knowledge()->derelict_recovery->active && elapsed < 600) {
    const auto step = need(interrupted->advance(thrust));
    applied += step.actuation.central.propulsion.applied_force_newtons.x > 0 &&
               step.fuel_debit_quanta > 0;
    ++elapsed;
  }
  check(applied > 0 && elapsed > 0 && elapsed <= 600 &&
            !interrupted->knowledge()->derelict_recovery->active &&
            !interrupted->knowledge()->derelict_recovery->canister &&
            interrupted->resources()->flight_quanta <
                started->resources()->flight_quanta,
        "Real thrust did not spend fuel and cancel on the physical retrieval "
        "envelope");
  manifest["interruption_ticks"] = elapsed;
  phase(*interrupted, output, "interrupted", "started",
        "advance-positive-x-until-canceled",
        "Actual propulsion and envelope exit; exact control/tick count "
        "recorded in manifest.",
        manifest);
  auto origin = origin_boundary(*acquisition);
  phase(*origin, output, "origin-carried", "initialized-origin",
        "same-tick-attachment",
        "Declared Origin boundary; preserved real claim/entry/resources/time, "
        "dropped travel receipts, no flown return.",
        manifest);
  auto paid = std::make_unique<Session>(*origin);
  need(paid->enable_paid_operations(need(paid->paid_operations_view()).token));
  check(need(paid->paid_operations_view()).cash == 180 &&
            paid->knowledge()->paid_operations->version == 1,
        "Paid-before-sale enrollment changed opening200 minus20 berth");
  phase(*paid, output, "origin-paid", "origin-carried", "enable-paid",
        "Actual explicit enrollment, no canister sale yet.", manifest);
  auto free_sold = std::make_unique<Session>(*origin);
  sell(*free_sold, 0, 0, 100);
  check(!free_sold->knowledge()->paid_operations &&
            free_sold->knowledge()->derelicts == origin->knowledge()->derelicts,
        "Free sale silently enrolled a paid budget or erased inspection notes");
  phase(*free_sold, output, "sold", "origin-carried", "sell",
        "Actual reviewed sale100; paid profile remains absent.", manifest);
  auto paid_sold = std::make_unique<Session>(*paid);
  sell(*paid_sold, 0, 0, 100);
  check(need(paid_sold->paid_operations_view()).cash == 280 &&
            paid_sold->knowledge()->paid_operations->version == 3,
        "Paid sale did not promote3 and add100 once to180");
  phase(*paid_sold, output, "paid-sold", "origin-paid", "sell",
        "Actual reviewed sale and paid-v3 settlement.", manifest);
  survey_after_paid_sale(*paid_sold, output);
  auto enrolled = std::make_unique<Session>(*free_sold);
  need(enrolled->enable_paid_operations(
      need(enrolled->paid_operations_view()).token));
  check(need(enrolled->paid_operations_view()).cash == 280 &&
            enrolled->knowledge()->paid_operations->version == 3,
        "Prior-sale enrollment lost100 income or added a second opening grant");
  phase(*enrolled, output, "prior-sale-enrolled", "sold", "enable-paid",
        "Actual enrollment after prior sale; same280 total.", manifest);
  auto lost = std::make_unique<Session>(*acquisition);
  lose(*lost);
  check(need(lost->derelict_recovery_view()).status == "lost" &&
            !need(lost->derelict_recovery_sale_view()).can_sell &&
            lost->knowledge()->derelicts == acquisition->knowledge()->derelicts,
        "Actual replacement respawned/sold lost cargo or erased its inspection "
        "note");
  phase(*lost, output, "lost-cargo", "complete",
        "record-loss-complete-recovery",
        "Actual explicit classified loss/replacement; finite source stays "
        "depleted, no collision claim.",
        manifest);
  auto unfinished = std::make_unique<Session>(*halfway);
  lose(*unfinished);
  check(!unfinished->knowledge()->derelict_recovery->active &&
            !unfinished->knowledge()->derelict_recovery->canister,
        "Replacement completed unfinished retrieval or claimed a source");
  phase(
      *unfinished, output, "unfinished-loss", "half",
      "record-loss-complete-recovery",
      "Actual explicit classified loss cancels partial work; no item claimed.",
      manifest);
  auto debt_document =
      std::make_unique<NativeSaveDocument>(need(paid->save_document()));
  auto& debt_account = *std::get<FreedomKnowledgeSaveDocument>(*debt_document)
                            .knowledge.paid_operations;
  debt_account.spent_credits = 200;
  debt_account.operating_tab = 85;
  debt_account.recovery_due = 60;
  auto debt = std::make_unique<Session>(need(Session::open(
      need(native_select_save_document(std::move(*debt_document))))));
  phase(*debt, output, "origin-debt", "origin-paid",
        "initialize-financial-boundary",
        "Explicit aggregate financial fixture cash0/tab85/insurance60; not "
        "flown spending/debt history.",
        manifest);
  sell(*debt, 60, 20, 20);
  check(
      need(debt->paid_operations_view()).cash == 20 &&
          debt->knowledge()->paid_operations->operating_tab == 65 &&
          debt->knowledge()->paid_operations->recovery_due == 0,
      "Sale100 failed independent insurance60/tab20/net20 and residual65 tab");
  phase(
      *debt, output, "debt-sold", "origin-debt", "sell",
      "Actual reviewed sale against disclosed initialized financial boundary.",
      manifest);
  check(bytes(*ready) == ready_bytes && manifest["phases"].size() == 17,
        "Independent branches changed the ready source or phase roster");
  manifest["completed"] = true;
  manifest["checks"] = checks;
  write_manifest(output, manifest);
}
} // namespace
auto main(int argc, char** argv) -> int {
  std::filesystem::path output;
  bool exported{};
  try {
    if (argc == 1) {
      output =
          std::filesystem::temp_directory_path() /
          ("apsis-derelict-recovery-scenario-" +
           std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count()));
    } else if (argc == 3 && std::string_view{argv[1]} == "--export" &&
               !std::string_view{argv[2]}.empty() &&
               std::filesystem::path{argv[2]}.is_absolute()) {
      output = argv[2];
      exported = true;
    } else {
      std::cerr << "Usage: native-derelict-recovery-tests [--export ABS_DIR]\n";
      return 2;
    }
    if (std::filesystem::is_symlink(output) ||
        (std::filesystem::exists(output) &&
         (!std::filesystem::is_directory(output) ||
          !std::filesystem::is_empty(output)))) {
      std::cerr << "Output must be an empty directory or new absolute path\n";
      return 2;
    }
    std::filesystem::create_directories(output);
    scenario(output);
    if (!exported) std::filesystem::remove_all(output);
    std::cout << "Native derelict recovery: " << checks
              << " checks, 0 failures\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Native derelict recovery after " << checks
              << " checks: " << error.what() << "; retained output " << output
              << '\n';
    return 1;
  }
}
