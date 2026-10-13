#include "apsis_drift/terrain_local_detail.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <source_location>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace apsis_drift;
using Error = TerrainLocalDetailError;
int failures{};

auto check(bool condition, std::string_view reason,
           std::source_location where = std::source_location::current())
    -> void {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL line " << where.line() << ": " << reason << '\n';
}

template <typename T, typename E>
auto required(std::expected<T, E> result,
              std::source_location where = std::source_location::current())
    -> T {
  if (!result) {
    std::cerr << "Fixture refused at line " << where.line() << '\n';
    std::exit(EXIT_FAILURE);
  }
  return std::move(*result);
}

template <typename T>
auto error_is(const std::expected<T, Error>& result, Error expected) -> bool {
  return !result && result.error() == expected;
}

auto sampler(const PhysicalLocalSystem& owner, std::uint32_t version,
             TerrainTileCache& cache) -> TerrainLocalDetailSampler {
  return required(TerrainLocalDetailSampler::create(
      owner, body_id(owner.catalog.planets.front().descriptor.id), {version},
      cache));
}

auto direction_at(const PlanetDescriptor& planet, TerrainTileKey key, double u,
                  double v) -> PlanetFixedDirection {
  const auto position =
      required(planet_fixed_from_terrain_address(planet, {key, u, v}));
  return {position.x, position.y, position.z};
}

auto unit(PlanetFixedDirection direction) -> PlanetFixedDirection {
  const double length = std::hypot(direction.x, direction.y, direction.z);
  return {direction.x / length, direction.y / length, direction.z / length};
}

auto component(PlanetFixedDirection direction, std::size_t axis) -> double {
  return std::array{direction.x, direction.y, direction.z}[axis];
}

auto close_direction(PlanetFixedDirection first, PlanetFixedDirection second)
    -> bool {
  first = unit(first);
  second = unit(second);
  return std::abs(first.x - second.x) < 1e-12 &&
         std::abs(first.y - second.y) < 1e-12 &&
         std::abs(first.z - second.z) < 1e-12;
}

auto edge_uv(std::size_t edge, double t, double inward = 0.0)
    -> std::pair<double, double> {
  switch (edge) {
    case 0: return {inward, t};
    case 1: return {1.0 - inward, t};
    case 2: return {t, inward};
    default: return {t, 1.0 - inward};
  }
}

auto legacy_and_ownership() -> void {
  const auto owner = required(generate_physical_origin_system(Seed{42}));
  auto cache = required(TerrainTileCache::create(3));
  const auto old = sampler(owner, kLegacyPhysicalTerrainRecipe, cache);
  auto reference_cache = required(TerrainTileCache::create(3));
  check(old.owner() == owner && old.body() == body_id(old.planet().id) &&
            old.recipe() == TerrainLocalDetailRecipe{1},
        "sampler retains its complete selected owner, body and recipe");
  constexpr std::array directions{
      PlanetFixedDirection{1, 0, 0},       PlanetFixedDirection{-1, 0, 0},
      PlanetFixedDirection{0, 1, 0},       PlanetFixedDirection{0, -1, 0},
      PlanetFixedDirection{0, 0, 1},       PlanetFixedDirection{0, 0, -1},
      PlanetFixedDirection{1, 1, 0},       PlanetFixedDirection{-1, -1, -1},
      PlanetFixedDirection{.73, -.19, .41}};
  for (const auto lod : std::array<std::uint8_t, 4>{0, 4, 8, 16}) {
    for (const auto direction : directions) {
      const auto expected = required(sample_planet_surface(
          old.planet(), {direction.x, direction.y, direction.z}, lod,
          reference_cache));
      const auto actual = required(old.sample(direction, lod));
      check(actual.elevation_metres == expected.elevation_metres &&
                actual.macro_elevation_metres == expected.elevation_metres &&
                actual.local_offset_metres == 0 &&
                actual.color == expected.color &&
                !actual.tangent_slope_estimate,
            "recipe1 exactly preserves legacy requested-LOD height/color");
    }
  }
  check(owner == required(generate_physical_origin_system(Seed{42})),
        "read-only sampling changed its catalog owner");
  check(cache.size() <= cache.capacity() && reference_cache.size() <= 3,
        "legacy sampling exceeded its caller's cache capacity");

  // Existing independent generator1 compatibility vectors, not new goldens
  // inferred from this provider's implementation.
  struct GoldenTile {
    std::uint64_t seed;
    CubeFace face;
    std::uint8_t lod;
    std::uint32_t x;
    std::uint32_t y;
    std::uint64_t checksum;
  };
  constexpr std::array tiles{
      GoldenTile{0, CubeFace::positive_x, 0, 0, 0, 9797442332981214159ULL},
      GoldenTile{42, CubeFace::positive_z, 4, 7, 11, 743763593216380847ULL},
      GoldenTile{std::numeric_limits<std::uint64_t>::max(),
                 CubeFace::negative_y, 16, 65535, 0, 6857593874197516006ULL}};
  for (const auto& fixture : tiles) {
    const auto planet = generate_planet_descriptor(Seed{fixture.seed});
    const auto tile = required(generate_terrain_tile(
        planet, {planet.id, fixture.face, fixture.lod, fixture.x, fixture.y}));
    check(tile.checksum() == fixture.checksum,
          "local detail selection changed a frozen generator1 tile");
  }
  check(derive_terrain_generation_seed(Seed{42}, TerrainGenerationStream::shape)
                    .value == 12495169707215482604ULL &&
            derive_terrain_generation_seed(Seed{42},
                                           TerrainGenerationStream::detail)
                    .value == 745371854408699215ULL,
        "existing terrain stream ordinals changed");
  const auto relief = derive_terrain_generation_seed(
      Seed{42}, TerrainGenerationStream::local_relief);
  check(relief != derive_terrain_generation_seed(
                      Seed{42}, TerrainGenerationStream::shape) &&
            relief != derive_terrain_generation_seed(
                          Seed{42}, TerrainGenerationStream::detail),
        "local relief is not an independent named stream");
}

