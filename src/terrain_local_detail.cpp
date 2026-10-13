#include "apsis_drift/terrain_local_detail.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <utility>

namespace apsis_drift {
namespace {
using Error = TerrainLocalDetailError;

[[nodiscard]] auto unit_direction(PlanetFixedDirection value)
    -> std::expected<PlanetFixedDirection, Error> {
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
  // Equivalent cube addresses must not introduce signed-zero identities.
  if (value.x == 0) value.x = 0;
  if (value.y == 0) value.y = 0;
  if (value.z == 0) value.z = 0;
  return value;
}

[[nodiscard]] auto mix64(std::uint64_t value) noexcept -> std::uint64_t {
  value ^= value >> 30U;
  value *= 0xBF58476D1CE4E5B9ULL;
  value ^= value >> 27U;
  value *= 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

[[nodiscard]] auto lattice_value(Seed seed, std::int64_t x, std::int64_t y,
                                 std::int64_t z) noexcept -> double {
  auto value = seed.value;
  value ^= mix64(static_cast<std::uint64_t>(x) + 0x9E3779B97F4A7C15ULL);
  value ^= std::rotl(
      mix64(static_cast<std::uint64_t>(y) + 0xD1B54A32D192ED03ULL), 21);
  value ^= std::rotl(
      mix64(static_cast<std::uint64_t>(z) + 0x94D049BB133111EBULL), 42);
  return static_cast<double>(mix64(value) >> 11U) / 4'503'599'627'370'496.0 -
         1.0;
}

[[nodiscard]] auto quintic(double t) noexcept -> double {
  return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

[[nodiscard]] auto lerp(double first, double second, double fraction) noexcept
    -> double {
  return first + (second - first) * fraction;
}

[[nodiscard]] auto noise(PlanetFixedDirection position, double spacing,
                         Seed seed) -> std::expected<double, Error> {
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

[[nodiscard]] auto amplitudes(TerrainCharacter character) noexcept
    -> std::array<double, 3> {
  switch (character) {
    case TerrainCharacter::oceanic: return {4.0, 1.0, 0.25};
    case TerrainCharacter::plains: return {8.0, 2.0, 0.5};
    case TerrainCharacter::rugged: return {32.0, 10.0, 3.0};
    case TerrainCharacter::alpine: return {48.0, 16.0, 5.0};
    case TerrainCharacter::volcanic: return {40.0, 12.0, 4.0};
  }
  return {};
}

[[nodiscard]] auto local_offset(PlanetFixedDirection unit, double radius,
                                Seed seed, TerrainCharacter character)
    -> std::expected<double, Error> {
  const PlanetFixedDirection position{unit.x * radius, unit.y * radius,
                                      unit.z * radius};
  constexpr std::array spacings{2'048.0, 768.0, 320.0};
  const auto weights = amplitudes(character);
  double offset{};
  for (std::size_t index = 0; index < spacings.size(); ++index) {
    const auto octave_seed = derive_seed(seed, SeedDomain::terrain, index + 1);
    const auto value = noise(position, spacings[index], octave_seed);
    if (!value) return std::unexpected{value.error()};
    offset += weights[index] * *value;
  }
  if (!std::isfinite(offset)) return std::unexpected{Error::unsafe_arithmetic};
  return offset;
}

struct MacroQuad {
  std::array<TerrainSample, 4> samples;
  double tx{}, ty{};
};

[[nodiscard]] auto macro_quad(const PlanetDescriptor& planet,
                              const TerrainTileAddress& address,
                              TerrainTileCache& cache, bool direct)
    -> std::expected<MacroQuad, Error> {
  constexpr auto intervals = kTerrainTileIntervalsPerAxis;
  const double grid_x = address.u * static_cast<double>(intervals);
  const double grid_y = address.v * static_cast<double>(intervals);
  const auto x0 = static_cast<std::size_t>(std::floor(grid_x));
  const auto y0 = static_cast<std::size_t>(std::floor(grid_y));
  const auto x1 = std::min(x0 + 1, intervals);
  const auto y1 = std::min(y0 + 1, intervals);
  MacroQuad result{
      {}, grid_x - static_cast<double>(x0), grid_y - static_cast<double>(y0)};
  const std::array<std::array<std::size_t, 2>, 4> coordinates{
      {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}}};
  std::shared_ptr<const TerrainTile> tile;
  if (!direct) {
    const auto fetched = cache.get(planet, address.tile);
    if (!fetched) return std::unexpected{Error::invalid_macro_source};
    tile = *fetched;
  }
  for (std::size_t i = 0; i < coordinates.size(); ++i) {
    const auto [x, y] = coordinates[i];
    if (direct) {
      const auto sample = generate_terrain_sample(planet, address.tile, x, y);
      if (!sample) return std::unexpected{Error::invalid_macro_source};
      result.samples[i] = *sample;
    } else {
      const auto sample = tile->sample_at(x, y);
      if (!sample) return std::unexpected{Error::invalid_macro_source};
      result.samples[i] = sample->get();
    }
  }
  return result;
}

[[nodiscard]] auto direct_legacy_sample(const PlanetDescriptor& planet,
                                        PlanetFixedDirection direction,
                                        TerrainTileCache& cache)
    -> std::expected<TerrainSurfaceSample, Error> {
  const auto address = terrain_address_from_planet_fixed(
      planet, {direction.x, direction.y, direction.z},
      kLocalDetailMacroSourceLod);
  if (!address) return std::unexpected{Error::invalid_direction};
  const auto quad = macro_quad(planet, *address, cache, true);
  if (!quad) return std::unexpected{quad.error()};
  constexpr std::int64_t fixed_one = 65'536;
  const auto fraction = [](double t) {
    return std::clamp(static_cast<std::int64_t>(std::llround(t * 65'536.0)),
                      std::int64_t{0}, std::int64_t{65'536});
  };
  const auto tx = fraction(quad->tx), ty = fraction(quad->ty);
  const auto interpolate = [](std::int64_t a, std::int64_t b, std::int64_t t) {
    return a + (b - a) * t / fixed_one;
  };
  const auto bilerp = [&](std::int64_t a, std::int64_t b, std::int64_t c,
                          std::int64_t d) {
    return interpolate(interpolate(a, b, tx), interpolate(c, d, tx), ty);
  };
  const auto& samples = quad->samples;
  const auto height =
      bilerp(samples[0].elevation_metres, samples[1].elevation_metres,
             samples[2].elevation_metres, samples[3].elevation_metres);
  const auto channel = [&](auto member) {
    return static_cast<std::uint8_t>(std::clamp<std::int64_t>(
        bilerp(samples[0].color.*member, samples[1].color.*member,
               samples[2].color.*member, samples[3].color.*member),
        0, 255));
  };
  return TerrainSurfaceSample{
      *address,
      static_cast<double>(height),
      {channel(&Rgb8::red), channel(&Rgb8::green), channel(&Rgb8::blue)}};
}

[[nodiscard]] auto continuous_macro(const PlanetDescriptor& planet,
                                    PlanetFixedDirection direction,
                                    TerrainTileCache& cache, bool direct)
    -> std::expected<double, Error> {
  const auto address = terrain_address_from_planet_direction(
      planet, direction, kLocalDetailMacroSourceLod);
  if (!address) return std::unexpected{Error::invalid_direction};
  const auto quad = macro_quad(planet, *address, cache, direct);
  if (!quad) return std::unexpected{quad.error()};
  const auto& samples = quad->samples;
  return lerp(
      lerp(samples[0].elevation_metres, samples[1].elevation_metres, quad->tx),
      lerp(samples[2].elevation_metres, samples[3].elevation_metres, quad->tx),
      quad->ty);
}

} // namespace

TerrainLocalDetailSampler::TerrainLocalDetailSampler(
    PhysicalLocalSystem owner, BodyId body, TerrainLocalDetailRecipe recipe,
    std::size_t planet_index, TerrainTileCache& macro_cache)
    : m_owner{std::move(owner)}, m_body{body}, m_recipe{recipe},
      m_planet_index{planet_index}, m_macro_cache{&macro_cache},
      m_relief_seed{derive_terrain_generation_seed(
          planet().seed, TerrainGenerationStream::local_relief)} {
}

auto TerrainLocalDetailSampler::create(const PhysicalLocalSystem& owner,
                                       BodyId body,
                                       TerrainLocalDetailRecipe recipe,
                                       TerrainTileCache& macro_cache)
    -> std::expected<TerrainLocalDetailSampler, TerrainLocalDetailError> {
  if (recipe.version != kLegacyPhysicalTerrainRecipe &&
      recipe.version != kLocalDetailTerrainRecipe)
    return std::unexpected{Error::unsupported_version};
  if (!validate_local_system(owner))
    return std::unexpected{Error::invalid_owner};
  const auto id = planet_id(body);
  if (!id) return std::unexpected{Error::invalid_body};
  const auto found = std::ranges::find_if(owner.catalog.planets,
                                          [id](const LocalSystemPlanet& entry) {
                                            return entry.descriptor.id == *id;
                                          });
  if (found == owner.catalog.planets.end())
    return std::unexpected{Error::invalid_body};
  const auto index =
      static_cast<std::size_t>(found - owner.catalog.planets.begin());
  return TerrainLocalDetailSampler{owner, body, recipe, index, macro_cache};
}

auto TerrainLocalDetailSampler::height(PlanetFixedDirection direction,
                                       std::uint8_t output_lod,
                                       bool direct_macro) const
    -> std::expected<TerrainLocalDetailSample, TerrainLocalDetailError> {
  const auto unit = unit_direction(direction);
  if (!unit) return std::unexpected{unit.error()};
  if (output_lod > kMaxTerrainLod) return std::unexpected{Error::invalid_tile};
  const auto source_lod = m_recipe.version == kLegacyPhysicalTerrainRecipe
                              ? output_lod
                              : kLocalDetailMacroSourceLod;
  std::expected<TerrainSurfaceSample, Error> legacy;
  if (direct_macro && m_recipe.version == kLocalDetailTerrainRecipe) {
    legacy = direct_legacy_sample(planet(), direction, *m_macro_cache);
  } else {
    const auto sampled =
        sample_planet_surface(planet(), {direction.x, direction.y, direction.z},
                              source_lod, *m_macro_cache);
    if (!sampled)
      return std::unexpected{sampled.error() ==
                                     TerrainTileError::coordinate_failure
                                 ? Error::invalid_direction
                                 : Error::invalid_macro_source};
    legacy = *sampled;
  }
  if (!legacy) return std::unexpected{legacy.error()};
  TerrainLocalDetailSample result{.macro_elevation_metres =
                                      legacy->elevation_metres,
                                  .elevation_metres = legacy->elevation_metres,
                                  .color = legacy->color};
  if (m_recipe.version == kLegacyPhysicalTerrainRecipe) return result;
  const auto macro =
      continuous_macro(planet(), direction, *m_macro_cache, direct_macro);
  if (!macro) return std::unexpected{macro.error()};
  const auto detail = local_offset(*unit, planet().radius.value * 1'000.0,
                                   m_relief_seed, planet().terrain_character);
  if (!detail) return std::unexpected{detail.error()};
  result.macro_elevation_metres = *macro;
  result.local_offset_metres = *detail;
  result.elevation_metres = *macro + *detail;
  return result;
}

auto TerrainLocalDetailSampler::sample_height(PlanetFixedDirection direction,
                                              std::uint8_t output_lod) const
    -> std::expected<TerrainLocalDetailSample, TerrainLocalDetailError> {
  return height(direction, output_lod, true);
}

auto TerrainLocalDetailSampler::sample(PlanetFixedDirection direction,
                                       std::uint8_t output_lod) const
    -> std::expected<TerrainLocalDetailSample, TerrainLocalDetailError> {
  auto result = height(direction, output_lod);
  if (!result || m_recipe.version == kLegacyPhysicalTerrainRecipe)
    return result;
  const auto unit = unit_direction(direction);
  if (!unit) return std::unexpected{unit.error()};
  const double radius = planet().radius.value * 1'000.0;
  const std::array position{unit->x * radius, unit->y * radius,
                            unit->z * radius};
  std::array<double, 3> slope{};
  for (std::size_t axis = 0; axis < slope.size(); ++axis) {
    auto positive = position;
    auto negative = position;
    positive[axis] += kLocalDetailSlopeStepMetres;
    negative[axis] -= kLocalDetailSlopeStepMetres;
    const auto upper =
        height({positive[0], positive[1], positive[2]}, output_lod);
    const auto lower =
        height({negative[0], negative[1], negative[2]}, output_lod);
    if (!upper || !lower)
      return std::unexpected{!upper ? upper.error() : lower.error()};
    slope[axis] = (upper->elevation_metres - lower->elevation_metres) /
                  (2.0 * kLocalDetailSlopeStepMetres);
  }
  const double radial =
      slope[0] * unit->x + slope[1] * unit->y + slope[2] * unit->z;
  result->tangent_slope_estimate = PlanetFixedDirection{
      slope[0] - radial * unit->x, slope[1] - radial * unit->y,
      slope[2] - radial * unit->z};
  return result;
}

auto TerrainLocalDetailSampler::fill_tile(
    TerrainTileKey key, std::size_t samples_per_axis,
    std::span<TerrainLocalDetailSample> output) const
    -> std::expected<void, TerrainLocalDetailError> {
  if (samples_per_axis != kTerrainTileSamplesPerAxis)
    return std::unexpected{Error::invalid_dimensions};
  if (output.size() < kTerrainTileSampleCount)
    return std::unexpected{Error::short_buffer};
  if (!planet_fixed_from_terrain_address(planet(), {key, 0.0, 0.0}))
    return std::unexpected{Error::invalid_tile};
  std::array<TerrainLocalDetailSample, kTerrainTileSampleCount> staged;
  for (std::size_t y = 0; y < samples_per_axis; ++y) {
    for (std::size_t x = 0; x < samples_per_axis; ++x) {
      const TerrainTileAddress address{
          key, static_cast<double>(x) / kTerrainTileIntervalsPerAxis,
          static_cast<double>(y) / kTerrainTileIntervalsPerAxis};
      const auto position =
          planet_fixed_from_terrain_address(planet(), address);
      if (!position) return std::unexpected{Error::invalid_tile};
      const auto sampled =
          sample({position->x, position->y, position->z}, key.lod);
      if (!sampled) return std::unexpected{sampled.error()};
      staged[y * samples_per_axis + x] = *sampled;
    }
  }
  std::ranges::copy(staged, output.begin());
  return {};
}

} // namespace apsis_drift
