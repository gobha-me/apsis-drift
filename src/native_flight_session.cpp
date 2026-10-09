#include "apsis_drift/native_flight_session.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace apsis_drift {
namespace {
auto route_actor(const FreedomBoardingState& b,
                 const GameplayBoardingView& view) -> OriginWalkerState {
  auto actor = b.station_entry;
  actor.foot_position_metres = view.eye_station;
  actor.foot_position_metres.y -= kOriginWalkerEyeHeightMetres;
  actor.velocity_metres_per_second = {};
  actor.heading_radians = view.heading_radians;
  return actor;
}
} // namespace
NativeFreedomFlightSession::NativeFreedomFlightSession(
    FreedomFlightSaveDocument document, FreedomFlightHydration hydrated,
    std::optional<std::filesystem::path> source)
    : document_(std::move(document)), system_(std::move(hydrated.system)),
      rotation_(hydrated.rotation), source_save_(std::move(source)) {
}

auto NativeFreedomFlightSession::open(NativeStartup selected)
    -> std::expected<NativeFreedomFlightSession, std::string> {
  std::optional<NativeStartingAssemblySelection> assembly;
  std::optional<FreedomBoardingState> boarding;
  std::optional<OriginWalkerState> actor;
  std::optional<FreedomSurfaceState> surface;
  if (auto* d = std::get_if<FreedomSurfaceSaveDocument>(&selected.document)) {
    if (selected.mode != NativeStartup::Mode::freedom)
      return std::unexpected{"Surface state requires Freedom"};
    if (auto valid = validate_freedom_surface_document(*d); !valid)
      return std::unexpected{valid.error().detail};
    surface = d->surface;
    auto base = std::move(d->base);
    selected.document = std::visit(
        [](auto& value) -> NativeSaveDocument { return std::move(value); },
        base);
  }
  if (auto* d = std::get_if<FreedomBoardingSaveDocument>(&selected.document)) {
    if (selected.mode != NativeStartup::Mode::freedom)
      return std::unexpected{"Boarding requires Freedom"};
    if (auto v = validate_freedom_boarding_document(*d); !v)
      return std::unexpected{v.error().detail};
    assembly = d->starting_assembly;
    boarding = d->boarding;
    actor = d->station_actor;
    auto voyage = std::move(d->voyage);
    selected.document = std::move(voyage);
  }
  if (auto* d = std::get_if<FreedomStartingAssemblySaveDocument>(
          &selected.document)) {
    if (selected.mode != NativeStartup::Mode::freedom)
      return std::unexpected{"Starting assembly requires Freedom"};
    if (auto v = validate_freedom_starting_assembly_document(*d); !v)
      return std::unexpected{v.error().detail};
    assembly = d->starting_assembly;
    auto journey = std::move(d->journey);
    selected.document = std::move(journey);
  }
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
  if (assembly) {
    auto binding = make_native_starting_assembly_binding(*assembly);
    if (!binding) return std::unexpected{binding.error()};
    result.starting_assembly_ = assembly;
    result.craft_binding_ = std::move(*binding);
  }
  result.boarding_ = boarding;
  result.surface_ = surface;
  if (boarding && (boarding->phase == FreedomBoardingPhase::boarding ||
                   boarding->phase == FreedomBoardingPhase::disembarking)) {
    auto view = result.boarding_view();
    if (!view) return std::unexpected{view.error()};
    result.actor_ = route_actor(*boarding, *view);
  }
  return result;
}

