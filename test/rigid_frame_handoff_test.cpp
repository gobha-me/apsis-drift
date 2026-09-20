#include "apsis_drift/rigid_frame_handoff.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <source_location>
#include <string_view>

namespace {
using namespace apsis_drift;
using Code = RigidFrameHandoffErrorCode;
int failures{};

auto check(bool condition, std::string_view message) -> void {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

template <typename T, typename E>
auto required(const std::expected<T, E>& result,
              std::source_location location = std::source_location::current())
    -> T {
  if (!result) {
    std::cerr << "FAIL: fixture API rejected valid input at " << location.line()
              << '\n';
    std::exit(1);
  }
  return *result;
}

auto components(RigidVector3 vector) -> std::array<double, 3> {
  return {vector.x, vector.y, vector.z};
}

auto bits(double a, double b) -> bool {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

auto same_bits(const RigidBodyState& a, const RigidBodyState& b) -> bool {
  if (a.craft != b.craft || a.frame != b.frame || a.tick != b.tick)
    return false;
  const std::array av{a.position_metres.x,
                      a.position_metres.y,
                      a.position_metres.z,
                      a.orientation.w,
                      a.orientation.x,
                      a.orientation.y,
                      a.orientation.z,
                      a.linear_velocity_metres_per_second.x,
                      a.linear_velocity_metres_per_second.y,
                      a.linear_velocity_metres_per_second.z,
                      a.angular_velocity_radians_per_second.x,
                      a.angular_velocity_radians_per_second.y,
                      a.angular_velocity_radians_per_second.z};
  const std::array bv{b.position_metres.x,
                      b.position_metres.y,
                      b.position_metres.z,
                      b.orientation.w,
                      b.orientation.x,
                      b.orientation.y,
                      b.orientation.z,
                      b.linear_velocity_metres_per_second.x,
                      b.linear_velocity_metres_per_second.y,
                      b.linear_velocity_metres_per_second.z,
                      b.angular_velocity_radians_per_second.x,
                      b.angular_velocity_radians_per_second.y,
                      b.angular_velocity_radians_per_second.z};
  for (std::size_t i = 0; i < av.size(); ++i)
    if (!bits(av[i], bv[i])) return false;
  return true;
}

struct Fixture {
  LocalSystemDescriptor system{generate_origin_system(Seed{42})};
  OriginStationDescriptor station{generate_origin_station(Seed{42})};
  RigidBodyWorldContext context{system, &station};

  auto system_frame() const -> RigidCoordinateFrame {
    return {RigidFrameKind::system_inertial, system.id, {}, {}};
  }
  auto station_frame() const -> RigidCoordinateFrame {
    return {
        RigidFrameKind::station_relative_inertial, system.id, {}, station.id};
  }
  auto planet_frame() const -> RigidCoordinateFrame {
    return {RigidFrameKind::planet_fixed,
            system.id,
            system.planets.front().descriptor.id,
            {}};
  }
  auto state() const -> RigidBodyState {
    RigidBodyState state;
    state.frame = station_frame();
    state.tick = 1234567;
    state.position_metres = {1.125, -2.375, 3.625};
    state.orientation =
        required(normalize_rigid_orientation({.7, -.2, .4, .5}));
    state.linear_velocity_metres_per_second = {.3125, -4.125, 7.25};
    state.angular_velocity_radians_per_second = {.15, -.35, .625};
    return state;
  }
};

auto expect_error(const RigidBodyWorldContext& context,
                  const RigidBodyState& state,
                  const RigidFrameHandoffRequest& request, Code code,
                  std::optional<RigidBodyError> detail = {}) -> void {
  const auto before = state;
  const auto original_request = request;
  const auto result = reframe_rigid_body(context, state, request);
  check(!result && result.error().code == code,
        "handoff rejects with expected error category");
  if (!result && detail)
    check(result.error().state_error == detail,
          "validation preserves detailed reason");
  check(same_bits(before, state), "failure preserves every source bit");
  check(request.destination == original_request.destination &&
            request.tick == original_request.tick,
        "failure preserves request");
}

auto roundtrip_bound(double actual, double original, double origin) -> bool {
  // Two correctly rounded binary64 +/- operations, with ample margin for
  // cancellation: no relative-to-the-small-local-offset accuracy promise.
  const double bound = 4 * std::numeric_limits<double>::epsilon() *
                       std::max({1.0, std::abs(original), std::abs(origin)});
  return std::abs(actual - original) <= bound;
}

auto transforms(const Fixture& fixture) -> void {
  const auto initial = fixture.state();
  std::array<std::uint64_t, 3> hashes{};
  const std::array ticks{SimulationTick{0}, SimulationTick{1234567},
                         std::numeric_limits<SimulationTick>::max() - 1};
  for (std::size_t i = 0; i < ticks.size(); ++i) {
    auto source = initial;
    source.tick = ticks[i];
    const auto before = source;
    const auto ephemeris = required(resolve_origin_station_ephemeris(
        fixture.system, fixture.station, {source.tick, 0}));
    const auto result = required(reframe_rigid_body(
        fixture.context, source, {fixture.system_frame(), source.tick}));
    auto expected = source;
    expected.frame = fixture.system_frame();
    expected.position_metres = {source.position_metres.x + ephemeris.position.x,
                                source.position_metres.y + ephemeris.position.y,
                                source.position_metres.z +
                                    ephemeris.position.z};
    expected.linear_velocity_metres_per_second = {
        source.linear_velocity_metres_per_second.x + ephemeris.velocity.x,
        source.linear_velocity_metres_per_second.y + ephemeris.velocity.y,
        source.linear_velocity_metres_per_second.z + ephemeris.velocity.z};
    expected =
        required(canonicalize_rigid_body_state(fixture.context, expected));
    check(same_bits(result, expected),
          "same-tick final station position and velocity added exactly once");
    check(same_bits(source, before), "successful handoff is non-mutating");
    auto unchanged = result;
    unchanged.frame = source.frame;
    unchanged.position_metres = source.position_metres;
    unchanged.linear_velocity_metres_per_second =
        source.linear_velocity_metres_per_second;
    check(same_bits(unchanged, source),
          "q, body spin, craft and tick retain bits");
    const auto back = required(reframe_rigid_body(fixture.context, result,
                                                  {source.frame, source.tick}));
    const auto p = components(source.position_metres);
    const auto bp = components(back.position_metres);
    const auto v = components(source.linear_velocity_metres_per_second);
    const auto bv = components(back.linear_velocity_metres_per_second);
    const std::array op{ephemeris.position.x, ephemeris.position.y,
                        ephemeris.position.z};
    const std::array ov{ephemeris.velocity.x, ephemeris.velocity.y,
                        ephemeris.velocity.z};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      check(roundtrip_bound(bp[axis], p[axis], op[axis]),
            "position roundtrip absolute bound");
      check(roundtrip_bound(bv[axis], v[axis], ov[axis]),
            "velocity roundtrip absolute bound");
    }
    // Save projection is existing strict native-state JSON, not a career save
    // upgrade. Resume on either side produces exactly the same next handoff.
    for (const auto& saved : {source, result}) {
      const auto json =
          required(encode_rigid_body_state_json(fixture.context, saved));
      const auto loaded =
          required(decode_rigid_body_state_json(fixture.context, json));
      const auto destination =
          saved.frame == source.frame ? result.frame : source.frame;
      check(same_bits(required(reframe_rigid_body(fixture.context, saved,
                                                  {destination, saved.tick})),
                      required(reframe_rigid_body(fixture.context, loaded,
                                                  {destination, loaded.tick}))),
            "JSON resume preserves exact handoff continuation");
    }
    hashes[i] = required(rigid_body_state_checksum(fixture.context, result));
  }
  for (const auto hash : hashes)
    std::cout << "handoff checksum " << hash << '\n';
  // Observed equal with GCC and Clang; analytical component assertions above
  // independently establish the transform rather than relying on these hashes.
  constexpr std::array<std::uint64_t, 3> golden{
      2880890648700871415ULL, 12164003416601128854ULL, 14594472587768856570ULL};
  check(hashes == golden, "cross-compiler handoff state checksum fixtures");

  // A system state exactly moving with the station maps to rest at its origin;
  // orbital angular speed of the station does NOT rotate these aligned axes.
  auto moving = initial;
  moving.frame = fixture.system_frame();
  const auto ephemeris = required(resolve_origin_station_ephemeris(
      fixture.system, fixture.station, {moving.tick, 0}));
  moving.position_metres = {ephemeris.position.x, ephemeris.position.y,
                            ephemeris.position.z};
  moving.linear_velocity_metres_per_second = {
      ephemeris.velocity.x, ephemeris.velocity.y, ephemeris.velocity.z};
  const auto rest = required(reframe_rigid_body(
      fixture.context, moving, {fixture.station_frame(), moving.tick}));
  for (const auto value : components(rest.position_metres))
    check(bits(value, 0.0), "station origin becomes canonical zero");
  for (const auto value : components(rest.linear_velocity_metres_per_second))
    check(bits(value, 0.0), "station motion cancels to canonical zero");
  check(rest.angular_velocity_radians_per_second ==
            moving.angular_velocity_radians_per_second,
        "no fictitious station angular-velocity subtraction");

  // The legal state contract permits a small norm residual; a frame translation
  // must not silently normalize it. Also exercise system->station->system
  // starting at non-binary local offsets rather than only reversing the first
  // station->system fixtures.
  for (const auto tick :
       {SimulationTick{1}, fixture.station.orbit.period_ticks - 1,
        fixture.station.orbit.period_ticks,
        fixture.station.orbit.period_ticks + 1}) {
    auto system_state = initial;
    system_state.tick = tick;
    system_state.frame = fixture.system_frame();
    system_state.orientation = {std::nextafter(1.0, 2.0), 0, 0, 0};
    system_state.position_metres = {.1, -98765.4321, 1.23456789e12};
    system_state.linear_velocity_metres_per_second = {-.1, 12345.6789,
                                                      -87654.321};
    const auto local = required(reframe_rigid_body(
        fixture.context, system_state, {fixture.station_frame(), tick}));
    const auto restored = required(reframe_rigid_body(
        fixture.context, local, {fixture.system_frame(), tick}));
    check(bits(local.orientation.w, system_state.orientation.w) &&
              bits(restored.orientation.w, system_state.orientation.w),
          "accepted nonunit norm residual is never renormalized");
    const auto station = required(resolve_origin_station_ephemeris(
        fixture.system, fixture.station, {tick, 0}));
    const auto p = components(system_state.position_metres);
    const auto rp = components(restored.position_metres);
    const auto v = components(system_state.linear_velocity_metres_per_second);
    const auto rv = components(restored.linear_velocity_metres_per_second);
    const std::array origin{station.position.x, station.position.y,
                            station.position.z};
    const std::array speed{station.velocity.x, station.velocity.y,
                           station.velocity.z};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      check(roundtrip_bound(rp[axis], p[axis], origin[axis]),
            "reverse position roundtrip bound at station phase wrap");
      check(roundtrip_bound(rv[axis], v[axis], speed[axis]),
            "reverse velocity roundtrip bound at station phase wrap");
    }
  }
}

auto validation(const Fixture& fixture) -> void {
  const auto source = fixture.state();
  const RigidFrameHandoffRequest request{fixture.system_frame(), source.tick};
  for (const auto frame : {fixture.system_frame(), fixture.station_frame(),
                           fixture.planet_frame()}) {
    auto state = source;
    state.frame = frame;
    check(same_bits(required(reframe_rigid_body(fixture.context, state,
                                                {frame, state.tick})),
                    state),
          "validated same-frame request retains all bits");
  }
  expect_error(fixture.context, source, {request.destination, source.tick + 1},
               Code::stale_tick);
  expect_error(fixture.context, source, {request.destination, source.tick - 1},
               Code::stale_tick);
  expect_error(fixture.context, source, {fixture.planet_frame(), source.tick},
               Code::unsupported_frame_pair);
  auto planet = source;
  planet.frame = fixture.planet_frame();
  expect_error(fixture.context, planet, request, Code::unsupported_frame_pair);
  auto destination = fixture.station_frame();
  ++destination.station->value;
  expect_error(fixture.context, source, {destination, source.tick},
               Code::invalid_destination, RigidBodyError::unknown_station);
  destination = request.destination;
  ++destination.system.value;
  expect_error(fixture.context, source, {destination, source.tick},
               Code::invalid_destination, RigidBodyError::unknown_system);
  destination = request.destination;
  destination.station = fixture.station.id;
  expect_error(fixture.context, source, {destination, source.tick},
               Code::invalid_destination,
               RigidBodyError::invalid_coordinate_frame);
  destination = request.destination;
  destination.kind = static_cast<RigidFrameKind>(255);
  expect_error(fixture.context, source, {destination, source.tick},
               Code::invalid_destination,
               RigidBodyError::invalid_coordinate_frame);

  for (const double value : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity(), -0.0,
                             kRigidBodyMaximumPositionMetres * 2}) {
    auto bad = source;
    bad.position_metres.x = value;
    expect_error(fixture.context, bad, request, Code::invalid_source);
    expect_error(fixture.context, bad, {bad.frame, bad.tick},
                 Code::invalid_source);
  }
  for (const auto orientation :
       {RigidOrientation{0, 0, 0, 0}, RigidOrientation{-1, 0, 0, 0},
        RigidOrientation{2, 0, 0, 0}}) {
    auto bad = source;
    bad.orientation = orientation;
    expect_error(fixture.context, bad, request, Code::invalid_source);
  }
  auto bad = source;
  bad.tick = std::numeric_limits<SimulationTick>::max();
  expect_error(fixture.context, bad, {request.destination, bad.tick},
               Code::invalid_source, RigidBodyError::tick_overflow);
  bad = source;
  ++bad.craft.version;
  expect_error(fixture.context, bad, request, Code::invalid_source,
               RigidBodyError::invalid_craft_frame);
  expect_error({fixture.system, nullptr}, source, request, Code::invalid_source,
               RigidBodyError::unknown_station);
  auto system_source = source;
  system_source.frame = fixture.system_frame();
  expect_error({fixture.system, nullptr}, system_source,
               {fixture.station_frame(), source.tick},
               Code::invalid_destination, RigidBodyError::unknown_station);
  auto forged_system = fixture.system;
  ++forged_system.seed.value;
  expect_error({forged_system, &fixture.station}, source, request,
               Code::invalid_source, RigidBodyError::invalid_world_context);
  const auto wrong_station = generate_origin_station(Seed{43});
  expect_error({fixture.system, &wrong_station}, source, request,
               Code::invalid_source);

