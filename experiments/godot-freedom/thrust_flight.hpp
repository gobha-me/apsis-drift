#pragma once
// Opt-in Freedom flight lab, version 1. Authoritative C++, independent of
// Godot. Not a replacement for the saved/replayed legacy planetary flight
// contract.
#include "apsis_drift/coordinates.hpp"
#include "apsis_drift/simulation.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace apsis_drift::flight_lab {
struct V {
  double x{}, y{}, z{};
  friend auto operator==(const V &, const V &) -> bool = default;
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
inline auto length(V a) -> double { return std::sqrt(dot(a, a)); }
inline auto finite(V a) -> bool {
  return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
inline auto unit(V a) -> V {
  const double n = length(a);
  if (!std::isfinite(n) || n < 1e-12)
    throw std::invalid_argument("degenerate flight vector");
  return a * (1 / n);
}
inline auto vec(PlanetFixedDirection d) -> V { return {d.x, d.y, d.z}; }
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
  friend auto operator==(const State &, const State &) -> bool = default;
};
inline constexpr double mass_kg = 8000;
inline constexpr double main_accel = 45;
inline constexpr double retro_accel = 9;
inline constexpr double side_accel = 14;
inline constexpr double up_accel = 26;
inline constexpr double down_accel = 18;
inline constexpr double test_floor =
    16; // explicit safety guard, NOT collision/landing

inline auto to_world(const State &s, V body) -> V {
  return s.right * body.x + s.up * body.y + s.back * body.z;
}
inline auto to_body(const State &s, V world) -> V {
  return {dot(s.right, world), dot(s.up, world), dot(s.back, world)};
}
inline auto validate(Demand d) -> void {
  for (double v : {d.main, d.retro, d.pitch, d.yaw, d.roll, d.strafe, d.heave})
    if (!std::isfinite(v) || std::abs(v) > 1)
      throw std::invalid_argument("invalid flight demand");
  if (d.main < 0 || d.retro < 0)
    throw std::invalid_argument("negative engine demand");
}
inline auto validate(const State &s) -> void {
  for (V v : {s.position, s.velocity, s.right, s.up, s.back, s.angular,
              s.thrust_body})
    if (!finite(v))
      throw std::invalid_argument("nonfinite flight lab state");
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
}
inline auto initial(const PlanetDescriptor &planet, GeodeticPosition origin,
                    double heading) -> State {
  if (!std::isfinite(heading))
    throw std::invalid_argument("invalid heading");
  const auto frame = make_local_tangent_frame(planet, origin);
  if (!frame)
    throw std::invalid_argument("invalid flight origin");
  State s;
  s.position = {frame->origin.x, frame->origin.y, frame->origin.z};
  const V forward = vec(frame->east) * std::cos(heading) +
                    vec(frame->north) * std::sin(heading);
  s.back = forward * (-1);
  s.up = vec(frame->up);
  s.right = unit(cross(forward, s.up));
  validate(s);
  return s;
}
inline auto density(const PlanetDescriptor &planet, double altitude) -> double {
  // Explicit fiction/tuning approximation: pressure-scaled isothermal
  // atmosphere. No weather, wind, planetary rotation or supersonic coefficient
  // model yet.
  return 1.225 * (planet.atmosphere_pressure.value / 1013.25) *
         std::exp(-std::max(0.0, altitude) / 8500.0);
}
inline auto advance(const PlanetDescriptor &planet, double surface,
                    State &state, Demand demand,
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
  s.dynamic_pressure = .5 * s.density * dot(s.velocity, s.velocity);
  // Bounded attitude controller: no Euler-angle/gimbal restriction. High q
  // reduces requested rate continuously; this is tuning, not measured
  // aerodynamics.
  const double authority =
      std::clamp(1 / (1 + s.dynamic_pressure / 40000), .35, 1.0);
  const V target{demand.pitch * 1.15 * authority, -demand.yaw * .90 * authority,
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
  s.main = toward(s.main, demand.main, 3.0 * dt);
  s.retro = toward(s.retro, demand.retro, 8.0 * dt);
  const V gravity =
      unit(s.position) * (-9.80665 * planet.surface_gravity.value / 1000.0 *
                          radius * radius / (distance * distance));
  const V air_velocity = to_body(s, s.velocity);
  V thrust{demand.strafe * side_accel,
           demand.heave * (demand.heave >= 0 ? up_accel : down_accel),
           s.retro * retro_accel - s.main * main_accel};
  if (demand.assist) {
    // Support gravity with actual available thrusters. Upright hover is not
    // free antigravity; inversion/high gravity can saturate the available jets.
    thrust = thrust - to_body(s, gravity);
    if (std::abs(demand.strafe) < .001)
      thrust.x -= air_velocity.x * .65;
    if (std::abs(demand.heave) < .001)
      thrust.y -= air_velocity.y * .65;
    // Deliberately no forward speed hold: releasing main thrust permits
    // coasting.
  }
  s.thrust_body = {std::clamp(thrust.x, -side_accel, side_accel),
                   std::clamp(thrust.y, -down_accel, up_accel),
                   std::clamp(thrust.z, -main_accel, retro_accel)};
  // D = 1/2 rho v^2 Cd A, resolved against body axes with authored Cd*A values.
  const V drag{-.5 * s.density * 54 * air_velocity.x *
                   std::abs(air_velocity.x) / mass_kg,
               -.5 * s.density * 80 * air_velocity.y *
                   std::abs(air_velocity.y) / mass_kg,
               -.5 * s.density * 3.84 * air_velocity.z *
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
    if (inward < 0)
      s.velocity = s.velocity - normal * inward;
    s.clearance = test_floor;
  }
  s.assist = demand.assist;
  ++s.tick;
  validate(s);
  state = s;
}
inline auto checksum(const State &s) -> std::uint64_t {
  std::uint64_t hash = 1469598103934665603ULL;
  auto add = [&](std::uint64_t bits) {
    for (int i = 0; i < 8; ++i) {
      hash ^= bits & 255;
      hash *= 1099511628211ULL;
      bits >>= 8;
    }
  };
  add(1);
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
  return hash;
}
} // namespace apsis_drift::flight_lab
