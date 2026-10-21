#include "apsis_drift/freedom_planet_jump.hpp"

#include "apsis_drift/intersystem_jump.hpp"
#include "apsis_drift/moon_terrain.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace apsis_drift {
namespace {
using Error = FreedomPlanetJumpError;
// Same source-derived bound as terrain_impact.cpp: signed16 macro noise and
// sea threshold give <17000m; local detail adds <=48+16+5m. The final metre
// covers rounding. Convex terrain/contact triangles cannot exceed it.
constexpr double kMaximumPlanetReliefMetres{17'070.0};
constexpr double kMinorStandOffMetres{1000.0};

auto norm(RigidVector3 v) -> double {
  return std::hypot(v.x, v.y, v.z);
}
auto distance(SystemPositionMetres a, SystemPositionMetres b) -> double {
  return norm({a.x - b.x, a.y - b.y, a.z - b.z});
}
auto rotate(RigidOrientation q, RigidVector3 v) -> RigidVector3 {
  const double n = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  const double k = 2 * (q.x * v.x + q.y * v.y + q.z * v.z) / n;
  const double s = (q.w * q.w - q.x * q.x - q.y * q.y - q.z * q.z) / n;
  return {s * v.x + k * q.x + 2 * q.w * (q.y * v.z - q.z * v.y) / n,
          s * v.y + k * q.y + 2 * q.w * (q.z * v.x - q.x * v.z) / n,
          s * v.z + k * q.z + 2 * q.w * (q.x * v.y - q.y * v.x) / n};
}
auto clearance(const PhysicalLocalSystem& system, SimulationTick tick,
               SystemPositionMetres point, double hull_radius,
               AtmosphericFlightRecipe atmosphere)
    -> std::expected<FreedomLocalBodyClearance, Error> {
  const auto primary =
      assess_freedom_arrival_volume(system, tick, point, hull_radius);
  if (!primary) return std::unexpected{Error::invalid_geometry};
  FreedomLocalBodyClearance result{*primary};
  // A sphere-only clearance is insufficient near a real atmosphere or terrain.
  // Test the complete bounded envelopes at both endpoints, without sampling a
  // local pad or claiming any continuous path between the jump endpoints.
  for (const auto& planet : system.catalog.planets) {
    const auto profile =
        resolve_atmospheric_flight_profile(planet.descriptor, atmosphere);
    const auto ephemeris =
        resolve_planet_ephemeris(system, planet.descriptor.id, {tick, 0});
    if (!profile || !ephemeris ||
        !std::isfinite(profile->space_boundary_altitude_metres) ||
        profile->space_boundary_altitude_metres < 0)
      return std::unexpected{Error::invalid_geometry};
    const double radius =
        static_cast<double>(planet.descriptor.radius.value) * 1000 +
        std::max(kMaximumPlanetReliefMetres,
                 profile->space_boundary_altitude_metres) +
        hull_radius;
    const double separation = distance(point, ephemeris->position);
    if (!std::isfinite(radius) || !std::isfinite(separation))
      return std::unexpected{Error::invalid_geometry};
    if (separation <= radius &&
        std::ranges::find(result.primary.intersecting_planets,
                          planet.descriptor.id) ==
            result.primary.intersecting_planets.end())
      result.primary.intersecting_planets.push_back(planet.descriptor.id);
  }
  const auto catalog = generate_secondary_body_catalog(system);
  if (!catalog) return std::unexpected{Error::invalid_geometry};
  const auto hierarchy = make_body_hierarchy(*catalog);
  if (!hierarchy) return std::unexpected{Error::invalid_geometry};
  const auto bodies = resolve_body_ephemerides(*hierarchy, {tick, 0});
  if (!bodies) return std::unexpected{Error::invalid_geometry};
  for (const auto& secondary : catalog->bodies) {
    const auto pose =
        std::ranges::find(*bodies, secondary.node.id, &BodyEphemeris::body);
    if (pose == bodies->end()) return std::unexpected{Error::invalid_geometry};
    const double radius = secondary.radius_metres + hull_radius +
                          (secondary.node.id.kind == BodyKind::moon
                               ? kMoonTerrainMaximumReliefMetres
                               : kMinorStandOffMetres);
    const double separation = distance(point, pose->position);
    if (!std::isfinite(radius) || !std::isfinite(separation))
      return std::unexpected{Error::invalid_geometry};
    if (secondary.node.id.kind == BodyKind::moon)
      result.intersects_moon = result.intersects_moon || separation <= radius;
    else if (secondary.node.id.kind == BodyKind::minor)
      result.intersects_minor = result.intersects_minor || separation <= radius;
    else
      return std::unexpected{Error::invalid_geometry};
  }
  return result;
}
} // namespace

auto freeze_freedom_planet_jump_arrival(const FreedomPlanetJumpRequest& request)
    -> std::expected<FrozenFreedomPlanetJumpArrival, FreedomPlanetJumpError> {
  if (request.version != kFreedomPlanetJumpVersion)
    return std::unexpected{Error::unsupported_policy};
  if (!request.attempt ||
      request.attempt == std::numeric_limits<std::uint64_t>::max())
    return std::unexpected{Error::invalid_attempt};
  const auto& source = request.source;
  if (source.flight.tick >
      std::numeric_limits<SimulationTick>::max() - kJumpTransitTicks - 3)
    return std::unexpected{Error::tick_overflow};
  if (source.moon || source.minor || source.model.physical_catalog != 2 ||
      source.model.physical_ephemeris != 2 ||
      source.flight.craft !=
          CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion})
    return std::unexpected{Error::invalid_owner};
  const auto hydrated = hydrate_freedom_flight_document(source);
  if (!hydrated) return std::unexpected{Error::invalid_source};
  const auto& system = hydrated->system;
  const auto relative = relative_freedom_flight_state(source, system);
  if (!relative || !relative->frame.planet)
    return std::unexpected{Error::invalid_source};
  const PlanetId destination{request.destination.body.value};
  if (request.destination.system != system.catalog.id ||
      request.destination.body != body_id(destination) ||
      destination == *relative->frame.planet)
    return std::unexpected{Error::invalid_destination};
  const auto planet =
      std::ranges::find_if(system.catalog.planets, [&](const auto& p) {
        return p.descriptor.id == destination;
      });
  if (planet == system.catalog.planets.end())
    return std::unexpected{Error::invalid_destination};
  const RigidBodyWorldContext context{system};
  const auto checksum = rigid_body_state_checksum(context, source.flight);
  const auto departure = reframe_rigid_body(
      context, source.flight,
      {{RigidFrameKind::system_inertial, system.catalog.id, {}, {}},
       source.flight.tick});
  const auto craft = resolve_craft_frame(source.flight.craft);
  if (!checksum || !departure || !craft)
    return std::unexpected{Error::invalid_source};
  const auto& properties = craft->properties;
  const double hull_radius =
      norm({static_cast<double>(std::max(std::abs(properties.hull_min_mm[0]),
                                         std::abs(properties.hull_max_mm[0]))) /
                1000,
            static_cast<double>(std::max(std::abs(properties.hull_min_mm[1]),
                                         std::abs(properties.hull_max_mm[1]))) /
                1000,
            static_cast<double>(std::max(std::abs(properties.hull_min_mm[2]),
                                         std::abs(properties.hull_max_mm[2]))) /
                1000});
  const SystemPositionMetres source_point{departure->position_metres.x,
                                          departure->position_metres.y,
                                          departure->position_metres.z};
  const auto departure_volume =
      clearance(system, source.flight.tick, source_point, hull_radius,
                source.model.atmosphere);
  if (!departure_volume) return std::unexpected{departure_volume.error()};
  if (!departure_volume->clear())
    return std::unexpected{Error::unclear_departure};

