#pragma once

#include "apsis_drift/rigid_body.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <variant>

namespace apsis_drift {
inline constexpr std::uint32_t kBoardingBodyPolicyVersion{1};
inline constexpr std::uint32_t kBoardingBodyLateralPolicyVersion{2};
inline constexpr std::size_t kBoardingBodyPartCount{15};
inline constexpr std::size_t kBoardingBodyPairCount{105};
inline constexpr std::uint32_t kBoardingBodyMassDenominator{1200};
// Numerical workspace only; this is neither a source crop nor world authority.
inline constexpr double kBoardingBodyHipWorkspaceMetres{8};
inline constexpr double kBoardingBodyStandingWidthMetres{.64};
inline constexpr double kBoardingBodyStandingCrownMetres{1.93};

// All input angles are degrees. Registered yaw lies in [-180,180]. Flexion and
// lean limits are relative to the named parent; pelvis and total trunk lean
// also retain their absolute +/-35 degree limits. No angle is silently clamped.
struct BoardingBodySidePose {
  double hip_flex_degrees{}, knee_flex_degrees{}, shoulder_flex_degrees{},
      elbow_flex_degrees{};
  // Only the named policy02 evaluator admits hip abduction. The other
  // freedoms, including manual ankle roll, have no registered input frame.
  double hip_abduction_degrees{}, hip_axial_degrees{}, ankle_roll_degrees{},
      shoulder_abduction_degrees{}, shoulder_axial_degrees{},
      wrist_pitch_degrees{}, wrist_yaw_degrees{}, wrist_roll_degrees{};
};
struct BoardingBodyPose {
  std::uint32_t policy_version{kBoardingBodyPolicyVersion};
  bool suit_on{true}, pack_detached{true}, flat_boots{true};
  RigidVector3 hip_metres{0, 1.04763, 0};
  double yaw_degrees{}, pelvis_lean_degrees{}, torso_relative_lean_degrees{},
      pelvis_twist_degrees{}, torso_twist_degrees{}, neck_pitch_degrees{},
      neck_yaw_degrees{}, neck_roll_degrees{};
  // Port (-X), then starboard (+X). Flat ankle pitch is derived as
  // -(pelvis lean + hip flex - knee flex), checked within +/-30 degrees.
  std::array<BoardingBodySidePose, 2> sides;
};
enum class BoardingBodyError : std::uint8_t {
  unsupported_policy,
  non_finite_input,
  excessive_workspace,
  invalid_prerequisite,
  unsupported_freedom,
  joint_limit,
  numerical_failure
};
struct BoardingBodyFrame {
  // Active column-vector rotation; +X width, +Y up, +Z back.
  std::array<RigidVector3, 3> columns{
      RigidVector3{1, 0, 0}, RigidVector3{0, 1, 0}, RigidVector3{0, 0, 1}};
};
struct BoardingBodyBox {
  RigidVector3 center_metres, half_size_metres;
  BoardingBodyFrame frame;
};
struct BoardingBodyCapsule {
  RigidVector3 start_metres, end_metres;
  double radius_metres{};
};
enum class BoardingBodyPartId : std::uint8_t {
  pelvis,
  trunk,
  helmet,
  port_thigh,
  port_shin,
  port_boot,
  port_upper_arm,
  port_forearm,
  port_hand,
  starboard_thigh,
  starboard_shin,
  starboard_boot,
  starboard_upper_arm,
  starboard_forearm,
  starboard_hand
};
struct BoardingBodyPart {
  BoardingBodyPartId id{};
  std::variant<BoardingBodyBox, BoardingBodyCapsule> world_reservation;
  RigidVector3 mass_point_metres;
  std::uint32_t mass_weight{};
};
struct BoardingBodySideJoints {
  RigidVector3 hip_metres, knee_metres, ankle_metres, sole_origin_metres,
      shoulder_metres, elbow_metres, wrist_metres;
  double required_ankle_pitch_degrees{};
  double required_ankle_roll_degrees{};
};
enum class BoardingBodyIntersection : std::uint8_t {
  separated_or_contact,
  interior_with_witness,
  // Strict narrow overlap, but no verified common binary64 interior point.
  // This is a refusal, not clearance; no epsilon repairs the witness.
  interior_unresolved,
  invalid_geometry
};
struct BoardingBodyInteriorWitness {
  RigidVector3 point_metres;
  double first_depth_metres{}, second_depth_metres{};
};
enum class BoardingBodyPairKind : std::uint8_t {
  nonadjacent,
  unregistered_connected
};
struct BoardingBodyPairDiagnostic {
  BoardingBodyPartId first{}, second{};
  BoardingBodyPairKind kind{};
  BoardingBodyIntersection intersection{};
  std::optional<BoardingBodyInteriorWitness> witness;
};
struct BoardingBodyDiagnostic {
  std::uint32_t policy_version{kBoardingBodyPolicyVersion};
  std::array<BoardingBodyPart, kBoardingBodyPartCount> parts;
  std::array<BoardingBodySideJoints, 2> joints;
  std::array<BoardingBodyPairDiagnostic, kBoardingBodyPairCount> pairs;
  RigidVector3 eye_metres, center_of_mass_metres;
  bool self_conflict{}, unresolved_intersection{};
  // Policy01 has no registered finite connected self neighborhoods. Successful
  // evaluation is diagnostic only and cannot qualify a body, support or route.
  static constexpr bool definition_incomplete{true};
  static constexpr bool model_qualified{false};
  static constexpr bool route_qualified{false};
};
[[nodiscard]] auto evaluate_origin_boarding_body(const BoardingBodyPose&)
    -> std::expected<BoardingBodyDiagnostic, BoardingBodyError>;
// Explicit policy2, upright-pelvis lateral branch with derived flat ankles.
// Zero-abduction geometry follows the unchanged policy1 arithmetic. This is
// kinematic diagnostic evidence only; Recipe03 still accepts policy1 alone.
[[nodiscard]] auto evaluate_origin_boarding_body02(const BoardingBodyPose&)
    -> std::expected<BoardingBodyDiagnostic, BoardingBodyError>;
} // namespace apsis_drift
