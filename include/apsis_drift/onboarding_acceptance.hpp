#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/render_profile.hpp"
#include "apsis_drift/save_schema.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift {

inline constexpr std::string_view kOnboardingAcceptanceScenario{
    "v0.4.38-station-to-universe-onboarding"};

struct OnboardingAcceptanceSeedMeasurement {
  Seed seed;
  OriginStationId origin_station;
  PlanetId home_planet;
  SimulationTick final_tick{};
  std::uint64_t guided_final_checksum{};
  std::uint64_t skipped_baseline_checksum{};
  std::size_t guided_discovery_count{};
  std::size_t guided_world_delta_count{};
  std::size_t save_checkpoint_count{};
  bool immutable_identities_match{};
  bool open_exploration_available{};
  bool skipped_history_empty{};
  bool post_onboarding_idle_stable{};
};

struct OnboardingAcceptanceReport {
  RenderConfiguration render_configuration;
  std::vector<OnboardingAcceptanceSeedMeasurement> seeds;
  bool guided_new_game_verified{};
  bool skipped_new_game_verified{};
  bool free_flight_redock_verified{};
  bool pause_resume_verified{};
  bool pilot_recovery_verified{};
  std::uint64_t pilot_recovery_checksum{};
  double contract_three_simulation_ms{};
  double contract_three_application_render_ms{};
  std::string presentation;
  std::uint64_t presentation_framebuffer_checksum{};
  std::uint64_t encoded_bytes{};
  std::size_t encoded_frames{};
};

struct OnboardingAcceptanceResult {
  OnboardingAcceptanceReport report;
  SaveDocument returned_save;
  std::vector<termforge::Pixel> final_frame;
};

enum class OnboardingAcceptanceError : std::uint8_t {
  invalid_configuration,
  signal_run_failure,
  origin_system_failure,
  intersystem_failure,
  persistence_failure,
  identity_mismatch,
  skipped_history_failure,
  incomplete_path,
};

[[nodiscard]] auto run_onboarding_acceptance(
    RenderConfiguration configuration,
    const std::filesystem::path& checkpoint_path)
    -> std::expected<OnboardingAcceptanceResult, OnboardingAcceptanceError>;

[[nodiscard]] auto onboarding_acceptance_json(
    const OnboardingAcceptanceReport& report) -> std::string;

}  // namespace apsis_drift
