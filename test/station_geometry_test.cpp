#include "apsis_drift/station_geometry.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace apsis_drift;
int failures{};
auto check(bool condition, std::string_view message) -> void {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
template <class T, class E> auto required(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("station geometry fixture refused");
  return *value;
}
auto near(double a, double b, double tolerance = 1e-12) -> bool {
  return std::abs(a - b) <= tolerance;
}
auto rotate(RigidOrientation q, RigidVector3 v) -> RigidVector3 {
  // Independent matrix oracle, normalized by the supplied quaternion norm.
  const double n = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
  return {
      ((q.w * q.w + q.x * q.x - q.y * q.y - q.z * q.z) * v.x +
       2 * (q.x * q.y - q.w * q.z) * v.y + 2 * (q.x * q.z + q.w * q.y) * v.z) /
          n,
      (2 * (q.x * q.y + q.w * q.z) * v.x +
       (q.w * q.w - q.x * q.x + q.y * q.y - q.z * q.z) * v.y +
       2 * (q.y * q.z - q.w * q.x) * v.z) /
          n,
      (2 * (q.x * q.z - q.w * q.y) * v.x + 2 * (q.y * q.z + q.w * q.x) * v.y +
       (q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z) * v.z) /
          n};
}
auto invalid() -> void {
  const auto station = generate_origin_station(Seed{42});
  const auto system = required(generate_physical_origin_system(Seed{42}));
  const auto geometry = required(origin_station_geometry(station));
  auto bad = geometry;
  bad.version = 0;
  check(!validate_origin_station_geometry(station, bad),
        "unknown geometry version refuses");
  bad = geometry;
  bad.station.value ^= 1;
  check(!validate_origin_station_geometry(station, bad),
        "wrong station owner refuses");
  bad = geometry;
  bad.ports[1].id = bad.ports[0].id;
  const auto duplicate = validate_origin_station_geometry(station, bad);
  check(!duplicate && duplicate.error() == StationGeometryError::duplicate_port,
        "duplicate port identity refuses before transforms");
  for (double value : {0., -1., std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity(), 1e300}) {
    for (unsigned field = 0; field < 10; ++field) {
      bad = geometry;
      switch (field) {
        case 0: bad.ports[0].clear_bore_metres = value; break;
        case 1: bad.ports[0].withdrawal_metres = value; break;
        case 2: bad.ports[0].capture.separation_metres = value; break;
        case 3: bad.ports[0].capture.alignment_radians = value; break;
        case 4:
          bad.ports[0].capture.lateral_speed_metres_per_second = value;
          break;
        case 5:
          bad.ports[0].capture.inward_speed_metres_per_second = value;
          break;
        case 6:
          bad.ports[0].capture.angular_speed_radians_per_second = value;
          break;
        case 7: bad.ports[0].docked_craft_orientation.w = value; break;
        case 8: bad.ports[0].outward_normal.y = value; break;
        default: bad.ports[0].craft_collar_metres.y = value; break;
      }
      if (bad == geometry) continue; // -1 is the valid outward Y normal.
      check(!resolve_origin_port_pose(system, station, bad,
                                      geometry.ports[0].id, 0),
            "malformed dimensions/rating/transform refuse before pose commit");
    }
  }
  bad = geometry;
  bad.exterior.maximum_metres = bad.exterior.minimum_metres;
  check(!validate_origin_station_geometry(station, bad),
        "zero/inside-out exterior volume refuses");
  bad = geometry;
  bad.ports[0].approach_reservation.maximum_metres.y = -100;
  check(!validate_origin_station_geometry(station, bad),
        "inside-out corridor refuses");
  bad = geometry;
  bad.ports[1].approach_reservation = bad.ports[0].approach_reservation;
  check(!validate_origin_station_geometry(station, bad),
        "intersecting approach reservations refuse");
  bad = geometry;
  bad.ports[0].collar_position_metres.x =
      std::numeric_limits<double>::infinity();
  check(!validate_origin_station_geometry(station, bad),
        "nonfinite datum refuses");
  for (std::uint32_t ordinal :
       {0U, 3U, std::numeric_limits<std::uint32_t>::max()})
    check(!resolve_origin_port_pose(system, station, geometry,
                                    {station.id, ordinal}, 0),
          "port index boundaries refuse before array access");
  check(!resolve_origin_port_pose(system, station, geometry, {{0}, 1}, 0),
        "foreign port owner refuses");
  for (double distance :
       {-.001, 12.001, std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity()})
    check(!resolve_origin_port_pose(system, station, geometry,
                                    geometry.ports[0].id, 0, distance),
          "invalid/unqualified approach distance refuses");
  check(!resolve_origin_port_pose(system, station, geometry,
                                  geometry.ports[0].id,
                                  std::numeric_limits<SimulationTick>::max()),
        "overflowing authoritative clock refuses");
  const auto other = required(generate_physical_origin_system(Seed{0}));
  check(!resolve_origin_port_pose(other, station, geometry,
                                  geometry.ports[0].id, 0),
        "missing orbital owner cannot resolve station pose");
  auto orbit = station.orbit;
  orbit.radius_kilometres = 0;
  const OriginStationDescriptor invalid_station{
      station.universe_seed, station.home_system_seed, station.station_seed,
      station.id, orbit};
  check(!origin_station_geometry(invalid_station),
        "invalid orbital parent/geometry cannot acquire a contract");
  check(geometry == required(origin_station_geometry(station)),
        "refusals leave source geometry unchanged");
}
auto poses(Seed seed) -> void {
  const auto station = generate_origin_station(seed);
  const auto system = required(generate_physical_origin_system(seed));
  const auto geometry = required(origin_station_geometry(station));
  const RigidBodyWorldContext context{system, &station};
  for (SimulationTick tick :
       {SimulationTick{0}, SimulationTick{25}, station.orbit.period_ticks - 1,
        station.orbit.period_ticks,
        std::numeric_limits<SimulationTick>::max() - 1})
    for (const auto& p : geometry.ports)
      for (double distance : {0., .15, 1., 6., 12.}) {
        const auto pose = required(resolve_origin_port_pose(
            system, station, geometry, p.id, tick, distance));
        const auto ephemeris = required(
            resolve_origin_station_ephemeris(system, station, {tick, 0}));
        check(pose.station == ephemeris && pose.port == p.id,
              "port queries retain actual physical station owner/clock");
        const auto& local = pose.station_relative;
        const auto collar = rotate(local.orientation, p.craft_collar_metres);
        const double authored_x = p.id.ordinal == 1 ? -22.15 : 22.15;
        check(near(local.position_metres.x + collar.x, authored_x - .97) &&
                  near(local.position_metres.y + collar.y, -1.35 - distance) &&
                  near(local.position_metres.z + collar.z, 0),
              "independent body rotation places measured craft collar at "
              "converted authored port");
        const auto nose = rotate(local.orientation, {0, 0, -1});
        const auto up = rotate(local.orientation, {0, 1, 0});
        check(near(nose.x, p.id.ordinal == 1 ? -1 : 1) && near(nose.z, 0) &&
                  up == RigidVector3{0, 1, 0},
              "D1/D2 preserve opposite authored headings and opposing collar "
              "normals");
        check(local.linear_velocity_metres_per_second == RigidVector3{} &&
                  local.angular_velocity_radians_per_second == RigidVector3{},
              "station nonrotating axes introduce no fictional frame spin or "
              "approach velocity");
        const auto& global = pose.system_inertial;
        check(global.position_metres.x ==
                      local.position_metres.x + ephemeris.position.x &&
                  global.position_metres.y ==
                      local.position_metres.y + ephemeris.position.y &&
                  global.position_metres.z ==
                      local.position_metres.z + ephemeris.position.z &&
                  global.linear_velocity_metres_per_second ==
                      RigidVector3{ephemeris.velocity.x, ephemeris.velocity.y,
                                   ephemeris.velocity.z},
              "world pose adds the authoritative station translation/velocity "
              "once");
        check(global.tick == tick && global.orientation == local.orientation &&
                  pose.planet_relative.orientation == local.orientation &&
                  pose.planet_relative.tick == tick,
              "frame views retain clock, craft attitude and body spin");
        const auto home = required(resolve_planet_ephemeris(
            system, station.orbit.host_planet, {tick, 0}));
        check(pose.planet_relative.position_metres.x ==
                      global.position_metres.x - home.position.x &&
                  pose.planet_relative.linear_velocity_metres_per_second.y ==
                      global.linear_velocity_metres_per_second.y -
                          home.velocity.y,
              "planet-relative flight pose uses the existing physical origin "
              "subtraction");
        const auto bytes = required(
            encode_rigid_body_state_json(context, pose.planet_relative));
        const auto loaded =
            required(decode_rigid_body_state_json(context, bytes));
        check(loaded == pose.planet_relative &&
                  required(rigid_body_state_checksum(context, loaded)) ==
                      required(rigid_body_state_checksum(context,
                                                         pose.planet_relative)),
              "port pose survives explicit physical v3 projection unchanged");
        check(required(resolve_origin_port_pose(system, station, geometry, p.id,
                                                tick, distance))
                      .planet_relative == pose.planet_relative,
              "repeated geometry query cannot advance or reroll world");
      }
}
} // namespace
int main() {
  try {
    invalid();
    for (Seed seed :
         {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}})
      poses(seed);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 2;
  }
  std::cout << "station geometry: " << failures << " failures\n";
  return failures ? 1 : 0;
}
