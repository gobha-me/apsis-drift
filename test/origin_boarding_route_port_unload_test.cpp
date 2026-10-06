#include "apsis_drift/origin_boarding_route_port_unload.hpp"
#include "origin_boarding_route_port_unload_internal.hpp"
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
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#if defined(__SSE2__) && defined(__x86_64__)
#include <xmmintrin.h>
#endif
namespace {
using namespace apsis_drift;
using Request = BoardingRouteFootPhaseRequest;
using Constant = BoardingRoutePhaseConstant;
using ConstantPoint = BoardingRoutePhasePointConstant;
using Cell = BoardingRouteFootPhaseCell;
using Compound = BoardingRoutePortUnloadCell;
using Diagnostic = BoardingRoutePortUnloadDiagnostic;
using Load = BoardingSourceEndpointLoadDiagnostic;
using Bounds = BoardingPlantedLegScalarBounds;
using Limits = detail::BoardingRoutePortUnloadLimits;
using Condition = BoardingRoutePortUnloadCondition;
using State = detail::BoardingRoutePortUnloadCellResult;
std::size_t checks{};
int failures{};
void check(bool okay, std::string_view label) {
  ++checks;
  if (!okay) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T, class E> auto require(std::expected<T, E> r) -> T {
  if (!r) throw std::runtime_error("Required port-unload test API refused");
  return std::move(*r);
}
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
auto sin_jet(Jet a) -> Jet {
  return {std::sin(a.value), std::cos(a.value) * a.first,
          std::cos(a.value) * a.second - std::sin(a.value) * a.first * a.first};
}
auto cos_jet(Jet a) -> Jet {
  return {std::cos(a.value), -std::sin(a.value) * a.first,
          -std::sin(a.value) * a.second -
              std::cos(a.value) * a.first * a.first};
}
using Matrix = std::array<Point, 3>;
auto transform(const Matrix& m, Point p) -> Point {
  Point out{};
  for (std::size_t row = 0; row < 3; ++row)
    for (std::size_t col = 0; col < 3; ++col)
      out[row] = out[row] + m[row][col] * p[col];
  return out;
}
auto transpose(const Matrix& m) -> Matrix {
  Matrix out{};
  for (std::size_t row = 0; row < 3; ++row)
    for (std::size_t col = 0; col < 3; ++col)
      out[row][col] = m[col][row];
  return out;
}
auto multiply(const Matrix& a, const Matrix& b) -> Matrix {
  Matrix out{};
  for (std::size_t row = 0; row < 3; ++row)
    for (std::size_t col = 0; col < 3; ++col)
      for (std::size_t k = 0; k < 3; ++k)
        out[row][col] = out[row][col] + a[row][k] * b[k][col];
  return out;
}
auto yaw(Jet half) -> Matrix {
  const auto theta = literal(2) * angle(literal(1), half), c = cos_jet(theta),
             s = sin_jet(theta);
  return {{Point{c, literal(0), s}, Point{literal(0), literal(1), literal(0)},
           Point{-s, literal(0), c}}};
}
auto pitch(Jet half) -> Matrix {
  const auto theta = literal(2) * angle(literal(1), half), c = cos_jet(theta),
             s = sin_jet(theta);
  return {{Point{literal(1), literal(0), literal(0)}, Point{literal(0), c, -s},
           Point{literal(0), s, c}}};
}
auto expression(const Constant& c) -> Jet {
  Jet out{};
  for (std::size_t i = 0; i < c.count; ++i)
    out = out + literal(c.terms[i]);
  return out;
}
auto expression(const ConstantPoint& p) -> Point {
  return {expression(p.coordinates[0]), expression(p.coordinates[1]),
          expression(p.coordinates[2])};
}
auto lerp(Jet a, Jet b, Jet s) -> Jet {
  return a + (b - a) * s;
}
auto interpolate(const std::array<double, 2>& a, Jet s) -> Jet {
  return lerp(literal(a[0]), literal(a[1]), s);
}
struct Oracle {
  std::array<Point, 18> points{};
  std::array<Point, 15> mass_points{};
  Point com{};
  std::array<Matrix, 4> frames{};
  std::array<std::array<Jet, 7>, 2> angles{};
  std::array<Jet, 2> D{}, rho2{}, gamma2{}, alpha{}, gamma{};
  std::array<std::array<long double, 3>, 2> speeds{};
  std::array<Point, 2> soles{};
  Jet reaction{}, root_yaw{}, torso{};
  std::array<Jet, 2> sole_yaw{};
};
auto oracle(const Request& r, double u, bool reverse) -> Oracle {
  Oracle out;
  const Jet t{u, (reverse ? -1.L : 1.L) / r.seconds_per_parameter, 0};
  const auto one = literal(1), two = literal(2),
             s = t * t * t *
                 (literal(10) - literal(15) * t + literal(6) * t * t),
             w = literal(64) * t * t * t * (one - t) * (one - t) * (one - t);
  auto& p = out.points;
  const auto root0 = expression(r.root[0]), root1 = expression(r.root[1]);
  for (std::size_t a = 0; a < 3; ++a)
    p[0][a] = lerp(root0[a], root1[a], s);
  const auto root_half = interpolate(r.root_yaw_half, s),
             torso_half = interpolate(r.torso_lean_half, s);
  out.root_yaw = two * angle(one, root_half);
  out.torso = two * angle(one, torso_half);
  out.frames[0] = yaw(root_half);
  out.frames[1] = multiply(out.frames[0], pitch(torso_half));
  out.reaction = interpolate(r.port_reaction_fraction, s);
  for (std::size_t i = 1; i < 4; ++i) {
    const auto height = i == 1 ? .3495 : i == 2 ? .70237 : .65237;
    p[i] = add(p[0], transform(out.frames[1], constant_point(0, height, 0)));
  }
  const auto l1 = literal(.47285), l2 = literal(.47478),
             arm = -literal(.35898) * root(literal(.5));
  for (std::size_t side = 0; side < 2; ++side) {
    const auto base = side == 0 ? 4U : 11U;
    const double sign = side == 0 ? -1. : 1.;
    const auto half = interpolate(r.feet[side].yaw_half, s);
    const auto flat = yaw(half);
    out.frames[side + 2] = flat;
    out.sole_yaw[side] = two * angle(one, half);
    p[base] =
        add(p[0], transform(out.frames[0], constant_point(sign * .14, 0, 0)));
    const auto a = expression(r.feet[side].sole[0]),
               b = expression(r.feet[side].sole[1]);
    Point sole;
    for (std::size_t k = 0; k < 3; ++k)
      sole[k] = lerp(a[k], b[k], s);
    sole[1] = sole[1] + literal(r.feet[side].swing_height_metres) * w;
    out.soles[side] = sole;
    p[base + 2] = add(sole, constant_point(0, .10, 0));
    p[base + 3] = add(sole, constant_point(0, .05, 0));
    const auto d = transform(transpose(flat), subtract(p[base + 2], p[base]));
    const auto rho = root(d[0] * d[0] + d[1] * d[1]), D = dot(d, d);
    const auto alpha = (l1 * l1 - l2 * l2 + D) / (two * D);
    const auto gamma2 = (l1 * l1 - alpha * alpha * D) / D, gamma = root(gamma2);
    const Point U{-d[0] / rho, -d[1] / rho, literal(0)},
        N{U[1], -U[0], literal(0)};
    const auto q = cross(N, d);
    p[base + 1] =
        add(p[base], transform(flat, add(scale(d, alpha), scale(q, gamma))));
    out.D[side] = D;
    out.rho2[side] = rho * rho;
    out.alpha[side] = alpha;
    out.gamma2[side] = gamma2;
    out.gamma[side] = gamma;
    const auto f1 = alpha * rho + gamma * d[2], g1 = gamma * rho - alpha * d[2],
               f2 = (one - alpha) * rho - gamma * d[2],
               g2 = -((one - alpha) * d[2] + gamma * rho);
    const auto theta1 = angle(f1, g1), theta2 = angle(f2, g2),
               phi = angle(-d[1], d[0]),
               delta = out.sole_yaw[side] - out.root_yaw;
    out.angles[side] = {
        theta1,  theta2, theta1 - theta2, delta, literal(sign) * phi,
        -theta2, -phi};
    const auto hip2 = delta.first * delta.first + phi.first * phi.first +
                      theta1.first * theta1.first +
                      2 * delta.first * theta1.first * std::sin(phi.value);
    out.speeds[side] = {std::sqrt(std::max(0.L, hip2)),
                        std::abs(theta1.first - theta2.first),
                        std::hypot(theta2.first, phi.first)};
    p[base + 4] = add(
        p[0], transform(out.frames[1], constant_point(sign * .20265, .579, 0)));
    p[base + 5] =
        add(p[base + 4], transform(out.frames[1], Point{literal(0), arm, arm}));
    p[base + 6] =
        add(p[base + 5], transform(out.frames[1], constant_point(0, .386, 0)));
  }
  for (std::size_t i = 0; i < 15; ++i) {
    const auto& m = masses[i];
    out.mass_points[i] = scale(add(p[m.first], p[m.second]), literal(.5));
    out.com = add(out.com, scale(out.mass_points[i],
                                 literal(static_cast<double>(m.weight))));
  }
  out.com = scale(out.com, literal(1) / literal(1200));
  return out;
}
auto component(RigidVector3 p, std::size_t i) -> double {
  return i == 0 ? p.x : i == 1 ? p.y : p.z;
}
auto contains(BoardingPlantedLegScalarBounds b, long double x) -> bool {
  const auto allowance = 2e-12L * std::max(1.L, std::abs(x));
  return std::isfinite(b.lower) && std::isfinite(b.upper) &&
         b.lower <= b.upper && x >= b.lower - allowance &&
         x <= b.upper + allowance;
}
void point_contains(const BoardingPlantedBodyPointEvidence& b, const Point& p) {
  for (std::size_t i = 0; i < 3; ++i) {
    check(contains({component(b.value.lower, i), component(b.value.upper, i)},
                   p[i].value),
          "Independent point value enclosed");
    check(contains({component(b.derivatives.velocity.lower, i),
                    component(b.derivatives.velocity.upper, i)},
                   p[i].first),
          "Independent physical point first derivative enclosed");
    check(contains({component(b.derivatives.acceleration.lower, i),
                    component(b.derivatives.acceleration.upper, i)},
                   p[i].second),
          "Independent physical point second derivative enclosed");
  }
}
auto physical_norm(const Point& p, bool acceleration = false) -> long double {
  long double squared{};
  for (const auto& a : p) {
    const auto v = acceleration ? a.second : a.first;
    squared += v * v;
  }
  return std::sqrt(squared);
}
void timing_oracle(const Cell& c, const Oracle& o) {
  check(contains(c.root_speed, physical_norm(o.points[0])) &&
            contains(c.root_acceleration, physical_norm(o.points[0], true)),
        "Independent root physical vector norms enclosed");
  check(contains(c.root_yaw_speed, std::abs(o.root_yaw.first)) &&
            contains(c.torso_joint_speed, std::abs(o.torso.first)),
        "Independent actual root and torso angular speeds enclosed");
  check(
      contains(c.port_reaction_fraction, o.reaction.value),
      "Declared interpolated reaction control enclosed without load authority");
  for (std::size_t side = 0; side < 2; ++side) {
    const auto center = physical_norm(o.soles[side]),
               spin = std::abs(o.sole_yaw[side].first),
               whole = center + std::hypot(.06L, .14L) * spin;
    check(contains(c.sole_center_speed[side], center) &&
              contains(c.sole_yaw_speed[side], spin) &&
              contains(c.whole_sole_speed[side], whole),
          "Moving whole sole includes translation and corner-radius rotation");
    const auto base = side == 0 ? 4U : 11U;
    const auto thigh = subtract(o.points[base + 1], o.points[base]),
               shin = subtract(o.points[base + 2], o.points[base + 1]);
    check(std::abs(std::sqrt(dot(thigh, thigh).value) - .47285L) < 2e-15L &&
              std::abs(std::sqrt(dot(shin, shin).value) - .47478L) < 2e-15L,
          "Independent actual three-dimensional rods retain both original "
          "lengths");
  }
}
void accepted_timing(const Cell& c) {
  const auto angular = std::numbers::pi / 6;
  check(c.root_speed.upper <= .25 && c.root_acceleration.upper <= .10 &&
            c.root_yaw_speed.upper <= angular &&
            c.torso_joint_speed.upper <= angular,
        "Accepted root and torso respect registered physical timing limits");
  for (std::size_t side = 0; side < 2; ++side) {
    check(c.whole_sole_speed[side].upper <= .35,
          "Accepted full sole respects translation plus rotation limit");
    for (auto b : c.legs[side].joint_speeds)
      check(b.upper <= angular,
            "Accepted physical joint resultant respects angular limit");
    for (auto b : c.legs[side].sector_margins)
      check(b.lower >= 0,
            "Accepted original directed sector margins are nonnegative");
  }
}
void cell_oracle(const Request& r, const Cell& c, bool reverse, double u) {
  const auto o = oracle(r, u, reverse);
  timing_oracle(c, o);
  for (std::size_t i = 0; i < 18; ++i)
    point_contains(c.points[i], o.points[i]);
  for (std::size_t i = 0; i < 15; ++i)
    point_contains(c.mass_points[i], o.mass_points[i]);
  point_contains(c.center_of_mass, o.com);
  for (std::size_t f = 0; f < 4; ++f)
    for (std::size_t col = 0; col < 3; ++col)
      point_contains(
          c.frames[f].columns[col],
          {o.frames[f][0][col], o.frames[f][1][col], o.frames[f][2][col]});
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& l = c.legs[side];
    check(contains(l.distance_squared, o.D[side].value) &&
              contains(l.rho_squared, o.rho2[side].value) &&
              contains(l.alpha, o.alpha[side].value) &&
              contains(l.gamma_squared, o.gamma2[side].value) &&
              contains(l.gamma, o.gamma[side].value),
          "Independent unfactored sphere/plane graph enclosed");
    const std::array jets{l.hip_pitch, l.shin_pitch,    l.knee_flex,
                          l.hip_axial, l.hip_abduction, l.ankle_pitch,
                          l.ankle_roll};
    for (std::size_t j = 0; j < jets.size(); ++j)
      check(contains(jets[j].rate, o.angles[side][j].first) &&
                contains(jets[j].coordinate_second, o.angles[side][j].second),
            "Every actual angle first/second enclosed");
    for (std::size_t j = 0; j < 3; ++j)
      check(contains(l.joint_speeds[j], o.speeds[side][j]),
            "Resultant joint speed including hip cross term enclosed");
  }
}
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
}
auto pressure_edges(const std::array<RigidVector3, 4>& quad,
                    const std::array<long double, 2>& pressure)
    -> std::array<long double, 2> {
  std::array minima{std::numeric_limits<long double>::infinity(),
                    std::numeric_limits<long double>::infinity()};
  const auto margin =
      static_cast<long double>(.02) + static_cast<long double>(.01);
  for (std::size_t i = 0; i < 4; ++i) {
    const auto a = quad[i], b = quad[(i + 1) % 4];
    const auto ex = static_cast<long double>(b.x) - a.x,
               ez = static_cast<long double>(b.z) - a.z,
               signed_side =
                   ez * (pressure[0] - a.x) - ex * (pressure[1] - a.z);
    minima[0] = std::min(minima[0], signed_side);
    minima[1] = std::min(minima[1], signed_side * signed_side -
                                        margin * margin * (ex * ex + ez * ez));
  }
  return minima;
}
void denied(const Diagnostic& d) {
  check(!d.world_qualified && !d.material_qualified &&
            !d.source_surface_sweep_qualified && !d.route_qualified &&
            !d.actor_qualified && !d.seat_qualified && !d.save_qualified &&
            !d.first_flight_qualified && !d.free_foot_swing_qualified &&
            !d.dynamics_qualified && !d.strength_qualified &&
            !d.friction_qualified,
        "Supported unload grants no world/material/dynamics/route or actor "
        "authority");
}
void pressure_oracle(const Diagnostic& d, const Compound& c, double u) {
  const auto o = oracle(d.controls, u, d.reverse);
  const auto w = o.reaction.value;
  check(contains(c.port_reaction_fraction, w) &&
            c.port_reaction_fraction.lower >= 0 &&
            c.port_reaction_fraction.upper <= .5,
        "Privately proven nonnegative unload share preserves exact weight "
        "domain");
  const auto source =
      d.initial->self.endpoint.sites.source.selected_partitions();
  std::array<long double, 2> bary{}, delta{};
  std::array<std::array<long double, 2>, 2> pressures{};
  for (std::size_t a = 0; a < 2; ++a) {
    const auto coord = a == 0 ? 0U : 2U;
    bary[a] = w * o.soles[0][coord].value + (1 - w) * o.soles[1][coord].value;
    delta[a] = o.com[coord].value - bary[a];
    check(contains(c.barycenter_xz[a], bary[a]) &&
              contains(c.common_pressure_delta_xz[a], delta[a]),
          "Independent full moving COM controls both pressure coordinates "
          "without symmetry shortcut");
    for (std::size_t side = 0; side < 2; ++side) {
      pressures[side][a] = o.soles[side][coord].value + delta[a];
      check(contains(c.pressures[side].pressure_xz[a], pressures[side][a]),
            "Pressure uses genuine sole center plus shared delta, no old "
            "port40mm witness offset");
    }
    const auto projected = w * pressures[0][a] + (1 - w) * pressures[1][a];
    check(std::abs(projected - o.com[coord].value) <
              128 * std::numeric_limits<long double>::epsilon(),
          "Exact complementary reactions cancel projected moment about full "
          "COM");
  }
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& p = c.pressures[side];
    check(p.complete && p.arithmetic_supported && p.coplanar &&
              p.sole_disk_contained && p.source_disk_contained &&
              p.coverage_complete && p.scanned_partitions == 10 &&
              p.source_partition < source.size(),
          "Accepted derived pressure owns full authentic ten-quad scan and "
          "finite disk margins");
    if (p.source_partition >= source.size()) continue;
    const auto plane = side == 0 ? -100000.0 * 1e-6 : -160000.0 * 1e-6;
    check(source[p.source_partition].plane_metres == plane,
          "Derived pressure selects actual unchanged sole plane");
    const auto min = pressure_edges(source[p.source_partition].perimeter_metres,
                                    pressures[side]);
    const auto& selected = p.candidates[p.source_partition];
    check(contains(selected.minimum_signed_side, min[0]) &&
              contains(selected.minimum_squared_gap, min[1]) &&
              selected.status ==
                  BoardingSourceEndpointLoadCandidateStatus::contained &&
              selected.quad_valid && selected.arithmetic_supported,
          "Independent true source taper edges corroborate selected finite "
          "pressure disk");
    for (std::size_t partition = 0; partition < 10; ++partition) {
      const auto& candidate = p.candidates[partition];
      if (source[partition].plane_metres != plane)
        check(candidate.status ==
                  BoardingSourceEndpointLoadCandidateStatus::noncoplanar,
              "Other authentic pressure height never becomes eligible via "
              "rounded plane tolerance");
    }
    const auto available_x = static_cast<long double>(.06) -
                             static_cast<long double>(.02) -
                             static_cast<long double>(.01),
               available_z = static_cast<long double>(.14) -
                             static_cast<long double>(.02) -
                             static_cast<long double>(.01);
    check(std::abs(delta[0]) <= available_x + 1e-15L &&
              std::abs(delta[1]) <= available_z + 1e-15L,
          "Independent derived disk fits full original fixed sole");
  }
  if (u == 1) {
    check(w == 0 &&
              std::abs(pressures[1][0] - o.com[0].value) <
                  128 * std::numeric_limits<long double>::epsilon() &&
              std::abs(pressures[1][1] - o.com[2].value) <
                  128 * std::numeric_limits<long double>::epsilon(),
          "Zero port reaction algebra puts genuine starboard disk at actual "
          "full COM");
  }
}
void ownership_oracle(const Diagnostic& d, const Compound& c, double u) {
  const auto o = oracle(d.controls, u, d.reverse);
  for (std::size_t i = 0; i < c.owners.size(); ++i) {
    const auto& r = d.initial->self.regions[i];
    const auto& owner = c.owners[i];
    if (!owner.arithmetic_supported ||
        owner.certificate == BoardingSourceEndpointSelfCertificate::none)
      continue;
    const auto side = static_cast<std::size_t>(r.second) >= 9 ? 1U : 0U,
               base = side == 0 ? 4U : 11U;
    long double extent{}, limit = r.limit_metres, secondary{};
    switch (r.junction) {
      case BoardingSelfJunction::hip: {
        const auto direction =
            scale(subtract(o.points[base + 1], o.points[base]),
                  literal(1) / literal(.47285));
        const auto local = transform(transpose(o.frames[0]), direction);
        extent = .24L * std::abs(local[0].value) -
                 (side == 0 ? -1.L : 1.L) * .14L * local[0].value +
                 .12L * std::abs(local[1].value) +
                 .18L * std::abs(local[2].value);
        break;
      }
      case BoardingSelfJunction::ankle: {
        const auto direction =
            scale(subtract(o.points[base + 1], o.points[base + 2]),
                  literal(1) / literal(.47478));
        const auto local = transform(transpose(o.frames[side + 2]), direction);
        extent = .06L * std::abs(local[0].value) +
                 .14L * std::abs(local[2].value) +
                 .05L * std::abs(local[1].value) - .05L * local[1].value;
        check(local[1].value > 0, "Actual ankle ownership can cancel boot "
                                  "thickness only with authentic upward ray");
        break;
      }
      case BoardingSelfJunction::waist: extent = .12L; break;
      case BoardingSelfJunction::neck: extent = limit = .2695L; break;
      case BoardingSelfJunction::wrist: extent = .05L; break;
      case BoardingSelfJunction::knee:
      case BoardingSelfJunction::elbow: {
        const auto cosine =
            r.junction == BoardingSelfJunction::knee
                ? -(o.D[side].value - .47285L * .47285L - .47478L * .47478L) /
                      (2 * .47285L * .47478L)
                : std::sqrt(.5L);
        const auto radius =
            r.junction == BoardingSelfJunction::knee ? .105L : .065L;
        extent = radius * radius;
        limit = static_cast<long double>(r.limit_metres) * r.limit_metres *
                (1 - cosine) / 2;
        break;
      }
      case BoardingSelfJunction::shoulder: {
        const auto q = std::sqrt(.5L);
        extent = -std::sqrt(static_cast<long double>(r.limit_metres) *
                                r.limit_metres -
                            .065L * .065L) *
                     q +
                 .065L * q;
        secondary = -.35898L * q + .065L;
        limit = -.18L;
        break;
      }
    }
    check(contains(owner.extent, extent) && contains(owner.limit, limit) &&
              contains(owner.secondary_extent, secondary),
          "Independent original finite junction extent and limit enclosed "
          "under actual rotated frames");
  }
}
void bindings(const Diagnostic& d) {
  std::uint32_t total{};
  std::size_t boxes{}, capsules{};
  for (std::size_t i = 0; i < 15; ++i) {
    const auto& p = d.parts[i];
    const auto& m = masses[i];
    check(static_cast<std::size_t>(p.id) == i &&
              static_cast<std::size_t>(p.mass.first) == m.first &&
              static_cast<std::size_t>(p.mass.second) == m.second &&
              p.mass.weight == m.weight,
          "Named stage preserves all original point and mass bindings");
    total += p.mass.weight;
    if (const auto* b =
            std::get_if<BoardingRoutePhaseBoxBinding>(&p.reservation)) {
      ++boxes;
      const RigidVector3 h = i == 0   ? RigidVector3{.24, .12, .18}
                             : i == 1 ? RigidVector3{.26, .2695, .18}
                             : i == 2 ? RigidVector3{.16, .18, .18}
                             : (i == 5 || i == 11)
                                 ? RigidVector3{.06, .05, .14}
                                 : RigidVector3{.04, .05, .02};
      check(b->half_size_metres == h,
            "Full WORLD box remains original despite separate SELF ellipsoid");
    } else
      ++capsules;
  }
  check(total == 1200 && boxes == 7 && capsules == 8,
        "Whole original fifteen-part WORLD roster retains1200 mass");
}
void accounting(const Diagnostic& d, const Limits& l = {}) {
  denied(d);
  check(d.initial != nullptr, "Compound stage owns exactly one original child");
  if (!d.initial) return;
  bindings(d);
  const auto& w = d.work;
  check(d.examined_nodes <= l.phase.nodes && d.maximum_depth <= l.phase.depth &&
            d.cells.size() <= l.phase.leaves &&
            d.output_capacity_bytes <= l.output_bytes,
        "Actual shared closed cover and owned output stay bounded");
  check(w.phase.graphs <= d.examined_nodes &&
            w.phase.legs <= 2 * w.phase.graphs &&
            w.phase.bodies <= w.phase.graphs &&
            w.phase.sectors <= 3 * w.phase.graphs &&
            w.phase.timing <= 6 * w.phase.graphs,
        "Each compound node invokes one shared original phase graph");
  check(w.contact.self_pairs <= l.pairs && w.contact.proposed_axes <= l.axes &&
            w.contact.signed_trials <= l.signed_trials &&
            w.ownership_attempts <= l.owners &&
            w.contact.pressure_candidates <= l.candidates &&
            w.contact.disk_edges <= l.edges,
        "Actual self, owner and finite-pressure counters obey lowered caps");
  check(w.contact.self_pairs <= 105 * d.examined_nodes &&
            w.contact.proposed_axes <= 14 * w.contact.self_pairs &&
            w.contact.signed_trials <= 2 * w.contact.proposed_axes &&
            w.ownership_attempts <= 14 * d.examined_nodes &&
            w.contact.pressure_candidates <= 20 * d.examined_nodes &&
            w.contact.disk_edges <= 88 * d.examined_nodes,
        "Aggregate work relationships retain actual finite per-node method");
  const auto& load = d.initial->load;
  if (load.complete)
    check(d.owned_initial_quad_guards == 10 &&
              d.owned_initial_quad_edges == 40 &&
              d.owned_initial_convexity_signs == 80 &&
              d.owned_initial_triangle_windings == 20 &&
              d.owned_initial_diagonal_incidence_guards == 10,
          "Original genuine quad guards are inherited once rather than "
          "recomputed per cell");
  auto cursor = std::min(d.requested_first, d.requested_last);
  for (const auto& c : d.cells) {
    check(c.phase.first == cursor && c.phase.first <= c.phase.last &&
              c.phase.complete && c.complete && c.arithmetic_supported &&
              c.self_complete && c.nonnegative_reactions &&
              c.nominal_vertical_equilibrium_complete &&
              c.finite_pressure_complete,
          "Accepted same-cell phase/self/support prefix is exactly closed "
          "and contiguous");
    cursor = c.phase.last;
    accepted_timing(c.phase);
    std::array<std::uint16_t, 6> counts{};
    for (auto certificate : c.pair_certificates) {
      const auto kind = static_cast<std::size_t>(certificate);
      check(kind > 0 && kind < counts.size(),
            "Every one of105 canonical self pairs retains accepted "
            "certificate");
      if (kind < counts.size()) ++counts[kind];
    }
    check(counts == c.certificate_counts,
          "Compact certificate counts reconcile all105 pairs");
    for (std::size_t owner_index = 0; owner_index < c.owners.size();
         ++owner_index) {
      const auto& owner = c.owners[owner_index];
      bool strict_plane = false;
      for (std::size_t pair = 0; pair < d.initial->self.pairs.size(); ++pair)
        if (d.initial->self.pairs[pair].connected_region_index == owner_index)
          strict_plane =
              c.pair_certificates[pair] ==
              BoardingSourceEndpointSelfCertificate::convex_support_plane;
      check(owner.arithmetic_supported && (owner.certified || strict_plane),
            "Every original connected region retains actual owner proof or "
            "separation fallback");
    }
    for (double u :
         std::array{c.phase.first, std::midpoint(c.phase.first, c.phase.last),
                    c.phase.last}) {
      cell_oracle(d.controls, c.phase, d.reverse, u);
      pressure_oracle(d, c, u);
      ownership_oracle(d, c, u);
    }
    check(c.endpoint_zero_port_reaction == (c.phase.last == 1),
          "Only a cell with canonical endpoint1 retains its zero-share "
          "endpoint witness");
  }
  if (d.complete)
    check(!d.first_refusal &&
              cursor == std::max(d.requested_first, d.requested_last) &&
              d.arithmetic_supported && d.kinematics_complete &&
              d.timing_complete && d.self_complete && d.nonnegative_reactions &&
              d.nominal_vertical_equilibrium_complete &&
              d.finite_pressure_complete && d.support_complete,
          "Successful exact requested cover carries every partial nominal "
          "support prerequisite");
  else
    check(d.first_refusal.has_value(), "Unresolved stage retains genuine "
                                       "refused interval and actual prefix");
  check(d.reporting_elapsed_seconds ==
            12 * std::abs(d.requested_last - d.requested_first),
        "Reverse/sub/point retain original physical12 second parameter clock");
  if (d.complete)
    check(d.endpoint_zero_port_reaction ==
              (std::max(d.requested_first, d.requested_last) == 1),
          "Whole or partial stage only unloads when it includes canonical "
          "endpoint1");
}
void observe(std::string_view label, const Diagnostic& d) {
  std::cout << std::setprecision(17) << label << " complete=" << d.complete
            << " cells=" << d.cells.size() << " nodes=" << d.examined_nodes
            << " work=" << d.work.phase.graphs << ','
            << d.work.contact.self_pairs << ',' << d.work.contact.proposed_axes
            << ',' << d.work.contact.signed_trials << ','
            << d.work.ownership_attempts << ','
            << d.work.contact.pressure_candidates << ','
            << d.work.contact.disk_edges;
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    std::cout << " refusal=" << static_cast<unsigned>(r.condition)
              << " predicate=" << static_cast<unsigned>(r.predicate_condition)
              << " phase=" << static_cast<unsigned>(r.phase_condition)
              << " interval=" << r.first << ',' << r.last
              << " depth=" << r.depth;
    if (r.side) std::cout << " side=" << *r.side;
    if (r.pair) std::cout << " pair=" << *r.pair;
    if (r.partition) std::cout << " source=" << *r.partition;
  }
  if (!d.cells.empty())
    std::cout << " prefix=" << d.cells.front().phase.first << ','
              << d.cells.back().phase.last;
  std::cout << '\n';
  std::cout.flush();
}
void source_geometry(const OriginBoardingBootSupport& source) {
  const auto partitions = source.selected_partitions();
  check(partitions.size() == 10,
        "Provider retains original paired upper9 and transition partition");
  for (std::size_t i = 0; i < partitions.size(); ++i) {
    const auto& p = partitions[i];
    const auto q = require(detail::boarding_source_endpoint_load_quad_math(
        p.perimeter_metres, p.plane_metres));
    check(q.arithmetic_supported && q.valid && q.checked_edges == 4 &&
              q.checked_side_signs == 8,
          "Actual source quad contains every positive winding and finite "
          "convexity guard");
    check(p.faces.size() == 2,
          "Authentic quad retains actual triangle diagonal pair");
    for (const auto& f : p.faces) {
      for (auto v : f.points_current_metres)
        check(v.y == p.plane_metres,
              "Original six triangle corners use exact stored source plane");
      const auto &a = f.points_current_metres[0],
                 &b = f.points_current_metres[1],
                 &c = f.points_current_metres[2];
      const long double upward = (static_cast<long double>(b.z) - a.z) *
                                     (static_cast<long double>(c.x) - a.x) -
                                 (static_cast<long double>(b.x) - a.x) *
                                     (static_cast<long double>(c.z) - a.z);
      check(upward > 0 && f.unit_normal_current.y > 0,
            "Independent genuine triangle winding remains upward");
    }
    std::size_t shared{};
    for (auto a : p.faces[0].points_current_metres)
      for (auto b : p.faces[1].points_current_metres)
        shared += a == b;
    check(shared == 2, "Original source triangle pair shares exactly one "
                       "authentic diagonal");
  }
  if (partitions.size() == 10) {
    const auto& t = partitions[9];
    check(t.plane_metres == -160000.0 * 1e-6 &&
              t.faces[0].key.triangle == 62119 &&
              t.faces[1].key.triangle == 62120,
          "Transition preserves original source triangle ordinals and plane");
    for (auto v : t.perimeter_metres)
      check((v.x == 498841.0 * 1e-6 || v.x == -498841.0 * 1e-6 ||
             v.x == 500881.0 * 1e-6 || v.x == -500881.0 * 1e-6) &&
                (v.z == -988000.0 * 1e-6 || v.z == -612000.0 * 1e-6),
            "Genuine transition taper uses original micrometre corner "
            "products");
  }
}
void numeric_controls() {
  using B = BoardingPlantedLegScalarBounds;
  const auto exact = [](double x) { return B{x, x}; };
  const std::array<B, 2> com{exact(.19), exact(-.67)};
  const std::array<std::array<B, 2>, 2> soles{
      {{exact(.02), exact(-.5)}, {exact(.3), exact(-.8)}}};
  const std::array<double, 2> planes{-.1, -.16};
  for (double w : {0., .25, .5, 1.})
    for (std::size_t side = 0; side < 2; ++side) {
      const auto e = require(detail::boarding_route_port_unload_pressure_math(
          com, soles, exact(w), planes, planes[side], side));
      check(e.arithmetic_supported && e.selected_plane_equal &&
                !e.source_qualified && !e.body_qualified && !e.self_qualified &&
                !e.support_qualified && !e.actor_qualified,
            "Arithmetic-only asymmetric zero/nonzero pressure has no "
            "authority");
      for (std::size_t a = 0; a < 2; ++a) {
        const long double bary =
                              static_cast<long double>(w) * soles[0][a].lower +
                              (1 - static_cast<long double>(w)) *
                                  soles[1][a].lower,
                          delta = static_cast<long double>(com[a].lower) - bary;
        check(contains(e.barycenter_xz[a], bary) &&
                  contains(e.delta_xz[a], delta),
              "Independent synthetic asymmetric pressure barycenter and "
              "shared delta enclosed");
        for (std::size_t foot = 0; foot < 2; ++foot)
          check(
              contains(e.pressure_xz[foot][a],
                       static_cast<long double>(soles[foot][a].lower) + delta),
              "Both synthetic derived pressures use common delta");
        if (w == 0)
          check(contains(e.pressure_xz[1][a], com[a].lower),
                "Zero port weight gives synthetic star pressure at full COM");
        if (w == 1)
          check(contains(e.pressure_xz[0][a], com[a].lower),
                "Zero star weight gives synthetic port pressure at full COM");
      }
      const auto wrong =
          require(detail::boarding_route_port_unload_pressure_math(
              com, soles, exact(w), planes, std::nextafter(planes[side], 0.),
              side));
      check(!wrong.selected_plane_equal, "One-step different pressure plane "
                                         "cannot gain coplanar certificate");
    }
  for (double w : {-.001, 1.001, std::numeric_limits<double>::infinity()})
    check(!detail::boarding_route_port_unload_pressure_math(
              com, soles, exact(w), planes, planes[0], 0),
          "Synthetic reaction outside nonnegative complementary domain "
          "refuses");
  check(!detail::boarding_route_port_unload_pressure_math(
            com, soles, {1, 0}, planes, planes[0], 0) &&
            !detail::boarding_route_port_unload_pressure_math(
                com, soles, exact(.5), planes, planes[0], 2),
        "Malformed pressure bounds and selected side refuse");
  const auto rotated = yaw(Jet{.25, 0, 0});
  detail::BoardingRoutePortUnloadNumericSolid solid;
  solid.first = {{-.2, .3, .4}, {-.2, .3, .4}};
  solid.second = {{.1, .7, -.2}, {.1, .7, -.2}};
  for (std::size_t col = 0; col < 3; ++col) {
    const RigidVector3 v{static_cast<double>(rotated[0][col].value),
                         static_cast<double>(rotated[1][col].value),
                         static_cast<double>(rotated[2][col].value)};
    solid.columns[col] = {v, v};
  }
  for (auto shape : std::array{BoardingSourceEndpointSelfShape::box,
                               BoardingSourceEndpointSelfShape::ellipsoid,
                               BoardingSourceEndpointSelfShape::capsule}) {
    solid.shape = shape;
    solid.half_size_metres = shape == BoardingSourceEndpointSelfShape::capsule
                                 ? RigidVector3{}
                                 : RigidVector3{.24, .12, .18};
    solid.radius_metres =
        shape == BoardingSourceEndpointSelfShape::capsule ? .105 : 0;
    for (auto n : std::array{RigidVector3{1, 0, 0}, RigidVector3{-1, 0, 0},
                             RigidVector3{0, 1, 0}, RigidVector3{0, 0, -1},
                             RigidVector3{1, -2, .5}}) {
      const auto e =
          require(detail::boarding_route_port_unload_support_math(solid, n));
      check(e.arithmetic_supported && !e.source_qualified &&
                !e.body_qualified && !e.self_qualified &&
                !e.support_qualified && !e.actor_qualified,
            "Rotated support fixture cannot enroll source or self authority");
      long double center{}, other{}, norm{};
      for (std::size_t a = 0; a < 3; ++a) {
        center += static_cast<long double>(component(solid.first.lower, a)) *
                  component(n, a);
        other += static_cast<long double>(component(solid.second.lower, a)) *
                 component(n, a);
        norm += static_cast<long double>(component(n, a)) * component(n, a);
      }
      long double h = center;
      if (shape == BoardingSourceEndpointSelfShape::capsule)
        h = std::max(center, other) +
            static_cast<long double>(solid.radius_metres) * std::sqrt(norm);
      else {
        long double contribution{};
        for (std::size_t col = 0; col < 3; ++col) {
          long double projection{};
          for (std::size_t a = 0; a < 3; ++a)
            projection += static_cast<long double>(
                              component(solid.columns[col].lower, a)) *
                          component(n, a);
          const auto term = component(solid.half_size_metres, col) * projection;
          contribution += shape == BoardingSourceEndpointSelfShape::box
                              ? std::abs(term)
                              : term * term;
        }
        h += shape == BoardingSourceEndpointSelfShape::box
                 ? contribution
                 : std::sqrt(contribution);
      }
      check(contains(e.support, h),
            "Independent actual rotated box/ellipsoid/capsule maximum "
            "support enclosed");
    }
  }
  solid.shape = BoardingSourceEndpointSelfShape::box;
  solid.half_size_metres = {.24, .12, .18};
  solid.radius_metres = 0;
  check(!detail::boarding_route_port_unload_support_math(solid, {}),
        "Zero proposal direction refuses numeric support");
  auto malformed = solid;
  malformed.columns[0].lower.x = 2;
  check(!detail::boarding_route_port_unload_support_math(malformed, {1, 0, 0}),
        "Frame interval outside original range refuses");
  malformed = solid;
  malformed.first.lower.x = std::numeric_limits<double>::quiet_NaN();
  check(!detail::boarding_route_port_unload_support_math(malformed, {1, 0, 0}),
        "Nonfinite numeric solid cannot retain support");
  malformed = solid;
  malformed.half_size_metres.y = 0;
  check(!detail::boarding_route_port_unload_support_math(malformed, {1, 0, 0}),
        "Zero active full-solid dimension refuses");
}
void invalid_controls(const OriginBoardingBootSupport& source) {
  const auto inf = std::numeric_limits<double>::infinity(),
             nan = std::numeric_limits<double>::quiet_NaN();
  for (double bad : {-1., std::nextafter(1., inf), inf, -inf, nan})
    check(!assess_origin_boarding_route_port_unload(source, bad, 1) &&
              !assess_origin_boarding_route_port_unload(source, 0, bad),
          "Invalid requested endpoint refuses before source/graph work");
  auto copied = source;
  const auto retained = std::move(copied);
  check(!assess_origin_boarding_route_port_unload(copied),
        "Moved-from genuine provider cannot acquire unload context");
  (void)retained;
  Limits raised;
  ++raised.pairs;
  check(!detail::boarding_route_port_unload_bounded(source, 0, 1, raised),
        "Private compound budgets cannot raise fixed original ceilings");
  for (int stage = 0; stage < 4; ++stage) {
    Limits l;
    switch (stage) {
      case 0: l.initial_source_partitions = 0; break;
      case 1: l.initial_body_records = 0; break;
      case 2: l.initial_self_pairs = 0; break;
      default: l.initial_pressure_partitions = 0; break;
    }
    const auto d =
        require(detail::boarding_route_port_unload_bounded(source, 0, 0, l));
    check(!d.complete && d.first_refusal &&
              d.first_refusal->condition == Condition::initial_prerequisite &&
              d.work.phase.graphs == 0 && d.work.contact.self_pairs == 0 &&
              d.cells.empty(),
          "Incomplete original child blocks new graph/self/support work");
    denied(d);
  }
  for (int stage = 0; stage < 3; ++stage) {
    Limits l;
    Condition why{};
    if (stage == 0) {
      l.phase.nodes = 0;
      why = Condition::node_capacity;
    }
    if (stage == 1) {
      l.phase.leaves = 0;
      why = Condition::leaf_capacity;
    }
    if (stage == 2) {
      l.output_bytes = 0;
      why = Condition::output_capacity;
    }
    const auto d =
        require(detail::boarding_route_port_unload_bounded(source, 0, 0, l));
    check(!d.complete && d.first_refusal && d.first_refusal->condition == why &&
              d.work.phase.graphs == 0 && d.cells.empty(),
          "Zero cover/output capacity checked before next genuine operation");
    denied(d);
  }
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
    const auto d = assess_origin_boarding_route_port_unload(source);
    check(!d || (!d->complete && !d->arithmetic_supported &&
                 !d->self_complete && !d->support_complete),
          "Unsafe environment cannot retain source-bound compound acceptance");
    const std::array<Bounds, 2> com{{{.1, .1}, {-.7, -.7}}};
    const std::array<std::array<Bounds, 2>, 2> soles{
        std::array<Bounds, 2>{Bounds{0, 0}, Bounds{-.5, -.5}},
        std::array<Bounds, 2>{Bounds{.3, .3}, Bounds{-.8, -.8}}};
    const auto e = detail::boarding_route_port_unload_pressure_math(
        com, soles, {0, 0}, {-.1, -.16}, -.16, 1);
    check(!e || !e->arithmetic_supported,
          "Unsafe synthetic pressure has no arithmetic authority");
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
auto snapshot(const Cell& c) -> std::vector<std::uint64_t> {
  std::vector<std::uint64_t> out;
  auto scalar = [&](double x) {
    out.push_back(std::bit_cast<std::uint64_t>(x));
  };
  auto bounds = [&](BoardingPlantedLegScalarBounds b) {
    scalar(b.lower);
    scalar(b.upper);
  };
  auto pt = [&](const BoardingPlantedBodyPointEvidence& p) {
    for (const auto& b : std::array{p.value, p.derivatives.velocity,
                                    p.derivatives.acceleration})
      for (auto v : std::array{b.lower, b.upper})
        for (std::size_t a = 0; a < 3; ++a)
          scalar(component(v, a));
  };
  scalar(c.first);
  scalar(c.last);
  for (const auto& p : c.points)
    pt(p);
  for (const auto& p : c.mass_points)
    pt(p);
  pt(c.center_of_mass);
  for (const auto& f : c.frames)
    for (const auto& p : f.columns)
      pt(p);
  for (const auto& l : c.legs) {
    for (auto b : std::array{l.distance_squared, l.rho_squared, l.gamma_squared,
                             l.alpha, l.gamma})
      bounds(b);
    for (auto j :
         std::array{l.hip_pitch, l.shin_pitch, l.knee_flex, l.hip_axial,
                    l.hip_abduction, l.ankle_pitch, l.ankle_roll}) {
      bounds(j.rate);
      bounds(j.coordinate_second);
    }
    for (auto b : l.joint_speeds)
      bounds(b);
    for (auto b : l.sector_margins)
      bounds(b);
    for (bool b :
         std::array{l.nominal_links, l.target_sole_identity,
                    l.derivative_domains, l.joint_sectors, l.timing_complete})
      out.push_back(b);
  }
  for (auto b : std::array{c.root_speed, c.root_acceleration, c.root_yaw_speed,
                           c.torso_joint_speed, c.port_reaction_fraction})
    bounds(b);
  for (auto b : c.sole_center_speed)
    bounds(b);
  for (auto b : c.sole_yaw_speed)
    bounds(b);
  for (auto b : c.whole_sole_speed)
    bounds(b);
  for (bool b : std::array{c.arithmetic_supported, c.nominal_links,
                           c.target_sole_identities, c.joint_sectors,
                           c.derivative_domains, c.timing_complete, c.complete})
    out.push_back(b);
  return out;
}
auto compound_snapshot(const Compound& c) -> Snapshot {
  Snapshot s;
  s.values = snapshot(c.phase);
  for (const auto& o : c.owners) {
    s.scalar(o.extent);
    s.scalar(o.limit);
    s.scalar(o.secondary_extent);
    s.integer(o.certificate);
    s.integer(o.arithmetic_supported);
    s.integer(o.certified);
    s.integer(o.structural_identity);
  }
  for (auto certificate : c.pair_certificates)
    s.integer(certificate);
  for (auto count : c.certificate_counts)
    s.integer(count);
  for (const auto& p : c.pressures) {
    for (auto b : p.pressure_xz)
      s.scalar(b);
    s.scalar(p.sole_minimum_signed_side);
    s.scalar(p.sole_minimum_squared_gap);
    for (const auto& q : p.candidates) {
      s.scalar(q.minimum_signed_side);
      s.scalar(q.minimum_squared_gap);
      s.integer(q.status);
      s.integer(q.arithmetic_supported);
      s.integer(q.quad_valid);
    }
    s.integer(p.source_partition);
    s.integer(p.scanned_partitions);
    for (bool b :
         std::array{p.arithmetic_supported, p.coplanar, p.sole_disk_contained,
                    p.source_disk_contained, p.coverage_complete, p.complete})
      s.integer(b);
  }
  s.scalar(c.port_reaction_fraction);
  for (auto b : c.barycenter_xz)
    s.scalar(b);
  for (auto b : c.common_pressure_delta_xz)
    s.scalar(b);
  for (bool b : std::array{
           c.arithmetic_supported, c.self_complete, c.nonnegative_reactions,
           c.nominal_vertical_equilibrium_complete, c.finite_pressure_complete,
           c.endpoint_zero_port_reaction, c.complete})
    s.integer(b);
  return s;
}
void observed_budgets(const OriginBoardingBootSupport& source) {
  const auto base =
      require(assess_origin_boarding_route_port_unload(source, 0, 0));
  observe("POINT_BASE", base);
  accounting(base);
  if (!base.complete) return;
  check(base.cells.size() == 1,
        "Actual complete source-bound point owns one shared compound cell");
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
  const auto replay =
      require(detail::boarding_route_port_unload_bounded(source, 0, 0, exact));
  check(replay.complete &&
            compound_snapshot(replay.cells[0]) ==
                compound_snapshot(base.cells[0]) &&
            snapshot(*replay.initial) == snapshot(*base.initial),
        "Exact actually used point capacities retain original child and "
        "fieldwise complete compound result");
  for (int stage = 0; stage < 14; ++stage) {
    auto l = exact;
    Condition condition{};
    BoardingRouteFootPhaseCondition phase_condition{};
    bool used = true;
    switch (stage) {
      case 0:
        used = l.pairs > 0;
        if (used) --l.pairs;
        condition = Condition::pair_capacity;
        break;
      case 1:
        used = l.axes > 0;
        if (used) --l.axes;
        condition = Condition::axis_capacity;
        break;
      case 2:
        used = l.signed_trials > 0;
        if (used) --l.signed_trials;
        condition = Condition::signed_trial_capacity;
        break;
      case 3:
        used = l.owners > 0;
        if (used) --l.owners;
        condition = Condition::ownership_capacity;
        break;
      case 4:
        used = l.candidates > 0;
        if (used) --l.candidates;
        condition = Condition::pressure_capacity;
        break;
      case 5:
        used = l.edges > 0;
        if (used) --l.edges;
        condition = Condition::edge_capacity;
        break;
      case 6:
        used = l.phase.nodes > 0;
        if (used) --l.phase.nodes;
        condition = Condition::node_capacity;
        break;
      case 7:
        used = l.phase.leaves > 0;
        if (used) --l.phase.leaves;
        condition = Condition::leaf_capacity;
        break;
      case 8:
        used = l.output_bytes > 0;
        if (used) --l.output_bytes;
        condition = Condition::output_capacity;
        break;
      case 9:
        used = l.phase.graphs > 0;
        if (used) --l.phase.graphs;
        condition = Condition::phase_predicate;
        phase_condition = BoardingRouteFootPhaseCondition::graph_capacity;
        break;
      case 10:
        used = l.phase.legs > 0;
        if (used) --l.phase.legs;
        condition = Condition::phase_predicate;
        phase_condition = BoardingRouteFootPhaseCondition::leg_capacity;
        break;
      case 11:
        used = l.phase.bodies > 0;
        if (used) --l.phase.bodies;
        condition = Condition::phase_predicate;
        phase_condition = BoardingRouteFootPhaseCondition::body_capacity;
        break;
      case 12:
        used = l.phase.sectors > 0;
        if (used) --l.phase.sectors;
        condition = Condition::phase_predicate;
        phase_condition = BoardingRouteFootPhaseCondition::sector_capacity;
        break;
      default:
        used = l.phase.timing > 0;
        if (used) --l.phase.timing;
        condition = Condition::phase_predicate;
        phase_condition = BoardingRouteFootPhaseCondition::timing_capacity;
        break;
    }
    if (!used) continue;
    const auto less =
        require(detail::boarding_route_port_unload_bounded(source, 0, 0, l));
    accounting(less, l);
    check(!less.complete && less.first_refusal &&
              less.first_refusal->condition == condition,
          "One less genuinely used operation capacity refuses at actual "
          "original stage");
    if (less.first_refusal && condition == Condition::phase_predicate)
      check(less.first_refusal->phase_condition == phase_condition,
            "Compound capacity refusal retains original phase stage");
  }
  for (int stage = 0; stage < 6; ++stage) {
    auto l = Limits{};
    Condition condition{};
    switch (stage) {
      case 0:
        l.pairs = 0;
        condition = Condition::pair_capacity;
        break;
      case 1:
        l.axes = 0;
        condition = Condition::axis_capacity;
        break;
      case 2:
        l.signed_trials = 0;
        condition = Condition::signed_trial_capacity;
        break;
      case 3:
        l.owners = 0;
        condition = Condition::ownership_capacity;
        break;
      case 4:
        l.candidates = 0;
        condition = Condition::pressure_capacity;
        break;
      default:
        l.edges = 0;
        condition = Condition::edge_capacity;
        break;
    }
    const auto zero =
        require(detail::boarding_route_port_unload_bounded(source, 0, 0, l));
    accounting(zero, l);
    check(!zero.complete && zero.first_refusal &&
              zero.first_refusal->condition == condition,
          "Original unchanged point independently reaches each zero compound "
          "operation cap");
  }
  Diagnostic context_owner;
  const auto context = require(detail::prepare_boarding_route_port_unload(
      source, Limits{}, context_owner));
  for (int stage = 0; stage < 6; ++stage) {
    Limits l;
    BoardingRoutePortUnloadCounters work;
    Condition condition{};
    switch (stage) {
      case 0:
        l.pairs = 0;
        work.contact.self_pairs = 1;
        condition = Condition::pair_capacity;
        break;
      case 1:
        l.axes = 0;
        work.contact.proposed_axes = 1;
        condition = Condition::axis_capacity;
        break;
      case 2:
        l.signed_trials = 0;
        work.contact.signed_trials = 1;
        condition = Condition::signed_trial_capacity;
        break;
      case 3:
        l.owners = 0;
        work.ownership_attempts = 1;
        condition = Condition::ownership_capacity;
        break;
      case 4:
        l.candidates = 0;
        work.contact.pressure_candidates = 1;
        condition = Condition::pressure_capacity;
        break;
      default:
        l.edges = 0;
        work.contact.disk_edges = 1;
        condition = Condition::edge_capacity;
        break;
    }
    Compound stale = base.cells[0];
    BoardingRoutePortUnloadRefusal why;
    const auto state = detail::boarding_route_port_unload_cell(
        context, 0, 0, false, l, work, stale, why);
    check(state == State::capacity && why.condition == condition &&
              work.phase.graphs == 0 && !stale.complete &&
              !stale.self_complete && !stale.finite_pressure_complete,
          "Incoming above-cap work refuses before graph increment and clears "
          "stale acceptance");
  }
  auto moved_context = context;
  const auto retained_context = std::move(moved_context);
  BoardingRoutePortUnloadCounters work;
  Compound stale = base.cells[0];
  BoardingRoutePortUnloadRefusal why;
  const auto moved_state = detail::boarding_route_port_unload_cell(
      moved_context, 0, 0, false, Limits{}, work, stale, why);
  check(moved_state == State::unsupported &&
            why.condition == Condition::invalid_binding && !stale.complete &&
            work.phase.graphs == 0,
        "Moved-from query context cannot reuse stale compound admission");
  (void)retained_context;
}
void public_cases(const OriginBoardingBootSupport& source) {
  const auto old =
      snapshot(require(assess_origin_boarding_source_endpoint_load(source)));
  auto initial = assess_origin_boarding_route_port_unload(source);
  if (!initial) {
    std::cout << "FIRST_PUBLIC_PORT_UNLOAD API_ERROR=" << initial.error()
              << '\n';
    std::cout.flush();
  }
  auto first = require(std::move(initial));
  observe("FIRST_PUBLIC_PORT_UNLOAD", first);
  accounting(first);
  check(snapshot(*first.initial) == old,
        "One fresh compound child remains fieldwise identical to original "
        "load/self/endpoint/sites");
  check(
      first.initial->self.endpoint.sites.sites[0].pressure_offset_terms[1][0] ==
          .040,
      "Old Sites02 geometric port40mm witness remains unchanged");
  observed_budgets(source);
  for (const auto range : std::array<std::array<double, 2>, 7>{{{1, 0},
                                                                {.125, .875},
                                                                {.875, .125},
                                                                {0, 0},
                                                                {.5, .5},
                                                                {1, 1},
                                                                {.875, 1}}}) {
    const auto d = require(
        assess_origin_boarding_route_port_unload(source, range[0], range[1]));
    observe("REQUEST", d);
    accounting(d);
    check(snapshot(*d.initial) == old,
          "Each independent reverse/sub/point owns exactly the unchanged "
          "source child");
    if (first.complete)
      check(d.complete, "Observed successful full unload remains complete for "
                        "valid reverse/sub/points");
    if (d.complete && range[0] == 1 && range[1] == 1) {
      check(d.cells.size() == 1 &&
                d.cells[0].port_reaction_fraction.lower == 0 &&
                d.cells[0].port_reaction_fraction.upper == 0 &&
                d.endpoint_zero_port_reaction,
            "Exact canonical endpoint carries genuine unloaded port and "
            "single-foot support");
      for (std::size_t a = 0; a < 2; ++a) {
        const auto axis = a == 0 ? 0U : 2U;
        const auto actual = d.cells[0].pressures[1].pressure_xz[a];
        const auto& com = d.cells[0].phase.center_of_mass.value;
        check(actual.lower == component(com.lower, axis) &&
                  actual.upper == component(com.upper, axis),
              "Starboard pressure endpoint retains same algebraic full COM "
              "bounds");
      }
    }
  }
  check(snapshot(require(
            assess_origin_boarding_source_endpoint_load(source))) == old,
        "New compound stage never mutates original owned source state");
}
} // namespace
int main() {
  try {
    static_assert(!std::is_default_constructible_v<
                  detail::BoardingRoutePortUnloadCellToken>);
    static_assert(
        !std::is_constructible_v<detail::BoardingRoutePortUnloadCellToken,
                                 const detail::BoardingRoutePortUnloadContext&,
                                 const Compound&>);
    static_assert(
        !std::is_constructible_v<detail::BoardingRoutePortUnloadContext,
                                 const Load&>);
    const auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    const auto source = require(make_origin_boarding_boot_support(binding));
    invalid_controls(source);
    unsafe_environment(source);
    numeric_controls();
    source_geometry(source);
    public_cases(source);
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
