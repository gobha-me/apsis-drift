#pragma once
#include "apsis_drift/local_system.hpp"
#include "thrust_flight.hpp"
#include <numbers>

namespace apsis_drift::flight_lab {
struct OrbitInfo {
  double radial_speed{}, horizontal_speed{}, circular_speed{};
  double periapsis{}, apoapsis{}, eccentricity{}, atmosphere_edge{};
  bool bound{}, clear_orbit{};
};
inline auto orbit_info(const PlanetDescriptor& p, const State& s) -> OrbitInfo {
  validate(s);
  const double radius = p.radius.value * 1000.0;
  const double mu =
      9.80665 * p.surface_gravity.value / 1000.0 * radius * radius;
  if (!(mu > 0)) throw std::invalid_argument("invalid orbital gravity");
  const double r = length(s.position), v2 = dot(s.velocity, s.velocity);
  const V radial = unit(s.position), h = cross(s.position, s.velocity);
  const double e = length(cross(s.velocity, h) * (1 / mu) - radial);
  const double energy = v2 * .5 - mu / r;
  OrbitInfo o;
  o.radial_speed = dot(s.velocity, radial);
  o.horizontal_speed =
      std::sqrt(std::max(0.0, v2 - o.radial_speed * o.radial_speed));
  o.circular_speed = std::sqrt(mu / r);
  o.eccentricity = e;
  o.bound = energy < 0;
  o.periapsis = dot(h, h) / mu / (1 + e) - radius;
  o.apoapsis = o.bound ? (-mu / energy) - (o.periapsis + radius) - radius : -1;
  o.atmosphere_edge =
      std::max(0.0, 8500 * std::log(std::max(1.0, density(p, 0) / 1e-6)));
  o.clear_orbit = o.bound && o.periapsis > std::max(20000.0, o.atmosphere_edge);
  return o;
}

struct SkyStar {
  Seed system_seed;
  V direction;
  Rgb8 color;
  double brightness;
};
// Isolated preview-catalog v1; no changes to the authored first-route catalog.
// Independent ordinal derivations, no mutable generator state or visual RNG.
inline auto sky_catalog(Seed study_seed) -> std::vector<SkyStar> {
  const Seed catalog =
      derive_seed(study_seed, SeedDomain::navigation, 0x534b5901);
  std::vector<SkyStar> stars;
  for (std::uint64_t i = 0; i < 1536; ++i) {
    const Seed system = derive_seed(catalog, SeedDomain::system, i);
    auto fraction = [&](std::uint64_t n) {
      // Seed derivation identifies a stream; its FNV output is not itself
      // uniformly decorrelated random samples. Avalanche each named sample.
      auto bits = derive_seed(system, SeedDomain::navigation, n).value;
      bits = (bits ^ (bits >> 30)) * 0xbf58476d1ce4e5b9ULL;
      bits = (bits ^ (bits >> 27)) * 0x94d049bb133111ebULL;
      bits ^= bits >> 31;
      return double(bits >> 11) * 0x1.0p-53;
    };
    const double z = 2 * fraction(0) - 1,
                 a = 2 * std::numbers::pi * fraction(1);
    const double h = std::sqrt(std::max(0.0, 1 - z * z));
    const auto descriptor = generate_local_system(system);
    stars.push_back({system,
                     {h * std::cos(a), z, h * std::sin(a)},
                     descriptor.star.color,
                     .25 + fraction(2) * .75});
  }
  return stars;
}
} // namespace apsis_drift::flight_lab
