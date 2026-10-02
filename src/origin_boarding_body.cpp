#include "apsis_drift/origin_boarding_body.hpp"
#include "origin_boarding_body_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <type_traits>
#include <vector>

namespace apsis_drift {
namespace {
using Vec = RigidVector3;
using Box = BoardingBodyBox;
using Capsule = BoardingBodyCapsule;
using Narrow = detail::BoardingBodyNarrowIntersection;
constexpr double radians_per_degree{std::numbers::pi / 180};
// Private dimensions have a numerical domain, not a collision tolerance.
// Public policy01 dimensions are fixed and all exceed this lower bound.
constexpr double minimum_private_dimension_metres{1e-6};
auto add(Vec a, Vec b) -> Vec {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(Vec a, Vec b) -> Vec {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(Vec a, double b) -> Vec {
  return {a.x * b, a.y * b, a.z * b};
}
auto dot(Vec a, Vec b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto cross(Vec a, Vec b) -> Vec {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto norm(Vec a) -> double {
  return std::hypot(a.x, a.y, a.z);
}
auto component(Vec a, std::size_t i) -> double {
  return i == 0 ? a.x : i == 1 ? a.y : a.z;
}
auto finite(Vec a) -> bool {
  return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
auto bounded(Vec a, double bound) -> bool {
  return finite(a) && std::abs(a.x) <= bound && std::abs(a.y) <= bound &&
         std::abs(a.z) <= bound;
}
auto rotate(const BoardingBodyFrame& f, Vec p) -> Vec {
  return add(add(scale(f.columns[0], p.x), scale(f.columns[1], p.y)),
             scale(f.columns[2], p.z));
}
auto local(const Box& b, Vec p) -> Vec {
  const auto d = sub(p, b.center_metres);
  return {dot(d, b.frame.columns[0]), dot(d, b.frame.columns[1]),
          dot(d, b.frame.columns[2])};
}
auto multiply(const BoardingBodyFrame& a, const BoardingBodyFrame& b)
    -> BoardingBodyFrame {
  return {{rotate(a, b.columns[0]), rotate(a, b.columns[1]),
           rotate(a, b.columns[2])}};
}
auto valid(const Box& b) -> bool {
  if (!bounded(b.center_metres, 16) || !bounded(b.half_size_metres, 4) ||
      b.half_size_metres.x < minimum_private_dimension_metres ||
      b.half_size_metres.y < minimum_private_dimension_metres ||
      b.half_size_metres.z < minimum_private_dimension_metres)
    return false;
  // Frame conditioning only: this does not enter any contact, distance, SAT
  // or witness comparison. It admits the rounding of registered trig frames,
  // but refuses malformed/scaled/sheared/reflected private diagnostic inputs.
  constexpr auto frame_rounding = 32 * std::numeric_limits<double>::epsilon();
  for (std::size_t i = 0; i < 3; ++i) {
    if (!finite(b.frame.columns[i]) ||
        std::abs(dot(b.frame.columns[i], b.frame.columns[i]) - 1) >
            frame_rounding)
      return false;
    for (std::size_t j = i + 1; j < 3; ++j)
      if (std::abs(dot(b.frame.columns[i], b.frame.columns[j])) >
          frame_rounding)
        return false;
  }
  return std::abs(dot(cross(b.frame.columns[0], b.frame.columns[1]),
                      b.frame.columns[2]) -
                  1) <= frame_rounding;
}
auto valid(const Capsule& c) -> bool {
  if (!bounded(c.start_metres, 16) || !bounded(c.end_metres, 16) ||
      !std::isfinite(c.radius_metres) ||
      c.radius_metres < minimum_private_dimension_metres || c.radius_metres > 4)
    return false;
  const auto axis = sub(c.end_metres, c.start_metres);
  // Degenerate capsules are spheres. Nonzero axes whose squared lengths
  // underflow have no supported finite segment-parameter calculation.
  return axis == Vec{} || dot(axis, axis) > 0;
}
auto closest_on_segment(Vec p, Vec a, Vec b) -> Vec {
  const auto d = sub(b, a);
  const auto length_squared = dot(d, d);
  if (length_squared == 0) return a;
  const auto t = std::clamp(dot(sub(p, a), d) / length_squared, 0.0, 1.0);
  return add(a, scale(d, t));
}
auto box_depth(const Box& b, Vec p) -> double {
  const auto q = local(b, p);
  return std::min({b.half_size_metres.x - std::abs(q.x),
                   b.half_size_metres.y - std::abs(q.y),
                   b.half_size_metres.z - std::abs(q.z)});
}
auto capsule_depth(const Capsule& c, Vec p) -> double {
  return c.radius_metres -
         norm(sub(p, closest_on_segment(p, c.start_metres, c.end_metres)));
}
auto witnessed(Vec p, double a, double b) -> Narrow {
  if (finite(p) && std::isfinite(a) && std::isfinite(b) && a > 0 && b > 0)
    return {BoardingBodyIntersection::interior_with_witness,
            BoardingBodyInteriorWitness{p, a, b}};
  return {BoardingBodyIntersection::interior_unresolved, std::nullopt};
}
struct ExactSum {
  double high{}, low{};
};
auto two_sum(double a, double b) -> ExactSum {
  const auto high = a + b;
  const auto bv = high - a;
  const auto av = high - bv;
  return {high, (a - av) + (b - bv)};
}
enum class SquaredRelation : std::uint8_t { below, equal, above, unsupported };
// Three distance coordinates and two radii need at most 24 product/residual
// terms. Preserve exact binary64 subtraction/addition and fma residuals in a
// fixed expansion, so rounded dot/sqrt equality cannot erase strict intrusion.
// This arithmetic never introduces geometric slack or wider production types.
auto squared_relation(Vec p, Vec q, double first_radius, double second_radius)
    -> SquaredRelation {
  std::array<double, 32> expansion{};
  std::size_t count{};
  bool supported{true};
  auto append = [&](double scalar) {
    std::array<double, 32> next{};
    std::size_t next_count{};
    auto carry = scalar;
    for (std::size_t i = 0; i < count; ++i) {
      const auto sum = two_sum(carry, expansion[i]);
      if (sum.low != 0) next[next_count++] = sum.low;
      carry = sum.high;
    }
    if (carry != 0) next[next_count++] = carry;
    expansion = next;
    count = next_count;
  };
  auto product = [&](double a, double b, double sign) {
    const auto high = a * b;
    // fma cannot preserve a residual below the binary64 subnormal domain.
    // Unsupported private scales refuse instead of asserting equality.
    if (a != 0 && b != 0 &&
        std::abs(high) < std::numeric_limits<double>::min()) {
      supported = false;
      return;
    }
    append(sign * std::fma(a, b, -high));
    append(sign * high);
  };
  auto square = [&](ExactSum value, double sign) {
    product(value.high, value.high, sign);
    product(2 * value.high, value.low, sign);
    product(value.low, value.low, sign);
  };
  for (std::size_t i = 0; i < 3; ++i)
    square(two_sum(component(p, i), -component(q, i)), 1);
  square(two_sum(first_radius, second_radius), -1);
  if (!supported) return SquaredRelation::unsupported;
  if (count == 0) return SquaredRelation::equal;
  return expansion[count - 1] < 0 ? SquaredRelation::below
                                  : SquaredRelation::above;
}
struct SegmentPair {
  Vec first, second;
  double distance_squared{};
};
// Boundary candidates plus the unconstrained stationary point cover the
// finite square of segment parameters. Cross-norm squared avoids subtracting
// nearly equal Gram products for almost-parallel axes; no epsilon is used.
auto segment_pair(const Capsule& a, const Capsule& b) -> SegmentPair {
  SegmentPair best{a.start_metres, b.start_metres,
                   dot(sub(a.start_metres, b.start_metres),
                       sub(a.start_metres, b.start_metres))};
  auto consider = [&](Vec p, Vec q) {
    const auto d = sub(p, q);
    const auto squared = dot(d, d);
    if (squared < best.distance_squared) best = {p, q, squared};
  };
  for (const auto p : {a.start_metres, a.end_metres})
    consider(p, closest_on_segment(p, b.start_metres, b.end_metres));
  for (const auto p : {b.start_metres, b.end_metres})
    consider(closest_on_segment(p, a.start_metres, a.end_metres), p);
  const auto u = sub(a.end_metres, a.start_metres);
  const auto v = sub(b.end_metres, b.start_metres);
  const auto n = cross(u, v);
  const auto determinant = dot(n, n);
  if (determinant > 0) {
    const auto r = sub(b.start_metres, a.start_metres);
    const auto s = dot(cross(r, v), n) / determinant;
    const auto t = dot(cross(r, u), n) / determinant;
    if (s >= 0 && s <= 1 && t >= 0 && t <= 1)
      consider(add(a.start_metres, scale(u, s)),
               add(b.start_metres, scale(v, t)));
  }
  return best;
}
struct SegmentBox {
  Vec axis_point, box_point;
  double distance_squared{};
  Vec local_axis_point, local_box_point;
};
auto segment_box(const Capsule& c, const Box& b) -> SegmentBox {
  const auto start = local(b, c.start_metres);
  const auto finish = local(b, c.end_metres);
  const auto direction = sub(finish, start);
  std::vector<double> events{0, 1};
  events.reserve(8);
  for (std::size_t i = 0; i < 3; ++i) {
    const auto slope = component(direction, i);
    if (slope == 0) continue;
    for (const double sign : {-1.0, 1.0}) {
      const auto t =
          (sign * component(b.half_size_metres, i) - component(start, i)) /
          slope;
      if (t > 0 && t < 1) events.push_back(t);
    }
  }
  std::ranges::sort(events);
  events.erase(std::unique(events.begin(), events.end()), events.end());
  double best_squared = std::numeric_limits<double>::infinity();
  double best_t{};
  Vec best_box;
  auto consider = [&](double t) {
    const auto p = add(start, scale(direction, t));
    const Vec q{std::clamp(p.x, -b.half_size_metres.x, b.half_size_metres.x),
                std::clamp(p.y, -b.half_size_metres.y, b.half_size_metres.y),
                std::clamp(p.z, -b.half_size_metres.z, b.half_size_metres.z)};
    const auto d = sub(p, q);
    const auto squared = dot(d, d);
    if (squared < best_squared) {
      best_squared = squared;
      best_t = t;
      best_box = q;
    }
  };
  for (const auto t : events)
    consider(t);
  for (std::size_t e = 0; e + 1 < events.size(); ++e) {
    const auto middle = events[e] + (events[e + 1] - events[e]) / 2;
    double numerator{}, denominator{};
    for (std::size_t i = 0; i < 3; ++i) {
      const auto p = component(start, i) + component(direction, i) * middle;
      const auto h = component(b.half_size_metres, i);
      if (p < -h || p > h) {
        const auto offset = component(start, i) - (p < -h ? -h : h);
        const auto slope = component(direction, i);
        numerator += slope * offset;
        denominator += slope * slope;
      }
    }
    if (denominator > 0)
      consider(std::clamp(-numerator / denominator, events[e], events[e + 1]));
  }
  return {add(c.start_metres, scale(sub(c.end_metres, c.start_metres), best_t)),
          add(b.center_metres, rotate(b.frame, best_box)), best_squared,
          add(start, scale(direction, best_t)), best_box};
}
auto corners(const Box& b) -> std::array<Vec, 8> {
  std::array<Vec, 8> result;
  for (std::size_t i = 0; i < result.size(); ++i)
    result[i] = add(
        b.center_metres,
        rotate(b.frame,
               {(i & 1) != 0 ? b.half_size_metres.x : -b.half_size_metres.x,
                (i & 2) != 0 ? b.half_size_metres.y : -b.half_size_metres.y,
                (i & 4) != 0 ? b.half_size_metres.z : -b.half_size_metres.z}));
  return result;
}
auto box_radius(const Box& b, Vec axis) -> double {
  return (std::abs(dot(b.frame.columns[0], axis)) * b.half_size_metres.x +
          std::abs(dot(b.frame.columns[1], axis)) * b.half_size_metres.y) +
         std::abs(dot(b.frame.columns[2], axis)) * b.half_size_metres.z;
}
auto connected(BoardingBodyPartId a, BoardingBodyPartId b) -> bool {
  using Id = BoardingBodyPartId;
  if ((a == Id::pelvis && b == Id::trunk) ||
      (a == Id::trunk && b == Id::helmet))
    return true;
  for (const auto base : {3U, 9U}) {
    const auto thigh = static_cast<Id>(base);
    const auto shin = static_cast<Id>(base + 1);
    const auto boot = static_cast<Id>(base + 2);
    const auto upper = static_cast<Id>(base + 3);
    const auto fore = static_cast<Id>(base + 4);
    const auto hand = static_cast<Id>(base + 5);
    if ((a == Id::pelvis && b == thigh) || (a == thigh && b == shin) ||
        (a == shin && b == boot) || (a == Id::trunk && b == upper) ||
        (a == upper && b == fore) || (a == fore && b == hand))
      return true;
  }
  return false;
}
auto narrow(const BoardingBodyPart& a, const BoardingBodyPart& b) -> Narrow {
  return std::visit(
      [](const auto& first, const auto& second) -> Narrow {
        using First = std::decay_t<decltype(first)>;
        using Second = std::decay_t<decltype(second)>;
        if constexpr (std::is_same_v<First, Box> && std::is_same_v<Second, Box>)
          return detail::boarding_body_box_box(first, second);
        else if constexpr (std::is_same_v<First, Capsule> &&
                           std::is_same_v<Second, Capsule>)
          return detail::boarding_body_capsule_capsule(first, second);
        else if constexpr (std::is_same_v<First, Capsule>)
          return detail::boarding_body_capsule_box(first, second);
        else {
          auto result = detail::boarding_body_capsule_box(second, first);
          if (result.witness)
            std::swap(result.witness->first_depth_metres,
                      result.witness->second_depth_metres);
          return result;
        }
      },
      a.world_reservation, b.world_reservation);
}
auto all_angles(const BoardingBodySidePose& s) -> std::array<double, 12> {
  return {s.hip_flex_degrees,       s.knee_flex_degrees,
          s.shoulder_flex_degrees,  s.elbow_flex_degrees,
          s.hip_abduction_degrees,  s.hip_axial_degrees,
          s.ankle_roll_degrees,     s.shoulder_abduction_degrees,
          s.shoulder_axial_degrees, s.wrist_pitch_degrees,
          s.wrist_yaw_degrees,      s.wrist_roll_degrees};
}
auto within(double value, double low, double high) -> bool {
  return value >= low && value <= high;
}
auto validate(const BoardingBodyPose& p)
    -> std::expected<void, BoardingBodyError> {
  if (p.policy_version != kBoardingBodyPolicyVersion)
    return std::unexpected(BoardingBodyError::unsupported_policy);
  if (!finite(p.hip_metres))
    return std::unexpected(BoardingBodyError::non_finite_input);
  for (const auto angle :
       {p.yaw_degrees, p.pelvis_lean_degrees, p.torso_relative_lean_degrees,
        p.pelvis_twist_degrees, p.torso_twist_degrees, p.neck_pitch_degrees,
        p.neck_yaw_degrees, p.neck_roll_degrees})
    if (!std::isfinite(angle))
      return std::unexpected(BoardingBodyError::non_finite_input);
  for (const auto& s : p.sides)
    for (const auto angle : all_angles(s))
      if (!std::isfinite(angle))
        return std::unexpected(BoardingBodyError::non_finite_input);
  if (!bounded(p.hip_metres, kBoardingBodyHipWorkspaceMetres))
    return std::unexpected(BoardingBodyError::excessive_workspace);
  if (!p.suit_on || !p.pack_detached || !p.flat_boots)
    return std::unexpected(BoardingBodyError::invalid_prerequisite);
  if (p.pelvis_twist_degrees != 0 || p.torso_twist_degrees != 0 ||
      p.neck_pitch_degrees != 0 || p.neck_yaw_degrees != 0 ||
      p.neck_roll_degrees != 0)
    return std::unexpected(BoardingBodyError::unsupported_freedom);
  for (const auto& s : p.sides)
    if (s.hip_abduction_degrees != 0 || s.hip_axial_degrees != 0 ||
        s.ankle_roll_degrees != 0 || s.shoulder_abduction_degrees != 0 ||
        s.shoulder_axial_degrees != 0 || s.wrist_pitch_degrees != 0 ||
        s.wrist_yaw_degrees != 0 || s.wrist_roll_degrees != 0)
      return std::unexpected(BoardingBodyError::unsupported_freedom);
  if (!within(p.yaw_degrees, -180, 180) ||
      !within(p.pelvis_lean_degrees, -35, 35) ||
      !within(p.torso_relative_lean_degrees, -35, 35) ||
      !within(p.pelvis_lean_degrees + p.torso_relative_lean_degrees, -35, 35))
    return std::unexpected(BoardingBodyError::joint_limit);
  for (const auto& s : p.sides)
    if (!within(s.hip_flex_degrees, -20, 120) ||
        !within(s.knee_flex_degrees, 0, 135) ||
        !within(s.shoulder_flex_degrees, -30, 150) ||
        !within(s.elbow_flex_degrees, 0, 145) ||
        !within(
            -(p.pelvis_lean_degrees + s.hip_flex_degrees - s.knee_flex_degrees),
            -30, 30))
      return std::unexpected(BoardingBodyError::joint_limit);
  return {};
}
} // namespace

namespace detail {
auto boarding_body_rotation(double yaw_radians, double pitch_radians)
    -> BoardingBodyFrame {
  const auto cy = std::cos(yaw_radians), sy = std::sin(yaw_radians);
  const auto cp = std::cos(pitch_radians), sp = std::sin(pitch_radians);
  return {
      {Vec{cy, 0, -sy}, Vec{sy * sp, cp, cy * sp}, Vec{sy * cp, -sp, cy * cp}}};
}
auto boarding_body_box_interior_depth(const Box& b, Vec p) -> double {
  if (!valid(b) || !finite(p)) return std::numeric_limits<double>::quiet_NaN();
  return box_depth(b, p);
}
auto boarding_body_capsule_interior_depth(const Capsule& c, Vec p) -> double {
  if (!valid(c) || !finite(p)) return std::numeric_limits<double>::quiet_NaN();
  return capsule_depth(c, p);
}
auto boarding_body_capsule_capsule(const Capsule& a, const Capsule& b)
    -> Narrow {
  if (!valid(a) || !valid(b))
    return {BoardingBodyIntersection::invalid_geometry, std::nullopt};
  const auto nearest = segment_pair(a, b);
  const auto radii = a.radius_metres + b.radius_metres;
  const auto relation = squared_relation(nearest.first, nearest.second,
                                         a.radius_metres, b.radius_metres);
  if (relation == SquaredRelation::unsupported)
    return {BoardingBodyIntersection::invalid_geometry, std::nullopt};
  if (relation != SquaredRelation::below)
    return {BoardingBodyIntersection::separated_or_contact, std::nullopt};
  const auto distance = std::sqrt(nearest.distance_squared);
  // sqrt can round a strict oblique intrusion back to contact. Placement
  // arithmetic cannot erase the preceding strict squared predicate.
  if (distance >= radii)
    return {BoardingBodyIntersection::interior_unresolved, std::nullopt};
  Vec point = nearest.first;
  if (distance > 0) {
    const auto low = std::max(0.0, distance - b.radius_metres);
    const auto high = std::min(a.radius_metres, distance);
    const auto along = low + (high - low) / 2;
    point = add(nearest.first,
                scale(sub(nearest.second, nearest.first), along / distance));
  }
  return witnessed(point, capsule_depth(a, point), capsule_depth(b, point));
}
auto boarding_body_capsule_box(const Capsule& c, const Box& b) -> Narrow {
  if (!valid(c) || !valid(b))
    return {BoardingBodyIntersection::invalid_geometry, std::nullopt};
  const auto nearest = segment_box(c, b);
  const auto relation = squared_relation(
      nearest.local_axis_point, nearest.local_box_point, c.radius_metres, 0);
  if (relation == SquaredRelation::unsupported)
    return {BoardingBodyIntersection::invalid_geometry, std::nullopt};
  if (relation != SquaredRelation::below)
    return {BoardingBodyIntersection::separated_or_contact, std::nullopt};
  const auto distance = std::sqrt(nearest.distance_squared);
  if (distance >= c.radius_metres)
    return {BoardingBodyIntersection::interior_unresolved, std::nullopt};
  // The closest closed-box point moved toward its genuine center is interior.
  // Move by at most one quarter of the available radial gap; if this rounds
  // back to a boundary, keep the overlap unresolved instead of adding epsilon.
  const auto inward = sub(b.center_metres, nearest.box_point);
  const auto inward_length = norm(inward);
  const auto fraction =
      inward_length == 0
          ? 0.0
          : std::min(.5, (c.radius_metres - distance) / (4 * inward_length));
  const auto point = add(nearest.box_point, scale(inward, fraction));
  return witnessed(point, capsule_depth(c, point), box_depth(b, point));
}
auto boarding_body_box_box(const Box& a, const Box& b) -> Narrow {
  if (!valid(a) || !valid(b))
    return {BoardingBodyIntersection::invalid_geometry, std::nullopt};
  std::array<Vec, 15> axes;
  std::copy(a.frame.columns.begin(), a.frame.columns.end(), axes.begin());
  std::copy(b.frame.columns.begin(), b.frame.columns.end(), axes.begin() + 3);
  std::size_t cursor{6};
  for (const auto x : a.frame.columns)
    for (const auto y : b.frame.columns)
      axes[cursor++] = cross(x, y);
  bool boundary{};
  const auto delta = sub(b.center_metres, a.center_metres);
  for (const auto axis : axes) {
    if (dot(axis, axis) == 0) continue;
    const auto displacement = std::abs(dot(delta, axis));
    const auto extent = box_radius(a, axis) + box_radius(b, axis);
    if (displacement > extent)
      return {BoardingBodyIntersection::separated_or_contact, std::nullopt};
    boundary = boundary || displacement == extent;
  }
  if (boundary)
    return {BoardingBodyIntersection::separated_or_contact, std::nullopt};
  // A center-to-center segment often provides a well-inside witness without
  // depending on rounded edge/face vertices. It is only a candidate: the full
  // SAT classification and both actual strict depths remain mandatory.
  double low{}, high{1};
  bool segment_possible{true};
  for (const auto* box : {&a, &b}) {
    const auto start = local(*box, a.center_metres);
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const auto slope = dot(delta, box->frame.columns[axis]);
      const auto offset = component(start, axis);
      const auto half = component(box->half_size_metres, axis);
      if (slope == 0) {
        segment_possible = segment_possible && std::abs(offset) < half;
        continue;
      }
      const auto first = (-half - offset) / slope;
      const auto second = (half - offset) / slope;
      low = std::max(low, std::min(first, second));
      high = std::min(high, std::max(first, second));
    }
  }
  if (segment_possible && low < high) {
    const auto point =
        add(a.center_metres, scale(delta, low + (high - low) / 2));
    const auto result =
        witnessed(point, box_depth(a, point), box_depth(b, point));
    if (result.witness) return result;
  }
  std::vector<Vec> vertices;
  vertices.reserve(160);
  auto retain = [&](Vec p) {
    if (finite(p) && box_depth(a, p) >= 0 && box_depth(b, p) >= 0)
      vertices.push_back(p);
  };
  const auto ac = corners(a), bc = corners(b);
  for (const auto p : ac)
    retain(p);
  for (const auto p : bc)
    retain(p);
  // Every intersection-polytope vertex is an original corner or an edge of
  // one box intersecting a face of the other. Closed membership is checked
  // without a clipping epsilon; final evidence needs strict membership.
  auto edge_faces = [&](const std::array<Vec, 8>& points, const Box& other) {
    for (std::size_t i = 0; i < 8; ++i)
      for (std::size_t bit = 1; bit <= 4; bit *= 2) {
        if ((i & bit) != 0) continue;
        const auto p = points[i], d = sub(points[i | bit], p);
        const auto q = local(other, p);
        for (std::size_t axis = 0; axis < 3; ++axis) {
          const auto slope = dot(d, other.frame.columns[axis]);
          if (slope == 0) continue;
          for (const double sign : {-1.0, 1.0}) {
            const auto t = (sign * component(other.half_size_metres, axis) -
                            component(q, axis)) /
                           slope;
            if (t >= 0 && t <= 1) retain(add(p, scale(d, t)));
          }
        }
      }
  };
  edge_faces(ac, b);
  edge_faces(bc, a);
  for (const auto p : {a.center_metres, b.center_metres}) {
    const auto result = witnessed(p, box_depth(a, p), box_depth(b, p));
    if (result.witness) return result;
  }
  if (!vertices.empty()) {
    const auto anchor = vertices.front();
    Vec sum;
    for (const auto p : vertices)
      sum = add(sum, sub(p, anchor));
    const auto point =
        add(anchor, scale(sum, 1.0 / static_cast<double>(vertices.size())));
    return witnessed(point, box_depth(a, point), box_depth(b, point));
  }
  return {BoardingBodyIntersection::interior_unresolved, std::nullopt};
}
} // namespace detail

auto evaluate_origin_boarding_body(const BoardingBodyPose& p)
    -> std::expected<BoardingBodyDiagnostic, BoardingBodyError> {
  if (const auto checked = validate(p); !checked)
    return std::unexpected(checked.error());
  BoardingBodyDiagnostic result;
  const auto root =
      detail::boarding_body_rotation(p.yaw_degrees * radians_per_degree, 0);
  const auto pelvis =
      multiply(root, detail::boarding_body_rotation(0, p.pelvis_lean_degrees *
                                                           radians_per_degree));
  const auto trunk = multiply(
      pelvis, detail::boarding_body_rotation(0, p.torso_relative_lean_degrees *
                                                    radians_per_degree));
  auto box_part = [&](std::size_t index, Vec center, Vec half,
                      const BoardingBodyFrame& frame, std::uint32_t weight) {
    result.parts[index] = {static_cast<BoardingBodyPartId>(index),
                           Box{center, half, frame}, center, weight};
  };
  auto capsule_part = [&](std::size_t index, Vec start, Vec finish,
                          double radius, std::uint32_t weight) {
    result.parts[index] = {static_cast<BoardingBodyPartId>(index),
                           Capsule{start, finish, radius},
                           add(start, scale(sub(finish, start), .5)), weight};
  };
  box_part(0, p.hip_metres, {.24, .12, .18}, pelvis, 144);
  box_part(1, add(p.hip_metres, rotate(trunk, {0, .3495, 0})),
           {.26, .2695, .18}, trunk, 540);
  box_part(2, add(p.hip_metres, rotate(trunk, {0, .70237, 0})), {.16, .18, .18},
           trunk, 96);
  result.eye_metres = add(p.hip_metres, rotate(trunk, {0, .65237, 0}));
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& angles = p.sides[side];
    auto& joints = result.joints[side];
    const auto sign = side == 0 ? -1.0 : 1.0;
    const auto thigh =
        multiply(pelvis, detail::boarding_body_rotation(
                             0, angles.hip_flex_degrees * radians_per_degree));
    const auto shin =
        multiply(thigh, detail::boarding_body_rotation(
                            0, -angles.knee_flex_degrees * radians_per_degree));
    const auto upper = multiply(
        trunk, detail::boarding_body_rotation(0, angles.shoulder_flex_degrees *
                                                     radians_per_degree));
    const auto fore =
        multiply(upper, detail::boarding_body_rotation(
                            0, angles.elbow_flex_degrees * radians_per_degree));
    joints.hip_metres = add(p.hip_metres, rotate(pelvis, {sign * .14, 0, 0}));
    joints.knee_metres = add(joints.hip_metres, rotate(thigh, {0, -.47285, 0}));
    joints.ankle_metres =
        add(joints.knee_metres, rotate(shin, {0, -.47478, 0}));
    joints.sole_origin_metres =
        add(joints.ankle_metres, rotate(root, {0, -.10, 0}));
    joints.required_ankle_pitch_degrees =
        -(p.pelvis_lean_degrees + angles.hip_flex_degrees -
          angles.knee_flex_degrees);
    joints.shoulder_metres =
        add(p.hip_metres, rotate(trunk, {sign * .20265, .579, 0}));
    joints.elbow_metres =
        add(joints.shoulder_metres, rotate(upper, {0, -.35898, 0}));
    joints.wrist_metres = add(joints.elbow_metres, rotate(fore, {0, -.386, 0}));
    const auto base = 3 + side * 6;
    capsule_part(base, joints.hip_metres, joints.knee_metres, .105, 120);
    capsule_part(base + 1, joints.knee_metres, joints.ankle_metres, .075, 48);
    box_part(base + 2, add(joints.ankle_metres, rotate(root, {0, -.05, 0})),
             {.06, .05, .14}, root, 12);
    capsule_part(base + 3, joints.shoulder_metres, joints.elbow_metres, .065,
                 15);
    capsule_part(base + 4, joints.elbow_metres, joints.wrist_metres, .055, 10);
    // Policy01's frozen hand follows the trunk, not the forearm or a newly
    // invented wrist frame. No independent wrist articulation is admitted.
    box_part(base + 5, joints.wrist_metres, {.04, .05, .02}, trunk, 5);
  }
  Vec weighted;
  std::uint32_t total_weight{};
  for (const auto& part : result.parts) {
    weighted = add(weighted, scale(part.mass_point_metres,
                                   static_cast<double>(part.mass_weight)));
    total_weight += part.mass_weight;
  }
  if (total_weight != kBoardingBodyMassDenominator)
    return std::unexpected(BoardingBodyError::numerical_failure);
  result.center_of_mass_metres = {weighted.x / kBoardingBodyMassDenominator,
                                  weighted.y / kBoardingBodyMassDenominator,
                                  weighted.z / kBoardingBodyMassDenominator};
  std::size_t pair_index{};
  for (std::size_t first = 0; first < result.parts.size(); ++first)
    for (std::size_t second = first + 1; second < result.parts.size();
         ++second) {
      const auto& a = result.parts[first];
      const auto& b = result.parts[second];
      const auto intersection = narrow(a, b);
      if (intersection.intersection ==
          BoardingBodyIntersection::invalid_geometry)
        return std::unexpected(BoardingBodyError::numerical_failure);
      result.pairs[pair_index++] = {
          a.id, b.id,
          connected(a.id, b.id) ? BoardingBodyPairKind::unregistered_connected
                                : BoardingBodyPairKind::nonadjacent,
          intersection.intersection, intersection.witness};
      result.self_conflict =
          result.self_conflict ||
          intersection.intersection ==
              BoardingBodyIntersection::interior_with_witness ||
          intersection.intersection ==
              BoardingBodyIntersection::interior_unresolved;
      result.unresolved_intersection =
          result.unresolved_intersection ||
          intersection.intersection ==
              BoardingBodyIntersection::interior_unresolved;
    }
  if (!finite(result.center_of_mass_metres) || !finite(result.eye_metres) ||
      pair_index != kBoardingBodyPairCount)
    return std::unexpected(BoardingBodyError::numerical_failure);
  return result;
}
} // namespace apsis_drift
