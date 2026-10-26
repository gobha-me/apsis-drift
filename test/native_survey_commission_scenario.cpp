#include "apsis_drift/freedom_system_network.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
unsigned checks{};
auto check(bool valid, const char* message) -> void {
  ++checks;
  if (!valid) throw std::runtime_error(message);
}
template <class T, class E>
auto need(std::expected<T, E> result,
          std::source_location where = std::source_location::current()) -> T {
  if (!result) {
    auto message = "Commission scenario line " + std::to_string(where.line());
    if constexpr (std::is_same_v<E, std::string>)
      message += ": " + result.error();
    else if constexpr (requires { result.error().detail; })
      message += ": " + result.error().detail;
    throw std::runtime_error(message);
  }
  if constexpr (!std::is_void_v<T>) return std::move(*result);
}
auto bytes(const NativeFreedomFlightSession& session) -> std::string {
  return need(encode_native_save_document_json(need(session.save_document())));
}
auto reopen(const NativeFreedomFlightSession& session)
    -> NativeFreedomFlightSession {
  const auto source = bytes(session);
  auto restored =
      need(NativeFreedomFlightSession::open(need(native_select_save_document(
          need(decode_native_save_document_json(source))))));
  check(bytes(restored) == source, "Commission complete Save/Continue differs");
  return restored;
}
auto checkpoint(const NativeFreedomFlightSession& session,
                const std::optional<std::filesystem::path>& output,
                std::string_view name, Json& phases) -> void {
  auto restored = reopen(session);
  check(restored.knowledge()->survey_commissions ==
            session.knowledge()->survey_commissions,
        "Commission ledger or provenance changed on Continue");
  if (output) need(session.save_as(*output / (std::string{name} + ".json")));
  const auto view = need(session.survey_commission_view());
  phases.push_back({{"file", std::string{name} + ".json"},
                    {"tick", std::to_string(view.tick)},
                    {"acquired_ticks", view.acquired_ticks},
                    {"earnings", view.earnings}});
}
auto refuse_actions(NativeFreedomFlightSession& session, SimulationTick offer,
                    std::uint32_t sequence) -> void {
  const auto before = bytes(session);
  check(!session.accept_survey_commission(offer, sequence) &&
            !session.submit_survey_commission(sequence) &&
            !session.abandon_survey_commission(sequence) &&
            bytes(session) == before,
        "Unavailable commission actions changed whole voyage");
}
auto recovered(NativeFreedomFlightSession session)
    -> NativeFreedomFlightSession {
  const auto original = need(session.recovery_document());
  const auto craft = session.document().origin.state.craft;
  // Explicit authenticated application loss fixture, not a collision claim.
  need(session.record_loss(
      FreedomLossCause::recoverable_destruction, craft,
      session.document().flight.tick,
      need(freedom_recovery_source_checksum(original.voyage))));
  auto pending = reopen(session);
  const auto before = bytes(session);
  check(!session.accept_survey_commission(0, 1) &&
            !session.submit_survey_commission(1) &&
            !session.abandon_survey_commission(1) && bytes(session) == before,
        "Pending recovery allowed commission mutation");
  if (session.knowledge()->paid_operations) {
    const auto frozen = bytes(session);
    const auto terms = need(session.paid_operations_view());
    check(!terms.at_origin && !terms.can_enable && !terms.can_purchase &&
              !terms.can_retainer && !session.purchase_paid_fuel(terms.token) &&
              !session.request_paid_retainer(terms.token) &&
              bytes(session) == frozen,
          "Pending loss allowed paid mutation after frozen snapshot");
  }
  need(session.complete_recovery());
  need(pending.complete_recovery());
  check(bytes(session) == bytes(pending) && session.walker() &&
            session.docking()->attached &&
            session.document().origin.state.craft != craft,
        "Commission recovery did not converge on actual replacement owner");
  return session;
}
// Explicit saved-space placement after real walking/boarding/release. It
// retains the actual source owners, clock, fuel and observations; this is not
// a claim that this test flew the intervening station-to-orbit ascent.
auto home_space(NativeFreedomFlightSession source)
    -> NativeFreedomFlightSession {
  auto document =
      std::get<FreedomKnowledgeSaveDocument>(need(source.save_document()));
  auto& flight =
      std::get<FreedomBoardingSaveDocument>(document.voyage.voyage.base)
          .voyage.flight;
  const auto planet = need(find_local_system_planet(
      source.system(), flight.origin.recipe.home_planet));
  flight.flight.position_metres = {0, 0,
                                   planet->descriptor.radius.value * 10000.0};
  flight.flight.linear_velocity_metres_per_second = {};
  flight.flight.angular_velocity_radians_per_second = {};
  flight.flight =
      need(canonicalize_rigid_body_state({source.system()}, flight.flight));
  return need(NativeFreedomFlightSession::open(
      need(native_select_save_document(std::move(document)))));
}
// Real charged arrival supplies the world and travel receipts. Only its current
// p/v is initialized to a circular measurement orbit, at the unchanged tick.
auto measurement_orbit(const NativeFreedomFlightSession& source)
    -> NativeFreedomFlightSession {
  auto document = need(source.travel_document());
  auto& flight =
      std::get<FreedomFlightSaveDocument>(document.voyage.voyage.voyage.base);
  const auto view = need(source.survey_commission_view());
  check(flight.flight.frame.system == view.system &&
            flight.flight.frame.planet == view.planet,
        "Actual neighbor jump must install the commissioned planet owner");
  const auto planet =
      need(find_local_system_planet(source.system(), view.planet));
  const double radius = planet->descriptor.radius.value * 1500.0;
  flight.flight.position_metres = {radius, 0, 0};
  flight.flight.linear_velocity_metres_per_second = {};
  flight.flight.angular_velocity_radians_per_second = {};
  const auto gravity =
      need(evaluate_central_body_gravity({source.system()}, flight.flight));
  flight.flight.linear_velocity_metres_per_second.z = std::sqrt(
      gravity.gravitational_parameter_metres_cubed_per_second_squared / radius);
  flight.model.assistance = false;
  flight.model.hold = {};
  return need(NativeFreedomFlightSession::open(
      need(native_select_save_document(std::move(document)))));
}
auto charged_leg(NativeFreedomFlightSession& session, SystemId destination)
    -> void {
  const auto charges = session.resources()->jump_charges;
  const auto source_craft = session.document().origin.state.craft;
  const auto before_account = session.knowledge()->survey_commissions;
  need(session.select_jump(destination));
  need(session.begin_jump());
  const auto spool = bytes(session);
  check(!session.submit_survey_commission(1) && bytes(session) == spool,
        "Active jump must not submit commission");
  for (SimulationTick tick = 1; tick <= kJumpSpoolTicks + kJumpTransitTicks;
       ++tick) {
    const auto step = need(session.advance({}));
    if (tick < kJumpSpoolTicks)
      check(session.resources()->jump_charges == charges,
            "Uncommitted spool billed a commission trip charge");
    if (tick == kJumpSpoolTicks)
      check(step.jump_committed &&
                session.resources()->jump_charges == charges - 1,
            "Commission trip must commit exactly one real jump charge");
    if (tick == kJumpSpoolTicks + kJumpTransitTicks)
      check(step.jump_arrived, "Commission trip did not use actual arrival");
  }
  check(session.system().catalog.id == destination &&
            session.resources()->jump_charges == charges - 1 &&
            session.document().origin.state.craft == source_craft &&
            session.knowledge()->survey_commissions->active->sequence ==
                before_account->active->sequence &&
            session.knowledge()->survey_commissions->active->system ==
                before_account->active->system &&
            session.knowledge()->survey_commissions->active->planet ==
                before_account->active->planet &&
            session.knowledge()->survey_commissions->active->accepted_tick ==
                before_account->active->accepted_tick &&
            session.knowledge()->survey_commissions->active->deadline_tick ==
                before_account->active->deadline_tick &&
            session.knowledge()->survey_commissions->paid_count ==
                before_account->paid_count &&
            (!before_account->active->measurement ||
             before_account->active->measurement->acquired_ticks != 3600 ||
             session.knowledge()->survey_commissions == before_account),
        "Actual jump changed commission reservation/completed history or "
        "installed the wrong world");
  // Leave the exact frozen-arrival admission boundary through ordinary
  // committed physics before any separately labelled pose initialization.
  need(session.advance({}));
}
// Home arrival is real. The following final-approach pose is an initialized
// same-tick port fixture, not flown return guidance. Actual select/capture
// commands must still certify the attachment used for payment.
auto home_port(const NativeFreedomFlightSession& source)
    -> NativeFreedomFlightSession {
  auto document = need(source.travel_document());
  auto& flight =
      std::get<FreedomFlightSaveDocument>(document.voyage.voyage.voyage.base);
  const auto station =
      generate_origin_station(flight.origin.recipe.universe_seed);
  const auto geometry = need(origin_station_geometry(station));
  flight.flight = need(release_origin_port(source.system(), station, geometry,
                                           {geometry.version,
                                            {station.id, 1},
                                            flight.flight.craft,
                                            flight.flight.tick}));
  auto session = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(std::move(document)))));
  need(session.select_port(1));
  check(need(session.assess_port()).decision ==
            OriginDockDecision::capture_ready,
        "Initialized home port pose is not actually capture ready");
  need(session.capture_port());
  return session;
}
// Initialized whole-clock deadline boundary, not a claim of flown elapsed time.
auto deadline_station(const NativeFreedomFlightSession& accepted)
    -> NativeFreedomFlightSession {
  auto document =
      std::get<FreedomKnowledgeSaveDocument>(need(accepted.save_document()));
  auto& flight =
      std::get<FreedomStartingAssemblySaveDocument>(document.voyage.voyage.base)
          .journey.voyage.flight;
  const auto deadline = need(accepted.survey_commission_view()).deadline_tick;
  const auto station =
      generate_origin_station(flight.origin.recipe.universe_seed);
  const auto geometry = need(origin_station_geometry(station));
  flight.flight = need(release_origin_port(
      accepted.system(), station, geometry,
      {geometry.version, {station.id, 1}, flight.flight.craft, deadline}));
  flight.origin.state.tick = deadline;
  document.voyage.resources.tick = deadline;
  return need(NativeFreedomFlightSession::open(
      need(native_select_save_document(std::move(document)))));
}
} // namespace

