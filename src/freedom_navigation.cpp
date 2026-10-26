#include "apsis_drift/freedom_navigation.hpp"

#include <algorithm>
#include <array>

namespace apsis_drift {

auto resolve_freedom_starting_chart(const FirstUniverseRoute& route,
                                    SystemId current_system,
                                    const FreedomResources& resources,
                                    bool selection_open,
                                    const FreedomSaveDocument* instance_owner)
    -> std::expected<UniverseNavigationView, UniverseNavigationError> {
  if (!validate_first_universe_route(route))
    return std::unexpected{UniverseNavigationError::invalid_route};
  if (current_system != route.origin && current_system != route.destination)
    return std::unexpected{UniverseNavigationError::unknown_system};
  if (!validate_freedom_resources(resources))
    return std::unexpected{UniverseNavigationError::invalid_context};
  if (instance_owner) {
    if (!validate_freedom_save_document(*instance_owner) ||
        instance_owner->recipe.universe_seed != route.universe_seed ||
        instance_owner->state.craft != resources.craft ||
        instance_owner->state.tick != resources.tick)
      return std::unexpected{UniverseNavigationError::invalid_context};
  } else if (resources.craft !=
             make_freedom_new_game_document(route.universe_seed).state.craft)
    return std::unexpected{UniverseNavigationError::invalid_context};

  UniverseNavigationView view{current_system, {}};
  view.destinations.reserve(2);
  for (const auto system : std::array{route.origin, route.destination}) {
    auto row = resolve_navigation_destination(
        route, {system, NavigationKnowledgeLevel::resolved}, current_system,
        true, resources.jump_charges > 0, selection_open);
    if (!row) return std::unexpected{row.error()};
    view.destinations.push_back(*row);
  }
  return view;
}

auto resolve_freedom_network_chart(const FreedomSystemNetwork& network,
                                   SystemId current,
                                   const FreedomResources& resources,
                                   bool selection_open,
                                   const FreedomSaveDocument* owner,
                                   std::span<const SystemId> acquired)
    -> std::expected<UniverseNavigationView, UniverseNavigationError> {
  if (!validate_freedom_system_network(network))
    return std::unexpected{UniverseNavigationError::invalid_route};
  if (!find_freedom_system_network_node(network, current))
    return std::unexpected{UniverseNavigationError::unknown_system};
  if (!validate_freedom_resources(resources) || !owner ||
      !validate_freedom_save_document(*owner) || !owner->network ||
      owner->network->version != network.version ||
      owner->recipe.universe_seed != network.universe_seed ||
      owner->state.craft != resources.craft ||
      owner->state.tick != resources.tick)
    return std::unexpected{UniverseNavigationError::invalid_context};
  UniverseNavigationView view{current, {}};
  view.destinations.reserve(network.nodes.size());
  for (const auto& node : network.nodes) {
    if ((network.version == kFreedomUnchartedSystemNetworkVersion ||
         network.version == kFreedomOuterSystemNetworkVersion) &&
        std::ranges::find(acquired, node.system) == acquired.end())
      continue;
    const bool is_current = node.system == current;
    std::optional<std::uint64_t> distance;
    if (!is_current) {
      const auto route =
          resolve_freedom_system_network_route(network, current, node.system);
      if (!route)
        return std::unexpected{UniverseNavigationError::invalid_route};
      distance = route->distance_metres;
    }
    const bool affordable = resources.jump_charges > 0;
    const bool available = !is_current && affordable;
    const auto reason =
        is_current        ? NavigationDisabledReason::current_system
        : !affordable     ? NavigationDisabledReason::insufficient_endurance
        : !selection_open ? NavigationDisabledReason::unavailable_during_travel
                          : NavigationDisabledReason::none;
    view.destinations.push_back({
        .system = node.system,
        .knowledge = NavigationKnowledgeLevel::resolved,
        .known = true,
        .valid = true,
        .authorized = true,
        .affordable = affordable,
        .available = available,
        .selectable = available && selection_open,
        .disabled_reason = reason,
        .position = node.position,
        .distance_metres = distance,
    });
  }
  return view;
}

} // namespace apsis_drift
