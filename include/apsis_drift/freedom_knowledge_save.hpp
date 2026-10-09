#pragma once

#include "apsis_drift/freedom_knowledge.hpp"
#include "apsis_drift/freedom_resource_save.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomKnowledgeSaveFormatVersion{25};
inline constexpr std::size_t kMaximumFreedomKnowledgeBytes{65'536};
struct FreedomKnowledgeSaveDocument {
  FreedomResourceSaveDocument voyage;
  FreedomKnowledge knowledge;
  friend auto operator==(const FreedomKnowledgeSaveDocument&,
                         const FreedomKnowledgeSaveDocument&) -> bool = default;
};
[[nodiscard]] auto encode_freedom_knowledge_json(const FreedomKnowledge&,
                                                 SimulationTick now)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_freedom_knowledge_json(std::string_view,
                                                 SimulationTick now)
    -> std::expected<FreedomKnowledge, SaveSchemaError>;
[[nodiscard]] auto validate_freedom_knowledge_document(
    const FreedomKnowledgeSaveDocument&)
    -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto encode_freedom_knowledge_document_json(
    const FreedomKnowledgeSaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_freedom_knowledge_document_json(std::string_view)
    -> std::expected<FreedomKnowledgeSaveDocument, SaveSchemaError>;
} // namespace apsis_drift
