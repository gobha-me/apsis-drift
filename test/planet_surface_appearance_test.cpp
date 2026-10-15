#include "apsis_drift/planet_surface_appearance.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <source_location>
#include <string_view>

namespace {
using namespace apsis_drift;
using Error = PlanetSurfaceAppearanceError;
int checks{}, failures{};
auto check(bool value, std::string_view label,
           std::source_location at = std::source_location::current()) -> void {
  ++checks;
  if (value) return;
  ++failures;
  std::cerr << "FAIL line " << at.line() << ": " << label << '\n';
}
template <class T>
auto refused(const std::expected<T, Error>& value, Error error) -> bool {
  return !value && value.error() == error;
}

// Mutable authored inputs allow malformed immutable descriptors without casts.
struct Inputs {
  Seed seed{42};
  PlanetId id{42};
  PlanetRadiusKm radius{5'000};
  SurfaceGravityMilliG gravity{1'000};
  AtmosphereClass atmosphere{AtmosphereClass::temperate};
  AtmospherePressureMillibars pressure{1'000};
  TerrainCharacter terrain{TerrainCharacter::plains};
  WaterCoverageBasisPoints water{5'000};
  PaletteFamily family{PaletteFamily::verdant};
  auto descriptor() const -> PlanetDescriptor {
    return {seed,
            id,
            "Authored reference",
            radius,
            gravity,
            atmosphere,
            pressure,
            terrain,
            water,
            PlanetPalette{family, {}, {}, {}, {}, {}, {}}};
  }
};

auto profiles_and_purity() -> void {
  constexpr std::array families{PaletteFamily::verdant, PaletteFamily::arid,
                                PaletteFamily::glacial, PaletteFamily::volcanic,
                                PaletteFamily::alien};
  constexpr std::array expected{
      PlanetSurfaceAppearance{1, 4'500, 8'500, 8'800, 3'500},
      PlanetSurfaceAppearance{1, 5'500, 6'500, 9'800, 1'800},
      PlanetSurfaceAppearance{1, 7'200, 3'000, 3'800, 6'500},
      PlanetSurfaceAppearance{1, 4'300, 12'500, 8'500, 4'000},
      PlanetSurfaceAppearance{1, 5'000, 9'000, 8'200, 4'500}};
  for (std::size_t i = 0; i < families.size(); ++i) {
    Inputs input;
    input.family = families[i];
    const auto planet = input.descriptor();
    const auto before = planet_descriptor_json(planet);
    const auto profile = resolve_planet_surface_appearance(planet);
    check(profile && *profile == expected[i], "palette response golden");
    check(profile && validate_planet_surface_appearance(*profile).has_value(),
          "every selected response satisfies coefficient bounds");
    check(resolve_planet_surface_appearance(planet) == profile &&
              planet_descriptor_json(planet) == before,
          "resolution is repeatable and cannot mutate descriptor");
    input.seed = Seed{999};
    input.id = PlanetId{999};
    input.water = WaterCoverageBasisPoints{0};
    input.terrain = TerrainCharacter::alpine;
    check(resolve_planet_surface_appearance(input.descriptor()) == profile,
          "visual response does not infer material from hydrology or relief");
  }
  for (const auto seed : {Seed{0}, Seed{42}, Seed{999}}) {
    const auto planet = generate_planet_descriptor(seed);
    check(resolve_planet_surface_appearance(planet).has_value(),
          "ordinary generated descriptor is accepted without new streams");
  }
}
auto descriptor_refusals() -> void {
  const auto refuses = [](const Inputs& input) {
    return refused(resolve_planet_surface_appearance(input.descriptor()),
                   Error::invalid_owner);
  };
  for (const auto version : {0U, 2U, std::numeric_limits<std::uint32_t>::max()})
    check(refused(
              resolve_planet_surface_appearance(Inputs{}.descriptor(), version),
              Error::unsupported_version),
          "unsupported presentation version refuses");
  Inputs input;
  input.id = PlanetId{43};
  check(resolve_planet_surface_appearance(input.descriptor()).has_value(),
        "authored identity is not regenerated from seed by material resolver");
  for (const auto radius :
       {0U, PlanetRadiusKm::min - 1, PlanetRadiusKm::max + 1,
        std::numeric_limits<std::uint32_t>::max()}) {
    input = Inputs{};
    input.radius = PlanetRadiusKm{radius};
    check(refuses(input), "invalid radius refuses");
  }
  for (const auto gravity : {0U, SurfaceGravityMilliG::min - 1U,
                             SurfaceGravityMilliG::max + 1U, 65'535U}) {
    input = Inputs{};
    input.gravity = SurfaceGravityMilliG{static_cast<std::uint16_t>(gravity)};
    check(refuses(input), "invalid gravity refuses");
  }
  for (const auto water : {10'001U, 65'535U}) {
    input = Inputs{};
    input.water = WaterCoverageBasisPoints{static_cast<std::uint16_t>(water)};
    check(refuses(input), "invalid water coverage refuses");
  }
  input = Inputs{};
  input.family = static_cast<PaletteFamily>(255);
  check(refuses(input), "illegal palette refuses");
  input = Inputs{};
  input.terrain = static_cast<TerrainCharacter>(255);
  check(refuses(input), "illegal terrain refuses");
  input = Inputs{};
  input.atmosphere = static_cast<AtmosphereClass>(255);
  check(refuses(input), "illegal atmosphere refuses");
  constexpr std::array classes{
      AtmosphereClass::airless, AtmosphereClass::tenuous,
      AtmosphereClass::temperate, AtmosphereClass::dense};
  constexpr std::array<std::uint16_t, 4> lower{0, 1, 250, 1'500};
  constexpr std::array<std::uint16_t, 4> upper{0, 249, 1'499, 2'500};
  for (std::size_t i = 0; i < classes.size(); ++i) {
    for (const auto pressure : {lower[i], upper[i]}) {
      input = Inputs{};
      input.atmosphere = classes[i];
      input.pressure = AtmospherePressureMillibars{pressure};
      check(resolve_planet_surface_appearance(input.descriptor()).has_value(),
            "coherent atmosphere boundary accepts");
    }
    input.pressure =
        AtmospherePressureMillibars{static_cast<std::uint16_t>(upper[i] + 1)};
    check(refuses(input), "atmosphere pressure above class refuses");
    if (lower[i] > 0) {
      input.pressure =
          AtmospherePressureMillibars{static_cast<std::uint16_t>(lower[i] - 1)};
      check(refuses(input), "atmosphere pressure below class refuses");
    }
  }
  input = Inputs{};
  input.pressure = AtmospherePressureMillibars{65'535};
  check(refuses(input), "extreme pressure refuses");
  for (const auto radius : {PlanetRadiusKm::min, PlanetRadiusKm::max}) {
    input = Inputs{};
    input.radius = PlanetRadiusKm{radius};
    check(resolve_planet_surface_appearance(input.descriptor()).has_value(),
          "valid radius endpoints accept");
  }
}
auto coefficient_refusals() -> void {
  check(validate_planet_surface_appearance({1, 0, 0, 0, 0}).has_value() &&
            validate_planet_surface_appearance(
                {1, 10'000, 20'000, 10'000, 10'000})
                .has_value(),
        "inclusive coefficient endpoints accept");
  for (const auto version : {0U, 2U, std::numeric_limits<std::uint32_t>::max()})
    check(refused(validate_planet_surface_appearance({version, 0, 0, 0, 0}),
                  Error::unsupported_version),
          "unknown profile version refuses");
  constexpr std::array members{
      &PlanetSurfaceAppearance::albedo_scale_basis_points,
      &PlanetSurfaceAppearance::detail_contrast_basis_points,
      &PlanetSurfaceAppearance::roughness_basis_points,
      &PlanetSurfaceAppearance::specular_basis_points};
  constexpr std::array<std::uint16_t, 4> maxima{10'000, 20'000, 10'000, 10'000};
  for (std::size_t i = 0; i < members.size(); ++i)
    for (const auto value : {static_cast<std::uint16_t>(maxima[i] + 1),
                             std::numeric_limits<std::uint16_t>::max()}) {
      PlanetSurfaceAppearance profile{1, 4'500, 8'500, 8'800, 3'500};
      profile.*members[i] = value;
      check(refused(validate_planet_surface_appearance(profile),
                    Error::invalid_profile),
            "each invalid coefficient refuses independently");
    }
}
} // namespace
int main() {
  profiles_and_purity();
  descriptor_refusals();
  coefficient_refusals();
  std::cout << checks << " surface appearance checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
