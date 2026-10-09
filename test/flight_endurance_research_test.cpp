#include "flight_endurance_meter.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "apsis_drift/atmospheric_flight.hpp"

namespace {
using namespace apsis_drift;
using Meter = research::FlightEnduranceMeter;
using V = RigidVector3;
using Json = nlohmann::ordered_json;
int checks{}, failures{};
auto check(bool ok, std::string_view message) -> void {
  ++checks;
  if (ok) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}
template <typename T, typename E> auto need(std::expected<T, E> r) -> T {
  if (!r) throw std::runtime_error("Required endurance reference refused");
  return std::move(*r);
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto mul(V a, double b) -> V {
  return {a.x * b, a.y * b, a.z * b};
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto reference_arithmetic() -> void {
  check(Meter::quantize(0) == 0 && Meter::quantize(-0.0) == 0 &&
            Meter::quantize(.00049) == 1 && Meter::quantize(.0005) == 1 &&
            Meter::quantize(std::numeric_limits<double>::denorm_min()) == 1 &&
            Meter::quantize(1.2345) == 1235 &&
            Meter::quantize(1'000'000'000) == 1'000'000'000'000ULL,
        "fixed quantization boundaries, upward ties and minimum positive bill");
  for (const auto v :
       {-1.0, std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        std::nextafter(1'000'000'000.0,
                       std::numeric_limits<double>::infinity())}) {
    bool rejected{};
    try {
      (void)Meter::quantize(v);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    check(rejected, "invalid/nonfinite measurement refused");
  }
  Meter main;
  VacuumActuation firing;
  firing.negative_force_newtons.z = 360'000;
  main.sample(firing);
  check(main.effort_quanta == 4'680'000'000ULL && main.ticks == 1 &&
            main.firing_ticks == 1,
        "one120Hz full-main exact vector");
  for (unsigned tick = 1; tick < 120; ++tick)
    main.sample(firing);
  check(main.effort_quanta == 561'600'000'000ULL &&
            main.effort_quanta / research::kEffortQuantaPerNewtonSecond ==
                360'000,
        "one authoritative second full-main exact vector");
  check(research::kStarterFlightCapacityQuanta == 3'032'640'000'000'000ULL &&
            research::kStarterFlightReserveQuanta == 1'010'880'000'000'000ULL &&
            research::kStarterFlightCapacityQuanta / main.effort_quanta ==
                5'400,
        "ninety full-main minutes and one-third reserve exact vectors");
  VacuumActuation opposition;
  opposition.positive_force_newtons.x = 112'000;
  opposition.negative_force_newtons.x = 112'000;
  Meter opposed;
  opposed.sample(opposition);
  check(opposed.effort_quanta == 2'912'000'000ULL,
        "opposing physical firings are billed despite zero net force");
  opposition = {};
  opposition.positive_torque_newton_metres.x = 728'000;
  opposition.negative_torque_newton_metres.x = 728'000;
  Meter opposed_torque;
  opposed_torque.sample(opposition);
  check(opposed_torque.effort_quanta == 2'912'000'000ULL,
        "opposing torque physical firings use explicit6.5m coupling");
  opposition.assist_torque_newton_metres = {1e9, 1e9, 1e9};
  Meter algebraic_delta;
  algebraic_delta.sample(opposition);
  check(algebraic_delta == opposed_torque,
        "assist algebraic delta is not an extra bill");
  const auto before = main;
  opposition.negative_torque_newton_metres.z = -1;
  bool rejected{};
  try {
    main.sample(opposition);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  check(rejected && main == before,
        "bad last channel retains all measurement counters");
  Meter overflow;
  overflow.effort_quanta = std::numeric_limits<std::uint64_t>::max();
  const auto previous = overflow;
  rejected = false;
  try {
    overflow.sample(firing);
  } catch (const std::overflow_error&) {
    rejected = true;
  }
  check(rejected && overflow == previous,
        "overflow measurement refuses transactionally");
  Meter compressed;
  for (unsigned batch = 0; batch < 8; ++batch)
    for (unsigned tick = 0; tick < 15; ++tick)
      compressed.sample(firing);
  check(compressed == before,
        "render batch partition cannot change exact integration");
  const auto checkpoint = compressed;
  Meter resumed = checkpoint;
  resumed.sample(firing);
  compressed.sample(firing);
  check(resumed == compressed,
        "integer checkpoint resume equals uninterrupted accounting");
}

auto matrix() -> Json {
  Json rows = Json::array();
  std::array<bool, 4> covered{};
  for (std::uint64_t seed = 0; seed < 128 && rows.size() < 4; ++seed) {
    const auto world = need(generate_physical_local_system(Seed{seed}, 2));
    for (const auto& body : world.catalog.planets) {
      const auto& p = body.descriptor;
      const auto kind = static_cast<std::size_t>(p.atmosphere_class);
      if (covered[kind]) continue;
      const RigidBodyWorldContext context{world};
      const auto rotation = need(generate_planet_rotation_recipe(world, p.id));
      const auto geometry = need(resolve_planet_rotation(world, rotation, 25));
      const auto spin = geometry.geometry.angular_velocity_radians_per_second;
      const V omega{spin.x, spin.y, spin.z};
      RigidBodyState source;
      source.tick = 25;
      source.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
      source.frame = {
          RigidFrameKind::planet_relative_inertial, world.catalog.id, p.id, {}};
      source.position_metres = {0, p.radius.value * 1000.0 + 100, 0};
      source.linear_velocity_metres_per_second =
          cross(omega, source.position_metres);
      source = need(canonicalize_rigid_body_state(context, source));
      const auto gravity = need(evaluate_central_body_gravity(context, source));
      const auto frame = need(resolve_craft_frame(source.craft)).properties;
      VacuumIntent neutral;
      neutral.assistance = false;
      const auto environment =
          need(evaluate_atmospheric_flight(context, source, neutral, rotation));
      const auto desired =
          mul(sub(cross(omega, cross(omega, source.position_metres)),
                  gravity.acceleration_metres_per_second_squared),
              frame.dry_mass_kg);
      VacuumIntent hover;
      hover.assistance = false;
      const std::array demand{desired.x, desired.y, desired.z};
      std::array positive{&hover.positive_translation.x,
                          &hover.positive_translation.y,
                          &hover.positive_translation.z};
      std::array negative{&hover.negative_translation.x,
                          &hover.negative_translation.y,
                          &hover.negative_translation.z};
      bool feasible = true;
      for (std::size_t axis = 0; axis < 3; ++axis) {
        feasible &= demand[axis] <= frame.positive_force_newtons[axis] &&
                    -demand[axis] <= frame.negative_force_newtons[axis];
        *positive[axis] = std::clamp(
            demand[axis] / frame.positive_force_newtons[axis], 0.0, 1.0);
        *negative[axis] = std::clamp(
            -demand[axis] / frame.negative_force_newtons[axis], 0.0, 1.0);
      }
      auto state = source;
      const auto hover_step =
          need(advance_atmospheric_flight(context, state, hover, rotation));
      Meter hover_meter;
      hover_meter.sample(hover_step.central.propulsion);
      auto coast = source;
      const auto coast_step =
          need(advance_atmospheric_flight(context, coast, neutral, rotation));
      Meter coast_meter;
      coast_meter.sample(coast_step.central.propulsion);
      check(
          coast_meter.effort_quanta == 0 && coast != source,
          "actual gravity/atmosphere dynamics do not bill coasting propulsion");
      auto simultaneous = neutral;
      simultaneous.positive_translation.x = 1;
      simultaneous.negative_translation.x = 1;
      auto opposing = source;
      const auto opposing_step = need(advance_atmospheric_flight(
          context, opposing, simultaneous, rotation));
      Meter opposing_meter;
      opposing_meter.sample(opposing_step.central.propulsion);
      check(
          opposing_step.central.propulsion.applied_force_newtons.x == 0 &&
              opposing_meter.effort_quanta == 2'912'000'000ULL,
          "actual equal opposing translation retains nonzero fuel equivalent");
      auto main = source;
      VacuumIntent throttle = neutral;
      throttle.negative_translation.z = 1;
      Meter main_meter;
      main_meter.sample(
          need(advance_atmospheric_flight(context, main, throttle, rotation))
              .central.propulsion);
      check(main_meter.effort_quanta == 4'680'000'000ULL,
            "actual full-main force costs same in every atmosphere");
      auto assisted = source;
      assisted.angular_velocity_radians_per_second = {0, .01, 0};
      VacuumIntent assist;
      Meter assist_meter;
      assist_meter.sample(
          need(advance_atmospheric_flight(context, assisted, assist, rotation))
              .central.propulsion);
      check(assist_meter.effort_quanta > 0,
            "actual neutral rotation stabilization costs propulsion");
      check(feasible && hover_meter.effort_quanta > 0,
            "reference hover demand fits actual rated translation and costs "
            "propulsion");
      rows.push_back(
          {{"system_seed", seed},
           {"planet", p.id.value},
           {"atmosphere", atmosphere_class_name(p.atmosphere_class)},
           {"gravity_milli_g", p.surface_gravity.value},
           {"surface_pressure_millibars", p.atmosphere_pressure.value},
           {"sample_altitude_metres", 100},
           {"sample_density_kg_per_m3", environment.density_kg_per_cubic_metre},
           {"sample_pressure_millibars", environment.pressure_millibars},
           {"space_boundary_metres",
            environment.space_boundary_altitude_metres},
           {"dry_mass_kg", frame.dry_mass_kg},
           {"hover_feasible", feasible},
           {"hover_effort_quanta_per_tick", hover_meter.effort_quanta},
           {"main_effort_quanta_per_tick", main_meter.effort_quanta},
           {"coast_effort_quanta_per_tick", coast_meter.effort_quanta},
           {"assist_effort_quanta_per_tick", assist_meter.effort_quanta}});
      covered[kind] = true;
    }
  }
  check(rows.size() == 4,
        "bounded matrix covers all generated atmosphere classes");
  return rows;
}
} // namespace
auto main(int argc, char** argv) -> int {
  try {
    reference_arithmetic();
    const auto rows = matrix();
    if (argc == 3 && std::string_view{argv[1]} == "--report") {
      const std::filesystem::path path{argv[2]};
      if (!path.is_absolute() || path.native().size() > 4000 ||
          !std::filesystem::is_directory(path.parent_path()) ||
          std::filesystem::exists(path))
        throw std::invalid_argument(
            "Expected a bounded fresh absolute report file");
      std::ofstream out{path};
      const Json report{{"schema_version", 1},
                        {"scope", "One actual120Hz demand sample per body, not "
                                  "full hover/ascent proof"},
                        {"quanta_per_equivalent_newton_second",
                         research::kEffortQuantaPerNewtonSecond},
                        {"reference_environments", rows}};
      out << report.dump(2) << '\n';
      if (!out) throw std::runtime_error("Endurance matrix write failed");
    } else if (argc != 1)
      throw std::invalid_argument(
          "usage: flight-endurance-research-tests [--report FILE]");
    std::cout << rows.dump() << '\n';
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << e.what() << '\n';
  }
  std::cout << "Endurance research " << checks << " checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
