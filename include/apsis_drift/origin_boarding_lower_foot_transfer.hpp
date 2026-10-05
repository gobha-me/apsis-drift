#pragma once

#include "apsis_drift/origin_boarding_source_endpoint_load.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace apsis_drift {
inline constexpr std::uint32_t kBoardingLowerFootTransferVersion{1};
inline constexpr std::size_t kBoardingLowerFootTransferMaximumDepth{10},
    kBoardingLowerFootTransferMaximumNodes{2047},
    kBoardingLowerFootTransferMaximumLeaves{1024},
    kBoardingLowerFootTransferMaximumCellBytes{8192},
    kBoardingLowerFootTransferMaximumOutputBytes{8 * std::size_t{1024} * 1024},
    kBoardingLowerFootTransferMaximumScratchBytes{16384};
inline constexpr std::uint64_t kBoardingLowerFootTransferMaximumPairs{214935},
    kBoardingLowerFootTransferMaximumAxes{3009090},
    kBoardingLowerFootTransferMaximumSignedTrials{6018180},
    kBoardingLowerFootTransferMaximumPressureCandidates{40940},
    kBoardingLowerFootTransferMaximumEdges{180136};

enum class BoardingLowerFootTransferCondition : std::uint8_t {
  none,
  initial_prerequisite,
  invalid_binding,
  unsupported_arithmetic,
  derivative_domain,
  reach,
  forward_branch,
  joint_sector,
  root_speed,
  root_acceleration,
  joint_speed,
  unresolved_self_pair,
  sole_disk_margin,
  source_disk_margin,
  incomplete_cover,
  node_capacity,
  depth_capacity,
  leaf_capacity,
  output_capacity,
  pair_capacity,
  axis_capacity,
  signed_trial_capacity,
  pressure_capacity,
  edge_capacity,
  unsplittable_interval
};
struct BoardingLowerFootTransferLeg {
  BoardingPlantedLegAngularDerivatives hip_pitch, shin_pitch, knee_flex,
      ankle_pitch, hip_abduction, ankle_roll;
  std::array<BoardingPlantedLegScalarBounds, 3> joint_speeds;
  // roll/hip lower/hip upper/knee/ankle/rho/D/gamma^2 margins.
  std::array<BoardingPlantedLegScalarBounds, 8> margins;
  bool link_identities{}, plane_identity{}, branch_certified{},
      joint_sectors_certified{}, derivative_domains_certified{},
      joint_speeds_certified{};
};
struct BoardingLowerFootTransferPressureCandidate {
  BoardingPlantedLegScalarBounds minimum_signed_side, minimum_squared_gap;
  BoardingSourceEndpointLoadCandidateStatus status{};
  bool arithmetic_supported{}, quad_valid{};
};
struct BoardingLowerFootTransferPressure {
  std::array<BoardingPlantedLegScalarBounds, 2> pressure_xz;
  BoardingPlantedLegScalarBounds sole_minimum_signed_side,
      sole_minimum_squared_gap;
  std::array<BoardingLowerFootTransferPressureCandidate,
             kBoardingBootSourcePartitionCount>
      candidates{};
  // This index addresses the source retained by initial;65535 means absent.
  std::uint16_t source_partition{65535}, scanned_partitions{};
  bool arithmetic_supported{}, coplanar{}, sole_disk_contained{},
      source_disk_contained{}, coverage_complete{}, complete{};
};
struct BoardingLowerFootTransferOwner {
  BoardingPlantedLegScalarBounds extent, limit, secondary_extent;
  BoardingSourceEndpointSelfCertificate certificate{};
  bool arithmetic_supported{}, certified{}, structural_identity{};
};
struct BoardingLowerFootTransferCell {
  double first{}, last{};
  std::array<BoardingPlantedBodyPointEvidence, kBoardingPlantedBodyPointCount>
      points;
  std::array<BoardingPlantedBodyPointEvidence, kBoardingBodyPartCount>
      mass_points;
  BoardingPlantedBodyPointEvidence center_of_mass;
  std::array<BoardingLowerFootTransferLeg, 2> legs;
  BoardingPlantedLegScalarBounds root_speed, root_acceleration;
  std::array<BoardingLowerFootTransferPressure, 2> pressures;
  BoardingPlantedLegScalarBounds port_reaction_fraction;
  std::array<BoardingPlantedLegScalarBounds, 2> barycenter_xz,
      common_pressure_delta_xz;
  std::array<BoardingLowerFootTransferOwner, kBoardingSelfConnectedRegionCount>
      owners;
  std::array<BoardingSourceEndpointSelfCertificate, kBoardingBodyPairCount>
      pair_certificates{};
  std::array<std::uint16_t, 6> certificate_counts{};
  bool arithmetic_supported{}, kinematics_complete{}, timing_complete{},
      self_complete{}, positive_reactions{},
      nominal_vertical_equilibrium_complete{}, finite_pressure_complete{},
      complete{};
};
struct BoardingLowerFootTransferCounters {
  std::uint64_t self_pairs{}, proposed_axes{}, signed_trials{},
      pressure_candidates{}, disk_edges{};
};
struct BoardingLowerFootTransferRefusal {
  double first{}, last{};
  std::size_t depth{};
  BoardingLowerFootTransferCondition condition{}, predicate_condition{};
  std::optional<std::size_t> side, pair, partition;
  BoardingPlantedLegScalarBounds limiting_bound;
};
struct BoardingLowerFootTransferDiagnostic {
  // Exactly one unchanged child retains the selected immutable source.
  BoardingSourceEndpointLoadDiagnostic initial;
  std::uint32_t transfer_version{kBoardingLowerFootTransferVersion};
  std::array<BoardingPlantedBodyPartBinding, kBoardingBodyPartCount> parts;
  double requested_first{}, requested_last{}, seconds_per_parameter{4},
      reporting_elapsed_seconds{}, common_translation_y_metres{.847};
  bool reverse{}, arithmetic_supported{}, kinematics_complete{},
      timing_complete{}, self_complete{},
      nominal_vertical_equilibrium_complete{}, finite_pressure_complete{},
      complete{};
  std::size_t examined_nodes{}, maximum_depth{}, checked_source_quads{},
      output_capacity_bytes{};
  BoardingLowerFootTransferCounters work;
  std::vector<BoardingLowerFootTransferCell> cells;
  std::optional<BoardingLowerFootTransferRefusal> first_refusal;
  static constexpr bool dynamics_qualified{false}, strength_qualified{false},
      world_qualified{false}, volume_qualified{false},
      material_qualified{false}, source_surface_sweep_qualified{false},
      route_qualified{false}, actor_qualified{false}, seat_qualified{false},
      save_qualified{false}, first_flight_qualified{false},
      foot_acquisition_qualified{false}, free_foot_swing_qualified{false},
      complete_lower_step_to_seat_transfer_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_lower_foot_transfer(
    const OriginBoardingBootSupport&, double first = 0, double last = 1)
    -> std::expected<BoardingLowerFootTransferDiagnostic, std::string>;
} // namespace apsis_drift