auto run_commission_scenario(const std::optional<std::filesystem::path>& output,
                             bool paid_mode) -> void {
  Json phases = Json::array();
  auto ordinary =
      need(NativeFreedomFlightSession::open(need(native_new_game({42}))));
  const auto ordinary_bytes = bytes(ordinary);
  check(!need(ordinary.survey_commission_view()).supported,
        "Historical ordinary chart gained commission policy implicitly");
  refuse_actions(ordinary, 0, 1);
  check(bytes(ordinary) == ordinary_bytes,
        "Unsupported reader changed ordinary startup");
  auto session = need(NativeFreedomFlightSession::open(
      need(native_planetary_network_new_game({42}))));
  const auto offered = need(session.survey_commission_view());
  check(offered.supported && !offered.selected && offered.at_origin &&
            offered.can_accept && !offered.can_submit &&
            offered.earnings == 0 && offered.offer_until > offered.tick &&
            offered.offer_sequence == 1,
        "Real optional planetary station lacks a read-only first offer");
  const auto unselected = bytes(session);
  for (unsigned i = 0; i < 3; ++i)
    (void)need(session.survey_commission_view());
  check(bytes(session) == unselected &&
            !session.knowledge()->survey_commissions,
        "Reading commissions selected a ledger or advanced time");
  checkpoint(session, output, "commission-station", phases);
  if (paid_mode) {
    const auto quote = need(session.paid_operations_view());
    check(quote.supported && quote.can_enable && !quote.selected &&
              quote.opening_cash == 180 && quote.service_credits == 0,
          "Optional paid selection lacks disclosed opening terms");
    const auto unchanged = bytes(session);
    check(!session.enable_paid_operations(quote.token + "x") &&
              bytes(session) == unchanged,
          "Stale enrollment token mutated whole Save");
    need(session.enable_paid_operations(quote.token));
    check(need(session.paid_operations_view()).cash == 180 &&
              !session.enable_paid_operations(quote.token) &&
              !session.replenish_resources(),
          "Enrollment repeated its grant or escaped through free service");
    checkpoint(session, output, "paid-selected", phases);
    // Initialized depleted fuel stores test a purchase, not flown consumption.
    auto depleted =
        std::get<FreedomKnowledgeSaveDocument>(need(session.save_document()));
    depleted.voyage.resources.flight_quanta = kFreedomFlightCapacityQuanta / 2;
    depleted.voyage.resources.jump_charges = 2;
    auto customer = need(NativeFreedomFlightSession::open(
        need(native_select_save_document(depleted))));
    checkpoint(customer, output, "paid-fuel-station", phases);
    const auto fuel = need(customer.paid_operations_view());
    const auto before_purchase = bytes(customer);
    check(fuel.can_purchase && fuel.flight_credits == 20 &&
              fuel.jump_credits == 15 && fuel.service_credits == 35 &&
              !customer.purchase_paid_fuel(quote.token) &&
              bytes(customer) == before_purchase,
          "Actual quantities or stale fuel quote changed the account");
    need(customer.purchase_paid_fuel(fuel.token));
    const auto bought = bytes(customer);
    check(need(customer.paid_operations_view()).cash == 145 &&
              customer.resources()->flight_quanta ==
                  kFreedomFlightCapacityQuanta &&
              customer.resources()->jump_charges == kFreedomJumpCapacity &&
              !customer.purchase_paid_fuel(fuel.token) &&
              bytes(customer) == bought,
          "Debit/restoration did not commit together or repeated");
    checkpoint(customer, output, "paid-fuel-bought", phases);
    // Keep the trip's actual starting stores untouched. The separate purchase
    // fixture above is not charged against the forthcoming survey voyage.
  }
  const auto current_start = bytes(session);
  check(!session.accept_survey_commission(offered.offer_until - 1,
                                          offered.offer_sequence) &&
            !session.accept_survey_commission(offered.offer_until,
                                              offered.offer_sequence + 1) &&
            !session.submit_survey_commission(1) &&
            bytes(session) == current_start,
        "Stale offer/sequence or premature submission changed whole Save");
  const auto body = session.document();
  const auto resources = session.resources();
  const auto entries = session.knowledge()->entries;
  need(session.accept_survey_commission(offered.offer_until,
                                        offered.offer_sequence));
  check(
      session.document() == body && session.resources() == resources &&
          session.knowledge()->entries == entries &&
          need(session.survey_commission_view()).acquired_ticks == 0,
      "Acceptance changed physics/resources/knowledge or invented measurement");
  checkpoint(session, output, "commission-accepted", phases);
  const auto accepted = session;
  if (paid_mode) {
    // Explicit insolvent account/store fixture: no claim these debts were
    // generated by the physical trip below. Verify a capped pilot can depart.
    auto poor_document =
        std::get<FreedomKnowledgeSaveDocument>(need(session.save_document()));
    poor_document.knowledge.paid_operations->spent_credits = 200;
    poor_document.knowledge.paid_operations->operating_tab = 200;
    poor_document.voyage.resources.flight_quanta = 0;
    poor_document.voyage.resources.jump_charges = 0;
    auto poor = need(NativeFreedomFlightSession::open(
        need(native_select_save_document(poor_document))));
    const auto support = need(poor.paid_operations_view());
    check(!support.can_purchase && support.can_retainer &&
              support.retainer_cash == 0 && support.retainer_tab == 0 &&
              support.retainer_subsidy == 85,
          "Capped insolvent starter lacks explicit noncash hardship support");
    checkpoint(poor, output, "paid-retainer-station", phases);
    need(poor.request_paid_retainer(support.token));
    const auto funded = bytes(poor);
    check(need(poor.paid_operations_view()).cash == 0 &&
              need(poor.paid_operations_view()).operating_tab == 200 &&
              poor.resources()->jump_charges == 3 &&
              poor.resources()->flight_quanta == kFreedomFlightCapacityQuanta &&
              !poor.request_paid_retainer(support.token) &&
              bytes(poor) == funded,
          "Support stacked stores, minted cash or rejected capped departure");
    checkpoint(poor, output, "paid-retainer-funded", phases);
  }
  auto malformed =
      std::get<FreedomKnowledgeSaveDocument>(need(session.save_document()));
  malformed.knowledge.survey_commissions->active->planet = PlanetId{0};
  check(!encode_freedom_knowledge_document_json(malformed) &&
            !native_select_save_document(malformed),
        "Malformed commission target entered complete save/session admission");
  const auto active = bytes(session);
  check(!session.accept_survey_commission(offered.offer_until,
                                          offered.offer_sequence) &&
            !session.submit_survey_commission(offered.offer_sequence) &&
            bytes(session) == active,
        "Repeated acceptance or unearned payout changed accepted voyage");
  auto abandoned = reopen(session);
  need(abandoned.abandon_survey_commission(offered.offer_sequence));
  check(!abandoned.knowledge()->survey_commissions->active &&
            need(abandoned.survey_commission_view()).earnings == 0,
        "Abandonment paid credits or retained active work");
  auto expired = deadline_station(accepted);
  const auto expired_before = bytes(expired);
  check(need(expired.survey_commission_view()).expired &&
            !expired.submit_survey_commission(offered.offer_sequence) &&
            !expired.accept_survey_commission(offered.offer_until,
                                              offered.offer_sequence) &&
            bytes(expired) == expired_before,
        "Expired reservation or stale epoch paid/changed the deadline fixture");
  const auto replacement = need(expired.survey_commission_view());
  need(expired.accept_survey_commission(replacement.offer_until,
                                        replacement.offer_sequence));
  check(need(expired.survey_commission_view()).sequence == 2 &&
            need(expired.survey_commission_view()).earnings == 0 &&
            need(expired.survey_commission_view()).acquired_ticks == 0,
        "Explicit expired-offer replacement reused old sequence/work/payment");
  (void)reopen(expired);
  for (unsigned tick = 0; tick < 1194; ++tick)
    need(session.advance_walk({0, -1, 0}));
  need(session.begin_boarding());
  for (SimulationTick tick = 0; tick < kGameplayBoardingTicks; ++tick)
    need(session.advance_walk({}));
  check(need(session.survey_commission_view()).can_abandon,
        "Seated attached Origin lost the same commission action eligibility");
  need(session.release_port());
  session = home_space(std::move(session));
  need(session.set_assistance(false));
  charged_leg(session, offered.system);
  session = measurement_orbit(session);
  const auto orbit_bytes = bytes(session);
  for (unsigned i = 0; i < 3; ++i)
    (void)need(session.survey_commission_view());
  check(bytes(session) == orbit_bytes &&
            need(session.survey_commission_view()).acquired_ticks == 0,
        "Arrival, initialized orbit, read or Save invented fresh measurement");
  NativeFlightControls invalid;
  invalid.positive_rotation.x = std::numeric_limits<double>::quiet_NaN();
  check(!session.advance(invalid) &&
            !session.advance({}, SimulationSeconds{0}) &&
            bytes(session) == orbit_bytes,
        "Invalid flight interval acquired commission work or changed state");
  need(session.begin_orbital_survey());
  const auto acquisition_start = session.document().flight.tick;
  for (unsigned tick = 0; tick < kSurveyCommissionMeasurementTicks - 1; ++tick)
    need(session.advance({}));
  check(need(session.survey_commission_view()).acquired_ticks == 3599 &&
            !need(session.survey_commission_view()).completed &&
            session.document().flight.tick == acquisition_start + 3599,
        "3599 genuine qualifying flight ticks completed or skipped commission "
        "work");
  checkpoint(session, output, "commission-near", phases);
  auto unfinished_loss = recovered(session);
  check(unfinished_loss.knowledge()->survey_commissions->active &&
            !unfinished_loss.knowledge()
                 ->survey_commissions->active->measurement &&
            need(unfinished_loss.survey_commission_view()).earnings == 0,
        "Replacement retained old unfinished craft acquisition or paid it");
  checkpoint(unfinished_loss, output, "commission-recovery-unfinished", phases);
  auto continued = reopen(session);
  need(session.advance({}));
  need(continued.advance({}));
  check(bytes(session) == bytes(continued) &&
            need(session.survey_commission_view()).completed &&
            need(session.survey_commission_view()).acquired_ticks == 3600 &&
            !need(session.survey_commission_view()).can_submit,
        "3600th committed tick/Continue differs or remotely pays commission");
  checkpoint(session, output, "commission-complete", phases);
  const auto earned = session.knowledge()->survey_commissions->active;
  auto completed_loss = recovered(session);
  check(
      completed_loss.knowledge()->survey_commissions->active == earned &&
          need(completed_loss.survey_commission_view()).can_submit,
      "Replacement discarded completed provenance or cannot submit at Origin");
  need(completed_loss.submit_survey_commission(offered.offer_sequence));
  check(need(completed_loss.survey_commission_view()).earnings == 100,
        "Completed old-craft measurement did not pay the personal replacement "
        "ledger");
  if (paid_mode) {
    check(need(completed_loss.paid_operations_view()).cash == 180 &&
              need(completed_loss.paid_operations_view()).recovery_due == 0,
          "Completed old-craft report bypassed the actual 100-credit excess");
  }
  checkpoint(completed_loss, output, "commission-recovery-complete", phases);
  charged_leg(session, accepted.document().flight.frame.system);
  session = home_port(session);
  check(session.knowledge()->survey_commissions->active == earned &&
            session.resources()->jump_charges == 1 &&
            session.travel()->next_attempt == 3 &&
            need(session.survey_commission_view()).can_submit,
        "Two actual charged legs/capture lost immutable completed report or "
        "reserve charge");
  checkpoint(session, output, "commission-returned", phases);
  // A completed report also expires. Initialize every active clock and the
  // constrained port pose together; immutable jump/report histories stay old.
  auto late_document = need(session.travel_document());
  auto& late_flight = std::get<FreedomBoardingSaveDocument>(
                          late_document.voyage.voyage.voyage.base)
                          .voyage.flight;
  const auto deadline = need(session.survey_commission_view()).deadline_tick;
  const auto home_station =
      generate_origin_station(late_flight.origin.recipe.universe_seed);
  const auto home_geometry = need(origin_station_geometry(home_station));
  late_flight.flight =
      need(release_origin_port(session.system(), home_station, home_geometry,
                               {home_geometry.version,
                                {home_station.id, 1},
                                late_flight.flight.craft,
                                deadline}));
  late_flight.origin.state.tick = deadline;
  late_document.voyage.voyage.resources.tick = deadline;
  auto late = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(std::move(late_document)))));
  const auto late_bytes = bytes(late);
  check(need(late.survey_commission_view()).completed &&
            need(late.survey_commission_view()).expired &&
            !late.submit_survey_commission(offered.offer_sequence) &&
            bytes(late) == late_bytes &&
            late.knowledge()->survey_commissions->active == earned &&
            need(late.survey_commission_view()).earnings == 0,
        "Completed report at exact deadline paid or discarded observations");
  checkpoint(late, output, "commission-expired", phases);
  const auto returned_body = session.document();
  const auto returned_resources = session.resources();
  const auto returned_travel = session.travel();
  const auto wrong_sequence = bytes(session);
  check(!session.submit_survey_commission(offered.offer_sequence + 1) &&
            bytes(session) == wrong_sequence,
        "Stale submission sequence changed completed whole voyage");
  need(session.submit_survey_commission(offered.offer_sequence));
  check(need(session.survey_commission_view()).earnings == 100 &&
            !session.knowledge()->survey_commissions->active &&
            session.knowledge()->survey_commissions->latest_paid->commission ==
                *earned &&
            session.document() == returned_body &&
            session.resources() == returned_resources &&
            session.travel() == returned_travel,
        "Payout did not retain receipt or changed physical/resources/travel "
        "state");
  if (paid_mode) {
    check(need(session.paid_operations_view()).cash == 280 &&
              session.knowledge()->paid_operations->settled_paid_count == 1,
          "Returned paid report did not join its actual survey income");
    check(!session.replenish_resources(),
          "Paid return escaped through free technical refueling");
  }
  checkpoint(session, output, "commission-paid", phases);
  const auto paid = bytes(session);
  check(!session.submit_survey_commission(offered.offer_sequence) &&
            bytes(session) == paid,
        "Repeated submission duplicated earnings or changed Save");
  auto paid_loss = recovered(session);
  check(need(paid_loss.survey_commission_view()).earnings == 100 &&
            paid_loss.knowledge()->survey_commissions ==
                session.knowledge()->survey_commissions,
        "Replacement discarded payment history or duplicated earnings");
  (void)reopen(paid_loss);
  const auto again = need(session.survey_commission_view());
  const auto old_surveys = session.knowledge()->orbital_surveys;
  need(session.accept_survey_commission(again.offer_until,
                                        again.offer_sequence));
  check(need(session.survey_commission_view()).sequence == 2 &&
            need(session.survey_commission_view()).acquired_ticks == 0 &&
            !need(session.survey_commission_view()).completed &&
            need(session.survey_commission_view()).earnings == 100 &&
            session.knowledge()->orbital_surveys == old_surveys,
        "Repeat commission reused old survey instead of requiring fresh "
        "intervals");
  checkpoint(session, output, "commission-repeat", phases);
  if (output) {
    const Json manifest{
        {"schema", "apsis.survey-commission-scenario/1"},
        {"scope",
         "Actual station walk/boarding, two charged system jumps, 3600 "
         "committed measurement ticks and physical port capture. Explicit "
         "saved home-space, remote circular-orbit and home-port placements; "
         "not uninterrupted manual station-to-orbit/return flight. Deadline "
         "fixture initializes a whole shared clock; loss fixtures declare "
         "authenticated application loss, not collision."},
        {"phases", phases},
        {"checks", checks}};
    std::ofstream file{*output / "commission-manifest.json"};
    file << manifest.dump(2) << '\n';
    check(static_cast<bool>(file), "Commission manifest write failed");
  }
  std::cout << "Survey commissions: " << checks << " checks, 0 failures\n";
}
// A bounded destination extension, appended after both historical scenarios.
// Only the noted stores/clock/pose boundaries are initialized; commands,
// boarding, charged handoffs, acquisition and settlement remain authoritative.
auto run_destination_scenario(const std::optional<std::filesystem::path>& root)
    -> void {
  std::optional<std::filesystem::path> output;
  if (root) {
    output = *root / "commission-destinations";
    std::filesystem::create_directories(*output);
  }
  Json phases = Json::array();
  auto session = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_planetary_network_new_game({42})))));
  const auto initial = bytes(*session);
  const auto offers = need(session->survey_commission_offers());
  check(offers.offer_count == 2 && !session->knowledge()->survey_commissions &&
            offers.offers[0].target != offers.offers[1].target,
        "Station must expose exactly two distinct earned chart destinations");
  const auto nearby = offers.offers[0].target;
  const auto waypoint = offers.offers[1].target;
  const auto quote = need(
      session->survey_commission_view(SurveyCommissionKind::orbital, waypoint));
  for (std::uint32_t i = 0; i < offers.offer_count; ++i) {
    const auto& row = offers.offers[i];
    check(row.route.available && row.route.leg_count == 2 &&
              row.route.required_charges == 2 &&
              row.route.legs[0].destination == row.target.system &&
              row.route.legs[1].destination == row.route.legs[0].source &&
              !row.system_name.empty() && !row.body_name.empty(),
          "Offer must disclose two existing named Origin return legs");
  }
  check(
      quote.can_accept && quote.system == waypoint.system &&
          quote.planet.value == waypoint.body.value &&
          quote.body_name == offers.offers[1].body_name &&
          need(session->survey_commission_view()).system == nearby.system &&
          bytes(*session) == initial,
      "Chosen quote must name its actual planet without selecting an account");
  checkpoint(*session, output, "station", phases);
  auto station = std::make_unique<NativeFreedomFlightSession>(reopen(*session));
  need(station->accept_survey_commission_target(waypoint, quote.offer_until,
                                                quote.offer_sequence));
  checkpoint(*station, output, "station-accepted", phases);
  // A separate initialized deadline branch preserves the actual accepted terms.
  auto expired =
      std::make_unique<NativeFreedomFlightSession>(deadline_station(*station));
  const auto renewal = need(
      expired->survey_commission_view(SurveyCommissionKind::orbital, waypoint));
  const auto expired_bytes = bytes(*expired);
  check(renewal.sequence == quote.offer_sequence && renewal.expired &&
            renewal.can_abandon && renewal.can_accept &&
            need(expired->survey_commission_view()) == renewal &&
            !expired->survey_commission_view(SurveyCommissionKind::orbital,
                                             nearby) &&
            !expired->accept_survey_commission_target(
                nearby, renewal.offer_until, renewal.offer_sequence) &&
            !expired->accept_survey_commission(renewal.offer_until,
                                               renewal.offer_sequence) &&
            !expired->accept_geological_survey_commission(
                renewal.offer_until, renewal.offer_sequence) &&
            bytes(*expired) == expired_bytes,
        "Expired work must remain visible and locked to its original "
        "kind/target");
  checkpoint(*expired, output, "expired", phases);
  need(expired->accept_survey_commission_target(waypoint, renewal.offer_until,
                                                renewal.offer_sequence));
  check(need(expired->survey_commission_view()).sequence == 2 &&
            need(expired->survey_commission_view()).acquired_ticks == 0 &&
            expired->knowledge()->survey_commissions->version == 3,
        "Same-target expired renewal requires a new serial and fresh work");
  checkpoint(*expired, output, "renewed", phases);
  need(expired->abandon_survey_commission(2));
  const auto geo =
      need(expired->survey_commission_view(SurveyCommissionKind::geological));
  need(expired->accept_geological_survey_commission(geo.offer_until,
                                                    geo.offer_sequence));
  check(
      expired->knowledge()->survey_commissions->version == 3 &&
          expired->knowledge()->survey_commissions->active->kind ==
              SurveyCommissionKind::geological,
      "Later geological acceptance must not downgrade a chosen-target ledger");
  for (unsigned tick = 0; tick < 1194; ++tick)
    need(session->advance_walk({0, -1, 0}));
  need(session->begin_boarding());
  for (SimulationTick tick = 0; tick < kGameplayBoardingTicks; ++tick)
    need(session->advance_walk({}));
  check(need(session->survey_commission_offers()).offer_count == 2,
        "Seated Origin must retain both read-only destination choices");
  checkpoint(*session, output, "seated", phases);
  for (const std::uint32_t carried : {1U, 2U, 3U}) {
    auto altered =
        std::make_unique<NativeSaveDocument>(need(session->save_document()));
    std::get<FreedomKnowledgeSaveDocument>(*altered)
        .voyage.resources.jump_charges = carried;
    auto budget = std::make_unique<NativeFreedomFlightSession>(
        need(NativeFreedomFlightSession::open(
            need(native_select_save_document(std::move(*altered))))));
    const auto plans = need(budget->survey_commission_offers());
    check(plans.offer_count == 2 &&
              plans.offers[1].route.jump_charges == carried &&
              plans.offers[1].route.charge_shortfall ==
                  (carried < 2 ? 2 - carried : 0) &&
              need(budget->survey_commission_view(SurveyCommissionKind::orbital,
                                                  waypoint))
                  .can_accept,
          "One/two/three carried charges change only the disclosed shortfall, "
          "not acceptance");
  }
  {
    // Initialized store deficit tests access, not flown consumption.
    auto depleted =
        std::make_unique<NativeSaveDocument>(need(session->save_document()));
    std::get<FreedomKnowledgeSaveDocument>(*depleted)
        .voyage.resources.jump_charges = 0;
    session = std::make_unique<NativeFreedomFlightSession>(
        need(NativeFreedomFlightSession::open(
            need(native_select_save_document(std::move(*depleted))))));
  }
  const auto empty_offer = need(session->survey_commission_offers());
  const auto empty_quote = need(
      session->survey_commission_view(SurveyCommissionKind::orbital, waypoint));
  check(empty_offer.offer_count == 2 &&
            empty_offer.offers[1].route.jump_charges == 0 &&
            empty_offer.offers[1].route.charge_shortfall == 2 &&
            empty_quote.can_accept,
        "Current charge shortfall must be disclosed without gating acceptance");
  checkpoint(*session, output, "zero-charge", phases);
  const auto before = bytes(*session);
  check(
      !session->accept_survey_commission_target(
          waypoint, empty_quote.offer_until - 1, empty_quote.offer_sequence) &&
          !session->accept_survey_commission_target(
              waypoint, empty_quote.offer_until,
              empty_quote.offer_sequence + 1) &&
          !session->accept_survey_commission_target(
              {waypoint.system, BodyId{1, BodyKind::moon, waypoint.body.value}},
              empty_quote.offer_until, empty_quote.offer_sequence) &&
          bytes(*session) == before,
      "Stale or foreign target-bound confirmations must leave the whole Save "
      "unchanged");
  need(session->accept_survey_commission_target(
      waypoint, empty_quote.offer_until, empty_quote.offer_sequence));
  check(need(session->survey_commission_view()) ==
                need(session->survey_commission_view(
                    SurveyCommissionKind::orbital, waypoint)) &&
            !session->survey_commission_view(SurveyCommissionKind::orbital,
                                             nearby),
        "Active default and matching-target readers must show identical "
        "accepted work");
  checkpoint(*session, output, "accepted", phases);
  {
    auto customer =
        std::make_unique<NativeFreedomFlightSession>(reopen(*session));
    const auto enrollment = need(customer->paid_operations_view());
    need(customer->enable_paid_operations(enrollment.token));
    // Explicit insolvent financial/store boundary, not earned voyage debt.
    auto poor =
        std::make_unique<NativeSaveDocument>(need(customer->save_document()));
    auto& knowledge = std::get<FreedomKnowledgeSaveDocument>(*poor);
    knowledge.knowledge.paid_operations->spent_credits = 200;
    knowledge.knowledge.paid_operations->operating_tab = 200;
    knowledge.voyage.resources.flight_quanta = 0;
    customer = std::make_unique<NativeFreedomFlightSession>(
        need(NativeFreedomFlightSession::open(
            need(native_select_save_document(std::move(*poor))))));
    const auto support = need(customer->paid_operations_view());
    check(support.can_retainer && support.cash == 0,
          "Zero-charge chosen-target work must retain the existing paid "
          "retainer");
    need(customer->request_paid_retainer(support.token));
    check(customer->resources()->jump_charges == 3 &&
              customer->knowledge()->survey_commissions ==
                  session->knowledge()->survey_commissions,
          "Paid support must preserve chosen terms and measurement history");
    (void)reopen(*customer);
  }
  need(session->replenish_resources());
  checkpoint(*session, output, "supported", phases);
  need(session->release_port());
  *session = home_space(std::move(*session));
  need(session->set_assistance(false));
  // The wrong commissioned body cannot acquire progress, even during real
  // ticks.
  need(session->advance({}));
  check(need(session->survey_commission_view()).acquired_ticks == 0,
        "Origin flight cannot remotely fulfill the Waypoint measurement");
  charged_leg(*session, waypoint.system);
  *session = measurement_orbit(*session);
  for (unsigned tick = 0; tick < 600; ++tick)
    need(session->advance({}));
  check(need(session->survey_commission_view()).acquired_ticks == 600,
        "Chosen destination must count exactly six hundred genuine intervals");
  checkpoint(*session, output, "partial", phases);
  auto peer = std::make_unique<NativeFreedomFlightSession>(reopen(*session));
  auto replacement =
      std::make_unique<NativeFreedomFlightSession>(recovered(reopen(*session)));
  check(
      !replacement->knowledge()->survey_commissions->active->measurement &&
          replacement->knowledge()->survey_commissions->active->target_policy ==
              SurveyCommissionTargetPolicy::charted_waypoint,
      "Replacement must clear unfinished old-craft work and retain chosen "
      "terms");
  for (unsigned tick = 600; tick < kSurveyCommissionMeasurementTicks; ++tick) {
    need(session->advance({}));
    need(peer->advance({}));
  }
  check(bytes(*session) == bytes(*peer) &&
            need(session->survey_commission_view()).completed &&
            !need(session->survey_commission_view()).can_submit,
        "Partial Continue must converge on fresh completion without remote "
        "payout");
  checkpoint(*session, output, "ready", phases);
  replacement =
      std::make_unique<NativeFreedomFlightSession>(recovered(reopen(*session)));
  check(replacement->knowledge()->survey_commissions->active ==
                session->knowledge()->survey_commissions->active &&
            need(replacement->survey_commission_view()).can_submit,
        "Completed chosen report must survive replacement with its original "
        "craft");
  need(replacement->submit_survey_commission(1));
  check(need(replacement->survey_commission_view()).earnings == 100,
        "Completed chosen report must remain claimable after recovery");
  const auto home = offers.offers[1].route.legs[1].destination;
  charged_leg(*session, home);
  *session = home_port(*session);
  checkpoint(*session, output, "origin", phases);
  need(session->submit_survey_commission(1));
  const auto paid = bytes(*session);
  check(need(session->survey_commission_view()).earnings == 100 &&
            !session->submit_survey_commission(1) && bytes(*session) == paid,
        "Chosen report must pay exactly once at the actual attached Origin "
        "owner");
  checkpoint(*session, output, "paid", phases);
  if (output) {
    const Json manifest{
        {"schema", "apsis.commission-destinations-scenario/1"},
        {"scope",
         "Actual station walk/boarding; explicit zero-charge and "
         "deadline-clock fixtures; actual acceptance, free service, two "
         "charged system jumps, 3600 qualifying intervals and capture/payment. "
         "Home-space, circular measurement orbit and final port pose are "
         "initialized at unchanged ticks, not a continuously flown route, "
         "profit, fuel or deadline viability claim."},
        {"nearby_system", std::to_string(nearby.system.value)},
        {"waypoint_system", std::to_string(waypoint.system.value)},
        {"waypoint_planet", std::to_string(waypoint.body.value)},
        {"phases", phases}};
    std::ofstream file{*output / "manifest.json"};
    file << manifest.dump(2) << '\n';
    check(static_cast<bool>(file), "Destination manifest write failed");
  }
}
auto run_native_survey_commission_scenario(
    const std::optional<std::filesystem::path>& output) -> void {
  run_commission_scenario(output, false);
  std::optional<std::filesystem::path> paid_output;
  if (output) {
    paid_output = *output / "paid-operations";
    std::filesystem::create_directories(*paid_output);
  }
  run_commission_scenario(paid_output, true);
  run_destination_scenario(output);
}

