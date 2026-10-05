#pragma once

#include "apsis_drift/origin_boarding_boot_support.hpp"
#include "apsis_drift/origin_boarding_planted_legs.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace apsis_drift {
inline constexpr std::uint32_t kBoardingSourceEndpointVersion{1};
inline constexpr std::size_t kBoardingSourceEndpointMaximumRecords{1};
struct BoardingSourceEndpointBody {
  // Canonical exact-expression bounds. The common Y is applied once externally.
  std::array<BoardingPlantedLegPointBounds, kBoardingPlantedBodyPointCount>
      points{};
  std::array<BoardingPlantedLegPointBounds, kBoardingBodyPartCount>
      mass_points{};
  BoardingPlantedLegPointBounds center_of_mass;
};
struct BoardingSourceEndpointHipRegionEvidence {
  BoardingBodyPartId first_part{BoardingBodyPartId::pelvis}, second_part{};
  // Original thigh capsule intersected with the original finite hip slab.
  double hip_limit_metres{}, axis_length_metres{}, thigh_radius_metres{};
  BoardingPlantedLegPointBounds axis;
  // Extent and limit are scaled by the exact nominal L1, units square metres.
  BoardingPlantedLegScalarBounds scaled_pelvis_extent, scaled_hip_limit,
      scaled_gap;
  bool bindings_valid{}, link_identity{}, structural_x_zero{},
      arithmetic_supported{}, certified{};
};
enum class BoardingSourceEndpointCondition : std::uint8_t {
  sites_prerequisite,
  body_capacity,
  unsupported_arithmetic,
  leg_closure,
  body_bindings,
  hip_region_guard,
  hip_region_support
};
struct BoardingSourceEndpointRefusal {
  BoardingSourceEndpointCondition condition{};
  std::optional<std::size_t> side;
  BoardingPlantedLegCondition leg_condition{BoardingPlantedLegCondition::none};
};
struct BoardingSourceEndpointDiagnostic {
  // Owns the unchanged Sites02 result and its immutable source lifetime.
  BoardingFootSitesDiagnostic sites;
  std::uint32_t endpoint_version{kBoardingSourceEndpointVersion};
  double common_translation_y_metres{.847}, root_z_metres{-.55};
  std::array<double, 2> port_x_terms{.16, -.14}, starboard_x_terms{.16, .14};
  std::array<BoardingPlantedLegEvidence, 2> legs{};
  std::array<BoardingPlantedBodyPartBinding, kBoardingBodyPartCount> parts{};
  std::optional<BoardingSourceEndpointBody> body;
  std::array<BoardingSourceEndpointHipRegionEvidence, 2> hips{};
  std::size_t evaluated_legs{}, assembled_records{}, examined_hip_regions{};
  bool arithmetic_supported{}, plane_identities{}, link_identities{},
      joint_limits_certified{}, reservations_complete{}, mass_model_complete{},
      hip_regions_certified{}, complete{};
  std::optional<BoardingSourceEndpointRefusal> first_refusal;
  // Complete covers this endpoint and two hip pairs, never all105 self pairs.
  static constexpr bool timing_qualified{false}, derivatives_qualified{false},
      dynamics_qualified{false}, self_qualified{false}, force_qualified{false},
      load_qualified{false}, world_qualified{false}, crop_qualified{false},
      sweep_qualified{false}, acquisition_qualified{false},
      free_foot_swing_qualified{false}, transfer_qualified{false},
      route_qualified{false}, actor_qualified{false}, seat_qualified{false},
      save_qualified{false}, first_flight_qualified{false};
};
// One compiled source-bound point, no clock, caller pose or supplied geometry.
[[nodiscard]] auto assess_origin_boarding_source_endpoint(
    const OriginBoardingBootSupport&)
    -> std::expected<BoardingSourceEndpointDiagnostic, std::string>;
} // namespace apsis_drift
