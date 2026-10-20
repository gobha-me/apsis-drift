#pragma once

#include "apsis_drift/godot/saved_flight.hpp"
#include "native_minor_route_fixture.hpp"

#include <algorithm>
#include <array>

// A fixed-seed actual command pilot reused by the C++ route test and opt-in
// voyage exporter. It navigates to the moving origin port; it never assigns
// a body, clock, resource pool, docking state or travel proof.
namespace apsis_drift::origin_route_reuse_fixture {
using minor_route_fixture::continued;
using minor_route_fixture::ensure;
using minor_route_fixture::need;
using V = RigidVector3;
inline constexpr unsigned kRendezvousMaximumTicks = 120U * 2400U;
inline constexpr unsigned kCaptureMaximumTicks = 7200;
inline auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline auto scale(V a, double b) -> V {
  return {a.x * b, a.y * b, a.z * b};
}
inline auto dot(V a, V b) -> double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline auto length(V a) -> double {
  return std::sqrt(dot(a, a));
}
inline auto unit(V a) -> V {
  const auto n = length(a);
  if (!std::isfinite(n) || n <= 0)
    throw std::runtime_error("Invalid pilot direction");
  return scale(a, 1 / n);
}
inline auto rotate(RigidOrientation q, V v) -> V {
  const auto r = godot_spike::rotate_saved(q, {v.x, v.y, v.z});
  return {r.x, r.y, r.z};
}
inline auto controls(const RigidBodyState& body,
                     const CraftFrameProperties& frame,
                     const AtmosphericFlightSample& air, V acceleration,
                     const std::array<V, 3>& desired_axes,
                     bool assistance = false) -> NativeFlightControls {
  const auto inverse = godot_spike::inverse_saved(body.orientation);
  V error{};
  constexpr std::array<V, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  for (std::size_t i = 0; i < axes.size(); ++i)
    error =
        add(error, cross(rotate(body.orientation, axes[i]), desired_axes[i]));
  const auto angular = sub(rotate(inverse, scale(error, 1.0)),
                           scale(body.angular_velocity_radians_per_second, 2));
  const auto body_accel =
      sub(rotate(inverse, acceleration),
          scale(add(air.drag_force_body_newtons, air.lift_force_body_newtons),
                1.0 / frame.dry_mass_kg));
  NativeFlightControls result;
  const std::array translation{body_accel.x, body_accel.y, body_accel.z};
  const std::array rotation{angular.x, angular.y, angular.z};
  const auto rate = rotate(inverse, error);
  const std::array desired_rate{rate.x, rate.y, rate.z};
  std::array<double*, 3> positive{&result.positive_translation.x,
                                  &result.positive_translation.y,
                                  &result.positive_translation.z};
  std::array<double*, 3> negative{&result.negative_translation.x,
                                  &result.negative_translation.y,
                                  &result.negative_translation.z};
  std::array<double*, 3> positive_rotation{&result.positive_rotation.x,
                                           &result.positive_rotation.y,
                                           &result.positive_rotation.z};
  std::array<double*, 3> negative_rotation{&result.negative_rotation.x,
                                           &result.negative_rotation.y,
                                           &result.negative_rotation.z};
  for (std::size_t i = 0; i < 3; ++i) {
    *positive[i] = std::clamp(translation[i] * frame.dry_mass_kg /
                                  frame.positive_force_newtons[i],
                              0.0, 1.0);
    *negative[i] = std::clamp(-translation[i] * frame.dry_mass_kg /
                                  frame.negative_force_newtons[i],
                              0.0, 1.0);
    // Assisted fractions request angular rates; Advanced fractions request
    // torque. Reuse the same attitude error and authoritative frame ratings.
    const auto demand =
        assistance
            ? desired_rate[i] /
                  (frame.max_angular_rate_milliradians_per_second[i] * .001)
            : rotation[i] *
                  static_cast<double>(frame.principal_inertia_kg_m2[i]) /
                  frame.torque_newton_metres[i];
    *positive_rotation[i] = std::clamp(demand, 0.0, 1.0);
    *negative_rotation[i] = std::clamp(-demand, 0.0, 1.0);
  }
  return result;
}

// Leave the actual captured craft attached. Service is deliberately separate:
// exporters can retain the genuinely reached pre-service phase without gifts.
inline auto capture_home(NativeFreedomFlightSession& session) -> unsigned {
  ensure(!session.moon() && !session.minor(),
         "Home capture needs the actual home body");
  need(session.select_port(1));
  const auto station =
      generate_origin_station(session.document().origin.recipe.universe_seed);
  const auto geometry = need(origin_station_geometry(station));
  const auto frame =
      need(resolve_craft_frame(session.document().flight.craft)).properties;
  NativeFlightControls held{};
  for (unsigned tick = 0; tick < kRendezvousMaximumTicks; ++tick) {
    const auto body = need(session.relative_state());
    const auto observed = need(session.observe());
    const auto gravity = need(evaluate_central_body_gravity(
        RigidBodyWorldContext{session.system()}, body));
    ensure(observed.atmosphere.altitude_metres >
               observed.atmosphere.space_boundary_altitude_metres,
           "Origin rendezvous entered the atmosphere");
    const auto dock =
        need(resolve_origin_port_pose(session.system(), station, geometry,
                                      {station.id, 1}, body.tick))
            .planet_relative;
    const auto goal = add(dock.position_metres, {0, -50, 0});
    const auto p = body.position_metres;
    const auto v = body.linear_velocity_metres_per_second;
    V acceleration{};
    std::array<V, 3> axes{};
    if (length(sub(goal, p)) > 10000) {
      const auto r = length(p);
      const auto radial = unit(p);
      const auto orbital_plane = unit(
          cross(dock.position_metres, dock.linear_velocity_metres_per_second));
      const auto forward = unit(cross(orbital_plane, radial));
      const auto target_radial = unit(goal);
      const auto angle =
          std::atan2(dot(orbital_plane, cross(radial, target_radial)),
                     dot(radial, target_radial));
      const auto omega = dot(dock.linear_velocity_metres_per_second,
                             unit(cross(orbital_plane, target_radial))) /
                         length(goal);
      const auto goal_velocity = add(
          scale(forward, r * (omega + std::clamp(angle * .008, -.0005, .0005))),
          scale(orbital_plane,
                dot(dock.linear_velocity_metres_per_second, orbital_plane)));
      const auto radial_speed = dot(v, radial);
      const auto tangent = sub(v, scale(radial, radial_speed));
      const auto radial_goal =
          std::clamp((length(goal) - r) * .03, -1200., 1200.);
      acceleration = sub(
          add(add(scale(radial, (radial_goal - radial_speed) * .1 -
                                    dot(tangent, tangent) / r),
                  scale(sub(goal_velocity, tangent), .05)),
              scale(orbital_plane, dot(sub(goal, p), orbital_plane) * .0002)),
          gravity.acceleration_metres_per_second_squared);
      const auto back = scale(unit(acceleration), -1);
      const auto up = unit(cross(back, orbital_plane));
      axes = {unit(cross(up, back)), up, back};
    } else {
      const auto next =
          need(resolve_origin_port_pose(session.system(), station, geometry,
                                        {station.id, 1}, body.tick + 1))
              .planet_relative;
      const auto station_acceleration =
          scale(sub(next.linear_velocity_metres_per_second,
                    dock.linear_velocity_metres_per_second),
                120);
      acceleration = sub(
          add(add(scale(sub(goal, p), .0008),
                  scale(sub(dock.linear_velocity_metres_per_second, v), .06)),
              station_acceleration),
          gravity.acceleration_metres_per_second_squared);
      axes = {rotate(dock.orientation, {1, 0, 0}),
              rotate(dock.orientation, {0, 1, 0}),
              rotate(dock.orientation, {0, 0, 1})};
    }
    if (session.port_approach_available()) {
      need(session.begin_port_approach());
      for (unsigned approach = 1; approach <= kCaptureMaximumTicks;
           ++approach) {
        const auto step = need(session.advance({}));
        ensure(!step.propulsion_refused && !session.recovery_pending(),
               "Moving-port approach lost propulsion or craft");
        if (need(session.assess_port()).decision ==
            OriginDockDecision::capture_ready) {
          need(session.capture_port());
          ensure(session.docking() && session.docking()->attached,
                 "Actual port capture did not attach");
          return tick + approach;
        }
      }
      throw std::runtime_error(
          "Actual origin port approach exceeded fixed 7200 ticks");
    }
    if (tick % kMaxCatchUpSteps == 0)
      held = controls(body, frame, observed.atmosphere, acceleration, axes,
                      session.document().model.assistance);
    const auto step = need(session.advance(held));
    ensure(!step.propulsion_refused && !session.recovery_pending(),
           "Origin rendezvous lost propulsion or craft");
  }
  throw std::runtime_error(
      "Actual origin rendezvous exceeded fixed 288000 ticks");
}
inline auto service_and_depart(NativeFreedomFlightSession& session) -> void {
  ensure(session.docking() && session.docking()->attached,
         "Free service requires the genuinely captured origin port");
  const auto before = session.document().flight.tick;
  need(session.replenish_resources());
  ensure(session.document().flight.tick == before &&
             session.resources()->flight_quanta ==
                 kFreedomFlightCapacityQuanta &&
             session.resources()->jump_charges == 3,
         "Actual attached service did not replenish both pools at zero time");
  session = continued(session);
  need(session.release_port());
  NativeFlightControls demand{};
  demand.negative_translation.y = .25;
  for (unsigned i = 0; i < 720; ++i)
    need(session.advance(demand));
  session = continued(session);
}
} // namespace apsis_drift::origin_route_reuse_fixture
