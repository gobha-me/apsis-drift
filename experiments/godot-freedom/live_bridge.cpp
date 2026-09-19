#include "flight_navigation.hpp"
#include "snapshot.hpp"
#include "streaming.hpp"
#include "thrust_flight.hpp"

#include <charconv>
#include <memory>

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <set>

namespace apsis_drift::godot_spike {

// This adapter owns a small experimental flight session, not a second physics
// implementation. All authoritative changes go through existing C++ APIs.
struct LiveWorld {
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

  explicit LiveWorld(const Request& request)
      : planet{generate_planet_descriptor(request.planet_seed)},
        cache{require(TerrainTileCache::create())}, lod{request.lod},
        span_metres{request.span_metres},
        relief_version{request.relief_version} {
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
  godot::String last_error;
  std::unique_ptr<PlanetStream> stream;
  std::set<StreamKey> exported;

 protected:
  static auto _bind_methods() -> void {
    godot::ClassDB::bind_method(godot::D_METHOD("initialize", "snapshot_json"),
                                &FreedomBridge::initialize);
    godot::ClassDB::bind_method(
        godot::D_METHOD("advance", "elapsed", "buttons"),
        &FreedomBridge::advance);
    godot::ClassDB::bind_method(
        godot::D_METHOD("advance_analog", "elapsed", "axes"),
        &FreedomBridge::advance_analog);
    godot::ClassDB::bind_method(godot::D_METHOD("enable_thrust_flight"),
                                &FreedomBridge::enable_thrust_flight);
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
      if (data.at("schema_version") != 1 ||
          data.at("terrain_generator_version") !=
              kTerrainTileGeneratorVersion ||
          data.at("seed_derivation_version") != kSeedDerivationVersion ||
          data.at("planet").at("generator_version") != kPlanetGeneratorVersion)
        throw std::invalid_argument(
            "unsupported generator or snapshot version");
      Request request;
      const auto seed = data.at("planet").at("planet_seed").get<std::string>();
      const auto parsed = std::from_chars(
          seed.data(), seed.data() + seed.size(), request.planet_seed.value);
      if (parsed.ec != std::errc{} || parsed.ptr != seed.data() + seed.size())
        throw std::invalid_argument("invalid planet seed");
      const auto samples = data.at("samples").get<double>();
      if (!std::isfinite(samples) || samples < 2 || samples > 513 ||
          std::trunc(samples) != samples)
        throw std::invalid_argument("invalid sample dimensions");
      request.samples = static_cast<unsigned>(samples);
      request.span_metres = data.at("span_metres").get<double>();
      request.latitude = data.at("frame").at("latitude_radians").get<double>();
      request.longitude =
          data.at("frame").at("longitude_radians").get<double>();
      const auto lod = data.at("lod").get<double>();
      if (!std::isfinite(lod) || lod < 0 || lod > 10 || std::trunc(lod) != lod)
        throw std::invalid_argument("invalid LOD");
      request.lod = static_cast<std::uint8_t>(lod);
      const auto relief = data.value("experimental_relief_version", 0.0);
      if (!std::isfinite(relief) || relief < 0 ||
          relief > kExperimentalReliefVersion || std::trunc(relief) != relief)
        throw std::invalid_argument("unsupported experimental relief version");
      request.relief_version = static_cast<unsigned>(relief);
      validate(request);
      auto candidate = std::make_unique<LiveWorld>(request);
      const auto& reference = data.at("replay").at(0);
      if (reference.at("tick") != 0 ||
          reference.at("checksum") !=
              std::to_string(
                  planetary_flight_state_checksum(candidate->flight)))
        throw std::invalid_argument(
            "snapshot initial state differs from live C++ state");
      stream.reset();
      exported.clear();
      world = std::move(candidate);
      last_error = godot::String{};
      return true;
    } catch (const std::exception& error) {
      last_error = godot::String{error.what()};
      return false;
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
    result["flight_model"] = world->lab ? "thrust-lab-1" : "legacy";
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
      result["climb_rate"] = flight.velocity.up_metres_per_second;
      result["rcs_acceleration"] = godot::Vector3(
          lab.thrust_body.x, lab.thrust_body.y, lab.thrust_body.z);
      result["checksum"] = godot::String{
          ("lab1:" + std::to_string(flight_lab::checksum(lab))).c_str()};
    }
    return result;
  }

  auto enable_streaming() -> bool {
    try {
      if (!world) throw std::runtime_error("initialize C++ world first");
      stream = std::make_unique<PlanetStream>(world->planet, world->lod,
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
      const auto frame = require(
          make_local_tangent_frame(world->planet, world->flight.pose.position));
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
          anchors.size() > kStreamMaxTiles * 2 * 3)
        throw std::invalid_argument("invalid tile anchor buffer");
      for (std::int64_t i = 0; i < anchors.size(); ++i)
        if (!std::isfinite(anchors[i]) || std::abs(anchors[i]) > 1.0e10)
          throw std::invalid_argument("invalid tile anchor coordinate");
      const auto frame = require(
          make_local_tangent_frame(world->planet, world->flight.pose.position));
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
      world->flight = candidate;
      if (world->lab) {
        world->lab = flight_lab::initial(world->planet, pose, 0);
        world->lab->clearance = candidate.clearance_metres;
        world->lab->density = flight_lab::density(world->planet, altitude);
      }
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
      s.dynamic_pressure =
          .5 * s.density * flight_lab::dot(s.velocity, s.velocity);
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
