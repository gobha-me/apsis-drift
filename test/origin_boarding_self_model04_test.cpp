#include "apsis_drift/origin_boarding_self_model03.hpp"
#include "apsis_drift/origin_boarding_self_model04.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <variant>

// Full-support and strict-membership oracles reuse the independently checked
// Recipe03 test recipe; no production private predicate is called here.
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
                        const BoardingBodyDiagnostic& b,
                        bool compare_policy = true) -> void {
  check(!compare_policy || a.policy_version == b.policy_version,
        "Canonical version identity");
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
    check(
        bits(x.required_ankle_pitch_degrees, y.required_ankle_pitch_degrees) &&
            bits(x.required_ankle_roll_degrees, y.required_ankle_roll_degrees),
        "Derived flat ankle unchanged");
  }
  for (std::size_t i = 0; i < a.pairs.size(); ++i) {
    const auto& x = a.pairs[i];
    const auto& y = b.pairs[i];
    check(x.first == y.first && x.second == y.second && x.kind == y.kind &&
              x.intersection == y.intersection &&
              x.witness.has_value() == y.witness.has_value(),
          "Canonical policy02 pair diagnostic retained");
    if (x.witness && y.witness)
      check(bits(x.witness->point_metres, y.witness->point_metres) &&
                bits(x.witness->first_depth_metres,
                     y.witness->first_depth_metres) &&
                bits(x.witness->second_depth_metres,
                     y.witness->second_depth_metres),
            "Canonical policy02 witness bits retained");
  }
  check(a.self_conflict == b.self_conflict &&
            a.unresolved_intersection == b.unresolved_intersection,
        "Canonical policy02 conflict flags retained");
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
                     const BoardingSelfDiagnostic04& body) -> void {
  const auto& trunk = std::get<BoardingSelfEllipsoid>(body.self_parts[1].solid);
  for (const auto& r : body.regions) {
    const auto a = part_index(r.first), b = part_index(r.second);
    const auto side = (a >= 9 || b >= 9) ? 1U : 0U;
    const auto& joint = body.canonical.joints[side];
    if (r.junction == BoardingSelfJunction::waist ||
        r.junction == BoardingSelfJunction::neck) {
      const auto* cap = std::get_if<BoardingSelfEllipsoidCap>(&r.region);
      check(cap != nullptr, "Recipe04 waist/neck are actual affine caps");
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
            "Recipe04 neck uses actual trunk center/halfY while waist is "
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
auto pair(const BoardingSelfDiagnostic04& body, unsigned a, unsigned b)
    -> const BoardingSelfPairDiagnostic03& {
  for (const auto& p : body.pairs)
    if (part_index(p.first) == a && part_index(p.second) == b) return p;
  throw std::runtime_error("Missing Recipe04 pair");
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
auto independent_connected(const BoardingSelfDiagnostic04& body,
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
                   const BoardingSelfDiagnostic04& body) -> void {
  const auto canonical = evaluate_origin_boarding_body02(pose);
  if (!canonical) throw std::runtime_error("Canonical control invalid");
  canonical_identity(body.canonical, *canonical);
  check(body.self_model_version == 4, "Output identifies Recipe04 separately");
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
      check(!accepted || p.certificate != BoardingSelfCertificate03::none,
            "Every accepted pair carries an explicit complete certificate");
      if (p.outcome == BoardingSelfPairOutcome::certified_no_interior)
        check(independent_separation(body.self_parts[a].solid,
                                     body.self_parts[b].solid),
              "Independent complete convex support corroborates separation");
      if (!p.connected_region_index) {
        check(
            p.outcome != BoardingSelfPairOutcome::certified_whole_owned &&
                p.outcome !=
                    BoardingSelfPairOutcome::strict_unowned_connected_interior,
            "Nonadjacent overlap receives no semantic ownership");
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
auto bits(const BoardingSelfAxialSlab03& a, const BoardingSelfAxialSlab03& b)
    -> bool {
  return bits(a.original_capsule, b.original_capsule) &&
         bits(a.root_metres, b.root_metres) &&
         bits(a.toward_metres, b.toward_metres) &&
         bits(a.axial_limit_metres, b.axial_limit_metres);
}
template <class A, class B>
auto identical_self(const A& a, const B& b) -> void {
  canonical_identity(a.canonical, b.canonical, false);
  check(a.self_checkpoint_passed == b.self_checkpoint_passed &&
            a.strict_conflict == b.strict_conflict &&
            a.unresolved_pair == b.unresolved_pair,
        "Zero-abduction summaries retain Recipe03 identity");
  for (std::size_t i = 0; i < a.self_parts.size(); ++i)
    check(a.self_parts[i].id == b.self_parts[i].id &&
              bits(a.self_parts[i].solid, b.self_parts[i].solid),
          "Every zero-abduction self-solid bit retained");
  for (std::size_t i = 0; i < a.regions.size(); ++i) {
    const auto& x = a.regions[i];
    const auto& y = b.regions[i];
    check(x.first == y.first && x.second == y.second &&
              x.junction == y.junction,
          "Zero-abduction connected region identities retained");
    std::visit(
        [&](const auto& r) {
          using T = std::decay_t<decltype(r)>;
          const auto* other = std::get_if<T>(&y.region);
          check(other && bits(r, *other),
                "All zero-abduction cap/sphere/slab geometry bits retained");
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
          "Every zero-abduction outcome/certificate/reason/witness retained");
    if (x.strict_witness_metres && y.strict_witness_metres)
      check(bits(*x.strict_witness_metres, *y.strict_witness_metres),
            "Strict witness component bits retained including signed zero");
  }
}
auto policy02() -> BoardingBodyPose {
  BoardingBodyPose pose;
  pose.policy_version = kBoardingBodyLateralPolicyVersion;
  return pose;
}
auto checkpoint(double direction, bool folded_arms) -> BoardingBodyPose {
  auto pose = policy02();
  pose.hip_metres.y =
      .1 + (.47285 + .47478) * std::cos(10 * std::numbers::pi / 180);
  pose.sides[0].hip_abduction_degrees = -10 * direction;
  pose.sides[1].hip_abduction_degrees = 10 * direction;
  const std::size_t lifted = direction == 1 ? 1 : 0;
  pose.sides[lifted].hip_flex_degrees = 30;
  pose.sides[lifted].knee_flex_degrees = 30;
  if (folded_arms)
    for (auto& side : pose.sides) {
      side.shoulder_flex_degrees = 45;
      side.elbow_flex_degrees = 135;
    }
  return pose;
}
auto load_projection(const BoardingBodyDiagnostic& body, std::size_t loaded)
    -> void {
  constexpr std::array<std::uint32_t, 15> weights{
      144, 540, 96, 120, 48, 12, 15, 10, 5, 120, 48, 12, 15, 10, 5};
  Wide weighted{};
  std::uint32_t total{};
  for (std::size_t i = 0; i < body.parts.size(); ++i) {
    const auto& part = body.parts[i];
    const auto point = std::visit(
        [](const auto& shape) -> Wide {
          using T = std::decay_t<decltype(shape)>;
          if constexpr (std::is_same_v<T, BoardingBodyCapsule>) {
            const auto a = wide(shape.start_metres), b = wide(shape.end_metres);
            return {(a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2};
          } else
            return wide(shape.center_metres);
        },
        part.world_reservation);
    check(part.mass_weight == weights[i], "Fixed surrogate mass fractions");
    weighted.x += point.x * weights[i];
    weighted.y += point.y * weights[i];
    weighted.z += point.z * weights[i];
    total += weights[i];
    const auto recorded = wide(part.mass_point_metres);
    check(norm(sub(recorded, point)) < 2e-15L,
          "Mass locations match actual box centers and full-capsule midpoints");
  }
  check(total == 1200 && kBoardingBodyMassDenominator == total,
        "Complete immutable mass denominator");
  const auto independent = scale(weighted, 1.L / total);
  check(norm(sub(independent, wide(body.center_of_mass_metres))) < 2e-15L,
        "Independent wider mass sum agrees with canonical COM");
  const auto sole = wide(body.joints[loaded].sole_origin_metres);
  const auto offset = sub(independent, sole);
  const auto lateral_margin = .06L - std::abs(offset.x);
  const auto forward_margin = .14L - std::abs(offset.z);
  check(std::abs(sole.y) < 2e-13L,
        "Loaded sole remains on abstract plane after arm change");
  check(body.joints[1 - loaded].sole_origin_metres.y > .06,
        "Opposite sole remains lifted in frozen checkpoint");
  check(lateral_margin > .03L && forward_margin > .03L,
        "Recomputed COM-centered20mm disk retains10mm finite sole margin");
  std::cout << "COM loaded=" << loaded << " lateral_margin=" << lateral_margin
            << " forward_margin=" << forward_margin << '\n';
}
auto frozen_tests() -> void {
  for (const double direction : {-1., 1.}) {
    for (const bool folded : {true, false}) {
      const auto pose = checkpoint(direction, folded);
      const auto actual = evaluate_origin_boarding_self_model04(pose);
      if (!actual) throw std::runtime_error("Frozen Body02 checkpoint invalid");
      common_checks(pose, *actual);
      region_geometry(pose, *actual);
      check(actual->canonical.policy_version == 2,
            "Recipe04 consumes explicit policy2 canonical body");
      BoardingBodyPose old_pose;
      old_pose.yaw_degrees = pose.yaw_degrees;
      const auto old_body = evaluate_origin_boarding_body(old_pose);
      if (!old_body)
        throw std::runtime_error("Historical boot frame control invalid");
      for (std::size_t side = 0; side < 2; ++side) {
        const auto& joint = actual->canonical.joints[side];
        check(std::abs(joint.knee_metres.x - joint.hip_metres.x) > .07,
              "Hip slab actually follows a tilted axis");
        check(std::abs(joint.ankle_metres.x - joint.knee_metres.x) > .07,
              "Ankle slab actually follows a tilted shin axis");
        const auto& boot = std::get<BoardingBodyBox>(
            actual->canonical.parts[5 + side * 6].world_reservation);
        const auto& old_boot = std::get<BoardingBodyBox>(
            old_body->parts[5 + side * 6].world_reservation);
        check(bits(boot.frame, old_boot.frame) &&
                  bits(boot.half_size_metres, RigidVector3{.06, .05, .14}),
              "Tilted canonical shin never rolls or resizes the flat boot");
        check(boot.frame.columns[0] == RigidVector3{1, 0, 0} &&
                  boot.frame.columns[1] == RigidVector3{0, 1, 0} &&
                  boot.frame.columns[2] == RigidVector3{0, 0, 1},
              "Frozen zero-yaw boots independently retain flat canonical axes");
        check(joint.required_ankle_pitch_degrees == 0 &&
                  joint.required_ankle_roll_degrees == -10 * direction,
              "Flat pitch/roll compensation remains explicit");
      }
      if (folded) {
        check(actual->self_checkpoint_passed && !actual->strict_conflict &&
                  !actual->unresolved_pair,
              "Frozen folded-arm lateral checkpoint certifies all105 pairs");
        load_projection(actual->canonical, direction == 1 ? 0 : 1);
      } else {
        check(!actual->self_checkpoint_passed && actual->strict_conflict,
              "Straight-arm lateral kinematics retain strict self refusal");
        for (const unsigned upper : {6U, 12U}) {
          const auto& p = pair(*actual, 1, upper);
          check(p.outcome == BoardingSelfPairOutcome::
                                 strict_unowned_connected_interior &&
                    p.certificate ==
                        BoardingSelfCertificate03::verified_strict_witness &&
                    p.strict_witness_metres.has_value(),
                "Both down arms retain actual trunk conflict witnesses");
        }
      }
      std::cout << "FROZEN direction=" << direction << " folded=" << folded
                << " passed=" << actual->self_checkpoint_passed
                << " strict=" << actual->strict_conflict
                << " unresolved=" << actual->unresolved_pair << '\n';
      // Preserve all failed pair diagnostics instead of changing the pose.
      if (!actual->self_checkpoint_passed)
        for (const auto& p : actual->pairs)
          if (p.outcome != BoardingSelfPairOutcome::certified_no_interior &&
              p.outcome != BoardingSelfPairOutcome::certified_whole_owned)
            std::cout << "PAIR " << part_index(p.first) << '/'
                      << part_index(p.second)
                      << " outcome=" << static_cast<unsigned>(p.outcome)
                      << " reason=" << static_cast<unsigned>(p.reason)
                      << " certificate=" << static_cast<unsigned>(p.certificate)
                      << '\n';
    }
  }
}
auto zero_compatibility() -> void {
  std::array<BoardingBodyPose, 4> poses{};
  for (auto& side : poses[0].sides) {
    side.shoulder_flex_degrees = 45;
    side.elbow_flex_degrees = 135;
  }
  poses[1] = poses[0];
  poses[1].yaw_degrees = 90;
  poses[2] = poses[0];
  poses[2].hip_metres = {.2, 1.12, -.5};
  poses[2].yaw_degrees = -37;
  poses[2].pelvis_lean_degrees = -10;
  poses[2].torso_relative_lean_degrees = 10;
  poses[2].sides[0].hip_flex_degrees = 20;
  poses[2].sides[0].knee_flex_degrees = 15;
  poses[2].sides[1].hip_flex_degrees = 35;
  poses[2].sides[1].knee_flex_degrees = 40;
  // Fourth is the default strict negative, with negative-zero inactive
  // freedoms.
  poses[3].sides[0].hip_abduction_degrees = -0.;
  poses[3].pelvis_twist_degrees = -0.;
  for (auto old_pose : poses) {
    const auto before = evaluate_origin_boarding_self_model03(old_pose);
    auto new_pose = old_pose;
    new_pose.policy_version = 2;
    const auto current = evaluate_origin_boarding_self_model04(new_pose);
    const auto after = evaluate_origin_boarding_self_model03(old_pose);
    if (!before || !current || !after)
      throw std::runtime_error("Zero-abduction compatibility fixture refused");
    check(before->self_model_version == 3 && current->self_model_version == 4 &&
              before->canonical.policy_version == 1 &&
              current->canonical.policy_version == 2,
          "Explicit versions are the only permitted zero-path difference");
    identical_self(*before, *current);
    identical_self(*before, *after);
    common_checks(new_pose, *current);
    region_geometry(new_pose, *current);
    check(!evaluate_origin_boarding_self_model03(new_pose) &&
              !evaluate_origin_boarding_self_model04(old_pose),
          "Named old/new evaluators never cross-admit body policies");
  }
}
auto validation() -> void {
  auto refuses = [](const BoardingBodyPose& p, BoardingBodyError error) {
    const auto result = evaluate_origin_boarding_self_model04(p);
    check(!result && result.error() == error,
          "Recipe04 preserves exact canonical validation refusal");
  };
  for (const std::uint32_t version :
       {0U, 1U, 3U, 4U, std::numeric_limits<std::uint32_t>::max()}) {
    auto p = policy02();
    p.policy_version = version;
    refuses(p, BoardingBodyError::unsupported_policy);
  }
  for (const auto member :
       {&BoardingBodyPose::suit_on, &BoardingBodyPose::pack_detached,
        &BoardingBodyPose::flat_boots}) {
    auto p = policy02();
    p.*member = false;
    refuses(p, BoardingBodyError::invalid_prerequisite);
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
  for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()}) {
    for (const auto member : pose_fields) {
      auto p = policy02();
      p.*member = bad;
      refuses(p, BoardingBodyError::non_finite_input);
    }
    for (std::size_t side = 0; side < 2; ++side)
      for (const auto member : side_fields) {
        auto p = policy02();
        p.sides[side].*member = bad;
        refuses(p, BoardingBodyError::non_finite_input);
      }
    for (std::size_t axis = 0; axis < 3; ++axis) {
      auto p = policy02();
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
    auto p = policy02();
    p.*member = std::numeric_limits<double>::denorm_min();
    refuses(p, BoardingBodyError::unsupported_freedom);
  }
  for (std::size_t side = 0; side < 2; ++side) {
    for (const auto member : {&BoardingBodySidePose::hip_axial_degrees,
                              &BoardingBodySidePose::ankle_roll_degrees,
                              &BoardingBodySidePose::shoulder_abduction_degrees,
                              &BoardingBodySidePose::shoulder_axial_degrees,
                              &BoardingBodySidePose::wrist_pitch_degrees,
                              &BoardingBodySidePose::wrist_yaw_degrees,
                              &BoardingBodySidePose::wrist_roll_degrees}) {
      auto p = policy02();
      p.sides[side].*member = std::numeric_limits<double>::denorm_min();
      refuses(p, BoardingBodyError::unsupported_freedom);
    }
    for (double direction : {-1., 1.}) {
      auto p = policy02();
      p.sides[side].hip_abduction_degrees = 15 * direction;
      check(evaluate_origin_boarding_self_model04(p).has_value(),
            "Exact derived roll boundary validated without promising self "
            "clearance");
      p.sides[side].hip_abduction_degrees =
          std::nextafter(15 * direction, 16 * direction);
      refuses(p, BoardingBodyError::joint_limit);
      p = checkpoint(direction, true);
      p.pelvis_lean_degrees = std::numeric_limits<double>::denorm_min();
      refuses(p, BoardingBodyError::unsupported_freedom);
    }
    auto p = policy02();
    p.sides[side].hip_flex_degrees = std::nextafter(30., 31.);
    refuses(p, BoardingBodyError::joint_limit);
    p = policy02();
    p.sides[side].hip_flex_degrees = 105;
    p.sides[side].knee_flex_degrees = std::nextafter(135., 136.);
    refuses(p, BoardingBodyError::joint_limit);
    p = policy02();
    p.sides[side].shoulder_flex_degrees = std::nextafter(150., 151.);
    refuses(p, BoardingBodyError::joint_limit);
    p = policy02();
    p.sides[side].elbow_flex_degrees = std::nextafter(145., 146.);
    refuses(p, BoardingBodyError::joint_limit);
  }
  auto p = policy02();
  p.hip_metres.x = std::nextafter(8., 9.);
  refuses(p, BoardingBodyError::excessive_workspace);
  p = policy02();
  p.yaw_degrees = std::nextafter(180., 181.);
  refuses(p, BoardingBodyError::joint_limit);
  p = policy02();
  p.pelvis_lean_degrees = 35;
  p.torso_relative_lean_degrees = std::numeric_limits<double>::epsilon() * 32;
  refuses(p, BoardingBodyError::joint_limit);
}
auto static_contract() -> void {
  static_assert(
      !std::is_same_v<BoardingSelfDiagnostic03, BoardingSelfDiagnostic04>);
  static_assert(BoardingSelfDiagnostic04::recipe_complete &&
                !BoardingSelfDiagnostic04::arbitrary_pose_proof_complete &&
                !BoardingSelfDiagnostic04::support_qualified &&
                !BoardingSelfDiagnostic04::world_qualified &&
                !BoardingSelfDiagnostic04::route_qualified &&
                !BoardingSelfDiagnostic04::actor_qualified);
  check(kBoardingSelfModel04Version == 4 && kBoardingSelfModel03Version == 3,
        "Distinct finite recipe identities");
  check(bits(kBoardingSelfWaistLimitMetres, 0x1.bb0cd605d7512p-3) &&
            bits(kBoardingSelfHipLengthMetres, 0x1.bb0cd605d7512p-3) &&
            bits(kBoardingSelfKneeRadiusMetres, 0x1.18f69ad39e925p-2) &&
            bits(kBoardingSelfAnkleLengthMetres, 0x1.7529d1ebf8034p-3) &&
            bits(kBoardingSelfShoulderRadiusMetres, 0x1.56872b020c49cp-2) &&
            bits(kBoardingSelfElbowRadiusMetres, 0x1.bab11b9fbb660p-3) &&
            bits(kBoardingSelfWristLengthMetres, 0x1.12c49dd0cc1e9p-4),
        "All fixed connected neighborhoods retain exact finite dimensions");
}
} // namespace
int main() {
  try {
    if (std::numeric_limits<long double>::digits < 64)
      throw std::runtime_error(
          "Independent deep-margin oracle requires64 mantissa bits");
    static_contract();
    validation();
    zero_compatibility();
    frozen_tests();
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << "EXCEPTION: " << error.what() << '\n';
  }
  std::cout << checks << " boarding self Recipe04 checks, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
