#pragma once

#include "apsis_drift/origin_boarding_source_endpoint.hpp"

namespace apsis_drift::detail {
// Absolute finite root Y/Z only; the registered source, X terms, shapes and
// branch stay fixed. Capacities can only lower the original bounded work.
[[nodiscard]] auto boarding_source_endpoint_bounded(
    const OriginBoardingBootSupport&, double root_y = .847,
    double root_z = -.55,
    std::size_t max_site_partitions = kBoardingBootSourcePartitionCount,
    std::size_t max_body_records = kBoardingSourceEndpointMaximumRecords)
    -> std::expected<BoardingSourceEndpointDiagnostic, std::string>;
} // namespace apsis_drift::detail