auto NativeFreedomFlightSession::boarding_document() const
    -> FreedomBoardingSaveDocument {
  return {{document_, *docking_},
          *starting_assembly_,
          *boarding_,
          boarding_->phase == FreedomBoardingPhase::station ? actor_
                                                            : std::nullopt};
}
auto NativeFreedomFlightSession::boarding_view() const
    -> std::expected<GameplayBoardingView, std::string> {
  if (!boarding_) return std::unexpected{"No gameplay boarding route"};
  return view_origin_gameplay_boarding(craft_binding_, boarding_->route);
}
auto NativeFreedomFlightSession::begin_boarding()
    -> std::expected<void, std::string> {
  if (!actor_ ||
      (boarding_ && boarding_->phase != FreedomBoardingPhase::station))
    return std::unexpected{"Boarding requires a station actor"};
  return begin_boarding_route(GameplayBoardingDirection::board);
}
auto NativeFreedomFlightSession::begin_disembarking()
    -> std::expected<void, std::string> {
  if (actor_ || !boarding_ || boarding_->phase != FreedomBoardingPhase::seated)
    return std::unexpected{"Disembarking requires a seated pilot"};
  return begin_boarding_route(GameplayBoardingDirection::disembark);
}
auto NativeFreedomFlightSession::begin_boarding_route(
    GameplayBoardingDirection direction) -> std::expected<void, std::string> {
  if (!starting_assembly_ || !docking_ || !docking_->attached ||
      docking_->target.ordinal != 1)
    return std::unexpected{"Boarding requires the Wayfarer attached to D1"};
  const auto entry = direction == GameplayBoardingDirection::board
                         ? *actor_
                         : boarding_->station_entry;
  if (auto valid = validate_origin_walker(entry); !valid)
    return std::unexpected{valid.error()};
  const auto station =
      generate_origin_station(document_.origin.recipe.universe_seed);
  const auto geometry = origin_station_geometry(station);
  if (!geometry) return std::unexpected{"Origin geometry unavailable"};
  const auto pose = resolve_origin_port_pose(
      system_, station, *geometry, docking_->target, document_.flight.tick);
  if (!pose) return std::unexpected{"D1 pose unavailable"};
  auto eye = entry.foot_position_metres;
  eye.y += kOriginWalkerEyeHeightMetres;
  auto route = begin_origin_gameplay_boarding(craft_binding_, *pose, eye,
                                              entry.heading_radians, direction);
  if (!route) return std::unexpected{route.error()};
  auto candidate = *this;
  candidate.boarding_ =
      FreedomBoardingState{direction == GameplayBoardingDirection::board
                               ? FreedomBoardingPhase::boarding
                               : FreedomBoardingPhase::disembarking,
                           entry, document_.flight.tick, *route};
  const auto view = candidate.boarding_view();
  if (!view) return std::unexpected{view.error()};
  candidate.actor_ = route_actor(*candidate.boarding_, *view);
  if (auto valid =
          validate_freedom_boarding_document(candidate.boarding_document());
      !valid)
    return std::unexpected{valid.error().detail};
  *this = std::move(candidate);
  return {};
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
  cancel_port_approach();
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
  if (surface_ && (surface_->gear_deployed || surface_->landed))
    return std::unexpected{"Stow landing gear before station capture"};
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
  cancel_port_approach();
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
  if (surface_ && surface_->landed && request.target)
    return std::unexpected{"Lift off before requesting orbit hold"};
  if (request.target &&
      surface_maneuver_.kind != NativeSurfaceManeuverKind::off)
    return std::unexpected{
        "Cancel surface maneuver before requesting orbit hold"};
  const auto observed = observe();
  if (!observed) return std::unexpected{observed.error()};
  if (!validate_orbit_hold_request(
          RigidBodyWorldContext{system_}, document_.flight, rotation_,
          {1, observed->atmosphere.space_boundary_altitude_metres}, request,
          document_.model.central))
    return std::unexpected{
        "Hold target/version refused for the saved body and air boundary"};
  document_.model.hold = request;
  cancel_port_approach();
  return {};
}

auto NativeFreedomFlightSession::advance(const NativeFlightControls& controls,
                                         SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (actor_)
    return std::unexpected{
        "Walking owns the shared tick while outside the craft"};
  if (surface_ && (surface_->gear_deployed || surface_->landed ||
                   surface_maneuver_.kind != NativeSurfaceManeuverKind::off))
    return advance_surface(controls, step);
  if (!port_approach_.active) return advance_craft_tick(controls, step);
  // Validate the actual player input before an aid can replace neutral input.
  // Invalid input/steps must retain both the body and this transient command.
  const VacuumIntent intent{
      controls.positive_translation, controls.negative_translation,
      controls.positive_rotation, controls.negative_rotation,
      document_.model.assistance};
  if (!evaluate_atmospheric_flight(
          RigidBodyWorldContext{system_}, document_.flight, intent, rotation_,
          document_.model.atmosphere, document_.model.central))
    return std::unexpected{"Flight input or atmospheric observation refused"};
  auto candidate = *this;
  auto requested = controls;
  const bool manual = controls.positive_translation != RigidVector3{} ||
                      controls.negative_translation != RigidVector3{} ||
                      controls.positive_rotation != RigidVector3{} ||
                      controls.negative_rotation != RigidVector3{};
  if (manual) {
    candidate.cancel_port_approach();
    candidate.port_approach_.note = "Approach canceled by manual control";
  } else if (auto aid = port_approach_controls()) {
    requested = *aid;
    --candidate.port_approach_.remaining_ticks;
  } else {
    candidate.cancel_port_approach();
    candidate.port_approach_.note = "Approach stopped: " + aid.error();
  }
  auto result = candidate.advance_craft_tick(requested, step);
  if (!result) return std::unexpected{result.error()};
  if (candidate.port_approach_.active) {
    const auto assessed = candidate.assess_port();
    if (assessed && assessed->decision == OriginDockDecision::capture_ready) {
      candidate.port_approach_.note =
          "Ready; select Capture port (holding with thrusters)";
    }
    if (candidate.port_approach_.remaining_ticks == 0) {
      candidate.cancel_port_approach();
      candidate.port_approach_.note =
          "Approach timed out; resume manual control";
    }
  }
  *this = std::move(candidate);
  return result;
}

