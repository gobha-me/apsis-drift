#include "apsis_drift/station_geometry.hpp"

#include <cmath>

namespace apsis_drift {
namespace {
auto finite(RigidVector3 v) -> bool {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
auto bounds(StationBounds b) -> bool {
  return finite(b.minimum_metres) && finite(b.maximum_metres) &&
         b.minimum_metres.x < b.maximum_metres.x &&
         b.minimum_metres.y < b.maximum_metres.y &&
         b.minimum_metres.z < b.maximum_metres.z;
}
auto disjoint(StationBounds a, StationBounds b) -> bool {
  return a.maximum_metres.x < b.minimum_metres.x ||
         b.maximum_metres.x < a.minimum_metres.x ||
         a.maximum_metres.y < b.minimum_metres.y ||
         b.maximum_metres.y < a.minimum_metres.y ||
         a.maximum_metres.z < b.minimum_metres.z ||
         b.maximum_metres.z < a.minimum_metres.z;
}
auto make_geometry(OriginStationId station) -> OriginStationGeometry {
  OriginStationGeometry result;
  result.station = station;
  result.exterior = {{-43, -7, -30}, {34, 15, 60}};
  // Authored D1/D2 Blender datums minus hub (0.97,0.978,0),
  // converted by (x,y,z) -> (x,z,-y). Design decimals, not float mesh noise.
  const double yaw_component = std::sqrt(.5);
  for (std::size_t n = 0; n < result.ports.size(); ++n) {
    auto& p = result.ports[n];
    p.id = {station, static_cast<std::uint32_t>(n + 1)};
    p.collar_position_metres = {n == 0 ? -23.12 : 21.18, -1.35, 0};
    p.docked_craft_orientation = {yaw_component, 0,
                                  n == 0 ? yaw_component : -yaw_component, 0};
    const auto x = p.collar_position_metres.x;
    // Aligned measured Wayfarer and twelve-metre withdrawal column.
    p.approach_reservation = {{x - 12, -18, -5}, {x + 12, -1.30, 5}};
  }
  return result;
}
} // namespace
auto origin_station_geometry(const OriginStationDescriptor& station)
    -> std::expected<OriginStationGeometry, StationGeometryError> {
  if (station != generate_origin_station(station.universe_seed))
    return std::unexpected{StationGeometryError::invalid_owner};
  return make_geometry(station.id);
}
auto validate_origin_station_geometry(const OriginStationDescriptor& station,
                                      const OriginStationGeometry& geometry)
    -> std::expected<void, StationGeometryError> {
  using E = StationGeometryError;
  if (geometry.version != 1) return std::unexpected{E::unsupported_version};
  if (station != generate_origin_station(station.universe_seed) ||
      geometry.station != station.id)
    return std::unexpected{E::invalid_owner};
  if (geometry.ports[0].id == geometry.ports[1].id)
    return std::unexpected{E::duplicate_port};
  if (!bounds(geometry.exterior)) return std::unexpected{E::invalid_geometry};
  for (const auto& p : geometry.ports) {
    if (!finite(p.collar_position_metres) || !finite(p.outward_normal) ||
        !finite(p.craft_collar_metres) || !bounds(p.approach_reservation))
      return std::unexpected{E::invalid_geometry};
    for (const double v :
         {p.clear_bore_metres, p.withdrawal_metres, p.capture.separation_metres,
          p.capture.alignment_radians,
          p.capture.lateral_speed_metres_per_second,
          p.capture.inward_speed_metres_per_second,
          p.capture.angular_speed_radians_per_second})
      if (!std::isfinite(v) || v <= 0)
        return std::unexpected{E::invalid_geometry};
  }
  if (!disjoint(geometry.ports[0].approach_reservation,
                geometry.ports[1].approach_reservation) ||
      geometry != make_geometry(station.id))
    return std::unexpected{E::invalid_geometry};
  return {};
}
auto resolve_origin_port_pose(const PhysicalLocalSystem& system,
                              const OriginStationDescriptor& station,
                              const OriginStationGeometry& geometry,
                              OriginPortId port, SimulationTick tick,
                              double distance)
    -> std::expected<OriginPortPose, StationGeometryError> {
  using E = StationGeometryError;
  if (const auto valid = validate_origin_station_geometry(station, geometry);
      !valid)
    return std::unexpected{valid.error()};
  if (port.station != station.id || port.ordinal < 1 ||
      port.ordinal > geometry.ports.size())
    return std::unexpected{E::unknown_port};
  const auto& p = geometry.ports[port.ordinal - 1];
  if (!std::isfinite(distance) || distance < 0 ||
      distance > p.withdrawal_metres)
    return std::unexpected{E::invalid_distance};
  const auto ephemeris =
      resolve_origin_station_ephemeris(system, station, {tick, 0});
  if (!ephemeris) return std::unexpected{E::ephemeris_failure};
  RigidBodyState candidate;
  candidate.tick = tick;
  candidate.frame = {RigidFrameKind::station_relative_inertial,
                     system.catalog.id,
                     {},
                     station.id};
  candidate.orientation = p.docked_craft_orientation;
  // The fixed +/-90 degree yaw maps collar +Z to +/-X exactly by contract.
  candidate.position_metres = {
      p.collar_position_metres.x - (port.ordinal == 1
                                        ? p.craft_collar_metres.z
                                        : -p.craft_collar_metres.z),
      p.collar_position_metres.y - distance - p.craft_collar_metres.y,
      p.collar_position_metres.z};
  const RigidBodyWorldContext context{system, &station};
  const auto local = canonicalize_rigid_body_state(context, candidate);
  if (!local) return std::unexpected{E::invalid_result};
  const auto global = reframe_rigid_body(
      context, *local,
      {{RigidFrameKind::system_inertial, system.catalog.id, {}, {}}, tick});
  if (!global) return std::unexpected{E::invalid_result};
  const auto relative =
      reframe_rigid_body(context, *global,
                         {{RigidFrameKind::planet_relative_inertial,
                           system.catalog.id,
                           station.orbit.host_planet,
                           {}},
                          tick});
  if (!relative) return std::unexpected{E::invalid_result};
  return OriginPortPose{port,
                        *ephemeris,
                        {p.collar_position_metres.x,
                         p.collar_position_metres.y - distance,
                         p.collar_position_metres.z},
                        *local,
                        *global,
                        *relative};
}
} // namespace apsis_drift
