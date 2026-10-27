#include "apsis_drift/native_planet_tracking.hpp"

#include "apsis_drift/godot/saved_flight.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <span>
#include <stdexcept>

namespace apsis_drift {
namespace {
auto bounded(RigidVector3 value, double limit) -> bool {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z) &&
         std::max({std::abs(value.x), std::abs(value.y), std::abs(value.z)}) <=
             limit;
}
auto context(const NativeFreedomFlightSession& session)
    -> std::expected<RigidBodyState, std::string> {
  const auto& document = session.document();
  const auto& source = document.flight;
  const auto& knowledge = session.knowledge();
  if (!knowledge)
    return std::unexpected{"This journey has no recorded planetary chart."};
  if (session.moon() || session.minor() || document.moon || document.minor)
    return std::unexpected{"Planet HUD tracking requires planetary flight."};
  if (session.recovery_pending())
    return std::unexpected{"Planet HUD tracking ended at the recorded loss."};
  if (session.walker() || session.surface_walker() ||
      (session.boarding() &&
       session.boarding()->phase != FreedomBoardingPhase::seated))
    return std::unexpected{"Board and sit before tracking a planet."};
  if (session.travel() && session.travel()->phase != FreedomJumpPhase::idle)
    return std::unexpected{"Planet HUD tracking is unavailable during a jump."};
  if (source.frame.kind != RigidFrameKind::planet_relative_inertial &&
      source.frame.kind != RigidFrameKind::system_inertial)
    return std::unexpected{"Planet HUD tracking has no supported local frame."};
  const auto& recipe = knowledge->recipe;
  if (source.frame.system != session.system().catalog.id ||
      source.tick != document.origin.state.tick ||
      recipe.universe_seed != document.origin.recipe.universe_seed ||
      recipe.craft_lineage != document.origin.lineage ||
      recipe.physical_catalog != document.model.physical_catalog ||
      recipe.ephemeris != document.model.physical_ephemeris ||
      document.model.physical_catalog != session.system().generator_version ||
      document.model.physical_ephemeris != session.system().ephemeris_version ||
      (document.world && document.world->system != source.frame.system))
    return std::unexpected{"Planet HUD tracking has a different world owner."};
  const double norm = ((source.orientation.w * source.orientation.w +
                        source.orientation.x * source.orientation.x) +
                       source.orientation.y * source.orientation.y) +
                      source.orientation.z * source.orientation.z;
  if (!bounded(source.position_metres, kRigidBodyMaximumPositionMetres) ||
      !bounded(source.linear_velocity_metres_per_second,
               kRigidBodyMaximumVelocityMetresPerSecond) ||
      !bounded(source.angular_velocity_radians_per_second,
               kRigidBodyMaximumAngularVelocityRadiansPerSecond) ||
      !std::isfinite(norm) ||
      std::abs(norm - 1.0) > kRigidBodyOrientationSquaredNormTolerance)
    return std::unexpected{"Planet HUD tracking has invalid craft geometry."};
  const auto relative = session.relative_state();
  if (!relative || !relative->frame.planet ||
      relative->frame.system != source.frame.system ||
      relative->tick != source.tick ||
      !bounded(relative->position_metres, kRigidBodyMaximumPositionMetres) ||
      (document.world && document.world->planet != *relative->frame.planet) ||
      !find_local_system_planet(session.system(), *relative->frame.planet))
    return std::unexpected{"Planet HUD tracking lost its reference planet."};
  return *relative;
}
auto resolved(const FreedomKnowledge& knowledge, KnowledgeSubject subject,
              KnowledgeFact fact, SimulationTick tick) -> bool {
  const auto found =
      std::ranges::find_if(knowledge.entries, [&](const KnowledgeEntry& entry) {
        return entry.subject == subject && entry.fact == fact;
      });
  return found != knowledge.entries.end() && !found->transitions.empty() &&
         found->transitions.size() <= kMaximumFreedomKnowledgeTransitions &&
         found->transitions.back().tick <= tick &&
         found->transitions.back().level >= NavigationKnowledgeLevel::resolved;
}
} // namespace

