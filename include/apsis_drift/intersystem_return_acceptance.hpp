#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "termforge/core/types.hpp"
#include "apsis_drift/intersystem_contract.hpp"

namespace apsis_drift {

inline constexpr std::uint64_t kIntersystemReturnAcceptanceSeed{42};
inline constexpr std::string_view kIntersystemReturnAcceptanceScenario{
    "v0.4.12-intersystem-return"};

struct IntersystemReturnAcceptanceReport {
  OriginStationId station;
  SimulationTick departure_tick{};
  SimulationTick return_commit_tick{};
  SimulationTick origin_arrival_tick{};
  SimulationTick docking_tick{};
  std::uint64_t departure_checksum{};
  std::uint64_t origin_arrival_checksum{};
  std::uint64_t docked_return_checksum{};
  std::size_t discovery_count{};
  std::size_t world_delta_count{};
  int width{};
  int height{};
  std::uint64_t framebuffer_checksum{};
};

struct IntersystemReturnAcceptanceResult {
  IntersystemReturnAcceptanceReport report;
  std::vector<termforge::Pixel> final_frame;
};

enum class IntersystemReturnAcceptanceError : std::uint8_t {
  invalid_configuration,
  initialization_failure,
  transition_failure,
  persistence_failure,
  simulation_failure,
  presentation_failure,
  incomplete_path,
};

[[nodiscard]] auto run_intersystem_return_acceptance(int width, int height)
    -> std::expected<IntersystemReturnAcceptanceResult,
                     IntersystemReturnAcceptanceError>;

[[nodiscard]] auto intersystem_return_acceptance_json(
    const IntersystemReturnAcceptanceReport& report,
    std::string_view presentation) -> std::string;

}  // namespace apsis_drift