  // Both directions and all components: legal input at the bound, illegal
  // translated result. No overflowing candidate leaks into authoritative state.
  const auto ephemeris = required(resolve_origin_station_ephemeris(
      fixture.system, fixture.station, {source.tick, 0}));
  const std::array op{ephemeris.position.x, ephemeris.position.y,
                      ephemeris.position.z};
  const std::array ov{ephemeris.velocity.x, ephemeris.velocity.y,
                      ephemeris.velocity.z};
  for (const bool to_system : {false, true}) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      for (const bool velocity : {false, true}) {
        auto edge = source;
        edge.frame =
            to_system ? fixture.station_frame() : fixture.system_frame();
        auto& vector = velocity ? edge.linear_velocity_metres_per_second
                                : edge.position_metres;
        const std::array fields{&vector.x, &vector.y, &vector.z};
        const double offset = velocity ? ov[axis] : op[axis];
        check(offset != 0, "overflow fixture has nonzero authority offset");
        const double bound = velocity ? kRigidBodyMaximumVelocityMetresPerSecond
                                      : kRigidBodyMaximumPositionMetres;
        *fields[axis] = std::copysign(bound, to_system ? offset : -offset);
        expect_error(
            fixture.context, edge,
            {to_system ? fixture.system_frame() : fixture.station_frame(),
             edge.tick},
            Code::invalid_result, RigidBodyError::excessive_magnitude);
      }
    }
  }
}