auto invalid_inputs() -> void {
  const auto owner = required(generate_physical_origin_system(Seed{42}));
  auto cache = required(TerrainTileCache::create(2));
  const auto body = body_id(owner.catalog.planets.front().descriptor.id);
  for (const auto version :
       {0U, 3U, std::numeric_limits<std::uint32_t>::max()}) {
    check(error_is(
              TerrainLocalDetailSampler::create(owner, body, {version}, cache),
              Error::unsupported_version),
          "unsupported recipe accepted");
  }
  auto corrupt = owner;
  ++corrupt.stellar_gm_km3_per_second2;
  check(error_is(TerrainLocalDetailSampler::create(corrupt, body, {2}, cache),
                 Error::invalid_owner),
        "unauthenticated catalog accepted");
  auto unsupported_owner = owner;
  unsupported_owner.generator_version = 999;
  check(!TerrainLocalDetailSampler::create(unsupported_owner, body, {2}, cache),
        "unsupported catalog version accepted");
  for (const auto invalid_body :
       std::array{BodyId{999, BodyKind::planet, body.value},
                  body_id(owner.catalog.star.id),
                  BodyId{1, BodyKind::moon, body.value},
                  BodyId{1, BodyKind::minor, body.value},
                  BodyId{1, BodyKind::planet, body.value ^ 1ULL}}) {
    check(error_is(TerrainLocalDetailSampler::create(owner, invalid_body, {2},
                                                     cache),
                   Error::invalid_body),
          "unsupported or absent body accepted");
  }
  const auto detail = sampler(owner, 2, cache);
  constexpr double nan = std::numeric_limits<double>::quiet_NaN();
  constexpr double inf = std::numeric_limits<double>::infinity();
  for (const auto direction : std::array{
           PlanetFixedDirection{}, PlanetFixedDirection{nan, 0, 1},
           PlanetFixedDirection{0, inf, 1}, PlanetFixedDirection{0, 1, -inf},
           PlanetFixedDirection{1, 0, nan}}) {
    check(error_is(detail.sample(direction, 8), Error::invalid_direction),
          "zero or nonfinite direction accepted");
  }
  check(!detail.sample({1, 0, 0}, kMaxTerrainLod + 1),
        "invalid output LOD accepted");
  const auto huge =
      detail.sample({std::numeric_limits<double>::max(), 0, 0}, 8);
  if (huge) {
    check(*huge == required(detail.sample({1, 0, 0}, 8)),
          "safely normalized huge direction changed the radial query");
  } else {
    check(huge.error() == Error::invalid_direction ||
              huge.error() == Error::unsafe_arithmetic,
          "huge finite direction failed for an unrelated reason");
  }
  constexpr double maximum = std::numeric_limits<double>::max();
  const auto overflowing_length = detail.sample({maximum, maximum, maximum}, 8);
  if (overflowing_length) {
    check(*overflowing_length == required(detail.sample({1, 1, 1}, 8)),
          "safely normalized huge corner direction changed the radial query");
  } else {
    check(overflowing_length.error() == Error::invalid_direction ||
              overflowing_length.error() == Error::unsafe_arithmetic,
          "overflowing legacy direction length returned an unrelated error");
  }

  const TerrainLocalDetailSample sentinel{
      -9876, 5432, -4444, {7, 8, 9}, PlanetFixedDirection{3, 4, 5}};
  std::vector output(kTerrainTileSampleCount + 3, sentinel);
  const auto initial = output;
  const TerrainTileKey valid{detail.planet().id, CubeFace::positive_x, 8, 127,
                             128};
  auto refused = [&](TerrainTileKey key, std::size_t size,
                     std::span<TerrainLocalDetailSample> buffer, Error error) {
    check(error_is(detail.fill_tile(key, size, buffer), error),
          "invalid tile/buffer did not refuse with its classified error");
    check(output == initial,
          "refused tile fill partially modified caller output");
  };
  for (const auto size : std::array<std::size_t, 5>{
           0, 1, 64, 66, std::numeric_limits<std::size_t>::max()}) {
    refused(valid, size, output, Error::invalid_dimensions);
  }
  refused(valid, 65, {}, Error::short_buffer);
  refused(valid, 65, std::span(output).first(kTerrainTileSampleCount - 1),
          Error::short_buffer);
  auto invalid_key = valid;
  invalid_key.planet.value ^= 1ULL;
  refused(invalid_key, 65, output, Error::invalid_tile);
  invalid_key = valid;
  invalid_key.face = static_cast<CubeFace>(255);
  refused(invalid_key, 65, output, Error::invalid_tile);
  invalid_key = valid;
  invalid_key.lod = kMaxTerrainLod + 1;
  refused(invalid_key, 65, output, Error::invalid_tile);
  invalid_key = valid;
  invalid_key.x = 256;
  refused(invalid_key, 65, output, Error::invalid_tile);
  invalid_key = valid;
  invalid_key.y = std::numeric_limits<std::uint32_t>::max();
  refused(invalid_key, 65, output, Error::invalid_tile);
  check(cache.size() <= 2, "refusal paths grew macro cache beyond its bound");
}

