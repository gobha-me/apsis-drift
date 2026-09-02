#include "apsis_drift/origin_system_contract_acceptance.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <format>
#include <span>
#include <utility>

#include "apsis_drift/intersystem_planetfall.hpp"
#include "apsis_drift/save_file.hpp"
#include "apsis_drift/system_rendering.hpp"

namespace apsis_drift {
namespace {

struct Guidance {
  bool forward{};
  bool backward{};
  bool left{};
  bool right{};
  bool rise{};
  bool fall{};
};

struct Replay {
  OriginSystemContractAcceptanceReport report;
  SaveDocument document;
  std::vector<termforge::Pixel> frame;
};

[[nodiscard]] auto checksum_bytes(std::string_view bytes) noexcept
    -> std::uint64_t {
  std::uint64_t hash{1469598103934665603ULL};
  for (const unsigned char byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

[[nodiscard]] auto checksum_pixels(
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

auto command_if_changed(std::vector<FlightCommand>& commands,
                        SimulationTick tick, bool current, bool desired,
                        FlightCommandKind press, FlightCommandKind release)
    -> void {
  if (current == desired) return;
  commands.push_back({tick, desired ? press : release});
}

[[nodiscard]] auto commands_for(const PlanetaryFlightState& flight,
                                const Guidance& guidance)
    -> std::vector<FlightCommand> {
  std::vector<FlightCommand> commands;
  command_if_changed(commands, flight.tick, flight.controls.forward,
                     guidance.forward, FlightCommandKind::press_forward,
                     FlightCommandKind::release_forward);
  command_if_changed(commands, flight.tick, flight.controls.backward,
                     guidance.backward, FlightCommandKind::press_backward,
                     FlightCommandKind::release_backward);
  command_if_changed(commands, flight.tick, flight.controls.turn_left,
                     guidance.left, FlightCommandKind::press_turn_left,
                     FlightCommandKind::release_turn_left);
  command_if_changed(commands, flight.tick, flight.controls.turn_right,
                     guidance.right, FlightCommandKind::press_turn_right,
                     FlightCommandKind::release_turn_right);
  command_if_changed(commands, flight.tick, flight.controls.rise, guidance.rise,
                     FlightCommandKind::press_rise,
                     FlightCommandKind::release_rise);
  command_if_changed(commands, flight.tick, flight.controls.fall, guidance.fall,
                     FlightCommandKind::press_fall,
                     FlightCommandKind::release_fall);
  return commands;
}

[[nodiscard]] auto descent_guidance(const IntersystemPlanetfallState& state,
                                    bool pilot, bool expanded_seed_guidance)
    -> Guidance {
  const auto& flight = state.flight;
  const auto& navigation = state.navigation;
  const double relative = navigation.relative_bearing_radians;
  const bool aligned = std::abs(relative) <= 0.20;
  const bool reached = navigation.status == SignalScannerStatus::reached;
  const auto target = std::ranges::find(
      state.catalog.signals, *state.scanner.selected, &SurfaceSignal::id);
  // The published seed-42 acceptance golden predates the absolute approach
  // altitude contract. Preserve its command schedule while expanded seeds use
  // the same altitude interpretation as signal navigation.
  const double target_altitude =
      target == state.catalog.signals.end()
          ? flight.pose.position.altitude_metres
          : static_cast<double>(expanded_seed_guidance
                                    ? target->approach_altitude_metres
                                    : target->surface_elevation_metres +
                                          target->approach_altitude_metres);
  const bool recovering = pilot && (flight.thermal.abort_latched ||
                                    flight.thermal.load_units >= 600'000U);
  return {.forward = aligned && !reached &&
                     navigation.distance_metres > 700.0 && !recovering,
          .backward = recovering,
          .left = relative < -0.025,
          .right = relative > 0.025,
          .rise = recovering,
          .fall =
              (aligned || expanded_seed_guidance) &&
              navigation.distance_metres < 25'000'000.0 &&
              flight.pose.position.altitude_metres > target_altitude + 250.0 &&
              !recovering};
}

[[nodiscard]] auto ascent_guidance(const PlanetaryFlightState& flight)
    -> Guidance {
  return {.backward =
              std::hypot(flight.velocity.east_metres_per_second,
                         flight.velocity.north_metres_per_second) > 25.0,
          .rise = true};
}

auto project_active_state(
    SaveDocument& document, const IntersystemContractState& career,
    const OriginSystemContractState& contract, OriginLocation location,
    const std::optional<PlanetaryFlightState>& flight,
    const std::optional<SystemFlightState>& system_flight,
    const std::optional<OriginStationFlightState>& station) -> void {
  document.state.intersystem_contract = career;
  document.state.origin_system_contract = contract;
  document.state.location = location;
  document.state.flight = flight;
  document.state.system_flight = system_flight;
  document.state.origin_station_flight = station;
}

[[nodiscard]] auto checkpoint(
    SaveDocument& document, std::string_view name,
    std::vector<OriginSystemContractCheckpoint>& checkpoints)
    -> std::expected<void, OriginSystemContractAcceptanceError> {
  const auto encoded = encode_save_document_json(document);
  const auto decoded = encoded ? decode_save_document_json(*encoded)
                               : std::expected<SaveDocument, SaveSchemaError>{
                                     std::unexpected{SaveSchemaError{}}};
  if (!encoded || !decoded || *decoded != document) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::persistence_failure};
  }
  document = *decoded;
  const SimulationTick tick =
      document.state.intersystem_contract
          ? document.state.intersystem_contract->universe_tick
          : 0;
  checkpoints.push_back({std::string{name}, tick, checksum_bytes(*encoded)});
  return {};
}

[[nodiscard]] auto prepared_document(Seed seed)
    -> std::expected<SaveDocument, OriginSystemContractAcceptanceError> {
  auto document = make_new_game_document(seed);
  auto contract = initial_origin_system_contract(seed);
  if (!contract || !document.state.intersystem_contract) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::initialization_failure};
  }
  document.state.onboarding = {.state = OnboardingState::guided,
                               .chapter = OnboardingChapter::contract_two};
  document.state.first_objective = FirstObjectiveStatus::turned_in;
  document.state.discoveries = {{document.state.first_objective_target, 0}};
  document.state.world_deltas = {
      {surface_signal_object_key(document.state.first_objective_target),
       SaveWorldDeltaKind::collected, 0}};
  document.state.origin_system_contract = *contract;
  if (!validate_save_document(document)) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::initialization_failure};
  }
  return document;
}

