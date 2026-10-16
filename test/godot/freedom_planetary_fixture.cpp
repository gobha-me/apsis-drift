#include "../../src/godot/contact_geometry_internal.hpp"
#include "../flight_endurance_meter.hpp"
#include "apsis_drift/godot/saved_flight.hpp"
#include "apsis_drift/terrain_local_detail.hpp"
#include "apsis_drift/terrain_sea_surface.hpp"
#include "apsis_drift/terrain_touchdown.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <numbers>
#include <optional>
#include <stdexcept>

// An opt-in integration fixture, not a gameplay autopilot. Every movement uses
// the public session commands. Checkpoints are actual Save As/Continue results,
// never practice starts, pose assignments or velocity injection.
// --system-frame explicitly selects the existing station starter's frame
// policy; it neither changes ordinary New Game nor initializes a ground
// arrival. The separate --terrain-feature mode explicitly initializes a
// measurement viewpoint; it is neither a flown arrival nor the default voyage
// evidence.
namespace {
using namespace apsis_drift;
using V = RigidVector3;
using Json = nlohmann::ordered_json;
enum class PhysicalFlightProfile : std::uint8_t { assisted, advanced };
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
  bool ground_route{};
  std::optional<PhysicalFlightProfile> profile{};
  bool continuation_route{};
  SimulationTick continuation_initial_tick{};
  bool continuation_assistance{true};
  std::uint64_t initial_flight_quanta{kFreedomFlightCapacityQuanta};
  auto flight(const NativeFlightControls& c)
      -> std::expected<NativeFlightStep, std::string> {
    auto result = session.advance(c);
    if (!result && continuation_route)
      throw std::runtime_error("Continuation flight refused at tick " +
                               std::to_string(session.document().flight.tick) +
                               ": " + result.error());
    if (result && uninterrupted)
      (void)need(uninterrupted->advance(c), "Uninterrupted flight refused");
    if (result && continuation_route && result->propulsion_refused)
      throw std::runtime_error(
          "Continuation propulsion refused at tick " +
          std::to_string(session.document().flight.tick) +
          "; remaining fuel Q=" +
          std::to_string(session.resources()->flight_quanta));
    if (result && endurance) {
      endurance->sample(result->actuation.central.propulsion);
      if (session.resources())
        check(!result->propulsion_refused &&
                  endurance->effort_quanta <= initial_flight_quanta &&
                  session.resources()->flight_quanta ==
                      initial_flight_quanta - endurance->effort_quanta,
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
  auto surface_walk(const OriginWalkControls& c)
      -> decltype(session.advance_surface_walk(c)) {
    auto result = session.advance_surface_walk(c);
    if (result && uninterrupted)
      (void)need(uninterrupted->advance_surface_walk(c),
                 "Uninterrupted ground walk refused");
    if (result && commands)
      commands->step("surface_walk",
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
      else if (op == "assistance_on")
        need(live.set_assistance(true));
      else if (op == "hold_on")
        need(live.hold_current_orbit());
      else if (op == "hold_off")
        need(live.set_hold({}));
      else if (op == "land")
        need(live.request_landing());
      else if (op == "surface_exit")
        need(live.begin_surface_walk());
      else if (op == "surface_return")
        need(live.return_from_surface_walk());
      else if (op == "liftoff")
        need(live.release_surface());
      else if (op == "gear_stow")
        need(live.set_landing_gear(false));
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
        need(session.relative_state(), "Relative position refused")
            .position_metres);
  }
  auto terrain_elevation() -> double {
    const auto view = godot_spike::project_saved_flight(session);
    const auto fixed = fixed_position();
    const PlanetFixedPositionMetres p{fixed.x, fixed.y, fixed.z};
    const auto& system = session.system();
    const auto sampler =
        need(TerrainLocalDetailSampler::create(
                 system, body_id(view.planet.id),
                 {need(terrain_surface_raw_recipe(
                           session.document().model.terrain_policy),
                       "Selected terrain raw recipe refused")},
                 cache),
             "Selected terrain provider refused");
    const auto raw = need(sampler.sample_height({p.x, p.y, p.z}, 8),
                          "Terrain height refused");
    return need(select_terrain_sea_surface(
                    session.document().model.terrain_policy,
                    raw.elevation_metres, view.planet.water_coverage.value > 0),
                "Selected terrain top refused")
        .elevation_metres;
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
              continued.surface_walker() == session.surface_walker() &&
              continued.surface_walk_selected() ==
                  session.surface_walk_selected() &&
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
                uninterrupted->surface_walker() == session.surface_walker() &&
                uninterrupted->surface_walk_selected() ==
                    session.surface_walk_selected() &&
                uninterrupted->docking() == session.docking() &&
                uninterrupted->starting_assembly() ==
                    session.starting_assembly() &&
                uninterrupted->resources() == session.resources() &&
                uninterrupted->knowledge() == session.knowledge() &&
                uninterrupted->travel() == session.travel(),
            "Checkpoint-resumed route diverged from uninterrupted actual trip");
      if (ground_route) {
        const auto original_path =
            output / (std::string{label} + "-uninterrupted.json");
        need(uninterrupted->save_as(original_path));
        std::ifstream original{original_path}, restored{path};
        const std::string original_bytes{
            std::istreambuf_iterator<char>{original}, {}};
        const std::string restored_bytes{
            std::istreambuf_iterator<char>{restored}, {}};
        check(!original_bytes.empty() && original_bytes == restored_bytes,
              "Complete uninterrupted ground voyage Save differs");
      }
    }
    session = std::move(continued);
    auto next = session;
    if (next.surface_walker())
      (void)need(next.advance_surface_walk({}),
                 "Expected neutral ground walk refused");
    else if (next.walker())
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
    if (continuation_route)
      check(body.tick >= continuation_initial_tick &&
                body.tick - continuation_initial_tick <= 750'000 &&
                session.document().model.assistance == continuation_assistance,
            "Continuation changed profile or exceeded its750000 elapsed-tick "
            "budget");
    const bool selected = session.document().model.frame_policy ==
                          kFreedomReleaseSystemFramePolicy;
    const bool system_phase =
        label == "departed" || label == "atmosphere-entry" ||
        label == "cruise-start" || label == "cruise-end" ||
        label == "landing-ready" || label == "surface-airborne" ||
        label == "space-exit" || label == "home-intercept" ||
        label == "near-port" || label == "orbit-before-hold" ||
        label == "orbit-hold-selected" || label == "orbit-hold-running" ||
        label == "orbit-hold-disabled";
    if (selected && !continuation_route)
      check(body.frame.kind == (system_phase
                                    ? RigidFrameKind::system_inertial
                                    : RigidFrameKind::planet_relative_inertial),
            "Selected surface boundary has the wrong canonical frame");
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
    if (selected || continuation_route) {
      auto& row = checkpoints.back();
      if (continuation_route)
        check(
            (session.document().model.frame_policy ==
                 kFreedomRelativeFramePolicy &&
             body.frame.kind == RigidFrameKind::planet_relative_inertial) ||
                (selected &&
                 (body.frame.kind == RigidFrameKind::system_inertial ||
                  body.frame.kind == RigidFrameKind::planet_relative_inertial)),
            "Continuation checkpoint has an unsupported canonical frame");
      row["coordinate_frame"] =
          (continuation_route
               ? body.frame.kind == RigidFrameKind::system_inertial
               : system_phase)
              ? "system_inertial"
              : "planet_relative_inertial";
    }
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
      if (ground_route) {
        row["surface_walking"] = session.surface_walker().has_value();
        row["landed"] = session.surface() && session.surface()->landed;
      }
    }
    if (profile) {
      auto& row = checkpoints.back();
      const bool assisted = *profile == PhysicalFlightProfile::assisted;
      check(session.document().model.assistance ==
                (label == "station-start" || label == "seated" || assisted),
            "Physical profile silently changed assistance");
      row["assistance"] = session.document().model.assistance;
      row["hold_enabled"] = session.document().model.hold.target.has_value();
      constexpr std::array classes{"stable", "decaying", "impact", "escape"};
      row["orbit_classification"] =
          classes[static_cast<unsigned>(observed.orbit.classification)];
      if (label == "manual-gear" || label == "landed")
        check(!session.document().model.hold.target &&
                  session.surface_maneuver().kind ==
                      NativeSurfaceManeuverKind::off,
              "Physical contact retained orbit hold or an unexpected aid");
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
              const std::array<V, 3>& desired_axes, bool assistance = false)
    -> NativeFlightControls {
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
  const auto rate = rotate(inverse, error);
  const std::array desired_rate{rate.x, rate.y, rate.z};
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
    // Assisted fractions request angular rates; Advanced fractions request
    // torque. Reuse the same attitude error and authoritative frame ratings.
    const auto demand =
        assistance
            ? desired_rate[i] /
                  (frame.max_angular_rate_milliradians_per_second[i] * .001)
            : rotation[i] *
                  static_cast<double>(frame.principal_inertia_kg_m2[i]) /
                  frame.torque_newton_metres[i];
    *positive_rotation[i] = std::clamp(demand, 0.0, 1.0);
    *negative_rotation[i] = std::clamp(-demand, 0.0, 1.0);
  }
  return result;
}

// A test pilot prospectively inspects actual nearby top geometry. This never
// assigns the live craft's pose or grants player knowledge of the chosen site.
auto landing_goal(Trace& trace) -> LandedCraftAnchor {
  const auto view = godot_spike::project_saved_flight(trace.session);
  const auto position = trace.fixed_position();
  const auto relative = need(trace.session.relative_state(),
                             "Prospective relative source refused");
  const auto radial = unit(position);
  const auto right =
      unit(cross(radial, std::abs(radial.y) < .9 ? V{0, 1, 0} : V{1, 0, 0}));
  const auto back = unit(cross(right, radial));
  const auto radius = static_cast<double>(view.planet.radius.value) * 1000;
  for (int ring = 0; ring <= 4; ++ring) {
    for (int x = -ring; x <= ring; ++x) {
      for (int z = -ring; z <= ring; ++z) {
        if (std::max(std::abs(x), std::abs(z)) != ring) continue;
        const auto aim = add(
            position, add(scale(right, x * 4000.0), scale(back, z * 4000.0)));
        const auto id = godot_spike::detail::locate_selected_contact_triangle(
            view.planet, {aim.x, aim.y, aim.z});
        if (!id) continue;
        const auto triangle =
            godot_spike::detail::build_selected_contact_triangle(
                trace.session.system(), view.planet,
                *godot_spike::saved_contact_recipe(
                    trace.session.document().model.terrain_policy),
                *id, trace.cache);
        if (!triangle) continue;
        const auto a = triangle->vertices[0], b = triangle->vertices[1],
                   c = triangle->vertices[2];
        const V centre{a.x + .3 * ((b.x - a.x) + (c.x - a.x)),
                       a.y + .3 * ((b.y - a.y) + (c.y - a.y)),
                       a.z + .3 * ((b.z - a.z) + (c.z - a.z))};
        const V n{triangle->outward_normal.x, triangle->outward_normal.y,
                  triangle->outward_normal.z};
        if (length(centre) < radius + 5 || dot(unit(centre), n) < .995)
          continue;
        const auto w = 1 + n.y, magnitude = std::hypot(w, n.z, n.x);
        if (magnitude < 1e-8) continue;
        auto fixed =
            need(reframe_rigid_body({trace.session.system()}, relative,
                                    {{RigidFrameKind::planet_fixed,
                                      trace.session.system().catalog.id,
                                      view.planet.id,
                                      {}},
                                     trace.session.document().flight.tick},
                                    trace.session.rotation()),
                 "Prospective fixed frame refused");
        fixed.orientation = {w / magnitude, n.z / magnitude, 0,
                             -n.x / magnitude};
        fixed.position_metres = sub(add(centre, scale(n, -.05)),
                                    rotate(fixed.orientation, {0, -2.08, -.7}));
        fixed.linear_velocity_metres_per_second = {};
        fixed.angular_velocity_radians_per_second = {};
        const auto source =
            need(reframe_rigid_body({trace.session.system()}, fixed,
                                    {relative.frame, fixed.tick},
                                    trace.session.rotation()),
                 "Prospective inertial frame refused");
        const auto checksum =
            need(rigid_body_state_checksum({trace.session.system()}, source),
                 "Prospective checksum refused");
        const auto anchor = prepare_landed_craft(
            trace.session.system(), trace.session.rotation(), source, true,
            checksum, trace.session.document().model.terrain_policy);
        if (!anchor) continue;
        auto ground = PlanetSurfaceWalkTerrain::create(trace.session.system(),
                                                       trace.session.rotation(),
                                                       *anchor, fixed.tick);
        if (!ground || !ground->entry()) continue;
        return *anchor;
      }
    }
  }
  throw std::runtime_error(
      "No supported dry ground access in bounded nearby site inspection");
}

auto surface_start(bool system_frame) -> NativeStartup {
  auto selected = need(native_new_game(Seed{42}), "New Game refused");
  if (system_frame) {
    auto& knowledge = std::get<FreedomKnowledgeSaveDocument>(selected.document);
    auto& assembly = std::get<FreedomStartingAssemblySaveDocument>(
        knowledge.voyage.voyage.base);
    auto& flight = assembly.journey.voyage.flight;
    check(flight.model.frame_policy == kFreedomRelativeFramePolicy,
          "Ordinary New Game changed frame policy");
    flight.model.frame_policy = kFreedomReleaseSystemFramePolicy;
    flight.world = FreedomActiveWorldSelection{1, flight.flight.frame.system,
                                               *flight.flight.frame.planet};
    // Explicit ownership selects its existing knowledge recipe/domain. Keep
    // every granted entry, craft, actor, pose, resource and history unchanged.
    knowledge.knowledge.recipe.version = kFreedomTravelKnowledgeVersion;
    knowledge.knowledge.recipe.world_domain = 1;
  }
  return selected;
}

auto run(const std::filesystem::path& output, bool record_commands,
         bool measure_endurance, bool surface_loop = false,
         bool system_frame = false,
         std::optional<PhysicalFlightProfile> profile = std::nullopt) -> void {
  Trace trace{
      need(NativeFreedomFlightSession::open(surface_start(system_frame)),
           "New session refused"),
      output, need(TerrainTileCache::create(), "Terrain cache refused")};
  if (measure_endurance || surface_loop) trace.endurance.emplace();
  if (surface_loop) trace.uninterrupted.emplace(trace.session);
  trace.ground_route = surface_loop;
  trace.profile = profile;
  const bool assisted_profile = profile == PhysicalFlightProfile::assisted;
  const bool manual_profile = profile == PhysicalFlightProfile::advanced;
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
  need(session.set_assistance(assisted_profile));
  trace.action(assisted_profile ? "assistance_on" : "assistance_off");
  need(session.release_port());
  trace.action("release");
  NativeFlightControls withdrawal;
  withdrawal.negative_translation.y = .25;
  for (int i = 0; i < 720; ++i)
    (void)need(trace.flight(withdrawal), "Withdrawal refused");
  trace.checkpoint("departed");
  unsigned hold_active_ticks{}, hold_force_ticks{};
  if (profile) {
    const auto orbit = need(session.observe(), "Flown orbit query refused");
    check(orbit.orbit.classification == OrbitClassification::stable &&
              orbit.orbit.bound &&
              orbit.atmosphere.altitude_metres >
                  orbit.atmosphere.space_boundary_altitude_metres,
          "Actual departed orbit is not stable above air; no initialized orbit "
          "or automatic insertion");
    const auto target = session.current_orbit_hold_target();
    if (!target)
      throw std::runtime_error("Actual departed hold selection refused: " +
                               target.error());
    trace.checkpoint("orbit-before-hold");
    const auto before_hold = session.document();
    const auto resources = session.resources();
    auto expected = before_hold;
    expected.model.hold = {kOrbitHoldVersion, *target};
    need(session.hold_current_orbit());
    trace.action("hold_on");
    check(session.document() == expected && session.resources() == resources,
          "Public hold selection changed more than the saved request");
    trace.checkpoint("orbit-hold-selected");
    for (unsigned step = 0; step < 120; ++step) {
      const auto result =
          need(trace.flight({}), "Actual requested hold refused");
      if (assisted_profile) {
        check(result.hold.status == OrbitHoldStatus::active ||
                  result.hold.status == OrbitHoldStatus::saturated,
              "Assisted flown hold did not allocate bounded correction");
        ++hold_active_ticks;
        if (result.applied_hold_force_body_newtons != V{}) ++hold_force_ticks;
      } else
        check(result.hold.status == OrbitHoldStatus::paused_advanced &&
                  result.applied_hold_force_body_newtons == V{} &&
                  result.fuel_debit_quanta == 0,
              "Advanced neutral hold request did not retain passive flight");
    }
    if (assisted_profile)
      check(hold_force_ticks > 0 &&
                session.resources()->flight_quanta < resources->flight_quanta,
            "Assisted flown hold lost actual correction or fuel debit");
    trace.checkpoint("orbit-hold-running");
    const auto held_body = session.document().flight;
    need(session.set_hold({}));
    trace.action("hold_off");
    check(session.document().flight == held_body &&
              !session.document().model.hold.target,
          "Hold disable changed the physical body");
    trace.checkpoint("orbit-hold-disabled");
  }
  const auto station = generate_origin_station(Seed{42});
  const auto geometry =
      need(origin_station_geometry(station), "Geometry refused");
  const auto departure =
      need(session.relative_state(), "Departure observation refused");
  const auto plane = unit(cross(departure.position_metres,
                                departure.linear_velocity_metres_per_second));
  const auto frame = need(resolve_craft_frame(session.document().flight.craft),
                          "Frame refused")
                         .properties;
  const double start_altitude =
      need(session.observe(), "Observation refused").atmosphere.altitude_metres;
  int phase{}, cruise_ticks{};
  bool entered{}, exited{}, captured{};
  bool manual_descent{}, controls_dirty{}, manual_contact_propulsion{};
  V cruise_start{}, cruise_end{};
  double minimum_terrain_clearance = start_altitude;
  SimulationTick approach_tick{};
  NativeFlightControls held;
  std::optional<LandedCraftAnchor> ground_goal;
  const int pilot_seconds = surface_loop ? 9000 : 6000;
  for (int tick = 0; tick < 120 * pilot_seconds - (profile ? 120 : 0); ++tick) {
    // Value copy stays valid when a checkpoint reloads the identical session.
    const auto body =
        need(session.relative_state(), "Pilot relative state refused");
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
        auto goal_velocity = add(
            scale(forward,
                  r * (omega + std::clamp(angle * .008, -.0005, .0005))),
            scale(plane, dot(dock.linear_velocity_metres_per_second, plane)));
        if (surface_loop) {
          // A ground stop changes the departure orbit plane. Follow the
          // shortest current great-circle arc rather than the old plane.
          const auto arc = cross(radial, goal_radial);
          const auto separation =
              std::atan2(length(arc), dot(radial, goal_radial));
          const auto station_spin =
              scale(cross(goal_radial, dock.linear_velocity_metres_per_second),
                    1 / length(goal_position));
          goal_velocity = cross(station_spin, p);
          if (length(arc) > 1e-10)
            goal_velocity = add(goal_velocity,
                                scale(cross(unit(arc), radial),
                                      std::min(3000.0, r * separation * .008)));
        }
        const auto radial_goal =
            std::clamp((length(goal_position) - r) * .03, -1200.0, 1200.0);
        acceleration = sub(
            add(add(scale(radial, (radial_goal - radial_speed) * .1 -
                                      dot(tangential, tangential) / r),
                    scale(sub(goal_velocity, tangential), .05)),
                surface_loop
                    ? V{}
                    : scale(plane, dot(sub(goal_position, p), plane) * .0002)),
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
    if (phase == 4) {
      const auto normal =
          rotate(rotation.fixed_to_system,
                 rotate(ground_goal->fixed.orientation, {0, 1, 0}));
      const auto target =
          rotate(rotation.fixed_to_system,
                 add(ground_goal->fixed.position_metres,
                     scale(rotate(ground_goal->fixed.orientation, {0, 1, 0}),
                           manual_descent ? 0 : 12)));
      const auto target_velocity = cross(spin, target);
      const auto error = sub(target, p);
      const auto desired_velocity = add(
          target_velocity,
          scale(error, std::min(.03, 200.0 / std::max(1.0, length(error)))));
      acceleration = sub(add(scale(sub(desired_velocity, v), .1),
                             cross(spin, cross(spin, target))),
                         gravity.acceleration_metres_per_second_squared);
      up = normal;
      right = rotate(rotation.fixed_to_system,
                     rotate(ground_goal->fixed.orientation, {1, 0, 0}));
      back = rotate(rotation.fixed_to_system,
                    rotate(ground_goal->fixed.orientation, {0, 0, 1}));
      const bool ready =
          length(error) < .5 && length(sub(v, cross(spin, p))) < .3 &&
          dot(rotate(body.orientation, {0, 1, 0}), up) > .999 &&
          length(sub(body.angular_velocity_radians_per_second,
                     rotate(godot_spike::inverse_saved(body.orientation),
                            spin))) < .002;
      if (manual_profile && !manual_descent && ready) {
        trace.checkpoint("landing-ready");
        need(session.request_landing());
        trace.action("land");
        check(!session.document().model.assistance &&
                  session.surface()->gear_deployed &&
                  !session.surface()->landed &&
                  session.surface_maneuver().kind ==
                      NativeSurfaceManeuverKind::off,
              "Advanced manual gear selected an assisted landing");
        trace.checkpoint("manual-gear");
        manual_descent = true;
        controls_dirty = true;
        continue;
      }
      if ((manual_profile && manual_descent && session.surface()->landed) ||
          (!manual_profile && ready)) {
        if (manual_profile)
          check(
              manual_contact_propulsion &&
                  !session.document().model.assistance &&
                  session.surface_maneuver().kind ==
                      NativeSurfaceManeuverKind::off,
              "Advanced actual contact lost manual thrust or selected an aid");
        else {
          trace.checkpoint("landing-ready");
          need(session.set_assistance(true));
          trace.action("assistance_on");
          need(session.request_landing());
          trace.action("land");
          for (unsigned step = 0; step < 7200 && !session.surface()->landed;
               ++step)
            (void)need(trace.flight({}), "Actual landing aid refused");
          check(session.surface()->landed.has_value(),
                "Actual landing did not finish");
        }
        trace.checkpoint("landed");
        const auto landed_anchor = session.surface()->landed;
        const auto fuel = session.resources()->flight_quanta;
        need(session.begin_surface_walk());
        trace.action("surface_exit");
        trace.checkpoint("surface-exited");
        for (unsigned step = 0; step < 600; ++step)
          (void)need(trace.surface_walk({-1, 0, 0}),
                     "Actual ground walk refused");
        trace.checkpoint("surface-halfway");
        for (unsigned step = 0; step < 600; ++step)
          (void)need(trace.surface_walk({-1, 0, 0}),
                     "Continued ground walk refused");
        check(!session.return_from_surface_walk(),
              "Distant return unexpectedly accepted");
        trace.checkpoint("surface-away");
        for (unsigned step = 0; step < 1200; ++step)
          (void)need(trace.surface_walk({1, 0, 0}),
                     "Actual ground return refused");
        check(session.surface()->landed == landed_anchor &&
                  session.resources()->flight_quanta == fuel,
              "Walking changed anchor or flight quantity");
        trace.checkpoint("surface-back");
        need(session.return_from_surface_walk());
        trace.action("surface_return");
        trace.checkpoint("surface-seated");
        need(session.release_surface());
        trace.action("liftoff");
        bool lift_propulsion{};
        for (unsigned step = 0;
             step < 1200 &&
             session.surface_maneuver().kind != NativeSurfaceManeuverKind::off;
             ++step) {
          const auto result =
              need(trace.flight({}), "Actual thruster liftoff refused");
          lift_propulsion =
              lift_propulsion ||
              result.actuation.central.propulsion.positive_force_newtons.y > 0;
        }
        check(!session.surface()->landed && session.surface_maneuver().kind ==
                                                NativeSurfaceManeuverKind::off,
              "Actual liftoff did not finish");
        if (profile) {
          auto snapshot = need(TerrainTouchdownSnapshot::create(session, true),
                               "Actual airborne pad snapshot refused");
          const auto query = snapshot.query();
          bool clear =
              query.assessment &&
              query.assessment->classification == TouchdownClass::pads_clear &&
              query.assessment->support_count > 0;
          for (unsigned i = 0; clear && i < query.assessment->support_count;
               ++i)
            clear = query.supports[i] && *query.supports[i] &&
                    (**query.supports[i]).minimum_gap_metres >= 3;
          check(lift_propulsion && clear &&
                    session.surface_maneuver().note ==
                        "Airborne; manual flight resumes with gear deployed",
                "Profile liftoff needs actual thrust, certified clear pads and "
                "successful aid completion");
        }
        need(session.set_landing_gear(false));
        trace.action("gear_stow");
        need(session.set_assistance(assisted_profile));
        trace.action(assisted_profile ? "assistance_on" : "assistance_off");
        trace.checkpoint("surface-airborne");
        phase = 2;
        continue;
      }
    }
    const bool near_surface =
        surface_loop && (phase == 4 || (phase == 2 && ground_goal));
    check((near_surface ? altitude >= 0 : altitude >= 10000) &&
              (phase != 3 || altitude <= start_altitude * 1.5),
          "Pilot left the bounded flight envelope");
    if (phase != 4 && tick % 7200 == 0) {
      const auto clearance = altitude - trace.terrain_elevation();
      minimum_terrain_clearance =
          std::min(minimum_terrain_clearance, clearance);
      check(clearance > (near_surface ? 0 : 1000),
            "Sampled terrain clearance lost");
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
      phase = surface_loop ? 4 : 2;
      trace.checkpoint("cruise-end");
      if (surface_loop) ground_goal = landing_goal(trace);
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
    if (!record_commands || tick % kMaxCatchUpSteps == 0 || controls_dirty) {
      held = controls(body, frame, observed.atmosphere, acceleration,
                      {right, up, back},
                      profile && session.document().model.assistance);
      controls_dirty = false;
    }
    const auto result = need(trace.flight(held), "Flight step refused");
    if (manual_profile && manual_descent && phase == 4)
      manual_contact_propulsion =
          manual_contact_propulsion || result.fuel_debit_quanta > 0;
  }
  if (!captured) {
    const auto available = session.port_approach_available();
    const auto observed = need(session.observe(), "Return observation refused");
    const auto port = need(session.assess_port(), "Return assessment refused");
    std::cerr << "Return status: tick=" << session.document().flight.tick
              << " altitude=" << observed.atmosphere.altitude_metres
              << " collar=" << port.separation_metres
              << " fuel=" << session.resources()->flight_quanta
              << " approach=" << (available ? "ready" : available.error())
              << '\n';
  }
  check(captured, "Planetary trace exceeded its bounded pilot duration");
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
  Json report{{"schema_version", surface_loop ? 4 : 1},
              {"seed", "42"},
              {"scope",
               surface_loop
                   ? "Public-command station/landing/walking/liftoff/return "
                     "voyage with exact Save/Continue; instant hatch transfer, "
                     "test pilot only; no manual/controller qualification"
                   : "Public-command planetary voyage with exact phase Save "
                     "As/Continue; test pilot only, no pose writes, landing, "
                     "continuous collision or native/manual acceptance"},
              {"physical_catalog", session.system().generator_version},
              {"physical_ephemeris", session.system().ephemeris_version},
              {"terrain_source_lod", 8},
              {"terrain_relief_version", 0},
              {"terrain_policy", session.document().model.terrain_policy},
              {"minimum_sampled_clearance_metres", minimum_terrain_clearance},
              {"cruise_surface_distance_metres", cruise_distance},
              {"approach_tick", std::to_string(approach_tick)},
              {"final_tick", std::to_string(session.document().flight.tick)},
              {"compiler", __VERSION__},
              {"checkpoints", trace.checkpoints}};
  if (surface_loop) {
    report["route"] = "surface-loop";
    report["uninterrupted_match"] = true;
  }
  if (system_frame) {
    report["schema_version"] = 5;
    report["route"] = "selected-surface-loop";
    report["frame_policy"] = kFreedomReleaseSystemFramePolicy;
    report["scope"] =
        "Explicit policy-2 selection of the existing full station starter; "
        "physical landing/walking/liftoff/return with exact Save/Continue; "
        "no ground initialization or ordinary New Game default change; "
        "test pilot only, no manual/controller qualification";
  }
  if (profile) {
    report["schema_version"] = 6;
    report["route"] = "physical-profile";
    report["flight_profile"] = assisted_profile ? "assisted" : "advanced";
    report["hold_ticks"] = 120;
    report["hold_active_ticks"] = hold_active_ticks;
    report["hold_force_ticks"] = hold_force_ticks;
    report["scope"] =
        "Actual selected station voyage with explicit runtime assistance "
        "profile, "
        "flown stable-orbit hold selection and complete Save/Continue; "
        "Advanced final touchdown uses recorded manual channels; "
        "explicit existing liftoff/port aids remain labelled aids, not fully "
        "unaided flight";
  }
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
auto checkpoint_bytes(const std::filesystem::path& path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  check(static_cast<bool>(input), "Continuation checkpoint cannot be read");
  std::string bytes(kMaximumSaveDocumentBytes + 1, '\0');
  input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  check(!input.bad() && input.gcount() > 0 &&
            static_cast<std::size_t>(input.gcount()) <=
                kMaximumSaveDocumentBytes,
        "Continuation checkpoint is empty, unreadable or oversized");
  bytes.resize(static_cast<std::size_t>(input.gcount()));
  return bytes;
}
auto neighbor_start(const std::optional<std::filesystem::path>& source)
    -> NativeFreedomFlightSession {
  if (!source)
    return need(NativeFreedomFlightSession::open(
                    need(native_new_game(Seed{42}), "New Game refused")),
                "New session refused");
  auto selected = native_continue(*source);
  if (!selected)
    throw std::runtime_error("Continuation refused: " + selected.error());
  check(std::holds_alternative<FreedomSurfaceWalkSaveDocument>(
            selected->document),
        "Continuation requires the genuine format30 surface voyage");
  auto session = NativeFreedomFlightSession::open(std::move(*selected));
  if (!session)
    throw std::runtime_error("Continuation session refused: " +
                             session.error());
  return std::move(*session);
}
auto run_neighbor(const std::filesystem::path& output, bool record_commands,
                  bool pilot, std::optional<IntersystemArrivalQuality> grade,
                  const std::optional<std::filesystem::path>& continue_from)
    -> void {
  const auto source_bytes =
      continue_from ? checkpoint_bytes(*continue_from) : std::string{};
  Trace trace{neighbor_start(continue_from), output,
              need(TerrainTileCache::create(), "Terrain cache refused")};
  trace.endurance.emplace();
  trace.uninterrupted.emplace(trace.session);
  auto& session = trace.session;
  const auto ids = generate_first_intersystem_identities(Seed{42});
  const auto initial_tick = session.document().flight.tick;
  const auto initial_model = session.document().model;
  trace.continuation_route = continue_from.has_value();
  trace.continuation_initial_tick = initial_tick;
  trace.continuation_assistance = initial_model.assistance;
  trace.ground_route = continue_from.has_value();
  OriginWalkerState entry{};
  RigidOrientation station_q{};
  if (continue_from) {
    const auto& initial_body = session.document().flight;
    const bool relative_home_ascent =
        initial_model.frame_policy == kFreedomRelativeFramePolicy &&
        initial_body.frame.kind == RigidFrameKind::planet_relative_inertial &&
        initial_body.frame.planet ==
            session.system()
                .catalog.planets.at(kOriginHomePlanetOrdinal)
                .descriptor.id;
    const bool system_ascent =
        initial_model.frame_policy == kFreedomReleaseSystemFramePolicy &&
        initial_body.frame.kind == RigidFrameKind::system_inertial;
    check(initial_tick <= 1'200'000 &&
              session.document().origin.recipe.universe_seed == Seed{42} &&
              session.system().catalog.id == ids.origin_system &&
              (relative_home_ascent || system_ascent) &&
              (initial_model.terrain_policy == 2 ||
               (initial_model.terrain_policy == kFreedomSeaDatumTerrainPolicy &&
                initial_model.optics == 1)) &&
              initial_model.physical_catalog == 2 &&
              initial_model.physical_ephemeris == 2 &&
              initial_model.assistance == !pilot &&
              session.document().flight.craft ==
                  CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion} &&
              session.starting_assembly() ==
                  NativeStartingAssemblySelection{} &&
              session.surface_walk_selected() && !session.surface_walker() &&
              !session.walker() && session.boarding() &&
              session.boarding()->phase == FreedomBoardingPhase::seated &&
              session.surface() && !session.surface()->landed &&
              !session.surface()->gear_deployed && !session.condition() &&
              !session.recovery_pending() && !session.travel() &&
              (!session.docking() || !session.docking()->attached) &&
              session.resources() && session.resources()->jump_charges == 3 &&
              session.knowledge() && !initial_model.hold.target &&
              session.surface_maneuver().kind == NativeSurfaceManeuverKind::off,
          "Continuation needs the supported seated home ascent, stowed gear, "
          "no actor/contact/loss/travel, existing profile and three charges");
    const auto observation =
        need(session.observe(), "Ascent observation refused");
    check(observation.atmosphere.altitude_metres >=
              observation.atmosphere.space_boundary_altitude_metres,
          "Continuation must already have genuinely exited the atmosphere");
    trace.initial_flight_quanta = session.resources()->flight_quanta;
    entry = session.boarding()->station_entry;
    // The optional --pilot flag must match; the saved profile is never
    // rewritten.
  }
  std::optional<Commands> commands;
  if (record_commands) {
    commands.emplace(output);
    trace.commands = &*commands;
  }
  if (continue_from) {
    trace.checkpoint("surface-ascent");
    check(checkpoint_bytes(output / "surface-ascent.json") == source_bytes,
          "Declared Continue changed the complete source checkpoint");
  } else {
    trace.checkpoint("station-start");
    for (int i = 0; i < 2960; ++i)
      (void)need(trace.walk({0, -1, 0}), "Walk refused");
    entry = *session.walker();
    need(session.begin_boarding());
    trace.action("board");
    for (unsigned i = 0; i < kGameplayBoardingTicks; ++i)
      (void)need(trace.walk({}), "Seat route refused");
    trace.checkpoint("seated");
    station_q = session.document().flight.orientation;
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
  }
  const auto frame = need(resolve_craft_frame(session.document().flight.craft),
                          "Frame refused")
                         .properties;
  const auto station = generate_origin_station(Seed{42});
  const auto geometry =
      need(origin_station_geometry(station), "Station geometry refused");
  NativeFlightControls held;
  if (!continue_from) {
    // An explicit test pilot brakes relative to the departure planet. Each
    // correction is real thrust/torque; no momentum/pose assignment or fuel
    // grant.
    const auto initial = session.document().flight;
    const auto plane = unit(cross(initial.position_metres,
                                  initial.linear_velocity_metres_per_second));
    unsigned settled{};
    for (unsigned tick = 0; tick < 120 * 1200; ++tick) {
      const auto body = session.document().flight;
      const auto observed =
          need(session.observe(), "Brake observation refused");
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
  }
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
                "Actual Advanced commitment lost the intended safe grade");
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
    const auto body = continue_from ? need(session.relative_state(),
                                           "Relative rendezvous pose refused")
                                    : session.document().flight;
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
      const auto port_q = continue_from ? dock.orientation : station_q;
      axes = {rotate(port_q, {1, 0, 0}), rotate(port_q, {0, 1, 0}),
              rotate(port_q, {0, 0, 1})};
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
      held = controls(body, frame, observed.atmosphere, acceleration, axes,
                      continue_from && initial_model.assistance);
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
  if (continue_from)
    check(*session.walker() == entry,
          "Continuation return changed its actual saved station-entry actor");
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
              {"terrain_policy", session.document().model.terrain_policy},
              {"profile", pilot ? "pilot" : "assisted"},
              {"origin_system", system_id_string(ids.origin_system)},
              {"neighbor_system", system_id_string(ids.target_system)},
              {"uninterrupted_match", true},
              {"checkpoints", trace.checkpoints},
              {"final_tick", std::to_string(session.document().flight.tick)},
              {"effort_quanta", trace.endurance->effort_quanta},
              {"flight_quanta_before_service", fuel_before_service}};
  if (continue_from) {
    report["schema_version"] = 7;
    report["route"] = "native-neighbor-from-ascent";
    report["start_mode"] = "continue";
    report["initial_tick"] = std::to_string(initial_tick);
    report["frame_policy"] = initial_model.frame_policy;
    report["flight_profile"] =
        initial_model.assistance ? "assisted" : "advanced";
    report["uninterrupted_scope"] = "suffix_from_declared_continue_only";
    report["initial_flight_quanta"] =
        std::to_string(trace.initial_flight_quanta);
    report["inherited_spent_quanta"] = std::to_string(
        kFreedomFlightCapacityQuanta - trace.initial_flight_quanta);
    report["source_file"] = "surface-ascent.json";
    check(checkpoint_bytes(*continue_from) == source_bytes,
          "Continuation modified the original saved source");
  }
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
auto terrain_feature(const std::filesystem::path& output) -> void {
  const Seed universe{42};
  const auto ids = generate_first_intersystem_identities(universe);
  const auto system =
      need(generate_physical_local_system(ids.target_system_seed, 2),
           "Feature catalog refused");
  const PlanetId id{7198692597331823481ULL};
  const auto& planet =
      need(find_local_system_planet(system, id), "Feature body refused")
          ->descriptor;
  const auto rotation = need(generate_planet_rotation_recipe(system, id),
                             "Feature rotation refused");
  auto cache = need(TerrainTileCache::create(12), "Feature cache refused");
  const auto sampler =
      need(TerrainLocalDetailSampler::create(system, body_id(id), {2}, cache),
           "Feature sampler refused");
  constexpr std::array<V, 3> directions{
      {{.9999999258258838, .0003851600018052678, 0},
       {.9999998211197538, .0005981308054568976, 0},
       {.9999996673714516, .0008156328747314038, 0}}};
  constexpr std::array labels{"left_trough", "crest", "right_trough"};
  std::array<TerrainLocalDetailSample, 3> heights;
  std::array<V, 3> points;
  const double radius = planet.radius.value * 1000.0;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto d = directions[i];
    heights[i] = need(sampler.sample_height({d.x, d.y, d.z}, 13),
                      "Feature height refused");
    points[i] = scale(unit(d), radius + heights[i].elevation_metres);
  }
  constexpr double clearance = 50;
  const V position = scale(unit(directions[0]),
                           radius + heights[0].elevation_metres + clearance);
  std::optional<PhysicalPlanetRotationGeometry> daylight;
  double angular_radius{}, sun_elevation{};
  // Choose one declared initialized time from the actual rotating world. This
  // never advances a session or substitutes a light for the native environment.
  for (SimulationTick index = 0; index < 32 && !daylight; ++index) {
    auto value =
        need(resolve_planet_rotation(
                 system, rotation, rotation.rotation.period_ticks * index / 32,
                 {position.x, position.y, position.z}),
             "Feature daylight query refused");
    const auto star = value.geometry.observer_to_star_fixed;
    sun_elevation = std::asin(
        std::clamp(dot(unit(position), {star.x, star.y, star.z}), -1.0, 1.0));
    angular_radius = std::asin(
        std::min(1.0, system.catalog.star.radius_kilometres * 1000.0 /
                          value.geometry.observer_star_distance_metres));
    if (sun_elevation > std::max(.2, angular_radius)) daylight = value;
  }
  check(daylight.has_value(),
        "No daylight feature viewpoint within32 declared times");
  RigidBodyState fixed;
  fixed.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  fixed.frame = {RigidFrameKind::planet_fixed, system.catalog.id, id, {}};
  fixed.tick = daylight->geometry.tick;
  fixed.position_metres = position;
  // These measured equatorial points lie in XY. Start with +Y yaw pi/2
  // (right=-Z), then rotate about fixed +Z so body -Z faces the real crest.
  const auto forward = unit(sub(points[1], position));
  const double half_roll =
      (std::atan2(forward.y, forward.x) - std::numbers::pi) / 2;
  const double c = std::sqrt(.5), w = c * std::cos(half_roll),
               z = c * std::sin(half_roll);
  fixed.orientation = need(normalize_rigid_orientation({w, -z, w, z}),
                           "Feature attitude refused");
  check(dot(rotate(fixed.orientation, {0, 0, -1}), forward) > 1 - 1e-12,
        "Initialized feature attitude does not face actual crest");
  fixed = need(canonicalize_rigid_body_state({system}, fixed),
               "Feature fixed state refused");
  FreedomFlightSaveDocument flight{
      make_freedom_new_game_document(universe),
      need(reframe_rigid_body({system}, fixed,
                              {{RigidFrameKind::planet_relative_inertial,
                                system.catalog.id,
                                id,
                                {}},
                               fixed.tick},
                              rotation),
           "Feature same-tick handoff refused"),
      {}};
  flight.origin.state.tick = fixed.tick;
  flight.model.physical_catalog = 2;
  flight.model.physical_ephemeris = 2;
  flight.model.terrain_policy = 2;
  flight.model.assistance = false;
  flight.world = FreedomActiveWorldSelection{1, system.catalog.id, id};
  auto session = need(
      NativeFreedomFlightSession::open(need(native_select_save_document(flight),
                                            "Feature native owner refused")),
      "Feature session refused");
  const auto path = output / "terrain-feature.json";
  need(session.save_as(path));
  const auto encoded =
      need(encode_native_save_document_json(need(
               session.save_document(), "Feature save projection refused")),
           "Feature save encode refused");
  std::ifstream saved{path};
  const std::string original{std::istreambuf_iterator<char>{saved}, {}};
  auto continued = need(NativeFreedomFlightSession::open(need(
                            native_continue(path), "Feature Continue refused")),
                        "Feature continued session refused");
  check(original == encoded && continued.document() == flight &&
            need(encode_native_save_document_json(
                     need(continued.save_document(),
                          "Feature continued projection refused")),
                 "Feature continued encoding refused") == encoded,
        "Initialized feature full Save/Continue changed owner/state/bytes");
  const auto view = godot_spike::project_saved_flight(session);
  const V presented_origin{view.tangent.origin.x, view.tangent.origin.y,
                           view.tangent.origin.z};
  const auto local = [&](V value) {
    return coordinates(
        {dot(value,
             {view.tangent.east.x, view.tangent.east.y, view.tangent.east.z}),
         dot(value, {view.tangent.up.x, view.tangent.up.y, view.tangent.up.z}),
         -dot(value, {view.tangent.north.x, view.tangent.north.y,
                      view.tangent.north.z})});
  };
  Json physical = Json::array();
  for (std::size_t i = 0; i < points.size(); ++i)
    physical.push_back(
        {{"label", labels[i]},
         {"fixed_direction", coordinates(directions[i])},
         {"elevation_metres", heights[i].elevation_metres},
         {"macro_elevation_metres", heights[i].macro_elevation_metres},
         {"local_offset_metres", heights[i].local_offset_metres},
         {"fixed_position_metres", coordinates(points[i])},
         {"local_metres", local(sub(points[i], presented_origin))}});
  const auto star = view.rotation.geometry.observer_to_star_fixed;
  const Json manifest{
      {"schema", "apsis.native-terrain-feature/1"},
      {"scope", "Initialized measurement viewpoint, not flown arrival or "
                "default voyage; no session steps"},
      {"save_file", "terrain-feature.json"},
      {"universe_seed", "42"},
      {"system_id", std::to_string(system.catalog.id.value)},
      {"planet_id", std::to_string(id.value)},
      {"system_seed", std::to_string(system.catalog.seed.value)},
      {"tick", std::to_string(fixed.tick)},
      {"checksum",
       std::to_string(need(rigid_body_state_checksum({system}, flight.flight),
                           "Feature checksum refused"))},
      {"physical_catalog", 2},
      {"physical_ephemeris", 2},
      {"terrain_policy", 2},
      {"radius_metres", radius},
      {"altitude_metres", view.pose.altitude_metres},
      {"clearance_metres", clearance},
      {"star_direction_fixed", coordinates({star.x, star.y, star.z})},
      {"star_direction_local", local({star.x, star.y, star.z})},
      {"star_angular_radius", angular_radius},
      {"sun_elevation_radians", sun_elevation},
      {"expected_native_sun_visibility", 1.0},
      {"daylight_search_bound", 32},
      {"camera_mode", "cockpit"},
      {"look_offset", {0., 0.}},
      {"framing", "Body -Z faces crest from craft centre; preserve authored "
                  "pilot eye and measure actual camera projection"},
      {"points", physical}};
  std::ofstream report{output / "terrain-feature-manifest.json"};
  report << manifest.dump(2) << '\n';
  check(static_cast<bool>(report), "Feature manifest write failed");
}
} // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2 || argc > 7)
      throw std::invalid_argument(
          "Expected one absolute empty output "
          "directory and optional --commands/--endurance/--neighbor/--pilot/"
          "--aligned/--offset/--surface-loop [--system-frame] or distinct "
          "--terrain-feature; --flight-profile=assisted|advanced requires "
          "--surface-loop --system-frame; --continue-from=/absolute/save "
          "requires --neighbor --commands");
    bool commands{}, endurance{}, neighbor{}, pilot{}, surface_loop{},
        feature{}, system_frame{};
    std::optional<IntersystemArrivalQuality> grade;
    std::optional<PhysicalFlightProfile> profile;
    std::optional<std::filesystem::path> continue_from;
    for (int i = 2; i < argc; ++i) {
      const std::string_view option{argv[i]};
      if (option.starts_with("--continue-from=") && !continue_from)
        continue_from = std::filesystem::path{std::string{option.substr(16)}};
      else if (option == "--commands" && !commands)
        commands = true;
      else if (option == "--endurance" && !endurance)
        endurance = true;
      else if (option == "--surface-loop" && !surface_loop)
        surface_loop = true;
      else if (option == "--system-frame" && !system_frame)
        system_frame = true;
      else if (option == "--flight-profile=assisted" && !profile)
        profile = PhysicalFlightProfile::assisted;
      else if (option == "--flight-profile=advanced" && !profile)
        profile = PhysicalFlightProfile::advanced;
      else if (option == "--terrain-feature" && !feature)
        feature = true;
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
    if (surface_loop && (neighbor || pilot || grade))
      throw std::invalid_argument(
          "--surface-loop is a distinct planetary fixture mode");
    if (profile && (!surface_loop || !system_frame))
      throw std::invalid_argument(
          "A flight profile requires --surface-loop --system-frame");
    if (system_frame && !surface_loop)
      throw std::invalid_argument("--system-frame requires --surface-loop");
    if (feature && (commands || endurance || neighbor || pilot || grade ||
                    surface_loop || system_frame || profile))
      throw std::invalid_argument(
          "--terrain-feature is a distinct initialized measurement mode");
    if (pilot && !neighbor)
      throw std::invalid_argument("--pilot requires --neighbor");
    if (grade && (!neighbor || !pilot))
      throw std::invalid_argument(
          "A selected grade requires --neighbor --pilot");
    if (continue_from &&
        (!neighbor || !commands || endurance || grade || surface_loop ||
         feature || system_frame || profile || !continue_from->is_absolute() ||
         continue_from->native().size() > 4000 ||
         !std::filesystem::is_regular_file(*continue_from)))
      throw std::invalid_argument(
          "--continue-from requires --neighbor --commands and one bounded "
          "absolute source; only matching --pilot may modify this route");
    if (feature)
      terrain_feature(output);
    else if (neighbor)
      run_neighbor(output, commands, pilot, grade, continue_from);
    else
      run(output, commands, endurance, surface_loop, system_frame, profile);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