auto tile_identity() -> void {
  const auto owner = required(generate_physical_origin_system(Seed{42}));
  auto cache = required(TerrainTileCache::create(12));
  const auto detail = sampler(owner, 2, cache);
  const auto& planet = detail.planet();
  const TerrainLocalDetailSample sentinel{
      -100, 17, -83, {1, 2, 3}, std::nullopt};
  const TerrainTileKey left_key{planet.id, CubeFace::positive_x, 8, 127, 128};
  const TerrainTileKey right_key{planet.id, CubeFace::positive_x, 8, 128, 128};
  const TerrainTileKey child_key{planet.id, CubeFace::positive_x, 9, 254, 256};
  std::vector left(kTerrainTileSampleCount + 2, sentinel);
  std::vector right(kTerrainTileSampleCount, sentinel);
  std::vector child(kTerrainTileSampleCount, sentinel);
  check(detail.fill_tile(left_key, 65, left).has_value(),
        "left tile fill refused");
  check(detail.fill_tile(right_key, 65, right).has_value(),
        "right tile fill refused");
  check(detail.fill_tile(child_key, 65, child).has_value(),
        "child tile fill refused");
  check(left[kTerrainTileSampleCount] == sentinel &&
            left[kTerrainTileSampleCount + 1] == sentinel,
        "successful tile fill changed its unused trailing region");
  for (std::size_t y = 0; y < 65; ++y) {
    check(left[y * 65 + 64] == right[y * 65],
          "same-face neighboring tiles disagree on a shared sample");
    for (std::size_t x = 0; x < 65; ++x) {
      const auto& value = left[y * 65 + x];
      check(std::isfinite(value.elevation_metres) &&
                value.tangent_slope_estimate &&
                value.elevation_metres ==
                    value.macro_elevation_metres + value.local_offset_metres,
            "tile sample is nonfinite, missing slope or inconsistent in its "
            "height decomposition");
      if (x <= 32 && y <= 32) {
        check(value == child[(y * 2) * 65 + x * 2],
              "aligned parent and child output LODs change the physical field");
      }
    }
  }
  constexpr std::array<std::pair<std::size_t, std::size_t>, 4> probe_sites{
      {{0, 0}, {32, 32}, {64, 64}, {17, 43}}};
  for (const auto& [x, y] : probe_sites) {
    const auto direction =
        direction_at(planet, left_key, static_cast<double>(x) / 64,
                     static_cast<double>(y) / 64);
    check(left[y * 65 + x] == required(detail.sample(direction, 8)),
          "tile and point consumers disagree on the same physical location");
    for (const auto lod : std::array<std::uint8_t, 4>{0, 4, 9, 16}) {
      check(required(detail.sample(direction, lod)) == left[y * 65 + x],
            "recipe2 point query depends on requested output LOD");
    }
  }

  // Discover adjacency through public address-to-position queries. No cube
  // face basis, edge orientation table or detail formula is duplicated here.
  struct EdgePoint {
    CubeFace face;
    std::size_t edge;
    double t;
    PlanetFixedDirection direction;
  };
  std::vector<EdgePoint> edges;
  for (std::uint8_t face = 0; face < 6; ++face) {
    for (std::size_t edge = 0; edge < 4; ++edge) {
      for (const double t : {.25, .5, .75}) {
        const auto [u, v] = edge_uv(edge, t);
        const auto kind = static_cast<CubeFace>(face);
        edges.push_back(
            {kind, edge, t,
             direction_at(planet, {planet.id, kind, 0, 0, 0}, u, v)});
      }
    }
  }
  std::size_t unique_edges{};
  std::size_t approached_edges{};
  for (const auto& source : edges) {
    const auto peer =
        std::ranges::find_if(edges, [&](const EdgePoint& candidate) {
          return candidate.face != source.face &&
                 close_direction(source.direction, candidate.direction);
        });
    check(peer != edges.end(),
          "cube edge has no adjacent-face public-address peer");
    if (peer == edges.end()) continue;
    check(required(detail.sample(source.direction, 0)) ==
              required(detail.sample(peer->direction, 16)),
          "physical height/slope disagree across a cube edge representation");
    if (source.face >= peer->face) continue;
    if (source.t == .5) ++unique_edges;
    if (source.t != .25) continue;
    ++approached_edges;
    for (const double epsilon : {1e-9, 1e-11}) {
      const auto [u, v] = edge_uv(source.edge, source.t, epsilon);
      const auto [other_u, other_v] = edge_uv(peer->edge, peer->t, epsilon);
      const auto a = required(detail.sample(
          direction_at(planet, {planet.id, source.face, 0, 0, 0}, u, v), 8));
      const auto b = required(
          detail.sample(direction_at(planet, {planet.id, peer->face, 0, 0, 0},
                                     other_u, other_v),
                        8));
      check(std::abs(a.elevation_metres - b.elevation_metres) < .05,
            "approaching a cube edge reveals a height discontinuity");
      check(a.tangent_slope_estimate && b.tangent_slope_estimate,
            "edge approach lost its finite-scale slope estimate");
      if (a.tangent_slope_estimate && b.tangent_slope_estimate) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
          check(std::abs(component(*a.tangent_slope_estimate, axis) -
                         component(*b.tangent_slope_estimate, axis)) < .002,
                "Cartesian slope estimate jumps across a cube edge");
        }
      }
    }
  }
  check(unique_edges == 12 && approached_edges == 12,
        "seam identity/continuity corpus did not cover all twelve cube edges");

  std::vector<PlanetFixedDirection> corners;
  for (std::uint8_t face = 0; face < 6; ++face) {
    for (const double u : {0.0, 1.0}) {
      for (const double v : {0.0, 1.0}) {
        corners.push_back(direction_at(
            planet, {planet.id, static_cast<CubeFace>(face), 0, 0, 0}, u, v));
      }
    }
  }
  std::vector<bool> visited(corners.size());
  std::size_t unique_corners{};
  for (std::size_t index = 0; index < corners.size(); ++index) {
    if (visited[index]) continue;
    ++unique_corners;
    const auto expected = required(detail.sample(corners[index], 0));
    std::size_t representations{};
    for (std::size_t peer = index; peer < corners.size(); ++peer) {
      if (!close_direction(corners[index], corners[peer])) continue;
      visited[peer] = true;
      ++representations;
      check(required(detail.sample(corners[peer], 16)) == expected,
            "three-face corner representations disagree on the physical field");
    }
    check(representations == 3,
          "cube corner is not shared by exactly three faces");
  }
  check(unique_corners == 8,
        "corner corpus did not cover all eight cube corners");
  check(cache.size() <= cache.capacity(),
        "tile sampling exceeded macro cache bound");
}

