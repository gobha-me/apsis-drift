#pragma once

#include "apsis_drift/rigid_body.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace apsis_drift {
inline constexpr std::uint32_t kBoardingPlantedLegRecipeVersion{1};
inline constexpr std::size_t kBoardingPlantedLegMaximumDepth{12};
inline constexpr std::size_t kBoardingPlantedLegMaximumNodes{8191};
inline constexpr std::size_t kBoardingPlantedLegMaximumLeaves{4096};
struct BoardingPlantedLegScalarBounds {
  double lower{}, upper{};
};
struct BoardingPlantedLegPointBounds {
  RigidVector3 lower, upper;
};
enum class BoardingPlantedLegCondition : std::uint8_t {
  none,
  unsupported_arithmetic,
  singular_plane,
  reach,
  forward_branch,
  hip_roll,
  hip_flex,
  knee_flex,
  ankle_pitch,
  subdivision_capacity,
  unsplittable_interval
};
struct BoardingPlantedLegEvidence {
  // Canonical exact-expression enclosures BEFORE the common Y placement.
  BoardingPlantedLegPointBounds hip, knee, ankle, boot_center;
  BoardingPlantedLegScalarBounds distance_squared, rho, alpha, gamma;
  BoardingPlantedLegScalarBounds hip_sine, hip_cosine, shin_sine, shin_cosine,
      knee_cosine, roll_sine, roll_cosine;
  // Canonical Y expressions plus the same placementY give plane+height.
  // These terms, not rounded report points, retain the planted identity.
  std::array<double, 3> ankle_y_terms{}, boot_center_y_terms{};
  double plane_metres{}, reporting_hip_flex_degrees{},
      reporting_knee_flex_degrees{}, reporting_ankle_pitch_degrees{},
      reporting_hip_abduction_degrees{};
  bool plane_identity{}, link_identities{}, limits_certified{};
  BoardingPlantedLegCondition limiting_condition{};
};
struct BoardingPlantedLegLeaf {
  double first{}, last{};
  BoardingPlantedLegScalarBounds root_x;
  std::array<BoardingPlantedLegEvidence, 2> legs;
};
struct BoardingPlantedLegRefusal {
  double first{}, last{};
  std::size_t side{}, depth{};
  BoardingPlantedLegCondition condition{}, limiting_condition{};
};
struct BoardingPlantedLegDiagnostic {
  std::uint32_t recipe_version{kBoardingPlantedLegRecipeVersion};
  double requested_first{}, requested_last{}, duration_seconds{12};
  // Every canonical point receives this common exact Y translation.
  double placement_y_metres{.72};
  bool reverse{}, complete{}, plane_identities{}, link_identities{},
      joint_limits_certified{};
  std::size_t examined_nodes{}, maximum_depth{};
  // Ascending, closed, gap-free accepted cover; reverse changes only direction.
  // A refused prefix never grants a complete interval result.
  std::vector<BoardingPlantedLegLeaf> leaves;
  std::optional<BoardingPlantedLegRefusal> first_refusal;
  static constexpr bool dynamics_qualified{false}, self_qualified{false},
      load_qualified{false}, world_qualified{false}, route_qualified{false},
      actor_qualified{false}, seat_qualified{false};
};
// Only the registered source-free paired fixture; endpoints finite in[0,1].
// No caller geometry, pose fitting or supplied identity flags are accepted.
[[nodiscard]] auto assess_origin_boarding_planted_legs(double first = 0,
                                                       double last = 1)
    -> std::expected<BoardingPlantedLegDiagnostic, std::string>;

inline constexpr std::uint32_t kBoardingPlantedLegTimingVersion{1};
enum class BoardingPlantedLegTimingCondition : std::uint8_t {
  none,
  closure_prerequisite,
  unsupported_arithmetic,
  rho_derivative,
  distance_derivative,
  gamma_derivative,
  root_speed,
  root_acceleration,
  joint_speed,
  subdivision_capacity,
  unsplittable_interval
};
enum class BoardingPlantedLegJoint : std::uint8_t { none, hip, knee, ankle };
struct BoardingPlantedLegPointDerivatives {
  BoardingPlantedLegPointBounds velocity, acceleration;
};
struct BoardingPlantedLegAngularDerivatives {
  // Signed coordinate derivatives in rad/s and rad/s^2. The second derivative
  // is not the complete physical angular-acceleration vector of a moving frame.
  BoardingPlantedLegScalarBounds rate, coordinate_second;
};
struct BoardingPlantedLegSpeedEvidence {
  BoardingPlantedLegScalarBounds speed_radians_per_second;
  bool supported{}, certified{};
};
struct BoardingPlantedLegTimingEvidence {
  BoardingPlantedLegEvidence closure;
  BoardingPlantedLegPointDerivatives hip, knee, ankle, boot_center;
  BoardingPlantedLegAngularDerivatives hip_pitch, shin_pitch, knee_flex,
      ankle_pitch, hip_abduction, ankle_roll;
  BoardingPlantedLegSpeedEvidence hip_speed, knee_speed, ankle_speed;
  bool derivative_domains_certified{}, joint_speed_certified{};
  BoardingPlantedLegTimingCondition limiting_condition{};
  BoardingPlantedLegJoint limiting_joint{};
  BoardingPlantedLegScalarBounds limiting_bound;
};
struct BoardingPlantedLegTimingLeaf {
  double first{}, last{};
  BoardingPlantedLegPointDerivatives root;
  BoardingPlantedLegScalarBounds root_speed, root_acceleration;
  std::array<BoardingPlantedLegTimingEvidence, 2> legs;
};
struct BoardingPlantedLegTimingRefusal {
  double first{}, last{};
  std::size_t side{}, depth{};
  BoardingPlantedLegTimingCondition condition{}, limiting_condition{};
  BoardingPlantedLegJoint joint{};
  BoardingPlantedLegScalarBounds limiting_bound;
};
struct BoardingPlantedLegTimingDiagnostic {
  std::uint32_t timing_version{kBoardingPlantedLegTimingVersion};
  // The original closure query and its independent unchanged bounded cover.
  BoardingPlantedLegDiagnostic closure;
  double requested_first{}, requested_last{}, seconds_per_parameter{12},
      reporting_elapsed_seconds{};
  bool reverse{}, complete{}, derivative_domains_certified{},
      root_speed_certified{}, root_acceleration_certified{},
      leg_joint_speed_certified{};
  std::size_t examined_nodes{}, maximum_depth{};
  // Ascending closed cover. Reverse negates physical first derivatives only;
  // point requests describe instantaneous derivatives, not stationary pauses.
  std::vector<BoardingPlantedLegTimingLeaf> leaves;
  std::optional<BoardingPlantedLegTimingRefusal> first_refusal;
  static constexpr bool dynamics_qualified{false},
      full_body_com_qualified{false}, self_qualified{false},
      load_qualified{false}, world_qualified{false}, route_qualified{false},
      actor_qualified{false}, seat_qualified{false}, save_qualified{false},
      free_foot_swing_qualified{false};
};
// Same compiled recipe and original 12*t clock; no caller timing or geometry.
[[nodiscard]] auto assess_origin_boarding_planted_legs_timing(double first = 0,
                                                              double last = 1)
    -> std::expected<BoardingPlantedLegTimingDiagnostic, std::string>;
} // namespace apsis_drift
