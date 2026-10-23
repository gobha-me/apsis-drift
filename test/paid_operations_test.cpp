#include "apsis_drift/paid_operations.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace apsis_drift;
int checks{};
auto check(bool value, std::string_view message) -> void {
  ++checks;
  if (!value) throw std::runtime_error(std::string{message});
}
template <class T> auto need(std::expected<T, std::string> value) -> T {
  if (!value) throw std::runtime_error(value.error());
  return *value;
}
auto cash(const PaidOperationsAccount& account) -> std::uint64_t {
  return need(paid_operations_cash(account, account.settled_paid_count));
}
auto poor() -> PaidOperationsAccount {
  auto account = need(make_paid_operations(0, 0, 0));
  account = need(purchase_paid_service(account, 0, 0, 0, 85));
  return need(purchase_paid_service(account, 0, 0, 0, 85));
}
auto capped() -> PaidOperationsAccount {
  auto account = poor();
  for (std::uint32_t sequence = 1; sequence <= 3; ++sequence)
    account = need(fund_paid_retainer(account, 0, 0, 0, sequence,
                                      4 * kPaidHourTicks, 85))
                  .account;
  return account;
}
auto prices_and_atomic_purchase() -> void {
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  check(!paid_fuel_price(0, 0, 0) && !paid_fuel_price(101, 100, 0) &&
            !paid_fuel_price(0, 100, 4),
        "Invalid pool dimensions, deficits and charge bounds refuse first");
  check(need(paid_fuel_price(0, 100, 0)) == 0 &&
            need(paid_fuel_price(1, 100, 0)) == 1 &&
            need(paid_fuel_price(5, 100, 0)) == 2 &&
            need(paid_fuel_price(6, 100, 0)) == 3 &&
            need(paid_fuel_price(50, 100, 2)) == 50 &&
            need(paid_fuel_price(100, 100, 3)) == 85,
        "Ceiling quotes charge exact flight effort and explicit jump charges");
  check(need(paid_fuel_price(maximum, maximum, 0)) == 40 &&
            need(paid_fuel_price(maximum / 2, maximum, 0)) == 20 &&
            need(paid_fuel_price(maximum / 40 + 1, maximum, 0)) == 2 &&
            need(paid_fuel_price(1, maximum, 3)) == 46,
        "Full-width valid dimensions avoid intermediate multiplication "
        "overflow without losing the one-credit ceiling");
  // Conditional price illustration for the older qualified seed42/policy2
  // voyage deficits. This is not a commission fulfillment or profit claim.
  constexpr std::uint64_t capacity{3'032'640'000'000'000};
  check(need(paid_fuel_price(665'668'128'603'695, capacity, 2)) == 39 &&
            need(paid_fuel_price(653'802'092'271'333, capacity, 2)) == 39,
        "Older reference deficits would quote9flight+30jump at these rates");
  const auto opening = need(make_paid_operations(0, 0, 0));
  const auto once = need(purchase_paid_service(opening, 0, 0, 0, 85));
  const auto twice = need(purchase_paid_service(once, 0, 0, 0, 85));
  check(cash(opening) == 180 && cash(once) == 95 && cash(twice) == 10 &&
            !purchase_paid_service(twice, 0, 0, 0, 11) &&
            !purchase_paid_service(opening, 0, 0, 0, 86) &&
            !purchase_paid_service(opening, 0, 0, 0, maximum) &&
            need(purchase_paid_service(opening, 0, 0, 0, 0)) == opening &&
            opening.spent_credits == 20,
        "Purchases are cash-only, bounded and leave their input unchanged "
        "on refusal; full stores do not invent a charge");
}
auto billing_and_validation() -> void {
  constexpr auto hour = kPaidHourTicks;
  const auto enrolled = need(make_paid_operations(3, 100, 7));
  check(cash(enrolled) == 480 && enrolled.started_tick == 100 &&
            enrolled.billed_through_hours == 4 &&
            enrolled.charged_generation == 7 && enrolled.recovery_due == 0 &&
            enrolled.operating_tab == 0,
        "Enrollment carries past earned gross, prepays20 once and charges "
        "no historical replacement fee");
  const auto initial = need(make_paid_operations(0, 100, 0));
  check(need(reconcile_paid_berth(initial, 0, 100 + 4 * hour - 1, 0)) ==
            initial,
        "Initial four-hour coverage is exclusive at its final endpoint");
  const auto edge = need(reconcile_paid_berth(initial, 0, 100 + 4 * hour, 0));
  check(edge.billed_through_hours == 5 && cash(edge) == 175 &&
            need(reconcile_paid_berth(edge, 0, 100 + 4 * hour, 0)) == edge,
        "Equality posts exactly the next lodging-and-parking bill once");
  auto stepped = initial;
  for (SimulationTick i = 4; i <= 10; ++i)
    stepped = need(reconcile_paid_berth(stepped, 0, 100 + i * hour, 0));
  const auto delayed =
      need(reconcile_paid_berth(initial, 0, 100 + 10 * hour, 0));
  check(delayed == stepped && delayed.billed_through_hours == 11 &&
            cash(delayed) == 145 && cash(initial) == 180,
        "Delayed O(1) posting and frequent effective views agree without "
        "mutating the saved account or charging poll order");
  const auto after_view =
      need(purchase_paid_service(delayed, 0, 100 + 10 * hour, 0, 85));
  check(after_view ==
            need(purchase_paid_service(initial, 0, 100 + 10 * hour, 0, 85)),
        "A read-only reconciled preview cannot alter the eventual purchase");
  const auto long_wait = need(reconcile_paid_berth(
      initial, 0, 100 + SimulationTick{1'000'000'000} * hour, 0));
  check(long_wait.billed_through_hours == 1'000'000'001 &&
            cash(long_wait) == 0 && long_wait.spent_credits == 200 &&
            long_wait.operating_tab == 200 && long_wait.recovery_due == 0 &&
            need(reconcile_paid_berth(
                long_wait, 0, 100 + SimulationTick{1'000'000'000} * hour, 0)) ==
                long_wait,
        "Very late bills consume finite cash, cap operating debt, waive "
        "the rest and never create insurance or repeated charges");
  for (unsigned fault = 0; fault < 9; ++fault) {
    auto invalid = initial;
    switch (fault) {
      case 0: invalid.version = 2; break;
      case 1: invalid.spent_credits = 19; break;
      case 2: invalid.spent_credits = 201; break;
      case 3: invalid.operating_tab = 201; break;
      case 4: invalid.recovery_due = 101; break;
      case 5: invalid.billed_through_hours = 0; break;
      case 6: invalid.billed_through_hours = 6; break;
      case 7: invalid.settled_paid_count = 1; break;
      case 8: invalid.started_tick = 101; break;
      default: break;
    }
    check(!validate_paid_operations(invalid, 0, 100, 0) &&
              !reconcile_paid_berth(invalid, 0, 100, 0),
          "Malformed versions, cash/debt/counts, future coverage and clocks "
          "refuse before reconciliation");
  }
  constexpr auto maximum = std::numeric_limits<SimulationTick>::max();
  check(!validate_paid_operations(initial, 0, 100, 1) &&
            !validate_paid_operations(initial, 1, 100, 0) &&
            !validate_paid_operations(initial, 0, 99, 0) &&
            !make_paid_operations(0, maximum, 0) &&
            !make_paid_operations(0, 0, maximum) &&
            !make_paid_operations(0, maximum - 4 * hour + 1, 0) &&
            make_paid_operations(0, maximum - 4 * hour, 0).has_value() &&
            !reconcile_paid_berth(need(make_paid_operations(0, 0, 0)), 0,
                                  maximum - 1, 0),
        "Count/generation joins, rewind, final ticks and unrepresentable "
        "coverage refuse instead of wrapping money or clocks");
}
auto operating_support() -> void {
  const auto opening = need(make_paid_operations(0, 0, 0));
  auto account = poor();
  check(!fund_paid_retainer(opening, 0, 0, 0, 1, 4 * kPaidHourTicks, 85) &&
            !fund_paid_retainer(account, 0, 0, 0, 1, 4 * kPaidHourTicks, 0) &&
            !fund_paid_retainer(account, 0, 0, 0, 0, 4 * kPaidHourTicks, 85) &&
            !fund_paid_retainer(account, 0, 0, 0, 1, 0, 85) &&
            !fund_paid_retainer(account, 0, 0, 0, 1, 4 * kPaidHourTicks + 1,
                                85) &&
            !fund_paid_retainer(account, 0, 0, 0, 1, 4 * kPaidHourTicks, 86),
        "Retainer refuses affordable/empty quotes, zero sequence, expiry, "
        "oversized reservation and service bounds");
  const auto first =
      need(fund_paid_retainer(account, 0, 0, 0, 1, 4 * kPaidHourTicks, 85));
  check(first.service_credits == 85 && first.berth_credits == 0 &&
            first.cash_debit == 10 && first.tab_added == 75 &&
            first.subsidized_credits == 0 && cash(first.account) == 0 &&
            first.account.operating_tab == 75 &&
            first.account.supported_sequence == 1 &&
            !fund_paid_retainer(first.account, 0, 0, 0, 1, 4 * kPaidHourTicks,
                                85),
        "Fresh work spends remaining cash then books only the actual "
        "noncash operating deficit, once per sequence");
  account = first.account;
  const auto second =
      need(fund_paid_retainer(account, 0, 0, 0, 2, 4 * kPaidHourTicks, 85));
  const auto third = need(
      fund_paid_retainer(second.account, 0, 0, 0, 3, 4 * kPaidHourTicks, 85));
  const auto fourth = need(
      fund_paid_retainer(third.account, 0, 0, 0, 4, 4 * kPaidHourTicks, 85));
  check(second.account.operating_tab == 160 &&
            third.account.operating_tab == 200 && third.tab_added == 40 &&
            third.subsidized_credits == 45 && fourth.tab_added == 0 &&
            fourth.subsidized_credits == 85 &&
            fourth.account.operating_tab == 200 && cash(fourth.account) == 0,
        "Repeated funded failures reach the cap then explicit subsidy "
        "without giving cash or blocking the next supported attempt");
  const auto fractional =
      need(fund_paid_retainer(poor(), 0, kPaidHourTicks / 2, 0, 1,
                              kPaidHourTicks / 2 + 4 * kPaidHourTicks, 85));
  check(fractional.account.billed_through_hours == 5 &&
            fractional.berth_credits == 5 && fractional.tab_added == 80,
        "Retainer prepays the ceiling hour through its actual deadline");
  const auto with_due = need(fund_paid_retainer(
      poor(), 0, 4 * kPaidHourTicks + 1, 0, 1, 8 * kPaidHourTicks + 1, 85));
  check(with_due.account.billed_through_hours == 9 &&
            with_due.berth_credits == 20 && with_due.cash_debit == 5 &&
            with_due.tab_added == 100 && with_due.account.spent_credits == 200,
        "Ordinary due billing happens before retainer cost breakdown, "
        "and existing posted coverage is never prepaid twice");
}
auto income_and_replacement() -> void {
  auto priority = capped();
  // Labelled valid financial inputs for exact priority/rounding boundaries;
  // no claim these balances came from a physical loss or fulfilled mission.
  priority.operating_tab = 80;
  priority.recovery_due = 30;
  const auto settled = need(settle_paid_survey_income(priority, 1, 0, 0));
  check(settled.recovery_due == 0 && settled.operating_tab == 45 &&
            cash(settled) == 35 && settled.spent_credits == 265 &&
            !settle_paid_survey_income(settled, 1, 0, 0) &&
            !settle_paid_survey_income(priority, 2, 0, 0),
        "Exactly one new100 pays insurance30 first, then half the remaining "
        "70 toward operating debt, preserving35cash");
  priority.operating_tab = 200;
  priority.recovery_due = 1;
  const auto odd = need(settle_paid_survey_income(priority, 1, 0, 0));
  check(
      odd.operating_tab == 151 && cash(odd) == 50,
      "Half-of-remainder repayment rounds down rather than taking extra cash");
  const auto opening = need(make_paid_operations(0, 0, 0));
  check(need(charge_paid_recovery(opening, 0, 10 * kPaidHourTicks, 0)) ==
            opening,
        "Matching generation is unchanged even when later berth bills are due");
  const auto charged =
      need(charge_paid_recovery(opening, 0, 10 * kPaidHourTicks, 1));
  check(charged.billed_through_hours == 11 && charged.spent_credits == 155 &&
            charged.charged_generation == 1 && cash(charged) == 45 &&
            !charge_paid_recovery(opening, 0, 10 * kPaidHourTicks, 2) &&
            !charge_paid_recovery(charged, 0, 10 * kPaidHourTicks, 0) &&
            need(charge_paid_recovery(charged, 0, 20 * kPaidHourTicks, 1)) ==
                charged,
        "Only a successive replacement reconciles old-generation bills and "
        "charges one cash-first excess; replay/skip/rewind cannot add fees");
  const auto split = need(charge_paid_recovery(poor(), 0, 0, 1));
  const auto split_income = need(settle_paid_survey_income(split, 1, 0, 1));
  check(cash(split) == 0 && split.recovery_due == 90 &&
            split.spent_credits == 200 && split.operating_tab == 0 &&
            cash(split_income) == 10 && split_income.recovery_due == 0 &&
            need(charge_paid_recovery(split, 0, 0, 2)).recovery_due == 100,
        "A partial cash fee leaves only its unpaid90 as insurance; retained "
        "income restores no net wealth and repeated unpaid losses cap at100");
  auto account = capped();
  for (std::uint32_t count = 1; count <= 8; ++count) {
    account = need(charge_paid_recovery(account, count - 1, 0, count));
    check(account.recovery_due == 100 && cash(account) == 0 &&
              account.operating_tab == 200,
          "Successive-generation fee with empty cash creates excess outside "
          "the already capped operating tab");
    const auto funded = need(fund_paid_retainer(
        account, count - 1, 0, count, count + 3, 4 * kPaidHourTicks, 85));
    check(funded.subsidized_credits == 85 && funded.account.recovery_due == 100,
          "Operating hardship never pays or subsidizes insurance excess");
    account = need(settle_paid_survey_income(funded.account, count, 0, count));
    check(cash(account) == 0 && account.recovery_due == 0 &&
              account.operating_tab == 200 &&
              account.settled_paid_count == count,
          "Repeated replacement plus recovered100report produces no new cash "
          "even with hardship already at its cap");
  }
  auto delayed = capped();
  delayed.operating_tab = 0;
  delayed.recovery_due = 100;
  const auto after_bills =
      need(settle_paid_survey_income(delayed, 1, 100 * kPaidHourTicks, 0));
  check(after_bills.operating_tab == 200 && after_bills.recovery_due == 0 &&
            cash(after_bills) == 0 && after_bills.spent_credits == 300,
        "Late rent uses OLD gross and hardship before the new payout, "
        "so bills cannot divert insurance's first claim on income");
  constexpr auto count_max = std::numeric_limits<std::uint32_t>::max();
  const auto exhausted = need(make_paid_operations(count_max, 0, 0));
  check(cash(exhausted) == 429'496'729'680ULL &&
            !settle_paid_survey_income(exhausted, 0, 0, 0),
        "Last cumulative count has exact uint64 cash and refuses payout wrap");
  constexpr auto generation_max = std::numeric_limits<std::uint64_t>::max();
  const auto last = need(make_paid_operations(0, 0, generation_max - 1));
  check(!charge_paid_recovery(last, 0, 0, generation_max) &&
            !charge_paid_recovery(last, 0, 0, 0),
        "Recovery refuses unrepresentable or wrapped craft generations");
}
} // namespace
auto main() -> int {
  try {
    prices_and_atomic_purchase();
    billing_and_validation();
    operating_support();
    income_and_replacement();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << checks << " paid operations checks, 0 failures\n";
  return 0;
}
