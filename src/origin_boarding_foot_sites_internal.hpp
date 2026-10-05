#pragma once

#include "apsis_drift/origin_boarding_boot_support.hpp"

namespace apsis_drift::detail {
// Fixed shapes and immutable source; private numerical offsets cannot become
// caller-provided contact authority. Only lowered coverage budgets are
// admitted.
[[nodiscard]] auto boarding_foot_sites_bounded(
    const OriginBoardingBootSupport&,
    const std::array<BoardingFootSiteOffsets, 2>& offsets = {},
    std::size_t max_partitions = kBoardingBootSourcePartitionCount)
    -> std::expected<BoardingFootSitesDiagnostic, std::string>;
} // namespace apsis_drift::detail
