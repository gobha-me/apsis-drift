#include "apsis_drift/origin_boarding_source_endpoint.hpp"
#include "origin_boarding_source_endpoint_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#if defined(__SSE2__) && defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace {
using namespace apsis_drift;
std::size_t checks{};
int failures{};
constexpr double upper_plane{kCabinCorridorFloorMetres};
constexpr double transition_plane{-160000.0 * 1e-6};
auto check(bool value, std::string_view label) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T, class E> auto require(std::expected<T, E> value) -> T {
  if (!value)
    throw std::runtime_error("Required source-bound endpoint API refused");
  return std::move(*value);
}
auto bits(double a, double b) -> bool {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
struct Point {
  long double x{}, y{}, z{};
};
auto add(Point a, Point b) -> Point {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(Point a, Point b) -> Point {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(Point p, long double s) -> Point {
  return {p.x * s, p.y * s, p.z * s};
}
auto dot(Point a, Point b) -> long double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
auto component(Point p, std::size_t axis) -> long double {
  return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
}
auto component(RigidVector3 p, std::size_t axis) -> double {
  return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
}
auto contains(BoardingPlantedLegScalarBounds b, long double x) -> void {
  // LD error allowance corroborates only this independent unfactored finite
  // oracle; it never admits production geometry or narrows outward bounds.
  const auto error = 8192 * std::numeric_limits<long double>::epsilon() *
                     std::max(1.L, std::abs(x));
  check(std::isfinite(b.lower) && std::isfinite(b.upper) &&
            b.lower <= b.upper &&
            static_cast<long double>(b.lower) <= x + error &&
            x - error <= static_cast<long double>(b.upper),
        "Independent endpoint expression belongs to outward enclosure");
}
auto point_contains(const BoardingPlantedLegPointBounds& b, Point p) -> void {
  for (std::size_t i = 0; i < 3; ++i)
    contains({component(b.lower, i), component(b.upper, i)}, component(p, i));
}
struct ExpectedPart {
  bool capsule{};
  std::size_t first{}, second{};
  RigidVector3 half;
  double radius{};
  std::uint32_t weight{};
};
const std::array<ExpectedPart, 15> expected_parts{
    {{false, 0, 0, {.24, .12, .18}, 0, 144},
     {false, 1, 1, {.26, .2695, .18}, 0, 540},
     {false, 2, 2, {.16, .18, .18}, 0, 96},
     {true, 4, 5, {}, .105, 120},
     {true, 5, 6, {}, .075, 48},
     {false, 7, 7, {.06, .05, .14}, 0, 12},
     {true, 8, 9, {}, .065, 15},
     {true, 9, 10, {}, .055, 10},
     {false, 10, 10, {.04, .05, .02}, 0, 5},
     {true, 11, 12, {}, .105, 120},
     {true, 12, 13, {}, .075, 48},
     {false, 14, 14, {.06, .05, .14}, 0, 12},
     {true, 15, 16, {}, .065, 15},
     {true, 16, 17, {}, .055, 10},
     {false, 17, 17, {.04, .05, .02}, 0, 5}}};
struct OracleLeg {
  Point hip, knee, ankle, boot, axis, shin;
  long double D{}, rho{}, alpha{}, gamma{}, hip_sine{}, hip_cosine{},
      shin_sine{}, shin_cosine{}, knee_cosine{}, support{}, gap{};
};
// Independent unfactored law-of-cosines reference, never a producer interval
// helper, rounded angle, BodyPose conversion or normalized report frame.
auto leg_oracle(std::size_t side, double root_y, double root_z)
    -> std::optional<OracleLeg> {
  const auto x = static_cast<long double>(.16) +
                 (side == 0 ? -static_cast<long double>(.14)
                            : static_cast<long double>(.14));
  const auto plane =
      static_cast<long double>(side == 0 ? upper_plane : transition_plane);
  const auto z = static_cast<long double>(side == 0 ? -.5 : -.8);
  const Point H{x, 0, static_cast<long double>(root_z)},
      A{x, plane + static_cast<long double>(.1) - root_y, z},
      B{x, plane + static_cast<long double>(.05) - root_y, z};
  const auto d = sub(A, H);
  const auto D = dot(d, d), rho = -d.y, L1 = static_cast<long double>(.47285),
             L2 = static_cast<long double>(.47478);
  if (!(rho > 0 && D > (L1 - L2) * (L1 - L2) && D < (L1 + L2) * (L1 + L2)))
    return {};
  const auto alpha = (L1 * L1 - L2 * L2 + D) / (2 * D);
  const auto square = L1 * L1 / D - alpha * alpha;
  if (!(square > 0)) return {};
  const auto gamma = std::sqrt(square);
  const Point q{0, -d.z, -rho};
  const auto K = add(H, add(scale(d, alpha), scale(q, gamma))), v = sub(K, H),
             shin = sub(A, K);
  const auto error = 65536 * std::numeric_limits<long double>::epsilon();
  check(d.x == 0 && std::abs(dot(q, d)) <= error &&
            std::abs(dot(q, q) - D) <= error,
        "Shared X gives structural sagittal orthogonal closure without rounded "
        "subtraction");
  check(std::abs(dot(v, v) - L1 * L1) <= error &&
            std::abs(dot(shin, shin) - L2 * L2) <= error,
        "Independent unfactored knee preserves both exact nominal lengths");
  const auto hs = -v.z / L1, hc = -v.y / L1, ss = -shin.z / L2,
             sc = -shin.y / L2;
  const auto support = static_cast<long double>(.12) * std::abs(v.y) +
                       static_cast<long double>(.18) * std::abs(v.z);
  const auto gap =
      static_cast<long double>(kBoardingSelfHipLengthMetres) * L1 - support;
  return OracleLeg{H,       K,  A,   B,     v,
                   shin,    D,  rho, alpha, gamma,
                   hs,      hc, ss,  sc,    hc * sc + hs * ss,
                   support, gap};
}
struct OracleBody {
  std::array<Point, 18> points;
  std::array<Point, 15> masses;
  Point com;
  std::array<OracleLeg, 2> legs;
};
auto body_oracle(double root_y, double root_z) -> std::optional<OracleBody> {
  const auto port = leg_oracle(0, root_y, root_z),
             star = leg_oracle(1, root_y, root_z);
  if (!port || !star) return {};
  OracleBody b;
  b.legs = {*port, *star};
  const Point P{static_cast<long double>(.16), 0,
                static_cast<long double>(root_z)};
  b.points[0] = P;
  b.points[1] = add(P, {0, static_cast<long double>(.3495), 0});
  b.points[2] = add(P, {0, static_cast<long double>(.70237), 0});
  b.points[3] = add(P, {0, static_cast<long double>(.65237), 0});
  const auto c = std::sqrt(.5L);
  for (std::size_t side = 0; side < 2; ++side) {
    const auto base = side == 0 ? std::size_t{4} : std::size_t{11};
    const auto& l = b.legs[side];
    b.points[base] = l.hip;
    b.points[base + 1] = l.knee;
    b.points[base + 2] = l.ankle;
    b.points[base + 3] = l.boot;
    const auto shoulder =
        add(P, {(side == 0 ? -1.L : 1.L) * static_cast<long double>(.20265),
                static_cast<long double>(.579), 0});
    const auto elbow = add(shoulder, {0, -static_cast<long double>(.35898) * c,
                                      -static_cast<long double>(.35898) * c});
    b.points[base + 4] = shoulder;
    b.points[base + 5] = elbow;
    b.points[base + 6] = add(elbow, {0, static_cast<long double>(.386), 0});
  }
  Point weighted{};
  for (std::size_t i = 0; i < 15; ++i) {
    const auto& p = expected_parts[i];
    b.masses[i] = scale(add(b.points[p.first], b.points[p.second]), .5L);
    weighted = add(weighted, scale(b.masses[i], p.weight));
  }
  b.com = scale(weighted, 1.L / 1200);
  return b;
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
  auto closure(const BoardingPlantedLegDiagnostic& d) -> void {
    integer(d.recipe_version);
    number(d.requested_first);
    number(d.requested_last);
    number(d.duration_seconds);
    number(d.placement_y_metres);
    integer(d.reverse);
    integer(d.complete);
    integer(d.plane_identities);
    integer(d.link_identities);
    integer(d.joint_limits_certified);
    integer(d.examined_nodes);
    integer(d.maximum_depth);
    integer(d.leaves.size());
    for (const auto& leaf : d.leaves) {
      number(leaf.first);
      number(leaf.last);
      scalar(leaf.root_x);
      for (const auto& e : leaf.legs)
        closure(e);
    }
    integer(d.first_refusal.has_value());
    if (d.first_refusal) {
      const auto& r = *d.first_refusal;
      number(r.first);
      number(r.last);
      integer(r.side);
      integer(r.depth);
      integer(r.condition);
      integer(r.limiting_condition);
    }
  }
  auto timing(const BoardingPlantedLegTimingEvidence& e) -> void {
    closure(e.closure);
    for (const auto& p : std::array{e.hip, e.knee, e.ankle, e.boot_center})
      derivatives(p);
    for (const auto& a :
         std::array{e.hip_pitch, e.shin_pitch, e.knee_flex, e.ankle_pitch,
                    e.hip_abduction, e.ankle_roll}) {
      scalar(a.rate);
      scalar(a.coordinate_second);
    }
    for (const auto& s : std::array{e.hip_speed, e.knee_speed, e.ankle_speed}) {
      scalar(s.speed_radians_per_second);
      integer(s.supported);
      integer(s.certified);
    }
    integer(e.derivative_domains_certified);
    integer(e.joint_speed_certified);
    integer(e.limiting_condition);
    integer(e.limiting_joint);
    scalar(e.limiting_bound);
  }
  auto timing(const BoardingPlantedLegTimingDiagnostic& d) -> void {
    integer(d.timing_version);
    closure(d.closure);
    number(d.requested_first);
    number(d.requested_last);
    number(d.seconds_per_parameter);
    number(d.reporting_elapsed_seconds);
    integer(d.reverse);
    integer(d.complete);
    integer(d.derivative_domains_certified);
    integer(d.root_speed_certified);
    integer(d.root_acceleration_certified);
    integer(d.leg_joint_speed_certified);
    integer(d.examined_nodes);
    integer(d.maximum_depth);
    integer(d.leaves.size());
    for (const auto& leaf : d.leaves) {
      number(leaf.first);
      number(leaf.last);
      derivatives(leaf.root);
      scalar(leaf.root_speed);
      scalar(leaf.root_acceleration);
      for (const auto& e : leaf.legs)
        timing(e);
    }
    integer(d.first_refusal.has_value());
    if (d.first_refusal) {
      const auto& r = *d.first_refusal;
      number(r.first);
      number(r.last);
      integer(r.side);
      integer(r.depth);
      integer(r.condition);
      integer(r.limiting_condition);
      integer(r.joint);
      scalar(r.limiting_bound);
    }
  }
};

auto snapshot(const BoardingPlantedBodyDiagnostic& d)
    -> std::vector<std::uint64_t> {
  Snapshot s;
  s.integer(d.recipe_version);
  s.timing(d.timing);
  s.number(d.common_translation_y_metres);
  s.integer(d.assembled_leaves);
  s.integer(d.complete);
  s.integer(d.reservations_complete);
  s.integer(d.mass_model_complete);
  s.integer(d.com_derivatives_complete);
  for (const auto& p : d.parts) {
    s.integer(p.id);
    s.integer(p.reservation.index());
    if (const auto* b =
            std::get_if<BoardingPlantedBodyBoxBinding>(&p.reservation)) {
      s.integer(b->center);
      for (std::size_t axis = 0; axis < 3; ++axis) {
        s.number(component(b->half_size_metres, axis));
        for (std::size_t row = 0; row < 3; ++row)
          s.number(component(b->frame.columns[axis], row));
      }
    } else {
      const auto& c =
          std::get<BoardingPlantedBodyCapsuleBinding>(p.reservation);
      s.integer(c.start);
      s.integer(c.end);
      s.number(c.radius_metres);
    }
    s.integer(p.mass.first);
    s.integer(p.mass.second);
    s.integer(p.mass.weight);
  }
  s.integer(d.leaves.size());
  const auto evidence = [&](const BoardingPlantedBodyPointEvidence& e) {
    s.point(e.value);
    s.derivatives(e.derivatives);
  };
  for (const auto& leaf : d.leaves) {
    s.number(leaf.first);
    s.number(leaf.last);
    for (const auto& p : leaf.points)
      evidence(p);
    for (const auto& p : leaf.mass_points)
      evidence(p);
    evidence(leaf.center_of_mass);
  }
  s.integer(d.first_refusal.has_value());
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    s.integer(r.condition);
    s.integer(r.leaf_index);
    s.number(r.first);
    s.number(r.last);
  }
  return std::move(s.values);
}

// Independent exact signed binary64 sums in units2^-1074. Separate unsigned
// accumulators preserve nextafter/subnormal height signs after cancellation.
class DyadicSum {
 public:
  auto add(double x) -> void {
    if (++terms_ > 32 || !std::isfinite(x))
      throw std::runtime_error("Independent dyadic operand bound");
    const auto raw = std::bit_cast<std::uint64_t>(x);
    const auto exponent = static_cast<unsigned>((raw >> 52) & 0x7ffU);
    const auto fraction = raw & ((std::uint64_t{1} << 52) - 1);
    const auto significand =
        exponent == 0 ? fraction : fraction | (std::uint64_t{1} << 52);
    const auto shift = exponent == 0 ? 0U : exponent - 1;
    auto& magnitude = (raw >> 63) != 0 ? negative_ : positive_;
    for (unsigned bit = 0; bit < 53; ++bit) {
      if ((significand & (std::uint64_t{1} << bit)) == 0) continue;
      const auto position = shift + bit;
      auto index = static_cast<std::size_t>(position / 32);
      std::uint64_t carry = std::uint64_t{1} << (position % 32);
      while (carry != 0) {
        if (index >= magnitude.size())
          throw std::runtime_error("Independent dyadic capacity");
        const auto sum = static_cast<std::uint64_t>(magnitude[index]) + carry;
        magnitude[index] = static_cast<std::uint32_t>(sum & 0xffffffffU);
        carry = sum >> 32;
        ++index;
      }
    }
  }
  [[nodiscard]] auto sign() const -> int {
    for (std::size_t i = positive_.size(); i > 0; --i) {
      if (positive_[i - 1] > negative_[i - 1]) return 1;
      if (positive_[i - 1] < negative_[i - 1]) return -1;
    }
    return 0;
  }

 private:
  std::array<std::uint32_t, 68> positive_{}, negative_{};
  unsigned terms_{};
};
auto sign(std::initializer_list<double> terms) -> int {
  DyadicSum result;
  for (double x : terms)
    result.add(x);
  return result.sign();
}
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
auto roster(const std::array<BoardingPlantedBodyPartBinding, 15>& parts)
    -> void {
  using P = BoardingPlantedBodyPointId;
  using B = BoardingBodyPartId;
  const std::array ids{B::pelvis,
                       B::trunk,
                       B::helmet,
                       B::port_thigh,
                       B::port_shin,
                       B::port_boot,
                       B::port_upper_arm,
                       B::port_forearm,
                       B::port_hand,
                       B::starboard_thigh,
                       B::starboard_shin,
                       B::starboard_boot,
                       B::starboard_upper_arm,
                       B::starboard_forearm,
                       B::starboard_hand};
  const std::array points{P::root,
                          P::trunk_center,
                          P::helmet_center,
                          P::eye,
                          P::port_hip,
                          P::port_knee,
                          P::port_ankle,
                          P::port_boot_center,
                          P::port_shoulder,
                          P::port_elbow,
                          P::port_wrist,
                          P::starboard_hip,
                          P::starboard_knee,
                          P::starboard_ankle,
                          P::starboard_boot_center,
                          P::starboard_shoulder,
                          P::starboard_elbow,
                          P::starboard_wrist};
  for (std::size_t i = 0; i < points.size(); ++i)
    check(static_cast<std::size_t>(points[i]) == i,
          "Unchanged18 named point-slot order");
  std::uint32_t mass{};
  std::size_t boxes{}, capsules{};
  for (std::size_t i = 0; i < parts.size(); ++i) {
    const auto& p = parts[i];
    const auto& e = expected_parts[i];
    check(p.id == ids[i] && p.mass.first == points[e.first] &&
              p.mass.second == points[e.second] && p.mass.weight == e.weight,
          "Exact original15 part/mass identities and integer weights");
    mass += p.mass.weight;
    if (e.capsule) {
      const auto* c =
          std::get_if<BoardingPlantedBodyCapsuleBinding>(&p.reservation);
      check(c && c->start == points[e.first] && c->end == points[e.second] &&
                bits(c->radius_metres, e.radius),
            "Original capsule type/radius names genuine shared joint slots");
      if (c) ++capsules;
    } else {
      const auto* b =
          std::get_if<BoardingPlantedBodyBoxBinding>(&p.reservation);
      check(b && b->center == points[e.first] && b->half_size_metres == e.half,
            "Original full box center and dimensions");
      if (b) {
        ++boxes;
        for (std::size_t axis = 0; axis < 3; ++axis)
          for (std::size_t row = 0; row < 3; ++row)
            check(bits(component(b->frame.columns[axis], row),
                       axis == row ? 1. : 0.),
                  "Fixed endpoint box frame is exact identity");
      }
    }
  }
  check(mass == 1200 && boxes == 7 && capsules == 8,
        "Complete fixed15 roster conserves all masses and types");
}
auto leg_checks(const BoardingPlantedLegEvidence& e, const OracleLeg& o,
                std::size_t side, double root_y) -> void {
  point_contains(e.hip, o.hip);
  point_contains(e.knee, o.knee);
  point_contains(e.ankle, o.ankle);
  point_contains(e.boot_center, o.boot);
  contains(e.distance_squared, o.D);
  contains(e.rho, o.rho);
  contains(e.alpha, o.alpha);
  contains(e.gamma, o.gamma);
  contains(e.hip_sine, o.hip_sine);
  contains(e.hip_cosine, o.hip_cosine);
  contains(e.shin_sine, o.shin_sine);
  contains(e.shin_cosine, o.shin_cosine);
  contains(e.knee_cosine, o.knee_cosine);
  const auto plane = side == 0 ? upper_plane : transition_plane;
  check(bits(e.plane_metres, plane) && bits(e.ankle_y_terms[0], plane) &&
            bits(e.ankle_y_terms[1], .1) && bits(e.ankle_y_terms[2], -root_y) &&
            bits(e.boot_center_y_terms[0], plane) &&
            bits(e.boot_center_y_terms[1], .05) &&
            bits(e.boot_center_y_terms[2], -root_y),
        "Source-bound ankle/boot terms retain original plane and once-only "
        "absolute placement");
  check(sign({e.boot_center_y_terms[0], e.boot_center_y_terms[1],
              e.boot_center_y_terms[2], root_y, -.05, -plane}) == 0,
        "Independent integer expression cancellation gives exact placed "
        "sole/source equality");
  check(sign({plane, .1, -root_y, -e.ankle.lower.y}) >= 0 &&
            sign({plane, .1, -root_y, -e.ankle.upper.y}) <= 0 &&
            sign({plane, .05, -root_y, -e.boot_center.lower.y}) >= 0 &&
            sign({plane, .05, -root_y, -e.boot_center.upper.y}) <= 0,
        "Integer dyadic oracle strictly encloses canonical source-plane height "
        "expressions");
  check(e.roll_sine.lower == 0 && e.roll_sine.upper == 0 &&
            e.roll_cosine.lower == 1 && e.roll_cosine.upper == 1,
        "Shared structural X establishes exact zero roll without tolerance");
  for (const auto& p : std::array{e.hip, e.knee, e.ankle, e.boot_center}) {
    const auto x = static_cast<long double>(.16) +
                   (side == 0 ? -static_cast<long double>(.14)
                              : static_cast<long double>(.14));
    contains({p.lower.x, p.upper.x}, x);
    check(sign({.16, side == 0 ? -.14 : .14, -p.lower.x}) >= 0 &&
              sign({.16, side == 0 ? -.14 : .14, -p.upper.x}) <= 0,
          "Integer dyadic bounds strictly enclose the shared two-term X, never "
          "ideal rounded .02/.30");
    check(bits(p.lower.x, e.hip.lower.x) && bits(p.upper.x, e.hip.upper.x),
          "Hip/knee/ankle/boot retain exactly the same X expression enclosure");
  }
  if (e.limits_certified) {
    const auto pi = std::acos(-1.L), hip = std::atan2(o.hip_sine, o.hip_cosine),
               shin = std::atan2(o.shin_sine, o.shin_cosine);
    const auto knee = hip - shin, ankle = -shin,
               err = 8192 * std::numeric_limits<long double>::epsilon();
    check(hip >= -20 * pi / 180 - err && hip <= 65 * pi / 180 + err &&
              knee >= -err && knee <= 135 * pi / 180 + err &&
              ankle >= -30 * pi / 180 - err && ankle <= 30 * pi / 180 + err,
          "Independent directed inverse sectors corroborate all certified "
          "fixed joint limits");
  }
}
auto inspect(const BoardingSourceEndpointDiagnostic& d, double root_y = .847,
             double root_z = -.55) -> void {
  check(
      d.endpoint_version == 1 && bits(d.common_translation_y_metres, root_y) &&
          bits(d.root_z_metres, root_z) && bits(d.port_x_terms[0], .16) &&
          bits(d.port_x_terms[1], -.14) && bits(d.starboard_x_terms[0], .16) &&
          bits(d.starboard_x_terms[1], .14),
      "Endpoint retains frozen shared X and one absolute common root Y/Z");
  check(!d.timing_qualified && !d.derivatives_qualified &&
            !d.dynamics_qualified && !d.self_qualified && !d.force_qualified &&
            !d.load_qualified && !d.world_qualified && !d.crop_qualified &&
            !d.sweep_qualified && !d.acquisition_qualified &&
            !d.free_foot_swing_qualified && !d.transfer_qualified &&
            !d.route_qualified && !d.actor_qualified && !d.seat_qualified &&
            !d.save_qualified && !d.first_flight_qualified,
        "Complete endpoint or two hip certificates grant no "
        "timing/fullself/physical/actor authority");
  check(d.assembled_records == static_cast<std::size_t>(d.body.has_value()) &&
            d.assembled_records <= 1,
        "Only one genuine complete body record can be installed");
  check(d.complete == (d.arithmetic_supported && d.sites.eligible &&
                       d.plane_identities && d.link_identities &&
                       d.joint_limits_certified && d.reservations_complete &&
                       d.mass_model_complete && d.hip_regions_certified),
        "Endpoint completeness retains every independent prerequisite");
  check(d.complete == !d.first_refusal.has_value(),
        "Refusal stays separate from complete body/limited hip result");
  check(d.hip_regions_certified == (d.hips[0].certified && d.hips[1].certified),
        "Only two actual finite hip certificates are combined");
  check(d.evaluated_legs <= 2 && d.examined_hip_regions <= 2 &&
            d.examined_hip_regions == (d.body ? 2U : 0U),
        "Recorded work retains bounded evaluated leg/hip counts with no hidden "
        "retry");
  if (d.body)
    check(d.evaluated_legs == 2,
          "Body requires two genuine evaluated leg records");
  for (std::size_t side = d.evaluated_legs; side < 2; ++side) {
    Snapshot uncomputed, zero;
    uncomputed.closure(d.legs[side]);
    zero.closure(BoardingPlantedLegEvidence{});
    check(uncomputed == zero, "Unentered second leg remains unknown default, "
                              "never invented evidence");
  }
  if (d.first_refusal && d.first_refusal->condition ==
                             BoardingSourceEndpointCondition::leg_closure)
    check(d.evaluated_legs > 0 && d.first_refusal->side == d.evaluated_legs - 1,
          "Leg refusal owns the first actually evaluated failing side");

  for (std::size_t side = 0; side < 2; ++side) {
    const auto o = leg_oracle(side, root_y, root_z);
    const auto& e = d.legs[side];
    if (e.link_identities) {
      check(
          o.has_value(),
          "Certified link identities require independent strict reach/domain");
      if (o) leg_checks(e, *o, side, root_y);
    }
    const auto& hip = d.hips[side];
    if (hip.arithmetic_supported) {
      check(o && hip.first_part == BoardingBodyPartId::pelvis &&
                hip.second_part ==
                    (side == 0 ? BoardingBodyPartId::port_thigh
                               : BoardingBodyPartId::starboard_thigh) &&
                bits(hip.axis_length_metres, .47285) &&
                bits(hip.thigh_radius_metres, .105) &&
                bits(hip.hip_limit_metres, kBoardingSelfHipLengthMetres),
            "Original finite region retains exact side/capsule/link/limit "
            "identity");
      if (o) {
        point_contains(hip.axis, o->axis);
        contains(hip.scaled_pelvis_extent, o->support);
        contains(hip.scaled_hip_limit,
                 static_cast<long double>(kBoardingSelfHipLengthMetres) *
                     static_cast<long double>(.47285));
        contains(hip.scaled_gap, o->gap);
        check(hip.axis.lower.x == 0 && hip.axis.upper.x == 0 &&
                  hip.structural_x_zero,
              "Whole pelvis support uses structurally zero transverse thigh "
              "direction");
      }
    }
    if (hip.certified) {
      check(hip.arithmetic_supported && hip.bindings_valid &&
                hip.link_identity && hip.structural_x_zero &&
                e.link_identities && hip.scaled_gap.lower >= 0 &&
                hip.thigh_radius_metres <= hip.hip_limit_metres &&
                hip.hip_limit_metres < hip.axis_length_metres,
            "Whole original pelvis/thigh common intersection has a guarded "
            "supported slab certificate");
      if (o)
        check(o->gap >= -8192 * std::numeric_limits<long double>::epsilon(),
              "Independent unnormalized whole-pelvis support corroborates "
              "granted hip ownership");
    }
  }
  if (d.body) {
    check(d.reservations_complete && d.mass_model_complete &&
              d.sites.eligible && d.link_identities && d.joint_limits_certified,
          "Body record is assembled only from complete real "
          "source/closure/sector prerequisites");
    roster(d.parts);
    const auto o = body_oracle(root_y, root_z);
    check(o.has_value(), "Complete body has both independent nonsingular "
                         "nominal-link references");
    if (o) {
      for (std::size_t i = 0; i < 18; ++i)
        point_contains(d.body->points[i], o->points[i]);
      for (std::size_t side = 0; side < 2; ++side) {
        const auto base = side == 0 ? std::size_t{4} : std::size_t{11};
        const auto& leg = d.legs[side];
        const std::array<const BoardingPlantedLegPointBounds*, 4> points{
            &leg.hip, &leg.knee, &leg.ankle, &leg.boot_center};
        for (std::size_t i = 0; i < points.size(); ++i) {
          Snapshot a, b;
          a.point(d.body->points[base + i]);
          b.point(*points[i]);
          check(a == b, "Body slots own the same closure bounds, never a "
                        "rounded reconstructed joint");
        }
      }

      for (std::size_t i = 0; i < 15; ++i)
        point_contains(d.body->mass_points[i], o->masses[i]);
      point_contains(d.body->center_of_mass, o->com);
      for (std::size_t i = 0; i < 18; ++i) {
        const auto& p = d.body->points[i];
        const auto low = static_cast<long double>(p.lower.y) +
                         static_cast<long double>(root_y);
        const auto high = static_cast<long double>(p.upper.y) +
                          static_cast<long double>(root_y);
        const auto expected = o->points[i].y + static_cast<long double>(root_y);
        const auto error = 8192 * std::numeric_limits<long double>::epsilon() *
                           std::max(1.L, std::abs(expected));
        check(low <= expected + error && high >= expected - error,
              "One exact common placement encloses every genuine world point "
              "without rounded endpoint authority");
      }
      const auto& P = d.body->points[0];
      check(P.lower.y == 0 && P.upper.y == 0 && bits(P.lower.z, root_z) &&
                bits(P.upper.z, root_z),
            "Canonical root stays unplaced; common Y is not applied twice");
    }
  } else
    check(!d.reservations_complete && !d.mass_model_complete,
          "Absent body cannot be inferred from successful sites or a partial "
          "leg");
}
auto observe(const OriginBoardingBootSupport& provider) -> void {
  const auto d = require(assess_origin_boarding_source_endpoint(provider));
  std::cout << std::setprecision(17)
            << "fixed endpoint body=" << d.body.has_value()
            << " arithmetic=" << d.arithmetic_supported
            << " plane=" << d.plane_identities << " links=" << d.link_identities
            << " limits=" << d.joint_limits_certified
            << " reservations=" << d.reservations_complete
            << " masses=" << d.mass_model_complete
            << " hips=" << d.hip_regions_certified
            << " complete=" << d.complete;
  if (d.first_refusal)
    std::cout << " refusal="
              << static_cast<unsigned>(d.first_refusal->condition)
              << " leg_condition="
              << static_cast<unsigned>(d.first_refusal->leg_condition);
  std::cout << '\n';
  for (std::size_t i = 0; i < 2; ++i) {
    const auto& e = d.legs[i];
    const auto& h = d.hips[i];
    std::cout << "fixed leg=" << i << " link=" << e.link_identities
              << " sector=" << e.limits_certified
              << " condition=" << static_cast<unsigned>(e.limiting_condition)
              << " hip_certificate=" << h.certified << " support=["
              << h.scaled_pelvis_extent.lower << ','
              << h.scaled_pelvis_extent.upper << "] gap=[" << h.scaled_gap.lower
              << ',' << h.scaled_gap.upper << "]\n";
  }
  inspect(d);
  check(d.complete && d.arithmetic_supported && d.plane_identities &&
            d.link_identities && d.joint_limits_certified &&
            d.reservations_complete && d.mass_model_complete &&
            d.hip_regions_certified && d.body && !d.first_refusal &&
            d.evaluated_legs == 2 && d.assembled_records == 1 &&
            d.examined_hip_regions == 2,
        "Observed unchanged fixed endpoint retains complete body and two hip "
        "certificates with exact work counts");
  check(d.sites.arithmetic_supported && d.sites.coverage_complete &&
            d.sites.eligible && !d.sites.first_refusal,
        "Observed fixed endpoint retains its complete unchanged source-site "
        "prerequisite");
  for (std::size_t side = 0; side < d.legs.size(); ++side) {
    const auto& leg = d.legs[side];
    const auto& hip = d.hips[side];
    check(leg.plane_identity && leg.link_identities && leg.limits_certified &&
              leg.limiting_condition == BoardingPlantedLegCondition::none &&
              hip.bindings_valid && hip.link_identity &&
              hip.structural_x_zero && hip.arithmetic_supported &&
              hip.certified && hip.scaled_gap.lower > 0,
          "Observed original finite hip certificate retains a strictly "
          "positive outward support gap");
  }
  check(
      site_snapshot(d.sites) ==
          site_snapshot(require(assess_origin_boarding_foot_sites(provider))),
      "Endpoint owns every unchanged public Sites02 field, source and refusal");
  if (d.body) {
    const auto old = require(assess_origin_boarding_planted_body(0, 0));
    check(binding_snapshot(d.parts) == binding_snapshot(old.parts),
          "New fixed endpoint reuses every old body binding bit without "
          "changing old shape/mass recipe");
  }
}
auto query(const OriginBoardingBootSupport& p, double y = .847, double z = -.55,
           std::size_t sitecap = 10, std::size_t bodycap = 1)
    -> BoardingSourceEndpointDiagnostic {
  auto d = require(
      detail::boarding_source_endpoint_bounded(p, y, z, sitecap, bodycap));
  inspect(d, y, z);
  return d;
}
auto root_controls(const OriginBoardingBootSupport& p) -> void {
  // Absolute, predeclared root positions only. No search for a succeeding
  // candidate, changed source/shape, alternative branch or enlarged region.
  for (const auto& yz : std::array{std::pair{8., -.55}, std::pair{-8., -.55},
                                   std::pair{.847, 8.}, std::pair{.847, -8.}}) {
    const auto d = query(p, yz.first, yz.second);
    check(!d.body && !d.complete && !d.reservations_complete &&
              !d.mass_model_complete && d.first_refusal,
          "Workspace endpoint violates independently bounded nominal "
          "reach/branch and cannot assemble a body");
  }
  for (double y : std::array{-.01, 0.}) {
    const auto d = query(p, y, -.55);
    check(!d.body && !d.complete && d.first_refusal &&
              d.first_refusal->condition ==
                  BoardingSourceEndpointCondition::leg_closure,
          "Hip at/below actual upper ankle cannot launder the directed dY<0 "
          "branch");
  }
  for (const auto& yz : std::array{
           std::pair{.747, -.55}, std::pair{.947, -.55}, std::pair{.847, -.45},
           std::pair{.847, -.65}, std::pair{.847, -.5}, std::pair{.847, -.8}}) {
    const auto d = query(p, yz.first, yz.second);
    if (d.complete)
      check(d.hips[0].certified && d.hips[1].certified,
            "Any completed private corroboration keeps both original finite "
            "regions");
  }
  for (double y : std::array{std::nextafter(0., -1.), std::nextafter(0., 1.)}) {
    const auto d = query(p, y, -.55);
    check(!d.complete && !d.body, "Adjacent subnormal root heights retain "
                                  "actual upward/singular leg refusal");
  }
  for (double z :
       std::array{std::nextafter(-.5, -1.), std::nextafter(-.5, 0.),
                  std::nextafter(-.8, -1.), std::nextafter(-.8, 0.)}) {
    const auto d = query(p, .847, z);
    if (d.body)
      check(d.link_identities && d.joint_limits_certified,
            "Adjacent sagittal displacement cannot bypass link/sector checks");
  }
  // A rounded root value cannot invent an exact three-term reach/plane edge.
  // These finite neighborhoods corroborate refusal/consistency only.
  const auto ankle_world_y = upper_plane + .1;
  for (double y :
       std::array{std::nextafter(ankle_world_y,
                                 -std::numeric_limits<double>::infinity()),
                  ankle_world_y,
                  std::nextafter(ankle_world_y,
                                 std::numeric_limits<double>::infinity())}) {
    const auto d = query(p, y, -.5);
    check(
        !d.complete && !d.body,
        "Near-zero reach/plane denominator cannot create a complete endpoint");
  }
}
auto capacities_invalid(const OriginBoardingBootSupport& p) -> void {
  for (std::size_t cap : std::array<std::size_t, 3>{0, 1, 9}) {
    const auto d = query(p, .847, -.55, cap, 1);
    check(!d.complete && !d.body && d.first_refusal &&
              d.first_refusal->condition ==
                  BoardingSourceEndpointCondition::sites_prerequisite &&
              !d.link_identities && !d.joint_limits_certified &&
              !d.hip_regions_certified && d.evaluated_legs == 0 &&
              d.examined_hip_regions == 0,
          "Incomplete owned site prefix refuses before new endpoint assembly");
    for (const auto& s : d.sites.sites)
      check(s.scanned_partitions == cap,
            "Source prefix never silently retries or resets work");
  }
  const auto no_body = query(p, .847, -.55, 10, 0);
  check(no_body.sites.eligible && !no_body.body && !no_body.complete &&
            no_body.first_refusal &&
            no_body.first_refusal->condition ==
                BoardingSourceEndpointCondition::body_capacity &&
            !no_body.link_identities && !no_body.joint_limits_certified &&
            !no_body.hip_regions_certified && no_body.evaluated_legs == 0 &&
            no_body.examined_hip_regions == 0,
        "Zero body capacity owns successful source evidence but grants no "
        "leg/body/hip certificate");
  check(!detail::boarding_source_endpoint_bounded(p, .847, -.55, 11, 1) &&
            !detail::boarding_source_endpoint_bounded(p, .847, -.55, 10, 2),
        "Cannot enlarge either fixed endpoint/source capacity");
  for (double value :
       std::array{std::numeric_limits<double>::quiet_NaN(),
                  std::numeric_limits<double>::infinity(),
                  -std::numeric_limits<double>::infinity(),
                  std::nextafter(8., 9.), std::nextafter(-8., -9.)}) {
    check(!detail::boarding_source_endpoint_bounded(p, value, -.55),
          "Nonfinite/excessive absolute root Y refuses at API boundary");
    check(!detail::boarding_source_endpoint_bounded(p, .847, value),
          "Nonfinite/excessive absolute root Z refuses at API boundary");
  }
}
struct SavedEnvironment {
  std::fenv_t saved{};
  SavedEnvironment() {
    if (std::fegetenv(&saved) != 0)
      throw std::runtime_error("Cannot save environment");
  }
  SavedEnvironment(const SavedEnvironment&) = delete;
  auto operator=(const SavedEnvironment&) -> SavedEnvironment& = delete;
  ~SavedEnvironment() {
    check(std::fesetenv(&saved) == 0, "Restore endpoint arithmetic state");
  }
};
auto denied_environment(const OriginBoardingBootSupport& p) -> void {
  const auto d = require(assess_origin_boarding_source_endpoint(p));
  check(!d.complete && !d.arithmetic_supported && !d.plane_identities &&
            !d.link_identities && !d.joint_limits_certified &&
            !d.reservations_complete && !d.mass_model_complete &&
            !d.hip_regions_certified && !d.body && d.first_refusal &&
            d.evaluated_legs == 0 && d.examined_hip_regions == 0,
        "Unsupported arithmetic retains no false endpoint identity/body/region "
        "authority");
  check(!d.sites.arithmetic_supported && !d.sites.eligible,
        "Real unsupported source prerequisite is retained without fabricated "
        "success");
  for (const auto& h : d.hips)
    check(!h.arithmetic_supported && !h.certified,
          "Unsupported state grants no finite hip certificate");
}
auto environment_controls(const OriginBoardingBootSupport& p) -> void {
  for (int mode : std::array{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    SavedEnvironment saved;
    check(std::fesetround(mode) == 0, "Set directed rounding adversary");
    denied_environment(p);
  }
#if defined(__SSE2__) && defined(__x86_64__)
  struct SavedControl {
    unsigned saved{_mm_getcsr()};
    ~SavedControl() { _mm_setcsr(saved); }
  };
  for (unsigned mode : std::array{1U << 15, 1U << 6, (1U << 15) | (1U << 6)}) {
    SavedEnvironment saved;
    SavedControl control;
    _mm_setcsr(control.saved | mode);
    denied_environment(p);
  }
  for (unsigned mode : std::array{1U, 2U, 3U}) {
    SavedEnvironment saved;
    SavedControl control;
    _mm_setcsr((control.saved & ~(3U << 13)) | (mode << 13));
    denied_environment(p);
  }
#endif
  check(std::fegetround() == FE_TONEAREST,
        "Restore nearest arithmetic before endpoint ownership checks");
}
auto ownership_controls(const OriginBoardingBootSupport& provider) -> void {
  const auto initial =
      require(assess_origin_boarding_source_endpoint(provider));
  const auto expected = snapshot(initial);
  auto owned = [] {
    auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    auto provider = require(make_origin_boarding_boot_support(binding));
    auto d = require(assess_origin_boarding_source_endpoint(provider));
    auto moved_binding = std::move(binding);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Empty-handle API contract.
    check(!make_origin_boarding_boot_support(binding),
          "Moved-from binding refuses source provider");
    check(moved_binding.contact() != nullptr,
          "Moved binding retains immutable contact selection");
    auto moved_provider = std::move(provider);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Empty-handle API contract.
    check(!assess_origin_boarding_source_endpoint(provider),
          "Moved-from provider grants no endpoint");
    check(moved_provider.selected_partitions().size() == 10,
          "Moved provider retains full source selection");
    return d;
  }();
  check(snapshot(owned) == expected, "Endpoint source names/closures/body/hip "
                                     "evidence survives caller lifetimes");
  auto copy = owned;
  auto moved = std::move(owned);
  check(snapshot(copy) == expected && snapshot(moved) == expected,
        "Copied/moved endpoint preserves every owned field");
  copy.common_translation_y_metres = 3.;
  copy.parts[0].mass.weight = 0;
  if (copy.body) copy.body->center_of_mass.lower.x = 9.;
  check(
      snapshot(moved) == expected,
      "Caller report mutation cannot change another immutable endpoint result");
  check(snapshot(require(assess_origin_boarding_source_endpoint(
            copy.sites.source))) == expected,
        "New endpoint is reconstructed from immutable provider, never caller "
        "supplied success/geometry");
}
} // namespace
int main() {
  try {
    static_assert(sizeof(BoardingSourceEndpointBody) <= 6144);
    static_assert(sizeof(BoardingSourceEndpointBody) +
                      sizeof(std::array<BoardingPlantedBodyPartBinding, 15>) <=
                  8192);
    if (std::numeric_limits<long double>::digits < 64)
      throw std::runtime_error(
          "Independent finite endpoint oracle requires64 mantissa bits");
    const auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    const auto provider = require(make_origin_boarding_boot_support(binding));
    // First assessment observes the registered immutable endpoint before any
    // private root/cap/environment control. No presumed success assertion.
    observe(provider);
    const auto old =
        snapshot(require(assess_origin_boarding_planted_body(0, 0)));
    root_controls(provider);
    capacities_invalid(provider);
    environment_controls(provider);
    ownership_controls(provider);
    check(snapshot(require(assess_origin_boarding_planted_body(0, 0))) == old,
          "All old body/closure/timing bits, counters, reports and refusals "
          "survive new endpoint assessments");
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout << checks << " source-bound endpoint checks, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
