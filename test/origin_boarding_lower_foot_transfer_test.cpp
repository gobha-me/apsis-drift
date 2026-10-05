#include "apsis_drift/origin_boarding_lower_foot_transfer.hpp"
#include "origin_boarding_lower_foot_transfer_internal.hpp"
#include "origin_boarding_source_endpoint_load_internal.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#if defined(__SSE2__) && defined(__x86_64__)
#include <xmmintrin.h>
#endif
namespace {
using namespace apsis_drift;
using Bounds = BoardingPlantedLegScalarBounds;
using Load = BoardingSourceEndpointLoadDiagnostic;
using Diagnostic = BoardingLowerFootTransferDiagnostic;
using Cell = BoardingLowerFootTransferCell;
using Result = detail::BoardingLowerFootTransferCellResult;
using Limits = detail::BoardingLowerFootTransferLimits;
using Condition = BoardingLowerFootTransferCondition;
std::size_t checks{};
int failures{};
auto check(bool value, std::string_view label) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T, class E> auto require(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Required transfer API refused input");
  return std::move(*value);
}
auto component(RigidVector3 p, std::size_t axis) -> double {
  return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
}
// Field-wise snapshot includes all runtime old fields; padding is never read.
struct Snapshot {
  std::vector<std::uint64_t> values;
  std::vector<std::string> names;
  friend auto operator==(const Snapshot&, const Snapshot&) -> bool = default;
  auto number(double n) -> void {
    values.push_back(std::bit_cast<std::uint64_t>(n));
  }
  template <class T> auto integer(T n) -> void {
    values.push_back(static_cast<std::uint64_t>(n));
  }
  auto scalar(BoardingPlantedLegScalarBounds b) -> void {
    number(b.lower);
    number(b.upper);
  }
  auto point(RigidVector3 p) -> void {
    number(p.x);
    number(p.y);
    number(p.z);
  }
  auto point(const BoardingPlantedLegPointBounds& b) -> void {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      number(component(b.lower, axis));
      number(component(b.upper, axis));
    }
  }
  auto derivatives(const BoardingPlantedLegPointDerivatives& e) -> void {
    point(e.velocity);
    point(e.acceleration);
  }
  auto closure(const BoardingPlantedLegEvidence& e) -> void {
    for (const auto& p : std::array{e.hip, e.knee, e.ankle, e.boot_center})
      point(p);
    for (const auto& b :
         std::array{e.distance_squared, e.rho, e.alpha, e.gamma, e.hip_sine,
                    e.hip_cosine, e.shin_sine, e.shin_cosine, e.knee_cosine,
                    e.roll_sine, e.roll_cosine})
      scalar(b);
    for (const auto x : e.ankle_y_terms)
      number(x);
    for (const auto x : e.boot_center_y_terms)
      number(x);
    number(e.plane_metres);
    number(e.reporting_hip_flex_degrees);
    number(e.reporting_knee_flex_degrees);
    number(e.reporting_ankle_pitch_degrees);
    number(e.reporting_hip_abduction_degrees);
    integer(e.plane_identity);
    integer(e.link_identities);
    integer(e.limits_certified);
    integer(e.limiting_condition);
  }
};

auto site_snapshot(const BoardingFootSitesDiagnostic& d) -> Snapshot {
  Snapshot out;
  const auto scalar = [&](const BoardingFootSiteScalarBounds& b) {
    out.number(b.lower);
    out.number(b.upper);
    out.integer(b.supported);
  };
  const auto edge = [&](const BoardingFootSiteEdgeEvidence& e) {
    scalar(e.signed_side);
    scalar(e.edge_length_squared);
    scalar(e.squared_margin_gap);
    out.integer(e.disk_contained);
  };
  const auto refusal = [&](const std::optional<BoardingFootSiteRefusal>& r) {
    out.integer(r.has_value());
    if (r) {
      out.integer(r->site);
      out.integer(r->partition.has_value());
      if (r->partition) out.integer(*r->partition);
      out.integer(r->condition);
    }
  };
  out.integer(d.sites_version);
  out.integer(d.arithmetic_supported);
  out.integer(d.coverage_complete);
  out.integer(d.eligible);
  refusal(d.first_refusal);
  for (double x : d.required_radius_margin_terms)
    out.number(x);
  for (const auto& p : d.source.selected_partitions()) {
    out.number(p.plane_metres);
    for (auto v : p.perimeter_metres)
      out.point(v);
    for (const auto& f : p.faces) {
      out.integer(f.key.buffer);
      out.integer(f.key.group);
      out.integer(f.key.triangle);
      out.integer(f.object);
      out.names.emplace_back(f.source_object);
      out.integer(f.evaluated_source_triangle.has_value());
      if (f.evaluated_source_triangle)
        out.integer(*f.evaluated_source_triangle);
      for (auto v : f.points_current_metres)
        out.point(v);
      out.point(f.unit_normal_current);
    }
  }
  for (const auto& s : d.sites) {
    for (double x : s.center_x_terms)
      out.number(x);
    for (double x : s.center_y_terms)
      out.number(x);
    for (double x : s.center_z_terms)
      out.number(x);
    for (const auto& row : s.pressure_offset_terms)
      for (double x : row)
        out.number(x);
    out.point(s.half_size_metres);
    for (auto p : s.center_bounds_metres)
      out.point(p);
    for (auto p : s.pressure_bounds_metres)
      out.point(p);
    for (const auto& row : s.sole_corner_bounds_metres)
      for (auto p : row)
        out.point(p);
    for (const auto& e : s.sole_edges)
      edge(e);
    for (const auto& p : s.partitions) {
      out.integer(p.evaluated);
      out.integer(p.footprint_overlap_possible);
      out.integer(p.disk_contained);
      out.integer(p.plane_relation);
      for (const auto& e : p.edges)
        edge(e);
    }
    out.integer(s.source_partition.has_value());
    if (s.source_partition) out.integer(*s.source_partition);
    out.integer(s.scanned_partitions);
    out.integer(s.arithmetic_supported);
    out.integer(s.coverage_complete);
    out.integer(s.placement_nonpenetrating);
    out.integer(s.sole_disk_contained);
    out.integer(s.source_disk_contained);
    out.integer(s.eligible);
    refusal(s.first_refusal);
  }
  return out;
}
auto binding_snapshot(
    const std::array<BoardingPlantedBodyPartBinding, 15>& parts) -> Snapshot {
  Snapshot out;
  for (const auto& p : parts) {
    out.integer(p.id);
    out.integer(p.reservation.index());
    out.integer(p.mass.first);
    out.integer(p.mass.second);
    out.integer(p.mass.weight);
    if (const auto* b =
            std::get_if<BoardingPlantedBodyBoxBinding>(&p.reservation)) {
      out.integer(b->center);
      out.point(b->half_size_metres);
      for (auto column : b->frame.columns)
        out.point(column);
    } else {
      const auto& c =
          std::get<BoardingPlantedBodyCapsuleBinding>(p.reservation);
      out.integer(c.start);
      out.integer(c.end);
      out.number(c.radius_metres);
    }
  }
  return out;
}
auto snapshot(const BoardingSourceEndpointDiagnostic& d) -> Snapshot {
  Snapshot out = site_snapshot(d.sites);
  out.integer(d.endpoint_version);
  out.number(d.common_translation_y_metres);
  out.number(d.root_z_metres);
  for (double x : d.port_x_terms)
    out.number(x);
  for (double x : d.starboard_x_terms)
    out.number(x);
  for (const auto& leg : d.legs)
    out.closure(leg);
  const auto bindings = binding_snapshot(d.parts);
  out.values.insert(out.values.end(), bindings.values.begin(),
                    bindings.values.end());
  out.integer(d.body.has_value());
  if (d.body) {
    for (const auto& p : d.body->points)
      out.point(p);
    for (const auto& p : d.body->mass_points)
      out.point(p);
    out.point(d.body->center_of_mass);
  }
  for (const auto& h : d.hips) {
    out.integer(h.first_part);
    out.integer(h.second_part);
    out.number(h.hip_limit_metres);
    out.number(h.axis_length_metres);
    out.number(h.thigh_radius_metres);
    out.point(h.axis);
    out.scalar(h.scaled_pelvis_extent);
    out.scalar(h.scaled_hip_limit);
    out.scalar(h.scaled_gap);
    out.integer(h.bindings_valid);
    out.integer(h.link_identity);
    out.integer(h.structural_x_zero);
    out.integer(h.arithmetic_supported);
    out.integer(h.certified);
  }
  out.integer(d.evaluated_legs);
  out.integer(d.assembled_records);
  out.integer(d.examined_hip_regions);
  out.integer(d.arithmetic_supported);
  out.integer(d.plane_identities);
  out.integer(d.link_identities);
  out.integer(d.joint_limits_certified);
  out.integer(d.reservations_complete);
  out.integer(d.mass_model_complete);
  out.integer(d.hip_regions_certified);
  out.integer(d.complete);
  out.integer(d.first_refusal.has_value());
  if (d.first_refusal) {
    out.integer(d.first_refusal->condition);
    out.integer(d.first_refusal->side.has_value());
    if (d.first_refusal->side) out.integer(*d.first_refusal->side);
    out.integer(d.first_refusal->leg_condition);
  }
  return out;
}