auto NativePlanetTracking::targets(const NativeFreedomFlightSession& session)
    const -> std::expected<NativePlanetTrackingTargets, std::string> {
  const auto body = context(session);
  if (!body) return std::unexpected{body.error()};
  const auto& knowledge = *session.knowledge();
  // Full validation and world resolution are deliberately restricted to this
  // paused list/admission path. Older charts can still track their real known
  // Home planet; tracking does not require adopting a planetary travel recipe.
  const auto subjects = known_freedom_subjects(knowledge, body->tick);
  if (!subjects)
    return std::unexpected{"Recorded planetary subjects are unavailable."};
  NativePlanetTrackingTargets result;
  result.system = session.system().catalog.id;
  result.tick = body->tick;
  for (const auto& subject : *subjects) {
    if (subject.kind != KnowledgeSubjectKind::planet ||
        subject.system != result.system ||
        !resolved(knowledge, subject, KnowledgeFact::identity_type,
                  body->tick) ||
        !resolved(knowledge, subject, KnowledgeFact::location, body->tick))
      continue;
    const auto identity = query_freedom_knowledge(
        knowledge, subject, KnowledgeFact::identity_type, body->tick);
    const auto location = query_freedom_knowledge(
        knowledge, subject, KnowledgeFact::location, body->tick);
    if (!identity || !*identity || !location || !*location ||
        !std::holds_alternative<KnownIdentity>((**identity).value) ||
        !std::holds_alternative<KnownOrbit>((**location).value))
      return std::unexpected{"A known planet has invalid recorded evidence."};
    const auto& name = std::get<KnownIdentity>((**identity).value).name;
    const auto actual =
        find_local_system_planet(session.system(), PlanetId{subject.identity});
    if (!actual || name.empty() || name.size() > 256 ||
        name.find('\0') != std::string::npos ||
        name != (*actual)->descriptor.display_name ||
        result.count == result.targets.size())
      return std::unexpected{"A known planet has an invalid physical owner."};
    result.targets[result.count++] = {PlanetId{subject.identity}, name};
  }
  const auto ordinal = [&](const NativePlanetTrackingTarget& target) {
    return (*find_local_system_planet(session.system(), target.planet))
        ->orbit.ordinal;
  };
  std::ranges::sort(std::span{result.targets}.first(result.count), {}, ordinal);
  return result;
}

auto NativePlanetTracking::select(const NativeFreedomFlightSession& session,
                                  BodyTarget target,
                                  SimulationTick offered_tick)
    -> std::expected<void, std::string> {
  const auto& flight = session.document().flight;
  if (offered_tick != flight.tick ||
      target.system != session.system().catalog.id ||
      target.body.version != kBodyIdentityVersion ||
      target.body.kind != BodyKind::planet || !target.body.value)
    return std::unexpected{"Refresh the current planetary target list."};
  const auto admitted = targets(session);
  if (!admitted) return std::unexpected{admitted.error()};
  const auto rows = std::span{admitted->targets}.first(admitted->count);
  const auto found = std::ranges::find(rows, PlanetId{target.body.value},
                                       &NativePlanetTrackingTarget::planet);
  if (found == rows.end())
    return std::unexpected{
        "Only a resolved current-system planet can be tracked."};
  const auto body = context(session);
  if (!body) return std::unexpected{body.error()};
  auto candidate = *this;
  candidate.admission_ = Admission{session.knowledge()->recipe,
                                   target,
                                   *body->frame.planet,
                                   session.document().origin.state.craft,
                                   flight.craft,
                                   flight.frame,
                                   flight.tick,
                                   flight.tick,
                                   found->name};
  const auto reading = candidate.read(session);
  if (!reading.available) return std::unexpected{reading.note};
  admission_ = std::move(candidate.admission_);
  return {};
}

