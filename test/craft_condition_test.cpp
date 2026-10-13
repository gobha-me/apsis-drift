#include "apsis_drift/craft_condition.hpp"
#include "apsis_drift/freedom_starting_assembly_save.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace apsis_drift;
auto check(bool good, const char* message) -> void {
  if (!good) throw std::runtime_error(message);
}
template <class T, class E> auto need(std::expected<T, E> v) -> T {
  if (!v) throw std::runtime_error("Condition fixture refused");
  return std::move(*v);
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
} // namespace
int main() {
  try {
    run();
    std::cout << "Craft condition contract passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
