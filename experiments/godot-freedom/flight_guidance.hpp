#pragma once

#include "flight_navigation.hpp"

#include <vector>

namespace apsis_drift::flight_lab {

enum class GuidanceMode : unsigned { none, orbit, return_to_surface, escape };
enum class GuidanceTermination : unsigned {
  disabled,
  horizon,
  atmosphere,
  spherical_surface,
  numerical_limit
};
enum class GuidanceCue : unsigned {
  none,
  below_reference_surface,
  climb_above_atmosphere,
  atmosphere_model_limit,
  build_horizontal_speed,
  circular_reference,
  orbit_established,
  return_reference,
  escape_reference,
  escape_energy_reached
};
struct GuidanceRequest {
  GuidanceMode mode{GuidanceMode::none};
  double horizon_seconds{900.0};
  unsigned sample_count{129};
};
struct GuidanceSample {
  double seconds{};
  V position;
};
struct Guidance {
  GuidanceMode mode{GuidanceMode::none};
  GuidanceTermination termination{GuidanceTermination::disabled};
  GuidanceCue cue{GuidanceCue::none};
  OrbitInfo orbit;
  std::vector<GuidanceSample> coast_samples;
  V target_position, target_velocity, delta_velocity;
  double reference_radius_metres{}, reference_periapsis_radius_metres{};
  bool target_available{}, delta_velocity_available{}, radial_degenerate{};
  // This is a reference, never a scheduled burn or an autopilot command.
  bool central_gravity_vacuum_only{true};
};

// Read-only experiment guidance. Throws invalid_argument for malformed state,
// descriptor/request. Bounded 2..257 samples and 1<=horizon<=1800 seconds.
// Forecast ignores thrust, assist, drag, terrain relief, other bodies and spin.
// It stops at the laboratory atmosphere threshold or spherical reference
// surface; neither endpoint means safe landing or a terrain collision proof.
// Targets describe ideal instantaneous velocity at target_position. A delta
// exists only if that position is the current position. Return means a
// zero-radial-speed ellipse from here toward the air edge/reference sphere,
// not a trajectory solution from arbitrary current motion.
inline auto flight_guidance(const PlanetDescriptor& planet, const State& state,
                            GuidanceRequest request = {}) -> Guidance;

namespace guidance_detail {
struct CoastState {
  V position, velocity;
};
inline auto acceleration(V position, double mu) -> V {
  const double radius = length(position);
  if (!std::isfinite(radius) || radius < 1000)
    throw std::invalid_argument("invalid guidance coast radius");
  return position * (-mu / (radius * radius * radius));
}
inline auto coast_step(CoastState initial, double mu, double seconds)
    -> CoastState {
  const V a = acceleration(initial.position, mu);
  const V vb = initial.velocity + a * (seconds * .5);
  const V b =
      acceleration(initial.position + initial.velocity * (seconds * .5), mu);
  const V vc = initial.velocity + b * (seconds * .5);
  const V c = acceleration(initial.position + vb * (seconds * .5), mu);
  const V vd = initial.velocity + c * seconds;
  const V d = acceleration(initial.position + vc * seconds, mu);
  return {initial.position +
              (initial.velocity + vb * 2 + vc * 2 + vd) * (seconds / 6),
          initial.velocity + (a + b * 2 + c * 2 + d) * (seconds / 6)};
}
// Conservative segment/sphere clipping of the bounded numerical path. A
// grazing chord can truncate slightly early; this is not swept terrain CCD.
inline auto boundary_fraction(V start, V end, double radius)
    -> std::optional<double> {
  const V change = end - start;
  const double a = dot(change, change), b = dot(start, change);
  if (a == 0 || b >= 0) return {};
  const double closest_fraction = std::clamp(-b / a, 0.0, 1.0);
  const V closest = start + change * closest_fraction;
  if (dot(closest, closest) > radius * radius) return {};
  // Bisection avoids subtracting nearly equal planet-scale quadratic roots.
  double outside = 0, inside = closest_fraction;
  for (unsigned iteration = 0; iteration < 48; ++iteration) {
    const double middle = (outside + inside) * .5;
    const V point = start + change * middle;
    if (dot(point, point) > radius * radius)
      outside = middle;
    else
      inside = middle;
  }
  return inside;
}
} // namespace guidance_detail

inline auto flight_guidance(const PlanetDescriptor& planet, const State& state,
                            GuidanceRequest request) -> Guidance {
  validate(state);
  if (planet != generate_planet_descriptor(planet.seed) ||
      static_cast<unsigned>(request.mode) >
          static_cast<unsigned>(GuidanceMode::escape) ||
      !std::isfinite(request.horizon_seconds) || request.horizon_seconds < 1 ||
      request.horizon_seconds > 1800 || request.sample_count < 2 ||
      request.sample_count > 257)
    throw std::invalid_argument("invalid guidance descriptor or request");
  Guidance result;
  result.mode = request.mode;
  result.orbit = orbit_info(planet, state);
  if (request.mode == GuidanceMode::none) return result;
  const double surface = planet.radius.value * 1000.0;
  const double mu =
      9.80665 * planet.surface_gravity.value / 1000.0 * surface * surface;
  const double radius = length(state.position);
  const double boundary = surface + result.orbit.atmosphere_edge;
  const V radial = unit(state.position);
  V tangent = state.velocity - radial * dot(state.velocity, radial);
  result.radial_degenerate = length(tangent) < 1e-6;
  if (result.radial_degenerate) {
    // No invented orbital plane: this is explicitly marked as a reference
    // chosen from the ship's projected nose, or a deterministic axis fallback.
    tangent = state.back * -1 - radial * dot(state.back * -1, radial);
    if (length(tangent) < 1e-6)
      tangent =
          cross(std::abs(radial.z) < .9 ? V{0, 0, 1} : V{0, 1, 0}, radial);
  }
  tangent = unit(tangent);
  result.reference_radius_metres = radius;
  result.target_position = state.position;
  result.target_available = radius > surface;
  result.delta_velocity_available = result.target_available;
  if (request.mode == GuidanceMode::orbit) {
    result.reference_radius_metres = std::max(radius, boundary + 20000.0);
    if (result.reference_radius_metres != radius) {
      result.target_position = radial * result.reference_radius_metres;
      result.delta_velocity_available = false;
    }
    result.target_velocity =
        tangent * std::sqrt(mu / result.reference_radius_metres);
    result.reference_periapsis_radius_metres = result.reference_radius_metres;
    result.cue = result.orbit.clear_orbit ? GuidanceCue::orbit_established
                 : result.radial_degenerate
                     ? GuidanceCue::build_horizontal_speed
                     : GuidanceCue::circular_reference;
  } else if (request.mode == GuidanceMode::return_to_surface) {
    result.reference_periapsis_radius_metres = std::min(radius, boundary);
    result.target_velocity =
        tangent *
        std::sqrt(mu *
                  (2.0 / radius -
                   2.0 / (radius + result.reference_periapsis_radius_metres)));
    result.cue = GuidanceCue::return_reference;
  } else {
    result.target_velocity = tangent * std::sqrt(2.0 * mu / radius);
    result.cue = result.orbit.bound ? GuidanceCue::escape_reference
                                    : GuidanceCue::escape_energy_reached;
  }
  if (result.delta_velocity_available)
    result.delta_velocity = result.target_velocity - state.velocity;
  result.coast_samples.reserve(request.sample_count);
  result.coast_samples.push_back({0, state.position});
  if (radius <= surface) {
    result.termination = GuidanceTermination::spherical_surface;
    result.cue = GuidanceCue::below_reference_surface;
    return result;
  }
  if (radius <= boundary) {
    result.termination = GuidanceTermination::atmosphere;
    result.cue = request.mode == GuidanceMode::return_to_surface
                     ? GuidanceCue::atmosphere_model_limit
                     : GuidanceCue::climb_above_atmosphere;
    result.delta_velocity_available = false;
    result.delta_velocity = {};
    return result;
  }
  guidance_detail::CoastState coast{state.position, state.velocity};
  double elapsed = 0;
  result.termination = GuidanceTermination::horizon;
  for (unsigned sample = 1; sample < request.sample_count; ++sample) {
    const double target_time =
        request.horizon_seconds * sample / (request.sample_count - 1);
    while (elapsed < target_time) {
      const double seconds = std::min(2.0, target_time - elapsed);
      const auto next = guidance_detail::coast_step(coast, mu, seconds);
      if (!finite(next.position) || !finite(next.velocity) ||
          length(next.position) > 1e10 || length(next.velocity) > 100000) {
        result.termination = GuidanceTermination::numerical_limit;
        return result;
      }
      if (const auto hit = guidance_detail::boundary_fraction(
              coast.position, next.position, boundary)) {
        result.coast_samples.push_back(
            {elapsed + seconds * *hit,
             coast.position + (next.position - coast.position) * *hit});
        result.termination = result.orbit.atmosphere_edge > 0
                                 ? GuidanceTermination::atmosphere
                                 : GuidanceTermination::spherical_surface;
        return result;
      }
      coast = next;
      // Set the scheduled timestamp exactly on the final internal substep.
      elapsed =
          seconds == target_time - elapsed ? target_time : elapsed + seconds;
    }
    result.coast_samples.push_back({target_time, coast.position});
  }
  return result;
}

} // namespace apsis_drift::flight_lab