auto snapshot(const BoardingSourceEndpointSelfDiagnostic& d) -> Snapshot {
  auto s = snapshot(d.endpoint);
  s.integer(d.self_version);
  for (const auto& p : d.parts) {
    s.integer(p.id);
    s.integer(p.shape);
  }
  std::array<BoardingPlantedBodyPartBinding, 15> parts{};
  for (std::size_t i = 0; i < 15; ++i)
    parts[i] = d.parts[i].binding;
  const auto bindings = binding_snapshot(parts);
  s.values.insert(s.values.end(), bindings.values.begin(),
                  bindings.values.end());
  for (const auto& r : d.regions) {
    s.integer(r.first);
    s.integer(r.second);
    s.integer(r.junction);
    s.integer(r.root);
    s.integer(r.toward);
    s.number(r.limit_metres);
  }
  for (const auto& p : d.pairs) {
    s.integer(p.first);
    s.integer(p.second);
    s.integer(p.connected_region_index.has_value());
    if (p.connected_region_index) s.integer(*p.connected_region_index);
    s.integer(p.certificate);
    s.scalar(p.certificate_gap);
    s.scalar(p.ownership_extent);
    s.scalar(p.ownership_limit);
    s.scalar(p.ownership_secondary_extent);
    s.point(p.separating_direction);
    s.integer(p.examined_axes);
    s.integer(p.signed_support_trials);
    s.integer(p.examined);
    s.integer(p.arithmetic_supported);
    s.integer(p.certified);
    s.integer(p.whole_owned);
    s.integer(p.structural_identity);
  }
  s.integer(d.examined_pairs);
  s.integer(d.certified_pairs);
  s.integer(d.examined_axes);
  s.integer(d.signed_support_trials);
  s.integer(d.bindings_complete);
  s.integer(d.arithmetic_supported);
  s.integer(d.coverage_complete);
  s.integer(d.self_qualified);
  s.integer(d.complete);
  s.integer(d.first_refusal.has_value());
  if (d.first_refusal) {
    s.integer(d.first_refusal->condition);
    s.integer(d.first_refusal->pair_index.has_value());
    if (d.first_refusal->pair_index) s.integer(*d.first_refusal->pair_index);
  }
  return s;
}
auto snapshot(const Load& d) -> Snapshot {
  auto s = snapshot(d.self);
  const auto scalar = [&](const BoardingFootSiteScalarBounds& b) {
    s.number(b.lower);
    s.number(b.upper);
    s.integer(b.supported);
  };
  const auto edge = [&](const BoardingFootSiteEdgeEvidence& e) {
    scalar(e.signed_side);
    scalar(e.edge_length_squared);
    scalar(e.squared_margin_gap);
    s.integer(e.disk_contained);
  };
  const auto& l = d.load;
  s.integer(l.load_version);
  scalar(l.barycenter_z);
  scalar(l.common_pressure_delta_z);
  for (double x : l.reaction_fractions)
    s.number(x);
  s.number(l.disk_radius_metres);
  s.number(l.disk_edge_margin_metres);
  s.number(l.required_projected_load_margin_metres);
  s.number(l.certified_resultant_disk_radius_metres);
  for (const auto& p : l.pressures) {
    s.number(p.plane_metres);
    for (auto v : p.pressure_bounds_metres)
      s.point(v);
    for (const auto& e : p.sole_edges)
      edge(e);
    for (const auto& e : p.source_edges)
      edge(e);
    s.integer(p.source_partition.has_value());
    if (p.source_partition) s.integer(*p.source_partition);
    for (auto k : p.source_keys) {
      s.integer(k.buffer);
      s.integer(k.group);
      s.integer(k.triangle);
    }
    for (const auto& c : p.candidates) {
      scalar(c.minimum_signed_side);
      scalar(c.minimum_squared_gap);
      s.integer(c.status);
      s.integer(c.arithmetic_supported);
      s.integer(c.quad_valid);
    }
    s.integer(p.scanned_partitions);
    s.integer(p.arithmetic_supported);
    s.integer(p.coplanar);
    s.integer(p.sole_disk_contained);
    s.integer(p.source_disk_contained);
    s.integer(p.coverage_complete);
    s.integer(p.complete);
  }
  for (const auto& q : l.source_quads) {
    scalar(q.minimum_edge_squared);
    scalar(q.minimum_signed_side);
    s.integer(q.checked_edges);
    s.integer(q.checked_side_signs);
    s.integer(q.arithmetic_supported);
    s.integer(q.horizontal);
    s.integer(q.upward);
    s.integer(q.convex);
    s.integer(q.nondegenerate);
    s.integer(q.valid);
  }
  s.integer(l.checked_quads);
  s.integer(l.pressure_candidates);
  s.integer(l.edge_checks);
  for (bool b : std::array{
           l.bindings_complete, l.arithmetic_supported, l.paired_com_x_identity,
           l.positive_reactions, l.projected_barycenter_identity,
           l.vertical_force_identity, l.vertical_moment_identity,
           l.placement_nonpenetrating, l.contact_supported,
           l.projected_margin_certified, l.load_qualified, l.complete})
    s.integer(b);
  s.integer(l.first_refusal.has_value());
  if (l.first_refusal) {
    s.integer(l.first_refusal->condition);
    s.integer(l.first_refusal->site.has_value());
    if (l.first_refusal->site) s.integer(*l.first_refusal->site);
    s.integer(l.first_refusal->partition.has_value());
    if (l.first_refusal->partition) s.integer(*l.first_refusal->partition);
  }
  return s;
} // Test-local LD automatic differentiation evaluates an unfactored 3D graph.
// Its rounding allowance corroborates bounds; it never admits game geometry.
struct Jet {
  long double value{}, first{}, second{};
};
auto operator+(Jet a, Jet b) -> Jet {
  return {a.value + b.value, a.first + b.first, a.second + b.second};
}
auto operator-(Jet a) -> Jet {
  return {-a.value, -a.first, -a.second};
}
auto operator-(Jet a, Jet b) -> Jet {
  return a + -b;
}
auto operator*(Jet a, Jet b) -> Jet {
  return {a.value * b.value, a.first * b.value + a.value * b.first,
          a.second * b.value + 2 * a.first * b.first + a.value * b.second};
}
auto reciprocal(Jet a) -> Jet {
  const auto inv = 1 / a.value;
  return {inv, -a.first * inv * inv,
          2 * a.first * a.first * inv * inv * inv - a.second * inv * inv};
}
auto operator/(Jet a, Jet b) -> Jet {
  return a * reciprocal(b);
}
auto root(Jet a) -> Jet {
  const auto r = std::sqrt(a.value);
  return {r, a.first / (2 * r),
          a.second / (2 * r) - a.first * a.first / (4 * r * r * r)};
}
auto literal(double x) -> Jet {
  return {static_cast<long double>(x), 0, 0};
}
using Point = std::array<Jet, 3>;
auto add(Point a, Point b) -> Point {
  for (std::size_t axis = 0; axis < 3; ++axis)
    a[axis] = a[axis] + b[axis];
  return a;
}
auto subtract(Point a, Point b) -> Point {
  for (std::size_t axis = 0; axis < 3; ++axis)
    a[axis] = a[axis] - b[axis];
  return a;
}
auto scale(Point a, Jet s) -> Point {
  for (auto& x : a)
    x = x * s;
  return a;
}
auto dot(Point a, Point b) -> Jet {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
auto cross(Point a, Point b) -> Point {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}
auto constant_point(double x, double y, double z) -> Point {
  return {literal(x), literal(y), literal(z)};
}
auto angle(Jet x, Jet y) -> Jet {
  const auto denominator = x.value * x.value + y.value * y.value;
  const auto numerator = x.value * y.first - y.value * x.first;
  return {std::atan2(y.value, x.value), numerator / denominator,
          (x.value * y.second - y.value * x.second) / denominator -
              numerator * 2 * (x.value * x.first + y.value * y.first) /
                  (denominator * denominator)};
}
struct Mass {
  std::size_t first{}, second{};
  std::uint32_t weight{};
};
constexpr std::array<Mass, 15> masses{{{0, 0, 144},
                                       {1, 1, 540},
                                       {2, 2, 96},
                                       {4, 5, 120},
                                       {5, 6, 48},
                                       {7, 7, 12},
                                       {8, 9, 15},
                                       {9, 10, 10},
                                       {10, 10, 5},
                                       {11, 12, 120},
                                       {12, 13, 48},
                                       {14, 14, 12},
                                       {15, 16, 15},
                                       {16, 17, 10},
                                       {17, 17, 5}}};
struct Oracle {
  std::array<Point, 18> points{};
  std::array<Point, 15> mass_points{};
  Point com{};
  std::array<std::array<Jet, 6>, 2> angular{};
  std::array<std::array<long double, 3>, 2> speeds{};
  std::array<long double, 2> hip_extent{}, ankle_extent{};
  std::array<std::array<long double, 8>, 2> margins{};
  Jet fraction{};
  std::array<Jet, 2> barycenter{}, delta{};
  std::array<std::array<Jet, 2>, 2> pressure{};
};
auto oracle(double t) -> Oracle {
  Oracle out;
  const Jet parameter{static_cast<long double>(t), 1, 0};
  const auto one = literal(1), two = literal(2);
  const auto s = parameter * parameter * parameter *
                 (literal(10) - literal(15) * parameter +
                  literal(6) * parameter * parameter);
  auto& p = out.points;
  p[0] = {literal(.16) + literal(.07) * s, literal(.012) * s,
          literal(-.55) - literal(.04) * s};
  p[1] = add(p[0], constant_point(0, .3495, 0));
  p[2] = add(p[0], constant_point(0, .70237, 0));
  p[3] = add(p[0], constant_point(0, .65237, 0));
  const auto arm = -literal(.35898) * root(literal(.5));
  const auto l1 = literal(.47285), l2 = literal(.47478);
  for (std::size_t side = 0; side < 2; ++side) {
    const auto base = side == 0 ? 4U : 11U;
    const auto sign = side == 0 ? -1. : 1.;
    const auto plane = side == 0 ? kCabinCorridorFloorMetres : -160000.0 * 1e-6;
    p[base] = add(p[0], constant_point(sign * .14, 0, 0));
    const auto x = literal(.16) + literal(sign * .14);
    p[base + 2] = {x, literal(plane) + literal(.1) - literal(.847),
                   literal(side == 0 ? -.5 : -.8)};
    p[base + 3] = {x, literal(plane) + literal(.05) - literal(.847),
                   literal(side == 0 ? -.5 : -.8)};
    const auto d = subtract(p[base + 2], p[base]);
    const auto rho = root(d[0] * d[0] + d[1] * d[1]);
    const auto distance = dot(d, d);
    const auto u = Point{-d[0] / rho, -d[1] / rho, literal(0)};
    const auto n = Point{u[1], -u[0], literal(0)};
    const auto q = cross(n, d);
    const auto alpha = (l1 * l1 - l2 * l2 + distance) / (two * distance);
    // Unfactored gamma differs algebraically from the production reach factor.
    const auto gamma = root((l1 * l1 - alpha * alpha * distance) / distance);
    p[base + 1] = add(add(p[base], scale(d, alpha)), scale(q, gamma));
    const auto f1 = alpha * rho + gamma * d[2], g1 = gamma * rho - alpha * d[2];
    const auto f2 = (one - alpha) * rho - gamma * d[2];
    const auto g2 = -((one - alpha) * d[2] + gamma * rho);
    const auto theta1 = angle(f1, g1), theta2 = angle(f2, g2),
               phi = angle(-d[1], d[0]);
    out.angular[side] = {
        theta1, theta2, theta1 - theta2, -theta2, literal(sign) * phi, -phi};
    const auto radians = std::numbers::pi_v<long double> / 180;
    out.margins[side] = {
        -d[1].value * (2 - std::sqrt(3.L)) - std::abs(d[0].value),
        g1.value * std::cos(20 * radians) + f1.value * std::sin(20 * radians),
        f1.value * std::sin(65 * radians) - g1.value * std::cos(65 * radians),
        (distance.value - l1.value * l1.value - l2.value * l2.value) /
                (2 * l1.value * l2.value) +
            std::sqrt(2.L) / 2,
        f2.value / 2 - std::abs(g2.value) * std::sqrt(3.L) / 2,
        rho.value * rho.value,
        distance.value,
        gamma.value * gamma.value};
    out.speeds[side] = {std::hypot(phi.first, theta1.first) / 4,
                        std::abs(theta1.first - theta2.first) / 4,
                        std::hypot(phi.first, theta2.first) / 4};
    const auto thigh = scale(subtract(p[base + 1], p[base]), reciprocal(l1));
    const auto shin = scale(subtract(p[base + 1], p[base + 2]), reciprocal(l2));
    out.hip_extent[side] =
        static_cast<long double>(.24) * std::abs(thigh[0].value) -
        static_cast<long double>(sign * .14) * thigh[0].value +
        static_cast<long double>(.12) * std::abs(thigh[1].value) +
        static_cast<long double>(.18) * std::abs(thigh[2].value);
    out.ankle_extent[side] =
        static_cast<long double>(.06) * std::abs(shin[0].value) +
        static_cast<long double>(.05) * std::abs(shin[1].value) +
        static_cast<long double>(.14) * std::abs(shin[2].value) -
        static_cast<long double>(.05) * shin[1].value;
    p[base + 4] = add(p[0], constant_point(sign * .20265, .579, 0));
    p[base + 5] = add(p[base + 4], Point{literal(0), arm, arm});
    p[base + 6] = add(p[base + 5], constant_point(0, .386, 0));
  }
  for (std::size_t i = 0; i < masses.size(); ++i) {
    const auto& m = masses[i];
    out.mass_points[i] = scale(add(p[m.first], p[m.second]), literal(.5));
    out.com = add(out.com, scale(out.mass_points[i], literal(m.weight)));
  }
  out.com = scale(out.com, reciprocal(literal(1200)));
  out.fraction = literal(.5) - literal(.25) * s;
  for (std::size_t axis = 0; axis < 2; ++axis) {
    const auto coordinate = axis == 0 ? 0U : 2U;
    out.barycenter[axis] = out.fraction * p[7][coordinate] +
                           (one - out.fraction) * p[14][coordinate];
    out.delta[axis] = out.com[coordinate] - out.barycenter[axis];
    out.pressure[0][axis] = p[7][coordinate] + out.delta[axis];
    out.pressure[1][axis] = p[14][coordinate] + out.delta[axis];
  }
  return out;
}
auto uncertainty(long double value) -> long double {
  return 262144 * std::numeric_limits<long double>::epsilon() *
         std::max(1.L, std::abs(value));
}
auto contains(Bounds b, long double x) -> void {
  check(std::isfinite(b.lower) && std::isfinite(b.upper) &&
            b.lower <= b.upper &&
            static_cast<long double>(b.lower) <= x + uncertainty(x) &&
            x - uncertainty(x) <= static_cast<long double>(b.upper),
        "Independent exact-expression value/derivative belongs to outward "
        "enclosure");
}
auto point_contains(const BoardingPlantedLegPointBounds& b, const Point& p,
                    std::size_t order, bool reverse) -> void {
  for (std::size_t axis = 0; axis < 3; ++axis) {
    const auto x = order == 0   ? p[axis].value
                   : order == 1 ? p[axis].first * (reverse ? -1.L : 1.L) / 4
                                : p[axis].second / 16;
    contains({component(b.lower, axis), component(b.upper, axis)}, x);
  }
}
auto point_contains(const BoardingPlantedBodyPointEvidence& b, const Point& p,
                    bool reverse) -> void {
  point_contains(b.value, p, 0, reverse);
  point_contains(b.derivatives.velocity, p, 1, reverse);
  point_contains(b.derivatives.acceleration, p, 2, reverse);
}
auto no_authority(const Diagnostic& d) -> void {
  check(!d.dynamics_qualified && !d.strength_qualified && !d.world_qualified &&
            !d.volume_qualified && !d.material_qualified &&
            !d.source_surface_sweep_qualified && !d.route_qualified &&
            !d.actor_qualified && !d.seat_qualified && !d.save_qualified &&
            !d.first_flight_qualified && !d.foot_acquisition_qualified &&
            !d.free_foot_swing_qualified &&
            !d.complete_lower_step_to_seat_transfer_qualified,
        "Partial nominal preparation never grants world, dynamics or actor "
        "authority");
}
auto corroborate(const Cell& c, double t, bool reverse,
                 const BoardingSourceEndpointSelfDiagnostic* initial = nullptr)
    -> void {
  const auto o = oracle(t);
  for (std::size_t i = 0; i < o.points.size(); ++i)
    point_contains(c.points[i], o.points[i], reverse);
  for (std::size_t i = 0; i < o.mass_points.size(); ++i)
    point_contains(c.mass_points[i], o.mass_points[i], reverse);
  point_contains(c.center_of_mass, o.com, reverse);
  long double speed2{}, acceleration2{};
  for (const auto& x : o.points[0]) {
    speed2 += x.first * x.first / 16;
    acceleration2 += x.second * x.second / 256;
  }
  contains(c.root_speed, std::sqrt(speed2));
  contains(c.root_acceleration, std::sqrt(acceleration2));
  for (std::size_t axis = 0; axis < 3; ++axis) {
    const auto independent =
        (literal(960) * o.points[0][axis] +
         literal(84) * (o.points[5][axis] + o.points[12][axis])) /
        literal(1200);
    check(std::abs(o.com[axis].first - independent.first) <=
                  uncertainty(independent.first) &&
              std::abs(o.com[axis].second - independent.second) <=
                  uncertainty(independent.second),
          "Full roster independently agrees with COM derivative identity");
  }
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& l = c.legs[side];
    const std::array actual{l.hip_pitch,   l.shin_pitch,    l.knee_flex,
                            l.ankle_pitch, l.hip_abduction, l.ankle_roll};
    for (std::size_t j = 0; j < actual.size(); ++j) {
      contains(actual[j].rate,
               o.angular[side][j].first * (reverse ? -1.L : 1.L) / 4);
      contains(actual[j].coordinate_second, o.angular[side][j].second / 16);
    }
    for (std::size_t j = 0; j < 3; ++j)
      contains(l.joint_speeds[j], o.speeds[side][j]);
    for (std::size_t j = 0; j < 8; ++j)
      contains(l.margins[j], o.margins[side][j]);
    const auto radians = std::numbers::pi_v<long double> / 180;
    const auto hip = o.angular[side][0].value / radians;
    const auto shin = o.angular[side][1].value / radians;
    const auto knee = o.angular[side][2].value / radians;
    const auto abduction = o.angular[side][4].value / radians;
    const auto ankle_roll = o.angular[side][5].value / radians;
    check(hip >= -20 - uncertainty(hip) && hip <= 65 + uncertainty(hip) &&
              knee >= -uncertainty(knee) && knee <= 135 + uncertainty(knee) &&
              std::abs(shin) <= 30 + uncertainty(shin) &&
              std::abs(abduction) <= 35 + uncertainty(abduction) &&
              std::abs(ankle_roll) <= 15 + uncertainty(ankle_roll),
          "Independent directed joint angles obey all original "
          "hip/knee/flat-ankle sectors");
    check(l.link_identities && l.plane_identity && l.branch_certified &&
              l.joint_sectors_certified && l.derivative_domains_certified &&
              l.joint_speeds_certified,
          "Accepted graph owns original leg domains/sectors and physical "
          "resultants");
  }
  if (c.nominal_vertical_equilibrium_complete) {
    contains(c.port_reaction_fraction, o.fraction.value);
    for (std::size_t axis = 0; axis < 2; ++axis) {
      contains(c.barycenter_xz[axis], o.barycenter[axis].value);
      contains(c.common_pressure_delta_xz[axis], o.delta[axis].value);
      for (std::size_t side = 0; side < 2; ++side)
        contains(c.pressures[side].pressure_xz[axis],
                 o.pressure[side][axis].value);
      const auto weighted = o.fraction * o.pressure[0][axis] +
                            (literal(1) - o.fraction) * o.pressure[1][axis];
      const auto coordinate = axis == 0 ? 0U : 2U;
      check(std::abs(weighted.value - o.com[coordinate].value) <=
                uncertainty(weighted.value),
            "Complementary vertical forces independently balance full "
            "asymmetric COM moments");
    }
  }
  if (initial && c.self_complete) {
    for (std::size_t i = 0; i < initial->regions.size(); ++i) {
      const auto& r = initial->regions[i];
      const auto& own = c.owners[i];
      if (!own.certified) continue;
      check(own.arithmetic_supported && own.extent.upper <= own.limit.lower,
            "Certified original finite owner bounds its whole region");
      const auto part = static_cast<std::size_t>(r.second);
      if (part == 3 || part == 9)
        contains(own.extent, o.hip_extent[part == 3 ? 0 : 1]);
      if (part == 5 || part == 11)
        contains(own.extent, o.ankle_extent[part == 5 ? 0 : 1]);
    }
  }
  if (t == 0 || t == 1) {
    for (const auto& p : o.points)
      for (const auto& x : p)
        check(x.first == 0 && x.second == 0,
              "Independent complete-body endpoint graph has C2 hold join");
  }
}
struct SavedEnvironment {
  std::fenv_t saved{};
  SavedEnvironment() {
    if (std::fegetenv(&saved) != 0)
      throw std::runtime_error("Cannot preserve FP environment");
  }
  SavedEnvironment(const SavedEnvironment&) = delete;
  auto operator=(const SavedEnvironment&) -> SavedEnvironment& = delete;
  ~SavedEnvironment() {
    check(std::fesetenv(&saved) == 0, "Restore arithmetic environment");
  }
};
auto environment_refusal(const OriginBoardingBootSupport& p) -> void {
  Cell c;
  BoardingLowerFootTransferRefusal r;
  const auto result =
      detail::boarding_lower_foot_transfer_kinematic_math(0, 0, false, c, r);
  check(result == Result::unsupported && !c.arithmetic_supported && !c.complete,
        "Compiled math refuses unsafe environment before positive graph "
        "evidence");
  Limits limits;
  limits.nodes = 0;
  const auto d = detail::boarding_lower_foot_transfer_bounded(p, 0, 1, limits);
  check(!d || (!d->complete && !d->arithmetic_supported && d->cells.empty() &&
               d->work.self_pairs == 0),
        "Unsafe environment cannot bypass graph/initial prerequisites through "
        "zero work cap");
}
auto environment_controls(const OriginBoardingBootSupport& p) -> void {
  for (int mode : std::array{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    SavedEnvironment saved;
    check(std::fesetround(mode) == 0, "Install non-nearest FP adversary");
    environment_refusal(p);
  }
#if defined(__SSE2__) && defined(__x86_64__)
  struct SavedControl {
    unsigned value{_mm_getcsr()};
    ~SavedControl() { _mm_setcsr(value); }
  };
  for (unsigned mask : std::array{1U << 15, 1U << 6, (1U << 15) | (1U << 6)}) {
    SavedEnvironment saved;
    SavedControl control;
    _mm_setcsr(control.value | mask);
    environment_refusal(p);
  }
#endif
#if defined(__SSE2__) && defined(__x86_64__)
  for (unsigned mode : std::array{1U, 2U, 3U}) {
    SavedEnvironment saved;
    SavedControl control;
    _mm_setcsr((control.value & ~(3U << 13)) | (mode << 13));
    environment_refusal(p);
  }
#endif
  check(std::fegetround() == FE_TONEAREST,
        "Nearest FP mode restored before public observation");
}
auto invalid_controls(const OriginBoardingBootSupport& p) -> void {
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto inf = std::numeric_limits<double>::infinity();
  for (double x : std::array{nan, inf, -inf, -.001, 1.001}) {
    check(!assess_origin_boarding_lower_foot_transfer(p, x, .5) &&
              !assess_origin_boarding_lower_foot_transfer(p, .5, x),
          "Nonfinite/out-of-range endpoints refuse as inputs");
    Cell c;
    BoardingLowerFootTransferRefusal r;
    check(detail::boarding_lower_foot_transfer_kinematic_math(
              x, .5, false, c, r) != Result::accepted,
          "Private malformed interval cannot yield positive math");
  }
  const auto bad_limit = [&](Limits l) {
    check(!detail::boarding_lower_foot_transfer_bounded(p, 0, 1, l),
          "Private budget may lower but cannot expand registered production "
          "caps");
  };
  Limits l;
  l.nodes = kBoardingLowerFootTransferMaximumNodes + 1;
  bad_limit(l);
  l = {};
  l.depth = kBoardingLowerFootTransferMaximumDepth + 1;
  bad_limit(l);
  l = {};
  l.leaves = kBoardingLowerFootTransferMaximumLeaves + 1;
  bad_limit(l);
  l = {};
  l.self_pairs = kBoardingLowerFootTransferMaximumPairs + 1;
  bad_limit(l);
  l = {};
  l.proposed_axes = kBoardingLowerFootTransferMaximumAxes + 1;
  bad_limit(l);
  l = {};
  l.signed_trials = kBoardingLowerFootTransferMaximumSignedTrials + 1;
  bad_limit(l);
  l = {};
  l.pressure_candidates =
      kBoardingLowerFootTransferMaximumPressureCandidates + 1;
  bad_limit(l);
  l = {};
  l.disk_edges = kBoardingLowerFootTransferMaximumEdges + 1;
  bad_limit(l);
  l = {};
  l.source_partitions = kBoardingBootSourcePartitionCount + 1;
  bad_limit(l);
  l = {};
  l.body_records = kBoardingSourceEndpointMaximumRecords + 1;
  bad_limit(l);
  l = {};
  l.initial_self_pairs = kBoardingBodyPairCount + 1;
  bad_limit(l);
  l = {};
  l.initial_self_axes = kBoardingSourceEndpointSelfMaximumAxes + 1;
  bad_limit(l);
  l = {};
  l.initial_pressure_partitions = kBoardingBootSourcePartitionCount + 1;
  bad_limit(l);
  auto source = p;
  auto live = std::move(source);
  // NOLINTBEGIN(bugprone-use-after-move) -- Empty-handle API contract.
  check(!assess_origin_boarding_lower_foot_transfer(source),
        "Moved-empty provider refuses public authority");
  // NOLINTEND(bugprone-use-after-move) -- Empty-handle API contract.
  check(live.contact() == p.contact(),
        "Moved live provider keeps immutable source identity");
}
auto prefix_invariants(const Diagnostic& d, const Limits& limits) -> void {
  no_authority(d);
  check(d.examined_nodes <= limits.nodes && d.maximum_depth <= limits.depth &&
            d.cells.size() <= limits.leaves,
        "Single shared cover respects lowered node/depth/leaf limits");
  check(
      d.work.self_pairs <= limits.self_pairs &&
          d.work.proposed_axes <= limits.proposed_axes &&
          d.work.signed_trials <= limits.signed_trials &&
          d.work.pressure_candidates <= limits.pressure_candidates &&
          d.work.disk_edges <= limits.disk_edges,
      "Every real attempted work prefix respects its independent lowered cap");
  check(d.work.self_pairs <= 105 * d.examined_nodes &&
            d.work.proposed_axes <= 14 * d.work.self_pairs &&
            d.work.signed_trials <= 2 * d.work.proposed_axes &&
            d.work.pressure_candidates <= 20 * d.examined_nodes &&
            d.work.disk_edges <= 88 * d.examined_nodes,
        "Actual aggregate prefixes respect per-node/pair/axis work "
        "relationships");
  check(d.output_capacity_bytes <= kBoardingLowerFootTransferMaximumOutputBytes,
        "Owned compact output stays within registered byte ceiling");
  check(d.seconds_per_parameter == 4 && d.common_translation_y_metres == .847,
        "Requested interval retains four-second clock and ONE fixed common "
        "placement");
  const auto low = std::min(d.requested_first, d.requested_last);
  const auto high = std::max(d.requested_first, d.requested_last);
  check(d.reverse == (d.requested_first > d.requested_last),
        "Request direction uses original endpoint order");
  check(d.reporting_elapsed_seconds == 4 * (high - low),
        "Subinterval elapsed time does not alter clock scale");
  double cursor = low;
  for (const auto& c : d.cells) {
    check(c.first == cursor && c.first <= c.last && c.last <= high,
          "Retained accepted cells form an ascending exact closed prefix");
    check(c.complete && c.arithmetic_supported && c.kinematics_complete &&
              c.timing_complete && c.self_complete &&
              c.nominal_vertical_equilibrium_complete && c.positive_reactions &&
              c.finite_pressure_complete,
          "Only fully certified complete cells enter accepted output");
    cursor = c.last;
  }
  if (d.complete) {
    check(!d.first_refusal && !d.cells.empty() && cursor == high &&
              d.kinematics_complete && d.timing_complete && d.self_complete &&
              d.nominal_vertical_equilibrium_complete &&
              d.finite_pressure_complete,
          "Complete result requires exact whole requested cover and all "
          "selected predicates");
  } else {
    check(d.first_refusal.has_value(),
          "Incomplete prefix retains an owned first refusal");
    check(!(d.kinematics_complete && d.timing_complete && d.self_complete &&
            d.nominal_vertical_equilibrium_complete &&
            d.finite_pressure_complete),
          "Refused prefix cannot manufacture all whole-request permissions");
  }
}
auto preliminary_cap_controls(const OriginBoardingBootSupport& p) -> void {
  for (std::size_t i = 0; i < 5; ++i) {
    Limits l;
    if (i == 0) l.source_partitions = 0;
    if (i == 1) l.body_records = 0;
    if (i == 2) l.initial_self_pairs = 0;
    if (i == 3) l.initial_self_axes = 0;
    if (i == 4) l.initial_pressure_partitions = 0;
    const auto d =
        require(detail::boarding_lower_foot_transfer_bounded(p, 0, 1, l));
    prefix_invariants(d, l);
    check(!d.complete && d.cells.empty() && d.examined_nodes == 0 &&
              d.work.self_pairs == 0,
          "Initial child capacity refusal stops before any new cover work");
  }
  Limits zero;
  zero.nodes = 0;
  const auto d =
      require(detail::boarding_lower_foot_transfer_bounded(p, 0, 1, zero));
  prefix_invariants(d, zero);
  check(
      d.cells.empty() && d.examined_nodes == 0 && d.work.self_pairs == 0,
      "Zero shared-node budget stops before first node or predicate attempts");
}
auto quad_and_pressure_controls(const OriginBoardingBootSupport& p,
                                const Load& initial) -> void {
  const auto source = p.selected_partitions();
  check(source.size() == 10,
        "Immutable provider retains ten original source quads");
  for (std::size_t i = 0; i < source.size(); ++i) {
    const auto& q = source[i];
    const auto e = require(detail::boarding_source_endpoint_load_quad_math(
        q.perimeter_metres, q.plane_metres));
    check(e.valid && e.checked_edges == 4 && e.checked_side_signs == 8,
          "Original actual quad kernel verifies every edge and convexity sign");
    for (const auto& point : q.perimeter_metres)
      check(point.y == q.plane_metres,
            "Actual source quad retains exact horizontal plane");
    for (const auto& face : q.faces) {
      const auto a = face.points_current_metres[0],
                 b = face.points_current_metres[1],
                 c = face.points_current_metres[2];
      const auto winding = (static_cast<long double>(b.z) - a.z) *
                               (static_cast<long double>(c.x) - a.x) -
                           (static_cast<long double>(b.x) - a.x) *
                               (static_cast<long double>(c.z) - a.z);
      check(winding > 0 && !face.source_object.empty(),
            "Independent actual source triangle winding/name identity");
    }
    auto bad = q.perimeter_metres;
    bad[1] = bad[0];
    check(!require(detail::boarding_source_endpoint_load_quad_math(
                       bad, q.plane_metres))
               .valid,
          "Repeated quad edge cannot certify finite pressure support");
    bad = q.perimeter_metres;
    std::swap(bad[1], bad[3]);
    check(!require(detail::boarding_source_endpoint_load_quad_math(
                       bad, q.plane_metres))
               .valid,
          "Reversed source winding cannot certify finite pressure support");
  }
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& pressure = initial.load.pressures[side];
    check(pressure.source_partition.has_value(),
          "Initial pressure owns actual selected source partition");
    if (!pressure.source_partition) continue;
    const auto xy = std::array{Bounds{pressure.pressure_bounds_metres[0].x,
                                      pressure.pressure_bounds_metres[1].x},
                               Bounds{pressure.pressure_bounds_metres[0].z,
                                      pressure.pressure_bounds_metres[1].z}};
    const auto e = require(detail::boarding_source_endpoint_load_pressure_math(
        p, side, *pressure.source_partition, xy));
    check(e.pressure.sole_disk_contained && e.pressure.source_disk_contained,
          "Unchanged local pressure math confirms complete initial source/sole "
          "margins");
    auto escape = xy;
    escape[0] = {2, 2};
    const auto bad = detail::boarding_source_endpoint_load_pressure_math(
        p, side, *pressure.source_partition, escape);
    check(!bad || !bad->pressure.sole_disk_contained,
          "Actual sole disk escape refuses local containment");
    auto malformed = xy;
    malformed[0] = {1, 0};
    check(!detail::boarding_source_endpoint_load_pressure_math(
              p, side, *pressure.source_partition, malformed),
          "Malformed pressure bounds reject before local arithmetic");
  }
}
auto observe(const OriginBoardingBootSupport& p) -> Diagnostic {
  auto d = require(assess_origin_boarding_lower_foot_transfer(p));
  std::cout << std::setprecision(17)
            << "FIRST lower-foot transfer version=" << d.transfer_version
            << " arithmetic=" << d.arithmetic_supported
            << " complete=" << d.complete
            << " kinematics=" << d.kinematics_complete
            << " timing=" << d.timing_complete << " self=" << d.self_complete
            << " nominal=" << d.nominal_vertical_equilibrium_complete
            << " pressure=" << d.finite_pressure_complete
            << " nodes=" << d.examined_nodes << " depth=" << d.maximum_depth
            << " cells=" << d.cells.size()
            << " quads=" << d.checked_source_quads
            << " pairs=" << d.work.self_pairs
            << " axes=" << d.work.proposed_axes
            << " signed=" << d.work.signed_trials
            << " candidates=" << d.work.pressure_candidates
            << " edges=" << d.work.disk_edges
            << " output=" << d.output_capacity_bytes
            << " cell_bytes=" << sizeof(Cell) << '\n';
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    std::cout << "REFUSAL condition=" << static_cast<unsigned>(r.condition)
              << " predicate=" << static_cast<unsigned>(r.predicate_condition)
              << " first=" << r.first << " last=" << r.last
              << " depth=" << r.depth
              << " side=" << (r.side ? static_cast<long long>(*r.side) : -1LL)
              << " pair=" << (r.pair ? static_cast<long long>(*r.pair) : -1LL)
              << " partition="
              << (r.partition ? static_cast<long long>(*r.partition) : -1LL)
              << " bound=[" << r.limiting_bound.lower << ','
              << r.limiting_bound.upper << "]\n";
  } else
    std::cout << "REFUSAL none\n";
  if (!d.cells.empty())
    std::cout << "COVER first=" << d.cells.front().first
              << " last=" << d.cells.back().last << '\n';
  std::cout.flush();
  return d;
}
auto pressure_edges(const std::array<RigidVector3, 4>& quad,
                    const std::array<Jet, 2>& pressure)
    -> std::array<long double, 2> {
  std::array<long double, 2> minima{
      std::numeric_limits<long double>::infinity(),
      std::numeric_limits<long double>::infinity()};
  const auto margin =
      static_cast<long double>(.02) + static_cast<long double>(.01);
  for (std::size_t i = 0; i < 4; ++i) {
    const auto a = quad[i], b = quad[(i + 1) % 4];
    const auto ex = static_cast<long double>(b.x) - a.x,
               ez = static_cast<long double>(b.z) - a.z;
    const auto side =
        ez * (pressure[0].value - a.x) - ex * (pressure[1].value - a.z);
    minima[0] = std::min(minima[0], side);
    minima[1] = std::min(minima[1],
                         side * side - margin * margin * (ex * ex + ez * ez));
  }
  return minima;
}
auto inspect_cells(const Diagnostic& d) -> void {
  prefix_invariants(d, Limits{});
  std::uint32_t mass_sum{};
  std::size_t boxes{}, capsules{};
  for (std::size_t i = 0; i < d.parts.size(); ++i) {
    const auto& p = d.parts[i];
    check(static_cast<std::size_t>(p.id) == i &&
              static_cast<std::size_t>(p.mass.first) == masses[i].first &&
              static_cast<std::size_t>(p.mass.second) == masses[i].second &&
              p.mass.weight == masses[i].weight,
          "Compiled immutable binding retains original exact enum/shape/mass "
          "graph");
    mass_sum += p.mass.weight;
    if (const auto* b =
            std::get_if<BoardingPlantedBodyBoxBinding>(&p.reservation)) {
      ++boxes;
      check(b->frame.columns == BoardingBodyFrame{}.columns,
            "WORLD boxes keep original full identity frames");
      if (i == 1)
        check(b->half_size_metres == RigidVector3{.26, .2695, .18},
              "WORLD trunk is full original box");
      if (i == 2)
        check(b->half_size_metres == RigidVector3{.16, .18, .18},
              "WORLD helmet is full original box");
      if (i == 5 || i == 11)
        check(b->half_size_metres == RigidVector3{.06, .05, .14},
              "Stationary soles preserve original boot extents");
    } else
      ++capsules;
  }
  check(mass_sum == 1200 && boxes == 7 && capsules == 8,
        "Original full mass and WORLD reservation roster is unchanged");
  const auto source =
      d.initial.self.endpoint.sites.source.selected_partitions();
  for (const auto& c : d.cells) {
    std::array<std::uint16_t, 6> counts{};
    for (const auto certificate : c.pair_certificates) {
      const auto kind = static_cast<std::size_t>(certificate);
      check(kind > 0 && kind < counts.size(),
            "Every accepted lexicographic self pair owns a genuine certificate "
            "kind");
      if (kind < counts.size()) ++counts[kind];
    }
    check(counts == c.certificate_counts,
          "Compact self family counts account for all105 pair decisions");
    for (double t :
         std::array{c.first, std::midpoint(c.first, c.last), c.last}) {
      corroborate(c, t, d.reverse, &d.initial.self);
      const auto o = oracle(t);
      for (std::size_t side = 0; side < 2; ++side) {
        const auto& p = c.pressures[side];
        check(p.complete && p.arithmetic_supported && p.coplanar &&
                  p.sole_disk_contained && p.source_disk_contained &&
                  p.coverage_complete && p.scanned_partitions == 10 &&
                  p.source_partition < source.size(),
              "Accepted pressure retains full source scan and both actual "
              "finite margins");
        if (p.source_partition >= source.size()) continue;
        const auto expected_plane =
            side == 0 ? kCabinCorridorFloorMetres : -160000.0 * 1e-6;
        check(source[p.source_partition].plane_metres == expected_plane,
              "Selected pressure source retains genuine fixed sole plane");
        const auto edge = pressure_edges(
            source[p.source_partition].perimeter_metres, o.pressure[side]);
        const auto& candidate = p.candidates[p.source_partition];
        contains(candidate.minimum_signed_side, edge[0]);
        contains(candidate.minimum_squared_gap, edge[1]);
        check(candidate.status ==
                      BoardingSourceEndpointLoadCandidateStatus::contained &&
                  candidate.arithmetic_supported && candidate.quad_valid,
              "Selected actual source quad owns contained pressure decision");
        const auto center_x = o.points[side == 0 ? 7 : 14][0].value;
        const auto center_z = o.points[side == 0 ? 7 : 14][2].value;
        const auto available_x =
            static_cast<long double>(.06) -
            (static_cast<long double>(.02) + static_cast<long double>(.01));
        const auto available_z =
            static_cast<long double>(.14) -
            (static_cast<long double>(.02) + static_cast<long double>(.01));
        check(std::abs(o.pressure[side][0].value - center_x) <=
                      available_x + uncertainty(center_x) &&
                  std::abs(o.pressure[side][1].value - center_z) <=
                      available_z + uncertainty(center_z),
              "Independent pressure disk fits original sole radius and edge "
              "margin");
      }
    }
  }
}
auto numeric_graph_controls() -> void {
  for (double t : std::array{0., .125, .5, .875, 1.}) {
    const auto independent = oracle(t);
    if (t == 0 || t == 1)
      for (const auto& p : independent.points)
        for (const auto& x : p)
          check(x.first == 0 && x.second == 0,
                "Independent nonsingular graph has exact endpoint C2 holds");
    for (bool reverse : std::array{false, true}) {
      Cell c;
      BoardingLowerFootTransferRefusal r;
      const auto result = detail::boarding_lower_foot_transfer_kinematic_math(
          t, t, reverse, c, r);
      check(result != Result::capacity,
            "Local compiled graph owns no cover/work-budget search");
      check(!c.self_complete && !c.nominal_vertical_equilibrium_complete &&
                !c.finite_pressure_complete && !c.complete,
            "Private graph arithmetic grants no self/source/load admission");
      if (c.kinematics_complete && c.timing_complete)
        corroborate(c, t, reverse);
      else
        check(r.condition != Condition::none,
              "Unqualified point graph retains actual predicate refusal");
    }
  }
  Cell whole;
  BoardingLowerFootTransferRefusal refusal;
  const auto result = detail::boarding_lower_foot_transfer_kinematic_math(
      0, 1, false, whole, refusal);
  if (result == Result::accepted)
    for (double t : std::array{0., .5, 1.})
      corroborate(whole, t, false);
  else
    check(!whole.complete && refusal.condition != Condition::none,
          "Clear samples never waive unresolved interval-middle graph "
          "uncertainty");
  // Source-reasoned negative inequalities remain unexecuted alternative poses.
  check(735.6L / 1200 < .6147L,
        "Fixed-Z COM necessary bound contradicts25/75 sole pressure ceiling");
  check(.5L * .1099L + .232L * .8469L > .23745L,
        "Fixed-Y60mm upper-ankle necessary bound contradicts "
        "unchanged30-degree sector");
}
auto reduced_cover_controls(const OriginBoardingBootSupport& p) -> void {
  for (std::size_t i = 0; i < 9; ++i) {
    Limits l;
    l.nodes = 7;
    l.depth = 2;
    l.leaves = 4;
    if (i == 0) l.depth = 0;
    if (i == 1) l.nodes = 1;
    if (i == 2) l.leaves = 0;
    if (i == 3) l.self_pairs = 0;
    if (i == 4) l.proposed_axes = 0;
    if (i == 5) l.signed_trials = 0;
    if (i == 6) l.pressure_candidates = 0;
    if (i == 7) l.disk_edges = 0;
    if (i == 8) {
      l.self_pairs = 105;
      l.proposed_axes = 14;
      l.signed_trials = 28;
      l.pressure_candidates = 20;
      l.disk_edges = 88;
    }
    const auto d =
        require(detail::boarding_lower_foot_transfer_bounded(p, 0, 1, l));
    prefix_invariants(d, l);
    if (l.leaves == 0)
      check(d.cells.empty() && d.examined_nodes == 0,
            "Zero leaf capacity refuses before unretained work");
  }
}
auto request_controls(const OriginBoardingBootSupport& p) -> void {
  for (auto interval : std::array{std::array{1., 0.}, std::array{.25, .75},
                                  std::array{.75, .25}, std::array{0., 0.},
                                  std::array{.5, .5}, std::array{1., 1.}}) {
    const auto d = require(assess_origin_boarding_lower_foot_transfer(
        p, interval[0], interval[1]));
    check(d.requested_first == interval[0] && d.requested_last == interval[1],
          "Output retains original request endpoints");
    check(d.complete && !d.first_refusal,
          "Observed qualified curve retains complete reverse, subinterval "
          "and point requests");
    inspect_cells(d);
  }
}
auto snapshot(const Diagnostic& d) -> Snapshot {
  auto s = snapshot(d.initial);
  const auto bindings = binding_snapshot(d.parts);
  s.values.insert(s.values.end(), bindings.values.begin(),
                  bindings.values.end());
  const auto evidence = [&](const BoardingPlantedBodyPointEvidence& e) {
    s.point(e.value);
    s.derivatives(e.derivatives);
  };
  const auto angular = [&](const BoardingPlantedLegAngularDerivatives& a) {
    s.scalar(a.rate);
    s.scalar(a.coordinate_second);
  };
  s.integer(d.transfer_version);
  s.number(d.requested_first);
  s.number(d.requested_last);
  s.number(d.seconds_per_parameter);
  s.number(d.reporting_elapsed_seconds);
  s.number(d.common_translation_y_metres);
  for (bool b :
       std::array{d.reverse, d.arithmetic_supported, d.kinematics_complete,
                  d.timing_complete, d.self_complete,
                  d.nominal_vertical_equilibrium_complete,
                  d.finite_pressure_complete, d.complete})
    s.integer(b);
  s.integer(d.examined_nodes);
  s.integer(d.maximum_depth);
  s.integer(d.checked_source_quads);
  s.integer(d.output_capacity_bytes);
  s.integer(d.work.self_pairs);
  s.integer(d.work.proposed_axes);
  s.integer(d.work.signed_trials);
  s.integer(d.work.pressure_candidates);
  s.integer(d.work.disk_edges);
  s.integer(d.cells.size());
  for (const auto& c : d.cells) {
    s.number(c.first);
    s.number(c.last);
    for (const auto& e : c.points)
      evidence(e);
    for (const auto& e : c.mass_points)
      evidence(e);
    evidence(c.center_of_mass);
    for (const auto& l : c.legs) {
      for (const auto& a :
           std::array{l.hip_pitch, l.shin_pitch, l.knee_flex, l.ankle_pitch,
                      l.hip_abduction, l.ankle_roll})
        angular(a);
      for (const auto& b : l.joint_speeds)
        s.scalar(b);
      for (const auto& b : l.margins)
        s.scalar(b);
      for (bool b :
           std::array{l.link_identities, l.plane_identity, l.branch_certified,
                      l.joint_sectors_certified, l.derivative_domains_certified,
                      l.joint_speeds_certified})
        s.integer(b);
    }
    s.scalar(c.root_speed);
    s.scalar(c.root_acceleration);
    for (const auto& p : c.pressures) {
      for (const auto& b : p.pressure_xz)
        s.scalar(b);
      s.scalar(p.sole_minimum_signed_side);
      s.scalar(p.sole_minimum_squared_gap);
      for (const auto& candidate : p.candidates) {
        s.scalar(candidate.minimum_signed_side);
        s.scalar(candidate.minimum_squared_gap);
        s.integer(candidate.status);
        s.integer(candidate.arithmetic_supported);
        s.integer(candidate.quad_valid);
      }
      s.integer(p.source_partition);
      s.integer(p.scanned_partitions);
      for (bool b :
           std::array{p.arithmetic_supported, p.coplanar, p.sole_disk_contained,
                      p.source_disk_contained, p.coverage_complete, p.complete})
        s.integer(b);
    }
    s.scalar(c.port_reaction_fraction);
    for (const auto& b : c.barycenter_xz)
      s.scalar(b);
    for (const auto& b : c.common_pressure_delta_xz)
      s.scalar(b);
    for (const auto& owner : c.owners) {
      s.scalar(owner.extent);
      s.scalar(owner.limit);
      s.scalar(owner.secondary_extent);
      s.integer(owner.certificate);
      s.integer(owner.arithmetic_supported);
      s.integer(owner.certified);
      s.integer(owner.structural_identity);
    }
    for (auto kind : c.pair_certificates)
      s.integer(kind);
    for (auto count : c.certificate_counts)
      s.integer(count);
    for (bool b :
         std::array{c.arithmetic_supported, c.kinematics_complete,
                    c.timing_complete, c.self_complete, c.positive_reactions,
                    c.nominal_vertical_equilibrium_complete,
                    c.finite_pressure_complete, c.complete})
      s.integer(b);
  }
  s.integer(d.first_refusal.has_value());
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    s.number(r.first);
    s.number(r.last);
    s.integer(r.depth);
    s.integer(r.condition);
    s.integer(r.predicate_condition);
    s.integer(r.side.has_value());
    if (r.side) s.integer(*r.side);
    s.integer(r.pair.has_value());
    if (r.pair) s.integer(*r.pair);
    s.integer(r.partition.has_value());
    if (r.partition) s.integer(*r.partition);
    s.scalar(r.limiting_bound);
  }
  return s;
}
auto work_at(const BoardingLowerFootTransferCounters& work, std::size_t index)
    -> std::uint64_t {
  if (index == 0) return work.self_pairs;
  if (index == 1) return work.proposed_axes;
  if (index == 2) return work.signed_trials;
  if (index == 3) return work.pressure_candidates;
  return work.disk_edges;
}
auto set_work_cap(Limits& limits, std::size_t index, std::uint64_t value)
    -> void {
  if (index == 0)
    limits.self_pairs = value;
  else if (index == 1)
    limits.proposed_axes = value;
  else if (index == 2)
    limits.signed_trials = value;
  else if (index == 3)
    limits.pressure_candidates = value;
  else
    limits.disk_edges = value;
}
auto set_stale_summaries(Cell& out) -> void {
  out.arithmetic_supported = out.kinematics_complete = out.timing_complete =
      out.self_complete = out.positive_reactions =
          out.nominal_vertical_equilibrium_complete =
              out.finite_pressure_complete = out.complete = true;
}
auto stale_output_controls(const Load& initial) -> void {
  const auto context =
      require(detail::prepare_boarding_lower_foot_transfer_pressure(initial));
  // A distinct owned instance exercises pressure-context identity refusal.
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Identity.
  const auto distinct = initial;
  Cell out;
  set_stale_summaries(out);
  BoardingLowerFootTransferCounters work;
  BoardingLowerFootTransferRefusal refusal;
  const auto result = detail::boarding_lower_foot_transfer_cell(
      distinct, context, 0, 0, false, Limits{}, work, out, refusal);
  check(result == Result::unsupported &&
            refusal.condition == Condition::invalid_binding &&
            !out.arithmetic_supported && !out.kinematics_complete &&
            !out.timing_complete && !out.self_complete &&
            !out.positive_reactions &&
            !out.nominal_vertical_equilibrium_complete &&
            !out.finite_pressure_complete && !out.complete,
        "Mismatched genuine initial-object context refuses and clears every "
        "stale summary before math");
  check(work.self_pairs == 0 && work.proposed_axes == 0 &&
            work.signed_trials == 0 && work.pressure_candidates == 0 &&
            work.disk_edges == 0,
        "Invalid compound context consumes no predicate work");
  {
    SavedEnvironment saved;
    check(std::fesetround(FE_UPWARD) == 0,
          "Install unsafe environment for reused pressure record");
    set_stale_summaries(out);
    refusal = {};
    const auto pressure = detail::boarding_lower_foot_transfer_pressure_cell(
        context, Limits{}, work, out, refusal);
    check(pressure == Result::unsupported &&
              refusal.condition == Condition::unsupported_arithmetic &&
              !out.arithmetic_supported && !out.positive_reactions &&
              !out.nominal_vertical_equilibrium_complete &&
              !out.finite_pressure_complete && !out.complete,
          "Unsafe pressure environment refuses and clears stale "
          "arithmetic/load completion before shortcuts");
    check(work.self_pairs == 0 && work.proposed_axes == 0 &&
              work.signed_trials == 0 && work.pressure_candidates == 0 &&
              work.disk_edges == 0,
          "Unsafe pressure environment consumes no geometric or source work");
  }
}