  const auto tick = source.flight.tick + kJumpTransitTicks;
  const auto recipe = generate_planet_rotation_recipe(
      system, destination, source.model.rotation_generator);
  const auto profile = resolve_atmospheric_flight_profile(
      planet->descriptor, source.model.atmosphere);
  if (!recipe || !profile ||
      !std::isfinite(profile->space_boundary_altitude_metres) ||
      profile->space_boundary_altitude_metres < 0)
    return std::unexpected{Error::invalid_geometry};
  const double radius =
      static_cast<double>(planet->descriptor.radius.value) * 1000 +
      std::max(profile->space_boundary_altitude_metres,
               kMaximumPlanetReliefMetres) +
      hull_radius + kFreedomPlanetJumpArrivalClearanceMetres;
  if (!std::isfinite(radius)) return std::unexpected{Error::invalid_geometry};
  const auto rotation =
      resolve_planet_rotation(system, *recipe, tick, {0, radius, 0});
  const auto ephemeris =
      resolve_planet_ephemeris(system, destination, {tick, 0});
  if (!rotation || !ephemeris) return std::unexpected{Error::invalid_geometry};
  const auto position =
      rotate(rotation->geometry.fixed_to_system, {0, radius, 0});
  auto arrival = source;
  arrival.origin.state.tick = tick;
  arrival.flight = *relative;
  arrival.flight.tick = tick;
  arrival.flight.frame = {RigidFrameKind::planet_relative_inertial,
                          system.catalog.id,
                          destination,
                          {}};
  arrival.flight.position_metres = position;
  arrival.model.hold = {};
  arrival.world = FreedomActiveWorldSelection{
      source.origin.network ? kFreedomNetworkActiveWorldVersion
                            : kFreedomActiveWorldVersion,
      system.catalog.id, destination};
  const auto canonical = canonicalize_rigid_body_state(context, arrival.flight);
  if (!canonical) return std::unexpected{Error::invalid_geometry};
  arrival.flight = *canonical;
  const auto destination_owner = hydrate_freedom_flight_document(arrival);
  if (!destination_owner) return std::unexpected{Error::invalid_geometry};
  const SystemPositionMetres arrival_point{
      ephemeris->position.x + arrival.flight.position_metres.x,
      ephemeris->position.y + arrival.flight.position_metres.y,
      ephemeris->position.z + arrival.flight.position_metres.z};
  const auto arrival_volume = clearance(system, tick, arrival_point,
                                        hull_radius, arrival.model.atmosphere);
  if (!arrival_volume) return std::unexpected{arrival_volume.error()};
  if (!arrival_volume->clear()) return std::unexpected{Error::unclear_arrival};
  return FrozenFreedomPlanetJumpArrival{kFreedomPlanetJumpVersion,
                                        request,
                                        *checksum,
                                        tick,
                                        std::move(arrival),
                                        source_point,
                                        arrival_point,
                                        *departure_volume,
                                        *arrival_volume};
}

auto validate_frozen_freedom_planet_jump_arrival(
    const FrozenFreedomPlanetJumpArrival& value)
    -> std::expected<void, FreedomPlanetJumpError> {
  const auto expected = freeze_freedom_planet_jump_arrival(value.request);
  if (!expected || *expected != value)
    return std::unexpected{Error::invalid_frozen_solution};
  return {};
}
} // namespace apsis_drift
