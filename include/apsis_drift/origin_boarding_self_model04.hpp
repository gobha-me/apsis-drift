#pragma once
#include "apsis_drift/origin_boarding_self_model03.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kBoardingSelfModel04Version{4};
// Same finite regions and comparison records as Recipe03, evaluated against
// the explicitly versioned lateral body. No arbitrary pose or route authority.
struct BoardingSelfDiagnostic04 {
  std::uint32_t self_model_version{kBoardingSelfModel04Version};
  BoardingBodyDiagnostic canonical;
  std::array<BoardingSelfPart, kBoardingBodyPartCount> self_parts;
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
// Pose-only policy2 admission. Unsupported rotated comparisons retain their
// exact refusal; the original Recipe03 evaluator continues to require policy1.
[[nodiscard]] auto evaluate_origin_boarding_self_model04(
    const BoardingBodyPose&)
    -> std::expected<BoardingSelfDiagnostic04, BoardingBodyError>;
} // namespace apsis_drift
