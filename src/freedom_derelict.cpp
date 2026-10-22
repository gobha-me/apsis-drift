#include "apsis_drift/freedom_derelict.hpp"

#include "apsis_drift/atmospheric_flight.hpp"
#include "apsis_drift/central_body_dynamics.hpp"
#include "apsis_drift/secondary_bodies.hpp"
#include "local_system_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace apsis_drift {
namespace {
using Error = FreedomDerelictError;
constexpr CraftFrameRecipe frame{kWayfarerFrameId, kWayfarerFrameVersion};
// Same source-derived full terrain bound used by ordinary-planet travel.
constexpr double maximum_relief_metres{17'070.0};
constexpr double moon_relief_metres{4'545.0};
auto attribute(Seed seed, std::uint64_t ordinal) -> std::uint64_t {
  return derive_seed(seed, SeedDomain::freedom_derelict, ordinal).value;
}
auto hull_radius() -> std::expected<double, Error> {
  const auto resolved = resolve_craft_frame(frame);
  if (!resolved) return std::unexpected{Error::invalid_owner};
  const auto& p = resolved->properties;
  double radius{};
  for (unsigned corner = 0; corner < 8; ++corner) {
    const auto component = [&](unsigned axis) {
      return (static_cast<double>((corner & (1U << axis))
                                      ? p.hull_max_mm[axis]
                                      : p.hull_min_mm[axis]) -
              p.center_of_mass_mm[axis]) /
             1000.0;
    };
    radius =
        std::max(radius, std::hypot(component(0), component(1), component(2)));
  }
  if (!std::isfinite(radius) || radius <= 0)
    return std::unexpected{Error::unsafe_arithmetic};
  return radius;
}
auto observation_shape(const FreedomDerelictObservation& r, SimulationTick now)
    -> bool {
  if (!r.craft.value || r.tick > now ||
      r.tick == std::numeric_limits<SimulationTick>::max())
    return false;
  for (const auto v :
       {r.position_metres.x, r.position_metres.y, r.position_metres.z})
    if (!std::isfinite(v) || std::abs(v) > kRigidBodyMaximumPositionMetres)
      return false;
  for (const auto v :
       {r.velocity_metres_per_second.x, r.velocity_metres_per_second.y,
        r.velocity_metres_per_second.z})
    if (!std::isfinite(v) ||
        std::abs(v) > kRigidBodyMaximumVelocityMetresPerSecond)
      return false;
  return true;
}
auto resolve_validated(const FreedomDerelictDescriptor& d, SimulationTick tick)
    -> std::expected<RigidBodyState, Error> {
  // Numeric circular carrier only: its PlanetId is the genuine parent, not a
  // second planet/body identity or a catalog entry for the Wayfarer hulk.
  const PlanetOrbit carrier{{},
                            d.planet,
                            0,
                            d.radius_kilometres,
                            d.period_ticks,
                            d.epoch_phase_turns,
                            d.inclination_microdegrees,
                            d.ascending_node_turns};
  const auto orbit =
      detail::resolve_validated_circular_orbit(carrier, {tick, 0}, true);
  if (!orbit) return std::unexpected{Error::unsafe_arithmetic};
  RigidBodyState pose{
      frame, {RigidFrameKind::planet_relative_inertial, d.system, d.planet, {}},
      tick,  {orbit->position.x, orbit->position.y, orbit->position.z},
      {},    {orbit->velocity.x, orbit->velocity.y, orbit->velocity.z},
      {}};
  const auto canonical =
      canonicalize_rigid_body_state(RigidBodyWorldContext{d.owner}, pose);
  if (!canonical) return std::unexpected{Error::unsafe_arithmetic};
  return *canonical;
}
auto observation(const FreedomDerelictDescriptor& d,
                 const FreedomDerelictObservation& r, bool inspection)
    -> std::expected<void, Error> {
  const auto parent = find_local_system_planet(d.owner, d.planet);
  if (!parent) return std::unexpected{Error::invalid_owner};
  const auto atmosphere =
      resolve_atmospheric_flight_profile((*parent)->descriptor);
  if (!atmosphere) return std::unexpected{Error::invalid_owner};
  const double altitude = std::hypot(r.position_metres.x, r.position_metres.y,
                                     r.position_metres.z) -
                          (*parent)->descriptor.radius.value * 1000.0;
  if (!std::isfinite(altitude))
    return std::unexpected{Error::unsafe_arithmetic};
  if (altitude <= atmosphere->space_boundary_altitude_metres)
    return std::unexpected{Error::invalid_observation};
  const auto pose = resolve_validated(d, r.tick);
  if (!pose) return std::unexpected{pose.error()};
  const double distance =
      std::hypot(r.position_metres.x - pose->position_metres.x,
                 r.position_metres.y - pose->position_metres.y,
                 r.position_metres.z - pose->position_metres.z);
  if (!std::isfinite(distance))
    return std::unexpected{Error::unsafe_arithmetic};
  if (!inspection) {
    if (distance > kFreedomDerelictContactRangeMetres)
      return std::unexpected{Error::contact_out_of_range};
    return {};
  }
  if (distance < kFreedomDerelictInspectionMinimumMetres ||
      distance > kFreedomDerelictInspectionMaximumMetres)
    return std::unexpected{Error::inspection_out_of_range};
  const double speed =
      std::hypot(r.velocity_metres_per_second.x -
                     pose->linear_velocity_metres_per_second.x,
                 r.velocity_metres_per_second.y -
                     pose->linear_velocity_metres_per_second.y,
                 r.velocity_metres_per_second.z -
                     pose->linear_velocity_metres_per_second.z);
  if (!std::isfinite(speed)) return std::unexpected{Error::unsafe_arithmetic};
  if (speed > kFreedomDerelictInspectionMaximumSpeedMetresPerSecond)
    return std::unexpected{Error::inspection_speed_exceeded};
  return {};
}
} // namespace

auto generate_freedom_derelict(Seed universe_seed)
    -> std::expected<FreedomDerelictDescriptor, Error> {
  const auto network = generate_freedom_system_network(
      universe_seed, kFreedomUnchartedSystemNetworkVersion);
  if (!network) return std::unexpected{Error::invalid_owner};
  const auto owner =
      resolve_freedom_network_system(*network, network->nodes[4].system);
  if (!owner || owner->catalog.planets.empty())
    return std::unexpected{Error::invalid_owner};
  const auto& parent = owner->catalog.planets.front().descriptor;
  const auto seed = derive_seed(universe_seed, SeedDomain::freedom_derelict,
                                kFreedomDerelictVersion);
  const auto id = attribute(seed, 0);
  if (!id || id == owner->catalog.id.value ||
      id == owner->catalog.star.id.value ||
      std::ranges::any_of(owner->catalog.planets, [&](const auto& p) {
        return p.descriptor.id.value == id;
      }))
    return std::unexpected{Error::invalid_descriptor};
  const auto atmosphere = resolve_atmospheric_flight_profile(parent);
  const auto hull = hull_radius();
  if (!atmosphere || !hull ||
      !std::isfinite(atmosphere->space_boundary_altitude_metres) ||
      atmosphere->space_boundary_altitude_metres < 0)
    return std::unexpected{Error::unsafe_arithmetic};
  const double envelope = std::max(maximum_relief_metres,
                                   atmosphere->space_boundary_altitude_metres);
  const double edge_km = std::ceil(envelope / 1000.0);
  if (!std::isfinite(edge_km) || edge_km < 0 || edge_km > 1000)
    return std::unexpected{Error::unsafe_arithmetic};
  // A small near-planet place, not a remote alternate universe. The integer
  // radius adds 200..400 km beyond both envelopes; the full Wayfarer hull
  // clearance is checked below.
  const std::uint64_t radius_km = parent.radius.value +
                                  static_cast<std::uint64_t>(edge_km) + 200 +
                                  attribute(seed, 1) % 201;
  const double radius = static_cast<double>(radius_km) * 1000.0;
  RigidBodyState reference{frame,
                           {RigidFrameKind::planet_relative_inertial,
                            owner->catalog.id,
                            parent.id,
                            {}},
                           0,
                           {radius, 0, 0},
                           {},
                           {},
                           {}};
  const auto gravity =
      evaluate_central_body_gravity(RigidBodyWorldContext{*owner}, reference);
  if (!gravity) return std::unexpected{Error::invalid_owner};
  const double gm_km3 =
      gravity->gravitational_parameter_metres_cubed_per_second_squared / 1.0e9;
  if (!std::isfinite(gm_km3) || gm_km3 <= 0)
    return std::unexpected{Error::unsafe_arithmetic};
  const double a = static_cast<double>(radius_km);
  const double ticks =
      std::round(static_cast<double>(kSimulationHz) *
                 (2.0 * std::numbers::pi * std::sqrt(((a * a) * a) / gm_km3)));
  if (!std::isfinite(ticks) || ticks < 1 || ticks > 9'007'199'254'740'991.0 ||
      radius - *hull <= parent.radius.value * 1000.0 + envelope)
    return std::unexpected{Error::unsafe_arithmetic};
  const auto secondary = generate_secondary_body_catalog(*owner);
  if (!secondary) return std::unexpected{Error::invalid_owner};
  for (const auto& body : secondary->bodies) {
    if (body.node.id.value == id)
      return std::unexpected{Error::invalid_descriptor};
    if (body.node.parent == body_id(parent.id) && body.node.orbit &&
        body.node.id.kind == BodyKind::moon &&
        radius + *hull >= body.node.orbit->radius_kilometres * 1000.0 -
                              body.radius_metres - moon_relief_metres)
      return std::unexpected{Error::invalid_descriptor};
  }
  return FreedomDerelictDescriptor{
      kFreedomDerelictVersion,
      universe_seed,
      id,
      owner->catalog.id,
      parent.id,
      *owner,
      radius_km,
      static_cast<SimulationTick>(ticks),
      static_cast<std::uint32_t>(attribute(seed, 2)),
      static_cast<std::int32_t>(attribute(seed, 3) % 20'000'001) - 10'000'000,
      static_cast<std::uint32_t>(attribute(seed, 4))};
}
auto validate_freedom_derelict_descriptor(const FreedomDerelictDescriptor& d)
    -> std::expected<void, Error> {
  if (d.version != kFreedomDerelictVersion)
    return std::unexpected{Error::unsupported_version};
  const auto canonical = generate_freedom_derelict(d.universe_seed);
  if (!canonical) return std::unexpected{canonical.error()};
  if (*canonical != d) return std::unexpected{Error::invalid_descriptor};
  return {};
}
auto resolve_freedom_derelict(const FreedomDerelictDescriptor& d,
                              SimulationTick tick)
    -> std::expected<RigidBodyState, Error> {
  if (tick == std::numeric_limits<SimulationTick>::max())
    return std::unexpected{Error::invalid_tick};
  if (const auto valid = validate_freedom_derelict_descriptor(d); !valid)
    return std::unexpected{valid.error()};
  return resolve_validated(d, tick);
}
auto validate_freedom_derelict_state(Seed seed, const FreedomDerelictState& s,
                                     SimulationTick now)
    -> std::expected<void, Error> {
  if (s.version != kFreedomDerelictVersion)
    return std::unexpected{Error::unsupported_version};
  if (now == std::numeric_limits<SimulationTick>::max())
    return std::unexpected{Error::invalid_tick};
  if ((s.inspection && !s.contact) ||
      (s.inspection && s.inspection->tick < s.contact->tick))
    return std::unexpected{Error::invalid_state};
  if ((s.contact && !observation_shape(*s.contact, now)) ||
      (s.inspection && !observation_shape(*s.inspection, now)))
    return std::unexpected{Error::invalid_observation};
  const auto descriptor = generate_freedom_derelict(seed);
  if (!descriptor) return std::unexpected{descriptor.error()};
  if (s.contact) {
    const auto result = observation(*descriptor, *s.contact, false);
    if (!result) return result;
  }
  if (s.inspection) return observation(*descriptor, *s.inspection, true);
  return {};
}
} // namespace apsis_drift
