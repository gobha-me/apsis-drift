#pragma once

#include <array>
#include <bit>
#include <cmath>

#include "apsis_drift/terrain_local_detail.hpp"

// Shared numerical lattice for the existing planet recipe2 and moon recipe1.
// World authentication and each recipe's scales remain in their consumers.
namespace apsis_drift::terrain_detail_internal {
using Error = TerrainLocalDetailError;
[[nodiscard]] inline auto mix64(std::uint64_t value) noexcept -> std::uint64_t {
  value ^= value >> 30U;
  value *= 0xBF58476D1CE4E5B9ULL;
  value ^= value >> 27U;
  value *= 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

[[nodiscard]] inline auto lattice_value(Seed seed, std::int64_t x,
                                        std::int64_t y, std::int64_t z) noexcept
    -> double {
  auto value = seed.value;
  value ^= mix64(static_cast<std::uint64_t>(x) + 0x9E3779B97F4A7C15ULL);
  value ^= std::rotl(
      mix64(static_cast<std::uint64_t>(y) + 0xD1B54A32D192ED03ULL), 21);
  value ^= std::rotl(
      mix64(static_cast<std::uint64_t>(z) + 0x94D049BB133111EBULL), 42);
  return static_cast<double>(mix64(value) >> 11U) / 4'503'599'627'370'496.0 -
         1.0;
}

[[nodiscard]] inline auto quintic(double t) noexcept -> double {
  return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

[[nodiscard]] inline auto lerp(double first, double second,
                               double fraction) noexcept -> double {
  return first + (second - first) * fraction;
}

template <class Point>
[[nodiscard]] inline auto noise(Point position, double spacing, Seed seed)
    -> std::expected<double, Error> {
  const std::array coordinates{position.x / spacing, position.y / spacing,
                               position.z / spacing};
  std::array<std::int64_t, 3> cell{};
  std::array<double, 3> fraction{};
  for (std::size_t index = 0; index < coordinates.size(); ++index) {
    const double coordinate = coordinates[index];
    // Validate before float-to-integer conversion or a neighbor increment.
    if (!std::isfinite(coordinate) || std::abs(coordinate) > 100'000'000.0)
      return std::unexpected{Error::unsafe_arithmetic};
    const double floor = std::floor(coordinate);
    cell[index] = static_cast<std::int64_t>(floor);
    fraction[index] = quintic(coordinate - floor);
  }
  const auto x = cell[0], y = cell[1], z = cell[2];
  const auto tx = fraction[0], ty = fraction[1], tz = fraction[2];
  const auto x00 =
      lerp(lattice_value(seed, x, y, z), lattice_value(seed, x + 1, y, z), tx);
  const auto x10 = lerp(lattice_value(seed, x, y + 1, z),
                        lattice_value(seed, x + 1, y + 1, z), tx);
  const auto x01 = lerp(lattice_value(seed, x, y, z + 1),
                        lattice_value(seed, x + 1, y, z + 1), tx);
  const auto x11 = lerp(lattice_value(seed, x, y + 1, z + 1),
                        lattice_value(seed, x + 1, y + 1, z + 1), tx);
  return lerp(lerp(x00, x10, ty), lerp(x01, x11, ty), tz);
}

} // namespace apsis_drift::terrain_detail_internal
