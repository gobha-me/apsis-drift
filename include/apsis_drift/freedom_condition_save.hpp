#pragma once

#include "apsis_drift/craft_condition.hpp"
#include "apsis_drift/freedom_surface_walk_save.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomConditionSaveFormatVersion{34};
using FreedomConditionVoyage =
    std::variant<FreedomKnowledgeSaveDocument, FreedomTravelSaveDocument,
                 FreedomRecoverySaveDocument, FreedomSurfaceWalkSaveDocument>;
struct FreedomConditionSaveDocument {
  std::uint32_t consequence_version{kImpactConsequencesVersion};
  FreedomConditionVoyage voyage;
  CraftCondition condition;
  std::optional<CraftCondition> retired_condition;
  friend auto operator==(const FreedomConditionSaveDocument&,
                         const FreedomConditionSaveDocument&) -> bool = default;
};
[[nodiscard]] auto condition_flight(const FreedomConditionVoyage&)
    -> FreedomFlightSaveDocument;
[[nodiscard]] auto validate_freedom_condition_document(
    const FreedomConditionSaveDocument&)
    -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto encode_freedom_condition_document_json(
    const FreedomConditionSaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_freedom_condition_document_json(std::string_view)
    -> std::expected<FreedomConditionSaveDocument, SaveSchemaError>;
} // namespace apsis_drift
