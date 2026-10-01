#include "apsis_drift/native_flight_session.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace apsis_drift {
NativeFreedomFlightSession::NativeFreedomFlightSession(
    FreedomFlightSaveDocument document, FreedomFlightHydration hydrated,
    std::optional<std::filesystem::path> source)
    : document_(std::move(document)), system_(std::move(hydrated.system)),
      rotation_(hydrated.rotation), source_save_(std::move(source)) {
}

auto NativeFreedomFlightSession::open(NativeStartup selected)
    -> std::expected<NativeFreedomFlightSession, std::string> {
  std::optional<OriginWalkerState> actor;
  if (selected.mode == NativeStartup::Mode::freedom &&
      std::holds_alternative<FreedomJourneySaveDocument>(selected.document)) {
    auto& journey = std::get<FreedomJourneySaveDocument>(selected.document);
    if (auto valid = validate_freedom_journey_document(journey); !valid)
      return std::unexpected{"Journey session rejected: " + valid.error().path +
                             ": " + valid.error().detail};
    actor = journey.actor;
    auto voyage = std::move(journey.voyage);
    selected.document = std::move(voyage);
  }
  std::optional<FreedomDockingState> docking;
  if (selected.mode == NativeStartup::Mode::freedom &&
      std::holds_alternative<FreedomDockingSaveDocument>(selected.document)) {
    auto& document = std::get<FreedomDockingSaveDocument>(selected.document);
    if (auto valid = validate_freedom_docking_document(document); !valid)
      return std::unexpected{"Docking session rejected: " + valid.error().path +
                             ": " + valid.error().detail};
    docking = document.docking;
    auto flight = std::move(document.flight);
    selected.document = std::move(flight);
  }
  if (selected.mode != NativeStartup::Mode::freedom ||
      !std::holds_alternative<FreedomFlightSaveDocument>(selected.document))
    return std::unexpected{
        "Flight session requires a selected Freedom flight save"};
  auto document =
      std::get<FreedomFlightSaveDocument>(std::move(selected.document));
  auto hydrated = hydrate_freedom_flight_document(document);
  if (!hydrated)
    return std::unexpected{"Flight session rejected: " + hydrated.error().path +
                           ": " + hydrated.error().detail};
  if (selected.home_planet !=
      hydrated->system.catalog.planets[kOriginHomePlanetOrdinal].descriptor)
    return std::unexpected{
        "Selected home planet differs from the saved physical owner"};
  NativeFreedomFlightSession result{std::move(document), std::move(*hydrated),
                                    std::move(selected.source_save)};
  result.docking_ = docking;
  result.actor_ = actor;
  return result;
}

auto NativeFreedomFlightSession::select_port(std::uint32_t ordinal)
    -> std::expected<void, std::string> {
  if (actor_)
    return std::unexpected{"Board and sit before controlling the craft"};
  if (docking_ && docking_->attached)
    return std::unexpected{"Release the current port before changing target"};
  const FreedomDockingState candidate{
      1, {document_.origin.state.station, ordinal}, false};
  if (auto valid = validate_freedom_docking_document({document_, candidate});
      !valid)
    return std::unexpected{"Port selection refused: " + valid.error().detail};
  docking_ = candidate;
  return {};
}

auto NativeFreedomFlightSession::assess_port() const
    -> std::expected<OriginDockAssessment, std::string> {
  if (!docking_) return std::unexpected{"Select an Origin port first"};
  const auto station =
      generate_origin_station(document_.origin.recipe.universe_seed);
  const auto geometry = origin_station_geometry(station);
  if (!geometry) return std::unexpected{"Origin geometry is unavailable"};
  const auto result = assess_origin_dock(system_, station, *geometry,
                                         docking_->target, document_.flight);
  if (!result) return std::unexpected{"Origin port assessment refused"};
  return *result;
}

