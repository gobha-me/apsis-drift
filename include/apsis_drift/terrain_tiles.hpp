#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <list>
#include <memory>
#include <utility>

#include "apsis_drift/coordinates.hpp"
#include "apsis_drift/planet.hpp"

namespace apsis_drift {

// Terrain generation is generated-world compatibility data. The version,
// stream ordinals, sample layout, and integer generator must not change in
// place once saves can refer to generated terrain.
inline constexpr std::uint32_t kTerrainTileGeneratorVersion{1};
inline constexpr std::size_t kTerrainTileIntervalsPerAxis{64};
inline constexpr std::size_t kTerrainTileSamplesPerAxis{
    kTerrainTileIntervalsPerAxis + 1};
inline constexpr std::size_t kTerrainTileSampleCount{
    kTerrainTileSamplesPerAxis * kTerrainTileSamplesPerAxis};
inline constexpr std::size_t kDefaultTerrainTileCacheCapacity{64};

enum class TerrainGenerationStream : std::uint64_t {
  shape = 1,
  detail = 2,
};

struct TerrainSample {
  // Signed elevation relative to the descriptor's spherical reference
  // surface. Zero is the generated sea level.
  std::int32_t elevation_metres{};
  Rgb8 color;

  friend auto operator==(const TerrainSample&, const TerrainSample&)
      -> bool = default;
};

struct TerrainSurfaceSample {
  TerrainTileAddress address;
  double elevation_metres{};
  Rgb8 color;

  friend auto operator==(const TerrainSurfaceSample&,
                         const TerrainSurfaceSample&) -> bool = default;
};

enum class TerrainTileError : std::uint8_t {
  invalid_planet,
  wrong_planet,
  invalid_cube_face,
  invalid_lod,
  invalid_tile_index,
  invalid_sample_coordinate,
  invalid_cache_capacity,
  coordinate_failure,
};

class TerrainTile {
 public:
  TerrainTile(const TerrainTile&) = default;
  TerrainTile(TerrainTile&&) noexcept = default;
  auto operator=(const TerrainTile&) -> TerrainTile& = default;
  auto operator=(TerrainTile&&) noexcept -> TerrainTile& = default;

  [[nodiscard]] auto key() const noexcept -> const TerrainTileKey& {
    return m_key;
  }

  [[nodiscard]] auto sample_at(std::size_t x, std::size_t y) const noexcept
      -> std::expected<std::reference_wrapper<const TerrainSample>,
                       TerrainTileError>;

  [[nodiscard]] auto samples() const noexcept
      -> const std::array<TerrainSample, kTerrainTileSampleCount>& {
    return m_samples;
  }

  [[nodiscard]] auto checksum() const noexcept -> std::uint64_t;

 private:
  friend auto generate_terrain_tile(const PlanetDescriptor&, TerrainTileKey)
      -> std::expected<TerrainTile, TerrainTileError>;

  TerrainTile(TerrainTileKey key,
              std::array<TerrainSample, kTerrainTileSampleCount> samples)
      : m_key{key}, m_samples{std::move(samples)} {}

  TerrainTileKey m_key;
  std::array<TerrainSample, kTerrainTileSampleCount> m_samples;
};

[[nodiscard]] auto derive_terrain_generation_seed(
    Seed planet_seed, TerrainGenerationStream stream) noexcept -> Seed;

[[nodiscard]] auto generate_terrain_tile(const PlanetDescriptor& planet,
                                         TerrainTileKey key)
    -> std::expected<TerrainTile, TerrainTileError>;

class TerrainTileCache {
 public:
  [[nodiscard]] static auto create(
      std::size_t capacity = kDefaultTerrainTileCacheCapacity)
      -> std::expected<TerrainTileCache, TerrainTileError>;

  TerrainTileCache(const TerrainTileCache&) = delete;
  auto operator=(const TerrainTileCache&) -> TerrainTileCache& = delete;
  TerrainTileCache(TerrainTileCache&&) noexcept = default;
  auto operator=(TerrainTileCache&&) noexcept -> TerrainTileCache& = default;

  [[nodiscard]] auto get(const PlanetDescriptor& planet, TerrainTileKey key)
      -> std::expected<std::shared_ptr<const TerrainTile>, TerrainTileError>;

  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return m_entries.size();
  }
  [[nodiscard]] auto capacity() const noexcept -> std::size_t {
    return m_capacity;
  }
  [[nodiscard]] auto contains(TerrainTileKey key) const noexcept -> bool;

 private:
  struct Entry {
    PlanetDescriptor planet;
    TerrainTileKey key;
    std::shared_ptr<const TerrainTile> tile;
  };

  explicit TerrainTileCache(std::size_t capacity) : m_capacity{capacity} {}

  std::size_t m_capacity{};
  // Most recently used is at the front; least recently used is at the back.
  std::list<Entry> m_entries;
};

// Presentation-only sampling resolves one planet-fixed direction through the
// versioned tile generator and performs quantized bilinear interpolation
// inside the addressed tile. It does not change generator compatibility data.
[[nodiscard]] auto sample_planet_surface(
    const PlanetDescriptor& planet, PlanetFixedPositionMetres position,
    std::uint8_t lod, TerrainTileCache& cache)
    -> std::expected<TerrainSurfaceSample, TerrainTileError>;

}  // namespace apsis_drift
