#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

#include "apsis_drift/render_profile.hpp"
#include "apsis_drift/simulation.hpp"

namespace apsis_drift {

inline constexpr std::string_view kFlightDeckAcceptanceScenario{
    "v0.2-flight-deck"};
inline constexpr std::uint32_t kFlightDeckAcceptanceSeed{0xC0FFEEU};
inline constexpr int kFlightDeckAcceptanceTerrainSize{1024};
inline constexpr SimulationTick kFlightDeckAcceptanceTicks{240};

[[nodiscard]] auto flight_deck_acceptance_commands() noexcept
    -> std::span<const FlightCommand>;

[[nodiscard]] auto replay_flight_deck_acceptance(
    const Terrain& terrain) noexcept -> std::expected<FlightState, FlightError>;

struct FlightDeckAcceptanceReport {
  std::uint64_t flight_checksum{};
  std::uint64_t framebuffer_checksum{};
  RenderConfiguration render_configuration{};
  std::string_view presentation;
};

[[nodiscard]] auto flight_deck_acceptance_json(
    const FlightDeckAcceptanceReport& report) -> std::string;

} // namespace apsis_drift
