#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/intersystem_contract.hpp"
#include "apsis_drift/onboarding.hpp"

namespace apsis_drift {

// Version 1 intentionally describes only the authored origin/first-target
// route. It is generated-world compatibility data, not a general galaxy
// catalog or mutable discovery store.
inline constexpr std::uint32_t kUniverseNavigationVersion{1};
inline constexpr std::int64_t kMetresPerLightSecond{299'792'458};
inline constexpr std::uint64_t kMinimumFirstRouteLightSeconds{48U * 3'600U};
inline constexpr std::uint64_t kMaximumFirstRouteLightSeconds{96U * 3'600U};
inline constexpr std::int64_t kLocalSystemBoundaryMetres{100'000'000'000LL};
inline constexpr std::uint64_t kDirectCruiseMaximumSpeedMetresPerSecond{
    1'000'000U};
inline constexpr SimulationTick kDirectCruiseMaximumTimeScale{65'536U};

struct UniversePositionMetres {
  std::int64_t x{};
  std::int64_t y{};
  std::int64_t z{};

  friend auto operator==(const UniversePositionMetres&,
                         const UniversePositionMetres&) -> bool = default;
};

enum class UniverseAxisDirection : std::uint8_t {
  positive_x,
  negative_x,
  positive_y,
  negative_y,
  positive_z,
  negative_z,
};

[[nodiscard]] auto universe_axis_direction_name(
    UniverseAxisDirection direction) noexcept -> std::string_view;

struct FirstUniverseRoute {
  Seed universe_seed;
  Seed route_seed;
  SystemId origin;
  SystemId destination;
  UniverseAxisDirection direction{UniverseAxisDirection::positive_x};
  std::uint64_t distance_light_seconds{};
  std::uint64_t distance_metres{};
  UniversePositionMetres origin_position;
  UniversePositionMetres destination_position;

  friend auto operator==(const FirstUniverseRoute&, const FirstUniverseRoute&)
      -> bool = default;
};

enum class NavigationKnowledgeLevel : std::uint8_t {
  contact,
  probable,
  resolved,
  visited,
};

[[nodiscard]] auto navigation_knowledge_level_name(
    NavigationKnowledgeLevel level) noexcept -> std::string_view;

struct NavigationKnowledge {
  SystemId system;
  NavigationKnowledgeLevel level{NavigationKnowledgeLevel::contact};

  friend auto operator==(const NavigationKnowledge&, const NavigationKnowledge&)
      -> bool = default;
};

enum class NavigationDisabledReason : std::uint8_t {
  none,
  current_system,
  requires_resolved_position,
  onboarding_locked,
  insufficient_endurance,
  unavailable_during_travel,
};

[[nodiscard]] auto navigation_disabled_reason_name(
    NavigationDisabledReason reason) noexcept -> std::string_view;

struct NavigationDestinationStatus {
  SystemId system;
  NavigationKnowledgeLevel knowledge{NavigationKnowledgeLevel::contact};
  bool known{};
  bool valid{};
  bool authorized{};
  bool affordable{};
  bool available{};
  bool selectable{};
  NavigationDisabledReason disabled_reason{NavigationDisabledReason::none};
  std::optional<UniversePositionMetres> position;
  std::optional<std::uint64_t> distance_metres;

  friend auto operator==(const NavigationDestinationStatus&,
                         const NavigationDestinationStatus&) -> bool = default;
};

struct UniverseNavigationView {
  SystemId current_system;
  std::vector<NavigationDestinationStatus> destinations;

  friend auto operator==(const UniverseNavigationView&,
                         const UniverseNavigationView&) -> bool = default;
};

enum class DirectCruiseTimeScale : std::uint32_t {
  one = 1,
  sixteen = 16,
  two_hundred_fifty_six = 256,
  four_thousand_ninety_six = 4'096,
  sixty_five_thousand_five_hundred_thirty_six = 65'536,
};

struct DirectTravelPlan {
  SystemId origin;
  SystemId destination;
  SimulationTick departure_tick{};
  SimulationTick arrival_tick{};
  std::uint64_t cruise_distance_metres{};
  std::uint64_t speed_metres_per_second{};
  UniversePositionMetres departure_position;
  UniversePositionMetres arrival_position;
  UniversePositionMetres velocity_metres_per_second;

  friend auto operator==(const DirectTravelPlan&, const DirectTravelPlan&)
      -> bool = default;
};

// Focus and pending selection belong to the live universe-view interaction,
// not to generated truth or save state. A load may therefore reopen the view
// with a fresh focus without changing an authoritative checksum.
struct UniverseNavigationSelectionState {
  std::size_t focused_index{};
  std::optional<SystemId> pending_destination;

  friend auto operator==(const UniverseNavigationSelectionState&,
                         const UniverseNavigationSelectionState&)
      -> bool = default;
};

enum class UniverseNavigationSelectionCommand : std::uint8_t {
  previous,
  next,
  select,
};

struct DirectTravelSample {
  SimulationTick tick{};
  UniversePositionMetres position;
  bool arrived{};

  friend auto operator==(const DirectTravelSample&, const DirectTravelSample&)
      -> bool = default;
};

enum class UniverseNavigationError : std::uint8_t {
  invalid_route,
  invalid_onboarding,
  invalid_knowledge,
  unknown_system,
  invalid_context,
  invalid_speed,
  invalid_time_scale,
  tick_overflow,
  unsafe_arithmetic,
};

[[nodiscard]] auto generate_first_universe_route(Seed universe_seed) noexcept
    -> FirstUniverseRoute;

[[nodiscard]] auto validate_first_universe_route(
    const FirstUniverseRoute& route) noexcept
    -> std::expected<void, UniverseNavigationError>;

// Resolves one known row without exposing any undiscovered system. A resolved
// coordinate is required for geometric validity. Authorization, affordability,
// route availability, and immediate UI selectability remain distinct facts.
[[nodiscard]] auto resolve_navigation_destination(
    const FirstUniverseRoute& route, NavigationKnowledge knowledge,
    SystemId current_system, bool authorized, bool affordable,
    bool selection_open) noexcept
    -> std::expected<NavigationDestinationStatus, UniverseNavigationError>;

// Projects the bounded starting chart. Contract one/two show only the current
// origin. Contract three, skipped, and completed careers also know the authored
// first target without recording an earned discovery.
[[nodiscard]] auto resolve_onboarding_navigation_view(
    const FirstUniverseRoute& route, const OnboardingProgress& onboarding,
    SystemId current_system, bool affordable = true,
    bool selection_open = true) noexcept
    -> std::expected<UniverseNavigationView, UniverseNavigationError>;

// Applies one bounded view command atomically. Disabled rows may receive
// focus for their explanation, but only a selectable row may become pending.
[[nodiscard]] auto advance_universe_navigation_selection(
    const UniverseNavigationView& view,
    UniverseNavigationSelectionState& selection,
    UniverseNavigationSelectionCommand command) noexcept
    -> std::expected<void, UniverseNavigationError>;

[[nodiscard]] auto make_direct_travel_plan(const FirstUniverseRoute& route,
                                           SystemId origin,
                                           SystemId destination,
                                           SimulationTick departure_tick,
                                           double speed_metres_per_second)
    -> std::expected<DirectTravelPlan, UniverseNavigationError>;

[[nodiscard]] auto resolve_direct_travel(const DirectTravelPlan& plan,
                                         SimulationTick tick) noexcept
    -> std::expected<DirectTravelSample, UniverseNavigationError>;

// One application update advances an exact number of authoritative ticks and
// clamps at arrival. It never integrates one physics step per skipped tick.
[[nodiscard]] auto advance_direct_travel(const DirectTravelPlan& plan,
                                         SimulationTick tick,
                                         DirectCruiseTimeScale scale) noexcept
    -> std::expected<SimulationTick, UniverseNavigationError>;

[[nodiscard]] auto direct_travel_save_projection_json(
    const DirectTravelPlan& plan, SimulationTick tick,
    DirectCruiseTimeScale scale)
    -> std::expected<std::string, UniverseNavigationError>;

} // namespace apsis_drift
