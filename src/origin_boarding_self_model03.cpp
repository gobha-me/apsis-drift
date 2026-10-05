#include "apsis_drift/origin_boarding_self_model03.hpp"
#include "apsis_drift/origin_boarding_self_model04.hpp"
#include "origin_boarding_self_model03_internal.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

namespace apsis_drift {
namespace {
using Vec = RigidVector3;
using Box = BoardingBodyBox;
using Capsule = BoardingBodyCapsule;
using Ellipsoid = BoardingSelfEllipsoid;
using Slab = BoardingSelfAxialSlab03;
using Membership = detail::BoardingSelfMembership;
using Sign = detail::BoardingSelfSign;
using Status = detail::BoardingSelfProofStatus03;
using Proof = detail::BoardingSelfProof03;
using Cert = BoardingSelfCertificate03;
constexpr std::size_t capacity{256};
auto at(Vec p, std::size_t i) -> double {
  return i == 0 ? p.x : i == 1 ? p.y : p.z;
}
auto neg(Vec p) -> Vec {
  return {-p.x, -p.y, -p.z};
}
auto sub(Vec a, Vec b) -> Vec {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto add(Vec a, Vec b) -> Vec {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto scale(Vec a, double k) -> Vec {
  return {a.x * k, a.y * k, a.z * k};
}
auto cross(Vec a, Vec b) -> Vec {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto bounded(Vec p, double limit = 16) -> bool {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
         std::abs(p.x) <= limit && std::abs(p.y) <= limit &&
         std::abs(p.z) <= limit;
}
auto dimension(double x) -> bool {
  return std::isfinite(x) && x >= 1e-6 && x <= 4;
}
struct Sum {
  double high{}, low{};
};
auto two_sum(double a, double b) -> Sum {
  const auto h = a + b, bv = h - a, av = h - bv;
  return {h, (a - av) + (b - bv)};
}
auto product_allowed(double a, double b) -> bool {
  return std::isfinite(a) && std::isfinite(b) &&
         (a == 0 || b == 0 || std::ilogb(a) + std::ilogb(b) >= -970);
}
// Point26 / full affine box98 raw scalar terms. Every product, including
// scaling a residual, is guarded. The existing Model02 exact sum owns signs.
struct Terms {
  std::array<double, capacity> values{};
  std::size_t count{};
  bool supported{true};
  auto append(double x) -> void {
    if (!std::isfinite(x) || count >= values.size()) {
      supported = false;
      return;
    }
    if (x != 0) values[count++] = x;
  }
  auto product(double a, double b, double sign = 1) -> void {
    if (!product_allowed(a, b)) {
      supported = false;
      return;
    }
    if (a == 0 || b == 0) return;
    const auto h = a * b;
    if (!std::isfinite(h)) {
      supported = false;
      return;
    }
    append(sign * std::fma(a, b, -h));
    append(sign * h);
  }
  auto sign() const -> Sign {
    return supported ? detail::boarding_self_model02_linear_sign(
                           std::span<const double>(values.data(), count))
                     : Sign::unsupported;
  }
};
struct Interval {
  double low{}, high{};
  bool supported{true};
};
auto point(double x) -> Interval {
  return {x, x, std::isfinite(x)};
}
auto failed() -> Interval {
  return {0, 0, false};
}
auto down(double x) -> double {
  return std::nextafter(x, -std::numeric_limits<double>::infinity());
}
auto up(double x) -> double {
  return std::nextafter(x, std::numeric_limits<double>::infinity());
}
auto interval(double a, double b) -> Interval {
  return {a, b, std::isfinite(a) && std::isfinite(b) && a <= b};
}
auto zero(Interval a) -> bool {
  return a.supported && a.low == 0 && a.high == 0;
}
auto plus(Interval a, Interval b) -> Interval {
  if (!a.supported || !b.supported) return failed();
  if (zero(a)) return b;
  if (zero(b)) return a;
  return interval(down(a.low + b.low), up(a.high + b.high));
}
auto minus(Interval a) -> Interval {
  return {-a.high, -a.low, a.supported};
}
auto minus(Interval a, Interval b) -> Interval {
  if (a.supported && b.supported && a.low == a.high && b.low == b.high &&
      a.low == b.low)
    return point(0);
  return plus(a, minus(b));
}
auto times(Interval a, Interval b) -> Interval {
  if (!a.supported || !b.supported) return failed();
  if (zero(a) || zero(b)) return point(0);
  if (a.low == 1 && a.high == 1) return b;
  if (b.low == 1 && b.high == 1) return a;
  if (a.low == -1 && a.high == -1) return minus(b);
  if (b.low == -1 && b.high == -1) return minus(a);
  const std::array<double, 4> p{a.low * b.low, a.low * b.high, a.high * b.low,
                                a.high * b.high};
  return interval(down(*std::min_element(p.begin(), p.end())),
                  up(*std::max_element(p.begin(), p.end())));
}
auto square(Interval a) -> Interval {
  if (!a.supported) return failed();
  if (zero(a)) return point(0);
  const auto f = a.low * a.low, l = a.high * a.high;
  return interval(
      a.low <= 0 && a.high >= 0 ? 0 : std::max(0.0, down(std::min(f, l))),
      up(std::max(f, l)));
}
auto divide(Interval a, Interval b) -> Interval {
  if (!a.supported || !b.supported || (b.low <= 0 && b.high >= 0))
    return failed();
  if (b.low == 1 && b.high == 1) return a;
  if (b.low == -1 && b.high == -1) return minus(a);
  return times(a, interval(down(1 / b.high), up(1 / b.low)));
}
auto root(Interval a) -> Interval {
  if (!a.supported || a.low < 0) return failed();
  if (zero(a)) return point(0);
  auto relation = [](double r, double x) {
    Terms t;
    t.product(r, r);
    t.append(-x);
    return t.sign();
  };
  double lo = std::sqrt(a.low), hi = std::sqrt(a.high);
  bool lok{}, hik{};
  for (std::size_t i = 0; i < 4; ++i) {
    const auto s = relation(lo, a.low);
    if (s == Sign::zero || s == Sign::negative) {
      lok = true;
      break;
    }
    if (s == Sign::unsupported) return failed();
    lo = down(lo);
  }
  for (std::size_t i = 0; i < 4; ++i) {
    const auto s = relation(hi, a.high);
    if (s == Sign::zero || s == Sign::positive) {
      hik = true;
      break;
    }
    if (s == Sign::unsupported) return failed();
    hi = up(hi);
  }
  return lok && hik ? interval(lo, hi) : failed();
}
auto absolute(Interval a) -> Interval {
  if (!a.supported || a.low >= 0) return a;
  if (a.high <= 0) return minus(a);
  return {0, std::max(-a.low, a.high), true};
}
using IVec = std::array<Interval, 3>;
auto iv(Vec p) -> IVec {
  return {point(p.x), point(p.y), point(p.z)};
}
auto diff(Vec a, Vec b) -> IVec {
  return {minus(point(a.x), point(b.x)), minus(point(a.y), point(b.y)),
          minus(point(a.z), point(b.z))};
}
auto dot(const IVec& a, const IVec& b) -> Interval {
  return plus(plus(times(a[0], b[0]), times(a[1], b[1])), times(a[2], b[2]));
}
auto norm2(const IVec& a) -> Interval {
  return plus(plus(square(a[0]), square(a[1])), square(a[2]));
}
auto icross(const IVec& a, const IVec& b) -> IVec {
  return {minus(times(a[1], b[2]), times(a[2], b[1])),
          minus(times(a[2], b[0]), times(a[0], b[2])),
          minus(times(a[0], b[1]), times(a[1], b[0]))};
}
auto valid(const BoardingSelfSolid& s) -> bool {
  return detail::boarding_self_model02_support(s, {}).supported;
}
auto valid(const BoardingSelfSphere& s) -> bool {
  return bounded(s.center_metres) && dimension(s.radius_metres);
}
auto same(const Capsule& a, const Capsule& b) -> bool {
  return a.start_metres == b.start_metres && a.end_metres == b.end_metres &&
         a.radius_metres == b.radius_metres;
}
auto exact_norm(Vec a, Vec b) -> std::optional<double> {
  if (!bounded(a) || !bounded(b)) return std::nullopt;
  std::array<double, 3> sq{};
  for (std::size_t i = 0; i < 3; ++i) {
    const auto d = two_sum(at(b, i), -at(a, i));
    if (!std::isfinite(d.high) || d.low != 0 ||
        !product_allowed(d.high, d.high))
      return std::nullopt;
    sq[i] = d.high * d.high;
    if (!std::isfinite(sq[i]) || std::fma(d.high, d.high, -sq[i]) != 0)
      return std::nullopt;
  }
  const auto first = two_sum(sq[0], sq[1]), last = two_sum(first.high, sq[2]);
  if (first.low != 0 || last.low != 0 || !std::isfinite(last.high) ||
      last.high <= 0)
    return std::nullopt;
  const auto n = std::sqrt(last.high);
  if (!std::isfinite(n) || n <= 0 || !product_allowed(n, n))
    return std::nullopt;
  const auto h = n * n;
  if (h != last.high || std::fma(n, n, -h) != 0) return std::nullopt;
  return n;
}
auto slab_validity(const Slab& s) -> Status {
  if (!valid(BoardingSelfSolid{s.original_capsule}) ||
      !bounded(s.root_metres) || !bounded(s.toward_metres) ||
      !dimension(s.axial_limit_metres) ||
      s.original_capsule.radius_metres > s.axial_limit_metres ||
      !((s.root_metres == s.original_capsule.start_metres &&
         s.toward_metres == s.original_capsule.end_metres) ||
        (s.root_metres == s.original_capsule.end_metres &&
         s.toward_metres == s.original_capsule.start_metres)) ||
      s.root_metres == s.toward_metres)
    return Status::invalid_geometry;
  const auto n = root(norm2(diff(s.toward_metres, s.root_metres)));
  if (n.supported && n.low > s.axial_limit_metres) return Status::proved;
  if (const auto exact = exact_norm(s.root_metres, s.toward_metres)) {
    const std::array<double, 2> terms{*exact, -s.axial_limit_metres};
    const auto sign = detail::boarding_self_model02_linear_sign(terms);
    if (sign == Sign::positive) return Status::proved;
    if (sign == Sign::zero || sign == Sign::negative)
      return Status::invalid_geometry;
  }
  if (n.supported && n.high <= s.axial_limit_metres)
    return Status::invalid_geometry;
  return Status::arithmetic_unresolved;
}
auto exact_projection(Vec a, Vec b, Vec c, Vec origin) -> Terms {
  Terms result;
  for (std::size_t i = 0; i < 3; ++i) {
    const auto d = two_sum(at(b, i), -at(a, i)),
               q = two_sum(at(c, i), -at(origin, i));
    for (const auto x : {d.high, d.low})
      for (const auto y : {q.high, q.low})
        result.product(x, y);
  }
  return result;
}
auto exact_plane(const Slab& s, Vec p, double n) -> Sign {
  Terms gap;
  gap.product(s.axial_limit_metres, n);
  const auto q =
      exact_projection(s.root_metres, s.toward_metres, p, s.root_metres);
  gap.supported = gap.supported && q.supported;
  for (std::size_t i = 0; i < q.count; ++i)
    gap.append(-q.values[i]);
  return gap.sign();
}
auto exact_box(const Slab& s, const Box& box, double n) -> Sign {
  Terms gap;
  gap.product(s.axial_limit_metres, n);
  const auto center = exact_projection(s.root_metres, s.toward_metres,
                                       box.center_metres, s.root_metres);
  gap.supported = gap.supported && center.supported;
  for (std::size_t i = 0; i < center.count; ++i)
    gap.append(-center.values[i]);
  for (std::size_t j = 0; j < 3; ++j) {
    const auto projection = exact_projection(s.root_metres, s.toward_metres,
                                             box.frame.columns[j], {});
    const auto sign = projection.sign();
    if (sign == Sign::unsupported) return sign;
    // Absolute WHOLE projection, never the absolute value of each summand.
    const auto factor =
        (sign == Sign::negative ? 1 : -1) * at(box.half_size_metres, j);
    for (std::size_t i = 0; i < projection.count; ++i)
      gap.product(projection.values[i], factor);
  }
  return gap.sign();
}
auto plane_sign(const Slab& s, Vec p) -> Sign {
  if (slab_validity(s) != Status::proved || !bounded(p))
    return Sign::unsupported;
  const auto d = diff(s.toward_metres, s.root_metres);
  const auto gap = minus(times(point(s.axial_limit_metres), root(norm2(d))),
                         dot(d, diff(p, s.root_metres)));
  if (gap.supported && gap.low > 0) return Sign::positive;
  if (gap.supported && gap.high < 0) return Sign::negative;
  if (const auto n = exact_norm(s.root_metres, s.toward_metres))
    return exact_plane(s, p, *n);
  return Sign::unsupported;
}
auto slab_support(const Slab& s, const Box& b)
    -> detail::BoardingSelfSlabSupport03 {
  detail::BoardingSelfSlabSupport03 result;
  const auto v = slab_validity(s);
  if (v != Status::proved || !valid(BoardingSelfSolid{b})) {
    result.proof.status = v == Status::proved ? Status::invalid_geometry : v;
    return result;
  }
  const auto d = diff(s.toward_metres, s.root_metres);
  auto gap = minus(times(point(s.axial_limit_metres), root(norm2(d))),
                   dot(d, diff(b.center_metres, s.root_metres)));
  for (std::size_t j = 0; j < 3; ++j)
    gap = minus(gap, times(point(at(b.half_size_metres, j)),
                           absolute(dot(d, iv(b.frame.columns[j])))));
  result.gap_lower_square_metres = gap.low;
  result.gap_upper_square_metres = gap.high;
  result.interval_supported = gap.supported;
  if (gap.supported && gap.low >= 0)
    result.proof = {Status::proved, Cert::original_axis_box_support};
  else if (const auto n = exact_norm(s.root_metres, s.toward_metres)) {
    const auto sign = exact_box(s, b, *n);
    if (sign != Sign::unsupported) {
      result.exact_specialization_used = true;
      result.proof =
          sign == Sign::negative
              ? Proof{Status::family_inapplicable, Cert::none}
              : Proof{Status::proved, Cert::original_axis_box_support};
    } else
      result.proof.status = Status::arithmetic_unresolved;
  } else
    result.proof.status = !gap.supported || gap.high >= 0
                              ? Status::arithmetic_unresolved
                              : Status::family_inapplicable;
  return result;
}
auto region_member(const BoardingSelfRegion03& r, Vec p) -> Membership {
  return std::visit(
      [&](const auto& shape) -> Membership {
        using Shape = std::decay_t<decltype(shape)>;
        if constexpr (!std::is_same_v<Shape, Slab>)
          return detail::boarding_self_model02_region_membership(
              BoardingSelfRegion{shape}, p);
        else {
          if (!bounded(p)) return Membership::invalid_geometry;
          const auto status = slab_validity(shape);
          if (status != Status::proved)
            return status == Status::invalid_geometry
                       ? Membership::invalid_geometry
                       : Membership::unresolved;
          const auto member = detail::boarding_self_model02_membership(
              BoardingSelfSolid{shape.original_capsule}, p);
          const auto plane = plane_sign(shape, p);
          if (member == Membership::outside || plane == Sign::negative)
            return Membership::outside;
          if (member == Membership::invalid_geometry) return member;
          if (member == Membership::unresolved || plane == Sign::unsupported)
            return Membership::unresolved;
          return member == Membership::boundary || plane == Sign::zero
                     ? Membership::boundary
                     : Membership::interior;
        }
      },
      r);
}
auto shoulder(const Capsule& upper, const Ellipsoid& trunk,
              const BoardingSelfSphere& sphere) -> Proof {
  if (!valid(BoardingSelfSolid{upper}) || !valid(BoardingSelfSolid{trunk}) ||
      !valid(sphere))
    return {Status::invalid_geometry, Cert::none};
  if (sphere.center_metres != upper.start_metres ||
      sphere.radius_metres < upper.radius_metres ||
      upper.start_metres == upper.end_metres)
    return {};
  const auto d = diff(upper.end_metres, upper.start_metres);
  const auto length = root(norm2(d));
  const auto threshold = sphere.radius_metres == upper.radius_metres
                             ? point(0)
                             : root(minus(square(point(sphere.radius_metres)),
                                          square(point(upper.radius_metres))));
  if (!length.supported || length.low <= 0 || !threshold.supported)
    return {Status::arithmetic_unresolved, Cert::none};
  const auto base = cross(trunk.frame.columns[0], trunk.frame.columns[1]);
  if (base == Vec{} || !bounded(base, 64))
    return {Status::arithmetic_unresolved, Cert::none};
  bool arithmetic{};
  for (const auto n : {base, neg(base)}) {
    const auto v = divide(dot(iv(n), d), length);
    if (!v.supported) {
      arithmetic = true;
      continue;
    }
    if (v.high >= 0) continue;
    const auto perp = divide(root(norm2(icross(iv(n), d))), length);
    const auto cylinder =
        plus(plus(dot(iv(n), iv(upper.start_metres)), times(threshold, v)),
             times(point(upper.radius_metres), perp));
    const auto distal =
        plus(dot(iv(n), iv(upper.end_metres)),
             times(point(upper.radius_metres), root(norm2(iv(n)))));
    const auto reverse =
        detail::boarding_self_model02_support(BoardingSelfSolid{trunk}, neg(n));
    if (!cylinder.supported || !distal.supported || !reverse.supported) {
      arithmetic = true;
      continue;
    }
    const auto minimum = -reverse.upper;
    if (cylinder.high < minimum && distal.high < minimum)
      return {Status::proved, Cert::shoulder_cofactor_split};
  }
  return {arithmetic ? Status::arithmetic_unresolved
                     : Status::family_inapplicable,
          Cert::none};
}
auto center(const BoardingSelfSolid& s) -> Vec {
  return std::visit(
      [](const auto& q) -> Vec {
        if constexpr (std::is_same_v<std::decay_t<decltype(q)>, Capsule>)
          return add(q.start_metres,
                     scale(sub(q.end_metres, q.start_metres), .5));
        else
          return q.center_metres;
      },
      s);
}
auto separated(const BoardingSelfSolid& a, const BoardingSelfSolid& b,
               bool& arithmetic) -> bool {
  std::array<Vec, 14> normals{};
  std::size_t count{};
  bool room = true;
  auto push = [&](Vec n) {
    if (count < normals.size())
      normals[count++] = n;
    else
      room = false;
  };
  push({1, 0, 0});
  push({0, 1, 0});
  push({0, 0, 1});
  push(sub(center(b), center(a)));
  for (const auto* s : {&a, &b})
    std::visit(
        [&](const auto& q) {
          if constexpr (!std::is_same_v<std::decay_t<decltype(q)>, Capsule>)
            for (std::size_t i = 0; i < 3; ++i)
              push(cross(q.frame.columns[(i + 1) % 3],
                         q.frame.columns[(i + 2) % 3]));
        },
        *s);
  for (const auto* s : {&a, &b})
    std::visit(
        [&](const auto& q) {
          if constexpr (std::is_same_v<std::decay_t<decltype(q)>, Capsule>) {
            const auto other = s == &a ? center(b) : center(a);
            push(sub(q.start_metres, other));
            push(sub(q.end_metres, other));
          }
        },
        *s);
  if (!room) {
    arithmetic = true;
    return false;
  }
  for (std::size_t i = 0; i < count; ++i)
    for (const auto n : {normals[i], neg(normals[i])}) {
      if (n == Vec{} || !bounded(n, 64)) continue;
      const auto first = detail::boarding_self_model02_support(a, n),
                 second = detail::boarding_self_model02_support(b, neg(n));
      if (!first.supported || !second.supported) {
        arithmetic = true;
        continue;
      }
      if (minus(point(-second.upper), point(first.upper)).low >= 0) return true;
    }
  return false;
}
auto expected_junction(BoardingBodyPartId a, BoardingBodyPartId b)
    -> std::optional<BoardingSelfJunction> {
  using Id = BoardingBodyPartId;
  using J = BoardingSelfJunction;
  if (a == Id::pelvis && b == Id::trunk) return J::waist;
  if (a == Id::trunk && b == Id::helmet) return J::neck;
  for (const auto base : {3U, 9U}) {
    if (a == Id::pelvis && b == static_cast<Id>(base)) return J::hip;
    if (a == static_cast<Id>(base) && b == static_cast<Id>(base + 1))
      return J::knee;
    if (a == static_cast<Id>(base + 1) && b == static_cast<Id>(base + 2))
      return J::ankle;
    if (a == Id::trunk && b == static_cast<Id>(base + 3)) return J::shoulder;
    if (a == static_cast<Id>(base + 3) && b == static_cast<Id>(base + 4))
      return J::elbow;
    if (a == static_cast<Id>(base + 4) && b == static_cast<Id>(base + 5))
      return J::wrist;
  }
  return std::nullopt;
}
auto old_region(const BoardingSelfConnectedRegion03& r)
    -> std::optional<BoardingSelfConnectedRegion> {
  return std::visit(
      [&](const auto& shape) -> std::optional<BoardingSelfConnectedRegion> {
        if constexpr (std::is_same_v<std::decay_t<decltype(shape)>, Slab>)
          return std::nullopt;
        else
          return BoardingSelfConnectedRegion{r.first, r.second, r.junction,
                                             BoardingSelfRegion{shape}};
      },
      r.region);
}
auto certificate(BoardingSelfCertificate c) -> Cert {
  switch (c) {
    case BoardingSelfCertificate::convex_support_plane:
      return Cert::convex_support_plane;
    case BoardingSelfCertificate::cap_partner_support:
      return Cert::cap_partner_support;
    case BoardingSelfCertificate::opposite_ray_endpoint_ball:
      return Cert::opposite_ray_endpoint_ball;
    case BoardingSelfCertificate::half_ray_angle_bound:
      return Cert::half_ray_angle_bound;
    case BoardingSelfCertificate::verified_strict_witness:
      return Cert::verified_strict_witness;
    default: return Cert::none;
  }
}
auto pair(const BoardingSelfPart& a, const BoardingSelfPart& b,
          const std::optional<BoardingSelfConnectedRegion03>& r)
    -> BoardingSelfPairDiagnostic03 {
  BoardingSelfPairDiagnostic03 result;
  result.first = a.id;
  result.second = b.id;
  const auto relation = expected_junction(a.id, b.id);
  if (static_cast<std::size_t>(a.id) >= kBoardingBodyPartCount ||
      static_cast<std::size_t>(b.id) >= kBoardingBodyPartCount ||
      a.id >= b.id || !valid(a.solid) || !valid(b.solid) ||
      (r && (!relation || r->first != a.id || r->second != b.id ||
             r->junction != *relation)) ||
      (!r && relation)) {
    result.reason = BoardingSelfUnresolved::invalid_geometry;
    return result;
  }
  if (r && (r->junction == BoardingSelfJunction::waist ||
            r->junction == BoardingSelfJunction::neck)) {
    const auto* cap = std::get_if<BoardingSelfEllipsoidCap>(&r->region);
    const auto* trunk = std::get_if<Ellipsoid>(
        r->junction == BoardingSelfJunction::waist ? &b.solid : &a.solid);
    const auto* bounded_ellipse = std::get_if<Ellipsoid>(&b.solid);
    const auto* pelvis = std::get_if<Box>(&a.solid);
    const auto same_ellipse = [](const Ellipsoid& x, const Ellipsoid& y) {
      return x.center_metres == y.center_metres &&
             x.semi_axes_metres == y.semi_axes_metres &&
             x.frame.columns == y.frame.columns;
    };
    const bool waist = r->junction == BoardingSelfJunction::waist;
    if (!cap || !trunk || !bounded_ellipse ||
        !same_ellipse(cap->ellipsoid, *bounded_ellipse) ||
        cap->axial_frame.columns != trunk->frame.columns ||
        (waist ? (!pelvis || cap->axial_origin_metres != pelvis->center_metres)
               : (cap->axial_origin_metres != trunk->center_metres ||
                  cap->axial_limit_metres != trunk->semi_axes_metres.y))) {
      result.reason = BoardingSelfUnresolved::invalid_geometry;
      return result;
    }
  }
  if (r && (r->junction == BoardingSelfJunction::knee ||
            r->junction == BoardingSelfJunction::elbow)) {
    const auto* sphere = std::get_if<BoardingSelfSphere>(&r->region);
    const auto* first = std::get_if<Capsule>(&a.solid);
    const auto* second = std::get_if<Capsule>(&b.solid);
    bool shared{};
    if (first && second && sphere)
      for (const auto p : {first->start_metres, first->end_metres})
        if (p == sphere->center_metres &&
            (p == second->start_metres || p == second->end_metres))
          shared = true;
    if (!sphere || !valid(*sphere) || !shared) {
      result.reason = BoardingSelfUnresolved::invalid_geometry;
      return result;
    }
  }
  if (!r || (r->junction != BoardingSelfJunction::hip &&
             r->junction != BoardingSelfJunction::ankle &&
             r->junction != BoardingSelfJunction::wrist &&
             r->junction != BoardingSelfJunction::shoulder)) {
    const auto converted =
        r ? old_region(*r) : std::optional<BoardingSelfConnectedRegion>{};
    if (r && !converted) {
      result.reason = BoardingSelfUnresolved::invalid_geometry;
      return result;
    }
    const auto old = detail::boarding_self_model02_pair(a, b, converted);
    result.outcome = old.outcome;
    result.reason = old.reason;
    result.certificate = certificate(old.certificate);
    result.strict_witness_metres = old.strict_witness_metres;
    return result;
  }
  const auto* first = std::get_if<Capsule>(&a.solid);
  const auto* second = std::get_if<Capsule>(&b.solid);
  const auto* capsule = first ? first : second;
  const auto* box = std::get_if<Box>(first ? &b.solid : &a.solid);
  const auto* slab = std::get_if<Slab>(&r->region);
  const auto* sphere = std::get_if<BoardingSelfSphere>(&r->region);
  if (r->junction == BoardingSelfJunction::shoulder) {
    if (!sphere || !valid(*sphere) || !second ||
        !std::holds_alternative<Ellipsoid>(a.solid) ||
        sphere->center_metres != second->start_metres) {
      result.reason = BoardingSelfUnresolved::invalid_geometry;
      return result;
    }
  } else {
    if (!slab || !capsule || !box || !same(slab->original_capsule, *capsule) ||
        slab->root_metres != (r->junction == BoardingSelfJunction::hip
                                  ? capsule->start_metres
                                  : capsule->end_metres) ||
        slab->toward_metres != (r->junction == BoardingSelfJunction::hip
                                    ? capsule->end_metres
                                    : capsule->start_metres)) {
      result.reason = BoardingSelfUnresolved::invalid_geometry;
      return result;
    }
    const auto status = slab_validity(*slab);
    if (status != Status::proved) {
      result.reason = status == Status::invalid_geometry
                          ? BoardingSelfUnresolved::invalid_geometry
                          : BoardingSelfUnresolved::arithmetic_unresolved;
      return result;
    }
  }
  std::array<Vec, 6> points{};
  std::size_t count{};
  points[count++] = center(a.solid);
  points[count++] = center(b.solid);
  for (const auto* s : {&a.solid, &b.solid})
    if (const auto* c = std::get_if<Capsule>(s)) {
      points[count++] = c->start_metres;
      points[count++] = c->end_metres;
    }
  for (std::size_t i = 0; i < count; ++i)
    if (detail::boarding_self_model02_membership(a.solid, points[i]) ==
            Membership::interior &&
        detail::boarding_self_model02_membership(b.solid, points[i]) ==
            Membership::interior &&
        region_member(r->region, points[i]) == Membership::outside) {
      result.outcome =
          BoardingSelfPairOutcome::strict_unowned_connected_interior;
      result.certificate = Cert::verified_strict_witness;
      result.strict_witness_metres = points[i];
      return result;
    }
  const auto proof =
      slab ? slab_support(*slab, *box).proof
           : shoulder(*second, std::get<Ellipsoid>(a.solid), *sphere);
  if (proof.status == Status::proved) {
    result.outcome = BoardingSelfPairOutcome::certified_whole_owned;
    result.certificate = proof.certificate;
    return result;
  }
  if (proof.status == Status::invalid_geometry) {
    result.reason = BoardingSelfUnresolved::invalid_geometry;
    return result;
  }
  bool arithmetic = proof.status == Status::arithmetic_unresolved;
  if (separated(a.solid, b.solid, arithmetic)) {
    result.outcome = BoardingSelfPairOutcome::certified_no_interior;
    result.certificate = Cert::convex_support_plane;
    return result;
  }
  result.reason = arithmetic ? BoardingSelfUnresolved::arithmetic_unresolved
                             : BoardingSelfUnresolved::unsupported_family;
  return result;
}
} // namespace
namespace detail {
auto boarding_self_model03_region_membership(const BoardingSelfRegion03& r,
                                             Vec p) -> Membership {
  return region_member(r, p);
}
auto boarding_self_model03_slab_box_support(const Slab& s, const Box& b)
    -> BoardingSelfSlabSupport03 {
  return slab_support(s, b);
}
auto boarding_self_model03_shoulder(const Capsule& c, const Ellipsoid& e,
                                    const BoardingSelfSphere& s) -> Proof {
  return shoulder(c, e, s);
}
auto boarding_self_model03_pair(
    const BoardingSelfPart& a, const BoardingSelfPart& b,
    const std::optional<BoardingSelfConnectedRegion03>& r)
    -> BoardingSelfPairDiagnostic03 {
  return pair(a, b, r);
}
auto boarding_self_model03_exact_norm(Vec a, Vec b) -> std::optional<double> {
  return exact_norm(a, b);
}
auto boarding_self_model03_axial_plane_sign(const Slab& s, Vec p) -> Sign {
  return plane_sign(s, p);
}
} // namespace detail

namespace {
// Only the two named pose evaluators reach this purpose-specific assembly.
// All finite region definitions and pair predicates remain identical.
template <typename Diagnostic>
auto build_self_recipe(const BoardingBodyDiagnostic& body)
    -> std::expected<Diagnostic, BoardingBodyError> {
  const auto* canonical = &body;
  Diagnostic result;
  result.canonical = *canonical;
  for (std::size_t i = 0; i < result.self_parts.size(); ++i) {
    const auto& part = canonical->parts[i];
    result.self_parts[i].id = part.id;
    result.self_parts[i].solid = std::visit(
        [&](const auto& q) -> BoardingSelfSolid {
          if constexpr (std::is_same_v<std::decay_t<decltype(q)>, Box>)
            if (part.id == BoardingBodyPartId::trunk ||
                part.id == BoardingBodyPartId::helmet)
              return Ellipsoid{q.center_metres, q.half_size_metres, q.frame};
          return q;
        },
        part.world_reservation);
  }
  using Id = BoardingBodyPartId;
  using J = BoardingSelfJunction;
  const auto& trunk = std::get<Ellipsoid>(result.self_parts[1].solid);
  const auto& helmet = std::get<Ellipsoid>(result.self_parts[2].solid);
  const auto hip =
      std::get<Box>(canonical->parts[0].world_reservation).center_metres;
  result.regions[0] = {Id::pelvis, Id::trunk, J::waist,
                       BoardingSelfEllipsoidCap{trunk, hip, trunk.frame,
                                                kBoardingSelfWaistLimitMetres}};
  result.regions[1] = {Id::trunk, Id::helmet, J::neck,
                       BoardingSelfEllipsoidCap{helmet, trunk.center_metres,
                                                trunk.frame,
                                                trunk.semi_axes_metres.y}};
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& joint = canonical->joints[side];
    const auto base = 3 + side * 6, index = 2 + side * 6;
    const auto thigh = static_cast<Id>(base), shin = static_cast<Id>(base + 1),
               boot = static_cast<Id>(base + 2),
               upper = static_cast<Id>(base + 3),
               fore = static_cast<Id>(base + 4),
               hand = static_cast<Id>(base + 5);
    result.regions[index] = {
        Id::pelvis, thigh, J::hip,
        Slab{std::get<Capsule>(result.self_parts[base].solid), joint.hip_metres,
             joint.knee_metres, kBoardingSelfHipLengthMetres}};
    result.regions[index + 1] = {
        thigh, shin, J::knee,
        BoardingSelfSphere{joint.knee_metres, kBoardingSelfKneeRadiusMetres}};
    result.regions[index + 2] = {
        shin, boot, J::ankle,
        Slab{std::get<Capsule>(result.self_parts[base + 1].solid),
             joint.ankle_metres, joint.knee_metres,
             kBoardingSelfAnkleLengthMetres}};
    result.regions[index + 3] = {
        Id::trunk, upper, J::shoulder,
        BoardingSelfSphere{joint.shoulder_metres,
                           kBoardingSelfShoulderRadiusMetres}};
    result.regions[index + 4] = {
        upper, fore, J::elbow,
        BoardingSelfSphere{joint.elbow_metres, kBoardingSelfElbowRadiusMetres}};
    result.regions[index + 5] = {
        fore, hand, J::wrist,
        Slab{std::get<Capsule>(result.self_parts[base + 4].solid),
             joint.wrist_metres, joint.elbow_metres,
             kBoardingSelfWristLengthMetres}};
  }
  std::sort(result.regions.begin(), result.regions.end(),
            [](const auto& a, const auto& b) {
              return a.first == b.first ? a.second < b.second
                                        : a.first < b.first;
            });
  result.self_checkpoint_passed = true;
  std::size_t index{};
  for (std::size_t a = 0; a < result.self_parts.size(); ++a)
    for (std::size_t b = a + 1; b < result.self_parts.size(); ++b) {
      std::optional<BoardingSelfConnectedRegion03> r;
      std::optional<std::size_t> ri;
      for (std::size_t i = 0; i < result.regions.size(); ++i)
        if (result.regions[i].first == result.self_parts[a].id &&
            result.regions[i].second == result.self_parts[b].id) {
          r = result.regions[i];
          ri = i;
          break;
        }
      auto d = detail::boarding_self_model03_pair(result.self_parts[a],
                                                  result.self_parts[b], r);
      d.connected_region_index = ri;
      result.strict_conflict =
          result.strict_conflict ||
          d.outcome == BoardingSelfPairOutcome::strict_nonadjacent_interior ||
          d.outcome ==
              BoardingSelfPairOutcome::strict_unowned_connected_interior;
      result.unresolved_pair = result.unresolved_pair ||
                               d.outcome == BoardingSelfPairOutcome::unresolved;
      result.self_checkpoint_passed =
          result.self_checkpoint_passed &&
          (d.outcome == BoardingSelfPairOutcome::certified_no_interior ||
           (r && d.outcome == BoardingSelfPairOutcome::certified_whole_owned));
      result.pairs[index++] = d;
    }
  if (index != kBoardingBodyPairCount)
    return std::unexpected(BoardingBodyError::numerical_failure);
  return result;
}
} // namespace

auto evaluate_origin_boarding_self_model03(const BoardingBodyPose& pose)
    -> std::expected<BoardingSelfDiagnostic03, BoardingBodyError> {
  auto canonical = evaluate_origin_boarding_body(pose);
  if (!canonical) return std::unexpected(canonical.error());
  return build_self_recipe<BoardingSelfDiagnostic03>(*canonical);
}
auto evaluate_origin_boarding_self_model04(const BoardingBodyPose& pose)
    -> std::expected<BoardingSelfDiagnostic04, BoardingBodyError> {
  auto canonical = evaluate_origin_boarding_body02(pose);
  if (!canonical) return std::unexpected(canonical.error());
  return build_self_recipe<BoardingSelfDiagnostic04>(*canonical);
}
} // namespace apsis_drift
