#include "apsis_drift/universe_navigation_acceptance.hpp"

#include <algorithm>
#include <format>

namespace apsis_drift {
namespace {

[[nodiscard]] auto hash_position(const DirectTravelSample& sample) noexcept
    -> std::uint64_t {
  std::uint64_t hash{1469598103934665603ULL};
  const auto add = [&](std::uint64_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
      hash ^= static_cast<std::uint8_t>(value & 0xFFU);
      hash *= 1099511628211ULL;
      value >>= 8U;
    }
  };
  add(sample.tick);
  add(static_cast<std::uint64_t>(sample.position.x));
  add(static_cast<std::uint64_t>(sample.position.y));
  add(static_cast<std::uint64_t>(sample.position.z));
  add(sample.arrived ? 1U : 0U);
  return hash;
}

[[nodiscard]] auto one_selectable(const UniverseNavigationView& view)
    -> bool {
  return std::ranges::count_if(
             view.destinations,
             [](const NavigationDestinationStatus& destination) {
               return destination.selectable;
             }) == 1;
}

}  // namespace

auto run_universe_navigation_acceptance()
    -> std::expected<UniverseNavigationAcceptanceReport,
                     UniverseNavigationAcceptanceError> {
  const auto route =
      generate_first_universe_route(Seed{kUniverseNavigationAcceptanceSeed});
  if (!validate_first_universe_route(route)) {
    return std::unexpected{
        UniverseNavigationAcceptanceError::generation_failure};
  }

  const OnboardingProgress guided_three{
      .state = OnboardingState::guided,
      .chapter = OnboardingChapter::contract_three,
  };
  const auto guided_view = resolve_onboarding_navigation_view(
      route, guided_three, route.origin);
  const auto skipped_view = resolve_onboarding_navigation_view(
      route, initial_onboarding_progress(NewGameOnboardingChoice::skip),
      route.origin);
  const OnboardingProgress completed{
      .state = OnboardingState::completed,
      .chapter = std::nullopt,
  };
  const auto completed_view =
      resolve_onboarding_navigation_view(route, completed, route.destination);
  if (!guided_view || !skipped_view || !completed_view ||
      guided_view->destinations.size() != 2 ||
      skipped_view->destinations != guided_view->destinations ||
      completed_view->destinations.size() != 2 ||
      !one_selectable(*guided_view) || !one_selectable(*skipped_view) ||
      !one_selectable(*completed_view)) {
    return std::unexpected{
        UniverseNavigationAcceptanceError::projection_failure};
  }

  const auto plan = make_direct_travel_plan(
      route, route.origin, route.destination, 0,
      static_cast<double>(kDirectCruiseMaximumSpeedMetresPerSecond));
  if (!plan) {
    return std::unexpected{
        UniverseNavigationAcceptanceError::travel_failure};
  }
  auto tick = plan->departure_tick;
  std::uint64_t updates{};
  while (tick < plan->arrival_tick) {
    const auto next = advance_direct_travel(
        *plan, tick,
        DirectCruiseTimeScale::sixty_five_thousand_five_hundred_thirty_six);
    if (!next || *next <= tick) {
      return std::unexpected{
          UniverseNavigationAcceptanceError::travel_failure};
    }
    tick = *next;
    ++updates;
  }
  const auto arrival = resolve_direct_travel(*plan, tick);
  const auto midpoint =
      plan->departure_tick + (plan->arrival_tick - plan->departure_tick) / 2U;
  const auto projection = direct_travel_save_projection_json(
      *plan, midpoint,
      DirectCruiseTimeScale::sixty_five_thousand_five_hundred_thirty_six);
  if (!arrival || !arrival->arrived ||
      arrival->position != plan->arrival_position) {
    return std::unexpected{
        UniverseNavigationAcceptanceError::travel_failure};
  }
  if (!projection || projection->size() >= 1'024U) {
    return std::unexpected{
        UniverseNavigationAcceptanceError::persistence_failure};
  }
  const auto realtime_milliseconds =
      (updates * 1'000U + kSimulationHz - 1U) / kSimulationHz;
  return UniverseNavigationAcceptanceReport{
      .route = route,
      .visible_rows = guided_view->destinations.size(),
      .selectable_rows = 1,
      .ftl_total_ticks = kJumpSpoolTicks + kJumpTransitTicks,
      .direct_plan = *plan,
      .maximum_scale_updates = updates,
      .maximum_scale_realtime_milliseconds = realtime_milliseconds,
      .projected_save_bytes = projection->size(),
      .direct_arrival_checksum = hash_position(*arrival),
      .application_renderer_budget_ms = 1.0,
  };
}

auto universe_navigation_acceptance_json(
    const UniverseNavigationAcceptanceReport& report) -> std::string {
  const auto duration_ticks =
      report.direct_plan.arrival_tick - report.direct_plan.departure_tick;
  return std::format(
      "{{\n"
      "  \"schema_version\": 1,\n"
      "  \"scenario\": \"v0.4.34-universe-navigation-contract\",\n"
      "  \"evidence_scope\": \"application_contract\",\n"
      "  \"seed\": \"{}\",\n"
      "  \"navigation_version\": {},\n"
      "  \"route_seed\": \"{}\",\n"
      "  \"origin_system_id\": \"{}\",\n"
      "  \"destination_system_id\": \"{}\",\n"
      "  \"axis\": \"{}\",\n"
      "  \"distance_light_seconds\": \"{}\",\n"
      "  \"distance_metres\": \"{}\",\n"
      "  \"visible_rows\": {},\n"
      "  \"selectable_rows\": {},\n"
      "  \"ftl_total_ticks\": \"{}\",\n"
      "  \"direct_cruise_distance_metres\": \"{}\",\n"
      "  \"direct_speed_metres_per_second\": \"{}\",\n"
      "  \"direct_duration_ticks\": \"{}\",\n"
      "  \"maximum_time_scale\": \"{}\",\n"
      "  \"maximum_scale_updates\": \"{}\",\n"
      "  \"maximum_scale_realtime_milliseconds\": \"{}\",\n"
      "  \"projected_save_bytes\": {},\n"
      "  \"resource_cost_units\": \"0\",\n"
      "  \"direct_arrival_checksum\": \"{}\",\n"
      "  \"application_renderer_budget_ms\": {:.3f},\n"
      "  \"terminal_proxy_evidence\": "
      "\"separate-live-capture-required-by-contract-three\"\n"
      "}}\n",
      report.route.universe_seed.value, kUniverseNavigationVersion,
      report.route.route_seed.value, system_id_string(report.route.origin),
      system_id_string(report.route.destination),
      universe_axis_direction_name(report.route.direction),
      report.route.distance_light_seconds, report.route.distance_metres,
      report.visible_rows, report.selectable_rows, report.ftl_total_ticks,
      report.direct_plan.cruise_distance_metres,
      report.direct_plan.speed_metres_per_second, duration_ticks,
      kDirectCruiseMaximumTimeScale, report.maximum_scale_updates,
      report.maximum_scale_realtime_milliseconds,
      report.projected_save_bytes, report.direct_arrival_checksum,
      report.application_renderer_budget_ms);
}

}  // namespace apsis_drift
