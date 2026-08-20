#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/intersystem_contract.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift {

inline constexpr std::uint64_t kIntersystemContractAcceptanceSeed{42};
inline constexpr std::string_view kIntersystemContractAcceptanceScenario{
    "v0.4.35-first-jump-onboarding"};

struct IntersystemContractAcceptanceCheckpoint {
  std::string name;
  SimulationTick tick{};
  std::uint64_t authoritative_checksum{};
  std::uint64_t resumed_final_checksum{};
};

struct IntersystemContractAcceptanceReport {
  MissionId mission;
  SystemId target_system;
  PlanetId target_planet;
  SurfaceSignalId target_objective;
  OriginStationId origin_station;
  SystemId outbound_selected_system;
  SystemId return_selected_system;
  std::size_t universe_navigation_rows{};
  bool open_exploration_available{};
  std::vector<IntersystemContractAcceptanceCheckpoint> checkpoints;
  SimulationTick final_tick{};
  std::uint64_t final_authoritative_checksum{};
  std::uint64_t wrong_side_recovery_checksum{};
  std::size_t target_system_planet_count{};
  std::uint64_t target_system_initial_framebuffer_checksum{};
  std::uint64_t target_system_moved_framebuffer_checksum{};
  std::size_t discovery_count{};
  std::size_t world_delta_count{};
  int width{};
  int height{};
  std::uint64_t framebuffer_checksum{};
  double simulation_ms{};
  double application_render_ms{};
};

struct IntersystemContractAcceptanceResult {
  IntersystemContractAcceptanceReport report;
  std::vector<termforge::Pixel> final_frame;
};

enum class IntersystemContractAcceptanceError : std::uint8_t {
  invalid_configuration,
  initialization_failure,
  transition_failure,
  simulation_failure,
  persistence_failure,
  presentation_failure,
  recovery_failure,
  incomplete_path,
  resume_mismatch,
};

[[nodiscard]] auto run_intersystem_contract_acceptance(int width, int height)
    -> std::expected<IntersystemContractAcceptanceResult,
                     IntersystemContractAcceptanceError>;

[[nodiscard]] auto intersystem_contract_acceptance_json(
    const IntersystemContractAcceptanceReport& report) -> std::string;

}  // namespace apsis_drift
