#pragma once

#include "apsis_drift/rigid_body.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

// Body-independent fixed-orientation swept-OBB/finite-triangle arithmetic.
// Owning planet/moon adapters validate provenance and supply actual facets;
// these helpers confer no material, body or collision authority by themselves.
namespace apsis_drift::detail::impact_numeric {
using V = RigidVector3;
inline constexpr double kContactToleranceMetres = 0.000001;
inline auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline auto mul(V a, double s) -> V {
  return {a.x * s, a.y * s, a.z * s};
}
inline auto dot(V a, V b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
inline auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline auto length(V a) -> double {
  return std::hypot(a.x, a.y, a.z);
}
inline auto finite(V a) -> bool {
  return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
inline auto rotate(RigidOrientation q, V v) -> V {
  const V imaginary{q.x, q.y, q.z};
  const double norm = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  return add(v, mul(add(mul(cross(imaginary, v), q.w),
                        cross(imaginary, cross(imaginary, v))),
                    2.0 / norm));
}
template <class Position> inline auto point(Position p) -> V {
  return {p.x, p.y, p.z};
}
template <class Triangle> inline auto normal(const Triangle& triangle) -> V {
  return {triangle.outward_normal.x, triangle.outward_normal.y,
          triangle.outward_normal.z};
}
template <class Prepared, class Part>
auto shape_center(const Prepared& p, const Part& part, double fraction) -> V {
  return add(add(p.start.position_metres, mul(p.delta, fraction)),
             rotate(p.start.orientation, part.center));
}
template <class Prepared, class Part>
auto radius_on(const Prepared& p, const Part& part, V axis, double inflation)
    -> double {
  return (part.half.x + inflation) * std::abs(dot(p.axes[0], axis)) +
         (part.half.y + inflation) * std::abs(dot(p.axes[1], axis)) +
         (part.half.z + inflation) * std::abs(dot(p.axes[2], axis));
}
// Continuous SAT for a fixed-orientation translating OBB and a finite triangle.
// All 13 separating axes are tested, including edge/edge axes; a plane crossing
// outside the triangle therefore cannot manufacture an impact.
template <class Prepared, class Part, class Triangle>
auto interval(const Prepared& p, const Part& part, const Triangle& triangle,
              double inflation) -> std::optional<std::array<double, 2>> {
  const V center = shape_center(p, part, 0);
  std::array<V, 3> vertices{sub(point(triangle.vertices[0]), center),
                            sub(point(triangle.vertices[1]), center),
                            sub(point(triangle.vertices[2]), center)};
  const std::array<V, 3> edges{sub(vertices[1], vertices[0]),
                               sub(vertices[2], vertices[1]),
                               sub(vertices[0], vertices[2])};
  std::array<V, 13> axes{};
  std::ranges::copy(p.axes, axes.begin());
  axes[3] = normal(triangle);
  std::size_t index = 4;
  for (const auto box_axis : p.axes)
    for (const auto edge : edges)
      axes[index++] = cross(box_axis, edge);
  double entry = 0, exit = 1;
  for (auto axis : axes) {
    const double norm = length(axis);
    if (norm <= 1e-12) continue;
    axis = mul(axis, 1.0 / norm);
    const auto projections = std::array{
        dot(vertices[0], axis), dot(vertices[1], axis), dot(vertices[2], axis)};
    const auto [minimum, maximum] =
        std::minmax_element(projections.begin(), projections.end());
    const double radius = radius_on(p, part, axis, inflation);
    const double speed = dot(p.delta, axis);
    if (speed == 0) {
      if (*minimum > radius || *maximum < -radius) return {};
      continue;
    }
    double first = (*minimum - radius) / speed,
           last = (*maximum + radius) / speed;
    if (first > last) std::swap(first, last);
    entry = std::max(entry, first);
    exit = std::min(exit, last);
    if (entry > exit) return {};
  }
  return std::array{entry, exit};
}
// Projection SAT against the finite facet footprint. The projected OBB is a
// convex hexagon; its edge directions are the three projected box axes. The
// triangle contributes its three edges. Infinite neighboring plane extensions
// cannot establish burial when these finite projections do not overlap.
template <class Prepared, class Part, class Triangle>
auto projection_overlap(const Prepared& p, const Part& part,
                        const Triangle& triangle) -> bool {
  const V center = shape_center(p, part, 0), n = normal(triangle);
  const std::array vertices{sub(point(triangle.vertices[0]), center),
                            sub(point(triangle.vertices[1]), center),
                            sub(point(triangle.vertices[2]), center)};
  std::array<V, 6> axes{};
  for (std::size_t i = 0; i < 3; ++i) {
    axes[i] = cross(n, p.axes[i]);
    axes[i + 3] = cross(n, sub(vertices[(i + 1) % 3], vertices[i]));
  }
  for (auto axis : axes) {
    const double norm = length(axis);
    if (norm <= 1e-12) continue;
    axis = mul(axis, 1 / norm);
    const auto projections = std::array{
        dot(vertices[0], axis), dot(vertices[1], axis), dot(vertices[2], axis)};
    const auto [minimum, maximum] =
        std::minmax_element(projections.begin(), projections.end());
    const double radius = radius_on(p, part, axis, 0);
    if (*minimum > radius || *maximum < -radius) return false;
  }
  return true;
}
// Clip the actual top triangle by all six proxy halfspaces at the SAT time.
// The centroid is on the triangle and in the proxy; numerical failure is
// unknown.
template <class Prepared, class Part, class Triangle>
auto contact_point(const Prepared& p, const Part& part,
                   const Triangle& triangle, double fraction)
    -> std::optional<V> {
  const V center = shape_center(p, part, fraction);
  std::vector<V> polygon{point(triangle.vertices[0]),
                         point(triangle.vertices[1]),
                         point(triangle.vertices[2])};
  const std::array halves{part.half.x, part.half.y, part.half.z};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    for (const double sign : {-1.0, 1.0}) {
      const V n = mul(p.axes[axis], sign);
      const auto distance = [&](V vertex) {
        return dot(sub(vertex, center), n) - halves[axis];
      };
      std::vector<V> clipped;
      if (polygon.empty()) return {};
      for (std::size_t i = 0; i < polygon.size(); ++i) {
        const V a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        const double da = distance(a), db = distance(b);
        const bool inside_a = da <= kContactToleranceMetres,
                   inside_b = db <= kContactToleranceMetres;
        if (inside_a) clipped.push_back(a);
        if (inside_a != inside_b) {
          const double denominator = da - db;
          if (denominator == 0 || !std::isfinite(denominator)) return {};
          const double t = std::clamp(da / denominator, 0.0, 1.0);
          clipped.push_back(add(a, mul(sub(b, a), t)));
        }
      }
      polygon = std::move(clipped);
      if (polygon.size() > 16) return {};
    }
  }
  if (polygon.empty()) return {};
  V average{};
  for (const auto vertex : polygon)
    average = add(average, sub(vertex, center));
  const V result =
      add(center, mul(average, 1.0 / static_cast<double>(polygon.size())));
  if (!finite(result) ||
      std::abs(dot(sub(result, point(triangle.vertices[0])),
                   normal(triangle))) > kContactToleranceMetres)
    return {};
  return result;
}
} // namespace apsis_drift::detail::impact_numeric
