#pragma once

#include "apsis_drift/origin_boarding_source_endpoint_self.hpp"

namespace apsis_drift::detail {
// Lowered work budgets only; the source-owned geometry and graph stay fixed.
[[nodiscard]] auto boarding_source_endpoint_self_bounded(
    const OriginBoardingBootSupport&,
    std::size_t max_site_partitions = kBoardingBootSourcePartitionCount,
    std::size_t max_body_records = kBoardingSourceEndpointMaximumRecords,
    std::size_t max_pairs = kBoardingBodyPairCount,
    std::size_t max_axes = kBoardingSourceEndpointSelfMaximumAxes)
    -> std::expected<BoardingSourceEndpointSelfDiagnostic, std::string>;
// Arithmetic evidence only; these seams cannot grant body or gameplay
// authority.
[[nodiscard]] auto boarding_source_endpoint_self_support(
    const OriginBoardingBootSupport&, BoardingBodyPartId, RigidVector3)
    -> std::expected<BoardingSourceEndpointSelfSupportEvidence, std::string>;
[[nodiscard]] auto boarding_source_endpoint_self_half_ray(
    double radius1, double radius2, double region_radius,
    BoardingPlantedLegScalarBounds outgoing_cosine)
    -> std::expected<BoardingSourceEndpointSelfHalfRayEvidence, std::string>;
} // namespace apsis_drift::detail
