#pragma once

#include <array>

#include "apsis_drift/physical_local_system.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"

namespace apsis_drift {
struct StationBounds {
  RigidVector3 minimum_metres, maximum_metres;
  friend auto operator==(const StationBounds&, const StationBounds&)
      -> bool = default;
};
struct OriginPortId {
  OriginStationId station;
  std::uint32_t ordinal{}; // 1 = D1, 2 = D2; qualified by the station identity.
  friend auto operator==(const OriginPortId&, const OriginPortId&)
      -> bool = default;
};
struct StationCaptureTolerances {
  double separation_metres{.15};
  double alignment_radians{.05235987755982989}; // 3 degrees.
  double lateral_speed_metres_per_second{.2};
  double inward_speed_metres_per_second{.3};
  double angular_speed_radians_per_second{.02};
  friend auto operator==(const StationCaptureTolerances&,
                         const StationCaptureTolerances&) -> bool = default;
};
struct OriginDockPort {
  OriginPortId id;
  RigidVector3 collar_position_metres;
  RigidVector3 outward_normal{0, -1, 0};
  RigidOrientation docked_craft_orientation;
  // Explicit Wayfarer HCD-90 interface geometry, independent of mass/thrust.
  RigidVector3 craft_collar_metres{0, 2.907, 5.2};
  double clear_bore_metres{.9};
  double withdrawal_metres{12};
  StationBounds approach_reservation;
  StationCaptureTolerances capture;
  friend auto operator==(const OriginDockPort&, const OriginDockPort&)
      -> bool = default;
};
struct OriginStationGeometry {
  std::uint32_t version{1};
  OriginStationId station;
  // Conservative occupied exterior bounds: broad phase/navigation only.
  StationBounds exterior;
  std::array<OriginDockPort, 2> ports;
  friend auto operator==(const OriginStationGeometry&,
                         const OriginStationGeometry&) -> bool = default;
};
enum class StationGeometryError : std::uint8_t {
  unsupported_version,
  invalid_owner,
  invalid_geometry,
  duplicate_port,
  unknown_port,
  invalid_distance,
  ephemeris_failure,
  invalid_result
};
[[nodiscard]] auto origin_station_geometry(const OriginStationDescriptor&)
    -> std::expected<OriginStationGeometry, StationGeometryError>;
[[nodiscard]] auto validate_origin_station_geometry(
    const OriginStationDescriptor&, const OriginStationGeometry&)
    -> std::expected<void, StationGeometryError>;
struct OriginPortPose {
  OriginPortId port;
  OriginStationEphemeris station;
  RigidVector3 collar_station_metres;
  // Reference craft pose at the requested outward collar separation.
  // These three views use the existing same-tick frame owner.
  RigidBodyState station_relative, system_inertial, planet_relative;
};
// Query, not capture/release: distance in [0,12] is an outward collar offset.
// Station axes are system-aligned/nonrotating; no invented frame-spin velocity.
[[nodiscard]] auto resolve_origin_port_pose(const PhysicalLocalSystem&,
                                            const OriginStationDescriptor&,
                                            const OriginStationGeometry&,
                                            OriginPortId, SimulationTick,
                                            double outward_distance_metres = 0)
    -> std::expected<OriginPortPose, StationGeometryError>;
} // namespace apsis_drift
