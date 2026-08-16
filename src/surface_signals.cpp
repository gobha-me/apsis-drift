#include "apsis_drift/surface_signals.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <tuple>

#include "surface_signal_generation.hpp"

namespace apsis_drift {
namespace {

class SplitMix64 {
 public:
  explicit SplitMix64(Seed seed) noexcept : m_state{seed.value} {}

  [[nodiscard]] auto next() noexcept -> std::uint64_t {
    auto value = (m_state += 0x9E3779B97F4A7C15ULL);
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
  }

  [[nodiscard]] auto bounded(std::uint64_t exclusive_upper) noexcept
      -> std::uint64_t {
    if (exclusive_upper == 0) return 0;
    const auto threshold =
        (std::numeric_limits<std::uint64_t>::max() - exclusive_upper + 1U) %
        exclusive_upper;
    for (;;) {
      const auto value = next();
      if (value >= threshold) return value % exclusive_upper;
    }
  }

 private:
  std::uint64_t m_state{};
};

constexpr std::array<CubeFace, kSurfaceSignalCount> kSignalFaces{
    CubeFace::positive_x, CubeFace::negative_x, CubeFace::positive_y,
    CubeFace::negative_y, CubeFace::positive_z, CubeFace::negative_z,
};

constexpr std::array<std::size_t, 3> kReliefSampleCoordinates{16, 32, 48};

[[nodiscard]] auto signal_kind(std::uint64_t value) noexcept
    -> SurfaceSignalKind {
  switch (value) {
    case 0: return SurfaceSignalKind::survey;
    case 1: return SurfaceSignalKind::recovery;
    default: return SurfaceSignalKind::anomaly;
  }
}

[[nodiscard]] auto map_terrain_error(TerrainTileError error) noexcept
    -> SurfaceSignalError {
  if (error == TerrainTileError::invalid_planet) {
    return SurfaceSignalError::invalid_planet;
  }
  return SurfaceSignalError::terrain_failure;
}

[[nodiscard]] auto candidate_key(const PlanetDescriptor& planet,
                                 CubeFace face,
                                 SplitMix64& random) noexcept
    -> TerrainTileKey {
  constexpr auto tiles_per_axis =
      std::uint32_t{1} << kSurfaceSignalPlacementLod;
  constexpr auto central_begin = tiles_per_axis / 4U;
  constexpr auto central_width = tiles_per_axis / 2U;
  return {
      planet.id,
      face,
      kSurfaceSignalPlacementLod,
      central_begin +
          static_cast<std::uint32_t>(random.bounded(central_width)),
      central_begin +
          static_cast<std::uint32_t>(random.bounded(central_width)),
  };
}

struct AcceptedTerrain {
  TerrainTileAddress anchor;
  std::int32_t surface_elevation_metres{};
  std::int32_t approach_altitude_metres{};
};

[[nodiscard]] auto inspect_candidate(const PlanetDescriptor& planet,
                                     TerrainTileCache& cache,
                                     TerrainTileKey key,
                                     std::int32_t maximum_relief_metres)
    -> std::expected<std::optional<AcceptedTerrain>, SurfaceSignalError> {
  const auto tile = cache.get(planet, key);
  if (!tile) return std::unexpected{map_terrain_error(tile.error())};

  auto minimum = std::numeric_limits<std::int32_t>::max();
  auto maximum = std::numeric_limits<std::int32_t>::min();
  for (const auto y : kReliefSampleCoordinates) {
    for (const auto x : kReliefSampleCoordinates) {
      const auto sample = (*tile)->sample_at(x, y);
      if (!sample) {
        return std::unexpected{map_terrain_error(sample.error())};
      }
      minimum = std::min(minimum, sample->get().elevation_metres);
      maximum = std::max(maximum, sample->get().elevation_metres);
    }
  }
  if (maximum - minimum > maximum_relief_metres) return std::nullopt;

  const auto center = (*tile)->sample_at(32, 32);
  if (!center) return std::unexpected{map_terrain_error(center.error())};
  return AcceptedTerrain{
      .anchor = {key, 0.5, 0.5},
      .surface_elevation_metres = center->get().elevation_metres,
      .approach_altitude_metres =
          maximum + kSurfaceSignalApproachClearanceMetres,
  };
}

[[nodiscard]] auto generate_attributes(Seed signal_seed) noexcept
    -> std::tuple<SurfaceSignalKind, std::uint16_t, SurfaceSignalReward> {
  SplitMix64 random{
      derive_surface_signal_seed(signal_seed, SurfaceSignalStream::attributes)};
  const auto kind = signal_kind(random.bounded(3));
  const auto strength_width =
      static_cast<std::uint64_t>(kSurfaceSignalMaximumStrengthBasisPoints) -
      kSurfaceSignalMinimumStrengthBasisPoints + 1U;
  const auto reward_width =
      static_cast<std::uint64_t>(kSurfaceSignalMaximumRewardPoints) -
      kSurfaceSignalMinimumRewardPoints + 1U;
  const auto strength = static_cast<std::uint16_t>(
      kSurfaceSignalMinimumStrengthBasisPoints +
      random.bounded(strength_width));
  const auto reward = static_cast<std::uint16_t>(
      kSurfaceSignalMinimumRewardPoints + random.bounded(reward_width));
  return {kind, strength, SurfaceSignalReward{reward}};
}

}  // namespace

auto derive_surface_signal_seed(Seed signal_seed,
                                SurfaceSignalStream stream) noexcept -> Seed {
  return derive_seed(signal_seed, SeedDomain::encounter,
                     static_cast<std::uint64_t>(stream));
}

auto detail::generate_surface_signals_with_limits(
    const PlanetDescriptor& planet, TerrainTileCache& cache,
    SurfaceSignalPlacementLimits limits)
    -> std::expected<SurfaceSignalCatalog, SurfaceSignalError> {
  SurfaceSignalCatalog catalog{.planet = planet.id, .signals = {}};
  for (std::size_t index = 0; index < catalog.signals.size(); ++index) {
    const auto ordinal = static_cast<std::uint32_t>(index);
    const auto signal_seed =
        derive_seed(planet.seed, SeedDomain::encounter, ordinal);
    SplitMix64 placement{
        derive_surface_signal_seed(signal_seed, SurfaceSignalStream::placement)};

    std::optional<AcceptedTerrain> accepted;
    std::uint16_t accepted_attempt{};
    for (std::uint16_t attempt = 0; attempt < limits.attempts; ++attempt) {
      auto candidate = inspect_candidate(
          planet, cache, candidate_key(planet, kSignalFaces[index], placement),
          limits.maximum_relief_metres);
      if (!candidate) return std::unexpected{candidate.error()};
      if (*candidate) {
        accepted = **candidate;
        accepted_attempt = attempt;
        break;
      }
    }
    if (!accepted) {
      return std::unexpected{SurfaceSignalError::placement_exhausted};
    }

    const auto [kind, strength, reward] = generate_attributes(signal_seed);
    catalog.signals[index] = SurfaceSignal{
        .id = SurfaceSignalId{signal_seed.value},
        .ordinal = ordinal,
        .kind = kind,
        .anchor = accepted->anchor,
        .surface_elevation_metres = accepted->surface_elevation_metres,
        .approach_altitude_metres = accepted->approach_altitude_metres,
        .strength_basis_points = strength,
        .reward = reward,
        .placement_attempt = accepted_attempt,
    };
  }
  return catalog;
}

auto generate_surface_signals(const PlanetDescriptor& planet,
                              TerrainTileCache& cache)
    -> std::expected<SurfaceSignalCatalog, SurfaceSignalError> {
  return detail::generate_surface_signals_with_limits(planet, cache, {});
}

auto surface_signal_id_string(SurfaceSignalId id) -> std::string {
  return std::format("signal-{:016x}", id.value);
}

}  // namespace apsis_drift