using Matrix = std::array<std::array<double, 3>, 3>;
auto matrix(RigidOrientation q) -> Matrix {
  const double n = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  const double s = 2 / n;
  return {{{1 - s * (q.y * q.y + q.z * q.z), s * (q.x * q.y - q.w * q.z),
            s * (q.x * q.z + q.w * q.y)},
           {s * (q.x * q.y + q.w * q.z), 1 - s * (q.x * q.x + q.z * q.z),
            s * (q.y * q.z - q.w * q.x)},
           {s * (q.x * q.z - q.w * q.y), s * (q.y * q.z + q.w * q.x),
            1 - s * (q.x * q.x + q.y * q.y)}}};
}
auto transpose(Matrix m) -> Matrix {
  return {{{m[0][0], m[1][0], m[2][0]},
           {m[0][1], m[1][1], m[2][1]},
           {m[0][2], m[1][2], m[2][2]}}};
}
auto apply(Matrix m, RigidVector3 v) -> RigidVector3 {
  return {(m[0][0] * v.x + m[0][1] * v.y) + m[0][2] * v.z,
          (m[1][0] * v.x + m[1][1] * v.y) + m[1][2] * v.z,
          (m[2][0] * v.x + m[2][1] * v.y) + m[2][2] * v.z};
}
auto product(Matrix a, Matrix b) -> Matrix {
  Matrix result{};
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j)
      result[i][j] =
          (a[i][0] * b[0][j] + a[i][1] * b[1][j]) + a[i][2] * b[2][j];
  return result;
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
auto scale(RigidVector3 v, double s) -> RigidVector3 {
  return {v.x * s, v.y * s, v.z * s};
}
auto max_component(RigidVector3 v) -> double {
  return std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)});
}
auto close(RigidVector3 a, RigidVector3 b, double tolerance) -> bool {
  return max_component(minus(a, b)) <= tolerance;
}
auto close(Matrix a, Matrix b, double tolerance) -> bool {
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j)
      if (std::abs(a[i][j] - b[i][j]) > tolerance) return false;
  return true;
}

