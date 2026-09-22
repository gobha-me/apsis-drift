#include "apsis_drift/planet_rotation.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <set>
#include <source_location>
#include <string_view>

#include "apsis_drift/celestial.hpp"
#include "apsis_drift/system_flight.hpp"

namespace {
using namespace apsis_drift;
using Error = PlanetRotationError;
using V = std::array<double, 3>;
using Matrix = std::array<V, 3>;
int failures{};
constexpr double tau{2 * std::numbers::pi_v<double>};

auto check(bool condition, std::string_view reason,
           std::source_location location = std::source_location::current())
    -> void {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL line " << location.line() << ": " << reason << '\n';
}

template <typename T, typename E>
auto required(const std::expected<T, E>& result,
              std::source_location location = std::source_location::current())
    -> T {
  if (!result) {
    std::cerr << "Fixture API refused line " << location.line() << '\n';
    std::exit(1);
  }
  return *result;
}

auto dot(V a, V b) -> double {
  return (a[0] * b[0] + a[1] * b[1]) + a[2] * b[2];
}
auto magnitude(V a) -> double {
  return std::sqrt(dot(a, a));
}
auto cross(V a, V b) -> V {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}
auto normalized(V a) -> V {
  const double n = magnitude(a);
  return {a[0] / n, a[1] / n, a[2] / n};
}
auto close(V a, V b, double tolerance) -> bool {
  return std::abs(a[0] - b[0]) <= tolerance &&
         std::abs(a[1] - b[1]) <= tolerance &&
         std::abs(a[2] - b[2]) <= tolerance;
}
auto apply(Matrix matrix, V vector) -> V {
  return {dot(matrix[0], vector), dot(matrix[1], vector),
          dot(matrix[2], vector)};
}
auto transpose(Matrix matrix) -> Matrix {
  return {{{matrix[0][0], matrix[1][0], matrix[2][0]},
           {matrix[0][1], matrix[1][1], matrix[2][1]},
           {matrix[0][2], matrix[1][2], matrix[2][2]}}};
}
auto product(Matrix a, Matrix b) -> Matrix {
  const auto columns = transpose(b);
  Matrix result{};
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j)
      result[i][j] = dot(a[i], columns[j]);
  return result;
}
auto rx(double angle) -> Matrix {
  const double c = std::cos(angle), s = std::sin(angle);
  return {{{1, 0, 0}, {0, c, -s}, {0, s, c}}};
}
auto ry(double angle) -> Matrix {
  const double c = std::cos(angle), s = std::sin(angle);
  return {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}};
}
auto rz(double angle) -> Matrix {
  const double c = std::cos(angle), s = std::sin(angle);
  return {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
}

// Independent matrix construction, not production quaternion multiplication.
auto oracle(const LocalSystemPlanet& body, const PlanetRotationRecipe& recipe,
            SimulationTick tick) -> Matrix {
  const double node =
      tau * static_cast<double>(body.orbit.ascending_node_turns) / 4294967296.0;
  const double inclination =
      static_cast<double>(body.orbit.inclination_microdegrees) *
      std::numbers::pi_v<double> / 180000000.0;
  const double azimuth =
      tau * static_cast<double>(recipe.pole_azimuth_turns) / 4294967296.0;
  const double tilt = static_cast<double>(recipe.tilt_microdegrees) *
                      std::numbers::pi_v<double> / 180000000.0;
  const auto cycle = (tick % recipe.period_ticks + recipe.epoch_phase_tick) %
                     recipe.period_ticks;
  const double phase = tau * static_cast<double>(cycle) /
                       static_cast<double>(recipe.period_ticks);
  return product(
      product(product(product(rz(node), rx(inclination)), rz(azimuth)),
              ry(tilt)),
      rz(phase));
}

auto quaternion_matrix(RigidOrientation q) -> Matrix {
  const double n = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  const double s = 2.0 / n;
  return {{{1 - s * (q.y * q.y + q.z * q.z), s * (q.x * q.y - q.w * q.z),
            s * (q.x * q.z + q.w * q.y)},
           {s * (q.x * q.y + q.w * q.z), 1 - s * (q.x * q.x + q.z * q.z),
            s * (q.y * q.z - q.w * q.x)},
           {s * (q.x * q.z - q.w * q.y), s * (q.y * q.z + q.w * q.x),
            1 - s * (q.x * q.x + q.y * q.y)}}};
}

struct Hash {
  std::uint64_t value{14695981039346656037ULL};
  auto word(std::uint64_t input) -> void {
    for (int i = 0; i < 8; ++i) {
      value ^= input & 255U;
      value *= 1099511628211ULL;
      input >>= 8U;
    }
  }
  auto scalar(double input) -> void {
    word(std::bit_cast<std::uint64_t>(input));
  }
  auto recipe(const PlanetRotationRecipe& r) -> void {
    word(r.version);
    word(r.system.value);
    word(static_cast<std::uint64_t>(r.catalog_kind));
    word(r.planet.value);
    word(r.period_ticks);
    word(r.epoch_phase_tick);
    word(r.tilt_microdegrees);
    word(r.pole_azimuth_turns);
  }
  auto geometry(const PlanetRotationGeometry& g) -> void {
    recipe(g.recipe);
    word(g.tick);
    word(g.cycle_tick);
    word(g.star.value);
    for (const double x : {g.phase_radians,
                           g.fixed_to_system.w,
                           g.fixed_to_system.x,
                           g.fixed_to_system.y,
                           g.fixed_to_system.z,
                           g.angular_velocity_radians_per_second.x,
                           g.angular_velocity_radians_per_second.y,
                           g.angular_velocity_radians_per_second.z,
                           g.planet_position.x,
                           g.planet_position.y,
                           g.planet_position.z,
                           g.planet_velocity.x,
                           g.planet_velocity.y,
                           g.planet_velocity.z,
                           g.star_position_fixed.x,
                           g.star_position_fixed.y,
                           g.star_position_fixed.z,
                           g.observer_position_fixed.x,
                           g.observer_position_fixed.y,
                           g.observer_position_fixed.z,
                           g.observer_to_star_fixed.x,
                           g.observer_to_star_fixed.y,
                           g.observer_to_star_fixed.z,
                           g.observer_to_star_system.x,
                           g.observer_to_star_system.y,
                           g.observer_to_star_system.z,
                           g.observer_star_distance_metres})
      scalar(x);
  }
};

auto recipe_contract() -> void {
  constexpr std::array streams{
      PlanetDescriptorStream::name,       PlanetDescriptorStream::physical,
      PlanetDescriptorStream::atmosphere, PlanetDescriptorStream::terrain,
      PlanetDescriptorStream::hydrology,  PlanetDescriptorStream::palette,
      PlanetDescriptorStream::celestial,  PlanetDescriptorStream::rotation};
  constexpr std::array<std::uint64_t, 6> old_goldens{
      4137554858639612274ULL,  1905239451672022865ULL,
      18119668118413985072ULL, 15299131893477559319ULL,
      13066816486509969910ULL, 10834501079542380501ULL};
  std::set<std::uint64_t> unique;
  for (std::size_t i = 0; i < streams.size(); ++i) {
    check(static_cast<std::uint64_t>(streams[i]) == i + 1,
          "rotation appended without renumbering existing streams");
    const auto seed = derive_planet_stream_seed(Seed{42}, streams[i]);
    check(unique.insert(seed.value).second,
          "all eight named streams independent");
    if (i < old_goldens.size())
      check(seed.value == old_goldens[i],
            "existing exact stream goldens unchanged");
  }
  check(kLocalSunGeneratorVersion == 1 && kLocalDayTicks == 72000 &&
            kSystemPlanetFrameRotationTicks == 24ULL * 3600ULL * kSimulationHz,
        "legacy light/spin constants unchanged");
  std::set<SimulationTick> periods;
  std::set<SimulationTick> phases;
  Hash hash;
  for (std::uint64_t seed = 0; seed < 32; ++seed) {
    const auto system = generate_local_system(Seed{seed});
    const auto before = required(local_system_diagnostic_json(system));
    for (const auto& body : system.planets) {
      const auto legacy_before =
          required(resolve_local_sun(body.descriptor, 12345));
      const auto r =
          required(generate_planet_rotation_recipe(system, body.descriptor.id));
      check(r.period_ticks >= kPlanetRotationMinimumPeriodTicks &&
                r.period_ticks <= kPlanetRotationMaximumPeriodTicks &&
                r.period_ticks % (60ULL * kSimulationHz) == 0,
            "bounded sidereal minutes");
      check(r.epoch_phase_tick < r.period_ticks &&
                r.tilt_microdegrees <= 45000000U,
            "bounded phase/pole");
      check(required(generate_planet_rotation_recipe(system,
                                                     body.descriptor.id)) == r,
            "recipe independent of generation call order");
      check(required(resolve_local_sun(body.descriptor, 12345)) ==
                legacy_before,
            "rotation does not consume legacy celestial stream");
      periods.insert(r.period_ticks);
      phases.insert(r.epoch_phase_tick);
      hash.recipe(r);
    }
    check(required(local_system_diagnostic_json(system)) == before &&
              generate_local_system(Seed{seed}) == system,
          "catalog and orbital generation unaffected");
  }
  check(periods.size() > 80 && phases.size() > 80,
        "planet-specific period/phase diversity");
  std::cout << "rotation recipe corpus " << hash.value << '\n';
  std::cout << "rotation stream "
            << derive_planet_stream_seed(Seed{42},
                                         PlanetDescriptorStream::rotation)
                   .value
            << '\n';
  // New v1 fixtures observed equal under GCC and Clang. Old stream goldens
  // above remain unchanged; geometric correctness has separate analytic tests.
  check(hash.value == 11471254448112290008ULL,
        "version-one multi-seed recipe corpus golden");
  check(derive_planet_stream_seed(Seed{42}, PlanetDescriptorStream::rotation)
                .value == 5781649447638365339ULL,
        "new append-only rotation stream golden");
}

auto geometry_contract() -> void {
  Hash hash;
  for (const auto& system :
       {generate_origin_system(Seed{42}), generate_local_system(Seed{42}),
        generate_local_system(Seed{0})}) {
    const auto before = required(local_system_diagnostic_json(system));
    for (const auto& body : system.planets) {
      const auto r =
          required(generate_planet_rotation_recipe(system, body.descriptor.id));
      const auto phase_zero =
          (r.period_ticks - r.epoch_phase_tick) % r.period_ticks;
      const auto fixed_omega = required(resolve_planet_rotation(system, r, 0))
                                   .angular_velocity_radians_per_second;
      const std::array ticks{SimulationTick{0},
                             SimulationTick{1},
                             phase_zero,
                             r.period_ticks - 1,
                             r.period_ticks,
                             r.period_ticks + 1,
                             std::numeric_limits<SimulationTick>::max() - 1};
      for (const auto tick : ticks) {
        const auto g = required(resolve_planet_rotation(system, r, tick));
        check(g.angular_velocity_radians_per_second == fixed_omega,
              "fixed pole Omega is bit-identical at every phase/tick");
        const auto matrix = quaternion_matrix(g.fixed_to_system);
        const auto expected = oracle(body, r, tick);
        for (std::size_t i = 0; i < 3; ++i)
          check(close(matrix[i], expected[i], 3e-15),
                "independent axis matrix agrees with quaternion");
        const auto columns = transpose(matrix);
        for (std::size_t i = 0; i < 3; ++i) {
          check(std::abs(dot(columns[i], columns[i]) - 1) < 2e-15,
                "unit rotation axes");
          check(std::abs(dot(columns[i], columns[(i + 1) % 3])) < 2e-15,
                "orthogonal rotation axes");
        }
        check(dot(cross(columns[0], columns[1]), columns[2]) > 1 - 3e-15,
              "right-handed rotation");
        const auto q = g.fixed_to_system;
        check(std::abs(((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z - 1) <
                  1e-12,
              "bounded quaternion norm");
        bool first = true;
        for (const double x : {q.w, q.x, q.y, q.z}) {
          if (x == 0)
            check(!std::signbit(x), "canonical q zero");
          else if (first) {
            check(x > 0, "canonical q sign");
            first = false;
          }
        }
        const auto ephem =
            required(resolve_planet_ephemeris(system, r.planet, {tick, 0}));
        check(g.planet_position == ephem.position &&
                  g.planet_velocity == ephem.velocity,
              "existing same-tick ephemeris preserved exactly");
        const V actual_light{g.observer_to_star_system.x,
                             g.observer_to_star_system.y,
                             g.observer_to_star_system.z};
        const V origin_to_star{-ephem.position.x, -ephem.position.y,
                               -ephem.position.z};
        check(close(actual_light, normalized(origin_to_star), 2e-15),
              "light comes from actual system-origin star");
        const V fixed{g.observer_to_star_fixed.x, g.observer_to_star_fixed.y,
                      g.observer_to_star_fixed.z};
        check(close(apply(matrix, fixed), actual_light, 2e-15),
              "both light frames share one orientation");
        const double speed = tau * static_cast<double>(kSimulationHz) /
                             static_cast<double>(r.period_ticks);
        check(close({g.angular_velocity_radians_per_second.x,
                     g.angular_velocity_radians_per_second.y,
                     g.angular_velocity_radians_per_second.z},
                    apply(matrix, {0, 0, speed}), 1e-18),
              "Omega sign/pole agree with quaternion");
        check(required(resolve_planet_rotation(
                  system,
                  required(generate_planet_rotation_recipe(system, r.planet)),
                  tick)) == g,
              "recipe/tick reconstruction reproduces exact result without a "
              "mutable clock");
        hash.geometry(g);
      }
      const auto a = required(resolve_planet_rotation(system, r, 321));
      const auto b =
          required(resolve_planet_rotation(system, r, 321 + r.period_ticks));
      check(a.fixed_to_system == b.fixed_to_system &&
                a.angular_velocity_radians_per_second ==
                    b.angular_velocity_radians_per_second,
            "sidereal spin wraps exactly");
      check(a.observer_to_star_fixed != b.observer_to_star_fixed,
            "sidereal period is not a synthetic solar-day loop");
      // Central difference at a phase wrap measures positive physical spin.
      const auto t = phase_zero + r.period_ticks;
      const auto minus = required(resolve_planet_rotation(system, r, t - 1));
      const auto plus = required(resolve_planet_rotation(system, r, t + 1));
      const auto at = required(resolve_planet_rotation(system, r, t));
      const auto vm =
          apply(quaternion_matrix(minus.fixed_to_system), {1, 0, 0});
      const auto vp = apply(quaternion_matrix(plus.fixed_to_system), {1, 0, 0});
      V derivative{};
      for (std::size_t i = 0; i < 3; ++i)
        derivative[i] =
            (vp[i] - vm[i]) * static_cast<double>(kSimulationHz) * .5;
      check(
          close(derivative,
                cross({at.angular_velocity_radians_per_second.x,
                       at.angular_velocity_radians_per_second.y,
                       at.angular_velocity_radians_per_second.z},
                      apply(quaternion_matrix(at.fixed_to_system), {1, 0, 0})),
                2e-13),
          "phase-wrap derivative agrees with Omega cross radius");
    }
    check(required(local_system_diagnostic_json(system)) == before,
          "geometry queries leave authoritative context unchanged");
  }
  std::cout << "rotation geometry corpus " << hash.value << '\n';
  check(hash.value == 2000843834884210064ULL,
        "cross-compiler version-one geometry corpus golden");
}

auto illumination_contract() -> void {
  const auto system = generate_origin_system(Seed{42});
  const auto& body = system.planets.front();
  const auto recipe =
      required(generate_planet_rotation_recipe(system, body.descriptor.id));
  const auto centre = required(resolve_planet_rotation(system, recipe, 12345));
  const V star{centre.star_position_fixed.x, centre.star_position_fixed.y,
               centre.star_position_fixed.z};
  const auto up = normalized(star);
  const double radius =
      static_cast<double>(body.descriptor.radius.value) * 1000;
  const V reference = std::abs(up[2]) < .9 ? V{0, 0, 1} : V{1, 0, 0};
  const auto tangent = normalized(cross(up, reference));
  // Exact finite-distance tangent on the spherical surface: n·S = radius.
  const double cosine = radius / magnitude(star);
  const double sine = std::sqrt(1 - cosine * cosine);
  const V horizon{up[0] * cosine + tangent[0] * sine,
                  up[1] * cosine + tangent[1] * sine,
                  up[2] * cosine + tangent[2] * sine};
  for (const auto& [normal, elevation] : std::array<std::pair<V, double>, 3>{
           {{up, 1}, {V{-up[0], -up[1], -up[2]}, -1}, {horizon, 0}}}) {
    const PlanetFixedPositionMetres observer{
        normal[0] * radius, normal[1] * radius, normal[2] * radius};
    const auto g =
        required(resolve_planet_rotation(system, recipe, 12345, observer));
    const V local{g.observer_to_star_fixed.x, g.observer_to_star_fixed.y,
                  g.observer_to_star_fixed.z};
    check(std::abs(dot(normal, local) - elevation) < 2e-15,
          "noon/horizon/night from finite-distance star geometry");
    const auto offset = apply(oracle(body, recipe, 12345),
                              {observer.x, observer.y, observer.z});
    const V to_star{-g.planet_position.x - offset[0],
                    -g.planet_position.y - offset[1],
                    -g.planet_position.z - offset[2]};
    check(close(normalized(to_star),
                {g.observer_to_star_system.x, g.observer_to_star_system.y,
                 g.observer_to_star_system.z},
                2e-15),
          "observer light agrees with independent system-space position");
  }
  const double latitude = std::asin(up[2]),
               longitude = std::atan2(up[1], up[0]);
  const auto substellar = required(
      planet_fixed_from_geodetic(body.descriptor, {latitude, longitude, 0}));
  check(
      close(normalized({substellar.x, substellar.y, substellar.z}), up, 2e-15),
      "substellar coordinates use existing geodetic convention");
  auto zero = required(
      resolve_planet_rotation(system, recipe, 12345, {-0.0, -0.0, -0.0}));
  check(zero == centre && !std::signbit(zero.observer_position_fixed.x),
        "observer signed-zero aliases canonicalized only in output");
}

auto malformed_contract() -> void {
  const auto system = generate_origin_system(Seed{42});
  const auto recipe = required(generate_planet_rotation_recipe(
      system, system.planets.front().descriptor.id));
  const auto before = required(local_system_diagnostic_json(system));
  const auto check_recipe = [&](PlanetRotationRecipe bad, Error error) {
    const auto original = bad;
    const auto result = resolve_planet_rotation(system, bad, 12345);
    check(!result && result.error() == error,
          "malformed recipe fails exact category");
    check(bad == original &&
              required(local_system_diagnostic_json(system)) == before,
          "refusal never mutates input recipe/context");
  };
  auto bad = recipe;
  bad.version = 0;
  check_recipe(bad, Error::unsupported_version);
  bad = recipe;
  ++bad.version;
  check_recipe(bad, Error::unsupported_version);
  bad = recipe;
  ++bad.system.value;
  check_recipe(bad, Error::owner_mismatch);
  bad = recipe;
  bad.catalog_kind = LocalSystemKind::procedural;
  check_recipe(bad, Error::owner_mismatch);
  bad = recipe;
  bad.catalog_kind = static_cast<LocalSystemKind>(255);
  check_recipe(bad, Error::owner_mismatch);
  bad = recipe;
  ++bad.planet.value;
  check_recipe(bad, Error::unknown_planet);
  for (const auto period :
       {SimulationTick{0}, SimulationTick{1},
        std::numeric_limits<SimulationTick>::max(), recipe.period_ticks + 1}) {
    bad = recipe;
    bad.period_ticks = period;
    check_recipe(bad, Error::invalid_recipe);
  }
  bad = recipe;
  bad.epoch_phase_tick = recipe.period_ticks;
  check_recipe(bad, Error::invalid_recipe);
  bad = recipe;
  bad.epoch_phase_tick = (recipe.epoch_phase_tick + 1) % recipe.period_ticks;
  check_recipe(bad, Error::invalid_recipe);
  bad = recipe;
  bad.period_ticks = recipe.period_ticks == kPlanetRotationMaximumPeriodTicks
                         ? recipe.period_ticks - 60ULL * kSimulationHz
                         : recipe.period_ticks + 60ULL * kSimulationHz;
  check_recipe(bad, Error::invalid_recipe);
  bad = recipe;
  bad.tilt_microdegrees = 45000001;
  check_recipe(bad, Error::invalid_recipe);
  bad = recipe;
  bad.tilt_microdegrees = (recipe.tilt_microdegrees + 1) % 45000001U;
  check_recipe(bad, Error::invalid_recipe);
  bad = recipe;
  ++bad.pole_azimuth_turns;
  check_recipe(bad, Error::invalid_recipe);
  const auto invalid_tick = resolve_planet_rotation(
      system, recipe, std::numeric_limits<SimulationTick>::max());
  check(!invalid_tick && invalid_tick.error() == Error::invalid_tick,
        "reserved maximum tick rejected");
  for (const double value :
       {std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        std::nextafter(kPlanetRotationMaximumObserverComponentMetres,
                       std::numeric_limits<double>::infinity())}) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      PlanetFixedPositionMetres observer{};
      const std::array fields{&observer.x, &observer.y, &observer.z};
      *fields[axis] = value;
      const auto result =
          resolve_planet_rotation(system, recipe, 12345, observer);
      check(!result && result.error() == Error::invalid_observer,
            "nonfinite/excessive observer rejected before geometry");
    }
  }
  check(resolve_planet_rotation(system, recipe, 12345, {1e12, -1e12, 1e12})
            .has_value(),
        "inclusive observer bound finite");
  const auto centre = required(resolve_planet_rotation(system, recipe, 12345));
  const auto coincident = resolve_planet_rotation(system, recipe, 12345,
                                                  centre.star_position_fixed);
  check(!coincident && coincident.error() == Error::observer_at_star,
        "coincident point-star direction refused");
  auto forged = system;
  ++forged.star.radius_kilometres;
  const auto invalid = resolve_planet_rotation(forged, recipe, 12345);
  check(!invalid && invalid.error() == Error::invalid_world_context,
        "forged star rejected by authoritative context");
  auto forged_orbit = system;
  ++forged_orbit.planets.front().orbit.period_ticks;
  check(!generate_planet_rotation_recipe(forged_orbit, recipe.planet),
        "forged parent ephemeris rejected");
  auto forged_planet = system;
  forged_planet.planets.clear();
  for (const auto& body : system.planets) {
    if (body.descriptor.id != recipe.planet) {
      forged_planet.planets.push_back(body);
      continue;
    }
    const auto& d = body.descriptor;
    const PlanetDescriptor changed{d.seed,
                                   d.id,
                                   d.display_name,
                                   PlanetRadiusKm{d.radius.value + 1},
                                   d.surface_gravity,
                                   d.atmosphere_class,
                                   d.atmosphere_pressure,
                                   d.terrain_character,
                                   d.water_coverage,
                                   d.palette};
    forged_planet.planets.push_back({changed, body.orbit});
  }
  const auto forged_descriptor =
      generate_planet_rotation_recipe(forged_planet, recipe.planet);
  check(!forged_descriptor &&
            forged_descriptor.error() == Error::invalid_world_context,
        "matching seed and ID do not authorize a forged descriptor");
  const auto procedural = generate_local_system(system.seed);
  const auto alias =
      required(generate_planet_rotation_recipe(procedural, recipe.planet));
  check(alias != recipe && alias.catalog_kind != recipe.catalog_kind,
        "same ID authored/procedural owner variants distinct");
  const auto mismatch = resolve_planet_rotation(procedural, recipe, 12345);
  check(!mismatch && mismatch.error() == Error::owner_mismatch,
        "origin recipe not accepted by procedural alias");
  check(!generate_planet_rotation_recipe(system, recipe.planet, 2),
        "generator version refusal");
  check(!generate_planet_rotation_recipe(system,
                                         PlanetId{recipe.planet.value + 1}),
        "generator unknown planet refusal");
}

auto physical_geometry_contract() -> void {
  Hash hash;
  for (const auto seed :
       std::array{Seed{0}, Seed{42},
                  Seed{std::numeric_limits<std::uint64_t>::max()}}) {
    for (const bool origin : {false, true}) {
      const auto system =
          required(origin ? generate_physical_origin_system(seed)
                          : generate_physical_local_system(seed));
      const auto legacy =
          origin ? generate_origin_system(seed) : generate_local_system(seed);
      for (const auto& body : system.catalog.planets) {
        const auto recipe = required(
            generate_planet_rotation_recipe(system, body.descriptor.id));
        const auto old_recipe = required(
            generate_planet_rotation_recipe(legacy, body.descriptor.id));
        check(recipe.rotation == old_recipe,
              "Physical catalog does not alter planet-specific spin recipe");
        check(recipe.origin_universe_seed == system.origin_universe_seed &&
                  recipe.physical_catalog_generator ==
                      system.generator_version &&
                  recipe.source_catalog_generator ==
                      system.source_catalog_generator &&
                  recipe.ephemeris_version == system.ephemeris_version,
              "Physical rotation retains complete catalog provenance");
        check(!generate_planet_rotation_recipe(system.catalog,
                                               body.descriptor.id),
              "Embedded physical catalog cannot use legacy rotation API");
        hash.word(static_cast<std::uint64_t>(recipe.catalog_family));
        hash.word(recipe.owner_version);
        hash.word(recipe.physical_catalog_generator);
        hash.word(recipe.source_catalog_generator);
        hash.word(recipe.ephemeris_version);
        hash.word(recipe.origin_universe_seed.has_value());
        hash.word(recipe.origin_universe_seed
                      ? recipe.origin_universe_seed->value
                      : 0);
        hash.recipe(recipe.rotation);
        const auto wrap =
            recipe.rotation.period_ticks - recipe.rotation.epoch_phase_tick;
        const std::array ticks{
            SimulationTick{0}, SimulationTick{1234567},
            wrap - 1,          wrap,
            wrap + 1,          std::numeric_limits<SimulationTick>::max() - 1};
        for (const auto tick : ticks) {
          const PlanetFixedPositionMetres observer{1234, -5678, 91011};
          const auto result =
              required(resolve_planet_rotation(system, recipe, tick, observer));
          const auto& g = result.geometry;
          const auto old = required(
              resolve_planet_rotation(legacy, old_recipe, tick, observer));
          const auto ephemeris = required(
              resolve_planet_ephemeris(system, body.descriptor.id, {tick, 0}));
          check(result.recipe == recipe && g.recipe == recipe.rotation &&
                    g.tick == tick,
                "Physical result retains wrapper and exact spin/tick payload");
          check(g.fixed_to_system == old.fixed_to_system &&
                    g.angular_velocity_radians_per_second ==
                        old.angular_velocity_radians_per_second,
                "Shared kernel keeps same spin and fixed pole across catalog "
                "families");
          check(g.planet_position == ephemeris.position &&
                    g.planet_velocity == ephemeris.velocity,
                "Physical rotation uses physical same-tick ephemeris, not "
                "legacy periods");
          if (tick == 1234567) {
            check(g.planet_position != old.planet_position &&
                      g.planet_velocity != old.planet_velocity &&
                      g.observer_to_star_system != old.observer_to_star_system,
                  "Nonzero tick demonstrates genuinely different physical "
                  "orbital lighting");
          }
          const auto expected_rotation = oracle(body, recipe.rotation, tick);
          const auto actual_rotation = quaternion_matrix(g.fixed_to_system);
          for (std::size_t axis = 0; axis < 3; ++axis)
            check(close(actual_rotation[axis], expected_rotation[axis], 3e-15),
                  "Physical quaternion matches independent matrix oracle");
          const auto offset =
              apply(expected_rotation, {observer.x, observer.y, observer.z});
          const V toward_star{-ephemeris.position.x - offset[0],
                              -ephemeris.position.y - offset[1],
                              -ephemeris.position.z - offset[2]};
          check(close(normalized(toward_star),
                      {g.observer_to_star_system.x, g.observer_to_star_system.y,
                       g.observer_to_star_system.z},
                      3e-15),
                "Physical observer light matches independent system-space "
                "direction");
          check(std::abs(magnitude(toward_star) -
                         g.observer_star_distance_metres) <=
                    magnitude(toward_star) * 2e-15,
                "Physical finite-distance star distance matches independent "
                "oracle");
          hash.geometry(g);
        }
        const auto at = required(resolve_planet_rotation(system, recipe, 123));
        const auto after = required(resolve_planet_rotation(
            system, recipe, 123 + recipe.rotation.period_ticks));
        check(at.geometry.fixed_to_system == after.geometry.fixed_to_system &&
                  at.geometry.angular_velocity_radians_per_second ==
                      after.geometry.angular_velocity_radians_per_second,
              "Physical spin wraps independently of orbital year");
        check(at.geometry.observer_to_star_fixed !=
                  after.geometry.observer_to_star_fixed,
              "Physical solar geometry is not looped at sidereal period");
        check(required(generate_planet_rotation_recipe(
                  system, body.descriptor.id)) == recipe,
              "Physical query never mutates its recipe");
      }
      check(system == required(origin ? generate_physical_origin_system(seed)
                                      : generate_physical_local_system(seed)),
            "Physical query never mutates its world context");
    }
  }
  std::cout << "physical rotation geometry corpus " << hash.value << '\n';
  // Observed identical with GCC and Clang on the qualification host; analytic
  // matrix/ephemeris checks above remain separate correctness evidence.
  check(hash.value == 3325105109563507698ULL,
        "physical owner-wrapper geometry corpus golden");
}

auto physical_refusal_contract() -> void {
  const auto system = required(generate_physical_origin_system(Seed{42}));
  const auto recipe = required(generate_planet_rotation_recipe(
      system, system.catalog.planets.front().descriptor.id));
  auto rejected = [&](auto change, Error error) {
    auto bad = recipe;
    change(bad);
    const auto before = bad;
    const auto result = resolve_planet_rotation(system, bad, 12345);
    check(!result && result.error() == error,
          "Physical forged recipe refused with expected category");
    check(before == bad, "Physical recipe refusal is read-only");
  };
  rejected(
      [](auto& r) {
        r.catalog_family = static_cast<PlanetRotationOwnerFamily>(0);
      },
      Error::unsupported_version);
  rejected([](auto& r) { ++r.owner_version; }, Error::unsupported_version);
  rejected([](auto& r) { ++r.physical_catalog_generator; },
           Error::unsupported_version);
  rejected([](auto& r) { ++r.source_catalog_generator; },
           Error::unsupported_version);
  rejected([](auto& r) { ++r.ephemeris_version; }, Error::unsupported_version);
  rejected([](auto& r) { ++r.rotation.version; }, Error::unsupported_version);
  rejected([](auto& r) { r.origin_universe_seed.reset(); },
           Error::owner_mismatch);
  rejected([](auto& r) { ++r.origin_universe_seed->value; },
           Error::owner_mismatch);
  rejected([](auto& r) { ++r.rotation.system.value; }, Error::owner_mismatch);
  rejected(
      [](auto& r) { r.rotation.catalog_kind = LocalSystemKind::procedural; },
      Error::owner_mismatch);
  rejected([](auto& r) { ++r.rotation.planet.value; }, Error::unknown_planet);
  rejected([](auto& r) { r.rotation.period_ticks = 0; }, Error::invalid_recipe);
  rejected(
      [](auto& r) {
        r.rotation.period_ticks = std::numeric_limits<SimulationTick>::max();
      },
      Error::invalid_recipe);
  rejected([](auto& r) { ++r.rotation.period_ticks; }, Error::invalid_recipe);
  rejected(
      [](auto& r) { r.rotation.epoch_phase_tick = r.rotation.period_ticks; },
      Error::invalid_recipe);
  rejected([](auto& r) { ++r.rotation.epoch_phase_tick; },
           Error::invalid_recipe);
  rejected([](auto& r) { ++r.rotation.tilt_microdegrees; },
           Error::invalid_recipe);
  rejected([](auto& r) { ++r.rotation.pole_azimuth_turns; },
           Error::invalid_recipe);

  auto forged_context = [&](auto change) {
    auto bad = system;
    change(bad);
    const auto before = bad;
    const auto generated =
        generate_planet_rotation_recipe(bad, recipe.rotation.planet);
    const auto resolved = resolve_planet_rotation(bad, recipe, 12345);
    check(!generated && generated.error() == Error::invalid_world_context &&
              !resolved && resolved.error() == Error::invalid_world_context,
          "Both physical APIs reject forged catalog before geometry");
    check(before == bad, "Physical context refusal is read-only");
  };
  forged_context([](auto& s) { ++s.generator_version; });
  forged_context([](auto& s) { ++s.stellar_gm_km3_per_second2; });
  forged_context([](auto& s) { ++s.stellar_mass_millisolar; });
  forged_context(
      [](auto& s) { ++s.catalog.planets.front().orbit.period_ticks; });
  forged_context([](auto& s) { ++s.origin_universe_seed->value; });
  forged_context([](auto& s) { s.origin_universe_seed.reset(); });
  forged_context([](auto& s) { ++s.catalog.star.radius_kilometres; });
  const auto other = required(generate_physical_origin_system(Seed{43}));
  const auto wrong_world = resolve_planet_rotation(other, recipe, 12345);
  check(!wrong_world && wrong_world.error() == Error::owner_mismatch,
        "Another physical universe cannot consume the origin recipe");
  const auto procedural =
      required(generate_physical_local_system(system.catalog.seed));
  const auto alias = required(
      generate_planet_rotation_recipe(procedural, recipe.rotation.planet));
  check(alias != recipe && alias.rotation.planet == recipe.rotation.planet,
        "Same-ID authored/procedural physical owners remain distinct");
  const auto wrong_variant = resolve_planet_rotation(procedural, recipe, 12345);
  check(!wrong_variant && wrong_variant.error() == Error::owner_mismatch,
        "Physical recipe cannot cross authored/procedural variants");
  check(!generate_planet_rotation_recipe(system, recipe.rotation.planet, 0),
        "Physical generation rejects unsupported spin version");
  const auto unknown = generate_planet_rotation_recipe(
      system, PlanetId{recipe.rotation.planet.value + 1});
  check(!unknown && unknown.error() == Error::unknown_planet,
        "Physical generation rejects foreign planet");
  const auto invalid_tick = resolve_planet_rotation(
      system, recipe, std::numeric_limits<SimulationTick>::max());
  check(!invalid_tick && invalid_tick.error() == Error::invalid_tick,
        "Physical rotation rejects reserved tick");
  const double bound = kPlanetRotationMaximumObserverComponentMetres;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    for (const double value :
         {std::numeric_limits<double>::quiet_NaN(),
          std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity(),
          std::nextafter(bound, std::numeric_limits<double>::infinity()),
          -std::nextafter(bound, std::numeric_limits<double>::infinity())}) {
      PlanetFixedPositionMetres observer{};
      const std::array fields{&observer.x, &observer.y, &observer.z};
      *fields[axis] = value;
      const auto result =
          resolve_planet_rotation(system, recipe, 12345, observer);
      check(!result && result.error() == Error::invalid_observer,
            "Physical observer axis rejected at invalid bound");
    }
  }
  check(resolve_planet_rotation(system, recipe, 12345, {bound, -bound, bound})
            .has_value(),
        "Physical observer inclusive component bounds accepted");
  const auto centre = required(resolve_planet_rotation(system, recipe, 12345));
  const auto zero = required(
      resolve_planet_rotation(system, recipe, 12345, {-0.0, -0.0, -0.0}));
  check(zero == centre &&
            !std::signbit(zero.geometry.observer_position_fixed.x),
        "Physical observer signed zeros canonicalized in output");
  const auto coincident = resolve_planet_rotation(
      system, recipe, 12345, centre.geometry.star_position_fixed);
  check(!coincident && coincident.error() == Error::observer_at_star,
        "Physical coincident point-star observer refused");
}
} // namespace

auto main() -> int {
  recipe_contract();
  geometry_contract();
  illumination_contract();
  malformed_contract();
  physical_refusal_contract();
  physical_geometry_contract();
  std::cout << "planet rotation: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
