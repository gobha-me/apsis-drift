#include "../flight_endurance_meter.hpp"
#include "apsis_drift/godot/saved_flight.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>

// An opt-in integration fixture, not a gameplay autopilot. Every movement uses
// the public session commands. Checkpoints are actual Save As/Continue results,
// never practice starts, pose assignments or velocity injection.
namespace {
using namespace apsis_drift;
using V = RigidVector3;
using Json = nlohmann::ordered_json;
auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(V a, double b) -> V {
  return {a.x * b, a.y * b, a.z * b};
}
auto dot(V a, V b) -> double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto length(V a) -> double {
  return std::sqrt(dot(a, a));
}
auto unit(V a) -> V {
  const auto n = length(a);
  if (!std::isfinite(n) || n <= 0)
    throw std::runtime_error("Invalid pilot direction");
  return scale(a, 1 / n);
}
auto rotate(RigidOrientation q, V v) -> V {
  const auto r = godot_spike::rotate_saved(q, {v.x, v.y, v.z});
  return {r.x, r.y, r.z};
}
template <class T, class E>
auto need(std::expected<T, E> value, std::string_view operation) -> T {
  if (!value) throw std::runtime_error(std::string{operation});
  return std::move(*value);
}
auto need(std::expected<void, std::string> value) -> void {
  if (!value) throw std::runtime_error(value.error());
}
auto check(bool value, std::string_view why) -> void {
  if (!value) throw std::runtime_error(std::string{why});
}
auto coordinates(V v) -> Json {
  return Json::array({v.x, v.y, v.z});
}

struct Commands {
  std::ofstream stream;
  std::string operation;
  Json arguments = Json::array();
  unsigned ticks{}, rows{};
  std::size_t bytes{};
  explicit Commands(const std::filesystem::path& output)
      : stream{output / "commands.jsonl"} {}
  auto write(std::string_view op, unsigned count, const Json& args) -> void {
    const auto line = Json::array({op, count, args}).dump() + '\n';
    check(line.size() <= 4096 && rows < 100000 &&
              bytes + line.size() <= std::size_t{32} * 1024 * 1024,
          "Command stream exceeded bounds");
    stream << line;
    check(static_cast<bool>(stream), "Command stream write failed");
    ++rows;
    bytes += line.size();
  }
  auto flush() -> void {
    if (ticks != 0) write(operation, ticks, arguments);
    ticks = 0;
  }
  auto step(std::string_view op, const Json& args) -> void {
    Json encoded = Json::array();
    constexpr std::string_view digits = "0123456789abcdef";
    for (const auto& value : args) {
      const auto bits = std::bit_cast<std::uint64_t>(value.get<double>());
      std::string hex(16, '0');
      for (std::size_t byte = 0; byte < 8; ++byte) {
        const auto octet = (bits >> (byte * 8)) & 255;
        hex[byte * 2] = digits[octet >> 4];
        hex[byte * 2 + 1] = digits[octet & 15];
      }
      encoded.push_back(hex);
    }
    if (operation != op || arguments != encoded || ticks == kMaxCatchUpSteps)
      flush();
    operation = op;
    arguments = std::move(encoded);
    ++ticks;
  }
  auto action(std::string_view op, const Json& args = Json::array()) -> void {
    flush();
    write(op, 0, args);
  }
};

struct Trace {
  NativeFreedomFlightSession session;
  std::filesystem::path output;
  TerrainTileCache cache;
  Json checkpoints = Json::array();
  Commands* commands{};
  std::optional<research::FlightEnduranceMeter> endurance{};
  Json endurance_checkpoints = Json::array();
  auto flight(const NativeFlightControls& c)
      -> std::expected<NativeFlightStep, std::string> {
    auto result = session.advance(c);
    if (result && endurance) {
      endurance->sample(result->actuation.central.propulsion);
      if (session.resources())
        check(!result->propulsion_refused &&
                  session.resources()->flight_quanta ==
                      kFreedomFlightCapacityQuanta - endurance->effort_quanta,
              "Fuel-enabled voyage differs from independent actual-work meter");
    }
    if (result && commands)
      commands->step(
          "flight",
          Json::array({c.positive_translation.x, c.positive_translation.y,
                       c.positive_translation.z, c.negative_translation.x,
                       c.negative_translation.y, c.negative_translation.z,
                       c.positive_rotation.x, c.positive_rotation.y,
                       c.positive_rotation.z, c.negative_rotation.x,
                       c.negative_rotation.y, c.negative_rotation.z}));
    return result;
  }
  auto walk(const OriginWalkControls& c) -> decltype(session.advance_walk(c)) {
    auto result = session.advance_walk(c);
    if (result && commands)
      commands->step("walk",
                     Json::array({c.forward, c.right, c.heading_radians}));
    return result;
  }
  auto action(std::string_view op) -> void {
    if (commands) commands->action(op);
  }

