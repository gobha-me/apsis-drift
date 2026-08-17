#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/render_profile.hpp"
#include "apsis_drift/signal_run.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift {

inline constexpr std::string_view kSignalRunAcceptanceScenario{
    "v0.4.1-signal-run"};
inline constexpr std::uint32_t kSignalRunAcceptanceSeed{42};
inline constexpr SimulationTick kSignalRunAcceptanceMaximumTicks{500'000};
inline constexpr SimulationTick kSignalRunAcceptanceResumeTick{600};
inline constexpr SimulationTick kSignalRunAtmosphericPacingTargetTicks{4'200};
inline constexpr SimulationTick kSignalRunReachedPacingTargetTicks{18'000};

struct SignalRunAcceptanceReport {
  RenderConfiguration render_configuration;
  std::string presentation;
  OriginStationId station_id;
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
  std::uint64_t return_flight_checksum{};
  std::uint64_t framebuffer_checksum{};
  std::size_t discovery_count{};
  std::size_t world_delta_count{};
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
    const std::filesystem::path& checkpoint_path,
    std::string_view presentation)
    -> std::expected<SignalRunAcceptanceResult, SignalRunAcceptanceError>;

[[nodiscard]] auto signal_run_acceptance_json(
    const SignalRunAcceptanceReport& report) -> std::string;

}  // namespace apsis_drift
