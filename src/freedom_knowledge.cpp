#include "apsis_drift/freedom_knowledge.hpp"

#include "apsis_drift/station_geometry.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <tuple>

namespace apsis_drift {
namespace {
using Error = FreedomKnowledgeError;
struct KnowledgeWorld {
  FirstUniverseRoute route;
  PhysicalLocalSystem origin;
  OriginStationDescriptor station;
};
auto world(const FreedomKnowledgeRecipe& r)
    -> std::expected<KnowledgeWorld, Error> {
  auto expected = FreedomKnowledgeRecipe{r.version, r.universe_seed};
  if (r.version == kFreedomObservedKnowledgeVersion)
    expected.observation_policy = 1;
  if ((r.version != kFreedomKnowledgeVersion &&
       r.version != kFreedomObservedKnowledgeVersion) ||
      r != expected)
    return std::unexpected{Error::unsupported_recipe};
  auto origin =
      generate_physical_origin_system(r.universe_seed, r.physical_catalog);
  if (!origin) return std::unexpected{Error::unsupported_recipe};
  return KnowledgeWorld{generate_first_universe_route(r.universe_seed),
                        std::move(*origin),
                        generate_origin_station(r.universe_seed)};
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
auto planet(const KnowledgeWorld& w, KnowledgeSubject s)
    -> const LocalSystemPlanet* {
  if (s.kind != KnowledgeSubjectKind::planet || s.system != w.route.origin)
    return nullptr;
  const auto found = std::ranges::find_if(
      w.origin.catalog.planets, [&](const LocalSystemPlanet& p) {
        return p.descriptor.id.value == s.identity;
      });
  return found == w.origin.catalog.planets.end() ? nullptr : &*found;
}
auto valid_subject(const KnowledgeWorld& w, KnowledgeSubject s) -> bool {
  switch (s.kind) {
    case KnowledgeSubjectKind::system:
      return s.identity == s.system.value &&
             (s.system == w.route.origin || s.system == w.route.destination);
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
                      kFreedomStartingChartVersion,
                      0,
                      {}}}});
  };
  for (const auto system : std::array{w.route.origin, w.route.destination}) {
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
  if (t.source_id != craft.value || t.flight != craft)
    return std::unexpected{Error::invalid_source};
  const bool presence = e.fact == KnowledgeFact::presence;
  if (presence != (t.source == KnowledgeSource::physical_arrival) ||
      presence != (t.level == NavigationKnowledgeLevel::visited))
    return std::unexpected{Error::invalid_transition};
  return {};
}
auto validate(const FreedomKnowledge& k, const KnowledgeWorld& w,
              SimulationTick now) -> std::expected<void, Error> {
  if (now == std::numeric_limits<SimulationTick>::max())
    return std::unexpected{Error::invalid_tick};
  if (k.entries.size() > kMaximumFreedomKnowledgeFacts)
    return std::unexpected{Error::oversized_ledger};
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
      return KnownIdentity{
          e.subject.system == w.route.origin ? "Home system" : "Nearby system"};
    case KnowledgeFact::location:
      if (e.subject.kind == KnowledgeSubjectKind::system)
        return e.subject.system == w.route.origin
                   ? w.route.origin_position
                   : w.route.destination_position;
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
      auto recipe = generate_planet_ambient_recipe(w.origin, p->descriptor.id);
      if (!recipe) return std::unexpected{Error::unsupported_recipe};
      auto ambient = resolve_planet_ambient_environment(w.origin, *recipe);
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
  if (version == kFreedomObservedKnowledgeVersion)
    recipe.observation_policy = 1;
  auto w = world(recipe);
  if (!w) return std::unexpected{w.error()};
  return FreedomKnowledge{recipe, baseline(*w)};
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
auto resolve_freedom_knowledge_chart(const FreedomKnowledge& k,
                                     SystemId current,
                                     const FreedomResources& resources,
                                     bool selection_open)
    -> std::expected<UniverseNavigationView, FreedomKnowledgeError> {
  if (auto v = validate_freedom_knowledge(k, resources.tick); !v)
    return std::unexpected{v.error()};
  auto view = resolve_freedom_starting_chart(
      generate_first_universe_route(k.recipe.universe_seed), current, resources,
      selection_open);
  if (!view) return std::unexpected{Error::invalid_ledger};
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