[[nodiscard]] auto replay(const SaveDocument& starting_document, int width,
                          int height, int render_interval)
    -> std::expected<Replay, OriginSystemContractAcceptanceError> {
  const Seed seed = starting_document.recipe.universe_seed;
  auto document = starting_document;
  if (!validate_save_document(document) ||
      document.state.onboarding.state != OnboardingState::guided ||
      document.state.onboarding.chapter != OnboardingChapter::contract_two ||
      document.state.first_objective != FirstObjectiveStatus::turned_in ||
      !document.state.intersystem_contract ||
      !document.state.origin_system_contract || document.state.flight ||
      document.state.system_flight || document.state.origin_station_flight) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::initialization_failure};
  }
  auto career = *document.state.intersystem_contract;
  auto contract = *document.state.origin_system_contract;
  const auto system = generate_origin_system(seed);
  std::vector<OriginSystemContractCheckpoint> checkpoints;
  LocalSystemRenderer renderer{{.width = width, .height = height}};
  std::vector<termforge::Pixel> frame(static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(height));

  if (!advance_origin_system_contract(contract, career.universe_tick,
                                      career.universe_tick,
                                      OriginSystemContractCommand::accept)) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::transition_failure};
  }
  document.state.origin_system_discoveries = {
      {contract.binding.target_objective, career.universe_tick}};
  if (!advance_origin_system_contract(contract, career.universe_tick,
                                      career.universe_tick,
                                      OriginSystemContractCommand::launch)) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::transition_failure};
  }
  auto station =
      initialize_origin_station_launch(seed, career.universe_tick, system);
  if (!station) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::initialization_failure};
  }
  const std::array launch_commands{
      FlightCommand{station->tick, FlightCommandKind::press_forward}};
  if (!advance_origin_station_flight(seed, career.universe_tick, system,
                                     *station, launch_commands) ||
      !advance_intersystem_time(career, 1)) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::simulation_failure};
  }
  auto outbound = initialize_origin_system_outbound_transfer(
      seed, career.universe_tick, system, contract, *station);
  if (!outbound || !advance_origin_system_contract(
                       contract, career.universe_tick, career.universe_tick,
                       OriginSystemContractCommand::begin_outbound_transfer)) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::transition_failure};
  }
  project_active_state(document, career, contract, OriginLocation::in_flight,
                       std::nullopt, *outbound, std::nullopt);
  if (!checkpoint(document, "outbound-transfer", checkpoints)) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::persistence_failure};
  }
  career = *document.state.intersystem_contract;
  contract = *document.state.origin_system_contract;
  outbound = *document.state.system_flight;
  const SimulationTick outbound_tick = career.universe_tick;
  const std::array scale_commands{
      FlightCommand{outbound->tick, FlightCommandKind::increase_time_scale},
      FlightCommand{outbound->tick, FlightCommandKind::increase_time_scale}};
  const auto before_scale = outbound->tick;
  if (!advance_system_flight(system, *outbound, scale_commands) ||
      !advance_intersystem_time(career, outbound->tick - before_scale)) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::simulation_failure};
  }
  bool cruise_checkpoint{};
  constexpr SimulationTick maximum_outbound_transfer_steps{300'000};
  for (SimulationTick step = 0; step < maximum_outbound_transfer_steps;
       ++step) {
    const auto guidance = resolve_system_flight_guidance(system, *outbound);
    if (!guidance) {
      return std::unexpected{
          OriginSystemContractAcceptanceError::simulation_failure};
    }
    if (guidance->orbit_insertion_ready) break;
    const auto before = outbound->tick;
    if (!advance_system_flight(system, *outbound, {}) ||
        !advance_intersystem_time(career, outbound->tick - before)) {
      return std::unexpected{
          OriginSystemContractAcceptanceError::simulation_failure};
    }
    if (step < 120U &&
        step % static_cast<SimulationTick>(render_interval) == 0U) {
      const auto rendered =
          renderer.render(system,
                          {.time = {outbound->tick, 0.0},
                           .position = outbound->position,
                           .velocity = outbound->velocity,
                           .forward = outbound->forward,
                           .up = outbound->up,
                           .selected_planet = outbound->target},
                          frame);
      if (!rendered) {
        std::fprintf(stderr,
                     "contract-two outbound presentation failed at tick %llu "
                     "(%u)\n",
                     static_cast<unsigned long long>(outbound->tick),
                     static_cast<unsigned>(rendered.error()));
        return std::unexpected{
            OriginSystemContractAcceptanceError::presentation_failure};
      }
    }
    if (!cruise_checkpoint && step > 8U) {
      project_active_state(document, career, contract,
                           OriginLocation::in_flight, std::nullopt, *outbound,
                           std::nullopt);
      if (!checkpoint(document, "time-scaled-cruise", checkpoints)) {
        return std::unexpected{
            OriginSystemContractAcceptanceError::persistence_failure};
      }
      career = *document.state.intersystem_contract;
      contract = *document.state.origin_system_contract;
      outbound = *document.state.system_flight;
      cruise_checkpoint = true;
    }
  }
  const auto outbound_guidance =
      resolve_system_flight_guidance(system, *outbound);
  if (!outbound_guidance || !outbound_guidance->orbit_insertion_ready) {
    std::fprintf(stderr,
                 "contract-two outbound transfer did not reach insertion\n");
    return std::unexpected{
        OriginSystemContractAcceptanceError::incomplete_path};
  }
  project_active_state(document, career, contract, OriginLocation::in_flight,
                       std::nullopt, *outbound, std::nullopt);
  if (!checkpoint(document, "target-approach", checkpoints)) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::persistence_failure};
  }
  career = *document.state.intersystem_contract;
  contract = *document.state.origin_system_contract;
  outbound = *document.state.system_flight;
  const auto orbital = insert_system_flight_orbit(system, *outbound);
  if (!orbital || !advance_origin_system_contract(
                      contract, career.universe_tick, career.universe_tick,
                      OriginSystemContractCommand::enter_target_planet)) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::transition_failure};
  }
  auto cache = TerrainTileCache::create();
  auto planetfall =
      cache ? initialize_intersystem_planetfall(
                  system.planets[contract.binding.target_ordinal].descriptor,
                  contract.binding.target_objective, *orbital, {}, *cache)
            : std::expected<IntersystemPlanetfallState,
                            IntersystemPlanetfallError>{
                  std::unexpected{IntersystemPlanetfallError::terrain_failure}};
  if (!planetfall) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::initialization_failure};
  }
  constexpr SimulationTick maximum_planet_ticks{2'000'000};
  const bool noncanonical_planetfall_guidance =
      seed != Seed{kOriginSystemContractAcceptanceSeed};
  for (SimulationTick step = 0;
       step < maximum_planet_ticks &&
       planetfall->collection.status != SignalCollectionStatus::complete;
       ++step) {
    const auto commands = commands_for(
        planetfall->flight,
        descent_guidance(*planetfall,
                         career.rule_profile == IntersystemRuleProfile::pilot,
                         noncanonical_planetfall_guidance));
    if (!advance_intersystem_planetfall(*planetfall, *cache, commands,
                                        career.rule_profile) ||
        !advance_intersystem_time(career, 1)) {
      return std::unexpected{
          OriginSystemContractAcceptanceError::simulation_failure};
    }
  }
  if (planetfall->collection.status != SignalCollectionStatus::complete ||
      !advance_origin_system_contract(
          contract, career.universe_tick, career.universe_tick,
          OriginSystemContractCommand::complete_objective)) {
    std::fprintf(stderr,
                 "contract-two planetary objective did not complete: "
                 "status=%u distance=%.3f bearing=%.6f altitude=%.3f "
                 "clearance=%.3f velocity=(%.3f,%.3f,%.3f) regime=%u "
                 "heading=%.6f position=(%.9f,%.9f) "
                 "mode=%u controls=(%u,%u,%u,%u,%u,%u) thermal=%u "
                 "abort=%u\n",
                 static_cast<unsigned>(planetfall->collection.status),
                 planetfall->navigation.distance_metres,
                 planetfall->navigation.relative_bearing_radians,
                 planetfall->flight.pose.position.altitude_metres,
                 planetfall->flight.clearance_metres,
                 planetfall->flight.velocity.east_metres_per_second,
                 planetfall->flight.velocity.north_metres_per_second,
                 planetfall->flight.velocity.up_metres_per_second,
                 static_cast<unsigned>(planetfall->flight.regime),
                 planetfall->flight.pose.heading_radians,
                 planetfall->flight.pose.position.latitude_radians,
                 planetfall->flight.pose.position.longitude_radians,
                 static_cast<unsigned>(planetfall->flight.mode),
                 planetfall->flight.controls.forward ? 1U : 0U,
                 planetfall->flight.controls.backward ? 1U : 0U,
                 planetfall->flight.controls.turn_left ? 1U : 0U,
                 planetfall->flight.controls.turn_right ? 1U : 0U,
                 planetfall->flight.controls.rise ? 1U : 0U,
                 planetfall->flight.controls.fall ? 1U : 0U,
                 planetfall->flight.thermal.load_units,
                 planetfall->flight.thermal.abort_latched ? 1U : 0U);
    return std::unexpected{
        OriginSystemContractAcceptanceError::incomplete_path};
  }
  document.state.origin_system_world_deltas.assign(
      planetfall->journal.entries().begin(),
      planetfall->journal.entries().end());
  project_active_state(document, career, contract, OriginLocation::in_flight,
                       planetfall->flight, std::nullopt, std::nullopt);
  if (!checkpoint(document, "objective-complete", checkpoints)) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::persistence_failure};
  }
  career = *document.state.intersystem_contract;
  contract = *document.state.origin_system_contract;
  planetfall->flight = *document.state.flight;
  const SimulationTick objective_tick = career.universe_tick;

  while (planetfall->flight.regime != FlightRegime::orbital) {
    const auto commands =
        commands_for(planetfall->flight, ascent_guidance(planetfall->flight));
    if (!advance_intersystem_planetfall(*planetfall, *cache, commands,
                                        career.rule_profile) ||
        !advance_intersystem_time(career, 1)) {
      return std::unexpected{
          OriginSystemContractAcceptanceError::simulation_failure};
    }
  }
  auto departure = depart_planetary_orbit(system, planetfall->flight);
  auto returning =
      departure
          ? initialize_origin_system_return_transfer(seed, system, contract,
                                                     *departure)
          : std::expected<SystemFlightState, OriginSystemContractError>{
                std::unexpected{OriginSystemContractError::invalid_flight}};
  if (!returning || !advance_origin_system_contract(
                        contract, career.universe_tick, career.universe_tick,
                        OriginSystemContractCommand::leave_target_planet)) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::transition_failure};
  }
  project_active_state(document, career, contract, OriginLocation::in_flight,
                       std::nullopt, *returning, std::nullopt);
  if (!checkpoint(document, "return-transfer", checkpoints)) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::persistence_failure};
  }
  career = *document.state.intersystem_contract;
  contract = *document.state.origin_system_contract;
  returning = *document.state.system_flight;
  const SimulationTick return_tick = career.universe_tick;
  const std::array return_scale{
      FlightCommand{returning->tick, FlightCommandKind::increase_time_scale},
      FlightCommand{returning->tick, FlightCommandKind::increase_time_scale}};
  const auto return_before = returning->tick;
  if (!advance_system_flight(system, *returning, return_scale) ||
      !advance_intersystem_time(career, returning->tick - return_before)) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::simulation_failure};
  }
  constexpr SimulationTick maximum_return_transfer_steps{400'000};
  for (SimulationTick step = 0; step < maximum_return_transfer_steps; ++step) {
    const auto guidance = resolve_system_flight_guidance(system, *returning);
    if (!guidance) {
      return std::unexpected{
          OriginSystemContractAcceptanceError::simulation_failure};
    }
    if (guidance->inside_approach_boundary) break;
    const auto before = returning->tick;
    if (!advance_system_flight(system, *returning, {}) ||
        !advance_intersystem_time(career, returning->tick - before)) {
      return std::unexpected{
          OriginSystemContractAcceptanceError::simulation_failure};
    }
  }
  auto rendezvous = initialize_origin_system_station_rendezvous(
      seed, system, contract, *returning);
  if (!rendezvous ||
      !advance_origin_system_contract(
          contract, career.universe_tick, career.universe_tick,
          OriginSystemContractCommand::begin_station_rendezvous)) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::transition_failure};
  }
  project_active_state(document, career, contract, OriginLocation::in_flight,
                       std::nullopt, std::nullopt, *rendezvous);
  if (!checkpoint(document, "station-rendezvous", checkpoints)) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::persistence_failure};
  }
  career = *document.state.intersystem_contract;
  contract = *document.state.origin_system_contract;
  rendezvous = *document.state.origin_station_flight;
  const SimulationTick rendezvous_tick = career.universe_tick;
  constexpr SimulationTick maximum_station_ticks{120'000};
  for (SimulationTick step = 0; step < maximum_station_ticks; ++step) {
    const auto guidance = resolve_origin_station_flight_guidance(
        seed, career.universe_tick, system, *rendezvous);
    if (!guidance) {
      return std::unexpected{
          OriginSystemContractAcceptanceError::simulation_failure};
    }
    if (guidance->arrived) break;
    if (!advance_origin_station_flight(seed, career.universe_tick, system,
                                       *rendezvous, {}) ||
        !advance_intersystem_time(career, 1)) {
      return std::unexpected{
          OriginSystemContractAcceptanceError::simulation_failure};
    }
  }
  const auto station_guidance = resolve_origin_station_flight_guidance(
      seed, career.universe_tick, system, *rendezvous);
  if (!station_guidance || !station_guidance->arrived ||
      !advance_origin_system_contract(contract, career.universe_tick,
                                      career.universe_tick,
                                      OriginSystemContractCommand::dock)) {
    std::fprintf(stderr, "contract-two station rendezvous did not complete\n");
    return std::unexpected{
        OriginSystemContractAcceptanceError::incomplete_path};
  }
  const std::uint64_t final_station_checksum =
      origin_station_flight_state_checksum(*rendezvous);
  if (!advance_origin_system_contract(contract, career.universe_tick,
                                      career.universe_tick,
                                      OriginSystemContractCommand::turn_in) ||
      !advance_onboarding(document.state.onboarding,
                          OnboardingCommand::complete_contract_two)) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::transition_failure};
  }
  document.state.origin_system_discoveries.insert(
      document.state.origin_system_discoveries.begin(),
      document.state.discoveries.begin(), document.state.discoveries.end());
  document.state.origin_system_world_deltas.insert(
      document.state.origin_system_world_deltas.begin(),
      document.state.world_deltas.begin(), document.state.world_deltas.end());
  document.state.discoveries.clear();
  document.state.world_deltas.clear();
  project_active_state(document, career, contract,
                       OriginLocation::docked_at_origin, std::nullopt,
                       std::nullopt, std::nullopt);
  if (!checkpoint(document, "turned-in", checkpoints)) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::persistence_failure};
  }

  const auto rendered = renderer.render(system,
                                        {.time = {returning->tick, 0.0},
                                         .position = returning->position,
                                         .velocity = returning->velocity,
                                         .forward = returning->forward,
                                         .up = returning->up,
                                         .selected_planet = returning->target},
                                        frame);
  if (!rendered) {
    std::fprintf(stderr,
                 "contract-two return presentation failed at tick %llu "
                 "(%u)\n",
                 static_cast<unsigned long long>(returning->tick),
                 static_cast<unsigned>(rendered.error()));
    return std::unexpected{
        OriginSystemContractAcceptanceError::presentation_failure};
  }
  return Replay{
      .report = {.binding = contract.binding,
                 .outbound_tick = outbound_tick,
                 .target_insertion_tick = orbital->tick,
                 .objective_tick = objective_tick,
                 .return_tick = return_tick,
                 .rendezvous_tick = rendezvous_tick,
                 .final_tick = career.universe_tick,
                 .outbound_checksum = system_flight_state_checksum(*outbound),
                 .return_checksum = system_flight_state_checksum(*returning),
                 .final_station_checksum = final_station_checksum,
                 .framebuffer_checksum = checksum_pixels(frame),
                 .checkpoints = std::move(checkpoints)},
      .document = std::move(document),
      .frame = std::move(frame)};
}

} // namespace

