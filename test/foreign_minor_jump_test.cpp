#include "apsis_drift/freedom_local_jump.hpp"
#include "apsis_drift/freedom_system_network.hpp"
#include "apsis_drift/intersystem_jump.hpp"
#include "apsis_drift/native_startup.hpp"
#include "apsis_drift/station_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
using namespace apsis_drift;
int checks{}, failures{};
auto check(bool value, std::string_view message) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E>
auto need(std::expected<T, E> value,
          std::source_location where = std::source_location::current()) -> T {
  if (!value) {
    std::cerr << where.file_name() << ":" << where.line() << " ";
    if constexpr (requires { value.error().detail; })
      std::cerr << value.error().detail << '\n';
    else if constexpr (std::is_enum_v<E>)
      std::cerr << "Refusal " << static_cast<unsigned>(value.error()) << '\n';
    throw std::runtime_error("Foreign Minor fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto norm(RigidVector3 v) -> double {
  return std::hypot(v.x, v.y, v.z);
}
auto fixture(std::uint32_t network_version = 1) -> FreedomLocalBodyJumpRequest {
  const Seed seed{42};
  const auto network =
      need(generate_freedom_system_network(seed, network_version));
  const auto owner =
      need(resolve_freedom_network_system(network, network.nodes[1].system));
  const auto& planet = owner.catalog.planets.front().descriptor;
  FreedomFlightSaveDocument source;
  source.origin = need(make_freedom_network_new_game_document(seed));
  source.origin.network->version = network_version;
  source.origin.state.tick = 1200;
  source.model.physical_catalog = 2;
  source.model.physical_ephemeris = 2;
  source.model.frame_policy = 2;
  source.model.terrain_policy = 3;
  source.model.optics = 1;
  source.model.companions = 1;
  source.world = FreedomActiveWorldSelection{2, owner.catalog.id, planet.id};
  source.flight.tick = source.origin.state.tick;
  source.flight.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  source.flight.frame = {RigidFrameKind::planet_relative_inertial,
                         owner.catalog.id,
                         planet.id,
                         {}};
  // Explicit saved-space geometry fixture, not a flown visit or paid receipt.
  source.flight.position_metres = {
      0, 0, static_cast<double>(planet.radius.value) * 10000};
  source.flight.linear_velocity_metres_per_second = {3, -4, 2};
  source.flight = need(canonicalize_rigid_body_state(
      RigidBodyWorldContext{owner}, source.flight));
  const auto catalog = need(generate_secondary_body_catalog(owner));
  return {kFreedomForeignMinorJumpVersion,
          source,
          {owner.catalog.id, catalog.bodies[1].node.id},
          2,
          source.model};
}
auto invalid_contract() -> void {
  const auto valid = fixture();
  for (unsigned fault = 0; fault < 13; ++fault) {
    auto request = valid;
    switch (fault) {
      case 0: request.version = 9; break;
      case 1: request.attempt = 0; break;
      case 2: request.attempt = UINT64_MAX; break;
      case 3: request.source.origin.network.reset(); break;
      case 4: request.source.origin.network->version = 3; break;
      case 5: request.destination.body.kind = BodyKind::moon; break;
      case 6: request.destination.system = {}; break;
      case 7:
        request.home_model.assistance = !request.home_model.assistance;
        break;
      case 8: request.home_model.hold.target = OrbitHoldTarget{}; break;
      case 9:
        request.source.flight.position_metres.x =
            std::numeric_limits<double>::quiet_NaN();
        break;
      case 10:
        request.source.flight.tick = UINT64_MAX - kJumpTransitTicks;
        request.source.origin.state.tick = request.source.flight.tick;
        break;
      case 11: request.recovery = FreedomDriveRecoveryState{}; break;
      case 12: request.version = kFreedomRecoveryForeignMinorJumpVersion; break;
      default: break;
    }
    check(!freeze_local_body_jump_arrival(request),
          "Invalid foreign source/target/profile/policy refuses before "
          "relocation");
  }
  auto wrong_entry = valid;
  const auto owner = need(hydrate_freedom_flight_document(valid.source)).system;
  const auto& other = owner.catalog.planets[1].descriptor;
  wrong_entry.source.world->planet = other.id;
  wrong_entry.source.flight.frame.planet = other.id;
  wrong_entry.source.flight.position_metres = {
      0, 0, static_cast<double>(other.radius.value) * 10000};
  check(!freeze_local_body_jump_arrival(wrong_entry),
        "A genuine other planet cannot masquerade as this route's entry "
        "reference");
  auto legacy = valid;
  legacy.version = kFreedomMinorLocalBodyJumpVersion;
  check(!freeze_local_body_jump_arrival(legacy),
        "Origin policy remains closed to foreign source");
}
auto route_contract(std::uint32_t network_version, bool assistance) -> void {
  auto request = fixture(network_version);
  request.source.model.assistance = assistance;
  request.home_model = request.source.model;
  const auto outbound = need(freeze_local_body_jump_arrival(request));
  check(
      outbound.request == request &&
          outbound.arrival_tick ==
              request.source.flight.tick + kJumpTransitTicks &&
          outbound.departure_volume.clear() && outbound.arrival_volume.clear(),
      "Foreign outbound retains actual source and full clear endpoint volumes");
  need(validate_frozen_local_body_jump_arrival(outbound));
  const auto minor =
      need(hydrate_freedom_minor_flight_document(outbound.arrival)).minor;
  const auto fixed = need(reframe_rigid_body(RigidBodyWorldContext{minor},
                                             outbound.arrival.flight,
                                             {{RigidFrameKind::body_fixed,
                                               minor.catalog.owner.catalog.id,
                                               {},
                                               {},
                                               minor.body},
                                              outbound.arrival_tick},
                                             minor));
  const double radius =
      minor.radius_metres + kFreedomLocalMinorArrivalClearanceMetres;
  check(
      norm({fixed.position_metres.x, fixed.position_metres.y - radius,
            fixed.position_metres.z}) < 1e-7 &&
          norm(fixed.linear_velocity_metres_per_second) < 1e-7 &&
          outbound.arrival.model.assistance == assistance &&
          minor.parent == body_id(minor.catalog.owner.catalog.star.id) &&
          minor.parent.kind == BodyKind::star,
      "Future owned minor rotation and actual stellar parent supply the fixed "
      "arrival");
  const double exclusion =
      need(minor_exclusion_radius_metres(minor, outbound.arrival.flight.craft));
  check(radius > exclusion && radius - exclusion > 9000,
        "Minor arrival retains more than9km above the existing "
        "hull-inclusive1km stand-off");
  const auto hierarchy = need(make_body_hierarchy(minor.catalog));
  const auto bodies =
      need(resolve_body_ephemerides(hierarchy, {outbound.arrival_tick, 0}));
  const auto absolute =
      std::ranges::find(bodies, minor.body, &BodyEphemeris::body);
  check(absolute != bodies.end(),
        "Actual future stellar-parented minor ephemeris exists");
  if (absolute == bodies.end()) throw std::runtime_error("Future minor absent");
  const double translation_bound =
      16 * std::numeric_limits<double>::epsilon() *
      std::max({1.0, std::abs(absolute->position.x),
                std::abs(absolute->position.y),
                std::abs(absolute->position.z)});
  check(norm({outbound.arrival_system_position.x - absolute->position.x -
                  outbound.arrival.flight.position_metres.x,
              outbound.arrival_system_position.y - absolute->position.y -
                  outbound.arrival.flight.position_metres.y,
              outbound.arrival_system_position.z - absolute->position.z -
                  outbound.arrival.flight.position_metres.z}) <=
            translation_bound,
        "Future minor-relative arrival translates through actual hierarchy "
        "into system axes");
  const auto encoded =
      need(encode_freedom_flight_document_json(outbound.arrival));
  check(encoded.find("\"format_version\": 64") != std::string::npos &&
            need(decode_freedom_flight_document_json(encoded)) ==
                outbound.arrival,
        "Explicit flight64 roundtrips full foreign world2 without normalizing "
        "history");
  for (unsigned fault = 0; fault < 4; ++fault) {
    auto raw = nlohmann::ordered_json::parse(encoded);
    switch (fault) {
      case 0: raw["world_owner"]["version"] = 1U; break;
      case 1:
        raw["format_version"] = kFreedomNetworkMinorFlightSaveFormatVersion;
        break;
      case 2: raw["world_owner"]["version"] = 4294967296ULL; break;
      case 3: raw["unexpected"] = 0; break;
      default: break;
    }
    check(!decode_freedom_flight_document_json(raw.dump()),
          "Flight64 refuses cross-version, wide and unknown fields without "
          "normalization");
  }
  const auto selected =
      need(native_select_save_document(NativeSaveDocument{outbound.arrival}));
  check(selected.home_planet.id == *request.source.flight.frame.planet,
        "Native startup reference is actual entry planet, not stellar parent "
        "or Origin "
        "Home");
  auto old_world = outbound.arrival;
  old_world.minor->version = 1;
  check(!hydrate_freedom_minor_flight_document(old_world),
        "Historical Minor world1 does not silently admit foreign owner");
  const auto origin = need(generate_physical_origin_system(
      request.source.origin.recipe.universe_seed, 2));
  FreedomLocalBodyJumpRequest back{
      kFreedomForeignMinorJumpVersion,
      outbound.arrival,
      {origin.catalog.id, body_id(request.source.origin.recipe.home_planet)},
      3,
      request.home_model};
  const auto returned = need(freeze_local_body_jump_arrival(back));
  need(validate_frozen_local_body_jump_arrival(returned));
  check(returned.arrival.model == request.home_model &&
            !returned.arrival.minor &&
            returned.arrival.world->system == origin.catalog.id &&
            returned.arrival.origin.recipe == request.source.origin.recipe &&
            returned.arrival.flight.craft == request.source.flight.craft,
        "Direct Home return retains expedition profile, craft and original "
        "journey identity");
  const auto station =
      generate_origin_station(request.source.origin.recipe.universe_seed);
  const auto geometry = need(origin_station_geometry(station));
  const auto port = need(resolve_origin_port_pose(
      origin, station, geometry, {station.id, 1}, returned.arrival_tick));
  const auto outward = geometry.ports[0].outward_normal;
  const auto actual = returned.arrival.flight.position_metres;
  check(norm({actual.x - port.planet_relative.position_metres.x -
                  outward.x * kAssistedOriginArrivalStandoffMetres,
              actual.y - port.planet_relative.position_metres.y -
                  outward.y * kAssistedOriginArrivalStandoffMetres,
              actual.z - port.planet_relative.position_metres.z -
                  outward.z * kAssistedOriginArrivalStandoffMetres}) < 1e-7,
        "Return uses actual future Origin port, never the foreign parent in "
        "its place");
  const auto network = need(generate_freedom_system_network(
      request.source.origin.recipe.universe_seed, network_version));
  const auto leg = need(resolve_freedom_system_network_route(
      network, minor.catalog.owner.catalog.id, origin.catalog.id));
  const auto& from = returned.source_system_position;
  const auto& to = returned.arrival_system_position;
  const double actual_distance =
      norm({static_cast<double>(leg.displacement.x) + to.x - from.x,
            static_cast<double>(leg.displacement.y) + to.y - from.y,
            static_cast<double>(leg.displacement.z) + to.z - from.z});
  check(actual_distance > 1000000000 &&
            actual_distance <= static_cast<double>(kFreedomJumpRatedMetres),
        "Actual cross-system return includes metre anchor displacement, not a "
        "zero-length local leg");
  auto beyond = back;
  // Labelled valid saved-space range boundary, not a physically flown escape.
  beyond.source.flight.position_metres = {kRigidBodyMaximumPositionMetres / 2,
                                          0, 0};
  const auto too_far = freeze_local_body_jump_arrival(beyond);
  check(!too_far && too_far.error() ==
                        FreedomLocalBodyJumpError::beyond_qualified_policy,
        "Actual endpoint displacement beyond rated drive refuses despite valid "
        "system anchors");
  auto excluded = back;
  excluded.source.flight.position_metres = {
      0, static_cast<double>(minor.radius_metres) + 500, 0};
  check(
      !freeze_local_body_jump_arrival(excluded),
      "Return cannot depart from inside the unchanged minor stand-off sphere");
  auto corrupt = returned;
  ++corrupt.source_checksum;
  check(!validate_frozen_local_body_jump_arrival(corrupt),
        "Frozen return refuses changed source checksum");
  back.destination.system = minor.catalog.owner.catalog.id;
  check(!freeze_local_body_jump_arrival(back),
        "Foreign Minor has no arbitrary parent/system return policy");
}
auto hold_contract() -> void {
  auto request = fixture();
  request.source.model.hold.target =
      OrbitHoldTarget{*request.source.flight.frame.planet,
                      norm(request.source.flight.position_metres),
                      {0, 1, 0}};
  request.home_model = request.source.model;
  request.home_model.hold = {};
  const auto frozen = need(freeze_local_body_jump_arrival(request));
  check(frozen.request.source.model.hold.target.has_value() &&
            !frozen.request.home_model.hold.target,
        "Only the body-bound hold is cleared from the authenticated expedition "
        "profile");
}
auto recovery_contract(std::uint32_t version) -> void {
  auto request = fixture();
  request.version = kFreedomRecoveryForeignMinorJumpVersion;
  FreedomDriveRecoveryState ready;
  ready.version = version;
  auto first = request;
  first.attempt = 1;
  first.recovery = ready;
  auto plain = first;
  plain.version = kFreedomForeignMinorJumpVersion;
  plain.recovery.reset();
  const auto first_frozen = need(freeze_local_body_jump_arrival(first));
  const auto first_plain = need(freeze_local_body_jump_arrival(plain));
  check(first_frozen.recovery &&
            !first_frozen.recovery->extra_radius_millimetres &&
            first_frozen.arrival == first_plain.arrival,
        "First recovered foreign-minor use retains exact nominal policy7 "
        "arrival");
  request.recovery = need(advance_freedom_drive_recovery(
      ready, FreedomJumpRecoveryKind::planet, 1, 123, 600));
  const auto frozen = need(freeze_local_body_jump_arrival(request));
  check(frozen.recovery && frozen.recovery->version == version &&
            frozen.recovery->extra_radius_millimetres > 0 &&
            frozen.arrival_volume.clear(),
        "Recovery6 retains actual early-use drive version/sample and clear "
        "destination envelope");
  need(validate_frozen_local_body_jump_arrival(frozen));
  const BodyTarget parent{request.source.flight.frame.system,
                          body_id(*request.source.flight.frame.planet)};
  const auto ordinary_outbound = need(freeze_freedom_jump_uncertainty(
      request.source.origin.recipe.universe_seed,
      request.source.origin.state.craft, request.source.flight.craft,
      request.source.origin.lineage, frozen.source_checksum,
      FreedomJumpRecoveryKind::minor, request.source.flight.tick, parent,
      request.destination, request.attempt, *request.recovery));
  check(ordinary_outbound == *frozen.recovery,
        "Purpose-specific outbound retains the ordinary Minor domain20 sample "
        "exactly");
  const auto origin = need(generate_physical_origin_system(
      request.source.origin.recipe.universe_seed, 2));
  auto drive = need(advance_freedom_drive_recovery(
      *request.recovery, FreedomJumpRecoveryKind::minor, request.attempt,
      frozen.source_checksum, request.source.flight.tick));
  FreedomLocalBodyJumpRequest back{
      kFreedomRecoveryForeignMinorJumpVersion,
      frozen.arrival,
      {origin.catalog.id, body_id(request.source.origin.recipe.home_planet)},
      3,
      request.home_model,
      drive};
  const auto returned = need(freeze_local_body_jump_arrival(back));
  check(returned.recovery && returned.arrival_volume.clear() &&
            returned.arrival.world->system == origin.catalog.id,
        "Early return recovery resolves offset and envelope in genuine Origin "
        "context");
  need(validate_frozen_local_body_jump_arrival(returned));
  const auto& source = back.source;
  const auto foreign_minor = source.minor->target;
  const auto ordinary_return = freeze_freedom_jump_uncertainty(
      source.origin.recipe.universe_seed, source.origin.state.craft,
      source.flight.craft, source.origin.lineage, returned.source_checksum,
      FreedomJumpRecoveryKind::minor, source.flight.tick, foreign_minor,
      back.destination, back.attempt, *back.recovery);
  check(!ordinary_return &&
            ordinary_return.error() == FreedomJumpRecoveryError::invalid_target,
        "Ordinary Minor sampler retains its cross-system return refusal");
  const auto specialized = need(freeze_freedom_foreign_minor_uncertainty(
      source.origin.recipe.universe_seed, source.origin.state.craft,
      source.flight.craft, source.origin.lineage, returned.source_checksum,
      source.flight.tick, foreign_minor, back.destination, back.attempt,
      *back.recovery));
  check(specialized == *returned.recovery,
        "Only the explicit foreign sampler admits the real cross-system Minor "
        "return");
  const auto same_system_return = freeze_freedom_foreign_minor_uncertainty(
      source.origin.recipe.universe_seed, source.origin.state.craft,
      source.flight.craft, source.origin.lineage, returned.source_checksum,
      source.flight.tick, foreign_minor, parent, back.attempt, *back.recovery);
  const auto wrong_direction = freeze_freedom_foreign_minor_uncertainty(
      source.origin.recipe.universe_seed, source.origin.state.craft,
      source.flight.craft, source.origin.lineage, returned.source_checksum,
      source.flight.tick, back.destination, foreign_minor, back.attempt,
      *back.recovery);
  check(!same_system_return &&
            same_system_return.error() ==
                FreedomJumpRecoveryError::invalid_target &&
            !wrong_direction &&
            wrong_direction.error() == FreedomJumpRecoveryError::invalid_target,
        "Foreign sampler refuses same-system Minor return and reversed "
        "cross-system direction");
}
} // namespace
int main() {
  try {
    invalid_contract();
    route_contract(1, true);
    route_contract(2, false);
    hold_contract();
    recovery_contract(1);
    recovery_contract(2);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
