#include "apsis_drift/planet_rotation.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace apsis_drift {
namespace {
// A dependency upgrade requires an explicit recipe compatibility decision, not
// silently regenerating old rotation semantics under the same version number.
static_assert(kSeedDerivationVersion == 1 && kPlanetGeneratorVersion == 1 &&
              kLocalSystemGeneratorVersion == 1 &&
              kAnalyticEphemerisVersion == 1 &&
              kOriginHomePlanetGeneratorVersion == 1);
static_assert(kPhysicalLocalSystemGeneratorVersion == 1 &&
              kOriginStationGeneratorVersion == 2);
constexpr double tau{2.0 * std::numbers::pi_v<double>};
constexpr double turn_scale{1.0 / 4'294'967'296.0};
constexpr SimulationTick ticks_per_minute{60ULL * kSimulationHz};

auto field(Seed rotation_seed, std::uint64_t ordinal) -> std::uint64_t {
  // Independently derived fields; adding a field cannot consume another's RNG.
  // Fixed SplitMix64 finalizer, then documented integer modulo mapping.
  auto value = derive_seed(rotation_seed, SeedDomain::planet, ordinal).value;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

auto recipe_for(const LocalSystemDescriptor& system,
                const PlanetDescriptor& planet) -> PlanetRotationRecipe {
  const auto seed =
      derive_planet_stream_seed(planet.seed, PlanetDescriptorStream::rotation);
  const auto period = kPlanetRotationMinimumPeriodTicks +
                      (field(seed, 1) % 3'961ULL) * ticks_per_minute;
  return {kPlanetRotationGeneratorVersion,
          system.id,
          system.kind,
          planet.id,
          period,
          field(seed, 2) % period,
          static_cast<std::uint32_t>(field(seed, 3) % 45'000'001ULL),
          static_cast<std::uint32_t>(field(seed, 4))};
}

auto multiply(RigidOrientation a, RigidOrientation b) -> RigidOrientation {
  return {((a.w * b.w - a.x * b.x) - a.y * b.y) - a.z * b.z,
          ((a.w * b.x + a.x * b.w) + a.y * b.z) - a.z * b.y,
          ((a.w * b.y - a.x * b.z) + a.y * b.w) + a.z * b.x,
          ((a.w * b.z + a.x * b.y) - a.y * b.x) + a.z * b.w};
}

auto axis_rotation(std::size_t axis, double angle) -> RigidOrientation {
  const double sine = std::sin(angle * .5);
  return {std::cos(angle * .5), axis == 0 ? sine : 0.0, axis == 1 ? sine : 0.0,
          axis == 2 ? sine : 0.0};
}

auto canonical_zero(double value) -> double {
  return value == 0 ? 0 : value;
}

auto rotate(RigidOrientation q, RigidVector3 vector) -> RigidVector3 {
  const auto product = multiply(multiply(q, {0, vector.x, vector.y, vector.z}),
                                {q.w, -q.x, -q.y, -q.z});
  const double norm2 = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  return {canonical_zero(product.x / norm2), canonical_zero(product.y / norm2),
          canonical_zero(product.z / norm2)};
}

auto length(RigidVector3 vector) -> double {
  return std::sqrt((vector.x * vector.x + vector.y * vector.y) +
                   vector.z * vector.z);
}

auto unit(RigidVector3 vector, double magnitude) -> RigidVector3 {
  return {canonical_zero(vector.x / magnitude),
          canonical_zero(vector.y / magnitude),
          canonical_zero(vector.z / magnitude)};
}

auto finite(RigidVector3 vector) -> bool {
  return std::isfinite(vector.x) && std::isfinite(vector.y) &&
         std::isfinite(vector.z);
}

auto physical_recipe_for(const PhysicalLocalSystem& system,
                         const PlanetDescriptor& planet)
    -> PhysicalPlanetRotationRecipe {
  return {.physical_catalog_generator = system.generator_version,
          .source_catalog_generator = system.source_catalog_generator,
          .ephemeris_version = system.ephemeris_version,
          .origin_universe_seed = system.origin_universe_seed,
          .rotation = recipe_for(system.catalog, planet)};
}

auto validate_query(SimulationTick tick, PlanetFixedPositionMetres observer)
    -> std::expected<void, PlanetRotationError> {
  if (tick == std::numeric_limits<SimulationTick>::max())
    return std::unexpected{PlanetRotationError::invalid_tick};
  for (const double value : {observer.x, observer.y, observer.z}) {
    if (!std::isfinite(value) ||
        std::abs(value) > kPlanetRotationMaximumObserverComponentMetres)
      return std::unexpected{PlanetRotationError::invalid_observer};
  }
  return {};
}

auto resolve_geometry(const PlanetRotationRecipe& recipe,
                      const PlanetOrbit& orbit, StarId star,
                      const PlanetEphemeris& ephemeris, SimulationTick tick,
                      PlanetFixedPositionMetres observer)
    -> std::expected<PlanetRotationGeometry, PlanetRotationError>;
} // namespace

auto generate_planet_rotation_recipe(const LocalSystemDescriptor& system,
                                     PlanetId planet, std::uint32_t version)
    -> std::expected<PlanetRotationRecipe, PlanetRotationError> {
  if (version != kPlanetRotationGeneratorVersion)
    return std::unexpected{PlanetRotationError::unsupported_version};
  const auto body = find_local_system_planet(system, planet);
  if (!body)
    return std::unexpected{body.error() == LocalSystemError::unknown_planet
                               ? PlanetRotationError::unknown_planet
                               : PlanetRotationError::invalid_world_context};
  return recipe_for(system, (*body)->descriptor);
}

auto resolve_planet_rotation(const LocalSystemDescriptor& system,
                             const PlanetRotationRecipe& recipe,
                             SimulationTick tick,
                             PlanetFixedPositionMetres observer)
    -> std::expected<PlanetRotationGeometry, PlanetRotationError> {
  if (recipe.version != kPlanetRotationGeneratorVersion)
    return std::unexpected{PlanetRotationError::unsupported_version};
  if (!validate_local_system(system))
    return std::unexpected{PlanetRotationError::invalid_world_context};
  if (recipe.system != system.id || recipe.catalog_kind != system.kind)
    return std::unexpected{PlanetRotationError::owner_mismatch};
  const auto body = find_local_system_planet(system, recipe.planet);
  if (!body) return std::unexpected{PlanetRotationError::unknown_planet};
  if (recipe != recipe_for(system, (*body)->descriptor))
    return std::unexpected{PlanetRotationError::invalid_recipe};
  const auto valid_query = validate_query(tick, observer);
  if (!valid_query) return std::unexpected{valid_query.error()};
  const auto ephemeris =
      resolve_planet_ephemeris(system, recipe.planet, {tick, 0});
  if (!ephemeris)
    return std::unexpected{PlanetRotationError::unsafe_arithmetic};
  return resolve_geometry(recipe, (*body)->orbit, system.star.id, *ephemeris,
                          tick, observer);
}

namespace {
// Both public families validate their exact recipe/context and resolve their
// own same-tick ephemeris before entering this common numerical kernel.
auto resolve_geometry(const PlanetRotationRecipe& recipe,
                      const PlanetOrbit& orbit, StarId star,
                      const PlanetEphemeris& ephemeris, SimulationTick tick,
                      PlanetFixedPositionMetres observer)
    -> std::expected<PlanetRotationGeometry, PlanetRotationError> {
  // Both terms < P <= 31,104,000, so the addition cannot overflow and no
  // information is lost by conversion to double even for near-maximum ticks.
  const auto cycle = (tick % recipe.period_ticks + recipe.epoch_phase_tick) %
                     recipe.period_ticks;
  const double phase = tau * static_cast<double>(cycle) /
                       static_cast<double>(recipe.period_ticks);
  const double node =
      tau * static_cast<double>(orbit.ascending_node_turns) * turn_scale;
  const double inclination =
      static_cast<double>(orbit.inclination_microdegrees) *
      (std::numbers::pi_v<double> / 180'000'000.0);
  const double azimuth =
      tau * static_cast<double>(recipe.pole_azimuth_turns) * turn_scale;
  const double tilt = static_cast<double>(recipe.tilt_microdegrees) *
                      (std::numbers::pi_v<double> / 180'000'000.0);
  auto q = multiply(axis_rotation(2, node), axis_rotation(0, inclination));
  q = multiply(q, axis_rotation(2, azimuth));
  q = multiply(q, axis_rotation(1, tilt));
  const auto pole = normalize_rigid_orientation(q);
  if (!pole) return std::unexpected{PlanetRotationError::unsafe_arithmetic};
  q = multiply(*pole, axis_rotation(2, phase));
  const auto normalized = normalize_rigid_orientation(q);
  if (!normalized)
    return std::unexpected{PlanetRotationError::unsafe_arithmetic};
  q = *normalized;
  const double spin = tau * static_cast<double>(kSimulationHz) /
                      static_cast<double>(recipe.period_ticks);
  // The pole is fixed: calculate Omega before phase multiplication so its
  // bits do not wobble with phase-dependent quaternion rounding.
  const auto omega = rotate(*pole, {0, 0, spin});
  const auto inverse = RigidOrientation{q.w, -q.x, -q.y, -q.z};
  const auto star_fixed =
      rotate(inverse, {-ephemeris.position.x, -ephemeris.position.y,
                       -ephemeris.position.z});
  const RigidVector3 to_star{star_fixed.x - observer.x,
                             star_fixed.y - observer.y,
                             star_fixed.z - observer.z};
  const double distance = length(to_star);
  if (!std::isfinite(distance))
    return std::unexpected{PlanetRotationError::unsafe_arithmetic};
  if (distance <= 1.0e-6)
    return std::unexpected{PlanetRotationError::observer_at_star};
  const auto light_fixed = unit(to_star, distance);
  const auto rotated_light = rotate(q, light_fixed);
  const auto light_system = unit(rotated_light, length(rotated_light));
  if (!finite(omega) || !finite(star_fixed) || !finite(light_fixed) ||
      !finite(light_system))
    return std::unexpected{PlanetRotationError::unsafe_arithmetic};
  observer = {canonical_zero(observer.x), canonical_zero(observer.y),
              canonical_zero(observer.z)};
  return PlanetRotationGeometry{
      recipe,
      tick,
      cycle,
      phase,
      q,
      {omega.x, omega.y, omega.z},
      ephemeris.position,
      ephemeris.velocity,
      star,
      {star_fixed.x, star_fixed.y, star_fixed.z},
      observer,
      {light_fixed.x, light_fixed.y, light_fixed.z},
      {light_system.x, light_system.y, light_system.z},
      distance};
}
} // namespace

auto generate_planet_rotation_recipe(const PhysicalLocalSystem& system,
                                     PlanetId planet, std::uint32_t version)
    -> std::expected<PhysicalPlanetRotationRecipe, PlanetRotationError> {
  if (version != kPlanetRotationGeneratorVersion)
    return std::unexpected{PlanetRotationError::unsupported_version};
  const auto body = find_local_system_planet(system, planet);
  if (!body)
    return std::unexpected{body.error() ==
                                   PhysicalLocalSystemError::unknown_planet
                               ? PlanetRotationError::unknown_planet
                               : PlanetRotationError::invalid_world_context};
  return physical_recipe_for(system, (*body)->descriptor);
}

auto resolve_planet_rotation(const PhysicalLocalSystem& system,
                             const PhysicalPlanetRotationRecipe& recipe,
                             SimulationTick tick,
                             PlanetFixedPositionMetres observer)
    -> std::expected<PhysicalPlanetRotationGeometry, PlanetRotationError> {
  if (recipe.catalog_family != PlanetRotationOwnerFamily::physical_circular ||
      recipe.owner_version != kPhysicalPlanetRotationOwnerVersion ||
      recipe.physical_catalog_generator !=
          kPhysicalLocalSystemGeneratorVersion ||
      recipe.source_catalog_generator != kLocalSystemGeneratorVersion ||
      recipe.ephemeris_version != kAnalyticEphemerisVersion ||
      recipe.rotation.version != kPlanetRotationGeneratorVersion)
    return std::unexpected{PlanetRotationError::unsupported_version};
  if (!validate_local_system(system))
    return std::unexpected{PlanetRotationError::invalid_world_context};
  if (recipe.rotation.system != system.catalog.id ||
      recipe.rotation.catalog_kind != system.catalog.kind ||
      recipe.origin_universe_seed != system.origin_universe_seed)
    return std::unexpected{PlanetRotationError::owner_mismatch};
  const auto body = find_local_system_planet(system, recipe.rotation.planet);
  if (!body) return std::unexpected{PlanetRotationError::unknown_planet};
  if (recipe != physical_recipe_for(system, (*body)->descriptor))
    return std::unexpected{PlanetRotationError::invalid_recipe};
  const auto valid_query = validate_query(tick, observer);
  if (!valid_query) return std::unexpected{valid_query.error()};
  const auto ephemeris =
      resolve_planet_ephemeris(system, recipe.rotation.planet, {tick, 0});
  if (!ephemeris)
    return std::unexpected{PlanetRotationError::unsafe_arithmetic};
  const auto geometry =
      resolve_geometry(recipe.rotation, (*body)->orbit, system.catalog.star.id,
                       *ephemeris, tick, observer);
  if (!geometry) return std::unexpected{geometry.error()};
  return PhysicalPlanetRotationGeometry{recipe, *geometry};
}

} // namespace apsis_drift
