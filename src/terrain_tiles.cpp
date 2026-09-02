#include "apsis_drift/terrain_tiles.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <tuple>
#include <type_traits>

namespace apsis_drift {
namespace {

inline constexpr std::int64_t kFixedOne{65'536};
inline constexpr std::int64_t kCanonicalFaceExtent{
    static_cast<std::int64_t>(kTerrainTileIntervalsPerAxis) << kMaxTerrainLod};

static_assert(kCanonicalFaceExtent == 4'194'304);

struct CubeLatticePosition {
  std::int64_t x{};
  std::int64_t y{};
  std::int64_t z{};
};

struct TerrainParameters {
  std::int32_t amplitude_metres{};
  std::array<std::int32_t, 4> octave_weights{};
};

[[nodiscard]] auto valid_face(CubeFace face) noexcept -> bool {
  switch (face) {
    case CubeFace::positive_x:
    case CubeFace::negative_x:
    case CubeFace::positive_y:
    case CubeFace::negative_y:
    case CubeFace::positive_z:
    case CubeFace::negative_z: return true;
  }
  return false;
}

[[nodiscard]] auto valid_atmosphere(const PlanetDescriptor& planet) noexcept
    -> bool {
  const auto pressure = planet.atmosphere_pressure.value;
  switch (planet.atmosphere_class) {
    case AtmosphereClass::airless: return pressure == 0;
    case AtmosphereClass::tenuous: return pressure >= 1 && pressure <= 249;
    case AtmosphereClass::temperate:
      return pressure >= 250 && pressure <= 1'499;
    case AtmosphereClass::dense:
      return pressure >= 1'500 && pressure <= AtmospherePressureMillibars::max;
  }
  return false;
}

[[nodiscard]] auto valid_terrain(TerrainCharacter terrain) noexcept -> bool {
  switch (terrain) {
    case TerrainCharacter::oceanic:
    case TerrainCharacter::plains:
    case TerrainCharacter::rugged:
    case TerrainCharacter::alpine:
    case TerrainCharacter::volcanic: return true;
  }
  return false;
}

[[nodiscard]] auto valid_palette(PaletteFamily palette) noexcept -> bool {
  switch (palette) {
    case PaletteFamily::verdant:
    case PaletteFamily::arid:
    case PaletteFamily::glacial:
    case PaletteFamily::volcanic:
    case PaletteFamily::alien: return true;
  }
  return false;
}

[[nodiscard]] auto valid_planet(const PlanetDescriptor& planet) noexcept
    -> bool {
  return planet.id.value == planet.seed.value &&
         planet.radius.value >= PlanetRadiusKm::min &&
         planet.radius.value <= PlanetRadiusKm::max &&
         planet.surface_gravity.value >= SurfaceGravityMilliG::min &&
         planet.surface_gravity.value <= SurfaceGravityMilliG::max &&
         planet.water_coverage.value <= WaterCoverageBasisPoints::max &&
         valid_atmosphere(planet) && valid_terrain(planet.terrain_character) &&
         valid_palette(planet.palette.family);
}

[[nodiscard]] auto validate_key(const PlanetDescriptor& planet,
                                TerrainTileKey key) noexcept
    -> std::expected<void, TerrainTileError> {
  if (!valid_planet(planet)) {
    return std::unexpected{TerrainTileError::invalid_planet};
  }
  if (key.planet != planet.id) {
    return std::unexpected{TerrainTileError::wrong_planet};
  }
  if (!valid_face(key.face)) {
    return std::unexpected{TerrainTileError::invalid_cube_face};
  }
  if (key.lod > kMaxTerrainLod) {
    return std::unexpected{TerrainTileError::invalid_lod};
  }
  const auto tiles_per_axis = std::uint32_t{1} << key.lod;
  if (key.x >= tiles_per_axis || key.y >= tiles_per_axis) {
    return std::unexpected{TerrainTileError::invalid_tile_index};
  }
  return {};
}

[[nodiscard]] auto canonical_axis(std::uint32_t tile_index, std::uint8_t lod,
                                  std::size_t sample_index) noexcept
    -> std::int64_t {
  const auto global_index =
      static_cast<std::uint64_t>(tile_index) *
          static_cast<std::uint64_t>(kTerrainTileIntervalsPerAxis) +
      static_cast<std::uint64_t>(sample_index);
  const auto lod_scale = std::uint64_t{1} << (kMaxTerrainLod - lod);
  const auto offset =
      static_cast<std::int64_t>(std::uint64_t{2} * global_index * lod_scale);
  return -kCanonicalFaceExtent + offset;
}

[[nodiscard]] auto cube_position(TerrainTileKey key, std::size_t sample_x,
                                 std::size_t sample_y) noexcept
    -> CubeLatticePosition {
  const auto u = canonical_axis(key.x, key.lod, sample_x);
  const auto v = canonical_axis(key.y, key.lod, sample_y);
  switch (key.face) {
    case CubeFace::positive_x: return {kCanonicalFaceExtent, u, v};
    case CubeFace::negative_x: return {-kCanonicalFaceExtent, -u, v};
    case CubeFace::positive_y: return {-u, kCanonicalFaceExtent, v};
    case CubeFace::negative_y: return {u, -kCanonicalFaceExtent, v};
    case CubeFace::positive_z: return {-v, u, kCanonicalFaceExtent};
    case CubeFace::negative_z: return {v, u, -kCanonicalFaceExtent};
  }
  return {};
}

[[nodiscard]] auto mix64(std::uint64_t value) noexcept -> std::uint64_t {
  value ^= value >> 30U;
  value *= 0xBF58476D1CE4E5B9ULL;
  value ^= value >> 27U;
  value *= 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

[[nodiscard]] auto lattice_value(std::uint64_t seed, std::int64_t x,
                                 std::int64_t y, std::int64_t z) noexcept
    -> std::int64_t {
  auto value = seed;
  value ^= mix64(static_cast<std::uint64_t>(x) + 0x9E3779B97F4A7C15ULL);
  value ^= std::rotl(
      mix64(static_cast<std::uint64_t>(y) + 0xD1B54A32D192ED03ULL), 21);
  value ^= std::rotl(
      mix64(static_cast<std::uint64_t>(z) + 0x94D049BB133111EBULL), 42);
  return static_cast<std::int64_t>(mix64(value) >> 48U) - 32'768;
}

struct LatticeAxis {
  std::int64_t cell{};
  std::int64_t fraction{};
};

[[nodiscard]] auto lattice_axis(std::int64_t coordinate,
                                std::int64_t cell_size) noexcept
    -> LatticeAxis {
  auto cell = coordinate / cell_size;
  auto remainder = coordinate % cell_size;
  if (remainder < 0) {
    --cell;
    remainder += cell_size;
  }
  return {cell, remainder * kFixedOne / cell_size};
}

[[nodiscard]] auto smooth_fraction(std::int64_t value) noexcept
    -> std::int64_t {
  const auto square = static_cast<std::uint64_t>(value * value);
  const auto factor = static_cast<std::uint64_t>(3 * kFixedOne - 2 * value);
  const auto denominator = static_cast<std::uint64_t>(kFixedOne * kFixedOne);
  return static_cast<std::int64_t>(square * factor / denominator);
}

[[nodiscard]] auto interpolate(std::int64_t from, std::int64_t to,
                               std::int64_t fraction) noexcept -> std::int64_t {
  return from + (to - from) * fraction / kFixedOne;
}

[[nodiscard]] auto value_noise(CubeLatticePosition position,
                               std::int64_t cell_size,
                               std::uint64_t seed) noexcept -> std::int64_t {
  const auto x = lattice_axis(position.x, cell_size);
  const auto y = lattice_axis(position.y, cell_size);
  const auto z = lattice_axis(position.z, cell_size);
  const auto tx = smooth_fraction(x.fraction);
  const auto ty = smooth_fraction(y.fraction);
  const auto tz = smooth_fraction(z.fraction);

  const auto x00 =
      interpolate(lattice_value(seed, x.cell, y.cell, z.cell),
                  lattice_value(seed, x.cell + 1, y.cell, z.cell), tx);
  const auto x10 =
      interpolate(lattice_value(seed, x.cell, y.cell + 1, z.cell),
                  lattice_value(seed, x.cell + 1, y.cell + 1, z.cell), tx);
  const auto x01 =
      interpolate(lattice_value(seed, x.cell, y.cell, z.cell + 1),
                  lattice_value(seed, x.cell + 1, y.cell, z.cell + 1), tx);
  const auto x11 =
      interpolate(lattice_value(seed, x.cell, y.cell + 1, z.cell + 1),
                  lattice_value(seed, x.cell + 1, y.cell + 1, z.cell + 1), tx);
  return interpolate(interpolate(x00, x10, ty), interpolate(x01, x11, ty), tz);
}

[[nodiscard]] auto terrain_parameters(TerrainCharacter character) noexcept
    -> TerrainParameters {
  switch (character) {
    case TerrainCharacter::oceanic: return {2'400, {12, 4, 2, 1}};
    case TerrainCharacter::plains: return {1'800, {13, 4, 1, 1}};
    case TerrainCharacter::rugged: return {5'500, {9, 6, 4, 2}};
    case TerrainCharacter::alpine: return {8'500, {7, 7, 5, 3}};
    case TerrainCharacter::volcanic: return {7'000, {8, 5, 4, 4}};
  }
  return {};
}

[[nodiscard]] auto terrain_field(CubeLatticePosition position, Seed shape,
                                 Seed detail,
                                 const TerrainParameters& parameters) noexcept
    -> std::int64_t {
  const std::array values{
      value_noise(position, std::int64_t{1} << 22U, shape.value),
      value_noise(position, std::int64_t{1} << 19U,
                  shape.value ^ 0xA0761D6478BD642FULL),
      value_noise(position, std::int64_t{1} << 16U, detail.value),
      value_noise(position, std::int64_t{1} << 13U,
                  detail.value ^ 0xE7037ED1A0B428DBULL),
  };
  std::int64_t weighted{};
  std::int64_t weight_total{};
  for (std::size_t index = 0; index < values.size(); ++index) {
    weighted += values[index] * parameters.octave_weights[index];
    weight_total += parameters.octave_weights[index];
  }
  return weighted / weight_total;
}

[[nodiscard]] auto elevation_metres(const PlanetDescriptor& planet,
                                    const TerrainParameters& parameters,
                                    std::int64_t field) noexcept
    -> std::int32_t {
  constexpr std::int64_t field_width{65'535};
  const auto sea_threshold =
      -32'768 +
      (static_cast<std::int64_t>(planet.water_coverage.value) * field_width +
       5'000) /
          10'000;
  const auto amplitude = static_cast<std::int64_t>(parameters.amplitude_metres);
  auto elevation = (field - sea_threshold) * amplitude / 32'768;
  if (planet.water_coverage.value == WaterCoverageBasisPoints::min) {
    elevation = std::max<std::int64_t>(1, elevation);
  } else if (planet.water_coverage.value == WaterCoverageBasisPoints::max) {
    elevation = std::min<std::int64_t>(-1, elevation);
  }
  return static_cast<std::int32_t>(elevation);
}

[[nodiscard]] auto blend(Rgb8 from, Rgb8 to, std::int64_t fraction) noexcept
    -> Rgb8 {
  const auto channel = [fraction](std::uint8_t first, std::uint8_t second) {
    const auto result =
        static_cast<std::int64_t>(first) +
        (static_cast<std::int64_t>(second) - first) * fraction / kFixedOne;
    return static_cast<std::uint8_t>(std::clamp<std::int64_t>(result, 0, 255));
  };
  return {channel(from.red, to.red), channel(from.green, to.green),
          channel(from.blue, to.blue)};
}

[[nodiscard]] auto sample_color(const PlanetDescriptor& planet,
                                const TerrainParameters& parameters,
                                std::int32_t elevation) noexcept -> Rgb8 {
  const auto amplitude = static_cast<std::int64_t>(parameters.amplitude_metres);
  if (elevation <= 0) {
    const auto depth =
        std::min(kFixedOne,
                 -static_cast<std::int64_t>(elevation) * kFixedOne / amplitude);
    return blend(planet.palette.shallow_water, planet.palette.deep_water,
                 depth);
  }

  const auto land = std::min(kFixedOne, static_cast<std::int64_t>(elevation) *
                                            kFixedOne / amplitude);
  constexpr std::int64_t highland_start{kFixedOne * 3 / 5};
  if (land <= highland_start) {
    return blend(planet.palette.lowland, planet.palette.highland,
                 land * kFixedOne / highland_start);
  }
  return blend(planet.palette.highland, planet.palette.peak,
               (land - highland_start) * kFixedOne /
                   (kFixedOne - highland_start));
}

auto hash_byte(std::uint64_t& hash, std::uint8_t byte) noexcept -> void {
  hash ^= byte;
  hash *= 1099511628211ULL;
}

template <typename Integer>
auto hash_integer(std::uint64_t& hash, Integer value) noexcept -> void {
  using Unsigned = std::make_unsigned_t<Integer>;
  auto remaining = static_cast<Unsigned>(value);
  for (std::size_t byte = 0; byte < sizeof(Unsigned); ++byte) {
    hash_byte(hash, static_cast<std::uint8_t>(remaining & 0xFFU));
    if constexpr (sizeof(Unsigned) > 1) remaining >>= 8U;
  }
}

} // namespace

auto derive_terrain_generation_seed(Seed planet_seed,
                                    TerrainGenerationStream stream) noexcept
    -> Seed {
  const auto terrain =
      derive_planet_stream_seed(planet_seed, PlanetDescriptorStream::terrain);
  return derive_seed(terrain, SeedDomain::terrain,
                     static_cast<std::uint64_t>(stream));
}

auto generate_terrain_tile(const PlanetDescriptor& planet, TerrainTileKey key)
    -> std::expected<TerrainTile, TerrainTileError> {
  const auto valid = validate_key(planet, key);
  if (!valid) return std::unexpected{valid.error()};

  const auto shape = derive_terrain_generation_seed(
      planet.seed, TerrainGenerationStream::shape);
  const auto detail = derive_terrain_generation_seed(
      planet.seed, TerrainGenerationStream::detail);
  const auto parameters = terrain_parameters(planet.terrain_character);
  std::array<TerrainSample, kTerrainTileSampleCount> samples;
  for (std::size_t y = 0; y < kTerrainTileSamplesPerAxis; ++y) {
    for (std::size_t x = 0; x < kTerrainTileSamplesPerAxis; ++x) {
      const auto field =
          terrain_field(cube_position(key, x, y), shape, detail, parameters);
      const auto elevation = elevation_metres(planet, parameters, field);
      samples[y * kTerrainTileSamplesPerAxis + x] =
          TerrainSample{elevation, sample_color(planet, parameters, elevation)};
    }
  }
  return TerrainTile{key, std::move(samples)};
}

auto TerrainTile::sample_at(std::size_t x, std::size_t y) const noexcept
    -> std::expected<std::reference_wrapper<const TerrainSample>,
                     TerrainTileError> {
  if (x >= kTerrainTileSamplesPerAxis || y >= kTerrainTileSamplesPerAxis) {
    return std::unexpected{TerrainTileError::invalid_sample_coordinate};
  }
  return std::cref(m_samples[y * kTerrainTileSamplesPerAxis + x]);
}

auto TerrainTile::checksum() const noexcept -> std::uint64_t {
  auto hash = std::uint64_t{14695981039346656037ULL};
  hash_integer(hash, kTerrainTileGeneratorVersion);
  hash_integer(hash, m_key.planet.value);
  hash_integer(hash, static_cast<std::uint8_t>(m_key.face));
  hash_integer(hash, m_key.lod);
  hash_integer(hash, m_key.x);
  hash_integer(hash, m_key.y);
  hash_integer(hash, static_cast<std::uint32_t>(kTerrainTileSamplesPerAxis));
  for (const auto& sample : m_samples) {
    hash_integer(hash, sample.elevation_metres);
    hash_byte(hash, sample.color.red);
    hash_byte(hash, sample.color.green);
    hash_byte(hash, sample.color.blue);
  }
  return hash;
}

auto TerrainTileCache::create(std::size_t capacity)
    -> std::expected<TerrainTileCache, TerrainTileError> {
  if (capacity == 0) {
    return std::unexpected{TerrainTileError::invalid_cache_capacity};
  }
  return TerrainTileCache{capacity};
}

auto TerrainTileCache::get(const PlanetDescriptor& planet, TerrainTileKey key)
    -> std::expected<std::shared_ptr<const TerrainTile>, TerrainTileError> {
  const auto valid = validate_key(planet, key);
  if (!valid) return std::unexpected{valid.error()};

  const auto found =
      std::find_if(m_entries.begin(), m_entries.end(),
                   [key](const Entry& entry) { return entry.key == key; });
  if (found != m_entries.end()) {
    if (found->planet != planet) {
      return std::unexpected{TerrainTileError::invalid_planet};
    }
    const auto tile = found->tile;
    m_entries.splice(m_entries.begin(), m_entries, found);
    return tile;
  }

  auto generated = generate_terrain_tile(planet, key);
  if (!generated) return std::unexpected{generated.error()};
  std::shared_ptr<const TerrainTile> tile =
      std::make_shared<TerrainTile>(std::move(*generated));
  m_entries.push_front({planet, key, tile});
  if (m_entries.size() > m_capacity) m_entries.pop_back();
  return tile;
}

auto TerrainTileCache::contains(TerrainTileKey key) const noexcept -> bool {
  return std::ranges::any_of(
      m_entries, [key](const Entry& entry) { return entry.key == key; });
}

[[nodiscard]] auto sample_tile_address(const TerrainTile& tile,
                                       const TerrainTileAddress& address)
    -> std::expected<TerrainSurfaceSample, TerrainTileError> {
  const auto interpolate_axis = [](double coordinate) {
    const double grid = std::clamp(coordinate, 0.0, 1.0) *
                        static_cast<double>(kTerrainTileIntervalsPerAxis);
    const auto lower = static_cast<std::size_t>(std::floor(grid));
    const auto upper = std::min(lower + 1, kTerrainTileIntervalsPerAxis);
    const auto fraction = static_cast<std::int64_t>(std::llround(
        (grid - static_cast<double>(lower)) * static_cast<double>(kFixedOne)));
    return std::tuple{lower, upper,
                      std::clamp(fraction, std::int64_t{0}, kFixedOne)};
  };
  const auto [x0, x1, tx] = interpolate_axis(address.u);
  const auto [y0, y1, ty] = interpolate_axis(address.v);
  const auto s00 = tile.sample_at(x0, y0);
  const auto s10 = tile.sample_at(x1, y0);
  const auto s01 = tile.sample_at(x0, y1);
  const auto s11 = tile.sample_at(x1, y1);
  if (!s00 || !s10 || !s01 || !s11) {
    return std::unexpected{TerrainTileError::invalid_sample_coordinate};
  }

  const auto lerp = [](std::int64_t from, std::int64_t to,
                       std::int64_t fraction) {
    return from + (to - from) * fraction / kFixedOne;
  };
  const auto bilerp = [&](std::int64_t a, std::int64_t b, std::int64_t c,
                          std::int64_t d) {
    return lerp(lerp(a, b, tx), lerp(c, d, tx), ty);
  };
  const auto elevation =
      bilerp(s00->get().elevation_metres, s10->get().elevation_metres,
             s01->get().elevation_metres, s11->get().elevation_metres);
  const auto channel = [&](auto member) {
    return static_cast<std::uint8_t>(std::clamp<std::int64_t>(
        bilerp(s00->get().color.*member, s10->get().color.*member,
               s01->get().color.*member, s11->get().color.*member),
        0, 255));
  };
  return TerrainSurfaceSample{
      .address = address,
      .elevation_metres = static_cast<double>(elevation),
      .color = {channel(&Rgb8::red), channel(&Rgb8::green),
                channel(&Rgb8::blue)},
  };
}

auto TerrainSurfaceSampler::create(const PlanetDescriptor& planet,
                                   std::uint8_t lod, TerrainTileCache& cache)
    -> std::expected<TerrainSurfaceSampler, TerrainTileError> {
  const TerrainTileKey validation_key{planet.id, CubeFace::positive_x, lod, 0,
                                      0};
  const auto valid = validate_key(planet, validation_key);
  if (!valid) return std::unexpected{valid.error()};
  return TerrainSurfaceSampler{planet, lod, cache};
}

auto TerrainSurfaceSampler::sample(PlanetFixedPositionMetres position)
    -> std::expected<TerrainSurfaceSample, TerrainTileError> {
  const auto address =
      terrain_address_from_planet_fixed(m_planet, position, m_lod);
  if (!address) {
    return std::unexpected{TerrainTileError::coordinate_failure};
  }
  return sample_address(*address);
}

auto TerrainSurfaceSampler::sample_direction(PlanetFixedDirection direction)
    -> std::expected<TerrainSurfaceSample, TerrainTileError> {
  const auto address =
      terrain_address_from_planet_direction(m_planet, direction, m_lod);
  if (!address) {
    return std::unexpected{TerrainTileError::coordinate_failure};
  }
  return sample_address(*address);
}

auto TerrainSurfaceSampler::sample_address(const TerrainTileAddress& address)
    -> std::expected<TerrainSurfaceSample, TerrainTileError> {
  const TerrainTile* tile{};
  if (m_last_tile != nullptr && address.tile == m_last_key) {
    tile = m_last_tile;
  } else {
    const auto found =
        std::ranges::find(m_tiles, address.tile, &PinnedTile::key);
    if (found != m_tiles.end()) {
      tile = found->tile.get();
    } else {
      const auto acquired = m_cache->get(m_planet, address.tile);
      if (!acquired) return std::unexpected{acquired.error()};
      m_tiles.push_back({address.tile, *acquired});
      tile = acquired->get();
    }
    m_last_key = address.tile;
    m_last_tile = tile;
  }
  return sample_tile_address(*tile, address);
}

auto sample_planet_surface(const PlanetDescriptor& planet,
                           PlanetFixedPositionMetres position, std::uint8_t lod,
                           TerrainTileCache& cache)
    -> std::expected<TerrainSurfaceSample, TerrainTileError> {
  const auto address = terrain_address_from_planet_fixed(planet, position, lod);
  if (!address) {
    return std::unexpected{TerrainTileError::coordinate_failure};
  }
  const auto tile = cache.get(planet, address->tile);
  if (!tile) return std::unexpected{tile.error()};
  return sample_tile_address(**tile, *address);
}

} // namespace apsis_drift
