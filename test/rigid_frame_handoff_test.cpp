#include "apsis_drift/rigid_frame_handoff.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
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
auto required(const std::expected<T, E>& result) -> T {
  if (!result) {
    std::cerr << "FAIL: fixture API rejected valid input\n";
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
} // namespace

auto main() -> int {
  const Fixture fixture;
  transforms(fixture);
  validation(fixture);
  std::cout << "rigid frame handoff: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
