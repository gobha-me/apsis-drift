#include "apsis_drift/origin_boarding_self_model02.hpp"
#include "origin_boarding_self_model02_internal.hpp"
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
using Membership = detail::BoardingSelfMembership;
using Sign = detail::BoardingSelfSign;
constexpr double private_bound{16};
constexpr double private_minimum_dimension{1e-6};
constexpr std::size_t expansion_capacity{256};
auto component(Vec a, std::size_t i) -> double {
  return i == 0 ? a.x : i == 1 ? a.y : a.z;
}
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
auto finite(Vec a) -> bool {
  return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
auto bounded(Vec a, double bound = private_bound) -> bool {
  return finite(a) && std::abs(a.x) <= bound && std::abs(a.y) <= bound &&
         std::abs(a.z) <= bound;
}
auto dimension(double a) -> bool {
  return std::isfinite(a) && a >= private_minimum_dimension && a <= 4;
}
auto identity(const BoardingBodyFrame& f) -> bool {
  return f.columns == BoardingBodyFrame{}.columns;
}
struct Sum {
  double high{}, low{};
};
auto two_sum(double a, double b) -> Sum {
  const auto high = a + b, bv = high - a, av = high - bv;
  return {high, (a - av) + (b - bv)};
}
// Raw term bounds: cofactor component4, determinant24, n.(C-P)48,
// D*(limit-halfY)96 => neck144. Waist: extents144+Dlimit48,
// plus displaced private center48 =>240. Fixed256 also bounds all smaller
// linear, cross and quadratic expressions. Checked capacity is not a tolerance.
class Expansion {
 public:
  auto append(double scalar) -> void {
    if (!std::isfinite(scalar) || count_ >= values_.size()) {
      supported_ = false;
      return;
    }
    if (scalar == 0) return;
    std::array<double, expansion_capacity> next{};
    std::size_t n{};
    auto carry = scalar;
    for (std::size_t i = 0; i < count_; ++i) {
      const auto s = two_sum(carry, values_[i]);
      if (!std::isfinite(s.high) || !std::isfinite(s.low)) {
        supported_ = false;
        return;
      }
      if (s.low != 0) next[n++] = s.low;
      carry = s.high;
    }
    if (carry != 0) next[n++] = carry;
    values_ = next;
    count_ = n;
  }
  auto product(double a, double b, double sign = 1) -> void {
    if (!std::isfinite(a) || !std::isfinite(b)) {
      supported_ = false;
      return;
    }
    if (a == 0 || b == 0) return;
    if (std::ilogb(a) + std::ilogb(b) < -970) {
      supported_ = false;
      return;
    }
    const auto high = a * b;
    if (!std::isfinite(high)) {
      supported_ = false;
      return;
    }
    append(sign * std::fma(a, b, -high));
    append(sign * high);
  }
  auto include(const Expansion& b, double sign = 1) -> void {
    supported_ = supported_ && b.supported_;
    for (std::size_t i = 0; i < b.count_; ++i)
      append(sign * b.values_[i]);
  }
  auto scaled(const Expansion& b, double factor) -> void {
    supported_ = supported_ && b.supported_;
    for (std::size_t i = 0; i < b.count_; ++i)
      product(b.values_[i], factor);
  }
  auto product_expansions(const Expansion& a, const Expansion& b,
                          double sign = 1) -> void {
    supported_ = supported_ && a.supported_ && b.supported_;
    // This is used only for the bounded endpoint-difference quadratic/cross
    // predicates; callers never nest a general polynomial product service.
    if (a.count_ > 2 || b.count_ > 2) {
      supported_ = false;
      return;
    }
    for (std::size_t i = 0; i < a.count_; ++i)
      for (std::size_t j = 0; j < b.count_; ++j)
        product(a.values_[i], b.values_[j], sign);
  }
  [[nodiscard]] auto sign() const -> Sign {
    if (!supported_) return Sign::unsupported;
    if (count_ == 0) return Sign::zero;
    return values_[count_ - 1] < 0 ? Sign::negative : Sign::positive;
  }

 private:
  std::array<double, expansion_capacity> values_{};
  std::size_t count_{};
  bool supported_{true};
};
auto difference(double a, double b) -> Expansion {
  const auto d = two_sum(a, -b);
  Expansion e;
  e.append(d.low);
  e.append(d.high);
  return e;
}
auto squared_difference(Vec a, Vec b, double radius) -> Sign {
  Expansion result;
  for (std::size_t i = 0; i < 3; ++i) {
    const auto d = difference(component(a, i), component(b, i));
    result.product_expansions(d, d);
  }
  result.product(radius, radius, -1);
  return result.sign();
}
auto exact_dot_difference(Vec a, Vec b, Vec c, Vec d) -> Expansion {
  Expansion result;
  for (std::size_t i = 0; i < 3; ++i)
    result.product_expansions(difference(component(a, i), component(b, i)),
                              difference(component(c, i), component(d, i)));
  return result;
}
auto actual_collinear(Vec a, Vec b, Vec c, Vec d) -> bool {
  for (std::size_t i = 0; i < 3; ++i) {
    const auto j = (i + 1) % 3, k = (i + 2) % 3;
    Expansion e;
    e.product_expansions(difference(component(a, j), component(b, j)),
                         difference(component(c, k), component(d, k)));
    e.product_expansions(difference(component(a, k), component(b, k)),
                         difference(component(c, j), component(d, j)), -1);
    if (e.sign() != Sign::zero) return false;
  }
  return true;
}
struct Interval {
  double low{}, high{};
  bool supported{true};
};
auto point(double a) -> Interval {
  return {a, a, std::isfinite(a)};
}
auto fail_interval() -> Interval {
  return {0, 0, false};
}
auto down(double x) -> double {
  return std::nextafter(x, -std::numeric_limits<double>::infinity());
}
auto up(double x) -> double {
  return std::nextafter(x, std::numeric_limits<double>::infinity());
}
auto interval(double low, double high) -> Interval {
  return {low, high, std::isfinite(low) && std::isfinite(high) && low <= high};
}
auto zero(Interval a) -> bool {
  return a.supported && a.low == 0 && a.high == 0;
}
auto plus(Interval a, Interval b) -> Interval {
  if (!a.supported || !b.supported) return fail_interval();
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
  if (!a.supported || !b.supported) return fail_interval();
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
  if (!a.supported) return fail_interval();
  if (zero(a)) return point(0);
  const auto first = a.low * a.low, last = a.high * a.high;
  // Squaring a range spanning zero has mathematical minimum zero. This is
  // an arithmetic range property, never a geometry or penetration clamp.
  const auto low = a.low <= 0 && a.high >= 0
                       ? 0
                       : std::max(0.0, down(std::min(first, last)));
  return interval(low, up(std::max(first, last)));
}
auto divide(Interval a, Interval b) -> Interval {
  if (!a.supported || !b.supported || (b.low <= 0 && b.high >= 0))
    return fail_interval();
  if (b.low == 1 && b.high == 1) return a;
  if (b.low == -1 && b.high == -1) return minus(a);
  return times(a, interval(down(1 / b.high), up(1 / b.low)));
}
auto root(Interval a) -> Interval {
  if (!a.supported || a.low < 0) return fail_interval();
  if (zero(a)) return point(0);
  double low = std::sqrt(a.low), high = std::sqrt(a.high);
  auto relation = [](double r, double value) {
    Expansion e;
    e.product(r, r);
    e.append(-value);
    return e.sign();
  };
  bool low_ok{}, high_ok{};
  for (std::size_t i = 0; i < 4; ++i) {
    const auto s = relation(low, a.low);
    if (s == Sign::zero || s == Sign::negative) {
      low_ok = true;
      break;
    }
    if (s == Sign::unsupported) return fail_interval();
    low = down(low);
  }
  for (std::size_t i = 0; i < 4; ++i) {
    const auto s = relation(high, a.high);
    if (s == Sign::zero || s == Sign::positive) {
      high_ok = true;
      break;
    }
    if (s == Sign::unsupported) return fail_interval();
    high = up(high);
  }
  return low_ok && high_ok ? interval(low, high) : fail_interval();
}
using IVec = std::array<Interval, 3>;
auto iv(Vec p) -> IVec {
  return {point(p.x), point(p.y), point(p.z)};
}
auto idifference(Vec a, Vec b) -> IVec {
  return {minus(point(a.x), point(b.x)), minus(point(a.y), point(b.y)),
          minus(point(a.z), point(b.z))};
}
auto idot(const IVec& a, const IVec& b) -> Interval {
  return plus(plus(times(a[0], b[0]), times(a[1], b[1])), times(a[2], b[2]));
}
auto inorm_squared(const IVec& a) -> Interval {
  return plus(plus(square(a[0]), square(a[1])), square(a[2]));
}
auto icross(const IVec& a, const IVec& b) -> IVec {
  return {minus(times(a[1], b[2]), times(a[2], b[1])),
          minus(times(a[2], b[0]), times(a[0], b[2])),
          minus(times(a[0], b[1]), times(a[1], b[0]))};
}
auto absolute(Interval a) -> Interval {
  if (!a.supported) return a;
  if (a.low >= 0) return a;
  if (a.high <= 0) return minus(a);
  return {0, std::max(-a.low, a.high), true};
}
struct Inverse {
  std::array<IVec, 3> rows{};
  Interval determinant;
  bool supported{};
};
auto inverse(const BoardingBodyFrame& f) -> Inverse {
  // Same near-rotation numerical domain as Model01; actual accepted columns
  // still define an affine solid, not an ideal rotation used in membership.
  constexpr auto rounding = 32 * std::numeric_limits<double>::epsilon();
  for (std::size_t i = 0; i < 3; ++i) {
    if (!finite(f.columns[i]) ||
        std::abs(dot(f.columns[i], f.columns[i]) - 1) > rounding)
      return {};
    for (std::size_t j = i + 1; j < 3; ++j)
      if (std::abs(dot(f.columns[i], f.columns[j])) > rounding) return {};
  }
  Inverse result;
  if (identity(f)) {
    for (std::size_t i = 0; i < 3; ++i)
      result.rows[i] = iv(f.columns[i]);
    result.determinant = point(1);
    result.supported = true;
    return result;
  }
  for (std::size_t i = 0; i < 3; ++i)
    result.rows[i] =
        icross(iv(f.columns[(i + 1) % 3]), iv(f.columns[(i + 2) % 3]));
  result.determinant = idot(iv(f.columns[0]), result.rows[0]);
  if (!result.determinant.supported || result.determinant.low <= 0) return {};
  for (auto& row : result.rows)
    for (auto& value : row) {
      value = divide(value, result.determinant);
      if (!value.supported) return {};
    }
  result.supported = true;
  return result;
}
auto valid(const Box& b) -> bool {
  return bounded(b.center_metres) && dimension(b.half_size_metres.x) &&
         dimension(b.half_size_metres.y) && dimension(b.half_size_metres.z) &&
         inverse(b.frame).supported;
}
auto valid(const Ellipsoid& b) -> bool {
  return valid(Box{b.center_metres, b.semi_axes_metres, b.frame});
}
auto valid(const Capsule& c) -> bool {
  return bounded(c.start_metres) && bounded(c.end_metres) &&
         dimension(c.radius_metres);
}
auto valid(const BoardingSelfSphere& s) -> bool {
  return bounded(s.center_metres) && dimension(s.radius_metres);
}
auto valid(const BoardingSelfEllipsoidCap& c) -> bool {
  return valid(c.ellipsoid) && bounded(c.axial_origin_metres) &&
         std::isfinite(c.axial_limit_metres) &&
         std::abs(c.axial_limit_metres) <= 4 &&
         inverse(c.axial_frame).supported;
}
auto valid(const BoardingSelfSolid& s) -> bool {
  return std::visit([](const auto& q) { return valid(q); }, s);
}
auto membership_from_sign(Sign s) -> Membership {
  return s == Sign::negative   ? Membership::interior
         : s == Sign::positive ? Membership::outside
         : s == Sign::zero     ? Membership::boundary
                               : Membership::unresolved;
}
auto sphere_member(Vec center, double radius, Vec p) -> Membership {
  return membership_from_sign(squared_difference(p, center, radius));
}
auto capsule_member(const Capsule& c, Vec p) -> Membership {
  if (!valid(c) || !bounded(p)) return Membership::invalid_geometry;
  if (c.start_metres == c.end_metres)
    return sphere_member(c.start_metres, c.radius_metres, p);
  const auto projection =
      exact_dot_difference(p, c.start_metres, c.end_metres, c.start_metres);
  if (projection.sign() == Sign::negative || projection.sign() == Sign::zero)
    return sphere_member(c.start_metres, c.radius_metres, p);
  Expansion end_projection;
  end_projection.include(projection);
  end_projection.include(exact_dot_difference(c.end_metres, c.start_metres,
                                              c.end_metres, c.start_metres),
                         -1);
  if (end_projection.sign() == Sign::positive ||
      end_projection.sign() == Sign::zero)
    return sphere_member(c.end_metres, c.radius_metres, p);
  const auto d = idifference(c.end_metres, c.start_metres),
             r = idifference(p, c.start_metres);
  const auto t = divide(idot(r, d), inorm_squared(d));
  if (!t.supported || t.low < 0 || t.high > 1) return Membership::unresolved;
  IVec residual;
  for (std::size_t i = 0; i < 3; ++i)
    residual[i] = minus(r[i], times(t, d[i]));
  const auto distance = inorm_squared(residual),
             rr = square(point(c.radius_metres));
  if (!distance.supported || !rr.supported) return Membership::unresolved;
  if (distance.high < rr.low) return Membership::interior;
  if (distance.low > rr.high) return Membership::outside;
  // For exactly axis-aligned capsules the two transverse differences give
  // an exact quadratic sign, avoiding any nearest-parameter rounding.
  const auto axis = sub(c.end_metres, c.start_metres);
  std::size_t nonzero{}, axis_index{};
  for (std::size_t i = 0; i < 3; ++i)
    if (component(axis, i) != 0) {
      ++nonzero;
      axis_index = i;
    }
  if (nonzero == 1 && projection.sign() == Sign::positive &&
      end_projection.sign() == Sign::negative) {
    Expansion e;
    for (std::size_t i = 0; i < 3; ++i)
      if (i != axis_index) {
        const auto delta =
            difference(component(p, i), component(c.start_metres, i));
        e.product_expansions(delta, delta);
      }
    e.product(c.radius_metres, c.radius_metres, -1);
    return membership_from_sign(e.sign());
  }
  return Membership::unresolved;
}
auto box_member(const Box& b, Vec p) -> Membership {
  if (!valid(b) || !bounded(p)) return Membership::invalid_geometry;
  const auto inv = inverse(b.frame);
  const auto r = idifference(p, b.center_metres);
  bool boundary{}, unresolved{};
  for (std::size_t i = 0; i < 3; ++i) {
    if (identity(b.frame)) {
      const auto h = component(b.half_size_metres, i), v = component(p, i),
                 c = component(b.center_metres, i);
      Expansion top, bottom;
      top.append(v);
      top.append(-c);
      top.append(-h);
      bottom.append(-v);
      bottom.append(c);
      bottom.append(-h);
      if (top.sign() == Sign::positive || bottom.sign() == Sign::positive)
        return Membership::outside;
      if (top.sign() == Sign::unsupported || bottom.sign() == Sign::unsupported)
        unresolved = true;
      boundary =
          boundary || top.sign() == Sign::zero || bottom.sign() == Sign::zero;
    } else {
      const auto q = absolute(idot(inv.rows[i], r));
      const auto h = component(b.half_size_metres, i);
      if (!q.supported)
        unresolved = true;
      else if (q.low > h)
        return Membership::outside;
      else if (q.high >= h)
        unresolved = true;
    }
  }
  return unresolved ? Membership::unresolved
         : boundary ? Membership::boundary
                    : Membership::interior;
}
auto ellipsoid_member(const Ellipsoid& b, Vec p) -> Membership {
  if (!valid(b) || !bounded(p)) return Membership::invalid_geometry;
  if (identity(b.frame)) {
    if (b.semi_axes_metres.x == b.semi_axes_metres.y &&
        b.semi_axes_metres.y == b.semi_axes_metres.z)
      return sphere_member(b.center_metres, b.semi_axes_metres.x, p);
    std::size_t nonzero{}, axis{};
    for (std::size_t i = 0; i < 3; ++i)
      if (component(p, i) != component(b.center_metres, i)) {
        ++nonzero;
        axis = i;
      }
    if (nonzero == 0) return Membership::interior;
    if (nonzero == 1) {
      Expansion first, last;
      first.append(component(p, axis));
      first.append(-component(b.center_metres, axis));
      first.append(-component(b.semi_axes_metres, axis));
      last.append(-component(p, axis));
      last.append(component(b.center_metres, axis));
      last.append(-component(b.semi_axes_metres, axis));
      if (first.sign() == Sign::unsupported || last.sign() == Sign::unsupported)
        return Membership::unresolved;
      if (first.sign() == Sign::positive || last.sign() == Sign::positive)
        return Membership::outside;
      return first.sign() == Sign::zero || last.sign() == Sign::zero
                 ? Membership::boundary
                 : Membership::interior;
    }
  }
  const auto inv = inverse(b.frame);
  const auto r = idifference(p, b.center_metres);
  Interval sum = point(0);
  for (std::size_t i = 0; i < 3; ++i)
    sum = plus(sum, square(divide(idot(inv.rows[i], r),
                                  point(component(b.semi_axes_metres, i)))));
  if (!sum.supported) return Membership::unresolved;
  if (sum.high < 1) return Membership::interior;
  if (sum.low > 1) return Membership::outside;
  return Membership::unresolved;
}
auto solid_member(const BoardingSelfSolid& s, Vec p) -> Membership {
  return std::visit(
      [&](const auto& q) {
        using Shape = std::decay_t<decltype(q)>;
        if constexpr (std::is_same_v<Shape, Box>)
          return box_member(q, p);
        else if constexpr (std::is_same_v<Shape, Capsule>)
          return capsule_member(q, p);
        else
          return ellipsoid_member(q, p);
      },
      s);
}
auto normal_norm(Vec n) -> Interval {
  std::size_t nonzero{}, axis{};
  for (std::size_t i = 0; i < 3; ++i)
    if (component(n, i) != 0) {
      ++nonzero;
      axis = i;
    }
  if (nonzero == 0) return point(0);
  if (nonzero == 1) return point(std::abs(component(n, axis)));
  return root(inorm_squared(iv(n)));
}
auto support(const BoardingSelfSolid& s, Vec n) -> Interval {
  if (!valid(s) || !bounded(n, 64) || (n != Vec{} && dot(n, n) == 0))
    return fail_interval();
  return std::visit(
      [&](const auto& shape) -> Interval {
        using Shape = std::decay_t<decltype(shape)>;
        if constexpr (std::is_same_v<Shape, Capsule>) {
          const auto a = idot(iv(shape.start_metres), iv(n)),
                     b = idot(iv(shape.end_metres), iv(n));
          if (!a.supported || !b.supported) return fail_interval();
          return plus(
              interval(std::max(a.low, b.low), std::max(a.high, b.high)),
              times(point(shape.radius_metres), normal_norm(n)));
        } else {
          Interval extent = point(0);
          const auto halves = [&]() {
            if constexpr (std::is_same_v<Shape, Box>)
              return shape.half_size_metres;
            else
              return shape.semi_axes_metres;
          }();
          if constexpr (std::is_same_v<Shape, Ellipsoid>) {
            if (identity(shape.frame)) {
              std::size_t nonzero{}, axis{};
              for (std::size_t i = 0; i < 3; ++i)
                if (component(n, i) != 0) {
                  ++nonzero;
                  axis = i;
                }
              if (nonzero <= 1)
                return plus(
                    idot(iv(shape.center_metres), iv(n)),
                    times(point(component(halves, axis)),
                          point(nonzero == 0 ? 0
                                             : std::abs(component(n, axis)))));
            }
          }
          for (std::size_t i = 0; i < 3; ++i) {
            auto term = times(point(component(halves, i)),
                              idot(iv(shape.frame.columns[i]), iv(n)));
            if constexpr (std::is_same_v<Shape, Box>)
              term = absolute(term);
            else
              term = square(term);
            extent = plus(extent, term);
          }
          if constexpr (std::is_same_v<Shape, Ellipsoid>) extent = root(extent);
          return plus(idot(iv(shape.center_metres), iv(n)), extent);
        }
      },
      s);
}
auto cofactor_y(const BoardingBodyFrame& f) -> std::array<Expansion, 3> {
  std::array<Expansion, 3> n;
  for (std::size_t i = 0; i < 3; ++i) {
    const auto j = (i + 1) % 3, k = (i + 2) % 3;
    n[i].product(component(f.columns[2], j), component(f.columns[0], k));
    n[i].product(component(f.columns[2], k), component(f.columns[0], j), -1);
  }
  return n;
}
auto determinant_y(const BoardingBodyFrame& f,
                   const std::array<Expansion, 3>& n) -> Expansion {
  Expansion e;
  for (std::size_t i = 0; i < 3; ++i)
    e.scaled(n[i], component(f.columns[1], i));
  return e;
}
auto cap_plane_sign(const BoardingSelfEllipsoidCap& cap, Vec p) -> Sign {
  // Sign of q.y-limit, retaining exact stored-value differences.
  if (identity(cap.axial_frame)) {
    Expansion e;
    e.append(p.y);
    e.append(-cap.axial_origin_metres.y);
    e.append(-cap.axial_limit_metres);
    return e.sign();
  }
  const auto n = cofactor_y(cap.axial_frame);
  const auto d = determinant_y(cap.axial_frame, n);
  if (d.sign() != Sign::positive) return Sign::unsupported;
  Expansion e;
  for (std::size_t i = 0; i < 3; ++i) {
    const auto delta =
        two_sum(component(p, i), -component(cap.axial_origin_metres, i));
    e.scaled(n[i], delta.high);
    e.scaled(n[i], delta.low);
  }
  e.scaled(d, -cap.axial_limit_metres);
  return e.sign();
}
auto region_member(const BoardingSelfRegion& r, Vec p) -> Membership {
  return std::visit(
      [&](const auto& q) -> Membership {
        using Shape = std::decay_t<decltype(q)>;
        if (!valid(q) || !bounded(p)) return Membership::invalid_geometry;
        if constexpr (std::is_same_v<Shape, Capsule>)
          return capsule_member(q, p);
        else if constexpr (std::is_same_v<Shape, BoardingSelfSphere>)
          return sphere_member(q.center_metres, q.radius_metres, p);
        else {
          const auto member = ellipsoid_member(q.ellipsoid, p);
          const auto plane = cap_plane_sign(q, p);
          if (member == Membership::outside || plane == Sign::positive)
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
auto same(const Ellipsoid& a, const Ellipsoid& b) -> bool {
  return a.center_metres == b.center_metres &&
         a.semi_axes_metres == b.semi_axes_metres &&
         a.frame.columns == b.frame.columns;
}
auto exact_cap_support(const BoardingSelfEllipsoidCap& cap,
                       const BoardingSelfSolid& partner) -> Sign {
  const auto n = cofactor_y(cap.axial_frame);
  const auto det = determinant_y(cap.axial_frame, n);
  if (det.sign() != Sign::positive) return Sign::unsupported;
  return std::visit(
      [&](const auto& shape) -> Sign {
        using Shape = std::decay_t<decltype(shape)>;
        if constexpr (std::is_same_v<Shape, Capsule>)
          return Sign::unsupported;
        else {
          Expansion gap;
          if constexpr (std::is_same_v<Shape, Ellipsoid>) {
            if (shape.frame.columns != cap.axial_frame.columns)
              return Sign::unsupported;
            if (identity(cap.axial_frame)) {
              gap.append(cap.axial_limit_metres);
              gap.append(-shape.semi_axes_metres.y);
              gap.append(-shape.center_metres.y);
              gap.append(cap.axial_origin_metres.y);
              return gap.sign();
            }
            const auto delta =
                two_sum(cap.axial_limit_metres, -shape.semi_axes_metres.y);
            gap.scaled(det, delta.high);
            gap.scaled(det, delta.low);
          } else {
            gap.scaled(det, cap.axial_limit_metres);
            for (std::size_t j = 0; j < 3; ++j) {
              Expansion projection;
              for (std::size_t i = 0; i < 3; ++i)
                projection.scaled(n[i], component(shape.frame.columns[j], i));
              const auto sign = projection.sign();
              if (sign == Sign::unsupported) return sign;
              gap.scaled(projection, (sign == Sign::negative ? 1 : -1) *
                                         component(shape.half_size_metres, j));
            }
          }
          for (std::size_t i = 0; i < 3; ++i) {
            const auto delta = two_sum(component(shape.center_metres, i),
                                       -component(cap.axial_origin_metres, i));
            gap.scaled(n[i], -delta.high);
            gap.scaled(n[i], -delta.low);
          }
          return gap.sign();
        }
      },
      partner);
}
auto exact_axis_support_gap(const BoardingSelfSolid& first,
                            const BoardingSelfSolid& second, Vec n) -> Sign {
  std::size_t nonzero{}, axis{};
  for (std::size_t i = 0; i < 3; ++i)
    if (component(n, i) != 0) {
      ++nonzero;
      axis = i;
    }
  if (nonzero != 1) return Sign::unsupported;
  Expansion gap;
  auto include = [&](const BoardingSelfSolid& solid,
                     double center_sign) -> bool {
    return std::visit(
        [&](const auto& q) -> bool {
          using Shape = std::decay_t<decltype(q)>;
          const auto a = component(n, axis);
          if constexpr (std::is_same_v<Shape, Capsule>) {
            const auto first_value = component(q.start_metres, axis),
                       last_value = component(q.end_metres, axis);
            // gap=min(second)-max(first); choose extrema before exact products.
            const auto v = center_sign * a > 0
                               ? std::min(first_value, last_value)
                               : std::max(first_value, last_value);
            gap.product(v, a, center_sign);
            gap.product(q.radius_metres, std::abs(a), -1);
          } else {
            if (!identity(q.frame)) return false;
            const auto h = [&]() {
              if constexpr (std::is_same_v<Shape, Box>)
                return q.half_size_metres;
              else
                return q.semi_axes_metres;
            }();
            gap.product(component(q.center_metres, axis), a, center_sign);
            gap.product(component(h, axis), std::abs(a), -1);
          }
          return true;
        },
        solid);
  };
  if (!include(first, -1) || !include(second, 1)) return Sign::unsupported;
  return gap.sign();
}
auto separated_on(const BoardingSelfSolid& a, const BoardingSelfSolid& b, Vec n,
                  bool& arithmetic_unresolved) -> bool {
  if (n == Vec{} || !bounded(n, 64)) return false;
  const auto exact = exact_axis_support_gap(a, b, n);
  if (exact == Sign::positive || exact == Sign::zero) return true;
  const auto upper = support(a, n), reverse = support(b, scale(n, -1));
  const auto gap = minus(minus(reverse), upper);
  if (!gap.supported) arithmetic_unresolved = true;
  return gap.supported && gap.low >= 0;
}
auto solid_center(const BoardingSelfSolid& solid) -> Vec {
  return std::visit(
      [](const auto& s) -> Vec {
        using Shape = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<Shape, Capsule>)
          return add(s.start_metres,
                     scale(sub(s.end_metres, s.start_metres), .5));
        else
          return s.center_metres;
      },
      solid);
}
auto separation(const BoardingSelfSolid& a, const BoardingSelfSolid& b,
                bool& arithmetic_unresolved) -> bool {
  std::array<Vec, 14> normals{};
  std::size_t count{};
  bool capacity_supported{true};
  auto push = [&](Vec n) {
    if (count < normals.size())
      normals[count++] = n;
    else
      capacity_supported = false;
  };
  push({1, 0, 0});
  push({0, 1, 0});
  push({0, 0, 1});
  push(sub(solid_center(b), solid_center(a)));
  for (const auto* s : {&a, &b})
    std::visit(
        [&](const auto& q) {
          using Shape = std::decay_t<decltype(q)>;
          if constexpr (!std::is_same_v<Shape, Capsule>)
            for (std::size_t i = 0; i < 3; ++i)
              push(cross(q.frame.columns[(i + 1) % 3],
                         q.frame.columns[(i + 2) % 3]));
        },
        *s);
  std::visit(
      [&](const auto& q) {
        if constexpr (std::is_same_v<std::decay_t<decltype(q)>, Capsule>) {
          push(sub(q.start_metres, solid_center(b)));
          push(sub(q.end_metres, solid_center(b)));
        }
      },
      a);
  std::visit(
      [&](const auto& q) {
        if constexpr (std::is_same_v<std::decay_t<decltype(q)>, Capsule>) {
          push(sub(q.start_metres, solid_center(a)));
          push(sub(q.end_metres, solid_center(a)));
        }
      },
      b);
  if (!capacity_supported) return false;
  for (std::size_t i = 0; i < count; ++i)
    if (separated_on(a, b, normals[i], arithmetic_unresolved) ||
        separated_on(a, b, scale(normals[i], -1), arithmetic_unresolved))
      return true;
  return false;
}
auto box_plane_sign(const Box& box, Vec origin, Vec toward,
                    bool subtract_axis_squared) -> Sign {
  std::array<Expansion, 3> d;
  for (std::size_t i = 0; i < 3; ++i)
    d[i] = difference(component(toward, i), component(origin, i));
  Expansion extent;
  for (std::size_t i = 0; i < 3; ++i)
    extent.product_expansions(d[i], difference(component(box.center_metres, i),
                                               component(origin, i)));
  for (std::size_t j = 0; j < 3; ++j) {
    Expansion projection;
    for (std::size_t i = 0; i < 3; ++i)
      projection.scaled(d[i], component(box.frame.columns[j], i));
    const auto sign = projection.sign();
    if (sign == Sign::unsupported) return sign;
    extent.scaled(projection, (sign == Sign::negative ? -1 : 1) *
                                  component(box.half_size_metres, j));
  }
  if (subtract_axis_squared)
    for (const auto& delta : d)
      extent.product_expansions(delta, delta, -1);
  return extent.sign();
}
auto nonpositive(Sign s) -> bool {
  return s == Sign::negative || s == Sign::zero;
}
auto nonnegative(Sign s) -> bool {
  return s == Sign::positive || s == Sign::zero;
}
auto capsule_sphere_owned(const Capsule& a, const Capsule& b,
                          const BoardingSelfSphere& region,
                          bool& arithmetic_unresolved)
    -> BoardingSelfCertificate {
  Vec joint, first, second;
  bool matched{};
  for (const auto candidate : {a.start_metres, a.end_metres}) {
    if (candidate == b.start_metres || candidate == b.end_metres) {
      joint = candidate;
      first = candidate == a.start_metres ? a.end_metres : a.start_metres;
      second = candidate == b.start_metres ? b.end_metres : b.start_metres;
      matched = true;
      break;
    }
  }
  if (!matched || joint != region.center_metres || first == joint ||
      second == joint)
    return BoardingSelfCertificate::none;
  const auto maximum = std::max(a.radius_metres, b.radius_metres);
  if (actual_collinear(first, joint, second, joint) &&
      exact_dot_difference(first, joint, second, joint).sign() ==
          Sign::negative &&
      maximum <= region.radius_metres)
    return BoardingSelfCertificate::opposite_ray_endpoint_ball;
  const auto u = idifference(first, joint), v = idifference(second, joint);
  const auto lengths = times(root(inorm_squared(u)), root(inorm_squared(v)));
  const auto cosine = divide(idot(u, v), lengths);
  const auto sine_squared = times(point(.5), minus(point(1), cosine));
  if (!sine_squared.supported) {
    arithmetic_unresolved = true;
    return BoardingSelfCertificate::none;
  }
  if (sine_squared.low <= 0) return BoardingSelfCertificate::none;
  const auto distance = divide(square(point(maximum)), sine_squared),
             radius = square(point(region.radius_metres));
  if (!distance.supported || !radius.supported) arithmetic_unresolved = true;
  return distance.supported && radius.supported && distance.high <= radius.low
             ? BoardingSelfCertificate::half_ray_angle_bound
             : BoardingSelfCertificate::none;
}
auto ownership(const BoardingSelfPart& a, const BoardingSelfPart& b,
               const BoardingSelfConnectedRegion& registered,
               bool& arithmetic_unresolved) -> BoardingSelfCertificate {
  const auto& region = registered.region;
  if (const auto* cap = std::get_if<BoardingSelfEllipsoidCap>(&region)) {
    const BoardingSelfSolid* partner{};
    if (const auto* ellipse = std::get_if<Ellipsoid>(&a.solid);
        ellipse && same(*ellipse, cap->ellipsoid))
      partner = &b.solid;
    if (const auto* ellipse = std::get_if<Ellipsoid>(&b.solid);
        ellipse && same(*ellipse, cap->ellipsoid))
      partner = &a.solid;
    if (partner) {
      if (std::holds_alternative<Capsule>(*partner))
        return BoardingSelfCertificate::none;
      if (const auto* e = std::get_if<Ellipsoid>(partner);
          e && e->frame.columns != cap->axial_frame.columns)
        return BoardingSelfCertificate::none;
      const auto sign = exact_cap_support(*cap, *partner);
      if (sign == Sign::unsupported) arithmetic_unresolved = true;
      if (nonnegative(sign))
        return BoardingSelfCertificate::cap_partner_support;
    }
    return BoardingSelfCertificate::none;
  }
  if (const auto* sphere = std::get_if<BoardingSelfSphere>(&region)) {
    const auto* first = std::get_if<Capsule>(&a.solid);
    const auto* second = std::get_if<Capsule>(&b.solid);
    if (first && second)
      return capsule_sphere_owned(*first, *second, *sphere,
                                  arithmetic_unresolved);
    const Capsule* upper = first ? first : second;
    const auto* trunk = std::get_if<Ellipsoid>(first ? &b.solid : &a.solid);
    if (!upper || !trunk || sphere->center_metres != upper->start_metres ||
        sphere->radius_metres < upper->radius_metres)
      return BoardingSelfCertificate::none;
    const auto d = idifference(upper->end_metres, upper->start_metres);
    const auto length = root(inorm_squared(d));
    if (!length.supported) {
      arithmetic_unresolved = true;
      return BoardingSelfCertificate::none;
    }
    if (length.low <= 0) return BoardingSelfCertificate::none;
    const auto uz = divide(d[2], length);
    if (!uz.supported) {
      arithmetic_unresolved = true;
      return BoardingSelfCertificate::none;
    }
    if (uz.high >= 0) return BoardingSelfCertificate::none;
    const auto threshold = root(minus(square(point(sphere->radius_metres)),
                                      square(point(upper->radius_metres))));
    const auto perpendicular =
        divide(root(plus(square(d[0]), square(d[1]))), length);
    const auto cylinder_max =
        plus(plus(point(upper->start_metres.z), times(threshold, uz)),
             times(point(upper->radius_metres), perpendicular));
    const auto distal_max =
        plus(point(upper->end_metres.z), point(upper->radius_metres));
    const auto minimum = minus(support(*trunk, {0, 0, -1}));
    if (!cylinder_max.supported || !distal_max.supported || !minimum.supported)
      arithmetic_unresolved = true;
    if (cylinder_max.supported && distal_max.supported && minimum.supported &&
        cylinder_max.high < minimum.low && distal_max.high < minimum.low)
      return BoardingSelfCertificate::shoulder_ball_cylinder_split;
    return BoardingSelfCertificate::none;
  }
  const auto* prefix = std::get_if<Capsule>(&region);
  if (!prefix) return BoardingSelfCertificate::none;
  const auto* first = std::get_if<Capsule>(&a.solid);
  const auto* second = std::get_if<Capsule>(&b.solid);
  const Capsule* axis = first ? first : second;
  const auto* box = std::get_if<Box>(first ? &b.solid : &a.solid);
  if (!axis || !box || prefix->radius_metres < axis->radius_metres)
    return BoardingSelfCertificate::none;
  if (registered.junction == BoardingSelfJunction::hip) {
    if (prefix->start_metres != axis->start_metres ||
        prefix->end_metres == prefix->start_metres ||
        !actual_collinear(axis->end_metres, axis->start_metres,
                          prefix->end_metres, prefix->start_metres) ||
        exact_dot_difference(axis->end_metres, axis->start_metres,
                             prefix->end_metres, prefix->start_metres)
                .sign() != Sign::positive)
      return BoardingSelfCertificate::none;
    const auto sign =
        box_plane_sign(*box, prefix->start_metres, prefix->end_metres, true);
    if (sign == Sign::unsupported) arithmetic_unresolved = true;
    if (nonpositive(sign))
      return BoardingSelfCertificate::collinear_proximal_capsule;
  } else if (registered.junction == BoardingSelfJunction::ankle) {
    if (prefix->start_metres != axis->end_metres)
      return BoardingSelfCertificate::none;
    const auto sign =
        box_plane_sign(*box, axis->end_metres, axis->start_metres, false);
    if (sign == Sign::unsupported) arithmetic_unresolved = true;
    if (nonpositive(sign)) return BoardingSelfCertificate::endpoint_halfspace;
  } else if (registered.junction == BoardingSelfJunction::wrist) {
    const auto wrist = axis->end_metres;
    if (prefix->start_metres != wrist || box->center_metres != wrist ||
        !identity(box->frame) || prefix->end_metres.x != wrist.x ||
        prefix->end_metres.z != wrist.z || prefix->end_metres.y >= wrist.y ||
        axis->start_metres.y >= wrist.y)
      return BoardingSelfCertificate::none;
    const auto h = box->half_size_metres;
    const auto band = h.z * .5, r = prefix->radius_metres;
    Expansion middle, lower, length, upper;
    middle.product(h.x, h.x);
    middle.product(h.z, h.z);
    middle.product(band, band);
    middle.product(r, r, -1);
    lower.product(h.x, h.x);
    lower.product(h.z, h.z);
    lower.product(r, r, -1);
    length.append(wrist.y);
    length.append(-prefix->end_metres.y);
    length.append(-h.y);
    for (std::size_t i = 0; i < 3; ++i) {
      const auto d =
          difference(component(axis->start_metres, i), component(wrist, i));
      const auto sign = d.sign();
      if (sign == Sign::unsupported) {
        arithmetic_unresolved = true;
        return BoardingSelfCertificate::none;
      }
      upper.scaled(d, i == 1 ? band
                             : (sign == Sign::negative ? -1 : 1) *
                                   component(h, i));
    }
    if (middle.sign() == Sign::unsupported ||
        lower.sign() == Sign::unsupported ||
        length.sign() == Sign::unsupported || upper.sign() == Sign::unsupported)
      arithmetic_unresolved = true;
    if (nonpositive(middle.sign()) && nonpositive(lower.sign()) &&
        nonnegative(length.sign()) && nonpositive(upper.sign()))
      return BoardingSelfCertificate::wrist_box_bands;
  }
  return BoardingSelfCertificate::none;
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
auto valid_region(const BoardingSelfConnectedRegion& r) -> bool {
  return std::visit([](const auto& q) { return valid(q); }, r.region);
}
auto witness(const BoardingSelfPart& a, const BoardingSelfPart& b,
             const std::optional<BoardingSelfConnectedRegion>& region)
    -> std::optional<Vec> {
  std::array<Vec, 14> points{};
  std::size_t count{};
  auto push = [&](Vec p) {
    if (count < points.size()) points[count++] = p;
  };
  push(solid_center(a.solid));
  push(solid_center(b.solid));
  for (const auto* shape : {&a.solid, &b.solid})
    std::visit(
        [&](const auto& q) {
          if constexpr (std::is_same_v<std::decay_t<decltype(q)>, Capsule>) {
            push(q.start_metres);
            push(q.end_metres);
          }
        },
        *shape);
  for (std::size_t i = 0; i < count; ++i)
    if (solid_member(a.solid, points[i]) == Membership::interior &&
        solid_member(b.solid, points[i]) == Membership::interior &&
        (!region ||
         region_member(region->region, points[i]) == Membership::outside))
      return points[i];
  return std::nullopt;
}
} // namespace

namespace detail {
auto boarding_self_model02_linear_sign(std::span<const double> terms) -> Sign {
  if (terms.size() > expansion_capacity) return Sign::unsupported;
  Expansion e;
  for (const auto term : terms)
    e.append(term);
  return e.sign();
}
auto boarding_self_model02_membership(const BoardingSelfSolid& s, Vec p)
    -> Membership {
  return solid_member(s, p);
}
auto boarding_self_model02_region_membership(const BoardingSelfRegion& r, Vec p)
    -> Membership {
  return region_member(r, p);
}
auto boarding_self_model02_support(const BoardingSelfSolid& s, Vec n)
    -> BoardingSelfSupportBounds {
  const auto bound = support(s, n);
  return {bound.low, bound.high, bound.supported};
}
auto boarding_self_model02_prefix(Vec joint, Vec toward, double length,
                                  double radius)
    -> std::expected<Capsule, BoardingBodyError> {
  if (!finite(joint) || !finite(toward) || !std::isfinite(length) ||
      !std::isfinite(radius))
    return std::unexpected(BoardingBodyError::non_finite_input);
  if (!bounded(joint) || !bounded(toward) || !dimension(length) ||
      !dimension(radius))
    return std::unexpected(BoardingBodyError::numerical_failure);
  const auto d = sub(toward, joint);
  const auto n = std::hypot(d.x, d.y, d.z);
  if (!(n > 0) || !std::isfinite(n))
    return std::unexpected(BoardingBodyError::numerical_failure);
  const auto reciprocal = 1 / n;
  const auto unit = scale(d, reciprocal), step = scale(unit, length),
             end = add(joint, step);
  if (!std::isfinite(reciprocal) || !finite(unit) || !finite(step) ||
      !bounded(end))
    return std::unexpected(BoardingBodyError::numerical_failure);
  return Capsule{joint, end, radius};
}
auto boarding_self_model02_pair(
    const BoardingSelfPart& a, const BoardingSelfPart& b,
    const std::optional<BoardingSelfConnectedRegion>& region)
    -> BoardingSelfPairDiagnostic {
  BoardingSelfPairDiagnostic result;
  result.first = a.id;
  result.second = b.id;
  const auto relation = expected_junction(a.id, b.id);
  if (static_cast<std::size_t>(a.id) >= kBoardingBodyPartCount ||
      static_cast<std::size_t>(b.id) >= kBoardingBodyPartCount ||
      a.id >= b.id || !valid(a.solid) || !valid(b.solid) ||
      (region && (!valid_region(*region) || region->first != a.id ||
                  region->second != b.id || !relation ||
                  region->junction != *relation)) ||
      (!region && relation)) {
    result.reason = BoardingSelfUnresolved::invalid_geometry;
    return result;
  }
  if (const auto point = witness(a, b, region)) {
    result.outcome =
        region ? BoardingSelfPairOutcome::strict_unowned_connected_interior
               : BoardingSelfPairOutcome::strict_nonadjacent_interior;
    result.certificate = BoardingSelfCertificate::verified_strict_witness;
    result.strict_witness_metres = *point;
    return result;
  }
  bool arithmetic_unresolved{};
  if (region) {
    const auto certificate = ownership(a, b, *region, arithmetic_unresolved);
    if (certificate != BoardingSelfCertificate::none) {
      result.outcome = BoardingSelfPairOutcome::certified_whole_owned;
      result.certificate = certificate;
      return result;
    }
  }
  if (separation(a.solid, b.solid, arithmetic_unresolved)) {
    result.outcome = BoardingSelfPairOutcome::certified_no_interior;
    result.certificate = BoardingSelfCertificate::convex_support_plane;
    return result;
  }
  result.reason = arithmetic_unresolved
                      ? BoardingSelfUnresolved::arithmetic_unresolved
                      : BoardingSelfUnresolved::unsupported_family;
  return result;
}
} // namespace detail

auto evaluate_origin_boarding_self_model02(const BoardingBodyPose& pose)
    -> std::expected<BoardingSelfDiagnostic, BoardingBodyError> {
  auto canonical = evaluate_origin_boarding_body(pose);
  if (!canonical) return std::unexpected(canonical.error());
  BoardingSelfDiagnostic result;
  result.canonical = *canonical;
  for (std::size_t i = 0; i < result.self_parts.size(); ++i) {
    const auto& part = canonical->parts[i];
    result.self_parts[i].id = part.id;
    result.self_parts[i].solid = std::visit(
        [&](const auto& q) -> BoardingSelfSolid {
          using Shape = std::decay_t<decltype(q)>;
          if constexpr (std::is_same_v<Shape, Box>) {
            if (part.id == BoardingBodyPartId::trunk ||
                part.id == BoardingBodyPartId::helmet)
              return Ellipsoid{q.center_metres, q.half_size_metres, q.frame};
          }
          return q;
        },
        part.world_reservation);
  }
  const auto& trunk = std::get<Ellipsoid>(result.self_parts[1].solid);
  const auto& helmet = std::get<Ellipsoid>(result.self_parts[2].solid);
  const auto hip =
      std::get<Box>(canonical->parts[0].world_reservation).center_metres;
  using Id = BoardingBodyPartId;
  using J = BoardingSelfJunction;
  result.regions[0] = {Id::pelvis, Id::trunk, J::waist,
                       BoardingSelfEllipsoidCap{trunk, hip, trunk.frame,
                                                kBoardingSelfWaistLimitMetres}};
  result.regions[1] = {Id::trunk, Id::helmet, J::neck,
                       BoardingSelfEllipsoidCap{helmet, hip, trunk.frame,
                                                kBoardingSelfNeckLimitMetres}};
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& joint = canonical->joints[side];
    const auto base = 3 + side * 6, index = 2 + side * 6;
    const auto thigh = static_cast<Id>(base), shin = static_cast<Id>(base + 1),
               boot = static_cast<Id>(base + 2),
               upper = static_cast<Id>(base + 3),
               fore = static_cast<Id>(base + 4),
               hand = static_cast<Id>(base + 5);
    const auto thigh_radius =
        std::get<Capsule>(canonical->parts[base].world_reservation)
            .radius_metres;
    const auto shin_radius =
        std::get<Capsule>(canonical->parts[base + 1].world_reservation)
            .radius_metres;
    const auto fore_radius =
        std::get<Capsule>(canonical->parts[base + 4].world_reservation)
            .radius_metres;
    const auto hip_prefix = detail::boarding_self_model02_prefix(
        joint.hip_metres, joint.knee_metres, kBoardingSelfHipLengthMetres,
        thigh_radius);
    const auto ankle_prefix = detail::boarding_self_model02_prefix(
        joint.ankle_metres, joint.knee_metres, kBoardingSelfAnkleLengthMetres,
        shin_radius);
    const auto wrist_prefix = detail::boarding_self_model02_prefix(
        joint.wrist_metres, joint.elbow_metres, kBoardingSelfWristLengthMetres,
        fore_radius);
    if (!hip_prefix) return std::unexpected(hip_prefix.error());
    if (!ankle_prefix) return std::unexpected(ankle_prefix.error());
    if (!wrist_prefix) return std::unexpected(wrist_prefix.error());
    result.regions[index] = {Id::pelvis, thigh, J::hip, *hip_prefix};
    result.regions[index + 1] = {
        thigh, shin, J::knee,
        BoardingSelfSphere{joint.knee_metres, kBoardingSelfKneeRadiusMetres}};
    result.regions[index + 2] = {shin, boot, J::ankle, *ankle_prefix};
    result.regions[index + 3] = {
        Id::trunk, upper, J::shoulder,
        BoardingSelfSphere{joint.shoulder_metres,
                           kBoardingSelfShoulderRadiusMetres}};
    result.regions[index + 4] = {
        upper, fore, J::elbow,
        BoardingSelfSphere{joint.elbow_metres, kBoardingSelfElbowRadiusMetres}};
    result.regions[index + 5] = {fore, hand, J::wrist, *wrist_prefix};
  }
  std::sort(result.regions.begin(), result.regions.end(),
            [](const auto& a, const auto& b) {
              return a.first == b.first ? a.second < b.second
                                        : a.first < b.first;
            });
  result.self_checkpoint_passed = true;
  std::size_t pair_index{};
  for (std::size_t first = 0; first < result.self_parts.size(); ++first)
    for (std::size_t second = first + 1; second < result.self_parts.size();
         ++second) {
      const auto& a = result.self_parts[first];
      const auto& b = result.self_parts[second];
      std::optional<std::size_t> region_index;
      std::optional<BoardingSelfConnectedRegion> region;
      for (std::size_t i = 0; i < result.regions.size(); ++i)
        if (result.regions[i].first == a.id &&
            result.regions[i].second == b.id) {
          region_index = i;
          region = result.regions[i];
          break;
        }
      auto pair = detail::boarding_self_model02_pair(a, b, region);
      pair.connected_region_index = region_index;
      const auto strict =
          pair.outcome ==
              BoardingSelfPairOutcome::strict_nonadjacent_interior ||
          pair.outcome ==
              BoardingSelfPairOutcome::strict_unowned_connected_interior;
      const auto unresolved =
          pair.outcome == BoardingSelfPairOutcome::unresolved;
      result.strict_conflict = result.strict_conflict || strict;
      result.unresolved_pair = result.unresolved_pair || unresolved;
      result.self_checkpoint_passed =
          result.self_checkpoint_passed &&
          (pair.outcome == BoardingSelfPairOutcome::certified_no_interior ||
           (region &&
            pair.outcome == BoardingSelfPairOutcome::certified_whole_owned));
      result.pairs[pair_index++] = pair;
    }
  if (pair_index != kBoardingBodyPairCount)
    return std::unexpected(BoardingBodyError::numerical_failure);
  return result;
}
} // namespace apsis_drift
