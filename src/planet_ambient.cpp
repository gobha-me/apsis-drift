#include "apsis_drift/planet_ambient.hpp"

#include <array>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace apsis_drift {
namespace {
using Error = PlanetAmbientError;
using Json = nlohmann::ordered_json;
static_assert(kSeedDerivationVersion == 1 && kPlanetGeneratorVersion == 1 &&
              kLocalSystemGeneratorVersion == 1 &&
              kOriginHomePlanetGeneratorVersion == 1);

auto field(Seed planet, PlanetDescriptorStream stream, std::uint64_t ordinal)
    -> std::uint64_t {
  auto v = derive_seed(derive_planet_stream_seed(planet, stream),
                       SeedDomain::planet, ordinal)
               .value;
  v = (v ^ (v >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  v = (v ^ (v >> 27U)) * 0x94D049BB133111EBULL;
  return v ^ (v >> 31U);
}
auto bounded(Seed seed, PlanetDescriptorStream stream, std::uint64_t ordinal,
             std::uint32_t low, std::uint32_t high) -> std::uint32_t {
  return low + static_cast<std::uint32_t>(
                   field(seed, stream, ordinal) %
                   (static_cast<std::uint64_t>(high) - low + 1));
}
auto valid_recipe_shape(const PlanetAmbientRecipe& r)
    -> std::expected<void, Error> {
  if (r.version != kPlanetAmbientGeneratorVersion)
    return std::unexpected{Error::unsupported_version};
  if (!((r.physical_catalog == 1 && r.ephemeris == 1) ||
        (r.physical_catalog == 2 && r.ephemeris == 2)) ||
      r.system.value != r.system_seed.value ||
      (r.catalog_kind != LocalSystemKind::procedural &&
       r.catalog_kind != LocalSystemKind::origin_home) ||
      r.origin_universe_seed.has_value() !=
          (r.catalog_kind == LocalSystemKind::origin_home))
    return std::unexpected{Error::owner_mismatch};
  return {};
}
auto environment_for(const PlanetDescriptor& p, const PlanetAmbientRecipe& r,
                     bool home) -> PlanetAmbientEnvironment {
  using S = PlanetDescriptorStream;
  PlanetAmbientEnvironment e{.recipe = r,
                             .surface_gravity = p.surface_gravity,
                             .atmosphere_class = p.atmosphere_class,
                             .surface_pressure = p.atmosphere_pressure};
  const auto seed = p.seed;
  if (home) {
    e.minimum_temperature_millikelvin =
        bounded(seed, S::ambient_temperature, 1, 273'000, 283'000);
    e.maximum_temperature_millikelvin =
        bounded(seed, S::ambient_temperature, 2, 293'000, 313'000);
    e.radiation_nanosieverts_per_hour =
        bounded(seed, S::ambient_radiation, 1, 50, 250);
    e.electric_field_volts_per_metre =
        bounded(seed, S::ambient_electrical, 1, 0, 100);
    e.gust_millimetres_per_second =
        bounded(seed, S::ambient_turbulence, 1, 0, 10'000);
    e.ground_acceleration_mm_per_second2 =
        bounded(seed, S::ambient_ground_motion, 1, 0, 50);
  } else {
    const auto temperature = field(seed, S::ambient_temperature, 1) % 3;
    std::uint32_t spread{};
    if (temperature == 0) {
      e.minimum_temperature_millikelvin =
          bounded(seed, S::ambient_temperature, 2, 30'000, 179'000);
      spread = bounded(seed, S::ambient_temperature, 3, 1'000, 120'000);
    } else if (temperature == 1) {
      e.minimum_temperature_millikelvin =
          bounded(seed, S::ambient_temperature, 2, 180'000, 330'000);
      spread = bounded(seed, S::ambient_temperature, 3, 5'000, 160'000);
    } else {
      e.minimum_temperature_millikelvin =
          bounded(seed, S::ambient_temperature, 2, 400'000, 1'200'000);
      spread = bounded(seed, S::ambient_temperature, 3, 25'000, 200'000);
    }
    e.maximum_temperature_millikelvin =
        e.minimum_temperature_millikelvin + spread;
    const auto radiation = field(seed, S::ambient_radiation, 1) % 3;
    e.radiation_nanosieverts_per_hour =
        radiation == 0 ? bounded(seed, S::ambient_radiation, 2, 0, 1'000)
        : radiation == 1
            ? bounded(seed, S::ambient_radiation, 2, 1'001, 1'000'000)
            : bounded(seed, S::ambient_radiation, 2, 1'000'001, 1'000'000'000);
    e.electric_field_volts_per_metre =
        bounded(seed, S::ambient_electrical, 1, 0, 100'000);
    e.gust_millimetres_per_second =
        bounded(seed, S::ambient_turbulence, 1, 0, 200'000);
    e.ground_acceleration_mm_per_second2 =
        bounded(seed, S::ambient_ground_motion, 1, 0, 20'000);
  }
  if (p.atmosphere_class == AtmosphereClass::airless) {
    e.electric_field_volts_per_metre = 0;
    e.gust_millimetres_per_second = 0;
    e.visibility = AtmosphericVisibilityKind::airless;
  } else if (field(seed, S::ambient_visibility, 1) % 3 == 0) {
    e.visibility = AtmosphericVisibilityKind::clear;
  } else {
    e.visibility = p.atmosphere_class == AtmosphereClass::tenuous ||
                           p.water_coverage.value < 1'000
                       ? AtmosphericVisibilityKind::dust
                       : AtmosphericVisibilityKind::cloud;
    e.maximum_obscuration_basis_points = static_cast<std::uint16_t>(
        bounded(seed, S::ambient_visibility, 2, 1, home ? 2'500 : 10'000));
  }
  return e;
}
// The closed schema needs at most two container levels. Bound nesting before
// parsing rather than relying on a callback after allocating a deep DOM.
auto bounded_document(std::string_view text) -> bool {
  if (text.empty() || text.size() > kMaximumPlanetAmbientRecipeBytes)
    return false;
  std::array<char, 2> stack{};
  std::size_t depth{};
  bool quoted{}, escaped{};
  for (const char c : text) {
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
    } else if (c == '"')
      quoted = true;
    else if (c == '{' || c == '[') {
      if (depth == stack.size()) return false;
      stack[depth++] = c;
    } else if (c == '}' || c == ']') {
      if (!depth || stack[depth - 1] != (c == '}' ? '{' : '[')) return false;
      --depth;
    }
  }
  return !quoted && !depth;
}
} // namespace

auto generate_planet_ambient_recipe(const PhysicalLocalSystem& system,
                                    PlanetId planet, std::uint32_t version)
    -> std::expected<PlanetAmbientRecipe, Error> {
  if (version != kPlanetAmbientGeneratorVersion)
    return std::unexpected{Error::unsupported_version};
  const auto p = find_local_system_planet(system, planet);
  if (!p)
    return std::unexpected{p.error() == PhysicalLocalSystemError::unknown_planet
                               ? Error::unknown_planet
                               : Error::invalid_context};
  return PlanetAmbientRecipe{version,
                             system.generator_version,
                             system.ephemeris_version,
                             system.catalog.id,
                             system.catalog.seed,
                             system.catalog.kind,
                             system.origin_universe_seed,
                             planet};
}

auto resolve_planet_ambient_environment(const PhysicalLocalSystem& system,
                                        const PlanetAmbientRecipe& recipe)
    -> std::expected<PlanetAmbientEnvironment, Error> {
  const auto shape = valid_recipe_shape(recipe);
  if (!shape) return std::unexpected{shape.error()};
  const auto expected =
      generate_planet_ambient_recipe(system, recipe.planet, recipe.version);
  if (!expected) return std::unexpected{expected.error()};
  if (*expected != recipe) return std::unexpected{Error::owner_mismatch};
  const auto p = find_local_system_planet(system, recipe.planet);
  if (!p) return std::unexpected{Error::invalid_context};
  const bool home =
      recipe.catalog_kind == LocalSystemKind::origin_home &&
      recipe.planet == generate_origin_home_planet(recipe.system_seed).id;
  const auto e = environment_for((*p)->descriptor, recipe, home);
  const auto valid = validate_planet_ambient_environment(e);
  if (!valid) return std::unexpected{valid.error()};
  return e;
}

auto validate_planet_ambient_environment(const PlanetAmbientEnvironment& e)
    -> std::expected<void, Error> {
  const auto shape = valid_recipe_shape(e.recipe);
  if (!shape) return std::unexpected{shape.error()};
  if (e.surface_gravity.value < SurfaceGravityMilliG::min ||
      e.surface_gravity.value > SurfaceGravityMilliG::max ||
      e.minimum_temperature_millikelvin < 30'000 ||
      e.maximum_temperature_millikelvin > 1'400'000 ||
      e.minimum_temperature_millikelvin > e.maximum_temperature_millikelvin ||
      e.radiation_nanosieverts_per_hour > 1'000'000'000 ||
      e.electric_field_volts_per_metre > 100'000 ||
      e.gust_millimetres_per_second > 200'000 ||
      e.ground_acceleration_mm_per_second2 > 20'000 ||
      e.maximum_obscuration_basis_points > 10'000)
    return std::unexpected{Error::invalid_environment};
  const auto pressure = e.surface_pressure.value;
  bool atmosphere{};
  switch (e.atmosphere_class) {
    case AtmosphereClass::airless: atmosphere = pressure == 0; break;
    case AtmosphereClass::tenuous:
      atmosphere = pressure >= 1 && pressure <= 249;
      break;
    case AtmosphereClass::temperate:
      atmosphere = pressure >= 250 && pressure <= 1'499;
      break;
    case AtmosphereClass::dense:
      atmosphere = pressure >= 1'500 && pressure <= 2'500;
      break;
    default: return std::unexpected{Error::invalid_environment};
  }
  if (!atmosphere) return std::unexpected{Error::invalid_environment};
  if (e.atmosphere_class == AtmosphereClass::airless) {
    if (e.visibility != AtmosphericVisibilityKind::airless ||
        e.maximum_obscuration_basis_points ||
        e.electric_field_volts_per_metre || e.gust_millimetres_per_second)
      return std::unexpected{Error::invalid_environment};
  } else {
    switch (e.visibility) {
      case AtmosphericVisibilityKind::clear:
        if (e.maximum_obscuration_basis_points)
          return std::unexpected{Error::invalid_environment};
        break;
      case AtmosphericVisibilityKind::cloud:
        if (e.atmosphere_class == AtmosphereClass::tenuous)
          return std::unexpected{Error::invalid_environment};
        [[fallthrough]];
      case AtmosphericVisibilityKind::dust:
        if (!e.maximum_obscuration_basis_points)
          return std::unexpected{Error::invalid_environment};
        break;
      default: return std::unexpected{Error::invalid_environment};
    }
  }
  return {};
}

auto evaluate_ambient_surface_exposure(const PhysicalLocalSystem& system,
                                       const PlanetAmbientRecipe& recipe,
                                       AmbientSurfaceOperation operation)
    -> std::expected<AmbientSurfaceExposure, Error> {
  if (operation != AmbientSurfaceOperation::outdoor &&
      operation != AmbientSurfaceOperation::landing &&
      operation != AmbientSurfaceOperation::atmospheric_transit)
    return std::unexpected{Error::invalid_operation};
  const auto e = resolve_planet_ambient_environment(system, recipe);
  if (!e) return std::unexpected{e.error()};
  return AmbientSurfaceExposure{
      *e, operation != AmbientSurfaceOperation::atmospheric_transit};
}

auto atmospheric_visibility_name(AtmosphericVisibilityKind value) noexcept
    -> std::string_view {
  switch (value) {
    case AtmosphericVisibilityKind::airless: return "airless";
    case AtmosphericVisibilityKind::clear: return "clear";
    case AtmosphericVisibilityKind::cloud: return "cloud";
    case AtmosphericVisibilityKind::dust: return "dust";
  }
  return "unknown";
}

auto encode_planet_ambient_recipe_json(const PhysicalLocalSystem& system,
                                       const PlanetAmbientRecipe& r)
    -> std::expected<std::string, Error> {
  const auto e = resolve_planet_ambient_environment(system, r);
  if (!e) return std::unexpected{e.error()};
  Json j{{"provider", "planet_ambient"},
         {"version", r.version},
         {"physical_catalog", r.physical_catalog},
         {"ephemeris", r.ephemeris},
         {"system", r.system.value},
         {"system_seed", r.system_seed.value},
         {"catalog_kind", static_cast<std::uint8_t>(r.catalog_kind)},
         {"origin_universe_seed", nullptr},
         {"planet", r.planet.value}};
  if (r.origin_universe_seed)
    j["origin_universe_seed"] = r.origin_universe_seed->value;
  return j.dump() + '\n';
}

auto decode_planet_ambient_recipe_json(const PhysicalLocalSystem& system,
                                       std::string_view text)
    -> std::expected<PlanetAmbientRecipe, Error> {
  if (!bounded_document(text))
    return std::unexpected{Error::invalid_recipe_document};
  bool duplicate{};
  std::unordered_set<std::string> keys;
  const auto callback = [&](int, Json::parse_event_t event, Json& parsed) {
    if (event == Json::parse_event_t::key &&
        !keys.insert(parsed.get<std::string>()).second)
      duplicate = true;
    return true;
  };
  const auto j = Json::parse(text, callback, false);
  if (duplicate || !j.is_object() || j.size() != 9 || !j.contains("provider") ||
      j["provider"] != "planet_ambient")
    return std::unexpected{Error::invalid_recipe_document};
  for (const auto key : {"version", "physical_catalog", "ephemeris", "system",
                         "system_seed", "catalog_kind", "planet"})
    if (!j.contains(key) || !j[key].is_number_unsigned())
      return std::unexpected{Error::invalid_recipe_document};
  if (!j.contains("origin_universe_seed") ||
      (!j["origin_universe_seed"].is_null() &&
       !j["origin_universe_seed"].is_number_unsigned()) ||
      j["version"] > std::numeric_limits<std::uint32_t>::max() ||
      j["physical_catalog"] > std::numeric_limits<std::uint32_t>::max() ||
      j["ephemeris"] > std::numeric_limits<std::uint32_t>::max() ||
      j["catalog_kind"] > 1)
    return std::unexpected{Error::invalid_recipe_document};
  PlanetAmbientRecipe r{
      j["version"].get<std::uint32_t>(),
      j["physical_catalog"].get<std::uint32_t>(),
      j["ephemeris"].get<std::uint32_t>(),
      SystemId{j["system"].get<std::uint64_t>()},
      Seed{j["system_seed"].get<std::uint64_t>()},
      static_cast<LocalSystemKind>(j["catalog_kind"].get<std::uint8_t>()),
      {},
      PlanetId{j["planet"].get<std::uint64_t>()}};
  if (!j["origin_universe_seed"].is_null())
    r.origin_universe_seed =
        Seed{j["origin_universe_seed"].get<std::uint64_t>()};
  const auto e = resolve_planet_ambient_environment(system, r);
  if (!e) return std::unexpected{e.error()};
  return r;
}
} // namespace apsis_drift
