#include "apsis_drift/moon_terrain.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace apsis_drift;
int checks{}, failures{};
std::uint64_t fingerprint{14695981039346656037ULL};
auto check(bool pass, std::string_view message) -> void {
  ++checks;
  if (!pass) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Moon terrain fixture refused");
  return std::move(*value);
}
auto word(std::uint64_t value) -> void {
  for (unsigned i = 0; i < 8; ++i) {
    fingerprint ^= (value >> (8 * i)) & 255;
    fingerprint *= 1099511628211ULL;
  }
}
auto fixture(Seed seed) -> MoonTerrainSampler {
  const auto catalog = need(generate_secondary_body_catalog(
      need(generate_physical_origin_system(seed, 2))));
  return need(MoonTerrainSampler::create(need(
      generate_physical_moon_descriptor(catalog, catalog.bodies[0].node.id))));
}
auto field_checks(const MoonTerrainSampler& terrain) -> void {
  const std::array directions{
      MoonFixedDirection{1, 0, 0},       MoonFixedDirection{-1, 0, 0},
      MoonFixedDirection{0, 1, 0},       MoonFixedDirection{0, 0, -1},
      MoonFixedDirection{1, 1, 1},       MoonFixedDirection{-4, 3, 2},
      MoonFixedDirection{0.1, -0.4, 0.9}};
  for (const auto direction : directions) {
    const auto value = need(terrain.sample(direction));
    check(value == need(terrain.sample(direction)) &&
              std::abs(value.elevation_metres) <=
                  kMoonTerrainMaximumReliefMetres,
          "Repeated field is exact and within the authored relief bound");
    const auto surface = need(terrain.surface_position(direction));
    check(std::abs(need(terrain.clearance(surface))) < 2e-9,
          "Collision clearance consumes the rendered surface height");
    const auto radius = std::hypot(surface.x, surface.y, surface.z);
    check(std::abs((radius - terrain.owner().radius_metres) -
                   value.elevation_metres) < 2e-9,
          "Surface position uses real moon radius and sampled relief");
    const MoonFixedPositionMetres above{surface.x * (1 + 10 / radius),
                                        surface.y * (1 + 10 / radius),
                                        surface.z * (1 + 10 / radius)};
    check(std::abs(need(terrain.clearance(above)) - 10) < 2e-8,
          "Radial ten-metre offset has ten-metre clearance");
    word(std::bit_cast<std::uint64_t>(value.elevation_metres));
    word(value.color.red);
    word(value.color.green);
    word(value.color.blue);
  }
  check(need(terrain.sample({1, 1, 1})) ==
                need(terrain.sample({100, 100, 100})) &&
            need(terrain.sample({1, 0, 0})) ==
                need(terrain.sample({1, -0.0, 0})),
        "Direction scale and signed zero do not create surface identities");
}
auto tile_checks(const MoonTerrainSampler& terrain) -> void {
  const auto body = terrain.owner().body;
  const MoonTerrainSample sentinel{123456, {1, 2, 3}};
  std::vector<MoonTerrainSample> tile(kTerrainTileSampleCount + 1, sentinel);
  check(terrain.fill_tile({body, CubeFace::positive_x, 0, 0, 0},
                          kTerrainTileSamplesPerAxis, tile)
                .has_value() &&
            tile.back() == sentinel,
        "65-square fill preserves the caller's trailing element");
  for (const auto index : {std::size_t{0}, std::size_t{32}, std::size_t{64}}) {
    for (const auto row : {std::size_t{0}, std::size_t{32}, std::size_t{64}}) {
      // Independent positive-X cube topology; no address helper in this oracle.
      const MoonFixedDirection direction{1, static_cast<double>(index) / 32 - 1,
                                         static_cast<double>(row) / 32 - 1};
      check(tile[row * kTerrainTileSamplesPerAxis + index] ==
                need(terrain.sample(direction)),
            "Tile vertices equal the direct field at independently derived "
            "coordinates");
    }
  }
  std::vector<MoonTerrainSample> neighbor(kTerrainTileSampleCount);
  check(terrain
            .fill_tile({body, CubeFace::positive_y, 0, 0, 0},
                       kTerrainTileSamplesPerAxis, neighbor)
            .has_value(),
        "Neighbor cube tile admitted");
  for (std::size_t row = 0; row < kTerrainTileSamplesPerAxis; ++row)
    check(tile[row * kTerrainTileSamplesPerAxis + 64] ==
              neighbor[row * kTerrainTileSamplesPerAxis],
          "Shared cube edge has identical continuous terrain and color");
  const auto before = tile;
  auto wrong = body;
  wrong.kind = BodyKind::planet;
  check(!terrain.fill_tile({wrong, CubeFace::positive_x, 0, 0, 0}, 65, tile) &&
            tile == before,
        "Same word with wrong body kind refuses without writing");
  for (const auto key :
       {MoonTerrainTileKey{body, static_cast<CubeFace>(255), 0, 0, 0},
        MoonTerrainTileKey{body, CubeFace::positive_x, 17, 0, 0},
        MoonTerrainTileKey{body, CubeFace::positive_x, 1, 2, 0}})
    check(!terrain.fill_tile(key, 65, tile) && tile == before,
          "Invalid face, LOD or index refuses atomically");
  check(!terrain.fill_tile({body, CubeFace::positive_x, 0, 0, 0}, 64, tile) &&
            tile == before,
        "Invalid dimensions refuse before any output");
  check(
      !terrain.fill_tile({body, CubeFace::positive_x, 0, 0, 0}, 65,
                         std::span{tile}.first(kTerrainTileSampleCount - 1)) &&
          tile == before,
      "Short output buffer refuses atomically");
  std::vector<MoonTerrainSample> fine(kTerrainTileSampleCount);
  check(terrain.fill_tile({body, CubeFace::positive_x, 1, 0, 0}, 65, fine)
            .has_value(),
        "Finer mesh admitted");
  check(fine[0] == tile[0] && fine[64] == tile[32] &&
            fine[64 * 65 + 64] == tile[32 * 65 + 32],
        "LOD changes sample locations without replacing the height field");
}
auto invalid_checks(const MoonTerrainSampler& terrain) -> void {
  auto forged = terrain.owner();
  ++forged.radius_metres;
  check(!MoonTerrainSampler::create(forged), "Forged physical owner refuses");
  check(!MoonTerrainSampler::create(terrain.owner(), 2),
        "Unknown terrain recipe refuses");
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto inf = std::numeric_limits<double>::infinity();
  for (const auto value : {MoonFixedDirection{}, MoonFixedDirection{nan, 0, 1},
                           MoonFixedDirection{1, inf, 0}})
    check(!terrain.sample(value) && !terrain.surface_position(value),
          "Zero/nonfinite direction refuses before lattice conversion");
  check(!terrain.clearance({0, 0, 0}) && !terrain.clearance({inf, 0, 0}) &&
            !terrain.clearance({0, nan, 1}),
        "Center/nonfinite contact position refuses");
}
} // namespace
int main() {
  try {
    const auto home = fixture(Seed{42});
    field_checks(home);
    tile_checks(home);
    invalid_checks(home);
    const auto other = fixture(Seed{43});
    field_checks(other);
    check(need(home.sample({1, 0, 0})) != need(other.sample({1, 0, 0})),
          "Different authenticated moon streams produce distinct terrain");
  } catch (const std::exception& error) {
    check(false, error.what());
  }
  std::cout << "Moon terrain fingerprint " << fingerprint << '\n';
  std::cout << checks << " moon terrain checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
