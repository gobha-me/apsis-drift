#include "apsis_drift/physical_local_system.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <source_location>
#include <string_view>

#include "apsis_drift/planet_rotation.hpp"

namespace {
using namespace apsis_drift;
using Error = PhysicalLocalSystemError;
int failures{};

auto check(bool condition, std::string_view reason,
           std::source_location where = std::source_location::current())
    -> void {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL line " << where.line() << ": " << reason << '\n';
}
template <typename T, typename E>
auto required(const std::expected<T, E>& result,
              std::source_location where = std::source_location::current())
    -> T {
  if (!result) {
    std::cerr << "Fixture refused at line " << where.line() << '\n';
    std::exit(1);
  }
  return *result;
}
template <typename T>
auto error_is(const std::expected<T, Error>& result, Error expected) -> bool {
  return !result && result.error() == expected;
}

auto hash_word(std::uint64_t& hash, std::uint64_t value) -> void {
  for (unsigned byte = 0; byte < 8; ++byte) {
    hash ^= (value >> (byte * 8)) & 255U;
    hash *= 1'099'511'628'211ULL;
  }
}
auto hash_pose(std::uint64_t& hash, const PlanetEphemeris& pose) -> void {
  hash_word(hash, pose.planet.value);
  hash_word(hash, pose.cycle_tick);
  for (const double value :
       {pose.phase_radians, pose.position.x, pose.position.y, pose.position.z,
        pose.velocity.x, pose.velocity.y, pose.velocity.z})
    hash_word(hash, std::bit_cast<std::uint64_t>(value));
}

auto orbit_oracle(const PhysicalLocalSystem& system,
                  const LocalSystemPlanet& body, EphemerisQueryTime time)
    -> void {
  const auto result =
      required(resolve_planet_ephemeris(system, body.descriptor.id, time));
  const long double a = body.orbit.radius_kilometres;
  const long double mu = system.stellar_gm_km3_per_second2;
  // Independent higher-precision scalar oracle. Production uses binary64 and
  // fixed multiplies; this oracle uses long-double pow and pi.
  const long double ideal_ticks = 240.0L * std::numbers::pi_v<long double> *
                                  std::sqrt(std::pow(a, 3.0L) / mu);
  check(std::abs(static_cast<long double>(body.orbit.period_ticks) -
                 ideal_ticks) <= .50002L,
        "Kepler period exceeds half-tick rounding plus numerical allowance");
  check(body.orbit.period_ticks ==
            static_cast<SimulationTick>(std::round(ideal_ticks)),
        "Independent oracle rounds to different integer period");

  const long double pi = std::numbers::pi_v<long double>;
  const long double phase =
      2 * pi *
      std::fmod(
          static_cast<long double>(body.orbit.epoch_phase_turns) /
                  4294967296.0L +
              (static_cast<long double>(time.tick % body.orbit.period_ticks) +
               time.sub_tick_fraction) /
                  body.orbit.period_ticks,
          1.0L);
  const long double node =
      2 * pi * body.orbit.ascending_node_turns / 4294967296.0L;
  const long double inclination =
      pi * body.orbit.inclination_microdegrees / 180000000.0L;
  // Orbit plane basis constructed independently; derivative uses exactly the
  // selected integer period, not a second unquantized Kepler period.
  const std::array<long double, 3> u{std::cos(node), std::sin(node), 0};
  const std::array<long double, 3> v{-std::sin(node) * std::cos(inclination),
                                     std::cos(node) * std::cos(inclination),
                                     std::sin(inclination)};
  const std::array actual_p{result.position.x, result.position.y,
                            result.position.z};
  const std::array actual_v{result.velocity.x, result.velocity.y,
                            result.velocity.z};
  long double p2{}, v2{}, pv{};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    const long double p =
        a * 1000 * (u[axis] * std::cos(phase) + v[axis] * std::sin(phase));
    const long double velocity =
        a * 1000 * (2 * pi * 120 / body.orbit.period_ticks) *
        (-u[axis] * std::sin(phase) + v[axis] * std::cos(phase));
    check(std::abs(actual_p[axis] - p) <= .501L,
          "Position disagrees with independent orbit basis");
    check(std::abs(actual_v[axis] - velocity) <= .000501L,
          "Velocity disagrees with derivative oracle");
    p2 += static_cast<long double>(actual_p[axis]) * actual_p[axis];
    v2 += static_cast<long double>(actual_v[axis]) * actual_v[axis];
    pv += static_cast<long double>(actual_p[axis]) * actual_v[axis];
  }
  const long double radius_m = a * 1000;
  const long double speed = std::sqrt(v2);
  check(std::abs(std::sqrt(p2) - radius_m) <= .87L,
        "Quantized orbit left circular radius bound");
  check(std::abs(pv) <= radius_m * .00087L + speed * .87L + .001L,
        "Position and velocity are not perpendicular within quantization");
  const long double relative_error =
      std::abs(v2 * radius_m / (mu * 1.0e9L) - 1);
  const long double relative_bound =
      2.0L / body.orbit.period_ticks + .00175L / speed + 1e-12L;
  check(relative_error < relative_bound,
        "Circular speed inconsistent with stellar GM");
}

