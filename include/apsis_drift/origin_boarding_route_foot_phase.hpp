#pragma once

#include "apsis_drift/origin_boarding_planted_legs.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kBoardingRouteFootPhaseVersion{1};
inline constexpr std::size_t kBoardingRouteFootPhaseMaximumDepth{10},
    kBoardingRouteFootPhaseMaximumNodes{2047},
    kBoardingRouteFootPhaseMaximumLeaves{1024},
    kBoardingRouteFootPhaseMaximumCellBytes{12288},
    kBoardingRouteFootPhaseMaximumOutputBytes{16 * std::size_t{1024} * 1024},
    kBoardingRouteFootPhaseMaximumScratchBytes{32768};
// Fixed sums preserve source/old endpoint terms; no arbitrary expression tree.
struct BoardingRoutePhaseConstant {
  std::array<double, 3> terms{};
  std::uint8_t count{1};
};
struct BoardingRoutePhasePointConstant {
  std::array<BoardingRoutePhaseConstant, 3> coordinates;
};
struct BoardingRouteFootPhaseControl {
  std::array<BoardingRoutePhasePointConstant, 2> sole;
  std::array<double, 2> yaw_half{};
  double swing_height_metres{};
};
struct BoardingRouteFootPhaseRequest {
  std::array<BoardingRoutePhasePointConstant, 2> root;
  std::array<double, 2> root_yaw_half{}, torso_lean_half{};
  std::array<BoardingRouteFootPhaseControl, 2> feet;
  double seconds_per_parameter{8};
  std::array<double, 2> port_reaction_fraction{.5, .5};
};
enum class BoardingRoutePhaseFrame : std::uint8_t {
  root,
  trunk,
  port_sole,
  starboard_sole
};
struct BoardingRoutePhaseBoxBinding {
  BoardingPlantedBodyPointId center{};
  RigidVector3 half_size_metres;
  BoardingRoutePhaseFrame frame{};
};
struct BoardingRoutePhasePartBinding {
  BoardingBodyPartId id{};
  std::variant<BoardingRoutePhaseBoxBinding, BoardingPlantedBodyCapsuleBinding>
      reservation;
  BoardingPlantedBodyMassBinding mass;
};
struct BoardingRoutePhaseFrameEvidence {
  std::array<BoardingPlantedBodyPointEvidence, 3> columns;
};
struct BoardingRouteFootPhaseLeg {
  BoardingPlantedLegScalarBounds distance_squared, rho_squared, gamma_squared,
      alpha, gamma;
  BoardingPlantedLegAngularDerivatives hip_pitch, shin_pitch, knee_flex,
      hip_axial, hip_abduction, ankle_pitch, ankle_roll;
  std::array<BoardingPlantedLegScalarBounds, 3> joint_speeds;
  std::array<BoardingPlantedLegScalarBounds, 6> sector_margins;
  bool nominal_links{}, target_sole_identity{}, derivative_domains{},
      joint_sectors{}, timing_complete{};
};
struct BoardingRouteFootPhaseCell {
  double first{}, last{};
  std::array<BoardingPlantedBodyPointEvidence, kBoardingPlantedBodyPointCount>
      points;
  std::array<BoardingPlantedBodyPointEvidence, kBoardingBodyPartCount>
      mass_points;
  BoardingPlantedBodyPointEvidence center_of_mass;
  std::array<BoardingRoutePhaseFrameEvidence, 4> frames;
  std::array<BoardingRouteFootPhaseLeg, 2> legs;
  BoardingPlantedLegScalarBounds root_speed, root_acceleration, root_yaw_speed,
      torso_joint_speed, port_reaction_fraction;
  std::array<BoardingPlantedLegScalarBounds, 2> sole_center_speed,
      sole_yaw_speed, whole_sole_speed;
  bool arithmetic_supported{}, nominal_links{}, target_sole_identities{},
      joint_sectors{}, derivative_domains{}, timing_complete{}, complete{};
};
enum class BoardingRouteFootPhaseCondition : std::uint8_t {
  none,
  unsupported_arithmetic,
  workspace,
  derivative_domain,
  reach,
  forward_branch,
  joint_sector,
  root_speed,
  root_acceleration,
  joint_speed,
  sole_speed,
  graph_capacity,
  leg_capacity,
  body_capacity,
  sector_capacity,
  timing_capacity,
  node_capacity,
  leaf_capacity,
  depth_capacity,
  output_capacity,
  unsplittable_interval,
  incomplete_cover
};
struct BoardingRouteFootPhaseCounters {
  std::uint64_t graphs{}, legs{}, bodies{}, sectors{}, timing{};
};
struct BoardingRouteFootPhaseRefusal {
  double first{}, last{};
  std::size_t depth{};
  BoardingRouteFootPhaseCondition condition{}, predicate_condition{};
  std::optional<std::size_t> side;
  BoardingPlantedLegScalarBounds limiting_bound;
};
struct BoardingRouteFootPhaseDiagnostic {
  std::uint32_t phase_version{kBoardingRouteFootPhaseVersion};
  BoardingRouteFootPhaseRequest request;
  std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount> parts;
  double requested_first{}, requested_last{}, reporting_elapsed_seconds{};
  bool reverse{}, arithmetic_supported{}, nominal_links{},
      target_sole_identities{}, joint_sectors{}, derivative_domains{},
      timing_complete{}, complete{};
  std::size_t examined_nodes{}, maximum_depth{}, output_capacity_bytes{};
  BoardingRouteFootPhaseCounters work;
  std::vector<BoardingRouteFootPhaseCell> cells;
  std::optional<BoardingRouteFootPhaseRefusal> first_refusal;
  static constexpr bool self_qualified{false}, source_qualified{false},
      material_qualified{false}, world_qualified{false},
      support_qualified{false}, load_qualified{false}, actor_qualified{false},
      route_qualified{false}, seat_qualified{false}, save_qualified{false},
      dynamics_qualified{false};
};
// Caller numerical controls certify only this source-free kinematic graph.
[[nodiscard]] auto assess_origin_boarding_route_foot_phase(
    const BoardingRouteFootPhaseRequest&, double first = 0, double last = 1)
    -> std::expected<BoardingRouteFootPhaseDiagnostic, std::string>;
} // namespace apsis_drift
