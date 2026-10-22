#pragma once

#include "apsis_drift/freedom_system_network.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"

#include <algorithm>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <type_traits>

namespace jump_strain_fixture {
using namespace apsis_drift;
inline unsigned checks{};
inline auto check(bool value, const char* message) -> void {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
template <class T, class E>
auto need(std::expected<T, E> result,
          std::source_location where = std::source_location::current()) -> T {
  if (!result) {
    std::cerr << where.file_name() << ':' << where.line() << ' ';
    if constexpr (std::is_same_v<E, std::string>)
      std::cerr << result.error();
    else if constexpr (requires { result.error().detail; })
      std::cerr << result.error().detail;
    else if constexpr (std::is_enum_v<E>)
      std::cerr << static_cast<unsigned>(result.error());
    std::cerr << '\n';
    throw std::runtime_error("Jump strain fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*result);
}
inline auto bytes(const NativeFreedomFlightSession& s) -> std::string {
  return need(encode_native_save_document_json(need(s.save_document())));
}
inline auto continued(const NativeFreedomFlightSession& s)
    -> NativeFreedomFlightSession {
  const auto text = bytes(s);
  auto result =
      need(NativeFreedomFlightSession::open(need(native_select_save_document(
          need(decode_native_save_document_json(text))))));
  check(bytes(result) == text,
        "Continue preserves the full selected strain voyage");
  return result;
}
inline auto source(std::uint32_t chart = 2, bool strain = true)
    -> NativeFreedomFlightSession {
  auto s = need(NativeFreedomFlightSession::open(
      need(strain ? native_jump_strain_new_game({42}, chart)
                  : native_jump_recovery_new_game({42}, chart))));
  for (unsigned i = 0; i < 1194; ++i)
    need(s.advance_walk({0, -1, 0}));
  need(s.begin_boarding());
  for (SimulationTick i = 0; i < kGameplayBoardingTicks; ++i)
    need(s.advance_walk({}));
  need(s.release_port());
  auto document =
      std::get<FreedomKnowledgeSaveDocument>(need(s.save_document()));
  auto& flight =
      std::get<FreedomBoardingSaveDocument>(document.voyage.voyage.base)
          .voyage.flight;
  const auto home = need(
      find_local_system_planet(s.system(), flight.origin.recipe.home_planet));
  // Labelled valid saved-space boundary, not a flown station ascent. Retain
  // actual boarding, craft, model, clock, chart and complete resource ledger.
  flight.flight.position_metres = {
      0, 0, static_cast<double>(home->descriptor.radius.value) * 10000};
  flight.flight.linear_velocity_metres_per_second = {};
  flight.flight.angular_velocity_radians_per_second = {};
  flight.flight = need(canonicalize_rigid_body_state(
      RigidBodyWorldContext{s.system()}, flight.flight));
  return need(NativeFreedomFlightSession::open(
      need(native_select_save_document(document))));
}
inline auto current(const NativeFreedomFlightSession& s) -> BodyTarget {
  return {s.system().catalog.id, body_id(*s.document().flight.frame.planet)};
}
inline auto planet(const NativeFreedomFlightSession& s) -> BodyTarget {
  const auto rows = need(resolve_freedom_known_planets(
      *s.knowledge(), current(s), s.document().flight.tick));
  for (const auto& row : rows)
    if (!row.current) return row.target;
  throw std::runtime_error("Expected another charted planet");
}
inline auto companion(bool minor) -> BodyTarget {
  const auto catalog = need(generate_secondary_body_catalog(
      need(generate_physical_origin_system({42}, 2))));
  return {catalog.owner.catalog.id, catalog.bodies[minor ? 1 : 0].node.id};
}
inline auto reading(const NativeFreedomFlightSession& s)
    -> FreedomJumpRecoveryReading {
  return need(freedom_jump_recovery_reading(*s.resources()->drive,
                                            s.document().flight.tick));
}
inline auto leg(NativeFreedomFlightSession& s, FreedomJumpRecoveryKind kind,
                FreedomTravelSaveDocument* transit = nullptr) -> void {
  const auto prior = *s.resources()->drive;
  const auto tick = s.document().flight.tick;
  const auto charges = s.resources()->jump_charges;
  const auto attempt = s.travel()->next_attempt;
  const auto acquired = s.knowledge()->field_samples;
  const auto at_commit =
      need(freedom_jump_recovery_reading(prior, tick + kJumpSpoolTicks));
  const auto cycle =
      prior.version == kFreedomStrainedDriveRecoveryVersion
          ? std::min(kFreedomDriveMaximumRecoveryTicks,
                     kFreedomDriveRecoveryTicks + at_commit.remaining_ticks)
          : kFreedomDriveRecoveryTicks;
  need(s.begin_jump());
  s = continued(s);
  for (SimulationTick i = 1; i <= kJumpSpoolTicks + kJumpTransitTicks; ++i) {
    const auto step = need(s.advance({}));
    if (i < kJumpSpoolTicks)
      check(s.resources()->drive == prior && !step.jump_committed,
            "Spool advances physical time without assigning strain early");
    if (i == kJumpSpoolTicks) {
      const auto& stamp = *s.resources()->drive->last_use;
      check(step.jump_committed && stamp.kind == kind &&
                stamp.attempt == attempt && stamp.recovery_ticks == cycle &&
                stamp.commit_tick == tick + i && reading(s).pending_arrival &&
                reading(s).cycle_ticks == cycle &&
                s.resources()->jump_charges + 1 == charges,
            "Typed commitment binds actual debt, attempt, clock and exactly "
            "one charge");
      if (transit) *transit = need(s.travel_document());
    }
    if (i == kJumpSpoolTicks - 1 || i == kJumpSpoolTicks ||
        i == kJumpSpoolTicks + 1 || i == kJumpSpoolTicks + kJumpTransitTicks)
      s = continued(s);
  }
  check(s.document().flight.tick ==
                tick + kJumpSpoolTicks + kJumpTransitTicks &&
            reading(s).remaining_ticks == cycle &&
            reading(s).cycle_ticks == cycle && !reading(s).pending_arrival &&
            !reading(s).ready &&
            reading(s).extra_radius_millimetres ==
                kFreedomJumpRecoveryMaximumRadiusMillimetres &&
            s.knowledge()->field_samples == acquired,
        "Arrival starts the complete new cycle with one clock and unchanged "
        "specimens");
}
} // namespace jump_strain_fixture
