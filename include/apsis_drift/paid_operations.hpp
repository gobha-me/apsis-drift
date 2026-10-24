#pragma once

#include "apsis_drift/simulation.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace apsis_drift {
inline constexpr std::uint32_t kPaidOperationsVersion{1};
inline constexpr std::uint32_t kPaidSpecimenOperationsVersion{2};
inline constexpr std::uint32_t kPaidDerelictOperationsVersion{3};
inline constexpr std::uint64_t kPaidCanisterCredits{100};
inline constexpr std::uint64_t kPaidSpecimenCredits{40};
inline constexpr std::uint32_t kPaidSpecimenBatchLimit{4};

struct PaidIncomeCounts {
  std::uint32_t survey_paid{}, specimen_sold{}, canister_sold{};
  friend auto operator==(const PaidIncomeCounts&, const PaidIncomeCounts&)
      -> bool = default;
};
inline constexpr std::uint64_t kPaidOpeningCredits{200};
inline constexpr std::uint64_t kPaidInitialBerthHours{4};
inline constexpr std::uint64_t kPaidBerthCreditsPerHour{5};
inline constexpr SimulationTick kPaidHourTicks{432'000};
inline constexpr std::uint64_t kPaidFullFlightCredits{40};
inline constexpr std::uint64_t kPaidJumpChargeCredits{15};
inline constexpr std::uint64_t kPaidOperatingTabCap{200};
inline constexpr std::uint64_t kPaidRecoveryExcessCredits{100};
inline constexpr std::uint64_t kPaidSurveyRewardCredits{100};

struct PaidOperationsAccount {
  std::uint32_t version{kPaidOperationsVersion};
  SimulationTick started_tick{};
  // Covers [start, start + billed_through_hours * hour). Initially prepaid.
  // Posting can be deferred; effective views derive all newly due periods.
  std::uint64_t billed_through_hours{kPaidInitialBerthHours};
  std::uint64_t spent_credits{kPaidInitialBerthHours *
                              kPaidBerthCreditsPerHour};
  std::uint64_t operating_tab{}, recovery_due{};
  std::uint32_t settled_paid_count{}, supported_sequence{};
  std::uint64_t charged_generation{};
  // Versions2/3. Legacy version1 persists exactly its original nine keys.
  std::uint32_t settled_specimen_count{};
  // Version3 only: the single derelict source can be sold at most once.
  std::uint32_t settled_canister_count{};
  friend auto operator==(const PaidOperationsAccount&,
                         const PaidOperationsAccount&) -> bool = default;
};
struct PaidRetainerOutcome {
  PaidOperationsAccount account;
  std::uint64_t service_credits{}, berth_credits{}, cash_debit{}, tab_added{},
      subsidized_credits{};
  friend auto operator==(const PaidRetainerOutcome&, const PaidRetainerOutcome&)
      -> bool = default;
};

// Pure purpose-specific accounting. The native owner supplies actual source
// count/generation, admission, quantities, fresh work and quote identities.
[[nodiscard]] auto validate_paid_operations(const PaidOperationsAccount&,
                                            std::uint32_t paid_count,
                                            SimulationTick now,
                                            std::uint64_t generation)
    -> std::expected<void, std::string>;
[[nodiscard]] auto paid_operations_cash(const PaidOperationsAccount&,
                                        std::uint32_t paid_count)
    -> std::expected<std::uint64_t, std::string>;
[[nodiscard]] auto make_paid_operations(std::uint32_t paid_count,
                                        SimulationTick now,
                                        std::uint64_t generation)
    -> std::expected<PaidOperationsAccount, std::string>;
[[nodiscard]] auto reconcile_paid_berth(const PaidOperationsAccount&,
                                        std::uint32_t paid_count,
                                        SimulationTick now,
                                        std::uint64_t generation)
    -> std::expected<PaidOperationsAccount, std::string>;
[[nodiscard]] auto paid_fuel_price(std::uint64_t missing_flight_quanta,
                                   std::uint64_t capacity_quanta,
                                   std::uint32_t missing_jump_charges)
    -> std::expected<std::uint64_t, std::string>;
[[nodiscard]] auto purchase_paid_service(const PaidOperationsAccount&,
                                         std::uint32_t paid_count,
                                         SimulationTick now,
                                         std::uint64_t generation,
                                         std::uint64_t service_credits)
    -> std::expected<PaidOperationsAccount, std::string>;
[[nodiscard]] auto fund_paid_retainer(
    const PaidOperationsAccount&, std::uint32_t paid_count, SimulationTick now,
    std::uint64_t generation, std::uint32_t commission_sequence,
    SimulationTick commission_deadline, std::uint64_t service_credits)
    -> std::expected<PaidRetainerOutcome, std::string>;
[[nodiscard]] auto settle_paid_survey_income(const PaidOperationsAccount&,
                                             std::uint32_t new_paid_count,
                                             SimulationTick now,
                                             std::uint64_t generation)
    -> std::expected<PaidOperationsAccount, std::string>;
// Matching generation is an unchanged idempotent replay. Only one successive
// actual replacement is chargeable; no retrospective fees on enrollment.
[[nodiscard]] auto charge_paid_recovery(const PaidOperationsAccount&,
                                        std::uint32_t paid_count,
                                        SimulationTick now,
                                        std::uint64_t replacement_generation)
    -> std::expected<PaidOperationsAccount, std::string>;
// Concrete earning sources. Legacy overloads above preserve survey-only
// version1 semantics; v2 is selected only by an actual specimen sale or
// explicit enrollment with prior specimen earnings, never by reading/loading.
[[nodiscard]] auto validate_paid_operations(const PaidOperationsAccount&,
                                            PaidIncomeCounts, SimulationTick,
                                            std::uint64_t generation)
    -> std::expected<void, std::string>;
[[nodiscard]] auto paid_operations_cash(const PaidOperationsAccount&,
                                        PaidIncomeCounts)
    -> std::expected<std::uint64_t, std::string>;
[[nodiscard]] auto make_paid_operations(PaidIncomeCounts, SimulationTick,
                                        std::uint64_t generation)
    -> std::expected<PaidOperationsAccount, std::string>;
[[nodiscard]] auto reconcile_paid_berth(const PaidOperationsAccount&,
                                        PaidIncomeCounts, SimulationTick,
                                        std::uint64_t generation)
    -> std::expected<PaidOperationsAccount, std::string>;
[[nodiscard]] auto purchase_paid_service(const PaidOperationsAccount&,
                                         PaidIncomeCounts, SimulationTick,
                                         std::uint64_t generation,
                                         std::uint64_t service_credits)
    -> std::expected<PaidOperationsAccount, std::string>;
[[nodiscard]] auto fund_paid_retainer(const PaidOperationsAccount&,
                                      PaidIncomeCounts, SimulationTick,
                                      std::uint64_t generation,
                                      std::uint32_t sequence,
                                      SimulationTick deadline,
                                      std::uint64_t service_credits)
    -> std::expected<PaidRetainerOutcome, std::string>;
[[nodiscard]] auto settle_paid_survey_income(const PaidOperationsAccount&,
                                             PaidIncomeCounts new_counts,
                                             SimulationTick,
                                             std::uint64_t generation)
    -> std::expected<PaidOperationsAccount, std::string>;
[[nodiscard]] auto settle_paid_specimen_income(const PaidOperationsAccount&,
                                               PaidIncomeCounts new_counts,
                                               SimulationTick,
                                               std::uint64_t generation,
                                               std::uint32_t sold_units)
    -> std::expected<PaidOperationsAccount, std::string>;
[[nodiscard]] auto settle_paid_canister_income(const PaidOperationsAccount&,
                                               PaidIncomeCounts new_counts,
                                               SimulationTick,
                                               std::uint64_t generation)
    -> std::expected<PaidOperationsAccount, std::string>;
[[nodiscard]] auto charge_paid_recovery(const PaidOperationsAccount&,
                                        PaidIncomeCounts, SimulationTick,
                                        std::uint64_t replacement_generation)
    -> std::expected<PaidOperationsAccount, std::string>;
} // namespace apsis_drift
