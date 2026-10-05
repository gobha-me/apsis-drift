#include "apsis_drift/origin_boarding_route_body.hpp"
#include "origin_boarding_body_internal.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <type_traits>

namespace apsis_drift {
namespace {
using Vec = RigidVector3;
using Frame = BoardingBodyFrame;
using Error = BoardingBodyError;
constexpr double radians_per_degree{std::numbers::pi / 180};
static_assert(sizeof(BoardingRouteBodyDiagnostic) <= 8192);
static_assert(sizeof(std::expected<BoardingBodyDiagnostic, Error>) <= 16384);
static_assert(sizeof(BoardingBodyPose) <= 512);

auto add(Vec a, Vec b) -> Vec {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(Vec a, Vec b) -> Vec {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(Vec a, double b) -> Vec {
  return {a.x * b, a.y * b, a.z * b};
}
auto rotate(const Frame& frame, Vec p) -> Vec {
  return add(add(scale(frame.columns[0], p.x), scale(frame.columns[1], p.y)),
             scale(frame.columns[2], p.z));
}
auto multiply(const Frame& a, const Frame& b) -> Frame {
  return {{rotate(a, b.columns[0]), rotate(a, b.columns[1]),
           rotate(a, b.columns[2])}};
}
auto roll(double radians) -> Frame {
  const auto c = std::cos(radians), s = std::sin(radians);
  return {{Vec{c, s, 0}, Vec{-s, c, 0}, Vec{0, 0, 1}}};
}
auto finite(Vec p) -> bool {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
auto finite(const Frame& frame) -> bool {
  return std::ranges::all_of(frame.columns, [](Vec p) { return finite(p); });
}
auto within(double value, double low, double high) -> bool {
  return value >= low && value <= high;
}
auto side_angles(const BoardingBodySidePose& side) -> std::array<double, 12> {
  return {side.hip_flex_degrees,       side.knee_flex_degrees,
          side.shoulder_flex_degrees,  side.elbow_flex_degrees,
          side.hip_abduction_degrees,  side.hip_axial_degrees,
          side.ankle_roll_degrees,     side.shoulder_abduction_degrees,
          side.shoulder_axial_degrees, side.wrist_pitch_degrees,
          side.wrist_yaw_degrees,      side.wrist_roll_degrees};
}
auto lateral_yaw(const BoardingBodyPose& pose) -> bool {
  return std::ranges::any_of(pose.sides, [](const auto& side) {
    return side.hip_abduction_degrees != 0 || side.hip_axial_degrees != 0;
  });
}
auto validate(const BoardingBodyPose& pose) -> std::expected<void, Error> {
  if (pose.policy_version != kBoardingBodyRoutePolicyVersion)
    return std::unexpected(Error::unsupported_policy);
  if (!finite(pose.hip_metres)) return std::unexpected(Error::non_finite_input);
  for (const auto angle :
       {pose.yaw_degrees, pose.pelvis_lean_degrees,
        pose.torso_relative_lean_degrees, pose.pelvis_twist_degrees,
        pose.torso_twist_degrees, pose.neck_pitch_degrees,
        pose.neck_yaw_degrees, pose.neck_roll_degrees})
    if (!std::isfinite(angle)) return std::unexpected(Error::non_finite_input);
  for (const auto& side : pose.sides)
    for (const auto angle : side_angles(side))
      if (!std::isfinite(angle))
        return std::unexpected(Error::non_finite_input);
  const auto bound = kBoardingBodyHipWorkspaceMetres;
  if (!within(pose.hip_metres.x, -bound, bound) ||
      !within(pose.hip_metres.y, -bound, bound) ||
      !within(pose.hip_metres.z, -bound, bound))
    return std::unexpected(Error::excessive_workspace);
  if (!pose.suit_on || !pose.pack_detached || !pose.flat_boots)
    return std::unexpected(Error::invalid_prerequisite);
  if (pose.pelvis_twist_degrees != 0 || pose.torso_twist_degrees != 0 ||
      pose.neck_pitch_degrees != 0 || pose.neck_yaw_degrees != 0 ||
      pose.neck_roll_degrees != 0)
    return std::unexpected(Error::unsupported_freedom);
  for (const auto& side : pose.sides)
    if (side.ankle_roll_degrees != 0 || side.shoulder_abduction_degrees != 0 ||
        side.shoulder_axial_degrees != 0 || side.wrist_pitch_degrees != 0 ||
        side.wrist_yaw_degrees != 0 || side.wrist_roll_degrees != 0)
      return std::unexpected(Error::unsupported_freedom);
  if (lateral_yaw(pose) && pose.pelvis_lean_degrees != 0)
    return std::unexpected(Error::unsupported_freedom);
  if (!within(pose.yaw_degrees, -180, 180) ||
      !within(pose.pelvis_lean_degrees, -35, 35) ||
      !within(pose.torso_relative_lean_degrees, -35, 35) ||
      !within(pose.pelvis_lean_degrees + pose.torso_relative_lean_degrees, -35,
              35))
    return std::unexpected(Error::joint_limit);
  for (const auto& side : pose.sides)
    if (!within(side.hip_flex_degrees, -20, 120) ||
        !within(side.hip_axial_degrees, -45, 45) ||
        !within(side.hip_abduction_degrees, -35, 35) ||
        !within(side.hip_abduction_degrees, -15, 15) ||
        !within(side.knee_flex_degrees, 0, 135) ||
        !within(side.shoulder_flex_degrees, -30, 150) ||
        !within(side.elbow_flex_degrees, 0, 145) ||
        !within(-(pose.pelvis_lean_degrees + side.hip_flex_degrees -
                  side.knee_flex_degrees),
                -30, 30))
      return std::unexpected(Error::joint_limit);
  return {};
}
auto geometry_finite(const BoardingRouteBodyDiagnostic& result) -> bool {
  if (!finite(result.eye_metres) || !finite(result.center_of_mass_metres))
    return false;
  for (const auto& part : result.parts) {
    if (!finite(part.mass_point_metres)) return false;
    const auto supported = std::visit(
        [](const auto& shape) {
          using Shape = std::decay_t<decltype(shape)>;
          if constexpr (std::is_same_v<Shape, BoardingBodyBox>)
            return finite(shape.center_metres) &&
                   finite(shape.half_size_metres) && finite(shape.frame);
          else
            return finite(shape.start_metres) && finite(shape.end_metres) &&
                   std::isfinite(shape.radius_metres);
        },
        part.world_reservation);
    if (!supported) return false;
  }
  for (std::size_t side = 0; side < result.joints.size(); ++side) {
    const auto& joints = result.joints[side];
    if (!finite(joints.hip_metres) || !finite(joints.knee_metres) ||
        !finite(joints.ankle_metres) || !finite(joints.sole_origin_metres) ||
        !finite(joints.shoulder_metres) || !finite(joints.elbow_metres) ||
        !finite(joints.wrist_metres) ||
        !std::isfinite(joints.required_ankle_pitch_degrees) ||
        !std::isfinite(joints.required_ankle_roll_degrees) ||
        !std::isfinite(result.sole_yaw_degrees[side]) ||
        !finite(result.flat_boot_frames[side]) ||
        !finite(result.ankle_compensation_frames[side]))
      return false;
  }
  return true;
}
} // namespace

auto evaluate_origin_boarding_route_body(const BoardingBodyPose& pose)
    -> std::expected<BoardingRouteBodyDiagnostic, Error> {
  if (const auto checked = validate(pose); !checked)
    return std::unexpected(checked.error());
  auto base_pose = pose;
  base_pose.policy_version = kBoardingBodyLateralPolicyVersion;
  for (auto& side : base_pose.sides)
    side.hip_axial_degrees = 0;
  const auto base = evaluate_origin_boarding_body02(base_pose);
  if (!base) return std::unexpected(base.error());
  BoardingRouteBodyDiagnostic result;
  result.parts = base->parts;
  result.joints = base->joints;
  result.eye_metres = base->eye_metres;
  result.center_of_mass_metres = base->center_of_mass_metres;
  result.lateral_yaw_branch = lateral_yaw(pose);
  const auto* pelvis =
      std::get_if<BoardingBodyBox>(&base->parts[0].world_reservation);
  if (!pelvis) return std::unexpected(Error::numerical_failure);
  bool changed{};
  for (std::size_t side = 0; side < pose.sides.size(); ++side) {
    const auto& angles = pose.sides[side];
    auto& joints = result.joints[side];
    const auto sign = side == 0 ? -1.0 : 1.0;
    const auto index = 3 + side * 6;
    const auto* boot =
        std::get_if<BoardingBodyBox>(&base->parts[index + 2].world_reservation);
    if (!boot) return std::unexpected(Error::numerical_failure);
    result.flat_boot_frames[side] = boot->frame;
    result.sole_yaw_degrees[side] = pose.yaw_degrees + angles.hip_axial_degrees;
    auto compensation = detail::boarding_body_rotation(
        0, joints.required_ankle_pitch_degrees * radians_per_degree);
    if (angles.hip_abduction_degrees != 0)
      compensation =
          multiply(compensation, roll(joints.required_ankle_roll_degrees *
                                      radians_per_degree));
    result.ankle_compensation_frames[side] = compensation;
    if (angles.hip_axial_degrees == 0) continue;
    changed = true;
    const auto flat = multiply(
        pelvis->frame, detail::boarding_body_rotation(
                           angles.hip_axial_degrees * radians_per_degree, 0));
    auto hip_frame = flat;
    if (angles.hip_abduction_degrees != 0)
      hip_frame = multiply(hip_frame, roll(sign * angles.hip_abduction_degrees *
                                           radians_per_degree));
    const auto thigh = multiply(
        hip_frame, detail::boarding_body_rotation(0, angles.hip_flex_degrees *
                                                         radians_per_degree));
    const auto shin =
        multiply(thigh, detail::boarding_body_rotation(
                            0, -angles.knee_flex_degrees * radians_per_degree));
    joints.knee_metres = add(joints.hip_metres, rotate(thigh, {0, -.47285, 0}));
    joints.ankle_metres =
        add(joints.knee_metres, rotate(shin, {0, -.47478, 0}));
    joints.sole_origin_metres =
        add(joints.ankle_metres, rotate(flat, {0, -.10, 0}));
    auto capsule = [&](std::size_t part, Vec a, Vec b, double radius) {
      result.parts[part].world_reservation = BoardingBodyCapsule{a, b, radius};
      result.parts[part].mass_point_metres = add(a, scale(sub(b, a), .5));
    };
    capsule(index, joints.hip_metres, joints.knee_metres, .105);
    capsule(index + 1, joints.knee_metres, joints.ankle_metres, .075);
    const auto center = add(joints.ankle_metres, rotate(flat, {0, -.05, 0}));
    result.parts[index + 2].world_reservation =
        BoardingBodyBox{center, {.06, .05, .14}, flat};
    result.parts[index + 2].mass_point_metres = center;
    result.flat_boot_frames[side] = flat;
  }
  if (changed) {
    Vec weighted;
    std::uint32_t total{};
    for (const auto& part : result.parts) {
      weighted = add(weighted, scale(part.mass_point_metres,
                                     static_cast<double>(part.mass_weight)));
      total += part.mass_weight;
    }
    if (total != kBoardingBodyMassDenominator)
      return std::unexpected(Error::numerical_failure);
    result.center_of_mass_metres = {weighted.x / kBoardingBodyMassDenominator,
                                    weighted.y / kBoardingBodyMassDenominator,
                                    weighted.z / kBoardingBodyMassDenominator};
  }
  if (!geometry_finite(result))
    return std::unexpected(Error::numerical_failure);
  return result;
}
} // namespace apsis_drift
