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

// Per-parameter derivatives are converted using the original fixed 12*t clock.
// Private geometry remains adversarial input only; side must be 0 or 1.
[[nodiscard]] auto boarding_planted_leg_timing_numeric(
    RigidVector3 hip, RigidVector3 parameter_first,
    RigidVector3 parameter_second, double ankle_x, double ankle_z, double plane,
    double thigh_length, double shin_length, bool forward_branch, bool reverse,
    std::size_t side) -> BoardingPlantedLegTimingEvidence;
[[nodiscard]] auto boarding_planted_legs_timing_bounded(double first,
                                                        double last,
                                                        std::size_t max_depth,
                                                        std::size_t max_nodes,
                                                        std::size_t max_leaves)
    -> std::expected<BoardingPlantedLegTimingDiagnostic, std::string>;
// These signed component inputs already use radians per second. The physical
// joint speed is their orthogonal resultant; knee supplies zero roll.
[[nodiscard]] auto boarding_planted_leg_joint_speed(
    BoardingPlantedLegScalarBounds roll_rate,
    BoardingPlantedLegScalarBounds pitch_rate)
    -> BoardingPlantedLegSpeedEvidence;
[[nodiscard]] auto boarding_planted_leg_speed_threshold()
    -> BoardingPlantedLegScalarBounds;
// Only compiled body expressions; these caps can lower the registered budgets.
// The original timing result remains owned unchanged, including refusal/prefix.
[[nodiscard]] auto boarding_planted_body_bounded(
    double first, double last, std::size_t max_body_leaves,
    std::size_t max_timing_depth = kBoardingPlantedLegMaximumDepth,
    std::size_t max_timing_nodes = kBoardingPlantedLegMaximumNodes,
    std::size_t max_timing_leaves = kBoardingPlantedLegMaximumLeaves)
    -> std::expected<BoardingPlantedBodyDiagnostic, std::string>;
// Fixed t=0 body and original hip region. Only the numerical candidate and
// lowered prerequisite budgets vary; no caller geometry grants authority.
[[nodiscard]] auto boarding_planted_hip_preflight_bounded(
    RigidVector3 candidate,
    std::size_t max_body_leaves = kBoardingPlantedBodyMaximumLeaves,
    std::size_t max_timing_depth = kBoardingPlantedLegMaximumDepth,
    std::size_t max_timing_nodes = kBoardingPlantedLegMaximumNodes,
    std::size_t max_timing_leaves = kBoardingPlantedLegMaximumLeaves)
    -> std::expected<BoardingPlantedHipPreflightDiagnostic, std::string>;
} // namespace apsis_drift::detail
