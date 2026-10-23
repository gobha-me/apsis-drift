#pragma once

#include <nlohmann/json.hpp>

#include "apsis_drift/freedom_travel_save.hpp"

namespace apsis_drift::detail {
[[nodiscard]] auto validate_composed_travel(const FreedomTravelSaveDocument&)
    -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto encode_composed_travel(const FreedomTravelSaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
// The public parser has already rejected duplicate keys, excessive depth and
// byte overflow. This decoder owns the closed explicit56/57/58/61/63 envelopes.
[[nodiscard]] auto decode_composed_travel(const nlohmann::ordered_json&)
    -> std::expected<FreedomTravelSaveDocument, SaveSchemaError>;
} // namespace apsis_drift::detail
