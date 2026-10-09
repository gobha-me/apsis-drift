#include "apsis_drift/freedom_navigation.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>

namespace {
using namespace apsis_drift;
int checks{}, failures{};
auto check(bool ok, std::string_view message) -> void {
  ++checks;
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
auto resources(Seed seed, std::uint32_t charges) -> FreedomResources {
  return {kFreedomResourceVersion,
          make_freedom_new_game_document(seed).state.craft,
          {kWayfarerFrameId, kWayfarerFrameVersion},
          120,
          kFreedomFlightCapacityQuanta,
          charges};
}

auto chart(Seed seed) -> void {
  const auto route = generate_first_universe_route(seed);
  const auto identities = generate_first_intersystem_identities(seed);
  const auto truth_before = generate_origin_system(seed);
  for (const auto current : std::array{route.origin, route.destination}) {
    for (std::uint32_t charges = 0; charges <= 3; ++charges) {
      for (const bool open : std::array{false, true}) {
        const auto ledger = resources(seed, charges);
        const auto view =
            resolve_freedom_starting_chart(route, current, ledger, open);
        check(view && view->current_system == current &&
                  view->destinations.size() == 2,
              "Explicit chart stays bounded to the known pair");
        if (!view) continue;
        for (std::size_t i = 0; i < view->destinations.size(); ++i) {
          const auto& row = view->destinations[i];
          const bool here = row.system == current;
          check(row.system == (i == 0 ? route.origin : route.destination) &&
                    row.knowledge == NavigationKnowledgeLevel::resolved &&
                    row.known && row.valid && row.authorized &&
                    row.affordable == (charges > 0) &&
                    row.available == (!here && charges > 0) &&
                    row.selectable == (!here && charges > 0 && open) &&
                    row.position == (i == 0 ? route.origin_position
                                            : route.destination_position) &&
                    row.distance_metres ==
                        (here ? std::nullopt
                              : std::optional{route.distance_metres}),
                "Both resolved anchors remain known without invented visits");
          const auto reason =
              here       ? NavigationDisabledReason::current_system
              : !charges ? NavigationDisabledReason::insufficient_endurance
              : !open    ? NavigationDisabledReason::unavailable_during_travel
                         : NavigationDisabledReason::none;
          check(row.disabled_reason == reason,
                "Current, actual charge and travel reasons are distinct");
        }
        check(std::ranges::count_if(view->destinations,
                                    &NavigationDestinationStatus::selectable) ==
                  ((charges > 0 && open) ? 1 : 0),
              "Only the other endpoint can become pending");
        const auto again = resolve_freedom_starting_chart(
            route, current, resources(seed, charges), open);
        check(again && *again == *view,
              "Repeated chart projection changes no state");
        auto dry = ledger;
        dry.flight_quanta = 0;
        const auto dry_chart =
            resolve_freedom_starting_chart(route, current, dry, open);
        check(dry_chart && *dry_chart == *view,
              "Empty flight pool neither mints nor blocks discrete charges");
        UniverseNavigationSelectionState selection;
        selection.focused_index = current == route.origin ? 1 : 0;
        const auto selected = advance_universe_navigation_selection(
            *view, selection, UniverseNavigationSelectionCommand::select);
        check(selected.has_value() == (charges > 0 && open) &&
                  selection.pending_destination ==
                      (selected ? std::optional{current == route.origin
                                                    ? route.destination
                                                    : route.origin}
                                : std::nullopt) &&
                  ledger == resources(seed, charges),
              "Selecting or refusing preserves authoritative resources");
      }
    }
  }
  // Extra stream reads cannot perturb the selected topology or generated truth.
  for (const auto domain : std::array{SeedDomain::mission, SeedDomain::weather,
                                      SeedDomain::encounter}) {
    for (std::uint64_t ordinal = 0; ordinal < 8; ++ordinal) {
      (void)derive_seed(seed, domain, ordinal);
    }
  }
  check(route == generate_first_universe_route(seed) &&
            identities == generate_first_intersystem_identities(seed) &&
            truth_before == generate_origin_system(seed),
        "Chart reads and other streams leave immutable truth unchanged");
}

auto boundaries() -> void {
  const Seed seed{42};
  const auto route = generate_first_universe_route(seed);
  const auto ledger = resources(seed, 3);
  check(kUniverseNavigationVersion == 1 &&
            route.route_seed.value == 4490051804352235517ULL &&
            route.origin.value == 0x09683d79dbc20b52ULL &&
            route.destination.value == 0x28630482e6b15573ULL &&
            route.direction == UniverseAxisDirection::negative_z &&
            route.distance_light_seconds == 321457 &&
            route.distance_metres == 96370384171306ULL &&
            route.destination_position ==
                UniversePositionMetres{0, 0, -96370384171306LL},
        "Seed42 retains existing exact topology golden");
  const auto max = kMaximumFirstRouteLightSeconds *
                   static_cast<std::uint64_t>(kMetresPerLightSecond);
  check(max == 103608273484800ULL &&
            max < static_cast<std::uint64_t>(INT64_MAX),
        "Maximum route multiplication stays representable");
  // Neither endpoint is visited even when the caller is currently there.
  for (const auto level : std::array{NavigationKnowledgeLevel::contact,
                                     NavigationKnowledgeLevel::probable}) {
    const auto row = resolve_navigation_destination(
        route, {route.destination, level}, route.origin, true, true, true);
    check(row && !row->position && !row->distance_metres && !row->valid &&
              !row->selectable,
          "Unresolved queries do not acquire chart geometry");
  }
  const SystemId unknown{route.destination.value ^ 1ULL};
  check(!resolve_freedom_starting_chart(route, unknown, ledger) &&
            !resolve_navigation_destination(
                route, {unknown, NavigationKnowledgeLevel::resolved},
                route.origin, true, true, true),
        "Unknown IDs return refusal without generated rows");
  for (unsigned fault = 0; fault < 9; ++fault) {
    auto bad = route;
    switch (fault) {
      case 0: ++bad.universe_seed.value; break;
      case 1: ++bad.route_seed.value; break;
      case 2: bad.origin = bad.destination; break;
      case 3: bad.destination = unknown; break;
      case 4: bad.distance_light_seconds = UINT64_MAX; break;
      case 5: bad.distance_metres = UINT64_MAX; break;
      case 6: bad.destination_position.z = INT64_MIN; break;
      case 7: ++bad.origin_position.x; break;
      case 8: bad.direction = static_cast<UniverseAxisDirection>(255); break;
      default: break;
    }
    const auto result =
        resolve_freedom_starting_chart(bad, route.origin, ledger);
    check(!result && result.error() == UniverseNavigationError::invalid_route,
          "Forged or overflow-sized route refuses before projection");
  }
  for (unsigned fault = 0; fault < 6; ++fault) {
    auto bad = ledger;
    switch (fault) {
      case 0: ++bad.version; break;
      case 1: ++bad.craft.value; break;
      case 2: ++bad.frame.id.value; break;
      case 3: ++bad.frame.version; break;
      case 4: ++bad.flight_quanta; break;
      case 5: bad.jump_charges = 4; break;
      default: break;
    }
    const auto result =
        resolve_freedom_starting_chart(route, route.origin, bad);
    check(!result && result.error() == UniverseNavigationError::invalid_context,
          "Wrong owner, unsupported ledger or over-capacity refuses");
  }
}
} // namespace

auto main() -> int {
  boundaries();
  for (const Seed seed : std::array{Seed{0}, Seed{42}, Seed{UINT64_MAX}})
    chart(seed);
  for (std::uint64_t seed = 1; seed <= 64; ++seed)
    chart({seed});
  std::cout << checks << " chart checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
