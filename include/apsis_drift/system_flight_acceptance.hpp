#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "apsis_drift/intersystem_contract.hpp"
#include "apsis_drift/planetary_flight.hpp"
#include "apsis_drift/system_flight.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift {

inline constexpr std::uint64_t kSystemFlightAcceptanceSeed{42};

enum class SystemFlightAcceptanceError : std::uint8_t {
  invalid_configuration,
  jump_failure,
  flight_failure,
  save_failure,
  render_failure,
  insertion_failure,
  cadence_mismatch,
};

struct SystemFlightAcceptanceReport {
  SystemId system;
  PlanetId planet;
  SimulationTick arrival_tick{};
  SimulationTick insertion_tick{};
  SimulationTick host_steps{};
  std::uint64_t system_flight_checksum{};
  std::uint64_t orbital_flight_checksum{};
  std::uint64_t framebuffer_checksum{};
};

struct SystemFlightAcceptanceResult {
  SystemFlightAcceptanceReport report;
  std::vector<termforge::Pixel> final_frame;
};

[[nodiscard]] auto run_system_flight_acceptance(int width, int height)
    -> std::expected<SystemFlightAcceptanceResult, SystemFlightAcceptanceError>;

[[nodiscard]] auto system_flight_acceptance_json(
    const SystemFlightAcceptanceReport& report) -> std::string;

} // namespace apsis_drift
