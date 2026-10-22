#include "apsis_drift/freedom_local_jump.hpp"

#include "apsis_drift/freedom_system_network.hpp"
#include "apsis_drift/intersystem_jump.hpp"
#include "apsis_drift/station_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace apsis_drift {
namespace {
using Error = FreedomLocalBodyJumpError;
auto norm(RigidVector3 v) -> double {
  return std::hypot(v.x, v.y, v.z);
}
auto distance(SystemPositionMetres a, SystemPositionMetres b) -> double {
  return norm({a.x - b.x, a.y - b.y, a.z - b.z});
}
template <class Owner>
auto clearance(const Owner& moon, SimulationTick tick,
               SystemPositionMetres point, double hull_radius,
               bool minor_shell = false)
    -> std::expected<FreedomLocalBodyClearance, Error> {
  const auto primary = assess_freedom_arrival_volume(moon.catalog.owner, tick,
                                                     point, hull_radius);
  const auto hierarchy = make_body_hierarchy(moon.catalog);
  if (!primary || !hierarchy) return std::unexpected{Error::invalid_geometry};
  const auto bodies = resolve_body_ephemerides(*hierarchy, {tick, 0});
  if (!bodies) return std::unexpected{Error::invalid_geometry};
  FreedomLocalBodyClearance result{*primary};
  for (const auto& secondary : moon.catalog.bodies) {
    const auto found =
        std::ranges::find(*bodies, secondary.node.id, &BodyEphemeris::body);
    if (found == bodies->end()) return std::unexpected{Error::invalid_geometry};
    // Conservative bounded moon relief, never a sampled safe landing site.
    const double radius = secondary.radius_metres + hull_radius +
                          (secondary.node.id.kind == BodyKind::moon
                               ? kMoonTerrainMaximumReliefMetres
                               : (minor_shell ? 1000.0 : 0.0));
    const bool blocked = distance(point, found->position) <= radius;
    if (secondary.node.id.kind == BodyKind::moon)
      result.intersects_moon = blocked;
    else if (secondary.node.id.kind == BodyKind::minor)
      result.intersects_minor = blocked;
  }
  return result;
}
template <class Owner>
auto system_pose(const FreedomFlightSaveDocument& source, const Owner& moon)
    -> std::expected<RigidBodyState, Error> {
  const RigidBodyWorldContext context =
      (source.moon || source.minor) ? RigidBodyWorldContext{moon}
                                    : RigidBodyWorldContext{moon.catalog.owner};
  const auto pose = reframe_rigid_body(
      context, source.flight,
      {{RigidFrameKind::system_inertial, moon.catalog.owner.catalog.id, {}, {}},
       source.flight.tick});
  if (!pose) return std::unexpected{Error::invalid_source};
  return *pose;
}
template <class Owner>
auto apply_recovery(const FreedomLocalBodyJumpRequest& request,
                    FrozenLocalBodyJumpArrival frozen, const Owner& owner,
                    FreedomJumpRecoveryKind kind)
    -> std::expected<FrozenLocalBodyJumpArrival, Error> {
  if (!request.recovery) return std::unexpected{Error::unsupported_policy};
  const auto& source = request.source;
  const BodyTarget source_target =
      source.moon    ? source.moon->target
      : source.minor ? source.minor->target
                     : BodyTarget{owner.catalog.owner.catalog.id,
                                  body_id(source.origin.recipe.home_planet)};
  const auto recovery = freeze_freedom_jump_uncertainty(
      source.origin.recipe.universe_seed, source.origin.state.craft,
      source.flight.craft, source.origin.lineage, frozen.source_checksum, kind,
      source.flight.tick, source_target, request.destination, request.attempt,
      *request.recovery);
  if (!recovery) return std::unexpected{Error::invalid_source};
  if (recovery->extra_radius_millimetres) {
    const auto craft = resolve_craft_frame(source.flight.craft);
    if (!craft) return std::unexpected{Error::invalid_owner};
    const auto& p = craft->properties;
    const double hull_radius =
        norm({static_cast<double>(std::max(std::abs(p.hull_min_mm[0]),
                                           std::abs(p.hull_max_mm[0]))) /
                  1000,
              static_cast<double>(std::max(std::abs(p.hull_min_mm[1]),
                                           std::abs(p.hull_max_mm[1]))) /
                  1000,
              static_cast<double>(std::max(std::abs(p.hull_min_mm[2]),
                                           std::abs(p.hull_max_mm[2]))) /
                  1000});
    const bool minor_shell = kind == FreedomJumpRecoveryKind::minor;
    const auto envelope = clearance(
        owner, frozen.arrival_tick, frozen.arrival_system_position,
        hull_radius +
            static_cast<double>(recovery->extra_radius_millimetres) / 1000,
        minor_shell);
    if (!envelope) return std::unexpected{envelope.error()};
    if (!envelope->clear()) return std::unexpected{Error::unclear_arrival};
    auto& point = frozen.arrival.flight.position_metres;
    point.x += recovery->offset_metres.x;
    point.y += recovery->offset_metres.y;
    point.z += recovery->offset_metres.z;
    const RigidBodyWorldContext context =
        (frozen.arrival.moon || frozen.arrival.minor)
            ? RigidBodyWorldContext{owner}
            : RigidBodyWorldContext{owner.catalog.owner};
    const auto canonical =
        canonicalize_rigid_body_state(context, frozen.arrival.flight);
    if (!canonical) return std::unexpected{Error::invalid_geometry};
    frozen.arrival.flight = *canonical;
    if (!hydrate_freedom_flight_document(frozen.arrival))
      return std::unexpected{Error::invalid_geometry};
    const auto pose = system_pose(frozen.arrival, owner);
    if (!pose) return std::unexpected{pose.error()};
    frozen.arrival_system_position = {pose->position_metres.x,
                                      pose->position_metres.y,
                                      pose->position_metres.z};
    const auto actual =
        clearance(owner, frozen.arrival_tick, frozen.arrival_system_position,
                  hull_radius, minor_shell);
    if (!actual) return std::unexpected{actual.error()};
    if (!actual->clear()) return std::unexpected{Error::unclear_arrival};
    frozen.arrival_volume = *envelope;
  }
  frozen.version = request.version;
  frozen.request = request;
  frozen.recovery = *recovery;
  return frozen;
}
} // namespace