auto rotating_contract(const Fixture& fixture) -> void {
  std::uint64_t corpus{14695981039346656037ULL};
  for (const auto& body : fixture.system.planets) {
    const auto recipe = required(
        generate_planet_rotation_recipe(fixture.system, body.descriptor.id));
    const auto wrap = recipe.period_ticks - recipe.epoch_phase_tick;
    const std::array ticks{
        SimulationTick{0}, SimulationTick{1234567},
        wrap - 1,          wrap,
        wrap + 1,          std::numeric_limits<SimulationTick>::max() - 1};
    for (const auto tick : ticks) {
      auto source = fixture.state();
      source.frame = {
          RigidFrameKind::planet_fixed, fixture.system.id, recipe.planet, {}};
      source.tick = tick;
      const double radius =
          static_cast<double>(body.descriptor.radius.value) * 1000;
      source.position_metres = {radius * .8, -radius * .3, radius * .7};
      source.linear_velocity_metres_per_second = {.123, -1234.567, 876.543};
      const auto before = source;
      const auto g =
          required(resolve_planet_rotation(fixture.system, recipe, tick));
      const Matrix r = matrix(g.fixed_to_system);
      const RigidVector3 origin{g.planet_position.x, g.planet_position.y,
                                g.planet_position.z};
      const RigidVector3 velocity{g.planet_velocity.x, g.planet_velocity.y,
                                  g.planet_velocity.z};
      const RigidVector3 omega{g.angular_velocity_radians_per_second.x,
                               g.angular_velocity_radians_per_second.y,
                               g.angular_velocity_radians_per_second.z};
      const auto destination = required(reframe_rigid_body(
          fixture.context, source, {fixture.system_frame(), tick}, recipe));
      const auto r_system = apply(r, source.position_metres);
      const auto q_system = product(r, matrix(source.orientation));
      constexpr double eps = std::numeric_limits<double>::epsilon();
      const double p_bound =
          128 * eps *
          std::max({1.0, max_component(source.position_metres),
                    max_component(origin)});
      const double v_bound =
          256 * eps *
          std::max({1.0,
                    max_component(source.linear_velocity_metres_per_second),
                    max_component(velocity),
                    max_component(omega) *
                        std::max(max_component(source.position_metres),
                                 max_component(origin))});
      check(close(destination.position_metres, plus(origin, r_system), p_bound),
            "independent rotating position oracle");
      check(
          close(destination.linear_velocity_metres_per_second,
                plus(plus(velocity,
                          apply(r, source.linear_velocity_metres_per_second)),
                     cross(omega, r_system)),
                v_bound),
          "translation plus local motion plus Omega cross radius exactly once");
      check(close(matrix(destination.orientation), q_system, 2e-14),
            "full attitude composition independent matrix oracle");
      check(close(destination.angular_velocity_radians_per_second,
                  plus(source.angular_velocity_radians_per_second,
                       apply(transpose(q_system), omega)),
                  2e-14),
            "frame spin expressed in BODY axes, not fixed or system axes");
      check(same_bits(source, before),
            "rotating provider never normalizes/mutates source");
      check(source.craft == destination.craft &&
                source.tick == destination.tick,
            "rotating handoff retains recipe and tick");
      const auto back = required(reframe_rigid_body(
          fixture.context, destination, {source.frame, tick}, recipe));
      check(close(back.position_metres, source.position_metres, p_bound),
            "rotating position roundtrip scale-qualified bound");
      check(close(back.linear_velocity_metres_per_second,
                  source.linear_velocity_metres_per_second, v_bound),
            "rotating velocity roundtrip includes "
            "translation/cancellation/spin error budget");
      check(close(matrix(back.orientation), matrix(source.orientation), 2e-14),
            "orientation roundtrip is geometric, not quaternion bit identity");
      check(close(back.angular_velocity_radians_per_second,
                  source.angular_velocity_radians_per_second, 2e-14),
            "body angular-rate roundtrip");
      for (const auto& saved : {source, destination}) {
        const auto json =
            required(encode_rigid_body_state_json(fixture.context, saved));
        const auto loaded =
            required(decode_rigid_body_state_json(fixture.context, json));
        check(same_bits(saved, loaded),
              "JSON hydration retains accepted rotating state bits");
        const auto target =
            saved.frame == source.frame ? fixture.system_frame() : source.frame;
        check(same_bits(required(reframe_rigid_body(fixture.context, saved,
                                                    {target, tick}, recipe)),
                        required(reframe_rigid_body(fixture.context, loaded,
                                                    {target, tick}, recipe))),
              "same explicitly retained recipe gives exact JSON continuation");
      }
      const auto checksum =
          required(rigid_body_state_checksum(fixture.context, destination));
      for (unsigned shift = 0; shift < 64; shift += 8) {
        corpus ^= (checksum >> shift) & 255U;
        corpus *= 1099511628211ULL;
      }
    }
  }
  std::cout << "rotating handoff corpus " << corpus << '\n';
  // New explicit-recipe fixture observed equal under GCC/Clang. The original
  // three station checksums above are neither updated nor replaced.
  check(corpus == 14154589409055641355ULL,
        "explicit rotating handoff cross-compiler corpus golden");

  auto fixed = fixture.state();
  fixed.frame = fixture.planet_frame();
  const auto recipe = required(
      generate_planet_rotation_recipe(fixture.system, *fixed.frame.planet));
  const auto g =
      required(resolve_planet_rotation(fixture.system, recipe, fixed.tick));
  const Matrix r = matrix(g.fixed_to_system);
  const RigidVector3 origin{g.planet_position.x, g.planet_position.y,
                            g.planet_position.z};
  const RigidVector3 velocity{g.planet_velocity.x, g.planet_velocity.y,
                              g.planet_velocity.z};
  const RigidVector3 omega{g.angular_velocity_radians_per_second.x,
                           g.angular_velocity_radians_per_second.y,
                           g.angular_velocity_radians_per_second.z};
  fixed.position_metres = {4e6, -3e6, 2e6};
  fixed.linear_velocity_metres_per_second = {};
  fixed.angular_velocity_radians_per_second = {};
  const auto corotating = required(reframe_rigid_body(
      fixture.context, fixed, {fixture.system_frame(), fixed.tick}, recipe));
  check(close(corotating.linear_velocity_metres_per_second,
              plus(velocity, cross(omega, apply(r, fixed.position_metres))),
              1e-8),
        "surface-fixed craft carries translational and rotational velocity");
  check(max_component(corotating.angular_velocity_radians_per_second) > 1e-6,
        "surface-fixed craft spins in inertial space");

  auto inertial = corotating;
  inertial.orientation = {};
  inertial.linear_velocity_metres_per_second = velocity;
  inertial.angular_velocity_radians_per_second = {};
  const auto nonspinning = required(reframe_rigid_body(
      fixture.context, inertial, {fixed.frame, fixed.tick}, recipe));
  check(
      close(nonspinning.angular_velocity_radians_per_second, scale(omega, -1),
            1e-18),
      "inertially nonspinning craft has negative frame spin in its body axes");
  check(close(nonspinning.linear_velocity_metres_per_second,
              apply(transpose(r),
                    scale(cross(omega, minus(inertial.position_metres, origin)),
                          -1)),
              1e-8),
        "inertial motion subtracts rotating surface speed once");

  // Local finite-difference world displacement independently checks velocity
  // sign. Planet-centre motion is excluded here because its existing
  // whole-metre ephemeris quantization is not an exact differentiable position
  // function.
  fixed.linear_velocity_metres_per_second = {12.5, -23.75, 34.125};
  const double dt = 1.0 / static_cast<double>(kSimulationHz);
  const auto prior =
      required(resolve_planet_rotation(fixture.system, recipe, fixed.tick - 1));
  const auto next =
      required(resolve_planet_rotation(fixture.system, recipe, fixed.tick + 1));
  const auto a =
      apply(matrix(prior.fixed_to_system),
            minus(fixed.position_metres,
                  scale(fixed.linear_velocity_metres_per_second, dt)));
  const auto b =
      apply(matrix(next.fixed_to_system),
            plus(fixed.position_metres,
                 scale(fixed.linear_velocity_metres_per_second, dt)));
  const auto moved = required(reframe_rigid_body(
      fixture.context, fixed, {fixture.system_frame(), fixed.tick}, recipe));
  check(close(scale(minus(b, a), .5 / dt),
              minus(moved.linear_velocity_metres_per_second, velocity), 2e-5),
        "independent local finite-difference displacement agrees with handoff "
        "velocity");

  for (const auto orientation : {RigidOrientation{1 + 4e-13, 0, 0, 0},
                                 RigidOrientation{0, 0, 1 - 4e-13, 0}}) {
    fixed.orientation = orientation;
    const auto original = fixed;
    check(same_bits(
              required(reframe_rigid_body(fixture.context, fixed,
                                          {fixed.frame, fixed.tick}, recipe)),
              fixed),
          "explicit same-planet identity preserves near-tolerance norm "
          "residual exactly");
    const auto system = required(reframe_rigid_body(
        fixture.context, fixed, {fixture.system_frame(), fixed.tick}, recipe));
    const auto back = required(reframe_rigid_body(
        fixture.context, system, {fixed.frame, fixed.tick}, recipe));
    check(same_bits(fixed, original), "new attitude normalization never "
                                      "modifies accepted input norm residual");
    check(close(matrix(back.orientation), matrix(fixed.orientation), 2e-14),
          "near-tolerance quaternion roundtrip preserves orientation, not norm "
          "residual bits");
  }
}

