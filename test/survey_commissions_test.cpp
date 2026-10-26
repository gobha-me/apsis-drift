#include "apsis_drift/survey_commissions.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
using namespace apsis_drift;
int checks{};
auto check(bool value, std::string_view message) -> void {
  ++checks;
  if (!value) throw std::runtime_error(std::string{message});
}
template <typename T> auto need(std::expected<T, std::string> value) -> T {
  if (!value) throw std::runtime_error(value.error());
  return std::move(*value);
}
auto acquire(SurveyCommissionAccount account, SimulationTick first,
             StarterCraftId craft = {7}) -> SurveyCommissionAccount {
  for (std::uint32_t i = 0; i < kSurveyCommissionMeasurementTicks; ++i)
    account = need(advance_survey_commission(account, craft, first + i, true));
  return account;
}
// Typed provider fixtures exercise record shape only, without claiming an
// authenticated site, physical collection, carried membership or flown route.
auto specimen(SimulationTick tick) -> FreedomFieldSampleRecord {
  return {{{2}, body_id(PlanetId{3})},         {1, -2, 3}, {9}, 2,
          SurfaceLandmarkKind::ochre_exposure, {7},        tick};
}
auto geological_invalid_boundaries() -> void {
  const SurveyCommissionAccount empty;
  check(!accept_geological_survey_commission(empty, {}, {3}, 0) &&
            !accept_geological_survey_commission(empty, {2}, {}, 0) &&
            !submit_geological_survey_commission(empty, 1, 1, specimen(1)),
        "Invalid targets and missing work refuse before geological payment");
  const auto account =
      need(accept_geological_survey_commission(empty, {2}, {3}, 100));
  for (unsigned fault = 0; fault < 13; ++fault) {
    auto sample = specimen(101);
    switch (fault) {
      case 0: sample.tick = 100; break;
      case 1: sample.tick = 99; break;
      case 2: sample.tick = 103; break;
      case 3: sample.target.system = {4}; break;
      case 4: sample.target.body = body_id(PlanetId{4}); break;
      case 5: sample.target.body.kind = BodyKind::moon; break;
      case 6: sample.target.body.version = 2; break;
      case 7: sample.site = {}; break;
      case 8: sample.craft = {}; break;
      case 9: sample.terrain_recipe = 0; break;
      case 10: sample.terrain_recipe = 4; break;
      case 11: sample.kind = static_cast<SurfaceLandmarkKind>(3); break;
      case 12: sample.cell.y = -32769; break;
      default: throw std::runtime_error("Invalid sample fault");
    }
    check(!submit_geological_survey_commission(account, 1, 102, sample),
          "Old, future, foreign or malformed geological proof refuses");
  }
  auto bad = account;
  bad.active->measurement = SurveyCommissionMeasurement{{7}, 101, 101, 1};
  check(!validate_survey_commissions(bad, 102),
        "Geological active work rejects an orbital measurement");
  bad = account;
  bad.active->kind = static_cast<SurveyCommissionKind>(0);
  check(!validate_survey_commissions(bad, 102), "Unknown work kind refuses");
  bad = account;
  bad.version = kSurveyCommissionVersion;
  check(!validate_survey_commissions(bad, 102),
        "Version one cannot contain geological work");
  bad = empty;
  bad.geological_paid_count = 1;
  check(!validate_survey_commissions(bad, 102),
        "Default account cannot claim geological earnings");
  const auto deadline = account.active->deadline_tick;
  check(!accept_geological_survey_commission(account, {2}, {3}, 102) &&
            !accept_survey_commission(account, {2}, {3}, 102) &&
            submit_geological_survey_commission(account, 1, deadline - 1,
                                                specimen(101))
                .has_value() &&
            !submit_geological_survey_commission(
                account, 1, std::numeric_limits<SimulationTick>::max(),
                specimen(101)) &&
            !accept_geological_survey_commission(
                empty, {2}, {3},
                std::numeric_limits<SimulationTick>::max() -
                    kSurveyCommissionReservationTicks + 1),
        "Kinds share one reservation; last predeadline payment works and clock "
        "wrap refuses");
  check(!submit_geological_survey_commission(account, 2, 102, specimen(101)) &&
            !submit_survey_commission(account, 1, 102) &&
            !submit_geological_survey_commission(account, 1, deadline,
                                                 specimen(101)) &&
            need(advance_survey_commission(account, {}, 102, true)) ==
                account &&
            need(advance_survey_commission(account, {7}, deadline, true)) ==
                account,
        "Stale serial, wrong action and deadline equality refuse; flight ticks "
        "do not measure samples");
  auto expired = need(accept_survey_commission(account, {2}, {3}, deadline));
  check(expired.version == kGeologicalSurveyCommissionVersion &&
            expired.active->sequence == 2 &&
            expired.active->kind == SurveyCommissionKind::orbital &&
            !expired.active->measurement && expired.paid_count == 0,
        "Expired geological terms can be replaced by fresh orbital work "
        "without demotion");
  check(account.active->accepted_tick == 100 && !account.active->measurement &&
            account.paid_count == 0 && !account.latest_paid,
        "All refused sample actions leave the original ledger unchanged");
}
auto geological_payment_and_mixed_history() -> void {
  const auto account =
      need(accept_geological_survey_commission({}, {2}, {3}, 100));
  const auto sample = specimen(101);
  auto paid =
      need(submit_geological_survey_commission(account, 1, 101, sample));
  check(paid.version == kGeologicalSurveyCommissionVersion && !paid.active &&
            paid.paid_count == 1 && paid.geological_paid_count == 1 &&
            paid.latest_paid->delivered_specimen == sample &&
            paid.latest_paid->commission == *account.active &&
            paid.latest_paid->submitted_tick == 101 &&
            survey_commission_earnings(paid) == 100 &&
            validate_survey_commissions(paid, 101).has_value() &&
            !submit_geological_survey_commission(paid, 1, 101, sample),
        "One fresh proof pays once, retaining exact sample and terms");
  for (unsigned fault = 0; fault < 8; ++fault) {
    auto bad = paid;
    switch (fault) {
      case 0: bad.version = kSurveyCommissionVersion; break;
      case 1: bad.geological_paid_count = 0; break;
      case 2: bad.geological_paid_count = 2; break;
      case 3: bad.latest_paid->delivered_specimen.reset(); break;
      case 4: bad.latest_paid->delivered_specimen->tick = 100; break;
      case 5: bad.latest_paid->submitted_tick = 100; break;
      case 6:
        bad.latest_paid->commission.measurement =
            SurveyCommissionMeasurement{{7}, 101, 101, 1};
        break;
      case 7:
        bad.latest_paid->commission.kind = SurveyCommissionKind::orbital;
        break;
      default: throw std::runtime_error("Invalid paid geological fault");
    }
    check(!validate_survey_commissions(bad, 102),
          "Paid sample receipt rejects version, count, chronology and mixed "
          "proofs");
  }
  auto mixed = need(accept_survey_commission(paid, {2}, {3}, 101));
  check(!submit_geological_survey_commission(mixed, 2, 102, specimen(102)),
        "A physical record cannot fulfill orbital work");
  mixed = acquire(mixed, 102);
  mixed = need(submit_survey_commission(mixed, 2, 3702));
  check(
      mixed.version == kGeologicalSurveyCommissionVersion &&
          mixed.paid_count == 2 && mixed.geological_paid_count == 1 &&
          !mixed.latest_paid->delivered_specimen &&
          survey_commission_earnings(mixed) == 200,
      "Orbital settlement in a mixed ledger preserves geological count and v2");
  auto bad = mixed;
  bad.geological_paid_count = bad.paid_count;
  check(!validate_survey_commissions(bad, 3702),
        "Latest orbital payment requires a nonzero orbital lifetime count");
  bad = mixed;
  bad.latest_paid->delivered_specimen = sample;
  check(!validate_survey_commissions(bad, 3702),
        "An orbital receipt cannot carry a second physical proof");
  mixed = need(accept_geological_survey_commission(mixed, {2}, {3}, 3702));
  check(
      !submit_geological_survey_commission(mixed, 3, 3703, sample),
      "A preceding paid collection cannot fulfill the next fresh reservation");
  const auto later = specimen(3703);
  mixed = need(submit_geological_survey_commission(mixed, 3, 3703, later));
  check(mixed.paid_count == 3 && mixed.geological_paid_count == 2 &&
            mixed.latest_paid->delivered_specimen == later,
        "Later recollection at the same site is fresh provider proof");
  const auto abandoned = need(abandon_survey_commission(
      need(accept_geological_survey_commission(mixed, {2}, {3}, 3703)), 4,
      3703));
  check(
      abandoned.paid_count == mixed.paid_count &&
          abandoned.geological_paid_count == mixed.geological_paid_count &&
          abandoned.latest_paid == mixed.latest_paid && !abandoned.active &&
          abandoned.version == kGeologicalSurveyCommissionVersion,
      "Abandonment consumes no proof or earnings and never demotes the ledger");
  // Explicit aggregate-history boundary: no claim to have executed billions of
  // jobs.
  auto last = paid;
  const auto maximum = std::numeric_limits<std::uint32_t>::max();
  last.next_sequence = maximum - 1;
  last.paid_count = maximum - 2;
  last.geological_paid_count = maximum - 2;
  last.latest_paid->commission.sequence = maximum - 2;
  last = need(accept_geological_survey_commission(last, {2}, {3}, 101));
  last = need(submit_geological_survey_commission(last, maximum - 1, 102,
                                                  specimen(102)));
  check(last.next_sequence == maximum && last.paid_count == maximum - 1 &&
            last.geological_paid_count == maximum - 1 &&
            survey_commission_earnings(last) ==
                static_cast<std::uint64_t>(maximum - 1) * 100 &&
            !accept_geological_survey_commission(last, {2}, {3}, 102) &&
            !submit_geological_survey_commission(last, maximum - 1, 102,
                                                 specimen(102)),
        "Last usable serial settles without counter or earnings overflow; no "
        "wrap or duplicate grant");
  const SurveyCommission legacy{
      1, {2}, {3}, 0, kSurveyCommissionReservationTicks, {}};
  const SurveyCommissionReceipt legacy_receipt{legacy, 1};
  const SurveyCommissionAccount legacy_account{1, 2, 0, legacy, {}};
  check(legacy.kind == SurveyCommissionKind::orbital &&
            !legacy_receipt.delivered_specimen &&
            legacy_account.geological_paid_count == 0 &&
            validate_survey_commissions(legacy_account, 1).has_value(),
        "Legacy aggregate positions and all appended defaults retain their "
        "exact meaning");
}
auto boundaries() -> void {
  const SurveyCommissionAccount empty;
  check(validate_survey_commissions(empty, 0).has_value() &&
            survey_commission_earnings(empty) == 0,
        "A selected empty account grants no opening credits");
  const auto hour = kSurveyCommissionHourTicks;
  check(need(survey_commission_offer_deadline(0)) == hour &&
            need(survey_commission_offer_deadline(hour - 1)) == hour &&
            need(survey_commission_offer_deadline(hour)) == 2 * hour,
        "Standing offer rolls at the next absolute hour, including equality");
  const auto maximum = std::numeric_limits<SimulationTick>::max();
  check(!survey_commission_offer_deadline(maximum) &&
            !survey_commission_offer_deadline(maximum - 1),
        "Last incomplete hour refuses overflowing offer arithmetic");
  auto last = need(accept_survey_commission(
      empty, {2}, {3}, maximum - kSurveyCommissionReservationTicks));
  check(
      last.active->deadline_tick == maximum &&
          !accept_survey_commission(
              empty, {2}, {3}, maximum - kSurveyCommissionReservationTicks + 1),
      "Reservation accepts the final representable deadline, not a wrap");
  check(!validate_survey_commissions(empty, maximum) &&
            !accept_survey_commission(empty, {0}, {3}, 0) &&
            !accept_survey_commission(empty, {2}, {0}, 0),
        "Invalid global tick and zero target identities are refused");
  auto exhausted = empty;
  exhausted.next_sequence = std::numeric_limits<std::uint32_t>::max() - 1;
  exhausted = need(accept_survey_commission(exhausted, {2}, {3}, 0));
  check(exhausted.active->sequence ==
                std::numeric_limits<std::uint32_t>::max() - 1 &&
            validate_survey_commissions(exhausted, 0).has_value(),
        "Last usable sequence is distinct and admitted");
  exhausted =
      need(abandon_survey_commission(exhausted, exhausted.active->sequence, 0));
  check(!accept_survey_commission(exhausted, {2}, {3}, 0),
        "Abandonment cannot wrap the exhausted sequence counter");
}
auto acquisition_and_payment() -> void {
  auto account = need(accept_survey_commission({}, {2}, {3}, 100));
  const auto first_sequence = account.active->sequence;
  check(first_sequence == 1 && account.next_sequence == 2 &&
            account.active->deadline_tick ==
                100 + kSurveyCommissionReservationTicks &&
            !account.active->measurement &&
            survey_commission_earnings(account) == 0,
        "Acceptance reserves terms without progress or payment");
  check(need(advance_survey_commission(account, {7}, 100, true)) == account &&
            !accept_survey_commission(account, {2}, {3}, 100) &&
            !submit_survey_commission(account, first_sequence, 100),
        "Acceptance tick and duplicate accept cannot generate work or wages");
  account = need(advance_survey_commission(account, {7}, 101, false));
  check(account.active->measurement->acquired_ticks == 0 &&
            account.active->measurement->started_tick == 101 &&
            need(advance_survey_commission(account, {7}, 101, true)) == account,
        "First held interval is stamped; same-tick retry cannot qualify it");
  account = need(advance_survey_commission(account, {7}, 102, true));
  const auto once = account;
  check(need(advance_survey_commission(account, {7}, 102, true)) == once &&
            !advance_survey_commission(account, {8}, 103, true) &&
            !advance_survey_commission(account, {0}, 103, false) &&
            !advance_survey_commission(account, {7}, 101, true),
        "Duplicate intervals, foreign source, zero craft and clock rewind "
        "cannot acquire fresh work");
  account = need(advance_survey_commission(account, {7}, 103, false));
  for (SimulationTick tick = 104; tick < 3702; ++tick)
    account = need(advance_survey_commission(account, {7}, tick, true));
  check(account.active->measurement->acquired_ticks == 3599 &&
            !submit_survey_commission(account, first_sequence, 3701) &&
            survey_commission_earnings(account) == 0,
        "3599 newly qualified intervals are incomplete despite held time");
  const auto near = account;
  auto continued = near;
  account = need(advance_survey_commission(account, {7}, 3702, true));
  continued = need(advance_survey_commission(continued, {7}, 3702, true));
  check(account == continued &&
            account.active->measurement->acquired_ticks == 3600 &&
            need(advance_survey_commission(account, {8}, 3703, true)) ==
                account,
        "Completion converges from identical state and freezes original "
        "provenance, including after craft replacement");
  check(!submit_survey_commission(account, first_sequence + 1, 3703),
        "Submission requires the exact currently accepted serial");
  account = need(submit_survey_commission(account, first_sequence, 3703));
  check(!account.active && account.paid_count == 1 &&
            account.latest_paid->commission.sequence == first_sequence &&
            account.latest_paid->submitted_tick == 3703 &&
            survey_commission_earnings(account) == 100 &&
            !submit_survey_commission(account, first_sequence, 3703),
        "Explicit payout atomically closes one serial and cannot be repeated");
  account = need(accept_survey_commission(account, {2}, {3}, 3703));
  check(account.active->sequence == first_sequence + 1 &&
            !account.active->measurement &&
            survey_commission_earnings(account) == 100,
        "Same-hour same-target repeat has a new serial and no reused work");
  account = acquire(account, 3704);
  account = need(submit_survey_commission(account, first_sequence + 1, 7304));
  check(
      account.paid_count == 2 && survey_commission_earnings(account) == 200 &&
          account.latest_paid->commission.sequence == first_sequence + 1 &&
          validate_survey_commissions(account, 7304).has_value(),
      "Fresh repeat work increases lifetime earnings with one latest receipt");
  const auto paid = account.latest_paid;
  account = need(accept_survey_commission(account, {2}, {3}, 7304));
  const auto sequence = account.active->sequence;
  check(!abandon_survey_commission(account, sequence - 1, 7304),
        "Stale abandonment cannot discard current work");
  account = need(abandon_survey_commission(account, sequence, 7304));
  check(!account.active && account.latest_paid == paid &&
            survey_commission_earnings(account) == 200,
        "Abandonment changes neither earned balance nor latest settlement");
}
auto expiry_and_invalid_state() -> void {
  auto account = need(accept_survey_commission({}, {2}, {3}, 0));
  account = acquire(account, 1);
  const auto deadline = account.active->deadline_tick;
  check(submit_survey_commission(account, 1, deadline - 1).has_value() &&
            !submit_survey_commission(account, 1, deadline) &&
            need(advance_survey_commission(account, {7}, deadline, true)) ==
                account &&
            validate_survey_commissions(account, deadline).has_value(),
        "Exact accepted deadline expires completed reports without mutation");
  auto next = need(accept_survey_commission(account, {2}, {3}, deadline));
  check(next.active->sequence == 2 && !next.active->measurement &&
            next.paid_count == 0 && !next.latest_paid,
        "Expired replacement creates fresh unpaid work, not a revived receipt");
  auto partial = need(accept_survey_commission({}, {2}, {3}, 0));
  partial = need(advance_survey_commission(partial, {7}, 1, true));
  check(need(advance_survey_commission(partial, {7}, deadline, true)) ==
            partial,
        "Expiry holds unfinished acquisition as well as completed reports");
  const auto paid = need(submit_survey_commission(account, 1, 3601));
  for (unsigned kind = 0; kind < 14; ++kind) {
    auto bad = paid;
    auto& receipt = *bad.latest_paid;
    auto& measurement = *receipt.commission.measurement;
    switch (kind) {
      case 0: bad.version = 3; break;
      case 1: bad.next_sequence = 0; break;
      case 2: bad.paid_count = bad.next_sequence; break;
      case 3: bad.paid_count = 0; break;
      case 4: receipt.commission.system = {}; break;
      case 5: ++receipt.commission.deadline_tick; break;
      case 6: measurement.craft = {}; break;
      case 7:
        measurement.started_tick = receipt.commission.accepted_tick;
        break;
      case 8: measurement.last_tick = measurement.started_tick - 1; break;
      case 9: measurement.acquired_ticks = 3601; break;
      case 10: measurement.acquired_ticks = 3599; break;
      case 11: receipt.submitted_tick = measurement.last_tick - 1; break;
      case 12: receipt.submitted_tick = receipt.commission.deadline_tick; break;
      case 13: receipt.commission.sequence = bad.next_sequence; break;
      default: throw std::runtime_error("Invalid malformed-account case");
    }
    check(!validate_survey_commissions(bad, deadline),
          "Malformed account, receipt or measurement chronology is rejected");
  }
  auto bad = paid;
  bad.latest_paid.reset();
  check(!validate_survey_commissions(bad, deadline),
        "A nonzero paid count must retain its latest complete receipt");
  bad = need(accept_survey_commission(paid, {2}, {3}, 3601));
  --bad.active->accepted_tick;
  --bad.active->deadline_tick;
  check(!validate_survey_commissions(bad, deadline),
        "New work cannot predate the preceding settlement");
  bad = need(accept_survey_commission(paid, {2}, {3}, 3601));
  bad.active->sequence = 1;
  check(!validate_survey_commissions(bad, deadline),
        "An active commission cannot reuse a paid serial");
  bad = need(accept_survey_commission({}, {2}, {3}, 0));
  bad.active->measurement = SurveyCommissionMeasurement{{7}, 10, 10, 2};
  check(!validate_survey_commissions(bad, 10),
        "One processed endpoint cannot claim two qualifying intervals");
  check(!validate_survey_commissions(paid, 3600),
        "A receipt cannot be loaded before its submitted tick");
}
auto chosen_target_terms() -> void {
  const auto chosen =
      need(accept_targeted_survey_commission({}, {4}, {5}, 100));
  check(chosen.version == 3 &&
            chosen.active->target_policy ==
                SurveyCommissionTargetPolicy::charted_waypoint &&
            chosen.active->deadline_tick ==
                100 + kSurveyCommissionReservationTicks,
        "Alternative acceptance alone selects version3 with unchanged terms");
  const auto deadline = chosen.active->deadline_tick;
  check(!accept_targeted_survey_commission(chosen, {4}, {5}, deadline - 1) &&
            !accept_targeted_survey_commission(chosen, {6}, {5}, deadline) &&
            !accept_targeted_survey_commission(chosen, {4}, {6}, deadline),
        "Accepted destination stays locked even at deadline equality");
  const auto renewed =
      need(accept_targeted_survey_commission(chosen, {4}, {5}, deadline));
  check(renewed.version == 3 && renewed.active->sequence == 2 &&
            renewed.active->accepted_tick == deadline &&
            !renewed.active->measurement &&
            renewed.active->target_policy == chosen.active->target_policy,
        "Same-target expired renewal retains policy and requires fresh work");
  for (unsigned fault = 0; fault < 5; ++fault) {
    auto bad = chosen;
    if (fault == 0) bad.version = 1;
    if (fault == 1) bad.version = 2;
    if (fault == 2) bad.active->kind = SurveyCommissionKind::geological;
    if (fault == 3)
      bad.active->target_policy = static_cast<SurveyCommissionTargetPolicy>(0);
    if (fault == 4)
      bad.active->target_policy = static_cast<SurveyCommissionTargetPolicy>(3);
    check(!validate_survey_commissions(bad, 100),
          "Unknown policies, old-policy containers and geological alternatives "
          "refuse");
  }
  const auto ready = acquire(chosen, 101);
  const auto paid = need(submit_survey_commission(ready, 1, 3700));
  check(paid.version == 3 && paid.paid_count == 1 &&
            paid.latest_paid->commission == *ready.active &&
            survey_commission_earnings(paid) == 100 &&
            !submit_survey_commission(paid, 1, 3700),
        "Chosen terms pay the unchanged reward once with exact retained "
        "provenance");
  const auto geo =
      need(accept_geological_survey_commission(paid, {2}, {3}, 3700));
  const auto mixed =
      need(submit_geological_survey_commission(geo, 2, 3701, specimen(3701)));
  check(mixed.version == 3 && mixed.geological_paid_count == 1 &&
            mixed.paid_count == 2 &&
            mixed.latest_paid->commission.target_policy ==
                SurveyCommissionTargetPolicy::legacy &&
            mixed.latest_paid->delivered_specimen == specimen(3701),
        "Geological delivery after alternative work preserves version3 and "
        "both income counters");
  auto legacy = need(accept_survey_commission({}, {2}, {3}, 0));
  check(!accept_targeted_survey_commission(legacy, {4}, {5},
                                           legacy.active->deadline_tick),
        "Expired legacy work requires explicit abandonment before changing "
        "target");
  legacy = need(abandon_survey_commission(legacy, 1, 0));
  check(need(accept_targeted_survey_commission(legacy, {4}, {5}, 0)).version ==
            3,
        "Explicit abandonment permits chosen-target enrollment without time "
        "advancement");
}
} // namespace
int main() {
  try {
    geological_invalid_boundaries();
    chosen_target_terms();
    boundaries();
    acquisition_and_payment();
    expiry_and_invalid_state();
    geological_payment_and_mixed_history();
    std::cout << "Survey commissions: " << checks << " checks, 0 failures\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Survey commission failure after " << checks
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
