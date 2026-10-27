#pragma once

#include "apsis_drift/godot/saved_minor_flight.hpp"
#include "apsis_drift/godot/saved_moon_flight.hpp"
#include "apsis_drift/godot/streaming.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/native_planet_tracking.hpp"
#include "apsis_drift/planet_optics.hpp"
#include "apsis_drift/secondary_bodies.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <string>

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
  if (session.moon())
    throw std::invalid_argument("Planet projection cannot carry a Moon owner");
  const auto body = require(session.relative_state());
  if (!body.frame.planet)
    throw std::invalid_argument(
        "Planet projection requires its selected owner");
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

// The same rated instrument for an actual Moon owner, including prospective
// free flight while landed. A foot observer cannot stand in for the craft's
// radial attitude. Saved rigid-state versions retain their admitted semantics.
inline auto rated_rise_margin(const NativeFreedomFlightSession& session,
                              const SavedMoonFlightProjection& view)
    -> std::optional<double> {
  const auto& document = session.document();
  const auto& body = document.flight;
  if (!session.moon() || !document.moon ||
      view.version != kSavedMoonFlightProjectionVersion ||
      view.moon != *session.moon() || view.body != body ||
      view.surface_observer ||
      body.frame.kind != RigidFrameKind::body_relative_inertial ||
      body.frame.body != view.moon.body || session.recovery_pending() ||
      session.walker() || session.surface_walker() ||
      (session.docking() && session.docking()->attached) ||
      (session.travel() && session.travel()->phase != FreedomJumpPhase::idle))
    return std::nullopt;
  const auto frame = resolve_craft_frame(body.craft);
  const auto gravity =
      evaluate_moon_body_gravity(RigidBodyWorldContext{*session.moon()}, body,
                                 {document.moon->dynamics_recipe});
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

inline auto validate_saved_projection(const NativeFreedomFlightSession& session)
    -> void {
  if (session.minor())
    (void)project_saved_minor_flight(session);
  else if (session.moon())
    (void)project_saved_moon_flight(session);
  else
    (void)project_saved_flight(session);
}

struct QuietTravelView {
  bool available{};
  std::uint32_t multiplier{1};
  std::string note;
};

struct SavedFlightWorld {
  NativeFreedomFlightSession session;
  FixedStepClock clock;
  std::optional<NativeFlightStep> last_step;
  double dropped_seconds{};
  // Presentation memoization only: one actual selected body, no save/catalog.
  mutable std::optional<PlanetOpticsField> optical_field{};
  mutable std::optional<SecondaryBodyCatalog> secondary_catalog{};
  // Transient request only. Append after historical aggregate fields; never
  // serialize it or turn unexecuted bonus work into clock debt.
  std::uint32_t quiet_travel_multiplier{1};
  // View-local admitted target; new/reopened worlds deliberately start empty.
  mutable NativePlanetTracking planet_tracking{};

  [[nodiscard]] auto quiet_travel_view() const -> QuietTravelView {
    const auto* refusal = quiet_travel_refusal(session);
    return {refusal == nullptr, quiet_travel_multiplier,
            refusal ? refusal
                    : "Quiet travel available. Assistance, fuel and deadlines "
                      "continue normally."};
  }

  [[nodiscard]] auto set_quiet_travel_multiplier(std::uint32_t multiplier)
      -> std::expected<void, std::string> {
    if (multiplier != 1 && multiplier != 2 && multiplier != 4)
      return std::unexpected{"Quiet travel supports only 1, 2 or 4."};
    if (multiplier != 1) {
      if (const auto* refusal = quiet_travel_refusal(session))
        return std::unexpected{std::string{refusal}};
    }
    quiet_travel_multiplier = multiplier;
    return {};
  }

  auto reset_quiet_travel() noexcept -> void { quiet_travel_multiplier = 1; }

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
    auto multiplier = quiet_travel_multiplier;
    if (std::ranges::any_of(fractions,
                            [](double value) { return value != 0; }) ||
        (multiplier != 1 && quiet_travel_refusal(candidate)))
      multiplier = 1;
    const auto scheduled =
        require(candidate_clock.advance(SimulationSeconds{elapsed}));
    auto dropped = scheduled.dropped;
    // Ordinary ticks have first claim on the existing15-tick budget. Bonus
    // ticks use this same clock's already scheduled opportunities, never a
    // scaled delta or a second accumulator.
    int bonus_budget = kMaxCatchUpSteps - scheduled.steps;
    const auto advance_tick = [&](int remaining_ordinary) {
      actuation = require(candidate.advance(controls));
      if (candidate.recovery_pending()) {
        multiplier = 1;
        // Only unsimulated ordinary time and the real fractional accumulator
        // are retired. Unexecuted bonus ticks were never time debt.
        dropped += kSimulationStep * remaining_ordinary +
                   candidate_clock.accumulator();
        candidate_clock.reset();
        return false;
      }
      if (multiplier != 1 && quiet_travel_refusal(candidate)) multiplier = 1;
      return true;
    };
    for (int i = 0; i < scheduled.steps; ++i) {
      const int remaining = scheduled.steps - i - 1;
      if (!advance_tick(remaining)) break;
      bool running = true;
      for (std::uint32_t bonus = 1; bonus < multiplier && bonus_budget > 0;
           ++bonus) {
        if (quiet_travel_refusal(candidate)) {
          multiplier = 1;
          break;
        }
        --bonus_budget;
        if (!advance_tick(remaining)) {
          running = false;
          break;
        }
      }
      if (!running) break;
    }
    // Qualify the projected selected owner before committing this whole batch.
    // Rendering never supplies pose, rotation or a substitute world recipe.
    validate_saved_projection(candidate);
    session = std::move(candidate);
    clock = candidate_clock;
    last_step = actuation;
    dropped_seconds += dropped.count();
    quiet_travel_multiplier = multiplier;
    (void)planet_tracking.read(session);
  }

 private:
  // Fixed reasons stay bounded and reveal no unobserved world facts. These
  // are scheduling guards, not new flight laws or all-body collision safety.
  static auto quiet_travel_refusal(const NativeFreedomFlightSession& selected)
      -> const char* {
    if (selected.recovery_pending())
      return "Quiet travel held: recovery pending.";
    if (selected.moon() || selected.minor() || selected.document().moon ||
        selected.document().minor)
      return "Quiet travel supports primary-planet flight only.";
    if (selected.walker() || selected.surface_walker())
      return "Quiet travel requires a seated pilot.";
    if (selected.boarding() &&
        selected.boarding()->phase != FreedomBoardingPhase::seated)
      return "Quiet travel held during boarding or disembarking.";
    if ((selected.docking() && selected.docking()->attached) ||
        (selected.surface() && selected.surface()->landed))
      return "Quiet travel requires unattached free flight.";
    if (selected.travel() && selected.travel()->phase != FreedomJumpPhase::idle)
      return "Quiet travel held during jump spool or transit.";
    if (selected.origin_assistance()) {
      const auto* sortie =
          std::get_if<OriginServiceSortie>(&*selected.origin_assistance());
      if (sortie && sortie->phase != OriginServiceSortiePhase::completed &&
          sortie->phase != OriginServiceSortiePhase::canceled)
        return "Quiet travel held while Origin fuel service is active.";
    }
    if (selected.surface_maneuver().kind != NativeSurfaceManeuverKind::off ||
        selected.port_approach().active || selected.port_docking().active ||
        selected.port_rendezvous().active)
      return "Quiet travel held while a flight maneuver is active.";
    const auto body = selected.relative_state();
    const auto observation = selected.observe();
    if (!body || body->frame.kind != RigidFrameKind::planet_relative_inertial ||
        !body->frame.planet || !observation)
      return "Quiet travel flight reference is unavailable.";
    const auto& air = observation->atmosphere;
    if (!std::isfinite(air.altitude_metres) ||
        !std::isfinite(air.space_boundary_altitude_metres) ||
        air.altitude_metres < air.space_boundary_altitude_metres + 100000.0)
      return "Quiet travel requires space with a 100 km atmosphere buffer.";
    const auto seed = selected.document().origin.recipe.universe_seed;
    if (selected.system().catalog.id ==
        generate_first_intersystem_identities(seed).origin_system) {
      const auto station = generate_origin_station(seed);
      const auto port = resolve_origin_station_ephemeris(
          selected.system(), station, {body->tick, 0});
      const auto planet = resolve_planet_ephemeris(
          selected.system(), *body->frame.planet, {body->tick, 0});
      if (!port || !planet)
        return "Quiet travel station reference is unavailable.";
      const double distance = std::hypot(
          planet->position.x + body->position_metres.x - port->position.x,
          planet->position.y + body->position_metres.y - port->position.y,
          planet->position.z + body->position_metres.z - port->position.z);
      if (!std::isfinite(distance) || distance <= 10000.0)
        return "Quiet travel requires more than 10 km from Origin station.";
    }
    // Paid port purchase is instantaneous and attached. The separate live NPC
    // sortie is excluded above; home/terminal/history records remain allowed.
    // Passive assistance/hold and fuel quantity do not gate.
    return nullptr;
  }
};
} // namespace apsis_drift::godot_spike
