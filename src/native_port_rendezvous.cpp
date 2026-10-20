#include "apsis_drift/native_flight_session.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace apsis_drift {
namespace {
using V = RigidVector3;
auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(V a, double b) -> V {
  return {a.x * b, a.y * b, a.z * b};
}
auto dot(V a, V b) -> double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto finite(V v) -> bool {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
auto length(V a) -> double {
  return std::sqrt(dot(a, a));
}
auto unit(V a) -> std::optional<V> {
  const auto n = length(a);
  if (!finite(a) || !std::isfinite(n) || n <= 0) return {};
  const auto result = scale(a, 1 / n);
  if (!finite(result)) return {};
  return result;
}
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
auto controls_finite(const NativeFlightControls& c) -> bool {
  return finite(c.positive_translation) && finite(c.negative_translation) &&
         finite(c.positive_rotation) && finite(c.negative_rotation);
}
} // namespace

auto NativeFreedomFlightSession::port_rendezvous_conditions(bool initial) const
    -> std::expected<void, std::string> {
  if (recovery_pending() ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle))
    return std::unexpected{
        "Rendezvous requires an active voyage and idle jump"};
  if (moon_ || minor_ || document_.moon || document_.minor ||
      !starting_assembly_ || !boarding_ ||
      boarding_->phase != FreedomBoardingPhase::seated || actor_ ||
      surface_walker_ || !docking_ || docking_->attached)
    return std::unexpected{
        "Rendezvous requires a seated free craft and selected Origin port"};
  if ((surface_ && (surface_->gear_deployed || surface_->landed ||
                    surface_->moon_landed)) ||
      surface_maneuver_.kind != NativeSurfaceManeuverKind::off ||
      document_.model.hold.target)
    return std::unexpected{
        "Stow gear and disable other maneuvers or orbit hold first"};
  if (system_.generator_version !=
          kContinuousPhysicalLocalSystemGeneratorVersion ||
      system_.ephemeris_version != kContinuousAnalyticEphemerisVersion ||
      document_.model.physical_catalog !=
          kContinuousPhysicalLocalSystemGeneratorVersion ||
      document_.model.physical_ephemeris != kContinuousAnalyticEphemerisVersion)
    return std::unexpected{"Historical orbit recipes retain manual rendezvous"};
  const auto origin = generate_physical_origin_system(
      document_.origin.recipe.universe_seed,
      kContinuousPhysicalLocalSystemGeneratorVersion);
  if (!origin || *origin != system_)
    return std::unexpected{
        "Rendezvous belongs to the authenticated Origin home system"};
  if (document_.flight.frame.kind == RigidFrameKind::system_inertial &&
      (document_.model.frame_policy != kFreedomReleaseSystemFramePolicy ||
       condition_))
    return std::unexpected{"Unsupported system-flight rendezvous selection"};
  const auto body = relative_state();
  if (!body || body->frame.kind != RigidFrameKind::planet_relative_inertial ||
      body->frame.planet != document_.origin.recipe.home_planet)
    return std::unexpected{
        "Rendezvous requires the genuine home-planet reference"};
  if (body->tick >= std::numeric_limits<SimulationTick>::max() - 2)
    return std::unexpected{
        "Rendezvous clock cannot retain an advanceable tick"};
  if (!resources_ || !resources_->flight_quanta ||
      !validate_freedom_resources(*resources_, document_))
    return std::unexpected{"Rendezvous requires valid flight fuel"};
  const auto frame = resolve_craft_frame(body->craft);
  if (!frame || !frame->properties.dry_mass_kg)
    return std::unexpected{"Rendezvous craft ratings unavailable"};
  const auto& f = frame->properties;
  for (std::size_t i = 0; i < 3; ++i)
    if (!f.positive_force_newtons[i] || !f.negative_force_newtons[i] ||
        !f.principal_inertia_kg_m2[i] || !f.torque_newton_metres[i] ||
        !f.max_angular_rate_milliradians_per_second[i])
      return std::unexpected{
          "Rendezvous requires finite nonzero actuator ratings"};
  VacuumIntent neutral;
  neutral.assistance = document_.model.assistance;
  const auto air = evaluate_atmospheric_flight(
      RigidBodyWorldContext{system_}, *body, neutral, rotation_,
      document_.model.atmosphere, document_.model.central);
  if (!air || !std::isfinite(air->altitude_metres) ||
      !std::isfinite(air->space_boundary_altitude_metres) ||
      air->altitude_metres <= air->space_boundary_altitude_metres)
    return std::unexpected{"Reach clear space before Origin rendezvous"};
  const auto a = assess_port();
  if (!a || !finite(a->collar_station_metres) ||
      !finite(a->collar_velocity_station_metres_per_second) ||
      !std::isfinite(a->separation_metres) ||
      !std::isfinite(a->outward_separation_metres) ||
      !std::isfinite(a->lateral_separation_metres) ||
      !std::isfinite(a->alignment_radians) ||
      !std::isfinite(a->angular_speed_radians_per_second))
    return std::unexpected{"Rendezvous port geometry or motion unavailable"};
  if (a->separation_metres > 50000 || a->outward_separation_metres < 0)
    return std::unexpected{
        "Rendezvous needs the approach side within 50 km of Origin"};
  if (initial &&
      (a->separation_metres <= 150 || a->outward_separation_metres < 150 ||
       a->lateral_separation_metres > 5000 ||
       length(a->collar_velocity_station_metres_per_second) > 40 ||
       a->alignment_radians > .35 || a->angular_speed_radians_per_second > .02))
    return std::unexpected{
        "Start 150 m to 50 km below the port, within 5 km laterally, aligned "
        "within 0.35 rad, below 40 m/s and 0.02 rad/s"};
  return {};
}

