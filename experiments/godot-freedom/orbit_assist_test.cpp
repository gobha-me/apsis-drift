#include "flight_navigation.hpp"

#include <iostream>
#include <limits>

using namespace apsis_drift;
namespace lab = apsis_drift::flight_lab;

namespace {
auto check(bool condition, const char* message) -> void {
  if (!condition) throw std::runtime_error(message);
}
template <typename Operation> auto rejects(Operation operation) -> void {
  try {
    operation();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error("invalid orbit-assist input accepted");
}
auto fresh(const PlanetDescriptor& planet, double altitude) -> lab::State {
  auto state = lab::initial(planet, {.25, .4, altitude}, .3, 2);
  lab::enable_orbit_preserving_translation(state);
  return state;
}
auto altitude_fixture(const PlanetDescriptor& planet, double altitude)
    -> lab::State {
  auto state = fresh(planet, altitude);
  // Axis position avoids unnecessary trig noise in numerical boundary probes.
  state.position = {planet.radius.value * 1000.0 + altitude, 0, 0};
  return state;
}
auto malformed(const PlanetDescriptor& planet) -> void {
  auto state = fresh(planet, 250000);
  rejects([&] { lab::enable_orbit_preserving_translation(state); });
  for (unsigned policy : {0U, 3U, std::numeric_limits<unsigned>::max()}) {
    auto invalid = state;
    invalid.translation_policy = policy;
    const auto before = invalid;
    rejects([&] { lab::validate(invalid); });
    rejects([&] { (void)lab::assist_translation_weight(planet, invalid); });
    rejects([&] { (void)lab::effective_aerodynamic_density(planet, invalid); });
    rejects([&] { lab::advance(planet, 0, invalid, {}); });
    check(invalid == before, "invalid policy partially changed flight state");
  }
  auto old = lab::initial(planet, {.25, .4, 250000}, .3, 2);
  old.tick = 1;
  const auto before = old;
  rejects([&] { lab::enable_orbit_preserving_translation(old); });
  check(old == before, "late policy selection modified running simulation");
  for (double bad : {NAN, INFINITY, -INFINITY}) {
    auto invalid = state;
    invalid.position.x = bad;
    rejects([&] { (void)lab::assist_translation_weight(planet, invalid); });
    rejects([&] { (void)lab::effective_aerodynamic_density(planet, invalid); });
  }
  const PlanetDescriptor invalid_planet{planet.seed,
                                        planet.id,
                                        planet.display_name,
                                        PlanetRadiusKm{0},
                                        planet.surface_gravity,
                                        planet.atmosphere_class,
                                        planet.atmosphere_pressure,
                                        planet.terrain_character,
                                        planet.water_coverage,
                                        planet.palette};
  rejects([&] { (void)lab::assist_translation_weight(invalid_planet, state); });
  for (bool overflow_tick : {false, true}) {
    auto boundary = state;
    if (overflow_tick) {
      boundary.tick = std::numeric_limits<SimulationTick>::max();
    } else {
      boundary.position = {1e10, 0, 0};
      boundary.velocity = {100000, 0, 0};
    }
    const auto original = boundary;
    rejects([&] { lab::advance(planet, 0, boundary, {}); });
    check(boundary == original,
          "clock or numeric-envelope refusal partially advanced policy two");
  }
}
auto fade_contract(const PlanetDescriptor& planet) -> void {
  const double sea = lab::density(planet, 0);
  check(sea > .001, "atmospheric fixture needs full-density range");
  auto state = altitude_fixture(planet, 0);
  const double edge = lab::orbit_info(planet, state).atmosphere_edge;
  check(lab::assist_translation_weight(planet, state) == 1,
        "dense atmosphere lost full available hover support");
  double previous = 1;
  for (unsigned i = 0; i <= 1000; ++i) {
    state = altitude_fixture(planet, edge * i / 1000.0);
    const double weight = lab::assist_translation_weight(planet, state);
    const double effective = lab::effective_aerodynamic_density(planet, state);
    const double raw = lab::density(planet, lab::length(state.position) -
                                                planet.radius.value * 1000.0);
    check(std::isfinite(weight) && weight >= 0 && weight <= 1 &&
              weight <= previous + 1e-14,
          "atmospheric support fade is not finite bounded monotonic");
    check(effective >= 0 && effective <= raw && std::isfinite(effective),
          "effective density exceeds raw density or becomes invalid");
    previous = weight;
  }
  for (double offset : {-0.001, 0.001, 1.0, 100000.0}) {
    state = altitude_fixture(planet, edge + offset);
    const double weight = lab::assist_translation_weight(planet, state);
    check(offset < 0 ? weight < 1e-16 : weight == 0,
          "air-edge fade has a discontinuity or nonzero vacuum tail");
    if (offset > 0)
      check(lab::effective_aerodynamic_density(planet, state) == 0,
            "SPACE retains residual aerodynamic braking");
  }
  const double full_altitude = 8500 * std::log(sea / .001);
  for (double offset : {-0.001, 0.001}) {
    state = altitude_fixture(planet, full_altitude + offset);
    check(lab::assist_translation_weight(planet, state) > 1 - 1e-12,
          "full-atmosphere fade boundary is discontinuous");
  }
  // Turning at the edge must not translate the ship through leftover thin-air
  // drag, regardless of attitude or assist toggle. One step starts in SPACE.
  for (RigidOrientation orientation :
       {RigidOrientation{}, RigidOrientation{.5, .5, .5, .5},
        RigidOrientation{0, 1, 0, 0}}) {
    auto on = altitude_fixture(planet, edge + .001);
    lab::set_orientation(on, orientation);
    on.velocity = {0, 8000, 0};
    on.angular = {.2, -.1, .3};
    auto off = on;
    lab::Demand command;
    command.pitch = .5;
    command.yaw = -.3;
    command.roll = .2;
    lab::advance(planet, 0, on, command);
    command.assist = false;
    lab::advance(planet, 0, off, command);
    check(on.position == off.position && on.velocity == off.velocity &&
              on.thrust_body == lab::V{} && off.thrust_body == lab::V{} &&
              on.dynamic_pressure == 0 && off.dynamic_pressure == 0,
          "space attitude maneuver introduced translation braking or gravity "
          "cancellation");
  }
}
auto orbit_contract(const PlanetDescriptor& planet) -> void {
  auto on = fresh(planet, 250000);
  const double radius = planet.radius.value * 1000.0;
  const double mu =
      9.80665 * planet.surface_gravity.value / 1000.0 * radius * radius;
  on.velocity = on.back * -std::sqrt(mu / lab::length(on.position));
  on.angular = {.2, -.1, .3};
  auto off = on;
  lab::Demand assisted, unassisted;
  unassisted.assist = false;
  for (unsigned tick = 0; tick < 120 * 600; ++tick) {
    lab::advance(planet, 0, on, assisted);
    lab::advance(planet, 0, off, unassisted);
    check(
        on.position == off.position && on.velocity == off.velocity,
        "hands-off assisted orbit diverged from unassisted gravity-only orbit");
    check(on.thrust_body == lab::V{} && off.thrust_body == lab::V{} &&
              on.dynamic_pressure == 0 && off.dynamic_pressure == 0,
          "neutral orbit has translational automatic force");
  }
  check(lab::length(on.angular) < 1e-8 && lab::length(off.angular) > .1,
        "orbit policy disabled attitude stabilization or rotational coast");
  check(lab::orbit_info(planet, on).clear_orbit &&
            std::abs(lab::length(on.position) - radius - 250000) < 100,
        "hands-off orbit failed or exceeded historical integration drift "
        "envelope");
  auto thrust = fresh(planet, 250000);
  auto ballistic = thrust;
  lab::Demand powered;
  powered.main = 1;
  for (unsigned tick = 0; tick < 240; ++tick) {
    lab::advance(planet, 0, thrust, powered);
    lab::advance(planet, 0, ballistic, {});
  }
  check(lab::length(thrust.velocity - ballistic.velocity) > 70 &&
            thrust.main == 1,
        "orbit-preserving policy suppressed explicit requested main thrust");
}
auto complete_orbit_contract(const PlanetDescriptor& planet) -> void {
  const auto environment = lab::orbit_info(planet, fresh(planet, 250000));
  const double safe_altitude = std::max(20000.0, environment.atmosphere_edge);
  auto on = fresh(planet, safe_altitude + 5000);
  const double surface_radius = planet.radius.value * 1000.0;
  const double radius = lab::length(on.position);
  const double mu = 9.80665 * planet.surface_gravity.value / 1000.0 *
                    surface_radius * surface_radius;
  const double period =
      2 * std::numbers::pi * std::sqrt(radius * radius * radius / mu);
  check(std::isfinite(period) && period * kSimulationHz < 2000000,
        "full-orbit fixture exceeded bounded test budget");
  const auto ticks =
      static_cast<unsigned>(std::ceil(period * kSimulationHz)) + 1U;
  on.velocity = on.back * -std::sqrt(mu / radius);
  on.angular = {.2, -.1, .3};
  auto off = on;
  lab::Demand unassisted;
  unassisted.assist = false;
  double minimum_periapsis = std::numeric_limits<double>::max();
  double maximum_radius_error = 0;
  for (unsigned tick = 0; tick < ticks; ++tick) {
    lab::advance(planet, 0, on, {});
    lab::advance(planet, 0, off, unassisted);
    check(on.position == off.position && on.velocity == off.velocity &&
              on.thrust_body == lab::V{} && off.thrust_body == lab::V{} &&
              on.dynamic_pressure == 0 && off.dynamic_pressure == 0,
          "assist altered linear coast during a complete low safe orbit");
    maximum_radius_error = std::max(
        maximum_radius_error, std::abs(lab::length(on.position) - radius));
    if (tick % kSimulationHz == 0 || tick + 1 == ticks) {
      const auto orbit = lab::orbit_info(planet, on);
      minimum_periapsis = std::min(minimum_periapsis, orbit.periapsis);
      check(orbit.clear_orbit && orbit.periapsis > safe_altitude + 1000 &&
                lab::assist_translation_weight(planet, on) == 0,
            "low safe orbit lost clearance classification during full coast");
    }
  }
  check(
      maximum_radius_error < 100,
      "complete orbit exceeded historical gravity-integrator drift allowance");
  std::cout << "Complete orbit coast: " << ticks << " ticks / " << period
            << " s; maximum radial error " << maximum_radius_error
            << " m; minimum periapsis margin "
            << minimum_periapsis - safe_altitude << " m\n";
}
auto legacy_contract(const PlanetDescriptor& planet) -> void {
  auto base = lab::initial(planet, {.25, .4, 100000}, .3);
  check(base.translation_policy == 1 &&
            lab::checksum(base) == 12702440880597842818ULL,
        "default translation policy changed historical initial bytes");
  for (bool assist : {false, true}) {
    auto state = base;
    for (unsigned tick = 0; tick < 240; ++tick)
      lab::advance(planet, 0, state, {.7, .1, .3, -.2, .4, .15, -.1, assist});
    check(lab::checksum(state) ==
              (assist ? 16915801839928502580ULL : 6380759757687234280ULL),
          "translation policy one changed historical mixed-demand checksum");
  }
  base = lab::initial(planet, {.25, .4, 250000}, .3, 2);
  const auto old = base;
  lab::enable_orbit_preserving_translation(base);
  check(lab::checksum(base) != lab::checksum(old) &&
            base.angular_model == old.angular_model &&
            base.orientation == old.orientation &&
            base.position == old.position && base.velocity == old.velocity,
        "new translation policy was unversioned or changed initial physical "
        "state");
}
} // namespace

auto main() -> int {
  try {
    const auto planet = generate_planet_descriptor(Seed{42});
    malformed(planet);
    fade_contract(planet);
    orbit_contract(planet);
    complete_orbit_contract(planet);
    legacy_contract(planet);
    bool found = false;
    for (std::uint64_t seed = 0; seed < 1024 && !found; ++seed) {
      const auto airless = generate_planet_descriptor(Seed{seed});
      if (airless.atmosphere_pressure.value != 0) continue;
      for (double altitude : {16.0, 60.0, 1000.0, 250000.0}) {
        auto on = fresh(airless, altitude);
        auto off = on;
        check(
            lab::assist_translation_weight(airless, on) == 0 &&
                lab::effective_aerodynamic_density(airless, on) == 0,
            "airless assistance fabricated an altitude-based hover/drag mode");
        lab::advance(airless, 0, on, {});
        lab::Demand demand;
        demand.assist = false;
        lab::advance(airless, 0, off, demand);
        check(on.position == off.position && on.velocity == off.velocity &&
                  on.thrust_body == lab::V{},
              "airless near-surface assistance canceled gravity");
      }
      found = true;
    }
    check(found, "airless fixture missing");
    std::cout
        << "Orbit-preserving translation assistance: all contracts pass\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
