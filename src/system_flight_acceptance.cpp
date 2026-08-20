#include "apsis_drift/system_flight_acceptance.hpp"

#include <array>
#include <format>
#include <vector>

#include "apsis_drift/intersystem_jump.hpp"
#include "apsis_drift/save_file.hpp"
#include "apsis_drift/save_schema.hpp"
#include "apsis_drift/system_rendering.hpp"

namespace apsis_drift {
namespace {

[[nodiscard]] auto pixels_checksum(
    std::span<const termforge::Pixel> pixels) noexcept -> std::uint64_t {
  std::uint64_t hash{1469598103934665603ULL};
  for (const auto pixel : pixels) {
    for (const std::uint8_t value : {pixel.r, pixel.g, pixel.b, pixel.a}) {
      hash ^= value;
      hash *= 1099511628211ULL;
    }
  }
  return hash;
}

struct Replay {
  IntersystemContractState contract;
  SystemFlightState flight;
  PlanetaryFlightState orbital;
  SimulationTick host_steps{};
  std::vector<termforge::Pixel> frame;
};

[[nodiscard]] auto replay(int width, int height, int render_interval)
    -> std::expected<Replay, SystemFlightAcceptanceError> {
  auto contract = initial_intersystem_contract_state(
      Seed{kSystemFlightAcceptanceSeed});
  const auto system =
      generate_local_system(contract.identities.target_system_seed);
  if (!advance_intersystem_contract(
          contract, contract.universe_tick,
          IntersystemContractCommand::accept_mission) ||
      !advance_intersystem_contract(
          contract, contract.universe_tick,
          IntersystemContractCommand::launch) ||
      !begin_intersystem_jump(contract)) {
    return std::unexpected{SystemFlightAcceptanceError::jump_failure};
  }
  for (SimulationTick tick = 0;
       tick < kJumpSpoolTicks + kJumpTransitTicks; ++tick) {
    if (!advance_intersystem_jump_tick(contract, system)) {
      return std::unexpected{SystemFlightAcceptanceError::jump_failure};
    }
  }
  if (!contract.arrival_solution) {
    return std::unexpected{SystemFlightAcceptanceError::jump_failure};
  }
  auto initialized = initial_system_flight_state(
      system, contract.identities.target_planet, *contract.arrival_solution);
  if (!initialized) {
    return std::unexpected{SystemFlightAcceptanceError::flight_failure};
  }
  auto flight = *initialized;
  const std::array speed_commands{
      FlightCommand{flight.tick, FlightCommandKind::increase_time_scale},
      FlightCommand{flight.tick, FlightCommandKind::increase_time_scale},
  };
  if (!advance_system_flight(system, flight, speed_commands) ||
      !advance_intersystem_time(contract,
                                flight.tick - contract.universe_tick)) {
    return std::unexpected{SystemFlightAcceptanceError::flight_failure};
  }

  LocalSystemRenderer renderer{{.width = width, .height = height}};
  std::vector<termforge::Pixel> frame(
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  bool resumed{};
  constexpr SimulationTick maximum_host_steps{30'000};
  SimulationTick host_steps{1};
  for (; host_steps < maximum_host_steps; ++host_steps) {
    const auto guidance = resolve_system_flight_guidance(system, flight);
    if (!guidance) {
      return std::unexpected{SystemFlightAcceptanceError::flight_failure};
    }
    if (guidance->orbit_insertion_ready) break;
    const auto before = flight.tick;
    if (!advance_system_flight(system, flight, {}) ||
        !advance_intersystem_time(contract, flight.tick - before)) {
      return std::unexpected{SystemFlightAcceptanceError::flight_failure};
    }
    if (!resumed && guidance->inside_approach_boundary) {
      auto document = make_new_game_document(Seed{kSystemFlightAcceptanceSeed},
                                             NewGameOnboardingChoice::skip);
      document.state.intersystem_contract = contract;
      document.state.system_flight = flight;
      const auto encoded = encode_save_document_json(document);
      const auto decoded = encoded ? decode_save_document_json(*encoded)
                                   : std::expected<SaveDocument,
                                                   SaveSchemaError>{
                                         std::unexpected{SaveSchemaError{}}};
      if (!decoded || !decoded->state.intersystem_contract ||
          !decoded->state.system_flight) {
        return std::unexpected{SystemFlightAcceptanceError::save_failure};
      }
      contract = *decoded->state.intersystem_contract;
      flight = *decoded->state.system_flight;
      resumed = true;
    }
    // Exercise both presentation cadences without turning the acceptance
    // trace into a renderer throughput benchmark. Final pixels are rendered
    // again from the authoritative insertion state below.
    if (host_steps <= 120 &&
        host_steps % static_cast<SimulationTick>(render_interval) == 0) {
      const auto rendered = renderer.render(
          system,
          {.time = {flight.tick, 0.0},
           .position = flight.position,
           .velocity = flight.velocity,
           .forward = flight.forward,
           .up = flight.up,
           .selected_planet = flight.target},
          frame);
      if (!rendered) {
        return std::unexpected{SystemFlightAcceptanceError::render_failure};
      }
    }
  }
  if (host_steps >= maximum_host_steps) {
    return std::unexpected{SystemFlightAcceptanceError::flight_failure};
  }
  const auto rendered = renderer.render(
      system,
      {.time = {flight.tick, 0.0},
       .position = flight.position,
       .velocity = flight.velocity,
       .forward = flight.forward,
       .up = flight.up,
       .selected_planet = flight.target},
      frame);
  const auto orbital = insert_system_flight_orbit(system, flight);
  if (!rendered) {
    return std::unexpected{SystemFlightAcceptanceError::render_failure};
  }
  if (!orbital) {
    return std::unexpected{SystemFlightAcceptanceError::insertion_failure};
  }
  return Replay{std::move(contract), std::move(flight), *orbital, host_steps,
                std::move(frame)};
}

}  // namespace

auto run_system_flight_acceptance(int width, int height)
    -> std::expected<SystemFlightAcceptanceResult,
                     SystemFlightAcceptanceError> {
  if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) >
          4'194'304ULL) {
    return std::unexpected{
        SystemFlightAcceptanceError::invalid_configuration};
  }
  const auto at_30 = replay(width, height, 4);
  const auto at_60 = replay(width, height, 2);
  if (!at_30 || !at_60) {
    return std::unexpected{at_30 ? at_60.error() : at_30.error()};
  }
  if (at_30->contract != at_60->contract ||
      at_30->flight != at_60->flight || at_30->orbital != at_60->orbital ||
      at_30->frame != at_60->frame) {
    return std::unexpected{SystemFlightAcceptanceError::cadence_mismatch};
  }
  return SystemFlightAcceptanceResult{
      .report =
          {.system = at_30->flight.system,
           .planet = at_30->flight.target,
           .arrival_tick = at_30->contract.arrival_solution
                               ? at_30->contract.arrival_solution->arrival_tick
                               : 0,
           .insertion_tick = at_30->flight.tick,
           .host_steps = at_30->host_steps,
           .system_flight_checksum =
               system_flight_state_checksum(at_30->flight),
           .orbital_flight_checksum =
               planetary_flight_state_checksum(at_30->orbital),
           .framebuffer_checksum = pixels_checksum(at_30->frame)},
      .final_frame = std::move(at_30->frame),
  };
}

auto system_flight_acceptance_json(
    const SystemFlightAcceptanceReport& report) -> std::string {
  return std::format(
      "{{\n"
      "  \"schema_version\": 2,\n"
      "  \"scenario\": \"v0.4.9-system-flight\",\n"
      "  \"evidence_scope\": \"application_framebuffer\",\n"
      "  \"system_id\": \"{}\",\n"
      "  \"planet_id\": \"planet-{:016x}\",\n"
      "  \"arrival_tick\": \"{}\",\n"
      "  \"insertion_tick\": \"{}\",\n"
      "  \"host_steps\": \"{}\",\n"
      "  \"system_flight_checksum\": \"{}\",\n"
      "  \"orbital_flight_checksum\": \"{}\",\n"
      "  \"framebuffer_checksum\": \"{}\"\n"
      "}}\n",
      system_id_string(report.system), report.planet.value,
      report.arrival_tick, report.insertion_tick, report.host_steps,
      report.system_flight_checksum, report.orbital_flight_checksum,
      report.framebuffer_checksum);
}

}  // namespace apsis_drift