auto NativeFreedomFlightSession::port_rendezvous_available() const
    -> std::expected<void, std::string> {
  return port_rendezvous_conditions(true);
}
auto NativeFreedomFlightSession::begin_port_rendezvous()
    -> std::expected<void, std::string> {
  if (auto valid = port_rendezvous_available(); !valid) return valid;
  const auto demand = port_rendezvous_controls();
  if (!demand) return std::unexpected{demand.error()};
  // Everything that may refuse precedes the transient command mutation. The
  // canonical body, current assistance and complete saved voyage stay intact.
  cancel_port_approach();
  rendezvous_held_ = *demand;
  rendezvous_held_ticks_ = kMaxCatchUpSteps;
  port_rendezvous_ = {true, kNativePortRendezvousTicks,
                      "Rendezvous with thrusters; manual input cancels; "
                      "capture stays explicit"};
  return {};
}
auto NativeFreedomFlightSession::cancel_port_rendezvous() -> void {
  port_rendezvous_ = {};
  rendezvous_held_ = {};
  rendezvous_held_ticks_ = 0;
}

auto NativeFreedomFlightSession::port_rendezvous_controls() const
    -> std::expected<NativeFlightControls, std::string> {
  const auto body = relative_state();
  const auto station =
      generate_origin_station(document_.origin.recipe.universe_seed);
  const auto geometry = origin_station_geometry(station);
  if (!body || !geometry || !docking_ || docking_->target.ordinal == 0 ||
      docking_->target.ordinal > geometry->ports.size())
    return std::unexpected{"Rendezvous reference geometry unavailable"};
  const auto dock = resolve_origin_port_pose(system_, station, *geometry,
                                             docking_->target, body->tick);
  const auto frame = resolve_craft_frame(body->craft);
  const auto gravity = evaluate_central_body_gravity(
      RigidBodyWorldContext{system_}, *body, document_.model.central);
  if (!dock || !frame || !gravity)
    return std::unexpected{
        "Rendezvous ephemeris, craft or gravity unavailable"};
  // Station-local axes are system-aligned and nonrotating. The authored
  // outward normal is already in these inertial axes; dock.orientation is
  // craft alignment and must not rotate that station-space vector again.
  const auto& port = geometry->ports[docking_->target.ordinal - 1];
  const auto& target = dock->planet_relative;
  const auto goal = add(target.position_metres, scale(port.outward_normal, 50));
  const auto p = body->position_metres;
  const auto v = body->linear_velocity_metres_per_second;
  V acceleration{};
  std::array<V, 3> axes{};
  if (length(sub(goal, p)) > 10000) {
    const auto r = length(p);
    const auto radial = unit(p);
    const auto orbital_plane = unit(cross(
        target.position_metres, target.linear_velocity_metres_per_second));
    const auto target_radial = unit(goal);
    if (!std::isfinite(r) || r <= 0 || !radial || !orbital_plane ||
        !target_radial)
      return std::unexpected{"Rendezvous orbital basis is degenerate"};
    const auto forward = unit(cross(*orbital_plane, *radial));
    const auto target_forward = unit(cross(*orbital_plane, *target_radial));
    const auto goal_radius = length(goal);
    if (!forward || !target_forward || !std::isfinite(goal_radius) ||
        goal_radius <= 0)
      return std::unexpected{"Rendezvous target direction is degenerate"};
    const auto angle =
        std::atan2(dot(*orbital_plane, cross(*radial, *target_radial)),
                   dot(*radial, *target_radial));
    const auto omega =
        dot(target.linear_velocity_metres_per_second, *target_forward) /
        goal_radius;
    const auto goal_velocity = add(
        scale(*forward, r * (omega + std::clamp(angle * .008, -.0005, .0005))),
        scale(*orbital_plane,
              dot(target.linear_velocity_metres_per_second, *orbital_plane)));
    const auto radial_speed = dot(v, *radial);
    const auto tangent = sub(v, scale(*radial, radial_speed));
    const auto radial_goal = std::clamp((goal_radius - r) * .03, -1200., 1200.);
    acceleration = sub(
        add(add(scale(*radial, (radial_goal - radial_speed) * .1 -
                                   dot(tangent, tangent) / r),
                scale(sub(goal_velocity, tangent), .05)),
            scale(*orbital_plane, dot(sub(goal, p), *orbital_plane) * .0002)),
        gravity->acceleration_metres_per_second_squared);
    const auto direction = unit(acceleration);
    if (!direction)
      return std::unexpected{"Rendezvous force direction is degenerate"};
    const auto back = scale(*direction, -1);
    const auto up = unit(cross(back, *orbital_plane));
    if (!up) return std::unexpected{"Rendezvous attitude basis is degenerate"};
    const auto right = unit(cross(*up, back));
    if (!right)
      return std::unexpected{"Rendezvous attitude basis is degenerate"};
    axes = {*right, *up, back};
  } else {
    const auto next = resolve_origin_port_pose(
        system_, station, *geometry, docking_->target, body->tick + 1);
    if (!next) return std::unexpected{"Rendezvous next port pose unavailable"};
    const auto station_acceleration =
        scale(sub(next->planet_relative.linear_velocity_metres_per_second,
                  target.linear_velocity_metres_per_second),
              kSimulationHz);
    acceleration = sub(
        add(add(scale(sub(goal, p), .0008),
                scale(sub(target.linear_velocity_metres_per_second, v), .06)),
            station_acceleration),
        gravity->acceleration_metres_per_second_squared);
    axes = {rotate(target.orientation, {1, 0, 0}),
            rotate(target.orientation, {0, 1, 0}),
            rotate(target.orientation, {0, 0, 1})};
  }
  if (!finite(acceleration) || !finite(axes[0]) || !finite(axes[1]) ||
      !finite(axes[2]))
    return std::unexpected{"Rendezvous arithmetic is nonfinite"};
  const auto inverse =
      RigidOrientation{body->orientation.w, -body->orientation.x,
                       -body->orientation.y, -body->orientation.z};
  V error{};
  constexpr std::array<V, 3> basis{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  for (std::size_t i = 0; i < basis.size(); ++i)
    error = add(error, cross(rotate(body->orientation, basis[i]), axes[i]));
  const auto angular = sub(rotate(inverse, scale(error, 1.0)),
                           scale(body->angular_velocity_radians_per_second, 2));
  const auto body_acceleration = rotate(inverse, acceleration);
  const auto rate = rotate(inverse, error);
  if (!finite(angular) || !finite(body_acceleration) || !finite(rate))
    return std::unexpected{"Rendezvous actuator demand is nonfinite"};
  const auto& f = frame->properties;
  const std::array translation{body_acceleration.x, body_acceleration.y,
                               body_acceleration.z};
  const std::array rotation{angular.x, angular.y, angular.z};
  const std::array desired_rate{rate.x, rate.y, rate.z};
  NativeFlightControls commands;
  const std::array positive{&commands.positive_translation.x,
                            &commands.positive_translation.y,
                            &commands.positive_translation.z};
  const std::array negative{&commands.negative_translation.x,
                            &commands.negative_translation.y,
                            &commands.negative_translation.z};
  const std::array positive_rotation{&commands.positive_rotation.x,
                                     &commands.positive_rotation.y,
                                     &commands.positive_rotation.z};
  const std::array negative_rotation{&commands.negative_rotation.x,
                                     &commands.negative_rotation.y,
                                     &commands.negative_rotation.z};
  for (std::size_t i = 0; i < 3; ++i) {
    const auto positive_demand =
        translation[i] * f.dry_mass_kg / f.positive_force_newtons[i];
    const auto negative_demand =
        -translation[i] * f.dry_mass_kg / f.negative_force_newtons[i];
    if (!std::isfinite(positive_demand) || !std::isfinite(negative_demand))
      return std::unexpected{"Rendezvous translation allocation is nonfinite"};
    *positive[i] = std::clamp(positive_demand, 0.0, 1.0);
    *negative[i] = std::clamp(negative_demand, 0.0, 1.0);
    const auto demand =
        document_.model.assistance
            ? desired_rate[i] /
                  (f.max_angular_rate_milliradians_per_second[i] * .001)
            : rotation[i] * static_cast<double>(f.principal_inertia_kg_m2[i]) /
                  f.torque_newton_metres[i];
    if (!std::isfinite(demand))
      return std::unexpected{"Rendezvous rotation allocation is nonfinite"};
    *positive_rotation[i] = std::clamp(demand, 0.0, 1.0);
    *negative_rotation[i] = std::clamp(-demand, 0.0, 1.0);
  }
  if (!controls_finite(commands))
    return std::unexpected{"Rendezvous allocation is nonfinite"};
  return commands;
}

auto NativeFreedomFlightSession::advance_port_rendezvous(
    const NativeFlightControls& controls, SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (!std::isfinite(step.count()) || step != kSimulationStep)
    return std::unexpected{"Rendezvous requires one fixed 120 Hz tick"};
  const auto body = relative_state();
  const VacuumIntent intent{
      controls.positive_translation, controls.negative_translation,
      controls.positive_rotation, controls.negative_rotation,
      document_.model.assistance};
  if (!body || !evaluate_atmospheric_flight(
                   RigidBodyWorldContext{system_}, *body, intent, rotation_,
                   document_.model.atmosphere, document_.model.central))
    return std::unexpected{"Rendezvous flight input or observation refused"};
  auto candidate = *this;
  auto requested = controls;
  const bool manual = controls.positive_translation != V{} ||
                      controls.negative_translation != V{} ||
                      controls.positive_rotation != V{} ||
                      controls.negative_rotation != V{};
  if (manual) {
    candidate.cancel_port_rendezvous();
    candidate.port_rendezvous_.note = "Rendezvous canceled by manual control";
  } else if (const auto valid = port_rendezvous_conditions(false); !valid) {
    candidate.cancel_port_rendezvous();
    candidate.port_rendezvous_.note = "Rendezvous stopped: " + valid.error();
  } else if (!port_rendezvous_.remaining_ticks) {
    candidate.cancel_port_rendezvous();
    candidate.port_rendezvous_.note =
        "Rendezvous timed out; resume manual control";
  } else {
    if (!rendezvous_held_ticks_) {
      const auto demand = port_rendezvous_controls();
      if (!demand) {
        candidate.cancel_port_rendezvous();
        candidate.port_rendezvous_.note =
            "Rendezvous stopped: " + demand.error();
      } else {
        candidate.rendezvous_held_ = *demand;
        candidate.rendezvous_held_ticks_ = kMaxCatchUpSteps;
      }
    }
    if (candidate.port_rendezvous_.active) {
      requested = candidate.rendezvous_held_;
      --candidate.rendezvous_held_ticks_;
      --candidate.port_rendezvous_.remaining_ticks;
    }
  }
  const auto result = candidate.advance_craft_tick(requested, step);
  if (!result) return std::unexpected{result.error()};
  if (result->propulsion_refused) {
    candidate.cancel_port_rendezvous();
    candidate.port_rendezvous_.note =
        "Rendezvous stopped: insufficient flight fuel";
  } else if (candidate.port_rendezvous_.active) {
    if (candidate.port_approach_available()) {
      // This committed far tick reaches the unchanged near gate. Arming the
      // old controller adds no tick and never captures the port. Its FIRST
      // physical command runs only on the following ordinary flight tick.
      if (auto close = candidate.begin_port_approach(); !close)
        return std::unexpected{close.error()};
      candidate.port_rendezvous_.note =
          "Near port; close-range aid active; capture stays explicit";
    } else if (!candidate.port_rendezvous_.remaining_ticks) {
      candidate.cancel_port_rendezvous();
      candidate.port_rendezvous_.note =
          "Rendezvous timed out; resume manual control";
    }
  }
  *this = std::move(candidate);
  return *result;
}
} // namespace apsis_drift
