#pragma once

#include "apsis_drift/origin_boarding_planted_legs.hpp"

namespace apsis_drift::detail {
// Private adversarial seams. Neither exposes caller geometry as public boarding
// authority. The ankle Y expression is exactly plane + .1, never a rounded
// input.
[[nodiscard]] auto boarding_planted_leg_numeric(
    RigidVector3 hip, double ankle_x, double ankle_z, double plane,
    double thigh_length, double shin_length, bool forward_branch)
    -> BoardingPlantedLegEvidence;

[[nodiscard]] auto boarding_planted_legs_bounded(double first, double last,
                                                 std::size_t max_depth,
                                                 std::size_t max_nodes,
                                                 std::size_t max_leaves)
    -> std::expected<BoardingPlantedLegDiagnostic, std::string>;
} // namespace apsis_drift::detail
