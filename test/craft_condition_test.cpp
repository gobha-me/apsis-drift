#include "apsis_drift/craft_condition.hpp"
#include "apsis_drift/freedom_starting_assembly_save.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <source_location>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace apsis_drift;
auto check(bool good, const char* message) -> void {
  if (!good) throw std::runtime_error(message);
}
template <class T, class E>
auto need(std::expected<T, E> v,
          std::source_location at = std::source_location::current()) -> T {
  if (!v) {
    auto message = "Condition fixture line " + std::to_string(at.line());
    if constexpr (std::is_same_v<E, std::string>)
      message += ": " + v.error();
    else if constexpr (requires { v.error().detail; })
      message += ": " + v.error().detail;
    throw std::runtime_error(message);
  }
  if constexpr (!std::is_void_v<T>) return std::move(*v);
}
auto next(FreedomFlightSaveDocument d) -> FreedomFlightSaveDocument {
  ++d.flight.tick;
  d.origin.state.tick = d.flight.tick;
  return d;
}
auto impact(const FreedomFlightSaveDocument& before,
            const FreedomFlightSaveDocument& candidate, double speed,
            CraftImpactRegion region = CraftImpactRegion::forebody)
    -> TerrainImpactSweep {
  const auto owner = need(hydrate_freedom_flight_document(before));
  TerrainImpactWitness w;
  w.terrain_policy = before.model.terrain_policy;
  w.craft = before.origin.state.craft;
  w.craft_frame = before.flight.craft;
  w.system = before.flight.frame.system;
  w.planet = before.flight.frame.planet.value();
  w.tick = candidate.flight.tick;
  w.source_checksum =
      need(rigid_body_state_checksum({owner.system}, before.flight));
  w.candidate_checksum =
      need(rigid_body_state_checksum({owner.system}, candidate.flight));
  w.fraction = .5;
  w.triangle.planet = w.planet;
  w.region = region;
  w.outward_normal_planet_fixed = {1, 0, 0};
  w.pre_response_velocity_planet_fixed = {-speed, 0, 0};
  w.closing_speed_metres_per_second = speed;
  return {1, TerrainImpactClass::impact, 0, w};
}
auto run() -> void {
  const auto before =
      need(make_freedom_starting_assembly_new_game_document({42}))
          .journey.voyage.flight;
  const auto candidate = next(before);
  const auto pristine = need(make_craft_condition(before));
  for (double speed :
       {0.0, 2.0, std::nextafter(2.0, 3.0), 8.0, std::nextafter(8.0, 9.0), 20.0,
        std::nextafter(20.0, 21.0), 60.0, std::nextafter(60.0, 61.0)}) {
    const auto event = impact(before, candidate, speed);
    const auto result =
        need(reduce_craft_condition(pristine, before, candidate, event));
    const auto expected = speed <= 2 ? 0 : speed <= 8 ? 1 : speed <= 20 ? 2 : 3;
    check(result.event_applied && result.condition.forebody == expected &&
              !result.condition.landing_structure &&
              result.catastrophic == (speed > 60),
          "Exact closing threshold classes/localization");
    check(condition_view_cracked(result.condition) == (expected >= 1) &&
              condition_visibility_degraded(result.condition) ==
                  (expected >= 2) &&
              condition_allows_landing(result.condition),
          "Capabilities derive from their named region");
    check(need(decode_craft_condition_json(need(encode_craft_condition_json(
              result.condition)))) == result.condition,
          "Latest receipt and gate roundtrip exactly");
    const auto retry = need(
        reduce_craft_condition(result.condition, before, candidate, event));
    check(!retry.event_applied && retry.condition == result.condition,
          "Retry of exact latest impact is idempotent");
    auto invalid = event;
    invalid.witness->candidate_checksum ^= 1;
    check(!reduce_craft_condition(pristine, before, candidate, invalid),
          "Wrong source/candidate witness cannot mutate condition");
  }
  auto flight = candidate;
  auto c =
      need(reduce_craft_condition(pristine, before, candidate,
                                  impact(before, candidate, 4,
                                         CraftImpactRegion::landing_structure)))
          .condition;
  const TerrainImpactSweep clear{
      1, TerrainImpactClass::clear, kTerrainImpactClearMarginMetres, {}};
  auto after = next(flight);
  c = need(reduce_craft_condition(c, flight, after, clear)).condition;
  flight = after;
  check(!c.contact_armed && c.clear_ticks == 1,
        "One certified clear tick cannot rearm");
  after = next(flight);
  c = need(reduce_craft_condition(c, flight, after,
                                  {1, TerrainImpactClass::near, 0, {}}))
          .condition;
  flight = after;
  check(!c.contact_armed && !c.clear_ticks && c.landing_structure == 1,
        "Known clear but near contact advances without damage or rearm");
  after = next(flight);
  c = need(reduce_craft_condition(c, flight, after,
                                  {1, TerrainImpactClass::unknown, 0, {}}))
          .condition;
  flight = after;
  check(!c.contact_armed && !c.clear_ticks,
        "Unknown coverage resets persisted clear streak");
  for (unsigned i = 0; i < 2; ++i) {
    after = next(flight);
    c = need(reduce_craft_condition(c, flight, after, clear)).condition;
    flight = after;
  }
  check(c.contact_armed && !c.clear_ticks,
        "Two certified margin sweeps rearm contact");
  after = next(flight);
  c = need(reduce_craft_condition(
               c, flight, after,
               impact(flight, after, 4, CraftImpactRegion::landing_structure)))
          .condition;
  flight = after;
  check(c.landing_structure == 2 && !c.forebody && !condition_allows_landing(c),
        "Repeated localized landing damage affects only landing capability");
  after = next(flight);
  const auto repeated = need(reduce_craft_condition(
      c, flight, after,
      impact(flight, after, 12, CraftImpactRegion::landing_structure)));
  check(!repeated.event_applied && repeated.condition.landing_structure == 2 &&
            repeated.condition.next_event == c.next_event,
        "Resting contact cannot repeatedly bill severity");
  const auto escalation =
      impact(flight, after, 80, CraftImpactRegion::landing_structure);
  const auto same_episode =
      need(reduce_craft_condition(c, flight, after, escalation));
  check(same_episode.event_applied && same_episode.catastrophic &&
            same_episode.condition.landing_structure == 3 &&
            same_episode.condition.next_event == c.next_event + 1,
        "New catastrophic witness escalates an unarmed contact episode once");
  const auto catastrophic_retry = need(reduce_craft_condition(
      same_episode.condition, flight, after, escalation));
  check(!catastrophic_retry.event_applied && catastrophic_retry.catastrophic &&
            catastrophic_retry.condition == same_episode.condition,
        "Matching catastrophic retry keeps one exact receipt/loss event");
  for (double bad : {std::numeric_limits<double>::infinity(),
                     std::numeric_limits<double>::quiet_NaN(), -1.0}) {
    auto invalid = impact(before, candidate, 4);
    invalid.witness->closing_speed_metres_per_second = bad;
    check(!reduce_craft_condition(pristine, before, candidate, invalid),
          "Invalid event numerical data refuses");
  }
  auto wrong = pristine;
  ++wrong.tick;
  check(!validate_craft_condition(wrong, before),
        "Condition cannot own an independent clock");
  wrong = pristine;
  ++wrong.craft.value;
  check(!validate_craft_condition(wrong, before),
        "Condition is bound to individual craft identity");
  auto short_clear = clear;
  short_clear.minimum_clearance_metres = std::nextafter(.05, 0.0);
  check(!reduce_craft_condition(pristine, before, candidate, short_clear),
        "Clear proof below selected margin refuses");
  check(!decode_craft_condition_json("{\"version\":1,\"version\":1}"),
        "Duplicate condition fields refuse");
}

