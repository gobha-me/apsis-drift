#pragma once
#include "apsis_drift/origin_boarding_intermediate_endpoint04.hpp"
#include <memory>
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingHipJointPlaneMethod01Version{1};
inline constexpr std::size_t kBoardingHipJointPlaneMethod01MaximumExpectedBytes{
    176},
    kBoardingHipJointPlaneMethod01MaximumDiagnosticBytes{168},
    kBoardingHipJointPlaneMethod01MaximumDataBytes{9216},
    kBoardingHipJointPlaneMethod01MaximumHelperBytes{4096},
    kBoardingHipJointPlaneMethod01MaximumScratchBytes{49152},
    kBoardingHipJointPlaneMethod01MaximumOutputBytes{16 * std::size_t{1024} *
                                                     1024},
    kBoardingHipJointPlaneMethod01MaximumLogBytes{16 * std::size_t{1024}};
enum class BoardingHipJointPlaneMethod01State : std::uint8_t {
  not_run = 0,
  evidence_complete = 1,
  unresolved = 2,
  capacity = 3,
  unsupported = 4
};
enum class BoardingHipJointPlaneMethod01Outcome : std::uint8_t {
  not_run = 0,
  contained = 1,
  strict_unowned_exists = 2,
  unresolved = 3
};
enum class BoardingHipJointPlaneMethod01Condition : std::uint8_t {
  none = 0,
  invalid_limits = 1,
  output_capacity = 2,
  original_unavailable = 3,
  capture_capacity = 4,
  capture_identity = 5,
  unsupported_arithmetic = 6,
  feature_capacity = 7,
  topology_unresolved = 8,
  chord_capacity = 9,
  denominator_unresolved = 10,
  classification_unresolved = 11,
  payload_allocation = 12,
  evidence_identity = 13,
  operation_capacity = 14
};
enum class BoardingHipJointPlaneMethod01Stage : std::uint8_t {
  not_run = 0,
  original_call = 1,
  capture = 2,
  chart = 3,
  vertex = 4,
  plane_gate = 5,
  center = 6,
  vertex_distance = 7,
  edge = 8,
  chord = 9,
  classification = 10,
  complete = 11
};
struct BoardingHipJointPlaneMethod01Feature {
  std::array<BoardingFootSiteScalarBounds, 3> point;
  std::uint8_t source_feature{255}, kind{255};
  std::array<std::uint8_t, 2> endpoints{255, 255};
  bool ready{};
};
struct BoardingHipJointPlaneMethod01Data {
  BoardingIntermediateEndpoint04Counters original_work;
  BoardingIntermediateEndpoint04SliceEvidence original_slice;
  std::optional<BoardingIntermediateEndpoint04Refusal> original_first_refusal;
  std::array<BoardingIntermediatePauseSourceIdentity, 2> source_identities;
  std::array<BoardingRoutePhasePartBinding, 2> selected_parts;
  std::array<BoardingPlantedLegPointBounds, 3> points, root_columns;
  std::array<BoardingFootSiteScalarBounds, 3> unit_axis, xi, center;
  RigidVector3 pelvis_half_size_metres;
  double hip_offset_metres{}, thigh_length_metres{}, radius_metres{},
      axial_limit_metres{};
  std::array<BoardingHipJointPlaneMethod01Feature, 20> features;
  std::array<BoardingFootSiteScalarBounds, 20> feature_distances;
  std::array<BoardingFootSiteScalarBounds, 190> chord_distances;
  BoardingFootSiteScalarBounds owner_extent, radius_squared,
      minimum_distance_squared;
  std::uint16_t availability{}, chord_distance_count{};
  std::uint8_t feature_count{}, feature_distance_count{};
  std::uint8_t minimum_lower_kind{255}, minimum_upper_kind{255};
  std::uint16_t minimum_lower_index{65535}, minimum_upper_index{65535};
  bool owner_branch{}, center_branch{}, feature_branch{}, minimum_complete{};
};
struct BoardingHipJointPlaneMethod01Counters {
  std::uint8_t preflight_guards{}, return_metadata_checks{}, endpoint_calls{};
  std::uint16_t capture_checks{}, feature_checks{}, chord_checks{},
      operations{};
};
struct BoardingHipJointPlaneMethod01Diagnostic {
  OriginBoardingIntermediatePauseSupport source;
  std::unique_ptr<const BoardingHipJointPlaneMethod01Data> data;
  BoardingHipJointPlaneMethod01Counters work;
  std::uint64_t operation_attempted{}, operation_written{};
  std::uint32_t feature_attempted{}, feature_written{}, original_flags{};
  std::uint8_t capture_attempted{}, capture_written{};
  std::uint8_t capture_cursor{255}, feature_cursor{255}, operation_cursor{255};
  std::uint16_t chord_cursor{65535}, operation_row{65535};
  BoardingHipJointPlaneMethod01Stage stage{}, operation_stage{};
  BoardingHipJointPlaneMethod01State state{};
  BoardingHipJointPlaneMethod01Outcome outcome{};
  BoardingHipJointPlaneMethod01Condition condition{};
  BoardingIntermediateEndpoint04State original_state{};
  BoardingIntermediateEndpoint04Condition original_condition{};
  BoardingIntermediateEndpoint04SelfStage original_stage{};
  std::uint16_t original_pair{65535};
  std::uint8_t original_region{255}, original_axis{255}, original_sign{255};
  bool original_available{}, arithmetic_supported{}, evidence_complete{};
  std::size_t required_output_bytes{}, output_capacity_bytes{};
  explicit BoardingHipJointPlaneMethod01Diagnostic(
      const OriginBoardingIntermediatePauseSupport& p)
      : source(p) {}
  BoardingHipJointPlaneMethod01Diagnostic(
      BoardingHipJointPlaneMethod01Diagnostic&&) noexcept = default;
  auto operator=(BoardingHipJointPlaneMethod01Diagnostic&&) noexcept
      -> BoardingHipJointPlaneMethod01Diagnostic& = default;
  BoardingHipJointPlaneMethod01Diagnostic(
      const BoardingHipJointPlaneMethod01Diagnostic&) = delete;
  auto operator=(const BoardingHipJointPlaneMethod01Diagnostic&)
      -> BoardingHipJointPlaneMethod01Diagnostic& = delete;
  static constexpr bool self_qualified{false}, endpoint_qualified{false},
      world_qualified{false}, route_qualified{false}, actor_qualified{false},
      seat_qualified{false}, save_qualified{false}, dynamics_qualified{false},
      material_qualified{false}, strength_qualified{false},
      friction_qualified{false}, first_flight_qualified{false};
};
using BoardingHipJointPlaneMethod01Expected =
    std::expected<BoardingHipJointPlaneMethod01Diagnostic, std::string>;
[[nodiscard]] auto assess_origin_boarding_hip_joint_plane_method01(
    const OriginBoardingIntermediatePauseSupport&)
    -> BoardingHipJointPlaneMethod01Expected;
[[nodiscard]] constexpr auto
boarding_hip_joint_plane_method01_required_output_bytes() -> std::size_t {
  return 2 * sizeof(BoardingHipJointPlaneMethod01Expected) +
         boarding_intermediate_endpoint04_required_output_bytes() +
         2 * sizeof(BoardingHipJointPlaneMethod01Data);
}
} // namespace apsis_drift
