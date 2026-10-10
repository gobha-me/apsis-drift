#include "apsis_drift/freedom_navigation.hpp"

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

} // namespace apsis_drift
