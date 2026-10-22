#pragma once
#include "apsis_drift/origin_service_save.hpp"
#include "apsis_drift/origin_service_sortie.hpp"

namespace apsis_drift {
// Fixed one-actor sortie: service + beacon + at most one frozen two-pose
// arrival and its resource bill. No nested voyage/recursive wrapper admitted.
inline constexpr std::size_t kMaximumOriginServiceSortieStateBytes{
    std::size_t{32} * 1024U};
[[nodiscard]] auto encode_origin_service_sortie_json(
    const PhysicalLocalSystem&, const PhysicalMoonDescriptor&,
    const OriginServiceSortie&) -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_origin_service_sortie_json(
    const PhysicalLocalSystem&, const PhysicalMoonDescriptor&, std::string_view)
    -> std::expected<OriginServiceSortie, SaveSchemaError>;
} // namespace apsis_drift
