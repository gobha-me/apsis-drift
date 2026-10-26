#pragma once

#include "apsis_drift/freedom_docking_save.hpp"
#include "apsis_drift/terrain_impact.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kCraftConditionVersion{1};
inline constexpr std::uint32_t kImpactConsequencesVersion{1};
inline constexpr std::uint32_t kRepairedCraftConditionVersion{2};
inline constexpr std::uint32_t kCraftRepairReceiptVersion{1};
inline constexpr std::uint64_t kCraftOriginRepairUnitCredits{10};
inline constexpr std::uint8_t kCraftOriginRepairMaximumUnits{6};
struct CraftImpactReceipt {
  std::uint64_t serial{}, source_condition_digest{};
  TerrainImpactWitness witness;
  std::uint8_t damage_class{};
  bool catastrophic{};
  friend auto operator==(const CraftImpactReceipt&, const CraftImpactReceipt&)
      -> bool = default;
};
// A bounded source projection, never a recursive older condition or voyage.
struct CraftRepairSource {
  std::uint8_t forebody{}, landing_structure{};
  bool contact_armed{};
  std::uint8_t clear_ticks{};
  std::uint64_t next_event{};
  CraftImpactReceipt latest;
  friend auto operator==(const CraftRepairSource&, const CraftRepairSource&)
      -> bool = default;
};
struct CraftRepairPayment {
  bool paid{};
  SimulationTick started_tick{};
  std::uint64_t spent_after{};
  friend auto operator==(const CraftRepairPayment&, const CraftRepairPayment&)
      -> bool = default;
};
struct CraftRepairReceipt {
  std::uint32_t version{kCraftRepairReceiptVersion};
  OriginDockConstraint attachment;
  std::uint64_t service_pose_checksum{};
  CraftRepairSource before;
  // Hash of immutable owner and this bounded receipt, not a lifetime proof.
  std::uint64_t source_projection_digest{};
  CraftRepairPayment payment;
  std::uint64_t credits{};
  friend auto operator==(const CraftRepairReceipt&, const CraftRepairReceipt&)
      -> bool = default;
};
struct CraftCondition {
  std::uint32_t version{kCraftConditionVersion};
  StarterCraftId craft;
  CraftFrameRecipe frame;
  std::optional<FreedomCraftLineage> lineage;
  SimulationTick tick{};
  std::uint8_t forebody{}, landing_structure{};
  bool contact_armed{true};
  std::uint8_t clear_ticks{};
  std::uint64_t next_event{1};
  std::optional<CraftImpactReceipt> latest;
  std::optional<CraftRepairReceipt> latest_repair;
  friend auto operator==(const CraftCondition&, const CraftCondition&)
      -> bool = default;
};
struct CraftConditionReduction {
  CraftCondition condition;
  bool event_applied{}, catastrophic{};
};
[[nodiscard]] auto make_craft_condition(const FreedomFlightSaveDocument&)
    -> std::expected<CraftCondition, std::string>;
[[nodiscard]] auto validate_craft_condition(const CraftCondition&,
                                            const FreedomFlightSaveDocument&)
    -> std::expected<void, std::string>;
[[nodiscard]] auto craft_condition_digest(const CraftCondition&)
    -> std::expected<std::uint64_t, std::string>;
[[nodiscard]] auto reduce_craft_condition(
    const CraftCondition&, const FreedomFlightSaveDocument& before,
    const FreedomFlightSaveDocument& candidate, const TerrainImpactSweep&)
    -> std::expected<CraftConditionReduction, std::string>;
[[nodiscard]] auto encode_craft_condition_json(const CraftCondition&)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_craft_condition_json(std::string_view)
    -> std::expected<CraftCondition, SaveSchemaError>;
// Explicit same-tick functional repair at an authenticated attached Origin
// port. The caller owns payment/account admission and complete-Save commit.
// Pristine/catastrophic/invalid sources refuse. Existing impacts and rearm
// state remain exact; only successful repair selects condition version two.
[[nodiscard]] auto craft_origin_repair_price(const CraftCondition&)
    -> std::expected<std::uint64_t, std::string>;
[[nodiscard]] auto repair_craft_condition(const CraftCondition&,
                                          const FreedomDockingSaveDocument&,
                                          CraftRepairPayment = {})
    -> std::expected<CraftCondition, std::string>;
[[nodiscard]] inline auto condition_allows_landing(const CraftCondition& c)
    -> bool {
  return c.landing_structure < 2;
}
[[nodiscard]] inline auto condition_view_cracked(const CraftCondition& c)
    -> bool {
  return c.forebody >= 1;
}
[[nodiscard]] inline auto condition_visibility_degraded(const CraftCondition& c)
    -> bool {
  return c.forebody >= 2;
}
} // namespace apsis_drift
