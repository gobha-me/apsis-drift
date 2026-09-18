#include "flight_navigation.hpp"
#include "relief.hpp"
#include <iostream>
#include <set>
using namespace apsis_drift;
using namespace apsis_drift::flight_lab;
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
int main() {
  try {
    const auto planet = generate_planet_descriptor(Seed{42});
    const double radius = planet.radius.value * 1000.0;
    const double mu =
        9.80665 * planet.surface_gravity.value / 1000.0 * radius * radius;
    auto circle = initial(planet, {.25, .4, 250000}, 0);
    circle.velocity = circle.back * (-std::sqrt(mu / length(circle.position)));
    const auto reference = orbit_info(planet, circle);
    check(reference.clear_orbit &&
              std::abs(reference.periapsis - 250000) < .01 &&
              std::abs(reference.apoapsis - 250000) < .01,
          "circular orbit telemetry");
    for (int i = 0; i < 120 * 600; ++i)
      advance(planet, 0, circle, {0, 0, 0, 0, 0, 0, 0, false});
    check(std::abs(length(circle.position) - radius - 250000) < 100,
          "coasting orbit drift");
    auto invalid = circle;
    invalid.velocity.x = NAN;
    bool rejected = false;
    try {
      (void)orbit_info(planet, invalid);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    check(rejected, "nonfinite navigation accepted");
    const auto stars = sky_catalog(Seed{42}), repeated = sky_catalog(Seed{42}),
               other = sky_catalog(Seed{43});
    std::set<std::uint64_t> identities;
    std::array<int, 96> sky_bins{};
    check(stars.size() == 1536, "bounded sky catalog");
    for (std::size_t i = 0; i < stars.size(); ++i) {
      check(identities.insert(stars[i].system_seed.value).second,
            "duplicate star identity");
      check(stars[i].system_seed == repeated[i].system_seed &&
                stars[i].direction == repeated[i].direction,
            "star determinism");
      check(stars[i].system_seed != other[i].system_seed &&
                std::abs(length(stars[i].direction) - 1) < 1e-12,
            "star seed isolation/direction");
      check(stars[i].color ==
                generate_local_system(stars[i].system_seed).star.color,
            "star descriptor mismatch");
      const auto &d = stars[i].direction;
      const int longitude =
          std::clamp(int((std::atan2(d.z, d.x) + std::numbers::pi) /
                         (2 * std::numbers::pi) * 12),
                     0, 11);
      const int latitude = std::clamp(int((d.y + 1) * 4), 0, 7);
      ++sky_bins[latitude * 12 + longitude];
    }
    check(std::count(sky_bins.begin(), sky_bins.end(), 0) < 5,
          "stars clustered into correlated arcs");
    // Test-only pilot drives the same actuator demands as the controller.
    // No relocation, velocity injection or game autopilot during this journey.
    auto cache = TerrainTileCache::create().value();
    auto ground = [&](V position) {
      const PlanetFixedPositionMetres fixed{position.x, position.y, position.z};
      return godot_spike::with_relief(
                 sample_planet_surface(planet, fixed, 8, cache).value(), planet,
                 fixed, 1)
          .elevation_metres;
    };
    auto launch = initial(planet, {.25, .4, 0}, 0);
    auto s = initial(planet, {.25, .4, ground(launch.position) + 60}, 0);
    const V plane_normal = s.right;
    int phase = 0, coast_ticks = 0;
    double peak_alt = 0, peak_q = 0, orbit_time = 0, entry_time = 0;
    for (int tick = 0; tick < 120 * 5000; ++tick) {
      const double r = length(s.position), alt = r - radius;
      const V radial = unit(s.position),
              tangent = unit(cross(plane_normal, radial)) * (-1);
      const auto info = orbit_info(planet, s);
      Demand d;
      d.assist = false;
      V desired = tangent;
      if (phase == 0) {
        const double target_climb =
            std::clamp((250000 - alt) * .012, -800.0, 1600.0);
        const double radial_accel =
            std::clamp((target_climb - info.radial_speed) * .10 + mu / (r * r) -
                           info.horizontal_speed * info.horizontal_speed / r,
                       -40.0, 40.0);
        const double tangent_accel =
            alt < 60000
                ? 0.0
                : std::clamp((info.circular_speed - info.horizontal_speed) *
                                 .035,
                             -30.0, 40.0);
        const V acceleration = radial * radial_accel + tangent * tangent_accel;
        desired = unit(acceleration);
        d.main = std::clamp(length(acceleration) / main_accel, 0.0, 1.0);
        if (tick < 240)
          d.heave = 1;
        if (info.clear_orbit && info.eccentricity < .035 && alt > 180000) {
          phase = 1;
          orbit_time = tick / 120.0;
          std::cout << "Orbit acquired at " << orbit_time << " s, altitude "
                    << alt << ", peri/apo " << info.periapsis << " / "
                    << info.apoapsis << " m\n";
        }
      } else if (phase == 1) {
        if (++coast_ticks == 120 * 120) {
          check(info.clear_orbit, "orbit failed during unpowered coast");
          phase = 2;
        }
      } else if (phase == 2) {
        desired = unit(s.velocity) * (-1);
        if (dot(desired, s.back * (-1)) > .97)
          d.main = 1;
        if (info.periapsis < 30000)
          phase = 3;
      } else {
        desired = unit(s.velocity);
        if (alt < info.atmosphere_edge && entry_time == 0 &&
            info.radial_speed < 0)
          entry_time = tick / 120.0;
        if (alt < 10000 && info.radial_speed < 0) {
          check(entry_time > orbit_time && peak_q > 1000,
                "atmospheric return not demonstrated");
          std::cout << "Atmospheric return at " << entry_time << " s; 10 km at "
                    << tick / 120.0 << " s; speed " << length(s.velocity)
                    << " m/s; max q " << peak_q << " Pa; peak altitude "
                    << peak_alt << " m\n";
          std::cout
              << "Journey + navigation + identified-star contracts passed\n";
          return 0;
        }
      }
      if (phase != 1) {
        const double angle =
            std::atan2(dot(desired, s.up), dot(desired, s.back * (-1)));
        d.pitch = std::clamp(angle * 2.2 - s.angular.x * .6, -1.0, 1.0);
      }
      advance(planet, ground(s.position), s, d);
      check(!s.floor_guard, "journey touched test floor");
      peak_alt = std::max(peak_alt, alt);
      peak_q = std::max(peak_q, s.dynamic_pressure);
    }
    throw std::runtime_error(
        "journey did not complete before bounded time limit");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