auto corpus() -> void {
  std::uint64_t recipe_hash{14'695'981'039'346'656'037ULL};
  std::uint64_t geometry_hash{14'695'981'039'346'656'037ULL};
  for (const auto seed :
       std::array{Seed{0}, Seed{1}, Seed{42}, Seed{999},
                  Seed{std::numeric_limits<std::uint64_t>::max()}}) {
    for (const bool origin : {false, true}) {
      const auto system =
          required(origin ? generate_physical_origin_system(seed)
                          : generate_physical_local_system(seed));
      const auto saved =
          required(origin ? generate_physical_origin_system(seed)
                          : generate_physical_local_system(seed));
      const auto legacy =
          origin ? generate_origin_system(seed) : generate_local_system(seed);
      check(validate_local_system(system).has_value(),
            "Canonical physical catalog rejected");
      check(!validate_local_system(system.catalog),
            "Physical periods accepted as legacy catalog");
      check(system.catalog.star == legacy.star &&
                system.catalog.id == legacy.id &&
                system.catalog.kind == legacy.kind,
            "Physical recipe changed unrelated star/owner fields");
      check(system.origin_universe_seed ==
                (origin ? std::optional{seed} : std::nullopt),
            "Origin universe ownership not retained");
      check(system.stellar_gm_km3_per_second2 ==
                132'712'440ULL * system.stellar_mass_millisolar,
            "Stellar mass does not produce exact nominal GM");
      hash_word(recipe_hash, seed.value);
      hash_word(recipe_hash, origin);
      hash_word(recipe_hash, system.stellar_mass_millisolar);
      hash_word(recipe_hash, system.stellar_gm_km3_per_second2);
      SimulationTick previous{};
      for (std::size_t index = 0; index < system.catalog.planets.size();
           ++index) {
        const auto& body = system.catalog.planets[index];
        check(body.descriptor == legacy.planets[index].descriptor,
              "Physical catalog changed procedural or authored-home planet");
        auto original_orbit = body.orbit;
        original_orbit.period_ticks = legacy.planets[index].orbit.period_ticks;
        check(original_orbit == legacy.planets[index].orbit,
              "Non-period orbital field changed");
        check(body.orbit.period_ticks > previous &&
                  body.orbit.period_ticks < (1ULL << 53U),
              "Periods are nonmonotone or lose integer precision");
        previous = body.orbit.period_ticks;
        hash_word(recipe_hash, body.descriptor.id.value);
        hash_word(recipe_hash, body.orbit.period_ticks);
        check(required(find_local_system_planet(system, body.descriptor.id)) ==
                  &body,
              "Lookup did not retain owned descriptor identity");
        check(!generate_planet_rotation_recipe(system.catalog,
                                               body.descriptor.id),
              "Legacy rotation silently accepted physical catalog");
        check(!resolve_planet_ephemeris(system.catalog, body.descriptor.id,
                                        {0, 0}),
              "Legacy ephemeris silently accepted physical catalog");
        for (const auto tick : std::array<SimulationTick, 6>{
                 0, 1, 1234567, body.orbit.period_ticks - 1,
                 body.orbit.period_ticks,
                 std::numeric_limits<SimulationTick>::max() - 1}) {
          orbit_oracle(system, body, {tick, 0});
          hash_pose(geometry_hash, required(resolve_planet_ephemeris(
                                       system, body.descriptor.id, {tick, 0})));
        }
        orbit_oracle(system, body, {body.orbit.period_ticks - 1, .999});
        const auto first = required(
            resolve_planet_ephemeris(system, body.descriptor.id, {123, .5}));
        const auto repeated = required(resolve_planet_ephemeris(
            system, body.descriptor.id, {123 + body.orbit.period_ticks, .5}));
        check(first == repeated, "One exact period does not repeat geometry");
      }
      check(system == saved, "Queries mutated the physical catalog");
      check(legacy == (origin ? generate_origin_system(seed)
                              : generate_local_system(seed)),
            "Physical generation interfered with legacy streams");
    }
  }
  std::cout << "Physical catalog recipe hash " << recipe_hash
            << "; geometry hash " << geometry_hash << '\n';
  // Observed identical with GCC and Clang on the qualification host. The
  // independent analytic checks above are separate from these regression bits.
  check(recipe_hash == 13'843'200'183'000'786'935ULL,
        "Physical-family-v1 recipe golden changed");
  check(geometry_hash == 886'106'762'250'797'741ULL,
        "Physical-family-v1 geometry golden changed");
}

