#include "apsis_drift/origin_boarding_self_model02.hpp"
#include "origin_boarding_self_model02_internal.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
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
auto bits(double a, double b) -> bool {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
auto bits(RigidVector3 a, RigidVector3 b) -> bool {
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
auto canonical_identity(const BoardingBodyDiagnostic& a,
                        const BoardingBodyDiagnostic& b) -> void {
  check(a.policy_version == b.policy_version, "Canonical version identity");
  check(bits(a.eye_metres, b.eye_metres) &&
            bits(a.center_of_mass_metres, b.center_of_mass_metres),
        "Canonical eye and COM bit identity");
  for (std::size_t i = 0; i < a.parts.size(); ++i) {
    const auto& x = a.parts[i];
    const auto& y = b.parts[i];
    check(x.id == y.id && x.mass_weight == y.mass_weight &&
              bits(x.mass_point_metres, y.mass_point_metres),
          "Every canonical named mass is unchanged");
    check(x.world_reservation.index() == y.world_reservation.index(),
          "Canonical world shape category unchanged");
    std::visit(
        [&](const auto& shape) {
          using Shape = std::decay_t<decltype(shape)>;
          const auto* other = std::get_if<Shape>(&y.world_reservation);
          check(other && bits(shape, *other),
                "Every world center/frame/dimension/endpoint bit unchanged");
        },
        x.world_reservation);
  }
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& x = a.joints[side];
    const auto& y = b.joints[side];
    for (const auto member : {&BoardingBodySideJoints::hip_metres,
                              &BoardingBodySideJoints::knee_metres,
                              &BoardingBodySideJoints::ankle_metres,
                              &BoardingBodySideJoints::sole_origin_metres,
                              &BoardingBodySideJoints::shoulder_metres,
                              &BoardingBodySideJoints::elbow_metres,
                              &BoardingBodySideJoints::wrist_metres})
      check(bits(x.*member, y.*member),
            "Actual canonical joint/sole bit identity");
    check(bits(x.required_ankle_pitch_degrees, y.required_ankle_pitch_degrees),
          "Derived flat ankle unchanged");
  }
  for (std::size_t i = 0; i < a.pairs.size(); ++i) {
    const auto& x = a.pairs[i];
    const auto& y = b.pairs[i];
    check(x.first == y.first && x.second == y.second && x.kind == y.kind &&
              x.intersection == y.intersection &&
              x.witness.has_value() == y.witness.has_value(),
          "Old Model01 pair diagnostic retained");
    if (x.witness && y.witness)
      check(bits(x.witness->point_metres, y.witness->point_metres) &&
                bits(x.witness->first_depth_metres,
                     y.witness->first_depth_metres) &&
                bits(x.witness->second_depth_metres,
                     y.witness->second_depth_metres),
            "Old Model01 witness bits retained");
  }
  check(a.self_conflict == b.self_conflict &&
            a.unresolved_intersection == b.unresolved_intersection,
        "Old Model01 conflict flags retained");
}
struct Wide {
  long double x{}, y{}, z{};
};
auto wide(RigidVector3 p) -> Wide {
  return {p.x, p.y, p.z};
}
auto sub(Wide a, Wide b) -> Wide {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(Wide a, long double k) -> Wide {
  return {a.x * k, a.y * k, a.z * k};
}
auto dot(Wide a, Wide b) -> long double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
auto cross(Wide a, Wide b) -> Wide {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto norm(Wide a) -> long double {
  return std::sqrt(dot(a, a));
}
// Independent inverse of actual columns. No producer frame, membership,
// support, cap, or predicate helper is used as the expected geometric oracle.
auto inverse_coordinate(const BoardingBodyFrame& frame, Wide p) -> Wide {
  const auto a = wide(frame.columns[0]), b = wide(frame.columns[1]),
             c = wide(frame.columns[2]);
  const auto determinant = dot(a, cross(b, c));
  if (!std::isfinite(determinant) || determinant == 0)
    throw std::runtime_error("Oracle singular frame");
  return {dot(p, cross(b, c)) / determinant, dot(p, cross(c, a)) / determinant,
          dot(p, cross(a, b)) / determinant};
}
auto strictly_inside(const BoardingBodyBox& s, RigidVector3 point) -> bool {
  const auto p =
      inverse_coordinate(s.frame, sub(wide(point), wide(s.center_metres)));
  return std::abs(p.x) < s.half_size_metres.x &&
         std::abs(p.y) < s.half_size_metres.y &&
         std::abs(p.z) < s.half_size_metres.z;
}
auto strictly_inside(const BoardingSelfEllipsoid& s, RigidVector3 point)
    -> bool {
  const auto p =
      inverse_coordinate(s.frame, sub(wide(point), wide(s.center_metres)));
  const auto x = p.x / s.semi_axes_metres.x, y = p.y / s.semi_axes_metres.y,
             z = p.z / s.semi_axes_metres.z;
  return x * x + y * y + z * z < 1;
}
auto capsule_distance_squared(const BoardingBodyCapsule& s, RigidVector3 point)
    -> long double {
  const auto d = sub(wide(s.end_metres), wide(s.start_metres));
  const auto p = sub(wide(point), wide(s.start_metres));
  const auto dd = dot(d, d);
  const auto t = dd == 0 ? 0.L : std::clamp(dot(p, d) / dd, 0.L, 1.L);
  const auto r = sub(p, scale(d, t));
  return dot(r, r);
}
auto strictly_inside(const BoardingBodyCapsule& s, RigidVector3 p) -> bool {
  const long double r = s.radius_metres;
  return capsule_distance_squared(s, p) < r * r;
}
auto strictly_inside(const BoardingSelfSolid& s, RigidVector3 p) -> bool {
  return std::visit(
      [&](const auto& shape) { return strictly_inside(shape, p); }, s);
}
auto strictly_outside(const BoardingSelfRegion& region, RigidVector3 point)
    -> bool {
  return std::visit(
      [&](const auto& shape) {
        using Shape = std::decay_t<decltype(shape)>;
        if constexpr (std::is_same_v<Shape, BoardingSelfSphere>) {
          const auto d = sub(wide(point), wide(shape.center_metres));
          const long double r = shape.radius_metres;
          return dot(d, d) > r * r;
        } else if constexpr (std::is_same_v<Shape, BoardingBodyCapsule>) {
          const long double r = shape.radius_metres;
          return capsule_distance_squared(shape, point) > r * r;
        } else {
          return inverse_coordinate(
                     shape.axial_frame,
                     sub(wide(point), wide(shape.axial_origin_metres)))
                     .y > shape.axial_limit_metres;
        }
      },
      region);
}
auto support(const BoardingSelfSolid& solid, Wide n) -> long double {
  return std::visit(
      [&](const auto& s) -> long double {
        using Shape = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<Shape, BoardingBodyCapsule>)
          return std::max(dot(wide(s.start_metres), n),
                          dot(wide(s.end_metres), n)) +
                 s.radius_metres * norm(n);
        else {
          RigidVector3 half;
          if constexpr (std::is_same_v<Shape, BoardingSelfEllipsoid>)
            half = s.semi_axes_metres;
          else
            half = s.half_size_metres;
          const auto x = half.x * dot(wide(s.frame.columns[0]), n),
                     y = half.y * dot(wide(s.frame.columns[1]), n),
                     z = half.z * dot(wide(s.frame.columns[2]), n);
          const auto offset = std::is_same_v<Shape, BoardingSelfEllipsoid>
                                  ? std::sqrt(x * x + y * y + z * z)
                                  : std::abs(x) + std::abs(y) + std::abs(z);
          return dot(wide(s.center_metres), n) + offset;
        }
      },
      solid);
}
struct ExpectedRegion {
  unsigned first, second;
  BoardingSelfJunction junction;
};
constexpr std::array expected_regions{
    ExpectedRegion{0, 1, BoardingSelfJunction::waist},
    ExpectedRegion{0, 3, BoardingSelfJunction::hip},
    ExpectedRegion{0, 9, BoardingSelfJunction::hip},
    ExpectedRegion{1, 2, BoardingSelfJunction::neck},
    ExpectedRegion{1, 6, BoardingSelfJunction::shoulder},
    ExpectedRegion{1, 12, BoardingSelfJunction::shoulder},
    ExpectedRegion{3, 4, BoardingSelfJunction::knee},
    ExpectedRegion{4, 5, BoardingSelfJunction::ankle},
    ExpectedRegion{6, 7, BoardingSelfJunction::elbow},
    ExpectedRegion{7, 8, BoardingSelfJunction::wrist},
    ExpectedRegion{9, 10, BoardingSelfJunction::knee},
    ExpectedRegion{10, 11, BoardingSelfJunction::ankle},
    ExpectedRegion{12, 13, BoardingSelfJunction::elbow},
    ExpectedRegion{13, 14, BoardingSelfJunction::wrist}};
auto part_index(BoardingBodyPartId id) -> std::size_t {
  return static_cast<std::size_t>(id);
}
auto region_index(unsigned a, unsigned b) -> std::optional<std::size_t> {
  for (std::size_t i = 0; i < expected_regions.size(); ++i)
    if (expected_regions[i].first == a && expected_regions[i].second == b)
      return i;
  return std::nullopt;
}
auto pair(const BoardingSelfDiagnostic& body, unsigned a, unsigned b)
    -> const BoardingSelfPairDiagnostic& {
  for (const auto& p : body.pairs)
    if (part_index(p.first) == a && part_index(p.second) == b) return p;
  throw std::runtime_error("Missing actual pair");
}
auto evaluated(const BoardingBodyPose& pose) -> BoardingSelfDiagnostic {
  const auto got = evaluate_origin_boarding_self_model02(pose);
  if (!got) {
    throw std::runtime_error("Registered control refused before diagnostic");
  }
  return *got;
}
auto registered_positive() -> BoardingBodyPose {
  BoardingBodyPose p;
  for (auto& s : p.sides) {
    s.shoulder_flex_degrees = 45;
    s.elbow_flex_degrees = 135;
  }
  return p;
}
auto constants_and_invariants() -> void {
  check(kBoardingSelfModel02Version == 2 &&
            kBoardingSelfConnectedRegionCount == 14,
        "Separately registered model version/count");
  check(bits(kBoardingSelfWaistLimitMetres, 0x1.bb0cd605d7512p-3) &&
            bits(kBoardingSelfNeckLimitMetres, 0x1.3ced916872b02p-1),
        "Frozen waist/neck bits");
  check(bits(kBoardingSelfHipLengthMetres, 0x1.bb0cd605d7512p-3) &&
            bits(kBoardingSelfKneeRadiusMetres, 0x1.18f69ad39e925p-2),
        "Frozen hip/knee bits");
  check(bits(kBoardingSelfAnkleLengthMetres, 0x1.7529d1ebf8034p-3) &&
            bits(kBoardingSelfShoulderRadiusMetres, 0x1.56872b020c49cp-2),
        "Frozen ankle/shoulder bits");
  check(bits(kBoardingSelfElbowRadiusMetres, 0x1.bab11b9fbb660p-3) &&
            bits(kBoardingSelfWristLengthMetres, 0x1.12c49dd0cc1e9p-4),
        "Frozen elbow/wrist bits");
  check(BoardingSelfDiagnostic::recipe_complete &&
            !BoardingSelfDiagnostic::arbitrary_pose_proof_complete,
        "Recipe completeness separate from arbitrary-pose proof coverage");
  check(!BoardingSelfDiagnostic::actor_qualified &&
            !BoardingSelfDiagnostic::world_qualified &&
            !BoardingSelfDiagnostic::support_qualified &&
            !BoardingSelfDiagnostic::route_qualified,
        "Self checkpoint grants no actor/world/support/route authority");
}
auto validation_tests() -> void {
  auto refuses = [](const BoardingBodyPose& p, BoardingBodyError expected) {
    const auto got = evaluate_origin_boarding_self_model02(p);
    check(!got && got.error() == expected,
          "New evaluator preserves explicit Model01 validation error");
  };
  BoardingBodyPose p;
  p.policy_version = 0;
  refuses(p, BoardingBodyError::unsupported_policy);
  p.policy_version = 2;
  refuses(p, BoardingBodyError::unsupported_policy);
  for (const auto member :
       {&BoardingBodyPose::suit_on, &BoardingBodyPose::pack_detached,
        &BoardingBodyPose::flat_boots}) {
    p = {};
    p.*member = false;
    refuses(p, BoardingBodyError::invalid_prerequisite);
  }
  constexpr std::array fields{&BoardingBodyPose::yaw_degrees,
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
  for (const auto bad : {std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity()}) {
    for (const auto member : fields) {
      p = {};
      p.*member = bad;
      refuses(p, BoardingBodyError::non_finite_input);
    }
    for (std::size_t side = 0; side < 2; ++side)
      for (const auto member : side_fields) {
        p = {};
        p.sides[side].*member = bad;
        refuses(p, BoardingBodyError::non_finite_input);
      }
    for (std::size_t axis = 0; axis < 3; ++axis) {
      p = {};
      if (axis == 0)
        p.hip_metres.x = bad;
      else if (axis == 1)
        p.hip_metres.y = bad;
      else
        p.hip_metres.z = bad;
      refuses(p, BoardingBodyError::non_finite_input);
    }
  }
  for (const auto member : {&BoardingBodyPose::pelvis_twist_degrees,
                            &BoardingBodyPose::torso_twist_degrees,
                            &BoardingBodyPose::neck_pitch_degrees,
                            &BoardingBodyPose::neck_yaw_degrees,
                            &BoardingBodyPose::neck_roll_degrees}) {
    p = {};
    p.*member = std::numeric_limits<double>::denorm_min();
    refuses(p, BoardingBodyError::unsupported_freedom);
  }
  for (std::size_t side = 0; side < 2; ++side)
    for (const auto member : {&BoardingBodySidePose::hip_abduction_degrees,
                              &BoardingBodySidePose::hip_axial_degrees,
                              &BoardingBodySidePose::ankle_roll_degrees,
                              &BoardingBodySidePose::shoulder_abduction_degrees,
                              &BoardingBodySidePose::shoulder_axial_degrees,
                              &BoardingBodySidePose::wrist_pitch_degrees,
                              &BoardingBodySidePose::wrist_yaw_degrees,
                              &BoardingBodySidePose::wrist_roll_degrees}) {
      p = {};
      p.sides[side].*member = std::numeric_limits<double>::denorm_min();
      refuses(p, BoardingBodyError::unsupported_freedom);
    }
  p = {};
  p.hip_metres.x = std::nextafter(8., 9.);
  refuses(p, BoardingBodyError::excessive_workspace);
  p = {};
  p.yaw_degrees = std::nextafter(180., 181.);
  refuses(p, BoardingBodyError::joint_limit);
  p = {};
  p.pelvis_lean_degrees = 35;
  p.torso_relative_lean_degrees = 1;
  refuses(p, BoardingBodyError::joint_limit);
  p = {};
  p.sides[0].knee_flex_degrees = 136;
  refuses(p, BoardingBodyError::joint_limit);
  p = {};
  p.sides[1].elbow_flex_degrees = 146;
  refuses(p, BoardingBodyError::joint_limit);
}
auto common_checks(const BoardingBodyPose& pose,
                   const BoardingSelfDiagnostic& body) -> void {
  const auto canonical = evaluate_origin_boarding_body(pose);
  if (!canonical) throw std::runtime_error("Canonical control invalid");
  canonical_identity(body.canonical, *canonical);
  check(body.self_model_version == 2,
        "Output identifies separate self version");
  for (std::size_t i = 0; i < body.self_parts.size(); ++i) {
    const auto& s = body.self_parts[i];
    const auto& w = canonical->parts[i];
    check(s.id == w.id, "Self part enumeration preserves all 15 IDs");
    if (i == 1 || i == 2) {
      const auto* e = std::get_if<BoardingSelfEllipsoid>(&s.solid);
      const auto* b = std::get_if<BoardingBodyBox>(&w.world_reservation);
      check(e && b && bits(e->center_metres, b->center_metres) &&
                bits(e->semi_axes_metres, b->half_size_metres) &&
                bits(e->frame, b->frame),
            "Only trunk/helmet use same-extrema affine self ellipsoids");
    } else
      std::visit(
          [&](const auto& shape) {
            using Shape = std::decay_t<decltype(shape)>;
            const auto* other = std::get_if<Shape>(&s.solid);
            check(other && bits(shape, *other),
                  "Other self solids retain exact canonical data");
          },
          w.world_reservation);
  }
  for (std::size_t i = 0; i < body.regions.size(); ++i) {
    const auto& r = body.regions[i];
    const auto& e = expected_regions[i];
    check(part_index(r.first) == e.first && part_index(r.second) == e.second &&
              r.junction == e.junction,
          "14 finite connected regions have frozen lexicographic mapping");
  }
  std::size_t cursor{};
  bool actual_conflict{}, actual_unresolved{}, all_certified = true;
  for (unsigned a = 0; a < 15; ++a)
    for (unsigned b = a + 1; b < 15; ++b) {
      const auto& record = body.pairs[cursor++];
      check(part_index(record.first) == a && part_index(record.second) == b,
            "105 pairs are complete deterministic lexicographic enumeration");
      check(record.connected_region_index == region_index(a, b),
            "Every pair has exact finite-region binding or no ownership");
      const bool strict =
          record.outcome ==
              BoardingSelfPairOutcome::strict_nonadjacent_interior ||
          record.outcome ==
              BoardingSelfPairOutcome::strict_unowned_connected_interior;
      const bool unresolved =
          record.outcome == BoardingSelfPairOutcome::unresolved;
      actual_conflict = actual_conflict || strict;
      actual_unresolved = actual_unresolved || unresolved;
      check(record.strict_witness_metres.has_value() == strict,
            "Only strict refusals carry verified common-interior witnesses");
      check(unresolved ? record.reason != BoardingSelfUnresolved::none
                       : record.reason == BoardingSelfUnresolved::none,
            "Unresolved reason is explicit and separate from proved result");
      if (record.strict_witness_metres) {
        const auto point = *record.strict_witness_metres;
        check(strictly_inside(body.self_parts[a].solid, point) &&
                  strictly_inside(body.self_parts[b].solid, point),
              "Independent actual affine/finite-solid membership verifies "
              "every emitted witness");
        if (record.connected_region_index)
          check(strictly_outside(
                    body.regions[*record.connected_region_index].region, point),
                "Connected refusal witness is strictly outside its finite "
                "region");
      }
      const bool accepted =
          record.outcome == BoardingSelfPairOutcome::certified_no_interior ||
          (record.connected_region_index &&
           record.outcome == BoardingSelfPairOutcome::certified_whole_owned);
      all_certified = all_certified && accepted;
      if (!record.connected_region_index)
        check(
            record.outcome != BoardingSelfPairOutcome::certified_whole_owned &&
                record.outcome !=
                    BoardingSelfPairOutcome::strict_unowned_connected_interior,
            "Nonadjacent pair receives no finite ownership exemption");
    }
  check(cursor == 105, "All 105 pair entries evaluated");
  check(body.strict_conflict == actual_conflict &&
            body.unresolved_pair == actual_unresolved,
        "Summary flags reflect every pair, not early exit");
  check(body.self_checkpoint_passed == all_certified,
        "Self checkpoint accepts exactly the complete pair acceptance matrix");
}
struct ReferencePlane {
  unsigned first, second;
  RigidVector3 normal;
};
// Independent frozen exact-rational prototype certificates, not production
// candidate selection. Every plane has millimetres of strict separation.
constexpr std::array reference_planes{
    ReferencePlane{0, 2, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{0, 4, {0x0.0p+0, -0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{0, 5, {0x0.0p+0, -0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{0, 6, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{0, 7, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{0, 8, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{0, 10, {0x0.0p+0, -0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{0, 11, {0x0.0p+0, -0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{0, 12, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{0, 13, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{0, 14, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{
        1, 3, {-0x1.1eb851eb851ecp-3, -0x1.65e353f7ced90p-2, 0x0.0p+0}},
    ReferencePlane{1, 4, {0x0.0p+0, -0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{1, 5, {0x0.0p+0, -0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{1, 7, {0x0.0p+0, 0x0.0p+0, -0x1.0000000000000p+0}},
    ReferencePlane{1, 8, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{
        1, 9, {0x1.1eb851eb851ecp-3, -0x1.65e353f7ced90p-2, 0x0.0p+0}},
    ReferencePlane{1, 10, {0x0.0p+0, -0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{1, 11, {0x0.0p+0, -0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{1, 13, {0x0.0p+0, 0x0.0p+0, -0x1.0000000000000p+0}},
    ReferencePlane{1, 14, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{2, 3, {0x0.0p+0, -0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{2, 4, {0x0.0p+0, -0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{2, 5, {0x0.0p+0, -0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{
        2, 6, {-0x1.9f06f69446738p-3, -0x1.f952d234eb9a0p-4, 0x0.0p+0}},
    ReferencePlane{2, 7, {0x0.0p+0, 0x0.0p+0, -0x1.0000000000000p+0}},
    ReferencePlane{2, 8, {-0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{2, 9, {0x0.0p+0, -0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{2, 10, {0x0.0p+0, -0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{2, 11, {0x0.0p+0, -0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{
        2, 12, {0x1.9f06f69446738p-3, -0x1.f952d234eb9a0p-4, 0x0.0p+0}},
    ReferencePlane{2, 13, {0x0.0p+0, 0x0.0p+0, -0x1.0000000000000p+0}},
    ReferencePlane{2, 14, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{3, 5, {0x0.0p+0, -0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{3, 6, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{3, 7, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{3, 8, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{3, 9, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{3, 10, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{3, 11, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{3, 12, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{3, 13, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{3, 14, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{4, 6, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{4, 7, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{4, 8, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{4, 9, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{4, 10, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{4, 11, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{4, 12, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{4, 13, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{4, 14, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{5, 6, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{5, 7, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{5, 8, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{5, 9, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{5, 10, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{5, 11, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{5, 12, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{5, 13, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{5, 14, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{6, 8, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{6, 9, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{6, 10, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{6, 11, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{6, 12, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{6, 13, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{6, 14, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{7, 9, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{7, 10, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{7, 11, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{7, 12, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{7, 13, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{7, 14, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{8, 9, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{8, 10, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{8, 11, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{8, 12, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{8, 13, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{8, 14, {0x1.0000000000000p+0, 0x0.0p+0, 0x0.0p+0}},
    ReferencePlane{9, 11, {0x0.0p+0, -0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{9, 12, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{9, 13, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{9, 14, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{10, 12, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{10, 13, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{10, 14, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{11, 12, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{11, 13, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{11, 14, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
    ReferencePlane{12, 14, {0x0.0p+0, 0x1.0000000000000p+0, 0x0.0p+0}},
};
auto identity_frame(const BoardingBodyFrame& f) -> bool {
  return f.columns == BoardingBodyFrame{}.columns;
}
auto own_oracle(const BoardingSelfDiagnostic& body,
                const BoardingSelfConnectedRegion& r) -> bool {
  const auto a = part_index(r.first), b = part_index(r.second);
  const std::size_t side = (a >= 9 || b >= 9) ? 1 : 0;
  const auto& joint = body.canonical.joints[side];
  switch (r.junction) {
    case BoardingSelfJunction::waist: {
      const auto& cap = std::get<BoardingSelfEllipsoidCap>(r.region);
      if (!identity_frame(cap.axial_frame)) return false;
      return support(body.self_parts[0].solid, {0, 1, 0}) -
                 cap.axial_origin_metres.y <=
             cap.axial_limit_metres;
    }
    case BoardingSelfJunction::neck: {
      const auto& cap = std::get<BoardingSelfEllipsoidCap>(r.region);
      if (!identity_frame(cap.axial_frame)) return false;
      return support(body.self_parts[1].solid, {0, 1, 0}) -
                 cap.axial_origin_metres.y <=
             cap.axial_limit_metres;
    }
    case BoardingSelfJunction::hip: {
      const auto& cap = std::get<BoardingBodyCapsule>(r.region);
      const auto& thigh =
          std::get<BoardingBodyCapsule>(body.self_parts[b].solid);
      const auto d = sub(wide(thigh.end_metres), wide(thigh.start_metres)),
                 e = sub(wide(cap.end_metres), wide(cap.start_metres));
      return d.x == 0 && d.z == 0 && d.y < 0 && e.x == 0 && e.z == 0 &&
             e.y < 0 && bits(cap.start_metres, thigh.start_metres) &&
             bits(cap.radius_metres, thigh.radius_metres) &&
             wide(cap.start_metres).y +
                     support(body.self_parts[0].solid, {0, -1, 0}) <=
                 -e.y;
    }
    case BoardingSelfJunction::knee: {
      const auto& sphere = std::get<BoardingSelfSphere>(r.region);
      const auto& thigh =
          std::get<BoardingBodyCapsule>(body.self_parts[a].solid);
      const auto& shin =
          std::get<BoardingBodyCapsule>(body.self_parts[b].solid);
      const auto u = sub(wide(thigh.start_metres), wide(sphere.center_metres)),
                 v = sub(wide(shin.end_metres), wide(sphere.center_metres));
      return bits(sphere.center_metres, thigh.end_metres) &&
             bits(sphere.center_metres, shin.start_metres) && u.x == 0 &&
             u.z == 0 && u.y > 0 && v.x == 0 && v.z == 0 && v.y < 0 &&
             std::max(thigh.radius_metres, shin.radius_metres) <=
                 sphere.radius_metres;
    }
    case BoardingSelfJunction::ankle: {
      const auto& cap = std::get<BoardingBodyCapsule>(r.region);
      const auto& shin =
          std::get<BoardingBodyCapsule>(body.self_parts[a].solid);
      const auto d = sub(wide(shin.start_metres), wide(shin.end_metres));
      return bits(cap.start_metres, shin.end_metres) &&
             bits(cap.radius_metres, shin.radius_metres) && d.x == 0 &&
             d.z == 0 && d.y > 0 &&
             support(body.self_parts[b].solid, {0, 1, 0}) -
                     joint.ankle_metres.y <=
                 0;
    }
    case BoardingSelfJunction::shoulder: {
      const auto& sphere = std::get<BoardingSelfSphere>(r.region);
      const auto& arm = std::get<BoardingBodyCapsule>(body.self_parts[b].solid);
      const auto d = sub(wide(arm.end_metres), wide(arm.start_metres));
      const auto uz = d.z / norm(d);
      const long double radius = arm.radius_metres, R = sphere.radius_metres;
      if (!bits(sphere.center_metres, arm.start_metres) || R < radius ||
          uz >= 0)
        return false;
      const auto threshold = std::sqrt(R * R - radius * radius);
      const auto shaft_z =
          arm.start_metres.z + threshold * uz + radius * std::sqrt(1 - uz * uz);
      const auto distal_z = static_cast<long double>(arm.end_metres.z) + radius;
      const auto min_z = -support(body.self_parts[1].solid, {0, 0, -1});
      return shaft_z < min_z && distal_z < min_z;
    }
    case BoardingSelfJunction::elbow: {
      const auto& sphere = std::get<BoardingSelfSphere>(r.region);
      const auto& upper =
          std::get<BoardingBodyCapsule>(body.self_parts[a].solid);
      const auto& fore =
          std::get<BoardingBodyCapsule>(body.self_parts[b].solid);
      const auto u = sub(wide(upper.start_metres), wide(sphere.center_metres)),
                 v = sub(wide(fore.end_metres), wide(sphere.center_metres));
      const auto cosine = dot(u, v) / (norm(u) * norm(v));
      const auto sin2 = (1 - cosine) / 2;
      const long double radius =
                            std::max(upper.radius_metres, fore.radius_metres),
                        R = sphere.radius_metres;
      return bits(sphere.center_metres, upper.end_metres) &&
             bits(sphere.center_metres, fore.start_metres) && sin2 > 0 &&
             radius * radius / sin2 <= R * R;
    }
    case BoardingSelfJunction::wrist: {
      const auto& cap = std::get<BoardingBodyCapsule>(r.region);
      const auto& fore =
          std::get<BoardingBodyCapsule>(body.self_parts[a].solid);
      const auto& hand = std::get<BoardingBodyBox>(body.self_parts[b].solid);
      const auto prefix = sub(wide(cap.end_metres), wide(cap.start_metres)),
                 d = sub(wide(fore.start_metres), wide(fore.end_metres));
      if (!identity_frame(hand.frame) ||
          !bits(hand.center_metres, fore.end_metres) ||
          !bits(cap.start_metres, fore.end_metres) ||
          !bits(cap.radius_metres, fore.radius_metres) || prefix.x != 0 ||
          prefix.z != 0 || prefix.y >= 0 || d.y >= 0)
        return false;
      const long double hx = hand.half_size_metres.x,
                        hy = hand.half_size_metres.y,
                        hz = hand.half_size_metres.z,
                        radius = fore.radius_metres, band = hz / 2;
      return hx * hx + hz * hz + band * band < radius * radius &&
             hx * hx + hz * hz < radius * radius && hy < -prefix.y &&
             d.y * band + std::abs(d.x) * hx + std::abs(d.z) * hz < 0;
    }
  }
  return false;
}
auto region_shapes(const BoardingBodyPose& pose,
                   const BoardingSelfDiagnostic& body) -> void {
  const auto& trunk =
      std::get<BoardingBodyBox>(body.canonical.parts[1].world_reservation);
  for (const auto& r : body.regions) {
    const auto first = part_index(r.first), second = part_index(r.second);
    const auto side = (first >= 9 || second >= 9) ? 1U : 0U;
    const auto& joint = body.canonical.joints[side];
    if (r.junction == BoardingSelfJunction::waist ||
        r.junction == BoardingSelfJunction::neck) {
      const auto* cap = std::get_if<BoardingSelfEllipsoidCap>(&r.region);
      check(cap != nullptr,
            "Waist/neck region is a clipped actual self ellipsoid");
      if (!cap) continue;
      const auto solid_index =
          r.junction == BoardingSelfJunction::waist ? 1U : 2U;
      const auto& e =
          std::get<BoardingSelfEllipsoid>(body.self_parts[solid_index].solid);
      check(bits(cap->ellipsoid.center_metres, e.center_metres) &&
                bits(cap->ellipsoid.semi_axes_metres, e.semi_axes_metres) &&
                bits(cap->ellipsoid.frame, e.frame),
            "Ownership cap retains exactly its named actual ellipsoid");
      check(bits(cap->axial_origin_metres, pose.hip_metres) &&
                bits(cap->axial_frame, trunk.frame),
            "Cap inverse frame is actual Rt at actual hip");
      check(bits(cap->axial_limit_metres,
                 r.junction == BoardingSelfJunction::waist
                     ? 0x1.bb0cd605d7512p-3
                     : 0x1.3ced916872b02p-1),
            "Cap uses frozen limit directly, not pose-rounded plane");
    } else if (r.junction == BoardingSelfJunction::knee ||
               r.junction == BoardingSelfJunction::shoulder ||
               r.junction == BoardingSelfJunction::elbow) {
      const auto* sphere = std::get_if<BoardingSelfSphere>(&r.region);
      check(sphere != nullptr,
            "Knee/shoulder/elbow retains finite joint sphere");
      if (!sphere) continue;
      const auto center =
          r.junction == BoardingSelfJunction::knee       ? joint.knee_metres
          : r.junction == BoardingSelfJunction::shoulder ? joint.shoulder_metres
                                                         : joint.elbow_metres;
      const auto radius =
          r.junction == BoardingSelfJunction::knee       ? 0x1.18f69ad39e925p-2
          : r.junction == BoardingSelfJunction::shoulder ? 0x1.56872b020c49cp-2
                                                         : 0x1.bab11b9fbb660p-3;
      check(bits(sphere->center_metres, center) &&
                bits(sphere->radius_metres, radius),
            "Finite sphere anchor/radius identity");
    } else {
      const auto* cap = std::get_if<BoardingBodyCapsule>(&r.region);
      check(cap != nullptr, "Hip/ankle/wrist retains finite actual prefix");
      if (!cap) continue;
      const auto start =
          r.junction == BoardingSelfJunction::hip     ? joint.hip_metres
          : r.junction == BoardingSelfJunction::ankle ? joint.ankle_metres
                                                      : joint.wrist_metres;
      const auto radius = r.junction == BoardingSelfJunction::hip     ? .105
                          : r.junction == BoardingSelfJunction::ankle ? .075
                                                                      : .055;
      check(bits(cap->start_metres, start) && bits(cap->radius_metres, radius),
            "Finite prefix uses original joint and radius");
    }
  }
}
auto positive_tests() -> void {
  const auto p = registered_positive();
  const auto body = evaluated(p);
  common_checks(p, body);
  region_shapes(p, body);
  check(body.self_checkpoint_passed && !body.strict_conflict &&
            !body.unresolved_pair,
        "Registered standing is independently self-positive with complete "
        "evidence");
  check(reference_planes.size() == 91, "Independent exact-rational reference "
                                       "has all 91 nonadjacent certificates");
  for (const auto& plane : reference_planes) {
    const auto& actual = pair(body, plane.first, plane.second);
    check(!actual.connected_region_index &&
              actual.outcome == BoardingSelfPairOutcome::certified_no_interior,
          "Every nonadjacent positive pair is actually certified");
    const auto n = wide(plane.normal);
    const auto gap =
        -support(body.self_parts[plane.second].solid, scale(n, -1)) -
        support(body.self_parts[plane.first].solid, n);
    check(gap > 0,
          "Independent full convex support proves separation with no samples");
  }
  for (const auto& r : body.regions) {
    const auto& actual = pair(body, static_cast<unsigned>(r.first),
                              static_cast<unsigned>(r.second));
    check(actual.outcome == BoardingSelfPairOutcome::certified_whole_owned,
          "All 14 registered positive connected pairs have whole containment "
          "evidence");
    check(own_oracle(body, r), "Independent complete curved/prefix/cap "
                               "argument verifies actual whole ownership");
  }
  const auto& trunk = std::get<BoardingSelfEllipsoid>(body.self_parts[1].solid);
  // Exact golden integer arithmetic, independent of long-double precision.
  // These four finite positive values lie in [.25,2), so multiplying their
  // 53-bit significands by at most4 yields exact units of2^-54, all <2^56.
  auto dyadic_units = [](double value) -> std::int64_t {
    const auto word = std::bit_cast<std::uint64_t>(value);
    const auto exponent = (word >> 52U) & 0x7ffU;
    if ((word >> 63U) != 0 || exponent < 1021U || exponent > 1023U)
      throw std::runtime_error(
          "Golden neck value outside exact bounded dyadic fixture");
    const auto mantissa =
        (word & ((std::uint64_t{1} << 52U) - 1U)) | (std::uint64_t{1} << 52U);
    return static_cast<std::int64_t>(mantissa << (exponent - 1021U));
  };
  check(bits(p.hip_metres.y, 0x1.0c317acc4ef89p+0) &&
            bits(trunk.center_metres.y, 0x1.65aa4fca42aedp+0) &&
            bits(trunk.semi_axes_metres.y, 0x1.13f7ced916873p-2),
        "Frozen neck stored coordinates match independent golden data");
  const auto exact_slack =
      dyadic_units(0x1.3ced916872b02p-1) - dyadic_units(trunk.center_metres.y) -
      dyadic_units(trunk.semi_axes_metres.y) + dyadic_units(p.hip_metres.y);
  check(exact_slack == 1, "Actual stored-value neck slack is exactly 2^-54, "
                          "not rounded sum equality");
}
auto negative_tests() -> void {
  struct Frozen {
    RigidVector3 hip;
    double pelvis, torso, flex;
  };
  constexpr std::array frozen{
      Frozen{{-.1, .7959731743474827, -1.7}, -10, 10, 100},
      Frozen{{-.1, .7959731743474827, -1.7}, -10, 20, 100},
      Frozen{{-.05, .8028741248020458, -1.7}, -10, 10, 100},
      Frozen{{-.05, .8028741248020458, -1.7}, -10, 20, 100},
      Frozen{{-.05, .7958115540189412, -1.7}, -5, 15, 95}};
  for (const auto& f : frozen) {
    BoardingBodyPose p;
    p.hip_metres = f.hip;
    p.yaw_degrees = 90;
    p.pelvis_lean_degrees = f.pelvis;
    p.torso_relative_lean_degrees = f.torso;
    for (auto& s : p.sides) {
      s.hip_flex_degrees = f.flex;
      s.knee_flex_degrees = 90;
      s.elbow_flex_degrees = 145;
    }
    const auto body = evaluated(p);
    common_checks(p, body);
    region_shapes(p, body);
    check(
        !body.self_checkpoint_passed && body.strict_conflict,
        "Every frozen 145-degree control refuses without pose/region changes");
    for (const auto id : {7U, 13U}) {
      const auto& actual = pair(body, 1, id);
      check(actual.outcome ==
                    BoardingSelfPairOutcome::strict_nonadjacent_interior &&
                actual.strict_witness_metres.has_value(),
            "Both frozen forearm/trunk pairs remain strict nonadjacent "
            "negatives");
      const auto point = body.canonical.parts[id].mass_point_metres;
      check(strictly_inside(body.self_parts[id].solid, point) &&
                strictly_inside(body.self_parts[1].solid, point),
            "Actual canonical forearm midpoint is independently strict common "
            "interior");
    }
  }
  BoardingBodyPose p;
  const auto body = evaluated(p);
  common_checks(p, body);
  region_shapes(p, body);
  check(!body.self_checkpoint_passed && body.strict_conflict,
        "Down-arm connected distal control refuses");
  for (const auto id : {6U, 12U}) {
    const auto& actual = pair(body, 1, id);
    check(actual.outcome ==
                  BoardingSelfPairOutcome::strict_unowned_connected_interior &&
              actual.strict_witness_metres.has_value(),
          "Both down-arm intersections fail finite shoulder ownership");
    const auto point =
        std::get<BoardingBodyCapsule>(body.self_parts[id].solid).end_metres;
    const auto index = region_index(1, id);
    check(index.has_value() &&
              strictly_inside(body.self_parts[1].solid, point) &&
              strictly_inside(body.self_parts[id].solid, point) &&
              strictly_outside(body.regions[*index].region, point),
          "Actual elbow endpoint is common interior strictly outside finite "
          "region");
  }
}
auto private_membership_tests() -> void {
  using namespace apsis_drift::detail;
  using M = BoardingSelfMembership;
  const BoardingSelfEllipsoid ellipse{{0, 0, 0}, {.5, .25, .125}, {}};
  const BoardingBodyBox box{{0, 0, 0}, {.5, .25, .125}, {}};
  const BoardingBodyCapsule ball{{0, 0, 0}, {0, 0, 0}, .5};
  const BoardingBodyCapsule axis{{0, 0, 0}, {0, 1, 0}, .5};
  for (const auto& solid :
       std::array<BoardingSelfSolid, 4>{ellipse, box, ball, axis}) {
    check(boarding_self_model02_membership(solid, {0, 0, 0}) == M::interior,
          "Actual primitive centre/root is interior");
    check(boarding_self_model02_membership(solid, {.5, 0, 0}) == M::boundary,
          "Supported exact axis equality is boundary, not penetration");
    const auto inward =
        boarding_self_model02_membership(solid, {std::nextafter(.5, 0.), 0, 0});
    check(inward == M::interior || inward == M::unresolved,
          "Adjacent representable intrusion never becomes outside or boundary");
    const auto outward =
        boarding_self_model02_membership(solid, {std::nextafter(.5, 1.), 0, 0});
    check(outward == M::outside || outward == M::unresolved,
          "Adjacent outward point never becomes interior");
    check(boarding_self_model02_membership(
              solid, {std::numeric_limits<double>::infinity(), 0, 0}) ==
              M::invalid_geometry,
          "Nonfinite private point refuses");
  }
  const BoardingSelfEllipsoid round{{0, 0, 0}, {.625, .625, .625}, {}};
  check(boarding_self_model02_membership(round, {.375, .5, 0}) == M::boundary,
        "Exactly binary Pythagorean ellipsoid contact is not penetration");
  const auto pythag_in = boarding_self_model02_membership(
      round, {std::nextafter(.375, 0.), .5, 0});
  check(pythag_in == M::interior || pythag_in == M::unresolved,
        "Oblique inward squared-rounding control cannot be clearance");
  const auto pythag_out = boarding_self_model02_membership(
      round, {std::nextafter(.375, 1.), .5, 0});
  check(pythag_out == M::outside || pythag_out == M::unresolved,
        "Oblique outward squared-rounding control cannot be penetration");
  for (const auto factor : {std::nextafter(1., 2.), std::nextafter(1., 0.)}) {
    auto frame = BoardingBodyFrame{};
    frame.columns[1].y = factor;
    const BoardingSelfSolid scaled_box =
        BoardingBodyBox{{0, 0, 0}, {.5, .5, .5}, frame};
    const BoardingSelfSolid scaled_ellipse =
        BoardingSelfEllipsoid{{0, 0, 0}, {.5, .5, .5}, frame};
    for (const auto& solid : {scaled_box, scaled_ellipse}) {
      const auto actual = boarding_self_model02_membership(solid, {0, .5, 0});
      check(factor > 1 ? (actual == M::interior || actual == M::unresolved)
                       : (actual == M::outside || actual == M::unresolved),
            "Actual affine inverse semantics cannot silently use transpose");
    }
    const BoardingSelfEllipsoidCap cap{
        BoardingSelfEllipsoid{{0, 0, 0}, {1, 1, 1}, frame},
        {0, 0, 0},
        frame,
        .5};
    const auto exact =
        boarding_self_model02_region_membership(cap, {0, factor * .5, 0});
    check(exact == M::boundary || exact == M::unresolved,
          "Near-identity inverse cap boundary remains exact or honestly "
          "unresolved");
  }
  for (const auto bad : {0., -1., std::numeric_limits<double>::infinity()}) {
    auto malformed = ellipse;
    malformed.semi_axes_metres.x = bad;
    check(boarding_self_model02_membership(malformed, {0, 0, 0}) ==
              M::invalid_geometry,
          "Malformed self dimension refuses");
  }
  auto malformed = ellipse;
  malformed.frame.columns[1] = malformed.frame.columns[0];
  check(boarding_self_model02_membership(malformed, {0, 0, 0}) ==
            M::invalid_geometry,
        "Singular actual columns refuse");
  malformed = ellipse;
  malformed.frame.columns[0].x = 1.01;
  const auto unconditioned =
      boarding_self_model02_membership(malformed, {0, 0, 0});
  check(unconditioned == M::invalid_geometry || unconditioned == M::unresolved,
        "Unsupported frame conditioning refuses rather than snapping");
}
auto private_region_tests() -> void {
  using namespace apsis_drift::detail;
  using M = BoardingSelfMembership;
  const BoardingSelfSphere sphere{{0, 0, 0}, .5};
  const BoardingBodyCapsule prefix{{0, 0, 0}, {0, 1, 0}, .125};
  const BoardingSelfEllipsoidCap cap{
      BoardingSelfEllipsoid{{0, 0, 0}, {1, 1, 1}, {}}, {0, 0, 0}, {}, .25};
  check(boarding_self_model02_region_membership(sphere, {.5, 0, 0}) ==
            M::boundary,
        "Finite sphere region retains closed boundary");
  check(boarding_self_model02_region_membership(prefix, {.125, .5, 0}) ==
            M::boundary,
        "Finite prefix side is a closed boundary");
  check(boarding_self_model02_region_membership(prefix, {0, 1.125, 0}) ==
            M::boundary,
        "Finite prefix distal cap is a closed boundary");
  check(boarding_self_model02_region_membership(prefix, {0, 1.25, 0}) ==
            M::outside,
        "Finite region cannot become infinite ray");
  check(boarding_self_model02_region_membership(cap, {0, .25, 0}) ==
            M::boundary,
        "Cap axial plane is closed independently of ellipsoid interior");
  check(boarding_self_model02_region_membership(cap, {1, 0, 0}) == M::boundary,
        "Cap keeps actual ellipsoid surface below the plane");
  const auto inward = boarding_self_model02_region_membership(
      cap, {0, std::nextafter(.25, 0.), 0});
  check(inward == M::interior || inward == M::unresolved,
        "Cap inward adjacent value never becomes outside");
  const auto outward = boarding_self_model02_region_membership(
      cap, {0, std::nextafter(.25, 1.), 0});
  check(outward == M::outside || outward == M::unresolved,
        "Cap outward adjacent value cannot be owned");
  auto invalid_cap = cap;
  invalid_cap.axial_frame.columns[2] = {0, 0, 0};
  check(boarding_self_model02_region_membership(invalid_cap, {0, 0, 0}) ==
            M::invalid_geometry,
        "Malformed axial frame refuses");
  auto invalid_sphere = sphere;
  invalid_sphere.radius_metres = 0;
  check(boarding_self_model02_region_membership(invalid_sphere, {0, 0, 0}) ==
            M::invalid_geometry,
        "Zero radius finite region refuses");
}
auto private_support_and_sign_tests() -> void {
  using namespace apsis_drift::detail;
  const std::array<BoardingSelfSolid, 3> solids{
      BoardingSelfEllipsoid{{.125, -.25, .0625}, {.5, .25, .125}, {}},
      BoardingBodyBox{{.125, -.25, .0625}, {.5, .25, .125}, {}},
      BoardingBodyCapsule{{.125, -.25, .0625}, {.125, .25, .0625}, .125}};
  for (const auto& solid : solids)
    for (const auto& direction :
         std::array{RigidVector3{1, 0, 0}, RigidVector3{.375, .5, .125},
                    RigidVector3{-1, -2, 3}}) {
      const auto bound = boarding_self_model02_support(solid, direction);
      const auto expected = support(solid, wide(direction));
      check(bound.supported && std::isfinite(bound.lower) &&
                std::isfinite(bound.upper) && bound.lower <= bound.upper &&
                static_cast<long double>(bound.lower) <= expected &&
                expected <= static_cast<long double>(bound.upper),
            "Support intervals contain independent actual full-solid support");
    }
  auto scaled = BoardingBodyBox{{0, 0, 0}, {.5, .5, .5}, {}};
  scaled.frame.columns[1].y = std::nextafter(1., 2.);
  const auto bound = boarding_self_model02_support(scaled, {0, 1, 0});
  const auto expected = support(BoardingSelfSolid{scaled}, {0, 1, 0});
  check(bound.supported && bound.lower <= expected && expected <= bound.upper,
        "Affine support preserves actual unnormalized columns");
  const RigidVector3 tiny_oblique{std::numeric_limits<double>::denorm_min(),
                                  std::numeric_limits<double>::denorm_min(), 0};
  const auto underflow = boarding_self_model02_support(solids[0], tiny_oblique);
  const auto tiny_expected = support(solids[0], wide(tiny_oblique));
  check(!underflow.supported ||
            (std::isfinite(underflow.lower) && std::isfinite(underflow.upper) &&
             underflow.lower <= underflow.upper &&
             static_cast<long double>(underflow.lower) <= tiny_expected &&
             tiny_expected <= static_cast<long double>(underflow.upper)),
        "Tiny oblique support refuses or encloses actual value; never fake "
        "exact zero");
  check(!boarding_self_model02_support(
             solids[0], {std::numeric_limits<double>::quiet_NaN(), 0, 0})
             .supported,
        "Nonfinite support direction refuses");
  const std::array exact_neck{0x1.3ced916872b02p-1, -0x1.65aa4fca42aedp+0,
                              -0x1.13f7ced916873p-2, 0x1.0c317acc4ef89p+0};
  check(boarding_self_model02_linear_sign(exact_neck) ==
            BoardingSelfSign::positive,
        "Four stored-value terms retain exact neck 2^-54 positive sign");
  const std::array zero{1., -1., .5, -.5};
  check(boarding_self_model02_linear_sign(zero) == BoardingSelfSign::zero,
        "Compensated exact equality remains zero");
  const std::array inward{1., -1., -std::numeric_limits<double>::epsilon()};
  check(boarding_self_model02_linear_sign(inward) == BoardingSelfSign::negative,
        "Compensated negative residual is not rounded equality");
  const std::array nonfinite{std::numeric_limits<double>::infinity()};
  check(boarding_self_model02_linear_sign(nonfinite) ==
            BoardingSelfSign::unsupported,
        "Nonfinite compensated sum refuses");
  const std::array<double, 257> excessive{};
  check(boarding_self_model02_linear_sign(excessive) ==
            BoardingSelfSign::unsupported,
        "Fixed private expansion input capacity is enforced");
}
auto private_prefix_tests() -> void {
  using namespace apsis_drift::detail;
  const auto prefix =
      boarding_self_model02_prefix({0, 0, 0}, {3, 4, 0}, .5, .125);
  check(prefix.has_value(),
        "Conditioned finite prefix accepts exact bounded recipe");
  if (prefix) {
    check(bits(prefix->start_metres, {0, 0, 0}) &&
              bits(prefix->end_metres,
                   {0x1.3333333333334p-2, 0x1.999999999999ap-2, 0}) &&
              bits(prefix->radius_metres, .125),
          "Prefix uses one reciprocal multiplication, not componentwise "
          "division");
    check(!bits(prefix->end_metres.x, 0x1.3333333333333p-2),
          "Direct division would differ by one representable endpoint");
  }
  check(!boarding_self_model02_prefix({0, 0, 0}, {0, 0, 0}, .5, .125),
        "Zero-axis prefix refuses");
  check(!boarding_self_model02_prefix({0, 0, 0}, {1, 0, 0}, 0, .125),
        "Zero-length prefix refuses");
  check(!boarding_self_model02_prefix({0, 0, 0}, {1, 0, 0}, .5, 0),
        "Zero-radius prefix refuses");
  const auto nonfinite = boarding_self_model02_prefix(
      {std::numeric_limits<double>::infinity(), 0, 0}, {1, 0, 0}, .5, .125);
  check(!nonfinite && nonfinite.error() == BoardingBodyError::non_finite_input,
        "Nonfinite prefix input retains explicit error");
  const auto beyond =
      boarding_self_model02_prefix({15.9, 0, 0}, {16, 0, 0}, 4, .125);
  check(!beyond && beyond.error() == BoardingBodyError::numerical_failure,
        "Prefix end outside numeric workspace refuses without clamp");
}
auto private_pair_tests() -> void {
  using namespace apsis_drift::detail;
  const BoardingSelfPart trunk{
      BoardingBodyPartId::trunk,
      BoardingSelfEllipsoid{{0, 0, 0}, {.5, .5, .5}, {}}};
  BoardingSelfPart pelvis{
      BoardingBodyPartId::pelvis,
      BoardingBodyBox{{0, -.125, 0}, {.0625, .25, .0625}, {}}};
  const BoardingSelfConnectedRegion region{
      BoardingBodyPartId::pelvis, BoardingBodyPartId::trunk,
      BoardingSelfJunction::waist,
      BoardingSelfEllipsoidCap{
          std::get<BoardingSelfEllipsoid>(trunk.solid), {0, 0, 0}, {}, .125}};
  const auto boundary = boarding_self_model02_pair(pelvis, trunk, region);
  check(boundary.outcome == BoardingSelfPairOutcome::certified_whole_owned ||
            boundary.outcome == BoardingSelfPairOutcome::unresolved,
        "Displaced finite partner at exact closed cap can be owned or honestly "
        "unresolved");
  std::get<BoardingBodyBox>(pelvis.solid).center_metres.y =
      std::nextafter(-.125, 0.);
  const auto adjacent = boarding_self_model02_pair(pelvis, trunk, region);
  check(adjacent.outcome != BoardingSelfPairOutcome::certified_whole_owned &&
            adjacent.outcome != BoardingSelfPairOutcome::certified_no_interior,
        "Displaced partner with real adjacent cap intrusion cannot be owned");
  std::get<BoardingBodyBox>(pelvis.solid).center_metres.y = -.0625;
  const auto crossed = boarding_self_model02_pair(pelvis, trunk, region);
  check(crossed.outcome ==
                BoardingSelfPairOutcome::strict_unowned_connected_interior ||
            crossed.outcome == BoardingSelfPairOutcome::unresolved,
        "Partial connected intersection outside finite cap refuses");
  const RigidVector3 outside{0, .15625, 0};
  check(strictly_inside(pelvis.solid, outside) &&
            strictly_inside(trunk.solid, outside) &&
            strictly_outside(region.region, outside),
        "Independent actual common-interior witness disproves whole cap "
        "ownership");
  auto wrong = region;
  wrong.first = BoardingBodyPartId::port_thigh;
  check(boarding_self_model02_pair(pelvis, trunk, wrong).outcome ==
            BoardingSelfPairOutcome::unresolved,
        "Unrelated finite-region pair cannot grant ownership");
  wrong = region;
  wrong.junction = BoardingSelfJunction::hip;
  check(boarding_self_model02_pair(pelvis, trunk, wrong).outcome ==
            BoardingSelfPairOutcome::unresolved,
        "Malformed junction metadata cannot grant cap ownership");
  check(boarding_self_model02_pair(pelvis, pelvis, std::nullopt).outcome ==
            BoardingSelfPairOutcome::unresolved,
        "Duplicate part identity refuses private pair authority");
  // Actual geometric family is applicable; a residual product-domain refusal
  // in its exact cofactor certificate must not be mislabeled shape mismatch.
  auto tiny_frame = BoardingBodyFrame{};
  tiny_frame.columns[0].z = 0x1p-500;
  tiny_frame.columns[2].x = -0x1p-500;
  const BoardingSelfEllipsoid tiny_ellipse{{0, 0, 0}, {.5, .5, .5}, tiny_frame};
  const BoardingSelfPart tiny_trunk{BoardingBodyPartId::trunk, tiny_ellipse};
  const BoardingSelfPart tiny_pelvis{
      BoardingBodyPartId::pelvis,
      BoardingBodyBox{{0, -.125, 0}, {.0625, .25, .0625}, {}}};
  const BoardingSelfConnectedRegion tiny_region{
      BoardingBodyPartId::pelvis, BoardingBodyPartId::trunk,
      BoardingSelfJunction::waist,
      BoardingSelfEllipsoidCap{tiny_ellipse, {0, 0, 0}, tiny_frame, .125}};
  const auto arithmetic =
      boarding_self_model02_pair(tiny_pelvis, tiny_trunk, tiny_region);
  check(arithmetic.outcome == BoardingSelfPairOutcome::unresolved &&
            arithmetic.reason == BoardingSelfUnresolved::arithmetic_unresolved,
        "Applicable cofactor product-domain failure has explicit arithmetic "
        "refusal reason");
  const BoardingSelfPart first{BoardingBodyPartId::port_thigh,
                               BoardingBodyCapsule{{0, 0, 0}, {0, 0, 0}, .125}};
  for (const auto x : {std::nextafter(.25, 0.), .25, std::nextafter(.25, 1.)}) {
    const BoardingSelfPart second{
        BoardingBodyPartId::starboard_thigh,
        BoardingBodyCapsule{{x, 0, 0}, {x, 0, 0}, .125}};
    const auto result = boarding_self_model02_pair(first, second, std::nullopt);
    if (x < .25)
      check(result.outcome ==
                    BoardingSelfPairOutcome::strict_nonadjacent_interior ||
                result.outcome == BoardingSelfPairOutcome::unresolved,
            "One-ULP sphere interior cannot be no-interior certificate");
    else
      check(result.outcome == BoardingSelfPairOutcome::certified_no_interior ||
                result.outcome == BoardingSelfPairOutcome::unresolved,
            "Exact/outward contact cannot produce strict false witness");
    if (result.strict_witness_metres)
      check(strictly_inside(first.solid, *result.strict_witness_metres) &&
                strictly_inside(second.solid, *result.strict_witness_metres),
            "Private adjacent-value witness has actual common interior");
  }
}
} // namespace
int main() {
  try {
    if (std::numeric_limits<long double>::digits < 64)
      throw std::runtime_error(
          "Independent test-only geometric oracle requires at least 64 "
          "mantissa bits; not a production wider-type dependency");
    constants_and_invariants();
    validation_tests();
    positive_tests();
    negative_tests();
    private_membership_tests();
    private_region_tests();
    private_support_and_sign_tests();
    private_prefix_tests();
    private_pair_tests();
  } catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << '\n';
    return 1;
  }
  std::cout << checks << " boarding self Model02 checks, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
