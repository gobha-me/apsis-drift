#include "apsis_drift/planet.hpp"

#include <array>
#include <format>
#include <limits>

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

template <typename Integer>
[[nodiscard]] auto generate_inclusive(SplitMix64& random, Integer minimum,
                                      Integer maximum) noexcept -> Integer {
  const auto width = static_cast<std::uint64_t>(maximum) -
                     static_cast<std::uint64_t>(minimum) + 1U;
  return static_cast<Integer>(static_cast<std::uint64_t>(minimum) +
                              random.bounded(width));
}

constexpr std::array<std::string_view, 16> kNameStarts{
    "Ae", "Al", "Ar", "Bel", "Ca", "Dra", "Eli", "Gal",
    "Io", "Ka", "Ly", "Mer", "Ny", "Or",  "Pra", "Sol",
};
constexpr std::array<std::string_view, 16> kNameMiddles{
    "ba", "ce", "di", "el", "fa", "gi", "ha", "io",
    "ka", "lu", "mi", "no", "ra", "se", "ta", "ve",
};
constexpr std::array<std::string_view, 16> kNameEnds{
    "a",  "ar", "ea", "en", "ia", "ion", "is",  "on",
    "or", "os", "um", "us", "yx", "ara", "eth", "une",
};

constexpr std::array<PlanetPalette, 5> kPalettes{
    PlanetPalette{PaletteFamily::verdant,
                  {88, 142, 196},
                  {18, 55, 104},
                  {32, 112, 146},
                  {76, 130, 68},
                  {112, 104, 72},
                  {206, 213, 205}},
    PlanetPalette{PaletteFamily::arid,
                  {176, 139, 102},
                  {54, 65, 87},
                  {85, 107, 116},
                  {166, 119, 66},
                  {119, 80, 50},
                  {218, 188, 137}},
    PlanetPalette{PaletteFamily::glacial,
                  {126, 169, 207},
                  {29, 69, 112},
                  {76, 139, 172},
                  {145, 172, 177},
                  {189, 207, 208},
                  {238, 246, 244}},
    PlanetPalette{PaletteFamily::volcanic,
                  {116, 76, 65},
                  {38, 34, 43},
                  {78, 55, 51},
                  {87, 58, 43},
                  {53, 46, 45},
                  {194, 102, 48}},
    PlanetPalette{PaletteFamily::alien,
                  {100, 78, 153},
                  {31, 38, 91},
                  {50, 105, 133},
                  {71, 123, 103},
                  {113, 75, 125},
                  {211, 179, 221}},
};

[[nodiscard]] auto generated_name(Seed planet_seed) -> std::string {
  SplitMix64 random{
      derive_planet_stream_seed(planet_seed, PlanetDescriptorStream::name)};
  const auto start = kNameStarts[random.bounded(kNameStarts.size())];
  const auto middle = kNameMiddles[random.bounded(kNameMiddles.size())];
  const auto end = kNameEnds[random.bounded(kNameEnds.size())];
  return std::format("{}{}{}", start, middle, end);
}

[[nodiscard]] auto color_hex(Rgb8 color) -> std::string {
  return std::format("#{:02x}{:02x}{:02x}", color.red, color.green, color.blue);
}

} // namespace

auto derive_planet_stream_seed(Seed planet_seed,
                               PlanetDescriptorStream stream) noexcept -> Seed {
  return derive_seed(planet_seed, SeedDomain::planet,
                     static_cast<std::uint64_t>(stream));
}

