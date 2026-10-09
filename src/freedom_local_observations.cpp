#include "apsis_drift/freedom_local_observations.hpp"

#include <cmath>
#include <type_traits>

namespace apsis_drift {
namespace {
using Error = FreedomKnowledgeError;
struct PilotContext {
  bool aboard{true};
  bool attached{};
};
auto pilot_context(const FreedomSurfaceBaseSave& base) -> PilotContext {
  return std::visit(
      [](const auto& d) -> PilotContext {
        using T = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<T, FreedomFlightSaveDocument>)
          return {};
        else if constexpr (std::is_same_v<T, FreedomDockingSaveDocument>)
          return {true, d.docking.attached};
        else if constexpr (std::is_same_v<T, FreedomJourneySaveDocument>)
          return {false, d.voyage.docking.attached};
        else if constexpr (std::is_same_v<T,
                                          FreedomStartingAssemblySaveDocument>)
          return {false, d.journey.voyage.docking.attached};
        else
          return {d.boarding.phase == FreedomBoardingPhase::seated,
                  d.voyage.docking.attached};
      },
      base);
}
auto proximity(double distance, double radius)
    -> std::optional<NavigationKnowledgeLevel> {
  if (distance <= 2 * radius) return NavigationKnowledgeLevel::resolved;
  if (distance <= 10 * radius) return NavigationKnowledgeLevel::probable;
  if (distance <= 100 * radius) return NavigationKnowledgeLevel::contact;
  return std::nullopt;
}
} // namespace
auto observe_freedom_local_ship(const FreedomKnowledge& knowledge,
                                const FreedomSurfaceSaveDocument& voyage,
                                LocalObservationEvent event)
    -> std::expected<FreedomKnowledge, FreedomKnowledgeError> {
  const auto flight = surface_base_flight(voyage.base);
  if (!validate_freedom_knowledge(knowledge, flight.flight.tick) ||
      !validate_freedom_surface_document(voyage))
    return std::unexpected{Error::invalid_ledger};
  if ((knowledge.recipe.version != kFreedomObservedKnowledgeVersion &&
       knowledge.recipe.version != kFreedomTravelKnowledgeVersion) ||
      knowledge.recipe.observation_policy != kFreedomLocalObservationPolicy)
    return std::unexpected{Error::unsupported_recipe};
  if (knowledge.recipe.universe_seed != flight.origin.recipe.universe_seed ||
      knowledge.recipe.physical_catalog != flight.model.physical_catalog ||
      knowledge.recipe.ephemeris != flight.model.physical_ephemeris)
    return std::unexpected{Error::invalid_subject};
  if (event != LocalObservationEvent::sensor_tick &&
      event != LocalObservationEvent::touchdown &&
      event != LocalObservationEvent::port_capture &&
      event != LocalObservationEvent::system_arrival)
    return std::unexpected{Error::invalid_source};
  const auto pilot = pilot_context(voyage.base);
  if ((event == LocalObservationEvent::touchdown &&
       (!pilot.aboard || !voyage.surface.landed)) ||
      (event == LocalObservationEvent::port_capture &&
       (!pilot.aboard || !pilot.attached)) ||
      (event == LocalObservationEvent::system_arrival &&
       (!pilot.aboard || pilot.attached || voyage.surface.landed ||
        !flight.world ||
        knowledge.recipe.version != kFreedomTravelKnowledgeVersion)))
    return std::unexpected{Error::invalid_source};
  if (!pilot.aboard ||
      (event == LocalObservationEvent::sensor_tick &&
       (flight.flight.tick == 0 ||
        flight.flight.tick % kFreedomLocalObservationInterval != 0)))
    return knowledge;
  auto hydrated = hydrate_freedom_flight_document(flight);
  if (!hydrated) return std::unexpected{Error::invalid_ledger};
  const auto& system = hydrated->system;
  const auto origin = system.catalog.id;
  const auto home_system =
      generate_first_universe_route(knowledge.recipe.universe_seed).origin;
  if (knowledge.recipe.version == kFreedomObservedKnowledgeVersion &&
      origin != home_system)
    return std::unexpected{Error::invalid_subject};
  const auto home = resolve_planet_ephemeris(
      system, *flight.flight.frame.planet, {flight.flight.tick, 0});
  if (!home) return std::unexpected{Error::invalid_subject};
  auto next = knowledge;
  const auto record = [&](KnowledgeSubject subject, KnowledgeFact fact,
                          NavigationKnowledgeLevel level) -> bool {
    const auto craft = flight.origin.state.craft;
    const KnowledgeEvidence evidence{subject,
                                     fact,
                                     {level,
                                      fact == KnowledgeFact::presence
                                          ? KnowledgeSource::physical_arrival
                                          : KnowledgeSource::local_ship,
                                      craft.value, flight.flight.tick, craft}};
    auto candidate =
        apply_freedom_knowledge(next, evidence, flight.flight.tick);
    if (!candidate) return false;
    next = std::move(*candidate);
    return true;
  };
  // Translation is kept relative to the home body so its close-range precision
  // is never lost to adding/subtracting two large system-space coordinates.
  for (const auto& body : system.catalog.planets) {
    const auto ephemeris = resolve_planet_ephemeris(system, body.descriptor.id,
                                                    {flight.flight.tick, 0});
    if (!ephemeris) return std::unexpected{Error::invalid_subject};
    const auto& position = flight.flight.position_metres;
    const double dx = (home->position.x - ephemeris->position.x) + position.x;
    const double dy = (home->position.y - ephemeris->position.y) + position.y;
    const double dz = (home->position.z - ephemeris->position.z) + position.z;
    const double distance = std::sqrt((dx * dx + dy * dy) + dz * dz);
    const double radius =
        static_cast<double>(body.descriptor.radius.value) * 1000;
    if (!std::isfinite(distance))
      return std::unexpected{Error::invalid_transition};
    const auto level = proximity(distance, radius);
    if (!level) continue;
    const KnowledgeSubject subject{KnowledgeSubjectKind::planet, origin,
                                   body.descriptor.id.value};
    for (const auto fact :
         {KnowledgeFact::identity_type, KnowledgeFact::location,
          KnowledgeFact::physical, KnowledgeFact::atmosphere})
      if (!record(subject, fact, *level))
        return std::unexpected{Error::invalid_transition};
    // Close sensing infers coarse surface envelopes. These never certify live
    // weather, engineering safety or a landing pad; those owners remain
    // separate.
    const auto surface_level =
        distance <= 1.02 * radius
            ? NavigationKnowledgeLevel::resolved
            : (*level == NavigationKnowledgeLevel::resolved
                   ? NavigationKnowledgeLevel::probable
                   : NavigationKnowledgeLevel::contact);
    for (const auto fact : {KnowledgeFact::hazards, KnowledgeFact::landing})
      if (!record(subject, fact, surface_level))
        return std::unexpected{Error::invalid_transition};
  }
  if (!pilot.attached &&
      !record({KnowledgeSubjectKind::system, origin, origin.value},
              KnowledgeFact::presence, NavigationKnowledgeLevel::visited))
    return std::unexpected{Error::invalid_transition};
  if (voyage.surface.landed &&
      !record({KnowledgeSubjectKind::planet, origin,
               voyage.surface.landed->fixed.frame.planet->value},
              KnowledgeFact::presence, NavigationKnowledgeLevel::visited))
    return std::unexpected{Error::invalid_transition};
  if (pilot.attached) {
    if (origin != home_system) return std::unexpected{Error::invalid_subject};
    const auto station =
        generate_origin_station(knowledge.recipe.universe_seed);
    if (!record({KnowledgeSubjectKind::station, origin, station.id.value},
                KnowledgeFact::presence, NavigationKnowledgeLevel::visited))
      return std::unexpected{Error::invalid_transition};
  }
  return next;
}
} // namespace apsis_drift
