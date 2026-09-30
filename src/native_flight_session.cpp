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
  return NativeFreedomFlightSession{std::move(document), std::move(*hydrated),
                                    std::move(selected.source_save)};
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

auto NativeFreedomFlightSession::save_as(const std::filesystem::path& path)
    const -> std::expected<void, std::string> {
  const auto& bytes = path.native();
  if (bytes.empty() || bytes.size() > 4096 ||
      bytes.find('\0') != std::string::npos || !path.is_absolute())
    return std::unexpected{
        "Flight Save As requires a bounded absolute save path"};
  const auto written = write_freedom_flight_file_atomically(path, document_);
  if (!written)
    return std::unexpected{save_file_error_message(written.error())};
  return {};
}
} // namespace apsis_drift
