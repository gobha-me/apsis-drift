#pragma once

#include <nlohmann/json.hpp>

#include "apsis_drift/freedom_surface_save.hpp"

namespace apsis_drift::detail {
[[nodiscard]] auto validate_moon_surface(const FreedomSurfaceSaveDocument&)
    -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto encode_moon_surface(const FreedomSurfaceSaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
// Called after public size/nesting/duplicate-key and format43 checks.
[[nodiscard]] auto decode_moon_surface(const nlohmann::ordered_json&)
    -> std::expected<FreedomSurfaceSaveDocument, SaveSchemaError>;
} // namespace apsis_drift::detail
