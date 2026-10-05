#include "apsis_drift/origin_boarding_world_checkpoint.hpp"
#include "origin_boarding_self_model02_internal.hpp"
#include "origin_boarding_world_checkpoint_internal.hpp"
#include "origin_lower_cockpit_contact_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace apsis_drift {
namespace {
using Vec = RigidVector3;
using Box = BoardingBodyBox;
using Capsule = BoardingBodyCapsule;
using Solid = BoardingSelfSolid;
using Sign = detail::BoardingSelfSign;
using Refusal = BoardingWorldCheckpointRefusal;
constexpr auto infinity = std::numeric_limits<double>::infinity();
struct Interval {
  double low{}, high{};
};
auto down(double x) -> double {
  return std::nextafter(x, -infinity);
}
auto up(double x) -> double {
  return std::nextafter(x, infinity);
}
auto point(double x) -> Interval {
  return {x, x};
}
auto finite(Interval x) -> bool {
  return std::isfinite(x.low) && std::isfinite(x.high) && x.low <= x.high;
}
auto finite(Vec p) -> bool {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
auto add(Interval a, Interval b) -> Interval {
  if (a.low == 0 && a.high == 0) return b;
  if (b.low == 0 && b.high == 0) return a;
  return {down(a.low + b.low), up(a.high + b.high)};
}
auto product(double a, double b) -> Interval {
  if (a == 0 || b == 0) return {0, 0};
  const auto v = a * b;
  return {down(v), up(v)};
}
auto negative(Interval x) -> Interval {
  return {-x.high, -x.low};
}
auto dot_bounds(Vec a, Vec b) -> Interval {
  return add(add(product(a.x, b.x), product(a.y, b.y)), product(a.z, b.z));
}
auto subtract(Vec a, Vec b) -> Vec {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto add(Vec a, Vec b) -> Vec {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto scale(Vec a, double s) -> Vec {
  return {a.x * s, a.y * s, a.z * s};
}
auto dot(Vec a, Vec b) -> double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
auto cross(Vec a, Vec b) -> Vec {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto component(Vec p, std::size_t i) -> double {
  return i == 0 ? p.x : i == 1 ? p.y : p.z;
}
constexpr std::array<Vec, 3> world_axes{Vec{1, 0, 0}, Vec{0, 1, 0},
                                        Vec{0, 0, 1}};
auto translation(const BoardingBootStanceTranslation& t, Vec n) -> Interval {
  return add(add(product(n.y, t.terms[0]), product(n.y, t.terms[1])),
             product(n.y, t.terms[2]));
}
auto support(const Solid& s, const BoardingBootStanceTranslation& t, Vec n)
    -> Interval {
  const auto local = detail::boarding_self_model02_support(s, n);
  if (!local.supported) return {-infinity, infinity};
  return add({local.lower, local.upper}, translation(t, n));
}
// Products are decomposed before summation. This bounded endpoint fallback
// uses only world axes, so it needs no rounded dot or ideal-frame assumption.
struct Terms {
  std::array<double, 32> values{};
  std::size_t count{};
  bool supported{true};
  void push(double v) {
    if (!std::isfinite(v) || count == values.size()) {
      supported = false;
      return;
    }
    values[count++] = v;
  }
  void multiply(double a, double b) {
    if (!std::isfinite(a) || !std::isfinite(b) ||
        (a != 0 && b != 0 && std::ilogb(a) + std::ilogb(b) < -970)) {
      supported = false;
      return;
    }
    const auto v = a * b;
    if (!std::isfinite(v)) {
      supported = false;
      return;
    }
    push(v);
    push(std::fma(a, b, -v));
  }
  auto sign() const -> Sign {
    return supported ? detail::boarding_self_model02_linear_sign(
                           std::span<const double>(values.data(), count))
                     : Sign::unsupported;
  }
};
auto extremum_sign(const Solid& s, const BoardingBootStanceTranslation& t,
                   std::size_t axis, bool maximum, double endpoint) -> Sign {
  Terms terms;
  std::visit(
      [&](const auto& shape) {
        using Shape = std::decay_t<decltype(shape)>;
        if constexpr (std::is_same_v<Shape, Box>) {
          terms.push(component(shape.center_metres, axis));
          for (std::size_t i = 0; i < 3; ++i)
            terms.multiply((maximum ? 1. : -1.) *
                               component(shape.half_size_metres, i),
                           std::abs(component(shape.frame.columns[i], axis)));
        } else if constexpr (std::is_same_v<Shape, Capsule>) {
          const auto a = component(shape.start_metres, axis),
                     b = component(shape.end_metres, axis);
          terms.push(maximum ? std::max(a, b) : std::min(a, b));
          terms.push(maximum ? shape.radius_metres : -shape.radius_metres);
        } else {
          terms.supported = false;
        }
      },
      s);
  if (axis == 1)
    for (const auto term : t.terms)
      terms.push(term);
  terms.push(-endpoint);
  return terms.sign();
}
auto bounds(const Solid& s, const BoardingBootStanceTranslation& t)
    -> std::optional<detail::CabinContactBox> {
  std::array<double, 3> low{}, high{};
  for (std::size_t i = 0; i < 3; ++i) {
    const auto hi = support(s, t, world_axes[i]),
               lo = negative(support(s, t, scale(world_axes[i], -1)));
    if (!finite(hi) || !finite(lo)) return std::nullopt;
    low[i] = lo.low;
    high[i] = hi.high;
    // Fixed crop endpoints and the shared Y join; only a proved enclosure
    // tightening is allowed. No source endpoint/body position is adjusted.
    constexpr std::array<std::array<double, 4>, 3> endpoints{
        {{-1.05, 1.05, -1.05, 1.05},
         {-.85, -.2, 2.97, -.2},
         {-3.5, -.5, 6.35, -3.5}}};
    for (const auto endpoint : endpoints[i]) {
      if (low[i] < endpoint) {
        const auto sign = extremum_sign(s, t, i, false, endpoint);
        if (sign == Sign::zero || sign == Sign::positive) low[i] = endpoint;
      }
      if (high[i] > endpoint) {
        const auto sign = extremum_sign(s, t, i, true, endpoint);
        if (sign == Sign::zero || sign == Sign::negative) high[i] = endpoint;
      }
    }
  }
  return detail::CabinContactBox{{low[0], low[1], low[2]},
                                 {high[0], high[1], high[2]}};
}
// Proposal calculations below are ordinary approximate arithmetic only.
// Their points/lengths do not supply a certificate; the resulting stored axis
// must pass the complete conservative support predicate.
auto closest_segment(Vec p, Vec a, Vec b) -> Vec {
  const auto d = subtract(b, a);
  const auto denominator = dot(d, d);
  if (!(denominator > 0) || !std::isfinite(denominator)) return a;
  return add(
      a, scale(d, std::clamp(dot(subtract(p, a), d) / denominator, 0., 1.)));
}
auto closest_triangle(Vec p, const std::array<Vec, 3>& v) -> Vec {
  const auto ab = subtract(v[1], v[0]), ac = subtract(v[2], v[0]),
             ap = subtract(p, v[0]);
  const auto d1 = dot(ab, ap), d2 = dot(ac, ap);
  if (d1 <= 0 && d2 <= 0) return v[0];
  const auto bp = subtract(p, v[1]);
  const auto d3 = dot(ab, bp), d4 = dot(ac, bp);
  if (d3 >= 0 && d4 <= d3) return v[1];
  const auto vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0)
    return add(v[0], scale(ab, d1 / (d1 - d3)));
  const auto cp = subtract(p, v[2]);
  const auto d5 = dot(ab, cp), d6 = dot(ac, cp);
  if (d6 >= 0 && d5 <= d6) return v[2];
  const auto vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0)
    return add(v[0], scale(ac, d2 / (d2 - d6)));
  const auto va = d3 * d6 - d5 * d4;
  if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0)
    return add(
        v[1], scale(subtract(v[2], v[1]), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
  const auto denominator = va + vb + vc;
  if (denominator == 0 || !std::isfinite(denominator)) return v[0];
  return add(add(v[0], scale(ab, vb / denominator)),
             scale(ac, vc / denominator));
}
auto closest_segments(Vec p, Vec q, Vec a, Vec b) -> Vec {
  const auto d = subtract(q, p), e = subtract(b, a), r = subtract(p, a);
  const auto dd = dot(d, d), ee = dot(e, e), de = dot(d, e), dr = dot(d, r),
             er = dot(e, r);
  if (!(dd > 0)) return subtract(p, closest_segment(p, a, b));
  if (!(ee > 0)) return subtract(closest_segment(a, p, q), a);
  const auto denominator = dd * ee - de * de;
  auto s = denominator > 0
               ? std::clamp((de * er - dr * ee) / denominator, 0., 1.)
               : 0.;
  auto t = (de * s + er) / ee;
  if (t < 0) {
    t = 0;
    s = std::clamp(-dr / dd, 0., 1.);
  } else if (t > 1) {
    t = 1;
    s = std::clamp((de - dr) / dd, 0., 1.);
  }
  return subtract(add(p, scale(d, s)), add(a, scale(e, t)));
}
auto flat_y_certificate(const Box& b, const BoardingBootStanceTranslation& t,
                        const std::array<Vec, 3>& triangle) -> bool {
  if (b.frame.columns[0].y != 0 || std::abs(b.frame.columns[1].y) != 1 ||
      b.frame.columns[2].y != 0)
    return false;
  bool below = true, above = true;
  for (const auto p : triangle) {
    const std::array bottom{b.center_metres.y, -b.half_size_metres.y,
                            t.terms[0],        t.terms[1],
                            t.terms[2],        -p.y};
    const std::array top{b.center_metres.y, b.half_size_metres.y, t.terms[0],
                         t.terms[1],        t.terms[2],           -p.y};
    const auto lo = detail::boarding_self_model02_linear_sign(bottom),
               hi = detail::boarding_self_model02_linear_sign(top);
    below = below && (lo == Sign::zero || lo == Sign::positive);
    above = above && (hi == Sign::zero || hi == Sign::negative);
  }
  return below || above;
}
using PairCertificate = detail::BoardingWorldPairCertificate;
auto pair_certificate(const Solid& s, const BoardingBootStanceTranslation& t,
                      const std::array<Vec, 3>& triangle) -> PairCertificate {
  // Even the exact special branch requires validated actual shape semantics.
  if ((!std::holds_alternative<Box>(s) &&
       !std::holds_alternative<Capsule>(s)) ||
      !detail::boarding_self_model02_support(s, world_axes[0]).supported ||
      !std::ranges::all_of(t.terms,
                           [](double x) { return std::isfinite(x); }) ||
      !std::ranges::all_of(triangle, [](Vec p) { return finite(p); }))
    return {false, 0, 0};
  std::array<Vec, 16> axes{};
  std::size_t count{};
  for (const auto n : world_axes)
    axes[count++] = n;
  const std::array edges{subtract(triangle[1], triangle[0]),
                         subtract(triangle[2], triangle[1]),
                         subtract(triangle[0], triangle[2])};
  std::visit(
      [&](const auto& shape) {
        using Shape = std::decay_t<decltype(shape)>;
        if constexpr (std::is_same_v<Shape, Box>) {
          for (std::size_t i = 0; i < 3; ++i)
            axes[count++] =
                cross(shape.frame.columns[i], shape.frame.columns[(i + 1) % 3]);
          axes[count++] = cross(edges[0], edges[1]);
          for (const auto edge : edges)
            for (const auto column : shape.frame.columns)
              axes[count++] = cross(edge, column);
        } else if constexpr (std::is_same_v<Shape, Capsule>) {
          // Feature directions are translation-invariant only when source
          // points and capsule proposals are in the same reporting frame. Their
          // rounding is harmless: the full stored-axis predicate below remains
          // authority.
          const auto shift = Vec{0, t.reporting_metres, 0};
          const auto a = add(shape.start_metres, shift),
                     b = add(shape.end_metres, shift), segment = subtract(b, a);
          axes[count++] = cross(edges[0], edges[1]);
          axes[count++] = segment;
          for (const auto edge : edges)
            axes[count++] = cross(segment, edge);
          axes[count++] = subtract(a, closest_triangle(a, triangle));
          axes[count++] = subtract(b, closest_triangle(b, triangle));
          for (const auto p : triangle)
            axes[count++] = subtract(p, closest_segment(p, a, b));
          for (std::size_t i = 0; i < 3; ++i)
            axes[count++] =
                closest_segments(a, b, triangle[i], triangle[(i + 1) % 3]);
        }
      },
      s);
  PairCertificate result;
  for (std::size_t i = 0; i < count; ++i) {
    ++result.axes_examined;
    const auto magnitude = std::max(
        {std::abs(axes[i].x), std::abs(axes[i].y), std::abs(axes[i].z)});
    if (!finite(axes[i]) || !(magnitude > 0)) {
      ++result.unsupported_axes;
      continue;
    }
    const auto n = Vec{axes[i].x / magnitude, axes[i].y / magnitude,
                       axes[i].z / magnitude};
    if (i == 1)
      if (const auto* box = std::get_if<Box>(&s))
        if (flat_y_certificate(*box, t, triangle)) {
          result.certified = true;
          return result;
        }
    const auto hi = support(s, t, n),
               lo = negative(support(s, t, scale(n, -1)));
    Interval projected{infinity, -infinity};
    for (const auto p : triangle) {
      const auto v = dot_bounds(p, n);
      projected.low = std::min(projected.low, v.low);
      projected.high = std::max(projected.high, v.high);
    }
    if (!finite(hi) || !finite(lo) || !finite(projected)) {
      ++result.unsupported_axes;
      continue;
    }
    if (projected.high <= lo.low || hi.high <= projected.low) {
      result.certified = true;
      return result;
    }
  }
  return result;
}
auto solid(const BoardingBodyPart& part) -> Solid {
  return std::visit([](const auto& shape) -> Solid { return shape; },
                    part.world_reservation);
}
struct WorldBounds {
  std::array<Interval, 3> minimum, maximum;
};
auto world_bounds(const Solid& s, const BoardingBootStanceTranslation& t)
    -> WorldBounds {
  WorldBounds result;
  for (std::size_t i = 0; i < 3; ++i) {
    result.maximum[i] = support(s, t, world_axes[i]);
    result.minimum[i] = negative(support(s, t, scale(world_axes[i], -1)));
  }
  return result;
}
auto broad_separation(const WorldBounds& body,
                      const detail::CabinContactObstacle& triangle) -> bool {
  for (std::size_t i = 0; i < 3; ++i)
    if (finite(body.minimum[i]) && finite(body.maximum[i]) &&
        (component(triangle.bounds.high, i) <= body.minimum[i].low ||
         body.maximum[i].high <= component(triangle.bounds.low, i)))
      return true;
  return false;
}
} // namespace

namespace detail {
auto boarding_world_checkpoint_pair(const BoardingSelfSolid& reservation,
                                    const BoardingBootStanceTranslation& stance,
                                    const std::array<RigidVector3, 3>& triangle)
    -> BoardingWorldPairCertificate {
  return pair_certificate(reservation, stance, triangle);
}
} // namespace detail

auto assess_origin_boarding_world_checkpoint(
    const OriginBoardingBootSupport& provider,
    const BoardingBootSupportRequest& request)
    -> std::expected<BoardingWorldCheckpointDiagnostic, std::string> {
  auto boot = assess_origin_boarding_boot_support(provider, request);
  if (!boot) return std::unexpected(boot.error());
  const auto* contact = provider.contact();
  if (!contact) return std::unexpected("Missing selected effective contact");
  BoardingWorldCheckpointDiagnostic result;
  result.boot_support = std::move(*boot);
  const auto& parts = result.boot_support.local_self.canonical.parts;
  for (std::size_t i = 0; i < parts.size(); ++i)
    result.coverage[i].part = parts[i].id;
  const auto roster = detail::visit_effective_lower_cockpit_contact(
      *contact, [](const auto&) { return false; });
  if (!roster) return std::unexpected(roster.error());
  result.effective_triangle_count = roster->total_triangles;
  if (result.effective_triangle_count >
      std::numeric_limits<std::uint64_t>::max() / parts.size())
    return std::unexpected("World checkpoint pair count overflow");
  result.expected_pairs = result.effective_triangle_count * parts.size();
  if (!result.boot_support.checkpoint_supported) {
    result.first_refusal = BoardingWorldRefusalEvidence{};
    result.first_refusal->reason = Refusal::boot_self_prerequisite;
    return result;
  }
  for (std::size_t i = 0; i < parts.size(); ++i) {
    auto& evidence = result.coverage[i];
    evidence.examined = true;
    const auto placed = bounds(solid(parts[i]), result.boot_support.stance);
    if (placed) {
      evidence.arithmetic_supported = true;
      evidence.lower_metres = placed->low;
      evidence.upper_metres = placed->high;
      const auto covered =
          detail::covers_lower_cockpit_bounds(*contact, *placed);
      if (!covered) return std::unexpected(covered.error());
      evidence.covered = *covered;
    }
    if (!evidence.covered) {
      result.first_refusal = BoardingWorldRefusalEvidence{};
      result.first_refusal->part = parts[i].id;
      result.first_refusal->reason =
          placed ? Refusal::crop_uncovered : Refusal::unsupported_arithmetic;
      return result;
    }
  }
  result.coverage_complete = true;
  for (const auto& part : parts) {
    const auto reservation = solid(part);
    const auto broad = world_bounds(reservation, result.boot_support.stance);
    const auto visit = detail::visit_effective_lower_cockpit_contact(
        *contact, [&](const detail::LowerCockpitEffectiveTriangle& triangle) {
          ++result.examined_pairs;
          // Stored obstacle bounds are the exact min/max of its stored
          // vertices; the body bounds enclose the actual translated support
          // expressions.
          if (broad_separation(broad, *triangle.obstacle)) {
            ++result.certified_pairs;
            return true;
          }
          const auto certificate =
              pair_certificate(reservation, result.boot_support.stance,
                               triangle.obstacle->points);
          if (certificate.certified) {
            ++result.certified_pairs;
            return true;
          }
          result.first_refusal = BoardingWorldRefusalEvidence{
              part.id,
              certificate.axes_examined == certificate.unsupported_axes
                  ? Refusal::unsupported_arithmetic
                  : Refusal::no_separation_certificate,
              triangle.key,
              triangle.object,
              std::string(triangle.source_object),
              triangle.evaluated_source_triangle,
              certificate.axes_examined,
              certificate.unsupported_axes};
          return false;
        });
    if (!visit) return std::unexpected(visit.error());
    if (visit->total_triangles != result.effective_triangle_count)
      return std::unexpected("Effective contact roster changed");
    if (!visit->complete) return result;
  }
  result.comparisons_complete =
      result.examined_pairs == result.expected_pairs &&
      result.certified_pairs == result.expected_pairs;
  result.nonpenetrating = result.comparisons_complete;
  result.checkpoint_supported =
      result.coverage_complete && result.nonpenetrating;
  return result;
}
} // namespace apsis_drift
