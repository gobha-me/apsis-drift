#include "apsis_drift/origin_boarding_route_body.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace {
using namespace apsis_drift;
using Vec = RigidVector3;
using Pose = BoardingBodyPose;
using Side = BoardingBodySidePose;
using Error = BoardingBodyError;
using Diagnostic = BoardingRouteBodyDiagnostic;
std::size_t checks{};
int failures{};
void check(bool okay, std::string_view label) {
  ++checks;
  if (!okay) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
auto pose() -> Pose {
  Pose p;
  p.policy_version = kBoardingBodyRoutePolicyVersion;
  return p;
}
auto evaluated(const Pose& p) -> Diagnostic {
  auto result = evaluate_origin_boarding_route_body(p);
  if (!result) throw std::runtime_error("Registered static pose refused");
  return *result;
}
void refused(const Pose& p, Error error, std::string_view label) {
  const auto result = evaluate_origin_boarding_route_body(p);
  check(!result && result.error() == error, label);
}
auto bits(double a, double b) -> bool {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
auto bits(Vec a, Vec b) -> bool {
  return bits(a.x, b.x) && bits(a.y, b.y) && bits(a.z, b.z);
}
auto bits(const BoardingBodyFrame& a, const BoardingBodyFrame& b) -> bool {
  for (std::size_t i = 0; i < 3; ++i)
    if (!bits(a.columns[i], b.columns[i])) return false;
  return true;
}
auto bits(const BoardingBodyBox& a, const BoardingBodyBox& b) -> bool {
  return bits(a.center_metres, b.center_metres) &&
         bits(a.half_size_metres, b.half_size_metres) && bits(a.frame, b.frame);
}
auto bits(const BoardingBodyCapsule& a, const BoardingBodyCapsule& b) -> bool {
  return bits(a.start_metres, b.start_metres) &&
         bits(a.end_metres, b.end_metres) &&
         bits(a.radius_metres, b.radius_metres);
}
void part_identity(const BoardingBodyPart& a, const BoardingBodyPart& b) {
  check(a.id == b.id && a.mass_weight == b.mass_weight &&
            bits(a.mass_point_metres, b.mass_point_metres),
        "Legacy named mass bit identity");
  std::visit(
      [&](const auto& shape) {
        const auto* other =
            std::get_if<std::decay_t<decltype(shape)>>(&b.world_reservation);
        check(other && bits(shape, *other),
              "Legacy complete world shape bit identity");
      },
      a.world_reservation);
}

// Independent long-double Rodrigues matrices and inverse compositions, rather
// than the producer's private rotation, forward or compensation helpers.
using Wide = std::array<long double, 3>;
using Matrix = std::array<Wide, 3>;
auto wide(Vec p) -> Wide {
  return {p.x, p.y, p.z};
}
auto add(Wide a, Wide b) -> Wide {
  return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}
auto sub(Wide a, Wide b) -> Wide {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
auto scale(Wide a, long double s) -> Wide {
  return {a[0] * s, a[1] * s, a[2] * s};
}
auto length(Wide a) -> long double {
  return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}
auto rotate(const Matrix& m, Wide p) -> Wide {
  Wide out{};
  for (std::size_t row = 0; row < 3; ++row)
    for (std::size_t column = 0; column < 3; ++column)
      out[row] += m[row][column] * p[column];
  return out;
}
auto multiply(const Matrix& a, const Matrix& b) -> Matrix {
  Matrix out{};
  for (std::size_t row = 0; row < 3; ++row)
    for (std::size_t col = 0; col < 3; ++col)
      for (std::size_t k = 0; k < 3; ++k)
        out[row][col] += a[row][k] * b[k][col];
  return out;
}
auto transpose(const Matrix& m) -> Matrix {
  Matrix out{};
  for (std::size_t row = 0; row < 3; ++row)
    for (std::size_t col = 0; col < 3; ++col)
      out[row][col] = m[col][row];
  return out;
}
auto rotation(std::size_t axis, double degrees) -> Matrix {
  Wide u{};
  u[axis] = 1;
  const auto angle =
      static_cast<long double>(degrees) * std::numbers::pi_v<long double> / 180;
  const auto c = std::cos(angle), s = std::sin(angle);
  const Matrix cross{{{0, -u[2], u[1]}, {u[2], 0, -u[0]}, {-u[1], u[0], 0}}};
  Matrix out{};
  for (std::size_t row = 0; row < 3; ++row)
    for (std::size_t col = 0; col < 3; ++col)
      out[row][col] = (row == col ? c : 0) + (1 - c) * u[row] * u[col] +
                      s * cross[row][col];
  return out;
}
auto near(long double actual, long double expected) -> bool {
  return std::isfinite(actual) && std::abs(actual - expected) < 4e-13L;
}
auto near(Vec actual, Wide expected) -> bool {
  return near(actual.x, expected[0]) && near(actual.y, expected[1]) &&
         near(actual.z, expected[2]);
}
void frame_near(const BoardingBodyFrame& actual, const Matrix& expected,
                std::string_view label) {
  for (std::size_t col = 0; col < 3; ++col)
    check(near(actual.columns[col],
               {expected[0][col], expected[1][col], expected[2][col]}),
          label);
}
void dimensions_and_masses(const Diagnostic& body) {
  constexpr std::array<std::uint32_t, 15> weights{
      144, 540, 96, 120, 48, 12, 15, 10, 5, 120, 48, 12, 15, 10, 5};
  std::uint32_t total{};
  Wide weighted{};
  for (std::size_t i = 0; i < body.parts.size(); ++i) {
    const auto& part = body.parts[i];
    check(static_cast<std::size_t>(part.id) == i &&
              part.mass_weight == weights[i],
          "Original fifteen-part identity and individual mass");
    total += part.mass_weight;
    weighted =
        add(weighted, scale(wide(part.mass_point_metres), part.mass_weight));
    const bool is_box = i < 3 || i == 5 || i == 8 || i == 11 || i == 14;
    check(std::holds_alternative<BoardingBodyBox>(part.world_reservation) ==
              is_box,
          "Full WORLD box/capsule categories retained");
    if (is_box) {
      const auto& b = std::get<BoardingBodyBox>(part.world_reservation);
      const Vec half = i == 0                ? Vec{.24, .12, .18}
                       : i == 1              ? Vec{.26, .2695, .18}
                       : i == 2              ? Vec{.16, .18, .18}
                       : (i == 5 || i == 11) ? Vec{.06, .05, .14}
                                             : Vec{.04, .05, .02};
      check(bits(b.half_size_metres, half) &&
                bits(b.center_metres, part.mass_point_metres),
            "Unshrunk original box halves and center mass");
    } else {
      const auto& c = std::get<BoardingBodyCapsule>(part.world_reservation);
      const auto relative = (i - 3) % 6;
      const double radius = relative == 0   ? .105
                            : relative == 1 ? .075
                            : relative == 3 ? .065
                                            : .055;
      check(bits(c.radius_metres, radius) &&
                near(part.mass_point_metres,
                     scale(add(wide(c.start_metres), wide(c.end_metres)), .5L)),
            "Original capsule radius and segment midpoint mass");
    }
  }
  check(total == 1200 &&
            near(body.center_of_mass_metres, scale(weighted, 1.L / 1200)),
        "Independent full fifteen-mass COM");
  check(!body.self_qualified && !body.source_qualified &&
            !body.material_qualified && !body.world_qualified &&
            !body.sweep_qualified && !body.support_qualified &&
            !body.route_qualified && !body.actor_qualified &&
            !body.seat_qualified && !body.save_qualified,
        "Static geometry grants no gameplay/proof permission");
}
void oracle(const Pose& p, const Diagnostic& body) {
  const auto root = rotation(1, p.yaw_degrees),
             pelvis = multiply(root, rotation(0, p.pelvis_lean_degrees)),
             trunk =
                 multiply(pelvis, rotation(0, p.torso_relative_lean_degrees));
  frame_near(std::get<BoardingBodyBox>(body.parts[0].world_reservation).frame,
             pelvis, "Independent pelvis frame");
  frame_near(std::get<BoardingBodyBox>(body.parts[1].world_reservation).frame,
             trunk, "Independent trunk frame");
  check(near(body.eye_metres,
             add(wide(p.hip_metres), rotate(trunk, {0, .65237L, 0}))),
        "Independent eye point");
  bool lateral = false;
  for (const auto& s : p.sides)
    lateral =
        lateral || s.hip_axial_degrees != 0 || s.hip_abduction_degrees != 0;
  check(body.lateral_yaw_branch == lateral,
        "Declared lateral versus sagittal branch");
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& a = p.sides[side];
    const auto& j = body.joints[side];
    const auto sign = side == 0 ? -1.L : 1.L;
    const auto base = 3 + side * 6;
    const auto flat =
        lateral ? rotation(1, p.yaw_degrees + a.hip_axial_degrees) : root;
    const auto proximal = multiply(
        multiply(pelvis, rotation(1, a.hip_axial_degrees)),
        rotation(2, static_cast<double>(sign) * a.hip_abduction_degrees));
    const auto thigh = multiply(proximal, rotation(0, a.hip_flex_degrees)),
               shin = multiply(thigh, rotation(0, -a.knee_flex_degrees));
    const auto hip =
        add(wide(p.hip_metres), rotate(pelvis, {sign * .14L, 0, 0}));
    const auto knee = add(hip, rotate(thigh, {0, -.47285L, 0}));
    const auto ankle = add(knee, rotate(shin, {0, -.47478L, 0}));
    check(near(j.hip_metres, hip) && near(j.knee_metres, knee) &&
              near(j.ankle_metres, ankle),
          "Independent axial/abduction forward hip-knee-ankle chain");
    check(near(length(sub(wide(j.knee_metres), wide(j.hip_metres))), .47285L) &&
              near(length(sub(wide(j.ankle_metres), wide(j.knee_metres))),
                   .47478L),
          "Both original leg link lengths");
    check(near(j.sole_origin_metres, add(ankle, {0, -.10L, 0})),
          "Flat sole origin below actual ankle");
    frame_near(body.flat_boot_frames[side], flat,
               "Independent per-side flat sole yaw frame");
    frame_near(
        body.ankle_compensation_frames[side], multiply(transpose(shin), flat),
        "Ankle compensation inverse independently closes shin to flat frame");
    check(near(j.required_ankle_pitch_degrees,
               -(p.pelvis_lean_degrees + a.hip_flex_degrees -
                 a.knee_flex_degrees)) &&
              near(j.required_ankle_roll_degrees,
                   -sign * a.hip_abduction_degrees),
          "Derived original ankle pitch and side-aware roll");
    check(
        near(body.sole_yaw_degrees[side], p.yaw_degrees + a.hip_axial_degrees),
        "Unwrapped independent sole yaw");
    const auto& boot =
        std::get<BoardingBodyBox>(body.parts[base + 2].world_reservation);
    frame_near(boot.frame, flat, "WORLD boot consumes declared flat frame");
    check(near(boot.center_metres, add(ankle, {0, -.05L, 0})),
          "Boot center below actual ankle");
    for (std::size_t k = 0; k < 2; ++k) {
      const auto& cap =
          std::get<BoardingBodyCapsule>(body.parts[base + k].world_reservation);
      check(bits(cap.start_metres, k == 0 ? j.hip_metres : j.knee_metres) &&
                bits(cap.end_metres, k == 0 ? j.knee_metres : j.ankle_metres),
            "WORLD capsules follow actual named joint endpoints");
    }
    const auto shoulder =
        add(wide(p.hip_metres), rotate(trunk, {sign * .20265L, .579L, 0}));
    const auto upper = multiply(trunk, rotation(0, a.shoulder_flex_degrees));
    const auto fore = multiply(upper, rotation(0, a.elbow_flex_degrees));
    const auto elbow = add(shoulder, rotate(upper, {0, -.35898L, 0})),
               wrist = add(elbow, rotate(fore, {0, -.386L, 0}));
    check(near(j.shoulder_metres, shoulder) && near(j.elbow_metres, elbow) &&
              near(j.wrist_metres, wrist),
          "Upper-body forward joints independent of hip axial");
    check(near(length(sub(wide(j.elbow_metres), wide(j.shoulder_metres))),
               .35898L) &&
              near(length(sub(wide(j.wrist_metres), wide(j.elbow_metres))),
                   .386L),
          "Both original arm link lengths");
  }
  dimensions_and_masses(body);
}
void invalid_inputs() {
  const auto nan = std::numeric_limits<double>::quiet_NaN(),
             inf = std::numeric_limits<double>::infinity();
  constexpr std::array pose_angles{&Pose::yaw_degrees,
                                   &Pose::pelvis_lean_degrees,
                                   &Pose::torso_relative_lean_degrees,
                                   &Pose::pelvis_twist_degrees,
                                   &Pose::torso_twist_degrees,
                                   &Pose::neck_pitch_degrees,
                                   &Pose::neck_yaw_degrees,
                                   &Pose::neck_roll_degrees};
  constexpr std::array side_angles{
      &Side::hip_flex_degrees,       &Side::knee_flex_degrees,
      &Side::shoulder_flex_degrees,  &Side::elbow_flex_degrees,
      &Side::hip_abduction_degrees,  &Side::hip_axial_degrees,
      &Side::ankle_roll_degrees,     &Side::shoulder_abduction_degrees,
      &Side::shoulder_axial_degrees, &Side::wrist_pitch_degrees,
      &Side::wrist_yaw_degrees,      &Side::wrist_roll_degrees};
  for (double bad : {nan, inf, -inf}) {
    for (auto member : pose_angles) {
      auto p = pose();
      p.*member = bad;
      refused(p, Error::non_finite_input,
              "Every original root angle refuses nonfinite");
    }
    for (std::size_t side = 0; side < 2; ++side)
      for (auto member : side_angles) {
        auto p = pose();
        p.sides[side].*member = bad;
        refused(p, Error::non_finite_input,
                "Every original side angle including uncleared axial refuses "
                "nonfinite");
      }
    for (auto member : {&Vec::x, &Vec::y, &Vec::z}) {
      auto p = pose();
      p.hip_metres.*member = bad;
      refused(p, Error::non_finite_input,
              "All hip coordinates refuse nonfinite");
    }
  }
  for (std::uint32_t version : {0U, 1U, 2U, 4U}) {
    auto p = pose();
    p.policy_version = version;
    p.sides[1].hip_axial_degrees = nan;
    refused(p, Error::unsupported_policy,
            "Explicit policy3 precedes nonfinite admission");
  }
  for (auto member :
       {&Pose::suit_on, &Pose::pack_detached, &Pose::flat_boots}) {
    auto p = pose();
    p.*member = false;
    refused(p, Error::invalid_prerequisite,
            "Each static prerequisite required");
  }
  for (auto member : {&Pose::pelvis_twist_degrees, &Pose::torso_twist_degrees,
                      &Pose::neck_pitch_degrees, &Pose::neck_yaw_degrees,
                      &Pose::neck_roll_degrees}) {
    auto p = pose();
    p.*member = .001;
    refused(p, Error::unsupported_freedom, "Unregistered root freedoms refuse");
  }
  for (std::size_t side = 0; side < 2; ++side)
    for (auto member :
         {&Side::ankle_roll_degrees, &Side::shoulder_abduction_degrees,
          &Side::shoulder_axial_degrees, &Side::wrist_pitch_degrees,
          &Side::wrist_yaw_degrees, &Side::wrist_roll_degrees}) {
      auto p = pose();
      p.sides[side].*member = .001;
      refused(p, Error::unsupported_freedom,
              "Unregistered side freedoms refuse");
    }
  for (std::size_t side = 0; side < 2; ++side)
    for (auto member : {&Side::hip_axial_degrees, &Side::hip_abduction_degrees})
      for (double lean : {-1., 1.}) {
        auto p = pose();
        p.pelvis_lean_degrees = lean;
        p.sides[side].*member = .001;
        refused(
            p, Error::unsupported_freedom,
            "Nonzero pelvis and lateral/axial coupling refuses on either side");
      }
  for (auto member : {&Vec::x, &Vec::y, &Vec::z})
    for (double edge : {-8., 8.}) {
      auto p = pose();
      p.hip_metres.*member = edge;
      check(evaluate_origin_boarding_route_body(p).has_value(),
            "Inclusive original workspace boundary");
      p.hip_metres.*member = std::nextafter(edge, edge < 0 ? -inf : inf);
      refused(p, Error::excessive_workspace,
              "One stored value beyond original workspace refuses");
    }
  auto p = pose();
  p.hip_metres.x = 9;
  p.suit_on = false;
  refused(p, Error::excessive_workspace,
          "Workspace precedence before prerequisite");
  p = pose();
  p.suit_on = false;
  p.neck_roll_degrees = 1;
  refused(p, Error::invalid_prerequisite,
          "Prerequisite precedence before unsupported freedom");
}
void limits() {
  const auto inf = std::numeric_limits<double>::infinity();
  for (double edge : {-180., 180.}) {
    auto p = pose();
    p.yaw_degrees = edge;
    check(evaluate_origin_boarding_route_body(p).has_value(),
          "Yaw exact inclusive limits");
    p.yaw_degrees = std::nextafter(edge, edge < 0 ? -inf : inf);
    refused(p, Error::joint_limit, "Yaw next stored value refuses");
  }
  for (double edge : {-35., 35.})
    for (bool pelvis : {false, true}) {
      auto p = pose();
      if (pelvis) {
        p.pelvis_lean_degrees = edge;
        for (auto& s : p.sides) {
          s.hip_flex_degrees = 45;
          s.knee_flex_degrees = 45 + edge;
        }
      } else
        p.torso_relative_lean_degrees = edge;
      check(evaluate_origin_boarding_route_body(p).has_value(),
            "Pelvis/relative torso exact inclusive limits");
      if (pelvis)
        p.pelvis_lean_degrees = std::nextafter(edge, edge < 0 ? -inf : inf);
      else
        p.torso_relative_lean_degrees =
            std::nextafter(edge, edge < 0 ? -inf : inf);
      refused(p, Error::joint_limit,
              "Pelvis/relative torso next stored value refuses");
    }
  auto total = pose();
  total.pelvis_lean_degrees = 20;
  total.torso_relative_lean_degrees = 15;
  check(evaluate_origin_boarding_route_body(total).has_value(),
        "Combined trunk lean exact35");
  total.torso_relative_lean_degrees = std::nextafter(35., inf) - 20;
  refused(total, Error::joint_limit,
          "Combined trunk lean independently bounded");
  struct Limit {
    double Side::* member;
    double low, high;
  };
  constexpr std::array bounds{Limit{&Side::hip_flex_degrees, -20, 120},
                              Limit{&Side::knee_flex_degrees, 0, 135},
                              Limit{&Side::shoulder_flex_degrees, -30, 150},
                              Limit{&Side::elbow_flex_degrees, 0, 145},
                              Limit{&Side::hip_axial_degrees, -45, 45},
                              Limit{&Side::hip_abduction_degrees, -15, 15}};
  for (std::size_t side = 0; side < 2; ++side)
    for (const auto& limit : bounds)
      for (double edge : {limit.low, limit.high}) {
        auto p = pose();
        auto& s = p.sides[side];
        s.*limit.member = edge;
        if (limit.member == &Side::hip_flex_degrees && edge == 120)
          s.knee_flex_degrees = 120;
        if (limit.member == &Side::knee_flex_degrees && edge == 135)
          s.hip_flex_degrees = 120;
        check(evaluate_origin_boarding_route_body(p).has_value(),
              "Every parent/axial/derived-roll exact boundary admitted");
        s.*limit.member = std::nextafter(edge, edge == limit.low ? -inf : inf);
        refused(p, Error::joint_limit,
                "Every side limit one stored value outside refuses");
      }
  for (std::size_t side = 0; side < 2; ++side)
    for (double edge : {-30., 30.}) {
      auto p = pose();
      if (edge < 0)
        p.sides[side].hip_flex_degrees = 30;
      else
        p.sides[side].knee_flex_degrees = 30;
      check(evaluate_origin_boarding_route_body(p).has_value(),
            "Derived ankle pitch exact30 admitted");
      if (edge < 0)
        p.sides[side].hip_flex_degrees = std::nextafter(30., inf);
      else
        p.sides[side].knee_flex_degrees = std::nextafter(30., inf);
      refused(p, Error::joint_limit,
              "Derived ankle pitch one stored value outside refuses");
    }
}
void legacy_identity() {
  for (int family = 0; family < 4; ++family)
    for (double yaw : {-180., 0., 73., 180.}) {
      auto p = pose();
      p.yaw_degrees = yaw;
      p.hip_metres = {-.17, .91, -1.7};
      for (auto& s : p.sides) {
        s.hip_flex_degrees = 70;
        s.knee_flex_degrees = 90;
        s.shoulder_flex_degrees = 45;
        s.elbow_flex_degrees = 135;
      }
      if (family == 1) {
        p.pelvis_lean_degrees = -10;
        p.torso_relative_lean_degrees = 10;
      }
      if (family == 2) {
        p.sides[0].hip_abduction_degrees = 15;
        p.sides[1].hip_abduction_degrees = -15;
        p.torso_relative_lean_degrees = 35;
      }
      if (family == 3) {
        p.pelvis_lean_degrees = 20;
        p.torso_relative_lean_degrees = -35;
      }
      auto old = p;
      old.policy_version = kBoardingBodyLateralPolicyVersion;
      const auto before = evaluate_origin_boarding_body02(old);
      if (!before) throw std::runtime_error("Original comparison pose refused");
      const auto after = evaluated(p);
      check(
          bits(before->eye_metres, after.eye_metres) &&
              bits(before->center_of_mass_metres, after.center_of_mass_metres),
          "Zero axial old eye and full COM bit identity");
      for (std::size_t i = 0; i < 15; ++i)
        part_identity(before->parts[i], after.parts[i]);
      for (std::size_t side = 0; side < 2; ++side) {
        const auto& a = before->joints[side];
        const auto& b = after.joints[side];
        for (auto member : {&BoardingBodySideJoints::hip_metres,
                            &BoardingBodySideJoints::knee_metres,
                            &BoardingBodySideJoints::ankle_metres,
                            &BoardingBodySideJoints::sole_origin_metres,
                            &BoardingBodySideJoints::shoulder_metres,
                            &BoardingBodySideJoints::elbow_metres,
                            &BoardingBodySideJoints::wrist_metres})
          check(bits(a.*member, b.*member),
                "Zero axial actual joint bit identity");
        check(bits(a.required_ankle_pitch_degrees,
                   b.required_ankle_pitch_degrees) &&
                  bits(a.required_ankle_roll_degrees,
                       b.required_ankle_roll_degrees),
              "Zero axial derived ankle bit identity");
      }
      oracle(p, after);
    }
}
void axial_oracles() {
  for (double yaw : {-180., -73., 0., 90., 180.})
    for (double mirror : {-1., 1.}) {
      auto p = pose();
      p.yaw_degrees = yaw;
      p.hip_metres = {.16, .88, -.55};
      p.torso_relative_lean_degrees = mirror * 35;
      for (std::size_t side = 0; side < 2; ++side) {
        auto& s = p.sides[side];
        const auto sign = side == 0 ? -1. : 1.;
        s.hip_axial_degrees = sign * mirror * 45;
        s.hip_abduction_degrees = sign * mirror * 15;
        s.hip_flex_degrees = 55;
        s.knee_flex_degrees = 75;
        s.shoulder_flex_degrees = 45;
        s.elbow_flex_degrees = 135;
      }
      oracle(p, evaluated(p));
    }
  // Only the changed side's leg/boot and full COM may differ from its own
  // genuine original evaluator control; shoulders/arms/hands stay identical.
  for (std::size_t side = 0; side < 2; ++side) {
    auto p = pose();
    p.yaw_degrees = 90;
    p.sides[side].hip_axial_degrees = 31;
    p.sides[side].hip_flex_degrees = 70;
    p.sides[side].knee_flex_degrees = 90;
    auto old = p;
    old.policy_version = 2;
    old.sides[side].hip_axial_degrees = 0;
    const auto base = evaluate_origin_boarding_body02(old);
    if (!base) throw std::runtime_error("One-side base refused");
    const auto result = evaluated(p);
    for (std::size_t i = 0; i < 15; ++i)
      if (i < 3 + 6 * side || i > 5 + 6 * side)
        part_identity(base->parts[i], result.parts[i]);
    check(!bits(base->center_of_mass_metres, result.center_of_mass_metres),
          "Actual changed leg updates full COM");
    oracle(p, result);
  }
}
} // namespace
int main() {
  try {
    invalid_inputs();
    limits();
    legacy_identity();
    axial_oracles();
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