auto NativeFreedomFlightSession::advance_craft_tick(
    const NativeFlightControls& controls, SimulationSeconds step)
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
  if (surface_ && surface_->landed) {
    if (controls.positive_translation != RigidVector3{} ||
        controls.negative_translation != RigidVector3{} ||
        controls.positive_rotation != RigidVector3{} ||
        controls.negative_rotation != RigidVector3{})
      return std::unexpected{"Select Liftoff before firing propulsion"};
    const auto pose = resolve_landed_craft(
        system_, rotation_, *surface_->landed, document_.flight.tick + 1);
    if (!pose) return std::unexpected{"Landed clock advance refused"};
    auto candidate = *this;
    candidate.document_.flight = *pose;
    candidate.document_.origin.state.tick = pose->tick;
    const auto observed = candidate.observe();
    if (!observed) return std::unexpected{observed.error()};
    NativeFlightStep result{};
    result.actuation.initial = *air;
    result.actuation.after = observed->atmosphere;
    result.actuation.observation_after = observed->orbit;
    document_ = std::move(candidate.document_);
    return result;
  }
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
  if (!std::isfinite(controls.forward) || !std::isfinite(controls.right) ||
      !std::isfinite(controls.heading_radians) ||
      std::abs(controls.forward) > 1 || std::abs(controls.right) > 1 ||
      std::abs(controls.heading_radians) > 1e6)
    return std::unexpected{"Walking controls must be finite and bounded"};
  if (boarding_ && boarding_->phase == FreedomBoardingPhase::seated)
    return advance_craft_tick({}, step);
  if (!actor_ || !docking_)
    return std::unexpected{"Walking requires a station actor"};
  auto candidate = *this;
  auto flight = candidate.advance_craft_tick({}, step);
  if (!flight) return std::unexpected{flight.error()};
  if (boarding_ && boarding_->phase != FreedomBoardingPhase::station) {
    auto route = step_origin_gameplay_boarding(boarding_->route);
    if (!route) return std::unexpected{route.error()};
    candidate.boarding_->route = *route;
    auto view = candidate.boarding_view();
    if (!view) return std::unexpected{view.error()};
    if (view->complete) {
      if (route->direction == GameplayBoardingDirection::board) {
        candidate.boarding_->phase = FreedomBoardingPhase::seated;
        candidate.actor_.reset();
      } else {
        candidate.boarding_->phase = FreedomBoardingPhase::station;
        candidate.actor_ = candidate.boarding_->station_entry;
      }
    } else
      candidate.actor_ = route_actor(*candidate.boarding_, *view);
  } else {
    auto actor = advance_origin_walker(*actor_, controls, step);
    if (!actor) return std::unexpected{actor.error()};
    candidate.actor_ = *actor;
  }
  if (candidate.boarding_) {
    if (auto valid =
            validate_freedom_boarding_document(candidate.boarding_document());
        !valid)
      return std::unexpected{"Boarding candidate refused: " +
                             valid.error().detail};
  } else {
    if (auto valid = validate_freedom_journey_document(
            {{candidate.document_, *candidate.docking_}, *candidate.actor_});
        !valid)
      return std::unexpected{"Walking candidate refused: " +
                             valid.error().detail};
    if (candidate.starting_assembly_) {
      if (auto valid = validate_freedom_starting_assembly_document(
              {{{candidate.document_, *candidate.docking_}, *candidate.actor_},
               *candidate.starting_assembly_});
          !valid)
        return std::unexpected{"Starting assembly walking candidate refused: " +
                               valid.error().detail};
    }
  }
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
      surface_ ? write_freedom_surface_file_atomically(path, surface_document())
      : boarding_
          ? write_freedom_boarding_file_atomically(path, boarding_document())
      : starting_assembly_ && actor_ && docking_
          ? write_freedom_starting_assembly_file_atomically(
                path, {{{document_, *docking_}, *actor_}, *starting_assembly_})
      : actor_ ? write_freedom_journey_file_atomically(
                     path, {{document_, *docking_}, *actor_})
      : docking_
          ? write_freedom_docking_file_atomically(path, {document_, *docking_})
          : write_freedom_flight_file_atomically(path, document_);
  if (!written)
    return std::unexpected{save_file_error_message(written.error())};
  return {};
}

