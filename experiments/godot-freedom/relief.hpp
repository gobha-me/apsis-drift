#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

#include "apsis_drift/terrain_tiles.hpp"

namespace apsis_drift::godot_spike {

// Opt-in recipe for the native experiment ONLY. Not terrain-generator v2, not
// save-compatible production terrain. Both snapshot and flight use this C++
// function; the Godot frontend never synthesizes terrain geometry.
inline constexpr unsigned kExperimentalReliefVersion = 1;

inline auto relief_mix(std::uint64_t x) -> std::uint64_t {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

inline auto relief_noise(Seed seed, const std::array<double, 3>& p,
                         double wavelength) -> double {
  std::array<std::int64_t, 3> cell{};
  std::array<double, 3> weight{};
  for (unsigned axis = 0; axis < 3; ++axis) {
    const auto scaled = p[axis] / wavelength;
    cell[axis] = static_cast<std::int64_t>(std::floor(scaled));
    const double t = scaled - static_cast<double>(cell[axis]);
    weight[axis] = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
  }
  std::array<double, 8> corner{};
  for (unsigned i = 0; i < 8; ++i) {
    auto hash = seed.value ^ 0x46524545444f4d31ULL; // FREEDOM1 namespace
    for (unsigned axis = 0; axis < 3; ++axis)
      hash = relief_mix(
          hash ^ static_cast<std::uint64_t>(cell[axis] + ((i >> axis) & 1U)));
    corner[i] = static_cast<double>(hash >> 11) / 9007199254740992.0;
  }
  const auto lerp = [](double a, double b, double t) {
    return a + (b - a) * t;
  };
  return lerp(lerp(lerp(corner[0], corner[1], weight[0]),
                   lerp(corner[2], corner[3], weight[0]), weight[1]),
              lerp(lerp(corner[4], corner[5], weight[0]),
                   lerp(corner[6], corner[7], weight[0]), weight[1]),
              weight[2]);
}

inline auto relief_offset(Seed planet_seed,
                          std::array<double, 3> reference_position,
                          unsigned version) -> double {
  if (version > kExperimentalReliefVersion)
    throw std::invalid_argument("unsupported experimental relief version");
  for (const auto component : reference_position)
    if (!std::isfinite(component) || std::abs(component) > 1.0e9)
      throw std::invalid_argument("invalid relief coordinate");
  if (version == 0) return 0.0;
  const auto seed = derive_terrain_generation_seed(
      planet_seed, TerrainGenerationStream::detail);
  // Smooth 3D sampling of a reference sphere: no cube-face or longitude seam.
  // Domain warp bends the ridge network without changing global world identity.
  for (unsigned axis = 0; axis < 3; ++axis) {
    const auto warp_seed = Seed{relief_mix(seed.value + axis)};
    reference_position[axis] +=
        (relief_noise(warp_seed, reference_position, 9500) - 0.5) * 1600;
  }
  constexpr std::array wavelengths{6200.0, 2300.0, 850.0, 320.0};
  constexpr std::array amplitudes{1550.0, 650.0, 210.0, 65.0};
  double elevation = -750.0;
  double previous_ridge = 1.0;
  for (unsigned octave = 0; octave < wavelengths.size(); ++octave) {
    const auto octave_seed = Seed{relief_mix(seed.value + 31U * octave)};
    const double ridge =
        1.0 - std::abs(2.0 * relief_noise(octave_seed, reference_position,
                                          wavelengths[octave]) -
                       1.0);
    elevation += ridge * ridge * previous_ridge * amplitudes[octave];
    previous_ridge = std::clamp(ridge * 1.8, 0.0, 1.0);
  }
  return std::round(elevation * 100.0) / 100.0;
}

inline auto with_relief(TerrainSurfaceSample sample,
                        const PlanetDescriptor& planet,
                        PlanetFixedPositionMetres position, unsigned version)
    -> TerrainSurfaceSample {
  if (version == 0)
    return sample; // Exact original v1 path, including arithmetic.
  auto coordinate = geodetic_from_planet_fixed(planet, position);
  if (!coordinate)
    throw std::invalid_argument("invalid relief surface direction");
  coordinate->altitude_metres = 0;
  const auto reference = planet_fixed_from_geodetic(planet, *coordinate);
  if (!reference)
    throw std::invalid_argument("invalid relief reference sphere");
  sample.elevation_metres += relief_offset(
      planet.seed, {reference->x, reference->y, reference->z}, version);
  return sample;
}
} // namespace apsis_drift::godot_spike
