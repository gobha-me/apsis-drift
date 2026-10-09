#include "apsis_drift/planet_ambient.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <source_location>
#include <unordered_set>

#include "apsis_drift/planet_rotation.hpp"

namespace {
using namespace apsis_drift;
using Error = PlanetAmbientError;
using Json = nlohmann::ordered_json;
int failures{}, checks{};
auto check(bool ok, std::string_view message,
           std::source_location at = std::source_location::current()) -> void {
  ++checks;
  if (ok) return;
  ++failures;
  std::cerr << "FAIL line " << at.line() << ": " << message << '\n';
}
template <typename T, typename E>
auto required(const std::expected<T, E>& r,
              std::source_location at = std::source_location::current()) -> T {
  if (!r) {
    std::cerr << "Fixture refused line " << at.line() << '\n';
    std::exit(1);
  }
  return *r;
}
template <typename T>
auto refused(const std::expected<T, Error>& r, Error error) -> bool {
  return !r && r.error() == error;
}
auto word(std::uint64_t& hash, std::uint64_t value) -> void {
  for (unsigned i = 0; i < 8; ++i) {
    hash ^= (value >> (i * 8)) & 255U;
    hash *= 1'099'511'628'211ULL;
  }
}
auto hash_environment(const PlanetAmbientEnvironment& e) -> std::uint64_t {
  std::uint64_t h{14'695'981'039'346'656'037ULL};
  for (const auto value : std::array<std::uint64_t, 14>{
           e.recipe.planet.value, e.surface_gravity.value,
           static_cast<std::uint64_t>(e.atmosphere_class),
           e.surface_pressure.value, e.minimum_temperature_millikelvin,
           e.maximum_temperature_millikelvin, e.radiation_nanosieverts_per_hour,
           e.electric_field_volts_per_metre, e.gust_millimetres_per_second,
           e.ground_acceleration_mm_per_second2,
           static_cast<std::uint64_t>(e.visibility),
           e.maximum_obscuration_basis_points, e.recipe.version,
           e.recipe.system_seed.value})
    word(h, value);
  return h;
}

auto ownership_and_persistence() -> void {
  const auto world = required(generate_physical_origin_system(Seed{42}, 2));
  const auto snapshot = required(generate_physical_origin_system(Seed{42}, 2));
  const auto planet = generate_origin_home_planet(world.catalog.seed).id;
  const auto recipe = required(generate_planet_ambient_recipe(world, planet));
  const auto e = required(resolve_planet_ambient_environment(world, recipe));
  const auto record =
      required(encode_planet_ambient_recipe_json(world, recipe));
  check(hash_environment(e) == 788099846072397699ULL,
        "home integer golden from independent byte-oriented oracle");
  check(record ==
            "{\"provider\":\"planet_ambient\",\"version\":1,\"physical_"
            "catalog\":2,"
            "\"ephemeris\":2,\"system\":677859337506523986,"
            "\"system_seed\":677859337506523986,\"catalog_kind\":1,"
            "\"origin_universe_seed\":42,\"planet\":4853608806881724904}\n",
        "closed recipe byte golden");
  check(required(decode_planet_ambient_recipe_json(world, record)) == recipe,
        "recipe-only persistence roundtrip");
  check(required(resolve_planet_ambient_environment(
            world,
            required(decode_planet_ambient_recipe_json(world, record)))) == e,
        "saved recipe reproduces full immutable truth");
  check(record.size() < 400 && record.find("temperature") == std::string::npos,
        "record retains recipe, not generated catalog");
  check(world == snapshot, "generation and persistence leave owner unchanged");
  check(e.minimum_temperature_millikelvin >= 273'000 &&
            e.maximum_temperature_millikelvin <= 313'000 &&
            e.radiation_nanosieverts_per_hour <= 250 &&
            e.gust_millimetres_per_second <= 10'000 &&
            e.ground_acceleration_mm_per_second2 <= 50,
        "authored home mild bounds, no procedural substitution");
  check(e.surface_gravity ==
                world.catalog.planets.front().descriptor.surface_gravity &&
            e.surface_pressure ==
                world.catalog.planets.front().descriptor.atmosphere_pressure,
        "existing home gravity/pressure authority");
  const auto old = required(generate_physical_origin_system(Seed{42}, 1));
  const auto old_r = required(generate_planet_ambient_recipe(old, planet));
  check(hash_environment(required(resolve_planet_ambient_environment(
            old, old_r))) == hash_environment(e),
        "catalog1/2 preserve ambient mapping");
  check(refused(resolve_planet_ambient_environment(old, recipe),
                Error::owner_mismatch),
        "version2 owner cannot be silently read as version1");
  check(refused(generate_planet_ambient_recipe(world, planet, 0),
                Error::unsupported_version),
        "unsupported generator refused before generation");
  check(refused(generate_planet_ambient_recipe(world, PlanetId{0}),
                Error::unknown_planet),
        "missing body refused");
  for (unsigned field = 0; field < 7; ++field) {
    auto bad = recipe;
    switch (field) {
      case 0: ++bad.version; break;
      case 1: ++bad.system.value; break;
      case 2: ++bad.system_seed.value; break;
      case 3: bad.catalog_kind = static_cast<LocalSystemKind>(255); break;
      case 4: bad.origin_universe_seed.reset(); break;
      case 5: ++bad.origin_universe_seed->value; break;
      case 6: bad.ephemeris = 1; break;
    }
    check(!resolve_planet_ambient_environment(world, bad),
          "forged owner/version refused");
    check(!encode_planet_ambient_recipe_json(world, bad),
          "forged recipe cannot be persisted");
  }
  auto bad_world = world;
  ++bad_world.stellar_gm_km3_per_second2;
  check(refused(resolve_planet_ambient_environment(bad_world, recipe),
                Error::invalid_context),
        "complete world validated, not matching numeric ID only");
  const auto other = required(generate_physical_origin_system(Seed{43}, 2));
  check(!decode_planet_ambient_recipe_json(other, record),
        "record refuses another universe");
  const auto procedural =
      required(generate_physical_local_system(world.catalog.seed, 2));
  check(!resolve_planet_ambient_environment(procedural, recipe),
        "procedural namesake cannot impersonate authored home");

  const auto j = Json::parse(record);
  for (const auto key : {"version", "physical_catalog", "ephemeris", "system",
                         "system_seed", "catalog_kind", "planet"}) {
    for (const auto& invalid : std::array<Json, 7>{
             -1, 1.0, true, "1", nullptr, Json::array(), Json::object()}) {
      auto corrupt = j;
      corrupt[key] = invalid;
      check(refused(decode_planet_ambient_recipe_json(world, corrupt.dump()),
                    Error::invalid_recipe_document),
            "strict integer types");
    }
    auto missing = j;
    missing.erase(key);
    check(!decode_planet_ambient_recipe_json(world, missing.dump()),
          "missing field refused");
  }
  for (const auto key :
       {"version", "physical_catalog", "ephemeris", "catalog_kind"}) {
    auto corrupt = j;
    corrupt[key] = std::numeric_limits<std::uint64_t>::max();
    check(!decode_planet_ambient_recipe_json(world, corrupt.dump()),
          "integer narrowing refused");
  }
  for (const auto& text : std::array<std::string, 9>{
           "", "{}", "[]", "null", "{\"version\":NaN}",
           std::string(kMaximumPlanetAmbientRecipeBytes + 1, ' '),
           "{\"x\":[[[0]]]}", "{\"version\":1," + record.substr(1),
           "{\"provider\":\"planet_ambient\",\"provider\":\"planet_ambient\"}"})
    check(!decode_planet_ambient_recipe_json(world, text),
          "bounded malformed/duplicate refusal");
  for (const auto& extra : {"catalog", "temperature", "unexpected"}) {
    auto corrupt = j;
    corrupt[extra] = 1;
    check(!decode_planet_ambient_recipe_json(world, corrupt.dump()),
          "closed recipe keys");
  }
  std::cout << "home42 " << hash_environment(e) << '\n' << record;
  std::cout << "reference_home " << world.catalog.seed.value << ' '
            << planet.value << ' ' << e.surface_gravity.value << ' '
            << static_cast<unsigned>(e.atmosphere_class) << ' '
            << e.surface_pressure.value << ' ' << hash_environment(e) << '\n';
}

auto reference_bounds() -> void {
  const auto world = required(generate_physical_origin_system(Seed{42}, 2));
  auto e = required(resolve_planet_ambient_environment(
      world, required(generate_planet_ambient_recipe(
                 world, generate_origin_home_planet(world.catalog.seed).id))));
  e.surface_gravity.value = 1'800;
  e.atmosphere_class = AtmosphereClass::dense;
  e.surface_pressure.value = 2'500;
  e.minimum_temperature_millikelvin = 30'000;
  e.maximum_temperature_millikelvin = 1'400'000;
  e.radiation_nanosieverts_per_hour = 1'000'000'000;
  e.electric_field_volts_per_metre = 100'000;
  e.gust_millimetres_per_second = 200'000;
  e.ground_acceleration_mm_per_second2 = 20'000;
  e.visibility = AtmosphericVisibilityKind::cloud;
  e.maximum_obscuration_basis_points = 10'000;
  check(bool(validate_planet_ambient_environment(e)),
        "all accepted upper/lower bounds coexist");
  for (unsigned field = 0; field < 11; ++field) {
    auto bad = e;
    switch (field) {
      case 0: bad.surface_gravity.value = 1'801; break;
      case 1: bad.minimum_temperature_millikelvin = 29'999; break;
      case 2: ++bad.maximum_temperature_millikelvin; break;
      case 3: bad.minimum_temperature_millikelvin = 1'400'001; break;
      case 4: ++bad.radiation_nanosieverts_per_hour; break;
      case 5: ++bad.electric_field_volts_per_metre; break;
      case 6: ++bad.gust_millimetres_per_second; break;
      case 7: ++bad.ground_acceleration_mm_per_second2; break;
      case 8: ++bad.maximum_obscuration_basis_points; break;
      case 9: bad.atmosphere_class = static_cast<AtmosphereClass>(255); break;
      case 10:
        bad.visibility = static_cast<AtmosphericVisibilityKind>(255);
        break;
    }
    check(refused(validate_planet_ambient_environment(bad),
                  Error::invalid_environment),
          "overflow/range/enum/ordering refused");
  }
  const std::array classes{AtmosphereClass::airless, AtmosphereClass::tenuous,
                           AtmosphereClass::temperate, AtmosphereClass::dense};
  const std::array<std::uint16_t, 4> low{0, 1, 250, 1'500},
      high{0, 249, 1'499, 2'500};
  for (std::size_t i = 0; i < classes.size(); ++i) {
    auto ref = e;
    ref.atmosphere_class = classes[i];
    ref.visibility = i == 0 ? AtmosphericVisibilityKind::airless
                            : AtmosphericVisibilityKind::clear;
    ref.maximum_obscuration_basis_points = 0;
    if (i == 0) {
      ref.electric_field_volts_per_metre = 0;
      ref.gust_millimetres_per_second = 0;
    }
    for (const auto pressure : {low[i], high[i]}) {
      ref.surface_pressure.value = pressure;
      check(bool(validate_planet_ambient_environment(ref)),
            "pressure classification endpoints");
    }
    ref.surface_pressure.value = static_cast<std::uint16_t>(high[i] + 1);
    check(!validate_planet_ambient_environment(ref),
          "pressure outside selected class refused");
    if (i) {
      ref.surface_pressure.value = static_cast<std::uint16_t>(low[i] - 1);
      check(!validate_planet_ambient_environment(ref),
            "lower pressure boundary refused");
    }
  }
  e.atmosphere_class = AtmosphereClass::airless;
  e.surface_pressure.value = 0;
  e.electric_field_volts_per_metre = 0;
  e.gust_millimetres_per_second = 0;
  e.visibility = AtmosphericVisibilityKind::airless;
  e.maximum_obscuration_basis_points = 0;
  check(bool(validate_planet_ambient_environment(e)),
        "airless can have radiation and geologic motion");
  for (unsigned field = 0; field < 4; ++field) {
    auto bad = e;
    switch (field) {
      case 0: bad.electric_field_volts_per_metre = 1; break;
      case 1: bad.gust_millimetres_per_second = 1; break;
      case 2: bad.visibility = AtmosphericVisibilityKind::dust; break;
      case 3: bad.maximum_obscuration_basis_points = 1; break;
    }
    check(!validate_planet_ambient_environment(bad),
          "airless atmospheric hazard contradictions");
  }
  e.atmosphere_class = AtmosphereClass::tenuous;
  e.surface_pressure.value = 1;
  e.visibility = AtmosphericVisibilityKind::dust;
  e.maximum_obscuration_basis_points = 1;
  check(bool(validate_planet_ambient_environment(e)),
        "tenuous dust without cloud deck");
  e.visibility = AtmosphericVisibilityKind::cloud;
  check(!validate_planet_ambient_environment(e), "tenuous cloud deck refused");
  check(atmospheric_visibility_name(
            static_cast<AtmosphericVisibilityKind>(255)) == "unknown",
        "invalid diagnostic name is explicit");
}

auto population_and_streams() -> void {
  std::array<bool, 4> atmospheres{};
  bool cold{}, hot{}, radiation{}, electrical{}, gust{}, seismic{}, cloud{},
      dust{};
  std::uint64_t hash{14'695'981'039'346'656'037ULL};
  for (std::uint64_t seed = 0; seed < 96; ++seed) {
    const auto world = required(generate_physical_local_system(Seed{seed}, 2));
    const auto before = required(generate_physical_local_system(Seed{seed}, 2));
    for (const auto& p : world.catalog.planets) {
      const auto original = planet_descriptor_json(p.descriptor);
      const auto rotation =
          required(generate_planet_rotation_recipe(world, p.descriptor.id));
      const auto r =
          required(generate_planet_ambient_recipe(world, p.descriptor.id));
      const auto e = required(resolve_planet_ambient_environment(world, r));
      check(bool(validate_planet_ambient_environment(e)),
            "generated environment coherent");
      check(e.surface_gravity == p.descriptor.surface_gravity &&
                e.surface_pressure == p.descriptor.atmosphere_pressure,
            "existing descriptor fields unchanged");
      check(required(resolve_planet_ambient_environment(
                world, required(decode_planet_ambient_recipe_json(
                           world, required(encode_planet_ambient_recipe_json(
                                      world, r)))))) == e,
            "all ordinary recipe records regenerate exactly");
      const auto outdoor = required(evaluate_ambient_surface_exposure(
          world, r, AmbientSurfaceOperation::outdoor));
      const auto landing = required(evaluate_ambient_surface_exposure(
          world, r, AmbientSurfaceOperation::landing));
      const auto transit = required(evaluate_ambient_surface_exposure(
          world, r, AmbientSurfaceOperation::atmospheric_transit));
      check(outdoor.environment == e && landing.environment == e &&
                transit.environment == e && outdoor.ground_motion_applies &&
                landing.ground_motion_applies && !transit.ground_motion_applies,
            "operation projection changes exposure, not generated truth");
      check(refused(evaluate_ambient_surface_exposure(
                        world, r, static_cast<AmbientSurfaceOperation>(255)),
                    Error::invalid_operation),
            "invalid operation refused");
      check(original == planet_descriptor_json(p.descriptor) &&
                rotation == required(generate_planet_rotation_recipe(
                                world, p.descriptor.id)),
            "ambient generation does not perturb descriptor or rotation");
      std::unordered_set<std::uint64_t> streams;
      for (std::uint64_t stream = 1; stream <= 14; ++stream)
        streams.insert(
            derive_planet_stream_seed(
                p.descriptor.seed, static_cast<PlanetDescriptorStream>(stream))
                .value);
      check(streams.size() == 14, "named old/new seed streams are disjoint");
      atmospheres[static_cast<std::size_t>(e.atmosphere_class)] = true;
      cold |= e.minimum_temperature_millikelvin < 100'000;
      hot |= e.maximum_temperature_millikelvin > 1'000'000;
      radiation |= e.radiation_nanosieverts_per_hour > 100'000'000;
      electrical |= e.electric_field_volts_per_metre > 50'000;
      gust |= e.gust_millimetres_per_second > 100'000;
      seismic |= e.ground_acceleration_mm_per_second2 > 10'000;
      cloud |= e.visibility == AtmosphericVisibilityKind::cloud;
      dust |= e.visibility == AtmosphericVisibilityKind::dust;
      word(hash, hash_environment(e));
      if (seed < 3 && p.orbit.ordinal == 0) {
        constexpr std::array<std::uint64_t, 3> expected{8018573801018913618ULL,
                                                        11439564530968859045ULL,
                                                        7347249332338612565ULL};
        check(hash_environment(e) == expected[seed],
              "ordinary integer golden from independent byte-oriented oracle");
        std::cout << "reference_ordinary " << seed << ' '
                  << p.descriptor.seed.value << ' ' << e.surface_gravity.value
                  << ' ' << static_cast<unsigned>(e.atmosphere_class) << ' '
                  << e.surface_pressure.value << ' '
                  << p.descriptor.water_coverage.value << ' '
                  << hash_environment(e) << '\n';
      }
    }
    check(world == before &&
              world == required(generate_physical_local_system(Seed{seed}, 2)),
          "whole catalog remains exact before/after ambient generation");
  }
  check(atmospheres == std::array{true, true, true, true} && cold && hot &&
            radiation && electrical && gust && seismic && cloud && dust,
        "ordinary bounded population represents all required environmental "
        "extremes");
  std::cout << "population96 " << hash << '\n';
  check(hash == 14535409883290593405ULL,
        "full ordinary population integer compatibility golden");
}
} // namespace
auto main() -> int {
  ownership_and_persistence();
  reference_bounds();
  population_and_streams();
  std::cout << "planet ambient " << checks << " checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
