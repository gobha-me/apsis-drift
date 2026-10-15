#pragma once

#include "apsis_drift/godot/streaming.hpp"
#include "apsis_drift/native_flight_session.hpp"

#include <algorithm>

namespace apsis_drift::godot_spike {
// Presentation projection only. The saved nonrotating state stays untouched.
inline auto rotate_saved(RigidOrientation q, StreamPoint v) -> StreamPoint {
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
inline auto inverse_saved(RigidOrientation q) -> RigidOrientation {
  return {q.w, -q.x, -q.y, -q.z};
}
struct SavedFlightProjection {
  PlanetDescriptor planet;
  PhysicalPlanetRotationGeometry rotation;
  GeodeticPosition pose;
  LocalTangentFrame tangent;
  std::array<LocalPositionMetres, 3> body_axes;
  std::array<LocalPositionMetres, 3> system_axes;
  LocalPositionMetres station_position;
  OriginStationDescriptor station;
  bool station_available{};
};
inline auto project_saved_flight(const NativeFreedomFlightSession& session)
    -> SavedFlightProjection {
  const auto body = require(session.relative_state());
  const auto& system = session.system();
  const auto planet =
      require(find_local_system_planet(system, *body.frame.planet))->descriptor;
  auto rotation =
      require(resolve_planet_rotation(system, session.rotation(), body.tick));
  const auto inverse = inverse_saved(rotation.geometry.fixed_to_system);
  const auto fixed =
      rotate_saved(inverse, {body.position_metres.x, body.position_metres.y,
                             body.position_metres.z});
  const auto pose = require(geodetic_from_planet_fixed(planet, fixed));
  const auto tangent = require(make_local_tangent_frame(planet, pose));
  rotation = require(
      resolve_planet_rotation(system, session.rotation(), body.tick, fixed));
  auto local_axis = [&](StreamPoint system_axis) -> LocalPositionMetres {
    const auto v = rotate_saved(inverse, system_axis);
    return {v.x * tangent.east.x + v.y * tangent.east.y + v.z * tangent.east.z,
            v.x * tangent.north.x + v.y * tangent.north.y +
                v.z * tangent.north.z,
            v.x * tangent.up.x + v.y * tangent.up.y + v.z * tangent.up.z};
  };
  const auto station =
      generate_origin_station(session.document().origin.recipe.universe_seed);
  const bool station_available =
      system.catalog.id == generate_first_intersystem_identities(
                               session.document().origin.recipe.universe_seed)
                               .origin_system &&
      planet.id == station.orbit.host_planet;
  LocalPositionMetres station_position{};
  if (station_available) {
    const auto ephemeris =
        require(resolve_origin_station_ephemeris(system, station, {body.tick}));
    const auto& p = ephemeris.host_relative_position;
    station_position =
        local_axis({p.x - body.position_metres.x, p.y - body.position_metres.y,
                    p.z - body.position_metres.z});
  }
  return {planet,
          rotation,
          pose,
          tangent,
          {local_axis(rotate_saved(body.orientation, {1, 0, 0})),
           local_axis(rotate_saved(body.orientation, {0, 1, 0})),
           local_axis(rotate_saved(body.orientation, {0, 0, 1}))},
          {local_axis({1, 0, 0}), local_axis({0, 1, 0}), local_axis({0, 0, 1})},
          station_position,
          station,
          station_available};
}

// Rated free-flight radial acceleration from full +Y thrust and central
// gravity only. Assumes fuel; excludes contact, drag and every other channel.
// This is an instrument, never a hover, departure or escape permission.
inline auto rated_rise_margin(const NativeFreedomFlightSession& session,
                              const SavedFlightProjection& view)
    -> std::optional<double> {
  const auto& document = session.document();
  const auto& body = document.flight;
  if (body.frame.kind != RigidFrameKind::planet_relative_inertial ||
      session.recovery_pending() || session.walker() ||
      session.surface_walker() ||
      (session.docking() && session.docking()->attached) ||
      (session.travel() && session.travel()->phase != FreedomJumpPhase::idle))
    return std::nullopt;
  const auto frame = resolve_craft_frame(body.craft);
  const auto gravity = evaluate_central_body_gravity(
      RigidBodyWorldContext{session.system()}, body, document.model.central);
  const double up = view.body_axes[1].up;
  if (!frame || !gravity || !std::isfinite(up) || std::abs(up) > 1.000000000001)
    return std::nullopt;
  const auto& properties = frame->properties;
  const auto& g = gravity->acceleration_metres_per_second_squared;
  const double margin =
      static_cast<double>(properties.positive_force_newtons[1]) /
          properties.dry_mass_kg * std::clamp(up, -1.0, 1.0) -
      std::hypot(g.x, g.y, g.z);
  return std::isfinite(margin) ? std::optional{margin} : std::nullopt;
}

struct SavedFlightWorld {
  NativeFreedomFlightSession session;
  FixedStepClock clock;
  std::optional<NativeFlightStep> last_step;
  double dropped_seconds{};

  auto advance(double elapsed, std::span<const double> fractions, bool paused)
      -> void {
    if (!std::isfinite(elapsed) || elapsed < 0 || elapsed > 60 ||
        fractions.size() != 12)
      throw std::invalid_argument("Invalid saved-flight elapsed time/buffer");
    for (const double v : fractions)
      if (!std::isfinite(v) || v < 0 || v > 1)
        throw std::invalid_argument(
            "Saved actuator fractions must be in [0,1]");
    if (paused) return;
    const NativeFlightControls controls{
        {fractions[0], fractions[1], fractions[2]},
        {fractions[3], fractions[4], fractions[5]},
        {fractions[6], fractions[7], fractions[8]},
        {fractions[9], fractions[10], fractions[11]}};
    auto candidate = session;
    auto candidate_clock = clock;
    auto actuation = last_step;
    const auto scheduled =
        require(candidate_clock.advance(SimulationSeconds{elapsed}));
    auto dropped = scheduled.dropped;
    for (int i = 0; i < scheduled.steps; ++i) {
      actuation = require(candidate.advance(controls));
      if (candidate.recovery_pending()) {
        // The actual loss owns its final tick. Retire the unsimulated frame
        // tail, including any fractional time, before explicit recovery.
        dropped += kSimulationStep * (scheduled.steps - i - 1) +
                   candidate_clock.accumulator();
        candidate_clock.reset();
        break;
      }
    }
    // Qualify the projected selected owner before committing this whole batch.
    // Rendering never supplies pose, rotation or a substitute world recipe.
    (void)project_saved_flight(candidate);
    session = std::move(candidate);
    clock = candidate_clock;
    last_step = actuation;
    dropped_seconds += dropped.count();
  }
};
} // namespace apsis_drift::godot_spike