auto slope_reconstruction() -> void {
  const auto owner = required(generate_physical_origin_system(Seed{42}));
  auto cache = required(TerrainTileCache::create(12));
  const auto detail = sampler(owner, 2, cache);
  const double radius =
      static_cast<double>(detail.planet().radius.value) * 1000;
  for (const auto raw : std::array{PlanetFixedDirection{1, 0, 0},
                                   PlanetFixedDirection{1, 1, .25},
                                   PlanetFixedDirection{-.31, .47, .73}}) {
    const auto normal = unit(raw);
    const auto actual = required(detail.sample(raw, 11));
    check(actual.tangent_slope_estimate.has_value(),
          "recipe2 provides no slope");
    if (!actual.tangent_slope_estimate) continue;
    std::array<double, 3> gradient{};
    // Six independent public height queries, with no relief/noise formula
    // copied into the test. Vary output LOD to check the selected field's
    // identity while reconstructing its advertised finite-scale derivative.
    for (std::size_t axis = 0; axis < 3; ++axis) {
      std::array<double, 3> plus{normal.x * radius, normal.y * radius,
                                 normal.z * radius};
      auto minus = plus;
      plus[axis] += 32;
      minus[axis] -= 32;
      const auto high = required(detail.sample({plus[0], plus[1], plus[2]}, 4));
      const auto low =
          required(detail.sample({minus[0], minus[1], minus[2]}, 16));
      gradient[axis] = (high.elevation_metres - low.elevation_metres) / 64;
    }
    const double radial = gradient[0] * normal.x + gradient[1] * normal.y +
                          gradient[2] * normal.z;
    double actual_radial{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const double expected = gradient[axis] - radial * component(normal, axis);
      const double value = component(*actual.tangent_slope_estimate, axis);
      check(std::isfinite(value) && std::abs(value - expected) < 1e-7,
            "slope disagrees with independent height stencil and tangent "
            "projection");
      actual_radial += value * component(normal, axis);
    }
    check(std::abs(actual_radial) < 1e-10,
          "advertised tangent slope has a radial component");
  }
}

auto continuous_macro_oracle() -> void {
  const auto owner = required(generate_physical_origin_system(Seed{42}));
  auto cache = required(TerrainTileCache::create(8));
  const auto detail = sampler(owner, 2, cache);
  const auto legacy = sampler(owner, 1, cache);
  const auto& planet = detail.planet();
  const TerrainTileKey key{planet.id, CubeFace::positive_x, 8, 127, 128};
  const auto tile = required(cache.get(planet, key));
  bool tested{};
  for (std::size_t y = 0; y < 64 && !tested; ++y) {
    for (std::size_t x = 0; x < 64 && !tested; ++x) {
      const std::array<long double, 4> corners{
          static_cast<long double>(
              required(tile->sample_at(x, y)).get().elevation_metres),
          static_cast<long double>(
              required(tile->sample_at(x + 1, y)).get().elevation_metres),
          static_cast<long double>(
              required(tile->sample_at(x, y + 1)).get().elevation_metres),
          static_cast<long double>(
              required(tile->sample_at(x + 1, y + 1)).get().elevation_metres)};
      if (*std::ranges::max_element(corners) ==
          *std::ranges::min_element(corners))
        continue;
      const auto direction =
          direction_at(planet, key, (static_cast<double>(x) + .375) / 64,
                       (static_cast<double>(y) + .625) / 64);
      const auto address =
          required(terrain_address_from_planet_direction(planet, direction, 8));
      check(address.tile == key,
            "interior macro oracle moved outside its source tile");
      const long double east = static_cast<long double>(address.u) * 64 -
                               static_cast<long double>(x);
      const long double north = static_cast<long double>(address.v) * 64 -
                                static_cast<long double>(y);
      // Tensor-product corner weights accumulated in extended precision,
      // independent of the provider's nested binary64 lerp expression order.
      const std::array weights{(1 - east) * (1 - north), east * (1 - north),
                               (1 - east) * north, east * north};
      long double expected{};
      for (std::size_t corner = 0; corner < corners.size(); ++corner) {
        expected += corners[corner] * weights[corner];
      }
      if (std::abs(expected - std::round(expected)) < .125L) continue;
      const auto actual = required(detail.sample(direction, 16));
      const auto truncated = required(legacy.sample(direction, 8));
      check(std::abs(static_cast<long double>(actual.macro_elevation_metres) -
                     expected) < 1e-9L,
            "continuous macro height disagrees with independent weighted "
            "source corners");
      check(std::abs(actual.macro_elevation_metres -
                     truncated.elevation_metres) > .1,
            "continuous macro field inherited legacy integer truncation");
      check(actual.color == truncated.color,
            "continuous physical interpolation changed retained legacy palette "
            "sampling");
      tested = true;
    }
  }
  check(tested,
        "bounded macro oracle found no nonconstant fractional source cell");
}

