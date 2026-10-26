#include "apsis_drift/native_flight_session.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace apsis_drift {
namespace {
struct ChartAnchor {
  SystemId system;
  std::string name;
  UniversePositionMetres position;
};
auto components(UniversePositionMetres position)
    -> std::array<std::int64_t, 3> {
  return {position.x, position.y, position.z};
}
auto bounded_anchor(UniversePositionMetres position) -> bool {
  constexpr std::int64_t limit = std::int64_t{1} << 47U;
  return std::ranges::all_of(components(position), [](std::int64_t value) {
    return value > -limit && value < limit;
  });
}
auto admitted_anchor(const FreedomKnowledge& knowledge, SystemId system,
                     SimulationTick tick)
    -> std::expected<std::optional<ChartAnchor>, std::string> {
  const KnowledgeSubject subject{KnowledgeSubjectKind::system, system,
                                 system.value};
  const auto resolved = [&](KnowledgeFact fact) {
    const auto entry = std::ranges::find_if(
        knowledge.entries, [&](const KnowledgeEntry& value) {
          return value.subject == subject && value.fact == fact;
        });
    return entry != knowledge.entries.end() &&
           entry->transitions.back().level >=
               NavigationKnowledgeLevel::resolved;
  };
  // The complete ledger is validated by the caller. Both facts must be
  // admitted before resolving either generated identity or anchor value.
  if (!resolved(KnowledgeFact::identity_type) ||
      !resolved(KnowledgeFact::location))
    return std::nullopt;
  const auto identity = query_freedom_knowledge(
      knowledge, subject, KnowledgeFact::identity_type, tick);
  const auto location = query_freedom_knowledge(knowledge, subject,
                                                KnowledgeFact::location, tick);
  if (!identity || !*identity || !location || !*location)
    return std::unexpected{"Known chart evidence could not be resolved"};
  const auto* name = std::get_if<KnownIdentity>(&(**identity).value);
  const auto* position =
      std::get_if<UniversePositionMetres>(&(**location).value);
  if (!system.value || !name || name->name.empty() || name->name.size() > 256 ||
      name->name.find('\0') != std::string::npos || !position ||
      !bounded_anchor(*position))
    return std::unexpected{"Known chart identity or anchor is invalid"};
  return ChartAnchor{system, name->name, *position};
}
} // namespace