auto point_stage_capacity_controls(const OriginBoardingBootSupport& p) -> void {
  // This original t=0 stance is independently qualified by the unchanged child.
  // It is not a prediction that the whole new trajectory qualifies.
  const auto baseline =
      require(assess_origin_boarding_lower_foot_transfer(p, 0, 0));
  check(baseline.complete && baseline.cells.size() == 1 &&
            baseline.examined_nodes == 1,
        "Same exact initial stance qualifies a full general-3D point packet");
  inspect_cells(baseline);
  if (!baseline.complete) return;
  const auto original = snapshot(baseline);
  Cell numeric;
  BoardingLowerFootTransferRefusal numeric_refusal;
  check(detail::boarding_lower_foot_transfer_kinematic_math(
            0, 0, false, numeric, numeric_refusal) == Result::accepted &&
            numeric.kinematics_complete && numeric.timing_complete,
        "Authentic initial point independently reaches complete general graph "
        "and C2 evidence");
  corroborate(numeric, 0, false);
  constexpr std::array conditions{
      Condition::pair_capacity, Condition::axis_capacity,
      Condition::signed_trial_capacity, Condition::pressure_capacity,
      Condition::edge_capacity};
  for (std::size_t i = 0; i < conditions.size(); ++i) {
    Limits limits;
    limits.depth = 0;
    limits.nodes = 1;
    limits.leaves = 1;
    set_work_cap(limits, i, 0);
    const auto d =
        require(detail::boarding_lower_foot_transfer_bounded(p, 0, 0, limits));
    prefix_invariants(d, limits);
    check(!d.complete && d.cells.empty() && d.examined_nodes == 1 &&
              d.initial.load.complete && d.first_refusal &&
              d.first_refusal->condition == conditions[i] &&
              d.first_refusal->predicate_condition == conditions[i] &&
              work_at(d.work, i) == 0,
          "Zero predicate budget is reached at authentic point and stops "
          "before first attempted operation");
    if (i == 0)
      check(d.work.proposed_axes == 0 && d.work.signed_trials == 0 &&
                d.work.pressure_candidates == 0 && d.work.disk_edges == 0,
            "Zero pair budget prevents every downstream stage");
    if (i == 1)
      check(d.work.self_pairs > 0 && d.work.signed_trials == 0 &&
                d.work.pressure_candidates == 0,
            "Zero axis budget reaches self pair stage before separator trials");
    if (i == 2)
      check(d.work.self_pairs > 0 && d.work.proposed_axes > 0 &&
                d.work.pressure_candidates == 0,
            "Zero signed-trial budget reaches genuine generated direction");
    if (i == 3)
      check(d.work.self_pairs == 105 && d.work.disk_edges == 4,
            "Zero pressure-candidate budget follows complete self and first "
            "authentic four sole edges");
    if (i == 4)
      check(d.work.self_pairs == 105 && d.work.pressure_candidates == 0,
            "Zero disk-edge budget reaches pressure phase after full self "
            "without candidate work");
  }
  Limits exact;
  exact.depth = baseline.maximum_depth;
  exact.nodes = baseline.examined_nodes;
  exact.leaves = baseline.cells.size();
  for (std::size_t i = 0; i < conditions.size(); ++i)
    set_work_cap(exact, i, work_at(baseline.work, i));
  const auto equal =
      require(detail::boarding_lower_foot_transfer_bounded(p, 0, 0, exact));
  prefix_invariants(equal, exact);
  check(snapshot(equal) == original,
        "Exact performed full-point capacities preserve every field-wise "
        "value, decision and count");
  for (std::size_t i = 0; i < conditions.size(); ++i) {
    const auto used = work_at(baseline.work, i);
    if (used == 0) continue;
    auto lower = exact;
    set_work_cap(lower, i, used - 1);
    const auto d =
        require(detail::boarding_lower_foot_transfer_bounded(p, 0, 0, lower));
    prefix_invariants(d, lower);
    check(!d.complete && d.cells.empty() && d.first_refusal &&
              d.first_refusal->condition == conditions[i] &&
              d.first_refusal->predicate_condition == conditions[i] &&
              work_at(d.work, i) == used - 1,
          "One-less genuinely used point budget refuses at exact affected "
          "capacity without fitting");
  }
}

