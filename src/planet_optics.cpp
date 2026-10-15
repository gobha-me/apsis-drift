#include "apsis_drift/planet_optics.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "apsis_drift/atmospheric_flight.hpp"

namespace apsis_drift {
namespace {
using Error = PlanetOpticsError;
constexpr std::uint64_t kOne{65'536};
constexpr std::size_t kColumns{8}, kRows{5};

auto word(Seed seed, std::uint64_t ordinal) -> std::uint64_t {
  auto value = derive_seed(derive_planet_stream_seed(
                               seed, PlanetDescriptorStream::optical_cover),
                           SeedDomain::planet, ordinal)
                   .value;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}
auto fade(std::uint64_t fraction) -> std::uint64_t {
  return fraction * fraction * (3 * kOne - 2 * fraction) / (kOne * kOne);
}
auto mix(std::uint64_t a, std::uint64_t b, std::uint64_t fraction)
    -> std::uint64_t {
  return (a * (kOne - fraction) + b * fraction + kOne / 2) / kOne;
}
auto atlas(Seed seed)
    -> std::array<std::uint8_t, kPlanetOpticsWidth * kPlanetOpticsHeight> {
  std::array<std::uint64_t, kColumns * kRows> lattice{};
  for (std::size_t y = 0; y < kRows; ++y)
    for (std::size_t x = 0; x < kColumns; ++x) {
      const auto longitude = y == 0 || y == kRows - 1 ? 0 : x;
      lattice[y * kColumns + x] =
          word(seed, 2 + y * kColumns + longitude) % 256;
    }
  std::array<std::uint8_t, kPlanetOpticsWidth * kPlanetOpticsHeight> result{};
  for (std::size_t y = 0; y < kPlanetOpticsHeight; ++y) {
    const auto scaled_y = y * (kRows - 1);
    const auto row = scaled_y / (kPlanetOpticsHeight - 1);
    const auto next_row = std::min(row + 1, kRows - 1);
    const auto fy = fade((scaled_y % (kPlanetOpticsHeight - 1)) * kOne /
                         (kPlanetOpticsHeight - 1));
    for (std::size_t x = 0; x < kPlanetOpticsWidth; ++x) {
      const auto column = x / (kPlanetOpticsWidth / kColumns);
      const auto next_column = (column + 1) % kColumns;
      const auto fx = fade((x % (kPlanetOpticsWidth / kColumns)) * kOne /
                           (kPlanetOpticsWidth / kColumns));
      result[y * kPlanetOpticsWidth + x] = static_cast<std::uint8_t>(
          mix(mix(lattice[row * kColumns + column],
                  lattice[row * kColumns + next_column], fx),
              mix(lattice[next_row * kColumns + column],
                  lattice[next_row * kColumns + next_column], fx),
              fy));
    }
  }
  return result;
}
} // namespace

auto generate_planet_optics_recipe(const PhysicalLocalSystem& system,
                                   PlanetId planet, std::uint32_t version)
    -> std::expected<PlanetOpticsRecipe, Error> {
  if (version != kPlanetOpticsVersion)
    return std::unexpected{Error::unsupported_version};
  const auto ambient = generate_planet_ambient_recipe(system, planet);
  if (!ambient) return std::unexpected{Error::invalid_owner};
  return PlanetOpticsRecipe{version, *ambient};
}

auto resolve_planet_optics(const PhysicalLocalSystem& system,
                           const PlanetOpticsRecipe& recipe)
    -> std::expected<PlanetOpticsField, Error> {
  if (recipe.version != kPlanetOpticsVersion ||
      recipe.ambient.version != kPlanetAmbientGeneratorVersion)
    return std::unexpected{Error::unsupported_version};
  const auto ambient =
      resolve_planet_ambient_environment(system, recipe.ambient);
  const auto planet = find_local_system_planet(system, recipe.ambient.planet);
  if (!ambient || !planet) return std::unexpected{Error::invalid_owner};
  const auto& descriptor = (*planet)->descriptor;
  const auto profile = resolve_atmospheric_flight_profile(descriptor);
  if (!profile) return std::unexpected{Error::invalid_owner};
  PlanetOpticsField result{.recipe = recipe, .kind = ambient->visibility};
  if (result.kind == AtmosphericVisibilityKind::cloud ||
      result.kind == AtmosphericVisibilityKind::dust) {
    const bool cloud = result.kind == AtmosphericVisibilityKind::cloud;
    result.lower_height_metres = cloud ? 2'000.0 : 0.0;
    result.upper_height_metres = std::min(
        cloud ? 6'000.0 : 3'000.0, profile->space_boundary_altitude_metres);
    result.lower_height_metres =
        std::min(result.lower_height_metres, result.upper_height_metres);
    result.opacity_cap_basis_points = static_cast<std::uint16_t>(
        static_cast<std::uint64_t>(ambient->maximum_obscuration_basis_points) *
        (2'500 + word(descriptor.seed, 1) % 5'001) / 10'000);
    result.density = atlas(descriptor.seed);
    const auto& lowland = descriptor.palette.lowland;
    result.tint =
        cloud ? Rgb8{214, 220, 225}
              : Rgb8{static_cast<std::uint8_t>((170U + lowland.red) / 2),
                     static_cast<std::uint8_t>((127U + lowland.green) / 2),
                     static_cast<std::uint8_t>((76U + lowland.blue) / 2)};
  }
  if (auto valid = validate_planet_optics_field(result); !valid)
    return std::unexpected{valid.error()};
  return result;
}

auto validate_planet_optics_field(const PlanetOpticsField& field)
    -> std::expected<void, Error> {
  if (field.recipe.version != kPlanetOpticsVersion ||
      field.recipe.ambient.version != kPlanetAmbientGeneratorVersion)
    return std::unexpected{Error::unsupported_version};
  if (!std::isfinite(field.lower_height_metres) ||
      !std::isfinite(field.upper_height_metres) ||
      field.lower_height_metres < 0 ||
      field.upper_height_metres < field.lower_height_metres ||
      field.opacity_cap_basis_points > 7'500)
    return std::unexpected{Error::invalid_field};
  switch (field.kind) {
    case AtmosphericVisibilityKind::airless:
    case AtmosphericVisibilityKind::clear:
      if (field.lower_height_metres != 0 || field.upper_height_metres != 0 ||
          field.opacity_cap_basis_points != 0 ||
          std::ranges::any_of(field.density, [](auto v) { return v != 0; }))
        return std::unexpected{Error::invalid_field};
      break;
    case AtmosphericVisibilityKind::cloud:
      if (field.lower_height_metres > 2'000 ||
          field.upper_height_metres > 6'000 ||
          field.lower_height_metres >= field.upper_height_metres)
        return std::unexpected{Error::invalid_field};
      break;
    case AtmosphericVisibilityKind::dust:
      if (field.lower_height_metres != 0 || field.upper_height_metres <= 0 ||
          field.upper_height_metres > 3'000)
        return std::unexpected{Error::invalid_field};
      break;
    default: return std::unexpected{Error::invalid_field};
  }
  for (std::size_t x = 1; x < kPlanetOpticsWidth; ++x)
    if (field.density[x] != field.density[0] ||
        field.density[(kPlanetOpticsHeight - 1) * kPlanetOpticsWidth + x] !=
            field.density[(kPlanetOpticsHeight - 1) * kPlanetOpticsWidth])
      return std::unexpected{Error::invalid_field};
  return {};
}

auto sample_planet_optics_density(const PlanetOpticsField& field,
                                  double latitude, double longitude,
                                  double height)
    -> std::expected<double, Error> {
  if (auto valid = validate_planet_optics_field(field); !valid)
    return std::unexpected{valid.error()};
  constexpr double pi = std::numbers::pi;
  if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
      !std::isfinite(height) || latitude < -pi / 2 || latitude > pi / 2 ||
      longitude < -pi || longitude > pi)
    return std::unexpected{Error::invalid_sample};
  if (height <= field.lower_height_metres ||
      height >= field.upper_height_metres)
    return 0.0;
  const double x = (longitude + pi) / (2 * pi) * kPlanetOpticsWidth;
  const double y = (latitude + pi / 2) / pi * (kPlanetOpticsHeight - 1);
  const auto ix = static_cast<std::size_t>(std::floor(x));
  const auto iy = static_cast<std::size_t>(std::floor(y));
  const double fx = x - std::floor(x), fy = y - std::floor(y);
  const auto pixel = [&](std::size_t px, std::size_t py) {
    return static_cast<double>(
               field.density[std::min(py, kPlanetOpticsHeight - 1) *
                                 kPlanetOpticsWidth +
                             px % kPlanetOpticsWidth]) /
           255.0;
  };
  const double density =
      std::lerp(std::lerp(pixel(ix, iy), pixel(ix + 1, iy), fx),
                std::lerp(pixel(ix, iy + 1), pixel(ix + 1, iy + 1), fx), fy);
  const double t = (height - field.lower_height_metres) /
                   (field.upper_height_metres - field.lower_height_metres);
  return density * 4 * t * (1 - t);
}
} // namespace apsis_drift
