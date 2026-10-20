#include "native_minor_route_fixture.hpp"

#include <iostream>
#include <limits>

namespace {
using namespace apsis_drift;
using namespace apsis_drift::minor_route_fixture;
unsigned checks{};
auto check(bool good, const char* why) -> void {
  ++checks;
  ensure(good, why);
}
auto neighbor(const NativeFreedomFlightSession& s) -> SystemId {
  return generate_first_intersystem_identities(
             s.document().origin.recipe.universe_seed)
      .target_system;
}
auto moon_target(const NativeFreedomFlightSession& s) -> BodyTarget {
  const auto catalog = need(generate_secondary_body_catalog(s.system()));
  return {s.system().catalog.id, catalog.bodies[0].node.id};
}
auto open(const FreedomTravelSaveDocument& d) -> NativeFreedomFlightSession {
  return need(NativeFreedomFlightSession::open(
      need(native_select_save_document(NativeSaveDocument{d}))));
}
auto refusal(NativeFreedomFlightSession& s, SystemId target, const char* why)
    -> void {
  const auto before = bytes(s);
  const auto available = s.jump_selection_available(target);
  check(!available && bytes(s) == before,
        "Refused selection query preserves complete physical and saved state");
  const auto selected = s.select_jump(target);
  check(!selected && selected.error() == available.error() &&
            bytes(s) == before,
        why);
  check(bytes(continued(s)) == before,
        "Refused complete voyage remains exact through Continue");
}
auto charged_leg(NativeFreedomFlightSession& s) -> void {
  const auto tick = s.document().flight.tick;
  const auto charges = s.resources()->jump_charges;
  const auto attempt = s.travel()->next_attempt;
  need(s.begin_jump());
  s = continued(s);
  for (SimulationTick i = 1; i <= kJumpSpoolTicks + kJumpTransitTicks; ++i) {
    const auto step = need(s.advance({}));
    check(s.document().flight.tick == tick + i &&
              s.resources()->tick == tick + i,
          "Actual charged leg advances one authoritative resource/body clock");
    if (i < kJumpSpoolTicks)
      check(!step.jump_committed && s.resources()->jump_charges == charges &&
                s.travel()->next_attempt == attempt,
            "Uncommitted ordinary spool never bills a charge or attempt");
    if (i == kJumpSpoolTicks) {
      check(step.jump_committed && s.resources()->jump_charges == charges - 1 &&
                s.travel()->next_attempt == attempt + 1,
            "Ordinary commitment spends precisely one existing charge/attempt");
      refusal(
          s, neighbor(s),
          "Committed transit cannot switch destination or discard its receipt");
      s = continued(s);
    }
    if (i > kJumpSpoolTicks)
      check(step.fuel_debit_quanta == 0,
            "Existing transit has no invented propulsion bill");
    if (i == kJumpSpoolTicks + kJumpTransitTicks)
      check(step.jump_arrived,
            "Ordinary charged route installs its actual destination");
  }
  s = continued(s);
}
auto cancel_spool(NativeFreedomFlightSession& s) -> void {
  const auto charges = s.resources()->jump_charges;
  const auto attempt = s.travel()->next_attempt;
  need(s.begin_jump());
  refusal(s, neighbor(s),
          "Active uncommitted local spool refuses destination changes");
  for (unsigned i = 0; i < 2; ++i)
    need(s.advance({}));
  need(s.cancel_jump());
  check(s.travel()->phase == FreedomJumpPhase::idle &&
            s.resources()->jump_charges == charges &&
            s.travel()->next_attempt == attempt &&
            !s.travel()->local->committed && !s.travel()->local->outbound &&
            !s.travel()->local->commitment_resources,
        "Canceling a real two-tick spool leaves a choice without billed local "
        "proof");
  s = continued(s);
}
auto choose_neighbor(NativeFreedomFlightSession& s) -> void {
  const auto before = bytes(s);
  auto expected = need(s.travel_document());
  const auto destination = neighbor(s);
  need(s.jump_selection_available(destination));
  check(bytes(s) == before, "Available chart query never rewrites or migrates "
                            "the selected local voyage");
  refusal(s, s.system().catalog.id,
          "Selecting current system preserves the harmless local choice");
  refusal(s, {0}, "Unknown destination preserves all local selection metadata");
  refusal(s, {std::numeric_limits<std::uint64_t>::max()},
          "Foreign destination cannot discard local choice or saved ownership");
  expected.travel.version = kFreedomTravelStateVersion;
  expected.travel.local.reset();
  expected.travel.prior_moon.reset();
  expected.travel.origin_routes.reset();
  expected.travel.selected = destination;
  need(s.select_jump(destination));
  check(need(s.travel_document()) == expected,
        "Explicit neighbor choice changes only proof-empty local "
        "metadata/version/selection, preserving full nested "
        "body/model/actor/knowledge/resources/global proof/bill/attempt");
  check(s.travel()->version == kFreedomTravelStateVersion &&
            !s.travel()->local && !s.travel()->prior_moon &&
            !s.travel()->origin_routes && s.travel()->selected == destination,
        "Choice returns to existing version1 intersystem route without new "
        "save policy");
  s = continued(s);
  const auto selected = bytes(s);
  need(s.jump_selection_available(destination));
  need(s.select_jump(destination));
  check(bytes(s) == selected, "Repeated neighbor availability and choice "
                              "preserve complete selected Save bytes");
}
auto select_empty(NativeFreedomFlightSession& s, unsigned kind) -> void {
  if (kind == 0) {
    need(s.select_local_jump(moon_target(s)));
    check(s.travel()->version == kFreedomLocalTravelStateVersion,
          "Actual empty Moon choice selects45/version2");
  } else {
    need(s.select_local_jump(minor_target(s)));
    if (kind >= 2) {
      need(s.select_local_jump(moon_target(s)));
      if (kind == 3) need(s.select_local_jump(minor_target(s)));
      check(s.travel()->version == kFreedomOriginLocalTravelStateVersion &&
                s.travel()->origin_routes->legacy.local.selected &&
                !s.travel()->origin_routes->legacy.local.committed &&
                s.travel()->origin_routes->legacy.next_attempt ==
                    s.travel()->next_attempt,
            "Public choice/adoption/switch produces proof-empty48 with "
            "retained harmless legacy selection");
    } else
      check(s.travel()->version == kFreedomMinorTravelStateVersion,
            "Actual empty minor choice selects47/version3");
  }
}
auto empty_choices(const NativeFreedomFlightSession& departed) -> void {
  for (const unsigned kind : {0U, 1U, 2U, 3U}) {
    for (const bool canceled : {false, true}) {
      auto s = departed;
      select_empty(s, kind);
      if (canceled) cancel_spool(s);
      choose_neighbor(s);
      charged_leg(s);
      check(s.system().catalog.id == neighbor(s) && s.travel()->committed &&
                !s.travel()->local && s.resources()->jump_charges == 2,
            "Actual45/47/48 Moon/minor choice reaches ordinary charged "
            "neighbor with one real bill");
    }
  }
}
auto prior_global(const NativeFreedomFlightSession& departed) -> void {
  auto returned = departed;
  need(returned.select_jump(neighbor(returned)));
  charged_leg(returned);
  const auto ids = generate_first_intersystem_identities(
      returned.document().origin.recipe.universe_seed);
  need(returned.select_jump(ids.origin_system));
  charged_leg(returned);
  check(returned.system().catalog.id == ids.origin_system &&
            returned.travel()->committed &&
            returned.travel()->commitment_resources &&
            returned.travel()->next_attempt == 3 &&
            returned.resources()->jump_charges == 1,
        "Actual neighboring-system return owns its real retained receipt/bill "
        "and one reserve");
  for (const unsigned kind : {0U, 1U, 2U, 3U}) {
    auto s = returned;
    select_empty(s, kind);
    cancel_spool(s);
    choose_neighbor(s);
    const auto before = need(returned.travel_document());
    check(s.travel()->committed == before.travel.committed &&
              s.travel()->commitment_resources ==
                  before.travel.commitment_resources &&
              s.travel()->next_attempt == before.travel.next_attempt,
          "Changing an uncommitted local mind retains actual previous "
          "intersystem proof/bill/attempt");
    charged_leg(s);
    check(s.resources()->jump_charges == 0 && s.travel()->next_attempt == 4,
          "Ordinary neighbor travel spends only the real last reserve, without "
          "refill");
    need(s.jump_selection_available(ids.origin_system));
    need(s.select_jump(ids.origin_system));
    check(!s.jump_available() && s.resources()->jump_charges == 0 &&
              s.document().flight.tick ==
                  returned.document().flight.tick + 2 + 600,
          "Destination selection remains distinct from having fuel/charge to "
          "commit");
    static_cast<void>(continued(s));
  }
}
auto committed_local(const NativeFreedomFlightSession& departed) -> void {
  auto s = departed;
  need(s.select_local_jump(moon_target(s)));
  charged_leg(s);
  check(s.moon().has_value(),
        "Actual charged Moon route owns a genuine Moon frame");
  refusal(s, neighbor(s),
          "Away Moon owner cannot erase its real outbound proof");
  need(s.select_local_jump(home_target(s)));
  charged_leg(s);
  const auto home = need(s.travel_document());
  refusal(s, neighbor(s),
          "Completed45 local history remains a real barrier at home");
  need(s.select_local_jump(minor_target(s)));
  check(s.travel()->prior_moon && !s.travel()->local->committed,
        "Public47 adoption retains completed Moon proof behind an empty active "
        "minor choice");
  refusal(
      s, neighbor(s),
      "Prior Moon proofs cannot be dropped behind a harmless active choice");
  need(s.select_local_jump(moon_target(s)));
  check(s.travel()->origin_routes && !s.travel()->local->committed &&
            s.travel()->origin_routes->legacy.prior_moon,
        "Public48 adoption keeps genuine proofs solely in its immutable legacy "
        "span");
  refusal(s, neighbor(s),
          "Committed legacy history cannot disappear during neighbor choice");

  // Explicit codec metadata fixture: retain the actual completed Moon receipt
  // pair and current home pose; place those proofs in the typed v4 live slot.
  // This is not a claim that another physical Moon journey was flown.
  auto fixture = home;
  fixture.travel.version = kFreedomOriginLocalTravelStateVersion;
  FreedomOriginLocalRoutes routes;
  routes.legacy = {kFreedomMinorTravelStateVersion, {}, {}, 1};
  routes.excursion_outbound = fixture.travel.local->outbound;
  fixture.travel.origin_routes = routes;
  auto active = open(fixture);
  refusal(active, neighbor(active),
          "Real proof in the current v4 local/excursion slots refuses erasure");
  need(active.select_local_jump(minor_target(active)));
  check(!active.travel()->local->committed &&
            active.travel()->origin_routes->inactive.local.committed &&
            active.travel()->origin_routes->inactive.excursion_outbound,
        "Public home switch places actual Moon proof into the inactive "
        "checkpoint");
  refusal(active, neighbor(active),
          "Inactive route and excursion proofs refuse erasure behind an empty "
          "active choice");
  auto minor = departed;
  need(minor.select_local_jump(minor_target(minor)));
  charged_leg(minor);
  refusal(minor, neighbor(minor),
          "Genuine away minor owner cannot clear its typed route/history");
}
} // namespace
int main() {
  try {
    const auto departed = departure();
    empty_choices(departed);
    prior_global(departed);
    committed_local(departed);
    std::cout << checks << " native local-choice checks, 0 failures\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Native local-choice failure after " << checks
              << " checks: " << e.what() << '\n';
    return 1;
  }
}
