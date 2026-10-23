#include "apsis_drift/godot/flight_guidance.hpp"
#include "apsis_drift/godot/moon_stream_runtime.hpp"
#include "apsis_drift/godot/saved_flight.hpp"
#include "apsis_drift/godot/saved_minor_flight.hpp"
#include "apsis_drift/godot/snapshot.hpp"
#include "apsis_drift/godot/streaming.hpp"
#include "apsis_drift/godot/surface_start.hpp"
#include "apsis_drift/godot/thrust_flight.hpp"

#include "apsis_drift/body_ephemeris.hpp"
#include "apsis_drift/craft_environment_assessment.hpp"
#include "apsis_drift/freedom_knowledge.hpp"
#include "apsis_drift/native_profile.hpp"
#include "apsis_drift/native_startup.hpp"
#include "apsis_drift/operating_motion.hpp"
#include "apsis_drift/planet_surface_appearance.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "apsis_drift/secondary_observations.hpp"
#include "apsis_drift/station_geometry.hpp"

#include <charconv>
#include <filesystem>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <set>

namespace apsis_drift::godot_spike {

// This adapter owns a small experimental flight session, not a second physics
// implementation. All authoritative changes go through existing C++ APIs.
struct LiveWorld {
  const SnapshotWorld world_recipe;
  const nlohmann::json world_context;
  PlanetDescriptor planet;
  TerrainTileCache cache;
  LocalTangentFrame frame;
  PlanetaryFlightState flight;
  FixedStepClock clock;
  std::uint8_t lod{};
  unsigned previous_buttons{};
  double dropped_seconds{};
  double span_metres{};
  unsigned relief_version{};
  std::optional<flight_lab::State> lab;
  const Request reference_start;
  std::uint64_t reference_flight_checksum{};
  std::optional<SurfacePracticeStart> surface_start;

  LiveWorld(const Request& request, SnapshotWorld resolved)
      : world_recipe{std::move(resolved)},
        world_context(snapshot_world_context(world_recipe)),
        planet{world_recipe.planet}, cache{require(TerrainTileCache::create())},
        lod{request.lod}, span_metres{request.span_metres},
        relief_version{request.relief_version}, reference_start{[&] {
          auto reference = request;
          reference.planet_seed = planet.seed;
          return reference;
        }()} {
    validate(request);
    auto origin = GeodeticPosition{request.latitude, request.longitude, 0};
    const auto probe = require(planet_fixed_from_geodetic(planet, origin));
    const auto sample =
        with_relief(require(sample_planet_surface(planet, probe, lod, cache)),
                    planet, probe, relief_version);
    origin.altitude_metres = sample.elevation_metres;
    frame = require(make_local_tangent_frame(planet, origin));
    origin.altitude_metres += 60.0;
    flight = require(initial_planetary_flight_state(
        planet, origin, {sample.elevation_metres}, 0.3, FlightMode::manual));
    reference_flight_checksum = planetary_flight_state_checksum(flight);
  }

  auto advance(double elapsed, unsigned buttons,
               std::optional<PlanetaryAnalogInput> analog = std::nullopt)
      -> void {
    if (lab)
      throw std::invalid_argument("use thrust input for flight lab session");
    if (!std::isfinite(elapsed) || elapsed < 0 || elapsed > 60 || buttons > 255)
      throw std::invalid_argument("invalid elapsed time or control bits");
    // Clock rejects invalid time before mutation. Catch-up uses existing
    // policy.
    const auto scheduled = require(clock.advance(SimulationSeconds{elapsed}));
    dropped_seconds += scheduled.dropped.count();
    constexpr std::array press{FlightCommandKind::press_forward,
                               FlightCommandKind::press_backward,
                               FlightCommandKind::press_turn_left,
                               FlightCommandKind::press_turn_right,
                               FlightCommandKind::press_strafe_left,
                               FlightCommandKind::press_strafe_right,
                               FlightCommandKind::press_rise,
                               FlightCommandKind::press_fall};
    constexpr std::array release{FlightCommandKind::release_forward,
                                 FlightCommandKind::release_backward,
                                 FlightCommandKind::release_turn_left,
                                 FlightCommandKind::release_turn_right,
                                 FlightCommandKind::release_strafe_left,
                                 FlightCommandKind::release_strafe_right,
                                 FlightCommandKind::release_rise,
                                 FlightCommandKind::release_fall};
    for (int step = 0; step < scheduled.steps; ++step) {
      std::array<FlightCommand, 8> commands{};
      std::size_t count{};
      for (unsigned bit = 0; bit < 8; ++bit) {
        const unsigned mask = 1U << bit;
        if ((buttons & mask) != (previous_buttons & mask))
          commands[count++] = {flight.tick,
                               buttons & mask ? press[bit] : release[bit]};
      }
      const auto probe =
          require(planet_fixed_from_geodetic(planet, flight.pose.position));
      const auto sample =
          with_relief(require(sample_planet_surface(planet, probe, lod, cache)),
                      planet, probe, relief_version);
      if (!advance_planetary_flight(
              planet, {sample.elevation_metres}, flight,
              std::span{commands.data(), analog ? 0 : count}, kSimulationStep,
              {}, analog))
        throw std::runtime_error("authoritative C++ flight rejected a step");
      previous_buttons = buttons;
    }
  }

