#include "apsis_drift/origin_walker.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>

#include <nlohmann/json.hpp>

#include "apsis_drift/origin_walk_geometry.hpp"

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
} // namespace
int main() {
  try {
    invalid();
    route();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return failures == 0 ? 0 : 1;
}
