#include "apsis_drift/freedom_jump_recovery.hpp"
#include "apsis_drift/freedom_jump_targeting.hpp"
#include "apsis_drift/freedom_local_jump.hpp"
#include "apsis_drift/freedom_planet_jump.hpp"
#include "apsis_drift/freedom_resources.hpp"
#include "apsis_drift/freedom_system_network.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "freedom_resource_save_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace apsis_drift;
unsigned checks{};
auto check(bool value, const char* message) -> void {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) {
    if constexpr (requires { value.error().detail; })
      std::cerr << value.error().detail << '\n';
    else if constexpr (std::is_enum_v<E>)
      std::cerr << "Refusal " << static_cast<unsigned>(value.error()) << '\n';
    throw std::runtime_error("Jump recovery provider fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto source(bool network = false) -> FreedomFlightSaveDocument {
  // A labelled saved-space numerical fixture, not a claimed native departure.
  const Seed seed{42};
  const auto owner = need(generate_physical_origin_system(seed, 2));
  FreedomFlightSaveDocument d;
  d.origin = network ? need(make_freedom_network_new_game_document(seed))
                     : make_freedom_new_game_document(seed);
  d.origin.state.tick = 1200;
  d.model.physical_catalog = 2;
  d.model.physical_ephemeris = 2;
  d.model.terrain_policy = 3;
  d.model.frame_policy = 2;
  d.model.optics = 1;
  d.model.companions = 1;
  const auto home =
      need(find_local_system_planet(owner, d.origin.recipe.home_planet));
  d.world = FreedomActiveWorldSelection{network ? 2U : 1U, owner.catalog.id,
                                        home->descriptor.id};
  d.flight.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  d.flight.frame = {RigidFrameKind::planet_relative_inertial,
                    owner.catalog.id,
                    home->descriptor.id,
                    {}};
  d.flight.tick = 1200;
  d.flight.position_metres = {
      0, 0, static_cast<double>(home->descriptor.radius.value) * 10000};
  d.flight.orientation = need(normalize_rigid_orientation({.8, .1, .2, .3}));
  d.flight.linear_velocity_metres_per_second = {3, -4, 2};
  d.flight.angular_velocity_radians_per_second = {.01, -.02, .03};
  d.flight = need(
      canonicalize_rigid_body_state(RigidBodyWorldContext{owner}, d.flight));
  need(hydrate_freedom_flight_document(d));
  return d;
}
auto stamp(const FrozenFreedomJumpUncertainty& u, FreedomJumpRecoveryKind kind,
           std::uint64_t attempt, SimulationTick commit)
    -> FreedomDriveRecoveryState {
  return {u.version,
          {},
          need(make_freedom_jump_recovery_stamp(kind, attempt,
                                                u.source_checksum, commit))};
}
auto offset_bound(const FrozenFreedomJumpUncertainty& value) -> void {
  const auto& p = value.offset_metres;
  const long double squared = static_cast<long double>(p.x) * p.x +
                              static_cast<long double>(p.y) * p.y +
                              static_cast<long double>(p.z) * p.z;
  const long double radius =
      static_cast<long double>(value.extra_radius_millimetres) / 1000;
  check(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
            squared <= radius * radius,
        "Independent long-double norm stays inside the disclosed recovery "
        "sphere");
}
auto scalar_contract() -> void {
  const FreedomDriveRecoveryState ready;
  check(need(freedom_jump_recovery_reading(ready, 0)) ==
            FreedomJumpRecoveryReading{0, 0, true, false},
        "First selected drive has no invented recovery debt");
  auto state = FreedomDriveRecoveryState{
      1,
      {},
      need(make_freedom_jump_recovery_stamp(FreedomJumpRecoveryKind::planet, 1,
                                            0, 100))};
  const auto arrival = state.last_use->arrival_tick;
  check(arrival == 100 + kJumpTransitTicks &&
            need(freedom_jump_recovery_reading(state, 100)) ==
                FreedomJumpRecoveryReading{5400, 2000000, false, true},
        "Committed transit retains a pending full45seconds, not elapsed "
        "transit debt");
  std::uint32_t previous = kFreedomJumpRecoveryMaximumRadiusMillimetres;
  for (SimulationTick elapsed = 0; elapsed <= kFreedomDriveRecoveryTicks;
       ++elapsed) {
    const auto reading =
        need(freedom_jump_recovery_reading(state, arrival + elapsed));
    const auto remaining = kFreedomDriveRecoveryTicks - elapsed;
    const long double q =
        static_cast<long double>(remaining) / kFreedomDriveRecoveryTicks;
    const long double independent = 2000000.0L * q * q * (3 - 2 * q);
    check(reading.remaining_ticks == remaining && !reading.pending_arrival &&
              reading.ready == (remaining == 0) &&
              reading.extra_radius_millimetres <= previous &&
              static_cast<long double>(reading.extra_radius_millimetres) +
                      1e-8L >=
                  independent &&
              static_cast<long double>(reading.extra_radius_millimetres) -
                      independent <=
                  1.00000001L,
          "Every bounded recovery tick corroborates smoothstep outward "
          "millimetre rounding and monotone fade");
    previous = reading.extra_radius_millimetres;
  }
  check(need(freedom_jump_recovery_reading(state, arrival + 5401)) ==
            FreedomJumpRecoveryReading{0, 0, true, false},
        "The inclusive45second boundary is ready without timer mutation");
  for (unsigned fault = 0; fault < 8; ++fault) {
    auto bad = state;
    switch (fault) {
      case 0: bad.version = 3; break;
      case 1: bad.lineage = FreedomCraftLineage{2, 1}; break;
      case 2: bad.lineage = FreedomCraftLineage{1, 0}; break;
      case 3:
        bad.last_use->kind = static_cast<FreedomJumpRecoveryKind>(255);
        break;
      case 4: bad.last_use->attempt = 0; break;
      case 5: bad.last_use->attempt = UINT64_MAX; break;
      case 6: ++bad.last_use->arrival_tick; break;
      case 7: bad.last_use->commit_tick = UINT64_MAX; break;
      default: break;
    }
    check(!validate_freedom_drive_recovery(bad, arrival),
          "Malformed version/lineage/stamp/counter/clock refuses");
  }
  check(!freedom_jump_recovery_reading(state, 99),
        "A future uncommitted use cannot manufacture a cooldown");
  const auto latest_commit =
      UINT64_MAX - kJumpTransitTicks - kFreedomDriveRecoveryTicks - 3;
  auto limit = need(make_freedom_jump_recovery_stamp(
      FreedomJumpRecoveryKind::system, 1, 7, latest_commit));
  check(
      need(freedom_jump_recovery_reading({1, {}, limit}, limit.arrival_tick))
                  .remaining_ticks == 5400 &&
          need(freedom_jump_recovery_reading({1, {}, limit}, UINT64_MAX - 3))
              .ready &&
          !make_freedom_jump_recovery_stamp(FreedomJumpRecoveryKind::system, 1,
                                            7, latest_commit + 1),
      "Near-limit clocks reserve a complete recovery and use safe subtraction");
  const auto d = source();
  const auto owner = need(hydrate_freedom_flight_document(d));
  const BodyTarget a{owner.system.catalog.id, body_id(*d.flight.frame.planet)};
  const BodyTarget b{owner.system.catalog.id,
                     body_id(owner.system.catalog.planets[1].descriptor.id)};
  const auto checksum = need(
      rigid_body_state_checksum(RigidBodyWorldContext{owner.system}, d.flight));
  // Explicit scalar prior-use fixture: outer travel separately binds this stamp
  // to a genuine immutable receipt. This helper grants no flight permission.
  const FreedomDriveRecoveryState prior{
      1,
      {},
      need(make_freedom_jump_recovery_stamp(FreedomJumpRecoveryKind::moon, 1,
                                            123, 960))};
  const auto freeze = [&](const FreedomDriveRecoveryState& saved,
                          std::uint64_t word = 0) {
    return freeze_freedom_jump_uncertainty(
        {42}, d.origin.state.craft, d.flight.craft, {}, checksum + word,
        FreedomJumpRecoveryKind::planet, 1200, a, b, 2, saved);
  };
  const auto actual = need(freeze(prior));
  check(actual == need(freeze(prior)) &&
            actual.sample_seed != need(freeze(prior, 1)).sample_seed &&
            actual.remaining_ticks == 5400 &&
            actual.extra_radius_millimetres == 2000000 &&
            static_cast<std::uint64_t>(SeedDomain::jump_recovery_uncertainty) ==
                20 &&
            derive_seed({42}, SeedDomain::jump_arrival, 3) !=
                derive_seed({42}, SeedDomain::jump_recovery_uncertainty, 3),
        "Independent named domain binds the real checksum and never consumes "
        "old arrival state");
  offset_bound(actual);
  auto wrong = prior;
  wrong.last_use->arrival_tick = 1201;
  wrong.last_use->commit_tick = 961;
  check(!freeze(wrong) && !freeze(ready),
        "Future pending use and discarded shared-attempt history refuse");
  wrong = prior;
  wrong.lineage = FreedomCraftLineage{1, 1};
  check(!freeze(wrong),
        "A foreign physical lineage cannot borrow the current drive");
}
auto resource_contract() -> void {
  auto d = source();
  FreedomResources old{1, d.origin.state.craft, d.flight.craft, d.flight.tick};
  need(validate_freedom_resources(old, d));
  old.drive = FreedomDriveRecoveryState{};
  check(!validate_freedom_resources(old),
        "Resource1 cannot silently select drive recovery");
  auto selected = old;
  selected.version = kFreedomRecoveryResourceVersion;
  need(validate_freedom_resources(selected, d));
  auto missing = selected;
  missing.drive.reset();
  check(!validate_freedom_resources(missing),
        "Resource2 requires its selected drive state");
  selected.drive->last_use = need(make_freedom_jump_recovery_stamp(
      FreedomJumpRecoveryKind::planet, 1, 123, 960));
  const auto next = need(advance_freedom_resource_tick(selected, 1200, 17));
  check(next.drive == selected.drive && next.tick == 1201 &&
            next.flight_quanta + 17 == selected.flight_quanta &&
            need(freedom_jump_recovery_reading(*next.drive, next.tick))
                    .remaining_ticks == 5399,
        "Ordinary resource clock/fuel advance retains one immutable "
        "arrival-based drive stamp");
  auto wrong = selected;
  wrong.drive->lineage = FreedomCraftLineage{1, 1};
  check(!validate_freedom_resources(wrong, d),
        "Resource owner validation binds actual craft lineage");
  wrong = selected;
  ++wrong.craft.value;
  check(!validate_freedom_resources(wrong, d),
        "A foreign craft cannot take another drive's state");
  const auto owner = need(hydrate_freedom_flight_document(d)).system;
  const auto station = generate_origin_station({42});
  const auto geometry = need(origin_station_geometry(station));
  d.flight = need(release_origin_port(
      owner, station, geometry,
      {geometry.version, {station.id, 1}, selected.frame, selected.tick}));
  selected.flight_quanta = 123;
  selected.jump_charges = 0;
  const auto serviced = need(replenish_freedom_resources(
      selected, {d, {geometry.version, {station.id, 1}, true}}));
  check(serviced.drive == selected.drive && serviced.tick == selected.tick &&
            serviced.flight_quanta == kFreedomFlightCapacityQuanta &&
            serviced.jump_charges == 3,
        "Genuine attached service refills fuel/charges without repairing or "
        "resetting recovery");
}
auto same_body_motion(const FreedomFlightSaveDocument& a,
                      const FreedomFlightSaveDocument& b) -> bool {
  return a.flight.linear_velocity_metres_per_second ==
             b.flight.linear_velocity_metres_per_second &&
         a.flight.orientation == b.flight.orientation &&
         a.flight.angular_velocity_radians_per_second ==
             b.flight.angular_velocity_radians_per_second &&
         a.model == b.model && a.origin == b.origin;
}
auto planet_contract(std::uint32_t drive_version = 1) -> void {
  const auto d = source(true);
  const auto owner = need(hydrate_freedom_flight_document(d)).system;
  const BodyTarget home{owner.catalog.id, body_id(*d.flight.frame.planet)};
  const BodyTarget target{owner.catalog.id,
                          body_id(owner.catalog.planets[1].descriptor.id)};
  const FreedomPlanetJumpRequest old{1, d, target, 1};
  const auto baseline = need(freeze_freedom_planet_jump_arrival(old));
  auto request = old;
  request.version = kFreedomRecoveryPlanetJumpVersion;
  request.recovery.emplace();
  request.recovery->version = drive_version;
  const auto first = need(freeze_freedom_planet_jump_arrival(request));
  check(first.arrival == baseline.arrival &&
            first.arrival_system_position == baseline.arrival_system_position &&
            first.recovery && first.recovery->extra_radius_millimetres == 0 &&
            first.recovery->remaining_ticks == 0,
        "First selected planet jump retains exact old baseline geometry");
  request.source = first.arrival;
  request.destination = home;
  request.attempt = 2;
  request.recovery =
      stamp(*first.recovery, FreedomJumpRecoveryKind::planet, 1, d.flight.tick);
  auto plain = request;
  plain.version = 1;
  plain.recovery.reset();
  const auto exact = need(freeze_freedom_planet_jump_arrival(plain));
  const auto early = need(freeze_freedom_planet_jump_arrival(request));
  const auto delta = early.recovery->offset_metres;
  check(
      same_body_motion(early.arrival, exact.arrival) &&
          early.recovery->extra_radius_millimetres == 2000000 &&
          early.arrival.flight.position_metres ==
              RigidVector3{exact.arrival.flight.position_metres.x + delta.x,
                           exact.arrival.flight.position_metres.y + delta.y,
                           exact.arrival.flight.position_metres.z + delta.z} &&
          early.arrival_volume.clear() && early.departure_volume.clear() &&
          validate_frozen_freedom_planet_jump_arrival(early).has_value(),
      "Early planet arrival adds only bounded location error, preserving "
      "genuine motion/model/owners and complete endpoints");
  offset_bound(*early.recovery);
  auto nonfinite = request;
  nonfinite.source.flight.position_metres.x =
      std::numeric_limits<double>::quiet_NaN();
  check(
      !freeze_freedom_planet_jump_arrival(nonfinite),
      "New planetary recovery policy never sanitizes nonfinite physical input");
  auto tampered = early;
  tampered.recovery->offset_metres.x += 1;
  check(!validate_frozen_freedom_planet_jump_arrival(tampered),
        "Edited planetary sample refuses regeneration");
  auto malformed = request;
  malformed.version = 1;
  check(!freeze_freedom_planet_jump_arrival(malformed),
        "Old planet policy never accepts recovery context");
  malformed = request;
  malformed.recovery.reset();
  check(!freeze_freedom_planet_jump_arrival(malformed),
        "New planet policy requires actual recovery input");
  auto late = request;
  late.source.flight.tick += 5400;
  late.source.origin.state.tick = late.source.flight.tick;
  const auto waited = need(freeze_freedom_planet_jump_arrival(late));
  auto late_plain = late;
  late_plain.version = 1;
  late_plain.recovery.reset();
  check(waited.recovery->extra_radius_millimetres == 0 &&
            waited.arrival ==
                need(freeze_freedom_planet_jump_arrival(late_plain)).arrival,
        "Ready repeat planet arrival regains exact baseline geometry "
        "at45seconds");
}
auto local_contract(bool minor, std::uint32_t drive_version = 1) -> void {
  const auto d = source();
  const auto owner = need(hydrate_freedom_flight_document(d)).system;
  const auto catalog = need(generate_secondary_body_catalog(owner));
  const auto old_policy =
      minor ? kFreedomMinorLocalBodyJumpVersion : kFreedomLocalBodyJumpVersion;
  const auto policy = minor ? kFreedomRecoveryMinorLocalBodyJumpVersion
                            : kFreedomRecoveryLocalBodyJumpVersion;
  const auto kind =
      minor ? FreedomJumpRecoveryKind::minor : FreedomJumpRecoveryKind::moon;
  FreedomLocalBodyJumpRequest request{
      old_policy,
      d,
      {owner.catalog.id, catalog.bodies[minor ? 1 : 0].node.id},
      1,
      d.model};
  const auto baseline = need(freeze_local_body_jump_arrival(request));
  request.version = policy;
  request.recovery.emplace();
  request.recovery->version = drive_version;
  const auto first = need(freeze_local_body_jump_arrival(request));
  check(
      first.arrival == baseline.arrival &&
          first.arrival_system_position == baseline.arrival_system_position &&
          first.recovery && first.recovery->extra_radius_millimetres == 0,
      "First selected companion use preserves its genuine old typed baseline");
  request.source = first.arrival;
  request.destination = {owner.catalog.id,
                         body_id(d.origin.recipe.home_planet)};
  request.attempt = 2;
  request.recovery = stamp(*first.recovery, kind, 1, d.flight.tick);
  auto plain = request;
  plain.version = old_policy;
  plain.recovery.reset();
  const auto exact = need(freeze_local_body_jump_arrival(plain));
  const auto early = need(freeze_local_body_jump_arrival(request));
  const auto delta = early.recovery->offset_metres;
  check(
      same_body_motion(early.arrival, exact.arrival) &&
          early.recovery->extra_radius_millimetres == 2000000 &&
          early.arrival.flight.position_metres ==
              RigidVector3{exact.arrival.flight.position_metres.x + delta.x,
                           exact.arrival.flight.position_metres.y + delta.y,
                           exact.arrival.flight.position_metres.z + delta.z} &&
          early.arrival_volume.clear() && early.departure_volume.clear() &&
          validate_frozen_local_body_jump_arrival(early).has_value(),
      "Actual typed companion return applies the shared drive's extra location "
      "error without losing its home model");
  offset_bound(*early.recovery);
  auto nonfinite = request;
  nonfinite.source.flight.linear_velocity_metres_per_second.y =
      std::numeric_limits<double>::infinity();
  check(!freeze_local_body_jump_arrival(nonfinite),
        "New companion policy still refuses actual nonfinite owner state");
  auto tampered = early;
  ++tampered.recovery->sample_seed.value;
  check(!validate_frozen_local_body_jump_arrival(tampered),
        "Edited companion seed refuses exact rederivation");
  auto missing = request;
  missing.recovery.reset();
  check(!freeze_local_body_jump_arrival(missing),
        "New companion policy cannot omit its drive state");
  auto legacy = request;
  legacy.version = old_policy;
  check(!freeze_local_body_jump_arrival(legacy),
        "Old companion policy rejects the new context");
}
auto system_contract(bool network, IntersystemRuleProfile profile,
                     std::uint32_t drive_version = 1) -> void {
  const auto d = source(network);
  const auto ids = generate_first_intersystem_identities({42});
  FreedomJumpRequest request;
  request.version = network ? 2 : 1;
  request.universe_seed = {42};
  request.craft = d.origin.state.craft;
  request.source = d.flight;
  request.destination = ids.target_system;
  request.profile = profile;
  request.network = d.origin.network;
  const auto baseline = need(freeze_freedom_jump_arrival(request));
  request.version = kFreedomRecoveryJumpTargetingVersion;
  request.recovery.emplace();
  request.recovery->version = drive_version;
  const auto first = need(freeze_freedom_jump_arrival(request));
  check(first.point == baseline.point &&
            first.sample_seed == baseline.sample_seed &&
            first.preview.nominal_arrival == baseline.preview.nominal_arrival &&
            first.preview.distance == baseline.preview.distance &&
            first.recovery->extra_radius_millimetres == 0,
        "First system recovery policy retains the exact pair/network Domain14 "
        "baseline point and disclosed base uncertainty");
  const auto destination =
      network
          ? need(resolve_freedom_network_system(
                need(generate_freedom_system_network({42})), ids.target_system))
          : need(generate_physical_local_system(ids.target_system_seed, 2));
  const auto ephemeris =
      need(resolve_planet_ephemeris(destination, first.preview.reference_planet,
                                    {first.preview.arrival_tick, 0}));
  request.source.tick = first.preview.arrival_tick;
  request.source.frame = {RigidFrameKind::planet_relative_inertial,
                          ids.target_system,
                          first.preview.reference_planet,
                          {}};
  request.source.position_metres = {first.point.x - ephemeris.position.x,
                                    first.point.y - ephemeris.position.y,
                                    first.point.z - ephemeris.position.z};
  request.source.linear_velocity_metres_per_second = {
      first.preview.arrival_velocity.x - ephemeris.velocity.x,
      first.preview.arrival_velocity.y - ephemeris.velocity.y,
      first.preview.arrival_velocity.z - ephemeris.velocity.z};
  request.source = need(canonicalize_rigid_body_state(
      RigidBodyWorldContext{destination}, request.source));
  request.destination = ids.origin_system;
  request.attempt = 2;
  request.recovery =
      stamp(*first.recovery, FreedomJumpRecoveryKind::system, 1, d.flight.tick);
  auto plain = request;
  plain.version = network ? 2 : 1;
  plain.recovery.reset();
  const auto exact = need(freeze_freedom_jump_arrival(plain));
  const auto early = need(freeze_freedom_jump_arrival(request));
  const auto delta = early.recovery->offset_metres;
  check(early.sample_seed == exact.sample_seed &&
            early.point == SystemPositionMetres{exact.point.x + delta.x,
                                                exact.point.y + delta.y,
                                                exact.point.z + delta.z} &&
            early.preview.arrival_velocity == exact.preview.arrival_velocity &&
            *early.preview.distance.envelope_radius_metres ==
                *exact.preview.distance.envelope_radius_metres + 2000 &&
            early.preview.volume->clear() && early.point_assessment.clear() &&
            validate_frozen_freedom_jump_arrival(early).has_value(),
        "Early system return adds independent recovery scatter and complete "
        "expanded-envelope checks without replacing base alignment/velocity "
        "policy");
  offset_bound(*early.recovery);
  const auto home = need(generate_physical_origin_system({42}, 2));
  check(early.preview.volume ==
            need(assess_freedom_arrival_volume(
                home, early.preview.arrival_tick, early.preview.nominal_arrival,
                *exact.preview.distance.envelope_radius_metres + 2000)),
        "Independent assessment uses the complete sum-of-radii envelope, not "
        "only the drawn point");
  auto nonfinite = request;
  nonfinite.source.angular_velocity_radians_per_second.z =
      std::numeric_limits<double>::quiet_NaN();
  check(!freeze_freedom_jump_arrival(nonfinite),
        "New system policy refuses nonfinite actual dynamics before sampling");
  auto corrupt = early;
  ++corrupt.recovery->extra_radius_millimetres;
  check(!validate_frozen_freedom_jump_arrival(corrupt),
        "Edited system radius refuses regeneration");
  auto missing = request;
  missing.recovery.reset();
  check(!freeze_freedom_jump_arrival(missing),
        "New system policy cannot omit drive state");
  auto legacy = request;
  legacy.version = network ? 2 : 1;
  check(!preview_freedom_jump(legacy),
        "Old system policies cannot silently accept recovery context");
}
auto strain_contract() -> void {
  const FreedomDriveRecoveryState ready{kFreedomStrainedDriveRecoveryVersion};
  check(need(freedom_jump_recovery_reading(ready, 0)) ==
            FreedomJumpRecoveryReading{0, 0, true, false, 5400, 5400},
        "A fresh strained drive has no debt and projects the normal45s cycle");
  const auto first = need(advance_freedom_drive_recovery(
      ready, FreedomJumpRecoveryKind::system, 1, 11, 100));
  check(first.version == 2 && first.lineage == ready.lineage &&
            first.last_use ==
                need(make_freedom_jump_recovery_stamp(
                    FreedomJumpRecoveryKind::system, 1, 11, 100)) &&
            !ready.last_use,
        "First strain use retains normal45s and never mutates the input");
  const auto second_tick = first.last_use->arrival_tick + 360;
  const auto second = need(advance_freedom_drive_recovery(
      first, FreedomJumpRecoveryKind::planet, 2, 12, second_tick));
  check(second.last_use->recovery_ticks == 10440 &&
            second.last_use->arrival_tick == second_tick + kJumpTransitTicks &&
            need(freedom_jump_recovery_reading(first, second_tick))
                    .next_recovery_ticks == 10440,
        "Real360tick spool cooling carries5040ticks into the next arrival");
  const auto third = need(
      advance_freedom_drive_recovery(second, FreedomJumpRecoveryKind::moon, 3,
                                     13, second.last_use->arrival_tick + 360));
  const auto fourth = need(
      advance_freedom_drive_recovery(third, FreedomJumpRecoveryKind::minor, 4,
                                     14, third.last_use->arrival_tick + 360));
  check(third.last_use->recovery_ticks == 10800 &&
            fourth.last_use->recovery_ticks == 10800,
        "A shared system/planet/Moon/minor sequence caps debt at90s");
  const auto untouched = fourth;
  check(!advance_freedom_drive_recovery(fourth, FreedomJumpRecoveryKind::system,
                                        5, 15,
                                        fourth.last_use->arrival_tick - 1) &&
            fourth == untouched,
        "A pending transit refuses an overlapping use atomically");
  const auto arrival = fourth.last_use->arrival_tick;
  std::uint32_t previous = kFreedomJumpRecoveryMaximumRadiusMillimetres;
  for (SimulationTick elapsed = 0; elapsed <= 10800; ++elapsed) {
    const auto reading =
        need(freedom_jump_recovery_reading(fourth, arrival + elapsed));
    const auto remaining = 10800 - elapsed;
    const long double q = static_cast<long double>(remaining) / 10800;
    const long double reference = 2000000.0L * q * q * (3 - 2 * q);
    check(reading.remaining_ticks == remaining &&
              reading.cycle_ticks == 10800 &&
              reading.next_recovery_ticks ==
                  std::min<SimulationTick>(10800, 5400 + remaining) &&
              reading.ready == !remaining && !reading.pending_arrival &&
              reading.extra_radius_millimetres <= previous &&
              static_cast<long double>(reading.extra_radius_millimetres) +
                      1e-8L >=
                  reference &&
              static_cast<long double>(reading.extra_radius_millimetres) -
                      reference <=
                  1.00000001L,
          "Every90s-cycle tick independently corroborates bounded normalized "
          "smoothstep and cooling");
    previous = reading.extra_radius_millimetres;
  }
  check(need(freedom_jump_recovery_reading(fourth, arrival + 5400))
                .extra_radius_millimetres == 1000000,
        "Half of a90s cycle leaves half its location-error radius");
  const auto cooled = need(advance_freedom_drive_recovery(
      fourth, FreedomJumpRecoveryKind::system, 5, 15, arrival + 10800));
  check(cooled.last_use->recovery_ticks == 5400,
        "Waiting out all debt restores a45s next cycle");
  auto legacy = first;
  legacy.version = 1;
  const auto legacy_next = need(advance_freedom_drive_recovery(
      legacy, FreedomJumpRecoveryKind::planet, 2, 12, second_tick));
  check(
      legacy_next.last_use ==
              need(make_freedom_jump_recovery_stamp(
                  FreedomJumpRecoveryKind::planet, 2, 12, second_tick)) &&
          need(freedom_jump_recovery_reading(legacy, second_tick))
                  .next_recovery_ticks == 5400,
      "Historical drive1 keeps its exact fixed45s stamp despite an early use");
  for (unsigned fault = 0; fault < 8; ++fault) {
    auto bad = fourth;
    switch (fault) {
      case 0: bad.version = 3; break;
      case 1: bad.last_use->recovery_ticks = 0; break;
      case 2: bad.last_use->recovery_ticks = 5399; break;
      case 3: bad.last_use->recovery_ticks = 10801; break;
      case 4: bad.last_use->recovery_ticks = UINT64_MAX; break;
      case 5: ++bad.last_use->arrival_tick; break;
      case 6: bad.last_use->attempt = 1; break;
      case 7: bad.version = 1; break;
      default: break;
    }
    check(!validate_freedom_drive_recovery(bad, arrival),
          "Unsupported versions/durations/first-use strain and clock tampering "
          "refuse");
  }
  for (auto attempt : {std::uint64_t{0}, std::uint64_t{2}, UINT64_MAX})
    check(!advance_freedom_drive_recovery(
              ready, FreedomJumpRecoveryKind::planet, attempt, 1, 100),
          "Invalid first attempts cannot manufacture a strain stamp");
  const auto latest = UINT64_MAX - kJumpTransitTicks - 10800 - 3;
  auto limit_prior = fourth;
  limit_prior.last_use->arrival_tick = latest;
  limit_prior.last_use->commit_tick = latest - kJumpTransitTicks;
  const auto limit = need(advance_freedom_drive_recovery(
      limit_prior, FreedomJumpRecoveryKind::system, 5, 15, latest));
  check(limit.last_use->recovery_ticks == 10800 &&
            need(freedom_jump_recovery_reading(limit, UINT64_MAX - 3)).ready &&
            !advance_freedom_drive_recovery(limit_prior,
                                            FreedomJumpRecoveryKind::system, 5,
                                            15, latest + 1),
        "The last full90s-cycle commit finishes safely; the next tick refuses");
  const auto d = source();
  const auto owner = need(hydrate_freedom_flight_document(d)).system;
  const BodyTarget a{owner.catalog.id, body_id(*d.flight.frame.planet)};
  const BodyTarget b{owner.catalog.id,
                     body_id(owner.catalog.planets[1].descriptor.id)};
  const auto sample_tick = arrival + 5400;
  const auto sampled = need(freeze_freedom_jump_uncertainty(
      {42}, d.origin.state.craft, d.flight.craft, {}, 17,
      FreedomJumpRecoveryKind::planet, sample_tick, a, b, 5, fourth));
  check(
      sampled.version == 2 && sampled.remaining_ticks == 5400 &&
          sampled.extra_radius_millimetres == 1000000 &&
          sampled == need(freeze_freedom_jump_uncertainty(
                         {42}, d.origin.state.craft, d.flight.craft, {}, 17,
                         FreedomJumpRecoveryKind::planet, sample_tick, a, b, 5,
                         fourth)),
      "Strain sample is frozen once from the actual normalized cooling input");
  offset_bound(sampled);
  auto shorter = fourth;
  shorter.last_use->recovery_ticks = 9000;
  const auto changed = need(freeze_freedom_jump_uncertainty(
      {42}, d.origin.state.craft, d.flight.craft, {}, 17,
      FreedomJumpRecoveryKind::planet, sample_tick, a, b, 5, shorter));
  check(changed.sample_seed != sampled.sample_seed,
        "Version2 sample stream binds its retained recovery duration");
  auto foreign = fourth;
  foreign.lineage = FreedomCraftLineage{1, 1};
  check(!freeze_freedom_jump_uncertainty(
            {42}, d.origin.state.craft, d.flight.craft, {}, 17,
            FreedomJumpRecoveryKind::planet, sample_tick, a, b, 5, foreign),
        "A foreign lineage cannot contribute cooling debt to this craft");
}
auto strain_resource_codec_contract() -> void {
  auto d = source();
  FreedomResources ledger{3, d.origin.state.craft, d.flight.craft,
                          d.flight.tick};
  const FreedomDriveRecoveryState ready{2};
  const auto first = need(advance_freedom_drive_recovery(
      ready, FreedomJumpRecoveryKind::moon, 1, 11, 100));
  ledger.drive =
      need(advance_freedom_drive_recovery(first, FreedomJumpRecoveryKind::minor,
                                          2, 12, first.last_use->arrival_tick));
  const auto before = ledger;
  need(validate_freedom_resources(ledger, d));
  const auto json = detail::resource_json(ledger);
  check(need(detail::resource_value(json)) == ledger &&
            json["drive"]["last_use"]["recovery_ticks"] == 10800,
        "Resource3 roundtrip retains exact bounded strain debt");
  const auto next =
      need(advance_freedom_resource_tick(ledger, ledger.tick, 17));
  check(
      next.drive == ledger.drive && next.tick == ledger.tick + 1 &&
          ledger == before,
      "Ordinary clock/fuel accounting retains strain without mutating source");
  auto mixed = ledger;
  mixed.version = 2;
  check(!validate_freedom_resources(mixed), "Resource2 cannot adopt drive2");
  mixed = ledger;
  mixed.drive->version = 1;
  check(!validate_freedom_resources(mixed), "Resource3 cannot adopt drive1");
  auto historical = *ledger.drive;
  historical.version = 1;
  historical.last_use = need(make_freedom_jump_recovery_stamp(
      FreedomJumpRecoveryKind::system, 1, 11, 100));
  const auto old = detail::drive_json(historical);
  check(
      old.dump() ==
              R"({"version":1,"lineage":null,"last_use":{"kind":0,"attempt":1,"source_checksum":11,"commit_tick":100,"arrival_tick":340}})" &&
          need(detail::drive_value(old, 1200)) == historical,
      "Historical drive1 JSON remains byte-exact with no duration field");
  for (unsigned fault = 0; fault < 13; ++fault) {
    auto bad = json;
    auto& drive = bad["drive"];
    auto& use = drive["last_use"];
    switch (fault) {
      case 0: use.erase("recovery_ticks"); break;
      case 1: use["recovery_ticks"] = 5400.0; break;
      case 2: use["recovery_ticks"] = -1; break;
      case 3: use["recovery_ticks"] = 5399U; break;
      case 4: use["recovery_ticks"] = 10801U; break;
      case 5: use["recovery_ticks"] = UINT64_MAX; break;
      case 6: use["unselected"] = 0U; break;
      case 7: drive["version"] = 1U; break;
      case 8: bad["version"] = 2U; break;
      case 9: bad["version"] = 4U; break;
      case 10: drive["version"] = 3U; break;
      case 11: drive.erase("last_use"); break;
      case 12: bad.erase("drive"); break;
      default: break;
    }
    check(!detail::resource_value(bad),
          "Closed resource3 codec refuses malformed duration, mixed policy and "
          "extra fields");
  }
  auto old_extra = old;
  old_extra["last_use"]["recovery_ticks"] = 5400U;
  check(!detail::drive_value(old_extra, 1200),
        "Historical drive1 shape refuses new duration fields");
  const FrozenFreedomJumpUncertainty sample{2, 17, {19}, 10800, 2000000, {}};
  const auto uncertainty = detail::uncertainty_json(sample);
  check(need(detail::uncertainty_value(uncertainty)) == sample,
        "Frozen uncertainty2 permits the full90s duration");
  auto old_sample = uncertainty;
  old_sample["version"] = 1U;
  check(!detail::uncertainty_value(old_sample),
        "Frozen uncertainty1 retains its old45s bound");
  const auto system = need(hydrate_freedom_flight_document(d)).system;
  const auto station = generate_origin_station({42});
  const auto geometry = need(origin_station_geometry(station));
  d.flight = need(release_origin_port(
      system, station, geometry,
      {geometry.version, {station.id, 1}, ledger.frame, ledger.tick}));
  ledger.flight_quanta = 123;
  ledger.jump_charges = 0;
  const auto serviced = need(replenish_freedom_resources(
      ledger, {d, {geometry.version, {station.id, 1}, true}}));
  check(serviced.drive == ledger.drive && serviced.tick == ledger.tick &&
            serviced.flight_quanta == kFreedomFlightCapacityQuanta &&
            serviced.jump_charges == 3,
        "Actual attached service refills resource3 without clearing strain");
}
} // namespace
int main() {
  try {
    scalar_contract();
    strain_contract();
    strain_resource_codec_contract();
    resource_contract();
    planet_contract();
    local_contract(false);
    local_contract(true);
    for (bool network : {false, true})
      for (auto profile :
           {IntersystemRuleProfile::assisted, IntersystemRuleProfile::pilot})
        system_contract(network, profile);
    planet_contract(2);
    local_contract(false, 2);
    local_contract(true, 2);
    for (bool network : {false, true})
      for (auto profile :
           {IntersystemRuleProfile::assisted, IntersystemRuleProfile::pilot})
        system_contract(network, profile, 2);
    std::cout << checks << " jump recovery provider checks passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
