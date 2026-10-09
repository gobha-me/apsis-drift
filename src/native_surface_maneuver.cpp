#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/terrain_touchdown.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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
auto dot(V a, V b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto norm(V a) -> double {
  return std::hypot(a.x, a.y, a.z);
}
auto rotate(RigidOrientation q, V v) -> V {
  const auto s = 2 / (((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z);
  return {
      (1 - s * (q.y * q.y + q.z * q.z)) * v.x +
          s * (q.x * q.y - q.w * q.z) * v.y + s * (q.x * q.z + q.w * q.y) * v.z,
      s * (q.x * q.y + q.w * q.z) * v.x +
          (1 - s * (q.x * q.x + q.z * q.z)) * v.y +
          s * (q.y * q.z - q.w * q.x) * v.z,
      s * (q.x * q.z - q.w * q.y) * v.x + s * (q.y * q.z + q.w * q.x) * v.y +
          (1 - s * (q.x * q.x + q.y * q.y)) * v.z};
}
auto manual_input(const NativeFlightControls& c) -> bool {
  return c.positive_translation != V{} || c.negative_translation != V{} ||
         c.positive_rotation != V{} || c.negative_rotation != V{};
}
} // namespace
auto NativeFreedomFlightSession::cancel_surface_maneuver() -> void {
  surface_maneuver_ = {};
}
auto NativeFreedomFlightSession::request_landing()
    -> std::expected<void, std::string> {
  if (travel_ && travel_->phase != FreedomJumpPhase::idle)
    return std::unexpected{"Cancel the active jump before landing aid"};
  if (actor_ || (docking_ && docking_->attached) ||
      (surface_ && surface_->landed))
    return std::unexpected{
        "Landing requires a seated pilot in a free airborne craft"};
  auto candidate = *this;
  if (auto gear = candidate.set_landing_gear(true); !gear) return gear;
  candidate.cancel_port_approach();
  candidate.cancel_surface_maneuver();
  if (document_.model.assistance) {
    if (document_.model.hold.target)
      return std::unexpected{"Disable orbit hold before assisted landing"};
    candidate.surface_maneuver_ = {
        NativeSurfaceManeuverKind::landing, 7200,
        "Landing with thrusters; manual input cancels without stowing gear"};
    if (auto controls = candidate.surface_maneuver_controls(); !controls)
      return std::unexpected{controls.error()};
  } else
    candidate.surface_maneuver_.note = "Gear deployed; manual landing";
  *this = std::move(candidate);
  return {};
}

auto NativeFreedomFlightSession::surface_maneuver_controls() const
    -> std::expected<NativeFlightControls, std::string> {
  if (!surface_ || !surface_->gear_deployed || surface_->landed || actor_ ||
      (docking_ && docking_->attached) || document_.model.hold.target)
    return std::unexpected{
        "Surface maneuver requires deployed gear and free flight"};
  auto snapshot = TerrainTouchdownSnapshot::create(*this, true);
  if (!snapshot)
    return std::unexpected{"Terrain unavailable for bounded surface maneuver"};
  const auto query = snapshot->query();
  if (!query.assessment)
    return std::unexpected{"Whole-pad terrain support is unresolved"};
  const auto& fixed = query.geometry.provenance.fixed_query_state;
  V normal{};
  double minimum = std::numeric_limits<double>::infinity(), maximum = -minimum;
  for (unsigned i = 0; i < query.assessment->support_count; ++i) {
    if (!query.supports[i] || !*query.supports[i])
      return std::unexpected{"Terrain support is unresolved"};
    const auto& pad = **query.supports[i];
    if (pad.surface != TouchdownSurface::solid || !pad.footprint_supported)
      return std::unexpected{"Surface aid needs certified dry bedrock"};
    normal = add(normal, pad.outward_normal);
    minimum = std::min(minimum, pad.minimum_gap_metres);
    maximum = std::max(maximum, pad.maximum_gap_metres);
  }
  if (norm(normal) <= 0) return std::unexpected{"Surface normal unavailable"};
  normal = scale(normal, 1 / norm(normal));
  const auto up = rotate(fixed.orientation, {0, 1, 0});
  const auto radial =
      scale(fixed.position_metres, 1 / norm(fixed.position_metres));
  const bool lifting =
      surface_maneuver_.kind == NativeSurfaceManeuverKind::liftoff;
  if (dot(up, normal) < (lifting ? .9781476007338057 : .999) ||
      dot(normal, radial) < .9781476007338057 ||
      norm(fixed.angular_velocity_radians_per_second) > .002)
    return std::unexpected{
        "Align upright within 2.5 degrees and stop rotation first"};
  if (minimum < (lifting ? -.30 : -.25) || maximum > 25 ||
      maximum - minimum > (lifting ? .300001 : .20) ||
      norm(fixed.linear_velocity_metres_per_second) > 5)
    return std::unexpected{
        "Surface aid needs even ground within 25 m and speed below 5 m/s"};
  const auto frame = resolve_craft_frame(document_.flight.craft);
  const auto gravity = evaluate_central_body_gravity(
      {system_}, document_.flight, document_.model.central);
  if (!frame || !gravity)
    return std::unexpected{"Surface thrust/gravity unavailable"};
  const auto planet = find_local_system_planet(system_, *fixed.frame.planet);
  if (!planet) return std::unexpected{"Surface owner unavailable"};
  const auto& properties = frame->properties;
  const auto nominal_g =
      static_cast<double>((*planet)->descriptor.surface_gravity.value) *
      9.80665 / 1000;
  const auto pressure = (*planet)->descriptor.atmosphere_pressure.value;
  if (nominal_g * 1000 > properties.max_surface_gravity_mm_per_second2 ||
      pressure > properties.max_pressure_millibars ||
      properties.positive_force_newtons[1] * dot(up, radial) <=
          properties.dry_mass_kg * nominal_g)
    return std::unexpected{
        "Surface gravity, pressure or vertical thrust rating exceeded"};
  const auto velocity = fixed.linear_velocity_metres_per_second;
  const auto normal_velocity = dot(velocity, normal);
  const auto lateral = sub(velocity, scale(normal, normal_velocity));
  const auto target_velocity =
      surface_maneuver_.kind == NativeSurfaceManeuverKind::liftoff
          ? 2.0
          : std::clamp(-.8 * (maximum + .04), -.4, -.04);
  const auto acceleration =
      add(scale(normal, 1.8 * (target_velocity - normal_velocity)),
          scale(lateral, -1.4));
  // A neutral fixed pose supplies the rotating-ground acceleration. The actual
  // craft remains untouched; only real rated thrust counters gravity and error.
  auto held = fixed;
  held.linear_velocity_metres_per_second = {};
  held.angular_velocity_radians_per_second = {};
  const LandedCraftAnchor target{1, 1, rotation_.rotation.version,
                                 rotation_.owner_version, held};
  const auto now =
      resolve_landed_craft(system_, rotation_, target, document_.flight.tick);
  const auto next = resolve_landed_craft(system_, rotation_, target,
                                         document_.flight.tick + 1);
  if (!now || !next)
    return std::unexpected{"Rotating-ground acceleration unavailable"};
  const auto q = document_.flight.orientation;
  const auto fixed_to_inertial =
      rotate(q, rotate({fixed.orientation.w, -fixed.orientation.x,
                        -fixed.orientation.y, -fixed.orientation.z},
                       acceleration));
  const auto demand = sub(
      add(fixed_to_inertial, scale(sub(next->linear_velocity_metres_per_second,
                                       now->linear_velocity_metres_per_second),
                                   1 / kSimulationStep.count())),
      gravity->acceleration_metres_per_second_squared);
  const auto body = rotate({q.w, -q.x, -q.y, -q.z}, demand);
  const auto fraction = [mass = properties.dry_mass_kg](double a,
                                                        std::uint32_t rating) {
    return std::clamp(a * mass / rating, 0.0, 1.0);
  };
  NativeFlightControls commands;
  commands.positive_translation = {
      fraction(body.x, properties.positive_force_newtons[0]),
      fraction(body.y, properties.positive_force_newtons[1]),
      fraction(body.z, properties.positive_force_newtons[2])};
  commands.negative_translation = {
      fraction(-body.x, properties.negative_force_newtons[0]),
      fraction(-body.y, properties.negative_force_newtons[1]),
      fraction(-body.z, properties.negative_force_newtons[2])};
  return commands;
}

auto NativeFreedomFlightSession::try_surface_contact() -> void {
  if (!surface_ || !surface_->gear_deployed || surface_->landed ||
      surface_maneuver_.kind == NativeSurfaceManeuverKind::liftoff ||
      (docking_ && docking_->attached) || actor_)
    return;
  const auto checksum = rigid_body_state_checksum({system_}, document_.flight);
  if (checksum) (void)commit_touchdown(*checksum);
}
auto NativeFreedomFlightSession::advance_surface(
    const NativeFlightControls& controls, SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (!std::isfinite(step.count()) || step != kSimulationStep ||
      document_.flight.tick >= std::numeric_limits<SimulationTick>::max() - 2)
    return std::unexpected{"Surface flight requires an advanceable fixed tick"};
  const VacuumIntent input{
      controls.positive_translation, controls.negative_translation,
      controls.positive_rotation, controls.negative_rotation,
      document_.model.assistance};
  if (!evaluate_atmospheric_flight({system_}, document_.flight, input,
                                   rotation_, document_.model.atmosphere,
                                   document_.model.central))
    return std::unexpected{"Surface flight input refused"};
  auto candidate = *this;
  auto requested = controls;
  if (manual_input(controls) &&
      candidate.surface_maneuver_.kind != NativeSurfaceManeuverKind::off) {
    candidate.cancel_surface_maneuver();
    candidate.surface_maneuver_.note =
        "Surface aid canceled by manual control; gear remains deployed";
  }
  if (candidate.surface_maneuver_.kind == NativeSurfaceManeuverKind::landing)
    candidate.try_surface_contact();
  if (candidate.surface_maneuver_.kind != NativeSurfaceManeuverKind::off) {
    if (auto aid = candidate.surface_maneuver_controls()) {
      requested = *aid;
      --candidate.surface_maneuver_.remaining_ticks;
    } else {
      candidate.cancel_surface_maneuver();
      candidate.surface_maneuver_.note = "Surface aid stopped: " + aid.error();
    }
  }
  auto result = candidate.advance_craft_tick(requested, step);
  if (!result) return std::unexpected{result.error()};
  if (candidate.surface_maneuver_.kind == NativeSurfaceManeuverKind::liftoff) {
    auto snapshot = TerrainTouchdownSnapshot::create(candidate, true);
    if (snapshot) {
      const auto query = snapshot->query();
      bool clear = query.assessment && query.assessment->classification ==
                                           TouchdownClass::pads_clear;
      for (unsigned i = 0; clear && i < query.assessment->support_count; ++i)
        clear = query.supports[i] && *query.supports[i] &&
                (**query.supports[i]).minimum_gap_metres >= 3;
      if (clear) {
        candidate.cancel_surface_maneuver();
        candidate.surface_maneuver_.note =
            "Airborne; manual flight resumes with gear deployed";
      }
    }
  } else
    candidate.try_surface_contact();
  if (candidate.surface_ && candidate.surface_->landed) {
    const auto observed = candidate.observe();
    if (!observed) return std::unexpected{observed.error()};
    result->actuation.after = observed->atmosphere;
    result->actuation.observation_after = observed->orbit;
  }
  if (candidate.surface_maneuver_.kind != NativeSurfaceManeuverKind::off &&
      candidate.surface_maneuver_.remaining_ticks == 0) {
    candidate.cancel_surface_maneuver();
    candidate.surface_maneuver_.note =
        "Surface aid timed out; manual control resumes";
  }
  *this = std::move(candidate);
  return result;
}
} // namespace apsis_drift