auto rotating_refusals(const Fixture& fixture) -> void {
  auto source = fixture.state();
  source.frame = fixture.planet_frame();
  const auto recipe = required(
      generate_planet_rotation_recipe(fixture.system, *source.frame.planet));
  const auto reject = [&](const RigidBodyState& state,
                          const RigidFrameHandoffRequest& request,
                          const PlanetRotationRecipe& selection, Code code,
                          std::optional<PlanetRotationError> detail = {}) {
    const auto before = state;
    const auto result =
        reframe_rigid_body(fixture.context, state, request, selection);
    check(!result && result.error().code == code,
          "explicit rotating request fails expected category");
    check(same_bits(before, state),
          "rotating refusal preserves complete source bits");
    if (!result && detail)
      check(result.error().rotation_error == detail,
            "rotation refusal retains detailed provider error");
  };
  reject(source, {fixture.system_frame(), source.tick + 1}, recipe,
         Code::stale_tick);
  auto bad_recipe = recipe;
  ++bad_recipe.version;
  reject(source, {source.frame, source.tick}, bad_recipe,
         Code::invalid_rotation_recipe,
         PlanetRotationError::unsupported_version);
  bad_recipe = recipe;
  ++bad_recipe.epoch_phase_tick;
  reject(source, {fixture.system_frame(), source.tick}, bad_recipe,
         Code::invalid_rotation_recipe);
  bad_recipe = recipe;
  bad_recipe.catalog_kind = LocalSystemKind::procedural;
  reject(source, {source.frame, source.tick}, bad_recipe,
         Code::invalid_rotation_recipe);
  bad_recipe = required(generate_planet_rotation_recipe(
      fixture.system, fixture.system.planets[1].descriptor.id));
  reject(source, {fixture.system_frame(), source.tick}, bad_recipe,
         Code::invalid_rotation_recipe);
  const RigidCoordinateFrame other_planet{
      RigidFrameKind::planet_fixed, fixture.system.id, bad_recipe.planet, {}};
  reject(source, {other_planet, source.tick}, recipe,
         Code::unsupported_frame_pair);
  reject(source, {fixture.station_frame(), source.tick}, recipe,
         Code::unsupported_frame_pair);
  auto station = source;
  station.frame = fixture.station_frame();
  reject(station, {source.frame, source.tick}, recipe,
         Code::unsupported_frame_pair);
  auto inertial = source;
  inertial.frame = fixture.system_frame();
  reject(inertial, {inertial.frame, inertial.tick}, recipe,
         Code::unsupported_frame_pair);
  reject(inertial, {fixture.station_frame(), inertial.tick}, recipe,
         Code::unsupported_frame_pair);
  auto destination = source.frame;
  destination.planet = PlanetId{source.frame.planet->value + 1};
  reject(inertial, {destination, inertial.tick}, recipe,
         Code::invalid_destination);
  destination = source.frame;
  ++destination.system.value;
  reject(inertial, {destination, inertial.tick}, recipe,
         Code::invalid_destination);
  for (const auto q :
       {RigidOrientation{0, 0, 0, 0}, RigidOrientation{-1, 0, 0, 0},
        RigidOrientation{1, -0.0, 0, 0}}) {
    auto invalid = source;
    invalid.orientation = q;
    reject(invalid, {fixture.system_frame(), invalid.tick}, recipe,
           Code::invalid_source);
  }
  auto bad_tick = source;
  bad_tick.tick = std::numeric_limits<SimulationTick>::max();
  reject(bad_tick, {fixture.system_frame(), bad_tick.tick}, recipe,
         Code::invalid_source);
  for (const double scalar : {std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity(), -0.0}) {
    for (const unsigned vector : {0U, 1U, 2U}) {
      auto invalid = source;
      (vector == 0   ? invalid.position_metres.x
       : vector == 1 ? invalid.linear_velocity_metres_per_second.y
                     : invalid.angular_velocity_radians_per_second.z) = scalar;
      reject(invalid, {fixture.system_frame(), invalid.tick}, recipe,
             Code::invalid_source);
    }
  }
  auto excessive = source;
  excessive.position_metres = {
      1e14, 0, 0}; // Legal p; Omega cross r alone exceeds v bound.
  reject(excessive, {fixture.system_frame(), excessive.tick}, recipe,
         Code::invalid_result);
  excessive = source;
  excessive.position_metres = {1e15, 1e15, 1e15};
  reject(excessive, {fixture.system_frame(), excessive.tick}, recipe,
         Code::invalid_result);
  excessive = source;
  excessive.orientation = {};
  excessive.angular_velocity_radians_per_second = {0, 0, 100};
  reject(excessive, {fixture.system_frame(), excessive.tick}, recipe,
         Code::invalid_result);
  excessive = inertial;
  excessive.orientation = {};
  const auto g =
      required(resolve_planet_rotation(fixture.system, recipe, source.tick));
  const auto omega = components({g.angular_velocity_radians_per_second.x,
                                 g.angular_velocity_radians_per_second.y,
                                 g.angular_velocity_radians_per_second.z});
  const std::array rates{&excessive.angular_velocity_radians_per_second.x,
                         &excessive.angular_velocity_radians_per_second.y,
                         &excessive.angular_velocity_radians_per_second.z};
  const auto axis = static_cast<std::size_t>(
      std::max_element(
          omega.begin(), omega.end(),
          [](double a, double b) { return std::abs(a) < std::abs(b); }) -
      omega.begin());
  *rates[axis] = -std::copysign(100.0, omega[axis]);
  reject(excessive, {source.frame, excessive.tick}, recipe,
         Code::invalid_result);
  excessive = inertial;
  excessive.position_metres = {g.planet_position.x, g.planet_position.y,
                               g.planet_position.z};
  excessive.linear_velocity_metres_per_second = {1e9, 1e9, 1e9};
  const auto inverse = transpose(matrix(g.fixed_to_system));
  const RigidVector3 planet_velocity{g.planet_velocity.x, g.planet_velocity.y,
                                     g.planet_velocity.z};
  check(max_component(
            apply(inverse, minus(excessive.linear_velocity_metres_per_second,
                                 planet_velocity))) >
            kRigidBodyMaximumVelocityMetresPerSecond,
        "reverse velocity-bound fixture independently exceeds component bound");
  reject(excessive, {source.frame, excessive.tick}, recipe,
         Code::invalid_result);
  // Original API still refuses reinterpretation, even though a canonical
  // rotation provider is now available in the linked application.
  expect_error(fixture.context, source, {fixture.system_frame(), source.tick},
               Code::unsupported_frame_pair);
}
} // namespace

auto main() -> int {
  const Fixture fixture;
  transforms(fixture);
  validation(fixture);
  rotating_contract(fixture);
  rotating_refusals(fixture);
  std::cout << "rigid frame handoff: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
