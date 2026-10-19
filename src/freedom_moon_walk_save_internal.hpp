#pragma once

#include <nlohmann/json.hpp>

#include "apsis_drift/freedom_surface_walk_save.hpp"

namespace apsis_drift::detail {
[[nodiscard]] auto validate_moon_walk(const FreedomSurfaceWalkSaveDocument&)
    -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto encode_moon_walk(const FreedomSurfaceWalkSaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
// Called after public size/nesting/duplicate-key and explicit format44 checks.
[[nodiscard]] auto decode_moon_walk(const nlohmann::ordered_json&)
    -> std::expected<FreedomSurfaceWalkSaveDocument, SaveSchemaError>;
} // namespace apsis_drift::detail