auto lifetime_controls(const NativeCraftBinding& binding) -> void {
  const auto returned = [&] {
    const auto local = require(make_origin_boarding_boot_support(binding));
    Limits l;
    l.nodes = 0;
    return require(
        detail::boarding_lower_foot_transfer_bounded(local, 0, 1, l));
  }();
  const auto original = snapshot(returned.initial);
  auto copy = returned;
  auto moved = std::move(copy);
  check(snapshot(moved.initial) == original,
        "Diagnostic move retains complete child snapshots and source names");
  const auto source =
      moved.initial.self.endpoint.sites.source.selected_partitions();
  check(source.size() == 10 &&
            moved.initial.self.endpoint.sites.source.contact() != nullptr,
        "Owned child source survives local provider lifetime and diagnostic "
        "movement");
  for (const auto& q : source)
    for (const auto& f : q.faces)
      check(!f.source_object.empty(),
            "Original selected face names survive provider lifetime");
  moved.initial.load.complete = !moved.initial.load.complete;
  check(snapshot(returned.initial) == original,
        "Mutable report copy cannot alter owned sibling evidence");
  const auto fresh = require(assess_origin_boarding_source_endpoint_load(
      moved.initial.self.endpoint.sites.source));
  check(snapshot(fresh) == original, "Copied report mutation cannot alter "
                                     "immutable source or fresh old query");
}
} // namespace
int main() {
  try {
    static_assert(sizeof(Cell) <= kBoardingLowerFootTransferMaximumCellBytes);
    static_assert(sizeof(Cell) * kBoardingLowerFootTransferMaximumLeaves <=
                  kBoardingLowerFootTransferMaximumOutputBytes);
    if (std::numeric_limits<long double>::digits < 64)
      throw std::runtime_error("Independent oracle requires64 mantissa bits");
    const auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    const auto provider = require(make_origin_boarding_boot_support(binding));
    invalid_controls(provider);
    environment_controls(provider);
    preliminary_cap_controls(provider);
    const auto initial =
        require(assess_origin_boarding_source_endpoint_load(provider));
    const auto unchanged = snapshot(initial);
    quad_and_pressure_controls(provider, initial);
    // This whole-candidate observation is printed BEFORE any outcome assertion.
    const auto actual = observe(provider);
    check(actual.complete && !actual.first_refusal,
          "First observed complete partial transfer remains qualified");
    check(snapshot(actual.initial) == unchanged,
          "New query owns exactly unchanged initial Load/Self/Endpoint/Sites "
          "chain");
    inspect_cells(actual);
    stale_output_controls(initial);
    point_stage_capacity_controls(provider);
    numeric_graph_controls();
    reduced_cover_controls(provider);
    request_controls(provider);
    lifetime_controls(binding);
    check(snapshot(require(assess_origin_boarding_source_endpoint_load(
              provider))) == unchanged,
          "New math, requests and lifetime controls leave original API bitwise "
          "evidence unchanged");
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout << checks << " lower-foot transfer checks, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