auto cache_conflict_and_seeds() -> void {
  const auto owner = required(generate_physical_origin_system(Seed{42}));
  auto cache = required(TerrainTileCache::create(1));
  const auto detail = sampler(owner, 2, cache);
  const auto& planet = detail.planet();
  const TerrainTileKey key{planet.id, CubeFace::positive_x, 8, 128, 128};
  // Valid legacy descriptor with the exact seed/id pair, but different
  // ownership metadata. Cache insertion must succeed before conflict refusal
  // can prove that the provider does not silently consume a namesake entry.
  const PlanetDescriptor counterfeit{planet.seed,
                                     planet.id,
                                     planet.display_name + " metadata conflict",
                                     planet.radius,
                                     planet.surface_gravity,
                                     planet.atmosphere_class,
                                     planet.atmosphere_pressure,
                                     planet.terrain_character,
                                     planet.water_coverage,
                                     planet.palette};
  const auto poisoned = required(cache.get(counterfeit, key));
  check(error_is(detail.sample({1, 0, 0}, 8), Error::invalid_macro_source),
        "cache descriptor conflict silently substituted another planet's macro "
        "source");
  check(cache.size() == 1 && cache.contains(key) &&
            required(cache.get(counterfeit, key)) == poisoned,
        "refused macro ownership changed the conflicting cache entry");
  const TerrainLocalDetailSample sentinel{-7, 9, 2, {1, 3, 5}, std::nullopt};
  std::vector output(kTerrainTileSampleCount + 1, sentinel);
  const auto before = output;
  check(error_is(detail.fill_tile(key, 65, output),
                 Error::invalid_macro_source) &&
            output == before,
        "macro conflict did not atomically refuse a full tile fill");

  const auto second_owner = required(generate_physical_origin_system(Seed{99}));
  auto second_cache = required(TerrainTileCache::create(2));
  auto clean_cache = required(TerrainTileCache::create(2));
  const auto second = sampler(second_owner, 2, second_cache);
  const auto clean = sampler(owner, 2, clean_cache);
  bool changed{};
  for (const auto direction :
       std::array{PlanetFixedDirection{1, 0, 0}, PlanetFixedDirection{0, 1, 0},
                  PlanetFixedDirection{-.31, .47, .73}}) {
    const auto a = required(clean.sample(direction, 8));
    const auto b = required(second.sample(direction, 8));
    changed = changed || a.local_offset_metres != b.local_offset_metres;
    check(clean_cache.size() <= 2 && second_cache.size() <= 2,
          "independent owners exceeded their bounded caches");
    check(required(clean.sample(direction, 16)) == a,
          "cache eviction or output LOD changed a regenerated sample");
  }
  check(clean.owner() != second.owner() && clean.body() != second.body() &&
            changed,
        "different seeded owners reused one local detail field");
  check(!TerrainLocalDetailSampler::create(owner, second.body(), {2},
                                           clean_cache),
        "body from a different authenticated owner accepted");
  const auto continuous_owner =
      required(generate_physical_origin_system(Seed{42}, 2));
  auto continuous_cache = required(TerrainTileCache::create(2));
  const auto continuous = sampler(continuous_owner, 2, continuous_cache);
  check(required(continuous.sample({.73, -.19, .41}, 8)) ==
            required(clean.sample({.73, -.19, .41}, 8)),
        "ephemeris selection changed unchanged physical terrain identity");
}

