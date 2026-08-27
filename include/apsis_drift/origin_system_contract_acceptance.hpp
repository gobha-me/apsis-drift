#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/origin_system_contract.hpp"
#include "apsis_drift/save_schema.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift {

inline constexpr std::string_view kOriginSystemContractAcceptanceScenario{
    "v0.4.33-origin-system-transfer"};
inline constexpr std::uint32_t kOriginSystemContractAcceptanceSeed{42};

struct OriginSystemContractCheckpoint {
  std::string name;
  SimulationTick tick{};
  std::uint64_t save_checksum{};

  friend auto operator==(const OriginSystemContractCheckpoint&,
                         const OriginSystemContractCheckpoint&) -> bool =
      default;
};

struct OriginSystemContractAcceptanceReport {
  OriginSystemContractBinding binding;
  SimulationTick outbound_tick{};
  SimulationTick target_insertion_tick{};
  SimulationTick objective_tick{};
  SimulationTick return_tick{};
  SimulationTick rendezvous_tick{};
  SimulationTick final_tick{};
  std::uint64_t outbound_checksum{};
  std::uint64_t return_checksum{};
  std::uint64_t final_station_checksum{};
  std::uint64_t framebuffer_checksum{};
  std::vector<OriginSystemContractCheckpoint> checkpoints;

  friend auto operator==(const OriginSystemContractAcceptanceReport&,
                         const OriginSystemContractAcceptanceReport&) -> bool =
      default;
};

struct OriginSystemContractAcceptanceResult {
  OriginSystemContractAcceptanceReport report;
  SaveDocument returned_save;
  std::vector<termforge::Pixel> final_frame;
};

enum class OriginSystemContractAcceptanceError : std::uint8_t {
  invalid_configuration,
  initialization_failure,
  transition_failure,
  simulation_failure,
  persistence_failure,
  incomplete_path,
  presentation_failure,
  cadence_mismatch,
};

[[nodiscard]] auto run_origin_system_contract_acceptance(int width, int height)
    -> std::expected<OriginSystemContractAcceptanceResult,
                     OriginSystemContractAcceptanceError>;

[[nodiscard]] auto run_origin_system_contract_acceptance(
    const SaveDocument& starting_document, int width, int height)
    -> std::expected<OriginSystemContractAcceptanceResult,
                     OriginSystemContractAcceptanceError>;

[[nodiscard]] auto origin_system_contract_acceptance_json(
    const OriginSystemContractAcceptanceReport& report) -> std::string;

}  // namespace apsis_drift
