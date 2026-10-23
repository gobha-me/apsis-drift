#include "apsis_drift/paid_operations.hpp"

#include <iostream>
#include <limits>
#include <source_location>
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
template <class T>
auto need(std::expected<T, std::string> value,
          const std::source_location where = std::source_location::current())
    -> T {
  if (!value)
    throw std::runtime_error(std::to_string(where.line()) + ": " +
                             value.error());
  return *value;
}
auto counts(const PaidOperationsAccount& account) -> PaidIncomeCounts {
  return {account.settled_paid_count, account.settled_specimen_count};
}
auto cash(const PaidOperationsAccount& account) -> std::uint64_t {
  return need(paid_operations_cash(account, counts(account)));
}
auto versions_and_legacy() -> void {
  const auto old = need(make_paid_operations(3, 100, 7));
  const auto typed = need(make_paid_operations(PaidIncomeCounts{3, 0}, 100, 7));
  check(old == typed && typed.version == 1 &&
            typed.settled_specimen_count == 0 && cash(typed) == 480,
        "No prior sales keeps exact historical enrollment and survey gross");
  check(
      need(reconcile_paid_berth(old, 3, 100 + 4 * kPaidHourTicks, 7)) ==
              need(reconcile_paid_berth(typed, PaidIncomeCounts{3, 0},
                                        100 + 4 * kPaidHourTicks, 7)) &&
          need(purchase_paid_service(old, 3, 100, 7, 85)) ==
              need(purchase_paid_service(typed, PaidIncomeCounts{3, 0}, 100, 7,
                                         85)) &&
          need(settle_paid_survey_income(old, 4, 100, 7)) ==
              need(settle_paid_survey_income(typed, PaidIncomeCounts{4, 0}, 100,
                                             7)) &&
          need(charge_paid_recovery(old, 3, 100, 8)) ==
              need(charge_paid_recovery(typed, PaidIncomeCounts{3, 0}, 100, 8)),
      "Typed zero-sale services, billing, survey income and recovery preserve "
      "legacy results");
  const auto enrolled =
      need(make_paid_operations(PaidIncomeCounts{2, 3}, 100, 7));
  check(enrolled.version == 2 && enrolled.settled_specimen_count == 3 &&
            enrolled.settled_paid_count == 2 && cash(enrolled) == 500 &&
            enrolled.spent_credits == 20 && enrolled.started_tick == 100 &&
            enrolled.charged_generation == 7 && enrolled.recovery_due == 0 &&
            validate_paid_operations(enrolled, PaidIncomeCounts{2, 3}, 100, 7)
                .has_value(),
        "Explicit enrollment carries all prior gross once without historical "
        "insurance fees");
  check(!paid_operations_cash(enrolled, 2) &&
            !validate_paid_operations(enrolled, 2, 100, 7) &&
            !reconcile_paid_berth(enrolled, 2, 100, 7) &&
            !purchase_paid_service(enrolled, 2, 100, 7, 0) &&
            !fund_paid_retainer(enrolled, 2, 100, 7, 1,
                                100 + 4 * kPaidHourTicks, 85) &&
            !settle_paid_survey_income(enrolled, 3, 100, 7) &&
            !charge_paid_recovery(enrolled, 2, 100, 7),
        "Every historical survey-only overload rejects version2 rather than "
        "dropping specimen income");
  check(!paid_operations_cash(enrolled, PaidIncomeCounts{2, 2}) &&
            !paid_operations_cash(enrolled, PaidIncomeCounts{3, 3}) &&
            !paid_operations_cash(old, PaidIncomeCounts{3, 1}),
        "Both source counts must match and version1 cannot silently adopt a "
        "sale");
  auto bad = old;
  bad.settled_specimen_count = 1;
  check(!paid_operations_cash(bad, 3) &&
            !paid_operations_cash(bad, PaidIncomeCounts{3, 1}),
        "A hidden specimen counter is invalid in version1");
  bad = old;
  bad.version = 2;
  check(!paid_operations_cash(bad, PaidIncomeCounts{3, 0}),
        "Version2 cannot be selected merely by a version change with no sales");
  bad = enrolled;
  bad.version = 3;
  check(!validate_paid_operations(bad, counts(enrolled), 100, 7),
        "Unknown account versions refuse");
}
auto sale_promotion_and_prior_bills() -> void {
  // Explicit valid financial boundary fixture, not a physical service/loss
  // history.
  auto prior = need(make_paid_operations(2, 100, 7));
  prior.spent_credits = 390;
  prior.operating_tab = 80;
  prior.recovery_due = 30;
  prior.supported_sequence = 9;
  const auto now = 100 + 4 * kPaidHourTicks;
  const auto first =
      need(settle_paid_specimen_income(prior, {2, 1}, now, 7, 1));
  check(first.version == 2 && first.started_tick == 100 &&
            first.billed_through_hours == 5 && first.spent_credits == 430 &&
            first.operating_tab == 75 && first.recovery_due == 0 &&
            first.charged_generation == 7 && first.supported_sequence == 9 &&
            first.settled_paid_count == 2 &&
            first.settled_specimen_count == 1 && cash(first) == 10 &&
            prior.version == 1 && prior.spent_credits == 390,
        "First sale posts old-budget rent5, then insurance30 and tab5; "
        "promotion resets no saved field");
  const auto next = need(settle_paid_specimen_income(first, {2, 5}, now, 7, 4));
  check(next.operating_tab == 0 && next.recovery_due == 0 && cash(next) == 95 &&
            next.spent_credits == 505 && next.billed_through_hours == 5 &&
            next.supported_sequence == 9 && next.charged_generation == 7,
        "Four fresh specimens add160, repay the remaining75 tab and retain85 "
        "cash");
  check(!settle_paid_specimen_income(next, {2, 5}, now, 7, 4) &&
            !settle_paid_specimen_income(next, {3, 6}, now, 7, 1) &&
            !settle_paid_specimen_income(next, {2, 7}, now, 7, 1),
        "Duplicate, cross-source and mismatched batch counts refuse");
  const auto fresh = need(make_paid_operations(0, 0, 0));
  check(
      cash(need(settle_paid_specimen_income(fresh, {0, 1}, 0, 0, 1))) == 220 &&
          cash(need(settle_paid_specimen_income(fresh, {0, 4}, 0, 0, 4))) ==
              340,
      "One and four actual units earn40 and160 without another opening grant");
  auto overdue = fresh;
  overdue.spent_credits = 200;
  overdue.recovery_due = 100;
  const auto late = need(
      settle_paid_specimen_income(overdue, {0, 4}, 100 * kPaidHourTicks, 0, 4));
  check(late.billed_through_hours == 101 && late.operating_tab == 170 &&
            late.recovery_due == 0 && late.spent_credits == 330 &&
            cash(late) == 30,
        "Overdue bills consume old cash/cap support before new160; "
        "insurance100 keeps first claim");
}
auto typed_services_income_and_replacement() -> void {
  constexpr PaidIncomeCounts initial_counts{0, 4};
  const auto initial = need(make_paid_operations(initial_counts, 0, 0));
  auto account = need(purchase_paid_service(initial, initial_counts, 0, 0, 85));
  check(cash(account) == 255 && cash(initial) == 340 &&
            !purchase_paid_service(initial, initial_counts, 0, 0, 86),
        "Version2 cash-only service preserves prices and immutable input on "
        "refusal");
  constexpr auto now = 4 * kPaidHourTicks;
  account = need(reconcile_paid_berth(account, initial_counts, now, 0));
  check(cash(account) == 250 && account.billed_through_hours == 5 &&
            need(reconcile_paid_berth(account, initial_counts, now, 0)) ==
                account,
        "Version2 pays the same one-time equality berth bill");
  account = need(purchase_paid_service(account, initial_counts, now, 0, 85));
  account = need(purchase_paid_service(account, initial_counts, now, 0, 85));
  account = need(purchase_paid_service(account, initial_counts, now, 0, 80));
  check(
      cash(account) == 0 &&
          !purchase_paid_service(account, initial_counts, now, 0, 1),
      "Combined gross can be spent only once, with insufficient cash refusing");
  const auto support = need(fund_paid_retainer(account, initial_counts, now, 0,
                                               1, 8 * kPaidHourTicks, 85));
  check(support.account.version == 2 &&
            support.account.settled_specimen_count == 4 &&
            support.account.billed_through_hours == 8 &&
            support.berth_credits == 15 && support.cash_debit == 0 &&
            support.tab_added == 100 && support.subsidized_credits == 0 &&
            !fund_paid_retainer(support.account, initial_counts, now, 0, 1,
                                8 * kPaidHourTicks, 85),
        "Retainer preserves specimen identity and funds "
        "unchanged85fuel+15berth once per sequence");
  const auto charged =
      need(charge_paid_recovery(support.account, initial_counts, now, 1));
  check(charged.version == 2 && charged.recovery_due == 100 &&
            charged.operating_tab == 100 && charged.charged_generation == 1 &&
            cash(charged) == 0 &&
            need(charge_paid_recovery(charged, initial_counts,
                                      20 * kPaidHourTicks, 1)) == charged &&
            !charge_paid_recovery(charged, initial_counts, now, 3),
        "Replacement preserves selected version/count and matching-generation "
        "replay does not post bills");
  const auto one =
      need(settle_paid_specimen_income(charged, {0, 5}, now, 1, 1));
  const auto two = need(settle_paid_specimen_income(one, {0, 7}, now, 1, 2));
  check(one.recovery_due == 60 && one.operating_tab == 100 && cash(one) == 0 &&
            two.recovery_due == 0 && two.operating_tab == 90 && cash(two) == 10,
        "Successive specimen batches pay insurance before any discretionary "
        "tab or cash gain");
  const auto survey = need(settle_paid_survey_income(two, {1, 7}, now, 1));
  check(survey.version == 2 && survey.settled_paid_count == 1 &&
            survey.settled_specimen_count == 7 && survey.operating_tab == 40 &&
            cash(survey) == 60 &&
            !settle_paid_survey_income(survey, {1, 7}, now, 1) &&
            !settle_paid_survey_income(two, {1, 8}, now, 1),
        "Survey100 on version2 preserves specimens and repays50 tab, "
        "adding50 to the existing10 cash");
  auto capped = charged;
  capped.operating_tab = 200;
  const auto hardship = need(fund_paid_retainer(capped, initial_counts, now, 1,
                                                2, 8 * kPaidHourTicks, 85));
  check(hardship.subsidized_credits == 85 &&
            hardship.account.recovery_due == 100 &&
            hardship.account.operating_tab == 200 &&
            cash(hardship.account) == 0,
        "At the cap, noncash operating support never subsidizes insurance or "
        "grants sale cash");
}
auto bounds_and_refusals() -> void {
  constexpr auto count_max = std::numeric_limits<std::uint32_t>::max();
  constexpr auto clock_max = std::numeric_limits<SimulationTick>::max();
  const auto source =
      need(make_paid_operations(PaidIncomeCounts{0, 1}, 100, 7));
  check(!settle_paid_specimen_income(source, {0, 1}, 100, 7, 0) &&
            !settle_paid_specimen_income(source, {0, 6}, 100, 7, 5) &&
            !settle_paid_specimen_income(source, {0, 2}, 99, 7, 1) &&
            !settle_paid_specimen_income(source, {0, 2}, clock_max, 7, 1) &&
            !settle_paid_specimen_income(source, {0, 2}, 100, 8, 1) &&
            cash(source) == 220 && source.settled_specimen_count == 1,
        "Units, source clock and generation bounds refuse without changing the "
        "account");
  const auto full =
      need(make_paid_operations(PaidIncomeCounts{count_max, count_max}, 0, 0));
  check(cash(full) == 601'295'421'480ULL &&
            !settle_paid_specimen_income(full, {count_max, 0}, 0, 0, 1) &&
            !settle_paid_survey_income(full, {0, count_max}, 0, 0),
        "Both full-width counters have exact bounded gross and cannot wrap");
  const auto near =
      need(make_paid_operations(PaidIncomeCounts{0, count_max - 3}, 0, 0));
  check(!settle_paid_specimen_income(near, {0, 0}, 0, 0, 4) &&
            need(settle_paid_specimen_income(near, {0, count_max}, 0, 0, 3))
                    .settled_specimen_count == count_max,
        "Batch arithmetic checks overflow before adding units and admits the "
        "exact last count");
  const auto last_generation =
      need(make_paid_operations(PaidIncomeCounts{0, 1}, 0, clock_max - 1));
  check(!charge_paid_recovery(last_generation, counts(last_generation), 0,
                              clock_max),
        "Version2 retains the historical generation exhaustion refusal");
  auto invalid = source;
  invalid.spent_credits = 241;
  check(!validate_paid_operations(invalid, counts(source), 100, 7) &&
            !settle_paid_specimen_income(invalid, {0, 2}, 100, 7, 1),
        "A new earning cannot repair an invalid pre-income source budget");
}
} // namespace

auto main() -> int {
  try {
    versions_and_legacy();
    sale_promotion_and_prior_bills();
    typed_services_income_and_replacement();
    bounds_and_refusals();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << checks << " paid specimen income checks, 0 failures\n";
  return 0;
}
