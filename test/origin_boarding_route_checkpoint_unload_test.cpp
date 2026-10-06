#include "apsis_drift/origin_boarding_route_checkpoint_unload.hpp"
#include "origin_boarding_route_checkpoint_unload_internal.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#if defined(__SSE2__) && defined(__x86_64__)
#include <xmmintrin.h>
#endif
namespace {
using namespace apsis_drift;
using Diagnostic = BoardingRouteCheckpointUnloadDiagnostic;
using Cell = BoardingRouteCheckpointUnloadCell;
using Request = BoardingRouteFootPhaseRequest;
using Limits = detail::BoardingRouteCheckpointUnloadLimits;
using Condition = BoardingRoutePortUnloadCondition;
using State = detail::BoardingRouteCheckpointUnloadCellResult;
using Bounds = BoardingPlantedLegScalarBounds;
std::size_t checks{};
int failures{};
void check(bool valid, std::string_view why) {
  ++checks;
  if (!valid) {
    ++failures;
    std::cerr << "FAIL: " << why << '\n';
  }
}
template <class T, class E> auto require(std::expected<T, E> r) -> T {
  if (!r)
    throw std::runtime_error("Required checkpoint unload test API refused");
  return std::move(*r);
}
auto component(RigidVector3 p, std::size_t i) -> double {
  return i == 0 ? p.x : i == 1 ? p.y : p.z;
}
// Independent long-double second-order AD; the oracle reconstructs the
// unfactored two-sphere knee intersection and actual full mass graph.
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
auto inverse(Jet a) -> Jet {
  const auto q = 1 / a.value;
  return {q, -a.first * q * q,
          2 * a.first * a.first * q * q * q - a.second * q * q};
}
auto operator/(Jet a, Jet b) -> Jet {
  return a * inverse(b);
}
auto scalar(double v) -> Jet {
  return {v, 0, 0};
}
auto root(Jet a) -> Jet {
  const auto q = std::sqrt(a.value);
  return {q, a.first / (2 * q),
          a.second / (2 * q) - a.first * a.first / (4 * q * q * q)};
}
auto sin_jet(Jet a) -> Jet {
  return {std::sin(a.value), std::cos(a.value) * a.first,
          std::cos(a.value) * a.second - std::sin(a.value) * a.first * a.first};
}
auto cos_jet(Jet a) -> Jet {
  return {std::cos(a.value), -std::sin(a.value) * a.first,
          -std::sin(a.value) * a.second -
              std::cos(a.value) * a.first * a.first};
}
using Point = std::array<Jet, 3>;
auto add(Point a, Point b) -> Point {
  for (std::size_t i = 0; i < 3; ++i)
    a[i] = a[i] + b[i];
  return a;
}
auto subtract(Point a, Point b) -> Point {
  for (std::size_t i = 0; i < 3; ++i)
    a[i] = a[i] - b[i];
  return a;
}
auto scale(Point a, Jet s) -> Point {
  for (auto& v : a)
    v = v * s;
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
  return {scalar(x), scalar(y), scalar(z)};
}
auto rotate(Point p, Jet c, Jet s) -> Point {
  return {c * p[0] + s * p[2], p[1], -s * p[0] + c * p[2]};
}
auto sum(const BoardingRoutePhaseConstant& v) -> Jet {
  Jet out;
  for (std::size_t i = 0; i < v.count; ++i)
    out = out + scalar(v.terms[i]);
  return out;
}
struct Mass {
  std::size_t a{}, b{};
  std::uint32_t weight{};
};
constexpr std::array<Mass, 15> mass{{{0, 0, 144},
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
  std::array<Point, 15> masses{};
  Point com{};
  Jet weight{}, cosine{}, sine{};
};
auto oracle(const Request& r, double u, bool reverse) -> Oracle {
  Oracle out;
  const Jet t{u, (reverse ? -1.L : 1.L) / 12, 0};
  const auto S = t * t * t * (scalar(10) - scalar(15) * t + scalar(6) * t * t);
  auto lerp = [&](Jet a, Jet b) { return a + (b - a) * S; };
  auto& p = out.points;
  for (std::size_t axis = 0; axis < 3; ++axis)
    p[0][axis] = lerp(sum(r.root[0].coordinates[axis]),
                      sum(r.root[1].coordinates[axis]));
  const auto q = lerp(scalar(r.root_yaw_half[0]), scalar(r.root_yaw_half[1]));
  const auto denominator = 1 + q.value * q.value;
  const Jet angle{
      2 * std::atan(q.value), 2 * q.first / denominator,
      2 * (q.second * denominator - 2 * q.value * q.first * q.first) /
          (denominator * denominator)};
  out.cosine = cos_jet(angle);
  out.sine = sin_jet(angle);
  out.weight = lerp(scalar(r.port_reaction_fraction[0]),
                    scalar(r.port_reaction_fraction[1]));
  for (std::size_t i = 1; i < 4; ++i)
    p[i] = add(p[0], constant_point(0,
                                    i == 1   ? .3495
                                    : i == 2 ? .70237
                                             : .65237,
                                    0));
  const auto l1 = scalar(.47285), l2 = scalar(.47478),
             arm = -scalar(.35898) * root(scalar(.5));
  for (std::size_t side = 0; side < 2; ++side) {
    const auto base = side == 0 ? 4U : 11U;
    const auto sign = side == 0 ? -1. : 1.;
    p[base] = add(
        p[0], rotate(constant_point(sign * .14, 0, 0), out.cosine, out.sine));
    Point sole;
    for (std::size_t axis = 0; axis < 3; ++axis)
      sole[axis] = sum(r.feet[side].sole[0].coordinates[axis]);
    p[base + 2] = add(sole, constant_point(0, .10, 0));
    p[base + 3] = add(sole, constant_point(0, .05, 0));
    const auto d = subtract(p[base + 2], p[base]);
    const auto rho = root(d[0] * d[0] + d[1] * d[1]), D = dot(d, d),
               alpha = (l1 * l1 - l2 * l2 + D) / (scalar(2) * D),
               gamma = root((l1 * l1 - alpha * alpha * D) / D);
    const Point U{-d[0] / rho, -d[1] / rho, scalar(0)},
        N{U[1], -U[0], scalar(0)};
    p[base + 1] = add(p[base], add(scale(d, alpha), scale(cross(N, d), gamma)));
    p[base + 4] = add(p[0], rotate(constant_point(sign * .20265, .579, 0),
                                   out.cosine, out.sine));
    p[base + 5] = add(p[base + 4],
                      rotate(Point{scalar(0), arm, arm}, out.cosine, out.sine));
    p[base + 6] = add(p[base + 5], constant_point(0, .386, 0));
  }
  for (std::size_t i = 0; i < 15; ++i) {
    out.masses[i] = scale(add(p[mass[i].a], p[mass[i].b]), scalar(.5));
    out.com = add(out.com, scale(out.masses[i], scalar(mass[i].weight)));
  }
  out.com = scale(out.com, scalar(1) / scalar(1200));
  return out;
}
auto contains(Bounds b, long double v) -> bool {
  const auto roundoff = 2e-12L * std::max(1.L, std::abs(v));
  return std::isfinite(b.lower) && std::isfinite(b.upper) &&
         b.lower <= b.upper && b.lower <= v + roundoff &&
         b.upper >= v - roundoff;
}
void point_contains(const BoardingPlantedBodyPointEvidence& b, const Point& p) {
  for (std::size_t i = 0; i < 3; ++i) {
    check(contains({component(b.value.lower, i), component(b.value.upper, i)},
                   p[i].value),
          "Independent actual body coordinate enclosed");
    check(
        contains({component(b.derivatives.velocity.lower, i),
                  component(b.derivatives.velocity.upper, i)},
                 p[i].first),
        "Local/T12 physical first derivative enclosed without global rescale");
    check(contains({component(b.derivatives.acceleration.lower, i),
                    component(b.derivatives.acceleration.upper, i)},
                   p[i].second),
          "Local/T12 squared physical second derivative enclosed");
  }
}
auto clock(double g) -> double {
  if (g <= .25) return 48 * g;
  if (g <= .5) return 12 + 48 * (g - .25);
  return 24 + 24 * (g - .5);
}
auto local(std::size_t index, double g) -> double {
  return index == 0 ? 4 * g : index == 1 ? 4 * g - 1 : 2 * g - 1;
}
auto phase_at(double g) -> std::size_t {
  return g < .25 ? 0U : g < .5 ? 1U : 2U;
}
void denied(const Diagnostic& d) {
  check(!d.world_qualified && !d.material_qualified &&
            !d.source_surface_sweep_qualified && !d.route_qualified &&
            !d.actor_qualified && !d.seat_qualified && !d.save_qualified &&
            !d.first_flight_qualified && !d.free_foot_swing_qualified &&
            !d.dynamics_qualified && !d.strength_qualified &&
            !d.friction_qualified,
        "Checkpoint nominal support grants no whole-route, world, material or "
        "actor authority");
}
struct Fingerprint {
  std::vector<std::uint64_t> numbers;
  std::vector<std::string> names;
  auto operator==(const Fingerprint&) const -> bool = default;
  void integer(std::uint64_t n) { numbers.push_back(n); }
  void number(double v) { integer(std::bit_cast<std::uint64_t>(v)); }
  void scalar(Bounds b) {
    number(b.lower);
    number(b.upper);
  }
  void vector(RigidVector3 v) {
    number(v.x);
    number(v.y);
    number(v.z);
  }
  void point(BoardingPlantedLegPointBounds b) {
    vector(b.lower);
    vector(b.upper);
  }
};
auto child_fingerprint(const BoardingSourceEndpointLoadDiagnostic& d)
    -> Fingerprint {
  Fingerprint s;
  const auto& e = d.self.endpoint;
  const auto& sites = e.sites;
  s.integer(e.endpoint_version);
  s.number(e.common_translation_y_metres);
  s.number(e.root_z_metres);
  for (auto x : e.port_x_terms)
    s.number(x);
  for (auto x : e.starboard_x_terms)
    s.number(x);
  for (const auto& part : e.parts) {
    s.integer(static_cast<std::size_t>(part.id));
    s.integer(static_cast<std::size_t>(part.mass.first));
    s.integer(static_cast<std::size_t>(part.mass.second));
    s.integer(part.mass.weight);
    if (const auto* b =
            std::get_if<BoardingPlantedBodyBoxBinding>(&part.reservation)) {
      s.integer(0);
      s.integer(static_cast<std::size_t>(b->center));
      s.vector(b->half_size_metres);
      for (auto c : b->frame.columns)
        s.vector(c);
    } else {
      const auto& c =
          std::get<BoardingPlantedBodyCapsuleBinding>(part.reservation);
      s.integer(1);
      s.integer(static_cast<std::size_t>(c.start));
      s.integer(static_cast<std::size_t>(c.end));
      s.number(c.radius_metres);
    }
  }
  s.integer(e.body.has_value());
  if (e.body) {
    for (auto p : e.body->points)
      s.point(p);
    for (auto p : e.body->mass_points)
      s.point(p);
    s.point(e.body->center_of_mass);
  }
  for (const auto& leg : e.legs) {
    for (auto p : std::array{leg.hip, leg.knee, leg.ankle, leg.boot_center})
      s.point(p);
    for (auto b : std::array{leg.distance_squared, leg.rho, leg.alpha,
                             leg.gamma, leg.hip_sine, leg.hip_cosine,
                             leg.shin_sine, leg.shin_cosine, leg.knee_cosine,
                             leg.roll_sine, leg.roll_cosine})
      s.scalar(b);
    for (auto x : leg.ankle_y_terms)
      s.number(x);
    for (auto x : leg.boot_center_y_terms)
      s.number(x);
    s.number(leg.plane_metres);
    s.integer(leg.plane_identity);
    s.integer(leg.link_identities);
    s.integer(leg.limits_certified);
  }
  for (const auto& p : sites.source.selected_partitions()) {
    s.number(p.plane_metres);
    for (auto v : p.perimeter_metres)
      s.vector(v);
    for (const auto& f : p.faces) {
      s.integer(static_cast<std::size_t>(f.key.buffer));
      s.integer(f.key.group);
      s.integer(f.key.triangle);
      s.integer(f.object);
      s.names.emplace_back(f.source_object);
      for (auto v : f.points_current_metres)
        s.vector(v);
      s.vector(f.unit_normal_current);
    }
  }
  for (const auto& site : sites.sites) {
    for (auto x : site.center_x_terms)
      s.number(x);
    for (auto x : site.center_y_terms)
      s.number(x);
    for (auto x : site.center_z_terms)
      s.number(x);
    for (auto terms : site.pressure_offset_terms)
      for (auto x : terms)
        s.number(x);
    s.vector(site.half_size_metres);
    for (auto v : site.center_bounds_metres)
      s.vector(v);
    for (auto v : site.pressure_bounds_metres)
      s.vector(v);
    s.integer(site.scanned_partitions);
    s.integer(site.eligible);
    s.integer(site.placement_nonpenetrating);
  }
  for (const auto& p : d.self.pairs) {
    s.integer(static_cast<std::size_t>(p.first));
    s.integer(static_cast<std::size_t>(p.second));
    s.integer(static_cast<std::size_t>(p.certificate));
    s.scalar(p.certificate_gap);
    s.scalar(p.ownership_extent);
    s.scalar(p.ownership_limit);
    s.scalar(p.ownership_secondary_extent);
    s.vector(p.separating_direction);
    s.integer(p.examined_axes);
    s.integer(p.signed_support_trials);
    s.integer(p.certified);
    s.integer(p.structural_identity);
  }
  const auto scalar_evidence = [&](BoardingFootSiteScalarBounds b) {
    s.number(b.lower);
    s.number(b.upper);
    s.integer(b.supported);
  };
  for (const auto& q : d.load.source_quads) {
    scalar_evidence(q.minimum_edge_squared);
    scalar_evidence(q.minimum_signed_side);
    s.integer(q.checked_edges);
    s.integer(q.checked_side_signs);
    s.integer(q.valid);
    s.integer(q.horizontal);
    s.integer(q.upward);
    s.integer(q.convex);
  }
  for (const auto& p : d.load.pressures) {
    s.number(p.plane_metres);
    for (auto v : p.pressure_bounds_metres)
      s.vector(v);
    for (auto k : p.source_keys) {
      s.integer(static_cast<std::size_t>(k.buffer));
      s.integer(k.group);
      s.integer(k.triangle);
    }
    for (const auto& edge : p.sole_edges) {
      scalar_evidence(edge.signed_side);
      scalar_evidence(edge.squared_margin_gap);
      scalar_evidence(edge.edge_length_squared);
      s.integer(edge.disk_contained);
    }
    for (const auto& edge : p.source_edges) {
      scalar_evidence(edge.signed_side);
      scalar_evidence(edge.squared_margin_gap);
      scalar_evidence(edge.edge_length_squared);
      s.integer(edge.disk_contained);
    }
    for (const auto& c : p.candidates) {
      scalar_evidence(c.minimum_signed_side);
      scalar_evidence(c.minimum_squared_gap);
      s.integer(static_cast<std::size_t>(c.status));
      s.integer(c.arithmetic_supported);
      s.integer(c.quad_valid);
    }
    s.integer(p.scanned_partitions);
    s.integer(p.complete);
  }
  for (bool b : std::array{
           e.complete, d.self.complete, sites.eligible, sites.coverage_complete,
           d.load.bindings_complete, d.load.arithmetic_supported,
           d.load.positive_reactions, d.load.vertical_force_identity,
           d.load.vertical_moment_identity, d.load.load_qualified,
           d.load.complete})
    s.integer(b);
  return s;
}
auto edges(const std::array<RigidVector3, 4>& quad,
           std::array<long double, 2> pressure) -> std::array<long double, 2> {
  std::array out{std::numeric_limits<long double>::infinity(),
                 std::numeric_limits<long double>::infinity()};
  const auto radius =
      static_cast<long double>(.02) + static_cast<long double>(.01);
  for (std::size_t i = 0; i < 4; ++i) {
    const auto a = quad[i], b = quad[(i + 1) % 4];
    const long double x = static_cast<long double>(b.x) - a.x,
                      z = static_cast<long double>(b.z) - a.z,
                      side = z * (pressure[0] - a.x) - x * (pressure[1] - a.z);
    out[0] = std::min(out[0], side);
    out[1] = std::min(out[1], side * side - radius * radius * (x * x + z * z));
  }
  return out;
}
// Original phase and general rotated105/14 SELF kernel arithmetic remains
// independently exercised by origin_boarding_route_foot_phase_test and
// origin_boarding_route_port_unload_test (required existing native targets).
// Here we check each reused decision, and independently rebuild the newly
// selected three-phase body, full COM and actual finite source disk geometry.
void inspect_cell(const Diagnostic& d, const Cell& cell) {
  const auto& c = cell.assessment;
  const auto& p = c.phase;
  check(cell.phase_index < 3 && cell.phase_index == phase_at(cell.global_first),
        "Every accepted cell belongs to its immutable phase with exact "
        "right-hand join tie");
  if (cell.phase_index >= 3) return;
  check(p.first == local(cell.phase_index, cell.global_first) &&
            p.last == local(cell.phase_index, cell.global_last),
        "Global cover endpoints map exactly to embedded local endpoints");
  check(c.complete && c.arithmetic_supported && p.complete && c.self_complete &&
            c.nonnegative_reactions &&
            c.nominal_vertical_equilibrium_complete &&
            c.finite_pressure_complete,
        "One same-cell graph/self/finite support certificate is complete");
  std::array<std::uint16_t, 6> counts{};
  for (auto cert : c.pair_certificates) {
    const auto kind = static_cast<std::size_t>(cert);
    check(kind > 0 && kind < counts.size(),
          "All105 actual pairs retain a genuine self certificate");
    if (kind < counts.size()) ++counts[kind];
  }
  check(counts == c.certificate_counts,
        "Compact pair certificate counts preserve whole original105 graph");
  for (std::size_t i = 0; i < 14; ++i) {
    const auto& o = c.owners[i];
    bool separated = false;
    for (std::size_t pair = 0; pair < 105; ++pair)
      if (d.initial->self.pairs[pair].connected_region_index == i)
        separated = c.pair_certificates[pair] ==
                    BoardingSourceEndpointSelfCertificate::convex_support_plane;
    check(
        o.arithmetic_supported && (o.certified || separated),
        "All14 original connected regions have finite ownership or separation");
  }
  const auto source =
      d.initial->self.endpoint.sites.source.selected_partitions();
  for (double u : std::array{p.first, std::midpoint(p.first, p.last), p.last}) {
    const auto o = oracle(d.controls[cell.phase_index], u, d.reverse);
    for (std::size_t i = 0; i < 18; ++i)
      point_contains(p.points[i], o.points[i]);
    for (std::size_t i = 0; i < 15; ++i)
      point_contains(p.mass_points[i], o.masses[i]);
    point_contains(p.center_of_mass, o.com);
    check(contains(c.port_reaction_fraction, o.weight.value),
          "Immutable selected phase reaction profile retains actual weight");
    if (cell.phase_index == 1)
      check(c.port_reaction_fraction.lower == .25 &&
                c.port_reaction_fraction.upper == .25,
            "Pivot has exactly constant quarter port share, never old "
            "local1-zero semantics");
    for (std::size_t axis = 0; axis < 2; ++axis) {
      const auto coord = axis == 0 ? 0U : 2U;
      const auto center0 = o.points[7][coord].value,
                 center1 = o.points[14][coord].value,
                 B = o.weight.value * center0 + (1 - o.weight.value) * center1,
                 delta = o.com[coord].value - B;
      check(contains(c.barycenter_xz[axis], B) &&
                contains(c.common_pressure_delta_xz[axis], delta),
            "Independent asymmetric full COM barycenter and shared delta "
            "remain enclosed");
      for (std::size_t side = 0; side < 2; ++side)
        check(contains(c.pressures[side].pressure_xz[axis],
                       o.points[side == 0 ? 7 : 14][coord].value + delta),
              "Derived pressure excludes original geometric40mm port offset");
    }
    for (std::size_t side = 0; side < 2; ++side) {
      const auto& pressure = c.pressures[side];
      check(pressure.complete && pressure.source_disk_contained &&
                pressure.sole_disk_contained &&
                pressure.scanned_partitions == 10 &&
                pressure.source_partition < source.size(),
            "Every derived pressure keeps complete actual source scan and both "
            "disks");
      if (pressure.source_partition >= source.size()) continue;
      std::array<long double, 2> pressure_point{};
      for (std::size_t axis = 0; axis < 2; ++axis) {
        const auto coord = axis == 0 ? 0U : 2U;
        const auto B = o.weight.value * o.points[7][coord].value +
                       (1 - o.weight.value) * o.points[14][coord].value;
        pressure_point[axis] =
            o.points[side == 0 ? 7 : 14][coord].value + o.com[coord].value - B;
      }
      for (std::size_t partition = 0; partition < source.size(); ++partition)
        if (source[partition].plane_metres != d.controls[cell.phase_index]
                                                  .feet[side]
                                                  .sole[0]
                                                  .coordinates[1]
                                                  .terms[0])
          check(pressure.candidates[partition].status ==
                    BoardingSourceEndpointLoadCandidateStatus::noncoplanar,
                "Every actual wrong-height source quad is retained as "
                "ineligible");
      const auto minima = edges(
          source[pressure.source_partition].perimeter_metres, pressure_point);
      const auto& selected = pressure.candidates[pressure.source_partition];
      check(contains(selected.minimum_signed_side, minima[0]) &&
                contains(selected.minimum_squared_gap, minima[1]),
            "Original finite tapered source edge and squared disk gaps "
            "corroborated independently");
      check(source[pressure.source_partition].plane_metres ==
                d.controls[cell.phase_index]
                    .feet[side]
                    .sole[0]
                    .coordinates[1]
                    .terms[0],
            "Selected actual quad remains on genuine unchanged sole plane");
    }
  }
  check(c.endpoint_zero_port_reaction == (cell.phase_index == 2 && p.last == 1),
        "Only finish/global1 carries endpoint zero witness, not preparation or "
        "pivot local1");
  if (p.first == p.last && (p.first == 0 || p.first == 1))
    for (const auto& point : p.points)
      for (const auto& bounds : std::array{point.derivatives.velocity,
                                           point.derivatives.acceleration})
        check(bounds.lower == RigidVector3{} && bounds.upper == RigidVector3{},
              "Every actual boundary body has exact C2 physical zero jets");
}
void controls(const Diagnostic& d) {
  const std::array<std::array<double, 2>, 3> yaw{
      {{0, 0}, {0, .25}, {.25, .25}}},
      weight{{{.5, .25}, {.25, .25}, {.25, 0}}};
  for (std::size_t i = 0; i < 3; ++i) {
    const auto& r = d.controls[i];
    check(r.seconds_per_parameter == 12 && r.root_yaw_half == yaw[i] &&
              r.port_reaction_fraction == weight[i] &&
              r.torso_lean_half == std::array<double, 2>{0, 0},
          "Each immutable stage retains exact selected yaw/share and "
          "original12 second duration");
    for (const auto& foot : r.feet)
      check(foot.sole[0].coordinates[0].terms ==
                    foot.sole[1].coordinates[0].terms &&
                foot.sole[0].coordinates[1].terms ==
                    foot.sole[1].coordinates[1].terms &&
                foot.sole[0].coordinates[2].terms ==
                    foot.sole[1].coordinates[2].terms &&
                foot.yaw_half == std::array<double, 2>{0, 0} &&
                foot.swing_height_metres == 0,
            "Both feet remain structural original stationary expression "
            "identities");
  }
  for (std::size_t join = 0; join < 2; ++join)
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const auto &a = d.controls[join].root[1].coordinates[axis],
                 &b = d.controls[join + 1].root[0].coordinates[axis];
      check(a.count == b.count && a.terms == b.terms,
            "Exact active stored checkpoint terms are shared across joins");
    }
  for (std::size_t endpoint = 0; endpoint < 2; ++endpoint)
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const auto& b = d.controls[1].root[endpoint].coordinates[axis];
      const std::array<std::array<double, 3>, 3> expected{
          {{.16, .07, 0}, {.847, .012, 0}, {-.55, -.04, 0}}};
      check(b.count == 2 && b.terms == expected[axis],
            "Pivot holds authentic B terms rather than rounded checkpoint "
            "reports");
    }
  std::uint32_t total{};
  std::size_t boxes{}, capsules{};
  for (std::size_t i = 0; i < 15; ++i) {
    const auto& p = d.parts[i];
    total += p.mass.weight;
    check(static_cast<std::size_t>(p.id) == i &&
              p.mass.weight == mass[i].weight &&
              static_cast<std::size_t>(p.mass.first) == mass[i].a &&
              static_cast<std::size_t>(p.mass.second) == mass[i].b,
          "All original15 part and1200 mass links retained");
    if (const auto* b =
            std::get_if<BoardingRoutePhaseBoxBinding>(&p.reservation)) {
      ++boxes;
      const RigidVector3 half = i == 0   ? RigidVector3{.24, .12, .18}
                                : i == 1 ? RigidVector3{.26, .2695, .18}
                                : i == 2 ? RigidVector3{.16, .18, .18}
                                : (i == 5 || i == 11)
                                    ? RigidVector3{.06, .05, .14}
                                    : RigidVector3{.04, .05, .02};
      check(b->half_size_metres == half,
            "Full original WORLD boxes survive rotated SELF method");
    } else
      ++capsules;
  }
  check(total == 1200 && boxes == 7 && capsules == 8,
        "Original full WORLD roster remains seven boxes and eight capsules");
}
void accounting(const Diagnostic& d, const Limits& l = {}) {
  denied(d);
  check(d.initial != nullptr,
        "Checkpoint owns one genuine unchanged original child");
  if (!d.initial) return;
  controls(d);
  if (d.initial->load.complete)
    check(d.owned_initial_quad_guards == 10 &&
              d.owned_initial_quad_edges == 40 &&
              d.owned_initial_convexity_signs == 80 &&
              d.owned_initial_triangle_windings == 20 &&
              d.owned_initial_diagonal_incidence_guards == 10,
          "Single retained original child owns all original ten quad guards "
          "once");
  check(d.examined_nodes <= l.phase.nodes && d.maximum_depth <= l.phase.depth &&
            d.mandatory_splits <= d.examined_nodes &&
            d.work.phase.graphs <= d.examined_nodes - d.mandatory_splits &&
            d.cells.size() <= l.phase.leaves &&
            d.output_capacity_bytes <= l.output_bytes,
        "Mandatory split nodes consume shared depth/work without fabricated "
        "graph operations");
  const auto& w = d.work;
  check(
      w.contact.self_pairs <= l.pairs && w.contact.proposed_axes <= l.axes &&
          w.contact.signed_trials <= l.signed_trials &&
          w.ownership_attempts <= l.owners &&
          w.contact.pressure_candidates <= l.candidates &&
          w.contact.disk_edges <= l.edges,
      "All actually accumulated stage work respects single global capacities");
  check(w.contact.self_pairs <= 105 * w.phase.graphs &&
            w.contact.proposed_axes <= 14 * w.contact.self_pairs &&
            w.contact.signed_trials <= 2 * w.contact.proposed_axes &&
            w.ownership_attempts <= 14 * w.phase.graphs &&
            w.contact.pressure_candidates <= 20 * w.phase.graphs &&
            w.contact.disk_edges <= 88 * w.phase.graphs,
        "Self/pressure work never resets or invents operations at phase "
        "switches");
  check(d.reporting_elapsed_seconds ==
            std::abs(clock(d.requested_last) - clock(d.requested_first)),
        "Mixed/reverse elapsed uses piecewise physical clock rather than36g");
  double cursor = std::min(d.requested_first, d.requested_last);
  std::array<bool, 2> joins{};
  for (std::size_t i = 0; i < d.cells.size(); ++i) {
    const auto& c = d.cells[i];
    check(c.global_first == cursor && c.global_first <= c.global_last,
          "Accepted prefix is exactly closed in GLOBAL coordinates");
    cursor = c.global_last;
    inspect_cell(d, c);
    if (i > 0) {
      const auto& previous = d.cells[i - 1];
      if (previous.phase_index + 1 == c.phase_index && c.phase_index > 0 &&
          c.phase_index <= 2)
        joins[c.phase_index - 1] =
            previous.global_first < previous.global_last &&
            c.global_first < c.global_last &&
            previous.assessment.phase.last == 1 &&
            c.assessment.phase.first == 0;
    }
  }
  check(d.join_expression_identity == std::array<bool, 2>{true, true},
        "Structural exact joins remain named separately from support "
        "qualification");
  check(d.qualified_joins == joins,
        "Only actual accepted adjacent two-sided cell proofs qualify joins");
  if (d.complete)
    check(!d.first_refusal &&
              cursor == std::max(d.requested_first, d.requested_last) &&
              d.arithmetic_supported && d.kinematics_complete &&
              d.timing_complete && d.self_complete && d.nonnegative_reactions &&
              d.nominal_vertical_equilibrium_complete &&
              d.finite_pressure_complete && d.support_complete,
          "Complete requested global cover requires every same-cell nominal "
          "support predicate");
  else
    check(d.first_refusal.has_value(),
          "Every incomplete candidate retains honest phase/global refusal");
  if (d.complete)
    check(d.endpoint_zero_port_reaction ==
              (std::max(d.requested_first, d.requested_last) == 1),
          "Only canonical global1 unloads port in either traversal direction");
}
void observe(std::string_view label, const Diagnostic& d) {
  std::cout << std::setprecision(17) << label << " complete=" << d.complete
            << " cells=" << d.cells.size() << " nodes=" << d.examined_nodes
            << " mandatory=" << d.mandatory_splits
            << " graphs=" << d.work.phase.graphs
            << " pairs=" << d.work.contact.self_pairs
            << " joins=" << d.qualified_joins[0] << ',' << d.qualified_joins[1];
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    std::cout << " refusal=" << static_cast<unsigned>(r.condition)
              << " predicate=" << static_cast<unsigned>(r.predicate_condition)
              << " phasecondition=" << static_cast<unsigned>(r.phase_condition)
              << " global=" << r.first << ',' << r.last;
    if (r.phase_index) std::cout << " phase=" << *r.phase_index;
  }
  if (!d.cells.empty())
    std::cout << " prefix=" << d.cells.front().global_first << ','
              << d.cells.back().global_last;
  std::cout << '\n';
  std::cout.flush();
}
void invalid_controls(const OriginBoardingBootSupport& source) {
  for (double v :
       {-1., std::nextafter(1., 2.), std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()})
    check(!assess_origin_boarding_route_checkpoint_unload(source, v, 1) &&
              !assess_origin_boarding_route_checkpoint_unload(source, 0, v),
          "Malformed global interval refuses before source or graph work");
  auto copy = source;
  const auto retained = std::move(copy);
  // NOLINTBEGIN(bugprone-use-after-move) -- Documented empty provider must
  // refuse candidate admission.
  check(!assess_origin_boarding_route_checkpoint_unload(copy),
        "Moved-from provider cannot acquire checkpoint authority");
  // NOLINTEND(bugprone-use-after-move) -- End empty-provider contract check.
  (void)retained;
  Limits raised;
  ++raised.phase.nodes;
  check(!detail::boarding_route_checkpoint_unload_bounded(source, 0, 1, raised),
        "Candidate cannot raise shared original cover limits");
  check(!detail::boarding_route_checkpoint_unload_controls(3) &&
            !detail::boarding_route_checkpoint_unload_controls(
                std::numeric_limits<std::size_t>::max()),
        "Unknown phase cannot supply a caller-selected profile");
  for (std::size_t cap = 0; cap < 3; ++cap) {
    Limits l;
    l.phase.nodes = cap;
    const auto d = require(
        detail::boarding_route_checkpoint_unload_bounded(source, 0, 1, l));
    observe("MANDATORY_NODE_CAP", d);
    check(!d.complete && d.first_refusal &&
              d.first_refusal->condition == Condition::node_capacity &&
              d.examined_nodes == cap && d.mandatory_splits == cap &&
              d.work.phase.graphs == 0 && d.work.contact.self_pairs == 0,
          "Global node0/1/2 caps stop before graph while mandatory splits "
          "consume actual nodes");
    if (d.first_refusal)
      check(d.first_refusal->first == 0 &&
                d.first_refusal->last == (cap == 0   ? 1.
                                          : cap == 1 ? .5
                                                     : .25),
            "Mandatory split priority is .5 then .25 with canonical left-first "
            "refusal");
    denied(d);
  }
  for (std::size_t depth = 0; depth < 2; ++depth) {
    Limits l;
    l.phase.depth = depth;
    const auto d = require(
        detail::boarding_route_checkpoint_unload_bounded(source, 0, 1, l));
    check(!d.complete && d.first_refusal &&
              d.first_refusal->condition == Condition::depth_capacity &&
              d.mandatory_splits == depth && d.work.phase.graphs == 0,
          "Mandatory join splitting consumes genuine global depth, never fresh "
          "phase budgets");
  }
  for (int stage = 0; stage < 2; ++stage) {
    Limits l;
    Condition why;
    if (stage == 0) {
      l.phase.leaves = 0;
      why = Condition::leaf_capacity;
    } else {
      l.output_bytes = 0;
      why = Condition::output_capacity;
    }
    const auto d = require(
        detail::boarding_route_checkpoint_unload_bounded(source, 0, 1, l));
    check(!d.complete && d.first_refusal && d.first_refusal->condition == why &&
              d.work.phase.graphs == 0,
          "Leaf and aggregate fixed output capacity refuse before new graph");
  }
  Diagnostic context_owner;
  const auto context = require(detail::prepare_boarding_route_checkpoint_unload(
      source, Limits{}, context_owner));
  BoardingRoutePortUnloadCounters work;
  BoardingRoutePortUnloadCell stale;
  stale.complete = stale.arithmetic_supported = stale.self_complete =
      stale.finite_pressure_complete = true;
  BoardingRoutePortUnloadRefusal why;
  const auto state = detail::boarding_route_checkpoint_unload_cell(
      context, 3, 0, 0, false, Limits{}, work, stale, why);
  check(state == State::unsupported &&
            why.condition == Condition::invalid_binding &&
            work.phase.graphs == 0 && !stale.complete && !stale.self_complete &&
            !stale.finite_pressure_complete,
        "Invalid privately selected profile cannot reuse stale caller-cell "
        "acceptance");
}
void unsafe_environment(const OriginBoardingBootSupport& source) {
  struct Guard {
    int mode = std::fegetround();
#if defined(__SSE2__) && defined(__x86_64__)
    unsigned csr = _mm_getcsr();
#endif
    ~Guard() {
      std::fesetround(mode);
#if defined(__SSE2__) && defined(__x86_64__)
      _mm_setcsr(csr);
#endif
    }
  } guard;
  auto inspect = [&]() {
    const auto d = assess_origin_boarding_route_checkpoint_unload(source);
    check(!d || (!d->complete && !d->arithmetic_supported &&
                 !d->support_complete && !d->self_complete),
          "Unsafe environment cannot retain new checkpoint qualification");
  };
  for (int mode : {FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO})
    if (std::fesetround(mode) == 0) inspect();
  std::fesetround(FE_TONEAREST);
#if defined(__SSE2__) && defined(__x86_64__)
  for (unsigned bit : {1U << 15, 1U << 6, 1U << 13}) {
    _mm_setcsr(guard.csr | bit);
    inspect();
    _mm_setcsr(guard.csr);
  }
#endif
}
auto cell_fingerprint(const Cell& c) -> Fingerprint {
  Fingerprint s;
  s.number(c.global_first);
  s.number(c.global_last);
  s.integer(c.phase_index);
  const auto& a = c.assessment;
  const auto& p = a.phase;
  s.number(p.first);
  s.number(p.last);
  const auto point = [&](const BoardingPlantedBodyPointEvidence& v) {
    s.point(v.value);
    s.point(v.derivatives.velocity);
    s.point(v.derivatives.acceleration);
  };
  for (const auto& v : p.points)
    point(v);
  for (const auto& v : p.mass_points)
    point(v);
  point(p.center_of_mass);
  for (const auto& frame : p.frames)
    for (const auto& column : frame.columns)
      point(column);
  for (const auto& leg : p.legs) {
    for (auto b : std::array{leg.distance_squared, leg.rho_squared,
                             leg.gamma_squared, leg.alpha, leg.gamma})
      s.scalar(b);
    for (auto j :
         std::array{leg.hip_pitch, leg.shin_pitch, leg.knee_flex, leg.hip_axial,
                    leg.hip_abduction, leg.ankle_pitch, leg.ankle_roll}) {
      s.scalar(j.rate);
      s.scalar(j.coordinate_second);
    }
    for (auto b : leg.joint_speeds)
      s.scalar(b);
    for (auto b : leg.sector_margins)
      s.scalar(b);
  }
  for (auto b : std::array{p.root_speed, p.root_acceleration, p.root_yaw_speed,
                           p.torso_joint_speed, p.port_reaction_fraction,
                           a.port_reaction_fraction})
    s.scalar(b);
  for (auto b : a.barycenter_xz)
    s.scalar(b);
  for (auto b : a.common_pressure_delta_xz)
    s.scalar(b);
  for (const auto& pressure : a.pressures) {
    for (auto b : pressure.pressure_xz)
      s.scalar(b);
    s.scalar(pressure.sole_minimum_signed_side);
    s.scalar(pressure.sole_minimum_squared_gap);
    s.integer(pressure.source_partition);
    s.integer(pressure.scanned_partitions);
    for (const auto& candidate : pressure.candidates) {
      s.scalar(candidate.minimum_signed_side);
      s.scalar(candidate.minimum_squared_gap);
      s.integer(static_cast<std::size_t>(candidate.status));
      s.integer(candidate.arithmetic_supported);
      s.integer(candidate.quad_valid);
    }
    s.integer(pressure.complete);
  }
  for (const auto& o : a.owners) {
    s.scalar(o.extent);
    s.scalar(o.limit);
    s.scalar(o.secondary_extent);
    s.integer(static_cast<std::size_t>(o.certificate));
    s.integer(o.certified);
  }
  for (auto cert : a.pair_certificates)
    s.integer(static_cast<std::size_t>(cert));
  for (auto n : a.certificate_counts)
    s.integer(n);
  for (bool b : std::array{
           p.complete, a.complete, a.arithmetic_supported, a.self_complete,
           a.nonnegative_reactions, a.nominal_vertical_equilibrium_complete,
           a.finite_pressure_complete, a.endpoint_zero_port_reaction})
    s.integer(b);
  return s;
}
void observed_budgets(const OriginBoardingBootSupport& source) {
  const auto base =
      require(assess_origin_boarding_route_checkpoint_unload(source, 0, 0));
  observe("POINT_BASE", base);
  accounting(base);
  check(base.complete,
        "Observed initial point remains complete nominal support");
  if (!base.complete) return;
  Limits exact;
  exact.phase.nodes = base.examined_nodes;
  exact.phase.leaves = base.cells.size();
  exact.output_bytes = base.output_capacity_bytes;
  exact.phase.graphs = base.work.phase.graphs;
  exact.phase.legs = base.work.phase.legs;
  exact.phase.bodies = base.work.phase.bodies;
  exact.phase.sectors = base.work.phase.sectors;
  exact.phase.timing = base.work.phase.timing;
  exact.pairs = base.work.contact.self_pairs;
  exact.axes = base.work.contact.proposed_axes;
  exact.signed_trials = base.work.contact.signed_trials;
  exact.owners = base.work.ownership_attempts;
  exact.candidates = base.work.contact.pressure_candidates;
  exact.edges = base.work.contact.disk_edges;
  const auto replay = require(
      detail::boarding_route_checkpoint_unload_bounded(source, 0, 0, exact));
  check(replay.complete &&
            cell_fingerprint(replay.cells[0]) ==
                cell_fingerprint(base.cells[0]) &&
            child_fingerprint(*replay.initial) ==
                child_fingerprint(*base.initial),
        "Exact actual point work and storage budgets preserve genuine "
        "fieldwise evidence");
  for (int stage = 0; stage < 6; ++stage) {
    auto l = exact;
    std::uint64_t* cap{};
    Condition why{};
    switch (stage) {
      case 0:
        cap = &l.pairs;
        why = Condition::pair_capacity;
        break;
      case 1:
        cap = &l.axes;
        why = Condition::axis_capacity;
        break;
      case 2:
        cap = &l.signed_trials;
        why = Condition::signed_trial_capacity;
        break;
      case 3:
        cap = &l.owners;
        why = Condition::ownership_capacity;
        break;
      case 4:
        cap = &l.candidates;
        why = Condition::pressure_capacity;
        break;
      default:
        cap = &l.edges;
        why = Condition::edge_capacity;
        break;
    }
    if (*cap == 0) continue;
    --*cap;
    const auto d = require(
        detail::boarding_route_checkpoint_unload_bounded(source, 0, 0, l));
    check(!d.complete && d.first_refusal && d.first_refusal->condition == why,
          "One less genuinely reached compound operation capacity refuses at "
          "original stage");
    accounting(d, l);
    auto zero = Limits{};
    std::uint64_t* zero_cap{};
    switch (stage) {
      case 0: zero_cap = &zero.pairs; break;
      case 1: zero_cap = &zero.axes; break;
      case 2: zero_cap = &zero.signed_trials; break;
      case 3: zero_cap = &zero.owners; break;
      case 4: zero_cap = &zero.candidates; break;
      default: zero_cap = &zero.edges; break;
    }
    *zero_cap = 0;
    const auto z = require(
        detail::boarding_route_checkpoint_unload_bounded(source, 0, 0, zero));
    check(!z.complete && z.first_refusal && z.first_refusal->condition == why,
          "Observed original point independently reaches zero compound caps");
  }
  Diagnostic owner;
  const auto context = require(detail::prepare_boarding_route_checkpoint_unload(
      source, Limits{}, owner));
  Limits caps;
  caps.pairs = 0;
  auto work = BoardingRoutePortUnloadCounters{};
  work.contact.self_pairs = 1;
  auto stale = base.cells[0].assessment;
  BoardingRoutePortUnloadRefusal why;
  const auto state = detail::boarding_route_checkpoint_unload_cell(
      context, 0, 0, 0, false, caps, work, stale, why);
  check(state == State::capacity && why.condition == Condition::pair_capacity &&
            work.phase.graphs == 0 && !stale.complete,
        "Incoming above-cap counter refuses before graph and clears stale "
        "current-cell flags");
  const auto preparation =
      require(assess_origin_boarding_route_checkpoint_unload(source, 0, .25));
  observe("PREPARATION_CAP_BASE", preparation);
  check(preparation.complete, "Observed full preparation remains complete");
  if (preparation.complete && preparation.work.contact.self_pairs > 0) {
    auto l = Limits{};
    l.pairs = preparation.work.contact.self_pairs;
    const auto across =
        require(detail::boarding_route_checkpoint_unload_bounded(
            source, 0, .25 + 1. / 1024, l));
    observe("SHARED_PHASE_CAP", across);
    check(!across.complete && across.first_refusal &&
              across.first_refusal->condition == Condition::pair_capacity &&
              across.first_refusal->first >= .25 &&
              across.first_refusal->phase_index == 1 &&
              across.work.contact.self_pairs == l.pairs &&
              !across.cells.empty() && across.cells.back().global_last == .25,
          "Actual completed preparation work remains charged when pivot first "
          "exhausts global pair capacity");
  }
  for (int stage = 0; stage < 3; ++stage) {
    auto l = exact;
    Condition expected{};
    if (stage == 0) {
      --l.phase.nodes;
      expected = Condition::node_capacity;
    }
    if (stage == 1) {
      --l.phase.leaves;
      expected = Condition::leaf_capacity;
    }
    if (stage == 2) {
      --l.output_bytes;
      expected = Condition::output_capacity;
    }
    const auto less = require(
        detail::boarding_route_checkpoint_unload_bounded(source, 0, 0, l));
    check(!less.complete && less.first_refusal &&
              less.first_refusal->condition == expected,
          "One less actual point node/leaf/output budget refuses at reached "
          "operation");
  }
}
void joins_and_reverse(const OriginBoardingBootSupport& source) {
  for (std::size_t join = 0; join < 2; ++join) {
    const double g = join == 0 ? .25 : .5;
    const auto point =
        require(assess_origin_boarding_route_checkpoint_unload(source, g, g));
    observe("JOIN_POINT", point);
    accounting(point);
    if (point.complete) {
      check(point.cells.size() == 1 && point.cells[0].phase_index == join + 1 &&
                point.cells[0].assessment.phase.first == 0 &&
                point.cells[0].assessment.phase.last == 0 &&
                point.qualified_joins == std::array<bool, 2>{false, false},
            "Exact join point chooses right phase and does not invent "
            "two-sided qualification");
      check(point.cells[0].assessment.port_reaction_fraction.lower == .25 &&
                point.cells[0].assessment.port_reaction_fraction.upper == .25 &&
                !point.endpoint_zero_port_reaction,
            "Both internal joins preserve exact quarter load without old "
            "local1-zero error");
    }
    const auto before = require(assess_origin_boarding_route_checkpoint_unload(
        source, g - 1. / 1024, g));
    const auto after = require(assess_origin_boarding_route_checkpoint_unload(
        source, g, g + 1. / 1024));
    accounting(before);
    accounting(after);
    check(!before.qualified_joins[join] && !after.qualified_joins[join],
          "One-sided join requests cannot manufacture missing adjacent phase "
          "proof");
    const auto crossing =
        require(assess_origin_boarding_route_checkpoint_unload(
            source, g - 1. / 1024, g + 1. / 1024));
    accounting(crossing);
    if (crossing.complete)
      check(crossing.qualified_joins[join],
            "Accepted actual adjacent cells qualify requested two-sided "
            "physical join");
  }
  const auto forward = require(
      assess_origin_boarding_route_checkpoint_unload(source, .125, .125));
  const auto repeated = require(
      assess_origin_boarding_route_checkpoint_unload(source, .125, .125));
  if (forward.complete && repeated.complete)
    check(
        cell_fingerprint(forward.cells[0]) ==
            cell_fingerprint(repeated.cells[0]),
        "Repeated exact point query retains original local clock and evidence");
  for (std::size_t phase = 0; phase < 3; ++phase) {
    const std::array<double, 4> boundaries{0, .25, .5, 1};
    const auto a = boundaries[phase], b = boundaries[phase + 1],
               x = a + (b - a) * .25, y = a + (b - a) * .75;
    const auto f = require(assess_origin_boarding_route_checkpoint_unload(
                   source, x, y)),
               r = require(assess_origin_boarding_route_checkpoint_unload(
                   source, y, x));
    accounting(f);
    accounting(r);
    if (f.complete && r.complete && f.cells.size() == r.cells.size())
      for (std::size_t i = 0; i < f.cells.size(); ++i) {
        const auto &fc = f.cells[i], &rc = r.cells[i];
        check(fc.global_first == rc.global_first &&
                  fc.global_last == rc.global_last &&
                  fc.phase_index == rc.phase_index,
              "Forward/reverse retain same canonical phase cover");
        for (std::size_t p = 0; p < 18; ++p) {
          const auto &fd = fc.assessment.phase.points[p].derivatives,
                     &rd = rc.assessment.phase.points[p].derivatives;
          check(fd.acceleration.lower == rd.acceleration.lower &&
                    fd.acceleration.upper == rd.acceleration.upper,
                "Reverse preserves second physical derivative");
          for (std::size_t axis = 0; axis < 3; ++axis)
            check(component(fd.velocity.lower, axis) ==
                          -component(rd.velocity.upper, axis) &&
                      component(fd.velocity.upper, axis) ==
                          -component(rd.velocity.lower, axis),
                  "Reverse negates only first physical derivative without "
                  "global clock factor");
        }
      }
  }
}
void observed_whole_refusal(const Diagnostic& d) {
  check(!d.complete && d.first_refusal && !d.support_complete &&
            !d.endpoint_zero_port_reaction,
        "Observed whole candidate retains refusal without claiming supported "
        "unload");
  check(d.qualified_joins == std::array<bool, 2>{true, false},
        "Actual preparation-to-pivot prefix join remains qualified while "
        "second join stays unqualified");
  if (!d.first_refusal) return;
  const auto& r = *d.first_refusal;
  check(r.condition == Condition::depth_capacity &&
            r.predicate_condition == Condition::unresolved_self_pair &&
            r.phase_index == 1 && r.first == .4658203125 &&
            r.last == .466796875,
        "Whole and reverse preserve exact canonical phase1 self-clearance "
        "refusal");
  check(!d.cells.empty() && d.cells.front().global_first == 0 &&
            d.cells.back().global_last == .4658203125,
        "Observed whole and reverse retain valid original supported prefix "
        "through refusal start");
}
void observed_second_join_refusal(const Diagnostic& d) {
  check(!d.complete && d.first_refusal && d.cells.empty() &&
            d.qualified_joins == std::array<bool, 2>{false, false},
        "Observed exact second join point remains refused without qualified "
        "join proof");
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    check(r.condition == Condition::unsplittable_interval &&
              r.predicate_condition == Condition::unresolved_self_pair &&
              r.phase_index == 2 && r.first == .5 && r.last == .5,
          "Canonical second join selects right phase2 and retains exact point "
          "self-refusal");
  }
}
void public_cases(const OriginBoardingBootSupport& source) {
  const auto old = child_fingerprint(
      require(assess_origin_boarding_source_endpoint_load(source)));
  auto result = assess_origin_boarding_route_checkpoint_unload(source);
  if (!result) {
    std::cout << "FIRST_PUBLIC_CHECKPOINT_UNLOAD API_ERROR=" << result.error()
              << '\n';
    std::cout.flush();
  }
  const auto first = require(std::move(result));
  observe("FIRST_PUBLIC_CHECKPOINT_UNLOAD", first);
  accounting(first);
  // Frozen first GCC/Clang20 observations preceded these outcome regressions.
  // Preserve each fresh FIRST print before asserting its now-known result.
  observed_whole_refusal(first);
  check(child_fingerprint(*first.initial) == old,
        "Candidate owns unchanged original source/body/self/load child without "
        "reconstruction");
  observed_budgets(source);
  const std::array<std::array<double, 2>, 10> requests{{{1, 0},
                                                        {0, .25},
                                                        {.25, .5},
                                                        {.5, 1},
                                                        {.125, .75},
                                                        {.75, .125},
                                                        {0, 0},
                                                        {1, 1},
                                                        {.25, .25},
                                                        {.5, .5}}};
  for (const auto range : requests) {
    const auto d = require(assess_origin_boarding_route_checkpoint_unload(
        source, range[0], range[1]));
    observe("REQUEST", d);
    accounting(d);
    check(child_fingerprint(*d.initial) == old,
          "Every fresh reverse/sub/point retains the same original child "
          "contract");
    if (range[0] == 1 && range[1] == 0) observed_whole_refusal(d);
    if (range[0] == 0 && range[1] == .25)
      check(d.complete,
            "Observed full preparation remains completely supported");
    if (range[0] == range[1] &&
        (range[0] == 0 || range[0] == .25 || range[0] == 1))
      check(d.complete, "Observed initial, first join and unloaded endpoint "
                        "points remain complete");
    if (range[0] == .5 && range[1] == .5) observed_second_join_refusal(d);
    if (d.complete && range[0] == 1 && range[1] == 1) {
      const auto& c = d.cells[0].assessment;
      check(
          c.port_reaction_fraction.lower == 0 &&
              c.port_reaction_fraction.upper == 0 &&
              d.endpoint_zero_port_reaction,
          "Only actual global1 retains genuine single-foot unloaded endpoint");
      for (std::size_t axis = 0; axis < 2; ++axis) {
        const auto coord = axis == 0 ? 0U : 2U;
        check(c.pressures[1].pressure_xz[axis].lower ==
                      component(c.phase.center_of_mass.value.lower, coord) &&
                  c.pressures[1].pressure_xz[axis].upper ==
                      component(c.phase.center_of_mass.value.upper, coord),
              "Starboard pressure equals same full moving COM bounds at exact "
              "zero reaction");
      }
    }
  }
  joins_and_reverse(source);
  check(child_fingerprint(require(
            assess_origin_boarding_source_endpoint_load(source))) == old,
        "New candidate does not mutate retained original source state");
}
} // namespace
int main() {
  try {
    static_assert(sizeof(Cell) <=
                  kBoardingRouteCheckpointUnloadMaximumCellBytes);
    static_assert(
        !std::is_constructible_v<detail::BoardingRouteCheckpointUnloadCellToken,
                                 const detail::BoardingRoutePortUnloadContext&,
                                 const BoardingRoutePortUnloadCell&,
                                 std::size_t>);
    const auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    const auto source = require(make_origin_boarding_boot_support(binding));
    invalid_controls(source);
    unsafe_environment(source);
    public_cases(source);
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
