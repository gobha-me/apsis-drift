#pragma once

#include "apsis_drift/origin_service_craft.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace apsis_drift {
inline constexpr std::size_t kMaximumOriginServiceStateBytes{std::size_t{16} *
                                                             1024U};
// Selected service state only. The caller supplies the authenticated Origin
// world and, for lunar state, its real home Moon. No world/save is invented.
[[nodiscard]] auto encode_origin_service_state_json(
    const PhysicalLocalSystem&, const OriginServiceCraftState&,
    const PhysicalMoonDescriptor* = nullptr)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_origin_service_state_json(
    const PhysicalLocalSystem&, std::string_view,
    const PhysicalMoonDescriptor* = nullptr)
    -> std::expected<OriginServiceCraftState, SaveSchemaError>;
} // namespace apsis_drift
