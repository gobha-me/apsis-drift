#include "apsis_drift/freedom_local_jump.hpp"

#include "apsis_drift/intersystem_jump.hpp"
#include "apsis_drift/station_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace apsis_drift {
namespace {
using Error = FreedomLocalBodyJumpError;
auto norm(RigidVector3 v) -> double {
  return std::hypot(v.x, v.y, v.z);
}
auto distance(SystemPositionMetres a, SystemPositionMetres b) -> double {
  return norm({a.x - b.x, a.y - b.y, a.z - b.z});
}
auto clearance(const PhysicalMoonDescriptor& moon, SimulationTick tick,
               SystemPositionMetres point, double hull_radius)
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
                               : 0.0);
    const bool blocked = distance(point, found->position) <= radius;
    if (secondary.node.id.kind == BodyKind::moon)
      result.intersects_moon = blocked;
    else if (secondary.node.id.kind == BodyKind::minor)
      result.intersects_minor = blocked;
  }
  return result;
}
auto system_pose(const FreedomFlightSaveDocument& source,
                 const PhysicalMoonDescriptor& moon)
    -> std::expected<RigidBodyState, Error> {
  const RigidBodyWorldContext context =
      source.moon ? RigidBodyWorldContext{moon}
                  : RigidBodyWorldContext{moon.catalog.owner};
  const auto pose = reframe_rigid_body(
      context, source.flight,
      {{RigidFrameKind::system_inertial, moon.catalog.owner.catalog.id, {}, {}},
       source.flight.tick});
  if (!pose) return std::unexpected{Error::invalid_source};
  return *pose;
}
} // namespace

auto freeze_local_body_jump_arrival(const FreedomLocalBodyJumpRequest& request)
    -> std::expected<FrozenLocalBodyJumpArrival, FreedomLocalBodyJumpError> {
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
    arrival.world = FreedomActiveWorldSelection{1, system->catalog.id, home};
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
