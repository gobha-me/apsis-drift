#include "apsis_drift/godot/flight_guidance.hpp"
#include "apsis_drift/godot/saved_flight.hpp"
#include "apsis_drift/godot/snapshot.hpp"
#include "apsis_drift/godot/streaming.hpp"
#include "apsis_drift/godot/surface_start.hpp"
#include "apsis_drift/godot/thrust_flight.hpp"

#include "apsis_drift/body_ephemeris.hpp"
#include "apsis_drift/craft_environment_assessment.hpp"
#include "apsis_drift/native_profile.hpp"
#include "apsis_drift/native_startup.hpp"
#include "apsis_drift/operating_motion.hpp"
#include "apsis_drift/station_geometry.hpp"

#include <charconv>
#include <filesystem>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
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
  std::set<StreamKey> exported;
  struct PendingFreedomStart {
    std::string id;
    NativeProfileSession profile_session;
    std::unique_ptr<SavedFlightWorld> flight;
    std::unique_ptr<NativeFreedomStationStart> station;
    std::unique_ptr<PlanetStream> stream;
    std::optional<FreedomRecoverySaveDocument> recovery_source;
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
            selected.document))
      throw std::invalid_argument(
          "Selected starting assembly requires staged model readiness");
    if (std::holds_alternative<FreedomFlightSaveDocument>(selected.document) ||
        std::holds_alternative<FreedomDockingSaveDocument>(selected.document) ||
        std::holds_alternative<FreedomJourneySaveDocument>(selected.document)) {
      auto opened = NativeFreedomFlightSession::open(std::move(selected));
      if (!opened) throw std::runtime_error(opened.error());
      (void)project_saved_flight(*opened);
      auto candidate = std::make_unique<SavedFlightWorld>(
          SavedFlightWorld{std::move(*opened), {}, {}, 0});
      stream.reset();
      exported.clear();
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
    stream.reset();
    exported.clear();
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
    godot::ClassDB::bind_method(
        godot::D_METHOD("advance_freedom_walk", "elapsed", "controls"),
        &FreedomBridge::advance_freedom_walk);
    godot::ClassDB::bind_method(godot::D_METHOD("begin_freedom_boarding"),
                                &FreedomBridge::begin_freedom_boarding);
    godot::ClassDB::bind_method(godot::D_METHOD("begin_freedom_disembarking"),
                                &FreedomBridge::begin_freedom_disembarking);
    godot::ClassDB::bind_method(godot::D_METHOD("get_freedom_boarding_state"),
                                &FreedomBridge::get_freedom_boarding_state);
    godot::ClassDB::bind_method(godot::D_METHOD("get_freedom_flight_state"),
                                &FreedomBridge::get_freedom_flight_state);
    godot::ClassDB::bind_method(
        godot::D_METHOD("select_freedom_jump", "system_id"),
        &FreedomBridge::select_freedom_jump);
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
    godot::ClassDB::bind_method(godot::D_METHOD("stow_freedom_landing_gear"),
                                &FreedomBridge::stow_freedom_landing_gear);
    godot::ClassDB::bind_method(godot::D_METHOD("liftoff_freedom_surface"),
                                &FreedomBridge::liftoff_freedom_surface);
    godot::ClassDB::bind_method(godot::D_METHOD("begin_freedom_surface_walk"),
                                &FreedomBridge::begin_freedom_surface_walk);
    godot::ClassDB::bind_method(
        godot::D_METHOD("return_from_freedom_surface_walk"),
        &FreedomBridge::return_from_freedom_surface_walk);
    godot::ClassDB::bind_method(godot::D_METHOD("advance_freedom_surface_walk",
                                                "elapsed", "controls",
                                                "paused"),
                                &FreedomBridge::advance_freedom_surface_walk);
    godot::ClassDB::bind_method(
        godot::D_METHOD("cancel_freedom_surface_maneuver"),
        &FreedomBridge::cancel_freedom_surface_maneuver);
    godot::ClassDB::bind_method(godot::D_METHOD("begin_freedom_port_approach"),
                                &FreedomBridge::begin_freedom_port_approach);
    godot::ClassDB::bind_method(godot::D_METHOD("cancel_freedom_port_approach"),
                                &FreedomBridge::cancel_freedom_port_approach);
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
            selected.document)) {
      auto opened = NativeFreedomFlightSession::open(std::move(selected));
      if (!opened) throw std::runtime_error(opened.error());
      const auto view = project_saved_flight(*opened);
      candidate->flight = std::make_unique<SavedFlightWorld>(
          SavedFlightWorld{std::move(*opened), {}, {}, 0});
      if (project_flight_state(*candidate->flight).is_empty() ||
          (view.station_available &&
           project_station_geometry(view.station).is_empty()) ||
          (candidate->flight->session.walker() &&
           project_walk_state(*candidate->flight).is_empty()))
        throw std::runtime_error("Pending Freedom presentation is unavailable");
      candidate->stream = std::make_unique<PlanetStream>(view.planet, 8, 0);
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
      auto selected = native_new_game(seed);
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
      auto source = saved_flight->session.recovery_document();
      if (!source) throw std::runtime_error(source.error());
      auto candidate = saved_flight->session;
      auto completed = candidate.complete_recovery();
      if (!completed) throw std::runtime_error(completed.error());
      auto replacement = candidate.recovery_document();
      if (!replacement) throw std::runtime_error(replacement.error());
      const auto planet = project_saved_flight(candidate).planet;
      NativeSaveDocument document = std::move(*replacement);
      if (candidate.surface_walk_selected()) {
        auto walking = candidate.surface_walk_document();
        if (!walking) throw std::runtime_error(walking.error());
        document = std::move(*walking);
      }
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
    result["streaming_ready"] = pending->stream != nullptr;
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
            : (project_saved_flight(pending->flight->session).station_available
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
      auto source = saved_flight->session.recovery_document();
      if (!source || *source != *pending->recovery_source) {
        last_error = "Recovery source changed before replacement commit";
        return false;
      }
    }
    stream = std::move(pending->stream);
    exported.clear();
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
      const auto previous_frame = saved_flight->session.document().flight.frame;
      auto candidate = std::make_unique<SavedFlightWorld>(*saved_flight);
      candidate->advance(
          elapsed,
          {fractions.ptr(), static_cast<std::size_t>(fractions.size())},
          paused);
      if (previous_frame != candidate->session.document().flight.frame) {
        auto replacement = std::make_unique<PlanetStream>(
            project_saved_flight(candidate->session).planet, 8, 0);
        stream = std::move(replacement);
        exported.clear();
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
      (void)project_saved_flight(candidate);
      saved_flight->session = std::move(candidate);
      if (reset_actuation) saved_flight->last_step.reset();
      last_error = godot::String{};
      return true;
    } catch (const std::exception& error) {
      last_error = godot::String{error.what()};
      return false;
    }
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
      (void)project_saved_flight(candidate);
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

  static auto project_surface_walk_actor(const SavedFlightWorld& selected)
      -> godot::Dictionary {
    godot::Dictionary result;
    const auto& session = selected.session;
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
    const auto& body = session.document().flight;
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

  auto begin_freedom_boarding() -> bool {
    return change_freedom_port(
        [](NativeFreedomFlightSession& session) {
          return session.begin_boarding();
        },
        true);
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
      (void)project_saved_flight(candidate);
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
      result["planet_id"] = decimal(document.flight.frame.planet->value);
      result["station_id"] = decimal(view.station.id.value);
      result["craft_id"] = decimal(document.origin.state.craft.value);
      result["actor_id"] = decimal(actor.actor_id);
      result["tick"] = decimal(document.flight.tick);
      result["geometry_version"] =
          static_cast<std::int64_t>(actor.geometry_version);
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

  static auto project_flight_state(const SavedFlightWorld& selected)
      -> godot::Dictionary {
    godot::Dictionary result;
    try {
      const auto& session = selected.session;
      const auto& document = session.document();
      const auto& body = document.flight;
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
      result["planet_id"] = decimal(body.frame.planet->value);
      result["surface_walk"] = project_surface_walk_actor(selected);
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
      result["position_metres"] = coordinates(body.position_metres);
      result["velocity_metres_per_second"] =
          coordinates(body.linear_velocity_metres_per_second);
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
      godot::Dictionary chart;
      chart["selected"] = session.knowledge().has_value();
      if (session.knowledge()) {
        const auto& ledger = *session.knowledge();
        std::uint32_t observed_facts{};
        SimulationTick last_observation{};
        for (const auto& entry : ledger.entries) {
          const auto& evidence = entry.transitions.back();
          if (evidence.source != KnowledgeSource::starting_chart) {
            ++observed_facts;
            last_observation = std::max(last_observation, evidence.tick);
          }
        }
        chart["local_sensors"] = ledger.recipe.observation_policy == 1;
        chart["observed_facts"] = observed_facts;
        chart["last_observation_tick"] = decimal(last_observation);
        const auto route =
            generate_first_universe_route(ledger.recipe.universe_seed);
        const auto view = require(resolve_freedom_knowledge_chart(
            ledger, session.system().catalog.id, *session.resources(),
            !session.travel() ||
                session.travel()->phase == FreedomJumpPhase::idle,
            &document.origin));
        godot::Array rows;
        for (const auto& row : view.destinations) {
          godot::Dictionary item;
          item["system_id"] =
              godot::String{system_id_string(row.system).c_str()};
          item["name"] =
              row.system == route.origin ? "Home system" : "Nearby system";
          item["confidence"] = godot::String{
              navigation_knowledge_level_name(row.knowledge).data()};
          item["current"] = row.system == view.current_system;
          item["known"] = row.known;
          item["valid"] = row.valid;
          item["authorized"] =
              row.authorized && session.starting_assembly().has_value();
          item["affordable"] = row.affordable;
          item["available"] = row.available;
          item["selectable"] =
              row.selectable && session.starting_assembly().has_value();
          item["disabled_reason"] = godot::String{
              navigation_disabled_reason_name(row.disabled_reason).data()};
          if (row.distance_metres)
            item["distance_light_seconds"] =
                decimal(route.distance_light_seconds);
          rows.append(item);
        }
        chart["rows"] = rows;
        chart["baseline"] = "Starting chart";
      }
      result["chart"] = chart;
      godot::Dictionary jump;
      const auto& travel = session.travel();
      const auto jump_available = session.jump_available();
      jump["available"] = jump_available.has_value();
      jump["refusal"] = jump_available
                            ? godot::String{}
                            : godot::String{jump_available.error().c_str()};
      jump["phase"] = !travel || travel->phase == FreedomJumpPhase::idle
                          ? "idle"
                      : travel->phase == FreedomJumpPhase::spool ? "spool"
                                                                 : "transit";
      jump["selected"] =
          travel && travel->selected
              ? godot::String{system_id_string(*travel->selected).c_str()}
              : godot::String{};
      jump["remaining_seconds"] =
          travel && travel->phase == FreedomJumpPhase::spool
              ? (kJumpSpoolTicks - (body.tick - travel->spool_tick)) / 120.0
          : travel && travel->phase == FreedomJumpPhase::transit
              ? (travel->committed->preview.arrival_tick - body.tick) / 120.0
              : 0.0;
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
      // No hidden sampled point, destination body identity or catalog leaks.
      result["jump"] = jump;

      const auto force = selected.last_step && !(session.surface() &&
                                                 session.surface()->landed)
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
    } catch (const std::exception&) {
      result.clear();
    }
    return result;
  }

  auto get_freedom_flight_state() const -> godot::Dictionary {
    return saved_flight ? project_flight_state(*saved_flight)
                        : godot::Dictionary{};
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
    const auto& flight = session.document().flight;
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
    result["rating"] = name(assessed->rating);
    result["shielding_thermal"] = name(assessed->axes[0]);
    result["structural"] = name(assessed->axes[1]);
    result["propulsion"] = name(assessed->axes[2]);
    result["operation_supported"] = assessed->operation_supported;
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
    if (saved_flight &&
        project_saved_flight(saved_flight->session).station_available)
      return project_station_geometry(generate_origin_station(
          saved_flight->session.document().origin.recipe.universe_seed));
    return {};
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
      stream.reset();
      exported.clear();
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

  auto enable_streaming() -> bool {
    try {
      if (saved_flight && saved_flight->session.starting_assembly()) {
        if (!stream)
          throw std::runtime_error(
              "Selected assembly requires staged streaming");
        last_error = godot::String{};
        return true;
      }
      if (!world && !saved_flight)
        throw std::runtime_error("initialize C++ world first");
      stream =
          saved_flight
              ? std::make_unique<PlanetStream>(
                    project_saved_flight(saved_flight->session).planet, 8, 0)
              : std::make_unique<PlanetStream>(world->planet, world->lod,
                                               world->relief_version);
      exported.clear();
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

  auto is_stream_busy() const -> bool { return stream && stream->busy(); }

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
            uv.push_back({static_cast<float>(tile->elevations[i]), 0});
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
      exported = std::move(next);
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