// These reducer/service boundary fixtures do not claim a flown damaged return.
auto dock_boundary(FreedomFlightSaveDocument source)
    -> FreedomDockingSaveDocument {
  const auto owner = need(hydrate_freedom_flight_document(source));
  const auto station =
      generate_origin_station(source.origin.recipe.universe_seed);
  const auto geometry = need(origin_station_geometry(station));
  const OriginPortId port{station.id, 1};
  const OriginDockConstraint constraint{
      geometry.version, port, source.flight.craft, source.flight.tick};
  source.flight =
      need(release_origin_port(owner.system, station, geometry, constraint));
  FreedomDockingSaveDocument result{std::move(source),
                                    {geometry.version, port, true}};
  need(validate_freedom_docking_document(result));
  return result;
}
auto repair_contract() -> void {
  using Json = nlohmann::ordered_json;
  auto flight = need(make_freedom_starting_assembly_new_game_document({42}))
                    .journey.voyage.flight;
  auto condition = need(make_craft_condition(flight));
  const auto legacy =
      "{\"version\":1,\"craft\":\"" +
      std::to_string(flight.origin.state.craft.value) +
      "\",\"frame_id\":2,\"frame_version\":1,\"lineage\":null,"
      "\"tick\":\"0\",\"forebody\":0,\"landing_structure\":0,"
      "\"contact_armed\":true,\"clear_ticks\":0,\"next_event\":\"1\","
      "\"latest\":null}";
  check(need(encode_craft_condition_json(condition)) == legacy,
        "The original ordered condition-v1 bytes remain exact");
  const auto pristine_port = dock_boundary(flight);
  check(!craft_origin_repair_price(condition) &&
            !repair_craft_condition(condition, pristine_port),
        "Pristine service cannot create a repair receipt");
  const auto pristine_bytes = need(encode_craft_condition_json(condition));
  const auto clear = TerrainImpactSweep{
      1, TerrainImpactClass::clear, kTerrainImpactClearMarginMetres, {}};
  auto next_flight = next(flight);
  condition = need(reduce_craft_condition(condition, flight, next_flight,
                                          impact(flight, next_flight, 40)))
                  .condition;
  flight = next_flight;
  for (unsigned i = 0; i < 2; ++i) {
    next_flight = next(flight);
    condition =
        need(reduce_craft_condition(condition, flight, next_flight, clear))
            .condition;
    flight = next_flight;
  }
  next_flight = next(flight);
  condition =
      need(reduce_craft_condition(condition, flight, next_flight,
                                  impact(flight, next_flight, 40,
                                         CraftImpactRegion::landing_structure)))
          .condition;
  flight = next_flight;
  check(condition.forebody == 3 && condition.landing_structure == 3 &&
            !condition.latest->catastrophic &&
            need(craft_origin_repair_price(condition)) == 60,
        "Two noncatastrophic severity-three regions cost exactly sixty");
  const auto port = dock_boundary(flight);
  const auto source = condition;
  const auto fixed = need(repair_craft_condition(condition, port));
  check(fixed.version == 2 && fixed.latest_repair &&
            fixed.craft == source.craft && fixed.frame == source.frame &&
            fixed.lineage == source.lineage && fixed.tick == source.tick &&
            fixed.latest == source.latest &&
            fixed.next_event == source.next_event &&
            fixed.contact_armed == source.contact_armed &&
            fixed.clear_ticks == source.clear_ticks && !fixed.forebody &&
            !fixed.landing_structure && !fixed.latest_repair->payment.paid &&
            !fixed.latest_repair->credits,
        "Free same-tick repair preserves individual impact/gate history");
  check(condition_allows_landing(fixed) && !condition_view_cracked(fixed) &&
            !condition_visibility_degraded(fixed),
        "Functional repair restores current visibility and landing ability");
  need(validate_craft_condition(fixed, port.flight));
  const auto encoded = need(encode_craft_condition_json(fixed));
  check(need(decode_craft_condition_json(encoded)) == fixed &&
            need(encode_craft_condition_json(source)) != encoded &&
            !repair_craft_condition(fixed, port),
        "Repair-v2 roundtrips and duplicate service refuses");
  const auto paid = need(repair_craft_condition(source, port, {true, 0, 80}));
  check(paid.latest_repair->payment == CraftRepairPayment{true, 0, 80} &&
            paid.latest_repair->credits == 60,
        "Paid receipt retains its exact tariff and cumulative debit watermark");
  for (const auto payment :
       {CraftRepairPayment{false, 1, 0}, CraftRepairPayment{false, 0, 1},
        CraftRepairPayment{true, source.tick + 1, 80},
        CraftRepairPayment{true, 0, 59}})
    check(!repair_craft_condition(source, port, payment),
          "Contradictory free/paid service anchors refuse");
  auto bad_port = port;
  bad_port.docking.attached = false;
  check(!repair_craft_condition(source, bad_port),
        "Port proximity without actual attachment is not service");
  bad_port = port;
  ++bad_port.docking.target.ordinal;
  check(!repair_craft_condition(source, bad_port),
        "A different port cannot authorize the current pose");
  bad_port = port;
  bad_port.flight.flight.position_metres.x += 1;
  check(!repair_craft_condition(source, bad_port),
        "A displaced pose cannot impersonate the canonical attachment");
  auto catastrophic = source;
  catastrophic.latest->witness.pre_response_velocity_planet_fixed = {-80, 0, 0};
  catastrophic.latest->witness.closing_speed_metres_per_second = 80;
  catastrophic.latest->catastrophic = true;
  check(!craft_origin_repair_price(catastrophic) &&
            !repair_craft_condition(catastrophic, port),
        "Catastrophe cannot be repaired into a resurrection");
  for (unsigned fault = 0; fault < 7; ++fault) {
    auto bad = fixed;
    switch (fault) {
      case 0: bad.latest_repair.reset(); break;
      case 1: bad.version = 1; break;
      case 2: bad.latest_repair->source_projection_digest ^= 1; break;
      case 3: bad.latest_repair->service_pose_checksum ^= 1; break;
      case 4: ++bad.latest_repair->attachment.tick; break;
      case 5: bad.latest_repair->before.clear_ticks = 2; break;
      case 6: bad.contact_armed = !bad.contact_armed; break;
      default: break;
    }
    check(!validate_craft_condition(bad, port.flight),
          "Malformed repair boundary cannot pass contextual admission");
  }
  const auto tree = Json::parse(encoded);
  check(tree.size() == 13 && tree.contains("latest_repair"),
        "Only condition-v2 selects the thirteenth field");
  for (const auto& [key, value] : tree["latest_repair"].items()) {
    (void)value;
    auto bad = tree;
    bad["latest_repair"].erase(key);
    check(!decode_craft_condition_json(bad.dump()),
          "Every closed repair-receipt field is required");
  }
  auto extra = tree;
  extra["latest_repair"]["older_condition"] = tree;
  check(!decode_craft_condition_json(extra.dump()),
        "Repair receipts reject recursive or extra condition fields");
  auto after = fixed;
  flight = port.flight;
  for (unsigned i = 0; i < 2; ++i) {
    next_flight = next(flight);
    after = need(reduce_craft_condition(after, flight, next_flight, clear))
                .condition;
    flight = next_flight;
  }
  check(after.contact_armed && after.latest_repair == fixed.latest_repair,
        "Only ordinary certified clear sweeps rearm after repair");
  next_flight = next(flight);
  after = need(reduce_craft_condition(after, flight, next_flight,
                                      impact(flight, next_flight, 4)))
              .condition;
  flight = next_flight;
  check(after.forebody == 1 && !after.landing_structure &&
            after.latest->serial == source.latest->serial + 1 &&
            after.latest_repair == fixed.latest_repair,
        "A later actual reducer event charges fresh damage after the boundary");
  const auto repaired_again =
      need(repair_craft_condition(after, dock_boundary(flight)));
  check(!repaired_again.forebody &&
            repaired_again.latest_repair->before.latest == *after.latest &&
            repaired_again.next_event == after.next_event &&
            repaired_again.latest == after.latest,
        "Repair after new damage replaces only the bounded latest receipt");
  check(need(encode_craft_condition_json(
            need(decode_craft_condition_json(pristine_bytes)))) == legacy &&
            source == condition,
        "Repair queries leave old condition bytes and source values untouched");
}
} // namespace
int main() {
  try {
    run();
    repair_contract();
    std::cout << "Craft condition contract passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
