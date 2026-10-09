#pragma once

#include "apsis_drift/freedom_resources.hpp"
#include "apsis_drift/freedom_surface_save.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomResourceSaveFormatVersion{24};
// A single voyage owns world/history/physics; the surface layer can be
// inactive. Capacity is recipe-derived. No rounding remainder is needed for
// integer Q.
struct FreedomResourceSaveDocument {
  FreedomSurfaceSaveDocument voyage;
  FreedomResources resources;
  friend auto operator==(const FreedomResourceSaveDocument&,
                         const FreedomResourceSaveDocument&) -> bool = default;
};
[[nodiscard]] auto validate_freedom_resource_document(
    const FreedomResourceSaveDocument&) -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto encode_freedom_resource_document_json(
    const FreedomResourceSaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_freedom_resource_document_json(std::string_view)
    -> std::expected<FreedomResourceSaveDocument, SaveSchemaError>;
} // namespace apsis_drift
