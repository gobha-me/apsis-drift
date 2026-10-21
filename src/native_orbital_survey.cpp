#include "apsis_drift/native_flight_session.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace apsis_drift {
namespace {
auto subject(SystemId system, PlanetId planet) -> KnowledgeSubject {
  return {KnowledgeSubjectKind::planet, system, planet.value};
}
auto known_name(const FreedomKnowledge& knowledge, KnowledgeSubject target,
                SimulationTick tick) -> std::string {
  const auto reading = query_freedom_knowledge(
      knowledge, target, KnowledgeFact::identity_type, tick);
  if (reading && *reading)
    if (const auto* identity = std::get_if<KnownIdentity>(&(**reading).value))
      return identity->name;
  return {};
}
} // namespace

auto NativeFreedomFlightSession::orbital_survey_conditions() const
    -> std::expected<void, std::string> {
  if (!knowledge_ ||
      (knowledge_->recipe.version != kFreedomObservedKnowledgeVersion &&
       knowledge_->recipe.version != kFreedomTravelKnowledgeVersion))
    return std::unexpected{"This journey has no supported ship observations"};
  if (recovery_pending())
    return std::unexpected{"Survey held: recovery pending"};
  if (moon_ || minor_)
    return std::unexpected{"Orbital terrain survey supports planets"};
  if (actor_ || surface_walker_ || (docking_ && docking_->attached) ||
      (surface_ && surface_->landed) ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle))
    return std::unexpected{"Survey held: resume free flight aboard Wayfarer"};
  const auto planet = rotation_.rotation.planet;
  if (knowledge_->orbital_surveys && knowledge_->orbital_surveys->active) {
    const auto& active = *knowledge_->orbital_surveys->active;
    if (active.system != system_.catalog.id || active.planet != planet ||
        active.craft != document_.origin.state.craft)
      return std::unexpected{"Survey held: target or source craft differs"};
  } else if (known_name(*knowledge_, subject(system_.catalog.id, planet),
                        document_.flight.tick)
                 .empty()) {
    return std::unexpected{"Identify the current planet before surveying it"};
  }
  const auto physical =
      std::ranges::find_if(system_.catalog.planets, [&](const auto& p) {
        return p.descriptor.id == planet;
      });
  const auto body = relative_state();
  const auto observed = observe();
  if (physical == system_.catalog.planets.end() || !body || !observed)
    return std::unexpected{"Survey held: orbital observation unavailable"};
  const double radius = physical->descriptor.radius.value * 1000.0;
  const auto& p = body->position_metres;
  const auto distance = std::hypot(p.x, p.y, p.z);
  if (!std::isfinite(distance) || distance > 2 * radius)
    return std::unexpected{"Survey held: approach within two planet radii"};
  if (!observed->orbit.bound ||
      observed->orbit.classification != OrbitClassification::stable ||
      distance <= radius + observed->atmosphere.space_boundary_altitude_metres)
    return std::unexpected{
        "Survey held: establish a bound orbit above the atmosphere"};
  return {};
}

auto NativeFreedomFlightSession::orbital_survey_view() const
    -> NativeOrbitalSurveyView {
  NativeOrbitalSurveyView view;
  view.selected = knowledge_ && knowledge_->orbital_surveys.has_value();
  view.active =
      view.selected && knowledge_->orbital_surveys->active.has_value();
  if (!knowledge_) {
    view.note = "This journey has no supported ship observations";
    return view;
  }
  const auto planet = rotation_.rotation.planet;
  const auto current = subject(system_.catalog.id, planet);
  view.body_name = known_name(*knowledge_, current, document_.flight.tick);
  if (view.active) {
    const auto& active = *knowledge_->orbital_surveys->active;
    view.acquired_ticks = active.acquired_ticks;
    view.body_name =
        known_name(*knowledge_, subject(active.system, active.planet),
                   document_.flight.tick);
  }
  if (!moon_ && !minor_ && view.selected) {
    for (const auto& record : knowledge_->orbital_surveys->completed) {
      if (record.system != system_.catalog.id || record.planet != planet)
        continue;
      view.completed = true;
      view.acquired_ticks = kFreedomOrbitalSurveyTicks;
      const auto reading = query_freedom_knowledge(
          *knowledge_, current, KnowledgeFact::landing, document_.flight.tick);
      if (reading && *reading)
        if (const auto* landing = std::get_if<KnownLanding>(&(**reading).value))
          view.result = *landing;
    }
  }
  const auto ready = orbital_survey_conditions();
  view.can_begin =
      ready.has_value() && !view.active && !view.completed &&
      (!view.selected || knowledge_->orbital_surveys->completed.size() <
                             kMaximumFreedomOrbitalSurveys);
  if (view.active)
    view.note = ready ? "Acquiring orbital terrain model" : ready.error();
  else if (view.completed)
    view.note =
        "Survey complete; terrain model retained in Recorded observations";
  else if (!ready)
    view.note = ready.error();
  else if (!view.can_begin)
    view.note = "Survey record limit reached";
  else
    view.note =
        "Ready: acquire 30 seconds in a stable orbit within two planet radii";
  return view;
}

