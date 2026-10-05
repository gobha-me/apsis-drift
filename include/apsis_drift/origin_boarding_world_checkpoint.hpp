#pragma once

#include "apsis_drift/origin_boarding_boot_support.hpp"
#include <cstdint>
#include <string>

namespace apsis_drift {
enum class BoardingWorldCheckpointRefusal : std::uint8_t {
  boot_self_prerequisite,
  crop_uncovered,
  unsupported_arithmetic,
  no_separation_certificate
};
struct BoardingWorldPartCoverage {
  BoardingBodyPartId part{};
  bool examined{}, arithmetic_supported{}, covered{};
  // Certified containing bounds, including the exact uniform stance.
  RigidVector3 lower_metres, upper_metres;
};
struct BoardingWorldRefusalEvidence {
  // Part applies to coverage/pair refusals. Prerequisite details stay in the
  // retained boot/self diagnostic; no world part has been examined there.
  BoardingBodyPartId part{};
  BoardingWorldCheckpointRefusal reason{};
  std::optional<LowerCockpitTriangleKey> key;
  std::uint32_t object{};
  std::string source_object;
  std::optional<std::uint32_t> evaluated_source_triangle;
  std::size_t axes_examined{}, unsupported_axes{};
};
struct BoardingWorldCheckpointDiagnostic {
  BoardingBootSupportDiagnostic boot_support;
  std::array<BoardingWorldPartCoverage, kBoardingBodyPartCount> coverage;
  std::uint64_t effective_triangle_count{}, expected_pairs{}, examined_pairs{},
      certified_pairs{};
  std::optional<BoardingWorldRefusalEvidence> first_refusal;
  bool coverage_complete{}, comparisons_complete{}, nonpenetrating{},
      checkpoint_supported{};
  // Nonpenetrating concerns selected source triangle surfaces vs the open
  // reservations only. It is not source solid-volume/material containment.
  static constexpr bool volume_qualified{false},
      arbitrary_pose_qualified{false}, sweep_qualified{false},
      route_qualified{false}, actor_qualified{false}, seat_qualified{false};
};
// Fresh boot/self assessment once, then unchanged canonical 15 reservations
// against the same immutable effective contact using its exact stance terms.
// A missing supporting-axis certificate is unresolved, not collision proof.
[[nodiscard]] auto assess_origin_boarding_world_checkpoint(
    const OriginBoardingBootSupport&, const BoardingBootSupportRequest&)
    -> std::expected<BoardingWorldCheckpointDiagnostic, std::string>;
} // namespace apsis_drift
