#include "apsis_drift/survey_commissions.hpp"

#include <limits>

namespace apsis_drift {
namespace {
auto validate_commission(const SurveyCommission& commission, SimulationTick now)
    -> std::expected<void, std::string> {
  if (!commission.sequence || !commission.system.value ||
      !commission.planet.value || commission.accepted_tick > now ||
      commission.deadline_tick < commission.accepted_tick ||
      commission.deadline_tick - commission.accepted_tick !=
          kSurveyCommissionReservationTicks)
    return std::unexpected{"Invalid commission identity or reservation"};
  if (!commission.measurement) return {};
  const auto& measurement = *commission.measurement;
  if (!measurement.craft.value ||
      measurement.started_tick <= commission.accepted_tick ||
      measurement.last_tick < measurement.started_tick ||
      measurement.last_tick > now ||
      measurement.last_tick >= commission.deadline_tick ||
      measurement.acquired_ticks > kSurveyCommissionMeasurementTicks ||
      measurement.acquired_ticks >
          measurement.last_tick - measurement.started_tick + 1)
    return std::unexpected{"Invalid commission measurement chronology"};
  return {};
}
auto complete(const SurveyCommission& commission) -> bool {
  return commission.measurement && commission.measurement->acquired_ticks ==
                                       kSurveyCommissionMeasurementTicks;
}
} // namespace

auto validate_survey_commissions(const SurveyCommissionAccount& account,
                                 SimulationTick now)
    -> std::expected<void, std::string> {
  if (now == std::numeric_limits<SimulationTick>::max() ||
      account.version != kSurveyCommissionVersion || !account.next_sequence ||
      account.paid_count >= account.next_sequence ||
      account.latest_paid.has_value() != (account.paid_count != 0))
    return std::unexpected{"Invalid commission account version or counters"};
  if (account.latest_paid) {
    const auto& receipt = *account.latest_paid;
    if (auto valid = validate_commission(receipt.commission, now); !valid)
      return valid;
    if (!complete(receipt.commission) ||
        receipt.commission.sequence >= account.next_sequence ||
        receipt.commission.sequence < account.paid_count ||
        receipt.submitted_tick < receipt.commission.measurement->last_tick ||
        receipt.submitted_tick > now ||
        receipt.submitted_tick >= receipt.commission.deadline_tick)
      return std::unexpected{"Invalid paid commission receipt"};
  }
  if (account.active) {
    const auto& active = *account.active;
    if (auto valid = validate_commission(active, now); !valid) return valid;
    if (active.sequence != account.next_sequence - 1 ||
        (account.latest_paid &&
         (active.sequence <= account.latest_paid->commission.sequence ||
          active.accepted_tick < account.latest_paid->submitted_tick)))
      return std::unexpected{"Invalid active commission sequence"};
  }
  return {};
}

auto survey_commission_offer_deadline(SimulationTick now)
    -> std::expected<SimulationTick, std::string> {
  const auto remaining =
      kSurveyCommissionHourTicks - now % kSurveyCommissionHourTicks;
  if (now > std::numeric_limits<SimulationTick>::max() - remaining)
    return std::unexpected{"Commission offer epoch would overflow"};
  return now + remaining;
}

auto accept_survey_commission(const SurveyCommissionAccount& account,
                              SystemId system, PlanetId planet,
                              SimulationTick now)
    -> std::expected<SurveyCommissionAccount, std::string> {
  if (auto valid = validate_survey_commissions(account, now); !valid)
    return std::unexpected{valid.error()};
  if (!system.value || !planet.value ||
      account.next_sequence == std::numeric_limits<std::uint32_t>::max() ||
      now > std::numeric_limits<SimulationTick>::max() -
                kSurveyCommissionReservationTicks)
    return std::unexpected{"Commission target, sequence or clock refused"};
  if (account.active && now < account.active->deadline_tick)
    return std::unexpected{"An unexpired commission is already accepted"};
  auto next = account;
  next.active = SurveyCommission{account.next_sequence,
                                 system,
                                 planet,
                                 now,
                                 now + kSurveyCommissionReservationTicks,
                                 {}};
  ++next.next_sequence;
  return next;
}

auto advance_survey_commission(const SurveyCommissionAccount& account,
                               StarterCraftId craft, SimulationTick now,
                               bool qualifies)
    -> std::expected<SurveyCommissionAccount, std::string> {
  if (auto valid = validate_survey_commissions(account, now); !valid)
    return std::unexpected{valid.error()};
  if (!account.active || now <= account.active->accepted_tick ||
      now >= account.active->deadline_tick || complete(*account.active))
    return account;
  if (!craft.value)
    return std::unexpected{"Commission measurement requires a source craft"};
  if (account.active->measurement) {
    if (account.active->measurement->craft != craft)
      return std::unexpected{"Unfinished commission source craft differs"};
    if (account.active->measurement->last_tick == now) return account;
  }
  auto next = account;
  auto& measurement = next.active->measurement;
  if (!measurement)
    measurement = SurveyCommissionMeasurement{craft, now, now, 0};
  measurement->last_tick = now;
  if (qualifies) ++measurement->acquired_ticks;
  return next;
}

auto submit_survey_commission(const SurveyCommissionAccount& account,
                              std::uint32_t expected_sequence,
                              SimulationTick now)
    -> std::expected<SurveyCommissionAccount, std::string> {
  if (auto valid = validate_survey_commissions(account, now); !valid)
    return std::unexpected{valid.error()};
  if (!account.active || account.active->sequence != expected_sequence ||
      !complete(*account.active) || now >= account.active->deadline_tick ||
      account.paid_count == std::numeric_limits<std::uint32_t>::max())
    return std::unexpected{"Commission payout is unavailable or stale"};
  auto next = account;
  next.latest_paid = SurveyCommissionReceipt{*account.active, now};
  next.active.reset();
  ++next.paid_count;
  return next;
}

auto abandon_survey_commission(const SurveyCommissionAccount& account,
                               std::uint32_t expected_sequence,
                               SimulationTick now)
    -> std::expected<SurveyCommissionAccount, std::string> {
  if (auto valid = validate_survey_commissions(account, now); !valid)
    return std::unexpected{valid.error()};
  if (!account.active || account.active->sequence != expected_sequence)
    return std::unexpected{"Commission abandonment is unavailable or stale"};
  auto next = account;
  next.active.reset();
  return next;
}
} // namespace apsis_drift
