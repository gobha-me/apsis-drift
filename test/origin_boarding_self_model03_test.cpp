#include "apsis_drift/origin_boarding_self_model03.hpp"
#include "origin_boarding_self_model03_internal.hpp"
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
auto constants_and_invariants() -> void {
  check(kBoardingSelfModel03Version == 3 &&
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
  check(BoardingSelfDiagnostic03::recipe_complete &&
            !BoardingSelfDiagnostic03::arbitrary_pose_proof_complete,
        "Recipe completeness separate from arbitrary-pose proof coverage");
  check(!BoardingSelfDiagnostic03::actor_qualified &&
            !BoardingSelfDiagnostic03::world_qualified &&
            !BoardingSelfDiagnostic03::support_qualified &&
            !BoardingSelfDiagnostic03::route_qualified,
        "Self checkpoint grants no actor/world/support/route authority");
}
auto validation_tests() -> void {
  auto refuses = [](const BoardingBodyPose& p, BoardingBodyError expected) {
    const auto got = evaluate_origin_boarding_self_model03(p);
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
auto bits(const BoardingSelfEllipsoid& a, const BoardingSelfEllipsoid& b)
    -> bool {
  return bits(a.center_metres, b.center_metres) &&
         bits(a.semi_axes_metres, b.semi_axes_metres) && bits(a.frame, b.frame);
}
auto bits(const BoardingSelfSphere& a, const BoardingSelfSphere& b) -> bool {
  return bits(a.center_metres, b.center_metres) &&
         bits(a.radius_metres, b.radius_metres);
}
auto bits(const BoardingSelfEllipsoidCap& a, const BoardingSelfEllipsoidCap& b)
    -> bool {
  return bits(a.ellipsoid, b.ellipsoid) &&
         bits(a.axial_origin_metres, b.axial_origin_metres) &&
         bits(a.axial_frame, b.axial_frame) &&
         bits(a.axial_limit_metres, b.axial_limit_metres);
}
auto bits(const BoardingSelfSolid& a, const BoardingSelfSolid& b) -> bool {
  return std::visit(
      [&](const auto& x) {
        using T = std::decay_t<decltype(x)>;
        const auto* y = std::get_if<T>(&b);
        return y && bits(x, *y);
      },
      a);
}
auto old_identity(const BoardingSelfDiagnostic& a,
                  const BoardingSelfDiagnostic& b) -> void {
  canonical_identity(a.canonical, b.canonical);
  check(a.self_model_version == b.self_model_version &&
            a.self_checkpoint_passed == b.self_checkpoint_passed &&
            a.strict_conflict == b.strict_conflict &&
            a.unresolved_pair == b.unresolved_pair,
        "Original Model02 complete summary stays identical before/after "
        "Recipe03");
  for (std::size_t i = 0; i < a.self_parts.size(); ++i)
    check(a.self_parts[i].id == b.self_parts[i].id &&
              bits(a.self_parts[i].solid, b.self_parts[i].solid),
          "Original Model02 every self solid stays identical");
  for (std::size_t i = 0; i < a.regions.size(); ++i) {
    const auto& x = a.regions[i];
    const auto& y = b.regions[i];
    check(x.first == y.first && x.second == y.second &&
              x.junction == y.junction,
          "Original Model02 connected identities unchanged");
    std::visit(
        [&](const auto& s) {
          using T = std::decay_t<decltype(s)>;
          const auto* t = std::get_if<T>(&y.region);
          check(
              t && bits(s, *t),
              "Original Model02 all cap/sphere/prefix geometry bits unchanged");
        },
        x.region);
  }
  for (std::size_t i = 0; i < a.pairs.size(); ++i) {
    const auto& x = a.pairs[i];
    const auto& y = b.pairs[i];
    check(x.first == y.first && x.second == y.second &&
              x.connected_region_index == y.connected_region_index &&
              x.outcome == y.outcome && x.certificate == y.certificate &&
              x.reason == y.reason &&
              x.strict_witness_metres.has_value() ==
                  y.strict_witness_metres.has_value(),
          "Original Model02 all105 outcomes/certificates/reasons unchanged");
    if (x.strict_witness_metres && y.strict_witness_metres)
      check(bits(*x.strict_witness_metres, *y.strict_witness_metres),
            "Original Model02 every strict witness unchanged");
  }
}
auto strictly_outside(const BoardingSelfRegion03& region, RigidVector3 point)
    -> bool {
  return std::visit(
      [&](const auto& s) {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, BoardingSelfSphere>) {
          const auto d = sub(wide(point), wide(s.center_metres));
          const long double r = s.radius_metres;
          return dot(d, d) > r * r;
        } else if constexpr (std::is_same_v<T, BoardingSelfAxialSlab03>) {
          const auto d = sub(wide(s.toward_metres), wide(s.root_metres)),
                     p = sub(wide(point), wide(s.root_metres));
          const long double r = s.original_capsule.radius_metres;
          return capsule_distance_squared(s.original_capsule, point) > r * r ||
                 dot(d, p) > s.axial_limit_metres * norm(d);
        } else {
          const auto local = inverse_coordinate(
              s.ellipsoid.frame,
              sub(wide(point), wide(s.ellipsoid.center_metres)));
          const auto hx = s.ellipsoid.semi_axes_metres.x,
                     hy = s.ellipsoid.semi_axes_metres.y,
                     hz = s.ellipsoid.semi_axes_metres.z;
          return local.x * local.x / (hx * hx) + local.y * local.y / (hy * hy) +
                         local.z * local.z / (hz * hz) >
                     1 ||
                 inverse_coordinate(
                     s.axial_frame,
                     sub(wide(point), wide(s.axial_origin_metres)))
                         .y > s.axial_limit_metres;
        }
      },
      region);
}
auto independent_box_gap(const BoardingSelfAxialSlab03& r,
                         const BoardingBodyBox& box) -> long double {
  const auto d = sub(wide(r.toward_metres), wide(r.root_metres));
  return r.axial_limit_metres * norm(d) - support(BoardingSelfSolid{box}, d) +
         dot(d, wide(r.root_metres));
}
auto region_geometry(const BoardingBodyPose& pose,
                     const BoardingSelfDiagnostic03& body) -> void {
  const auto& trunk = std::get<BoardingSelfEllipsoid>(body.self_parts[1].solid);
  for (const auto& r : body.regions) {
    const auto a = part_index(r.first), b = part_index(r.second);
    const auto side = (a >= 9 || b >= 9) ? 1U : 0U;
    const auto& joint = body.canonical.joints[side];
    if (r.junction == BoardingSelfJunction::waist ||
        r.junction == BoardingSelfJunction::neck) {
      const auto* cap = std::get_if<BoardingSelfEllipsoidCap>(&r.region);
      check(cap != nullptr, "Recipe03 waist/neck are actual affine caps");
      if (!cap) continue;
      const bool neck = r.junction == BoardingSelfJunction::neck;
      const auto& actual =
          std::get<BoardingSelfEllipsoid>(body.self_parts[neck ? 2 : 1].solid);
      check(bits(cap->ellipsoid, actual) && bits(cap->axial_frame, trunk.frame),
            "Named cap ellipsoid and actual trunk inverse frame retained");
      check(bits(cap->axial_origin_metres,
                 neck ? trunk.center_metres : pose.hip_metres) &&
                bits(cap->axial_limit_metres,
                     neck ? trunk.semi_axes_metres.y
                          : kBoardingSelfWaistLimitMetres),
            "Recipe03 neck uses actual trunk center/halfY while waist is "
            "unchanged");
    } else if (r.junction == BoardingSelfJunction::hip ||
               r.junction == BoardingSelfJunction::ankle ||
               r.junction == BoardingSelfJunction::wrist) {
      const auto* slab = std::get_if<BoardingSelfAxialSlab03>(&r.region);
      check(slab != nullptr,
            "Six finite regions are original capsule axial slabs");
      if (!slab) continue;
      const auto id = r.junction == BoardingSelfJunction::hip ? b : a;
      const auto& original =
          std::get<BoardingBodyCapsule>(body.self_parts[id].solid);
      const auto root =
          r.junction == BoardingSelfJunction::hip     ? joint.hip_metres
          : r.junction == BoardingSelfJunction::ankle ? joint.ankle_metres
                                                      : joint.wrist_metres;
      const auto toward =
          r.junction == BoardingSelfJunction::hip     ? joint.knee_metres
          : r.junction == BoardingSelfJunction::ankle ? joint.knee_metres
                                                      : joint.elbow_metres;
      const auto limit = r.junction == BoardingSelfJunction::hip
                             ? kBoardingSelfHipLengthMetres
                         : r.junction == BoardingSelfJunction::ankle
                             ? kBoardingSelfAnkleLengthMetres
                             : kBoardingSelfWristLengthMetres;
      check(bits(slab->original_capsule, original) &&
                bits(slab->root_metres, root) &&
                bits(slab->toward_metres, toward) &&
                bits(slab->axial_limit_metres, limit),
            "Every slab preserves full original capsule/actual named "
            "orientation/frozen finite length");
      const auto length = norm(sub(wide(toward), wide(root)));
      check(original.radius_metres <= limit && limit < length,
            "Finite slab retains root ball and excludes distal axis");
    } else {
      const auto* sphere = std::get_if<BoardingSelfSphere>(&r.region);
      check(sphere != nullptr, "Remaining connected regions retain spheres");
      if (!sphere) continue;
      const auto center =
          r.junction == BoardingSelfJunction::knee    ? joint.knee_metres
          : r.junction == BoardingSelfJunction::elbow ? joint.elbow_metres
                                                      : joint.shoulder_metres;
      const auto radius = r.junction == BoardingSelfJunction::knee
                              ? kBoardingSelfKneeRadiusMetres
                          : r.junction == BoardingSelfJunction::elbow
                              ? kBoardingSelfElbowRadiusMetres
                              : kBoardingSelfShoulderRadiusMetres;
      check(bits(sphere->center_metres, center) &&
                bits(sphere->radius_metres, radius),
            "Other joint sphere centers/radii retain exact policy bits");
    }
  }
}
auto pair(const BoardingSelfDiagnostic03& body, unsigned a, unsigned b)
    -> const BoardingSelfPairDiagnostic03& {
  for (const auto& p : body.pairs)
    if (part_index(p.first) == a && part_index(p.second) == b) return p;
  throw std::runtime_error("Missing Recipe03 pair");
}
auto independent_shoulder_bound(const BoardingBodyCapsule& arm,
                                const BoardingSelfEllipsoid& trunk,
                                const BoardingSelfSphere& region) -> bool {
  if (!bits(arm.start_metres, region.center_metres) ||
      region.radius_metres < arm.radius_metres)
    return false;
  const auto d = sub(wide(arm.end_metres), wide(arm.start_metres));
  const auto length = norm(d);
  if (length == 0) return false;
  // Independently computed long-double anatomical directions. These certify
  // a full geometric enclosure; they are not samples of the curved solids.
  const auto candidate =
      cross(wide(trunk.frame.columns[0]), wide(trunk.frame.columns[1]));
  for (const auto sign : {1.L, -1.L}) {
    const auto n = scale(candidate, sign);
    const auto v = dot(n, d) / length;
    if (v >= 0) continue;
    const auto perpendicular = norm(cross(n, d)) / length;
    const long double R = region.radius_metres, r = arm.radius_metres;
    const auto threshold = std::sqrt(R * R - r * r);
    const auto cylinder =
        dot(n, wide(arm.start_metres)) + threshold * v + r * perpendicular;
    const auto distal = dot(n, wide(arm.end_metres)) + r * norm(n);
    const auto minimum = -support(BoardingSelfSolid{trunk}, scale(n, -1));
    if (cylinder < minimum && distal < minimum) return true;
  }
  return false;
}
auto independent_connected(const BoardingSelfDiagnostic03& body,
                           const BoardingSelfConnectedRegion03& r) -> bool {
  const auto a = part_index(r.first), b = part_index(r.second);
  if (const auto* slab = std::get_if<BoardingSelfAxialSlab03>(&r.region)) {
    const auto box_id = r.junction == BoardingSelfJunction::hip ? a : b;
    const auto* box =
        std::get_if<BoardingBodyBox>(&body.self_parts[box_id].solid);
    return box && independent_box_gap(*slab, *box) >= 0;
  }
  if (const auto* cap = std::get_if<BoardingSelfEllipsoidCap>(&r.region)) {
    if (r.junction == BoardingSelfJunction::neck) {
      const auto& trunk =
          std::get<BoardingSelfEllipsoid>(body.self_parts[1].solid);
      return bits(cap->axial_origin_metres, trunk.center_metres) &&
             bits(cap->axial_frame, trunk.frame) &&
             bits(cap->axial_limit_metres, trunk.semi_axes_metres.y);
    }
    const auto& box = std::get<BoardingBodyBox>(body.self_parts[0].solid);
    const auto x = wide(cap->axial_frame.columns[0]),
               z = wide(cap->axial_frame.columns[2]);
    const auto n = cross(z, x);
    const auto denominator = dot(n, wide(cap->axial_frame.columns[1]));
    return denominator > 0 && support(BoardingSelfSolid{box}, n) -
                                      dot(n, wide(cap->axial_origin_metres)) <=
                                  cap->axial_limit_metres * denominator;
  }
  const auto& sphere = std::get<BoardingSelfSphere>(r.region);
  if (r.junction == BoardingSelfJunction::shoulder)
    return independent_shoulder_bound(
        std::get<BoardingBodyCapsule>(body.self_parts[b].solid),
        std::get<BoardingSelfEllipsoid>(body.self_parts[1].solid), sphere);
  const auto& first = std::get<BoardingBodyCapsule>(body.self_parts[a].solid);
  const auto& second = std::get<BoardingBodyCapsule>(body.self_parts[b].solid);
  const auto u = sub(wide(first.start_metres), wide(sphere.center_metres)),
             v = sub(wide(second.end_metres), wide(sphere.center_metres));
  const auto angle = (1 - dot(u, v) / (norm(u) * norm(v))) / 2;
  const long double radius =
      std::max(first.radius_metres, second.radius_metres);
  return angle > 0 && radius * radius / angle <=
                          static_cast<long double>(sphere.radius_metres) *
                              sphere.radius_metres;
}
auto independent_separation(const BoardingSelfSolid& a,
                            const BoardingSelfSolid& b) -> bool {
  // Independent fixed integer direction fan, then full geometric displacement
  // directions. No producer support/membership/candidate helper is called.
  auto separates = [&](Wide n) {
    return norm(n) > 0 && -support(b, scale(n, -1)) >= support(a, n);
  };
  for (int x = -2; x <= 2; ++x)
    for (int y = -2; y <= 2; ++y)
      for (int z = -2; z <= 2; ++z)
        if (separates({static_cast<long double>(x), static_cast<long double>(y),
                       static_cast<long double>(z)}))
          return true;
  auto centre = [](const BoardingSelfSolid& s) {
    return std::visit(
        [](const auto& q) -> Wide {
          using T = std::decay_t<decltype(q)>;
          if constexpr (std::is_same_v<T, BoardingBodyCapsule>)
            return scale(
                Wide{
                    static_cast<long double>(q.start_metres.x) + q.end_metres.x,
                    static_cast<long double>(q.start_metres.y) + q.end_metres.y,
                    static_cast<long double>(q.start_metres.z) +
                        q.end_metres.z},
                .5L);
          else
            return wide(q.center_metres);
        },
        s);
  };
  const auto ca = centre(a), cb = centre(b);
  const auto direction = sub(cb, ca);
  if (separates(direction) || separates(scale(direction, -1))) return true;
  for (const auto* s : {&a, &b}) {
    const bool found = std::visit(
        [&](const auto& q) {
          using T = std::decay_t<decltype(q)>;
          if constexpr (std::is_same_v<T, BoardingBodyCapsule>) {
            for (const auto p : {q.start_metres, q.end_metres})
              for (const auto target : {ca, cb}) {
                const auto n = sub(wide(p), target);
                if (separates(n) || separates(scale(n, -1))) return true;
              }
          } else {
            for (const auto col : q.frame.columns) {
              const auto n = wide(col);
              if (separates(n) || separates(scale(n, -1))) return true;
            }
          }
          return false;
        },
        *s);
    if (found) return true;
  }
  return false;
}
auto common_checks(const BoardingBodyPose& pose,
                   const BoardingSelfDiagnostic03& body) -> void {
  const auto canonical = evaluate_origin_boarding_body(pose);
  if (!canonical) throw std::runtime_error("Canonical control invalid");
  canonical_identity(body.canonical, *canonical);
  check(body.self_model_version == 3, "Output identifies Recipe03 separately");
  for (std::size_t i = 0; i < body.self_parts.size(); ++i) {
    const auto& s = body.self_parts[i];
    const auto& w = canonical->parts[i];
    check(s.id == w.id, "All 15 self IDs retain canonical mapping");
    if (i == 1 || i == 2) {
      const auto* e = std::get_if<BoardingSelfEllipsoid>(&s.solid);
      const auto* b = std::get_if<BoardingBodyBox>(&w.world_reservation);
      check(e && b && bits(e->center_metres, b->center_metres) &&
                bits(e->semi_axes_metres, b->half_size_metres) &&
                bits(e->frame, b->frame),
            "Same-extrema actual affine self ellipsoids preserve every world "
            "bit");
    } else
      std::visit(
          [&](const auto& q) {
            using T = std::decay_t<decltype(q)>;
            const auto* other = std::get_if<T>(&s.solid);
            check(other && bits(q, *other),
                  "Other self solids preserve canonical bits");
          },
          w.world_reservation);
  }
  for (std::size_t i = 0; i < body.regions.size(); ++i) {
    const auto& r = body.regions[i];
    const auto& e = expected_regions[i];
    check(part_index(r.first) == e.first && part_index(r.second) == e.second &&
              r.junction == e.junction,
          "All 14 finite regions retain exact lexicographic bindings");
  }
  bool conflict{}, unresolved{}, all = true;
  std::size_t cursor{};
  for (unsigned a = 0; a < 15; ++a)
    for (unsigned b = a + 1; b < 15; ++b) {
      const auto& p = body.pairs[cursor++];
      check(part_index(p.first) == a && part_index(p.second) == b &&
                p.connected_region_index == region_index(a, b),
            "All 105 pairs retain exact lexicographic/region identity");
      const bool strict =
          p.outcome == BoardingSelfPairOutcome::strict_nonadjacent_interior ||
          p.outcome ==
              BoardingSelfPairOutcome::strict_unowned_connected_interior;
      const bool uncertain = p.outcome == BoardingSelfPairOutcome::unresolved;
      conflict = conflict || strict;
      unresolved = unresolved || uncertain;
      check(p.strict_witness_metres.has_value() == strict,
            "Strict outcomes alone carry actual common-interior witnesses");
      check(uncertain ? p.reason != BoardingSelfUnresolved::none
                      : p.reason == BoardingSelfUnresolved::none,
            "Unsupported/arithmetic reasons remain explicit");
      if (p.strict_witness_metres) {
        check(strictly_inside(body.self_parts[a].solid,
                              *p.strict_witness_metres) &&
                  strictly_inside(body.self_parts[b].solid,
                                  *p.strict_witness_metres),
              "Independent affine/finite-distance oracle verifies every strict "
              "witness");
        if (p.connected_region_index)
          check(
              strictly_outside(body.regions[*p.connected_region_index].region,
                               *p.strict_witness_metres),
              "Connected strict witness lies outside its actual finite region");
      }
      const bool accepted =
          p.outcome == BoardingSelfPairOutcome::certified_no_interior ||
          (p.connected_region_index &&
           p.outcome == BoardingSelfPairOutcome::certified_whole_owned);
      all = all && accepted;
      if (!p.connected_region_index) {
        check(
            p.outcome != BoardingSelfPairOutcome::certified_whole_owned &&
                p.outcome !=
                    BoardingSelfPairOutcome::strict_unowned_connected_interior,
            "Nonadjacent overlap receives no semantic ownership");
        if (p.outcome == BoardingSelfPairOutcome::certified_no_interior)
          check(independent_separation(body.self_parts[a].solid,
                                       body.self_parts[b].solid),
                "Independent full convex support corroborates every "
                "nonadjacent certificate");
      }
      if (p.outcome == BoardingSelfPairOutcome::certified_whole_owned)
        check(
            independent_connected(body,
                                  body.regions[*p.connected_region_index]),
            "Independent complete finite-region bound corroborates ownership");
    }
  check(cursor == 105 && body.self_checkpoint_passed == all &&
            body.strict_conflict == conflict &&
            body.unresolved_pair == unresolved,
        "Complete 105-pair summary matches every outcome");
}
struct PublicControl {
  std::string_view name;
  BoardingBodyPose pose;
  bool original_positive{}, original_negative{}, down_arm{};
};
auto public_controls() -> std::array<PublicControl, 13> {
  std::array<PublicControl, 13> out{};
  BoardingBodyPose standing;
  for (auto& s : standing.sides) {
    s.shoulder_flex_degrees = 45;
    s.elbow_flex_degrees = 135;
  }
  out[0] = {"original-standing", standing, true, false, false};
  struct Frozen {
    RigidVector3 hip;
    double pelvis, relative, flex;
    std::string_view old_name, new_name;
  };
  constexpr std::array frozen{Frozen{{-.1, .7959731743474827, -1.7},
                                     -10,
                                     10,
                                     100,
                                     "original11",
                                     "folded11"},
                              Frozen{{-.1, .7959731743474827, -1.7},
                                     -10,
                                     20,
                                     100,
                                     "original12",
                                     "folded12"},
                              Frozen{{-.05, .8028741248020458, -1.7},
                                     -10,
                                     10,
                                     100,
                                     "original22",
                                     "folded22"},
                              Frozen{{-.05, .8028741248020458, -1.7},
                                     -10,
                                     20,
                                     100,
                                     "original23",
                                     "folded23"},
                              Frozen{{-.05, .7958115540189412, -1.7},
                                     -5,
                                     15,
                                     95,
                                     "original26",
                                     "folded26"}};
  for (std::size_t i = 0; i < frozen.size(); ++i) {
    const auto& f = frozen[i];
    BoardingBodyPose p;
    p.hip_metres = f.hip;
    p.yaw_degrees = 90;
    p.pelvis_lean_degrees = f.pelvis;
    p.torso_relative_lean_degrees = f.relative;
    for (auto& s : p.sides) {
      s.hip_flex_degrees = f.flex;
      s.knee_flex_degrees = 90;
      s.elbow_flex_degrees = 145;
    }
    out[i + 1] = {f.old_name, p, false, true, false};
    for (auto& s : p.sides) {
      s.shoulder_flex_degrees = 45;
      s.elbow_flex_degrees = 135;
    }
    out[i + 8] = {f.new_name, p, false, false, false};
  }
  out[6] = {"original-down-arm", BoardingBodyPose{}, false, true, true};
  standing.yaw_degrees = 90;
  out[7] = {"standing-yaw90", standing, false, false, false};
  return out;
}
auto public_tests() -> void {
  for (const auto& control : public_controls()) {
    const auto before = evaluate_origin_boarding_self_model02(control.pose);
    const auto actual = evaluate_origin_boarding_self_model03(control.pose);
    const auto after = evaluate_origin_boarding_self_model02(control.pose);
    if (!before || !after || !actual)
      throw std::runtime_error(
          "Preregistered public control failed pose validation");
    old_identity(*before, *after);
    canonical_identity(actual->canonical, before->canonical);
    for (std::size_t i = 0; i < 15; ++i)
      check(bits(actual->self_parts[i].solid, before->self_parts[i].solid),
            "Recipe03 preserves all Model02 self-solid bits");
    common_checks(control.pose, *actual);
    region_geometry(control.pose, *actual);
    if (!control.original_negative)
      check(actual->self_checkpoint_passed && !actual->strict_conflict &&
                !actual->unresolved_pair,
            "All seven registered positive checkpoints retain complete "
            "105-pair qualification");
    if (control.original_negative) {
      check(!actual->self_checkpoint_passed && actual->strict_conflict,
            "All original negative controls retain strict refusal");
      for (const auto id :
           control.down_arm ? std::array{6U, 12U} : std::array{7U, 13U}) {
        const auto& p = pair(*actual, 1, id);
        check(
            p.outcome ==
                (control.down_arm
                     ? BoardingSelfPairOutcome::
                           strict_unowned_connected_interior
                     : BoardingSelfPairOutcome::strict_nonadjacent_interior),
            "Original down-arm/forearm strict pair remains unchanged negative");
      }
    }
    std::cout << "CONTROL " << control.name
              << " passed=" << actual->self_checkpoint_passed
              << " strict=" << actual->strict_conflict
              << " unresolved=" << actual->unresolved_pair << '\n';
    if (!actual->self_checkpoint_passed)
      for (const auto& p : actual->pairs)
        if (p.outcome != BoardingSelfPairOutcome::certified_no_interior &&
            p.outcome != BoardingSelfPairOutcome::certified_whole_owned)
          std::cout << "  REFUSED " << part_index(p.first) << '/'
                    << part_index(p.second)
                    << " outcome=" << static_cast<unsigned>(p.outcome)
                    << " reason=" << static_cast<unsigned>(p.reason) << '\n';
  }
}
using namespace apsis_drift::detail;
auto y_slab() -> BoardingSelfAxialSlab03 {
  return {{{0, 2, 0}, {0, 0, 0}, .125}, {0, 0, 0}, {0, 2, 0}, .5};
}
auto oblique_slab() -> BoardingSelfAxialSlab03 {
  return {{{0, 0, 0}, {1.5, 2, 0}, .125}, {0, 0, 0}, {1.5, 2, 0}, .625};
}
auto proof_refuses(const BoardingSelfProof03& proof) -> bool {
  return proof.status != BoardingSelfProofStatus03::proved &&
         proof.certificate == BoardingSelfCertificate03::none;
}
auto private_invalid_tests() -> void {
  for (const auto bad : {std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity()}) {
    for (unsigned field = 0; field < 8; ++field) {
      auto s = y_slab();
      if (field == 0)
        s.root_metres.x = bad;
      else if (field == 1)
        s.toward_metres.y = bad;
      else if (field == 2)
        s.original_capsule.start_metres.z = bad;
      else if (field == 3)
        s.original_capsule.end_metres.x = bad;
      else if (field == 4)
        s.original_capsule.radius_metres = bad;
      else if (field == 5)
        s.axial_limit_metres = bad;
      else if (field == 6)
        s.root_metres.y = bad;
      else
        s.toward_metres.z = bad;
      check(boarding_self_model03_region_membership(BoardingSelfRegion03{s},
                                                    {0, .25, 0}) ==
                BoardingSelfMembership::invalid_geometry,
            "Every nonfinite axial field refuses before membership");
      check(proof_refuses(
                boarding_self_model03_slab_box_support(
                    s, BoardingBodyBox{{0, .25, 0}, {.0625, .0625, .0625}, {}})
                    .proof),
            "Nonfinite axial field cannot grant whole-box ownership");
    }
    check(boarding_self_model03_region_membership(
              BoardingSelfRegion03{y_slab()}, {bad, 0, 0}) ==
              BoardingSelfMembership::invalid_geometry,
          "Nonfinite membership point refuses");
    check(!boarding_self_model03_exact_norm({bad, 0, 0}, {1, 0, 0}),
          "Nonfinite exact-norm root refuses");
    check(!boarding_self_model03_exact_norm({0, 0, 0}, {0, bad, 0}),
          "Nonfinite exact-norm toward refuses");
  }
  for (unsigned control = 0; control < 9; ++control) {
    auto s = y_slab();
    if (control == 0)
      s.axial_limit_metres = 0;
    else if (control == 1)
      s.axial_limit_metres = .0625;
    else if (control == 2)
      s.axial_limit_metres = 2;
    else if (control == 3)
      s.axial_limit_metres = 3;
    else if (control == 4)
      s.original_capsule.radius_metres = 0;
    else if (control == 5)
      s.original_capsule.radius_metres = 5;
    else if (control == 6)
      s.root_metres = {0, .25, 0};
    else if (control == 7)
      s.toward_metres = {0, 3, 0};
    else {
      s.original_capsule.start_metres = {0, 0, 0};
      s.toward_metres = {0, 0, 0};
    }
    check(boarding_self_model03_region_membership(BoardingSelfRegion03{s},
                                                  {0, .25, 0}) ==
              BoardingSelfMembership::invalid_geometry,
          "Malformed finite-cut domain or original orientation refuses");
    check(proof_refuses(
              boarding_self_model03_slab_box_support(
                  s, BoardingBodyBox{{0, .25, 0}, {.0625, .0625, .0625}, {}})
                  .proof),
          "Malformed slab cannot qualify full box");
  }
  auto s = y_slab();
  s.root_metres = {17, 0, 0};
  check(boarding_self_model03_region_membership(BoardingSelfRegion03{s},
                                                {0, .25, 0}) ==
            BoardingSelfMembership::invalid_geometry,
        "Private numeric workspace remains bounded, not crop authority");
  for (unsigned control = 0; control < 5; ++control) {
    BoardingBodyBox box{{0, .25, 0}, {.0625, .0625, .0625}, {}};
    if (control == 0)
      box.center_metres.x = std::numeric_limits<double>::quiet_NaN();
    else if (control == 1)
      box.half_size_metres.y = 0;
    else if (control == 2)
      box.half_size_metres.z = -.1;
    else if (control == 3)
      box.frame.columns[1] = {0, 0, 0};
    else
      box.frame.columns[0] = {1, .01, 0};
    const auto got = boarding_self_model03_slab_box_support(y_slab(), box);
    check(got.proof.status == BoardingSelfProofStatus03::invalid_geometry &&
              !got.interval_supported,
          "Malformed/nonfinite/unsupported-frame box refuses");
  }
}
auto exact_norm_tests() -> void {
  const auto oblique = boarding_self_model03_exact_norm({0, 0, 0}, {1.5, 2, 0});
  check(oblique && bits(*oblique, 2.5),
        "Dyadic Pythagorean actual axis has certified represented norm");
  const auto axis = boarding_self_model03_exact_norm({0, 0, 0}, {-0., 2, +0.});
  check(axis && bits(*axis, 2.),
        "Signed zero components do not alter exact nonzero geometry");
  check(!boarding_self_model03_exact_norm({0, 0, 0}, {0, 0, 0}),
        "Zero original segment cannot qualify exact norm");
  check(!boarding_self_model03_exact_norm({0, 0, 0}, {.1, .2, 0}),
        "Inexact component-square residual disables exact family");
  check(!boarding_self_model03_exact_norm({0, 0, 0}, {1, 0x1p-27, 0}),
        "Exact squares with inexact sequential sum disable exact family");
  check(!boarding_self_model03_exact_norm({0, 0, 0}, {1, 1, 0}),
        "Inexact sqrt-candidate square cannot certify exact norm");
  check(!boarding_self_model03_exact_norm({-1, 0, 0},
                                          {std::nextafter(1., 0.), 0, 0}),
        "Nonzero actual endpoint-subtraction residual disables exact norm");
  for (const auto tiny : {0x1p-500, 0x1p-600}) {
    check(!boarding_self_model03_exact_norm({0, 0, 0}, {1, tiny, 0}),
          "Product guard precedes zero FMA residual; tiny nonzero component "
          "never disappears");
    BoardingSelfAxialSlab03 s{
        {{0, 0, 0}, {1, tiny, 0}, .125}, {0, 0, 0}, {1, tiny, 0}, .5};
    const auto sign = boarding_self_model03_axial_plane_sign(s, {.5, 0, 0});
    check(sign == BoardingSelfSign::positive ||
              sign == BoardingSelfSign::unsupported,
          "True tiny inward axial point cannot become equality/outside from "
          "lost square");
    const auto member = boarding_self_model03_region_membership(
        BoardingSelfRegion03{s}, {.5, 0, 0});
    check(member == BoardingSelfMembership::interior ||
              member == BoardingSelfMembership::unresolved,
          "Unsupported infinitesimal boundary arithmetic refuses honestly");
  }
}
auto axial_membership_tests() -> void {
  const auto s = y_slab();
  using M = BoardingSelfMembership;
  using S = BoardingSelfSign;
  for (const auto y : {std::nextafter(.5, 0.), .5, std::nextafter(.5, 1.)}) {
    const auto m = boarding_self_model03_region_membership(
        BoardingSelfRegion03{s}, {0, y, 0});
    const auto sign = boarding_self_model03_axial_plane_sign(s, {0, y, 0});
    check(m == (y < .5    ? M::interior
                : y == .5 ? M::boundary
                          : M::outside),
          "Finite original-axis cut distinguishes exact and one-ULP neighbors");
    check(sign == (y < .5    ? S::positive
                   : y == .5 ? S::zero
                             : S::negative),
          "Plane sign preserves unnormalized exact contact");
  }
  check(boarding_self_model03_region_membership(BoardingSelfRegion03{s},
                                                {0, -.0625, 0}) == M::interior,
        "Root cap is retained without accidental q>=0 cut");
  check(boarding_self_model03_region_membership(BoardingSelfRegion03{s},
                                                {0, -.125, 0}) == M::boundary,
        "Original root radius contact remains boundary");
  check(boarding_self_model03_region_membership(
            BoardingSelfRegion03{s}, {0, std::nextafter(-.125, -1.), 0}) ==
            M::outside,
        "Original radius penetration rule remains strict behind root");
  check(boarding_self_model03_region_membership(BoardingSelfRegion03{s},
                                                {0, .5625, 0}) == M::outside,
        "Flat front excludes old short-prefix distal hemisphere");
  check(strictly_inside(s.original_capsule, {0, .5625, 0}),
        "Distal-semantic control really remains inside original K");
  check(boarding_self_model03_region_membership(BoardingSelfRegion03{s},
                                                {.125, .25, 0}) == M::boundary,
        "Original radial boundary remains boundary inside plane");
  check(boarding_self_model03_region_membership(
            BoardingSelfRegion03{s}, {std::nextafter(.125, 1.), .25, 0}) ==
            M::outside,
        "Plane inclusion cannot hide outside original capsule");
  const auto oblique = oblique_slab();
  for (const auto x :
       {std::nextafter(.375, 0.), .375, std::nextafter(.375, 1.)}) {
    const auto sign =
        boarding_self_model03_axial_plane_sign(oblique, {x, .5, 0});
    const auto expected = x < .375    ? S::positive
                          : x == .375 ? S::zero
                                      : S::negative;
    check(sign == expected, "Exact dyadic oblique tangent and inward/outward X "
                            "neighbors preserve true sign");
    check(boarding_self_model03_region_membership(BoardingSelfRegion03{oblique},
                                                  {x, .5, 0}) ==
              (x < .375    ? M::interior
               : x == .375 ? M::boundary
                           : M::outside),
          "Oblique exact norm gives full region contact independently of "
          "rounded dot/sqrt");
  }
  // Exact analytic oracle: d=(3/2,2,0), norm=5/2 and L=5/8.
  // At x=3/8,y=1/2, s=25/16=L*norm exactly. The one-ULP
  // change gives signed residual -(3/2)*delta, with no squaring needed.
  auto translated = oblique;
  translated.root_metres = {.125, .25, 0};
  translated.toward_metres = {1.625, 2.25, 0};
  translated.original_capsule = {translated.toward_metres,
                                 translated.root_metres, .125};
  check(boarding_self_model03_axial_plane_sign(translated, {.5, .75, 0}) ==
            S::zero,
        "Actual reversed K orientation and dyadic translated cut retain exact "
        "boundary");
  const auto capacity =
      boarding_self_model02_linear_sign(std::array<double, 257>{});
  check(capacity == S::unsupported,
        "Shared exact scalar collection refuses fixed256 input overflow");
}
auto support_tests() -> void {
  auto box = BoardingBodyBox{{0, .375, 0}, {.0625, .125, .0625}, {}};
  const auto slab = y_slab();
  using P = BoardingSelfProofStatus03;
  for (const auto y :
       {std::nextafter(.375, 0.), .375, std::nextafter(.375, 1.)}) {
    box.center_metres.y = y;
    const auto got = boarding_self_model03_slab_box_support(slab, box);
    const auto expected = independent_box_gap(slab, box);
    check(got.interval_supported &&
              std::isfinite(got.gap_lower_square_metres) &&
              std::isfinite(got.gap_upper_square_metres) &&
              got.gap_lower_square_metres <= expected &&
              expected <= got.gap_upper_square_metres,
          "Outward gap bounds enclose independent full-box support including "
          "residual");
    if (y <= .375)
      check(got.proof.status == P::proved &&
                got.proof.certificate ==
                    BoardingSelfCertificate03::original_axis_box_support,
            "Full-box exact contact/inward support is owned without geometric "
            "epsilon");
    else
      check(proof_refuses(got.proof),
            "One-ULP outward whole-box support cannot be owned");
    check(got.exact_specialization_used,
          "Represented norm exact support family is actually exercised");
  }
  const auto oblique = oblique_slab();
  BoardingBodyFrame frame;
  frame.columns = {RigidVector3{.8, -.6, 0}, RigidVector3{.6, .8, 0},
                   RigidVector3{0, 0, 1}};
  const BoardingBodyBox cancellation{
      {.28125, .375, 0}, {.125, .125, .125}, frame};
  const auto got =
      boarding_self_model03_slab_box_support(oblique, cancellation);
  const auto expected = independent_box_gap(oblique, cancellation);
  check(expected > .07L && got.interval_supported &&
            got.gap_lower_square_metres <= expected &&
            expected <= got.gap_upper_square_metres,
        "Actual cancelling frame projections, not abs-per-product, determine "
        "full support");
  check(got.proof.status == P::proved,
        "Deep positive cancelling projection cannot become artificial "
        "absolute-term refusal");
  auto scaled = cancellation;
  scaled.frame.columns[2].z = std::nextafter(1., 2.);
  const auto affine = boarding_self_model03_slab_box_support(oblique, scaled);
  const auto exact = independent_box_gap(oblique, scaled);
  check(
      affine.interval_supported && affine.gap_lower_square_metres <= exact &&
          exact <= affine.gap_upper_square_metres,
      "Actual affine stored-column perturbation remains in support enclosure");
  auto nonexact = slab;
  nonexact.toward_metres = {.1, 2, 0};
  nonexact.original_capsule.start_metres = nonexact.toward_metres;
  box = {{0, .25, 0}, {.0625, .0625, .0625}, {}};
  const auto fallback = boarding_self_model03_slab_box_support(nonexact, box);
  const auto gap = independent_box_gap(nonexact, box);
  check(fallback.interval_supported &&
            fallback.gap_lower_square_metres <= gap &&
            gap <= fallback.gap_upper_square_metres &&
            fallback.proof.status == P::proved,
        "General outward interval can prove deep support outside exact norm "
        "family");
  check(!fallback.exact_specialization_used,
        "General proof does not falsely claim exact represented norm");
}
auto verifies_private_witness(
    const BoardingSelfPairDiagnostic03& got, const BoardingSelfPart& a,
    const BoardingSelfPart& b,
    const std::optional<BoardingSelfConnectedRegion03>& region) -> void {
  if (!got.strict_witness_metres) return;
  const auto point = *got.strict_witness_metres;
  check(strictly_inside(a.solid, point) && strictly_inside(b.solid, point),
        "Independent membership verifies every private actual common-interior "
        "witness");
  if (region)
    check(strictly_outside(region->region, point),
          "Private connected witness is beyond actual finite ownership");
}
auto pair_refuses(const BoardingSelfPairDiagnostic03& got) -> bool {
  return got.outcome != BoardingSelfPairOutcome::certified_no_interior &&
         got.outcome != BoardingSelfPairOutcome::certified_whole_owned;
}
auto private_pair_tests() -> void {
  const auto slab = y_slab();
  const BoardingSelfPart fore{BoardingBodyPartId::port_forearm,
                              slab.original_capsule};
  const BoardingSelfPart hand{
      BoardingBodyPartId::port_hand,
      BoardingBodyBox{{.25, .375, 0}, {.25, .25, .25}, {}}};
  const BoardingSelfConnectedRegion03 r{fore.id, hand.id,
                                        BoardingSelfJunction::wrist, slab};
  const std::optional<BoardingSelfConnectedRegion03> region{r};
  const RigidVector3 interior{.0625, .5625, 0};
  check(strictly_inside(fore.solid, interior) &&
            strictly_inside(hand.solid, interior) &&
            strictly_outside(r.region, interior),
        "Analytic dyadic interior defeats center/corner-only containment");
  const auto& box = std::get<BoardingBodyBox>(hand.solid);
  check(box.center_metres.y < slab.axial_limit_metres,
        "Adversarial partner center itself lies below cut");
  for (const auto x : {0., .5})
    for (const auto y : {.125, .625})
      for (const auto z : {-.25, .25})
        check(!strictly_inside(slab.original_capsule, {x, y, z}),
              "All eight partner corners outside K do not prove empty "
              "intersection");
  const auto got = boarding_self_model03_pair(fore, hand, region);
  check(pair_refuses(got), "Whole actual intersection outside cut cannot "
                           "receive ownership or separation");
  verifies_private_witness(got, fore, hand, region);
  const auto support_result = boarding_self_model03_slab_box_support(slab, box);
  check(proof_refuses(support_result.proof),
        "Full partner support includes outward extrema despite center/corners "
        "shortcut");
  const BoardingSelfPart disjoint{
      hand.id, BoardingBodyBox{{2, .75, 0}, {.125, .125, .125}, {}}};
  const auto empty = boarding_self_model03_pair(fore, disjoint, region);
  check(empty.outcome == BoardingSelfPairOutcome::certified_no_interior ||
            empty.outcome == BoardingSelfPairOutcome::unresolved,
        "Partner beyond cut may be vacuously separated; failed projection "
        "alone is not collision");
  check(!empty.strict_witness_metres,
        "Disjoint whole pair cannot receive fabricated collision witness");
  for (unsigned control = 0; control < 7; ++control) {
    auto wrong = r;
    if (control == 0)
      wrong.first = BoardingBodyPartId::trunk;
    else if (control == 1)
      wrong.second = BoardingBodyPartId::starboard_hand;
    else if (control == 2)
      wrong.junction = BoardingSelfJunction::hip;
    else if (control == 3)
      wrong.region = BoardingSelfSphere{{0, 0, 0}, 4};
    else {
      auto changed = slab;
      if (control == 4)
        changed.original_capsule.radius_metres = .25;
      else if (control == 5)
        changed.original_capsule.start_metres.y = 3;
      else {
        changed.root_metres = slab.toward_metres;
        changed.toward_metres = slab.root_metres;
      }
      wrong.region = changed;
    }
    const auto bad = boarding_self_model03_pair(fore, hand, wrong);
    check(bad.outcome == BoardingSelfPairOutcome::unresolved &&
              bad.reason == BoardingSelfUnresolved::invalid_geometry,
          "Wrong private pair/junction/variant/original-axis named binding "
          "refuses");
  }
  check(boarding_self_model03_pair(fore, fore, std::nullopt).reason ==
            BoardingSelfUnresolved::invalid_geometry,
        "Duplicate pair identity cannot grant fixture authority");
}
auto private_neck_tests() -> void {
  const BoardingSelfEllipsoid trunk{{0, 0, 0}, {.5, .5, .5}, {}};
  const BoardingSelfEllipsoid helmet{{0, .625, 0}, {.5, .5, .5}, {}};
  const BoardingSelfPart a{BoardingBodyPartId::trunk, trunk},
      b{BoardingBodyPartId::helmet, helmet};
  const BoardingSelfEllipsoidCap cap{helmet, trunk.center_metres, trunk.frame,
                                     trunk.semi_axes_metres.y};
  const BoardingSelfConnectedRegion03 region{a.id, b.id,
                                             BoardingSelfJunction::neck, cap};
  const auto got = boarding_self_model03_pair(a, b, region);
  check(got.outcome == BoardingSelfPairOutcome::certified_whole_owned &&
            got.certificate == BoardingSelfCertificate03::cap_partner_support,
        "Actual trunk-top cap owns complete trunk/helmet overlap at exact "
        "support plane");
  for (const auto y : {std::nextafter(.5, 0.), .5, std::nextafter(.5, 1.)}) {
    const auto m = boarding_self_model03_region_membership(
        BoardingSelfRegion03{cap}, {0, y, 0});
    check(m == (y < .5    ? BoardingSelfMembership::interior
                : y == .5 ? BoardingSelfMembership::boundary
                          : BoardingSelfMembership::outside),
          "Actual-center cap contact and adjacent neck-plane values remain "
          "strict");
  }
  for (unsigned control = 0; control < 7; ++control) {
    auto wrong = region;
    auto changed = cap;
    if (control == 0)
      changed.axial_origin_metres.y = .125;
    else if (control == 1)
      changed.axial_limit_metres = .625;
    else if (control == 2)
      changed.axial_frame.columns[1].y = std::nextafter(1., 2.);
    else if (control == 3)
      changed.ellipsoid.center_metres.y = .75;
    else if (control == 4) {
      wrong.region = BoardingSelfSphere{{0, .625, 0}, 1};
    } else if (control == 5) {
      wrong.junction = BoardingSelfJunction::waist;
    } else
      changed.axial_origin_metres.x = std::numeric_limits<double>::quiet_NaN();
    if (control != 4) wrong.region = changed;
    const auto bad = boarding_self_model03_pair(a, b, wrong);
    check(bad.outcome == BoardingSelfPairOutcome::unresolved &&
              bad.reason == BoardingSelfUnresolved::invalid_geometry,
          "Shifted-higher/wrong-limit/frame/helmet/variant/junction neck "
          "cannot grant ownership");
  }
  auto affine_trunk = trunk, affine_helmet = helmet;
  const auto factor = std::nextafter(1., 2.);
  affine_trunk.frame.columns[1].y = factor;
  affine_helmet.frame = affine_trunk.frame;
  const BoardingSelfEllipsoidCap affine_cap{
      affine_helmet, affine_trunk.center_metres, affine_trunk.frame, .5};
  const auto plane = factor * .5;
  check(boarding_self_model03_region_membership(
            BoardingSelfRegion03{affine_cap}, {0, plane, 0}) ==
            BoardingSelfMembership::boundary,
        "Stored affine scaling uses actual inverse plane, not transpose");
  const auto affine = boarding_self_model03_pair(
      BoardingSelfPart{a.id, affine_trunk},
      BoardingSelfPart{b.id, affine_helmet},
      BoardingSelfConnectedRegion03{a.id, b.id, BoardingSelfJunction::neck,
                                    affine_cap});
  check(affine.outcome == BoardingSelfPairOutcome::certified_whole_owned,
        "Nonideal actual frame shares exact anatomical top support without "
        "geometry snapping");
  for (const auto junction :
       {BoardingSelfJunction::knee, BoardingSelfJunction::elbow}) {
    const bool knee = junction == BoardingSelfJunction::knee;
    const auto first_id = knee ? BoardingBodyPartId::port_thigh
                               : BoardingBodyPartId::port_upper_arm;
    const auto second_id =
        knee ? BoardingBodyPartId::port_shin : BoardingBodyPartId::port_forearm;
    const BoardingSelfPart first{
        first_id, BoardingBodyCapsule{{0, 1, 0}, {0, 0, 0}, .125}},
        second{second_id, BoardingBodyCapsule{{0, 0, 0}, {0, -1, 0}, .125}};
    const BoardingSelfConnectedRegion03 good{
        first_id, second_id, junction, BoardingSelfSphere{{0, 0, 0}, .25}};
    check(boarding_self_model03_pair(first, second, good).outcome ==
              BoardingSelfPairOutcome::certified_whole_owned,
          "Generic private connected sphere at actual shared joint permits "
          "complete bounded overlap");
    auto shifted = good;
    shifted.region = BoardingSelfSphere{{0, .125, 0}, .25};
    check(boarding_self_model03_pair(first, second, shifted).reason ==
              BoardingSelfUnresolved::invalid_geometry,
          "Shifted shared-joint sphere binding refuses before delegation");
    shifted = good;
    shifted.region = cap;
    check(boarding_self_model03_pair(first, second, shifted).reason ==
              BoardingSelfUnresolved::invalid_geometry,
          "Cap variant cannot masquerade as connected sphere");
  }
}
auto quarter_turn(RigidVector3 p) -> RigidVector3 {
  return {p.z, p.y, -p.x};
}
auto shoulder_tests() -> void {
  const BoardingBodyCapsule arm{
      {.20265, 1.62663, 0},
      {.20265, 1.3727928076896532, -.25383719231034685},
      .065};
  const BoardingSelfEllipsoid trunk{{0, 1.39713, 0}, {.26, .2695, .18}, {}};
  const BoardingSelfSphere sphere{arm.start_metres,
                                  kBoardingSelfShoulderRadiusMetres};
  const auto initial = boarding_self_model03_shoulder(arm, trunk, sphere);
  check(independent_shoulder_bound(arm, trunk, sphere),
        "Independent complete cylinder/distal proof verifies original fixture");
  check(initial.status == BoardingSelfProofStatus03::proved &&
            initial.certificate ==
                BoardingSelfCertificate03::shoulder_cofactor_split,
        "Complete shoulder cofactor proof covers fixed original fixture");
  auto rotated_arm = arm;
  rotated_arm.start_metres = quarter_turn(arm.start_metres);
  rotated_arm.end_metres = quarter_turn(arm.end_metres);
  auto rotated_trunk = trunk;
  rotated_trunk.center_metres = quarter_turn(trunk.center_metres);
  for (std::size_t i = 0; i < 3; ++i)
    rotated_trunk.frame.columns[i] = quarter_turn(trunk.frame.columns[i]);
  auto rotated_sphere = sphere;
  rotated_sphere.center_metres = rotated_arm.start_metres;
  check(
      independent_shoulder_bound(rotated_arm, rotated_trunk, rotated_sphere),
      "Exact generic quarter-turn preserves complete curved containment proof");
  const auto rotated = boarding_self_model03_shoulder(
      rotated_arm, rotated_trunk, rotated_sphere);
  check(rotated.status == BoardingSelfProofStatus03::proved &&
            rotated.certificate ==
                BoardingSelfCertificate03::shoulder_cofactor_split,
        "New covariant proof covers -X direction without ideal-normal geometry "
        "substitution");
  const BoardingBodyCapsule shaft{{0, 0, 0}, {0, 0, -1}, .125};
  const BoardingSelfSphere root{{0, 0, 0}, .5};
  const BoardingSelfEllipsoid cylinder_obstacle{
      {0, 0, -.625}, {.0625, .0625, .0625}, {}};
  check(
      strictly_inside(shaft, cylinder_obstacle.center_metres) &&
          strictly_inside(cylinder_obstacle, cylinder_obstacle.center_metres) &&
          strictly_outside(BoardingSelfRegion03{root},
                           cylinder_obstacle.center_metres),
      "Complete outside-sphere cylinder has genuine unowned common interior");
  check(proof_refuses(
            boarding_self_model03_shoulder(shaft, cylinder_obstacle, root)),
        "Checking root/distal alone cannot waive intervening cylinder "
        "obstruction");
  const long double distal_gap =
      norm(sub(wide(shaft.end_metres), wide(cylinder_obstacle.center_metres))) -
      shaft.radius_metres - .0625L;
  check(distal_gap > 0,
        "Cylinder-control obstacle genuinely excludes the entire distal ball");
  const BoardingSelfEllipsoid distal_obstacle{
      {0, 0, -1.0625}, {.015625, .015625, .015625}, {}};
  check(strictly_inside(shaft, distal_obstacle.center_metres) &&
            strictly_inside(distal_obstacle, distal_obstacle.center_metres) &&
            strictly_outside(BoardingSelfRegion03{root},
                             distal_obstacle.center_metres),
        "Distal cap beyond finite axis can be a strict unowned overlap");
  check(distal_obstacle.center_metres.z < shaft.end_metres.z,
        "Distal-control witness is beyond finite axis cylinder");
  check(proof_refuses(
            boarding_self_model03_shoulder(shaft, distal_obstacle, root)),
        "Finite cylinder-only check cannot skip distal endpoint ball");
  auto too_small = sphere;
  too_small.radius_metres = .03125;
  check(proof_refuses(boarding_self_model03_shoulder(arm, trunk, too_small)),
        "Root ball must actually fit finite registered sphere");
  auto bad_trunk = trunk;
  bad_trunk.frame.columns[0] = {0, 0, 0};
  check(boarding_self_model03_shoulder(arm, bad_trunk, sphere).status ==
            BoardingSelfProofStatus03::invalid_geometry,
        "Degenerate represented certificate frame refuses");
  auto wrong_root = sphere;
  wrong_root.center_metres.x = 0;
  check(proof_refuses(boarding_self_model03_shoulder(arm, trunk, wrong_root)),
        "Wrong sphere/actual shoulder anchor cannot certify ownership");
}
} // namespace
int main() {
  try {
    if (std::numeric_limits<long double>::digits < 64)
      throw std::runtime_error(
          "Independent deep-margin geometric oracle requires >=64 mantissa "
          "bits; exact dyadic contact fixtures do not rely on wider "
          "arithmetic");
    constants_and_invariants();
    validation_tests();
    private_invalid_tests();
    exact_norm_tests();
    axial_membership_tests();
    support_tests();
    private_pair_tests();
    private_neck_tests();
    shoulder_tests();
    public_tests();
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << "EXCEPTION: " << error.what() << '\n';
  }
  std::cout << checks << " boarding self Model03 checks, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