auto NativeFreedomFlightSession::begin_orbital_survey()
    -> std::expected<void, std::string> {
  const auto view = orbital_survey_view();
  if (!view.can_begin) return std::unexpected{view.note};
  auto candidate = *this;
  if (!candidate.knowledge_->orbital_surveys)
    candidate.knowledge_->orbital_surveys = FreedomOrbitalSurveyState{};
  candidate.knowledge_->orbital_surveys->active =
      FreedomOrbitalSurveyAcquisition{
          system_.catalog.id, rotation_.rotation.planet,
          document_.origin.state.craft, document_.flight.tick, 0};
  const auto saved = candidate.save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (const auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}

auto NativeFreedomFlightSession::cancel_orbital_survey()
    -> std::expected<void, std::string> {
  if (recovery_pending()) return std::unexpected{"Recovery is pending"};
  if (!knowledge_ || !knowledge_->orbital_surveys ||
      !knowledge_->orbital_surveys->active)
    return std::unexpected{"No unfinished orbital survey"};
  knowledge_->orbital_surveys->active.reset();
  return {};
}

auto NativeFreedomFlightSession::advance_orbital_survey_tick()
    -> std::expected<void, std::string> {
  if (!knowledge_ || !knowledge_->orbital_surveys ||
      !knowledge_->orbital_surveys->active || !orbital_survey_conditions())
    return {};
  auto& state = *knowledge_->orbital_surveys;
  auto& active = *state.active;
  if (active.acquired_ticks >= kFreedomOrbitalSurveyTicks ||
      active.started_tick >= document_.flight.tick)
    return std::unexpected{"Orbital acquisition clock refused"};
  ++active.acquired_ticks;
  if (active.acquired_ticks != kFreedomOrbitalSurveyTicks) return {};
  const auto target = subject(active.system, active.planet);
  const auto tick = document_.flight.tick;
  const auto craft = active.craft;
  const FreedomOrbitalSurveyRecord record{active.system, active.planet, craft,
                                          active.started_tick, tick};
  // The passive observer may have staged a weaker reading in this same tick.
  // Coalesce that uncommitted reading before applying the strongest evidence.
  // No previously committed transition is removed or rewritten.
  auto found =
      std::ranges::find_if(knowledge_->entries, [&](const auto& entry) {
        return entry.subject == target && entry.fact == KnowledgeFact::landing;
      });
  if (found != knowledge_->entries.end() && !found->transitions.empty() &&
      found->transitions.back().tick == tick &&
      found->transitions.back().level < NavigationKnowledgeLevel::resolved) {
    found->transitions.pop_back();
    if (found->transitions.empty()) knowledge_->entries.erase(found);
  }
  // A complete acquisition is represented by the completed history, never by
  // an active counter outside its admitted range.
  state.active.reset();
  auto learned = apply_freedom_knowledge(
      *knowledge_,
      {target,
       KnowledgeFact::landing,
       {NavigationKnowledgeLevel::resolved, KnowledgeSource::local_ship,
        craft.value, tick, craft}},
      tick);
  if (!learned) return std::unexpected{"Orbital survey evidence refused"};
  learned->orbital_surveys->completed.push_back(record);
  std::ranges::sort(learned->orbital_surveys->completed,
                    [](const auto& a, const auto& b) {
                      return std::tie(a.system.value, a.planet.value) <
                             std::tie(b.system.value, b.planet.value);
                    });
  if (!validate_freedom_knowledge(*learned, tick))
    return std::unexpected{"Completed orbital survey history refused"};
  knowledge_ = std::move(*learned);
  return {};
}

auto NativeFreedomFlightSession::advance(const NativeFlightControls& controls,
                                         SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (!knowledge_ || !knowledge_->orbital_surveys ||
      !knowledge_->orbital_surveys->active)
    return advance_flight(controls, step);
  // Only this outer transaction counts a committed flight interval. Recursive
  // frame/impact/aid integration cannot count twice, and a refused acquisition
  // cannot commit a partial physical step.
  auto candidate = *this;
  auto result = candidate.advance_flight(controls, step);
  if (!result) return std::unexpected{result.error()};
  if (auto acquired = candidate.advance_orbital_survey_tick(); !acquired)
    return std::unexpected{acquired.error()};
  *this = std::move(candidate);
  return result;
}
} // namespace apsis_drift
