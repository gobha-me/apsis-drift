#include "apsis_drift/godot/saved_flight.hpp"
#include "apsis_drift/native_planet_tracking.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <type_traits>

namespace apsis_drift {
namespace {
unsigned tracking_checks{};
auto check(bool value, const char* message) -> void {
  ++tracking_checks;
  if (!value) throw std::runtime_error(message);
}
template <class T, class E>
auto need(std::expected<T, E> value,
          std::source_location at = std::source_location::current()) -> T {
  if (!value) {
    auto message = "Planet tracking fixture line " + std::to_string(at.line());
    if constexpr (std::is_same_v<E, std::string>)
      message += ": " + value.error();
    if constexpr (requires { value.error().detail; })
      message += ": " + value.error().detail;
    throw std::runtime_error(message);
  }
  if constexpr (!std::is_void_v<T>) {
    if constexpr (std::is_trivially_copyable_v<T>)
      return *value;
    else
      return std::move(*value);
  }
}
auto bytes(const NativeFreedomFlightSession& session) -> std::string {
  auto document =
      std::make_unique<NativeSaveDocument>(need(session.save_document()));
  return need(encode_native_save_document_json(*document));
}
auto open(const NativeSaveDocument& document)
    -> std::unique_ptr<NativeFreedomFlightSession> {
  return std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_select_save_document(document)))));
}
auto continued(const NativeFreedomFlightSession& session)
    -> std::unique_ptr<NativeFreedomFlightSession> {
  auto saved = std::make_unique<NativeSaveDocument>(
      need(decode_native_save_document_json(bytes(session))));
  auto result = open(*saved);
  check(bytes(*result) == bytes(session),
        "Tracking fixture Continue changed the complete Save");
  return result;
}
auto board(NativeFreedomFlightSession& session) -> void {
  for (unsigned i = 0; i < 1194; ++i)
    need(session.advance_walk({0, -1, 0}));
  need(session.begin_boarding());
  for (SimulationTick i = 0; i < kGameplayBoardingTicks; ++i)
    need(session.advance_walk({}));
}
auto target(const NativeFreedomFlightSession& session, PlanetId planet)
    -> BodyTarget {
  return {session.system().catalog.id, body_id(planet)};
}
auto off(const NativePlanetTrackingReading& reading) -> void {
  check(
      !reading.selected && !reading.available && !reading.planet.value &&
          reading.name.empty() && reading.range_metres == 0 &&
          reading.position_metres == RigidVector3{} && !reading.note.empty() &&
          reading.note.size() <= 1024,
      "Unavailable tracking leaked a name, target or stale physical geometry");
}
auto oracle(NativePlanetTracking& tracking,
            const NativeFreedomFlightSession& session, PlanetId expected)
    -> NativePlanetTrackingReading {
  const auto original = bytes(session);
  const auto reading = tracking.read(session);
  const auto again = tracking.read(session);
  check(reading.available && reading.selected && reading.planet == expected &&
            reading.tick == session.document().flight.tick &&
            reading.system == session.system().catalog.id &&
            reading.craft == session.document().origin.state.craft &&
            reading.craft_frame == session.document().flight.craft &&
            reading.world_frame == session.document().flight.frame &&
            reading.name ==
                (*need(find_local_system_planet(session.system(), expected)))
                    .descriptor.display_name,
        "Planet tracking lost its actual name, owner, frame or current tick");
  check(again.position_metres == reading.position_metres &&
            again.range_metres == reading.range_metres &&
            again.tick == reading.tick && bytes(session) == original,
        "Repeated same-tick tracker reads changed Save bytes or physical "
        "readings");
  const auto body = need(session.relative_state());
  const auto body_pose = need(resolve_planet_ephemeris(
      session.system(), *body.frame.planet, {body.tick, 0}));
  const auto destination = need(
      resolve_planet_ephemeris(session.system(), expected, {body.tick, 0}));
  const RigidVector3 displacement{
      (destination.position.x - body_pose.position.x) - body.position_metres.x,
      (destination.position.y - body_pose.position.y) - body.position_metres.y,
      (destination.position.z - body_pose.position.z) - body.position_metres.z};
  const double range =
      std::hypot(displacement.x, displacement.y, displacement.z);
  check(reading.reference_planet == *body.frame.planet &&
            reading.range_metres == range && range > 0,
        "Tracking range was not the actual same-tick craft-to-centre distance");
  // Independent inverse rotation of the reported tangent vector reconstructs
  // the physical system displacement, rather than comparing to a render node.
  const auto view = godot_spike::project_saved_flight(session);
  const auto& r = reading.position_metres;
  const auto reconstruct = [&](std::size_t i) {
    const auto& axis = view.system_axes[i];
    return axis.east * r.x + axis.up * r.y - axis.north * r.z;
  };
  const double error = std::hypot(reconstruct(0) - displacement.x,
                                  reconstruct(1) - displacement.y,
                                  reconstruct(2) - displacement.z);
  check(error <= range * 1e-12 &&
            std::abs(std::hypot(r.x, r.y, r.z) - range) <= range * 1e-12,
        "Native bearing used a scaled point or a different physical frame");
  return reading;
}
auto old_chart_and_evidence() -> void {
  auto session = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(need(native_new_game({42})))));
  NativePlanetTracking tracking;
  off(tracking.read(*session));
  const auto home = session->document().origin.recipe.home_planet;
  const auto unseen = session->system().catalog.planets[1].descriptor.id;
  const auto foot = bytes(*session);
  check(!tracking.targets(*session) &&
            !tracking.select(*session, target(*session, home),
                             session->document().flight.tick) &&
            bytes(*session) == foot,
        "On-foot tracking bypassed actor guards or changed the Save");
  board(*session);
  const auto saved = bytes(*session);
  for (unsigned fault = 0; fault < 4; ++fault) {
    auto invalid =
        std::make_unique<NativeSaveDocument>(need(session->save_document()));
    auto& mapped = std::get<FreedomKnowledgeSaveDocument>(*invalid);
    auto& flight =
        std::get<FreedomBoardingSaveDocument>(mapped.voyage.voyage.base)
            .voyage.flight;
    switch (fault) {
      case 0:
        flight.flight.position_metres.x =
            std::numeric_limits<double>::quiet_NaN();
        break;
      case 1:
        flight.flight.orientation.w = std::numeric_limits<double>::infinity();
        break;
      case 2: ++mapped.knowledge.recipe.universe_seed.value; break;
      default: ++mapped.voyage.resources.tick; break;
    }
    check(
        !native_select_save_document(std::move(*invalid)),
        "Nonfinite craft geometry or mismatched owner/clock passed admission");
  }
  const auto list = need(tracking.targets(*session));
  check(list.count == 1 && list.targets[0].planet == home &&
            bytes(*session) == saved,
        "An old chart hid known Home, exposed unobserved bodies or mutated "
        "state");
  check(!tracking.select(*session, target(*session, unseen), list.tick),
        "An unknown physical catalog planet became a tracking target");
  need(tracking.select(*session, target(*session, home), list.tick));
  oracle(tracking, *session, home);
  check(bytes(*session) == saved,
        "Selecting known Home changed the complete Save");
  // Explicit pure-provider evidence fixtures, not claims of sensor acquisition
  // or a flown approach. Absence and probable location must both remain hidden.
  auto document =
      std::make_unique<NativeSaveDocument>(need(session->save_document()));
  auto& ledger = std::get<FreedomKnowledgeSaveDocument>(*document).knowledge;
  const KnowledgeSubject subject{KnowledgeSubjectKind::planet,
                                 session->system().catalog.id, unseen.value};
  const auto craft = session->document().origin.state.craft;
  const auto event = [&](KnowledgeFact fact, NavigationKnowledgeLevel level) {
    return KnowledgeEvidence{
        subject,
        fact,
        {level, KnowledgeSource::local_ship, craft.value, list.tick, craft}};
  };
  ledger = need(apply_freedom_knowledge(
      ledger,
      event(KnowledgeFact::identity_type, NavigationKnowledgeLevel::resolved),
      list.tick));
  auto identity_only = open(*document);
  check(need(tracking.targets(*identity_only)).count == 1 &&
            !tracking.select(*identity_only, target(*identity_only, unseen),
                             list.tick),
        "Identity without a resolved location disclosed a tracking target");
  auto probable = std::make_unique<NativeSaveDocument>(*document);
  auto& partial = std::get<FreedomKnowledgeSaveDocument>(*probable).knowledge;
  partial = need(apply_freedom_knowledge(
      partial,
      event(KnowledgeFact::location, NavigationKnowledgeLevel::probable),
      list.tick));
  auto unresolved = open(*probable);
  check(
      need(tracking.targets(*unresolved)).count == 1 &&
          !tracking.select(*unresolved, target(*unresolved, unseen), list.tick),
      "Probable location was presented as an exact physical target");
  ledger = need(apply_freedom_knowledge(
      ledger,
      event(KnowledgeFact::location, NavigationKnowledgeLevel::resolved),
      list.tick));
  auto resolved_session = open(*document);
  need(tracking.select(*resolved_session, target(*resolved_session, unseen),
                       list.tick));
  oracle(tracking, *resolved_session, unseen);
  off(tracking.read(*session));
  check(bytes(*session) == saved,
        "Discarding evidence-bound tracking mutated its original owner");
  need(tracking.select(*session, target(*session, home), list.tick));
  need(session->begin_disembarking());
  off(tracking.read(*session));
}
auto charted_and_moving() -> void {
  auto session = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_planetary_network_new_game({42})))));
  board(*session);
  NativePlanetTracking tracking;
  const auto list = need(tracking.targets(*session));
  check(list.count == session->system().catalog.planets.size() &&
            list.count > 1,
        "Planetary admission omitted known planets or exposed another system");
  const auto home = session->document().origin.recipe.home_planet;
  const auto other = list.targets[1].planet;
  need(tracking.select(*session, target(*session, other), list.tick));
  const auto original = bytes(*session);
  const auto initial = oracle(tracking, *session, other);
  for (unsigned fault = 0; fault < 5; ++fault) {
    auto bad = target(*session, home);
    auto tick = list.tick;
    switch (fault) {
      case 0: ++bad.system.value; break;
      case 1: bad.body.kind = BodyKind::moon; break;
      case 2: ++bad.body.version; break;
      case 3: bad.body.value = 0; break;
      default: ++tick; break;
    }
    check(!tracking.select(*session, bad, tick) &&
              tracking.read(*session).planet == other &&
              bytes(*session) == original,
          "A refused target choice erased the valid target or changed state");
  }
  // Resource-only initialized boundary: real world/pilot/clock/knowledge remain
  // exact. Passive tracking needs neither fuel nor jump charge affordability.
  auto dry_doc =
      std::make_unique<NativeSaveDocument>(need(session->save_document()));
  auto& resources =
      std::get<FreedomKnowledgeSaveDocument>(*dry_doc).voyage.resources;
  resources.flight_quanta = 0;
  resources.jump_charges = 0;
  auto dry = open(*dry_doc);
  NativePlanetTracking dry_tracking;
  need(dry_tracking.select(*dry, target(*dry, other), list.tick));
  check(oracle(dry_tracking, *dry, other).range_metres == initial.range_metres,
        "A passive body-centre reading required operational fuel or charges");
  auto replay = continued(*session);
  need(session->release_port());
  need(replay->release_port());
  auto later = initial;
  for (unsigned batch : {1U, 119U, 120U}) {
    for (unsigned i = 0; i < batch; ++i) {
      need(session->advance({}));
      need(replay->advance({}));
    }
    later = oracle(tracking, *session, other);
    check(bytes(*session) == bytes(*replay) &&
              session->knowledge() == replay->knowledge() &&
              session->resources() == replay->resources() &&
              session->travel() == replay->travel(),
          "Passive moving reads changed physics, knowledge, resources or jump "
          "state");
  }
  const auto before_stale = bytes(*session);
  check(!tracking.select(*session, target(*session, home), list.tick) &&
            tracking.read(*session).planet == other &&
            bytes(*session) == before_stale,
        "A stale offered tick changed the current live target or saved state");
  check(later.tick > list.tick &&
            later.position_metres != initial.position_metres &&
            later.range_metres != initial.range_metres,
        "Live tracking froze at the offered tick or ignored real moving "
        "ephemerides");
  // Explicit coincident-geometry boundary, not a flown encounter. The craft
  // remains in its actual admitted home reference; only this test pose places
  // its reference point exactly at the other known body's current centre.
  auto coincident_doc =
      std::make_unique<NativeSaveDocument>(need(session->save_document()));
  auto& coincident_flight =
      std::get<FreedomBoardingSaveDocument>(
          std::get<FreedomKnowledgeSaveDocument>(*coincident_doc)
              .voyage.voyage.base)
          .voyage.flight.flight;
  const auto current = need(resolve_planet_ephemeris(
      session->system(), home, {coincident_flight.tick, 0}));
  const auto destination = need(resolve_planet_ephemeris(
      session->system(), other, {coincident_flight.tick, 0}));
  coincident_flight.position_metres = {
      destination.position.x - current.position.x,
      destination.position.y - current.position.y,
      destination.position.z - current.position.z};
  auto coincident = open(*coincident_doc);
  check(!tracking.select(*coincident, target(*coincident, other),
                         coincident->document().flight.tick),
        "A coincident target generated a false centre bearing");
  oracle(tracking, *session, other);
  tracking.clear();
  off(tracking.read(*session));
  check(bytes(*session) == bytes(*replay),
        "Clearing tracking changed the voyage");
  need(tracking.select(*session, target(*session, home),
                       session->document().flight.tick));
  oracle(tracking, *session, home);
  auto reload = continued(*session);
  auto reopened_world = std::make_unique<godot_spike::SavedFlightWorld>(
      *reload, FixedStepClock{}, std::optional<NativeFlightStep>{}, 0.0);
  off(reopened_world->planet_tracking.read(reopened_world->session));
  check(bytes(reopened_world->session) == bytes(*session),
        "Continue serialized or retained a transient view target");
  // Coordinate-only initialized boundary using the existing authoritative
  // handoff; no pose, clock or physical motion is fabricated.
  auto system_doc =
      std::make_unique<NativeSaveDocument>(need(session->save_document()));
  auto& voyage = std::get<FreedomKnowledgeSaveDocument>(*system_doc);
  auto& flight =
      std::get<FreedomBoardingSaveDocument>(voyage.voyage.voyage.base)
          .voyage.flight;
  flight.model.frame_policy = kFreedomReleaseSystemFramePolicy;
  flight.world = FreedomActiveWorldSelection{
      kFreedomNetworkActiveWorldVersion, session->system().catalog.id, home};
  flight.flight = need(reframe_rigid_body(
      {session->system()}, flight.flight,
      {{RigidFrameKind::system_inertial, session->system().catalog.id, {}, {}},
       flight.flight.tick}));
  auto system_frame = open(*system_doc);
  off(tracking.read(*system_frame));
  need(tracking.select(*system_frame, target(*system_frame, other),
                       system_frame->document().flight.tick));
  auto system_reading = oracle(tracking, *system_frame, other);
  need(system_frame->advance({}));
  check(oracle(tracking, *system_frame, other).tick == system_reading.tick + 1,
        "System-frame tracking failed after an ordinary committed tick");
  // An actual classified loss freezes the current voyage; tracking neither
  // causes that loss nor transfers its cached label to the replacement craft.
  const auto digest = need(rigid_body_state_checksum(
      {system_frame->system()}, system_frame->document().flight));
  need(system_frame->record_loss(FreedomLossCause::recoverable_destruction,
                                 system_frame->document().origin.state.craft,
                                 system_frame->document().flight.tick, digest));
  off(tracking.read(*system_frame));
  need(system_frame->complete_recovery());
  off(tracking.read(*system_frame));
}
} // namespace

auto run_native_planet_tracking_checks() -> void {
  old_chart_and_evidence();
  charted_and_moving();
  std::cout << tracking_checks << " known-planet-tracking checks passed\n";
}
} // namespace apsis_drift
