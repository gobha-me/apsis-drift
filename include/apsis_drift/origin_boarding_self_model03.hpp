#pragma once

#include "apsis_drift/origin_boarding_self_model02.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kBoardingSelfModel03Version{3};

// Original Euclidean capsule K intersected with
// (B-A).(x-A) <= L*sqrt((B-A).(B-A)), using exact-real differences of
// the stored endpoints. Radius <= L < original axis length. There is no
// constructed short-capsule endpoint or extra distal hemisphere.
struct BoardingSelfAxialSlab03 {
  BoardingBodyCapsule original_capsule;
  RigidVector3 root_metres, toward_metres;
  double axial_limit_metres{};
};

// The cap retains actual inverse-frame semantics. Recipe03 neck origin is
// the actual trunk centre, its frame is the actual trunk frame, and its
// limit is the stored trunk halfY. The waist and spheres retain Recipe02.
using BoardingSelfRegion03 =
    std::variant<BoardingSelfEllipsoidCap, BoardingSelfAxialSlab03,
                 BoardingSelfSphere>;
struct BoardingSelfConnectedRegion03 {
  BoardingBodyPartId first{}, second{};
  BoardingSelfJunction junction{};
  BoardingSelfRegion03 region;
};
enum class BoardingSelfCertificate03 : std::uint8_t {
  none,
  convex_support_plane,
  cap_partner_support,
  original_axis_box_support,
  opposite_ray_endpoint_ball,
  half_ray_angle_bound,
  shoulder_cofactor_split,
  verified_strict_witness
};
struct BoardingSelfPairDiagnostic03 {
  BoardingBodyPartId first{}, second{};
  std::optional<std::size_t> connected_region_index;
  BoardingSelfPairOutcome outcome{BoardingSelfPairOutcome::unresolved};
  BoardingSelfCertificate03 certificate{BoardingSelfCertificate03::none};
  BoardingSelfUnresolved reason{BoardingSelfUnresolved::none};
  // Both actual memberships are strict. Connected refusal additionally
  // requires certified strict exclusion from that finite region.
  std::optional<RigidVector3> strict_witness_metres;
};
struct BoardingSelfDiagnostic03 {
  std::uint32_t self_model_version{kBoardingSelfModel03Version};
  BoardingBodyDiagnostic canonical;
  std::array<BoardingSelfPart, kBoardingBodyPartCount> self_parts;
  // Both arrays are lexicographic by canonical part enum values. Every
  // connected index names the region with exactly the same part pair.
  std::array<BoardingSelfConnectedRegion03, kBoardingSelfConnectedRegionCount>
      regions;
  std::array<BoardingSelfPairDiagnostic03, kBoardingBodyPairCount> pairs;
  bool self_checkpoint_passed{}, strict_conflict{}, unresolved_pair{};
  static constexpr bool recipe_complete{true};
  static constexpr bool arbitrary_pose_proof_complete{false};
  static constexpr bool support_qualified{false};
  static constexpr bool world_qualified{false};
  static constexpr bool route_qualified{false};
  static constexpr bool actor_qualified{false};
};

// Reconstructs canonical policy01 once from its validated pose. The public
// evaluator accepts no caller-created shapes, regions, proofs or tolerances.
// Recipe02 output and all source/world/support/save contracts remain intact.
[[nodiscard]] auto evaluate_origin_boarding_self_model03(
    const BoardingBodyPose&)
    -> std::expected<BoardingSelfDiagnostic03, BoardingBodyError>;
} // namespace apsis_drift