auto generated_recipe_domain() -> void {
  struct Fixture {
    PhysicalLocalSystem owner;
    BodyId body;
  };
  // Select complete, authenticated catalogs rather than fabricating a planet
  // descriptor inside an otherwise inconsistent physical owner. The bounded
  // search covers each terrain character and generated radii near both bounds;
  // it does not pretend those generated samples are the exact radius endpoints.
  std::array<std::optional<Fixture>, 7> selected;
  for (std::uint64_t seed = 0; seed < 256; ++seed) {
    const auto owner = required(generate_physical_local_system(Seed{seed}));
    for (const auto& entry : owner.catalog.planets) {
      const auto& planet = entry.descriptor;
      const auto character = static_cast<std::size_t>(planet.terrain_character);
      check(character < 5,
            "generated catalog has an unsupported terrain character");
      if (character >= 5) continue;
      if (!selected[character]) {
        selected[character].emplace(Fixture{owner, body_id(planet.id)});
      }
      if (planet.radius.value <= PlanetRadiusKm::min + 100 && !selected[5]) {
        selected[5].emplace(Fixture{owner, body_id(planet.id)});
      }
      if (planet.radius.value >= PlanetRadiusKm::max - 100 && !selected[6]) {
        selected[6].emplace(Fixture{owner, body_id(planet.id)});
      }
    }
    if (std::ranges::all_of(
            selected, [](const auto& value) { return value.has_value(); }))
      break;
  }
  check(std::ranges::all_of(
            selected, [](const auto& value) { return value.has_value(); }),
        "bounded generated corpus missed a terrain character or near-bound "
        "radius");
  for (const auto& fixture : selected) {
    if (!fixture) continue;
    auto cache = required(TerrainTileCache::create(8));
    const auto detail = required(TerrainLocalDetailSampler::create(
        fixture->owner, fixture->body, {2}, cache));
    const auto legacy = required(TerrainLocalDetailSampler::create(
        fixture->owner, fixture->body, {1}, cache));
    for (const auto direction : std::array{
             PlanetFixedDirection{1, 0, 0}, PlanetFixedDirection{-1, -1, -1},
             PlanetFixedDirection{-.31, .47, .73}}) {
      const auto value = required(detail.sample(direction, 8));
      check(std::isfinite(value.macro_elevation_metres) &&
                std::isfinite(value.local_offset_metres) &&
                std::isfinite(value.elevation_metres) &&
                value.tangent_slope_estimate,
            "valid terrain/radius domain produced unsafe height or missing "
            "slope");
      if (value.tangent_slope_estimate) {
        check(std::isfinite(value.tangent_slope_estimate->x) &&
                  std::isfinite(value.tangent_slope_estimate->y) &&
                  std::isfinite(value.tangent_slope_estimate->z),
              "valid terrain/radius domain produced nonfinite tangent slope");
      }
      check(
          required(detail.sample(direction, 16)) == value,
          "terrain character or near-bound radius breaks output LOD identity");
      check(!required(legacy.sample(direction, 8)).tangent_slope_estimate,
            "terrain character or near-bound radius gives recipe1 an invented "
            "slope");
    }
    check(cache.size() <= cache.capacity(),
          "generated domain sampling exceeded macro cache capacity");
  }
}

