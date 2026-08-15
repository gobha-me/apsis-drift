#include "apsis_drift/flight_deck_acceptance.hpp"

#include <array>
#include <format>

namespace apsis_drift {
namespace {

constexpr std::array kCommands{
    FlightCommand{0, FlightCommandKind::toggle_autopilot},
    FlightCommand{0, FlightCommandKind::press_forward},
    FlightCommand{18, FlightCommandKind::press_turn_right},
    FlightCommand{36, FlightCommandKind::press_turn_left},
    FlightCommand{48, FlightCommandKind::press_strafe_right},
    FlightCommand{60, FlightCommandKind::release_turn_right},
    FlightCommand{72, FlightCommandKind::release_turn_left},
    FlightCommand{84, FlightCommandKind::press_rise},
    FlightCommand{96, FlightCommandKind::release_strafe_right},
    FlightCommand{108, FlightCommandKind::release_rise},
    FlightCommand{120, FlightCommandKind::press_backward},
    FlightCommand{132, FlightCommandKind::release_forward},
    FlightCommand{144, FlightCommandKind::press_strafe_left},
    FlightCommand{156, FlightCommandKind::press_fall},
    FlightCommand{168, FlightCommandKind::release_backward},
    FlightCommand{180, FlightCommandKind::release_strafe_left},
    FlightCommand{192, FlightCommandKind::release_fall},
    FlightCommand{204, FlightCommandKind::toggle_autopilot},
};

}  // namespace

auto flight_deck_acceptance_commands() noexcept
    -> std::span<const FlightCommand> {
  return kCommands;
}

auto replay_flight_deck_acceptance(const Terrain& terrain) noexcept
    -> std::expected<FlightState, FlightError> {
  auto initialized = initial_flight_state(terrain);
  if (!initialized) return std::unexpected{initialized.error()};

  auto state = *initialized;
  std::size_t next_command{};
  while (state.tick < kFlightDeckAcceptanceTicks) {
    const auto first = next_command;
    while (next_command < kCommands.size() &&
           kCommands[next_command].tick == state.tick) {
      ++next_command;
    }
    const std::span commands{kCommands.data() + first, next_command - first};
    if (auto advanced =
            advance_flight(terrain, state, commands, kSimulationStep);
        !advanced) {
      return std::unexpected{advanced.error()};
    }
  }
  return state;
}

auto flight_deck_acceptance_json(const FlightDeckAcceptanceReport& report)
    -> std::string {
  return std::format(
      "{{\n"
      "  \"schema_version\": 1,\n"
      "  \"scenario\": \"{}\",\n"
      "  \"seed\": {},\n"
      "  \"simulation_hz\": {},\n"
      "  \"final_tick\": {},\n"
      "  \"command_count\": {},\n"
      "  \"flight_checksum\": \"{}\",\n"
      "  \"framebuffer_checksum\": \"{}\",\n"
      "  \"render_profile\": \"{}\",\n"
      "  \"viewport_width\": {},\n"
      "  \"viewport_height\": {},\n"
      "  \"presentation\": \"{}\"\n"
      "}}\n",
      kFlightDeckAcceptanceScenario, kFlightDeckAcceptanceSeed,
      kSimulationHz, kFlightDeckAcceptanceTicks, kCommands.size(),
      report.flight_checksum, report.framebuffer_checksum,
      profile_name(report.render_configuration),
      report.render_configuration.viewport.width,
      report.render_configuration.viewport.height, report.presentation);
}

}  // namespace apsis_drift
