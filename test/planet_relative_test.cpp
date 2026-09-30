#include "apsis_drift/rigid_frame_handoff.hpp"
#include "apsis_drift/vacuum_dynamics.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
using Code = RigidFrameHandoffErrorCode;
int failures{};

auto check(bool condition, std::string_view message) -> void {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}
template <typename T, typename E>
auto required(std::expected<T, E> result) -> T {
  if (!result) throw std::runtime_error("required fixture/provider failed");
  return *result;
}
auto scalars(RigidBodyState& s) -> std::array<double*, 13> {
  return {&s.position_metres.x,
          &s.position_metres.y,
          &s.position_metres.z,
          &s.orientation.w,
          &s.orientation.x,
          &s.orientation.y,
          &s.orientation.z,
          &s.linear_velocity_metres_per_second.x,
          &s.linear_velocity_metres_per_second.y,
          &s.linear_velocity_metres_per_second.z,
          &s.angular_velocity_radians_per_second.x,
          &s.angular_velocity_radians_per_second.y,
          &s.angular_velocity_radians_per_second.z};
}
auto same_bits(RigidBodyState a, RigidBodyState b) -> bool {
  const auto av = scalars(a), bv = scalars(b);
  for (std::size_t i = 0; i < av.size(); ++i)
    if (std::bit_cast<std::uint64_t>(*av[i]) !=
        std::bit_cast<std::uint64_t>(*bv[i]))
      return false;
  return a.craft == b.craft && a.frame == b.frame && a.tick == b.tick;
}
struct Fixture {
  Fixture(Seed seed = Seed{42}, bool origin = true)
      : system(required(origin ? generate_physical_origin_system(seed)
                               : generate_physical_local_system(seed))),
        station(generate_origin_station(seed)),
        context(system, origin ? &station : nullptr) {}
  PhysicalLocalSystem system;
  OriginStationDescriptor station;
  RigidBodyWorldContext context;
  auto system_frame() const -> RigidCoordinateFrame {
    return {RigidFrameKind::system_inertial, system.catalog.id, {}, {}};
  }
  auto station_frame() const -> RigidCoordinateFrame {
    return {RigidFrameKind::station_relative_inertial,
            system.catalog.id,
            {},
            station.id};
  }
  auto relative_frame(PlanetId planet) const -> RigidCoordinateFrame {
    return {RigidFrameKind::planet_relative_inertial,
            system.catalog.id,
            planet,
            {}};
  }
  auto planet_frame(PlanetId planet) const -> RigidCoordinateFrame {
    return {RigidFrameKind::planet_fixed, system.catalog.id, planet, {}};
  }
  auto state() const -> RigidBodyState {
    RigidBodyState s;
    s.frame = system_frame();
    s.tick = 1234567;
    s.position_metres = {1.25, -2.5, 3.75};
    s.orientation =
        required(normalize_rigid_orientation({.125, .375, .125, .875}));
    s.linear_velocity_metres_per_second = {4, -5, 6};
    s.angular_velocity_radians_per_second = {.125, -.25, .5};
    return s;
  }
};
// Independent matrix oracle: no use of the provider's quaternion composition.
using Matrix = std::array<std::array<double, 3>, 3>;
auto matrix(RigidOrientation q) -> Matrix {
  const double n = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  return {
      {{1 - 2 * (q.y * q.y + q.z * q.z) / n, 2 * (q.x * q.y - q.w * q.z) / n,
        2 * (q.x * q.z + q.w * q.y) / n},
       {2 * (q.x * q.y + q.w * q.z) / n, 1 - 2 * (q.x * q.x + q.z * q.z) / n,
        2 * (q.y * q.z - q.w * q.x) / n},
       {2 * (q.x * q.z - q.w * q.y) / n, 2 * (q.y * q.z + q.w * q.x) / n,
        1 - 2 * (q.x * q.x + q.y * q.y) / n}}};
}
auto transpose(Matrix a) -> Matrix {
  Matrix out{};
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j)
      out[i][j] = a[j][i];
  return out;
}
auto product(Matrix a, Matrix b) -> Matrix {
  Matrix out{};
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j)
      for (std::size_t k = 0; k < 3; ++k)
        out[i][j] += a[i][k] * b[k][j];
  return out;
}
auto apply(Matrix a, RigidVector3 v) -> RigidVector3 {
  return {a[0][0] * v.x + a[0][1] * v.y + a[0][2] * v.z,
          a[1][0] * v.x + a[1][1] * v.y + a[1][2] * v.z,
          a[2][0] * v.x + a[2][1] * v.y + a[2][2] * v.z};
}
auto plus(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto minus(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto cross(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto maxabs(RigidVector3 a) -> double {
  return std::max({std::abs(a.x), std::abs(a.y), std::abs(a.z)});
}
auto close(RigidVector3 a, RigidVector3 b, double tolerance) -> bool {
  return maxabs(minus(a, b)) <= tolerance;
}
auto close(Matrix a, Matrix b) -> bool {
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j)
      if (std::abs(a[i][j] - b[i][j]) > 2e-14) return false;
  return true;
}
auto projection_golden(const Fixture& f) -> void {
  auto source = f.state();
  source.frame = f.relative_frame(f.station.orbit.host_planet);
  source.tick = 120;
  source.orientation = {.5, .5, -.5, .5};
  constexpr std::string_view golden =
      R"({"format":"apsis-drift-rigid-body","version":3,"craft":{"id":"1","version":1},)"
      R"("frame":{"kind":"planet_relative_inertial","system":"677859337506523986","planet":"4853608806881724904","station":null},)"
      R"("tick":"120","position_metres":["1.25","-2.5","3.75"],"orientation_wxyz":["0.5","0.5","-0.5","0.5"],)"
      R"("linear_velocity_metres_per_second":["4","-5","6"],"angular_velocity_radians_per_second":["0.125","-0.25","0.5"],)"
      R"("owner":{"family":"physical_circular","version":1,"generator":1,"source_catalog_generator":1,"ephemeris_version":1,"system_seed":"677859337506523986","catalog_kind":"origin_home","origin_universe_seed":"42"}})";
  check(required(encode_rigid_body_state_json(f.context, source)) == golden,
        "v3 planet-relative compact projection golden");
  // Independent FNV-1a over the documented little-endian v3 domain/fields.
  check(required(rigid_body_state_checksum(f.context, source)) ==
            275273080438876040ULL,
        "v3 independent checksum golden");
  check(same_bits(source,
                  required(decode_rigid_body_state_json(f.context, golden))),
        "v3 golden hydration is exact");
}
auto translation(const Fixture& f) -> void {
  constexpr double eps = std::numeric_limits<double>::epsilon();
  for (const auto& planet : f.system.catalog.planets) {
    const auto id = planet.descriptor.id;
    for (const auto tick :
         {SimulationTick{0}, SimulationTick{1234567},
          planet.orbit.period_ticks - 1, planet.orbit.period_ticks,
          std::numeric_limits<SimulationTick>::max() - 1}) {
      auto source = f.state();
      source.frame = f.relative_frame(id);
      source.tick = tick;
      const auto original = source;
      const auto ephemeris =
          required(resolve_planet_ephemeris(f.system, id, {tick, 0}));
      const RigidVector3 p{ephemeris.position.x, ephemeris.position.y,
                           ephemeris.position.z};
      const RigidVector3 v{ephemeris.velocity.x, ephemeris.velocity.y,
                           ephemeris.velocity.z};
      const auto system = required(
          reframe_rigid_body(f.context, source, {f.system_frame(), tick}));
      check(system.position_metres == plus(source.position_metres, p) &&
                system.linear_velocity_metres_per_second ==
                    plus(source.linear_velocity_metres_per_second, v),
            "planet translation uses final physical p/v once");
      auto preserved = system;
      preserved.frame = source.frame;
      preserved.position_metres = source.position_metres;
      preserved.linear_velocity_metres_per_second =
          source.linear_velocity_metres_per_second;
      check(same_bits(source, preserved),
            "planet translation preserves q/spin/craft/tick exactly");
      const auto reverse =
          required(reframe_rigid_body(f.context, system, {source.frame, tick}));
      check(close(reverse.position_metres, source.position_metres,
                  4 * eps * std::max(1.0, maxabs(p))) &&
                close(reverse.linear_velocity_metres_per_second,
                      source.linear_velocity_metres_per_second,
                      4 * eps * std::max(1.0, maxabs(v))),
            "planet roundtrip accounts for cancellation against system origin");
      const auto resumed = required(decode_rigid_body_state_json(
          f.context,
          required(encode_rigid_body_state_json(f.context, source))));
      check(same_bits(system,
                      required(reframe_rigid_body(f.context, resumed,
                                                  {f.system_frame(), tick}))),
            "v3 next translation resumes exactly");
      const auto system_resumed = required(decode_rigid_body_state_json(
          f.context,
          required(encode_rigid_body_state_json(f.context, system))));
      check(same_bits(reverse,
                      required(reframe_rigid_body(f.context, system_resumed,
                                                  {source.frame, tick}))),
            "v2 next translation into v3 resumes exactly");
      check(same_bits(source, required(reframe_rigid_body(
                                  f.context, source, {source.frame, tick}))) &&
                same_bits(source, original),
            "nonrotating identity/source bits remain exact");
      auto centre = source;
      centre.position_metres = {};
      centre.linear_velocity_metres_per_second = {};
      const auto global = required(
          reframe_rigid_body(f.context, centre, {f.system_frame(), tick}));
      const auto cancellation =
          required(reframe_rigid_body(f.context, global, {source.frame, tick}));
      check(global.position_metres == p &&
                global.linear_velocity_metres_per_second == v &&
                cancellation.position_metres == RigidVector3{} &&
                cancellation.linear_velocity_metres_per_second ==
                    RigidVector3{},
            "co-moving planet centre cancels exactly");
    }
  }
}
auto rotation(const Fixture& f) -> void {
  constexpr double eps = std::numeric_limits<double>::epsilon();
  for (const auto& planet : f.system.catalog.planets) {
    const auto id = planet.descriptor.id;
    const auto recipe = required(generate_planet_rotation_recipe(f.system, id));
    const auto wrap =
        recipe.rotation.period_ticks - recipe.rotation.epoch_phase_tick;
    for (const auto tick :
         {SimulationTick{0}, SimulationTick{1234567}, wrap - 1, wrap, wrap + 1,
          std::numeric_limits<SimulationTick>::max() - 1}) {
      auto source = f.state();
      source.frame = f.planet_frame(id);
      source.tick = tick;
      source.position_metres = {1e6, -2e6, 3e6};
      const auto geometry =
          required(resolve_planet_rotation(f.system, recipe, tick)).geometry;
      const auto r = matrix(geometry.fixed_to_system);
      const RigidVector3 omega{geometry.angular_velocity_radians_per_second.x,
                               geometry.angular_velocity_radians_per_second.y,
                               geometry.angular_velocity_radians_per_second.z};
      const auto relative = required(reframe_rigid_body(
          f.context, source, {f.relative_frame(id), tick}, recipe));
      const double ptol =
          128 * eps * std::max(1.0, maxabs(source.position_metres));
      const double vtol =
          256 * eps *
          std::max({1.0, maxabs(source.linear_velocity_metres_per_second),
                    maxabs(omega) * maxabs(source.position_metres)});
      check(close(relative.position_metres, apply(r, source.position_metres),
                  ptol),
            "relative rotation includes no system translation");
      check(
          close(relative.linear_velocity_metres_per_second,
                plus(apply(r, source.linear_velocity_metres_per_second),
                     cross(omega, apply(r, source.position_metres))),
                vtol),
          "relative rotation includes tangential speed without host velocity");
      check(
          close(matrix(relative.orientation),
                product(r, matrix(source.orientation))) &&
              close(relative.angular_velocity_radians_per_second,
                    plus(source.angular_velocity_radians_per_second,
                         apply(transpose(matrix(relative.orientation)), omega)),
                    2e-14),
          "relative attitude/body spin match independent matrix oracle");
      const auto reverse = required(reframe_rigid_body(
          f.context, relative, {source.frame, tick}, recipe));
      check(
          close(reverse.position_metres, source.position_metres, ptol) &&
              close(reverse.linear_velocity_metres_per_second,
                    source.linear_velocity_metres_per_second, vtol) &&
              close(matrix(reverse.orientation), matrix(source.orientation)) &&
              close(reverse.angular_velocity_radians_per_second,
                    source.angular_velocity_radians_per_second, 2e-14),
          "relative/fixed roundtrip has local-scale bounds");
      const auto resumed = required(decode_rigid_body_state_json(
          f.context,
          required(encode_rigid_body_state_json(f.context, relative))));
      check(same_bits(reverse,
                      required(reframe_rigid_body(
                          f.context, resumed, {source.frame, tick}, recipe))),
            "v3 next rotating handoff resumes exactly with same recipe");
      auto rest = source;
      rest.linear_velocity_metres_per_second = {};
      rest.angular_velocity_radians_per_second = {};
      const auto moving = required(
          reframe_rigid_body(f.context, rest, {relative.frame, tick}, recipe));
      check(close(moving.linear_velocity_metres_per_second,
                  cross(omega, moving.position_metres), vtol),
            "surface-fixed rest has physical tangential nonrotating velocity");
      auto nonspinning = relative;
      nonspinning.angular_velocity_radians_per_second = {};
      const auto fixed = required(reframe_rigid_body(
          f.context, nonspinning, {source.frame, tick}, recipe));
      const auto body_spin =
          apply(transpose(matrix(nonspinning.orientation)), omega);
      check(close(fixed.angular_velocity_radians_per_second,
                  {-body_spin.x, -body_spin.y, -body_spin.z}, 2e-14),
            "inertial nonspin subtracts rotating-frame spin in body axes");
    }
  }
}
auto projection_and_refusals(const Fixture& f) -> void {
  auto source = f.state();
  source.frame = f.relative_frame(f.station.orbit.host_planet);
  const auto text = required(encode_rigid_body_state_json(f.context, source));
  const auto document = Json::parse(text);
  check(document["version"] == 3 &&
            document["frame"]["kind"] == "planet_relative_inertial" &&
            document["owner"]["family"] == "physical_circular",
        "v3 explicitly names physical nonrotating ownership");
  auto resumed = source;
  for (int i = 0; i < 10; ++i) {
    resumed = required(decode_rigid_body_state_json(
        f.context, required(encode_rigid_body_state_json(f.context, resumed))));
    check(same_bits(source, resumed),
          "v3 hydration never normalizes q or edits canonical bits");
  }
  auto wrong = document;
  wrong["version"] = 2;
  check(!decode_rigid_body_state_json(f.context, wrong.dump()),
        "v2 cannot relabel the new frame");
  wrong = document;
  wrong["frame"]["kind"] = "planet_fixed";
  check(!decode_rigid_body_state_json(f.context, wrong.dump()),
        "v3 cannot relabel old rotating meaning");
  wrong = document;
  wrong["frame"]["kind"] = "system_inertial";
  wrong["frame"]["planet"] = nullptr;
  check(!decode_rigid_body_state_json(f.context, wrong.dump()),
        "v3 cannot relabel old system frame");
  const auto legacy = generate_origin_system(Seed{42});
  const RigidBodyWorldContext legacy_context{legacy, &f.station};
  check(!validate_rigid_body_state(legacy_context, source) &&
            !encode_rigid_body_state_json(legacy_context, source) &&
            !rigid_body_state_checksum(legacy_context, source) &&
            !decode_rigid_body_state_json(legacy_context, text),
        "legacy owner cannot admit/project/hash/hydrate new frame");
  const auto reject = [&](const RigidBodyWorldContext& context,
                          RigidBodyState s,
                          const RigidFrameHandoffRequest& request) {
    const auto before = s;
    check(!reframe_rigid_body(context, s, request) && same_bits(s, before),
          "invalid source/destination/clock/pair refuses without mutation");
  };
  reject(f.context, source, {f.station_frame(), source.tick});
  reject(f.context, source,
         {f.planet_frame(f.station.orbit.host_planet), source.tick});
  reject(f.context, source, {f.system_frame(), source.tick + 1});
  auto bad = source;
  bad.frame.planet = PlanetId{0};
  reject(f.context, bad, {f.system_frame(), bad.tick});
  bad = source;
  bad.frame.planet.reset();
  reject(f.context, bad, {f.system_frame(), bad.tick});
  bad = source;
  bad.frame.station = f.station.id;
  reject(f.context, bad, {f.system_frame(), bad.tick});
  bad = source;
  bad.frame.system = SystemId{0};
  reject(f.context, bad, {f.system_frame(), bad.tick});
  bad = source;
  bad.tick = std::numeric_limits<SimulationTick>::max();
  reject(f.context, bad, {f.system_frame(), bad.tick});
  for (std::size_t i = 0; i < 13; ++i)
    for (const double invalid :
         {std::numeric_limits<double>::quiet_NaN(),
          std::numeric_limits<double>::infinity(), -0.0}) {
      bad = source;
      *scalars(bad)[i] = invalid;
      reject(f.context, bad, {f.system_frame(), bad.tick});
    }
  bad = source;
  bad.orientation = {2, 0, 0, 0};
  reject(f.context, bad, {f.system_frame(), bad.tick});
  auto no_planet = f.relative_frame(PlanetId{0});
  reject(f.context, f.state(), {no_planet, source.tick});
  wrong = document;
  wrong["owner"]["origin_universe_seed"] = "43";
  check(!decode_rigid_body_state_json(f.context, wrong.dump()),
        "wrong universe projection refuses");
  wrong = document;
  wrong["owner"]["generator"] = 2;
  check(!decode_rigid_body_state_json(f.context, wrong.dump()),
        "wrong generator projection refuses");
  wrong = document;
  wrong["frame"]["planet"] = "0";
  check(!decode_rigid_body_state_json(f.context, wrong.dump()),
        "unknown planet projection refuses");
  wrong = document;
  wrong["position_metres"][0] = "-0";
  check(!decode_rigid_body_state_json(f.context, wrong.dump()),
        "uncanonical v3 state refuses");
  wrong = document;
  wrong["extra"] = 1;
  check(!decode_rigid_body_state_json(f.context, wrong.dump()),
        "unknown v3 field refuses");
  auto duplicate = text;
  duplicate.insert(1, "\"version\":3,");
  for (const auto& invalid :
       {duplicate, text + std::string(1, '\0'), text + "x",
        std::string(kMaximumRigidBodyDocumentBytes + 1, ' '),
        std::string("[[[[[[[[[[0]]]]]]]]]]")})
    check(!decode_rigid_body_state_json(f.context, invalid),
          "bounded malformed v3 input refuses");
  const auto recipe = required(
      generate_planet_rotation_recipe(f.system, f.station.orbit.host_planet));
  const auto legacy_recipe = required(
      generate_planet_rotation_recipe(legacy, f.station.orbit.host_planet));
  check(!reframe_rigid_body(
            f.context, source,
            {f.planet_frame(f.station.orbit.host_planet), source.tick},
            legacy_recipe),
        "legacy recipe cannot select relative rotation");
  check(
      !reframe_rigid_body(f.context, source, {source.frame, source.tick},
                          recipe),
      "explicit rotation recipe cannot be ignored for a nonrotating identity");
  auto altered = recipe;
  ++altered.owner_version;
  check(!reframe_rigid_body(
            f.context, source,
            {f.planet_frame(f.station.orbit.host_planet), source.tick},
            altered),
        "invalid physical rotation owner refuses");
  for (const auto& planet : f.system.catalog.planets)
    if (planet.descriptor.id != f.station.orbit.host_planet) {
      check(!reframe_rigid_body(
                f.context, source,
                {f.planet_frame(planet.descriptor.id), source.tick}, recipe),
            "cross-planet rotating pair refuses");
      reject(f.context, source,
             {f.relative_frame(planet.descriptor.id), source.tick});
    }
  bad = source;
  check(!advance_vacuum_dynamics(
            f.context, bad, VacuumIntent{},
            VacuumDynamicsRecipe{kCoastingVacuumDynamicsVersion}) &&
            same_bits(bad, source),
        "moving-origin frame cannot opt into force-free system integrator");
  const auto ephemeris = required(resolve_planet_ephemeris(
      f.system, *source.frame.planet, {source.tick, 0}));
  bad = source;
  bad.position_metres.x =
      std::copysign(kRigidBodyMaximumPositionMetres, ephemeris.position.x);
  reject(f.context, bad, {f.system_frame(), source.tick});
  bad = source;
  bad.linear_velocity_metres_per_second.x = std::copysign(
      kRigidBodyMaximumVelocityMetresPerSecond, ephemeris.velocity.x);
  reject(f.context, bad, {f.system_frame(), source.tick});
  bad = source;
  bad.frame = f.planet_frame(f.station.orbit.host_planet);
  bad.position_metres = {1e14, 0, 0};
  const auto excessive =
      reframe_rigid_body(f.context, bad, {source.frame, bad.tick}, recipe);
  check(!excessive && excessive.error().code == Code::invalid_result,
        "valid fixed radius can exceed planet-relative tangential velocity "
        "bound");
}
} // namespace

auto main() -> int {
  try {
    const Fixture f;
    projection_golden(f);
    projection_and_refusals(f);
    for (const Seed seed : {Seed{0}, Seed{42}, Seed{18446744073709551615ULL}}) {
      const Fixture origin{seed};
      translation(origin);
      rotation(origin);
      const Fixture procedural{seed, false};
      translation(procedural);
      rotation(procedural);
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "planet relative: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
