#include "apsis_drift/native_flight_session.hpp"

#include <algorithm>
#include <optional>

namespace apsis_drift {
namespace {
auto resolved_system_name(const FreedomKnowledge& knowledge, SystemId system,
                          SimulationTick tick)
    -> std::expected<std::optional<std::string>, std::string> {
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
  // The caller admitted the complete ledger at tick. Admit both facts before
  // asking the knowledge provider to resolve a generated name.
  if (!resolved(KnowledgeFact::identity_type) ||
      !resolved(KnowledgeFact::location))
    return std::nullopt;
  const auto identity = query_freedom_knowledge(
      knowledge, subject, KnowledgeFact::identity_type, tick);
  if (!identity || !*identity)
    return std::unexpected{"Return charge identity evidence is invalid"};
  const auto* name = std::get_if<KnownIdentity>(&(**identity).value);
  if (!name || !system.value || name->name.empty() || name->name.size() > 256 ||
      name->name.find('\0') != std::string::npos)
    return std::unexpected{"Return charge system identity is unavailable"};
  return name->name;
}
} // namespace

auto NativeFreedomFlightSession::return_charge_view() const
    -> std::expected<NativeReturnChargeView, std::string> {
  NativeReturnChargeView view;
  view.tick = document_.flight.tick;
  view.note = "This journey has no selected knowledge and resource ledgers.";

  if (resources_) {
    if (!validate_freedom_resources(*resources_, document_))
      return std::unexpected{
          "Return charge resource owner or clock is invalid"};
    const auto reading = freedom_resource_reading(*resources_, 0, false);
    if (!reading)
      return std::unexpected{"Return charge resource reading is invalid"};
    view.jump_charges = reading->jump_charges;
    view.flight_fraction = reading->fraction;
    view.reserve = reading->reserve;
    view.operationally_empty = reading->operationally_empty;
  }
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
      return std::unexpected{
          "Return charge knowledge owner or clock is invalid"};
  }
  if (!resources_ || !knowledge_) return view;
  const auto current = document_.flight.frame.system;
  if (!current.value || current != system_.catalog.id ||
      (document_.world && document_.world->system != current))
    return std::unexpected{"Return charge current system owner is invalid"};
  if (recovery_pending()) {
    view.note = "Continue the recorded loss before checking another voyage.";
    return view;
  }
  if (travel_ && travel_->phase != FreedomJumpPhase::idle) {
    view.note = "Finish or cancel the active jump before checking a new route.";
    return view;
  }
  if (moon_ || minor_ || document_.moon || document_.minor ||
      (travel_ && (travel_->local || (travel_->composed &&
                                      travel_->composed->active !=
                                          FreedomComposedRouteKind::system)))) {
    view.note = "Select a system route. Companion and planet jumps are not "
                "included in this charge check.";
    return view;
  }
  if (!travel_ || !travel_->selected) {
    view.note = "Select another known system to check its named return jumps.";
    return view;
  }
  const auto destination = *travel_->selected;
  if (destination == current) {
    view.note = "Select another system. The current system is not a new jump.";
    return view;
  }
  const auto origin =
      generate_first_universe_route(document_.origin.recipe.universe_seed)
          .origin;
  const auto current_name =
      resolved_system_name(*knowledge_, current, view.tick);
  const auto destination_name =
      resolved_system_name(*knowledge_, destination, view.tick);
  const auto origin_name = resolved_system_name(*knowledge_, origin, view.tick);
  if (!current_name || !destination_name || !origin_name)
    return std::unexpected{"Return charge system evidence could not be read"};
  if (!*current_name || !*destination_name || !*origin_name) {
    view.note = "Resolved system identity and location are required for the "
                "selected route and Origin return.";
    return view;
  }
  view.legs[0] = {current, destination, **current_name, **destination_name};
  view.leg_count = 1;
  if (destination != origin) {
    view.legs[1] = {destination, origin, **destination_name, **origin_name};
    view.leg_count = 2;
  }
  view.required_charges = view.leg_count;
  view.charge_shortfall = view.required_charges > view.jump_charges
                              ? view.required_charges - view.jump_charges
                              : 0;
  view.available = true;
  view.note = "Charge budget only; maneuver fuel and jump feasibility must be "
              "checked separately. No replenishment is assumed.";
  return view;
}
} // namespace apsis_drift