auto NativeFreedomFlightSession::capture_port()
    -> std::expected<void, std::string> {
  if (actor_)
    return std::unexpected{"Board and sit before controlling the craft"};
  if (!docking_ || docking_->attached)
    return std::unexpected{"Select a free Origin port before capture"};
  const auto assessed = assess_port();
  if (!assessed) return std::unexpected{assessed.error()};
  if (assessed->decision != OriginDockDecision::capture_ready)
    return std::unexpected{
        "Capture refused: " +
        std::string{origin_dock_decision_text(assessed->decision)}};
  const auto station =
      generate_origin_station(document_.origin.recipe.universe_seed);
  const auto geometry = origin_station_geometry(station);
  if (!geometry) return std::unexpected{"Origin geometry is unavailable"};
  const auto constraint = capture_origin_port(
      system_, station, *geometry, docking_->target, document_.flight);
  if (!constraint) return std::unexpected{"Physical port capture refused"};
  const auto pose =
      release_origin_port(system_, station, *geometry, *constraint);
  if (!pose) return std::unexpected{"Constrained pose refused"};
  auto candidate = document_;
  candidate.flight = *pose;
  auto attachment = *docking_;
  attachment.attached = true;
  if (auto valid = validate_freedom_docking_document({candidate, attachment});
      !valid)
    return std::unexpected{"Captured state cannot be persisted: " +
                           valid.error().detail};
  document_ = std::move(candidate);
  docking_ = attachment;
  return {};
}

auto NativeFreedomFlightSession::release_port()
    -> std::expected<void, std::string> {
  if (actor_)
    return std::unexpected{"Board and sit before controlling the craft"};
  if (!docking_ || !docking_->attached)
    return std::unexpected{"The craft is not attached to an Origin port"};
  if (auto valid = validate_freedom_docking_document({document_, *docking_});
      !valid)
    return std::unexpected{"Release state refused: " + valid.error().detail};
  // The canonical body already is the same-tick release pose. Removing the
  // constraint never advances it, changes momentum or withdraws the craft.
  docking_->attached = false;
  return {};
}

auto NativeFreedomFlightSession::observe() const
    -> std::expected<NativeFlightObservation, std::string> {
  const RigidBodyWorldContext context{system_};
  VacuumIntent intent;
  intent.assistance = document_.model.assistance;
  const auto air = evaluate_atmospheric_flight(
      context, document_.flight, intent, rotation_, document_.model.atmosphere,
      document_.model.central);
  if (!air) return std::unexpected{"Saved atmospheric observation refused"};
  const OrbitalTelemetryRecipe policy{1, air->space_boundary_altitude_metres};
  const auto orbit = evaluate_orbital_telemetry(
      context, document_.flight, rotation_, policy, document_.model.central);
  if (!orbit) return std::unexpected{"Saved orbital observation refused"};
  return NativeFlightObservation{*air, *orbit};
}

auto NativeFreedomFlightSession::set_hold(OrbitHoldRequest request)
    -> std::expected<void, std::string> {
  if (actor_)
    return std::unexpected{"Board and sit before controlling the craft"};
  const auto observed = observe();
  if (!observed) return std::unexpected{observed.error()};
  if (!validate_orbit_hold_request(
          RigidBodyWorldContext{system_}, document_.flight, rotation_,
          {1, observed->atmosphere.space_boundary_altitude_metres}, request,
          document_.model.central))
    return std::unexpected{
        "Hold target/version refused for the saved body and air boundary"};
  document_.model.hold = request;
  return {};
}