namespace {
auto freeze_minor_local_body_jump_arrival(
    const FreedomLocalBodyJumpRequest& request)
    -> std::expected<FrozenLocalBodyJumpArrival, FreedomLocalBodyJumpError> {
  if (request.version != kFreedomMinorLocalBodyJumpVersion)
    return std::unexpected{Error::unsupported_policy};
  if (!request.attempt ||
      request.attempt == std::numeric_limits<std::uint64_t>::max())
    return std::unexpected{Error::invalid_attempt};
  const auto& source = request.source;
  if (source.flight.tick >
      std::numeric_limits<SimulationTick>::max() - kJumpTransitTicks - 3)
    return std::unexpected{Error::tick_overflow};
  const auto hydrated = hydrate_freedom_flight_document(source);
  if (!hydrated) return std::unexpected{Error::invalid_source};
  const auto system =
      generate_physical_origin_system(source.origin.recipe.universe_seed, 2);
  if (!system || hydrated->system != *system ||
      source.flight.craft !=
          CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion})
    return std::unexpected{Error::invalid_owner};
  const auto catalog = generate_secondary_body_catalog(*system);
  if (!catalog) return std::unexpected{Error::invalid_owner};
  const auto minor =
      generate_physical_minor_descriptor(*catalog, catalog->bodies[1].node.id);
  if (!minor) return std::unexpected{Error::invalid_owner};
  const auto home = source.origin.recipe.home_planet;
  const BodyTarget home_target{system->catalog.id, body_id(home)};
  const BodyTarget minor_target{system->catalog.id, minor->body};
  const bool outbound = !source.minor;
  if (request.destination != (outbound ? minor_target : home_target))
    return std::unexpected{Error::invalid_destination};
  const auto relative = relative_freedom_flight_state(source, *system);
  if (!relative || (outbound && relative->frame.planet != home) ||
      (!outbound && source.minor->target != minor_target))
    return std::unexpected{Error::invalid_source};
  if (outbound && request.home_model != source.model)
    return std::unexpected{Error::invalid_home_model};
  const RigidBodyWorldContext context =
      outbound ? RigidBodyWorldContext{*system} : RigidBodyWorldContext{*minor};
  const auto checksum = rigid_body_state_checksum(context, source.flight);
  const auto departure = system_pose(source, *minor);
  const auto craft = resolve_craft_frame(source.flight.craft);
  if (!checksum || !departure || !craft)
    return std::unexpected{Error::invalid_source};
  RigidVector3 hull;
  const auto& properties = craft->properties;
  hull.x = static_cast<double>(std::max(std::abs(properties.hull_min_mm[0]),
                                        std::abs(properties.hull_max_mm[0]))) /
           1000;
  hull.y = static_cast<double>(std::max(std::abs(properties.hull_min_mm[1]),
                                        std::abs(properties.hull_max_mm[1]))) /
           1000;
  hull.z = static_cast<double>(std::max(std::abs(properties.hull_min_mm[2]),
                                        std::abs(properties.hull_max_mm[2]))) /
           1000;
  const double hull_radius = norm(hull);
  const SystemPositionMetres departure_point{departure->position_metres.x,
                                             departure->position_metres.y,
                                             departure->position_metres.z};
  const auto departure_volume =
      clearance(*minor, source.flight.tick, departure_point, hull_radius, true);
  if (!departure_volume) return std::unexpected{departure_volume.error()};
  if (!departure_volume->clear())
    return std::unexpected{Error::unclear_departure};
  const auto tick = source.flight.tick + kJumpTransitTicks;
  auto arrival = source;
  arrival.origin.state.tick = tick;
  if (outbound) {
    auto fixed = source.flight;
    fixed.tick = tick;
    fixed.frame = {
        RigidFrameKind::body_fixed, system->catalog.id, {}, {}, minor->body};
    fixed.position_metres = {
        0, minor->radius_metres + kFreedomLocalMinorArrivalClearanceMetres, 0};
    fixed.orientation = {};
    fixed.linear_velocity_metres_per_second = {};
    fixed.angular_velocity_radians_per_second = {};
    const auto pose =
        reframe_rigid_body(RigidBodyWorldContext{*minor}, fixed,
                           {{RigidFrameKind::body_relative_inertial,
                             system->catalog.id,
                             {},
                             {},
                             minor->body},
                            tick},
                           *minor);
    if (!pose) return std::unexpected{Error::invalid_geometry};
    arrival.flight = *pose;
    arrival.model = FreedomFlightModel{};
    arrival.model.physical_catalog = 2;
    arrival.model.physical_ephemeris = 2;
    arrival.model.assistance = source.model.assistance;
    arrival.world.reset();
    arrival.minor = FreedomMinorWorldSelection{1, minor_target};
  } else {
    const auto station =
        generate_origin_station(source.origin.recipe.universe_seed);
    const auto geometry = origin_station_geometry(station);
    if (!geometry) return std::unexpected{Error::invalid_geometry};
    const auto port = resolve_origin_port_pose(*system, station, *geometry,
                                               {station.id, 1}, tick);
    if (!port) return std::unexpected{Error::invalid_geometry};
    auto pose = port->planet_relative;
    // A port supplies geometry, not a replacement craft selection. Preserve
    // the actual source frame exactly, as the dock-constraint adapter does.
    pose.craft = source.flight.craft;
    const auto outward = geometry->ports[0].outward_normal;
    pose.position_metres.x += outward.x * kAssistedOriginArrivalStandoffMetres;
    pose.position_metres.y += outward.y * kAssistedOriginArrivalStandoffMetres;
    pose.position_metres.z += outward.z * kAssistedOriginArrivalStandoffMetres;
    const auto canonical =
        canonicalize_rigid_body_state(RigidBodyWorldContext{*system}, pose);
    if (!canonical) return std::unexpected{Error::invalid_geometry};
    arrival.flight = *canonical;
    arrival.model = request.home_model;
    arrival.minor.reset();
    arrival.world = FreedomActiveWorldSelection{source.origin.network ? 2U : 1U,
                                                system->catalog.id, home};
  }
  const auto destination_owner = hydrate_freedom_flight_document(arrival);
  if (!destination_owner)
    return std::unexpected{outbound ? Error::invalid_geometry
                                    : Error::invalid_home_model};
  const auto destination = system_pose(arrival, *minor);
  if (!destination) return std::unexpected{Error::invalid_geometry};
  const SystemPositionMetres arrival_point{destination->position_metres.x,
                                           destination->position_metres.y,
                                           destination->position_metres.z};
  const auto arrival_volume =
      clearance(*minor, tick, arrival_point, hull_radius, true);
  if (!arrival_volume) return std::unexpected{arrival_volume.error()};
  if (!arrival_volume->clear()) return std::unexpected{Error::unclear_arrival};
  return FrozenLocalBodyJumpArrival{2,
                                    request,
                                    *checksum,
                                    tick,
                                    std::move(arrival),
                                    departure_point,
                                    arrival_point,
                                    *departure_volume,
                                    *arrival_volume};
}
} // namespace
namespace {
auto foreign_clearance(const PhysicalMoonDescriptor& owner, SimulationTick tick,
                       SystemPositionMetres point, double radius,
                       AtmosphericFlightRecipe atmosphere)
    -> std::expected<FreedomLocalBodyClearance, Error> {
  auto result = clearance(owner, tick, point, radius);
  if (!result) return std::unexpected{result.error()};
  // Same conservative planet envelopes as ordinary-planet travel. A new
  // cross-system endpoint must not hide inside an atmosphere or dry relief.
  for (const auto& planet : owner.catalog.owner.catalog.planets) {
    const auto profile =
        resolve_atmospheric_flight_profile(planet.descriptor, atmosphere);
    const auto pose = resolve_planet_ephemeris(owner.catalog.owner,
                                               planet.descriptor.id, {tick, 0});
    if (!profile || !pose ||
        !std::isfinite(profile->space_boundary_altitude_metres) ||
        profile->space_boundary_altitude_metres < 0)
      return std::unexpected{Error::invalid_geometry};
    const double bound =
        static_cast<double>(planet.descriptor.radius.value) * 1000 +
        std::max(17070.0, profile->space_boundary_altitude_metres) + radius;
    const auto separation = distance(point, pose->position);
    if (!std::isfinite(bound) || !std::isfinite(separation))
      return std::unexpected{Error::invalid_geometry};
    if (separation <= bound &&
        std::ranges::find(result->primary.intersecting_planets,
                          planet.descriptor.id) ==
            result->primary.intersecting_planets.end())
      result->primary.intersecting_planets.push_back(planet.descriptor.id);
  }
  return result;
}
auto foreign_leg(const FreedomLocalBodyJumpRequest& request,
                 SystemId destination, SystemPositionMetres from,
                 SystemPositionMetres to, double envelope = 0)
    -> std::expected<void, Error> {
  if (!request.source.origin.network)
    return std::unexpected{Error::invalid_owner};
  const auto network = generate_freedom_system_network(
      request.source.origin.recipe.universe_seed,
      request.source.origin.network->version);
  if (!network) return std::unexpected{Error::invalid_owner};
  UniversePositionMetres delta{};
  if (request.source.flight.frame.system != destination) {
    const auto route = resolve_freedom_system_network_route(
        *network, request.source.flight.frame.system, destination);
    if (!route) return std::unexpected{Error::invalid_destination};
    delta = route->displacement; // Already metres, not light-seconds.
  }
  const RigidVector3 leg{static_cast<double>(delta.x) + (to.x - from.x),
                         static_cast<double>(delta.y) + (to.y - from.y),
                         static_cast<double>(delta.z) + (to.z - from.z)};
  const double metres = norm(leg);
  if (!std::isfinite(leg.x) || !std::isfinite(leg.y) || !std::isfinite(leg.z) ||
      !std::isfinite(metres) || metres <= 0 || !std::isfinite(envelope) ||
      envelope < 0)
    return std::unexpected{Error::invalid_geometry};
  const auto assessment = assess_freedom_jump_distance(
      metres + envelope, IntersystemArrivalQuality::aligned);
  if (!assessment) return std::unexpected{Error::invalid_geometry};
  if (assessment->reach != FreedomJumpReach::rated)
    return std::unexpected{Error::beyond_qualified_policy};
  return {};
}
auto freeze_foreign_moon_jump(const FreedomLocalBodyJumpRequest& request)
    -> std::expected<FrozenLocalBodyJumpArrival, Error> {
  const bool recovering =
      request.version == kFreedomRecoveryForeignMoonJumpVersion;
  if (request.recovery.has_value() != recovering)
    return std::unexpected{Error::unsupported_policy};
  if (!request.attempt ||
      request.attempt == std::numeric_limits<std::uint64_t>::max())
    return std::unexpected{Error::invalid_attempt};
  const auto& source = request.source;
  if (source.flight.tick >
      std::numeric_limits<SimulationTick>::max() - kJumpTransitTicks - 3)
    return std::unexpected{Error::tick_overflow};
  const auto hydrated = hydrate_freedom_flight_document(source);
  const auto origin =
      generate_physical_origin_system(source.origin.recipe.universe_seed, 2);
  if (!hydrated || !origin || !source.origin.network || source.minor ||
      hydrated->system.catalog.id == origin->catalog.id ||
      source.flight.craft !=
          CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion})
    return std::unexpected{Error::invalid_source};
  const auto network = generate_freedom_system_network(
      source.origin.recipe.universe_seed, source.origin.network->version);
  if (!network) return std::unexpected{Error::invalid_owner};
  const auto actual =
      resolve_freedom_network_system(*network, hydrated->system.catalog.id);
  if (!actual || *actual != hydrated->system)
    return std::unexpected{Error::invalid_owner};
  const auto catalog = generate_secondary_body_catalog(*actual);
  const auto origin_catalog = generate_secondary_body_catalog(*origin);
  if (!catalog || !origin_catalog) return std::unexpected{Error::invalid_owner};
  const auto moon =
      generate_physical_moon_descriptor(*catalog, catalog->bodies[0].node.id);
  const auto origin_moon = generate_physical_moon_descriptor(
      *origin_catalog, origin_catalog->bodies[0].node.id);
  if (!moon || !origin_moon) return std::unexpected{Error::invalid_owner};
  const bool outbound = !source.moon;
  if (request.home_model.hold.target)
    return std::unexpected{Error::invalid_home_model};
  const BodyTarget moon_target{actual->catalog.id, moon->body};
  const BodyTarget parent_target{actual->catalog.id, moon->parent};
  const BodyTarget home_target{origin->catalog.id,
                               body_id(source.origin.recipe.home_planet)};
  if (request.destination != (outbound ? moon_target : home_target))
    return std::unexpected{Error::invalid_destination};
  const auto relative = relative_freedom_flight_state(source, *actual);
  if (!relative ||
      (outbound && (!source.world || !relative->frame.planet ||
                    body_id(*relative->frame.planet) != moon->parent)) ||
      (!outbound && (source.moon->version != kFreedomForeignMoonWorldVersion ||
                     source.moon->target != moon_target)))
    return std::unexpected{Error::invalid_source};
  auto expedition = source.model;
  expedition.hold = {};
  if (outbound && request.home_model != expedition)
    return std::unexpected{Error::invalid_home_model};
  const auto context =
      outbound ? RigidBodyWorldContext{*actual} : RigidBodyWorldContext{*moon};
  const auto checksum = rigid_body_state_checksum(context, source.flight);
  const auto departure = system_pose(source, *moon);
  const auto craft = resolve_craft_frame(source.flight.craft);
  if (!checksum || !departure || !craft)
    return std::unexpected{Error::invalid_source};
  const auto& p = craft->properties;
  const double hull_radius =
      norm({static_cast<double>(std::max(std::abs(p.hull_min_mm[0]),
                                         std::abs(p.hull_max_mm[0]))) /
                1000,
            static_cast<double>(std::max(std::abs(p.hull_min_mm[1]),
                                         std::abs(p.hull_max_mm[1]))) /
                1000,
            static_cast<double>(std::max(std::abs(p.hull_min_mm[2]),
                                         std::abs(p.hull_max_mm[2]))) /
                1000});
  const SystemPositionMetres from{departure->position_metres.x,
                                  departure->position_metres.y,
                                  departure->position_metres.z};
  const auto departure_volume =
      foreign_clearance(*moon, source.flight.tick, from, hull_radius,
                        request.home_model.atmosphere);
  if (!departure_volume) return std::unexpected{departure_volume.error()};
  if (!departure_volume->clear())
    return std::unexpected{Error::unclear_departure};
  const auto tick = source.flight.tick + kJumpTransitTicks;
  auto arrival = source;
  arrival.origin.state.tick = tick;
  if (outbound) {
    auto fixed = source.flight;
    fixed.tick = tick;
    fixed.frame = {
        RigidFrameKind::body_fixed, actual->catalog.id, {}, {}, moon->body};
    fixed.position_metres = {0,
                             moon->radius_metres +
                                 kMoonTerrainMaximumReliefMetres +
                                 kFreedomLocalMoonArrivalClearanceMetres,
                             0};
    fixed.orientation = {};
    fixed.linear_velocity_metres_per_second = {};
    fixed.angular_velocity_radians_per_second = {};
    const auto pose =
        reframe_rigid_body(RigidBodyWorldContext{*moon}, fixed,
                           {{RigidFrameKind::body_relative_inertial,
                             actual->catalog.id,
                             {},
                             {},
                             moon->body},
                            tick},
                           *moon);
    if (!pose) return std::unexpected{Error::invalid_geometry};
    arrival.flight = *pose;
    arrival.model = {};
    arrival.model.physical_catalog = 2;
    arrival.model.physical_ephemeris = 2;
    arrival.model.assistance = source.model.assistance;
    arrival.world.reset();
    arrival.moon =
        FreedomMoonWorldSelection{kFreedomForeignMoonWorldVersion, moon_target};
  } else {
    const auto station =
        generate_origin_station(source.origin.recipe.universe_seed);
    const auto geometry = origin_station_geometry(station);
    if (!geometry) return std::unexpected{Error::invalid_geometry};
    const auto port = resolve_origin_port_pose(*origin, station, *geometry,
                                               {station.id, 1}, tick);
    if (!port) return std::unexpected{Error::invalid_geometry};
    auto pose = port->planet_relative;
    pose.craft = source.flight.craft;
    const auto outward = geometry->ports[0].outward_normal;
    pose.position_metres.x += outward.x * kAssistedOriginArrivalStandoffMetres;
    pose.position_metres.y += outward.y * kAssistedOriginArrivalStandoffMetres;
    pose.position_metres.z += outward.z * kAssistedOriginArrivalStandoffMetres;
    const auto canonical =
        canonicalize_rigid_body_state(RigidBodyWorldContext{*origin}, pose);
    if (!canonical) return std::unexpected{Error::invalid_geometry};
    arrival.flight = *canonical;
    arrival.model = request.home_model;
    arrival.moon.reset();
    arrival.world = FreedomActiveWorldSelection{
        2, origin->catalog.id, source.origin.recipe.home_planet};
  }
  if (!hydrate_freedom_flight_document(arrival))
    return std::unexpected{outbound ? Error::invalid_geometry
                                    : Error::invalid_home_model};
  const auto& destination_owner = outbound ? *moon : *origin_moon;
  const auto destination_pose = system_pose(arrival, destination_owner);
  if (!destination_pose) return std::unexpected{Error::invalid_geometry};
  SystemPositionMetres to{destination_pose->position_metres.x,
                          destination_pose->position_metres.y,
                          destination_pose->position_metres.z};
  const auto initial_volume = foreign_clearance(
      destination_owner, tick, to, hull_radius, request.home_model.atmosphere);
  if (!initial_volume) return std::unexpected{initial_volume.error()};
  if (!initial_volume->clear()) return std::unexpected{Error::unclear_arrival};
  auto volume = *initial_volume;
  std::optional<FrozenFreedomJumpUncertainty> recovery;
  if (recovering) {
    const auto sample = freeze_freedom_foreign_moon_uncertainty(
        source.origin.recipe.universe_seed, source.origin.state.craft,
        source.flight.craft, source.origin.lineage, *checksum,
        source.flight.tick, outbound ? parent_target : moon_target,
        request.destination, request.attempt, *request.recovery);
    if (!sample) return std::unexpected{Error::invalid_source};
    const double spread =
        static_cast<double>(sample->extra_radius_millimetres) / 1000;
    const auto range =
        foreign_leg(request, arrival.flight.frame.system, from, to, spread);
    if (!range) return std::unexpected{range.error()};
    const auto envelope =
        foreign_clearance(destination_owner, tick, to, hull_radius + spread,
                          request.home_model.atmosphere);
    if (!envelope) return std::unexpected{envelope.error()};
    if (!envelope->clear()) return std::unexpected{Error::unclear_arrival};
    auto& point = arrival.flight.position_metres;
    point.x += sample->offset_metres.x;
    point.y += sample->offset_metres.y;
    point.z += sample->offset_metres.z;
    const auto destination_context = outbound ? RigidBodyWorldContext{*moon}
                                              : RigidBodyWorldContext{*origin};
    const auto canonical =
        canonicalize_rigid_body_state(destination_context, arrival.flight);
    if (!canonical) return std::unexpected{Error::invalid_geometry};
    arrival.flight = *canonical;
    if (!hydrate_freedom_flight_document(arrival))
      return std::unexpected{Error::invalid_geometry};
    const auto offset_pose = system_pose(arrival, destination_owner);
    if (!offset_pose) return std::unexpected{Error::invalid_geometry};
    to = {offset_pose->position_metres.x, offset_pose->position_metres.y,
          offset_pose->position_metres.z};
    const auto actual_volume =
        foreign_clearance(destination_owner, tick, to, hull_radius,
                          request.home_model.atmosphere);
    if (!actual_volume) return std::unexpected{actual_volume.error()};
    if (!actual_volume->clear()) return std::unexpected{Error::unclear_arrival};
    volume = *envelope;
    recovery = *sample;
  }
  const auto range =
      foreign_leg(request, arrival.flight.frame.system, from, to);
  if (!range) return std::unexpected{range.error()};
  return FrozenLocalBodyJumpArrival{
      request.version,   request, *checksum, tick, std::move(arrival), from, to,
      *departure_volume, volume,  recovery};
}
} // namespace