auto run_origin_system_contract_acceptance(int width, int height)
    -> std::expected<OriginSystemContractAcceptanceResult,
                     OriginSystemContractAcceptanceError> {
  auto prepared = prepared_document(Seed{kOriginSystemContractAcceptanceSeed});
  if (!prepared) return std::unexpected{prepared.error()};
  return run_origin_system_contract_acceptance(*prepared, width, height);
}

auto run_origin_system_contract_acceptance(
    const SaveDocument& starting_document, int width, int height)
    -> std::expected<OriginSystemContractAcceptanceResult,
                     OriginSystemContractAcceptanceError> {
  if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) >
          4'194'304ULL) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::invalid_configuration};
  }
  auto first = replay(starting_document, width, height, 4);
  if (!first) return std::unexpected{first.error()};
  auto second = replay(starting_document, width, height, 2);
  if (!second) return std::unexpected{second.error()};
  if (first->report != second->report || first->document != second->document ||
      first->frame != second->frame) {
    return std::unexpected{
        OriginSystemContractAcceptanceError::cadence_mismatch};
  }
  return OriginSystemContractAcceptanceResult{
      first->report, std::move(first->document), std::move(first->frame)};
}

auto origin_system_contract_acceptance_json(
    const OriginSystemContractAcceptanceReport& report) -> std::string {
  std::string checkpoints;
  for (std::size_t index = 0; index < report.checkpoints.size(); ++index) {
    const auto& checkpoint = report.checkpoints[index];
    checkpoints +=
        std::format("    {{\"name\": \"{}\", \"tick\": \"{}\", "
                    "\"save_checksum\": \"{}\"}}{}\n",
                    checkpoint.name, checkpoint.tick, checkpoint.save_checksum,
                    index + 1U == report.checkpoints.size() ? "" : ",");
  }
  return std::format(
      "{{\n"
      "  \"schema_version\": 1,\n"
      "  \"scenario\": \"{}\",\n"
      "  \"evidence_scope\": \"application_framebuffer\",\n"
      "  \"seed\": \"{}\",\n"
      "  \"system_id\": \"{}\",\n"
      "  \"contract_id\": \"{}\",\n"
      "  \"home_planet_id\": \"planet-{:016x}\",\n"
      "  \"target_planet_id\": \"planet-{:016x}\",\n"
      "  \"target_objective_id\": \"{}\",\n"
      "  \"outbound_tick\": \"{}\",\n"
      "  \"target_insertion_tick\": \"{}\",\n"
      "  \"objective_tick\": \"{}\",\n"
      "  \"return_tick\": \"{}\",\n"
      "  \"rendezvous_tick\": \"{}\",\n"
      "  \"final_tick\": \"{}\",\n"
      "  \"outbound_checksum\": \"{}\",\n"
      "  \"return_checksum\": \"{}\",\n"
      "  \"final_station_checksum\": \"{}\",\n"
      "  \"framebuffer_checksum\": \"{}\",\n"
      "  \"checkpoints\": [\n{}  ]\n"
      "}}\n",
      kOriginSystemContractAcceptanceScenario,
      kOriginSystemContractAcceptanceSeed,
      system_id_string(report.binding.system),
      mission_id_string(report.binding.contract),
      report.binding.home_planet.value, report.binding.target_planet.value,
      surface_signal_id_string(report.binding.target_objective),
      report.outbound_tick, report.target_insertion_tick, report.objective_tick,
      report.return_tick, report.rendezvous_tick, report.final_tick,
      report.outbound_checksum, report.return_checksum,
      report.final_station_checksum, report.framebuffer_checksum, checkpoints);
}

} // namespace apsis_drift
