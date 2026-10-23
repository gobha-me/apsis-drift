#include "apsis_drift/paid_operations.hpp"

#include <algorithm>
#include <limits>

namespace apsis_drift {
namespace {
constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
constexpr auto initial_berth_cost =
    kPaidInitialBerthHours * kPaidBerthCreditsPerHour;
constexpr auto maximum_service_cost =
    kPaidFullFlightCredits + 3 * kPaidJumpChargeCredits;
auto budget(PaidIncomeCounts counts) -> std::uint64_t {
  // Both source counts are uint32; their fixed gross plus opening fits uint64.
  return kPaidOpeningCredits +
         static_cast<std::uint64_t>(counts.survey_paid) *
             kPaidSurveyRewardCredits +
         static_cast<std::uint64_t>(counts.specimen_sold) *
             kPaidSpecimenCredits;
}
auto coverage_fits(const PaidOperationsAccount& account, std::uint64_t hours)
    -> bool {
  return hours <= (maximum - account.started_tick) / kPaidHourTicks;
}
auto berth_cost(std::uint64_t hours)
    -> std::expected<std::uint64_t, std::string> {
  if (hours > maximum / kPaidBerthCreditsPerHour)
    return std::unexpected{"Berth price would overflow"};
  return hours * kPaidBerthCreditsPerHour;
}
auto apply_operating_cost(PaidOperationsAccount account,
                          PaidIncomeCounts counts, std::uint64_t cost)
    -> PaidRetainerOutcome {
  const auto cash = budget(counts) - account.spent_credits;
  const auto debit = std::min(cash, cost);
  const auto remaining = cost - debit;
  const auto added =
      std::min(kPaidOperatingTabCap - account.operating_tab, remaining);
  account.spent_credits += debit;
  account.operating_tab += added;
  return {account, 0, 0, debit, added, remaining - added};
}
auto apply_income(PaidOperationsAccount account, PaidIncomeCounts counts,
                  std::uint64_t gross) -> PaidOperationsAccount {
  account.settled_paid_count = counts.survey_paid;
  account.settled_specimen_count = counts.specimen_sold;
  const auto insurance = std::min(account.recovery_due, gross);
  const auto remaining = gross - insurance;
  const auto tab_payment = std::min(account.operating_tab, remaining / 2);
  account.recovery_due -= insurance;
  account.operating_tab -= tab_payment;
  account.spent_credits += insurance + tab_payment;
  return account;
}
} // namespace

auto paid_operations_cash(const PaidOperationsAccount& account,
                          PaidIncomeCounts counts)
    -> std::expected<std::uint64_t, std::string> {
  const auto legacy = account.version == kPaidOperationsVersion &&
                      counts.specimen_sold == 0 &&
                      account.settled_specimen_count == 0;
  const auto specimen = account.version == kPaidSpecimenOperationsVersion &&
                        counts.specimen_sold != 0 &&
                        account.settled_specimen_count == counts.specimen_sold;
  if ((!legacy && !specimen) ||
      account.settled_paid_count != counts.survey_paid ||
      account.billed_through_hours < kPaidInitialBerthHours ||
      account.spent_credits < initial_berth_cost ||
      account.spent_credits > budget(counts) ||
      account.operating_tab > kPaidOperatingTabCap ||
      account.recovery_due > kPaidRecoveryExcessCredits)
    return std::unexpected{"Invalid paid account version, count or balances"};
  return budget(counts) - account.spent_credits;
}
auto validate_paid_operations(const PaidOperationsAccount& account,
                              PaidIncomeCounts counts, SimulationTick now,
                              std::uint64_t generation)
    -> std::expected<void, std::string> {
  if (auto cash = paid_operations_cash(account, counts); !cash)
    return std::unexpected{cash.error()};
  if (now == maximum || account.started_tick > now || generation == maximum ||
      account.charged_generation != generation ||
      !coverage_fits(account, account.billed_through_hours))
    return std::unexpected{
        "Paid account clock, coverage or generation differs"};
  const auto elapsed_hours = (now - account.started_tick) / kPaidHourTicks;
  // At most the current partial hour and four reserved hours can be prepaid.
  // Tick/hour bounds make this fixed +5 safe even at the last global tick.
  if (account.billed_through_hours > elapsed_hours + 5)
    return std::unexpected{"Paid berth coverage exceeds the reservation bound"};
  return {};
}
auto make_paid_operations(PaidIncomeCounts counts, SimulationTick now,
                          std::uint64_t generation)
    -> std::expected<PaidOperationsAccount, std::string> {
  PaidOperationsAccount account;
  account.version = counts.specimen_sold ? kPaidSpecimenOperationsVersion
                                         : kPaidOperationsVersion;
  account.started_tick = now;
  account.settled_paid_count = counts.survey_paid;
  account.settled_specimen_count = counts.specimen_sold;
  account.charged_generation = generation;
  if (auto valid = validate_paid_operations(account, counts, now, generation);
      !valid)
    return std::unexpected{valid.error()};
  return account;
}
auto reconcile_paid_berth(const PaidOperationsAccount& account,
                          PaidIncomeCounts counts, SimulationTick now,
                          std::uint64_t generation)
    -> std::expected<PaidOperationsAccount, std::string> {
  if (auto valid = validate_paid_operations(account, counts, now, generation);
      !valid)
    return std::unexpected{valid.error()};
  const auto due_through = (now - account.started_tick) / kPaidHourTicks + 1;
  if (due_through <= account.billed_through_hours) return account;
  if (!coverage_fits(account, due_through))
    return std::unexpected{"New berth coverage would overflow the game clock"};
  auto price = berth_cost(due_through - account.billed_through_hours);
  if (!price) return std::unexpected{price.error()};
  auto next = apply_operating_cost(account, counts, *price).account;
  next.billed_through_hours = due_through;
  return next;
}
auto paid_fuel_price(std::uint64_t missing_flight_quanta,
                     std::uint64_t capacity_quanta,
                     std::uint32_t missing_jump_charges)
    -> std::expected<std::uint64_t, std::string> {
  if (!capacity_quanta || missing_flight_quanta > capacity_quanta ||
      missing_jump_charges > 3)
    return std::unexpected{"Fuel quote needs bounded actual pool quantities"};
  // Accumulate the fixed 40-credit numerator as quotient/remainder. Avoid
  // overflowing 40 * missing even for otherwise valid uint64 pool dimensions.
  std::uint64_t flight{}, remainder{};
  for (std::uint64_t i = 0; i < kPaidFullFlightCredits; ++i) {
    const auto gap = capacity_quanta - missing_flight_quanta;
    if (remainder >= gap) {
      ++flight;
      remainder -= gap;
    } else {
      remainder += missing_flight_quanta;
    }
  }
  if (remainder) ++flight;
  return flight + static_cast<std::uint64_t>(missing_jump_charges) *
                      kPaidJumpChargeCredits;
}
auto purchase_paid_service(const PaidOperationsAccount& account,
                           PaidIncomeCounts counts, SimulationTick now,
                           std::uint64_t generation,
                           std::uint64_t service_credits)
    -> std::expected<PaidOperationsAccount, std::string> {
  if (service_credits > maximum_service_cost)
    return std::unexpected{"Service quote exceeds starter restoration bounds"};
  auto next = reconcile_paid_berth(account, counts, now, generation);
  if (!next) return std::unexpected{next.error()};
  const auto cash = paid_operations_cash(*next, counts);
  if (!cash) return std::unexpected{cash.error()};
  if (*cash < service_credits)
    return std::unexpected{"Insufficient cash for the quoted fuel service"};
  next->spent_credits += service_credits;
  return *next;
}
auto fund_paid_retainer(const PaidOperationsAccount& account,
                        PaidIncomeCounts counts, SimulationTick now,
                        std::uint64_t generation,
                        std::uint32_t commission_sequence,
                        SimulationTick commission_deadline,
                        std::uint64_t service_credits)
    -> std::expected<PaidRetainerOutcome, std::string> {
  auto reconciled = reconcile_paid_berth(account, counts, now, generation);
  if (!reconciled) return std::unexpected{reconciled.error()};
  if (!commission_sequence ||
      commission_sequence <= reconciled->supported_sequence ||
      commission_deadline <= now ||
      commission_deadline - now > 4 * kPaidHourTicks ||
      service_credits > maximum_service_cost)
    return std::unexpected{"Fresh unexpired commission support terms required"};
  const auto duration = commission_deadline - reconciled->started_tick;
  const auto through =
      duration / kPaidHourTicks + (duration % kPaidHourTicks ? 1U : 0U);
  if (!coverage_fits(*reconciled, through))
    return std::unexpected{"Retainer berth coverage would overflow"};
  const auto extra = through > reconciled->billed_through_hours
                         ? through - reconciled->billed_through_hours
                         : 0;
  const auto berth = berth_cost(extra);
  if (!berth) return std::unexpected{berth.error()};
  if (*berth > maximum - service_credits)
    return std::unexpected{"Retainer quote would overflow"};
  const auto total = service_credits + *berth;
  const auto cash = paid_operations_cash(*reconciled, counts);
  if (!cash) return std::unexpected{cash.error()};
  if (!total || *cash >= total)
    return std::unexpected{
        "Use ordinary cash service for this affordable quote"};
  auto outcome = apply_operating_cost(*reconciled, counts, total);
  outcome.account.billed_through_hours =
      std::max(outcome.account.billed_through_hours, through);
  outcome.account.supported_sequence = commission_sequence;
  outcome.service_credits = service_credits;
  outcome.berth_credits = *berth;
  return outcome;
}
auto settle_paid_survey_income(const PaidOperationsAccount& account,
                               PaidIncomeCounts new_counts, SimulationTick now,
                               std::uint64_t generation)
    -> std::expected<PaidOperationsAccount, std::string> {
  if (account.settled_paid_count == std::numeric_limits<std::uint32_t>::max() ||
      new_counts.survey_paid != account.settled_paid_count + 1 ||
      new_counts.specimen_sold != account.settled_specimen_count)
    return std::unexpected{"Exactly one new survey payment must be settled"};
  auto next =
      reconcile_paid_berth(account,
                           PaidIncomeCounts{account.settled_paid_count,
                                            account.settled_specimen_count},
                           now, generation);
  if (!next) return std::unexpected{next.error()};
  return apply_income(*next, new_counts, kPaidSurveyRewardCredits);
}
auto settle_paid_specimen_income(const PaidOperationsAccount& account,
                                 PaidIncomeCounts new_counts,
                                 SimulationTick now, std::uint64_t generation,
                                 std::uint32_t sold_units)
    -> std::expected<PaidOperationsAccount, std::string> {
  if (!sold_units || sold_units > kPaidSpecimenBatchLimit ||
      account.settled_specimen_count >
          std::numeric_limits<std::uint32_t>::max() - sold_units ||
      new_counts.specimen_sold != account.settled_specimen_count + sold_units ||
      new_counts.survey_paid != account.settled_paid_count)
    return std::unexpected{"One actual bounded specimen batch must be settled"};
  auto next =
      reconcile_paid_berth(account,
                           PaidIncomeCounts{account.settled_paid_count,
                                            account.settled_specimen_count},
                           now, generation);
  if (!next) return std::unexpected{next.error()};
  // Only this actual earning action promotes an existing selected account.
  // Reconcile the old budget first; the new gross cannot pay an overdue bill
  // ahead of the insurance-first income contract.
  next->version = kPaidSpecimenOperationsVersion;
  return apply_income(*next, new_counts,
                      static_cast<std::uint64_t>(sold_units) *
                          kPaidSpecimenCredits);
}
auto charge_paid_recovery(const PaidOperationsAccount& account,
                          PaidIncomeCounts counts, SimulationTick now,
                          std::uint64_t replacement_generation)
    -> std::expected<PaidOperationsAccount, std::string> {
  if (auto valid = validate_paid_operations(account, counts, now,
                                            account.charged_generation);
      !valid)
    return std::unexpected{valid.error()};
  if (replacement_generation == account.charged_generation) return account;
  if (replacement_generation == maximum ||
      replacement_generation != account.charged_generation + 1)
    return std::unexpected{
        "Recovery fee needs one successive actual generation"};
  auto next =
      reconcile_paid_berth(account, counts, now, account.charged_generation);
  if (!next) return std::unexpected{next.error()};
  const auto cash = paid_operations_cash(*next, counts);
  if (!cash) return std::unexpected{cash.error()};
  const auto debit = std::min(*cash, kPaidRecoveryExcessCredits);
  next->spent_credits += debit;
  next->recovery_due =
      std::min(kPaidRecoveryExcessCredits,
               next->recovery_due + kPaidRecoveryExcessCredits - debit);
  next->charged_generation = replacement_generation;
  return *next;
}
// Historical survey-only APIs intentionally supply no specimen income. The
// typed admission rejects version2 here instead of silently discarding gross.
auto paid_operations_cash(const PaidOperationsAccount& account,
                          std::uint32_t paid_count)
    -> std::expected<std::uint64_t, std::string> {
  return paid_operations_cash(account, PaidIncomeCounts{paid_count, 0});
}
auto validate_paid_operations(const PaidOperationsAccount& account,
                              std::uint32_t paid_count, SimulationTick now,
                              std::uint64_t generation)
    -> std::expected<void, std::string> {
  return validate_paid_operations(account, PaidIncomeCounts{paid_count, 0}, now,
                                  generation);
}
auto make_paid_operations(std::uint32_t paid_count, SimulationTick now,
                          std::uint64_t generation)
    -> std::expected<PaidOperationsAccount, std::string> {
  return make_paid_operations(PaidIncomeCounts{paid_count, 0}, now, generation);
}
auto reconcile_paid_berth(const PaidOperationsAccount& account,
                          std::uint32_t paid_count, SimulationTick now,
                          std::uint64_t generation)
    -> std::expected<PaidOperationsAccount, std::string> {
  return reconcile_paid_berth(account, PaidIncomeCounts{paid_count, 0}, now,
                              generation);
}
auto purchase_paid_service(const PaidOperationsAccount& account,
                           std::uint32_t paid_count, SimulationTick now,
                           std::uint64_t generation,
                           std::uint64_t service_credits)
    -> std::expected<PaidOperationsAccount, std::string> {
  return purchase_paid_service(account, PaidIncomeCounts{paid_count, 0}, now,
                               generation, service_credits);
}
auto fund_paid_retainer(const PaidOperationsAccount& account,
                        std::uint32_t paid_count, SimulationTick now,
                        std::uint64_t generation,
                        std::uint32_t commission_sequence,
                        SimulationTick commission_deadline,
                        std::uint64_t service_credits)
    -> std::expected<PaidRetainerOutcome, std::string> {
  return fund_paid_retainer(account, PaidIncomeCounts{paid_count, 0}, now,
                            generation, commission_sequence,
                            commission_deadline, service_credits);
}
auto settle_paid_survey_income(const PaidOperationsAccount& account,
                               std::uint32_t new_paid_count, SimulationTick now,
                               std::uint64_t generation)
    -> std::expected<PaidOperationsAccount, std::string> {
  return settle_paid_survey_income(account, PaidIncomeCounts{new_paid_count, 0},
                                   now, generation);
}
auto charge_paid_recovery(const PaidOperationsAccount& account,
                          std::uint32_t paid_count, SimulationTick now,
                          std::uint64_t replacement_generation)
    -> std::expected<PaidOperationsAccount, std::string> {
  return charge_paid_recovery(account, PaidIncomeCounts{paid_count, 0}, now,
                              replacement_generation);
}
} // namespace apsis_drift
