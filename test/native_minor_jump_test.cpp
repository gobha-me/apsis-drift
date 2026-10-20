#include "native_minor_route_fixture.hpp"

#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>

namespace {
using namespace apsis_drift;
using namespace apsis_drift::minor_route_fixture;
auto invalid_sources(const NativeFreedomFlightSession& s) -> void {
  auto bad = s.document();
  bad.world = FreedomActiveWorldSelection{1, s.system().catalog.id,
                                          bad.origin.recipe.home_planet};
  ensure(!hydrate_freedom_flight_document(bad),
         "Minor admitted simultaneous planet owner");
  bad = s.document();
  bad.minor->target.body.kind = BodyKind::moon;
  ensure(!hydrate_freedom_flight_document(bad),
         "Minor admitted Moon body alias");
  bad = s.document();
  bad.minor->target.system.value ^= 1;
  ensure(!hydrate_freedom_flight_document(bad),
         "Minor admitted foreign system");
  bad = s.document();
  bad.flight.position_metres.x = std::numeric_limits<double>::quiet_NaN();
  ensure(!encode_freedom_flight_document_json(bad),
         "Minor encoded nonfinite state");
  bad = s.document();
  bad.flight.tick = std::numeric_limits<SimulationTick>::max();
  bad.origin.state.tick = bad.flight.tick;
  ensure(!hydrate_freedom_flight_document(bad),
         "Minor admitted exhausted clock");
  bad = s.document();
  bad.flight.position_metres = {
      0, static_cast<double>(s.minor()->radius_metres) + 100, 0};
  ensure(!hydrate_freedom_flight_document(bad),
         "Minor admitted source inside inspection exclusion shell");
  auto frozen = *s.travel()->local->committed;
  frozen.arrival.flight.position_metres.x += 1;
  ensure(!validate_frozen_local_body_jump_arrival(frozen),
         "Frozen minor arrival accepted changed pose");
  auto json = nlohmann::ordered_json::parse(
      need(encode_freedom_flight_document_json(s.document())));
  ensure(json["format_version"] == 46 && json["flight"]["version"] == 5,
         "Minor requires explicit flight46/rigid5");
  json["world_owner"]["terrain_recipe"] = 1;
  ensure(!decode_freedom_flight_document_json(json.dump()),
         "Minor accepted hidden terrain field");
}
auto run() -> void {
  auto s = departure();
  const auto craft = s.document().origin.state.craft;
  const auto binding = s.starting_assembly();
  const auto model = s.document().model;
  const auto entries = s.knowledge()->entries;
  const auto marker = s.knowledge()->surface_marker;
  const auto condition = s.condition();
  const auto target = minor_target(s);
  auto bad_target = target;
  bad_target.body.kind = BodyKind::moon;
  const auto before = bytes(s);
  ensure(!s.select_local_jump(bad_target) && bytes(s) == before,
         "Invalid minor target mutated actual home save");
  need(s.select_local_jump(target));
  need(s.begin_jump());
  auto controls = NativeFlightControls{};
  controls.positive_translation.x = std::numeric_limits<double>::quiet_NaN();
  const auto spooling = bytes(s);
  ensure(!s.advance(controls) && bytes(s) == spooling,
         "Nonfinite spool input mutated state");
  need(s.cancel_jump());
  ensure(s.resources()->jump_charges == 3 && s.travel()->next_attempt == 1,
         "Cancelled minor spool billed charge/attempt");
  leg(s);
  ensure(s.minor() && !s.moon() && !s.surface() && s.document().minor &&
             !s.document().world && s.resources()->jump_charges == 2 &&
             s.starting_assembly() == binding &&
             s.document().origin.state.craft == craft,
         "Actual minor arrival lost typed owner, assembly or charges");
  invalid_sources(s);
  const auto arrival = bytes(s);
  ensure(!s.set_landing_gear(true) && !s.request_landing() &&
             !s.begin_surface_walk() && !s.select_port(1) &&
             !s.mark_surface_marker() &&
             !s.select_local_jump({s.system().catalog.id,
                                   body_id(s.system().catalog.star.id)}) &&
             bytes(s) == arrival,
         "Unsupported minor action changed complete save");
  ensure(!s.advance(controls) && bytes(s) == arrival,
         "Nonfinite minor input changed save");
  static_cast<void>(approach(s));
  const auto guarded = bytes(s);
  static_cast<void>(need(s.observe()));
  ensure(bytes(s) == guarded, "Minor observation mutated authoritative save");
  static_cast<void>(withdraw(s));
  need(s.select_local_jump(home_target(s)));
  leg(s);
  ensure(!s.minor() && !s.moon() && s.document().world &&
             s.document().model == model && s.resources()->jump_charges == 1 &&
             s.starting_assembly() == binding &&
             s.document().origin.state.craft == craft &&
             s.knowledge()->entries == entries &&
             s.knowledge()->surface_marker == marker,
         "Minor return lost original home profile, history or resources");
  if (condition) {
    auto expected = *condition;
    expected.tick = s.document().flight.tick;
    ensure(s.condition() == expected,
           "Retained condition changed beyond actual clock");
  }
  need(s.select_port(1));
  ensure(s.assess_port().has_value(),
         "Returned minor voyage cannot approach real home station");
  s = continued(s);

  // Separate actual completed Moon route: adoption retains its immutable
  // original proof/bill. No charge refill or direct Moon-to-minor route.
  auto old = departure();
  const auto catalog = need(generate_secondary_body_catalog(old.system()));
  need(old.select_local_jump(
      {old.system().catalog.id, catalog.bodies[0].node.id}));
  leg(old);
  const auto on_moon = bytes(old);
  ensure(!old.select_local_jump(minor_target(old)) && bytes(old) == on_moon,
         "Direct Moon-to-minor selection was admitted");
  need(old.select_local_jump(home_target(old)));
  leg(old);
  const auto historical = need(old.travel_document());
  ensure(nlohmann::ordered_json::parse(need(encode_freedom_travel_document_json(
             historical)))["format_version"] == 45,
         "Historical Moon bytes changed format");
  const auto retained = old.travel()->local;
  const auto charges = old.resources()->jump_charges;
  need(old.select_local_jump(minor_target(old)));
  ensure(old.travel()->version == 3 && old.travel()->prior_moon &&
             (retained && old.travel()->prior_moon->local == *retained) &&
             old.resources()->jump_charges == charges,
         "Minor adoption discarded old Moon proof or refilled charges");
  auto adopted = need(old.travel_document());
  ensure(nlohmann::ordered_json::parse(need(encode_freedom_travel_document_json(
             adopted)))["format_version"] == 47,
         "Minor adoption did not select explicit47");
  adopted.travel.prior_moon->local.outbound->request.home_model.assistance =
      !adopted.travel.prior_moon->local.outbound->request.home_model.assistance;
  ensure(!validate_freedom_travel_document(adopted),
         "Tampered retained Moon source was admitted");
  old = continued(old);
}
} // namespace
int main() {
  try {
    run();
    std::cout << "Native minor journey, codecs, guard, adoption and refusal "
                 "checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
