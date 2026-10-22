#include "apsis_drift/origin_service_craft.hpp"

#include "apsis_drift/rigid_frame_handoff.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace apsis_drift {
namespace {
using Error = OriginServiceCraftError;
constexpr auto last_tick = std::numeric_limits<SimulationTick>::max() - 3;
auto authentic_origin(const PhysicalLocalSystem& owner) -> bool {
  if (!owner.origin_universe_seed || !validate_local_system(owner))
    return false;
  const auto expected =
      generate_physical_origin_system(*owner.origin_universe_seed, 2);
  return expected && *expected == owner;
}
auto resources_valid(const OriginServiceCraftDescriptor& d,
                     const OriginServiceCraftResources& r) -> bool {
  return r.craft == d.id && r.tick <= last_tick &&
         r.flight_quanta <= d.flight_capacity_quanta &&
         r.cargo_quanta <= d.cargo_capacity_quanta &&
         r.jump_charges <= d.jump_capacity;
}
// Authentication is an admission operation. These hot ledger operations take
// that admitted descriptor and check its closed scalar recipe without replaying
// the generated catalog at120Hz. They do not establish a new owner.
auto ledger_descriptor_valid(const OriginServiceCraftDescriptor& d) -> bool {
  const OriginServiceCraftDescriptor fixed;
  return d.version == fixed.version && d.id.value != 0 &&
         d.home_port.ordinal == 2 && d.frame == fixed.frame &&
         d.flight_capacity_quanta == fixed.flight_capacity_quanta &&
         d.cargo_capacity_quanta == fixed.cargo_capacity_quanta &&
         d.jump_capacity == fixed.jump_capacity &&
         d.link_range_metres == fixed.link_range_metres &&
         d.max_relative_speed_metres_per_second ==
             fixed.max_relative_speed_metres_per_second &&
         d.transfer_quanta_per_tick == fixed.transfer_quanta_per_tick;
}
auto home_moon(const PhysicalLocalSystem& owner, const BodyTarget& body)
    -> bool {
  const auto catalog = generate_secondary_body_catalog(owner);
  return catalog &&
         body == BodyTarget{owner.catalog.id, catalog->bodies[0].node.id};
}
auto finite_vector(const RigidVector3& v) -> bool {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
auto difference(const RigidVector3& a, const RigidVector3& b) -> RigidVector3 {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto hull_radius(CraftFrameRecipe recipe) -> std::expected<double, Error> {
  const auto frame = resolve_craft_frame(recipe);
  if (!frame) return std::unexpected(Error::invalid_descriptor);
  const auto& p = frame->properties;
  double result{};
  for (unsigned i = 0; i < 8; ++i) {
    const auto x =
        (static_cast<double>((i & 1) ? p.hull_max_mm[0] : p.hull_min_mm[0]) -
         p.center_of_mass_mm[0]) /
        1000;
    const auto y =
        (static_cast<double>((i & 2) ? p.hull_max_mm[1] : p.hull_min_mm[1]) -
         p.center_of_mass_mm[1]) /
        1000;
    const auto z =
        (static_cast<double>((i & 4) ? p.hull_max_mm[2] : p.hull_min_mm[2]) -
         p.center_of_mass_mm[2]) /
        1000;
    result = std::max(result, std::hypot(x, y, z));
  }
  if (!std::isfinite(result) || result <= 0)
    return std::unexpected(Error::invalid_descriptor);
  return result;
}
} // namespace

auto generate_origin_service_craft_descriptor(const PhysicalLocalSystem& owner,
                                              std::uint32_t version)
    -> std::expected<OriginServiceCraftDescriptor, Error> {
  if (version != kOriginServiceCraftVersion)
    return std::unexpected(Error::unsupported_version);
  if (!authentic_origin(owner)) return std::unexpected(Error::invalid_owner);
  const auto station = generate_origin_station(*owner.origin_universe_seed);
  OriginServiceCraftDescriptor d;
  d.universe_seed = *owner.origin_universe_seed;
  d.system = owner.catalog.id;
  d.id = {
      derive_seed(station.station_seed, kOriginServiceCraftSeedDomain).value};
  d.home_port = {station.id, 2};
  if (!d.id.value || !d.transfer_quanta_per_tick)
    return std::unexpected(Error::invalid_descriptor);
  return d;
}
auto validate_origin_service_craft_descriptor(
    const PhysicalLocalSystem& owner, const OriginServiceCraftDescriptor& d)
    -> std::expected<void, Error> {
  const auto expected =
      generate_origin_service_craft_descriptor(owner, d.version);
  if (!expected) return std::unexpected(expected.error());
  if (*expected != d) return std::unexpected(Error::invalid_descriptor);
  return {};
}
auto initialize_origin_service_craft(
    const PhysicalLocalSystem& owner, SimulationTick tick,
    std::span<const OriginDockConstraint> occupied)
    -> std::expected<OriginServiceCraftState, Error> {
  const auto d = generate_origin_service_craft_descriptor(owner);
  if (!d) return std::unexpected(d.error());
  if (tick > last_tick) return std::unexpected(Error::invalid_clock);
  if (occupied.size() > 2) return std::unexpected(Error::invalid_state);
  const auto station = generate_origin_station(d->universe_seed);
  const auto geometry = origin_station_geometry(station);
  if (!geometry) return std::unexpected(Error::invalid_owner);
  std::array<bool, 2> seen{};
  for (const auto& constraint : occupied) {
    if (constraint.tick != tick || constraint.port.ordinal < 1 ||
        constraint.port.ordinal > 2 ||
        !resolve_origin_docked_pose(owner, station, *geometry, constraint))
      return std::unexpected(Error::invalid_state);
    const auto index = constraint.port.ordinal - 1;
    if (seen[index]) return std::unexpected(Error::invalid_state);
    seen[index] = true;
    if (constraint.port == d->home_port)
      return std::unexpected(Error::occupied_port);
  }
  const auto body =
      release_origin_port(owner, station, *geometry,
                          {geometry->version, d->home_port, d->frame, tick});
  if (!body) return std::unexpected(Error::invalid_state);
  return OriginServiceCraftState{*d,
                                 *body,
                                 {d->id, tick, d->flight_capacity_quanta,
                                  d->cargo_capacity_quanta, d->jump_capacity},
                                 true,
                                 {}};
}
auto validate_origin_service_craft(const PhysicalLocalSystem& owner,
                                   const OriginServiceCraftState& state,
                                   const PhysicalMoonDescriptor* moon)
    -> std::expected<void, Error> {
  const auto& d = state.descriptor;
  if (const auto valid = validate_origin_service_craft_descriptor(owner, d);
      !valid)
    return std::unexpected(valid.error());
  if (!resources_valid(d, state.resources))
    return std::unexpected(Error::invalid_resources);
  if (state.body.tick != state.resources.tick || state.body.craft != d.frame)
    return std::unexpected(Error::invalid_state);
  const auto station = generate_origin_station(d.universe_seed);
  const bool lunar =
      state.body.frame.kind == RigidFrameKind::body_relative_inertial;
  if (lunar && (!moon || !validate_physical_moon_descriptor(*moon) ||
                moon->catalog.owner != owner ||
                !home_moon(owner, {owner.catalog.id, moon->body}) ||
                state.body.frame.body != moon->body))
    return std::unexpected(Error::invalid_owner);
  if (state.body.frame.kind != RigidFrameKind::system_inertial &&
      state.body.frame.kind != RigidFrameKind::station_relative_inertial &&
      state.body.frame.kind != RigidFrameKind::planet_relative_inertial &&
      !lunar)
    return std::unexpected(Error::invalid_state);
  const auto context = lunar ? RigidBodyWorldContext{*moon, &station}
                             : RigidBodyWorldContext{owner, &station};
  if (!validate_rigid_body_state(context, state.body))
    return std::unexpected(Error::invalid_state);
  if (state.attached) {
    const auto geometry = origin_station_geometry(station);
    if (!geometry) return std::unexpected(Error::invalid_owner);
    const auto pose = release_origin_port(
        owner, station, *geometry,
        {geometry->version, d.home_port, d.frame, state.body.tick});
    if (!pose || *pose != state.body)
      return std::unexpected(Error::invalid_state);
  }
  if (state.delivery) {
    const auto& r = *state.delivery;
    if (!r.target_craft.value || !home_moon(owner, r.target_body) ||
        r.tick > state.resources.tick || r.delivered_quanta == 0 ||
        r.delivered_quanta > d.cargo_capacity_quanta ||
        r.initial_target_quanta > kFreedomFlightCapacityQuanta ||
        r.delivered_quanta >
            kFreedomFlightCapacityQuanta - r.initial_target_quanta ||
        state.resources.cargo_quanta !=
            d.cargo_capacity_quanta - r.delivered_quanta)
      return std::unexpected(Error::invalid_resources);
  } else if (state.resources.cargo_quanta != d.cargo_capacity_quanta) {
    return std::unexpected(Error::invalid_resources);
  }
  return {};
}
auto advance_origin_service_resources(const OriginServiceCraftDescriptor& d,
                                      const OriginServiceCraftResources& r,
                                      SimulationTick source_tick,
                                      std::uint64_t debit)
    -> std::expected<OriginServiceCraftResources, Error> {
  if (!ledger_descriptor_valid(d) || !resources_valid(d, r))
    return std::unexpected(Error::invalid_resources);
  if (r.tick != source_tick || source_tick >= last_tick)
    return std::unexpected(Error::invalid_clock);
  if (debit > r.flight_quanta) return std::unexpected(Error::unavailable_fuel);
  auto next = r;
  ++next.tick;
  next.flight_quanta -= debit;
  return next;
}
auto spend_origin_service_jump_charge(const OriginServiceCraftDescriptor& d,
                                      const OriginServiceCraftResources& r,
                                      SimulationTick source_tick)
    -> std::expected<OriginServiceCraftResources, Error> {
  if (!ledger_descriptor_valid(d) || !resources_valid(d, r))
    return std::unexpected(Error::invalid_resources);
  if (r.tick != source_tick) return std::unexpected(Error::invalid_clock);
  if (!r.jump_charges) return std::unexpected(Error::unavailable_charge);
  auto next = r;
  --next.jump_charges;
  return next;
}
auto transfer_origin_service_fuel(const PhysicalMoonDescriptor& moon,
                                  const OriginServiceCraftState& service,
                                  const FreedomFlightSaveDocument& recipient,
                                  const MoonLandedCraftAnchor& anchor,
                                  const FreedomResources& receiver)
    -> std::expected<OriginServiceFuelTransfer, Error> {
  if (!validate_physical_moon_descriptor(moon) ||
      !validate_origin_service_craft(moon.catalog.owner, service, &moon))
    return std::unexpected(Error::invalid_owner);
  const auto tick = service.body.tick;
  const auto hydrated = hydrate_freedom_moon_flight_document(recipient);
  if (service.attached || !service.resources.flight_quanta || !hydrated ||
      hydrated->moon != moon ||
      !validate_freedom_resources(receiver, recipient) ||
      recipient.origin.recipe.universe_seed !=
          service.descriptor.universe_seed ||
      recipient.flight.tick != tick || receiver.tick != tick ||
      !receiver.jump_charges || !validate_moon_landed_craft(moon, anchor, tick))
    return std::unexpected(Error::invalid_target);
  const auto landed = resolve_moon_landed_craft(moon, anchor, tick);
  if (!landed || *landed != recipient.flight)
    return std::unexpected(Error::invalid_target);
  const RigidBodyWorldContext context{moon};
  const auto checksum = rigid_body_state_checksum(context, anchor.fixed);
  const RigidCoordinateFrame fixed_frame{RigidFrameKind::body_fixed,
                                         moon.catalog.owner.catalog.id,
                                         {},
                                         {},
                                         moon.body};
  const auto fixed =
      reframe_rigid_body(context, service.body, {fixed_frame, tick}, moon);
  const auto target =
      reframe_rigid_body(context, recipient.flight, {fixed_frame, tick}, moon);
  if (!checksum || !fixed || !target)
    return std::unexpected(Error::unsafe_service_geometry);
  const auto delta =
      difference(fixed->position_metres, target->position_metres);
  const auto velocity = difference(fixed->linear_velocity_metres_per_second,
                                   target->linear_velocity_metres_per_second);
  const auto radius = hull_radius(service.descriptor.frame);
  const double distance = std::hypot(delta.x, delta.y, delta.z);
  const double speed = std::hypot(velocity.x, velocity.y, velocity.z);
  if (!finite_vector(delta) || !finite_vector(velocity) || !radius ||
      !std::isfinite(distance) || distance < 2 * (*radius) + .05 ||
      distance > service.descriptor.link_range_metres ||
      !std::isfinite(speed) ||
      speed > service.descriptor.max_relative_speed_metres_per_second)
    return std::unexpected(Error::unsafe_service_geometry);
  const auto snapshot = MoonContactSnapshot::create(moon, service.body, false);
  if (!snapshot) return std::unexpected(Error::unsafe_service_geometry);
  const auto clearance = snapshot->hull_clearance();
  if (!clearance || !clearance->certified)
    return std::unexpected(Error::unsafe_service_geometry);
  const BodyTarget body{moon.catalog.owner.catalog.id, moon.body};
  auto next = service;
  if (next.delivery) {
    const auto& r = *next.delivery;
    if (r.target_craft != receiver.craft || r.target_body != body ||
        r.target_anchor_checksum != *checksum || tick < r.tick ||
        receiver.flight_quanta != r.initial_target_quanta + r.delivered_quanta)
      return std::unexpected(Error::stale_transfer);
    if (tick == r.tick) return OriginServiceFuelTransfer{service, receiver, 0};
  }
  const auto amount =
      std::min({service.descriptor.transfer_quanta_per_tick,
                service.resources.cargo_quanta,
                kFreedomFlightCapacityQuanta - receiver.flight_quanta});
  if (!amount) return OriginServiceFuelTransfer{service, receiver, 0};
  if (!next.delivery)
    next.delivery = OriginServiceFuelReceipt{
        receiver.craft, body, *checksum, receiver.flight_quanta, tick, 0};
  next.resources.cargo_quanta -= amount;
  next.delivery->delivered_quanta += amount;
  next.delivery->tick = tick;
  auto result = receiver;
  result.flight_quanta += amount; // ONLY recipient field permitted to change.
  return OriginServiceFuelTransfer{next, result, amount};
}
} // namespace apsis_drift
