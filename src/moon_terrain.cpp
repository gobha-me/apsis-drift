#include "apsis_drift/moon_terrain.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "terrain_detail_noise_internal.hpp"

namespace apsis_drift {
namespace {
using Error = MoonTerrainError;
[[nodiscard]] auto unit(MoonFixedDirection value)
    -> std::expected<MoonFixedDirection, Error> {
  if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
      !std::isfinite(value.z))
    return std::unexpected{Error::invalid_direction};
  const double scale =
      std::max({std::abs(value.x), std::abs(value.y), std::abs(value.z)});
  if (scale == 0) return std::unexpected{Error::invalid_direction};
  value = {value.x / scale, value.y / scale, value.z / scale};
  const double length =
      std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
  value = {value.x / length, value.y / length, value.z / length};
  if (value.x == 0) value.x = 0;
  if (value.y == 0) value.y = 0;
  if (value.z == 0) value.z = 0;
  return value;
}
} // namespace

auto MoonTerrainSampler::create(const PhysicalMoonDescriptor& owner,
                                std::uint32_t version)
    -> std::expected<MoonTerrainSampler, Error> {
  if (version != kMoonTerrainRecipeVersion)
    return std::unexpected{Error::unsupported_version};
  if (!validate_physical_moon_descriptor(owner))
    return std::unexpected{Error::invalid_owner};
  return MoonTerrainSampler{owner};
}

auto MoonTerrainSampler::sample_unit(MoonFixedDirection direction) const
    -> std::expected<MoonTerrainSample, Error> {
  const double radius = m_owner.radius_metres;
  const MoonFixedDirection position{direction.x * radius, direction.y * radius,
                                    direction.z * radius};
  // Three broad dry-relief bands, then the existing rugged detail bands.
  constexpr std::array spacing{131072.0, 32768.0, 8192.0, 2048.0, 768.0, 320.0};
  constexpr std::array amplitude{3200.0, 1000.0, 300.0, 32.0, 10.0, 3.0};
  double height{};
  for (std::size_t i = 0; i < spacing.size(); ++i) {
    const auto seed =
        derive_seed(m_owner.terrain_seed, SeedDomain::terrain, i + 1);
    const auto value =
        terrain_detail_internal::noise(position, spacing[i], seed);
    if (!value) return std::unexpected{Error::unsafe_arithmetic};
    height += amplitude[i] * *value;
  }
  if (!std::isfinite(height) ||
      std::abs(height) > kMoonTerrainMaximumReliefMetres)
    return std::unexpected{Error::unsafe_arithmetic};
  // Quantized tint-derived dry rock. No sea-level clamp or borrowed palette.
  const auto shade = static_cast<int>(std::llround(height / 128.0));
  const auto channel = [shade](std::uint8_t value) {
    return static_cast<std::uint8_t>(
        std::clamp(static_cast<int>(value) + shade, 0, 255));
  };
  return MoonTerrainSample{height,
                           {channel(m_owner.tint.red),
                            channel(m_owner.tint.green),
                            channel(m_owner.tint.blue)}};
}

auto MoonTerrainSampler::sample(MoonFixedDirection direction) const
    -> std::expected<MoonTerrainSample, Error> {
  const auto normalized = unit(direction);
  if (!normalized) return std::unexpected{normalized.error()};
  return sample_unit(*normalized);
}

auto MoonTerrainSampler::surface_position(MoonFixedDirection direction) const
    -> std::expected<MoonFixedPositionMetres, Error> {
  const auto normalized = unit(direction);
  if (!normalized) return std::unexpected{normalized.error()};
  const auto height = sample_unit(*normalized);
  if (!height) return std::unexpected{height.error()};
  const double radius = m_owner.radius_metres + height->elevation_metres;
  return MoonFixedPositionMetres{normalized->x * radius, normalized->y * radius,
                                 normalized->z * radius};
}

auto MoonTerrainSampler::clearance(MoonFixedPositionMetres position) const
    -> std::expected<double, Error> {
  if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
      !std::isfinite(position.z))
    return std::unexpected{Error::invalid_position};
  const double radius = std::hypot(position.x, position.y, position.z);
  if (!std::isfinite(radius) || radius == 0)
    return std::unexpected{Error::invalid_position};
  const auto height = sample({position.x, position.y, position.z});
  if (!height) return std::unexpected{height.error()};
  return (radius - m_owner.radius_metres) - height->elevation_metres;
}

auto MoonTerrainSampler::fill_tile(MoonTerrainTileKey key,
                                   std::size_t samples_per_axis,
                                   std::span<MoonTerrainSample> output) const
    -> std::expected<void, Error> {
  if (samples_per_axis != kTerrainTileSamplesPerAxis)
    return std::unexpected{Error::invalid_dimensions};
  if (output.size() < kTerrainTileSampleCount)
    return std::unexpected{Error::short_buffer};
  if (key.body != m_owner.body) return std::unexpected{Error::wrong_body};
  if (!direction_from_cube_surface_address(
          {key.face, key.lod, key.x, key.y, 0, 0}))
    return std::unexpected{Error::invalid_tile};
  std::array<MoonTerrainSample, kTerrainTileSampleCount> staged;
  for (std::size_t y = 0; y < samples_per_axis; ++y) {
    for (std::size_t x = 0; x < samples_per_axis; ++x) {
      const auto direction = direction_from_cube_surface_address(
          {key.face, key.lod, key.x, key.y,
           static_cast<double>(x) / kTerrainTileIntervalsPerAxis,
           static_cast<double>(y) / kTerrainTileIntervalsPerAxis});
      if (!direction) return std::unexpected{Error::invalid_tile};
      const auto value = sample({direction->x, direction->y, direction->z});
      if (!value) return std::unexpected{value.error()};
      staged[y * samples_per_axis + x] = *value;
    }
  }
  std::ranges::copy(staged, output.begin());
  return {};
}
} // namespace apsis_drift
