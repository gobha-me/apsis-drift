#include "apsis_drift/orbital.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <vector>

namespace apsis_drift {
namespace {

struct Vector3 {
  double x{};
  double y{};
  double z{};
};

[[nodiscard]] auto finite(double value) noexcept -> bool {
  return std::isfinite(value);
}

[[nodiscard]] auto finite(Vector3 value) noexcept -> bool {
  return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] auto vector(PlanetFixedPositionMetres value) noexcept
    -> Vector3 {
  return {value.x, value.y, value.z};
}

[[nodiscard]] auto vector(PlanetFixedDirection value) noexcept -> Vector3 {
  return {value.x, value.y, value.z};
}

[[nodiscard]] auto add(Vector3 left, Vector3 right) noexcept -> Vector3 {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] auto multiply(Vector3 value, double scalar) noexcept -> Vector3 {
  return {value.x * scalar, value.y * scalar, value.z * scalar};
}

[[nodiscard]] auto dot(Vector3 left, Vector3 right) noexcept -> double {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] auto cross(Vector3 left, Vector3 right) noexcept -> Vector3 {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

[[nodiscard]] auto length(Vector3 value) noexcept -> double {
  return std::sqrt(value.x * value.x + value.y * value.y +
                   value.z * value.z);
}

[[nodiscard]] auto normalized(Vector3 value) noexcept -> Vector3 {
  return multiply(value, 1.0 / length(value));
}

[[nodiscard]] auto valid_direction(Vector3 value) noexcept -> bool {
  if (!finite(value)) return false;
  const auto magnitude = length(value);
  return finite(magnitude) && magnitude > 1.0e-12;
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
      return pressure >= 1'500 &&
             pressure <= AtmospherePressureMillibars::max;
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
  return planet.radius.value >= PlanetRadiusKm::min &&
         planet.radius.value <= PlanetRadiusKm::max &&
         planet.surface_gravity.value >= SurfaceGravityMilliG::min &&
         planet.surface_gravity.value <= SurfaceGravityMilliG::max &&
         planet.water_coverage.value <= WaterCoverageBasisPoints::max &&
         valid_atmosphere(planet) && valid_terrain(planet.terrain_character) &&
         valid_palette(planet.palette.family);
}

[[nodiscard]] auto mix64(std::uint64_t value) noexcept -> std::uint64_t {
  value ^= value >> 30U;
  value *= 0xBF58476D1CE4E5B9ULL;
  value ^= value >> 27U;
  value *= 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

[[nodiscard]] auto lattice_hash(std::uint64_t seed, std::int32_t x,
                                std::int32_t y, std::int32_t z) noexcept
    -> double {
  auto value = seed;
  value ^= mix64(static_cast<std::uint32_t>(x) + 0x9E3779B9ULL);
  value ^= std::rotl(mix64(static_cast<std::uint32_t>(y) + 0x85EBCA6BULL),
                     21);
  value ^= std::rotl(mix64(static_cast<std::uint32_t>(z) + 0xC2B2AE35ULL),
                     42);
  constexpr double denominator{16'777'215.0};
  return static_cast<double>(mix64(value) >> 40U) / denominator;
}

[[nodiscard]] auto fade(double value) noexcept -> double {
  return value * value * (3.0 - 2.0 * value);
}

[[nodiscard]] auto lerp(double from, double to, double amount) noexcept
    -> double {
  return from + (to - from) * amount;
}

[[nodiscard]] auto value_noise(Vector3 position, std::uint64_t seed) noexcept
    -> double {
  const auto x0 = static_cast<std::int32_t>(std::floor(position.x));
  const auto y0 = static_cast<std::int32_t>(std::floor(position.y));
  const auto z0 = static_cast<std::int32_t>(std::floor(position.z));
  const double tx = fade(position.x - static_cast<double>(x0));
  const double ty = fade(position.y - static_cast<double>(y0));
  const double tz = fade(position.z - static_cast<double>(z0));

  const double x00 = lerp(lattice_hash(seed, x0, y0, z0),
                          lattice_hash(seed, x0 + 1, y0, z0), tx);
  const double x10 = lerp(lattice_hash(seed, x0, y0 + 1, z0),
                          lattice_hash(seed, x0 + 1, y0 + 1, z0), tx);
  const double x01 = lerp(lattice_hash(seed, x0, y0, z0 + 1),
                          lattice_hash(seed, x0 + 1, y0, z0 + 1), tx);
  const double x11 = lerp(lattice_hash(seed, x0, y0 + 1, z0 + 1),
                          lattice_hash(seed, x0 + 1, y0 + 1, z0 + 1), tx);
  return lerp(lerp(x00, x10, ty), lerp(x01, x11, ty), tz);
}

[[nodiscard]] auto surface_field(Vector3 normal,
                                 const PlanetDescriptor& planet) noexcept
    -> double {
  double base_frequency{};
  double contrast{};
  switch (planet.terrain_character) {
    case TerrainCharacter::oceanic:
      base_frequency = 1.7;
      contrast = 0.82;
      break;
    case TerrainCharacter::plains:
      base_frequency = 2.1;
      contrast = 0.72;
      break;
    case TerrainCharacter::rugged:
      base_frequency = 2.8;
      contrast = 1.08;
      break;
    case TerrainCharacter::alpine:
      base_frequency = 3.2;
      contrast = 1.20;
      break;
    case TerrainCharacter::volcanic:
      base_frequency = 2.5;
      contrast = 1.32;
      break;
  }

  const auto terrain_seed = derive_planet_stream_seed(
      planet.seed, PlanetDescriptorStream::terrain);
  const double broad = value_noise(multiply(normal, base_frequency),
                                   terrain_seed.value);
  const double regional = value_noise(multiply(normal, base_frequency * 2.7),
                                      terrain_seed.value ^
                                          0xD1B54A32D192ED03ULL);
  const double combined = broad * 0.68 + regional * 0.32;
  return std::clamp(0.5 + (combined - 0.5) * contrast, 0.0, 1.0);
}

[[nodiscard]] auto channel(double value) noexcept -> std::uint8_t {
  return static_cast<std::uint8_t>(
      std::clamp(static_cast<int>(std::lround(value)), 0, 255));
}

[[nodiscard]] auto pixel(Rgb8 color) noexcept -> termforge::Pixel {
  return {color.red, color.green, color.blue, 255};
}

[[nodiscard]] auto blend(termforge::Pixel from, termforge::Pixel to,
                         double amount) noexcept -> termforge::Pixel {
  amount = std::clamp(amount, 0.0, 1.0);
  const double keep = 1.0 - amount;
  return {channel(static_cast<double>(from.r) * keep +
                  static_cast<double>(to.r) * amount),
          channel(static_cast<double>(from.g) * keep +
                  static_cast<double>(to.g) * amount),
          channel(static_cast<double>(from.b) * keep +
                  static_cast<double>(to.b) * amount),
          255};
}

[[nodiscard]] auto shade(termforge::Pixel color, double factor) noexcept
    -> termforge::Pixel {
  return {channel(static_cast<double>(color.r) * factor),
          channel(static_cast<double>(color.g) * factor),
          channel(static_cast<double>(color.b) * factor), 255};
}

[[nodiscard]] auto surface_color(const PlanetDescriptor& planet,
                                 double field) noexcept -> termforge::Pixel {
  const double water_fraction =
      static_cast<double>(planet.water_coverage.value) / 10'000.0;
  const double water_threshold =
      water_fraction <= 0.0
          ? 0.0
          : (water_fraction >= 1.0 ? 1.0
                                   : 0.28 + water_fraction * 0.44);
  if (water_threshold > 0.0 && field < water_threshold) {
    const double depth =
        std::clamp((water_threshold - field) / water_threshold, 0.0, 1.0);
    return blend(pixel(planet.palette.shallow_water),
                 pixel(planet.palette.deep_water), depth);
  }

  const double land = std::clamp(
      (field - water_threshold) /
          std::max(1.0e-9, 1.0 - water_threshold),
      0.0, 1.0);
  if (land < 0.68) {
    return blend(pixel(planet.palette.lowland),
                 pixel(planet.palette.highland), land / 0.68);
  }
  return blend(pixel(planet.palette.highland), pixel(planet.palette.peak),
               (land - 0.68) / 0.32);
}

[[nodiscard]] auto space_pixel(int x, int y) noexcept -> termforge::Pixel {
  const auto key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x))
                    << 32U) |
                   static_cast<std::uint32_t>(y);
  const auto star = mix64(key ^ 0xA0761D6478BD642FULL);
  if ((star & 0x7FFU) == 0U) {
    const auto brightness = static_cast<std::uint8_t>(150U +
        static_cast<unsigned>((star >> 12U) & 0x69U));
    return {brightness, brightness,
            static_cast<std::uint8_t>(std::min(255U,
                static_cast<unsigned>(brightness) + 12U)), 255};
  }
  return {4, 7, 13, 255};
}

[[nodiscard]] auto atmosphere_strength(const PlanetDescriptor& planet) noexcept
    -> double {
  if (planet.atmosphere_class == AtmosphereClass::airless) return 0.0;
  return std::clamp(
      static_cast<double>(planet.atmosphere_pressure.value) /
          static_cast<double>(AtmospherePressureMillibars::max),
      0.08, 1.0);
}

}  // namespace

