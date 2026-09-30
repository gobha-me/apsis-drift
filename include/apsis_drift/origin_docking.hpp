#pragma once

#include "apsis_drift/station_geometry.hpp"

namespace apsis_drift {
enum class OriginDockDecision : std::uint8_t {
  capture_ready,
  incompatible_craft,
  wrong_side,
  wrong_attitude,
  outside_reservation,
  too_far,
  retreating,
  excessive_closure,
  excessive_lateral_motion,
  excessive_angular_motion
};
enum class OriginDockError : std::uint8_t {
  invalid_geometry,
  unknown_port,
  invalid_state,
  unsupported_frame,
  handoff_failure,
  invalid_result,
  capture_refused,
  invalid_constraint
};
struct OriginDockAssessment {
  OriginPortId port;
  SimulationTick tick{};
  OriginDockDecision decision{OriginDockDecision::capture_ready};
  RigidVector3 collar_station_metres, collar_velocity_station_metres_per_second;
  double separation_metres{}, outward_separation_metres{},
      lateral_separation_metres{}, alignment_radians{},
      inward_speed_metres_per_second{}, lateral_speed_metres_per_second{},
      angular_speed_radians_per_second{};
  bool hull_inside_reservation{};
  friend auto operator==(const OriginDockAssessment&,
                         const OriginDockAssessment&) -> bool = default;
};
// A mechanical attachment at one tick, not a player save or mission result.
// The journey owner must persist these fields and synchronize its clock.
struct OriginDockConstraint {
  std::uint32_t geometry_version{1};
  OriginPortId port;
  CraftFrameRecipe craft{kWayfarerFrameId, kWayfarerFrameVersion};
  SimulationTick tick{};
  friend auto operator==(const OriginDockConstraint&,
                         const OriginDockConstraint&) -> bool = default;
};
// Pure queries: canonical pose/rates, immutable geometry and same-tick frames.
// Clearance here means the complete stowed hull fits the fixed reserved port
// column. It is not a swept exterior/dynamic-obstruction collision service.
[[nodiscard]] auto assess_origin_dock(const PhysicalLocalSystem&,
                                      const OriginStationDescriptor&,
                                      const OriginStationGeometry&,
                                      OriginPortId, const RigidBodyState&)
    -> std::expected<OriginDockAssessment, OriginDockError>;
[[nodiscard]] auto capture_origin_port(const PhysicalLocalSystem&,
                                       const OriginStationDescriptor&,
                                       const OriginStationGeometry&,
                                       OriginPortId, const RigidBodyState&)
    -> std::expected<OriginDockConstraint, OriginDockError>;
// The constrained pose and its release pose are identical at this tick.
// Retains station co-motion; no advance, force impulse, refill or new spawn.
[[nodiscard]] auto resolve_origin_docked_pose(const PhysicalLocalSystem&,
                                              const OriginStationDescriptor&,
                                              const OriginStationGeometry&,
                                              const OriginDockConstraint&)
    -> std::expected<OriginPortPose, OriginDockError>;
[[nodiscard]] auto release_origin_port(const PhysicalLocalSystem&,
                                       const OriginStationDescriptor&,
                                       const OriginStationGeometry&,
                                       const OriginDockConstraint&)
    -> std::expected<RigidBodyState, OriginDockError>;
} // namespace apsis_drift