  auto advance_thrust(double elapsed, flight_lab::Demand demand) -> void {
    if (!lab || !std::isfinite(elapsed) || elapsed < 0 || elapsed > 60)
      throw std::invalid_argument("invalid flight lab session/time");
    flight_lab::validate(demand);
    auto candidate = *lab;
    auto candidate_clock = clock;
    const auto scheduled =
        require(candidate_clock.advance(SimulationSeconds{elapsed}));
    for (int i = 0; i < scheduled.steps; ++i) {
      const PlanetFixedPositionMetres fixed{
          candidate.position.x, candidate.position.y, candidate.position.z};
      const auto sample =
          with_relief(require(sample_planet_surface(planet, fixed, lod, cache)),
                      planet, fixed, relief_version);
      flight_lab::advance(planet, sample.elevation_metres, candidate, demand);
    }
    const auto pose = require(geodetic_from_planet_fixed(
        planet,
        {candidate.position.x, candidate.position.y, candidate.position.z}));
    const auto tangent = require(make_local_tangent_frame(planet, pose));
    const auto forward = candidate.back * (-1);
    const auto east = flight_lab::vec(tangent.east),
               north = flight_lab::vec(tangent.north),
               up = flight_lab::vec(tangent.up);
    auto projected = flight;
    projected.pose.position = pose;
    if (std::hypot(flight_lab::dot(forward, east),
                   flight_lab::dot(forward, north)) > 1e-8)
      projected.pose.heading_radians = std::atan2(
          flight_lab::dot(forward, north), flight_lab::dot(forward, east));
    projected.velocity = {flight_lab::dot(candidate.velocity, east),
                          flight_lab::dot(candidate.velocity, north),
                          flight_lab::dot(candidate.velocity, up)};
    projected.tick = candidate.tick;
    projected.clearance_metres = candidate.clearance;
    // Legacy-shaped view is ONLY for terrain/camera adapters, never legacy
    // saves.
    flight = projected;
    lab = candidate;
    clock = candidate_clock;
    dropped_seconds += scheduled.dropped.count();
    previous_buttons =
        (demand.main > 0 ? 1 : 0) | (demand.retro > 0 ? 2 : 0) |
        (demand.yaw < 0 ? 4 : 0) | (demand.yaw > 0 ? 8 : 0) |
        (demand.strafe < 0 ? 16 : 0) | (demand.strafe > 0 ? 32 : 0) |
        (demand.heave > 0 ? 64 : 0) | (demand.heave < 0 ? 128 : 0);
  }
};

class FreedomBridge : public godot::RefCounted {
  GDCLASS(FreedomBridge, godot::RefCounted)
  std::unique_ptr<LiveWorld> world;
  std::optional<OperatingMotionRecipe> operating_motion_recipe;
  std::unique_ptr<NativeFreedomStationStart> native_start;
  std::unique_ptr<SavedFlightWorld> saved_flight;
  NativeProfileSession profile_session;
  std::optional<NativeProfileCatalog> profile_catalog;
  godot::String last_error;
  std::unique_ptr<PlanetStream> stream;
  std::unique_ptr<MoonStreamRuntime> moon_stream;
  std::set<MoonStreamKey> moon_exported;
  std::set<StreamKey> exported;
  std::uint64_t stream_generation{};
  auto replace_stream(std::unique_ptr<PlanetStream>&& replacement) -> void {
    if (stream_generation == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("Terrain stream generations exhausted");
    stream = std::move(replacement);
    moon_stream.reset();
    moon_exported.clear();
    exported.clear();
    ++stream_generation;
  }
  auto replace_moon_stream(std::unique_ptr<MoonStreamRuntime>&& replacement)
      -> void {
    if (stream_generation == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("Terrain stream generations exhausted");
    moon_stream = std::move(replacement);
    stream.reset();
    exported.clear();
    moon_exported.clear();
    ++stream_generation;
  }
  auto replace_saved_stream(const NativeFreedomFlightSession& session) -> void {
    if (session.minor()) {
      replace_stream(nullptr);
    } else if (session.moon()) {
      auto replacement = std::make_unique<MoonStreamRuntime>(*session.moon());
      replacement->request(project_saved_moon_flight(session).tangent.origin);
      replace_moon_stream(std::move(replacement));
    } else
      replace_stream(saved_stream(session));
  }
  static auto saved_stream(const NativeFreedomFlightSession& session)
      -> std::unique_ptr<PlanetStream> {
    const auto relative = require(session.relative_state());
    if (!relative.frame.planet)
      throw std::invalid_argument("Saved terrain requires a selected planet");
    return std::make_unique<PlanetStream>(
        session.system(), body_id(*relative.frame.planet),
        session.document().model.terrain_policy);
  }
  struct PendingFreedomStart {
    std::string id;
    NativeProfileSession profile_session;
    std::unique_ptr<SavedFlightWorld> flight;
    std::unique_ptr<NativeFreedomStationStart> station;
    std::unique_ptr<PlanetStream> stream;
    std::unique_ptr<MoonStreamRuntime> moon_stream;
    // Compare the complete selected owner, including consequence/suit wrappers.
    std::optional<NativeSaveDocument> recovery_source;
  };
  std::unique_ptr<PendingFreedomStart> pending;
  std::uint64_t next_candidate_id{1};

  auto commit_freedom_start(NativeStartup selected) -> bool {
    NativeProfileSession source_metadata;
    if (selected.source_save) {
      auto explicit_source =
          NativeProfileSession::from_explicit_path(selected.document);
      if (!explicit_source)
        throw std::runtime_error(explicit_source.error().detail);
      source_metadata = std::move(*explicit_source);
    }
    if (std::holds_alternative<FreedomStartingAssemblySaveDocument>(
            selected.document) ||
        std::holds_alternative<FreedomBoardingSaveDocument>(
            selected.document) ||
        std::holds_alternative<FreedomSurfaceSaveDocument>(selected.document) ||
        std::holds_alternative<FreedomResourceSaveDocument>(
            selected.document) ||
        std::holds_alternative<FreedomKnowledgeSaveDocument>(
            selected.document) ||
        std::holds_alternative<FreedomTravelSaveDocument>(selected.document) ||
        std::holds_alternative<FreedomRecoverySaveDocument>(
            selected.document) ||
        std::holds_alternative<FreedomSurfaceWalkSaveDocument>(
            selected.document) ||
        std::holds_alternative<FreedomConditionSaveDocument>(
            selected.document) ||
        std::holds_alternative<FreedomAssistanceSaveDocument>(
            selected.document))
      throw std::invalid_argument(
          "Selected starting assembly requires staged model readiness");
    if (std::holds_alternative<FreedomFlightSaveDocument>(selected.document) ||
        std::holds_alternative<FreedomDockingSaveDocument>(selected.document) ||
        std::holds_alternative<FreedomJourneySaveDocument>(selected.document)) {
      auto opened = NativeFreedomFlightSession::open(std::move(selected));
      if (!opened) throw std::runtime_error(opened.error());
      validate_saved_projection(*opened);
      auto candidate = std::make_unique<SavedFlightWorld>(
          SavedFlightWorld{std::move(*opened), {}, {}, 0});
      replace_saved_stream(candidate->session);
      world.reset();
      native_start.reset();
      saved_flight = std::move(candidate);
      profile_session = std::move(source_metadata);
      pending.reset();
      last_error = godot::String{};
      return true;
    }
    auto prepared = prepare_native_freedom_station_start(std::move(selected));
    if (!prepared) throw std::runtime_error(prepared.error());
    auto candidate =
        std::make_unique<NativeFreedomStationStart>(std::move(*prepared));
    replace_stream(nullptr);
    world.reset();
    saved_flight.reset();
    native_start = std::move(candidate);
    profile_session = std::move(source_metadata);
    pending.reset();
    last_error = godot::String{};
    return true;
  }

 protected:
  static auto _bind_methods() -> void {
    godot::ClassDB::bind_method(godot::D_METHOD("initialize", "snapshot_json"),
                                &FreedomBridge::initialize);
    godot::ClassDB::bind_method(
        godot::D_METHOD("initialize_freedom_new_game", "universe_seed"),
        &FreedomBridge::initialize_freedom_new_game);
    godot::ClassDB::bind_method(
        godot::D_METHOD("initialize_freedom_continue", "save_path"),
        &FreedomBridge::initialize_freedom_continue);
    godot::ClassDB::bind_method(
        godot::D_METHOD("stage_freedom_new_game", "universe_seed"),
        &FreedomBridge::stage_freedom_new_game);
    godot::ClassDB::bind_method(
        godot::D_METHOD("stage_freedom_network_new_game", "universe_seed"),
        &FreedomBridge::stage_freedom_network_new_game);
    godot::ClassDB::bind_method(
        godot::D_METHOD("stage_freedom_uncharted_network_new_game",
                        "universe_seed"),
        &FreedomBridge::stage_freedom_uncharted_network_new_game);
    godot::ClassDB::bind_method(
        godot::D_METHOD("stage_freedom_planetary_network_new_game",
                        "universe_seed"),
        &FreedomBridge::stage_freedom_planetary_network_new_game);
    godot::ClassDB::bind_method(
        godot::D_METHOD("stage_freedom_jump_recovery_new_game", "universe_seed",
                        "chart_kind"),
        &FreedomBridge::stage_freedom_jump_recovery_new_game);
    godot::ClassDB::bind_method(
        godot::D_METHOD("stage_freedom_jump_strain_new_game", "universe_seed",
                        "chart_kind"),
        &FreedomBridge::stage_freedom_jump_strain_new_game);
    godot::ClassDB::bind_method(
        godot::D_METHOD("stage_freedom_origin_assistance_new_game",
                        "universe_seed", "chart_kind", "drive_kind"),
        &FreedomBridge::stage_freedom_origin_assistance_new_game);
    godot::ClassDB::bind_method(
        godot::D_METHOD("request_freedom_origin_assistance"),
        &FreedomBridge::request_freedom_origin_assistance);
    godot::ClassDB::bind_method(
        godot::D_METHOD("cancel_freedom_origin_assistance"),
        &FreedomBridge::cancel_freedom_origin_assistance);
    godot::ClassDB::bind_method(
        godot::D_METHOD("stage_freedom_continue", "save_path"),
        &FreedomBridge::stage_freedom_continue);
    godot::ClassDB::bind_method(godot::D_METHOD("stage_freedom_recovery"),
                                &FreedomBridge::stage_freedom_recovery);
    godot::ClassDB::bind_method(godot::D_METHOD("get_freedom_recovery_state"),
                                &FreedomBridge::get_freedom_recovery_state);
    godot::ClassDB::bind_method(godot::D_METHOD("list_freedom_profiles"),
                                &FreedomBridge::list_freedom_profiles);
    godot::ClassDB::bind_method(
        godot::D_METHOD("stage_freedom_profile", "index"),
        &FreedomBridge::stage_freedom_profile);
    godot::ClassDB::bind_method(godot::D_METHOD("get_freedom_profile_state"),
                                &FreedomBridge::get_freedom_profile_state);
    godot::ClassDB::bind_method(
        godot::D_METHOD("save_freedom_profile", "save_as"),
        &FreedomBridge::save_freedom_profile);
    godot::ClassDB::bind_method(godot::D_METHOD("get_pending_freedom_start"),
                                &FreedomBridge::get_pending_freedom_start);
    godot::ClassDB::bind_method(
        godot::D_METHOD("commit_pending_freedom_start", "candidate_id"),
        &FreedomBridge::commit_pending_freedom_start);
    godot::ClassDB::bind_method(
        godot::D_METHOD("discard_pending_freedom_start", "candidate_id"),
        &FreedomBridge::discard_pending_freedom_start);
    godot::ClassDB::bind_method(godot::D_METHOD("get_freedom_craft_binding"),
                                &FreedomBridge::get_freedom_craft_binding);
    godot::ClassDB::bind_method(godot::D_METHOD("get_freedom_start"),
                                &FreedomBridge::get_freedom_start);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_freedom_environment_assessment", "operation"),
        &FreedomBridge::get_freedom_environment_assessment);
    godot::ClassDB::bind_method(godot::D_METHOD("get_freedom_walk_state"),
                                &FreedomBridge::get_freedom_walk_state);
    godot::ClassDB::bind_method(godot::D_METHOD("get_freedom_station_crew"),
                                &FreedomBridge::get_freedom_station_crew);
    godot::ClassDB::bind_method(
        godot::D_METHOD("advance_freedom_walk", "elapsed", "controls"),
        &FreedomBridge::advance_freedom_walk);
    godot::ClassDB::bind_method(godot::D_METHOD("begin_freedom_boarding"),
                                &FreedomBridge::begin_freedom_boarding);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_freedom_boarding_availability"),
        &FreedomBridge::get_freedom_boarding_availability);
    godot::ClassDB::bind_method(godot::D_METHOD("begin_freedom_disembarking"),
                                &FreedomBridge::begin_freedom_disembarking);
    godot::ClassDB::bind_method(godot::D_METHOD("get_freedom_boarding_state"),
                                &FreedomBridge::get_freedom_boarding_state);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_freedom_origin_assistance_state"),
        &FreedomBridge::get_freedom_origin_assistance_state);
    godot::ClassDB::bind_method(godot::D_METHOD("get_freedom_flight_state"),
                                &FreedomBridge::get_freedom_flight_state);
    godot::ClassDB::bind_method(godot::D_METHOD("get_freedom_observations"),
                                &FreedomBridge::get_freedom_observations);
    godot::ClassDB::bind_method(
        godot::D_METHOD("select_freedom_jump", "system_id"),
        &FreedomBridge::select_freedom_jump);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_freedom_planet_destinations"),
        &FreedomBridge::get_freedom_planet_destinations);
    godot::ClassDB::bind_method(
        godot::D_METHOD("select_freedom_planet_jump", "system_id", "planet_id"),
        &FreedomBridge::select_freedom_planet_jump);
    godot::ClassDB::bind_method(godot::D_METHOD("select_freedom_minor_jump"),
                                &FreedomBridge::select_freedom_minor_jump);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_freedom_minor_geometry", "candidate_id"),
        &FreedomBridge::get_freedom_minor_geometry);
    godot::ClassDB::bind_method(godot::D_METHOD("select_freedom_moon_jump"),
                                &FreedomBridge::select_freedom_moon_jump);
    godot::ClassDB::bind_method(
        godot::D_METHOD("select_freedom_home_body_jump"),
        &FreedomBridge::select_freedom_home_body_jump);
    godot::ClassDB::bind_method(godot::D_METHOD("request_freedom_moon_stream"),
                                &FreedomBridge::request_freedom_moon_stream);
    godot::ClassDB::bind_method(godot::D_METHOD("poll_freedom_moon_stream"),
                                &FreedomBridge::poll_freedom_moon_stream);
    godot::ClassDB::bind_method(
        godot::D_METHOD("freedom_moon_stream_transforms", "anchors"),
        &FreedomBridge::freedom_moon_stream_transforms);
    godot::ClassDB::bind_method(godot::D_METHOD("begin_freedom_jump"),
                                &FreedomBridge::begin_freedom_jump);
    godot::ClassDB::bind_method(godot::D_METHOD("cancel_freedom_jump"),
                                &FreedomBridge::cancel_freedom_jump);
    godot::ClassDB::bind_method(
        godot::D_METHOD("select_freedom_port", "ordinal"),
        &FreedomBridge::select_freedom_port);
    godot::ClassDB::bind_method(godot::D_METHOD("capture_freedom_port"),
                                &FreedomBridge::capture_freedom_port);
    godot::ClassDB::bind_method(godot::D_METHOD("request_freedom_landing"),
                                &FreedomBridge::request_freedom_landing);
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_freedom_landing_gear", "deployed"),
        &FreedomBridge::set_freedom_landing_gear);
    godot::ClassDB::bind_method(godot::D_METHOD("stow_freedom_landing_gear"),
                                &FreedomBridge::stow_freedom_landing_gear);
    godot::ClassDB::bind_method(godot::D_METHOD("liftoff_freedom_surface"),
                                &FreedomBridge::liftoff_freedom_surface);
    godot::ClassDB::bind_method(godot::D_METHOD("begin_freedom_surface_walk"),
                                &FreedomBridge::begin_freedom_surface_walk);
    godot::ClassDB::bind_method(
        godot::D_METHOD("return_from_freedom_surface_walk"),
        &FreedomBridge::return_from_freedom_surface_walk);
    godot::ClassDB::bind_method(godot::D_METHOD("begin_freedom_orbital_survey"),
                                &FreedomBridge::begin_freedom_orbital_survey);
    godot::ClassDB::bind_method(
        godot::D_METHOD("cancel_freedom_orbital_survey"),
        &FreedomBridge::cancel_freedom_orbital_survey);
    godot::ClassDB::bind_method(godot::D_METHOD("begin_freedom_star_search"),
                                &FreedomBridge::begin_freedom_star_search);
    godot::ClassDB::bind_method(godot::D_METHOD("cancel_freedom_star_search"),
                                &FreedomBridge::cancel_freedom_star_search);
    godot::ClassDB::bind_method(godot::D_METHOD("scan_freedom_derelict"),
                                &FreedomBridge::scan_freedom_derelict);
    godot::ClassDB::bind_method(godot::D_METHOD("inspect_freedom_derelict"),
                                &FreedomBridge::inspect_freedom_derelict);
    godot::ClassDB::bind_method(godot::D_METHOD("mark_freedom_surface_marker"),
                                &FreedomBridge::mark_freedom_surface_marker);
    godot::ClassDB::bind_method(
        godot::D_METHOD("replace_freedom_surface_marker"),
        &FreedomBridge::replace_freedom_surface_marker);
    godot::ClassDB::bind_method(godot::D_METHOD("clear_freedom_surface_marker"),
                                &FreedomBridge::clear_freedom_surface_marker);
    godot::ClassDB::bind_method(
        godot::D_METHOD("enable_freedom_surface_landmarks"),
        &FreedomBridge::enable_freedom_surface_landmarks);
    godot::ClassDB::bind_method(
        godot::D_METHOD("inspect_freedom_surface_landmark", "id"),
        &FreedomBridge::inspect_freedom_surface_landmark);
    godot::ClassDB::bind_method(godot::D_METHOD("get_freedom_field_samples"),
                                &FreedomBridge::get_freedom_field_samples);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_freedom_field_sample_availability", "site_id"),
        &FreedomBridge::get_freedom_field_sample_availability);
    godot::ClassDB::bind_method(
        godot::D_METHOD("collect_freedom_field_sample", "site_id"),
        &FreedomBridge::collect_freedom_field_sample);
    godot::ClassDB::bind_method(
        godot::D_METHOD("mark_freedom_field_sample_site", "system_id",
                        "body_kind", "body_id", "site_id", "replace"),
        &FreedomBridge::mark_freedom_field_sample_site);
    godot::ClassDB::bind_method(godot::D_METHOD("discard_freedom_field_sample",
                                                "system_id", "body_kind",
                                                "body_id", "site_id"),
                                &FreedomBridge::discard_freedom_field_sample);
    godot::ClassDB::bind_method(godot::D_METHOD("advance_freedom_surface_walk",
                                                "elapsed", "controls",
                                                "paused"),
                                &FreedomBridge::advance_freedom_surface_walk);
    godot::ClassDB::bind_method(
        godot::D_METHOD("cancel_freedom_surface_maneuver"),
        &FreedomBridge::cancel_freedom_surface_maneuver);
    godot::ClassDB::bind_method(godot::D_METHOD("begin_freedom_port_docking"),
                                &FreedomBridge::begin_freedom_port_docking);
    godot::ClassDB::bind_method(godot::D_METHOD("cancel_freedom_port_docking"),
                                &FreedomBridge::cancel_freedom_port_docking);
    godot::ClassDB::bind_method(godot::D_METHOD("begin_freedom_port_approach"),
                                &FreedomBridge::begin_freedom_port_approach);
    godot::ClassDB::bind_method(godot::D_METHOD("cancel_freedom_port_approach"),
                                &FreedomBridge::cancel_freedom_port_approach);
    godot::ClassDB::bind_method(
        godot::D_METHOD("begin_freedom_port_rendezvous"),
        &FreedomBridge::begin_freedom_port_rendezvous);
    godot::ClassDB::bind_method(
        godot::D_METHOD("cancel_freedom_port_rendezvous"),
        &FreedomBridge::cancel_freedom_port_rendezvous);
    godot::ClassDB::bind_method(godot::D_METHOD("replenish_freedom_resources"),
                                &FreedomBridge::replenish_freedom_resources);
    godot::ClassDB::bind_method(godot::D_METHOD("release_freedom_port"),
                                &FreedomBridge::release_freedom_port);
    godot::ClassDB::bind_method(godot::D_METHOD("advance_freedom_flight",
                                                "elapsed", "fractions",
                                                "paused"),
                                &FreedomBridge::advance_freedom_flight);
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_freedom_assistance", "enabled"),
        &FreedomBridge::set_freedom_assistance);
    godot::ClassDB::bind_method(
        godot::D_METHOD("enable_freedom_impact_consequences"),
        &FreedomBridge::enable_freedom_impact_consequences);
    godot::ClassDB::bind_method(godot::D_METHOD("set_freedom_hold", "enabled"),
                                &FreedomBridge::set_freedom_hold);
    godot::ClassDB::bind_method(godot::D_METHOD("get_freedom_station_geometry"),
                                &FreedomBridge::get_freedom_station_geometry);
    godot::ClassDB::bind_method(godot::D_METHOD("get_wayfarer_frame"),
                                &FreedomBridge::get_wayfarer_frame);
    godot::ClassDB::bind_method(
        godot::D_METHOD("initialize_operating_motion", "recipe_json"),
        &FreedomBridge::initialize_operating_motion);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_operating_motion_pose", "roof_transfer",
                        "inner_door", "seat_boarding", "station_closure"),
        &FreedomBridge::get_operating_motion_pose);
    godot::ClassDB::bind_method(godot::D_METHOD("save_freedom_as", "save_path"),
                                &FreedomBridge::save_freedom_as);
    godot::ClassDB::bind_method(godot::D_METHOD("get_world_lighting"),
                                &FreedomBridge::get_world_lighting);
    godot::ClassDB::bind_method(
        godot::D_METHOD("advance", "elapsed", "buttons"),
        &FreedomBridge::advance);
    godot::ClassDB::bind_method(
        godot::D_METHOD("advance_analog", "elapsed", "axes"),
        &FreedomBridge::advance_analog);
    godot::ClassDB::bind_method(godot::D_METHOD("enable_thrust_flight"),
                                &FreedomBridge::enable_thrust_flight);
    godot::ClassDB::bind_method(godot::D_METHOD("enable_surface_practice"),
                                &FreedomBridge::enable_surface_practice);
    godot::ClassDB::bind_method(godot::D_METHOD("enable_orbit_practice"),
                                &FreedomBridge::enable_orbit_practice);
    godot::ClassDB::bind_method(godot::D_METHOD("get_sky_catalog"),
                                &FreedomBridge::get_sky_catalog);
    godot::ClassDB::bind_method(
        godot::D_METHOD("get_freedom_background_sky", "candidate_id"),
        &FreedomBridge::get_freedom_background_sky);
    godot::ClassDB::bind_method(godot::D_METHOD("start_practice", "reentry"),
                                &FreedomBridge::start_practice);
    godot::ClassDB::bind_method(godot::D_METHOD("camera_clearance", "offset"),
                                &FreedomBridge::camera_clearance);
    godot::ClassDB::bind_method(
        godot::D_METHOD("advance_thrust", "elapsed", "axes", "assist"),
        &FreedomBridge::advance_thrust);
    godot::ClassDB::bind_method(godot::D_METHOD("get_state"),
                                &FreedomBridge::get_state);
    godot::ClassDB::bind_method(godot::D_METHOD("get_flight_guidance", "mode"),
                                &FreedomBridge::get_flight_guidance);
    godot::ClassDB::bind_method(godot::D_METHOD("get_last_error"),
                                &FreedomBridge::get_last_error);
    godot::ClassDB::bind_method(godot::D_METHOD("enable_streaming"),
                                &FreedomBridge::enable_streaming);
    godot::ClassDB::bind_method(godot::D_METHOD("get_stream_identity"),
                                &FreedomBridge::get_stream_identity);
    godot::ClassDB::bind_method(godot::D_METHOD("request_stream", "observer"),
                                &FreedomBridge::request_stream);
    godot::ClassDB::bind_method(godot::D_METHOD("poll_stream"),
                                &FreedomBridge::poll_stream);
    godot::ClassDB::bind_method(godot::D_METHOD("is_stream_busy"),
                                &FreedomBridge::is_stream_busy);
    godot::ClassDB::bind_method(godot::D_METHOD("stream_transforms", "anchors"),
                                &FreedomBridge::stream_transforms);
    godot::ClassDB::bind_method(
        godot::D_METHOD("set_survey_pose", "latitude", "longitude", "altitude"),
        &FreedomBridge::set_survey_pose);
  }

 public:
  auto initialize_operating_motion(const godot::String& recipe_json) -> bool {
    try {
      if (static_cast<std::size_t>(recipe_json.length()) >
          kOperatingMotionMaximumDocumentBytes)
        throw std::invalid_argument("Motion recipe exceeds document bound");
      const auto utf8 = recipe_json.utf8();
      const auto candidate = decode_operating_motion_recipe(
          {utf8.get_data(), static_cast<std::size_t>(utf8.length())});
      if (!candidate) throw std::invalid_argument(candidate.error());
      operating_motion_recipe = *candidate;
      last_error = godot::String{};
      return true;
    } catch (const std::exception& error) {
      last_error = godot::String{error.what()};
      return false;
    }
  }

  static auto project_operating_pose(const OperatingPose& pose)
      -> godot::Dictionary {
    const auto matrices = [](const auto& values, const auto& ids) {
      godot::Dictionary result;
      for (std::size_t index = 0; index < values.size(); ++index) {
        godot::PackedFloat64Array columns;
        for (const auto& column : values[index].columns) {
          columns.append(column.x);
          columns.append(column.y);
          columns.append(column.z);
        }
        result[godot::String{std::string{ids[index]}.c_str()}] = columns;
      }
      return result;
    };
    godot::Dictionary result;
    result["craft_world_deltas"] =
        matrices(pose.craft_world_deltas, kOperatingCraftGroupIds);
    result["station_node_local"] =
        matrices(pose.station_node_local, kOperatingStationGroupIds);
    result["station_contact_deltas"] =
        matrices(pose.station_contact_deltas, kOperatingStationGroupIds);
    return result;
  }

  auto get_operating_motion_pose(double roof_transfer, double inner_door,
                                 double seat_boarding, double station_closure)
      -> godot::Dictionary {
    try {
      if (!operating_motion_recipe)
        throw std::invalid_argument(
            "Initialize the admitted motion recipe first");
      const auto pose = evaluate_operating_motion(
          *operating_motion_recipe,
          {roof_transfer, inner_door, seat_boarding, station_closure});
      if (!pose) throw std::invalid_argument(pose.error());
      const auto result = project_operating_pose(*pose);
      last_error = godot::String{};
      return result;
    } catch (const std::exception& error) {
      last_error = godot::String{error.what()};
      return {};
    }
  }

  auto initialize_freedom_new_game(const godot::String& universe_seed) -> bool {
    try {
      const auto utf8 = universe_seed.utf8();
      const std::string digits{utf8.get_data(),
                               static_cast<std::size_t>(utf8.length())};
      if (digits.empty() || digits.size() > 20 ||
          (digits.size() > 1 && digits.front() == '0'))
        throw std::invalid_argument(
            "New Game requires a canonical decimal seed");
      Seed seed;
      const auto parsed = std::from_chars(
          digits.data(), digits.data() + digits.size(), seed.value);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != digits.data() + digits.size())
        throw std::invalid_argument("New Game seed is malformed or overflows");
      auto selected = native_new_game(seed);
      if (!selected) throw std::runtime_error(selected.error());
      return commit_freedom_start(std::move(*selected));
    } catch (const std::exception& error) {
      last_error = godot::String{error.what()};
      return false;
    }
  }

  auto initialize_freedom_continue(const godot::String& save_path) -> bool {
    try {
      const auto utf8 = save_path.utf8();
      const std::string bytes{utf8.get_data(),
                              static_cast<std::size_t>(utf8.length())};
      if (bytes.empty() || bytes.size() > 4'096 ||
          bytes.find('\0') != std::string::npos)
        throw std::invalid_argument("Continue requires a bounded save path");
      const std::filesystem::path path{bytes};
      if (!path.is_absolute())
        throw std::invalid_argument("Continue requires an absolute save path");
      auto selected = native_continue(path);
      if (!selected) throw std::runtime_error(selected.error());
      return commit_freedom_start(std::move(*selected));
    } catch (const std::exception& error) {
      last_error = godot::String{error.what()};
      return false;
    }
  }

  static auto project_craft_binding(const NativeCraftBinding& binding)
      -> godot::Dictionary {
    godot::Dictionary result;
    const auto* selection = binding.selection();
    result["profile"] = selection ? "wayfarer-stowed-01" : "legacy-original";
    result["hardware_known"] = selection != nullptr;
    result["operating_model_sha256"] =
        selection ? godot::String{selection->operating_model_sha256.c_str()}
                  : godot::String{"12db339e004fcfa6586f745597108a69009b6b8ebc08"
                                  "8b5e4ff373dece656be8"};
    result["stowed_model_sha256"] =
        selection ? godot::String{selection->stowed_model_sha256.c_str()}
                  : godot::String{};
    result["frame_sha256"] =
        selection ? godot::String{selection->frame_sha256.c_str()}
                  : godot::String{};
    result["contact_sha256"] =
        selection ? godot::String{selection->contact_sha256.c_str()}
                  : godot::String{};
    godot::Array deltas;
    const auto transform = [](const OperatingTransform& value) {
      godot::Basis basis;
      for (int i = 0; i < 3; ++i) {
        const auto c = value.columns[static_cast<std::size_t>(i)];
        basis.set_column(i, godot::Vector3(c.x, c.y, c.z));
      }
      const auto t = value.columns[3];
      return godot::Transform3D{basis, godot::Vector3(t.x, t.y, t.z)};
    };
    if (const auto* pose = binding.pose())
      for (const auto& delta : pose->craft_world_deltas)
        deltas.append(transform(delta));
    result["craft_world_deltas"] = deltas;
    result["replacement_world_delta"] =
        binding.pose() ? transform(binding.pose()->craft_world_deltas[10])
                       : godot::Transform3D{};
    result["operating_atlas_surface"] =
        selection ? std::int64_t{14} : std::int64_t{18};
    return result;
  }
  auto get_freedom_craft_binding() const -> godot::Dictionary {
    if (saved_flight)
      return project_craft_binding(saved_flight->session.craft_binding());
    if (native_start) return project_craft_binding(NativeCraftBinding{});
    return {};
  }
  auto stage_selected(
      NativeStartup selected,
      std::optional<NativeProfileSession> metadata = std::nullopt) -> bool {
    if (pending)
      throw std::invalid_argument("A Freedom start is already pending");
    if (next_candidate_id == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("Freedom candidate identifiers exhausted");
    auto candidate = std::make_unique<PendingFreedomStart>();
    candidate->id = std::to_string(next_candidate_id);
    if (metadata)
      candidate->profile_session = std::move(*metadata);
    else if (selected.source_save) {
      auto explicit_source =
          NativeProfileSession::from_explicit_path(selected.document);
      if (!explicit_source)
        throw std::runtime_error(explicit_source.error().detail);
      candidate->profile_session = std::move(*explicit_source);
    }
    if (std::holds_alternative<FreedomFlightSaveDocument>(selected.document) ||
        std::holds_alternative<FreedomDockingSaveDocument>(selected.document) ||
        std::holds_alternative<FreedomJourneySaveDocument>(selected.document) ||
        std::holds_alternative<FreedomStartingAssemblySaveDocument>(
            selected.document) ||
        std::holds_alternative<FreedomBoardingSaveDocument>(
            selected.document) ||
        std::holds_alternative<FreedomSurfaceSaveDocument>(selected.document) ||
        std::holds_alternative<FreedomResourceSaveDocument>(
            selected.document) ||
        std::holds_alternative<FreedomKnowledgeSaveDocument>(
            selected.document) ||
        std::holds_alternative<FreedomTravelSaveDocument>(selected.document) ||
        std::holds_alternative<FreedomRecoverySaveDocument>(
            selected.document) ||
        std::holds_alternative<FreedomSurfaceWalkSaveDocument>(
            selected.document) ||
        std::holds_alternative<FreedomConditionSaveDocument>(
            selected.document) ||
        std::holds_alternative<FreedomAssistanceSaveDocument>(
            selected.document)) {
      auto opened = NativeFreedomFlightSession::open(std::move(selected));
      if (!opened) throw std::runtime_error(opened.error());
      validate_saved_projection(*opened);
      const bool station_available =
          !opened->moon() && !opened->minor() &&
          project_saved_flight(*opened).station_available;
      candidate->flight = std::make_unique<SavedFlightWorld>(
          SavedFlightWorld{std::move(*opened), {}, {}, 0});
      if (project_flight_state(*candidate->flight).is_empty() ||
          (station_available &&
           project_station_geometry(
               generate_origin_station(candidate->flight->session.document()
                                           .origin.recipe.universe_seed))
               .is_empty()) ||
          (candidate->flight->session.walker() &&
           project_walk_state(*candidate->flight).is_empty()))
        throw std::runtime_error("Pending Freedom presentation is unavailable");
      if (candidate->flight->session.moon()) {
        candidate->moon_stream = std::make_unique<MoonStreamRuntime>(
            *candidate->flight->session.moon());
        candidate->moon_stream->request(
            project_saved_moon_flight(candidate->flight->session)
                .tangent.origin);
      } else if (!candidate->flight->session.minor())
        candidate->stream = saved_stream(candidate->flight->session);
    } else {
      auto start = prepare_native_freedom_station_start(std::move(selected));
      if (!start) throw std::runtime_error(start.error());
      candidate->station =
          std::make_unique<NativeFreedomStationStart>(std::move(*start));
      if (project_station_start(*candidate->station).is_empty() ||
          project_station_geometry(candidate->station->station).is_empty())
        throw std::runtime_error("Pending station presentation is unavailable");
    }
    pending = std::move(candidate);
    ++next_candidate_id;
    last_error = godot::String{};
    return true;
  }
  auto stage_freedom_new_game(const godot::String& universe_seed) -> bool {
    return stage_freedom_new_game_selection(universe_seed, native_new_game);
  }
  auto stage_freedom_network_new_game(const godot::String& universe_seed)
      -> bool {
    return stage_freedom_new_game_selection(universe_seed,
                                            native_network_new_game);
  }
  auto stage_freedom_uncharted_network_new_game(
      const godot::String& universe_seed) -> bool {
    return stage_freedom_new_game_selection(universe_seed,
                                            native_uncharted_network_new_game);
  }
  auto stage_freedom_planetary_network_new_game(
      const godot::String& universe_seed) -> bool {
    return stage_freedom_new_game_selection(universe_seed,
                                            native_planetary_network_new_game);
  }
  auto stage_freedom_jump_recovery_new_game(const godot::String& universe_seed,
                                            std::int64_t chart_kind) -> bool {
    if (chart_kind < 0 || chart_kind > 3) {
      last_error = "Unsupported starting chart choice";
      return false;
    }
    if (chart_kind == 3)
      return stage_freedom_new_game_selection(
          universe_seed,
          +[](Seed seed) { return native_jump_recovery_new_game(seed, 3); });
    if (chart_kind == 0)
      return stage_freedom_new_game_selection(
          universe_seed,
          +[](Seed seed) { return native_jump_recovery_new_game(seed, 0); });
    if (chart_kind == 1)
      return stage_freedom_new_game_selection(
          universe_seed,
          +[](Seed seed) { return native_jump_recovery_new_game(seed, 1); });
    return stage_freedom_new_game_selection(
        universe_seed,
        +[](Seed seed) { return native_jump_recovery_new_game(seed, 2); });
  }
  auto stage_freedom_jump_strain_new_game(const godot::String& universe_seed,
                                          std::int64_t chart_kind) -> bool {
    if (chart_kind < 0 || chart_kind > 3) {
      last_error = "Unsupported starting chart choice";
      return false;
    }
    if (chart_kind == 3)
      return stage_freedom_new_game_selection(
          universe_seed,
          +[](Seed seed) { return native_jump_strain_new_game(seed, 3); });
    if (chart_kind == 0)
      return stage_freedom_new_game_selection(
          universe_seed,
          +[](Seed seed) { return native_jump_strain_new_game(seed, 0); });
    if (chart_kind == 1)
      return stage_freedom_new_game_selection(
          universe_seed,
          +[](Seed seed) { return native_jump_strain_new_game(seed, 1); });
    return stage_freedom_new_game_selection(
        universe_seed,
        +[](Seed seed) { return native_jump_strain_new_game(seed, 2); });
  }
  auto stage_freedom_origin_assistance_new_game(
      const godot::String& universe_seed, std::int64_t chart_kind,
      std::int64_t drive_kind) -> bool {
    if (chart_kind < 0 || chart_kind > 3 || drive_kind < 0 || drive_kind > 2) {
      last_error = "Unsupported starting chart or drive choice";
      return false;
    }
    return stage_freedom_new_game_selection(
        universe_seed, [chart_kind, drive_kind](Seed seed) {
          return native_origin_assistance_new_game(
              seed, static_cast<std::uint32_t>(chart_kind),
              static_cast<std::uint32_t>(drive_kind));
        });
  }
  auto request_freedom_origin_assistance() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.request_origin_assistance();
    });
  }
  auto cancel_freedom_origin_assistance() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.cancel_origin_assistance();
    });
  }
  template <class Factory>
  auto stage_freedom_new_game_selection(const godot::String& universe_seed,
                                        Factory factory) -> bool {
    try {
      if (pending)
        throw std::invalid_argument("A Freedom start is already pending");
      const auto utf8 = universe_seed.utf8();
      const std::string digits{utf8.get_data(),
                               static_cast<std::size_t>(utf8.length())};
      Seed seed;
      const auto read = std::from_chars(
          digits.data(), digits.data() + digits.size(), seed.value);
      if (digits.empty() || digits.size() > 20 ||
          (digits.size() > 1 && digits.front() == '0') ||
          read.ec != std::errc{} || read.ptr != digits.data() + digits.size())
        throw std::invalid_argument("New Game requires canonical decimal seed");
      auto selected = factory(seed);
      if (!selected) throw std::runtime_error(selected.error());
      return stage_selected(std::move(*selected));
    } catch (const std::exception& e) {
      last_error = e.what();
      return false;
    }
  }
  auto stage_freedom_continue(const godot::String& save_path) -> bool {
    try {
      if (pending)
        throw std::invalid_argument("A Freedom start is already pending");
      const auto utf8 = save_path.utf8();
      const std::string bytes{utf8.get_data(),
                              static_cast<std::size_t>(utf8.length())};
      const std::filesystem::path path{bytes};
      if (bytes.empty() || bytes.size() > 4096 ||
          bytes.find('\0') != std::string::npos || !path.is_absolute())
        throw std::invalid_argument(
            "Continue requires bounded absolute save path");
      auto selected = native_continue(path);
      if (!selected) throw std::runtime_error(selected.error());
      return stage_selected(std::move(*selected));
    } catch (const std::exception& e) {
      last_error = e.what();
      return false;
    }
  }
  auto get_freedom_recovery_state() const -> godot::Dictionary {
    if (!saved_flight) return {};
    auto document = saved_flight->session.recovery_document();
    if (!document) return {};
    auto explanation = freedom_recovery_explanation(*document);
    if (!explanation) return {};
    godot::Dictionary result;
    result["pending"] = document->recovery.pending;
    result["recorded"] = document->recovery.latest.has_value();
    result["explanation"] = godot::String{explanation->c_str()};
    result["craft_id"] = godot::String{
        std::to_string(
            recovery_flight(document->voyage).origin.state.craft.value)
            .c_str()};
    if (document->recovery.latest) {
      result["retired_craft_id"] = godot::String{
          std::to_string(recovery_flight(document->recovery.latest->retired)
                             .origin.state.craft.value)
              .c_str()};
      result["cause"] = document->recovery.latest->cause ==
                                FreedomLossCause::irrecoverable_destruction
                            ? "irrecoverable_destruction"
                            : "recoverable_destruction";
    }
    return result;
  }
  auto stage_freedom_recovery() -> bool {
    try {
      if (pending || !saved_flight || !saved_flight->session.recovery_pending())
        throw std::invalid_argument("Continue an actual pending loss first");
      auto source = saved_flight->session.save_document();
      if (!source) throw std::runtime_error(source.error());
      auto candidate = saved_flight->session;
      auto completed = candidate.complete_recovery();
      if (!completed) throw std::runtime_error(completed.error());
      auto replacement = candidate.save_document();
      if (!replacement) throw std::runtime_error(replacement.error());
      const auto planet = project_saved_flight(candidate).planet;
      NativeSaveDocument document = std::move(*replacement);
      if (!stage_selected({NativeStartup::Mode::freedom, std::move(document),
                           planet, candidate.source_save()},
                          profile_session))
        return false;
      pending->recovery_source = std::move(*source);
      return true;
    } catch (const std::exception& e) {
      last_error = e.what();
      return false;
    }
  }
  auto get_pending_freedom_start() const -> godot::Dictionary {
    if (!pending) return {};
    godot::Dictionary result;
    result["candidate_id"] = godot::String{pending->id.c_str()};
    result["streaming_ready"] =
        pending->stream != nullptr || pending->moon_stream != nullptr ||
        (pending->flight && pending->flight->session.minor().has_value());
    result["mode"] =
        pending->flight ? (pending->flight->session.walker() ? "freedom_walk"
                                                             : "freedom_flight")
                        : "freedom";
    result["craft_binding"] = project_craft_binding(
        pending->flight ? pending->flight->session.craft_binding()
                        : NativeCraftBinding{});
    result["walk_state"] = pending->flight
                               ? project_walk_state(*pending->flight)
                               : godot::Dictionary{};
    result["flight_state"] = pending->flight
                                 ? project_flight_state(*pending->flight)
                                 : godot::Dictionary{};
    result["station_state"] = pending->station
                                  ? project_station_start(*pending->station)
                                  : godot::Dictionary{};
    result["station_geometry"] =
        pending->station
            ? project_station_geometry(pending->station->station)
            : (!pending->flight->session.moon() &&
                       !pending->flight->session.minor() &&
                       project_saved_flight(pending->flight->session)
                           .station_available
                   ? project_station_geometry(generate_origin_station(
                         pending->flight->session.document()
                             .origin.recipe.universe_seed))
                   : godot::Dictionary{});
    return result;
  }
  auto matching_candidate(const godot::String& id) const -> bool {
    const auto utf8 = id.utf8();
    return pending &&
           std::string_view{utf8.get_data(), static_cast<std::size_t>(
                                                 utf8.length())} == pending->id;
  }
  auto commit_pending_freedom_start(const godot::String& id) -> bool {
    if (!matching_candidate(id)) {
      last_error = "Pending candidate identifier differs";
      return false;
    }
    if (pending->recovery_source) {
      if (!saved_flight) {
        last_error = "Recovery source is no longer active";
        return false;
      }
      auto source = saved_flight->session.save_document();
      if (!source || *source != *pending->recovery_source) {
        last_error = "Recovery source changed before replacement commit";
        return false;
      }
    }
    if (pending->moon_stream)
      replace_moon_stream(std::move(pending->moon_stream));
    else
      replace_stream(std::move(pending->stream));
    world.reset();
    saved_flight = std::move(pending->flight);
    native_start = std::move(pending->station);
    profile_session = std::move(pending->profile_session);
    pending.reset();
    last_error = godot::String{};
    return true;
  }
  auto discard_pending_freedom_start(const godot::String& id) -> bool {
    if (!matching_candidate(id)) {
      last_error = "Pending candidate identifier differs";
      return false;
    }
    pending.reset();
    last_error = godot::String{};
    return true;
  }

  auto current_save_document() const
      -> std::expected<NativeSaveDocument, std::string> {
    if (saved_flight) return saved_flight->session.save_document();
    if (native_start) return native_start->selected.document;
    return std::unexpected{"No Freedom journey is selected"};
  }

  auto list_freedom_profiles() -> godot::Dictionary {
    godot::Dictionary result;
    const auto directory = resolve_profile_directory();
    if (!directory) {
      profile_catalog.reset();
      result["writable"] = false;
      result["diagnostic"] = godot::String{directory.error().detail.c_str()};
      result["entries"] = godot::Array{};
      return result;
    }
    profile_catalog = scan_native_profile_catalog(*directory);
    result["writable"] = profile_catalog->writable;
    result["overflow"] = profile_catalog->overflow;
    result["diagnostic"] = godot::String{profile_catalog->diagnostic.c_str()};
    result["continue_index"] =
        profile_catalog->continue_index
            ? static_cast<std::int64_t>(*profile_catalog->continue_index)
            : -1;
    godot::Array entries;
    for (const auto& entry : profile_catalog->entries) {
      godot::Dictionary row;
      row["available"] = entry.activatable();
      row["diagnostic"] = godot::String{entry.diagnostic.c_str()};
      row["filename"] = godot::String{entry.path.filename().string().c_str()};
      if (entry.header) {
        row["id"] =
            godot::String{std::to_string(entry.header->id.value).c_str()};
        row["sequence"] =
            godot::String{std::to_string(entry.header->save_sequence).c_str()};
        row["seed"] = godot::String{
            std::to_string(entry.header->summary.universe_seed.value).c_str()};
        row["tick"] =
            godot::String{std::to_string(entry.header->summary.tick).c_str()};
        row["location"] = godot::String{std::string{
            native_profile_location_name(entry.header->summary.location)}
                                            .c_str()};
      }
      entries.append(row);
    }
    result["entries"] = entries;
    return result;
  }

  auto stage_freedom_profile(std::int64_t index) -> bool {
    try {
      if (pending || !profile_catalog || index < 0 ||
          static_cast<std::uint64_t>(index) >= profile_catalog->entries.size())
        throw std::invalid_argument("Select an available catalog entry");
      auto loaded = load_native_catalog_profile(
          profile_catalog->entries[static_cast<std::size_t>(index)]);
      if (!loaded) throw std::runtime_error(loaded.error().detail);
      auto metadata = NativeProfileSession::from_catalog(*loaded);
      if (!metadata) throw std::runtime_error(metadata.error().detail);
      auto selected = native_select_save_document(
          std::move(loaded->profile.document), loaded->path);
      if (!selected) throw std::runtime_error(selected.error());
      return stage_selected(std::move(*selected), std::move(*metadata));
    } catch (const std::exception& e) {
      last_error = e.what();
      return false;
    }
  }

  auto get_freedom_profile_state() const -> godot::Dictionary {
    godot::Dictionary result;
    const auto document = current_save_document();
    if (!document) return result;
    const auto dirty = profile_session.dirty(*document);
    result["dirty"] = !dirty || *dirty;
    result["explicit_path"] = profile_session.explicit_path();
    result["can_save_as"] = !profile_session.explicit_path();
    result["can_save"] = !profile_session.explicit_path() &&
                         profile_session.active().has_value();
    result["reason"] =
        profile_session.explicit_path()
            ? "Explicit-path session: use the existing file workflow"
        : !profile_session.active() ? "Save As creates your first catalog slot"
                                    : "";
    if (profile_session.active()) {
      result["id"] = godot::String{
          std::to_string(profile_session.active()->profile.header.id.value)
              .c_str()};
      result["sequence"] = godot::String{
          std::to_string(profile_session.active()->profile.header.save_sequence)
              .c_str()};
    }
    return result;
  }

  auto save_freedom_profile(bool save_as) -> bool {
    try {
      auto document = current_save_document();
      if (!document) throw std::runtime_error(document.error());
      const auto directory =
          !save_as && profile_session.active()
              ? std::expected<std::filesystem::path,
                              ProfileCatalogError>{profile_session.active()
                                                       ->path.parent_path()}
              : resolve_profile_directory();
      if (!directory) throw std::runtime_error(directory.error().detail);
      const auto saved =
          profile_session.save(*directory, std::move(*document), save_as);
      if (!saved) throw std::runtime_error(saved.error().detail);
      last_error = godot::String{};
      return true;
    } catch (const std::exception& e) {
      last_error = e.what();
      return false;
    }
  }

  auto save_freedom_as(const godot::String& save_path) -> bool {
    try {
      if (!native_start && !saved_flight)
        throw std::invalid_argument(
            "Save As requires a selected Freedom session");
      const auto utf8 = save_path.utf8();
      const std::string bytes{utf8.get_data(),
                              static_cast<std::size_t>(utf8.length())};
      const auto saved =
          saved_flight
              ? saved_flight->session.save_as(std::filesystem::path{bytes})
              : native_save_freedom(native_start->selected,
                                    std::filesystem::path{bytes});
      if (!saved) throw std::runtime_error(saved.error());
      last_error = godot::String{};
      return true;
    } catch (const std::exception& error) {
      last_error = godot::String{error.what()};
      return false;
    }
  }

  static auto project_station_start(const NativeFreedomStationStart& start)
      -> godot::Dictionary {
    godot::Dictionary result;
    const auto& save = std::get<FreedomSaveDocument>(start.selected.document);
    const auto coordinates = [](const auto& value) {
      godot::PackedFloat64Array array;
      array.append(value.x);
      array.append(value.y);
      array.append(value.z);
      return array;
    };
    result["mode"] = "freedom";
    result["universe_seed"] =
        godot::String{std::to_string(save.recipe.universe_seed.value).c_str()};
    result["system_seed"] =
        godot::String{std::to_string(start.system.catalog.seed.value).c_str()};
    result["system_id"] =
        godot::String{std::to_string(start.system.catalog.id.value).c_str()};
    result["home_planet_id"] =
        godot::String{std::to_string(start.host.planet.value).c_str()};
    result["home_planet_radius_metres"] =
        static_cast<double>(start.selected.home_planet.radius.value) * 1'000.0;
    result["station_id"] =
        godot::String{std::to_string(start.station.id.value).c_str()};
    result["craft_id"] =
        godot::String{std::to_string(save.state.craft.value).c_str()};
    result["tick"] = godot::String{std::to_string(save.state.tick).c_str()};
    result["cycle_tick"] =
        godot::String{std::to_string(start.ephemeris.cycle_tick).c_str()};
    result["continued"] = start.selected.source_save.has_value();
    result["discovery_count"] =
        static_cast<std::int64_t>(save.state.discoveries.size());
    result["world_delta_count"] =
        static_cast<std::int64_t>(save.state.world_deltas.size());
    result["host_position_metres"] = coordinates(start.host.position);
    result["host_velocity_metres_per_second"] =
        coordinates(start.host.velocity);
    result["station_position_metres"] = coordinates(start.ephemeris.position);
    result["station_velocity_metres_per_second"] =
        coordinates(start.ephemeris.velocity);
    result["station_relative_position_metres"] =
        coordinates(start.ephemeris.host_relative_position);
    result["station_relative_velocity_metres_per_second"] =
        coordinates(start.ephemeris.host_relative_velocity);
    result["station_phase_radians"] = start.ephemeris.phase_radians;
    return result;
  }

  auto get_freedom_start() const -> godot::Dictionary {
    return native_start ? project_station_start(*native_start)
                        : godot::Dictionary{};
  }

  auto advance_freedom_flight(double elapsed,
                              godot::PackedFloat64Array fractions, bool paused)
      -> bool {
    try {
      if (!saved_flight)
        throw std::invalid_argument("Continue a physical flight save first");
      // Terrain residency follows the authenticated planet owner.
      const auto previous_frame =
          require(saved_flight->session.relative_state()).frame;
      const auto previous_system = saved_flight->session.system().catalog.id;
      const auto previous_policy =
          saved_flight->session.document().model.terrain_policy;
      auto candidate = std::make_unique<SavedFlightWorld>(*saved_flight);
      candidate->advance(
          elapsed,
          {fractions.ptr(), static_cast<std::size_t>(fractions.size())},
          paused);
      if (previous_frame !=
              require(candidate->session.relative_state()).frame ||
          previous_system != candidate->session.system().catalog.id ||
          previous_policy !=
              candidate->session.document().model.terrain_policy) {
        replace_saved_stream(candidate->session);
      }
      saved_flight = std::move(candidate);
      last_error = godot::String{};
      return true;
    } catch (const std::exception& error) {
      last_error = godot::String{error.what()};
      return false;
    }
  }

  auto set_freedom_assistance(bool enabled) -> bool {
    if (!saved_flight) {
      last_error = "Continue a physical flight save first";
      return false;
    }
    const auto accepted = saved_flight->session.set_assistance(enabled);
    if (!accepted) {
      last_error = godot::String{accepted.error().c_str()};
      return false;
    }
    last_error = godot::String{};
    return true;
  }

  auto enable_freedom_impact_consequences() -> bool {
    if (!saved_flight) {
      last_error = "Select a saved Wayfarer voyage first";
      return false;
    }
    const auto accepted = saved_flight->session.enable_impact_consequences();
    if (!accepted) {
      last_error = godot::String{accepted.error().c_str()};
      return false;
    }
    last_error = godot::String{};
    return true;
  }

  auto set_freedom_hold(bool enabled) -> bool {
    if (!saved_flight) {
      last_error = "Continue a physical flight save first";
      return false;
    }
    const auto accepted = enabled ? saved_flight->session.hold_current_orbit()
                                  : saved_flight->session.set_hold({});
    if (!accepted) {
      last_error = godot::String{accepted.error().c_str()};
      return false;
    }
    last_error = godot::String{};
    return true;
  }

  template <class Command>
  auto change_freedom_port(Command command, bool reset_actuation = false)
      -> bool {
    try {
      if (!saved_flight)
        throw std::runtime_error("Continue physical flight first");
      auto candidate = saved_flight->session;
      auto changed = command(candidate);
      if (!changed) throw std::runtime_error(changed.error());
      validate_saved_projection(candidate);
      saved_flight->session = std::move(candidate);
      if (reset_actuation) saved_flight->last_step.reset();
      last_error = godot::String{};
      return true;
    } catch (const std::exception& error) {
      last_error = godot::String{error.what()};
      return false;
    }
  }

  static auto project_saved_jump(const NativeFreedomFlightSession& session,
                                 SimulationTick tick) -> godot::Dictionary {
    godot::Dictionary jump;
    const auto& travel = session.travel();
    const auto available = session.jump_available();
    jump["available"] = available.has_value();
    jump["refusal"] =
        available ? godot::String{} : godot::String{available.error().c_str()};
    jump["phase"] = !travel || travel->phase == FreedomJumpPhase::idle ? "idle"
                    : travel->phase == FreedomJumpPhase::spool         ? "spool"
                                                               : "transit";
    godot::String selected;
    std::optional<SimulationTick> arrival;
    if (travel) {
      if (travel->composed) {
        const auto& composed = *travel->composed;
        if (composed.active == FreedomComposedRouteKind::system) {
          if (travel->selected)
            selected =
                godot::String{system_id_string(*travel->selected).c_str()};
          if (travel->committed)
            arrival = travel->committed->preview.arrival_tick;
        } else {
          if (composed.selected_body)
            selected = godot::String{
                std::to_string(composed.selected_body->body.value).c_str()};
          if (composed.active == FreedomComposedRouteKind::planet) {
            jump["route_kind"] = "planet";
            if (composed.planet_committed)
              arrival = composed.planet_committed->arrival_tick;
            if (composed.selected_body && session.knowledge()) {
              const auto target = *composed.selected_body;
              const auto identity =
                  query_freedom_knowledge(*session.knowledge(),
                                          {KnowledgeSubjectKind::planet,
                                           target.system, target.body.value},
                                          KnowledgeFact::identity_type, tick);
              if (identity && *identity &&
                  std::holds_alternative<KnownIdentity>((*identity)->value))
                jump["destination_name"] = godot::String{
                    std::get<KnownIdentity>((*identity)->value).name.c_str()};
            }
          } else {
            if (composed.active == FreedomComposedRouteKind::foreign_moon ||
                composed.active == FreedomComposedRouteKind::foreign_minor) {
              jump["route_kind"] =
                  composed.active == FreedomComposedRouteKind::foreign_moon
                      ? "foreign_moon"
                      : "foreign_minor";
              if (composed.selected_body)
                jump["destination_name"] =
                    composed.selected_body->body.kind == BodyKind::moon ? "Moon"
                    : composed.selected_body->body.kind == BodyKind::minor
                        ? "Asteroid inspection"
                        : "Origin Home";
            }
            const auto& checkpoint =
                composed.active == FreedomComposedRouteKind::moon
                    ? composed.moon
                : composed.active == FreedomComposedRouteKind::minor
                    ? composed.minor
                : composed.active == FreedomComposedRouteKind::foreign_moon
                    ? composed.foreign_moon
                    : composed.foreign_minor;
            if (checkpoint.committed)
              arrival = checkpoint.committed->arrival_tick;
          }
        }
      } else if (travel->local) {
        if (travel->local->selected)
          selected = godot::String{
              std::to_string(travel->local->selected->body.value).c_str()};
        if (travel->local->committed)
          arrival = travel->local->committed->arrival_tick;
      } else {
        if (travel->selected && !session.moon() && !session.minor())
          selected = godot::String{system_id_string(*travel->selected).c_str()};
        if (travel->committed)
          arrival = travel->committed->preview.arrival_tick;
      }
    }
    jump["selected"] = selected;
    double remaining{};
    if (travel && travel->phase == FreedomJumpPhase::spool) {
      if (tick < travel->spool_tick ||
          tick - travel->spool_tick > kJumpSpoolTicks)
        throw std::runtime_error("Jump spool clock is unavailable");
      remaining = (kJumpSpoolTicks - (tick - travel->spool_tick)) / 120.0;
    } else if (travel && travel->phase == FreedomJumpPhase::transit) {
      if (!arrival || *arrival < tick)
        throw std::runtime_error("Committed jump arrival clock is unavailable");
      remaining = (*arrival - tick) / 120.0;
    }
    jump["remaining_seconds"] = remaining;
    if (session.resources() && session.resources()->drive) {
      const auto reading =
          freedom_jump_recovery_reading(*session.resources()->drive, tick);
      if (!reading)
        throw std::runtime_error("Drive recovery clock is unavailable");
      godot::Dictionary recovery;
      recovery["version"] = session.resources()->drive->version;
      recovery["remaining_seconds"] = reading->remaining_ticks / 120.0;
      recovery["extra_radius_metres"] =
          reading->extra_radius_millimetres / 1000.0;
      recovery["ready"] = reading->ready;
      recovery["pending_arrival"] = reading->pending_arrival;
      if (session.resources()->drive->version ==
          kFreedomStrainedDriveRecoveryVersion) {
        recovery["cycle_seconds"] = reading->cycle_ticks / 120.0;
        recovery["next_recovery_seconds"] =
            reading->next_recovery_ticks / 120.0;
      }
      jump["recovery"] = recovery;
    }
    if (!session.moon() && !session.minor()) {
      if (const auto preview = session.jump_preview(); preview) {
        jump["distance_light_hours"] =
            preview->distance_metres / (kMetresPerLightSecond * 3600.0);
        jump["heading_error_degrees"] =
            preview->alignment.heading_error_millidegrees / 1000.0;
        jump["drift_percent"] =
            preview->alignment.velocity_error_basis_points / 100.0;
        jump["quality"] = godot::String{
            intersystem_arrival_quality_name(preview->alignment.quality)
                .data()};
        jump["envelope_radius_metres"] =
            preview->distance.envelope_radius_metres
                ? godot::Variant{static_cast<std::int64_t>(
                      *preview->distance.envelope_radius_metres)}
                : godot::Variant{};
      }
    }
    return jump;
  }

  auto select_freedom_moon_jump() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session)
                                   -> std::expected<void, std::string> {
      if (session.moon()) return std::unexpected{"Already at the Moon"};
      const auto catalog = generate_secondary_body_catalog(session.system());
      if (!catalog) return std::unexpected{"Actual Moon catalog unavailable"};
      return session.select_local_jump(
          {session.system().catalog.id, catalog->bodies[0].node.id});
    });
  }
  auto select_freedom_minor_jump() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session)
                                   -> std::expected<void, std::string> {
      if (session.minor()) return std::unexpected{"Already at the minor body"};
      if (session.moon())
        return std::unexpected{"Return home before selecting the minor body"};
      const auto catalog = generate_secondary_body_catalog(session.system());
      if (!catalog)
        return std::unexpected{"Actual minor-body catalog unavailable"};
      return session.select_local_jump(
          {session.system().catalog.id, catalog->bodies[1].node.id});
    });
  }
  auto select_freedom_home_body_jump() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session)
                                   -> std::expected<void, std::string> {
      if (!session.moon() && !session.minor())
        return std::unexpected{
            "Return home requires an actual companion owner"};
      return session.select_local_jump(
          {generate_first_universe_route(
               session.document().origin.recipe.universe_seed)
               .origin,
           body_id(session.document().origin.recipe.home_planet)});
    });
  }
  auto select_freedom_jump(const godot::String& system_id) -> bool {
    return change_freedom_port([&](NativeFreedomFlightSession& session)
                                   -> std::expected<void, std::string> {
      const auto utf8 = system_id.utf8();
      const std::string_view text{utf8.get_data(),
                                  static_cast<std::size_t>(utf8.length())};
      if (text.size() != 23 || !text.starts_with("system-"))
        return std::unexpected{"Select a chart destination"};
      SystemId id;
      const auto parsed = std::from_chars(
          text.data() + 7, text.data() + text.size(), id.value, 16);
      if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
          system_id_string(id) != text)
        return std::unexpected{"Invalid chart destination"};
      return session.select_jump(id);
    });
  }
  auto get_freedom_planet_destinations() const -> godot::Dictionary {
    godot::Dictionary result;
    result["version"] = 1;
    result["selected"] = false;
    result["rows"] = godot::Array{};
    result["reason"] = "Planetary charts are not selected for this journey";
    if (!saved_flight) return result;
    const auto& session = saved_flight->session;
    if (!session.knowledge() || !freedom_knowledge_has_planetary_chart(
                                    session.knowledge()->recipe.version))
      return result;
    if (session.moon() || session.minor()) {
      result["reason"] = "Return to planetary space before selecting a planet";
      return result;
    }
    const auto& document = session.document();
    const auto planet = document.world ? std::optional{document.world->planet}
                                       : document.flight.frame.planet;
    if (!planet) return result;
    const auto known = resolve_freedom_known_planets(
        *session.knowledge(), {session.system().catalog.id, body_id(*planet)},
        document.flight.tick);
    if (!known) {
      result["reason"] = "Planetary navigation records are unavailable";
      return result;
    }
    godot::Array rows;
    for (const auto& row : *known) {
      godot::Dictionary item;
      item["system_id"] =
          godot::String{std::to_string(row.target.system.value).c_str()};
      item["planet_id"] =
          godot::String{std::to_string(row.target.body.value).c_str()};
      item["name"] = godot::String{row.name.c_str()};
      item["current"] = row.current;
      item["visited"] = row.visited;
      const auto available =
          session.planet_jump_selection_available(row.target);
      item["selectable"] = available.has_value();
      item["reason"] = available ? godot::String{}
                                 : godot::String{available.error().c_str()};
      rows.append(item);
    }
    result["selected"] = true;
    result["rows"] = rows;
    result["reason"] = godot::String{};
    return result;
  }
  auto select_freedom_planet_jump(const godot::String& system,
                                  const godot::String& planet) -> bool {
    const auto system_word = identity_word(system);
    const auto planet_word = identity_word(planet);
    if (!system_word || !planet_word) {
      last_error = "Planet destination requires canonical unsigned identities";
      return false;
    }
    return change_freedom_port([&](NativeFreedomFlightSession& session) {
      return session.select_planet_jump(
          {SystemId{*system_word}, body_id(PlanetId{*planet_word})});
    });
  }
  auto begin_freedom_jump() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.begin_jump();
    });
  }
  auto cancel_freedom_jump() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.cancel_jump();
    });
  }
  auto select_freedom_port(std::int64_t ordinal) -> bool {
    return change_freedom_port([ordinal](NativeFreedomFlightSession& session)
                                   -> std::expected<void, std::string> {
      if (ordinal < 1 || ordinal > 2) return std::unexpected{"Select D1 or D2"};
      return session.select_port(static_cast<std::uint32_t>(ordinal));
    });
  }
  auto capture_freedom_port() -> bool {
    return change_freedom_port(
        [](NativeFreedomFlightSession& session) {
          return session.capture_port();
        },
        true);
  }
  auto replenish_freedom_resources() -> bool {
    return change_freedom_port(
        [](NativeFreedomFlightSession& session) {
          return session.replenish_resources();
        },
        true);
  }
  auto release_freedom_port() -> bool {
    return change_freedom_port(
        [](NativeFreedomFlightSession& session) {
          return session.release_port();
        },
        true);
  }

  auto request_freedom_landing() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.request_landing();
    });
  }
  auto set_freedom_landing_gear(bool deployed) -> bool {
    return change_freedom_port([deployed](NativeFreedomFlightSession& session) {
      return session.set_landing_gear(deployed);
    });
  }
  auto stow_freedom_landing_gear() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.set_landing_gear(false);
    });
  }
  auto liftoff_freedom_surface() -> bool {
    return change_freedom_port(
        [](NativeFreedomFlightSession& session) {
          return session.release_surface();
        },
        true);
  }
  auto begin_freedom_surface_walk() -> bool {
    return change_freedom_port(
        [](NativeFreedomFlightSession& session) {
          return session.begin_surface_walk();
        },
        true);
  }
  auto return_from_freedom_surface_walk() -> bool {
    return change_freedom_port(
        [](NativeFreedomFlightSession& session) {
          return session.return_from_surface_walk();
        },
        true);
  }
  auto begin_freedom_orbital_survey() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.begin_orbital_survey();
    });
  }
  auto cancel_freedom_orbital_survey() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.cancel_orbital_survey();
    });
  }
  auto begin_freedom_star_search() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.begin_star_search();
    });
  }
  auto cancel_freedom_star_search() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.cancel_star_search();
    });
  }
  auto scan_freedom_derelict() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.scan_derelict();
    });
  }
  auto inspect_freedom_derelict() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.inspect_derelict();
    });
  }
  auto mark_freedom_surface_marker() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.mark_surface_marker();
    });
  }
  auto replace_freedom_surface_marker() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.replace_surface_marker();
    });
  }
  auto clear_freedom_surface_marker() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.clear_surface_marker();
    });
  }
  auto enable_freedom_surface_landmarks() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.enable_surface_landmarks();
    });
  }
  auto inspect_freedom_surface_landmark(const godot::String& id) -> bool {
    const auto utf8 = id.utf8();
    const std::string_view text{utf8.get_data(),
                                static_cast<std::size_t>(utf8.length())};
    std::uint64_t value{};
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || text.size() > 20 || parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size() ||
        std::to_string(value) != text) {
      last_error = "Exposure ID must be a canonical unsigned decimal word";
      return false;
    }
    return change_freedom_port([&](NativeFreedomFlightSession& session) {
      return session.inspect_surface_landmark(SurfaceLandmarkId{value});
    });
  }
  static auto identity_word(const godot::String& value)
      -> std::expected<std::uint64_t, std::string> {
    const auto utf8 = value.utf8();
    const std::string_view text{utf8.get_data(),
                                static_cast<std::size_t>(utf8.length())};
    std::uint64_t word{};
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), word);
    if (text.empty() || text.size() > 20 || parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size() || std::to_string(word) != text)
      return std::unexpected{
          "Identity must be a canonical unsigned decimal word"};
    return word;
  }
  auto get_freedom_field_sample_availability(const godot::String& id) const
      -> godot::Dictionary {
    godot::Dictionary result;
    result["available"] = false;
    const auto site = identity_word(id);
    if (!site || !saved_flight) {
      result["reason"] = !site ? godot::String{site.error().c_str()}
                               : godot::String{"Select a journey first"};
      return result;
    }
    const auto available =
        saved_flight->session.field_sample_collection_available(
            SurfaceLandmarkId{*site});
    result["available"] = available.has_value();
    result["reason"] =
        available ? godot::String{} : godot::String{available.error().c_str()};
    return result;
  }
  auto collect_freedom_field_sample(const godot::String& id) -> bool {
    return change_freedom_port([&](NativeFreedomFlightSession& session)
                                   -> std::expected<void, std::string> {
      const auto site = identity_word(id);
      if (!site) return std::unexpected{site.error()};
      return session.collect_field_sample(SurfaceLandmarkId{*site});
    });
  }
  auto discard_freedom_field_sample(const godot::String& system,
                                    const godot::String& kind,
                                    const godot::String& body,
                                    const godot::String& id) -> bool {
    return change_freedom_port([&](NativeFreedomFlightSession& session)
                                   -> std::expected<void, std::string> {
      const auto system_word = identity_word(system);
      const auto body_word = identity_word(body);
      const auto site = identity_word(id);
      if (!system_word || !body_word || !site ||
          (kind != "planet" && kind != "moon"))
        return std::unexpected{"Select an actual carried specimen"};
      const BodyTarget target{
          SystemId{*system_word},
          {kBodyIdentityVersion,
           kind == "planet" ? BodyKind::planet : BodyKind::moon, *body_word}};
      return session.discard_field_sample(target, SurfaceLandmarkId{*site});
    });
  }
  auto mark_freedom_field_sample_site(const godot::String& system,
                                      const godot::String& kind,
                                      const godot::String& body,
                                      const godot::String& id, bool replace)
      -> bool {
    return change_freedom_port([&](NativeFreedomFlightSession& session)
                                   -> std::expected<void, std::string> {
      const auto system_word = identity_word(system);
      const auto body_word = identity_word(body);
      const auto site = identity_word(id);
      if (!system_word || !body_word || !site ||
          (kind != "planet" && kind != "moon"))
        return std::unexpected{"Select an actual retained specimen note"};
      const BodyTarget target{
          SystemId{*system_word},
          {kBodyIdentityVersion,
           kind == "planet" ? BodyKind::planet : BodyKind::moon, *body_word}};
      return session.mark_field_sample_site(target, SurfaceLandmarkId{*site},
                                            replace);
    });
  }
  auto get_freedom_field_samples() const -> godot::Dictionary {
    godot::Dictionary result;
    result["selected"] = false;
    result["capacity"] = static_cast<std::int64_t>(kFreedomFieldSampleCapacity);
    result["carried"] = godot::Array{};
    result["journal"] = godot::Array{};
    if (!saved_flight || !saved_flight->session.knowledge()) return result;
    const auto& knowledge = *saved_flight->session.knowledge();
    if (!knowledge.field_samples) return result;
    const auto decimal = [](std::uint64_t value) {
      return godot::String{std::to_string(value).c_str()};
    };
    const auto project = [&](const FreedomFieldSampleRecord& sample) {
      godot::Dictionary row;
      row["system_id"] = decimal(sample.target.system.value);
      row["body_kind"] =
          sample.target.body.kind == BodyKind::planet ? "planet" : "moon";
      row["body_id"] = decimal(sample.target.body.value);
      row["site_id"] = decimal(sample.site.value);
      row["craft_id"] = decimal(sample.craft.value);
      row["tick"] = decimal(sample.tick);
      row["kind"] = static_cast<std::int64_t>(sample.kind);
      const char* label = "Pale specimen";
      switch (sample.kind) {
        case SurfaceLandmarkKind::pale_exposure: break;
        case SurfaceLandmarkKind::ochre_exposure:
          label = "Ochre specimen";
          break;
        case SurfaceLandmarkKind::dark_exposure: label = "Dark specimen"; break;
      }
      row["label"] = label;
      godot::String source =
          sample.target.body.kind == BodyKind::moon ? "Moon" : "Planet";
      if (sample.target.body.kind == BodyKind::planet) {
        const auto identity = query_freedom_knowledge(
            knowledge,
            {KnowledgeSubjectKind::planet, sample.target.system,
             sample.target.body.value},
            KnowledgeFact::identity_type,
            saved_flight->session.document().flight.tick);
        if (identity && *identity &&
            std::holds_alternative<KnownIdentity>((*identity)->value))
          source = godot::String{
              std::get<KnownIdentity>((*identity)->value).name.c_str()};
      }
      row["source_name"] = source;
      return row;
    };
    godot::Array carried, journal;
    for (const auto& sample : knowledge.field_samples->carried)
      carried.append(project(sample));
    for (const auto& sample : knowledge.field_samples->journal)
      journal.append(project(sample));
    result["selected"] = true;
    result["carried"] = carried;
    result["journal"] = journal;
    return result;
  }
  auto advance_freedom_surface_walk(double elapsed,
                                    const godot::PackedFloat64Array& controls,
                                    bool paused) -> bool {
    try {
      if (!saved_flight || !saved_flight->session.surface_walker())
        throw std::invalid_argument("Select a surface walking journey first");
      if (!std::isfinite(elapsed) || elapsed < 0 || elapsed > 60 ||
          controls.size() != 3)
        throw std::invalid_argument(
            "Invalid surface walking time/control buffer");
      for (std::int64_t i = 0; i < 3; ++i)
        if (!std::isfinite(controls[i]))
          throw std::invalid_argument("Surface controls must be finite");
      if (std::abs(controls[0]) > 1 || std::abs(controls[1]) > 1 ||
          std::abs(controls[2]) > std::numbers::pi)
        throw std::invalid_argument("Surface walking controls exceed bounds");
      auto candidate = saved_flight->session;
      auto clock = saved_flight->clock;
      auto actuation = saved_flight->last_step;
      const auto scheduled =
          require(clock.advance(SimulationSeconds{paused ? 0.0 : elapsed}));
      const OriginWalkControls demand{controls[0], controls[1], controls[2]};
      for (int i = 0; i < scheduled.steps; ++i)
        actuation = require(candidate.advance_surface_walk(demand));
      validate_saved_projection(candidate);
      saved_flight->session = std::move(candidate);
      saved_flight->clock = clock;
      saved_flight->last_step = std::move(actuation);
      saved_flight->dropped_seconds += scheduled.dropped.count();
      last_error = godot::String{};
      return true;
    } catch (const std::exception& error) {
      last_error = godot::String{error.what()};
      return false;
    }
  }

  static auto project_surface_walk_blockage(
      const NativeFreedomFlightSession& session) -> godot::Variant {
    const auto blocked = session.surface_walk_blockage();
    if (!blocked) return {};
    switch (*blocked) {
      case PlanetSurfaceWalkStatus::terrain_unresolved:
        return "terrain_unresolved";
      case PlanetSurfaceWalkStatus::water: return "water";
      case PlanetSurfaceWalkStatus::slope: return "slope";
      case PlanetSurfaceWalkStatus::step: return "step";
      case PlanetSurfaceWalkStatus::craft_obstruction:
        return "craft_obstruction";
      case PlanetSurfaceWalkStatus::supported:
        throw std::invalid_argument("Supported step cannot be a blockage");
      default: throw std::invalid_argument("Unknown surface movement result");
    }
  }

  static auto project_moon_surface_walk_actor(const SavedFlightWorld& selected)
      -> godot::Dictionary {
    godot::Dictionary result;
    const auto& session = selected.session;
    if (!session.moon() || !session.surface_walker() || !session.surface() ||
        !session.surface()->moon_landed)
      return result;
    const auto view = project_saved_moon_flight(session);
    const auto& actor = *session.surface_walker();
    const auto fixed = require(reframe_rigid_body(
        RigidBodyWorldContext{*session.moon()}, view.body,
        {view.fixed_frame, view.body.tick}, *session.moon()));
    const auto foot = actor.foot_position_metres;
    const double radius = std::hypot(foot.x, foot.y, foot.z);
    const RigidVector3 up{foot.x / radius, foot.y / radius, foot.z / radius};
    auto right = saved_moon_detail::rotate(
        session.surface()->moon_landed->fixed.orientation, {1, 0, 0});
    const double vertical = (right.x * up.x + right.y * up.y) + right.z * up.z;
    right = {right.x - vertical * up.x, right.y - vertical * up.y,
             right.z - vertical * up.z};
    const double magnitude = std::hypot(right.x, right.y, right.z);
    if (!std::isfinite(magnitude) || magnitude < 1e-8)
      throw std::invalid_argument("Moon walking heading basis unavailable");
    right = {right.x / magnitude, right.y / magnitude, right.z / magnitude};
    const RigidVector3 back{right.y * up.z - right.z * up.y,
                            right.z * up.x - right.x * up.z,
                            right.x * up.y - right.y * up.x};
    const auto vector = [&](RigidVector3 v) {
      const auto local = moon_fixed_vector_to_local(view, v);
      return godot::Vector3(local.x, local.y, local.z);
    };
    const auto point = [&](RigidVector3 v) {
      return vector({v.x - fixed.position_metres.x,
                     v.y - fixed.position_metres.y,
                     v.z - fixed.position_metres.z});
    };
    result["version"] = static_cast<std::int64_t>(actor.version);
    result["heading"] = actor.heading_radians;
    result["latitude"] = view.pose.latitude_radians;
    result["longitude"] = view.pose.longitude_radians;
    result["eye_position"] =
        point({foot.x + up.x * kOriginWalkerEyeHeightMetres,
               foot.y + up.y * kOriginWalkerEyeHeightMetres,
               foot.z + up.z * kOriginWalkerEyeHeightMetres});
    godot::Basis basis;
    basis.set_column(0, vector(right));
    basis.set_column(1, vector(up));
    basis.set_column(2, vector(back));
    result["basis"] = basis;
    godot::Dictionary access;
    const auto returned = session.surface_return_access();
    access["available"] = returned.has_value();
    if (returned) {
      access["position"] = point(returned->foot_position_metres);
      access["distance_metres"] = returned->distance_metres;
      access["within_reach"] = returned->within_reach;
    }
    result["return_access"] = access;
    result["movement_blockage"] = project_surface_walk_blockage(session);
    return result;
  }

  static auto project_moon_flight_state(const SavedFlightWorld& selected)
      -> godot::Dictionary {
    godot::Dictionary result;
    const auto& session = selected.session;
    const auto& document = session.document();
    const auto& body = document.flight;
    const auto view = project_saved_moon_flight(session);
    const auto fixed =
        require(reframe_rigid_body(RigidBodyWorldContext{view.moon}, body,
                                   {view.fixed_frame, body.tick}, view.moon));
    const auto observation = require(session.observe());
    if (!observation.moon)
      throw std::invalid_argument("Actual Moon observation missing");
    const auto coordinates = [](const auto& value) {
      godot::PackedFloat64Array array;
      for (double v : {value.x, value.y, value.z})
        array.append(v);
      return array;
    };
    const auto decimal = [](std::uint64_t v) {
      return godot::String{std::to_string(v).c_str()};
    };
    const auto vector = [](LocalPositionMetres v) {
      return godot::Vector3(v.east, v.up, -v.north);
    };
    godot::Basis basis, system_basis;
    for (int i = 0; i < 3; ++i) {
      basis.set_column(i, vector(view.body_axes[static_cast<std::size_t>(i)]));
      system_basis.set_column(
          i, vector(view.system_axes[static_cast<std::size_t>(i)]));
    }
    result["mode"] = "freedom_flight";
    result["body_kind"] = "moon";
    result["body_version"] = static_cast<std::int64_t>(view.moon.body.version);
    result["body_id"] = decimal(view.moon.body.value);
    result["body_radius"] = static_cast<double>(view.moon.radius_metres);
    result["universe_seed"] =
        decimal(document.origin.recipe.universe_seed.value);
    result["system_id"] = decimal(body.frame.system.value);
    result["craft_id"] = decimal(document.origin.state.craft.value);
    result["frame_id"] = decimal(body.craft.id.value);
    result["frame_version"] = static_cast<std::int64_t>(body.craft.version);
    result["tick"] = decimal(body.tick);
    result["checksum"] = decimal(require(
        rigid_body_state_checksum(RigidBodyWorldContext{view.moon}, body)));
    result["continued"] = session.source_save().has_value();
    result["coordinate_frame"] = "moon_relative_inertial";
    result["body_basis"] = basis;
    result["station_basis"] = system_basis;
    result["station_available"] = false;
    result["station_id"] = godot::String{};
    result["air_density"] = 0.0;
    result["dynamic_pressure"] = 0.0;
    result["recovery_pending"] = session.recovery_pending();
    if (const auto margin = rated_rise_margin(session, view))
      result["rated_rise_margin"] = *margin;

    result["attached"] = false;
    result["target_port"] = std::int64_t{0};
    result["assistance"] = document.model.assistance;
    result["hold_enabled"] = false;
    result["latitude"] = view.pose.latitude_radians;
    result["longitude"] = view.pose.longitude_radians;
    result["altitude"] = view.pose.altitude_metres;
    const auto velocity =
        session.surface_walker()
            ? session.surface_walker()->velocity_metres_per_second
            : fixed.linear_velocity_metres_per_second;
    result["surface_speed"] = std::hypot(velocity.x, velocity.y, velocity.z);
    result["radial_rate"] =
        session.surface_walker()
            ? (velocity.x * view.tangent.up.x +
               velocity.y * view.tangent.up.y) +
                  velocity.z * view.tangent.up.z
            : observation.moon->radial_speed_metres_per_second;
    result["position_metres"] = coordinates(body.position_metres);
    result["velocity_metres_per_second"] =
        coordinates(body.linear_velocity_metres_per_second);
    result["angular_velocity_body"] =
        coordinates(body.angular_velocity_radians_per_second);
    result["dropped_seconds"] = selected.dropped_seconds;
    result["boarding"] = project_boarding_state(selected);
    result["surface_walk"] = project_moon_surface_walk_actor(selected);
    result["orbital_survey"] = project_orbital_survey(selected.session);
    result["star_search"] = project_star_search(selected.session);
    result["surface_marker"] =
        project_surface_marker(selected, view.system_axes);
    godot::Dictionary surface;
    surface["gear_deployed"] =
        session.surface() && session.surface()->gear_deployed;
    surface["landed"] =
        session.surface() && session.surface()->moon_landed.has_value();
    surface["maneuver"] = "off";
    surface["remaining_seconds"] = 0.0;
    surface["note"] = godot::String{session.surface_maneuver().note.c_str()};
    godot::Dictionary clearance;
    clearance["available"] = false;
    // One current observation; never query contact from per-tile transforms.
    if (session.boarding() &&
        session.boarding()->phase == FreedomBoardingPhase::seated &&
        !session.surface_walker() && !session.recovery_pending() &&
        session.surface() && !session.surface()->moon_landed &&
        (!session.travel() ||
         session.travel()->phase == FreedomJumpPhase::idle)) {
      if (const auto reading = project_moon_gear_clearance(
              view.moon, body, session.surface()->gear_deployed)) {
        clearance["available"] = true;
        clearance["version"] = static_cast<std::int64_t>(reading->version);
        clearance["terrain_version"] =
            static_cast<std::int64_t>(reading->terrain_version);
        clearance["body_id"] = decimal(reading->body.value);
        clearance["body_version"] =
            static_cast<std::int64_t>(reading->body.version);
        clearance["system_id"] = decimal(reading->frame.system.value);
        clearance["craft_id"] = result["craft_id"];
        clearance["frame_id"] = decimal(reading->craft.id.value);
        clearance["frame_version"] =
            static_cast<std::int64_t>(reading->craft.version);
        clearance["tick"] = decimal(reading->tick);
        clearance["source_checksum"] = decimal(reading->source_checksum);
        clearance["minimum_gap_metres"] = reading->minimum_gap_metres;
        clearance["maximum_gap_metres"] = reading->maximum_gap_metres;
      }
    }
    surface["gear_clearance"] = clearance;
    result["surface"] = surface;
    godot::Dictionary resources;
    resources["selected"] = session.resources().has_value();
    if (session.resources()) {
      const auto reading = require(freedom_resource_reading(
          *session.resources(),
          selected.last_step ? selected.last_step->fuel_debit_quanta : 0,
          selected.last_step && selected.last_step->propulsion_refused));
      resources["quantity_quanta"] = decimal(reading.quantity_quanta);
      resources["capacity_quanta"] = decimal(reading.capacity_quanta);
      resources["last_tick_quanta"] = decimal(reading.last_tick_quanta);
      resources["fraction"] = reading.fraction;
      resources["main_equivalent_seconds"] = reading.main_equivalent_seconds;
      resources["equivalent_newtons"] = reading.equivalent_newtons;
      resources["jump_charges"] =
          static_cast<std::int64_t>(reading.jump_charges);
      resources["reserve"] = reading.reserve;
      resources["operationally_empty"] = reading.operationally_empty;
      resources["propulsion_refused"] = reading.propulsion_refused;
    }
    result["resources"] = resources;
    result["jump"] = project_saved_jump(session, body.tick);
    const auto force =
        selected.last_step && selected.last_step->moon &&
                !(session.surface() && session.surface()->moon_landed)
            ? selected.last_step->moon->propulsion
            : VacuumActuation{};
    result["positive_force_body"] = coordinates(force.positive_force_newtons);
    result["negative_force_body"] = coordinates(force.negative_force_newtons);
    result["positive_torque_body"] =
        coordinates(force.positive_torque_newton_metres);
    result["negative_torque_body"] =
        coordinates(force.negative_torque_newton_metres);
    result["applied_force_body"] = coordinates(force.applied_force_newtons);
    const auto frame = require(resolve_craft_frame(body.craft));
    godot::PackedFloat64Array positive, negative;
    for (const auto v : frame.properties.positive_force_newtons)
      positive.append(v);
    for (const auto v : frame.properties.negative_force_newtons)
      negative.append(v);
    result["positive_force_ratings"] = positive;
    result["negative_force_ratings"] = negative;
    result["star_direction"] = vector(view.star_direction);
    const auto& star = session.system().catalog.star;
    result["star_color"] =
        godot::Color(star.color.red / 255.0f, star.color.green / 255.0f,
                     star.color.blue / 255.0f, 1);
    result["star_angular_radius"] = std::asin(
        std::min(1.0, static_cast<double>(star.radius_kilometres) * 1000.0 /
                          view.rotation.observer_star_distance_metres));
    result["surface_landmarks"] =
        project_moon_surface_landmarks(selected, view);
    return result;
  }

  static auto project_minor_flight_state(const SavedFlightWorld& selected)
      -> godot::Dictionary {
    godot::Dictionary result;
    const auto& session = selected.session;
    const auto& document = session.document();
    const auto& body = document.flight;
    const auto view = project_saved_minor_flight(session);
    const auto fixed =
        require(reframe_rigid_body(RigidBodyWorldContext{view.minor}, body,
                                   {view.fixed_frame, body.tick}, view.minor));
    const auto observation = require(session.observe());
    if (!observation.minor)
      throw std::invalid_argument("Actual minor observation missing");
    const auto coordinates = [](const auto& value) {
      godot::PackedFloat64Array array;
      for (double v : {value.x, value.y, value.z})
        array.append(v);
      return array;
    };
    const auto decimal = [](std::uint64_t v) {
      return godot::String{std::to_string(v).c_str()};
    };
    const auto vector = [](LocalPositionMetres v) {
      return godot::Vector3(v.east, v.up, -v.north);
    };
    godot::Basis basis, system_basis;
    for (int i = 0; i < 3; ++i) {
      basis.set_column(i, vector(view.body_axes[static_cast<std::size_t>(i)]));
      system_basis.set_column(
          i, vector(view.system_axes[static_cast<std::size_t>(i)]));
    }
    result["mode"] = "freedom_flight";
    result["body_kind"] = "minor";
    result["body_version"] = static_cast<std::int64_t>(view.minor.body.version);
    result["body_id"] = decimal(view.minor.body.value);
    result["body_radius"] = static_cast<double>(view.minor.radius_metres);
    result["universe_seed"] =
        decimal(document.origin.recipe.universe_seed.value);
    result["system_id"] = decimal(body.frame.system.value);
    result["craft_id"] = decimal(document.origin.state.craft.value);
    result["frame_id"] = decimal(body.craft.id.value);
    result["frame_version"] = static_cast<std::int64_t>(body.craft.version);
    result["tick"] = decimal(body.tick);
    result["checksum"] = decimal(require(
        rigid_body_state_checksum(RigidBodyWorldContext{view.minor}, body)));
    result["continued"] = session.source_save().has_value();
    result["coordinate_frame"] = "minor_relative_inertial";
    result["body_basis"] = basis;
    result["station_basis"] = system_basis;
    result["station_available"] = false;
    result["station_id"] = godot::String{};
    result["air_density"] = 0.0;
    result["dynamic_pressure"] = 0.0;
    result["recovery_pending"] = session.recovery_pending();

    result["attached"] = false;
    result["target_port"] = std::int64_t{0};
    result["assistance"] = document.model.assistance;
    result["hold_enabled"] = false;
    result["latitude"] = view.pose.latitude_radians;
    result["longitude"] = view.pose.longitude_radians;
    result["altitude"] = view.pose.altitude_metres;
    const auto velocity = fixed.linear_velocity_metres_per_second;
    result["surface_speed"] = std::hypot(velocity.x, velocity.y, velocity.z);
    result["radial_rate"] = observation.minor->radial_speed_metres_per_second;
    result["position_metres"] = coordinates(body.position_metres);
    result["velocity_metres_per_second"] =
        coordinates(body.linear_velocity_metres_per_second);
    result["angular_velocity_body"] =
        coordinates(body.angular_velocity_radians_per_second);
    result["dropped_seconds"] = selected.dropped_seconds;
    result["boarding"] = project_boarding_state(selected);
    result["surface_walk"] = godot::Dictionary{};
    result["orbital_survey"] = project_orbital_survey(selected.session);
    result["star_search"] = project_star_search(selected.session);
    result["surface_marker"] =
        project_surface_marker(selected, view.system_axes);
    godot::Dictionary surface;
    surface["gear_deployed"] = false;
    surface["landed"] = false;
    surface["maneuver"] = "off";
    surface["remaining_seconds"] = 0.0;
    surface["note"] = godot::String{session.surface_maneuver().note.c_str()};
    godot::Dictionary clearance;
    clearance["available"] = false;
    surface["gear_clearance"] = clearance;
    result["surface"] = surface;
    godot::Dictionary resources;
    resources["selected"] = session.resources().has_value();
    if (session.resources()) {
      const auto reading = require(freedom_resource_reading(
          *session.resources(),
          selected.last_step ? selected.last_step->fuel_debit_quanta : 0,
          selected.last_step && selected.last_step->propulsion_refused));
      resources["quantity_quanta"] = decimal(reading.quantity_quanta);
      resources["capacity_quanta"] = decimal(reading.capacity_quanta);
      resources["last_tick_quanta"] = decimal(reading.last_tick_quanta);
      resources["fraction"] = reading.fraction;
      resources["main_equivalent_seconds"] = reading.main_equivalent_seconds;
      resources["equivalent_newtons"] = reading.equivalent_newtons;
      resources["jump_charges"] =
          static_cast<std::int64_t>(reading.jump_charges);
      resources["reserve"] = reading.reserve;
      resources["operationally_empty"] = reading.operationally_empty;
      resources["propulsion_refused"] = reading.propulsion_refused;
    }
    result["resources"] = resources;
    result["jump"] = project_saved_jump(session, body.tick);
    const auto force = selected.last_step && selected.last_step->minor
                           ? selected.last_step->minor->propulsion
                           : VacuumActuation{};
    result["positive_force_body"] = coordinates(force.positive_force_newtons);
    result["negative_force_body"] = coordinates(force.negative_force_newtons);
    result["positive_torque_body"] =
        coordinates(force.positive_torque_newton_metres);
    result["negative_torque_body"] =
        coordinates(force.negative_torque_newton_metres);
    result["applied_force_body"] = coordinates(force.applied_force_newtons);
    const auto frame = require(resolve_craft_frame(body.craft));
    godot::PackedFloat64Array positive, negative;
    for (const auto v : frame.properties.positive_force_newtons)
      positive.append(v);
    for (const auto v : frame.properties.negative_force_newtons)
      negative.append(v);
    result["positive_force_ratings"] = positive;
    result["negative_force_ratings"] = negative;
    result["star_direction"] = vector(view.star_direction);
    const auto& star = session.system().catalog.star;
    result["star_color"] =
        godot::Color(star.color.red / 255.0f, star.color.green / 255.0f,
                     star.color.blue / 255.0f, 1);
    result["star_angular_radius"] = std::asin(
        std::min(1.0, static_cast<double>(star.radius_kilometres) * 1000.0 /
                          view.rotation.observer_star_distance_metres));
    result["minor_center"] = godot::Vector3(
        view.minor_center.x, view.minor_center.y, view.minor_center.z);
    godot::Basis minor_basis;
    for (int i = 0; i < 3; ++i) {
      const auto& axis = view.minor_fixed_basis[static_cast<std::size_t>(i)];
      minor_basis.set_column(i, godot::Vector3(axis.x, axis.y, axis.z));
    }
    result["minor_fixed_basis"] = minor_basis;
    result["minor_tint"] = godot::Color(view.minor.tint.red / 255.0f,
                                        view.minor.tint.green / 255.0f,
                                        view.minor.tint.blue / 255.0f, 1);
    const auto exclusion =
        require(minor_exclusion_radius_metres(view.minor, body.craft));
    result["exclusion_radius"] = exclusion;
    result["stand_off_gap"] =
        observation.minor->gravity.distance_metres - exclusion;
    result["inspection_limited"] = true;
    result["inspection_note"] =
        selected.last_step && selected.last_step->minor_standoff_limited
            ? "Stand-off limit: inward motion stopped; steer or thrust away."
            : "";
    return result;
  }

  static auto project_surface_walk_actor(const SavedFlightWorld& selected)
      -> godot::Dictionary {
    godot::Dictionary result;
    const auto& session = selected.session;
    if (session.moon()) return project_moon_surface_walk_actor(selected);
    if (!session.surface_walker() || !session.surface() ||
        !session.surface()->landed)
      return result;
    const auto rotate = [](RigidOrientation q, RigidVector3 v) {
      const double n = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
      const double k = 2 * (q.x * v.x + q.y * v.y + q.z * v.z) / n;
      const double s = (q.w * q.w - q.x * q.x - q.y * q.y - q.z * q.z) / n;
      return RigidVector3{
          s * v.x + k * q.x + 2 * q.w * (q.y * v.z - q.z * v.y) / n,
          s * v.y + k * q.y + 2 * q.w * (q.z * v.x - q.x * v.z) / n,
          s * v.z + k * q.z + 2 * q.w * (q.x * v.y - q.y * v.x) / n};
    };
    const auto& actor = *session.surface_walker();
    const auto body = require(session.relative_state());
    const auto foot = actor.foot_position_metres;
    const auto radius = std::hypot(foot.x, foot.y, foot.z);
    const RigidVector3 up{foot.x / radius, foot.y / radius, foot.z / radius};
    auto right =
        rotate(session.surface()->landed->fixed.orientation, {1, 0, 0});
    const auto vertical = (right.x * up.x + right.y * up.y) + right.z * up.z;
    right = {right.x - up.x * vertical, right.y - up.y * vertical,
             right.z - up.z * vertical};
    const auto magnitude = std::hypot(right.x, right.y, right.z);
    if (!std::isfinite(magnitude) || magnitude < 1e-8)
      throw std::invalid_argument("Surface heading basis is unavailable");
    right = {right.x / magnitude, right.y / magnitude, right.z / magnitude};
    const RigidVector3 back{right.y * up.z - right.z * up.y,
                            right.z * up.x - right.x * up.z,
                            right.x * up.y - right.y * up.x};
    const auto rotation =
        require(resolve_planet_rotation(session.system(), session.rotation(),
                                        body.tick))
            .geometry.fixed_to_system;
    const auto view = project_saved_flight(session);
    const auto position = require(geodetic_from_planet_fixed(
        view.planet, PlanetFixedPositionMetres{foot.x, foot.y, foot.z}));
    result["latitude"] = position.latitude_radians;
    result["longitude"] = position.longitude_radians;
    const auto local = [&](RigidVector3 v) {
      const auto& a = view.system_axes;
      return godot::Vector3{
          static_cast<godot::real_t>(a[0].east * v.x + a[1].east * v.y +
                                     a[2].east * v.z),
          static_cast<godot::real_t>(a[0].up * v.x + a[1].up * v.y +
                                     a[2].up * v.z),
          static_cast<godot::real_t>(
              -(a[0].north * v.x + a[1].north * v.y + a[2].north * v.z))};
    };
    const auto eye =
        rotate(rotation, {foot.x + up.x * kOriginWalkerEyeHeightMetres,
                          foot.y + up.y * kOriginWalkerEyeHeightMetres,
                          foot.z + up.z * kOriginWalkerEyeHeightMetres});
    result["eye_position"] =
        local({eye.x - body.position_metres.x, eye.y - body.position_metres.y,
               eye.z - body.position_metres.z});
    godot::Basis basis;
    basis.set_column(0, local(rotate(rotation, right)));
    basis.set_column(1, local(rotate(rotation, up)));
    basis.set_column(2, local(rotate(rotation, back)));
    result["basis"] = basis;
    godot::Dictionary access;
    const auto returned = session.surface_return_access();
    access["available"] = returned.has_value();
    if (returned) {
      const auto point = rotate(rotation, returned->foot_position_metres);
      access["position"] = local({point.x - body.position_metres.x,
                                  point.y - body.position_metres.y,
                                  point.z - body.position_metres.z});
      access["distance_metres"] = returned->distance_metres;
      access["within_reach"] = returned->within_reach;
    }
    result["return_access"] = access;
    result["movement_blockage"] = project_surface_walk_blockage(session);
    result["heading"] = actor.heading_radians;
    result["version"] = static_cast<std::int64_t>(actor.version);
    return result;
  }
  auto cancel_freedom_surface_maneuver() -> bool {
    return change_freedom_port(
        [](NativeFreedomFlightSession& session)
            -> std::expected<void, std::string> {
          session.cancel_surface_maneuver();
          return {};
        },
        true);
  }
  auto begin_freedom_port_docking() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.begin_port_docking();
    });
  }
  auto cancel_freedom_port_docking() -> bool {
    return change_freedom_port(
        [](NativeFreedomFlightSession& session)
            -> std::expected<void, std::string> {
          session.cancel_port_docking();
          return {};
        },
        true);
  }
  auto begin_freedom_port_approach() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.begin_port_approach();
    });
  }
  auto cancel_freedom_port_approach() -> bool {
    return change_freedom_port(
        [](NativeFreedomFlightSession& session)
            -> std::expected<void, std::string> {
          session.cancel_port_approach();
          return {};
        },
        true);
  }
  auto begin_freedom_port_rendezvous() -> bool {
    return change_freedom_port([](NativeFreedomFlightSession& session) {
      return session.begin_port_rendezvous();
    });
  }
  auto cancel_freedom_port_rendezvous() -> bool {
    return change_freedom_port(
        [](NativeFreedomFlightSession& session)
            -> std::expected<void, std::string> {
          session.cancel_port_rendezvous();
          return {};
        },
        true);
  }

  auto begin_freedom_boarding() -> bool {
    return change_freedom_port(
        [](NativeFreedomFlightSession& session) {
          return session.begin_boarding();
        },
        true);
  }
  auto get_freedom_boarding_availability() const -> godot::Dictionary {
    godot::Dictionary result;
    if (!saved_flight) return result;
    const auto& session = saved_flight->session;
    const auto& document = session.document();
    const auto available = session.boarding_available();
    const auto decimal = [](std::uint64_t value) {
      return godot::String{std::to_string(value).c_str()};
    };
    result["version"] = 1;
    result["available"] = available.has_value();
    result["reason"] = available ? godot::String{"Climb aboard Wayfarer"}
                                 : godot::String{available.error().c_str()};
    result["universe_seed"] =
        decimal(document.origin.recipe.universe_seed.value);
    result["system_id"] = decimal(session.system().catalog.id.value);
    result["craft_id"] = decimal(document.origin.state.craft.value);
    result["tick"] = decimal(document.flight.tick);
    return result;
  }
  auto begin_freedom_disembarking() -> bool {
    return change_freedom_port(
        [](NativeFreedomFlightSession& session) {
          return session.begin_disembarking();
        },
        true);
  }

  static auto project_boarding_state(const SavedFlightWorld& selected)
      -> godot::Dictionary {
    godot::Dictionary result;
    const auto& session = selected.session;
    if (!session.boarding()) return result;
    try {
      const auto view = require(session.boarding_view());
      const auto vector = [](RigidVector3 value) {
        godot::PackedFloat64Array a;
        for (const auto v : {value.x, value.y, value.z})
          a.append(v);
        return a;
      };
      switch (session.boarding()->phase) {
        case FreedomBoardingPhase::station: result["state"] = "station"; break;
        case FreedomBoardingPhase::boarding:
          result["state"] = "boarding";
          break;
        case FreedomBoardingPhase::seated: result["state"] = "seated"; break;
        case FreedomBoardingPhase::disembarking:
          result["state"] = "disembarking";
          break;
      }
      constexpr std::array names{"approach", "ladder",   "cabin",
                                 "seat",     "hardware", "complete"};
      result["phase"] = names[static_cast<std::size_t>(view.phase)];
      result["progress"] = view.progress;
      result["heading_radians"] = view.heading_radians;
      result["eye_station"] = vector(view.eye_station);
      result["eye_craft"] = vector(view.eye_craft);
      result["seated"] = view.seated;
      result["complete"] = view.complete;
      result["pose"] = project_operating_pose(view.operating_pose);
    } catch (const std::exception&) {
      result.clear();
    }
    return result;
  }
  auto get_freedom_boarding_state() const -> godot::Dictionary {
    return saved_flight ? project_boarding_state(*saved_flight)
                        : godot::Dictionary{};
  }

  auto advance_freedom_walk(double elapsed,
                            const godot::PackedFloat64Array& controls) -> bool {
    try {
      if (!saved_flight || !saved_flight->session.walker())
        throw std::invalid_argument("Select a station walking journey first");
      if (!std::isfinite(elapsed) || elapsed < 0 || elapsed > 60 ||
          controls.size() != 3)
        throw std::invalid_argument("Invalid walking time/control buffer");
      for (std::int64_t i = 0; i < controls.size(); ++i)
        if (!std::isfinite(controls[i]))
          throw std::invalid_argument("Walking controls must be finite");
      if (std::abs(controls[0]) > 1 || std::abs(controls[1]) > 1 ||
          std::abs(controls[2]) > std::numbers::pi)
        throw std::invalid_argument("Walking axes/heading exceed bounds");
      auto candidate = saved_flight->session;
      auto clock = saved_flight->clock;
      auto actuation = saved_flight->last_step;
      const auto scheduled = require(clock.advance(SimulationSeconds{elapsed}));
      const OriginWalkControls demand{controls[0], controls[1], controls[2]};
      for (int i = 0; i < scheduled.steps; ++i)
        actuation = candidate.walker()
                        ? require(candidate.advance_walk(demand))
                        : require(candidate.advance(NativeFlightControls{}));
      validate_saved_projection(candidate);
      saved_flight->session = std::move(candidate);
      saved_flight->clock = clock;
      saved_flight->last_step = std::move(actuation);
      saved_flight->dropped_seconds += scheduled.dropped.count();
      last_error = godot::String{};
      return true;
    } catch (const std::exception& error) {
      last_error = godot::String{error.what()};
      return false;
    }
  }

  static auto project_walk_state(const SavedFlightWorld& selected)
      -> godot::Dictionary {
    godot::Dictionary result;
    if (!selected.session.walker()) return result;
    try {
      const auto& session = selected.session;
      const auto& actor = *session.walker();
      const auto& document = session.document();
      const auto view = project_saved_flight(session);
      const auto ephemeris = require(resolve_origin_station_ephemeris(
          session.system(), view.station, {document.flight.tick}));
      const auto coordinates = [](const auto& value) {
        godot::PackedFloat64Array array;
        for (double v : {value.x, value.y, value.z})
          array.append(v);
        return array;
      };
      const auto decimal = [](std::uint64_t value) {
        return godot::String{std::to_string(value).c_str()};
      };
      const auto local_vector = [](LocalPositionMetres value) {
        return godot::Vector3(value.east, value.up, -value.north);
      };
      godot::Basis basis;
      for (int i = 0; i < 3; ++i)
        basis.set_column(
            i, local_vector(view.system_axes[static_cast<std::size_t>(i)]));
      const auto foot = actor.foot_position_metres;
      // Eye height is presentation framing inside the qualified standing body.
      // The station-relative support point remains entirely application-owned.
      RigidVector3 eye{foot.x, foot.y + kOriginWalkerEyeHeightMetres, foot.z};
      const auto boarding = project_boarding_state(selected);
      if (session.boarding() &&
          (session.boarding()->phase == FreedomBoardingPhase::boarding ||
           session.boarding()->phase == FreedomBoardingPhase::disembarking))
        eye = require(session.boarding_view()).eye_station;
      const auto project = [&](RigidVector3 value) {
        // Complete subtraction/projection in binary64 before the renderer cast.
        const auto& axes = view.system_axes;
        return LocalPositionMetres{
            axes[0].east * value.x + axes[1].east * value.y +
                axes[2].east * value.z,
            axes[0].north * value.x + axes[1].north * value.y +
                axes[2].north * value.z,
            axes[0].up * value.x + axes[1].up * value.y + axes[2].up * value.z};
      };
      result["mode"] = "freedom_walk";
      result["universe_seed"] =
          decimal(document.origin.recipe.universe_seed.value);
      result["system_id"] = decimal(session.system().catalog.id.value);
      result["planet_id"] = decimal(view.planet.id.value);
      result["station_id"] = decimal(view.station.id.value);
      result["craft_id"] = decimal(document.origin.state.craft.value);
      result["actor_id"] = decimal(actor.actor_id);
      result["tick"] = decimal(document.flight.tick);
      result["geometry_version"] =
          static_cast<std::int64_t>(actor.geometry_version);
      godot::Dictionary access;
      access["minimum_x"] = kOriginWalkMinimumXMetres;
      access["maximum_x"] = kOriginWalkMaximumXMetres;
      access["minimum_z"] = kOriginWalkMinimumZMetres;
      access["maximum_z"] = kOriginWalkMaximumZMetres;
      result["walk_access"] = access;
      result["continued"] = session.source_save().has_value();
      result["foot_position_metres"] = coordinates(foot);
      result["eye_position_metres"] = coordinates(eye);
      result["velocity_metres_per_second"] =
          coordinates(actor.velocity_metres_per_second);
      result["heading_radians"] = actor.heading_radians;
      result["station_basis"] = basis;
      result["station_position"] = local_vector(view.station_position);
      const auto eye_offset = project(eye);
      result["actor_eye_position"] =
          godot::Vector3(view.station_position.east + eye_offset.east,
                         view.station_position.up + eye_offset.up,
                         -(view.station_position.north + eye_offset.north));
      result["actor_position_metres"] = coordinates(
          RigidVector3{ephemeris.host_relative_position.x + foot.x,
                       ephemeris.host_relative_position.y + foot.y,
                       ephemeris.host_relative_position.z + foot.z});
      result["actor_global_position_metres"] = coordinates(RigidVector3{
          ephemeris.position.x + foot.x, ephemeris.position.y + foot.y,
          ephemeris.position.z + foot.z});
      result["dropped_seconds"] = selected.dropped_seconds;
      if (!boarding.is_empty()) result["boarding"] = boarding;
    } catch (const std::exception&) {
      // Read-only presentation queries do not overwrite a command refusal.
      result.clear();
    }
    return result;
  }

  auto get_freedom_walk_state() const -> godot::Dictionary {
    return saved_flight ? project_walk_state(*saved_flight)
                        : godot::Dictionary{};
  }

  auto get_freedom_station_crew() const -> godot::Dictionary {
    godot::Dictionary result;
    if (!saved_flight) return result;
    const auto crew = saved_flight->session.station_crew();
    if (!crew || crew->topic_count < 3 ||
        crew->topic_count > crew->topics.size())
      return result;
    result["version"] = 2;
    result["station_id"] =
        godot::String{std::to_string(crew->station.value).c_str()};
    result["tick"] = godot::String{
        std::to_string(saved_flight->session.document().flight.tick).c_str()};
    result["ordinal"] = static_cast<std::int64_t>(crew->ordinal);
    result["name"] = godot::String{crew->name.data()};
    godot::PackedFloat64Array foot;
    for (const auto value :
         {crew->foot_position_metres.x, crew->foot_position_metres.y,
          crew->foot_position_metres.z})
      foot.append(value);
    result["foot_position_metres"] = foot;
    result["heading_radians"] = crew->heading_radians;
    result["height_metres"] = crew->height_metres;
    result["available"] = crew->available;
    result["reason"] = godot::String{crew->reason.c_str()};
    godot::Array topics;
    for (std::size_t i = 0; i < crew->topic_count; ++i) {
      const auto& topic = crew->topics[i];
      godot::Dictionary item;
      item["id"] = static_cast<std::int64_t>(topic.id);
      item["title"] = godot::String{topic.title.data()};
      item["text"] = godot::String{topic.text.data()};
      topics.append(item);
    }
    result["topics"] = topics;
    result["remark"] = godot::String{crew->remark.data()};
    return result;
  }

  static auto project_star_search(const NativeFreedomFlightSession& session)
      -> godot::Dictionary {
    const auto view = session.star_search_view();
    godot::Dictionary result;
    result["selected"] = view.selected;
    result["active"] = view.active;
    result["can_begin"] = view.can_begin;
    result["acquired_ticks"] = static_cast<std::int64_t>(view.acquired_ticks);
    result["completed"] = static_cast<std::int64_t>(view.completed);
    result["note"] = godot::String{view.note.c_str()};
    return result;
  }
  static auto project_orbital_survey(const NativeFreedomFlightSession& session)
      -> godot::Dictionary {
    const auto view = session.orbital_survey_view();
    godot::Dictionary result, model;
    result["selected"] = view.selected;
    result["active"] = view.active;
    result["can_begin"] = view.can_begin;
    result["completed"] = view.completed;
    result["note"] = godot::String{view.note.c_str()};
    result["body_name"] = godot::String{view.body_name.c_str()};
    result["acquired_ticks"] = static_cast<std::int64_t>(view.acquired_ticks);
    result["required_ticks"] =
        static_cast<std::int64_t>(kFreedomOrbitalSurveyTicks);
    if (view.result) {
      model["character"] =
          godot::String{terrain_character_name(view.result->character).data()};
      model["water_basis_points"] = view.result->water_basis_points;
    }
    result["result"] = model;
    return result;
  }
  static auto project_surface_marker(
      const SavedFlightWorld& selected,
      const std::array<LocalPositionMetres, 3>& axes) -> godot::Dictionary {
    const auto& session = selected.session;
    const auto marker = require(session.surface_marker_view());
    const auto& knowledge = session.knowledge();
    const bool supported =
        knowledge &&
        (knowledge->recipe.version == kFreedomObservedKnowledgeVersion ||
         knowledge->recipe.version == kFreedomTravelKnowledgeVersion ||
         freedom_knowledge_uses_network(knowledge->recipe.version));
    godot::Dictionary result;
    const auto version = knowledge && knowledge->surface_marker
                             ? knowledge->surface_marker->version
                             : kFreedomSurfaceMarkerVersion;
    result["version"] = static_cast<std::int64_t>(version);
    result["selected"] = marker.selected;
    result["present"] = marker.point.has_value();
    result["current_body"] = marker.current_body;
    result["can_mark"] = supported && session.surface_walker().has_value() &&
                         !session.recovery_pending();
    result["can_clear"] =
        marker.point.has_value() && !session.recovery_pending();
    result["system_name"] = godot::String{
        marker.known_system_name.value_or("Unresolved system").c_str()};
    const bool lunar = version == kFreedomMoonSurfaceMarkerVersion ||
                       version == kFreedomNetworkMoonSurfaceMarkerVersion;
    result[lunar ? "body_name" : "planet_name"] = godot::String{
        marker.known_body_name
            .value_or(lunar ? "Unresolved moon" : "Unresolved planet")
            .c_str()};
    if (marker.point) {
      const auto decimal = [](std::uint64_t value) {
        return godot::String{std::to_string(value).c_str()};
      };
      std::visit(
          [&](const auto& point) {
            result["system_id"] = decimal(point.system.value);
            result["tick"] = decimal(point.tick);
          },
          *marker.point);
      if (const auto* point =
              std::get_if<FreedomSurfaceMarkerPoint>(&*marker.point)) {
        result["planet_id"] = decimal(point->planet.value);
      } else {
        const auto& moon_point =
            std::get<FreedomMoonSurfaceMarkerPoint>(*marker.point);
        result["body_kind"] = "moon";
        result["body_version"] =
            static_cast<std::int64_t>(moon_point.body.version);
        result["body_id"] = decimal(moon_point.body.value);
      }
    }
    if (marker.current_body) {
      if (!marker.target_craft_relative_metres ||
          !marker.straight_line_distance_metres ||
          !std::isfinite(*marker.straight_line_distance_metres) ||
          *marker.straight_line_distance_metres < 0)
        throw std::runtime_error("Surface marker projection is unavailable");
      const auto& v = *marker.target_craft_relative_metres;
      const auto& a = axes;
      const godot::Vector3 position{
          static_cast<godot::real_t>(a[0].east * v.x + a[1].east * v.y +
                                     a[2].east * v.z),
          static_cast<godot::real_t>(a[0].up * v.x + a[1].up * v.y +
                                     a[2].up * v.z),
          static_cast<godot::real_t>(
              -(a[0].north * v.x + a[1].north * v.y + a[2].north * v.z))};
      if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
          !std::isfinite(position.z))
        throw std::runtime_error("Surface marker projection is nonfinite");
      result["position"] = position;
      result["distance_metres"] = *marker.straight_line_distance_metres;
    }
    return result;
  }

  static auto project_surface_landmark_sites(const auto& landmarks,
                                             const auto& view) -> godot::Array {
    const auto decimal = [](std::uint64_t value) {
      return godot::String{std::to_string(value).c_str()};
    };
    godot::Array sites;
    for (const auto& item : landmarks.sites) {
      const auto& identity = item.site.identity;
      const auto& v = item.target_craft_relative_metres;
      const auto& a = view.system_axes;
      const godot::Vector3 position{
          static_cast<godot::real_t>(a[0].east * v.x + a[1].east * v.y +
                                     a[2].east * v.z),
          static_cast<godot::real_t>(a[0].up * v.x + a[1].up * v.y +
                                     a[2].up * v.z),
          static_cast<godot::real_t>(
              -(a[0].north * v.x + a[1].north * v.y + a[2].north * v.z))};
      if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
          !std::isfinite(position.z))
        throw std::runtime_error("Exposure projection is non-finite");
      const char* label = "Pale exposure";
      const char* description =
          "Pale bands interrupt the darker surface. Composition unresolved.";
      switch (identity.kind) {
        case SurfaceLandmarkKind::pale_exposure: break;
        case SurfaceLandmarkKind::ochre_exposure:
          label = "Ochre exposure";
          description = "Ochre bands cross the surface. Colour alone does not "
                        "identify their composition.";
          break;
        case SurfaceLandmarkKind::dark_exposure:
          label = "Dark exposure";
          description = "Dark bands interrupt the surrounding ground. "
                        "Composition unresolved.";
          break;
      }
      godot::Dictionary site;
      site["id"] = decimal(identity.id.value);
      site["kind"] = static_cast<std::int64_t>(identity.kind);
      site["label"] = label;
      site["description"] = description;
      site["position"] = position;
      site["radius_metres"] = identity.radius_metres;
      site["tint"] =
          godot::Color{identity.tint.red / 255.0F, identity.tint.green / 255.0F,
                       identity.tint.blue / 255.0F, 1.0F};
      site["distance_metres"] = item.distance_metres;
      site["nearby"] = item.nearby;
      site["can_inspect"] = item.can_inspect;
      site["inspected"] = item.inspected;
      sites.append(site);
    }
    return sites;
  }

  static auto project_surface_landmarks(const SavedFlightWorld& selected,
                                        const SavedFlightProjection& view)
      -> godot::Dictionary {
    const auto landmarks = require(selected.session.surface_landmarks_view());
    const auto decimal = [](std::uint64_t value) {
      return godot::String{std::to_string(value).c_str()};
    };
    godot::Dictionary result;
    result["version"] = 1;
    result["selected"] = landmarks.selected;
    result["can_enable"] = landmarks.can_enable;
    result["system_id"] = decimal(selected.session.system().catalog.id.value);
    result["planet_id"] = decimal(view.planet.id.value);
    result["tick"] = decimal(selected.session.document().flight.tick);
    result["inspection_count"] =
        static_cast<std::int64_t>(landmarks.inspection_count);
    result["sites"] = project_surface_landmark_sites(landmarks, view);
    return result;
  }

  static auto project_moon_surface_landmarks(
      const SavedFlightWorld& selected, const SavedMoonFlightProjection& view)
      -> godot::Dictionary {
    const auto landmarks =
        require(selected.session.moon_surface_landmarks_view());
    const auto decimal = [](std::uint64_t value) {
      return godot::String{std::to_string(value).c_str()};
    };
    godot::Dictionary result;
    result["version"] = 2;
    result["selected"] = landmarks.selected;
    result["can_enable"] = landmarks.can_enable;
    result["system_id"] = decimal(selected.session.system().catalog.id.value);
    result["body_kind"] = "moon";
    result["body_version"] = 1;
    result["body_id"] = decimal(view.moon.body.value);
    result["tick"] = decimal(selected.session.document().flight.tick);
    result["inspection_count"] =
        static_cast<std::int64_t>(landmarks.inspection_count);
    result["sites"] = project_surface_landmark_sites(landmarks, view);
    return result;
  }

  static auto project_distant_contacts(const SavedFlightWorld& selected,
                                       const SavedFlightProjection& view)
      -> godot::Dictionary {
    const auto& session = selected.session;
    const auto& document = session.document();
    const auto& system = session.system();
    const auto decimal = [](std::uint64_t value) {
      return godot::String{std::to_string(value).c_str()};
    };
    godot::Dictionary result;
    result["version"] = 1;
    result["selected"] = document.model.companions == 1;
    result["system_id"] = decimal(system.catalog.id.value);
    result["tick"] = decimal(document.flight.tick);
    godot::Array contacts;
    if (document.model.companions == 1) {
      if (!selected.secondary_catalog ||
          selected.secondary_catalog->owner != system)
        selected.secondary_catalog =
            require(generate_secondary_body_catalog(system));
      auto observer = require(reframe_rigid_body(
          RigidBodyWorldContext{system}, require(session.relative_state()),
          {{RigidFrameKind::system_inertial, system.catalog.id, {}, {}},
           document.flight.tick}));
      if (session.surface_walker()) {
        const auto& foot = session.surface_walker()->foot_position_metres;
        const auto position = rotate_saved(
            view.rotation.geometry.fixed_to_system, {foot.x, foot.y, foot.z});
        const auto planet = require(resolve_planet_ephemeris(
            system, view.planet.id, {document.flight.tick, 0}));
        observer.position_metres = {planet.position.x + position.x,
                                    planet.position.y + position.y,
                                    planet.position.z + position.z};
      }
      if (session.walker()) {
        const auto station = require(resolve_origin_station_ephemeris(
            system, view.station, {document.flight.tick, 0}));
        const auto& foot = session.walker()->foot_position_metres;
        observer.position_metres = {station.position.x + foot.x,
                                    station.position.y + foot.y +
                                        kOriginWalkerEyeHeightMetres,
                                    station.position.z + foot.z};
      }
      const auto observations = require(observe_secondary_bodies(
          *selected.secondary_catalog,
          {observer.position_metres.x, observer.position_metres.y,
           observer.position_metres.z},
          {document.flight.tick, 0}));
      const auto native_direction = [&](RigidVector3 v) {
        const auto& a = view.system_axes;
        return godot::Vector3{
            static_cast<godot::real_t>(a[0].east * v.x + a[1].east * v.y +
                                       a[2].east * v.z),
            static_cast<godot::real_t>(a[0].up * v.x + a[1].up * v.y +
                                       a[2].up * v.z),
            static_cast<godot::real_t>(
                -(a[0].north * v.x + a[1].north * v.y + a[2].north * v.z))};
      };
      for (const auto& item : observations) {
        const char* status = "unresolved";
        const char* reason =
            "Below the optical resolution limit; direction annotation only.";
        switch (item.status) {
          case SecondaryObservationStatus::visible:
            status = "visible";
            reason = "Distant optical projection only; supported local trips "
                     "use separate drive controls.";
            break;
          case SecondaryObservationStatus::unresolved: break;
          case SecondaryObservationStatus::occluded:
            status = "occluded";
            reason = "The line of sight is blocked by another body.";
            break;
          case SecondaryObservationStatus::close_unavailable:
            status = "close_unavailable";
            reason = "Outside this optical projection's range; it supplies no "
                     "surface or collision. Supported local trips use separate "
                     "drive controls.";
            break;
        }
        godot::Dictionary contact;
        contact["id"] = decimal(item.id.value);
        contact["label"] = "Unidentified contact";
        contact["status"] = status;
        contact["direction"] = native_direction(item.direction);
        contact["angular_radius"] = item.angular_radius;
        contact["light_direction"] = native_direction(item.light_direction);
        contact["tint"] =
            godot::Color{item.tint.red / 255.0F, item.tint.green / 255.0F,
                         item.tint.blue / 255.0F, 1.0F};
        contact["spin_radians"] = item.spin_radians;
        contact["reason"] = reason;
        contacts.append(contact);
      }
    }
    result["contacts"] = contacts;
    return result;
  }

  static auto project_player_flight_state(const SavedFlightWorld& selected)
      -> godot::Dictionary {
    godot::Dictionary result;
    try {
      const auto& session = selected.session;
      if (session.moon()) return project_moon_flight_state(selected);
      if (session.minor()) return project_minor_flight_state(selected);
      const auto& document = session.document();
      const auto& body = document.flight;
      const auto relative = require(session.relative_state());
      const auto view = project_saved_flight(session);
      const auto observed = require(session.observe());
      const auto coordinates = [](const auto& value) {
        godot::PackedFloat64Array array;
        for (double v : {value.x, value.y, value.z})
          array.append(v);
        return array;
      };
      const auto local_vector = [](LocalPositionMetres value) {
        return godot::Vector3(value.east, value.up, -value.north);
      };
      const auto decimal = [](std::uint64_t v) {
        return godot::String{std::to_string(v).c_str()};
      };
      godot::Basis basis;
      godot::Basis station_basis;
      for (int i = 0; i < 3; ++i) {
        basis.set_column(
            i, local_vector(view.body_axes[static_cast<std::size_t>(i)]));
        station_basis.set_column(
            i, local_vector(view.system_axes[static_cast<std::size_t>(i)]));
      }
      result["mode"] = "freedom_flight";
      const auto boarding = project_boarding_state(selected);
      if (!boarding.is_empty()) result["boarding"] = boarding;
      result["universe_seed"] =
          decimal(document.origin.recipe.universe_seed.value);
      result["system_id"] = decimal(session.system().catalog.id.value);
      result["planet_id"] = decimal(view.planet.id.value);
      result["terrain_policy"] =
          static_cast<std::int64_t>(document.model.terrain_policy);
      result["surface_walk"] = project_surface_walk_actor(selected);
      result["orbital_survey"] = project_orbital_survey(selected.session);
      result["star_search"] = project_star_search(selected.session);
      result["surface_marker"] =
          project_surface_marker(selected, view.system_axes);
      result["surface_landmarks"] = project_surface_landmarks(selected, view);
      result["distant_contacts"] = project_distant_contacts(selected, view);
      result["station_available"] = view.station_available;
      result["station_id"] = view.station_available
                                 ? decimal(view.station.id.value)
                                 : godot::String{};
      result["craft_id"] = decimal(document.origin.state.craft.value);
      result["frame_id"] = decimal(body.craft.id.value);
      result["frame_version"] = static_cast<std::int64_t>(body.craft.version);
      result["tick"] = decimal(body.tick);
      result["checksum"] = decimal(require(rigid_body_state_checksum(
          RigidBodyWorldContext{session.system()}, body)));
      result["continued"] = session.source_save().has_value();
      // Existing view vectors remain selected-planet relative observations.
      result["position_metres"] = coordinates(relative.position_metres);
      result["velocity_metres_per_second"] =
          coordinates(relative.linear_velocity_metres_per_second);
      if (document.model.frame_policy == kFreedomReleaseSystemFramePolicy) {
        result["frame_policy"] =
            static_cast<std::int64_t>(document.model.frame_policy);
        result["coordinate_frame"] =
            body.frame.kind == RigidFrameKind::system_inertial
                ? "system_inertial"
                : "planet_relative_inertial";
        result["observation_frame"] = "planet_relative_inertial";
        result["canonical_position_metres"] = coordinates(body.position_metres);
        result["canonical_velocity_metres_per_second"] =
            coordinates(body.linear_velocity_metres_per_second);
      }
      godot::PackedFloat64Array orientation;
      for (double v : {body.orientation.w, body.orientation.x,
                       body.orientation.y, body.orientation.z})
        orientation.append(v);
      result["orientation_wxyz"] = orientation;
      result["angular_velocity_body"] =
          coordinates(body.angular_velocity_radians_per_second);
      result["body_basis"] = basis;
      result["station_position"] = local_vector(view.station_position);
      result["station_basis"] = station_basis;
      result["latitude"] = view.pose.latitude_radians;
      result["longitude"] = view.pose.longitude_radians;
      result["altitude"] = view.pose.altitude_metres;
      result["planet_radius"] = view.planet.radius.value * 1000.0;
      result["atmosphere_edge"] =
          observed.atmosphere.space_boundary_altitude_metres;
      result["air_density"] = observed.atmosphere.density_kg_per_cubic_metre;
      result["dynamic_pressure"] = observed.atmosphere.dynamic_pressure_pascals;
      result["inertial_speed"] =
          observed.orbit.inertial_speed_metres_per_second;
      result["surface_speed"] =
          observed.orbit.surface_relative_speed_metres_per_second;
      result["radial_rate"] = observed.orbit.radial_rate_metres_per_second;
      switch (observed.orbit.classification) {
        case OrbitClassification::stable:
          result["orbit_classification"] = "stable";
          break;
        case OrbitClassification::decaying:
          result["orbit_classification"] = "decaying";
          break;
        case OrbitClassification::impact:
          result["orbit_classification"] = "impact";
          break;
        case OrbitClassification::escape:
          result["orbit_classification"] = "escape";
          break;
      }
      result["orbit_bound"] = observed.orbit.bound;
      result["orbit_near_parabolic"] = observed.orbit.near_parabolic;
      result["periapsis_radius"] = observed.orbit.periapsis_radius_metres;
      result["apoapsis_radius"] =
          observed.orbit.apoapsis_radius_metres
              ? godot::Variant{*observed.orbit.apoapsis_radius_metres}
              : godot::Variant{};
      result["assistance"] = document.model.assistance;
      result["hold_enabled"] = document.model.hold.target.has_value();
      godot::Dictionary hold;
      const auto current_hold = session.current_orbit_hold_target();
      hold["available"] = current_hold.has_value();
      hold["refusal"] = current_hold
                            ? godot::String{}
                            : godot::String{current_hold.error().c_str()};
      if (document.model.hold.target) {
        const auto& target = *document.model.hold.target;
        hold["planet_id"] = decimal(target.planet.value);
        hold["radius_metres"] = target.radius_metres;
        hold["plane_normal"] = coordinates(target.plane_normal);
      }
      result["orbit_hold"] = hold;
      result["dropped_seconds"] = selected.dropped_seconds;
      result["attached"] = session.docking() && session.docking()->attached;
      result["target_port"] =
          session.docking()
              ? static_cast<std::int64_t>(session.docking()->target.ordinal)
              : std::int64_t{0};
      godot::Dictionary docking;
      if (session.docking()) {
        const auto assessed = require(session.assess_port());
        docking["ready"] =
            assessed.decision == OriginDockDecision::capture_ready;
        docking["reason"] =
            godot::String{origin_dock_decision_text(assessed.decision).data()};
        docking["separation"] = assessed.separation_metres;
        docking["alignment_radians"] = assessed.alignment_radians;
        docking["inward_speed"] = assessed.inward_speed_metres_per_second;
        docking["lateral_speed"] = assessed.lateral_speed_metres_per_second;
        docking["angular_speed"] = assessed.angular_speed_radians_per_second;
        docking["hull_inside"] = assessed.hull_inside_reservation;
        const auto geometry = require(origin_station_geometry(view.station));
        const auto& collar =
            geometry.ports[session.docking()->target.ordinal - 1]
                .collar_position_metres;
        const auto offset =
            rotate_saved(inverse_saved(body.orientation),
                         {collar.x - assessed.collar_station_metres.x,
                          collar.y - assessed.collar_station_metres.y,
                          collar.z - assessed.collar_station_metres.z});
        docking["offset_body_metres"] = coordinates(offset);
      }
      result["docking"] = docking;
      const auto docking_available = session.port_docking_available();
      const auto& docking_aid = session.port_docking();
      godot::Dictionary smooth_docking;
      smooth_docking["available"] = docking_available.has_value();
      smooth_docking["refusal"] =
          docking_available ? godot::String{}
                            : godot::String{docking_available.error().c_str()};
      smooth_docking["active"] = docking_aid.active;
      smooth_docking["remaining_seconds"] = docking_aid.remaining_ticks / 120.0;
      smooth_docking["note"] = godot::String{docking_aid.note.c_str()};
      smooth_docking["phase"] = godot::String{
          docking_aid.phase == NativePortDockingPhase::aligning   ? "aligning"
          : docking_aid.phase == NativePortDockingPhase::closing  ? "closing"
          : docking_aid.phase == NativePortDockingPhase::complete ? "complete"
                                                                  : "off"};
      result["port_docking"] = smooth_docking;
      const auto available = session.port_approach_available();
      godot::Dictionary approach;
      approach["available"] = available.has_value();
      approach["refusal"] = available
                                ? godot::String{}
                                : godot::String{available.error().c_str()};
      approach["active"] = session.port_approach().active;
      approach["remaining_seconds"] =
          session.port_approach().remaining_ticks / 120.0;
      approach["note"] = godot::String{session.port_approach().note.c_str()};
      result["port_approach"] = approach;
      const auto rendezvous_available = session.port_rendezvous_available();
      godot::Dictionary rendezvous;
      rendezvous["available"] = rendezvous_available.has_value();
      rendezvous["refusal"] =
          rendezvous_available
              ? godot::String{}
              : godot::String{rendezvous_available.error().c_str()};
      rendezvous["active"] = session.port_rendezvous().active;
      rendezvous["remaining_seconds"] =
          session.port_rendezvous().remaining_ticks / 120.0;
      rendezvous["note"] =
          godot::String{session.port_rendezvous().note.c_str()};
      result["port_rendezvous"] = rendezvous;
      godot::Dictionary surface;
      surface["gear_deployed"] =
          session.surface() && session.surface()->gear_deployed;
      surface["landed"] =
          session.surface() && session.surface()->landed.has_value();
      const auto& maneuver = session.surface_maneuver();
      surface["maneuver"] =
          maneuver.kind == NativeSurfaceManeuverKind::landing   ? "landing"
          : maneuver.kind == NativeSurfaceManeuverKind::liftoff ? "liftoff"
                                                                : "off";
      surface["remaining_seconds"] = maneuver.remaining_ticks / 120.0;
      surface["note"] = godot::String{maneuver.note.c_str()};
      godot::Dictionary clearance;
      clearance["available"] = false;
      if (const auto sample = session.last_gear_clearance()) {
        clearance["available"] = true;
        clearance["reference"] = "deployed_pad_planes";
        clearance["coordinate_frame"] = "planet_relative_inertial";
        clearance["system_id"] = decimal(sample->frame.system.value);
        clearance["planet_id"] = decimal(sample->frame.planet->value);
        clearance["craft_id"] = decimal(document.origin.state.craft.value);
        clearance["frame_id"] = decimal(sample->craft.id.value);
        clearance["frame_version"] =
            static_cast<std::int64_t>(sample->craft.version);
        clearance["terrain_policy"] =
            static_cast<std::int64_t>(sample->terrain_policy);
        clearance["tick"] = decimal(sample->tick);
        clearance["source_checksum"] = decimal(sample->source_checksum);
        clearance["age_seconds"] =
            static_cast<double>(body.tick - sample->tick) / 120.0;
        clearance["minimum_gap_metres"] = sample->minimum_gap_metres;
        clearance["maximum_gap_metres"] = sample->maximum_gap_metres;
      }
      surface["gear_clearance"] = clearance;
      result["surface"] = surface;
      godot::Dictionary impact;
      impact["selected"] = session.condition().has_value();
      if (session.condition()) {
        const auto& condition = *session.condition();
        impact["policy_version"] =
            static_cast<std::int64_t>(kImpactConsequencesVersion);
        impact["version"] = static_cast<std::int64_t>(condition.version);
        impact["craft_id"] = decimal(condition.craft.value);
        impact["frame_id"] = decimal(condition.frame.id.value);
        impact["frame_version"] =
            static_cast<std::int64_t>(condition.frame.version);
        impact["tick"] = decimal(condition.tick);
        impact["forebody"] = static_cast<std::int64_t>(condition.forebody);
        impact["landing_structure"] =
            static_cast<std::int64_t>(condition.landing_structure);
        impact["cracked"] = condition_view_cracked(condition);
        impact["visibility_degraded"] =
            condition_visibility_degraded(condition);
        impact["landing_available"] = condition_allows_landing(condition);
        impact["pending_loss"] = session.recovery_pending();
        godot::Dictionary latest;
        if (condition.latest) {
          const auto& receipt = *condition.latest;
          const auto& witness = receipt.witness;
          latest["serial"] = decimal(receipt.serial);
          latest["tick"] = decimal(witness.tick);
          latest["source_condition_digest"] =
              decimal(receipt.source_condition_digest);
          latest["source_checksum"] = decimal(witness.source_checksum);
          latest["candidate_checksum"] = decimal(witness.candidate_checksum);
          latest["system_id"] = decimal(witness.system.value);
          latest["planet_id"] = decimal(witness.planet.value);
          latest["region"] = witness.region == CraftImpactRegion::forebody
                                 ? "forebody"
                                 : "landing_structure";
          latest["closing_speed"] = witness.closing_speed_metres_per_second;
          latest["fraction"] = witness.fraction;
          latest["contact_point_craft"] =
              coordinates(witness.contact_point_craft_metres);
          latest["damage_class"] =
              static_cast<std::int64_t>(receipt.damage_class);
          latest["catastrophic"] = receipt.catastrophic;
        }
        impact["latest"] = latest;
      }
      result["impact"] = impact;
      const auto impact_available = session.impact_consequences_available();
      godot::Dictionary impact_opt_in;
      impact_opt_in["available"] = impact_available.has_value();
      impact_opt_in["reason"] =
          impact_available ? godot::String{}
                           : godot::String{impact_available.error().c_str()};
      result["impact_opt_in"] = impact_opt_in;
      godot::Dictionary resources;
      resources["selected"] = session.resources().has_value();
      if (session.resources()) {
        const auto reading = require(freedom_resource_reading(
            *session.resources(),
            selected.last_step ? selected.last_step->fuel_debit_quanta : 0,
            selected.last_step && selected.last_step->propulsion_refused));
        resources["quantity_quanta"] = decimal(reading.quantity_quanta);
        resources["capacity_quanta"] = decimal(reading.capacity_quanta);
        resources["last_tick_quanta"] = decimal(reading.last_tick_quanta);
        resources["fraction"] = reading.fraction;
        resources["main_equivalent_seconds"] = reading.main_equivalent_seconds;
        resources["equivalent_newtons"] = reading.equivalent_newtons;
        resources["jump_charges"] =
            static_cast<std::int64_t>(reading.jump_charges);
        resources["reserve"] = reading.reserve;
        resources["operationally_empty"] = reading.operationally_empty;
        resources["propulsion_refused"] = reading.propulsion_refused;
      }
      result["resources"] = resources;
      godot::Dictionary chart;
      chart["selected"] = session.knowledge().has_value();
      if (session.knowledge()) {
        const auto& ledger = *session.knowledge();
        std::uint32_t observed_facts{};
        std::uint32_t observation_transitions{};
        SimulationTick last_observation{};
        for (const auto& entry : ledger.entries) {
          for (const auto& transition : entry.transitions)
            if (transition.source != KnowledgeSource::starting_chart)
              ++observation_transitions;
          const auto& evidence = entry.transitions.back();
          if (evidence.source != KnowledgeSource::starting_chart) {
            ++observed_facts;
            last_observation = std::max(last_observation, evidence.tick);
          }
        }
        chart["local_sensors"] = ledger.recipe.observation_policy == 1;
        chart["observed_facts"] = observed_facts;
        chart["observation_transitions"] = observation_transitions;
        chart["last_observation_tick"] = decimal(last_observation);
        const auto view = require(resolve_freedom_knowledge_chart(
            ledger, session.system().catalog.id, *session.resources(),
            !session.travel() ||
                session.travel()->phase == FreedomJumpPhase::idle,
            &document.origin));
        godot::Array rows;
        for (const auto& row : view.destinations) {
          const auto selection = session.jump_selection_available(row.system);
          godot::Dictionary item;
          item["system_id"] =
              godot::String{system_id_string(row.system).c_str()};
          const auto identity = require(query_freedom_knowledge(
              ledger,
              {KnowledgeSubjectKind::system, row.system, row.system.value},
              KnowledgeFact::identity_type, document.flight.tick));
          if (!identity ||
              !std::holds_alternative<KnownIdentity>(identity->value))
            throw std::runtime_error("Chart identity is unavailable");
          item["name"] = godot::String{
              std::get<KnownIdentity>(identity->value).name.c_str()};
          item["confidence"] = godot::String{
              navigation_knowledge_level_name(row.knowledge).data()};
          item["current"] = row.system == view.current_system;
          item["known"] = row.known;
          item["valid"] = row.valid;
          item["authorized"] =
              row.authorized && session.starting_assembly().has_value();
          item["affordable"] = row.affordable;
          item["available"] = row.available && selection.has_value();
          item["selectable"] = row.selectable && selection.has_value();
          item["disabled_reason"] =
              row.selectable && !selection
                  ? godot::String{selection.error().c_str()}
                  : godot::String{
                        navigation_disabled_reason_name(row.disabled_reason)
                            .data()};
          if (row.distance_metres)
            item["distance_light_seconds"] =
                decimal(*row.distance_metres / kMetresPerLightSecond);
          rows.append(item);
        }
        chart["rows"] = rows;
        chart["baseline"] = "Starting chart";
      }
      result["chart"] = chart;
      result["jump"] = project_saved_jump(session, body.tick);

      const auto force =
          selected.last_step &&
                  !(session.docking() && session.docking()->attached) &&
                  !(session.surface() && session.surface()->landed)
              ? selected.last_step->actuation.central.propulsion
              : VacuumActuation{};
      result["positive_force_body"] = coordinates(force.positive_force_newtons);
      result["negative_force_body"] = coordinates(force.negative_force_newtons);
      result["positive_torque_body"] =
          coordinates(force.positive_torque_newton_metres);
      result["negative_torque_body"] =
          coordinates(force.negative_torque_newton_metres);
      result["applied_force_body"] = coordinates(force.applied_force_newtons);
      const auto frame = require(resolve_craft_frame(body.craft));
      godot::PackedFloat64Array positive_ratings, negative_ratings;
      for (const auto v : frame.properties.positive_force_newtons)
        positive_ratings.append(v);
      for (const auto v : frame.properties.negative_force_newtons)
        negative_ratings.append(v);
      if (const auto margin = rated_rise_margin(session, view))
        result["rated_rise_margin"] = *margin;
      result["positive_force_ratings"] = positive_ratings;
      result["negative_force_ratings"] = negative_ratings;
      const auto& star = view.rotation.geometry.observer_to_star_fixed;
      const auto& tangent = view.tangent;
      result["star_direction"] = godot::Vector3(
          star.x * tangent.east.x + star.y * tangent.east.y +
              star.z * tangent.east.z,
          star.x * tangent.up.x + star.y * tangent.up.y + star.z * tangent.up.z,
          -(star.x * tangent.north.x + star.y * tangent.north.y +
            star.z * tangent.north.z));
      const auto profile =
          require(resolve_atmospheric_flight_profile(view.planet));
      const auto& source_star = session.system().catalog.star;
      const auto& atmosphere = view.planet.palette.atmosphere;
      godot::Dictionary lighting;
      lighting["version"] = static_cast<std::int64_t>(1);
      lighting["probe_frame"] = "native_tangent_metres";
      lighting["system_id"] = result["system_id"];
      const auto target = body_id(view.planet.id);
      lighting["body_version"] = static_cast<std::int64_t>(target.version);
      lighting["body_kind"] = "planet";
      lighting["body_id"] = decimal(target.value);
      lighting["tick"] = result["tick"];
      lighting["catalog_generator"] = static_cast<std::int64_t>(
          view.rotation.recipe.physical_catalog_generator);
      lighting["star_id"] = decimal(source_star.id.value);
      lighting["direction"] = result["star_direction"];
      // system_axes already include the actual saved frame and body rotation.
      // station_basis is system-to-native, not the station's orbital attitude.
      lighting["native_to_system"] = station_basis.transposed();
      lighting["star_color"] = godot::Color(source_star.color.red / 255.0f,
                                            source_star.color.green / 255.0f,
                                            source_star.color.blue / 255.0f, 1);
      lighting["star_angular_radius"] = std::asin(std::min(
          1.0, static_cast<double>(source_star.radius_kilometres) * 1000.0 /
                   view.rotation.geometry.observer_star_distance_metres));
      lighting["atmosphere_tint"] =
          godot::Color(atmosphere.red / 255.0f, atmosphere.green / 255.0f,
                       atmosphere.blue / 255.0f, 1);
      lighting["scale_height"] = profile.scale_height_metres;
      lighting["sea_density"] = profile.sea_level_density_kg_per_cubic_metre;
      lighting["atmosphere_edge"] = profile.space_boundary_altitude_metres;
      result["lighting"] = lighting;
      // Visible material response only: no palette identity, climate envelope,
      // seed or physical ice/water classification is disclosed by this packet.
      const auto appearance =
          require(resolve_planet_surface_appearance(view.planet));
      godot::Dictionary surface_appearance;
      surface_appearance["version"] =
          static_cast<std::int64_t>(appearance.version);
      surface_appearance["disclosure"] = std::int64_t{1};
      surface_appearance["system_id"] = result["system_id"];
      surface_appearance["planet_id"] = result["planet_id"];
      surface_appearance["albedo_scale_basis_points"] =
          static_cast<std::int64_t>(appearance.albedo_scale_basis_points);
      surface_appearance["detail_contrast_basis_points"] =
          static_cast<std::int64_t>(appearance.detail_contrast_basis_points);
      surface_appearance["roughness_basis_points"] =
          static_cast<std::int64_t>(appearance.roughness_basis_points);
      surface_appearance["specular_basis_points"] =
          static_cast<std::int64_t>(appearance.specular_basis_points);
      result["surface_appearance"] = surface_appearance;
      if (document.model.optics) {
        const auto recipe = require(generate_planet_optics_recipe(
            session.system(), view.planet.id, document.model.optics));
        if (!selected.optical_field || selected.optical_field->recipe != recipe)
          selected.optical_field =
              require(resolve_planet_optics(session.system(), recipe));
        const auto& field = *selected.optical_field;
        godot::PackedByteArray density;
        density.resize(static_cast<std::int64_t>(field.density.size()));
        std::copy(field.density.begin(), field.density.end(), density.ptrw());
        godot::Basis fixed_to_tangent;
        fixed_to_tangent.set_column(0, godot::Vector3(view.tangent.east.x,
                                                      view.tangent.up.x,
                                                      -view.tangent.north.x));
        fixed_to_tangent.set_column(1, godot::Vector3(view.tangent.east.y,
                                                      view.tangent.up.y,
                                                      -view.tangent.north.y));
        fixed_to_tangent.set_column(2, godot::Vector3(view.tangent.east.z,
                                                      view.tangent.up.z,
                                                      -view.tangent.north.z));
        godot::Dictionary optics;
        optics["version"] = static_cast<std::int64_t>(kPlanetOpticsVersion);
        optics["disclosure"] = static_cast<std::int64_t>(1);
        optics["system_id"] = result["system_id"];
        optics["planet_id"] = result["planet_id"];
        optics["width"] = static_cast<std::int64_t>(kPlanetOpticsWidth);
        optics["height"] = static_cast<std::int64_t>(kPlanetOpticsHeight);
        optics["density"] = density;
        optics["reference_radius"] = view.planet.radius.value * 1000.0;
        optics["planet_center"] = godot::Vector3(
            0, -(view.planet.radius.value * 1000.0 + view.pose.altitude_metres),
            0);
        optics["fixed_to_tangent"] = fixed_to_tangent;
        optics["lower_height_metres"] = field.lower_height_metres;
        optics["upper_height_metres"] = field.upper_height_metres;
        optics["opacity_cap"] = field.opacity_cap_basis_points / 10000.0;
        optics["tint"] =
            godot::Color(field.tint.red / 255.0f, field.tint.green / 255.0f,
                         field.tint.blue / 255.0f, 1);
        result["optics"] = optics;
      }

    } catch (const std::exception&) {
      result.clear();
    }
    return result;
  }

  static auto service_owner(const NativeFreedomFlightSession& session)
      -> const OriginServiceCraftState& {
    return *std::visit(
        [](const auto& value) -> const OriginServiceCraftState* {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, OriginServiceCraftState>)
            return &value;
          else if constexpr (std::is_same_v<T, OriginServiceSortie>)
            return &value.service;
          else
            return &value.last.service;
        },
        *session.origin_assistance());
  }
  static auto project_origin_service_status(
      const NativeFreedomFlightSession& session, bool assess_request = false)
      -> godot::Dictionary {
    godot::Dictionary result;
    result["selected"] = session.origin_assistance().has_value();
    if (!session.origin_assistance()) return result;
    const auto& service = service_owner(session);
    const auto decimal = [](std::uint64_t value) {
      return godot::String{std::to_string(value).c_str()};
    };
    result["craft_id"] = decimal(service.descriptor.id.value);
    result["reserved_port"] =
        static_cast<std::int64_t>(service.descriptor.home_port.ordinal);
    result["cargo_quanta"] = decimal(service.resources.cargo_quanta);
    result["delivered_quanta"] =
        decimal(service.delivery ? service.delivery->delivered_quanta : 0);
    result["jump_charges"] =
        static_cast<std::int64_t>(service.resources.jump_charges);
    result["cargo_fraction"] =
        static_cast<double>(service.resources.cargo_quanta) /
        static_cast<double>(service.descriptor.cargo_capacity_quanta);
    result["phase"] = "Stationed at D2";
    result["cancel_available"] = false;
    if (const auto* sortie =
            std::get_if<OriginServiceSortie>(&*session.origin_assistance())) {
      constexpr std::array names{
          "Request accepted",        "Leaving Origin",
          "Jumping to beacon",       "In transit to beacon",
          "Approaching your ship",   "Delivering fuel",
          "Withdrawing from beacon", "Jumping home",
          "In transit home",         "Approaching Origin",
          "Returned to D2",          "Canceled at D2"};
      result["phase"] = names.at(static_cast<std::size_t>(sortie->phase));
      result["cancel_available"] =
          !sortie->return_requested &&
          sortie->phase != OriginServiceSortiePhase::completed &&
          sortie->phase != OriginServiceSortiePhase::canceled;
      if (sortie->return_requested && !service.attached)
        result["phase"] = "Returning · request canceled";
    }
    if (const auto* unavailable = std::get_if<OriginServiceUnavailable>(
            &*session.origin_assistance())) {
      result["phase"] = "Service unavailable · last contact " +
                        decimal(unavailable->last.service.body.tick);
      result["cancel_available"] = false;
    }
    result["request_available"] = false;
    result["refusal"] = "";
    if (assess_request) {
      const auto available = session.origin_assistance_available();
      result["request_available"] = available.has_value();
      if (!available)
        result["refusal"] = godot::String{available.error().c_str()};
    }
    return result;
  }
  auto get_freedom_origin_assistance_state() const -> godot::Dictionary {
    return saved_flight
               ? project_origin_service_status(saved_flight->session, true)
               : godot::Dictionary{};
  }
  static auto project_derelict(const NativeFreedomFlightSession& session)
      -> godot::Dictionary {
    const auto view = session.derelict_view();
    godot::Dictionary packet;
    packet["version"] = std::int64_t{1};
    packet["selected"] = view.selected;
    packet["known"] = view.known;
    packet["local"] = view.local;
    packet["inspected"] = view.inspected;
    packet["can_scan"] = view.can_scan;
    packet["can_inspect"] = view.can_inspect;
    packet["note"] = godot::String{view.note.c_str()};
    packet["history"] = godot::String{view.history.c_str()};
    packet["id"] = view.known ? godot::String{std::to_string(view.id).c_str()}
                              : godot::String{};
    packet["position"] = godot::Vector3{};
    packet["basis"] = godot::Basis{};
    packet["distance_metres"] = view.distance_metres;
    packet["relative_speed_metres_per_second"] =
        view.relative_speed_metres_per_second;
    if (!view.local || !view.hull) return packet;
    const auto player = require(session.relative_state());
    const auto axes = project_saved_flight(session).system_axes;
    const auto local = [&](RigidVector3 value) {
      return godot::Vector3{
          static_cast<godot::real_t>(axes[0].east * value.x +
                                     axes[1].east * value.y +
                                     axes[2].east * value.z),
          static_cast<godot::real_t>(axes[0].up * value.x +
                                     axes[1].up * value.y +
                                     axes[2].up * value.z),
          static_cast<godot::real_t>(-(axes[0].north * value.x +
                                       axes[1].north * value.y +
                                       axes[2].north * value.z))};
    };
    packet["position"] =
        local({view.hull->position_metres.x - player.position_metres.x,
               view.hull->position_metres.y - player.position_metres.y,
               view.hull->position_metres.z - player.position_metres.z});
    godot::Basis basis;
    for (int i = 0; i < 3; ++i) {
      const auto direction = rotate_saved(
          view.hull->orientation,
          {i == 0 ? 1.0 : 0.0, i == 1 ? 1.0 : 0.0, i == 2 ? 1.0 : 0.0});
      basis.set_column(i, local({direction.x, direction.y, direction.z}));
    }
    packet["basis"] = basis;
    return packet;
  }
  static auto project_origin_service_craft(
      const NativeFreedomFlightSession& session) -> godot::Dictionary {
    godot::Dictionary packet;
    if (!session.origin_assistance()) return packet;
    const auto* world = session.origin_assistance_world();
    if (!world) throw std::invalid_argument("Missing service world context");
    const auto& service = service_owner(session);
    packet["version"] = std::int64_t{1};
    packet["craft_id"] =
        godot::String{std::to_string(service.descriptor.id.value).c_str()};
    packet["visible"] = false;
    packet["position"] = godot::Vector3{};
    packet["basis"] = godot::Basis{};
    const auto coordinates = [](RigidVector3 value) {
      godot::PackedFloat64Array result;
      for (const auto v : {value.x, value.y, value.z})
        result.append(v);
      return result;
    };
    godot::Dictionary force;
    force["mode"] = "freedom_flight";
    force["frame_id"] = "2";
    force["attached"] = service.attached;
    const auto& applied = session.origin_assistance_propulsion();
    force["positive_force_body"] = coordinates(applied.positive_force_newtons);
    force["negative_force_body"] = coordinates(applied.negative_force_newtons);
    const auto frame = require(resolve_craft_frame(service.body.craft));
    godot::PackedFloat64Array positive, negative;
    for (const auto value : frame.properties.positive_force_newtons)
      positive.append(value);
    for (const auto value : frame.properties.negative_force_newtons)
      negative.append(value);
    force["positive_force_ratings"] = positive;
    force["negative_force_ratings"] = negative;
    packet["propulsion"] = force;
    if (std::holds_alternative<OriginServiceUnavailable>(
            *session.origin_assistance()))
      return packet;
    if (const auto* sortie =
            std::get_if<OriginServiceSortie>(&*session.origin_assistance());
        sortie && (sortie->phase == OriginServiceSortiePhase::outward_transit ||
                   sortie->phase == OriginServiceSortiePhase::return_transit))
      return packet;
    if (session.minor() ||
        session.system().catalog.id != world->origin().catalog.id)
      return packet;
    const bool service_on_moon =
        service.body.frame.kind == RigidFrameKind::body_relative_inertial;
    if (service_on_moon != session.moon().has_value()) return packet;
    const auto player = require(session.relative_state());
    std::array<LocalPositionMetres, 3> axes;
    RigidBodyState observed = service.body;
    if (session.moon()) {
      if (session.moon()->body != world->moon().body ||
          observed.frame != player.frame)
        return packet;
      axes = project_saved_moon_flight(session).system_axes;
    } else {
      const auto station =
          generate_origin_station(service.descriptor.universe_seed);
      if (player.frame.planet != station.orbit.host_planet) return packet;
      observed = require(
          reframe_rigid_body(RigidBodyWorldContext{world->origin(), &station},
                             service.body, {player.frame, player.tick}));
      axes = project_saved_flight(session).system_axes;
    }
    const auto local = [&](RigidVector3 value) {
      return godot::Vector3{
          static_cast<godot::real_t>(axes[0].east * value.x +
                                     axes[1].east * value.y +
                                     axes[2].east * value.z),
          static_cast<godot::real_t>(axes[0].up * value.x +
                                     axes[1].up * value.y +
                                     axes[2].up * value.z),
          static_cast<godot::real_t>(-(axes[0].north * value.x +
                                       axes[1].north * value.y +
                                       axes[2].north * value.z))};
    };
    packet["position"] =
        local({observed.position_metres.x - player.position_metres.x,
               observed.position_metres.y - player.position_metres.y,
               observed.position_metres.z - player.position_metres.z});
    godot::Basis basis;
    for (int i = 0; i < 3; ++i) {
      const auto direction = rotate_saved(
          observed.orientation,
          {i == 0 ? 1.0 : 0.0, i == 1 ? 1.0 : 0.0, i == 2 ? 1.0 : 0.0});
      basis.set_column(i, local({direction.x, direction.y, direction.z}));
    }
    packet["basis"] = basis;
    packet["visible"] = true;
    return packet;
  }
  static auto project_flight_state(const SavedFlightWorld& selected)
      -> godot::Dictionary {
    auto result = project_player_flight_state(selected);
    if (result.is_empty()) return result;
    try {
      result["derelict"] = project_derelict(selected.session);
      if (selected.session.origin_assistance()) {
        auto status = project_origin_service_status(selected.session);
        status["craft"] = project_origin_service_craft(selected.session);
        result["origin_service"] = status;
      }
    } catch (const std::exception&) {
      result.clear();
    }
    return result;
  }

  auto get_freedom_flight_state() const -> godot::Dictionary {
    return saved_flight ? project_flight_state(*saved_flight)
                        : godot::Dictionary{};
  }

  // Lazy recorded-knowledge projection: presentation neither discovers facts
  // nor bypasses confidence by reading the generated catalog directly.
  auto get_freedom_observations() const -> godot::Dictionary {
    godot::Dictionary result;
    result["selected"] = false;
    result["tick"] = "0";
    result["subjects"] = godot::Array{};
    const auto decimal = [](auto value) {
      return godot::String{std::to_string(value).c_str()};
    };
    try {
      if (!saved_flight || !saved_flight->session.knowledge())
        throw std::runtime_error("No selected recorded observations");
      const auto& session = saved_flight->session;
      const auto now = session.document().flight.tick;
      result["tick"] = decimal(now);
      const auto& ledger = *session.knowledge();
      const auto known = known_freedom_subjects(ledger, now);
      if (!known)
        throw std::runtime_error("Recorded observation subjects unavailable");
      constexpr std::array facts{
          KnowledgeFact::identity_type, KnowledgeFact::location,
          KnowledgeFact::physical,      KnowledgeFact::atmosphere,
          KnowledgeFact::hazards,       KnowledgeFact::landing,
          KnowledgeFact::station_ports, KnowledgeFact::presence};
      constexpr std::array fact_names{"identity_type", "location", "physical",
                                      "atmosphere",    "hazards",  "landing",
                                      "station_ports", "presence"};
      constexpr std::array source_names{"starting_chart", "local_ship",
                                        "physical_arrival"};
      constexpr std::array kind_names{"system", "planet", "station"};
      godot::Array subjects;
      for (const auto& subject : *known) {
        godot::Dictionary row;
        const auto kind = static_cast<std::size_t>(subject.kind);
        if (kind >= kind_names.size())
          throw std::runtime_error("Unsupported recorded subject kind");
        row["kind"] = kind_names[kind];
        row["system_id"] =
            godot::String{system_id_string(subject.system).c_str()};
        row["subject_id"] = decimal(subject.identity);
        row["name"] = godot::String{
            (std::string{"Unresolved "} + kind_names[kind]).c_str()};
        godot::Array readings;
        for (std::size_t i = 0; i < facts.size(); ++i) {
          const auto query =
              query_freedom_knowledge(ledger, subject, facts[i], now);
          if (!query)
            throw std::runtime_error("Recorded observation fact unavailable");
          if (!*query) continue;
          const auto& reading = **query;
          const auto& provenance = reading.provenance;
          const auto source = static_cast<std::size_t>(provenance.source);
          if (source >= source_names.size())
            throw std::runtime_error("Unsupported recorded observation source");
          godot::Dictionary value;
          std::visit(
              [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, KnownIdentity>) {
                  value["name"] = godot::String{v.name.c_str()};
                  row["name"] = value["name"];
                } else if constexpr (std::is_same_v<T,
                                                    UniversePositionMetres>) {
                  value["x_metres"] = decimal(v.x);
                  value["y_metres"] = decimal(v.y);
                  value["z_metres"] = decimal(v.z);
                } else if constexpr (std::is_same_v<T, KnownOrbit>) {
                  value["radius_kilometres"] = decimal(v.radius_kilometres);
                  value["period_ticks"] = decimal(v.period_ticks);
                  value["epoch_phase_turns"] = v.epoch_phase_turns;
                  value["inclination_microdegrees"] =
                      v.inclination_microdegrees;
                  value["ascending_node_turns"] = v.ascending_node_turns;
                  if (v.host_planet)
                    value["host_planet"] = decimal(v.host_planet->value);
                } else if constexpr (std::is_same_v<T, KnownPhysical>) {
                  value["radius_kilometres"] = v.radius_kilometres;
                  value["gravity_milli_g"] = v.gravity_milli_g;
                } else if constexpr (std::is_same_v<T, KnownAtmosphere>) {
                  value["class"] =
                      godot::String{atmosphere_class_name(v.kind).data()};
                  value["pressure_millibars"] = v.pressure_millibars;
                } else if constexpr (std::is_same_v<T, KnownHazards>) {
                  value["min_temperature_mk"] = v.min_temperature_mk;
                  value["max_temperature_mk"] = v.max_temperature_mk;
                  value["radiation_nsv_per_hour"] = v.radiation_nsv_per_hour;
                  value["electric_field_v_per_m"] = v.electric_field_v_per_m;
                  value["gust_mm_per_second"] = v.gust_mm_per_second;
                  value["ground_acceleration_mm_per_second2"] =
                      v.ground_acceleration_mm_per_second2;
                  value["visibility"] = godot::String{
                      atmospheric_visibility_name(v.visibility).data()};
                  value["maximum_obscuration_basis_points"] =
                      v.maximum_obscuration_basis_points;
                } else if constexpr (std::is_same_v<T, KnownLanding>) {
                  value["character"] =
                      godot::String{terrain_character_name(v.character).data()};
                  value["water_basis_points"] = v.water_basis_points;
                } else if constexpr (std::is_same_v<T, KnownStationPorts>) {
                  value["count"] = v.count;
                } else if constexpr (std::is_same_v<T, KnownPresence>) {
                  value["arrival_tick"] = decimal(v.arrival_tick);
                }
              },
              reading.value);
          godot::Dictionary fact;
          fact["fact"] = fact_names[i];
          fact["confidence"] = godot::String{
              navigation_knowledge_level_name(provenance.level).data()};
          fact["source"] = source_names[source];
          fact["source_id"] = decimal(provenance.source_id);
          fact["tick"] = decimal(provenance.tick);
          fact["craft_id"] = decimal(provenance.flight.value);
          fact["value"] = value;
          readings.append(fact);
        }
        if (ledger.orbital_surveys &&
            subject.kind == KnowledgeSubjectKind::planet) {
          for (const auto& record : ledger.orbital_surveys->completed) {
            if (record.system != subject.system ||
                record.planet.value != subject.identity)
              continue;
            godot::Dictionary survey;
            survey["started_tick"] = decimal(record.started_tick);
            survey["completed_tick"] = decimal(record.completed_tick);
            survey["craft_id"] = decimal(record.craft.value);
            survey["acquired_ticks"] =
                static_cast<std::int64_t>(kFreedomOrbitalSurveyTicks);
            row["orbital_survey"] = survey;
          }
        }
        row["facts"] = readings;
        subjects.append(row);
      }
      result["subjects"] = subjects;
      result["selected"] = true;
    } catch (const std::exception& error) {
      result["selected"] = false;
      result["subjects"] = godot::Array{};
      result["error"] = godot::String{error.what()};
    }
    return result;
  }

  auto get_freedom_environment_assessment(std::int64_t operation) const
      -> godot::Dictionary {
    godot::Dictionary result;
    if (!saved_flight || operation < 0 || operation > 4) {
      result["error"] = "Select a flight and supported surface operation";
      return result;
    }
    const auto& session = saved_flight->session;
    if (!session.knowledge()) {
      result["error"] = "This save has no selected environmental survey";
      return result;
    }
    const auto relative = session.relative_state();
    if (!relative || !relative->frame.planet) {
      result["error"] = "The current body's relative owner is unavailable";
      return result;
    }
    const auto& flight = *relative;
    const auto recipe =
        generate_planet_ambient_recipe(session.system(), *flight.frame.planet,
                                       session.knowledge()->recipe.ambient);
    if (!recipe) {
      result["error"] = "The current body's environmental owner is unavailable";
      return result;
    }
    const auto assessed = query_known_craft_environment(
        session.system(), *recipe, flight.craft,
        static_cast<CraftEnvironmentOperation>(operation), *session.knowledge(),
        flight.tick);
    if (!assessed) {
      result["error"] =
          "The current environmental survey could not be validated";
      return result;
    }
    const auto name = [](CraftEnvironmentRating rating) {
      return godot::String{craft_environment_rating_name(rating).data()};
    };
    constexpr std::array margin_names{"minimum_temperature",
                                      "maximum_temperature",
                                      "radiation",
                                      "electrical",
                                      "pressure",
                                      "gust",
                                      "ground_motion",
                                      "gravity",
                                      "vertical_thrust",
                                      "support_load"};
    static_assert(margin_names.size() == kCraftEnvironmentMarginCount);
    if (assessed->count > assessed->margins.size()) {
      result["error"] = "The environmental margin count is invalid";
      return result;
    }
    godot::Array margins;
    for (std::size_t i = 0; i < assessed->count; ++i) {
      const auto& margin = assessed->margins[i];
      if (!margin) continue;
      const auto kind = static_cast<std::size_t>(margin->kind);
      if (kind >= margin_names.size()) {
        result["error"] = "The environmental margin kind is invalid";
        return result;
      }
      godot::Dictionary row;
      row["kind"] = margin_names[kind];
      row["rating"] = name(margin->rating);
      margins.append(row);
    }
    result["rating"] = name(assessed->rating);
    result["shielding_thermal"] = name(assessed->axes[0]);
    result["structural"] = name(assessed->axes[1]);
    result["propulsion"] = name(assessed->axes[2]);
    result["operation_supported"] = assessed->operation_supported;
    result["margins"] = margins;
    return result;
  }

  auto get_wayfarer_frame() const -> godot::String {
    const auto diagnostic = craft_frame_diagnostic_json(wayfarer_frame());
    return diagnostic ? godot::String{diagnostic->c_str()} : godot::String{};
  }

  static auto project_station_geometry(const OriginStationDescriptor& station)
      -> godot::Dictionary {
    godot::Dictionary result;
    const auto geometry = origin_station_geometry(station);
    if (!geometry) return result;
    const auto vector = [](RigidVector3 value) {
      godot::PackedFloat64Array array;
      array.append(value.x);
      array.append(value.y);
      array.append(value.z);
      return array;
    };
    result["version"] = std::int64_t{1};
    result["station_id"] =
        godot::String{std::to_string(geometry->station.value).c_str()};
    result["asset_offset_metres"] = vector(kOriginStationAssetOffsetMetres);
    result["bounds_minimum_metres"] = vector(geometry->exterior.minimum_metres);
    result["bounds_maximum_metres"] = vector(geometry->exterior.maximum_metres);
    godot::Array ports;
    for (const auto& port : geometry->ports) {
      godot::Dictionary item;
      item["ordinal"] = static_cast<std::int64_t>(port.id.ordinal);
      item["station_id"] = result["station_id"];
      item["position_metres"] = vector(port.collar_position_metres);
      item["outward_normal"] = vector(port.outward_normal);
      item["withdrawal_metres"] = port.withdrawal_metres;
      item["bore_metres"] = port.clear_bore_metres;
      ports.append(item);
    }
    result["ports"] = ports;
    return result;
  }

  auto get_freedom_station_geometry() const -> godot::Dictionary {
    if (native_start) return project_station_geometry(native_start->station);
    if (saved_flight && !saved_flight->session.moon() &&
        !saved_flight->session.minor() &&
        project_saved_flight(saved_flight->session).station_available)
      return project_station_geometry(generate_origin_station(
          saved_flight->session.document().origin.recipe.universe_seed));
    return {};
  }

  auto get_freedom_minor_geometry(const godot::String& candidate_id) const
      -> godot::Dictionary {
    const SavedFlightWorld* selected = saved_flight.get();
    if (!candidate_id.is_empty()) {
      if (!pending || candidate_id != godot::String{pending->id.c_str()})
        return {};
      selected = pending->flight.get();
    }
    if (!selected || !selected->session.minor()) return {};
    try {
      const auto& owner = *selected->session.minor();
      const auto state = project_minor_flight_state(*selected);
      const MinorProxyMeshDimensions dimensions{};
      const auto counts = require(minor_proxy_mesh_counts(dimensions));
      std::vector<MinorProxyMeshVertex> vertices(counts.vertices);
      std::vector<std::uint32_t> indices(counts.indices);
      (void)require(
          fill_minor_proxy_mesh(owner, dimensions, vertices, indices));
      godot::Dictionary result;
      result["version"] = std::int64_t{1};
      result["body_kind"] = "minor";
      for (const auto* key :
           {"body_version", "body_id", "system_id", "craft_id", "frame_id",
            "frame_version", "tick", "body_radius"})
        result[key] = state[key];
      result["source_checksum"] = state["checksum"];
      godot::PackedVector3Array positions, normals;
      godot::PackedInt32Array elements;
      for (const auto& vertex : vertices) {
        const auto& p = vertex.position_metres;
        const auto& n = vertex.normal;
        positions.append(godot::Vector3(p.x, p.y, p.z));
        normals.append(godot::Vector3(n.x, n.y, n.z));
      }
      for (const auto index : indices)
        elements.append(static_cast<std::int32_t>(index));
      result["vertices"] = positions;
      result["normals"] = normals;
      result["indices"] = elements;
      return result;
    } catch (const std::exception&) {
      return {};
    }
  }

  auto get_freedom_background_sky(const godot::String& candidate_id) const
      -> godot::Dictionary {
    const SavedFlightWorld* selected = saved_flight.get();
    if (!candidate_id.is_empty()) {
      if (!pending || candidate_id != godot::String{pending->id.c_str()})
        return {};
      selected = pending->flight.get();
    }
    if (!selected) return {};
    const auto universe =
        selected->session.document().origin.recipe.universe_seed;
    godot::Dictionary result;
    result["version"] = std::int64_t{1};
    result["universe_seed"] =
        godot::String{std::to_string(universe.value).c_str()};
    // Reuse preview catalog1 only as distant background art. Its arbitrary
    // directions are not route positions, discoveries or navigable systems.
    result["disclosure"] = "background_only";
    godot::Array stars;
    for (const auto& star : flight_lab::sky_catalog(universe)) {
      godot::Dictionary item;
      item["direction"] =
          godot::Vector3(star.direction.x, star.direction.y, star.direction.z);
      item["color"] =
          godot::Color(star.color.red / 255.0f, star.color.green / 255.0f,
                       star.color.blue / 255.0f, 1);
      item["brightness"] = star.brightness;
      stars.append(item);
    }
    result["stars"] = stars;
    return result;
  }

  auto get_sky_catalog() const -> godot::Array {
    godot::Array result;
    if (!world) return result;
    for (const auto& star : flight_lab::sky_catalog(world->planet.seed)) {
      godot::Dictionary item;
      item["catalog_version"] = 1;
      item["system_seed"] =
          godot::String(std::to_string(star.system_seed.value).c_str());
      item["direction"] =
          godot::Vector3(star.direction.x, star.direction.y, star.direction.z);
      item["color"] =
          godot::Color(star.color.red / 255.0, star.color.green / 255.0,
                       star.color.blue / 255.0);
      item["brightness"] = star.brightness;
      result.append(item);
    }
    return result;
  }
  auto initialize(const godot::String& snapshot_json) -> bool {
    try {
      const auto utf8 = snapshot_json.utf8();
      const auto data = nlohmann::json::parse(utf8.get_data());
      if ((data.at("schema_version") != 1 && data.at("schema_version") != 2) ||
          data.at("terrain_generator_version") !=
              kTerrainTileGeneratorVersion ||
          data.at("seed_derivation_version") != kSeedDerivationVersion ||
          data.at("planet").at("generator_version") != kPlanetGeneratorVersion)
        throw std::invalid_argument(
            "unsupported generator or snapshot version");
      Request request;
      const bool physical = data.at("schema_version") == 2;
      if (!physical && data.contains("world_context"))
        throw std::invalid_argument(
            "standalone snapshot cannot contain world context");
      if (physical) {
        const auto universe = data.at("world_context")
                                  .at("origin_universe_seed")
                                  .get<std::string>();
        Seed universe_seed;
        const auto parsed_universe =
            std::from_chars(universe.data(), universe.data() + universe.size(),
                            universe_seed.value);
        if (parsed_universe.ec != std::errc{} ||
            parsed_universe.ptr != universe.data() + universe.size())
          throw std::invalid_argument("invalid physical origin universe seed");
        request.physical_origin_seed = universe_seed;
      }
      const auto seed = data.at("planet").at("planet_seed").get<std::string>();
      const auto parsed = std::from_chars(
          seed.data(), seed.data() + seed.size(), request.planet_seed.value);
      if (parsed.ec != std::errc{} || parsed.ptr != seed.data() + seed.size())
        throw std::invalid_argument("invalid planet seed");
      const auto numeric = [](const nlohmann::json& value) -> double {
        // get<double>() also accepts JSON booleans; snapshot scalars do not.
        if (!value.is_number())
          throw std::invalid_argument("snapshot scalar must be numeric");
        return value.get<double>();
      };
      const auto samples = numeric(data.at("samples"));
      if (!std::isfinite(samples) || samples < 2 || samples > 513 ||
          std::trunc(samples) != samples)
        throw std::invalid_argument("invalid sample dimensions");
      request.samples = static_cast<unsigned>(samples);
      request.span_metres = numeric(data.at("span_metres"));
      request.latitude = numeric(data.at("frame").at("latitude_radians"));
      request.longitude = numeric(data.at("frame").at("longitude_radians"));
      const auto lod = numeric(data.at("lod"));
      if (!std::isfinite(lod) || lod < 0 || lod > 10 || std::trunc(lod) != lod)
        throw std::invalid_argument("invalid LOD");
      request.lod = static_cast<std::uint8_t>(lod);
      const auto relief = data.contains("experimental_relief_version")
                              ? numeric(data.at("experimental_relief_version"))
                              : 0.0;
      if (!std::isfinite(relief) || relief < 0 ||
          relief > kExperimentalReliefVersion || std::trunc(relief) != relief)
        throw std::invalid_argument("unsupported experimental relief version");
      request.relief_version = static_cast<unsigned>(relief);
      validate(request);
      auto resolved = resolve_snapshot_world(request);
      // Both schemas compare the complete descriptor. Schema2 additionally
      // proves the exact physical origin owner; seed-only aliases cannot pass.
      const auto canonical_planet =
          nlohmann::json::parse(planet_descriptor_json(resolved.planet));
      if (data.at("planet") != canonical_planet)
        throw std::invalid_argument(
            "snapshot planet differs from C++ descriptor");
      if (physical &&
          data.at("world_context") != snapshot_world_context(resolved))
        throw std::invalid_argument(
            "snapshot world context differs from C++ origin recipe");
      auto candidate =
          std::make_unique<LiveWorld>(request, std::move(resolved));
      const auto& reference = data.at("replay").at(0);
      if (reference.at("tick") != 0 ||
          reference.at("checksum") !=
              std::to_string(
                  planetary_flight_state_checksum(candidate->flight)))
        throw std::invalid_argument(
            "snapshot initial state differs from live C++ state");
      replace_stream(nullptr);
      native_start.reset();
      saved_flight.reset();
      world = std::move(candidate);
      pending.reset();
      last_error = godot::String{};
      return true;
    } catch (const std::exception& error) {
      last_error = godot::String{error.what()};
      return false;
    }
  }

  auto get_world_lighting() const -> godot::Dictionary {
    godot::Dictionary result;
    result["enabled"] = false;
    try {
      if (!world) throw std::runtime_error("bridge not initialized");
      if (!world->world_recipe.physical) return result;
      const auto& flight = world->flight;
      // The lab remains nonrotating. Its displayed geodetic position locates
      // a visual lighting probe, not a canonical rigid-state frame handoff.
      const auto observer = require(
          planet_fixed_from_geodetic(world->planet, flight.pose.position));
      const auto lighting = require(resolve_planet_rotation(
          *world->world_recipe.physical, *world->world_recipe.rotation,
          flight.tick, observer));
      const auto& geometry = lighting.geometry;
      const auto view = stream ? require(make_local_tangent_frame(
                                     world->planet, flight.pose.position))
                               : world->frame;
      const auto toward_star = flight_lab::vec(geometry.observer_to_star_fixed);
      const auto radial =
          flight_lab::unit({observer.x, observer.y, observer.z});
      const auto direction = godot::Vector3(
          flight_lab::dot(toward_star, flight_lab::vec(view.east)),
          flight_lab::dot(toward_star, flight_lab::vec(view.up)),
          -flight_lab::dot(toward_star, flight_lab::vec(view.north)));
      godot::Basis local_to_fixed;
      local_to_fixed.set_column(
          0, godot::Vector3(view.east.x, view.east.y, view.east.z));
      local_to_fixed.set_column(
          1, godot::Vector3(view.up.x, view.up.y, view.up.z));
      local_to_fixed.set_column(
          2, godot::Vector3(-view.north.x, -view.north.y, -view.north.z));
      const auto& q = geometry.fixed_to_system;
      const godot::Basis fixed_to_system{godot::Quaternion(q.x, q.y, q.z, q.w)};
      const auto& star = world->world_recipe.physical->catalog.star;
      result["enabled"] = true;
      result["tick"] = static_cast<std::int64_t>(flight.tick);
      result["direction"] = direction;
      result["direction_system"] =
          godot::Vector3(geometry.observer_to_star_system.x,
                         geometry.observer_to_star_system.y,
                         geometry.observer_to_star_system.z);
      result["local_to_system"] = fixed_to_system * local_to_fixed;
      result["solar_elevation_sine"] = flight_lab::dot(radial, toward_star);
      result["rotation_period_ticks"] =
          static_cast<std::int64_t>(lighting.recipe.rotation.period_ticks);
      result["star_color"] =
          godot::Color(star.color.red / 255.0f, star.color.green / 255.0f,
                       star.color.blue / 255.0f, 1);
      result["star_radius_metres"] =
          static_cast<double>(star.radius_kilometres) * 1000.0;
      result["star_distance_metres"] = geometry.observer_star_distance_metres;
      result["star_angular_radius_radians"] = std::asin(
          std::min(1.0, static_cast<double>(star.radius_kilometres) * 1000.0 /
                            geometry.observer_star_distance_metres));
      result["world_context_json"] =
          godot::String(world->world_context.dump().c_str());
      result["model"] = "physical-circular-rotation-1-presentation";
      result["probe_frame"] = "visual_geodetic";
      return result;
    } catch (const std::exception& error) {
      result["enabled"] = false;
      result["error"] = godot::String(error.what());
      return result;
    }
  }

  auto advance(double elapsed, std::int64_t buttons) -> bool {
    try {
      if (!world) throw std::runtime_error("bridge not initialized");
      if (buttons < 0 || buttons > 255)
        throw std::invalid_argument("invalid control bits");
      world->advance(elapsed, static_cast<unsigned>(buttons));
      last_error = godot::String{};
      return true;
    } catch (const std::exception& error) {
      last_error = godot::String{error.what()};
      return false;
    }
  }

  auto advance_analog(double elapsed, const godot::PackedFloat64Array& axes)
      -> bool {
    try {
      if (!world) throw std::runtime_error("bridge not initialized");
      if (axes.size() != 4)
        throw std::invalid_argument("expected four flight axes");
      for (int i = 0; i < 4; ++i)
        if (!std::isfinite(axes[i]) || std::abs(axes[i]) > 1.0)
          throw std::invalid_argument("flight axis outside finite [-1,1]");
      const unsigned buttons =
          (axes[0] > 0 ? 1U : 0U) | (axes[0] < 0 ? 2U : 0U) |
          (axes[1] < 0 ? 4U : 0U) | (axes[1] > 0 ? 8U : 0U) |
          (axes[2] < 0 ? 16U : 0U) | (axes[2] > 0 ? 32U : 0U) |
          (axes[3] > 0 ? 64U : 0U) | (axes[3] < 0 ? 128U : 0U);
      world->advance(elapsed, buttons,
                     PlanetaryAnalogInput{axes[0], axes[1], axes[2], axes[3]});
      last_error = godot::String{};
      return true;
    } catch (const std::exception& error) {
      last_error = godot::String{error.what()};
      return false;
    }
  }

  auto enable_thrust_flight() -> bool {
    try {
      if (!world || world->flight.tick != 0 || world->lab)
        throw std::invalid_argument(
            "enable flight lab once, before the first tick");
      world->lab =
          flight_lab::initial(world->planet, world->flight.pose.position,
                              world->flight.pose.heading_radians);
      world->lab->clearance = world->flight.clearance_metres;
      last_error = godot::String{};
      return true;
    } catch (const std::exception& e) {
      last_error = e.what();
      return false;
    }
  }

  auto enable_surface_practice() -> bool {
    return enable_surface_practice_impl(false);
  }

  auto enable_orbit_practice() -> bool {
    return enable_surface_practice_impl(true);
  }

  auto enable_surface_practice_impl(bool orbital_assist) -> bool {
    try {
      if (!world || world->flight.tick != 0 || world->lab ||
          world->flight.pose.position.latitude_radians !=
              world->reference_start.latitude ||
          world->flight.pose.position.longitude_radians !=
              world->reference_start.longitude ||
          planetary_flight_state_checksum(world->flight) !=
              world->reference_flight_checksum)
        throw std::invalid_argument(
            "surface practice requires a fresh original snapshot session");
      const auto candidate =
          survey_surface_start(world->planet, world->reference_start,
                               world->cache, world->flight.tick);
      auto lab_candidate = candidate.flight;
      flight_lab::enable_rigid_attitude(lab_candidate);
      if (orbital_assist)
        flight_lab::enable_orbit_preserving_translation(lab_candidate);
      // The legacy-shaped projection is only terrain/camera telemetry, never a
      // replacement legacy replay or a saved career. Commit both views
      // together.
      auto projected = world->flight;
      projected.pose.position = candidate.pose;
      projected.pose.heading_radians = kSurfaceStartHeadingRadians;
      projected.velocity = {};
      projected.clearance_metres = candidate.flight.clearance;
      world->flight = projected;
      world->lab = lab_candidate;
      world->surface_start = candidate;
      last_error = godot::String{};
      return true;
    } catch (const std::exception& e) {
      last_error = e.what();
      return false;
    }
  }

  auto advance_thrust(double elapsed, const godot::PackedFloat64Array& axes,
                      bool assist) -> bool {
    try {
      if (!world || axes.size() != 7)
        throw std::invalid_argument("expected seven flight actuator demands");
      world->advance_thrust(elapsed, {axes[0], axes[1], axes[2], axes[3],
                                      axes[4], axes[5], axes[6], assist});
      last_error = godot::String{};
      return true;
    } catch (const std::exception& e) {
      last_error = e.what();
      return false;
    }
  }

  auto get_flight_guidance(std::int64_t mode) const -> godot::Dictionary {
    godot::Dictionary out;
    try {
      if (mode < 0 || mode > 3 || !world || !world->lab)
        throw std::invalid_argument(
            "guidance requires a native flight state and mode 0..3");
      const auto& state = *world->lab;
      const auto g = flight_lab::flight_guidance(
          world->planet, state, {static_cast<flight_lab::GuidanceMode>(mode)});
      out["ok"] = true;
      out["mode"] = mode;
      if (mode == 0) return out;
      const double radius = world->planet.radius.value * 1000.0;
      const auto radial = flight_lab::unit(state.position);
      auto tangent =
          state.velocity - radial * flight_lab::dot(state.velocity, radial);
      if (flight_lab::length(tangent) < 1e-6) {
        tangent =
            state.back * -1 - radial * flight_lab::dot(state.back * -1, radial);
        if (flight_lab::length(tangent) < 1e-6)
          tangent = flight_lab::cross(std::abs(radial.z) < .9
                                          ? flight_lab::V{0, 0, 1}
                                          : flight_lab::V{0, 1, 0},
                                      radial);
      }
      tangent = flight_lab::unit(tangent);
      godot::PackedVector2Array coast;
      for (const auto& sample : g.coast_samples)
        coast.push_back(
            godot::Vector2(flight_lab::dot(sample.position, tangent) / radius,
                           -flight_lab::dot(sample.position, radial) / radius));
      godot::PackedVector2Array reference;
      if (g.target_available) {
        const double a_radius = g.reference_radius_metres;
        for (int i = 0; i <= 96; ++i) {
          const double angle =
              (mode == 3 ? 2.0 : 2 * std::numbers::pi) * i / 96;
          double r = a_radius;
          if (mode == 2) {
            const double peri = g.reference_periapsis_radius_metres;
            const double eccentricity = (a_radius - peri) / (a_radius + peri);
            r = a_radius * (1 - eccentricity) /
                (1 - eccentricity * std::cos(angle));
          } else if (mode == 3) {
            r = 2 * a_radius / (1 + std::cos(angle));
          }
          reference.push_back(godot::Vector2(r * std::sin(angle) / radius,
                                             -r * std::cos(angle) / radius));
        }
      }
      out["coast"] = coast;
      out["reference"] = reference;
      out["air_radius"] = 1 + g.orbit.atmosphere_edge / radius;
      out["seconds"] =
          g.coast_samples.empty() ? 0 : g.coast_samples.back().seconds;
      out["termination"] = static_cast<std::int64_t>(g.termination);
      using flight_lab::GuidanceCue;
      const char* cue = "none";
      switch (g.cue) {
        case GuidanceCue::none: break;
        case GuidanceCue::below_reference_surface: cue = "below_surface"; break;
        case GuidanceCue::climb_above_atmosphere: cue = "climb"; break;
        case GuidanceCue::atmosphere_model_limit: cue = "in_atmosphere"; break;
        case GuidanceCue::build_horizontal_speed: cue = "sideways"; break;
        case GuidanceCue::circular_reference: cue = "circular"; break;
        case GuidanceCue::orbit_established: cue = "orbit"; break;
        case GuidanceCue::return_reference: cue = "return"; break;
        case GuidanceCue::escape_reference: cue = "escape"; break;
        case GuidanceCue::escape_energy_reached: cue = "unbound"; break;
      }
      out["cue"] = godot::String{cue};
      out["radial_degenerate"] = g.radial_degenerate;
      out["reference_altitude"] = g.reference_radius_metres - radius;
      out["reference_speed"] = flight_lab::length(g.target_velocity);
      out["horizontal_speed"] = g.orbit.horizontal_speed;
      out["radial_speed"] = g.orbit.radial_speed;
      out["delta_available"] = g.delta_velocity_available;
      out["delta_speed"] = flight_lab::length(g.delta_velocity);
      const auto body_delta = flight_lab::to_body(state, g.delta_velocity);
      out["body_delta"] =
          godot::Vector3(body_delta.x, body_delta.y, body_delta.z);
      out["assist"] = state.assist;
      out["thrust_active"] = flight_lab::length(state.thrust_body) > 0.01;
      return out;
    } catch (const std::exception& error) {
      out["ok"] = false;
      out["error"] = godot::String{error.what()};
      return out;
    }
  }

  auto get_state() const -> godot::Dictionary {
    godot::Dictionary result;
    if (!world) return result;
    const auto& flight = world->flight;
    const auto fixed =
        planet_fixed_from_geodetic(world->planet, flight.pose.position);
    if (!fixed) return result;
    const auto local = local_from_planet_fixed(world->frame, *fixed);
    if (!local) return result;
    result["position"] =
        stream ? godot::Vector3{}
               : godot::Vector3(local->east, local->up, -local->north);
    result["heading"] = flight.pose.heading_radians;
    result["clearance"] = flight.clearance_metres;
    result["speed"] = std::hypot(flight.velocity.east_metres_per_second,
                                 flight.velocity.north_metres_per_second,
                                 flight.velocity.up_metres_per_second);
    result["tick"] = static_cast<std::int64_t>(flight.tick);
    result["controls"] = static_cast<std::int64_t>(world->previous_buttons);
    result["checksum"] = godot::String{
        std::to_string(planetary_flight_state_checksum(flight)).c_str()};
    result["dropped_seconds"] = world->dropped_seconds;
    result["inside_patch"] =
        std::abs(local->east) < world->span_metres * 0.45 &&
        std::abs(local->north) < world->span_metres * 0.45;
    result["streaming"] = stream != nullptr;
    result["latitude"] = flight.pose.position.latitude_radians;
    result["longitude"] = flight.pose.position.longitude_radians;
    result["altitude"] = flight.pose.position.altitude_metres;
    result["planet_radius"] = world->planet.radius.value * 1000.0;
    const unsigned lab_version = !world->lab ? 0
                                 : world->lab->translation_policy == 2
                                     ? 3
                                     : world->lab->angular_model;
    result["flight_model"] = lab_version == 3   ? "thrust-lab-3"
                             : lab_version == 2 ? "thrust-lab-2"
                             : lab_version == 1 ? "thrust-lab-1"
                                                : "legacy";
    if (world->surface_start) {
      const auto& start = *world->surface_start;
      godot::Dictionary reference;
      reference["version"] = kSurfaceStartVersion;
      reference["planet_seed"] =
          godot::String{std::to_string(world->planet.seed.value).c_str()};
      reference["latitude"] = start.reference.latitude_radians;
      reference["longitude"] = start.reference.longitude_radians;
      reference["heading"] = kSurfaceStartHeadingRadians;
      reference["source_lod"] = world->lod;
      reference["relief_version"] = world->relief_version;
      reference["grid_half_extent_metres"] = kSurfaceStartHalfExtentMetres;
      reference["grid_spacing_metres"] = kSurfaceStartSpacingMetres;
      reference["sample_count"] = kSurfaceStartSamples;
      reference["margin_metres"] = kSurfaceStartMarginMetres;
      reference["center_elevation_metres"] = start.center_elevation_metres;
      reference["maximum_sampled_elevation_metres"] =
          start.maximum_sampled_elevation_metres;
      reference["altitude_metres"] = start.pose.altitude_metres;
      result["surface_start_reference"] = reference;
    }
    if (world->lab) {
      const auto& lab = *world->lab;
      const auto view_frame =
          stream
              ? make_local_tangent_frame(world->planet, flight.pose.position)
              : std::expected<LocalTangentFrame, CoordinateError>{world->frame};
      if (!view_frame) return {};
      godot::Basis basis;
      int column = 0;
      for (auto axis : {lab.right, lab.up, lab.back})
        basis.set_column(
            column++,
            godot::Vector3(
                flight_lab::dot(axis, flight_lab::vec(view_frame->east)),
                flight_lab::dot(axis, flight_lab::vec(view_frame->up)),
                -flight_lab::dot(axis, flight_lab::vec(view_frame->north))));
      result["body_basis"] = basis;
      const auto orbit = flight_lab::orbit_info(world->planet, lab);
      result["periapsis"] = orbit.periapsis;
      result["apoapsis"] = orbit.apoapsis;
      result["bound_orbit"] = orbit.bound;
      result["clear_orbit"] = orbit.clear_orbit;
      result["atmosphere_edge"] = orbit.atmosphere_edge;
      result["horizontal_speed"] = orbit.horizontal_speed;
      result["circular_speed"] = orbit.circular_speed;
      result["body_velocity"] =
          godot::Vector3(flight_lab::to_body(lab, lab.velocity).x,
                         flight_lab::to_body(lab, lab.velocity).y,
                         flight_lab::to_body(lab, lab.velocity).z);
      godot::Basis sky_basis;
      sky_basis.set_column(0, godot::Vector3(view_frame->east.x,
                                             view_frame->east.y,
                                             view_frame->east.z));
      sky_basis.set_column(1, godot::Vector3(view_frame->up.x, view_frame->up.y,
                                             view_frame->up.z));
      sky_basis.set_column(2, godot::Vector3(-view_frame->north.x,
                                             -view_frame->north.y,
                                             -view_frame->north.z));
      result["view_to_inertial"] = sky_basis;
      result["main_thrust"] = lab.main;
      result["retro_thrust"] = lab.retro;
      result["air_density"] = lab.density;
      result["dynamic_pressure"] = lab.dynamic_pressure;
      result["acceleration"] = lab.acceleration;
      result["floor_guard"] = lab.floor_guard;
      result["assist"] = lab.assist;
      result["angular_model"] = lab.angular_model;
      result["translation_policy"] = lab.translation_policy;
      result["translation_assist_weight"] =
          flight_lab::assist_translation_weight(world->planet, lab);
      result["effective_air_density"] =
          flight_lab::effective_aerodynamic_density(world->planet, lab);
      result["angular_velocity_body"] =
          godot::Vector3(lab.angular.x, lab.angular.y, lab.angular.z);
      result["attitude_stabilized"] = lab.angular_model == 1 || lab.assist;
      result["applied_torque_body"] = godot::Vector3(
          lab.torque_body.x, lab.torque_body.y, lab.torque_body.z);
      result["torque_saturated"] = lab.torque_saturated;
      result["climb_rate"] = flight.velocity.up_metres_per_second;
      result["rcs_acceleration"] = godot::Vector3(
          lab.thrust_body.x, lab.thrust_body.y, lab.thrust_body.z);
      result["checksum"] =
          godot::String{((lab_version == 3   ? "lab3:"
                          : lab_version == 2 ? "lab2:"
                                             : "lab1:") +
                         std::to_string(flight_lab::checksum(lab)))
                            .c_str()};
    }
    return result;
  }

  auto request_freedom_moon_stream() -> bool {
    try {
      if (!saved_flight || !saved_flight->session.moon() || !moon_stream ||
          moon_stream->owner() != *saved_flight->session.moon())
        throw std::invalid_argument("Actual Moon stream owner unavailable");
      moon_stream->request(
          project_saved_moon_flight(saved_flight->session).tangent.origin);
      last_error = godot::String{};
      return true;
    } catch (const std::exception& error) {
      last_error = error.what();
      return false;
    }
  }
  auto poll_freedom_moon_stream() -> godot::Dictionary {
    godot::Dictionary result;
    try {
      if (!saved_flight || !saved_flight->session.moon() || !moon_stream ||
          moon_stream->owner() != *saved_flight->session.moon())
        throw std::invalid_argument("Actual Moon stream owner unavailable");
      auto batch = moon_stream->poll();
      if (!batch) return result;
      godot::Array tiles;
      std::set<MoonStreamKey> next;
      for (const auto& [key, tile] : batch->tiles) {
        if (!tile || tile->body != moon_stream->owner().body ||
            tile->key != key ||
            tile->terrain_version != kMoonTerrainRecipeVersion ||
            tile->vertices.size() != 1221 || tile->normals.size() != 1221 ||
            tile->colors.size() != 1221 || tile->indices.size() != 7680)
          throw std::invalid_argument("Invalid owned Moon tile dimensions");
        next.insert(key);
        godot::Dictionary entry;
        entry["id"] = godot::String{
            (std::to_string(key.face) + ":" + std::to_string(key.lod) + ":" +
             std::to_string(key.x) + ":" + std::to_string(key.y))
                .c_str()};
        if (!moon_exported.contains(key)) {
          godot::PackedFloat64Array anchor;
          for (const auto v :
               {tile->anchor.x, tile->anchor.y, tile->anchor.z}) {
            if (!std::isfinite(v) || std::abs(v) > 1e8)
              throw std::invalid_argument("Nonfinite Moon tile anchor");
            anchor.append(v);
          }
          godot::PackedVector3Array vertices, normals;
          godot::PackedColorArray colors;
          godot::PackedInt32Array indices;
          for (std::size_t i = 0; i < tile->vertices.size(); ++i) {
            const auto p = tile->vertices[i], n = tile->normals[i];
            if (!saved_moon_detail::finite(p) || !saved_moon_detail::finite(n))
              throw std::invalid_argument("Nonfinite Moon tile attribute");
            vertices.append({static_cast<godot::real_t>(p.x),
                             static_cast<godot::real_t>(p.y),
                             static_cast<godot::real_t>(p.z)});
            normals.append({static_cast<godot::real_t>(n.x),
                            static_cast<godot::real_t>(n.y),
                            static_cast<godot::real_t>(n.z)});
            const auto c = tile->colors[i];
            colors.append(godot::Color(c.red / 255.0f, c.green / 255.0f,
                                       c.blue / 255.0f, 1));
          }
          for (const auto i : tile->indices) {
            if (i < 0 || static_cast<std::size_t>(i) >= tile->vertices.size())
              throw std::invalid_argument(
                  "Moon tile index exceeds owned buffer");
            indices.append(i);
          }
          entry["anchor"] = anchor;
          entry["vertices"] = vertices;
          entry["normals"] = normals;
          entry["colors"] = colors;
          entry["indices"] = indices;
        }
        tiles.append(entry);
      }
      result["body_kind"] = "moon";
      result["body_id"] = godot::String{
          std::to_string(moon_stream->owner().body.value).c_str()};
      result["tiles"] = tiles;
      result["generated"] = static_cast<std::int64_t>(batch->generated);
      result["reused"] = static_cast<std::int64_t>(batch->reused);
      moon_exported = std::move(next);
      last_error = godot::String{};
    } catch (const std::exception& error) {
      last_error = error.what();
      result.clear();
      result["error"] = last_error;
    }
    return result;
  }
  auto freedom_moon_stream_transforms(godot::PackedFloat64Array anchors)
      -> godot::Array {
    godot::Array result;
    try {
      if (!saved_flight || !saved_flight->session.moon() || !moon_stream ||
          moon_stream->owner() != *saved_flight->session.moon() ||
          anchors.size() % 3 ||
          anchors.size() >
              static_cast<std::int64_t>(kMoonStreamMaxTiles) * 2 * 3)
        throw std::invalid_argument("Invalid Moon tile anchor buffer or owner");
      for (std::int64_t i = 0; i < anchors.size(); ++i)
        if (!std::isfinite(anchors[i]) || std::abs(anchors[i]) > 1e8)
          throw std::invalid_argument("Invalid Moon tile anchor coordinate");
      const auto view = project_saved_moon_flight(saved_flight->session);
      const auto craft = require(
          reframe_rigid_body(RigidBodyWorldContext{view.moon}, view.body,
                             {view.fixed_frame, view.body.tick}, view.moon));
      const auto local = [&](RigidVector3 v) {
        const auto p = moon_fixed_vector_to_local(view, v);
        return godot::Vector3(p.x, p.y, p.z);
      };
      godot::Basis basis;
      basis.set_column(0, local({1, 0, 0}));
      basis.set_column(1, local({0, 1, 0}));
      basis.set_column(2, local({0, 0, 1}));
      for (std::int64_t i = 0; i < anchors.size(); i += 3) {
        // Subtract in double precision BEFORE the Godot float conversion. The
        // observer foot changes the axes, never the ship-centred render origin.
        const auto p = local({anchors[i] - craft.position_metres.x,
                              anchors[i + 1] - craft.position_metres.y,
                              anchors[i + 2] - craft.position_metres.z});
        result.append(godot::Transform3D(basis, p));
      }
      last_error = godot::String{};
    } catch (const std::exception& error) {
      last_error = error.what();
      result.clear();
    }
    return result;
  }

  auto enable_streaming() -> bool {
    try {
      if (saved_flight && saved_flight->session.minor()) {
        last_error = godot::String{};
        return true;
      }
      if (saved_flight && saved_flight->session.moon()) {
        if (!moon_stream) replace_saved_stream(saved_flight->session);
        last_error = godot::String{};
        return true;
      }
      if (saved_flight && saved_flight->session.starting_assembly()) {
        if (!stream)
          throw std::runtime_error(
              "Selected assembly requires staged streaming");
        last_error = godot::String{};
        return true;
      }
      if (!world && !saved_flight)
        throw std::runtime_error("initialize C++ world first");
      replace_stream(
          saved_flight ? saved_stream(saved_flight->session)
                       : std::make_unique<PlanetStream>(
                             world->planet, world->lod, world->relief_version));
      last_error = godot::String{};
      return true;
    } catch (const std::exception& e) {
      last_error = e.what();
      return false;
    }
  }

  auto request_stream(godot::Vector3 observer) -> bool {
    try {
      if (!stream) throw std::runtime_error("streaming is not enabled");
      const auto frame =
          saved_flight ? project_saved_flight(saved_flight->session).tangent
                       : require(make_local_tangent_frame(
                             world->planet, world->flight.pose.position));
      const auto fixed = require(planet_fixed_from_local(
          frame, {observer.x, -observer.z, observer.y}));
      stream->request(fixed);
      last_error = godot::String{};
      return true;
    } catch (const std::exception& e) {
      last_error = e.what();
      return false;
    }
  }

  auto is_stream_busy() const -> bool {
    return (stream && stream->busy()) || (moon_stream && moon_stream->busy());
  }

  auto get_stream_identity() const -> godot::Dictionary {
    godot::Dictionary result;
    const auto decimal = [](std::uint64_t word) {
      return godot::String{std::to_string(word).c_str()};
    };
    if (moon_stream) {
      const auto& owner = moon_stream->owner();
      result["schema_version"] = std::int64_t{1};
      result["generation"] = decimal(stream_generation);
      result["kind"] = "owned";
      result["system_id"] = decimal(owner.catalog.owner.catalog.id.value);
      result["body_version"] = static_cast<std::int64_t>(owner.body.version);
      result["body_kind"] = "moon";
      result["body_id"] = decimal(owner.body.value);
      result["physical_catalog"] =
          static_cast<std::int64_t>(owner.catalog.owner.generator_version);
      result["terrain_recipe"] =
          static_cast<std::int64_t>(kMoonTerrainRecipeVersion);
      return result;
    }
    if (!stream) return result;
    result["schema_version"] = std::int64_t{1};
    result["generation"] = decimal(stream_generation);
    result["terrain_policy"] =
        static_cast<std::int64_t>(stream->selected_terrain_policy());
    if (stream->selected_owner()) {
      const auto& owner = *stream->selected_owner();
      const auto body = stream->selected_body();
      if (!body) return {};
      result["kind"] = "owned";
      result["system_id"] = decimal(owner.catalog.id.value);
      result["body_version"] = static_cast<std::int64_t>(body->version);
      result["body_kind"] = "planet";
      result["body_id"] = decimal(body->value);
      result["physical_catalog"] =
          static_cast<std::int64_t>(owner.generator_version);
    } else {
      if (!world) return {};
      result["kind"] = "study";
      result["planet_id"] = decimal(world->planet.id.value);
      result["experimental_relief_version"] =
          static_cast<std::int64_t>(world->relief_version);
    }
    return result;
  }

  auto poll_stream() -> godot::Dictionary {
    godot::Dictionary result;
    try {
      if (!stream) throw std::runtime_error("streaming is not enabled");
      auto batch = stream->poll();
      if (!batch) return result;
      godot::Array entries;
      std::set<StreamKey> next;
      for (const auto& [key, tile] : batch->tiles) {
        godot::Dictionary entry;
        entry["id"] = godot::String(stream_id(key).c_str());
        next.insert(key);
        if (!exported.contains(key)) {
          godot::PackedFloat64Array anchor;
          for (double v : {tile->anchor.x, tile->anchor.y, tile->anchor.z})
            anchor.push_back(v);
          entry["anchor"] = anchor;
          godot::PackedVector3Array vertices, normals;
          godot::PackedColorArray colors;
          godot::PackedVector2Array uv;
          godot::PackedInt32Array indices;
          for (std::size_t i = 0; i < tile->vertices.size(); ++i) {
            const auto p = tile->vertices[i], n = tile->normals[i];
            const auto c = tile->colors[i];
            vertices.push_back({static_cast<float>(p.x),
                                static_cast<float>(p.y),
                                static_cast<float>(p.z)});
            normals.push_back({static_cast<float>(n.x), static_cast<float>(n.y),
                               static_cast<float>(n.z)});
            colors.push_back(
                {c.red / 255.0f, c.green / 255.0f, c.blue / 255.0f, 1});
            uv.push_back({static_cast<float>(tile->elevations[i]),
                          tile->wetness.empty()
                              ? 0.0f
                              : static_cast<float>(tile->wetness[i])});
          }
          for (auto i : tile->indices)
            indices.push_back(i);
          entry["vertices"] = vertices;
          entry["normals"] = normals;
          entry["colors"] = colors;
          entry["uv"] = uv;
          entry["indices"] = indices;
        }
        entries.push_back(entry);
      }
      godot::Dictionary morph;
      if (!batch->transition.morphs.empty()) {
        godot::Array draw_ids, endpoints;
        for (const auto& [key, tile] : batch->transition.draw_tiles) {
          (void)tile;
          draw_ids.push_back(godot::String{stream_id(key).c_str()});
        }
        const auto as_float = [](double value) -> float {
          if (!std::isfinite(value) ||
              std::abs(value) > std::numeric_limits<float>::max())
            throw std::invalid_argument("unrepresentable terrain morph value");
          return static_cast<float>(value);
        };
        for (const auto& [key, endpoint] : batch->transition.morphs) {
          const auto count =
              batch->transition.draw_tiles.at(key)->vertices.size();
          if (endpoint.coarse_vertices.size() != count ||
              endpoint.coarse_normals.size() != count ||
              endpoint.coarse_colors.size() != count ||
              endpoint.coarse_elevations.size() != count ||
              (!endpoint.coarse_wetness.empty() &&
               endpoint.coarse_wetness.size() != count))
            throw std::invalid_argument("incomplete terrain morph attributes");
          godot::PackedFloat32Array positions, normals, colors;
          for (std::size_t i = 0; i < count; ++i) {
            const auto position = endpoint.coarse_vertices[i];
            const auto normal = endpoint.coarse_normals[i];
            const auto color = endpoint.coarse_colors[i];
            for (const auto value : {position.x, position.y, position.z,
                                     endpoint.coarse_elevations[i]})
              positions.push_back(as_float(value));
            for (const auto value :
                 {normal.x, normal.y, normal.z,
                  endpoint.coarse_wetness.empty() ? 0.0
                                                  : endpoint.coarse_wetness[i]})
              normals.push_back(as_float(value));
            for (const auto value : {color[0], color[1], color[2], 1.0})
              colors.push_back(as_float(value));
          }
          godot::Dictionary entry;
          entry["id"] = godot::String{stream_id(key).c_str()};
          entry["coarse_id"] =
              godot::String{stream_id(endpoint.coarse_key).c_str()};
          entry["refining"] = endpoint.refining;
          entry["position_elevation"] = positions;
          entry["normals"] = normals;
          entry["colors"] = colors;
          endpoints.push_back(entry);
        }
        morph["version"] = 1;
        morph["draw_ids"] = draw_ids;
        morph["entries"] = endpoints;
      }
      exported = std::move(next);
      result["morph"] = morph;
      result["morph_payload_bytes"] =
          static_cast<std::int64_t>(batch->transition.bytes());
      result["transition_draw_tiles"] =
          static_cast<std::int64_t>(batch->transition.draw_tiles.size());
      result["identity"] = get_stream_identity();
      result["tiles"] = entries;
      result["generated"] = batch->generated;
      result["reused"] = batch->reused;
      result["worker_ms"] = batch->milliseconds;
      result["cpu_mesh_bytes"] = static_cast<std::int64_t>(batch->bytes());
      last_error = godot::String{};
    } catch (const std::exception& e) {
      last_error = e.what();
      result["error"] = last_error;
    }
    return result;
  }

  auto stream_transforms(godot::PackedFloat64Array anchors) -> godot::Array {
    godot::Array result;
    try {
      if (!stream || anchors.size() % 3 ||
          anchors.size() > static_cast<std::int64_t>(kStreamMaxTiles) * 2 * 3)
        throw std::invalid_argument("invalid tile anchor buffer");
      for (std::int64_t i = 0; i < anchors.size(); ++i)
        if (!std::isfinite(anchors[i]) || std::abs(anchors[i]) > 1.0e10)
          throw std::invalid_argument("invalid tile anchor coordinate");
      const auto frame =
          saved_flight ? project_saved_flight(saved_flight->session).tangent
                       : require(make_local_tangent_frame(
                             world->planet, world->flight.pose.position));
      godot::Basis basis;
      basis.set_column(0, {static_cast<float>(frame.east.x),
                           static_cast<float>(frame.up.x),
                           static_cast<float>(-frame.north.x)});
      basis.set_column(1, {static_cast<float>(frame.east.y),
                           static_cast<float>(frame.up.y),
                           static_cast<float>(-frame.north.y)});
      basis.set_column(2, {static_cast<float>(frame.east.z),
                           static_cast<float>(frame.up.z),
                           static_cast<float>(-frame.north.z)});
      for (std::int64_t i = 0; i < anchors.size(); i += 3) {
        const auto p = require(local_from_planet_fixed(
            frame, {anchors[i], anchors[i + 1], anchors[i + 2]}));
        result.push_back(godot::Transform3D(
            basis, {static_cast<float>(p.east), static_cast<float>(p.up),
                    static_cast<float>(-p.north)}));
      }
      last_error = godot::String{};
    } catch (const std::exception& e) {
      last_error = e.what();
    }
    return result;
  }

  // Explicit inspection/test relocation, never called by normal flight input.
  // It does not stand in for a completed gameplay jump or orbital handoff.
  auto set_survey_pose(double latitude, double longitude, double altitude)
      -> bool {
    try {
      if (!stream || !std::isfinite(altitude) || altitude > 1.0e9)
        throw std::invalid_argument(
            "invalid survey pose or streaming disabled");
      GeodeticPosition pose{latitude, longitude, altitude};
      const auto fixed =
          require(planet_fixed_from_geodetic(world->planet, pose));
      const auto sample =
          with_relief(require(sample_planet_surface(world->planet, fixed,
                                                    world->lod, world->cache)),
                      world->planet, fixed, world->relief_version);
      auto candidate = require(initial_planetary_flight_state(
          world->planet, pose, {sample.elevation_metres}, 0.0,
          FlightMode::manual));
      auto lab_candidate = world->lab;
      if (world->lab) {
        lab_candidate = flight_lab::initial(world->planet, pose, 0,
                                            world->lab->angular_model);
        if (world->lab->translation_policy == 2)
          flight_lab::enable_orbit_preserving_translation(*lab_candidate);
        lab_candidate->clearance = candidate.clearance_metres;
        lab_candidate->density = flight_lab::density(world->planet, altitude);
      }
      world->flight = candidate;
      world->lab = lab_candidate;
      world->clock = FixedStepClock{};
      world->previous_buttons = 0;
      last_error = godot::String{};
      return true;
    } catch (const std::exception& e) {
      last_error = e.what();
      return false;
    }
  }

  auto get_last_error() const -> godot::String { return last_error; }

  auto camera_clearance(godot::Vector3 offset) -> double {
    try {
      if (!world || !stream || !offset.is_finite() || offset.length() > 5000)
        throw std::invalid_argument("invalid camera clearance probe");
      const auto frame = require(
          make_local_tangent_frame(world->planet, world->flight.pose.position));
      const auto fixed = require(
          planet_fixed_from_local(frame, {offset.x, -offset.z, offset.y}));
      const auto pose =
          require(geodetic_from_planet_fixed(world->planet, fixed));
      const auto sample =
          with_relief(require(sample_planet_surface(world->planet, fixed,
                                                    world->lod, world->cache)),
                      world->planet, fixed, world->relief_version);
      last_error = godot::String{};
      return pose.altitude_metres - sample.elevation_metres;
    } catch (const std::exception& e) {
      last_error = e.what();
      return std::numeric_limits<double>::quiet_NaN();
    }
  }

  // Explicit unsaved playtest fixture, never automatic orbit insertion.
  auto start_practice(bool reentry) -> bool {
    try {
      if (!world || !world->lab || !stream) {
        last_error = "Practice requires streaming thrust flight";
        return false;
      }
      if (!set_survey_pose(.25, .4, reentry ? 90000.0 : 250000.0)) return false;
      auto& s = *world->lab;
      const auto orbit = flight_lab::orbit_info(world->planet, s);
      s.velocity = s.back * (-(reentry ? 4500.0 : orbit.circular_speed));
      if (reentry) s.velocity = s.velocity - s.up * 650;
      s.assist = false;
      s.density =
          flight_lab::density(world->planet, reentry ? 90000.0 : 250000.0);
      const double aerodynamic_density =
          s.translation_policy == 1
              ? s.density
              : flight_lab::effective_aerodynamic_density(world->planet, s);
      s.dynamic_pressure =
          .5 * aerodynamic_density * flight_lab::dot(s.velocity, s.velocity);
      world->advance_thrust(0, {0, 0, 0, 0, 0, 0, 0, false});
      last_error = godot::String{};
      return true;
    } catch (const std::exception& e) {
      last_error = e.what();
      return false;
    }
  }
};

auto initialize_bridge(godot::ModuleInitializationLevel level) -> void {
  if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE)
    GDREGISTER_CLASS(FreedomBridge);
}
auto terminate_bridge(godot::ModuleInitializationLevel) -> void {
}
} // namespace apsis_drift::godot_spike

extern "C" GDExtensionBool GDE_EXPORT
apsis_freedom_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                           GDExtensionClassLibraryPtr library,
                           GDExtensionInitialization* initialization) {
  godot::GDExtensionBinding::InitObject init{get_proc_address, library,
                                             initialization};
  init.register_initializer(apsis_drift::godot_spike::initialize_bridge);
  init.register_terminator(apsis_drift::godot_spike::terminate_bridge);
  init.set_minimum_library_initialization_level(
      godot::MODULE_INITIALIZATION_LEVEL_SCENE);
  return init.init();
}
