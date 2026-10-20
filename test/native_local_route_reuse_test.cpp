#include "native_local_route_reuse_fixture.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
using namespace apsis_drift;
using namespace apsis_drift::minor_route_fixture;
using origin_route_reuse_fixture::capture_home;
using origin_route_reuse_fixture::service_and_depart;
int checks{};
auto check(bool good, const char* why) -> void {
  ++checks;
  ensure(good, why);
}
template <class F>
auto refuses(NativeFreedomFlightSession& session, F operation, const char* why)
    -> void {
  const auto before = bytes(session);
  check(!operation() && bytes(session) == before, why);
}
auto target(const NativeFreedomFlightSession& session,
            FreedomLocalRouteKind kind) -> BodyTarget {
  const auto catalog = need(generate_secondary_body_catalog(session.system()));
  return {
      session.system().catalog.id,
      catalog.bodies[kind == FreedomLocalRouteKind::moon ? 0U : 1U].node.id};
}
auto other(FreedomLocalRouteKind kind) -> FreedomLocalRouteKind {
  return kind == FreedomLocalRouteKind::moon ? FreedomLocalRouteKind::minor
                                             : FreedomLocalRouteKind::moon;
}
auto same_metadata(const NativeFreedomFlightSession& session,
                   const FreedomKnowledge& before) -> void {
  const auto& after = *session.knowledge();
  check(after.surface_marker == before.surface_marker &&
            after.surface_landmarks == before.surface_landmarks &&
            after.moon_surface_landmarks == before.moon_surface_landmarks,
        "Travel, service and Continue retain unrelated marker/inspection "
        "components");
}
auto selected(NativeFreedomFlightSession& session, FreedomLocalRouteKind kind)
    -> void {
  auto expected = session.document();
  const auto checksum = need(rigid_body_state_checksum(
      RigidBodyWorldContext{session.system()}, expected.flight));
  // The first explicit local selection names the already-authoritative home
  // owner. Historical absence is not a different physical world or a tick.
  // Permit only that exact metadata adoption; all remaining document fields,
  // including the full rigid body, model, origin and typed owners stay exact.
  if (!expected.world)
    expected.world = FreedomActiveWorldSelection{
        kFreedomActiveWorldVersion, session.system().catalog.id,
        expected.origin.recipe.home_planet};
  const auto resources = session.resources();
  const auto destination = target(session, kind);
  need(session.select_local_jump(destination));
  check(session.document() == expected && session.resources() == resources &&
            need(rigid_body_state_checksum(
                RigidBodyWorldContext{session.system()},
                session.document().flight)) == checksum,
        "Selecting/adopting/switching only names the actual home world; "
        "flight bits, model, origin, owners, clock and resources stay exact");
  const auto selected_bytes = bytes(session);
  need(session.select_local_jump(destination));
  check(bytes(session) == selected_bytes,
        "Repeating the selected destination preserves complete Save bytes");
  session = continued(session);
  check(need(session.local_jump_preview()).request.home_model ==
            session.document().model,
        "Every new home excursion uses the actual current flight model");
  need(session.begin_jump());
  refuses(
      session,
      [&] { return session.select_local_jump(target(session, other(kind))); },
      "Active spool refuses checkpoint switching atomically");
  refuses(
      session,
      [&] {
        return session.set_assistance(!session.document().model.assistance);
      },
      "Active spool refuses assistance changes atomically");
  NativeFlightControls invalid{};
  invalid.positive_translation.x = std::numeric_limits<double>::quiet_NaN();
  refuses(
      session, [&] { return session.advance(invalid); },
      "Invalid spool input refuses before any clock/fuel/checkpoint change");
  need(session.cancel_jump());
  check(bytes(session) == selected_bytes,
        "Uncommitted cancellation preserves selected Save exactly");
}
auto roundtrip(NativeFreedomFlightSession& session, FreedomLocalRouteKind kind,
               std::optional<FrozenLocalBodyJumpArrival>& first_v4) -> void {
  const auto home_model = session.document().model;
  const auto next = session.travel()->next_attempt;
  leg(session);
  check(session.moon().has_value() == (kind == FreedomLocalRouteKind::moon) &&
            session.minor().has_value() ==
                (kind == FreedomLocalRouteKind::minor),
        "Actual committed outbound installs the genuine selected body owner");
  check(session.travel()->local->outbound.has_value(),
        "Outbound retains a first departure proof");
  if (session.travel()->origin_routes) {
    const auto& routes = *session.travel()->origin_routes;
    check(routes.active == kind &&
              routes.excursion_outbound == session.travel()->local->committed &&
              routes.excursion_outbound->request.home_model == home_model,
          "Current excursion owns this actual outbound/model independently of "
          "the first proof");
    if (!first_v4) first_v4 = session.travel()->local->outbound;
    check(session.travel()->local->outbound == first_v4,
          "First new v4 route departure is immutable");
  }
  const auto away = target(session, kind);
  refuses(
      session, [&] { return session.select_local_jump(away); },
      "Selecting the current local body refuses without changing complete "
      "Save");
  refuses(
      session,
      [&] { return session.select_local_jump(target(session, other(kind))); },
      "Direct Moon/minor switching refuses away from home without checkpoint "
      "mutation");
  refuses(
      session, [&] { return session.replenish_resources(); },
      "A local body cannot grant free station service");
  // Actual user preference changes on an excursion must not become the
  // saved home model; the current outbound, rather than a lifetime first,
  // authenticates return. This does not write any pose or resource state.
  need(session.set_assistance(!home_model.assistance));
  need(session.select_local_jump(home_target(session)));
  check(need(session.local_jump_preview()).request.home_model == home_model,
        "Return uses the model belonging to this actual excursion");
  leg(session);
  check(!session.moon() && !session.minor() &&
            session.document().model == home_model &&
            session.travel()->next_attempt == next + 2,
        "Charged return restores the actual excursion model and advances "
        "exactly two attempts");
  if (session.travel()->origin_routes)
    check(session.travel()->local->outbound == first_v4,
          "Return does not overwrite the first v4 route departure");
  refuses(
      session, [&] { return session.select_local_jump(home_target(session)); },
      "Home-to-current-home selection refuses atomically");
}
auto charge_exhaustion_branch(const NativeFreedomFlightSession& source,
                              FreedomLocalRouteKind kind) -> void {
  check(source.resources()->jump_charges == 1,
        "One completed roundtrip leaves one genuine initial charge");
  auto branch = continued(source);
  need(branch.select_local_jump(target(branch, kind)));
  leg(branch);
  check(branch.resources()->jump_charges == 0,
        "A real third jump consumes the last charge without a service grant");
  need(branch.select_local_jump(home_target(branch)));
  refuses(
      branch, [&] { return branch.begin_jump(); },
      "Zero-charge actual local arrival cannot begin another jump");
}
auto change_uncommitted_choice(const NativeFreedomFlightSession& source)
    -> void {
  auto session = continued(source);
  const auto tick = session.document().flight.tick;
  const auto resources = session.resources();
  need(
      session.select_local_jump(target(session, FreedomLocalRouteKind::minor)));
  check(session.travel()->version == kFreedomMinorTravelStateVersion &&
            !session.travel()->local->committed &&
            !session.travel()->local->outbound,
        "An uncommitted minor choice has authentic empty old47 history");
  need(session.begin_jump());
  need(session.cancel_jump());
  const FreedomLocalTravelLegacySpan expected{
      session.travel()->version, *session.travel()->local,
      session.travel()->prior_moon, session.travel()->next_attempt};
  need(session.select_local_jump(target(session, FreedomLocalRouteKind::moon)));
  check(session.travel()->version == kFreedomOriginLocalTravelStateVersion &&
            session.travel()->origin_routes->legacy == expected &&
            session.travel()->origin_routes->inactive ==
                FreedomLocalRouteCheckpoint{} &&
            !session.travel()->local->committed &&
            !session.travel()->local->outbound &&
            !session.travel()->origin_routes->excursion_outbound &&
            session.document().flight.tick == tick &&
            session.resources() == resources,
        "Changing an uncommitted home choice retains empty legacy exactly with "
        "no trip, time or resource grant");
  session = continued(session);
  need(
      session.select_local_jump(target(session, FreedomLocalRouteKind::minor)));
  need(session.select_local_jump(target(session, FreedomLocalRouteKind::moon)));
  check(session.travel()->origin_routes->legacy == expected &&
            session.document().flight.tick == tick &&
            session.resources() == resources,
        "Empty checkpoint switches and Continue preserve the original "
        "selection-only proof state");
}
auto run_order(FreedomLocalRouteKind initial) -> void {
  auto session = departure();
  if (initial == FreedomLocalRouteKind::moon)
    change_uncommitted_choice(session);
  const auto metadata = *session.knowledge();
  const auto opposite = other(initial);
  const std::array order{initial, opposite, initial};
  std::optional<FreedomLocalTravelLegacySpan> legacy;
  std::array<std::optional<FrozenLocalBodyJumpArrival>, 2> first_new{};
  std::optional<FrozenLocalBodyJumpArrival> old_moon;
  for (std::size_t index = 0; index < order.size(); ++index) {
    const auto kind = order[index];
    const bool adopts =
        session.travel() &&
        session.travel()->version == kFreedomMinorTravelStateVersion &&
        kind == FreedomLocalRouteKind::moon;
    std::optional<FreedomLocalTravelLegacySpan> expected_legacy;
    if (adopts)
      expected_legacy = FreedomLocalTravelLegacySpan{
          session.travel()->version, *session.travel()->local,
          session.travel()->prior_moon, session.travel()->next_attempt};
    const auto bad =
        BodyTarget{SystemId{session.system().catalog.id.value ^ 1U},
                   target(session, kind).body};
    refuses(
        session, [&] { return session.select_local_jump(bad); },
        "Foreign target cannot cause an adoption/switch or alter Save");
    selected(session, kind);
    if (adopts) {
      check(session.travel()->version ==
                    kFreedomOriginLocalTravelStateVersion &&
                session.travel()->origin_routes &&
                !session.travel()->prior_moon &&
                session.travel()->origin_routes->legacy == *expected_legacy &&
                !session.travel()->local->committed &&
                !session.travel()->local->outbound &&
                !session.travel()->origin_routes->excursion_outbound &&
                session.travel()->origin_routes->inactive ==
                    FreedomLocalRouteCheckpoint{},
            "Explicit home adoption preserves exact old proofs and starts both "
            "v4 checkpoints empty");
      legacy = expected_legacy;
    } else if (!session.travel()->origin_routes) {
      check(session.travel()->version ==
                (kind == FreedomLocalRouteKind::moon
                     ? kFreedomLocalTravelStateVersion
                     : kFreedomMinorTravelStateVersion),
            "Same-kind legacy selection keeps its existing format/version");
      if (old_moon)
        check(session.travel()->prior_moon &&
                  session.travel()->prior_moon->local.outbound == old_moon,
              "Old45-to-minor retains the original Moon first proof verbatim");
    }
    roundtrip(session, kind,
              first_new[kind == FreedomLocalRouteKind::moon ? 0U : 1U]);
    if (index == 0 && kind == FreedomLocalRouteKind::moon)
      old_moon = session.travel()->local->outbound;
    if (legacy)
      check(session.travel()->origin_routes->legacy == *legacy,
            "All later commits and returns leave the legacy span unchanged");
    same_metadata(session, metadata);
    const auto encoded = bytes(session);
    check(encoded.size() <= kMaximumSaveDocumentBytes,
          "Complete encoded route history remains within the existing save "
          "bound");
    std::cout << (initial == FreedomLocalRouteKind::moon ? "moon-minor-moon"
                                                         : "minor-moon-minor")
              << " excursion=" << index + 1
              << " version=" << session.travel()->version
              << " attempt=" << session.travel()->next_attempt
              << " bytes=" << encoded.size() << '\n';
    if (index + 1 < order.size()) {
      if (index == 0) charge_exhaustion_branch(session, opposite);
      const auto ticks = capture_home(session);
      std::cout << "actual-home-capture ticks=" << ticks << '\n';
      refuses(
          session,
          [&] { return session.select_local_jump(target(session, opposite)); },
          "Attached craft cannot adopt or switch local routes");
      same_metadata(session, metadata);
      service_and_depart(session);
      // Alternate only an existing user setting. Each next outbound must bind
      // this current profile, not a stale route checkpoint or legacy model.
      need(session.set_assistance(!session.document().model.assistance));
      session = continued(session);
    }
  }
  check(session.travel()->origin_routes.has_value(),
        "Both three-excursion orders reach reusable v4");
  const auto active = session.travel()->origin_routes->active;
  const FreedomLocalRouteCheckpoint saved_active{
      *session.travel()->local,
      session.travel()->origin_routes->excursion_outbound};
  const auto saved_inactive = session.travel()->origin_routes->inactive;
  need(session.select_local_jump(target(session, other(active))));
  check(session.travel()->origin_routes->inactive == saved_active &&
            session.travel()->local->committed ==
                saved_inactive.local.committed &&
            session.travel()->local->outbound ==
                saved_inactive.local.outbound &&
            session.travel()->origin_routes->excursion_outbound ==
                saved_inactive.excursion_outbound,
        "Home switch moves the whole proof/excursion pair into the fixed "
        "inactive checkpoint");
  need(session.select_local_jump(target(session, active)));
  check(session.travel()->local->committed == saved_active.local.committed &&
            session.travel()->local->outbound == saved_active.local.outbound &&
            session.travel()->origin_routes->excursion_outbound ==
                saved_active.excursion_outbound &&
            session.travel()->origin_routes->inactive == saved_inactive,
        "Switching back restores the previous route proofs rather than "
        "inventing a new history");
  session = continued(session);
  if (legacy)
    check(session.travel()->origin_routes->legacy == *legacy,
          "Selection-only swaps preserve exact original legacy proofs");
  // A separate real last-charge branch exercises a second departure on the
  // SAME v4 route. It intentionally cannot fund its return and is not counted
  // as a fourth completed roundtrip or silently replenished.
  const auto final_source = bytes(session);
  auto branch = continued(session);
  const auto first = branch.travel()->local->outbound;
  need(branch.set_assistance(!branch.document().model.assistance));
  const auto new_model = branch.document().model;
  selected(branch, active);
  leg(branch);
  check(branch.travel()->local->outbound == first &&
            branch.travel()->origin_routes->excursion_outbound != first &&
            branch.travel()
                    ->origin_routes->excursion_outbound->request.home_model ==
                new_model,
        "A later same-route actual commit refreshes excursion/model without "
        "overwriting the first proof");
  need(branch.select_local_jump(home_target(branch)));
  check(need(branch.local_jump_preview()).request.home_model == new_model,
        "Later return preview follows the new excursion, not its different "
        "first-departure model");
  refuses(
      branch, [&] { return branch.begin_jump(); },
      "Last-charge continuation cannot invent resources for an unfunded "
      "return");
  check(bytes(session) == final_source,
        "Independent last-charge branch leaves the completed three-trip source "
        "unchanged");
}
} // namespace
int main() {
  try {
    run_order(FreedomLocalRouteKind::moon);
    run_order(FreedomLocalRouteKind::minor);
    std::cout << checks << " local-route reuse checks, 0 failures\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Route reuse failure after " << checks
              << " checks: " << e.what() << '\n';
    return 1;
  }
}
