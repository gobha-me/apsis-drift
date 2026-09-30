#include "apsis_drift/rigid_frame_handoff.hpp"

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
auto physical_golden(const Fixture& f) -> void {
  auto source = f.state();
  source.tick = 120;
  source.orientation = {.5, .5, -.5, .5};
  constexpr std::string_view golden =
      R"({"format":"apsis-drift-rigid-body","version":2,"craft":{"id":"1","version":1},)"
      R"("frame":{"kind":"system_inertial","system":"677859337506523986","planet":null,"station":null},)"
      R"("tick":"120","position_metres":["1.25","-2.5","3.75"],"orientation_wxyz":["0.5","0.5","-0.5","0.5"],)"
      R"("linear_velocity_metres_per_second":["4","-5","6"],"angular_velocity_radians_per_second":["0.125","-0.25","0.5"],)"
      R"("owner":{"family":"physical_circular","version":1,"generator":1,"source_catalog_generator":1,"ephemeris_version":1,"system_seed":"677859337506523986","catalog_kind":"origin_home","origin_universe_seed":"42"}})";
  check(required(encode_rigid_body_state_json(f.context, source)) == golden,
        "physical v2 compact projection golden is exact");
  // Independently calculated FNV-1a over the documented fixed-width LE recipe.
  check(required(rigid_body_state_checksum(f.context, source)) ==
            384458611037776355ULL,
        "physical v2 checksum golden");
  check(same_bits(source,
                  required(decode_rigid_body_state_json(f.context, golden))),
        "physical golden hydrates without mutation");
}
auto owner_and_projection(const Fixture& f) -> void {
  const auto source = f.state();
  const auto original = source;
  const auto encoded =
      required(encode_rigid_body_state_json(f.context, source));
  const auto document = Json::parse(encoded);
  check(document["version"] == 2 &&
            document["owner"]["family"] == "physical_circular" &&
            document["owner"]["origin_universe_seed"] == "42",
        "projection retains explicit physical owner");
  auto resumed = source;
  for (int i = 0; i < 10; ++i) {
    resumed = required(decode_rigid_body_state_json(
        f.context, required(encode_rigid_body_state_json(f.context, resumed))));
    check(
        same_bits(resumed, source),
        "hydration preserves all canonical bits including quaternion residual");
  }
  const RigidBodyWorldContext unwrapped{f.system.catalog, &f.station};
  check(!validate_rigid_body_state(unwrapped, source),
        "embedded physical catalog cannot implicitly opt in");
  const auto legacy = generate_origin_system(Seed{42});
  const RigidBodyWorldContext legacy_context{legacy, &f.station};
  check(legacy.id == source.frame.system,
        "cross-family fixture has identical system ID");
  check(!decode_rigid_body_state_json(legacy_context, encoded),
        "physical document refuses legacy owner");
  check(!decode_rigid_body_state_json(
            f.context,
            required(encode_rigid_body_state_json(legacy_context, source))),
        "legacy document refuses physical owner");
  check(required(rigid_body_state_checksum(f.context, source)) !=
            required(rigid_body_state_checksum(legacy_context, source)),
        "checksum separates catalog meanings at identical numeric IDs");
  const auto reject_owner = [&](const PhysicalLocalSystem& bad_owner) {
    const RigidBodyWorldContext context{bad_owner, &f.station};
    check(!validate_rigid_body_state(context, source),
          "altered physical owner refuses");
    check(!encode_rigid_body_state_json(context, source),
          "invalid owner cannot project");
    check(!decode_rigid_body_state_json(context, encoded),
          "invalid owner cannot hydrate");
    check(!rigid_body_state_checksum(context, source),
          "invalid owner cannot checksum");
  };
  const auto reject_changed = [&](auto mutation) {
    auto owner = f.system;
    mutation(owner);
    reject_owner(owner);
  };
  reject_changed([](auto& owner) { ++owner.generator_version; });
  reject_changed([](auto& owner) { ++owner.source_catalog_generator; });
  reject_changed([](auto& owner) { ++owner.ephemeris_version; });
  reject_changed([](auto& owner) { owner.origin_universe_seed = Seed{43}; });
  reject_changed([](auto& owner) { ++owner.stellar_gm_km3_per_second2; });
  reject_owner(PhysicalLocalSystem{
      f.system.generator_version, f.system.source_catalog_generator,
      f.system.ephemeris_version, f.system.origin_universe_seed, legacy,
      f.system.stellar_mass_millisolar, f.system.stellar_gm_km3_per_second2});
  auto bad_orbit = f.station.orbit;
  ++bad_orbit.period_ticks;
  const OriginStationDescriptor bad_station{
      f.station.universe_seed, f.station.home_system_seed,
      f.station.station_seed, f.station.id, bad_orbit};
  check(!validate_rigid_body_state({f.system, &bad_station}, source),
        "forged supplied station refuses even system frame");
  const auto other_station = generate_origin_station(Seed{43});
  check(!validate_rigid_body_state({f.system, &other_station}, source),
        "wrong universe station refuses");
  const auto procedural = required(generate_physical_local_system(Seed{42}));
  RigidBodyState procedural_state = source;
  procedural_state.frame.system = procedural.catalog.id;
  const RigidBodyWorldContext procedural_context{procedural};
  const auto procedural_json = required(
      encode_rigid_body_state_json(procedural_context, procedural_state));
  check(Json::parse(procedural_json)["owner"]["origin_universe_seed"].is_null(),
        "procedural owner explicitly has no origin universe");
  check(same_bits(procedural_state, required(decode_rigid_body_state_json(
                                        procedural_context, procedural_json))),
        "procedural physical projection roundtrip");
  check(same_bits(source, original), "refusals never mutate source");
  const auto reject_json = [&](const Json& invalid) {
    check(!decode_rigid_body_state_json(f.context, invalid.dump()),
          "malformed or mismatched physical projection refuses");
  };
  for (const auto key : {"version", "generator", "source_catalog_generator",
                         "ephemeris_version"}) {
    auto invalid = document;
    invalid["owner"][key] = 2;
    reject_json(invalid);
    invalid = document;
    invalid["owner"].erase(key);
    reject_json(invalid);
    invalid = document;
    invalid["owner"][key] = "1";
    reject_json(invalid);
  }
  for (const auto key :
       {"family", "system_seed", "catalog_kind", "origin_universe_seed"}) {
    auto invalid = document;
    invalid["owner"][key] = "wrong";
    reject_json(invalid);
    invalid = document;
    invalid["owner"].erase(key);
    reject_json(invalid);
  }
  auto invalid = document;
  invalid["owner"]["origin_universe_seed"] = nullptr;
  reject_json(invalid);
  invalid = document;
  invalid["owner"]["origin_universe_seed"] = "43";
  reject_json(invalid);
  invalid = document;
  invalid["owner"]["system_seed"] = "0";
  reject_json(invalid);
  invalid = document;
  invalid["owner"]["extra"] = 1;
  reject_json(invalid);
  invalid = document;
  invalid.erase("owner");
  reject_json(invalid);
  invalid = document;
  invalid["version"] = 1;
  reject_json(invalid);
  invalid = document;
  invalid["frame"]["system"] = "0";
  reject_json(invalid);
  invalid = document;
  invalid["frame"]["kind"] = "planet_fixed";
  invalid["frame"]["planet"] = "0";
  reject_json(invalid);
  invalid = document;
  invalid["tick"] = "18446744073709551615";
  reject_json(invalid);
  invalid = document;
  invalid["position_metres"][0] = "-0";
  reject_json(invalid);
  for (const auto value : {"nan", "inf", "1.0", "1e999", "01"}) {
    invalid = document;
    invalid["position_metres"][0] = value;
    reject_json(invalid);
  }
  for (const auto& text :
       {std::string("{"), encoded + "x", encoded + std::string(1, '\0'),
        std::string(kMaximumRigidBodyDocumentBytes + 1, ' '),
        std::string("{\"version\":2,\"version\":2}"),
        std::string("[[[[[[[[[[0]]]]]]]]]]")})
    check(!decode_rigid_body_state_json(f.context, text),
          "bounded malformed input refuses");
  auto duplicate = encoded;
  const auto at = duplicate.find("\"family\":");
  duplicate.insert(at, "\"family\":\"physical_circular\",");
  check(!decode_rigid_body_state_json(f.context, duplicate),
        "nested duplicate owner field refuses");
}
auto numerical_refusals(const Fixture& f) -> void {
  const auto source = f.state();
  for (std::size_t i = 0; i < 13; ++i) {
    for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(), -0.0}) {
      auto invalid = source;
      *scalars(invalid)[i] = bad;
      check(!validate_rigid_body_state(f.context, invalid),
            "all nonfinite/signedzero components refuse");
      check(!reframe_rigid_body(f.context, invalid,
                                {f.station_frame(), invalid.tick}),
            "invalid source cannot hand off");
    }
  }
  for (std::size_t i = 0; i < 13; ++i) {
    auto invalid = source;
    const double bound = i < 3   ? kRigidBodyMaximumPositionMetres
                         : i < 7 ? 2.0
                         : i < 10
                             ? kRigidBodyMaximumVelocityMetresPerSecond
                             : kRigidBodyMaximumAngularVelocityRadiansPerSecond;
    *scalars(invalid)[i] =
        std::nextafter(bound, std::numeric_limits<double>::infinity());
    check(!validate_rigid_body_state(f.context, invalid),
          "over-bound components refuse");
  }
  auto invalid = source;
  invalid.orientation = {1.01, 0, 0, 0};
  check(!validate_rigid_body_state(f.context, invalid),
        "nonunit attitude refuses");
  invalid = source;
  invalid.orientation = {-source.orientation.w, -source.orientation.x,
                         -source.orientation.y, -source.orientation.z};
  check(!validate_rigid_body_state(f.context, invalid),
        "uncanonical quaternion sign refuses");
  check(same_bits(required(canonicalize_rigid_body_state(f.context, invalid)),
                  source),
        "explicit canonicalization accepts only sign alias");
  invalid = source;
  invalid.tick = std::numeric_limits<SimulationTick>::max();
  check(!validate_rigid_body_state(f.context, invalid),
        "reserved tick refuses");
  invalid = source;
  ++invalid.craft.version;
  check(!validate_rigid_body_state(f.context, invalid),
        "unsupported craft refuses");
  const auto stale = reframe_rigid_body(f.context, source,
                                        {f.station_frame(), source.tick + 1});
  check(!stale && stale.error().code == Code::stale_tick,
        "stale handoff refuses");
  auto wrong = f.station_frame();
  wrong.planet = f.station.orbit.host_planet;
  check(!reframe_rigid_body(f.context, source, {wrong, source.tick}),
        "contradictory destination refuses");
  wrong = f.station_frame();
  wrong.station = OriginStationId{0};
  check(!reframe_rigid_body(f.context, source, {wrong, source.tick}),
        "unknown station refuses");
  wrong = f.system_frame();
  wrong.system = SystemId{0};
  check(!reframe_rigid_body(f.context, source, {wrong, source.tick}),
        "unknown system refuses");
}
auto station_handoffs(const Fixture& f) -> void {
  constexpr double eps = std::numeric_limits<double>::epsilon();
  for (const auto tick : {SimulationTick{0}, SimulationTick{1234567},
                          std::numeric_limits<SimulationTick>::max() - 1}) {
    auto source = f.state();
    source.frame = f.station_frame();
    source.tick = tick;
    const auto original = source;
    const auto ephemeris = required(
        resolve_origin_station_ephemeris(f.system, f.station, {tick, 0}));
    const RigidVector3 p{ephemeris.position.x, ephemeris.position.y,
                         ephemeris.position.z};
    const RigidVector3 v{ephemeris.velocity.x, ephemeris.velocity.y,
                         ephemeris.velocity.z};
    const auto system = required(
        reframe_rigid_body(f.context, source, {f.system_frame(), tick}));
    check(system.position_metres == plus(source.position_metres, p) &&
              system.linear_velocity_metres_per_second ==
                  plus(source.linear_velocity_metres_per_second, v),
          "station handoff uses final physical p/v exactly");
    auto preserved = system;
    preserved.frame = source.frame;
    preserved.position_metres = source.position_metres;
    preserved.linear_velocity_metres_per_second =
        source.linear_velocity_metres_per_second;
    check(same_bits(source, preserved),
          "station handoff preserves craft/tick/q/body spin bits");
    const auto reverse = required(
        reframe_rigid_body(f.context, system, {f.station_frame(), tick}));
    check(close(reverse.position_metres, source.position_metres,
                4 * eps * std::max(1.0, maxabs(p))) &&
              close(reverse.linear_velocity_metres_per_second,
                    source.linear_velocity_metres_per_second,
                    4 * eps * std::max(1.0, maxabs(v))),
          "station roundtrip bounds physical-origin cancellation");
    const auto resumed = required(decode_rigid_body_state_json(
        f.context, required(encode_rigid_body_state_json(f.context, system))));
    check(same_bits(reverse,
                    required(reframe_rigid_body(f.context, resumed,
                                                {f.station_frame(), tick}))),
          "next station handoff resumes exactly");
    check(same_bits(source, required(reframe_rigid_body(
                                f.context, source, {source.frame, tick}))) &&
              same_bits(source, original),
          "station identity/source bit exact");
    auto rest = source;
    rest.position_metres = {};
    rest.linear_velocity_metres_per_second = {};
    const auto origin =
        required(reframe_rigid_body(f.context, rest, {f.system_frame(), tick}));
    const auto cancellation = required(
        reframe_rigid_body(f.context, origin, {f.station_frame(), tick}));
    check(cancellation.position_metres == RigidVector3{} &&
              cancellation.linear_velocity_metres_per_second == RigidVector3{},
          "co-moving station origin cancels exactly");
  }
  const auto ephemeris = required(
      resolve_origin_station_ephemeris(f.system, f.station, {1234567, 0}));
  for (const bool to_system : {true, false}) {
    auto source = f.state();
    source.frame = to_system ? f.station_frame() : f.system_frame();
    const auto destination = to_system ? f.system_frame() : f.station_frame();
    source.position_metres.x =
        std::copysign(kRigidBodyMaximumPositionMetres,
                      to_system ? ephemeris.position.x : -ephemeris.position.x);
    const auto position =
        reframe_rigid_body(f.context, source, {destination, source.tick});
    check(!position && position.error().code == Code::invalid_result,
          "valid station source can exceed resulting position bound");
    source = f.state();
    source.frame = to_system ? f.station_frame() : f.system_frame();
    source.linear_velocity_metres_per_second.x =
        std::copysign(kRigidBodyMaximumVelocityMetresPerSecond,
                      to_system ? ephemeris.velocity.x : -ephemeris.velocity.x);
    const auto velocity =
        reframe_rigid_body(f.context, source, {destination, source.tick});
    check(!velocity && velocity.error().code == Code::invalid_result,
          "valid station source can exceed resulting velocity bound");
  }
}
auto rotating_handoffs(const Fixture& f) -> void {
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
      source.tick = tick;
      source.frame = f.planet_frame(id);
      source.position_metres = {1e6, -2e6, 3e6};
      const auto original = source;
      const auto g =
          required(resolve_planet_rotation(f.system, recipe, tick)).geometry;
      const auto r = matrix(g.fixed_to_system);
      const RigidVector3 p{g.planet_position.x, g.planet_position.y,
                           g.planet_position.z};
      const RigidVector3 v{g.planet_velocity.x, g.planet_velocity.y,
                           g.planet_velocity.z};
      const RigidVector3 omega{g.angular_velocity_radians_per_second.x,
                               g.angular_velocity_radians_per_second.y,
                               g.angular_velocity_radians_per_second.z};
      const auto system = required(reframe_rigid_body(
          f.context, source, {f.system_frame(), tick}, recipe));
      const double ptol =
          128 * eps *
          std::max({1.0, maxabs(source.position_metres), maxabs(p)});
      const double vtol =
          256 * eps *
          std::max({1.0, maxabs(source.linear_velocity_metres_per_second),
                    maxabs(v),
                    maxabs(omega) *
                        std::max(maxabs(source.position_metres), maxabs(p))});
      check(close(system.position_metres,
                  plus(p, apply(r, source.position_metres)), ptol),
            "physical rotation p matches independent matrix oracle");
      check(close(system.linear_velocity_metres_per_second,
                  plus(plus(v,
                            apply(r, source.linear_velocity_metres_per_second)),
                       cross(omega, apply(r, source.position_metres))),
                  vtol),
            "physical rotation v includes translation and omega cross r");
      check(close(matrix(system.orientation),
                  product(r, matrix(source.orientation))),
            "physical attitude matches independent matrix composition");
      check(close(system.angular_velocity_radians_per_second,
                  plus(source.angular_velocity_radians_per_second,
                       apply(transpose(matrix(system.orientation)), omega)),
                  2e-14),
            "physical frame spin resolved in body axes");
      const auto reverse = required(
          reframe_rigid_body(f.context, system, {source.frame, tick}, recipe));
      check(
          close(reverse.position_metres, source.position_metres, ptol) &&
              close(reverse.linear_velocity_metres_per_second,
                    source.linear_velocity_metres_per_second, vtol) &&
              close(matrix(reverse.orientation), matrix(source.orientation)) &&
              close(reverse.angular_velocity_radians_per_second,
                    source.angular_velocity_radians_per_second, 2e-14),
          "physical rotating roundtrip bounded");
      check(system.craft == source.craft && system.tick == source.tick &&
                same_bits(source, original),
            "rotation preserves craft/tick and const source");
      check(same_bits(source,
                      required(reframe_rigid_body(
                          f.context, source, {source.frame, tick}, recipe))),
            "validated physical rotating identity bit exact");
      const auto resumed = required(decode_rigid_body_state_json(
          f.context,
          required(encode_rigid_body_state_json(f.context, system))));
      check(same_bits(reverse,
                      required(reframe_rigid_body(
                          f.context, resumed, {source.frame, tick}, recipe))),
            "exact next rotating handoff after hydration");
    }
  }
}
auto rotating_refusals(const Fixture& f) -> void {
  const auto recipe = required(
      generate_planet_rotation_recipe(f.system, f.station.orbit.host_planet));
  auto source = f.state();
  source.frame = f.planet_frame(recipe.rotation.planet);
  const auto request = RigidFrameHandoffRequest{f.system_frame(), source.tick};
  const auto reject = [&](const auto& context, const auto& s,
                          const RigidFrameHandoffRequest& req,
                          const auto& selected) {
    check(!reframe_rigid_body(context, s, req, selected),
          "wrong owner/recipe/frame/clock/result refuses rotation");
  };
  const auto legacy = generate_origin_system(Seed{42});
  const RigidBodyWorldContext legacy_context{legacy, &f.station};
  const auto legacy_recipe =
      required(generate_planet_rotation_recipe(legacy, recipe.rotation.planet));
  reject(f.context, source, request, legacy_recipe);
  reject(legacy_context, source, request, recipe);
  reject(f.context, source, {source.frame, source.tick}, legacy_recipe);
  auto bad = recipe;
  ++bad.owner_version;
  reject(f.context, source, request, bad);
  bad = recipe;
  ++bad.physical_catalog_generator;
  reject(f.context, source, request, bad);
  bad = recipe;
  ++bad.source_catalog_generator;
  reject(f.context, source, request, bad);
  bad = recipe;
  ++bad.ephemeris_version;
  reject(f.context, source, request, bad);
  bad = recipe;
  bad.origin_universe_seed = Seed{43};
  reject(f.context, source, request, bad);
  bad = recipe;
  ++bad.rotation.version;
  reject(f.context, source, request, bad);
  bad = recipe;
  ++bad.rotation.period_ticks;
  reject(f.context, source, request, bad);
  bad = recipe;
  bad.rotation.system = SystemId{0};
  reject(f.context, source, request, bad);
  bad = recipe;
  bad.rotation.planet = PlanetId{0};
  reject(f.context, source, request, bad);
  reject(f.context, source, {f.station_frame(), source.tick}, recipe);
  reject(f.context, source, {f.system_frame(), source.tick + 1}, recipe);
  auto invalid = source;
  invalid.tick = std::numeric_limits<SimulationTick>::max();
  reject(f.context, invalid, {f.system_frame(), invalid.tick}, recipe);
  check(!reframe_rigid_body(f.context, source, request),
        "no-recipe planetary transition remains unsupported");
  invalid = source;
  invalid.position_metres = {1e14, 0, 0};
  reject(f.context, invalid, request, recipe);
  invalid = source;
  invalid.position_metres = {1e15, 1e15, 1e15};
  reject(f.context, invalid, request, recipe);
}
} // namespace

auto main() -> int {
  try {
    const Fixture f;
    physical_golden(f);
    owner_and_projection(f);
    numerical_refusals(f);
    station_handoffs(f);
    rotating_handoffs(f);
    rotating_refusals(f);
    for (const Seed seed : {Seed{0}, Seed{18446744073709551615ULL}}) {
      const Fixture origin{seed};
      station_handoffs(origin);
      rotating_handoffs(origin);
      const Fixture procedural{seed, false};
      rotating_handoffs(procedural);
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "physical rigid state and handoffs: " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
