#pragma once

#include "apsis_drift/origin_boarding_source_endpoint_load.hpp"

namespace apsis_drift::detail {
[[nodiscard]] auto boarding_source_endpoint_load_bounded(
    const OriginBoardingBootSupport&,
    std::size_t max_source_partitions = kBoardingBootSourcePartitionCount,
    std::size_t max_body_records = kBoardingSourceEndpointMaximumRecords,
    std::size_t max_self_pairs = kBoardingBodyPairCount,
    std::size_t max_self_axes = kBoardingSourceEndpointSelfMaximumAxes,
    std::size_t max_pressure_partitions = kBoardingBootSourcePartitionCount)
    -> std::expected<BoardingSourceEndpointLoadDiagnostic, std::string>;
// Local source/sole disk arithmetic, never a body or reaction certificate.
[[nodiscard]] auto boarding_source_endpoint_load_pressure_math(
    const OriginBoardingBootSupport&, std::size_t site,
    std::size_t source_partition,
    std::array<BoardingPlantedLegScalarBounds, 2> pressure_xz)
    -> std::expected<BoardingSourceEndpointLoadPressureMathEvidence,
                     std::string>;
// Supplied quad geometry only; cannot attest a provider's triangle pair.
[[nodiscard]] auto boarding_source_endpoint_load_quad_math(
    std::array<RigidVector3, 4> perimeter, double plane_metres)
    -> std::expected<BoardingSourceEndpointLoadQuadMathEvidence, std::string>;
} // namespace apsis_drift::detail
