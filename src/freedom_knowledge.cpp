#include "apsis_drift/freedom_knowledge.hpp"

#include "apsis_drift/moon_surface_landmarks.hpp"
#include "apsis_drift/physical_moon.hpp"
#include "apsis_drift/station_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <tuple>

namespace apsis_drift {
namespace {
using Error = FreedomKnowledgeError;
struct KnowledgeWorld {
  FirstUniverseRoute route;
  PhysicalLocalSystem origin;
  OriginStationDescriptor station;
  std::optional<PhysicalLocalSystem> neighbor;
  std::optional<FreedomCraftLineage> craft_lineage;
  std::optional<FreedomSystemNetwork> network;
  std::vector<PhysicalLocalSystem> additional;
  std::uint32_t chart{kFreedomStartingChartVersion};
};
auto world(const FreedomKnowledgeRecipe& r)
    -> std::expected<KnowledgeWorld, Error> {
  auto expected = FreedomKnowledgeRecipe{r.version, r.universe_seed};
  if (r.version == kFreedomObservedKnowledgeVersion ||
      r.version == kFreedomTravelKnowledgeVersion ||
      freedom_knowledge_uses_network(r.version))
    expected.observation_policy = 1;
  if (r.version == kFreedomTravelKnowledgeVersion) expected.world_domain = 1;
  if (freedom_knowledge_uses_network(r.version)) {
    expected.chart = freedom_knowledge_chart_version(r.version);
    expected.topology = 2;
    expected.world_domain = 2;
  }
  if (r.craft_lineage) {
    if ((r.version != kFreedomTravelKnowledgeVersion &&
         !freedom_knowledge_uses_network(r.version)) ||
        r.craft_lineage->version != kFreedomCraftLineageVersion ||
        !r.craft_lineage->generation ||
        !derive_freedom_craft_identity(r.universe_seed,
                                       r.craft_lineage->generation))
      return std::unexpected{Error::unsupported_recipe};
    expected.craft_lineage = r.craft_lineage;
  }
  if ((r.version != kFreedomKnowledgeVersion &&
       r.version != kFreedomObservedKnowledgeVersion &&
       r.version != kFreedomTravelKnowledgeVersion &&
       !freedom_knowledge_uses_network(r.version)) ||
      r != expected)
    return std::unexpected{Error::unsupported_recipe};
  auto origin =
      generate_physical_origin_system(r.universe_seed, r.physical_catalog);
  if (!origin) return std::unexpected{Error::unsupported_recipe};
  std::optional<PhysicalLocalSystem> neighbor;
  if (r.version == kFreedomTravelKnowledgeVersion ||
      freedom_knowledge_uses_network(r.version)) {
    const auto ids = generate_first_intersystem_identities(r.universe_seed);
    auto system = generate_physical_local_system(ids.target_system_seed,
                                                 r.physical_catalog);
    if (!system) return std::unexpected{Error::unsupported_recipe};
    neighbor = std::move(*system);
  }
  std::optional<FreedomSystemNetwork> network;
  std::vector<PhysicalLocalSystem> additional;
  if (freedom_knowledge_uses_network(r.version)) {
    auto selected = generate_freedom_system_network(
        r.universe_seed, freedom_knowledge_network_version(r.version));
    if (!selected) return std::unexpected{Error::unsupported_recipe};
    network = *selected;
    for (std::size_t i = 2; i < network->nodes.size(); ++i) {
      auto system = resolve_freedom_network_system(
          *network, network->nodes[i].system, r.physical_catalog);
      if (!system) return std::unexpected{Error::unsupported_recipe};
      additional.push_back(std::move(*system));
    }
  }
  return KnowledgeWorld{generate_first_universe_route(r.universe_seed),
                        std::move(*origin),
                        generate_origin_station(r.universe_seed),
                        std::move(neighbor),
                        r.craft_lineage,
                        network,
                        std::move(additional),
                        r.chart};
}
auto key(KnowledgeSubject s, KnowledgeFact f) {
  return std::tuple{s.kind, s.system.value, s.identity, f};
}
auto less(const KnowledgeEntry& a, const KnowledgeEntry& b) -> bool {
  return key(a.subject, a.fact) < key(b.subject, b.fact);
}
auto locate(const FreedomKnowledge& k, KnowledgeSubject s, KnowledgeFact f) {
  return std::ranges::find_if(k.entries, [&](const KnowledgeEntry& e) {
    return e.subject == s && e.fact == f;
  });
}
auto system_for(const KnowledgeWorld& w, SystemId id)
    -> const PhysicalLocalSystem* {
  if (id == w.origin.catalog.id) return &w.origin;
  if (w.neighbor && id == w.neighbor->catalog.id) return &*w.neighbor;
  const auto found = std::ranges::find_if(
      w.additional, [&](const auto& s) { return s.catalog.id == id; });
  return found == w.additional.end() ? nullptr : &*found;
}
auto moon_owner(const KnowledgeWorld& w, BodyTarget target, bool network)
    -> std::optional<PhysicalMoonDescriptor> {
  if (target.body.kind != BodyKind::moon ||
      (!network && target.system != w.origin.catalog.id) ||
      (network && !w.network))
    return {};
  const auto* system = system_for(w, target.system);
  if (!system) return {};
  const auto catalog = generate_secondary_body_catalog(*system);
  if (!catalog) return {};
  const auto moon = generate_physical_moon_descriptor(*catalog, target.body);
  if (!moon) return {};
  return *moon;
}
auto planet(const KnowledgeWorld& w, KnowledgeSubject s)
    -> const LocalSystemPlanet* {
  const auto* system = system_for(w, s.system);
  if (s.kind != KnowledgeSubjectKind::planet || !system) return nullptr;
  const auto found = std::ranges::find_if(
      system->catalog.planets, [&](const LocalSystemPlanet& p) {
        return p.descriptor.id.value == s.identity;
      });
  return found == system->catalog.planets.end() ? nullptr : &*found;
}
auto valid_subject(const KnowledgeWorld& w, KnowledgeSubject s) -> bool {
  switch (s.kind) {
    case KnowledgeSubjectKind::system:
      return s.identity == s.system.value &&
             (w.network ? find_freedom_system_network_node(*w.network, s.system)
                              .has_value()
                        : (s.system == w.route.origin ||
                           s.system == w.route.destination));
    case KnowledgeSubjectKind::planet: return planet(w, s) != nullptr;
    case KnowledgeSubjectKind::station:
      return s.system == w.route.origin && s.identity == w.station.id.value;
  }
  return false;
}
auto valid_fact(KnowledgeSubject s, KnowledgeFact f) -> bool {
  switch (f) {
    case KnowledgeFact::identity_type:
    case KnowledgeFact::location:
    case KnowledgeFact::presence: return true;
    case KnowledgeFact::physical:
    case KnowledgeFact::atmosphere:
    case KnowledgeFact::hazards:
    case KnowledgeFact::landing: return s.kind == KnowledgeSubjectKind::planet;
    case KnowledgeFact::station_ports:
      return s.kind == KnowledgeSubjectKind::station;
  }
  return false;
}
auto baseline(const KnowledgeWorld& w) -> std::vector<KnowledgeEntry> {
  std::vector<KnowledgeEntry> rows;
  const auto add = [&](KnowledgeSubject s, KnowledgeFact f) {
    if (std::ranges::any_of(rows, [&](const KnowledgeEntry& e) {
          return e.subject == s && e.fact == f;
        }))
      return;
    rows.push_back({s,
                    f,
                    {{NavigationKnowledgeLevel::resolved,
                      KnowledgeSource::starting_chart,
                      w.chart,
                      0,
                      {}}}});
  };
  std::vector<SystemId> anchors{w.route.origin, w.route.destination};
  if (w.network) {
    anchors.clear();
    for (const auto& node : w.network->nodes)
      if (node.ordinal < kFreedomSystemNetworkSize)
        anchors.push_back(node.system);
  }
  for (const auto system : anchors) {
    const KnowledgeSubject s{KnowledgeSubjectKind::system, system,
                             system.value};
    add(s, KnowledgeFact::identity_type);
    add(s, KnowledgeFact::location);
  }
  for (const auto id : std::array{
           w.origin.catalog.planets[kOriginHomePlanetOrdinal].descriptor.id,
           w.station.orbit.host_planet}) {
    const KnowledgeSubject s{KnowledgeSubjectKind::planet, w.route.origin,
                             id.value};
    add(s, KnowledgeFact::identity_type);
    add(s, KnowledgeFact::location);
  }
  if (w.chart == kFreedomPlanetaryStartingChartVersion ||
      w.chart == kFreedomUnchartedStartingChartVersion) {
    const auto add_planets = [&](const PhysicalLocalSystem& system) {
      for (const auto& body : system.catalog.planets) {
        const KnowledgeSubject subject{KnowledgeSubjectKind::planet,
                                       system.catalog.id,
                                       body.descriptor.id.value};
        add(subject, KnowledgeFact::identity_type);
        add(subject, KnowledgeFact::location);
      }
    };
    add_planets(w.origin);
    if (w.neighbor) add_planets(*w.neighbor);
    for (std::size_t i = 0; i < w.additional.size() && i < 2; ++i)
      add_planets(w.additional[i]);
  }
  const KnowledgeSubject station{KnowledgeSubjectKind::station, w.route.origin,
                                 w.station.id.value};
  add(station, KnowledgeFact::identity_type);
  add(station, KnowledgeFact::location);
  add(station, KnowledgeFact::station_ports);
  std::ranges::sort(rows, less);
  return rows;
}
auto validate_evidence(const KnowledgeWorld& w, const KnowledgeEvidence& e,
                       SimulationTick now) -> std::expected<void, Error> {
  if (!valid_subject(w, e.subject))
    return std::unexpected{Error::invalid_subject};
  if (!valid_fact(e.subject, e.fact))
    return std::unexpected{Error::invalid_fact};
  const auto& t = e.transition;
  if (now == std::numeric_limits<SimulationTick>::max() || t.tick > now)
    return std::unexpected{Error::invalid_tick};
  if (static_cast<unsigned>(t.level) >
      static_cast<unsigned>(NavigationKnowledgeLevel::visited))
    return std::unexpected{Error::invalid_transition};
  if (t.source == KnowledgeSource::starting_chart) {
    const auto expected = baseline(w);
    if (std::ranges::none_of(expected, [&](const KnowledgeEntry& b) {
          return b.subject == e.subject && b.fact == e.fact &&
                 b.transitions.front() == t;
        }))
      return std::unexpected{Error::invalid_source};
    return {};
  }
  if (t.source != KnowledgeSource::local_ship &&
      t.source != KnowledgeSource::physical_arrival)
    return std::unexpected{Error::invalid_source};
  const auto craft =
      make_freedom_new_game_document(w.route.universe_seed).state.craft;
  if (t.source_id != t.flight.value)
    return std::unexpected{Error::invalid_source};
  if (w.craft_lineage) {
    if (!t.flight.value || !craft.value)
      return std::unexpected{Error::invalid_source};
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto generation = t.flight.value >= craft.value
                                ? t.flight.value - craft.value
                                : maximum - craft.value + t.flight.value;
    if (generation > w.craft_lineage->generation)
      return std::unexpected{Error::invalid_source};
  } else if (t.flight != craft)
    return std::unexpected{Error::invalid_source};
  const bool presence = e.fact == KnowledgeFact::presence;
  if (presence != (t.source == KnowledgeSource::physical_arrival) ||
      presence != (t.level == NavigationKnowledgeLevel::visited))
    return std::unexpected{Error::invalid_transition};
  return {};
}
auto sample_key(const FreedomFieldSampleRecord& r) {
  return std::tuple{r.target.system.value,
                    static_cast<unsigned>(r.target.body.kind),
                    r.target.body.value, r.site.value};
}
auto same_sample_source(const FreedomFieldSampleRecord& a,
                        const FreedomFieldSampleRecord& b) -> bool {
  return a.target == b.target && a.cell == b.cell && a.site == b.site &&
         a.terrain_recipe == b.terrain_recipe && a.kind == b.kind;
}
auto valid_sample_craft(const KnowledgeWorld& w, StarterCraftId craft) -> bool {
  const auto starter = derive_freedom_craft_identity(w.route.universe_seed, 0);
  if (!starter || !starter->value || !craft.value) return false;
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  const auto generation = craft.value >= starter->value
                              ? craft.value - starter->value
                              : maximum - starter->value + craft.value;
  const auto derived =
      derive_freedom_craft_identity(w.route.universe_seed, generation);
  return generation <= (w.craft_lineage ? w.craft_lineage->generation : 0) &&
         derived && *derived == craft;
}
auto valid_sample_source(const FreedomKnowledge& k, const KnowledgeWorld& w,
                         const FreedomFieldSampleRecord& r, SimulationTick now)
    -> bool {
  if (r.tick > now || !valid_sample_craft(w, r.craft)) return false;
  if (r.target.body.kind == BodyKind::planet) {
    const auto* body = planet(w, {KnowledgeSubjectKind::planet, r.target.system,
                                  r.target.body.value});
    if (!body || r.target.body != body_id(body->descriptor.id) ||
        !k.surface_landmarks || r.terrain_recipe < 1 || r.terrain_recipe > 3)
      return false;
    const auto identity = identify_surface_landmark(body->descriptor, r.cell,
                                                    kSurfaceLandmarkVersion);
    if (!identity || !*identity || (*identity)->id != r.site ||
        (*identity)->kind != r.kind)
      return false;
    return std::ranges::any_of(
        k.surface_landmarks->inspections, [&](const auto& inspection) {
          return inspection.system == r.target.system &&
                 inspection.planet == body->descriptor.id &&
                 inspection.cell == r.cell && inspection.site == r.site &&
                 inspection.terrain_policy == r.terrain_recipe &&
                 inspection.tick <= r.tick;
        });
  }
  if (r.target.body.kind != BodyKind::moon || !k.moon_surface_landmarks ||
      r.terrain_recipe != kMoonTerrainRecipeVersion)
    return false;
  const auto moon = moon_owner(w, r.target,
                               k.field_samples->version ==
                                   kFreedomNetworkFieldSamplesVersion);
  if (!moon) return false;
  const auto identity = identify_moon_surface_landmark(
      *moon, r.cell, kMoonSurfaceLandmarkVersion);
  if (!identity || !*identity || (*identity)->id != r.site ||
      (*identity)->kind != r.kind)
    return false;
  return std::ranges::any_of(
      k.moon_surface_landmarks->inspections, [&](const auto& inspection) {
        return inspection.target == r.target && inspection.cell == r.cell &&
               inspection.site == r.site &&
               inspection.terrain_recipe == r.terrain_recipe &&
               inspection.tick <= r.tick;
      });
}
auto next_star_target(const KnowledgeWorld& w, SystemId source,
                      const std::vector<FreedomStarSearchRecord>& records,
                      SimulationTick started) -> std::optional<SystemId> {
  if (!w.network) return {};
  const auto from = find_freedom_system_network_node(*w.network, source);
  if (!from) return {};
  if (from->ordinal >= kFreedomSystemNetworkSize &&
      std::ranges::none_of(records, [&](const auto& r) {
        return r.target == source && r.completed_tick <= started;
      }))
    return {};
  std::optional<std::pair<std::uint64_t, SystemId>> best;
  for (const auto& node : w.network->nodes) {
    if (node.ordinal < kFreedomSystemNetworkSize ||
        std::ranges::any_of(
            records, [&](const auto& r) { return r.target == node.system; }))
      continue;
    const auto route =
        resolve_freedom_system_network_route(*w.network, source, node.system);
    if (!route) return {};
    if (!best ||
        std::pair{route->squared_distance_light_seconds, node.system.value} <
            std::pair{best->first, best->second.value})
      best = std::pair{route->squared_distance_light_seconds, node.system};
  }
  return best ? std::optional{best->second} : std::nullopt;
}
auto validate_star_search(const FreedomKnowledge& k, const KnowledgeWorld& w,
                          SimulationTick now) -> std::expected<void, Error> {
  if ((k.recipe.version == kFreedomUnchartedKnowledgeVersion) !=
      k.star_search.has_value())
    return std::unexpected{Error::invalid_star_search};
  if (!k.star_search) return {};
  const auto& state = *k.star_search;
  if (state.version != kFreedomStarSearchVersion || state.completed.size() > 2)
    return std::unexpected{Error::invalid_star_search};
  std::vector<FreedomStarSearchRecord> prior;
  for (const auto& r : state.completed) {
    if (!valid_sample_craft(w, r.craft) || r.started_tick > r.completed_tick ||
        r.completed_tick > now ||
        r.completed_tick - r.started_tick < kFreedomStarSearchTicks ||
        (!prior.empty() && r.started_tick < prior.back().completed_tick) ||
        next_star_target(w, r.source, prior, r.started_tick) != r.target)
      return std::unexpected{Error::invalid_star_search};
    for (auto fact : {KnowledgeFact::identity_type, KnowledgeFact::location}) {
      const auto entry = locate(
          k, {KnowledgeSubjectKind::system, r.target, r.target.value}, fact);
      const KnowledgeTransition expected{
          NavigationKnowledgeLevel::resolved, KnowledgeSource::local_ship,
          r.craft.value, r.completed_tick, r.craft};
      if (entry == k.entries.end() || entry->transitions.empty() ||
          entry->transitions.front() != expected)
        return std::unexpected{Error::invalid_star_search};
    }
    prior.push_back(r);
  }
  if (state.active) {
    const auto& a = *state.active;
    const auto current = derive_freedom_craft_identity(
        k.recipe.universe_seed,
        k.recipe.craft_lineage ? k.recipe.craft_lineage->generation : 0);
    if (!current || a.craft != *current || a.started_tick > a.last_tick ||
        a.last_tick > now || a.acquired_ticks >= kFreedomStarSearchTicks ||
        a.acquired_ticks > a.last_tick - a.started_tick ||
        (!prior.empty() && a.started_tick < prior.back().completed_tick) ||
        next_star_target(w, a.source, prior, a.started_tick) != a.target)
      return std::unexpected{Error::invalid_star_search};
  }
  for (const auto& entry : k.entries) {
    const auto node =
        find_freedom_system_network_node(*w.network, entry.subject.system);
    if (node && node->ordinal >= kFreedomSystemNetworkSize &&
        std::ranges::none_of(prior, [&](const auto& r) {
          return r.target == node->system && !entry.transitions.empty() &&
                 r.completed_tick <= entry.transitions.front().tick;
        }))
      return std::unexpected{Error::invalid_star_search};
  }
  return {};
}
auto validate_station_stellar_lead(const FreedomKnowledge& k,
                                   const KnowledgeWorld& w, SimulationTick now)
    -> std::expected<void, Error> {
  if (!k.station_stellar_lead) return {};
  const auto& lead = *k.station_stellar_lead;
  if (k.recipe.version != kFreedomUnchartedKnowledgeVersion || !k.star_search ||
      lead.version != kFreedomStationStellarLeadVersion ||
      lead.station != w.station.id || lead.attendant != 1 ||
      lead.received_tick > now || !valid_sample_craft(w, lead.craft))
    return std::unexpected{Error::invalid_station_stellar_lead};
  // Validate the whole ledger at now, then reconstruct only evidence that
  // existed at receipt. Later sensor fixes cannot reroll a retained report.
  std::vector<FreedomStarSearchRecord> prior;
  for (const auto& record : k.star_search->completed)
    if (record.completed_tick <= lead.received_tick) prior.push_back(record);
  if (next_star_target(w, w.origin.catalog.id, prior, lead.received_tick) !=
      lead.target)
    return std::unexpected{Error::invalid_station_stellar_lead};
  return {};
}
auto validate(const FreedomKnowledge& k, const KnowledgeWorld& w,
              SimulationTick now) -> std::expected<void, Error> {
  if (now == std::numeric_limits<SimulationTick>::max())
    return std::unexpected{Error::invalid_tick};
  if (k.entries.size() > (k.recipe.version == kFreedomUnchartedKnowledgeVersion
                              ? kMaximumFreedomUnchartedKnowledgeFacts
                          : w.network ? kMaximumFreedomNetworkKnowledgeFacts
                                      : kMaximumFreedomKnowledgeFacts))
    return std::unexpected{Error::oversized_ledger};
  if (auto v = validate_star_search(k, w, now); !v) return v;
  if (auto v = validate_station_stellar_lead(k, w, now); !v) return v;
  if (k.survey_commissions) {
    if (!freedom_knowledge_has_planetary_chart(k.recipe.version) ||
        !w.neighbor || w.neighbor->catalog.planets.empty() ||
        !validate_survey_commissions(*k.survey_commissions, now))
      return std::unexpected{Error::invalid_survey_commissions};
    const auto valid_commission = [&](const SurveyCommission& commission) {
      return commission.system == w.neighbor->catalog.id &&
             commission.planet ==
                 w.neighbor->catalog.planets.front().descriptor.id &&
             (!commission.measurement ||
              valid_sample_craft(w, commission.measurement->craft));
    };
    const auto& account = *k.survey_commissions;
    if ((account.active && !valid_commission(*account.active)) ||
        (account.latest_paid &&
         !valid_commission(account.latest_paid->commission)))
      return std::unexpected{Error::invalid_survey_commissions};
  }
  if (k.paid_operations) {
    const auto count = k.survey_commissions ? k.survey_commissions->paid_count : 0U;
    const auto generation = k.recipe.craft_lineage ? k.recipe.craft_lineage->generation : 0;
    if (!freedom_knowledge_has_planetary_chart(k.recipe.version) ||
        !validate_paid_operations(*k.paid_operations, count, now, generation) ||
        (k.paid_operations->supported_sequence &&
         (!k.survey_commissions ||
          k.paid_operations->supported_sequence >= k.survey_commissions->next_sequence)))
      return std::unexpected{Error::invalid_paid_operations};
  }
  if (k.derelicts) {
    const auto valid_craft = [&](const auto& record) {
      return !record || valid_sample_craft(w, record->craft);
    };
    if (k.recipe.version != kFreedomUnchartedKnowledgeVersion ||
        !validate_freedom_derelict_state(k.recipe.universe_seed, *k.derelicts,
                                         now) ||
        !valid_craft(k.derelicts->contact) ||
        !valid_craft(k.derelicts->inspection))
      return std::unexpected{Error::invalid_derelicts};
  }
  if (k.orbital_surveys) {
    const auto& surveys = *k.orbital_surveys;
    if ((k.recipe.version != kFreedomObservedKnowledgeVersion &&
         k.recipe.version != kFreedomTravelKnowledgeVersion &&
         !freedom_knowledge_uses_network(k.recipe.version)) ||
        surveys.version != kFreedomOrbitalSurveyVersion ||
        surveys.completed.size() > kMaximumFreedomOrbitalSurveys)
      return std::unexpected{Error::invalid_orbital_surveys};
    const auto target = [](const auto& item) {
      return std::tuple{item.system.value, item.planet.value};
    };
    const auto valid_source = [&](const auto& item) {
      const KnowledgeSubject subject{KnowledgeSubjectKind::planet, item.system,
                                     item.planet.value};
      return locate(k, subject, KnowledgeFact::identity_type) !=
                 k.entries.end() &&
             validate_evidence(w,
                               {subject,
                                KnowledgeFact::landing,
                                {NavigationKnowledgeLevel::resolved,
                                 KnowledgeSource::local_ship, item.craft.value,
                                 item.started_tick, item.craft}},
                               now)
                 .has_value();
    };
    for (std::size_t i = 0; i < surveys.completed.size(); ++i) {
      const auto& record = surveys.completed[i];
      const auto landing = locate(
          k, {KnowledgeSubjectKind::planet, record.system, record.planet.value},
          KnowledgeFact::landing);
      if (!valid_source(record) || record.completed_tick > now ||
          record.started_tick > record.completed_tick ||
          record.completed_tick - record.started_tick <
              kFreedomOrbitalSurveyTicks ||
          landing == k.entries.end() ||
          !std::ranges::any_of(
              landing->transitions,
              [&](const auto& evidence) {
                return evidence.level == NavigationKnowledgeLevel::resolved &&
                       evidence.tick <= record.completed_tick;
              }) ||
          (i && !(target(surveys.completed[i - 1]) < target(record))))
        return std::unexpected{Error::invalid_orbital_surveys};
    }
    if (surveys.active) {
      const auto& active = *surveys.active;
      if (!valid_source(active) ||
          active.acquired_ticks >= kFreedomOrbitalSurveyTicks ||
          active.started_tick > now ||
          active.acquired_ticks > now - active.started_tick ||
          std::ranges::any_of(surveys.completed, [&](const auto& record) {
            return target(record) == target(active);
          }))
        return std::unexpected{Error::invalid_orbital_surveys};
    }
  }
  if (k.surface_marker) {
    if ((k.recipe.version != kFreedomObservedKnowledgeVersion &&
         k.recipe.version != kFreedomTravelKnowledgeVersion &&
         !freedom_knowledge_uses_network(k.recipe.version)) ||
        (k.surface_marker->version != kFreedomSurfaceMarkerVersion &&
         k.surface_marker->version != kFreedomMoonSurfaceMarkerVersion &&
         k.surface_marker->version != kFreedomNetworkMoonSurfaceMarkerVersion))
      return std::unexpected{Error::invalid_surface_marker};
    if (k.surface_marker->version == kFreedomNetworkMoonSurfaceMarkerVersion &&
        !w.network)
      return std::unexpected{Error::invalid_surface_marker};
    if (k.surface_marker->point) {
      if (k.surface_marker->version == kFreedomSurfaceMarkerVersion) {
        const auto* m =
            std::get_if<FreedomSurfaceMarkerPoint>(&*k.surface_marker->point);
        if (!m) return std::unexpected{Error::invalid_surface_marker};
        const auto* body = planet(
            w, {KnowledgeSubjectKind::planet, m->system, m->planet.value});
        const auto& p = m->position;
        const auto radius = std::hypot(p.x, p.y, p.z);
        // Preserve the original planet walker's spatial envelope exactly.
        if (!body || m->tick > now || !std::isfinite(p.x) ||
            !std::isfinite(p.y) || !std::isfinite(p.z) ||
            !std::isfinite(radius) || radius > 1e8 ||
            radius <= static_cast<double>(body->descriptor.radius.value) * 1000)
          return std::unexpected{Error::invalid_surface_marker};
      } else {
        const auto* m = std::get_if<FreedomMoonSurfaceMarkerPoint>(
            &*k.surface_marker->point);
        if (!m || m->tick > now ||
            !moon_owner(w, {m->system, m->body},
                        k.surface_marker->version ==
                            kFreedomNetworkMoonSurfaceMarkerVersion))
          return std::unexpected{Error::invalid_surface_marker};
        const auto& p = m->position;
        const auto radius = std::hypot(p.x, p.y, p.z);
        // Dry negative relief is supported. This is navigation metadata,
        // not a fresh terrain/support/landing certificate or sensor fact.
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
            !std::isfinite(radius) || radius <= 1 || radius > 1e8)
          return std::unexpected{Error::invalid_surface_marker};
      }
    }
  }
  const auto planet_count =
      k.surface_landmarks ? k.surface_landmarks->inspections.size() : 0;
  const auto moon_count = k.moon_surface_landmarks
                              ? k.moon_surface_landmarks->inspections.size()
                              : 0;
  if (planet_count > kMaximumFreedomSurfaceLandmarkInspections ||
      moon_count > kMaximumFreedomSurfaceLandmarkInspections - planet_count)
    return std::unexpected{Error::invalid_surface_landmarks};
  if (k.surface_landmarks) {
    const auto& landmarks = *k.surface_landmarks;
    if ((k.recipe.version != kFreedomObservedKnowledgeVersion &&
         k.recipe.version != kFreedomTravelKnowledgeVersion &&
         !freedom_knowledge_uses_network(k.recipe.version)) ||
        landmarks.version != kFreedomSurfaceLandmarkVersion ||
        landmarks.inspections.size() >
            kMaximumFreedomSurfaceLandmarkInspections)
      return std::unexpected{Error::invalid_surface_landmarks};
    const auto inspection_key = [](const auto& inspection) {
      return std::tuple{inspection.system.value, inspection.planet.value,
                        inspection.site.value};
    };
    for (std::size_t i = 0; i < landmarks.inspections.size(); ++i) {
      const auto& inspection = landmarks.inspections[i];
      const auto* body =
          planet(w, {KnowledgeSubjectKind::planet, inspection.system,
                     inspection.planet.value});
      if (!body || inspection.tick > now || inspection.terrain_policy < 1 ||
          inspection.terrain_policy > 3 ||
          (i && !(inspection_key(landmarks.inspections[i - 1]) <
                  inspection_key(inspection))))
        return std::unexpected{Error::invalid_surface_landmarks};
      const auto identity = identify_surface_landmark(
          body->descriptor, inspection.cell, landmarks.version);
      if (!identity || !*identity || (*identity)->id != inspection.site)
        return std::unexpected{Error::invalid_surface_landmarks};
    }
  }
  if (k.moon_surface_landmarks) {
    const auto& landmarks = *k.moon_surface_landmarks;
    if ((k.recipe.version != kFreedomObservedKnowledgeVersion &&
         k.recipe.version != kFreedomTravelKnowledgeVersion &&
         !freedom_knowledge_uses_network(k.recipe.version)) ||
        (landmarks.version != kFreedomMoonSurfaceLandmarkVersion &&
         landmarks.version != kFreedomNetworkMoonSurfaceLandmarkVersion) ||
        (landmarks.version == kFreedomNetworkMoonSurfaceLandmarkVersion &&
         !w.network))
      return std::unexpected{Error::invalid_surface_landmarks};
    const auto inspection_key = [](const auto& item) {
      return std::tuple{item.target.system.value, item.target.body.version,
                        item.target.body.kind, item.target.body.value,
                        item.site.value};
    };
    for (std::size_t i = 0; i < landmarks.inspections.size(); ++i) {
      const auto& inspection = landmarks.inspections[i];
      if (inspection.tick > now ||
          inspection.terrain_recipe != kMoonTerrainRecipeVersion ||
          (i && !(inspection_key(landmarks.inspections[i - 1]) <
                  inspection_key(inspection))))
        return std::unexpected{Error::invalid_surface_landmarks};
      const auto moon = moon_owner(
          w, inspection.target,
          landmarks.version == kFreedomNetworkMoonSurfaceLandmarkVersion);
      if (!moon) return std::unexpected{Error::invalid_surface_landmarks};
      const auto identity = identify_moon_surface_landmark(
          *moon, inspection.cell, kMoonSurfaceLandmarkVersion);
      if (!identity || !*identity || (*identity)->id != inspection.site)
        return std::unexpected{Error::invalid_surface_landmarks};
    }
  }
  if (k.field_samples) {
    const auto& samples = *k.field_samples;
    const auto carrier = derive_freedom_craft_identity(
        w.route.universe_seed,
        w.craft_lineage ? w.craft_lineage->generation : 0);
    if ((k.recipe.version != kFreedomObservedKnowledgeVersion &&
         k.recipe.version != kFreedomTravelKnowledgeVersion &&
         !freedom_knowledge_uses_network(k.recipe.version)) ||
        (samples.version != kFreedomFieldSamplesVersion &&
         samples.version != kFreedomNetworkFieldSamplesVersion) ||
        (samples.version == kFreedomNetworkFieldSamplesVersion && !w.network) ||
        !carrier || samples.carrier != *carrier || !samples.carrier.value ||
        samples.carried.size() > kFreedomFieldSampleCapacity ||
        samples.journal.size() > kMaximumFreedomSurfaceLandmarkInspections)
      return std::unexpected{Error::invalid_field_samples};
    const auto valid_records = [&](const auto& records) {
      for (std::size_t i = 0; i < records.size(); ++i)
        if (!valid_sample_source(k, w, records[i], now) ||
            (i && !(sample_key(records[i - 1]) < sample_key(records[i]))))
          return false;
      return true;
    };
    if (!valid_records(samples.journal) || !valid_records(samples.carried))
      return std::unexpected{Error::invalid_field_samples};
    for (const auto& specimen : samples.carried) {
      const auto note =
          std::ranges::find_if(samples.journal, [&](const auto& r) {
            return same_sample_source(r, specimen);
          });
      if (specimen.craft != samples.carrier || note == samples.journal.end() ||
          note->tick > specimen.tick)
        return std::unexpected{Error::invalid_field_samples};
    }
  }
  for (std::size_t i = 0; i < k.entries.size(); ++i) {
    const auto& e = k.entries[i];
    if ((i && !less(k.entries[i - 1], e)) || e.transitions.empty() ||
        e.transitions.size() > kMaximumFreedomKnowledgeTransitions)
      return std::unexpected{Error::invalid_ledger};
    if (e.fact != KnowledgeFact::identity_type &&
        locate(k, e.subject, KnowledgeFact::identity_type) == k.entries.end())
      return std::unexpected{Error::invalid_ledger};
    for (std::size_t j = 0; j < e.transitions.size(); ++j) {
      const auto& t = e.transitions[j];
      if (auto v = validate_evidence(w, {e.subject, e.fact, t}, now); !v)
        return v;
      if (j && (t.level <= e.transitions[j - 1].level ||
                t.tick <= e.transitions[j - 1].tick ||
                t.source == KnowledgeSource::starting_chart))
        return std::unexpected{Error::invalid_transition};
    }
  }
  for (const auto& b : baseline(w)) {
    const auto found = locate(k, b.subject, b.fact);
    if (found == k.entries.end() ||
        found->transitions.front() != b.transitions.front())
      return std::unexpected{Error::invalid_ledger};
  }
  return {};
}
auto value(const KnowledgeWorld& w, const KnowledgeEntry& e)
    -> std::expected<KnowledgeValue, Error> {
  const auto& t = e.transitions.back();
  if (t.level < NavigationKnowledgeLevel::resolved) return std::monostate{};
  const auto* p = planet(w, e.subject);
  switch (e.fact) {
    case KnowledgeFact::identity_type:
      if (p) return KnownIdentity{p->descriptor.display_name};
      if (e.subject.kind == KnowledgeSubjectKind::station)
        return KnownIdentity{"Origin Station"};
      if (w.network) {
        const auto node =
            find_freedom_system_network_node(*w.network, e.subject.system);
        if (!node) return std::unexpected{Error::invalid_subject};
        constexpr std::array names{"Home system", "Nearby system",
                                   "Waypoint A",  "Waypoint B",
                                   "Frontier A",  "Frontier B"};
        return KnownIdentity{names[node->ordinal]};
      }
      return KnownIdentity{
          e.subject.system == w.route.origin ? "Home system" : "Nearby system"};
    case KnowledgeFact::location:
      if (e.subject.kind == KnowledgeSubjectKind::system) {
        if (w.network) {
          const auto node =
              find_freedom_system_network_node(*w.network, e.subject.system);
          if (!node) return std::unexpected{Error::invalid_subject};
          return node->position;
        }
        return e.subject.system == w.route.origin
                   ? w.route.origin_position
                   : w.route.destination_position;
      }
      if (p) {
        const auto& o = p->orbit;
        return KnownOrbit{o.radius_kilometres,    o.period_ticks,
                          o.epoch_phase_turns,    o.inclination_microdegrees,
                          o.ascending_node_turns, std::nullopt};
      } else {
        const auto& o = w.station.orbit;
        return KnownOrbit{o.radius_kilometres,    o.period_ticks,
                          o.epoch_phase_turns,    o.inclination_microdegrees,
                          o.ascending_node_turns, o.host_planet};
      }
    case KnowledgeFact::physical:
      return KnownPhysical{p->descriptor.radius.value,
                           p->descriptor.surface_gravity.value};
    case KnowledgeFact::atmosphere:
      return KnownAtmosphere{p->descriptor.atmosphere_class,
                             p->descriptor.atmosphere_pressure.value};
    case KnowledgeFact::hazards: {
      const auto* system = system_for(w, e.subject.system);
      if (!system) return std::unexpected{Error::invalid_subject};
      auto recipe = generate_planet_ambient_recipe(*system, p->descriptor.id);
      if (!recipe) return std::unexpected{Error::unsupported_recipe};
      auto ambient = resolve_planet_ambient_environment(*system, *recipe);
      if (!ambient) return std::unexpected{Error::unsupported_recipe};
      const auto& a = *ambient;
      return KnownHazards{a.minimum_temperature_millikelvin,
                          a.maximum_temperature_millikelvin,
                          a.radiation_nanosieverts_per_hour,
                          a.electric_field_volts_per_metre,
                          a.gust_millimetres_per_second,
                          a.ground_acceleration_mm_per_second2,
                          a.visibility,
                          a.maximum_obscuration_basis_points};
    }
    case KnowledgeFact::landing:
      return KnownLanding{p->descriptor.terrain_character,
                          p->descriptor.water_coverage.value};
    case KnowledgeFact::station_ports: {
      auto geometry = origin_station_geometry(w.station);
      if (!geometry) return std::unexpected{Error::unsupported_recipe};
      return KnownStationPorts{
          static_cast<std::uint32_t>(geometry->ports.size())};
    }
    case KnowledgeFact::presence: return KnownPresence{t.tick};
  }
  return std::unexpected{Error::invalid_fact};
}
} // namespace

