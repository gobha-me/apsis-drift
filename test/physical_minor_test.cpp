#include "apsis_drift/physical_minor_rotation.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace {
using namespace apsis_drift;
int checks{}, failures{};
auto check(bool value, std::string_view message) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Minor fixture refused");
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto near(double a, long double b, long double relative = 1e-14L) -> bool {
  return std::isfinite(a) && std::isfinite(b) &&
         std::abs(static_cast<long double>(a) - b) <=
             relative * std::max(1.0L, std::abs(b));
}
auto run() -> void {
  std::uint64_t fingerprint{14695981039346656037ULL};
  for (const auto seed : {42ULL, 4ULL, 7ULL, 123ULL}) {
    const auto system = need(generate_physical_origin_system(Seed{seed}, 2));
    const auto catalog = need(generate_secondary_body_catalog(system));
    const auto& source = catalog.bodies[1];
    const auto minor =
        need(generate_physical_minor_descriptor(catalog, source.node.id));
    const auto duplicate =
        need(generate_physical_minor_descriptor(catalog, source.node.id));
    check(minor == duplicate && minor.catalog == catalog,
          "Deterministic owner retains its source catalog");
    check(minor.body == source.node.id && minor.body.kind == BodyKind::minor &&
              minor.parent == *source.node.parent &&
              minor.parent.kind == BodyKind::star && !planet_id(minor.body),
          "Genuine minor identity and star parent");
    check(minor.radius_metres == source.radius_metres &&
              minor.spin_period_ticks == source.spin_period_ticks &&
              minor.epoch_spin_phase_turns == source.epoch_spin_phase_turns &&
              minor.tint == source.tint,
          "Exact inherited optical radius/spin/epoch/tint");
    check(minor.density_kilograms_per_cubic_metre == 3000 &&
              minor.gravitational_constant_si == 6.67430e-11 &&
              minor.atmosphere_pressure_millibars == 0 &&
              minor.water_coverage_basis_points == 0 &&
              minor.spin_pole_system.x == 0 && minor.spin_pole_system.y == 0 &&
              minor.spin_pole_system.z == 1,
          "Fixed authored physical extension, airless/dry and +Z pole");
    need(validate_physical_minor_descriptor(minor));
    const auto properties = need(resolve_physical_minor_properties(minor));
    const long double r = source.radius_metres;
    const long double mass =
        3000.0L * (4.0L / 3.0L) * std::numbers::pi_v<long double> * r * r * r;
    check(properties.descriptor == minor &&
              near(properties.mass_kilograms, mass) &&
              near(properties
                       .gravitational_parameter_metres_cubed_per_second_squared,
                   6.67430e-11L * mass) &&
              near(properties.surface_gravity_metres_per_second_squared,
                   6.67430e-11L * mass / (r * r)),
          "Independent long-double mass/GM/gravity dimensions");
    check(
        near(
            properties.surface_gravity_metres_per_second_squared * r * r,
            properties.gravitational_parameter_metres_cubed_per_second_squared),
        "GM equals gR squared");
    for (const auto tick : {SimulationTick{0}, SimulationTick{12345},
                            SimulationTick{9007199254740993ULL}}) {
      const auto rotation = need(resolve_physical_minor_rotation(minor, tick));
      const auto bodies = need(resolve_body_ephemerides(
          need(make_body_hierarchy(catalog)), {tick, 0}));
      const auto it =
          std::find_if(bodies.begin(), bodies.end(),
                       [&](const auto& b) { return b.body == minor.body; });
      check(it != bodies.end() && rotation.body_position == it->position &&
                rotation.body_velocity == it->velocity,
            "Rotation retains actual hierarchy ephemeris at original tick");
      check(rotation.descriptor == minor && rotation.tick == tick &&
                rotation.cycle_tick == tick % source.spin_period_ticks,
            "Exact epoch clock, no rounded epoch ticks");
      const long double phase =
          (static_cast<long double>(tick % source.spin_period_ticks) /
               source.spin_period_ticks +
           static_cast<long double>(source.epoch_spin_phase_turns) /
               4294967296.0L) *
          2 * std::numbers::pi_v<long double>;
      const long double expected_phase =
          std::remainder(phase, 2 * std::numbers::pi_v<long double>);
      check(near(rotation.phase_radians, expected_phase, 5e-15L) &&
                near(rotation.angular_velocity_radians_per_second.z,
                     2 * std::numbers::pi_v<long double> * kSimulationHz /
                         source.spin_period_ticks),
            "Source spin independent phase/rate reference");
      const auto offset = need(
          resolve_physical_minor_rotation(minor, tick, {1000, 2000, -3000}));
      const auto star = offset.star_position_fixed;
      const long double x = star.x - 1000.0L, y = star.y - 2000.0L,
                        z = star.z + 3000.0L;
      const long double distance = std::sqrt(x * x + y * y + z * z);
      check(near(offset.observer_star_distance_metres, distance) &&
                near(offset.observer_to_star_fixed.x, x / distance) &&
                near(offset.observer_to_star_fixed.y, y / distance) &&
                near(offset.observer_to_star_fixed.z, z / distance),
            "Finite actual observer-to-own-star illumination");
    }
    for (auto word :
         {minor.body.value, static_cast<std::uint64_t>(minor.radius_metres),
          static_cast<std::uint64_t>(minor.spin_period_ticks),
          std::bit_cast<std::uint64_t>(properties.mass_kilograms)}) {
      for (unsigned i = 0; i < 8; ++i) {
        fingerprint ^= (word >> (8 * i)) & 255U;
        fingerprint *= 1099511628211ULL;
      }
    }
    check(
        !generate_physical_minor_descriptor(catalog, catalog.bodies[0].node.id),
        "Moon is not a minor");
    check(!generate_physical_minor_descriptor(catalog, source.node.id, 2),
          "Unsupported physical recipe refuses");
    {
      auto bad = minor;
      ++bad.radius_metres;
      check(!validate_physical_minor_descriptor(bad), "Radius tamper refuses");
    }
    {
      auto bad = minor;
      bad.density_kilograms_per_cubic_metre =
          std::numeric_limits<double>::quiet_NaN();
      check(!resolve_physical_minor_properties(bad),
            "Nonfinite density refuses before math");
    }
    {
      auto bad = minor;
      bad.spin_pole_system.x = -0.0;
      check(!validate_physical_minor_descriptor(bad),
            "Noncanonical pole zero refuses");
    }
    {
      auto bad = minor;
      bad.catalog.bodies[1].tint.red ^= 1U;
      check(!validate_physical_minor_descriptor(bad),
            "Source catalog tamper refuses");
    }
    const auto foreign = need(generate_secondary_body_catalog(
        need(generate_physical_origin_system(Seed{seed + 1}, 2))));
    check(!generate_physical_minor_descriptor(foreign, minor.body),
          "Foreign source identity refuses");
    check(!resolve_physical_minor_rotation(
              minor, std::numeric_limits<SimulationTick>::max()) &&
              !resolve_physical_minor_rotation(
                  minor, 0, {std::numeric_limits<double>::infinity(), 0, 0}) &&
              !resolve_physical_minor_rotation(minor, 0, {1e13, 0, 0}),
          "Invalid tick/nonfinite/excessive observer refuses");
  }
  std::cout << "Physical minor bounded fingerprint " << fingerprint << '\n';
}
} // namespace
int main() {
  try {
    run();
  } catch (const std::exception& error) {
    check(false, error.what());
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