OrbitalRenderer::OrbitalRenderer(OrbitalRenderSettings settings)
    : m_settings(settings) {}

auto OrbitalRenderer::render(
    const PlanetDescriptor& planet, const OrbitalCamera& camera,
    std::span<termforge::Pixel> destination) const
    -> std::expected<OrbitalRenderStats, OrbitalRenderError> {
  return render_impl(planet, camera, nullptr, 0, destination);
}

auto OrbitalRenderer::render_tile_backed(
    const PlanetDescriptor& planet, const OrbitalCamera& camera,
    std::uint8_t terrain_lod, TerrainTileCache& cache,
    std::span<termforge::Pixel> destination) const
    -> std::expected<OrbitalRenderStats, OrbitalRenderError> {
  return render_impl(planet, camera, &cache, terrain_lod, destination);
}

auto OrbitalRenderer::render_impl(
    const PlanetDescriptor& planet, const OrbitalCamera& camera,
    TerrainTileCache* cache, std::uint8_t terrain_lod,
    std::span<termforge::Pixel> destination) const
    -> std::expected<OrbitalRenderStats, OrbitalRenderError> {
  if (!validate_viewport({m_settings.width, m_settings.height})) {
    return std::unexpected{OrbitalRenderError::invalid_viewport};
  }
  const auto expected = static_cast<std::size_t>(m_settings.width) *
                        static_cast<std::size_t>(m_settings.height);
  if (destination.size() != expected) {
    return std::unexpected{OrbitalRenderError::invalid_framebuffer};
  }
  if (!valid_planet(planet)) {
    return std::unexpected{OrbitalRenderError::invalid_planet};
  }
  if (cache != nullptr && terrain_lod > kMaxTerrainLod) {
    return std::unexpected{OrbitalRenderError::invalid_terrain_lod};
  }
  if (!finite(m_settings.field_of_view_degrees) ||
      m_settings.field_of_view_degrees <= 1.0 ||
      m_settings.field_of_view_degrees >= 179.0) {
    return std::unexpected{OrbitalRenderError::invalid_field_of_view};
  }

  const Vector3 camera_position = vector(camera.position);
  const Vector3 camera_forward = vector(camera.forward);
  const Vector3 camera_up = vector(camera.up);
  if (!finite(camera_position) || !finite(camera_forward) ||
      !finite(camera_up)) {
    return std::unexpected{OrbitalRenderError::non_finite_camera};
  }
  if (!valid_direction(camera_forward) || !valid_direction(camera_up)) {
    return std::unexpected{OrbitalRenderError::invalid_camera_basis};
  }

  const double radius = static_cast<double>(planet.radius.value) * 1'000.0;
  const double camera_distance =
      std::hypot(camera_position.x, camera_position.y, camera_position.z);
  const double maximum_safe_coordinate =
      std::sqrt(std::numeric_limits<double>::max() / 3.0);
  if (!finite(camera_distance) || camera_distance > maximum_safe_coordinate) {
    return std::unexpected{OrbitalRenderError::non_finite_camera};
  }
  if (camera_distance <= radius) {
    return std::unexpected{OrbitalRenderError::camera_inside_planet};
  }

  const Vector3 forward = normalized(camera_forward);
  const Vector3 camera_right = cross(forward, normalized(camera_up));
  if (!valid_direction(camera_right)) {
    return std::unexpected{OrbitalRenderError::invalid_camera_basis};
  }
  const Vector3 right = normalized(camera_right);
  const Vector3 up = normalized(cross(right, forward));

  const Vector3 raw_light = vector(m_settings.light_direction);
  if (!valid_direction(raw_light)) {
    return std::unexpected{OrbitalRenderError::invalid_light_direction};
  }
  const Vector3 light = normalized(raw_light);

  const double horizontal_tangent = std::tan(
      m_settings.field_of_view_degrees * std::numbers::pi / 360.0);
  if (!finite(horizontal_tangent) || horizontal_tangent <= 0.0) {
    return std::unexpected{OrbitalRenderError::invalid_field_of_view};
  }
  const double aspect = static_cast<double>(m_settings.width) /
                        static_cast<double>(m_settings.height);
  const double vertical_tangent = horizontal_tangent / aspect;
  const double camera_radius_squared = dot(camera_position, camera_position);
  const double atmosphere = atmosphere_strength(planet);
  const double atmosphere_radius = radius * (1.0 + 0.018 + atmosphere * 0.025);
  const double atmosphere_radius_squared = atmosphere_radius * atmosphere_radius;
  const auto atmosphere_color = pixel(planet.palette.atmosphere);

  OrbitalRenderStats stats;
  std::vector<TerrainTileKey> touched_tiles;
  std::vector<std::shared_ptr<const TerrainTile>> pinned_tiles;
  for (int y = 0; y < m_settings.height; ++y) {
    const double screen_y =
        (1.0 - (static_cast<double>(y) + 0.5) * 2.0 /
                   static_cast<double>(m_settings.height)) *
        vertical_tangent;
    for (int x = 0; x < m_settings.width; ++x) {
      const double screen_x =
          ((static_cast<double>(x) + 0.5) * 2.0 /
               static_cast<double>(m_settings.width) -
           1.0) *
          horizontal_tangent;
      const Vector3 ray = normalized(add(
          forward, add(multiply(right, screen_x), multiply(up, screen_y))));
      const double camera_along_ray = dot(camera_position, ray);
      const double discriminant =
          camera_along_ray * camera_along_ray -
          (camera_radius_squared - radius * radius);
      const auto index = static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(m_settings.width) +
                         static_cast<std::size_t>(x);

      if (discriminant >= 0.0) {
        const double distance = -camera_along_ray - std::sqrt(discriminant);
        if (distance > 0.0) {
          const Vector3 point =
              add(camera_position, multiply(ray, distance));
          const Vector3 normal = normalized(point);
          termforge::Pixel color;
          if (cache != nullptr) {
            const auto sample = sample_planet_surface(
                planet, {point.x, point.y, point.z}, terrain_lod, *cache);
            if (!sample) {
              return std::unexpected{OrbitalRenderError::terrain_failure};
            }
            color = pixel(sample->color);
            if (std::ranges::find(touched_tiles, sample->address.tile) ==
                touched_tiles.end()) {
              touched_tiles.push_back(sample->address.tile);
              const auto pinned = cache->get(planet, sample->address.tile);
              if (!pinned) {
                return std::unexpected{OrbitalRenderError::terrain_failure};
              }
              pinned_tiles.push_back(*pinned);
            }
          } else {
            const double field = surface_field(normal, planet);
            color = surface_color(planet, field);
          }
          const double diffuse = std::max(0.0, dot(normal, light));
          const double view = std::max(0.0, dot(normal, multiply(ray, -1.0)));
          const double limb = 0.58 + 0.42 * std::sqrt(view);
          color = shade(color, (0.20 + diffuse * 0.88) * limb);
          if (atmosphere > 0.0) {
            const double scatter =
                std::pow(1.0 - view, 2.2) * (0.18 + atmosphere * 0.36);
            color = blend(color, atmosphere_color, scatter);
          }
          destination[index] = color;
          ++stats.surface_pixels;
          continue;
        }
      }

      auto background = space_pixel(x, y);
      if (atmosphere > 0.0 && camera_along_ray < 0.0) {
        const double closest_squared = camera_radius_squared -
                                       camera_along_ray * camera_along_ray;
        if (closest_squared < atmosphere_radius_squared &&
            closest_squared > radius * radius) {
          const double closest = std::sqrt(std::max(0.0, closest_squared));
          const double halo =
              std::pow(std::clamp((atmosphere_radius - closest) /
                                      (atmosphere_radius - radius),
                                  0.0, 1.0),
                       1.6) *
              (0.22 + atmosphere * 0.48);
          background = blend(background, atmosphere_color, halo);
          ++stats.atmosphere_pixels;
        }
      }
      destination[index] = background;
    }
  }
  stats.terrain_tiles_touched = touched_tiles.size();
  return stats;
}

}  // namespace apsis_drift