auto malformed() -> void {
  const auto original = required(generate_physical_origin_system(Seed{42}));
  const auto planet = original.catalog.planets.front().descriptor.id;
  for (const auto version :
       {0U, 2U, std::numeric_limits<std::uint32_t>::max()}) {
    check(error_is(generate_physical_local_system(Seed{42}, version),
                   Error::unsupported_version),
          "Unknown physical generator accepted");
    check(error_is(generate_physical_origin_system(Seed{42}, version),
                   Error::unsupported_version),
          "Unknown physical origin generator accepted");
  }
  auto rejected = [&](auto change, Error error = Error::invalid_context) {
    auto bad = original;
    change(bad);
    const auto before = bad;
    check(error_is(validate_local_system(bad), error),
          "Forged context accepted or wrong error");
    check(error_is(resolve_planet_ephemeris(bad, planet, {0, 0}), error),
          "Ephemeris bypassed strict context validation");
    check(bad == before, "Rejected context mutated input");
  };
  rejected([](auto& s) { ++s.generator_version; }, Error::unsupported_version);
  rejected([](auto& s) { ++s.source_catalog_generator; },
           Error::unsupported_version);
  rejected([](auto& s) { ++s.ephemeris_version; }, Error::unsupported_version);
  rejected([](auto& s) { s.origin_universe_seed.reset(); });
  rejected([](auto& s) { ++s.origin_universe_seed->value; });
  rejected([](auto& s) { s.catalog.kind = LocalSystemKind::procedural; });
  rejected([](auto& s) { s.catalog.kind = static_cast<LocalSystemKind>(99); });
  rejected([](auto& s) { ++s.catalog.id.value; });
  rejected([](auto& s) { ++s.catalog.seed.value; });
  rejected([](auto& s) { ++s.catalog.star.seed.value; });
  rejected([](auto& s) { ++s.catalog.star.radius_kilometres; });
  rejected([](auto& s) { ++s.stellar_mass_millisolar; });
  rejected([](auto& s) { s.stellar_mass_millisolar = 0; });
  rejected([](auto& s) { s.stellar_mass_millisolar = 1601; });
  rejected([](auto& s) { s.stellar_gm_km3_per_second2 = 0; });
  rejected([](auto& s) {
    s.stellar_gm_km3_per_second2 = std::numeric_limits<std::uint64_t>::max();
  });
  rejected([](auto& s) { s.catalog.planets.clear(); });
  rejected([](auto& s) { s.catalog.planets.pop_back(); });
  rejected([](auto& s) { s.catalog.planets[0].orbit.period_ticks = 0; });
  rejected([](auto& s) { ++s.catalog.planets[0].orbit.period_ticks; });
  rejected([](auto& s) {
    s.catalog.planets[0].orbit.period_ticks =
        std::numeric_limits<SimulationTick>::max();
  });
  rejected([](auto& s) { ++s.catalog.planets[0].orbit.radius_kilometres; });
  rejected([](auto& s) { ++s.catalog.planets[0].orbit.epoch_phase_turns; });
  rejected(
      [](auto& s) { ++s.catalog.planets[0].orbit.inclination_microdegrees; });
  rejected([](auto& s) { ++s.catalog.planets[0].orbit.ascending_node_turns; });
  rejected([](auto& s) { ++s.catalog.planets[0].orbit.planet.value; });
  rejected([](auto& s) {
    // Same seed and PlanetId is not the authored home descriptor.
    auto procedural = generate_local_system(s.catalog.seed);
    for (std::size_t i = 0; i < procedural.planets.size(); ++i)
      procedural.planets[i].orbit.period_ticks =
          s.catalog.planets[i].orbit.period_ticks;
    s.catalog.planets = std::move(procedural.planets);
  });
  auto procedural = required(generate_physical_local_system(Seed{42}));
  procedural.origin_universe_seed = Seed{42};
  check(error_is(validate_local_system(procedural), Error::invalid_context),
        "Procedural context accepted an origin owner");
  check(error_is(find_local_system_planet(original, PlanetId{0}),
                 Error::unknown_planet),
        "Unknown planet accepted");
  for (const double fraction :
       {-1.0, 1.0, std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()})
    check(error_is(resolve_planet_ephemeris(original, planet, {0, fraction}),
                   Error::non_finite_time),
          "Invalid fraction accepted");
  check(error_is(resolve_planet_ephemeris(
                     original, planet,
                     {std::numeric_limits<SimulationTick>::max(), 0}),
                 Error::invalid_tick),
        "Unsupported maximum tick accepted");
}

auto mass_boundaries() -> void {
  bool minimum{}, maximum{};
  for (std::uint64_t seed = 0; seed < 20000 && !(minimum && maximum); ++seed) {
    const auto system = required(generate_physical_local_system(Seed{seed}));
    minimum = minimum || system.stellar_mass_millisolar == 80;
    maximum = maximum || system.stellar_mass_millisolar == 1600;
    check(system.stellar_mass_millisolar >= 80 &&
              system.stellar_mass_millisolar <= 1600,
          "Generated mass escaped bounded recipe");
    if (system.stellar_mass_millisolar == 80 ||
        system.stellar_mass_millisolar == 1600)
      for (const auto& body : system.catalog.planets)
        orbit_oracle(system, body, {0, 0});
  }
  check(minimum && maximum, "Mass endpoint fixtures not exercised");
}
} // namespace

auto main() -> int {
  check(kPhysicalLocalSystemRecipeFamily == "physical_circular" &&
            kPhysicalLocalSystemGeneratorVersion == 1,
        "Explicit physical recipe family changed");
  malformed();
  mass_boundaries();
  corpus();
  std::cout << "Physical local-system contracts: " << failures << " failures\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
