#pragma once

#include "apsis_drift/origin_boarding_body.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kBoardingSelfModel02Version{2};
inline constexpr std::size_t kBoardingSelfConnectedRegionCount{14};
inline constexpr double kBoardingSelfWaistLimitMetres{0x1.bb0cd605d7512p-3};
inline constexpr double kBoardingSelfNeckLimitMetres{0x1.3ced916872b02p-1};
inline constexpr double kBoardingSelfHipLengthMetres{0x1.bb0cd605d7512p-3};
inline constexpr double kBoardingSelfKneeRadiusMetres{0x1.18f69ad39e925p-2};
inline constexpr double kBoardingSelfAnkleLengthMetres{0x1.7529d1ebf8034p-3};
inline constexpr double kBoardingSelfShoulderRadiusMetres{0x1.56872b020c49cp-2};
inline constexpr double kBoardingSelfElbowRadiusMetres{0x1.bab11b9fbb660p-3};
inline constexpr double kBoardingSelfWristLengthMetres{0x1.12c49dd0cc1e9p-4};

// Actual stored columns define an affine image, not an ideal rotation.
struct BoardingSelfEllipsoid {
  RigidVector3 center_metres, semi_axes_metres;
  BoardingBodyFrame frame;
};
// Retained boxes are affine images of their actual stored columns and half
// sizes. Canonical policy01 world data and old diagnostic behavior are intact.
using BoardingSelfSolid =
    std::variant<BoardingBodyBox, BoardingBodyCapsule, BoardingSelfEllipsoid>;
struct BoardingSelfPart {
  BoardingBodyPartId id{};
  BoardingSelfSolid solid;
};
struct BoardingSelfSphere {
  RigidVector3 center_metres;
  double radius_metres{};
};
// The ellipsoid intersected with inverse(axial_frame)(x-origin).y <= limit.
struct BoardingSelfEllipsoidCap {
  BoardingSelfEllipsoid ellipsoid;
  RigidVector3 axial_origin_metres;
  BoardingBodyFrame axial_frame;
  double axial_limit_metres{};
};
enum class BoardingSelfJunction : std::uint8_t {
  waist,
  neck,
  hip,
  knee,
  ankle,
  shoulder,
  elbow,
  wrist
};
using BoardingSelfRegion =
    std::variant<BoardingSelfEllipsoidCap, BoardingBodyCapsule,
                 BoardingSelfSphere>;
struct BoardingSelfConnectedRegion {
  BoardingBodyPartId first{}, second{};
  BoardingSelfJunction junction{};
  BoardingSelfRegion region;
};
enum class BoardingSelfPairOutcome : std::uint8_t {
  certified_no_interior,
  certified_whole_owned,
  strict_nonadjacent_interior,
  strict_unowned_connected_interior,
  unresolved
};
enum class BoardingSelfCertificate : std::uint8_t {
  none,
  convex_support_plane,
  cap_partner_support,
  collinear_proximal_capsule,
  opposite_ray_endpoint_ball,
  endpoint_halfspace,
  shoulder_ball_cylinder_split,
  half_ray_angle_bound,
  wrist_box_bands,
  verified_strict_witness
};
enum class BoardingSelfUnresolved : std::uint8_t {
  none,
  unsupported_family,
  arithmetic_unresolved,
  invalid_geometry
};
struct BoardingSelfPairDiagnostic {
  BoardingBodyPartId first{}, second{};
  std::optional<std::size_t> connected_region_index;
  BoardingSelfPairOutcome outcome{BoardingSelfPairOutcome::unresolved};
  BoardingSelfCertificate certificate{BoardingSelfCertificate::none};
  BoardingSelfUnresolved reason{BoardingSelfUnresolved::none};
  // Both actual solid memberships are strict. For a connected refusal, the
  // point is additionally strictly outside the registered finite region.
  std::optional<RigidVector3> strict_witness_metres;
};
struct BoardingSelfDiagnostic {
  std::uint32_t self_model_version{kBoardingSelfModel02Version};
  BoardingBodyDiagnostic canonical;
  std::array<BoardingSelfPart, kBoardingBodyPartCount> self_parts;
  // Regions and pairs are lexicographic by canonical part enum values. A
  // connected index always names the region with exactly the same pair.
  std::array<BoardingSelfConnectedRegion, kBoardingSelfConnectedRegionCount>
      regions;
  std::array<BoardingSelfPairDiagnostic, kBoardingBodyPairCount> pairs;
  // Nonadjacent pairs require certified_no_interior. Connected pairs require
  // certified_whole_owned or certified_no_interior (vacuous ownership).
  bool self_checkpoint_passed{}, strict_conflict{}, unresolved_pair{};
  static constexpr bool recipe_complete{true};
  static constexpr bool arbitrary_pose_proof_complete{false};
  static constexpr bool support_qualified{false};
  static constexpr bool world_qualified{false};
  static constexpr bool route_qualified{false};
  static constexpr bool actor_qualified{false};
};
// Reconstructs from validated policy01 pose. Output geometry is diagnostic;
// there is no public entry accepting caller-forged shapes/regions/proofs.
[[nodiscard]] auto evaluate_origin_boarding_self_model02(
    const BoardingBodyPose&)
    -> std::expected<BoardingSelfDiagnostic, BoardingBodyError>;
} // namespace apsis_drift
