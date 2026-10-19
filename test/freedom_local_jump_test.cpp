#include "apsis_drift/freedom_local_jump.hpp"
#include "apsis_drift/intersystem_jump.hpp"
#include "apsis_drift/station_geometry.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>

namespace {
using namespace apsis_drift;
int failures{};
auto check(bool pass, std::string_view message) -> void {
  if (!pass) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E> auto need(const std::expected<T, E>& result) -> T {
  if (!result) {
    std::cerr << "FAIL: local body jump fixture refused\n";
    if constexpr (requires { result.error().detail; })
      std::cerr << result.error().detail << '\n';
    else if constexpr (std::is_enum_v<E>)
      std::cerr << "error enum " << static_cast<unsigned>(result.error())
                << '\n';
    std::exit(1);
  }
  return *result;
}
auto norm(RigidVector3 v) -> double {
  return std::hypot(v.x, v.y, v.z);
}
auto fixture() -> FreedomLocalBodyJumpRequest {
  const auto seed = Seed{42};
  auto history = make_freedom_new_game_document(seed);
  const auto system = need(generate_physical_origin_system(seed, 2));
  const auto station = generate_origin_station(seed);
  const auto geometry = need(origin_station_geometry(station));
  const auto pose = need(resolve_origin_port_pose(system, station, geometry,
                                                  {station.id, 1}, 1200, 12));
  history.state.tick = pose.planet_relative.tick;
  FreedomFlightSaveDocument source{
      history,
      pose.planet_relative,
      {},
      FreedomActiveWorldSelection{1, system.catalog.id,
                                  history.recipe.home_planet}};
  source.model.physical_catalog = 2;
  source.model.physical_ephemeris = 2;
  source.model.terrain_policy = 3;
  source.model.frame_policy = 2;
  source.model.optics = 1;
  source.model.companions = 1;
  source.flight.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  // This pure source has no docking owner. The outer journey separately
  // refuses attached/outside pilots before an actual jump can begin.
  const auto catalog = need(generate_secondary_body_catalog(system));
  return {1,
          source,
          {system.catalog.id, catalog.bodies[0].node.id},
          1,
          source.model};
}
auto valid_contract() -> void {
  const auto request = fixture();
  const auto original = request;
  const auto moon_arrival = need(freeze_local_body_jump_arrival(request));
  check(request == original && moon_arrival.request == request &&
            moon_arrival.arrival_tick ==
                request.source.flight.tick + kJumpTransitTicks,
        "Pure local freeze preserves full genuine source and existing "
        "commit-to-arrival duration");
  check(moon_arrival.departure_volume.clear() &&
            moon_arrival.arrival_volume.clear() &&
            validate_frozen_local_body_jump_arrival(moon_arrival).has_value(),
        "Genuine local route freezes independently checked clear bounded body "
        "volumes");
  const auto moon =
      need(hydrate_freedom_moon_flight_document(moon_arrival.arrival)).moon;
  check(moon_arrival.source_checksum ==
            need(rigid_body_state_checksum(
                RigidBodyWorldContext{moon.catalog.owner},
                request.source.flight)),
        "Home source checksum uses original canonical physical frame");
  const auto fixed = need(reframe_rigid_body(RigidBodyWorldContext{moon},
                                             moon_arrival.arrival.flight,
                                             {{RigidFrameKind::body_fixed,
                                               moon.catalog.owner.catalog.id,
                                               {},
                                               {},
                                               moon.body},
                                              moon_arrival.arrival_tick},
                                             moon));
  const auto radius = moon.radius_metres + kMoonTerrainMaximumReliefMetres +
                      kFreedomLocalMoonArrivalClearanceMetres;
  check(norm({fixed.position_metres.x, fixed.position_metres.y - radius,
              fixed.position_metres.z}) < 1e-8 &&
            norm(fixed.linear_velocity_metres_per_second) < 1e-8 &&
            norm(fixed.angular_velocity_radians_per_second) < 1e-12,
        "Actual moon arrival is above global relief with surface co-motion "
        "applied exactly once");
  check(std::abs(fixed.orientation.w - 1) < 1e-12 &&
            std::abs(fixed.orientation.x) + std::abs(fixed.orientation.y) +
                    std::abs(fixed.orientation.z) <
                1e-12,
        "Actual moon fixed arrival is upright without borrowed planet "
        "orientation");
  auto history = moon_arrival.arrival.origin;
  history.state.tick = request.source.origin.state.tick;
  check(history == request.source.origin &&
            moon_arrival.arrival.flight.craft == request.source.flight.craft &&
            moon_arrival.arrival.flight.tick ==
                moon_arrival.arrival.origin.state.tick,
        "Local hop retains craft/history and one authoritative clock without "
        "resource metadata/refill");
  auto system_source = request;
  system_source.source.flight = need(reframe_rigid_body(
      RigidBodyWorldContext{moon.catalog.owner}, request.source.flight,
      {{RigidFrameKind::system_inertial, moon.catalog.owner.catalog.id, {}, {}},
       request.source.flight.tick}));
  const auto system_frozen =
      need(freeze_local_body_jump_arrival(system_source));
  check(system_frozen.request.source.flight == system_source.source.flight &&
            system_frozen.source_checksum ==
                need(rigid_body_state_checksum(
                    RigidBodyWorldContext{moon.catalog.owner},
                    system_source.source.flight)) &&
            system_frozen.arrival == moon_arrival.arrival,
        "Canonical system2 source remains saved verbatim while only the "
        "clearance view changes frame");
  const FreedomLocalBodyJumpRequest back{
      1,
      moon_arrival.arrival,
      {request.destination.system,
       body_id(request.source.origin.recipe.home_planet)},
      2,
      request.home_model};
  const auto home_arrival = need(freeze_local_body_jump_arrival(back));
  check(home_arrival.source_checksum ==
                need(rigid_body_state_checksum(RigidBodyWorldContext{moon},
                                               back.source.flight)) &&
            home_arrival.arrival.model == request.source.model &&
            !home_arrival.arrival.moon && home_arrival.arrival.world &&
            home_arrival.arrival.world->planet ==
                request.source.origin.recipe.home_planet,
        "Moon source uses genuine rigid4 checksum and home return restores "
        "frozen actual home profile");
  const auto station =
      generate_origin_station(request.source.origin.recipe.universe_seed);
  const auto geometry = need(origin_station_geometry(station));
  const auto port = need(resolve_origin_port_pose(moon.catalog.owner, station,
                                                  geometry, {station.id, 1},
                                                  home_arrival.arrival_tick));
  const auto n = geometry.ports[0].outward_normal;
  const auto& p = home_arrival.arrival.flight.position_metres;
  const auto& q = port.planet_relative.position_metres;
  check(norm({p.x - q.x - n.x * kAssistedOriginArrivalStandoffMetres,
              p.y - q.y - n.y * kAssistedOriginArrivalStandoffMetres,
              p.z - q.z - n.z * kAssistedOriginArrivalStandoffMetres}) < 1e-8 &&
            home_arrival.arrival.flight.linear_velocity_metres_per_second ==
                port.planet_relative.linear_velocity_metres_per_second &&
            home_arrival.arrival.flight.orientation ==
                port.planet_relative.orientation,
        "Home return uses actual future D1 orientation/velocity and existing "
        "forty-kilometre outward standoff");
  check(home_arrival.arrival_tick ==
                back.source.flight.tick + kJumpTransitTicks &&
            need(encode_freedom_flight_document_json(home_arrival.arrival))
                    .size() <= kMaximumSaveDocumentBytes,
        "Home arrival retains existing flight codec and transit clock");
  auto changed = moon_arrival;
  ++changed.source_checksum;
  check(!validate_frozen_local_body_jump_arrival(changed),
        "Frozen source checksum tampering refuses");
  changed = moon_arrival;
  changed.arrival.flight.position_metres.x += 1;
  check(!validate_frozen_local_body_jump_arrival(changed),
        "Frozen destination pose tampering refuses");
  changed = moon_arrival;
  ++changed.arrival_tick;
  check(!validate_frozen_local_body_jump_arrival(changed),
        "Frozen arrival clock tampering refuses");
  changed = moon_arrival;
  changed.arrival_volume.intersects_moon = true;
  check(!validate_frozen_local_body_jump_arrival(changed),
        "Frozen clearance stamp tampering refuses");
  auto wrong_home = back;
  wrong_home.home_model.hold.target =
      OrbitHoldTarget{request.source.origin.recipe.home_planet,
                      std::numeric_limits<double>::quiet_NaN(),
                      {0, 0, 1}};
  check(!freeze_local_body_jump_arrival(wrong_home),
        "Return cannot introduce a malformed home profile");
}
auto refusals() -> void {
  const auto source = fixture();
  const auto refuse = [](const FreedomLocalBodyJumpRequest& request) {
    check(!freeze_local_body_jump_arrival(request),
          "Invalid local route request refuses");
  };
  auto bad = source;
  ++bad.version;
  refuse(bad);
  bad = source;
  bad.attempt = 0;
  refuse(bad);
  bad = source;
  bad.attempt = std::numeric_limits<std::uint64_t>::max();
  refuse(bad);
  bad = source;
  bad.destination.body = body_id(source.source.origin.recipe.home_planet);
  refuse(bad);
  bad = source;
  bad.destination.body.kind = BodyKind::minor;
  refuse(bad);
  bad = source;
  ++bad.destination.body.version;
  refuse(bad);
  bad = source;
  ++bad.destination.body.value;
  refuse(bad);
  bad = source;
  ++bad.destination.system.value;
  refuse(bad);
  bad = source;
  bad.home_model.assistance = !bad.source.model.assistance;
  refuse(bad);
  bad = source;
  bad.source.flight.position_metres.x =
      std::numeric_limits<double>::quiet_NaN();
  refuse(bad);
  bad = source;
  bad.source.flight.orientation.w = -1;
  refuse(bad);
  bad = source;
  ++bad.source.origin.state.tick;
  refuse(bad);
  bad = source;
  bad.source.flight.tick =
      std::numeric_limits<SimulationTick>::max() - kJumpTransitTicks - 2;
  bad.source.origin.state.tick = bad.source.flight.tick;
  refuse(bad);
  bad = source;
  bad.source.flight.position_metres = {1, 0, 0};
  refuse(bad);
  const auto out = need(freeze_local_body_jump_arrival(source));
  const FreedomLocalBodyJumpRequest same_moon{
      1, out.arrival, source.destination, 2, source.home_model};
  refuse(same_moon);
  // Existing intersystem policy still refuses same-system destinations. The
  // local provider never widens its request type or validation route.
  FreedomJumpRequest legacy;
  legacy.universe_seed = source.source.origin.recipe.universe_seed;
  legacy.craft = source.source.origin.state.craft;
  legacy.source = source.source.flight;
  legacy.destination = source.source.flight.frame.system;
  check(!preview_freedom_jump(legacy) && !freeze_freedom_jump_arrival(legacy),
        "Old intersystem validator still rejects same-system jumps");
}
} // namespace

auto main() -> int {
  valid_contract();
  refusals();
  std::cout << "Local home/moon body jump: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
