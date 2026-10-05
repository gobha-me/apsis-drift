#pragma once

#include "apsis_drift/origin_boarding_source_endpoint_self.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace apsis_drift {
inline constexpr std::uint32_t kBoardingSourceEndpointLoadVersion{1};
struct BoardingSourceEndpointLoadQuadMathEvidence {
  // Minima enclose ALL four lengths/eight convexity signs, never a subset.
  BoardingFootSiteScalarBounds minimum_edge_squared, minimum_signed_side;
  std::size_t checked_edges{}, checked_side_signs{};
  bool arithmetic_supported{}, horizontal{}, upward{}, convex{},
      nondegenerate{}, valid{};
};
enum class BoardingSourceEndpointLoadCandidateStatus : std::uint8_t {
  unexamined,
  noncoplanar,
  contained,
  margin_refused,
  invalid_support,
  numerical_unresolved
};
struct BoardingSourceEndpointLoadCandidate {
  BoardingFootSiteScalarBounds minimum_signed_side, minimum_squared_gap;
  BoardingSourceEndpointLoadCandidateStatus status{};
  bool arithmetic_supported{}, quad_valid{};
};
struct BoardingSourceEndpointLoadPressureEvidence {
  double plane_metres{};
  std::array<RigidVector3, 2> pressure_bounds_metres{};
  std::array<BoardingFootSiteEdgeEvidence, 4> sole_edges{}, source_edges{};
  std::optional<std::size_t> source_partition;
  std::array<LowerCockpitTriangleKey, 2> source_keys{};
  std::array<BoardingSourceEndpointLoadCandidate,
             kBoardingBootSourcePartitionCount>
      candidates{};
  std::size_t scanned_partitions{};
  bool arithmetic_supported{}, coplanar{}, sole_disk_contained{},
      source_disk_contained{}, coverage_complete{}, complete{};
};
enum class BoardingSourceEndpointLoadCondition : std::uint8_t {
  self_prerequisite,
  invalid_binding,
  unsupported_arithmetic,
  invalid_support_quad,
  pressure_capacity,
  sole_disk_margin,
  source_disk_margin,
  no_matching_support,
  numerical_unresolved
};
struct BoardingSourceEndpointLoadRefusal {
  BoardingSourceEndpointLoadCondition condition{};
  std::optional<std::size_t> site, partition;
};
struct BoardingSourceEndpointLoadPayload {
  std::uint32_t load_version{kBoardingSourceEndpointLoadVersion};
  BoardingFootSiteScalarBounds barycenter_z, common_pressure_delta_z;
  std::array<double, 2> reaction_fractions{.5, .5};
  double disk_radius_metres{kBoardingBootPressureRadiusMetres},
      disk_edge_margin_metres{kBoardingBootDiskEdgeMarginMetres},
      required_projected_load_margin_metres{kBoardingBootLoadMarginMetres},
      certified_resultant_disk_radius_metres{};
  std::array<BoardingSourceEndpointLoadPressureEvidence, 2> pressures{};
  std::array<BoardingSourceEndpointLoadQuadMathEvidence,
             kBoardingBootSourcePartitionCount>
      source_quads{};
  std::size_t checked_quads{}, pressure_candidates{}, edge_checks{};
  bool bindings_complete{}, arithmetic_supported{}, paired_com_x_identity{},
      positive_reactions{}, projected_barycenter_identity{},
      vertical_force_identity{}, vertical_moment_identity{},
      placement_nonpenetrating{}, contact_supported{},
      projected_margin_certified{}, load_qualified{}, complete{};
  std::optional<BoardingSourceEndpointLoadRefusal> first_refusal;
};
struct BoardingSourceEndpointLoadDiagnostic {
  // One unchanged child owns endpoint/Sites02 and immutable selected source.
  BoardingSourceEndpointSelfDiagnostic self;
  BoardingSourceEndpointLoadPayload load;
  static constexpr bool dynamics_qualified{false}, strength_qualified{false},
      world_qualified{false}, crop_qualified{false}, sweep_qualified{false},
      volume_qualified{false}, material_qualified{false},
      acquisition_qualified{false}, continuous_qualified{false},
      free_foot_swing_qualified{false}, transfer_qualified{false},
      route_qualified{false}, actor_qualified{false}, seat_qualified{false},
      save_qualified{false}, first_flight_qualified{false};
};
struct BoardingSourceEndpointLoadPressureMathEvidence {
  BoardingSourceEndpointLoadQuadMathEvidence source_quad;
  BoardingSourceEndpointLoadPressureEvidence pressure;
  std::size_t edge_checks{};
  // Local geometry arithmetic only: no body or equilibrium enrollment.
  static constexpr bool body_qualified{false}, self_qualified{false},
      force_qualified{false}, load_qualified{false}, world_qualified{false},
      route_qualified{false}, actor_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_source_endpoint_load(
    const OriginBoardingBootSupport&)
    -> std::expected<BoardingSourceEndpointLoadDiagnostic, std::string>;
} // namespace apsis_drift