  auto fixed_position() const -> V {
    const auto view = godot_spike::project_saved_flight(session);
    return rotate(
        godot_spike::inverse_saved(view.rotation.geometry.fixed_to_system),
        session.document().flight.position_metres);
  }
  auto terrain_elevation() -> double {
    const auto view = godot_spike::project_saved_flight(session);
    const auto fixed = fixed_position();
    const PlanetFixedPositionMetres p{fixed.x, fixed.y, fixed.z};
    const auto sample = need(sample_planet_surface(view.planet, p, 8, cache),
                             "Terrain sample refused");
    return sample.elevation_metres;
  }
  auto checkpoint(std::string_view label) -> void {
    if (commands) commands->action("checkpoint", Json::array({label}));
    const auto path = output / (std::string{label} + ".json");
    need(session.save_as(path));
    auto continued = need(NativeFreedomFlightSession::open(
                              need(native_continue(path), "Continue refused")),
                          "Continued session refused");
    check(continued.document() == session.document() &&
              continued.boarding() == session.boarding() &&
              continued.walker() == session.walker() &&
              continued.docking() == session.docking() &&
              continued.starting_assembly() == session.starting_assembly() &&
              continued.resources() == session.resources() &&
              continued.knowledge() == session.knowledge() &&
              !continued.port_approach().active,
          "Checkpoint changed the voyage or restored transient guidance");
    session = std::move(continued);
    auto next = session;
    if (next.walker())
      (void)need(next.advance_walk({}), "Expected neutral walking refused");
    else
      (void)need(next.advance({}), "Expected neutral flight refused");
    const auto next_path = output / (std::string{label} + "-next.json");
    need(next.save_as(next_path));
    const auto next_checksum =
        need(rigid_body_state_checksum(RigidBodyWorldContext{next.system()},
                                       next.document().flight),
             "Next checksum refused");
    const auto observed = need(session.observe(), "Observation refused");
    const auto& body = session.document().flight;
    const auto checksum =
        need(rigid_body_state_checksum(RigidBodyWorldContext{session.system()},
                                       body),
             "Checksum refused");
    checkpoints.push_back(
        {{"label", label},
         {"file", path.filename().string()},
         {"tick", std::to_string(body.tick)},
         {"checksum", std::to_string(checksum)},
         {"next_file", next_path.filename().string()},
         {"next_tick", std::to_string(next.document().flight.tick)},
         {"next_checksum", std::to_string(next_checksum)},
         {"position_metres", coordinates(body.position_metres)},
         {"altitude_metres", observed.atmosphere.altitude_metres},
         {"air_speed_metres_per_second",
          observed.atmosphere.air_speed_metres_per_second},
         {"air_density", observed.atmosphere.density_kg_per_cubic_metre},
         {"atmosphere_edge_metres",
          observed.atmosphere.space_boundary_altitude_metres},
         {"terrain_elevation_metres", terrain_elevation()},
         {"attached", session.docking()->attached},
         {"walking", session.walker().has_value()}});
    if (endurance)
      endurance_checkpoints.push_back(
          {{"label", label},
           {"tick", body.tick},
           {"checksum", checksum},
           {"flight_ticks", endurance->ticks},
           {"firing_ticks", endurance->firing_ticks},
           {"effort_quanta", endurance->effort_quanta},
           {"channel_milliunit_ticks", endurance->channel_milliunit_ticks}});
    std::cout << label << " tick=" << body.tick << " checksum=" << checksum
              << '\n'
              << std::flush;
  }
};

// Allocate measured demands through the same twelve signed channels exposed
// to the native controller. Saturation is physical; no correction writes state.
auto controls(const RigidBodyState& body, const CraftFrameProperties& frame,
              const AtmosphericFlightSample& air, V acceleration,
              const std::array<V, 3>& desired_axes) -> NativeFlightControls {
  const auto inverse = godot_spike::inverse_saved(body.orientation);
  V error{};
  constexpr std::array<V, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  for (std::size_t i = 0; i < axes.size(); ++i)
    error =
        add(error, cross(rotate(body.orientation, axes[i]), desired_axes[i]));
  const auto angular = sub(rotate(inverse, scale(error, 1.0)),
                           scale(body.angular_velocity_radians_per_second, 2));
  const auto body_accel =
      sub(rotate(inverse, acceleration),
          scale(add(air.drag_force_body_newtons, air.lift_force_body_newtons),
                1.0 / frame.dry_mass_kg));
  NativeFlightControls result;
  const std::array translation{body_accel.x, body_accel.y, body_accel.z};
  const std::array rotation{angular.x, angular.y, angular.z};
  std::array<double*, 3> positive{&result.positive_translation.x,
                                  &result.positive_translation.y,
                                  &result.positive_translation.z};
  std::array<double*, 3> negative{&result.negative_translation.x,
                                  &result.negative_translation.y,
                                  &result.negative_translation.z};
  std::array<double*, 3> positive_rotation{&result.positive_rotation.x,
                                           &result.positive_rotation.y,
                                           &result.positive_rotation.z};
  std::array<double*, 3> negative_rotation{&result.negative_rotation.x,
                                           &result.negative_rotation.y,
                                           &result.negative_rotation.z};
  for (std::size_t i = 0; i < 3; ++i) {
    *positive[i] = std::clamp(translation[i] * frame.dry_mass_kg /
                                  frame.positive_force_newtons[i],
                              0.0, 1.0);
    *negative[i] = std::clamp(-translation[i] * frame.dry_mass_kg /
                                  frame.negative_force_newtons[i],
                              0.0, 1.0);
    const auto demand = rotation[i] *
                        static_cast<double>(frame.principal_inertia_kg_m2[i]) /
                        frame.torque_newton_metres[i];
    *positive_rotation[i] = std::clamp(demand, 0.0, 1.0);
    *negative_rotation[i] = std::clamp(-demand, 0.0, 1.0);
  }
  return result;
}

auto run(const std::filesystem::path& output, bool record_commands,
         bool measure_endurance) -> void {
  Trace trace{need(NativeFreedomFlightSession::open(
                       need(native_new_game(Seed{42}), "New Game refused")),
                   "New session refused"),
              output,
              need(TerrainTileCache::create(), "Terrain cache refused")};
  if (measure_endurance) trace.endurance.emplace();
  std::optional<Commands> commands;
  if (record_commands) {
    commands.emplace(output);
    trace.commands = &*commands;
  }
  auto& session = trace.session;
  const auto discoveries = session.document().origin.state.discoveries;
  trace.checkpoint("station-start");
  for (int i = 0; i < 2960; ++i)
    (void)need(trace.walk({0, -1, 0}), "Walk refused");
  const auto entry = *session.walker();
  need(session.begin_boarding());
  trace.action("board");
  for (unsigned i = 0; i < kGameplayBoardingTicks; ++i)
    (void)need(trace.walk({}), "Boarding refused");
  const auto original_q = session.document().flight.orientation;
  trace.checkpoint("seated");
  need(session.set_assistance(false));
  trace.action("assistance_off");
  need(session.release_port());
  trace.action("release");
  NativeFlightControls withdrawal;
  withdrawal.negative_translation.y = .25;
  for (int i = 0; i < 720; ++i)
    (void)need(trace.flight(withdrawal), "Withdrawal refused");
  trace.checkpoint("departed");
  const auto station = generate_origin_station(Seed{42});
  const auto geometry =
      need(origin_station_geometry(station), "Geometry refused");
  const auto plane =
      unit(cross(session.document().flight.position_metres,
                 session.document().flight.linear_velocity_metres_per_second));
  const auto frame = need(resolve_craft_frame(session.document().flight.craft),
                          "Frame refused")
                         .properties;
  const double start_altitude =
      need(session.observe(), "Observation refused").atmosphere.altitude_metres;
  int phase{}, cruise_ticks{};
  bool entered{}, exited{}, captured{};
  V cruise_start{}, cruise_end{};
  double minimum_terrain_clearance = start_altitude;
  SimulationTick approach_tick{};
  NativeFlightControls held;
  for (int tick = 0; tick < 120 * 6000; ++tick) {
    // Value copy stays valid when a checkpoint reloads the identical session.
    const auto body = session.document().flight;
    const auto observed = need(session.observe(), "Observation refused");
    const auto p = body.position_metres;
    const auto v = body.linear_velocity_metres_per_second;
    const auto radial = unit(p);
    const auto r = length(p);
    const auto altitude = observed.atmosphere.altitude_metres;
    const auto gravity =
        need(evaluate_central_body_gravity(
                 RigidBodyWorldContext{session.system()}, body),
             "Gravity refused");
    const auto rotation =
        need(resolve_planet_rotation(session.system(), session.rotation(),
                                     body.tick),
             "Rotation refused")
            .geometry;
    const V spin{rotation.angular_velocity_radians_per_second.x,
                 rotation.angular_velocity_radians_per_second.y,
                 rotation.angular_velocity_radians_per_second.z};
    const auto radial_speed = dot(v, radial);
    const auto tangential = sub(v, scale(radial, radial_speed));
    const auto forward = unit(cross(plane, radial));
    const auto dock =
        need(resolve_origin_port_pose(session.system(), station, geometry,
                                      {station.id, 1}, body.tick),
             "Port pose refused")
            .planet_relative;
    auto goal_tangent = add(cross(spin, p), scale(forward, 200));
    double goal_altitude = 25000;
    if (phase == 0 && observed.atmosphere.air_speed_metres_per_second > 1200)
      goal_altitude = start_altitude;
    if (phase == 2) {
      goal_altitude =
          length(dock.position_metres) - gravity.reference_radius_metres;
      if (altitude > 100000)
        goal_tangent =
            scale(forward, r * length(dock.linear_velocity_metres_per_second) /
                               length(dock.position_metres));
    }
    const auto goal_radial_speed =
        std::clamp((goal_altitude - altitude) * .03,
                   altitude < 70000 ? -200.0 : -1200.0, 1200.0);
    auto acceleration =
        sub(add(scale(radial, (goal_radial_speed - radial_speed) * .1 -
                                  dot(tangential, tangential) / r),
                scale(sub(goal_tangent, tangential), .05)),
            gravity.acceleration_metres_per_second_squared);
    auto up = radial;
    auto back = scale(forward, -1);
    auto right = unit(cross(up, back));
    if (phase == 3) {
      const auto goal_position = add(dock.position_metres, {0, -50, 0});
      if (length(sub(goal_position, p)) > 10000) {
        // Follow the planet's arc before closing locally on the moving port.
        const auto goal_radial = unit(goal_position);
        const auto angle = std::atan2(dot(plane, cross(radial, goal_radial)),
                                      dot(radial, goal_radial));
        const auto omega = dot(dock.linear_velocity_metres_per_second,
                               unit(cross(plane, goal_radial))) /
                           length(goal_position);
        const auto goal_velocity = add(
            scale(forward,
                  r * (omega + std::clamp(angle * .008, -.0005, .0005))),
            scale(plane, dot(dock.linear_velocity_metres_per_second, plane)));
        const auto radial_goal =
            std::clamp((length(goal_position) - r) * .03, -1200.0, 1200.0);
        acceleration =
            sub(add(add(scale(radial, (radial_goal - radial_speed) * .1 -
                                          dot(tangential, tangential) / r),
                        scale(sub(goal_velocity, tangential), .05)),
                    scale(plane, dot(sub(goal_position, p), plane) * .0002)),
                gravity.acceleration_metres_per_second_squared);
      } else {
        const auto next =
            need(resolve_origin_port_pose(session.system(), station, geometry,
                                          {station.id, 1}, body.tick + 1),
                 "Next port pose refused")
                .planet_relative;
        const auto station_acceleration =
            scale(sub(next.linear_velocity_metres_per_second,
                      dock.linear_velocity_metres_per_second),
                  120);
        acceleration = sub(
            add(add(scale(sub(goal_position, p), .0008),
                    scale(sub(dock.linear_velocity_metres_per_second, v), .06)),
                station_acceleration),
            gravity.acceleration_metres_per_second_squared);
        up = rotate(original_q, {0, 1, 0});
        right = rotate(original_q, {1, 0, 0});
        back = rotate(original_q, {0, 0, 1});
      }
    }
    check(altitude >= 10000 && (phase != 3 || altitude <= start_altitude * 1.5),
          "Pilot left the bounded flight envelope");
    if (tick % 7200 == 0) {
      const auto clearance = altitude - trace.terrain_elevation();
      minimum_terrain_clearance =
          std::min(minimum_terrain_clearance, clearance);
      check(clearance > 1000, "Sampled terrain clearance lost");
    }
    if (!entered &&
        altitude < observed.atmosphere.space_boundary_altitude_metres) {
      check(observed.atmosphere.density_kg_per_cubic_metre > 0 &&
                radial_speed < 0,
            "Atmospheric entry did not descend through air");
      entered = true;
      trace.checkpoint("atmosphere-entry");
    }
    if (phase == 0 && std::abs(altitude - 25000) < 100 &&
        std::abs(radial_speed) < 10 &&
        observed.atmosphere.air_speed_metres_per_second < 350) {
      check(entered, "Cruise preceded atmospheric entry");
      phase = 1;
      cruise_start = unit(trace.fixed_position());
      trace.checkpoint("cruise-start");
    }
    if (phase == 1 && ++cruise_ticks == 120 * 30) {
      cruise_end = unit(trace.fixed_position());
      phase = 2;
      trace.checkpoint("cruise-end");
    }
    if (phase == 2 && !exited &&
        altitude >= observed.atmosphere.space_boundary_altitude_metres) {
      check(radial_speed > 0, "Space boundary crossed without ascent");
      exited = true;
      trace.checkpoint("space-exit");
    }
    if (phase == 2 && altitude > start_altitude * .8) {
      check(exited, "Intercept preceded physical space exit");
      phase = 3;
      trace.checkpoint("home-intercept");
    }
    if (phase == 3 && session.port_approach_available()) {
      approach_tick = body.tick;
      trace.checkpoint("near-port");
      need(session.begin_port_approach());
      trace.action("approach");
      for (int i = 0; i < 7200; ++i) {
        (void)need(trace.flight({}), "Approach refused");
        if (need(session.assess_port(), "Assessment refused").decision ==
            OriginDockDecision::capture_ready) {
          need(session.capture_port());
          trace.action("capture");
          captured = true;
          break;
        }
      }
      check(captured, "Thruster approach did not reach capture");
      break;
    }
    if (!record_commands || tick % kMaxCatchUpSteps == 0)
      held = controls(body, frame, observed.atmosphere, acceleration,
                      {right, up, back});
    (void)need(trace.flight(held), "Flight step refused");
  }
  check(captured, "Planetary trace exceeded 6000 game seconds");
  trace.checkpoint("captured");
  need(session.begin_disembarking());
  trace.action("unboard");
  for (unsigned i = 0; i < kGameplayBoardingTicks; ++i)
    (void)need(trace.walk({}), "Disembarking refused");
  check(session.walker()->foot_position_metres == entry.foot_position_metres,
        "Return did not restore actual station entry");
  for (int i = 0; i < 120; ++i)
    (void)need(trace.walk({0, .25, 0}), "Returned walking refused");
  check(session.walker()->foot_position_metres != entry.foot_position_metres &&
            session.document().origin.state.discoveries == discoveries,
        "Returned movement failed or fabricated discoveries");
  trace.checkpoint("station-return");
  const auto radius =
      godot_spike::project_saved_flight(session).planet.radius.value * 1000.0;
  const auto cruise_distance =
      radius * std::atan2(length(cross(cruise_start, cruise_end)),
                          dot(cruise_start, cruise_end));
  check(cruise_distance > 5000, "Cruise did not cross five surface kilometres");
  if (commands) commands->flush();
  Json report{{"schema_version", 1},
              {"seed", "42"},
              {"scope",
               "Public-command planetary voyage with exact phase Save "
               "As/Continue; test pilot only, no pose writes, landing, "
               "continuous collision or native/manual acceptance"},
              {"physical_catalog", session.system().generator_version},
              {"physical_ephemeris", session.system().ephemeris_version},
              {"terrain_source_lod", 8},
              {"terrain_relief_version", 0},
              {"minimum_sampled_clearance_metres", minimum_terrain_clearance},
              {"cruise_surface_distance_metres", cruise_distance},
              {"approach_tick", std::to_string(approach_tick)},
              {"final_tick", std::to_string(session.document().flight.tick)},
              {"compiler", __VERSION__},
              {"checkpoints", trace.checkpoints}};
  if (commands)
    report["command_stream"] = {{"file", "commands.jsonl"},
                                {"rows", commands->rows},
                                {"bytes", commands->bytes},
                                {"cadence_ticks", kMaxCatchUpSteps},
                                {"float_encoding", "ieee754-le-hex"}};
  std::ofstream stream{output / "trace.json"};
  stream << report.dump(2) << '\n';
  check(static_cast<bool>(stream), "Trace report write failed");
  if (trace.endurance) {
    const Json measured{
        {"schema_version", 1},
        {"accounting_proposal_version", 1},
        {"scope", "Independent gross propulsion observation of the current "
                  "native session"},
        {"quanta_per_equivalent_newton_second",
         research::kEffortQuantaPerNewtonSecond},
        {"flight_ticks", trace.endurance->ticks},
        {"firing_ticks", trace.endurance->firing_ticks},
        {"effort_quanta", trace.endurance->effort_quanta},
        {"channel_milliunit_ticks", trace.endurance->channel_milliunit_ticks},
        {"checkpoints", trace.endurance_checkpoints}};
    std::ofstream accounting{output / "endurance.json"};
    accounting << measured.dump(2) << '\n';
    check(static_cast<bool>(accounting), "Endurance report write failed");
  }
  std::cout << "Planetary voyage: " << trace.checkpoints.size()
            << " exact checkpoints, surface cruise " << cruise_distance
            << " m, 0 failures\n";
}
} // namespace
int main(int argc, char** argv) {
  try {
    if (argc < 2 || argc > 4)
      throw std::invalid_argument(
          "Expected one absolute empty output "
          "directory and optional --commands/--endurance");
    bool commands{}, endurance{};
    for (int i = 2; i < argc; ++i) {
      const std::string_view option{argv[i]};
      if (option == "--commands" && !commands)
        commands = true;
      else if (option == "--endurance" && !endurance)
        endurance = true;
      else
        throw std::invalid_argument("Unknown or repeated fixture option");
    }
    const std::filesystem::path output{argv[1]};
    if (!output.is_absolute() || output.native().size() > 4000 ||
        !std::filesystem::is_directory(output) ||
        !std::filesystem::is_empty(output))
      throw std::invalid_argument(
          "Expected one bounded absolute empty output directory");
    run(output, commands, endurance);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
