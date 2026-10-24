#include "apsis_drift/paid_operations.hpp"

#include <iostream>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string_view>

namespace {
using namespace apsis_drift;
unsigned checks{};
auto check(bool ok, std::string_view message) -> void {
  ++checks;
  if (!ok) throw std::runtime_error(std::string{message});
}
template <class T>
auto need(std::expected<T, std::string> value,
          std::source_location at = std::source_location::current()) -> T {
  if (!value)
    throw std::runtime_error(std::to_string(at.line()) + ": " + value.error());
  return *value;
}
auto counts(const PaidOperationsAccount& account) -> PaidIncomeCounts {
  return {account.settled_paid_count, account.settled_specimen_count,
          account.settled_canister_count};
}
auto cash(const PaidOperationsAccount& account) -> std::uint64_t {
  return need(paid_operations_cash(account, counts(account)));
}
auto versions_and_prior_income() -> void {
  const auto legacy = need(make_paid_operations(2U, 100, 7));
  const auto typed =
      need(make_paid_operations(PaidIncomeCounts{2, 0, 0}, 100, 7));
  check(legacy == typed && typed.version == 1 &&
            typed.settled_canister_count == 0 && cash(typed) == 380,
        "An absent canister source changed historical enrollment");
  check(
      need(purchase_paid_service(legacy, 2U, 100, 7, 85)) ==
              need(purchase_paid_service(typed, PaidIncomeCounts{2, 0, 0}, 100,
                                         7, 85)) &&
          need(reconcile_paid_berth(legacy, 2U, 100 + 4 * kPaidHourTicks, 7)) ==
              need(reconcile_paid_berth(typed, PaidIncomeCounts{2, 0, 0},
                                        100 + 4 * kPaidHourTicks, 7)) &&
          need(settle_paid_survey_income(legacy, 3U, 100, 7)) ==
              need(settle_paid_survey_income(typed, PaidIncomeCounts{3, 0, 0},
                                             100, 7)) &&
          need(charge_paid_recovery(legacy, 2U, 100, 8)) ==
              need(charge_paid_recovery(typed, PaidIncomeCounts{2, 0, 0}, 100,
                                        8)),
      "Zero-canister typed service, billing, survey and replacement differ "
      "from old APIs");
  const auto specimen =
      need(make_paid_operations(PaidIncomeCounts{2, 3, 0}, 100, 7));
  check(specimen.version == 2 && specimen.settled_specimen_count == 3 &&
            specimen.settled_canister_count == 0 && cash(specimen) == 500,
        "Existing specimen earnings selected version3 without a canister sale");
  const auto enrolled =
      need(make_paid_operations(PaidIncomeCounts{2, 3, 1}, 100, 7));
  check(enrolled.version == 3 && enrolled.started_tick == 100 &&
            enrolled.billed_through_hours == 4 &&
            enrolled.spent_credits == 20 && enrolled.settled_paid_count == 2 &&
            enrolled.settled_specimen_count == 3 &&
            enrolled.settled_canister_count == 1 &&
            enrolled.charged_generation == 7 && enrolled.operating_tab == 0 &&
            enrolled.recovery_due == 0 && cash(enrolled) == 600,
        "Prior-sale enrollment must add200 opening plus200survey "
        "plus120specimens plus100canister minus20berth once");
  check(!paid_operations_cash(enrolled, 2U) &&
            !validate_paid_operations(enrolled, 2U, 100, 7) &&
            !reconcile_paid_berth(enrolled, 2U, 100, 7) &&
            !purchase_paid_service(enrolled, 2U, 100, 7, 0) &&
            !fund_paid_retainer(enrolled, 2U, 100, 7, 1,
                                100 + 4 * kPaidHourTicks, 85) &&
            !settle_paid_survey_income(enrolled, 3U, 100, 7) &&
            !charge_paid_recovery(enrolled, 2U, 100, 7),
        "Legacy overload discarded version3 income instead of refusing");
}
auto promotions_and_services() -> void {
  const auto original = need(make_paid_operations(PaidIncomeCounts{}, 0, 0));
  const auto sale =
      need(settle_paid_canister_income(original, {0, 0, 1}, 0, 0));
  check(original.version == 1 && cash(original) == 180 && sale.version == 3 &&
            sale.spent_credits == 20 && sale.settled_canister_count == 1 &&
            cash(sale) == 280,
        "An actual first canister sale must earn100 without a second200 "
        "opening grant");
  const auto with_specimen =
      need(make_paid_operations(PaidIncomeCounts{0, 1, 0}, 100, 4));
  const auto second =
      need(settle_paid_canister_income(with_specimen, {0, 1, 1}, 100, 4));
  check(second.version == 3 && second.started_tick == 100 &&
            second.spent_credits == 20 && second.charged_generation == 4 &&
            second.settled_specimen_count == 1 && cash(second) == 320 &&
            with_specimen.version == 2 && cash(with_specimen) == 220,
        "Version2 promotion dropped specimen earnings or reset an account "
        "anchor");
  const auto survey =
      need(settle_paid_survey_income(second, {1, 1, 1}, 100, 4));
  const auto specimens =
      need(settle_paid_specimen_income(survey, {1, 3, 1}, 100, 4, 2));
  check(survey.version == 3 && cash(survey) == 420 && specimens.version == 3 &&
            cash(specimens) == 500 && specimens.settled_paid_count == 1 &&
            specimens.settled_specimen_count == 3 &&
            specimens.settled_canister_count == 1,
        "Later survey/specimen settlements demoted version3 or lost the finite "
        "sale");
  const auto bought = need(purchase_paid_service(sale, {0, 0, 1}, 0, 0, 85));
  const auto billed =
      need(reconcile_paid_berth(bought, {0, 0, 1}, 4 * kPaidHourTicks, 0));
  const auto recovered =
      need(charge_paid_recovery(billed, {0, 0, 1}, 4 * kPaidHourTicks, 1));
  check(cash(bought) == 195 && cash(billed) == 190 &&
            billed.billed_through_hours == 5 && cash(recovered) == 90 &&
            recovered.recovery_due == 0 && recovered.version == 3 &&
            recovered.settled_canister_count == 1 &&
            recovered.charged_generation == 1 &&
            need(charge_paid_recovery(recovered, {0, 0, 1}, 20 * kPaidHourTicks,
                                      1)) == recovered,
        "Version3 changed85 service,5 hourly berth,100 replacement or "
        "same-generation idempotence");
  check(need(paid_fuel_price(100, 100, 3)) == 85 &&
            need(paid_fuel_price(std::numeric_limits<std::uint64_t>::max(),
                                 std::numeric_limits<std::uint64_t>::max(),
                                 3)) == 85 &&
            !paid_fuel_price(1, 0, 0) && !paid_fuel_price(101, 100, 0) &&
            !paid_fuel_price(0, 100, 4),
        "Canister income changed existing bounded fuel prices or widened "
        "invalid dimensions");
}
auto insurance_then_tab() -> void {
  // Actual pure financial operations only. These are not flown consumption,
  // physical replacement, delivered service or profitable voyage evidence.
  auto source = need(make_paid_operations(PaidIncomeCounts{}, 0, 0));
  source = need(purchase_paid_service(source, PaidIncomeCounts{}, 0, 0, 85));
  source = need(purchase_paid_service(source, PaidIncomeCounts{}, 0, 0, 55));
  check(cash(source) == 40 && source.spent_credits == 160,
        "Two bounded cash purchases failed independent180-85-55 arithmetic");
  source = need(charge_paid_recovery(source, PaidIncomeCounts{}, 0, 1));
  check(cash(source) == 0 && source.recovery_due == 60 &&
            source.spent_credits == 200,
        "Recovery must consume40 available cash and retain60 insurance due");
  const auto support = need(fund_paid_retainer(source, PaidIncomeCounts{}, 0, 1,
                                               1, 4 * kPaidHourTicks, 85));
  check(support.cash_debit == 0 && support.tab_added == 85 &&
            support.subsidized_credits == 0 && support.berth_credits == 0 &&
            support.account.operating_tab == 85 &&
            support.account.recovery_due == 60 && cash(support.account) == 0,
        "Noncash retainer funded insurance or added cash instead of85 "
        "operating tab");
  const auto paid =
      need(settle_paid_canister_income(support.account, {0, 0, 1}, 0, 1));
  check(paid.version == 3 && paid.recovery_due == 0 &&
            paid.operating_tab == 65 && paid.spent_credits == 280 &&
            cash(paid) == 20 && paid.supported_sequence == 1 &&
            paid.started_tick == 0 && paid.charged_generation == 1 &&
            paid.settled_canister_count == 1,
        "Sale100 must pay insurance60, then tab20, leaving20cash and65tab");
  const auto charged = need(charge_paid_recovery(paid, {0, 0, 1}, 0, 2));
  const auto next_report =
      need(settle_paid_survey_income(charged, {1, 0, 1}, 0, 2));
  check(charged.recovery_due == 80 && charged.operating_tab == 65 &&
            cash(charged) == 0 && next_report.recovery_due == 0 &&
            next_report.operating_tab == 55 && cash(next_report) == 10 &&
            next_report.settled_canister_count == 1 &&
            !settle_paid_canister_income(charged, {0, 0, 1}, 0, 2),
        "Later replacement/report reset income, repeated finite sale or "
        "bypassed insurance priority");
}
auto full_insurance_and_support_cap() -> void {
  auto source = need(make_paid_operations(PaidIncomeCounts{}, 0, 0));
  for (auto price : {85U, 85U, 10U})
    source =
        need(purchase_paid_service(source, PaidIncomeCounts{}, 0, 0, price));
  source = need(charge_paid_recovery(source, PaidIncomeCounts{}, 0, 1));
  const auto support = need(fund_paid_retainer(source, PaidIncomeCounts{}, 0, 1,
                                               1, 4 * kPaidHourTicks, 85));
  check(source.recovery_due == 100 && cash(source) == 0 &&
            support.account.operating_tab == 85,
        "All-cash-spent boundary did not produce100 insurance and separate85 "
        "support");
  const auto paid =
      need(settle_paid_canister_income(support.account, {0, 0, 1}, 0, 1));
  check(paid.recovery_due == 0 && paid.operating_tab == 85 && cash(paid) == 0 &&
            paid.spent_credits == 300,
        "Full100 insurance must absorb the entire sale before any tab or "
        "wallet gain");
  const auto late = need(settle_paid_canister_income(support.account, {0, 0, 1},
                                                     4 * kPaidHourTicks, 1));
  check(late.recovery_due == 0 && late.operating_tab == 90 && cash(late) == 0 &&
            late.billed_through_hours == 5 && late.spent_credits == 300,
        "Due5 berth must reconcile from old budget before100 income settles "
        "insurance");
  const auto more = need(
      fund_paid_retainer(paid, {0, 0, 1}, 0, 1, 2, 4 * kPaidHourTicks, 85));
  const auto capped = need(fund_paid_retainer(more.account, {0, 0, 1}, 0, 1, 3,
                                              4 * kPaidHourTicks, 85));
  check(more.account.operating_tab == 170 &&
            capped.account.operating_tab == 200 && capped.tab_added == 30 &&
            capped.subsidized_credits == 55 && cash(capped.account) == 0 &&
            capped.account.version == 3 &&
            capped.account.settled_canister_count == 1 &&
            !fund_paid_retainer(capped.account, {0, 0, 1}, 0, 1, 3,
                                4 * kPaidHourTicks, 85),
        "Existing200 support cap or one-use commission sequence changed on "
        "version3");
  const auto loss = need(charge_paid_recovery(capped.account, {0, 0, 1}, 0, 2));
  const auto report = need(settle_paid_survey_income(loss, {1, 0, 1}, 0, 2));
  check(loss.recovery_due == 100 && report.recovery_due == 0 &&
            report.operating_tab == 200 && cash(report) == 0,
        "Capped operating subsidy subsidized insurance or created crash-sale "
        "cash");
}
auto refusals_and_bounds() -> void {
  const auto source =
      need(make_paid_operations(PaidIncomeCounts{2, 3, 1}, 100, 7));
  check(!make_paid_operations(PaidIncomeCounts{0, 0, 2}, 0, 0) &&
            !paid_operations_cash(source, PaidIncomeCounts{2, 3, 0}) &&
            !paid_operations_cash(source, PaidIncomeCounts{2, 3, 2}) &&
            !paid_operations_cash(source, PaidIncomeCounts{3, 3, 1}) &&
            !paid_operations_cash(source, PaidIncomeCounts{2, 4, 1}),
        "Finite source or exact survey/specimen/canister joins widened");
  for (unsigned damage = 0; damage < 9; ++damage) {
    auto bad = source;
    switch (damage) {
      case 0: bad.version = 4; break;
      case 1: bad.version = 2; break;
      case 2: bad.version = 1; break;
      case 3: bad.settled_canister_count = 0; break;
      case 4: bad.settled_canister_count = 2; break;
      case 5: bad.spent_credits = 19; break;
      case 6: bad.spent_credits = 621; break;
      case 7: bad.operating_tab = 201; break;
      case 8: bad.recovery_due = 101; break;
    }
    check(!validate_paid_operations(bad, {2, 3, 1}, 100, 7),
          "Invalid version3 hidden counter or balance accepted");
  }
  auto zero_sale = source;
  zero_sale.settled_canister_count = 0;
  check(!validate_paid_operations(zero_sale, {2, 3, 0}, 100, 7),
        "Version3 accepted without an actual finite sale");
  const auto fresh = need(make_paid_operations(PaidIncomeCounts{}, 100, 7));
  for (const auto& next :
       {PaidIncomeCounts{0, 0, 0}, PaidIncomeCounts{0, 0, 2},
        PaidIncomeCounts{1, 0, 1}, PaidIncomeCounts{0, 1, 1}})
    check(!settle_paid_canister_income(fresh, next, 100, 7),
          "Duplicate/multiple/cross-source income counters settled as one "
          "canister");
  check(!settle_paid_canister_income(fresh, {0, 0, 1}, 99, 7) &&
            !settle_paid_canister_income(fresh, {0, 0, 1}, 100, 8) &&
            !settle_paid_canister_income(source, {2, 3, 1}, 100, 7) &&
            !settle_paid_survey_income(source, {3, 3, 0}, 100, 7) &&
            !settle_paid_specimen_income(source, {2, 4, 0}, 100, 7, 1),
        "Sale clock/generation, duplicate or later income dropping source "
        "counter accepted");
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  constexpr auto count_maximum = std::numeric_limits<std::uint32_t>::max();
  check(!make_paid_operations(PaidIncomeCounts{0, 0, 1}, maximum, 0) &&
            !make_paid_operations(PaidIncomeCounts{0, 0, 1},
                                  maximum - kPaidHourTicks, 0) &&
            !make_paid_operations(PaidIncomeCounts{0, 0, 1}, 0, maximum) &&
            !settle_paid_canister_income(fresh, {0, 0, 1}, maximum, 7) &&
            !purchase_paid_service(source, {2, 3, 1}, 100, 7, 86) &&
            !charge_paid_recovery(source, {2, 3, 1}, 100, maximum),
        "Reserved clock, prepaid overflow, generation or service price bounds "
        "accepted");
  const auto huge = need(make_paid_operations(
      PaidIncomeCounts{count_maximum, count_maximum, 1}, 0, 0));
  check(
      cash(huge) == static_cast<std::uint64_t>(count_maximum) * 140 + 280 &&
          !settle_paid_survey_income(huge, {0, count_maximum, 1}, 0, 0) &&
          !settle_paid_specimen_income(huge, {count_maximum, 0, 1}, 0, 0, 1) &&
          !settle_paid_canister_income(huge, {count_maximum, count_maximum, 1},
                                       0, 0) &&
          source.version == 3 && cash(source) == 600,
      "Full-width income overflowed or finite source wrapped/reset an "
      "immutable account");
}
} // namespace
auto main() -> int {
  try {
    versions_and_prior_income();
    promotions_and_services();
    insurance_then_tab();
    full_insurance_and_support_cap();
    refusals_and_bounds();
    std::cout << "Paid canister income: " << checks << " checks, 0 failures\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Paid canister income after " << checks
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
