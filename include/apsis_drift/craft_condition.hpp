#pragma once

#include "apsis_drift/terrain_impact.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kCraftConditionVersion{1};
inline constexpr std::uint32_t kImpactConsequencesVersion{1};
struct CraftImpactReceipt {
  std::uint64_t serial{}, source_condition_digest{};
  TerrainImpactWitness witness;
  std::uint8_t damage_class{};
  bool catastrophic{};
  friend auto operator==(const CraftImpactReceipt&, const CraftImpactReceipt&)
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
