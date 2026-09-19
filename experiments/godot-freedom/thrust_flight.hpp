#pragma once
// Opt-in Freedom flight lab, version 1. Authoritative C++, independent of
// Godot. Not a replacement for the saved/replayed legacy planetary flight
// contract.
#include "apsis_drift/coordinates.hpp"
#include "apsis_drift/simulation.hpp"
#include "apsis_drift/vacuum_dynamics.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace apsis_drift::flight_lab {
struct V {
  double x{}, y{}, z{};
  friend auto operator==(const V&, const V&) -> bool = default;
};
inline auto operator+(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline auto operator-(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline auto operator*(V a, double b) -> V {
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
inline auto finite(V a) -> bool {
  return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
inline auto unit(V a) -> V {
  const double n = length(a);
  if (!std::isfinite(n) || n < 1e-12)
    throw std::invalid_argument("degenerate flight vector");
  return a * (1 / n);
}
inline auto vec(PlanetFixedDirection d) -> V {
  return {d.x, d.y, d.z};
}
inline auto rotate(V v, V axis, double angle) -> V {
  return v * std::cos(angle) + cross(axis, v) * std::sin(angle) +
         axis * (dot(axis, v) * (1 - std::cos(angle)));
}
inline auto toward(double a, double b, double amount) -> double {
  return a + std::clamp(b - a, -amount, amount);
}

struct Demand {
  // Independent main and retro actuators, then the other five motion axes.
  double main{}, retro{}, pitch{}, yaw{}, roll{}, strafe{}, heave{};
  bool assist{true};
};
struct State {
  SimulationTick tick{};
  V position, velocity; // planet-centered, nonrotating test frame
  V right, up, back;    // orthonormal body axes in the same frame; nose = -back
  V angular;            // body-axis angular velocity, radians/second
  double main{}, retro{};
  double density{}, dynamic_pressure{}, clearance{}, acceleration{};
  V thrust_body; // actual limited engine/RCS acceleration, including assist
  bool floor_guard{}, assist{true};
  // Version one keeps its historical basis-only attitude and checksum.
  // Version two owns q in the same nonrotating frame; basis is derived only.
  unsigned angular_model{1};
  std::optional<RigidOrientation> orientation;
  V torque_body;
  bool torque_saturated{};
  // Independent experimental translation policy. One preserves historical
  // assist/checksum behavior; two fades atmospheric support to true vacuum
  // coasting. This does not select a rotational model or alter saved careers.
  unsigned translation_policy{1};
  friend auto operator==(const State&, const State&) -> bool = default;
};
inline constexpr double mass_kg = 8000;
inline constexpr double main_accel = 45;
inline constexpr double retro_accel = 9;
inline constexpr double side_accel = 14;
inline constexpr double up_accel = 26;
inline constexpr double down_accel = 18;
inline constexpr double test_floor =
    16; // explicit safety guard, NOT collision/landing

inline auto to_world(const State& s, V body) -> V {
  return s.right * body.x + s.up * body.y + s.back * body.z;
}
inline auto to_body(const State& s, V world) -> V {
  return {dot(s.right, world), dot(s.up, world), dot(s.back, world)};
}
inline auto orientation_axes(RigidOrientation q) -> std::array<V, 3> {
  const double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
  const double xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
  const double wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
  return {V{1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy)},
          V{2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx)},
          V{2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy)}};
}
inline auto set_orientation(State& s, RigidOrientation orientation) -> void {
  s.orientation = orientation;
  const auto axes = orientation_axes(orientation);
  s.right = axes[0];
  s.up = axes[1];
  s.back = axes[2];
}
inline auto validate(Demand d) -> void {
  for (double v : {d.main, d.retro, d.pitch, d.yaw, d.roll, d.strafe, d.heave})
    if (!std::isfinite(v) || std::abs(v) > 1)
      throw std::invalid_argument("invalid flight demand");
  if (d.main < 0 || d.retro < 0)
    throw std::invalid_argument("negative engine demand");
}
inline auto validate(const State& s) -> void {
  if (s.translation_policy != 1 && s.translation_policy != 2)
    throw std::invalid_argument("unknown flight lab translation policy");
  if (s.angular_model != 1 && s.angular_model != 2)
    throw std::invalid_argument("unknown flight lab angular model");
  if ((s.angular_model == 2) != s.orientation.has_value())
    throw std::invalid_argument("flight lab angular model/state mismatch");
  for (V v : {s.position, s.velocity, s.right, s.up, s.back, s.angular,
              s.thrust_body})
    if (!finite(v)) throw std::invalid_argument("nonfinite flight lab state");
  if (length(s.position) < 1000 || length(s.position) > 1e10 ||
      length(s.velocity) > 100000 || length(s.angular) > 10)
    throw std::invalid_argument("flight lab numerical envelope exceeded");
  for (V v : {s.right, s.up, s.back})
    if (std::abs(length(v) - 1) > 1e-8)
      throw std::invalid_argument("nonunit body axis");
  if (std::abs(dot(s.right, s.up)) > 1e-8 ||
      length(cross(s.right, s.up) - s.back) > 1e-8)
    throw std::invalid_argument("invalid body frame");
  for (double v : {s.main, s.retro, s.density, s.dynamic_pressure, s.clearance,
                   s.acceleration})
    if (!std::isfinite(v))
      throw std::invalid_argument("nonfinite flight lab scalar");
  if (s.main < 0 || s.main > 1 || s.retro < 0 || s.retro > 1 || s.density < 0 ||
      s.dynamic_pressure < 0 || s.acceleration < 0)
    throw std::invalid_argument("invalid flight lab scalar");
  if (s.angular_model == 2) {
    const auto& q = *s.orientation;
    for (double component : {s.angular.x, s.angular.y, s.angular.z})
      if (component == 0 && std::signbit(component))
        throw std::invalid_argument("noncanonical lab angular velocity");
    bool first = true;
    for (double component : {q.w, q.x, q.y, q.z}) {
      if (!std::isfinite(component) || std::abs(component) > 2 ||
          (component == 0 && std::signbit(component)))
        throw std::invalid_argument("invalid canonical lab orientation");
      if (component != 0 && first) {
        if (component < 0)
          throw std::invalid_argument("noncanonical lab orientation sign");
        first = false;
      }
    }
    const double norm = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
    if (std::abs(norm - 1) > kRigidBodyOrientationSquaredNormTolerance ||
        !finite(s.torque_body))
      throw std::invalid_argument("invalid lab orientation or torque");
    const auto axes = orientation_axes(q);
    if (length(s.right - axes[0]) > 1e-10 || length(s.up - axes[1]) > 1e-10 ||
        length(s.back - axes[2]) > 1e-10)
      throw std::invalid_argument("lab basis diverges from authoritative q");
  }
}
inline auto enable_orbit_preserving_translation(State& state) -> void {
  validate(state);
  if (state.tick != 0 || state.translation_policy != 1)
    throw std::invalid_argument(
        "translation policy requires a fresh lab state");
  state.translation_policy = 2;
}
inline auto enable_rigid_attitude(State& state) -> void {
  validate(state);
  if (state.tick != 0 || state.angular_model != 1)
    throw std::invalid_argument("rigid attitude requires a fresh lab1 state");
  // One conversion at initialization, never a renderer or per-tick roundtrip.
  const V r = state.right, u = state.up, b = state.back;
  const double trace = r.x + u.y + b.z;
  RigidOrientation q;
  if (trace > 0) {
    const double scale = 2 * std::sqrt(trace + 1);
    q = {scale / 4, (u.z - b.y) / scale, (b.x - r.z) / scale,
         (r.y - u.x) / scale};
  } else if (r.x > u.y && r.x > b.z) {
    const double scale = 2 * std::sqrt(1 + r.x - u.y - b.z);
    q = {(u.z - b.y) / scale, scale / 4, (u.x + r.y) / scale,
         (b.x + r.z) / scale};
  } else if (u.y > b.z) {
    const double scale = 2 * std::sqrt(1 + u.y - r.x - b.z);
    q = {(b.x - r.z) / scale, (u.x + r.y) / scale, scale / 4,
         (b.y + u.z) / scale};
  } else {
    const double scale = 2 * std::sqrt(1 + b.z - r.x - u.y);
    q = {(r.y - u.x) / scale, (b.x + r.z) / scale, (b.y + u.z) / scale,
         scale / 4};
  }
  const auto normalized = normalize_rigid_orientation(q);
  if (!normalized) throw std::invalid_argument("invalid initial lab attitude");
  State candidate = state;
  candidate.angular_model = 2;
  set_orientation(candidate, *normalized);
  validate(candidate);
  state = candidate;
}
inline auto initial(const PlanetDescriptor& planet, GeodeticPosition origin,
                    double heading, unsigned angular_model = 1) -> State {
  if (angular_model != 1 && angular_model != 2)
    throw std::invalid_argument("unknown initial angular model");
  if (!std::isfinite(heading)) throw std::invalid_argument("invalid heading");
  const auto frame = make_local_tangent_frame(planet, origin);
  if (!frame) throw std::invalid_argument("invalid flight origin");
  State s;
  s.position = {frame->origin.x, frame->origin.y, frame->origin.z};
  const V forward = vec(frame->east) * std::cos(heading) +
                    vec(frame->north) * std::sin(heading);
  s.back = forward * (-1);
  s.up = vec(frame->up);
  s.right = unit(cross(forward, s.up));
  validate(s);
  if (angular_model == 2) enable_rigid_attitude(s);
  return s;
}
inline auto density(const PlanetDescriptor& planet, double altitude) -> double {
  // Explicit fiction/tuning approximation: pressure-scaled isothermal
  // atmosphere. No weather, wind, planetary rotation or supersonic coefficient
  // model yet.
  return 1.225 * (planet.atmosphere_pressure.value / 1013.25) *
         std::exp(-std::max(0.0, altitude) / 8500.0);
}
// Available translational-assist envelope, independent of the runtime assist
// toggle. Actual automatic support is this weight only while assist is ON.
// Policy two has no automatic airless hover: that needs a later explicit mode,
// not an altitude switch that would erase low-altitude ballistic trajectories.
inline auto assist_translation_weight(const PlanetDescriptor& planet,
                                      const State& state) -> double {
  validate(state);
  if (planet.radius.value < PlanetRadiusKm::min ||
      planet.radius.value > PlanetRadiusKm::max ||
      planet.surface_gravity.value < SurfaceGravityMilliG::min ||
      planet.surface_gravity.value > SurfaceGravityMilliG::max ||
      planet.atmosphere_pressure.value > AtmospherePressureMillibars::max)
    throw std::invalid_argument("invalid translation assist planet");
  if (state.translation_policy == 1) return 1.0;
  const double sea_density = density(planet, 0);
  constexpr double space_density = 1e-6;
  if (sea_density <= space_density) return 0.0;
  const double altitude = length(state.position) - planet.radius.value * 1000.0;
  const double atmosphere_edge = std::max(
      0.0, 8500 * std::log(std::max(1.0, sea_density / space_density)));
  if (altitude >= atmosphere_edge) return 0.0;
  // Explicit C1 density ramp: full support at 0.001 kg/m^3 (or sea-level
  // density for a thinner atmosphere), exactly zero at the existing air edge.
  const double full_density = std::min(sea_density, 0.001);
  const double fraction =
      std::clamp((density(planet, altitude) - space_density) /
                     (full_density - space_density),
                 0.0, 1.0);
  return fraction * fraction * (3.0 - 2.0 * fraction);
}
// The same explicit experimental air/space fade applies to aerodynamic forces
// regardless of the assist toggle. Raw State::density remains environmental
// telemetry; policy-two dynamic pressure/drag use this effective density.
inline auto effective_aerodynamic_density(const PlanetDescriptor& planet,
                                          const State& state) -> double {
  const double weight = assist_translation_weight(planet, state);
  const double raw =
      density(planet, length(state.position) - planet.radius.value * 1000.0);
  return state.translation_policy == 1 ? raw : raw * weight;
}
inline auto advance(const PlanetDescriptor& planet, double surface,
                    State& state, Demand demand,
                    SimulationSeconds step = kSimulationStep) -> void {
  validate(state);
  validate(demand);
  if (!std::isfinite(surface) || std::abs(surface) > 1e7 ||
      step != kSimulationStep || !std::isfinite(step.count()) ||
      state.tick == std::numeric_limits<SimulationTick>::max())
    throw std::invalid_argument("invalid flight lab environment, tick or step");
  if (planet.radius.value < PlanetRadiusKm::min ||
      planet.radius.value > PlanetRadiusKm::max ||
      planet.surface_gravity.value < SurfaceGravityMilliG::min ||
      planet.surface_gravity.value > SurfaceGravityMilliG::max ||
      planet.atmosphere_pressure.value > AtmospherePressureMillibars::max)
    throw std::invalid_argument("invalid flight lab planet");
  State s = state;
  const double dt = step.count(), radius = planet.radius.value * 1000.0;
  const double distance = length(s.position), altitude = distance - radius;
  s.density = density(planet, altitude);
  const double translation_weight =
      s.translation_policy == 1 ? 1.0 : assist_translation_weight(planet, s);
  const double aerodynamic_density =
      s.translation_policy == 1 ? s.density : s.density * translation_weight;
  s.dynamic_pressure = .5 * aerodynamic_density * dot(s.velocity, s.velocity);
  // Bounded attitude controller: no Euler-angle/gimbal restriction. High q
  // reduces requested rate continuously; this is tuning, not measured
  // aerodynamics.
  const double authority =
      std::clamp(1 / (1 + s.dynamic_pressure / 40000), .35, 1.0);
  if (s.angular_model == 1) {
    const V target{demand.pitch * 1.15 * authority,
                   -demand.yaw * .90 * authority,
                   -demand.roll * 1.70 * authority};
    s.angular = {toward(s.angular.x, target.x, 2.8 * dt),
                 toward(s.angular.y, target.y, 2.8 * dt),
                 toward(s.angular.z, target.z, 4.0 * dt)};
    const V omega = to_world(s, s.angular);
    const double angular_speed = length(omega);
    if (angular_speed > 1e-12) {
      const V axis = omega * (1 / angular_speed);
      s.right = unit(rotate(s.right, axis, angular_speed * dt));
      s.up = rotate(s.up, axis, angular_speed * dt);
      s.up = unit(s.up - s.right * dot(s.right, s.up));
      s.back = cross(s.right, s.up);
    }
  } else {
    // Existing pressure-dependent command tuning, not aerodynamic torque.
    // The shared provider owns rotational inertia and bounded stabilization.
    const V command{demand.pitch * authority, -demand.yaw * authority,
                    -demand.roll * authority};
    RigidVector3 positive{std::max(command.x, 0.0), std::max(command.y, 0.0),
                          std::max(command.z, 0.0)};
    RigidVector3 negative{std::max(-command.x, 0.0), std::max(-command.y, 0.0),
                          std::max(-command.z, 0.0)};
    bool command_saturated = false;
    if (!demand.assist) {
      // The lab sticks remain rate commands while deflected. Assistance OFF
      // changes only released axes: exactly zero commanded torque there.
      // Active-axis gyro compensation is bounded, real actuator activity;
      // this does not change the vacuum provider's raw actuator semantics.
      const auto& frame = starter_shuttle_frame().properties;
      const V inertia{static_cast<double>(frame.principal_inertia_kg_m2[0]),
                      static_cast<double>(frame.principal_inertia_kg_m2[1]),
                      static_cast<double>(frame.principal_inertia_kg_m2[2])};
      const V momentum{inertia.x * s.angular.x, inertia.y * s.angular.y,
                       inertia.z * s.angular.z};
      const V gyro = cross(s.angular, momentum);
      const std::array commands{command.x, command.y, command.z};
      const std::array omega{s.angular.x, s.angular.y, s.angular.z};
      const std::array gyroscopic{gyro.x, gyro.y, gyro.z};
      const std::array plus{&positive.x, &positive.y, &positive.z};
      const std::array minus{&negative.x, &negative.y, &negative.z};
      for (std::size_t axis = 0; axis < commands.size(); ++axis) {
        double torque = 0;
        if (commands[axis] != 0) {
          const double target =
              commands[axis] *
              frame.max_angular_rate_milliradians_per_second[axis] * .001;
          const double requested =
              static_cast<double>(frame.principal_inertia_kg_m2[axis]) *
                  (target - omega[axis]) / dt +
              gyroscopic[axis];
          const double limit = frame.torque_newton_metres[axis];
          torque = std::clamp(requested, -limit, limit);
          command_saturated = command_saturated || requested != torque;
        }
        const double fraction = torque / frame.torque_newton_metres[axis];
        *plus[axis] = std::max(fraction, 0.0);
        *minus[axis] = std::max(-fraction, 0.0);
      }
    }
    const auto attitude = advance_vacuum_attitude(
        {kStarterShuttleFrameId, kStarterShuttleFrameVersion}, *s.orientation,
        {s.angular.x, s.angular.y, s.angular.z}, positive, negative,
        demand.assist, step);
    if (!attitude) throw std::invalid_argument("lab2 attitude step rejected");
    set_orientation(s, attitude->orientation);
    const auto angular = attitude->angular_velocity_radians_per_second;
    s.angular = {angular.x, angular.y, angular.z};
    const auto torque = attitude->actuation.applied_torque_newton_metres;
    s.torque_body = {torque.x, torque.y, torque.z};
    s.torque_saturated =
        command_saturated || attitude->actuation.torque_saturated;
  }
  s.main = toward(s.main, demand.main, 3.0 * dt);
  s.retro = toward(s.retro, demand.retro, 8.0 * dt);
  const V gravity =
      unit(s.position) * (-9.80665 * planet.surface_gravity.value / 1000.0 *
                          radius * radius / (distance * distance));
  const V air_velocity = to_body(s, s.velocity);
  V thrust{demand.strafe * side_accel,
           demand.heave * (demand.heave >= 0 ? up_accel : down_accel),
           s.retro * retro_accel - s.main * main_accel};
  if (demand.assist && s.translation_policy == 1) {
    // Support gravity with actual available thrusters. Upright hover is not
    // free antigravity; inversion/high gravity can saturate the available jets.
    thrust = thrust - to_body(s, gravity);
    if (std::abs(demand.strafe) < .001) thrust.x -= air_velocity.x * .65;
    if (std::abs(demand.heave) < .001) thrust.y -= air_velocity.y * .65;
    // Deliberately no forward speed hold: releasing main thrust permits
    // coasting.
  } else if (demand.assist) {
    const double weight = translation_weight;
    if (weight > 0) {
      thrust = thrust - to_body(s, gravity) * weight;
      if (std::abs(demand.strafe) < .001)
        thrust.x -= air_velocity.x * .65 * weight;
      if (std::abs(demand.heave) < .001)
        thrust.y -= air_velocity.y * .65 * weight;
    }
  }
  s.thrust_body = {std::clamp(thrust.x, -side_accel, side_accel),
                   std::clamp(thrust.y, -down_accel, up_accel),
                   std::clamp(thrust.z, -main_accel, retro_accel)};
  // D = 1/2 rho v^2 Cd A, resolved against body axes with authored Cd*A values.
  const V drag{-.5 * aerodynamic_density * 54 * air_velocity.x *
                   std::abs(air_velocity.x) / mass_kg,
               -.5 * aerodynamic_density * 80 * air_velocity.y *
                   std::abs(air_velocity.y) / mass_kg,
               -.5 * aerodynamic_density * 3.84 * air_velocity.z *
                   std::abs(air_velocity.z) / mass_kg};
  const V acceleration = gravity + to_world(s, s.thrust_body + drag);
  s.acceleration = length(acceleration);
  s.velocity = s.velocity + acceleration * dt;
  s.position = s.position + s.velocity * dt;
  s.clearance = length(s.position) - radius - surface;
  s.floor_guard = s.clearance < test_floor;
  if (s.floor_guard) {
    const V normal = unit(s.position);
    s.position = normal * (radius + surface + test_floor);
    const double inward = dot(s.velocity, normal);
    if (inward < 0) s.velocity = s.velocity - normal * inward;
    s.clearance = test_floor;
  }
  s.assist = demand.assist;
  ++s.tick;
  validate(s);
  state = s;
}
inline auto checksum(const State& s) -> std::uint64_t {
  std::uint64_t hash = 1469598103934665603ULL;
  auto add = [&](std::uint64_t bits) {
    for (int i = 0; i < 8; ++i) {
      hash ^= bits & 255;
      hash *= 1099511628211ULL;
      bits >>= 8;
    }
  };
  add(s.angular_model);
  add(s.tick);
  add(s.assist);
  add(s.floor_guard);
  for (V v : {s.position, s.velocity, s.right, s.up, s.back, s.angular,
              s.thrust_body})
    for (double x : {v.x, v.y, v.z})
      add(std::bit_cast<std::uint64_t>(x));
  for (double x : {s.main, s.retro, s.density, s.dynamic_pressure, s.clearance,
                   s.acceleration})
    add(std::bit_cast<std::uint64_t>(x));
  if (s.angular_model == 2 && s.orientation) {
    for (double x :
         {s.orientation->w, s.orientation->x, s.orientation->y,
          s.orientation->z, s.torque_body.x, s.torque_body.y, s.torque_body.z})
      add(std::bit_cast<std::uint64_t>(x));
    add(s.torque_saturated);
  }
  if (s.translation_policy == 2) {
    add(0x5452414e534c4154ULL); // TRANSLAT: new policy, no legacy byte changes.
    add(s.translation_policy);
  }
  return hash;
}
} // namespace apsis_drift::flight_lab
