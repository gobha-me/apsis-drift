#include "apsis_drift/origin_service_route.hpp"
#include "apsis_drift/atmospheric_flight.hpp"
#include "apsis_drift/intersystem_jump.hpp"
#include "apsis_drift/moon_body_dynamics.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include <algorithm>
#include <array>
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
auto mul(V a, double b) -> V {
  return {a.x * b, a.y * b, a.z * b};
}
auto norm(V a) -> double {
  return std::hypot(a.x, a.y, a.z);
}
auto dot(V a, V b) -> double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto rotate(RigidOrientation q, V v) -> V {
  const V u{q.x, q.y, q.z};
  const auto c = cross(u, v);
  return add(v,
             mul(add(mul(c, q.w), cross(u, c)), 2.0 / (q.w * q.w + dot(u, u))));
}
auto hull_radius(CraftFrameRecipe frame) -> std::expected<double, std::string> {
  const auto f = resolve_craft_frame(frame);
  if (!f) return std::unexpected{"Invalid hull frame"};
  const auto& p = f->properties;
  V h;
  std::array<double*, 3> out{&h.x, &h.y, &h.z};
  for (unsigned i = 0; i < 3; ++i)
    *out[i] =
        std::max(std::abs(double(p.hull_min_mm[i]) - p.center_of_mass_mm[i]),
                 std::abs(double(p.hull_max_mm[i]) - p.center_of_mass_mm[i])) /
        1000.;
  return norm(h);
}
auto primary_clear(const PhysicalLocalSystem& system,
                   const RigidBodyState& inertial, double radius)
    -> std::expected<void, std::string> {
  const SystemPositionMetres point{inertial.position_metres.x,
                                   inertial.position_metres.y,
                                   inertial.position_metres.z};
  const auto primary =
      assess_freedom_arrival_volume(system, inertial.tick, point, radius);
  if (!primary || !primary->clear())
    return std::unexpected{"Service endpoint intersects a primary body"};
  for (const auto& p : system.catalog.planets) {
    const auto e =
        resolve_planet_ephemeris(system, p.descriptor.id, {inertial.tick, 0});
    const auto air = resolve_atmospheric_flight_profile(p.descriptor, {});
    if (!e || !air)
      return std::unexpected{"Service endpoint environment unavailable"};
    const auto separation =
        norm({point.x - e->position.x, point.y - e->position.y,
              point.z - e->position.z});
    if (!std::isfinite(separation) ||
        separation <=
            p.descriptor.radius.value * 1000. +
                std::max(17070., air->space_boundary_altitude_metres) + radius)
      return std::unexpected{
          "Service endpoint intersects terrain/atmosphere envelope"};
  }
  return {};
}
auto moon_occupancy(const PhysicalMoonDescriptor& moon,
                    const RigidBodyState& body)
    -> std::expected<void, std::string> {
  const RigidBodyWorldContext context{moon};
  auto fixed = reframe_rigid_body(
      context, body,
      {{RigidFrameKind::body_fixed, context.system.id, {}, {}, moon.body},
       body.tick},
      moon);
  if (!fixed || fixed->tick >= UINT64_MAX - 1)
    return std::unexpected{"Service occupancy frame unavailable"};
  auto next = *fixed;
  ++next.tick;
  const auto sweep = assess_moon_terrain_impact(moon, *fixed, next, false);
  if (!sweep || sweep->classification != MoonTerrainImpactClass::clear)
    return std::unexpected{
        "Service arrival full hull terrain clearance unavailable"};
  return {};
}
auto moon_goal(const PhysicalMoonDescriptor& moon,
               const OriginServiceBeacon& beacon, SimulationTick tick,
               double height) -> std::expected<RigidBodyState, std::string> {
  auto fixed = beacon.anchor.fixed;
  const auto sampler = MoonTerrainSampler::create(moon);
  if (!sampler) return std::unexpected{"Service terrain owner unavailable"};
  const auto p = fixed.position_metres;
  const auto support = query_moon_contact_surface(*sampler, {p.x, p.y, p.z});
  if (!support) return std::unexpected{"Service beacon facet unavailable"};
  const auto normal = support->triangle.outward_normal;
  fixed.position_metres = add(p, mul({normal.x, normal.y, normal.z}, height));
  fixed.tick = tick;
  fixed.linear_velocity_metres_per_second = {};
  fixed.angular_velocity_radians_per_second = {};
  const RigidBodyWorldContext context{moon};
  const auto canonical = canonicalize_rigid_body_state(context, fixed);
  if (!canonical) return std::unexpected{"Service goal noncanonical"};
  const auto result =
      reframe_rigid_body(context, *canonical,
                         {{RigidFrameKind::body_relative_inertial,
                           context.system.id,
                           {},
                           {},
                           moon.body},
                          tick},
                         moon);
  if (!result) return std::unexpected{"Service goal rotation unavailable"};
  return *result;
}
auto controls(const RigidBodyState& state, const RigidBodyState& goal,
              const RigidBodyState& next, V gravity)
    -> std::expected<VacuumIntent, std::string> {
  const auto frame = resolve_craft_frame(state.craft);
  if (!frame) return std::unexpected{"Service actuator owner unavailable"};
  const auto feed = mul(sub(next.linear_velocity_metres_per_second,
                            goal.linear_velocity_metres_per_second),
                        kSimulationHz);
  const auto acceleration =
      sub(add(add(mul(sub(goal.position_metres, state.position_metres), .008),
                  mul(sub(goal.linear_velocity_metres_per_second,
                          state.linear_velocity_metres_per_second),
                      .18)),
              feed),
          gravity);
  const RigidOrientation inverse{state.orientation.w, -state.orientation.x,
                                 -state.orientation.y, -state.orientation.z};
  V error{};
  for (const V axis : std::array<V, 3>{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}})
    error = add(error, mul(cross(rotate(state.orientation, axis),
                                 rotate(goal.orientation, axis)),
                           .5));
  const auto rate = add(rotate(inverse, mul(error, 1.5)),
                        goal.angular_velocity_radians_per_second);
  const auto force =
      mul(rotate(inverse, acceleration), frame->properties.dry_mass_kg);
  VacuumIntent result;
  result.assistance = true;
  const std::array values{force.x, force.y, force.z};
  const std::array rates{rate.x, rate.y, rate.z};
  const std::array pos{&result.positive_translation.x,
                       &result.positive_translation.y,
                       &result.positive_translation.z};
  const std::array neg{&result.negative_translation.x,
                       &result.negative_translation.y,
                       &result.negative_translation.z};
  const std::array pr{&result.positive_rotation.x, &result.positive_rotation.y,
                      &result.positive_rotation.z};
  const std::array nr{&result.negative_rotation.x, &result.negative_rotation.y,
                      &result.negative_rotation.z};
  for (unsigned i = 0; i < 3; ++i) {
    if (!std::isfinite(values[i]) || !std::isfinite(rates[i]))
      return std::unexpected{"Service controller is nonfinite"};
    *pos[i] = std::clamp(
        values[i] / frame->properties.positive_force_newtons[i], 0., 1.);
    *neg[i] = std::clamp(
        -values[i] / frame->properties.negative_force_newtons[i], 0., 1.);
    const auto r =
        rates[i] /
        (frame->properties.max_angular_rate_milliradians_per_second[i] * .001);
    *pr[i] = std::clamp(r, 0., 1.);
    *nr[i] = std::clamp(-r, 0., 1.);
  }
  return result;
}
auto bill(OriginServiceCraftState& state, const RigidBodyState& body,
          const VacuumActuation& actuation)
    -> std::expected<std::uint64_t, std::string> {
  const auto debit = freedom_propulsion_tick_quanta(actuation);
  if (!debit || *debit > state.resources.flight_quanta)
    return std::unexpected{"Responder propulsion budget exhausted"};
  const auto next = advance_origin_service_resources(
      state.descriptor, state.resources, state.body.tick, *debit);
  if (!next)
    return std::unexpected{"Responder resource ownership/clock refused"};
  state.body = body;
  state.resources = *next;
  return *debit;
}
} // namespace
auto validate_origin_service_beacon(const OriginServiceCraftDescriptor& service,
                                    const PhysicalMoonDescriptor& moon,
                                    const OriginServiceBeacon& target,
                                    SimulationTick tick)
    -> std::expected<void, std::string> {
  const auto identity = derive_freedom_craft_identity(
      service.universe_seed, target.lineage ? target.lineage->generation : 0);
  if (target.version != 1 || !identity || *identity != target.target ||
      (target.lineage &&
       (target.lineage->version != kFreedomCraftLineageVersion ||
        !target.lineage->generation)) ||
      moon.catalog.owner.catalog.id != service.system ||
      !validate_moon_landed_craft(moon, target.anchor, tick))
    return std::unexpected{"Foreign or unsupported service beacon"};
  return {};
}
auto freeze_origin_service_moon_arrival(const PhysicalLocalSystem& system,
                                        const OriginServiceCraftState& service,
                                        const PhysicalMoonDescriptor& moon,
                                        const OriginServiceBeacon& beacon)
    -> std::expected<OriginServiceRouteArrival, std::string> {
  if (!validate_origin_service_craft(system, service) || service.attached ||
      service.body.tick > UINT64_MAX - kJumpTransitTicks - 2 ||
      !service.resources.jump_charges)
    return std::unexpected{"Service departure owner/clock/charge unavailable"};
  const auto station =
      generate_origin_station(service.descriptor.universe_seed);
  const auto geometry = origin_station_geometry(station);
  if (!geometry)
    return std::unexpected{"Service departure station unavailable"};
  const auto port = assess_origin_dock(
      system, station, *geometry, service.descriptor.home_port, service.body);
  if (!port || port->outward_separation_metres < 150 ||
      port->outward_separation_metres > 1000 ||
      port->lateral_separation_metres > .5 || port->alignment_radians > .05 ||
      std::hypot(port->inward_speed_metres_per_second,
                 port->lateral_speed_metres_per_second) > 1.)
    return std::unexpected{
        "Service must actually withdraw in the supported D2 corridor"};
  if (const auto valid = validate_origin_service_beacon(
          service.descriptor, moon, beacon, service.body.tick);
      !valid)
    return std::unexpected{valid.error()};
  const auto at = service.body.tick + kJumpTransitTicks;
  const auto arrival =
      moon_goal(moon, beacon, at, kOriginServiceArrivalHeightMetres);
  if (!arrival) return std::unexpected{arrival.error()};
  const auto radius = hull_radius(service.body.craft);
  if (!radius) return std::unexpected{radius.error()};
  const RigidBodyWorldContext departure{system};
  const auto system_source = reframe_rigid_body(
      departure, service.body,
      {{RigidFrameKind::system_inertial, system.catalog.id, {}, {}},
       service.body.tick});
  const RigidBodyWorldContext context{moon};
  const auto system_arrival = reframe_rigid_body(
      context, *arrival,
      {{RigidFrameKind::system_inertial, system.catalog.id, {}, {}}, at});
  if (!system_source || !system_arrival)
    return std::unexpected{"Service endpoints cannot resolve common system"};
  if (const auto v = primary_clear(system, *system_source, *radius); !v)
    return std::unexpected{v.error()};
  if (const auto v = primary_clear(system, *system_arrival, *radius); !v)
    return std::unexpected{v.error()};
  if (const auto v = moon_occupancy(moon, *arrival); !v)
    return std::unexpected{v.error()};
  const auto checksum = rigid_body_state_checksum(departure, service.body);
  if (!checksum) return std::unexpected{"Service source checksum unavailable"};
  return OriginServiceRouteArrival{
      1,        service.descriptor.id, service.body, *checksum, at,
      *arrival, service.descriptor,    beacon};
}
auto advance_origin_service_moon_route(const PhysicalLocalSystem& system,
                                       const PhysicalMoonDescriptor& moon,
                                       const OriginServiceBeacon& beacon,
                                       OriginServiceCraftState& service,
                                       double height)
    -> std::expected<OriginServiceRouteStep, std::string> {
  if (!std::isfinite(height) || height < kOriginServiceHoverHeightMetres ||
      height > kOriginServiceArrivalHeightMetres || service.attached ||
      service.body.tick >= UINT64_MAX - 2 ||
      !validate_origin_service_craft(system, service, &moon))
    return std::unexpected{"Invalid Moon service flight state/goal"};
  if (const auto v = validate_origin_service_beacon(service.descriptor, moon,
                                                    beacon, service.body.tick);
      !v)
    return std::unexpected{v.error()};
  const auto goal = moon_goal(moon, beacon, service.body.tick, height),
             next = moon_goal(moon, beacon, service.body.tick + 1, height);
  const RigidBodyWorldContext context{moon};
  const auto gravity = evaluate_moon_body_gravity(context, service.body);
  if (!goal || !next || !gravity)
    return std::unexpected{"Moon guidance source unavailable"};
  const auto intent = controls(service.body, *goal, *next,
                               gravity->acceleration_metres_per_second_squared);
  if (!intent) return std::unexpected{intent.error()};
  auto body = service.body;
  const auto actuation = advance_moon_body_dynamics(context, body, *intent);
  if (!actuation) return std::unexpected{"Service Moon dynamics refused"};
  const auto sweep =
      assess_moon_terrain_impact(moon, service.body, body, false);
  if (!sweep || sweep->classification != MoonTerrainImpactClass::clear)
    return std::unexpected{
        "Service powered path terrain clearance unavailable"};
  const auto radius = hull_radius(body.craft);
  if (!radius) return std::unexpected{radius.error()};
  const auto fixed = reframe_rigid_body(
      context, body, {beacon.anchor.fixed.frame, body.tick}, moon);
  const auto before =
      reframe_rigid_body(context, service.body,
                         {beacon.anchor.fixed.frame, service.body.tick}, moon);
  if (!fixed || !before)
    return std::unexpected{"Service relative hull query unavailable"};
  const auto a = sub(before->position_metres,
                     beacon.anchor.fixed.position_metres),
             b = sub(fixed->position_metres,
                     beacon.anchor.fixed.position_metres),
             delta = sub(b, a);
  const auto d = dot(delta, delta);
  const auto t = d > 0 ? std::clamp(-dot(a, delta) / d, 0., 1.) : 0.;
  if (norm(add(a, mul(delta, t))) <= 2 * *radius + 2.)
    return std::unexpected{
        "Service path intersects conservative target hull envelope"};
  auto candidate = service;
  const auto debit = bill(candidate, body, actuation->propulsion);
  if (!debit) return std::unexpected{debit.error()};
  if (!validate_origin_service_craft(system, candidate, &moon))
    return std::unexpected{"Service tick ownership refused"};
  const auto distance = norm(b),
             speed = norm(fixed->linear_velocity_metres_per_second);
  service = candidate;
  return OriginServiceRouteStep{
      actuation->propulsion, *debit, distance, speed,
      distance <= service.descriptor.link_range_metres &&
          speed <= service.descriptor.max_relative_speed_metres_per_second};
}
auto freeze_origin_service_home_arrival(const PhysicalLocalSystem& system,
                                        const OriginServiceCraftState& service,
                                        const PhysicalMoonDescriptor& moon)
    -> std::expected<OriginServiceRouteArrival, std::string> {
  if (service.attached ||
      !validate_origin_service_craft(system, service, &moon) ||
      !service.resources.jump_charges ||
      service.body.tick > UINT64_MAX - kJumpTransitTicks - 2)
    return std::unexpected{"Home service departure unavailable"};
  const auto station =
      generate_origin_station(service.descriptor.universe_seed);
  const auto geometry = origin_station_geometry(station);
  if (!geometry) return std::unexpected{"Service port owner unavailable"};
  const auto at = service.body.tick + kJumpTransitTicks;
  auto target =
      release_origin_port(system, station, *geometry,
                          {geometry->version, service.descriptor.home_port,
                           service.body.craft, at});
  if (!target) return std::unexpected{"Service home constraint unavailable"};
  const auto normal =
      geometry->ports[service.descriptor.home_port.ordinal - 1].outward_normal;
  target->position_metres = add(target->position_metres, mul(normal, 500));
  const RigidBodyWorldContext home{system}, context{moon};
  const auto canonical = canonicalize_rigid_body_state(home, *target);
  if (!canonical) return std::unexpected{"Service home arrival invalid"};
  const auto source = reframe_rigid_body(
      context, service.body,
      {{RigidFrameKind::system_inertial, system.catalog.id, {}, {}},
       service.body.tick});
  const auto destination = reframe_rigid_body(
      home, *canonical,
      {{RigidFrameKind::system_inertial, system.catalog.id, {}, {}}, at});
  const auto radius = hull_radius(service.body.craft);
  if (!source || !destination || !radius)
    return std::unexpected{"Service home endpoint projection refused"};
  if (const auto v = primary_clear(system, *source, *radius); !v)
    return std::unexpected{v.error()};
  if (const auto v = primary_clear(system, *destination, *radius); !v)
    return std::unexpected{v.error()};
  if (const auto v = moon_occupancy(moon, service.body); !v)
    return std::unexpected{v.error()};
  const auto checksum = rigid_body_state_checksum(context, service.body);
  if (!checksum) return std::unexpected{"Service home source checksum refused"};
  return OriginServiceRouteArrival{1,
                                   service.descriptor.id,
                                   service.body,
                                   *checksum,
                                   at,
                                   *canonical,
                                   service.descriptor,
                                   {}};
}
auto advance_origin_service_home_route(const PhysicalLocalSystem& system,
                                       OriginServiceCraftState& service,
                                       double outward_standoff)
    -> std::expected<OriginServiceRouteStep, std::string> {
  if (!std::isfinite(outward_standoff) || outward_standoff < 0 ||
      outward_standoff > 500 || service.attached ||
      !validate_origin_service_craft(system, service) ||
      service.body.tick >= UINT64_MAX - 2)
    return std::unexpected{"Invalid home service route"};
  const auto station =
      generate_origin_station(service.descriptor.universe_seed);
  const auto geometry = origin_station_geometry(station);
  if (!geometry) return std::unexpected{"Service home geometry unavailable"};
  auto goal =
      release_origin_port(system, station, *geometry,
                          {geometry->version, service.descriptor.home_port,
                           service.body.craft, service.body.tick});
  auto next =
      release_origin_port(system, station, *geometry,
                          {geometry->version, service.descriptor.home_port,
                           service.body.craft, service.body.tick + 1});
  const auto gravity = evaluate_central_body_gravity(
      RigidBodyWorldContext{system}, service.body);
  if (!goal || !next || !gravity)
    return std::unexpected{"Service home goal unavailable"};
  const auto outward =
      geometry->ports[service.descriptor.home_port.ordinal - 1].outward_normal;
  goal->position_metres =
      add(goal->position_metres, mul(outward, outward_standoff));
  next->position_metres =
      add(next->position_metres, mul(outward, outward_standoff));
  const auto intent = controls(service.body, *goal, *next,
                               gravity->acceleration_metres_per_second_squared);
  if (!intent) return std::unexpected{intent.error()};
  auto body = service.body;
  const auto actuation = advance_central_body_dynamics(
      RigidBodyWorldContext{system}, body, *intent);
  if (!actuation) return std::unexpected{"Service home powered tick refused"};
  auto candidate = service;
  const auto debit = bill(candidate, body, actuation->propulsion);
  if (!debit) return std::unexpected{debit.error()};
  if (!validate_origin_service_craft(system, candidate))
    return std::unexpected{"Service home tick ownership invalid"};
  const auto distance = norm(sub(body.position_metres, next->position_metres)),
             speed = norm(sub(body.linear_velocity_metres_per_second,
                              next->linear_velocity_metres_per_second));
  service = candidate;
  return OriginServiceRouteStep{actuation->propulsion, *debit, distance, speed,
                                false};
}
} // namespace apsis_drift