auto NativeFreedomFlightSession::advance(const NativeFlightControls& controls,
                                         SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (actor_)
    return std::unexpected{
        "Walking owns the shared tick while outside the craft"};
  if (!std::isfinite(step.count()) || step != kSimulationStep)
    return std::unexpected{"Flight step requires one fixed 120 Hz tick"};
  if (document_.flight.tick >= std::numeric_limits<SimulationTick>::max() - 2)
    return std::unexpected{
        "Flight clock cannot retain an advanceable saved candidate"};
  const RigidBodyWorldContext context{system_};
  VacuumIntent intent{controls.positive_translation,
                      controls.negative_translation, controls.positive_rotation,
                      controls.negative_rotation, document_.model.assistance};
  const auto air = evaluate_atmospheric_flight(
      context, document_.flight, intent, rotation_, document_.model.atmosphere,
      document_.model.central);
  if (!air)
    return std::unexpected{"Flight input or atmospheric observation refused"};
  if (docking_ && docking_->attached) {
    if (controls.positive_translation != RigidVector3{} ||
        controls.negative_translation != RigidVector3{} ||
        controls.positive_rotation != RigidVector3{} ||
        controls.negative_rotation != RigidVector3{})
      return std::unexpected{"Release the port before firing propulsion"};
    const auto station =
        generate_origin_station(document_.origin.recipe.universe_seed);
    const auto geometry = origin_station_geometry(station);
    if (!geometry) return std::unexpected{"Origin geometry is unavailable"};
    const OriginDockConstraint constraint{
        docking_->geometry_version, docking_->target, document_.flight.craft,
        document_.flight.tick + 1};
    const auto pose =
        release_origin_port(system_, station, *geometry, constraint);
    if (!pose) return std::unexpected{"Constrained clock advance refused"};
    auto candidate = *this;
    candidate.document_.flight = *pose;
    candidate.document_.origin.state.tick = pose->tick;
    if (auto valid =
            validate_freedom_docking_document({candidate.document_, *docking_});
        !valid)
      return std::unexpected{"Constrained candidate refused: " +
                             valid.error().detail};
    auto observed = candidate.observe();
    if (!observed) return std::unexpected{observed.error()};
    NativeFlightStep result{};
    result.actuation.initial = *air;
    result.actuation.after = observed->atmosphere;
    result.actuation.observation_after = observed->orbit;
    document_ = std::move(candidate.document_);
    return result;
  }
  const auto correction = evaluate_orbit_hold_correction(
      context, document_.flight, intent, rotation_,
      {1, air->space_boundary_altitude_metres}, document_.model.hold,
      document_.model.central);
  if (!correction) return std::unexpected{"Flight hold correction refused"};
  auto candidate = document_;
  const auto actuation = advance_atmospheric_flight(
      context, candidate.flight, correction->commands, rotation_,
      candidate.model.atmosphere, candidate.model.central, step);
  if (!actuation)
    return std::unexpected{
        "Flight integration or post-step observation refused"};
  candidate.origin.state.tick = candidate.flight.tick;
  if (const auto valid = hydrate_freedom_flight_document(candidate); !valid)
    return std::unexpected{"Flight candidate cannot be persisted: " +
                           valid.error().path + ": " + valid.error().detail};
  NativeFlightStep result{*correction, *actuation, {}};
  if (correction->status == OrbitHoldStatus::active ||
      correction->status == OrbitHoldStatus::saturated)
    result.applied_hold_force_body_newtons =
        actuation->central.propulsion.applied_force_newtons;
  document_ = std::move(candidate);
  return result;
}

auto NativeFreedomFlightSession::advance_walk(
    const OriginWalkControls& controls, SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (!actor_ || !docking_)
    return std::unexpected{"Walking requires a supported station actor"};
  auto actor = advance_origin_walker(*actor_, controls, step);
  if (!actor) return std::unexpected{actor.error()};
  auto candidate = *this;
  candidate.actor_.reset();
  auto flight = candidate.advance({}, step);
  if (!flight) return std::unexpected{flight.error()};
  candidate.actor_ = *actor;
  if (auto valid = validate_freedom_journey_document(
          {{candidate.document_, *candidate.docking_}, *actor});
      !valid)
    return std::unexpected{"Walking candidate refused: " +
                           valid.error().detail};
  *this = std::move(candidate);
  return *flight;
}

auto NativeFreedomFlightSession::save_as(const std::filesystem::path& path)
    const -> std::expected<void, std::string> {
  const auto& bytes = path.native();
  if (bytes.empty() || bytes.size() > 4096 ||
      bytes.find('\0') != std::string::npos || !path.is_absolute())
    return std::unexpected{
        "Flight Save As requires a bounded absolute save path"};
  const auto written =
      actor_ ? write_freedom_journey_file_atomically(
                   path, {{document_, *docking_}, *actor_})
      : docking_
          ? write_freedom_docking_file_atomically(path, {document_, *docking_})
          : write_freedom_flight_file_atomically(path, document_);
  if (!written)
    return std::unexpected{save_file_error_message(written.error())};
  return {};
}
} // namespace apsis_drift
