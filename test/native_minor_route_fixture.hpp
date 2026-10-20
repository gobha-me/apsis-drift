#pragma once
// Actual command-driven test/export route. No world, pose, clock or resource
// injection; this is not a gameplay autopilot or a landing fixture.
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/save_file.hpp"

#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace apsis_drift::minor_route_fixture {
inline auto ensure(bool good, const char* message) -> void {
  if (!good) throw std::runtime_error(message);
}
template <class T, class E> inline auto need(std::expected<T, E> value) -> T {
  if (!value) {
    if constexpr (std::is_same_v<E, std::string>)
      throw std::runtime_error(value.error());
    else if constexpr (std::is_same_v<E, SaveSchemaError>)
      throw std::runtime_error(value.error().path + ": " +
                               value.error().detail);
    else
      throw std::runtime_error("Minor route provider refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
inline auto bytes(const NativeFreedomFlightSession& session) -> std::string {
  return need(encode_native_save_document_json(need(session.save_document())));
}
inline auto continued(const NativeFreedomFlightSession& session)
    -> NativeFreedomFlightSession {
  const auto source = bytes(session);
  auto next =
      need(NativeFreedomFlightSession::open(need(native_select_save_document(
          need(decode_native_save_document_json(source))))));
  ensure(bytes(next) == source,
         "Minor route Continue changed complete save bytes");
  return next;
}
inline auto departure() -> NativeFreedomFlightSession {
  auto s =
      need(NativeFreedomFlightSession::open(need(native_new_game(Seed{42}))));
  ensure(s.walker().has_value(), "Minor route must start on actual station");
  for (unsigned i = 0; i < 1194; ++i)
    need(s.advance_walk({0, -1, 0}));
  need(s.begin_boarding());
  for (unsigned i = 0; i < kGameplayBoardingTicks; ++i)
    need(s.advance_walk({}));
  need(s.release_port());
  NativeFlightControls demand{};
  demand.negative_translation.y = .25;
  for (unsigned i = 0; i < 720; ++i)
    need(s.advance(demand));
  return s;
}
inline auto minor_target(const NativeFreedomFlightSession& s) -> BodyTarget {
  const auto catalog = need(generate_secondary_body_catalog(s.system()));
  return {s.system().catalog.id, catalog.bodies[1].node.id};
}
inline auto home_target(const NativeFreedomFlightSession& s) -> BodyTarget {
  return {s.system().catalog.id,
          body_id(s.document().origin.recipe.home_planet)};
}
inline auto leg(NativeFreedomFlightSession& s) -> void {
  const auto charges = s.resources()->jump_charges;
  const auto attempt = s.travel()->next_attempt;
  need(s.begin_jump());
  s = continued(s);
  for (SimulationTick i = 1; i <= kJumpSpoolTicks + kJumpTransitTicks; ++i) {
    const auto step = need(s.advance({}));
    if (i < kJumpSpoolTicks)
      ensure(s.resources()->jump_charges == charges, "Spool debited a charge");
    if (i == kJumpSpoolTicks) {
      ensure(step.jump_committed &&
                 s.resources()->jump_charges == charges - 1 &&
                 s.travel()->next_attempt == attempt + 1,
             "Commit must debit one charge/attempt");
      ensure(!s.cancel_jump(), "Committed transit allowed cancellation");
      s = continued(s);
    }
    if (i > kJumpSpoolTicks)
      ensure(step.fuel_debit_quanta == 0 &&
                 s.resources()->flight_quanta ==
                     s.travel()->local->commitment_resources->flight_quanta,
             "Transit burned propulsion fuel");
    if (i == kJumpSpoolTicks + kJumpTransitTicks)
      ensure(step.jump_arrived, "Actual local arrival missing");
  }
  s = continued(s);
}
inline auto approach(NativeFreedomFlightSession& s) -> unsigned {
  NativeFlightControls demand{};
  demand.negative_translation.y = 1;
  for (unsigned i = 1; i <= 24000; ++i) {
    const auto before = s.document().flight;
    const auto fuel = s.resources()->flight_quanta;
    const auto step = need(s.advance(demand));
    ensure(!step.propulsion_refused,
           "Minor approach exhausted propulsion fuel");
    if (step.minor_standoff_limited) {
      const auto& after = s.document().flight;
      ensure(after.tick == before.tick + 1 &&
                 after.position_metres == before.position_metres &&
                 s.resources()->flight_quanta + step.fuel_debit_quanta == fuel,
             "Stand-off response lost real tick/position/fuel transaction");
      const auto p = after.position_metres,
                 v = after.linear_velocity_metres_per_second;
      ensure(((p.x * v.x + p.y * v.y) + p.z * v.z) >= -1e-5,
             "Stand-off retained inward radial velocity");
      s = continued(s);
      return i;
    }
  }
  throw std::runtime_error(
      "Bounded actual minor approach did not reach stand-off guard");
}
inline auto withdraw(NativeFreedomFlightSession& s) -> unsigned {
  NativeFlightControls demand{};
  demand.positive_translation.y = 1;
  for (unsigned i = 1; i <= 12000; ++i) {
    const auto step = need(s.advance(demand));
    ensure(!step.propulsion_refused,
           "Minor withdrawal exhausted propulsion fuel");
    const auto observation = need(s.observe());
    ensure(observation.minor.has_value(), "Actual minor observation absent");
    if (observation.minor->altitude_metres >= 2000) {
      ensure(!step.minor_standoff_limited,
             "Outward thrust remained trapped at stand-off");
      s = continued(s);
      return i;
    }
  }
  throw std::runtime_error("Bounded actual minor withdrawal failed");
}
} // namespace apsis_drift::minor_route_fixture
