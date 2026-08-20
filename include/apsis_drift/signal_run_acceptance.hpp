#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/celestial.hpp"
#include "apsis_drift/render_profile.hpp"
#include "apsis_drift/signal_run.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift {

inline constexpr std::string_view kSignalRunAcceptanceScenario{
    "v0.4.32-home-signal-run"};
inline constexpr std::uint32_t kSignalRunAcceptanceSeed{42};
inline constexpr std::uint32_t kSignalRunDefaultSeed{0xC0FFEEU};
inline constexpr std::uint32_t kSignalRunDenseSeed{1U};
inline constexpr SimulationTick kSignalRunAcceptanceMaximumTicks{750'000};
inline constexpr SimulationTick kSignalRunAcceptanceResumeTick{600};
inline constexpr SimulationTick kSignalRunAtmosphericPacingTargetTicks{4'200};
inline constexpr SimulationTick kSignalRunReachedPacingTargetTicks{18'000};
inline constexpr SimulationTick kAtmosphericLegPacingTargetTicks{14'400};
inline constexpr SimulationTick kTerrainSafetyProbeTicks{120'000};

enum class SunCheckpointVisibility : std::uint8_t {
  visible,
  planet_occluded,
  reemerged,
};

struct SunCycleCheckpointMeasurement {
  SunCheckpointVisibility visibility{};
  SimulationTick tick{};
  PlanetFixedDirection direction;
  std::size_t sun_pixels{};
  std::uint64_t framebuffer_checksum{};
};

struct SignalRunScenarioMeasurement {
  std::uint32_t seed{};
  IntersystemRuleProfile rule_profile{IntersystemRuleProfile::assisted};
  AtmosphereClass atmosphere_class{};
  SimulationTick atmospheric_tick{};
  SimulationTick terrain_tick{};
  SimulationTick reached_tick{};
  SimulationTick completion_tick{};
  SimulationTick orbital_return_tick{};
  double minimum_clearance_metres{};
  std::uint64_t atmospheric_framebuffer_checksum{};
  std::uint64_t return_flight_checksum{};
  std::uint32_t peak_thermal_load_units{};
  bool thermal_abort_observed{};
};

struct SignalRunSaveCheckpointMeasurement {
  std::string name;
  SimulationTick tick{};
  std::uint64_t save_checksum{};
};

struct SignalRunAcceptanceReport {
  RenderConfiguration render_configuration;
  OriginStationId station_id;
  HomeSignalContractId contract_id;
  SurfaceSignalId target_id;
  SimulationTick launch_tick{};
  double initial_distance_metres{};
  SimulationTick first_motion_tick{};
  SimulationTick orbital_acceleration_ticks{};
  SimulationTick orbital_braking_ticks{};
  double peak_orbital_speed_metres_per_second{};
  SimulationTick atmospheric_tick{};
  SimulationTick terrain_tick{};
  SimulationTick reached_tick{};
  SimulationTick completion_tick{};
  SimulationTick orbital_return_tick{};
  SimulationTick resume_tick{};
  std::uint64_t checkpoint_flight_checksum{};
  std::uint64_t resumed_flight_checksum{};
  LocalSunGeometry checkpoint_sun;
  std::uint64_t checkpoint_framebuffer_checksum{};
  std::uint64_t resumed_framebuffer_checksum{};
  std::uint64_t return_flight_checksum{};
  std::uint64_t framebuffer_checksum{};
  SimulationTick terrain_safety_probe_ticks{};
  double terrain_safety_minimum_clearance_metres{};
  std::uint64_t terrain_safety_flight_checksum{};
  std::size_t discovery_count{};
  std::size_t world_delta_count{};
  std::vector<SignalRunSaveCheckpointMeasurement> save_checkpoints;
  std::vector<SunCycleCheckpointMeasurement> sun_cycle;
  std::vector<SignalRunScenarioMeasurement> scenarios;
};

struct SignalRunAcceptanceResult {
  SignalRunAcceptanceReport report;
  SaveDocument returned_save;
  std::vector<termforge::Pixel> final_frame;
};

enum class SignalRunAcceptanceError : std::uint8_t {
  invalid_configuration,
  initialization_failure,
  simulation_failure,
  incomplete_path,
  checkpoint_write_failure,
  checkpoint_load_failure,
  resume_mismatch,
  presentation_failure,
};

[[nodiscard]] auto run_signal_run_acceptance(
    RenderConfiguration configuration,
    const std::filesystem::path& checkpoint_path)
    -> std::expected<SignalRunAcceptanceResult, SignalRunAcceptanceError>;

[[nodiscard]] auto signal_run_acceptance_json(
    const SignalRunAcceptanceReport& report) -> std::string;

}  // namespace apsis_drift
