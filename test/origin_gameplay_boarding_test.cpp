#include "apsis_drift/origin_gameplay_boarding.hpp"
#include "apsis_drift/origin_walker.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
using namespace apsis_drift;
int failures{};
auto check(bool value, std::string_view why) -> void {
  if (!value) {
    std::cerr << "FAIL: " << why << '\n';
    ++failures;
  }
}
template <class T, class E> auto required(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Gameplay boarding fixture refused");
  return *value;
}
auto distance(RigidVector3 a, RigidVector3 b) -> double {
  return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}
auto run() -> void {
  // Validate malformed states before source preparation or visual checks.
  GameplayBoardingState invalid;
  check(!validate_origin_gameplay_boarding(invalid), "default state refuses");
  invalid.craft_station_position = {-28.32, -4.257, 0};
  invalid.craft_station_orientation = {.70710678118654752440, 0,
                                       .70710678118654752440, 0};
  invalid.station_entry_eye = {-22, 1.70, 0};
  check(validate_origin_gameplay_boarding(invalid).has_value(),
        "finite local fixture");
  for (double value : {std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity()}) {
    auto bad = invalid;
    bad.station_entry_eye.x = value;
    check(!validate_origin_gameplay_boarding(bad), "nonfinite eye refuses");
    bad = invalid;
    bad.station_entry_heading_radians = value;
    check(!validate_origin_gameplay_boarding(bad), "nonfinite heading refuses");
    bad = invalid;
    bad.craft_station_orientation.w = value;
    check(!validate_origin_gameplay_boarding(bad),
          "nonfinite quaternion refuses");
  }
  auto bad = invalid;
  bad.elapsed_ticks = kGameplayBoardingTicks + 1;
  check(!step_origin_gameplay_boarding(bad), "one-past-end state refuses");
  check(!step_origin_gameplay_boarding(
            invalid, std::numeric_limits<std::uint32_t>::max()),
        "unbounded tick batch refuses without overflow");
  check(required(step_origin_gameplay_boarding(invalid, 0)) == invalid,
        "zero ticks unchanged");
  bad = invalid;
  bad.direction = static_cast<GameplayBoardingDirection>(255);
  check(!validate_origin_gameplay_boarding(bad), "unknown direction refuses");
  bad = invalid;
  bad.station_entry_heading_radians = std::numbers::pi + .001;
  check(!validate_origin_gameplay_boarding(bad),
        "heading beyond walker range refuses");
  check(!view_origin_gameplay_boarding({}, invalid),
        "missing native source refuses");

  const auto binding = required(make_native_starting_assembly_binding({}));
  const auto station = generate_origin_station(Seed{42});
  const auto system = required(generate_physical_origin_system(Seed{42}));
  const auto geometry = required(origin_station_geometry(station));
  const auto port = required(resolve_origin_port_pose(system, station, geometry,
                                                      geometry.ports[0].id, 0));
  auto walker = OriginWalkerState{};
  for (int i = 0; i < 2960; ++i)
    walker = required(advance_origin_walker(walker, {0, -1, 0}));
  check(validate_origin_walker(walker).has_value(),
        "real supported D1 walking approach");
  const RigidVector3 entry{walker.foot_position_metres.x,
                           walker.foot_position_metres.y +
                               kOriginWalkerEyeHeightMetres,
                           walker.foot_position_metres.z};
  check(!begin_origin_gameplay_boarding(binding, port, {0, 1.70, 0}, 0,
                                        GameplayBoardingDirection::board),
        "distant hub entry refuses");
  auto wrong = port;
  wrong.port.ordinal = 2;
  check(!begin_origin_gameplay_boarding(binding, wrong, entry, 0,
                                        GameplayBoardingDirection::board),
        "D2 refuses");
  auto state = required(begin_origin_gameplay_boarding(
      binding, port, entry, walker.heading_radians,
      GameplayBoardingDirection::board));
  for (double heading : {-std::numbers::pi, std::numbers::pi}) {
    auto facing = required(begin_origin_gameplay_boarding(
        binding, port, entry, heading, GameplayBoardingDirection::board));
    auto previous = required(view_origin_gameplay_boarding(binding, facing));
    check(previous.heading_radians == heading,
          "exact extreme entry heading retained");
    for (std::uint32_t i = 0; i < 120; ++i) {
      facing = required(step_origin_gameplay_boarding(facing));
      const auto view =
          required(view_origin_gameplay_boarding(binding, facing));
      check(std::isfinite(view.heading_radians) &&
                std::abs(view.heading_radians) <= std::numbers::pi,
            "extreme heading remains in renderer range");
      check(std::hypot(std::sin(view.heading_radians) -
                           std::sin(previous.heading_radians),
                       std::cos(view.heading_radians) -
                           std::cos(previous.heading_radians)) < .04,
            "facing basis stays continuous across angular wrap");
      previous = view;
    }
  }
  std::vector<GameplayBoardingView> forward;
  forward.reserve(kGameplayBoardingTicks + 1);
  for (std::uint32_t i = 0; i <= kGameplayBoardingTicks; ++i) {
    const auto view = required(view_origin_gameplay_boarding(binding, state));
    check(view.hardware.station_closure == 0, "attached D1 closure unchanged");
    check(std::isfinite(view.eye_station.x) &&
              std::isfinite(view.eye_station.y) &&
              std::isfinite(view.eye_station.z),
          "finite continuous eye");
    if (!forward.empty())
      check(distance(view.eye_station, forward.back().eye_station) < .08,
            "no teleport between120Hz route samples");
    check(view.seated == (i == kGameplayBoardingTicks),
          "seated controls only at completed route");
    forward.push_back(view);
    state = required(step_origin_gameplay_boarding(state));
  }
  check(forward.front().eye_station == entry, "exact real entry eye retained");
  check(forward.front().heading_radians == walker.heading_radians,
        "exact entry heading retained");
  check(forward.back().eye_craft == RigidVector3{0, 1.365, -2.49},
        "exact existing flight eye");
  check(forward.back().hardware == OperatingProgress{},
        "all craft hardware closed before flight");
  check(required(step_origin_gameplay_boarding(state)) == state,
        "completion saturates without extra clock");
  auto reverse = required(begin_origin_gameplay_boarding(
      binding, port, entry, walker.heading_radians,
      GameplayBoardingDirection::disembark));
  for (std::uint32_t i = 0; i <= kGameplayBoardingTicks; ++i) {
    const auto view = required(view_origin_gameplay_boarding(binding, reverse));
    const auto& paired = forward[kGameplayBoardingTicks - i];
    check(view.eye_station == paired.eye_station &&
              view.hardware == paired.hardware &&
              view.heading_radians == paired.heading_radians,
          "exact reversible authored path and mechanisms");
    check(!view.seated, "disembarking never enables seated flight controls");
    reverse = required(step_origin_gameplay_boarding(reverse));
  }
  const auto end = required(view_origin_gameplay_boarding(binding, reverse));
  check(end.complete && end.eye_station == entry &&
            end.heading_radians == walker.heading_radians,
        "exact station return");
}
} // namespace
auto main() -> int {
  try {
    run();
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
  return failures == 0 ? 0 : 1;
}
