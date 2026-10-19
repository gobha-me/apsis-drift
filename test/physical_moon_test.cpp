#include "apsis_drift/physical_moon.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
using namespace apsis_drift;
using Error = PhysicalMoonError;
int checks{}, failures{};
std::uint64_t fingerprint{14695981039346656037ULL};

auto check(bool value, std::string_view message) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Physical moon fixture refused");
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
template <class T>
auto refused(const std::expected<T, Error>& value, Error error) -> bool {
  return !value && value.error() == error;
}
auto word(std::uint64_t value) -> void {
  for (unsigned byte = 0; byte < 8; ++byte) {
    fingerprint ^= (value >> (byte * 8U)) & 255U;
    fingerprint *= 1099511628211ULL;
  }
}
auto bits(double value) -> std::uint64_t {
  return std::bit_cast<std::uint64_t>(value);
}
auto close(double actual, long double expected) -> bool {
  constexpr long double relative = 16 * std::numeric_limits<double>::epsilon();
  return std::isfinite(actual) && std::isfinite(expected) && expected > 0 &&
         std::abs(static_cast<long double>(actual) - expected) <=
             relative * expected;
}
auto inherited(const PhysicalMoonDescriptor& descriptor,
               const SecondaryBodyCatalog& catalog) -> void {
  const auto& source = catalog.bodies[0];
  check(descriptor.catalog == catalog &&
            descriptor.version == kPhysicalMoonRecipeVersion &&
            descriptor.body == source.node.id &&
            descriptor.parent == *source.node.parent &&
            descriptor.radius_metres == source.radius_metres &&
            descriptor.spin_period_ticks == source.spin_period_ticks &&
            descriptor.epoch_spin_phase_turns ==
                source.epoch_spin_phase_turns &&
            descriptor.tint == source.tint,
        "Descriptor retains complete source owner and exact moon fields");
  check(descriptor.body.kind == BodyKind::moon &&
            descriptor.parent.kind == BodyKind::planet &&
            !planet_id(descriptor.body),
        "Physical moon remains typed moon with a real planet parent");
  check(descriptor.atmosphere_pressure_millibars == 0 &&
            descriptor.water_coverage_basis_points == 0 &&
            bits(descriptor.spin_pole_system.x) == bits(0.0) &&
            bits(descriptor.spin_pole_system.y) == bits(0.0) &&
            bits(descriptor.spin_pole_system.z) == bits(1.0),
        "Recipe1 is explicitly dry airless with canonical system +Z pole");
  check(
      descriptor.density_kilograms_per_cubic_metre == 3000.0 &&
          descriptor.gravitational_constant_si == 6.67430e-11,
      "Density and nominal G are fixed recipe constants, not source inference");
  check(descriptor.terrain_seed == derive_seed(Seed{source.node.id.value},
                                               SeedDomain::moon_surface, 1),
        "Terrain stream binds exact moon identity and independent recipe1");
}
auto scalars(const PhysicalMoonProperties& value) -> void {
  const long double radius = value.descriptor.radius_metres;
  // Independent unfactored long-double reference, not the producer's
  // radius-square/radius-cube or intermediate binary64 volume expressions.
  const long double mass = 4.0L * std::numbers::pi_v<long double> * 3000.0L *
                           radius * radius * radius / 3.0L;
  const long double mu = 6.67430e-11L * mass;
  const long double gravity = 4.0L * std::numbers::pi_v<long double> *
                              6.67430e-11L * 3000.0L * radius / 3.0L;
  check(close(value.mass_kilograms, mass) &&
            close(value.gravitational_parameter_metres_cubed_per_second_squared,
                  mu) &&
            close(value.surface_gravity_metres_per_second_squared, gravity),
        "Mass, GM and surface gravity agree with independent SI sphere oracle");
  check(
      close(value.gravitational_parameter_metres_cubed_per_second_squared /
                value.mass_kilograms,
            6.67430e-11L) &&
          close(value.surface_gravity_metres_per_second_squared *
                    static_cast<double>(radius) * static_cast<double>(radius),
                value.gravitational_parameter_metres_cubed_per_second_squared),
      "GM/mass and g*radius^2 preserve dimensional gravity relations");
}
auto fixture(const PhysicalLocalSystem& owner) -> PhysicalMoonProperties {
  const auto catalog = need(generate_secondary_body_catalog(owner));
  const auto source_before = catalog;
  const auto hierarchy_before = need(make_body_hierarchy(catalog));
  const auto ephemerides_before =
      need(resolve_body_ephemerides(hierarchy_before, {25, 0.5}));
  const auto descriptor = need(
      generate_physical_moon_descriptor(catalog, catalog.bodies[0].node.id));
  inherited(descriptor, catalog);
  check(validate_physical_moon_descriptor(descriptor).has_value() &&
            descriptor == need(generate_physical_moon_descriptor(
                              catalog, catalog.bodies[0].node.id)),
        "Full descriptor rederivation is deterministic and authenticated");
  const auto value = need(resolve_physical_moon_properties(descriptor));
  check(value.descriptor == descriptor,
        "Resolved scalars own exact descriptor and complete source catalog");
  scalars(value);
  const auto hierarchy_after = need(make_body_hierarchy(catalog));
  check(catalog == source_before &&
            catalog == need(generate_secondary_body_catalog(owner)) &&
            hierarchy_after.system == hierarchy_before.system &&
            hierarchy_after.bodies == hierarchy_before.bodies &&
            hierarchy_after.catalog == hierarchy_before.catalog &&
            need(resolve_body_ephemerides(hierarchy_before, {25, 0.5})) ==
                ephemerides_before,
        "Physical extension leaves source catalog and same-tick hierarchy "
        "unchanged");
  for (const auto field : std::array<std::uint64_t, 15>{
           descriptor.version, descriptor.body.version,
           static_cast<std::uint64_t>(descriptor.body.kind),
           descriptor.body.value, descriptor.parent.value,
           descriptor.radius_metres, descriptor.spin_period_ticks,
           descriptor.epoch_spin_phase_turns, descriptor.terrain_seed.value,
           descriptor.tint.red, descriptor.tint.green, descriptor.tint.blue,
           bits(value.mass_kilograms),
           bits(value.gravitational_parameter_metres_cubed_per_second_squared),
           bits(value.surface_gravity_metres_per_second_squared)})
    word(field);
  return value;
}
auto invalid_candidates(const SecondaryBodyCatalog& catalog) -> void {
  const auto body = catalog.bodies[0].node.id;
  const auto descriptor =
      need(generate_physical_moon_descriptor(catalog, body));
  auto invalid_catalog = catalog;
  ++invalid_catalog.bodies[0].radius_metres;
  check(refused(generate_physical_moon_descriptor(invalid_catalog, body, 0),
                Error::unsupported_version) &&
            refused(generate_physical_moon_descriptor(catalog, body, 2),
                    Error::unsupported_version),
        "Unknown physical recipes refuse before source or scalar selection");
  check(refused(generate_physical_moon_descriptor(invalid_catalog, body),
                Error::invalid_catalog),
        "Forged source catalog cannot authorize a physical moon");
  for (const auto kind : {BodyKind::star, BodyKind::planet, BodyKind::minor,
                          static_cast<BodyKind>(255)}) {
    auto wrong = body;
    wrong.kind = kind;
    check(refused(generate_physical_moon_descriptor(invalid_catalog, wrong),
                  Error::invalid_body),
          "Wrong body kind refuses before catalog/scalar work");
  }
  auto wrong = body;
  ++wrong.version;
  check(refused(generate_physical_moon_descriptor(catalog, wrong),
                Error::invalid_body),
        "Unknown body identity version refused");
  wrong = body;
  wrong.value ^= 1;
  check(refused(generate_physical_moon_descriptor(catalog, wrong),
                Error::invalid_body),
        "Unlisted typed moon refused");
  const auto other = need(generate_secondary_body_catalog(
      need(generate_physical_origin_system(Seed{43}, 2))));
  check(other.bodies[0].node.id != body &&
            refused(generate_physical_moon_descriptor(other, body),
                    Error::invalid_body),
        "Moon identity from another genuine physical owner refused");
  for (unsigned field = 0; field < 19; ++field) {
    auto bad = descriptor;
    switch (field) {
      case 0: ++bad.parent.value; break;
      case 1: bad.parent.kind = BodyKind::minor; break;
      case 2: bad.radius_metres = 0; break;
      case 3:
        bad.radius_metres = std::numeric_limits<std::uint32_t>::max();
        break;
      case 4: ++bad.spin_period_ticks; break;
      case 5: bad.spin_period_ticks = 0; break;
      case 6: bad.epoch_spin_phase_turns ^= 1; break;
      case 7: bad.tint.red ^= 1; break;
      case 8: bad.terrain_seed.value ^= 1; break;
      case 9: bad.density_kilograms_per_cubic_metre = 0; break;
      case 10:
        bad.density_kilograms_per_cubic_metre =
            std::numeric_limits<double>::quiet_NaN();
        break;
      case 11: bad.gravitational_constant_si = -1; break;
      case 12:
        bad.gravitational_constant_si = std::numeric_limits<double>::infinity();
        break;
      case 13: bad.spin_pole_system.x = -0.0; break;
      case 14: bad.spin_pole_system.y = 0.1; break;
      case 15: bad.spin_pole_system.z = -1; break;
      case 16: bad.atmosphere_pressure_millibars = 1; break;
      case 17: bad.water_coverage_basis_points = 1; break;
      case 18: bad.density_kilograms_per_cubic_metre = 3001; break;
    }
    check(refused(validate_physical_moon_descriptor(bad),
                  Error::descriptor_mismatch) &&
              refused(resolve_physical_moon_properties(bad),
                      Error::descriptor_mismatch),
          "Tampered/nonfinite inherited or recipe field refuses before scalar "
          "math");
  }
  auto bad_source = descriptor;
  ++bad_source.catalog.owner.stellar_gm_km3_per_second2;
  check(
      refused(resolve_physical_moon_properties(bad_source),
              Error::invalid_catalog),
      "Retained owner catalog must authenticate even with matching moon words");
  auto bad_version = descriptor;
  bad_version.version = 0;
  bad_version.density_kilograms_per_cubic_metre =
      std::numeric_limits<double>::quiet_NaN();
  check(refused(resolve_physical_moon_properties(bad_version),
                Error::unsupported_version),
        "Unsupported descriptor version takes precedence over physical fields");
}
auto ownership() -> void {
  const auto retained = [] {
    const auto owner = need(generate_physical_origin_system(Seed{42}, 2));
    const auto catalog = need(generate_secondary_body_catalog(owner));
    return need(
        resolve_physical_moon_properties(need(generate_physical_moon_descriptor(
            catalog, catalog.bodies[0].node.id))));
  }();
  auto copied = retained;
  const auto moved = std::move(copied);
  check(
      validate_physical_moon_descriptor(moved.descriptor).has_value() &&
          moved == retained &&
          moved.descriptor.catalog.owner.catalog.planets.data() !=
              retained.descriptor.catalog.owner.catalog.planets.data(),
      "Scalar result survives source lifetime and owns deep copy/move catalog");
  scalars(moved);
}
} // namespace

