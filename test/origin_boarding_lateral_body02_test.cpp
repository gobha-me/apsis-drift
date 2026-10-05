#include "apsis_drift/origin_boarding_body.hpp"
#include "apsis_drift/origin_boarding_self_model03.hpp"
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {
using namespace apsis_drift;
int failures{};
auto check(bool value, std::string_view label) -> void {
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
auto near(double actual, long double expected) -> bool {
  return std::isfinite(actual) && std::abs(actual - expected) < 2e-13L;
}
auto subtract(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto distance(RigidVector3 a, RigidVector3 b) -> double {
  auto d = subtract(a, b);
  return std::hypot(d.x, d.y, d.z);
}
auto evaluated(const BoardingBodyPose& pose) -> BoardingBodyDiagnostic {
  auto result = evaluate_origin_boarding_body02(pose);
  if (!result) throw std::runtime_error("Expected lateral pose refused");
  return *result;
}
auto pose02() -> BoardingBodyPose {
  BoardingBodyPose result;
  result.policy_version = kBoardingBodyLateralPolicyVersion;
  return result;
}
auto refused(const BoardingBodyPose& pose, BoardingBodyError reason,
             std::string_view label) -> void {
  auto result = evaluate_origin_boarding_body02(pose);
  check(!result && result.error() == reason, label);
}
// Enumerate actual output fields, without padding or producer serialization.
auto snapshot(const BoardingBodyDiagnostic& body)
    -> std::vector<std::uint64_t> {
  std::vector<std::uint64_t> result;
  auto number = [&](double x) {
    result.push_back(std::bit_cast<std::uint64_t>(x));
  };
  auto point = [&](RigidVector3 p) {
    number(p.x);
    number(p.y);
    number(p.z);
  };
  for (const auto& part : body.parts) {
    result.push_back(static_cast<std::uint64_t>(part.id));
    result.push_back(part.mass_weight);
    point(part.mass_point_metres);
    std::visit(
        [&](const auto& shape) {
          using Shape = std::decay_t<decltype(shape)>;
          if constexpr (std::is_same_v<Shape, BoardingBodyBox>) {
            result.push_back(0);
            point(shape.center_metres);
            point(shape.half_size_metres);
            for (auto column : shape.frame.columns)
              point(column);
          } else {
            result.push_back(1);
            point(shape.start_metres);
            point(shape.end_metres);
            number(shape.radius_metres);
          }
        },
        part.world_reservation);
  }
  for (const auto& joint : body.joints) {
    for (auto p : {joint.hip_metres, joint.knee_metres, joint.ankle_metres,
                   joint.sole_origin_metres, joint.shoulder_metres,
                   joint.elbow_metres, joint.wrist_metres})
      point(p);
    number(joint.required_ankle_pitch_degrees);
    number(joint.required_ankle_roll_degrees);
  }
  for (const auto& pair : body.pairs) {
    result.push_back(static_cast<std::uint64_t>(pair.first));
    result.push_back(static_cast<std::uint64_t>(pair.second));
    result.push_back(static_cast<std::uint64_t>(pair.kind));
    result.push_back(static_cast<std::uint64_t>(pair.intersection));
    result.push_back(pair.witness.has_value());
    if (pair.witness) {
      point(pair.witness->point_metres);
      number(pair.witness->first_depth_metres);
      number(pair.witness->second_depth_metres);
    }
  }
  point(body.eye_metres);
  point(body.center_of_mass_metres);
  result.push_back(body.self_conflict);
  result.push_back(body.unresolved_intersection);
  return result;
}
auto compatibility() -> void {
  for (double yaw : {-180., -37., 0., 61., 180.}) {
    for (double lean : {-10., 0., 10.}) {
      BoardingBodyPose old;
      old.yaw_degrees = yaw;
      old.hip_metres = {.2, 1.12, -.5};
      old.pelvis_lean_degrees = lean;
      old.torso_relative_lean_degrees = -lean;
      old.sides[0].hip_flex_degrees = 20;
      old.sides[0].knee_flex_degrees = 15;
      old.sides[1].hip_flex_degrees = 35;
      old.sides[1].knee_flex_degrees = 40;
      old.sides[0].shoulder_flex_degrees = 55;
      old.sides[0].elbow_flex_degrees = 30;
      old.sides[1].shoulder_flex_degrees = 20;
      old.sides[1].elbow_flex_degrees = 70;
      auto historical = evaluate_origin_boarding_body(old);
      check(historical.has_value(), "Historical pose remains supported");
      auto lateral = old;
      lateral.policy_version = kBoardingBodyLateralPolicyVersion;
      const auto current = evaluated(lateral);
      check(current.policy_version == 2, "Explicit version2 diagnostic");
      if (historical)
        check(snapshot(*historical) == snapshot(current),
              "All zero-abduction numeric and pair outputs identical");
      check(!evaluate_origin_boarding_body(lateral),
            "Policy1 refuses version2");
      check(!evaluate_origin_boarding_self_model03(lateral),
            "Recipe03 does not silently admit new kinematics");
    }
  }
}
// Wider independent closed-form rotation of a downward fixed-length link.
auto expected_link(double yaw, double roll, double flex, double length)
    -> std::array<long double, 3> {
  constexpr auto radians = std::numbers::pi_v<long double> / 180;
  const auto y = yaw * radians, r = roll * radians, f = flex * radians;
  const auto x = length * std::sin(r) * std::cos(f);
  const auto z = -length * std::sin(f);
  return {std::cos(y) * x + std::sin(y) * z,
          -length * std::cos(r) * std::cos(f),
          -std::sin(y) * x + std::cos(y) * z};
}
auto check_link(RigidVector3 actual, const std::array<long double, 3>& expected)
    -> void {
  check(near(actual.x, expected[0]) && near(actual.y, expected[1]) &&
            near(actual.z, expected[2]),
        "Independent abduction/flex/yaw order");
}
using WideVector = std::array<long double, 3>;
auto rotate_x(WideVector p, long double degrees) -> WideVector {
  const auto angle = degrees * std::numbers::pi_v<long double> / 180;
  const auto c = std::cos(angle), s = std::sin(angle);
  return {p[0], c * p[1] - s * p[2], s * p[1] + c * p[2]};
}
auto rotate_z(WideVector p, long double degrees) -> WideVector {
  const auto angle = degrees * std::numbers::pi_v<long double> / 180;
  const auto c = std::cos(angle), s = std::sin(angle);
  return {c * p[0] - s * p[1], s * p[0] + c * p[1], p[2]};
}
auto rotate_y(WideVector p, long double degrees) -> WideVector {
  const auto angle = degrees * std::numbers::pi_v<long double> / 180;
  const auto c = std::cos(angle), s = std::sin(angle);
  return {c * p[0] + s * p[2], p[1], -s * p[0] + c * p[2]};
}
auto geometry() -> void {
  for (double yaw : {-90., 0., 73.}) {
    auto p = pose02();
    p.yaw_degrees = yaw;
    p.sides[0].hip_abduction_degrees = 12;
    p.sides[0].hip_flex_degrees = 35;
    p.sides[0].knee_flex_degrees = 20;
    p.sides[1].hip_abduction_degrees = -7;
    p.sides[1].hip_flex_degrees = 10;
    p.sides[1].knee_flex_degrees = 30;
    const auto body = evaluated(p);
    for (std::size_t side = 0; side < 2; ++side) {
      const auto sign = side == 0 ? -1. : 1.;
      const auto roll = sign * p.sides[side].hip_abduction_degrees;
      const auto flex = p.sides[side].hip_flex_degrees;
      const auto knee = p.sides[side].knee_flex_degrees;
      const auto& joints = body.joints[side];
      check_link(subtract(joints.knee_metres, joints.hip_metres),
                 expected_link(yaw, roll, flex, .47285));
      check_link(subtract(joints.ankle_metres, joints.knee_metres),
                 expected_link(yaw, roll, flex - knee, .47478));
      check(
          near(distance(joints.hip_metres, joints.knee_metres), .47285L) &&
              near(distance(joints.knee_metres, joints.ankle_metres), .47478L),
          "Both fixed limb lengths preserved");
      check(joints.required_ankle_roll_degrees == -roll &&
                joints.required_ankle_pitch_degrees == -(flex - knee),
            "Declared inverse ankle compensation");
      const auto& boot =
          std::get<BoardingBodyBox>(body.parts[5 + side * 6].world_reservation);
      check(boot.half_size_metres.x == .06 && boot.half_size_metres.y == .05 &&
                boot.half_size_metres.z == .14,
            "Rigid boots retain dimensions");
      check(boot.frame.columns[1].x == 0 && boot.frame.columns[1].y == 1 &&
                boot.frame.columns[1].z == 0 &&
                near(boot.frame.columns[0].x,
                     std::cos(yaw * std::numbers::pi_v<long double> / 180)) &&
                near(boot.frame.columns[0].z,
                     -std::sin(yaw * std::numbers::pi_v<long double> / 180)),
            "Boots remain flat in registered root yaw frame");
      check(near(boot.center_metres.y - .05, joints.sole_origin_metres.y),
            "Boot bottom and sole origin agree");
      // Apply the complete independent ankle-to-world chain to every basis
      // axis, using emitted compensation rather than a forced flat frame.
      for (std::size_t axis = 0; axis < 3; ++axis) {
        WideVector basis{};
        basis[axis] = 1;
        auto compensated = rotate_z(basis, joints.required_ankle_roll_degrees);
        compensated =
            rotate_x(compensated, joints.required_ankle_pitch_degrees);
        compensated = rotate_x(compensated, -knee);
        compensated = rotate_x(compensated, flex);
        compensated = rotate_z(compensated, roll);
        compensated = rotate_y(compensated, yaw);
        check_link(boot.frame.columns[axis], compensated);
      }
    }
  }
}
auto load_shift() -> void {
  for (const double direction : {-1., 1.}) {
    auto p = pose02();
    p.hip_metres.y =
        .1 + (.47285 + .47478) * std::cos(10 * std::numbers::pi / 180);
    p.sides[0].hip_abduction_degrees = -10 * direction;
    p.sides[1].hip_abduction_degrees = 10 * direction;
    const std::size_t loaded = direction == 1 ? 0 : 1;
    const auto lifted = 1 - loaded;
    p.sides[lifted].hip_flex_degrees = 30;
    p.sides[lifted].knee_flex_degrees = 30;
    const auto body = evaluated(p);
    const auto sole = body.joints[loaded].sole_origin_metres;
    const auto offset = subtract(body.center_of_mass_metres, sole);
    check(near(sole.y, 0), "Loaded sole remains at abstract plane");
    check(body.joints[lifted].sole_origin_metres.y > .06,
          "Other sole lifts without changing limb lengths");
    check(.06 - std::abs(offset.x) > .01 && .14 - std::abs(offset.z) > .01,
          "Actual COM lies inside sole rectangle with declared load margin");
    check(.06 - std::abs(offset.x) > .03 && .14 - std::abs(offset.z) > .03,
          "COM-centered 20mm pressure disk retains 10mm margin");
  }
  auto p = pose02();
  const auto body = evaluated(p);
  for (auto joint : body.joints)
    check(std::abs(body.center_of_mass_metres.x - joint.sole_origin_metres.x) >
              .06,
          "Unshifted control lacks single-sole load projection");
}
auto validation() -> void {
  BoardingBodyPose p;
  refused(p, BoardingBodyError::unsupported_policy,
          "Version1 requires old entry point");
  p.policy_version = 3;
  refused(p, BoardingBodyError::unsupported_policy, "Unknown version refuses");
  for (double value : {std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()}) {
    p = pose02();
    p.sides[0].hip_abduction_degrees = value;
    refused(p, BoardingBodyError::non_finite_input,
            "Nonfinite abduction refuses");
    p = pose02();
    p.sides[0].ankle_roll_degrees = value;
    refused(p, BoardingBodyError::non_finite_input,
            "Nonfinite manual roll refuses");
  }
  for (double boundary : {-15., 15.}) {
    p = pose02();
    p.sides[0].hip_abduction_degrees = boundary;
    check(evaluate_origin_boarding_body02(p).has_value(),
          "Exact ankle roll boundary accepted");
    p.sides[0].hip_abduction_degrees = std::nextafter(boundary, boundary * 2);
    refused(p, BoardingBodyError::joint_limit,
            "Representable roll limit violation refuses");
  }
  p = pose02();
  p.sides[1].hip_abduction_degrees = 35;
  refused(p, BoardingBodyError::joint_limit,
          "Hip limit cannot override flat ankle roll");
  p = pose02();
  p.sides[0].hip_abduction_degrees = 10;
  p.pelvis_lean_degrees = std::nextafter(0., 1.);
  refused(p, BoardingBodyError::unsupported_freedom,
          "Any undeclared pelvis coupling refuses");
  p = pose02();
  p.sides[0].hip_flex_degrees = 30;
  check(evaluate_origin_boarding_body02(p).has_value(),
        "Pitch boundary accepted");
  p.sides[0].hip_flex_degrees = std::nextafter(30., 40.);
  refused(p, BoardingBodyError::joint_limit,
          "Pitch boundary violation refuses");
  for (const auto field : {&BoardingBodySidePose::ankle_roll_degrees,
                           &BoardingBodySidePose::hip_axial_degrees,
                           &BoardingBodySidePose::shoulder_abduction_degrees,
                           &BoardingBodySidePose::wrist_roll_degrees}) {
    p = pose02();
    p.sides[0].*field = 1;
    refused(p, BoardingBodyError::unsupported_freedom,
            "Manual/undeclared freedoms refuse");
  }
  p = pose02();
  p.pack_detached = false;
  refused(p, BoardingBodyError::invalid_prerequisite,
          "Pack prerequisite retained");
}
} // namespace
int main() {
  try {
    validation();
    compatibility();
    geometry();
    load_shift();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "Lateral body02: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