auto make_freedom_starting_knowledge(Seed seed, std::uint32_t version)
    -> std::expected<FreedomKnowledge, FreedomKnowledgeError> {
  FreedomKnowledgeRecipe recipe{version, seed};
  if (version == kFreedomObservedKnowledgeVersion ||
      version == kFreedomTravelKnowledgeVersion ||
      freedom_knowledge_uses_network(version))
    recipe.observation_policy = 1;
  if (version == kFreedomTravelKnowledgeVersion) recipe.world_domain = 1;
  if (freedom_knowledge_uses_network(version)) {
    recipe.chart = freedom_knowledge_chart_version(version);
    recipe.topology = 2;
    recipe.world_domain = 2;
  }
  auto w = world(recipe);
  if (!w) return std::unexpected{w.error()};
  FreedomKnowledge result{recipe, baseline(*w)};
  if (version == kFreedomUnchartedKnowledgeVersion)
    result.star_search.emplace();
  return result;
}
auto validate_freedom_knowledge(const FreedomKnowledge& k, SimulationTick now)
    -> std::expected<void, FreedomKnowledgeError> {
  auto w = world(k.recipe);
  if (!w) return std::unexpected{w.error()};
  return validate(k, *w, now);
}
auto apply_freedom_knowledge(const FreedomKnowledge& k,
                             const KnowledgeEvidence& evidence,
                             SimulationTick now)
    -> std::expected<FreedomKnowledge, FreedomKnowledgeError> {
  auto w = world(k.recipe);
  if (!w) return std::unexpected{w.error()};
  if (auto v = validate(k, *w, now); !v) return std::unexpected{v.error()};
  if (auto v = validate_evidence(*w, evidence, now); !v)
    return std::unexpected{v.error()};
  auto next = k;
  auto found = std::ranges::find_if(next.entries, [&](const KnowledgeEntry& e) {
    return e.subject == evidence.subject && e.fact == evidence.fact;
  });
  if (found != next.entries.end()) {
    const auto& last = found->transitions.back();
    if (evidence.transition.level <= last.level ||
        evidence.transition.tick < last.tick)
      return next;
    if (evidence.transition.tick == last.tick)
      return std::unexpected{Error::invalid_transition};
    found->transitions.push_back(evidence.transition);
  } else {
    next.entries.push_back(
        {evidence.subject, evidence.fact, {evidence.transition}});
    std::ranges::sort(next.entries, less);
  }
  if (auto v = validate(next, *w, now); !v) return std::unexpected{v.error()};
  return next;
}
auto offer_freedom_station_stellar_lead(const FreedomKnowledge& k,
                                        StarterCraftId craft,
                                        SimulationTick now)
    -> std::expected<FreedomStationStellarLead, FreedomKnowledgeError> {
  const auto w = world(k.recipe);
  if (!w) return std::unexpected{w.error()};
  if (auto v = validate(k, *w, now); !v) return std::unexpected{v.error()};
  if (k.recipe.version != kFreedomUnchartedKnowledgeVersion || !k.star_search ||
      k.station_stellar_lead)
    return std::unexpected{Error::invalid_station_stellar_lead};
  const auto current = derive_freedom_craft_identity(
      k.recipe.universe_seed,
      k.recipe.craft_lineage ? k.recipe.craft_lineage->generation : 0);
  const auto target =
      next_star_target(*w, w->origin.catalog.id, k.star_search->completed, now);
  if (!current || craft != *current || !target)
    return std::unexpected{Error::invalid_station_stellar_lead};
  return FreedomStationStellarLead{
      kFreedomStationStellarLeadVersion, w->station.id, 1, craft, now, *target};
}
auto receive_freedom_station_stellar_lead(const FreedomKnowledge& k,
                                          const FreedomStationStellarLead& lead,
                                          SimulationTick now)
    -> std::expected<FreedomKnowledge, FreedomKnowledgeError> {
  const auto offer = offer_freedom_station_stellar_lead(k, lead.craft, now);
  if (!offer) return std::unexpected{offer.error()};
  if (*offer != lead)
    return std::unexpected{Error::invalid_station_stellar_lead};
  auto next = k;
  next.station_stellar_lead = lead;
  return next;
}
auto freedom_station_stellar_lead_status(const FreedomKnowledge& k,
                                         SimulationTick now)
    -> std::expected<FreedomStationStellarLeadStatus, FreedomKnowledgeError> {
  const auto w = world(k.recipe);
  if (!w) return std::unexpected{w.error()};
  if (auto v = validate(k, *w, now); !v) return std::unexpected{v.error()};
  using Status = FreedomStationStellarLeadStatus;
  if (!k.station_stellar_lead) return Status::absent;
  const auto target = k.station_stellar_lead->target;
  const auto presence =
      locate(k, {KnowledgeSubjectKind::system, target, target.value},
             KnowledgeFact::presence);
  if (presence != k.entries.end()) return Status::visited;
  if (std::ranges::any_of(k.star_search->completed,
                          [&](const auto& r) { return r.target == target; }))
    return Status::sensor_fix_acquired;
  return Status::unverified;
}
auto begin_freedom_star_search(const FreedomKnowledge& k, SystemId source,
                               StarterCraftId craft, SimulationTick now)
    -> std::expected<FreedomKnowledge, FreedomKnowledgeError> {
  const auto w = world(k.recipe);
  if (!w || !validate(k, *w, now) || !k.star_search || k.star_search->active)
    return std::unexpected{Error::invalid_star_search};
  const auto current = derive_freedom_craft_identity(
      k.recipe.universe_seed,
      k.recipe.craft_lineage ? k.recipe.craft_lineage->generation : 0);
  const auto target =
      next_star_target(*w, source, k.star_search->completed, now);
  if (!target || !current || craft != *current)
    return std::unexpected{Error::invalid_star_search};
  auto next = k;
  next.star_search->active =
      FreedomStarSearchAcquisition{source, *target, craft, now, now, 0};
  if (!validate(next, *w, now))
    return std::unexpected{Error::invalid_star_search};
  return next;
}
auto advance_freedom_star_search(const FreedomKnowledge& k, SystemId source,
                                 StarterCraftId craft, SimulationTick now,
                                 bool qualifying)
    -> std::expected<FreedomKnowledge, FreedomKnowledgeError> {
  const auto w = world(k.recipe);
  if (!w || !validate(k, *w, now) || !k.star_search || !k.star_search->active)
    return std::unexpected{Error::invalid_star_search};
  const auto& active = *k.star_search->active;
  if (active.source != source || active.craft != craft ||
      now < active.last_tick)
    return std::unexpected{Error::invalid_star_search};
  if (now == active.last_tick) return k;
  auto next = k;
  auto& a = *next.star_search->active;
  a.last_tick = now;
  if (qualifying) ++a.acquired_ticks;
  if (a.acquired_ticks == kFreedomStarSearchTicks) {
    const FreedomStarSearchRecord r{a.source, a.target, a.craft, a.started_tick,
                                    now};
    const KnowledgeTransition transition{NavigationKnowledgeLevel::resolved,
                                         KnowledgeSource::local_ship,
                                         craft.value, now, craft};
    for (auto fact : {KnowledgeFact::identity_type, KnowledgeFact::location})
      next.entries.push_back(
          {{KnowledgeSubjectKind::system, a.target, a.target.value},
           fact,
           {transition}});
    std::ranges::sort(next.entries, less);
    next.star_search->active.reset();
    next.star_search->completed.push_back(r);
  }
  if (!validate(next, *w, now))
    return std::unexpected{Error::invalid_star_search};
  return next;
}
auto query_freedom_knowledge(const FreedomKnowledge& k,
                             KnowledgeSubject subject, KnowledgeFact fact,
                             SimulationTick now)
    -> std::expected<std::optional<KnowledgeReading>, FreedomKnowledgeError> {
  auto w = world(k.recipe);
  if (!w) return std::unexpected{w.error()};
  if (auto v = validate(k, *w, now); !v) return std::unexpected{v.error()};
  const auto found = locate(k, subject, fact);
  if (found == k.entries.end()) return std::nullopt;
  auto known = value(*w, *found);
  if (!known) return std::unexpected{known.error()};
  return KnowledgeReading{subject, fact, found->transitions.back(),
                          std::move(*known)};
}
auto known_freedom_subjects(const FreedomKnowledge& k, SimulationTick now)
    -> std::expected<std::vector<KnowledgeSubject>, FreedomKnowledgeError> {
  if (auto v = validate_freedom_knowledge(k, now); !v)
    return std::unexpected{v.error()};
  std::vector<KnowledgeSubject> subjects;
  for (const auto& e : k.entries)
    if (e.fact == KnowledgeFact::identity_type) subjects.push_back(e.subject);
  return subjects;
}
auto resolve_freedom_known_planets(const FreedomKnowledge& k,
                                   BodyTarget current, SimulationTick now)
    -> std::expected<std::vector<FreedomKnownPlanetRow>,
                     FreedomKnowledgeError> {
  if (!freedom_knowledge_has_planetary_chart(k.recipe.version))
    return std::unexpected{Error::unsupported_recipe};
  auto w = world(k.recipe);
  if (!w) return std::unexpected{w.error()};
  if (auto valid = validate(k, *w, now); !valid)
    return std::unexpected{valid.error()};
  const KnowledgeSubject current_subject{KnowledgeSubjectKind::planet,
                                         current.system, current.body.value};
  if (current.body.version != kBodyIdentityVersion ||
      current.body.kind != BodyKind::planet || !planet(*w, current_subject))
    return std::unexpected{Error::invalid_subject};
  std::vector<FreedomKnownPlanetRow> rows;
  for (const auto& entry : k.entries) {
    if (entry.subject.kind != KnowledgeSubjectKind::planet ||
        entry.subject.system != current.system ||
        entry.fact != KnowledgeFact::identity_type ||
        entry.transitions.back().level < NavigationKnowledgeLevel::resolved)
      continue;
    const auto location = locate(k, entry.subject, KnowledgeFact::location);
    if (location == k.entries.end() ||
        location->transitions.back().level < NavigationKnowledgeLevel::resolved)
      continue;
    auto identity = value(*w, entry);
    if (!identity || !std::holds_alternative<KnownIdentity>(*identity))
      return std::unexpected{Error::invalid_ledger};
    const BodyTarget target{entry.subject.system,
                            body_id(PlanetId{entry.subject.identity})};
    const bool visited =
        locate(k, entry.subject, KnowledgeFact::presence) != k.entries.end();
    rows.push_back({target, std::get<KnownIdentity>(*identity).name,
                    target == current, visited});
  }
  const auto* catalog = system_for(*w, current.system);
  // Admission above comes from evidence. This lookup only orders admitted
  // rows by their actual orbital ordinal; it never adds a catalog subject.
  std::ranges::sort(rows, {}, [&](const FreedomKnownPlanetRow& row) {
    return std::ranges::find_if(catalog->catalog.planets,
                                [&](const auto& body) {
                                  return body.descriptor.id.value ==
                                         row.target.body.value;
                                }) -
           catalog->catalog.planets.begin();
  });
  return rows;
}
auto resolve_freedom_knowledge_chart(const FreedomKnowledge& k,
                                     SystemId current,
                                     const FreedomResources& resources,
                                     bool selection_open,
                                     const FreedomSaveDocument* instance_owner)
    -> std::expected<UniverseNavigationView, FreedomKnowledgeError> {
  if (auto v = validate_freedom_knowledge(k, resources.tick); !v)
    return std::unexpected{v.error()};
  auto view =
      [&]() -> std::expected<UniverseNavigationView, UniverseNavigationError> {
    if (freedom_knowledge_uses_network(k.recipe.version)) {
      auto network = generate_freedom_system_network(
          k.recipe.universe_seed,
          freedom_knowledge_network_version(k.recipe.version));
      if (!network)
        return std::unexpected{UniverseNavigationError::invalid_route};
      std::vector<SystemId> acquired;
      for (const auto& entry : k.entries)
        if (entry.subject.kind == KnowledgeSubjectKind::system &&
            entry.fact == KnowledgeFact::location &&
            entry.transitions.back().level >=
                NavigationKnowledgeLevel::resolved)
          acquired.push_back(entry.subject.system);
      return resolve_freedom_network_chart(*network, current, resources,
                                           selection_open, instance_owner,
                                           acquired);
    }
    return resolve_freedom_starting_chart(
        generate_first_universe_route(k.recipe.universe_seed), current,
        resources, selection_open, instance_owner);
  }();
  if (!view) return std::unexpected{Error::invalid_ledger};
  if (k.recipe.version == kFreedomUnchartedKnowledgeVersion)
    std::erase_if(view->destinations, [&](const auto& row) {
      return locate(
                 k,
                 {KnowledgeSubjectKind::system, row.system, row.system.value},
                 KnowledgeFact::location) == k.entries.end();
    });
  for (auto& row : view->destinations) {
    const auto presence =
        locate(k, {KnowledgeSubjectKind::system, row.system, row.system.value},
               KnowledgeFact::presence);
    if (presence != k.entries.end())
      row.knowledge = NavigationKnowledgeLevel::visited;
  }
  return std::move(*view);
}
} // namespace apsis_drift