auto generate_planet_descriptor(Seed planet_seed) -> PlanetDescriptor {
  SplitMix64 physical{
      derive_planet_stream_seed(planet_seed, PlanetDescriptorStream::physical)};
  const PlanetRadiusKm radius{
      generate_inclusive(physical, PlanetRadiusKm::min, PlanetRadiusKm::max)};
  const SurfaceGravityMilliG gravity{generate_inclusive(
      physical, SurfaceGravityMilliG::min, SurfaceGravityMilliG::max)};

  SplitMix64 atmosphere{derive_planet_stream_seed(
      planet_seed, PlanetDescriptorStream::atmosphere)};
  const auto atmosphere_roll = atmosphere.bounded(100);
  AtmosphereClass atmosphere_class{};
  AtmospherePressureMillibars pressure{};
  if (atmosphere_roll < 12) {
    atmosphere_class = AtmosphereClass::airless;
    pressure = AtmospherePressureMillibars{0};
  } else if (atmosphere_roll < 35) {
    atmosphere_class = AtmosphereClass::tenuous;
    pressure = AtmospherePressureMillibars{
        generate_inclusive(atmosphere, std::uint16_t{1}, std::uint16_t{249})};
  } else if (atmosphere_roll < 80) {
    atmosphere_class = AtmosphereClass::temperate;
    pressure = AtmospherePressureMillibars{generate_inclusive(
        atmosphere, std::uint16_t{250}, std::uint16_t{1'499})};
  } else {
    atmosphere_class = AtmosphereClass::dense;
    pressure = AtmospherePressureMillibars{generate_inclusive(
        atmosphere, std::uint16_t{1'500}, AtmospherePressureMillibars::max)};
  }

  SplitMix64 terrain{
      derive_planet_stream_seed(planet_seed, PlanetDescriptorStream::terrain)};
  const auto terrain_character =
      static_cast<TerrainCharacter>(terrain.bounded(5));

  SplitMix64 hydrology{derive_planet_stream_seed(
      planet_seed, PlanetDescriptorStream::hydrology)};
  const WaterCoverageBasisPoints water_coverage{generate_inclusive(
      hydrology, WaterCoverageBasisPoints::min, WaterCoverageBasisPoints::max)};

  SplitMix64 palette{
      derive_planet_stream_seed(planet_seed, PlanetDescriptorStream::palette)};
  const auto& selected_palette = kPalettes[palette.bounded(kPalettes.size())];

  return PlanetDescriptor{
      .seed = planet_seed,
      .id = PlanetId{planet_seed.value},
      .display_name = generated_name(planet_seed),
      .radius = radius,
      .surface_gravity = gravity,
      .atmosphere_class = atmosphere_class,
      .atmosphere_pressure = pressure,
      .terrain_character = terrain_character,
      .water_coverage = water_coverage,
      .palette = selected_palette,
  };
}

auto atmosphere_class_name(AtmosphereClass value) noexcept -> std::string_view {
  switch (value) {
    case AtmosphereClass::airless: return "airless";
    case AtmosphereClass::tenuous: return "tenuous";
    case AtmosphereClass::temperate: return "temperate";
    case AtmosphereClass::dense: return "dense";
  }
  return "unknown";
}

auto terrain_character_name(TerrainCharacter value) noexcept
    -> std::string_view {
  switch (value) {
    case TerrainCharacter::oceanic: return "oceanic";
    case TerrainCharacter::plains: return "plains";
    case TerrainCharacter::rugged: return "rugged";
    case TerrainCharacter::alpine: return "alpine";
    case TerrainCharacter::volcanic: return "volcanic";
  }
  return "unknown";
}

auto palette_family_name(PaletteFamily value) noexcept -> std::string_view {
  switch (value) {
    case PaletteFamily::verdant: return "verdant";
    case PaletteFamily::arid: return "arid";
    case PaletteFamily::glacial: return "glacial";
    case PaletteFamily::volcanic: return "volcanic";
    case PaletteFamily::alien: return "alien";
  }
  return "unknown";
}

auto planet_descriptor_json(const PlanetDescriptor& descriptor) -> std::string {
  return std::format("{{\n"
                     "  \"schema_version\": 1,\n"
                     "  \"generator_version\": {},\n"
                     "  \"planet_seed\": \"{}\",\n"
                     "  \"planet_id\": \"planet-{:016x}\",\n"
                     "  \"display_name\": \"{}\",\n"
                     "  \"radius_km\": {},\n"
                     "  \"surface_gravity_milli_g\": {},\n"
                     "  \"atmosphere\": {{\"class\": \"{}\", "
                     "\"pressure_millibars\": {}}},\n"
                     "  \"terrain_character\": \"{}\",\n"
                     "  \"water_coverage_basis_points\": {},\n"
                     "  \"palette\": {{\n"
                     "    \"family\": \"{}\",\n"
                     "    \"atmosphere\": \"{}\",\n"
                     "    \"deep_water\": \"{}\",\n"
                     "    \"shallow_water\": \"{}\",\n"
                     "    \"lowland\": \"{}\",\n"
                     "    \"highland\": \"{}\",\n"
                     "    \"peak\": \"{}\"\n"
                     "  }}\n"
                     "}}\n",
                     kPlanetGeneratorVersion, descriptor.seed.value,
                     descriptor.id.value, descriptor.display_name,
                     descriptor.radius.value, descriptor.surface_gravity.value,
                     atmosphere_class_name(descriptor.atmosphere_class),
                     descriptor.atmosphere_pressure.value,
                     terrain_character_name(descriptor.terrain_character),
                     descriptor.water_coverage.value,
                     palette_family_name(descriptor.palette.family),
                     color_hex(descriptor.palette.atmosphere),
                     color_hex(descriptor.palette.deep_water),
                     color_hex(descriptor.palette.shallow_water),
                     color_hex(descriptor.palette.lowland),
                     color_hex(descriptor.palette.highland),
                     color_hex(descriptor.palette.peak));
}

} // namespace apsis_drift
