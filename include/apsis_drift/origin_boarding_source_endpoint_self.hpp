#pragma once

#include "apsis_drift/origin_boarding_self_model02.hpp"
#include "apsis_drift/origin_boarding_source_endpoint.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace apsis_drift {
inline constexpr std::uint32_t kBoardingSourceEndpointSelfVersion{1};
inline constexpr std::size_t kBoardingSourceEndpointSelfMaximumAxes{14};
enum class BoardingSourceEndpointSelfShape : std::uint8_t {
  box,
  ellipsoid,
  capsule
};
struct BoardingSourceEndpointSelfPart {
  BoardingBodyPartId id{};
  BoardingSourceEndpointSelfShape shape{};
  // Original binding; ellipsoids use the corresponding box half sizes.
  BoardingPlantedBodyPartBinding binding;
};
struct BoardingSourceEndpointSelfRegion {
  BoardingBodyPartId first{}, second{};
  BoardingSelfJunction junction{};
  BoardingPlantedBodyPointId root{}, toward{};
  double limit_metres{};
};
enum class BoardingSourceEndpointSelfCertificate : std::uint8_t {
  none,
  convex_support_plane,
  cap_partner_support,
  original_axis_box_support,
  half_ray_angle_bound,
  shoulder_split
};
enum class BoardingSourceEndpointSelfCondition : std::uint8_t {
  endpoint_prerequisite,
  unsupported_arithmetic,
  invalid_binding,
  pair_capacity,
  axis_capacity,
  unresolved_pair
};
struct BoardingSourceEndpointSelfPair {
  BoardingBodyPartId first{}, second{};
  std::optional<std::size_t> connected_region_index;
  BoardingSourceEndpointSelfCertificate certificate{};
  BoardingPlantedLegScalarBounds certificate_gap;
  // Ownership inequality is limit-extent>=0; shoulder also requires the
  // secondary distal extent strictly below the same limit.
  BoardingPlantedLegScalarBounds ownership_extent, ownership_limit,
      ownership_secondary_extent;
  RigidVector3 separating_direction;
  std::size_t examined_axes{}, signed_support_trials{};
  bool examined{}, arithmetic_supported{}, certified{}, whole_owned{},
      structural_identity{};
};
struct BoardingSourceEndpointSelfRefusal {
  BoardingSourceEndpointSelfCondition condition{};
  std::optional<std::size_t> pair_index;
};
struct BoardingSourceEndpointSelfSupportEvidence {
  BoardingPlantedLegScalarBounds support;
  bool arithmetic_supported{};
};
struct BoardingSourceEndpointSelfHalfRayEvidence {
  BoardingPlantedLegScalarBounds sine_squared, squared_gap;
  bool arithmetic_supported{}, certified{};
};
struct BoardingSourceEndpointSelfDiagnostic {
  BoardingSourceEndpointDiagnostic endpoint;
  std::uint32_t self_version{kBoardingSourceEndpointSelfVersion};
  std::array<BoardingSourceEndpointSelfPart, kBoardingBodyPartCount> parts{};
  std::array<BoardingSourceEndpointSelfRegion,
             kBoardingSelfConnectedRegionCount>
      regions{};
  std::array<BoardingSourceEndpointSelfPair, kBoardingBodyPairCount> pairs{};
  std::size_t examined_pairs{}, certified_pairs{}, examined_axes{},
      signed_support_trials{};
  bool bindings_complete{}, arithmetic_supported{}, coverage_complete{},
      self_qualified{}, complete{};
  std::optional<BoardingSourceEndpointSelfRefusal> first_refusal;
  // Static whole-body self only; no strict witness or conflict inference.
  static constexpr bool arbitrary_pose_qualified{false},
      continuous_qualified{false}, force_qualified{false},
      load_qualified{false}, world_qualified{false}, crop_qualified{false},
      sweep_qualified{false}, dynamics_qualified{false},
      acquisition_qualified{false}, transfer_qualified{false},
      free_foot_swing_qualified{false}, route_qualified{false},
      actor_qualified{false}, seat_qualified{false}, save_qualified{false},
      first_flight_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_source_endpoint_self(
    const OriginBoardingBootSupport&)
    -> std::expected<BoardingSourceEndpointSelfDiagnostic, std::string>;
} // namespace apsis_drift
