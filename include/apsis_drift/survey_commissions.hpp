#pragma once

#include "apsis_drift/freedom_save.hpp"
#include "apsis_drift/intersystem_contract.hpp"
#include "apsis_drift/planet.hpp"
#include "apsis_drift/simulation.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace apsis_drift {
inline constexpr std::uint32_t kSurveyCommissionVersion{1};
inline constexpr std::uint32_t kSurveyCommissionMeasurementTicks{3600};
inline constexpr SimulationTick kSurveyCommissionHourTicks{432'000};
inline constexpr SimulationTick kSurveyCommissionReservationTicks{
    4 * kSurveyCommissionHourTicks};
inline constexpr std::uint64_t kSurveyCommissionRewardCredits{100};

struct SurveyCommissionMeasurement {
  StarterCraftId craft;
  // Endpoints of processed, committed flight intervals, including held ones.
  // The first is strictly after acceptance. Counting is inclusive: at most
  // last_tick - started_tick + 1 intervals can have qualified.
  SimulationTick started_tick{}, last_tick{};
  std::uint32_t acquired_ticks{};
  friend auto operator==(const SurveyCommissionMeasurement&,
                         const SurveyCommissionMeasurement&) -> bool = default;
};
struct SurveyCommission {
  std::uint32_t sequence{};
  SystemId system;
  PlanetId planet;
  SimulationTick accepted_tick{}, deadline_tick{};
  std::optional<SurveyCommissionMeasurement> measurement;
  friend auto operator==(const SurveyCommission&, const SurveyCommission&)
      -> bool = default;
};
struct SurveyCommissionReceipt {
  SurveyCommission commission;
  SimulationTick submitted_tick{};
  friend auto operator==(const SurveyCommissionReceipt&,
                         const SurveyCommissionReceipt&) -> bool = default;
};
struct SurveyCommissionAccount {
  std::uint32_t version{kSurveyCommissionVersion};
  std::uint32_t next_sequence{1}, paid_count{};
  std::optional<SurveyCommission> active;
  std::optional<SurveyCommissionReceipt> latest_paid;
  friend auto operator==(const SurveyCommissionAccount&,
                         const SurveyCommissionAccount&) -> bool = default;
};

// A personal ledger, independent of craft replacement. World membership,
// historical craft lineage, actual Origin eligibility and the displayed offer
// token are authenticated by the owning knowledge/session layers.
[[nodiscard]] auto validate_survey_commissions(const SurveyCommissionAccount&,
                                               SimulationTick now)
    -> std::expected<void, std::string>;
[[nodiscard]] auto survey_commission_offer_deadline(SimulationTick now)
    -> std::expected<SimulationTick, std::string>;
[[nodiscard]] constexpr auto survey_commission_earnings(
    const SurveyCommissionAccount& account) -> std::uint64_t {
  return static_cast<std::uint64_t>(account.paid_count) *
         kSurveyCommissionRewardCredits;
}
[[nodiscard]] auto accept_survey_commission(const SurveyCommissionAccount&,
                                            SystemId, PlanetId,
                                            SimulationTick now)
    -> std::expected<SurveyCommissionAccount, std::string>;
// Call once after an ordinary committed flight interval. Repeating the same
// tick, holding conditions, completion or expiry never adds progress. Once
// present, measurement source remains fixed until replacement discards an
// unfinished acquisition; completed provenance is immutable.
[[nodiscard]] auto advance_survey_commission(const SurveyCommissionAccount&,
                                             StarterCraftId, SimulationTick now,
                                             bool qualifies)
    -> std::expected<SurveyCommissionAccount, std::string>;
[[nodiscard]] auto submit_survey_commission(const SurveyCommissionAccount&,
                                            std::uint32_t expected_sequence,
                                            SimulationTick now)
    -> std::expected<SurveyCommissionAccount, std::string>;
[[nodiscard]] auto abandon_survey_commission(const SurveyCommissionAccount&,
                                             std::uint32_t expected_sequence,
                                             SimulationTick now)
    -> std::expected<SurveyCommissionAccount, std::string>;
} // namespace apsis_drift
