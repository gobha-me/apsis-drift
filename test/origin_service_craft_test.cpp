#include "apsis_drift/origin_service_craft.hpp"
#include "moon_contact_fixture.hpp"

#include <iostream>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace apsis_drift;
unsigned checks{};
auto check(bool v, const char* message) -> void {
  ++checks;
  if (!v) throw std::runtime_error(message);
}
template <class T, class E>
auto need(std::expected<T, E> v,
          std::source_location loc = std::source_location::current()) -> T {
  if (!v) {
    std::cerr << loc.file_name() << ':' << loc.line() << " refusal ";
    if constexpr (std::is_enum_v<E>)
      std::cerr << static_cast<unsigned>(v.error());
    else if constexpr (requires { v.error().detail; })
      std::cerr << v.error().detail;
    std::cerr << '\n';
    throw std::runtime_error("Service source fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*v);
}
auto owner_and_budgets() -> void {
  for (const Seed seed : {Seed{0}, Seed{42}, Seed{123456789}}) {
    const auto owner = need(generate_physical_origin_system(seed, 2));
    // Detect provider mutation with an independent source catalog snapshot.
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Snapshot
    const auto before = owner;
    const auto station = generate_origin_station(seed);
    const auto d = need(generate_origin_service_craft_descriptor(owner));
    check(d == need(generate_origin_service_craft_descriptor(owner)) &&
              d.id.value ==
                  derive_seed(station.station_seed, static_cast<SeedDomain>(21))
                      .value &&
              d.home_port == OriginPortId{station.id, 2},
          "Independent service identity and genuine D2 source");
    auto state = need(initialize_origin_service_craft(owner, 1200));
    need(validate_origin_service_craft(owner, state));
    const auto geometry = need(origin_station_geometry(station));
    const OriginDockConstraint own{geometry.version, d.home_port, d.frame,
                                   1200};
    check(state.body ==
                  need(release_origin_port(owner, station, geometry, own)) &&
              state.attached,
          "Initial state is the actual mechanically constrained Wayfarer");
    const std::array occupied{own};
    check(!initialize_origin_service_craft(owner, 1200, occupied),
          "Occupied D2 refuses");
    const OriginDockConstraint player{
        geometry.version, {station.id, 1}, d.frame, 1200};
    const std::array other{player};
    check(initialize_origin_service_craft(owner, 1200, other).has_value(),
          "Real D1 occupant does not block D2");
    const std::array duplicate{player, player};
    check(!initialize_origin_service_craft(owner, 1200, duplicate),
          "Duplicate existing port roster refuses");
    auto wrong = player;
    ++wrong.tick;
    const std::array stale{wrong};
    check(!initialize_origin_service_craft(owner, 1200, stale),
          "Stale attachment cannot assert vacancy");
    auto altered = d;
    altered.link_range_metres = std::numeric_limits<double>::infinity();
    check(!validate_origin_service_craft_descriptor(owner, altered),
          "Nonfinite descriptor tuning refuses");
    altered = d;
    ++altered.id.value;
    check(!validate_origin_service_craft_descriptor(owner, altered),
          "Forged service ID refuses");
    check(!generate_origin_service_craft_descriptor(owner, 2),
          "Unknown service version refuses");
    const auto foreign =
        need(generate_physical_origin_system(Seed{seed.value + 1}, 2));
    check(!validate_origin_service_craft_descriptor(foreign, d),
          "Foreign source refuses");
    const auto debited =
        need(advance_origin_service_resources(d, state.resources, 1200, 12345));
    auto expected = state.resources;
    ++expected.tick;
    expected.flight_quanta -= 12345;
    check(debited == expected,
          "Actual tick debit preserves cargo, charges and identity");
    check(!advance_origin_service_resources(d, state.resources, 1199, 0),
          "Wrong source clock refuses");
    check(!advance_origin_service_resources(d, state.resources, 1200,
                                            state.resources.flight_quanta + 1),
          "Unavailable actual propulsion refuses");
    auto charges = state.resources;
    for (unsigned i = 0; i < d.jump_capacity; ++i)
      charges = need(spend_origin_service_jump_charge(d, charges, 1200));
    check(!spend_origin_service_jump_charge(d, charges, 1200) &&
              charges.cargo_quanta == d.cargo_capacity_quanta,
          "Finite jump pool exhausts independently of cargo");
    auto overflow = state.resources;
    overflow.tick = std::numeric_limits<SimulationTick>::max() - 3;
    check(!advance_origin_service_resources(d, overflow, overflow.tick, 0),
          "Clock boundary refuses before overflow");
    auto invalid = state;
    invalid.body.craft = {kStarterShuttleFrameId, kStarterShuttleFrameVersion};
    check(!validate_origin_service_craft(owner, invalid),
          "Frame recipe does not masquerade as service identity");
    check(owner == before, "Provider leaves source catalog unchanged");
  }
}
// LABELLED metadata/near-service geometry fixture. The actual contact owner,
// anchor, triangles and rigid frame math are real; this does not claim flown
// station departure, responder route, live actor admission or return reserve.
auto transfer_contract() -> void {
  const auto f = test_fixture::make_moon_contact_fixture();
  const auto& owner = f.moon.catalog.owner;
  const RigidBodyWorldContext context{f.moon};
  const auto anchor = need(prepare_moon_landed_craft(
      f.moon, f.source, true,
      need(rigid_body_state_checksum(context, f.source))));
  FreedomFlightSaveDocument player;
  player.origin = make_freedom_new_game_document(Seed{42});
  player.origin.state.tick = f.source.tick;
  player.flight =
      need(resolve_moon_landed_craft(f.moon, anchor, f.source.tick));
  player.model.physical_catalog = 2;
  player.model.physical_ephemeris = 2;
  player.moon = FreedomMoonWorldSelection{1, {owner.catalog.id, f.moon.body}};
  need(hydrate_freedom_moon_flight_document(player));
  FreedomResources receiver;
  receiver.craft = player.origin.state.craft;
  receiver.tick = player.flight.tick;
  receiver.flight_quanta = kFreedomFlightCapacityQuanta / 2;
  receiver.version = kFreedomStrainedResourceVersion;
  receiver.drive = FreedomDriveRecoveryState{
      kFreedomStrainedDriveRecoveryVersion, player.origin.lineage, {}};
  need(validate_freedom_resources(receiver, player));
  auto service = need(initialize_origin_service_craft(owner, f.source.tick));
  service.attached = false;
  auto hover = anchor.fixed;
  const auto n = f.triangle.outward_normal;
  hover.position_metres = {hover.position_metres.x + 30 * n.x,
                           hover.position_metres.y + 30 * n.y,
                           hover.position_metres.z + 30 * n.z};
  service.body = need(reframe_rigid_body(
      context, hover, {player.flight.frame, hover.tick}, f.moon));
  need(validate_origin_service_craft(owner, service, &f.moon));
  const auto original_player = player;
  const auto original_receiver = receiver;
  const auto original_service = service;
  const auto transaction = need(
      transfer_origin_service_fuel(f.moon, service, player, anchor, receiver));
  auto expected = receiver;
  expected.flight_quanta += service.descriptor.transfer_quanta_per_tick;
  check(transaction.receiver == expected &&
            transaction.transferred_quanta ==
                service.descriptor.transfer_quanta_per_tick,
        "Only recipient fuel changes; strained drive, clock, charges, lineage "
        "binding and versions remain exact");
  check(
      transaction.service.resources.cargo_quanta +
              transaction.receiver.flight_quanta ==
          service.resources.cargo_quanta + receiver.flight_quanta,
      "Fuel transfer conserves the independent source and receiver quantities");
  check(transaction.service.resources.flight_quanta ==
                service.resources.flight_quanta &&
            transaction.service.resources.jump_charges ==
                service.resources.jump_charges &&
            transaction.service.body == service.body,
        "Transfer advances no actor or propulsion clock");
  need(validate_origin_service_craft(owner, transaction.service, &f.moon));
  const auto repeated = need(transfer_origin_service_fuel(
      f.moon, transaction.service, player, anchor, transaction.receiver));
  check(repeated.transferred_quanta == 0 &&
            repeated.service == transaction.service &&
            repeated.receiver == transaction.receiver,
        "One authoritative tick cannot repeatedly credit fuel");
  auto next_service = transaction.service;
  next_service.resources = need(advance_origin_service_resources(
      next_service.descriptor, next_service.resources, next_service.body.tick,
      0));
  ++hover.tick;
  next_service.body = need(reframe_rigid_body(
      context, hover, {player.flight.frame, hover.tick}, f.moon));
  auto next_player = player;
  ++next_player.origin.state.tick;
  next_player.flight =
      need(resolve_moon_landed_craft(f.moon, anchor, hover.tick));
  auto next_receiver = need(
      advance_freedom_resource_tick(transaction.receiver, receiver.tick, 0));
  const auto second = need(transfer_origin_service_fuel(
      f.moon, next_service, next_player, anchor, next_receiver));
  check(second.transferred_quanta ==
                service.descriptor.transfer_quanta_per_tick &&
            second.service.delivery->target_anchor_checksum ==
                transaction.service.delivery->target_anchor_checksum,
        "Original landed-anchor binding remains stable at the next real frame "
        "clock");
  auto bad_receiver = receiver;
  bad_receiver.jump_charges = 0;
  check(!transfer_origin_service_fuel(f.moon, service, player, anchor,
                                      bad_receiver),
        "Stranded recipient must retain a jump charge");
  bad_receiver = transaction.receiver;
  --bad_receiver.flight_quanta;
  check(!transfer_origin_service_fuel(f.moon, transaction.service, player,
                                      anchor, bad_receiver),
        "Changed receiver quantity cannot reuse a receipt");
  auto bad_service = service;
  bad_service.body.position_metres.x = std::numeric_limits<double>::quiet_NaN();
  check(!transfer_origin_service_fuel(f.moon, bad_service, player, anchor,
                                      receiver),
        "Nonfinite service state refuses before geometry");
  auto far = hover;
  --far.tick;
  far.position_metres = {anchor.fixed.position_metres.x + 41 * n.x,
                         anchor.fixed.position_metres.y + 41 * n.y,
                         anchor.fixed.position_metres.z + 41 * n.z};
  bad_service = service;
  bad_service.body = need(reframe_rigid_body(
      context, far, {player.flight.frame, far.tick}, f.moon));
  check(!transfer_origin_service_fuel(f.moon, bad_service, player, anchor,
                                      receiver),
        "Outside finite link range refuses");
  far = anchor.fixed;
  bad_service.body = need(reframe_rigid_body(
      context, far, {player.flight.frame, far.tick}, f.moon));
  check(!transfer_origin_service_fuel(f.moon, bad_service, player, anchor,
                                      receiver),
        "Overlapping actual full hull envelopes refuse");
  auto moving = hover;
  --moving.tick;
  moving.linear_velocity_metres_per_second = {.2, 0, 0};
  bad_service.body = need(reframe_rigid_body(
      context, moving, {player.flight.frame, moving.tick}, f.moon));
  check(!transfer_origin_service_fuel(f.moon, bad_service, player, anchor,
                                      receiver),
        "Unstable relative link speed refuses");
  auto stale_anchor = anchor;
  ++stale_anchor.version;
  check(!transfer_origin_service_fuel(f.moon, service, player, stale_anchor,
                                      receiver),
        "Unsupported landing anchor refuses");
  auto stale_receipt = transaction.service;
  stale_receipt.delivery->target_anchor_checksum ^= 1;
  check(!transfer_origin_service_fuel(f.moon, stale_receipt, player, anchor,
                                      transaction.receiver),
        "Receipt cannot bind another anchor");
  check(player == original_player && receiver == original_receiver &&
            service == original_service,
        "Every pure positive/refusal leaves its input documents unchanged");
}
} // namespace
int main() {
  try {
    owner_and_budgets();
    transfer_contract();
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
  std::cout << "Origin service provider: " << checks << " checks, 0 failures\n";
}