auto sampled_feature(bool report) -> void {
  struct OwnedFixture {
    PhysicalLocalSystem owner;
    BodyId body;
  };
  std::optional<OwnedFixture> selected;
  for (std::uint64_t seed = 0; seed < 32 && !selected; ++seed) {
    const auto owner = required(generate_physical_local_system(Seed{seed}));
    for (const auto& entry : owner.catalog.planets) {
      if (entry.descriptor.terrain_character != TerrainCharacter::alpine)
        continue;
      selected.emplace(OwnedFixture{owner, body_id(entry.descriptor.id)});
      break;
    }
  }
  check(selected.has_value(),
        "bounded feature corpus has no authenticated alpine body");
  if (!selected) return;
  auto cache = required(TerrainTileCache::create(12));
  const auto detail = required(TerrainLocalDetailSampler::create(
      selected->owner, selected->body, {2}, cache));
  const auto& planet = detail.planet();
  const double radius = static_cast<double>(planet.radius.value) * 1000;
  struct ProfilePoint {
    double arc_metres;
    PlanetFixedDirection direction;
    TerrainLocalDetailSample sample;
  };
  struct Extremum {
    std::size_t index;
    bool crest;
  };
  struct Feature {
    bool found{};
    std::size_t profile{};
    std::size_t intervals{};
    double width_metres{};
    double prominence_metres{};
    double crest_height{};
    double left_trough_height{};
    double right_trough_height{};
    double crest_local_offset{};
    double left_trough_local_offset{};
    double right_trough_local_offset{};
    PlanetFixedDirection crest_direction;
  } feature;
  constexpr std::size_t sample_count{513};
  constexpr double requested_spacing{32};
  constexpr std::array anchors{PlanetFixedDirection{.73, -.19, .41},
                               PlanetFixedDirection{1, 0, 0},
                               PlanetFixedDirection{-.31, .47, .73}};
  double minimum_spacing = std::numeric_limits<double>::infinity();
  double maximum_spacing{};
  for (std::size_t anchor_index = 0; anchor_index < anchors.size();
       ++anchor_index) {
    const auto normal = unit(anchors[anchor_index]);
    const auto geodetic = required(geodetic_from_planet_fixed(
        planet, {normal.x * radius, normal.y * radius, normal.z * radius}));
    const auto frame = required(make_local_tangent_frame(
        planet, {geodetic.latitude_radians, geodetic.longitude_radians, 0}));
    std::vector<ProfilePoint> profile;
    profile.reserve(sample_count);
    double arc{};
    PlanetFixedDirection previous;
    for (std::size_t index = 0; index < sample_count; ++index) {
      const double distance =
          (static_cast<double>(index) - 256) * requested_spacing;
      const double angle = distance / radius;
      const double radial = std::cos(angle);
      const double east = std::sin(angle);
      const auto direction = unit({frame.up.x * radial + frame.east.x * east,
                                   frame.up.y * radial + frame.east.y * east,
                                   frame.up.z * radial + frame.east.z * east});
      if (index != 0) {
        const PlanetFixedDirection cross{
            previous.y * direction.z - previous.z * direction.y,
            previous.z * direction.x - previous.x * direction.z,
            previous.x * direction.y - previous.y * direction.x};
        const double dot = previous.x * direction.x + previous.y * direction.y +
                           previous.z * direction.z;
        const double measured =
            radius * std::atan2(std::hypot(cross.x, cross.y, cross.z), dot);
        minimum_spacing = std::min(minimum_spacing, measured);
        maximum_spacing = std::max(maximum_spacing, measured);
        check(std::abs(measured - requested_spacing) < 1e-6,
              "physical great-circle samples disagree with requested spacing");
        arc += measured;
      }
      const auto value = required(detail.sample(direction, 14));
      check(std::isfinite(value.elevation_metres) &&
                std::isfinite(value.local_offset_metres),
            "real owned feature profile contains nonfinite height");
      profile.push_back({arc, direction, value});
      previous = direction;
    }
    std::vector<Extremum> extrema;
    for (std::size_t index = 1; index + 1 < profile.size(); ++index) {
      const double before = profile[index - 1].sample.elevation_metres;
      const double current = profile[index].sample.elevation_metres;
      const double after = profile[index + 1].sample.elevation_metres;
      if (current > before && current >= after)
        extrema.push_back({index, true});
      if (current < before && current <= after)
        extrema.push_back({index, false});
    }
    for (std::size_t index = 1; index + 1 < extrema.size(); ++index) {
      if (!extrema[index].crest || extrema[index - 1].crest ||
          extrema[index + 1].crest)
        continue;
      const auto& left = profile[extrema[index - 1].index];
      const auto& crest = profile[extrema[index].index];
      const auto& right = profile[extrema[index + 1].index];
      const double prominence =
          crest.sample.elevation_metres -
          std::max(left.sample.elevation_metres, right.sample.elevation_metres);
      if (prominence <= feature.prominence_metres) continue;
      feature = {true,
                 anchor_index,
                 extrema[index + 1].index - extrema[index - 1].index,
                 right.arc_metres - left.arc_metres,
                 prominence,
                 crest.sample.elevation_metres,
                 left.sample.elevation_metres,
                 right.sample.elevation_metres,
                 crest.sample.local_offset_metres,
                 left.sample.local_offset_metres,
                 right.sample.local_offset_metres,
                 crest.direction};
    }
  }
  check(feature.found,
        "bounded actual height profiles have no crest bracketed by troughs");
  if (!feature.found) return;
  // A sampled physical crest, not a noise-cell wavelength or analytic promise.
  // Four intervals give at least five explicit samples across its width.
  check(feature.intervals >= 4 &&
            feature.width_metres / maximum_spacing > 3.999999,
        "selected physical feature is not resolved by at least four sample "
        "intervals");
  check(cache.size() <= cache.capacity(),
        "feature profile exceeded macro cache bound");
  if (!report) return;

  const auto address = required(terrain_address_from_planet_direction(
      planet, feature.crest_direction, 14));
  std::array<TerrainLocalDetailSample, kTerrainTileSampleCount> output;
  const auto start = std::clock();
  const auto filled = detail.fill_tile(address.tile, 65, output);
  const auto finish = std::clock();
  check(filled.has_value(), "measured tile fill refused");
  check(cache.size() <= cache.capacity(),
        "measured tile fill exceeded macro cache bound");
  std::cout << std::setprecision(17) << "Measurement: recipe=2 system_seed="
            << selected->owner.catalog.seed.value
            << " body=" << selected->body.value
            << " terrain=alpine radius_metres=" << radius << '\n'
            << "Feature: profile=" << feature.profile
            << " profiles=" << anchors.size()
            << " samples_per_profile=" << sample_count
            << " width_definition=adjacent_trough_to_trough_on_total_height"
            << " width_metres=" << feature.width_metres
            << " crest_metres=" << feature.crest_height
            << " left_trough_metres=" << feature.left_trough_height
            << " right_trough_metres=" << feature.right_trough_height
            << " prominence_metres=" << feature.prominence_metres << '\n'
            << "Local contribution: crest_metres=" << feature.crest_local_offset
            << " left_trough_metres=" << feature.left_trough_local_offset
            << " right_trough_metres=" << feature.right_trough_local_offset
            << '\n'
            << "Sampling: measured_min_spacing_metres=" << minimum_spacing
            << " measured_max_spacing_metres=" << maximum_spacing
            << " feature_intervals=" << feature.intervals
            << " feature_sample_count=" << feature.intervals + 1
            << " four_interval_target=checked "
               "projected_pixel_target=unqualified_issue212\n"
            << "Storage: sample_bytes=" << sizeof(TerrainLocalDetailSample)
            << " fixed_staging_bytes=" << sizeof(output)
            << " caller_output_bytes=" << sizeof(output)
            << " macro_cache_entries=" << cache.size()
            << " macro_cache_capacity=" << cache.capacity() << '\n';
  if (start != static_cast<std::clock_t>(-1) &&
      finish != static_cast<std::clock_t>(-1)) {
    std::cout << "Tile: cpu_seconds="
              << static_cast<double>(finish - start) / CLOCKS_PER_SEC;
  } else {
    std::cout << "Tile: cpu_seconds=unavailable";
  }
  std::cout << " output_lod=14 samples=4225 cache=reused_profile_cache"
               " timing_is_not_a_pass_threshold_or_fps\n";
}