auto NativePlanetTracking::read(const NativeFreedomFlightSession& session)
    -> NativePlanetTrackingReading {
  const auto& document = session.document();
  const auto& flight = document.flight;
  NativePlanetTrackingReading result;
  result.system = session.system().catalog.id;
  result.reference_planet = flight.frame.planet.value_or(
      document.world ? document.world->planet : PlanetId{});
  result.craft = document.origin.state.craft;
  result.craft_frame = flight.craft;
  result.world_frame = flight.frame;
  result.tick = flight.tick;
  if (!admission_) return result;
  const auto refuse = [&](std::string message) {
    clear();
    result.note = std::move(message);
    return result;
  };
  const auto body = context(session);
  if (!body) return refuse(body.error());
  const auto& admitted = *admission_;
  if (admitted.recipe != session.knowledge()->recipe ||
      admitted.target.system != result.system ||
      admitted.reference != *body->frame.planet ||
      admitted.craft != result.craft || admitted.craft_frame != flight.craft ||
      admitted.world_frame != flight.frame || flight.tick < admitted.tick ||
      flight.tick < admitted.last_read_tick)
    return refuse("Planet HUD tracking ended when its local owner changed.");
  const KnowledgeSubject subject{KnowledgeSubjectKind::planet,
                                 admitted.target.system,
                                 admitted.target.body.value};
  if (!resolved(*session.knowledge(), subject, KnowledgeFact::identity_type,
                flight.tick) ||
      !resolved(*session.knowledge(), subject, KnowledgeFact::location,
                flight.tick))
    return refuse("Planet HUD tracking no longer has resolved observations.");
  const auto actual = find_local_system_planet(
      session.system(), PlanetId{admitted.target.body.value});
  if (!actual || (*actual)->descriptor.display_name != admitted.name)
    return refuse("Planet HUD tracking lost its physical target owner.");
  const auto target = resolve_planet_ephemeris(
      session.system(), (*actual)->descriptor.id, {flight.tick, 0});
  const auto reference = resolve_planet_ephemeris(
      session.system(), admitted.reference, {flight.tick, 0});
  if (!target || !reference)
    return refuse("The current planetary ephemeris is unavailable.");
  const RigidVector3 delta{
      (target->position.x - reference->position.x) - body->position_metres.x,
      (target->position.y - reference->position.y) - body->position_metres.y,
      (target->position.z - reference->position.z) - body->position_metres.z};
  const double range = std::hypot(delta.x, delta.y, delta.z);
  if (!bounded(delta, kRigidBodyMaximumPositionMetres) ||
      !std::isfinite(range) || range <= 0)
    return refuse("The planet has no finite nonzero centre bearing.");
  try {
    // This existing projection resolves rotation/tangent against the current
    // owned system. It performs no knowledge query or physical-world
    // generation.
    const auto view = godot_spike::project_saved_flight(session);
    const auto& axes = view.system_axes;
    const RigidVector3 native{
        axes[0].east * delta.x + axes[1].east * delta.y +
            axes[2].east * delta.z,
        axes[0].up * delta.x + axes[1].up * delta.y + axes[2].up * delta.z,
        -(axes[0].north * delta.x + axes[1].north * delta.y +
          axes[2].north * delta.z)};
    if (!bounded(native, kRigidBodyMaximumPositionMetres))
      return refuse("The native planetary bearing is invalid.");
    result.selected = result.available = true;
    result.planet = (*actual)->descriptor.id;
    result.reference_planet = admitted.reference;
    result.name = admitted.name;
    result.position_metres = native;
    result.range_metres = range;
    result.note = "Craft reference point to planet centre. Bearing only; not "
                  "altitude, clearance or a safe route.";
    admission_->last_read_tick = flight.tick;
    return result;
  } catch (const std::exception&) {
    return refuse("The current native planetary frame is unavailable.");
  }
}
} // namespace apsis_drift
