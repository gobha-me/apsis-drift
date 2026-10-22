#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/save_file.hpp"

#include <cmath>

namespace apsis_drift {
auto NativeFreedomFlightSession::star_search_conditions() const
    -> std::expected<void, std::string> {
  if (!knowledge_ ||
      knowledge_->recipe.version != kFreedomUnchartedKnowledgeVersion ||
      !knowledge_->star_search)
    return std::unexpected{"This journey has no uncharted-region search"};
  if (recovery_pending())
    return std::unexpected{"Search held: recovery pending"};
  if (moon_ || minor_ || actor_ || surface_walker_ ||
      (docking_ && docking_->attached) || (surface_ && surface_->landed) ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle))
    return std::unexpected{
        "Search held: resume ordinary space flight aboard Wayfarer"};
  const auto body = relative_state();
  const auto observed = observe();
  if (!body || !observed ||
      !std::isfinite(observed->atmosphere.altitude_metres))
    return std::unexpected{"Search held: flight reference unavailable"};
  if (observed->atmosphere.altitude_metres <=
      observed->atmosphere.space_boundary_altitude_metres)
    return std::unexpected{"Search held: leave the atmosphere"};
  if (knowledge_->star_search->active) {
    const auto& active = *knowledge_->star_search->active;
    if (active.source != system_.catalog.id ||
        active.craft != document_.origin.state.craft)
      return std::unexpected{"Search held: source system or craft differs"};
  }
  return {};
}
auto NativeFreedomFlightSession::star_search_view() const
    -> NativeStarSearchView {
  NativeStarSearchView view;
  view.selected = knowledge_ && knowledge_->star_search.has_value();
  if (!view.selected) {
    view.note = "This journey has no uncharted-region search";
    return view;
  }
  const auto& state = *knowledge_->star_search;
  view.active = state.active.has_value();
  view.completed = state.completed.size();
  if (state.active) view.acquired_ticks = state.active->acquired_ticks;
  const auto ready = star_search_conditions();
  view.can_begin = ready.has_value() && !view.active && view.completed < 2;
  if (view.completed == 2)
    view.note = "No further stellar contacts detected in this region";
  else if (!ready)
    view.note = ready.error();
  else if (view.active)
    view.note = "Acquiring a stellar navigation fix";
  else
    view.note = "Ready: search for ten seconds in ordinary space flight";
  return view;
}
auto NativeFreedomFlightSession::begin_star_search()
    -> std::expected<void, std::string> {
  const auto view = star_search_view();
  if (!view.can_begin) return std::unexpected{view.note};
  auto next = begin_freedom_star_search(*knowledge_, system_.catalog.id,
                                        document_.origin.state.craft,
                                        document_.flight.tick);
  if (!next) return std::unexpected{"Stellar acquisition source refused"};
  auto candidate = *this;
  candidate.knowledge_ = std::move(*next);
  const auto saved = candidate.player_save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (const auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::cancel_star_search()
    -> std::expected<void, std::string> {
  if (recovery_pending()) return std::unexpected{"Recovery pending"};
  if (!knowledge_ || !knowledge_->star_search ||
      !knowledge_->star_search->active)
    return std::unexpected{"No unfinished stellar search"};
  knowledge_->star_search->active.reset();
  return {};
}
auto NativeFreedomFlightSession::advance_star_search_tick()
    -> std::expected<void, std::string> {
  if (!knowledge_ || !knowledge_->star_search ||
      !knowledge_->star_search->active)
    return {};
  auto next = advance_freedom_star_search(
      *knowledge_, system_.catalog.id, document_.origin.state.craft,
      document_.flight.tick, star_search_conditions().has_value());
  if (!next) return std::unexpected{"Stellar acquisition transaction refused"};
  knowledge_ = std::move(*next);
  return {};
}
} // namespace apsis_drift