namespace paid_recovery_reference {
using Json = nlohmann::ordered_json;
unsigned checks{};
auto require(bool condition, const char* message) -> void {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) {
    if constexpr (std::is_same_v<E, std::string>)
      throw std::runtime_error(value.error());
    else
      throw std::runtime_error(value.error().detail);
  }
  if constexpr (!std::is_void_v<T>) {
    if constexpr (std::is_trivially_copyable_v<T>)
      return *value;
    else
      return std::move(*value);
  }
}
auto bytes(const NativeFreedomFlightSession& session) -> std::string {
  auto saved =
      std::make_unique<NativeSaveDocument>(need(session.save_document()));
  return need(encode_native_save_document_json(*saved));
}
auto retainer_quote(const NativePaidOperationsView& view) -> Json {
  return {{"can_retainer", view.can_retainer},
          {"service_credits", std::to_string(view.service_credits)},
          {"retainer_cash", std::to_string(view.retainer_cash)},
          {"retainer_tab", std::to_string(view.retainer_tab)},
          {"retainer_subsidy", std::to_string(view.retainer_subsidy)},
          {"retainer_berth", std::to_string(view.retainer_berth)},
          {"retainer_total", std::to_string(view.retainer_total)},
          {"terms", view.terms},
          {"note", view.note},
          {"funded", false}};
}
auto snapshot(const NativeFreedomFlightSession& session,
              const std::filesystem::path& folder, const std::string& name,
              Json& actions) -> Json {
  const auto before = bytes(session);
  const auto path = folder / (name + ".json");
  need(session.save_as(path));
  actions.push_back(
      {{"action", "save_as"}, {"file", path.filename().string()}});
  std::ifstream stream(path, std::ios::binary);
  const std::string written{std::istreambuf_iterator<char>{stream},
                            std::istreambuf_iterator<char>{}};
  require(written == before && bytes(session) == before,
          "Snapshot mutated complete Save");
  const auto paid = need(session.paid_operations_view());
  const auto view = need(session.survey_commission_view());
  const auto& resources = *session.resources();
  const auto& account = *session.knowledge()->paid_operations;
  const auto& work = *session.knowledge()->survey_commissions;
  require(
      view.earnings == 0 && work.paid_count == 0 &&
          work.geological_paid_count == 0 && !work.latest_paid && work.active &&
          (!work.active->measurement ||
           work.active->measurement->acquired_ticks == 0) &&
          view.acquired_ticks == 0 && !view.completed &&
          work.active->kind == SurveyCommissionKind::orbital &&
          account.settled_paid_count == 0 &&
          account.settled_specimen_count == 0 &&
          account.settled_canister_count == 0 && account.started_tick == 0 &&
          account.operating_tab == 0 && bytes(session) == before,
      "Read or replacement created earnings, another opening grant or "
      "operating debt");
  return {
      {"file", path.filename().string()},
      {"tick", std::to_string(session.document().flight.tick)},
      {"craft", std::to_string(session.document().origin.state.craft.value)},
      {"generation", session.document().origin.lineage
                         ? session.document().origin.lineage->generation
                         : 0},
      {"resource_craft", std::to_string(resources.craft.value)},
      {"resource_tick", std::to_string(resources.tick)},
      {"flight_quanta", std::to_string(resources.flight_quanta)},
      {"jump_charges", resources.jump_charges},
      {"cash", std::to_string(paid.cash)},
      {"insurance_due", std::to_string(paid.recovery_due)},
      {"operating_tab", std::to_string(paid.operating_tab)},
      {"spent_credits", std::to_string(account.spent_credits)},
      {"charged_generation", account.charged_generation},
      {"earnings", std::to_string(view.earnings)},
      {"commission",
       {{"sequence", work.active->sequence},
        {"next_sequence", work.next_sequence},
        {"system", std::to_string(work.active->system.value)},
        {"planet", std::to_string(work.active->planet.value)},
        {"accepted_tick", std::to_string(work.active->accepted_tick)},
        {"deadline_tick", std::to_string(work.active->deadline_tick)},
        {"kind", static_cast<unsigned>(work.active->kind)},
        {"paid_count", work.paid_count},
        {"acquired_ticks", view.acquired_ticks}}}};
}
auto run(const std::filesystem::path& folder) -> Json {
  Json result{
      {"schema", "apsis.paid-recovery-reference/1"},
      {"seed", "42"},
      {"completed", false},
      {"phase", "new-game"},
      {"completed_cycles", 0},
      {"actions", Json::array()},
      {"scope",
       "Debt/replacement composition only. Public New Game, accepted orbital "
       "work and one paid enrollment; real1194-tick walk, full "
       "boarding/release and120 ordinary manual actuator ticks per cycle. Each "
       "loss is an explicitly classified recoverable_destruction application "
       "event, not a physical collision. No initialized pose/resources/time, "
       "commission earnings, additional opening grant, funded retainer, "
       "route/profit/start-viability/manual or GPU claim."},
      {"inputs",
       {{"walk_controls", {0, -1, 0}},
        {"walk_ticks", 1194},
        {"boarding_ticks", kGameplayBoardingTicks},
        {"work_controls", {{"negative_translation_y", 0.25}}},
        {"work_ticks", 120},
        {"retainer_action", "query only; no demand or funding invocation"}}},
      {"cycles", Json::array()}};
  try {
    result["actions"].push_back(
        {{"action", "new_game"}, {"preset", "planetary"}, {"seed", "42"}});
    auto startup = std::make_unique<NativeStartup>(
        need(native_planetary_network_new_game({42})));
    auto live = std::make_unique<NativeFreedomFlightSession>(
        need(NativeFreedomFlightSession::open(std::move(*startup))));
    const auto first = need(live->survey_commission_view());
    need(live->accept_survey_commission(first.offer_until,
                                        first.offer_sequence));
    result["actions"].push_back(
        {{"action", "accept_survey_commission"},
         {"offer_until", std::to_string(first.offer_until)},
         {"sequence", first.offer_sequence}});
    const auto enrollment = need(live->paid_operations_view());
    need(live->enable_paid_operations(enrollment.token));
    result["actions"].push_back(
        {{"action", "enable_paid_operations"}, {"token", enrollment.token}});
    require(need(live->paid_operations_view()).cash == 180 &&
                live->document().flight.tick == 0,
            "Initial acceptance/enrollment changed opening180 or time");
    result["initial"] = snapshot(*live, folder, "initial", result["actions"]);
    constexpr std::array<std::uint64_t, 3> expected_cash{80, 0, 0};
    constexpr std::array<std::uint64_t, 3> expected_due{0, 20, 100};
    for (unsigned cycle = 1; cycle <= 3; ++cycle) {
      const auto prefix = "cycle-" + std::to_string(cycle);
      result["phase"] = prefix + "-walk";
      result["cycles"].push_back(
          {{"cycle", cycle},
           {"predeparture", snapshot(*live, folder, prefix + "-predeparture",
                                     result["actions"])}});
      auto& row = result["cycles"].back();
      const auto commission = *live->knowledge()->survey_commissions;
      const auto prior_craft = live->document().origin.state.craft;
      require(need(live->survey_commission_view()).at_origin &&
                  live->resources()->craft == prior_craft,
              "Next attempt is not at actual Origin with current stores owner");
      for (unsigned i = 0; i < 1194; ++i)
        need(live->advance_walk({0, -1, 0}));
      result["actions"].push_back({{"action", "advance_walk"},
                                   {"controls", {0, -1, 0}},
                                   {"ticks", 1194}});
      result["phase"] = prefix + "-board";
      need(live->begin_boarding());
      result["actions"].push_back({{"action", "begin_boarding"}});
      for (SimulationTick i = 0; i < kGameplayBoardingTicks; ++i)
        need(live->advance_walk({}));
      result["actions"].push_back({{"action", "advance_walk"},
                                   {"controls", {0, 0, 0}},
                                   {"ticks", kGameplayBoardingTicks}});
      need(live->release_port());
      result["actions"].push_back({{"action", "release_port"}});
      result["phase"] = prefix + "-actuate";
      const auto fuel_before = live->resources()->flight_quanta;
      NativeFlightControls controls;
      controls.negative_translation.y = 0.25;
      std::uint64_t fuel_debit{};
      unsigned applied{};
      for (unsigned i = 0; i < 120; ++i) {
        const auto step = need(live->advance(controls));
        fuel_debit += step.fuel_debit_quanta;
        const auto force =
            step.actuation.central.propulsion.applied_force_newtons;
        applied += force.x != 0 || force.y != 0 || force.z != 0;
      }
      result["actions"].push_back(
          {{"action", "advance"},
           {"controls", {0, 0, 0, 0, 0.25, 0, 0, 0, 0, 0, 0, 0}},
           {"ticks", 120}});
      require(applied > 0 && fuel_debit > 0 &&
                  live->resources()->flight_quanta < fuel_before &&
                  live->resources()->jump_charges == 3 &&
                  !live->recovery_pending(),
              "Ordinary actuator work did not apply/spend fuel or caused an "
              "unclassified loss");
      row["work"] = {{"fuel_debit_quanta", std::to_string(fuel_debit)},
                     {"applied_force_ticks", applied},
                     {"state", snapshot(*live, folder, prefix + "-worked",
                                        result["actions"])}};
      result["phase"] = prefix + "-record-loss";
      const auto loss_tick = live->document().flight.tick;
      require(loss_tick == cycle * SimulationTick{2754},
              "Reference changed its actual committed tick counts");
      auto recovery = std::make_unique<FreedomRecoverySaveDocument>(
          need(live->recovery_document()));
      const auto checksum =
          need(freedom_recovery_source_checksum(recovery->voyage));
      need(live->record_loss(FreedomLossCause::recoverable_destruction,
                             prior_craft, loss_tick, checksum));
      result["actions"].push_back(
          {{"action", "record_loss"},
           {"cause", "recoverable_destruction"},
           {"craft", std::to_string(prior_craft.value)},
           {"tick", std::to_string(loss_tick)},
           {"source_checksum", std::to_string(checksum)}});
      require(live->recovery_pending(),
              "Explicit classified loss did not freeze pending recovery");
      row["source_checksum"] = std::to_string(checksum);
      row["pending"] =
          snapshot(*live, folder, prefix + "-pending", result["actions"]);
      result["phase"] = prefix + "-replacement";
      need(live->complete_recovery());
      result["actions"].push_back({{"action", "complete_recovery"}});
      recovery = std::make_unique<FreedomRecoverySaveDocument>(
          need(live->recovery_document()));
      require(!live->recovery_pending() && recovery->recovery.latest &&
                  recovery_flight(recovery->recovery.latest->retired)
                          .origin.state.craft == prior_craft &&
                  recovery->recovery.latest->source_checksum == checksum &&
                  live->document().origin.state.craft != prior_craft &&
                  live->document().origin.lineage &&
                  live->document().origin.lineage->generation == cycle &&
                  live->resources()->craft ==
                      live->document().origin.state.craft &&
                  live->resources()->flight_quanta ==
                      kFreedomFlightCapacityQuanta &&
                  live->resources()->jump_charges == 3 &&
                  live->document().flight.tick == loss_tick &&
                  *live->knowledge()->survey_commissions == commission,
              "Replacement changed time/commission, kept retired craft, or "
              "invalidated full current stores");
      const auto paid = need(live->paid_operations_view());
      require(paid.cash == expected_cash[cycle - 1] &&
                  paid.recovery_due == expected_due[cycle - 1] &&
                  paid.operating_tab == 0,
              "Insurance-first repeated-loss accounting differs from180 "
              "initial cash/100 excess/cap100");
      row["retired_craft"] = std::to_string(prior_craft.value);
      row["replacement"] =
          snapshot(*live, folder, prefix + "-replacement", result["actions"]);
      const auto completed = bytes(*live);
      need(live->complete_recovery());
      result["actions"].push_back({{"action", "complete_recovery"}});
      require(bytes(*live) == completed,
              "Repeated complete_recovery changed full Save or charged again");
      row["repeat_recovery_save_neutral"] = true;
      result["phase"] = prefix + "-continue";
      const auto saved = folder / (prefix + "-replacement.json");
      auto continued = std::make_unique<NativeFreedomFlightSession>(
          need(NativeFreedomFlightSession::open(need(native_continue(saved)))));
      result["actions"].push_back(
          {{"action", "continue"}, {"file", saved.filename().string()}});
      require(bytes(*continued) == completed,
              "Replacement Save/Continue lost state");
      live.swap(continued);
      row["continued"] =
          snapshot(*live, folder, prefix + "-continued", result["actions"]);
      result["phase"] = prefix + "-next-work";
      need(live->abandon_survey_commission(commission.active->sequence));
      result["actions"].push_back({{"action", "abandon_survey_commission"},
                                   {"sequence", commission.active->sequence}});
      const auto fresh = need(live->survey_commission_view());
      require(fresh.at_origin && fresh.can_accept,
              "Actual Origin lacks a fresh unpaid next action");
      need(live->accept_survey_commission(fresh.offer_until,
                                          fresh.offer_sequence));
      result["actions"].push_back(
          {{"action", "accept_survey_commission"},
           {"offer_until", std::to_string(fresh.offer_until)},
           {"sequence", fresh.offer_sequence}});
      require(live->knowledge()->survey_commissions->active &&
                  !live->knowledge()->survey_commissions->active->measurement &&
                  live->knowledge()->survey_commissions->paid_count == 0 &&
                  need(live->paid_operations_view()).cash == paid.cash,
              "Abandon/reaccept created income or reused progress");
      row["next_attempt"] =
          snapshot(*live, folder, prefix + "-next-work", result["actions"]);
      const auto quote_before = bytes(*live);
      const auto quote = need(live->paid_operations_view());
      row["retainer_quote"] = retainer_quote(quote);
      result["actions"].push_back(
          {{"action", "query_retainer"}, {"funded", false}});
      require(bytes(*live) == quote_before,
              "Retainer quote mutated the full Save");
      // Full stores do not demand support eligibility. If support is offered
      // here, only the actual prepaid-berth extension needs funding.
      if (quote.can_retainer)
        require(quote.service_credits == 0 && quote.retainer_berth == 5 &&
                    quote.retainer_total == 5 && quote.retainer_tab == 5 &&
                    quote.retainer_cash == 0 && quote.retainer_subsidy == 0,
                "Observed full-store retainer quote is not the berth-only5 "
                "boundary");
      result["completed_cycles"] = cycle;
    }
    result["phase"] = "complete";
    result["completed"] = true;
  } catch (const std::exception& e) {
    result["error"] = e.what();
  }
  result["checks"] = checks;
  return result;
}

} // namespace paid_recovery_reference
auto run_native_paid_recovery_reference(
    const std::optional<std::filesystem::path>& output) -> void {
  const auto folder = output
                          ? *output / "paid-recovery-reference"
                          : std::filesystem::temp_directory_path() /
                                ("apsis-paid-recovery-reference-" +
                                 std::to_string(std::chrono::steady_clock::now()
                                                    .time_since_epoch()
                                                    .count()));
  if (output)
    std::filesystem::create_directories(folder);
  else if (!std::filesystem::create_directory(folder))
    throw std::runtime_error(
        "Could not create owned paid recovery temporary directory");
  paid_recovery_reference::checks = 0;
  const auto result = paid_recovery_reference::run(folder);
  {
    std::ofstream stream(folder / "manifest.json", std::ios::binary);
    stream << result.dump(2) << '\n';
    if (!stream)
      throw std::runtime_error(
          "Could not retain paid recovery reference manifest in " +
          folder.string());
  }
  if (!result["completed"].get<bool>())
    throw std::runtime_error("Paid recovery reference failed at " +
                             result["phase"].get<std::string>() + ": " +
                             result["error"].get<std::string>() +
                             "; retained evidence " + folder.string());
  std::cout << "Paid recovery reference: " << paid_recovery_reference::checks
            << " checks, 0 failures\n";
  if (!output) std::filesystem::remove_all(folder);
}
} // namespace apsis_drift
