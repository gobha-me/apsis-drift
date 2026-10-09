#include "apsis_drift/origin_gameplay_boarding.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "apsis_drift/operating_motion_recipe.hpp"

namespace apsis_drift {
namespace {
using V = RigidVector3;
using Q = RigidOrientation;
constexpr V flight_eye{0, 1.365, -2.49};
// Existing D1 collar (0,2.907,5.2) plus the walker eye height. The
// ladder/cabin points are authored transit positions inside the existing crop,
// not new floors, collision exemptions or an anatomical clearance proof.
constexpr V hatch_eye{0, 4.607, 5.2}, ladder_eye{0, 1.55, 4.7};
constexpr V d1_position{-28.32, -4.257, 0};
constexpr double yaw_component{0.70710678118654752440};
constexpr Q d1_orientation{yaw_component, 0, yaw_component, 0};
auto finite(V v) -> bool {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(V a, double s) -> V {
  return {a.x * s, a.y * s, a.z * s};
}
auto squared(V a) -> double {
  return a.x * a.x + a.y * a.y + a.z * a.z;
}
auto conjugate(Q q) -> Q {
  return {q.w, -q.x, -q.y, -q.z};
}
auto multiply(Q a, Q b) -> Q {
  return {((a.w * b.w - a.x * b.x) - a.y * b.y) - a.z * b.z,
          ((a.w * b.x + a.x * b.w) + a.y * b.z) - a.z * b.y,
          ((a.w * b.y - a.x * b.z) + a.y * b.w) + a.z * b.x,
          ((a.w * b.z + a.x * b.y) - a.y * b.x) + a.z * b.w};
}
// Same active WXYZ rotation convention as rigid_frame_handoff.cpp.
auto rotate(Q q, V v) -> V {
  const auto p = multiply(multiply(q, {0, v.x, v.y, v.z}), conjugate(q));
  const auto n = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  return {p.x / n, p.y / n, p.z / n};
}
auto transform(const OperatingTransform& t, V v) -> V {
  return add(add(add(scale(t.columns[0], v.x), scale(t.columns[1], v.y)),
                 scale(t.columns[2], v.z)),
             t.columns[3]);
}
auto smooth(double t) -> double {
  t = std::clamp(t, 0.0, 1.0);
  return t * t * t * (10 + t * (-15 + 6 * t));
}
auto mix(V a, V b, double t) -> V {
  if (t <= 0) return a;
  if (t >= 1) return b;
  return add(a, scale(sub(b, a), smooth(t)));
}
auto binding_valid(const NativeCraftBinding& b) -> bool {
  return b.selection() && *b.selection() == NativeStartingAssemblySelection{} &&
         b.contact() && b.pose();
}
auto recipe() -> const std::expected<OperatingMotionRecipe, std::string>& {
  static const auto r =
      decode_operating_motion_recipe(detail::kOperatingMotionRecipeJson);
  return r;
}
auto near(V a, V b) -> bool {
  return squared(sub(a, b)) <= 1e-16;
}
auto near(Q a, Q b) -> bool {
  return std::abs(a.w - b.w) <= 1e-8 && std::abs(a.x - b.x) <= 1e-8 &&
         std::abs(a.y - b.y) <= 1e-8 && std::abs(a.z - b.z) <= 1e-8;
}
auto entry_near(V eye) -> bool {
  // Reachable walking side of the measured D1 collar, with a modest gameplay
  // interaction radius. Ordinary walking still owns its floor/shaft tests.
  return squared(sub(eye, V{-23.12, .35, 0})) <=
         kGameplayBoardingEntryRadiusMetres *
             kGameplayBoardingEntryRadiusMetres;
}
} // namespace

auto validate_origin_gameplay_boarding(const GameplayBoardingState& s)
    -> std::expected<void, std::string> {
  if (s.direction != GameplayBoardingDirection::board &&
      s.direction != GameplayBoardingDirection::disembark)
    return std::unexpected("Unknown boarding direction");
  if (s.elapsed_ticks > kGameplayBoardingTicks)
    return std::unexpected("Boarding progress exceeds route");
  if (!finite(s.station_entry_eye) || !finite(s.craft_station_position) ||
      !std::isfinite(s.station_entry_heading_radians) ||
      std::abs(s.station_entry_heading_radians) > std::numbers::pi ||
      !near(s.craft_station_orientation, d1_orientation))
    return std::unexpected("Boarding requires finite D1 state");
  if (!near(s.craft_station_position, d1_position) ||
      !entry_near(s.station_entry_eye))
    return std::unexpected("Boarding requires the attached D1 approach");
  return {};
}
auto begin_origin_gameplay_boarding(const NativeCraftBinding& binding,
                                    const OriginPortPose& pose, V eye,
                                    double heading,
                                    GameplayBoardingDirection direction)
    -> std::expected<GameplayBoardingState, std::string> {
  if (!binding_valid(binding))
    return std::unexpected("Boarding requires the selected native assembly");
  if (pose.port.ordinal != 1 ||
      pose.station_relative.frame.kind !=
          RigidFrameKind::station_relative_inertial ||
      !pose.station_relative.frame.station ||
      *pose.station_relative.frame.station != pose.port.station ||
      !near(pose.collar_station_metres, V{-23.12, -1.35, 0}))
    return std::unexpected("Boarding requires attached D1");
  GameplayBoardingState s{direction,
                          0,
                          eye,
                          heading,
                          pose.station_relative.position_metres,
                          pose.station_relative.orientation};
  if (auto valid = validate_origin_gameplay_boarding(s); !valid)
    return std::unexpected(valid.error());
  return s;
}
auto step_origin_gameplay_boarding(const GameplayBoardingState& s,
                                   std::uint32_t ticks)
    -> std::expected<GameplayBoardingState, std::string> {
  if (auto valid = validate_origin_gameplay_boarding(s); !valid)
    return std::unexpected(valid.error());
  if (ticks > kGameplayBoardingTicks)
    return std::unexpected("Boarding tick batch exceeds route");
  auto next = s;
  next.elapsed_ticks +=
      std::min(ticks, kGameplayBoardingTicks - s.elapsed_ticks);
  return next;
}
auto view_origin_gameplay_boarding(const NativeCraftBinding& binding,
                                   const GameplayBoardingState& s)
    -> std::expected<GameplayBoardingView, std::string> {
  if (auto valid = validate_origin_gameplay_boarding(s); !valid)
    return std::unexpected(valid.error());
  if (!binding_valid(binding))
    return std::unexpected("Boarding requires the selected native assembly");
  const auto& r = recipe();
  if (!r) return std::unexpected(r.error());
  const auto t = s.direction == GameplayBoardingDirection::board
                     ? s.elapsed_ticks
                     : kGameplayBoardingTicks - s.elapsed_ticks;
  GameplayBoardingView v;
  v.progress = static_cast<double>(s.elapsed_ticks) / kGameplayBoardingTicks;
  v.complete = s.elapsed_ticks == kGameplayBoardingTicks;
  v.seated = s.direction == GameplayBoardingDirection::board && v.complete;
  const double seat = 1 - smooth((static_cast<double>(t) - 840) / 240);
  const double open = 1 - smooth((static_cast<double>(t) - 1080) / 240);
  v.hardware = {open, open, seat, 0};
  auto pose = evaluate_operating_motion(*r, v.hardware);
  if (!pose) return std::unexpected(pose.error());
  v.operating_pose = *pose;
  const auto initial =
      rotate(conjugate(s.craft_station_orientation),
             sub(s.station_entry_eye, s.craft_station_position));
  if (t < 120) {
    v.phase = GameplayBoardingPhase::approach;
    v.eye_craft = mix(initial, hatch_eye, static_cast<double>(t) / 120);
  } else if (t < 480) {
    v.phase = GameplayBoardingPhase::ladder;
    v.eye_craft =
        mix(hatch_eye, ladder_eye, static_cast<double>(t - 120) / 360);
  } else if (t < 840) {
    v.phase = GameplayBoardingPhase::cabin;
    const auto open_eye = transform(pose->craft_world_deltas[10], flight_eye);
    v.eye_craft = mix(ladder_eye, open_eye, static_cast<double>(t - 480) / 360);
  } else {
    v.phase = t < 1080 ? GameplayBoardingPhase::seat
                       : GameplayBoardingPhase::hardware;
    // Occupied eye follows the real ancestor-composed seat_lift delta, rather
    // than a second independently animated seat. At progress0 rest is identity.
    v.eye_craft = t >= 1080
                      ? flight_eye
                      : transform(pose->craft_world_deltas[10], flight_eye);
  }
  const auto yaw = std::numbers::pi / 2;
  const auto turn = std::remainder(yaw - s.station_entry_heading_radians,
                                   2 * std::numbers::pi);
  v.heading_radians =
      std::remainder(s.station_entry_heading_radians +
                         turn * smooth(static_cast<double>(t) / 120),
                     2 * std::numbers::pi);
  v.eye_station = add(s.craft_station_position,
                      rotate(s.craft_station_orientation, v.eye_craft));
  if (t == 0) {
    v.eye_station = s.station_entry_eye;
    v.heading_radians = s.station_entry_heading_radians;
  }
  if (v.complete) v.phase = GameplayBoardingPhase::complete;
  v.body_center_station = sub(v.eye_station, V{0, .65, 0});
  // Compact authored proxy: no limb IK, balance, suit clearance, pressure or
  // continuous whole-mesh collision guarantee. Keep the research APIs intact.
  return v;
}
} // namespace apsis_drift
