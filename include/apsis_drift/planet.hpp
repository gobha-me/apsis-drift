#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "apsis_drift/seed.hpp"

namespace apsis_drift {

// Planet generation is generated-world compatibility data. A changed mapping
// from streams to descriptor fields requires a new version.
inline constexpr std::uint32_t kPlanetGeneratorVersion{1};

// These ordinals name independent children of a planet seed. Never renumber an
// existing stream; add a new value instead.
enum class PlanetDescriptorStream : std::uint64_t {
  name = 1,
  physical = 2,
  atmosphere = 3,
  terrain = 4,
  hydrology = 5,
  palette = 6,
  celestial = 7,
};

struct PlanetId {
  std::uint64_t value{};

  friend auto operator==(const PlanetId&, const PlanetId&) -> bool = default;
};

struct PlanetRadiusKm {
  static constexpr std::uint32_t min{2'500};
  static constexpr std::uint32_t max{9'000};

  std::uint32_t value{};

  friend auto operator==(const PlanetRadiusKm&, const PlanetRadiusKm&)
      -> bool = default;
};

struct SurfaceGravityMilliG {
  static constexpr std::uint16_t min{350};
  static constexpr std::uint16_t max{1'800};

  std::uint16_t value{};

  friend auto operator==(const SurfaceGravityMilliG&,
                         const SurfaceGravityMilliG&) -> bool = default;
};

struct AtmospherePressureMillibars {
  static constexpr std::uint16_t min{0};
  static constexpr std::uint16_t max{2'500};

  std::uint16_t value{};

  friend auto operator==(const AtmospherePressureMillibars&,
                         const AtmospherePressureMillibars&) -> bool = default;
};

struct WaterCoverageBasisPoints {
  static constexpr std::uint16_t min{0};
  static constexpr std::uint16_t max{10'000};

  std::uint16_t value{};

  friend auto operator==(const WaterCoverageBasisPoints&,
                         const WaterCoverageBasisPoints&) -> bool = default;
};

enum class AtmosphereClass : std::uint8_t {
  airless,
  tenuous,
  temperate,
  dense,
};

enum class TerrainCharacter : std::uint8_t {
  oceanic,
  plains,
  rugged,
  alpine,
  volcanic,
};

enum class PaletteFamily : std::uint8_t {
  verdant,
  arid,
  glacial,
  volcanic,
  alien,
};

struct Rgb8 {
  std::uint8_t red{};
  std::uint8_t green{};
  std::uint8_t blue{};

  friend auto operator==(const Rgb8&, const Rgb8&) -> bool = default;
};

struct PlanetPalette {
  PaletteFamily family{};
  Rgb8 atmosphere;
  Rgb8 deep_water;
  Rgb8 shallow_water;
  Rgb8 lowland;
  Rgb8 highland;
  Rgb8 peak;

  friend auto operator==(const PlanetPalette&, const PlanetPalette&)
      -> bool = default;
};

// The descriptor contains no mutable generator state and exposes no mutation
// API. Its fixed-width units are suitable for simulation, rendering, and
// versioned diagnostics without treating floating point as serialized data.
struct PlanetDescriptor {
  const Seed seed;
  const PlanetId id;
  const std::string display_name;
  const PlanetRadiusKm radius;
  const SurfaceGravityMilliG surface_gravity;
  const AtmosphereClass atmosphere_class;
  const AtmospherePressureMillibars atmosphere_pressure;
  const TerrainCharacter terrain_character;
  const WaterCoverageBasisPoints water_coverage;
  const PlanetPalette palette;

  friend auto operator==(const PlanetDescriptor&, const PlanetDescriptor&)
      -> bool = default;
};

[[nodiscard]] auto derive_planet_stream_seed(
    Seed planet_seed, PlanetDescriptorStream stream) noexcept -> Seed;

[[nodiscard]] auto generate_planet_descriptor(Seed planet_seed)
    -> PlanetDescriptor;

[[nodiscard]] auto atmosphere_class_name(AtmosphereClass value) noexcept
    -> std::string_view;
[[nodiscard]] auto terrain_character_name(TerrainCharacter value) noexcept
    -> std::string_view;
[[nodiscard]] auto palette_family_name(PaletteFamily value) noexcept
    -> std::string_view;

[[nodiscard]] auto planet_descriptor_json(const PlanetDescriptor& descriptor)
    -> std::string;

} // namespace apsis_drift
