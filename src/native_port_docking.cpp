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
auto scale(V a, double s) -> V {
  return {a.x * s, a.y * s, a.z * s};
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto finite(V a) -> bool {
  return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
auto length(V a) -> double {
  return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
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
auto latch_ready(const OriginDockAssessment& a) -> bool {
  return a.decision == OriginDockDecision::capture_ready &&
         a.separation_metres <= kNativePortDockingLatchMetres &&
         a.alignment_radians <= kNativePortDockingLatchRadians &&
         length(a.collar_velocity_station_metres_per_second) <=
             kNativePortDockingLatchSpeed &&
         a.angular_speed_radians_per_second <=
             kNativePortDockingLatchAngularSpeed;
}
} // namespace

auto NativeFreedomFlightSession::port_docking_conditions(bool initial) const
    -> std::expected<void, std::string> {
  if (docking_ && origin_assistance_port_reserved(docking_->target))
    return std::unexpected{"D2 is reserved for the Origin service craft"};
  if (recovery_pending() ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle))
    return std::unexpected{"Docking requires an active voyage and idle jump"};
  if (moon_ || minor_ || document_.moon || document_.minor ||
      !starting_assembly_ || !boarding_ ||
      boarding_->phase != FreedomBoardingPhase::seated || actor_ ||
      surface_walker_ || !docking_ || docking_->attached)
    return std::unexpected{
        "Sit in a free craft and select an Origin port before docking"};
  if ((surface_ && (surface_->gear_deployed || surface_->landed ||
                    surface_->moon_landed)) ||
      surface_maneuver_.kind != NativeSurfaceManeuverKind::off ||
      document_.model.hold.target)
    return std::unexpected{
        "Stow gear and disable other maneuvers or orbit hold first"};
  const auto origin = generate_physical_origin_system(
      document_.origin.recipe.universe_seed,
      kContinuousPhysicalLocalSystemGeneratorVersion);
  if (!origin || *origin != system_ ||
      system_.ephemeris_version != kContinuousAnalyticEphemerisVersion ||
      document_.model.physical_catalog !=
          kContinuousPhysicalLocalSystemGeneratorVersion ||
      document_.model.physical_ephemeris != kContinuousAnalyticEphemerisVersion)
    return std::unexpected{
        "Docking guidance requires the continuous Origin home recipe"};
  if (document_.flight.frame.kind == RigidFrameKind::system_inertial &&
      (document_.model.frame_policy != kFreedomReleaseSystemFramePolicy ||
       condition_))
    return std::unexpected{"Unsupported system-flight docking selection"};
  const auto body = relative_state();
  if (!body || body->frame.kind != RigidFrameKind::planet_relative_inertial ||
      body->frame.planet != document_.origin.recipe.home_planet ||
      body->tick >= std::numeric_limits<SimulationTick>::max() - 2)
    return std::unexpected{
        "Docking reference or advanceable clock unavailable"};
  if (!resources_ || !resources_->flight_quanta ||
      !validate_freedom_resources(*resources_, document_))
    return std::unexpected{"Docking requires valid flight fuel"};
  VacuumIntent neutral;
  neutral.assistance = document_.model.assistance;
  const auto air = evaluate_atmospheric_flight(
      RigidBodyWorldContext{system_}, *body, neutral, rotation_,
      document_.model.atmosphere, document_.model.central);
  if (!air || !std::isfinite(air->altitude_metres) ||
      !std::isfinite(air->space_boundary_altitude_metres) ||
      air->altitude_metres <= air->space_boundary_altitude_metres)
    return std::unexpected{"Reach clear space before docking"};
  const auto a = assess_port();
  if (!a || !finite(a->collar_station_metres) ||
      !finite(a->collar_velocity_station_metres_per_second) ||
      !std::isfinite(a->separation_metres) ||
      !std::isfinite(a->alignment_radians) ||
      !std::isfinite(a->outward_separation_metres) ||
      !std::isfinite(a->angular_speed_radians_per_second))
    return std::unexpected{"Docking collar geometry or motion unavailable"};
  if (a->outward_separation_metres < 0 || a->separation_metres > 150)
    return std::unexpected{"Dock from the outward approach side within 150 m"};
  if (initial && (a->alignment_radians > .35 ||
                  a->angular_speed_radians_per_second > .02 ||
                  length(a->collar_velocity_station_metres_per_second) > 5))
    return std::unexpected{"Dock within 0.35 rad alignment, 0.02 rad/s "
                           "rotation and 5 m/s relative speed"};
  const auto station =
      generate_origin_station(document_.origin.recipe.universe_seed);
  const auto geometry = origin_station_geometry(station);
  const auto frame = resolve_craft_frame(body->craft);
  if (!geometry || !frame || body->craft != wayfarer_frame().recipe)
    return std::unexpected{
        "Docking requires the supported craft and Origin geometry"};
  const auto& port = geometry->ports[docking_->target.ordinal - 1];
  const auto craft_origin =
      sub(a->collar_station_metres,
          rotate(body->orientation, port.craft_collar_metres));
  for (unsigned mask = 0; mask < 8; ++mask) {
    const auto& f = frame->properties;
    const auto corner = add(
        craft_origin,
        rotate(body->orientation,
               {((mask & 1U) ? f.hull_max_mm[0] : f.hull_min_mm[0]) / 1000.0,
                ((mask & 2U) ? f.hull_max_mm[1] : f.hull_min_mm[1]) / 1000.0,
                ((mask & 4U) ? f.hull_max_mm[2] : f.hull_min_mm[2]) / 1000.0}));
    const auto& b = port.approach_reservation;
    if (!finite(corner) || corner.x < b.minimum_metres.x ||
        corner.x > b.maximum_metres.x || corner.z < b.minimum_metres.z ||
        corner.z > b.maximum_metres.z || corner.y < -170 ||
        corner.y > b.maximum_metres.y)
      return std::unexpected{
          "Keep the complete stowed hull in the port approach column"};
  }
  return {};
}

auto NativeFreedomFlightSession::port_docking_available() const
    -> std::expected<void, std::string> {
  return port_docking_conditions(true);
}

auto NativeFreedomFlightSession::begin_port_docking()
    -> std::expected<void, std::string> {
  if (auto valid = port_docking_available(); !valid) return valid;
  auto candidate = *this;
  const auto body = relative_state();
  const auto a = assess_port();
  if (!body || !a) return std::unexpected{"Docking reference unavailable"};
  candidate.document_.flight = *body;
  candidate.cancel_port_approach();
  candidate.port_docking_ = {true,
                             kNativePortDockingTicks,
                             NativePortDockingPhase::aligning,
                             "Aligning with thrusters; manual input cancels",
                             std::max(2.0, a->outward_separation_metres),
                             {}};
  if (auto demand = candidate.port_docking_controls(); !demand)
    return std::unexpected{demand.error()};
  const auto saved = candidate.player_save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}

auto NativeFreedomFlightSession::cancel_port_docking() -> void {
  port_docking_ = {};
}

auto NativeFreedomFlightSession::port_docking_controls() const
    -> std::expected<NativeFlightControls, std::string> {
  const auto a = assess_port();
  const auto body = relative_state();
  const auto station =
      generate_origin_station(document_.origin.recipe.universe_seed);
  const auto geometry = origin_station_geometry(station);
  if (!a || !body || !geometry)
    return std::unexpected{"Docking control reference unavailable"};
  const auto& port = geometry->ports[docking_->target.ordinal - 1];
  const auto distance = port_docking_.phase == NativePortDockingPhase::aligning
                            ? port_docking_.alignment_outward_metres
                            : .0005;
  const auto target =
      add(port.collar_position_metres, scale(port.outward_normal, distance));
  const auto now =
      resolve_origin_station_ephemeris(system_, station, {body->tick});
  const auto next =
      resolve_origin_station_ephemeris(system_, station, {body->tick + 1});
  const auto gravity = evaluate_central_body_gravity(
      RigidBodyWorldContext{system_}, *body, document_.model.central);
  const auto frame = resolve_craft_frame(body->craft);
  if (!now || !next || !gravity || !frame)
    return std::unexpected{"Docking ephemeris, gravity or craft unavailable"};
  const auto acceleration = sub(
      add(sub(scale(sub(target, a->collar_station_metres), .5),
              scale(a->collar_velocity_station_metres_per_second, 1.4)),
          scale(
              {next->host_relative_velocity.x - now->host_relative_velocity.x,
               next->host_relative_velocity.y - now->host_relative_velocity.y,
               next->host_relative_velocity.z - now->host_relative_velocity.z},
              kSimulationHz)),
      gravity->acceleration_metres_per_second_squared);
  const auto q = body->orientation;
  const RigidOrientation inverse{q.w, -q.x, -q.y, -q.z};
  constexpr std::array<V, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  V error{};
  for (const auto axis : axes)
    error = add(error, cross(rotate(q, axis),
                             rotate(port.docked_craft_orientation, axis)));
  const auto local_error = rotate(inverse, error);
  const auto angular =
      sub(local_error, scale(body->angular_velocity_radians_per_second, 2));
  const auto linear = rotate(inverse, acceleration);
  if (!finite(linear) || !finite(angular) || !finite(local_error))
    return std::unexpected{"Docking actuator demand is nonfinite"};
  const auto& f = frame->properties;
  const std::array translation{linear.x, linear.y, linear.z};
  const std::array rotation{angular.x, angular.y, angular.z};
  const std::array rate{local_error.x, local_error.y, local_error.z};
  NativeFlightControls c;
  const std::array positive{&c.positive_translation.x,
                            &c.positive_translation.y,
                            &c.positive_translation.z};
  const std::array negative{&c.negative_translation.x,
                            &c.negative_translation.y,
                            &c.negative_translation.z};
  const std::array positive_rotation{
      &c.positive_rotation.x, &c.positive_rotation.y, &c.positive_rotation.z};
  const std::array negative_rotation{
      &c.negative_rotation.x, &c.negative_rotation.y, &c.negative_rotation.z};
  for (std::size_t i = 0; i < 3; ++i) {
    if (!f.positive_force_newtons[i] || !f.negative_force_newtons[i] ||
        !f.principal_inertia_kg_m2[i] || !f.torque_newton_metres[i] ||
        !f.max_angular_rate_milliradians_per_second[i])
      return std::unexpected{"Docking actuator ratings unavailable"};
    const double p =
        translation[i] * f.dry_mass_kg / f.positive_force_newtons[i];
    const double n =
        -translation[i] * f.dry_mass_kg / f.negative_force_newtons[i];
    const double turn =
        document_.model.assistance
            ? rate[i] / (f.max_angular_rate_milliradians_per_second[i] * .001)
            : rotation[i] * static_cast<double>(f.principal_inertia_kg_m2[i]) /
                  f.torque_newton_metres[i];
    if (!std::isfinite(p) || !std::isfinite(n) || !std::isfinite(turn))
      return std::unexpected{"Docking allocation is nonfinite"};
    *positive[i] = std::clamp(p, 0., 1.);
    *negative[i] = std::clamp(n, 0., 1.);
    *positive_rotation[i] = std::clamp(turn, 0., 1.);
    *negative_rotation[i] = std::clamp(-turn, 0., 1.);
  }
  return c;
}

auto NativeFreedomFlightSession::advance_port_docking(
    const NativeFlightControls& controls, SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (!std::isfinite(step.count()) || step != kSimulationStep)
    return std::unexpected{"Docking requires one fixed 120 Hz tick"};
  const auto body = relative_state();
  const VacuumIntent intent{
      controls.positive_translation, controls.negative_translation,
      controls.positive_rotation, controls.negative_rotation,
      document_.model.assistance};
  if (!body || !evaluate_atmospheric_flight(
                   RigidBodyWorldContext{system_}, *body, intent, rotation_,
                   document_.model.atmosphere, document_.model.central))
    return std::unexpected{"Docking flight input or observation refused"};
  auto candidate = *this;
  auto requested = controls;
  const bool manual = controls.positive_translation != V{} ||
                      controls.negative_translation != V{} ||
                      controls.positive_rotation != V{} ||
                      controls.negative_rotation != V{};
  if (manual) {
    candidate.cancel_port_docking();
    candidate.port_docking_.note = "Docking canceled by manual control";
  } else if (const auto valid = port_docking_conditions(false); !valid) {
    candidate.cancel_port_docking();
    candidate.port_docking_.note = "Docking stopped: " + valid.error();
  } else if (!port_docking_.remaining_ticks) {
    candidate.cancel_port_docking();
    candidate.port_docking_.note = "Docking timed out; resume manual control";
  } else {
    const auto a = candidate.assess_port();
    if (!a) return std::unexpected{a.error()};
    if (candidate.port_docking_.phase == NativePortDockingPhase::aligning &&
        a->alignment_radians <= .001 &&
        a->angular_speed_radians_per_second <= .001) {
      candidate.port_docking_.phase = NativePortDockingPhase::closing;
      candidate.port_docking_.note =
          "Closing and braking with thrusters; manual input cancels";
    }
    const auto demand = candidate.port_docking_controls();
    if (!demand) return std::unexpected{demand.error()};
    requested = *demand;
    --candidate.port_docking_.remaining_ticks;
  }
  const auto result = candidate.advance_craft_tick(requested, step);
  if (!result) return std::unexpected{result.error()};
  if (result->propulsion_refused) {
    candidate.cancel_port_docking();
    candidate.port_docking_.note = "Docking stopped: insufficient flight fuel";
  } else if (candidate.port_docking_.active) {
    const auto a = candidate.assess_port();
    if (!a) return std::unexpected{a.error()};
    if (latch_ready(*a)) {
      const auto witness = *a;
      if (auto captured = candidate.capture_port(); !captured)
        return std::unexpected{captured.error()};
      candidate.port_docking_ = {
          false,
          0,
          NativePortDockingPhase::complete,
          "Docked; collar latched within 1 mm after physical alignment",
          0,
          witness};
    } else if (const auto valid = candidate.port_docking_conditions(false);
               !valid) {
      candidate.cancel_port_docking();
      candidate.port_docking_.note = "Docking stopped: " + valid.error();
    } else if (!candidate.port_docking_.remaining_ticks) {
      candidate.cancel_port_docking();
      candidate.port_docking_.note = "Docking timed out; resume manual control";
    }
  }
  *this = std::move(candidate);
  return *result;
}
} // namespace apsis_drift