auto freeze_local_body_jump_arrival(const FreedomLocalBodyJumpRequest& request)
    -> std::expected<FrozenLocalBodyJumpArrival, FreedomLocalBodyJumpError> {
  if (request.version == kFreedomForeignMoonJumpVersion ||
      request.version == kFreedomRecoveryForeignMoonJumpVersion)
    return freeze_foreign_moon_jump(request);
  if (request.version == kFreedomRecoveryLocalBodyJumpVersion ||
      request.version == kFreedomRecoveryMinorLocalBodyJumpVersion) {
    if (!request.recovery) return std::unexpected{Error::unsupported_policy};
    const bool minor_route =
        request.version == kFreedomRecoveryMinorLocalBodyJumpVersion;
    auto baseline = request;
    baseline.version = minor_route ? kFreedomMinorLocalBodyJumpVersion
                                   : kFreedomLocalBodyJumpVersion;
    baseline.recovery.reset();
    auto frozen = freeze_local_body_jump_arrival(baseline);
    if (!frozen) return std::unexpected{frozen.error()};
    const auto system = generate_physical_origin_system(
        request.source.origin.recipe.universe_seed, 2);
    if (!system) return std::unexpected{Error::invalid_owner};
    const auto catalog = generate_secondary_body_catalog(*system);
    if (!catalog) return std::unexpected{Error::invalid_owner};
    if (minor_route) {
      const auto owner = generate_physical_minor_descriptor(
          *catalog, catalog->bodies[1].node.id);
      if (!owner) return std::unexpected{Error::invalid_owner};
      return apply_recovery(request, std::move(*frozen), *owner,
                            FreedomJumpRecoveryKind::minor);
    }
    const auto owner =
        generate_physical_moon_descriptor(*catalog, catalog->bodies[0].node.id);
    if (!owner) return std::unexpected{Error::invalid_owner};
    return apply_recovery(request, std::move(*frozen), *owner,
                          FreedomJumpRecoveryKind::moon);
  }
  if (request.recovery) return std::unexpected{Error::unsupported_policy};
  if (request.version == kFreedomMinorLocalBodyJumpVersion)
    return freeze_minor_local_body_jump_arrival(request);
  if (request.version != kFreedomLocalBodyJumpVersion)
    return std::unexpected{Error::unsupported_policy};
  if (!request.attempt ||
      request.attempt == std::numeric_limits<std::uint64_t>::max())
    return std::unexpected{Error::invalid_attempt};
  const auto& source = request.source;
  if (source.flight.tick >
      std::numeric_limits<SimulationTick>::max() - kJumpTransitTicks - 3)
    return std::unexpected{Error::tick_overflow};
  const auto hydrated = hydrate_freedom_flight_document(source);
  if (!hydrated) return std::unexpected{Error::invalid_source};
  const auto system =
      generate_physical_origin_system(source.origin.recipe.universe_seed, 2);
  if (!system || hydrated->system != *system ||
      source.flight.craft !=
          CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion})
    return std::unexpected{Error::invalid_owner};
  const auto catalog = generate_secondary_body_catalog(*system);
  if (!catalog) return std::unexpected{Error::invalid_owner};
  const auto moon =
      generate_physical_moon_descriptor(*catalog, catalog->bodies[0].node.id);
  if (!moon) return std::unexpected{Error::invalid_owner};
  const auto home = source.origin.recipe.home_planet;
  const BodyTarget home_target{system->catalog.id, body_id(home)};
  const BodyTarget moon_target{system->catalog.id, moon->body};
  const bool outbound = !source.moon;
  if (request.destination != (outbound ? moon_target : home_target))
    return std::unexpected{Error::invalid_destination};
  const auto relative = relative_freedom_flight_state(source, *system);
  if (!relative || (outbound && relative->frame.planet != home) ||
      (!outbound && source.moon->target != moon_target))
    return std::unexpected{Error::invalid_source};
  if (outbound && request.home_model != source.model)
    return std::unexpected{Error::invalid_home_model};
  const RigidBodyWorldContext context =
      outbound ? RigidBodyWorldContext{*system} : RigidBodyWorldContext{*moon};
  const auto checksum = rigid_body_state_checksum(context, source.flight);
  const auto departure = system_pose(source, *moon);
  const auto craft = resolve_craft_frame(source.flight.craft);
  if (!checksum || !departure || !craft)
    return std::unexpected{Error::invalid_source};
  RigidVector3 hull;
  const auto& properties = craft->properties;
  hull.x = static_cast<double>(std::max(std::abs(properties.hull_min_mm[0]),
                                        std::abs(properties.hull_max_mm[0]))) /
           1000;
  hull.y = static_cast<double>(std::max(std::abs(properties.hull_min_mm[1]),
                                        std::abs(properties.hull_max_mm[1]))) /
           1000;
  hull.z = static_cast<double>(std::max(std::abs(properties.hull_min_mm[2]),
                                        std::abs(properties.hull_max_mm[2]))) /
           1000;
  const double hull_radius = norm(hull);
  const SystemPositionMetres departure_point{departure->position_metres.x,
                                             departure->position_metres.y,
                                             departure->position_metres.z};
  const auto departure_volume =
      clearance(*moon, source.flight.tick, departure_point, hull_radius);
  if (!departure_volume) return std::unexpected{departure_volume.error()};
  if (!departure_volume->clear())
    return std::unexpected{Error::unclear_departure};
  const auto tick = source.flight.tick + kJumpTransitTicks;
  auto arrival = source;
  arrival.origin.state.tick = tick;
  if (outbound) {
    auto fixed = source.flight;
    fixed.tick = tick;
    fixed.frame = {
        RigidFrameKind::body_fixed, system->catalog.id, {}, {}, moon->body};
    fixed.position_metres = {0,
                             moon->radius_metres +
                                 kMoonTerrainMaximumReliefMetres +
                                 kFreedomLocalMoonArrivalClearanceMetres,
                             0};
    fixed.orientation = {};
    fixed.linear_velocity_metres_per_second = {};
    fixed.angular_velocity_radians_per_second = {};
    const auto pose =
        reframe_rigid_body(RigidBodyWorldContext{*moon}, fixed,
                           {{RigidFrameKind::body_relative_inertial,
                             system->catalog.id,
                             {},
                             {},
                             moon->body},
                            tick},
                           *moon);
    if (!pose) return std::unexpected{Error::invalid_geometry};
    arrival.flight = *pose;
    arrival.model = FreedomFlightModel{};
    arrival.model.physical_catalog = 2;
    arrival.model.physical_ephemeris = 2;
    arrival.model.assistance = source.model.assistance;
    arrival.world.reset();
    arrival.moon = FreedomMoonWorldSelection{1, moon_target};
  } else {
    const auto station =
        generate_origin_station(source.origin.recipe.universe_seed);
    const auto geometry = origin_station_geometry(station);
    if (!geometry) return std::unexpected{Error::invalid_geometry};
    const auto port = resolve_origin_port_pose(*system, station, *geometry,
                                               {station.id, 1}, tick);
    if (!port) return std::unexpected{Error::invalid_geometry};
    auto pose = port->planet_relative;
    // A port supplies geometry, not a replacement craft selection. Preserve
    // the actual source frame exactly, as the dock-constraint adapter does.
    pose.craft = source.flight.craft;
    const auto outward = geometry->ports[0].outward_normal;
    pose.position_metres.x += outward.x * kAssistedOriginArrivalStandoffMetres;
    pose.position_metres.y += outward.y * kAssistedOriginArrivalStandoffMetres;
    pose.position_metres.z += outward.z * kAssistedOriginArrivalStandoffMetres;
    const auto canonical =
        canonicalize_rigid_body_state(RigidBodyWorldContext{*system}, pose);
    if (!canonical) return std::unexpected{Error::invalid_geometry};
    arrival.flight = *canonical;
    arrival.model = request.home_model;
    arrival.moon.reset();
    arrival.world = FreedomActiveWorldSelection{source.origin.network ? 2U : 1U,
                                                system->catalog.id, home};
  }
  const auto destination_owner = hydrate_freedom_flight_document(arrival);
  if (!destination_owner)
    return std::unexpected{outbound ? Error::invalid_geometry
                                    : Error::invalid_home_model};
  const auto destination = system_pose(arrival, *moon);
  if (!destination) return std::unexpected{Error::invalid_geometry};
  const SystemPositionMetres arrival_point{destination->position_metres.x,
                                           destination->position_metres.y,
                                           destination->position_metres.z};
  const auto arrival_volume =
      clearance(*moon, tick, arrival_point, hull_radius);
  if (!arrival_volume) return std::unexpected{arrival_volume.error()};
  if (!arrival_volume->clear()) return std::unexpected{Error::unclear_arrival};
  return FrozenLocalBodyJumpArrival{1,
                                    request,
                                    *checksum,
                                    tick,
                                    std::move(arrival),
                                    departure_point,
                                    arrival_point,
                                    *departure_volume,
                                    *arrival_volume};
}
auto validate_frozen_local_body_jump_arrival(
    const FrozenLocalBodyJumpArrival& value)
    -> std::expected<void, FreedomLocalBodyJumpError> {
  const auto expected = freeze_local_body_jump_arrival(value.request);
  if (!expected || *expected != value)
    return std::unexpected{Error::invalid_frozen_solution};
  return {};
}
} // namespace apsis_drift
