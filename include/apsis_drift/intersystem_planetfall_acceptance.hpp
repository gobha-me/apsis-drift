#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/intersystem_planetfall.hpp"
#include "apsis_drift/planetary_presentation.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift {

inline constexpr std::uint64_t kIntersystemPlanetfallAcceptanceSeed{42};
inline constexpr std::string_view kIntersystemPlanetfallAcceptanceScenario{
    "v0.4.11-entry-anywhere-planetfall"};

struct IntersystemPlanetfallEntryMeasurement {
  std::string name;
  GeodeticPosition initial_position;
  GeodeticPosition terrain_position;
  SimulationTick terrain_tick{};
  std::uint64_t flight_checksum{};
  TerrainTileAddress terrain_anchor;

  friend auto operator==(const IntersystemPlanetfallEntryMeasurement&,
                         const IntersystemPlanetfallEntryMeasurement&)
      -> bool = default;
};

struct IntersystemPlanetfallAcceptanceReport {
  PlanetId planet;
  SurfaceSignalId target;
  std::array<IntersystemPlanetfallEntryMeasurement, 3> entries;
  SimulationTick abort_orbit_tick{};
  std::uint64_t abort_orbit_checksum{};
  SimulationTick completion_tick{};
  std::uint64_t completed_flight_checksum{};
  std::size_t world_delta_count{};
  std::uint64_t framebuffer_checksum{};
};

struct IntersystemPlanetfallAcceptanceResult {
  IntersystemPlanetfallAcceptanceReport report;
  std::vector<termforge::Pixel> final_frame;
};

enum class IntersystemPlanetfallAcceptanceError : std::uint8_t {
  invalid_configuration,
  initialization_failure,
  simulation_failure,
  incomplete_path,
  save_failure,
  cadence_mismatch,
  presentation_failure,
};

[[nodiscard]] auto run_intersystem_planetfall_acceptance(int width, int height)
    -> std::expected<IntersystemPlanetfallAcceptanceResult,
                     IntersystemPlanetfallAcceptanceError>;

[[nodiscard]] auto intersystem_planetfall_acceptance_json(
    const IntersystemPlanetfallAcceptanceReport& report,
    std::string_view presentation) -> std::string;

}  // namespace apsis_drift
