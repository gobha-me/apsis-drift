#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/planetary_presentation.hpp"
#include "apsis_drift/render_profile.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift {

inline constexpr std::string_view kPlanetfallAcceptanceScenario{
    "v0.3-planetfall"};
inline constexpr std::uint32_t kPlanetfallAcceptanceSeed{42U};
inline constexpr std::size_t kPlanetfallAcceptanceFramesPerStage{60};
inline constexpr SimulationTick kPlanetfallAcceptanceTicks{119'360};

struct PlanetfallStageMeasurement {
  PlanetaryPresentationMode presentation_mode{};
  FlightRegime flight_regime{};
  SimulationTick tick{};
  GeodeticPosition position;
  double clearance_metres{};
  std::uint64_t flight_checksum{};
  std::uint64_t framebuffer_checksum{};
  TerrainTileAddress surface_anchor;
  std::size_t orbital_tiles_touched{};
  std::size_t local_tiles_touched{};
  double orbital_render_avg_ms{};
  double local_render_avg_ms{};
  double composite_avg_ms{};
  double total_avg_ms{};
  double total_p95_ms{};
};

struct PlanetfallAcceptanceReport {
  RenderConfiguration render_configuration{};
  PlanetId planet_id;
  std::string planet_name;
  PlanetaryFlightState final_state;
  std::size_t command_count{};
  std::vector<PlanetfallStageMeasurement> stages;
};

struct PlanetfallAcceptanceResult {
  PlanetfallAcceptanceReport report;
  std::vector<termforge::Pixel> final_frame;
};

enum class PlanetfallAcceptanceError : std::uint8_t {
  invalid_configuration,
  terrain_failure,
  flight_failure,
  presentation_failure,
  incomplete_path,
};

[[nodiscard]] auto run_planetfall_acceptance(
    RenderConfiguration configuration)
    -> std::expected<PlanetfallAcceptanceResult, PlanetfallAcceptanceError>;

[[nodiscard]] auto planetfall_acceptance_json(
    const PlanetfallAcceptanceReport& report) -> std::string;

}  // namespace apsis_drift
