#pragma once

#include "apsis_drift/freedom_resource_save.hpp"
#include <nlohmann/json.hpp>

namespace apsis_drift::detail {
[[nodiscard]] auto resource_json(const FreedomResources&)
    -> nlohmann::ordered_json;
[[nodiscard]] auto resource_value(const nlohmann::ordered_json&)
    -> std::expected<FreedomResources, SaveSchemaError>;
[[nodiscard]] auto drive_json(const FreedomDriveRecoveryState&)
    -> nlohmann::ordered_json;
[[nodiscard]] auto drive_value(const nlohmann::ordered_json&, SimulationTick)
    -> std::expected<FreedomDriveRecoveryState, SaveSchemaError>;
[[nodiscard]] auto uncertainty_json(const FrozenFreedomJumpUncertainty&)
    -> nlohmann::ordered_json;
[[nodiscard]] auto uncertainty_value(const nlohmann::ordered_json&)
    -> std::expected<FrozenFreedomJumpUncertainty, SaveSchemaError>;
} // namespace apsis_drift::detail
