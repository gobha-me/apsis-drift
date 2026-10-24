#pragma once

#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "apsis_drift/save_file.hpp"

#include <memory>
#include <source_location>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace apsis_drift::test_support {
namespace derelict_recovery_detail {
template <class T, class E>
auto need(std::expected<T, E> value,
          std::source_location at = std::source_location::current()) -> T {
  if (!value) {
    auto message = "Derelict recovery setup line " + std::to_string(at.line());
    if constexpr (std::is_same_v<E, std::string>)
      message += ": " + value.error();
    else if constexpr (requires { value.error().detail; })
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
} // namespace derelict_recovery_detail

// Public opt-in starter, real1194 walking ticks, complete boarding/release,
// two real stellar acquisitions and one actually charged jump/arrival. Exactly
// two declared initialized poses follow the existing derelict fixture: Home
// space before stellar search, then120m source-relative matched-speed approach.
// Neither pose is a flown ascent/approach, retrieval, return or profit claim.
[[nodiscard]] inline auto make_derelict_recovery_fixture(
    Seed seed = {42}, std::uint32_t drive_kind = 0, bool origin_service = false)
    -> std::unique_ptr<NativeFreedomFlightSession> {
  using derelict_recovery_detail::need;
  auto live = std::make_unique<NativeFreedomFlightSession>(need(
      NativeFreedomFlightSession::open(need(native_derelict_recovery_new_game(
          seed, drive_kind, origin_service)))));
  for (unsigned tick = 0; tick < 1194; ++tick)
    need(live->advance_walk({0, -1, 0}));
  need(live->begin_boarding());
  for (SimulationTick tick = 0; tick < kGameplayBoardingTicks; ++tick)
    need(live->advance_walk({}));
  need(live->release_port());
  need(live->set_assistance(false));
  auto source =
      std::make_unique<NativeSaveDocument>(need(live->save_document()));
  auto& mapped =
      origin_service
          ? std::get<FreedomKnowledgeSaveDocument>(
                std::get<FreedomAssistanceSaveDocument>(*source).voyage)
          : std::get<FreedomKnowledgeSaveDocument>(*source);
  auto& home_flight =
      std::get<FreedomBoardingSaveDocument>(mapped.voyage.voyage.base)
          .voyage.flight;
  const auto home = need(find_local_system_planet(
      live->system(), home_flight.origin.recipe.home_planet));
  home_flight.flight.position_metres = {
      0, 0, static_cast<double>(home->descriptor.radius.value) * 10000};
  home_flight.flight.linear_velocity_metres_per_second = {};
  home_flight.flight.angular_velocity_radians_per_second = {};
  home_flight.flight =
      need(canonicalize_rigid_body_state({live->system()}, home_flight.flight));
  live = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_select_save_document(std::move(*source))))));
  for (unsigned discovery = 0; discovery < 2; ++discovery) {
    need(live->begin_star_search());
    for (SimulationTick tick = 0; tick < kFreedomStarSearchTicks; ++tick)
      need(live->advance({}));
  }
  auto descriptor = std::make_unique<FreedomDerelictDescriptor>(
      need(generate_freedom_derelict(seed)));
  need(live->select_jump(descriptor->system));
  need(live->begin_jump());
  for (SimulationTick tick = 0; tick < kJumpSpoolTicks + kJumpTransitTicks;
       ++tick)
    need(live->advance({}));
  need(live->advance({})); // leave frozen instant-arrival admission boundary
  const auto relative = need(live->relative_state());
  if (relative.frame.system != descriptor->system ||
      relative.frame.planet != descriptor->planet ||
      live->resources()->jump_charges != 2 ||
      live->knowledge()->star_search->completed.size() != 2)
    throw std::runtime_error(
        "Derelict recovery setup lost actual discovery/charged arrival");
  auto approached =
      std::make_unique<NativeSaveDocument>(need(live->save_document()));
  auto& voyage =
      origin_service
          ? std::get<FreedomTravelSaveDocument>(
                std::get<FreedomAssistanceSaveDocument>(*approached).voyage)
          : std::get<FreedomTravelSaveDocument>(*approached);
  auto& flight =
      std::get<FreedomFlightSaveDocument>(voyage.voyage.voyage.voyage.base);
  auto hull = need(resolve_freedom_derelict(*descriptor, flight.flight.tick));
  hull.position_metres.x += 120;
  flight.flight = need(reframe_rigid_body({descriptor->owner}, hull,
                                          {flight.flight.frame, hull.tick}));
  live = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_select_save_document(std::move(*approached))))));
  need(live->scan_derelict());
  need(live->inspect_derelict());
  if (!live->knowledge()->derelict_recovery ||
      !live->derelict_view().inspected ||
      !need(live->derelict_recovery_view()).can_begin)
    throw std::runtime_error(
        "Actual scanned/inspected120m fixture cannot begin retrieval");
  return live;
}
} // namespace apsis_drift::test_support
