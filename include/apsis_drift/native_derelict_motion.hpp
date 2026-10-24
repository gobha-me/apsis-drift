#pragma once

#include "apsis_drift/rigid_body.hpp"

#include <optional>

namespace apsis_drift {
struct DerelictRelativeMotion {
  // Positive closing means the current player-to-hull range is decreasing.
  double closing_metres_per_second{}, transverse_metres_per_second{};
  friend auto operator==(const DerelictRelativeMotion&,
                         const DerelictRelativeMotion&) -> bool = default;
};

// Read-only kinematics of already admitted simultaneous parent-relative states.
// No coordinate conversion, future sampling, contact discovery or clock step.
// Different frames/ticks, invalid position/velocity or zero separation refuse.
[[nodiscard]] auto observe_derelict_relative_motion(
    const RigidBodyState& player, const RigidBodyState& hull)
    -> std::optional<DerelictRelativeMotion>;
} // namespace apsis_drift
