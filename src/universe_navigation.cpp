#include "apsis_drift/universe_navigation.hpp"

#include <cmath>
#include <format>
#include <limits>

#include "apsis_drift/seed.hpp"

namespace apsis_drift {
namespace {

enum class NavigationStream : std::uint64_t {
  direction = 1,
  distance = 2,
};

class SplitMix64 {
 public:
  explicit SplitMix64(Seed seed) noexcept : m_state{seed.value} {}

  [[nodiscard]] auto next() noexcept -> std::uint64_t {
    auto value = (m_state += 0x9E3779B97F4A7C15ULL);
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
  }

  [[nodiscard]] auto bounded(std::uint64_t exclusive_upper) noexcept
      -> std::uint64_t {
    if (exclusive_upper == 0) return 0;
    const auto threshold =
        (std::numeric_limits<std::uint64_t>::max() - exclusive_upper + 1U) %
        exclusive_upper;
    for (;;) {
      const auto value = next();
      if (value >= threshold) return value % exclusive_upper;
    }
  }

 private:
  std::uint64_t m_state{};
};

[[nodiscard]] auto valid_direction(UniverseAxisDirection direction) noexcept
    -> bool {
  switch (direction) {
    case UniverseAxisDirection::positive_x:
    case UniverseAxisDirection::negative_x:
    case UniverseAxisDirection::positive_y:
    case UniverseAxisDirection::negative_y:
    case UniverseAxisDirection::positive_z:
    case UniverseAxisDirection::negative_z: return true;
  }
  return false;
}

[[nodiscard]] auto valid_knowledge(NavigationKnowledgeLevel level) noexcept
    -> bool {
  switch (level) {
    case NavigationKnowledgeLevel::contact:
    case NavigationKnowledgeLevel::probable:
    case NavigationKnowledgeLevel::resolved:
    case NavigationKnowledgeLevel::visited: return true;
  }
  return false;
}

[[nodiscard]] auto valid_scale(DirectCruiseTimeScale scale) noexcept -> bool {
  switch (scale) {
    case DirectCruiseTimeScale::one:
    case DirectCruiseTimeScale::sixteen:
    case DirectCruiseTimeScale::two_hundred_fifty_six:
    case DirectCruiseTimeScale::four_thousand_ninety_six:
    case DirectCruiseTimeScale::sixty_five_thousand_five_hundred_thirty_six:
      return true;
  }
  return false;
}

[[nodiscard]] auto direction_vector(UniverseAxisDirection direction) noexcept
    -> UniversePositionMetres {
  switch (direction) {
    case UniverseAxisDirection::positive_x: return {1, 0, 0};
    case UniverseAxisDirection::negative_x: return {-1, 0, 0};
    case UniverseAxisDirection::positive_y: return {0, 1, 0};
    case UniverseAxisDirection::negative_y: return {0, -1, 0};
    case UniverseAxisDirection::positive_z: return {0, 0, 1};
    case UniverseAxisDirection::negative_z: return {0, 0, -1};
  }
  return {};
}

[[nodiscard]] auto position_for_system(const FirstUniverseRoute& route,
                                       SystemId system) noexcept
    -> std::optional<UniversePositionMetres> {
  if (system == route.origin) return route.origin_position;
  if (system == route.destination) return route.destination_position;
  return std::nullopt;
}

[[nodiscard]] auto safe_add(SimulationTick left, SimulationTick right) noexcept
    -> std::optional<SimulationTick> {
  if (right > std::numeric_limits<SimulationTick>::max() - left) {
    return std::nullopt;
  }
  return left + right;
}

[[nodiscard]] auto route_endpoint(const FirstUniverseRoute& route,
                                  SystemId system) noexcept
    -> std::optional<UniversePositionMetres> {
  return position_for_system(route, system);
}

[[nodiscard]] auto offset(UniversePositionMetres position,
                          UniversePositionMetres direction,
                          std::int64_t distance) noexcept
    -> UniversePositionMetres {
  return {position.x + direction.x * distance,
          position.y + direction.y * distance,
          position.z + direction.z * distance};
}

[[nodiscard]] auto direct_plan_valid(const DirectTravelPlan& plan) noexcept
    -> bool {
  if (plan.origin == plan.destination ||
      plan.arrival_tick < plan.departure_tick ||
      plan.speed_metres_per_second == 0 ||
      plan.speed_metres_per_second >
          kDirectCruiseMaximumSpeedMetresPerSecond ||
      plan.cruise_distance_metres == 0) {
    return false;
  }
  constexpr auto maximum_coordinate =
      static_cast<std::int64_t>(kMaximumFirstRouteLightSeconds) *
          kMetresPerLightSecond +
      kLocalSystemBoundaryMetres;
  const auto bounded_position = [&](UniversePositionMetres position) {
    const auto bounded = [&](std::int64_t value) {
      return value >= -maximum_coordinate && value <= maximum_coordinate;
    };
    return bounded(position.x) && bounded(position.y) && bounded(position.z);
  };
  if (!bounded_position(plan.departure_position) ||
      !bounded_position(plan.arrival_position) ||
      plan.cruise_distance_metres >
          std::numeric_limits<std::uint64_t>::max() / kSimulationHz) {
    return false;
  }
  const auto nonzero_velocity =
      (plan.velocity_metres_per_second.x != 0 ? 1 : 0) +
      (plan.velocity_metres_per_second.y != 0 ? 1 : 0) +
      (plan.velocity_metres_per_second.z != 0 ? 1 : 0);
  if (nonzero_velocity != 1) return false;
  const UniversePositionMetres delta{
      plan.arrival_position.x - plan.departure_position.x,
      plan.arrival_position.y - plan.departure_position.y,
      plan.arrival_position.z - plan.departure_position.z,
  };
  const auto axis_matches = [&](std::int64_t position_delta,
                                std::int64_t velocity) {
    return velocity >=
               -static_cast<std::int64_t>(
                   kDirectCruiseMaximumSpeedMetresPerSecond) &&
           velocity <= static_cast<std::int64_t>(
                           kDirectCruiseMaximumSpeedMetresPerSecond) &&
           velocity != 0 &&
           std::abs(velocity) ==
               static_cast<std::int64_t>(plan.speed_metres_per_second) &&
           ((position_delta > 0) == (velocity > 0)) &&
           static_cast<std::uint64_t>(std::abs(position_delta)) ==
               plan.cruise_distance_metres;
  };
  const bool geometry_matches =
      (axis_matches(delta.x, plan.velocity_metres_per_second.x) &&
       delta.y == 0 && delta.z == 0) ||
      (axis_matches(delta.y, plan.velocity_metres_per_second.y) &&
       delta.x == 0 && delta.z == 0) ||
      (axis_matches(delta.z, plan.velocity_metres_per_second.z) &&
       delta.x == 0 && delta.y == 0);
  const auto numerator = plan.cruise_distance_metres * kSimulationHz;
  const auto expected_duration =
      numerator / plan.speed_metres_per_second +
      (numerator % plan.speed_metres_per_second != 0);
  return geometry_matches &&
         plan.arrival_tick - plan.departure_tick == expected_duration;
}

}  // namespace

auto universe_axis_direction_name(UniverseAxisDirection direction) noexcept
    -> std::string_view {
  switch (direction) {
    case UniverseAxisDirection::positive_x: return "+x";
    case UniverseAxisDirection::negative_x: return "-x";
    case UniverseAxisDirection::positive_y: return "+y";
    case UniverseAxisDirection::negative_y: return "-y";
    case UniverseAxisDirection::positive_z: return "+z";
    case UniverseAxisDirection::negative_z: return "-z";
  }
  return "invalid";
}

auto navigation_knowledge_level_name(NavigationKnowledgeLevel level) noexcept
    -> std::string_view {
  switch (level) {
    case NavigationKnowledgeLevel::contact: return "contact";
    case NavigationKnowledgeLevel::probable: return "probable";
    case NavigationKnowledgeLevel::resolved: return "resolved";
    case NavigationKnowledgeLevel::visited: return "visited";
  }
  return "invalid";
}

auto navigation_disabled_reason_name(NavigationDisabledReason reason) noexcept
    -> std::string_view {
  switch (reason) {
    case NavigationDisabledReason::none: return "none";
    case NavigationDisabledReason::current_system: return "current-system";
    case NavigationDisabledReason::requires_resolved_position:
      return "requires-resolved-position";
    case NavigationDisabledReason::onboarding_locked:
      return "onboarding-locked";
    case NavigationDisabledReason::insufficient_endurance:
      return "insufficient-endurance";
    case NavigationDisabledReason::unavailable_during_travel:
      return "unavailable-during-travel";
  }
  return "invalid";
}

auto generate_first_universe_route(Seed universe_seed) noexcept
    -> FirstUniverseRoute {
  const auto identities = generate_first_intersystem_identities(universe_seed);
  const auto route_seed = derive_seed(
      universe_seed, SeedDomain::navigation, kFirstTargetSystemOrdinal);
  SplitMix64 direction_random{derive_seed(
      route_seed, SeedDomain::navigation,
      static_cast<std::uint64_t>(NavigationStream::direction))};
  SplitMix64 distance_random{derive_seed(
      route_seed, SeedDomain::navigation,
      static_cast<std::uint64_t>(NavigationStream::distance))};
  const auto direction = static_cast<UniverseAxisDirection>(
      direction_random.bounded(6));
  const auto distance_light_seconds =
      kMinimumFirstRouteLightSeconds +
      distance_random.bounded(kMaximumFirstRouteLightSeconds -
                                  kMinimumFirstRouteLightSeconds +
                              1U);
  const auto distance_metres =
      distance_light_seconds *
      static_cast<std::uint64_t>(kMetresPerLightSecond);
  const auto unit = direction_vector(direction);
  const auto signed_distance = static_cast<std::int64_t>(distance_metres);
  return {
      .universe_seed = universe_seed,
      .route_seed = route_seed,
      .origin = identities.origin_system,
      .destination = identities.target_system,
      .direction = direction,
      .distance_light_seconds = distance_light_seconds,
      .distance_metres = distance_metres,
      .origin_position = {},
      .destination_position = {unit.x * signed_distance,
                               unit.y * signed_distance,
                               unit.z * signed_distance},
  };
}

auto validate_first_universe_route(const FirstUniverseRoute& route) noexcept
    -> std::expected<void, UniverseNavigationError> {
  if (!valid_direction(route.direction) || route.origin == route.destination ||
      route.distance_light_seconds < kMinimumFirstRouteLightSeconds ||
      route.distance_light_seconds > kMaximumFirstRouteLightSeconds ||
      route.distance_metres <=
          static_cast<std::uint64_t>(2 * kLocalSystemBoundaryMetres) ||
      route != generate_first_universe_route(route.universe_seed)) {
    return std::unexpected{UniverseNavigationError::invalid_route};
  }
  return {};
}

auto resolve_navigation_destination(
    const FirstUniverseRoute& route, NavigationKnowledge knowledge,
    SystemId current_system, bool authorized, bool affordable,
    bool selection_open) noexcept
    -> std::expected<NavigationDestinationStatus, UniverseNavigationError> {
  if (!validate_first_universe_route(route)) {
    return std::unexpected{UniverseNavigationError::invalid_route};
  }
  if (!valid_knowledge(knowledge.level)) {
    return std::unexpected{UniverseNavigationError::invalid_knowledge};
  }
  const auto position = position_for_system(route, knowledge.system);
  const auto current_position = position_for_system(route, current_system);
  if (!position || !current_position) {
    return std::unexpected{UniverseNavigationError::unknown_system};
  }
  const bool resolved =
      knowledge.level == NavigationKnowledgeLevel::resolved ||
      knowledge.level == NavigationKnowledgeLevel::visited;
  const bool current = knowledge.system == current_system;
  const bool available = resolved && authorized && affordable && !current;
  NavigationDisabledReason reason{NavigationDisabledReason::none};
  if (current) {
    reason = NavigationDisabledReason::current_system;
  } else if (!resolved) {
    reason = NavigationDisabledReason::requires_resolved_position;
  } else if (!authorized) {
    reason = NavigationDisabledReason::onboarding_locked;
  } else if (!affordable) {
    reason = NavigationDisabledReason::insufficient_endurance;
  } else if (!selection_open) {
    reason = NavigationDisabledReason::unavailable_during_travel;
  }
  return NavigationDestinationStatus{
      .system = knowledge.system,
      .knowledge = knowledge.level,
      .known = true,
      .valid = resolved,
      .authorized = authorized,
      .affordable = affordable,
      .available = available,
      .selectable = available && selection_open,
      .disabled_reason = reason,
      .position = resolved ? position : std::nullopt,
      .distance_metres = resolved && !current
                             ? std::optional{route.distance_metres}
                             : std::nullopt,
  };
}

auto resolve_onboarding_navigation_view(
    const FirstUniverseRoute& route, const OnboardingProgress& onboarding,
    SystemId current_system, bool affordable, bool selection_open) noexcept
    -> std::expected<UniverseNavigationView, UniverseNavigationError> {
  if (!validate_onboarding_progress(onboarding)) {
    return std::unexpected{UniverseNavigationError::invalid_onboarding};
  }
  if (!validate_first_universe_route(route)) {
    return std::unexpected{UniverseNavigationError::invalid_route};
  }
  if (!route_endpoint(route, current_system)) {
    return std::unexpected{UniverseNavigationError::unknown_system};
  }
  const auto access = resolve_onboarding_access(onboarding);
  if (!access) {
    return std::unexpected{UniverseNavigationError::invalid_onboarding};
  }
  if (current_system == route.destination &&
      !access->first_jump_solution_available) {
    return std::unexpected{UniverseNavigationError::invalid_context};
  }
  UniverseNavigationView view{
      .current_system = current_system,
      .destinations = {},
  };
  view.destinations.reserve(2);
  const auto origin = resolve_navigation_destination(
      route,
      {.system = route.origin,
       .level = NavigationKnowledgeLevel::visited},
      current_system, access->first_jump_solution_available, affordable,
      selection_open);
  if (!origin) return std::unexpected{origin.error()};
  view.destinations.push_back(*origin);

  if (access->first_jump_solution_available) {
    const auto target = resolve_navigation_destination(
        route,
        {.system = route.destination,
         .level = current_system == route.destination
                      ? NavigationKnowledgeLevel::visited
                      : NavigationKnowledgeLevel::resolved},
        current_system, true, affordable, selection_open);
    if (!target) return std::unexpected{target.error()};
    view.destinations.push_back(*target);
  }
  return view;
}

auto make_direct_travel_plan(const FirstUniverseRoute& route, SystemId origin,
                             SystemId destination,
                             SimulationTick departure_tick,
                             double speed_metres_per_second)
    -> std::expected<DirectTravelPlan, UniverseNavigationError> {
  if (!validate_first_universe_route(route)) {
    return std::unexpected{UniverseNavigationError::invalid_route};
  }
  if (!std::isfinite(speed_metres_per_second) ||
      speed_metres_per_second < 1.0 ||
      speed_metres_per_second >
          static_cast<double>(kDirectCruiseMaximumSpeedMetresPerSecond) ||
      std::trunc(speed_metres_per_second) != speed_metres_per_second) {
    return std::unexpected{UniverseNavigationError::invalid_speed};
  }
  const auto from = route_endpoint(route, origin);
  const auto to = route_endpoint(route, destination);
  if (!from || !to) {
    return std::unexpected{UniverseNavigationError::unknown_system};
  }
  if (origin == destination) {
    return std::unexpected{UniverseNavigationError::invalid_context};
  }
  auto direction = direction_vector(route.direction);
  if (origin == route.destination) {
    direction = {-direction.x, -direction.y, -direction.z};
  }
  const auto cruise_distance =
      route.distance_metres -
      static_cast<std::uint64_t>(2 * kLocalSystemBoundaryMetres);
  if (cruise_distance >
      std::numeric_limits<std::uint64_t>::max() / kSimulationHz) {
    return std::unexpected{UniverseNavigationError::unsafe_arithmetic};
  }
  const auto speed = static_cast<std::uint64_t>(speed_metres_per_second);
  const auto numerator = cruise_distance * kSimulationHz;
  const auto duration_ticks = numerator / speed + (numerator % speed != 0);
  const auto arrival_tick = safe_add(departure_tick, duration_ticks);
  if (!arrival_tick) {
    return std::unexpected{UniverseNavigationError::tick_overflow};
  }
  const auto departure_position =
      offset(*from, direction, kLocalSystemBoundaryMetres);
  const auto arrival_position =
      offset(*to, direction, -kLocalSystemBoundaryMetres);
  const auto signed_speed = static_cast<std::int64_t>(speed);
  return DirectTravelPlan{
      .origin = origin,
      .destination = destination,
      .departure_tick = departure_tick,
      .arrival_tick = *arrival_tick,
      .cruise_distance_metres = cruise_distance,
      .speed_metres_per_second = speed,
      .departure_position = departure_position,
      .arrival_position = arrival_position,
      .velocity_metres_per_second = {direction.x * signed_speed,
                                     direction.y * signed_speed,
                                     direction.z * signed_speed},
  };
}

auto resolve_direct_travel(const DirectTravelPlan& plan,
                           SimulationTick tick) noexcept
    -> std::expected<DirectTravelSample, UniverseNavigationError> {
  if (!direct_plan_valid(plan) || tick < plan.departure_tick ||
      tick > plan.arrival_tick) {
    return std::unexpected{UniverseNavigationError::invalid_context};
  }
  if (tick == plan.arrival_tick) {
    return DirectTravelSample{
        .tick = tick, .position = plan.arrival_position, .arrived = true};
  }
  const auto elapsed_ticks = tick - plan.departure_tick;
  if (elapsed_ticks >
      std::numeric_limits<std::uint64_t>::max() /
          plan.speed_metres_per_second) {
    return std::unexpected{UniverseNavigationError::unsafe_arithmetic};
  }
  const auto travelled =
      elapsed_ticks * plan.speed_metres_per_second / kSimulationHz;
  const auto direction = UniversePositionMetres{
      plan.velocity_metres_per_second.x == 0
          ? 0
          : (plan.velocity_metres_per_second.x > 0 ? 1 : -1),
      plan.velocity_metres_per_second.y == 0
          ? 0
          : (plan.velocity_metres_per_second.y > 0 ? 1 : -1),
      plan.velocity_metres_per_second.z == 0
          ? 0
          : (plan.velocity_metres_per_second.z > 0 ? 1 : -1),
  };
  const auto signed_travelled = static_cast<std::int64_t>(travelled);
  return DirectTravelSample{
      .tick = tick,
      .position = offset(plan.departure_position, direction,
                         signed_travelled),
      .arrived = false,
  };
}

auto advance_direct_travel(const DirectTravelPlan& plan, SimulationTick tick,
                           DirectCruiseTimeScale scale) noexcept
    -> std::expected<SimulationTick, UniverseNavigationError> {
  if (!direct_plan_valid(plan) || tick < plan.departure_tick ||
      tick > plan.arrival_tick) {
    return std::unexpected{UniverseNavigationError::invalid_context};
  }
  if (!valid_scale(scale)) {
    return std::unexpected{UniverseNavigationError::invalid_time_scale};
  }
  if (tick == plan.arrival_tick) return tick;
  const auto ticks = static_cast<SimulationTick>(scale);
  if (ticks >= plan.arrival_tick - tick) return plan.arrival_tick;
  return tick + ticks;
}

auto direct_travel_save_projection_json(const DirectTravelPlan& plan,
                                        SimulationTick tick,
                                        DirectCruiseTimeScale scale)
    -> std::expected<std::string, UniverseNavigationError> {
  const auto sample = resolve_direct_travel(plan, tick);
  if (!sample) return std::unexpected{sample.error()};
  if (!valid_scale(scale)) {
    return std::unexpected{UniverseNavigationError::invalid_time_scale};
  }
  return std::format(
      "{{\n"
      "  \"navigation_version\": {},\n"
      "  \"origin_system_id\": \"{}\",\n"
      "  \"destination_system_id\": \"{}\",\n"
      "  \"departure_tick\": \"{}\",\n"
      "  \"arrival_tick\": \"{}\",\n"
      "  \"current_tick\": \"{}\",\n"
      "  \"position_metres\": {{\"x\": \"{}\", \"y\": \"{}\", "
      "\"z\": \"{}\"}},\n"
      "  \"velocity_metres_per_second\": {{\"x\": \"{}\", \"y\": "
      "\"{}\", \"z\": \"{}\"}},\n"
      "  \"time_scale\": \"{}\"\n"
      "}}\n",
      kUniverseNavigationVersion, system_id_string(plan.origin),
      system_id_string(plan.destination), plan.departure_tick,
      plan.arrival_tick, tick, sample->position.x, sample->position.y,
      sample->position.z, plan.velocity_metres_per_second.x,
      plan.velocity_metres_per_second.y, plan.velocity_metres_per_second.z,
      static_cast<std::uint32_t>(scale));
}

}  // namespace apsis_drift
