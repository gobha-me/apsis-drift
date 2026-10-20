#pragma once

#include <nlohmann/json.hpp>

#include "apsis_drift/freedom_travel_save.hpp"

namespace apsis_drift::detail {
[[nodiscard]] auto validate_local_travel(const FreedomTravelSaveDocument&)
    -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto encode_local_travel(const FreedomTravelSaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
// Public travel parsing has already bounded depth/bytes and rejected duplicate
// keys before dispatching the explicit local formats45/47/48.
[[nodiscard]] auto decode_local_travel(const nlohmann::ordered_json&)
    -> std::expected<FreedomTravelSaveDocument, SaveSchemaError>;

// Reuse the historical travel serialization unchanged for the retained prior
// intersystem commitment, its paired bill, and the pinned authored selection.
[[nodiscard]] auto travel_frozen_json(const FrozenFreedomJumpArrival&)
    -> std::expected<nlohmann::ordered_json, SaveSchemaError>;
[[nodiscard]] auto travel_frozen_value(const nlohmann::ordered_json&)
    -> std::expected<FrozenFreedomJumpArrival, SaveSchemaError>;
[[nodiscard]] auto travel_resources_json(const FreedomResources&)
    -> nlohmann::ordered_json;
[[nodiscard]] auto travel_resources_value(const nlohmann::ordered_json&)
    -> std::expected<FreedomResources, SaveSchemaError>;
[[nodiscard]] auto travel_binding_json(const NativeStartingAssemblySelection&)
    -> nlohmann::ordered_json;
} // namespace apsis_drift::detail