auto frozen_goldens(bool print) -> void {
  // Recipe2 vectors frozen from matching GCC/Clang provider runs.
  constexpr bool kGoldensFrozen = true;
  constexpr std::array<std::array<std::uint64_t, 7>, 4> expected{{
      {4634696597773484032ULL, 4616668784923819803ULL, 4635008020679511026ULL,
       4567764447463612259ULL, 4570482903184918055ULL, 13790230332411286134ULL,
       4634696597773484032ULL},
      {4651461951073812480ULL, 4615381738643715218ULL, 4651493979666951925ULL,
       4567935636718179250ULL, 13783578233400955826ULL, 13793826326076461370ULL,
       4651461951073812480ULL},
      {4644794512562978816ULL, 4604909297937852165ULL, 4644807665059839316ULL,
       13796769291369268304ULL, 4567817992156895784ULL, 4576124407490808418ULL,
       4644794512562978816ULL},
      {4652874707766579309ULL, 13839353858409789466ULL, 4652857521433073457ULL,
       13796079559381366137ULL, 4577768931263352902ULL, 4579611905775560984ULL,
       4652869325957365760ULL},
  }};
  struct GoldenFixture {
    Seed seed;
    PlanetFixedDirection direction;
  };
  constexpr std::array fixtures{
      GoldenFixture{Seed{0}, {.72, -.18, .54}},
      GoldenFixture{Seed{42}, {.72, -.18, .54}},
      GoldenFixture{Seed{std::numeric_limits<std::uint64_t>::max()},
                    {.72, -.18, .54}},
      GoldenFixture{Seed{42}, {.73, -.19, .41}}};
  for (std::size_t index = 0; index < fixtures.size(); ++index) {
    const auto owner =
        required(generate_physical_origin_system(fixtures[index].seed));
    auto cache = required(TerrainTileCache::create(8));
    const auto detail = sampler(owner, 2, cache);
    const auto legacy = sampler(owner, 1, cache);
    const auto value = required(detail.sample(fixtures[index].direction, 8));
    const auto old = required(legacy.sample(fixtures[index].direction, 8));
    check(value.tangent_slope_estimate.has_value(),
          "golden fixture has no slope");
    if (!value.tangent_slope_estimate) continue;
    const auto slope = *value.tangent_slope_estimate;
    const std::array observed{
        std::bit_cast<std::uint64_t>(value.macro_elevation_metres),
        std::bit_cast<std::uint64_t>(value.local_offset_metres),
        std::bit_cast<std::uint64_t>(value.elevation_metres),
        std::bit_cast<std::uint64_t>(slope.x),
        std::bit_cast<std::uint64_t>(slope.y),
        std::bit_cast<std::uint64_t>(slope.z),
        std::bit_cast<std::uint64_t>(old.elevation_metres)};
    if (print) {
      std::cout << "Golden " << index << ": {";
      for (std::size_t column = 0; column < observed.size(); ++column) {
        if (column != 0) std::cout << ", ";
        std::cout << observed[column] << "ULL";
      }
      std::cout << "}\n";
    } else {
      check(kGoldensFrozen && observed == expected[index],
            "paired height/slope binary64 goldens are unfrozen or changed");
    }
  }
}
auto direct_samples_and_height() -> void {
  for (const auto seed :
       std::array{Seed{0}, Seed{42},
                  Seed{std::numeric_limits<std::uint64_t>::max()}}) {
    const auto planet = generate_planet_descriptor(seed);
    for (unsigned face = 0; face < 6; ++face) {
      for (const auto lod : std::array<std::uint8_t, 3>{0, 8, 16}) {
        const auto last = (std::uint32_t{1} << lod) - 1;
        const TerrainTileKey key{planet.id, static_cast<CubeFace>(face), lod,
                                 last, last / 2};
        const auto tile = required(generate_terrain_tile(planet, key));
        for (const auto coordinate : std::array<std::array<std::size_t, 2>, 6>{
                 {{0, 0}, {64, 0}, {0, 64}, {64, 64}, {17, 43}, {32, 32}}}) {
          const auto actual = required(generate_terrain_sample(
              planet, key, coordinate[0], coordinate[1]));
          check(
              actual ==
                  required(tile.sample_at(coordinate[0], coordinate[1])).get(),
              "direct source sample differs from frozen tile arithmetic");
        }
        check(!generate_terrain_sample(planet, key, 65, 0) &&
                  !generate_terrain_sample(
                      planet, key, 0, std::numeric_limits<std::size_t>::max()),
              "direct source sample accepted an out-of-bounds coordinate");
      }
    }
  }
  const auto owner = required(generate_physical_origin_system(Seed{42}, 2));
  auto cache = required(TerrainTileCache::create(2));
  for (const auto version : {1U, 2U}) {
    const auto selected = sampler(owner, version, cache);
    for (const auto direction : std::array{
             PlanetFixedDirection{1, 0, 0}, PlanetFixedDirection{1, 1, 1},
             PlanetFixedDirection{-1, -.999999, .1},
             PlanetFixedDirection{0, 0, -1},
             PlanetFixedDirection{.73, -.19, .41}}) {
      for (const auto lod : std::array<std::uint8_t, 3>{0, 8, 16}) {
        auto expected = required(selected.sample(direction, lod));
        expected.tangent_slope_estimate.reset();
        const auto before = cache.size();
        const auto actual = required(selected.sample_height(direction, lod));
        check(
            actual == expected,
            "height-only direct path changed the selected scalar field/color");
        if (version == 2)
          check(cache.size() == before,
                "direct local-detail height constructed whole source tiles");
      }
    }
  }
}
} // namespace

auto main(int argc, char** argv) -> int {
  bool print{};
  bool measure{};
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--print-goldens" && !print)
      print = true;
    else if (argument == "--measure" && !measure)
      measure = true;
    else
      return EXIT_FAILURE;
  }
  check(kLegacyPhysicalTerrainRecipe == 1 && kLocalDetailTerrainRecipe == 2 &&
            kLocalDetailMacroSourceLod == 8 &&
            kLocalDetailSlopeStepMetres == 32,
        "explicit physical terrain recipe constants changed");
  invalid_inputs();
  direct_samples_and_height();
  legacy_and_ownership();
  tile_identity();
  slope_reconstruction();
  continuous_macro_oracle();
  cache_conflict_and_seeds();
  generated_recipe_domain();
  sampled_feature(measure);
  frozen_goldens(print);
  std::cout << "Terrain local-detail contracts: " << failures << " failures\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
