#pragma once

#include "apsis_drift/origin_boarding_body.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kBoardingBodyRoutePolicyVersion{3};

// Static forward geometry only. Independent flat sole yaw is derived from
// root yaw plus the registered hip axial rotation, never an ankle yaw input.
struct BoardingRouteBodyDiagnostic {
  std::uint32_t policy_version{kBoardingBodyRoutePolicyVersion};
  std::array<BoardingBodyPart, kBoardingBodyPartCount> parts;
  std::array<BoardingBodySideJoints, 2> joints;
  std::array<BoardingBodyFrame, 2> flat_boot_frames;
  std::array<BoardingBodyFrame, 2> ankle_compensation_frames;
  std::array<double, 2> sole_yaw_degrees{};
  RigidVector3 eye_metres, center_of_mass_metres;
  bool lateral_yaw_branch{};

  // Rounded forward records grant no connected-self, source or motion proof.
  static constexpr bool self_qualified{false};
  static constexpr bool source_qualified{false};
  static constexpr bool material_qualified{false};
  static constexpr bool world_qualified{false};
  static constexpr bool sweep_qualified{false};
  static constexpr bool support_qualified{false};
  static constexpr bool route_qualified{false};
  static constexpr bool actor_qualified{false};
  static constexpr bool seat_qualified{false};
  static constexpr bool save_qualified{false};
};

// Policy3: upright pelvis when either hip has abduction/axial rotation;
// otherwise the unchanged sagittal pelvis-lean branch. All dimensions and
// limits are retained. Manual ankle, neck, wrist and twist freedoms refuse.
[[nodiscard]] auto evaluate_origin_boarding_route_body(const BoardingBodyPose&)
    -> std::expected<BoardingRouteBodyDiagnostic, BoardingBodyError>;
} // namespace apsis_drift
