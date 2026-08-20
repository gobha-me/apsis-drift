#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

#include "apsis_drift/universe_navigation.hpp"

namespace apsis_drift {

inline constexpr std::uint64_t kUniverseNavigationAcceptanceSeed{42};

enum class UniverseNavigationAcceptanceError : std::uint8_t {
  generation_failure,
  projection_failure,
  travel_failure,
  persistence_failure,
};

struct UniverseNavigationAcceptanceReport {
  FirstUniverseRoute route;
  std::size_t visible_rows{};
  std::size_t selectable_rows{};
  SimulationTick ftl_total_ticks{};
  DirectTravelPlan direct_plan;
  std::uint64_t maximum_scale_updates{};
  std::uint64_t maximum_scale_realtime_milliseconds{};
  std::size_t projected_save_bytes{};
  std::uint64_t direct_arrival_checksum{};
  double application_renderer_budget_ms{};
};

[[nodiscard]] auto run_universe_navigation_acceptance()
    -> std::expected<UniverseNavigationAcceptanceReport,
                     UniverseNavigationAcceptanceError>;

[[nodiscard]] auto universe_navigation_acceptance_json(
    const UniverseNavigationAcceptanceReport& report) -> std::string;

}  // namespace apsis_drift
