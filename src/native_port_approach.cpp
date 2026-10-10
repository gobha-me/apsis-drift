#include "apsis_drift/native_flight_session.hpp"

#include <algorithm>
#include <cmath>

namespace apsis_drift {
namespace {
using V = RigidVector3;
auto rotate(RigidOrientation q, V v) -> V {
  const double n = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  return {(1 - 2 * (q.y * q.y + q.z * q.z) / n) * v.x +
              2 * (q.x * q.y - q.w * q.z) / n * v.y +
              2 * (q.x * q.z + q.w * q.y) / n * v.z,
          2 * (q.x * q.y + q.w * q.z) / n * v.x +
              (1 - 2 * (q.x * q.x + q.z * q.z) / n) * v.y +
              2 * (q.y * q.z - q.w * q.x) / n * v.z,
          2 * (q.x * q.z - q.w * q.y) / n * v.x +
              2 * (q.y * q.z + q.w * q.x) / n * v.y +
              (1 - 2 * (q.x * q.x + q.y * q.y) / n) * v.z};
}
auto length(V v) -> double {
  return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
} // namespace

auto NativeFreedomFlightSession::port_approach_available() const
    -> std::expected<void, std::string> {
  if (actor_ || surface_walker_)
    return std::unexpected{"Board and sit before approaching a port"};
  if (surface_ && (surface_->landed || surface_->gear_deployed))
    return std::unexpected{"Lift off and stow gear before port approach"};
  if (!docking_ || docking_->attached)
    return std::unexpected{"Select a free port before using the approach aid"};
  if (document_.model.hold.target)
    return std::unexpected{"Disable orbit hold before using the approach aid"};
  if (system_.ephemeris_version != kContinuousAnalyticEphemerisVersion)
    return std::unexpected{"This historical orbit recipe uses manual approach; "
                           "new games support the aid"};
  const auto assessed = assess_port();
  if (!assessed) return std::unexpected{assessed.error()};
  const auto& a = *assessed;
  if (a.outward_separation_metres < 0 || a.separation_metres > 150)
    return std::unexpected{
        "Approach aid needs the craft below the port within 150 m"};
  if (a.alignment_radians > .005 || a.angular_speed_radians_per_second > .002)
    return std::unexpected{"Align within 0.3 degrees and stop rotation first"};
  if (length(a.collar_velocity_station_metres_per_second) > 40)
    return std::unexpected{"Reduce station-relative speed below 40 m/s first"};
  const auto station =
      generate_origin_station(document_.origin.recipe.universe_seed);
  const auto geometry = origin_station_geometry(station);
  const auto frame = resolve_craft_frame(document_.flight.craft);
  if (!geometry || !frame)
    return std::unexpected{"Approach geometry or craft unavailable"};
  const auto& port = geometry->ports[docking_->target.ordinal - 1];
  const auto offset =
      rotate(document_.flight.orientation, port.craft_collar_metres);
  const V origin{a.collar_station_metres.x - offset.x,
                 a.collar_station_metres.y - offset.y,
                 a.collar_station_metres.z - offset.z};
  // Continue the fixed port column downward for this small approach aid. This
  // checks the stowed hull, not swept exterior/dynamic-obstacle collisions.
  for (unsigned mask = 0; mask < 8; ++mask) {
    const auto& p = frame->properties;
    const auto corner = rotate(
        document_.flight.orientation,
        {((mask & 1U) != 0 ? p.hull_max_mm[0] : p.hull_min_mm[0]) / 1000.0,
         ((mask & 2U) != 0 ? p.hull_max_mm[1] : p.hull_min_mm[1]) / 1000.0,
         ((mask & 4U) != 0 ? p.hull_max_mm[2] : p.hull_min_mm[2]) / 1000.0});
    const V point{origin.x + corner.x, origin.y + corner.y,
                  origin.z + corner.z};
    const auto& b = port.approach_reservation;
    if (point.x < b.minimum_metres.x || point.x > b.maximum_metres.x ||
        point.z < b.minimum_metres.z || point.z > b.maximum_metres.z ||
        point.y > b.maximum_metres.y || point.y < -170)
      return std::unexpected{
          "Keep the complete craft below the port in its approach column"};
  }
  return {};
}

auto NativeFreedomFlightSession::begin_port_approach()
    -> std::expected<void, std::string> {
  if (recovery_pending())
    return std::unexpected{
        "Continue the recorded loss before controlling the replacement"};
  if (travel_ && travel_->phase != FreedomJumpPhase::idle)
    return std::unexpected{"Cancel the active jump before approach"};
  if (auto valid = port_approach_available(); !valid) return valid;
  port_approach_ = {true, kNativePortApproachTicks,
                    "Approaching with thrusters; manual input cancels"};
  return {};
}
auto NativeFreedomFlightSession::cancel_port_approach() -> void {
  port_approach_ = {false, 0, "Approach aid off"};
}
auto NativeFreedomFlightSession::port_approach_controls() const
    -> std::expected<NativeFlightControls, std::string> {
  if (auto valid = port_approach_available(); !valid)
    return std::unexpected{valid.error()};
  const auto assessed = assess_port();
  if (!assessed) return std::unexpected{assessed.error()};
  const auto station =
      generate_origin_station(document_.origin.recipe.universe_seed);
  const auto geometry = origin_station_geometry(station);
  if (!geometry) return std::unexpected{"Approach geometry unavailable"};
  const auto& p = geometry->ports[docking_->target.ordinal - 1];
  const auto& a = *assessed;
  // Stop just outside the collar. The existing capture gate remains separate.
  const V target{p.collar_position_metres.x + .05 * p.outward_normal.x,
                 p.collar_position_metres.y + .05 * p.outward_normal.y,
                 p.collar_position_metres.z + .05 * p.outward_normal.z};
  const auto& v = a.collar_velocity_station_metres_per_second;
  const auto now = resolve_origin_station_ephemeris(system_, station,
                                                    {document_.flight.tick});
  const auto next = resolve_origin_station_ephemeris(
      system_, station, {document_.flight.tick + 1});
  const auto gravity =
      evaluate_central_body_gravity(RigidBodyWorldContext{system_},
                                    document_.flight, document_.model.central);
  if (!now || !next || !gravity)
    return std::unexpected{"Approach station motion unavailable"};
  // Allocate real thrust for the target ephemeris acceleration minus central
  // gravity, then close the measured relative error. Never rewrite either body.
  const auto& current_velocity = now->host_relative_velocity;
  const auto& next_velocity = next->host_relative_velocity;
  const auto& g = gravity->acceleration_metres_per_second_squared;
  const auto dt = kSimulationStep.count();
  const V demand{.5 * (target.x - a.collar_station_metres.x) - 1.4 * v.x +
                     (next_velocity.x - current_velocity.x) / dt - g.x,
                 .5 * (target.y - a.collar_station_metres.y) - 1.4 * v.y +
                     (next_velocity.y - current_velocity.y) / dt - g.y,
                 .5 * (target.z - a.collar_station_metres.z) - 1.4 * v.z +
                     (next_velocity.z - current_velocity.z) / dt - g.z};
  const auto q = document_.flight.orientation;
  const auto body = rotate({q.w, -q.x, -q.y, -q.z}, demand);
  const auto frame = resolve_craft_frame(document_.flight.craft);
  if (!frame) return std::unexpected{"Approach craft unavailable"};
  const auto& f = frame->properties;
  NativeFlightControls commands;
  const auto fraction = [mass = f.dry_mass_kg](double acceleration,
                                               std::uint32_t rating) {
    return std::clamp(acceleration * mass / rating, 0.0, 1.0);
  };
  commands.positive_translation = {
      fraction(body.x, f.positive_force_newtons[0]),
      fraction(body.y, f.positive_force_newtons[1]),
      fraction(body.z, f.positive_force_newtons[2])};
  commands.negative_translation = {
      fraction(-body.x, f.negative_force_newtons[0]),
      fraction(-body.y, f.negative_force_newtons[1]),
      fraction(-body.z, f.negative_force_newtons[2])};
  return commands;
}
} // namespace apsis_drift
