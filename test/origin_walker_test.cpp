#include "apsis_drift/origin_walker.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "../src/origin_walk_index.hpp"
#include "apsis_drift/origin_walk_geometry.hpp"
#include "apsis_drift/simulation.hpp"

namespace {
using namespace apsis_drift;
int failures{};
auto check(bool value, std::string_view label) -> void {
  if (!value) {
    std::cerr << "FAIL: " << label << '\n';
    ++failures;
  }
}
template <class T> auto require(std::expected<T, std::string> value) -> T {
  if (!value) throw std::runtime_error(value.error());
  return *value;
}
auto hash_state(std::uint64_t& trace, const OriginWalkerState& state) -> void {
  const auto hash = [&](std::uint64_t bits) {
    for (unsigned byte = 0; byte < 8; ++byte) {
      trace ^= (bits >> (byte * 8U)) & 255U;
      trace *= 1099511628211ULL;
    }
  };
  hash(state.geometry_version);
  hash(state.actor_id);
  for (const auto value :
       {state.foot_position_metres.x, state.foot_position_metres.y,
        state.foot_position_metres.z, state.velocity_metres_per_second.x,
        state.velocity_metres_per_second.y, state.velocity_metres_per_second.z,
        state.heading_radians})
    hash(std::bit_cast<std::uint64_t>(value));
}
auto invalid() -> void {
  check(!validate_origin_walk_geometry_json(""), "empty geometry refuses");
  check(!validate_origin_walk_geometry_json("{}"),
        "incomplete geometry refuses");
  check(!validate_origin_walk_geometry_json(std::string(1000, '[') +
                                            std::string(1000, ']')),
        "excessive nested geometry refuses before parser recursion");
  auto json = nlohmann::json::parse(kOriginWalkGeometryJson);
  auto bad = json;
  bad["source_sha256"] = "stale";
  check(!validate_origin_walk_geometry_json(bad.dump()),
        "stale source refuses");
  bad = json;
  bad["triangles"][0][0] = json["vertices_micrometres"].size();
  check(!validate_origin_walk_geometry_json(bad.dump()),
        "one-past-end index refuses");
  bad = json;
  bad["triangles"][0][0] = -1;
  check(!validate_origin_walk_geometry_json(bad.dump()),
        "negative index refuses");
  bad = json;
  bad["vertices_micrometres"][0] = nlohmann::json::array({0, 0});
  check(!validate_origin_walk_geometry_json(bad.dump()),
        "invalid vertex dimensions refuse");
  bad = json;
  bad["vertices_micrometres"][0][0] = 1e30;
  check(!validate_origin_walk_geometry_json(bad.dump()),
        "invalid vertex range/type refuses");
  bad = json;
  bad["triangles"][0][1] = bad["triangles"][0][0];
  check(!validate_origin_walk_geometry_json(bad.dump()),
        "repeated index refuses");
  check(validate_origin_walk_geometry_json(kOriginWalkGeometryJson).has_value(),
        "canonical geometry qualifies");
  const OriginWalkerState origin;
  for (const auto value : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity()}) {
    auto state = origin;
    state.foot_position_metres.x = value;
    check(!validate_origin_walker(state), "nonfinite position refuses");
    state = origin;
    state.velocity_metres_per_second.z = value;
    check(!validate_origin_walker(state), "nonfinite velocity refuses");
    check(!advance_origin_walker(origin, {value, 0, 0}),
          "nonfinite controls refuse");
  }
  auto state = origin;
  state.actor_id = 2;
  check(!validate_origin_walker(state), "unknown actor refuses");
  state = origin;
  state.geometry_version = 2;
  check(!validate_origin_walker(state), "unknown geometry refuses");
  state = origin;
  state.foot_position_metres.x = -100;
  check(!validate_origin_walker(state), "outside crop refuses");
  state = origin;
  state.foot_position_metres.y = 1;
  check(!validate_origin_walker(state), "unsupported floating spawn refuses");
  state = origin;
  state.foot_position_metres = {-23.12, -1.35, 0};
  check(!validate_origin_walker(state),
        "open well lacks supported standing floor");
  check(!advance_origin_walker(origin, {1.01, 0, 0}),
        "excessive controls refuse");
  check(!advance_origin_walker(origin, {0, 0, 4}),
        "noncanonical heading refuses");
  check(!advance_origin_walker(origin, {}, SimulationSeconds{1}),
        "large step cannot tunnel");
  check(!advance_origin_walker(origin, {}, SimulationSeconds{-1}),
        "negative step refuses");
}
auto route() -> void {
  OriginWalkerState state;
  check(validate_origin_walker(state).has_value(),
        "fresh hub spawn physically clear/supported");
  for (int i = 0; i < 960; ++i)
    state = require(advance_origin_walker(state, {0, -1, 0}));
  check(std::abs(state.foot_position_metres.x + 16) < 1e-10,
        "actual 16m route reaches workshop approach");
  check(state.velocity_metres_per_second.x == -2, "requested speed retained");
  const auto midpoint = state;
  for (int i = 0; i < 960; ++i)
    state = require(advance_origin_walker(state, {0, 1, 0}));
  check(std::abs(state.foot_position_metres.x) < 1e-10,
        "walk returns hub without teleport");
  auto replay = OriginWalkerState{};
  for (int i = 0; i < 960; ++i)
    replay = require(advance_origin_walker(replay, {0, -1, 0}));
  check(replay == midpoint, "deterministic fixed-step route");
  for (int i = 0; i < 2000; ++i)
    replay = require(advance_origin_walker(replay, {0, -1, 0}));
  std::cout << "D1 stopped x=" << replay.foot_position_metres.x << '\n';
  check(replay.foot_position_metres.x < -20 &&
            replay.foot_position_metres.x > -23.12,
        "D1 route reaches actual vestibule and stops before open well");
  check(replay.velocity_metres_per_second == RigidVector3{},
        "blocked route stops velocity");
  check(validate_origin_walker(replay).has_value(),
        "blocked pose remains supported");
  const auto stopped = replay;
  for (int i = 0; i < 100; ++i)
    replay = require(advance_origin_walker(replay, {0, -1, 0}));
  check(replay == stopped,
        "cannot tunnel through wall/well by repeated requests");
  auto sideways = OriginWalkerState{};
  for (int i = 0; i < 200; ++i)
    sideways = require(advance_origin_walker(sideways, {1, 0, 0}));
  check(std::abs(sideways.foot_position_metres.z) < 1.5 &&
            validate_origin_walker(sideways).has_value(),
        "source wall or bounded route stops lateral motion safely");
  const auto neutral = require(advance_origin_walker(midpoint, {}));
  check(neutral.foot_position_metres == midpoint.foot_position_metres &&
            neutral.velocity_metres_per_second == RigidVector3{},
        "neutral/focus-loss input stops without inertia");
  const auto diagonal = require(advance_origin_walker({}, {1, 1, 0}));
  check(std::hypot(diagonal.velocity_metres_per_second.x,
                   diagonal.velocity_metres_per_second.z) <= 2 + 1e-12,
        "diagonal intent cannot exceed supported speed");
  const auto facing =
      require(advance_origin_walker({}, {0, 0, std::numbers::pi / 2}));
  check(facing.heading_radians == std::numbers::pi / 2 &&
            facing.foot_position_metres == RigidVector3{},
        "heading changes no position");
}
auto index_boundaries() -> void {
  using origin_walk_detail::Bounds;
  using origin_walk_detail::TriangleIndex;
  const Bounds zero{{0, 0, 0}, {0, 0, 0}};
  check(TriangleIndex{}.query(zero).empty(), "empty index query is bounded");
  const TriangleIndex touching{{zero, zero, {{1, 1, 1}, {2, 2, 2}}}};
  check(touching.query(zero) == std::vector<std::size_t>{0, 1},
        "touching duplicate bounds retain distinct original IDs in order");
  check(touching.query({{2, 2, 2}, {2, 2, 2}}) == std::vector<std::size_t>{2},
        "inclusive upper boundary retains touching triangle");
  check(touching.query({{3, 3, 3}, {4, 4, 4}}).empty(),
        "outside query produces no IDs");

  const auto geometry = nlohmann::json::parse(kOriginWalkGeometryJson);
  std::vector<std::array<double, 3>> vertices;
  for (const auto& point : geometry["vertices_micrometres"])
    vertices.push_back({point[0].get<double>() * .000001,
                        point[1].get<double>() * .000001,
                        point[2].get<double>() * .000001});
  std::vector<Bounds> bounds;
  for (const auto& triangle : geometry["triangles"]) {
    const auto first = vertices[triangle[0].get<std::size_t>()];
    Bounds box{first, first};
    for (const auto& id : triangle)
      for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto value = vertices[id.get<std::size_t>()][axis];
        box.low[axis] = std::min(box.low[axis], value);
        box.high[axis] = std::max(box.high[axis], value);
      }
    bounds.push_back(box);
  }
  const TriangleIndex index{bounds};
  const auto verify = [&](const Bounds& query) {
    std::vector<std::size_t> expected;
    for (std::size_t id = 0; id < bounds.size(); ++id) {
      bool overlap = true;
      for (std::size_t axis = 0; axis < 3; ++axis)
        if (bounds[id].low[axis] > query.high[axis] ||
            bounds[id].high[axis] < query.low[axis])
          overlap = false;
      if (overlap) expected.push_back(id);
    }
    check(index.query(query) == expected,
          "exact index IDs/order match full original-triangle AABB scan");
  };
  verify({{-1000, -1000, -1000}, {1000, 1000, 1000}});
  for (const auto edge : {-24.5, -22.5, -20.5, -18.5, -16.5, -14.5, -12.5,
                          -10.5, -8.5, -6.5, -4.5, -2.5, -.5, .45}) {
    for (const auto x : {std::nextafter(edge, -INFINITY), edge,
                         std::nextafter(edge, INFINITY)}) {
      for (const auto offset : std::array<std::array<double, 2>, 5>{
               {{-.32, -.32}, {-.32, .32}, {.32, -.32}, {.32, .32}, {0, 0}}})
        verify({{x + offset[0], -.03, offset[1]},
                {x + offset[0], .03, offset[1]}});
      verify({{x - .32, .045, -.32}, {x + .32, 1.975, .32}});
      // Complete positive/negative diagonal swept reservations, including
      // both sides of the former coarse-bin boundaries.
      for (const auto direction : {-1.0, 1.0}) {
        const auto step = direction * 2.0 / std::sqrt(2.0) / 120.0;
        verify(
            {{x - .32 + std::min(0.0, step), .045, -.32 + std::min(0.0, step)},
             {x + .32 + std::max(0.0, step), 1.975,
              .32 + std::max(0.0, step)}});
      }
    }
  }
  for (std::size_t id = 0; id < bounds.size(); id += 4096) {
    verify({bounds[id].low, bounds[id].low});
    verify({bounds[id].high, bounds[id].high});
  }
  origin_walk_detail::QueryWork work;
  check(
      index.query({{-12.82, .045, -.32}, {-12.18, 1.975, .32}}, &work).empty(),
      "workbench aisle standing reservation remains clear");
  check(
      work.queries == 1 && work.node_tests < 1024 &&
          work.triangle_tests < 1024 && work.candidate_ids == 0,
      "workbench query bounds work instead of scanning the dense 98k bin pair");
}
auto cadence_trace() -> void {
  std::array<OriginWalkerState, 2> final;
  for (std::size_t cadence = 0; cadence < final.size(); ++cadence) {
    auto& state = final[cadence];
    FixedStepClock clock;
    std::uint64_t trace{14695981039346656037ULL};
    const int steps_per_frame = cadence == 0 ? 1 : 2;
    for (const auto direction : {-1.0, 1.0}) {
      for (int frame = 0; frame < 960 / steps_per_frame; ++frame) {
        const auto scheduled = clock.advance(kSimulationStep * steps_per_frame);
        if (!scheduled) throw std::runtime_error("route clock refused");
        check(scheduled->steps == steps_per_frame,
              "60/120Hz schedules the same fixed walking ticks");
        for (int tick = 0; tick < scheduled->steps; ++tick) {
          state = require(advance_origin_walker(state, {0, direction, 0}));
          hash_state(trace, state);
        }
      }
    }
    // Captured from the unchanged exact-geometry coarse-bin implementation,
    // hashing all actor fields at every tick, including IEEE double bits.
    check(trace == 13269319587162273820ULL,
          "all 1920 actor states remain bitwise equal to the original route");
  }
  check(final[0] == final[1], "60/120Hz final actor state remains identical");
}
auto measure_route() -> void {
  using Clock = std::chrono::steady_clock;
  struct Bucket {
    std::vector<std::int64_t> nanoseconds;
    origin_walk_detail::QueryWork work;
  };
  std::array<Bucket, 8> buckets;
  OriginWalkerState state;
  if (const auto valid = validate_origin_walker(state); !valid)
    throw std::runtime_error(valid.error());
  std::uint64_t trace{14695981039346656037ULL};
  for (const auto direction : {-1.0, 1.0}) {
    for (int i = 0; i < 960; ++i) {
      const auto bucket = static_cast<std::size_t>(
          std::clamp(std::floor(-state.foot_position_metres.x / 2), 0.0, 7.0));
      auto& sample = buckets[bucket];
      auto* previous = origin_walk_detail::exchange_query_work(&sample.work);
      const auto start = Clock::now();
      const auto advanced = advance_origin_walker(state, {0, direction, 0});
      const auto stop = Clock::now();
      origin_walk_detail::exchange_query_work(previous);
      state = require(advanced);
      sample.nanoseconds.push_back(
          std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start)
              .count());
      hash_state(trace, state);
    }
  }
  nlohmann::ordered_json report{
      {"scope", "Origin route CPU only; no GPU measurement"},
      {"steps", 1920},
      {"trace_hash", trace},
      {"final_x", state.foot_position_metres.x},
      {"buckets", nlohmann::ordered_json::array()}};
  for (std::size_t i = 0; i < buckets.size(); ++i) {
    auto& sample = buckets[i];
    std::ranges::sort(sample.nanoseconds);
    const auto& times = sample.nanoseconds;
    const auto& work = sample.work;
    report["buckets"].push_back({{"x_min", -2 * static_cast<int>(i + 1)},
                                 {"x_max", -2 * static_cast<int>(i)},
                                 {"samples", times.size()},
                                 {"p50_ns", times[times.size() / 2]},
                                 {"p95_ns", times[times.size() * 95 / 100]},
                                 {"max_ns", times.back()},
                                 {"queries", work.queries},
                                 {"broad_entries", work.broad_entries},
                                 {"node_tests", work.node_tests},
                                 {"triangle_tests", work.triangle_tests},
                                 {"candidate_ids", work.candidate_ids}});
  }
  std::cout << report.dump(2) << '\n';
}
} // namespace
int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view{argv[1]} == "--measure-route") {
      measure_route();
      return 0;
    }
    invalid();
    index_boundaries();
    route();
    cadence_trace();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return failures == 0 ? 0 : 1;
}
