#include "apsis_drift/native_flight_session.hpp"

#include "apsis_drift/freedom_system_network.hpp"
#include "apsis_drift/save_file.hpp"

#include <memory>

namespace apsis_drift {
auto NativeFreedomFlightSession::charted_arrival_view() const
    -> std::expected<NativeChartedArrivalView, std::string> {
  NativeChartedArrivalView view;
  view.supported =
      knowledge_ && document_.origin.network &&
      freedom_knowledge_has_planetary_chart(knowledge_->recipe.version);
  view.enabled = knowledge_ && knowledge_->charted_arrivals.has_value();
  view.note =
      "Charted arrivals require an existing planetary navigation chart.";
  if (!view.supported) return view;
  if (!validate_freedom_knowledge(*knowledge_, document_.flight.tick))
    return std::unexpected{"Charted arrival knowledge is invalid"};
  if (recovery_pending()) {
    view.note =
        "Continue the recorded loss before changing arrival preference.";
    return view;
  }
  if (moon_ || minor_ ||
      (travel_ && (travel_->local || (travel_->composed &&
                                      travel_->composed->active !=
                                          FreedomComposedRouteKind::system)))) {
    view.note = "A local body route is selected. Charted arrivals apply only "
                "to system jumps.";
    return view;
  }
  if (!travel_ || !travel_->selected) {
    view.note = "Select another known system to assess its charted primary "
                "planet. Existing placement remains in use.";
    return view;
  }
  if (*travel_->selected == system_.catalog.id) {
    view.note = "Select another system. The current system is not a new jump "
                "destination.";
    return view;
  }
  const auto ids = generate_first_intersystem_identities(
      document_.origin.recipe.universe_seed);
  if (*travel_->selected == ids.origin_system) {
    view.note = "Origin retains its existing station-return placement.";
    return view;
  }
  const auto network = generate_freedom_system_network(
      document_.origin.recipe.universe_seed, document_.origin.network->version);
  if (!network)
    return std::unexpected{"Charted arrival network is unavailable"};
  const auto destination =
      resolve_freedom_network_system(*network, *travel_->selected, 2);
  if (!destination || destination->catalog.planets.empty())
    return std::unexpected{
        "Selected system is not an authenticated network destination"};
  const auto known = freedom_charted_arrival_known_at(
      *knowledge_, *travel_->selected,
      destination->catalog.planets.front().descriptor.id, document_.flight.tick,
      document_.flight.tick);
  if (!known) return std::unexpected{"Charted arrival evidence is invalid"};
  view.eligible = *known;
  if (!view.eligible)
    view.note = "Wide arrival retained: the primary planet's identity and "
                "location are not yet resolved.";
  else if (travel_->phase != FreedomJumpPhase::idle)
    view.note = "The current system jump retains its selected arrival policy. "
                "Finish or cancel it before changing the preference.";
  else
    view.note = view.enabled
                    ? "Charted arrival enabled for this known primary: a high "
                      "approach, not an orbit or landing. Velocity, attitude "
                      "and ordinary jump costs are retained."
                    : "Charted arrival is available for this known primary. "
                      "Enable explicitly for a nearer high approach; existing "
                      "placement is selected.";
  return view;
}

auto NativeFreedomFlightSession::set_charted_arrivals(bool enabled)
    -> std::expected<void, std::string> {
  if (!knowledge_ || !document_.origin.network ||
      !freedom_knowledge_has_planetary_chart(knowledge_->recipe.version))
    return std::unexpected{
        "Charted arrivals require a planetary navigation chart"};
  if (recovery_pending() ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle))
    return std::unexpected{
        "Finish or cancel the active jump/recovery before changing arrivals"};
  if (auto valid =
          validate_freedom_knowledge(*knowledge_, document_.flight.tick);
      !valid)
    return std::unexpected{"Current arrival preference owner is invalid"};
  if (knowledge_->charted_arrivals.has_value() == enabled) return {};
  auto candidate = std::make_unique<NativeFreedomFlightSession>(*this);
  if (enabled)
    candidate->knowledge_->charted_arrivals.emplace();
  else
    candidate->knowledge_->charted_arrivals.reset();
  const auto saved = candidate->save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (const auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(*candidate);
  return {};
}
} // namespace apsis_drift
