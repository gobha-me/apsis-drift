#include "apsis_drift/freedom_system_network.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "apsis_drift/surface_landmarks.hpp"
#include <source_location>

#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace apsis_drift;
unsigned checks{};
auto check(bool value, const char* message) -> void {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
template <class T, class E>
auto need(std::expected<T, E> value,
          std::source_location location = std::source_location::current())
    -> T {
  if (!value) {
    std::cerr << location.line() << ": ";
    if constexpr (std::is_same_v<E, std::string>)
      std::cerr << value.error() << '\n';
    else if constexpr (requires { value.error().detail; })
      std::cerr << value.error().detail << '\n';
    else if constexpr (std::is_enum_v<E>)
      std::cerr << "Refusal " << static_cast<unsigned>(value.error()) << '\n';
    throw std::runtime_error("Native planetary fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto bytes(const NativeFreedomFlightSession& s) -> std::string {
  return need(encode_native_save_document_json(need(s.save_document())));
}
auto continued(const NativeFreedomFlightSession& s)
    -> NativeFreedomFlightSession {
  const auto text = bytes(s);
  auto result =
      need(NativeFreedomFlightSession::open(need(native_select_save_document(
          need(decode_native_save_document_json(text))))));
  check(bytes(result) == text,
        "Continue preserves the complete selected voyage bytes");
  return result;
}
auto initial_source(std::uint32_t chart = 2) -> FreedomKnowledgeSaveDocument {
  auto s = need(NativeFreedomFlightSession::open(
      need(native_jump_recovery_new_game({42}, chart))));
  for (unsigned t = 0; t < 1194; ++t)
    need(s.advance_walk({0, -1, 0}));
  need(s.begin_boarding());
  for (SimulationTick t = 0; t < kGameplayBoardingTicks; ++t)
    need(s.advance_walk({}));
  need(s.release_port());
  auto document =
      std::get<FreedomKnowledgeSaveDocument>(need(s.save_document()));
  auto& boarded =
      std::get<FreedomBoardingSaveDocument>(document.voyage.voyage.base);
  auto& flight = boarded.voyage.flight;
  const auto home = need(
      find_local_system_planet(s.system(), flight.origin.recipe.home_planet));
  // Explicit saved-space test boundary, not a flown station ascent. Retain the
  // actual native boarded pilot, owner, clock, model, resources and knowledge.
  flight.flight.position_metres = {
      0, 0, static_cast<double>(home->descriptor.radius.value) * 10000};
  flight.flight.linear_velocity_metres_per_second = {};
  flight.flight.angular_velocity_radians_per_second = {};
  flight.flight = need(canonicalize_rigid_body_state(
      RigidBodyWorldContext{s.system()}, flight.flight));
  // Saved metadata fixture for the already qualified seed-42 Home exposure;
  // this does not claim collection during the station/boarding route above.
  // The real charged legs must retain a nonempty pouch and first notes.
  const SurfaceLandmarkCell cell{11251, -263, -263};
  const auto identity = need(identify_surface_landmark(home->descriptor, cell));
  check(identity && identity->id == SurfaceLandmarkId{3477446278323237925ULL},
        "The fixed previously qualified exposure retains its actual identity");
  document.knowledge.surface_landmarks = FreedomSurfaceLandmarkState{
      1,
      {{s.system().catalog.id, home->descriptor.id, cell, identity->id, 2, 7}}};
  const FreedomFieldSampleRecord note{
      {s.system().catalog.id, body_id(home->descriptor.id)},
      cell,
      identity->id,
      2,
      identity->kind,
      flight.origin.state.craft,
      20};
  document.knowledge.field_samples =
      FreedomFieldSampleState{1, flight.origin.state.craft, {note}, {note}};
  return document;
}
auto fixture() -> NativeFreedomFlightSession {
  static const auto source = initial_source();
  return need(NativeFreedomFlightSession::open(
      need(native_select_save_document(source))));
}
auto current_target(const NativeFreedomFlightSession& s) -> BodyTarget {
  return {s.system().catalog.id, body_id(*s.document().flight.frame.planet)};
}
auto other_planet(const NativeFreedomFlightSession& s) -> BodyTarget {
  const auto rows = need(resolve_freedom_known_planets(
      *s.knowledge(), current_target(s), s.document().flight.tick));
  for (const auto& row : rows)
    if (!row.current) return row.target;
  throw std::runtime_error("Expected another known ordinary planet");
}
auto companion(const NativeFreedomFlightSession& s, bool minor) -> BodyTarget {
  const auto source = need(generate_physical_origin_system({42}, 2));
  const auto catalog = need(generate_secondary_body_catalog(source));
  check(s.document().origin.recipe.universe_seed == Seed{42},
        "Fixed companion fixture seed remains42");
  return {source.catalog.id, catalog.bodies[minor ? 1 : 0].node.id};
}
auto reading(const NativeFreedomFlightSession& s)
    -> FreedomJumpRecoveryReading {
  return need(freedom_jump_recovery_reading(*s.resources()->drive,
                                            s.document().flight.tick));
}
auto leg(NativeFreedomFlightSession& s, FreedomJumpRecoveryKind kind) -> void {
  const auto before = *s.resources();
  const auto samples = s.knowledge()->field_samples;
  const auto inspections = s.knowledge()->surface_landmarks;
  const auto pilot = s.boarding();
  const auto attempt = s.travel()->next_attempt;
  need(s.begin_jump());
  s = continued(s);
  for (SimulationTick i = 1; i <= kJumpSpoolTicks + kJumpTransitTicks; ++i) {
    const auto result = need(s.advance({}));
    if (i < kJumpSpoolTicks)
      check(s.resources()->drive == before.drive && !result.jump_committed,
            "Spooling ages the clock without creating a use stamp");
    if (i == kJumpSpoolTicks) {
      check(result.jump_committed && s.resources()->drive->last_use &&
                s.resources()->drive->last_use->kind == kind &&
                s.resources()->drive->last_use->attempt == attempt &&
                s.resources()->jump_charges + 1 == before.jump_charges &&
                reading(s).pending_arrival,
            "Every typed commit atomically stamps the same drive and spends "
            "one charge");
      const auto frozen = bytes(s);
      NativeFlightControls invalid;
      invalid.positive_rotation.x = std::numeric_limits<double>::quiet_NaN();
      check(!s.advance(invalid) && !s.cancel_jump() && bytes(s) == frozen,
            "Invalid input and committed cancellation cannot change the frozen "
            "outcome");
      const auto document = need(s.travel_document());
      std::optional<FrozenFreedomJumpUncertainty> uncertainty;
      if (kind == FreedomJumpRecoveryKind::system)
        uncertainty = document.travel.committed->recovery;
      else if (kind == FreedomJumpRecoveryKind::planet)
        uncertainty = document.travel.composed->planet_committed->recovery;
      else
        uncertainty = (kind == FreedomJumpRecoveryKind::moon
                           ? document.travel.composed->moon
                           : document.travel.composed->minor)
                          .committed->recovery;
      check(uncertainty && (before.drive->last_use
                                ? uncertainty->extra_radius_millimetres > 0
                                : uncertainty->extra_radius_millimetres == 0),
            "First jump is precise baseline; actual early repeat freezes extra "
            "spread");
    }
    if (i == kJumpSpoolTicks - 1 || i == kJumpSpoolTicks ||
        i == kJumpSpoolTicks + 1 || i == kJumpSpoolTicks + kJumpTransitTicks)
      s = continued(s);
  }
  check(!reading(s).pending_arrival && !reading(s).ready &&
            reading(s).remaining_ticks == kFreedomDriveRecoveryTicks &&
            reading(s).extra_radius_millimetres ==
                kFreedomJumpRecoveryMaximumRadiusMillimetres &&
            s.resources()->jump_charges + 1 == before.jump_charges &&
            s.boarding() == pilot && s.knowledge()->field_samples == samples &&
            s.knowledge()->surface_landmarks == inspections,
        "Arrival begins the full45s and retains actual pilot, carried "
        "specimens and notes");
}
auto selection_and_cancel() -> void {
  for (std::uint32_t chart = 0; chart < 3; ++chart) {
    const auto plain =
        need(chart == 0   ? native_new_game({42})
             : chart == 1 ? native_network_new_game({42})
                          : native_planetary_network_new_game({42}));
    const auto selected = need(native_jump_recovery_new_game({42}, chart));
    const auto& old = std::get<FreedomKnowledgeSaveDocument>(plain.document);
    const auto& current =
        std::get<FreedomKnowledgeSaveDocument>(selected.document);
    check(old.knowledge == current.knowledge &&
              old.voyage.voyage == current.voyage.voyage &&
              !old.voyage.resources.drive &&
              current.voyage.resources.version ==
                  kFreedomRecoveryResourceVersion &&
              current.voyage.resources.drive &&
              !current.voyage.resources.drive->last_use,
          "Drive selection preserves each actual chart, universe and physical "
          "station start");
    auto live = need(NativeFreedomFlightSession::open(selected));
    check(reading(live).ready,
          "Every explicitly selected station craft starts ready");
    live = continued(live);
  }
  check(!native_jump_recovery_new_game({42}, 5),
        "Unknown chart declines before startup");
  auto s = fixture();
  const auto home = current_target(s);
  need(s.select_planet_jump(other_planet(s)));
  leg(s, FreedomJumpRecoveryKind::planet);
  need(s.select_planet_jump(home));
  const auto stamp = s.resources()->drive;
  const auto charges = s.resources()->jump_charges;
  const auto remaining = reading(s).remaining_ticks;
  const auto paused = bytes(s);
  for (unsigned query = 0; query < 3; ++query) {
    need(s.planet_jump_preview());
    need(s.jump_available());
    need(freedom_jump_recovery_reading(*s.resources()->drive,
                                       s.document().flight.tick));
  }
  check(
      bytes(s) == paused && reading(s).remaining_ticks == remaining,
      "Paused UI previews and Save do not age recovery or mutate the outcome");
  need(s.begin_jump());
  for (unsigned i = 0; i < 17; ++i)
    need(s.advance({}));
  need(s.cancel_jump());
  check(s.resources()->drive == stamp &&
            s.resources()->jump_charges == charges &&
            reading(s).remaining_ticks == remaining - 17,
        "Canceled spool keeps the stamp/charge while genuine physical ticks "
        "age recovery");
  s = continued(s);
  leg(s, FreedomJumpRecoveryKind::planet);
  const auto network = need(generate_freedom_system_network({42}));
  need(s.select_jump(network.nodes[1].system));
  leg(s, FreedomJumpRecoveryKind::system);
  const auto nocharge = bytes(s);
  check(
      !s.jump_available() && !s.begin_jump() && bytes(s) == nocharge,
      "Empty charges do not create a new stamp or change the complete voyage");
}
auto chart_travel() -> void {
  for (std::uint32_t chart = 0; chart < 2; ++chart) {
    auto s = need(NativeFreedomFlightSession::open(
        need(native_select_save_document(initial_source(chart)))));
    const auto route = generate_first_universe_route({42});
    need(s.select_jump(route.destination));
    check(s.travel()->version == kFreedomRecoveringTravelStateVersion &&
              s.knowledge()->recipe.version ==
                  (chart == 0 ? kFreedomTravelKnowledgeVersion
                              : kFreedomNetworkKnowledgeVersion),
          "First recovering selection uses composer without upgrading charts");
    leg(s, FreedomJumpRecoveryKind::system);
    need(s.select_jump(route.origin));
    leg(s, FreedomJumpRecoveryKind::system);
  }
}
auto companions() -> void {
  for (const bool minor : {false, true}) {
    auto s = fixture();
    const auto home = current_target(s);
    const auto kind =
        minor ? FreedomJumpRecoveryKind::minor : FreedomJumpRecoveryKind::moon;
    need(s.select_local_jump(companion(s, minor)));
    leg(s, kind);
    need(s.select_local_jump(home));
    leg(s, kind);
    if (minor) {
      need(s.select_planet_jump(other_planet(s)));
      leg(s, FreedomJumpRecoveryKind::planet);
    } else {
      const auto network = need(generate_freedom_system_network({42}));
      need(s.select_jump(network.nodes[1].system));
      leg(s, FreedomJumpRecoveryKind::system);
    }
  }
}
} // namespace
int main() {
  try {
    selection_and_cancel();
    companions();
    chart_travel();
    std::cout << checks << " native jump recovery checks passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
