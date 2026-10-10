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
#include <numbers>
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
  std::optional<NativeFreedomFlightSession> uninterrupted{};
  auto flight(const NativeFlightControls& c)
      -> std::expected<NativeFlightStep, std::string> {
    auto result = session.advance(c);
    if (result && uninterrupted)
      (void)need(uninterrupted->advance(c), "Uninterrupted flight refused");
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
    if (result && uninterrupted)
      (void)need(uninterrupted->advance_walk(c), "Uninterrupted walk refused");
    if (result && commands)
      commands->step("walk",
                     Json::array({c.forward, c.right, c.heading_radians}));
    return result;
  }
  auto action(std::string_view op, const Json& args = Json::array()) -> void {
    if (uninterrupted) {
      auto& live = *uninterrupted;
      if (op == "board")
        need(live.begin_boarding());
      else if (op == "unboard")
        need(live.begin_disembarking());
      else if (op == "assistance_off")
        need(live.set_assistance(false));
      else if (op == "release")
        need(live.release_port());
      else if (op == "approach")
        need(live.begin_port_approach());
      else if (op == "capture")
        need(live.capture_port());
      else if (op == "jump_begin")
        need(live.begin_jump());
      else if (op == "port_select")
        need(live.select_port(args.at(0).get<unsigned>()));
      else if (op == "replenish")
        need(live.replenish_resources());
      else if (op == "jump_select") {
        const auto ids = generate_first_intersystem_identities(
            live.document().origin.recipe.universe_seed);
        const auto id = args.at(0).get<std::string>();
        check(id == system_id_string(ids.origin_system) ||
                  id == system_id_string(ids.target_system),
              "Uninterrupted destination is not a seeded endpoint");
        need(live.select_jump(id == system_id_string(ids.origin_system)
                                  ? ids.origin_system
                                  : ids.target_system));
      } else
        check(false, "Unknown uninterrupted semantic action");
    }
    if (commands) commands->action(op, args);
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
              continued.travel() == session.travel() &&
              !continued.port_approach().active,
          "Checkpoint changed the voyage or restored transient guidance");
    if (uninterrupted) {
      check(uninterrupted->document() == session.document() &&
                uninterrupted->surface_document() ==
                    session.surface_document() &&
                uninterrupted->boarding() == session.boarding() &&
                uninterrupted->walker() == session.walker() &&
                uninterrupted->docking() == session.docking() &&
                uninterrupted->starting_assembly() ==
                    session.starting_assembly() &&
                uninterrupted->resources() == session.resources() &&
                uninterrupted->knowledge() == session.knowledge() &&
                uninterrupted->travel() == session.travel(),
            "Checkpoint-resumed route diverged from uninterrupted actual trip");
    }
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
         {"attached", session.docking() && session.docking()->attached},
         {"walking", session.walker().has_value()}});
    if (uninterrupted) {
      auto& row = checkpoints.back();
      row["system_id"] = std::to_string(body.frame.system.value);
      row["station_available"] =
          godot_spike::project_saved_flight(session).station_available;
      constexpr std::array phases{"idle", "spool", "transit"};
      row["jump_phase"] =
          phases[session.travel()
                     ? static_cast<unsigned>(session.travel()->phase)
                     : 0];
      row["jump_charges"] = session.resources()->jump_charges;
      row["flight_quanta"] = std::to_string(session.resources()->flight_quanta);
      row["seated"] = session.boarding() &&
                      session.boarding()->phase == FreedomBoardingPhase::seated;
    }
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
namespace {
auto run_neighbor(const std::filesystem::path& output, bool record_commands,
                  bool pilot, std::optional<IntersystemArrivalQuality> grade)
    -> void {
  Trace trace{need(NativeFreedomFlightSession::open(
                       need(native_new_game(Seed{42}), "New Game refused")),
                   "New session refused"),
              output,
              need(TerrainTileCache::create(), "Terrain cache refused")};
  trace.endurance.emplace();
  trace.uninterrupted.emplace(trace.session);
  std::optional<Commands> commands;
  if (record_commands) {
    commands.emplace(output);
    trace.commands = &*commands;
  }
  auto& session = trace.session;
  const auto ids = generate_first_intersystem_identities(Seed{42});
  trace.checkpoint("station-start");
  for (int i = 0; i < 2960; ++i)
    (void)need(trace.walk({0, -1, 0}), "Walk refused");
  const auto entry = *session.walker();
  need(session.begin_boarding());
  trace.action("board");
  for (unsigned i = 0; i < kGameplayBoardingTicks; ++i)
    (void)need(trace.walk({}), "Seat route refused");
  trace.checkpoint("seated");
  const auto station_q = session.document().flight.orientation;
  if (pilot) {
    need(session.set_assistance(false));
    trace.action("assistance_off");
  }
  need(session.release_port());
  trace.action("release");
  NativeFlightControls withdrawal;
  withdrawal.negative_translation.y = .25;
  for (int i = 0; i < 720; ++i)
    (void)need(trace.flight(withdrawal), "Withdrawal refused");
  trace.checkpoint("departed");
  const auto frame = need(resolve_craft_frame(session.document().flight.craft),
                          "Frame refused")
                         .properties;
  const auto station = generate_origin_station(Seed{42});
  const auto geometry =
      need(origin_station_geometry(station), "Station geometry refused");
  // An explicit test pilot brakes relative to the departure planet. Each
  // correction is real thrust/torque; no momentum/pose assignment or fuel
  // grant.
  const auto initial = session.document().flight;
  const auto plane = unit(cross(initial.position_metres,
                                initial.linear_velocity_metres_per_second));
  unsigned settled{};
  NativeFlightControls held;
  for (unsigned tick = 0; tick < 120 * 1200; ++tick) {
    const auto body = session.document().flight;
    const auto observed = need(session.observe(), "Brake observation refused");
    const auto gravity =
        need(evaluate_central_body_gravity(
                 RigidBodyWorldContext{session.system()}, body),
             "Brake gravity refused");
    check(observed.atmosphere.altitude_metres >
              observed.atmosphere.space_boundary_altitude_metres,
          "Physical braking entered atmosphere before departure");
    if (length(body.linear_velocity_metres_per_second) < 5 &&
        length(body.angular_velocity_radians_per_second) < .005) {
      if (++settled == 240) break;
    } else
      settled = 0;
    if (tick % kMaxCatchUpSteps == 0) {
      const auto acceleration =
          sub(scale(body.linear_velocity_metres_per_second, -.1),
              gravity.acceleration_metres_per_second_squared);
      const auto back = scale(unit(acceleration), -1);
      const auto up = unit(cross(back, plane));
      const auto right = unit(cross(up, back));
      held = controls(body, frame, observed.atmosphere, acceleration,
                      {right, up, back});
    }
    (void)need(trace.flight(held), "Physical source braking refused");
  }
  check(settled == 240, "Source braking exceeded bounded time");
  trace.checkpoint("source-ready");
  Json targeting = Json::array();
  auto steer_grade = [&](SystemId destination) {
    if (!grade) return;
    const auto route = generate_first_universe_route(Seed{42});
    const auto from = session.system().catalog.id == route.origin
                          ? route.origin_position
                          : route.destination_position;
    const auto to = destination == route.origin ? route.origin_position
                                                : route.destination_position;
    const auto heading =
        *grade == IntersystemArrivalQuality::aligned ? 0 : 20'000;
    bool intermediate{};
    unsigned stable{};
    NativeFlightControls correction;
    for (unsigned tick = 0; tick < 120 * 1200; ++tick) {
      const auto body = session.document().flight;
      const auto preview =
          need(session.jump_preview(), "Grade preview refused");
      const auto forward =
          unit({static_cast<double>(to.x - from.x) +
                    (preview.nominal_arrival.x - preview.source_position.x),
                static_cast<double>(to.y - from.y) +
                    (preview.nominal_arrival.y - preview.source_position.y),
                static_cast<double>(to.z - from.z) +
                    (preview.nominal_arrival.z - preview.source_position.z)});
      const V reference = std::abs(forward.y) < .9 ? V{0, 1, 0} : V{1, 0, 0};
      const auto side = unit(cross(forward, reference));
      const auto up = unit(cross(side, forward));
      const auto angle = heading * std::numbers::pi_v<double> / 180000.0;
      const auto target =
          add(scale(forward, std::cos(angle)), scale(side, std::sin(angle)));
      const auto facing = rotate(body.orientation, {0, 0, -1});
      // A real quarter-turn avoids the torque controller's antipodal zero.
      // This is still actuator input, never an orientation assignment.
      if (tick == 0) intermediate = dot(facing, target) < -.8;
      if (intermediate && dot(facing, side) > .95 &&
          length(body.angular_velocity_radians_per_second) < .01)
        intermediate = false;
      if (!intermediate && preview.alignment.quality == *grade &&
          std::abs(preview.alignment.heading_error_millidegrees - heading) <
              1000 &&
          length(body.linear_velocity_metres_per_second) < 5 &&
          length(body.angular_velocity_radians_per_second) < .001) {
        if (++stable == 240) break;
      } else
        stable = 0;
      if (tick % kMaxCatchUpSteps == 0) {
        const auto observed =
            need(session.observe(), "Grade observation refused");
        check(observed.atmosphere.altitude_metres >
                  observed.atmosphere.space_boundary_altitude_metres,
              "Grade correction entered atmosphere");
        const auto gravity =
            need(evaluate_central_body_gravity(
                     RigidBodyWorldContext{session.system()}, body),
                 "Grade gravity refused");
        const auto acceleration =
            sub(scale(body.linear_velocity_metres_per_second, -.1),
                gravity.acceleration_metres_per_second_squared);
        const auto back = scale(intermediate ? side : target, -1);
        const auto right = unit(cross(up, back));
        correction = controls(body, frame, observed.atmosphere, acceleration,
                              {right, unit(cross(back, right)), back});
      }
      (void)need(trace.flight(correction), "Physical grade correction refused");
    }
    check(stable == 240, "Physical grade correction exceeded bounded time");
  };
  auto jump = [&](SystemId destination, std::string_view prefix) {
    need(session.select_jump(destination));
    trace.action("jump_select", Json::array({system_id_string(destination)}));
    steer_grade(destination);
    trace.checkpoint(std::string{prefix} + "-selection");
    const auto charge = session.resources()->jump_charges;
    need(session.begin_jump());
    trace.action("jump_begin");
    trace.checkpoint(std::string{prefix} + "-spool");
    for (SimulationTick t = 1; t <= kJumpSpoolTicks + kJumpTransitTicks; ++t) {
      const auto step = need(trace.flight({}), "Actual jump step refused");
      if (t == kJumpSpoolTicks) {
        check(step.jump_committed &&
                  session.resources()->jump_charges == charge - 1,
              "Actual commitment did not bill exactly one charge");
        if (grade) {
          const auto& frozen = *session.travel()->committed;
          const auto& preview = frozen.preview;
          check(preview.request.profile == IntersystemRuleProfile::pilot &&
                    preview.alignment.quality == *grade && preview.volume &&
                    preview.volume->clear() && frozen.point_assessment.clear(),
                "Actual Pilot commitment lost the intended safe grade");
          const auto aligned = need(
              assess_freedom_jump_distance(preview.distance_metres,
                                           IntersystemArrivalQuality::aligned),
              "Aligned reference refused");
          check(preview.distance.envelope_radius_metres ==
                    *aligned.envelope_radius_metres *
                        (*grade == IntersystemArrivalQuality::aligned ? 1 : 10),
                "Offset commitment did not retain its larger envelope");
          targeting.push_back(
              {{"leg", prefix},
               {"quality", intersystem_arrival_quality_name(*grade)},
               {"heading_millidegrees",
                preview.alignment.heading_error_millidegrees},
               {"drift_basis_points",
                preview.alignment.velocity_error_basis_points},
               {"aligned_radius_metres", *aligned.envelope_radius_metres},
               {"envelope_radius_metres",
                *preview.distance.envelope_radius_metres}});
        }
        trace.checkpoint(std::string{prefix} + "-commit");
      }
      if (t == kJumpSpoolTicks + 1)
        trace.checkpoint(std::string{prefix} + "-transit");
      if (t == kJumpSpoolTicks + kJumpTransitTicks)
        check(step.jump_arrived && session.system().catalog.id == destination,
              "Actual jump did not hand off to selected world");
    }
    trace.checkpoint(std::string{prefix} + "-arrival");
  };
  jump(ids.target_system, "outbound");
  const auto before_observe = session.resources()->flight_quanta;
  for (unsigned i = 0; i < 120; ++i)
    (void)need(trace.flight({}), "Neighbor observation flight refused");
  const auto presence =
      need(query_freedom_knowledge(*session.knowledge(),
                                   {KnowledgeSubjectKind::system,
                                    ids.target_system, ids.target_system.value},
                                   KnowledgeFact::presence,
                                   session.document().flight.tick),
           "Neighbor knowledge refused");
  check(presence &&
            presence->provenance.level == NavigationKnowledgeLevel::visited &&
            !session.docking() && session.resources()->jump_charges == 2 &&
            session.resources()->flight_quanta == before_observe,
        "Neighbor visit duplicated charge, fuel bill or attachment");
  check(!session.replenish_resources(),
        "Neighbor granted phantom free service");
  trace.checkpoint("neighbor-observed");
  jump(ids.origin_system, "return");
  check(session.resources()->jump_charges == 1,
        "Physical return did not preserve reserve charge");
  need(session.select_port(1));
  trace.action("port_select", Json::array({1}));
  bool captured{};
  held = {};
  for (unsigned tick = 0; tick < 120 * 2400; ++tick) {
    const auto body = session.document().flight;
    const auto observed =
        need(session.observe(), "Rendezvous observation refused");
    const auto gravity =
        need(evaluate_central_body_gravity(
                 RigidBodyWorldContext{session.system()}, body),
             "Rendezvous gravity refused");
    check(observed.atmosphere.altitude_metres >
              observed.atmosphere.space_boundary_altitude_metres,
          "Physical rendezvous entered atmosphere");
    const auto dock =
        need(resolve_origin_port_pose(session.system(), station, geometry,
                                      {station.id, 1}, body.tick),
             "Moving port refused")
            .planet_relative;
    const auto goal = add(dock.position_metres, {0, -50, 0});
    const auto p = body.position_metres;
    const auto v = body.linear_velocity_metres_per_second;
    auto acceleration = V{};
    std::array<V, 3> axes;
    if (length(sub(goal, p)) > 10000) {
      const auto r = length(p);
      const auto radial = unit(p);
      const auto orbital_plane = unit(
          cross(dock.position_metres, dock.linear_velocity_metres_per_second));
      const auto forward = unit(cross(orbital_plane, radial));
      const auto target_radial = unit(goal);
      const auto angle =
          std::atan2(dot(orbital_plane, cross(radial, target_radial)),
                     dot(radial, target_radial));
      const auto omega = dot(dock.linear_velocity_metres_per_second,
                             unit(cross(orbital_plane, target_radial))) /
                         length(goal);
      const auto goal_velocity = add(
          scale(forward, r * (omega + std::clamp(angle * .008, -.0005, .0005))),
          scale(orbital_plane,
                dot(dock.linear_velocity_metres_per_second, orbital_plane)));
      const auto radial_speed = dot(v, radial);
      const auto tangent = sub(v, scale(radial, radial_speed));
      const auto radial_goal =
          std::clamp((length(goal) - r) * .03, -1200., 1200.);
      acceleration = sub(
          add(add(scale(radial, (radial_goal - radial_speed) * .1 -
                                    dot(tangent, tangent) / r),
                  scale(sub(goal_velocity, tangent), .05)),
              scale(orbital_plane, dot(sub(goal, p), orbital_plane) * .0002)),
          gravity.acceleration_metres_per_second_squared);
      const auto back = scale(unit(acceleration), -1);
      const auto up = unit(cross(back, orbital_plane));
      axes = {unit(cross(up, back)), up, back};
    } else {
      const auto next =
          need(resolve_origin_port_pose(session.system(), station, geometry,
                                        {station.id, 1}, body.tick + 1),
               "Next moving port refused")
              .planet_relative;
      const auto station_acceleration =
          scale(sub(next.linear_velocity_metres_per_second,
                    dock.linear_velocity_metres_per_second),
                120);
      acceleration = sub(
          add(add(scale(sub(goal, p), .0008),
                  scale(sub(dock.linear_velocity_metres_per_second, v), .06)),
              station_acceleration),
          gravity.acceleration_metres_per_second_squared);
      axes = {rotate(station_q, {1, 0, 0}), rotate(station_q, {0, 1, 0}),
              rotate(station_q, {0, 0, 1})};
    }
    if (session.port_approach_available()) {
      trace.checkpoint("near-port");
      need(session.begin_port_approach());
      trace.action("approach");
      for (unsigned i = 0; i < 7200; ++i) {
        (void)need(trace.flight({}), "Physical approach refused");
        if (need(session.assess_port(), "Port assessment refused").decision ==
            OriginDockDecision::capture_ready) {
          need(session.capture_port());
          trace.action("capture");
          captured = true;
          break;
        }
      }
      break;
    }
    if (tick % kMaxCatchUpSteps == 0)
      held = controls(body, frame, observed.atmosphere, acceleration, axes);
    (void)need(trace.flight(held),
               "Physical moving-station rendezvous refused");
  }
  check(captured, "Physical moving-station rendezvous exceeded bounds");
  trace.checkpoint("captured");
  const auto fuel_before_service = session.resources()->flight_quanta;
  check(session.resources()->jump_charges == 1 &&
            fuel_before_service < kFreedomFlightCapacityQuanta,
        "Composed trip did not pay actual propulsion and two jump charges");
  need(session.replenish_resources());
  trace.action("replenish");
  check(session.resources()->flight_quanta == kFreedomFlightCapacityQuanta &&
            session.resources()->jump_charges == 3,
        "Attached station service did not replenish both pools");
  trace.checkpoint("replenished");
  need(session.begin_disembarking());
  trace.action("unboard");
  for (unsigned i = 0; i < kGameplayBoardingTicks; ++i)
    (void)need(trace.walk({}), "Disembarking refused");
  check(session.walker()->foot_position_metres == entry.foot_position_metres,
        "Physical return lost actual station entry");
  for (unsigned i = 0; i < 120; ++i)
    (void)need(trace.walk({0, .25, 0}), "Returned station walk refused");
  trace.checkpoint("station-return");
  if (commands) commands->flush();
  Json report{{"schema_version", 2},
              {"route", "native-neighbor"},
              {"seed", "42"},
              {"physical_catalog", 2},
              {"physical_ephemeris", 2},
              {"terrain_source_lod", 8},
              {"terrain_relief_version", 0},
              {"profile", pilot ? "pilot" : "assisted"},
              {"origin_system", system_id_string(ids.origin_system)},
              {"neighbor_system", system_id_string(ids.target_system)},
              {"uninterrupted_match", true},
              {"checkpoints", trace.checkpoints},
              {"final_tick", std::to_string(session.document().flight.tick)},
              {"effort_quanta", trace.endurance->effort_quanta},
              {"flight_quanta_before_service", fuel_before_service}};
  if (grade) {
    report["targeting_grade"] = intersystem_arrival_quality_name(*grade);
    report["targeting"] = std::move(targeting);
  }
  if (commands)
    report["command_stream"] = {{"file", "commands.jsonl"},
                                {"rows", commands->rows},
                                {"bytes", commands->bytes},
                                {"cadence_ticks", kMaxCatchUpSteps},
                                {"float_encoding", "ieee754-le-hex"}};
  std::ofstream stream(output / "trace.json");
  stream << report.dump(2) << '\n';
  check(static_cast<bool>(stream), "Composed report write failed");
  std::cout << "Native neighboring round trip: " << trace.checkpoints.size()
            << " phases, effort " << trace.endurance->effort_quanta << "Q\n";
}
} // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2 || argc > 7)
      throw std::invalid_argument(
          "Expected one absolute empty output "
          "directory and optional --commands/--endurance/--neighbor/--pilot/"
          "--aligned/--offset");
    bool commands{}, endurance{}, neighbor{}, pilot{};
    std::optional<IntersystemArrivalQuality> grade;
    for (int i = 2; i < argc; ++i) {
      const std::string_view option{argv[i]};
      if (option == "--commands" && !commands)
        commands = true;
      else if (option == "--endurance" && !endurance)
        endurance = true;
      else if (option == "--neighbor" && !neighbor)
        neighbor = true;
      else if (option == "--pilot" && !pilot)
        pilot = true;
      else if (option == "--aligned" && !grade)
        grade = IntersystemArrivalQuality::aligned;
      else if (option == "--offset" && !grade)
        grade = IntersystemArrivalQuality::offset;
      else
        throw std::invalid_argument("Unknown or repeated fixture option");
    }
    const std::filesystem::path output{argv[1]};
    if (!output.is_absolute() || output.native().size() > 4000 ||
        !std::filesystem::is_directory(output) ||
        !std::filesystem::is_empty(output))
      throw std::invalid_argument(
          "Expected one bounded absolute empty output directory");
    if (pilot && !neighbor)
      throw std::invalid_argument("--pilot requires --neighbor");
    if (grade && (!neighbor || !pilot))
      throw std::invalid_argument(
          "A selected grade requires --neighbor --pilot");
    if (neighbor)
      run_neighbor(output, commands, pilot, grade);
    else
      run(output, commands, endurance);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