auto NativeFreedomFlightSession::surface_document() const
    -> FreedomSurfaceSaveDocument {
  FreedomSurfaceBaseSave base = document_;
  if (boarding_)
    base = boarding_document();
  else if (starting_assembly_ && actor_ && docking_)
    base = FreedomStartingAssemblySaveDocument{
        {{document_, *docking_}, *actor_}, *starting_assembly_};
  else if (actor_ && docking_)
    base = FreedomJourneySaveDocument{{document_, *docking_}, *actor_};
  else if (docking_)
    base = FreedomDockingSaveDocument{document_, *docking_};
  return {std::move(base), surface_.value_or(FreedomSurfaceState{})};
}
auto NativeFreedomFlightSession::set_landing_gear(bool deployed)
    -> std::expected<void, std::string> {
  if (actor_ || (docking_ && docking_->attached))
    return std::unexpected{
        "Release the station port and sit before changing gear"};
  if (surface_ && surface_->landed && !deployed)
    return std::unexpected{"Lift off before stowing landing gear"};
  if (!surface_) surface_ = FreedomSurfaceState{};
  surface_->gear_deployed = deployed;
  if (deployed) cancel_port_approach();
  if (!deployed) cancel_surface_maneuver();
  return {};
}
auto NativeFreedomFlightSession::commit_touchdown(
    std::uint64_t expected_source_checksum)
    -> std::expected<void, std::string> {
  if (actor_ || (docking_ && docking_->attached) || !surface_ ||
      !surface_->gear_deployed || surface_->landed)
    return std::unexpected{
        "Touchdown requires deployed gear and a free airborne craft"};
  const auto anchor =
      prepare_landed_craft(system_, rotation_, document_.flight,
                           surface_->gear_deployed, expected_source_checksum);
  if (!anchor)
    return std::unexpected{
        "Touchdown refused by terrain, hull or motion checks"};
  const auto pose =
      resolve_landed_craft(system_, rotation_, *anchor, document_.flight.tick);
  if (!pose) return std::unexpected{"Touchdown pose refused"};
  auto candidate = *this;
  candidate.document_.flight = *pose;
  candidate.surface_->landed = *anchor;
  candidate.document_.model.hold = {};
  candidate.cancel_port_approach();
  candidate.cancel_surface_maneuver();
  candidate.surface_maneuver_.note = "Landed on certified dry terrain";
  if (auto valid =
          validate_freedom_surface_document(candidate.surface_document());
      !valid)
    return std::unexpected{valid.error().detail};
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::release_surface()
    -> std::expected<void, std::string> {
  if (!surface_ || !surface_->landed || actor_ ||
      (docking_ && docking_->attached))
    return std::unexpected{"Liftoff requires a landed craft"};
  const auto pose = release_landed_craft(system_, rotation_, *surface_->landed,
                                         document_.flight.tick);
  if (!pose)
    return std::unexpected{"Liftoff terrain or thrust authority refused"};
  auto candidate = *this;
  candidate.document_.flight = *pose;
  candidate.surface_->landed.reset();
  candidate.surface_maneuver_ = {
      NativeSurfaceManeuverKind::liftoff, 1200,
      "Lifting with thrusters; manual input cancels"};
  if (auto controls = candidate.surface_maneuver_controls(); !controls)
    return std::unexpected{controls.error()};
  *this = std::move(candidate);
  return {};
}
} // namespace apsis_drift