auto NativeFreedomFlightSession::known_chart_map_view() const
    -> std::expected<NativeKnownChartMapView, std::string> {
  NativeKnownChartMapView view;
  view.tick = document_.flight.tick;
  view.note = "No system chart is available for this journey.";
  if (resources_ && !validate_freedom_resources(*resources_, document_))
    return std::unexpected{"Known chart resource owner or clock is invalid"};
  if (knowledge_) {
    const auto& recipe = knowledge_->recipe;
    if (!validate_freedom_knowledge(*knowledge_, view.tick) ||
        recipe.universe_seed != document_.origin.recipe.universe_seed ||
        recipe.physical_catalog != document_.model.physical_catalog ||
        recipe.ephemeris != document_.model.physical_ephemeris ||
        recipe.craft_lineage != document_.origin.lineage ||
        freedom_knowledge_uses_network(recipe.version) !=
            document_.origin.network.has_value() ||
        (document_.origin.network &&
         document_.origin.network->version !=
             freedom_knowledge_network_version(recipe.version)))
      return std::unexpected{"Known chart knowledge owner or clock is invalid"};
    if (recipe.version == kFreedomOuterKnowledgeVersion) view.version = 2;
  }
  const auto current = document_.flight.frame.system;
  if (!current.value || current != system_.catalog.id ||
      (document_.world && document_.world->system != current) ||
      (document_.moon && document_.moon->target.system != current) ||
      (document_.minor && document_.minor->target.system != current))
    return std::unexpected{"Known chart current system owner is invalid"};
  if (!resources_ || !knowledge_) return view;

  const auto chart = resolve_freedom_knowledge_chart(
      *knowledge_, current, *resources_, false, &document_.origin);
  if (!chart)
    return std::unexpected{"The admitted navigation chart is invalid"};
  std::array<ChartAnchor, kNativeKnownChartMapCapacity> anchors{};
  const auto capacity = view.version == 2 ? kNativeKnownChartMapCapacity
                                          : kFreedomUnchartedSystemNetworkSize;
  std::size_t count{};
  for (const auto& row : chart->destinations) {
    const auto anchor = admitted_anchor(*knowledge_, row.system, view.tick);
    if (!anchor) return std::unexpected{anchor.error()};
    if (!*anchor) continue;
    if (count == capacity || !row.position ||
        *row.position != (**anchor).position)
      return std::unexpected{"Known chart anchor count or owner is invalid"};
    for (std::size_t i = 0; i < count; ++i)
      if (anchors[i].system == row.system)
        return std::unexpected{"Known chart contains a duplicate system"};
    anchors[count++] = **anchor;
  }
  const auto identities = generate_first_intersystem_identities(
      document_.origin.recipe.universe_seed);
  const auto home = std::ranges::find(anchors, identities.origin_system,
                                      &ChartAnchor::system);
  const auto nearby = std::ranges::find(anchors, identities.target_system,
                                        &ChartAnchor::system);
  if (home == anchors.end() || nearby == anchors.end()) {
    view.note = "Charted positions for Home and Nearby are needed to display "
                "the diagram.";
    return view;
  }
  const auto origin = components(home->position);
  const auto neighbor = components(nearby->position);
  std::optional<std::size_t> axis;
  std::int64_t separation{};
  for (std::size_t i = 0; i < origin.size(); ++i) {
    const auto delta = neighbor[i] - origin[i];
    if (!delta) continue;
    if (axis) return std::unexpected{"Known chart basis is not cardinal"};
    axis = i;
    separation = delta;
  }
  if (!axis || !separation)
    return std::unexpected{"Known chart basis has no positive separation"};
  const std::int64_t direction = separation > 0 ? 1 : -1;
  const auto distance = separation > 0 ? separation : -separation;
  const auto perpendicular = (*axis + 1) % origin.size();
  const auto remaining = (*axis + 2) % origin.size();
  const auto denominator = static_cast<double>(distance);
  const bool system_selection =
      travel_ && travel_->selected && !travel_->local &&
      (!travel_->composed ||
       travel_->composed->active == FreedomComposedRouteKind::system);
  std::array<std::array<std::int64_t, 2>, kNativeKnownChartMapCapacity>
      projected{};
  for (std::size_t i = 0; i < count; ++i) {
    const auto point = components(anchors[i].position);
    // Each admitted absolute component is below 2^47 metres. Differences
    // are below 2^48 and these integer numerators below 2^50, before
    // conversion.
    const auto a = (point[*axis] - origin[*axis]) * direction;
    const auto b = point[perpendicular] - origin[perpendicular];
    const auto c = point[remaining] - origin[remaining];
    projected[i] = {2 * a + b - c, b + c};
    for (std::size_t prior = 0; prior < i; ++prior)
      if (projected[prior] == projected[i])
        return std::unexpected{
            "Known chart projection overlaps system anchors"};
    const auto x = static_cast<double>(projected[i][0]) / (2 * denominator);
    const auto y = 0.5 - static_cast<double>(projected[i][1]) / denominator;
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 ||
        y > 1)
      return std::unexpected{"Known chart projection is outside its bounds"};
    view.nodes[i] = {anchors[i].system,
                     anchors[i].name,
                     anchors[i].system == current,
                     system_selection &&
                         anchors[i].system == *travel_->selected,
                     x,
                     y};
  }
  view.node_count = static_cast<std::uint32_t>(count);
  view.available = true;
  view.note = "Known-system diagram. Ring: current; box: selected. Numbers "
              "match the chart. Use jump controls for distance and readiness.";
  return view;
}
} // namespace apsis_drift
