#include "apsis_drift/planet_optics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <source_location>
#include <string_view>

#include "apsis_drift/atmospheric_flight.hpp"

namespace {
using namespace apsis_drift;
using Error = PlanetOpticsError;
int checks{}, failures{};
auto check(bool value, std::string_view label,
           std::source_location at = std::source_location::current()) -> void {
  ++checks;
  if (value) return;
  ++failures;
  std::cerr << "FAIL line " << at.line() << ": " << label << '\n';
}
template <class T, class E>
auto required(const std::expected<T, E>& value,
              std::source_location at = std::source_location::current()) -> T {
  if (!value) {
    std::cerr << "Fixture refused line " << at.line() << '\n';
    std::exit(1);
  }
  return *value;
}
template <class T>
auto refused(const std::expected<T, Error>& value, Error error) -> bool {
  return !value && value.error() == error;
}
auto pixel_hash(const PlanetOpticsField& field) -> std::uint64_t {
  std::uint64_t hash{14'695'981'039'346'656'037ULL};
  for (auto byte : field.density) {
    hash ^= byte;
    hash *= 1'099'511'628'211ULL;
  }
  return hash;
}
auto generated_fields() -> void {
  std::array<bool, 4> kinds{};
  for (std::uint64_t seed = 0; seed < 32; ++seed) {
    const auto world = required(generate_physical_origin_system(Seed{seed}, 2));
    const auto original = world;
    for (const auto& planet : world.catalog.planets) {
      const auto recipe =
          required(generate_planet_optics_recipe(world, planet.descriptor.id));
      const auto before =
          required(resolve_planet_ambient_environment(world, recipe.ambient));
      const auto field = required(resolve_planet_optics(world, recipe));
      kinds[static_cast<std::size_t>(field.kind)] = true;
      check(validate_planet_optics_field(field).has_value() &&
                required(resolve_planet_optics(world, recipe)) == field,
            "owned optical field is valid and exactly repeatable");
      check(before == required(resolve_planet_ambient_environment(
                          world, recipe.ambient)) &&
                before.visibility == field.kind,
            "optical stream leaves ambient truth unchanged");
      check(field.opacity_cap_basis_points <=
                    before.maximum_obscuration_basis_points &&
                static_cast<std::uint32_t>(field.opacity_cap_basis_points) *
                        4 <=
                    static_cast<std::uint32_t>(
                        before.maximum_obscuration_basis_points) *
                        3,
            "selected actual ray-opacity cap stays below potential");
      const auto atmosphere =
          required(resolve_atmospheric_flight_profile(planet.descriptor));
      check(field.upper_height_metres <=
                atmosphere.space_boundary_altitude_metres,
            "optical band never exceeds actual atmospheric edge");
      for (std::size_t x = 0; x < kPlanetOpticsWidth; ++x)
        check(field.density[x] == field.density[0] &&
                  field.density[(kPlanetOpticsHeight - 1) * kPlanetOpticsWidth +
                                x] == field.density[(kPlanetOpticsHeight - 1) *
                                                    kPlanetOpticsWidth],
              "both pole rows are longitude independent");
      if (field.kind == AtmosphericVisibilityKind::airless ||
          field.kind == AtmosphericVisibilityKind::clear)
        check(field.opacity_cap_basis_points == 0 &&
                  std::ranges::all_of(field.density,
                                      [](auto v) { return v == 0; }),
              "airless and clear bodies have no optical field");
      else
        check(field.upper_height_metres > field.lower_height_metres &&
                  std::ranges::any_of(field.density,
                                      [](auto v) { return v != 0; }),
              "generated cloud/dust has a nonempty broad density field");
    }
    check(world == original, "optical generation cannot change physical owner");
  }
  check(std::ranges::all_of(kinds, [](bool value) { return value; }),
        "bounded generated references cover cloud dust clear and airless");
}
auto sampling_and_refusals() -> void {
  const auto world = required(generate_physical_origin_system(Seed{42}, 2));
  const auto home = generate_origin_home_planet(world.catalog.seed).id;
  const auto recipe = required(generate_planet_optics_recipe(world, home));
  const auto field = required(resolve_planet_optics(world, recipe));
  // Independent Python byte-oriented seed/fixed-point interpolation oracle.
  check(field.kind == AtmosphericVisibilityKind::cloud &&
            field.opacity_cap_basis_points == 369 &&
            pixel_hash(field) == 3'659'503'612'776'873'746ULL &&
            field.density[0] == 40 && field.density[511] == 191 &&
            field.density[1024] == 94 && field.density[1500] == 146,
        "home optical integer atlas and strength golden");
  check(field.lower_height_metres == 2'000 &&
            field.upper_height_metres == 6'000 &&
            field.tint == Rgb8{214, 220, 225},
        "cloud reference band and bounded tint are versioned");
  constexpr double pi = std::numbers::pi;
  const double middle =
      (field.lower_height_metres + field.upper_height_metres) / 2;
  for (const double latitude : {-pi / 2, -.7, 0.0, .6, pi / 2}) {
    const auto left =
        required(sample_planet_optics_density(field, latitude, -pi, middle));
    const auto right =
        required(sample_planet_optics_density(field, latitude, pi, middle));
    check(left == right && left >= 0 && left <= 1,
          "periodic seam samples agree exactly and remain bounded");
    check(std::abs(required(sample_planet_optics_density(field, latitude,
                                                         -pi + 1e-8, middle)) -
                   required(sample_planet_optics_density(
                       field, latitude, pi - 1e-8, middle))) < 1e-6,
          "longitude seam is continuous from either side");
  }
  for (const double latitude : {-pi / 2, pi / 2})
    check(
        required(sample_planet_optics_density(field, latitude, -2, middle)) ==
            required(sample_planet_optics_density(field, latitude, 2, middle)),
        "pole density does not depend on longitude");
  auto authored = field;
  authored.density.fill(128);
  check(std::abs(
            required(sample_planet_optics_density(authored, .3, -.2, middle)) -
            128.0 / 255) < 1e-15 &&
            std::abs(required(sample_planet_optics_density(authored, .3, -.2,
                                                           3'000)) -
                     128.0 / 255 * .75) < 1e-15,
        "declared constant reference independently checks vertical taper");
  authored.kind = AtmosphericVisibilityKind::dust;
  authored.lower_height_metres = 0;
  authored.upper_height_metres = 3'000;
  check(validate_planet_optics_field(authored).has_value() &&
            required(sample_planet_optics_density(authored, 0, 0, 1'500)) ==
                128.0 / 255,
        "authored dust reference uses its distinct lower band");
  for (double height : {-100.0, 2'000.0, 6'000.0, 1e12})
    check(required(sample_planet_optics_density(field, 0, 0, height)) == 0,
          "out-of-band rays never invent optical density");
  for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity()}) {
    check(refused(sample_planet_optics_density(field, invalid, 0, middle),
                  Error::invalid_sample) &&
              refused(sample_planet_optics_density(field, 0, invalid, middle),
                      Error::invalid_sample) &&
              refused(sample_planet_optics_density(field, 0, 0, invalid),
                      Error::invalid_sample),
          "nonfinite angles and height refuse before sampling");
    auto bad = field;
    bad.upper_height_metres = invalid;
    check(!validate_planet_optics_field(bad), "nonfinite band refuses");
  }
  check(!sample_planet_optics_density(field, pi, 0, middle) &&
            !sample_planet_optics_density(field, 0, pi + .01, middle),
        "angular bounds refuse instead of wrapping invalid latitude");
  auto bad = field;
  bad.kind = static_cast<AtmosphericVisibilityKind>(255);
  check(!validate_planet_optics_field(bad), "unknown field kind refuses");
  bad = field;
  bad.density[1] = static_cast<std::uint8_t>(bad.density[0] ^ 1U);
  check(!sample_planet_optics_density(bad, 0, 0, middle),
        "incoherent pole refuses before a different pixel can be sampled");
  bad = field;
  bad.opacity_cap_basis_points = 7'501;
  check(!validate_planet_optics_field(bad), "optical strength bound refuses");
  bad = field;
  bad.lower_height_metres = -1;
  check(!validate_planet_optics_field(bad), "negative layer bound refuses");
  bad = field;
  bad.lower_height_metres = bad.upper_height_metres;
  check(!validate_planet_optics_field(bad), "empty cloudy layer refuses");
  bad = field;
  bad.kind = AtmosphericVisibilityKind::clear;
  check(!validate_planet_optics_field(bad),
        "clear cannot hide a density layer");
  auto wrong = recipe;
  wrong.version = 0;
  check(
      refused(resolve_planet_optics(world, wrong), Error::unsupported_version),
      "unknown optics version refuses");
  wrong = recipe;
  ++wrong.ambient.system.value;
  check(refused(resolve_planet_optics(world, wrong), Error::invalid_owner),
        "forged system ownership refuses");
  wrong = recipe;
  wrong.ambient.physical_catalog = 1;
  wrong.ambient.ephemeris = 1;
  check(refused(resolve_planet_optics(world, wrong), Error::invalid_owner),
        "mixed physical owner refuses");
  check(refused(generate_planet_optics_recipe(world, home, 2),
                Error::unsupported_version) &&
            refused(generate_planet_optics_recipe(world, PlanetId{}),
                    Error::invalid_owner),
        "unsupported generator and unknown body refuse");
  check(required(resolve_planet_optics(world, recipe)) == field,
        "failed queries cannot change original optical field");
}
} // namespace
int main() {
  generated_fields();
  sampling_and_refusals();
  std::cout << checks << " planet optics checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
