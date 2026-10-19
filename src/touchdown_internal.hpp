#pragma once
#include "apsis_drift/touchdown.hpp"
namespace apsis_drift::detail {
[[nodiscard]] auto assess_touchdown_geometry(
    const RigidBodyState&, std::uint8_t support_count,
    const std::array<TouchdownPadObservation, 4>&, bool gear_deployed,
    double surface_gravity, double pressure_millibars)
    -> std::expected<TouchdownAssessment, TouchdownError>;
} // namespace apsis_drift::detail