int main() {
  static_assert(static_cast<std::uint64_t>(SeedDomain::moon_surface) == 17);
  try {
    const auto home =
        fixture(need(generate_physical_origin_system(Seed{42}, 2)));
    const auto legacy =
        fixture(need(generate_physical_origin_system(Seed{42}, 1)));
    check(home.descriptor.body == legacy.descriptor.body &&
              home.descriptor.terrain_seed == legacy.descriptor.terrain_seed &&
              bits(home.mass_kilograms) == bits(legacy.mass_kilograms) &&
              bits(home.surface_gravity_metres_per_second_squared) ==
                  bits(legacy.surface_gravity_metres_per_second_squared) &&
              home.descriptor.catalog.owner != legacy.descriptor.catalog.owner,
          "Compatible catalog versions retain source moon scalars without "
          "erasing owner version");
    for (const auto seed : {Seed{4}, Seed{43}, Seed{31337}}) {
      const auto value = fixture(need(generate_physical_local_system(seed, 2)));
      const long double radius_ratio =
          static_cast<long double>(value.descriptor.radius_metres) /
          home.descriptor.radius_metres;
      check(close(value.mass_kilograms / home.mass_kilograms,
                  radius_ratio * radius_ratio * radius_ratio) &&
                close(value.surface_gravity_metres_per_second_squared /
                          home.surface_gravity_metres_per_second_squared,
                      radius_ratio),
            "Fixed density preserves cubic mass and linear surface-gravity "
            "scaling");
    }
    const auto catalog = need(generate_secondary_body_catalog(
        need(generate_physical_origin_system(Seed{42}, 2))));
    invalid_candidates(catalog);
    ownership();
  } catch (const std::exception& error) {
    check(false, error.what());
  }
  check(fingerprint == 9590516388413047147ULL,
        "Physical moon recipe1 retains its scalar golden fingerprint");
  std::cout << "Physical moon fingerprint " << fingerprint << '\n';
  std::cout << checks << " physical moon checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
