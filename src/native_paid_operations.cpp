#include "apsis_drift/native_flight_session.hpp"

#include "apsis_drift/save_file.hpp"

#include <limits>

namespace apsis_drift {
static_assert(kPaidSurveyRewardCredits == kSurveyCommissionRewardCredits);

auto NativeFreedomFlightSession::survey_paid_count() const -> std::uint32_t {
  return knowledge_ && knowledge_->survey_commissions
             ? knowledge_->survey_commissions->paid_count
             : 0;
}
auto NativeFreedomFlightSession::paid_operations_generation() const
    -> std::uint64_t {
  return document_.origin.lineage ? document_.origin.lineage->generation : 0;
}
auto NativeFreedomFlightSession::paid_operations_origin_available() const
    -> bool {
  if (!knowledge_ || !resources_ ||
      !freedom_knowledge_has_planetary_chart(knowledge_->recipe.version) ||
      recovery_pending() || !docking_ || !docking_->attached || moon_ || minor_ ||
      document_.flight.craft != CraftFrameRecipe{kWayfarerFrameId, 1} ||
      (boarding_ && boarding_->phase != FreedomBoardingPhase::station &&
       boarding_->phase != FreedomBoardingPhase::seated))
    return false;
  return system_.catalog.id ==
         generate_first_intersystem_identities(
             document_.origin.recipe.universe_seed)
             .origin_system;
}
auto NativeFreedomFlightSession::effective_paid_operations_account() const
    -> std::expected<PaidOperationsAccount, std::string> {
  if (!knowledge_ || !knowledge_->paid_operations)
    return std::unexpected{"Paid operations are not selected"};
  return reconcile_paid_berth(*knowledge_->paid_operations, survey_paid_count(),
                               document_.flight.tick,
                               paid_operations_generation());
}
auto NativeFreedomFlightSession::paid_operations_token() const -> std::string {
  if (!knowledge_ || !resources_) return {};
  std::string token{"paid1"};
  const auto word = [&](std::uint64_t value) {
    token += ':';
    token += std::to_string(value);
  };
  for (const auto value :
       {document_.origin.recipe.universe_seed.value,
        document_.origin.state.craft.value, document_.flight.tick,
        resources_->flight_quanta,
        static_cast<std::uint64_t>(resources_->jump_charges),
        static_cast<std::uint64_t>(survey_paid_count()),
        paid_operations_generation()})
    word(value);
  word(knowledge_->paid_operations.has_value());
  if (knowledge_->paid_operations) {
    const auto& account = *knowledge_->paid_operations;
    for (const auto value :
         {account.started_tick, account.billed_through_hours,
          account.spent_credits, account.operating_tab, account.recovery_due,
          static_cast<std::uint64_t>(account.settled_paid_count),
          static_cast<std::uint64_t>(account.supported_sequence),
          account.charged_generation})
      word(value);
  }
  if (knowledge_->survey_commissions && knowledge_->survey_commissions->active) {
    const auto& work = *knowledge_->survey_commissions->active;
    word(work.sequence);
    word(work.deadline_tick);
    word(work.measurement ? work.measurement->acquired_ticks : 0);
  } else {
    word(0);
  }
  return token;
}
auto NativeFreedomFlightSession::paid_operations_view() const
    -> std::expected<NativePaidOperationsView, std::string> {
  NativePaidOperationsView view;
  view.tick = document_.flight.tick;
  view.terms =
      "Optional paid operating prototype. Enroll once at Origin: a 200-credit "
      "opening budget includes four prepaid hours of lodging and parking "
      "for 20 credits; prior survey earnings are retained. Flight fuel "
      "costs up to 40 credits per full tank; jump charges cost 15 each. "
      "One berth bill costs 5 credits per game hour. Menus and offline time "
      "do not advance that clock. Each replacement charges a 100-credit "
      "insurance excess; unpaid excess is capped at 100 and settles before "
      "other repayment. Completed reports survive. Operating support uses "
      "cash first, then a noncompounding tab capped at 200; further necessary "
      "support is hardship subsidy. Half of report income remaining after "
      "insurance repays the tab. No cash advances, interest or seizure of "
      "your only ship. Enrollment stays selected in this saved journey; "
      "loading an older save rewinds the whole account. Prices are provisional.";
  view.note = "Paid operations require a planetary-chart Wayfarer journey.";
  if (!knowledge_ || !resources_ ||
      !freedom_knowledge_has_planetary_chart(knowledge_->recipe.version) ||
      document_.flight.craft != CraftFrameRecipe{kWayfarerFrameId, 1})
    return view;
  view.supported = true;
  view.at_origin = paid_operations_origin_available();
  view.selected = knowledge_->paid_operations.has_value();
  view.capacity_quanta = kFreedomFlightCapacityQuanta;
  if (resources_->flight_quanta > view.capacity_quanta ||
      resources_->jump_charges > kFreedomJumpCapacity)
    return std::unexpected{"Current starter stores are invalid"};
  view.missing_flight_quanta = view.capacity_quanta - resources_->flight_quanta;
  view.missing_jump_charges = kFreedomJumpCapacity - resources_->jump_charges;
  const auto flight = paid_fuel_price(view.missing_flight_quanta,
                                      view.capacity_quanta, 0);
  const auto jumps = paid_fuel_price(0, view.capacity_quanta,
                                     view.missing_jump_charges);
  if (!flight || !jumps)
    return std::unexpected{"Current fuel quote is unavailable"};
  view.flight_credits = *flight;
  view.jump_credits = *jumps;
  view.service_credits = *flight + *jumps;
  view.token = paid_operations_token();
  const auto count = survey_paid_count();
  const auto generation = paid_operations_generation();
  if (!view.selected) {
    const auto initial = make_paid_operations(count, view.tick, generation);
    if (initial) {
      const auto cash = paid_operations_cash(*initial, count);
      if (!cash) return std::unexpected{cash.error()};
      view.opening_cash = *cash;
      view.can_enable = view.at_origin;
    }
    view.note = view.at_origin
                    ? "Review the terms before enabling paid operations. "
                      "Your current technical fuel service is still free."
                    : "Return and attach at Origin to select this prototype.";
    return view;
  }
  const auto account = effective_paid_operations_account();
  if (!account) return std::unexpected{account.error()};
  const auto cash = paid_operations_cash(*account, count);
  if (!cash) return std::unexpected{cash.error()};
  view.cash = *cash;
  view.operating_tab = account->operating_tab;
  view.recovery_due = account->recovery_due;
  view.started_tick = account->started_tick;
  if (account->billed_through_hours >
      (std::numeric_limits<SimulationTick>::max() - account->started_tick) /
          kPaidHourTicks)
    return std::unexpected{"Berth coverage exceeds the supported clock"};
  view.berth_until_tick =
      account->started_tick + account->billed_through_hours * kPaidHourTicks;
  view.can_purchase =
      view.at_origin && view.service_credits != 0 &&
      purchase_paid_service(*knowledge_->paid_operations, count, view.tick,
                             generation, view.service_credits)
          .has_value();
  if (view.at_origin && knowledge_->survey_commissions &&
      knowledge_->survey_commissions->active) {
    const auto& work = *knowledge_->survey_commissions->active;
    if ((!work.measurement || work.measurement->acquired_ticks == 0) &&
        view.tick < work.deadline_tick) {
      const auto supported = fund_paid_retainer(
          *knowledge_->paid_operations, count, view.tick, generation,
          work.sequence, work.deadline_tick, view.service_credits);
      if (supported) {
        view.can_retainer = true;
        view.retainer_cash = supported->cash_debit;
        view.retainer_tab = supported->tab_added;
        view.retainer_subsidy = supported->subsidized_credits;
        view.retainer_berth = supported->berth_credits;
        view.retainer_total =
            supported->service_credits + supported->berth_credits;
      }
    }
  }
  view.note =
      !view.at_origin
          ? "Fuel service and operating retainers require physical Origin "
            "attachment. A living pilot stranded elsewhere needs actual help."
      : view.can_retainer
          ? "A fresh survey operating retainer can fund these stores and "
            "berth coverage. It supplies resources, never cash. Review the "
            "cash, tab and subsidy amounts before accepting."
      : view.service_credits == 0
          ? "Starter stores are full. Berth charges use game time; reading "
            "this page neither posts bills nor refreshes coverage."
      : !view.can_purchase
          ? "Cash does not cover full restoration. Accept fresh survey work "
            "at Origin to review an eligible noncash operating retainer."
          : "Confirm the full fuel quote to debit cash and restore the "
            "actual starter stores together.";
  return view;
}

auto NativeFreedomFlightSession::enable_paid_operations(std::string_view token)
    -> std::expected<void, std::string> {
  const auto view = paid_operations_view();
  if (!view) return std::unexpected{view.error()};
  if (!view->can_enable || token != view->token)
    return std::unexpected{"Review current paid operating terms at Origin"};
  const auto account = make_paid_operations(
      survey_paid_count(), document_.flight.tick, paid_operations_generation());
  if (!account) return std::unexpected{account.error()};
  auto candidate = *this;
  candidate.knowledge_->paid_operations = *account;
  const auto saved = candidate.save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::purchase_paid_fuel(std::string_view token)
    -> std::expected<void, std::string> {
  const auto view = paid_operations_view();
  if (!view) return std::unexpected{view.error()};
  if (!view->can_purchase || token != view->token)
    return std::unexpected{"Review a current affordable fuel quote at Origin"};
  const auto paid = purchase_paid_service(
      *knowledge_->paid_operations, survey_paid_count(), document_.flight.tick,
      paid_operations_generation(), view->service_credits);
  if (!paid) return std::unexpected{paid.error()};
  auto candidate = *this;
  const auto fueled = replenish_freedom_resources(
      *candidate.resources_, {candidate.document_, *candidate.docking_});
  if (!fueled) return std::unexpected{"Actual attached fuel service refused"};
  candidate.knowledge_->paid_operations = *paid;
  candidate.resources_ = *fueled;
  const auto saved = candidate.save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::request_paid_retainer(std::string_view token)
    -> std::expected<void, std::string> {
  const auto view = paid_operations_view();
  if (!view) return std::unexpected{view.error()};
  if (!view->can_retainer || token != view->token)
    return std::unexpected{"Review an eligible fresh-work retainer at Origin"};
  const auto& work = *knowledge_->survey_commissions->active;
  const auto supported = fund_paid_retainer(
      *knowledge_->paid_operations, survey_paid_count(), document_.flight.tick,
      paid_operations_generation(), work.sequence, work.deadline_tick,
      view->service_credits);
  if (!supported) return std::unexpected{supported.error()};
  auto candidate = *this;
  const auto fueled = replenish_freedom_resources(
      *candidate.resources_, {candidate.document_, *candidate.docking_});
  if (!fueled) return std::unexpected{"Actual retained-work service refused"};
  candidate.knowledge_->paid_operations = supported->account;
  candidate.resources_ = *fueled;
  const auto saved = candidate.save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}
} // namespace apsis_drift
