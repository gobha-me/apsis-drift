#include "apsis_drift/origin_boarding_source_endpoint_self.hpp"
#include "origin_boarding_source_endpoint_self_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
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
auto check(bool value, std::string_view label) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T, class E> auto require(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Required endpoint-self API refused");
  return std::move(*value);
}
auto bits(double a, double b) -> bool {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
struct Wide {
  long double x{}, y{}, z{};
};
auto wide(RigidVector3 p) -> Wide {
  return {p.x, p.y, p.z};
}
auto add(Wide a, Wide b) -> Wide {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(Wide a, Wide b) -> Wide {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(Wide p, long double a) -> Wide {
  return {p.x * a, p.y * a, p.z * a};
}
auto dot(Wide a, Wide b) -> long double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
auto norm(Wide p) -> long double {
  return std::sqrt(dot(p, p));
}
auto component(Wide p, std::size_t i) -> long double {
  return i == 0 ? p.x : i == 1 ? p.y : p.z;
}
auto component(RigidVector3 p, std::size_t i) -> double {
  return i == 0 ? p.x : i == 1 ? p.y : p.z;
}
auto corroboration_error(long double x) -> long double {
  // Finite independent LD reference error only; never an admission tolerance.
  return 65536 * std::numeric_limits<long double>::epsilon() *
         std::max(1.L, std::abs(x));
}
auto contains(BoardingPlantedLegScalarBounds b, long double x) -> void {
  check(std::isfinite(b.lower) && std::isfinite(b.upper) &&
            b.lower <= b.upper &&
            static_cast<long double>(b.lower) <= x + corroboration_error(x) &&
            x - corroboration_error(x) <= static_cast<long double>(b.upper),
        "Independent complete expression belongs to outward bounds");
}
auto point_contains(const BoardingPlantedLegPointBounds& b, Wide p) -> void {
  for (std::size_t i = 0; i < 3; ++i)
    contains({component(b.lower, i), component(b.upper, i)}, component(p, i));
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

struct ExpectedPart {
  std::size_t first{}, second{};
  RigidVector3 half;
  double radius{};
  bool capsule{}, ellipsoid{};
};
const std::array<ExpectedPart, 15> expected_parts{
    {{0, 0, {.24, .12, .18}, 0, false, false},
     {1, 1, {.26, .2695, .18}, 0, false, true},
     {2, 2, {.16, .18, .18}, 0, false, true},
     {4, 5, {}, .105, true, false},
     {5, 6, {}, .075, true, false},
     {7, 7, {.06, .05, .14}, 0, false, false},
     {8, 9, {}, .065, true, false},
     {9, 10, {}, .055, true, false},
     {10, 10, {.04, .05, .02}, 0, false, false},
     {11, 12, {}, .105, true, false},
     {12, 13, {}, .075, true, false},
     {14, 14, {.06, .05, .14}, 0, false, false},
     {15, 16, {}, .065, true, false},
     {16, 17, {}, .055, true, false},
     {17, 17, {.04, .05, .02}, 0, false, false}}};
struct Oracle {
  std::array<Wide, 18> points{};
  std::array<Wide, 2> thigh{}, shin{};
  std::array<long double, 2> knee_cosine{};
};
auto oracle() -> Oracle {
  Oracle b;
  const Wide root{static_cast<long double>(.16), 0,
                  static_cast<long double>(-.55)};
  b.points[0] = root;
  b.points[1] = add(root, {0, static_cast<long double>(.3495), 0});
  b.points[2] = add(root, {0, static_cast<long double>(.70237), 0});
  b.points[3] = add(root, {0, static_cast<long double>(.65237), 0});
  const auto c = std::sqrt(.5L);
  for (std::size_t side = 0; side < 2; ++side) {
    const auto base = side == 0 ? std::size_t{4} : std::size_t{11};
    const auto x = static_cast<long double>(.16) +
                   (side == 0 ? -static_cast<long double>(.14)
                              : static_cast<long double>(.14));
    const auto plane = static_cast<long double>(
        side == 0 ? kCabinCorridorFloorMetres : -160000.0 * 1e-6);
    const Wide H{x, 0, static_cast<long double>(-.55)},
        A{x,
          plane + static_cast<long double>(.1) - static_cast<long double>(.847),
          static_cast<long double>(side == 0 ? -.5 : -.8)};
    const auto d = sub(A, H);
    // Widen actual stored binary64 constants, not ideal decimal values.
    const auto l1 = static_cast<long double>(.47285);
    const auto l2 = static_cast<long double>(.47478);
    const auto D = dot(d, d), alpha = (l1 * l1 - l2 * l2 + D) / (2 * D);
    const auto gamma = std::sqrt(l1 * l1 / D - alpha * alpha);
    const Wide q{0, -d.z, d.y};
    const auto K = add(H, add(scale(d, alpha), scale(q, gamma)));
    b.points[base] = H;
    b.points[base + 1] = K;
    b.points[base + 2] = A;
    b.points[base + 3] = {x,
                          plane + static_cast<long double>(.05) -
                              static_cast<long double>(.847),
                          A.z};
    b.thigh[side] = sub(K, H);
    b.shin[side] = sub(A, K);
    b.knee_cosine[side] = dot(b.thigh[side], b.shin[side]) / (l1 * l2);
    check(std::abs(norm(b.thigh[side]) - l1) < corroboration_error(l1) &&
              std::abs(norm(b.shin[side]) - l2) < corroboration_error(l2),
          "Independent unfactored knee preserves both nominal links");
    const auto S =
        add(root, {(side == 0 ? -1.L : 1.L) * static_cast<long double>(.20265),
                   static_cast<long double>(.579), 0});
    b.points[base + 4] = S;
    b.points[base + 5] = add(S, {0, -static_cast<long double>(.35898) * c,
                                 -static_cast<long double>(.35898) * c});
    b.points[base + 6] =
        add(b.points[base + 5], {0, static_cast<long double>(.386), 0});
  }
  return b;
}
auto solid_center(const Oracle& b, std::size_t id) -> Wide {
  const auto& p = expected_parts[id];
  return scale(add(b.points[p.first], b.points[p.second]), .5L);
}
auto support(const Oracle& b, std::size_t id, Wide n) -> long double {
  const auto& p = expected_parts[id];
  if (p.capsule)
    return std::max(dot(b.points[p.first], n), dot(b.points[p.second], n)) +
           static_cast<long double>(p.radius) * norm(n);
  const auto center = dot(b.points[p.first], n);
  if (p.ellipsoid) {
    const Wide hn{static_cast<long double>(p.half.x) * n.x,
                  static_cast<long double>(p.half.y) * n.y,
                  static_cast<long double>(p.half.z) * n.z};
    return center + norm(hn);
  }
  return center + static_cast<long double>(p.half.x) * std::abs(n.x) +
         static_cast<long double>(p.half.y) * std::abs(n.y) +
         static_cast<long double>(p.half.z) * std::abs(n.z);
}
auto maximizing_point(const Oracle& b, std::size_t id, Wide n) -> Wide {
  const auto& p = expected_parts[id];
  if (p.capsule) {
    const auto end = dot(b.points[p.first], n) >= dot(b.points[p.second], n)
                         ? b.points[p.first]
                         : b.points[p.second];
    return add(end, scale(n, static_cast<long double>(p.radius) / norm(n)));
  }
  if (p.ellipsoid) {
    const Wide hn{static_cast<long double>(p.half.x) * n.x,
                  static_cast<long double>(p.half.y) * n.y,
                  static_cast<long double>(p.half.z) * n.z};
    const auto divisor = norm(hn);
    return add(b.points[p.first],
               {static_cast<long double>(p.half.x) * hn.x / divisor,
                static_cast<long double>(p.half.y) * hn.y / divisor,
                static_cast<long double>(p.half.z) * hn.z / divisor});
  }
  return add(b.points[p.first], {n.x < 0 ? -static_cast<long double>(p.half.x)
                                         : static_cast<long double>(p.half.x),
                                 n.y < 0 ? -static_cast<long double>(p.half.y)
                                         : static_cast<long double>(p.half.y),
                                 n.z < 0 ? -static_cast<long double>(p.half.z)
                                         : static_cast<long double>(p.half.z)});
}
struct ExpectedRegion {
  std::size_t first{}, second{};
  BoardingSelfJunction junction{};
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
auto connected_index(std::size_t first, std::size_t second)
    -> std::optional<std::size_t> {
  for (std::size_t i = 0; i < expected_regions.size(); ++i)
    if (expected_regions[i].first == first &&
        expected_regions[i].second == second)
      return i;
  return {};
}
auto proposal_matches(const Oracle& b, std::size_t first, std::size_t second,
                      RigidVector3 reported, std::size_t examined) -> bool {
  // Higher-precision reference to the frozen fan, not another direction search.
  std::array<Wide, 14> fan{};
  std::size_t count{};
  const auto push = [&](Wide n) {
    if (count < fan.size()) fan[count++] = n;
  };
  push({1, 0, 0});
  push({0, 1, 0});
  push({0, 0, 1});
  push(sub(solid_center(b, second), solid_center(b, first)));
  for (std::size_t id : std::array{first, second})
    if (!expected_parts[id].capsule) {
      push({1, 0, 0});
      push({0, 1, 0});
      push({0, 0, 1});
    }
  for (std::size_t id : std::array{first, second})
    if (expected_parts[id].capsule) {
      const auto other = solid_center(b, id == first ? second : first);
      push(sub(b.points[expected_parts[id].first], other));
      push(sub(b.points[expected_parts[id].second], other));
    }
  if (examined == 0 || examined > count) return false;
  const auto n = wide(reported), last = fan[examined - 1];
  for (long double direction : std::array{1.L, -1.L}) {
    const auto diff = sub(n, scale(last, direction));
    if (std::abs(diff.x) <= corroboration_error(last.x) &&
        std::abs(diff.y) <= corroboration_error(last.y) &&
        std::abs(diff.z) <= corroboration_error(last.z))
      return true;
  }
  return false;
}
// This corroborates the WHOLE original finite ownership methods, never samples
// or a midpoint approximation of a production solid.
auto whole_owned(const Oracle& b, std::size_t index) -> bool {
  const auto& r = expected_regions[index];
  const auto side = r.second >= 9 ? std::size_t{1} : std::size_t{0};
  if (r.junction == BoardingSelfJunction::waist)
    return static_cast<long double>(.12) <= kBoardingSelfWaistLimitMetres;
  if (r.junction == BoardingSelfJunction::neck) {
    // Exact identity of SAME trunk halfY, not tolerance-repaired cancellation.
    return bits(expected_parts[1].half.y, .2695);
  }
  if (r.junction == BoardingSelfJunction::hip) {
    const auto v = b.thigh[side];
    return static_cast<long double>(.12) * std::abs(v.y) +
               static_cast<long double>(.18) * std::abs(v.z) <=
           static_cast<long double>(kBoardingSelfHipLengthMetres) *
               static_cast<long double>(.47285);
  }
  if (r.junction == BoardingSelfJunction::ankle) {
    const auto v = scale(b.shin[side], -1);
    const auto extent = dot(v, {0, -static_cast<long double>(.05), 0}) +
                        static_cast<long double>(.05) * std::abs(v.y) +
                        static_cast<long double>(.14) * std::abs(v.z);
    return v.y > 0 &&
           static_cast<long double>(.075) <= kBoardingSelfAnkleLengthMetres &&
           kBoardingSelfAnkleLengthMetres < .47478 &&
           extent <= static_cast<long double>(kBoardingSelfAnkleLengthMetres) *
                         static_cast<long double>(.47478);
  }
  if (r.junction == BoardingSelfJunction::wrist) {
    return .055 <= kBoardingSelfWristLengthMetres &&
           kBoardingSelfWristLengthMetres < .386 &&
           .05 <= kBoardingSelfWristLengthMetres;
  }
  if (r.junction == BoardingSelfJunction::knee ||
      r.junction == BoardingSelfJunction::elbow) {
    const auto outgoing = r.junction == BoardingSelfJunction::knee
                              ? -b.knee_cosine[side]
                              : std::sqrt(.5L);
    const auto R =
        static_cast<long double>(r.junction == BoardingSelfJunction::knee
                                     ? kBoardingSelfKneeRadiusMetres
                                     : kBoardingSelfElbowRadiusMetres);
    const auto radius = static_cast<long double>(
        r.junction == BoardingSelfJunction::knee ? .105 : .065);
    return (1 - outgoing) / 2 > 0 &&
           radius * radius <= R * R * (1 - outgoing) / 2;
  }
  if (r.junction == BoardingSelfJunction::shoulder) {
    const auto c = std::sqrt(.5L),
               R = static_cast<long double>(kBoardingSelfShoulderRadiusMetres),
               radius = static_cast<long double>(.065);
    const auto S = b.points[expected_parts[r.second].first],
               E = b.points[expected_parts[r.second].second];
    const auto cylinder =
        S.z - c * std::sqrt(R * R - radius * radius) + radius * c;
    const auto distal = E.z + radius;
    const auto trunk_minimum = b.points[1].z - static_cast<long double>(.18);
    return R >= radius && cylinder < trunk_minimum && distal < trunk_minimum;
  }
  return false;
}

auto authority(const BoardingSourceEndpointSelfDiagnostic& d) -> void {
  check(
      !d.arbitrary_pose_qualified && !d.continuous_qualified &&
          !d.force_qualified && !d.load_qualified && !d.world_qualified &&
          !d.crop_qualified && !d.sweep_qualified && !d.dynamics_qualified &&
          !d.acquisition_qualified && !d.transfer_qualified &&
          !d.free_foot_swing_qualified && !d.route_qualified &&
          !d.actor_qualified && !d.seat_qualified && !d.save_qualified &&
          !d.first_flight_qualified,
      "Static self never grants load, world, motion, actor or save authority");
  check(!d.endpoint.self_qualified && !d.endpoint.timing_qualified &&
            !d.endpoint.world_qualified && !d.endpoint.load_qualified,
        "Original owned endpoint retains its exact authority boundary");
}
auto pair_equal(const BoardingSourceEndpointSelfPair& a,
                const BoardingSourceEndpointSelfPair& b) -> bool {
  const auto scalar = [](BoardingPlantedLegScalarBounds x,
                         BoardingPlantedLegScalarBounds y) {
    return bits(x.lower, y.lower) && bits(x.upper, y.upper);
  };
  return a.first == b.first && a.second == b.second &&
         a.connected_region_index == b.connected_region_index &&
         a.certificate == b.certificate &&
         scalar(a.certificate_gap, b.certificate_gap) &&
         scalar(a.ownership_extent, b.ownership_extent) &&
         scalar(a.ownership_limit, b.ownership_limit) &&
         scalar(a.ownership_secondary_extent, b.ownership_secondary_extent) &&
         bits(a.separating_direction.x, b.separating_direction.x) &&
         bits(a.separating_direction.y, b.separating_direction.y) &&
         bits(a.separating_direction.z, b.separating_direction.z) &&
         a.examined_axes == b.examined_axes &&
         a.signed_support_trials == b.signed_support_trials &&
         a.examined == b.examined &&
         a.arithmetic_supported == b.arithmetic_supported &&
         a.certified == b.certified && a.whole_owned == b.whole_owned &&
         a.structural_identity == b.structural_identity;
}
auto binding_and_regions(const BoardingSourceEndpointSelfDiagnostic& d)
    -> void {
  using Shape = BoardingSourceEndpointSelfShape;
  check(d.self_version == 1 && d.parts.size() == 15 && d.regions.size() == 14 &&
            d.pairs.size() == 105,
        "Versioned fixed15/14/105 output roster");
  if (!d.bindings_complete) return;
  std::array<std::size_t, 3> counts{};
  for (std::size_t id = 0; id < 15; ++id) {
    const auto& p = d.parts[id];
    const auto& e = expected_parts[id];
    const auto shape = e.capsule     ? Shape::capsule
                       : e.ellipsoid ? Shape::ellipsoid
                                     : Shape::box;
    check(static_cast<std::size_t>(p.id) == id && p.binding.id == p.id &&
              p.shape == shape,
          "Self uses exact original part IDs and five boxes/two "
          "ellipsoids/eight capsules");
    ++counts[static_cast<std::size_t>(p.shape)];
    check(p.binding.mass.first == d.endpoint.parts[id].mass.first &&
              p.binding.mass.second == d.endpoint.parts[id].mass.second &&
              p.binding.mass.weight == d.endpoint.parts[id].mass.weight,
          "Self classification never mutates any original mass identity");
    if (e.capsule) {
      const auto* c = std::get_if<BoardingPlantedBodyCapsuleBinding>(
          &p.binding.reservation);
      check(c && static_cast<std::size_t>(c->start) == e.first &&
                static_cast<std::size_t>(c->end) == e.second &&
                bits(c->radius_metres, e.radius),
            "Each original capsule retains exact endpoints/radius binding");
    } else {
      const auto* b =
          std::get_if<BoardingPlantedBodyBoxBinding>(&p.binding.reservation);
      check(b && static_cast<std::size_t>(b->center) == e.first &&
                bits(b->half_size_metres.x, e.half.x) &&
                bits(b->half_size_metres.y, e.half.y) &&
                bits(b->half_size_metres.z, e.half.z),
            "Each box/ellipsoid uses original center and dimension binding");
      if (b)
        for (std::size_t axis = 0; axis < 3; ++axis)
          for (std::size_t row = 0; row < 3; ++row)
            check(
                component(b->frame.columns[axis], row) ==
                    (axis == row ? 1. : 0.),
                "Only the owned canonical identity frame enters self geometry");
    }
  }
  check(counts[static_cast<std::size_t>(Shape::box)] == 5 &&
            counts[static_cast<std::size_t>(Shape::ellipsoid)] == 2 &&
            counts[static_cast<std::size_t>(Shape::capsule)] == 8,
        "Exact five/two/eight self primitive totals");
  constexpr std::array<std::size_t, 14> roots{0, 4, 11, 1,  8,  15, 5,
                                              6, 9, 10, 12, 13, 16, 17};
  constexpr std::array<std::size_t, 14> toward{1, 5, 12, 2,  9,  16, 4,
                                               5, 8, 9,  11, 12, 15, 16};
  for (std::size_t i = 0; i < 14; ++i) {
    const auto& r = d.regions[i];
    const auto& e = expected_regions[i];
    check(static_cast<std::size_t>(r.first) == e.first &&
              static_cast<std::size_t>(r.second) == e.second &&
              r.junction == e.junction,
          "All14 original connected pairs retain lexicographic named bindings");
    check(static_cast<std::size_t>(r.root) == roots[i] &&
              static_cast<std::size_t>(r.toward) == toward[i],
          "All finite cap/slab/sphere records retain genuine shared roots and "
          "original toward endpoints");
    check(std::isfinite(r.limit_metres) && r.limit_metres > 0,
          "Each ownership region is genuinely finite with positive original "
          "extent");
    double limit{};
    switch (e.junction) {
      case BoardingSelfJunction::waist:
        limit = kBoardingSelfWaistLimitMetres;
        break;
      case BoardingSelfJunction::neck: limit = .2695; break;
      case BoardingSelfJunction::hip:
        limit = kBoardingSelfHipLengthMetres;
        break;
      case BoardingSelfJunction::knee:
        limit = kBoardingSelfKneeRadiusMetres;
        break;
      case BoardingSelfJunction::ankle:
        limit = kBoardingSelfAnkleLengthMetres;
        break;
      case BoardingSelfJunction::shoulder:
        limit = kBoardingSelfShoulderRadiusMetres;
        break;
      case BoardingSelfJunction::elbow:
        limit = kBoardingSelfElbowRadiusMetres;
        break;
      case BoardingSelfJunction::wrist:
        limit = kBoardingSelfWristLengthMetres;
        break;
    }
    check(bits(r.limit_metres, limit),
          "Original finite region constants, including actual trunk halfY neck "
          "limit");
  }
  if (d.endpoint.body) {
    const auto b = oracle();
    for (std::size_t i = 0; i < 18; ++i)
      point_contains(d.endpoint.body->points[i], b.points[i]);
  }
}
auto ownership_evidence(const BoardingSourceEndpointSelfDiagnostic& d,
                        const BoardingSourceEndpointSelfPair& p,
                        const Oracle& b) -> void {
  check(p.connected_region_index.has_value() && p.whole_owned,
        "Named whole ownership always has a declared connected region");
  if (!p.connected_region_index || *p.connected_region_index >= 14) return;
  const auto index = *p.connected_region_index;
  const auto& r = expected_regions[index];
  const auto side = r.second >= 9 ? std::size_t{1} : std::size_t{0};
  check(whole_owned(b, index),
        "Independent complete finite region bound corroborates ownership");
  long double extent{}, limit{}, secondary{};
  switch (r.junction) {
    case BoardingSelfJunction::waist:
      extent = static_cast<long double>(.12);
      limit = kBoardingSelfWaistLimitMetres;
      break;
    case BoardingSelfJunction::neck:
      extent = static_cast<long double>(.2695);
      limit = extent;
      check(p.structural_identity && bits(p.ownership_extent.lower, .2695) &&
                bits(p.ownership_extent.upper, .2695) &&
                bits(p.ownership_limit.lower, .2695) &&
                bits(p.ownership_limit.upper, .2695) &&
                p.certificate_gap.lower == 0 && p.certificate_gap.upper == 0,
            "Neck uses authentic SAME-expression exact equality, not rounded "
            "zero repair");
      break;
    case BoardingSelfJunction::hip:
      extent = static_cast<long double>(.12) * std::abs(b.thigh[side].y) +
               static_cast<long double>(.18) * std::abs(b.thigh[side].z);
      limit = static_cast<long double>(kBoardingSelfHipLengthMetres) *
              static_cast<long double>(.47285);
      break;
    case BoardingSelfJunction::ankle:
      extent = static_cast<long double>(.14) * std::abs(b.shin[side].z);
      limit = static_cast<long double>(kBoardingSelfAnkleLengthMetres) *
              static_cast<long double>(.47478);
      check(b.shin[side].y < 0, "Whole boot support cancels equal .05 terms "
                                "only after authentic positive shin cosine");
      break;
    case BoardingSelfJunction::wrist:
      extent = static_cast<long double>(.05);
      limit = kBoardingSelfWristLengthMetres;
      break;
    case BoardingSelfJunction::knee:
    case BoardingSelfJunction::elbow: {
      const bool knee = r.junction == BoardingSelfJunction::knee;
      const auto outgoing = knee ? -b.knee_cosine[side] : std::sqrt(.5L);
      const auto radius = static_cast<long double>(knee ? .105 : .065);
      const auto R =
          static_cast<long double>(knee ? kBoardingSelfKneeRadiusMetres
                                        : kBoardingSelfElbowRadiusMetres);
      extent = radius * radius;
      limit = R * R * (1 - outgoing) / 2;
      if (knee) {
        contains(d.endpoint.legs[side].knee_cosine, b.knee_cosine[side]);
        check(std::abs(outgoing + b.knee_cosine[side]) == 0,
              "K-to-H/K-to-A rays use NEGATIVE owned knee cosine");
      }
      break;
    }
    case BoardingSelfJunction::shoulder: {
      const auto R = static_cast<long double>(
                     kBoardingSelfShoulderRadiusMetres),
                 radius = static_cast<long double>(.065), c = std::sqrt(.5L);
      // Coordinates relative to canonical root before the common placement.
      extent = -c * std::sqrt(R * R - radius * radius) + radius * c;
      secondary = -static_cast<long double>(.35898) * c + radius;
      limit = -static_cast<long double>(.18);
      contains(p.ownership_secondary_extent, secondary);
      check(p.ownership_extent.upper < p.ownership_limit.lower &&
                p.ownership_secondary_extent.upper < p.ownership_limit.lower,
            "Shoulder requires BOTH full outside-sphere cylinder and distal "
            "ball below trunk");
      check(R >= radius, "Original shoulder sphere contains entire root ball");
      break;
    }
  }
  contains(p.ownership_extent, extent);
  contains(p.ownership_limit, limit);
  const auto gap = limit - extent;
  contains(p.certificate_gap, gap);
  check(p.certificate_gap.lower >= 0,
        "Whole-owned certificate carries nonnegative outward minimum gap");
}
auto diagnostic_checks(const BoardingSourceEndpointSelfDiagnostic& d) -> void {
  authority(d);
  binding_and_regions(d);
  check(d.examined_pairs <= 105 && d.certified_pairs <= d.examined_pairs &&
            d.examined_axes <= 14 * d.examined_pairs &&
            d.signed_support_trials <= 2 * d.examined_axes,
        "Finite complete-profile counters cannot "
        "exceed105pairs/14proposals/28signedtrials");
  std::size_t examined{}, certified{}, axes{}, trials{}, index{};
  bool unentered{};
  const auto b = oracle();
  for (std::size_t first = 0; first < 15; ++first)
    for (std::size_t second = first + 1; second < 15; ++second, ++index) {
      const auto& p = d.pairs[index];
      if (d.bindings_complete)
        check(static_cast<std::size_t>(p.first) == first &&
                  static_cast<std::size_t>(p.second) == second &&
                  p.connected_region_index == connected_index(first, second),
              "Every one of105 canonical pairs retains exact ID/region "
              "accounting");
      if (!p.examined) {
        unentered = true;
        check(!p.certified && !p.whole_owned && p.examined_axes == 0 &&
                  p.signed_support_trials == 0,
              "Unentered pair cannot forge arithmetic work or a certificate");
        continue;
      }
      check(!unentered, "Entered pair ledger is one complete ordered prefix");
      ++examined;
      axes += p.examined_axes;
      trials += p.signed_support_trials;
      check(p.examined_axes <= 14 &&
                p.signed_support_trials <= 2 * p.examined_axes,
            "Per-pair finite proposal and signed-trial caps");
      if (p.certified) {
        ++certified;
        check(p.arithmetic_supported &&
                  p.certificate !=
                      BoardingSourceEndpointSelfCertificate::none &&
                  p.certificate_gap.lower >= 0,
              "Every positive has supported nonnegative genuine certificate "
              "evidence");
        if (p.whole_owned)
          ownership_evidence(d, p, b);
        else {
          check(p.certificate ==
                    BoardingSourceEndpointSelfCertificate::convex_support_plane,
                "Absence uses full convex support, never adjacency exemption");
          const auto n = wide(p.separating_direction);
          check(norm(n) > 0 && std::isfinite(norm(n)) && std::abs(n.x) <= 64 &&
                    std::abs(n.y) <= 64 && std::abs(n.z) <= 64,
                "Reported separation proposal is finite nonzero within fixed "
                "workspace");
          check(proposal_matches(b, first, second, p.separating_direction,
                                 p.examined_axes),
                "Separating direction belongs to the last examined frozen "
                "finite proposal, not an adaptive search");
          const auto gap =
              -support(b, second, scale(n, -1)) - support(b, first, n);
          contains(p.certificate_gap, gap);
          check(gap >= -corroboration_error(gap),
                "Independent full primitive extrema corroborate every "
                "separating plane");
        }
      } else
        check(!p.whole_owned &&
                  p.certificate == BoardingSourceEndpointSelfCertificate::none,
              "Unresolved pair never invents collision or ownership authority");
    }
  check(examined == d.examined_pairs && certified == d.certified_pairs &&
            axes == d.examined_axes && trials == d.signed_support_trials,
        "All prefix/work counters agree with complete fixed-size pair ledger");
  check(d.complete ==
            (d.endpoint.complete && d.bindings_complete &&
             d.arithmetic_supported && d.coverage_complete && certified == 105),
        "Only all105 genuine decisions with complete prerequisites permit "
        "completion");
  check(d.self_qualified == d.complete &&
            d.first_refusal.has_value() != d.complete,
        "Static self qualification and first refusal remain honest");
  if (d.first_refusal && d.first_refusal->pair_index)
    check(*d.first_refusal->pair_index < 105,
          "Owned first-refusal index belongs to finite graph");
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
auto observe(const OriginBoardingBootSupport& provider)
    -> BoardingSourceEndpointSelfDiagnostic {
  auto d = require(assess_origin_boarding_source_endpoint_self(provider));
  // This is the FIRST self observation, before local numeric or reduced-budget
  // calls. Frozen inputs stay unchanged whether complete or honestly refused.
  std::cout << std::setprecision(17)
            << "Frozen endpoint self: endpoint=" << d.endpoint.complete
            << " bindings=" << d.bindings_complete
            << " arithmetic=" << d.arithmetic_supported
            << " coverage=" << d.coverage_complete
            << " examined=" << d.examined_pairs
            << " certified=" << d.certified_pairs << " axes=" << d.examined_axes
            << " signed_trials=" << d.signed_support_trials
            << " self=" << d.self_qualified << " complete=" << d.complete;
  if (d.first_refusal) {
    std::cout << " refusal="
              << static_cast<unsigned>(d.first_refusal->condition);
    if (d.first_refusal->pair_index)
      std::cout << " pair=" << *d.first_refusal->pair_index;
  }
  std::cout << '\n';
  for (std::size_t i = 0; i < d.pairs.size(); ++i) {
    const auto& p = d.pairs[i];
    if (p.examined && (p.connected_region_index || !p.certified))
      std::cout << " pair=" << i << " ids=" << static_cast<unsigned>(p.first)
                << ',' << static_cast<unsigned>(p.second)
                << " certificate=" << static_cast<unsigned>(p.certificate)
                << " certified=" << p.certified << " whole=" << p.whole_owned
                << " gap=[" << p.certificate_gap.lower << ','
                << p.certificate_gap.upper << "] extent=["
                << p.ownership_extent.lower << ',' << p.ownership_extent.upper
                << "] limit=[" << p.ownership_limit.lower << ','
                << p.ownership_limit.upper << "] secondary=["
                << p.ownership_secondary_extent.lower << ','
                << p.ownership_secondary_extent.upper << "]\n";
  }
  diagnostic_checks(d);
  const auto old = require(assess_origin_boarding_source_endpoint(provider));
  check(snapshot(d.endpoint) == snapshot(old),
        "Owned endpoint retains every old field/bit/source row without new "
        "geometry conversion");
  return d;
}

auto primitive_controls(const OriginBoardingBootSupport& provider) -> void {
  const auto b = oracle();
  const std::array<RigidVector3, 13> directions{{{1, 0, 0},
                                                 {-1, 0, 0},
                                                 {0, 1, 0},
                                                 {0, -1, 0},
                                                 {0, 0, 1},
                                                 {0, 0, -1},
                                                 {1, 2, 3},
                                                 {-3, .125, 2},
                                                 {64, -64, 64},
                                                 {1, 0x1p-40, -1},
                                                 {0x1p-40, 1, 0},
                                                 {.125, -.25, .5},
                                                 {-1, -1, -1}}};
  for (std::size_t id = 0; id < 15; ++id)
    for (const auto& n : directions) {
      const auto e = require(detail::boarding_source_endpoint_self_support(
          provider, static_cast<BoardingBodyPartId>(id), n));
      check(e.arithmetic_supported, "Finite registered source part/direction "
                                    "has supported primitive arithmetic");
      if (!e.arithmetic_supported) continue;
      const auto expected = support(b, id, wide(n));
      contains(e.support, expected);
      const auto extremum = maximizing_point(b, id, wide(n));
      check(std::abs(dot(extremum, wide(n)) - expected) <=
                corroboration_error(expected),
            "Independent convex maximizing point attains complete support, not "
            "a sample bound");
      const auto& p = expected_parts[id];
      if (p.capsule) {
        const auto displacement = sub(b.points[p.second], b.points[p.first]);
        const auto t =
            std::clamp(dot(sub(extremum, b.points[p.first]), displacement) /
                           dot(displacement, displacement),
                       0.L, 1.L);
        const auto distance =
            norm(sub(extremum, add(b.points[p.first], scale(displacement, t))));
        check(std::abs(distance - static_cast<long double>(p.radius)) <=
                  corroboration_error(distance),
              "Capsule support extremum lies on actual endpoint/cylinder "
              "boundary");
      } else if (p.ellipsoid) {
        const auto d = sub(extremum, b.points[p.first]);
        const auto value =
            d.x * d.x / (static_cast<long double>(p.half.x) * p.half.x) +
            d.y * d.y / (static_cast<long double>(p.half.y) * p.half.y) +
            d.z * d.z / (static_cast<long double>(p.half.z) * p.half.z);
        check(std::abs(value - 1) <= corroboration_error(value),
              "Ellipsoid support uses complete unequal semi-axes, not "
              "circumscribed midpoint box");
      } else {
        const auto d = sub(extremum, b.points[p.first]);
        for (std::size_t i = 0; i < 3; ++i)
          check(std::abs(component(d, i)) <=
                    static_cast<long double>(component(p.half, i)) +
                        corroboration_error(component(d, i)),
                "Box maximizing corner belongs to actual identity-box closure");
      }
    }
  // Full signed support has translation dependence n.C, not an absolute-value
  // bounding sphere about the canonical origin.
  for (std::size_t id = 0; id < 15; ++id) {
    const auto plus = require(detail::boarding_source_endpoint_self_support(
        provider, static_cast<BoardingBodyPartId>(id), {1, 0, 0}));
    const auto minus = require(detail::boarding_source_endpoint_self_support(
        provider, static_cast<BoardingBodyPartId>(id), {-1, 0, 0}));
    const auto C = solid_center(b, id);
    check(
        std::abs((support(b, id, {1, 0, 0}) - support(b, id, {-1, 0, 0})) / 2 -
                 C.x) <= corroboration_error(C.x),
        "Independent signed supports recover true canonical center X");
    if (plus.arithmetic_supported && minus.arithmetic_supported)
      check(static_cast<long double>(plus.support.lower) -
                        minus.support.upper <=
                    2 * C.x + corroboration_error(C.x) &&
                2 * C.x - corroboration_error(C.x) <=
                    static_cast<long double>(plus.support.upper) -
                        minus.support.lower,
            "Both stored support signs retain actual canonical translation");
  }
  for (const auto& n : std::array<RigidVector3, 4>{
           {{0, 0, 0},
            {std::nextafter(64., std::numeric_limits<double>::infinity()), 0,
             0},
            {0, std::numeric_limits<double>::infinity(), 0},
            {0, 0, std::numeric_limits<double>::quiet_NaN()}}})
    check(!detail::boarding_source_endpoint_self_support(
              provider, BoardingBodyPartId::pelvis, n),
          "Zero/nonfinite/out-of-workspace proposal is an API refusal");
  check(!detail::boarding_source_endpoint_self_support(
            provider, static_cast<BoardingBodyPartId>(15), {1, 0, 0}),
        "No caller-added part can enter fixed support roster");
  const auto tiny = require(detail::boarding_source_endpoint_self_support(
      provider, BoardingBodyPartId::pelvis,
      {std::numeric_limits<double>::denorm_min(), 0, 0}));
  if (tiny.arithmetic_supported)
    contains(tiny.support,
             support(b, 0,
                     {static_cast<long double>(
                          std::numeric_limits<double>::denorm_min()),
                      0, 0}));
}
auto half_ray_controls() -> void {
  struct Case {
    double first{}, second{}, region{};
    BoardingPlantedLegScalarBounds cosine;
  };
  const std::array<Case, 8> cases{
      {{1, 1, 2, {-1, -1}},
       {1, 1, 1, {-1, -1}},
       {.105, .075, kBoardingSelfKneeRadiusMetres, {-.9, -.8}},
       {.065, .055, kBoardingSelfElbowRadiusMetres, {.7, .71}},
       {1, 1, 1, {0, 0}},
       {1, 1, 8, {1, 1}},
       {.125, .25, 1, {-.5, .5}},
       {8, 8, 8, {-1, -1}}}};
  for (const auto& c : cases) {
    const auto e = require(detail::boarding_source_endpoint_self_half_ray(
        c.first, c.second, c.region, c.cosine));
    const auto slo = (1 - static_cast<long double>(c.cosine.upper)) / 2;
    const auto shi = (1 - static_cast<long double>(c.cosine.lower)) / 2;
    const auto radius = static_cast<long double>(std::max(c.first, c.second));
    const auto R = static_cast<long double>(c.region);
    if (e.arithmetic_supported) {
      contains(e.sine_squared, slo);
      contains(e.sine_squared, shi);
      contains(e.squared_gap, R * R * slo - radius * radius);
      contains(e.squared_gap, R * R * shi - radius * radius);
    }
    if (e.certified)
      check(e.arithmetic_supported && e.sine_squared.lower > 0 &&
                e.squared_gap.lower >= 0 && slo > 0 &&
                R * R * slo >= radius * radius,
            "Half-ray certificate requires entire cosine interval and original "
            "maximum radius");
    if (R * R * slo < radius * radius || slo <= 0)
      check(!e.certified,
            "Known deficit/parallel rays never become finite sphere ownership");
  }
  const auto positive = require(
      detail::boarding_source_endpoint_self_half_ray(1, 1, 2, {-1, -1}));
  check(positive.arithmetic_supported && positive.certified &&
            positive.squared_gap.lower > 0,
        "Fixed generous opposite-ray scalar control exercises genuine positive "
        "inequality");
  const auto equality = require(
      detail::boarding_source_endpoint_self_half_ray(1, 1, 1, {-1, -1}));
  check(equality.arithmetic_supported && equality.certified &&
            equality.squared_gap.lower == 0 && equality.squared_gap.upper == 0,
        "Exact finite opposite-ray equality is retained without arbitrary "
        "epsilon");
  for (double r : std::array{std::nextafter(1., 0.), std::nextafter(1., 2.)}) {
    const auto e = require(
        detail::boarding_source_endpoint_self_half_ray(r, r, 1, {-1, -1}));
    if (r > 1)
      check(!e.certified,
            "Adjacent radius above exact ownership boundary refuses");
    if (e.certified)
      check(r <= 1 && e.squared_gap.lower >= 0,
            "Adjacent radius certificate respects exact bound");
  }
  const auto near = require(detail::boarding_source_endpoint_self_half_ray(
      .01, .01, 1, {std::nextafter(1., 0.), std::nextafter(1., 0.)}));
  check(!near.certified, "Nearparallel finite rays cannot hide radius outside "
                         "tiny half-angle sphere");
  for (double invalid : std::array{0., -1., std::nextafter(8., 9.),
                                   std::numeric_limits<double>::infinity(),
                                   std::numeric_limits<double>::quiet_NaN()}) {
    check(
        !detail::boarding_source_endpoint_self_half_ray(invalid, 1, 1, {0, 0}),
        "Malformed first radius denies before numerical evidence");
    check(
        !detail::boarding_source_endpoint_self_half_ray(1, invalid, 1, {0, 0}),
        "Malformed second radius denies before numerical evidence");
    check(
        !detail::boarding_source_endpoint_self_half_ray(1, 1, invalid, {0, 0}),
        "Malformed original sphere denies before numerical evidence");
  }
  for (const auto& c : std::array<BoardingPlantedLegScalarBounds, 5>{
           {{.1, -.1},
            {std::nextafter(-1., -2.), 0},
            {0, std::nextafter(1., 2.)},
            {0, std::numeric_limits<double>::infinity()},
            {std::numeric_limits<double>::quiet_NaN(), 0}}})
    check(!detail::boarding_source_endpoint_self_half_ray(1, 1, 1, c),
          "Nonfinite/unordered/out-of-unit cosine bounds cannot forge outgoing "
          "geometry");
}
auto capacity_controls(const OriginBoardingBootSupport& provider,
                       const BoardingSourceEndpointSelfDiagnostic& full)
    -> void {
  const auto old = snapshot(full.endpoint);
  for (std::size_t pairs : std::array<std::size_t, 6>{0, 1, 2, 14, 104, 105}) {
    const auto d = require(detail::boarding_source_endpoint_self_bounded(
        provider, 10, 1, pairs, 14));
    diagnostic_checks(d);
    check(snapshot(d.endpoint) == old,
          "Pair budgets never reset, modify or rebuild child endpoint geometry "
          "differently");
    check(d.examined_pairs == pairs,
          "Lowered pair budget retains exact lexicographic count, including "
          "unresolved work");
    if (pairs < 105) {
      check(
          !d.complete && !d.self_qualified && d.first_refusal.has_value() &&
              !d.coverage_complete,
          "Incomplete pair prefix retains refusal and no static qualification");
      check(d.first_refusal && d.first_refusal->pair_index == pairs &&
                d.first_refusal->condition ==
                    BoardingSourceEndpointSelfCondition::pair_capacity,
            "Observed complete graph loses coverage first at exact pair-budget "
            "boundary");
    }
    for (std::size_t i = 0; i < pairs; ++i)
      check(pair_equal(d.pairs[i], full.pairs[i]),
            "Every retained lower-budget pair is bitwise same as full-profile "
            "prefix");
  }
  for (std::size_t axes : std::array<std::size_t, 4>{0, 1, 3, 13}) {
    const auto d = require(detail::boarding_source_endpoint_self_bounded(
        provider, 10, 1, 105, axes));
    diagnostic_checks(d);
    check(snapshot(d.endpoint) == old && d.examined_pairs == 105,
          "Axis budgets retain owned endpoint and all105 pair causality");
    for (std::size_t i = 0; i < 105; ++i) {
      const auto& p = d.pairs[i];
      check(p.examined_axes <= axes,
            "No additional axis is invented after reduced finite budget");
      if (full.pairs[i].whole_owned)
        check(p.certified && p.whole_owned && p.examined_axes == 0 &&
                  p.signed_support_trials == 0,
              "Named whole finite ownership remains independent of "
              "separating-plane budget");
    }
    const auto first_failure = std::ranges::find_if(d.pairs, [](const auto& p) {
      return !p.arithmetic_supported || !p.certified;
    });
    if (first_failure != d.pairs.end()) {
      const auto index =
          static_cast<std::size_t>(first_failure - d.pairs.begin());
      const auto capsule = [&](BoardingBodyPartId part) {
        return d.parts[static_cast<std::size_t>(part)].shape ==
               BoardingSourceEndpointSelfShape::capsule;
      };
      // Three world axes and one center direction, then each solid contributes
      // two endpoint directions or three identity-frame directions.
      const auto count = 4U + (capsule(first_failure->first) ? 2U : 3U) +
                         (capsule(first_failure->second) ? 2U : 3U);
      const auto reason =
          !first_failure->arithmetic_supported
              ? BoardingSourceEndpointSelfCondition::unsupported_arithmetic
          : first_failure->examined_axes < count
              ? BoardingSourceEndpointSelfCondition::axis_capacity
              : BoardingSourceEndpointSelfCondition::unresolved_pair;
      check(d.first_refusal && d.first_refusal->pair_index == index &&
                d.first_refusal->condition == reason,
            "Reduced-axis refusal belongs to earliest actual incomplete pair "
            "and cause");
    } else {
      check(!d.first_refusal,
            "Complete reduced-axis graph has no invented refusal");
    }
  }
  for (std::size_t sites : std::array<std::size_t, 3>{0, 1, 9}) {
    const auto d = require(detail::boarding_source_endpoint_self_bounded(
        provider, sites, 1, 105, 14));
    diagnostic_checks(d);
    check(!d.endpoint.complete && !d.complete && !d.self_qualified &&
              d.examined_pairs == 0 && d.examined_axes == 0 &&
              d.signed_support_trials == 0,
          "Incomplete source prerequisite executes no self geometry or pair "
          "math");
    check(d.first_refusal &&
              d.first_refusal->condition ==
                  BoardingSourceEndpointSelfCondition::endpoint_prerequisite,
          "Source prefix refusal preserves exact first prerequisite category");
  }
  const auto empty = require(
      detail::boarding_source_endpoint_self_bounded(provider, 10, 0, 105, 14));
  diagnostic_checks(empty);
  check(!empty.endpoint.body && empty.examined_pairs == 0 && !empty.complete &&
            !empty.bindings_complete,
        "Body-capacity zero preserves honest unassembled self prefix");
  for (std::size_t value :
       std::array<std::size_t, 2>{11, std::numeric_limits<std::size_t>::max()})
    check(!detail::boarding_source_endpoint_self_bounded(provider, value, 1,
                                                         105, 14),
          "Source capacity cannot exceed original10");
  check(
      !detail::boarding_source_endpoint_self_bounded(provider, 10, 2, 105, 14),
      "Body capacity cannot exceed original1");
  check(
      !detail::boarding_source_endpoint_self_bounded(provider, 10, 1, 106, 14),
      "Pair capacity cannot enlarge original105 graph");
  check(
      !detail::boarding_source_endpoint_self_bounded(provider, 10, 1, 105, 15),
      "Proposal capacity cannot enlarge original14 order");
}

class SavedEnvironment {
 public:
  SavedEnvironment() {
    if (std::fegetenv(&saved_) != 0)
      throw std::runtime_error("Could not save arithmetic environment");
  }
  ~SavedEnvironment() { std::fesetenv(&saved_); }
  SavedEnvironment(const SavedEnvironment&) = delete;
  auto operator=(const SavedEnvironment&) -> SavedEnvironment& = delete;

 private:
  std::fenv_t saved_{};
};
#if defined(__SSE2__) && defined(__x86_64__)
class SavedControl {
 public:
  SavedControl() : saved(_mm_getcsr()) {}
  ~SavedControl() { _mm_setcsr(saved); }
  SavedControl(const SavedControl&) = delete;
  auto operator=(const SavedControl&) -> SavedControl& = delete;
  unsigned saved;
};
#endif
auto unsupported_environment(const OriginBoardingBootSupport& provider)
    -> void {
  const auto d = require(assess_origin_boarding_source_endpoint_self(provider));
  check(!d.arithmetic_supported && !d.bindings_complete &&
            !d.coverage_complete && !d.complete && !d.self_qualified &&
            d.examined_pairs == 0 && d.examined_axes == 0 &&
            d.signed_support_trials == 0,
        "Unsupported process arithmetic grants no geometry/work/self result");
  check(d.first_refusal.has_value(),
        "Unsupported arithmetic retains a genuine owned prerequisite refusal");
  authority(d);
  const auto s = require(detail::boarding_source_endpoint_self_support(
      provider, BoardingBodyPartId::pelvis, {1, 0, 0}));
  check(!s.arithmetic_supported,
        "Support seam respects same arithmetic environment requirement");
  const auto h = require(
      detail::boarding_source_endpoint_self_half_ray(1, 1, 2, {-1, -1}));
  check(!h.arithmetic_supported && !h.certified,
        "Half-ray seam cannot bypass process arithmetic guard");
}
auto environment_controls(const OriginBoardingBootSupport& provider) -> void {
  for (int rounding : std::array{FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO}) {
    SavedEnvironment saved;
    check(std::fesetround(rounding) == 0,
          "Directed rounding adversary installed");
    unsupported_environment(provider);
  }
#if defined(__SSE2__) && defined(__x86_64__)
  for (unsigned bit : std::array{15U, 6U}) {
    SavedEnvironment saved;
    SavedControl control;
    _mm_setcsr(control.saved | (1U << bit));
    unsupported_environment(provider);
  }
  for (unsigned rounding : std::array{1U, 2U, 3U}) {
    SavedEnvironment saved;
    SavedControl control;
    _mm_setcsr((control.saved & ~(3U << 13)) | (rounding << 13));
    unsupported_environment(provider);
  }
#endif
  check(
      std::fegetround() == FE_TONEAREST,
      "All environment adversaries restore nearest before later observations");
}
auto lifetime_controls(const OriginBoardingBootSupport& provider,
                       const BoardingSourceEndpointSelfDiagnostic& initial)
    -> void {
  const auto old = snapshot(initial);
  auto independent = [] {
    auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    auto source = require(make_origin_boarding_boot_support(binding));
    auto d = require(assess_origin_boarding_source_endpoint_self(source));
    auto moved = std::move(source);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Empty-handle API contract.
    check(!assess_origin_boarding_source_endpoint_self(source),
          "Moved-from immutable provider refuses new self assessment");
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Empty-handle API contract.
    check(!detail::boarding_source_endpoint_self_support(
              source, BoardingBodyPartId::pelvis, {1, 0, 0}),
          "Moved-from provider cannot supply numeric support");
    check(moved.selected_partitions().size() == 10,
          "Moved immutable provider retains whole fixed source selection");
    return d;
  }();
  check(snapshot(independent) == old,
        "All self/region/pair/source evidence survives complete "
        "caller/binding/provider destruction");
  auto copy = independent;
  auto moved = std::move(independent);
  check(snapshot(copy) == old && snapshot(moved) == old,
        "Copied/moved diagnostic owns complete child and fixed results");
  copy.self_qualified = true;
  copy.complete = true;
  copy.parts[0].binding.mass.weight = 0;
  copy.regions[0].limit_metres = 8;
  copy.pairs[0].certified = true;
  copy.endpoint.common_translation_y_metres = 8;
  if (copy.endpoint.body) copy.endpoint.body->points[0].lower.x = 64;
  check(snapshot(moved) == old, "Mutable report copy cannot change another "
                                "diagnostic or immutable source");
  check(snapshot(require(assess_origin_boarding_source_endpoint_self(
            copy.endpoint.sites.source))) == old,
        "Public evaluation consumes immutable provider, never caller report "
        "success/geometry");
  check(snapshot(require(
            assess_origin_boarding_source_endpoint_self(provider))) == old,
        "After numeric/cap/environment controls original public output is "
        "bitwise stable");
}
} // namespace
int main() {
  try {
    static_assert(
        sizeof(std::array<BoardingSourceEndpointSelfPair, 105>) +
            sizeof(std::array<BoardingSourceEndpointSelfRegion, 14>) <=
        16384);
    if (std::numeric_limits<long double>::digits < 64)
      throw std::runtime_error(
          "Independent finite geometry reference requires64 mantissa bits");
    const auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    const auto provider = require(make_origin_boarding_boot_support(binding));
    const auto full = observe(provider);
    // Added only after the identical first GCC/Clang discovery was recorded.
    check(full.complete && full.self_qualified && full.bindings_complete &&
              full.arithmetic_supported && full.coverage_complete &&
              full.examined_pairs == 105 && full.certified_pairs == 105 &&
              !full.first_refusal,
          "Observed fixed endpoint retains complete static self qualification");
    check(std::ranges::count_if(full.pairs,
                                [](const auto& pair) {
                                  return pair.certified && pair.whole_owned;
                                }) == 14,
          "Observed graph retains all14 original finite junction certificates");
    check(std::ranges::count_if(
              full.pairs,
              [](const auto& pair) {
                return pair.certified &&
                       pair.certificate ==
                           BoardingSourceEndpointSelfCertificate::
                               convex_support_plane;
              }) == 91,
          "Observed graph retains full separating support for91 other pairs");
    primitive_controls(provider);
    half_ray_controls();
    capacity_controls(provider, full);
    environment_controls(provider);
    lifetime_controls(provider, full);
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout << checks << " fixed source endpoint self checks, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
