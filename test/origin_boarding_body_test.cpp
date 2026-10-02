#include "../src/origin_boarding_body_internal.hpp"
#include "apsis_drift/origin_boarding_body.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace {
using namespace apsis_drift;
std::size_t checks{};
int failures{};
auto check(bool value, std::string_view label) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
auto near(double actual, double expected, double tolerance = 2e-14) -> bool {
  return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
}
auto point_near(RigidVector3 actual, RigidVector3 expected,
                double tolerance = 2e-14) -> bool {
  return near(actual.x, expected.x, tolerance) &&
         near(actual.y, expected.y, tolerance) &&
         near(actual.z, expected.z, tolerance);
}
auto evaluated(const BoardingBodyPose& pose = {}) -> BoardingBodyDiagnostic {
  auto result = evaluate_origin_boarding_body(pose);
  if (!result) throw std::runtime_error("Valid diagnostic input refused");
  return *result;
}
auto refused(const BoardingBodyPose& pose, BoardingBodyError error,
             std::string_view label) -> void {
  const auto result = evaluate_origin_boarding_body(pose);
  check(!result && result.error() == error, label);
}
auto part(const BoardingBodyDiagnostic& body, BoardingBodyPartId id)
    -> const BoardingBodyPart& {
  const auto found = std::ranges::find(body.parts, id, &BoardingBodyPart::id);
  if (found == body.parts.end()) throw std::runtime_error("Missing named part");
  return *found;
}
auto box(const BoardingBodyDiagnostic& body, BoardingBodyPartId id)
    -> const BoardingBodyBox& {
  return std::get<BoardingBodyBox>(part(body, id).world_reservation);
}
auto capsule(const BoardingBodyDiagnostic& body, BoardingBodyPartId id)
    -> const BoardingBodyCapsule& {
  return std::get<BoardingBodyCapsule>(part(body, id).world_reservation);
}
auto pair(const BoardingBodyDiagnostic& body, BoardingBodyPartId a,
          BoardingBodyPartId b) -> const BoardingBodyPairDiagnostic& {
  const auto found = std::ranges::find_if(body.pairs, [a, b](const auto& p) {
    return (p.first == a && p.second == b) || (p.first == b && p.second == a);
  });
  if (found == body.pairs.end()) throw std::runtime_error("Missing named pair");
  return *found;
}
auto interior(BoardingBodyIntersection value) -> bool {
  return value == BoardingBodyIntersection::interior_with_witness ||
         value == BoardingBodyIntersection::interior_unresolved;
}
struct WidePoint {
  long double x, y, z;
};
auto wide(RigidVector3 p) -> WidePoint {
  return {p.x, p.y, p.z};
}
auto subtract(WidePoint a, WidePoint b) -> WidePoint {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto dot(WidePoint a, WidePoint b) -> long double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
// These membership checks use wider arithmetic and the emitted solids only;
// they do not call the producer's intersection or witness routines.
auto strictly_inside(const BoardingBodyBox& shape, RigidVector3 p) -> bool {
  const auto relative = subtract(wide(p), wide(shape.center_metres));
  const std::array<long double, 3> half{shape.half_size_metres.x,
                                        shape.half_size_metres.y,
                                        shape.half_size_metres.z};
  for (std::size_t i = 0; i < half.size(); ++i) {
    if (!(std::abs(dot(relative, wide(shape.frame.columns[i]))) < half[i]))
      return false;
  }
  return true;
}
auto strictly_inside(const BoardingBodyCapsule& shape, RigidVector3 p) -> bool {
  const auto axis = subtract(wide(shape.end_metres), wide(shape.start_metres));
  const auto relative = subtract(wide(p), wide(shape.start_metres));
  const auto length_squared = dot(axis, axis);
  const auto t =
      length_squared == 0
          ? 0.L
          : std::clamp(dot(relative, axis) / length_squared, 0.L, 1.L);
  const WidePoint residual{relative.x - t * axis.x, relative.y - t * axis.y,
                           relative.z - t * axis.z};
  const long double radius = shape.radius_metres;
  return dot(residual, residual) < radius * radius;
}
auto strictly_inside(const BoardingBodyPart& shape, RigidVector3 p) -> bool {
  return std::visit([p](const auto& s) { return strictly_inside(s, p); },
                    shape.world_reservation);
}
auto verify_witnesses(const BoardingBodyDiagnostic& body) -> void {
  std::array<bool, kBoardingBodyPartCount * kBoardingBodyPartCount> seen{};
  for (const auto& p : body.pairs) {
    const auto a = static_cast<std::size_t>(p.first);
    const auto b = static_cast<std::size_t>(p.second);
    check(a < kBoardingBodyPartCount && b < kBoardingBodyPartCount && a != b,
          "Pair names two distinct registered parts");
    if (a >= kBoardingBodyPartCount || b >= kBoardingBodyPartCount) continue;
    const auto index = std::min(a, b) * kBoardingBodyPartCount + std::max(a, b);
    check(!seen[index], "Each unordered self pair occurs exactly once");
    seen[index] = true;
    check(p.intersection != BoardingBodyIntersection::invalid_geometry,
          "Registered solids remain valid finite geometry");
    check(
        p.witness.has_value() ==
            (p.intersection == BoardingBodyIntersection::interior_with_witness),
        "Only verified interior status carries a witness");
    if (p.witness) {
      const auto& w = *p.witness;
      check(std::isfinite(w.first_depth_metres) && w.first_depth_metres > 0 &&
                std::isfinite(w.second_depth_metres) &&
                w.second_depth_metres > 0,
            "Both reported witness depths are finite and strictly positive");
      check(strictly_inside(part(body, p.first), w.point_metres) &&
                strictly_inside(part(body, p.second), w.point_metres),
            "Independent wide membership verifies common interior witness");
    }
  }
  check(body.self_conflict && body.definition_incomplete &&
            !body.model_qualified && !body.route_qualified,
        "Policy01 self conflicts never qualify a model or route");
}
// Independent scalar active rotation: pitch the input vector, then yaw it.
// No producer frame helper or matrix multiplication is used for expectations.
auto transformed(RigidVector3 local, RigidVector3 origin, double yaw_degrees,
                 double pitch_degrees) -> RigidVector3 {
  const auto yaw = yaw_degrees * std::numbers::pi / 180;
  const auto pitch = pitch_degrees * std::numbers::pi / 180;
  const auto y = std::cos(pitch) * local.y - std::sin(pitch) * local.z;
  const auto z = std::sin(pitch) * local.y + std::cos(pitch) * local.z;
  return {origin.x + std::cos(yaw) * local.x + std::sin(yaw) * z, origin.y + y,
          origin.z - std::sin(yaw) * local.x + std::cos(yaw) * z};
}
auto validation_tests() -> void {
  BoardingBodyPose p;
  p.policy_version = 0;
  refused(p, BoardingBodyError::unsupported_policy, "Version zero refuses");
  p.policy_version = 2;
  refused(p, BoardingBodyError::unsupported_policy, "Future policy refuses");
  for (const auto member :
       {&BoardingBodyPose::suit_on, &BoardingBodyPose::pack_detached,
        &BoardingBodyPose::flat_boots}) {
    p = {};
    p.*member = false;
    refused(p, BoardingBodyError::invalid_prerequisite,
            "Each suit/pack/flat-boot prerequisite is explicit");
  }
  constexpr std::array pose_fields{
      &BoardingBodyPose::yaw_degrees,
      &BoardingBodyPose::pelvis_lean_degrees,
      &BoardingBodyPose::torso_relative_lean_degrees,
      &BoardingBodyPose::pelvis_twist_degrees,
      &BoardingBodyPose::torso_twist_degrees,
      &BoardingBodyPose::neck_pitch_degrees,
      &BoardingBodyPose::neck_yaw_degrees,
      &BoardingBodyPose::neck_roll_degrees};
  constexpr std::array side_fields{
      &BoardingBodySidePose::hip_flex_degrees,
      &BoardingBodySidePose::knee_flex_degrees,
      &BoardingBodySidePose::shoulder_flex_degrees,
      &BoardingBodySidePose::elbow_flex_degrees,
      &BoardingBodySidePose::hip_abduction_degrees,
      &BoardingBodySidePose::hip_axial_degrees,
      &BoardingBodySidePose::ankle_roll_degrees,
      &BoardingBodySidePose::shoulder_abduction_degrees,
      &BoardingBodySidePose::shoulder_axial_degrees,
      &BoardingBodySidePose::wrist_pitch_degrees,
      &BoardingBodySidePose::wrist_yaw_degrees,
      &BoardingBodySidePose::wrist_roll_degrees};
  const std::array nonfinite{std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()};
  for (const auto value : nonfinite) {
    for (const auto member : pose_fields) {
      p = {};
      p.*member = value;
      refused(p, BoardingBodyError::non_finite_input,
              "All pose fields reject nonfinite state");
    }
    for (std::size_t side = 0; side < 2; ++side) {
      for (const auto member : side_fields) {
        p = {};
        p.sides[side].*member = value;
        refused(p, BoardingBodyError::non_finite_input,
                "Both sides reject every nonfinite joint");
      }
    }
    for (const auto member :
         {&RigidVector3::x, &RigidVector3::y, &RigidVector3::z}) {
      p = {};
      p.hip_metres.*member = value;
      refused(p, BoardingBodyError::non_finite_input,
              "All hip coordinates reject nonfinite state");
    }
  }
  for (const auto member :
       {&RigidVector3::x, &RigidVector3::y, &RigidVector3::z}) {
    for (const double value : {-8., 8.}) {
      p = {};
      p.hip_metres.*member = value;
      check(evaluate_origin_boarding_body(p).has_value(),
            "Workspace equality remains diagnostic input");
      p.hip_metres.*member = std::nextafter(value, value * 2);
      refused(p, BoardingBodyError::excessive_workspace,
              "One ULP beyond workspace refuses");
    }
  }
  for (const auto member : {&BoardingBodyPose::pelvis_twist_degrees,
                            &BoardingBodyPose::torso_twist_degrees,
                            &BoardingBodyPose::neck_pitch_degrees,
                            &BoardingBodyPose::neck_yaw_degrees,
                            &BoardingBodyPose::neck_roll_degrees}) {
    for (const double value : {-1e-9, 1e-9}) {
      p = {};
      p.*member = value;
      refused(p, BoardingBodyError::unsupported_freedom,
              "Tiny unsupported parent freedom refuses");
    }
  }
  for (std::size_t side = 0; side < 2; ++side) {
    for (const auto member : {&BoardingBodySidePose::hip_abduction_degrees,
                              &BoardingBodySidePose::hip_axial_degrees,
                              &BoardingBodySidePose::ankle_roll_degrees,
                              &BoardingBodySidePose::shoulder_abduction_degrees,
                              &BoardingBodySidePose::shoulder_axial_degrees,
                              &BoardingBodySidePose::wrist_pitch_degrees,
                              &BoardingBodySidePose::wrist_yaw_degrees,
                              &BoardingBodySidePose::wrist_roll_degrees}) {
      p = {};
      p.sides[side].*member = 1e-9;
      refused(p, BoardingBodyError::unsupported_freedom,
              "Unregistered side freedom refuses within historical limit");
      p.sides[side].*member = -0.;
      check(evaluate_origin_boarding_body(p).has_value(),
            "Negative zero requests no unsupported rotation");
    }
  }
  for (const double value : {-180., 180.}) {
    p = {};
    p.yaw_degrees = value;
    check(evaluate_origin_boarding_body(p).has_value(),
          "Root yaw limit equality accepted diagnostically");
    p.yaw_degrees = std::nextafter(value, value * 2);
    refused(p, BoardingBodyError::joint_limit,
            "Yaw outside registered limit refuses");
  }
  for (const auto member : {&BoardingBodyPose::pelvis_lean_degrees,
                            &BoardingBodyPose::torso_relative_lean_degrees}) {
    for (const double value : {-35., 35.}) {
      p = {};
      p.*member = value;
      // Balance hip flex so the independently required flat ankle stays zero.
      if (member == &BoardingBodyPose::pelvis_lean_degrees) {
        for (auto& side : p.sides) {
          side.hip_flex_degrees = 45 - value;
          side.knee_flex_degrees = 45;
        }
      }
      check(evaluate_origin_boarding_body(p).has_value(),
            "Registered lean equality accepted diagnostically");
      p.*member = std::nextafter(value, value * 2);
      refused(p, BoardingBodyError::joint_limit,
              "One ULP outside lean limit refuses");
    }
  }
  p = {};
  p.pelvis_lean_degrees = 20;
  p.torso_relative_lean_degrees = 20;
  refused(p, BoardingBodyError::joint_limit,
          "Relative leans cannot exceed absolute trunk limit");
  for (std::size_t side = 0; side < 2; ++side) {
    struct Limit {
      double BoardingBodySidePose::* member;
      double low, high;
    };
    constexpr std::array limits{
        Limit{&BoardingBodySidePose::hip_flex_degrees, -20, 120},
        Limit{&BoardingBodySidePose::knee_flex_degrees, 0, 135},
        Limit{&BoardingBodySidePose::shoulder_flex_degrees, -30, 150},
        Limit{&BoardingBodySidePose::elbow_flex_degrees, 0, 145}};
    for (const auto limit : limits) {
      for (const double value : {limit.low, limit.high}) {
        p = {};
        p.sides[side].*limit.member = value;
        if (limit.member == &BoardingBodySidePose::hip_flex_degrees)
          p.sides[side].knee_flex_degrees = std::max(0., value);
        if (limit.member == &BoardingBodySidePose::knee_flex_degrees)
          p.sides[side].hip_flex_degrees = std::min(120., value);
        check(evaluate_origin_boarding_body(p).has_value(),
              "Each registered joint endpoint is valid input");
        p.sides[side].*limit.member =
            std::nextafter(value, value == limit.low ? -1000. : 1000.);
        refused(p, BoardingBodyError::joint_limit,
                "Each joint one ULP outside refuses");
      }
    }
    for (const double value : {-30., 30.}) {
      p = {};
      p.sides[side].hip_flex_degrees = value < 0 ? 0 : value;
      p.sides[side].knee_flex_degrees = value < 0 ? -value : 0;
      check(evaluate_origin_boarding_body(p).has_value(),
            "Derived flat ankle exactly at 30 degrees accepted");
      p.sides[side].hip_flex_degrees =
          value < 0 ? 0 : std::nextafter(value, 1000.);
      p.sides[side].knee_flex_degrees =
          value < 0 ? std::nextafter(-value, 1000.) : 0;
      refused(p, BoardingBodyError::joint_limit,
              "Derived ankle outside flat boot limit refuses");
    }
  }
}
auto canonical_tests() -> void {
  const auto body = evaluated();
  check(body.policy_version == 1 && body.parts.size() == 15 &&
            body.pairs.size() == 105,
        "Versioned fixed part and pair inventory");
  constexpr std::array<std::uint32_t, 15> weights{
      144, 540, 96, 120, 48, 12, 15, 10, 5, 120, 48, 12, 15, 10, 5};
  std::uint32_t mass{};
  for (std::size_t i = 0; i < weights.size(); ++i) {
    const auto& p = part(body, static_cast<BoardingBodyPartId>(i));
    check(p.mass_weight == weights[i],
          "Each named part has preregistered integer mass");
    mass += p.mass_weight;
    const auto expected_mass_point = std::visit(
        [](const auto& shape) {
          using Shape = std::decay_t<decltype(shape)>;
          if constexpr (std::is_same_v<Shape, BoardingBodyBox>)
            return shape.center_metres;
          else
            return RigidVector3{(shape.start_metres.x + shape.end_metres.x) / 2,
                                (shape.start_metres.y + shape.end_metres.y) / 2,
                                (shape.start_metres.z + shape.end_metres.z) /
                                    2};
        },
        p.world_reservation);
    check(point_near(p.mass_point_metres, expected_mass_point),
          "Mass point is actual box center or capsule endpoint midpoint");
  }
  check(mass == 1200 && kBoardingBodyMassDenominator == 1200,
        "Fixed weights sum to exact denominator");
  check(point_near(body.center_of_mass_metres, {0, 68765563. / 60000000., 0}),
        "Canonical COM equals independent rational weighted sum");
  check(point_near(body.eye_metres, {0, 1.70, 0}),
        "Canonical eye is 1.70 metres");
  check(near(box(body, BoardingBodyPartId::helmet).center_metres.y + .18, 1.93),
        "Canonical crown is 1.93 metres");
  check(kBoardingBodyStandingWidthMetres == .64 &&
            kBoardingBodyStandingCrownMetres == 1.93,
        "Ordinary standing policy remains unchanged");
  const std::array box_ids{
      BoardingBodyPartId::pelvis,        BoardingBodyPartId::trunk,
      BoardingBodyPartId::helmet,        BoardingBodyPartId::port_boot,
      BoardingBodyPartId::port_hand,     BoardingBodyPartId::starboard_boot,
      BoardingBodyPartId::starboard_hand};
  const std::array<RigidVector3, 7> half_sizes{{{.24, .12, .18},
                                                {.26, .2695, .18},
                                                {.16, .18, .18},
                                                {.06, .05, .14},
                                                {.04, .05, .02},
                                                {.06, .05, .14},
                                                {.04, .05, .02}}};
  for (std::size_t i = 0; i < box_ids.size(); ++i)
    check(box(body, box_ids[i]).half_size_metres == half_sizes[i],
          "Full box reservations retain exact registered dimensions");
  for (std::size_t side = 0; side < 2; ++side) {
    const auto sign = side == 0 ? -1. : 1.;
    const auto& j = body.joints[side];
    check(point_near(j.hip_metres, {sign * .14, 1.04763, 0}) &&
              point_near(j.knee_metres, {sign * .14, .57478, 0}) &&
              point_near(j.ankle_metres, {sign * .14, .1, 0}),
          "Canonical hip/knee/ankle segment lengths");
    check(point_near(j.sole_origin_metres, {sign * .14, 0, 0}) &&
              j.required_ankle_pitch_degrees == 0,
          "Canonical flat sole datum and required ankle pitch");
    check(point_near(j.shoulder_metres, {sign * .20265, 1.62663, 0}) &&
              point_near(j.elbow_metres, {sign * .20265, 1.26765, 0}) &&
              point_near(j.wrist_metres, {sign * .20265, .88165, 0}),
          "Canonical arm segment lengths");
    const auto thigh = side == 0 ? BoardingBodyPartId::port_thigh
                                 : BoardingBodyPartId::starboard_thigh;
    const auto shin = side == 0 ? BoardingBodyPartId::port_shin
                                : BoardingBodyPartId::starboard_shin;
    const auto arm = side == 0 ? BoardingBodyPartId::port_upper_arm
                               : BoardingBodyPartId::starboard_upper_arm;
    const auto forearm = side == 0 ? BoardingBodyPartId::port_forearm
                                   : BoardingBodyPartId::starboard_forearm;
    for (const auto& [id, endpoints] : std::array{
             std::pair{thigh, std::pair{j.hip_metres, j.knee_metres}},
             std::pair{shin, std::pair{j.knee_metres, j.ankle_metres}},
             std::pair{arm, std::pair{j.shoulder_metres, j.elbow_metres}},
             std::pair{forearm, std::pair{j.elbow_metres, j.wrist_metres}}}) {
      check(point_near(capsule(body, id).start_metres, endpoints.first) &&
                point_near(capsule(body, id).end_metres, endpoints.second),
            "Actual capsule reservation retains its named joint endpoints");
    }
    check(capsule(body, thigh).radius_metres == .105 &&
              capsule(body, shin).radius_metres == .075 &&
              capsule(body, arm).radius_metres == .065 &&
              capsule(body, forearm).radius_metres == .055,
          "Capsules retain complete endpoint balls and radii");
    const RigidVector3 shoulder_witness{sign * .148825, 1.04763 + .579, 0};
    const RigidVector3 hip_witness{sign * .14, 1.04763 + .0925, 0};
    check(
        strictly_inside(part(body, arm), shoulder_witness) &&
            strictly_inside(part(body, BoardingBodyPartId::helmet),
                            shoulder_witness),
        "Known 22.35 mm intrinsic shoulder/helmet overlap has strict witness");
    check(
        strictly_inside(part(body, thigh), hip_witness) &&
            strictly_inside(part(body, BoardingBodyPartId::trunk), hip_witness),
        "Known 25 mm intrinsic hip/trunk overlap has strict witness");
    const auto& helmet_shape = box(body, BoardingBodyPartId::helmet);
    const auto& trunk_shape = box(body, BoardingBodyPartId::trunk);
    const auto shoulder_gap = std::abs(capsule(body, arm).start_metres.x -
                                       helmet_shape.center_metres.x) -
                              helmet_shape.half_size_metres.x;
    const auto hip_gap = trunk_shape.center_metres.y -
                         trunk_shape.half_size_metres.y -
                         capsule(body, thigh).start_metres.y;
    check(
        near(capsule(body, arm).radius_metres - shoulder_gap, .02235) &&
            near(capsule(body, thigh).radius_metres - hip_gap, .025),
        "Actual geometry reproduces intrinsic 22.35 and 25 mm radial overlaps");
    for (const auto& [a, b] : {std::pair{arm, BoardingBodyPartId::helmet},
                               std::pair{thigh, BoardingBodyPartId::trunk}}) {
      const auto& p = pair(body, a, b);
      check(p.kind == BoardingBodyPairKind::nonadjacent &&
                p.intersection ==
                    BoardingBodyIntersection::interior_with_witness,
            "Intrinsic nonadjacent conflict is not a connected waiver");
    }
  }
  const auto& connected =
      pair(body, BoardingBodyPartId::pelvis, BoardingBodyPartId::trunk);
  check(connected.kind == BoardingBodyPairKind::unregistered_connected &&
            interior(connected.intersection),
        "Pelvis/trunk overlap remains unregistered connected conflict");
  verify_witnesses(body);
}
auto frame_tests() -> void {
  BoardingBodyPose p;
  p.hip_metres = {.3, 1.2, -.4};
  p.yaw_degrees = 37;
  p.pelvis_lean_degrees = -10;
  p.torso_relative_lean_degrees = 20;
  for (auto& side : p.sides) {
    side.hip_flex_degrees = 60;
    side.knee_flex_degrees = 50;
    side.shoulder_flex_degrees = 25;
    side.elbow_flex_degrees = 70;
  }
  const auto body = evaluated(p);
  check(point_near(body.eye_metres,
                   transformed({0, .65237, 0}, p.hip_metres, 37, 10)),
        "Root yaw follows total relative torso pitch without applying pelvis "
        "twice");
  check(point_near(box(body, BoardingBodyPartId::trunk).center_metres,
                   transformed({0, .3495, 0}, p.hip_metres, 37, 10)),
        "Trunk and pelvis share hip origin with distinct frames");
  for (std::size_t side = 0; side < 2; ++side) {
    const auto sign = side == 0 ? -1. : 1.;
    const auto& j = body.joints[side];
    const auto hip = transformed({sign * .14, 0, 0}, p.hip_metres, 37, -10);
    const auto knee = transformed({0, -.47285, 0}, hip, 37, 50);
    const auto ankle = transformed({0, -.47478, 0}, knee, 37, 0);
    const auto shoulder =
        transformed({sign * .20265, .579, 0}, p.hip_metres, 37, 10);
    const auto elbow = transformed({0, -.35898, 0}, shoulder, 37, 35);
    const auto wrist = transformed({0, -.386, 0}, elbow, 37, 105);
    check(point_near(j.hip_metres, hip) && point_near(j.knee_metres, knee) &&
              point_near(j.ankle_metres, ankle),
          "Knee flex subtracts from thigh pitch in noncommuting root frame");
    check(point_near(j.shoulder_metres, shoulder) &&
              point_near(j.elbow_metres, elbow) &&
              point_near(j.wrist_metres, wrist),
          "Elbow flex adds toward forward negative Z, opposite knee sign");
    check(point_near(j.sole_origin_metres,
                     transformed({0, -.10, 0}, ankle, 37, 0)) &&
              j.required_ankle_pitch_degrees == 0,
          "Boot remains flat under yaw rather than following shin");
    const auto hand = side == 0 ? BoardingBodyPartId::port_hand
                                : BoardingBodyPartId::starboard_hand;
    const auto boot = side == 0 ? BoardingBodyPartId::port_boot
                                : BoardingBodyPartId::starboard_boot;
    for (std::size_t column = 0; column < 3; ++column) {
      RigidVector3 basis{};
      if (column == 0)
        basis.x = 1;
      else if (column == 1)
        basis.y = 1;
      else
        basis.z = 1;
      check(point_near(box(body, hand).frame.columns[column],
                       transformed(basis, {}, 37, 10)),
            "Hand frame stays torso-bound rather than forearm-bound");
      check(point_near(box(body, boot).frame.columns[column],
                       transformed(basis, {}, 37, 0)),
            "Boot frame is yaw-only");
    }
  }
  verify_witnesses(body);
}
auto frozen_tests() -> void {
  struct Frozen {
    RigidVector3 hip;
    double pelvis, relative_torso, hip_flex;
    RigidVector3 com;
  };
  constexpr std::array frozen{
      Frozen{{-.1, .7959731743474827, -1.7},
             -10,
             10,
             100,
             {-.19826000840719174, 1.0007091024990753, -1.7}},
      Frozen{{-.1, .7959731743474827, -1.7},
             -10,
             20,
             100,
             {-.15753159897638175, .9977914961983262, -1.7}},
      Frozen{{-.05, .8028741248020458, -1.7},
             -10,
             10,
             100,
             {-.14826000840719172, 1.0076100529536385, -1.7}},
      Frozen{{-.05, .8028741248020458, -1.7},
             -10,
             20,
             100,
             {-.1075315989763818, 1.0046924466528893, -1.7}},
      Frozen{{-.05, .7958115540189412, -1.7},
             -5,
             15,
             95,
             {-.1075315989763818, .997629875869785, -1.7}}};
  for (const auto& f : frozen) {
    BoardingBodyPose p;
    p.hip_metres = f.hip;
    p.yaw_degrees = 90;
    p.pelvis_lean_degrees = f.pelvis;
    p.torso_relative_lean_degrees = f.relative_torso;
    for (auto& side : p.sides) {
      side.hip_flex_degrees = f.hip_flex;
      side.knee_flex_degrees = 90;
      side.elbow_flex_degrees = 145;
    }
    const auto body = evaluated(p);
    check(point_near(body.center_of_mass_metres, f.com),
          "Frozen hip pose reproduces independently recorded COM");
    const auto trunk_lean = f.pelvis + f.relative_torso;
    for (std::size_t side = 0; side < 2; ++side) {
      const auto sign = side == 0 ? -1. : 1.;
      const auto forearm = side == 0 ? BoardingBodyPartId::port_forearm
                                     : BoardingBodyPartId::starboard_forearm;
      const auto arm = side == 0 ? BoardingBodyPartId::port_upper_arm
                                 : BoardingBodyPartId::starboard_upper_arm;
      const auto midpoint =
          transformed({sign * .20265, .3781163445477753, -.1107002522157519},
                      f.hip, 90, trunk_lean);
      check(
          strictly_inside(part(body, forearm), midpoint) &&
              strictly_inside(part(body, BoardingBodyPartId::trunk), midpoint),
          "Frozen forearm midpoint is actual common interior, not AABB "
          "overlap");
      const auto& conflict = pair(body, forearm, BoardingBodyPartId::trunk);
      check(conflict.kind == BoardingBodyPairKind::nonadjacent &&
                conflict.intersection ==
                    BoardingBodyIntersection::interior_with_witness,
            "Every frozen forearm/trunk pair emits a nonadjacent witness");
      const auto shoulder_witness =
          transformed({sign * .148825, .579, 0}, f.hip, 90, trunk_lean);
      check(strictly_inside(part(body, arm), shoulder_witness) &&
                strictly_inside(part(body, BoardingBodyPartId::helmet),
                                shoulder_witness),
            "Intrinsic shoulder conflict persists independently of ship and "
            "pose");
      check(pair(body, arm, BoardingBodyPartId::helmet).intersection ==
                BoardingBodyIntersection::interior_with_witness,
            "Frozen shoulder/helmet actual overlap remains reported");
      check(body.joints[side].required_ankle_pitch_degrees == 0,
            "Frozen hips keep registered flat ankle relationship");
    }
    verify_witnesses(body);
  }
}
auto narrow_tests() -> void {
  using namespace apsis_drift::detail;
  const BoardingBodyBox cube{{0, 0, 0}, {1, 1, 1}, {}};
  auto other = cube;
  other.center_metres.x = 2;
  check(boarding_body_box_box(cube, other).intersection ==
            BoardingBodyIntersection::separated_or_contact,
        "Exact box face contact is not interior");
  other.center_metres.x = std::nextafter(2., 0.);
  check(interior(boarding_body_box_box(cube, other).intersection),
        "One ULP box penetration is never clearance");
  other.center_metres.x = std::nextafter(2., 3.);
  check(boarding_body_box_box(cube, other).intersection ==
            BoardingBodyIntersection::separated_or_contact,
        "One ULP box separation stays clear");
  const auto verify = [&](const auto& a, const auto& b,
                          const BoardingBodyNarrowIntersection& result) {
    check(result.intersection ==
                  BoardingBodyIntersection::interior_with_witness &&
              result.witness.has_value(),
          "Macroscopic narrow overlap has a verified witness");
    if (result.witness)
      check(strictly_inside(a, result.witness->point_metres) &&
                strictly_inside(b, result.witness->point_metres),
            "Private narrow witness independently lies strictly inside both "
            "solids");
  };
  other = cube;
  other.half_size_metres = {.1, .2, .3};
  verify(cube, other, boarding_body_box_box(cube, other));
  const BoardingBodyCapsule contained{{0, -.3, 0}, {0, .3, 0}, .1};
  verify(contained, cube, boarding_body_capsule_box(contained, cube));
  const BoardingBodyCapsule enclosing{{0, -2, 0}, {0, 2, 0}, 3};
  verify(enclosing, cube, boarding_body_capsule_box(enclosing, cube));
  const BoardingBodyCapsule first{{0, -.25, 0}, {0, .25, 0}, 1};
  auto second = first;
  second.start_metres.x = 2;
  second.end_metres.x = 2;
  check(boarding_body_capsule_capsule(first, second).intersection ==
            BoardingBodyIntersection::separated_or_contact,
        "Parallel capsules exactly touch");
  second.start_metres.x = second.end_metres.x = std::nextafter(2., 0.);
  check(interior(boarding_body_capsule_capsule(first, second).intersection),
        "One ULP capsule penetration never becomes clear");
  second.start_metres.x = second.end_metres.x = std::nextafter(2., 3.);
  check(boarding_body_capsule_capsule(first, second).intersection ==
            BoardingBodyIntersection::separated_or_contact,
        "One ULP capsule separation stays clear");
  second.start_metres.x = second.end_metres.x = .5;
  verify(first, second, boarding_body_capsule_capsule(first, second));
  auto sphere = first;
  sphere.start_metres = sphere.end_metres = {2, 0, 0};
  check(boarding_body_capsule_box(sphere, cube).intersection ==
            BoardingBodyIntersection::separated_or_contact,
        "Endpoint sphere exactly touches box face");
  sphere.start_metres.x = sphere.end_metres.x = std::nextafter(2., 0.);
  check(interior(boarding_body_capsule_box(sphere, cube).intersection),
        "One ULP capsule/box penetration is not contact");
  sphere.start_metres.x = sphere.end_metres.x = std::nextafter(2., 3.);
  check(boarding_body_capsule_box(sphere, cube).intersection ==
            BoardingBodyIntersection::separated_or_contact,
        "One ULP capsule/box separation stays clear");
  // Same 45-degree orientation, separated perpendicular to the long axis.
  // Their X/Z AABBs overlap, but their actual .20 m total thickness does not.
  const double root_half = std::sqrt(.5);
  const BoardingBodyFrame rotated{{RigidVector3{root_half, 0, -root_half},
                                   RigidVector3{0, 1, 0},
                                   RigidVector3{root_half, 0, root_half}}};
  const BoardingBodyBox thin{{0, 0, 0}, {1, .05, .1}, rotated};
  auto shifted = thin;
  shifted.center_metres = {.21 * root_half, 0, .21 * root_half};
  check(std::abs(shifted.center_metres.x) < 2 * 1.1 * root_half &&
            std::abs(shifted.center_metres.z) < 2 * 1.1 * root_half,
        "Rotated control has overlapping broad AABBs");
  check(boarding_body_box_box(thin, shifted).intersection ==
            BoardingBodyIntersection::separated_or_contact,
        "Rotated boxes use narrow geometry instead of AABB collision");
  const BoardingBodyCapsule finite_axis{{2, 0, 0}, {3, 0, 0}, .1};
  check(boarding_body_capsule_box(finite_axis, cube).intersection ==
            BoardingBodyIntersection::separated_or_contact,
        "Finite segment endpoints prevent infinite-axis box false positive");
  const BoardingBodyCapsule horizontal{{-1, 0, 0}, {1, 0, 0}, .1};
  const BoardingBodyCapsule beyond_end{{2, -1, 0}, {2, 1, 0}, .1};
  check(boarding_body_capsule_capsule(horizontal, beyond_end).intersection ==
            BoardingBodyIntersection::separated_or_contact,
        "Infinite lines crossing beyond a finite endpoint do not intersect "
        "capsules");
  const BoardingBodyCapsule skew{{0, -1, .15}, {0, 1, .15}, .1};
  verify(horizontal, skew, boarding_body_capsule_capsule(horizontal, skew));
  // Exact binary Pythagorean contact; one inward ULP is real penetration
  // even when the rounded sum of squared coordinates remains unchanged.
  const BoardingBodyCapsule tangent_first{{0, 0, 0}, {0, 0, 0}, .3125};
  auto tangent_second = tangent_first;
  tangent_second.start_metres = tangent_second.end_metres = {.375, .5, 0};
  check(boarding_body_capsule_capsule(tangent_first, tangent_second)
                .intersection == BoardingBodyIntersection::separated_or_contact,
        "Exact oblique Pythagorean capsule contact is boundary");
  tangent_second.start_metres.x = tangent_second.end_metres.x =
      std::nextafter(.375, 0.);
  const long double inward_x = tangent_second.start_metres.x;
  check(inward_x * inward_x + .25L < .625L * .625L,
        "Independent wider arithmetic proves inward oblique ULP penetration");
  check(interior(boarding_body_capsule_capsule(tangent_first, tangent_second)
                     .intersection),
        "Rounded squared equality cannot clear actual oblique ULP penetration");
  tangent_second.start_metres.x = tangent_second.end_metres.x =
      std::nextafter(.375, 1.);
  check(boarding_body_capsule_capsule(tangent_first, tangent_second)
                .intersection == BoardingBodyIntersection::separated_or_contact,
        "Outward oblique ULP capsule separation stays clear");
  const BoardingBodyCapsule oblique_first{{0, 0, 0}, {0, 0, 0}, .06};
  const BoardingBodyCapsule oblique_second{
      {0x1.26e978d4fdf36p-4, 0x1.89374bc6a7efdp-4, 0},
      {0x1.26e978d4fdf36p-4, 0x1.89374bc6a7efdp-4, 0},
      .06};
  const auto displacement = wide(oblique_second.start_metres);
  const long double radius_sum = .12;
  check(
      dot(displacement, displacement) < radius_sum * radius_sum,
      "Independent wider arithmetic proves sqrt-rounding capsule penetration");
  check(interior(boarding_body_capsule_capsule(oblique_first, oblique_second)
                     .intersection),
        "Square root rounding cannot erase strict capsule penetration");
  const BoardingBodyBox corner_box{{0, 0, 0}, {.125, .125, .125}, {}};
  const BoardingBodyCapsule corner_sphere{
      {0x1.9374bc6a7ef9dp-3, 0x1.c49ba5e353f7dp-3, 0},
      {0x1.9374bc6a7ef9dp-3, 0x1.c49ba5e353f7dp-3, 0},
      .12};
  const auto corner_delta =
      subtract(wide(corner_sphere.start_metres), {.125L, .125L, 0});
  check(dot(corner_delta, corner_delta) < radius_sum * radius_sum,
        "Independent wider arithmetic proves oblique box-corner penetration");
  check(interior(
            boarding_body_capsule_box(corner_sphere, corner_box).intersection),
        "Square root rounding cannot clear capsule/box corner penetration");
  const BoardingBodyCapsule tiny_first{{0, 0, 0}, {0, 0, 0}, 1e-250};
  const BoardingBodyCapsule tiny_second{{1e-200, 0, 0}, {1e-200, 0, 0}, 1e-250};
  check(boarding_body_capsule_capsule(tiny_first, tiny_second).intersection ==
            BoardingBodyIntersection::invalid_geometry,
        "Unsupported private scales refuse squared-distance underflow");
  const BoardingBodyBox extent_first{{0, 0, 0}, {.5, .5, .5}, {}};
  const BoardingBodyBox extent_second{
      {1, 0, 0}, {std::nextafter(.5, 1.), .5, .5}, {}};
  const RigidVector3 extent_witness{std::nextafter(.5, 0.), 0, 0};
  check(strictly_inside(extent_first, extent_witness) &&
            strictly_inside(extent_second, extent_witness),
        "Independent membership proves representable witness in "
        "extent-rounding overlap");
  const auto extent_result = boarding_body_box_box(extent_first, extent_second);
  check(interior(extent_result.intersection),
        "Rounded box half-extent sum cannot erase strict interior overlap");
  if (extent_result.witness)
    check(
        strictly_inside(extent_first, extent_result.witness->point_metres) &&
            strictly_inside(extent_second, extent_result.witness->point_metres),
        "Extent-rounding emitted witness independently lies inside both boxes");
  auto exact_extent_contact = extent_second;
  exact_extent_contact.half_size_metres.x = .5;
  check(
      boarding_body_box_box(extent_first, exact_extent_contact).intersection ==
          BoardingBodyIntersection::separated_or_contact,
      "Exact half-plus-half box contact remains boundary");
  const BoardingBodyBox displacement_first{{-.5, 0, 0}, {.5, .5, .5}, {}};
  const BoardingBodyBox displacement_second{
      {std::nextafter(.5, 0.), 0, 0}, {.5, .5, .5}, {}};
  const RigidVector3 displacement_witness{-0x1p-55, 0, 0};
  check(strictly_inside(displacement_first, displacement_witness) &&
            strictly_inside(displacement_second, displacement_witness),
        "Independent membership proves real center-difference cancellation "
        "overlap");
  check(interior(boarding_body_box_box(displacement_first, displacement_second)
                     .intersection),
        "Rounded projected center difference cannot clear strict box overlap");
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto inf = std::numeric_limits<double>::infinity();
  for (const double value : {0., -1., nan, inf}) {
    other = cube;
    other.half_size_metres.x = value;
    check(boarding_body_box_box(cube, other).intersection ==
              BoardingBodyIntersection::invalid_geometry,
          "Invalid box half-size refuses before intersection");
    auto invalid = first;
    invalid.radius_metres = value;
    check(boarding_body_capsule_capsule(first, invalid).intersection ==
                  BoardingBodyIntersection::invalid_geometry &&
              boarding_body_capsule_box(invalid, cube).intersection ==
                  BoardingBodyIntersection::invalid_geometry,
          "Invalid capsule radius refuses across both queries");
  }
  for (const double value : {nan, inf, -inf}) {
    other = cube;
    other.center_metres.y = value;
    check(boarding_body_box_box(cube, other).intersection ==
              BoardingBodyIntersection::invalid_geometry,
          "Nonfinite box center refuses");
    auto invalid = first;
    invalid.end_metres.z = value;
    check(boarding_body_capsule_capsule(first, invalid).intersection ==
              BoardingBodyIntersection::invalid_geometry,
          "Nonfinite capsule endpoint refuses");
    other = cube;
    other.frame.columns[2].z = value;
    check(boarding_body_box_box(cube, other).intersection ==
              BoardingBodyIntersection::invalid_geometry,
          "Nonfinite frame buffer entry refuses");
  }
  other = cube;
  other.frame.columns[2] = {};
  check(boarding_body_box_box(cube, other).intersection ==
            BoardingBodyIntersection::invalid_geometry,
        "Missing third frame axis refuses");
  other = cube;
  other.frame.columns[2] = other.frame.columns[0];
  check(boarding_body_box_box(cube, other).intersection ==
            BoardingBodyIntersection::invalid_geometry,
        "Duplicate frame axes refuse");
  other = cube;
  other.frame.columns[0].x = 1.01;
  check(boarding_body_box_box(cube, other).intersection ==
            BoardingBodyIntersection::invalid_geometry,
        "Scaled frame refuses rather than altering dimensions");
  other = cube;
  other.frame.columns[1].x = .01;
  check(boarding_body_box_box(cube, other).intersection ==
            BoardingBodyIntersection::invalid_geometry,
        "Sheared frame refuses malformed orientation");
  other = cube;
  other.frame.columns[2].z = -1;
  check(boarding_body_box_box(cube, other).intersection ==
            BoardingBodyIntersection::invalid_geometry,
        "Reflected frame refuses improper rotation");
  check(boarding_body_box_interior_depth(cube, {1, 0, 0}) == 0 &&
            boarding_body_capsule_interior_depth(first, {1, 0, 0}) == 0,
        "Exact per-shape surface has zero depth");
}
} // namespace
int main() {
  try {
    validation_tests();
    canonical_tests();
    frame_tests();
    frozen_tests();
    narrow_tests();
  } catch (const std::exception& error) {
    std::cerr << "Exception: " << error.what() << '\n';
    return 1;
  }
  std::cout << checks << " boarding body checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
